#include "engine/MidiSyncIo.h"

#include "core/ControllerProfile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

#if JUCE_WINDOWS
extern "C" __declspec (dllimport) unsigned int __stdcall timeBeginPeriod (unsigned int);
extern "C" __declspec (dllimport) unsigned int __stdcall timeEndPeriod (unsigned int);
#endif

namespace lynx
{
namespace
{
void atomicMax (std::atomic<double>& a, double v) noexcept
{
    auto now = a.load (std::memory_order_relaxed);
    while (v > now && ! a.compare_exchange_weak (now, v, std::memory_order_relaxed))
    {
    }
}

void atomicAdd (std::atomic<double>& a, double v) noexcept
{
    auto now = a.load (std::memory_order_relaxed);
    while (! a.compare_exchange_weak (now, now + v, std::memory_order_relaxed))
    {
    }
}
} // namespace

MidiSyncIo::MidiSyncIo() : juce::Thread ("LYNX MIDI sync") {}

MidiSyncIo::~MidiSyncIo()
{
    stopThread (2000);
    const juce::ScopedLock lock (outputLock);
    for (auto& s : slots)
        s.device.reset();
}

void MidiSyncIo::configure (const prefs::MidiPrefs& prefs, bool devices, bool dryRun, const std::vector<std::string>& inputs)
{
    dry.store (dryRun);
    {
        const juce::SpinLock::ScopedLockType lock (clockNameLock);
        clockName = juce::String::fromUTF8 (prefs.clockInput.c_str());
    }
    clockOffsetMs.store (juce::jlimit (-1000, 1000, prefs.clockOffsetMs));
    // (the receiver belongs to the MIDI thread: it resets itself on its next message)
    if (following.exchange (! prefs.clockInput.empty()) != ! prefs.clockInput.empty())
        resetReceiver.store (true);

    // the outputs that need a device: sync or notes off on, or a port (round 190jl: FL opens an output "when it has a
    // port number or master sync", REA 165f)
    // (round 190jl: an output without settings of its own whose input has a port takes that port, as in FL)
    std::vector<prefs::MidiOutputPrefs> wanted;
    for (const auto& o : prefs.outputs)
        if (o.masterSync || o.allNotesOff || prefs.outputPort (o.name) >= 0)
        {
            wanted.push_back (o);
            wanted.back().port = prefs.outputPort (o.name);
        }
    const auto available = devices ? juce::MidiOutput::getAvailableDevices() : juce::Array<juce::MidiDeviceInfo>();
    std::vector<std::string> outputNames;
    for (const auto& d : available)
        outputNames.push_back (d.name.toStdString());
    for (const auto& o : prefs.outputs) // (and, for a dry run, the outputs the settings name)
        outputNames.push_back (o.name);
    for (const auto& i : prefs.inputs)
        if (i.port >= 0)
        {
            const auto name = controller::pairedOutput (i.name, outputNames);
            if (! name.empty() && std::none_of (wanted.begin(), wanted.end(), [&] (const prefs::MidiOutputPrefs& w) { return w.name == name; })
                && prefs.outputPort (name) >= 0)
            {
                auto o = prefs.output (name);
                o.port = prefs.outputPort (name);
                wanted.push_back (o);
            }
        }
    if ((int) wanted.size() > maxOutputs)
        wanted.resize (maxOutputs);

    auto any = false, notesOff = false, ports = false;
    portBits = {};
    std::uint32_t release = 0;
    // (round 190jl: a device keeps its slot, so a change to another output's settings neither reopens it nor forgets the
    // notes it sounds; the new ones take the free slots)
    std::array<const prefs::MidiOutputPrefs*, maxOutputs> assigned {};
    std::vector<char> placed (wanted.size(), 0);
    for (int i = 0; i < maxOutputs; ++i)
        for (std::size_t k = 0; k < wanted.size(); ++k)
            if (placed[k] == 0 && ! slots[(std::size_t) i].name.empty() && slots[(std::size_t) i].name == wanted[k].name)
            {
                assigned[(std::size_t) i] = &wanted[k];
                placed[k] = 1;
                break;
            }
    for (std::size_t k = 0; k < wanted.size(); ++k)
        for (int i = 0; placed[k] == 0 && i < maxOutputs; ++i)
            if (assigned[(std::size_t) i] == nullptr
                && std::none_of (wanted.begin(), wanted.end(), [&] (const prefs::MidiOutputPrefs& w) { return ! slots[(std::size_t) i].name.empty() && w.name == slots[(std::size_t) i].name; }))
            {
                assigned[(std::size_t) i] = &wanted[k];
                placed[k] = 1;
            }
    for (int i = 0; i < maxOutputs; ++i)
    {
        auto& slot = slots[(std::size_t) i];
        const prefs::MidiOutputPrefs* want = assigned[(std::size_t) i];
        // (round 190gh, hot-plug: the open device must still be listed under its identifier; one that was unplugged is
        // closed, and opened again by name when it comes back, possibly under another identifier)
        const auto stillListed = ! devices || std::any_of (available.begin(), available.end(), [&slot] (const juce::MidiDeviceInfo& d) {
                                     return d.identifier.toStdString() == slot.id && d.name.toStdString() == slot.name;
                                 });
        const auto sameDevice = want != nullptr && slot.name == want->name && (slot.device != nullptr || ! devices) && stillListed;
        if (! sameDevice)
        {
            slot.active.store (false);
            std::unique_ptr<juce::MidiOutput> opened;
            std::string openedId;
            if (want != nullptr && devices)
                for (const auto& d : available)
                    if (d.name.toStdString() == want->name)
                    {
                        opened = juce::MidiOutput::openDevice (d.identifier);
                        openedId = d.identifier.toStdString();
                        break;
                    }
            {
                const juce::ScopedLock lock (outputLock);
                slot.device = std::move (opened);
            }
            slot.name = want != nullptr ? want->name : std::string();
            slot.id = openedId;
            slot.opened.fetch_add (1, std::memory_order_release);
        }
        slot.sync = want != nullptr && want->masterSync;
        slot.notesOff = want != nullptr && want->allNotesOff;
        const auto type = (int) midisync::syncTypeFromKey (want != nullptr ? want->syncType : std::string());
        if (slot.type.exchange (type) != type || ! sameDevice)
            slot.generation.fetch_add (1);
        slot.offsetMs.store (want != nullptr ? juce::jlimit (-1000, 1000, want->offsetMs) : 0);
        slot.active.store (slot.sync && (slot.device != nullptr || ! devices));
        any = any || slot.active.load();
        notesOff = notesOff || (slot.notesOff && (slot.device != nullptr || dryRun));

        // round 190jl: its port, and the inputs that are the same device (their own notes don't come back to it)
        const auto port = want != nullptr && want->port >= 0 && (slot.device != nullptr || ! devices) ? std::min (want->port, 255) : -1;
        const auto before = slot.port.exchange (port);
        if (before >= 0 && before != port && sameDevice)
            release |= 1u << i; // (moved to another port, or none: what it played there ends)
        std::uint64_t echo = 0;
        if (port >= 0)
            for (std::size_t k = 0; k < inputs.size() && k < 63; ++k)
                if (inputs[k] == slot.name || controller::pairedOutput (inputs[k], { slot.name }) == slot.name)
                    echo |= std::uint64_t { 1 } << (k + 1);
        slot.echoMask.store (echo);
        if (port >= 0)
        {
            ports = true;
            portBits[(std::size_t) port >> 6] |= std::uint64_t { 1 } << (port & 63);
        }
    }
    anySync.store (any);
    anyNotesOff.store (notesOff);
    anyPorts.store (ports);
    if (release != 0)
    {
        portReleaseSlots.fetch_or (release);
        notify();
    }
    if ((devices || dryRun) && (any || notesOff || ports || release != 0) && ! isThreadRunning())
        startThread (juce::Thread::Priority::highest);
}

std::array<std::uint64_t, 4> MidiSyncIo::hardwarePorts() const noexcept
{
    return portBits;
}

void MidiSyncIo::channelMessage (int port, int sample, const std::uint8_t* bytes, int size, int source) noexcept
{
    if (! anyPorts.load (std::memory_order_relaxed) || port < 0 || bytes == nullptr || size < 1 || size > 3)
        return;
    for (auto& slot : slots)
    {
        if (slot.port.load (std::memory_order_relaxed) != port)
            continue;
        if (source > 0 && (slot.echoMask.load (std::memory_order_relaxed) >> (source & 63) & 1u) != 0)
            continue; // (the device's own input: it played it already)
        Timed t;
        // heard with the block's audio (one buffer + the device's latency), as FL sends them when the audio is played
        // (REA midiout190jl: FL's MIDI thread sends what the played position reached; the master sync offset is the
        // clock's alone, FLEngine 0xbb0b00 / 0x15e05e8). Rollup 0.1.30: the rack's delay compensation too (round
        // 190hq's heardLatencyMs, as the clock): the block's audio is heard that much later behind latent plugins
        t.dueMs = blockStartMs + heardLatencyMs() + 1000.0 * sample / sampleRate;
        t.message.sample = (int) ((blockSample + sample) & 0x7fffffff); // (the stream's sample: Timed's note in the header)
        t.message.size = (std::uint8_t) size;
        std::copy (bytes, bytes + size, t.message.bytes.begin());
        if (! slot.portQueue.push (t))
            droppedCount.fetch_add (1, std::memory_order_relaxed);
    }
}

void MidiSyncIo::requestPortNotesOff() noexcept
{
    if (anyPorts.load (std::memory_order_relaxed))
        portNotesOffRequests.fetch_add (1, std::memory_order_release);
}

void MidiSyncIo::sendNow (Slot& slot, const std::uint8_t* bytes, int size, double dueMs, std::int64_t deviceSample)
{
    const auto isDry = dry.load (std::memory_order_relaxed);
    if (slot.device == nullptr && ! isDry)
        return;
    if (slot.device != nullptr)
        slot.device->sendMessageNow (juce::MidiMessage (bytes, size));
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (testSink)
        testSink (slot.name, bytes, size, dueMs, now, deviceSample);
    if (size >= 2 && (bytes[0] & 0xE0) == 0x80) // (a note on / off: the notes each port device still sounds)
    {
        const auto ch = bytes[0] & 15, key = bytes[1] & 127;
        if ((bytes[0] & 0xF0) == 0x90 && size > 2 && bytes[2] > 0)
            slot.held[(std::size_t) key] = (std::uint16_t) (slot.held[(std::size_t) key] | (1u << ch));
        else
            slot.held[(std::size_t) key] = (std::uint16_t) (slot.held[(std::size_t) key] & ~(1u << ch));
    }
    const auto late = std::max (0.0, now - dueMs);
    atomicAdd (lateSum, late);
    atomicMax (lateMax, late);
    lateN.fetch_add (1, std::memory_order_relaxed);
    if (lateLog != nullptr)
        lateLog->push_back (late);
    sentCount.fetch_add (1, std::memory_order_relaxed);
}

// (a LYNX safety net: FL relies on the MIDI Out's own note offs; "Send all notes off" stays FL's CC 123 bursts,
// requestTransportNotesOff / sendAllNotesOff)
void MidiSyncIo::releasePortNotes (Slot& slot)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (int key = 0; key < 128; ++key)
        for (int ch = 0; slot.held[(std::size_t) key] != 0 && ch < 16; ++ch)
            if ((slot.held[(std::size_t) key] >> ch & 1u) != 0)
            {
                const std::uint8_t off[3] { (std::uint8_t) (0x80 | ch), (std::uint8_t) key, 0 };
                sendNow (slot, off, 3, now);
            }
}

