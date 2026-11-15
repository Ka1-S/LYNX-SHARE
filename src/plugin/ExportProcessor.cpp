#include "plugin/ExportProcessor.h"

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include "core/EffectPresets.h"
#include "core/Effects.h"
#include "core/Instruments.h"
#include "core/LynxBanks.h"
#include "core/ModelParams.h"
#include "core/PluginLayout.h"
#include "core/RackRenderer.h"
#include "core/SnapshotExchange.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectFile.h"
#include "ui/ChannelSettings.h"
#include "ui/FacetEqEditor.h"
#include "ui/ImageCaches.h"
#include "ui/PluginEditors.h"
#include "ui/ProEditors.h"
#include "ui/Theme.h"

namespace lynx::plugexport
{
namespace
{
constexpr int insertTrack = 1; // the effect sits in Insert 1, slot 0, as in the app's --parityshot / --plugui
constexpr int sideTrack = 2;   // round 188b: the host's side-chain bus comes in here, the effect's side-chain source

// The side-chain track of an effect's song: the host's side-chain bus is its input, it goes nowhere else (round 188b).
// States saved before it existed (round 180b: Master + Insert 1) get it on load.
void addSideTrack (Song& song)
{
    if (song.mixer.size() == 2)
    {
        song.mixer.resize (3);
        song.mixer[(std::size_t) sideTrack].name = "Sidechain";
        song.mixer[(std::size_t) sideTrack].effects.clear();
        song.mixer[(std::size_t) sideTrack].toMaster = false;
        if (! song.mixer[1].effects.empty() && song.mixer[1].effects[0].sidechain < 0)
            song.mixer[1].effects[0].sidechain = sideTrack;
    }
}

Song makeSong (const Entry& entry)
{
    auto song = Song::makeDefault();
    song.channels.clear();
    song.mixer.resize (2);
    song.mixer[0].name = "Master";
    song.mixer[0].effects.clear(); // (the default project's limiter)
    song.mixer[1].name = "Insert 1";
    song.mixer[1].effects.clear();
    for (auto& pattern : song.patterns) // (the default project's demo beat: nothing plays by itself in a plugin)
        pattern.notes.clear();
    song.playlist.clear();
    if (entry.instrument)
    {
        if (auto channel = banks::defaultChannel (entry.synth, entry.name))
        {
            channel->mixerTrack = 0;
            song.channels.push_back (std::move (*channel));
        }
    }
    else
    {
        auto slot = makeEffectSlot (entry.effect);
        slot.smartDisable = false; // a host decides when it is silent
        song.mixer[1].effects.push_back (std::move (slot));
        addSideTrack (song);
    }
    return song;
}

class ExportProcessor;

class Parameter final : public juce::AudioProcessorParameterWithID
{
public:
    Parameter (ExportProcessor& o, int indexInEngine, int order, const juce::String& name, float defaultValue01)
        : juce::AudioProcessorParameterWithID (juce::ParameterID ("p" + juce::String (indexInEngine), 1), name),
          owner (o), engineIndex (indexInEngine), slotOrder (order), initial (defaultValue01), value (defaultValue01)
    {
    }

    float getValue() const override { return value.load (std::memory_order_relaxed); }
    void setValue (float v) override { value.store (snapped (v), std::memory_order_relaxed); }
    int getNumSteps() const override { return steps > 0 ? steps + 1 : AudioProcessorParameter::getNumSteps(); }
    bool isDiscrete() const override { return steps > 0; }
    // a stepped engine parameter takes only its steps: the host reads back what the engine plays (and a state restores it)
    float snapped (float v) const noexcept
    {
        v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
        return steps > 0 ? std::round (v * (float) steps) / (float) steps : v;
    }
    float getDefaultValue() const override { return initial; }
    juce::String getText (float v, int) const override;
    float getValueForText (const juce::String& text) const override;
    juce::String getLabel() const override { return {}; }
    bool isAutomatable() const override { return true; }

