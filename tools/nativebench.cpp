// nativebench (round 190pz): what every LYNX native engine costs, measured as the mixer and the Channel Rack run it.
//
// For each built-in effect (FL-native equivalents, LYNX's own devices, the Live-device stand-ins, the plugin pack's
// engines) and each built-in instrument, a one-channel song is rendered through RackRenderer as perfbench does (one
// thread, live quality, flush-to-zero as on LYNX's audio threads), with and without the engine; the difference of the
// instructions retired (Linux perf_event: exact, whatever the machine's load) is the engine's own cost:
//
//   effects      signal      a programme (kick, hats, a saw-chord pad; about -18 dBFS RMS) through the slot
//                silence     no input, the slot's smart disable off: does the effect sleep by itself?
//                smart       no input, smart disable on (the default), after its hold: does the renderer take it out?
//                automation  the signal with the slot's first two parameters automated (a point every 1/16 beat)
//   instruments  signal      a 4-note chord held 3.5 of every 4 beats, an 8th-note line an octave up
//                silence     2 s after the song stopped (its notes ended): idle() (the renderer skips the channel) or still rendering?
//
// plus the memory one instance takes (heap bytes held after the rack build: the first instance and each further one),
// the allocations made while rendering (operator new on the audio thread: must be 0), the block-to-block spread of the
// engine's instructions (spikes) and the thread CPU time (noisy: an indication only; decisions rest on instructions).
//
// usage: nativebench [--fl <FL Studio's Data folder>] [--rates 44100,48000] [--blocks 128,512] [--seconds S]
//                    [--only name[,name...]] [--kinds fx|inst|all] [--tsv file] [--hashes file]
//   --hashes: one line per engine, mode and configuration with the hash of the output (a change that must be exact
//             gives the same file before and after)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

#include <execinfo.h>
#include <malloc.h>
#include <time.h>
#if defined(__linux__)
 #include <linux/perf_event.h>
 #include <sys/ioctl.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif
#if defined(__x86_64__)
 #include <xmmintrin.h>
#endif

#include "WavReader.h"
#include "core/Effects.h"
#include "core/FlpImport.h"
#include "core/Instruments.h"
#include "core/LiveEventQueue.h"
#include "core/NativeEngines.h"
#include "core/RackRenderer.h"
#include "core/Scheduler.h"
#include "core/SynthPresets.h"

// allocations on the measuring thread while it renders (the audio thread must not allocate)
namespace
{
std::atomic<bool> countingAllocs { false };
std::atomic<long> allocCount { 0 };
// LYNX_NB_ALLOCTRACE=n: the call stacks of the first n allocations made while rendering (where an engine allocates)
int allocTraces = -1;
void noteAlloc (std::size_t size)
{
    allocCount.fetch_add (1, std::memory_order_relaxed);
    if (allocTraces < 0)
        allocTraces = std::getenv ("LYNX_NB_ALLOCTRACE") != nullptr ? std::atoi (std::getenv ("LYNX_NB_ALLOCTRACE")) : 0;
    if (allocTraces > 0)
    {
        --allocTraces;
        countingAllocs.store (false);
        void* frames[24];
        const auto n = backtrace (frames, 24);
        std::fprintf (stderr, "--- allocation of %zu bytes while rendering:\n", size);
        backtrace_symbols_fd (frames, n, 2);
        countingAllocs.store (true);
    }
}
} // namespace