void MidiSyncIo::sendAllNotesOff()
{
    const juce::ScopedLock lock (outputLock);
    for (auto& s : slots)
        if (s.notesOff && s.device != nullptr)
            for (int ch = 1; ch <= 16; ++ch)
            {
                s.device->sendMessageNow (juce::MidiMessage::controllerEvent (ch, 64, 0));
                s.device->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
            }
}

void MidiSyncIo::requestTransportNotesOff() noexcept
{
    if (anyNotesOff.load (std::memory_order_relaxed))
    {
        notesOffRequests.fetch_add (1, std::memory_order_release);
        notify();
    }
}

void MidiSyncIo::sendTransportNotesOff()
{
    const auto requested = notesOffRequests.load (std::memory_order_acquire);
    if (requested == notesOffHandled)
        return;
    notesOffHandled = requested;
    const juce::ScopedLock lock (outputLock);
    for (auto& s : slots)
        if (s.notesOff && (s.device != nullptr || dry.load (std::memory_order_relaxed)))
        {
            if (s.device != nullptr)
            {
                std::array<std::uint8_t, 48> bytes {};
                midisync::transportNotesOff (bytes);
                for (int ch = 0; ch < 16; ++ch)
                    s.device->sendMessageNow (juce::MidiMessage (bytes.data() + ch * 3, 3));
            }
            transportNotesOffCount.fetch_add (1, std::memory_order_relaxed);
        }
}

