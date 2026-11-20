// Issue #138 ("short sample exporting produces a quiet / silent track"): a 2-second note at the very start of the
// Playlist against the same note one bar later, through the Export window's path.
//
// Each case is exported twice by an ExportJob made on the message thread and run on a thread of its own while the
// message loop runs (the Export window; the command line's --export renders on the message thread and never showed
// the difference): once with the note at tick 0, once with it at bar 2. The note must come out the same.

#include "engine/ShortExportTest.h"

#include <cstring>
#include <thread>

#include "engine/AacEncoder.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "engine/Mp3Format.h"
#include "engine/StandInAudioDevice.h"

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

// --- round 190hn: the stress matrix ----------------------------------------------------------------------------------
// Every export setting the Export window has, drawn at random (a seed makes a run repeatable), over short sounds at the
// very start of the song: each case is exported with the sound at tick 0 and again with it one bar later, and the two
// files must hold the same sound. A file that comes out silent, or quieter than its shifted twin, is reported with
// everything needed to make it again.
namespace
{
struct StressCase
{
    int index = 0;
    juce::String what; // the settings, for the report
    Song early, late;  // the sound at tick 0, and a bar later
    exportopts::Options options;
    bool realtime = false;
    bool levelOnly = false;   // lossy formats: the levels are compared with more room
    // normalising, the wrapped tail and loops change the level by design (what else is in the file: the peak the gain
    // follows, the tail laid over the start, the notes of one pass ringing into the next): only silence is checked
    bool silenceOnly = false;
};

struct Decoded
{
    std::vector<float> l, r;
    double rate = 0.0;
    juce::String error;
};

Decoded decodeFile (juce::AudioFormatManager& formats, const juce::File& file)
{
    Decoded d;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        d.error = "can't read " + file.getFileName() + (file.existsAsFile() ? " (" + juce::String (file.getSize()) + " bytes)" : " (no file)");
        return d;
    }
    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
    const auto* l = b.getReadPointer (0);
    const auto* r = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
    d.l.assign (l, l + b.getNumSamples());
    d.r.assign (r, r + b.getNumSamples());
    d.rate = reader->sampleRate;
    return d;
}

struct Level
{
    double energy = 0.0; // sum of squares over both sides
    float peak = 0.0f;
    std::int64_t frames = 0;
};

Level levelOf (const std::vector<Decoded>& files)
{
    Level v;
    for (const auto& d : files)
    {
        v.frames += (std::int64_t) d.l.size();
        for (std::size_t i = 0; i < d.l.size(); ++i)
        {
            v.energy += (double) d.l[i] * d.l[i] + (double) d.r[i] * d.r[i];
            v.peak = juce::jmax (v.peak, std::abs (d.l[i]), std::abs (d.r[i]));
        }
    }
    return v;
}

juce::String dbText (double gain) { return juce::String (juce::Decibels::gainToDecibels (gain, -200.0), 1) + " dB"; }

// a short sound written for the cases: a tone that starts at full level on its first frame and decays, `frames` long
juce::File writeTone (const juce::File& folder, int frames, double rate, bool loopable)
{
    const auto file = folder.getChildFile ("tone-" + juce::String (frames) + "-" + juce::String ((int) rate) + (loopable ? "-loop" : "") + ".wav");
    if (file.existsAsFile())
        return file;
    juce::AudioBuffer<float> b (1, frames);
    for (int i = 0; i < frames; ++i)
    {
        const auto t = i / rate;
        const auto env = loopable ? 1.0 : std::exp (-3.0 * t / juce::jmax (0.005, frames / rate));
        b.setSample (0, i, (float) (0.7 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t + 0.5 * juce::MathConstants<double>::pi) * env));
    }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (file.createOutputStream().release(), rate, 1, 24, {}, 0));
    if (w == nullptr || ! w->writeFromAudioSampleBuffer (b, 0, frames))
        return {};
    return file;
}

struct StressMaker
{
    juce::Random random;
    juce::File folder;

    explicit StressMaker (std::int64_t seed, juce::File f) : random (seed), folder (std::move (f)) {}
    ChannelDef hosted; // a hosted instrument (--plugin), when one was given

