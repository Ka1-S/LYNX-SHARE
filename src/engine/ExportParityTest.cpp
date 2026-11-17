// Round 190gh (B7, community complaint #9: "exports differ: stems out of sync, group FX dropped, render ≠ playback"):
// export parity through the real export path (ExportJob), as --selftest "export_parity" and --exportparitytest.
//
// The project: three sample channels (decaying sines and a click train written here, so every render is the same),
// inserts 1 and 2 sending to a bus (insert 3, with a delay), insert 2 with LYNX's Limiter (72 samples of look-ahead, so
// the delay compensation works; below its ceiling it only delays), insert 4 with an EQ and an automated fader. No
// Master effects: the parts add up to the mix. The checks:
//  1. the export (offline, blocks of 512) against playback: the live engine on a stand-in audio device whose callbacks
//     come in blocks of 441, its Master recorded by the real-time bounce's tap: the same samples, below −90 dBFS;
//  2. "One per mixer insert" and "One per channel" stems as the Export window makes them: every file as long as the
//     mix, the inserts on the Master (and the channels) adding up to the mix (so each starts at 0, the PDC trimmed as
//     in the mix), the bus's stem with its delay (group FX), and an insert fed only by a Fruity Send (an extra output)
//     exported with its sound;
//  3. the PDC trim: on, the mix starts where the project starts; off, the file starts with the 72 samples of latency.
// Round 190hn: the playback check again with the start (startRealtimeBounce's seek and play) landing inside a device
// callback, right after that callback took the position requests (AudioEngine::afterPositionRequests): the start must
// still play from its position in the first block, not one block late (the macOS release self-test of 0.1.29 found the
// real-time export one block late: "best lag 441").

#include "engine/AudioEngine.h"

#include "engine/ChannelRackProcessor.h"
#include "engine/Exporter.h"
#include "engine/PluginHost.h"
#include "engine/SessionPack.h"
#include "ui/ExportDialog.h" // (round 190jx: Render to audio and Freeze, as the menus run them)

#include <cstdlib>

namespace lynx
{
namespace
{
// a stand-in audio device: callbacks of `block` samples from its own thread, about four times faster than real time
struct ParityDevice final : juce::AudioIODevice, private juce::Thread
{
    ParityDevice() : juce::AudioIODevice ("Parity device", "Export parity test"), juce::Thread ("parity device") {}
    ~ParityDevice() override { close(); }

    static constexpr double rate = 48000.0;
    static inline int block = 441; // (the test sets it before opening)
    // (round 190jb) called on the device thread before each callback while it runs: the callbacks so far
    static inline std::function<void (int)> beforeBlock;

    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { rate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { block }; }
    int getDefaultBufferSize() override { return block; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger& outs, double, int) override
    {
        close();
        outsNow = outs;
        outsNow.setRange (2, juce::jmax (0, outsNow.getHighestBit() - 1), false);
        data.setSize (2, block);
        opened = true;
        startThread();
        return {};
    }
    void close() override
    {
        stop();
        stopThread (10000);
        opened = false;
    }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (! opened || cb == nullptr)
            return;
        cb->audioDeviceAboutToStart (this);
        const juce::ScopedLock sl (callbackLock);
        callback = cb;
    }
    void stop() override
    {
        juce::AudioIODeviceCallback* old = nullptr;
        {
            const juce::ScopedLock sl (callbackLock);
            old = std::exchange (callback, nullptr);
        }
        if (old != nullptr)
            old->audioDeviceStopped();
    }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outsNow; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return block; }
    int getInputLatencyInSamples() override { return 0; }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            {
                const juce::ScopedLock sl (callbackLock);
                if (callback != nullptr)
                {
                    if (beforeBlock)
                        beforeBlock (blocks);
                    ++blocks;
                    callback->audioDeviceIOCallbackWithContext (nullptr, 0, data.getArrayOfWritePointers(), data.getNumChannels(), block, {});
                }
            }
            juce::Thread::sleep (2); // (a block is 9.2 ms: ~4× real time, well within the bounce tap's 5 s)
        }
    }

    juce::BigInteger outsNow;
    int blocks = 0;
    juce::AudioBuffer<float> data;
    juce::CriticalSection callbackLock;
    juce::AudioIODeviceCallback* callback = nullptr;
    bool opened = false;
};

struct ParityType final : juce::AudioIODeviceType
{
    ParityType() : juce::AudioIODeviceType ("Export parity test") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override { return input ? juce::StringArray() : juce::StringArray ("Parity device"); }
    int getDefaultDeviceIndex (bool input) const override { return input ? -1 : 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool input) const override { return d != nullptr && ! input ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String&) override
    {
        return out == "Parity device" ? new ParityDevice() : nullptr;
    }
};

bool writeSample (const juce::File& file, int kind)
{
    // kind 3 (round 190jz, issue #194): a steady 440 Hz tone of 2 s for the faded audio clip
    juce::AudioBuffer<float> b (1, kind == 3 ? 96000 : 24000);
    const double f = kind == 0 ? 220.0 : kind == 1 ? 331.0 : kind == 3 ? 440.0 : 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / 48000.0;
        b.setSample (0, i, kind == 3 ? (float) (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * f * t))
                           : f > 0.0 ? (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * f * t) * std::exp (-3.0 * t))
                                     : (i % 2400 < 24 ? 0.25f : 0.0f));
    }
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (file.createOutputStream().release(), 48000.0, 1, 32, {}, 0));
    return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

