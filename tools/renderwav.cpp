// renderwav: imports an .flp and renders a pattern with the built-in instruments.
// Usage: renderwav project.flp output.wav [pattern-index] [loops]
// Without an index, picks the pattern with the most notes.
//
// Real-time diagnostics (REA round 87), environment variables:
//   LYNX_BLOCK=n      block size (default 256)
//   LYNX_THREADS=n    n worker threads, as the app's audio engine uses
//   LYNX_REALTIME=1   paces the blocks at real time (the workers sleep between blocks, as with a sound card)
//   LYNX_WARMUP=n     n samples of stopped-transport silence before the song, like FL's render warm-up (1536–2304
//                     samples, REA round 154): the effects' parameter glides settle as in a running app (round 170i)
//   LYNX_PROFILE=1    per-channel / per-insert-effect CPU time, heaviest first
//   LYNX_ALLOCS=1     counts the heap allocations made while a block is processed (the audio thread and the
//                     workers; round 167e) and prints where the first distinct ones came from (Linux: addresses for
//                     addr2line -e renderwav)
// The block timing report (budget = block duration) is always printed.

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <time.h>
#include <atomic>
#include <new>
#if defined(__linux__)
 #include <execinfo.h>
#endif
#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <mmsystem.h>
#endif

#include "WavReader.h"
#include "core/FlpImport.h"
#include "core/PluginSubstitutes.h"
#include "core/RackRenderer.h"
#include "core/WorkerPool.h"

// LYNX_ALLOCS: every operator new while `counting` is set (inside RackRenderer::process) is counted
namespace allocs
{
std::atomic<bool> counting { false };
std::atomic<long> count { 0 };
constexpr int maxSites = 96, depth = 12;
struct Site
{
    void* frames[depth] {};
    int n = 0;
    long hits = 0;
};
Site sites[maxSites];
std::atomic<int> siteCount { 0 };
std::atomic_flag recording = ATOMIC_FLAG_INIT;

void note() noexcept
{
    count.fetch_add (1, std::memory_order_relaxed);
#if defined(__linux__)
    if (recording.test_and_set()) // (one at a time; backtrace() itself may allocate once: not counted, flag held)
        return;
    counting.store (false);
    void* f[depth + 2];
    const auto n = backtrace (f, depth + 2);
    const auto used = std::max (0, n - 2);
    bool found = false;
    for (int i = 0; i < siteCount.load() && ! found; ++i)
        if (sites[i].n == used && std::equal (f + 2, f + 2 + used, sites[i].frames))
            found = ++sites[i].hits > 0;
    if (! found && siteCount.load() < maxSites)
    {
        auto& s = sites[siteCount.load()];
        s.n = used;
        std::copy (f + 2, f + 2 + used, s.frames);
        s.hits = 1;
        siteCount.fetch_add (1);
    }
    counting.store (true);
    recording.clear();
#endif
}
} // namespace allocs

