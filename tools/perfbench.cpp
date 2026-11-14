// perfbench: the audio engine's CPU cost per block, headless (round perfcore190ge).
//
// Renders a scenario through RackRenderer as the app's audio callback does (live quality, a MeterBank, the live event
// queues) and reports, per repetition, the CPU time and the instructions retired by the rendering, as ns per sample
// and as % of one core at real time, plus a hash of the output samples (the same hash before and after a change =
// a bit-identical null test).
//
// usage: perfbench <scenario> [--block N] [--threads N] [--seconds S] [--reps R] [--offline] [--nometers] [--raw path]
//                              [--stopped]
// scenarios:
//   demo            the showcase song (Demo.lynx), whole song
//   hiphop | pop    the two LYNX demo songs
//   synth100        100 built-in synth channels (presets in turn, chords + an arpeggio line) into 50 inserts with
//                   3 LYNX effects each (EQ / dynamics / space)
//   inserts50       one synth into 50 inserts in a chain of sends, 4 effects each: the mixer and effects alone
//   stream          24 audio-clip channels playing 60 s takes (long audio streaming)
//   idle            the synth100 project with the transport stopped and nothing playing (the cost of an idle project)
//   flp:<path>      an FL Studio project, whole song (FL's own data from FL_ROOT, as renderwav)
// --threads N: N worker threads besides the calling one (the app's WorkerPool); the CPU time is then the process'
// (every thread, spinning included) and the instructions are the calling thread's only.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <time.h>
#if defined(__linux__)
 #include <linux/perf_event.h>
 #include <sys/ioctl.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

#include "WavReader.h"
#include "core/DemoSongs.h"
#include "core/Effects.h"
#include "core/FlpImport.h"
#include "core/LiveEventQueue.h"
#include "core/Meters.h"
#include "core/RackRenderer.h"
#include "core/ShowcaseSong.h"
#include "core/SynthPresets.h"
#include "core/WorkerPool.h"

