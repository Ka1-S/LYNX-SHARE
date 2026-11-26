#include "ui/PluginThumbs.h"

#include "core/NativeEngines.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/ChannelSettings.h"
#include "ui/ControllerEditors.h"
#include "ui/Controllers164.h"
#include "ui/FacetEqEditor.h"
#include "ui/FlExtraEditors.h"
#include "ui/NewtoneEditor.h"
#include "ui/AudioEditor.h"
#include "ui/PatcherEditor.h"
#include "ui/PluginEditors.h"
#include "ui/PluginSkin.h"
#include "ui/ProEditors.h"
#include "ui/QuickPick.h"
#include "ui/ShotScale.h"
#include "ui/VideoPlayerView.h"
#include "ui/VisualEditors.h"
#include "core/FlExtraFx.h"
#include "core/Vfx.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace lynx::pluginthumbs
{
namespace
{
juce::File root()
{
    return platform::cacheFolder ("plugin-thumbs");
}

// a picture written next to its place, then moved in: the picker never reads half a file
bool savePng (const juce::Image& image, const juce::File& file)
{
    if (! image.isValid())
        return false;
    file.getParentDirectory().createDirectory();
    const auto temp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".part");
    temp.deleteFile();
    {
        juce::FileOutputStream out (temp);
        if (! out.openedOk())
            return false;
        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, out))
            return false;
    }
    return temp.moveFileTo (file);
}

// the song of one plugin and its editor, as the picture shows it (an instrument: its page of the channel window
// without the channel's header)
std::unique_ptr<juce::Component> generatorEditor (AudioEngine& engine, ChannelDef channel)
{
    Song song;
    song.mixer = Song::defaultMixer (2);
    song.channels.push_back (std::move (channel));
    song.patterns.emplace_back();
    engine.setSong (std::move (song));
    engine.dispatchPendingMessages();
    return makeChannelFrame (engine, 0, 0);
}

// --- round 175b: the editors that are empty until they get something to show ---------------------------------------
// (FL's own pictures show them in use: Newtone with a sung phrase, Control Surface with two knobs, Video Player with a
// picture.) LYNX draws its own stand-ins here; nothing of FL's is used.

// a short sung-like phrase (a voice's harmonics, vibrato, a glide into each note, gaps between them) for Newtone and
// Newtime to analyse and draw
juce::File demoVoice (const juce::File& folder)
{
    const auto file = folder.getChildFile ("demo-voice.wav");
    if (file.existsAsFile())
        return file;
    constexpr double rate = 44100.0;
    const int steps[] = { 0, 4, 7, 9, 7, 4, 2, 5, 4, 0 };
    const auto noteLength = 0.34, gapLength = 0.06;
    const auto total = (int) (rate * (noteLength + gapLength) * (double) std::size (steps)) + (int) rate / 4;
    juce::AudioBuffer<float> buffer (1, total);
    buffer.clear();
    auto* out = buffer.getWritePointer (0);
    double phase = 0.0, freq = 220.0;
    for (int k = 0; k < (int) std::size (steps); ++k)
    {
        const auto start = (int) (rate * (noteLength + gapLength) * k);
        const auto target = 220.0 * std::pow (2.0, (steps[k] + 3) / 12.0);
        const auto n = (int) (rate * noteLength);
        for (int i = 0; i < n && start + i < total; ++i)
        {
            const auto t = i / rate;
            freq += (target - freq) * 0.0015;                                    // the glide
            const auto f = freq * (1.0 + 0.012 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t) * juce::jmin (1.0, t * 4.0));
            phase += 2.0 * juce::MathConstants<double>::pi * f / rate;
            double v = 0.0;
            for (int h = 1; h <= 10; ++h)
                v += std::sin (phase * h) / (h * (h < 4 ? 1.0 : 1.6));
            const auto env = juce::jmin (1.0, t / 0.03) * juce::jmin (1.0, (noteLength - t) / 0.05);
            out[start + i] = (float) (0.28 * v * env * (0.8 + 0.2 * std::sin (k * 1.7)));
        }
    }
    folder.createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr)
        return {};
    if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), rate, 1, 16, {}, 0)))
    {
        stream.release();
        writer->writeFromAudioSampleBuffer (buffer, 0, total);
    }
    return file;
}