Song paritySong (const juce::File& folder)
{
    Song s;
    s.mixer = Song::defaultMixer (5);
    s.mixer[0].effects.clear();
    for (int c = 0; c < 3; ++c)
    {
        ChannelDef ch;
        ch.instrument = InstrumentKind::sampler;
        ch.samplePath = folder.getChildFile (juce::String ("parity-") + juce::String (c) + ".wav").getFullPathName().toStdString();
        ch.name = c == 0 ? "Low" : c == 1 ? "High" : "Clicks";
        ch.volume = 0.8f;
        ch.mixerTrack = c == 0 ? 1 : c == 1 ? 2 : 4;
        s.channels.push_back (ch);
    }
    for (int t : { 1, 2 })
    {
        s.mixer[(std::size_t) t].toMaster = false;
        s.mixer[(std::size_t) t].sends.push_back ({ 3, 1.0f });
    }
    s.mixer[2].effects.push_back (makeEffectSlot (EffectKind::limiter)); // (latency, linear below its ceiling)
    s.mixer[3].name = "Bus";
    s.mixer[3].effects.push_back (makeEffectSlot (EffectKind::delay));
    s.mixer[4].effects.push_back (makeEffectSlot (EffectKind::eq));
    auto& p = s.patterns.emplace_back();
    for (int k = 0; k < 8; ++k)
        for (int c = 0; c < 3; ++c)
            p.notes.push_back ({ (std::int64_t) k * ppq / 2 + c * 37, ppq / 3, c, 60 + (k % 3) * 2, 0.7f + 0.03f * (float) k });
    p.sortNotes();
    Automation a;
    a.name = "Insert 4 volume";
    a.target = { AutomationTargetKind::mixerVolume, 4, 0, 0 };
    a.points = { { 0, 0.2f, 0.0f }, { 8 * ppq, 1.0f, 0.0f } };
    s.automations.push_back (a);
    Clip clip;
    clip.length = 8 * ppq;
    s.playlist.push_back (clip);
    // round 190jz (issue #194): an audio clip with both fades, each with its tension (FL's S curve bent, round 180d), on
    // insert 4: the Playlist's fade handles must sound the same in the export and in playback
    {
        ChannelDef audio;
        audio.instrument = InstrumentKind::audioClip;
        audio.samplePath = folder.getChildFile ("parity-3.wav").getFullPathName().toStdString();
        audio.name = "Faded clip";
        audio.mixerTrack = 4;
        s.channels.push_back (audio);
        Clip faded;
        faded.kind = ClipKind::audio;
        faded.channel = (int) s.channels.size() - 1;
        faded.track = 2;
        faded.start = 2 * ppq;
        faded.length = 4 * ppq;
        faded.fadeIn = ppq;
        faded.fadeInTension = 25.0f;
        faded.fadeOut = ppq + ppq / 2;
        faded.fadeOutTension = -40.0f;
        s.playlist.push_back (faded);
    }
    Clip lane;
    lane.kind = ClipKind::automation;
    lane.automation = 0;
    lane.track = 1;
    lane.length = 8 * ppq;
    s.playlist.push_back (lane);
    s.mode = PlayMode::song;
    return s;
}

struct Audio
{
    std::vector<float> l, r;
    juce::String error;
};

Audio readWav (const juce::File& file)
{
    Audio a;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr)
    {
        a.error = "can't read " + file.getFileName();
        return a;
    }
    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
    a.l.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
    a.r.assign (b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0), b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0) + b.getNumSamples());
    return a;
}

float maxDiff (const Audio& a, const Audio& b, std::size_t n)
{
    float d = 0.0f;
    for (std::size_t i = 0; i < n && i < a.l.size() && i < b.l.size(); ++i)
        d = juce::jmax (d, std::abs (a.l[i] - b.l[i]), std::abs (a.r[i] - b.r[i]));
    return d;
}

int firstSound (const Audio& a, float level = 1.0e-4f)
{
    for (std::size_t i = 0; i < a.l.size(); ++i)
        if (std::abs (a.l[i]) > level || std::abs (a.r[i]) > level)
            return (int) i;
    return -1;
}

juce::String db (float v) { return juce::String (juce::Decibels::gainToDecibels (v, -200.0f), 1) + " dBFS"; }

// the UI's start inside a device callback (round 190hn): the audio thread waits here, after its position requests, until
// the test thread has started playback
struct StartRace
{
    juce::WaitableEvent entered, go;
    std::atomic<bool> armed { false };
    static void hook (void* p)
    {
        auto* race = static_cast<StartRace*> (p);
        if (race->armed.exchange (false))
        {
            race->entered.signal();
            race->go.wait (5000);
        }
    }
};

exportopts::Options parityOptions()
{
    exportopts::Options o;
    o.format = exportopts::Format::wav;
    o.wavBits = 32; // float: no dither, no rounding
    o.sampleRate = ParityDevice::rate;
    o.tail = exportopts::TailMode::leave;
    o.hqAllPlugins = false;        // (the playback quality, as the real-time bounce)
    o.disableMaxPolyphony = false; // (as the real-time bounce)
    o.pluginWarmUp = false;
    return o;
}
} // namespace