namespace
{
using namespace lynx;

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

SynthSettings presetAt (std::size_t k)
{
    static const auto presets = synthPresets();
    return presets.empty() ? SynthSettings {} : presets[k % presets.size()].second;
}

Note makeNote (int channel, double beat, double beats, int key, float velocity)
{
    Note n;
    n.channel = channel;
    n.start = (std::int64_t) (beat * ppq);
    n.length = (std::int64_t) (beats * ppq);
    n.key = key;
    n.velocity = velocity;
    return n;
}

// 100 synth channels, two per insert, 50 inserts with 3 effects each; every channel plays 4-bar loops (a held chord
// on even channels, an eighth-note line on odd ones) for the whole song
Song makeSynth100 (int bars)
{
    Song song;
    song.bpm = 128.0;
    song.mixer = Song::defaultMixer (50);
    constexpr int channels = 100;
    const EffectKind chains[][3] = {
        { EffectKind::parametricEq, EffectKind::compressor, EffectKind::reverb },
        { EffectKind::facetEq, EffectKind::crestCompressor, EffectKind::delay },
        { EffectKind::eq, EffectKind::chorus, EffectKind::apexLimiter },
        { EffectKind::filter, EffectKind::ember, EffectKind::atrium },
        { EffectKind::ridgeStrip, EffectKind::echoform, EffectKind::limiter },
    };
    for (int t = 1; t <= 50; ++t)
        for (const auto kind : chains[(std::size_t) (t % 5)])
            song.mixer[(std::size_t) t].effects.push_back (makeEffectSlot (kind));
    const int roots[4] = { 45, 41, 48, 43 };
    for (int c = 0; c < channels; ++c)
    {
        ChannelDef ch;
        ch.name = "Synth " + std::to_string (c + 1);
        ch.instrument = InstrumentKind::synth;
        ch.synth = presetAt ((std::size_t) c);
        ch.volume = 0.3f;
        ch.mixerTrack = 1 + c / 2;
        song.channels.push_back (ch);
        auto& p = song.patterns.emplace_back();
        p.name = ch.name;
        for (int bar = 0; bar < 4; ++bar)
        {
            const auto root = roots[bar] + 12 * (c % 3);
            if (c % 2 == 0)
                for (int k : { 0, 4, 7 })
                    p.notes.push_back (makeNote (c, bar * 4, 3.9, root + k, 0.7f));
            else
                for (int e = 0; e < 8; ++e)
                    p.notes.push_back (makeNote (c, bar * 4 + e * 0.5, 0.4, root + 12 + (e * 5) % 12, 0.8f));
        }
        for (int b = 0; b < bars; b += 4)
        {
            Clip clip;
            clip.pattern = c;
            clip.track = c % 100;
            clip.start = (std::int64_t) b * 4 * ppq;
            clip.length = p.effectiveLength (song.beatsPerBar);
            song.playlist.push_back (clip);
        }
    }
    song.mode = PlayMode::song;
    return song;
}

// one synth into insert 1; inserts 1..50 in a chain of sends (n → n+1), 4 effects each, the last to the Master
Song makeInserts50 (int bars)
{
    Song song;
    song.bpm = 128.0;
    song.mixer = Song::defaultMixer (50);
    const EffectKind kinds[] = { EffectKind::parametricEq, EffectKind::compressor, EffectKind::distortion, EffectKind::chorus,
                                 EffectKind::facetEq, EffectKind::ember, EffectKind::delay, EffectKind::utility };
    for (int t = 1; t <= 50; ++t)
    {
        for (int k = 0; k < 4; ++k)
        {
            auto slot = makeEffectSlot (kinds[(std::size_t) ((t + k) % 8)]);
            slot.mix = 0.5f;
            song.mixer[(std::size_t) t].effects.push_back (slot);
        }
        if (t < 50)
        {
            song.mixer[(std::size_t) t].sends.clear();
            song.mixer[(std::size_t) t].toMaster = false;
            MixerSend s;
            s.target = t + 1;
            song.mixer[(std::size_t) t].sends.push_back (s);
        }
    }
    ChannelDef ch;
    ch.name = "Saw";
    ch.instrument = InstrumentKind::synth;
    ch.synth = presetAt (3);
    ch.mixerTrack = 1;
    song.channels.push_back (ch);
    auto& p = song.patterns.emplace_back();
    for (int e = 0; e < 16; ++e)
        p.notes.push_back (makeNote (0, e * 1.0, 0.9, 48 + (e * 7) % 12, 0.8f));
    for (int b = 0; b < bars; b += 4)
    {
        Clip clip;
        clip.pattern = 0;
        clip.start = (std::int64_t) b * 4 * ppq;
        clip.length = p.effectiveLength (song.beatsPerBar);
        song.playlist.push_back (clip);
    }
    song.mode = PlayMode::song;
    return song;
}

// 24 audio clips of 60 s each, on 12 inserts, playing together
Song makeStream (int seconds, std::map<std::string, std::shared_ptr<const SampleData>>& samples)
{
    Song song;
    song.bpm = 120.0;
    song.mixer = Song::defaultMixer (12);
    constexpr int clips = 24;
    for (int c = 0; c < clips; ++c)
    {
        auto data = std::make_shared<SampleData>();
        const auto frames = (std::size_t) (44100.0 * seconds);
        data->left.resize (frames);
        data->right.resize (frames);
        std::uint32_t seed = 1234567u + (std::uint32_t) c * 7919u;
        double phase = 0.0;
        const auto hz = 55.0 * (1 + c % 7);
        for (std::size_t i = 0; i < frames; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const auto noise = (float) ((int) (seed >> 9) - (1 << 22)) / (float) (1 << 22);
            phase += hz / 44100.0;
            const auto tone = (float) std::sin (6.283185307179586 * phase);
            data->left[i] = 0.2f * tone + 0.02f * noise;
            data->right[i] = 0.2f * tone - 0.02f * noise;
        }
        const auto path = "/stream/take" + std::to_string (c) + ".wav";
        samples[path] = data;
        ChannelDef ch;
        ch.name = "Take " + std::to_string (c + 1);
        ch.instrument = InstrumentKind::audioClip;
        ch.samplePath = path;
        ch.mixerTrack = 1 + c % 12;
        ch.volume = 0.4f;
        song.channels.push_back (ch);
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = c;
        clip.track = c;
        clip.start = 0;
        clip.length = (std::int64_t) (seconds * song.bpm / 60.0 * ppq);
        song.playlist.push_back (clip);
    }
    for (int t = 1; t <= 12; ++t)
        song.mixer[(std::size_t) t].effects.push_back (makeEffectSlot (t % 2 == 0 ? EffectKind::parametricEq : EffectKind::compressor));
    song.mode = PlayMode::song;
    return song;
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
        std::fprintf (stderr, "usage: perfbench <demo|hiphop|pop|synth100|inserts50|stream|idle|flp:path> [--block N] [--threads N] "
                              "[--seconds S] [--reps R] [--offline] [--nometers] [--raw path] [--stopped]\n");
        return 2;
    }
    const std::string scenario = argv[1];
    int blockSize = 256, threads = 0, reps = 5;
    double seconds = 0.0;
    bool offline = false, meters = true, stopped = scenario == "idle";
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
    }

    constexpr int sampleRate = 44100;
    std::map<std::string, std::shared_ptr<const SampleData>> cache;
    Song song;
    if (scenario == "demo")
        song = makeShowcaseSong();
    else if (scenario == "hiphop")
        song = demos::make (1);
    else if (scenario == "pop")
        song = demos::make (2);
    else if (scenario == "synth100" || scenario == "idle")
        song = makeSynth100 (16);
    else if (scenario == "inserts50")
        song = makeInserts50 (16);
    else if (scenario == "stream")
        song = makeStream (60, cache);
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

    std::int64_t songTicks = 0;
    song = makeExportSong (song, true, (std::int64_t) std::ceil (ticksPerSample (song.bpm, sampleRate) * sampleRate), songTicks);
    const auto tps = ticksPerSample (song.bpm, sampleRate);
    auto totalSamples = (std::size_t) ((double) songTicks / tps);
    if (seconds > 0.0)
        totalSamples = std::min (totalSamples, (std::size_t) (seconds * sampleRate));
    if (stopped && seconds > 0.0)
        totalSamples = (std::size_t) (seconds * sampleRate);
    totalSamples = std::max<std::size_t> (totalSamples, (std::size_t) blockSize);

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

    std::vector<double> cpu, nsPerSample;
    std::vector<long long> instr;
    std::uint64_t firstHash = 0;
    bool sameHash = true;
    for (int rep = 0; rep < reps; ++rep)
    {
        auto state = RackState::build (song, nullptr, sampleRate, provider);
        auto renderer = std::make_unique<RackRenderer>();
        if (pool != nullptr)
            renderer->setWorkerPool (pool.get());
        renderer->prepare (sampleRate, std::max (2048, blockSize));
        auto bank = std::make_unique<MeterBank>();
        auto live = std::make_unique<LiveEventQueue>();
        auto midiLive = std::make_unique<LiveEventQueue>();

        std::vector<float> left (totalSamples), right (totalSamples);
        std::vector<double> blockCpu;
        blockCpu.reserve (totalSamples / (std::size_t) blockSize + 1);
        InstructionCounter counter;
        const auto clock = pool != nullptr ? CLOCK_PROCESS_CPUTIME_ID : CLOCK_THREAD_CPUTIME_ID;
        double tick = 0.0, used = 0.0;
        const auto wall0 = std::chrono::steady_clock::now();
        counter.start();
        for (std::size_t pos = 0; pos < totalSamples; pos += (std::size_t) blockSize)
        {
            const auto n = (int) std::min<std::size_t> ((std::size_t) blockSize, totalSamples - pos);
            const auto c0 = cpuMs (clock);
            renderer->process (state.get(), ! stopped, stopped ? 0.0 : tick, n, left.data() + pos, right.data() + pos, live.get(),
                               meters ? bank.get() : nullptr, midiLive.get());
            const auto spent = cpuMs (clock) - c0;
            used += spent;
            blockCpu.push_back (spent);
            tick += n * tps;
            if (meters) // the UI's meter reads (Mixer + Playlist headers), as often as a 60 Hz timer would
                if ((pos / (std::size_t) blockSize) % std::max<std::size_t> (1, (std::size_t) (sampleRate / 60 / blockSize)) == 0)
                {
                    counter.pause();
                    for (int t = 0; t < (int) song.mixer.size(); ++t)
                        for (int s = 0; s < 2; ++s)
                            (void) bank->takePeak (t, s), (void) bank->takeViewPeak (t, s);
                    counter.resume();
                }
        }
        counter.pause();
        const auto wallMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - wall0).count();
        const auto instructions = counter.read();
        const auto audioMs = 1000.0 * (double) totalSamples / sampleRate;
        std::sort (blockCpu.begin(), blockCpu.end());
        const auto p99 = blockCpu[std::min (blockCpu.size() - 1, (std::size_t) (0.99 * (double) blockCpu.size()))];
        const auto budget = 1000.0 * blockSize / sampleRate;
        const auto h = hashSamples (left, right);
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
    return 0;
}
