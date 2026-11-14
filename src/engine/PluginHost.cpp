#include "engine/PluginHost.h"
#include "platform/Paths.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "core/Preferences.h"
#include "core/PluginScanPolicy.h"

#include "core/FlWrapper.h"
#include "core/FlpExport.h"
#include "core/LoadTrace.h"

#include "engine/ClapFormat.h"
#include "engine/Vst2Format.h"
#include "engine/Vst3NoteTuning.h"
#include "engine/PluginSandbox.h"
#include "engine/FlStudio.h"
#include "platform/CrashGuard.h"
#include "core/Denormals.h"
#include "core/HostSlide.h"
#include "ui/I18n.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace lynx
{
namespace
{
modulewarm::Registry& warmup(); // (slow modules started ahead, round 121: below)
} // namespace

void preparePlugin (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize, bool sideInput)
{
    // Try stereo output; plugins that don't accept it keep whatever layout they have.
    plugin.enableAllBuses();
    auto layout = plugin.getBusesLayout();

    if (! layout.outputBuses.isEmpty())
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    }
    // Issue #109: the second input bus (a sidechain input) only while something feeds it. A plugin whose detector
    // follows an enabled side bus (Waves' dynamics) otherwise listens to silence and never acts. The other extra inputs
    // stay off too: nothing feeds them.
    auto withoutSide = layout;
    for (int i = sideInput ? 2 : 1; i < withoutSide.inputBuses.size(); ++i)
        withoutSide.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
    if (plugin.checkBusesLayoutSupported (withoutSide))
        plugin.setBusesLayout (withoutSide);
    else if (! layout.outputBuses.isEmpty() && plugin.checkBusesLayoutSupported (layout))
        plugin.setBusesLayout (layout); // (a plugin that refuses it keeps its buses on, as before)

    plugin.setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin.prepareToPlay (sampleRate, blockSize);
}

std::vector<PluginOutputInfo> pluginOutputs (const juce::AudioPluginInstance& plugin)
{
    std::vector<PluginOutputInfo> list;
    const auto buses = plugin.getBusCount (false);
    int channel = 0;
    if (buses > 1)
    {
        for (int b = 0; b < buses; ++b)
            if (const auto* bus = plugin.getBus (false, b); bus != nullptr && bus->isEnabled())
            {
                const auto n = bus->getNumberOfChannels();
                if (n > 0)
                    list.push_back ({ bus->getName(), channel, n, b == 0 || bus->isEnabledByDefault() });
                channel += n;
            }
        return list;
    }
    const auto total = plugin.getTotalNumOutputChannels();
    if (total <= 2)
    {
        if (total > 0)
            list.push_back ({ buses == 1 ? plugin.getBus (false, 0)->getName() : juce::String(), 0, total });
        return list;
    }
    for (int ch = 0; ch < total; ch += 2)
        list.push_back ({ "Output " + juce::String (ch / 2 + 1), ch, juce::jmin (2, total - ch) });
    return list;
}

std::vector<PluginOutputInfo> pluginInputs (const juce::AudioPluginInstance& plugin)
{
    std::vector<PluginOutputInfo> list;
    int channel = 0;
    for (int b = 0; b < plugin.getBusCount (true); ++b)
        if (const auto* bus = plugin.getBus (true, b))
        {
            const auto n = bus->getLastEnabledLayout().size();
            list.push_back ({ bus->getName(), channel, juce::jmax (1, n), b == 0 || bus->isEnabledByDefault() });
            channel += bus->isEnabled() ? bus->getNumberOfChannels() : 0;
        }
    return list;
}

void setPluginRenderMode (juce::AudioPluginInstance& plugin, bool offline, int blockSize)
{
    if (plugin.isNonRealtime() == offline)
        return;
    plugin.setNonRealtime (offline);
    if (plugin.getSampleRate() > 0.0)
    {
        // VST3 reads the process mode in its setup (setupProcessing). JUCE's VST3 host skips prepareToPlay when the
        // plugin is active at the same rate and block size, so the plugin was never told: an export ran it in
        // real-time mode (round 169c, issue #32 [LOG] the LYNX Test Synth saw kRealtime through a whole export). It is
        // set up again from scratch.
        plugin.releaseResources();
        plugin.prepareToPlay (plugin.getSampleRate(), blockSize);
    }
}

namespace
{
void setPluginParameter (juce::AudioPluginInstance& plugin, int index, float value) noexcept
{
    const auto& params = plugin.getParameters();

    if (index >= 0 && index < params.size())
        if (auto* p = params[index]; p->getValue() != value)
            p->setValue (value);
}

// The song's transport as hosted plugins see it (tempo, position, play state, the loop when `reportLoop`: FL's wrapper
// "Send loop position", on by default), from the renderer's timeline.
class TimelinePlayHead final : public juce::AudioPlayHead
{
public:
    explicit TimelinePlayHead (bool loop) : reportLoop (loop) {}
    const bool reportLoop;

    juce::Optional<PositionInfo> getPosition() const override
    {
        const auto& t = hostTimeline();
        const auto bpm = t.bpm.load (std::memory_order_relaxed);
        const auto rate = t.sampleRate.load (std::memory_order_relaxed);
        // (the block's own start: a generator's chunk is split at its notes, round 169c)
        const auto quarters = t.tickAt (hostBlockOffset()) / (double) ppq;
        const auto beatsPerBar = juce::jmax (1, t.beatsPerBar.load (std::memory_order_relaxed));
        PositionInfo info;
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { beatsPerBar, 4 });
        info.setPpqPosition (quarters);
        info.setPpqPositionOfLastBarStart (std::floor (quarters / beatsPerBar) * beatsPerBar);
        info.setTimeInSeconds (quarters * 60.0 / juce::jmax (1.0, bpm));
        info.setTimeInSamples ((juce::int64) std::llround (quarters * 60.0 / juce::jmax (1.0, bpm) * rate));
        info.setIsPlaying (t.playing.load (std::memory_order_relaxed));
        if (reportLoop && t.looping.load (std::memory_order_relaxed))
        {
            info.setIsLooping (true);
            info.setLoopPoints (LoopPoints { t.loopStart.load (std::memory_order_relaxed) / (double) ppq, t.loopEnd.load (std::memory_order_relaxed) / (double) ppq });
        }
        return info;
    }
};

juce::AudioPlayHead& timelinePlayHead (bool reportLoop = true)
{
    static TimelinePlayHead withLoop (true), withoutLoop (false);
    return reportLoop ? withLoop : withoutLoop;
}

int bufferChannels (const juce::AudioPluginInstance& plugin)
{
    return juce::jmax (2, plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
}
} // namespace

namespace
{
std::atomic<int> fixedBlockSamples { 512 };
std::atomic<double> fixedBlockRate { 44100.0 };
}

int fixedPluginBlock (bool maximumSize) noexcept
{
    return juce::jlimit (16, PluginHost::maxBlockSize,
                         flFixedBlockSize (fixedBlockRate.load (std::memory_order_relaxed), fixedBlockSamples.load (std::memory_order_relaxed), maximumSize));
}