void* operator new (std::size_t size)
{
    if (countingAllocs.load (std::memory_order_relaxed))
        noteAlloc (size);
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    if (countingAllocs.load (std::memory_order_relaxed))
        noteAlloc (size);
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new[] (std::size_t size, const std::nothrow_t& t) noexcept { return operator new (size, t); }
void* operator new (std::size_t size, std::align_val_t al)
{
    if (countingAllocs.load (std::memory_order_relaxed))
        noteAlloc (size);
    const auto a = std::max<std::size_t> ((std::size_t) al, sizeof (void*));
    if (auto* p = std::aligned_alloc (a, (size + a - 1) / a * a))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size, std::align_val_t al) { return operator new (size, al); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }

namespace
{
using namespace lynx;
namespace fs = std::filesystem;

double threadCpuMs()
{
    timespec ts {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec * 1e-6;
}

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
    void stop() { if (fd >= 0) ioctl (fd, PERF_EVENT_IOC_DISABLE, 0); }
    long long read()
    {
        long long v = -1;
        if (fd >= 0 && ::read (fd, &v, sizeof (v)) != (ssize_t) sizeof (v))
            v = -1;
        return v;
    }
#else
    void start() {}
    void stop() {}
    long long read() { return -1; }
#endif
};

std::size_t heapBytes()
{
    const auto m = mallinfo2();
    return m.uordblks + m.hblkhd;
}

std::string local (std::string p)
{
    std::replace (p.begin(), p.end(), '\\', '/');
    return existingPathIgnoringCase (p);
}

natives::FlAccess flAccess (const std::string& data)
{
    natives::FlAccess fl;
    if (data.empty())
        return fl;
    fl.roots.data = fs::path (data).string();
    fl.roots.factoryData = fs::path (data).parent_path().string();
    fl.fileExists = [] (const std::string& p) { return fs::is_regular_file (local (p)); };
    fl.readFile = [] (const std::string& p) {
        std::ifstream f (local (p), std::ios::binary);
        return std::vector<std::uint8_t> { std::istreambuf_iterator<char> (f), {} };
    };
    fl.listPresets = [] (const std::string& folder) {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator (local (folder), ec))
            if (e.is_regular_file())
                out.push_back (e.path().string());
        std::sort (out.begin(), out.end());
        return out;
    };
    fl.saveEmbedded = [] (const std::vector<std::uint8_t>& wav, const std::string& name) {
        const auto path = (fs::temp_directory_path() / ("lynx-nativebench-" + std::to_string (::getpid()) + "-" + std::to_string (wav.size()) + "-"
                                                        + std::to_string (name.size()) + ".wav")).string();
        std::ofstream (path, std::ios::binary).write ((const char*) wav.data(), (std::streamsize) wav.size());
        return path;
    };
    return fl;
}

// the effects' programme at `rate`: a kick every beat, 16th hats, a saw-chord pad (120 BPM), deterministic
std::shared_ptr<SampleData> programme (double rate, double seconds)
{
    auto d = std::make_shared<SampleData>();
    d->sampleRate = rate;
    const auto frames = (std::size_t) (rate * seconds);
    d->left.resize (frames);
    d->right.resize (frames);
    std::uint32_t seed = 987654321u;
    const double chord[4] = { 130.81, 164.81, 196.0, 246.94 };
    double phase[8] {};
    double lp = 0.0, lpR = 0.0;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto t = (double) i / rate;
        const auto beat = std::fmod (t * 2.0, 1.0) / 2.0;     // seconds since the beat
        const auto six = std::fmod (t * 8.0, 1.0) / 8.0;      // since the 16th
        const auto kick = std::exp (-beat * 18.0) * std::sin (6.283185307179586 * (50.0 * beat + 2.2 * (1.0 - std::exp (-beat * 40.0))));
        seed = seed * 1664525u + 1013904223u;
        const auto noise = (double) ((int) (seed >> 9) - (1 << 22)) / (double) (1 << 22);
        const auto hat = std::exp (-six * 160.0) * noise;
        double padL = 0.0, padR = 0.0;
        for (int k = 0; k < 4; ++k)
        {
            for (int s = 0; s < 2; ++s)
            {
                auto& ph = phase[k * 2 + s];
                ph += chord[k] * (s == 0 ? 0.997 : 1.003) / rate;
                ph -= std::floor (ph);
                (s == 0 ? padL : padR) += 2.0 * ph - 1.0;
            }
        }
        lp += 0.08 * (padL - lp);
        lpR += 0.08 * (padR - lpR);
        d->left[i] = (float) (0.45 * kick + 0.08 * hat + 0.05 * lp);
        d->right[i] = (float) (0.45 * kick + 0.07 * hat + 0.05 * lpR);
    }
    return d;
}

struct Config
{
    double rate;
    int block;
};

struct Run
{
    long long instr = 0;
    double cpuMs = 0.0;
    std::vector<long long> blocks;
    std::uint64_t hash = 0;
    long allocs = 0;
    std::size_t samples = 0;
    std::size_t heapAfterBuild = 0, heapBefore = 0;
    double peakOut = 0.0;
};