void* operator new (std::size_t n)
{
    if (allocs::counting.load (std::memory_order_relaxed))
        allocs::note();
    if (auto* p = std::malloc (n > 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    if (allocs::counting.load (std::memory_order_relaxed))
        allocs::note();
    return std::malloc (n > 0 ? n : 1);
}
void* operator new[] (std::size_t n, const std::nothrow_t& t) noexcept { return operator new (n, t); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
// like the app's audio threads: MMCSS "Pro Audio" on Windows (with a 1 ms timer for the real-time pacing)
void realtimePriority()
{
#if defined(_WIN32)
    using AvSet = void* (__stdcall*) (const wchar_t*, unsigned long*);
    static const auto avSet = [] () -> AvSet {
        if (auto* avrt = LoadLibraryW (L"avrt.dll"))
            return reinterpret_cast<AvSet> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadCharacteristicsW")));
        return nullptr;
    }();
    unsigned long task = 0;
    if (avSet == nullptr || avSet (L"Pro Audio", &task) == nullptr)
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

void writeWav (const char* path, const std::vector<float>& left, const std::vector<float>& right, int sampleRate)
{
    std::ofstream out (path, std::ios::binary);
    auto u32 = [&] (std::uint32_t v) { out.write ((const char*) &v, 4); };
    auto u16 = [&] (std::uint16_t v) { out.write ((const char*) &v, 2); };
    const auto frames = (std::uint32_t) left.size();
    const auto dataBytes = frames * 4;

    out.write ("RIFF", 4);
    u32 (36 + dataBytes);
    out.write ("WAVEfmt ", 8);
    u32 (16);
    u16 (1);  // PCM
    u16 (2);  // stereo
    u32 ((std::uint32_t) sampleRate);
    u32 ((std::uint32_t) sampleRate * 4);
    u16 (4);
    u16 (16);
    out.write ("data", 4);
    u32 (dataBytes);

    for (std::uint32_t i = 0; i < frames; ++i)
        for (float s : { left[i], right[i] })
        {
            const auto clipped = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
            u16 ((std::uint16_t) (std::int16_t) (clipped * 32767.0f));
        }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: renderwav project.flp output.wav [pattern] [loops]\n");
        return 2;
    }

    std::ifstream in (argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());

    lynx::Song song;

    // On Linux/WSL FL lives under /mnt/c; the .flp paths use backslashes.
    const std::string flRoot = std::getenv ("FL_ROOT") ? std::getenv ("FL_ROOT")
#if defined(_WIN32)
                                                       : "C:/Program Files/Image-Line/FL Studio 2026";
#else
                                                       : "/mnt/c/Program Files/Image-Line/FL Studio 2026";
#endif
    auto toLocal = [] (std::string p) {
        std::replace (p.begin(), p.end(), '\\', '/');
        // absolute Windows paths ("C:/...") → the WSL mount ("/mnt/c/...")
#if ! defined(_WIN32) // (the Windows build of this tool reads them as they are)
        if (p.size() > 2 && p[1] == ':' && p[2] == '/' && std::isalpha ((unsigned char) p[0]))
            p = "/mnt/" + std::string (1, (char) std::tolower ((unsigned char) p[0])) + p.substr (2);
#endif
        return p;
    };

    lynx::FlpImportOptions options;
    options.roots = { flRoot, flRoot + "/Data", "" };
    options.fileExists = [&] (const std::string& p) { return std::filesystem::exists (toLocal (p)); };
    options.readFile = [&] (const std::string& p) { // (the rack compressors' presets of Harmless/Harmor)
        std::ifstream f (toLocal (p), std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    };

    const auto importStart = std::chrono::steady_clock::now();
    try
    {
        song = lynx::importFlp (flp::parseProject (bytes), options);
        std::printf ("import: %.0f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - importStart).count());
    }
    catch (const flp::Error& e)
    {
        std::fprintf (stderr, "error: %s\n", e.what());
        return 1;
    }

    // LYNX_SUBSTITUTES=1: third-party effects become their LYNX substitutes, as in the app when none is installed
    // (otherwise they stay unresolved, i.e. silent pass-through); LYNX_DISABLE="t.k t.k": those insert slots off
    if (const char* e = std::getenv ("LYNX_SUBSTITUTES"); e != nullptr && std::atoi (e) != 0)
    {
        const auto context = lynx::substituteContextOf (song);
        for (std::size_t t = 0; t < song.mixer.size(); ++t)
            for (std::size_t k = 0; k < song.mixer[t].effects.size(); ++k)
                if (auto& fx = song.mixer[t].effects[k]; fx.kind == lynx::EffectKind::plugin && fx.plugin.foreign.format != lynx::ForeignPlugin::Format::none
                    && lynx::substituteEffectSlot (fx, lynx::pluginIdentity (fx.plugin.foreign), context))
                    std::printf ("insert %d: %s -> %s (slot %d.%d)\n", (int) t, lynx::substituteOriginalName (fx).c_str(), lynx::effectInfo (fx.kind).name, (int) t, (int) k);
    }
    // LYNX_SUBSTITUTES_BYPASS=1: the project's "substitutes bypassed" switch on (A/B)
    if (const char* e = std::getenv ("LYNX_SUBSTITUTES_BYPASS"); e != nullptr && std::atoi (e) != 0)
        song.substitutesBypassed = true;
    // LYNX_PARAM="t.k.p=v …": insert t, slot k, parameter p set to the normalised value v (experiments)
    if (const char* e = std::getenv ("LYNX_PARAM"))
    {
        std::istringstream list (e);
        std::string item;
        while (list >> item)
        {
            int t = -1, k = -1, prm = -1;
            float v = 0.0f;
            if (std::sscanf (item.c_str(), "%d.%d.%d=%f", &t, &k, &prm, &v) == 4 && t >= 0 && t < (int) song.mixer.size() && k >= 0
                && k < (int) song.mixer[(std::size_t) t].effects.size() && prm >= 0 && prm < lynx::maxEffectParams)
                song.mixer[(std::size_t) t].effects[(std::size_t) k].params[(std::size_t) prm] = v;
        }
    }
    if (const char* e = std::getenv ("LYNX_DISABLE"))
    {
        std::istringstream list (e);
        std::string item;
        while (list >> item)
            if (const auto dot = item.find ('.'); dot != std::string::npos)
            {
                const auto t = (std::size_t) std::atoi (item.substr (0, dot).c_str()), k = (std::size_t) std::atoi (item.substr (dot + 1).c_str());
                if (t < song.mixer.size() && k < song.mixer[t].effects.size())
                    song.mixer[t].effects[k].enabled = false;
            }
    }

    // "song" instead of the index: the whole song (playlist), like the app's export
    const bool wholeSong = argc > 3 && std::string (argv[3]) == "song";
    std::int64_t songTicks = 0;

    if (wholeSong) // with the app's empty tail (1 s), so the song does not wrap to its start inside the rendered tail
        song = lynx::makeExportSong (song, true, (std::int64_t) std::ceil (lynx::ticksPerSample (song.bpm, 44100) * 44100.0), songTicks);

    int chosen = argc > 3 && ! wholeSong ? std::atoi (argv[3]) : -1;

    if (chosen < 0 || chosen >= (int) song.patterns.size())
    {
        chosen = 0;
        for (int i = 0; i < (int) song.patterns.size(); ++i)
            if (song.patterns[(std::size_t) i].notes.size() > song.patterns[(std::size_t) chosen].notes.size())
                chosen = i;
    }

    song.currentPattern = chosen;
    const int loops = argc > 4 ? std::max (1, std::atoi (argv[4])) : 2;
    const auto& pattern = song.patterns[(std::size_t) chosen];
    const auto lengthTicks = wholeSong ? songTicks : pattern.effectiveLength (song.meter()) * loops;

    constexpr int sampleRate = 44100;
    const int blockSize = std::getenv ("LYNX_BLOCK") ? std::clamp (std::atoi (std::getenv ("LYNX_BLOCK")), 16, 8192) : 256;
    const auto tps = lynx::ticksPerSample (song.bpm, sampleRate);
    auto totalSamples = (std::size_t) (lengthTicks / tps) + sampleRate; // +1 s of tail
    if (const char* s = std::getenv ("LYNX_SECONDS"); s != nullptr && std::atof (s) > 0) // benchmark: render only the start
        totalSamples = std::min (totalSamples, (std::size_t) (std::atof (s) * sampleRate));

    std::printf ("%.1f BPM, %zu channels, pattern \"%s\" (%zu notes), %d loops, %.1f s\n", song.bpm,
                 song.channels.size(), pattern.name.c_str(), pattern.notes.size(), loops,
                 (double) totalSamples / sampleRate);

    for (std::size_t i = 0; i < song.channels.size(); ++i)
    {
        static const char* kinds[] = { "kick", "snare", "hat", "clap", "synth", "sampler", "audio clip", "plugin", "controller", "sakura", "drumpad", "boobass", "drumsynth", "harmless", "sytrus", "harmor", "gms", "morphine", "ogun", "minisynth", "dx10", "kepler", "poizone", "fruit kick", "bassdrum", "toxic", "threeosc", "autogun", "transistor bass", "simsynth", "wasp", "plucked", "beepmap", "sawer", "flkeys", "granulizer", "drumaxx", "slayer", "fruity dance", "directwave", "midi out", "patcher", "soundfont", "layer", "dashboard", "voltage", "vfx", "video player", "kepler exo", "wavetraveller", "fruity slicer", "slicex" };
        const auto k = (std::size_t) song.channels[i].instrument;
        std::printf ("  %2zu %-28s -> %s\n", i, song.channels[i].name.c_str(), k < std::size (kinds) ? kinds[k] : "?");
    }

    // like the app's export: instruments with FL render-quality settings use them (LYNX_LIVE=1: the live ones)
    lynx::setOfflineRendering (std::getenv ("LYNX_LIVE") == nullptr);
    std::map<std::string, std::shared_ptr<const lynx::SampleData>> cache;
    const auto buildStart = std::chrono::steady_clock::now();
    auto state = lynx::RackState::build (song, nullptr, sampleRate, [&] (const std::string& p) {
        auto& entry = cache[p];
        if (entry == nullptr)
            entry = lynx::tools::readWav (toLocal (p));
        if (entry == nullptr)
            std::printf ("  (could not read %s)\n", p.c_str());
        return entry;
    });
    std::printf ("rack build: %.0f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - buildStart).count());
    {
        std::size_t edges = 0, delayed = 0, tracksWithFx = 0;
        for (const auto& ins : state->mixerInputs)
            for (const auto& in : ins)
            {
                ++edges;
                const auto& d = state->sendDelays[(std::size_t) in.source];
                delayed += (std::size_t) in.delay < d.size() && d[(std::size_t) in.delay] != nullptr;
            }
        for (const auto& t : song.mixer)
            tracksWithFx += ! t.effects.empty();
        std::printf ("mixer: %zu edges (%zu with PDC delay), %zu tracks with effects\n", edges, delayed, tracksWithFx);
    }
    lynx::RackRenderer renderer;
    // LYNX_THREADS=n: n worker threads for the channels and mixer layers, as the app's audio engine uses
    std::unique_ptr<lynx::WorkerPool> pool;
    if (const char* t = std::getenv ("LYNX_THREADS"); t != nullptr && std::atoi (t) > 0)
    {
        pool = std::make_unique<lynx::WorkerPool> (std::atoi (t), [] { realtimePriority(); });
        renderer.setWorkerPool (pool.get());
    }
    renderer.prepare (sampleRate, std::max (2048, blockSize));
    const bool profile = std::getenv ("LYNX_PROFILE") != nullptr;
    const bool countAllocs = std::getenv ("LYNX_ALLOCS") != nullptr;
    long blocksWithAllocs = 0;
    const bool realtime = std::getenv ("LYNX_REALTIME") != nullptr;
#if defined(_WIN32)
    timeBeginPeriod (1);
    realtimePriority();
#endif
    renderer.setProfiling (profile);

    std::vector<float> left (totalSamples), right (totalSamples);
    // the units whose latency is only known after their first block (the app then re-plans PDC once)
    const auto latenciesBefore = state->currentLatencies();
    const auto started = std::chrono::steady_clock::now();
    double tick = 0.0;
    const double budgetMs = 1000.0 * blockSize / sampleRate;
    std::vector<double> wallMs, cpuMs;
    auto threadCpuMs = [] {
#if defined(_WIN32)
        FILETIME created, exited, kernel, user;
        GetThreadTimes (GetCurrentThread(), &created, &exited, &kernel, &user);
        return (((std::uint64_t) user.dwHighDateTime << 32 | user.dwLowDateTime) + ((std::uint64_t) kernel.dwHighDateTime << 32 | kernel.dwLowDateTime)) * 1e-4;
#else
        timespec ts {};
        clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
        return ts.tv_sec * 1000.0 + ts.tv_nsec * 1e-6;
#endif
    };

    if (const char* w = std::getenv ("LYNX_WARMUP"); w != nullptr && std::atoi (w) > 0)
    {
        std::vector<float> wl ((std::size_t) blockSize), wr ((std::size_t) blockSize);
        for (int done = 0, total = std::atoi (w); done < total; done += blockSize)
        {
            const int n = std::min (blockSize, total - done);
            std::fill (wl.begin(), wl.end(), 0.0f);
            std::fill (wr.begin(), wr.end(), 0.0f);
            renderer.process (state.get(), false, 0.0, n, wl.data(), wr.data());
        }
    }

    for (std::size_t pos = 0; pos < totalSamples; pos += blockSize)
    {
        const auto n = (int) std::min<std::size_t> (blockSize, totalSamples - pos);
        const bool playing = tick < (double) lengthTicks;
        const auto t0 = std::chrono::steady_clock::now();
        const auto c0 = threadCpuMs();
        const auto allocsBefore = allocs::count.load();
        allocs::counting.store (countAllocs);
        renderer.process (state.get(), playing, tick, n, left.data() + pos, right.data() + pos);
        allocs::counting.store (false);
        blocksWithAllocs += allocs::count.load() != allocsBefore;
        wallMs.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
        cpuMs.push_back (threadCpuMs() - c0);
        tick += n * tps;

        if (realtime) // the next block starts one block later, like a sound card's callback
            std::this_thread::sleep_until (started + std::chrono::microseconds ((long long) ((pos + (std::size_t) n) * 1.0e6 / sampleRate)));
    }

    {
        const auto after = state->currentLatencies();
        std::size_t index = song.channels.size();
        for (std::size_t t = 0; t < song.mixer.size(); ++t)
            for (std::size_t k = 0; k < state->effects[t].size(); ++k, ++index)
                if (index < after.size() && after[index] != latenciesBefore[index])
                    std::printf ("latency changed after the first block: insert %zu slot %zu %s: %d -> %d\n", t, k + 1,
                                 lynx::effectInfo (song.mixer[t].effects[k].kind).name, latenciesBefore[index], after[index]);
    }
    std::printf ("render: %.0f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count());
    {
        auto report = [&] (const char* what, std::vector<double> v) {
            if (v.empty())
                return;
            double sum = 0.0;
            std::size_t over = 0, nearly = 0;
            for (auto x : v)
                sum += x, over += x >= budgetMs, nearly += x >= 0.8 * budgetMs && x < budgetMs;
            std::sort (v.begin(), v.end());
            auto pct = [&] (double p) { return v[std::min (v.size() - 1, (std::size_t) (p * (double) v.size()))]; };
            std::printf ("blocks %s: %zu x %d samples (budget %.2f ms): mean %.0f%%, p50 %.0f%%, p99 %.0f%%, max %.0f%%; "
                         "over budget %zu (%.2f%%), 80-100%% %zu\n",
                         what, v.size(), blockSize, budgetMs, 100.0 * sum / (double) v.size() / budgetMs, 100.0 * pct (0.5) / budgetMs,
                         100.0 * pct (0.99) / budgetMs, 100.0 * v.back() / budgetMs, over, 100.0 * (double) over / (double) v.size(), nearly);
        };
        report ("wall", wallMs);
        {
            // where the slowest blocks were (seconds into the render)
            std::vector<std::size_t> order (cpuMs.size());
            for (std::size_t i = 0; i < order.size(); ++i)
                order[i] = i;
            const auto& by = pool == nullptr ? cpuMs : wallMs;
            std::sort (order.begin(), order.end(), [&] (std::size_t a, std::size_t b) { return by[a] > by[b]; });
            std::printf ("slowest blocks:");
            for (std::size_t i = 0; i < order.size() && i < 8; ++i)
                std::printf (" %.2fs=%.0f%%", (double) (order[i] * (std::size_t) blockSize) / sampleRate, 100.0 * by[order[i]] / budgetMs);
            std::printf ("\n");
        }
        if (pool == nullptr)
            report ("thread-cpu", cpuMs);
#if ! defined(LYNX_OLDPOOL)
        if (pool != nullptr)
            std::printf ("workers: %d threads ran %llu indices, %llu wake-ups\n", pool->workers(),
                         (unsigned long long) pool->indicesRunByWorkers(), (unsigned long long) pool->wakeUps());
#endif
    }

    if (countAllocs)
    {
        std::printf ("allocations while processing: %ld in %ld of %zu blocks\n", allocs::count.load(), blocksWithAllocs, wallMs.size());
        for (int i = 0; i < allocs::siteCount.load(); ++i)
        {
            std::printf ("  site %d (%ld):", i, allocs::sites[i].hits);
#if defined(__linux__)
            if (char** names = backtrace_symbols (allocs::sites[i].frames, allocs::sites[i].n))
            {
                for (int k = 0; k < allocs::sites[i].n; ++k)
                    std::printf (" %s", std::strrchr (names[k], '(') != nullptr ? std::strrchr (names[k], '(') : names[k]);
                std::free (names);
            }
#endif
            std::printf ("\n");
        }
    }

    if (profile)
    {
        const auto& u = renderer.unitTimes();
        const auto total = (double) u.processNs.load();
        struct Row
        {
            double ns;
            std::string name;
            double maxNs = 0.0;
        };
        std::vector<Row> rows;
        for (std::size_t c = 0; c < song.channels.size() && c < (std::size_t) lynx::RackRenderer::maxChannels; ++c)
            if (const auto ns = (double) u.channelNs[c].load(); ns > 0.0)
                rows.push_back ({ ns, "channel " + std::to_string (c) + " \"" + song.channels[c].name + "\"", (double) u.channelMaxNs[c].load() });
        for (std::size_t t = 0; t < song.mixer.size(); ++t)
            for (std::size_t k = 0; k < song.mixer[t].effects.size() && k < (std::size_t) lynx::maxEffectSlots; ++k)
                if (const auto ns = (double) u.effectNs[t * lynx::maxEffectSlots + k].load(); ns > 0.0)
                    rows.push_back ({ ns, "insert " + std::to_string (t) + " slot " + std::to_string (k + 1) + " "
                                              + lynx::effectInfo (song.mixer[t].effects[k].kind).name
                                              + (song.mixer[t].effects[k].plugin.name.empty() ? "" : " (" + song.mixer[t].effects[k].plugin.name + ")"),
                                          (double) u.effectMaxNs[t * lynx::maxEffectSlots + k].load() });
        std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.ns > b.ns; });
        const auto audioMs = 1000.0 * (double) u.processedSamples.load() / sampleRate;
        std::printf ("profile: process %.0f ms for %.0f ms of audio (units below are CPU time, summed over threads)\n", total * 1e-6, audioMs);
        for (std::size_t i = 0; i < rows.size() && i < 25; ++i)
            std::printf ("  %6.1f%% of real time, longest call %7.2f ms  %s\n", 100.0 * rows[i].ns * 1e-6 / std::max (1.0, audioMs),
                         rows[i].maxNs * 1e-6, rows[i].name.c_str());
        // the spikes: units whose single call took longest
        std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.maxNs > b.maxNs; });
        std::printf ("longest single calls:\n");
        for (std::size_t i = 0; i < rows.size() && i < 10; ++i)
            std::printf ("  %7.2f ms  %s\n", rows[i].maxNs * 1e-6, rows[i].name.c_str());
    }

    // LYNX_RAW=path: also write the un-normalized float samples (L block then R block), for bit-exact comparisons
    if (const char* raw = std::getenv ("LYNX_RAW"); raw != nullptr)
    {
        std::ofstream r (raw, std::ios::binary);
        r.write ((const char*) left.data(), (std::streamsize) (left.size() * sizeof (float)));
        r.write ((const char*) right.data(), (std::streamsize) (right.size() * sizeof (float)));
    }

    // normalize the peak to -1 dBFS
    float peak = 0.0f;
    for (std::size_t i = 0; i < totalSamples; ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    std::printf ("peak before normalizing: %.4f\n", peak);

    if (peak > 0.0f)
    {
        const auto gain = 0.891f / peak;
        for (std::size_t i = 0; i < totalSamples; ++i)
        {
            left[i] *= gain;
            right[i] *= gain;
        }
    }

    writeWav (argv[2], left, right, sampleRate);
    std::printf ("written to %s\n", argv[2]);
    return 0;
}
