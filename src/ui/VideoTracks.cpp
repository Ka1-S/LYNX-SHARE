#include "ui/VideoTracks.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/SessionIds.h"
#include "core/Song.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/Dialogs.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/ImageReadback.h"
#include "ui/PlaylistComponent.h"
#include "ui/TextCache.h"
#include "ui/Theme.h"
#include "ui/VideoMedia.h"
#include "ui/VisualEditors.h"

namespace lynx
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------
// Filmstrip thumbnails: up to ~300 small frames per file, decoded once in the background (low priority)
struct ThumbStrip
{
    double interval = 1.0; // seconds between thumbnails
    int w = 0, h = 0;
    std::vector<juce::Image> images;
    std::atomic<int> ready { 0 };
    std::atomic<bool> done { false };
    std::mutex lock;
};

std::mutex stripLock;
std::map<juce::String, std::shared_ptr<ThumbStrip>> strips;

juce::ThreadPool& thumbPool()
{
    static juce::ThreadPool pool (juce::ThreadPoolOptions {}.withThreadName ("LYNX video thumbnails").withNumberOfThreads (1)
                                                            .withDesiredThreadPriority (juce::Thread::Priority::background));
    return pool;
}

void makeThumbs (const juce::String& path, std::shared_ptr<ThumbStrip> strip, int stride = 8)
{
    const auto pr = media::probe (path);
    const auto& info = pr.info;
    if (! info.hasVideo || info.duration <= 0.0 || info.width <= 0 || info.height <= 0)
    {
        strip->done = true;
        return;
    }
    const int h = 54;
    const int w = juce::jlimit (24, 128, juce::roundToInt (h * (double) info.width / info.height)) & ~1;
    const auto interval = std::max (0.5, info.duration / 300.0);
    const auto count = (int) std::ceil (info.duration / interval);
    if (stride == 8)
    {
        std::lock_guard<std::mutex> l (strip->lock);
        strip->w = w;
        strip->h = h;
        strip->interval = interval;
        strip->images.resize ((std::size_t) count);
    }
    auto store = [&] (int i, const std::uint8_t* bgra, int sw, int sh) {
        juce::Image img (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
        {
            juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
            media::scaleBgra (bgra, sw, sh, sw * 4, data.getLinePointer (0), w, h, data.lineStride);
        }
        std::lock_guard<std::mutex> l (strip->lock);
        if (i >= 0 && i < (int) strip->images.size())
            strip->images[(std::size_t) i] = img;
        strip->ready = std::max (strip->ready.load(), i + 1);
    };
    if (pr.backend != "ffmpeg")
    {
        if (auto reader = media::openNative (juce::File (path)))
        {
            std::vector<std::uint8_t> bgra;
            const auto frameDur = 1.0 / (info.fps > 1.0 ? info.fps : 30.0);
            // coarse to fine (every 8th thumbnail, then 4th, 2nd, all), each pass a new job: every video on the
            // Playlist gets a whole strip quickly, not one file's strip after another's (round 174d review)
            for (int i = 0; i < count; i += stride)
            {
                {
                    std::lock_guard<std::mutex> l (strip->lock);
                    if (strip->images[(std::size_t) i].isValid())
                        continue;
                }
                const auto t = std::min (info.duration - frameDur, i * interval + interval * 0.5);
                reader->seek (t);
                int fw = 0, fh = 0, reads = 0;
                double ft = -1.0;
                bool got = false;
                // (a seek lands on a key frame before t: decode on to t, at most a few seconds' worth)
                while (reads++ < 240 && reader->next (bgra, fw, fh, ft))
                {
                    got = true;
                    if (ft + frameDur > t)
                        break;
                }
                if (got)
                    store (i, bgra.data(), fw, fh);
            }
            if (stride > 1)
                thumbPool().addJob ([path, strip, stride] { makeThumbs (path, strip, stride / 2); });
            else
                strip->done = true;
            return;
        }
    }
    // ffmpeg: one process for the whole strip
    const auto ffmpeg = findFfmpeg();
    if (ffmpeg.existsAsFile())
    {
        juce::ChildProcess proc;
        if (proc.start (juce::StringArray { ffmpeg.getFullPathName(), "-nostdin", "-v", "quiet", "-i", path, "-an", "-sn", "-vf",
                                            "fps=1/" + juce::String (interval, 6) + ",scale=" + juce::String (w) + ":" + juce::String (h),
                                            "-pix_fmt", "bgra", "-f", "rawvideo", "-" },
                        juce::ChildProcess::wantStdOut))
        {
            std::vector<std::uint8_t> bytes ((std::size_t) w * (std::size_t) h * 4);
            for (int i = 0; i < count; ++i)
            {
                std::size_t got = 0;
                while (got < bytes.size())
                {
                    const auto n = proc.readProcessOutput (bytes.data() + got, (int) (bytes.size() - got));
                    if (n <= 0)
                        break;
                    got += (std::size_t) n;
                }
                if (got < bytes.size())
                    break;
                store (i, bytes.data(), w, h);
            }
            proc.kill();
        }
    }
    strip->done = true;
}

std::shared_ptr<ThumbStrip> thumbsFor (const juce::String& path)
{
    std::lock_guard<std::mutex> lock (stripLock);
    auto& s = strips[path];
    if (s == nullptr)
    {
        s = std::make_shared<ThumbStrip>();
        thumbPool().addJob ([path, strip = s] { makeThumbs (path, strip); });
    }
    return s;
}

juce::Image thumbAt (ThumbStrip& strip, double fileSeconds)
{
    std::lock_guard<std::mutex> l (strip.lock);
    if (strip.images.empty())
        return {};
    const auto n = (int) strip.images.size();
    const auto i = juce::jlimit (0, n - 1, (int) std::floor (fileSeconds / strip.interval));
    // a thumbnail is made as a software image (on the thumbnails' thread) and drawn on every paint of the Playlist: the
    // first time it is drawn it becomes the window's kind of image, so its GPU copy is made once (round 190gv)
    auto shown = [&] (int k) -> juce::Image {
        auto& image = strip.images[(std::size_t) k];
        image = ui::forScreen (image);
        return image;
    };
    // (not decoded yet: the nearest decoded one, earlier first)
    for (int d = 0; d < n; ++d)
    {
        if (i - d >= 0 && strip.images[(std::size_t) (i - d)].isValid())
            return shown (i - d);
        if (i + d < n && strip.images[(std::size_t) (i + d)].isValid())
            return shown (i + d);
    }
    return {};
}

juce::String trackName (const Song& song, int track)
{
    const auto& n = song.videoTracks[(std::size_t) track].name;
    return n.empty() ? tr ("Video {0}", { juce::String (track + 1) }) : juce::String::fromUTF8 (n.c_str());
}

juce::Colour trackColour (const Song& song, int track)
{
    const auto c = song.videoTracks[(std::size_t) track].colour;
    return c != 0 ? juce::Colour (c) : juce::Colour (0xff4f7a8f); // (a steel blue: video, apart from the audio clips)
}

videotracks::Overlay& overlay()
{
    static videotracks::Overlay o;
    return o;
}

double playheadSeconds (AudioEngine& engine)
{
    return engine.getSong().secondsForTicks ((std::int64_t) std::llround (std::max (0.0, engine.songPlayheadTicks())));
}

// ---------------------------------------------------------------------------------------------------------------
// The video window of one track
class VideoWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    VideoWindow (AudioEngine& e, int t) : juce::DocumentWindow ("Video", juce::Colours::black, juce::DocumentWindow::allButtons), track (t), engine (e)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setContentNonOwned (&surface, false);
        surface.owner = this;
        centreWithSize (640, 360);
        decoder.startThread (juce::Thread::Priority::low);
        setVisible (true);
        startTimerHz (60);
        timerCallback();
    }

    ~VideoWindow() override { setContentNonOwned (nullptr, false); }

    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([t = track] { closeWindow (t); });
    }

    int track;
    static std::map<int, std::unique_ptr<VideoWindow>>& all()
    {
        static std::map<int, std::unique_ptr<VideoWindow>> windows;
        return windows;
    }
    static void closeWindow (int t) { all().erase (t); }