juce::String AudioEngine::exportParitySelfTest (const std::function<void (const juce::String&)>& log)
{
    auto say = [&log] (const juce::String& line) {
        if (log)
            log (line);
    };
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("lynx-exportparity-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    folder.createDirectory();
    struct Cleanup
    {
        juce::File f;
        ~Cleanup() { f.deleteRecursively(); }
    } cleanup { folder };
    for (int k = 0; k < 4; ++k)
        if (! writeSample (folder.getChildFile ("parity-" + juce::String (k) + ".wav"), k))
            return "ERROR can't write the test samples in " + folder.getFullPathName();
    auto song = paritySong (folder);
    // (diagnosis: LYNX_EXPORTPARITY_CHANNELS="0,2" plays only those channels; LYNX_EXPORTPARITY_NOAUTO=1 drops the automation)
    if (const auto only = juce::SystemStats::getEnvironmentVariable ("LYNX_EXPORTPARITY_CHANNELS", {}); only.isNotEmpty())
        for (int c = 0; c < (int) song.channels.size(); ++c)
            song.channels[(std::size_t) c].muted = ! juce::StringArray::fromTokens (only, ",", {}).contains (juce::String (c));
    if (juce::SystemStats::getEnvironmentVariable ("LYNX_EXPORTPARITY_NOAUTO", {}) == "1")
    {
        song.automations.clear();
        song.playlist.pop_back();
    }
    const auto options = parityOptions();
    juce::StringArray errors, notes;

    auto exportSong = [&] (const Song& s, const exportopts::Options& o, const juce::String& name, std::int64_t exact, std::int64_t* frames) {
        const auto file = folder.getChildFile (name + ".wav");
        ExportJob job (s, o, file);
        job.setExactFrames (exact);
        juce::String error;
        if (! job.run ({}, error))
        {
            errors.add (name + ": " + error);
            return Audio {};
        }
        if (frames != nullptr)
            *frames = job.getWrittenFrames();
        auto a = readWav (file);
        if (a.error.isNotEmpty())
            errors.add (a.error);
        return a;
    };

    // --- 1. the export against playback -----------------------------------------------------------------------------
    std::int64_t mixFrames = 0;
    const auto mix = exportSong (song, options, "mix", 0, &mixFrames);
    if (mix.l.empty())
        return "ERROR the mix: " + errors.joinIntoString ("; ");
    // playback through a stand-in device with callbacks of `block` samples: what the real-time bounce's tap records (as
    // Export › "Render through the hardware in real time"), the message thread doing its part (the engine's timer)
    // `raced` (round 190hn): the start lands inside a callback, after it took the position requests (StartRace)
    // `switchAt` (round 190jb, seconds of the song, < 0 = none): render-ahead is switched on (`engage`) or off there, as
    // its load probe switches it in the middle of a song under load (the rare mid-song miss of 190hn / 190ip)
    auto playback = [&] (int block, juce::String& error, bool raced, double switchAt = -1.0, bool engage = true) {
        Audio played;
        ParityDevice::block = block;
        AudioEngine live (false);
        live.player.setProcessor (&live.graph);
        live.deviceManager.addAudioDeviceType (std::make_unique<ParityType>());
        int playingBlocks = 0;
        const auto switchBlock = switchAt >= 0.0 ? (int) std::lround (switchAt * ParityDevice::rate / block) : -1;
        // (set before the device runs, cleared after it is closed: only the device thread calls it)
        ParityDevice::beforeBlock = nullptr;
        if (switchBlock >= 0)
        {
            if (! engage && live.rackProcessor != nullptr)
                live.rackProcessor->setRenderAheadAlways (true);
            ParityDevice::beforeBlock = [&live, &playingBlocks, switchBlock, engage] (int) {
                if (live.rackProcessor == nullptr || ! live.transport.playing.load())
                    return;
                if (playingBlocks++ == switchBlock)
                    live.rackProcessor->setRenderAheadAlways (engage);
            };
        }
        if (const auto e = live.deviceManager.initialise (0, 2, nullptr, true); e.isNotEmpty())
        {
            error = "open the stand-in device: " + e;
            return played;
        }
        live.deviceManager.addAudioCallback (&live);
        // (diagnosis, round 190hn: LYNX_EXPORTPARITY_RENDERAHEAD=1 makes playback render ahead from its first block)
        if (live.rackProcessor != nullptr && juce::SystemStats::getEnvironmentVariable ("LYNX_EXPORTPARITY_RENDERAHEAD", {}) == "1")
            live.rackProcessor->setRenderAheadAlways (true);
        live.setSong (song);
        for (int i = 0; i < 20; ++i) // (the rack is built and published by the engine's timer)
        {
            live.timerCallback();
            juce::Thread::sleep (10);
        }
        StartRace race;
        if (raced)
        {
            live.afterPositionRequestsContext = &race;
            live.afterPositionRequests.store (&StartRace::hook, std::memory_order_release);
            race.armed = true;
            if (! race.entered.wait (5000))
                notes.add ("the start race's callback did not come");
        }
        const auto started = live.startRealtimeBounce (0.0);
        race.go.signal();
        if (started.isNotEmpty())
        {
            live.afterPositionRequests.store (nullptr);
            live.deviceManager.closeAudioDevice();
            ParityDevice::beforeBlock = nullptr;
            error = "playback (real-time bounce): " + started;
            return played;
        }
        const auto latency = (std::int64_t) live.bounceLatency();
        const auto wanted = latency + (std::int64_t) mix.l.size();
        const auto since = juce::Time::getMillisecondCounter();
        while ((std::int64_t) played.l.size() < wanted && juce::Time::getMillisecondCounter() - since < 60000 && live.bounceStillPlaying())
        {
            live.timerCallback();
            juce::Thread::sleep (20);
            live.pullRealtimeBounce (played.l, played.r);
        }
        const auto dropouts = live.bounceDropouts();
        live.finishRealtimeBounce();
        live.afterPositionRequests.store (nullptr);
        live.deviceManager.removeAudioCallback (&live);
        live.deviceManager.closeAudioDevice();
        ParityDevice::beforeBlock = nullptr;
        if (switchBlock >= 0)
        {
            const auto engagements = live.rackProcessor != nullptr ? (int) live.rackProcessor->renderAheadStats().engagements.load() : -1;
            say ("  render-ahead " + juce::String (engage ? "engaged" : "released") + " at block " + juce::String (switchBlock) + " ("
                 + juce::String (switchAt, 2) + " s): " + juce::String (engagements) + " engagement(s)");
            if (engage && engagements < 1)
                notes.add ("render-ahead did not engage at " + juce::String (switchAt, 2) + " s");
        }
        if (dropouts > 0)
            notes.add (juce::String (dropouts) + " dropout(s) during playback");
        if ((std::int64_t) played.l.size() < wanted)
        {
            error = "playback: " + juce::String ((int) played.l.size()) + " frames recorded of " + juce::String ((int) wanted);
            return played;
        }
        played.l.erase (played.l.begin(), played.l.begin() + latency); // (the delay compensation's start, as the export trims it)
        played.r.erase (played.r.begin(), played.r.begin() + latency);
        played.l.resize (mix.l.size());
        played.r.resize (mix.r.size());
        say ("playback in blocks of " + juce::String (block) + (raced ? ", started inside a callback" : "") + ": " + juce::String ((int) latency) + " samples of delay compensation trimmed");
        // LYNX_EXPORTPARITY_KEEP=<folder>: the export and the playback kept as WAV files (for a closer look)
        if (const auto keep = juce::SystemStats::getEnvironmentVariable ("LYNX_EXPORTPARITY_KEEP", {}); juce::File::isAbsolutePath (keep))
        {
            const juce::File dir (keep);
            dir.createDirectory();
            folder.getChildFile ("mix.wav").copyFileTo (dir.getChildFile ("export.wav"));
            juce::AudioBuffer<float> b (2, (int) played.l.size());
            b.copyFrom (0, 0, played.l.data(), b.getNumSamples());
            b.copyFrom (1, 0, played.r.data(), b.getNumSamples());
            juce::WavAudioFormat wav;
            const auto out = dir.getChildFile ("playback-" + juce::String (block) + (raced ? "-raced" : "") + ".wav");
            out.deleteFile();
            if (std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.createOutputStream().release(), ParityDevice::rate, 2, 32, {}, 0)); w != nullptr)
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        return played;
    };
    // where two renders part: the first 10 ms window above −90 dBFS and the largest difference per pass of the pattern
    auto where = [] (const Audio& a, const Audio& b) {
        const auto n = juce::jmin (a.l.size(), b.l.size());
        juce::String text;
        for (std::size_t w = 0; w < n; w += 480)
        {
            float m = 0.0f;
            for (std::size_t i = w; i < juce::jmin (n, w + 480); ++i)
                m = juce::jmax (m, std::abs (a.l[i] - b.l[i]), std::abs (a.r[i] - b.r[i]));
            if (m >= 3.16e-5f)
                return "first apart at " + juce::String ((double) w / 48000.0, 2) + " s (" + db (m) + ")";
        }
        return juce::String ("never apart");
    };
    // the playback (in blocks of 441) against the export; `how` names the variant in the error
    auto checkPlayback = [&] (bool raced, double switchAt = -1.0, bool engage = true) {
        const juce::String how = switchAt >= 0.0 ? "playback with render-ahead " + juce::String (engage ? "engaging" : "released") + " at "
                                                       + juce::String (switchAt, 2) + " s"
                                 : raced ? "playback started inside a callback"
                                         : "playback";
        juce::String playError;
        const auto played = playback (441, playError, raced, switchAt, engage);
        if (playError.isNotEmpty())
        {
            errors.add (playError);
            return;
        }
        const auto n = juce::jmin (mix.l.size(), played.l.size());
        const auto d = maxDiff (mix, played, n);
        say ("export vs " + how + ": " + juce::String ((int) mix.l.size()) + " / " + juce::String ((int) played.l.size()) + " frames, max difference " + db (d)
             + ", first sound at " + juce::String (firstSound (mix)) + " / " + juce::String (firstSound (played)));
        if (d >= 3.16e-5f)
        {
            // (what kind of difference: an offset, or something else)
            int bestLag = 0;
            float best = d;
            for (int lag = -4096; lag <= 4096; ++lag)
            {
                float m = 0.0f;
                for (std::size_t i = 4096; i + 4096 < n && m < best; i += 7)
                    m = juce::jmax (m, std::abs (mix.l[i] - played.l[(std::size_t) ((int) i + lag)]));
                if (m < best)
                    best = m, bestLag = lag;
            }
            std::size_t worst = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (std::abs (mix.l[i] - played.l[i]) > std::abs (mix.l[worst] - played.l[worst]))
                    worst = i;
            say ("  best lag " + juce::String (bestLag) + " (" + db (best) + "), the largest difference at frame " + juce::String ((int) worst));
            juce::String e512;
            const auto same = playback (512, e512, raced, switchAt, engage);
            errors.add (how + " differs from the export by " + db (d) + " (best lag " + juce::String (bestLag) + ": " + db (best) + ", worst at frame "
                        + juce::String ((int) worst) + ", " + where (mix, played) + ", first sound at " + juce::String (firstSound (mix)) + " / "
                        + juce::String (firstSound (played)) + "; in blocks of 512: " + (e512.isNotEmpty() ? e512 : db (maxDiff (mix, same, n)) + ", " + where (mix, same))
                        + ")");
        }
    };
    checkPlayback (false);
    checkPlayback (true);
    // Round 190jb: render-ahead switched on, and off, in the middle of the song, as its load probe does under load (the rare
    // mid-song miss: 1 in ~100 runs under load, "first apart at 3.56 s"). LYNX_EXPORTPARITY_ENGAGE_AT / _RELEASE_AT =
    // seconds (the stress script draws them) replace the self-test's fixed points.
    {
        auto at = [] (const char* name, double fallback) {
            const auto v = juce::SystemStats::getEnvironmentVariable (name, {});
            return v.isNotEmpty() ? v.getDoubleValue() : fallback;
        };
        if (const auto engageAt = at ("LYNX_EXPORTPARITY_ENGAGE_AT", 3.5); engageAt >= 0.0)
            checkPlayback (false, engageAt, true);
        if (const auto releaseAt = at ("LYNX_EXPORTPARITY_RELEASE_AT", 2.0); releaseAt >= 0.0)
            checkPlayback (false, releaseAt, false);
    }

    // --- 2. stems ---------------------------------------------------------------------------------------------------
    auto checkSum = [&] (const juce::String& what, const std::vector<Audio>& parts) {
        Audio sum;
        sum.l.assign (mix.l.size(), 0.0f);
        sum.r.assign (mix.r.size(), 0.0f);
        for (const auto& p : parts)
        {
            if (p.l.size() != mix.l.size())
                errors.add (what + ": a stem of " + juce::String ((int) p.l.size()) + " frames, the mix has " + juce::String ((int) mix.l.size()));
            for (std::size_t i = 0; i < juce::jmin (p.l.size(), sum.l.size()); ++i)
                sum.l[i] += p.l[i], sum.r[i] += p.r[i];
        }
        const auto d = maxDiff (mix, sum, mix.l.size());
        say (what + ": " + juce::String ((int) parts.size()) + " stems, their sum against the mix " + db (d));
        if (d >= 3.16e-5f)
            errors.add (what + " don't add up to the mix (" + db (d) + ")");
    };
    {
        auto o = options;
        o.stems = exportopts::Stems::mixerTracks;
        const auto tracks = exportopts::stemTracks (song);
        if (tracks != std::vector<int> { 1, 2, 3, 4 })
            errors.add ("one per mixer insert: " + juce::String ((int) tracks.size()) + " inserts, expected 4");
        std::vector<Audio> onMaster; // (inserts 1 and 2 go to the bus: the bus and insert 4 make the mix)
        for (auto t : tracks)
        {
            auto stem = exportSong (makeBounceSong (song, t), o, "insert-" + juce::String (t), mixFrames, nullptr);
            if (stem.l.size() != mix.l.size())
                errors.add ("insert " + juce::String (t) + ": " + juce::String ((int) stem.l.size()) + " frames, the mix has " + juce::String ((int) mix.l.size()));
            if (song.mixer[(std::size_t) t].toMaster)
                onMaster.push_back (std::move (stem));
        }
        checkSum ("one per mixer insert (the inserts on the Master)", onMaster);
        // group FX: the bus's stem with its delay against the same without it
        auto dry = makeBounceSong (song, 3);
        dry.mixer[3].effects.clear();
        const auto wet = exportSong (makeBounceSong (song, 3), o, "bus", mixFrames, nullptr);
        const auto plain = exportSong (dry, o, "bus-dry", mixFrames, nullptr);
        if (maxDiff (wet, plain, wet.l.size()) < 1.0e-3f)
            errors.add ("the bus's stem lacks its delay (group FX)");
        // an insert fed only by an extra output (a Fruity Send on insert 4 to insert 5): it gets a stem, with sound
        auto sent = song;
        auto send = makeEffectSlot (EffectKind::fruitySend);
        send.params[3] = 5.0f / (float) (maxMixerTracks - 1);
        sent.mixer[4].effects.push_back (send);
        sent.mixer[5].name = "Send target";
        if (const auto withSend = exportopts::stemTracks (sent); withSend != std::vector<int> { 1, 2, 3, 4, 5 })
            errors.add ("one per mixer insert: insert 5, fed only by a send, has no stem");
        else if (firstSound (exportSong (makeBounceSong (sent, 5), o, "insert-5", mixFrames, nullptr)) < 0)
            errors.add ("insert 5 (fed only by a send) is silent");
    }
    // round 190gj (owner decision 2026-11-14, FL's split render): an insert whose compressor listens to another insert
    // keeps its ducking in its stem (the key plays unheard in the stem's render), so the stems still add up to the mix
    {
        auto o = options;
        o.stems = exportopts::Stems::mixerTracks;
        auto keyed = song;
        auto comp = makeEffectSlot (EffectKind::compressor);
        comp.params[0] = 0.1f; // (a low threshold and a high ratio: the key's hits are heard on insert 4)
        comp.params[1] = 0.8f;
        comp.sidechain = 1;
        keyed.mixer[4].effects.push_back (comp);
        std::int64_t keyedFrames = 0;
        const auto keyedMix = exportSong (keyed, options, "keyed-mix", 0, &keyedFrames);
        std::vector<Audio> parts;
        for (auto t : { 3, 4 }) // (the inserts on the Master)
            parts.push_back (exportSong (makeBounceSong (keyed, t), o, "keyed-insert-" + juce::String (t), keyedFrames, nullptr));
        Audio sum;
        sum.l.assign (keyedMix.l.size(), 0.0f);
        sum.r.assign (keyedMix.r.size(), 0.0f);
        for (const auto& p : parts)
            for (std::size_t i = 0; i < juce::jmin (p.l.size(), sum.l.size()); ++i)
                sum.l[i] += p.l[i], sum.r[i] += p.r[i];
        const auto d = maxDiff (keyedMix, sum, keyedMix.l.size());
        say ("side-chained insert: the stems' sum against the mix " + db (d));
        if (keyedMix.l.empty() || d >= 3.16e-5f)
            errors.add ("with a side-chained insert the stems don't add up to the mix (" + db (d) + ")");
        auto unkeyed = makeBounceSong (keyed, 4);
        unkeyed.mixer[4].effects.back().sidechain = -1;
        const auto plain = exportSong (unkeyed, o, "unkeyed-insert-4", keyedFrames, nullptr);
        if (parts.size() == 2 && maxDiff (parts[1], plain, plain.l.size()) < 1.0e-3f)
            errors.add ("the side-chained insert's stem is not ducked (its key is silent in its render)");
    }
    {
        auto o = options;
        o.stems = exportopts::Stems::channels;
        std::vector<Audio> parts;
        for (auto c : exportopts::stemChannels (song, o.range))
            parts.push_back (exportSong (exportopts::makeChannelStemSong (song, c), o, "channel-" + juce::String (c), mixFrames, nullptr));
        checkSum ("one per channel", parts);
    }

    // --- 3. the PDC trim ----------------------------------------------------------------------------------------------
    {
        auto o = options;
        o.trimPdc = false;
        const auto untrimmed = exportSong (song, o, "untrimmed", 0, nullptr);
        const auto on = firstSound (mix), off = firstSound (untrimmed);
        say ("PDC trim: the first sound at " + juce::String (on) + " (on) and " + juce::String (off) + " (off)");
        if (on > 2)
            errors.add ("with the PDC trim the mix starts at " + juce::String (on));
        if (off - on < 60)
            errors.add ("without the PDC trim the latency is not in the file (" + juce::String (off - on) + " samples)");
    }

    if (! errors.isEmpty())
        return "ERROR " + errors.joinIntoString ("; ");
    return "OK (export = playback, also started inside a callback and with render-ahead switched mid-song, stems of 2 modes add up to the mix, side-chained stems ducked" + (notes.isEmpty() ? juce::String() : ", " + notes.joinIntoString (", ")) + ")";
}