    ExportProcessor& owner;
    int steps = 0; // > 0: whole steps over 0..1
    const int engineIndex, slotOrder;
    const float initial;
    std::atomic<float> value;
};

class ExportEditor;

class ExportProcessor final : public juce::AudioProcessor
{
public:
    explicit ExportProcessor (const Entry& e)
        : AudioProcessor (e.instrument ? BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       : BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
          entry (e),
          song (makeSong (e)),
          renderer (std::make_unique<RackRenderer>())
    {
        plugui::hostedElsewhere = true;
        ++liveInstances;
        if (entry.instrument)
        {
            if (const auto* table = modelparams::tableOf (entry.synth); table != nullptr && table->params != nullptr && ! song.channels.empty())
                for (const auto& p : *table->params)
                {
                    auto* param = new Parameter (*this, p.index, (int) params.size(), p.name, modelparams::value01 (song.channels[0], p.index).value_or (0.0f));
                    if (p.stepped && p.high > p.low && p.high - p.low <= 4096.0f)
                        param->steps = (int) std::lround (p.high - p.low);
                    add (param);
                }
        }
        else
        {
            const auto& info = effectInfo (entry.effect);
            for (std::size_t i = 0; i < info.params.size(); ++i)
                add (new Parameter (*this, (int) i, (int) i, juce::String::fromUTF8 (info.params[i].name), slot().params[i]));
        }
        if (! entry.instrument)
            live = slot().params;
        sent.resize (params.size());
        for (auto* p : params)
            sent[(std::size_t) p->slotOrder] = p->getValue();
    }

    ~ExportProcessor() override;
    static inline std::atomic<int> liveInstances { 0 };

    const juce::String getName() const override { return entry.name; }
    double getTailLengthSeconds() const override { return entry.instrument ? 0.0 : 10.0; }
    bool acceptsMidi() const override { return entry.instrument; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        if (layout.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;
        if (entry.instrument)
            return true;
        // the side-chain bus: off, mono or stereo (round 188b)
        const auto side = layout.inputBuses.size() > 1 ? layout.inputBuses[1] : juce::AudioChannelSet::disabled();
        return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
               && (side.isDisabled() || side == juce::AudioChannelSet::mono() || side == juce::AudioChannelSet::stereo());
    }

    void prepareToPlay (double sampleRate, int maxBlock) override
    {
        const juce::ScopedLock sl (songLock);
        rate = sampleRate;
        renderer->prepare (sampleRate, juce::jmax (maxBlock, 64));
        inputs.setSize (2, juce::jmax (maxBlock, 64));
        sideInputs.setSize (2, juce::jmax (maxBlock, 64));
        // a (re)start is a new stream that sounds exactly as a fresh instance's: new engines, not the ones of the last
        // build (publish reuses them, and a reset leaves some state behind: smoothed settings, a detector's history),
        // round 188b
        shape = RackState {};
        publish();
        setLatencySamples (currentLatency());
        resetWanted.store (true, std::memory_order_relaxed);
    }

    void releaseResources() override {}

    void reset() override { resetWanted.store (true, std::memory_order_relaxed); }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    void getStateInformation (juce::MemoryBlock& out) override
    {
        const juce::ScopedLock sl (songLock);
        takeParameters();
        const auto json = ProjectFile::toJson (song).toStdString();
        out.replaceAll (json.data(), json.size());
    }

    void setStateInformation (const void* data, int size) override { loadState (data, size); }

    // false: not a state of this plugin, nothing changed
    bool loadState (const void* data, int size)
    {
        if (data == nullptr || size <= 0)
            return false;
        juce::String error;
        auto loaded = ProjectFile::fromJson (juce::String::createStringFromData (data, size), error);
        if (! loaded || ! matches (*loaded))
            return false;
        if (! entry.instrument)
            addSideTrack (*loaded);
        const juce::ScopedLock sl (songLock);
        song = std::move (*loaded);
        if (entry.instrument)
            song.channels[0].name = entry.name; // (the name of this build's table, plugin/plugins.def)
        else
            slot().smartDisable = false;
        showValues();
        if (rate > 0.0)
            publish();
        editorDirty.store (true, std::memory_order_relaxed);
        return true;
    }

    // ---- the editor's side (message thread)
    Song songCopy()
    {
        const juce::ScopedLock sl (songLock);
        takeParameters();
        return song;
    }

    // What the editor's engine holds now went into the plugin: its values to the host, the rest to the audio
    void takeFromEditor (const Song& edited)
    {
        const juce::ScopedLock sl (songLock);
        takeParameters();
        if (entry.instrument ? (edited.channels.empty() || edited.channels[0] == song.channels[0])
                             : (edited.mixer.size() < 2 || edited.mixer[1].effects.empty() || edited.mixer[1].effects[0] == slot()))
            return;
        const auto rebuild = entry.instrument || ! sameExceptParams (edited);
        const auto before = values01();
        if (entry.instrument)
            song.channels[0] = edited.channels[0];
        else
            slot() = edited.mixer[1].effects[0];
        const auto after = values01();
        for (auto* p : params)
            if (const auto v = after[(std::size_t) p->slotOrder]; v != before[(std::size_t) p->slotOrder])
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (v);
                p->endChangeGesture();
            }
        if (rebuild)
            publish();
        setLatencySamples (currentLatency());
    }

    bool takeEditorDirty() noexcept { return editorDirty.exchange (false, std::memory_order_relaxed); }

    // The host moved a parameter since `known`? (the editor shows it)
    std::vector<float> values01()
    {
        std::vector<float> out (params.size());
        if (entry.instrument)
        {
            for (auto* p : params)
                out[(std::size_t) p->slotOrder] = song.channels.empty() ? 0.0f : modelparams::value01 (song.channels[0], p->engineIndex).value_or (0.0f);
        }
        else
            for (auto* p : params)
                out[(std::size_t) p->slotOrder] = slot().params[(std::size_t) p->engineIndex];
        return out;
    }

    ProcessTimes* times() noexcept { return &renderer->processTimes(); }

    std::shared_ptr<Effect> runningEffect() const
    {
        return shape.effects.size() > (std::size_t) insertTrack && ! shape.effects[(std::size_t) insertTrack].empty()
                   ? shape.effects[(std::size_t) insertTrack][0]
                   : nullptr;
    }

    const Entry& entry;
    std::vector<Parameter*> params;

    // Host values into the Song (message thread, songLock held)
    void takeParameters()
    {
        if (entry.instrument)
        {
            if (! song.channels.empty())
                for (auto* p : params)
                    modelparams::setValue01 (song.channels[0], p->engineIndex, p->getValue());
        }
        else
            for (auto* p : params)
                slot().params[(std::size_t) p->engineIndex] = p->getValue();
    }

    juce::CriticalSection songLock;

private:
    void add (Parameter* p)
    {
        params.push_back (p);
        addParameter (p);
    }

    EffectSlot& slot() { return song.mixer[(std::size_t) insertTrack].effects[0]; }

    bool matches (const Song& s) const
    {
        if (entry.instrument)
            return s.channels.size() == 1 && s.channels[0].instrument == entry.synth;
        return (s.mixer.size() == 2 || s.mixer.size() == 3) && s.mixer[1].effects.size() == 1 && s.mixer[1].effects[0].kind == entry.effect;
    }

    // Only the named parameters differ: they reach the audio as host values, nothing to rebuild
    bool sameExceptParams (const Song& edited)
    {
        auto a = edited.mixer[1].effects[0];
        for (auto* p : params)
            a.params[(std::size_t) p->engineIndex] = slot().params[(std::size_t) p->engineIndex];
        return a == slot();
    }

    void showValues()
    {
        if (entry.instrument)
        {
            for (auto* p : params)
                if (! song.channels.empty())
                    p->value.store (modelparams::value01 (song.channels[0], p->engineIndex).value_or (p->initial), std::memory_order_relaxed);
        }
        else
            for (auto* p : params)
                p->value.store (slot().params[(std::size_t) p->engineIndex], std::memory_order_relaxed);
        for (auto* p : params)
            p->sendValueChangedMessageToListeners (p->getValue());
    }

    // (songLock held, off the audio thread)
    void publish()
    {
        auto state = RackState::build (song, &shape, rate);
        shape.song.channels = song.channels;
        shape.song.mixer = song.mixer;
        shape.song.bpm = song.bpm;
        shape.instruments = state->instruments;
        shape.serial = state->serial;
        shape.effects = state->effects;
        shape.sampleRate = state->sampleRate;
        shape.latencies = state->latencies;
        shape.pdc = state->pdc;
        shape.channelDelays = state->channelDelays;
        shape.sendDelays = state->sendDelays;
        shape.shifterStock = state->shifterStock;
        renderer->reserveFor (*state); // (round 190gn: its buffers hold this song before the audio thread plays it)
        exchange.publish (std::move (state));
    }

    int currentLatency() const
    {
        const auto effect = runningEffect();
        return effect != nullptr ? effect->latencySamples() : 0;
    }

    Song song;
    double rate = 0.0;
    std::unique_ptr<RackRenderer> renderer;
    SnapshotExchange<RackState> exchange;
    RackState shape; // what the next build reuses (as AudioEngine::lastPublishedShape)
    LiveEventQueue liveEvents;
    EffectParams live {};
    std::vector<float> sent; // what the instrument last got (audio thread)
    juce::AudioBuffer<float> inputs, sideInputs;
    std::atomic<bool> resetWanted { false }, editorDirty { false };
};

float Parameter::getValueForText (const juce::String& text) const
{
    if (! owner.entry.instrument)
    {
        const auto near = presets::valueFromText (owner.entry.effect, engineIndex, text.toStdString()).value_or (getValue());
        if (getText (near, 64) == text)
            return near;
        // the nearest number may sit between two shown values: the value nearby that shows exactly this text
        // (in steps finer than the text's last digit anywhere on a log scale: 1e-6 near it, then 1e-5 further out)
        for (const auto [step, count] : { std::pair { 1.0e-6f, 4000 }, std::pair { 1.0e-5f, 2000 } })
            for (int i = 1; i <= count; ++i)
                for (const auto v : { near - (float) i * step, near + (float) i * step })
                    if (v >= 0.0f && v <= 1.0f && getText (v, 64) == text)
                        return v;
        return near;
    }
    if (const auto* p = modelparams::find (owner.entry.synth, engineIndex); p != nullptr && p->high > p->low)
        return juce::jlimit (0.0f, 1.0f, (text.getFloatValue() - p->low) / (p->high - p->low));
    return juce::jlimit (0.0f, 1.0f, text.getFloatValue());
}

juce::String Parameter::getText (float v, int) const
{
    if (! owner.entry.instrument)
    {
        return juce::String::fromUTF8 (formatEffectParam (owner.entry.effect, engineIndex, v, 120.0).c_str());
    }
    if (const auto* p = modelparams::find (owner.entry.synth, engineIndex))
        return juce::String (p->low + v * (p->high - p->low), 2);
    return juce::String (v, 3);
}

void ExportProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    if (numSamples == 0 || buffer.getNumChannels() < 2)
        return;