void setFixedPluginBlock (int deviceBlock, double sampleRate) noexcept
{
    fixedBlockSamples.store (juce::jlimit (16, PluginHost::maxBlockSize, deviceBlock), std::memory_order_relaxed);
    if (sampleRate > 0.0)
        fixedBlockRate.store (sampleRate, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------

// Crashes of hosted plugins this session. The audio thread only writes a fixed slot (no allocation, no lock): a bounded
// copy of the instance's name, taken when the instance was created.
namespace
{
constexpr int crashSlots = 16;
struct CrashSlot
{
    char name[128] {};
    std::uint32_t code = 0;
    bool onLoad = false;
};
CrashSlot crashLog[crashSlots];
std::atomic<int> crashCount { 0 };

void recordCrash (const std::string& name, std::uint32_t code, bool onLoad = false) noexcept
{
    const auto index = crashCount.load (std::memory_order_relaxed);
    auto& slot = crashLog[index % crashSlots];
    std::size_t i = 0;
    for (; i + 1 < sizeof (slot.name) && i < name.size(); ++i)
        slot.name[i] = name[i];
    slot.name[i] = 0;
    slot.code = code;
    slot.onLoad = onLoad;
    crashCount.store (index + 1, std::memory_order_release); // (two plugins crashing in the same instant may share a slot)
}

// The calls of one audio block, as a guarded unit
struct BlockCall
{
    juce::AudioPluginInstance* plugin;
    juce::AudioBuffer<float>* view;
    juce::MidiBuffer* midi;
    void operator()() const { plugin->processBlock (*view, *midi); }
};

// a block call under the crash guard, with this thread's floating-point mode put back after it (core/Denormals.h)
std::uint32_t runBlock (BlockCall call) noexcept
{
    const KeepFloatMode keep;
    return crashguard::run (call);
}
} // namespace

int pluginCrashCount() noexcept
{
    return crashCount.load (std::memory_order_acquire);
}

std::vector<PluginCrash> pluginCrashes()
{
    std::vector<PluginCrash> list;
    const auto count = pluginCrashCount();
    for (int i = std::max (0, count - crashSlots); i < count; ++i)
        list.push_back ({ crashLog[i % crashSlots].name, crashLog[i % crashSlots].code, crashLog[i % crashSlots].onLoad });
    return list;
}

namespace
{
std::optional<std::string> guardedState (juce::AudioPluginInstance& plugin, std::atomic<bool>& crashed, const std::string& name)
{
    if (crashed.load())
        return std::nullopt;
    juce::MemoryBlock state;
    if (const auto code = crashguard::run ([&] { plugin.getStateInformation (state); }); code != 0)
    {
        crashed.store (true);
        recordCrash (name, code);
        return std::nullopt;
    }
    return state.toBase64Encoding().toStdString();
}

void guardedUiParameter (juce::AudioPluginInstance& plugin, std::atomic<bool>& crashed, const std::string& name, int index, float value)
{
    if (crashed.load())
        return;
    if (const auto code = crashguard::run ([&] {
            const auto& params = plugin.getParameters();
            if (index >= 0 && index < params.size())
                params[index]->setValueNotifyingHost (value);
        });
        code != 0)
    {
        crashed.store (true);
        recordCrash (name, code);
    }
}
} // namespace

PluginInstrument::PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize, int fixedBlock, HostedOptions o)
    : options (o),
      plugin (std::move (instance)),
      blockSize (maxBlockSize),
      name (plugin->getName().toStdString())
{
    buffer.setSize (bufferChannels (*plugin), blockSize);
    midi.ensureSize (8192); // the audio thread only adds events within this capacity
    if (options.mpe)
    {
        // issue #108: the MPE zone (15 member channels, per-note bend range 48) goes out with the first block, before
        // any note; a CLAP plugin that takes CLAP notes gets the member channels' data as note expressions
        mpe::zoneSetup (mpe::maxMembers, (int) mpe::defaultBendRange, [this] (int status, int d1, int d2) {
            const std::uint8_t bytes[3] = { (std::uint8_t) status, (std::uint8_t) d1, (std::uint8_t) d2 };
            midi.addEvent (bytes, 3, 0);
        });
        setClapNoteExpressions (*plugin, mpe::defaultBendRange);
    }
    outs = pluginOutputs (*plugin);
    routeOffset = std::make_unique<std::atomic<int>[]> (outs.size() + 1);
    routeActive = std::make_unique<std::atomic<bool>[]> (outs.size() + 1);
    for (std::size_t k = 0; k <= outs.size(); ++k)
    {
        routeOffset[k].store (0);
        routeActive[k].store (k >= outs.size() || outs[k].activeByDefault);
    }
    if (! outs.empty())
    {
        aux.resize (outs.size());
        for (auto& a : aux)
            for (auto& side : a)
                side.assign ((std::size_t) auxCapacity, 0.0f);
    }
    if (fixedBlock > 0)
    {
        // (every output channel goes through the fixed blocks: the other outputs keep their place too)
        const auto channels = juce::jlimit (2, 64, plugin->getTotalNumOutputChannels()); // (64: render()'s pointer array)
        fixed = std::make_unique<FixedBlock> (juce::jmin (fixedBlock, blockSize), channels);
        pending.ensureSize (8192);
        scratch.assign ((std::size_t) blockSize * (std::size_t) channels, 0.0f);
    }
    plugin->setPlayHead (&timelinePlayHead (options.sendLoopPosition));
    producesMidi = plugin->producesMidi(); // only then is what the buffer holds after processing the plugin's output
    // issue #63: a VST3's note ons carry the note's tuning (FL's wrapper); the patched JUCE host asks for it
    const auto format = plugin->getPluginDescription().pluginFormatName;
    noteFormat = format == "VST3" ? hostpitch::Format::vst3 : format == ClapPluginFormat::formatName ? hostpitch::Format::clap : hostpitch::Format::midi;
    takesTuning = noteFormat == hostpitch::Format::vst3;
    if (takesTuning)
        juce::lynxNoteTuningHook = [] (void* context, int channel, int key) noexcept {
            return channel == 0 ? static_cast<hostpitch::SentKeys*> (context)->takeTuning (key) : 0.0f;
        };
}

void PluginInstrument::releaseHeldKeys() noexcept
{
    heldKeys.releaseAll ([this] (int ch, int key) { midi.addEvent (juce::MidiMessage::noteOff (ch + 1, key), 0); });
    mpeNotes.reset(); // (issue #108: their member channels are free again)
    sentKeys.played = {}; // (their note offs went out with these)
}

void PluginInstrument::transportStopped() noexcept
{
    // FL's "Send All Notes Off when playback stops" (on by default); off: each note still held gets its note off
    if (options.allNotesOffOnStop)
    {
        allNotesOff();
        return;
    }
    releaseHeldKeys();
}

void PluginInstrument::panic() noexcept
{
    allNotesOff();
    if (options.resetOnFlush) // FL's "Reset plugin when FL Studio resets": cleared before the next block
        resetPending.store (true, std::memory_order_relaxed);
}

void PluginInstrument::setRenderMode (bool offline)
{
    if (! hasCrashed())
        setPluginRenderMode (*plugin, offline, plugin->getBlockSize() > 0 ? plugin->getBlockSize() : blockSize);
}

void PluginInstrument::resetForExport()
{
    // FL's "Reset plugin when FL Studio resets" (on by default): an export starts without what the plugin still holds
    // (tails, notes)
    if (! hasCrashed() && options.resetOnFlush)
        if (const auto code = crashguard::run ([this] { plugin->reset(); }); code != 0)
            failed (code);
}

void PluginInstrument::collectMidiOutput (const juce::MidiBuffer& buffer) noexcept
{
    if (! producesMidi)
        return;
    for (const auto event : buffer)
    {
        const auto m = event.getMessage();
        if ((m.isNoteOn() || m.isNoteOff()) && midiOutCount < (int) midiOut.size())
            midiOut[(std::size_t) midiOutCount++] = { m.getNoteNumber(), m.isNoteOn() ? m.getFloatVelocity() : 0.0f };
    }
}

int PluginInstrument::takeMidiOutput (MidiOutNote* out, int max) noexcept
{
    const auto count = juce::jmin (max, midiOutCount);
    std::copy (midiOut.begin(), midiOut.begin() + count, out);
    midiOutCount = 0;
    return count;
}

PluginInstrument::~PluginInstrument()
{
    // a plugin that crashed is not called again, not even to release it (its memory is left as it is)
    if (crashed.load())
        (void) plugin.release();
    else
        crashguard::run ([this] { plugin->releaseResources(); });
}

void PluginInstrument::failed (std::uint32_t code) noexcept
{
    crashed.store (true, std::memory_order_relaxed);
    recordCrash (name, code);
}

std::optional<std::string> PluginInstrument::captureState()
{
    return guardedState (*plugin, crashed, name);
}

void PluginInstrument::setParameterFromUi (int index, float value)
{
    if (midiControls.set (index, value)) // a MIDI CC / aftertouch / bend "parameter": sent with the next block
        return;
    guardedUiParameter (*plugin, crashed, name, index, value);
}

// The MIDI velocity of a note on: LYNX's own law is JUCE's (127 v, rounded); a project with FL's levels sends it as
// Fruity Wrapper does (core/HostNotePitch.h, round 190ad)
juce::uint8 PluginInstrument::midiVelocity (float velocity) const noexcept
{
    if (flVelocity.load (std::memory_order_relaxed))
        return (juce::uint8) hostpitch::flWrapperVelocity (velocity);
    return juce::MidiMessage::floatValueToMidiByte (juce::jlimit (0.0f, 1.0f, velocity));
}

void PluginInstrument::noteOnMidi (int key, float velocity, int midiChannel) noexcept
{
    midi.addEvent (juce::MidiMessage::noteOn (juce::jlimit (0, 15, midiChannel) + 1, juce::jlimit (0, 127, key), midiVelocity (velocity)), 0);
    hold (juce::jlimit (0, 15, midiChannel), key, true);
}

void PluginInstrument::noteOffMidi (int key, int midiChannel) noexcept
{
    midi.addEvent (juce::MidiMessage::noteOff (juce::jlimit (0, 15, midiChannel) + 1, juce::jlimit (0, 127, key)), 0);
    hold (juce::jlimit (0, 15, midiChannel), key, false);
}

void PluginInstrument::flushMidiControls() noexcept
{
    midiControls.flush (0, [this] (const std::uint8_t* bytes, int size) { midi.addEvent (bytes, size, 0); });
}

void PluginInstrument::addBend (int offset) noexcept
{
    const auto range = (float) juce::jlimit (1, 48, options.bendRange);
    const auto semitones = livePitch / 100.0f + slideNow;
    const auto value = juce::jlimit (0, 16383, 8192 + juce::roundToInt (juce::jlimit (-1.0f, 1.0f, semitones / range) * 8191.0f));
    if (value == sentBend)
        return;
    sentBend = value;
    (bendInto != nullptr ? *bendInto : midi).addEvent (juce::MidiMessage::pitchWheel (1, value), offset);
}

void PluginInstrument::bendBlock (juce::MidiBuffer& into, int base, int count) noexcept
{
    bendInto = &into;
    if (slideLeft <= 0.0f)
        addBend (base);
    else
        for (int o = 0; o < count; o += 64) // a slide moves in steps of 64 samples (~1.5 ms)
        {
            const auto step = (float) juce::jmin (64, count - o);
            if (slideLeft > 0.0f)
            {
                const auto t = juce::jmin (1.0f, step / slideLeft);
                slideNow += (slideTarget - slideNow) * t;
                slideLeft = juce::jmax (0.0f, slideLeft - step);
            }
            addBend (base + o);
        }
    bendInto = nullptr;
}

void PluginInstrument::slideTo (int key, float, float samples) noexcept
{
    if (lastKey < 0)
        return; // nothing sounds that could slide
    slideTarget = (float) (key - lastKey);
    slideLeft = juce::jmax (1.0f, samples);
}

// Issue #108: the per-note messages of an MPE member channel (pitch bend over 48 semitones, CC 74, channel pressure)
void PluginInstrument::sendExpression (int channel, mpe::Expr expr, float value) noexcept
{
    const auto ch = juce::jlimit (1, 15, channel) + 1;
    if (expr == mpe::Expr::pitch)
        midi.addEvent (juce::MidiMessage::pitchWheel (ch, mpe::bendValue (value, mpe::defaultBendRange)), 0);
    else if (expr == mpe::Expr::timbre)
        midi.addEvent (juce::MidiMessage::controllerEvent (ch, 74, mpe::timbreOfModX (value)), 0);
    else
        midi.addEvent (juce::MidiMessage::channelPressureChange (ch, juce::jlimit (0, 127, juce::roundToInt (juce::jlimit (0.0f, 1.0f, value) * 127.0f))), 0);
}

void PluginInstrument::noteExpression (int key, int prop, float value, float) noexcept
{
    // (without MPE a plugin has no per-note pitch or timbre to take: FL sends none either)
    if (! options.mpe || ! std::isfinite (value))
        return;
    const auto played = juce::jlimit (0, 127, key);
    const auto k = sentKeys.sentFor (played);
    const auto ch = mpeNotes.channelOf (k);
    if (ch < 0)
        return;
    if (prop == (int) vfx::Prop::pitch) // (the note's own cents, from the key it went out on)
        sendExpression (ch, mpe::Expr::pitch, value - (float) (k - played) * 100.0f);
    else if (prop == (int) vfx::Prop::modX)
        sendExpression (ch, mpe::Expr::timbre, value);
    else if (prop == (int) vfx::Prop::modY)
        sendExpression (ch, mpe::Expr::pressure, mpe::pressureOfModY (value));
}

void PluginInstrument::noteOn (int key, float velocity) noexcept
{
    if (options.mpe)
    {
        // issue #108: the note on its own member channel. Its key as the MIDI formats get it (the nearest key); the
        // rest of its fine pitch is that channel's pitch bend, its Mod X the channel's CC 74, its Mod Y the pressure —
        // all three before the note on, as the MPE specification asks. The channel's own pitch (knob, slides, porta)
        // stays on the master channel, which bends every note.
        const auto played = juce::jlimit (0, 127, key);
        const auto fine = std::exchange (nextFine, 0.0f);
        const auto sent = hostpitch::of (played, fine, hostpitch::Format::midi);
        sentKeys.on (played, sent);
        sentKeys.clearTunings(); // (no VST3 tuning in this mode: the bend carries it)
        const auto k = sent.key;
        lastKey = k;
        slideNow = slideTarget = slideLeft = 0.0f;
        if (auto& glide = HostSlide::pendingGlide(); glide.samples > 0.0f)
        {
            slideNow = glide.fromCents / 100.0f;
            slideLeft = glide.samples;
            glide.taken = true;
        }
        addBend (0);
        const auto ch = mpeNotes.noteOn (k);
        sendExpression (ch, mpe::Expr::pitch, (std::isfinite (fine) ? fine : 0.0f) - (float) (k - played) * 100.0f);
        sendExpression (ch, mpe::Expr::timbre, nextModX);
        sendExpression (ch, mpe::Expr::pressure, mpe::pressureOfModY (nextModY));
        midi.addEvent (juce::MidiMessage::noteOn (ch + 1, k, juce::jlimit (0.0f, 1.0f, velocity)), 0);
        hold (ch, k, true);
        nextModX = nextModY = 0.5f;
        return;
    }
    // issue #63: key + the note's fine pitch, rounded to the key it goes out on (FL's wrapper); a VST3 also gets the
    // rest as its note on's tuning (picked up by the patched JUCE VST3 host, engine/Vst3NoteTuning.h)
    const auto sent = hostpitch::of (juce::jlimit (0, 127, key), std::exchange (nextFine, 0.0f), noteFormat);
    sentKeys.on (juce::jlimit (0, 127, key), sent);
    const auto k = sent.key;
    // a new note starts at its own key: a slide's bend is undone before it
    lastKey = k;
    slideNow = slideTarget = slideLeft = 0.0f;
    // FL's channel porta (round 168l): the note starts bent to the previous note's pitch and bends back to its own
    if (auto& glide = HostSlide::pendingGlide(); glide.samples > 0.0f)
    {
        slideNow = glide.fromCents / 100.0f;
        slideLeft = glide.samples;
        glide.taken = true;
    }
    addBend (0);
    midi.addEvent (juce::MidiMessage::noteOn (1, k, midiVelocity (velocity)), 0);
    hold (0, k, true);
    // FL's "Send MOD X as polyphonic aftertouch": the note's Mod X right after its note on
    if (options.modXAftertouch)
        midi.addEvent (juce::MidiMessage::aftertouchChange (1, k, juce::jlimit (0, 127, juce::roundToInt (nextModX * 127.0f))), 0);
    nextModX = nextModY = 0.5f;
}

void PluginInstrument::noteOff (int key) noexcept
{
    const auto k = sentKeys.off (juce::jlimit (0, 127, key)); // (the key its note on went out on, issue #63)
    // (issue #108: on the member channel its note on took)
    const auto ch = options.mpe ? juce::jmax (0, mpeNotes.noteOff (k)) : 0;
    // FL's "Send note release velocity" (on by default): the note's release velocity, else 0
    if (options.releaseVelocity)
        midi.addEvent (juce::MidiMessage::noteOff (ch + 1, k, juce::jlimit (0.0f, 1.0f, nextRelease)), 0);
    else
        midi.addEvent (juce::MidiMessage::noteOff (ch + 1, k), 0);
    nextRelease = 0.5f;
    hold (ch, k, false);
}

void PluginInstrument::allNotesOff() noexcept
{
    // each held key's own note off first (round 169c, issue #32): a VST3 plugin never sees CC 123 / CC 120 (JUCE's VST3
    // host passes a controller only as the parameter the plugin maps it to, and drops it otherwise), so a jump while
    // playing left SynthMaster's notes sounding under the chased ones: doubled notes, a sequence out of time
    releaseHeldKeys();
    midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
}

// A Patcher parameter output (round 171b): the parameter's value as the plugin reports it, guarded like the audio calls
static float hostedParameterValue (juce::AudioPluginInstance& plugin, const std::atomic<bool>& crashed, int index) noexcept
{
    if (crashed.load (std::memory_order_relaxed) || index < 0)
        return -1.0f;
    float value = -1.0f;
    crashguard::run ([&] {
        const auto& params = plugin.getParameters();
        if (index < params.size())
            value = params[index]->getValue();
    });
    return value;
}

float PluginInstrument::parameterValue (int index) const noexcept
{
    return hostedParameterValue (*plugin, crashed, index);
}

void PluginInstrument::setParameter (int index, float value) noexcept
{
    if (crashed.load (std::memory_order_relaxed) || midiControls.set (index, value))
        return;
    if (const auto code = crashguard::run ([&] { setPluginParameter (*plugin, index, value); }); code != 0)
        failed (code);
}

void PluginInstrument::render (float* left, float* right, int n, float gl, float gr) noexcept
{
    // (the extra outputs' buffers hold the whole chunk: this render's part starts where the last one ended)
    chunkBase = auxCursor;
    auxCursor += n;
    if (crashed.load (std::memory_order_relaxed))
    {
        midi.clear();
        return; // silent
    }
    if (resetPending.exchange (false, std::memory_order_relaxed))
        if (const auto code = crashguard::run ([this] { plugin->reset(); }); code != 0)
        {
            failed (code);
            midi.clear();
            return;
        }
    // FL's "Send pitch bend range": the pitch-bend-sensitivity RPN (0, 0) once, before the first notes
    if (options.sendBendRange && ! bendRangeSent)
    {
        bendRangeSent = true;
        const auto range = juce::jlimit (1, 48, options.bendRange);
        for (const auto& m : { juce::MidiMessage::controllerEvent (1, 101, 0), juce::MidiMessage::controllerEvent (1, 100, 0),
                               juce::MidiMessage::controllerEvent (1, 6, range), juce::MidiMessage::controllerEvent (1, 38, 0),
                               juce::MidiMessage::controllerEvent (1, 101, 127), juce::MidiMessage::controllerEvent (1, 100, 127) })
            midi.addEvent (m, 0); // (the first render comes before any note: the plugin is new)
    }
    flushMidiControls();
    // issue #63: the VST3 host picks up each note on's tuning while this thread runs the plugin's blocks
    const auto tuningBefore = juce::lynxNoteTuningContext;
    juce::lynxNoteTuningContext = takesTuning ? &sentKeys : nullptr;
    const juce::ScopeGuard tuningBack { [tuningBefore] { juce::lynxNoteTuningContext = tuningBefore; } };
    // the play head reports where each block starts in the chunk (round 169c); put back to 0 for the effects after it
    const auto chunkOffset = blockClock.start (hostTimeline().chunk.load (std::memory_order_relaxed));
    blockClock.advance (n);
    const juce::ScopeGuard offsetBack { [] { setHostBlockOffset (0); } };

    if (fixed != nullptr)
    {
        bendBlock (midi, 0, n);
        // events of this chunk start where the block being collected stands now
        const auto at = fixed->position();
        for (const auto event : midi)
            pending.addEvent (event.data, event.numBytes, at);
        midi.clear();

        auto process = [this] (float* const* ch, int count) {
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
            view.clear();
            if (! crashed.load (std::memory_order_relaxed))
                if (const auto code = runBlock (BlockCall { plugin.get(), &view, &pending }); code != 0)
                {
                    failed (code);
                    view.clear(); // silent from here on
                }
                else
                    collectMidiOutput (pending);
            pending.clear();
            const auto channels = (int) (scratch.size() / (std::size_t) blockSize);
            for (int c = 0; c < channels; ++c)
                std::memcpy (ch[c], view.getReadPointer (c < view.getNumChannels() ? c : (view.getNumChannels() > 1 ? 1 : 0)),
                             sizeof (float) * (size_t) count);
        };

        const auto channels = (int) (scratch.size() / (std::size_t) blockSize);
        std::array<float*, 64> io {};
        for (int c = 0; c < juce::jmin (channels, (int) io.size()); ++c)
            io[(std::size_t) c] = scratch.data() + (std::size_t) c * (std::size_t) blockSize;
        for (int done = 0; done < n;)
        {
            const auto count = juce::jmin (blockSize, n - done);
            setHostBlockOffset (chunkOffset + done);
            fixed->run (io.data(), count, false, process);
            mixOutputs (io.data(), juce::jmin (channels, (int) io.size()), done, count, gl, gr, left, right);
            done += count;
        }
        return;
    }

    for (int done = 0; done < n;)
    {
        const auto count = juce::jmin (blockSize, n - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
        view.clear();
        bendBlock (midi, 0, count);
        setHostBlockOffset (chunkOffset + done);
        if (const auto code = runBlock (BlockCall { plugin.get(), &view, &midi }); code != 0)
        {
            midi.clear();
            failed (code);
            return;
        }
        collectMidiOutput (midi);
        midi.clear();

        mixOutputs (view.getArrayOfReadPointers(), view.getNumChannels(), done, count, gl, gr, left, right);
        done += count;
    }
}

void PluginInstrument::mixOutputs (const float* const* ch, int numChannels, int at, int count, float gl, float gr, float* left,
                                   float* right) noexcept
{
    if (numChannels <= 0)
        return;
    // no output list (a plugin without outputs): the first channels, as before
    if (outs.empty())
    {
        const auto* l = ch[0];
        const auto* r = numChannels > 1 ? ch[1] : l;
        for (int i = 0; i < count; ++i)
        {
            left[at + i] += l[i] * gl;
            right[at + i] += r[i] * gr;
        }
        return;
    }
    const auto viaAux = anyAux.load (std::memory_order_relaxed) && ! aux.empty();
    for (std::size_t k = 0; k < outs.size(); ++k)
    {
        const auto& o = outs[k];
        if (o.channel >= numChannels || ! routeActive[k].load (std::memory_order_relaxed))
            continue; // FL: an inactive output is silent
        const auto* l = ch[o.channel];
        const auto* r = o.channels > 1 && o.channel + 1 < numChannels ? ch[o.channel + 1] : l;
        if (routeOffset[k].load (std::memory_order_relaxed) == 0 || ! viaAux)
        {
            if (routeOffset[k].load (std::memory_order_relaxed) != 0)
                continue; // (routed elsewhere while the aux buffers are not in use yet: dropped for this chunk)
            for (int i = 0; i < count; ++i)
            {
                left[at + i] += l[i] * gl;
                right[at + i] += r[i] * gr;
            }
            continue;
        }
        auto& a = aux[k];
        const auto from = chunkBase + at;
        for (int i = 0; i < count && from + i < auxCapacity; ++i)
        {
            a[0][(std::size_t) (from + i)] += l[i] * gl;
            a[1][(std::size_t) (from + i)] += r[i] * gr;
        }
    }
}

void PluginInstrument::configure (const ChannelDef& channel, double) noexcept
{
    channelTrack.store (channel.mixerTrack, std::memory_order_relaxed);
    bool elsewhere = false;
    for (std::size_t k = 0; k < outs.size(); ++k)
    {
        const auto route = channel.plugin.outputRoute ((int) k);
        routeOffset[k].store (route.offset, std::memory_order_relaxed);
        // ("Process inactive inputs and outputs": an inactive output plays too, FL [EXEC fl188d k33])
        const auto on = route.isActive (outs[k].activeByDefault) || channel.plugin.processInactive;
        routeActive[k].store (on, std::memory_order_relaxed);
        elsewhere = elsewhere || (on && route.offset != 0);
    }
    anyAux.store (elsewhere && ! aux.empty(), std::memory_order_relaxed);
}

void PluginInstrument::beginChunk (int n) noexcept
{
    auxCursor = 0;
    if (! anyAux.load (std::memory_order_relaxed))
        return;
    for (auto& a : aux)
        for (auto& side : a)
            std::fill (side.begin(), side.begin() + juce::jlimit (0, auxCapacity, n), 0.0f);
}

int PluginInstrument::auxOutputTrack (int k) const noexcept
{
    if (k < 0 || k >= (int) outs.size() || ! routeActive[(std::size_t) k].load (std::memory_order_relaxed))
        return -1;
    const auto offset = routeOffset[(std::size_t) k].load (std::memory_order_relaxed);
    return offset == 0 ? -1 : outputTargetTrack (channelTrack.load (std::memory_order_relaxed), { offset, true });
}

const float* PluginInstrument::auxOutputData (int k, int side) const noexcept
{
    return k >= 0 && k < (int) aux.size() ? aux[(std::size_t) k][(std::size_t) (side & 1)].data() : nullptr;
}

// ---------------------------------------------------------------------------

PluginEffect::PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize, int fixedBlock, HostedOptions o)
    : plugin (std::move (instance)),
      blockSize (maxBlockSize),
      name (plugin->getName().toStdString())
{
    buffer.setSize (bufferChannels (*plugin), blockSize);
    midi.ensureSize (2048); // MIDI CC "parameters" (core/MidiControls.h)
    sideBus = hasSideBus (*plugin);
    findSideChannel();
    outs = pluginOutputs (*plugin);
    for (auto& m : outputMode)
        m.store (-1, std::memory_order_relaxed);
    if (fixedBlock > 0)
    {
        // (with a side bus the fixed blocks always carry it, so turning it on later changes nothing here; a second
        // output pair rides on the same channels back, for its Connections, round 189j)
        const auto four = sideBus || sideChannel >= 0 || outs.size() > 1;
        fixed = std::make_unique<FixedBlock> (juce::jmin (fixedBlock, blockSize), four ? 4 : 2);
        if (four)
            sideScratch.assign ((std::size_t) blockSize * 2, 0.0f);
    }
    options = o;
    plugin->setPlayHead (&timelinePlayHead (options.sendLoopPosition));
}

void PluginEffect::setRenderMode (bool offline)
{
    if (! hasCrashed())
        setPluginRenderMode (*plugin, offline, plugin->getBlockSize() > 0 ? plugin->getBlockSize() : blockSize);
}

void PluginEffect::resetForExport()
{
    // FL's "Reset plugin when FL Studio resets" (on by default): an export starts without what the plugin still holds
    // (tails, notes)
    if (! hasCrashed() && options.resetOnFlush)
        if (const auto code = crashguard::run ([this] { plugin->reset(); }); code != 0)
            failed (code);
}

PluginEffect::~PluginEffect()
{
    if (crashed.load())
        (void) plugin.release();
    else
        crashguard::run ([this] { plugin->releaseResources(); });
}

void PluginEffect::failed (std::uint32_t code) noexcept
{
    crashed.store (true, std::memory_order_relaxed);
    recordCrash (name, code);
}

std::optional<std::string> PluginEffect::captureState()
{
    return guardedState (*plugin, crashed, name);
}

void PluginEffect::flushMidiControls() noexcept
{
    midiControls.flush (0, [this] (const std::uint8_t* bytes, int size) { midi.addEvent (bytes, size, 0); });
}

void PluginEffect::setParameterFromUi (int index, float value)
{
    if (midiControls.set (index, value))
        return;
    guardedUiParameter (*plugin, crashed, name, index, value);
}

float PluginEffect::parameterValue (int index) const noexcept
{
    return hostedParameterValue (*plugin, crashed, index);
}

void PluginEffect::setParameter (int index, float value) noexcept
{
    if (crashed.load (std::memory_order_relaxed) || midiControls.set (index, value))
        return;
    if (const auto code = crashguard::run ([&] { setPluginParameter (*plugin, index, value); }); code != 0)
        failed (code);
}

void PluginEffect::findSideChannel() noexcept
{
    sideChannel = -1;
    sideCount = 0;
    if (const auto side = sideInputChannels (*plugin); side > 0)
    {
        const auto first = plugin->getChannelIndexInProcessBlockBuffer (true, 1, 0);
        if (first >= 2 && first + side <= buffer.getNumChannels())
        {
            sideChannel = first;
            sideCount = juce::jmin (2, side);
        }
    }
}

void PluginEffect::setSideInputActive (bool on)
{
    if (! sideBus || on == (sideChannel >= 0) || crashed.load (std::memory_order_relaxed))
        return;
    const std::lock_guard<std::mutex> lock (layoutLock); // waits for the block in progress
    if (const auto code = crashguard::run ([this, on] {
            plugin->releaseResources();
            preparePlugin (*plugin, plugin->getSampleRate(), plugin->getBlockSize(), on);
        });
        code != 0)
    {
        failed (code);
        return;
    }
    buffer.setSize (bufferChannels (*plugin), blockSize);
    findSideChannel();
}

bool hasSideBus (const juce::AudioPluginInstance& plugin)
{
    return plugin.getBusCount (true) >= 2;
}

int sideInputChannels (const juce::AudioPluginInstance& plugin)
{
    if (plugin.getBusCount (true) < 2)
        return 0;
    const auto* bus = plugin.getBus (true, 1);
    return bus != nullptr && bus->isEnabled() ? bus->getNumberOfChannels() : 0;
}

namespace
{
// a plugin that plays its side input (bus 1) and drops its main input: what reaches the bus is what comes out
class SideEchoPlugin final : public juce::AudioPluginInstance
{
public:
    SideEchoPlugin()
        : AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "LYNX side echo"; d.pluginFormatName = "Internal"; }
    const juce::String getName() const override { return "LYNX side echo"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        auto side = getBusBuffer (buffer, true, 1);
        for (int c = 0; c < 2; ++c)
            buffer.copyFrom (c, 0, side, c, 0, buffer.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

// issue #109: a compressor as Waves' RCompressor behaves: its detector follows the side bus when that bus is on, its own
// input otherwise; a level over 0.5 turns the output down to a quarter
class SideDetectCompressor final : public juce::AudioPluginInstance
{
public:
    SideDetectCompressor()
        : AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "LYNX side compressor"; d.pluginFormatName = "Internal"; }
    const juce::String getName() const override { return "LYNX side compressor"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto* side = getBusCount (true) > 1 ? getBus (true, 1) : nullptr;
        auto detector = side != nullptr && side->isEnabled() ? getBusBuffer (buffer, true, 1) : getBusBuffer (buffer, true, 0);
        const auto level = detector.getMagnitude (0, buffer.getNumSamples());
        if (level > 0.5f)
            for (int c = 0; c < 2; ++c)
                buffer.applyGain (c, 0, buffer.getNumSamples(), 0.25f);
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};
} // namespace

juce::String pluginSidechainSelfTest()
{
    constexpr int block = 256;
    for (const auto fixedBlock : { 0, 64 })
    {
        auto instance = std::make_unique<SideEchoPlugin>();
        preparePlugin (*instance, 48000.0, block);
        // issue #109: the side bus stays off until the slot has a source
        if (sideInputChannels (*instance) != 0 || ! hasSideBus (*instance))
            return "ERROR side bus on without a source";
        PluginEffect fx (std::move (instance), block, fixedBlock);
        if (! fx.canTakeSideInput() || fx.hasSideInput())
            return "ERROR side bus not found, or on without a source";
        fx.setSideInputActive (true);
        if (! fx.hasSideInput() || sideInputChannels (fx.getInstance()) != 2)
            return "ERROR side bus not turned on";
        std::vector<float> l (block, 0.25f), r (block, 0.25f), sl (block), sr (block);
        for (int i = 0; i < block; ++i)
        {
            sl[(std::size_t) i] = 0.5f;
            sr[(std::size_t) i] = -0.5f;
        }
        EffectParams params {};
        // fixed blocks delay by one block: two passes, the second one carries the first's side input
        for (int pass = 0; pass < (fixedBlock > 0 ? 2 : 1); ++pass)
        {
            std::fill (l.begin(), l.end(), 0.25f);
            std::fill (r.begin(), r.end(), 0.25f);
            fx.setSidechain (sl.data(), sr.data());
            fx.process (l.data(), r.data(), block, params, 120.0);
        }
        if (std::abs (l[block - 1] - 0.5f) > 1.0e-6f || std::abs (r[block - 1] + 0.5f) > 1.0e-6f)
            return "ERROR side input not heard (fixed " + juce::String (fixedBlock) + ")";
        // the slot's sidechain removed: silence on the bus (two more passes for the fixed blocks' delay)
        for (int pass = 0; pass < (fixedBlock > 0 ? 2 : 1); ++pass)
            fx.process (l.data(), r.data(), block, params, 120.0);
        if (std::abs (l[block - 1]) > 1.0e-6f || std::abs (r[block - 1]) > 1.0e-6f)
            return "ERROR stale side input (fixed " + juce::String (fixedBlock) + ")";
        // the source removed: the bus goes off again
        fx.setSideInputActive (false);
        if (fx.hasSideInput() || sideInputChannels (fx.getInstance()) != 0)
            return "ERROR side bus still on (fixed " + juce::String (fixedBlock) + ")";
    }

    // issue #109 (Waves RCompressor): a compressor whose detector follows its side bus when that bus is on. With no
    // source picked it must hear its own input and act; with a source it follows the source.
    for (const auto fixedBlock : { 0, 64 })
    {
        auto instance = std::make_unique<SideDetectCompressor>();
        preparePlugin (*instance, 48000.0, block);
        PluginEffect fx (std::move (instance), block, fixedBlock);
        EffectParams params {};
        std::vector<float> l (block), r (block), quiet (block, 0.0f);
        auto run = [&] (const float* side) {
            for (int pass = 0; pass < (fixedBlock > 0 ? 2 : 1); ++pass)
            {
                std::fill (l.begin(), l.end(), 0.8f);
                std::fill (r.begin(), r.end(), 0.8f);
                if (side != nullptr)
                    fx.setSidechain (side, side);
                fx.process (l.data(), r.data(), block, params, 120.0);
            }
            return l[block - 1];
        };
        if (std::abs (run (nullptr) - 0.2f) > 1.0e-6f)
            return "ERROR compressor without a source doesn't act (fixed " + juce::String (fixedBlock) + ")";
        fx.setSideInputActive (true);
        if (std::abs (run (quiet.data()) - 0.8f) > 1.0e-6f)
            return "ERROR compressor doesn't follow its source (fixed " + juce::String (fixedBlock) + ")";
        fx.setSideInputActive (false);
        if (std::abs (run (nullptr) - 0.2f) > 1.0e-6f)
            return "ERROR compressor deaf after the source is removed (fixed " + juce::String (fixedBlock) + ")";
    }
    return "OK";
}

void PluginEffect::setConnections (const SlotConnections& c)
{
    for (int o = 0; o < maxPluginConnections; ++o)
        outputMode[(std::size_t) o].store (c.routed ? c.outputs[(std::size_t) o] : -1, std::memory_order_relaxed);
    routed.store (c.routed, std::memory_order_relaxed);
}

void PluginEffect::routeOutputs (const float* const* ch, int numChannels, int at, int count, float* left, float* right, bool taps) noexcept
{
    // FL [EXEC fxconn189j]: the output pairs at index 0 summed into the insert (none: silence), the others to their
    // track (the renderer's tap; `taps` false: not from here), an inactive one nowhere
    std::fill (left + at, left + at + count, 0.0f);
    std::fill (right + at, right + at + count, 0.0f);
    for (std::size_t k = 0; k < outs.size(); ++k)
    {
        const auto& o = outs[k];
        if (o.channel >= numChannels)
            continue;
        const auto mode = k < outputMode.size() ? outputMode[k].load (std::memory_order_relaxed) : -1;
        float* dl = left;
        float* dr = right;
        if (mode >= 0)
        {
            dl = taps && k < extraL.size() ? extraL[k] : nullptr;
            dr = taps && k < extraR.size() ? extraR[k] : nullptr;
        }
        if (mode == -2 || dl == nullptr || dr == nullptr)
            continue;
        const auto* l = ch[o.channel];
        const auto* r = o.channels > 1 && o.channel + 1 < numChannels ? ch[o.channel + 1] : l;
        for (int i = 0; i < count; ++i)
        {
            dl[at + i] += l[i];
            dr[at + i] += r[i];
        }
    }
}

void PluginEffect::process (float* left, float* right, int n, const EffectParams&, double) noexcept
{
    // this block's sidechain (null = silence); cleared so a slot whose side input is removed hears silence again
    const auto* scL = sideL;
    const auto* scR = sideR != nullptr ? sideR : sideL;
    sideL = sideR = nullptr;
    // input 1 from another insert (Connections, round 189j): null = silence; cleared like the sidechain
    const auto replaceMain = mainSet;
    const auto* inL = mainL;
    const auto* inR = mainR != nullptr ? mainR : mainL;
    mainSet = false;
    mainL = mainR = nullptr;
    const auto isRouted = routed.load (std::memory_order_relaxed);
    // copies the sidechain into the side bus channels of `view` from sample `from` of the block (silence without one)
    auto feedSide = [this, scL, scR] (juce::AudioBuffer<float>& view, int from, int count) noexcept {
        if (sideChannel < 0 || scL == nullptr)
            return; // (the view was cleared)
        view.copyFrom (sideChannel, 0, scL + from, count);
        if (sideCount > 1)
            view.copyFrom (sideChannel + 1, 0, scR + from, count);
    };
    if (crashed.load (std::memory_order_relaxed))
        return; // bypassed: the input plays through
    // the buses being laid out again (a sidechain source picked or removed, issue #109): this block plays dry
    const std::unique_lock<std::mutex> layout (layoutLock, std::try_to_lock);
    if (! layout.owns_lock())
        return;
    if (resetPending.exchange (false, std::memory_order_relaxed))
        if (const auto code = crashguard::run ([this] { plugin->reset(); }); code != 0)
        {
            failed (code);
            return;
        }
    flushMidiControls();

    if (fixed != nullptr)
    {
        // the sidechain rides along as channels 2 and 3 of the fixed blocks (copies: FixedBlock writes them back)
        const auto sideRun = sideChannel >= 0 && n <= blockSize;
        float* io[4] = { left, right, sideScratch.data(), sideScratch.data() + blockSize };
        if (sideChannel >= 0)
        {
            const auto m = juce::jmin (n, blockSize);
            for (int c = 0; c < 2; ++c)
            {
                const auto* src = c == 0 ? scL : scR;
                if (src != nullptr && sideRun)
                    std::memcpy (io[2 + c], src, sizeof (float) * (size_t) m);
                else
                    std::fill (io[2 + c], io[2 + c] + m, 0.0f);
            }
        }
        // (input 1 from another insert: after the side input took the slot's own one)
        if (replaceMain)
        {
            for (int c = 0; c < 2; ++c)
            {
                const auto* src = c == 0 ? inL : inR;
                if (src != nullptr)
                    std::memcpy (io[c], src, sizeof (float) * (size_t) n);
                else
                    std::fill (io[c], io[c] + n, 0.0f);
            }
        }
        // routed: output pair 1 / 2 come back on channels 0-1 / 2-3 (pairs past the second are not carried here)
        fixed->run (io, n, true, [this, isRouted] (float* const* ch, int count) {
            if (crashed.load (std::memory_order_relaxed))
                return; // (crashed in an earlier block of this call: the input passes through)
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
            view.clear();
            view.copyFrom (0, 0, ch[0], count);
            view.copyFrom (1, 0, ch[1], count);
            if (sideChannel >= 0)
            {
                view.copyFrom (sideChannel, 0, ch[2], count);
                if (sideCount > 1)
                    view.copyFrom (sideChannel + 1, 0, ch[3], count);
            }
            if (const auto code = runBlock (BlockCall { plugin.get(), &view, &midi }); code != 0)
            {
                midi.clear();
                failed (code); // this block keeps its dry input
                return;
            }
            midi.clear();
            if (isRouted)
            {
                // into the insert: the pairs at index 0; pair 2 sent elsewhere rides back on channels 2-3
                routeOutputs (view.getArrayOfReadPointers(), view.getNumChannels(), 0, count, ch[0], ch[1], false);
                if (sideScratch.empty())
                    return;
                const auto pair2 = outs.size() > 1 && outputMode[1].load (std::memory_order_relaxed) >= 0;
                if (pair2 && outs[1].channel < view.getNumChannels())
                {
                    const auto& o = outs[1];
                    std::memcpy (ch[2], view.getReadPointer (o.channel), sizeof (float) * (size_t) count);
                    std::memcpy (ch[3], view.getReadPointer (o.channels > 1 && o.channel + 1 < view.getNumChannels() ? o.channel + 1 : o.channel),
                                 sizeof (float) * (size_t) count);
                }
                else
                {
                    std::fill (ch[2], ch[2] + count, 0.0f);
                    std::fill (ch[3], ch[3] + count, 0.0f);
                }
                return;
            }
            std::memcpy (ch[0], view.getReadPointer (0), sizeof (float) * (size_t) count);
            std::memcpy (ch[1], view.getReadPointer (view.getNumChannels() > 1 ? 1 : 0), sizeof (float) * (size_t) count);
        });
        if (isRouted && outs.size() > 1 && outputMode[1].load (std::memory_order_relaxed) >= 0 && extraL[1] != nullptr && extraR[1] != nullptr
            && sideScratch.size() >= (std::size_t) blockSize * 2 && n <= blockSize)
            for (int i = 0; i < n; ++i)
            {
                extraL[1][i] += io[2][i];
                extraR[1][i] += io[3][i];
            }
        return;
    }

    for (int done = 0; done < n;)
    {
        const auto count = juce::jmin (blockSize, n - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
        view.clear();
        if (! replaceMain)
        {
            view.copyFrom (0, 0, left + done, count);
            view.copyFrom (1, 0, right + done, count);
        }
        else if (inL != nullptr)
        {
            view.copyFrom (0, 0, inL + done, count);
            view.copyFrom (1, 0, inR + done, count);
        }
        feedSide (view, done, count);
        if (const auto code = runBlock (BlockCall { plugin.get(), &view, &midi }); code != 0)
        {
            midi.clear();
            failed (code); // this block keeps its dry input
            return;
        }
        midi.clear();

        if (isRouted)
            routeOutputs (view.getArrayOfReadPointers(), view.getNumChannels(), done, count, left, right, true);
        else
        {
            std::memcpy (left + done, view.getReadPointer (0), sizeof (float) * (size_t) count);
            std::memcpy (right + done, view.getReadPointer (view.getNumChannels() > 1 ? 1 : 0), sizeof (float) * (size_t) count);
        }
        done += count;
    }
}

// ---------------------------------------------------------------------------

namespace
{
// LV2 lists plugins by URI from whole folders: lilv reads every entry of a folder it is given as a bundle, so it gets
// only the folders that hold .lv2 bundles (not the VST2/VST3/CLAP ones, which only filled the log with errors)
juce::FileSearchPath lv2Folders (const juce::FileSearchPath& paths)
{
    juce::FileSearchPath lv2;
    for (int i = 0; i < paths.getNumPaths(); ++i)
        if (paths[i].isDirectory() && ! paths[i].findChildFiles (juce::File::findDirectories, false, "*.lv2").isEmpty())
            lv2.addIfNotAlreadyThere (paths[i]);
    return lv2;
}

// The standard folders and the user's scan folders (Plugin manager), without the FL Studio lookup of suggestedFolders
juce::FileSearchPath lv2SearchFolders()
{
    auto folders = platform::standardPluginFolders();
    juce::StringArray lines;
    lines.addLines (platform::settingsFile ("plugin-folders.txt").loadFileAsString());
    for (const auto& line : lines)
        if (juce::File::isAbsolutePath (line.trim()))
            folders.addIfNotAlreadyThere (juce::File (line.trim()));
    return folders;
}

// The plugin files (VST2/VST3/CLAP) and LV2 URIs found in these folders, by every format that scans.
juce::StringArray listPluginFiles (juce::AudioPluginFormatManager& formats, const juce::FileSearchPath& paths, bool withLv2 = true)
{
    juce::StringArray files;
    for (auto* format : formats.getFormats())
        if (format->canScanForPlugins() && (withLv2 || format->getName() != "LV2"))
            files.addArray (format->searchPathsForPlugins (format->getName() == "LV2" ? lv2Folders (paths) : paths, true, false));
    return files;
}

// `--scan-one LV2 <this><folders>`: one child describes every LV2 plugin of these folders
const juce::String lv2BatchPrefix ("lv2-folders:");

juce::String lv2Stamp (const juce::FileSearchPath& folders)
{
    std::vector<std::pair<std::string, long long>> bundles;
    for (int i = 0; i < folders.getNumPaths(); ++i)
        for (const auto& bundle : folders[i].findChildFiles (juce::File::findDirectories, false, "*.lv2"))
        {
            auto newest = 0LL; // (a plugin's own .ttl can change while manifest.ttl does not)
            for (const auto& ttl : bundle.findChildFiles (juce::File::findFiles, false, "*.ttl"))
                newest = juce::jmax (newest, (long long) ttl.getLastModificationTime().toMilliseconds());
            bundles.emplace_back (bundle.getFullPathName().toStdString(), newest);
        }
    return juce::String::fromUTF8 (scanpolicy::lv2Stamp (std::move (bundles)).c_str());
}
} // namespace

// Background scan: a thread that runs PluginHost::scan and publishes its progress in PluginHost::state.
class PluginHost::BackgroundScan final : public juce::Thread
{
public:
    BackgroundScan (PluginHost& h, juce::FileSearchPath p, juce::StringArray f = {})
        : juce::Thread ("LYNX plugin scan"),
          host (h),
          paths (std::move (p)),
          files (std::move (f))
    {
    }

    ~BackgroundScan() override
    {
        host.stopScan = true;
        stopThread (15000); // (listing a large folder can take a moment; a running child is killed at once)
    }

    void run() override
    {
        const auto progress = [this] (float done, const juce::String& file) {
            const std::scoped_lock lock (host.stateLock);
            host.state.progress = done;
            host.state.file = juce::File::createFileWithoutCheckingPath (file).getFileName();
            return ! threadShouldExit();
        };
        const auto found = files.isEmpty() ? host.scan (paths, progress) : host.scanFiles (files, progress);
        const std::scoped_lock lock (host.stateLock);
        host.state.running = false;
        host.state.found = found;
        host.state.progress = 1.0f;
        host.state.file = {};
        ++host.state.finished;
    }

private:
    PluginHost& host;
    juce::FileSearchPath paths;
    juce::StringArray files; // a targeted scan of these files instead of the folders
};

PluginHost::PluginHost (bool keep)
    : persistent (keep)
{
    addPluginFormats (formats);
    // JUCE's LV2 host only knows ~/.lv2 and one of /usr/lib/lv2 or /usr/lib64/lv2: the plugins found elsewhere by a
    // scan must load in a later session too, before (or without) the next scan
    if (persistent)
        for (auto* format : formats.getFormats())
            if (format->getName() == "LV2")
                format->searchPathsForPlugins (lv2Folders (lv2SearchFolders()), true, false);
    if (persistent)
    {
        if (auto xml = juce::XmlDocument::parse (settingsFolder().getChildFile ("plugins.xml")))
            known.recreateFromXml (*xml);
        favouriteIds = prefs::parseFavourites (settingsFolder().getChildFile ("plugin-favourites.txt").loadFileAsString().toStdString());
        userNames = prefs::parsePluginNames (settingsFolder().getChildFile ("plugin-names.txt").loadFileAsString().toStdString());
        // (plugin-database.txt since round 190ba; before, the folders shared plugin-folders.txt with the scan folders)
        const auto databaseFile = settingsFolder().getChildFile ("plugin-database.txt");
        userFolders = plugincatalog::parseFolders ((databaseFile.existsAsFile() ? databaseFile : settingsFolder().getChildFile ("plugin-folders.txt")).loadFileAsString().toStdString());
        // once: files marked as failed by scans before the class-count timeout (REA_rodada107 §2) get another try,
        // since a large bundle (Waves shell) was marked "hung" only for taking longer than 30 s
        const auto retried = settingsFolder().getChildFile ("scan-retry.txt");
        // round 113: once more, as the scan now keeps a description whose plugin only hung while closing (or, round
        // 111, crashed in its teardown at exit after writing it)
        if (retried.loadFileAsString().getIntValue() < 2)
        {
            if (! known.getBlacklistedFiles().isEmpty())
            {
                known.clearBlacklistedFiles();
                save();
            }
            retried.replaceWithText ("2\n");
        }
    }
    if (persistent)
        flDbWarmup = std::thread ([this] {
            for (int i = 0; i < 60 && ! warmupQuit; ++i) // after the start-up's own disk work
                std::this_thread::sleep_for (std::chrono::milliseconds (50));
            if (! warmupQuit)
                flPluginDatabase();
        });
}

PluginHost::~PluginHost()
{
    warmupQuit = true;
    if (flDbWarmup.joinable())
        flDbWarmup.join();
    background = nullptr; // stops the scan (the child being examined is killed)
}

juce::File PluginHost::settingsFolder()
{
    return platform::settingsFolder();
}

juce::FileSearchPath PluginHost::defaultSearchPath()
{
    return platform::standardPluginFolders();
}

juce::FileSearchPath PluginHost::suggestedFolders()
{
    juce::FileSearchPath paths;
    auto add = [&paths] (const juce::File& folder) {
        if (folder.isDirectory())
            paths.addIfNotAlreadyThere (folder);
    };
    // the standard folders even before they exist (plugins installed later land there)
    for (int i = 0; i < defaultSearchPath().getNumPaths(); ++i)
        paths.addIfNotAlreadyThere (defaultSearchPath()[i]);
    const auto fl = findFlStudio();
    for (const auto& folder : fl.vstFolders)
        add (juce::File (folder));
    for (const auto& folder : flPluginDatabase().pluginFolders())
    {
        const auto path = juce::String::fromUTF8 (folder.c_str());
        if (juce::File::isAbsolutePath (path))
            add (juce::File (path));
    }
    return paths;
}

juce::FileSearchPath PluginHost::pluginFolders() const
{
    const auto file = settingsFolder().getChildFile ("plugin-folders.txt");
    if (persistent && file.existsAsFile())
    {
        juce::FileSearchPath paths;
        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());
        for (const auto& line : lines)
            if (line.trim().isNotEmpty() && juce::File::isAbsolutePath (line.trim()))
                paths.add (juce::File (line.trim()));
        // (a file holding only the Plugin database's folders, written there until 0.1.25, is not a list of scan folders)
        if (paths.getNumPaths() > 0 || ! file.loadFileAsString().containsChar ('\t'))
            return paths;
    }
    return const_cast<PluginHost*> (this)->suggestedFolders();
}

void PluginHost::setPluginFolders (const juce::FileSearchPath& folders)
{
    if (! persistent)
        return;
    juce::StringArray lines;
    for (int i = 0; i < folders.getNumPaths(); ++i)
        lines.add (folders[i].getFullPathName());
    settingsFolder().getChildFile ("plugin-folders.txt").replaceWithText (lines.joinIntoString ("\n") + "\n");
}

const FlPluginDb& PluginHost::flPluginDatabase()
{
    const std::scoped_lock lock (flDbLock);
    if (! flDbLoaded)
    {
        flDb = readFlPluginDatabase (findFlStudio(), &flDbFiles);
        flDbLoaded = true;
    }
    return flDb;
}

void PluginHost::save()
{
    if (! persistent)
        return;
    const std::scoped_lock lock (saveLock);
    if (auto xml = known.createXml())
        xml->writeTo (settingsFolder().getChildFile ("plugins.xml"));
}

namespace
{
// Child side of the scan: how many classes a VST3 module's factory declares (0 = unknown). The module stays loaded
// (and initialised) for the description that follows, which loads the same module again. Windows only for now.
int countVst3Classes (const juce::String& path)
{
   #if JUCE_WINDOWS
    juce::File binary (path);
    if (binary.isDirectory()) // bundle: Contents/x86_64-win/<name>.vst3
        binary = binary.getChildFile ("Contents").getChildFile ("x86_64-win").getChildFile (binary.getFileName());
    if (! binary.existsAsFile())
        return 0;
    // the leading part of IPluginFactory's vtable (FUnknown, then getFactoryInfo, countClasses)
    struct Factory
    {
        virtual int queryInterface (const char*, void**) = 0;
        virtual unsigned addRef() = 0;
        virtual unsigned release() = 0;
        virtual int getFactoryInfo (void*) = 0;
        virtual int countClasses() = 0;
    };
    static juce::DynamicLibrary module; // kept open until the child exits
    if (! module.open (binary.getFullPathName()))
        return 0;
    if (auto* init = (bool (*)()) module.getFunction ("InitDll"))
        if (! init())
            return 0;
    auto* get = (Factory* (*)()) module.getFunction ("GetPluginFactory");
    auto* factory = get != nullptr ? get() : nullptr;
    if (factory == nullptr)
        return 0;
    const auto classes = factory->countClasses();
    factory->release();
    return juce::jmax (0, classes);
   #else
    juce::ignoreUnused (path);
    return 0;
   #endif
}

juce::File classCountFile (const juce::File& result)
{
    return result.withFileExtension ("classes");
}

// the child wrote its whole answer (the file closes with the root element): a hang after it is the plugin's shutdown
bool scanResultComplete (const juce::File& result)
{
    if (! result.existsAsFile())
        return false;
    const auto xml = juce::XmlDocument::parse (result);
    return xml != nullptr && xml->hasTagName ("PLUGINS") && xml->getNumChildElements() > 0;
}

// why a plugin file was not described (%APPDATA%\LYNX\scan-failures.log), so a "missing" plugin can be explained
void logScanFailure (const juce::String& file, const juce::String& why, juce::uint32 elapsedMs)
{
    const auto log = lynx::platform::settingsFile ("scan-failures.log");
    if (log.getSize() > 256 * 1024)
        log.deleteFile();
    log.appendText (juce::Time::getCurrentTime().toISO8601 (true) + " | " + file + " | " + why + " | " + juce::String (elapsedMs / 1000.0, 1) + " s\n");
}

} // namespace

bool PluginHost::scanOne (juce::AudioPluginFormat& format, const juce::String& file, const std::function<bool()>& keepGoing)
{
    // a result file of our own: several LYNX processes (and scans) may run at once
    const auto result = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("lynx-scan-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");
    // a 32-bit module (round 176a) is described by the 32-bit bridge, which can load it; without the bridge it is skipped
    const auto bits32 = isModule32Bit (file);
    const auto exe = bits32 ? bridge32Executable() : juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    if (bits32 && ! exe.existsAsFile())
        return false;
    ++scanChildren;
    juce::ChildProcess child;
    auto ok = child.start (juce::StringArray { exe.getFullPathName(), "--scan-one", format.getName(), file, result.getFullPathName() }, 0);
    const auto started = juce::Time::getMillisecondCounter();
    if (ok)
    {
        // wait in short steps so a cancelled scan (or LYNX closing) doesn't wait for a slow plugin; a file holding
        // many plugins (Waves shell) gets more time once the child has said how many (core/PluginScanPolicy.h)
        auto timeout = (juce::uint32) scanpolicy::baseTimeoutMs;
        auto classesKnown = false;
        while (child.isRunning() && ! stopScan && juce::Time::getMillisecondCounter() - started < timeout)
        {
            child.waitForProcessToFinish (100);
            if (keepGoing && ! keepGoing())
                stopScan = true;
            if (! classesKnown && classCountFile (result).existsAsFile())
            {
                classesKnown = true;
                timeout = (juce::uint32) scanpolicy::timeoutMs (classCountFile (result).loadFileAsString().getIntValue());
            }
        }
        if (child.isRunning())
        {
            child.kill();
            if (stopScan) // not the plugin's fault: try it again next time
            {
                result.deleteFile();
                classCountFile (result).deleteFile();
                return false;
            }
            // the description may be complete and only the plugin's shutdown hung (round 113): keep it then
            ok = scanResultComplete (result);
            logScanFailure (file, ok ? "result kept, child killed after it" : (classesKnown ? "timeout while describing" : "timeout before the class count"),
                            juce::Time::getMillisecondCounter() - started);
        }
        else if (child.getExitCode() != 0)
            logScanFailure (file, "exit code " + juce::String ((int) child.getExitCode()), juce::Time::getMillisecondCounter() - started);
    }
    ok = ok && (child.isRunning() || child.getExitCode() == 0 || scanResultComplete (result));
    const auto xml = ok ? juce::XmlDocument::parse (result) : nullptr;
    if (xml != nullptr)
    {
        auto added = 0;
        for (auto* e : xml->getChildIterator())
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*e))
                added += known.addType (d) ? 1 : 0;
        }
        if (xml->getNumChildElements() == 0 && added == 0)
            rememberEmptyScan (file); // nothing in it: not described again on every open until it changes
    }
    else
    {
        if (ok) // on Linux and macOS, JUCE reports exit code 0 for a child killed by a signal: no answer is the only sign
            logScanFailure (file, "ended without an answer", juce::Time::getMillisecondCounter() - started);
        known.addToBlacklist (file);
    }
    result.deleteFile();
    classCountFile (result).deleteFile();
    return xml != nullptr; // a child can also end "well" without an answer
}

