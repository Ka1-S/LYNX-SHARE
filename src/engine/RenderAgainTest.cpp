#include "engine/RenderAgainTest.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/QuickRender.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "engine/SessionPack.h"

namespace lynx::renderagain
{
namespace
{
constexpr double rate = 48000.0;

struct Audio
{
    std::vector<float> left, right;

    double rms() const
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
            sum += (double) left[i] * left[i] + (double) right[i] * right[i];
        return left.empty() ? 0.0 : std::sqrt (sum / (2.0 * (double) left.size()));
    }
    bool audible() const { return rms() > 1.0e-3; }
    // the largest difference to `o` (their lengths must match)
    double diff (const Audio& o) const
    {
        if (o.left.size() != left.size())
            return 1.0e9;
        double d = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
            d = std::max (d, (double) std::max (std::abs (left[i] - o.left[i]), std::abs (right[i] - o.right[i])));
        return d;
    }
    juce::String describe() const { return juce::String ((int) left.size()) + " frames, RMS " + juce::String (rms(), 5); }
};

// The default song (Pattern mode). `exact`: without the Clap and the Hat (their noise goes on from one play to the next:
// only a new engine plays them the same) and with a synth line, so that every render of it is the same bit for bit
Song testSong (bool exact)
{
    auto s = Song::makeDefault();
    if (exact)
    {
        auto& notes = s.patterns[0].notes;
        notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const Note& n) { return n.channel == 1 || n.channel == 2; }), notes.end());
        for (int k = 0; k < 4; ++k)
        {
            Note n;
            n.channel = 4;
            n.start = k * ppq;
            n.length = ppq / 2;
            n.key = 60 + 3 * k;
            notes.push_back (n);
        }
    }
    return s;
}

// ... in Song mode: its pattern twice on track 1
Song songModeSong (bool exact = false)
{
    auto s = testSong (exact);
    s.playlist.clear();
    for (int bar = 0; bar < 2; ++bar)
        s.playlist.push_back ({ 0, 0, bar * 4 * ppq, 4 * ppq, 0 });
    s.mode = PlayMode::song;
    return s;
}

Audio renderOn (AudioEngine& engine, double seconds)
{
    Audio a;
    AudioEngine::RenderOptions o;
    o.seconds = seconds;
    o.sampleRate = rate;
    o.tailSeconds = 0.0;
    o.capture = [&a] (const float* l, const float* r, int n) {
        a.left.insert (a.left.end(), l, l + n);
        a.right.insert (a.right.end(), r, r + n);
    };
    engine.renderOffline (o);
    return a;
}

Audio readWav (const juce::File& file)
{
    Audio a;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr)
        return a;
    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
    a.left.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
    const auto* r = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
    a.right.assign (r, r + b.getNumSamples());
    return a;
}

exportopts::Options exportOptions()
{
    exportopts::Options o;
    o.format = exportopts::Format::wav;
    o.wavBits = 32;
    o.dither = exportopts::DitherMode::none;
    o.sampleRate = rate;
    o.maxTailSeconds = 2.0;
    return o;
}

// An export the way the Export window makes it (an ExportJob: its own engine), here on the calling (message) thread
Audio exportSong (const Song& song, const exportopts::Options& o, const juce::File& file, bool extraData = true)
{
    ExportJob job (song, o, file);
    job.setExtraData (extraData);
    juce::String error;
    if (! job.run ({}, error))
        return {};
    return readWav (file);
}

// A stand-in device: the engine's callback is driven by hand
struct TestDevice final : juce::AudioIODevice
{
    TestDevice() : juce::AudioIODevice ("renderagain", "test") {}
    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { rate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 512 }; }
    int getDefaultBufferSize() override { return 512; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 512; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override
    {
        juce::BigInteger b;
        b.setRange (0, 2, true);
        return b;
    }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

// `samples` played from the song's start through the device callback (Play), then stopped
Audio playOn (AudioEngine& engine, int samples)
{
    auto& cb = engine.deviceCallback();
    std::vector<float> l (512), r (512);
    float* outs[] = { l.data(), r.data() };
    juce::AudioIODeviceCallbackContext context;
    engine.stop();
    engine.seek (0.0);
    cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context); // (the seek, stopped)
    engine.play();
    Audio a;
    while ((int) a.left.size() < samples)
    {
        cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
        a.left.insert (a.left.end(), l.begin(), l.end());
        a.right.insert (a.right.end(), r.begin(), r.end());
    }
    engine.stop();
    for (int k = 0; k < 200; ++k) // (the tails die out)
        cb.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, context);
    a.left.resize ((std::size_t) samples);
    a.right.resize ((std::size_t) samples);
    return a;
}
} // namespace