    template <typename T>
    T pick (std::initializer_list<T> list)
    {
        return *(list.begin() + random.nextInt ((int) list.size()));
    }
    bool chance (float p) { return random.nextFloat() < p; }

    StressCase make (int index, bool allowRealtime)
    {
        StressCase c;
        c.index = index;
        juce::StringArray what;
        auto& o = c.options;
        o.sampleRate = pick ({ 22050.0, 44100.0, 44100.0, 48000.0, 48000.0, 88200.0, 96000.0, 192000.0 });
        const auto format = random.nextInt (9);
        switch (format)
        {
            case 0: o.format = exportopts::Format::wav; o.wavBits = 16; break;
            case 1: o.format = exportopts::Format::wav; o.wavBits = 24; break;
            case 2: o.format = exportopts::Format::wav; o.wavBits = 32; break;
            case 3: o.format = exportopts::Format::flac; o.flacBits = 16; break;
            case 4: o.format = exportopts::Format::flac; o.flacBits = 24; o.flacLevel = random.nextInt (9); break;
            case 5: o.format = exportopts::Format::mp3; o.mp3Kbps = pick ({ 64, 128, 192, 320 }); break;
            case 6: o.format = exportopts::Format::ogg; o.oggKbps = pick ({ 64, 128, 192, 256, 320 }); break;
            case 7: o.format = exportopts::Format::wav; o.wavBits = 24; o.moreFormats = (1u << (int) exportopts::Format::mp3) | (1u << (int) exportopts::Format::ogg) | (1u << (int) exportopts::Format::flac); break;
            default: o.format = exportopts::Format::ogg; o.oggKbps = 128; o.moreFormats = 1u << (int) exportopts::Format::wav; break;
        }
        if (o.format == exportopts::Format::mp3 && o.sampleRate > 48000.0)
            o.sampleRate = 44100.0; // (the window offers MP3 up to 48 kHz)
        o.dither = pick ({ exportopts::DitherMode::none, exportopts::DitherMode::tpdf, exportopts::DitherMode::shaped });
        o.normalize = chance (0.3f);
        o.tail = pick ({ exportopts::TailMode::leave, exportopts::TailMode::leave, exportopts::TailMode::cut, exportopts::TailMode::wrap });
        o.maxTailSeconds = pick ({ 0.5, 2.0, 12.0 });
        o.loops = pick ({ 1, 1, 1, 2, 3 });
        o.hqAllPlugins = chance (0.5f);
        o.pluginWarmUp = chance (0.5f);
        o.disableMaxPolyphony = chance (0.7f);
        o.masterEffects = chance (0.8f);
        o.insertEffects = chance (0.8f);
        o.trimPdc = chance (0.8f);
        o.channels = pick ({ exportopts::Channels::stereo, exportopts::Channels::stereo, exportopts::Channels::monoMerged, exportopts::Channels::monoLeft });
        o.stems = pick ({ exportopts::Stems::none, exportopts::Stems::none, exportopts::Stems::none, exportopts::Stems::mixerTracks, exportopts::Stems::channels,
                          exportopts::Stems::channelsDry });
        o.stemsIncludeMaster = o.stems != exportopts::Stems::none && chance (0.5f);
        o.saveTempo = chance (0.3f);
        c.realtime = allowRealtime && chance (0.15f);

        // the sound: what is short and sits at the very start
        const auto source = random.nextInt (hosted.instrument == InstrumentKind::plugin ? 7 : 6);
        const auto bpm = pick ({ 90.0, 120.0, 128.0, 140.0, 174.0 });
        const auto noteTicks = (std::int64_t) pick ({ ppq / 4, ppq / 2, ppq, 2 * ppq, 4 * ppq, 6 * ppq }); // a step … 1.5 bars
        const auto frames = pick ({ 256, 2205, 13230, 44100, 88200, 132300 }); // 6 ms … 3 s
        const auto sampleRate = pick ({ 44100.0, 48000.0 });
        ChannelDef ch;
        ch.mixerTrack = 1;
        juce::String sourceText;
        switch (source)
        {
            case 0: // a one-shot
            case 1: // a one-shot from a start offset
                ch.instrument = InstrumentKind::sampler;
                ch.samplePath = writeTone (folder, frames, sampleRate, false).getFullPathName().toStdString();
                if (source == 1)
                    ch.sampler.startOffset = pick ({ 0.1f, 0.25f, 0.5f });
                sourceText = "one-shot " + juce::String (frames) + " frames" + (source == 1 ? " from " + juce::String (ch.sampler.startOffset, 2) : juce::String());
                break;
            case 2: // a looped sample, held for the note
            {
                ch.instrument = InstrumentKind::sampler;
                const auto loopFrames = juce::jmax (2205, frames / 4);
                SampleZone z;
                z.path = writeTone (folder, loopFrames, sampleRate, true).getFullPathName().toStdString();
                z.loopMode = 2;
                z.loopStart = loopFrames / 4;
                z.loopEnd = loopFrames - 1;
                ch.zones = { z };
                ch.sampler.release = 0.05f;
                sourceText = "looped " + juce::String (loopFrames) + " frames";
                break;
            }
            case 3: // an audio clip (Song only)
                ch.instrument = InstrumentKind::audioClip;
                ch.samplePath = writeTone (folder, frames, sampleRate, false).getFullPathName().toStdString();
                sourceText = "audio clip " + juce::String (frames) + " frames";
                break;
            case 4:
                ch.instrument = InstrumentKind::synth;
                sourceText = "synth";
                break;
            case 6:
                ch = hosted;
                ch.mixerTrack = 1;
                sourceText = "hosted " + juce::String (hosted.plugin.name);
                break;
            default:
                ch.instrument = pick ({ InstrumentKind::kick, InstrumentKind::clap, InstrumentKind::hat });
                sourceText = ch.instrument == InstrumentKind::kick ? "kick" : ch.instrument == InstrumentKind::clap ? "clap" : "hat";
                break;
        }
        const auto audioClip = ch.instrument == InstrumentKind::audioClip;
        o.range = c.realtime || audioClip ? pick ({ exportopts::Range::song, exportopts::Range::selection })
                                          : pick ({ exportopts::Range::song, exportopts::Range::song, exportopts::Range::pattern, exportopts::Range::selection });
        // (a start offset inside the sound: 1/8 or 1/4 beat is at most 83 ms at 90 BPM)
        const auto clipOffset = audioClip && frames >= 13230 && chance (0.4f) ? (std::int64_t) pick ({ ppq / 8, ppq / 4 }) : 0;
        if (clipOffset > 0)
            sourceText << " from tick " << juce::String ((int) clipOffset);

        auto base = Song::makeDefault(); // (with its Master limiter, as a new project)
        base.bpm = bpm;
        base.channels = { ch };
        base.patterns.clear();
        base.patterns.emplace_back().name = "Short";
        base.mode = o.range == exportopts::Range::pattern ? PlayMode::pattern : PlayMode::song;
        const auto latencyFx = chance (0.3f);
        if (latencyFx)
            base.mixer[1].effects.push_back (makeEffectSlot (pick ({ EffectKind::limiter, EffectKind::apexLimiter, EffectKind::maximus })));
        if (chance (0.3f))
            base.mixer[1].effects.push_back (makeEffectSlot (pick ({ EffectKind::reverb, EffectKind::delay, EffectKind::eq, EffectKind::compressor })));
        const auto twoNotes = ! audioClip && chance (0.25f);

        auto place = [&] (std::int64_t at) {
            auto s = base;
            const auto length = audioClip ? (std::int64_t) std::ceil ((double) frames / sampleRate * bpm / 60.0 * ppq) - clipOffset : noteTicks;
            const auto end = at + juce::jmax<std::int64_t> (1, length) + (twoNotes ? ppq : 0);
            if (audioClip)
            {
                Clip clip;
                clip.kind = ClipKind::audio;
                clip.channel = 0;
                clip.track = 0;
                clip.start = at;
                clip.length = juce::jmax<std::int64_t> (1, length);
                clip.offset = clipOffset;
                s.playlist.push_back (clip);
            }
            else
            {
                auto& p = s.patterns[0];
                for (int k = 0; k < (twoNotes ? 2 : 1); ++k)
                {
                    Note n;
                    n.channel = 0;
                    n.key = 60 + 4 * k;
                    n.start = at + k * ppq;
                    n.length = noteTicks;
                    n.velocity = 0.8f;
                    p.notes.push_back (n);
                }
                p.sortNotes();
                Clip clip;
                clip.pattern = 0;
                clip.track = 0;
                clip.start = 0;
                clip.length = end;
                s.playlist.push_back (clip);
            }
            s.currentPattern = 0;
            if (o.range == exportopts::Range::selection)
            {
                s.loopStart = 0;
                s.loopEnd = end;
            }
            return s;
        };
        c.early = place (0);
        c.late = place (4 * ppq);

        what.add ("#" + juce::String (index));
        what.add (sourceText + (twoNotes ? " x2" : "") + ", note " + juce::String ((int) noteTicks) + " ticks at " + juce::String (bpm, 0) + " BPM");
        what.add (o.range == exportopts::Range::song ? "song" : o.range == exportopts::Range::pattern ? "pattern" : "selection");
        what.add (juce::String (exportopts::extension (o.format)).substring (1) + (o.format == exportopts::Format::wav ? juce::String (o.wavBits) : o.format == exportopts::Format::flac ? juce::String (o.flacBits) : o.format == exportopts::Format::mp3 ? juce::String (o.mp3Kbps) : juce::String (o.oggKbps))
                  + (o.moreFormats != 0 ? "+more" : ""));
        what.add (juce::String (o.sampleRate / 1000.0, 2) + " kHz");
        what.add (o.tail == exportopts::TailMode::leave ? "tail " + juce::String (o.maxTailSeconds, 1) + " s" : o.tail == exportopts::TailMode::cut ? "tail cut" : "tail wrapped");
        if (o.normalize) what.add ("normalised");
        if (o.loops > 1) what.add (juce::String (o.loops) + " loops");
        if (o.stems != exportopts::Stems::none)
            what.add (juce::String (o.stems == exportopts::Stems::mixerTracks ? "insert stems" : o.stems == exportopts::Stems::channels ? "channel stems" : "dry channel stems")
                      + (o.stemsIncludeMaster ? " + mix" : ""));
        if (o.channels != exportopts::Channels::stereo) what.add (o.channels == exportopts::Channels::monoMerged ? "mono" : "mono left");
        what.add (juce::String ("dither ") + (o.dither == exportopts::DitherMode::none ? "off" : o.dither == exportopts::DitherMode::tpdf ? "tpdf" : "shaped"));
        what.add (juce::String ("HQ ") + (o.hqAllPlugins ? "on" : "off") + ", warm-up " + (o.pluginWarmUp ? "on" : "off"));
        if (! o.masterEffects) what.add ("no Master fx");
        if (! o.insertEffects) what.add ("no insert fx");
        if (! o.trimPdc) what.add ("PDC not trimmed");
        if (latencyFx) what.add ("latency fx");
        if (base.mixer[1].effects.size() > (latencyFx ? 1u : 0u)) what.add (juce::String ("+") + effectInfo (base.mixer[1].effects.back().kind).name);
        if (c.realtime) what.add ("REAL TIME");
        c.levelOnly = o.format == exportopts::Format::mp3 || o.format == exportopts::Format::ogg;
        c.silenceOnly = o.normalize || o.tail == exportopts::TailMode::wrap || o.loops > 1;
        c.what = what.joinIntoString (", ");
        return c;
    }
};

