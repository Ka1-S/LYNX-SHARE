#include <optional>
#include "ui/WindowOwner.h"
#include "core/NoteNames.h"
#include "ui/ChannelSettings.h"
#include "core/FlControlMenus.h"
#include "core/FlmDwSampler.h"
#include "core/SamplerPage.h"
#include "ui/FileChoosers.h"
#include "ui/Icons.h"
#include "ui/LayerEditor.h"
#include "ui/DanceView.h"
#include "ui/Controllers164.h"
#include "ui/VoltageEditor.h"
#include "ui/AutomationUi.h"
#include "ui/BrowserComponent.h"

#include "core/MixerOps.h"
#include "core/ClipOps.h"
#include "core/TempoDetect.h"

#include <cstring>
#include <functional>

#include "core/DrumSynthLive.h"
#include "core/FlSynths.h"
#include "core/FlKeys.h"
#include "core/ChannelMiscFx.h"
#include "core/FlpImport.h"
#include "core/Harmor.h"
#include "core/HarmorControls.h"
#include "core/Gms.h"
#include "core/MiniSynth.h"
#include "core/SimSynth.h"
#include "core/Drumaxx.h"
#include "core/DrumControls.h"
#include <map>
#include "core/Granulizer.h"
#include "core/Morphine.h"
#include "core/Ogun.h"
#include "core/Dx10.h"
#include "core/Kepler.h"
#include "core/PoiZone.h"
#include "core/LynxBanks.h"
#include "core/ModelParams.h"
#include "core/BassDrum.h"
#include "core/FruitKick.h"
#include "core/Toxic.h"
#include "core/ThreeOsc.h"
#include "core/BeepMap.h"
#include "core/Sawer.h"
#include "core/TransistorBass.h"
#include "core/Slayer.h"
#include "core/SytrusEngine.h"
#include "core/SynthPresets.h"
#include "core/PluginLayout.h"
#include "engine/SampleLibrary.h"
#include "engine/Text.h"
#include "core/RackOps.h"
#include "core/NativeEngines.h"
#include "core/PresetCycle.h"
#include "ui/ControllerEditors.h"
#include "ui/HintBar.h"
#include "ui/ValueControl.h"
#include "ui/I18n.h"
#include "ui/PluginSkin.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"
#include "ui/SoundFontEditor.h"
#include "ui/VfxEditors.h"
#include "ui/VideoPlayerView.h"
#include "ui/WaveTravellerEditor.h"
#include "ui/FpcEditor.h"
#include "ui/FruitySlicerEditor.h"
#include "ui/FlVoicePanel.h"
#include "core/FruitySlicer.h"
#include "ui/SlicexEditor.h"
#include "ui/BrowserComponent.h"
#include "engine/SampleLibrary.h"
#include "core/SampleFiles.h"
#include "core/ProjectContents.h"

namespace lynx
{
namespace
{
// Which channel a page edits (round 164f): a Channel Rack channel, or the generator of a Patcher node found through
// `locate` (index −1) — the same pages edit both, so a node opens the editor its engine has on a channel.
struct ChannelRef
{
    std::function<ChannelDef* (Song&)> locate;
    int index = -1; // the Channel Rack index (automation targets, its windows); −1 = a Patcher node

    static ChannelRef rack (int c)
    {
        return { [c] (Song& s) -> ChannelDef* { return c >= 0 && c < (int) s.channels.size() ? &s.channels[(std::size_t) c] : nullptr; }, c };
    }
    const ChannelDef* get (const AudioEngine& engine) const { return locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr; }
    void edit (AudioEngine& engine, std::function<void (ChannelDef&)> change, bool undoable = true) const
    {
        engine.editSong ([l = locate, change = std::move (change)] (Song& s) {
            if (auto* d = l ? l (s) : nullptr)
                change (*d);
        }, undoable);
    }
};

void showModulationFor (juce::Component& from, AudioEngine& engine, int channel);

// Cutoff and pitch envelopes (FL's channel envelopes), used by the synth and the sampler.
class ModEnvelopeEditor final : public juce::Component
{
public:
    std::function<void()> onChange;
    static constexpr int preferredHeight = 16 + 2 * 62;

    ModEnvelopeEditor()
    {
        const juce::String titles[] = { tr ("Cutoff env."), tr ("Pitch env.") };

        for (int e = 0; e < 2; ++e)
        {
            auto& row = rows[(std::size_t) e];
            row.title.setText (titles[e], juce::dontSendNotification);
            row.title.setFont (theme::font (theme::type::small));
            addAndMakeVisible (row.title);
            knob (row.amount, e == 0 ? -10.0 : -48.0, e == 0 ? 10.0 : 48.0, 0.01, e == 0 ? " oct" : " st");
            row.amount.setDoubleClickReturnValue (true, 0.0);
            row.amount.setTooltip (tr ("0 = off"));

            for (auto* s : { &row.attack, &row.hold, &row.decay, &row.release })
            {
                knob (*s, 0.0, 10.0, 0.001, " s");
                s->setSkewFactorFromMidPoint (0.5);
            }

            knob (row.sustain, 0.0, 1.0, 0.01, "");
        }

        const char* names[] = { TR_NOOP ("Amount"), TR_NOOP ("Attack"), TR_NOOP ("Hold"), TR_NOOP ("Decay"), TR_NOOP ("Sustain"), TR_NOOP ("Release") };

        for (int i = 0; i < 6; ++i)
        {
            labels[(std::size_t) i].setText (tr (names[i]), juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (theme::font (theme::type::caption));
            addAndMakeVisible (labels[(std::size_t) i]);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (16);
        header.removeFromLeft (80);
        const auto w = header.getWidth() / 6;

        for (auto& l : labels)
            l.setBounds (header.removeFromLeft (w));

        for (auto& row : rows)
        {
            auto line = area.removeFromTop (62);
            row.title.setBounds (line.removeFromLeft (80).withSizeKeepingCentre (80, 24));
            for (auto* s : { &row.amount, &row.attack, &row.hold, &row.decay, &row.sustain, &row.release })
                s->setBounds (line.removeFromLeft (w));
        }
    }

    void show (const ModEnvelope& filter, const ModEnvelope& pitch)
    {
        rows[0].show (filter);
        rows[1].show (pitch);
    }

    ModEnvelope filter() const { return rows[0].read(); }
    ModEnvelope pitch() const { return rows[1].read(); }

private:
    struct Row
    {
        juce::Label title;
        juce::Slider amount, attack, hold, decay, sustain, release;

        void show (const ModEnvelope& e)
        {
            amount.setValue (e.amount, juce::dontSendNotification);
            attack.setValue (e.attack, juce::dontSendNotification);
            hold.setValue (e.hold, juce::dontSendNotification);
            decay.setValue (e.decay, juce::dontSendNotification);
            sustain.setValue (e.sustain, juce::dontSendNotification);
            release.setValue (e.release, juce::dontSendNotification);
        }

        ModEnvelope read() const
        {
            return { (float) amount.getValue(), (float) attack.getValue(), (float) hold.getValue(),
                     (float) decay.getValue(), (float) sustain.getValue(), (float) release.getValue() };
        }
    };

    void knob (juce::Slider& s, double lo, double hi, double step, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        // round 170j: the plugin kit's knobs in LYNX Synth's own art ("the LYNX lab"), or the forced plugin style
        s.getProperties().set (skin::sliderProperty, (juce::int64) skin::artAccent (skin::Art::lynxSynth).getARGB());
        s.getProperties().set (skin::styleProperty, skin::packStyle (skin::Style::themed, skin::Art::lynxSynth));
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] {
            if (onChange)
                onChange();
        };
        addAndMakeVisible (s);
    }

    std::array<Row, 2> rows;
    std::array<juce::Label, 6> labels;
};

// Channel LFOs (pan, volume, cutoff, pitch), like FL's.
class LfoEditor final : public juce::Component
{
public:
    std::function<void()> onChange;
    static constexpr int preferredHeight = 18 + lfoTargets * 60;

    LfoEditor()
    {
        const juce::String titles[] = { tr ("Pan LFO"), tr ("Volume LFO"), tr ("Cutoff LFO"), tr ("Pitch LFO") };
        const double amountRange[] = { 1.0, 1.0, 8.0, 24.0 };
        const char* amountSuffix[] = { "", "", " oct", " st" };

        for (int i = 0; i < lfoTargets; ++i)
        {
            auto& row = rows[(std::size_t) i];
            row.title.setText (titles[i], juce::dontSendNotification);
            row.title.setFont (theme::font (theme::type::small));
            addAndMakeVisible (row.title);
            knob (row.amount, -amountRange[i], amountRange[i], 0.01, amountSuffix[i]);
            row.amount.setDoubleClickReturnValue (true, 0.0);
            row.amount.setTooltip (tr ("0 = off"));
            knob (row.period, 0.01, 16.0, 0.001, "");
            row.period.setSkewFactorFromMidPoint (0.5);
            knob (row.delay, 0.0, 8.0, 0.001, "");
            row.delay.setSkewFactorFromMidPoint (0.5);
            knob (row.attack, 0.0, 8.0, 0.001, "");
            row.attack.setSkewFactorFromMidPoint (0.5);
            row.shape.addItemList ({ tr ("Sine"), tr ("Triangle"), tr ("Pulse") }, 1);
            row.shape.onChange = [this] { changed(); };
            addAndMakeVisible (row.shape);
            row.sync.setButtonText (tr ("Beats"));
            row.sync.setTooltip (tr ("Period, delay and fade-in in beats (follows the BPM) instead of seconds"));
            row.sync.onClick = [this] { changed(); };
            addAndMakeVisible (row.sync);
            row.global.setButtonText (tr ("Global"));
            row.global.setTooltip (tr ("Phase follows the song position instead of restarting on each note"));
            row.global.onClick = [this] { changed(); };
            addAndMakeVisible (row.global);
        }

        const juce::String headers[] = { tr ("Amount"), tr ("Period"), tr ("Shape"), tr ("Delay"), tr ("Fade-in"), tr ("Unit") };

        for (int i = 0; i < 6; ++i)
        {
            labels[(std::size_t) i].setText (headers[i], juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (theme::font (theme::type::caption));
            addAndMakeVisible (labels[(std::size_t) i]);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (18);
        header.removeFromLeft (80);
        const auto w = header.getWidth() / 6;

        for (auto& l : labels)
            l.setBounds (header.removeFromLeft (w));

        for (auto& row : rows)
        {
            auto line = area.removeFromTop (60);
            row.title.setBounds (line.removeFromLeft (80).withSizeKeepingCentre (80, 24));
            row.amount.setBounds (line.removeFromLeft (w));
            row.period.setBounds (line.removeFromLeft (w));
            row.shape.setBounds (line.removeFromLeft (w).withSizeKeepingCentre (w - 6, 24));
            row.delay.setBounds (line.removeFromLeft (w));
            row.attack.setBounds (line.removeFromLeft (w));
            auto toggles = line.removeFromLeft (w).withSizeKeepingCentre (w - 6, 50);
            row.sync.setBounds (toggles.removeFromTop (24));
            row.global.setBounds (toggles.removeFromBottom (24));
        }
    }

    void show (const std::array<ChannelLfo, lfoTargets>& lfo)
    {
        for (std::size_t i = 0; i < lfoTargets; ++i)
        {
            auto& row = rows[i];
            const auto& l = lfo[i];
            row.amount.setValue (l.amount, juce::dontSendNotification);
            row.period.setValue (l.period, juce::dontSendNotification);
            row.delay.setValue (l.delay, juce::dontSendNotification);
            row.attack.setValue (l.attack, juce::dontSendNotification);
            row.shape.setSelectedId (l.shape + 1, juce::dontSendNotification);
            row.sync.setToggleState (l.sync, juce::dontSendNotification);
            row.global.setToggleState (l.global, juce::dontSendNotification);
            const auto unit = l.sync ? " t" : " s";
            for (auto* s : { &row.period, &row.delay, &row.attack })
                s->setTextValueSuffix (unit);
        }
    }

    std::array<ChannelLfo, lfoTargets> read() const
    {
        std::array<ChannelLfo, lfoTargets> lfo {};

        for (std::size_t i = 0; i < lfoTargets; ++i)
        {
            const auto& row = rows[i];
            lfo[i] = { (float) row.amount.getValue(), (float) row.period.getValue(), row.sync.getToggleState(),
                       juce::jlimit (0, 2, row.shape.getSelectedId() - 1), (float) row.delay.getValue(), (float) row.attack.getValue(),
                       row.global.getToggleState() };
        }

        return lfo;
    }

private:
    struct Row
    {
        juce::Label title;
        juce::Slider amount, period, delay, attack;
        juce::ComboBox shape;
        juce::ToggleButton sync, global;
    };

    void knob (juce::Slider& s, double lo, double hi, double step, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        // round 170j: the plugin kit's knobs in LYNX Synth's own art ("the LYNX lab"), or the forced plugin style
        s.getProperties().set (skin::sliderProperty, (juce::int64) skin::artAccent (skin::Art::lynxSynth).getARGB());
        s.getProperties().set (skin::styleProperty, skin::packStyle (skin::Style::themed, skin::Art::lynxSynth));
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] { changed(); };
        addAndMakeVisible (s);
    }

    void changed()
    {
        if (onChange)
            onChange();
    }

    std::array<Row, lfoTargets> rows;
    std::array<juce::Label, 6> labels;
};

// Channel modulation window: cutoff/pitch envelopes and LFOs of the synth or the sampler.
class ModulationPanel final : public juce::Component,
                              private juce::ChangeListener
{
public:
    ModulationPanel (AudioEngine& e, ChannelRef c)
        : engine (e),
          ref (std::move (c))
    {
        envelopes.onChange = [this] { apply(); };
        lfos.onChange = [this] { apply(); };
        addAndMakeVisible (envelopes);
        addAndMakeVisible (lfos);
        refresh();
        engine.addChangeListener (this);
        setSize (660, 20 + ModEnvelopeEditor::preferredHeight + 16 + LfoEditor::preferredHeight);
    }

    ~ModulationPanel() override { engine.removeChangeListener (this); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        envelopes.setBounds (area.removeFromTop (ModEnvelopeEditor::preferredHeight));
        area.removeFromTop (16);
        lfos.setBounds (area.removeFromTop (LfoEditor::preferredHeight));
    }

    int getChannel() const noexcept { return ref.index; }

private:
    const ChannelDef* current() const
    {
        return ref.get (engine);
    }

    bool isSynth() const { return current() != nullptr && current()->instrument == InstrumentKind::synth; }

    void refresh()
    {
        const auto* c = current();

        if (c == nullptr)
            return;

        if (isSynth())
        {
            envelopes.show (c->synth.filterEnv, c->synth.pitchEnv);
            lfos.show (c->synth.lfo);
        }
        else
        {
            envelopes.show (c->sampler.filterEnv, c->sampler.pitchEnv);
            lfos.show (c->sampler.lfo);
        }

        // only its own window: as a tab of the channel's window it left that window titled "Modulation - ..." while the
        // Sampler page showed (issue #12)
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>(); w != nullptr && w->getContentComponent() == this)
            w->setName (tr ("Modulation - {0}", { juce::String::fromUTF8 (c->name.c_str()) }));
    }