private:
    struct Surface final : public juce::Component
    {
        VideoWindow* owner = nullptr;
        juce::Image image;
        double seconds = 0.0;
        juce::String message, timecode, title;
        bool keepAspect = true;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black);
            if (image.isValid())
            {
                if (keepAspect)
                    g.drawImageWithin (image, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred, false);
                else
                    g.drawImage (image, getLocalBounds().toFloat());
            }
            else
            {
                g.setColour (theme::colour::textSecondary);
                g.setFont (theme::font (theme::type::body));
                g.drawFittedText (message, getLocalBounds().reduced (16), juce::Justification::centred, 12);
            }
            if (overlay())
            {
                auto picture = getLocalBounds();
                if (image.isValid() && keepAspect)
                    picture = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                  .appliedTo (image.getBounds().toDouble(), getLocalBounds().toDouble()).toNearestInt();
                overlay() (g, picture, seconds);
            }
            if (timecode.isNotEmpty())
            {
                // the burn-in: white on a black box, bottom centre (as a video editor's window burn)
                const auto fh = juce::jlimit (14.0f, 48.0f, (float) getHeight() * 0.06f);
                g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), fh, juce::Font::bold));
                const auto w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), timecode)) + 16;
                const auto box = juce::Rectangle<int> ((getWidth() - w) / 2, getHeight() - (int) (fh * 1.9f), w, (int) (fh * 1.4f));
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.fillRect (box);
                g.setColour (juce::Colours::white);
                g.drawText (timecode, box, juce::Justification::centred, false);
            }
        }

        void resized() override
        {
            if (owner != nullptr)
                owner->sizeDecoder();
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (owner != nullptr)
                owner->setFullScreen (! owner->isFullScreen());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (owner != nullptr && e.mods.isPopupMenu())
                owner->showMenu();
        }
    };

    void sizeDecoder()
    {
        const auto scale = (double) juce::jmax (1.0f, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                                          ? (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                                                          : 1.0f);
        auto w = surface.getWidth(), h = surface.getHeight();
        if (info.width > 0 && info.height > 0)
        {
            const auto fit = std::min ((double) w / info.width, (double) h / info.height);
            w = juce::roundToInt (info.width * fit);
            h = juce::roundToInt (info.height * fit);
        }
        decoder.setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
    }

    void showMenu()
    {
        juce::PopupMenu m;
        const auto& song = engine.getSong();
        m.addItem (tr ("Burn-in timecode"), true, song.video.burnIn, [this] {
            engine.editSong ([] (Song& s) { s.video.burnIn = ! s.video.burnIn; });
        });
        m.addItem (tr ("Keep aspect ratio"), true, surface.keepAspect, [this] {
            surface.keepAspect = ! surface.keepAspect;
            surface.repaint();
        });
        m.addItem (tr ("Always on top"), true, isAlwaysOnTop(), [this] { setAlwaysOnTop (! isAlwaysOnTop()); });
        m.addItem (tr ("Full screen"), true, isFullScreen(), [this] { setFullScreen (! isFullScreen()); });
        m.addSeparator();
        for (const auto pct : { 50, 100, 200 })
            m.addItem (tr ("Video size {0}%", { juce::String (pct) }), info.width > 0, false, [this, pct] {
                setFullScreen (false);
                const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
                const auto scale = d != nullptr ? d->scale : 1.0;
                const auto border = getBorderThickness();
                setSize (juce::roundToInt (info.width * pct / 100.0 / scale) + border.getLeftAndRight(),
                         juce::roundToInt (info.height * pct / 100.0 / scale) + border.getTopAndBottom() + getTitleBarHeight());
            });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&surface).withMousePosition());
    }

    void timerCallback() override
    {
        const auto& song = engine.getSong();
        if (track < 0 || track >= (int) song.videoTracks.size())
        {
            juce::MessageManager::callAsync ([t = track] { closeWindow (t); });
            stopTimer();
            return;
        }
        const auto seconds = playheadSeconds (engine);
        if (overlay() && std::abs (seconds - surface.seconds) > 1e-6)
            surface.repaint(); // (the cue signals move with the play position)
        surface.seconds = seconds;
        const auto& vt = song.videoTracks[(std::size_t) track];
        const auto title = trackName (song, track);
        const auto show = videotrack::showing (vt, seconds);
        juce::String path;
        if (show)
        {
            path = juce::String::fromUTF8 (vt.clips[(std::size_t) show->clip].path.c_str());
            path = platform::localFile (path).getFullPathName();
        }
        if (path != shownPath)
        {
            shownPath = path;
            info = path.isNotEmpty() ? media::probe (path).info : video::Info {};
            decoder.setSource (info.hasVideo ? path : juce::String(), info);
            sizeDecoder();
            frames = -1;
        }
        const auto name = title + (show ? juce::String (" - ") + juce::File (path).getFileName() : juce::String());
        if (name != getName())
            setName (name);
        if (show && info.hasVideo)
        {
            decoder.request (show->fileSeconds, engine.getTransport().playing.load());
            double ft = 0.0;
            auto img = decoder.frame (ft);
            if (img.getPixelData() != surface.image.getPixelData() || frames != decoder.frameCount())
            {
                frames = decoder.frameCount();
                surface.image = img;
                surface.repaint();
            }
            surface.message = {};
        }
        else
        {
            juce::String msg;
            if (! show)
                msg = vt.muted ? tr ("This video track is muted") : tr ("No video at this position");
            else if (! juce::File (path).existsAsFile())
                msg = tr ("The video was not found:") + "\n" + path;
            else
                msg = media::cannotReadMessage (path);
            if (surface.image.isValid() || msg != surface.message)
            {
                surface.image = {};
                surface.message = msg;
                surface.repaint();
            }
        }
        const auto tc = song.video.burnIn ? juce::String (vtime::format (videotrack::timecodeOf (song.video, seconds), song.video.rate)) : juce::String();
        if (tc != surface.timecode)
        {
            surface.timecode = tc;
            surface.repaint();
        }
    }

    AudioEngine& engine;
    Surface surface;
    media::FrameDecoder decoder;
    juce::String shownPath;
    video::Info info;
    int frames = -1;
};