// a still for the Video Player's screen: a dusk landscape (drawn here, no video needed)
juce::Image demoVideoFrame()
{
    juce::Image frame (juce::Image::RGB, 640, 360, true);
    juce::Graphics g (frame);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d2b53), 0.0f, 0.0f, juce::Colour (0xfff2845c), 0.0f, 250.0f, false));
    g.fillAll();
    g.setColour (juce::Colour (0xffffd29a).withAlpha (0.9f));
    g.fillEllipse (400.0f, 150.0f, 90.0f, 90.0f);
    auto ridge = [&] (float base, float amp, float seed, juce::Colour c) {
        juce::Path p;
        p.startNewSubPath (0.0f, 360.0f);
        for (int x = 0; x <= 640; x += 8)
            p.lineTo ((float) x, base - amp * (0.5f + 0.5f * std::sin (x * 0.011f + seed)) - amp * 0.35f * std::sin (x * 0.037f + seed * 2.0f));
        p.lineTo (640.0f, 360.0f);
        p.closeSubPath();
        g.setColour (c);
        g.fillPath (p);
    };
    ridge (250.0f, 60.0f, 0.3f, juce::Colour (0xff6b3b5c));
    ridge (285.0f, 45.0f, 2.1f, juce::Colour (0xff3a2443));
    ridge (325.0f, 30.0f, 4.0f, juce::Colour (0xff1a1323));
    return frame;
}

// A page for the HTML NoteBook's picture (written next to the pictures, deleted after the render)
juce::File demoPage (const juce::File& folder)
{
    auto file = folder.getChildFile ("notes.html");
    file.replaceWithText ("<html><head><title>Session notes</title></head><body>"
                          "<h1>Session notes</h1>"
                          "<p><b>Song:</b> Night Drive &mdash; 124 BPM, A minor</p>"
                          "<h2>To do</h2><ul>"
                          "<li>Record the second verse vocal</li>"
                          "<li>Tighten the bass under the kick (sidechain)</li>"
                          "<li>Automate the filter in the break</li>"
                          "<li>Bounce stems for the mix</li></ul>"
                          "<h2>Lyrics</h2>"
                          "<p><i>City lights are fading, the road keeps calling me home...</i></p>"
                          "</body></html>");
    return file;
}

void addDemoContent (EffectSlot& slot, const juce::File& folder)
{
    if (slot.kind == EffectKind::htmlNotebook && flextra::htmlnotebook::address (slot.state).empty())
        slot.state = flextra::htmlnotebook::stateFor (demoPage (folder).getFullPathName().toStdString());
    if (slot.kind == EffectKind::newtone || slot.kind == EffectKind::newtime || slot.kind == EffectKind::edison)
        if (const auto voice = demoVoice (folder); voice.existsAsFile())
            slot.samplePath = voice.getFullPathName().toStdString();
    if (slot.kind == EffectKind::controlSurface && slot.patcher.surface.empty())
    {
        SurfaceControl a, b;
        a.name = "Knob 1";
        a.value = 0.72f;
        a.x = 24.0f;
        a.y = 24.0f;
        a.size = 1.6f;
        b = a;
        b.name = "Knob 2";
        b.value = 0.35f;
        b.x = 130.0f;
        b.y = 60.0f;
        slot.patcher.surface = { a, b };
        if (slot.params.size() > 1)
        {
            slot.params[0] = a.value;
            slot.params[1] = b.value;
        }
    }
}

void addDemoContent (ChannelDef& channel)
{
    if (channel.instrument == InstrumentKind::vfx)
        if (auto st = vfx::readState (channel.pluginState); st && st->kind == vfx::Kind::script)
        {
            auto settings = vfx::parseScript (st->fl).value_or (vfx::defaultScript());
            settings.setSource ("import flvfx as vfx\n\n"
                                "def createDialog():\n"
                                "    form = vfx.ScriptDialog('Humanize', 'Moves each note a little in time and velocity.')\n"
                                "    form.addInputKnob('Timing', 0.2, 0, 1)\n"
                                "    form.addInputKnob('Velocity', 0.35, 0, 1)\n"
                                "    form.addInputKnob('Length', 0.1, 0, 1)\n"
                                "    form.addInputCombo('Mode', ['Random', 'Groove'], 0)\n"
                                "    form.addInputCheckbox('Bypass', 0)\n"
                                "    return form\n\n"
                                "def onTriggerVoice(incomingVoice):\n"
                                "    v = incomingVoice.copy()\n"
                                "    v.trigger()\n\n"
                                "def onReleaseVoice(incomingVoice):\n"
                                "    for v in vfx.context.voices:\n"
                                "        if v.note == incomingVoice.note:\n"
                                "            v.release()\n");
            st->fl = vfx::writeScript (settings);
            channel.pluginState = vfx::writeState (*st);
        }
}

