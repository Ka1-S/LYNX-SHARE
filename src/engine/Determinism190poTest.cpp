#include "engine/Determinism190poTest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "core/Denormals.h"
#include "core/Effects.h"
#include "core/WorkerPool.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"

namespace lynx::determinism190po
{
namespace
{
constexpr double rate = 48000.0;
constexpr int inserts = 8;

// 24 synth lines (24 channels: the rack renders in parallel, RackState's rule) on eight inserts, each insert with four
// of FL's exact effects that keep long double states (Fruity Compressor, Fruity Delay 2, Fruity Reeverb 2, Fruity
// Limiter: core/FruityDynamics, FruityDelay, Reeverb2): enough parallel units for the task graph to hand some to the
// workers in every block
constexpr int channels = 24;
Song testSong()
{
    auto s = Song::makeDefault();
    s.channels.clear();
    s.patterns[0].notes.clear();
    for (int t = 1; t <= inserts; ++t)
    {
        auto& track = s.mixer[(std::size_t) t];
        track.effects.clear();
        for (auto kind : { EffectKind::fruityCompressor, EffectKind::fruityDelay2, EffectKind::reeverb2, EffectKind::fruityLimiter })
            track.effects.push_back (makeEffectSlot (kind));
        // the compressor working (its envelope's long double one-pole drives the gain): threshold -30 dB, ratio 4:1,
        // gain +12 dB (FL's raw controls: -600...0, 4...300, -300...300)
        auto& comp = track.effects.front().params;
        comp[0] = 0.5f;
        comp[1] = (40.0f - 4.0f) / 296.0f;
        comp[2] = 420.0f / 600.0f;
    }
    for (int c = 0; c < channels; ++c)
    {
        ChannelDef ch;
        ch.name = "Synth " + std::to_string (c + 1);
        ch.instrument = InstrumentKind::synth;
        ch.volume = 0.5f;
        ch.pan = -0.6f + 0.05f * (float) c;
        ch.mixerTrack = 1 + c % inserts;
        s.channels.push_back (ch);
        for (int k = 0; k < 8; ++k)
        {
            Note n;
            n.channel = c;
            n.start = k * ppq / 2 + (c % 4) * ticksPerStep / 2;
            n.length = ppq / 3;
            n.key = 40 + c + (k * 5) % 12;
            n.velocity = 0.5f + 0.05f * (float) k;
            s.patterns[0].notes.push_back (n);
        }
    }
    return s;
}

struct Audio
{
    std::vector<float> left, right;
    std::uint64_t byWorkers = 0; // the task-graph indices the engine's workers ran
    // the first frame where the two differ in any bit (-1: identical; -2: other lengths)
    long firstDifference (const Audio& o) const
    {
        if (o.left.size() != left.size())
            return -2;
        for (std::size_t i = 0; i < left.size(); ++i)
            if (std::memcmp (&left[i], &o.left[i], sizeof (float)) != 0 || std::memcmp (&right[i], &o.right[i], sizeof (float)) != 0)
                return (long) i;
        return -1;
    }
    double rms() const
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
            sum += (double) left[i] * left[i] + (double) right[i] * right[i];
        return left.empty() ? 0.0 : std::sqrt (sum / (2.0 * (double) left.size()));
    }
};

// a render on a new engine with `workers` extra threads (0: everything on this thread)
Audio renderWith (int workers, double seconds)
{
    Audio a;
    AudioEngine engine (false, workers);
    engine.setSong (testSong());
    AudioEngine::RenderOptions o;
    o.seconds = seconds;
    o.sampleRate = rate;
    o.tailSeconds = 0.0;
    o.capture = [&a] (const float* l, const float* r, int n) {
        a.left.insert (a.left.end(), l, l + n);
        a.right.insert (a.right.end(), r, r + n);
    };
    engine.renderOffline (o);
    if (const auto* pool = engine.getWorkerPool())
        a.byWorkers = pool->indicesRunByWorkers();
    return a;
}

// FNV-1a 64 of the file's bytes
juce::String fileHash (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data))
        return "unreadable";
    std::uint64_t h = 1469598103934665603ull;
    const auto* p = static_cast<const std::uint8_t*> (data.getData());
    for (std::size_t i = 0; i < data.getSize(); ++i)
        h = (h ^ p[i]) * 1099511628211ull;
    return juce::String::toHexString ((juce::int64) h) + "/" + juce::String ((juce::int64) data.getSize());
}

juce::String hex (unsigned v) { return "0x" + juce::String::toHexString ((int) v); }
} // namespace

