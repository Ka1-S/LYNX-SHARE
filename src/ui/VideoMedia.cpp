#include "ui/VideoMedia.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include <juce_audio_formats/juce_audio_formats.h>

#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/VisualEditors.h"

namespace lynx::media
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------
// ffmpeg (the program)
Probe probeWithFfmpeg (const juce::String& path)
{
    Probe p;
    const auto ffmpeg = findFfmpeg();
    if (! ffmpeg.existsAsFile())
        return p;
    juce::ChildProcess process;
    if (process.start (juce::StringArray { ffmpeg.getFullPathName(), "-hide_banner", "-nostdin", "-i", path }, juce::ChildProcess::wantStdErr))
    {
        p.report = process.readAllProcessOutput();
        process.waitForProcessToFinish (2000);
    }
    p.info = video::parseFfmpegReport (p.report.toStdString());
    if (p.info.hasVideo || p.info.hasAudio)
        p.backend = "ffmpeg";
    return p;
}

std::shared_ptr<const video::AudioTrack> decodeTrackWithFfmpeg (const juce::File& file, const video::Info& info)
{
    const auto ffmpeg = findFfmpeg();
    if (! ffmpeg.existsAsFile())
        return nullptr;
    juce::ChildProcess process;
    if (! process.start (juce::StringArray { ffmpeg.getFullPathName(), "-nostdin", "-v", "quiet", "-i", file.getFullPathName(), "-vn", "-sn",
                                             "-ac", "2", "-ar", juce::String ((int) trackRate), "-f", "s16le", "-" },
                         juce::ChildProcess::wantStdOut))
        return nullptr;
    auto track = std::make_shared<video::AudioTrack>();
    track->rate = trackRate;
    if (info.duration > 0.0)
        track->samples.reserve ((std::size_t) (info.duration * trackRate * 2.0) + 4096);
    std::vector<char> buffer ((std::size_t) 1 << 16);
    bool odd = false; // a byte of a sample split between two reads
    char carry = 0;
    for (;;)
    {
        const auto got = process.readProcessOutput (buffer.data(), (int) buffer.size());
        if (got <= 0)
            break;
        int i = 0;
        if (odd)
        {
            track->samples.push_back ((std::int16_t) ((unsigned char) carry | ((unsigned char) buffer[0] << 8)));
            odd = false;
            i = 1;
        }
        for (; i + 1 < got; i += 2)
            track->samples.push_back ((std::int16_t) ((unsigned char) buffer[(std::size_t) i] | ((unsigned char) buffer[(std::size_t) i + 1] << 8)));
        if (i < got)
        {
            carry = buffer[(std::size_t) i];
            odd = true;
        }
    }
    process.waitForProcessToFinish (2000);
    if (track->samples.size() % 2 != 0)
        track->samples.pop_back();
    if (track->samples.empty())
        return nullptr;
    return track;
}

std::mutex probeLock;
std::map<juce::String, Probe> probes;

std::shared_ptr<const video::AudioTrack> decodeTrack (const juce::File& file)
{
    if (! file.existsAsFile())
        return nullptr;
    const auto p = probe (file.getFullPathName());
    if (! p.info.hasAudio)
        return nullptr;
    if (p.backend != "ffmpeg")
        if (auto t = decodeNativeAudio (file, trackRate))
            return t;
    return decodeTrackWithFfmpeg (file, p.info);
}

struct TrackEntry
{
    std::shared_ptr<const video::AudioTrack> track;
    bool pending = false, done = false;
};

std::mutex trackLock;
std::map<std::string, TrackEntry> tracks;

