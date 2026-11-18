// LYNX vocal lab (round 190li): runs LYNX's Newtone engine (core/NewtoneFx.h: analyseNotes + an edit + renderEdits)
// offline on a WAV, for the scoring in tools/vocallab/vocallab.py. Also dumps the analysed notes (detection scoring).
//
// usage: newtonelab <in.wav> <out.wav|-> [options / edits, applied in order]
//   engine=N           the render engine revision (Track::engine; 0 = FL's as built until 0.1.30, 1 = round 190li)
//   fine=0|1           "Separate close notes" analysis (AnalyseOptions::fineSplit)
//   notes=<path.json>  write the analysed notes (before the edits) as JSON
//   shift=<sel>:<st>   pitch offset (semitones) added to the chosen notes
//   centre=<0..1>  variation=<0..2>  transition=<0..2>      the editor's global knobs (every note)
//   modulation=<0..2>  pitchdrift=<0..2>                     LYNX's split of Variation (round 190li; every note)
//   formant=<sel>:<st> volume=<sel>:<dB> stretch=<sel>:<factor, > 1 = longer> lengthen=<sel>:<seconds> (later notes move)
//   quantize=<grid seconds>
//   bench=<N>          render N more times and print the render's cost (ms, and CPU instructions per source second where
//                      the kernel exposes the counter): the engine's cost for an edit (the editor's preview plays the result)
// <sel> = "all", or "@<seconds>": the voiced note whose source span holds that time.
// Standalone build (no core library): g++ -std=c++20 -O2 -Isrc -DNEWTONELAB_STANDALONE tools/newtonelab.cpp
//   src/core/NewtoneFx.cpp src/core/VocalPitch.cpp
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Instruments.h"
#include "core/NewtoneFx.h"

#ifdef NEWTONELAB_STANDALONE
// (the parts of the core the Newtone engine does not use: Edison's score dump and Newtime's stretch)
#include "core/Song.h"
#include "core/TimeStretch.h"
namespace lynx
{
void Pattern::sortNotes() {}
Pattern& Song::activePattern() { std::abort(); }
std::shared_ptr<SampleData> timeStretch (const SampleData&, double, StretchMethod, double, const StretchOptions&) { std::abort(); }
}
#endif

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace
{
long long instructionsOf (void (*f) (void*), void* ctx)
{
#ifdef __linux__
    perf_event_attr a {};
    a.type = PERF_TYPE_HARDWARE;
    a.size = sizeof a;
    a.config = PERF_COUNT_HW_INSTRUCTIONS;
    a.disabled = 1;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    const auto fd = (int) syscall (SYS_perf_event_open, &a, 0, -1, -1, 0);
    if (fd >= 0)
    {
        ioctl (fd, PERF_EVENT_IOC_RESET, 0);
        ioctl (fd, PERF_EVENT_IOC_ENABLE, 0);
        f (ctx);
        ioctl (fd, PERF_EVENT_IOC_DISABLE, 0);
        long long n = 0;
        if (read (fd, &n, sizeof n) != (ssize_t) sizeof n)
            n = -1;
        close (fd);
        return n;
    }
#endif
    f (ctx);
    return -1;
}

std::shared_ptr<lynx::SampleData> readWav (const char* path)
{
    std::ifstream in (path, std::ios::binary);
    std::vector<unsigned char> d ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0)
        return nullptr;
    int fmt = 0, ch = 0, bits = 0;
    double sr = 0;
    std::size_t i = 12;
    auto u16 = [&] (std::size_t at) { return (unsigned) d[at] | ((unsigned) d[at + 1] << 8); };
    auto u32 = [&] (std::size_t at) { return u16 (at) | (u16 (at + 2) << 16); };
    while (i + 8 <= d.size())
    {
        const auto n = (std::size_t) u32 (i + 4);
        if (std::memcmp (&d[i], "fmt ", 4) == 0)
        {
            fmt = (int) u16 (i + 8);
            ch = (int) u16 (i + 10);
            sr = u32 (i + 12);
            bits = (int) u16 (i + 22);
            if (fmt == 0xFFFE && n >= 26)
                fmt = (int) u16 (i + 32);
        }
        else if (std::memcmp (&d[i], "data", 4) == 0 && ch > 0)
        {
            auto s = std::make_shared<lynx::SampleData>();
            s->sampleRate = sr;
            const auto bytes = bits / 8;
            const auto frames = std::min (n, d.size() - i - 8) / (std::size_t) (bytes * ch);
            s->left.resize (frames);
            if (ch > 1)
                s->right.resize (frames);
            for (std::size_t f = 0; f < frames; ++f)
                for (int c = 0; c < std::min (ch, 2); ++c)
                {
                    const auto* p = &d[i + 8 + (f * (std::size_t) ch + (std::size_t) c) * (std::size_t) bytes];
                    float v = 0;
                    if (fmt == 3 && bits == 32)
                        std::memcpy (&v, p, 4);
                    else if (bits == 16)
                        v = (float) (std::int16_t) (p[0] | (p[1] << 8)) / 32768.0f;
                    else if (bits == 24)
                        v = (float) ((std::int32_t) ((std::uint32_t) p[0] << 8 | (std::uint32_t) p[1] << 16 | (std::uint32_t) p[2] << 24) >> 8) / 8388608.0f;
                    else if (bits == 32)
                        v = (float) ((double) (std::int32_t) ((std::uint32_t) p[0] | (std::uint32_t) p[1] << 8 | (std::uint32_t) p[2] << 16 | (std::uint32_t) p[3] << 24) / 2147483648.0);
                    (c == 0 ? s->left : s->right)[f] = v;
                }
            return s;
        }
        i += 8 + n + (n & 1);
    }
    return nullptr;
}