const juce::StringArray videoExtensions { "mp4", "m4v", "mov", "mkv", "webm", "avi", "wmv", "mxf", "mpg", "mpeg", "m2v", "ts", "m2ts", "mts", "3gp", "flv", "dv", "ogv" };
} // namespace

// ---------------------------------------------------------------------------------------------------------------
namespace videotracks
{
bool isVideoFile (const juce::File& file)
{
    return videoExtensions.contains (file.getFileExtension().trimCharactersAtStart (".").toLowerCase());
}

bool importVideo (AudioEngine& engine, const juce::File& file, double songSeconds, int track)
{
    if (! file.existsAsFile())
        return false;
    const auto pr = media::probe (file.getFullPathName());
    if (! pr.info.hasVideo)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Import video file"),
                                                file.getFileName() + "\n\n" + media::cannotReadMessage (file.getFullPathName()));
        return false;
    }
    const auto path = file.getFullPathName().toStdString();
    const auto duration = pr.info.duration > 0.0 ? pr.info.duration : 10.0;
    const auto fps = pr.info.fps;
    const auto start = std::max (0.0, songSeconds);
    engine.editSong ([=] (Song& s) {
        auto t = track;
        if (t < 0 || t >= (int) s.videoTracks.size())
        {
            // the first track with nothing in the clip's span, else a new one
            t = -1;
            for (int i = 0; i < (int) s.videoTracks.size() && t < 0; ++i)
                if (std::none_of (s.videoTracks[(std::size_t) i].clips.begin(), s.videoTracks[(std::size_t) i].clips.end(),
                                  [&] (const VideoClip& c) { return c.start < start + duration && c.end() > start; }))
                    t = i;
            if (t < 0)
            {
                s.videoTracks.push_back ({});
                t = (int) s.videoTracks.size() - 1;
            }
        }
        if (s.video.rateFromVideo)
            if (const auto r = vtime::nearestRate (fps))
            {
                s.video.rate = *r;
                s.video.rateFromVideo = false; // (the first video decides; then it is the user's)
            }
        videotrack::addClip (s.videoTracks[(std::size_t) t], path, videotrack::snapToFrame (s.video, start), duration, uids::next (s));
        s.mode = PlayMode::song;
    });
    return true;
}

void chooseAndImport (AudioEngine& engine, juce::Component*)
{
    static std::unique_ptr<juce::FileChooser> chooser;
    juce::StringArray patterns;
    for (const auto& e : videoExtensions)
        patterns.add ("*." + e);
    chooser = ui::makeFileChooser (tr ("Import video file"), juce::File(), patterns.joinIntoString (";"));
    ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [&engine] (const juce::FileChooser& fc) {
        if (fc.getResult().existsAsFile())
            importVideo (engine, fc.getResult(), engine.getSong().mode == PlayMode::song ? playheadSeconds (engine) : 0.0);
    });
}

void showWindow (AudioEngine& engine, int track)
{
    auto& windows = VideoWindow::all();
    if (auto it = windows.find (track); it != windows.end())
    {
        it->second->toFront (true);
        return;
    }
    if (track < 0 || track >= (int) engine.getSong().videoTracks.size())
        return;
    windows[track] = std::make_unique<VideoWindow> (engine, track);
}

void setOverlay (Overlay o)
{
    overlay() = std::move (o);
    for (auto& [t, w] : VideoWindow::all())
        w->repaint();
}

void syncWindows (AudioEngine& engine)
{
    auto& windows = VideoWindow::all();
    for (auto it = windows.begin(); it != windows.end();)
        if (it->first >= (int) engine.getSong().videoTracks.size())
            it = windows.erase (it);
        else
            ++it;
}
} // namespace videotracks

// ---------------------------------------------------------------------------------------------------------------
VideoLanesView::VideoLanesView (AudioEngine& e, PlaylistComponent& p) : engine (e), playlist (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    refresh();
    startTimerHz (30);
}

VideoLanesView::~VideoLanesView() = default;

int VideoLanesView::contentHeight (const Song& song)
{
    if (song.videoTracks.empty())
        return 0;
    int h = dividerHeight + (song.video.showTimecodeRuler ? timecodeHeight : 0);
    for (const auto& t : song.videoTracks)
        h += t.height > 0 ? t.height : defaultLaneHeight;
    return h;
}

void VideoLanesView::refresh()
{
    const auto& song = engine.getSong();
    map = videotrack::TempoMap (song, std::max (videotrack::videoEnd (song.videoTracks), song.secondsForTicks (song.songLength())) + 600.0);
    videotracks::syncWindows (engine);
    repaint();
}

int VideoLanesView::laneHeight (int track) const
{
    const auto& tracks = engine.getSong().videoTracks;
    if (track < 0 || track >= (int) tracks.size())
        return defaultLaneHeight;
    return tracks[(std::size_t) track].height > 0 ? tracks[(std::size_t) track].height : defaultLaneHeight;
}

int VideoLanesView::laneTop (int track) const
{
    int y = engine.getSong().video.showTimecodeRuler ? timecodeHeight : 0;
    for (int i = 0; i < track; ++i)
        y += laneHeight (i);
    return y;
}