std::uint64_t hashOf (const std::vector<float>& l, const std::vector<float>& r)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const auto* v : { &l, &r })
        for (const auto x : *v)
        {
            std::uint32_t bits;
            std::memcpy (&bits, &x, 4);
            h = (h ^ bits) * 1099511628211ull;
        }
    return h;
}

constexpr double bpm = 120.0;

// renders `song` for warmup + seconds (playing), measures the last `seconds`
// `playSeconds` >= 0: the transport stops after that many seconds (the instruments' silence: their notes end, the song stops)
Run render (const Song& song, const RackState::SampleProvider& provider, const Config& c, double warmup, double seconds, double playSeconds = -1.0)
{
    Run run;
    run.heapBefore = heapBytes();
    auto state = RackState::build (song, nullptr, c.rate, provider);
    run.heapAfterBuild = heapBytes();
    auto renderer = std::make_unique<RackRenderer>();
    renderer->prepare (c.rate, std::max (2048, c.block));
    auto live = std::make_unique<LiveEventQueue>();
    const auto tps = ticksPerSample (bpm, c.rate);
    std::vector<float> l ((std::size_t) c.block), r ((std::size_t) c.block);
    double tick = 0.0;
    const auto warmBlocks = (int) std::ceil (warmup * c.rate / c.block);
    const auto playBlocks = playSeconds < 0.0 ? 1 << 30 : (int) std::ceil (playSeconds * c.rate / c.block);
    int played = 0;
    auto playing = [&] { return played++ < playBlocks; };
    for (int b = 0; b < warmBlocks; ++b)
    {
        const auto on = playing();
        renderer->process (state.get(), on, tick, c.block, l.data(), r.data(), live.get(), nullptr);
        tick += on ? c.block * tps : 0.0;
    }
    const auto blocks = (int) std::ceil (seconds * c.rate / c.block);
    std::vector<float> outL, outR;
    outL.reserve ((std::size_t) blocks * (std::size_t) c.block);
    outR.reserve ((std::size_t) blocks * (std::size_t) c.block);
    run.blocks.reserve ((std::size_t) blocks);
    InstructionCounter counter;
    allocCount.store (0);
    const auto cpu0 = threadCpuMs();
    for (int b = 0; b < blocks; ++b)
    {
        const auto on = playing();
        countingAllocs.store (true, std::memory_order_relaxed);
        counter.start();
        renderer->process (state.get(), on, tick, c.block, l.data(), r.data(), live.get(), nullptr);
        counter.stop();
        countingAllocs.store (false, std::memory_order_relaxed);
        const auto n = counter.read();
        run.blocks.push_back (n);
        run.instr += n;
        tick += on ? c.block * tps : 0.0;
        outL.insert (outL.end(), l.begin(), l.end());
        outR.insert (outR.end(), r.begin(), r.end());
    }
    run.cpuMs = threadCpuMs() - cpu0;
    run.allocs = allocCount.load();
    run.samples = (std::size_t) blocks * (std::size_t) c.block;
    run.hash = hashOf (outL, outR);
    for (std::size_t i = 0; i < outL.size(); ++i)
        run.peakOut = std::max (run.peakOut, (double) std::max (std::fabs (outL[i]), std::fabs (outR[i])));
    return run;
}

Note note (int channel, double beat, double beats, int key, float velocity)
{
    Note n;
    n.channel = channel;
    n.start = (std::int64_t) (beat * ppq);
    n.length = (std::int64_t) (beats * ppq);
    n.key = key;
    n.velocity = velocity;
    return n;
}

