// Issue #134 ("Switching audio output mid project crashes the DAW"): the audio device changed under a playing project.
//
// AudioEngine::deviceChangeSelfTest() runs the engine as the app does (the device manager, the processor player, the
// engine's callback and its timer's work) on stand-in devices whose callbacks come from a real thread, and switches
// between them the way Settings › Audio does: another output device, another sample rate, another buffer size, a
// device that appears (headphones plugged in), the open device going away, and a device that will not open. The
// devices differ in what a pair of speakers and a headset differ in: 8 kHz mono (a Bluetooth hands-free headset) up
// to 192 kHz with 8 outputs, buffers from 32 to 8192 samples, and callbacks that deliver more samples than the device
// announced. The project plays every instrument and effect LYNX can make on this machine.
//
// After every switch the song must still sound (finite samples, not silence), the transport must run at the new
// device's rate and the engine must have rebuilt its plugins for it.

#include "engine/AudioEngine.h"

#include "engine/ChannelRackProcessor.h"

namespace lynx
{
namespace
{
struct StandIn
{
    const char* name;
    double rate;
    int buffer;
    int outputs;
    int extra;    // samples a callback may deliver beyond the announced buffer (0: never)
    bool plugged; // listed from the start (the others appear later, as headphones being plugged in)
};

constexpr StandIn standIns[] = {
    { "Speakers", 48000.0, 480, 2, 0, true },
    { "Headphones", 44100.0, 1024, 2, 0, false },
    { "Hands-free headset", 8000.0, 80, 1, 0, false },
    { "Headset", 16000.0, 160, 1, 0, false },
    { "Interface", 96000.0, 32, 8, 0, true },
    { "Interface HD", 192000.0, 8192, 2, 0, true },
    { "USB headphones", 44100.0, 441, 2, 448, false }, // (a shared-mode period that is not a power of two, and uneven calls)
    { "Monitor", 22050.0, 4096, 2, 0, true },
};

struct StandInState
{
    juce::CriticalSection lock;
    juce::StringArray plugged;
    juce::String refuses; // a device whose open() fails
    // what the device threads saw since the last reset
    std::atomic<std::int64_t> blocks { 0 }, frames { 0 }, nonFinite { 0 };
    std::atomic<float> peak { 0.0f };
    void reset()
    {
        blocks = 0;
        frames = 0;
        nonFinite = 0;
        peak = 0.0f;
    }
};
StandInState& standInState()
{
    static StandInState state;
    return state;
}

const StandIn* standInNamed (const juce::String& name)
{
    for (const auto& s : standIns)
        if (name == s.name)
            return &s;
    return nullptr;
}

// A device with its own thread: the callbacks come as fast as the engine renders them (no pacing: more switches
// meet a callback in progress), each with the announced size or, for the uneven ones, anything up to `extra`.
struct StandInDevice final : juce::AudioIODevice, private juce::Thread
{
    explicit StandInDevice (const StandIn& s) : juce::AudioIODevice (s.name, "Device change test"), juce::Thread ("stand-in device"), spec (s) {}
    ~StandInDevice() override { close(); }