double VideoLanesView::secondsAt (int x) const { return map.seconds (std::max (0.0, playlist.xToTick (x))); }

int VideoLanesView::xOf (double songSeconds) const { return (int) std::floor (playlist.tickToX (map.ticks (songSeconds))); }

VideoLanesView::Hit VideoLanesView::hitAt (juce::Point<int> p) const
{
    Hit h;
    const auto& song = engine.getSong();
    if (p.y >= getHeight() - dividerHeight)
    {
        h.part = Hit::divider;
        return h;
    }
    if (song.video.showTimecodeRuler && p.y < timecodeHeight)
    {
        h.part = p.x >= playlist.headerWidth ? Hit::timecode : Hit::none;
        return h;
    }
    for (int t = 0; t < (int) song.videoTracks.size(); ++t)
        if (p.y >= laneTop (t) && p.y < laneTop (t) + laneHeight (t))
        {
            h.track = t;
            if (p.x < playlist.headerWidth)
            {
                h.part = p.x < 18 ? Hit::mute : Hit::header;
                return h;
            }
            const auto& clips = song.videoTracks[(std::size_t) t].clips;
            for (int c = (int) clips.size() - 1; c >= 0; --c)
            {
                const auto x0 = xOf (clips[(std::size_t) c].start), x1 = xOf (clips[(std::size_t) c].end());
                if (p.x >= x0 - 1 && p.x <= x1 + 1)
                {
                    h.clip = c;
                    const auto edge = std::min (6, std::max (2, (x1 - x0) / 4));
                    h.part = p.x <= x0 + edge ? Hit::head : p.x >= x1 - edge ? Hit::tail : Hit::body;
                    return h;
                }
            }
            h.part = Hit::none;
            return h;
        }
    return h;
}

void VideoLanesView::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto& song = engine.getSong();
    const auto gridX = playlist.headerWidth;
    const auto gridW = getWidth() - gridX - PlaylistComponent::scrollBarSize;
    const auto lanesBottom = getHeight() - dividerHeight;
    g.fillAll (colour::bg1);
    const auto rate = song.video.rate;

    // --- the timecode row ------------------------------------------------------------------------------------------
    if (song.video.showTimecodeRuler)
    {
        const auto row = juce::Rectangle<int> (gridX, 0, gridW, timecodeHeight);
        g.setColour (colour::bg2);
        g.fillRect (0, 0, getWidth(), timecodeHeight);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        textcache::drawText (g, "TC " + juce::String (vtime::rateName (rate)), 4, 0, gridX - 6, timecodeHeight, juce::Justification::centredLeft, true);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (row);
        const auto from = secondsAt (gridX), to = secondsAt (gridX + gridW);
        const auto pxPerSecond = std::max (1e-6, (double) gridW / std::max (1e-6, to - from));
        // label spacing: frames, then seconds, then minutes, at least ~90 px apart
        const double frame = 1.0 / rate.fps();
        double step = 3600.0;
        for (const auto s : { frame, 2 * frame, 5 * frame, 10 * frame, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0 })
            if (s * pxPerSecond >= 90.0)
            {
                step = s;
                break;
            }
        const auto tcFrom = videotrack::timecodeOf (song.video, std::max (0.0, from));
        for (auto tc = std::floor (tcFrom / step) * step; videotrack::songOf (song.video, tc) <= to; tc += step)
        {
            const auto x = xOf (videotrack::songOf (song.video, tc));
            g.setColour (colour::borderStrong);
            g.fillRect (x, timecodeHeight - 5, 1, 5);
            g.setColour (colour::textSecondary);
            textcache::drawText (g, vtime::format (tc, rate), x + 3, 0, 110, timecodeHeight, juce::Justification::centredLeft, false);
        }
        g.setColour (colour::border);
        g.fillRect (0, timecodeHeight - 1, getWidth(), 1);
    }

    // --- the lanes -------------------------------------------------------------------------------------------------
    for (int t = 0; t < (int) song.videoTracks.size(); ++t)
    {
        const auto& vt = song.videoTracks[(std::size_t) t];
        const auto top = laneTop (t), lh = laneHeight (t);
        if (top >= lanesBottom)
            break;
        const auto lane = juce::Rectangle<int> (gridX, top, gridW, std::min (lh, lanesBottom - top));
        g.setColour (t % 2 == 0 ? colour::bg0 : colour::bg0.brighter (0.03f));
        g.fillRect (lane);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (lane);
            const auto base = trackColour (song, t);
            for (int c = 0; c < (int) vt.clips.size(); ++c)
            {
                const auto& clip = vt.clips[(std::size_t) c];
                const auto x0 = xOf (clip.start), x1 = std::max (x0 + 2, xOf (clip.end()));
                if (x1 < lane.getX() || x0 > lane.getRight())
                    continue;
                const auto box = juce::Rectangle<int> (x0, top + 1, x1 - x0, lh - 3);
                const auto colour = vt.muted ? colour::noteMuted : base;
                const auto hovered = hover.track == t && hover.clip == c;
                // FL's clip: a title bar in the clip's colour and a darker body
                const auto titleH = std::min (14, box.getHeight() / 3);
                g.setColour (colour.darker (0.75f));
                g.fillRect (box);
                const auto body = box.withTrimmedTop (titleH).reduced (1, 1);
                if (auto strip = thumbsFor (juce::String::fromUTF8 (clip.path.c_str())); strip->h > 0 && body.getHeight() > 6)
                {
                    juce::Graphics::ScopedSaveState s2 (g);
                    g.reduceClipRegion (body);
                    const auto tw = std::max (8, juce::roundToInt ((double) strip->w * body.getHeight() / strip->h));
                    // slots aligned to the clip's start; only the visible ones
                    const auto first = std::max (0, (lane.getX() - body.getX()) / tw);
                    for (int x = body.getX() + first * tw; x < std::min (body.getRight(), lane.getRight()); x += tw)
                    {
                        const auto songT = secondsAt (x + tw / 2);
                        const auto fileT = clip.offset + (songT - clip.start);
                        if (auto img = thumbAt (*strip, fileT); img.isValid())
                        {
                            g.setOpacity (vt.muted ? 0.35f : 1.0f);
                            g.drawImage (img, juce::Rectangle<float> ((float) x, (float) body.getY(), (float) tw, (float) body.getHeight()),
                                         juce::RectanglePlacement::stretchToFit);
                        }
                    }
                    g.setOpacity (1.0f);
                }
                g.setColour (colour.withAlpha (hovered ? 1.0f : 0.92f));
                g.fillRect (box.withHeight (titleH));
                g.setColour (colour::textPrimary);
                g.setFont (font (type::caption));
                // the menu icon (FL's clip menu) and the file name
                g.fillRect (box.getX() + 3, box.getY() + titleH / 2 - 3, 6, 1);
                g.fillRect (box.getX() + 3, box.getY() + titleH / 2, 6, 1);
                g.fillRect (box.getX() + 3, box.getY() + titleH / 2 + 3, 6, 1);
                textcache::drawText (g, juce::File (juce::String::fromUTF8 (clip.path.c_str())).getFileName(), box.getX() + 12, box.getY(),
                                     box.getWidth() - 14, titleH, juce::Justification::centredLeft, true);
                g.setColour (hovered ? colour::textPrimary.withAlpha (0.8f) : colour.brighter (0.2f).withAlpha (0.8f));
                g.drawRect (box, 1);
                if (hovered && (hover.part == Hit::head || hover.part == Hit::tail))
                {
                    g.setColour (colour::accent);
                    g.fillRect (hover.part == Hit::head ? box.getX() : box.getRight() - 2, box.getY(), 2, box.getHeight());
                }
            }
        }
        g.setColour (colour::border);
        g.fillRect (gridX, top + lh - 1, gridW, 1);

        // the header (FL's track header: a plate with the name; the LED mutes)
        const auto plate = juce::Rectangle<float> (2.0f, (float) top + 1.0f, (float) playlist.headerWidth - 4.0f, (float) lh - 2.0f);
        g.setColour (colour::bg2);
        g.fillRect (0, top, playlist.headerWidth, lh);
        g.setColour (hover.track == t && hover.part == Hit::header ? colour::bg4 : colour::bg3);
        g.fillRoundedRectangle (plate, radius::sm);
        g.setColour (trackColour (song, t));
        g.fillRoundedRectangle (plate.withX (plate.getRight() - 4.0f).withWidth (3.0f).reduced (0.0f, 3.0f), 1.5f);
        g.setColour (vt.muted ? colour::ledOff : colour::ledOn);
        g.fillEllipse (6.0f, (float) top + 7.0f, 7.0f, 7.0f);
        g.setColour (vt.muted ? colour::textSecondary : colour::textPrimary);
        g.setFont (font (type::caption));
        textcache::drawFittedText (g, trackName (song, t), 18, top + 2, playlist.headerWidth - 26, 16, juce::Justification::centredLeft, 1, 0.8f);
        // the frame shown at the playhead, small, under the name (when there is room)
        if (lh >= 44)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption).withHeight (10.0f));
            juce::String status = vt.clips.empty() ? tr ("empty") : tr ("{0} clips", { juce::String ((int) vt.clips.size()) });
            textcache::drawText (g, status, 8, top + 20, playlist.headerWidth - 16, 14, juce::Justification::centredLeft, true);
        }
    }
    g.setColour (colour::border);
    g.fillRect (playlist.headerWidth - 1, 0, 1, lanesBottom);

    // --- the playhead ----------------------------------------------------------------------------------------------
    if (playlist.playheadTick >= 0.0)
    {
        g.setColour (colour::playhead);
        const auto x = (float) playlist.tickToX (playlist.playheadTick);
        if (x >= (float) gridX)
            g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 1.5f, (float) lanesBottom));
    }

    // --- the divider -----------------------------------------------------------------------------------------------
    const auto strip = juce::Rectangle<int> (0, lanesBottom, getWidth(), dividerHeight);
    g.setColour (hover.part == Hit::divider || drag == Drag::divider ? colour::bg5 : colour::bg3);
    g.fillRect (strip);
    g.setColour (colour::borderStrong);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void VideoLanesView::timerCallback()
{
    // the playhead (moves with the Playlist's) and thumbnails arriving
    const auto x = playlist.playheadTick >= 0.0 ? (int) playlist.tickToX (playlist.playheadTick) : -1;
    if (x != lastPlayheadX)
    {
        repaint (juce::Rectangle<int> (std::min (x, lastPlayheadX) - 2, 0, std::abs (x - lastPlayheadX) + 5, getHeight()));
        lastPlayheadX = x;
    }
    bool pending = false;
    {
        std::lock_guard<std::mutex> lock (stripLock);
        for (const auto& [path, s] : strips)
            if (! s->done)
                pending = true;
    }
    if (pending || std::exchange (thumbsWerePending, pending))
        repaint();
}

