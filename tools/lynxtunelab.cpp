// LYNX vocal lab (round 190li): runs LYNX Tune (core/LynxTune.h, the real-time pitch corrector) offline on a WAV, in
// blocks, for the scoring in tools/vocallab/tunelab.py. The output is latency-compensated (the same length as the input,
// sample-aligned with it) so it can be compared with the source and the ideals directly.
//
// usage: lynxtunelab <in.wav> <out.wav|-> [name=value ...]
//   retune=<ms>  humanize=<%>  flex=<%>  scale=<0..13 (0 chromatic)>  key=<0..11>  formant=<0|1>  fshift=<st>
//   natvib=<dB>  range=<0..3>  low=<0|1>  tracking=<0..1>  mix=<0..1>  block=<samples, 256>  rev=<the hidden revision>
//   ignorevib=<0|1>  p<index>=<raw 0..1 value>  (any parameter by index)
//   trace=<path.csv>   per-block meters: time, sung note, target note, applied correction (semitones)
//   bench=<N>          process the file N more times for the timing (ns per sample, and CPU instructions if the
//                      kernel exposes the counter)
// Standalone build: g++ -std=c++20 -O2 -ffp-contract=off -Isrc tools/lynxtunelab.cpp src/core/LynxTune.cpp
//   src/core/VocalPitch.cpp -o lynxtunelab
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "core/Effects.h"
#include "core/LynxTune.h"
#include "core/VocalPitch.h"

namespace
{
struct Wav
{
    double sr = 48000.0;
    std::vector<float> x; // mono (a stereo file is mixed down)
};

bool readWav (const char* path, Wav& w)
{
    std::ifstream in (path, std::ios::binary);
    std::vector<unsigned char> d ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0)
        return false;
    int fmt = 0, ch = 0, bits = 0;
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
            w.sr = u32 (i + 12);
            bits = (int) u16 (i + 22);
            if (fmt == 0xFFFE && n >= 26)
                fmt = (int) u16 (i + 32);
        }
        else if (std::memcmp (&d[i], "data", 4) == 0 && ch > 0)
        {
            const auto bytes = bits / 8;
            const auto frames = std::min (n, d.size() - i - 8) / (std::size_t) (bytes * ch);
            w.x.assign (frames, 0.0f);
            for (std::size_t f = 0; f < frames; ++f)
                for (int c = 0; c < ch; ++c)
                {
                    const auto* p = &d[i + 8 + (f * (std::size_t) ch + (std::size_t) c) * (std::size_t) bytes];
                    float v = 0;
                    if (fmt == 3 && bits == 32)
                        std::memcpy (&v, p, 4);
                    else if (bits == 16)
                        v = (float) (std::int16_t) (p[0] | (p[1] << 8)) / 32768.0f;
                    else if (bits == 24)
                        v = (float) ((std::int32_t) ((std::uint32_t) p[0] << 8 | (std::uint32_t) p[1] << 16 | (std::uint32_t) p[2] << 24) >> 8) / 8388608.0f;
                    w.x[f] += v / (float) ch;
                }
            return true;
        }
        i += 8 + n + (n & 1);
    }
    return false;
}

void writeWav (const char* path, const std::vector<float>& x, double sr)
{
    std::FILE* f = std::fopen (path, "wb");
    if (f == nullptr)
        return;
    auto w32 = [&] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
    const auto data = (std::uint32_t) (x.size() * 4);
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + data);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (3);
    w16 (1);
    w32 ((std::uint32_t) sr);
    w32 ((std::uint32_t) sr * 4);
    w16 (4);
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 (data);
    std::fwrite (x.data(), 4, x.size(), f);
    std::fclose (f);
}

int openInstructionCounter()
{
    perf_event_attr a {};
    a.type = PERF_TYPE_HARDWARE;
    a.size = sizeof a;
    a.config = PERF_COUNT_HW_INSTRUCTIONS;
    a.disabled = 1;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    return (int) syscall (SYS_perf_event_open, &a, 0, -1, -1, 0);
}
} // namespace