// the effects' song: the programme as an audio clip on insert 1 (with `slots` on it, `copies` times on inserts 1..copies)
Song effectSong (const std::vector<EffectSlot>& slots, int copies, bool withInput, double seconds)
{
    Song song;
    song.bpm = bpm;
    song.mixer = Song::defaultMixer (std::max (2, copies));
    for (int k = 0; k < copies; ++k)
        for (const auto& s : slots)
            song.mixer[(std::size_t) (1 + k)].effects.push_back (s);
    ChannelDef ch;
    ch.name = "Programme";
    ch.instrument = InstrumentKind::audioClip;
    ch.samplePath = "/nativebench/programme.wav";
    ch.mixerTrack = 1;
    ch.volume = 0.8f;
    song.channels.push_back (ch);
    if (withInput)
    {
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = 0;
        clip.track = 0;
        clip.start = 0;
        clip.length = (std::int64_t) (seconds * bpm / 60.0 * ppq);
        song.playlist.push_back (clip);
    }
    song.mode = PlayMode::song;
    return song;
}

// the instruments' song: `channel` on insert 1 playing chords + a line for `beats` (none: no notes)
Song instrumentSong (const ChannelDef* channel, double beats)
{
    Song song;
    song.bpm = bpm;
    song.mixer = Song::defaultMixer (2);
    if (channel != nullptr)
    {
        auto ch = *channel;
        ch.mixerTrack = 1;
        song.channels.push_back (ch);
        auto& p = song.patterns.emplace_back();
        p.name = "Bench";
        const int roots[4] = { 48, 45, 41, 43 };
        for (int bar = 0; bar * 4 < (int) beats; ++bar)
        {
            const auto root = roots[bar % 4];
            for (int k : { 0, 4, 7, 11 })
                p.notes.push_back (note (0, bar * 4, 3.5, root + k, 0.75f));
            for (int e = 0; e < 8; ++e)
                p.notes.push_back (note (0, bar * 4 + e * 0.5, 0.4, root + 12 + (e * 5) % 12, 0.8f));
        }
        Clip clip;
        clip.pattern = 0;
        clip.start = 0;
        clip.length = (std::int64_t) (beats * ppq);
        song.playlist.push_back (clip);
    }
    song.mode = PlayMode::song;
    return song;
}

std::vector<std::string> split (const std::string& s, char sep = ',')
{
    std::vector<std::string> out;
    std::string cur;
    for (const auto ch : s)
        if (ch == sep)
        {
            if (! cur.empty())
                out.push_back (cur);
            cur.clear();
        }
        else
            cur += ch;
    if (! cur.empty())
        out.push_back (cur);
    return out;
}

std::string lower (std::string s)
{
    for (auto& ch : s)
        ch = (char) std::tolower ((unsigned char) ch);
    return s;
}

struct Stats
{
    double perSample = 0.0;   // engine instructions per sample
    double cpuPct = 0.0;      // engine thread CPU, % of a core at real time (noisy)
    double spike = 0.0;       // the engine's p99 block / its median block
    double worst = 0.0;       // the engine's longest block / its median block
};

Stats diff (const Run& with, const Run& base, double rate)
{
    Stats s;
    s.perSample = (double) (with.instr - base.instr) / (double) with.samples;
    const auto audioMs = 1000.0 * (double) with.samples / rate;
    s.cpuPct = 100.0 * (with.cpuMs - base.cpuMs) / audioMs;
    std::vector<long long> d;
    const auto n = std::min (with.blocks.size(), base.blocks.size());
    for (std::size_t i = 0; i < n; ++i)
        d.push_back (with.blocks[i] - base.blocks[i]);
    std::sort (d.begin(), d.end());
    if (! d.empty() && d[d.size() / 2] > 0)
    {
        s.spike = (double) d[std::min (d.size() - 1, (std::size_t) (0.99 * (double) d.size()))] / (double) d[d.size() / 2];
        s.worst = (double) d.back() / (double) d[d.size() / 2];
    }
    return s;
}
} // namespace