int PluginHost::scan (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress)
{
    const auto before = known.getNumTypes();
    auto lv2Done = false;
    {
        const std::scoped_lock lock (scanLock);
        stopScan = false;
        lv2Done = describeLv2 (paths, progress);
    }
    if (! stopScan)
        scanFiles (listPluginFiles (formats, paths, ! lv2Done), progress);
    if (! stopScan)
        forgetMisses(); // everything was looked at again: the projects' missing plugins may be findable now
    return known.getNumTypes() - before;
}

// LV2 plugins are described from their data files, and JUCE reads all the folders in every process that starts, so a
// child per plugin repeats the same seconds of work hundreds of times. One child does them all, and only when the
// folders changed. False if it failed or was cancelled: the scan then asks plugin by plugin, as a bad bundle needs.
bool PluginHost::describeLv2 (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress)
{
    for (auto* format : formats.getFormats())
    {
        if (format->getName() != "LV2")
            continue;
        const auto folders = lv2Folders (paths);
        auto searched = folders;
        searched.addPath (format->getDefaultLocationsToSearch());
        const auto stamp = lv2Stamp (searched);
        // (with how many are listed: a plugin list that was lost is not "nothing changed")
        const auto listed = [this] {
            const auto types = known.getTypes();
            return juce::String ((int) std::count_if (types.begin(), types.end(), [] (const auto& d) { return d.pluginFormatName == "LV2"; }));
        };
        const auto saved = settingsFolder().getChildFile ("scan-lv2.txt");
        if (persistent && saved.loadFileAsString() == stamp + listed())
            return true;
        const auto batch = lv2BatchPrefix + folders.toString();
        if (! scanOne (*format, batch, [&progress] { return ! progress || progress (0.0f, "LV2"); }))
        {
            known.removeFromBlacklist (batch);
            return false;
        }
        format->searchPathsForPlugins (folders, true, false); // this process's own list learns the new bundles
        save(); // before the stamp: the stamp says these plugins are in the list on disk
        if (persistent)
            saved.replaceWithText (stamp + listed(), false, false, "\n");
        return true;
    }
    return false;
}

