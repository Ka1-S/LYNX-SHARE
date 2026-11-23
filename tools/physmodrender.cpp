// physmodrender: LYNX Model's renders and cost per voice, headless (round 190ni).
//
//   physmodrender note <out.wav> <model 0-5 | table.lpm> <key> <velocity 0-1> <seconds> [--rate R] [--set index=value ...]
//         [--release S]        one note (mono 32-bit float WAV), the model's own preset values unless --set
//   physmodrender notes <list.txt> [--rate R]
//         one note per line: <out.wav> <model | table.lpm> <key> <velocity> <seconds> [index=value ...] [cents=C]
//         (batch, one process; cents = the note's own detune, e.g. a recording's tuning for the timbre score)
//   physmodrender bench [--model M] [--voices N] [--seconds S] [--rate R] [--block B] [--reps K] [--cpu sse2|avx]
//         N voices held (different keys, damping 0 so they keep ringing), rendered in blocks of B: instructions and
//         thread CPU time per voice-sample, % of one core per voice and for N voices at real time, modes per voice,
//         the engine's memory, the tables' size; then an output hash (the SSE and AVX paths must print the same)
//   physmodrender info      the compiled-in tables: anchors, modes, bytes, provenance hash

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <optional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
 #include <linux/perf_event.h>
 #include <sys/ioctl.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

#include "core/CpuFeatures.h"
#include "core/physmod/LynxModel.h"

using namespace lynx;

