// Issue #138 ("short sample exporting produces a quiet / silent track"): a 2-second note at the very start of the
// Playlist against the same note one bar later, through the Export window's path.
//
// Each case is exported twice by an ExportJob made on the message thread and run on a thread of its own while the
// message loop runs (the Export window; the command line's --export renders on the message thread and never showed
// the difference): once with the note at tick 0, once with it at bar 2. The note must come out the same.

#include "engine/ShortExportTest.h"

#include <thread>

#include "engine/AudioEngine.h"
#include "engine/Exporter.h"

namespace lynx::shortexport
{
namespace
{
struct Case
{
    juce::String name;
    Song song;                 // one channel, an empty pattern 0
    exportopts::Options options;
    bool exact = true;         // false: the instrument is not the same twice (free LFOs, noise): levels only
};

struct Rendered
{
    std::vector<float> left;
    double rate = 0.0;
    juce::String error;
};

constexpr std::int64_t bar = 4 * ppq;
constexpr std::int64_t noteLength = 4 * ppq; // 2 s at 120 BPM

Song withNote (Song song, std::int64_t at)
{
    auto& pattern = song.patterns[0];
    pattern.notes.clear();
    Note n;
    n.channel = 0;
    n.key = 60;
    n.start = at;
    n.length = noteLength;
    n.velocity = 0.8f;
    pattern.notes.push_back (n);
    song.playlist.clear();
    Clip clip;
    clip.pattern = 0;
    clip.track = 0;
    clip.start = 0;
    clip.length = at + noteLength;
    song.playlist.push_back (clip);
    song.currentPattern = 0;
    return song;
}

std::vector<Case> makeCases (bool everyUnit, const juce::File& sampleFile)
{
    const auto& fl = flStudioAccess();
    std::vector<Case> cases;
    auto base = [] (ChannelDef channel) {
        auto song = Song::makeDefault(); // (with its Master limiter, as a new project)
        song.bpm = 120.0;
        song.channels.clear();
        channel.mixerTrack = 1;
        song.channels.push_back (std::move (channel));
        song.patterns.clear();
        song.patterns.emplace_back().name = "Short";
        song.mode = PlayMode::song;
        return song;
    };
    auto add = [&cases] (juce::String name, Song song, exportopts::Options o, bool exact = true) {
        cases.push_back ({ std::move (name), std::move (song), o, exact });
    };
    exportopts::Options window; // the Export window's defaults
    window.sampleRate = 44100.0;
    window.format = exportopts::Format::wav;
    window.wavBits = 32;
    window.dither = exportopts::DitherMode::none;
    window.maxTailSeconds = 2.0;

    std::vector<std::pair<juce::String, ChannelDef>> channels;
    for (const auto kind : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap, InstrumentKind::synth })
    {
        ChannelDef c;
        c.instrument = kind;
        channels.push_back ({ kind == InstrumentKind::kick ? "Kick" : kind == InstrumentKind::snare ? "Snare" : kind == InstrumentKind::hat ? "Hat"
                              : kind == InstrumentKind::clap ? "Clap" : "Synth", c });
    }
    for (const auto& engine : natives::instrumentEngines())
    {
        if (! everyUnit && engine.kind != InstrumentKind::threeosc && engine.kind != InstrumentKind::plucked && engine.kind != InstrumentKind::dx10)
            continue;
        if (engine.kind == InstrumentKind::plugin || engine.kind == InstrumentKind::patcher || engine.kind == InstrumentKind::videoplayer
            || engine.kind == InstrumentKind::fruitydance)
            continue;
        if (auto c = natives::makeChannel (engine.kind, fl))
            channels.push_back ({ engine.name, std::move (*c) });
    }

    // a sampler playing a percussive one-shot (what a short exported sound usually is): a WAV written next to the exports
    if (sampleFile != juce::File())
    {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = sampleFile.getFullPathName().toStdString();
        channels.push_back ({ "Sampler", c });
    }