juce::String check (std::function<void (const juce::String&)> log)
{
    juce::StringArray problems;
    auto say = [&log] (const juce::String& s) {
        if (log)
            log (s);
    };
    // `again` must be audible and match `first` (bit for bit: the same song through the same code)
    auto same = [&] (const juce::String& what, const Audio& first, const Audio& again) {
        const auto d = first.diff (again);
        say (what + ": first " + first.describe() + ", again " + again.describe() + ", max diff " + juce::String (d, 9));
        if (! first.audible() || ! again.audible() || d > 0.0)
            problems.add (what + " (first " + first.describe() + ", again " + again.describe() + ", max diff " + juce::String (d, 9) + ")");
    };
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-renderagain190ou-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();

    // 1. the same engine, three renders (the bug: the second one started where the first had stopped: past the song's end
    // a silent file, in a looping pattern another part of it); Pattern mode and Song mode
    for (const auto mode : { PlayMode::pattern, PlayMode::song })
    {
        const auto name = juce::String ("same engine, ") + (mode == PlayMode::song ? "Song" : "Pattern") + " mode";
        AudioEngine engine (false);
        engine.setSong (mode == PlayMode::song ? songModeSong (true) : testSong (true));
        const auto a = renderOn (engine, 3.0);
        const auto b = renderOn (engine, 3.0);
        const auto c = renderOn (engine, 3.0);
        same (name + ", render 2", a, b);
        same (name + ", render 3", a, c);
        // a render past the song's end, then one from its start again (190op's silent second render)
        AudioEngine fresh (false);
        fresh.setSong (engine.getSong());
        const auto first = renderOn (fresh, 3.0);
        renderOn (engine, 30.0);
        same (name + ", after a long render", first, renderOn (engine, 3.0));
    }
    // round 190op's case: a song seven bars long whose notes are all in its first bar, three renders of 2.5 s (they gave
    // peaks 0.233 / 0 / 0: the second and third started past the notes)
    {
        auto s = songModeSong (true);
        s.playlist.resize (1);
        auto late = s.playlist[0];
        late.track = 2;
        late.start = 24 * ppq;
        s.playlist.push_back (late);
        AudioEngine engine (false);
        engine.setSong (s);
        const auto a = renderOn (engine, 2.5);
        same ("same engine, a song longer than the render, render 2", a, renderOn (engine, 2.5));
        same ("same engine, a song longer than the render, render 3", a, renderOn (engine, 2.5));
    }
    // the default song (its Clap and Hat play noise: another noise each time on one engine): the same loudness again
    {
        AudioEngine engine (false);
        const auto a = renderOn (engine, 3.0);
        const auto b = renderOn (engine, 3.0);
        const auto ratio = a.rms() > 0.0 ? b.rms() / a.rms() : 0.0;
        say ("same engine, the default song: first " + a.describe() + ", again " + b.describe());
        if (! a.audible() || std::abs (ratio - 1.0) > 0.02)
            problems.add ("same engine, the default song (first " + a.describe() + ", again " + b.describe() + ")");
    }

    // 2. Export twice in one session (two ExportJobs of the same song)
    const auto song = songModeSong();
    const auto mix = exportSong (song, exportOptions(), dir.getChildFile ("mix1.wav"));
    same ("export twice", mix, exportSong (song, exportOptions(), dir.getChildFile ("mix2.wav")));

    // 3. Export, then play: the live engine plays the same before and after an export, and after an offline render of
    // its own (the live engine's own engine: the render's state must not stay behind)
    {
        AudioEngine live (true, -1, false, true); // (as the app's engine, its device not opened: the callback is driven by hand)
        live.setSong (songModeSong (true));
        TestDevice device;
        live.deviceCallback().audioDeviceAboutToStart (&device);
        const auto before = playOn (live, (int) rate * 2);
        same ("play, then play", before, playOn (live, (int) rate * 2));
        exportSong (live.getSong(), exportOptions(), dir.getChildFile ("mix3.wav"));
        same ("export, then play", before, playOn (live, (int) rate * 2));
        live.seek (4.0 * ppq); // (the playhead elsewhere: a render starts at the song's start and leaves it where it was)
        {
            std::vector<float> l (512), r (512);
            float* outs[] = { l.data(), r.data() };
            live.deviceCallback().audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, 512, {}); // (the seek, stopped)
        }
        const auto at = live.getTransport().positionTicks.load();
        AudioEngine fresh (false);
        fresh.setSong (live.getSong());
        same ("an offline render on the playing engine", renderOn (fresh, 2.0), renderOn (live, 2.0));
        if (std::abs (live.getTransport().positionTicks.load() - at) > 0.5)
            problems.add ("an offline render moved the playhead from " + juce::String (at, 1) + " to " + juce::String (live.getTransport().positionTicks.load(), 1));
        live.deviceCallback().audioDeviceAboutToStart (&device); // (as the device restarts after a render)
        same ("its own render, then play", before, playOn (live, (int) rate * 2));
        live.deviceCallback().audioDeviceStopped();
    }

    // 4. Stems with the mix, then the mixdown: the mixdown is the mix made before
    {
        auto o = exportOptions();
        o.stems = exportopts::Stems::mixerTracks;
        o.stemsIncludeMaster = true;
        const auto plan = planStemExport (song, o, dir.getChildFile ("stems").getChildFile ("song.wav"));
        dir.getChildFile ("stems").createDirectory();
        const auto written = runStemExport (plan, [] (std::function<void()> f) { f(); }, {}, {}, {}, {});
        say ("stems: " + juce::String (written) + " of " + juce::String ((int) plan.items.size()) + " file(s)");
        if (written != (int) plan.items.size() || written < 2)
            problems.add ("stems: " + juce::String (written) + " of " + juce::String ((int) plan.items.size()) + " file(s)");
        // (the default song's Snare and Synth channels have no notes: their inserts' stems are silent)
        auto audible = 0;
        for (const auto& item : plan.items)
            audible += readWav (item.file).audible() ? 1 : 0;
        if (audible < 4)
            problems.add ("stems: " + juce::String (audible) + " audible file(s), the mix and the Kick, Clap and Hat inserts expected");
        same ("stems, then the mixdown", mix, exportSong (song, exportOptions(), dir.getChildFile ("mix4.wav")));
    }

    // 5. Online session: the stand-in renders made twice (sessionpack::render, private engines), the same files
    {
        sessionpack::RenderItem item;
        item.index = 2; // (the Hat channel)
        item.timeline = true;
        item.name = "Hat";
        const std::vector<sessionpack::RenderItem> items { item, sessionpack::RenderItem { true, 1, true, "Kick", {} } };
        std::vector<std::vector<sessionpack::Rendered>> runs (2);
        for (int k = 0; k < 2; ++k)
        {
            const auto folder = dir.getChildFile ("standins" + juce::String (k));
            folder.createDirectory();
            juce::String error;
            if (! sessionpack::render (song, items, rate, folder, "test", runs[(std::size_t) k], {}, error))
                problems.add ("online session renders: " + error);
        }
        auto files = [] (const std::vector<sessionpack::Rendered>& r) {
            std::vector<juce::File> out;
            for (const auto& x : r)
                for (const auto& s : x.standIns)
                    out.push_back (juce::File (juce::String::fromUTF8 (s.path.c_str())));
            return out;
        };
        const auto first = files (runs[0]), again = files (runs[1]);
        if (first.empty() || first.size() != again.size())
            problems.add ("online session renders: " + juce::String ((int) first.size()) + " / " + juce::String ((int) again.size()) + " stand-ins");
        for (std::size_t i = 0; i < first.size() && i < again.size(); ++i)
            same ("online session render " + first[i].getFileName() + " again", readWav (first[i]), readWav (again[i]));
        // the Session's bounce of a time selection (Bounce to New Track / Consolidate), twice
        auto selection = song;
        selection.loopStart = 0;
        selection.loopEnd = 4 * ppq;
        juce::String error;
        const auto f1 = dir.getChildFile ("bounce1.wav"), f2 = dir.getChildFile ("bounce2.wav");
        const auto ok = sessionpack::renderSelection (selection, rate, f1, {}, error) && sessionpack::renderSelection (selection, rate, f2, {}, error);
        if (! ok)
            problems.add ("bounce of a time selection: " + error);
        same ("bounce of a time selection again", readWav (f1), readWav (f2));
    }

    // 6. Render to audio clip (Consolidate this track: the plan's song, no extra chunks), twice
    {
        quickrender::Plan plan;
        if (quickrender::planTrack (song, 0, false, quickrender::Start::songStart, plan) != quickrender::Result::ok)
            problems.add ("render to audio clip: no plan");
        else
        {
            const auto render = quickrender::makePlanRenderSong (song, plan);
            auto o = exportOptions();
            o.range = exportopts::Range::selection;
            const auto a = exportSong (render, o, dir.getChildFile ("clip1.wav"), false);
            same ("render to audio clip again", a, exportSong (render, o, dir.getChildFile ("clip2.wav"), false));
        }
    }

    dir.deleteRecursively();
    return problems.isEmpty() ? juce::String ("OK (6 flows)") : "ERROR " + problems.joinIntoString ("; ");
}
} // namespace lynx::renderagain