namespace
{
// A file of the plugin folders is a module or a bundle folder (VST3 / CLAP / LV2); some plugins in a VST3 bundle
// are listed by the module inside it (Contents\x86_64-win\<name>.vst3 — Antares, sonible…). Round 165i: those
// listings never matched the bundle folder, so every start-up's background scan described all of them again (a
// child process loading each plugin, ~4 s and 100–400 MB apiece, with their licence helpers).
bool listedInside (const juce::KnownPluginList& known, juce::AudioPluginFormat& format, const juce::String& file)
{
    const juce::File bundle (file);
    if (! juce::File::isAbsolutePath (file) || ! bundle.isDirectory())
        return false;
    auto any = false;
    for (const auto& d : known.getTypes())
    {
        if (d.pluginFormatName != format.getName() || ! juce::File::isAbsolutePath (d.fileOrIdentifier)
            || ! juce::File (d.fileOrIdentifier).isAChildOf (bundle))
            continue;
        if (format.pluginNeedsRescanning (d))
            return false;
        any = true;
    }
    return any;
}
} // namespace

bool PluginHost::sameModule (const juce::PluginDescription& d, const juce::String& file)
{
    return d.fileOrIdentifier == file
        || (juce::File::isAbsolutePath (file) && juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (file).isDirectory()
            && juce::File (d.fileOrIdentifier).isAChildOf (juce::File (file)));
}