    for (const auto& [name, channel] : channels)
    {
        add (name + ", song", base (channel), window);
        if (everyUnit || name == "Synth" || name == "Sampler")
        {
            auto o = window;
            o.range = exportopts::Range::pattern;
            add (name + ", pattern", base (channel), o);
        }
    }
    // the same with what a project adds: effects with latency (delay compensation and its trim), a reverb's tail,
    // the other tail modes, normalising, another rate
    {
        ChannelDef synth;
        synth.instrument = InstrumentKind::synth;
        for (const auto kind : { EffectKind::apexLimiter, EffectKind::facetEq, EffectKind::reverb, EffectKind::pitchShift, EffectKind::maximus,
                                 EffectKind::fruityLimiter, EffectKind::compressor })
            if (auto slot = everyUnit || kind == EffectKind::apexLimiter || kind == EffectKind::reverb ? natives::newEffect (kind, fl) : std::nullopt)
            {
                auto song = base (synth);
                song.mixer[1].effects.push_back (*slot);
                add (juce::String ("Synth + ") + effectInfo (kind).name + " on its insert", std::move (song), window);
                auto master = base (synth);
                master.mixer[0].effects.push_back (*slot);
                add (juce::String ("Synth + ") + effectInfo (kind).name + " on the Master", std::move (master), window);
            }
        for (const auto tail : { exportopts::TailMode::cut, exportopts::TailMode::wrap })
        {
            auto o = window;
            o.tail = tail;
            add (tail == exportopts::TailMode::cut ? "Synth, tail cut" : "Synth, tail wrapped", base (synth), o);
        }
        auto o = window;
        o.normalize = true;
        add ("Synth, normalised", base (synth), o);
        o = window;
        o.sampleRate = 48000.0;
        add ("Synth, 48 kHz", base (synth), o);
        if (everyUnit)
        {
            o = window;
            o.sampleRate = 96000.0;
            add ("Synth, 96 kHz", base (synth), o);
        }
    }
    return cases;
}

struct Runner : std::enable_shared_from_this<Runner>
{
    std::vector<Case> cases;
    juce::StringArray pluginFiles;
    int repeats = 1;
    juce::File folder;
    std::function<void (const juce::String&)> log;
    std::function<void (juce::String)> done;

    std::size_t at = 0;
    int repeat = 0, pass = 0; // pass 0: the note at tick 0; 1: at bar 2
    Rendered rendered[2];
    std::unique_ptr<ExportJob> job;
    std::thread thread;
    std::atomic<bool> finished { false };
    juce::String failures;
    int compared = 0;

    void say (const juce::String& line)
    {
        if (log)
            log (line);
    }

    void next()
    {
        if (at >= cases.size())
        {
            folder.deleteRecursively();
            done (failures.isEmpty() ? "OK (" + juce::String (compared) + " pairs of exports)" : "ERROR " + failures);
            return;
        }
        const auto& c = cases[at];
        const auto file = folder.getChildFile ("short-" + juce::String ((int) at) + "-" + juce::String (pass) + ".wav");
        file.deleteFile();
        // (as the Export window: the job is made here, on the message thread, and runs on its own thread)
        job = std::make_unique<ExportJob> (withNote (c.song, pass == 0 ? 0 : bar), c.options, file);
        finished = false;
        auto self = shared_from_this();
        thread = std::thread ([self, file] {
            auto& out = self->rendered[self->pass];
            out = {};
            juce::String error;
            if (! self->job->run ([] (double) { return true; }, error))
                out.error = error.isEmpty() ? juce::String ("failed") : error;
            else
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
                if (reader == nullptr)
                    out.error = "the file could not be read back";
                else
                {
                    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
                    out.left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
                    out.rate = reader->sampleRate;
                }
            }
            self->finished = true;
        });
        poll();
    }

    void poll()
    {
        auto self = shared_from_this();
        if (! finished)
        {
            juce::Timer::callAfterDelay (5, [self] { self->poll(); });
            return;
        }
        thread.join();
        job.reset(); // (on the message thread, as the Export window's)
        if (pass == 0)
        {
            pass = 1;
            next();
            return;
        }
        compare();
        pass = 0;
        if (++repeat >= repeats)
        {
            repeat = 0;
            ++at;
        }
        // (through the message loop: the engines' pending messages go before the next one is made)
        juce::Timer::callAfterDelay (1, [self] { self->next(); });
    }