struct StressRunner
{
    int count = 0;
    std::int64_t seed = 0;
    bool allowRealtime = true;
    ChannelDef hosted;
    std::function<void (const juce::String&)> log;
    juce::File folder;
    juce::AudioFormatManager formats;
    std::unique_ptr<AudioEngine> live; // the real-time cases' engine on a stand-in device (made on the message thread)
    int silent = 0, quiet = 0, failed = 0, done = 0, realtimeCases = 0;
    juce::StringArray failures;

    static void onMessageThread (std::function<void()> f)
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread ([] (void* p) -> void* { (*static_cast<std::function<void()>*> (p))(); return nullptr; }, &f);
    }

    // the files one export wrote (the stems', or the mix in every format asked), decoded
    std::vector<Decoded> exportOnce (const StressCase& c, const Song& song, const juce::String& name, juce::String& error)
    {
        std::vector<Decoded> out;
        const auto dir = folder.getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        const auto file = dir.getChildFile ("export" + juce::String (exportopts::extension (c.options.format)));
        std::vector<juce::File> files;
        if (c.realtime)
        {
            std::unique_ptr<ExportJob> job;
            onMessageThread ([&] {
                live->setSong (song);
            });
            juce::Thread::sleep (120); // (the engine's timer publishes the rack)
            onMessageThread ([&] { job = std::make_unique<ExportJob> (*live, c.options, file); });
            if (! job->run ([] (double) { return true; }, error))
                error = "real-time export: " + error;
            onMessageThread ([&] { job.reset(); });
            for (const auto f : exportopts::formatsOf (job == nullptr ? c.options : c.options))
                files.push_back (f == c.options.format ? file : file.withFileExtension (exportopts::extension (f)));
        }
        else if (c.options.stems != exportopts::Stems::none)
        {
            const auto plan = planStemExport (song, c.options, file);
            const auto written = runStemExport (
                plan, [] (std::function<void()> f) { juce::MessageManager::callAsync (std::move (f)); }, [] { return false; }, {}, {}, {});
            if (written <= 0)
                error = "the stems export wrote no file (" + juce::String ((int) plan.items.size()) + " planned)";
            for (const auto& item : plan.items)
                for (const auto f : exportopts::formatsOf (item.options))
                    files.push_back (f == item.options.format ? item.file : item.file.withFileExtension (exportopts::extension (f)));
        }
        else
        {
            std::unique_ptr<ExportJob> job;
            onMessageThread ([&] { job = std::make_unique<ExportJob> (song, c.options, file); });
            if (! job->run ([] (double) { return true; }, error))
                error = error.isEmpty() ? juce::String ("the export failed") : error;
            onMessageThread ([&] { job.reset(); });
            for (const auto f : exportopts::formatsOf (c.options))
                files.push_back (f == c.options.format ? file : file.withFileExtension (exportopts::extension (f)));
        }
        if (error.isNotEmpty())
            return out;
        for (const auto& f : files)
        {
            auto d = decodeFile (formats, f);
            if (d.error.isNotEmpty())
            {
                error = d.error;
                return {};
            }
            out.push_back (std::move (d));
        }
        dir.deleteRecursively();
        return out;
    }

    void runCase (const StressCase& c)
    {
        juce::String errorA, errorB;
        const auto a = exportOnce (c, c.early, "a", errorA);
        const auto b = exportOnce (c, c.late, "b", errorB);
        ++done;
        if (errorA.isNotEmpty() || errorB.isNotEmpty())
        {
            ++failed;
            failures.add (c.what + ": " + (errorA.isNotEmpty() ? errorA : errorB));
            say ("FAILED " + c.what + ": " + (errorA.isNotEmpty() ? errorA : errorB));
            return;
        }
        // per format: the files of each format written (the first format's files hold the comparison; every format must
        // hold sound)
        const auto la = levelOf (a), lb = levelOf (b);
        const auto ratio = lb.energy > 0.0 ? std::sqrt (la.energy / lb.energy) : 0.0;
        juce::String line = c.what + " | tick 0: peak " + dbText (la.peak) + ", " + juce::String ((int) la.frames) + " frames in " + juce::String ((int) a.size())
                            + " file(s) | a bar later: peak " + dbText (lb.peak) + " | level " + dbText (ratio);
        juce::String problem;
        for (std::size_t k = 0; k < a.size(); ++k)
        {
            const auto one = levelOf ({ a[k] });
            const auto twin = k < b.size() ? levelOf ({ b[k] }) : Level {};
            if (one.peak < 1.0e-4f && twin.peak >= 1.0e-3f)
                problem = "SILENT (file " + juce::String ((int) k + 1) + " of " + juce::String ((int) a.size()) + ": peak " + dbText (one.peak) + ", its twin " + dbText (twin.peak) + ")";
        }
        if (problem.isEmpty() && lb.peak < 1.0e-3f)
            problem = "BOTH SILENT (the sound a bar later is silent too)";
        if (problem.isEmpty() && ! c.silenceOnly && juce::Decibels::gainToDecibels (ratio, -200.0) < (c.levelOnly ? -1.5 : -0.25))
            problem = "QUIET (" + dbText (ratio) + " against the sound a bar later)";
        if (problem.isNotEmpty())
        {
            ++(problem.startsWith ("QUIET") ? quiet : silent);
            failures.add (c.what + ": " + problem);
            say ("FAILED " + line + " -> " + problem);
        }
        else
            say (line);
    }

    void say (const juce::String& line)
    {
        if (log)
            log (line);
    }

    juce::String run()
    {
        registerAudioFormats (formats);
        StressMaker maker (seed, folder);
        maker.hosted = hosted;
        if (allowRealtime)
            onMessageThread ([this] {
                live = std::make_unique<AudioEngine> (false);
                if (live->openStandInDevice (48000.0, 256).isNotEmpty())
                    live.reset();
            });
        for (int i = 0; i < count; ++i)
        {
            const auto c = maker.make (i, live != nullptr);
            realtimeCases += c.realtime ? 1 : 0;
            runCase (c);
        }
        onMessageThread ([this] {
            if (live != nullptr)
                live->closeStandInDevice();
            live.reset();
        });
        const auto summary = juce::String (done) + " cases (" + juce::String (realtimeCases) + " in real time, seed " + juce::String (seed) + "): " + juce::String (silent)
                             + " silent, " + juce::String (quiet) + " quiet, " + juce::String (failed) + " failed";
        return failures.isEmpty() ? "OK (" + summary + ")" : "ERROR " + summary + "; " + failures.joinIntoString ("; ");
    }
};
} // namespace