void writeWav (const char* path, const lynx::SampleData& s)
{
    const auto frames = s.frames();
    const int ch = s.isStereo() ? 2 : 1;
    std::FILE* f = std::fopen (path, "wb");
    if (f == nullptr)
        return;
    auto w32 = [&] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
    const auto data = (std::uint32_t) (frames * (std::size_t) ch * 4);
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + data);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (3);
    w16 ((std::uint16_t) ch);
    w32 ((std::uint32_t) s.sampleRate);
    w32 ((std::uint32_t) s.sampleRate * (std::uint32_t) ch * 4);
    w16 ((std::uint16_t) (ch * 4));
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 (data);
    for (std::size_t i = 0; i < frames; ++i)
    {
        std::fwrite (&s.leftData()[i], 4, 1, f);
        if (ch == 2)
            std::fwrite (&s.rightData()[i], 4, 1, f);
    }
    std::fclose (f);
}

std::vector<int> select (const lynx::newtonefx::Track& t, const std::string& sel, double fs)
{
    if (sel == "all")
        return {};
    if (! sel.empty() && sel[0] == '@')
    {
        const auto at = (std::int64_t) std::llround (std::atof (sel.c_str() + 1) * fs);
        for (int k = 0; k < (int) t.pitches.size(); ++k)
        {
            const auto& p = t.pitches[(std::size_t) k];
            if (p.voiced() && at >= p.origStart && at < p.voicedEnd())
                return { k };
        }
        for (int k = 0; k < (int) t.pitches.size(); ++k)
            if (at >= t.pitches[(std::size_t) k].origStart && at < t.pitches[(std::size_t) k].origEnd)
                return { k };
    }
    std::fprintf (stderr, "no note at %s\n", sel.c_str());
    std::exit (3);
}

template <typename F>
void each (lynx::newtonefx::Track& t, const std::vector<int>& which, F&& f)
{
    if (which.empty())
    {
        for (auto& p : t.pitches)
            f (p);
    }
    else
        for (const auto k : which)
            f (t.pitches[(std::size_t) k]);
}

void dumpNotes (const char* path, const lynx::newtonefx::Track& t, double fs)
{
    std::FILE* f = std::fopen (path, "w");
    if (f == nullptr)
        return;
    std::fprintf (f, "{\"sr\": %g, \"notes\": [", fs);
    bool first = true;
    for (const auto& p : t.pitches)
    {
        std::fprintf (f, "%s{\"start\": %.6f, \"end\": %.6f, \"voicedEnd\": %.6f, \"pitch\": %.4f, \"voiced\": %s}", first ? "" : ", ",
                      (double) p.origStart / fs, (double) p.origEnd / fs, (double) p.voicedEnd() / fs, p.origPitch, p.voiced() ? "true" : "false");
        first = false;
    }
    std::fprintf (f, "]}\n");
    std::fclose (f);
}
} // namespace

