// Round 190jl (Discord #help, 0.1.29: "a MIDI Out channel sends no notes to my hardware synth"): FL's Options › MIDI ›
// Output "Port". AudioEngine::midiOutPortSelfTest() is the --selftest line "midi_out_ports":
//  1. MidiSyncIo::portSelfTest: ports, due times, the echo guard and notes off through the sender thread (dry run);
//  2. end to end: the live engine on a stand-in audio device (real-time blocks of 256 at 48 kHz) plays a pattern on a
//     MIDI Out channel on port 1; a stand-in output device on port 1 (the sender's dry run, no device opened) must get
//     the exact note bytes, a beat apart to the sample (counted in the device's sample stream: round 190mh, the wall
//     clock's due times are only reported, a loaded CI runner's callbacks wake up late), and a stop must end the note
//     that still sounds with exactly one note off.

#include <memory>
#include <mutex>

#include "core/MidiOut.h"
#include "engine/AudioEngine.h"
#include "engine/ChannelRackProcessor.h"

namespace lynx
{
namespace
{
// callbacks of 256 samples at 48 kHz, paced in real time (the MIDI sender times its messages on the wall clock)
struct PortTestDevice final : juce::AudioIODevice, private juce::Thread
{
    PortTestDevice() : juce::AudioIODevice ("Port test device", "MIDI port test"), juce::Thread ("port test device") {}
    ~PortTestDevice() override { close(); }
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

struct PortTestType final : juce::AudioIODeviceType
{
    PortTestType() : juce::AudioIODeviceType ("MIDI port test") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override { return input ? juce::StringArray() : juce::StringArray ("Port test device"); }
    int getDefaultDeviceIndex (bool input) const override { return input ? -1 : 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool input) const override { return d != nullptr && ! input ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String&) override
    {
        return out == "Port test device" ? new PortTestDevice() : nullptr;
    }
};
} // namespace

juce::String AudioEngine::midiOutPortSelfTest()
{
    auto ok = true;
    juce::String report = MidiSyncIo::portSelfTest (ok);
    juce::StringArray errors;
    if (! ok)
        errors.add ("sender: " + report.replace ("\n", " | "));

    // --- 2. end to end ------------------------------------------------------------------------------------------------
    struct Sent
    {
        std::vector<std::uint8_t> bytes;
        double due = 0.0, at = 0.0;
        std::int64_t sample = -1; // (in the device's stream; −1: a stop marker's note off)
    };
    std::mutex lock;
    std::vector<Sent> sent; // (before the engine: its sender thread, which fills them, ends first)
    const auto livePtr = std::make_unique<AudioEngine> (false); // (round 190mh: on the heap, as the stack of --selftest is tight)
    auto& live = *livePtr;
    live.player.setProcessor (&live.graph);
    live.deviceManager.addAudioDeviceType (std::make_unique<PortTestType>());
    if (const auto e = live.deviceManager.initialise (0, 2, nullptr, true); e.isNotEmpty())
        return "ERROR open the stand-in device: " + e;
    live.deviceManager.addAudioCallback (&live);

    Song song;
    song.bpm = 120.0;
    song.mixer = Song::defaultMixer (2);
    midiout::State st;
    st.channel = 1; // MIDI channel 2
    st.port = 1;
    ChannelDef out;
    out.name = "MIDI Out";
    out.instrument = InstrumentKind::midiOut;
    out.pluginState = midiout::write (st);
    song.channels.push_back (out);
    auto& pattern = song.patterns.emplace_back();
    pattern.lengthTicks = 8 * ppq;
    pattern.notes.push_back ({ 0, ppq / 2, 0, 60, 0.78f });      // C, half a beat
    pattern.notes.push_back ({ ppq, ppq / 2, 0, 64, 0.5f });     // E on beat 2
    pattern.notes.push_back ({ 2 * ppq, 6 * ppq, 0, 67, 1.0f }); // G held: the stop ends it
    pattern.sortNotes();
    song.mode = PlayMode::pattern;
    live.setSong (song);

    live.midiSync.testSink = [&] (const std::string& device, const std::uint8_t* bytes, int size, double due, double at, std::int64_t sample) {
        if (device != "Stand-in synth")
            return;
        const std::lock_guard<std::mutex> g (lock);
        sent.push_back ({ { bytes, bytes + size }, due, at, sample });
    };
    prefs::MidiPrefs prefs;
    prefs::MidiOutputPrefs device;
    device.name = "Stand-in synth";
    device.port = 1;
    prefs.setOutput (device);
    live.midiPrefs = prefs;
    live.midiSync.configure (prefs, false, true, {}); // (dry run: timed and handed to the sink, no device opened)
    live.hardwareMidiPorts = live.midiSync.hardwarePorts();
    for (int i = 0; i < 20; ++i) // (the rack is built and published by the engine's timer)
    {
        live.timerCallback();
        juce::Thread::sleep (10);
    }
    if (live.rackProcessor == nullptr)
        return "ERROR no rack";
    live.rackProcessor->getRenderer().setHardwareMidiPorts (live.hardwareMidiPorts);
    live.publishRenderAheadTargets();

    live.play();
    const auto since = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - since < 1400) // (2.8 beats at 120 BPM: the G sounds)
    {
        live.timerCallback();
        juce::Thread::sleep (20);
    }
    live.stop();
    const auto stopped = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - stopped < 200)
    {
        live.timerCallback();
        juce::Thread::sleep (20);
    }
    live.deviceManager.removeAudioCallback (&live);
    live.deviceManager.closeAudioDevice();
    juce::Thread::sleep (100); // (the sender's last messages)