std::unique_ptr<juce::Component> effectEditor (AudioEngine& engine, EffectSlot slot)
{
    Song song;
    song.mixer = Song::defaultMixer (2);
    const auto kind = slot.kind;
    song.mixer[1].effects.push_back (std::move (slot));
    engine.setSong (std::move (song));
    engine.dispatchPendingMessages();
    if (kind == EffectKind::facetEq)
    {
        auto editor = makeFacetEqEditor (engine, 1, 0, true);
        if (editor != nullptr)
            tickFacetEqEditor (*editor);
        return editor;
    }
    if (ProEditorWindows::handles (kind))
        return makeProEditor (engine, 1, 0, true); // (display-first editors with their demo signal)
    // the effects whose window holds their own editor (as the mixer opens them, PluginEditors.cpp open()), not the
    // generic knob page: else the Patcher, Big Clock, HTML NoteBook, Newtone… were an empty box or a 90 px square
    if (kind == EffectKind::patcher)
        return PatcherWindows::makeSlotEditor (engine, 1, 0);
    if (VisualWindows::handles (kind))
        return makeVisualEditor (engine, 1, 0, true);
    if (FlExtraWindows::handles (kind))
        return makeFlExtraSlotEditor (engine, 1, 0, true); // (its own slot target: the NoteBook reads its page from it)
    if (kind == EffectKind::formulaController)
        return FormulaControllerWindows::makeEditor (engine, 1, 0);
    if (NewtoneWindows::handles (kind))
        return makeNewtoneEditor (engine, 1, 0);
    if (kind == EffectKind::edison) // (round 190cd: the audio editor with a phrase in it)
        return makeEdisonSlotEditor (engine, 1, 0);
    if (Controller164Windows::handles (kind))
    {
        auto locate = [] (Song& song) -> EffectSlot* { return song.mixer.size() > 1 && ! song.mixer[1].effects.empty() ? &song.mixer[1].effects[0] : nullptr; };
        return kind == EffectKind::controlSurface ? makeControlSurfaceEditor (engine, locate, std::nullopt)
                                                  : makeXyControllerEditor (engine, locate, nullptr, std::nullopt);
    }
    return makeNativeEffectEditor (engine, 1, 0, 1.0f, 0);
}

// the editor's drawn part: the frame of the window / channel page around it is often wider than its controls (a
// generator's page is at least the channel window's width), so the borders of the background's own colour are cut,
// keeping a small margin; a very tall editor (Control Surface's column) keeps its top part, at most 1.3 × its width.
// `full` (round 190bi, a whole-editor picture): the height is kept whole.
juce::Rectangle<int> contentArea (const juce::Image& image, bool full)
{
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    const auto w = image.getWidth(), h = image.getHeight();
    const auto bg = data.getPixelColour (juce::jmin (3, w - 1), juce::jmin (3, h - 1));
    auto differs = [&] (int x, int y) {
        const auto c = data.getPixelColour (x, y);
        return std::abs ((int) c.getRed() - (int) bg.getRed()) + std::abs ((int) c.getGreen() - (int) bg.getGreen())
                   + std::abs ((int) c.getBlue() - (int) bg.getBlue()) > 24;
    };
    int left = w, right = -1, top = h, bottom = -1;
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2)
            if (differs (x, y))
            {
                left = juce::jmin (left, x);
                right = juce::jmax (right, x);
                top = juce::jmin (top, y);
                bottom = juce::jmax (bottom, y);
            }
    if (right < left || bottom < top)
        return image.getBounds();
    auto area = juce::Rectangle<int>::leftTopRightBottom (left, top, right + 2, bottom + 2).expanded (10).getIntersection (image.getBounds());
    if (area.getWidth() < w / 4 || area.getHeight() < h / 6) // (a stray mark: keep it whole)
        area = image.getBounds();
    if (! full && area.getHeight() > area.getWidth() * 13 / 10)
        area.setHeight (area.getWidth() * 13 / 10);
    return area;
}

