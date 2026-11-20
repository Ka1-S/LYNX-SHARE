// Round 190ks (GitHub #198: "the pads I click in my drum plugin's window are not recorded"). AudioEngine::padRecordSelfTest
// is `LYNX --padrectest "<LYNX Test Pads.vst3>" report.txt`: the live engine on a stand-in audio device (real-time blocks
// of 256 at 48 kHz) records a take while a hosted drum plugin with a MIDI output (tools/testplugin/lynx_test_pads_vst3.cpp)
// "is clicked" (its scripted hits, LYNX_TEST_PADS_AUTO) and passes the notes it is sent on (LYNX_TEST_PADS_ECHO):
//  1. channel 0, its window open: the two hits are recorded on its channel, at their velocity, where they were heard;
//  2. the pattern's own note that the plugin echoes back is not recorded a second time;
//  3. channel 1, the same plugin with its window closed: nothing of it is recorded;
//  4. the wrapper's "Record notes from the plugin window" off on channel 0: its channel is no longer marked.

#include <cstdlib>

#include "engine/AudioEngine.h"
#include "engine/ChannelRackProcessor.h"

namespace lynx
{
namespace
{
// callbacks of 256 samples at 48 kHz, paced in real time (the take's positions come from the audio clock)
struct PadTestDevice final : juce::AudioIODevice, private juce::Thread
{
    PadTestDevice() : juce::AudioIODevice ("Pad test device", "Pad record test"), juce::Thread ("pad test device") {}
    ~PadTestDevice() override { close(); }
    static constexpr double rate = 48000.0;
    static constexpr int block = 256;
    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { rate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { block }; }
    int getDefaultBufferSize() override { return block; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger& outs, double, int) override
    {
        close();
        outsNow = outs;
        data.setSize (2, block);
        opened = true;
        startThread (juce::Thread::Priority::high);
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
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    void run() override
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (std::int64_t b = 0; ! threadShouldExit(); ++b)
        {
            const auto due = start + (double) b * block * 1000.0 / rate;
            while (juce::Time::getMillisecondCounterHiRes() < due && ! threadShouldExit())
                juce::Thread::sleep (1);
            const juce::ScopedLock sl (callbackLock);
            if (callback != nullptr)
                callback->audioDeviceIOCallbackWithContext (nullptr, 0, data.getArrayOfWritePointers(), data.getNumChannels(), block, {});
        }
    }
    juce::BigInteger outsNow;
    juce::AudioBuffer<float> data;
    juce::CriticalSection callbackLock;
    juce::AudioIODeviceCallback* callback = nullptr;
    bool opened = false;
};

struct PadTestType final : juce::AudioIODeviceType
{
    PadTestType() : juce::AudioIODeviceType ("Pad record test") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override { return input ? juce::StringArray() : juce::StringArray ("Pad test device"); }
    int getDefaultDeviceIndex (bool input) const override { return input ? -1 : 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool input) const override { return d != nullptr && ! input ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String&) override
    {
        return out == "Pad test device" ? new PadTestDevice() : nullptr;
    }
};

void setVariable (const char* name, const char* value)
{
#if JUCE_WINDOWS
    _putenv_s (name, value);
#else
    setenv (name, value, 1);
#endif
}
} // namespace

juce::String AudioEngine::padRecordSelfTest (const juce::String& pluginPath)
{
    const juce::File file (pluginPath);
    if (! file.exists())
        return "ERROR no plugin at " + pluginPath;
    // the hits "clicked" in its window: at its 150th process() call key 36 at 100, at its 230th key 40 at 50 (each held
    // 10 calls), whatever the host sends it goes back out (a MIDI thru plugin). Read by each instance when it is made.
    setVariable ("LYNX_TEST_PADS_AUTO", "150:36:100:10,230:40:50:10");
    setVariable ("LYNX_TEST_PADS_ECHO", "1");

    AudioEngine live (false);
    live.player.setProcessor (&live.graph);
    live.deviceManager.addAudioDeviceType (std::make_unique<PadTestType>());
    if (const auto e = live.deviceManager.initialise (0, 2, nullptr, true); e.isNotEmpty())
        return "ERROR open the stand-in device: " + e;
    live.deviceManager.addAudioCallback (&live);

    live.getPluginHost().addPluginFile (file);
    juce::String id, name;
    for (const auto& t : live.getPluginHost().getKnownPlugins().getTypes())
        if (t.isInstrument && juce::File (t.fileOrIdentifier).getFileName() == file.getFileName())
        {
            id = t.createIdentifierString();
            name = t.name;
            break;
        }
    if (id.isEmpty())
        return "ERROR no instrument in " + file.getFileName();

    Song song;
    song.bpm = 120.0;
    song.mixer = Song::defaultMixer (2);
    for (int c = 0; c < 2; ++c)
    {
        ChannelDef ch;
        ch.name = c == 0 ? "Pads (window open)" : "Pads (window closed)";
        ch.instrument = InstrumentKind::plugin;
        ch.plugin.id = id.toStdString();
        ch.plugin.name = name.toStdString();
        song.channels.push_back (ch);
    }
    auto& pattern = song.patterns.emplace_back();
    pattern.lengthTicks = 8 * ppq; // (2 bars: the take does not wrap)
    pattern.notes.push_back ({ ppq / 2, ppq / 4, 0, 38, 0.78f }); // the pattern's snare on channel 0: the plugin echoes it
    pattern.sortNotes();
    song.mode = PlayMode::pattern;
    live.setSong (song);
    auto options = live.getRecordOptions();
    options.mode = RecordMode::overdub; // (blend: the take does not clear the channel's pattern note)
    options.recordNotes = true;
    options.quantise = 0;
    options.countInBars = 0;
    live.setRecordOptions (options);

    juce::StringArray errors;
    juce::String report;
    auto pump = [&live] (int ms) {
        const auto since = juce::Time::getMillisecondCounter();
        while ((int) (juce::Time::getMillisecondCounter() - since) < ms)
        {
            live.timerCallback();
            juce::Thread::sleep (10);
        }
    };
    pump (200); // (the rack is built and published by the engine's timer; the plugins load)
    if (live.rackProcessor == nullptr)
        return "ERROR no rack";
    // channel 0's plugin window "is open" (the engine is told as by the plugin window)
    const int editorKey = 0;
    live.setEditorOpen (&editorKey, [] (Song& s) -> PluginRef* { return s.channels.empty() ? nullptr : &s.channels[0].plugin; }, -1, true);
    const auto marked = live.recordedPluginChannels()[0];
    report << "marked channels: " << juce::String::toHexString ((juce::int64) marked) << "\n";
    if (marked != 1u)
        errors.add ("the open window's channel is not marked");

    // the take: the hits come at about 0.8 s and 1.2 s after the instance was made; record for 2.4 s from now
    live.setRecording (true);
    live.play();
    pump (2400);
    live.stop();
    pump (200);
    live.setRecording (false);
    pump (100);

    auto notesOn = [&live] (int channel) {
        std::vector<Note> out;
        const auto& s = live.getSong();
        if (! s.patterns.empty())
            for (const auto& n : s.patterns[0].notes)
                if (n.channel == channel)
                    out.push_back (n);
        return out;
    };
    const auto zero = notesOn (0), one = notesOn (1);
    for (const auto& n : zero)
        report << "channel 0: key " << n.key << " start " << (int) n.start << " length " << (int) n.length << " velocity "
               << juce::roundToInt (n.velocity * 127.0f) << "\n";
    for (const auto& n : one)
        report << "channel 1: key " << n.key << " start " << (int) n.start << "\n";
    auto count = [] (const std::vector<Note>& notes, int key) { return (int) std::count_if (notes.begin(), notes.end(), [key] (const Note& n) { return n.key == key; }); };
    const Note* kick = nullptr;
    const Note* tom = nullptr;
    for (const auto& n : zero)
    {
        if (n.key == 36 && kick == nullptr)
            kick = &n;
        if (n.key == 40 && tom == nullptr)
            tom = &n;
    }
    if (kick == nullptr || tom == nullptr)
        errors.add ("the clicked pads were not recorded (key 36: " + juce::String (count (zero, 36)) + ", key 40: " + juce::String (count (zero, 40)) + ")");
    else
    {
        if (juce::roundToInt (kick->velocity * 127.0f) != 100 || juce::roundToInt (tom->velocity * 127.0f) != 50)
            errors.add ("velocities " + juce::String (juce::roundToInt (kick->velocity * 127.0f)) + " / " + juce::String (juce::roundToInt (tom->velocity * 127.0f)));
        // 80 calls of 256 samples apart = 20480 samples = 0.4267 s = 0.853 beats at 120 BPM: within a block or two
        const auto apart = (double) (tom->start - kick->start) / (double) ppq;
        report << "kick → tom: " << juce::String (apart, 3) << " beats (0.853)\n";
        if (std::abs (apart - 80.0 * 256.0 / 48000.0 * 2.0) > 2.5 * 256.0 / 48000.0 * 2.0)
            errors.add ("the hits are " + juce::String (apart, 3) + " beats apart");
        // held 10 calls = 2560 samples = 0.1067 beats
        const auto held = (double) kick->length / (double) ppq;
        if (std::abs (held - 10.0 * 256.0 / 48000.0 * 2.0) > 2.5 * 256.0 / 48000.0 * 2.0)
            errors.add ("the kick is held " + juce::String (held, 3) + " beats");
    }
    if (count (zero, 36) != 1 || count (zero, 40) != 1)
        errors.add ("each hit once: key 36 " + juce::String (count (zero, 36)) + "×, key 40 " + juce::String (count (zero, 40)) + "×");
    if (count (zero, 38) != 1)
        errors.add ("the echoed pattern note is in the pattern " + juce::String (count (zero, 38)) + " times");
    if (! one.empty())
        errors.add ("the closed window's channel recorded " + juce::String ((int) one.size()) + " notes");

    // 4. the option off: the channel is no longer marked (nothing of it is captured)
    live.editSong ([] (Song& s) { s.channels[0].plugin.recordOwnNotes = false; });
    pump (100);
    if (live.recordedPluginChannels()[0] != 0)
        errors.add ("the option off still marks the channel");

    live.setEditorOpen (&editorKey, {}, -1, false);
    live.deviceManager.removeAudioCallback (&live);
    live.deviceManager.closeAudioDevice();
    setVariable ("LYNX_TEST_PADS_AUTO", "");
    setVariable ("LYNX_TEST_PADS_ECHO", "");
    if (! errors.isEmpty())
        return "ERROR " + errors.joinIntoString ("; ") + "\n" + report;
    return "OK\n" + report;
}
} // namespace lynx