void MidiSyncIo::deviceStarted (double rate, int bufferSize, int outputLatencySamples) noexcept
{
    sampleRate = rate > 0.0 ? rate : 44100.0;
    // the audio of a block computed now is heard one buffer plus the device's latency later: the sync messages for
    // its samples are due then
    latencyMs = 1000.0 * (bufferSize + std::max (0, outputLatencySamples)) / sampleRate;
    predictedMs = -1.0;
    blockSample = nextBlockSample = 0;
    for (auto& s : slots)
        s.generator.reset();
}

void MidiSyncIo::beginBlock (Transport& transport, double callbackMs, int numSamples) noexcept
{
    // the block clock: callbacks wake up with jitter, the samples don't. Follow the sample count and pull towards
    // the callback times slowly; resynchronise after a gap (xrun, device restart).
    if (predictedMs < 0.0 || std::abs (callbackMs - predictedMs) > 50.0)
        blockStartMs = callbackMs;
    else
        blockStartMs = predictedMs + 0.02 * (callbackMs - predictedMs);
    predictedMs = blockStartMs + 1000.0 * numSamples / sampleRate;
    blockSample = nextBlockSample;
    nextBlockSample += std::max (0, numSamples);

    // round 190jl: notes off asked for (stop, panic): in each port device's queue, after what was already played
    if (const auto asked = portNotesOffRequests.load (std::memory_order_acquire); asked != portNotesOffSeen)
    {
        portNotesOffSeen = asked;
        for (auto& slot : slots)
            if (slot.port.load (std::memory_order_relaxed) >= 0)
            {
                Timed t;
                t.dueMs = blockStartMs + heardLatencyMs(); // (after the notes queued with the same delay)
                t.message.size = 0; // (the marker)
                if (! slot.portQueue.push (t))
                    droppedCount.fetch_add (1, std::memory_order_relaxed);
            }
    }

    if (! following.load (std::memory_order_relaxed))
    {
        midisync::Receiver::Command stale;
        double at = 0.0;
        while (clockIn.popCommand (stale, at)) // (nothing old is applied when following starts again)
        {
        }
        if (wasFollowing)
            transport.externalBpm.store (0.0, std::memory_order_relaxed);
        wasFollowing = false;
        waitingFirstPulse = false;
        appliedBpm.store (0.0, std::memory_order_relaxed);
        return;
    }
    wasFollowing = true;

    midisync::Receiver::Command command;
    double ticks = 0.0;
    while (clockIn.popCommand (command, ticks))
        switch (command)
        {
            case midisync::Receiver::Command::start:
            case midisync::Receiver::Command::resume:
                transport.playing.store (false, std::memory_order_relaxed);
                transport.positionTicks.store (ticks, std::memory_order_relaxed);
                waitingFirstPulse = true;
                break;
            case midisync::Receiver::Command::stop:
                transport.playing.store (false, std::memory_order_relaxed);
                waitingFirstPulse = false;
                break;
            case midisync::Receiver::Command::locate:
                if (! transport.playing.load (std::memory_order_relaxed))
                    transport.positionTicks.store (ticks, std::memory_order_relaxed);
                break;
            case midisync::Receiver::Command::none: break;
        }

    if (waitingFirstPulse && clockIn.running())
    {
        waitingFirstPulse = false;
        transport.playing.store (true, std::memory_order_relaxed);
    }

    const auto nowSeconds = blockStartMs * 0.001;
    const auto bpm = clockIn.receiving (nowSeconds) ? clockIn.bpm() : 0.0;
    auto applied = bpm;
    if (bpm > 0.0 && clockIn.running() && transport.playing.load (std::memory_order_relaxed))
    {
        // phase lock: the block's first sample is heard `latencyMs` from now; it should then be where the master is
        // (plus the user's offset: positive = LYNX later)
        // (round 190hq: heard after the rack's delay compensation too)
        const auto target = clockIn.ticksAt (nowSeconds + (heardLatencyMs() - clockOffsetMs.load (std::memory_order_relaxed)) * 0.001);
        const auto position = transport.positionTicks.load (std::memory_order_relaxed);
        const auto error = target - position;
        if (std::abs (error) > 480.0) // half a beat off (a jump on the master's side): relocate
            transport.positionTicks.store (target, std::memory_order_relaxed);
        else // pull in within ~0.5 s, at most ±5 % of the tempo
            applied = bpm * std::clamp (1.0 + error / (bpm * 16.0 * 0.5), 0.95, 1.05);
    }
    transport.externalBpm.store (applied, std::memory_order_relaxed);
    appliedBpm.store (bpm, std::memory_order_relaxed);
}