// `area` of `image` as a picture of its own
juce::Image cropped (const juce::Image& image, juce::Rectangle<int> area)
{
    if (! image.isValid())
        return image;
    juce::Image out (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics (out).drawImageAt (image.getClippedImage (area), 0, 0);
    return out;
}

juce::Image trimToContent (const juce::Image& image)
{
    return image.isValid() ? cropped (image, contentArea (image, false)) : image;
}

std::unique_ptr<juce::ChildProcess>& child()
{
    static std::unique_ptr<juce::ChildProcess> process;
    return process;
}
bool startedOnce = false;
} // namespace

juce::File nativeFolder()
{
    // (-r2: the pictures trimmed to the editor's drawn part; -r3 to -r5: FL's budget and the editors' demo content (round
    // 175b); a new drawing rule = a new folder)
    return root().getChildFile (juce::String (JUCE_APPLICATION_VERSION_STRING).retainCharacters ("0123456789.-abcdefghijklmnopqrstuvwxyz") + "-r5");
}

juce::File capturedFolder()
{
    return root().getChildFile ("plugins");
}

juce::String fileNameFor (const std::string& key)
{
    auto text = juce::String::fromUTF8 (key.c_str());
    for (const auto* prefix : { "lynx:gen:", "lynx:fx:" })
        if (text.startsWith (prefix))
            text = juce::String (juce::String (prefix).contains ("gen") ? "gen-" : "fx-") + text.substring ((int) std::strlen (prefix));
    auto slug = text.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789- ").replaceCharacter (' ', '-').substring (0, 48);
    // (FNV-1a of the whole key: two plugins whose names fold to the same slug still differ)
    std::uint32_t h = 2166136261u;
    for (const auto c : key)
        h = (h ^ (std::uint8_t) c) * 16777619u;
    return slug + "-" + juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 8) + ".png";
}

juce::File fileFor (const std::string& key, bool native)
{
    return (native ? nativeFolder() : capturedFolder()).getChildFile (fileNameFor (key));
}

juce::Image shrink (const juce::Image& image, float pixelsPerUnit)
{
    if (! image.isValid())
        return {};
    const auto scale = juce::jmin ((double) pictureScale / juce::jmax (0.01f, pixelsPerUnit), (double) maxWidth / image.getWidth(),
                                   (double) maxHeight / image.getHeight());
    if (scale >= 1.0)
        return image.convertedToFormat (juce::Image::ARGB);
    const auto w = juce::jmax (1, juce::roundToInt (image.getWidth() * scale));
    const auto h = juce::jmax (1, juce::roundToInt (image.getHeight() * scale));
    // halve first while far above the target (a box filter keeps the fine lines), then the last step
    auto from = image.convertedToFormat (juce::Image::ARGB);
    while (from.getWidth() >= w * 2 && from.getHeight() >= h * 2)
        from = from.rescaled (from.getWidth() / 2, from.getHeight() / 2, juce::Graphics::highResamplingQuality);
    return from.rescaled (w, h, juce::Graphics::highResamplingQuality);
}