juce::String check (std::function<void (const juce::String&)> log)
{
    auto say = [&log] (const juce::String& line) {
        if (log)
            log (line);
    };
    juce::StringArray failures;
    auto expect = [&] (bool ok, const juce::String& what) {
        say ((ok ? "ok   " : "FAIL ") + what);
        if (! ok)
            failures.add (what);
    };

    // 1. the threads' x87 mode: a new thread as the system starts it, then inside a render scope, then the render
    // workers (the indices the pool's threads ran; the caller's too)
    const auto mainCw = ScopedExtendedPrecision::controlWord();
    unsigned freshCw = 0, scopedCw = 0;
    std::thread ([&] {
        freshCw = ScopedExtendedPrecision::controlWord();
        const ScopedFlushDenormals scope;
        scopedCw = ScopedExtendedPrecision::controlWord();
    }).join();
    say ("x87 control word: main thread " + hex (mainCw) + ", a new thread " + hex (freshCw) + ", in a render scope " + hex (scopedCw));
    expect ((scopedCw & 0x300u) == 0x300u, "a thread in a render scope runs the x87 at 64-bit precision (" + hex (scopedCw) + ")");
    {
        WorkerPool pool (3);
        std::vector<unsigned> seen (256, 0);
        std::atomic<int> spin { 0 };
        auto fn = [&] (int i) {
            seen[(std::size_t) i] = ScopedExtendedPrecision::controlWord();
            for (int k = 0; k < 20000; ++k) // (long enough for the workers to take indices too)
                spin.fetch_add (1, std::memory_order_relaxed);
        };
        for (int round = 0; round < 8; ++round)
            pool.parallelFor ((int) seen.size(), fn);
        const auto bad = std::count_if (seen.begin(), seen.end(), [] (unsigned cw) { return (cw & 0x300u) != 0x300u; });
        expect (bad == 0, "render workers at 64-bit precision (" + juce::String ((int) bad) + " of " + juce::String ((int) seen.size())
                              + " indices not; " + juce::String ((juce::int64) pool.indicesRunByWorkers()) + " run by the workers)");

        // what the engines do with it: a long double recurrence kept as a double (a one-pole state between blocks),
        // on whichever thread takes the index, against the same on this thread. (A 4-second render rarely shows it:
        // the difference reaches a float output only near a rounding boundary — Exoplanet's first one came at 63 s.)
        auto recurrence = [] (int i) {
            long double y = 0.0L;
            const long double c = 0.999L - (long double) i * 1.0e-6L;
            for (int k = 0; k < 512; ++k)
                y = (long double) (k % 7) / 3.0L + (y - (long double) (k % 7) / 3.0L) * c;
            return (double) y;
        };
        std::vector<double> here (seen.size()), anywhere (seen.size());
        for (std::size_t i = 0; i < here.size(); ++i)
            here[i] = recurrence ((int) i);
        auto onPool = [&] (int i) {
            const ScopedFlushDenormals scope; // (a unit's process call: inside RackRenderer::process or a worker's loop)
            anywhere[(std::size_t) i] = recurrence (i);
            for (int k = 0; k < 20000; ++k)
                spin.fetch_add (1, std::memory_order_relaxed);
        };
        pool.parallelFor ((int) anywhere.size(), onPool);
        int differ = 0;
        for (std::size_t i = 0; i < here.size(); ++i)
            differ += std::memcmp (&here[i], &anywhere[i], sizeof (double)) != 0;
        expect (differ == 0, "long double recurrences on the workers = on this thread (" + juce::String (differ) + " of "
                                 + juce::String ((int) here.size()) + " differ)");
    }

    // 2. the same render on one thread and on seven: the same samples. (Every needed worker woken: with the default
    // policy a chunk wakes only as many as its measured work needs, and this small song's work fits one thread.)
    auto& tuning = WorkerPool::tuning();
    const auto helpBefore = tuning.helpPercent.exchange (0);
    const juce::ScopeGuard helpBack { [&tuning, helpBefore] { tuning.helpPercent.store (helpBefore); } };
    const double seconds = 4.0;
    const auto alone = renderWith (0, seconds);
    expect (alone.rms() > 1.0e-3, "the test song sounds (RMS " + juce::String (alone.rms(), 5) + ")");
    for (int run = 0; run < 2; ++run)
    {
        const auto parallel = renderWith (6, seconds);
        const auto at = parallel.firstDifference (alone);
        expect (at == -1, "render with 6 workers (run " + juce::String (run + 1) + ", " + juce::String ((juce::int64) parallel.byWorkers)
                              + " units' blocks on the workers) = render on one thread"
                              + (at == -1 ? juce::String() : at == -2 ? juce::String (": other length") : ": first difference at frame " + juce::String (at)));
    }

    // 3. export twice (ExportJob, as the Export window and `LYNX --export` make it): the same file bytes
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("lynx-determinism190po-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        dir.createDirectory();
        exportopts::Options o;
        o.format = exportopts::Format::wav;
        o.wavBits = 24;
        o.dither = exportopts::DitherMode::none;
        o.sampleRate = rate;
        o.maxTailSeconds = 2.0;
        juce::StringArray hashes;
        const auto song = testSong();
        for (int run = 0; run < 2; ++run)
        {
            const auto file = dir.getChildFile ("export-" + juce::String (run + 1) + ".wav");
            ExportJob job (song, o, file);
            juce::String error;
            const auto ok = job.run ({}, error);
            hashes.add (ok && file.existsAsFile() ? fileHash (file) : "failed: " + error);
        }
        say ("export hashes " + hashes.joinIntoString (" "));
        expect (! hashes[0].startsWith ("failed") && hashes[0] == hashes[1], "two exports of the song have the same hash");
        dir.deleteRecursively();
    }

    if (failures.isEmpty())
        return "OK (x87 main " + hex (mainCw) + " new thread " + hex (freshCw) + " scoped " + hex (scopedCw) + "; 1 / 6 workers bit-identical; 2 exports one hash)";
    return "ERROR " + failures.joinIntoString ("; ");
}
} // namespace lynx::determinism190po