void MidiSyncIo::subBlock (double startTicks, double ticksPerSample, int numSamples, int offset, bool playing) noexcept
{
    if (! anySync.load (std::memory_order_relaxed))
        return;
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (auto& slot : slots)
    {
        if (! slot.active.load (std::memory_order_relaxed))
            continue;
        if (const auto g = slot.generation.load (std::memory_order_acquire); g != slot.seenGeneration)
        {
            slot.seenGeneration = g;
            slot.generator.reset();
            slot.generator.setType ((midisync::SyncType) slot.type.load (std::memory_order_relaxed));
        }
        midisync::Block block;
        block.startTicks = startTicks;
        block.ticksPerSample = ticksPerSample;
        block.sampleRate = sampleRate;
        block.numSamples = numSamples;
        block.playing = playing;
        scratch.clear();
        const auto songSeconds = ticksPerSample > 0.0 ? startTicks / (ticksPerSample * sampleRate) : 0.0;
        slot.generator.process (block, songSeconds, scratch);
        const auto base = blockStartMs + heardLatencyMs() + slot.offsetMs.load (std::memory_order_relaxed);
        for (int i = 0; i < scratch.count; ++i)
        {
            const auto& m = scratch.items[(std::size_t) i];
            if (! slot.queue.push ({ base + 1000.0 * (offset + m.sample) / sampleRate, m }))
                droppedCount.fetch_add (1, std::memory_order_relaxed);
        }
    }
    const auto us = 1.0e6 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    atomicAdd (blockSum, us);
    atomicMax (blockMax, us);
    blockN.fetch_add (1, std::memory_order_relaxed);
}