// --- Round 190jb: exports with the live plugin instances ---------------------------------------------------------------
// The parity project plus a hosted instrument (each of `pluginFiles`' first instrument) on insert 5, played on the
// stand-in device by one live engine that stays open (its plugins keep running, as in the app):
//  1. an export without a hold: new instances, as before (its length is the playback's);
//  2. playback, recorded by the real-time bounce's tap;
//  3. an export with the Export window's hold (LiveExportHold): it must render with the live instance and equal the
//     playback, below -90 dBFS or within what two playbacks of the plugin differ by (a plugin may not repeat itself);
//  4. the hold given back: the engine plays again, the plugin at the device's rate in real-time mode;
//  5. playback again: the same as the first (the export changed nothing that is heard afterwards);
//  6. LYNX's own units with a plugin lent: insert 4's stem (sampler, EQ, automated fader) bit-identical with and without;
//  7. an export at 44.1 kHz with the hold: the plugin prepared at that rate and back at 48 kHz afterwards;
//  8. (round 190jx) Render to audio of the plugin's insert with the live instance (the Mixer menu's hold): playback
//     with the rendered audio in the insert's place = the first playback; with a new instance instead, it is not (the
//     seeded test synth); the render undone, playback = the first (the plugin given back as it was);
//  9. (round 190jx) Freeze of the plugin's channel with the live instance: playback of the frozen channel = the first
//     playback, and its audio differs from a freeze made with a new instance (the seeded test synth).
// With the LYNX Test Synth in its seeded mode (LYNX_TEST_SYNTH_SEED=1: a pitch per instance) the export without the
// hold must differ from the playback: proof that the instance, not just the plugin, is what renders.
juce::String AudioEngine::exportLiveSelfTest (const std::function<void (const juce::String&)>& log, const juce::StringArray& pluginFiles)
{
    auto say = [&log] (const juce::String& line) {
        if (log)
            log (line);
    };
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("lynx-exportlive-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    folder.createDirectory();
    struct Cleanup
    {
        juce::File f;
        ~Cleanup() { f.deleteRecursively(); }
    } cleanup { folder };
    for (int k = 0; k < 4; ++k) // (parity-3: round 190jz's faded audio clip)
        if (! writeSample (folder.getChildFile ("parity-" + juce::String (k) + ".wav"), k))
            return "ERROR can't write the test samples in " + folder.getFullPathName();

    juce::StringArray errors, notes, checked;
    // the hosted instruments: each file's first instrument
    std::vector<std::pair<juce::String, ChannelDef>> hosted;
    {
        AudioEngine scanner (false);
        for (const auto& path : pluginFiles)
        {
            const juce::File file (path);
            if (! file.exists())
            {
                notes.add ("not installed: " + file.getFileName());
                continue;
            }
            scanner.getPluginHost().addPluginFile (file);
            auto found = false;
            for (const auto& t : scanner.getPluginHost().getKnownPlugins().getTypes())
                if (t.isInstrument && juce::File (t.fileOrIdentifier).getFileName() == file.getFileName())
                {
                    ChannelDef c;
                    c.instrument = InstrumentKind::plugin;
                    c.plugin.id = t.createIdentifierString().toStdString();
                    c.plugin.name = t.name.toStdString();
                    hosted.push_back ({ t.name + " (" + t.pluginFormatName + ")", c });
                    found = true;
                    break;
                }
            if (! found)
                errors.add ("no instrument in " + file.getFileName());
        }
    }
    if (hosted.empty())
        return errors.isEmpty() ? "OK (no hosted instrument to check" + (notes.isEmpty() ? juce::String() : ": " + notes.joinIntoString (", ")) + ")"
                                : "ERROR " + errors.joinIntoString ("; ");

    auto options = parityOptions();
    options.pluginWarmUp = true; // (the Export window's default: new instances wait for their plugins, lent ones only settle)
    auto exportSong = [&] (const Song& s, const exportopts::Options& o, const juce::String& name, int* lent) {
        const auto file = folder.getChildFile (name + ".wav");
        Audio a;
        {
            ExportJob job (s, o, file); // (made on this thread, the message thread: it may take the hold)
            if (lent != nullptr)
                *lent = job.livePlugins();
            juce::String error;
            if (! job.run ({}, error))
            {
                errors.add (name + ": " + error);
                return a;
            }
        }
        a = readWav (file);
        if (a.error.isNotEmpty())
            errors.add (a.error);
        return a;
    };
    auto peakOf = [] (const Audio& a) {
        float p = 0.0f;
        for (std::size_t i = 0; i < a.l.size(); ++i)
            p = juce::jmax (p, std::abs (a.l[i]), std::abs (a.r[i]));
        return p;
    };

    for (const auto& [name, plugin] : hosted)
    {
        auto song = paritySong (folder);
        auto c = plugin;
        c.name = "Hosted";
        c.volume = 0.8f;
        c.mixerTrack = 5;
        // (its index: after paritySong's channels, which round 190jz's faded audio clip made four)
        const auto hostedChannel = (int) song.channels.size();
        song.channels.push_back (c);
        song.mixer[5].name = "Hosted";
        for (int k = 0; k < 8; ++k)
            song.patterns[0].notes.push_back ({ (std::int64_t) k * ppq / 2 + 11, ppq / 3, hostedChannel, 57 + (k % 4) * 3, 0.8f });
        song.patterns[0].sortNotes();
        const auto errorsBefore = errors.size();

        // 1. new instances (the old way); its length is what playback records
        const auto freshStart = juce::Time::getMillisecondCounterHiRes();
        const auto fresh = exportSong (song, options, "fresh", nullptr);
        const auto freshMs = juce::Time::getMillisecondCounterHiRes() - freshStart;
        if (fresh.l.empty())
            continue;

        ParityDevice::block = 441;
        ParityDevice::beforeBlock = nullptr;
        AudioEngine live (false);
        live.player.setProcessor (&live.graph);
        live.deviceManager.addAudioDeviceType (std::make_unique<ParityType>());
        if (const auto e = live.deviceManager.initialise (0, 2, nullptr, true); e.isNotEmpty())
            return "ERROR open the stand-in device: " + e;
        live.deviceManager.addAudioCallback (&live);
        live.setRecordingTarget (folder.getChildFile ("Audio").getChildFile ("Recorded"), "exportlive"); // (frozen audio: in the test's folder)
        live.setSong (song);
        for (int i = 0; i < 20; ++i)
        {
            live.timerCallback();
            juce::Thread::sleep (10);
        }
        juce::StringArray aheadCounts; // render-ahead's engagements so far, after each playback
        auto playOnce = [&] (const juce::String& what) {
          for (int attempt = 0;; ++attempt)
          {
            Audio played;
            const auto started = live.startRealtimeBounce (0.0);
            if (started.isNotEmpty())
            {
                errors.add (name + ", " + what + ": " + started);
                return played;
            }
            const auto latency = (std::int64_t) live.bounceLatency();
            const auto wanted = latency + (std::int64_t) fresh.l.size();
            const auto since = juce::Time::getMillisecondCounter();
            while ((std::int64_t) played.l.size() < wanted && juce::Time::getMillisecondCounter() - since < 60000 && live.bounceStillPlaying())
            {
                live.timerCallback();
                juce::Thread::sleep (20);
                live.pullRealtimeBounce (played.l, played.r);
            }
            const auto dropouts = live.bounceDropouts();
            live.finishRealtimeBounce();
            if (live.rackProcessor != nullptr) // (render-ahead may engage in the middle: its load probe)
                aheadCounts.add (what + " " + juce::String ((int) live.rackProcessor->renderAheadStats().engagements.load()));
            for (int i = 0; i < 10; ++i) // (the engine's timer after a stop)
            {
                live.timerCallback();
                juce::Thread::sleep (5);
            }
            if (dropouts > 0 && attempt < 3) // (the stand-in device's thread starved by the machine's load: again)
            {
                notes.add (name + ", " + what + ": " + juce::String (dropouts) + " dropout(s), played again");
                continue;
            }
            if ((std::int64_t) played.l.size() < wanted)
            {
                errors.add (name + ", " + what + ": " + juce::String ((int) played.l.size()) + " frames recorded of " + juce::String ((int) wanted));
                return Audio {};
            }
            played.l.erase (played.l.begin(), played.l.begin() + latency);
            played.r.erase (played.r.begin(), played.r.begin() + latency);
            played.l.resize (fresh.l.size());
            played.r.resize (fresh.r.size());
            return played;
          }
        };
        // where two renders part (the first 10 ms window above -90 dBFS), and how far apart their levels are: the
        // largest difference of 10 ms RMS windows (a plugin that does not repeat its waveform, Surge XT's oscillators'
        // phases, still repeats its level)
        auto apart = [] (const Audio& a, const Audio& b) {
            const auto n = juce::jmin (a.l.size(), b.l.size());
            for (std::size_t w = 0; w < n; w += 480)
            {
                float m = 0.0f;
                for (std::size_t i = w; i < juce::jmin (n, w + 480); ++i)
                    m = juce::jmax (m, std::abs (a.l[i] - b.l[i]), std::abs (a.r[i] - b.r[i]));
                if (m >= 3.16e-5f)
                    return "apart from " + juce::String ((double) w / ParityDevice::rate, 2) + " s";
            }
            return juce::String ("never apart");
        };
        auto levels = [] (const Audio& a, const Audio& b) {
            const auto n = juce::jmin (a.l.size(), b.l.size());
            float worst = 0.0f;
            for (std::size_t w = 0; w < n; w += 480)
            {
                double ea = 0.0, eb = 0.0;
                const auto end = juce::jmin (n, w + 480);
                for (std::size_t i = w; i < end; ++i)
                {
                    ea += (double) a.l[i] * a.l[i] + (double) a.r[i] * a.r[i];
                    eb += (double) b.l[i] * b.l[i] + (double) b.r[i] * b.r[i];
                }
                const auto count = (double) (2 * (end - w));
                worst = juce::jmax (worst, (float) std::abs (std::sqrt (ea / count) - std::sqrt (eb / count)));
            }
            return worst;
        };
        auto hostedNow = [&]() -> juce::AudioPluginInstance* {
            auto* p = dynamic_cast<PluginInstrument*> (live.getChannelInstrument (hostedChannel).get());
            return p != nullptr ? &p->getInstance() : nullptr;
        };

        // 2. playback
        const auto first = playOnce ("first playback");
        if (hostedNow() == nullptr)
            errors.add (name + ": the plugin did not load in the live engine");

        // 3. the export with the hold (the Export window's)
        int lent = 0;
        Audio held;
        double heldMs = 0.0;
        {
            auto hold = LiveExportHold::open (live);
            if (hold == nullptr)
                errors.add (name + ": the live engine could not be held");
            else if (! live.heldForExport())
                errors.add (name + ": the hold is open but the engine is not held");
            const auto heldStart = juce::Time::getMillisecondCounterHiRes();
            held = exportSong (song, options, "held", &lent);
            heldMs = juce::Time::getMillisecondCounterHiRes() - heldStart;
            if (hold != nullptr && lent < 1)
                errors.add (name + ": the export did not take the live plugin (" + hold->lastRefusal() + ")");
        } // (4. the hold given back)
        if (live.heldForExport())
            errors.add (name + ": the engine is still held after the export");
        if (auto* p = hostedNow(); p != nullptr && (p->getSampleRate() != ParityDevice::rate || p->isNonRealtime()))
            errors.add (name + ": after the export the plugin runs at " + juce::String (p->getSampleRate()) + " Hz"
                        + (p->isNonRealtime() ? ", in offline mode" : ""));

        // 5. playback again
        const auto second = playOnce ("second playback");

        // 6. LYNX's own units with the plugin lent: insert 4's stem
        auto stemOptions = options;
        stemOptions.stems = exportopts::Stems::mixerTracks;
        const auto stemFresh = exportSong (makeBounceSong (song, 4), stemOptions, "stem-fresh", nullptr);
        Audio stemHeld;
        int stemLent = 0;
        {
            auto hold = LiveExportHold::open (live);
            stemHeld = exportSong (makeBounceSong (song, 4), stemOptions, "stem-held", &stemLent);
        }

        // 7. another sample rate
        auto at441 = options;
        at441.sampleRate = 44100.0;
        Audio other;
        int otherLent = 0;
        double rateDuring = 0.0;
        {
            auto hold = LiveExportHold::open (live);
            other = exportSong (song, at441, "at44100", &otherLent);
            if (auto* p = hostedNow())
                rateDuring = p->getSampleRate();
        }
        const auto rateAfter = hostedNow() != nullptr ? hostedNow()->getSampleRate() : 0.0;

        // 8. Render to audio of insert 5 (round 190jx): first with new instances (the old way), then as the menu does it
        const auto renders = folder.getChildFile ("Renders");
        auto settle = [&live] {
            for (int i = 0; i < 20; ++i)
            {
                live.timerCallback();
                live.freeRetiredRacks();
                juce::Thread::sleep (5);
            }
        };
        juce::String renderLine;
        float renderFreshVsPlay = -1.0f, renderVsPlay = -1.0f, undoneVsPlay = -1.0f, frozenVsPlay = -1.0f, freezeStems = -1.0f;
        {
            if (const auto e = bounceInsertToAudioNow (live, 5, false, renders); e.isNotEmpty())
                errors.add (name + ", Render to audio with a new instance: " + e);
            settle();
            const auto played = playOnce ("playback after Render to audio (new instance)");
            if (! played.l.empty() && ! first.l.empty())
                renderFreshVsPlay = maxDiff (played, first, fresh.l.size());
            live.undo();
            settle();
        }
        {
            if (const auto e = bounceInsertToAudioNow (live, 5, true, renders); e.isNotEmpty())
                errors.add (name + ", Render to audio: " + e);
            if (live.heldForExport())
                errors.add (name + ": the engine is still held after Render to audio");
            if (auto* p = hostedNow(); p == nullptr || p->getSampleRate() != ParityDevice::rate || p->isNonRealtime())
                errors.add (name + ": after Render to audio the plugin is not back in real time at the device's rate");
            settle();
            const auto played = playOnce ("playback after Render to audio");
            if (! played.l.empty() && ! first.l.empty())
                renderVsPlay = maxDiff (played, first, fresh.l.size());
            live.undo();
            settle();
            const auto undone = playOnce ("playback after Render to audio, undone");
            if (! undone.l.empty() && ! first.l.empty())
                undoneVsPlay = maxDiff (undone, first, fresh.l.size());
        }
        // 9. Freeze of the hosted channel (`hostedChannel`; round 190jx, the index made relative at rollup step 6: round 190jz's
        // faded audio clip moved it from 3 to 4): a stem made with a new instance first (sessionpack, no hold)
        {
            const auto freshFolder = folder.getChildFile ("FrozenFresh");
            std::vector<StandIn> freshStems;
            juce::String error;
            Audio freshStem, heldStem;
            if (! sessionpack::renderFreeze (live.getSong(), hostedChannel, ParityDevice::rate, freshFolder, freshStems, {}, error) || freshStems.empty())
                errors.add (name + ", Freeze with a new instance: " + error);
            else
                freshStem = readWav (juce::File (juce::String::fromUTF8 (freshStems.front().path.c_str())));
            if (const auto e = freezeChannelNow (live, hostedChannel, {}, true); e.isNotEmpty())
                errors.add (name + ", Freeze: " + e);
            else
            {
                const auto& f = live.getSong().channels[(std::size_t) hostedChannel].freeze;
                if (! f.on() || f.stems.empty())
                    errors.add (name + ": Freeze left the channel unfrozen");
                else
                    heldStem = readWav (juce::File (juce::String::fromUTF8 (f.stems.front().path.c_str())));
            }
            if (live.heldForExport())
                errors.add (name + ": the engine is still held after Freeze");
            settle();
            const auto played = playOnce ("playback of the frozen channel");
            if (! played.l.empty() && ! first.l.empty())
                frozenVsPlay = maxDiff (played, first, fresh.l.size());
            if (! freshStem.l.empty() && ! heldStem.l.empty())
                freezeStems = maxDiff (freshStem, heldStem, juce::jmin (freshStem.l.size(), heldStem.l.size()));
        }
        renderLine = name + ": Render to audio with the live instance, playback after vs before " + db (renderVsPlay) + " (with a new instance "
                     + db (renderFreshVsPlay) + "), undone " + db (undoneVsPlay) + "; Freeze with the live instance, playback of the frozen channel vs before "
                     + db (frozenVsPlay) + ", its audio vs a new instance's " + db (freezeStems);
        say (renderLine);
        // (as the export: within what two playbacks of the plugin differ by, and at most -90 dBFS for one that repeats itself)
        const auto playAllowed = juce::jmax (3.16e-5f, maxDiff (second, first, fresh.l.size()) * 2.0f);
        if (renderVsPlay < 0.0f || renderVsPlay >= playAllowed)
            errors.add (name + ": playback after Render to audio differs from playback before by " + db (renderVsPlay));
        if (undoneVsPlay < 0.0f || undoneVsPlay >= playAllowed)
            errors.add (name + ": playback after Render to audio, undone, differs from playback before by " + db (undoneVsPlay));
        if (frozenVsPlay < 0.0f || frozenVsPlay >= playAllowed)
            errors.add (name + ": playback of the frozen channel differs from playback before by " + db (frozenVsPlay));
        if (std::getenv ("LYNX_TEST_SYNTH_SEED") != nullptr && name.startsWith ("LYNX Test Synth"))
        {
            if (renderFreshVsPlay >= 0.0f && renderFreshVsPlay < 1.0e-2f)
                errors.add (name + ": Render to audio with a new instance sounds like the live one (" + db (renderFreshVsPlay) + "): the check can't tell");
            if (freezeStems >= 0.0f && freezeStems < 1.0e-2f)
                errors.add (name + ": a freeze with a new instance sounds like the live one (" + db (freezeStems) + "): the check can't tell");
        }

        live.deviceManager.removeAudioCallback (&live);
        live.deviceManager.closeAudioDevice();
        if (errors.size() > errorsBefore || first.l.empty() || second.l.empty() || held.l.empty())
            continue;

        const auto n = fresh.l.size();
        const auto heldVsPlay = maxDiff (held, first, n);
        const auto playVsPlay = maxDiff (second, first, n);
        const auto freshVsPlay = maxDiff (fresh, first, n);
        const auto stemSame = maxDiff (stemHeld, stemFresh, juce::jmax (stemHeld.l.size(), stemFresh.l.size()));
        const auto heldLevels = levels (held, first), playLevels = levels (second, first);
        // (a plugin that does not repeat itself from one playback to the next is held to that much)
        const auto allowed = juce::jmax (3.16e-5f, playVsPlay * 2.0f);
        say (name + ": export with the live instance vs playback " + db (heldVsPlay) + " (" + juce::String (lent) + " plugin(s) lent), playback after it vs before "
             + db (playVsPlay) + " (" + apart (second, first) + "), export with a new instance vs playback " + db (freshVsPlay)
             + "; 10 ms levels: export vs playback " + db (heldLevels) + ", playback vs playback " + db (playLevels) + "; insert 4's stem with / without the hold "
             + db (stemSame) + " (" + juce::String (stemLent) + " lent); at 44.1 kHz: " + juce::String (otherLent) + " lent, the plugin at "
             + juce::String (rateDuring) + " Hz during the export's hold, " + juce::String (rateAfter) + " Hz after, peak " + db (peakOf (other))
             + "; render-ahead engagements after each playback: " + aheadCounts.joinIntoString (", ")
             + "; export time with new instances " + juce::String (freshMs, 0) + " ms, with the live ones " + juce::String (heldMs, 0) + " ms");
        if (heldVsPlay >= allowed)
            errors.add (name + ": the export with the live instance differs from playback by " + db (heldVsPlay));
        // (a plugin that repeats itself: the export left nothing behind that playback hears afterwards)
        if (heldVsPlay < 3.16e-5f && playVsPlay >= 3.16e-5f)
            errors.add (name + ": playback after the export differs from playback before it by " + db (playVsPlay) + " (" + apart (second, first) + ")");
        if (heldLevels >= juce::jmax (3.16e-5f, playLevels * 2.0f))
            errors.add (name + ": the export's levels differ from playback's by " + db (heldLevels));
        if (playVsPlay >= 3.16e-5f && heldVsPlay >= 3.16e-5f)
            notes.add (name + " does not repeat itself (two playbacks differ by " + db (playVsPlay) + ")");
        if (stemHeld.l.size() != stemFresh.l.size() || stemSame > 0.0f)
            errors.add (name + ": LYNX's own units render differently with a plugin lent (" + db (stemSame) + ", "
                        + juce::String ((int) stemHeld.l.size()) + " / " + juce::String ((int) stemFresh.l.size()) + " frames)");
        if (stemLent < 1)
            errors.add (name + ": the stem's export did not take the live plugin");
        if (otherLent < 1 || rateAfter != ParityDevice::rate || peakOf (other) < 1.0e-3f)
            errors.add (name + ": the export at 44.1 kHz (" + juce::String (otherLent) + " lent, the plugin back at " + juce::String (rateAfter)
                        + " Hz, peak " + db (peakOf (other)) + ")");
        // the seeded test synth: a new instance plays other pitches, so the old way can't match playback
        if (std::getenv ("LYNX_TEST_SYNTH_SEED") != nullptr && name.startsWith ("LYNX Test Synth") && freshVsPlay < 1.0e-2f)
            errors.add (name + ": a new instance renders the same as the live one (" + db (freshVsPlay) + "): the seed is not per instance");
        checked.add (name + " " + db (heldVsPlay));
    }
    ParityDevice::beforeBlock = nullptr;
    if (! errors.isEmpty())
        return "ERROR " + errors.joinIntoString ("; ");
    return "OK (export, Render to audio and Freeze with the live instances = playback: " + checked.joinIntoString (", ") + "; restored after"
           + (notes.isEmpty() ? juce::String() : "; " + notes.joinIntoString (", ")) + ")";
}
} // namespace lynx
