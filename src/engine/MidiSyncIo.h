#pragma once

// MIDI sync devices (REA round 165f, docs/analise/REA_rodada165f-midisync.md): master sync (MIDI clock + SPP, or
// MTC) to the output devices that have "Send master sync", "Send all notes off" on init and after an export, and
// "External clock sync" from one input.
// Round 190jl: the output devices' "Port" (FL's Options › MIDI › Output port number): what the MIDI Out channels (and
// plugin wrapper output ports) on that port play goes to the device, time-stamped by the audio thread like the clock
// and sent by the same sender thread.
// Threads: the audio thread runs the generators and pushes time-stamped messages into one lock-free queue per output
// (it never touches a device); a sender thread sends each message when it is due; the MIDI thread feeds the clock
// receiver; the message thread opens and closes devices.

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/LiveEventQueue.h"
#include "core/MidiSync.h"
#include "core/Preferences.h"
#include "engine/Transport.h"

namespace lynx
{
class MidiSyncIo final : private juce::Thread
{
public:
    static constexpr int maxOutputs = 8;

    MidiSyncIo();
    ~MidiSyncIo() override;

    // message thread: opens the outputs that send sync or notes off, closes the others, sets the clock input. With
    // `devices` false nothing is opened (tests, --snapshot).
    // `dryRun`: no device is opened but the sender runs and times every message as if it sent it (--midisync-test).
    // `inputs` (round 190jl): the MIDI inputs by source number (NoteEvent::source − 1): an output device does not get
    // what its own input played back (the same device, or its paired port: controller::pairedOutput)
    void configure (const prefs::MidiPrefs& prefs, bool devices = true, bool dryRun = false, const std::vector<std::string>& inputs = {});
    // the ports the open (or dry-run) output devices have (a 256-bit set for RackRenderer::setHardwareMidiPorts)
    std::array<std::uint64_t, 4> hardwarePorts() const noexcept;
    // message / render thread: CC 123 (All Notes Off) and CC 64 = 0 on the 16 channels of every output with the option
    void sendAllNotesOff();
    // audio thread: the transport was set to a new position with "Reset plugins on transport" on. FL then sends CC 123
    // (All Notes Off, no CC 64) on the 16 channels of every output with "Send all notes off" (FUN_011e7a10 →
    // FUN_011c89c0, REA_rodada167l §6); the sender thread sends it (the audio thread never touches a device).
    void requestTransportNotesOff() noexcept;
    // tests: how many outputs got the transport's CC 123 burst (devices or dry run)
    std::uint64_t transportNotesOffSent() const noexcept { return transportNotesOffCount.load(); }

    // --- audio thread ---
    void deviceStarted (double sampleRate, int bufferSize, int outputLatencySamples) noexcept;
    // Round 190hq (B13, complaint "sync off with latent plugins"): the rack's own delay before the Master output (its
    // plugin delay compensation, ms); added to the device's latency for the clocks sent and followed. Audio thread.
    void setRackLatencyMs (double ms) noexcept { rackLatencyMs = std::max (0.0, ms); }
    double heardLatencyMs() const noexcept { return latencyMs + rackLatencyMs; }
    // Before the block: an external clock's commands and tempo move the transport. `callbackMs` = the callback's
    // Time::getMillisecondCounterHiRes().
    void beginBlock (Transport& transport, double callbackMs, int numSamples) noexcept;
    // Each part of the block (split at loop ends): the master sync messages for it. `offset` = its first sample in
    // the block.
    void subBlock (double startTicks, double ticksPerSample, int numSamples, int offset, bool playing) noexcept;
    bool sending() const noexcept { return anySync.load (std::memory_order_relaxed); }
    // Round 190jl, audio thread: a short message a MIDI Out channel (or a plugin) sent on `port`, at `sample` of the
    // device block (beginBlock's), from MIDI input `source` (0 = none). Queued for every output device on that port.
    void channelMessage (int port, int sample, const std::uint8_t* bytes, int size, int source) noexcept;
    bool hasPorts() const noexcept { return anyPorts.load (std::memory_order_relaxed); }
    // message / audio thread: every note a port device was sent and that still sounds ends (a note off for each; transport
    // stop, panic), in the device's queue after what was already played (the sender thread sends them)
    void requestPortNotesOff() noexcept;
    // tests: a sink that gets every message the sender sends or (dry run) would send: device name, bytes, due and send
    // times (ms, Time::getMillisecondCounterHiRes) and (round 190mh) a port message's sample in the device's stream:
    // the samples of the blocks since deviceStarted + its offset in its block, modulo 2^31 (−1: not a port message, e.g.
    // a note off of a stop / panic marker)
    std::function<void (const std::string&, const std::uint8_t*, int, double, double, std::int64_t)> testSink;
    // LYNX --selftest (round 190jl): ports, timing, echo guard and notes off through the sender thread (dry run)
    static juce::String portSelfTest (bool& ok);

    // --- MIDI thread: true if the message was the followed clock's (consumed) ---
    bool handleInput (const juce::String& device, const juce::MidiMessage& m) noexcept;