    juce::StringArray getOutputChannelNames() override
    {
        juce::StringArray names;
        for (int c = 0; c < spec.outputs; ++c)
            names.add ("Out " + juce::String (c + 1));
        return names;
    }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { spec.rate, spec.rate * 2.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { spec.buffer, spec.buffer * 2 }; }
    int getDefaultBufferSize() override { return spec.buffer; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger& outs, double rate, int size) override
    {
        close();
        if (standInState().refuses == spec.name)
            return lastError = "the stand-in refuses to open";
        lastError = {};
        rateNow = rate > 0.0 ? rate : spec.rate;
        sizeNow = size > 0 ? size : spec.buffer;
        outsNow = outs;
        outsNow.setRange (spec.outputs, juce::jmax (0, outsNow.getHighestBit() + 1 - spec.outputs), false);
        const auto longest = sizeNow + spec.extra;
        data.setSize (juce::jmax (1, outsNow.countNumberOfSetBits()), longest);
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
        cb->audioDeviceAboutToStart (this); // (as JUCE's own devices: before the callbacks begin)
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
    juce::String getLastError() override { return lastError; }
    int getCurrentBufferSizeSamples() override { return sizeNow; }
    double getCurrentSampleRate() override { return rateNow; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outsNow; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return sizeNow; }
    int getInputLatencyInSamples() override { return 0; }

private:
    void run() override
    {
        auto& state = standInState();
        juce::Random random (juce::String (spec.name).hashCode());
        while (! threadShouldExit())
        {
            {
                const juce::ScopedLock sl (callbackLock);
                if (callback != nullptr)
                {
                    const auto n = spec.extra > 0 ? 1 + random.nextInt (sizeNow + spec.extra) : sizeNow;
                    callback->audioDeviceIOCallbackWithContext (nullptr, 0, data.getArrayOfWritePointers(), data.getNumChannels(), n, {});
                    auto peak = state.peak.load (std::memory_order_relaxed);
                    for (int c = 0; c < data.getNumChannels(); ++c)
                        for (int i = 0; i < n; ++i)
                        {
                            const auto v = data.getSample (c, i);
                            if (! std::isfinite (v))
                                state.nonFinite.fetch_add (1, std::memory_order_relaxed);
                            else
                                peak = juce::jmax (peak, std::abs (v));
                        }
                    state.peak.store (peak, std::memory_order_relaxed);
                    state.frames.fetch_add (n, std::memory_order_relaxed);
                    state.blocks.fetch_add (1, std::memory_order_relaxed);
                }
            }
            juce::Thread::yield();
        }
    }

    const StandIn spec;
    double rateNow = 0.0;
    int sizeNow = 0;
    juce::BigInteger outsNow;
    juce::AudioBuffer<float> data;
    juce::CriticalSection callbackLock;
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::String lastError;
    bool opened = false;
};

struct StandInType final : juce::AudioIODeviceType
{
    StandInType() : juce::AudioIODeviceType ("Device change test") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override
    {
        const juce::ScopedLock sl (standInState().lock);
        return input ? juce::StringArray() : standInState().plugged;
    }
    int getDefaultDeviceIndex (bool input) const override { return input ? -1 : 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool input) const override
    {
        return d != nullptr && ! input ? getDeviceNames (false).indexOf (d->getName()) : -1;
    }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String&) override
    {
        const auto* spec = standInNamed (out);
        return spec != nullptr && getDeviceNames (false).contains (out) ? new StandInDevice (*spec) : nullptr;
    }
    // a device plugged in or removed: the list changes and the listeners hear of it, as from the system
    void setPlugged (const juce::String& name, bool on)
    {
        {
            const juce::ScopedLock sl (standInState().lock);
            auto& list = standInState().plugged;
            if (on)
                list.addIfNotAlreadyThere (name);
            else
                list.removeString (name);
        }
        callDeviceChangeListeners();
    }
};

// Every instrument and effect LYNX can make here, each channel playing a short note per beat into one of the inserts
// that hold the effects (all of them with `everyUnit`; else a handful, for the selftest).
Song testSong (bool everyUnit)
{
    const auto& fl = flStudioAccess();
    auto song = Song::makeDefault();
    song.channels.clear();
    song.patterns.clear();
    auto& pattern = song.patterns.emplace_back();
    pattern.name = "Device change";

    std::vector<EffectSlot> effects;
    for (int k = 0; k <= (int) lastEffectKind; ++k)
    {
        const auto kind = (EffectKind) k;
        if (kind == EffectKind::plugin || kind == EffectKind::patcher || kind == EffectKind::parallel)
            continue;
        if (! everyUnit && kind != EffectKind::eq && kind != EffectKind::compressor && kind != EffectKind::delay && kind != EffectKind::reverb
            && kind != EffectKind::limiter && kind != EffectKind::chorus && kind != EffectKind::pitchShift && kind != EffectKind::facetEq
            && kind != EffectKind::apexLimiter && kind != EffectKind::reeverb2 && kind != EffectKind::fruityDelay3 && kind != EffectKind::maximus)
            continue;
        if (auto slot = natives::newEffect (kind, fl))
            effects.push_back (std::move (*slot));
    }

    constexpr int perInsert = 4;
    const auto inserts = juce::jmax (1, ((int) effects.size() + perInsert - 1) / perInsert);
    song.mixer = Song::defaultMixer (inserts);
    for (std::size_t i = 0; i < effects.size(); ++i)
        song.mixer[1 + i / perInsert].effects.push_back (effects[i]);
    {
        EffectSlot limiter; // (the default project's Master limiter)
        limiter.kind = EffectKind::limiter;
        limiter.params = { 11.7f / 12.0f, 0.0f, 0.5f };
        song.mixer[0].effects.push_back (limiter);
    }

    const InstrumentKind basic[] = { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap, InstrumentKind::synth };
    auto add = [&] (ChannelDef c) {
        const auto index = (int) song.channels.size();
        c.mixerTrack = 1 + index % inserts;
        c.volume = 0.6f;
        song.channels.push_back (std::move (c));
        for (int beat = 0; beat < 4; ++beat)
        {
            Note n;
            n.channel = index;
            n.key = 48 + (index * 7) % 24;
            n.start = (std::int64_t) beat * ppq + (std::int64_t) (index % 4) * (ppq / 4);
            n.length = ppq / 2;
            n.velocity = 0.8f;
            pattern.notes.push_back (n);
        }
    };
    for (const auto kind : basic)
    {
        ChannelDef c;
        c.instrument = kind;
        c.name = "basic";
        add (std::move (c));
    }
    for (const auto& engine : natives::instrumentEngines())
    {
        if (! everyUnit && engine.kind != InstrumentKind::threeosc && engine.kind != InstrumentKind::plucked && engine.kind != InstrumentKind::dx10
            && engine.kind != InstrumentKind::harmless && engine.kind != InstrumentKind::minisynth)
            continue;
        if (engine.kind == InstrumentKind::plugin || engine.kind == InstrumentKind::patcher || engine.kind == InstrumentKind::videoplayer
            || engine.kind == InstrumentKind::fruitydance)
            continue;
        if (auto c = natives::makeChannel (engine.kind, fl))
            add (std::move (*c));
    }
    pattern.sortNotes();
    song.mode = PlayMode::pattern;
    song.currentPattern = 0;
    return song;
}
} // namespace

juce::String AudioEngine::deviceChangeSelfTest (int switches, bool everyUnit, const juce::StringArray& pluginFiles,
                                                const std::function<void (const juce::String&)>& log)
{
    auto say = [&log] (const juce::String& line) {
        if (log)
            log (line);
    };
    auto& state = standInState();
    {
        const juce::ScopedLock sl (state.lock);
        state.plugged.clear();
        for (const auto& s : standIns)
            if (s.plugged)
                state.plugged.add (s.name);
        state.refuses = {};
    }
    state.reset();

    AudioEngine engine (false);
    auto typeOwner = std::make_unique<StandInType>();
    auto* type = typeOwner.get();
    auto& devices = engine.deviceManager;
    // what the engine's constructor does for the app, on the stand-in devices instead of the machine's
    engine.player.setProcessor (&engine.graph);
    devices.addAudioDeviceType (std::move (typeOwner));
    if (const auto error = devices.initialise (0, 2, nullptr, true); error.isNotEmpty())
        return "ERROR open: " + error;
    devices.addAudioCallback (&engine);
    if (engine.rackProcessor != nullptr)
        engine.rackProcessor->getRenderer().setProfiling (true);

    auto song = testSong (everyUnit);
    // hosted plugins (--devicechangetest --plugin file): the first instrument and the first effect of each file
    int hostedInstruments = 0, hostedEffects = 0;
    for (const auto& path : pluginFiles)
    {
        engine.plugins.addPluginFile (juce::File (path));
        bool gotInstrument = false, gotEffect = false;
        for (const auto& t : engine.plugins.getKnownPlugins().getTypes())
        {
            if (juce::File (t.fileOrIdentifier).getFileName() != juce::File (path).getFileName())
                continue;
            if (t.isInstrument && ! gotInstrument)
            {
                gotInstrument = true;
                ChannelDef c;
                c.instrument = InstrumentKind::plugin;
                c.plugin.id = t.createIdentifierString().toStdString();
                c.plugin.name = t.name.toStdString();
                c.name = c.plugin.name;
                c.mixerTrack = 1;
                const auto index = (int) song.channels.size();
                song.channels.push_back (c);
                for (int beat = 0; beat < 4; ++beat)
                    song.patterns[0].notes.push_back ({ (std::int64_t) beat * ppq, ppq / 2, index, 60, 0.8f });
                ++hostedInstruments;
            }
            if (! t.isInstrument && ! gotEffect)
            {
                gotEffect = true;
                EffectSlot slot;
                slot.kind = EffectKind::plugin;
                slot.plugin.id = t.createIdentifierString().toStdString();
                slot.plugin.name = t.name.toStdString();
                song.mixer[1].effects.push_back (slot);
                ++hostedEffects;
            }
        }
    }
    // two instruments and an effect whose plugin is not installed here: the rack asks the plugin host for them at
    // every rate (as for installed ones), which is where `duringPluginCreation` stands in for a plugin that runs the
    // message loop while it is created (a licence box, COM, a splash window): the engine's timer runs inside it
    constexpr int askedPlugins = 3;
    for (int k = 0; k < 2; ++k)
    {
        ChannelDef c;
        c.instrument = InstrumentKind::plugin;
        c.plugin.id = "VST3-NotInstalled-" + std::to_string (k) + "-0-0";
        c.plugin.name = "Not installed " + std::to_string (k);
        c.name = c.plugin.name;
        c.mixerTrack = 1;
        song.channels.push_back (c);
    }
    {
        EffectSlot slot;
        slot.kind = EffectKind::plugin;
        slot.plugin.id = "VST3-NotInstalledFx-0-0";
        slot.plugin.name = "Not installed effect";
        song.mixer[2 < (int) song.mixer.size() ? 2 : 1].effects.push_back (slot);
    }
    struct Creations
    {
        int total = 0, depth = 0, deepest = 0;
    } creations;
    engine.duringPluginCreation = [&engine, &creations] {
        ++creations.total;
        creations.deepest = std::max (creations.deepest, ++creations.depth);
        if (creations.depth <= 8) // (a real plugin nests as deep as the stack allows; 8 shows it)
            engine.timerCallback(); // the message loop ran: the timer's tick, inside the creation
        --creations.depth;
    };
    song.patterns[0].sortNotes();
    int nativeEffects = 0;
    for (const auto& t : song.mixer)
        nativeEffects += (int) t.effects.size();
    say ("project: " + juce::String ((int) song.channels.size()) + " channels, " + juce::String (nativeEffects) + " effect slots on "
         + juce::String ((int) song.mixer.size() - 1) + " inserts (" + juce::String (hostedInstruments) + " hosted instruments, "
         + juce::String (hostedEffects) + " hosted effects)");
    engine.setSong (std::move (song));

    // the message loop's part while the device threads run: the engine's timer (it rebuilds the rack for a new rate)
    // (`seconds` of audio through the device, at its rate; returns what came)
    auto settle = [&engine, &state] (double seconds, int maxMs) {
        const auto start = juce::Time::getMillisecondCounter();
        const auto from = state.frames.load();
        auto played = [&] { return (double) (state.frames.load() - from) / juce::jmax (1.0, engine.transport.sampleRate.load()); };
        while (juce::Time::getMillisecondCounter() - start < (juce::uint32) maxMs)
        {
            engine.timerCallback();
            engine.rackDisposer.flush();
            if (played() >= seconds && std::abs (engine.transport.sampleRate.load() - engine.lastPublishedShape.sampleRate) < 0.5)
                break;
            juce::Thread::sleep (2);
        }
        return played();
    };

    juce::String failures;
    int done = 0;
    auto builtRate = engine.transport.sampleRate.load(); // the rate the plugins were last made for
    auto fail = [&] (const juce::String& what) {
        failures << (failures.isEmpty() ? "" : "; ") << what;
        say ("  FAILED: " + what);
    };
    // the engine after a change: running at the device's rate, rebuilt for it, sounding, finite
    auto check = [&] (const juce::String& what, bool expectSound) {
        state.reset();
        const auto crashesBefore = pluginCrashCount();
        const auto nonFiniteBefore = RackRenderer::nonFiniteEffectBlocks();
        constexpr double wanted = 0.75; // (more than a beat: every channel played)
        const auto ran = settle (wanted, everyUnit ? 120000 : 30000);
        engine.timerCallback();
        auto* device = devices.getCurrentAudioDevice();
        juce::String line = what + ": ";
        if (device == nullptr)
        {
            fail (what + ": no device is open");
            return;
        }
        line << device->getName() << " " << device->getCurrentSampleRate() << " Hz, buffer " << device->getCurrentBufferSizeSamples() << ", "
             << device->getActiveOutputChannels().countNumberOfSetBits() << " out; " << (juce::int64) state.blocks.load() << " callbacks, "
             << juce::String (ran, 2) << " s, peak "
             << juce::String (state.peak.load(), 4);
        if (const auto muted = RackRenderer::nonFiniteEffectBlocks() - nonFiniteBefore; muted > 0)
            line << ", " << (juce::int64) muted << " effect blocks muted for non-finite output";
        say (line);
        if (ran < wanted)
            fail (what + ": only " + juce::String (ran, 3) + " s of audio came (" + juce::String ((juce::int64) state.blocks.load()) + " callbacks)");
        if (std::abs (engine.transport.sampleRate.load() - device->getCurrentSampleRate()) > 0.5)
            fail (what + ": the transport runs at " + juce::String (engine.transport.sampleRate.load()) + " Hz");
        if (std::abs (engine.lastPublishedShape.sampleRate - device->getCurrentSampleRate()) > 0.5)
            fail (what + ": the rack is still built for " + juce::String (engine.lastPublishedShape.sampleRate) + " Hz");
        if (state.nonFinite.load() > 0)
            fail (what + ": " + juce::String ((juce::int64) state.nonFinite.load()) + " non-finite samples went to the device");
        if (expectSound && state.peak.load() < 0.01f)
        {
            // quiet: does the song come back by itself?
            const auto first = state.peak.load();
            const auto positionBefore = engine.transport.positionTicks.load();
            state.reset();
            settle (2.0, everyUnit ? 120000 : 30000);
            say ("    quiet (peak " + juce::String (first, 6) + "); 2 s later: peak " + juce::String (state.peak.load(), 6) + ", playing "
                 + juce::String (engine.transport.playing.load() ? 1 : 0) + ", position " + juce::String (positionBefore, 1) + " -> "
                 + juce::String (engine.transport.positionTicks.load(), 1) + " ticks, non-finite effect blocks "
                 + juce::String ((juce::int64) (RackRenderer::nonFiniteEffectBlocks() - nonFiniteBefore)));
            if (state.peak.load() < 0.01f)
                fail (what + ": the song went silent (peak " + juce::String (state.peak.load(), 6) + ")");
        }
        if (pluginCrashCount() != crashesBefore)
            fail (what + ": a hosted plugin crashed");
        // the plugins were asked for once per rate, never one inside another's creation (a rebuild nested in a rebuild)
        const auto rateNow = device->getCurrentSampleRate();
        const auto expected = std::abs (rateNow - builtRate) > 0.5 ? askedPlugins + hostedInstruments + hostedEffects : 0;
        builtRate = rateNow;
        if (creations.total != expected)
            fail (what + ": the rack asked for " + juce::String (creations.total) + " plugins, expected " + juce::String (expected));
        if (creations.deepest > 1)
            fail (what + ": a plugin was created inside another plugin's creation, " + juce::String (creations.deepest)
                  + " deep (a rack rebuild nested in a rebuild)");
        creations.total = creations.deepest = 0;
        ++done;
    };
    auto choose = [&] (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>& change) {
        // (Settings › Audio: AudioPage::apply)
        auto setup = devices.getAudioDeviceSetup();
        change (setup);
        return devices.setAudioDeviceSetup (setup, true);
    };

    creations.total = creations.deepest = 0; // (the project's own creation)
    engine.play();
    check ("start", true);

    juce::Random random (134);
    juce::StringArray everyDevice;
    for (const auto& s : standIns)
        everyDevice.add (s.name);
    for (int k = 0; k < switches; ++k)
    {
        const auto& spec = standIns[(std::size_t) (k < (int) std::size (standIns) ? (k + 1) % (int) std::size (standIns)
                                                                                 : random.nextInt ((int) std::size (standIns)))];
        const juce::String name (spec.name);
        // the headphones are plugged in while the song plays: the list is refreshed, then they are chosen
        if (! type->getDeviceNames (false).contains (name))
        {
            type->setPlugged (name, true);
            engine.timerCallback();
        }
        // now and then stopped (a project that is only open), edited, or mid-way through a rack rebuild
        const auto mode = k % 5;
        if (mode == 3)
            engine.stop();
        if (mode == 4)
            engine.editSong ([] (Song& s) { s.bpm = s.bpm > 150.0 ? 120.0 : s.bpm + 7.0; }, false);
        if (const auto error = choose ([&name] (auto& s) { s.outputDeviceName = name; }); error.isNotEmpty())
            fail ("switch to " + name + ": " + error);
        if (mode == 3)
        {
            settle (0.05, 5000);
            engine.play();
        }
        check ("switch " + juce::String (k + 1) + " to " + name, true);

        // the same device at its other rate / buffer (the Sample rate and Buffer boxes)
        if (k % 3 == 1)
        {
            if (const auto error = choose ([&spec] (auto& s) { s.sampleRate = spec.rate * 2.0; }); error.isNotEmpty())
                fail ("rate of " + name + ": " + error);
            check ("  rate x2", true);
        }
        if (k % 3 == 2)
        {
            if (const auto error = choose ([&spec] (auto& s) { s.bufferSize = spec.buffer * 2; }); error.isNotEmpty())
                fail ("buffer of " + name + ": " + error);
            check ("  buffer x2", true);
        }
        // the device in use is unplugged: JUCE reopens what is left (the default one)
        if (k % 4 == 3 && type->getDeviceNames (false).size() > 1)
        {
            type->setPlugged (name, false);
            check ("  " + name + " unplugged", true);
        }
        // a device that will not open (in use by another program): the error is reported, nothing plays, and the
        // next choice works again
        if (k % 7 == 5)
        {
            const auto other = juce::String (standIns[(std::size_t) ((k + 3) % (int) std::size (standIns))].name);
            if (! type->getDeviceNames (false).contains (other))
                type->setPlugged (other, true);
            if (other != (devices.getCurrentAudioDevice() != nullptr ? devices.getCurrentAudioDevice()->getName() : juce::String()))
            {
                state.refuses = other;
                const auto error = choose ([&other] (auto& s) { s.outputDeviceName = other; });
                state.refuses = {};
                for (int i = 0; i < 5; ++i)
                    engine.timerCallback();
                say ("  " + other + " refuses to open: \"" + error + "\", device " + (devices.getCurrentAudioDevice() != nullptr ? "open" : "closed"));
                if (error.isEmpty())
                    fail ("a device that refuses to open reported no error");
                if (const auto again = choose ([] (auto& s) { s.outputDeviceName = standIns[0].name; }); again.isNotEmpty())
                    fail ("back to " + juce::String (standIns[0].name) + ": " + again);
                check ("  back to " + juce::String (standIns[0].name), true);
            }
        }
    }

    engine.stop();
    devices.closeAudioDevice();
    engine.rackDisposer.flush();
    return failures.isEmpty() ? "OK (" + juce::String (done) + " device changes)" : "ERROR " + failures;
}
} // namespace lynx