void MidiSyncIo::run()
{
#if JUCE_WINDOWS
    timeBeginPeriod (1); // 1 ms sleeps for this thread's waits
#endif
    std::array<std::deque<Timed>, maxOutputs> pending, pendingPort;
    while (! threadShouldExit())
    {
        sendTransportNotesOff();
        // round 190jl: a device that moved to another port (or whose slot has another device now)
        if (const auto release = portReleaseSlots.exchange (0); release != 0)
        {
            const juce::ScopedLock lock (outputLock);
            for (int i = 0; i < maxOutputs; ++i)
                if ((release >> i & 1u) != 0)
                    releasePortNotes (slots[(std::size_t) i]);
        }
        auto any = false;
        for (int i = 0; i < maxOutputs; ++i)
        {
            auto& slot = slots[(std::size_t) i];
            if (const auto opened = slot.opened.load (std::memory_order_acquire); opened != slot.seenOpened)
            {
                slot.seenOpened = opened; // (another device: the old one's notes are not this one's)
                slot.held.fill (0);
            }
            Timed t;
            while (slot.queue.pop (t))
                pending[(std::size_t) i].push_back (t);
            while (slot.portQueue.pop (t))
                pendingPort[(std::size_t) i].push_back (t);
            any = any || ! pending[(std::size_t) i].empty() || ! pendingPort[(std::size_t) i].empty();
        }
        if (! any)
        {
            if (anySync.load())
                std::this_thread::sleep_for (std::chrono::microseconds (500));
            else if (anyPorts.load()) // (the port messages are due a buffer after they come: a 1 ms look is in time)
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            else
                wait (20); // (requestTransportNotesOff wakes it)
            continue;
        }

        auto now = juce::Time::getMillisecondCounterHiRes();
        auto next = now + 1000.0;
        {
            const juce::ScopedLock lock (outputLock);
            for (int i = 0; i < maxOutputs; ++i)
            {
                auto& slot = slots[(std::size_t) i];
                for (auto* queue : { &pending[(std::size_t) i], &pendingPort[(std::size_t) i] })
                {
                    while (! queue->empty() && queue->front().dueMs <= now + 0.05)
                    {
                        const auto& t = queue->front();
                        if (t.message.size == 0)
                            releasePortNotes (slot); // (a stop / panic marker)
                        else
                            sendNow (slot, t.message.bytes.data(), (int) t.message.size, t.dueMs, queue == &pendingPort[(std::size_t) i] ? t.message.sample : -1);
                        queue->pop_front();
                        now = juce::Time::getMillisecondCounterHiRes();
                    }
                    if (! queue->empty())
                        next = std::min (next, queue->front().dueMs);
                }
            }
        }
        // sleep until just before the next message, at most 1 ms (new messages arrive from the audio thread every
        // block), then in short steps (FL's sync thread polls with Sleep (1); REA 165f)
        const auto untilNext = next - juce::Time::getMillisecondCounterHiRes();
        if (untilNext > 0.3)
            std::this_thread::sleep_for (std::chrono::microseconds ((int) (std::min (untilNext - 0.25, 1.0) * 1000.0)));
        else if (untilNext > 0.02)
            std::this_thread::sleep_for (std::chrono::microseconds (50));
    }
#if JUCE_WINDOWS
    timeEndPeriod (1);
#endif
}

bool MidiSyncIo::handleInput (const juce::String& device, const juce::MidiMessage& m) noexcept
{
    if (! following.load (std::memory_order_relaxed))
        return false;
    {
        const juce::SpinLock::ScopedLockType lock (clockNameLock);
        if (device != clockName)
            return false;
    }
    if (resetReceiver.exchange (false))
        clockIn.reset();
    // (JUCE time-stamps input in seconds on Time::getMillisecondCounterHiRes()'s clock)
    return clockIn.feed (m.getRawData(), m.getRawDataSize(), m.getTimeStamp());
}

std::string MidiSyncIo::clockStatus() const
{
    return midisync::statusText (clockIn, juce::Time::getMillisecondCounterHiRes() * 0.001);
}

double MidiSyncIo::followedBpm() const noexcept
{
    return appliedBpm.load (std::memory_order_relaxed);
}