int PluginHost::scanFiles (const juce::StringArray& files, const std::function<bool (float, const juce::String&)>& progress)
{
    const std::scoped_lock lock (scanLock);
    stopScan = false;
    const auto before = known.getNumTypes();
    // what was found so far is saved as the scan goes (at most every few seconds): LYNX closed in the middle of a
    // background scan used to lose it all, and the next start described the same files again
    auto lastSave = juce::Time::getMillisecondCounter();
    auto unsaved = false;
    for (int i = 0; i < files.size() && ! stopScan; ++i)
    {
        const auto& file = files[i];
        if (progress && ! progress ((float) i / (float) juce::jmax (1, files.size()), file))
            break;
        if (known.getBlacklistedFiles().contains (file))
            continue;
        for (auto* format : formats.getFormats())
        {
            if (! format->canScanForPlugins() || ! format->fileMightContainThisPluginType (file))
                continue;
            if (! known.isListingUpToDate (file, *format) && ! listedInside (known, *format, file) && ! emptyScanIsCurrent (file))
            {
                scanOne (*format, file);
                unsaved = true;
            }
            break;
        }
        if (unsaved && juce::Time::getMillisecondCounter() - lastSave > 5000)
        {
            save();
            lastSave = juce::Time::getMillisecondCounter();
            unsaved = false;
        }
    }
    save();
    return known.getNumTypes() - before;
}

void PluginHost::startBackgroundScanOfFiles (const juce::StringArray& files)
{
    if (backgroundScan().running || files.isEmpty())
        return;
    background = nullptr;
    {
        const std::scoped_lock lock (stateLock);
        state.running = true;
        state.progress = 0.0f;
        state.found = 0;
        state.file = {};
    }
    background = std::make_unique<BackgroundScan> (*this, juce::FileSearchPath(), files);
    background->startThread (juce::Thread::Priority::low);
}

namespace
{
// the file of a scan-empty.txt line ("<date>|<file>", "|bridge32" after it for a 32-bit module the bridge described)
juce::String emptyScanFile (const juce::String& line)
{
    auto file = line.fromFirstOccurrenceOf ("|", false, false);
    return file.endsWith ("|bridge32") ? file.dropLastCharacters (9) : file;
}
} // namespace

void PluginHost::forgetFile (const juce::String& file)
{
    for (const auto& t : known.getTypes())
        if (sameModule (t, file))
            known.removeType (t);
    known.removeFromBlacklist (file);
    {
        juce::StringArray lines;
        const auto list = settingsFolder().getChildFile ("scan-empty.txt");
        lines.addLines (list.loadFileAsString());
        for (int i = lines.size(); --i >= 0;)
            if (emptyScanFile (lines[i]) == file)
                lines.remove (i);
        if (persistent)
            list.replaceWithText (lines.joinIntoString ("\n"));
    }
    if (persistent && ! juce::File::isAbsolutePath (file)) // an LV2 URI: the next scan describes the LV2 folders again
        settingsFolder().getChildFile ("scan-lv2.txt").deleteFile();
    save();
}

void PluginHost::forgetAll()
{
    known.clear();
    known.clearBlacklistedFiles();
    if (persistent)
    {
        settingsFolder().getChildFile ("scan-empty.txt").deleteFile();
        settingsFolder().getChildFile ("scan-lv2.txt").deleteFile();
    }
    forgetMisses();
    save();
}

bool PluginHost::emptyScanIsCurrent (const juce::String& file)
{
    if (! persistent || ! juce::File::isAbsolutePath (file)) // (an LV2 URI has no file date to compare)
        return false;
    const auto stamp = juce::String (juce::File (file).getLastModificationTime().toMilliseconds());
    juce::StringArray lines;
    lines.addLines (settingsFolder().getChildFile ("scan-empty.txt").loadFileAsString());
    // round 176a: a 32-bit module counts as empty only when the 32-bit bridge described it ("|bridge32"); the 64-bit
    // scans before the bridge could not load it at all, so those are looked at again
    return lines.contains (stamp + "|" + file + (isModule32Bit (file) ? "|bridge32" : ""));
}

void PluginHost::rememberEmptyScan (const juce::String& file)
{
    if (! persistent || ! juce::File::isAbsolutePath (file))
        return;
    const auto list = settingsFolder().getChildFile ("scan-empty.txt");
    juce::StringArray lines;
    lines.addLines (list.loadFileAsString());
    for (int i = lines.size(); --i >= 0;)
        if (lines[i].trim().isEmpty() || emptyScanFile (lines[i]) == file)
            lines.remove (i);
    lines.add (juce::String (juce::File (file).getLastModificationTime().toMilliseconds()) + "|" + file + (isModule32Bit (file) ? "|bridge32" : ""));
    list.replaceWithText (lines.joinIntoString ("\n"));
}

namespace
{
juce::String missKey (const FlpPluginUse& use)
{
    const auto& p = use.plugin;
    juce::String key = juce::String ((int) p.format) + ":" + (use.instrument ? "i" : "e") + ":" + juce::String::fromUTF8 (p.name.c_str()) + ":"
                       + juce::String::fromUTF8 (p.path.c_str()) + ":" + juce::String ((juce::int64) p.vst2Id);
    if (p.hasClassId)
        key << ":" << juce::String::toHexString (p.classId.data(), (int) p.classId.size(), 0);
    if (! p.clapId.empty())
        key << ":" << juce::String::fromUTF8 (p.clapId.c_str());
    // a Waves shell: the sub-plugin's name says which one
    key << ":" << juce::String::fromUTF8 (wavesPluginName (pluginIdentity (p).state).c_str());
    return key.replaceCharacters ("\r\n", "  ");
}

constexpr juce::int64 missMemoryMs = 7LL * 24 * 3600 * 1000;
} // namespace

bool PluginHost::recentlyMissed (const FlpPluginUse& use) const
{
    if (! persistent)
        return false;
    juce::StringArray lines;
    lines.addLines (settingsFolder().getChildFile ("scan-misses.txt").loadFileAsString());
    const auto key = missKey (use);
    const auto now = juce::Time::currentTimeMillis();
    for (const auto& line : lines)
        if (line.fromFirstOccurrenceOf ("|", false, false) == key)
        {
            const auto when = line.upToFirstOccurrenceOf ("|", false, false).getLargeIntValue();
            return now >= when && now - when < missMemoryMs;
        }
    return false;
}

void PluginHost::rememberMisses (const std::vector<FlpPluginUse>& uses)
{
    if (! persistent || uses.empty())
        return;
    const auto list = settingsFolder().getChildFile ("scan-misses.txt");
    juce::StringArray lines;
    lines.addLines (list.loadFileAsString());
    const auto now = juce::String (juce::Time::currentTimeMillis());
    for (const auto& use : uses)
    {
        const auto key = missKey (use);
        for (int i = lines.size(); --i >= 0;)
            if (lines[i].trim().isEmpty() || lines[i].fromFirstOccurrenceOf ("|", false, false) == key)
                lines.remove (i);
        lines.add (now + "|" + key);
    }
    list.replaceWithText (lines.joinIntoString ("\n"));
}

void PluginHost::forgetMisses()
{
    if (persistent)
        settingsFolder().getChildFile ("scan-misses.txt").deleteFile();
}

void PluginHost::setFavourite (const std::string& id, bool favourite)
{
    if (favourite)
        favouriteIds.insert (id);
    else
        favouriteIds.erase (id);
    if (persistent)
        settingsFolder().getChildFile ("plugin-favourites.txt").replaceWithText (juce::String (prefs::formatFavourites (favouriteIds)));
}

void PluginHost::setPluginName (const std::string& key, const std::string& name)
{
    auto clean = juce::String::fromUTF8 (name.c_str()).replaceCharacters ("\t\r\n", "   ").trim().toStdString();
    if (clean.empty())
        userNames.erase (key);
    else
        userNames[key] = clean;
    if (persistent)
        settingsFolder().getChildFile ("plugin-names.txt").replaceWithText (juce::String::fromUTF8 (prefs::formatPluginNames (userNames).c_str()));
    if (onDatabaseEdited)
        onDatabaseEdited();
}

void PluginHost::setDatabaseFolders (const plugincatalog::Folders& folders)
{
    const auto viewOnly = folders.of == userFolders.of && folders.effects == userFolders.effects && folders.generators == userFolders.generators
                          && folders.hidden == userFolders.hidden && folders.top == userFolders.top;
    userFolders = folders;
    if (persistent)
        settingsFolder().getChildFile ("plugin-database.txt").replaceWithText (juce::String::fromUTF8 (plugincatalog::formatFolders (userFolders).c_str()));
    if (onDatabaseEdited && ! viewOnly)
        onDatabaseEdited();
}

void PluginHost::adoptDatabase (const plugincatalog::Folders& folders, const std::map<std::string, std::string>& names)
{
    const auto view = userFolders.view;
    const auto foldersChanged = folders.of != userFolders.of || folders.effects != userFolders.effects || folders.generators != userFolders.generators;
    const auto namesChanged = names != userNames;
    userFolders = folders;
    userFolders.view = view;
    userNames = names;
    if (! persistent)
        return;
    const auto databaseFile = settingsFolder().getChildFile ("plugin-database.txt");
    if (foldersChanged || ! databaseFile.existsAsFile())
        databaseFile.replaceWithText (juce::String::fromUTF8 (plugincatalog::formatFolders (userFolders).c_str()));
    if (namesChanged)
        settingsFolder().getChildFile ("plugin-names.txt").replaceWithText (juce::String::fromUTF8 (prefs::formatPluginNames (userNames).c_str()));
}

void PluginHost::startBackgroundScan (const juce::FileSearchPath& paths)
{
    if (backgroundScan().running)
        return;
    background = nullptr; // the previous, finished thread
    {
        const std::scoped_lock lock (stateLock);
        state.running = true;
        state.progress = 0.0f;
        state.found = 0;
        state.file = {};
    }
    background = std::make_unique<BackgroundScan> (*this, paths);
    background->startThread (juce::Thread::Priority::low);
}

PluginHost::ScanState PluginHost::backgroundScan() const
{
    const std::scoped_lock lock (stateLock);
    return state;
}

void PluginHost::cancelBackgroundScan()
{
    if (background != nullptr)
    {
        stopScan = true;
        background->signalThreadShouldExit();
    }
}


int PluginHost::scanOneFile (const juce::String& formatName, const juce::String& file, const juce::File& output)
{
    // first tell the parent how many plugins this file holds, so it waits long enough for a large bundle
    if (formatName == "VST3")
        if (const auto classes = countVst3Classes (file); classes > 1)
            classCountFile (output).replaceWithText (juce::String (classes));

    juce::AudioPluginFormatManager manager;
    addPluginFormats (manager);
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : manager.getFormats())
        if (format->getName() == formatName && formatName == "LV2" && file.startsWith (lv2BatchPrefix))
        {
            // (--lv2scantest: a child that dies, and one that a cancel has to stop)
            const auto fault = juce::SystemStats::getEnvironmentVariable ("LYNX_TEST_CRASH", {});
            if (fault == "lv2-batch")
                std::abort();
            if (fault == "lv2-batch-hang")
                juce::Thread::sleep (60000);
            auto ids = format->searchPathsForPlugins (juce::FileSearchPath (file.fromFirstOccurrenceOf (lv2BatchPrefix, false, false)), true, false);
            ids.removeDuplicates (false); // newer JUCE lists a bundle once per plugin, and describes all of them each time
            classCountFile (output).replaceWithText (juce::String (ids.size())); // (a large collection gets the time it needs)
            for (const auto& id : ids)
                format->findAllTypesForFile (found, id);
        }
        else if (format->getName() == formatName)
        {
            // an LV2 URI is looked up in the folders the scan listed it from: JUCE alone knows only ~/.lv2 and one
            // of /usr/lib/lv2 or /usr/lib64/lv2
            if (formatName == "LV2" && ! juce::File::isAbsolutePath (file))
            {
                format->searchPathsForPlugins (lv2Folders (lv2SearchFolders()), true, false);
            }
            format->findAllTypesForFile (found, file);
        }
    juce::XmlElement xml ("PLUGINS");
    for (auto* d : found)
        xml.addChildElement (d->createXml().release());
    xml.writeTo (output);
   #if JUCE_WINDOWS
    // the answer is written: the child ends here, without unloading the plugin modules, whose shutdown can hang (a
    // Waves shell in a desktop session) and get the whole file marked as hung (round 113)
    if (! found.isEmpty())
        TerminateProcess (GetCurrentProcess(), 0);
   #endif
    return found.size();
}

namespace
{
std::atomic<bool> loadedAny { false };
}

bool PluginHost::pluginsWereLoaded() noexcept
{
    return loadedAny.load();
}

int PluginHost::addPluginFile (const juce::File& file)
{
    loadedAny.store (true);
    const auto before = known.getNumTypes();
    juce::OwnedArray<juce::PluginDescription> found;

    if (file.isDirectory() && file.hasFileExtension ("lv2"))
    {
        // LV2 plugins are known by URI: the bundle's folder is searched, and the URIs its manifest names are added
        const auto manifest = file.getChildFile ("manifest.ttl").loadFileAsString();
        for (auto* format : formats.getFormats())
            if (format->getName() == "LV2")
            {
                format->searchPathsForPlugins (lv2Folders (lv2SearchFolders()), true, false);
                for (const auto& uri : format->searchPathsForPlugins (juce::FileSearchPath (file.getParentDirectory().getFullPathName()), false, false))
                    if (manifest.contains ("<" + uri + ">"))
                        known.scanAndAddFile (uri, true, found, *format);
            }
    }
    else if (isModule32Bit (file.getFullPathName()))
    {
        // a 32-bit module (round 176a): described by the 32-bit bridge, as a scan does
        for (auto* format : formats.getFormats())
            if (format->fileMightContainThisPluginType (file.getFullPathName()))
            {
                scanOne (*format, file.getFullPathName());
                break;
            }
    }
    else
        for (auto* format : formats.getFormats())
            known.scanAndAddFile (file.getFullPathName(), true, found, *format);

    save();
    return known.getNumTypes() - before;
}

void PluginHost::findLv2Plugins()
{
    for (auto* format : formats.getFormats())
        if (format->getName() == "LV2")
            format->searchPathsForPlugins (lv2Folders (lv2SearchFolders()), true, false);
}

std::unique_ptr<juce::PluginDescription> PluginHost::find (const juce::String& id) const
{
    return known.getTypeForIdentifierString (id);
}

std::string PluginHost::bitnessKey (const juce::PluginDescription& d)
{
    return (d.pluginFormatName + "|" + juce::String::toHexString (d.uniqueId) + "|" + d.name).toStdString();
}

std::unique_ptr<juce::PluginDescription> PluginHost::otherBuild (const juce::PluginDescription& description) const
{
    if (! bridge32Supported())
        return nullptr;
    const auto key = bitnessKey (description);
    const auto is32 = isPlugin32Bit (description);
    for (const auto& t : known.getTypes())
        if (t.fileOrIdentifier != description.fileOrIdentifier && bitnessKey (t) == key && isPlugin32Bit (t) != is32)
            return std::make_unique<juce::PluginDescription> (t);
    return nullptr;
}

std::unique_ptr<juce::PluginDescription> PluginHost::buildFor (const PluginRef& ref) const
{
    auto description = find (juce::String::fromUTF8 (ref.id.c_str()));
    if (description == nullptr)
        return nullptr;
    // FL's wrapper "Bit size" [UI fl33c/fl36c]: Native loads the 64-bit build of a "32+64" plugin, "32 bit" the other
    const auto want32 = ref.bitSize == 32;
    if (isPlugin32Bit (*description) != want32)
        if (auto other = otherBuild (*description))
            return other;
    return description;
}