    void apply()
    {
        if (current() == nullptr)
            return;

        const auto filter = envelopes.filter(), pitch = envelopes.pitch();
        const auto lfo = lfos.read();
        const auto synth = isSynth();

        ref.edit (engine, [synth, filter, pitch, lfo] (ChannelDef& channelDef) {
            auto& ch = channelDef;

            if (synth)
            {
                ch.synth.filterEnv = filter;
                ch.synth.pitchEnv = pitch;
                ch.synth.lfo = lfo;
            }
            else
            {
                ch.sampler.filterEnv = filter;
                ch.sampler.pitchEnv = pitch;
                ch.sampler.lfo = lfo;
            }
        });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    AudioEngine& engine;
    ChannelRef ref;
    ModEnvelopeEditor envelopes;
    LfoEditor lfos;
};

// A Sampler knob: right-click opens its automation menu instead of moving it (issue #12)
class MenuSlider final : public juce::Slider
{
public:
    std::function<void()> onMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        menuClick = e.mods.isPopupMenu() && onMenu != nullptr;
        if (menuClick)
            onMenu();
        else
            juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! menuClick)
            juce::Slider::mouseDrag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! menuClick)
            juce::Slider::mouseUp (e);
        menuClick = false;
    }

private:
    bool menuClick = false;
};

// Physical-model channels (Sakura, Drumpad; REA_rodada28): knobs for the main raw FL parameters (0..1)
class ModelPanel final : public juce::Component,
                         public juce::TooltipClient,
                         public juce::DragAndDropTarget,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    ModelPanel (AudioEngine& e, ChannelRef c)
        : engine (e),
          ref (std::move (c))
    {
        static const ChannelDef none;
        const auto* found = ref.get (engine);
        const auto& def = found != nullptr ? *found : none;
        sakura = def.instrument == InstrumentKind::sakura;
        boobass = def.instrument == InstrumentKind::boobass;
        drumsynth = def.instrument == InstrumentKind::drumsynth;
        harmless = def.instrument == InstrumentKind::harmless;
        harmor = def.instrument == InstrumentKind::harmor;
        gms = def.instrument == InstrumentKind::gms;
        morphine = def.instrument == InstrumentKind::morphine;
        autogun = def.instrument == InstrumentKind::autogun;
        transistorBass = def.instrument == InstrumentKind::transistorbass;
        transistorBassMobile = transistorBass && transistorBassIsFlm (def);
        ogun = def.instrument == InstrumentKind::ogun || autogun; // Autogun: Ogun's parameter layout (REA_rodada57)
        miniSynth = def.instrument == InstrumentKind::minisynth;
        dx10 = def.instrument == InstrumentKind::dx10;
        kepler = def.instrument == InstrumentKind::kepler;
        keplerExo = def.instrument == InstrumentKind::keplerExo;
        poizone = def.instrument == InstrumentKind::poizone;
        fruitkick = def.instrument == InstrumentKind::fruitkick;
        bassdrum = def.instrument == InstrumentKind::bassdrum;
        toxic = def.instrument == InstrumentKind::toxic;
        threeOsc = def.instrument == InstrumentKind::threeosc;
        simSynth = def.instrument == InstrumentKind::simsynth;
        wasp = def.instrument == InstrumentKind::wasp;
        plucked = def.instrument == InstrumentKind::plucked;
        beepMap = def.instrument == InstrumentKind::beepmap;
        sawer = def.instrument == InstrumentKind::sawer;
        flKeys = def.instrument == InstrumentKind::flkeys;
        granulizer = def.instrument == InstrumentKind::granulizer;
        drumaxx = def.instrument == InstrumentKind::drumaxx;
        slayer = def.instrument == InstrumentKind::slayer;
        fruityDance = def.instrument == InstrumentKind::fruitydance;
        pocketSuperSaw = def.instrument == InstrumentKind::pocketSuperSaw; // round 190by
        // round 168q: the engines that run FL's own files say so when this computer lacks them
        engineFilesMissing = ((kepler || keplerExo) && ! keplerDllUsable (def.samplePath))
                          || (transistorBass && ! transistorBassIsFlm (def) && ! transistorBassDllUsable (def.samplePath)) // (FLM's build needs no file)
                          || (sawer && sawerReadTable (def.samplePath).empty());
        sytrus = def.instrument == InstrumentKind::sytrus;
        // DrumSynth Live: the patch on the key selected in FL's editor (raw FL values)
        if (drumsynth)
        {
            if (lynx::drumSynthEditorKeyIndex < (int) def.modelParams.size())
                editorKey = (int) def.modelParams[(std::size_t) lynx::drumSynthEditorKeyIndex];
            if (drumSynthKeyToSlot (editorKey) < 0)
                editorKey = 60;
            wavesFound = ! def.samplePath.empty();
        }
        const auto offset = drumsynth ? drumSynthPatchParams * drumSynthKeyToSlot (editorKey) : 0;
        static const std::pair<int, const char*> sakuraKnobs[] = {
            { 0, "Master" }, { 1, "Tune" }, { 2, "Attack" }, { 3, "Decay" }, { 4, "Sustain" }, { 5, "Release" },
            { 6, "Str1 decay" }, { 9, "Str1 damp" }, { 11, "Str2 decay" }, { 14, "Str2 damp" }, { 16, "Saturation" },
            { 17, "String mix" }, { 18, "Stereo sep" }, { 19, "Comb ratio" }, { 22, "Comb level" }, { 24, "Lo-cut" },
            { 29, "Hi-cut" }, { 30, "Hi-cut res" }, { 31, "Noise level" }, { 32, "Noise rate" } };
        static const std::pair<int, const char*> drumpadKnobs[] = {
            { 2, "Mallet amp" }, { 3, "Mallet decay" }, { 4, "Mallet noise" }, { 6, "Decay" }, { 7, "Cutoff" },
            { 8, "Tension" }, { 9, "Phase" }, { 10, "Material" }, { 11, "Size" }, { 12, "Shape" }, { 13, "Low freq" },
            { 18, "Mid freq" }, { 19, "Mid level" }, { 24, "High freq" }, { 25, "High level" }, { 27, "Lo-fi" },
            { 28, "Hold" }, { 29, "Pitch" } };
        auto add = [&] (int index, const char* name, double low = 0.0, double high = -1.0) {
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            index += offset;
            if (high > low)
                knob->setRange (low, high, 1.0);
            else
                knob->setRange (0.0, boobass ? 65535.0 : 1.0, boobass ? 1.0 : 0.001);
            knob->setValue (index < (int) def.modelParams.size() ? def.modelParams[(std::size_t) index] : 0.0, juce::dontSendNotification);
            knob->setName (name);
            knob->onValueChange = [this, index, k = knob.get()] {
                ref.edit (engine, [index, v = (float) k->getValue()] (ChannelDef& channelDef) {
                    auto& params = channelDef.modelParams;
                    if (index < (int) params.size())
                        params[(std::size_t) index] = v;
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        static const std::pair<int, const char*> booBassKnobs[] = { { 0, "Bass" }, { 1, "Mid" }, { 2, "Treble" } };
        struct Ranged { int index; const char* name; double low, high; };
        static const Ranged drumSynthKnobs[] = {
            { 0, "Level", 0, 128 }, { 1, "Pan", -64, 64 }, { 2, "Tone", -50, 50 }, { 3, "Harmonics", -50, 50 },
            { 4, "Osc1 freq", 0, 1000 }, { 7, "Osc1 decay", 0, 127 }, { 8, "Osc1 level", 0, 127 }, { 10, "Osc2 freq", 0, 1000 },
            { 13, "Osc2 decay", 0, 127 }, { 14, "Osc2 level", 0, 127 }, { 17, "Wave mix", 0, 127 }, { 21, "Release", 0, 127 },
            { 23, "Click", 0, 100 }, { 33, "Ring mod", 0, 127 } };
        // Harmless: raw FL values (REA_rodada21/32 ranges)
        static const Ranged harmlessKnobs[] = {
            { 36, "Volume", 0, 2000 }, { 26, "Attack", 0, 1000 }, { 27, "Decay", 0, 1000 }, { 28, "Release", 0, 1000 },
            { 54, "Cutoff", 0, 8192 }, { 59, "Resonance", 0, 1000 }, { 52, "Filter env", -1000, 1000 },
            { 31, "Pluck", 0, 1000 }, { 23, "Mask mix", 0, 1000 }, { 65, "Unison", 1, 9 }, { 67, "Uni detune", 0, 256 },
            { 25, "Tremolo", 0, 1000 }, { 71, "Phaser mix", 0, 1000 }, { 79, "Harmonizer", 0, 1000 } };
        // Harmor: raw FL integers inside the saved state, both parts and the global strip (core/HarmorControls.h, round 172a)
        auto addHarmor = [&] (const harmorui::Control& k) {
            const auto at = harmorParamOffset (def.pluginState, k.part, k.index);
            if (at < 0)
                return;
            std::int32_t value = 0;
            std::memcpy (&value, def.pluginState.data() + at, 4);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (k.low, k.high, 1.0);
            knob->setValue (k.invert ? k.low + k.high - value : value, juce::dontSendNotification);
            knob->setName (juce::String::fromUTF8 (k.name.c_str()));
            knob->onValueChange = [this, part = k.part, index = k.index, flip = k.invert ? k.low + k.high : 0, s = knob.get()] {
                const auto shown = (std::int32_t) s->getValue();
                ref.edit (engine, [part, index, v = flip != 0 ? flip - shown : shown] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (const auto o = harmorParamOffset (st, part, index); o >= 0)
                        std::memcpy (st.data() + o, &v, 4);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // GMS / Morphine: the plugin's own 0…1 floats inside the saved state (GMS P[i], Morphine global A[i]; REA_rodada39)
        struct FloatKnob { int index; const char* name; };
        static const FloatKnob gmsKnobs[] = {
            { 0, "Master" }, { 1, "Pan" }, { 8, "Level" }, { 4, "Attack" }, { 5, "Decay" }, { 6, "Sustain" }, { 7, "Release" },
            { 12, "Cutoff" }, { 13, "Resonance" }, { 16, "Env1 amount" }, { 50, "Modulation" }, { 48, "Osc2 mix" }, { 49, "Osc3 mix" },
            { 45, "Noise" }, { 41, "Unison" }, { 42, "Uni detune" }, { 43, "Stereo" }, { 10, "Slide time" } };
        static const FloatKnob morphineKnobs[] = {
            { 0, "Master" }, { 9, "Attack" }, { 10, "Decay" }, { 11, "Sustain" }, { 12, "Release" }, { 1, "Glide" }, { 4, "Vel curve" },
            { 7, "Overdrive" }, { 14, "Chorus mix" }, { 21, "Delay mix" }, { 25, "Reverb mix" }, { 36, "Noise mix" } };
        // BassDrum: its 23 floats 0…1 inside the saved state (REA_rodada15 §12.3 / rodada43 §2.2)
        static const FloatKnob bassDrumKnobs[] = {
            { 0, "Output" }, { 6, "Base" }, { 7, "Peak" }, { 8, "Slide" }, { 2, "Duration" }, { 5, "Attack" }, { 4, "Drive" },
            { 3, "Butt" }, { 15, "Click level" }, { 14, "Click freq" }, { 13, "Click cut" }, { 21, "Noise mix" }, { 22, "Noise filter" },
            { 20, "Noise pitch" }, { 19, "Noise start" }, { 1, "Sampling" } };
        // MiniSynth: its 43 floats 0…1 inside the saved state (REA_rodada41 §1.2)
        static const FloatKnob miniSynthKnobs[] = {
            { 5, "Osc kind" }, { 6, "Osc mod" }, { 7, "Noise" }, { 1, "Transpose" }, { 8, "Cutoff" }, { 9, "Resonance" },
            { 16, "Filter env" }, { 21, "Attack" }, { 22, "Decay" }, { 23, "Sustain" }, { 24, "Release" }, { 20, "LFO amount" },
            { 19, "LFO rate" }, { 2, "Slide time" } };
        auto paramOffset = [this] (const std::vector<std::uint8_t>& st, int index) {
            return gms ? gmsParamOffset (st, index)
                       : miniSynth ? miniSynthParamOffset (st, index)
                       : poizone ? poizoneParamOffset (st, index)
                       : bassdrum ? bassDrumParamOffset (st, index)
                       : toxic ? toxicParamOffset (st, index) : morphineParamOffset (st, index);
        };
        auto addFloat = [&] (const FloatKnob& k) {
            const auto at = paramOffset (def.pluginState, k.index);
            if (at < 0)
                return;
            float value = 0.0f;
            std::memcpy (&value, def.pluginState.data() + at, 4);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (0.0, 1.0, 0.001);
            knob->setValue (std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f, juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, k, paramOffset, s = knob.get()] {
                ref.edit (engine, [k, paramOffset, v = (float) s->getValue()] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (const auto o = paramOffset (st, k.index); o >= 0)
                        std::memcpy (st.data() + o, &v, 4);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // SimSynth: its 45 doubles 0…1 at the start of the saved state (REA_rodada64 §1.1)
        static const FloatKnob simSynthKnobs[] = {
            { 3, "Osc 1 level" }, { 10, "Osc 2 level" }, { 17, "Osc 3 level" }, { 26, "Cutoff" }, { 27, "Resonance" },
            { 28, "Filter env" }, { 30, "Key track" }, { 35, "Attack" }, { 36, "Decay" }, { 37, "Sustain" }, { 38, "Release" },
            { 22, "LFO rate" }, { 29, "LFO cutoff" }, { 39, "Volume" } };
        auto addDouble = [&] (const FloatKnob& k) {
            const auto at = 8 * (std::size_t) k.index;
            if (! simSynthStateUsable (def.pluginState))
                return;
            double value = 0.0;
            std::memcpy (&value, def.pluginState.data() + at, 8);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (0.0, 1.0, 0.001);
            knob->setValue (std::isfinite (value) ? std::clamp (value, 0.0, 1.0) : 0.0, juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, at, s = knob.get()] {
                ref.edit (engine, [at, v = s->getValue()] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (simSynthStateUsable (st))
                        std::memcpy (st.data() + at, &v, 8);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // FL Slayer: the 28 parameter integers of the saved state (REA_rodada68 §6.1), ranges of the plugin's controls
        static const Ranged slayerKnobs[] = {
            { 0, "Strings", 0, 6 }, { 1, "Coils", 0, 2 }, { 2, "Pickup", 0, 65535 }, { 3, "Tone", 0, 65535 }, { 4, "Slap", 0, 65535 },
            { 5, "Fret", 0, 65535 }, { 6, "Harmonic", 0, 65535 }, { 7, "Damp", 0, 65535 }, { 8, "Vel volume", 0, 65535 },
            { 9, "Vel damp", 0, 65535 }, { 12, "Amp type", 0, 3 }, { 13, "Drive", 0, 65535 }, { 14, "Presence", 0, 65535 },
            { 15, "Feedback", 0, 65535 }, { 16, "EQ low", 0, 65535 }, { 17, "EQ mid", 0, 65535 }, { 18, "EQ high", 0, 65535 },
            { 19, "Cabinet", 0, 3 }, { 20, "Effect", 0, 15 }, { 21, "FX par 1", 0, 65535 }, { 22, "FX par 2", 0, 65535 },
            { 24, "Mode", 0, 5 }, { 25, "Timing", 0, 65535 }, { 26, "Latch", 0, 1 }, { 27, "Glissando", 0, 1 } };
        auto addSlayer = [&] (const Ranged& k) {
            const auto at = 4 + 4 * (std::size_t) k.index;
            if (! slayerStateUsable (def.pluginState) || at + 4 > def.pluginState.size())
                return;
            std::int32_t value = 0;
            std::memcpy (&value, def.pluginState.data() + at, 4);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange ((double) k.low, (double) k.high, 1.0);
            knob->setValue ((double) std::clamp ((int) value, (int) k.low, (int) k.high), juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, at, s = knob.get()] {
                ref.edit (engine, [at, v = (std::int32_t) s->getValue()] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (slayerStateUsable (st) && at + 4 <= st.size())
                        std::memcpy (st.data() + at, &v, 4);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Sytrus: its "Main settings" page, raw FL integers inside the saved (zlib) state (REA_rodada74 §3)
        auto addSytrus = [&] (int setting) {
            const auto value = sytrus::settingValue (def.pluginState, setting);
            if (! value)
                return;
            const auto& d = sytrus::settings()[(std::size_t) setting];
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (d.low, d.high, 1.0);
            knob->setValue (std::clamp (*value, d.low, d.high), juce::dontSendNotification);
            knob->setName (d.name);
            knob->onValueChange = [this, setting, s = knob.get()] {
                ref.edit (engine, [setting, v = (int) s->getValue()] (ChannelDef& channelDef) {
                    sytrus::setSetting (channelDef.pluginState, setting, v);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Ogun: raw FL integers P0…P63 inside the saved (zlib) state (REA_rodada37 §1.3)
        static const Ranged ogunKnobs[] = {
            { 0, "Master vol", 0, 256 }, { 5, "Pre-decay", 0, 1000 }, { 6, "Decay", 0, 10000 }, { 7, "Release", 0, 10000 },
            { 8, "Fullness", -1000, 1000 }, { 9, "Morph", 0, 1000 }, { 10, "Seed 1", 0, 1000 }, { 11, "Seed 2", 0, 1000 },
            { 52, "Richness", 6, 16 }, { 12, "Unison", 1, 9 }, { 15, "Uni pitch", 0, 256 }, { 17, "Cutoff", 0, 256 },
            { 18, "Resonance", 0, 256 }, { 3, "Mod X", 0, 512 }, { 4, "Mod Y", 0, 512 }, { 54, "Quality", 0, 4 } };
        static const Ranged autogunKnobs[] = { { 63, "Magic dust", 0, 1000 } };
        auto addOgun = [&] (const Ranged& k) {
            const auto value = ogunParam (def.pluginState, k.index);
            if (! value)
                return;
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (k.low, k.high, 1.0);
            knob->setValue (*value, juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, index = k.index, s = knob.get()] {
                ref.edit (engine, [index, v = (int) s->getValue()] (ChannelDef& channelDef) {
                    ogunSetParam (channelDef.pluginState, index, v);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Fruit Kick: its six integers (REA_rodada10 §4.2 ranges)
        static const Ranged fruitKickKnobs[] = {
            { 0, "Max freq", -2400, 3600 }, { 1, "Min freq", -1200, 1200 }, { 2, "Freq decay", 0, 256 }, { 3, "Amp decay", 0, 256 },
            { 4, "Click", 0, 64 }, { 5, "Distortion", 0, 128 } };
        auto addFruitKick = [&] (const Ranged& k) {
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (k.low, k.high, 1.0);
            knob->setValue (fruitKickParam (def.pluginState, k.index), juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, index = k.index, s = knob.get()] {
                ref.edit (engine, [index, v = (int) s->getValue()] (ChannelDef& channelDef) {
                    fruitKickSetParam (channelDef.pluginState, index, v);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Fruity DX10: the 22 raw FL integers of the saved state (REA_rodada42 §1.2)
        static const Ranged dx10Knobs[] = {
            { 0, "Attack", 0, 65536 }, { 1, "Decay", 0, 65536 }, { 2, "Release", 0, 65536 }, { 21, "Coarse", -3, 3 },
            { 11, "Waveform", 0, 65536 }, { 10, "Vibrato", 0, 65536 }, { 13, "LFO rate", 0, 65536 },
            { 3, "Mod 1 coarse", 0, 65536 }, { 4, "Mod 1 fine", 0, 65536 }, { 5, "Mod 1 init", 0, 65536 }, { 6, "Mod 1 time", 0, 65536 },
            { 7, "Mod 1 sustain", 0, 65536 }, { 8, "Mod 1 release", 0, 65536 }, { 9, "Mod 1 velocity", 0, 65536 },
            { 14, "Mod 2 coarse", 0, 65536 }, { 15, "Mod 2 fine", 0, 65536 }, { 16, "Mod 2 init", 0, 65536 }, { 17, "Mod 2 time", 0, 65536 },
            { 18, "Mod 2 sustain", 0, 65536 }, { 19, "Mod 2 release", 0, 65536 }, { 20, "Mod 2 velocity", 0, 65536 }, { 12, "Mod thru", 0, 65536 } };
        auto addDx10 = [&] (const Ranged& k) {
            const auto value = dx10Param (def.pluginState, k.index);
            if (! value)
                return;
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (k.low, k.high, 1.0);
            knob->setValue (*value, juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, index = k.index, s = knob.get()] {
                ref.edit (engine, [index, v = (int) s->getValue()] (ChannelDef& channelDef) {
                    dx10SetParam (channelDef.pluginState, index, v);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // 3x Osc: the 21 parameters of the saved state, then invert ×3, AM and HQ (REA_rodada47 §1.1)
        static const char* const threeOscNames[] = {
            "Osc 1 pan", "Osc 1 shape", "Osc 1 coarse", "Osc 1 fine", "Osc 1 phase", "Osc 1 detune", "Osc 2 mix", "Osc 2 pan",
            "Osc 2 shape", "Osc 2 coarse", "Osc 2 fine", "Osc 2 phase", "Osc 2 detune", "Osc 3 mix", "Osc 3 pan", "Osc 3 shape",
            "Osc 3 coarse", "Osc 3 fine", "Osc 3 phase", "Osc 3 detune", "Phase rand", "Invert 1", "Invert 2", "Invert 3",
            "Osc 3 AM", "HQ" };
        auto addThreeOsc = [&] (int index) {
            const auto value = threeOscParam (def.pluginState, index);
            if (! value)
                return;
            const auto [low, high] = threeOscParamRange (index);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (low, high, 1.0);
            knob->setValue (*value, juce::dontSendNotification);
            knob->setName (threeOscNames[index]);
            knob->onValueChange = [this, index, s = knob.get()] {
                ref.edit (engine, [index, v = (int) s->getValue()] (ChannelDef& channelDef) {
                    threeOscSetParam (channelDef.pluginState, index, v);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Fruity Granulizer: the 12 parameters of the saved state (REA_rodada67 §1)
        static const char* const granulizerNames[] = {
            "Grain attack", "Grain hold", "Grain spacing", "Wave spacing", "Effect depth", "Effect speed", "Randomness",
            "Stereo", "Loop", "Freeze", "Sample start", "Transient hold" };
        auto addGranulizer = [&] (int index) {
            const auto value = granulizerParam (def.pluginState, index);
            if (! value)
                return;
            const auto [low, high] = granulizerParamRange (index);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (low, high, 1.0);
            knob->setValue (*value, juce::dontSendNotification);
            knob->setName (granulizerNames[index]);
            knob->onValueChange = [this, index, s = knob.get()] {
                ref.edit (engine, [index, v = (int) s->getValue()] (ChannelDef& channelDef) {
                    granulizerSetParam (channelDef.pluginState, index, v);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Drumaxx: the master level and the 16 pad volumes, 0…1 floats of the saved state (REA_rodada67 §6)
        auto addDrumaxx = [&] (int index, const juce::String& name) {
            const auto value = drumaxxParam (def.pluginState, index);
            if (! std::isfinite (value))
                return;
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (0.0, 1.0, 0.001);
            knob->setValue (std::clamp ((double) value, 0.0, 1.0), juce::dontSendNotification);
            knob->setName (name);
            knob->onValueChange = [this, index, s = knob.get()] {
                ref.edit (engine, [index, v = (float) s->getValue()] (ChannelDef& channelDef) {
                    drumaxxSetParam (channelDef.pluginState, index, v);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Kepler: the panel's 0…1 floats inside the saved state ("Kepler-1" order, REA_rodada46 §1.1)
        static const FloatKnob keplerKnobs[] = {
            { 33, "Volume" }, { 19, "Cutoff" }, { 20, "Resonance" }, { 22, "Env amount" }, { 26, "Attack" }, { 27, "Decay" },
            { 28, "Sustain" }, { 29, "Release" }, { 11, "PW" }, { 16, "Sub level" }, { 17, "Noise" }, { 5, "LFO rate" },
            { 10, "Vibrato" }, { 18, "High-pass" }, { 30, "Chorus" } };
        // Kepler Exo: the same layout with the "Kepler-2" parameter list (REA_rodada167d §1)
        static const FloatKnob keplerExoKnobs[] = {
            { 0x48, "Volume" }, { 0x25, "Cutoff" }, { 0x26, "Resonance" }, { 0x2c, "Env amount" }, { 0x3d, "Attack" },
            { 0x3e, "Decay" }, { 0x3f, "Sustain" }, { 0x40, "Release" }, { 0x21, "DCO2 level" }, { 0x0e, "DCO2 detune" },
            { 0x30, "LFO1 rate" }, { 0x36, "LFO2 rate" }, { 0x50, "Portamento" }, { 0x24, "High-pass" }, { 0x87, "Saturation" },
            { 0x70, "Delay amount" }, { 0x73, "Delay feedback" }, { 0x80, "Reverb decay" }, { 0x85, "Reverb wet" },
            { 0x93, "Hyper chorus mix" } };
        auto addKepler = [&] (const FloatKnob& k) {
            const auto at = keplerPanelOffset (def.pluginState, k.index);
            if (at < 0)
                return;
            float value = 0.0f;
            std::memcpy (&value, def.pluginState.data() + at, 4);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (0.0, 1.0, 0.001);
            knob->setValue (std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f, juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, k, s = knob.get()] {
                ref.edit (engine, [k, v = (float) s->getValue()] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (const auto o = keplerPanelOffset (st, k.index); o >= 0)
                        std::memcpy (st.data() + o, &v, 4);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Plucked!: the five int32 knobs of the saved state (REA_rodada65 §1.1)
        static const Ranged pluckedKnobs[] = { { 0, "Decay", 0, 256 }, { 1, "Color", 0, 128 }, { 2, "Normalize", 0, 1 },
                                               { 3, "Gate", 0, 1 }, { 4, "Widen", 0, 1 } };
        auto addPlucked = [&] (const Ranged& k) {
            if (def.pluginState.size() < 20)
                return;
            std::int32_t value = 0;
            std::memcpy (&value, def.pluginState.data() + 4 * k.index, 4);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (k.low, k.high, 1.0);
            knob->setValue (std::clamp ((double) value, k.low, k.high), juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, k, s = knob.get()] {
                ref.edit (engine, [k, v = (std::int32_t) s->getValue()] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (st.size() >= 20)
                        std::memcpy (st.data() + 4 * k.index, &v, 4);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // BeepMap: the int32 settings of the saved state (REA_rodada65 §2.1; Frequency's range not confirmed)
        static const Ranged beepMapKnobs[] = { { 0, "Frequency", 0, 9600 }, { 1, "Length", 0, 4096 }, { 2, "Scale", 0, 2 },
                                               { 3, "Use blue", 0, 1 }, { 4, "Grainy", 0, 1 }, { 5, "Loop", 0, 1 }, { 6, "Widen", 0, 1 } };
        auto addBeepMap = [&] (const Ranged& k) {
            const auto at = beepMapParamOffset (def.pluginState, k.index);
            if (at < 0)
                return;
            std::int32_t value = 0;
            std::memcpy (&value, def.pluginState.data() + at, 4);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (k.low, k.high, 1.0);
            knob->setValue (std::clamp ((double) value, k.low, k.high), juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, k, s = knob.get()] {
                ref.edit (engine, [k, v = (std::int32_t) s->getValue()] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (const auto o = beepMapParamOffset (st, k.index); o >= 0)
                        std::memcpy (st.data() + o, &v, 4);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // Sawer: 0…1 floats of the saved state (REA_rodada65 §3.3)
        static const FloatKnob sawerKnobs[] = {
            { 0, "Level" }, { 27, "Cutoff" }, { 28, "Resonance" }, { 36, "EGF amount" }, { 2, "Attack" }, { 3, "Decay" },
            { 4, "Sustain" }, { 5, "Release" }, { 19, "Sub level" }, { 18, "Sub harmonic" }, { 26, "Noise" }, { 14, "Unison" },
            { 15, "Uni detune" }, { 45, "LFO amount" } };
        auto addSawer = [&] (const FloatKnob& k) {
            const auto at = sawerParamOffset (def.pluginState, k.index);
            if (at < 0)
                return;
            float value = 0.0f;
            std::memcpy (&value, def.pluginState.data() + at, 4);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (0.0, 1.0, 0.001);
            knob->setValue (std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f, juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, k, s = knob.get()] {
                ref.edit (engine, [k, v = (float) s->getValue()] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (const auto o = sawerParamOffset (st, k.index); o >= 0)
                        std::memcpy (st.data() + o, &v, 4);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // FL Keys: the 15 int32 knobs of the saved state (REA_rodada69 §8.1)
        auto addFlKeys = [&] (int index) {
            const auto state = flkeys::parseState (def.pluginState);
            if (! state)
                return;
            const auto& r = flkeys::knobRange[index];
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (r.min, r.max, 1.0);
            knob->setValue (std::clamp ((double) state->knobs[(std::size_t) index], (double) r.min, (double) r.max), juce::dontSendNotification);
            knob->setName (r.name);
            knob->onValueChange = [this, index, s = knob.get()] {
                ref.edit (engine, [index, v = (int) s->getValue()] (ChannelDef& channelDef) {
                    auto& st = channelDef.pluginState;
                    if (auto parsed = flkeys::parseState (st))
                    {
                        parsed->knobs[(std::size_t) index] = v;
                        st = flkeys::writeState (*parsed);
                    }
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        // an engine whose parameters are listed (core/ModelParams.h, round 168n): one knob per parameter, automatable
        // (right-click: FL's parameter menu; a move is a tweak)
        instrumentKind = def.instrument;
        modelTable = modelparams::tableOf (def.instrument);
        if (modelTable != nullptr && ! modelparams::rawValue (def, modelTable->params->front().index))
            modelTable = nullptr; // (a state the engine can't read: the plain page)
        if (modelTable != nullptr && (drumaxx || drumsynth))
        {
            // round 168p: Drumaxx's pad voice / DrumSynth Live's patch: one knob per control, editing the selected
            // pad's / key's parameter (the pad buttons / the key display pick the part); the rest as listed
            drumPart = drumsynth ? editorKey : drumaxxPadOf (ref.index);
            for (const auto& c : drumcontrols::controls (def.instrument))
                if (c.perPart)
                    addPartParam (def, c);
                else if (const auto* p = modelparams::find (def.instrument, c.index))
                    addModelParam (def, *p);
        }
        else if (modelTable != nullptr)
        {
            for (const auto& p : *modelTable->params)
                addModelParam (def, p);
            // Toxic Biohazard without the user's DLL: the drawn wave (0) and the sine (1) only — the built-in waves
            // 2…32 are FL's files
            if (toxic && ! toxicDllUsable (def.samplePath))
                for (auto& k : knobs)
                    if (k->getName().endsWith (" wave"))
                        k->setRange (0.0, 1.0, 1.0);
        }
        else if (flKeys)
            for (int i = 0; i < 15; ++i)
                addFlKeys (i);
        else if (plucked)
            for (const auto& k : pluckedKnobs)
                addPlucked (k);
        else if (beepMap)
            for (const auto& k : beepMapKnobs)
                addBeepMap (k);
        else if (sawer)
            for (const auto& k : sawerKnobs)
                addSawer (k);
        else if (threeOsc)
            for (int i = 0; i < threeOscParamCount(); ++i)
                addThreeOsc (i);
        else if (granulizer)
        {
            for (int i = 0; i < granulizerParamCount(); ++i)
                addGranulizer (i);
            setupGranulizerModes (def);
        }
        else if (drumaxx)
        {
            addDrumaxx (0x2c1, "Master");
            for (int pad = 0; pad < 16; ++pad)
                addDrumaxx (pad * 44, "Pad " + juce::String (pad + 1));
        }
        else if (dx10)
            for (const auto& k : dx10Knobs)
                addDx10 (k);
        else if (kepler)
            for (const auto& k : keplerKnobs)
                addKepler (k);
        else if (keplerExo)
            for (const auto& k : keplerExoKnobs)
                addKepler (k);
        else if (gms)
            for (const auto& k : gmsKnobs)
                addFloat (k);
        else if (bassdrum)
            for (const auto& k : bassDrumKnobs)
                addFloat (k);
        else if (fruitkick)
            for (const auto& k : fruitKickKnobs)
                addFruitKick (k);
        else if (morphine)
            for (const auto& k : morphineKnobs)
                addFloat (k);
        else if (miniSynth)
            for (const auto& k : miniSynthKnobs)
                addFloat (k);
        else if (simSynth)
            for (const auto& k : simSynthKnobs)
                addDouble (k);
        else if (wasp || fruityDance)
            ;
        else if (slayer)
            for (const auto& k : slayerKnobs)
                addSlayer (k);
        else if (sytrus)
            for (int i = 0; i < (int) sytrus::settings().size(); ++i)
                addSytrus (i);
        else if (ogun)
        {
            for (const auto& k : ogunKnobs)
                addOgun (k);
            if (autogun)
                for (const auto& k : autogunKnobs)
                    addOgun (k);
        }
        else if (harmor)
            for (const auto& k : harmorui::controls())
                addHarmor (k);
        else if (harmless)
            for (const auto& k : harmlessKnobs)
                add (k.index, k.name, k.low, k.high);
        else if (drumsynth)
            for (const auto& k : drumSynthKnobs)
                add (k.index, k.name, k.low, k.high);
        else if (boobass)
            for (const auto& [index, name] : booBassKnobs)
                add (index, name);
        else if (sakura)
            for (const auto& [index, name] : sakuraKnobs)
                add (index, name);
        else
            for (const auto& [index, name] : drumpadKnobs)
                add (index, name);
        const auto rows = ((int) knobs.size() + 6) / 7; // the grid of resized()
        setSize (720, std::max (dx10 || threeOsc ? 500 : 384, 46 + rows * 110 + 8 + (granulizerModes ? 64 : 0)));

        // a Granulizer node inside a Patcher (round 165c): no channel to drop a sample on, so its own Sample button
        if (granulizer && ref.index < 0)
        {
            sampleButton.setButtonText (tr ("Sample..."));
            const auto path = juce::String::fromUTF8 (def.samplePath.c_str());
            ui::setHint (sampleButton, path.isEmpty() ? tr ("Choose the sample the Granulizer plays") : path);
            sampleButton.onClick = [this] {
                juce::StringArray patterns;
                for (const auto e : sampleFileExtensions)
                    patterns.add ("*." + juce::String (std::string (e)));
                chooser = lynx::ui::makeFileChooser (tr ("Sample"), juce::File(), patterns.joinIntoString (";"));
                lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [safe = juce::Component::SafePointer<ModelPanel> (this)] (const juce::FileChooser& fc) {
                                          if (safe == nullptr || fc.getResult() == juce::File())
                                              return;
                                          safe->loadGranulizerSample (fc.getResult());
                                      });
            };
            addAndMakeVisible (sampleButton);
            resized(); // (setSize above laid the page out before the button existed)
        }

        // FL's arrangement of the plugin's page (core/PluginLayout.cpp): the knobs at FL's places, faders and
        // switches drawn by the panel
        // (a Transistor Bass from a .flm: FL Studio Mobile's build has no sequencer, delay or reverb)
        layout = transistorBassMobile ? plugui::transistorBassFlmLayout() : plugui::instrumentLayout (def.instrument);
        if (layout != nullptr && plugui::hostedElsewhere) // (round 180b: the plate prints the plugin's name)
        {
            hostedName = def.name;
            hostedLayout = *layout;
            for (auto& item : hostedLayout.items)
                if ((item.style & plugui::enginePlate) != 0)
                    item.label = hostedName.c_str();
            layout = &hostedLayout;
        }
        // a state the engine cannot read gives no knobs: keep the plain page then
        if (layout != nullptr && ! wasp && std::none_of (knobs.begin(), knobs.end(), [this] (const auto& k) { return itemFor (k->getName()) != nullptr; }))
            layout = nullptr;
        if (layout != nullptr)
        {
            accent = flKeys ? skin::colour::azure : boobass || transistorBass ? skin::colour::ember : harmor || sytrus ? skin::colour::violet
                                                                                                                                           : skin::colour::crimson;
            // round 126: the page's own style (FL Keys hardware, BooBass vivid, the synths neon); round 170j: a plugin
            // with its own art (Sakura, Toxic Biohazard, Sytrus, 3x Osc) is Themed
            pageStyle = flKeys ? skin::Style::hardware : boobass || transistorBass ? skin::Style::vivid : skin::Style::neon;
            pageArt = skin::artForInstrument (def.instrument);
            if (pageArt != skin::Art::none)
            {
                pageStyle = skin::Style::themed;
                artLayer = std::make_unique<skin::ArtLayer>();
                addAndMakeVisible (*artLayer);
                artLayer->setArt (pageArt, [this] { return themedNow(); });
            }
            for (auto& k : knobs)
            {
                k->getProperties().set (skin::sliderProperty, (juce::int64) accent.getARGB());
                k->getProperties().set (skin::styleProperty, skin::packStyle (pageStyle, pageArt));
                const auto* item = itemFor (k->getName());
                if (item == nullptr || (item->type != plugui::Item::knob && item->type != plugui::Item::vslider && item->type != plugui::Item::hslider))
                {
                    k->setVisible (false); // edited by a switch of the panel (or not shown by FL)
                    continue;
                }
                k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                if (item->type == plugui::Item::vslider)
                    k->setSliderStyle (juce::Slider::LinearVertical);
                if ((item->style & plugui::springBack) != 0) // (a pitch bend wheel: back to the centre when let go)
                    k->onDragEnd = [s = k.get(), before = k->onDragEnd] {
                        s->setValue (0.5 * (s->getMinimum() + s->getMaximum()), juce::sendNotificationSync);
                        if (before)
                            before();
                    };
                if (item->type == plugui::Item::hslider)
                    k->setSliderStyle (juce::Slider::LinearHorizontal);
                k->setPopupDisplayEnabled (true, true, this);
                k->setTooltip (k->getName());
            }
            updateConditional();
            // FL's size × 1.25, at most about 1200 × 760 (big pages such as Harmor's fit a laptop screen)
            const auto f = juce::jmin (layoutScale, 1200.0f / (float) layout->width, 720.0f / (float) layout->height);
            setSize ((int) std::ceil (f * (float) layout->width) + 16, (int) std::ceil (f * (float) layout->height) + layoutTop + 8);
        }
        if (transistorBass && ref.index >= 0)
            startTimerHz (10); // the page follows the program a note selects (round 169g2)
    }

    // Transistor Bass with its sequencer on: a note selects program = key (REA_rodada57 §2.10) and FL's editor shows
    // that program, so its knobs edit what plays. The channel's state takes the playing program (not an undo step)
    // and every knob reads it again (round 169g2)
    void timerCallback() override
    {
        const auto* c = channelNow();
        const auto instrument = engine.getChannelInstrument (ref.index);
        if (c == nullptr || instrument == nullptr)
            return;
        const auto selected = instrument->selectedProgram();
        const auto prog = transistorBassProgram (c->pluginState);
        if (selected < 0 || ! prog || prog->program == selected)
            return;
        auto next = c->pluginState;
        if (! transistorBassSelectProgram (next, selected))
            return;
        ref.edit (engine, [next = std::move (next)] (ChannelDef& d) { d.pluginState = next; }, false);
        syncKnobs (true);
        repaint();
    }

    // a knob of a listed engine parameter (round 168n): raw range of the state, automation target channelParam
    void addModelParam (const ChannelDef& def, const EngineParam& p)
    {
        auto knob = std::make_unique<MenuSlider>();
        knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        knob->setRange (p.low, p.high, p.stepped ? 1.0 : 0.0);
        knob->setValue (modelparams::rawValue (def, p.index).value_or (p.low), juce::dontSendNotification);
        knob->setDoubleClickReturnValue (false, 0.0);
        knob->setName (juce::String::fromUTF8 (p.name.c_str()));
        const auto index = p.index;
        knob->onValueChange = [this, index, s = knob.get()] {
            ref.edit (engine, [index, v = (float) s->getValue()] (ChannelDef& c) {
                modelparams::setRawValue (c, index, v);
                natives::followControl (c, index, flStudioAccess()); // (Harmless' compression type loads its preset)
            });
            if (instrumentKind == InstrumentKind::transistorbass && index >= tbseq::length) // (a step's note moves its octave)
                syncKnobs();
            if (layout != nullptr)
            {
                updateConditional();
                repaint();
            }
        };
        if (ref.index >= 0) // (a Patcher node: no automation target)
        {
            const auto target = [this, index] { return AutomationTarget { AutomationTargetKind::channelParam, ref.index, 0, index }; };
            ui::setOwnMenu (*knob);
            knob->onMenu = [this, k = knob.get(), target] { ui::showSliderMenu (*k, &engine, target()); };
            knob->onDragStart = [this, target] { engine.noteTweaked (target(), true); };
            knob->onDragEnd = [this, target] { engine.tweakReleased (target()); };
        }
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }

    // round 168p: a knob of Drumaxx's selected pad / DrumSynth Live's selected key (drumcontrols' per-part control):
    // named as the control (the layout places it), editing and automating the part's engine parameter
    void addPartParam (const ChannelDef& def, const drumcontrols::Control& c)
    {
        auto knob = std::make_unique<MenuSlider>();
        knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        knob->setRange (c.low, c.high, c.integer ? 1.0 : 0.0);
        knob->setValue (drumcontrols::value (def, c, drumPart).value_or (c.low), juce::dontSendNotification);
        knob->setDoubleClickReturnValue (false, 0.0);
        knob->setName (c.name);
        const auto* control = &c;
        knob->onValueChange = [this, control, s = knob.get()] {
            ref.edit (engine, [control, part = drumPart, v = (float) s->getValue()] (ChannelDef& d) { drumcontrols::setValue (d, *control, v, part); });
            if (layout != nullptr)
                repaint();
        };
        if (ref.index >= 0)
        {
            const auto target = [this, control] {
                return AutomationTarget { AutomationTargetKind::channelParam, ref.index, 0, drumcontrols::absoluteIndex (instrumentKind, *control, drumPart) };
            };
            ui::setOwnMenu (*knob);
            knob->onMenu = [this, k = knob.get(), target] { ui::showSliderMenu (*k, &engine, target()); };
            knob->onDragStart = [this, target] { engine.noteTweaked (target(), true); };
            knob->onDragEnd = [this, target] { engine.tweakReleased (target()); };
        }
        partKnobs.push_back ({ knob.get(), control });
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }

    // the pad remembered for each Drumaxx channel while LYNX runs (FL's editor keeps it, not the state)
    static int& drumaxxPadOf (int channel)
    {
        static std::map<int, int> pads;
        return pads[channel];
    }

    // another pad / key selected: the part's knobs show its values (no rebuild)
    void selectPart (int part)
    {
        const auto* def = ref.get (engine);
        if (def == nullptr || part == drumPart)
            return;
        drumPart = part;
        if (drumaxx)
            drumaxxPadOf (ref.index) = part;
        if (drumsynth)
        {
            editorKey = part;
            ref.edit (engine, [part] (ChannelDef& d) { // FL keeps the selected key in the state
                if (lynx::drumSynthEditorKeyIndex < (int) d.modelParams.size())
                    d.modelParams[(std::size_t) lynx::drumSynthEditorKeyIndex] = (float) part;
            });
        }
        for (const auto& [k, c] : partKnobs)
            k->setValue (drumcontrols::value (*def, *c, drumPart).value_or (c->low), juce::dontSendNotification);
        repaint();
    }

    // the listed parameters' knobs read the channel's state again (an edit that moved other values: Transistor Bass's
    // note / octave, CLEAR, octave buttons; round 168q2)
    void syncKnobs (bool all = false)
    {
        const auto* c = channelNow();
        if (c == nullptr || modelTable == nullptr)
            return;
        for (auto& k : knobs)
            if (const auto* p = modelparams::findByName (instrumentKind, k->getName().toStdString()); p != nullptr && (all || p->index >= tbseq::length))
                if (const auto v = modelparams::rawValue (*c, p->index))
                    k->setValue (*v, juce::dontSendNotification);
    }

    // the items shown only while another control is within a range (LayoutItem::whenParam, e.g. an effect slot's
    // fields for its type): their knobs follow
    bool conditionHolds (const plugui::LayoutItem& item) const
    {
        auto holds = [this] (const char* param, float lo, float hi) {
            if (param == nullptr)
                return true;
            const auto* k = knobFor (param);
            if (k == nullptr)
                return true;
            const auto v = normalised (*k); // (0…1 over the knob's range, as the effect layouts' conditions)
            return v >= lo && (v < hi || (hi >= 1.0f && v <= hi));
        };
        return (item.page < 0 || item.page == page) && holds (item.whenParam, item.whenMin, item.whenMax)
               && holds (item.when2Param, item.when2Min, item.when2Max) && holds (item.when3Param, item.when3Min, item.when3Max);
    }

    int pageCount() const { return layout != nullptr ? (int) layout->pages.size() : 0; }

    void showPage (int p)
    {
        page = p;
        updateConditional();
        repaint();
    }

    void updateConditional()
    {
        if (layout == nullptr)
            return;
        for (auto& k : knobs)
            if (const auto* item = itemFor (k->getName()); item != nullptr && (item->type == plugui::Item::knob || item->type == plugui::Item::vslider || item->type == plugui::Item::hslider))
                k->setVisible (conditionHolds (*item));
    }

    // --- FL's arrangement ------------------------------------------------------------------------------

    static constexpr float layoutScale = 1.25f;
    static constexpr int layoutTop = 34;

    const plugui::LayoutItem* itemFor (const juce::String& name) const
    {
        if (layout == nullptr)
            return nullptr;
        const plugui::LayoutItem* any = nullptr;
        for (const auto& item : layout->items) // the knob / fader of the parameter first (a pad may share it)
            if (item.param != nullptr && name == item.param)
            {
                if (item.type == plugui::Item::knob || item.type == plugui::Item::vslider)
                    return &item;
                if (any == nullptr && item.type != plugui::Item::display)
                    any = &item;
            }
        return any;
    }

    juce::Slider* knobFor (const char* name) const
    {
        for (const auto& k : knobs)
            if (name != nullptr && k->getName() == name)
                return k.get();
        return nullptr;
    }

    float layoutFactor() const
    {
        if (layout == nullptr)
            return 1.0f;
        return juce::jmax (0.4f, juce::jmin ((float) (getWidth() - 16) / (float) layout->width, (float) (getHeight() - layoutTop - 8) / (float) layout->height));
    }

    juce::Rectangle<float> layoutRect (const plugui::LayoutItem& item) const
    {
        const auto f = layoutFactor();
        return { 8.0f + item.x * f, (float) layoutTop + item.y * f, item.w * f, item.h * f };
    }

    static float normalised (const juce::Slider& k)
    {
        const auto range = k.getMaximum() - k.getMinimum();
        return range > 0.0 ? (float) ((k.getValue() - k.getMinimum()) / range) : 0.0f;
    }

    static void setNormalised (juce::Slider& k, float v)
    {
        k.setValue (k.getMinimum() + (double) v * (k.getMaximum() - k.getMinimum()), juce::sendNotificationSync);
    }

    // the radio's option cell (grids fill row by row)
    juce::Rectangle<float> optionCell (const plugui::LayoutItem& item, int o, int count) const
    {
        const auto r = layoutRect (item);
        const auto columns = juce::jmax (1, item.columns);
        const auto rows = (count + columns - 1) / columns;
        const auto w = r.getWidth() / (float) columns, h = r.getHeight() / (float) juce::jmax (1, rows);
        return { r.getX() + w * (float) (o % columns), r.getY() + h * (float) (o / columns), w, h };
    }

    // a tabs item's segment o of `count` (left to right)
    juce::Rectangle<float> tabCell (const plugui::LayoutItem& item, int o, int count) const
    {
        const auto r = layoutRect (item);
        const auto w = r.getWidth() / (float) juce::jmax (1, count);
        return { r.getX() + w * (float) o, r.getY(), w, r.getHeight() };
    }

    // a small drawing of an oscillator shape (3x Osc's shape buttons)
    static void drawShape (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& name)
    {
        juce::Path p;
        const auto x0 = r.getX(), w = r.getWidth(), yc = r.getCentreY(), a = r.getHeight() * 0.5f;
        const auto n = 48;
        for (int i = 0; i <= n; ++i)
        {
            const auto t = (float) i / (float) n;
            float y = 0.0f;
            if (name == "Sine") y = std::sin (t * juce::MathConstants<float>::twoPi);
            else if (name == "Square") y = t < 0.5f ? 1.0f : -1.0f;
            else if (name == "Rounded") y = t < 0.46f ? 1.0f - 2.0f * std::pow (0.997f, t * 16384.0f) : (t < 0.83f ? 1.0f : -1.0f);
            else if (name == "Triangle") y = t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f);
            else if (name == "Saw") y = 2.0f * t - 1.0f;
            else if (name == "Noise") y = std::sin ((float) i * 12.9898f) * std::cos ((float) i * 4.1414f) * 1.6f;
            else /* custom */ y = std::sin (t * juce::MathConstants<float>::twoPi * 3.0f) * std::exp (-std::abs (t - 0.5f) * 5.0f);
            y = juce::jlimit (-1.0f, 1.0f, y);
            const juce::Point<float> pt { x0 + w * t, yc - a * y };
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // round 170j: the page draws its own art right now (Settings may force another style)
    bool themedNow() const { return pageArt != skin::Art::none && skin::resolve (pageStyle, pageArt) == skin::Style::themed; }
    juce::Colour pageAccent() const { return themedNow() ? skin::artAccent (pageArt) : accent; }

    // where the art's moving part lives: next to the plugin's big name (Toxic's trefoil, Sytrus's figure, 3x Osc's
    // scope); Sakura's petals fall in the band of the branch, above the first row of sections (a small area to repaint)
    juce::Rectangle<float> artStage() const
    {
        if (layout == nullptr)
            return {};
        const auto f = layoutFactor();
        if (pageArt == skin::Art::sakura)
        {
            auto top = (float) getHeight();
            for (const auto& item : layout->items)
                if (item.type == plugui::Item::section && layoutRect (item).getY() > (float) getHeight() * 0.18f)
                    top = juce::jmin (top, layoutRect (item).getY());
            return { 0.0f, 30.0f, (float) getWidth(), juce::jmax (40.0f, top - 30.0f) };
        }
        for (const auto& item : layout->items)
            if (item.type == plugui::Item::label && item.label != nullptr && (item.style & plugui::dim) != 0 && item.h * f > 30.0f * f
                && juce::String (item.label).length() > 2)
            {
                const auto r = layoutRect (item);
                const auto w = juce::GlyphArrangement::getStringWidth (theme::font (r.getHeight() * 0.6f, true), item.label);
                const auto left = (item.style & plugui::alignLeft) != 0;
                const auto x0 = left ? r.getX() : r.getRight() - juce::jmin (w, r.getWidth());
                if (pageArt == skin::Art::toxic)
                    return { x0 - r.getHeight() * 1.5f, r.getY() - r.getHeight() * 0.15f, r.getHeight() * 1.3f, r.getHeight() * 1.3f };
                return { left ? x0 + w + 12.0f : x0 - r.getHeight() * 2.6f - 12.0f, r.getY() - r.getHeight() * 0.1f, r.getHeight() * 2.6f, r.getHeight() * 1.2f };
            }
        return {};
    }

    void paintLayout (juce::Graphics& g)
    {
        using namespace theme;
        const skin::StyleScope styleScope (skin::resolve (pageStyle, pageArt), pageArt);
        const auto accent = pageAccent(); // (the art's own light when Themed)
        const auto f = layoutFactor();
        const auto* found = ref.get (engine);
        if (found == nullptr)
            return;
        const auto& def = *found;
        const auto caption = [&] (const char* text, juce::Rectangle<float> r, bool dimmed, juce::Justification j) {
            if (text == nullptr || *text == 0)
                return;
            g.setColour (dimmed ? colour::textDisabled : colour::textSecondary);
            g.setFont (font (juce::jmax (8.0f, 9.5f * f), true));
            g.drawFittedText (caps (tr (text)), r.toNearestInt(), j, 1, 0.7f);
        };

        for (const auto& item : layout->items)
        {
            if (! conditionHolds (item))
                continue;
            const auto r = layoutRect (item);
            // round 170j: a partial repaint (the art's petals) draws only the items it touches (captions may hang
            // up to ~26 units past an item's sides)
            if (! g.clipRegionIntersects (r.expanded (32.0f * f, 8.0f * f).getSmallestIntegerContainer()))
                continue;
            const auto dimmed = (item.style & plugui::dim) != 0;
            auto* k = knobFor (item.param);
            switch (item.type)
            {
                case plugui::Item::tabs: // a row of segmented buttons (round 168n); pageTabs: the layout's pages
                    if ((item.style & plugui::pageTabs) != 0)
                    {
                        const auto labels = plugui::optionLabels (item);
                        for (int o = 0; o < (int) labels.size(); ++o)
                        {
                            const auto cell = tabCell (item, o, (int) labels.size()).reduced (1.5f * f, 2.0f * f);
                            skin::button (g, cell, o == page, false, accent, radius::sm * f);
                            g.setColour (o == page ? colour::onAccent : colour::textSecondary);
                            g.setFont (font (juce::jmax (8.0f, juce::jmin (11.0f * f, cell.getHeight() * 0.62f)), true));
                            g.drawFittedText (tr (labels[(std::size_t) o]), cell.reduced (2.0f * f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
                        }
                    }
                    else if (k != nullptr)
                    {
                        const auto labels = plugui::optionLabels (item);
                        const auto chosen = plugui::optionAt (item, normalised (*k));
                        for (int o = 0; o < (int) labels.size(); ++o)
                        {
                            const auto cell = tabCell (item, o, (int) labels.size()).reduced (1.5f * f, 2.0f * f);
                            skin::button (g, cell, o == chosen, false, accent, radius::sm * f);
                            g.setColour (o == chosen ? colour::onAccent : colour::textSecondary);
                            g.setFont (font (juce::jmax (8.0f, juce::jmin (11.0f * f, cell.getHeight() * 0.62f)), true));
                            g.drawFittedText (tr (labels[(std::size_t) o]), cell.reduced (2.0f * f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
                        }
                    }
                    break;
                case plugui::Item::menu: // a choice list: the current option and ▾ (round 168n)
                    if (k != nullptr)
                    {
                        const auto labels = plugui::optionLabels (item);
                        const auto chosen = plugui::optionAt (item, normalised (*k));
                        auto box = r;
                        if (item.label != nullptr && (item.style & plugui::noCaption) == 0)
                            caption (item.label, box.removeFromTop (12.0f * f), dimmed, juce::Justification::centredLeft);
                        skin::valuePlate (g, box, radius::sm * f);
                        g.setColour (colour::textPrimary);
                        g.setFont (font (juce::jmax (8.0f, juce::jmin (11.0f * f, box.getHeight() * 0.62f)), true));
                        const auto text = juce::isPositiveAndBelow (chosen, (int) labels.size()) ? tr (labels[(std::size_t) chosen]) : juce::String();
                        g.drawFittedText (text, box.reduced (4.0f * f, 0.0f).withTrimmedRight (10.0f * f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
                        g.setColour (colour::textSecondary);
                        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe"), box.withTrimmedLeft (box.getWidth() - 12.0f * f), juce::Justification::centred, false);
                    }
                    break;
                case plugui::Item::value: // the control's value as text (round 168n)
                    if (k != nullptr)
                    {
                        skin::valuePlate (g, r, radius::sm * f);
                        g.setColour (colour::textPrimary);
                        g.setFont (font (juce::jmax (8.0f, juce::jmin (11.0f * f, r.getHeight() * 0.7f)), true));
                        g.drawFittedText (k->getTextFromValue (k->getValue()), r.reduced (3.0f * f, 0.0f).toNearestInt(),
                                          (item.style & plugui::alignLeft) != 0 ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.6f);
                    }
                    break;
                case plugui::Item::section:
                    skin::section (g, r, radius::lg * f, (item.style & plugui::raised) != 0, accent);
                    if (item.label != nullptr && (item.style & plugui::boxed) == 0)
                        skin::sectionTitle (g, r.withHeight (16.0f * f).translated (0.0f, 1.0f * f), caps (tr (item.label)), accent, juce::jmax (8.0f, 10.0f * f));
                    break;
                case plugui::Item::label:
                    if (item.label != nullptr && juce::String (item.label).length() <= 2 && dimmed) // big dim numbers (3x Osc rows)
                    {
                        g.setColour (colour::bg4);
                        g.setFont (font (r.getHeight() * 0.9f, true));
                        g.drawText (item.label, r, juce::Justification::centred, false);
                    }
                    else if (dimmed && r.getHeight() > 30.0f * f) // the plugin's name, big and readable (round 168q2)
                    {
                        g.setColour (themedNow() ? accent.interpolatedWith (juce::Colours::white, 0.55f) : colour::textSecondary);
                        g.setFont (font (r.getHeight() * 0.6f, true));
                        g.drawFittedText (item.label, r.toNearestInt(),
                                          (item.style & plugui::alignLeft) != 0 ? juce::Justification::centredLeft : juce::Justification::centredRight, 1, 0.8f);
                    }
                    else
                        caption (item.label, r, dimmed, (item.style & plugui::alignLeft) != 0 ? juce::Justification::centredLeft : juce::Justification::centred);
                    break;
                case plugui::Item::knob: // (a small knob's caption may use some room on both sides, as a fader's does)
                    if ((item.style & plugui::noCaption) == 0)
                        caption (item.label, r.withTop (r.getBottom() - 12.0f * f).expanded (juce::jmax (0.0f, 52.0f * f - r.getWidth()) * 0.5f, 0.0f), dimmed, juce::Justification::centred);
                    break;
                case plugui::Item::vslider:
                case plugui::Item::hslider:
                    if ((item.style & plugui::noCaption) == 0)
                        caption (item.label, r.withTop (r.getBottom() - 12.0f * f).expanded (16.0f * f, 0.0f), dimmed, juce::Justification::centred);
                    break;
                case plugui::Item::radio:
                    if (k != nullptr)
                    {
                        const auto labels = plugui::optionLabels (item);
                        const auto chosen = plugui::optionAt (item, normalised (*k));
                        for (int o = 0; o < (int) labels.size(); ++o)
                        {
                            const auto cell = optionCell (item, o, (int) labels.size()).reduced (3.0f * f);
                            skin::button (g, cell, o == chosen, false, accent, radius::md * f);
                            g.setColour (o == chosen ? accent.brighter (0.5f) : colour::textSecondary);
                            if (item.columns > 0)
                                drawShape (g, cell.reduced (cell.getWidth() * 0.22f, cell.getHeight() * 0.3f), labels[(std::size_t) o]);
                            else
                                g.drawFittedText (tr (labels[(std::size_t) o]), cell.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                        }
                    }
                    break;
                case plugui::Item::toggle:
                    if (k != nullptr)
                    {
                        const auto on = normalised (*k) >= 0.5f;
                        if ((item.style & plugui::boxed) != 0)
                        {
                            const auto box = r.reduced (3.0f * f);
                            skin::button (g, box, on, false, accent, radius::md * f);
                            g.setColour (on ? colour::onAccent : colour::textSecondary);
                            g.setFont (font (juce::jmax (8.0f, 11.0f * f), true));
                            g.drawText (item.label != nullptr ? item.label : "", box, juce::Justification::centred, false);
                        }
                        else
                        {
                            const auto dot = juce::Rectangle<float> (10.0f * f, 10.0f * f).withCentre ({ r.getX() + 6.0f * f, r.getCentreY() });
                            if (on)
                            {
                                g.setColour (accent.withAlpha (0.3f));
                                g.fillEllipse (dot.expanded (3.0f * f));
                            }
                            g.setColour (on ? accent : juce::Colour (0xff0b0c0f));
                            g.fillEllipse (dot);
                            g.setColour (juce::Colours::white.withAlpha (on ? 0.4f : 0.08f));
                            g.drawEllipse (dot.reduced (0.5f), 1.0f);
                            caption (item.label, r.withTrimmedLeft (14.0f * f), false, juce::Justification::centredLeft);
                        }
                    }
                    break;
                case plugui::Item::stepper:
                    if (k != nullptr)
                    {
                        auto box = r;
                        if (item.label != nullptr)
                            caption (item.label, box.removeFromTop (12.0f * f), false, juce::Justification::centred);
                        skin::valuePlate (g, box, radius::sm * f);
                        g.setColour (colour::textPrimary);
                        g.setFont (font (juce::jmax (9.0f, 12.0f * f), true));
                        // oversampling orders read as FL shows them (0 → 1x … 3 → 8x)
                        // a Transistor Bass step's note reads as a note name (60 = C5, FL's naming; round 168q2)
                        static const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                        const auto whole = (int) std::lround (k->getValue());
                        const auto* units = unitParam (*k); // (round 169h: a 0…1 number box in whole units)
                        const auto text = units != nullptr ? juce::String (units->unitString (units->unitOf ((float) k->getValue())))
                                        : juce::String (item.param).containsIgnoreCase ("oversampling") ? juce::String (1 << juce::jlimit (0, 8, (int) k->getValue())) + "x"
                                        : juce::String (item.param).endsWith (" note") ? juce::String (noteNames[((whole % 12) + 12) % 12]) + juce::String (whole / 12)
                                                                                       : juce::String (whole);
                        g.drawFittedText (text, box.withTrimmedRight (8.0f * f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
                        g.setColour (colour::textSecondary);
                        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb4"), box.withTrimmedLeft (box.getWidth() - 10.0f * f).withHeight (box.getHeight() * 0.5f), juce::Justification::centred, false);
                        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe"), box.withTrimmedLeft (box.getWidth() - 10.0f * f).withTrimmedTop (box.getHeight() * 0.5f), juce::Justification::centred, false);
                    }
                    break;
                case plugui::Item::display:
                    skin::well (g, r, radius::md * f);
                    // round 168q: BeepMap's bitmap, its file row, the Granulizer's sample and grains, Transistor Bass's
                    // program and sequence
                    if (juce::String (item.display) == "beepmapimage")
                        paintBeepMapImage (g, r.reduced (3.0f * f));
                    if (juce::String (item.display) == "beepmapfile")
                        paintBeepMapFile (g, r, f);
                    if (juce::String (item.display) == "granulizerwave")
                        paintGranulizerWave (g, r.reduced (4.0f * f), f);
                    if (juce::String (item.display) == "granulizergrains")
                        paintGranulizerGrains (g, r.reduced (5.0f * f), f);
                    if (juce::String (item.display) == "tbprogram")
                        paintTransistorBass (g, r, f);
                    if (juce::String (item.display) == "tbsteps")
                        paintTransistorBassSteps (g, r, f);
                    if (juce::String (item.display) == "tbseqtools")
                        paintTransistorBassTools (g, r, f);
                    if (juce::String (item.display) == "xy")
                        if (auto* kx = knobFor (item.param); kx != nullptr)
                            if (auto* ky = knobFor (item.param2); ky != nullptr)
                            {
                                // FL's Ogun pad (TQuickXYZ, round 169h shots fl-ogun-*.png): a 4 × 4 grid and a small ring
                                // whose centre is the value, drawn clipped to the pad — at X = 0 only its inner half shows
                                const juce::Graphics::ScopedSaveState clip (g);
                                g.reduceClipRegion (r.toNearestInt());
                                for (int k = 1; k < 4; ++k)
                                {
                                    g.setColour (k == 2 ? colour::gridBar : colour::gridBeat);
                                    g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / 4.0f), r.getY(), r.getBottom());
                                    g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) k / 4.0f), r.getX(), r.getRight());
                                }
                                const juce::Point<float> pt { r.getX() + r.getWidth() * normalised (*kx), r.getBottom() - r.getHeight() * normalised (*ky) };
                                g.setColour (accent);
                                g.drawEllipse (juce::Rectangle<float> (10.0f * f, 10.0f * f).withCentre (pt), juce::jmax (1.5f, 1.5f * f));
                            }
                    if (juce::String (item.display) == "toxicwave" && item.param != nullptr) // round 168n: the wave's cycle
                    {
                        const juce::String name (item.param);
                        const auto n = name.fromFirstOccurrenceOf (" ", false, false).getIntValue();
                        const auto slot = name.startsWith ("LFO") ? 5 + n : n - 1;
                        const auto inner = r.reduced (5.0f * f, 6.0f * f);
                        const auto shape = toxicWaveShape (def.pluginState, def.samplePath, slot, 96);
                        if (shape.empty())
                        {
                            g.setColour (colour::textSecondary);
                            g.setFont (font (juce::jmax (8.0f, 9.5f * f)));
                            g.drawFittedText (tr ("Needs FL Studio's files"), inner.toNearestInt(), juce::Justification::centred, 2, 0.7f);
                        }
                        else
                        {
                            g.setColour (colour::gridBeat);
                            g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
                            juce::Path wave;
                            for (int i = 0; i < (int) shape.size(); ++i)
                            {
                                const juce::Point<float> pt { inner.getX() + inner.getWidth() * (float) i / (float) (shape.size() - 1),
                                                              inner.getCentreY() - inner.getHeight() * 0.5f * shape[(std::size_t) i] };
                                if (i == 0)
                                    wave.startNewSubPath (pt);
                                else
                                    wave.lineTo (pt);
                            }
                            g.setColour (accent.withAlpha (0.25f));
                            g.strokePath (wave, juce::PathStrokeType (3.0f * f));
                            g.setColour (accent.brighter (0.3f));
                            g.strokePath (wave, juce::PathStrokeType (1.3f * f));
                        }
                    }
                    paintDrumDisplay (g, item, r, def, f);
                    // round 168o: the envelopes of the synth group's pages, drawn from their knobs (attack, decay, sustain, release;
                    // a missing stage is skipped, a missing sustain is the level the decay falls to: none)
                    {
                        struct Env
                        {
                            const char* id;
                            const char* a;
                            const char* d;
                            const char* s;
                            const char* r;
                            const char* start; // a modulator's initial level (DX10)
                        };
                        static const Env envs[] = {
                            { "dx10amp", "Attack", "Decay", nullptr, "Release", nullptr },
                            { "dx10mod1", nullptr, "Mod 1 time", "Mod 1 sustain", "Mod 1 release", "Mod 1 init" },
                            { "dx10mod2", nullptr, "Mod 2 time", "Mod 2 sustain", "Mod 2 release", "Mod 2 init" },
                            { "simamp", "Attack", "Decay", "Sustain", "Release", nullptr },
                            { "simfilt", "Filter attack", "Filter decay", "Filter sustain", "Filter release", nullptr },
                            { "ministenv", "Attack", "Decay", "Sustain", "Release", nullptr },
                        };
                        for (const auto& e : envs)
                            if (juce::String (item.display) == e.id)
                            {
                                const auto value = [&] (const char* name, float otherwise) {
                                    auto* kn = name != nullptr ? knobFor (name) : nullptr;
                                    return kn != nullptr ? juce::jlimit (0.0f, 1.0f, normalised (*kn)) : otherwise;
                                };
                                const auto inner = r.reduced (5.0f * f, 5.0f * f);
                                const auto a = value (e.a, 0.0f), d = value (e.d, 0.5f), su = value (e.s, 0.0f), rl = value (e.r, 0.3f);
                                const auto peak = e.start != nullptr ? value (e.start, 1.0f) : 1.0f;
                                const auto unit = inner.getWidth() / 4.0f;
                                const auto y = [&] (float level) { return inner.getBottom() - inner.getHeight() * level; };
                                juce::Path env;
                                auto x = inner.getX();
                                env.startNewSubPath (x, y (e.start != nullptr ? peak : 0.0f));
                                x += unit * a;
                                env.lineTo (x, y (peak));
                                // DX10's amp decay ≥ 0.98 holds the note
                                const auto hold = e.s != nullptr ? su : (d >= 0.98f ? peak : 0.0f);
                                x += unit * (0.15f + 0.85f * d);
                                env.lineTo (x, y (hold));
                                env.lineTo (inner.getRight() - unit * (0.15f + 0.85f * rl), y (hold));
                                env.lineTo (inner.getRight(), y (0.0f));
                                g.setColour (colour::gridBeat);
                                g.drawHorizontalLine ((int) inner.getBottom(), inner.getX(), inner.getRight());
                                g.setColour (accent.withAlpha (0.25f));
                                g.strokePath (env, juce::PathStrokeType (3.0f * f));
                                g.setColour (accent.brighter (0.3f));
                                g.strokePath (env, juce::PathStrokeType (1.3f * f));
                            }
                    }
                    if (juce::String (item.display) == "gmswaves") // round 168o: GMS's waves are read from FL Studio's folder
                    {
                        g.setColour (colour::textSecondary);
                        g.setFont (font (juce::jmax (8.0f, 9.5f * f)));
                        const auto text = def.samplePath.empty() ? tr ("Needs FL Studio's files")
                                                                 : juce::File (juce::String::fromUTF8 (def.samplePath.c_str())).getFileName();
                        g.drawFittedText (text, r.reduced (6.0f * f).toNearestInt(), juce::Justification::centred, 3, 0.7f);
                    }
                    if (juce::String (item.display) == "flkeysname")
                        if (const auto st = flkeys::parseState (def.pluginState))
                        {
                            g.setColour (colour::textPrimary);
                            g.setFont (font (juce::jmax (9.0f, 11.0f * f)));
                            g.drawFittedText (juce::String::fromUTF8 (st->wavetable.c_str()), r.reduced (6.0f * f, 0.0f).toNearestInt(),
                                              juce::Justification::centredLeft, 1, 0.8f);
                        }
                    break;
                default:
                    break;
            }
        }
    }

    // round 168p: the displays of the drum pages (Drumaxx's kit / pads, BassDrum's noise sample, DrumSynth Live's
    // selected patch and its key display)
    static juce::String keyName (int key) { return juce::String (notenames::keyName (key)); }

    juce::Rectangle<float> dslKeyCell (juce::Rectangle<float> r, int key) const
    {
        // the 120 patch keys in 4 rows of 30 (C0 … B9, FL's octave numbers)
        const auto w = r.getWidth() / 30.0f, h = r.getHeight() / 4.0f;
        return { r.getX() + w * (float) (key % 30), r.getY() + h * (float) (key / 30), w, h };
    }

    void paintDrumDisplay (juce::Graphics& g, const plugui::LayoutItem& item, juce::Rectangle<float> r, const ChannelDef& def, float f)
    {
        using namespace theme;
        const juce::String id (item.display);
        auto text = [&] (const juce::String& t, juce::Colour c, juce::Justification j = juce::Justification::centred) {
            g.setColour (c);
            g.setFont (font (juce::jmax (8.0f, juce::jmin (12.0f * f, r.getHeight() * 0.62f))));
            g.drawFittedText (t, r.reduced (4.0f * f, 1.0f).toNearestInt(), j, 1, 0.7f);
        };
        if (id == "drumaxxkit")
        {
            const auto name = juce::String::fromUTF8 (drumaxxKitName (def.pluginState).c_str());
            text (name.isEmpty() ? tr ("(no kit name)") : name, colour::textPrimary, juce::Justification::centredLeft);
        }
        else if (id == "drumaxxpad")
        {
            const auto pad = item.steps;
            const auto keyValue = drumaxxParam (def.pluginState, 44 * pad + 4);
            const auto key = std::isfinite (keyValue) ? (int) keyValue : -1;
            skin::button (g, r.reduced (2.0f * f), pad == drumPart, false, accent, radius::md * f);
            g.setColour (pad == drumPart ? colour::onAccent : colour::textSecondary);
            g.setFont (font (juce::jmax (8.0f, 10.0f * f), true));
            g.drawFittedText (juce::String (pad + 1) + "\n" + (key >= 0 && key < 128 ? keyName (key) : juce::String()), r.toNearestInt(),
                              juce::Justification::centred, 2, 0.7f);
        }
        else if (id == "drumaxxpadname")
        {
            const auto name = juce::String::fromUTF8 (drumaxxPadName (def.pluginState, item.steps).c_str());
            text (name.isEmpty() ? tr ("Pad {0}", { juce::String (item.steps + 1) }) : name, item.steps == drumPart ? accent.brighter (0.4f) : colour::textSecondary,
                  juce::Justification::centredLeft);
        }
        else if (id == "bassdrumnoise")
        {
            if (! bassDrumStateUsable (def.pluginState) || def.samplePath.empty())
                text (tr ("Needs FL Studio's files"), colour::textSecondary);
            else
            {
                const auto name = juce::String::fromUTF8 (bassDrumNoiseName (def.pluginState).c_str());
                text (name.isEmpty() ? tr ("(no noise sample)") : name, colour::textPrimary, juce::Justification::centredLeft);
            }
        }
        else if (id == "dslpatch")
            text (keyName (drumPart) + "  (" + juce::String (drumPart) + ")", colour::textPrimary);
        else if (id == "dslsend" || id == "dslmorph")
        {
            const auto slot = drumSynthKeyToSlot (drumPart);
            const auto at = drumSynthPatchParams * slot + (id == "dslsend" ? 34 : 36);
            const auto v = slot >= 0 && at < (int) def.modelParams.size() ? (int) std::lround (def.modelParams[(std::size_t) at]) : -1;
            text (id == "dslsend" ? (v < 0 ? juce::String ("-") : juce::String (v)) : (v < 0 || v > 119 ? tr ("(none)") : keyName (v)), colour::textPrimary);
        }
        else if (id == "dslkeys")
        {
            const auto inner = r.reduced (3.0f * f);
            for (int key = 0; key < drumSynthPatches; ++key)
            {
                const auto cell = dslKeyCell (inner, key).reduced (0.5f * f);
                const auto slot = drumSynthKeyToSlot (key);
                const auto level = (std::size_t) (drumSynthPatchParams * slot) < def.modelParams.size() ? def.modelParams[(std::size_t) (drumSynthPatchParams * slot)] : 0.0f;
                static constexpr bool blackKey[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
                const auto black = blackKey[key % 12];
                g.setColour (key == drumPart ? accent : level > 0.0f ? accent.withAlpha (0.35f) : black ? colour::bg1 : colour::bg4);
                g.fillRect (cell);
                if (key % 12 == 0)
                {
                    g.setColour (key == drumPart ? colour::onAccent : colour::textSecondary);
                    g.setFont (font (juce::jmax (6.5f, juce::jmin (8.0f * f, cell.getHeight() * 0.6f))));
                    g.drawText (juce::String (key / 12), cell, juce::Justification::centred, false);
                }
            }
        }
    }

    // a click on a pad button / the key display selects the part the knobs edit; true when handled
    bool clickDrumDisplay (const plugui::LayoutItem& item, juce::Point<float> at)
    {
        if (item.type != plugui::Item::display || item.display == nullptr || ! layoutRect (item).contains (at) || ! conditionHolds (item))
            return false;
        const juce::String id (item.display);
        if (drumaxx && (id == "drumaxxpad" || id == "drumaxxpadname"))
        {
            selectPart (item.steps);
            return true;
        }
        if (drumsynth && id == "dslkeys")
        {
            const auto inner = layoutRect (item).reduced (3.0f * layoutFactor());
            for (int key = 0; key < drumSynthPatches; ++key)
                if (dslKeyCell (inner, key).contains (at))
                {
                    selectPart (key);
                    return true;
                }
        }
        return false;
    }

    void resizeLayout()
    {
        for (auto& k : knobs)
        {
            const auto* item = itemFor (k->getName());
            if (item == nullptr) // (hidden knobs too: another page's knobs keep their place for when the page shows)
                continue;
            auto r = layoutRect (*item);
            if ((item->type == plugui::Item::vslider || item->type == plugui::Item::hslider) && (item->style & plugui::noCaption) == 0 && item->label != nullptr)
                r.removeFromBottom (13.0f * layoutFactor());
            if (item->type == plugui::Item::knob)
            {
                if ((item->style & plugui::noCaption) == 0)
                    r.removeFromBottom (14.0f * layoutFactor());
                const auto d = juce::jmin (r.getWidth(), r.getHeight());
                r = r.withSizeKeepingCentre (d, d).withY (r.getY());
            }
            k->setBounds (r.toNearestInt());
        }
    }

    // a stepper's number moves one unit (the engine's own units for a 0…1 number box, round 169h)
    const EngineParam* unitParam (const juce::Slider& k) const
    {
        const auto* p = modelparams::findByName (instrumentKind, k.getName().toStdString());
        return p != nullptr && p->hasUnits() ? p : nullptr;
    }
    void stepBy (juce::Slider& k, int delta)
    {
        if (const auto* p = unitParam (k))
            k.setValue (p->rawOfUnit (p->unitOf ((float) k.getValue()) + delta), juce::sendNotificationSync);
        else
            k.setValue (k.getValue() + delta, juce::sendNotificationSync);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (layout != nullptr && wheel.deltaY != 0.0f)
            for (const auto& item : layout->items)
                if (item.type == plugui::Item::stepper && layoutRect (item).contains (e.position) && conditionHolds (item))
                    if (auto* k = knobFor (item.param))
                    {
                        stepBy (*k, wheel.deltaY > 0.0f ? 1 : -1);
                        repaint();
                        return;
                    }
        juce::Component::mouseWheelMove (e, wheel);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (stepperDrag != nullptr)
        {
            if (auto* k = knobFor (stepperDrag->param))
            {
                const auto pixels = 6.0f * layoutFactor();
                const auto units = (int) ((stepperDragY - e.position.y) / pixels);
                if (units != stepperDragged)
                {
                    stepBy (*k, units - stepperDragged);
                    stepperDragged = units;
                    repaint();
                }
            }
            return;
        }
        if (xyItem == nullptr)
            return;
        const auto r = layoutRect (*xyItem);
        if (auto* kx = knobFor (xyItem->param))
            setNormalised (*kx, juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()));
        if (auto* ky = knobFor (xyItem->param2))
            setNormalised (*ky, juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight()));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        xyItem = nullptr;
        stepperDrag = nullptr;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (layout == nullptr)
            return;
        for (const auto& item : layout->items) // round 168p: Drumaxx's pads, DrumSynth Live's keys
            if (clickDrumDisplay (item, e.position))
                return;
        for (const auto& item : layout->items) // the page switches first (they edit no parameter)
            if (item.type == plugui::Item::tabs && (item.style & plugui::pageTabs) != 0 && layoutRect (item).contains (e.position) && conditionHolds (item))
            {
                const auto count = (int) plugui::optionLabels (item).size();
                for (int o = 0; o < count; ++o)
                    if (tabCell (item, o, count).contains (e.position))
                        showPage (o);
                return;
            }
        for (const auto& item : layout->items) // round 168q2: Transistor Bass's CLEAR / octave up / octave down
            if (item.type == plugui::Item::display && juce::String (item.display) == "tbseqtools" && layoutRect (item).contains (e.position))
            {
                if (const auto button = transistorBassToolButton (item, e.position); button >= 0)
                    transistorBassToolAction (button);
                return;
            }
        for (const auto& item : layout->items) // round 168q: BeepMap's Open / Paste
            if (item.type == plugui::Item::display && juce::String (item.display) == "beepmapfile" && layoutRect (item).contains (e.position))
            {
                if (const auto button = beepMapFileButton (item, e.position); button >= 0)
                    beepMapAction (button);
                return;
            }
        for (const auto& item : layout->items)
        {
            auto* k = knobFor (item.param);
            if (k == nullptr || ! layoutRect (item).contains (e.position) || ! conditionHolds (item)
                || item.type == plugui::Item::knob || item.type == plugui::Item::vslider || item.type == plugui::Item::hslider
                || item.type == plugui::Item::section || item.type == plugui::Item::label)
                continue;
            if (e.mods.isPopupMenu() && ref.index >= 0 && modelTable != nullptr) // a switch / tabs: FL's parameter menu
            {
                if (const auto* p = modelparams::findByName (instrumentKind, k->getName().toStdString()))
                    ui::showSliderMenu (*k, &engine, AutomationTarget { AutomationTargetKind::channelParam, ref.index, 0, p->index });
                return;
            }
            if (item.type == plugui::Item::menu)
            {
                const auto labels = plugui::optionLabels (item);
                const auto chosen = plugui::optionAt (item, normalised (*k));
                juce::PopupMenu menu;
                for (int o = 0; o < (int) labels.size(); ++o)
                    menu.addItem (tr (labels[(std::size_t) o]), true, o == chosen,
                                  [safe = juce::Component::SafePointer<ModelPanel> (this), name = k->getName(), value = plugui::optionValue (item, o)] {
                                      if (safe == nullptr)
                                          return;
                                      if (auto* knob = safe->knobFor (name.toRawUTF8()))
                                          setNormalised (*knob, value);
                                      safe->repaint();
                                  });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (layoutRect (item).toNearestInt())));
                return;
            }
            if (item.type == plugui::Item::tabs)
            {
                const auto count = (int) plugui::optionLabels (item).size();
                for (int o = 0; o < count; ++o)
                    if (tabCell (item, o, count).contains (e.position))
                        setNormalised (*k, plugui::optionValue (item, o));
                repaint();
                return;
            }
            if (item.type == plugui::Item::display && juce::String (item.display) == "xy")
            {
                xyItem = &item;
                mouseDrag (e);
                return;
            }
            if (item.type == plugui::Item::stepper)
            {
                const auto r = layoutRect (item);
                const auto up = e.position.y < r.getY() + (item.label != nullptr ? 12.0f * layoutFactor() : 0.0f) + (r.getHeight() - (item.label != nullptr ? 12.0f * layoutFactor() : 0.0f)) * 0.5f;
                stepBy (*k, up ? 1 : -1);
                stepperDrag = &item; // (round 169h) a vertical drag moves the number too
                stepperDragY = e.position.y;
                stepperDragged = 0;
            }
            else if (item.type == plugui::Item::toggle)
                setNormalised (*k, normalised (*k) >= 0.5f ? 0.0f : 1.0f);
            else if (item.type == plugui::Item::radio)
            {
                const auto count = (int) plugui::optionLabels (item).size();
                for (int o = 0; o < count; ++o)
                    if (optionCell (item, o, count).contains (e.position))
                        setNormalised (*k, plugui::optionValue (item, o));
            }
            repaint();
            return;
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        if (layout != nullptr) // a plugin page in FL's arrangement: the plugin skin's canvas (round 170j: the art's when Themed)
        {
            const skin::StyleScope styleScope (themedNow() ? skin::Style::themed : skin::style(), pageArt);
            skin::canvas (g, getLocalBounds().toFloat(), pageAccent());
        }
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (layout != nullptr ? theme::type::body : theme::type::title));
        if (! engineFilesMissing)
            g.drawFittedText (shownDescription(), descriptionArea(), juce::Justification::centredLeft, layout != nullptr ? 1 : 2, 0.8f);
        if (engineFilesMissing) // round 168q: an engine that runs FL's own DLL / data file, which this computer lacks
        {
            const auto band = juce::Rectangle<float> (8.0f, 5.0f, (float) getWidth() - 16.0f, 22.0f);
            g.setColour (accent.withAlpha (0.18f));
            g.fillRoundedRectangle (band, 4.0f);
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (tr ("Needs FL Studio's files: the engine file was not found, so the channel stays silent (its settings are kept)."),
                              band.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
        if (layout != nullptr)
        {
            paintLayout (g);
            return;
        }
        g.setFont (theme::font (theme::type::small));
        for (const auto& k : knobs)
            g.drawText (k->getName(), k->getBounds().withHeight (14).translated (0, -14), juce::Justification::centred);
    }

    // the engine's description (what it is and where its data comes from)
    juce::String description() const
    {
        if (plugui::hostedElsewhere)
            return juce::String::fromUTF8 (hostedName.c_str());
        return drumsynth ? tr ("DrumSynth Live — FL's engine, patch on key {0}", { juce::String (editorKey) })
                               + (wavesFound ? tr (" (next hit; recorded waves from your FL installation)")
                                             : tr (" (next hit; recorded waves not found: silent)"))
               : harmless ? tr ("Harmless — FL's additive engine with its effects rack")
               : harmor ? tr ("Harmor — FL's additive engine with its effects rack")
               : gms ? tr ("GMS — FL's engine (waves read from your FL installation)")
               : morphine ? tr ("Morphine — FL's additive engine (new settings restart the voices)")
               : transistorBass && transistorBassMobile ? tr ("Transistor Bass — FL Studio Mobile's build of the engine")
               : transistorBass ? tr ("Transistor Bass — FL's engine with its sequencer and effects (oscillator cycles read from your FL installation)")
               : autogun ? tr ("Autogun — FL's engine with its effects and Magic Dust (SG.dat read from your FL installation)")
               : ogun ? tr ("Ogun — FL's engine with its effects (Seed 2 Random is a fixed draw per channel)")
               : poizone ? tr ("PoiZone — FL's engine with its gate, chorus, delay and arpeggiator (new settings restart the voices)")
               : miniSynth ? tr ("MiniSynth — FL's engine with its effects (wave tables read from your FL installation)")
               : dx10 ? tr ("Fruity DX10 — FL's FM engine")
               : kepler ? tr ("Kepler — FL's engine (LFO shape and noise read from your FL installation)")
               : keplerExo ? tr ("Kepler Exo — FL's engine with its effects (tables read from your FL installation)")
               : fruitkick ? tr ("Fruit Kick — FL's engine (channel envelopes stepped per tick)")
               : bassdrum ? tr ("BassDrum — FL's engine (table and noise samples read from your FL installation)")
               : toxic ? tr ("Toxic Biohazard — FL's engine with its effects (waves read from your FL installation)")
               : threeOsc ? tr ("3x Osc — FL's engine (custom wave from the channel's sample)")
               : simSynth ? tr ("SimSynth — FL's engine (new settings restart the voices)")
               : wasp ? tr ("Wasp — FL Studio 2026 has no 64-bit Wasp: the channel keeps its settings and stays silent, as in FL")
               : flKeys ? tr ("FL Keys — FL's engine (wavetable read from your FL installation)")
               : plucked ? tr ("Plucked! — FL's plucked-string engine")
               : beepMap ? tr ("BeepMap — FL's engine (plays the bitmap saved with the channel)")
               : sawer ? tr ("Sawer — FL's engine with its effects (oscillator cycle read from your FL installation)")
               : granulizer ? tr ("Fruity Granulizer — FL's engine on the channel's sample")
               : drumaxx ? tr ("Drumaxx — FL's physical drum engine (pads on their keys; patterns not played)")
               : slayer ? tr ("FL Slayer — FL's engine with its effects, amp and cabinet (picks read from your FL installation; new settings restart the voices)")
               : sytrus ? tr ("Sytrus — FL's engine, main settings (operators, filters and effects keep the saved patch)")
               : pocketSuperSaw ? tr ("Pocket SuperSaw — LYNX's engine for FL Studio Mobile's SuperSaw")
               : fruityDance ? tr ("Fruity Dance — a visual plugin: it makes no sound, the channel keeps its settings")
               : boobass ? tr ("BooBass — FL's engine, its sound read from your FL installation (knobs: 3-band EQ)")
               : sakura ? tr ("Sakura — physical strings (FL's engine; new parameters apply to the next note)")
                        : tr ("Drumpad — physical drum (FL's engine; new parameters apply to the next hit)");
    }

    // round 172a: a designed page shows only the plugin and what it is; the notes in brackets (data files, what restarts
    // the voices...) are the header's tooltip instead of text on the page
    juce::String shownDescription() const
    {
        const auto full = description();
        if (layout == nullptr)
            return full;
        auto at = full.indexOfChar ('(');
        if (const auto wide = full.indexOfChar ((juce::juce_wchar) 0xff08); wide > 0 && (at < 0 || wide < at)) // a full-width bracket
            at = wide;
        return at > 0 ? full.substring (0, at).trimEnd() : full;
    }

    juce::String getTooltip() override
    {
        const auto full = description();
        if (! engineFilesMissing && descriptionArea().contains (getMouseXYRelative()) && shownDescription() != full)
            return full;
        return {};
    }

    // the engine's one-line description at the top (round 169h: wrapped to a second line above the knob grid, or shrunk a
    // little on a designed page, instead of cut with "…" in a narrow window); clear of the sample button
    juce::Rectangle<int> descriptionArea() const
    {
        const auto right = sampleButton.isVisible() ? getWidth() - 136 : getWidth() - 10;
        return { 10, layout != nullptr ? 6 : 2, juce::jmax (40, right - 10), layout != nullptr ? 20 : 30 }; // (the knob grid's captions start at y 32)
    }

    void resized() override
    {
        if (sampleButton.isVisible())
            sampleButton.setBounds (getWidth() - 128, 4, 120, 24);
        if (layout != nullptr)
        {
            resizeLayout();
            if (artLayer != nullptr)
            {
                artLayer->setBounds (getLocalBounds());
                artLayer->setStage (artStage());
                artLayer->toFront (false);
            }
            return;
        }
        constexpr int columns = 7, w = 96, h = 92;
        for (std::size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (12 + (int) (i % columns) * (w + 4), 46 + (int) (i / columns) * (h + 18), w, h);
        if (granulizerModes)
        {
            // FL's KEY TO, TRANSIENTS and TEMPO LOCKED controls, in a row under the knobs
            const auto rows = ((int) knobs.size() + columns - 1) / columns;
            const auto y = 46 + rows * (h + 18) + 4;
            granKeyToLabel.setBounds (12, y, 150, 18);
            granKeyTo.setBounds (12, y + 20, 150, 24);
            granTransientsLabel.setBounds (174, y, 150, 18);
            granTransients.setBounds (174, y + 20, 150, 24);
            granTempoLocked.setBounds (336, y + 20, 140, 24);
            granBeatsLabel.setBounds (488, y, 200, 18);
            granBeats.setBounds (488, y + 20, 200, 24);
        }
    }

    // FL's Granulizer takes a sample dropped on its window (from the Browser or the system) and plays it; before, the
    // drop fell through to the Playlist under the window and made an audio clip (audit 165)
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return takesSample() && BrowserComponent::isBrowserDrag (details) && SampleLibrary::isSampleFile (BrowserComponent::draggedFile (details));
    }
    void itemDragEnter (const SourceDetails&) override { setDropHighlight (true); }
    void itemDragExit (const SourceDetails&) override { setDropHighlight (false); }
    void itemDropped (const SourceDetails& details) override
    {
        setDropHighlight (false);
        loadGranulizerSample (BrowserComponent::draggedFile (details));
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return takesSample() && files.size() == 1 && SampleLibrary::isSampleFile (juce::File (files[0]));
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { setDropHighlight (true); }
    void fileDragExit (const juce::StringArray&) override { setDropHighlight (false); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        setDropHighlight (false);
        loadGranulizerSample (juce::File (files[0]));
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropHighlight)
        {
            g.setColour (theme::colour::accent);
            g.drawRect (getLocalBounds(), 2);
        }
    }

private:
    bool takesSample() const
    {
        const auto* c = ref.get (engine);
        return c != nullptr && projectcontents::pluginWindowTakesSample (c->instrument);
    }
    void setDropHighlight (bool on)
    {
        if (dropHighlight != on)
        {
            dropHighlight = on;
            repaint();
        }
    }

    // the Granulizer plays the new sample with the same settings (FL keeps the knobs and the channel's name)
    // --- round 168q: the displays of the group-D pages ---------------------------------------------------------

    const ChannelDef* channelNow() const { return ref.get (engine); }

    // BeepMap: the bitmap, stretched to the display (red = left, green = right, as the engine reads it)
    void paintBeepMapImage (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto* c = channelNow();
        const auto pic = c != nullptr ? beepMapPicture (c->pluginState) : std::nullopt;
        if (! pic || pic->width <= 0 || pic->height <= 0)
            return;
        if (beepMapCacheState != c->pluginState)
        {
            beepMapCacheState = c->pluginState;
            beepMapCache = juce::Image (juce::Image::RGB, pic->width, pic->height, false);
            for (int y = 0; y < pic->height; ++y)
                for (int x = 0; x < pic->width; ++x)
                {
                    const auto* px = pic->bgr.data() + 3 * ((std::size_t) y * (std::size_t) pic->width + (std::size_t) x);
                    beepMapCache.setPixelAt (x, y, juce::Colour (px[2], px[1], px[0]));
                }
        }
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (beepMapCache, r, juce::RectanglePlacement::stretchToFit);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    }

    void paintBeepMapFile (juce::Graphics& g, juce::Rectangle<float> r, float f)
    {
        using namespace theme;
        const auto* c = channelNow();
        const auto pic = c != nullptr ? beepMapPicture (c->pluginState) : std::nullopt;
        auto row = r.reduced (4.0f * f);
        const auto w = row.getWidth() / 3.2f;
        const juce::String caps[] = { tr ("Open..."), tr ("Paste") };
        for (int i = 0; i < 2; ++i)
        {
            const auto cell = row.removeFromLeft (w).reduced (2.0f * f, 0.0f);
            skin::button (g, cell, false, false, accent, radius::sm * f);
            g.setColour (colour::textPrimary);
            g.setFont (font (juce::jmax (8.0f, juce::jmin (11.0f * f, cell.getHeight() * 0.5f)), true));
            g.drawFittedText (caps[i], cell.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
        g.setColour (colour::textSecondary);
        g.setFont (font (juce::jmax (8.0f, 10.0f * f)));
        const auto text = pic ? tr ("{0} × {1} pixels", { juce::String (pic->width), juce::String (pic->height) }) : tr ("No bitmap");
        g.drawFittedText (text, row.reduced (4.0f * f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.7f);
    }

    // which button of the BeepMap file row is under `p` (0 open, 1 paste, −1 none)
    int beepMapFileButton (const plugui::LayoutItem& item, juce::Point<float> p) const
    {
        auto row = layoutRect (item).reduced (4.0f * layoutFactor());
        const auto w = row.getWidth() / 3.2f;
        for (int i = 0; i < 2; ++i)
            if (row.removeFromLeft (w).contains (p))
                return i;
        return -1;
    }

    void setBeepMapBitmap (const std::vector<std::uint8_t>& bmp)
    {
        const auto* c = channelNow();
        if (c == nullptr)
            return;
        auto next = beepMapWithBitmap (c->pluginState, bmp);
        if (next.empty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, juce::String ("BeepMap"),
                                                    tr ("BeepMap reads uncompressed 24- or 32-bit BMP pictures at least 3 pixels high."));
            return;
        }
        ref.edit (engine, [next = std::move (next)] (ChannelDef& d) { d.pluginState = next; });
        repaint();
    }

    void beepMapAction (int button)
    {
        if (button == 1) // a BMP file path or the file itself copied in the clipboard
        {
            const juce::File file (juce::SystemClipboard::getTextFromClipboard().trim().unquoted());
            juce::MemoryBlock data;
            if (file.existsAsFile() && file.loadFileAsData (data))
                setBeepMapBitmap (std::vector<std::uint8_t> ((const std::uint8_t*) data.getData(), (const std::uint8_t*) data.getData() + data.getSize()));
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, juce::String ("BeepMap"), tr ("Copy a .bmp file's path first, or use Open."));
            return;
        }
        chooser = lynx::ui::makeFileChooser (tr ("Open a bitmap"), juce::File(), "*.bmp");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<ModelPanel> (this)] (const juce::FileChooser& fc) {
                                  juce::MemoryBlock data;
                                  if (safe == nullptr || ! fc.getResult().loadFileAsData (data))
                                      return;
                                  safe->setBeepMapBitmap (std::vector<std::uint8_t> ((const std::uint8_t*) data.getData(),
                                                                                      (const std::uint8_t*) data.getData() + data.getSize()));
                              });
    }

    // Granulizer: the channel's sample (or how to give it one) with the start point
    void paintGranulizerWave (juce::Graphics& g, juce::Rectangle<float> r, float f)
    {
        using namespace theme;
        const auto* c = channelNow();
        const auto path = c != nullptr ? c->samplePath : std::string();
        if (path.empty())
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (juce::jmax (9.0f, 11.0f * f)));
            g.drawFittedText (tr ("Drop a sample here (or on the channel button)"), r.toNearestInt(), juce::Justification::centred, 2, 0.7f);
            return;
        }
        if (path != granulizerPeaksPath)
        {
            granulizerPeaksPath = path;
            granulizerPeaks.clear();
            if (const auto s = engine.getSampleLibrary().load (juce::String::fromUTF8 (path.c_str())))
            {
                const auto n = (std::int64_t) s->left.size();
                constexpr int columns = 400;
                for (int i = 0; i < columns && n > 0; ++i)
                {
                    float peak = 0.0f;
                    for (auto k = n * i / columns; k < n * (i + 1) / columns; ++k)
                        peak = std::max ({ peak, std::fabs (s->left[(std::size_t) k]), s->right.empty() ? 0.0f : std::fabs (s->right[(std::size_t) k]) });
                    granulizerPeaks.push_back (std::min (peak, 1.0f));
                }
            }
        }
        if (granulizerPeaks.empty())
            return;
        const auto mid = r.getCentreY();
        juce::Path wave;
        for (int i = 0; i < (int) granulizerPeaks.size(); ++i)
        {
            const auto x = r.getX() + r.getWidth() * (float) i / (float) (granulizerPeaks.size() - 1);
            wave.addRectangle (x, mid - granulizerPeaks[(std::size_t) i] * r.getHeight() * 0.48f, juce::jmax (1.0f, r.getWidth() / (float) granulizerPeaks.size()),
                               juce::jmax (1.0f, granulizerPeaks[(std::size_t) i] * r.getHeight() * 0.96f));
        }
        g.setColour (accent.withAlpha (0.65f));
        g.fillPath (wave);
        if (const auto* k = knobFor ("Sample start"))
        {
            const auto x = r.getX() + r.getWidth() * normalised (*k);
            g.setColour (colour::textPrimary);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }
        g.setColour (colour::textSecondary);
        g.setFont (font (juce::jmax (8.0f, 9.5f * f)));
        g.drawFittedText (juce::File (juce::String::fromUTF8 (path.c_str())).getFileName(), r.removeFromTop (14.0f * f).toNearestInt(),
                          juce::Justification::topLeft, 1, 0.8f);
    }

    // Granulizer: one grain's envelope (attack, hold, release) and the next grain's start, from the knobs
    void paintGranulizerGrains (juce::Graphics& g, juce::Rectangle<float> r, float)
    {
        using namespace theme;
        const auto* att = knobFor ("Grain attack");
        const auto* hold = knobFor ("Grain hold");
        const auto* spacing = knobFor ("Grain spacing");
        if (att == nullptr || hold == nullptr || spacing == nullptr)
            return;
        const auto a = (float) att->getValue(), h = (float) hold->getValue(), sp = (float) spacing->getValue();
        const auto total = juce::jmax (1.0f, 2.0f * a + h + sp);
        const auto xOf = [&] (float t) { return r.getX() + r.getWidth() * t / total; };
        for (int grain = 0; grain < 2; ++grain)
        {
            const auto t0 = (float) grain * sp;
            juce::Path p;
            p.startNewSubPath (xOf (t0), r.getBottom());
            p.lineTo (xOf (t0 + a), r.getY());
            p.lineTo (xOf (t0 + a + h), r.getY());
            p.lineTo (xOf (t0 + 2.0f * a + h), r.getBottom());
            g.setColour ((grain == 0 ? accent : colour::textSecondary).withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }
    }

    // Transistor Bass: the current program and its sequence (16 steps: note, gate, accent, slide)
    void paintTransistorBass (juce::Graphics& g, juce::Rectangle<float> r, float f)
    {
        using namespace theme;
        const auto* c = channelNow();
        const auto prog = c != nullptr ? transistorBassProgram (c->pluginState) : std::nullopt;
        if (! prog)
            return;
        g.setColour (colour::textPrimary);
        g.setFont (font (juce::jmax (9.0f, 11.0f * f), true));
        const auto text = tr ("Program {0}", { juce::String (prog->program + 1) }) + "   "
                        + (prog->sequencer ? tr ("sequencer on") : tr ("sequencer off"))
                        + (prog->transpose != 0 ? "   " + tr ("transpose {0}", { juce::String (prog->transpose) }) : juce::String());
        g.drawFittedText (text, r.reduced (6.0f * f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }

    // the step numbers under the sequence, lit up to its length (round 168q2)
    void paintTransistorBassSteps (juce::Graphics& g, juce::Rectangle<float> r, float f)
    {
        using namespace theme;
        const auto* c = channelNow();
        const auto prog = c != nullptr ? transistorBassProgram (c->pluginState) : std::nullopt;
        const auto length = prog ? juce::jlimit (1, 16, prog->length) : 16;
        const auto w = r.getWidth() / 16.0f;
        g.setFont (font (juce::jmax (8.0f, 10.0f * f), true));
        for (int i = 0; i < 16; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.0f * f, 4.0f * f);
            const auto in = i < length;
            g.setColour (in ? accent.withAlpha (0.22f) : colour::bg3);
            g.fillRoundedRectangle (cell, radius::sm * f);
            g.setColour (in ? colour::textPrimary : colour::textDisabled);
            g.drawText (juce::String (i + 1), cell, juce::Justification::centred, false);
        }
    }

    static constexpr int transistorBassTools = 3;
    juce::Rectangle<float> transistorBassToolCell (juce::Rectangle<float> r, int i) const
    {
        const auto h = r.getHeight() / (float) transistorBassTools;
        return juce::Rectangle<float> (r.getX(), r.getY() + h * (float) i, r.getWidth(), h).reduced (2.0f * layoutFactor());
    }

    void paintTransistorBassTools (juce::Graphics& g, juce::Rectangle<float> r, float f)
    {
        using namespace theme;
        const juce::String caps[] = { tr ("Clear"), juce::String::fromUTF8 ("Oct +"), juce::String::fromUTF8 ("Oct \xe2\x88\x92") };
        for (int i = 0; i < transistorBassTools; ++i)
        {
            const auto cell = transistorBassToolCell (r, i);
            skin::button (g, cell, false, false, accent, radius::sm * f);
            g.setColour (colour::textPrimary);
            g.setFont (font (juce::jmax (8.0f, juce::jmin (10.0f * f, cell.getHeight() * 0.45f)), true));
            g.drawFittedText (caps[i], cell.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
    }

    int transistorBassToolButton (const plugui::LayoutItem& item, juce::Point<float> p) const
    {
        for (int i = 0; i < transistorBassTools; ++i)
            if (transistorBassToolCell (layoutRect (item), i).contains (p))
                return i;
        return -1;
    }

    // FL's CLEAR (gates, slides and accents off, notes C5) and its octave up / down buttons on the whole sequence
    void transistorBassToolAction (int button)
    {
        const auto* c = channelNow();
        if (c == nullptr)
            return;
        auto next = c->pluginState;
        if (! (button == 0 ? transistorBassClearSequence (next) : transistorBassShiftSequence (next, button == 1 ? 1 : -1)))
            return;
        ref.edit (engine, [next = std::move (next)] (ChannelDef& d) { d.pluginState = next; });
        syncKnobs();
        repaint();
    }

    juce::Image beepMapCache;
    std::vector<std::uint8_t> beepMapCacheState;
    std::string granulizerPeaksPath;
    std::vector<float> granulizerPeaks;

    void loadGranulizerSample (const juce::File& file)
    {
        if (! takesSample() || ! file.existsAsFile())
            return;
        if (engine.getSampleLibrary().load (file.getFullPathName()) == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Sample"), tr ("Could not read {0}", { file.getFileName() }));
            return;
        }
        ref.edit (engine, [path = file.getFullPathName().toStdString()] (ChannelDef& d) { d.samplePath = path; });
        if (sampleButton.isVisible())
            ui::setHint (sampleButton, file.getFullPathName());
        repaint();
    }
    bool dropHighlight = false;

    // Fruity Granulizer: what the keys move (Key to), where the slices come from (Transients), tempo lock and the beat
    // count it uses (REA_rodada165g-flgran165)
    void setupGranulizerModes (const ChannelDef& def)
    {
        const auto keyMode = granulizerMode (def.pluginState, 1), sliceMode = granulizerMode (def.pluginState, 0),
                   sync = granulizerMode (def.pluginState, 2);
        if (! keyMode || ! sliceMode || ! sync)
            return;
        granulizerModes = true;
        auto label = [this] (juce::Label& l, const juce::String& text) {
            l.setText (text, juce::dontSendNotification);
            l.setFont (theme::font (theme::type::small));
            addAndMakeVisible (l);
        };
        label (granKeyToLabel, tr ("Key to"));
        granKeyTo.addItem (tr ("Pitch"), 1);
        granKeyTo.addItem (tr ("Percent"), 2);
        granKeyTo.addItem (tr ("Step"), 3);
        granKeyTo.addItem (tr ("Transient"), 4);
        granKeyTo.setSelectedId (std::clamp (*keyMode, 0, 3) + 1, juce::dontSendNotification);
        granKeyTo.setTooltip (tr ("What the keys do: Pitch = transpose; Percent = start point (sample time, C5 to C7); "
                                  "Step = start point in 16th notes (song time); Transient = start at the slice of the key"));
        granKeyTo.onChange = [this] {
            const auto v = granKeyTo.getSelectedId() - 1;
            ref.edit (engine, [v] (ChannelDef& d) { granulizerSetMode (d.pluginState, 1, v); });
            if (v == 3 && granTransients.getSelectedId() == 1) // as FL: transient keys switch slice detection on
                granTransients.setSelectedId (3, juce::dontSendNotification);
        };
        addAndMakeVisible (granKeyTo);
        label (granTransientsLabel, tr ("Transients"));
        granTransients.addItem (tr ("Off"), 1);
        granTransients.addItem (tr ("Use regions"), 2);
        granTransients.addItem (tr ("Detect"), 3);
        granTransients.setSelectedId (std::clamp (*sliceMode, 0, 2) + 1, juce::dontSendNotification);
        granTransients.setTooltip (tr ("Transients source: the sample's cue points / regions, or FL's automatic slicing. "
                                       "Grains then line up with the slices and the Transient hold knob sets how long each "
                                       "slice's first grain holds"));
        granTransients.onChange = [this] {
            const auto v = granTransients.getSelectedId() - 1;
            ref.edit (engine, [v] (ChannelDef& d) { granulizerSetMode (d.pluginState, 0, v); });
        };
        addAndMakeVisible (granTransients);
        granTempoLocked.setButtonText (tr ("Tempo locked"));
        granTempoLocked.setToggleState (*sync != 0, juce::dontSendNotification);
        granTempoLocked.setTooltip (tr ("Match song tempo: the sample plays once per its length in beats (the WAV's ACID "
                                        "beats, else Beats below). Loop is always on and Wave spacing has no effect while "
                                        "locked"));
        granTempoLocked.onClick = [this] {
            const auto on = granTempoLocked.getToggleState();
            ref.edit (engine, [on] (ChannelDef& d) { granulizerSetMode (d.pluginState, 2, on ? 1 : 0); });
        };
        addAndMakeVisible (granTempoLocked);
        label (granBeatsLabel, tr ("Beats (0 = the sample's own)"));
        granBeats.setSliderStyle (juce::Slider::LinearHorizontal);
        granBeats.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        granBeats.setRange (0.0, 1024.0, 0.01);
        granBeats.setValue (def.sampler.stretchBeats, juce::dontSendNotification);
        granBeats.setTooltip (tr ("The sample's length in beats for Tempo locked and the Step keys, as FL's time-stretch "
                                  "TIME; a WAV with ACID beats uses its own"));
        granBeats.onValueChange = [this] {
            const auto v = (float) granBeats.getValue();
            ref.edit (engine, [v] (ChannelDef& d) { d.sampler.stretchBeats = v; });
        };
        addAndMakeVisible (granBeats);
    }

    bool granulizerModes = false;
    juce::ComboBox granKeyTo, granTransients;
    juce::ToggleButton granTempoLocked;
    juce::Slider granBeats;
    juce::Label granKeyToLabel, granTransientsLabel, granBeatsLabel;

    AudioEngine& engine;
    ChannelRef ref;
    juce::Colour accent { skin::colour::crimson }; // the plugin page's accent (ui/PluginSkin.h)
    skin::Style pageStyle = skin::Style::glow;     // the plugin page's automatic style (round 126)
    skin::Art pageArt = skin::Art::none;           // its own art (round 170j: Themed)
    std::unique_ptr<skin::ArtLayer> artLayer;      // the art's moving part (petals, a glowing trefoil...)
    bool sakura = true, boobass = false, drumsynth = false, harmless = false, harmor = false, gms = false, morphine = false, ogun = false, autogun = false, transistorBass = false, transistorBassMobile = false, miniSynth = false, dx10 = false, kepler = false, keplerExo = false, poizone = false,
         fruitkick = false, bassdrum = false, toxic = false, threeOsc = false, simSynth = false, wasp = false, plucked = false, beepMap = false,
         sawer = false, flKeys = false, granulizer = false, drumaxx = false, slayer = false, fruityDance = false, pocketSuperSaw = false, sytrus = false, wavesFound = false,
         engineFilesMissing = false;
    int editorKey = 60;
    int drumPart = 0; // round 168p: the Drumaxx pad / DrumSynth Live key the per-part knobs edit
    std::vector<std::pair<juce::Slider*, const drumcontrols::Control*>> partKnobs;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    const plugui::Layout* layout = nullptr;
    plugui::Layout hostedLayout;
    std::string hostedName;
    const modelparams::Table* modelTable = nullptr; // the engine's listed parameters (round 168n)
    InstrumentKind instrumentKind = InstrumentKind::synth;
    const plugui::LayoutItem* stepperDrag = nullptr; // (round 169h)
    float stepperDragY = 0.0f;
    int stepperDragged = 0;
    int page = 0; // the layout's page shown (pageTabs items switch it; round 168n)
    const plugui::LayoutItem* xyItem = nullptr; // the pad being dragged
    juce::TextButton sampleButton; // (a Granulizer node: round 165c)
    std::unique_ptr<juce::FileChooser> chooser;
};

// FL's Sampler › "Precomputed effects", the loop flags and the declicking mode (REA_rodada167h §1–§3): computed once on
// the sample when the channel changes (core/SamplePrecompute.h). Opened from the Sampler settings.
class PrecomputePanel final : public juce::Component
{
public:
    PrecomputePanel (AudioEngine& e, ChannelRef c) : engine (e), ref (std::move (c))
    {
        const std::pair<juce::ToggleButton*, const char*> toggles[] = {
            { &removeDc, TR_NOOP ("Remove DC offset") }, { &normalize, TR_NOOP ("Normalize") }, { &polarity, TR_NOOP ("Reverse polarity") },
            { &fadeStereo, TR_NOOP ("Fade stereo") }, { &swapStereo, TR_NOOP ("Swap stereo") },
            { &useLoops, TR_NOOP ("Use loop points") }, { &pingPong, TR_NOOP ("Ping pong loop") } };
        for (auto [b, text] : toggles)
        {
            b->setButtonText (tr (text));
            b->onClick = [this] { apply(); };
            addAndMakeVisible (b);
        }
        normalize.setTooltip (tr ("Raises the sample so its loudest point reaches 0 dB"));
        fadeStereo.setTooltip (tr ("The left channel fades out over the sample while the right one fades in"));
        useLoops.setTooltip (tr ("Plays the loop stored in the sample file (WAV loop points)"));
        pingPong.setTooltip (tr ("The loop plays forward, then backward"));
        const std::tuple<juce::Slider*, juce::Label*, const char*, double> knobs[] = {
            { &fadeIn, &fadeInLabel, TR_NOOP ("Fade in"), 1024.0 }, { &fadeOut, &fadeOutLabel, TR_NOOP ("Fade out"), 1024.0 },
            { &crossfade, &crossfadeLabel, TR_NOOP ("Crossfade"), 2048.0 }, { &trim, &trimLabel, TR_NOOP ("Trim"), 32768.0 } };
        for (auto [k, label, text, hi] : knobs)
        {
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
            k->setRange (0.0, hi, 1.0);
            k->setDoubleClickReturnValue (true, 0.0);
            k->onValueChange = [this] { apply(); };
            addAndMakeVisible (k);
            label->setText (tr (text), juce::dontSendNotification);
            label->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (label);
        }
        auto percent = [] (double hi) { return [hi] (double v) { return juce::String (v / hi * 100.0, 1) + "%"; }; };
        fadeIn.textFromValueFunction = percent (1024.0);
        fadeOut.textFromValueFunction = percent (1024.0);
        crossfade.textFromValueFunction = percent (2048.0);
        trim.textFromValueFunction = [] (double v) {
            return v <= 0.0 ? tr ("Off") : juce::String (20.0 * std::log10 (v / 32768.0), 1) + " dB";
        };
        fadeIn.setTooltip (tr ("Fades the sample in: 25% ramps over the whole sample, 100% over its last quarter"));
        fadeOut.setTooltip (tr ("Fades the sample out: 25% over the whole sample, 100% within its first quarter"));
        crossfade.setTooltip (tr ("Crossfades the loop's end into its start (a share of the loop), for a smooth loop"));
        trim.setTooltip (tr ("Cuts the silence below this level at the sample's start and end"));
        declickLabel.setText (tr ("Declicking mode"), juce::dontSendNotification);
        addAndMakeVisible (declickLabel);
        for (const auto* name : samplerpage::declickModeNames) // (FL's items, round 190dh)
            declick.addItem (tr (name), declick.getNumItems() + 1);
        declick.setTooltip (tr ("How fast a cut note fades out: 10 ms (out only, transient no bleeding), 20 ms (transient, generic), 100 ms (smooth), 200 ms (crossfade)"));
        declick.onChange = [this] { apply(); };
        addAndMakeVisible (declick);

        // FL's legacy effects page (REA_rodada167l §1): FL's knob ranges, defaults and hints
        legacyHeader.setText (tr ("Legacy effects"), juce::dontSendNotification);
        legacyHeader.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (legacyHeader);
        const std::tuple<juce::Slider*, juce::Label*, const char*, const char*, double, double, double> legacy[] = {
            { &boost, &boostLabel, TR_NOOP ("Boost"), TR_NOOP ("Boosts the gain"), 0.0, 256.0, 0.0 },
            { &eq, &eqLabel, TR_NOOP ("EQ"), TR_NOOP ("Shifts the frequency balance"), 0.0, 256.0, 128.0 },
            { &ringMix, &ringMixLabel, TR_NOOP ("RM mix"), TR_NOOP ("Ring modulation mix"), 0.0, 256.0, 0.0 },
            { &ringFreq, &ringFreqLabel, TR_NOOP ("RM freq"), TR_NOOP ("Ring modulation frequency"), 0.0, 256.0, 128.0 },
            { &pogo, &pogoLabel, TR_NOOP ("Pogo"), TR_NOOP ("Pogo effect"), 0.0, 512.0, 256.0 },
            { &cutoff, &cutoffLabel, TR_NOOP ("Cut"), TR_NOOP ("Filter cutoff"), 16.0, 1024.0, 1024.0 },
            { &resonance, &resonanceLabel, TR_NOOP ("Res"), TR_NOOP ("Filter resonance"), 0.0, 640.0, 0.0 },
            { &stereoDelay, &stereoDelayLabel, TR_NOOP ("Stereo delay"), TR_NOOP ("Stereo delay"), 0.0, 4096.0, 2048.0 },
            { &reverbAmount, &reverbLabel, TR_NOOP ("Reverb"), TR_NOOP ("Reverb amount"), 0.0, 256.0, 0.0 } };
        for (auto [k, label, text, hint, lo, hi, def] : legacy)
        {
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
            k->setRange (lo, hi, 1.0);
            k->setValue (def, juce::dontSendNotification);
            k->setDoubleClickReturnValue (true, def);
            k->setTooltip (tr (hint));
            k->onValueChange = [this] { apply(); };
            addAndMakeVisible (k);
            label->setText (tr (text), juce::dontSendNotification);
            label->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (label);
        }
        reverbType.addItemList ({ tr ("Type 1"), tr ("Type 2") }, 1);
        reverbType.setTooltip (tr ("Reverb type"));
        reverbType.onChange = [this] { apply(); };
        addAndMakeVisible (reverbType);
        clip.setButtonText (tr ("Clip"));
        clip.setTooltip (tr ("Add clipping at 0dB"));
        clip.onClick = [this] { apply(); };
        addAndMakeVisible (clip);
        load();
        setSize (500, 560);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto rows = area.removeFromTop (4 * 26);
        auto left = rows.removeFromLeft (rows.getWidth() / 2);
        for (auto* b : { &removeDc, &normalize, &polarity, &fadeStereo })
            b->setBounds (left.removeFromTop (26));
        for (auto* b : { &swapStereo, &useLoops, &pingPong })
            b->setBounds (rows.removeFromTop (26));
        area.removeFromTop (10);
        auto knobRow = area.removeFromTop (110);
        const auto w = knobRow.getWidth() / 4;
        for (auto [k, label] : { std::pair { &fadeIn, &fadeInLabel }, { &fadeOut, &fadeOutLabel }, { &crossfade, &crossfadeLabel }, { &trim, &trimLabel } })
        {
            auto column = knobRow.removeFromLeft (w);
            label->setBounds (column.removeFromTop (18));
            k->setBounds (column);
        }
        area.removeFromTop (12);
        auto row = area.removeFromTop (26);
        declickLabel.setBounds (row.removeFromLeft (140));
        declick.setBounds (row.removeFromLeft (220));

        area.removeFromTop (14);
        legacyHeader.setBounds (area.removeFromTop (20));
        auto layoutRow = [] (juce::Rectangle<int> r, std::initializer_list<std::pair<juce::Slider*, juce::Label*>> knobs) {
            const auto w = r.getWidth() / 5;
            for (auto [k, label] : knobs)
            {
                auto column = r.removeFromLeft (w);
                label->setBounds (column.removeFromTop (18));
                k->setBounds (column);
            }
            return r;
        };
        layoutRow (area.removeFromTop (96), { { &boost, &boostLabel }, { &eq, &eqLabel }, { &ringMix, &ringMixLabel }, { &ringFreq, &ringFreqLabel }, { &pogo, &pogoLabel } });
        area.removeFromTop (6);
        auto last = layoutRow (area.removeFromTop (96), { { &cutoff, &cutoffLabel }, { &resonance, &resonanceLabel }, { &stereoDelay, &stereoDelayLabel }, { &reverbAmount, &reverbLabel } });
        last.removeFromTop (22);
        reverbType.setBounds (last.removeFromTop (24).reduced (4, 0));
        last.removeFromTop (8);
        clip.setBounds (last.removeFromTop (24).reduced (4, 0));
    }

private:
    void load()
    {
        const auto* c = ref.get (engine);
        if (c == nullptr)
            return;
        const juce::ScopedValueSetter<bool> guard (loading, true);
        const auto& p = c->sampler.precompute;
        removeDc.setToggleState (p.removeDc, juce::dontSendNotification);
        normalize.setToggleState (p.normalize, juce::dontSendNotification);
        polarity.setToggleState (p.reversePolarity, juce::dontSendNotification);
        fadeStereo.setToggleState (p.fadeStereo, juce::dontSendNotification);
        swapStereo.setToggleState (p.swapStereo, juce::dontSendNotification);
        useLoops.setToggleState (c->sampler.useLoopPoints, juce::dontSendNotification);
        pingPong.setToggleState (p.pingPong, juce::dontSendNotification);
        fadeIn.setValue (p.fadeIn, juce::dontSendNotification);
        fadeOut.setValue (p.fadeOut, juce::dontSendNotification);
        crossfade.setValue (p.crossfade, juce::dontSendNotification);
        trim.setValue (p.trim, juce::dontSendNotification);
        boost.setValue (p.boost, juce::dontSendNotification);
        eq.setValue (p.eq, juce::dontSendNotification);
        ringMix.setValue (p.ringMix, juce::dontSendNotification);
        ringFreq.setValue (p.ringFreq, juce::dontSendNotification);
        pogo.setValue (p.pogo, juce::dontSendNotification);
        cutoff.setValue (p.cutoff, juce::dontSendNotification);
        resonance.setValue (p.resonance, juce::dontSendNotification);
        stereoDelay.setValue (p.stereoDelay, juce::dontSendNotification);
        reverbAmount.setValue (p.reverb, juce::dontSendNotification);
        reverbType.setSelectedId (p.reverbType == 0 ? 1 : 2, juce::dontSendNotification);
        clip.setToggleState (p.clip, juce::dontSendNotification);
        declick.setSelectedId (juce::jlimit (0, 5, c->sampler.declickMode) + 1, juce::dontSendNotification);
    }

    void apply()
    {
        if (loading)
            return;
        SamplePrecompute p;
        p.removeDc = removeDc.getToggleState();
        p.normalize = normalize.getToggleState();
        p.reversePolarity = polarity.getToggleState();
        p.fadeStereo = fadeStereo.getToggleState();
        p.swapStereo = swapStereo.getToggleState();
        p.pingPong = pingPong.getToggleState();
        p.fadeIn = (int) fadeIn.getValue();
        p.fadeOut = (int) fadeOut.getValue();
        p.crossfade = (int) crossfade.getValue();
        p.trim = (int) trim.getValue();
        p.boost = (int) boost.getValue();
        p.eq = (int) eq.getValue();
        p.ringMix = (int) ringMix.getValue();
        p.ringFreq = (int) ringFreq.getValue();
        p.pogo = (int) pogo.getValue();
        p.cutoff = (int) cutoff.getValue();
        p.resonance = (int) resonance.getValue();
        p.stereoDelay = (int) stereoDelay.getValue();
        p.reverb = (int) reverbAmount.getValue();
        p.reverbType = reverbType.getSelectedId() == 1 ? 0 : 1;
        p.clip = clip.getToggleState();
        const auto loops = useLoops.getToggleState();
        const auto mode = juce::jlimit (0, 5, declick.getSelectedId() - 1);
        const auto* c = ref.get (engine);
        if (c == nullptr || (c->sampler.precompute == p && c->sampler.useLoopPoints == loops && c->sampler.declickMode == mode))
            return;
        ref.edit (engine, [p, loops, mode] (ChannelDef& d) {
            d.sampler.precompute = p;
            d.sampler.useLoopPoints = loops;
            d.sampler.declickMode = mode;
        });
    }

    AudioEngine& engine;
    ChannelRef ref;
    bool loading = false;
    juce::ToggleButton removeDc, normalize, polarity, fadeStereo, swapStereo, useLoops, pingPong;
    juce::Slider fadeIn, fadeOut, crossfade, trim;
    juce::Label fadeInLabel, fadeOutLabel, crossfadeLabel, trimLabel, declickLabel;
    juce::ComboBox declick;
    juce::Label legacyHeader;
    juce::Slider boost, eq, ringMix, ringFreq, pogo, cutoff, resonance, stereoDelay, reverbAmount;
    juce::Label boostLabel, eqLabel, ringMixLabel, ringFreqLabel, pogoLabel, cutoffLabel, resonanceLabel, stereoDelayLabel, reverbLabel;
    juce::ComboBox reverbType;
    juce::ToggleButton clip;
};

// FL's Load button of the sampler's File row (round 190df): an icon button that also takes the mouse wheel (FL steps
// through the sample history with it [OpenBtnMouseWheel 0xf68f50])
class WheelIconButton final : public IconButton
{
public:
    using IconButton::IconButton;
    std::function<void (int)> onWheel; // +1 up, -1 down
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (onWheel != nullptr && w.deltaY != 0.0f)
            onWheel (w.deltaY > 0.0f ? 1 : -1);
        else
            IconButton::mouseWheelMove (e, w);
    }
};

// FL's "effects target" selector at the head of the Precomputed effects box (round 190dh; form TPluginForm
// FXPanel.EffectsTargetSelect, 46 x 19, two cells; EffectsTargetSelectChange 0xf616d0 shows FXLegacyPanel for cell 1):
// a wrench (the effects) and the legacy effects
class EffectsTargetSelect final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onChange;
    int getIndex() const noexcept { return index; }
    void setIndex (int i)
    {
        if (i != index)
        {
            index = i;
            repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        for (int i = 0; i < 2; ++i)
        {
            auto cell = b.withWidth (b.getWidth() / 2.0f).translated ((float) i * b.getWidth() / 2.0f, 0.0f).reduced (1.0f);
            g.setColour (i == index ? theme::colour::bg6 : theme::colour::bg3);
            g.fillRoundedRectangle (cell, 2.0f);
            const auto colour = i == index ? theme::colour::warning : theme::colour::textSecondary;
            const auto icon = cell.withSizeKeepingCentre (cell.getHeight() - 3.0f, cell.getHeight() - 3.0f);
            if (i == 1)
                icons::draw (g, Icon::mic, icon, colour);
            else
            {
                // a wrench: a handle and an open jaw
                const auto u = icon.getWidth() / 24.0f;
                juce::Path p;
                p.startNewSubPath (icon.getX() + 5.0f * u, icon.getBottom() - 5.0f * u);
                p.lineTo (icon.getX() + 14.0f * u, icon.getY() + 10.0f * u);
                g.setColour (colour);
                g.strokePath (p, juce::PathStrokeType (3.2f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                juce::Path jaw;
                jaw.addCentredArc (icon.getX() + 16.5f * u, icon.getY() + 7.5f * u, 5.0f * u, 5.0f * u, 0.0f, 0.9f, 5.4f, true);
                g.strokePath (jaw, juce::PathStrokeType (2.6f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto i = e.x < getWidth() / 2 ? 0 : 1;
        if (i != index)
        {
            index = i;
            repaint();
            if (onChange != nullptr)
                onChange (index);
        }
    }

private:
    int index = 0;
};

// The Sampler page (round 190dh: FL's Sampler page, form TPluginForm.SmpSheet, 561 x 399, and the FL 2026 shot
// dock188a): File row, Content, Playback | Time stretching, Precomputed effects; the waveform; then "LYNX extras", the
// sampler controls FL's page has no place for (Gain, the envelope, the filter, Cut itself, Porta, Modulation).
class SettingsPanel final : public juce::Component,
                            public juce::DragAndDropTarget,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer
{
public:
    // the page in FL's form units (FL's 561 x 399 sheet, then LYNX's extras below it)
    static constexpr int designWidth = 558, designHeight = 524;
    static constexpr float defaultScale = 1.25f;

    SettingsPanel (AudioEngine& e, ChannelRef c)
        : engine (e),
          ref (std::move (c))
    {
        // ---- File row (round 190df; form GrooveBox, shot dock188a fl-05) ----
        fileLabel.setText (tr ("File"), juce::dontSendNotification);
        fileLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (fileLabel);
        fileName.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (fileName);
        loadButton.setTooltip (tr ("Load sample file... / Sample history"));
        loadButton.onClick = [this] { chooseSample(); };
        loadButton.onRightClick = [this] { showSampleHistory(); };
        loadButton.onWheel = [this] (int delta) {
            if (const auto* c = current())
                if (const auto next = samplehistory::step (c->samplePath, delta))
                    replaceSample (juce::File (juce::String::fromUTF8 (next->c_str())));
        };
        locateButton.setTooltip (tr ("Locate sample in browser"));
        locateButton.onClick = [this] {
            if (const auto* c = current(); c != nullptr && ! c->samplePath.empty() && locateInBrowserHandler())
                locateInBrowserHandler() (juce::File (juce::String::fromUTF8 (c->samplePath.c_str())));
        };
        removeButton.setTooltip (tr ("Remove sample"));
        removeButton.onClick = [this] { removeSample(); };
        for (auto* b : { &loadButton, &locateButton, &removeButton })
        {
            b->setFlat (false);
            addAndMakeVisible (b);
        }

        // ---- Content (form WavePanel) ----
        toggle (keepOnDisk, TR_NOOP ("Keep on disk"), TR_NOOP ("Stream sample from hard disk when possible"));
        keepOnDisk.setTooltip (tr ("Stream sample from hard disk when possible") + " - "
                               + tr ("FL's option, kept for FL projects: LYNX streams samples longer than 30 s from its disk cache by itself"));
        toggle (resampleBox, TR_NOOP ("Resample"), TR_NOOP ("Upsample to mixing frequency"));
        resampleBox.setTooltip (tr ("Upsample to mixing frequency") + " - " + tr ("FL's option, kept for FL projects: LYNX resamples in real time"));
        toggle (loadRegionsBox, TR_NOOP ("Load regions"), nullptr);
        loadRegionsBox.setTooltip (tr ("Load sample regions and REX / ZGR beat markers") + " - " + tr ("FL's option, kept for FL projects"));
        toggle (loadMarkersBox, TR_NOOP ("Load slice markers"), nullptr);
        loadMarkersBox.setTooltip (tr ("Load ACID and Apple Loops beat markers") + " - " + tr ("FL's option, kept for FL projects"));
        declickLabel.setText (tr ("Declicking mode"), juce::dontSendNotification);
        addAndMakeVisible (declickLabel);
        for (const auto* name : samplerpage::declickModeNames)
            declick.addItem (tr (name), declick.getNumItems() + 1);
        declick.setTooltip (tr ("Declicking method") + " - "
                            + tr ("How fast a cut note fades out: 10 ms (out only, transient no bleeding), 20 ms (transient, generic), 100 ms (smooth), 200 ms (crossfade)"));
        declick.onChange = [this] { apply (&declick); };
        addAndMakeVisible (declick);

        // ---- Playback (form LoopPanel) ----
        knob (startOffsetKnob, startOffsetLabel, TR_NOOP ("Start offset"), TR_NOOP ("Sample start offset"), 0.0, 1.0, 1.0 / 16384.0, 0.0);
        startOffsetKnob.textFromValueFunction = [] (double v) { return juce::String (v * 100.0, 1) + " %"; };
        knob (loopStartKnob, loopStartLabel, TR_NOOP ("Start"), TR_NOOP ("Loop start position"), 0.0, 1.0, 1.0, 0.0);
        knob (loopEndKnob, loopEndLabel, TR_NOOP ("End"), TR_NOOP ("Loop end position"), 0.0, 1.0, 1.0, 0.0);
        for (auto* k : { &loopStartKnob, &loopEndKnob })
            k->textFromValueFunction = [] (double v) { return juce::String ((juce::int64) std::llround (v)); }; // (frames)
        toggle (useLoops, TR_NOOP ("Use loop points"), TR_NOOP ("Use sample loop points"));
        toggle (pingPong, TR_NOOP ("Ping pong loop"), TR_NOOP ("Loop forward / backward"));

        // ---- Time stretching (form StretchPanel) ----
        knob (stretchPitchKnob, stretchPitchLabel, TR_NOOP ("Pitch"), TR_NOOP ("Pitch shift"), -1200.0, 1200.0, 1.0, 0.0);
        stretchPitchKnob.textFromValueFunction = [] (double v) { return tr ("{0} cents", { (v > 0.5 ? "+" : "") + juce::String (juce::roundToInt (v)) }); };
        knob (mulKnob, mulLabel, TR_NOOP ("Mul"), TR_NOOP ("Time stretch (multiplicator)"), -20000.0, 20000.0, 1.0, 0.0);
        mulKnob.textFromValueFunction = [] (double v) { return juce::String::charToString (0x00d7) + juce::String (samplerpage::mulFactor ((int) v), 3); };
        knob (stretch, stretchLabel, TR_NOOP ("Time"), TR_NOOP ("Time stretch (time)"), 0.0, 1024.0, 0.01, 0.0);
        stretch.textFromValueFunction = [] (double v) { return v <= 0.0 ? tr ("(none)") : tr ("{0} beats", { juce::String (v, 2) }); };
        // issue #32 #7: "Fit to tempo" sets exact lengths (16.45 beats for a loop with a tail, 128 for 32 bars)
        stretch.setSkewFactorFromMidPoint (16.0);
        stretch.addMouseListener (&stretchMenu, false);
        modeLabel.setText (tr ("Mode"), juce::dontSendNotification);
        addAndMakeVisible (modeLabel);
        for (int i = 0; i < samplerpage::modeItemCount; ++i)
        {
            if (samplerpage::modeItems[i].header)
                stretchMethod.addSectionHeading (tr (samplerpage::modeItems[i].name));
            else
                stretchMethod.addItem (tr (samplerpage::modeItems[i].name), i + 1);
        }
        stretchMethod.setTooltip (tr ("Stretching method") + " - " + tr ("Resample") + " / "
                                  + tr ("How the stretch keeping pitch is computed: General for full mixes and chords, Drums for loops and percussive sounds (attacks kept sharp), Vocal for a single voice or instrument"));
        stretchMethod.onChange = [this] { apply (&stretchMethod); };
        addAndMakeVisible (stretchMethod);

        // ---- Precomputed effects (form FXPanel) ----
        toggle (removeDc, TR_NOOP ("Remove DC offset"), TR_NOOP ("Center vertically"));
        toggle (normalize, TR_NOOP ("Normalize"), TR_NOOP ("Normalize to 0dB"));
        toggle (reverse, TR_NOOP ("Reverse"), TR_NOOP ("Flip horizontally"));
        toggle (polarity, TR_NOOP ("Reverse polarity"), TR_NOOP ("Flip vertically"));
        toggle (fadeStereo, TR_NOOP ("Fade stereo"), TR_NOOP ("Fade from left channel to right channel"));
        toggle (swapStereo, TR_NOOP ("Swap stereo"), TR_NOOP ("Swap left & right channels"));
        knob (regionStart, regionStartLabel, TR_NOOP ("Smp start"), TR_NOOP ("Sample start offset"), 0.0, 1.0, 1.0 / 16384.0, 0.0);
        knob (lengthKnob, lengthLabel, TR_NOOP ("Length"), TR_NOOP ("Sample end offset"), 0.0, 1.0, 1.0 / 16384.0, 1.0);
        for (auto* k : { &regionStart, &lengthKnob })
            k->textFromValueFunction = [] (double v) { return juce::String (v * 100.0, 1) + " %"; };
        knob (fadeIn, fadeInLabel, TR_NOOP ("In"), TR_NOOP ("Quick fade in"), 0.0, 1024.0, 1.0, 0.0);
        knob (fadeOut, fadeOutLabel, TR_NOOP ("Out"), TR_NOOP ("Quick fade out"), 0.0, 1024.0, 1.0, 0.0);
        knob (crossfade, crossfadeLabel, TR_NOOP ("Crossfade"), TR_NOOP ("Crossfade loop"), 0.0, 1024.0, 1.0, 0.0);
        knob (trim, trimLabel, TR_NOOP ("Trim"), TR_NOOP ("Silence trimming threshold"), 0.0, 256.0, 1.0, 0.0);
        auto percent = [] (double hi) { return [hi] (double v) { return juce::String (v / hi * 100.0, 1) + " %"; }; };
        fadeIn.textFromValueFunction = percent (1024.0);
        fadeOut.textFromValueFunction = percent (1024.0);
        crossfade.textFromValueFunction = percent (2048.0); // (of the loop: FL's knob reaches half of it)
        trim.textFromValueFunction = [] (double v) { return v <= 0.0 ? tr ("Off") : juce::String (20.0 * std::log10 (v / 32768.0), 1) + " dB"; };
        effectsTarget.setTooltip (tr ("effects target"));
        effectsTarget.onChange = [this] (int) { showLegacy(); };
        addAndMakeVisible (effectsTarget);
        // the legacy effects (form FXLegacyPanel; REA round 167l §1: FL's ranges and defaults)
        const std::tuple<MenuSlider*, juce::Label*, const char*, const char*, double, double, double> legacy[] = {
            { &boost, &boostLabel, TR_NOOP ("Boost"), TR_NOOP ("Boosts the gain"), 0.0, 256.0, 0.0 },
            { &eq, &eqLabel, TR_NOOP ("EQ"), TR_NOOP ("Shifts the frequency balance"), 0.0, 256.0, 128.0 },
            { &ringMix, &ringMixLabel, TR_NOOP ("RM mix"), TR_NOOP ("Ring modulation mix"), 0.0, 256.0, 0.0 },
            { &ringFreq, &ringFreqLabel, TR_NOOP ("RM freq"), TR_NOOP ("Ring modulation frequency"), 0.0, 256.0, 128.0 },
            { &legacyCut, &legacyCutLabel, TR_NOOP ("Cut"), TR_NOOP ("Filter cutoff"), 16.0, 1024.0, 1024.0 },
            { &legacyRes, &legacyResLabel, TR_NOOP ("Res"), TR_NOOP ("Filter resonance"), 0.0, 640.0, 0.0 },
            { &reverbAmount, &reverbLabel, TR_NOOP ("Reverb"), TR_NOOP ("Reverb amount"), 0.0, 256.0, 0.0 },
            { &stereoDelay, &stereoDelayLabel, TR_NOOP ("Stereo delay"), TR_NOOP ("Stereo delay"), 0.0, 4096.0, 2048.0 },
            { &pogo, &pogoLabel, TR_NOOP ("Pogo"), TR_NOOP ("Pogo effect"), 0.0, 512.0, 256.0 } };
        for (auto [k, label, text, hint, lo, hi, def] : legacy)
            knob (*k, *label, text, hint, lo, hi, 1.0, def);
        reverbType.addItemList ({ "A", "B" }, 1);
        reverbType.setTooltip (tr ("Reverb type"));
        reverbType.onChange = [this] { apply (&reverbType); };
        addChildComponent (reverbType);
        toggle (clip, TR_NOOP ("Clip"), TR_NOOP ("Add clipping at 0dB"));

        // ---- the waveform ----
        wave.setEmptyText (tr ("No sample loaded"));
        wave.setTooltip (tr ("Waveform") + " - " + tr ("click: preview the sample; mouse wheel: zoom"));
        wave.setWatermark (tr ("Sampler"));
        wave.setCaptionVisible (false);
        wave.setZoomable (true);
        wave.onViewChanged = [this] { waveScroll.setCurrentRange (wave.getViewStart(), wave.getViewEnd() - wave.getViewStart(), juce::dontSendNotification); };
        // round 190w: FL 2026 previews the sample when its display is clicked (from the start offset, to its end) and shows
        // the playback locator over it (What's New 21138; [SHOT] flgap190w burst n2 / o3)
        wave.onClick = [this] { previewFromDisplay(); };
        addAndMakeVisible (wave);
        waveScroll.setRangeLimits (0.0, 1.0, juce::dontSendNotification);
        waveScroll.setCurrentRange (0.0, 1.0, juce::dontSendNotification);
        waveScroll.setAutoHide (false);
        waveScroll.addListener (this);
        addAndMakeVisible (waveScroll);

        // ---- LYNX extras ----
        knob (gain, gainLabel, TR_NOOP ("Gain"), nullptr, -24.0, 24.0, 0.1, 0.0);
        gain.textFromValueFunction = [] (double v) { return (v > 0.04 ? "+" : "") + juce::String (std::abs (v) < 0.05 ? 0.0 : v, 1) + " dB"; };
        gain.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("+-0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
        gain.setTooltip (tr ("Sample gain: boosts or cuts the sample's level before the channel volume (-24 to +24 dB, double-click = 0 dB)"));
        knob (attack, attackLabel, TR_NOOP ("Attack"), nullptr, 0.0, 2.0, 0.001, 0.0);
        knob (decay, decayLabel, TR_NOOP ("Decay"), nullptr, 0.0, 10.0, 0.001, 0.0);
        knob (sustain, sustainLabel, TR_NOOP ("Sustain"), nullptr, 0.0, 1.0, 0.01, 1.0);
        knob (release, releaseLabel, TR_NOOP ("Release"), nullptr, 0.0, 4.0, 0.001, 0.0);
        for (auto* k : { &attack, &decay, &release })
            k->textFromValueFunction = [] (double v) { return juce::String (v, 3) + " s"; };
        knob (cutoff, cutoffLabel, TR_NOOP ("Cutoff"), nullptr, 20.0, 20000.0, 1.0, 20000.0);
        cutoff.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " Hz"; };
        knob (resonance, resonanceLabel, TR_NOOP ("Resonance"), nullptr, 0.0, 1.0, 0.01, 0.0);
        cutoff.setSkewFactorFromMidPoint (1000.0);
        cutoff.setTooltip (tr ("Channel filter cutoff (low-pass at maximum with no resonance = off)"));
        decay.setSkewFactorFromMidPoint (0.5);
        attack.setSkewFactorFromMidPoint (0.2);
        release.setSkewFactorFromMidPoint (0.4);
        release.setTooltip (tr ("0 = the whole sample plays (one-shot), like FL's steps"));
        filterType.addItemList ({ tr ("Low-pass"), tr ("Band-pass"), tr ("High-pass"), tr ("Notch") }, 1);
        filterType.onChange = [this] { apply (&filterType); };
        addAndMakeVisible (filterType);
        toggle (cut, TR_NOOP ("Cut itself"), nullptr);
        cut.setTooltip (tr ("A new note cuts the previous one (good for hi-hats and 808s)"));
        porta.setButtonText ("Porta");
        porta.setTooltip (tr ("Portamento: the note starts from the previous note's pitch (with Cut itself, the 808 glide)"));
        porta.onClick = [this] { apply (&porta); };
        addAndMakeVisible (porta);
        modulationButton.setButtonText (tr ("Modulation (envelopes and LFOs)..."));
        modulationButton.onClick = [this] { showModulationFor (*this, engine, ref.index); };
        addAndMakeVisible (modulationButton);
        extrasLabel.setText (tr ("LYNX extras"), juce::dontSendNotification);

        // the knobs an automation clip can drive (samplerparam, issue #12): right-click = FL's parameter menu; a move
        // is a tweak (Last tweaked, automation recording)
        for (auto [slider, param] : { std::pair { &attack, (int) samplerparam::attack }, { &decay, (int) samplerparam::decay },
                                      { &sustain, (int) samplerparam::sustain }, { &release, (int) samplerparam::release },
                                      { &cutoff, (int) samplerparam::cutoff }, { &resonance, (int) samplerparam::resonance },
                                      { &regionStart, (int) samplerparam::start }, { &lengthKnob, (int) samplerparam::end } })
        {
            if (ref.index < 0) // (a Patcher node: no automation target)
                break;
            const auto target = [this, param] { return AutomationTarget { AutomationTargetKind::channelParam, ref.index, 0, param }; };
            ui::setOwnMenu (*slider);
            slider->onMenu = [this, slider, target] { ui::showSliderMenu (*slider, &engine, target()); };
            slider->onDragStart = [this, target] { engine.noteTweaked (target(), true); };
            slider->onDragEnd = [this, target] { engine.tweakReleased (target()); };
            tweakTargets.push_back ({ slider, param });
        }

        refresh();
        showLegacy();
        engine.addChangeListener (this);
        setSize (juce::roundToInt (designWidth * defaultScale), juce::roundToInt (designHeight * defaultScale));
        if (ref.index >= 0)
            startTimerHz (30); // the knobs follow their automation while the song plays (issue #12)
    }

    ~SettingsPanel() override
    {
        stopPreview();
        waveScroll.removeListener (this);
        engine.removeChangeListener (this);
    }

    // FL's sample display click: the channel's root note, the sample from its start offset; a second click stops it
    void previewFromDisplay()
    {
        const auto* c = current();
        if (c == nullptr || ref.index < 0)
            return;
        if (previewKey >= 0)
        {
            stopPreview();
            return;
        }
        previewKey = juce::jlimit (0, 127, c->misc.rootNote);
        previewSeen = false;
        previewFrames = 0;
        engine.sendLiveNote (ref.index, previewKey, 100.0f / 128.0f, true); // (FL's default velocity)
    }

    void stopPreview()
    {
        if (previewKey >= 0 && ref.index >= 0)
            engine.sendLiveNote (ref.index, previewKey, 0.0f, false);
        previewKey = -1;
        wave.setLocator (-1.0f);
    }

    // the locator follows the preview's voice; the preview ends with it (or ~2 s after a note that never sounded)
    void followPreview()
    {
        if (previewKey < 0)
            return;
        const auto instrument = engine.getChannelInstrument (ref.index);
        const auto at = instrument != nullptr ? instrument->playPosition() : -1.0f;
        ++previewFrames;
        if (at >= 0.0f)
            previewSeen = true;
        if ((previewSeen && at < 0.0f) || (! previewSeen && previewFrames > 60))
        {
            stopPreview();
            return;
        }
        wave.setLocator (at);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        // FL's boxes: a rounded panel with its caption on a tab at the top left (the File row has none)
        for (const auto& [area, caption, captionLeft] : groups)
        {
            const auto r = area.toFloat();
            g.setColour (theme::colour::bg3);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (theme::colour::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            if (caption.isEmpty())
                continue;
            const auto h = scaled (19.0f);
            g.setFont (theme::font (juce::jlimit (10.0f, 18.0f, scaled (12.5f)), true));
            const auto w = (float) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), caption) + scaled (16.0f);
            juce::Path tab;
            const auto x0 = r.getX() + scaled ((float) captionLeft);
            tab.startNewSubPath (x0, r.getY());
            tab.lineTo (x0 + w, r.getY());
            tab.lineTo (x0 + w - h * 0.45f, r.getY() + h);
            tab.lineTo (x0, r.getY() + h);
            tab.closeSubPath();
            g.setColour (theme::colour::bg4);
            g.fillPath (tab);
            g.setColour (theme::colour::textPrimary);
            g.drawText (caption, juce::Rectangle<float> (x0 + scaled (6.0f), r.getY(), w, h), juce::Justification::centredLeft, false);
        }
        // FL 2026's loop glyph between the loop START and END captions
        if (! loopGlyph.isEmpty())
            icons::draw (g, Icon::loop, loopGlyph.toFloat(), theme::colour::textSecondary);
    }

    void resized() override
    {
        // FL's layout in its own units, scaled to the page (the whole page fits; at 1.25 it is FL's at 125 %)
        scale = juce::jmax (0.55f, juce::jmin ((float) getWidth() / (float) designWidth, (float) getHeight() / (float) designHeight));
        offsetX = juce::jmax (0, (getWidth() - juce::roundToInt ((float) designWidth * scale)) / 2);
        groups.clear();
        const auto captionFont = theme::font (juce::jlimit (8.0f, 13.0f, scaled (8.6f)), true);
        const auto textFont = juce::jlimit (9.0f, 17.0f, scaled (12.0f));
        auto place = [this, captionFont] (juce::Slider& k, juce::Label& label, int x, int y, int labelX, int labelW, int size = 34) {
            k.setBounds (R (x, y, size, size));
            label.setFont (captionFont);
            label.setBounds (R (labelX, y + size - 1, labelW, 13));
        };

        // left column: File row, Content, Playback
        groups.push_back ({ R (7, 7, 268, 35), juce::String(), 0 });
        {
            removeButton.setBounds (R (243, 13, 19, 23));
            locateButton.setBounds (R (224, 13, 19, 23));
            loadButton.setBounds (R (192, 13, 32, 23));
            fileLabel.setFont (theme::font (textFont, true));
            fileLabel.setBounds (R (11, 15, 24, 15));
            fileName.setFont (theme::font (textFont));
            fileName.setBounds (R (38, 13, 152, 21));
        }
        groups.push_back ({ R (7, 50, 268, 110), tr ("Content"), 0 });
        keepOnDisk.setBounds (R (12, 75, 126, 21));
        resampleBox.setBounds (R (12, 96, 126, 21));
        loadRegionsBox.setBounds (R (140, 75, 128, 21));
        loadMarkersBox.setBounds (R (140, 96, 128, 21));
        declickLabel.setFont (theme::font (textFont));
        declickLabel.setBounds (R (14, 126, 100, 23));
        declick.setBounds (R (116, 126, 152, 23));

        groups.push_back ({ R (7, 168, 268, 79), tr ("Playback"), 0 });
        place (startOffsetKnob, startOffsetLabel, 26, 194, 9, 68);
        place (loopStartKnob, loopStartLabel, 80, 194, 66, 40);
        place (loopEndKnob, loopEndLabel, 118, 194, 112, 30);
        loopGlyph = R (105, 229, 10, 10);
        useLoops.setBounds (R (150, 191, 122, 21));
        pingPong.setBounds (R (150, 213, 122, 21));

        // right column: Time stretching, Precomputed effects
        groups.push_back ({ R (283, 7, 268, 76), tr ("Time stretching"), 0 });
        place (stretchPitchKnob, stretchPitchLabel, 291, 33, 285, 44, 32);
        place (mulKnob, mulLabel, 333, 33, 327, 44, 32);
        place (stretch, stretchLabel, 373, 33, 367, 44, 32);
        modeLabel.setFont (theme::font (textFont, true));
        modeLabel.setBounds (R (410, 31, 40, 23));
        stretchMethod.setBounds (R (450, 31, 96, 23));

        groups.push_back ({ R (283, 91, 268, 156), tr ("Precomputed effects"), 49 });
        effectsTarget.setBounds (R (284, 92, 46, 18));
        removeDc.setBounds (R (288, 116, 126, 23));
        normalize.setBounds (R (288, 138, 126, 23));
        reverse.setBounds (R (288, 160, 126, 23));
        polarity.setBounds (R (416, 116, 130, 23));
        fadeStereo.setBounds (R (416, 138, 130, 23));
        swapStereo.setBounds (R (416, 160, 130, 23));
        place (regionStart, regionStartLabel, 290, 195, 280, 54);
        place (lengthKnob, lengthLabel, 332, 195, 326, 46);
        place (fadeIn, fadeInLabel, 386, 195, 383, 40);
        place (fadeOut, fadeOutLabel, 425, 195, 422, 40);
        place (crossfade, crossfadeLabel, 472, 195, 459, 60);
        place (trim, trimLabel, 511, 195, 508, 40);
        // the legacy panel over the same box (FXLegacyPanel at 284, 113)
        place (boost, boostLabel, 291, 118, 285, 46);
        place (eq, eqLabel, 333, 118, 327, 46);
        place (ringMix, ringMixLabel, 381, 118, 375, 46);
        place (ringFreq, ringFreqLabel, 421, 118, 415, 46);
        place (legacyCut, legacyCutLabel, 472, 117, 466, 46);
        place (legacyRes, legacyResLabel, 511, 117, 505, 46);
        clip.setBounds (R (287, 167, 52, 16));
        reverbType.setBounds (R (288, 199, 44, 22));
        place (reverbAmount, reverbLabel, 333, 195, 324, 52);
        place (stereoDelay, stereoDelayLabel, 381, 195, 362, 72);
        place (pogo, pogoLabel, 511, 195, 505, 46);

        // the waveform under both columns, with its scroll bar
        wave.setBounds (R (7, 255, 544, 112));
        waveScroll.setBounds (R (7, 368, 544, 14));

        // LYNX extras (FL's page has no place for them)
        groups.push_back ({ R (7, 390, 544, 128), extrasLabel.getText(), 0 });
        {
            int x = 18;
            for (auto [k, label] : { std::pair { &gain, &gainLabel }, { &attack, &attackLabel }, { &decay, &decayLabel },
                                     { &sustain, &sustainLabel }, { &release, &releaseLabel }, { &cutoff, &cutoffLabel },
                                     { &resonance, &resonanceLabel } })
            {
                place (*k, *label, x, 418, x - 12, 58, 36);
                x += 58;
            }
            filterType.setBounds (R (432, 420, 110, 22));
            cut.setBounds (R (432, 446, 110, 21));
            porta.setBounds (R (432, 468, 110, 21));
            modulationButton.setBounds (R (18, 488, 260, 22));
        }
        for (auto* b : { &keepOnDisk, &resampleBox, &loadRegionsBox, &loadMarkersBox, &useLoops, &pingPong, &removeDc, &normalize, &reverse,
                         &polarity, &fadeStereo, &swapStereo, &clip, &cut, &porta })
            b->setSize (b->getWidth(), b->getHeight()); // (their text follows the LookAndFeel's toggle font, sized by height)
        repaint();
    }

    int getChannel() const noexcept { return ref.index; }

    // A sample dropped on the waveform (from the Browser or the system) replaces the channel's sample (issue #12)
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return BrowserComponent::isBrowserDrag (details) && SampleLibrary::isSampleFile (BrowserComponent::draggedFile (details));
    }
    void itemDragEnter (const SourceDetails&) override { setDropHighlight (true); }
    void itemDragExit (const SourceDetails&) override { setDropHighlight (false); }
    void itemDropped (const SourceDetails& details) override
    {
        setDropHighlight (false);
        replaceSample (BrowserComponent::draggedFile (details));
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && SampleLibrary::isSampleFile (juce::File (files[0]));
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { setDropHighlight (true); }
    void fileDragExit (const juce::StringArray&) override { setDropHighlight (false); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        setDropHighlight (false);
        replaceSample (juce::File (files[0]));
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropHighlight)
        {
            g.setColour (theme::colour::accent);
            g.drawRect (wave.getBounds().expanded (2), 2);
        }
    }

    // (the shot route, round 190dh: turn a control as a user would — `what`: a knob's name or a toggle's — and read the
    // channel back; true when the control exists)
    bool shotTurn (const juce::String& what, double value, bool shift = false)
    {
        refresh(); // (the engine's change message is asynchronous: the page shows the channel as a user would see it)
        shiftHeld = shift;
        const juce::ScopedValueSetter<bool> nofocus (shotDriven, true);
        auto turn = [&] (juce::Slider& s) { s.setValue (value, juce::sendNotificationSync); return true; };
        auto click = [&] (juce::Button& b) { b.setToggleState (value != 0.0, juce::dontSendNotification); b.onClick(); return true; };
        bool done = false;
        if (what == "pitch") done = turn (stretchPitchKnob);
        else if (what == "mul") done = turn (mulKnob);
        else if (what == "time") done = turn (stretch);
        else if (what == "loopStart") done = turn (loopStartKnob);
        else if (what == "loopEnd") done = turn (loopEndKnob);
        else if (what == "crossfade") done = turn (crossfade);
        else if (what == "smpStart") done = turn (regionStart);
        else if (what == "length") done = turn (lengthKnob);
        else if (what == "useLoops") done = click (useLoops);
        else if (what == "pingPong") done = click (pingPong);
        else if (what == "keepOnDisk") done = click (keepOnDisk);
        else if (what == "mode") { stretchMethod.setSelectedId ((int) value + 1, juce::sendNotificationSync); done = true; }
        else if (what == "declick") { declick.setSelectedId ((int) value + 1, juce::sendNotificationSync); done = true; }
        else if (what == "legacy") { effectsTarget.setIndex ((int) value); showLegacy(); done = true; }
        else if (what == "zoom") { wave.setView (0.25, 0.25 + value); done = true; }
        shiftHeld = false;
        refresh();
        return done;
    }
    bool shotLegacyShown() const { return boost.isVisible() && ! removeDc.isVisible(); }
    bool shotEnabled (const juce::String& what) const { return what == "keepOnDisk" ? keepOnDisk.isEnabled() : what == "loopStart" ? loopStartKnob.isEnabled() : false; }
    double shotScrollStart() const { return waveScroll.getCurrentRangeStart(); }
    juce::String shotModeText() const { return stretchMethod.getText(); }

private:
    juce::Rectangle<int> R (int x, int y, int w, int h) const
    {
        return { offsetX + juce::roundToInt ((float) x * scale), juce::roundToInt ((float) y * scale), juce::roundToInt ((float) w * scale),
                 juce::roundToInt ((float) h * scale) };
    }
    float scaled (float v) const noexcept { return v * scale; }

    void toggle (juce::ToggleButton& b, const char* text, const char* hint)
    {
        b.setButtonText (tr (text));
        if (hint != nullptr)
            b.setTooltip (tr (hint));
        b.onClick = [this, &b] { apply (&b); };
        addAndMakeVisible (b);
    }

    void knob (juce::Slider& s, juce::Label& label, const char* name, const char* hint, double lo, double hi, double step, double def)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setPopupDisplayEnabled (true, true, this);
        s.setRange (lo, hi, step);
        s.setValue (def, juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, def);
        if (hint != nullptr)
            s.setTooltip (tr (hint));
        s.onValueChange = [this, &s] { apply (&s); };
        label.setText (tr (name).toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredTop);
        label.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        label.setInterceptsMouseClicks (false, false);
        label.setBorderSize ({});
        addAndMakeVisible (s);
        addAndMakeVisible (label);
    }

    // FL's EffectsTargetSelect: cell 1 shows the legacy panel over the effects (FUN_0060a270 on FXLegacyPanel)
    void showLegacy()
    {
        const auto legacy = effectsTarget.getIndex() == 1;
        for (juce::Component* c : std::initializer_list<juce::Component*> { &removeDc, &normalize, &reverse, &polarity, &fadeStereo, &swapStereo, &regionStart,
                                                                           &regionStartLabel, &lengthKnob, &lengthLabel, &fadeIn, &fadeInLabel, &fadeOut,
                                                                           &fadeOutLabel, &crossfade, &crossfadeLabel, &trim, &trimLabel })
            c->setVisible (! legacy);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &boost, &boostLabel, &eq, &eqLabel, &ringMix, &ringMixLabel, &ringFreq, &ringFreqLabel,
                                                                           &legacyCut, &legacyCutLabel, &legacyRes, &legacyResLabel, &reverbAmount, &reverbLabel,
                                                                           &stereoDelay, &stereoDelayLabel, &pogo, &pogoLabel, &reverbType, &clip })
            c->setVisible (legacy);
    }

    void scrollBarMoved (juce::ScrollBar*, double start) override
    {
        const auto width = wave.getViewEnd() - wave.getViewStart();
        wave.setView (start, start + width);
    }

    void setDropHighlight (bool on)
    {
        if (dropHighlight != on)
        {
            dropHighlight = on;
            repaint (wave.getBounds().expanded (4));
        }
    }

    // the new sample plays with the same settings; slices of the old one no longer apply, the channel takes its name;
    // loop points set on the old one go with it
    void replaceSample (const juce::File& file)
    {
        if (current() == nullptr || ! file.existsAsFile())
            return;
        if (engine.getSampleLibrary().load (file.getFullPathName()) == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Sample"), tr ("Could not read {0}", { file.getFileName() }));
            return;
        }
        ref.edit (engine, [path = file.getFullPathName().toStdString(), name = file.getFileNameWithoutExtension().toStdString()] (ChannelDef& d) {
            d.samplePath = path;
            d.zones.clear();
            d.sampler.slices.clear();
            d.sampler.loopStart = d.sampler.loopEnd = -1;
            d.name = name;
        });
        samplehistory::add (file.getFullPathName().toStdString()); // (FL: every sample a sampler loads)
    }

    // FL's Load button [OpenBtnClick 0xf68f20]: the sample file dialog
    void chooseSample()
    {
        const auto* c = current();
        const juce::File now (c != nullptr ? juce::String::fromUTF8 (c->samplePath.c_str()) : juce::String());
        sampleChooser = lynx::ui::makeFileChooser (tr ("Load sample file..."), now.existsAsFile() ? now : juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        lynx::ui::launchChooser (sampleChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [safe = juce::Component::SafePointer<SettingsPanel> (this)] (const juce::FileChooser& fc) {
                                     if (safe != nullptr && fc.getResult() != juce::File())
                                         safe->replaceSample (fc.getResult());
                                 });
    }

    // FL's right-click on Load [WAVFileNameListMenuPopup 0x121afb0]: caption "Sample history", then the samples loaded
    // (newest first, by name); a click loads one into this channel
    juce::PopupMenu sampleHistoryMenu()
    {
        juce::PopupMenu menu;
        const auto& paths = samplehistory::list();
        if (paths.empty())
            return menu;
        menu.addSectionHeader (tr ("Sample history"));
        for (const auto& p : paths)
        {
            const juce::File file (juce::String::fromUTF8 (p.c_str()));
            menu.addItem (file.getFileName(), [safe = juce::Component::SafePointer<SettingsPanel> (this), file] {
                if (safe != nullptr)
                    if (const auto* c = safe->current(); c != nullptr && c->samplePath != file.getFullPathName().toStdString())
                        safe->replaceSample (file);
            });
        }
        return menu;
    }
    void showSampleHistory()
    {
        auto menu = sampleHistoryMenu();
        if (menu.getNumItems() > 0)
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton));
    }

    // FL's Remove sample [RemoveBtnClick 0xf667a0]: one undo step "remove channel sample", only with a sample
    void removeSample()
    {
        const auto* c = current();
        if (c == nullptr || (c->samplePath.empty() && c->zones.empty()))
            return;
        stopPreview();
        ref.edit (engine, [] (ChannelDef& d) {
            d.samplePath.clear();
            d.zones.clear();
            d.sampler.slices.clear();
            d.sampler.loopStart = d.sampler.loopEnd = -1;
        });
    }

public:
    // (the shot route: the history menu without showing it, and the row's buttons)
    juce::PopupMenu sampleHistoryMenuForShot() { return sampleHistoryMenu(); }
    void pressFileRow (int button)
    {
        if (button == 0 || button == 1)
            loadButton.onWheel (button == 0 ? -1 : 1);
        else if (button == 2)
            locateButton.onClick();
        else if (button == 3)
            removeSample();
    }

private:
    const ChannelDef* current() const
    {
        return ref.get (engine);
    }

    // the channel's sample as the loop knobs see it (frames and the file's own loop); none for a multi-sample channel
    std::shared_ptr<const SampleData> sampleOf (const ChannelDef& c) const
    {
        if (c.samplePath.empty() || ! c.zones.empty())
            return nullptr;
        return engine.getSampleLibrary().load (juce::String::fromUTF8 (c.samplePath.c_str()));
    }

    void refresh()
    {
        const auto* c = current();
        if (c == nullptr)
            return;
        const auto& s = c->sampler;
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto sample = sampleOf (*c);
        const auto frames = sample != nullptr ? (std::int64_t) sample->frames() : 0;
        // Content
        keepOnDisk.setToggleState (s.keepOnDisk, juce::dontSendNotification);
        keepOnDisk.setEnabled (samplerpage::keepOnDiskAvailable (s, sample != nullptr && sample->loopStart >= 0 && sample->loopEnd > sample->loopStart));
        resampleBox.setToggleState (s.resampleOnLoad, juce::dontSendNotification);
        loadRegionsBox.setToggleState (s.loadRegions, juce::dontSendNotification);
        loadMarkersBox.setToggleState (s.loadSliceMarkers, juce::dontSendNotification);
        declick.setSelectedId (juce::jlimit (0, 5, s.declickMode) + 1, juce::dontSendNotification);
        // Playback
        startOffsetKnob.setValue (s.startOffset, juce::dontSendNotification);
        useLoops.setToggleState (s.useLoopPoints, juce::dontSendNotification);
        pingPong.setToggleState (s.precompute.pingPong, juce::dontSendNotification);
        loopStartKnob.setEnabled (frames > 1);
        loopEndKnob.setEnabled (frames > 1);
        shownFileLoop = { sample != nullptr ? sample->loopStart : -1, sample != nullptr ? sample->loopEnd : -1 };
        shownFrames = frames;
        if (frames > 1)
        {
            loopStartKnob.setRange (0.0, (double) (frames - 1), 1.0);
            loopEndKnob.setRange (0.0, (double) (frames - 1), 1.0);
            const auto [a, b] = samplerpage::shownLoop (s, shownFileLoop.first, shownFileLoop.second);
            loopStartKnob.setValue ((double) juce::jmax<std::int64_t> (0, a), juce::dontSendNotification);
            loopEndKnob.setValue ((double) (b >= 0 ? b : frames - 1), juce::dontSendNotification);
            loopStartKnob.setDoubleClickReturnValue (false, 0.0);
            loopEndKnob.setDoubleClickReturnValue (false, 0.0);
            wave.setLoop (a >= 0 && s.useLoopPoints ? (float) a / (float) (frames - 1) : -1.0f, b >= 0 && s.useLoopPoints ? (float) b / (float) (frames - 1) : -1.0f);
        }
        else
            wave.setLoop (-1.0f, -1.0f);
        // Time stretching
        stretchPitchKnob.setValue (s.flStretchPitch, juce::dontSendNotification);
        mulKnob.setValue (s.stretchMul, juce::dontSendNotification);
        stretch.setValue (samplerpage::stretchTime (s), juce::dontSendNotification);
        stretchMethod.setSelectedId (samplerpage::modeIndex (s) + 1, juce::dontSendNotification);
        // Precomputed effects
        const auto& p = s.precompute;
        removeDc.setToggleState (p.removeDc, juce::dontSendNotification);
        normalize.setToggleState (p.normalize, juce::dontSendNotification);
        reverse.setToggleState (s.reverse, juce::dontSendNotification);
        polarity.setToggleState (p.reversePolarity, juce::dontSendNotification);
        fadeStereo.setToggleState (p.fadeStereo, juce::dontSendNotification);
        swapStereo.setToggleState (p.swapStereo, juce::dontSendNotification);
        regionStart.setValue (s.regionStart, juce::dontSendNotification);
        lengthKnob.setValue (samplerpage::lengthOf (s), juce::dontSendNotification);
        fadeIn.setValue (p.fadeIn, juce::dontSendNotification);
        fadeOut.setValue (p.fadeOut, juce::dontSendNotification);
        // (a value over FL's knob range from an old project shows at the end of a range that holds it)
        crossfade.setRange (0.0, juce::jmax (1024.0, (double) p.crossfade), 1.0);
        crossfade.setValue (p.crossfade, juce::dontSendNotification);
        trim.setRange (0.0, juce::jmax (256.0, (double) p.trim), 1.0);
        trim.setValue (p.trim, juce::dontSendNotification);
        boost.setValue (p.boost, juce::dontSendNotification);
        eq.setValue (p.eq, juce::dontSendNotification);
        ringMix.setValue (p.ringMix, juce::dontSendNotification);
        ringFreq.setValue (p.ringFreq, juce::dontSendNotification);
        legacyCut.setValue (p.cutoff, juce::dontSendNotification);
        legacyRes.setValue (p.resonance, juce::dontSendNotification);
        reverbAmount.setValue (p.reverb, juce::dontSendNotification);
        reverbType.setSelectedId (p.reverbType == 0 ? 1 : 2, juce::dontSendNotification);
        stereoDelay.setValue (p.stereoDelay, juce::dontSendNotification);
        pogo.setValue (p.pogo, juce::dontSendNotification);
        clip.setToggleState (p.clip, juce::dontSendNotification);
        // LYNX extras
        gain.setValue (s.gainDb, juce::dontSendNotification);
        attack.setValue (s.attack, juce::dontSendNotification);
        decay.setValue (s.decay, juce::dontSendNotification);
        sustain.setValue (s.sustain, juce::dontSendNotification);
        release.setValue (s.release, juce::dontSendNotification);
        cutoff.setValue (s.cutoff, juce::dontSendNotification);
        resonance.setValue (s.resonance, juce::dontSendNotification);
        filterType.setSelectedId (s.filterType + 1, juce::dontSendNotification);
        cut.setToggleState (channelCutsItself (*c), juce::dontSendNotification); // (issue #68: the rack menu's state too)
        porta.setToggleState (s.porta, juce::dontSendNotification);
        showAutomation (true);

        const auto path = juce::String::fromUTF8 ((c->samplePath.empty() && ! c->zones.empty() ? c->zones.front().path : c->samplePath).c_str());
        fileName.setText (juce::File (path).getFileName(), juce::dontSendNotification); // (FL: the name, the path's end)
        fileName.setTooltip (path);
        locateButton.setEnabled (! c->samplePath.empty());
        removeButton.setEnabled (! c->samplePath.empty() || ! c->zones.empty());
        wave.setFile (path, [&e = engine, path] { return e.getSampleLibrary().load (path); });
        wave.setRegion (s.regionStart, s.regionEnd);
        wave.setReversed (s.reverse);
        wave.setStartOffset (s.startOffset); // round 190w (FL dims the region before it)

        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName (tr ("Sampler settings - {0}", { juce::String::fromUTF8 (c->name.c_str()) })
                        + (c->zones.empty() ? juce::String() : tr (" (multi-sample: {0} zones)", { juce::String ((int) c->zones.size()) })));
    }

    // The knobs an automation drives show its value while the song plays, as in FL (issue #12); a knob held by the
    // mouse shows the hand's value. `force`: after refresh() put the Song's values back.
    void showAutomation (bool force)
    {
        const auto* c = current();
        if (c == nullptr || ref.index < 0)
            return;
        const auto& shown = engine.getAutomationDisplay();
        if (! force && shown.getGeneration() == shownGeneration)
            return;
        shownGeneration = shown.getGeneration();
        if (! force && automatedShown.empty() && ! shown.any())
            return;

        std::vector<juce::Slider*> now;
        const juce::ScopedValueSetter<bool> guard (updating, true);
        auto shownSettings = c->sampler; // (the region as the automation has it: LENGTH is relative to SMP START)
        for (const auto& [slider, param] : tweakTargets)
            if (const auto v = shown.valueOf ({ AutomationTargetKind::channelParam, ref.index, 0, param }))
                samplerparam::set (shownSettings, param, *v);
        for (const auto& [slider, param] : tweakTargets)
        {
            if (slider->isMouseButtonDown())
                continue;
            const auto v = shown.valueOf ({ AutomationTargetKind::channelParam, ref.index, 0, param });
            const auto was = std::find (automatedShown.begin(), automatedShown.end(), slider) != automatedShown.end();
            const auto value = [&] (const SamplerSettings& s) {
                return param == samplerparam::end ? (double) samplerpage::lengthOf (s) : (double) samplerparam::toNatural (param, samplerparam::get (s, param));
            };
            if (v)
            {
                slider->setValue (value (shownSettings), juce::dontSendNotification);
                now.push_back (slider);
            }
            else if (was && ! force)
                slider->setValue (value (c->sampler), juce::dontSendNotification);
        }
        automatedShown = std::move (now);
        wave.setRegion (shownSettings.regionStart, shownSettings.regionEnd);
    }

    void timerCallback() override
    {
        if (isShowing())
            showAutomation (false);
        followPreview();
    }

    // One control changed (`changed`): its law applied to the channel's sampler settings (core/SamplerPage.h), one edit
    void apply (juce::Component* changed)
    {
        const auto* c = current();
        if (updating || c == nullptr)
            return;
        auto s = c->sampler;
        const auto& old = c->sampler;
        bool cutOn = channelCutsItself (*c), cutChanged = false;
        auto value = [] (juce::Component* k) { return static_cast<juce::Slider*> (k)->getValue(); };
        auto on = [] (juce::Component* b) { return static_cast<juce::Button*> (b)->getToggleState(); };
        auto& p = s.precompute;

        if (changed == &keepOnDisk) s.keepOnDisk = on (changed);
        else if (changed == &resampleBox) s.resampleOnLoad = on (changed);
        else if (changed == &loadRegionsBox) s.loadRegions = on (changed);
        else if (changed == &loadMarkersBox) s.loadSliceMarkers = on (changed);
        else if (changed == &declick) s.declickMode = juce::jlimit (0, 5, declick.getSelectedId() - 1);
        else if (changed == &startOffsetKnob) s.startOffset = (float) value (changed);
        else if (changed == &loopStartKnob || changed == &loopEndKnob)
        {
            const auto keep = shiftHeld || (! shotDriven && juce::ModifierKeys::currentModifiers.isShiftDown()); // (FL: Shift moves the loop)
            if (changed == &loopStartKnob)
                samplerpage::setLoopStart (s, (std::int64_t) std::llround (value (changed)), shownFrames, shownFileLoop.first, shownFileLoop.second, keep);
            else
                samplerpage::setLoopEnd (s, (std::int64_t) std::llround (value (changed)), shownFrames, shownFileLoop.first, shownFileLoop.second, keep);
        }
        else if (changed == &useLoops)
        {
            s.useLoopPoints = on (changed);
            // FL's LoopBoxClick 0xf67f70: switching loop points on makes the channel cut itself (FUN_00feaaf0)
            if (s.useLoopPoints && ! cutOn && ref.index >= 0)
                cutOn = cutChanged = true;
        }
        else if (changed == &pingPong) p.pingPong = on (changed);
        else if (changed == &stretchPitchKnob) samplerpage::turnStretchPitch (s, juce::roundToInt (value (changed)));
        else if (changed == &mulKnob)
        {
            const auto sample = sampleOf (*c);
            const auto beats = sample != nullptr && sample->sampleRate > 0.0
                                   ? (double) sample->frames() / sample->sampleRate * engine.getSong().bpm / 60.0 : 0.0;
            samplerpage::setStretchMul (s, juce::roundToInt (value (changed)), beats);
        }
        else if (changed == &stretch) samplerpage::setStretchTime (s, (float) value (changed));
        else if (changed == &stretchMethod) samplerpage::selectMode (s, stretchMethod.getSelectedId() - 1);
        else if (changed == &removeDc) p.removeDc = on (changed);
        else if (changed == &normalize) p.normalize = on (changed);
        else if (changed == &reverse) s.reverse = on (changed);
        else if (changed == &polarity) p.reversePolarity = on (changed);
        else if (changed == &fadeStereo) p.fadeStereo = on (changed);
        else if (changed == &swapStereo) p.swapStereo = on (changed);
        else if (changed == &regionStart) samplerpage::setSmpStart (s, (float) value (changed));
        else if (changed == &lengthKnob) samplerpage::setLength (s, (float) value (changed));
        else if (changed == &fadeIn) p.fadeIn = juce::roundToInt (value (changed));
        else if (changed == &fadeOut) p.fadeOut = juce::roundToInt (value (changed));
        else if (changed == &crossfade)
        {
            const auto loopsBefore = s.useLoopPoints;
            samplerpage::turnCrossfade (s, juce::roundToInt (value (changed)));
            // (FL ticks "Use loop points" the way a click does, so the channel cuts itself as with the click)
            if (s.useLoopPoints && ! loopsBefore && ! cutOn && ref.index >= 0)
                cutOn = cutChanged = true;
        }
        else if (changed == &trim) p.trim = juce::roundToInt (value (changed));
        else if (changed == &boost) p.boost = juce::roundToInt (value (changed));
        else if (changed == &eq) p.eq = juce::roundToInt (value (changed));
        else if (changed == &ringMix) p.ringMix = juce::roundToInt (value (changed));
        else if (changed == &ringFreq) p.ringFreq = juce::roundToInt (value (changed));
        else if (changed == &legacyCut) p.cutoff = juce::roundToInt (value (changed));
        else if (changed == &legacyRes) p.resonance = juce::roundToInt (value (changed));
        else if (changed == &reverbAmount) p.reverb = juce::roundToInt (value (changed));
        else if (changed == &reverbType) p.reverbType = reverbType.getSelectedId() == 1 ? 0 : 1;
        else if (changed == &stereoDelay) p.stereoDelay = juce::roundToInt (value (changed));
        else if (changed == &pogo) p.pogo = juce::roundToInt (value (changed));
        else if (changed == &clip) p.clip = on (changed);
        else if (changed == &gain) s.gainDb = std::abs (value (changed)) < 0.05 ? 0.0f : (float) value (changed);
        else if (changed == &attack) s.attack = (float) value (changed);
        else if (changed == &decay) s.decay = (float) value (changed);
        else if (changed == &sustain) s.sustain = (float) value (changed);
        else if (changed == &release) s.release = (float) value (changed);
        else if (changed == &cutoff) s.cutoff = (float) value (changed);
        else if (changed == &resonance) s.resonance = (float) value (changed);
        else if (changed == &filterType) s.filterType = juce::jlimit (0, 3, filterType.getSelectedId() - 1);
        else if (changed == &cut)
        {
            cutOn = on (changed);
            cutChanged = cutOn != channelCutsItself (*c); // (issue #68: the rack menu's cut-itself too)
        }
        else if (changed == &porta) s.porta = on (changed);
        else
            return;

        // leaving Beats (or Resample): an imported clip's offline transposition moves to the Pitch knob, which the other
        // modes apply in real time, so the sample stays at its pitch
        s.moveBakedPitchToKnob();
        bool dropFlmVoice = false;
        {
            // an ADSR edited by hand replaces FL's imported volume envelope (and FL Studio Mobile's pad envelope, round
            // 190bs); so does an edited envelope or filter for FL's whole sampler voice (REA_rodada76)
            auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
            const auto adsr = moved (s.attack, old.attack) || moved (s.decay, old.decay) || moved (s.sustain, old.sustain) || moved (s.release, old.release);
            if (adsr)
            {
                s.flVolumeEnvelope.clear();
                s.flmPad.clear();
                dropFlmVoice = flmdw::isChannel (*c); // and FL Studio Mobile's DW Sampler voice (round 190de)
            }
            if (adsr || moved (s.cutoff, old.cutoff) || moved (s.resonance, old.resonance) || s.filterType != old.filterType)
                s.flChannel.clear();
        }
        if (cutChanged)
            s.cut = cutOn;
        if (s == old && ! cutChanged)
            return;

        const auto before = old;
        ref.edit (engine, [s, cutChanged, cutOn, dropFlmVoice, index = ref.index] (ChannelDef& channelDef) {
            channelDef.sampler = s;
            if (dropFlmVoice) // (the Sampler's own voice from now on: the module's values go, its zones stay)
                channelDef.modelParams.clear();
            if (cutChanged)
                setChannelCutsItself (channelDef, index, cutOn);
        });
        for (const auto& [slider, param] : tweakTargets)
            if (std::abs (samplerparam::get (before, param) - samplerparam::get (s, param)) > 1.0e-6f)
                engine.noteTweaked ({ AutomationTargetKind::channelParam, ref.index, 0, param });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (current() == nullptr) // channel removed
        {
            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::DocumentWindow> (w)] {
                    if (safe != nullptr)
                        safe->closeButtonPressed();
                });
            return;
        }

        refresh();
    }

    AudioEngine& engine;
    ChannelRef ref;
    bool updating = false, shiftHeld = false, shotDriven = false;
    float scale = 1.0f;
    int offsetX = 0;
    struct Group
    {
        juce::Rectangle<int> area;
        juce::String caption;
        int captionLeft;
    };
    std::vector<Group> groups;
    juce::Rectangle<int> loopGlyph;

    // FL's TIME knob menu (r168s c_f20): (none), Autodetect, Project tempo, 1 beat, 2 beats, 1 bar, 2 bars, 3 bars, 4 bars
    struct StretchMenu final : juce::MouseListener
    {
        explicit StretchMenu (SettingsPanel& p) : panel (p) {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                panel.showStretchMenu();
        }
        SettingsPanel& panel;
    } stretchMenu { *this };

    void showStretchMenu()
    {
        const auto* c = current();
        if (c == nullptr)
            return;
        const auto& song = engine.getSong();
        const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (c->samplePath.c_str()));
        const auto now = samplerpage::stretchTime (c->sampler);
        auto set = [this] (float beats) { ref.edit (engine, [beats] (ChannelDef& d) { samplerpage::setStretchTime (d.sampler, beats); }); };
        juce::PopupMenu m;
        m.addItem (tr ("(none)"), true, now <= 0.0f, [set] { set (0.0f); });
        m.addItem (tr ("Autodetect"), sample != nullptr, false, [set, sample, bpm = song.bpm] { set (tempodetect::fitBeatsOf (sample.get(), bpm)); });
        const auto atTempo = sample != nullptr && sample->sampleRate > 0.0 ? (float) ((double) sample->frames() / sample->sampleRate * song.bpm / 60.0) : 0.0f;
        m.addItem (tr ("Project tempo"), atTempo > 0.0f, false, [set, atTempo] { set (atTempo); });
        for (const auto preset : clipops::timePresetBeats (song.beatsPerBar))
        {
            const auto bars = song.beatsPerBar > 0 && preset >= (float) song.beatsPerBar ? preset / (float) song.beatsPerBar : 0.0f;
            const auto label = bars > 0.0f ? (bars == 1.0f ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars, 0) }))
                                           : (preset == 1.0f ? tr ("1 beat") : tr ("{0} beats", { juce::String (preset, 0) }));
            m.addItem (label, true, now == preset, [set, preset] { set (preset); });
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stretch));
    }

    // File row
    static constexpr int fileRowHeight = 34; // FL's File row (round 190df)
    juce::Label fileLabel, fileName;
    WheelIconButton loadButton { "load", Icon::open }, locateButton { "locate", Icon::browser }, removeButton { "remove", Icon::close };
    std::unique_ptr<juce::FileChooser> sampleChooser;
    // Content
    juce::ToggleButton keepOnDisk, resampleBox, loadRegionsBox, loadMarkersBox;
    juce::Label declickLabel;
    juce::ComboBox declick;
    // Playback
    MenuSlider startOffsetKnob, loopStartKnob, loopEndKnob;
    juce::Label startOffsetLabel, loopStartLabel, loopEndLabel;
    juce::ToggleButton useLoops, pingPong;
    std::pair<std::int64_t, std::int64_t> shownFileLoop { -1, -1 };
    std::int64_t shownFrames = 0;
    // Time stretching
    MenuSlider stretchPitchKnob, mulKnob, stretch;
    juce::Label stretchPitchLabel, mulLabel, stretchLabel, modeLabel;
    juce::ComboBox stretchMethod;
    // Precomputed effects (and FL's legacy panel)
    juce::ToggleButton removeDc, normalize, reverse, polarity, fadeStereo, swapStereo, clip;
    MenuSlider regionStart, lengthKnob, fadeIn, fadeOut, crossfade, trim;
    juce::Label regionStartLabel, lengthLabel, fadeInLabel, fadeOutLabel, crossfadeLabel, trimLabel;
    EffectsTargetSelect effectsTarget;
    MenuSlider boost, eq, ringMix, ringFreq, legacyCut, legacyRes, reverbAmount, stereoDelay, pogo;
    juce::Label boostLabel, eqLabel, ringMixLabel, ringFreqLabel, legacyCutLabel, legacyResLabel, reverbLabel, stereoDelayLabel, pogoLabel;
    juce::ComboBox reverbType;
    // the waveform
    ui::WaveformDisplay wave;
    juce::ScrollBar waveScroll { false };
    // LYNX extras
    MenuSlider gain, attack, decay, sustain, release, cutoff, resonance;
    juce::Label gainLabel, attackLabel, decayLabel, sustainLabel, releaseLabel, cutoffLabel, resonanceLabel, extrasLabel;
    juce::ComboBox filterType;
    juce::ToggleButton cut, porta;
    juce::TextButton modulationButton;

    std::vector<std::pair<MenuSlider*, int>> tweakTargets; // knob -> samplerparam
    std::vector<juce::Slider*> automatedShown;             // knobs showing their automation's value now
    std::uint32_t shownGeneration = 0;
    bool dropHighlight = false;
    int previewKey = -1, previewFrames = 0; // round 190w: the display's preview note (-1 = none)
    bool previewSeen = false;
};

// Built-in synth page: 3 oscillators, envelope and filter.
class SynthPanel final : public juce::Component,
                         private juce::ChangeListener
{
public:
    SynthPanel (AudioEngine& e, ChannelRef c)
        : engine (e),
          ref (std::move (c))
    {
        for (int o = 0; o < 3; ++o)
        {
            auto& row = rows[(std::size_t) o];
            row.title.setText (tr ("Osc {0}", { juce::String (o + 1) }), juce::dontSendNotification);
            row.wave.addItemList ({ tr ("Sine"), tr ("Triangle"), tr ("Saw"), tr ("Square"), tr ("Noise"), tr ("Harmonics") }, 1);
            row.wave.onChange = [this] { apply(); };
            knob (row.octave, -3.0, 3.0, 1.0, " oct");
            knob (row.semitones, -11.0, 11.0, 1.0, " st");
            knob (row.detune, -100.0, 100.0, 1.0, " ct");
            knob (row.pan, -1.0, 1.0, 0.01, "");
            knob (row.level, 0.0, 4.0, 0.01, "");
            row.level.setSkewFactorFromMidPoint (1.0); // above 1: gain (Harmor goes up to +12 dB)
            addAndMakeVisible (row.title);
            addAndMakeVisible (row.wave);
        }

        knob (attack, 0.0, 5.0, 0.001, " s");
        knob (decay, 0.0, 5.0, 0.001, " s");
        knob (sustain, 0.0, 1.0, 0.01, "");
        knob (release, 0.0, 5.0, 0.001, " s");
        knob (cutoff, 20.0, 20000.0, 1.0, " Hz");
        knob (resonance, 0.0, 1.0, 0.01, "");
        knob (fm, 0.0, 8.0, 0.01, "");
        fm.setSkewFactorFromMidPoint (1.0);
        fm.setTooltip (tr ("FM: osc 3 (which stops sounding) modulates the phase of osc 1 and 2; 0 = off"));
        knob (fmDecay, 0.0, 5.0, 0.001, " s");
        fmDecay.setSkewFactorFromMidPoint (0.5);
        fmDecay.setTooltip (tr ("Time for the FM index to decay (0 = constant); the brightness fades like an electric piano"));
        knob (keyTrack, -1.0, 2.0, 0.01, "");
        keyTrack.setTooltip (tr ("Cutoff follows the note: cutoff octaves per note octave, relative to C3 (130.8 Hz)"));
        knob (sweep, 0.0, 100.0, 0.01, "");
        sweep.setSkewFactorFromMidPoint (2.0);
        sweep.setTooltip (tr ("Kick pitch sweep: the note starts (1 + value) times higher and falls"));
        knob (sweepTime, 0.001, 2.0, 0.001, " s");
        sweepTime.setSkewFactorFromMidPoint (0.1);
        sweepTime.setTooltip (tr ("Sweep time constant"));
        for (auto* b : { &bandLow, &bandMid, &bandHigh })
        {
            knob (*b, 0.0, 2.0, 0.01, "");
            b->setTooltip (tr ("3-band tone after the filter (low < 178 Hz < mid < 1.18 kHz < high); 1 = unchanged"));
        }
        ring.setButtonText (tr ("Ring"));
        ring.setTooltip (tr ("With AM on: ring modulation, (osc 1 + 2) × (1 − level + osc 3), like Sytrus's RM"));
        ring.onClick = [this] { apply(); };
        addAndMakeVisible (ring);

        modulationButton.setButtonText (tr ("Modulation (envelopes and LFOs)..."));
        modulationButton.onClick = [this] { showModulationFor (*this, engine, ref.index); };
        addAndMakeVisible (modulationButton);

        for (auto* s : { &attack, &decay, &release })
            s->setSkewFactorFromMidPoint (0.5);
        cutoff.setSkewFactorFromMidPoint (1000.0);

        const char* names[] = { TR_NOOP ("Octave"), TR_NOOP ("Semitone"), TR_NOOP ("Fine"), TR_NOOP ("Pan"), TR_NOOP ("Level"), TR_NOOP ("Attack"), TR_NOOP ("Decay"), TR_NOOP ("Sustain"), TR_NOOP ("Release"), TR_NOOP ("Cutoff"), TR_NOOP ("Resonance"), TR_NOOP ("Porta time"), TR_NOOP ("FM"), TR_NOOP ("FM decay"),
                                TR_NOOP ("Keytrack"), TR_NOOP ("Sweep"), TR_NOOP ("Sweep time"), TR_NOOP ("Low"), TR_NOOP ("Mid"), TR_NOOP ("High") };

        for (int i = 0; i < (int) labels.size(); ++i)
        {
            labels[(std::size_t) i].setText (tr (names[i]), juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (theme::font (theme::type::caption));
            addAndMakeVisible (labels[(std::size_t) i]);
        }

        presetBox.setTextWhenNothingSelected (tr ("Presets..."));
        const auto presets = synthPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            presetBox.addItem (juce::String::fromUTF8 (presets[(std::size_t) i].first.c_str()), i + 1);
        presetBox.onChange = [this] {
            const auto presets = synthPresets();
            const auto index = presetBox.getSelectedId() - 1;

            if (juce::isPositiveAndBelow (index, (int) presets.size()) && current() != nullptr)
                ref.edit (engine, [s = presets[(std::size_t) index].second] (ChannelDef& channelDef) { channelDef.synth = s; });

            presetBox.setSelectedId (0, juce::dontSendNotification);
        };
        addAndMakeVisible (presetBox);

        flPresets.setButtonText (tr ("FL presets..."));
        flPresets.setTooltip (tr ("The 3x Osc channel presets that ship with FL Studio (Patches\\Channel presets\\3x Osc)"));
        flPresets.onClick = [this] { showFlPresets(); };
        addAndMakeVisible (flPresets);

        filterTypeBox.addItemList ({ tr ("Low-pass"), tr ("Band-pass"), tr ("High-pass"), tr ("Notch") }, 1);
        filterTypeBox.setTooltip (tr ("Filter type"));
        filterTypeBox.onChange = [this] { apply(); };
        addAndMakeVisible (filterTypeBox);

        am.setButtonText ("AM");
        am.setTooltip (tr ("Osc 3 modulates the amplitude of osc 1 and 2 (like 3x Osc's AM); its level is the depth"));
        am.onClick = [this] { apply(); };
        addAndMakeVisible (am);

        knob (porta, 0.0, 2.0, 0.005, " t");
        porta.setSkewFactorFromMidPoint (0.25);
        porta.setTooltip (tr ("Portamento time, in beats (applies to the Porta button and to notes marked porta)"));
        portaOn.setButtonText ("Porta");
        portaOn.setTooltip (tr ("Portamento: the new note glides from the previous one (notes marked porta invert this)"));
        portaOn.onClick = [this] { apply(); };
        addAndMakeVisible (portaOn);

        mono.setButtonText ("Mono");
        mono.setTooltip (tr ("Single voice: the new note takes over the one currently sounding (basses and leads)"));
        mono.onClick = [this] { apply(); };
        addAndMakeVisible (mono);

        refresh();
        engine.addChangeListener (this);
        setSize (720, 510);
    }

    ~SynthPanel() override { engine.removeChangeListener (this); }

    int getChannel() const noexcept { return ref.index; }

    void paint (juce::Graphics& g) override // round 170j: the art's canvas behind the controls
    {
        const skin::StyleScope styleScope (skin::resolve (skin::Style::themed, skin::Art::lynxSynth), skin::Art::lynxSynth);
        skin::canvas (g, getLocalBounds().toFloat(), skin::artAccent (skin::Art::lynxSynth));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto topRow = area.removeFromTop (24);
        presetBox.setBounds (topRow.removeFromLeft (200));
        mono.setBounds (topRow.removeFromLeft (90).withTrimmedLeft (16));
        am.setBounds (topRow.removeFromLeft (52));
        ring.setBounds (topRow.removeFromLeft (60));
        flPresets.setBounds (topRow.removeFromLeft (130).reduced (4, 0));
        portaOn.setBounds (topRow.removeFromLeft (70));
        filterTypeBox.setBounds (topRow.removeFromRight (140));
        area.removeFromTop (6);
        auto header = area.removeFromTop (16);
        header.removeFromLeft (150);

        for (int i = 0; i < oscKnobs; ++i)
            labels[(std::size_t) i].setBounds (header.removeFromLeft (88));

        for (auto& row : rows)
        {
            auto line = area.removeFromTop (64);
            row.title.setBounds (line.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            row.wave.setBounds (line.removeFromLeft (100).withSizeKeepingCentre (96, 24));
            for (auto* s : { &row.octave, &row.semitones, &row.detune, &row.pan, &row.level })
                s->setBounds (line.removeFromLeft (88));
        }

        area.removeFromTop (10);
        auto names = area.removeFromTop (16);
        const auto w = area.getWidth() / 9;

        for (int i = oscKnobs; i < oscKnobs + 9; ++i)
            labels[(std::size_t) i].setBounds (names.removeFromLeft (w));

        auto knobsRow = area.removeFromTop (80);
        for (auto* s : { &attack, &decay, &sustain, &release, &cutoff, &resonance, &porta, &fm, &fmDecay })
            s->setBounds (knobsRow.removeFromLeft (w));

        area.removeFromTop (6);
        auto names2 = area.removeFromTop (16);
        for (int i = oscKnobs + 9; i < (int) labels.size(); ++i)
            labels[(std::size_t) i].setBounds (names2.removeFromLeft (w));
        auto knobsRow2 = area.removeFromTop (80);
        for (auto* s : { &keyTrack, &sweep, &sweepTime, &bandLow, &bandMid, &bandHigh })
            s->setBounds (knobsRow2.removeFromLeft (w));

        area.removeFromTop (12);
        modulationButton.setBounds (area.removeFromTop (26).withSizeKeepingCentre (260, 26));
    }

private:
    struct Row
    {
        juce::Label title;
        juce::ComboBox wave;
        juce::Slider octave, semitones, detune, pan, level;
    };

    static constexpr int oscKnobs = 5; // oscillator knob labels come first

    juce::TextButton modulationButton;
    juce::ToggleButton mono, am, portaOn;
    juce::TextButton flPresets;

    // menu with the presets from the FL install for the synths LYNX converts: the 3x Osc channel .fst
    // files (by letter) and, per generator, the "Plugin presets" folders (Sytrus, Harmor, GMS...)
    void showFlPresets()
    {
        const auto roots = findFlStudioRoots();
        const auto patches = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches");
        auto sorted = [] (juce::Array<juce::File> files) {
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) {
                return a.getFileName().compareNatural (b.getFileName()) < 0;
            });
            return files;
        };

        juce::PopupMenu menu;

        // 3x Osc: ~170 presets grouped by first letter
        {
            juce::PopupMenu threeOsc, sub;
            juce::juce_wchar letter = 0;
            for (const auto& f : sorted (patches.getChildFile ("Channel presets/3x Osc").findChildFiles (juce::File::findFiles, false, "*.fst")))
            {
                const auto first = juce::CharacterFunctions::toUpperCase (f.getFileName()[0]);
                if (first != letter && sub.getNumItems() > 0)
                {
                    threeOsc.addSubMenu (juce::String::charToString (letter), sub);
                    sub = {};
                }
                letter = first;
                sub.addItem (f.getFileNameWithoutExtension(), [this, f] { loadFlPreset (f); });
            }
            if (sub.getNumItems() > 0)
                threeOsc.addSubMenu (juce::String::charToString (letter), sub);
            if (threeOsc.getNumItems() > 0)
                menu.addSubMenu ("3x Osc", threeOsc);
        }

        // a preset folder as a menu: subfolders become submenus
        std::function<juce::PopupMenu (const juce::File&, int)> folderMenu = [&] (const juce::File& folder, int depth) {
            juce::PopupMenu m;
            if (depth < 3)
                for (const auto& dir : sorted (folder.findChildFiles (juce::File::findDirectories, false)))
                    if (auto sub = folderMenu (dir, depth + 1); sub.getNumItems() > 0)
                        m.addSubMenu (dir.getFileName(), sub);
            for (const auto& f : sorted (folder.findChildFiles (juce::File::findFiles, false, "*.fst;*.gmsynth")))
                m.addItem (f.getFileNameWithoutExtension(), [this, f] { loadFlPreset (f); });
            return m;
        };

        const auto generators = patches.getChildFile ("Plugin presets/Generators");
        for (const auto* name : { "Sytrus", "Harmor", "GMS", "Harmless", "MiniSynth", "Fruity DX10", "Fruit Kick", "Ogun", "Sakura", "Kepler", "Kepler Exo",
                                 "PoiZone", "Toxic Biohazard", "BassDrum" })
            if (auto sub = folderMenu (generators.getChildFile (name), 0); sub.getNumItems() > 0)
                menu.addSubMenu (name, sub);

        if (menu.getNumItems() == 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("FL presets"),
                                                    tr ("FL presets not found (install FL Studio or check the Data\\Patches folder)."));
            return;
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&flPresets));
    }

    void loadFlPreset (const juce::File& file)
    {
        juce::MemoryBlock data;

        if (! file.loadFileAsData (data) || current() == nullptr)
            return;

        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());

        if (auto synth = synthFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize())))
            ref.edit (engine, [s = *synth] (ChannelDef& channelDef) { channelDef.synth = s; });
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("FL presets"),
                                                    tr ("This preset is not for a synth LYNX SHARE converts, or it could not be read."));
    }
    juce::Slider porta;
    juce::ComboBox filterTypeBox;

    void knob (juce::Slider& s, double lo, double hi, double step, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        // round 170j: the plugin kit's knobs in LYNX Synth's own art ("the LYNX lab"), or the forced plugin style
        s.getProperties().set (skin::sliderProperty, (juce::int64) skin::artAccent (skin::Art::lynxSynth).getARGB());
        s.getProperties().set (skin::styleProperty, skin::packStyle (skin::Style::themed, skin::Art::lynxSynth));
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] { apply(); };
        addAndMakeVisible (s);
    }

    const ChannelDef* current() const
    {
        return ref.get (engine);
    }

    void refresh()
    {
        const auto* c = current();

        if (c == nullptr)
            return;

        updating = true;
        const auto& s = c->synth;

        for (std::size_t o = 0; o < 3; ++o)
        {
            rows[o].wave.setSelectedId ((int) s.osc[o].wave + 1, juce::dontSendNotification);
            rows[o].octave.setValue (s.osc[o].octave, juce::dontSendNotification);
            rows[o].detune.setValue (s.osc[o].detune, juce::dontSendNotification);
            rows[o].semitones.setValue (s.osc[o].semitones, juce::dontSendNotification);
            rows[o].pan.setValue (s.osc[o].pan, juce::dontSendNotification);
            rows[o].level.setValue (s.osc[o].level, juce::dontSendNotification);
        }

        attack.setValue (s.attack, juce::dontSendNotification);
        decay.setValue (s.decay, juce::dontSendNotification);
        sustain.setValue (s.sustain, juce::dontSendNotification);
        release.setValue (s.release, juce::dontSendNotification);
        cutoff.setValue (s.cutoff, juce::dontSendNotification);
        resonance.setValue (s.resonance, juce::dontSendNotification);
        mono.setToggleState (s.mono, juce::dontSendNotification);
        am.setToggleState (s.am, juce::dontSendNotification);
        porta.setValue (s.portaBeats, juce::dontSendNotification);
        portaOn.setToggleState (s.porta, juce::dontSendNotification);
        filterTypeBox.setSelectedId (s.filterType + 1, juce::dontSendNotification);
        fm.setValue (s.fm, juce::dontSendNotification);
        fmDecay.setValue (s.fmDecay, juce::dontSendNotification);
        keyTrack.setValue (s.keyTrack, juce::dontSendNotification);
        sweep.setValue (s.sweep, juce::dontSendNotification);
        sweepTime.setValue (s.sweepTime, juce::dontSendNotification);
        bandLow.setValue (s.bandGain[0], juce::dontSendNotification);
        bandMid.setValue (s.bandGain[1], juce::dontSendNotification);
        bandHigh.setValue (s.bandGain[2], juce::dontSendNotification);
        ring.setToggleState (s.ring, juce::dontSendNotification);

        updating = false;

        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName (tr ("Synth settings - {0}", { juce::String::fromUTF8 (c->name.c_str()) }));
    }

    void apply()
    {
        if (updating || current() == nullptr)
            return;

        auto s = current()->synth; // keeps what this window does not show (envelopes and LFOs)

        for (std::size_t o = 0; o < 3; ++o)
            s.osc[o] = { (Waveform) juce::jlimit (0, 5, rows[o].wave.getSelectedId() - 1), (int) rows[o].octave.getValue(),
                         (float) rows[o].detune.getValue(), (float) rows[o].level.getValue(), (int) rows[o].semitones.getValue(),
                         (float) rows[o].pan.getValue() };

        s.attack = (float) attack.getValue();
        s.decay = (float) decay.getValue();
        s.sustain = (float) sustain.getValue();
        s.release = (float) release.getValue();
        s.cutoff = (float) cutoff.getValue();
        s.resonance = (float) resonance.getValue();
        s.mono = mono.getToggleState();
        s.am = am.getToggleState();
        s.portaBeats = (float) porta.getValue();
        s.porta = portaOn.getToggleState();
        s.filterType = juce::jlimit (0, 3, filterTypeBox.getSelectedId() - 1);
        s.fm = (float) fm.getValue();
        s.fmDecay = (float) fmDecay.getValue();
        if (s.fmDecay <= 0.0f)
            s.fmSustain = 1.0f;
        s.keyTrack = (float) keyTrack.getValue();
        s.sweep = (float) sweep.getValue();
        s.sweepTime = (float) sweepTime.getValue();
        s.bandGain = { (float) bandLow.getValue(), (float) bandMid.getValue(), (float) bandHigh.getValue() };
        s.ring = ring.getToggleState();

        if (s == current()->synth)
            return;

        ref.edit (engine, [s] (ChannelDef& channelDef) { channelDef.synth = s; });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (current() == nullptr)
        {
            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::DocumentWindow> (w)] {
                    if (safe != nullptr)
                        safe->closeButtonPressed();
                });
            return;
        }

        refresh();
    }

    AudioEngine& engine;
    ChannelRef ref;
    bool updating = false;
    std::array<Row, 3> rows;
    juce::Slider attack, decay, sustain, release, cutoff, resonance, fm, fmDecay, keyTrack, sweep, sweepTime, bandLow, bandMid, bandHigh;
    juce::ToggleButton ring;
    std::array<juce::Label, 20> labels;
    juce::ComboBox presetBox;
};

// --- FL's channel window frame (REA round 143b) --------------------------------------------------------------
// FL 2026 draws every channel's settings window the same way: a header strip with the channel's tabs on the left
// (the instrument / plugin page, the wrench "Miscellaneous functions" page) and ON, PAN, VOL and TRACK on the right
// [EXEC: FL 2026 build 5654, "Detailed settings" of a generator and of a sampler]. LYNX does the same around its own
// instrument pages, in its own style; the envelopes and LFOs of the synth and the sampler are the "Envelope /
// instrument" tab instead of a second window.

// A small rotary control with LYNX's control contract (drag, Ctrl = fine, wheel, Alt / middle-click / double-click
// reset, right-click menu with automation items when the spec has a target); the hint bar shows its name and value.
class FrameKnob final : public juce::Component,
                        public ui::HintSource,
                        private juce::Timer
{
public:
    FrameKnob (AudioEngine& e, std::function<ui::ValueSpec()> makeSpec, std::function<double()> get, std::function<void (double)> set,
               juce::String caption)
        : engine (e), specOf (std::move (makeSpec)), getter (std::move (get)), setter (std::move (set)), label (std::move (caption))
    {
        setRepaintsOnMouseActivity (true);
        if (specOf().target)
            startTimerHz (30); // follows its automation while the song plays (issue #12)
    }

    // the value drawn: the automation's while the song plays (pan / volume: the targets these knobs have), else the Song's
    double shownValue (const ui::ValueSpec& spec) const
    {
        if (spec.target && ! isMouseButtonDown())
            if (const auto v = engine.getAutomationDisplay().valueOf (*spec.target))
                return spec.target->kind == AutomationTargetKind::channelPan || spec.target->kind == AutomationTargetKind::mixerPan
                           ? (double) *v * 2.0 - 1.0
                       // the Levels adjustment (round 190dz): automation's 0..1 over the knob's own range
                       : spec.target->kind == AutomationTargetKind::channelAdjust ? spec.minimum + (double) *v * (spec.maximum - spec.minimum)
                                                                                  : (double) *v;
        return getter();
    }

    void timerCallback() override
    {
        if (! isShowing())
            return;
        if (const auto generation = engine.getAutomationDisplay().getGeneration(); generation != shownGeneration)
        {
            shownGeneration = generation;
            repaint();
        }
    }
    std::uint32_t shownGeneration = 0;

    ui::Hint hintAt (juce::Point<int>) override
    {
        return ui::valueHint (specOf(), getter(), doubleClicked ? tr ("[LMB] drag or [Wheel] changes  [2xLMB] show in the Mixer  [RMB] menu")
                                                                : ui::valueGestures());
    }

    // FL's number wheel (round 168s, issue #32): the value in a box over the caption instead of a knob; the whole
    // control is the hit area, a notch / 7.5 px of drag = one step (ui::digitWheel). Double-click: `doubleClicked`.
    bool digitBox = false;
    std::function<void()> doubleClicked;
    std::function<void (juce::PopupMenu&)> extraMenuItems;

    void paint (juce::Graphics& g) override
    {
        const auto spec = specOf();
        auto area = getLocalBounds().toFloat();
        const auto caption = area.removeFromBottom (13.0f);
        if (digitBox)
        {
            // FL's digiwheel (c_f14): a display about 22 px high, the value right-aligned before up / down marks; here dark,
            // red only while touched
            const auto box = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 2.0f, 48.0f), juce::jmin (22.0f, area.getHeight() - 4.0f));
            g.setColour (theme::drawDigitDisplay (g, box, isMouseOverOrDragging() || drag.isActive(), false, true));
            g.setFont (theme::font (13.0f, true));
            g.drawFittedText (spec.text (getter()), box.withTrimmedRight (theme::digitMarksWidth).reduced (3.0f, 0.0f).toNearestInt(),
                              juce::Justification::centredRight, 1, 0.6f);
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::caption));
            g.drawFittedText (theme::caps (label), caption.toNearestInt(), juce::Justification::centred, 1, 0.65f);
            return;
        }
        const auto valueArea = area.removeFromBottom (13.0f);
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto shown = shownValue (spec);
        const auto v = (float) juce::jlimit (0.0, 1.0, (shown - spec.minimum) / juce::jmax (1e-9, spec.maximum - spec.minimum));
        theme::drawKnob (g, juce::Rectangle<float> (size, size).withCentre (area.getCentre()), v, spec.minimum < 0.0,
                         isMouseOverOrDragging() ? theme::colour::accent : theme::colour::knobArc, isMouseOver());
        g.setFont (theme::font (theme::type::caption));
        g.setColour (theme::colour::textPrimary);
        // squeezed (down to 65 %) before an ellipsis: translated captions are often longer (round 165o)
        g.drawFittedText (spec.text (shown), valueArea.toNearestInt(), juce::Justification::centred, 1, 0.65f);
        g.setColour (theme::colour::textSecondary);
        g.drawFittedText (theme::caps (label), caption.toNearestInt(), juce::Justification::centred, 1, 0.65f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (! extraMenuItems)
            {
                ui::showValueMenu (*this, &engine, specOf(), getter(), setter);
                return;
            }
            const auto spec = specOf();
            juce::PopupMenu menu;
            menu.addSectionHeader (spec.name + ": " + spec.text (getter()));
            ui::addValueItems (menu, *this, &engine, spec, getter(), setter);
            menu.addSeparator();
            extraMenuItems (menu);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        if (ui::isResetClick (e))
        {
            setter (specOf().defaultValue);
            repaint();
            return;
        }
        drag.begin (e, getter());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag.isActive())
            return;
        const auto spec = specOf();
        setter (drag.update (e, spec));
        ui::showValueTip (*this, getLocalBounds(), spec.text (getter()));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        drag.end();
        ui::hideValueTip();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (doubleClicked)
        {
            doubleClicked();
            return;
        }
        setter (specOf().defaultValue);
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (const auto v = ui::wheelValue (getter(), e, wheel, specOf()))
        {
            setter (*v);
            ui::flashValueTip (*this, getLocalBounds(), specOf().text (getter()));
            repaint();
        }
    }

private:
    AudioEngine& engine;
    std::function<ui::ValueSpec()> specOf;
    std::function<double()> getter;
    std::function<void (double)> setter;
    juce::String label;
    ui::ValueDrag drag;
};

juce::String channelNameOf (const Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return "?";
    const auto& n = song.channels[(std::size_t) channel].name;
    return n.empty() ? tr ("Channel {0}", { juce::String (channel + 1) }) : juce::String::fromUTF8 (n.c_str());
}

// An integer spec shown as a number, 0 as "--" (FL's empty number boxes).
ui::ValueSpec countSpec (const juce::String& name, int maximum, int defaultValue, std::function<juce::String (int)> text = {})
{
    ui::ValueSpec spec;
    spec.name = name;
    spec.maximum = maximum;
    spec.defaultValue = defaultValue;
    spec.interval = 1.0;
    spec.pixelsForRange = juce::jlimit (60.0, 400.0, 5.0 * maximum);
    spec.toText = [text] (double v) {
        const auto i = juce::roundToInt (v);
        if (text)
            return text (i);
        return i == 0 ? juce::String ("--") : juce::String (i);
    };
    spec.fromText = [] (const juce::String& t) -> std::optional<double> {
        const auto digits = t.retainCharacters ("0123456789");
        return digits.isEmpty() ? std::optional<double> (0.0) : std::optional<double> (digits.getDoubleValue());
    };
    return spec;
}

// The wrench tab: FL's "Miscellaneous functions" with what LYNX's channels have (Levels, Polyphony, Time, Group,
// Arpeggiator).
// FL's Root note keyboard (Misc tab, REA round 149): every key 0…127 (C0…G10, C5 = 60), the channel's root marked;
// a click sets it. LYNX's own drawing.
class RootKeyboard final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onPick;
    // FL's key region ruler (round 164): a drag on the strip above the keys sets the notes the channel plays,
    // a double-click there gives it the whole keyboard back
    std::function<void (int, int)> onRegion;
    void setRoot (int key)
    {
        root = key;
        repaint();
    }
    void setRegion (int low, int high)
    {
        if (low != regionLow || high != regionHigh)
        {
            regionLow = low;
            regionHigh = high;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto w = whiteWidth();
        const auto top = (float) rulerHeight();
        const auto keysH = (float) getHeight() - top;
        if (onRegion)
        {
            const auto ruler = juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), top - 2.0f);
            g.setColour (colour::bg0);
            g.fillRect (ruler);
            const auto full = regionLow <= 0 && regionHigh >= 127;
            const auto x0 = keyLeft (regionLow), x1 = keyRight (regionHigh);
            g.setColour (full ? colour::bg5 : colour::accent);
            g.fillRect (juce::Rectangle<float> (x0, 1.0f, juce::jmax (2.0f, x1 - x0), ruler.getHeight() - 2.0f));
        }
        for (int k = 0, white = 0; k < 128; ++k)
            if (! isBlack (k))
            {
                const auto r = juce::Rectangle<float> ((float) (white++) * w, top, w - 1.0f, keysH);
                const auto outside = onRegion && (k < regionLow || k > regionHigh);
                g.setColour (k == root ? colour::accent : k == hover ? juce::Colour (0xffc8ccd4) : outside ? juce::Colour (0xff9a9ea8) : juce::Colour (0xffe6e8ec));
                g.fillRect (r);
                if (k % 12 == 0 && w * 7.0f >= 14.0f) // the octave's C named (FL's C5 = 60)
                {
                    g.setColour (k == root ? colour::onAccent : juce::Colour (0xff5a5f6a));
                    g.setFont (font (8.5f));
                    g.drawText ("C" + juce::String (k / 12), r.withTrimmedTop (r.getHeight() - 11.0f).expanded (6.0f, 0.0f).toNearestInt(),
                                juce::Justification::centred);
                }
            }
        for (int k = 0; k < 128; ++k)
            if (isBlack (k))
            {
                const auto x = (float) whitesBefore (k) * w - w * 0.32f;
                g.setColour (k == root ? colour::accent : k == hover ? colour::bg5 : colour::bg0);
                g.fillRect (juce::Rectangle<float> (x, top, w * 0.64f, keysH * 0.6f));
            }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto onRuler = onRegion && e.position.y < (float) rulerHeight();
        if (const auto k = keyAt (e.position); k != hover || onRuler != hoverRuler)
        {
            hover = k;
            hoverRuler = onRuler;
            if (onRuler)
                setTooltip (tr ("Key region: {0} to {1} (drag to set, double-click for the whole keyboard)",
                                { keyName (regionLow), keyName (regionHigh) }));
            else
                setTooltip (k >= 0 ? tr ("Root note: {0}", { keyName (k) }) : juce::String());
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragFrom = -1;
        if (onRegion && e.position.y < (float) rulerHeight())
        {
            dragFrom = keyAtX (e.position.x);
            if (dragFrom >= 0)
                onRegion (dragFrom, dragFrom);
            return;
        }
        if (const auto k = keyAt (e.position); k >= 0 && onPick)
            onPick (k);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFrom < 0 || ! onRegion)
            return;
        const auto k = keyAtX (e.position.x);
        if (k >= 0)
            onRegion (juce::jmin (dragFrom, k), juce::jmax (dragFrom, k));
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (onRegion && e.position.y < (float) rulerHeight())
            onRegion (0, 127);
    }

    static juce::String keyName (int key)
    {
        return juce::String (notenames::keyName (key)); // Settings › General › Note names (round 165f)
    }

private:
    static bool isBlack (int k) noexcept
    {
        const auto n = k % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
    static int whitesBefore (int k) noexcept
    {
        int count = 0;
        for (int i = 0; i < k; ++i)
            count += isBlack (i) ? 0 : 1;
        return count;
    }
    int rulerHeight() const noexcept { return onRegion ? 9 : 0; }
    float whiteWidth() const noexcept { return (float) getWidth() / 75.0f; } // 75 white keys in 0…127
    float keyLeft (int k) const noexcept
    {
        const auto w = whiteWidth();
        return isBlack (k) ? (float) whitesBefore (k) * w - w * 0.32f : (float) whitesBefore (k) * w;
    }
    float keyRight (int k) const noexcept
    {
        const auto w = whiteWidth();
        return isBlack (k) ? keyLeft (k) + w * 0.64f : keyLeft (k) + w;
    }
    // the key under x on the ruler (each key owns its own horizontal span; black keys win where they overlap)
    int keyAtX (float x) const noexcept
    {
        for (int k = 0; k < 128; ++k)
            if (isBlack (k) && x >= keyLeft (k) && x < keyRight (k))
                return k;
        const auto white = (int) std::floor (x / whiteWidth());
        for (int k = 0, n = 0; k < 128; ++k)
            if (! isBlack (k) && n++ == white)
                return k;
        return x < 0.0f ? 0 : 127;
    }
    int keyAt (juce::Point<float> p) const noexcept
    {
        const auto w = whiteWidth();
        const auto top = (float) rulerHeight();
        if (p.y < top)
            return keyAtX (p.x);
        if (p.y < top + ((float) getHeight() - top) * 0.6f)
            for (int k = 0; k < 128; ++k)
                if (isBlack (k))
                {
                    const auto x = (float) whitesBefore (k) * w - w * 0.32f;
                    if (p.x >= x && p.x < x + w * 0.64f)
                        return k;
                }
        const auto white = (int) std::floor (p.x / w);
        for (int k = 0, n = 0; k < 128; ++k)
            if (! isBlack (k) && n++ == white)
                return k;
        return -1;
    }

    int root = 60, hover = -1, dragFrom = -1;
    int regionLow = 0, regionHigh = 127;
    bool hoverRuler = false;
};

class MiscPanel final : public juce::Component,
                        private juce::ChangeListener
{
public:
    static constexpr int preferredWidth = 800, preferredHeight = 360;

    MiscPanel (AudioEngine& e, int c) : engine (e), channel (c)
    {
        const auto name = [this] { return channelNameOf (engine.getSong(), channel); };
        auto knob = [this] (std::function<ui::ValueSpec()> spec, std::function<double (const ChannelDef&)> get,
                            std::function<void (ChannelDef&, double)> set, const char* caption) {
            auto k = std::make_unique<FrameKnob> (
                engine, std::move (spec), [this, get] { return current() != nullptr ? get (*current()) : 0.0; },
                [this, set] (double v) {
                    if (current() == nullptr)
                        return;
                    engine.editSong ([ch = channel, set, v] (Song& s) {
                        if (ch < (int) s.channels.size())
                            set (s.channels[(std::size_t) ch], v);
                    });
                },
                tr (caption));
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
            return knobs.back().get();
        };

        // FL's "Levels adjustment" (round 190dz; TPluginForm OfsLevelsPanel, channel parameters 0x10 / 0x11): the offsets on
        // top of the channel's own Pan / Volume (those stay on the header), which Automate on an audio clip moves
        pan = knob ([this, name] {
                        auto s = ui::panSpec (name() + " - " + tr ("Panning offset"));
                        s.target = AutomationTarget { AutomationTargetKind::channelAdjust, channel, 0, leveladjust::pan };
                        return s;
                    },
                    [] (const ChannelDef& d) { return (double) d.misc.levels.pan; }, [] (ChannelDef& d, double v) { d.misc.levels.pan = (float) v; },
                    TR_NOOP ("Pan"));
        volume = knob ([this, name] {
                           auto s = ui::percentSpec (name() + " - " + tr ("Volume multiplier"), 1.0);
                           s.maximum = 2.0; // FL: 0…25600, 12800 = 100 % (FL's volume curve, up to +21.6 dB)
                           s.target = AutomationTarget { AutomationTargetKind::channelAdjust, channel, 0, leveladjust::volume };
                           return s;
                       },
                       [] (const ChannelDef& d) { return (double) d.misc.levels.volume; },
                       [] (ChannelDef& d, double v) { d.misc.levels.volume = (float) v; }, TR_NOOP ("Vol"));
        // Polyphony
        maxPoly = knob ([name] { return countSpec (name() + " - " + tr ("Maximum polyphony (-- = no limit)"), 64, 0); },
                        [] (const ChannelDef& d) { return (double) d.maxPolyphony; },
                        [] (ChannelDef& d, double v) { d.maxPolyphony = juce::jlimit (0, 64, juce::roundToInt (v)); }, TR_NOOP ("Max"));
        slide = knob (
            [name] {
                ui::ValueSpec s;
                s.name = name() + " - " + tr ("Portamento time");
                s.maximum = 2.0;
                s.defaultValue = 0.125;
                s.toText = [] (double v) { return tr ("{0} beats", { juce::String (v, 3) }); };
                s.fromText = [] (const juce::String& t) -> std::optional<double> { return t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
                return s;
            },
            [] (const ChannelDef& d) { return (double) channelPortamentoBeats (d); },
            [] (ChannelDef& d, double v) { setChannelPortamento (d, channelPortamento (d), (float) v); }, TR_NOOP ("Slide"));
        porta.setButtonText (tr ("Porta"));
        porta.setTooltip (tr ("Portamento: each note glides from the previous one's pitch (the Slide knob sets the time)"));
        porta.onClick = [this] { edit ([on = porta.getToggleState()] (ChannelDef& d) { setChannelPortamento (d, on, channelPortamentoBeats (d)); }); };
        mono.setButtonText (tr ("Mono"));
        mono.setTooltip (tr ("Mono: one voice; a note played over a held one glides to it (Slide time) without a new attack"));
        mono.onClick = [this] { edit ([on = mono.getToggleState()] (ChannelDef& d) { setChannelMono (d, on); }); };
        // Time
        swing = knob ([name] { return ui::percentSpec (name() + " - " + tr ("Swing mix"), 1.0); },
                      [] (const ChannelDef& d) { return (double) d.swingMix; }, [] (ChannelDef& d, double v) { d.swingMix = (float) v; },
                      TR_NOOP ("Swing"));
        // Levels adjustment: Mod X / Mod Y offsets (FL 0x13 / 0x14, ±256) on the channel filter's cutoff / resonance knobs'
        // travel (sampler, synth); the knobs themselves are on the Sampler / Synth pages
        auto offsetSpec = [name] (const char* what) {
            ui::ValueSpec s;
            s.name = name() + " - " + tr (what);
            s.minimum = -1.0;
            s.maximum = 1.0;
            s.defaultValue = 0.0;
            s.toText = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
            s.fromText = [] (const juce::String& t) -> std::optional<double> {
                const auto digits = t.retainCharacters ("-0123456789.,").replaceCharacter (',', '.');
                if (digits.isEmpty())
                    return std::nullopt;
                return digits.getDoubleValue() / 100.0;
            };
            return s;
        };
        modX = knob ([offsetSpec] { return offsetSpec (TR_NOOP ("Modulation X offset")); },
                     [] (const ChannelDef& d) { return (double) d.misc.levels.modX; }, [] (ChannelDef& d, double v) { d.misc.levels.modX = (float) v; },
                     TR_NOOP ("Mod X"));
        modY = knob ([offsetSpec] { return offsetSpec (TR_NOOP ("Modulation Y offset")); },
                     [] (const ChannelDef& d) { return (double) d.misc.levels.modY; }, [] (ChannelDef& d, double v) { d.misc.levels.modY = (float) v; },
                     TR_NOOP ("Mod Y"));
        // Time: Gate and Shift on FL's time-knob scale (REA round 146b)
        auto timeSpec = [name] (const char* what, int maximum, int defaultValue, bool offAtMax) {
            ui::ValueSpec s;
            s.name = name() + " - " + tr (what);
            s.maximum = maximum;
            s.defaultValue = defaultValue;
            s.interval = 1.0;
            s.pixelsForRange = 300.0;
            s.toText = [maximum, offAtMax] (double v) {
                const auto i = juce::roundToInt (v);
                if (offAtMax && i >= maximum)
                    return tr ("Off");
                // steps:ticks, as FL shows them (24 ticks per step)
                const auto ticks = juce::roundToInt (channelmisc::flTimeKnobBeats (i) * 96.0);
                return juce::String (ticks / 24) + ":" + juce::String (ticks % 24).paddedLeft ('0', 2);
            };
            return s;
        };
        timeGate = knob ([timeSpec] { return timeSpec (TR_NOOP ("Gate: long notes are cut to this length (Off = no limit)"), 1446, 1446, true); },
                         [] (const ChannelDef& d) { return d.misc.gateBeats > 0.0f ? (double) channelmisc::flTimeKnobValue (d.misc.gateBeats) : 1446.0; },
                         [] (ChannelDef& d, double v) {
                             const auto i = juce::roundToInt (v);
                             d.misc.gateBeats = i >= 1446 ? 0.0f : (float) std::max (1.0 / 96.0, channelmisc::flTimeKnobBeats (i));
                         },
                         TR_NOOP ("Gate"));
        timeShift = knob ([timeSpec] { return timeSpec (TR_NOOP ("Fine time shift: delays the notes, up to one step"), 1024, 0, false); },
                          [] (const ChannelDef& d) { return (double) channelmisc::flTimeKnobValue (d.misc.shiftBeats); },
                          [] (ChannelDef& d, double v) { d.misc.shiftBeats = (float) channelmisc::flTimeKnobBeats (juce::roundToInt (v)); },
                          TR_NOOP ("Shift"));
        fullPorta.setButtonText (tr ("Full porta"));
        fullPorta.setTooltip (tr ("Full porta: portamento notes ignore the Gate and play their whole length"));
        fullPorta.onClick = [this] { edit ([on = fullPorta.getToggleState()] (ChannelDef& d) { d.misc.fullPorta = on; }); };
        // Vol / Key tracking
        trackVol.setButtonText (tr ("Vol"));
        trackKey.setButtonText (trc ("@key tracking: Key"));
        for (auto* b : { &trackVol, &trackKey })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (146);
            b->onClick = [this] { repaint(); for (auto& k : knobs) k->repaint(); };
            addAndMakeVisible (b);
        }
        trackVol.setTooltip (tr ("Velocity tracking: the note's velocity moves its pan"));
        trackKey.setTooltip (tr ("Keyboard tracking: the note's key moves its pan"));
        trackKey.setToggleState (true, juce::dontSendNotification);
        auto tracking = [this] (ChannelDef& d) -> NoteTracking& { return trackKey.getToggleState() ? d.misc.key : d.misc.velocity; };
        auto trackingOf = [this] (const ChannelDef& d) -> const NoteTracking& { return trackKey.getToggleState() ? d.misc.key : d.misc.velocity; };
        trackPan = knob ([name] { return ui::panSpec (name() + " - " + tr ("Tracking: pan")); },
                         [trackingOf] (const ChannelDef& d) { return (double) trackingOf (d).pan; },
                         [tracking] (ChannelDef& d, double v) { tracking (d).pan = (float) v; }, TR_NOOP ("Pan"));
        trackMid = knob ([this, name] {
                             const auto key = trackKey.getToggleState();
                             auto s = countSpec (name() + " - " + tr ("Tracking: the middle value (no offset)"), key ? 131 : 128, key ? 60 : 100,
                                                 [key] (int v) { return key ? juce::MidiMessage::getMidiNoteName (120 - v, true, true, 5) : juce::String (v); }); // FL: C5 = 60
                             return s;
                         },
                         [trackingOf] (const ChannelDef& d) { return (double) trackingOf (d).mid; },
                         [tracking] (ChannelDef& d, double v) { tracking (d).mid = juce::roundToInt (v); }, TR_NOOP ("Mid"));
        // FL's tracking Mod X / Y (round 151): the notes' Mod X / Y follow the velocity / key like their pan
        trackModX = knob ([name] { return ui::panSpec (name() + " - " + tr ("Tracking: Mod X (the note's filter cutoff / a plugin's MOD X)")); },
                          [trackingOf] (const ChannelDef& d) { return (double) trackingOf (d).modX; },
                          [tracking] (ChannelDef& d, double v) { tracking (d).modX = (float) v; }, TR_NOOP ("Mod X"));
        trackModY = knob ([name] { return ui::panSpec (name() + " - " + tr ("Tracking: Mod Y (the note's filter resonance)")); },
                          [trackingOf] (const ChannelDef& d) { return (double) trackingOf (d).modY; },
                          [tracking] (ChannelDef& d, double v) { tracking (d).modY = (float) v; }, TR_NOOP ("Mod Y"));
        // Echo delay / fat mode
        echoFeed = knob ([name] { return ui::percentSpec (name() + " - " + tr ("Delay feedback (100% = the echoes as loud as the note)"), 0.0); },
                         [] (const ChannelDef& d) { return (double) d.misc.echo.feedback / 2.0; },
                         [] (ChannelDef& d, double v) { d.misc.echo.feedback = (float) (2.0 * v); }, TR_NOOP ("Feed"));
        echoPan = knob ([name] { return ui::panSpec (name() + " - " + tr ("Delay pan: each echo moves this much")); },
                        [] (const ChannelDef& d) { return (double) d.misc.echo.pan; }, [] (ChannelDef& d, double v) { d.misc.echo.pan = (float) v; },
                        TR_NOOP ("Pan"));
        echoModX = knob ([name] { return ui::panSpec (name() + " - " + tr ("Delay Mod X: each echo moves the note's Mod X this much")); },
                         [] (const ChannelDef& d) { return (double) d.misc.echo.modX; }, [] (ChannelDef& d, double v) { d.misc.echo.modX = (float) v; },
                         TR_NOOP ("Mod X"));
        echoModY = knob ([name] { return ui::panSpec (name() + " - " + tr ("Delay Mod Y: each echo moves the note's Mod Y this much")); },
                         [] (const ChannelDef& d) { return (double) d.misc.echo.modY; }, [] (ChannelDef& d, double v) { d.misc.echo.modY = (float) v; },
                         TR_NOOP ("Mod Y"));
        echoPitch = knob ([name] {
                              ui::ValueSpec s;
                              s.name = name() + " - " + tr ("Delay pitch: each echo moves this many cents");
                              s.minimum = -1200.0;
                              s.maximum = 1200.0;
                              s.interval = 1.0;
                              s.toText = [] (double v) { return tr ("{0} cents", { juce::String (juce::roundToInt (v)) }); };
                              return s;
                          },
                          [] (const ChannelDef& d) { return (double) d.misc.echo.pitch; }, [] (ChannelDef& d, double v) { d.misc.echo.pitch = (float) v; },
                          TR_NOOP ("Pitch"));
        echoTime = knob ([name] {
                             ui::ValueSpec s;
                             s.name = name() + " - " + tr ("Delay time (steps:ticks)");
                             s.minimum = 1.0;
                             s.maximum = 192.0 * 4.0;
                             s.defaultValue = 144.0;
                             s.interval = 1.0;
                             s.pixelsForRange = 400.0;
                             s.toText = [] (double v) { const auto t = juce::roundToInt (v); return juce::String (t / 48) + ":" + juce::String ((t % 48) / 2).paddedLeft ('0', 2); };
                             return s;
                         },
                         [] (const ChannelDef& d) { return (double) d.misc.echo.timeBeats * 192.0; },
                         [] (ChannelDef& d, double v) { d.misc.echo.timeBeats = (float) (juce::roundToInt (v) / 192.0); }, TR_NOOP ("Time"));
        echoCount = knob ([name] { return countSpec (name() + " - " + tr ("Delay number of echoes"), 10, 4); },
                          [] (const ChannelDef& d) { return (double) d.misc.echo.echoes; },
                          [] (ChannelDef& d, double v) { d.misc.echo.echoes = juce::jlimit (1, 10, juce::roundToInt (v)); }, TR_NOOP ("Echoes"));
        pingPong.setButtonText (tr ("Ping pong"));
        pingPong.setTooltip (tr ("Ping pong: the echoes' pan bounces off the left and right ends"));
        pingPong.onClick = [this] { edit ([on = pingPong.getToggleState()] (ChannelDef& d) { d.misc.echo.pingPong = on; }); };
        fatMode.setButtonText (tr ("Fat mode"));
        fatMode.setTooltip (tr ("Fat mode: a second, mirrored series of echoes (opposite pan and pitch steps), all at the feedback level"));
        fatMode.onClick = [this] { edit ([on = fatMode.getToggleState()] (ChannelDef& d) { d.misc.echo.fat = on; }); };
        // Root note row: main pitch
        mainPitch.setButtonText (tr ("Enable main pitch"));
        mainPitch.setTooltip (tr ("The channel follows the main pitch (the toolbar's master pitch)"));
        mainPitch.onClick = [this] { edit ([on = mainPitch.getToggleState()] (ChannelDef& d) { d.misc.mainPitch = on; }); };
        // FL's Add to key and Root note keyboard (round 149): a sampler's root moves its pitch, other instruments are
        // transposed by (C5 − root) with Add to key
        addToKey.setButtonText (tr ("Add to key"));
        addToKey.setTooltip (tr ("Add to key: the notes are played the root note's distance from C5 away (instruments that are not samplers)"));
        addToKey.onClick = [this] { edit ([on = addToKey.getToggleState()] (ChannelDef& d) { d.misc.addToKey = on; }); };
        // FL's Root note › Fine tune (round 151 [DATA: .flp event 0x8E, cents]): every note of the channel this many
        // cents away
        fineTune = knob ([name] {
                             ui::ValueSpec s;
                             s.name = name() + " - " + tr ("Fine tuning");
                             s.minimum = -100.0;
                             s.maximum = 100.0;
                             s.interval = 1.0;
                             s.toText = [] (double v) { return tr ("{0} cents", { (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) }); };
                             return s;
                         },
                         [] (const ChannelDef& d) { return (double) d.misc.fineTune; }, [] (ChannelDef& d, double v) { d.misc.fineTune = (float) v; },
                         TR_NOOP ("Fine tune"));
        rootReset.setButtonText (tr ("Reset"));
        rootReset.setTooltip (tr ("Reset the root note to C5"));
        rootReset.onClick = [this] { if (rootKeys.onPick) rootKeys.onPick (60); };
        addAndMakeVisible (rootReset);
        rootKeys.onPick = [this] (int key) {
            edit ([key] (ChannelDef& d) {
                if (d.instrument == InstrumentKind::sampler)
                {
                    // the sample plays at its own pitch on the root: the pitch moves by the root's change (within ±24)
                    const auto pitch = std::clamp (d.sampler.pitch + (float) (d.misc.rootNote - key), -24.0f, 24.0f);
                    const auto moved = (int) std::lround (pitch - d.sampler.pitch);
                    d.sampler.pitch = pitch;
                    d.misc.rootNote = std::clamp (d.misc.rootNote - moved, 0, 127);
                }
                else
                    d.misc.rootNote = key;
            });
        };
        rootKeys.onRegion = [this] (int low, int high) {
            edit ([low, high] (ChannelDef& d) {
                d.keyLow = juce::jlimit (0, 127, low);
                d.keyHigh = juce::jlimit (d.keyLow, 127, high);
            });
        };
        addAndMakeVisible (addToKey);
        addAndMakeVisible (rootKeys);
        // Arpeggiator: FL's Slide and Chord
        arpSlide.setButtonText (tr ("Slide"));
        arpSlide.setTooltip (tr ("Slide: the arpeggio glides from note to note (instruments that slide)"));
        arpSlide.onClick = [this] { edit ([on = arpSlide.getToggleState()] (ChannelDef& d) { d.arp.slide = on; }); };
        arpChord.addItem (tr ("Auto (sustain)"), 1);
        arpChord.addItem (tr ("None (octaves)"), 2);
        for (const auto& [chordName, intervals] : arpChords())
            arpChord.addItem (tr (chordName), arpChord.getNumItems() + 1);
        arpChord.setTooltip (tr ("Chord: Auto plays the held notes; a chord plays it on the lowest held note"));
        arpChord.onChange = [this] {
            const auto id = arpChord.getSelectedId();
            if (id <= 0)
                return;
            edit ([id] (ChannelDef& d) {
                if (id == 1)
                    d.arp.chord.clear();
                else if (id == 2)
                    d.arp.chord = { 0 };
                else if (id - 3 < (int) arpChords().size())
                    d.arp.chord = arpChords()[(std::size_t) (id - 3)].second;
            });
        };
        for (auto* b : { &fullPorta, &pingPong, &fatMode, &mainPitch, &arpSlide })
            addAndMakeVisible (b);
        addAndMakeVisible (arpChord);
        // Group
        cut = knob ([this, name] { return countSpec (name() + " - " + tr ("Cut: notes of this channel stop the channels cut by this group"), 
                                                     juce::jmax (99, current() != nullptr ? current()->cutGroup : 0), 0,
                                                     [this] (int g) { return juce::String (cutGroupText (g, channel)); }); },
                    [] (const ChannelDef& d) { return (double) d.cutGroup; }, [] (ChannelDef& d, double v) { d.cutGroup = juce::jmax (0, juce::roundToInt (v)); },
                    TR_NOOP ("Cut group"));
        cutBy = knob ([this, name] { return countSpec (name() + " - " + tr ("By: this channel is cut by the channels of this group"),
                                                       juce::jmax (99, current() != nullptr ? current()->cutBy : 0), 0,
                                                       [this] (int g) { return juce::String (cutGroupText (g, channel)); }); },
                      [] (const ChannelDef& d) { return (double) d.cutBy; }, [] (ChannelDef& d, double v) { d.cutBy = juce::jmax (0, juce::roundToInt (v)); },
                      TR_NOOP ("Cut by"));
        cutSelf.setButtonText (tr ("Cut self"));
        cutSelf.setClickingTogglesState (true);
        cutSelf.setTooltip (tr ("Cut self: a new note of this channel stops its own sounding note (hi-hats, 808s)"));
        cutSelf.onClick = [this] { edit ([on = cutSelf.getToggleState(), ch = channel] (ChannelDef& d) { setChannelCutsItself (d, ch, on); }); };
        // Arpeggiator
        arpDirection.addItemList ({ tr ("Off"), tr ("Up"), tr ("Down"), tr ("Up and down"), tr ("Random"), tr ("Up and down (sticky)") }, 1);
        arpDirection.setTooltip (tr ("Arpeggiator direction (Off = the notes play as written)"));
        arpDirection.onChange = [this] { edit ([d = arpDirection.getSelectedId() - 1] (ChannelDef& c) { c.arp.direction = juce::jlimit (0, 5, d); }); };
        for (const auto& [text, beats] : arpSpeeds())
            arpTime.addItem (tr (text), arpTime.getNumItems() + 1);
        arpTime.setTooltip (tr ("Arpeggiator time: the length of each step"));
        arpTime.onChange = [this] {
            const auto i = arpTime.getSelectedId() - 1;
            if (i >= 0 && i < (int) arpSpeeds().size())
                edit ([b = arpSpeeds()[(std::size_t) i].second] (ChannelDef& c) { c.arp.stepBeats = b; });
        };
        arpRange = knob ([name] { return countSpec (name() + " - " + tr ("Arpeggiator range (octaves)"), 4, 1); },
                         [] (const ChannelDef& d) { return (double) d.arp.range; },
                         [] (ChannelDef& d, double v) { d.arp.range = juce::jlimit (1, 4, juce::roundToInt (v)); }, TR_NOOP ("Range"));
        arpGate = knob ([name] {
                            auto s = ui::percentSpec (name() + " - " + tr ("Arpeggio gate (past 100% the next note waits)"), 0.5);
                            s.minimum = 0.05;
                            s.maximum = 128.0 / 48.0;
                            return s;
                        },
                        [] (const ChannelDef& d) { return (double) d.arp.gate; },
                        [] (ChannelDef& d, double v) { d.arp.gate = juce::jlimit (0.05f, 128.0f / 48.0f, (float) v); }, TR_NOOP ("Gate"));
        // FL's Repeat 0 plays the whole chord at once on each step (round 149)
        arpRepeat = knob ([name] { return countSpec (name() + " - " + tr ("Arpeggiator repeat: times the arpeggio plays in an octave before the next one (0: the chord at once)"), 16, 1,
                                                     [] (int i) { return i == 0 ? tr ("Chord") : juce::String (i); }); },
                          [] (const ChannelDef& d) { return (double) d.arp.octaveRepeat; },
                          [] (ChannelDef& d, double v) { d.arp.octaveRepeat = juce::jlimit (0, 16, juce::roundToInt (v)); }, TR_NOOP ("Repeat"));

        for (auto* b : { &porta, &mono })
            addAndMakeVisible (b);
        addAndMakeVisible (cutSelf);
        addAndMakeVisible (arpDirection);
        addAndMakeVisible (arpTime);
        refresh();
        engine.addChangeListener (this);
        setSize (preferredWidth, preferredHeight);
    }

    ~MiscPanel() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        for (const auto& [area, title] : sections)
        {
            g.setColour (theme::colour::bg3);
            g.fillRoundedRectangle (area.toFloat(), 4.0f);
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::caption, true));
            auto text = theme::caps (title);
            if (&title == &sections[(std::size_t) juce::jmax (0, rootSection)].second && rootSection >= 0 && current() != nullptr)
                text << ": " << RootKeyboard::keyName (current()->misc.rootNote); // FL's "Root note: C5"
            g.drawText (text, area.withHeight (20).reduced (8, 0), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        sections.clear();
        auto area = getLocalBounds().reduced (8);
        auto section = [this] (juce::Rectangle<int> r, const juce::String& title) {
            sections.push_back ({ r, title });
            return r.withTrimmedTop (20).reduced (6, 4);
        };
        constexpr int knobW = 50, knobH = 64;
        auto row = [] (juce::Rectangle<int>& r, std::initializer_list<FrameKnob*> ks) {
            auto line = r.removeFromTop (knobH);
            for (auto* k : ks)
                k->setBounds (line.removeFromLeft (knobW));
        };
        auto top = area.removeFromTop (118);
        {
            auto levels = section (top.removeFromLeft (4 * knobW + 12), tr ("Levels adjustment"));
            row (levels, { pan, volume, modX, modY });
        }
        top.removeFromLeft (6);
        {
            auto poly = section (top.removeFromLeft (2 * knobW + 30), tr ("Polyphony"));
            row (poly, { maxPoly, slide });
            auto toggles = poly.removeFromTop (22);
            porta.setBounds (toggles.removeFromLeft (64));
            mono.setBounds (toggles);
        }
        top.removeFromLeft (6);
        {
            auto time = section (top.removeFromLeft (3 * knobW + 12), tr ("Time"));
            row (time, { timeGate, timeShift, swing });
            fullPorta.setBounds (time.removeFromTop (22));
        }
        top.removeFromLeft (6);
        {
            auto track = section (top, tr ("Tracking"));
            auto tabs = track.removeFromTop (0);
            juce::ignoreUnused (tabs);
            const auto title = sections.back().first;
            trackVol.setBounds (title.getRight() - 84, title.getY() + 2, 38, 16);
            trackKey.setBounds (title.getRight() - 44, title.getY() + 2, 38, 16);
            row (track, { trackPan, trackModX, trackModY, trackMid });
        }
        area.removeFromTop (6);
        auto bottom = area.removeFromTop (130);
        {
            auto group = section (bottom.removeFromLeft (2 * knobW + 16), tr ("Group"));
            row (group, { cut, cutBy });
            cutSelf.setBounds (group.removeFromTop (24).reduced (4, 0));
        }
        bottom.removeFromLeft (6);
        {
            auto arp = section (bottom.removeFromLeft (330), tr ("Arpeggiator"));
            auto boxes = arp.removeFromLeft (118);
            arpDirection.setBounds (boxes.removeFromTop (22));
            boxes.removeFromTop (4);
            arpTime.setBounds (boxes.removeFromTop (22));
            boxes.removeFromTop (4);
            arpChord.setBounds (boxes.removeFromTop (22));
            boxes.removeFromTop (4);
            arpSlide.setBounds (boxes.removeFromTop (22));
            arp.removeFromLeft (6);
            row (arp, { arpRange, arpGate, arpRepeat });
        }
        bottom.removeFromLeft (6);
        {
            auto echo = section (bottom, tr ("Echo delay / fat mode"));
            {
                // seven knobs (round 151 added Mod X / Y): narrower when the window is
                auto line = echo.removeFromTop (knobH);
                const auto w = juce::jmin (knobW, line.getWidth() / 7);
                for (auto* k : { echoFeed, echoPan, echoModX, echoModY, echoPitch, echoTime, echoCount })
                    k->setBounds (line.removeFromLeft (w));
            }
            auto toggles = echo.removeFromTop (22);
            pingPong.setBounds (toggles.removeFromLeft (100));
            fatMode.setBounds (toggles.removeFromLeft (100));
        }
        area.removeFromTop (6);
        {
            auto rootRow = section (area.removeFromTop (74), tr ("Root note"));
            rootSection = (int) sections.size() - 1;
            const auto resetWidth = theme::buttonWidth (rootReset, 50, 96); // long "Reset" translations
            auto toggles = rootRow.removeFromLeft (100 + resetWidth);
            mainPitch.setBounds (toggles.removeFromTop (22));
            auto line = toggles.removeFromTop (22);
            rootReset.setBounds (line.removeFromRight (resetWidth).reduced (0, 1));
            addToKey.setBounds (line);
            auto fine = rootRow.removeFromLeft (58);
            row (fine, { fineTune });
            rootKeys.setBounds (rootRow.reduced (4, 2));
        }
    }

private:
    // LYNX's chord choices for the arpeggiator (the piano roll's chord stamps)
    static const std::vector<std::pair<const char*, std::vector<int>>>& arpChords()
    {
        static const std::vector<std::pair<const char*, std::vector<int>>> chords {
            { TR_NOOP ("Major"), { 0, 4, 7 } }, { TR_NOOP ("Minor"), { 0, 3, 7 } }, { "Dim", { 0, 3, 6 } }, { "Aug", { 0, 4, 8 } },
            { "Sus2", { 0, 2, 7 } }, { "Sus4", { 0, 5, 7 } }, { "7", { 0, 4, 7, 10 } }, { "Maj7", { 0, 4, 7, 11 } },
            { "m7", { 0, 3, 7, 10 } }, { "m9", { 0, 3, 7, 10, 14 } }, { TR_NOOP ("Octave"), { 0, 12 } },
        };
        return chords;
    }

    static const std::vector<std::pair<const char*, float>>& arpSpeeds()
    {
        static const std::vector<std::pair<const char*, float>> speeds { { "1/4", 1.0f },  { "1/8", 0.5f },
                                                                         { TR_NOOP ("1/8 triplet"), 1.0f / 3.0f }, { "1/16", 0.25f },
                                                                         { TR_NOOP ("1/16 triplet"), 1.0f / 6.0f }, { "1/32", 0.125f } };
        return speeds;
    }

    const ChannelDef* current() const
    {
        const auto& channels = engine.getSong().channels;
        return channel >= 0 && channel < (int) channels.size() ? &channels[(std::size_t) channel] : nullptr;
    }

    void edit (std::function<void (ChannelDef&)> change)
    {
        if (updating || current() == nullptr)
            return;
        engine.editSong ([ch = channel, change] (Song& s) {
            if (ch < (int) s.channels.size())
                change (s.channels[(std::size_t) ch]);
        });
    }

    void refresh()
    {
        const auto* c = current();
        if (c == nullptr)
            return;
        updating = true;
        porta.setToggleState (channelPortamento (*c), juce::dontSendNotification);
        porta.setEnabled (channelHasPortamento (*c));
        slide->setEnabled (channelHasPortamento (*c));
        mono.setToggleState (channelMono (*c), juce::dontSendNotification);
        mono.setEnabled (channelHasMono (*c));
        cutSelf.setToggleState (channelCutsItself (*c), juce::dontSendNotification);
        arpDirection.setSelectedId (juce::jlimit (0, 5, c->arp.direction) + 1, juce::dontSendNotification);
        fullPorta.setToggleState (c->misc.fullPorta, juce::dontSendNotification);
        pingPong.setToggleState (c->misc.echo.pingPong, juce::dontSendNotification);
        fatMode.setToggleState (c->misc.echo.fat, juce::dontSendNotification);
        mainPitch.setToggleState (c->misc.mainPitch, juce::dontSendNotification);
        addToKey.setToggleState (c->misc.addToKey, juce::dontSendNotification);
        addToKey.setEnabled (c->instrument != InstrumentKind::sampler);
        rootKeys.setRoot (c->misc.rootNote);
        rootKeys.setRegion (c->keyLow, c->keyHigh);
        arpSlide.setToggleState (c->arp.slide, juce::dontSendNotification);
        const auto filter = c->instrument == InstrumentKind::synth || c->instrument == InstrumentKind::sampler;
        modX->setEnabled (filter);
        modY->setEnabled (filter);
        {
            auto chordId = c->arp.chord.empty() ? 1 : (c->arp.chord == std::vector<int> { 0 } ? 2 : 0);
            for (int i = 0; chordId == 0 && i < (int) arpChords().size(); ++i)
                if (arpChords()[(std::size_t) i].second == c->arp.chord)
                    chordId = i + 3;
            arpChord.setSelectedId (chordId, juce::dontSendNotification);
            if (chordId == 0)
                arpChord.setText (tr ("Chord from FL"), juce::dontSendNotification);
        }
        int speed = 0;
        for (int i = 0; i < (int) arpSpeeds().size(); ++i)
            if (std::abs (arpSpeeds()[(std::size_t) i].second - c->arp.stepBeats) < 1e-4f)
                speed = i + 1;
        arpTime.setSelectedId (speed, juce::dontSendNotification);
        if (speed == 0)
            arpTime.setText (tr ("{0} beats", { juce::String (c->arp.stepBeats, 3) }), juce::dontSendNotification);
        updating = false;
        repaint();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    AudioEngine& engine;
    int channel;
    bool updating = false;
    std::vector<std::unique_ptr<FrameKnob>> knobs;
    FrameKnob *pan = nullptr, *volume = nullptr, *maxPoly = nullptr, *slide = nullptr, *swing = nullptr, *cut = nullptr, *cutBy = nullptr,
              *arpRange = nullptr, *arpGate = nullptr, *arpRepeat = nullptr, *modX = nullptr, *modY = nullptr, *timeGate = nullptr,
              *timeShift = nullptr, *trackPan = nullptr, *trackMid = nullptr, *echoFeed = nullptr, *echoPan = nullptr, *echoPitch = nullptr,
              *echoTime = nullptr, *echoCount = nullptr, *trackModX = nullptr, *trackModY = nullptr, *echoModX = nullptr, *echoModY = nullptr,
              *fineTune = nullptr;
    juce::ToggleButton porta, mono, fullPorta, pingPong, fatMode, mainPitch, arpSlide, addToKey;
    RootKeyboard rootKeys;
    juce::TextButton rootReset;
    int rootSection = -1;
    juce::TextButton trackVol, trackKey;
    juce::ComboBox arpChord;
    juce::TextButton cutSelf;
    juce::ComboBox arpDirection, arpTime;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> sections;
};

// FL's preset arrows of a generator's window (round 167o, issue #24; FL's TPluginForm NextPrevPreset, FLEngine
// 0x12b6c70): ◀ name ▶ over the .fst presets of the plugin's folder (FL's factory folder and the user's, subfolders
// too) in FL's order; no preset yet: ▶ the first, ◀ the last; both wrap. The step loads the preset into the channel
// (one undo step), names the channel after it and says "n / N" in the hint bar, as FL. A click on the name lists them.
class ChannelPresetBar final : public juce::Component
{
public:
    // after a preset is loaded (the channel's old name, the preset's): the window refreshes its page and title
    std::function<void (const juce::String&, const juce::String&)> onLoaded;

    ChannelPresetBar (AudioEngine& e, int c) : engine (e), channel (c)
    {
        for (auto* b : { &prev, &next })
        {
            b->setTooltip (b == &prev ? tr ("Previous preset") : tr ("Next preset"));
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        ui::setHint (*this, tr ("Presets: [LMB] the list, the arrows load the previous / next one"));
    }

    // Whether this channel's instrument has an FL preset folder (the bar is shown only then)
    static bool wanted (const AudioEngine& engine, int channel)
    {
        const auto& song = engine.getSong();
        if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
            return false;
        const auto kind = song.channels[(std::size_t) channel].instrument;
        return ! natives::presetFolderOf (kind).empty() || banks::hasBank (kind);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        prev.setBounds (r.removeFromLeft (24));
        next.setBounds (r.removeFromRight (24));
        nameArea = r.reduced (4, 0);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg2);
        g.fillRoundedRectangle (nameArea.toFloat(), theme::radius::sm);
        g.setColour (current >= 0 ? theme::colour::textPrimary : theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::body));
        g.drawText (current >= 0 ? currentName : tr ("Presets"), nameArea.reduced (6, 0), juce::Justification::centredLeft, true);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (nameArea.contains (e.getPosition()) && ! e.mouseWasDraggedSinceMouseDown())
            showMenu();
    }

private:
    const ChannelDef* def() const
    {
        const auto& song = engine.getSong();
        return juce::isPositiveAndBelow (channel, (int) song.channels.size()) ? &song.channels[(std::size_t) channel] : nullptr;
    }

    const std::vector<presetcycle::Preset>& list()
    {
        const auto* d = def();
        const auto kind = d != nullptr ? d->instrument : InstrumentKind::synth;
        if (! loaded || kind != listedKind)
        {
            const auto folder = natives::presetFolderOf (kind);
            presets = folder.empty() ? std::vector<presetcycle::Preset>()
                                     : presetcycle::listPresets (presetcycle::pluginPresetFolders (flStudioAccess().roots, true, folder));
            // LYNX's own bank after FL's presets (round 168n): the only presets without FL Studio
            const auto& bank = banks::bankOf (kind);
            for (int i = 0; i < (int) bank.size(); ++i)
                presets.push_back ({ banks::pathOf (i), banks::relativeOf (bank[(std::size_t) i]), bank[(std::size_t) i].name });
            listedKind = kind;
            loaded = true;
            current = d != nullptr ? presetcycle::indexOfName (presets, d->name) : -1; // (a channel named after its preset)
            currentName = current >= 0 ? juce::String::fromUTF8 (presets[(std::size_t) current].name.c_str()) : juce::String();
        }
        return presets;
    }

    void step (int delta)
    {
        const auto& all = list();
        const auto count = (int) all.size();
        // FL's next index; a preset LYNX can't read (an unknown or damaged state) is passed over in the same
        // direction, so the arrows keep going
        auto at = presetcycle::step (current, delta, count);
        for (int tries = 0; at >= 0 && tries < count; ++tries)
        {
            if (load (at, tries == count - 1))
                return;
            at = presetcycle::step (at, delta, count);
        }
    }

    bool load (int index, bool reportFailure = true)
    {
        const auto& all = list();
        const auto* d = def();
        if (d == nullptr || ! juce::isPositiveAndBelow (index, (int) all.size()))
            return false;
        const auto& preset = all[(std::size_t) index];
        const auto bankIndex = banks::indexOfPath (preset.path);
        auto changed = bankIndex >= 0 ? banks::withPreset (*d, bankIndex) : natives::withPreset (*d, preset.path, preset.name, flStudioAccess());
        if (! changed)
        {
            if (reportFailure)
                ui::postHint ({ tr ("This preset could not be read: {0}", { juce::String::fromUTF8 (preset.name.c_str()) }), {} }, 2500);
            current = index; // (the next step goes on from it)
            return false;
        }
        const auto kind = d->instrument;
        const auto oldName = juce::String::fromUTF8 (d->name.c_str());
        engine.editSong ([ch = channel, kind, c = std::move (*changed)] (Song& s) {
            if (ch < (int) s.channels.size() && s.channels[(std::size_t) ch].instrument == kind)
                s.channels[(std::size_t) ch] = c;
        });
        if (onLoaded != nullptr)
            onLoaded (oldName, juce::String::fromUTF8 (preset.name.c_str()));
        current = index;
        currentName = juce::String::fromUTF8 (preset.name.c_str());
        ui::postHint ({ currentName, juce::String (presetcycle::positionText (index, (int) all.size())) }, 2500);
        repaint();
        return true;
    }

    void showMenu()
    {
        const auto& all = list();
        juce::PopupMenu menu;
        std::map<std::string, juce::PopupMenu> folders;
        std::vector<std::string> order;
        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto& rel = all[(std::size_t) i].relative;
            const auto slash = rel.find_last_of ('/');
            const auto folder = slash == std::string::npos ? std::string() : rel.substr (0, slash);
            if (folders.find (folder) == folders.end())
                order.push_back (folder);
            folders[folder].addItem (juce::String::fromUTF8 (all[(std::size_t) i].name.c_str()), true, i == current, [this, i] { load (i); });
        }
        for (const auto& folder : order)
            if (folder.empty())
                for (juce::PopupMenu::MenuItemIterator it (folders[folder]); it.next();)
                    menu.addItem (it.getItem());
            else
                menu.addSubMenu (juce::String::fromUTF8 (folder.c_str()), folders[folder]);
        if (menu.getNumItems() == 0)
            menu.addItem (tr ("No presets found (they come with FL Studio)"), false, false, [] {});
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    AudioEngine& engine;
    int channel;
    juce::TextButton prev { juce::String::fromUTF8 ("\xe2\x97\x80") }, next { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    juce::Rectangle<int> nameArea;
    std::vector<presetcycle::Preset> presets;
    InstrumentKind listedKind = InstrumentKind::synth;
    bool loaded = false;
    int current = -1;
    juce::String currentName;
};

// The frame: header strip (tabs on the left; ON, PAN, VOL, TRACK on the right) over the page of the selected tab.
class ChannelFrame final : public juce::Component,
                           private juce::ChangeListener
{
public:
    static constexpr int headerHeight = 62;

    // c.index < 0 (a Patcher node, round 164f): the instrument's pages alone, without the channel's header and Misc tab
    ChannelFrame (AudioEngine& e, ChannelRef c, std::unique_ptr<juce::Component> instrumentPage, std::unique_ptr<juce::Component> envelopePage)
        : engine (e), channel (c.index), node (c.index < 0)
    {
        auto addPage = [this] (std::unique_ptr<juce::Component> page, const juce::String& title, const juce::String& tip) {
            auto button = std::make_unique<juce::TextButton> (title);
            button->setTooltip (tip);
            button->setClickingTogglesState (true);
            button->setRadioGroupId (1);
            const auto index = (int) pages.size();
            button->onClick = [this, index] { showTab (index); };
            addAndMakeVisible (*button);
            addChildComponent (*page);
            sizes.push_back ({ juce::jmax (1, page->getWidth()), juce::jmax (1, page->getHeight()) });
            tabs.push_back (std::move (button));
            pages.push_back (std::move (page));
        };
        instrumentPageFirst = instrumentPage != nullptr;
        if (instrumentPage != nullptr) // (none: a channel whose instrument has its own window, e.g. a hosted plugin)
            addPage (std::move (instrumentPage), tr ("Plugin"), tr ("The instrument's own settings"));
        if (envelopePage != nullptr)
            addPage (std::move (envelopePage), tr ("Envelope / instrument"), tr ("Envelope / instrument settings: the filter and pitch envelopes and the LFOs"));
        if (node)
        {
            int width = 1, height = 1;
            for (auto& p : pages)
            {
                width = juce::jmax (width, p->getWidth());
                height = juce::jmax (height, p->getHeight());
            }
            setSize (width, headerHeight + height);
            showTab (0);
            return;
        }
        addPage (std::make_unique<MiscPanel> (engine, channel), tr ("Misc"), tr ("Miscellaneous functions: levels, polyphony, time, tracking, cut groups, arpeggiator, echo delay"));

        on.setButtonText (tr ("On"));
        on.setTooltip (tr ("Channel on / off (mute)"));
        on.onClick = [this] {
            const auto muted = ! on.getToggleState();
            engine.editSong ([ch = channel, muted] (Song& s) {
                if (ch < (int) s.channels.size())
                    s.channels[(std::size_t) ch].muted = muted;
            });
        };
        addAndMakeVisible (on);
        if (ChannelPresetBar::wanted (engine, channel))
        {
            presetBar = std::make_unique<ChannelPresetBar> (engine, channel);
            presetBar->onLoaded = [this, hasPage = instrumentPageFirst] (const juce::String& oldName, const juce::String& newName) {
                // the plugin page shows the new preset's settings, the window takes the channel's new name (as FL)
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChannelFrame> (this), hasPage, oldName, newName] {
                    if (safe != nullptr)
                        safe->presetLoaded (hasPage, oldName, newName);
                });
            };
            addAndMakeVisible (*presetBar);
        }
        const auto name = [this] { return channelNameOf (engine.getSong(), channel); };
        auto headerKnob = [this] (std::function<ui::ValueSpec()> spec, std::function<double (const ChannelDef&)> get, std::function<void (Song&, int, double)> set,
                                  const char* caption) {
            auto k = std::make_unique<FrameKnob> (
                engine, std::move (spec),
                [this, get] { return channel < (int) engine.getSong().channels.size() ? get (engine.getSong().channels[(std::size_t) channel]) : 0.0; },
                [this, set] (double v) {
                    engine.editSong ([ch = channel, set, v] (Song& s) {
                        if (ch < (int) s.channels.size())
                            set (s, ch, v);
                    });
                },
                tr (caption));
            addAndMakeVisible (*k);
            header.push_back (std::move (k));
        };
        headerKnob ([this, name] {
                        auto s = ui::panSpec (name() + " - " + tr ("Pan"));
                        s.target = AutomationTarget { AutomationTargetKind::channelPan, channel };
                        return s;
                    },
                    [] (const ChannelDef& d) { return (double) d.pan; }, [] (Song& s, int ch, double v) { s.channels[(std::size_t) ch].pan = (float) v; },
                    TR_NOOP ("Pan"));
        headerKnob ([this, name] {
                        auto s = ui::percentSpec (name() + " - " + tr ("Volume"), 0.78);
                        s.target = AutomationTarget { AutomationTargetKind::channelVolume, channel };
                        return s;
                    },
                    [] (const ChannelDef& d) { return (double) d.volume; },
                    [] (Song& s, int ch, double v) { s.channels[(std::size_t) ch].volume = (float) v; }, TR_NOOP ("Vol"));
        // FL's PITCH knob and its RANGE (REA round 146b): the channel's pitch, within ± range semitones
        headerKnob ([this, name] {
                        ui::ValueSpec s;
                        s.name = name() + " - " + tr ("Pitch (semitones, within the range)");
                        const auto range = channel < (int) engine.getSong().channels.size() ? engine.getSong().channels[(std::size_t) channel].misc.pitchRange : 2;
                        s.minimum = -juce::jmax (1, range);
                        s.maximum = juce::jmax (1, range);
                        s.toText = [] (double v) { return tr ("{0} cents", { juce::String (juce::roundToInt (v * 100.0)) }); };
                        return s;
                    },
                    [] (const ChannelDef& d) { return (double) d.sampler.pitch; },
                    [] (Song& s, int ch, double v) { s.channels[(std::size_t) ch].sampler.pitch = (float) juce::jlimit (-24.0, 24.0, v); }, TR_NOOP ("Pitch"));
        headerKnob ([name] { return ui::digitWheel (countSpec (name() + " - " + tr ("Pitch knob range (semitones)"), 48, 2,
                                                              [] (int v) { return juce::String (v); })); },
                    [] (const ChannelDef& d) { return (double) d.misc.pitchRange; },
                    [] (Song& s, int ch, double v) { s.channels[(std::size_t) ch].misc.pitchRange = juce::jlimit (0, 48, juce::roundToInt (v)); },
                    TR_NOOP ("Range"));
        header.back()->digitBox = true;
        // FL's TRACK digiwheel (round 168s, issue #32 "very small and not accurate"): a wheel notch or 7.5 px of drag = the
        // next insert, double-click shows the insert in the Mixer, the menu has "Assign to free mixer track"
        headerKnob ([this, name] {
                        return ui::digitWheel (countSpec (name() + " - " + tr ("Target mixer track"), maxMixerTracks - 1, 0,
                                                          [] (int t) { return t == 0 ? tr ("Master") : juce::String (t); }));
                    },
                    [] (const ChannelDef& d) { return (double) d.mixerTrack; },
                    [] (Song& s, int ch, double v) {
                        mixerops::routeChannel (s, ch, juce::roundToInt (v)); // any insert (round 162): missing ones are added
                    },
                    TR_NOOP ("Track"));
        {
            auto& track = *header.back();
            track.digitBox = true;
            track.doubleClicked = [this] {
                if (channel < (int) engine.getSong().channels.size() && openMixerInsertHandler())
                    openMixerInsertHandler() (engine.getSong().channels[(std::size_t) channel].mixerTrack);
            };
            track.extraMenuItems = [this] (juce::PopupMenu& menu) {
                menu.addItem (tr ("Assign to free mixer track"), [this] {
                    engine.editSong ([ch = channel] (Song& s) {
                        if (ch < (int) s.channels.size())
                            s.channels[(std::size_t) ch].mixerTrack = s.claimFreeInsert();
                    });
                });
            };
        }

        int width = MiscPanel::preferredWidth, height = MiscPanel::preferredHeight;
        for (auto& p : pages)
        {
            width = juce::jmax (width, p->getWidth());
            height = juce::jmax (height, p->getHeight());
        }
        refresh();
        engine.addChangeListener (this);
        setSize (width, headerHeight + height);
        showTab (0);
    }

    ~ChannelFrame() override
    {
        if (! node)
            engine.removeChangeListener (this);
    }

    void showTab (int index)
    {
        index = juce::jlimit (0, (int) pages.size() - 1, index);
        current = index;
        for (int i = 0; i < (int) pages.size(); ++i)
        {
            pages[(std::size_t) i]->setVisible (i == index);
            tabs[(std::size_t) i]->setToggleState (i == index, juce::dontSendNotification);
        }
    }

    void presetLoaded (bool hasPage, const juce::String& oldName, const juce::String& newName)
    {
        if (hasPage && ! pages.empty())
            if (auto fresh = makeChannelSettingsPanel (engine, channel))
            {
                removeChildComponent (pages.front().get());
                pages.front() = std::move (fresh);
                addChildComponent (*pages.front());
                resized();
                showTab (current);
            }
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            if (const auto title = window->getName(); oldName.isNotEmpty() && title.startsWith (oldName))
                window->setName (newName + title.substring (oldName.length()));
    }

    int getTab() const noexcept { return current; }
    int tabCount() const noexcept { return (int) pages.size(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::bg3);
        g.fillRect (getLocalBounds().removeFromTop (headerHeight));
        g.setColour (theme::colour::border);
        g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        auto tabsArea = getLocalBounds().removeFromTop (headerHeight).reduced (8, 6);
        if (presetBar != nullptr) // tabs on the upper row, the preset arrows under them
        {
            presetBar->setBounds (tabsArea.removeFromBottom (22).removeFromLeft (juce::jmin (300, tabsArea.getWidth())));
            tabsArea.removeFromBottom (2);
        }
        for (auto& t : tabs)
        {
            const auto w = juce::jmax (64, (int) juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (theme::type::body)), t->getButtonText()) + 24);
            t->setBounds (tabsArea.removeFromLeft (w).withSizeKeepingCentre (w, juce::jmin (26, tabsArea.getHeight())));
            tabsArea.removeFromLeft (4);
        }
        auto right = getLocalBounds().removeFromTop (headerHeight).reduced (6, 2);
        for (auto it = header.rbegin(); it != header.rend(); ++it)
            (*it)->setBounds (right.removeFromRight (48));
        on.setBounds (right.removeFromRight (52).withSizeKeepingCentre (52, 24));
        // each page keeps its own size (some scale their drawing with it), centred under the header
        const auto body = getLocalBounds().withTrimmedTop (headerHeight);
        for (std::size_t i = 0; i < pages.size(); ++i)
        {
            const auto w = juce::jmin (body.getWidth(), sizes[i].x), h = juce::jmin (body.getHeight(), sizes[i].y);
            pages[i]->setBounds (body.getX() + (body.getWidth() - w) / 2, body.getY(), w, h);
        }
    }

private:
    void refresh()
    {
        if (channel < (int) engine.getSong().channels.size())
            on.setToggleState (! engine.getSong().channels[(std::size_t) channel].muted, juce::dontSendNotification);
        for (auto& k : header)
            k->repaint();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    AudioEngine& engine;
    int channel;
    bool node = false;
    int current = 0;
    std::vector<std::unique_ptr<juce::TextButton>> tabs;
    std::vector<std::unique_ptr<juce::Component>> pages;
    std::vector<juce::Point<int>> sizes;
    std::vector<std::unique_ptr<FrameKnob>> header;
    juce::ToggleButton on;
    std::unique_ptr<ChannelPresetBar> presetBar;
    bool instrumentPageFirst = false;
};

bool hasEnvelopeTab (InstrumentKind kind) noexcept
{
    return kind == InstrumentKind::synth || kind == InstrumentKind::sampler;
}

// The channel's Envelope / instrument settings page: LYNX's own for the synth and the sampler; FL's (the plugin voice
// processor's block in the generator's state) for Fruity Slicer (issue #129)
std::unique_ptr<juce::Component> makeEnvelopePage (AudioEngine& engine, const ChannelRef& ref, InstrumentKind kind)
{
    // round 190df: a sampler that runs FL's voice (an FL project's sampler: SamplerSettings::flChannel) gets FL's own
    // page, with the three tensions, the predelay and the envelope presets; its edits keep FL's voice (LYNX's page
    // would replace it)
    if (const auto* def = ref.get (engine); kind == InstrumentKind::sampler && def != nullptr && flsamplervoice::has (def->sampler))
        return makeFlVoicePanel (
            engine,
            [&engine, ref] () -> std::optional<flvoice::Settings> {
                const auto* d = ref.get (engine);
                if (d == nullptr || d->instrument != InstrumentKind::sampler || ! flsamplervoice::has (d->sampler))
                    return std::nullopt;
                return flsamplervoice::read (d->sampler);
            },
            [&engine, ref] (const flvoice::Settings& settings) {
                ref.edit (engine, [settings] (ChannelDef& d) { flsamplervoice::write (d.sampler, settings); });
            });
    if (hasEnvelopeTab (kind))
        return std::make_unique<ModulationPanel> (engine, ref);
    if (kind != InstrumentKind::fruityslicer)
        return nullptr;
    return makeFlVoicePanel (
        engine,
        [&engine, ref] () -> std::optional<flvoice::Settings> {
            const auto* def = ref.get (engine);
            if (def == nullptr || def->instrument != InstrumentKind::fruityslicer)
                return std::nullopt;
            const auto state = fslicer::parse (def->pluginState);
            return fslicer::voiceSettings (state ? *state : fslicer::State {});
        },
        [&engine, ref] (const flvoice::Settings& settings) {
            ref.edit (engine, [settings] (ChannelDef& def) {
                if (auto state = fslicer::parse (def.pluginState))
                {
                    fslicer::setVoiceSettings (*state, settings);
                    def.pluginState = fslicer::write (*state);
                }
            });
        });
}

void showModulationFor (juce::Component& from, AudioEngine& engine, int channel)
{
    if (auto* frame = from.findParentComponentOfClass<ChannelFrame>(); frame != nullptr && frame->tabCount() >= 2)
        frame->showTab (1);
    else if (channel >= 0)
        openChannelModulation (engine, channel);
}

bool isModelKind (InstrumentKind kind) noexcept
{
    return kind == InstrumentKind::sakura || kind == InstrumentKind::drumpad || kind == InstrumentKind::boobass
           || kind == InstrumentKind::drumsynth || kind == InstrumentKind::harmless || kind == InstrumentKind::harmor
           || kind == InstrumentKind::gms || kind == InstrumentKind::morphine
           || kind == InstrumentKind::ogun || kind == InstrumentKind::minisynth
           || kind == InstrumentKind::dx10 || kind == InstrumentKind::kepler || kind == InstrumentKind::keplerExo || kind == InstrumentKind::poizone
           || kind == InstrumentKind::fruitkick || kind == InstrumentKind::bassdrum || kind == InstrumentKind::toxic
           || kind == InstrumentKind::threeosc
           || kind == InstrumentKind::autogun || kind == InstrumentKind::transistorbass
           || kind == InstrumentKind::simsynth || kind == InstrumentKind::wasp || kind == InstrumentKind::plucked
           || kind == InstrumentKind::beepmap || kind == InstrumentKind::sawer || kind == InstrumentKind::flkeys
           || kind == InstrumentKind::granulizer || kind == InstrumentKind::drumaxx
           || kind == InstrumentKind::slayer || kind == InstrumentKind::fruitydance || kind == InstrumentKind::sytrus || kind == InstrumentKind::pocketSuperSaw;
}

// The instrument page of a channel or a Patcher node (null: none of its own)
std::unique_ptr<juce::Component> makeInstrumentPage (AudioEngine& engine, const ChannelRef& ref)
{
    const auto* def = ref.get (engine);
    if (def == nullptr)
        return nullptr;
    const auto kind = def->instrument;
    if (kind == InstrumentKind::fruitydance && ref.index >= 0)
        return makeDanceEditor (engine, ref.index); // round 165e
    if (kind == InstrumentKind::videoplayer)
        return makeVideoPlayerEditor (engine, ref.locate, ref.index); // round 165g
    if (kind == InstrumentKind::wavetraveller)
        return makeWaveTravellerEditor (engine, ref.locate, ref.index); // round 167f
    if (kind == InstrumentKind::fruityslicer)
        return makeFruitySlicerEditor (engine, ref.locate, ref.index); // round 180c
    if (kind == InstrumentKind::slicex)
        return makeSlicexEditor (engine, ref.locate, ref.index); // round 190r
    if (kind == InstrumentKind::fpc)
        return makeFpcEditor (engine, ref.locate, ref.index); // round 190q
    if (kind == InstrumentKind::synth)
        return std::make_unique<SynthPanel> (engine, ref);
    if (isModelKind (kind))
        return std::make_unique<ModelPanel> (engine, ref);
    if (kind == InstrumentKind::sampler)
        return std::make_unique<SettingsPanel> (engine, ref);
    return nullptr;
}

// Where "Align all channel editors" put every channel editor (FL stores it per channel, so editors opened later come
// up there too); empty = centred, as before.
std::optional<juce::Point<int>>& alignedChannelEditorPosition()
{
    static std::optional<juce::Point<int>> at;
    return at;
}

class SettingsWindow final : public juce::DocumentWindow
{
public:
    SettingsWindow (AudioEngine& engine, int c, bool modulationWindow)
        : juce::DocumentWindow (tr ("Channel settings"), theme::colour::bg2, DocumentWindow::closeButton),
          channel (c),
          modulation (modulationWindow)
    {
        setUsingNativeTitleBar (true);
        // FL names the window after the channel and its mixer track (REA round 149 [EXEC]: "FL Studio VSTi (Master)",
        // "Synth Bass (Synth Bass)")
        if (const auto& song = engine.getSong(); channel >= 0 && channel < (int) song.channels.size())
        {
            const auto& def = song.channels[(std::size_t) channel];
            const auto t = def.mixerTrack;
            const auto track = t <= 0 || t >= (int) song.mixer.size() ? tr ("Master")
                               : song.mixer[(std::size_t) t].name.empty() ? tr ("Insert {0}", { juce::String (t) })
                                                                          : juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str());
            setName (juce::String::fromUTF8 (def.name.c_str()) + " (" + track + ")"
                     + (modulation ? " - " + tr ("Envelope / instrument") : juce::String()));
        }

        if (modulation)
            setContentOwned (new ModulationPanel (engine, ChannelRef::rack (channel)), true);
        else if (! hasChannelSettings (engine.getSong().channels[(std::size_t) channel].instrument))
            setContentOwned (new ChannelFrame (engine, ChannelRef::rack (channel), nullptr, nullptr), true); // the Misc tab alone
        else
            setContentOwned (makeChannelFrame (engine, channel).release(), true);

        centreWithSize (getWidth(), getHeight());
        if (! modulation)
            if (const auto& at = alignedChannelEditorPosition(); at.has_value())
                setTopLeftPosition (*at); // "Align all channel editors" set every channel's place (round 169k)
        setVisible (true);
        markLinkedWindow (*this, channel, -1); // FL's Auto select linked modules (round 187a)
        keepAboveMainWindow (*this);
    }

    int getChannel() const { return channel; }
    bool isModulation() const { return modulation; }
    void closeButtonPressed() override;

private:
    int channel;
    bool modulation;
};

std::vector<std::unique_ptr<SettingsWindow>>& windows()
{
    static std::vector<std::unique_ptr<SettingsWindow>> open;
    return open;
}

void SettingsWindow::closeButtonPressed()
{
    auto* self = this;
    juce::MessageManager::callAsync ([self] {
        auto& open = windows();
        open.erase (std::remove_if (open.begin(), open.end(), [self] (auto& w) { return w.get() == self; }), open.end());
    });
}
} // namespace

namespace
{
void openWindow (AudioEngine& engine, int channel, bool modulation)
{
    for (auto& w : windows())
        if (w->getChannel() == channel && w->isModulation() == modulation)
        {
            w->toFront (true);
            return;
        }

    windows().push_back (std::make_unique<SettingsWindow> (engine, channel, modulation));
}
} // namespace

void alignAllChannelEditors (int selectedChannel)
{
    // FL's AlignAllChannelEditorsAction (FUN_012317c0, REA_rodada169k §2.4): the focused channel editor (else the
    // selected channel's, else the first open one) gives the place; every channel editor goes there.
    auto& open = windows();
    SettingsWindow* reference = nullptr;
    for (auto& w : open)
        if (! w->isModulation() && w->isActiveWindow())
            reference = w.get();
    for (auto& w : open)
        if (reference == nullptr && ! w->isModulation() && w->getChannel() == selectedChannel)
            reference = w.get();
    for (auto& w : open)
        if (reference == nullptr && ! w->isModulation())
            reference = w.get();
    if (reference == nullptr)
        return;
    const auto at = reference->getPosition();
    alignedChannelEditorPosition() = at;
    for (auto& w : open)
        if (! w->isModulation())
            w->setTopLeftPosition (at);
    reference->toFront (true);
}

void openChannelSettings (AudioEngine& engine, int channel)
{
    openWindow (engine, channel, false);
}

void openChannelMisc (AudioEngine& engine, int channel)
{
    openWindow (engine, channel, false);
    for (auto& w : windows())
        if (w->getChannel() == channel && ! w->isModulation())
            if (auto* frame = dynamic_cast<ChannelFrame*> (w->getContentComponent()))
                frame->showTab (frame->tabCount() - 1);
}

void openChannelModulation (AudioEngine& engine, int channel)
{
    openWindow (engine, channel, true);
}

void closeChannelSettings()
{
    windows().clear();
}

bool hasChannelSettings (InstrumentKind kind) noexcept
{
    switch (kind)
    {
        case InstrumentKind::synth:
        case InstrumentKind::sampler:
        case InstrumentKind::controller:
        case InstrumentKind::midiOut:
        case InstrumentKind::sakura:
        case InstrumentKind::drumpad:
        case InstrumentKind::boobass:
        case InstrumentKind::drumsynth:
        case InstrumentKind::harmless:
        case InstrumentKind::harmor:
        case InstrumentKind::gms:
        case InstrumentKind::morphine:
        case InstrumentKind::ogun:
        case InstrumentKind::minisynth:
        case InstrumentKind::dx10:
        case InstrumentKind::kepler:
        case InstrumentKind::keplerExo:
        case InstrumentKind::poizone:
        case InstrumentKind::fruitkick:
        case InstrumentKind::bassdrum:
        case InstrumentKind::toxic:
        case InstrumentKind::threeosc:
        case InstrumentKind::autogun:
        case InstrumentKind::transistorbass:
        case InstrumentKind::simsynth:
        case InstrumentKind::wasp:
        case InstrumentKind::plucked:
        case InstrumentKind::beepmap:
        case InstrumentKind::sawer:
        case InstrumentKind::flkeys:
        case InstrumentKind::granulizer:
        case InstrumentKind::drumaxx:
        case InstrumentKind::slayer:
        case InstrumentKind::fruitydance:
        case InstrumentKind::sytrus:
        case InstrumentKind::soundfont:
        case InstrumentKind::layer:
        case InstrumentKind::dashboard:
        case InstrumentKind::voltage:
        case InstrumentKind::vfx:
        case InstrumentKind::wavetraveller:
        case InstrumentKind::fruityslicer:
        case InstrumentKind::slicex:
        case InstrumentKind::fpc:
        case InstrumentKind::pocketSuperSaw:
            return true;
        default:
            return false;
    }
}

bool isChannelSettingsOpen (int channel)
{
    return std::any_of (windows().begin(), windows().end(), [channel] (auto& w) { return w->getChannel() == channel && ! w->isModulation(); });
}

void closeChannelSettings (int channel)
{
    auto& open = windows();
    open.erase (std::remove_if (open.begin(), open.end(), [channel] (auto& w) { return w->getChannel() == channel && ! w->isModulation(); }),
                open.end());
}

std::unique_ptr<juce::Component> makeChannelModulationPanel (AudioEngine& engine, int channel)
{
    return std::make_unique<ModulationPanel> (engine, ChannelRef::rack (channel));
}

std::unique_ptr<juce::Component> makeChannelPrecomputePanel (AudioEngine& engine, int channel)
{
    return std::make_unique<PrecomputePanel> (engine, ChannelRef::rack (channel));
}

int showChannelLayoutPage (juce::Component& frame, int page)
{
    if (auto* panel = dynamic_cast<ModelPanel*> (&frame); panel != nullptr && panel->pageCount() > 0)
    {
        const auto count = panel->pageCount();
        if (page >= 0 && page < count)
            panel->showPage (page);
        return count;
    }
    for (auto* child : frame.getChildren())
        if (const auto count = showChannelLayoutPage (*child, page); count > 0)
            return count;
    return 0;
}

int channelFrameHeaderHeight() noexcept
{
    return ChannelFrame::headerHeight;
}

std::unique_ptr<juce::Component> makeChannelFrame (AudioEngine& engine, int channel, int tab)
{
    const auto kind = engine.getSong().channels[(std::size_t) channel].instrument;
    auto frame = std::make_unique<ChannelFrame> (engine, ChannelRef::rack (channel), makeChannelSettingsPanel (engine, channel),
                                                 makeEnvelopePage (engine, ChannelRef::rack (channel), kind));
    frame->showTab (tab < 0 ? frame->tabCount() - 1 : tab);
    return frame;
}

std::unique_ptr<juce::Component> makeChannelSettingsPanel (AudioEngine& engine, int channel)
{
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::synth)
        return std::make_unique<SynthPanel> (engine, ChannelRef::rack (channel));
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::controller)
    {
        if (parseKeyboardController (engine.getSong().channels[(std::size_t) channel].controllerState))
            return makeKeyboardControllerEditor (engine, channel); // round 165c (it showed the Envelope Controller's read-only page)
        return makeEnvelopeControllerEditor (engine, channel);
    }
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::midiOut)
        return makeMidiOutEditor (engine, channel);
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::soundfont)
        return makeSoundFontEditor (engine, channel);
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::layer)
        return makeLayerEditor (engine, channel);
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::dashboard)
        return makeDashboardEditor (engine, channel);
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::voltage)
        return makeVoltageControllerEditor (engine, channel);
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::vfx)
        return makeVfxEditor (engine, channel);
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::fruitydance)
        return makeDanceEditor (engine, channel); // round 165e
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::videoplayer)
        return makeVideoPlayerEditor (engine, channel); // round 165g
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::wavetraveller)
        return makeWaveTravellerEditor (engine, channel); // round 167f
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::fruityslicer)
        return makeFruitySlicerEditor (engine, [channel] (Song& s) -> ChannelDef* {
            return channel >= 0 && channel < (int) s.channels.size() ? &s.channels[(std::size_t) channel] : nullptr;
        }, channel); // round 180c
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::slicex)
        return makeSlicexEditor (engine, [channel] (Song& s) -> ChannelDef* {
            return channel >= 0 && channel < (int) s.channels.size() ? &s.channels[(std::size_t) channel] : nullptr;
        }, channel); // round 190r
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::fpc)
        return makeFpcEditor (engine, [channel] (Song& s) -> ChannelDef* {
            return channel >= 0 && channel < (int) s.channels.size() ? &s.channels[(std::size_t) channel] : nullptr;
        }, channel); // round 190q
    if (const auto kind = engine.getSong().channels[(std::size_t) channel].instrument;
        kind == InstrumentKind::sakura || kind == InstrumentKind::drumpad || kind == InstrumentKind::boobass
                 || kind == InstrumentKind::drumsynth || kind == InstrumentKind::harmless || kind == InstrumentKind::harmor
                 || kind == InstrumentKind::gms || kind == InstrumentKind::morphine
                 || kind == InstrumentKind::ogun || kind == InstrumentKind::minisynth
                 || kind == InstrumentKind::dx10 || kind == InstrumentKind::kepler || kind == InstrumentKind::keplerExo || kind == InstrumentKind::poizone
                 || kind == InstrumentKind::fruitkick || kind == InstrumentKind::bassdrum || kind == InstrumentKind::toxic
                 || kind == InstrumentKind::threeosc
                 || kind == InstrumentKind::autogun || kind == InstrumentKind::transistorbass
                 || kind == InstrumentKind::simsynth || kind == InstrumentKind::wasp || kind == InstrumentKind::plucked
                 || kind == InstrumentKind::beepmap || kind == InstrumentKind::sawer || kind == InstrumentKind::flkeys
                 || kind == InstrumentKind::granulizer || kind == InstrumentKind::drumaxx
                 || kind == InstrumentKind::slayer || kind == InstrumentKind::fruitydance || kind == InstrumentKind::sytrus || kind == InstrumentKind::pocketSuperSaw)
        return std::make_unique<ModelPanel> (engine, ChannelRef::rack (channel));

    return std::make_unique<SettingsPanel> (engine, ChannelRef::rack (channel));
}

bool hasNodeEditor (const ChannelDef& channel) noexcept
{
    return channel.instrument == InstrumentKind::synth || channel.instrument == InstrumentKind::sampler || isModelKind (channel.instrument);
}

std::unique_ptr<juce::Component> makeNodeChannelFrame (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate)
{
    ChannelRef ref { std::move (locate), -1 };
    const auto* def = ref.get (engine);
    if (def == nullptr)
        return nullptr;
    auto page = makeInstrumentPage (engine, ref);
    if (page == nullptr)
        return nullptr;
    const auto kind = def->instrument;
    return std::make_unique<ChannelFrame> (engine, ref, std::move (page), makeEnvelopePage (engine, ref, kind));
}
std::function<void (int)>& openMixerInsertHandler()
{
    static std::function<void (int)> handler;
    return handler;
}

std::function<void (const juce::File&)>& locateInBrowserHandler()
{
    static std::function<void (const juce::File&)> handler;
    return handler;
}

namespace
{
SettingsPanel* findSamplerPage (juce::Component& c)
{
    if (auto* p = dynamic_cast<SettingsPanel*> (&c))
        return p;
    for (auto* child : c.getChildren())
        if (auto* p = findSamplerPage (*child))
            return p;
    return nullptr;
}
} // namespace

std::optional<juce::PopupMenu> samplerHistoryMenu (juce::Component& root)
{
    if (auto* p = findSamplerPage (root))
        return p->sampleHistoryMenuForShot();
    return std::nullopt;
}

bool samplerPageTurn (juce::Component& root, const juce::String& what, double value, bool shift)
{
    auto* p = findSamplerPage (root);
    return p != nullptr && p->shotTurn (what, value, shift);
}

juce::String samplerPageQuery (juce::Component& root, const juce::String& what)
{
    auto* p = findSamplerPage (root);
    if (p == nullptr)
        return {};
    if (what == "legacy")
        return p->shotLegacyShown() ? "1" : "0";
    if (what == "mode")
        return p->shotModeText();
    if (what == "scroll")
        return juce::String (p->shotScrollStart(), 3);
    return p->shotEnabled (what) ? "1" : "0";
}

bool samplerFileRowPress (juce::Component& root, int button)
{
    auto* p = findSamplerPage (root);
    if (p == nullptr)
        return false;
    p->pressFileRow (button);
    return true;
}
} // namespace lynx