void stress (int count, std::int64_t seed, bool realtime, const juce::String& pluginFile, std::function<void (const juce::String&)> log,
             std::function<void (juce::String)> done)
{
    auto runner = std::make_shared<StressRunner>();
    if (pluginFile.isNotEmpty()) // (its first instrument, as --shortexporttest --plugin)
    {
        AudioEngine scanner (false);
        scanner.getPluginHost().addPluginFile (juce::File (pluginFile));
        for (const auto& t : scanner.getPluginHost().getKnownPlugins().getTypes())
            if (t.isInstrument && juce::File (t.fileOrIdentifier).getFileName() == juce::File (pluginFile).getFileName())
            {
                runner->hosted.instrument = InstrumentKind::plugin;
                runner->hosted.plugin.id = t.createIdentifierString().toStdString();
                runner->hosted.plugin.name = t.name.toStdString();
                break;
            }
    }
    runner->count = juce::jmax (1, count);
    runner->seed = seed;
    runner->allowRealtime = realtime;
    // (the log is called from the stress thread: lines go to the message thread in order)
    runner->log = [log] (const juce::String& line) { juce::MessageManager::callAsync ([log, line] { if (log) log (line); }); };
    runner->folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-exportstress-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    runner->folder.createDirectory();
    std::thread ([runner, done] {
        auto result = runner->run();
        runner->folder.deleteRecursively();
        juce::MessageManager::callAsync ([done, result] { done (result); });
    }).detach();
}