int renderNative (const juce::File& folder, const juce::StringArray& only, const std::function<void (const juce::String&)>& progress, float fullScale)
{
    folder.createDirectory();
    // one drawing process at a time for this folder (two LYNX windows open the picker together)
    juce::InterProcessLock lock ("LYNX-plugin-thumbs-" + juce::String::toHexString (folder.getFullPathName().hashCode()));
    if (! lock.enter (0))
        return 0;

    skin::setArtMotion (false); // (a still picture: the moving layers stay at their first frame)
    AudioEngine engine (false);
    int drawn = 0;
    auto shoot = [&] (const std::string& key, std::unique_ptr<juce::Component> editor, int cropTop) {
        const auto file = shot::file (folder.getChildFile (fileNameFor (key)));
        if (editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= cropTop)
        {
            if (progress)
                progress (juce::String::fromUTF8 (key.c_str()) + ": no editor");
            return;
        }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto area = editor->getLocalBounds().withTrimmedTop (cropTop);
        juce::Image picture;
        if (fullScale > 0.0f)
        {
            // (the drawn part found at 100 %, then cut from the picture at the scale: every scale gets the same layout box,
            // so the 2x file is exactly twice the 1x one)
            const auto box = contentArea (editor->createComponentSnapshot (area, true, 1.0f), true);
            const auto big = editor->createComponentSnapshot (area, true, fullScale);
            picture = cropped (big, (box.toFloat() * fullScale).toNearestInt().getIntersection (big.getBounds()));
        }
        else
            picture = shrink (trimToContent (editor->createComponentSnapshot (area, true, 1.0f)));
        if (savePng (picture, file))
            ++drawn;
        if (progress)
            progress (file.getFileName() + " " + juce::String (picture.getWidth()) + "x" + juce::String (picture.getHeight()) + " "
                      + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
    };
    auto wanted = [&] (const std::string& key) {
        if (! only.isEmpty() && ! only.contains (juce::String::fromUTF8 (key.c_str())))
            return false;
        return ! shot::file (folder.getChildFile (fileNameFor (key))).existsAsFile();
    };

    // round 190bi: one plugin that throws (the Linux build: std::bad_function_call) is reported and skipped instead of
    // ending the whole run with the pictures after it undrawn
    auto guarded = [&] (const std::string& key, const std::function<void()>& draw) {
        try
        {
            draw();
        }
        catch (const std::exception& e)
        {
            if (progress)
                progress (juce::String::fromUTF8 (key.c_str()) + ": failed (" + e.what() + ")");
        }
    };
    for (const auto& g : quickpick::generatorEntries (engine))
    {
        if (g.folder == 3 || ! g.available || ! g.make || ! wanted (g.key))
            continue;
        guarded (g.key, [&] {
            auto channel = g.make();
            if (! channel.has_value())
                return;
            if (channel->instrument == InstrumentKind::patcher) // (its window is its node graph, as the Channel Rack opens it)
            {
                auto frame = generatorEditor (engine, std::move (*channel));
                shoot (g.key, PatcherWindows::makeChannelEditor (engine, 0), 0);
                return;
            }
            addDemoContent (*channel);
            const auto video = channel->instrument == InstrumentKind::videoplayer;
            auto editor = generatorEditor (engine, std::move (*channel));
            if (video && editor != nullptr)
                showVideoStill (*editor, demoVideoFrame());
            shoot (g.key, std::move (editor), channelFrameHeaderHeight());
        });
    }
    for (const auto& f : quickpick::effectEntries (engine))
    {
        if (f.folder == 3 || ! f.make || ! wanted (f.key))
            continue;
        guarded (f.key, [&] {
            auto slot = f.make();
            if (! slot.has_value())
                return;
            addDemoContent (*slot, folder);
            shoot (f.key, effectEditor (engine, std::move (*slot)), 0);
        });
    }
    engine.setSong (Song {});
    folder.getChildFile ("demo-voice.wav").deleteFile();
    folder.getChildFile ("notes.html").deleteFile();
    return drawn;
}

juce::String checkEditors (const std::function<void (const juce::String&)>& before)
{
    AudioEngine engine (false);
    int generators = 0, effects = 0;
    juce::StringArray problems;
    auto paint = [&] (const std::string& key, std::unique_ptr<juce::Component> editor) {
        const auto name = juce::String::fromUTF8 (key.c_str());
        if (editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= 0)
        {
            problems.add (name + ": no editor");
            return false;
        }
        const auto picture = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        if (! picture.isValid())
        {
            problems.add (name + ": nothing painted");
            return false;
        }
        return true;
    };
    auto guarded = [&] (const std::string& key, const std::function<void()>& open) {
        if (before)
            before (juce::String::fromUTF8 (key.c_str()));
        try
        {
            open();
        }
        catch (const std::exception& e)
        {
            problems.add (juce::String::fromUTF8 (key.c_str()) + ": " + e.what());
        }
    };
    for (const auto& g : quickpick::generatorEntries (engine))
    {
        if (g.folder == 3 || ! g.available || ! g.make)
            continue;
        guarded (g.key, [&] {
            auto channel = g.make();
            if (! channel.has_value())
                return;
            const auto patcher = channel->instrument == InstrumentKind::patcher;
            addDemoContent (*channel);
            auto frame = generatorEditor (engine, std::move (*channel));
            // (a Patcher channel: its page, and the node graph the Channel Rack opens)
            if (paint (g.key, std::move (frame)) && (! patcher || paint (g.key, PatcherWindows::makeChannelEditor (engine, 0))))
                ++generators;
        });
    }
    for (const auto& f : quickpick::effectEntries (engine))
    {
        if (f.folder == 3 || ! f.available || ! f.make)
            continue;
        guarded (f.key, [&] {
            auto slot = f.make();
            if (slot.has_value() && paint (f.key, effectEditor (engine, std::move (*slot))))
                ++effects;
        });
    }
    engine.setSong (Song {});
    if (generators == 0 || effects == 0)
        problems.add ("nothing opened");
    return problems.isEmpty() ? "OK (" + juce::String (generators) + " generators, " + juce::String (effects) + " effects)"
                              : "ERROR " + problems.joinIntoString ("; ");
}

bool rendering()
{
    auto& p = child();
    return p != nullptr && p->isRunning();
}

bool ensureRendering (bool again)
{
    if (rendering())
        return true;
    if (startedOnce && ! again)
        return false;
    startedOnce = true;
    auto& p = child();
    p = std::make_unique<juce::ChildProcess>();
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    if (! p->start (juce::StringArray { exe.getFullPathName(), "--pluginthumbs", nativeFolder().getFullPathName() }, 0))
    {
        p.reset();
        return false;
    }
    return true;
}

void captureEditor (const std::string& key, juce::Component& editor)
{
   #if JUCE_WINDOWS
    const auto file = fileFor (key, false);
    if (key.empty() || file.existsAsFile() || ! editor.isShowing())
        return;
    auto* peer = editor.getPeer();
    if (peer == nullptr)
        return;
    auto* hwnd = (HWND) peer->getNativeHandle();
    RECT r {};
    if (hwnd == nullptr || ! GetClientRect (hwnd, &r) || r.right <= 8 || r.bottom <= 8)
        return;
    const auto w = (int) r.right, h = (int) r.bottom;
    auto* screen = GetDC (nullptr);
    auto* dc = CreateCompatibleDC (screen);
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    auto* bitmap = CreateDIBSection (dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    juce::Image picture;
    if (bitmap != nullptr && bits != nullptr)
    {
        auto* old = SelectObject (dc, bitmap);
        // PW_CLIENTONLY | PW_RENDERFULLCONTENT: the client area with the plugin's own (GPU) drawing in it
        if (PrintWindow (hwnd, dc, 0x1 | 0x2))
        {
            GdiFlush();
            picture = juce::Image (juce::Image::RGB, w, h, false);
            juce::Image::BitmapData out (picture, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                const auto* row = static_cast<const std::uint8_t*> (bits) + (std::size_t) y * (std::size_t) w * 4;
                for (int x = 0; x < w; ++x)
                    out.setPixelColour (x, y, juce::Colour (row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]));
            }
        }
        SelectObject (dc, old);
        DeleteObject (bitmap);
    }
    DeleteDC (dc);
    ReleaseDC (nullptr, screen);

    // the editor's part only (not the window's own LYNX bar): its area in the window, in physical pixels
    const auto scale = (float) w / (float) juce::jmax (1, peer->getComponent().getWidth());
    if (picture.isValid())
    {
        const auto& top = peer->getComponent();
        const auto area = (top.getLocalArea (&editor, editor.getLocalBounds()).toFloat() * scale).toNearestInt().getIntersection ({ 0, 0, w, h });
        if (area.getWidth() < 16 || area.getHeight() < 16)
            return;
        picture = picture.getClippedImage (area);
    }
    if (picture.isValid())
    {
        const auto pw = picture.getWidth(), ph = picture.getHeight();
        // a fully black or white capture = the plugin drew nothing PrintWindow can see: keep the tile instead
        juce::Image::BitmapData data (picture, juce::Image::BitmapData::readOnly);
        const auto first = data.getPixelColour (pw / 2, ph / 2);
        bool flat = true;
        for (int y = 0; y < ph && flat; y += juce::jmax (1, ph / 16))
            for (int x = 0; x < pw && flat; x += juce::jmax (1, pw / 16))
                flat = data.getPixelColour (x, y) == first;
        if (! flat)
        {
            // (a clipped image shares its pixels: copy it out so the PNG writer and the scaler see only the editor)
            juce::Image own (juce::Image::RGB, pw, ph, false);
            juce::Graphics (own).drawImageAt (picture, 0, 0);
            savePng (shrink (trimToContent (own), scale), file);
        }
    }
   #else
    juce::ignoreUnused (key, editor);
   #endif
}
} // namespace lynx::pluginthumbs