void VideoLanesView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.getPosition());
    if (h.track != hover.track || h.clip != hover.clip || h.part != hover.part)
    {
        hover = h;
        repaint();
    }
    switch (h.part)
    {
        case Hit::head:
        case Hit::tail: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Hit::divider: setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
        case Hit::body: setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        default: setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void VideoLanesView::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void VideoLanesView::mouseDown (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.getPosition());
    drag = Drag::none;
    const auto& song = engine.getSong();
    if (h.part == Hit::divider)
    {
        drag = Drag::divider;
        dragStartHeight = getHeight();
        return;
    }
    if (h.part == Hit::timecode)
    {
        // click on the timecode row: the play position there (as on the Playlist's ruler)
        engine.seek (map.ticks (secondsAt (e.x)));
        return;
    }
    if (h.part == Hit::mute && ! e.mods.isPopupMenu())
    {
        engine.editSong ([t = h.track] (Song& s) { s.videoTracks[(std::size_t) t].muted = ! s.videoTracks[(std::size_t) t].muted; });
        return;
    }
    if (h.part == Hit::header || h.part == Hit::mute)
    {
        if (e.mods.isPopupMenu())
            showHeaderMenu (h.track);
        return;
    }
    if (h.track < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        if (h.clip >= 0 && ! e.mods.isShiftDown())
        {
            // FL's Playlist: right click deletes the clip
            engine.editSong ([t = h.track, c = h.clip] (Song& s) {
                auto& clips = s.videoTracks[(std::size_t) t].clips;
                if (c < (int) clips.size())
                    clips.erase (clips.begin() + c);
            });
            hover = {};
            return;
        }
        showMenu (h, e.getPosition());
        return;
    }
    if (h.clip < 0)
        return;
    const auto& clip = song.videoTracks[(std::size_t) h.track].clips[(std::size_t) h.clip];
    // the title bar's menu icon
    if (e.x < xOf (clip.start) + 12 && e.y < laneTop (h.track) + 15 && h.part != Hit::head)
    {
        showMenu (h, e.getPosition());
        return;
    }
    dragTrack = h.track;
    dragClip = h.clip;
    grabOffset = secondsAt (e.x) - clip.start;
    drag = h.part == Hit::head ? Drag::head : h.part == Hit::tail ? Drag::tail : Drag::move;
}