namespace
{
// The deprecatedUid JUCE assigns to a VST3 class: hash-31 over the TUID's 16 bytes as `char`
// (signed, like the SDK's TUID). FL stores the TUID in the same memory order.
int vst3Hash (const std::array<std::uint8_t, 16>& tuid)
{
    juce::uint32 value = 0;

    for (auto b : tuid)
        value = value * 31 + (juce::uint32) (int) (signed char) b;

    return (int) value;
}

// JUCE's "normalised" uniqueId: the same hash over the FUID's 4 integers (COM layout on Windows).
int vst3NormalisedHash (const std::array<std::uint8_t, 16>& d)
{
    auto make = [] (juce::uint32 a, juce::uint32 b, juce::uint32 c, juce::uint32 e) { return (a << 24) | (b << 16) | (c << 8) | e; };
    const juce::uint32 longs[] = { make (d[3], d[2], d[1], d[0]), make (d[5], d[4], d[7], d[6]),
                                   make (d[8], d[9], d[10], d[11]), make (d[12], d[13], d[14], d[15]) };
    juce::uint32 value = 0;

    for (auto l : longs)
        value = value * 31 + l;

    return (int) value;
}

juce::String plainName (const juce::String& s)
{
    return s.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
}
} // namespace

std::optional<juce::PluginDescription> PluginHost::findForeign (const ForeignPlugin& plugin, bool instrument, juce::String* how)
{
    auto types = known.getTypes(); // a copy: a background scan may add types meanwhile
    // entries whose file is gone (a plugin moved to another folder by its installer keeps a stale entry until the next
    // full scan) go last: the same class ID found at the old path would load nothing ("no compatible plug-in format")
    std::stable_partition (types.begin(), types.end(), [this] (const juce::PluginDescription& d) { return fileStillThere (d.fileOrIdentifier); });
    // a module the user chose not to wait for (round 121): its plugins are missing for this open (substitutes play)
    types.removeIf ([] (const juce::PluginDescription& d) { return warmup().skipped (d.fileOrIdentifier.toStdString()); });
    auto found = [how] (const juce::PluginDescription& d, const char* reason) {
        if (how != nullptr)
            *how = reason;
        return std::optional<juce::PluginDescription> (d);
    };
    auto fileName = plainName (juce::File::createFileWithoutCheckingPath (
        juce::String::fromUTF8 (plugin.path.c_str()).replaceCharacter ('\\', '/')).getFileNameWithoutExtension());
    auto name = plainName (juce::String::fromUTF8 (plugin.name.c_str()));
    // a Waves shell holds many plugins: its file name says nothing; the sub-plugin's name is in its state
    const auto shell = name.startsWith ("waveshell") || fileName.startsWith ("waveshell");
    if (shell)
    {
        fileName = {};
        name = plainName (juce::String::fromUTF8 (wavesPluginName (pluginIdentity (plugin).state).c_str()));
    }
    // a list entry whose file is gone (an uninstalled older version next to the current one, round 113) never matches:
    // loading it fails and the slot would play nothing
    // (cached per open: a Waves shell is ~600 entries with the same file, asked for every slot — 0.6 s of file checks)
    auto present = [this] (const juce::PluginDescription& d) { return fileStillThere (d.fileOrIdentifier); };
    // 1) the same plugin, by its format identifier
    for (const auto& d : types)
    {
        if (! present (d))
            continue;
        if (plugin.format == ForeignPlugin::Format::vst3 && plugin.hasClassId && d.pluginFormatName == "VST3"
            && (d.deprecatedUid == vst3Hash (plugin.classId) || d.uniqueId == vst3NormalisedHash (plugin.classId)))
            return found (d, TR_NOOP ("same VST3 class ID"));
        if (plugin.format == ForeignPlugin::Format::vst2 && plugin.vst2Id != 0 && d.pluginFormatName == Vst2PluginFormat::formatName
            && (juce::uint32) d.uniqueId == plugin.vst2Id)
            return found (d, TR_NOOP ("same VST2 ID"));
        if (plugin.format == ForeignPlugin::Format::clap && ! plugin.clapId.empty() && d.pluginFormatName == ClapPluginFormat::formatName
            && d.uniqueId == juce::String::fromUTF8 (plugin.clapId.c_str()).hashCode())
            return found (d, TR_NOOP ("same CLAP id"));
    }
    // 2) the file FL Studio uses for it on this PC (its plugin database), or the very file the project saved
    auto sameFile = [&] (const std::string& path) -> const juce::PluginDescription* {
       #if JUCE_WINDOWS
        const auto p = juce::String::fromUTF8 (path.c_str()).replaceCharacter ('/', '\\');
       #else
        const auto p = juce::String::fromUTF8 (path.c_str());
       #endif
        if (p.isEmpty() || ! juce::File::isAbsolutePath (p))
            return nullptr;
        const juce::File file (p);
        for (const auto& d : types)
            if (d.isInstrument == instrument && juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (d.fileOrIdentifier) == file && present (d))
                return &d;
        return nullptr;
    };
    // (not for a Waves shell: its file holds many plugins, the first one of the file would be the wrong one)
    if (! shell)
    {
        if (const auto* entry = flPluginDatabase().match (plugin, instrument))
            if (const auto* d = sameFile (entry->path))
                return found (*d, TR_NOOP ("FL Studio's plugin database"));
        if (const auto* d = sameFile (plugin.path))
            return found (*d, TR_NOOP ("same file as in the project"));
    }
    // 3) fallback, since FL swaps VST2 <-> VST3: same name (plugin or file), same role
    if (name.isEmpty() && fileName.isEmpty())
        return std::nullopt;
    // a plugin installed in several formats: prefer the one FL used (VST3 → VST3, CLAP → CLAP), whose state carries over
    const auto sameFormat = plugin.format == ForeignPlugin::Format::vst3   ? juce::String ("VST3")
                            : plugin.format == ForeignPlugin::Format::clap ? juce::String (ClapPluginFormat::formatName)
                            : plugin.format == ForeignPlugin::Format::vst2 ? juce::String (Vst2PluginFormat::formatName)
                                                                           : juce::String();
    std::optional<juce::PluginDescription> byName;
    for (const auto& d : types)
    {
        if (d.isInstrument != instrument || ! present (d))
            continue;
        const auto candidate = plainName (d.name);
        const auto candidateFile = plainName (juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier).getFileNameWithoutExtension());
        if (shell)
        {
            // "RComp" -> "RComp Stereo" / "RComp Mono"; the project's channel layout isn't known: stereo first
            if (name.isNotEmpty() && (candidate == name + "stereo" || candidate == name))
                return found (d, TR_NOOP ("same Waves plugin"));
            if (name.isNotEmpty() && candidate == name + "mono" && ! byName)
                byName = d;
            continue;
        }
        if ((name.isNotEmpty() && (candidate == name || candidateFile == name))
            || (fileName.isNotEmpty() && (candidate == fileName || candidateFile == fileName)))
        {
            if (d.pluginFormatName == sameFormat)
                return found (d, TR_NOOP ("same name"));
            if (! byName)
                byName = d;
        }
    }
    if (byName && how != nullptr)
        *how = TR_NOOP ("same name, other format");
    return byName;
}

bool PluginHost::isInstalled (const PluginRef& ref)
{
    if (ref.id.empty() || playedElsewhere (ref)) // (another participant of the live session plays it: P5-A1)
        return false;
    const auto d = find (juce::String::fromUTF8 (ref.id.c_str()));
    return d != nullptr && fileStillThere (d->fileOrIdentifier);
}

// --- Online Sessions P5-A1: plugins the host shares (core/RemotePlugins.h) ------------------------------------------

namespace
{
bool sharedParameter (const juce::AudioProcessorParameter& p)
{
    // (a JUCE-built plugin lists 16 x 128 "MIDI CC" parameters: not something to show a guest)
    return p.isAutomatable() && ! p.getName (32).startsWith ("MIDI CC ");
}
} // namespace

std::optional<remoteplug::Shared> PluginHost::describeForSharing (const std::string& id, juce::String* error)
{
    const auto description = find (juce::String::fromUTF8 (id.c_str()));
    if (description == nullptr || ! fileStillThere (description->fileOrIdentifier))
    {
        if (error != nullptr)
            *error = "plugin not installed or not scanned";
        return std::nullopt;
    }
    PluginRef ref;
    ref.id = id;
    ref.name = description->name.toStdString();
    juce::String why;
    auto instance = createInstance (ref, 44100.0, &why);
    if (instance == nullptr)
    {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    }
    remoteplug::Shared shared;
    shared.id = id;
    shared.name = description->name.toStdString();
    shared.vendor = description->manufacturerName.toStdString();
    shared.format = description->pluginFormatName.toStdString();
    shared.instrument = description->isInstrument;
    const auto& params = instance->getParameters();
    for (int i = 0; i < params.size() && shared.params.size() < remoteplug::maxParams; ++i)
    {
        auto* p = params.getUnchecked (i);
        if (p == nullptr || ! sharedParameter (*p))
            continue;
        remoteplug::Param out;
        out.index = i;
        out.name = p->getName (48).toStdString();
        out.unit = p->getLabel().substring (0, 12).toStdString();
        // (what a fresh instance has — the sound is "a fresh instance plus the values" —, which is not always the
        // default the plugin declares)
        out.def = juce::jlimit (0.0f, 1.0f, p->getValue());
        out.low = p->getText (0.0f, 24).toStdString();
        out.high = p->getText (1.0f, 24).toStdString();
        const auto steps = p->getNumSteps();
        if (p->isDiscrete() && steps >= 2 && steps <= 1 << 16)
        {
            out.steps = steps;
            if (steps <= remoteplug::maxSteps)
                for (int k = 0; k < steps; ++k)
                    out.stepNames.push_back (p->getText ((float) k / (float) (steps - 1), 24).toStdString());
        }
        shared.params.push_back (std::move (out));
    }
    instance->releaseResources();
    instance.reset();
    remoteplug::clean (shared);
    return shared;
}

void PluginHost::applyValues (juce::AudioPluginInstance& plugin, const std::vector<std::pair<int, float>>& values)
{
    const auto& params = plugin.getParameters();
    for (const auto& [index, value] : values)
        if (index >= 0 && index < params.size())
            if (auto* p = params.getUnchecked (index); p != nullptr && sharedParameter (*p))
                p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
}

std::vector<std::pair<int, float>> PluginHost::readValues (juce::AudioPluginInstance& plugin, const remoteplug::Shared* fresh)
{
    std::vector<std::pair<int, float>> out;
    std::map<int, float> base;
    if (fresh != nullptr)
        for (const auto& param : fresh->params)
            base[param.index] = param.def;
    const auto& params = plugin.getParameters();
    for (int i = 0; i < params.size() && out.size() < remoteplug::maxParams; ++i)
        if (auto* p = params.getUnchecked (i); p != nullptr && sharedParameter (*p))
        {
            const auto v = juce::jlimit (0.0f, 1.0f, p->getValue());
            const auto it = base.find (i);
            if (std::abs (v - (it != base.end() ? it->second : juce::jlimit (0.0f, 1.0f, p->getDefaultValue()))) > 1.0e-4f)
                out.push_back ({ i, v });
        }
    return out;
}

std::optional<PluginHost::Identity> PluginHost::portableIdentity (const PluginRef& ref) const
{
    const auto d = find (juce::String::fromUTF8 (ref.id.c_str()));
    if (d == nullptr)
        return std::nullopt;
    Identity id;
    id.ref = juce::String::fromUTF8 (ref.id.c_str());
    id.name = d->name;
    id.format = d->pluginFormatName;
    id.manufacturer = d->manufacturerName;
    id.version = d->version;
    id.file = juce::File::createFileWithoutCheckingPath (d->fileOrIdentifier).getFileName();
    id.uniqueId = d->uniqueId;
    id.deprecatedUid = d->deprecatedUid;
    id.instrument = d->isInstrument;
    return id;
}

std::optional<juce::PluginDescription> PluginHost::findByIdentity (const Identity& identity, bool* sameFormat)
{
    const auto types = known.getTypes();
    for (const auto& d : types)
        if (d.pluginFormatName == identity.format && fileStillThere (d.fileOrIdentifier)
            && ((identity.uniqueId != 0 && d.uniqueId == identity.uniqueId) || (identity.deprecatedUid != 0 && d.deprecatedUid == identity.deprecatedUid))
            && (identity.name.isEmpty() || plainName (d.name) == plainName (identity.name)))
        {
            if (sameFormat != nullptr)
                *sameFormat = true;
            return d;
        }
    for (const auto& d : types)
        if (d.isInstrument == identity.instrument && fileStillThere (d.fileOrIdentifier) && plainName (d.name) == plainName (identity.name)
            && (identity.manufacturer.isEmpty() || plainName (d.manufacturerName) == plainName (identity.manufacturer)))
        {
            if (sameFormat != nullptr)
                *sameFormat = d.pluginFormatName == identity.format;
            return d;
        }
    return std::nullopt;
}

bool PluginHost::fileStillThere (const juce::String& file)
{
    if (! juce::File::isAbsolutePath (file))
        return true;
    const std::scoped_lock lock (existsLock);
    if (const auto it = existsCache.find (file); it != existsCache.end())
        return it->second;
    return existsCache[file] = juce::File (file).exists();
}

juce::StringArray PluginHost::filesToScanFor (const std::vector<FlpPluginUse>& uses)
{
    juce::StringArray files;
    std::vector<std::pair<const FlpPluginUse*, const FlPluginDbEntry*>> needed;
    for (const auto& use : uses)
        if (! findForeign (use.plugin, use.instrument))
            needed.push_back ({ &use, flPluginDatabase().match (use.plugin, use.instrument) });
    if (needed.empty())
        return files;

    auto hosted = [] (const juce::String& path) {
        return path.endsWithIgnoreCase (".vst3") || path.endsWithIgnoreCase (".clap") || path.endsWithIgnoreCase (".dll")
            || path.endsWithIgnoreCase (".component") || path.endsWithIgnoreCase (".lv2");
    };
    auto addExisting = [&] (const std::string& path) {
       #if JUCE_WINDOWS
        const auto p = juce::String::fromUTF8 (path.c_str()).replaceCharacter ('/', '\\');
       #else
        const auto p = juce::String::fromUTF8 (path.c_str());
       #endif
        if (p.isNotEmpty() && juce::File::isAbsolutePath (p) && hosted (p) && juce::File (p).exists())
            files.addIfNotAlreadyThere (juce::File (p).getFullPathName(), true);
    };
    for (const auto& [use, entry] : needed)
    {
        addExisting (use->plugin.path);
        if (entry != nullptr)
            addExisting (entry->path);
    }
    // same-named plugin files in the scan folders (listing only: nothing is loaded here)
    const auto folders = pluginFolders();
    for (const auto& file : listPluginFiles (formats, folders))
        for (const auto& [use, entry] : needed)
            if (pluginFileMatches (file.toStdString(), use->plugin, entry))
                files.addIfNotAlreadyThere (file, true);
    // known to crash or hang: nothing to learn
    juce::StringArray worth;
    for (const auto& file : files)
        if (! known.getBlacklistedFiles().contains (file))
            worth.add (file);
    worth.removeRange (32, worth.size()); // a project rarely needs more; the rest waits for the background scan
    return worth;
}

std::string PluginHost::stateFromFlWrapper (const ForeignPlugin& f, const juce::PluginDescription& d)
{
    // CLAP: u32 0 + the clap_plugin_state.save bytes (REA_rodada22 §2); LYNX's CLAP host stores the same
    if (f.format == ForeignPlugin::Format::clap && d.pluginFormatName == "CLAP")
        return f.state.size() > 4 ? juce::MemoryBlock (f.state.data() + 4, f.state.size() - 4).toBase64Encoding().toStdString() : std::string {};

    // VST2: LYNX's VST2 host keeps FL's own state layout (REA_rodada89 §2)
    if (f.format == ForeignPlugin::Format::vst2 && d.pluginFormatName == Vst2PluginFormat::formatName)
    {
        FlVst2State check;
        return parseFlVst2State (f.state, check) ? juce::MemoryBlock (f.state.data(), f.state.size()).toBase64Encoding().toStdString()
                                                 : std::string {};
    }

    if (f.format != ForeignPlugin::Format::vst3 || d.pluginFormatName != "VST3")
        return {}; // VST2 <-> VST3: different state formats; plugin at defaults

    std::vector<std::uint8_t> component, controller;

    if (! splitFlVst3State (f.state, component, controller))
        return {};

    juce::XmlElement xml ("VST3PluginState");

    if (! component.empty())
        xml.createNewChildElement ("IComponent")->addTextElement (juce::MemoryBlock (component.data(), component.size()).toBase64Encoding());
    if (! controller.empty())
        xml.createNewChildElement ("IEditController")->addTextElement (juce::MemoryBlock (controller.data(), controller.size()).toBase64Encoding());

    juce::MemoryBlock binary;
    juce::AudioProcessor::copyXmlToBinary (xml, binary);
    return binary.toBase64Encoding().toStdString();
}