MidiSyncIo::Stats MidiSyncIo::stats() const
{
    // the last second's numbers (read and reset at most once a second)
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastStatsMs < 1000.0)
        return lastStats;
    lastStatsMs = now;
    auto& self = const_cast<MidiSyncIo&> (*this);
    Stats s;
    s.sent = sentCount.load();
    s.dropped = droppedCount.load();
    const auto ln = self.lateN.exchange (0);
    s.meanLateMs = ln > 0 ? self.lateSum.exchange (0.0) / (double) ln : 0.0;
    s.maxLateMs = self.lateMax.exchange (0.0);
    const auto bn = self.blockN.exchange (0);
    s.meanBlockUs = bn > 0 ? self.blockSum.exchange (0.0) / (double) bn : 0.0;
    s.maxBlockUs = self.blockMax.exchange (0.0);
    lastStats = s;
    return s;
}
// Round 190jl: the output devices' ports through the sender thread, dry run (no device opened): a message on a port
// reaches every device with that port at its due time (the block's start + one buffer + the device latency + its
// sample; the master sync offset is the clock's alone, as in FL), never another port's device, never the device whose
// own input played it; an output without a port takes its input's (FL); a stop / panic ends the notes each device still
// sounds; a device moved to another port ends what it played on the old one.
juce::String MidiSyncIo::portSelfTest (bool& ok)
{
    juce::String report;
    ok = true;
    auto fail = [&] (const juce::String& why) {
        ok = false;
        report << "FAIL " << why << "\n";
    };
    struct Sent
    {
        std::string device;
        std::vector<std::uint8_t> bytes;
        double due = 0.0, at = 0.0;
    };
    std::mutex lock;
    std::vector<Sent> sent;
    MidiSyncIo io;
    io.testSink = [&] (const std::string& device, const std::uint8_t* bytes, int size, double due, double at, std::int64_t) {
        const std::lock_guard<std::mutex> g (lock);
        sent.push_back ({ device, { bytes, bytes + size }, due, at });
    };
    prefs::MidiPrefs p;
    auto add = [&p] (const char* name, int port, bool notesOff, int offsetMs) {
        prefs::MidiOutputPrefs o;
        o.name = name;
        o.port = port;
        o.allNotesOff = notesOff;
        o.offsetMs = offsetMs;
        p.setOutput (o);
    };
    add ("Synth A", 1, true, 0);
    add ("Synth B", 2, false, 0);
    add ("Pad C MIDI 1", 1, false, 10); // (its input is source 1: its own notes don't come back to it)
    add ("No port", -1, true, 0);
    add ("Keys MIDI Out", -1, true, 0); // (no port of its own: its input "Keys MIDI In" has port 4)
    prefs::MidiInputPrefs keys;
    keys.name = "Keys MIDI In";
    keys.port = 4;
    p.setInput (keys);
    io.configure (p, false, true, { "Pad C MIDI 1", "Keys MIDI In" });
    const auto bits = io.hardwarePorts();
    if (bits[0] != 0b10110 || bits[1] != 0 || bits[2] != 0 || bits[3] != 0)
        fail ("hardware ports " + juce::String ((juce::int64) bits[0]));

    Transport transport;
    io.deviceStarted (48000.0, 256, 0);
    const auto t0 = juce::Time::getMillisecondCounterHiRes() + 5.0;
    io.beginBlock (transport, t0, 256);
    const std::uint8_t on[3] { 0x90, 60, 100 }, onB[3] { 0x91, 61, 90 }, cc[3] { 0xB0, 7, 100 }, nowhere[3] { 0x92, 62, 80 };
    io.channelMessage (1, 100, on, 3, 0);       // → A and C
    io.channelMessage (2, 0, onB, 3, 0);        // → B
    io.channelMessage (1, 10, cc, 3, 1);        // → A only (C's own input)
    io.channelMessage (3, 0, nowhere, 3, 0);    // → nobody
    const std::uint8_t keyOn[3] { 0x93, 50, 70 }, keyOwn[3] { 0x93, 51, 70 };
    io.channelMessage (4, 0, keyOn, 3, 0);      // → Keys (the port its input gave it)
    io.channelMessage (4, 0, keyOwn, 3, 2);     // → nobody (Keys' own input)
    std::this_thread::sleep_for (std::chrono::milliseconds (80));
    const auto latency = 1000.0 * 256 / 48000.0;
    auto find = [&] (const std::string& device, std::vector<std::uint8_t> bytes) -> const Sent* {
        for (const auto& m : sent)
            if (m.device == device && m.bytes == bytes)
                return &m;
        return nullptr;
    };
    {
        const std::lock_guard<std::mutex> g (lock);
        const auto* a = find ("Synth A", { 0x90, 60, 100 });
        const auto* c = find ("Pad C MIDI 1", { 0x90, 60, 100 });
        const auto* b = find ("Synth B", { 0x91, 61, 90 });
        const auto* aCc = find ("Synth A", { 0xB0, 7, 100 });
        if (a == nullptr || c == nullptr || b == nullptr || aCc == nullptr)
            fail ("a port message did not reach its device");
        else
        {
            const auto dueA = t0 + latency + 1000.0 * 100 / 48000.0;
            // (the 10 ms master sync offset of C does not move its notes: FL applies it to the clock only)
            if (std::abs (a->due - dueA) > 1.0e-6 || std::abs (c->due - dueA) > 1.0e-6 || std::abs (b->due - (t0 + latency)) > 1.0e-6)
                fail ("due times " + juce::String (a->due - t0, 4) + " / " + juce::String (c->due - t0, 4) + " / " + juce::String (b->due - t0, 4));
            report << "port 1 note: due +" << juce::String (a->due - t0, 3) << " ms (expected " << juce::String (dueA - t0, 3) << "), sent "
                   << juce::String (a->at - a->due, 3) << " ms after it\n";
            if (a->at < a->due - 0.1 || c->at < c->due - 0.1)
                fail ("sent early");
        }
        if (find ("Pad C MIDI 1", { 0xB0, 7, 100 }) != nullptr || find ("Keys MIDI Out", { 0x93, 51, 70 }) != nullptr)
            fail ("the device's own input came back to it");
        if (find ("Keys MIDI Out", { 0x93, 50, 70 }) == nullptr)
            fail ("the output did not take its input's port");
        for (const auto& m : sent)
            if (m.bytes == std::vector<std::uint8_t> { 0x92, 62, 80 } || m.device == "No port" || (m.device == "Synth B" && m.bytes[0] != 0x91))
                fail ("a message reached a device on another port: " + juce::String (m.device));
        report << "messages sent: " << (int) sent.size() << " (expected 5)\n";
        if (sent.size() != 5)
            fail ("message count");
        sent.clear();
    }

    // stop / panic: the notes each device still sounds end, after what was already queued
    io.requestPortNotesOff();
    io.beginBlock (transport, juce::Time::getMillisecondCounterHiRes(), 256);
    std::this_thread::sleep_for (std::chrono::milliseconds (80));
    {
        const std::lock_guard<std::mutex> g (lock);
        auto count = [&] (const std::string& device) { return (int) std::count_if (sent.begin(), sent.end(), [&] (const Sent& m) { return m.device == device; }); };
        const auto aOff = find ("Synth A", { 0x80, 60, 0 }) != nullptr, bOff = find ("Synth B", { 0x81, 61, 0 }) != nullptr,
                   cOff = find ("Pad C MIDI 1", { 0x80, 60, 0 }) != nullptr, kOff = find ("Keys MIDI Out", { 0x83, 50, 0 }) != nullptr;
        report << "notes off: A " << count ("Synth A") << " messages, B " << count ("Synth B") << ", C " << count ("Pad C MIDI 1") << ", Keys "
               << count ("Keys MIDI Out") << "\n";
        if (! aOff || ! bOff || ! cOff || ! kOff || sent.size() != 4)
            fail ("notes off on stop");
        sent.clear();
    }
    // a second stop: nothing sounds any more
    io.requestPortNotesOff();
    io.beginBlock (transport, juce::Time::getMillisecondCounterHiRes(), 256);
    std::this_thread::sleep_for (std::chrono::milliseconds (60));
    {
        const std::lock_guard<std::mutex> g (lock);
        if (! sent.empty())
            fail ("second stop sent " + juce::String ((int) sent.size()));
    }
    // a port moved: what the device played on the old one ends
    {
        const std::lock_guard<std::mutex> g (lock);
        sent.clear();
    }
    io.beginBlock (transport, juce::Time::getMillisecondCounterHiRes(), 256);
    io.channelMessage (2, 0, onB, 3, 0);
    std::this_thread::sleep_for (std::chrono::milliseconds (40));
    add ("Synth B", 5, false, 0);
    io.configure (p, false, true, { "Pad C MIDI 1", "Keys MIDI In" });
    std::this_thread::sleep_for (std::chrono::milliseconds (60));
    {
        const std::lock_guard<std::mutex> g (lock);
        if (find ("Synth B", { 0x91, 61, 90 }) == nullptr || find ("Synth B", { 0x81, 61, 0 }) == nullptr)
            fail ("a port change left a note on");
    }
    // rollup 0.1.30: behind latent plugins the notes go out when their audio is heard, after the rack's delay
    // compensation as well (round 190hq's heardLatencyMs, which the clock uses), and a stop's notes off after them
    {
        const std::lock_guard<std::mutex> g (lock);
        sent.clear();
    }
    io.setRackLatencyMs (20.0);
    const auto t1 = juce::Time::getMillisecondCounterHiRes() + 5.0;
    io.beginBlock (transport, t1, 256);
    const std::uint8_t onA[3] { 0x90, 64, 100 };
    io.channelMessage (1, 48, onA, 3, 0);
    io.requestPortNotesOff();
    io.beginBlock (transport, t1, 256);
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    {
        const std::lock_guard<std::mutex> g (lock);
        const auto* a = find ("Synth A", { 0x90, 64, 100 });
        const auto* off = find ("Synth A", { 0x80, 64, 0 });
        const auto due = t1 + latency + 20.0 + 1000.0 * 48 / 48000.0;
        if (a == nullptr || std::abs (a->due - due) > 1.0e-6)
            fail ("due time behind latent plugins " + juce::String (a != nullptr ? a->due - t1 : -1.0, 4));
        else if (off == nullptr || off->at < a->at)
            fail ("notes off before the note behind latent plugins");
        else
            report << "behind 20 ms of plugin delay compensation: due +" << juce::String (a->due - t1, 3) << " ms (expected "
                   << juce::String (due - t1, 3) << ")\n";
    }
    io.setRackLatencyMs (0.0);
    io.stopThread (1000);
    report << (ok ? "OK" : "FAILED") << "\n";
    return report;
}