void VideoLanesView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::divider)
    {
        playlist.setVideoHeight (dragStartHeight + e.getDistanceFromDragStartY());
        return;
    }
    if (drag == Drag::none)
        return;
    const auto& song = engine.getSong();
    if (dragTrack >= (int) song.videoTracks.size() || dragClip >= (int) song.videoTracks[(std::size_t) dragTrack].clips.size())
        return;
    const auto free = e.mods.isAltDown();
    auto at = secondsAt (e.x);
    if (drag == Drag::move)
        at -= grabOffset;
    if (! free)
        at = videotrack::snapToFrame (song.video, std::max (0.0, at));
    const auto frame = 1.0 / song.video.rate.fps();
    const auto kind = drag;
    auto newIndex = dragClip;
    engine.editSong ([t = dragTrack, c = dragClip, at, frame, kind, &newIndex] (Song& s) {
        auto& vt = s.videoTracks[(std::size_t) t];
        if (kind == Drag::move)
            newIndex = videotrack::moveClip (vt, c, at);
        else if (kind == Drag::head)
            videotrack::trimStart (vt.clips[(std::size_t) c], at, frame);
        else
            videotrack::trimEnd (vt.clips[(std::size_t) c], at, frame);
    });
    if (newIndex >= 0)
        dragClip = newIndex;
}

void VideoLanesView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
    dragTrack = dragClip = -1;
}

void VideoLanesView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.getPosition());
    if (h.part == Hit::divider)
    {
        playlist.setVideoHeight (0); // fit the lanes
        return;
    }
    if (h.part == Hit::header)
    {
        showHeaderMenu (h.track);
        return;
    }
    if (h.track >= 0)
        videotracks::showWindow (engine, h.track);
}

void VideoLanesView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    playlist.mouseWheelMove (e.getEventRelativeTo (&playlist), wheel); // (time scroll and zoom: the Playlist's)
}

bool VideoLanesView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (videotracks::isVideoFile (juce::File (f)))
            return true;
    return false;
}

void VideoLanesView::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto h = hitAt ({ x, y });
    auto track = h.track;
    for (const auto& f : files)
        if (videotracks::isVideoFile (juce::File (f)) && videotracks::importVideo (engine, juce::File (f), secondsAt (std::max (x, playlist.headerWidth)), track))
            track = -1; // (more files: where there is room)
}