int PluginHost::adoptFlPlugins (Song& song, juce::StringArray* missing, juce::StringArray* matched, juce::StringArray* withoutState,
                                juce::StringArray* substituted)
{
    int found = 0;

    auto convertState = [] (const ForeignPlugin& f, const juce::PluginDescription& d) { return stateFromFlWrapper (f, d); };

    auto adopt = [&] (PluginRef& ref, bool instrument) {
        juce::String how;
        const auto d = findForeign (ref.foreign, instrument, &how);

        if (! d)
        {
            if (missing != nullptr)
                missing->addIfNotAlreadyThere (juce::String::fromUTF8 (ref.foreign.name.c_str()));
            ref.foreign = {};
            return false;
        }

        if (matched != nullptr)
            matched->addIfNotAlreadyThere (juce::String::fromUTF8 (ref.foreign.name.c_str()) + " -> " + d->fileOrIdentifier + " (" + tr (how) + ")");
        ref.id = d->createIdentifierString().toStdString();
        ref.name = d->name.toStdString();
        ref.state = convertState (ref.foreign, *d);
        // found, but the project's settings could not be carried over: it plays its default sound
        if (ref.state.empty() && ! ref.foreign.state.empty() && withoutState != nullptr)
            withoutState->addIfNotAlreadyThere (juce::String::fromUTF8 (ref.foreign.name.c_str()) + " (" + d->pluginFormatName + ")");
        ref.foreign = {};
        ++found;
        return true;
    };

    // FL automates a VST3 by "slot" (record 4: slot → ParamID); LYNX by position in JUCE's list, which is
    // getParameterInfo order (REA_rodada22 §2.3). Needs the plugin's current list: only when something is
    // automated, in-process (skipped with the sandbox on, where k stays the position — FL's default for a new instance).
    // (the instance it asks for stays ready for the rack: the same plugin is not created twice on open)
    auto remapVst3Slots = [&] (const ForeignPlugin& f, const juce::PluginDescription& d, const PluginRef& ref, auto&& isThisPlugin) {
        if (f.format != ForeignPlugin::Format::vst3 || d.pluginFormatName != "VST3" || pluginSandboxEnabled())
            return;
        const auto saved = flVst3ParamSlots (f.state);
        auto automated = false;
        song.forEachTarget ([&] (AutomationTarget& t) { automated = automated || isThisPlugin (t); });
        if (saved.empty() || ! automated)
            return;

        const loadtrace::Scope timing ("plugin_vst3_remap");
        juce::String error;
        auto instance = createInstance (ref, preloadRate, &error);
        if (instance == nullptr)
        {
            const std::scoped_lock lock (preloadLock);
            preloaded[preloadKey (ref, preloadRate)].push_back ({ nullptr, error });
            return;
        }
        struct Keep
        {
            PluginHost& host;
            const PluginRef& ref;
            std::unique_ptr<juce::AudioPluginInstance>& instance;
            ~Keep()
            {
                const std::scoped_lock lock (host.preloadLock);
                host.preloaded[preloadKey (ref, host.preloadRate)].push_back ({ std::move (instance), {} });
            }
        } keep { *this, ref, instance };
        std::vector<std::uint32_t> ids;
        for (auto* p : instance->getParameters())
        {
            const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (p);
            ids.push_back (hosted != nullptr ? (std::uint32_t) hosted->getParameterID().getLargeIntValue() : 0xffffffffu);
        }
        const auto positions = flVst3SlotPositions (saved, ids);
        song.forEachTarget ([&] (AutomationTarget& t) {
            if (isThisPlugin (t) && ! midictl::isMidiParam (t.param)) // (MIDI CC indices are not slots)
                t.param = t.param >= 0 && t.param < (int) positions.size() ? positions[(std::size_t) t.param] : -1;
        });
    };

    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        auto& channel = song.channels[(std::size_t) c];
        if (channel.plugin.foreign.format == ForeignPlugin::Format::none)
            continue;
        const auto foreign = channel.plugin.foreign;
        if (adopt (channel.plugin, true))
        {
            channel.instrument = InstrumentKind::plugin;
            if (const auto d = findForeign (foreign, true))
                remapVst3Slots (foreign, *d, channel.plugin, [c] (const AutomationTarget& t) { return t.kind == AutomationTargetKind::channelParam && t.index == c; });
        }
    }

    // the song's key etc., for substitutes whose own original doesn't say (round 113)
    const auto context = substituteContextOf (song);
    // backwards: removing an unmatched slot shifts only the slots after it (their automations follow)
    for (int track = 0; track < (int) song.mixer.size(); ++track)
        for (int slot = (int) song.mixer[(std::size_t) track].effects.size() - 1; slot >= 0; --slot)
        {
            auto& fx = song.mixer[(std::size_t) track].effects[(std::size_t) slot];
            if (fx.kind != EffectKind::plugin || fx.plugin.foreign.format == ForeignPlugin::Format::none)
                continue;
            const auto foreign = fx.plugin.foreign;
            // not installed: a LYNX substitute plays in its place and keeps the original record for later
            if (pluginSubstitutesEnabled() && ! findForeign (foreign, false) && substituteEffectSlot (fx, pluginIdentity (foreign), context))
            {
                if (substituted != nullptr)
                    substituted->addIfNotAlreadyThere (juce::String::fromUTF8 (substituteOriginalName (fx).c_str()) + " -> "
                                                       + juce::String::fromUTF8 (effectInfo (fx.kind).name));
                continue;
            }
            if (! adopt (fx.plugin, false))
                song.removeEffectSlot (track, slot);
            else if (const auto d = findForeign (foreign, false))
                remapVst3Slots (foreign, *d, fx.plugin, [track, slot] (const AutomationTarget& t) {
                    return t.kind == AutomationTargetKind::effectParam && t.index == track && t.slot == slot;
                });
        }

    // the plugins inside Patchers (REA_rodada141), nested ones too: found, a substitute, or a silent / pass-through node
    std::function<void (PatcherDoc&)> adoptGraph = [&] (PatcherDoc& doc) {
        for (auto& node : doc.nodes)
            if (node.generator)
            {
                if (node.channel.plugin.foreign.format != ForeignPlugin::Format::none && adopt (node.channel.plugin, true))
                    node.channel.instrument = InstrumentKind::plugin;
                if (node.channel.instrument == InstrumentKind::patcher)
                    adoptGraph (node.channel.patcher);
            }
            else
            {
                auto& fx = node.effect;
                if (fx.kind == EffectKind::plugin && fx.plugin.foreign.format != ForeignPlugin::Format::none)
                {
                    const auto foreign = fx.plugin.foreign;
                    if (pluginSubstitutesEnabled() && ! findForeign (foreign, false) && substituteEffectSlot (fx, pluginIdentity (foreign), context))
                    {
                        if (substituted != nullptr)
                            substituted->addIfNotAlreadyThere (juce::String::fromUTF8 (substituteOriginalName (fx).c_str()) + " -> "
                                                               + juce::String::fromUTF8 (effectInfo (fx.kind).name));
                    }
                    else
                        adopt (fx.plugin, false);
                }
                if (fx.kind == EffectKind::patcher)
                    adoptGraph (fx.patcher);
            }
    };
    for (auto& channel : song.channels)
        if (channel.instrument == InstrumentKind::patcher)
            adoptGraph (channel.patcher);
    for (auto& track : song.mixer)
        for (auto& fx : track.effects)
            if (fx.kind == EffectKind::patcher)
                adoptGraph (fx.patcher);

    return found;
}