juce::ThreadPool& trackPool()
{
    static juce::ThreadPool pool (1);
    return pool;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
Probe probe (const juce::String& path)
{
    {
        std::lock_guard<std::mutex> lock (probeLock);
        if (const auto it = probes.find (path); it != probes.end())
            return it->second;
    }
    const juce::File file (path);
    if (! file.existsAsFile())
        return {}; // (not cached: it may appear later)
    Probe p;
    if (auto reader = openNative (file))
    {
        p.info = reader->info();
        p.backend = nativeBackendName();
    }
    if (! p.info.hasVideo)
    {
        // the system could not read the picture: ffmpeg may (a sound-only reading of the system is kept otherwise)
        auto f = probeWithFfmpeg (path);
        if (f.info.hasVideo || ! p.info.hasAudio)
            p = f;
    }
    if (p.backend.isNotEmpty() || findFfmpeg().existsAsFile())
    {
        std::lock_guard<std::mutex> lock (probeLock);
        probes[path] = p; // (without any decoder not cached: ffmpeg may be located later)
    }
    return p;
}

void clearCaches()
{
    {
        std::lock_guard<std::mutex> lock (probeLock);
        probes.clear();
    }
    std::lock_guard<std::mutex> lock (trackLock);
    tracks.clear();
}

bool canDecodeVideo()
{
    return nativeBackendName().isNotEmpty() || findFfmpeg().existsAsFile();
}

juce::String ffmpegInstallHelp (bool forExport)
{
    juce::String s = forExport ? tr ("To export with video, install ffmpeg (free), then export again:")
                               : tr ("To read this video, install ffmpeg (free), then load the video again:");
    s << "\n";
#if JUCE_WINDOWS
    s << tr ("Windows: open a terminal and type") << "  winget install Gyan.FFmpeg\n";
#elif JUCE_MAC
    s << tr ("macOS: install Homebrew, then type in a terminal") << "  brew install ffmpeg\n";
#else
    s << "Ubuntu, Debian, Mint:  sudo apt install ffmpeg\n";
    s << "Fedora:  sudo dnf install ffmpeg-free  (" << tr ("every codec: ffmpeg from RPM Fusion") << ")\n";
    s << "Arch, Manjaro:  sudo pacman -S ffmpeg\n";
    s << "openSUSE:  sudo zypper install ffmpeg\n";
#endif
    s << tr ("Or download it from ffmpeg.org and choose \"Locate ffmpeg...\".");
    return s;
}

juce::String cannotReadMessage (const juce::String& path)
{
    const auto native = nativeBackendName();
    if (! findFfmpeg().existsAsFile())
    {
        if (native.isNotEmpty())
            return tr ("This system cannot read this video file ({0}).", { juce::File (path).getFileExtension().trimCharactersAtStart (".") }) + "\n" + ffmpegInstallHelp();
        return ffmpegInstallHelp();
    }
    return tr ("Neither this system nor ffmpeg could read a video in this file");
}

// ---------------------------------------------------------------------------------------------------------------
std::shared_ptr<const video::AudioTrack> provideTrack (const std::string& path, bool wait, std::function<void()> ready)
{
    const auto file = platform::localFile (path);
    {
        std::lock_guard<std::mutex> lock (trackLock);
        auto& e = tracks[path];
        if (e.done)
            return e.track;
        if (! wait)
        {
            if (! e.pending)
            {
                e.pending = true;
                trackPool().addJob ([path, file, ready] {
                    auto track = decodeTrack (file);
                    {
                        std::lock_guard<std::mutex> l (trackLock);
                        auto& entry = tracks[path];
                        entry.track = track;
                        entry.done = true;
                        entry.pending = false;
                    }
                    if (ready)
                        juce::MessageManager::callAsync (ready);
                });
            }
            return nullptr;
        }
    }
    // exporting: decode now (the export must have the sound)
    auto track = decodeTrack (file);
    std::lock_guard<std::mutex> lock (trackLock);
    auto& e = tracks[path];
    if (! e.done)
    {
        e.track = track;
        e.done = true;
    }
    return e.track;
}

bool extractAudioToWav (const juce::File& video, const juce::File& wav)
{
    if (! video.existsAsFile())
        return false;
    const auto p = probe (video.getFullPathName());
    if (! p.info.hasAudio)
        return false;
    if (p.backend != "ffmpeg")
        if (auto t = decodeNativeAudio (video, trackRate))
        {
            wav.deleteFile();
            std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream().release());
            if (stream == nullptr)
                return false;
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.get(), t->rate, 2, 16, {}, 0));
            if (writer == nullptr)
                return false;
            stream.release(); // (the writer owns it)
            constexpr int block = 8192;
            juce::AudioBuffer<float> buf (2, block);
            for (std::int64_t f = 0; f < t->frames(); f += block)
            {
                const auto n = (int) std::min<std::int64_t> (block, t->frames() - f);
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample (0, i, t->samples[(std::size_t) ((f + i) * 2)] / 32768.0f);
                    buf.setSample (1, i, t->samples[(std::size_t) ((f + i) * 2 + 1)] / 32768.0f);
                }
                if (! writer->writeFromAudioSampleBuffer (buf, 0, n))
                    return false;
            }
            writer = nullptr;
            return wav.getSize() > 44;
        }
    const auto ffmpeg = findFfmpeg();
    if (! ffmpeg.existsAsFile())
        return false;
    juce::ChildProcess proc;
    if (proc.start (juce::StringArray { ffmpeg.getFullPathName(), "-nostdin", "-v", "quiet", "-y", "-i", video.getFullPathName(), "-vn", "-sn",
                                        "-ac", "2", "-c:a", "pcm_s16le", wav.getFullPathName() },
                    0))
        proc.waitForProcessToFinish (10 * 60 * 1000);
    return wav.existsAsFile() && wav.getSize() > 44;
}