    const auto* state = exchange.acquire();
    if (state == nullptr)
    {
        buffer.clear();
        return;
    }

    auto bpm = 120.0, quarters = 0.0;
    auto playing = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            bpm = position->getBpm().orFallback (120.0);
            quarters = position->getPpqPosition().orFallback (0.0);
            playing = position->getIsPlaying();
        }
    renderer->setTempoOverride (bpm);
    if (resetWanted.exchange (false, std::memory_order_relaxed))
    {
        NoteEvent e;
        e.type = NoteEvent::Type::choke;
        e.channel = -1;
        liveEvents.push (e);
        e.channel = RackRenderer::stopSoundChannel;
        liveEvents.push (e);
    }

    if (entry.instrument)
    {
        // parameters: the engine takes them at once, without restarting its voices
        if (! state->instruments.empty() && state->instruments[0] != nullptr)
            for (auto* p : params)
                if (const auto v = p->getValue(); v != sent[(std::size_t) p->slotOrder])
                {
                    state->instruments[0]->setParameter (p->engineIndex, v);
                    sent[(std::size_t) p->slotOrder] = v;
                }

        // the renderer applies live events at the start of a block: cut the block at each MIDI event
        auto done = 0;
        const auto renderUpTo = [&] (int end) {
            if (end > done)
            {
                renderer->process (state, false, quarters * lynx::ppq, end - done, buffer.getWritePointer (0, done), buffer.getWritePointer (1, done), &liveEvents);
                done = end;
            }
        };
        for (const auto metadata : midi)
        {
            renderUpTo (juce::jlimit (0, numSamples, metadata.samplePosition));
            const auto message = metadata.getMessage();
            NoteEvent e;
            e.channel = 0;
            e.key = message.getNoteNumber();
            if (message.isNoteOn())
            {
                e.type = NoteEvent::Type::on;
                e.velocity = message.getFloatVelocity();
            }
            else if (message.isNoteOff())
                e.type = NoteEvent::Type::off;
            else if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                e.type = NoteEvent::Type::choke;
                e.channel = -1;
            }
            else if (message.isPitchWheel())
            {
                e.type = NoteEvent::Type::control;
                e.key = NoteEvent::controlPitchBend;
                e.velocity = (float) (message.getPitchWheelValue() - 8192) / 8192.0f;
            }
            else if (message.isController())
            {
                e.type = NoteEvent::Type::control;
                e.key = message.getControllerNumber();
                e.velocity = (float) message.getControllerValue() / 127.0f;
            }
            else
                continue;
            liveEvents.push (e);
        }
        renderUpTo (numSamples);
        midi.clear();
    }
    else
    {
        if (state->song.mixer.size() > (std::size_t) insertTrack && ! state->song.mixer[(std::size_t) insertTrack].effects.empty())
            live = state->song.mixer[(std::size_t) insertTrack].effects[0].params; // (the parameters a host doesn't see)
        for (auto* p : params)
            live[(std::size_t) p->engineIndex] = p->getValue();
        inputs.setSize (2, numSamples, true, false, true); // (no allocation: prepareToPlay's size, or the host broke its promise)
        inputs.copyFrom (0, 0, buffer, 0, 0, numSamples);
        inputs.copyFrom (1, 0, buffer, 1, 0, numSamples);
        // the host's side-chain bus (round 188b): into the side-chain track, the slot's side-chain source
        const auto* sideBus = getBusCount (true) > 1 ? getBus (true, 1) : nullptr;
        const auto sideChannels = sideBus != nullptr && sideBus->isEnabled() ? sideBus->getNumberOfChannels() : 0;
        if (sideChannels > 0 && buffer.getNumChannels() >= 2 + sideChannels)
        {
            sideInputs.setSize (2, numSamples, true, false, true);
            const auto first = sideBus->getChannelIndexInProcessBlockBuffer (0);
            sideInputs.copyFrom (0, 0, buffer, first, 0, numSamples);
            sideInputs.copyFrom (1, 0, buffer, sideChannels > 1 ? first + 1 : first, 0, numSamples);
            renderer->setSideInput (sideInputs.getReadPointer (0), sideInputs.getReadPointer (1), sideTrack);
        }
        else
            renderer->setSideInput (nullptr, nullptr, -1);
        renderer->setSlotParamsOverride (&live, insertTrack, 0);
        renderer->setMetronomeInput (inputs.getReadPointer (0), inputs.getReadPointer (1), insertTrack);
        renderer->process (state, playing, quarters * lynx::ppq, numSamples, buffer.getWritePointer (0), buffer.getWritePointer (1), &liveEvents);
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);
}