    const std::lock_guard<std::mutex> g (lock);
    std::vector<Sent> notes;
    for (const auto& m : sent)
        if (! m.bytes.empty() && (m.bytes[0] & 0xE0) == 0x80)
            notes.push_back (m);
    juce::String seen;
    for (const auto& m : notes)
        seen << juce::String::toHexString (m.bytes.data(), (int) m.bytes.size()) << " ";
    report << "end to end: " << (int) sent.size() << " messages, notes: " << seen.trim() << "\n";
    // channel 2: C on, C off half a beat later, E on at the beat, E off, G on at beat 3, G off at the stop
    const std::vector<std::vector<std::uint8_t>> expected { { 0x91, 60, 99 }, { 0x81, 60, 0 }, { 0x91, 64, 64 }, { 0x81, 64, 0 }, { 0x91, 67, 127 }, { 0x81, 67, 0 } };
    auto sameNotes = notes.size() >= expected.size();
    for (std::size_t k = 0; sameNotes && k < expected.size(); ++k)
        sameNotes = notes[k].bytes == expected[k];
    if (! sameNotes)
        errors.add ("notes " + seen.trim());
    else
    {
        // a beat = 24000 samples at 120 BPM and 48 kHz: the notes the rack played, counted in the device's sample stream
        // (exact, whatever the callbacks' wake-up jitter). Round 190mh: this was a 1 ms check of the wall-clock due times,
        // which follow the callbacks' times smoothed (MidiSyncIo::beginBlock) and missed it on loaded macOS CI runners;
        // the due time of a sample is MidiSyncIo::portSelfTest's exact check ("due +7.417 ms")
        const auto at = [&notes] (std::size_t k) { return notes[k].sample - notes[0].sample; };
        report << "samples: C on→off " << (juce::int64) at (1) << " (12000), C→E " << (juce::int64) at (2) << " (24000), E on→off "
               << (juce::int64) (at (3) - at (2)) << " (12000), C→G " << (juce::int64) at (4) << " (48000)\n";
        if (notes[0].sample < 0 || at (1) != 12000 || at (2) != 24000 || at (3) != 36000 || at (4) != 48000)
            errors.add ("note samples " + juce::String ((juce::int64) at (1)) + " / " + juce::String ((juce::int64) at (2)) + " / "
                        + juce::String ((juce::int64) at (3)) + " / " + juce::String ((juce::int64) at (4)));
        const auto beat = notes[2].due - notes[0].due, half = notes[1].due - notes[0].due, third = notes[4].due - notes[0].due;
        report << "due (wall clock, smoothed): C→E " << juce::String (beat, 3) << " ms (500), C on→off " << juce::String (half, 3)
               << " ms (250), C→G " << juce::String (third, 3) << " ms (1000)\n";
        double late = 0.0;
        for (const auto& m : notes)
            late = juce::jmax (late, m.at - m.due);
        report << "sent at most " << juce::String (late, 3) << " ms after the due time\n";
        if (notes.size() != expected.size())
            errors.add ("extra notes after the stop");
        else
            report << "G's note off: " << (notes[5].sample >= 0 ? "the rack's, at the stop" : "the stop's notes off") << "\n";
    }
    if (! errors.isEmpty())
        return "ERROR " + errors.joinIntoString ("; ") + "\n" + report;
    return "OK\n" + report;
}
} // namespace lynx