// ---------------------------------------------------------------------------------------------------------------
void scaleBgra (const std::uint8_t* src, int srcW, int srcH, int srcStride, std::uint8_t* dst, int w, int h, int dstStride) noexcept
{
    if (srcW <= 0 || srcH <= 0 || w <= 0 || h <= 0)
        return;
    if (srcW == w && srcH == h)
    {
        for (int y = 0; y < h; ++y)
        {
            auto* d = dst + (std::size_t) y * (std::size_t) dstStride;
            std::memcpy (d, src + (std::size_t) y * (std::size_t) srcStride, (std::size_t) w * 4);
            for (int x = 0; x < w; ++x)
                d[x * 4 + 3] = 255;
        }
        return;
    }
    // each target pixel averages the source pixels whose centres fall in it (at least the nearest one)
    std::vector<int> x0 ((std::size_t) w + 1);
    for (int x = 0; x <= w; ++x)
        x0[(std::size_t) x] = (int) ((std::int64_t) x * srcW / w);
    for (int y = 0; y < h; ++y)
    {
        auto ya = (int) ((std::int64_t) y * srcH / h), yb = (int) ((std::int64_t) (y + 1) * srcH / h);
        if (yb <= ya)
            yb = ya + 1;
        yb = std::min (yb, srcH);
        auto* d = dst + (std::size_t) y * (std::size_t) dstStride;
        for (int x = 0; x < w; ++x)
        {
            auto xa = x0[(std::size_t) x], xb = x0[(std::size_t) x + 1];
            if (xb <= xa)
                xb = xa + 1;
            xb = std::min (xb, srcW);
            // (a step of up to 2 source pixels per axis while shrinking a lot: quick, and still smooth enough)
            const auto stepX = std::max (1, (xb - xa) / 2), stepY = std::max (1, (yb - ya) / 2);
            std::uint32_t b = 0, g = 0, r = 0, n = 0;
            for (int sy = ya; sy < yb; sy += stepY)
            {
                const auto* row = src + (std::size_t) sy * (std::size_t) srcStride;
                for (int sx = xa; sx < xb; sx += stepX)
                {
                    const auto* px = row + sx * 4;
                    b += px[0];
                    g += px[1];
                    r += px[2];
                    ++n;
                }
            }
            d[x * 4 + 0] = (std::uint8_t) (b / n);
            d[x * 4 + 1] = (std::uint8_t) (g / n);
            d[x * 4 + 2] = (std::uint8_t) (r / n);
            d[x * 4 + 3] = 255;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
FrameDecoder::FrameDecoder() : juce::Thread ("LYNX video frames") {}

FrameDecoder::~FrameDecoder()
{
    signalThreadShouldExit();
    notify();
    {
        std::lock_guard<std::mutex> lock (procLock);
        if (process != nullptr)
            process->kill(); // (our own child process)
    }
    stopThread (4000);
}

void FrameDecoder::setSource (const juce::String& p, const video::Info& i)
{
    std::lock_guard<std::mutex> lock (want);
    if (p == path)
        return;
    path = p;
    info = i;
    restart = true;
    notify();
}

void FrameDecoder::setSize (int w, int h)
{
    std::lock_guard<std::mutex> lock (want);
    w = juce::jlimit (16, 3840, w & ~1);
    h = juce::jlimit (16, 2160, h & ~1);
    if (w == width && h == height)
        return;
    width = w;
    height = h;
    restart = true;
    notify();
}

void FrameDecoder::request (double t, bool moving)
{
    std::lock_guard<std::mutex> lock (want);
    if (std::abs (t - wantTime) < 1e-6 && moving == wantMoving)
        return;
    wantTime = t;
    wantMoving = moving;
    notify();
}

juce::Image FrameDecoder::frame (double& time)
{
    std::lock_guard<std::mutex> lock (out);
    time = shownTime;
    return shown;
}

void FrameDecoder::run()
{
    while (! threadShouldExit())
    {
        juce::String p;
        {
            std::lock_guard<std::mutex> lock (want);
            p = path;
        }
        if (p.isEmpty())
        {
            stopProcess();
            wait (100);
            continue;
        }
        const auto pr = probe (p);
        if (pr.backend.isNotEmpty() && pr.backend != "ffmpeg")
            if (auto reader = openNative (juce::File (p)))
            {
                runNative (*reader, p);
                continue;
            }
        runFfmpeg (p);
    }
    stopProcess();
}

// The system's decoder: seek to the wanted frame, decode forward; while playing, keep one frame ahead and skip the
// late ones. Returns when the source changes or the thread stops.
void FrameDecoder::runNative (Reader& reader, const juce::String& p)
{
    std::vector<std::uint8_t> next, held;
    int nw = 0, nh = 0, hw = 0, hh = 0;
    double heldTime = -1e9, lastDecoded = -1e9;
    bool haveHeld = false, atEnd = false;
    const auto readerFps = reader.info().fps;
    const auto frameDur = 1.0 / (readerFps > 1.0 && readerFps < 1000.0 ? readerFps : 30.0);
    int w = 0, h = 0;
    lastShown = -1e9;
    while (! threadShouldExit())
    {
        double t;
        bool moving, fresh;
        {
            std::lock_guard<std::mutex> lock (want);
            if (path != p)
                return;
            t = std::max (0.0, wantTime);
            moving = wantMoving;
            fresh = restart;
            restart = false;
            w = width;
            h = height;
        }
        if (w <= 0 || h <= 0)
        {
            wait (50);
            continue;
        }
        if (fresh)
            lastShown = -1e9; // (a new size: draw the frame again)
        // the held frame covers t: show it (once)
        if (haveHeld && heldTime <= t + 1e-4 && (t < heldTime + frameDur || atEnd))
        {
            if (std::abs (heldTime - lastShown) > 1e-6)
            {
                publish (held.data(), hw, hh, w, h, heldTime);
                lastShown = heldTime;
            }
            if (! moving || atEnd)
            {
                wait (moving ? 20 : 100);
                continue;
            }
        }
        // a jump back, or far ahead: seek (scrubbing); else decode on from where we are
        if (! haveHeld || t < heldTime - 1e-4 || t > lastDecoded + 2.0)
        {
            reader.seek (t);
            haveHeld = false;
            atEnd = false;
            lastDecoded = -1e9;
        }
        // decode up to the frame of t (one frame ahead at most)
        bool interrupted = false;
        while (! threadShouldExit() && ! atEnd && (! haveHeld || heldTime + frameDur <= t + 1e-4))
        {
            double ft = 0.0;
            if (! reader.next (next, nw, nh, ft))
            {
                atEnd = true;
                break;
            }
            lastDecoded = ft;
            if (haveHeld && moving && heldTime > lastShown + 1e-6)
                ++dropped; // decoded but never shown (late)
            std::swap (next, held);
            hw = nw;
            hh = nh;
            heldTime = ft;
            haveHeld = true;
            {
                // the wanted time moved back / the source changed meanwhile: start over
                std::lock_guard<std::mutex> lock (want);
                if (path != p || restart || wantTime < heldTime - 1.0)
                {
                    interrupted = true;
                    break;
                }
                t = std::max (t, wantTime);
            }
        }
        if (interrupted)
            continue;
        if (haveHeld && (heldTime <= t + 1e-4 || ! moving) && std::abs (heldTime - lastShown) > 1e-6)
        {
            // (a still before the video's first frame shows the first frame)
            publish (held.data(), hw, hh, w, h, heldTime);
            lastShown = heldTime;
        }
        if (! haveHeld)
        {
            wait (100); // unreadable here
            continue;
        }
        if (moving)
        {
            // until the held frame is due (ahead), or until the next one is (shown)
            const auto due = heldTime > t ? heldTime : heldTime + frameDur;
            wait (juce::jlimit (1, 20, (int) std::ceil ((due - t) * 1000.0)));
        }
        else
            wait (100);
    }
}

// ffmpeg (the program): one process per stretch of playback; a still (stopped transport) is one frame.
void FrameDecoder::runFfmpeg (const juce::String& entry)
{
    double streamStart = -1.0, nextTime = 0.0, eofTime = -1.0, fps = 30.0;
    int w = 0, h = 0;
    bool streaming = false;
    juce::String current;
    std::vector<std::uint8_t> bytes;
    lastShown = -1e9;
    while (! threadShouldExit())
    {
        double t;
        bool moving, fresh;
        juce::String p;
        {
            std::lock_guard<std::mutex> lock (want);
            t = std::max (0.0, wantTime);
            moving = wantMoving;
            fresh = restart;
            restart = false;
            p = path;
            if (fresh)
            {
                w = std::min (width, 1920);
                h = std::min (height, 1080);
                fps = info.fps > 1.0 && info.fps < 240.0 ? info.fps : 30.0;
            }
        }
        if (p != entry)
        {
            stopProcess();
            {
                std::lock_guard<std::mutex> lock (want);
                restart = true; // (the next source's loop starts fresh)
            }
            return;
        }
        if (p.isEmpty() || w <= 0 || h <= 0)
        {
            stopProcess();
            streaming = false;
            wait (100);
            continue;
        }
        const auto frameTime = 1.0 / fps;
        if (fresh)
        {
            eofTime = -1.0;
            lastShown = -1e9;
        }
        bytes.resize ((std::size_t) w * (std::size_t) h * 4);
        if (! moving)
        {
            if (std::abs (t - lastShown) < frameTime * 0.5)
            {
                if (streaming)
                {
                    stopProcess();
                    streaming = false;
                }
                wait (50);
                continue;
            }
            startProcess (p, t, w, h, fps, false);
            current = p;
            streaming = false;
            if (readFrame (bytes))
                publish (bytes.data(), w, h, w, h, t);
            lastShown = t; // (past the end: the last frame stays)
            stopProcess();
            continue;
        }
        if (! streaming && eofTime >= 0.0 && p == current && t >= eofTime - frameTime)
        {
            wait (50);
            continue;
        }
        if (! streaming || p != current || t < streamStart - 1e-3 || t > nextTime + 1.0)
        {
            startProcess (p, t, w, h, fps, true);
            current = p;
            streamStart = nextTime = t;
            eofTime = -1.0;
            streaming = process != nullptr;
            if (! streaming)
            {
                wait (200);
                continue;
            }
        }
        if (nextTime > t + 0.04)
        {
            wait (juce::jlimit (2, 30, (int) ((nextTime - t - 0.04) * 1000.0)));
            continue;
        }
        if (! readFrame (bytes))
        {
            stopProcess();
            streaming = false;
            eofTime = nextTime;
            continue;
        }
        const auto ft = nextTime;
        nextTime += frameTime;
        if (nextTime <= t)
        {
            ++dropped;
            continue; // late: on to the frame of now
        }
        publish (bytes.data(), w, h, w, h, ft);
        lastShown = ft;
    }
}

void FrameDecoder::startProcess (const juce::String& p, double t, int w, int h, double fps, bool moving)
{
    stopProcess();
    const auto ffmpeg = findFfmpeg();
    if (! ffmpeg.existsAsFile())
        return;
    juce::StringArray args { ffmpeg.getFullPathName(), "-nostdin", "-v", "quiet", "-ss", juce::String (t, 3), "-i", p, "-an", "-sn" };
    if (! moving)
        args.addArray ({ "-frames:v", "1" });
    args.addArray ({ "-vf", "fps=" + juce::String (fps, 6) + ",scale=" + juce::String (w) + ":" + juce::String (h) + ":flags=bilinear",
                     "-pix_fmt", "bgra", "-f", "rawvideo", "-" });
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (args, juce::ChildProcess::wantStdOut))
        return;
    std::lock_guard<std::mutex> lock (procLock);
    process = std::move (proc);
}

void FrameDecoder::stopProcess()
{
    std::unique_ptr<juce::ChildProcess> old;
    {
        std::lock_guard<std::mutex> lock (procLock);
        old = std::move (process);
    }
    if (old != nullptr && old->isRunning())
        old->kill();
}

bool FrameDecoder::readFrame (std::vector<std::uint8_t>& bytes)
{
    std::size_t got = 0;
    while (got < bytes.size() && ! threadShouldExit())
    {
        juce::ChildProcess* p;
        {
            std::lock_guard<std::mutex> lock (procLock);
            p = process.get();
        }
        if (p == nullptr)
            return false;
        const auto n = p->readProcessOutput (bytes.data() + got, (int) std::min<std::size_t> (bytes.size() - got, (std::size_t) 1 << 20));
        if (n <= 0)
            return false;
        got += (std::size_t) n;
    }
    return got == bytes.size();
}

void FrameDecoder::publish (const std::uint8_t* bgra, int srcW, int srcH, int w, int h, double t)
{
    if (bgra == nullptr || srcW <= 0 || srcH <= 0)
        return;
    // never larger than the source (the window scales up when it draws)
    if (w > srcW || h > srcH)
    {
        w = srcW;
        h = srcH;
    }
    // a buffer neither shown nor still held by a window (the UI keeps the frame it drew)
    juce::Image* free = nullptr;
    for (auto& img : pool)
        if (img.isValid() && img.getWidth() == w && img.getHeight() == h && img.getReferenceCount() == 1)
            free = &img;
    if (free == nullptr)
    {
        auto* slot = &pool[0];
        for (auto& img : pool)
            if (img.isNull() || img.getReferenceCount() == 1)
                slot = &img;
        *slot = juce::Image (juce::Image::ARGB, w, h, false);
        free = slot;
    }
    auto back = *free;
    {
        juce::Image::BitmapData data (back, juce::Image::BitmapData::writeOnly);
        scaleBgra (bgra, srcW, srcH, srcW * 4, data.getLinePointer (0), w, h, data.lineStride);
    }
    {
        std::lock_guard<std::mutex> lock (out);
        shown = back;
        shownTime = t;
    }
    ++frames;
}

#if ! JUCE_WINDOWS && ! JUCE_MAC
// Linux: no system decoder (ffmpeg only)
std::unique_ptr<Reader> openNative (const juce::File&) { return nullptr; }
std::shared_ptr<const video::AudioTrack> decodeNativeAudio (const juce::File&, double) { return nullptr; }
juce::String nativeBackendName() { return {}; }
#endif
} // namespace lynx::media