PluginIdentity PluginHost::identityOf (const PluginRef& ref)
{
    if (ref.foreign.format != ForeignPlugin::Format::none)
        return pluginIdentity (ref.foreign);

    PluginIdentity id;
    id.name = ref.name;
    // JUCE's identifier: "<format>-<name>-<file hash>-<unique id>" (hex)
    const auto text = juce::String::fromUTF8 (ref.id.c_str());
    const auto format = text.upToFirstOccurrenceOf ("-", false, false);
    id.format = format == "VST3"                           ? ForeignPlugin::Format::vst3
                : format == Vst2PluginFormat::formatName   ? ForeignPlugin::Format::vst2
                : format == ClapPluginFormat::formatName   ? ForeignPlugin::Format::clap
                                                           : ForeignPlugin::Format::other;
    if (id.format == ForeignPlugin::Format::vst2)
        id.vst2Id = (std::uint32_t) text.fromLastOccurrenceOf ("-", false, false).getHexValue64();

    juce::MemoryBlock state;
    if (ref.state.empty() || ! state.fromBase64Encoding (juce::String::fromUTF8 (ref.state.c_str())))
        return id;
    const auto* bytes = static_cast<const std::uint8_t*> (state.getData());
    if (id.format == ForeignPlugin::Format::vst3)
    {
        // JUCE's VST3 state: XML "VST3PluginState" whose IComponent element holds the component state in base64
        if (const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
            if (const auto* component = xml->getChildByName ("IComponent"))
            {
                juce::MemoryBlock raw;
                if (raw.fromBase64Encoding (component->getAllSubText().trim()))
                    id.state.assign (static_cast<const std::uint8_t*> (raw.getData()), static_cast<const std::uint8_t*> (raw.getData()) + raw.getSize());
            }
    }
    else
        id.state.assign (bytes, bytes + state.getSize()); // VST2: FL's layout (LYNX's VST2 host); CLAP: the plugin's own
    return id;
}

int PluginHost::applySubstitutes (Song& song, int* restored, int* substituted)
{
    if (restored != nullptr)
        *restored = 0;
    if (substituted != nullptr)
        *substituted = 0;
    // round 190ap: opened without third-party plugins, the slots stay exactly as the project has them: no substitute
    // takes a plugin's place and no substitute goes back to its original (that converts settings through the plugin)
    if (pluginsHeld())
        return 0;
    int back = 0, replaced = 0;
    bool foreignBack = false;
    const auto substitute = pluginSubstitutesEnabled();
    const auto context = substituteContextOf (song);
    for (auto& track : song.mixer)
        for (auto& fx : track.effects)
        {
            if (fx.isSubstitute())
            {
                const auto& original = fx.substituteFor;
                const auto installed = original.foreign.format != ForeignPlugin::Format::none
                                           ? findForeign (original.foreign, false).has_value()
                                           : find (juce::String::fromUTF8 (original.id.c_str())) != nullptr;
                if (installed)
                {
                    foreignBack = foreignBack || original.foreign.format != ForeignPlugin::Format::none;
                    restoreOriginalPlugin (fx);
                    ++back;
                }
            }
            // (an insert with a stand-in render plays that in place of its chain: no substitute; round 168a)
            else if (substitute && track.standIns.empty() && fx.kind == EffectKind::plugin && fx.plugin.foreign.format == ForeignPlugin::Format::none
                     && ! fx.plugin.id.empty() && find (juce::String::fromUTF8 (fx.plugin.id.c_str())) == nullptr)
            {
                if (substituteEffectSlot (fx, identityOf (fx.plugin), context))
                    ++replaced;
            }
        }
    // originals that came from an .flp: their FL record still needs converting to the installed plugin's state
    if (foreignBack)
        adoptFlPlugins (song);
    if (restored != nullptr)
        *restored = back;
    if (substituted != nullptr)
        *substituted = replaced;
    return back + replaced;
}

bool pluginSubstitutesEnabled()
{
    return PluginHost::settingsFolder().getChildFile ("substitutes.txt").loadFileAsString().trim() != "0";
}

void setPluginSubstitutesEnabled (bool enabled)
{
    PluginHost::settingsFolder().getChildFile ("substitutes.txt").replaceWithText (enabled ? "1" : "0");
}

std::string PluginHost::preloadKey (const PluginRef& ref, double sampleRate)
{
    return ref.id + "|" + std::to_string (std::hash<std::string> {} (ref.state)) + "|" + std::to_string (ref.state.size()) + "|"
           + std::to_string ((long long) std::llround (sampleRate)) + (ref.separateProcess ? "|own process" : "")
           + (ref.bitSize != 0 ? "|bits" + std::to_string (ref.bitSize) : "");
}

void PluginHost::preload (const PluginRef& ref, int wanted)
{
    if (ref.id.empty() || pluginHeld (ref.id)) // (switched off: nothing to make ready, round 190ap)
        return;
    {
        const std::scoped_lock lock (preloadLock);
        if (const auto it = preloaded.find (preloadKey (ref, preloadRate)); it != preloaded.end() && (int) it->second.size() >= wanted)
            return;
    }
    Preloaded p;
    p.instance = createInstance (ref, preloadRate, &p.error);
    const std::scoped_lock lock (preloadLock);
    preloaded[preloadKey (ref, preloadRate)].push_back (std::move (p));
}

void PluginHost::clearPreloaded()
{
    {
        const std::scoped_lock lock (existsLock);
        existsCache.clear(); // (asked again on the next open)
    }
    std::map<std::string, std::vector<Preloaded>> old;
    {
        const std::scoped_lock lock (preloadLock);
        old.swap (preloaded);
    }
    // (the unused instances are released here, outside the lock)
}

int PluginHost::preloadedCount() const
{
    const std::scoped_lock lock (preloadLock);
    int n = 0;
    for (const auto& [key, list] : preloaded)
        n += (int) list.size();
    return n;
}

namespace
{
std::mutex& holdLock()
{
    static std::mutex m;
    return m;
}
pluginhold::State& holdState()
{
    static pluginhold::State s;
    return s;
}
juce::String heldMessage (const PluginRef& ref)
{
    return juce::String::fromUTF8 (ref.name.c_str()) + ": " + tr ("switched off (the project was opened without third-party plugins)");
}
} // namespace

bool pluginsHeld()
{
    const std::scoped_lock lock (holdLock());
    return holdState().active;
}

bool pluginHeld (const std::string& id)
{
    const std::scoped_lock lock (holdLock());
    return holdState().holds (id);
}

pluginhold::State pluginHoldState()
{
    const std::scoped_lock lock (holdLock());
    return holdState();
}

void holdPlugins()
{
    const std::scoped_lock lock (holdLock());
    holdState().start();
}

void releasePlugin (const std::string& id)
{
    const std::scoped_lock lock (holdLock());
    holdState().release (id);
}

void endPluginHold()
{
    const std::scoped_lock lock (holdLock());
    holdState().end();
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::instantiate (const PluginRef& ref, double sampleRate, juce::String* error)
{
    // round 190ap: a plugin that is switched off is not created, and no instance made earlier is handed out for it
    if (pluginHeld (ref.id))
    {
        if (error != nullptr)
            *error = heldMessage (ref);
        return nullptr;
    }
    if (playedElsewhere (ref)) // Online Sessions P5-A1: the session's host plays it; here its stand-ins play
    {
        if (error != nullptr)
            *error = juce::String::fromUTF8 (ref.name.c_str()) + ": played by another computer in the live session";
        return nullptr;
    }
    auto instance = instantiateOnce (ref, sampleRate, error);
    // a hosted plugin's sound is its (default) state plus the parameter values the session keeps for it
    if (instance != nullptr && ! ref.values.empty())
        applyValues (*instance, ref.values);
    return instance;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::instantiateOnce (const PluginRef& ref, double sampleRate, juce::String* error)
{
    {
        const std::scoped_lock lock (preloadLock);
        if (auto it = preloaded.find (preloadKey (ref, sampleRate)); it != preloaded.end() && ! it->second.empty())
        {
            auto p = std::move (it->second.back());
            it->second.pop_back();
            if (it->second.empty())
                preloaded.erase (it);
            if (p.instance == nullptr && error != nullptr)
                *error = p.error;
            return std::move (p.instance);
        }
    }
    return createInstance (ref, sampleRate, error);
}

std::unique_ptr<juce::AudioPluginInstance> createRightInstance (juce::AudioPluginFormatManager& formats, const juce::PluginDescription& description,
                                                                double sampleRate, int blockSize, juce::String& error)
{
    auto instance = formats.createPluginInstance (description, sampleRate, blockSize, error);
    // A shell that hands out another plugin than the one asked for: a Waves shell's FIRST instance in a process comes
    // up as another of its plugins (round 119 saw Q10; round 121 §3: "SSLChannel Stereo" came up as "JJP-Guitars
    // Mono/Stereo") — its class list is reordered (587 -> 1174 entries) when it gets a host context, which JUCE sets
    // between its class lookup (by index) and the creation. Created again, it is the right one.
    if (instance == nullptr || instance->getName() == description.name)
        return instance;
    loadtrace::add ("plugin_mismatch " + description.name.toStdString() + " -> " + instance->getName().toStdString(), 0.0);
    if (description.pluginFormatName != "VST3")
        return instance;
    // the wrong ones stay alive until the right one exists (released first, the shell went back to its first list)
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> wrong;
    for (int attempt = 0; attempt < 3 && instance != nullptr && instance->getName() != description.name; ++attempt)
    {
        wrong.push_back (std::move (instance));
        instance = formats.createPluginInstance (description, sampleRate, blockSize, error);
        if (instance != nullptr && instance->getName() != description.name)
            loadtrace::add ("plugin_mismatch_again " + description.name.toStdString() + " -> " + instance->getName().toStdString(), 0.0);
    }
    if (instance != nullptr && instance->getName() != description.name)
    {
        // another plugin playing in its place would sound wrong without a word: a load failure is said
        error = description.name + ": " + tr ("the plugin file handed out another plugin ({0})", { instance->getName() });
        return nullptr;
    }
    return instance;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const PluginRef& ref, double sampleRate, juce::String* error)
{
    // round 190ap: switched off ("Open without third-party plugins"): before anything of the plugin is looked up,
    // waited for or loaded (every creation of a project's plugin comes through here)
    if (pluginHeld (ref.id))
    {
        if (error != nullptr)
            *error = heldMessage (ref);
        return nullptr;
    }
    if (onInstantiate)
        onInstantiate (juce::String::fromUTF8 (ref.name.c_str()));
    const loadtrace::Scope timing ("plugin_instantiate");
    const loadtrace::Scope each ("plugin " + ref.name); // per plugin in load.log: which one is slow
    const auto description = buildFor (ref); // (the 32-bit or the 64-bit build: "Bit size", round 176a)

    if (description == nullptr)
    {
        if (error != nullptr)
            *error = juce::String::fromUTF8 (ref.name.c_str()) + juce::String::fromUTF8 (": plugin not installed or not scanned");
        return nullptr;
    }

    // a module still starting ahead on its own thread (round 121): wait for it here rather than enter it from two
    // threads at once (the open's panel waits without blocking before it gets here; this is for anything else)
    for (const auto until = std::chrono::steady_clock::now() + std::chrono::minutes (3);
         warmup().state (description->fileOrIdentifier.toStdString()) == modulewarm::State::warming && std::chrono::steady_clock::now() < until;)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));

    loadedAny.store (true); // (even a failed load may leave the module mapped)
    juce::String message;
    // the block size the plugin is told (FL's wrapper, REA rounds 134/139): with fixed size buffers it is only ever
    // called with that block, so it is prepared for it — unless "Use maximum buffer size from host" is on
    const auto preparedBlock = ref.fixedBuffers && ! ref.hostMaxBuffer ? fixedPluginBlock (ref.maxSizeBuffers) : maxBlockSize;
    const auto bits32 = isPlugin32Bit (*description);
    // the session's plugin list (round 159: an opt-in crash report names the third-party plugins that were loaded)
    crashguard::notePlugin ((description->name + "|" + description->manufacturerName + "|" + description->pluginFormatName + "|" + description->version
                             + (bits32 ? "|32-bit bridge" : (pluginSandboxEnabled() || ref.separateProcess || pluginAlwaysSeparate (description->name) ? "|separate process" : "")))
                                .toStdString());
    // a 32-bit plugin always runs in the 32-bit bridge (round 176a; FL bridges them the same way, whatever "Make bridged" says)
    if (bits32 || pluginSandboxEnabled() || ref.separateProcess || pluginAlwaysSeparate (description->name))
    {
        // separate process (for every plugin, or this one only — FL's "Make bridged"): the child prepares the plugin and applies the state
        if (auto remote = createSandboxedPlugin (*description, sampleRate, preparedBlock, ref.state, message))
            return remote;
        if (error != nullptr)
            *error = juce::String::fromUTF8 (ref.name.c_str()) + ": " + message;
        return nullptr;
    }

    crashguard::note (("loading plugin " + description->name + " (" + description->pluginFormatName + ", "
                       + juce::File (description->fileOrIdentifier).getFileName() + ")").toStdString());
    auto instance = createRightInstance (formats, *description, sampleRate, maxBlockSize, message);
    crashguard::note (("loaded plugin " + description->name + (instance == nullptr ? juce::String (" (failed)") : juce::String())).toStdString());

    if (instance == nullptr)
    {
        if (error != nullptr)
            *error = message;
        return nullptr;
    }

    preparePlugin (*instance, sampleRate, preparedBlock);

    if (! ref.state.empty())
    {
        juce::MemoryBlock state;

        if (state.fromBase64Encoding (juce::String::fromUTF8 (ref.state.c_str())))
        {
            // round 190dt: guarded like the audio calls (a plugin that crashes on its own saved settings took LYNX down
            // while the project opened). It is left out: the slot keeps those settings, so the project saves them.
            auto* raw = instance.get();
            if (const auto code = crashguard::run ([raw, &state] { raw->setStateInformation (state.getData(), (int) state.getSize()); }); code != 0)
            {
                (void) instance.release(); // never called again, not even to delete it (as after a crash in process)
                recordCrash (description->name.toStdString(), code, true);
                crashguard::note (("plugin " + description->name + " crashed while loading its saved settings (" + crashguard::describe (code)
                                   + "): left out").toStdString());
                if (error != nullptr)
                    *error = tr ("{0} crashed and was switched off.", { juce::String::fromUTF8 (ref.name.c_str()) }) + " ("
                             + crashguard::describe (code) + ")";
                return nullptr;
            }
        }
    }

    return instance;
}

// --- slow modules started ahead (round 121) -------------------------------------------------------------------------
namespace
{
#if JUCE_WINDOWS
// The two calls needed from a VST3 factory, declared by their place in the interface (the VST3 ABI: FUnknown's three,
// then getFactoryInfo, countClasses). No SDK header here: JUCE keeps its copy private to its own module.
struct FactoryCalls
{
    virtual long __stdcall queryInterface (const char*, void**) = 0;
    virtual unsigned long __stdcall addRef() = 0;
    virtual unsigned long __stdcall release() = 0;
    virtual long __stdcall getFactoryInfo (void*) = 0;
    virtual int __stdcall countClasses() = 0;
};


struct StartedModule
{
    HMODULE module = nullptr;
    FactoryCalls* factory = nullptr;
};

std::mutex startedLock;
std::vector<StartedModule>& startedModules()
{
    static auto* list = new std::vector<StartedModule>(); // kept for the session (never torn down: round 111)
    return *list;
}

// What JUCE's VST3 host does first, on this thread: load the module, its entry point, its factory, the class count.
// The factory is kept: JUCE's later GetPluginFactory gets the same, already listed one.
bool loadModuleAhead (const std::string& path)
{
    juce::File file (juce::String::fromUTF8 (path.c_str()));
    if (file.isDirectory()) // a bundle folder
        file = file.getChildFile ("Contents").getChildFile ("x86_64-win").getChildFile (file.getFileName());
    const auto module = LoadLibraryW (file.getFullPathName().toWideCharPointer());
    if (module == nullptr)
        return false;
    if (auto* entry = (bool (*)()) (void*) GetProcAddress (module, "InitDll"); entry != nullptr && ! entry())
    {
        FreeLibrary (module);
        return false;
    }
    auto* getFactory = (FactoryCalls* (*)()) (void*) GetProcAddress (module, "GetPluginFactory");
    auto* factory = getFactory != nullptr ? getFactory() : nullptr;
    if (factory == nullptr)
    {
        if (auto* exit = (bool (*)()) (void*) GetProcAddress (module, "ExitDll"))
            exit();
        FreeLibrary (module);
        return false;
    }
    factory->countClasses(); // the slow part (a Waves shell: its service, its plugin list)
    const std::scoped_lock l (startedLock);
    startedModules().push_back ({ module, factory });
    return true;
}
#endif

modulewarm::Registry& warmup()
{
   #if JUCE_WINDOWS
    static modulewarm::Registry registry (loadModuleAhead);
   #else
    static modulewarm::Registry registry ([] (const std::string&) { return true; }, {}, false); // (nothing slow known)
   #endif
    return registry;
}

void startModuleOf (const juce::PluginDescription& d)
{
    if (pluginSandboxEnabled() || std::getenv ("LYNX_NO_MODULE_WARMUP") != nullptr) // (the env var: A/B measurements)
        return;
    if (isPlugin32Bit (d)) // (it loads in the 32-bit bridge, never in LYNX's own process)
        return;
    if (modulewarm::worthStartingAhead (d.pluginFormatName.toStdString(), d.fileOrIdentifier.toStdString()))
        warmup().start (d.fileOrIdentifier.toStdString());
}
} // namespace

void PluginHost::startModulesFor (const std::vector<FlpPluginUse>& uses)
{
    if (pluginsHeld()) // (opened without third-party plugins: no module is started ahead, round 190ap)
        return;
    for (const auto& u : uses)
        if (const auto d = findForeign (u.plugin, u.instrument))
            startModuleOf (*d);
}

void PluginHost::startModulesFor (const std::vector<PluginRef>& refs)
{
    for (const auto& ref : refs)
        if (! ref.separateProcess && ! ref.id.empty() && ! pluginHeld (ref.id))
            if (const auto d = buildFor (ref))
                startModuleOf (*d);
}

modulewarm::Waiting PluginHost::modulesStarting()
{
    return warmup().waiting();
}

void PluginHost::skipStartingModules()
{
    warmup().skipWaiting();
}

void PluginHost::newOpen()
{
    warmup().newOpen();
}

namespace
{
juce::String failureLine (const PluginRef& ref, const juce::String& why)
{
    const auto name = juce::String::fromUTF8 (ref.name.c_str());
    if (why.isEmpty())
        return name + ": " + tr ("the plugin could not start");
    return why.startsWith (name) ? why : name + ": " + why;
}
} // namespace

juce::StringArray PluginHost::getLoadFailures() const
{
    const std::scoped_lock lock (failuresLock);
    return loadFailures;
}

void PluginHost::clearLoadFailures()
{
    const std::scoped_lock lock (failuresLock);
    loadFailures.clear();
}

std::shared_ptr<Instrument> PluginHost::makeInstrument (const PluginRef& ref, double sampleRate, juce::String* error)
{
    if (playedElsewhere (ref)) // (P5-A1: not a load failure — the host of the live session plays it)
        return nullptr;
    juce::String why;
    if (auto instance = instantiate (ref, sampleRate, &why))
        return std::make_shared<PluginInstrument> (std::move (instance), maxBlockSize, ref.fixedBuffers ? fixedPluginBlock (ref.maxSizeBuffers) : 0,
                                                   HostedOptions::of (ref));

    if (! pluginHeld (ref.id)) // (switched off on purpose is not a load failure, round 190ap)
    {
        const std::scoped_lock lock (failuresLock);
        loadFailures.addIfNotAlreadyThere (failureLine (ref, why));
    }
    if (error != nullptr)
        *error = why;
    return nullptr;
}

std::shared_ptr<Effect> PluginHost::makeEffect (const PluginRef& ref, double sampleRate, juce::String* error)
{
    if (playedElsewhere (ref)) // (P5-A1: not a load failure — the host of the live session plays it)
        return nullptr;
    juce::String why;
    if (auto instance = instantiate (ref, sampleRate, &why))
        return std::make_shared<PluginEffect> (std::move (instance), maxBlockSize, ref.fixedBuffers ? fixedPluginBlock (ref.maxSizeBuffers) : 0,
                                               HostedOptions::of (ref));

    if (! pluginHeld (ref.id)) // (switched off on purpose is not a load failure, round 190ap)
    {
        const std::scoped_lock lock (failuresLock);
        loadFailures.addIfNotAlreadyThere (failureLine (ref, why));
    }
    if (error != nullptr)
        *error = why;
    return nullptr;
}

std::string capturePluginState (juce::AudioPluginInstance& plugin)
{
    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    return state.toBase64Encoding().toStdString();
}

// ---- Export to FL Studio (round 171e) ----
namespace
{
std::vector<std::uint8_t> blockBytes (const juce::MemoryBlock& m)
{
    const auto* b = static_cast<const std::uint8_t*> (m.getData());
    return std::vector<std::uint8_t> (b, b + m.getSize());
}

std::vector<std::uint8_t> juceStateBytes (const std::string& base64)
{
    juce::MemoryBlock m;
    if (base64.empty() || ! m.fromBase64Encoding (juce::String::fromUTF8 (base64.c_str())))
        return {};
    return blockBytes (m);
}

#if JUCE_WINDOWS
// IPluginFactory's leading calls by their vtable place (FUnknown's three, getFactoryInfo, countClasses, getClassInfo)
struct FactoryClasses
{
    virtual long __stdcall queryInterface (const char*, void**) = 0;
    virtual unsigned long __stdcall addRef() = 0;
    virtual unsigned long __stdcall release() = 0;
    virtual long __stdcall getFactoryInfo (void*) = 0;
    virtual int __stdcall countClasses() = 0;
    virtual long __stdcall getClassInfo (int index, void* info) = 0;
};
struct ClassInfo // PClassInfo
{
    char cid[16];
    int cardinality;
    char category[32];
    char name[64];
};
#endif

// The VST3 class TUID of a scanned VST3 (JUCE keeps only its hash): FL's plugin database for the same file, else the
// module's own class list (the class whose TUID hashes to the description's deprecatedUid)
std::optional<std::array<std::uint8_t, 16>> vst3ClassIdOf (const juce::PluginDescription& d, const FlPluginDb& db)
{
    auto matches = [&d] (const std::array<std::uint8_t, 16>& id) { return vst3Hash (id) == d.deprecatedUid || vst3NormalisedHash (id) == d.uniqueId; };
    ForeignPlugin probe;
    probe.format = ForeignPlugin::Format::vst3;
    probe.name = d.name.toStdString();
    probe.path = d.fileOrIdentifier.toStdString();
    if (const auto* entry = db.match (probe, d.isInstrument); entry != nullptr && entry->hasClassId && matches (entry->classId))
        return entry->classId;
   #if JUCE_WINDOWS
    juce::File file (d.fileOrIdentifier);
    if (file.isDirectory())
        file = file.getChildFile ("Contents").getChildFile ("x86_64-win").getChildFile (file.getFileName());
    const auto module = LoadLibraryW (file.getFullPathName().toWideCharPointer());
    if (module == nullptr)
        return std::nullopt;
    std::optional<std::array<std::uint8_t, 16>> found;
    auto* init = (bool (*)()) (void*) GetProcAddress (module, "InitDll");
    const auto started = init == nullptr || init();
    if (started)
        if (auto* get = (FactoryClasses* (*)()) (void*) GetProcAddress (module, "GetPluginFactory"))
            if (auto* factory = get())
            {
                for (int i = 0, n = factory->countClasses(); i < n && ! found; ++i)
                {
                    ClassInfo info {};
                    if (factory->getClassInfo (i, &info) != 0)
                        continue;
                    std::array<std::uint8_t, 16> id {};
                    std::memcpy (id.data(), info.cid, 16);
                    if (std::strncmp (info.category, "Audio Module Class", 32) == 0 && matches (id))
                        found = id;
                }
                factory->release();
            }
    if (started && init != nullptr)
        if (auto* exit = (bool (*)()) (void*) GetProcAddress (module, "ExitDll"))
            exit();
    FreeLibrary (module); // (the module stays loaded while JUCE's own instance holds it)
    return found;
   #else
    return std::nullopt;
   #endif
}
} // namespace

std::optional<ForeignPlugin> PluginHost::flWrapperRecord (const PluginRef& ref, bool instrument)
{
    const auto d = find (juce::String::fromUTF8 (ref.id.c_str()));
    if (d == nullptr)
        return std::nullopt;
    ForeignPlugin p;
    p.name = d->name.toStdString();
    p.path = d->fileOrIdentifier.toStdString();
    p.midiInPort = ref.midiInPort;
    p.midiOutPort = ref.midiOutPort;
    p.bendRange = ref.bendRange;
    const auto state = juceStateBytes (ref.state);
    if (d->pluginFormatName == "VST3")
    {
        p.format = ForeignPlugin::Format::vst3;
        const auto id = vst3ClassIdOf (*d, flPluginDatabase());
        if (! id)
            return std::nullopt;
        p.classId = *id;
        p.hasClassId = true;
        // FL names a VST3 after its file (REA_rodada5 §2.2)
        p.name = juce::File (d->fileOrIdentifier).getFileNameWithoutExtension().toStdString();
        std::vector<std::uint8_t> component, controller;
        if (const auto xml = juce::AudioProcessor::getXmlFromBinary (state.data(), (int) state.size()); xml != nullptr && xml->hasTagName ("VST3PluginState"))
        {
            juce::MemoryBlock m;
            if (const auto* c = xml->getChildByName ("IComponent"); c != nullptr && m.fromBase64Encoding (c->getAllSubText().trim()))
                component = blockBytes (m);
            m.reset();
            if (const auto* c = xml->getChildByName ("IEditController"); c != nullptr && m.fromBase64Encoding (c->getAllSubText().trim()))
                controller = blockBytes (m);
        }
        p.state = flpexport::flVst3State (component, controller);
    }
    else if (d->pluginFormatName == Vst2PluginFormat::formatName)
    {
        p.format = ForeignPlugin::Format::vst2;
        p.vst2Id = (std::uint32_t) d->uniqueId;
        FlVst2State check;
        if (parseFlVst2State (state, check))
            p.state = state; // LYNX's VST2 host keeps FL's own layout (REA_rodada89 §2)
        else if (! state.empty())
        {
            FlVst2State chunk;
            chunk.flags = 8;
            chunk.chunk = state;
            p.state = writeFlVst2State (chunk);
        }
    }
    else if (d->pluginFormatName == ClapPluginFormat::formatName)
    {
        p.format = ForeignPlugin::Format::clap;
        p.clapId = clapPluginId (*d);
        if (p.clapId.empty())
            return std::nullopt;
        if (! state.empty())
        {
            p.state = { 0, 0, 0, 0 };
            p.state.insert (p.state.end(), state.begin(), state.end());
        }
    }
    else
        return std::nullopt;
    juce::ignoreUnused (instrument);
    return p;
}

} // namespace lynx