// The app's own editor, on an engine that plays nothing and only lives while the editor is open
class ExportEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ExportEditor (ExportProcessor& p)
        : juce::AudioProcessorEditor (p), processor (p), engine (false, 0, false)
    {
        setLookAndFeel (&lookAndFeel);
        engine.setSong (processor.songCopy());
        if (! processor.entry.instrument)
            engine.setExternalRuntime (processor.times(), [&p] { return p.runningEffect(); });
        known = engine.getSong();
        inner = processor.entry.instrument ? makeChannelSettingsPanel (engine, 0)
              : processor.entry.effect == EffectKind::facetEq ? makeFacetEqEditor (engine, insertTrack, 0, false)
              : ProEditorWindows::handles (processor.entry.effect) ? makeProEditor (engine, insertTrack, 0, false)
                                                                   : makeNativeEffectEditor (engine, insertTrack, 0, 1.0f, 0);
        if (inner != nullptr)
        {
            addAndMakeVisible (*inner);
            setSize (juce::jmax (200, inner->getWidth()), juce::jmax (100, inner->getHeight()));
        }
        else
            setSize (400, 200);
        startTimerHz (30);
    }

    ~ExportEditor() override
    {
        stopTimer();
        inner.reset();
        setLookAndFeel (nullptr);
    }

    void resized() override
    {
        if (inner != nullptr)
            inner->setBounds (getLocalBounds());
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }

