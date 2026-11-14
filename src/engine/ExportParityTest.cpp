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

#include "engine/AudioEngine.h"

#include "engine/ChannelRackProcessor.h"
#include "engine/Exporter.h"

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
                    callback->audioDeviceIOCallbackWithContext (nullptr, 0, data.getArrayOfWritePointers(), data.getNumChannels(), block, {});
            }
            juce::Thread::sleep (2); // (a block is 9.2 ms: ~4× real time, well within the bounce tap's 5 s)
        }
    }

    juce::BigInteger outsNow;
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
    juce::AudioBuffer<float> b (1, 24000);
    const double f = kind == 0 ? 220.0 : kind == 1 ? 331.0 : 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / 48000.0;
        b.setSample (0, i, f > 0.0 ? (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * f * t) * std::exp (-3.0 * t))
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
    for (int k = 0; k < 3; ++k)
        if (! writeSample (folder.getChildFile ("parity-" + juce::String (k) + ".wav"), k))
            return "ERROR can't write the test samples in " + folder.getFullPathName();
    const auto song = paritySong (folder);
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
    auto playback = [&] (int block, juce::String& error) {
        Audio played;
        ParityDevice::block = block;
        AudioEngine live (false);
        live.player.setProcessor (&live.graph);
        live.deviceManager.addAudioDeviceType (std::make_unique<ParityType>());
        if (const auto e = live.deviceManager.initialise (0, 2, nullptr, true); e.isNotEmpty())
        {
            error = "open the stand-in device: " + e;
            return played;
        }
        live.deviceManager.addAudioCallback (&live);
        live.setSong (song);
        for (int i = 0; i < 20; ++i) // (the rack is built and published by the engine's timer)
        {
            live.timerCallback();
            juce::Thread::sleep (10);
        }
        if (const auto e = live.startRealtimeBounce (0.0); e.isNotEmpty())
        {
            error = "playback (real-time bounce): " + e;
            return played;
        }
        const auto latency = (std::int64_t) live.bounceLatency();
        const auto wanted = latency + (std::int64_t) mix.l.size();
        const auto started = juce::Time::getMillisecondCounter();
        while ((std::int64_t) played.l.size() < wanted && juce::Time::getMillisecondCounter() - started < 60000 && live.bounceStillPlaying())
        {
            live.timerCallback();
            juce::Thread::sleep (20);
            live.pullRealtimeBounce (played.l, played.r);
        }
        const auto dropouts = live.bounceDropouts();
        live.finishRealtimeBounce();
        live.deviceManager.removeAudioCallback (&live);
        live.deviceManager.closeAudioDevice();
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
        say ("playback in blocks of " + juce::String (block) + ": " + juce::String ((int) latency) + " samples of delay compensation trimmed");
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
            const auto out = dir.getChildFile ("playback-" + juce::String (block) + ".wav");
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
    juce::String playError;
    const auto played = playback (441, playError);
    if (playError.isNotEmpty())
        return "ERROR " + playError;
    {
        const auto n = juce::jmin (mix.l.size(), played.l.size());
        const auto d = maxDiff (mix, played, n);
        say ("export vs playback: " + juce::String ((int) mix.l.size()) + " / " + juce::String ((int) played.l.size()) + " frames, max difference " + db (d)
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
            const auto same = playback (512, e512);
            errors.add ("playback differs from the export by " + db (d) + " (best lag " + juce::String (bestLag) + ": " + db (best) + ", worst at frame "
                        + juce::String ((int) worst) + ", " + where (mix, played) + "; in blocks of 512: "
                        + (e512.isNotEmpty() ? e512 : db (maxDiff (mix, same, n)) + ", " + where (mix, same)) + ")");
        }
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
    return "OK (export = playback, stems of 2 modes add up to the mix" + (notes.isEmpty() ? juce::String() : ", " + notes.joinIntoString (", ")) + ")";
}
} // namespace lynx