namespace
{
struct InstructionCounter
{
#if defined(__linux__)
    int fd = -1;
    InstructionCounter()
    {
        perf_event_attr a {};
        a.type = PERF_TYPE_HARDWARE;
        a.size = sizeof (a);
        a.config = PERF_COUNT_HW_INSTRUCTIONS;
        a.disabled = 1;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        fd = (int) syscall (SYS_perf_event_open, &a, 0, -1, -1, 0);
    }
    ~InstructionCounter()
    {
        if (fd >= 0)
            close (fd);
    }
    void start() { if (fd >= 0) { ioctl (fd, PERF_EVENT_IOC_RESET, 0); ioctl (fd, PERF_EVENT_IOC_ENABLE, 0); } }
    long long stop()
    {
        if (fd < 0)
            return -1;
        ioctl (fd, PERF_EVENT_IOC_DISABLE, 0);
        long long v = -1;
        if (::read (fd, &v, sizeof (v)) != (ssize_t) sizeof (v))
            v = -1;
        return v;
    }
#else
    void start() {}
    long long stop() { return -1; }
#endif
};

double threadMs()
{
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec ts {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec * 1e-6;
#else
    return 1000.0 * (double) std::clock() / CLOCKS_PER_SEC;
#endif
}

bool writeWav (const std::string& path, const std::vector<float>& mono, int rate)
{
    std::ofstream f (path, std::ios::binary);
    if (! f)
        return false;
    const auto put32 = [&f] (std::uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    const auto put16 = [&f] (std::uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    const auto bytes = (std::uint32_t) (mono.size() * 4);
    f.write ("RIFF", 4);
    put32 (36 + bytes);
    f.write ("WAVEfmt ", 8);
    put32 (16);
    put16 (3); // IEEE float
    put16 (1);
    put32 ((std::uint32_t) rate);
    put32 ((std::uint32_t) rate * 4);
    put16 (4);
    put16 (32);
    f.write ("data", 4);
    put32 (bytes);
    f.write (reinterpret_cast<const char*> (mono.data()), (std::streamsize) bytes);
    return (bool) f;
}

std::vector<std::uint8_t> readFile (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

// "<model | table.lpm>": the model number, or a table file (its model's preset values)
struct Source
{
    int model = 0;
    std::optional<lm::Table> table;
};
std::optional<Source> sourceOf (const std::string& s)
{
    Source src;
    if (s.size() > 4 && s.substr (s.size() - 4) == ".lpm")
    {
        const auto bytes = readFile (s);
        std::string err;
        src.table = lm::parseTable (bytes.data(), bytes.size(), &err);
        if (! src.table)
        {
            std::fprintf (stderr, "%s: %s\n", s.c_str(), err.c_str());
            return std::nullopt;
        }
        src.model = src.table->model;
        return src;
    }
    src.model = std::clamp (std::atoi (s.c_str()), 0, lm::modelCount - 1);
    return src;
}

bool renderNote (const std::string& out, const std::string& source, int key, float velocity, double seconds, int rate,
                 const std::vector<std::pair<int, double>>& sets, double releaseAt, float cents = 0.0f)
{
    const auto src = sourceOf (source);
    if (! src)
        return false;
    auto v = lm::presetValues (src->model);
    std::vector<double> state (v.begin(), v.end());
    for (const auto& [i, x] : sets)
        if (i >= 0 && i < lm::stateSize)
            state[(std::size_t) i] = x;
    const int length = (int) std::lround (seconds * rate);
    lm::RefNote n;
    n.start = 0;
    n.key = key;
    n.velocity = velocity;
    n.release = releaseAt > 0.0 ? (int) std::lround (releaseAt * rate) : -1;
    n.cents = cents;
    const auto stereo = lm::renderReference (state, { n }, rate, length, 256, src->table ? &*src->table : nullptr);
    std::vector<float> mono ((std::size_t) length);
    for (int i = 0; i < length; ++i)
        mono[(std::size_t) i] = stereo[(std::size_t) i * 2];
    if (! writeWav (out, mono, rate))
    {
        std::fprintf (stderr, "cannot write %s\n", out.c_str());
        return false;
    }
    return true;
}

std::vector<std::pair<int, double>> parseSets (const std::vector<std::string>& words)
{
    std::vector<std::pair<int, double>> sets;
    for (const auto& w : words)
        if (const auto eq = w.find ('='); eq != std::string::npos && eq > 0 && std::isdigit ((unsigned char) w[0]))
            sets.push_back ({ std::atoi (w.substr (0, eq).c_str()), std::atof (w.substr (eq + 1).c_str()) });
    return sets;
}

int bench (int model, int voices, double seconds, int rate, int block, int reps)
{
    auto v = lm::presetValues (model);
    v[(std::size_t) lm::p::damping] = 0.0; // (every T60 x4: the voices keep ringing through the measurement)
    v[(std::size_t) lm::p::release] = 1.0;
    InstructionCounter counter;
    std::printf ("model %s, %d voices, %d Hz, block %d, %s path\n", lm::modelName (model), voices, rate, block,
                 lm::modesUseAvx() ? "AVX" : "SSE");
    double bestNs = 1e30;
    long long bestIns = -1;
    std::uint64_t hash = 1469598103934665603ull;
    double modesMean = 0.0;
    int activeAtEnd = 0;
    for (int r = 0; r < reps; ++r)
    {
        lm::Engine e;
        e.prepare (rate);
        e.setValues (v.data());
        const auto& t = lm::builtInTable (model);
        const int lo = (int) std::ceil (t.anchors.front().key), hi = (int) std::floor (t.anchors.back().key);
        for (int k = 0; k < voices; ++k)
            e.note (lo + (k * std::max (1, hi - lo)) / std::max (1, voices - 1) % 128, 0.9f);
        std::vector<float> L ((std::size_t) block), R ((std::size_t) block);
        // the strikes' first 50 ms are not measured (the contact pulses), then `seconds` of ringing
        const int warm = rate / 20 / block + 1;
        for (int b = 0; b < warm; ++b)
        {
            std::fill (L.begin(), L.end(), 0.0f);
            std::fill (R.begin(), R.end(), 0.0f);
            e.render (L.data(), R.data(), block);
        }
        int modes = 0;
        for (int k = 0; k < lm::maxVoices; ++k)
            modes += e.modesOfVoice (k);
        modesMean = (double) modes / std::max (1, e.activeVoices());
        const int blocks = (int) (seconds * rate / block);
        long long voiceSamples = 0;
        const auto t0 = threadMs();
        counter.start();
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (L.begin(), L.end(), 0.0f);
            std::fill (R.begin(), R.end(), 0.0f);
            voiceSamples += (long long) e.activeVoices() * block;
            e.render (L.data(), R.data(), block);
            if (r == 0)
                for (int i = 0; i < block; ++i)
                {
                    std::uint32_t bits;
                    std::memcpy (&bits, &L[(std::size_t) i], 4);
                    hash = (hash ^ bits) * 1099511628211ull;
                }
        }
        const auto ins = counter.stop();
        const auto ms = threadMs() - t0;
        activeAtEnd = e.activeVoices();
        const auto ns = ms * 1e6 / (double) std::max (voiceSamples, 1ll);
        if (ns < bestNs)
            bestNs = ns;
        if (ins >= 0)
        {
            const auto per = (long long) std::llround ((double) ins / (double) std::max (voiceSamples, 1ll) * 1000.0);
            bestIns = bestIns < 0 ? per : std::min (bestIns, per);
        }
    }
    const auto corePerVoice = bestNs * rate / 1e9 * 100.0;
    std::printf ("modes per voice (mean) %.1f, voices still active at the end %d\n", modesMean, activeAtEnd);
    std::printf ("instructions per voice-sample %s\n", bestIns >= 0 ? (std::to_string (bestIns / 1000) + "." + std::to_string (bestIns % 1000 / 100)).c_str() : "n/a");
    std::printf ("CPU per voice-sample %.2f ns (best of %d), %.3f %% of one core per voice, %.2f %% for %d voices\n", bestNs, reps,
                 corePerVoice, corePerVoice * voices, voices);
    std::printf ("engine memory %zu bytes; tables %zu bytes on disk (all six), %zu bytes parsed\n", lm::Engine::footprintBytes(),
                 [] { std::size_t s = 0; for (int m = 0; m < lm::modelCount; ++m) s += lm::builtInTableBytes (m); return s; }(),
                 [] { std::size_t s = 0; for (int m = 0; m < lm::modelCount; ++m) s += lm::tableMemoryBytes (lm::builtInTable (m)); return s; }());
    std::printf ("output hash %016llx\n", (unsigned long long) hash);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    std::vector<std::string> a (argv + 1, argv + argc);
    const auto opt = [&a] (const char* name, double fallback) {
        for (std::size_t i = 0; i + 1 < a.size(); ++i)
            if (a[i] == name)
                return std::atof (a[i + 1].c_str());
        return fallback;
    };
    if (! a.empty() && a[0] == "--cpu" && a.size() > 1)
        a.erase (a.begin(), a.begin() + 2);
    for (std::size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == "--cpu")
            cpu::setLevelForTest (a[i + 1] == "sse2" ? cpu::sse2 : a[i + 1] == "avx" ? cpu::avx : cpu::avx2);
    const int rate = (int) opt ("--rate", 48000);
    if (a.size() >= 6 && a[0] == "note")
    {
        std::vector<std::string> rest;
        for (std::size_t i = 6; i < a.size(); ++i)
            if (a[i] == "--set" && i + 1 < a.size())
                rest.push_back (a[++i]);
        return renderNote (a[1], a[2], std::atoi (a[3].c_str()), (float) std::atof (a[4].c_str()), std::atof (a[5].c_str()), rate,
                           parseSets (rest), opt ("--release", -1.0))
                   ? 0
                   : 1;
    }
    if (a.size() >= 2 && a[0] == "notes")
    {
        std::ifstream f (a[1]);
        std::string line;
        int bad = 0, n = 0;
        while (std::getline (f, line))
        {
            std::istringstream in (line);
            std::vector<std::string> w;
            for (std::string x; in >> x;)
                w.push_back (x);
            if (w.size() < 5 || w[0][0] == '#')
                continue;
            ++n;
            float cents = 0.0f;
            for (std::size_t k = 5; k < w.size(); ++k)
                if (w[k].rfind ("cents=", 0) == 0)
                    cents = (float) std::atof (w[k].c_str() + 6);
            if (! renderNote (w[0], w[1], std::atoi (w[2].c_str()), (float) std::atof (w[3].c_str()), std::atof (w[4].c_str()), rate,
                              parseSets (std::vector<std::string> (w.begin() + 5, w.end())), -1.0, cents))
                ++bad;
        }
        std::printf ("rendered %d notes, %d failed\n", n - bad, bad);
        return bad == 0 ? 0 : 1;
    }
    if (! a.empty() && a[0] == "bench")
        return bench ((int) opt ("--model", 0), (int) opt ("--voices", 32), opt ("--seconds", 2.0), rate, (int) opt ("--block", 256),
                      (int) opt ("--reps", 5));
    if (! a.empty() && a[0] == "info")
    {
        for (int m = 0; m < lm::modelCount; ++m)
        {
            const auto& t = lm::builtInTable (m);
            std::printf ("%-14s %3zu anchors %4zu modes %6zu bytes  keys %.0f-%.0f  provenance ", lm::modelName (m), t.anchors.size(), t.modes.size(),
                         lm::builtInTableBytes (m), t.anchors.empty() ? 0.0 : t.anchors.front().key, t.anchors.empty() ? 0.0 : t.anchors.back().key);
            for (auto b : t.provenance)
                std::printf ("%02x", b);
            std::printf ("\n");
        }
        return 0;
    }
    std::fprintf (stderr, "usage: physmodrender note|notes|bench|info ... (see tools/physmodrender.cpp)\n");
    return 2;
}