private:
    void timerCallback() override
    {
        const auto& current = engine.getSong();
        // the editor changed something: it goes to the plugin and the host
        if (! sameSound (current, known))
        {
            processor.takeFromEditor (current);
            known = current;
        }
        // the host changed something (automation, a loaded state): the editor shows it
        auto theirs = processor.songCopy();
        if (processor.takeEditorDirty() || ! sameSound (theirs, known))
        {
            engine.editSong ([&theirs] (Song& s) { s.channels = theirs.channels; s.mixer = theirs.mixer; }, false);
            known = engine.getSong();
        }
    }

    static bool sameSound (const Song& a, const Song& b) { return a.channels == b.channels && a.mixer == b.mixer; }

    ExportProcessor& processor;
    theme::LookAndFeel lookAndFeel;
    AudioEngine engine;
    Song known;
    std::unique_ptr<juce::Component> inner;
};

juce::AudioProcessorEditor* ExportProcessor::createEditor() { return new ExportEditor (*this); }

ExportProcessor::~ExportProcessor()
{
    // the last instance empties the UI's static image caches while JUCE still runs (ui/ImageCaches.h): freed later, at
    // DLL unload, their Direct2D device deadlocks in the graphics driver (FL Studio hung on exit, round 180b)
    if (--liveInstances == 0 && juce::MessageManager::existsAndIsCurrentThread())
        ui::clearImageCaches();
}
} // namespace

juce::AudioProcessor* makeProcessor (const Entry& entry) { return new ExportProcessor (entry); }
juce::AudioProcessor* makeProcessorAt (int catalogIndex) { return makeProcessor (catalog[(size_t) catalogIndex]); }

bool loadState (juce::AudioProcessor& processor, const void* data, int size)
{
    auto* p = dynamic_cast<ExportProcessor*> (&processor);
    return p != nullptr && p->loadState (data, size);
}
} // namespace lynx::plugexport