int main (int argc, char** argv)
{
#if defined(__x86_64__)
    _mm_setcsr (_mm_getcsr() | 0x8040u); // FTZ + DAZ, as LYNX's audio threads
#endif
    std::string flData, tsvPath, hashPath, kinds = "all";
    std::vector<Config> configs;
    std::vector<double> rates { 44100.0, 48000.0 };
    std::vector<int> blocks { 128, 512 };
    std::set<std::string> only;
    double seconds = 3.0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--fl")
            flData = next();
        else if (a == "--tsv")
            tsvPath = next();
        else if (a == "--hashes")
            hashPath = next();
        else if (a == "--kinds")
            kinds = next();
        else if (a == "--seconds")
            seconds = std::max (0.5, std::atof (next().c_str()));
        else if (a == "--rates")
        {
            rates.clear();
            for (const auto& r : split (next()))
                rates.push_back (std::atof (r.c_str()));
        }
        else if (a == "--blocks")
        {
            blocks.clear();
            for (const auto& b : split (next()))
                blocks.push_back (std::atoi (b.c_str()));
        }
        else if (a == "--only")
            for (const auto& n : split (next()))
                only.insert (lower (n));
    }
    for (const auto r : rates)
        for (const auto b : blocks)
            configs.push_back ({ r, b });
    const auto fl = flAccess (flData);

    std::FILE* tsv = tsvPath.empty() ? nullptr : std::fopen (tsvPath.c_str(), "w");
    std::FILE* hashes = hashPath.empty() ? nullptr : std::fopen (hashPath.c_str(), "w");
    if (tsv != nullptr)
        std::fprintf (tsv, "group\tname\tkind\trate\tblock\tmode\tinstr_per_sample\tcpu_pct\tp99_over_median\tworst_over_median\tallocs\tpeak\t"
                           "heap_first_kb\theap_next_kb\tnote\n");

    const double warmup = 1.0, smartWarmup = 6.0;
    std::map<double, std::shared_ptr<SampleData>> progs;
    for (const auto r : rates)
        progs[r] = programme (r, warmup + smartWarmup + seconds + 1.0);
    double currentRate = 44100.0;
    const RackState::SampleProvider provider = [&] (const std::string& p) -> std::shared_ptr<const SampleData> {
        if (p == "/nativebench/programme.wav")
            return progs[currentRate];
        return tools::readWav (local (p));
    };

    auto emit = [&] (const char* group, const std::string& name, int kind, const Config& c, const char* mode, const Stats& s, const Run& with,
                     double heapFirstKb, double heapNextKb, const std::string& note) {
        std::printf ("%-5s %-28s %6.0f/%-4d %-10s %9.1f instr/sample  %6.2f %%  spike %5.2f / %6.2f  allocs %ld  peak %.3f  heap %.0f + %.0f KB %s\n", group,
                     name.c_str(), c.rate, c.block, mode, s.perSample, s.cpuPct, s.spike, s.worst, with.allocs, with.peakOut, heapFirstKb, heapNextKb,
                     note.c_str());
        std::fflush (stdout);
        if (tsv != nullptr)
        {
            std::fprintf (tsv, "%s\t%s\t%d\t%.0f\t%d\t%s\t%.2f\t%.3f\t%.3f\t%.3f\t%ld\t%.5f\t%.1f\t%.1f\t%s\n", group, name.c_str(), kind, c.rate, c.block, mode,
                          s.perSample, s.cpuPct, s.spike, s.worst, with.allocs, with.peakOut, heapFirstKb, heapNextKb, note.c_str());
            std::fflush (tsv);
        }
        if (hashes != nullptr)
        {
            std::fprintf (hashes, "%s %s %.0f/%d %s %016llx\n", group, name.c_str(), c.rate, c.block, mode, (unsigned long long) with.hash);
            std::fflush (hashes);
        }
    };

    // ---- effects
    if (kinds == "all" || kinds == "fx")
    {
        // baselines per configuration and mode
        struct Base { Run signal, silence, smart; };
        std::map<int, Base> base;
        for (std::size_t k = 0; k < configs.size(); ++k)
        {
            currentRate = configs[k].rate;
            base[(int) k].signal = render (effectSong ({}, 1, true, 30.0), provider, configs[k], warmup, seconds);
            base[(int) k].silence = render (effectSong ({}, 1, false, 30.0), provider, configs[k], warmup, seconds);
            base[(int) k].smart = render (effectSong ({}, 1, false, 30.0), provider, configs[k], smartWarmup, seconds);
        }
        static const std::set<EffectKind> skipped { EffectKind::plugin, EffectKind::parallel, EffectKind::hardwareInsert };
        for (int kindIndex = 0; kindIndex < 256; ++kindIndex)
        {
            const auto kind = (EffectKind) kindIndex;
            if (kindIndex > (int) EffectKind::timeprism) // (the last kind)
                break;
            if (skipped.count (kind) != 0)
                continue;
            const auto name = std::string (effectInfo (kind).name != nullptr ? effectInfo (kind).name : "?");
            if (! only.empty() && only.count (lower (name)) == 0)
                continue;
            auto made = natives::newEffect (kind, fl, bpm);
            if (! made)
            {
                std::printf ("fx    %-28s not made here (needs FL Studio)\n", name.c_str());
                continue;
            }
            auto slot = *made;
            const auto& info = effectInfo (kind);
            for (std::size_t k = 0; k < configs.size(); ++k)
            {
                const auto& c = configs[k];
                currentRate = c.rate;
                // signal (+ the memory: one instance, two instances)
                const auto with = render (effectSong ({ slot }, 1, true, 30.0), provider, c, warmup, seconds);
                double firstKb = 0.0, nextKb = 0.0;
                if (k == 0)
                {
                    const auto two = render (effectSong ({ slot }, 2, true, 30.0), provider, c, 0.0, 0.05);
                    const auto baseHeap = (double) base[0].signal.heapAfterBuild - (double) base[0].signal.heapBefore;
                    firstKb = ((double) with.heapAfterBuild - (double) with.heapBefore - baseHeap) / 1024.0;
                    nextKb = ((double) two.heapAfterBuild - (double) two.heapBefore - ((double) with.heapAfterBuild - (double) with.heapBefore)) / 1024.0;
                }
                const auto sig = diff (with, base[(int) k].signal, c.rate);
                emit ("fx", name, kindIndex, c, "signal", sig, with, firstKb, nextKb, "");
                // silence, smart disable off: the effect's own behaviour
                auto awake = slot;
                awake.smartDisable = false;
                const auto quiet = render (effectSong ({ awake }, 1, false, 30.0), provider, c, warmup, seconds);
                const auto qs = diff (quiet, base[(int) k].silence, c.rate);
                char ratio[64];
                std::snprintf (ratio, sizeof (ratio), "x%.2f of signal", sig.perSample > 0.0 ? qs.perSample / sig.perSample : 0.0);
                emit ("fx", name, kindIndex, c, "silence", qs, quiet, 0.0, 0.0, ratio);
                // silence, smart disable as made (on): after the hold
                const auto smart = render (effectSong ({ slot }, 1, false, 30.0), provider, c, smartWarmup, seconds);
                const auto ss = diff (smart, base[(int) k].smart, c.rate);
                emit ("fx", name, kindIndex, c, "smart", ss, smart, 0.0, 0.0, slot.smartDisable ? "smart disable on" : "smart disable off as made");
                // automation: the first two parameters moving
                if (! info.params.empty())
                {
                    auto song = effectSong ({ slot }, 1, true, 30.0);
                    std::string names;
                    for (int p = 0; p < std::min<int> (2, (int) info.params.size()); ++p)
                    {
                        Automation a;
                        a.name = "p" + std::to_string (p);
                        a.target.kind = AutomationTargetKind::effectParam;
                        a.target.index = 1;
                        a.target.slot = 0;
                        a.target.param = p;
                        const auto d = info.params[(std::size_t) p].defaultValue;
                        const auto lo = std::clamp (d - 0.2f, 0.0f, 1.0f), hi = std::clamp (d + 0.2f, 0.0f, 1.0f);
                        for (double b = 0.0; b <= 64.0; b += 0.25)
                        {
                            AutomationPoint pt;
                            pt.tick = (std::int64_t) std::llround (b * ppq);
                            pt.value = lo + (hi - lo) * (float) (0.5 + 0.5 * std::sin (b * 0.9 + p));
                            a.points.push_back (pt);
                        }
                        song.automations.push_back (a);
                        Clip clip;
                        clip.kind = ClipKind::automation;
                        clip.automation = (int) song.automations.size() - 1;
                        clip.pattern = 0;
                        clip.track = 2 + p;
                        clip.start = 0;
                        clip.length = 64 * ppq;
                        song.playlist.push_back (clip);
                        names += (names.empty() ? "" : "+") + std::string (info.params[(std::size_t) p].name);
                    }
                    const auto moving = render (song, provider, c, warmup, seconds);
                    const auto ms = diff (moving, base[(int) k].signal, c.rate);
                    char text[160];
                    std::snprintf (text, sizeof (text), "x%.2f of signal (%s)", sig.perSample > 0.0 ? ms.perSample / sig.perSample : 0.0, names.c_str());
                    emit ("fx", name, kindIndex, c, "automation", ms, moving, 0.0, 0.0, text);
                }
            }
        }
    }

    // ---- instruments
    if (kinds == "all" || kinds == "inst")
    {
        struct Item
        {
            std::string name;
            ChannelDef channel;
        };
        std::vector<Item> items;
        auto own = [&] (const char* n, InstrumentKind kind) {
            ChannelDef ch;
            ch.name = n;
            ch.instrument = kind;
            items.push_back ({ n, ch });
        };
        own ("LYNX Synth (default)", InstrumentKind::synth);
        {
            const auto presets = synthPresets();
            for (std::size_t k : { (std::size_t) 3, (std::size_t) 7 })
                if (k < presets.size())
                {
                    ChannelDef ch;
                    ch.instrument = InstrumentKind::synth;
                    ch.synth = presets[k].second;
                    ch.name = "LYNX Synth (" + presets[k].first + ")";
                    items.push_back ({ ch.name, ch });
                }
        }
        own ("Kick", InstrumentKind::kick);
        own ("Snare", InstrumentKind::snare);
        own ("Hat", InstrumentKind::hat);
        own ("Clap", InstrumentKind::clap);
        own ("Fruity DrumSynth Live", InstrumentKind::drumsynth);
        for (const auto& e : natives::instrumentEngines())
        {
            auto c = natives::makeChannel (e.kind, fl);
            if (! c)
            {
                std::printf ("inst  %-28s not made here (needs FL Studio)\n", e.name);
                continue;
            }
            if (e.kind == InstrumentKind::granulizer && c->samplePath.empty() && ! flData.empty())
                c->samplePath = (fs::path (flData) / "Patches" / "Packs" / "Loops" / "DL Pendulus.wav").string();
            items.push_back ({ e.name, *c });
        }
        std::map<int, Run> base, baseStopped;
        for (std::size_t k = 0; k < configs.size(); ++k)
        {
            currentRate = configs[k].rate;
            base[(int) k] = render (instrumentSong (nullptr, 64.0), provider, configs[k], warmup, seconds);
            baseStopped[(int) k] = render (instrumentSong (nullptr, 64.0), provider, configs[k], 2.0 + 2.0, seconds, 2.0);
        }
        for (const auto& item : items)
        {
            if (! only.empty() && only.count (lower (item.name)) == 0)
                continue;
            for (std::size_t k = 0; k < configs.size(); ++k)
            {
                const auto& c = configs[k];
                currentRate = c.rate;
                const auto song = instrumentSong (&item.channel, 64.0);
                const auto with = render (song, provider, c, warmup, seconds);
                double firstKb = 0.0;
                if (k == 0)
                    firstKb = ((double) with.heapAfterBuild - (double) with.heapBefore - ((double) base[0].heapAfterBuild - (double) base[0].heapBefore)) / 1024.0;
                const auto sig = diff (with, base[(int) k], c.rate);
                emit ("inst", item.name, (int) item.channel.instrument, c, "signal", sig, with, firstKb, 0.0, with.peakOut < 1e-6 ? "SILENT" : "");
                // after the notes: 2 s of the song, then stopped (its notes end); measured from 2 s after the stop
                const auto quiet = render (song, provider, c, 2.0 + 2.0, seconds, 2.0);
                const auto qs = diff (quiet, baseStopped[(int) k], c.rate);
                char ratio[64];
                std::snprintf (ratio, sizeof (ratio), "x%.3f of signal", sig.perSample > 0.0 ? qs.perSample / sig.perSample : 0.0);
                emit ("inst", item.name, (int) item.channel.instrument, c, "silence", qs, quiet, 0.0, 0.0, ratio);
            }
        }
    }
    if (tsv != nullptr)
        std::fclose (tsv);
    if (hashes != nullptr)
        std::fclose (hashes);
    std::printf ("DONE\n");
    return 0;
}