    // --- UI ---
    std::string clockStatus() const;             // "No clock" / "Receiving clock 120.0 BPM" (English; the UI translates)
    double followedBpm() const noexcept;         // the external tempo LYNX plays at now (0 = not following)
    struct Stats
    {
        std::uint64_t sent = 0, dropped = 0;
        double meanLateMs = 0.0, maxLateMs = 0.0;   // send time − due time, last second
        double meanBlockUs = 0.0, maxBlockUs = 0.0; // audio-thread cost of the sync per block, last second
    };
    Stats stats() const;

    // tests / --midisync-test: the receiver
    midisync::Receiver& receiver() noexcept { return clockIn; }
    // LYNX --midisync-test report.txt ["output device"] ["input device"]: the generator → receiver loopback in memory,
    // then 5 s of simulated audio callbacks (48 kHz, 256 frames) sending MIDI clock through the sender thread (to the
    // output device, or timed without one), and with an input (a loopback cable / virtual port) the clock received.
    static juce::String selfTest (const juce::String& outputName, const juce::String& inputName);

private:
    void run() override;

    struct Timed
    {
        double dueMs = 0.0;
        midisync::Message message; // (round 190mh: a port message's `sample` is its sample in the device's stream since
                                   //  deviceStarted, modulo 2^31, for testSink; the sender reads no sample. Timed keeps
                                   //  its 24 bytes: two queues of 2048 per output live in every AudioEngine)
    };
    struct Slot
    {
        std::atomic<bool> active { false };  // the audio thread generates for it
        std::atomic<int> type { 0 };
        std::atomic<int> offsetMs { 0 };
        std::atomic<std::uint32_t> generation { 0 };
        std::uint32_t seenGeneration = 0;    // audio thread
        midisync::Generator generator;       // audio thread
        SpscQueue<Timed, 2048> queue;        // audio → sender
        std::string name;                    // message thread
        std::string id;                      // message thread: the opened device's identifier (round 190gh, hot-plug)
        bool notesOff = false, sync = false; // message thread
        std::unique_ptr<juce::MidiOutput> device; // under outputLock
        // round 190jl: the device's port (−1 = none) and what goes out on it
        std::atomic<int> port { -1 };
        std::atomic<std::uint64_t> echoMask { 0 }; // the input sources (bit s & 63) whose own messages it does not get
        SpscQueue<Timed, 2048> portQueue;    // audio → sender
        std::array<std::uint16_t, 128> held {}; // sender thread: [key] the channels a note sounds on at the device
        std::atomic<std::uint32_t> opened { 0 }; // raised when another device is opened in the slot
        std::uint32_t seenOpened = 0;        // sender thread
    };
    std::array<Slot, maxOutputs> slots;
    juce::CriticalSection outputLock;        // the devices (message, sender, render threads; never the audio thread)
    std::atomic<bool> anySync { false }, dry { false }, anyNotesOff { false }, anyPorts { false };
    std::atomic<std::uint32_t> portNotesOffRequests { 0 };
    std::uint32_t portNotesOffSeen = 0;      // audio thread
    std::array<std::uint64_t, 4> portBits {}; // message thread (configure)
    std::atomic<std::uint32_t> portReleaseSlots { 0 }; // the slots whose port changed: their notes end (message thread sets)
    void releasePortNotes (Slot& slot); // sender thread, under outputLock
    void sendNow (Slot& slot, const std::uint8_t* bytes, int size, double dueMs, std::int64_t deviceSample = -1); // sender thread, under outputLock
    std::atomic<std::uint32_t> notesOffRequests { 0 };
    std::uint32_t notesOffHandled = 0;       // sender thread
    std::atomic<std::uint64_t> transportNotesOffCount { 0 };
    void sendTransportNotesOff();            // sender thread

    // timing: the device block clock mapped to milliseconds (smoothed: callbacks wake up with jitter)
    double sampleRate = 44100.0, latencyMs = 0.0, rackLatencyMs = 0.0;
    double blockStartMs = 0.0, predictedMs = -1.0;
    std::int64_t blockSample = 0, nextBlockSample = 0; // audio thread: the device samples before this block / the next
    midisync::BlockMessages scratch;

    // external clock
    midisync::Receiver clockIn;
    juce::SpinLock clockNameLock;
    juce::String clockName;                  // under clockNameLock
    std::atomic<bool> following { false }, resetReceiver { false };
    std::atomic<int> clockOffsetMs { 0 };
    bool waitingFirstPulse = false;          // audio thread: Start / Continue received, play on the first pulse
    bool wasFollowing = false;
    std::atomic<double> appliedBpm { 0.0 };

    // stats (sender thread / audio thread writes, UI reads)
    std::atomic<std::uint64_t> sentCount { 0 }, droppedCount { 0 };
    std::atomic<double> lateSum { 0.0 }, lateMax { 0.0 }, blockSum { 0.0 }, blockMax { 0.0 };
    std::atomic<std::uint64_t> lateN { 0 }, blockN { 0 };
    std::vector<double>* lateLog = nullptr;  // --midisync-test only (the sender thread appends)
    mutable Stats lastStats;
    mutable double lastStatsMs = 0.0;
};
} // namespace lynx