juce::String MidiSyncIo::selfTest (const juce::String& outputName, const juce::String& inputName)
{
    juce::String report;
    // 1. in memory: the generator's clock (time-stamped with ±0.5 ms of jitter) followed by a receiver
    {
        std::mt19937 random (165);
        std::uniform_real_distribution<double> jitter (-0.0005, 0.0005);
        for (const auto bpm : { 90.0, 120.0, 174.0 })
        {
            midisync::Generator g;
            midisync::Receiver r;
            const double rate = 48000.0, tps = bpm * 960.0 / (60.0 * rate);
            double ticks = 0.0;
            std::int64_t sample = 0;
            midisync::BlockMessages out;
            for (int b = 0; b < 48000 * 8 / 256; ++b)
            {
                midisync::Block block;
                block.startTicks = ticks;
                block.ticksPerSample = tps;
                block.sampleRate = rate;
                block.numSamples = 256;
                block.playing = true;
                out.clear();
                g.process (block, 0.0, out);
                for (int i = 0; i < out.count; ++i)
                    r.feed (out.items[(std::size_t) i].bytes.data(), out.items[(std::size_t) i].size,
                            (double) (sample + out.items[(std::size_t) i].sample) / rate + jitter (random));
                ticks += 256 * tps;
                sample += 256;
            }
            const auto positionError = r.ticksAt ((double) sample / rate) - ticks;
            report << "loopback bpm=" << bpm << " received=" << juce::String (r.bpm(), 3) << " error=" << juce::String (r.bpm() - bpm, 3)
                   << " position error ticks=" << juce::String (positionError, 2) << " running=" << (r.running() ? 1 : 0) << "\n";
        }
    }

    // 1b. "Reset plugins on transport": a position change sends the CC 123 burst through the sender thread
    {
        MidiSyncIo off;
        prefs::MidiPrefs p;
        prefs::MidiOutputPrefs o;
        o.name = "(dry run)";
        o.allNotesOff = true;
        p.setOutput (o);
        off.configure (p, false, true);
        off.requestTransportNotesOff();
        for (int i = 0; i < 200 && off.transportNotesOffSent() == 0; ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
        report << "transport notes off: bursts=" << (juce::int64) off.transportNotesOffSent() << " (expected 1)\n";
    }

    // 2. the sender thread at real time
    MidiSyncIo io;
    prefs::MidiPrefs p;
    prefs::MidiOutputPrefs o;
    o.name = outputName.isNotEmpty() ? outputName.toStdString() : std::string ("(dry run)");
    o.masterSync = true;
    p.setOutput (o);
    const auto real = outputName.isNotEmpty();
    std::vector<double> lates;
    lates.reserve (4096);
    io.lateLog = &lates;
    io.configure (p, real, ! real);
    report << "output=" << (real ? outputName : juce::String ("(dry run, timed without a device)")) << " opened=" << (io.sending() ? 1 : 0) << "\n";
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        report << "  midi-out=" << d.name << "\n";
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        report << "  midi-in=" << d.name << "\n";

    // the input side of a loopback: a receiver fed from the input callback
    struct In final : juce::MidiInputCallback
    {
        midisync::Receiver receiver;
        std::vector<double> times;
        std::mutex lock;
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            receiver.feed (m.getRawData(), m.getRawDataSize(), m.getTimeStamp());
            if (m.getRawDataSize() > 0 && m.getRawData()[0] == 0xf8)
            {
                const std::lock_guard<std::mutex> g (lock);
                times.push_back (juce::Time::getMillisecondCounterHiRes());
            }
        }
    } in;
    std::unique_ptr<juce::MidiInput> input;
    if (inputName.isNotEmpty())
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            if (d.name == inputName)
                input = juce::MidiInput::openDevice (d.identifier, &in);
    if (input != nullptr)
        input->start();

    if (io.sending())
    {
        Transport transport;
        transport.sampleRate.store (48000.0);
        transport.bpm.store (120.0);
        transport.playing.store (true);
        io.deviceStarted (48000.0, 256, 0);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < 48000 * 5 / 256; ++b)
        {
            // wake up like an audio callback (with the scheduler's jitter), on the block's due time
            const auto due = start + b * 256.0 * 1000.0 / 48000.0;
            while (juce::Time::getMillisecondCounterHiRes() < due)
                std::this_thread::sleep_for (std::chrono::microseconds (200));
            io.beginBlock (transport, juce::Time::getMillisecondCounterHiRes(), 256);
            const auto tps = transport.ticksPerSample();
            io.subBlock (transport.positionTicks.load(), tps, 256, 0, true);
            transport.positionTicks.store (transport.positionTicks.load() + 256 * tps);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
        io.lastStatsMs = 0.0;
        const auto s = io.stats();
        report << "sender: sent=" << (juce::int64) s.sent << " dropped=" << (juce::int64) s.dropped << " late mean ms=" << juce::String (s.meanLateMs, 3)
               << " max ms=" << juce::String (s.maxLateMs, 3) << " (5 s)\n";
        io.stopThread (1000);
        if (! lates.empty())
        {
            std::sort (lates.begin(), lates.end());
            auto pct = [&lates] (double q) { return lates[(std::size_t) std::min ((double) lates.size() - 1, q * (double) lates.size())]; };
            report << "sender late ms: p50=" << juce::String (pct (0.5), 3) << " p95=" << juce::String (pct (0.95), 3)
                   << " p99=" << juce::String (pct (0.99), 3) << "\n";
        }
        report << "audio thread per block: mean us=" << juce::String (s.meanBlockUs, 2) << " max us=" << juce::String (s.maxBlockUs, 2) << "\n";
    }
    if (input != nullptr)
    {
        input->stop();
        const std::lock_guard<std::mutex> g (in.lock);
        double sum = 0.0, sum2 = 0.0;
        const auto n = (int) in.times.size() - 1;
        for (int i = 0; i < n; ++i)
        {
            const auto d = in.times[(std::size_t) i + 1] - in.times[(std::size_t) i];
            sum += d;
            sum2 += d * d;
        }
        const auto mean = n > 0 ? sum / n : 0.0;
        report << "input=" << inputName << " clocks=" << (int) in.times.size() << " interval mean ms=" << juce::String (mean, 3)
               << " sd ms=" << juce::String (n > 0 ? std::sqrt (std::max (0.0, sum2 / n - mean * mean)) : 0.0, 3)
               << " received bpm=" << juce::String (in.receiver.bpm(), 2) << "\n";
    }
    else if (inputName.isNotEmpty())
        report << "input=" << inputName << " not found\n";
    return report;
}
} // namespace lynx