void VideoLanesView::showHeaderMenu (int track)
{
    if (track < 0)
        return;
    juce::PopupMenu m;
    const auto& song = engine.getSong();
    m.addItem (tr ("Rename and color..."), [this, track] {
        const auto& s = engine.getSong();
        if (track >= (int) s.videoTracks.size())
            return;
        showNameAndColourDialog (tr ("Rename video track"), trackName (s, track), s.videoTracks[(std::size_t) track].colour,
                                 [this, track] (const juce::String& name, std::uint32_t colour) {
                                     engine.editSong ([track, n = name.trim().toStdString(), colour] (Song& s2) {
                                         if (track < (int) s2.videoTracks.size())
                                         {
                                             s2.videoTracks[(std::size_t) track].name = n;
                                             s2.videoTracks[(std::size_t) track].colour = colour;
                                         }
                                     });
                                 });
    });
    m.addItem (tr ("Mute"), true, song.videoTracks[(std::size_t) track].muted, [this, track] {
        engine.editSong ([track] (Song& s) { s.videoTracks[(std::size_t) track].muted = ! s.videoTracks[(std::size_t) track].muted; });
    });
    m.addItem (tr ("Open the video window"), [this, track] { videotracks::showWindow (engine, track); });
    juce::PopupMenu size;
    for (const auto px : { 36, 60, 90, 140 })
        size.addItem (juce::String (px) + " px", true, laneHeight (track) == px, [this, track, px] {
            engine.editSong ([track, px] (Song& s) { s.videoTracks[(std::size_t) track].height = px; });
            playlist.setVideoHeight (0);
        });
    m.addSubMenu (tr ("Size"), size);
    m.addSeparator();
    m.addItem (tr ("Add a video track"), [this] {
        engine.editSong ([] (Song& s) { s.videoTracks.push_back ({}); });
    });
    m.addItem (tr ("Delete this video track"), [this, track] {
        engine.editSong ([track] (Song& s) {
            if (track < (int) s.videoTracks.size())
                s.videoTracks.erase (s.videoTracks.begin() + track);
        });
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void VideoLanesView::showMenu (const Hit& h, juce::Point<int> at)
{
    juce::PopupMenu m;
    const auto& song = engine.getSong();
    const auto when = secondsAt (at.x);
    if (h.track >= 0 && h.clip >= 0)
    {
        const auto t = h.track, c = h.clip;
        m.addSectionHeader (juce::File (juce::String::fromUTF8 (song.videoTracks[(std::size_t) t].clips[(std::size_t) c].path.c_str())).getFileName());
        m.addItem (tr ("Split at the play position"), [this, t, c] {
            const auto split = videotrack::snapToFrame (engine.getSong().video, playheadSeconds (engine));
            engine.editSong ([t, c, split] (Song& s) {
                if (t < (int) s.videoTracks.size())
                    videotrack::splitClip (s.videoTracks[(std::size_t) t], c, split, uids::next (s));
            });
        });
        m.addItem (tr ("Move the clip's start to the play position"), [this, t, c] {
            const auto start = videotrack::snapToFrame (engine.getSong().video, playheadSeconds (engine));
            engine.editSong ([t, c, start] (Song& s) {
                if (t < (int) s.videoTracks.size())
                    videotrack::moveClip (s.videoTracks[(std::size_t) t], c, start);
            });
        });
        m.addItem (tr ("Clip start timecode..."), [this, t, c] {
            const auto& s = engine.getSong();
            const auto& clip = s.videoTracks[(std::size_t) t].clips[(std::size_t) c];
            auto* w = new juce::AlertWindow (tr ("Clip start timecode"), tr ("Where the clip starts (hours:minutes:seconds:frames)"), juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("tc", vtime::format (videotrack::timecodeOf (s.video, clip.start), s.video.rate));
            w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, t, c] (int r) {
                if (r != 1)
                    return;
                const auto& s2 = engine.getSong();
                if (const auto tc = vtime::parseSeconds (w->getTextEditorContents ("tc").toStdString(), s2.video.rate))
                    engine.editSong ([t, c, start = videotrack::songOf (s2.video, *tc)] (Song& s3) {
                        if (t < (int) s3.videoTracks.size() && c < (int) s3.videoTracks[(std::size_t) t].clips.size())
                            videotrack::moveClip (s3.videoTracks[(std::size_t) t], c, std::max (0.0, start));
                    });
            }), true);
        });
        m.addItem (tr ("Extract the sound to an audio clip"), [this, t, c] { extractAudio (t, c); });
        m.addItem (tr ("Delete"), [this, t, c] {
            engine.editSong ([t, c] (Song& s) {
                auto& clips = s.videoTracks[(std::size_t) t].clips;
                if (c < (int) clips.size())
                    clips.erase (clips.begin() + c);
            });
        });
        m.addSeparator();
    }
    m.addItem (tr ("Import video file..."), [this, track = h.track, when] {
        static std::unique_ptr<juce::FileChooser> chooser;
        juce::StringArray patterns;
        for (const auto& ext : videoExtensions)
            patterns.add ("*." + ext);
        chooser = ui::makeFileChooser (tr ("Import video file"), juce::File(), patterns.joinIntoString (";"));
        juce::Component::SafePointer<VideoLanesView> self (this);
        ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [self, track, when] (const juce::FileChooser& fc) {
                               if (self != nullptr && fc.getResult().existsAsFile())
                                   videotracks::importVideo (self->engine, fc.getResult(), when, track);
                           });
    });
    m.addItem (tr ("Add a video track"), [this] { engine.editSong ([] (Song& s) { s.videoTracks.push_back ({}); }); });
    if (h.track >= 0)
        m.addItem (tr ("Open the video window"), [this, track = h.track] { videotracks::showWindow (engine, track); });
    m.addSeparator();
    juce::PopupMenu rates;
    for (int i = 0; i < vtime::rateCount; ++i)
    {
        const auto r = vtime::rateAt (i);
        rates.addItem (juce::String (vtime::rateName (r)), true, song.video.rate == r, [this, r] {
            engine.editSong ([r] (Song& s) {
                s.video.rate = r;
                s.video.rateFromVideo = false;
            });
        });
    }
    m.addSubMenu (tr ("Frame rate"), rates);
    m.addItem (tr ("Start timecode..."), [this] {
        const auto& s = engine.getSong();
        auto* w = new juce::AlertWindow (tr ("Start timecode"), tr ("The timecode at the song's start (hours:minutes:seconds:frames)"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("tc", vtime::format (s.video.startTimecode, s.video.rate));
        w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
            if (r != 1)
                return;
            if (const auto tc = vtime::parseSeconds (w->getTextEditorContents ("tc").toStdString(), engine.getSong().video.rate); tc && ! (*tc < 0.0))
                engine.editSong ([v = *tc] (Song& s2) { s2.video.startTimecode = v; });
        }), true);
    });
    m.addItem (tr ("Timecode ruler"), true, song.video.showTimecodeRuler, [this] {
        engine.editSong ([] (Song& s) { s.video.showTimecodeRuler = ! s.video.showTimecodeRuler; });
    });
    m.addItem (tr ("Burn-in timecode"), true, song.video.burnIn, [this] {
        engine.editSong ([] (Song& s) { s.video.burnIn = ! s.video.burnIn; });
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void VideoLanesView::extractAudio (int track, int clipIndex)
{
    const auto& song = engine.getSong();
    if (track >= (int) song.videoTracks.size() || clipIndex >= (int) song.videoTracks[(std::size_t) track].clips.size())
        return;
    const auto clip = song.videoTracks[(std::size_t) track].clips[(std::size_t) clipIndex];
    const auto file = platform::localFile (clip.path);
    auto folder = platform::cacheFolder ("video-audio");
    folder.createDirectory();
    const auto key = juce::String::toHexString (file.getFullPathName().hashCode64()) + "-" + juce::String::toHexString (file.getLastModificationTime().toMilliseconds());
    const auto wav = folder.getChildFile (file.getFileNameWithoutExtension() + " (" + tr ("audio") + ") " + key.substring (0, 8) + ".wav");
    juce::Component::SafePointer<VideoLanesView> self (this);
    std::thread ([self, file, wav, clip] {
        const bool ok = wav.existsAsFile() || media::extractAudioToWav (file, wav);
        juce::MessageManager::callAsync ([self, wav, ok, clip, name = file.getFileName()] {
            if (self == nullptr)
                return;
            if (! ok)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Extract the sound to an audio clip"),
                                                        name + "\n\n" + tr ("This video has no sound that can be read."));
                return;
            }
            auto& eng = self->engine;
            if (eng.getSampleLibrary().load (wav.getFullPathName()) == nullptr)
                return;
            const auto& s = eng.getSong();
            const videotrack::TempoMap m (s, clip.end() + 60.0);
            const auto start = (std::int64_t) std::llround (m.ticks (clip.start));
            const auto end = (std::int64_t) std::llround (m.ticks (clip.end()));
            const auto offset = (std::int64_t) std::llround (clip.offset * std::max (1.0, s.bpm) / 60.0 * ppq);
            const auto path = wav.getFullPathName().toStdString();
            const auto chName = wav.getFileNameWithoutExtension().toStdString();
            eng.editSong ([=] (Song& song2) {
                int t = 0;
                for (;; ++t)
                    if (std::none_of (song2.playlist.begin(), song2.playlist.end(), [&] (const Clip& c) { return c.track == t && c.start < end && c.end() > start; }))
                        break;
                ChannelDef ch;
                ch.name = chName;
                ch.instrument = InstrumentKind::audioClip;
                ch.sampler.declickMode = audioClipDeclickMode; // FL's AudioTrack.fst
                ch.samplePath = path;
                ch.mixerTrack = song2.claimFreeInsert();
                song2.channels.push_back (ch);
                Clip c;
                c.kind = ClipKind::audio;
                c.channel = (int) song2.channels.size() - 1;
                c.track = t;
                c.start = start;
                c.length = std::max<std::int64_t> (1, end - start);
                c.offset = offset;
                song2.playlist.push_back (c);
                song2.mode = PlayMode::song;
            });
        });
    }).detach();
}