int main (int argc, char** argv)
{
    using namespace lynx;
    namespace lt = lynx::lynxtune;
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: lynxtunelab in.wav out.wav|- [retune=ms humanize=%% flex=%% scale=N key=N ...]\n");
        return 1;
    }
    Wav src;
    if (! readWav (argv[1], src))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    EffectParams p {};
    const auto& inf = lt::info();
    for (std::size_t k = 0; k < inf.params.size(); ++k)
        p[k] = inf.params[k].defaultValue;
    p[lt::scale] = 0.0f; // chromatic unless asked
    int block = 256, bench = 0;
    std::string trace;
    for (int a = 3; a < argc; ++a)
    {
        const std::string s (argv[a]);
        const auto eq = s.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = s.substr (0, eq);
        const auto v = std::atof (s.c_str() + eq + 1);
        if (key == "retune") p[lt::retune] = (float) std::sqrt (std::clamp (v, 0.0, 400.0) / 400.0);
        else if (key == "humanize") p[lt::humanize] = (float) (v / 100.0);
        else if (key == "flex") p[lt::flexTune] = (float) (v / 100.0);
        else if (key == "scale") p[lt::scale] = (float) (v / (vocal::scaleCount - 1));
        else if (key == "key") p[lt::key] = (float) (v / 11.0);
        else if (key == "formant") p[lt::formantPreserve] = (float) v;
        else if (key == "fshift") p[lt::formantShift] = (float) (v / 14.0 + 0.5);
        else if (key == "natvib") p[lt::naturalVibrato] = (float) (v / 24.0 + 0.5);
        else if (key == "range") p[lt::range] = (float) (v / (lt::rangeCount - 1));
        else if (key == "low") p[lt::lowLatency] = (float) v;
        else if (key == "tracking") p[lt::tracking] = (float) v;
        else if (key == "mix") p[lt::mix] = (float) v;
        else if (key == "ignorevib") p[lt::ignoreVibrato] = (float) v;
        else if (key == "rev") p[lt::count] = (float) v; // the hidden revision (round 190li), right after the named ones
        else if (key == "block") block = std::max (1, (int) v);
        else if (key == "bench") bench = (int) v;
        else if (key == "trace") trace = s.substr (eq + 1);
        else if (key.size() > 1 && key[0] == 'p') p[(std::size_t) std::clamp (std::atoi (key.c_str() + 1), 0, maxEffectParams - 1)] = (float) v;
        else
        {
            std::fprintf (stderr, "unknown option %s\n", s.c_str());
            return 1;
        }
    }

    auto fx = lt::make (&p);
    fx->prepare (src.sr);
    const auto lat = fx->latencySamples();
    const auto n = (int) src.x.size();
    std::vector<float> buf ((std::size_t) (n + lat + block), 0.0f);
    std::copy (src.x.begin(), src.x.end(), buf.begin());
    std::FILE* tr = trace.empty() ? nullptr : std::fopen (trace.c_str(), "w");
    if (tr != nullptr)
        std::fprintf (tr, "t,sung,target,shift\n");
    auto runOnce = [&] (std::vector<float>& b, bool traced) {
        fx->reset();
        for (int i = 0; i + block <= (int) b.size(); i += block)
        {
            fx->process (b.data() + i, nullptr, block, p, 120.0);
            if (traced && tr != nullptr)
                std::fprintf (tr, "%.5f,%.4f,%.4f,%.5f\n", (double) (i + block - lat) / src.sr, 127.0 * fx->controlOutput (0),
                              127.0 * fx->controlOutput (1), 4.0 * (fx->controlOutput (2) - 0.5));
        }
    };
    runOnce (buf, true);
    if (tr != nullptr)
        std::fclose (tr);
    std::vector<float> out (buf.begin() + lat, buf.begin() + lat + n);
    if (std::string (argv[2]) != "-")
        writeWav (argv[2], out, src.sr);

    double nsPerSample = 0.0, insPerSample = -1.0;
    if (bench > 0)
    {
        const auto counter = openInstructionCounter();
        std::vector<float> b2;
        long long ins = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < bench; ++r)
        {
            b2.assign ((std::size_t) (n + lat + block), 0.0f);
            std::copy (src.x.begin(), src.x.end(), b2.begin());
            if (counter >= 0)
            {
                ioctl (counter, PERF_EVENT_IOC_RESET, 0);
                ioctl (counter, PERF_EVENT_IOC_ENABLE, 0);
            }
            runOnce (b2, false);
            if (counter >= 0)
            {
                ioctl (counter, PERF_EVENT_IOC_DISABLE, 0);
                long long c = 0;
                if (read (counter, &c, sizeof c) == (ssize_t) sizeof c)
                    ins += c;
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        nsPerSample = (double) std::chrono::duration_cast<std::chrono::nanoseconds> (t1 - t0).count() / ((double) bench * (double) b2.size());
        if (counter >= 0)
        {
            insPerSample = (double) ins / ((double) bench * (double) b2.size());
            close (counter);
        }
    }
    std::printf ("{\"latency\": %d, \"samples\": %d, \"ns_per_sample\": %.3f, \"instructions_per_sample\": %.2f}\n", lat, n, nsPerSample, insPerSample);
    return 0;
}