int main (int argc, char** argv)
{
    using namespace lynx::newtonefx;
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: newtonelab in.wav out.wav|- [engine=N fine=0|1 notes=x.json shift=all:3 ...]\n");
        return 1;
    }
    auto src = readWav (argv[1]);
    if (src == nullptr)
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    const auto fs = src->sampleRate;
    AnalyseOptions options;
    int engine = -1;
    std::vector<std::string> args (argv + 3, argv + argc);
    for (const auto& a : args)
        if (a.rfind ("fine=", 0) == 0)
            options.fineSplit = std::atoi (a.c_str() + 5) != 0;
        else if (a.rfind ("engine=", 0) == 0)
            engine = std::atoi (a.c_str() + 7);
    auto track = analyseNotes (*src, options);
    if (engine >= 0)
        track.engine = engine;
    for (const auto& a : args)
    {
        const auto eq = a.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = a.substr (0, eq), value = a.substr (eq + 1);
        const auto colon = value.find (':');
        const auto sel = colon == std::string::npos ? std::string ("all") : value.substr (0, colon);
        const auto amount = std::atof (colon == std::string::npos ? value.c_str() : value.c_str() + colon + 1);
        if (key == "notes")
            dumpNotes (value.c_str(), track, fs);
        else if (key == "shift")
            each (track, select (track, sel, fs), [&] (Note& p) { p.shift += amount; });
        else if (key == "centre")
            applyCentre (track, amount);
        else if (key == "variation")
            applyVariation (track, amount);
        else if (key == "transition")
            applyTransition (track, amount);
        else if (key == "modulation")
            applyModulation (track, amount);
        else if (key == "pitchdrift")
            applyPitchDrift (track, amount);
        else if (key == "formant")
            each (track, select (track, sel, fs), [&] (Note& p) { p.formant = std::exp2 (amount / 12.0); });
        else if (key == "volume")
            each (track, select (track, sel, fs), [&] (Note& p) { p.volume = std::pow (10.0, amount / 20.0); });
        else if (key == "stretch" || key == "lengthen")
        {
            const auto which = select (track, sel, fs);
            for (const auto k : which)
            {
                auto& p = track.pitches[(std::size_t) k];
                const auto len = (double) (p.voicedEnd() - p.origStart); // (the voiced part gets longer; the tail keeps its length)
                // (lengthen: by `amount` seconds; stretch: by the factor)
                const auto delta = (std::int64_t) std::llround (key == "lengthen" ? amount * fs : len * (amount - 1.0));
                const auto factor = (len + (double) delta) / std::max (1.0, len);
                const auto tail = p.origEnd - p.voicedEnd();
                // the voiced part stretched, the tail after it (silence, the next consonant) as it was: a split at the voice's end
                if (tail > 0 && splitNote (track, (std::size_t) k, p.voicedEnd()))
                {
                    auto& q = track.pitches[(std::size_t) k];
                    q.curEnd = q.curStart + (std::int64_t) std::llround (len) + delta;
                    q.stretch = 1.0 / factor;
                    for (auto j = (std::size_t) k + 1; j < track.pitches.size(); ++j)
                        track.pitches[j].curStart += delta, track.pitches[j].curEnd += delta;
                }
                else
                {
                    p.curEnd = p.origEnd + delta;
                    p.stretch = (double) (p.origEnd - p.origStart) / (double) (p.curEnd - p.curStart);
                    for (auto j = (std::size_t) k + 1; j < track.pitches.size(); ++j)
                        track.pitches[j].curStart += delta, track.pitches[j].curEnd += delta;
                }
            }
        }
        else if (key == "quantize")
            quantizeTime (track, {}, amount * fs);
        else if (key != "fine" && key != "engine" && key != "bench")
        {
            std::fprintf (stderr, "unknown option %s\n", a.c_str());
            return 1;
        }
    }
    track.edited = true;
    if (std::string (argv[2]) == "-")
        return 0;
    int bench = 0;
    for (const auto& a : args)
        if (a.rfind ("bench=", 0) == 0)
            bench = std::atoi (a.c_str() + 6);
    if (bench > 0)
    {
        struct Ctx
        {
            const lynx::SampleData* s;
            const Track* t;
            int n;
        } ctx { src.get(), &track, bench };
        const auto t0 = std::chrono::steady_clock::now();
        const auto ins = instructionsOf ([] (void* c) {
            auto* x = static_cast<Ctx*> (c);
            for (int k = 0; k < x->n; ++k)
                (void) renderEdits (*x->s, *x->t);
        }, &ctx);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / bench;
        const auto seconds = (double) src->frames() / fs;
        std::printf ("bench: %.1f ms per render of %.2f s (%.1f ms per second of audio)", ms, seconds, ms / seconds);
        if (ins > 0)
            std::printf (", %.1f M instructions per second of audio", (double) ins / bench / seconds / 1e6);
        std::printf ("\n");
    }
    auto out = renderEdits (*src, track);
    writeWav (argv[2], out != nullptr ? *out : *src);
    std::printf ("notes %d engine %d rendered %s\n", (int) track.pitches.size(), track.engine, out != nullptr ? "yes" : "no (unedited)");
    return 0;
}