ui::Hint VideoLanesView::hintAt (juce::Point<int> p)
{
    const auto h = hitAt (p);
    const auto& song = engine.getSong();
    switch (h.part)
    {
        case Hit::divider: return { tr ("Video tracks: drag to resize, double-click to fit the tracks"), {} };
        case Hit::timecode: return { tr ("Timecode (click sets the play position, right-click the lane for the frame rate and the start timecode)"), {} };
        case Hit::mute: return { tr ("Mute the video track"), {} };
        case Hit::header: return { tr ("Video track: double-click or right-click for its menu"), trackName (song, h.track), false, true };
        case Hit::head: case Hit::tail: return { tr ("Drag to trim the video clip (Alt: off the frame grid)"), {} };
        case Hit::body:
        {
            const auto& c = song.videoTracks[(std::size_t) h.track].clips[(std::size_t) h.clip];
            return { tr ("Video clip: drag to move (Alt: off the frame grid), double-click opens the video window, right-click deletes, Shift+right-click for its menu"),
                     vtime::format (videotrack::timecodeOf (song.video, c.start), song.video.rate), false, true };
        }
        default: break;
    }
    if (h.track >= 0)
        return { tr ("Video track: drop video files here, double-click opens the video window, right-click for the menu"), trackName (song, h.track), false, true };
    return {};
}
namespace videotracks
{
namespace
{
const VideoClip* exportClip (const Song& song)
{
    for (const auto& t : song.videoTracks)
        if (! t.muted && ! t.clips.empty())
            return &t.clips.front();
    return nullptr;
}

juce::String runFfmpeg (const juce::StringArray& args, int& exitCode)
{
    juce::ChildProcess p;
    if (! p.start (args, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
    {
        exitCode = -1;
        return "ffmpeg did not start";
    }
    // (read as it runs: a full pipe would stall ffmpeg)
    juce::MemoryOutputStream log;
    char buffer[4096];
    while (p.isRunning())
    {
        const auto n = p.readProcessOutput (buffer, sizeof (buffer));
        if (n > 0)
            log.write (buffer, (size_t) n);
        else
            juce::Thread::sleep (20);
    }
    for (int n; (n = p.readProcessOutput (buffer, sizeof (buffer))) > 0;)
        log.write (buffer, (size_t) n);
    exitCode = (int) p.getExitCode();
    return log.toString();
}
} // namespace

bool hasVideo (const Song& song) { return exportClip (song) != nullptr; }

juce::File videoFileFor (const Song& song, const juce::File& audio)
{
    juce::String ext = ".mp4";
    if (const auto* c = exportClip (song))
    {
        const auto source = juce::File (juce::String::fromUTF8 (c->path.c_str())).getFileExtension().toLowerCase();
        if (source == ".mov" || source == ".mkv" || source == ".m4v" || source == ".mp4")
            ext = source;
    }
    auto f = audio.withFileExtension (ext);
    return f == audio ? audio.getSiblingFile (audio.getFileNameWithoutExtension() + " (video)" + ext) : f;
}

juce::String muxWithVideo (const Song& song, double origin, const juce::File& audio, const juce::File& out)
{
    const auto* clip = exportClip (song);
    if (clip == nullptr)
        return tr ("The project has no video clip");
    const auto ffmpeg = findFfmpeg();
    if (! ffmpeg.existsAsFile())
        return media::ffmpegInstallHelp (true);
    double audioLength = 0.0;
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (audio)); r != nullptr && r->sampleRate > 0)
            audioLength = (double) r->lengthInSamples / r->sampleRate;
    }
    if (audioLength <= 0.0)
        return tr ("Could not read {0}", { audio.getFullPathName() });
    const auto video = juce::File (juce::String::fromUTF8 (clip->path.c_str()));
    if (! video.existsAsFile())
        return tr ("Could not read {0}", { video.getFullPathName() });
    // where the clip's picture starts in the export, and from where in its file
    const auto frame = 1.0 / std::max (1.0, song.video.rate.fps());
    auto delay = clip->start - origin;
    auto from = clip->offset;
    if (delay < 0.0)
    {
        from -= delay;
        delay = 0.0;
    }
    const auto seconds = [] (double v) { return juce::String (v, 6); };
    out.deleteFile();
    juce::StringArray base { ffmpeg.getFullPathName(), "-hide_banner", "-nostdin", "-y" };
    int code = 0;
    juce::String log;
    if (delay < frame * 0.5 && from < frame * 0.5)
    {
        // the picture as it is (no re-encode, no quality loss)
        auto a = base;
        a.addArray ({ "-i", video.getFullPathName(), "-i", audio.getFullPathName(), "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy", "-c:a", "aac",
                      "-b:a", "320k", "-t", seconds (audioLength), out.getFullPathName() });
        log = runFfmpeg (a, code);
        if (code == 0 && out.existsAsFile())
            return {};
    }
    // re-encoded: black until the clip starts, the clip from `from`, as long as the audio (the video's own encoder
    // choice: H.264 when this ffmpeg has one, else MPEG-4 Part 2)
    const auto end = std::min (clip->length, audioLength - delay);
    for (const auto* codec : { "libx264", "mpeg4" })
    {
        auto a = base;
        a.addArray ({ "-ss", seconds (from), "-t", seconds (std::max (frame, end)), "-i", video.getFullPathName(), "-i", audio.getFullPathName(),
                      "-filter_complex",
                      "[0:v:0]tpad=start_duration=" + seconds (delay) + ":start_mode=add:color=black:stop_mode=add:stop_duration="
                          + seconds (std::max (0.0, audioLength - delay - end)) + ",fps=" + juce::String (song.video.rate.num) + "/"
                          + juce::String (song.video.rate.den) + ",format=yuv420p[v]",
                      "-map", "[v]", "-map", "1:a:0", "-c:v", codec });
        if (juce::String (codec) == "libx264")
            a.addArray ({ "-crf", "16", "-preset", "medium" });
        else
            a.addArray ({ "-q:v", "2" });
        a.addArray ({ "-c:a", "aac", "-b:a", "320k", "-t", seconds (audioLength), out.getFullPathName() });
        log = runFfmpeg (a, code);
        if (code == 0 && out.existsAsFile())
            return {};
    }
    out.deleteFile();
    return tr ("FFmpeg could not write the video:") + "\n" + log.trim().getLastCharacters (600);
}
} // namespace videotracks
} // namespace lynx
