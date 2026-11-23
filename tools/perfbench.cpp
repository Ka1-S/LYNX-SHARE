// perfbench: the audio engine's CPU cost per block, headless (round perfcore190ge).
//
// Renders a scenario through RackRenderer as the app's audio callback does (live quality, a MeterBank, the live event
// queues) and reports, per repetition, the CPU time and the instructions retired by the rendering, as ns per sample
// and as % of one core at real time, plus a hash of the output samples (the same hash before and after a change =
// a bit-identical null test).
//
// usage: perfbench <scenario> [--block N] [--threads N] [--seconds S] [--reps R] [--offline] [--nometers] [--raw path]
//                              [--stopped] [--ahead] [--live C] [--paced] [--apptransport]
// scenarios:
//   demo            the showcase song (Demo.lynx), whole song
//   hiphop | pop    the two LYNX demo songs
//   synth100        100 built-in synth channels (presets in turn, chords + an arpeggio line) into 50 inserts with
//                   3 LYNX effects each (EQ / dynamics / space)
//   inserts50       one synth into 50 inserts in a chain of sends, 4 effects each: the mixer and effects alone
//   stream          24 audio-clip channels playing 60 s takes (long audio streaming)
//   idle            the synth100 project with the transport stopped and nothing playing (the cost of an idle project)
//   afterstop       the synth100 project played for --warmup seconds (not measured), then stopped (round 190gp: the
//                   seconds after Stop, while the effects' tails die away and smart disable takes them out)
//   big200          200 channels, 120 inserts with LYNX's own effects, 2,000 clips, 200 automation clips (round
//                   perfgate190id; this and the next: core/PerfProjects.h, the big-project performance gate)
//   automation      32 synths, 16 inserts, 400 dense automation clips and a tempo clip
//   calibrate       a fixed piece of work no LYNX change touches: the machine's speed (the gate divides by it)
//   flp:<path>      an FL Studio project, whole song (FL's own data from FL_ROOT, as renderwav)
// --gate: one machine-readable "GATE key=value ..." line at the end (CPU, instructions, import / build / rebuild / edit time,
// peak memory, hash; dropouts with --paced) for scripts/perf_gate.py.
// --threads N: N worker threads besides the calling one (the app's WorkerPool); the CPU time is then the process'
// (every thread, spinning included) and the instructions are the calling thread's only.
// Round 190gq (render-ahead, core/RenderAhead.h):
//   --apptransport  the position moves as the app's audio callback moves it (the tempo the renderer used, Transport's
//                   arithmetic); implied by --ahead, so a real-time and a render-ahead run of one scenario can be nulled
//   --ahead         render through RenderAhead as ChannelRackProcessor does (the device callback's model, the thread; it
//                   starts when the load needs it, as in the app); --ahead-always: whenever it can (the null tests)
//   --live C        channel C is played live (the selected channel): it and what it feeds stay real-time (repeatable)
//   --loop B        (with --apptransport) a loop region over the song's first B bars: the blocks are split at its end
//                   and the position wraps, as the app's callback does it (core/RenderAhead.h planSubBlock)
//   --paced         the blocks come at real time, as a device's callbacks: reports the callbacks that ran past their
//                   block's duration (dropouts), the worst callback, and the process CPU (every thread)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <time.h>
#if ! defined(_WIN32)
 #include <sys/resource.h>
#endif
#if defined(__linux__)
 #include <linux/perf_event.h>
 #include <sys/ioctl.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

#include "WavReader.h"
#include "core/DemoSongs.h"
#include "core/Effects.h"
#include "core/AlsImport.h"
#include "core/FlpImport.h"
#include "core/LiveEventQueue.h"
#include "core/LoadTrace.h"
#include "core/Meters.h"
#include "core/RackRenderer.h"
#include "core/RenderAhead.h"
#include "core/PerfProjects.h"
#include "core/ShowcaseSong.h"
#include "core/SynthPresets.h"
#include "core/WorkerPool.h"
#include "core/TrackStates.h"
#include <sstream>

// round 190gp: A/B switches of this round's changes, looked up weakly (the same tool links with the objects from before
// them, which do not have them): LYNX_DECLARED_TAILS=0 (smart disable's fixed hold), LYNX_SYNTH_LANES=0 (the synth's
// voices one by one)
namespace lynx
{
void setDeclaredTails (bool on) noexcept __attribute__ ((weak));
void setSynthVoiceLanes (bool on) noexcept __attribute__ ((weak));
}