juce::String formatsCheck()
{
    // a sampler playing a decaying tone (the same samples on every render) for one bar, the Master without effects
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-selftest-formats-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    folder.createDirectory();
    struct Cleanup
    {
        juce::File f;
        ~Cleanup() { f.deleteRecursively(); }
    } cleanup { folder };
    auto song = Song::makeDefault();
    song.bpm = 120.0;
    ChannelDef ch;
    ch.instrument = InstrumentKind::sampler;
    ch.mixerTrack = 1;
    ch.samplePath = writeTone (folder, 22050, 44100.0, false).getFullPathName().toStdString();
    song.channels = { ch };
    for (auto& track : song.mixer)
        track.effects.clear();
    song.patterns.clear();
    song.patterns.emplace_back().name = "Formats";
    song = withNote (song, 0);
    song.mode = PlayMode::song;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto exportWith = [&] (exportopts::Options o, const juce::String& name, juce::String& error) {
        o.maxTailSeconds = 0.5;
        o.dither = exportopts::DitherMode::none;
        const auto file = folder.getChildFile (name + exportopts::extension (o.format));
        ExportJob job (song, o, file);
        if (! job.run ([] (double) { return true; }, error) && error.isEmpty())
            error = "the export failed";
        return file;
    };
    auto same = [] (const Decoded& a, const Decoded& b) {
        return a.error.isEmpty() && b.error.isEmpty() && ! a.l.empty() && a.l == b.l && a.r == b.r && a.rate == b.rate;
    };
    auto bitsOf = [&formats] (const juce::File& f) {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        return reader == nullptr ? 0 : (int) reader->bitsPerSample;
    };

    juce::StringArray done;
    juce::String error;
    // 24-bit AIFF from the same render as the 24-bit WAV ("Also save"): the same samples
    exportopts::Options both;
    both.sampleRate = 44100.0;
    both.wavBits = 24;
    both.aiffBits = 24;
    both.moreFormats = 1u << (int) exportopts::Format::aiff;
    const auto wav24 = exportWith (both, "both", error);
    const auto aif24 = wav24.withFileExtension (exportopts::extension (exportopts::Format::aiff));
    if (error.isNotEmpty())
        return "ERROR wav + aiff export: " + error;
    const auto a = decodeFile (formats, wav24), b = decodeFile (formats, aif24);
    if (! same (a, b) || bitsOf (aif24) != 24)
        return "ERROR the 24-bit AIFF differs from the WAV of the same render (" + juce::String ((int) a.l.size()) + " / " + juce::String ((int) b.l.size())
               + " frames, " + juce::String (bitsOf (aif24)) + " bits)";
    done.add ("aiff24 = wav24 (" + juce::String ((int) a.l.size()) + " frames)");

    // a 16-bit AIFF on its own (streamed into the file while it renders) against a 16-bit WAV render
    exportopts::Options wav16, aif16;
    wav16.sampleRate = aif16.sampleRate = 48000.0;
    wav16.wavBits = 16;
    aif16.format = exportopts::Format::aiff;
    aif16.aiffBits = 16;
    const auto c = decodeFile (formats, exportWith (wav16, "w16", error));
    const auto d = decodeFile (formats, exportWith (aif16, "a16", error));
    if (error.isNotEmpty())
        return "ERROR 16-bit export: " + error;
    if (! same (c, d) || bitsOf (folder.getChildFile ("a16.aif")) != 16)
        return "ERROR the streamed 16-bit AIFF differs from the 16-bit WAV render";
    done.add ("aiff16 streamed = wav16");

    // M4A: the system's AAC encoder (a 96 kHz render halved to 48 kHz first), or a clear refusal where there is none
    exportopts::Options m4a;
    m4a.format = exportopts::Format::aac;
    m4a.sampleRate = 96000.0;
    const auto m4aFile = exportWith (m4a, "m", error);
    if (aac::available())
    {
        juce::MemoryBlock bytes;
        m4aFile.loadFileAsData (bytes);
        if (error.isNotEmpty() || bytes.getSize() < 2000 || std::memcmp (static_cast<const char*> (bytes.getData()) + 4, "ftyp", 4) != 0)
            return "ERROR M4A: " + (error.isNotEmpty() ? error : "no MPEG-4 file (" + juce::String ((int) bytes.getSize()) + " bytes)");
        done.add ("m4a " + juce::String ((int) bytes.getSize()) + " bytes");
    }
    else
    {
        if (error.isEmpty() || m4aFile.existsAsFile())
            return "ERROR M4A without an AAC encoder did not refuse";
        done.add ("m4a not available here (refused)");
    }
    return "OK (" + done.joinIntoString ("; ") + ")";
}
} // namespace lynx::shortexport

namespace lynx
{
juce::String AudioEngine::openStandInDevice (double sampleRate, int blockSize)
{
    player.setProcessor (&graph);
    deviceManager.addAudioDeviceType (std::make_unique<standin::Type> (sampleRate, blockSize));
    if (const auto e = deviceManager.initialise (0, 2, nullptr, true); e.isNotEmpty())
        return e;
    deviceManager.addAudioCallback (this);
    return {};
}

void AudioEngine::closeStandInDevice()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}
} // namespace lynx