    void compare()
    {
        const auto& c = cases[at];
        const auto& a = rendered[0];
        const auto& b = rendered[1];
        auto fail = [&] (const juce::String& what) {
            failures << (failures.isEmpty() ? "" : "; ") << c.name << ": " << what;
            say ("  FAILED: " + what);
        };
        if (a.error.isNotEmpty() || b.error.isNotEmpty())
        {
            fail ("export error: " + (a.error.isNotEmpty() ? a.error : b.error));
            return;
        }
        ++compared;
        const auto rate = a.rate;
        const auto shift = (std::int64_t) std::llround ((double) bar / ppq * 60.0 / 120.0 * rate); // bar 2, in samples
        const auto window = (std::int64_t) (2.0 * rate);                                          // the note
        auto stats = [] (const std::vector<float>& x, std::int64_t from, std::int64_t n) {
            double sum = 0.0, peak = 0.0;
            std::int64_t count = 0;
            for (auto i = from; i < from + n && i < (std::int64_t) x.size(); ++i, ++count)
            {
                sum += (double) x[(std::size_t) i] * x[(std::size_t) i];
                peak = std::max (peak, (double) std::abs (x[(std::size_t) i]));
            }
            return std::pair<double, double> { count > 0 ? std::sqrt (sum / (double) count) : 0.0, peak };
        };
        const auto [rmsA, peakA] = stats (a.left, 0, window);
        const auto [rmsB, peakB] = stats (b.left, shift, window);
        // the first 50 ms on their own: an attack that was lost shows here before it shows in the whole note
        const auto [headA, headPeakA] = stats (a.left, 0, (std::int64_t) (0.05 * rate));
        const auto [headB, headPeakB] = stats (b.left, shift, (std::int64_t) (0.05 * rate));
        double diff = 0.0;
        for (std::int64_t i = 0; i < window && i < (std::int64_t) a.left.size() && shift + i < (std::int64_t) b.left.size(); ++i)
            diff = std::max (diff, (double) std::abs (a.left[(std::size_t) i] - b.left[(std::size_t) (shift + i)]));
        const auto db = [] (double x, double y) { return 20.0 * std::log10 (std::max (1.0e-9, x) / std::max (1.0e-9, y)); };
        juce::String line = c.name + (repeats > 1 ? " #" + juce::String (repeat + 1) : juce::String()) + ": tick 0 RMS " + juce::String (rmsA, 5)
                            + " peak " + juce::String (peakA, 4) + " | bar 2 RMS " + juce::String (rmsB, 5) + " peak " + juce::String (peakB, 4)
                            + " | " + juce::String (db (rmsA, rmsB), 2) + " dB, first 50 ms " + juce::String (db (headA, headB), 2) + " dB, max diff "
                            + juce::String (diff, 6) + " (" + juce::String ((int) a.left.size()) + " / " + juce::String ((int) b.left.size()) + " samples)";
        say (line);
        if (rmsB < 1.0e-5)
        {
            if (rmsA >= 1.0e-5)
                fail ("the note at bar 2 is silent");
            return; // (an instrument that makes no sound here: nothing to compare)
        }
        // normalising and the wrapped tail change the level by design (what else is in the file); they must still sound
        const auto levelOnly = c.options.normalize || c.options.tail == exportopts::TailMode::wrap;
        const auto tolerance = c.exact && ! levelOnly ? 0.25 : 1.5;
        if (std::abs (db (rmsA, rmsB)) > tolerance)
            fail ("the note at tick 0 is " + juce::String (db (rmsA, rmsB), 2) + " dB against the note at bar 2");
        else if (! levelOnly && headB > 1.0e-5 && std::abs (db (headA, headB)) > (c.exact ? 1.0 : 6.0))
            fail ("the first 50 ms of the note at tick 0 are " + juce::String (db (headA, headB), 2) + " dB against the note at bar 2");
    }
};
} // namespace

void run (bool everyUnit, int repeats, const juce::StringArray& pluginFiles, std::function<void (const juce::String&)> log,
          std::function<void (juce::String)> done)
{
    auto runner = std::make_shared<Runner>();
    runner->repeats = juce::jmax (1, repeats);
    runner->log = std::move (log);
    runner->done = std::move (done);
    runner->folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-shortexport-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    runner->folder.createDirectory();
    // the sampler's one-shot: 1.5 s of a decaying 220 Hz tone that starts at full level on its first sample
    auto sampleFile = runner->folder.getChildFile ("one-shot.wav");
    {
        juce::AudioBuffer<float> tone (1, 66150);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample (0, i, 0.8f * (float) (std::cos (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0) * std::exp (-i / 15000.0)));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (sampleFile.createOutputStream().release(), 44100.0, 1, 16, {}, 0));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples()))
            sampleFile = juce::File();
    }
    runner->cases = makeCases (everyUnit, sampleFile);

    // hosted plugins (--shortexporttest --plugin file): each file's first instrument
    if (! pluginFiles.isEmpty())
    {
        AudioEngine scanner (false);
        for (const auto& path : pluginFiles)
        {
            scanner.getPluginHost().addPluginFile (juce::File (path));
            for (const auto& t : scanner.getPluginHost().getKnownPlugins().getTypes())
                if (t.isInstrument && juce::File (t.fileOrIdentifier).getFileName() == juce::File (path).getFileName())
                {
                    ChannelDef c;
                    c.instrument = InstrumentKind::plugin;
                    c.plugin.id = t.createIdentifierString().toStdString();
                    c.plugin.name = t.name.toStdString();
                    c.mixerTrack = 1;
                    auto song = Song::makeDefault();
                    song.bpm = 120.0;
                    song.channels = { c };
                    song.patterns.clear();
                    song.patterns.emplace_back().name = "Short";
                    song.mode = PlayMode::song;
                    exportopts::Options o;
                    o.sampleRate = 44100.0;
                    o.wavBits = 32;
                    o.dither = exportopts::DitherMode::none;
                    o.maxTailSeconds = 2.0;
                    runner->cases.push_back ({ "hosted " + t.name, std::move (song), o, false });
                    break;
                }
        }
    }
    runner->next();
}
} // namespace lynx::shortexport