namespace
{
using namespace lynx;

void applySwitches()
{
    auto off = [] (const char* name) { const char* v = std::getenv (name); return v != nullptr && std::atoi (v) == 0; };
    if (setDeclaredTails != nullptr)
        setDeclaredTails (! off ("LYNX_DECLARED_TAILS"));
    if (setSynthVoiceLanes != nullptr)
        setSynthVoiceLanes (! off ("LYNX_SYNTH_LANES"));
}

#if defined(_WIN32) // (no thread CPU clock here: the process' CPU time)
using clockid_t = int;
constexpr clockid_t CLOCK_THREAD_CPUTIME_ID = 0, CLOCK_PROCESS_CPUTIME_ID = 1;
double cpuMs (clockid_t) { return 1000.0 * (double) std::clock() / CLOCKS_PER_SEC; }
#else
double cpuMs (clockid_t clock)
{
    timespec ts {};
    clock_gettime (clock, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec * 1e-6;
}
#endif

// instructions retired in user space by the calling thread (-1 when the PMU is not available)
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
    void pause() { if (fd >= 0) ioctl (fd, PERF_EVENT_IOC_DISABLE, 0); }
    void resume() { if (fd >= 0) ioctl (fd, PERF_EVENT_IOC_ENABLE, 0); }
    long long read()
    {
        long long v = -1;
        if (fd >= 0 && ::read (fd, &v, sizeof (v)) != (ssize_t) sizeof (v))
            v = -1;
        return v;
    }
#else
    void start() {}
    void pause() {}
    void resume() {}
    long long read() { return -1; }
#endif
};

std::string toLocal (std::string p)
{
    std::replace (p.begin(), p.end(), '\\', '/');
    if (p.size() > 2 && p[1] == ':' && p[2] == '/' && std::isalpha ((unsigned char) p[0]))
        p = "/mnt/" + std::string (1, (char) std::tolower ((unsigned char) p[0])) + p.substr (2);
    return p;
}

Song importProject (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const std::string flRoot = std::getenv ("FL_ROOT") ? std::getenv ("FL_ROOT") : "/mnt/c/Program Files/Image-Line/FL Studio 2026";
    FlpImportOptions options;
    options.roots = { flRoot, flRoot + "/Data", "" };
    options.fileExists = [] (const std::string& p) { return std::filesystem::exists (toLocal (p)); };
    options.readFile = [] (const std::string& p) {
        std::ifstream f (toLocal (p), std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    };
    return importFlp (flp::parseProject (bytes), options);
}

// the process' peak resident memory in MB (-1 when unknown)
double peakRssMb()
{
#if ! defined(_WIN32)
    rusage usage {};
    if (getrusage (RUSAGE_SELF, &usage) == 0)
 #if defined(__APPLE__)
        return (double) usage.ru_maxrss / 1048576.0; // (bytes on macOS)
 #else
        return (double) usage.ru_maxrss / 1024.0; // (kilobytes on Linux)
 #endif
#endif
    return -1.0;
}

// perfgate190id: a fixed piece of DSP-like work (biquads, a soft clip, an interpolated table read over 256 KB) that no
// LYNX change touches. The gate divides its CPU times by this one's, so a faster or slower machine (CI runners differ)
// moves both alike; median of 7 runs, ms of thread CPU.
double calibrationMs()
{
    std::vector<float> buffer ((std::size_t) 1 << 16), table (4096);
    for (std::size_t i = 0; i < table.size(); ++i)
        table[i] = (float) std::sin (6.283185307179586 * (double) i / (double) table.size());
    std::uint32_t seed = 12345u;
    for (auto& x : buffer)
    {
        seed = seed * 1664525u + 1013904223u;
        x = (float) ((int) (seed >> 9) - (1 << 22)) / (float) (1 << 22);
    }
    std::vector<double> runs;
    volatile float sink = 0.0f;
    for (int run = 0; run < 7; ++run)
    {
        const auto t0 = cpuMs (CLOCK_THREAD_CPUTIME_ID);
        float z1 = 0.0f, z2 = 0.0f, y1 = 0.0f, y2 = 0.0f, phase = 0.0f;
        for (int pass = 0; pass < 48; ++pass)
            for (auto& x : buffer)
            {
                const auto in = x;
                const auto y = 0.2f * in + 0.4f * z1 + 0.2f * z2 + 0.6f * y1 - 0.25f * y2;
                z2 = z1, z1 = in, y2 = y1, y1 = y;
                phase += 0.013f;
                phase -= (float) (int) phase;
                const auto pos = phase * 4095.0f;
                const auto k = (int) pos;
                const auto frac = pos - (float) k;
                const auto t = table[(std::size_t) k] + frac * (table[(std::size_t) k + 1] - table[(std::size_t) k]);
                const auto v = y * 0.9f + 0.1f * t;
                x = v / (1.0f + std::fabs (v)) + 1e-7f * in;
            }
        runs.push_back (cpuMs (CLOCK_THREAD_CPUTIME_ID) - t0);
        sink = sink + buffer[(std::size_t) run];
    }
    std::sort (runs.begin(), runs.end());
    return runs[runs.size() / 2];
}

std::uint64_t hashSamples (const std::vector<float>& l, const std::vector<float>& r)
{
    std::uint64_t h = 1469598103934665603ull;
    auto add = [&h] (const std::vector<float>& v) {
        for (const auto x : v)
        {
            std::uint32_t bits;
            std::memcpy (&bits, &x, 4);
            h = (h ^ bits) * 1099511628211ull;
        }
    };
    add (l);
    add (r);
    return h;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: perfbench <demo|hiphop|pop|synth100|inserts50|stream|big200|automation|idle|calibrate|flp:path|als:path> [--block N] [--threads N] "
                              "[--seconds S] [--reps R] [--offline] [--nometers] [--raw path] [--stopped]\n");
        return 2;
    }
    applySwitches();
    const std::string scenario = argv[1];
    if (scenario == "calibrate") // (perfgate190id: the machine's speed, see calibrationMs)
    {
        const auto ms = calibrationMs();
        std::printf ("RESULT calibrate: %.2f ms\nGATE scenario=calibrate calib_ms=%.3f\n", ms, ms);
        return 0;
    }
    int blockSize = 256, threads = 0, reps = 5;
    double seconds = 0.0, warmup = 0.0;
    bool offline = false, meters = true, stopped = scenario == "idle" || scenario == "afterstop", units = false, aheadMode = false, paced = false, appTransport = false,
         aheadAlways = false;
    std::vector<int> liveChannels;
    std::vector<std::string> deactivateSpecs;
    int loopBars = 0;
    bool gate = false;
    const bool playWarmup = scenario == "afterstop"; // (the warmup plays, the measured part is stopped)
    const char* raw = nullptr;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? argv[++i] : (char*) "0"; };
        if (a == "--block")
            blockSize = std::clamp (std::atoi (next()), 16, 4096);
        else if (a == "--threads")
            threads = std::max (0, std::atoi (next()));
        else if (a == "--seconds")
            seconds = std::atof (next());
        else if (a == "--reps")
            reps = std::max (1, std::atoi (next()));
        else if (a == "--offline")
            offline = true;
        else if (a == "--nometers")
            meters = false;
        else if (a == "--raw")
            raw = next();
        else if (a == "--stopped")
            stopped = true;
        else if (a == "--warmup") // seconds rendered first and not measured (an idle project: past the effects' smart disable hold)
            warmup = std::max (0.0, std::atof (next()));
        else if (a == "--units") // the renderer's own per-channel / per-slot CPU (as renderwav's LYNX_PROFILE)
            units = true;
        else if (a == "--ahead")
            aheadMode = appTransport = true;
        else if (a == "--ahead-always") // (render-ahead whatever the load: the null tests)
            aheadMode = appTransport = aheadAlways = true;
        else if (a == "--live")
            liveChannels.push_back (std::atoi (next()));
        else if (a == "--paced")
            paced = true;
        else if (a == "--apptransport")
            appTransport = true;
        else if (a == "--loop")
            loopBars = std::max (0, std::atoi (next()));
        else if (a == "--gate") // (perfgate190id: one machine-readable GATE line at the end, scripts/perf_gate.py)
            gate = true;
        else if (a == "--deactivate") // round 190op (P4): "c:1,2" channels / "i:3,4" inserts deactivated (core/TrackStates.h)
            deactivateSpecs.push_back (next());
    }

    constexpr int sampleRate = 44100;
    std::map<std::string, std::shared_ptr<const SampleData>> cache;
    Song song;
    const auto import0 = std::chrono::steady_clock::now();
    if (scenario == "demo")
        song = makeShowcaseSong();
    else if (scenario == "hiphop")
        song = demos::make (1);
    else if (scenario == "pop")
        song = demos::make (2);
    else if (scenario == "idle" || scenario == "afterstop")
        song = perfprojects::synth100 (16);
    else if (perfprojects::make (scenario, song, cache)) // (the gate's synthetic projects, core/PerfProjects.h)
    {
    }
    else if (scenario.rfind ("als:", 0) == 0) // (round 190gq: a Live set, its arrangement on the Playlist)
    {
        const auto path = scenario.substr (4);
        std::ifstream in (path, std::ios::binary);
        std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        AlsImportOptions options;
        options.sessionOnPlaylist = true;
        options.common.projectFolder = std::filesystem::path (path).parent_path().string();
        options.common.fileExists = [] (const std::string& p) { return std::filesystem::exists (toLocal (p)); };
        options.common.readFile = [] (const std::string& p) {
            std::ifstream f (toLocal (p), std::ios::binary);
            return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        };
        options.libraryRoots = { "/mnt/c/ProgramData/Ableton/Live 12 Trial/Resources/Core Library" };
        std::string error;
        auto imported = importAls (bytes, options, &error);
        if (! imported)
        {
            std::fprintf (stderr, "error: %s\n", error.c_str());
            return 1;
        }
        song = std::move (*imported);
    }
    else if (scenario.rfind ("flp:", 0) == 0)
    {
        try
        {
            song = importProject (scenario.substr (4));
        }
        catch (const std::exception& e)
        {
            std::fprintf (stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    else
    {
        std::fprintf (stderr, "unknown scenario %s\n", scenario.c_str());
        return 2;
    }

    const auto importMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - import0).count();
    // round 190of: LYNX_PB_SMARTALL=1 = FL's Tools > Macros > "Switch smart disable for all plugins" (every slot on), and
    // LYNX_PB_SLOTS=1 lists how many slots have it on as imported
    {
        int on = 0, all = 0;
        for (auto& track : song.mixer)
            for (auto& slot : track.effects)
            {
                ++all;
                on += slot.smartDisable ? 1 : 0;
                if (std::getenv ("LYNX_PB_SMARTALL") != nullptr)
                    slot.smartDisable = true;
            }
        if (std::getenv ("LYNX_PB_SLOTS") != nullptr)
            std::printf ("slots: %d, smart disable on in %d%s\n", all, on, std::getenv ("LYNX_PB_SMARTALL") != nullptr ? " (now all on)" : "");
    }
    // round 190ok: LYNX_PB_REV=n gives every effect slot (inside Patchers and parallel branches too) engine revision n:
    // LYNX_PB_REV=1 renders as a project saved before round 190ok (the gold renders' check that those are bit for bit)
    if (const char* rev = std::getenv ("LYNX_PB_REV"))
    {
        const auto n = std::atoi (rev);
        std::function<void (EffectSlot&)> slotRev;
        std::function<void (PatcherDoc&)> docRev = [&] (PatcherDoc& doc) {
            for (auto& node : doc.nodes)
            {
                slotRev (node.effect);
                docRev (node.channel.patcher);
            }
        };
        slotRev = [&] (EffectSlot& slot) {
            slot.engineRev = n;
            for (auto& b : slot.branches)
                for (auto& inner : b.chain)
                    slotRev (inner);
            docRev (slot.patcher);
        };
        for (auto& track : song.mixer)
            for (auto& slot : track.effects)
                slotRev (slot);
        for (auto& c : song.channels)
            docRev (c.patcher);
    }
    // round 190op (P4): deactivated channels / inserts (RackState::build plays the projection: they are not loaded)
    for (const auto& spec : deactivateSpecs)
    {
        if (spec.size() < 3 || spec[1] != ':')
            continue;
        std::vector<int> list;
        std::stringstream ss (spec.substr (2));
        for (std::string item; std::getline (ss, item, ',');)
            if (! item.empty())
                list.push_back (std::atoi (item.c_str()));
        const auto n = spec[0] == 'c' ? deactivate::setChannels (song, list, true) : deactivate::setInserts (song, list, true);
        std::printf ("deactivated %d %s (%s)\n", n, spec[0] == 'c' ? "channel(s)" : "insert(s)", spec.c_str());
    }
    std::int64_t songTicks = 0;
    song = makeExportSong (song, true, (std::int64_t) std::ceil (ticksPerSample (song.bpm, sampleRate) * sampleRate), songTicks);
    const auto tps = ticksPerSample (song.bpm, sampleRate);
    auto totalSamples = (std::size_t) ((double) songTicks / tps);
    if (seconds > 0.0)
        totalSamples = std::min (totalSamples, (std::size_t) (seconds * sampleRate));
    if (stopped && seconds > 0.0)
        totalSamples = (std::size_t) (seconds * sampleRate);
    totalSamples = std::max<std::size_t> (totalSamples, (std::size_t) blockSize);
    if (appTransport) // (whole device blocks: a device keeps its block size; render-ahead drops its window when it changes)
        totalSamples = totalSamples / (std::size_t) blockSize * (std::size_t) blockSize;

    setOfflineRendering (offline);
    auto provider = [&] (const std::string& p) {
        auto& entry = cache[p];
        if (entry == nullptr)
            entry = tools::readWav (toLocal (p));
        return entry;
    };

    std::unique_ptr<WorkerPool> pool;
    if (threads > 0)
        pool = std::make_unique<WorkerPool> (threads);

    std::printf ("scenario %s: %zu channels, %zu inserts, %.1f s at %d Hz, block %d, %d worker threads, %s, meters %s%s\n", scenario.c_str(),
                 song.channels.size(), song.mixer.size(), (double) totalSamples / sampleRate, sampleRate, blockSize, threads,
                 offline ? "offline quality" : "live quality", meters ? "on" : "off", stopped ? ", stopped" : "");

    std::vector<double> cpu, nsPerSample, buildMs, droppedPerMin, overPerMin;
    std::vector<long long> instr;
    std::uint64_t firstHash = 0;
    bool sameHash = true;
    for (int rep = 0; rep < reps; ++rep)
    {
        const auto build0 = std::chrono::steady_clock::now();
        auto state = RackState::build (song, nullptr, sampleRate, provider);
        buildMs.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - build0).count());
        const auto* statePtr = state.get();
        auto renderer = std::make_unique<RackRenderer>();
        if (pool != nullptr)
            renderer->setWorkerPool (pool.get());
        renderer->prepare (sampleRate, std::max (2048, blockSize));
        renderer->setProfiling (units);
        auto bank = std::make_unique<MeterBank>();
        const auto levels = std::getenv ("LYNX_PB_LEVELS") != nullptr; // (round 190of)
        std::vector<float> trackPeaks (song.mixer.size(), 0.0f);
        auto live = std::make_unique<LiveEventQueue>();
        auto midiLive = std::make_unique<LiveEventQueue>();
        // render-ahead (round 190gq): the controller as ChannelRackProcessor drives it, the snapshot from an exchange
        SnapshotExchange<RackState> exchange;
        std::unique_ptr<RenderAhead> ahead;
        renderahead::Targets targets;
        renderahead::TransportModel model;
        MarkerPlan markers;
        if (aheadMode)
        {
            ahead = std::make_unique<RenderAhead> (*renderer);
            ahead->setWorkerPool (pool.get());
            ahead->prepare (sampleRate, std::max (2048, blockSize));
            ahead->setTargets (&targets);
            ahead->setAlwaysEngage (aheadAlways);
            targets.publish (liveChannels.data(), (int) liveChannels.size(), nullptr, 0);
            exchange.publish (std::move (state));
        }
        // the device callback's transport: linear through the export song, or round the loop region (--loop)
        model.sampleRate = sampleRate;
        model.deviceBlock = blockSize;
        model.songMode = loopBars > 0;
        model.loopEnd = loopBars > 0 ? (double) loopBars * song.beatsPerBar * ppq : 0.0;
        model.markers = &markers;

        std::vector<float> left (totalSamples), right (totalSamples);
        std::vector<double> blockCpu;
        blockCpu.reserve (totalSamples / (std::size_t) blockSize + 1);
        // the app's transport (--apptransport): the next block's position moves with the tempo this one used
        auto bpmNow = song.bpm;
        auto blockPlaying = ! stopped; // (the warmup of 190gp's afterstop plays; rollup 0.1.28)
        auto renderBlock = [&] (double tick, int n, float* l, float* r) {
            if (ahead != nullptr)
            {
                renderahead::Block b;
                b.playing = blockPlaying;
                b.startTick = tick;
                b.samples = n;
                b.tps = renderahead::transportTps (bpmNow, sampleRate);
                b.metronomeTrack = 0;
                b.model = &model;
                b.metering = meters;
                ahead->process (exchange, b, l, r, live.get(), meters ? bank.get() : nullptr, midiLive.get());
                if (pool != nullptr && ! ahead->ownsWorkers())
                    pool->park();
                const auto* used = ahead->currentState();
                bpmNow = used != nullptr && used->tempoMap ? renderer->currentBpm() : song.bpm;
                return;
            }
            renderer->process (statePtr, blockPlaying, tick, n, l, r, live.get(), meters ? bank.get() : nullptr, midiLive.get());
            if (pool != nullptr) // (as the app's audio callback ends: the workers sleep until the next block, REA round 116)
                pool->park();
            if (appTransport)
                bpmNow = statePtr->tempoMap ? renderer->currentBpm() : song.bpm;
        };
        auto stepTicks = [&] (int n) { return appTransport ? n * renderahead::transportTps (bpmNow, sampleRate) : n * tps; };
        double warmTick = 0.0;
        if (warmup > 0.0) // (stopped: the same position; playing: the song advances and the measured part starts later)
        {
            std::vector<float> wl ((std::size_t) blockSize), wr ((std::size_t) blockSize);
            for (int done = 0, total = (int) (warmup * sampleRate); done < total; done += blockSize)
            {
                const auto step = stepTicks (blockSize);
                const auto play = ! stopped || playWarmup; // (round 190gp's afterstop: the warmup plays)
                blockPlaying = play;
                renderBlock (play ? (appTransport ? warmTick : done * tps) : 0.0, blockSize, wl.data(), wr.data());
                warmTick += step;
            }
            blockPlaying = ! stopped;
            if (units)
                renderer->resetUnitTimes();
        }
        InstructionCounter counter;
        const auto clock = pool != nullptr || ahead != nullptr || paced ? CLOCK_PROCESS_CPUTIME_ID : CLOCK_THREAD_CPUTIME_ID;
        const auto warmupBlocks = (int) std::ceil (warmup * sampleRate / blockSize);
        double tick = stopped ? 0.0 : (appTransport ? warmTick : (double) warmupBlocks * blockSize * tps), used = 0.0;
        const auto wall0 = std::chrono::steady_clock::now();
        const auto period = std::chrono::duration<double> ((double) blockSize / sampleRate);
        auto due = wall0;
        int dropouts = 0, over = 0;
        double worstMs = 0.0, spinMs = 0.0;
        std::vector<double> callbackMs;
        const auto processCpu0 = cpuMs (CLOCK_PROCESS_CPUTIME_ID);
        const auto pool0 = pool != nullptr ? pool->stats() : WorkerPool::Stats {}; // (round 190kb)
        counter.start();
        for (std::size_t pos = 0; pos < totalSamples; pos += (std::size_t) blockSize)
        {
            const auto n = (int) std::min<std::size_t> ((std::size_t) blockSize, totalSamples - pos);
            if (paced) // the device asks for the next block at its time (sleep, then spin the last 300 µs: the wake-up's
            {          // timer slack is not the engine's; the spinning's CPU is taken out of the total below)
                std::this_thread::sleep_until (due - std::chrono::microseconds (300));
                const auto spin0 = cpuMs (CLOCK_THREAD_CPUTIME_ID);
                while (std::chrono::steady_clock::now() < due)
                    WorkerPool::relax();
                spinMs += cpuMs (CLOCK_THREAD_CPUTIME_ID) - spin0;
                due += std::chrono::duration_cast<std::chrono::steady_clock::duration> (period);
            }
            const auto c0 = cpuMs (clock);
            const auto w0 = std::chrono::steady_clock::now();
            if (appTransport && ! stopped)
            {
                // the callback's loop: sub-blocks up to the loop's end, the position as Transport moves it
                model.deviceBlock = n;
                for (int done = 0; done < n;)
                {
                    const auto tpsNow = renderahead::transportTps (bpmNow, sampleRate);
                    const auto sb = renderahead::planSubBlock (model, tick, done, tpsNow);
                    model.offset = done;
                    renderBlock (sb.start, sb.samples, left.data() + pos + (std::size_t) done, right.data() + pos + (std::size_t) done);
                    tick = sb.next;
                    done += sb.samples;
                }
            }
            else
            {
                const auto step = stepTicks (n);
                renderBlock (stopped ? 0.0 : tick, n, left.data() + pos, right.data() + pos);
                tick += stopped ? 0.0 : step;
            }
            const auto spent = cpuMs (clock) - c0;
            const auto wallSpent = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - w0).count();
            if (paced)
            {
                callbackMs.push_back (wallSpent);
                worstMs = std::max (worstMs, wallSpent);
                if (wallSpent > 1000.0 * n / sampleRate)
                    ++over; // the callback itself took longer than its block lasts
                if (std::chrono::steady_clock::now() > due) // finished after the next block was due: a dropout
                {
                    ++dropouts;
                    due = std::chrono::steady_clock::now(); // (the device goes on from now)
                }
            }
            used += spent;
            blockCpu.push_back (paced ? wallSpent : spent);
            if (meters) // the UI's meter reads (Mixer + Playlist headers), as often as a 60 Hz timer would
                if ((pos / (std::size_t) blockSize) % std::max<std::size_t> (1, (std::size_t) (sampleRate / 60 / blockSize)) == 0)
                {
                    counter.pause();
                    // round 190of: LYNX_PB_LEVELS=1 keeps each insert's highest peak over the last 2 s of rep 0 (what keeps a
                    // stopped project's chains awake: smart disable needs -96 dB)
                    const auto lastSeconds = rep == 0 && levels && (double) pos >= (double) totalSamples - 2.0 * sampleRate;
                    for (int t = 0; t < (int) song.mixer.size(); ++t)
                        for (int s = 0; s < 2; ++s)
                        {
                            const auto peak = bank->takePeak (t, s);
                            (void) bank->takeViewPeak (t, s);
                            if (lastSeconds && t < (int) trackPeaks.size())
                                trackPeaks[(std::size_t) t] = std::max (trackPeaks[(std::size_t) t], peak);
                        }
                    counter.resume();
                }
        }
        if (paced) // every thread's CPU, the render-ahead thread and the workers included (not the pacing's spin)
            used = cpuMs (CLOCK_PROCESS_CPUTIME_ID) - processCpu0 - spinMs;
        if (paced)
        {
            std::sort (callbackMs.begin(), callbackMs.end());
            const auto audioSeconds = (double) totalSamples / sampleRate;
            droppedPerMin.push_back (dropouts * 60.0 / audioSeconds);
            overPerMin.push_back (over * 60.0 / audioSeconds);
            std::printf ("paced: %d dropouts (%.2f per minute), %d callbacks over their block (%.2f per minute), worst callback %.3f ms, p99 %.3f ms, "
                         "median %.3f ms (budget %.3f ms)\n", dropouts, dropouts * 60.0 / audioSeconds, over, over * 60.0 / audioSeconds, worstMs, callbackMs[std::min (callbackMs.size() - 1, (std::size_t) (0.99 * (double) callbackMs.size()))],
                         callbackMs[callbackMs.size() / 2], 1000.0 * blockSize / sampleRate);
        }
        if (pool != nullptr) // round 190kb: where the workers' time went, per second of audio (ms/s = 0.1 % of a core)
        {
            const auto p = pool->stats();
            const auto audioSeconds = (double) totalSamples / sampleRate;
            const auto ms = [audioSeconds] (std::uint64_t ns) { return (double) ns / 1.0e6 / audioSeconds; };
            const auto per = [audioSeconds] (std::uint64_t n) { return (double) n / audioSeconds; };
            std::printf ("pool: rounds %.0f/s, work %.2f ms/s, spin %.2f ms/s (%.0f/s: found %.0f, parked %.0f, timed out %.0f), sleeps %.0f/s (early %.0f), "
                         "waits %.2f ms/s (%.0f/s, yields %.0f, sleeps %.0f = %.2f ms/s), caller wait %.2f ms/s (sleeps %.0f/s)\n",
                         per (p.rounds - pool0.rounds), ms (p.workNs - pool0.workNs), ms (p.spinNs - pool0.spinNs), per (p.spins - pool0.spins),
                         per (p.spinFound - pool0.spinFound), per (p.spinParked - pool0.spinParked), per (p.spinTimedOut - pool0.spinTimedOut),
                         per (p.sleeps - pool0.sleeps), per (p.earlyWakes - pool0.earlyWakes), ms (p.waitNs - pool0.waitNs), per (p.waits - pool0.waits),
                         per (p.waitYields - pool0.waitYields), per (p.waitSleeps - pool0.waitSleeps), ms (p.waitSleepNs - pool0.waitSleepNs),
                         ms (p.callerWaitNs - pool0.callerWaitNs), per (p.callerSleeps - pool0.callerSleeps));
            // round 190kz (sched): rounds per device block, and the blocks rendered batched (one round for all their pieces)
            const auto blocks = (double) ((totalSamples + (std::size_t) blockSize - 1) / (std::size_t) blockSize);
            std::printf ("sched: %.2f rounds per block, batched blocks %llu (one round %llu) since start, of %.0f measured blocks; two-round blocks %llu\n",
                         (double) (p.rounds - pool0.rounds) / std::max (1.0, blocks), (unsigned long long) renderer->batchedBlockCount(),
                         (unsigned long long) renderer->batchedRoundCount(), blocks, (unsigned long long) renderer->batchedLiveRoundCount());
            {
                static const char* const why[] = { "switched off", "no graph", "big block", "live controllers", "MIDI between units",
                                                   "launchers", "bindings", "held notes", "timed params", "many pieces", "other" };
                std::printf ("sched: not batched:");
                auto any = false;
                for (int k = 0; k < (int) RackRenderer::BatchRefusal::count; ++k)
                    if (const auto c = renderer->batchRefusedCount ((RackRenderer::BatchRefusal) k); c > 0)
                        std::printf (" %s %llu", why[k], (unsigned long long) c), any = true;
                std::printf ("%s\n", any ? "" : " none");
                if (*renderer->livePlanRefusal() != 0) // (round 190lv)
                    std::printf ("sched: live controllers kept piece by piece: %s\n", renderer->livePlanRefusal());
            }
        }
        if (ahead != nullptr)
        {
            const auto& st = ahead->stats();
            std::printf ("ahead: blocks %llu, from ring %llu, waited %llu, inline %llu, flushes %llu, drains %llu, engagements %llu, ahead channels %d, "
                         "ahead inserts %d, boundary %d, window %d samples, split mismatches %llu, backoffs %llu, probes %llu, kept on %llu (for fewer overruns %llu), kept off %llu (probe cpu %.1f%% off / %.1f%% on, overruns %.0f / %.0f per minute)\n",
                         (unsigned long long) st.blocks.load(), (unsigned long long) st.fromRing.load(), (unsigned long long) st.waited.load(),
                         (unsigned long long) st.inline_.load(), (unsigned long long) st.flushes.load(), (unsigned long long) st.drains.load(),
                         (unsigned long long) st.engagements.load(), st.aheadChannels.load(), st.aheadTracks.load(), st.boundary.load(), st.window.load(),
                         (unsigned long long) st.splitMismatches.load(), (unsigned long long) st.backoffs.load(),
                         (unsigned long long) st.probes.load(), (unsigned long long) st.keptOn.load(), (unsigned long long) st.keptForOverruns.load(), (unsigned long long) st.keptOff.load(),
                         (double) st.probeCpuOff.load(), (double) st.probeCpuOn.load(), (double) st.probeOverOff.load(), (double) st.probeOverOn.load());
        }
        counter.pause();
        const auto wallMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - wall0).count();
        const auto instructions = counter.read();
        const auto audioMs = 1000.0 * (double) totalSamples / sampleRate;
        std::sort (blockCpu.begin(), blockCpu.end());
        const auto p99 = blockCpu[std::min (blockCpu.size() - 1, (std::size_t) (0.99 * (double) blockCpu.size()))];
        const auto budget = 1000.0 * blockSize / sampleRate;
        const auto h = hashSamples (left, right);
        if (const char* dump = std::getenv ("LYNX_PB_DUMP"); dump != nullptr && rep == 0) // (round 190lv: the samples, for a diff)
            if (auto* f = std::fopen (dump, "wb"); f != nullptr)
            {
                std::fwrite (left.data(), sizeof (float), left.size(), f);
                std::fwrite (right.data(), sizeof (float), right.size(), f);
                std::fclose (f);
            }
        if (rep == 0 && levels) // (round 190of: the inserts still above -140 dB at the end)
            for (std::size_t t = 0; t < trackPeaks.size(); ++t)
                if (trackPeaks[t] > 1.0e-7f)
                    std::printf ("level insert %d \"%s\": %.1f dBFS%s\n", (int) t, song.mixer[t].name.c_str(), 20.0 * std::log10 (trackPeaks[t]),
                                 trackPeaks[t] > 1.5849e-5f ? " (above smart disable's -96 dB)" : "");
        if (rep == 0)
            firstHash = h;
        sameHash = sameHash && h == firstHash;
        cpu.push_back (used);
        nsPerSample.push_back (used * 1e6 / (double) totalSamples);
        instr.push_back (instructions);
        std::printf ("rep %d: cpu %.1f ms (%.2f%% of a core), wall %.1f ms, %.1f ns/sample, %.3f G instructions (%.0f/sample), "
                     "block p99 %.1f%% of budget, hash %016llx\n",
                     rep, used, 100.0 * used / audioMs, wallMs, used * 1e6 / (double) totalSamples, (double) instructions * 1e-9,
                     (double) instructions / (double) totalSamples, 100.0 * p99 / budget, (unsigned long long) h);
        if (units && rep == 0)
        {
            const auto& u = renderer->unitTimes();
            std::vector<std::pair<double, std::string>> rows;
            for (std::size_t c = 0; c < song.channels.size() && c < (std::size_t) RackRenderer::maxChannels; ++c)
                if (const auto ns = (double) u.channelNs[c].load(); ns > 0.0)
                    rows.push_back ({ ns, "channel " + std::to_string (c) + " " + song.channels[c].name + " [kind "
                                              + std::to_string ((int) song.channels[c].instrument) + (song.channels[c].samplePath.empty() ? std::string() : " " + std::filesystem::path (song.channels[c].samplePath).filename().string())
                                              + ", insert " + std::to_string (song.channels[c].mixerTrack) + "]" });
            for (std::size_t t = 0; t < song.mixer.size(); ++t)
                for (std::size_t k = 0; k < song.mixer[t].effects.size() && k < (std::size_t) maxEffectSlots; ++k)
                    if (const auto ns = (double) u.effectNs[t * maxEffectSlots + k].load(); ns > 0.0)
                        rows.push_back ({ ns, "insert " + std::to_string (t) + " slot " + std::to_string (k + 1) + " "
                                                  + effectInfo (song.mixer[t].effects[k].kind).name });
            std::sort (rows.begin(), rows.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
            double total = 0.0;
            for (const auto& r : rows)
                total += r.first;
            std::printf ("units: %.1f ms in channels and slots (%.1f%% of this rep's CPU)\n", total * 1e-6, 100.0 * total * 1e-6 / used);
            // round 190kr: LYNX_PB_ALLUNITS=1 lists every unit (else the top 20)
            const auto shownRows = std::getenv ("LYNX_PB_ALLUNITS") != nullptr ? rows.size() : (std::size_t) 20;
            for (std::size_t i = 0; i < rows.size() && i < shownRows; ++i)
                std::printf ("  %6.2f%% of a core  %s\n", 100.0 * rows[i].first * 1e-6 / audioMs, rows[i].second.c_str());
        }
        if (raw != nullptr && rep == 0)
        {
            std::ofstream r (raw, std::ios::binary);
            r.write ((const char*) left.data(), (std::streamsize) (left.size() * sizeof (float)));
            r.write ((const char*) right.data(), (std::streamsize) (right.size() * sizeof (float)));
        }
    }
    auto median = [] (auto v) {
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    };
    const auto audioMs = 1000.0 * (double) totalSamples / sampleRate;
    std::printf ("RESULT %s block=%d threads=%d: median cpu %.1f ms = %.2f%% of a core, %.1f ns/sample, %.0f instructions/sample, "
                 "hash %016llx%s\n",
                 scenario.c_str(), blockSize, threads, median (cpu), 100.0 * median (cpu) / audioMs, median (nsPerSample),
                 (double) median (instr) / (double) totalSamples, (unsigned long long) firstHash, sameHash ? "" : " (HASH VARIES BETWEEN REPS)");
    // rep 0's build decodes the samples (a project's load); the later ones build everything new from decoded samples
    // (the reps must not share instruments: their outputs are compared). An edit's snapshot (perfgate190id) is a
    // build from the previous one, as AudioEngine publishes it: unchanged channels and effects are kept.
    const auto firstBuild = buildMs.front();
    const auto rebuild = buildMs.size() > 1 ? median (std::vector<double> (buildMs.begin() + 1, buildMs.end())) : firstBuild;
    // (rollup 0.1.31 step 5, round 190ok's gate proposal: 12 edits, not 5, and the instructions each one retires on this
    // thread, so the gate can hold the edit to its count while one ~5 ms timing moves +-25 % run to run)
    std::vector<double> editMs, editInstr;
    {
        InstructionCounter editCounter;
        auto previous = RackState::build (song, nullptr, sampleRate, provider);
        const auto trace = std::getenv ("PERFBENCH_TRACE_EDIT") != nullptr; // (the phases of the last edit snapshot)
        for (int k = 0; k < 12; ++k)
        {
            if (trace)
                loadtrace::begin ("edit");
            editCounter.start();
            const auto t0 = std::chrono::steady_clock::now();
            auto next = RackState::build (song, previous.get(), sampleRate, provider);
            editMs.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
            editCounter.pause();
            editInstr.push_back ((double) editCounter.read());
            previous = std::move (next);
            if (trace)
                loadtrace::end();
        }
        if (trace)
            std::printf ("%s", loadtrace::table().c_str());
    }
    const auto edit = median (editMs);
    const auto editInstructions = median (editInstr); // (-1: no CPU counter)
    std::printf ("project: import %.1f ms, first build %.1f ms, rebuild %.1f ms, edit snapshot %.1f ms (medians), peak memory %.0f MB; %zu channels, "
                 "%zu inserts, %zu playlist clips, %zu automations\n",
                 importMs, firstBuild, rebuild, edit, peakRssMb(), song.channels.size(), song.mixer.size(), song.playlist.size(), song.automations.size());
    if (gate)
    {
        std::printf ("GATE scenario=%s block=%d threads=%d seconds=%.2f cpu_pct=%.3f ns_per_sample=%.2f instr_per_sample=%.0f import_ms=%.2f "
                     "build_ms=%.2f rebuild_ms=%.2f edit_ms=%.2f rss_mb=%.1f hash=%016llx hash_stable=%d",
                     scenario.c_str(), blockSize, threads, audioMs / 1000.0, 100.0 * median (cpu) / audioMs, median (nsPerSample),
                     median (instr) < 0 ? -1.0 : (double) median (instr) / (double) totalSamples, importMs, firstBuild, rebuild, edit, peakRssMb(),
                     (unsigned long long) firstHash, sameHash ? 1 : 0);
        if (editInstructions > 0)
            std::printf (" edit_instr=%.0f", editInstructions);
        if (paced)
            std::printf (" dropouts_per_min=%.2f overruns_per_min=%.2f", median (droppedPerMin), median (overPerMin));
        std::printf ("\n");
    }
    return 0;
}
