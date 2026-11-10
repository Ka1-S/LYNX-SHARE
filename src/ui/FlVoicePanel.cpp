#include "ui/FlVoicePanel.h"

#include <array>
#include <cmath>

#include "engine/AudioEngine.h"
#include "flp/Flp.h"
#include "ui/I18n.h"
#include "ui/PluginSkin.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
class FlVoicePanel final : public juce::Component,
                           private juce::ChangeListener
{
public:
    FlVoicePanel (AudioEngine& e, std::function<std::optional<flvoice::Settings>()> r, std::function<void (const flvoice::Settings&)> w)
        : engine (e), read (std::move (r)), write (std::move (w))
    {
        const juce::String targets[] = { tr ("Pan"), tr ("Volume"), tr ("Mod X"), tr ("Mod Y"), tr ("Pitch") };
        for (int k = 0; k < flvoice::targetCount; ++k)
        {
            auto& b = tabs[(std::size_t) k];
            b.setButtonText (targets[k]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (7129);
            b.setConnectedEdges ((k > 0 ? juce::Button::ConnectedOnLeft : 0) | (k < flvoice::targetCount - 1 ? juce::Button::ConnectedOnRight : 0));
            b.onClick = [this, k] { target = k; refresh(); };
            addAndMakeVisible (b);
        }
        tabs[(std::size_t) target].setToggleState (true, juce::dontSendNotification);

        title (envelopeTitle, tr ("Envelope"));
        title (tensionTitle, tr ("Tension"));
        title (lfoTitle, tr ("LFO"));
        title (filterTitle, tr ("Filter"));

        toggle (envelopeOn, tr ("Envelope on"));
        toggle (envelopeTempo, tr ("Tempo sync"));
        toggle (lfoTempo, tr ("Tempo sync"));
        toggle (lfoGlobal, tr ("Global"));
        lfoGlobal.setTooltip (tr ("Phase follows the song position instead of restarting on each note"));

        const char* envelopeNames[] = { TR_NOOP ("Delay"), TR_NOOP ("Attack"), TR_NOOP ("Hold"), TR_NOOP ("Decay"), TR_NOOP ("Sustain"),
                                        TR_NOOP ("Release"), TR_NOOP ("Amount") };
        for (int i = 0; i < 7; ++i)
            knob (envelope[(std::size_t) i], envelopeLabels[(std::size_t) i], tr (envelopeNames[i]));
        const char* lfoNames[] = { TR_NOOP ("Delay"), TR_NOOP ("Attack"), TR_NOOP ("Amount"), TR_NOOP ("Speed") };
        for (int i = 0; i < 4; ++i)
            knob (lfo[(std::size_t) i], lfoLabels[(std::size_t) i], tr (lfoNames[i]));
        for (auto& t : tension)
        {
            knob (t, unusedLabel, {});
            t.setRange (-128.0, 128.0, 1.0);
            t.setDoubleClickReturnValue (true, 0.0);
            t.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0 / 128.0)) + "%"; };
            t.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue() * 128.0 / 100.0; };
        }
        knob (modX, modXLabel, tr ("Mod X"));
        knob (modY, modYLabel, tr ("Mod Y"));
        for (auto* s : { &modX, &modY })
        {
            s->setRange (0.0, 256.0, 1.0);
            s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0 / 256.0)) + "%"; };
            s->valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() * 256.0 / 100.0; };
        }
        modX.setDoubleClickReturnValue (true, 256.0);
        modY.setDoubleClickReturnValue (true, 0.0);

        // times: FL's knob values (100 = none … 65536), shown in seconds, or in beats with the tempo switch
        for (auto* s : { &envelope[0], &envelope[1], &envelope[2], &envelope[3], &envelope[5], &lfo[0], &lfo[1], &lfo[3] })
        {
            s->setRange (100.0, 65536.0, 1.0);
            const auto isLfo = s == &lfo[0] || s == &lfo[1] || s == &lfo[3];
            s->textFromValueFunction = [this, isLfo] (double v) {
                const auto t = flp::Channel::envelopeTime (juce::roundToInt (v));
                const auto synced = (isLfo ? lfoTempo : envelopeTempo).getToggleState();
                return synced ? juce::String (t * 2.0, 3) + " " + tr ("Beats").toLowerCase() : juce::String (t, 3) + " s";
            };
            s->valueFromTextFunction = [this, isLfo] (const juce::String& text) {
                const auto synced = (isLfo ? lfoTempo : envelopeTempo).getToggleState();
                const auto t = text.getDoubleValue() / (synced ? 2.0 : 1.0);
                return t <= 0.0 ? 100.0 : juce::jlimit (101.0, 65536.0, 65536.0 * std::log (t / 0.064 + 1.0) / std::log (1000.0));
            };
        }
        envelope[4].setRange (0.0, 128.0, 1.0); // sustain
        envelope[4].textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0 / 128.0)) + "%"; };
        envelope[4].valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue() * 128.0 / 100.0; };
        for (auto* s : { &envelope[6], &lfo[2] })
        {
            s->setDoubleClickReturnValue (true, 0.0);
            s->setTooltip (tr ("0 = off"));
        }

        shape.addItemList ({ tr ("Sine"), tr ("Triangle"), tr ("Pulse") }, 1);
        shape.onChange = [this] { apply(); };
        addAndMakeVisible (shape);
        // FL's own names for the processor's filters
        filterType.addItemList ({ "Fast LP", "LP", "BP", "HP", "BS", "LP x2", "SVF LP", "SVF LP x2" }, 1);
        filterType.setTooltip (tr ("Filter type"));
        filterType.onChange = [this] { apply(); };
        addAndMakeVisible (filterType);

        updating = false;
        refresh();
        engine.addChangeListener (this);
        setSize (660, 432);
    }

    ~FlVoicePanel() override { engine.removeChangeListener (this); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto row = area.removeFromTop (24);
        const auto tabWidth = row.getWidth() / flvoice::targetCount;
        for (auto& b : tabs)
            b.setBounds (row.removeFromLeft (tabWidth));
        area.removeFromTop (10);

        const auto column = (area.getWidth() - 80) / 7;
        auto knobRow = [&] (juce::Label& name, int height) {
            auto line = area.removeFromTop (height);
            name.setBounds (line.removeFromLeft (80).withSizeKeepingCentre (80, 24));
            return line;
        };
        auto place = [column] (juce::Rectangle<int>& line, juce::Slider& s, juce::Label* label) {
            auto cell = line.removeFromLeft (column);
            if (label != nullptr)
                label->setBounds (cell.removeFromTop (16));
            s.setBounds (cell);
        };

        // envelope: the switches, then the seven knobs, the tensions under attack, decay and release
        {
            auto line = area.removeFromTop (24);
            line.removeFromLeft (80);
            envelopeOn.setBounds (line.removeFromLeft (column * 2));
            envelopeTempo.setBounds (line.removeFromLeft (column * 2));
        }
        {
            auto line = knobRow (envelopeTitle, 78);
            for (int i = 0; i < 7; ++i)
                place (line, envelope[(std::size_t) i], &envelopeLabels[(std::size_t) i]);
        }
        {
            auto line = knobRow (tensionTitle, 62);
            line.removeFromLeft (column);
            place (line, tension[0], nullptr);
            line.removeFromLeft (column);
            place (line, tension[1], nullptr);
            line.removeFromLeft (column);
            place (line, tension[2], nullptr);
        }
        area.removeFromTop (10);

        // LFO: shape and switches, then the four knobs
        {
            auto line = area.removeFromTop (24);
            line.removeFromLeft (80);
            shape.setBounds (line.removeFromLeft (column * 2).reduced (3, 0));
            lfoTempo.setBounds (line.removeFromLeft (column * 2));
            lfoGlobal.setBounds (line.removeFromLeft (column * 2));
        }
        {
            auto line = knobRow (lfoTitle, 78);
            for (int i = 0; i < 4; ++i)
                place (line, lfo[(std::size_t) i], &lfoLabels[(std::size_t) i]);
        }
        area.removeFromTop (10);

        // filter
        {
            auto line = knobRow (filterTitle, 78);
            place (line, modX, &modXLabel);
            place (line, modY, &modYLabel);
            filterType.setBounds (line.removeFromLeft (column * 2).withSizeKeepingCentre (column * 2 - 6, 24));
        }
    }

private:
    void title (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (theme::font (theme::type::small));
        addAndMakeVisible (l);
    }

    void toggle (juce::ToggleButton& b, const juce::String& text)
    {
        b.setButtonText (text);
        b.onClick = [this] { apply(); };
        addAndMakeVisible (b);
    }

    void knob (juce::Slider& s, juce::Label& label, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
        // the same knobs as the Sampler's Envelope / instrument page (round 170j)
        s.getProperties().set (skin::sliderProperty, (juce::int64) skin::artAccent (skin::Art::lynxSynth).getARGB());
        s.getProperties().set (skin::styleProperty, skin::packStyle (skin::Style::themed, skin::Art::lynxSynth));
        s.onValueChange = [this] { apply(); };
        addAndMakeVisible (s);
        if (name.isNotEmpty())
        {
            label.setText (name, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (theme::font (theme::type::caption));
            addAndMakeVisible (label);
        }
    }

    // amounts: pan ±64 and the others ±128 of FL's range, shown in percent; pitch in cents
    void amountRange (juce::Slider& s, bool lfoAmount)
    {
        const auto cents = target == flvoice::pitch;
        const auto full = cents ? 2400.0 : (lfoAmount && target == flvoice::pan) ? 64.0 : 128.0;
        s.textFromValueFunction = [cents, full] (double v) {
            return cents ? juce::String (juce::roundToInt (v)) + " ct" : juce::String (juce::roundToInt (v * 100.0 / full)) + "%";
        };
        s.valueFromTextFunction = [cents, full] (const juce::String& t) { return cents ? t.getDoubleValue() : t.getDoubleValue() * full / 100.0; };
        s.setRange (-full, full, 1.0);
    }

    void refresh()
    {
        const auto settings = read ? read() : std::nullopt;
        if (! settings)
            return;
        const juce::ScopedValueSetter<bool> guard (updating, true); // (a new range may move a knob)
        current = *settings;
        const auto& a = current.articulators[(std::size_t) target];
        const auto quiet = juce::dontSendNotification;
        amountRange (envelope[6], false);
        amountRange (lfo[2], true);
        envelopeOn.setToggleState (a.envelope, quiet);
        envelopeTempo.setToggleState (a.envelopeTempo(), quiet);
        lfoTempo.setToggleState (a.lfoTempo(), quiet);
        lfoGlobal.setToggleState (a.lfoGlobal(), quiet);
        const std::int32_t env[] = { a.delay, a.attack, a.hold, a.decay, a.sustain, a.release, a.amount };
        for (int i = 0; i < 7; ++i)
            envelope[(std::size_t) i].setValue (env[i], quiet);
        const std::int32_t tens[] = { a.attackTension, a.decayTension, a.releaseTension };
        for (int i = 0; i < 3; ++i)
            tension[(std::size_t) i].setValue (tens[i], quiet);
        const std::int32_t l[] = { a.lfoDelay, a.lfoAttack, a.lfoAmount, a.lfoSpeed };
        for (int i = 0; i < 4; ++i)
            lfo[(std::size_t) i].setValue (l[i], quiet);
        shape.setSelectedId (juce::jlimit (0, 2, (int) a.lfoShape) + 1, quiet);
        modX.setValue (current.modX, quiet);
        modY.setValue (current.modY, quiet);
        filterType.setSelectedId (juce::jlimit (0, 7, (int) current.filterType) + 1, quiet);
        // the texts again: seconds / beats follow the tempo switches, and a value that did not move keeps the text it
        // had before its unit was set
        for (auto* group : { envelope.data(), tension.data(), lfo.data() })
            for (int i = 0; i < (group == envelope.data() ? 7 : group == tension.data() ? 3 : 4); ++i)
                group[i].updateText();
        modX.updateText();
        modY.updateText();

        // what the voice processor does not use: the pan target has no envelope (its LFO alone moves the pan), and
        // the volume envelope has no amount
        const auto hasEnvelope = target != flvoice::pan;
        envelopeOn.setEnabled (hasEnvelope);
        envelopeTempo.setEnabled (hasEnvelope);
        for (int i = 0; i < 7; ++i)
            envelope[(std::size_t) i].setEnabled (hasEnvelope && (i != 6 || target != flvoice::volume));
        for (auto& t : tension)
            t.setEnabled (hasEnvelope);
    }

    void apply()
    {
        if (updating || ! read || ! write || ! read())
            return;
        auto next = current;
        auto& a = next.articulators[(std::size_t) target];
        auto value = [] (const juce::Slider& s) { return (std::int32_t) juce::roundToInt (s.getValue()); };
        a.envelope = envelopeOn.getToggleState();
        a.setFlag (1, envelopeTempo.getToggleState());
        a.setFlag (2, lfoTempo.getToggleState());
        a.setFlag (0x20, lfoGlobal.getToggleState());
        a.delay = value (envelope[0]), a.attack = value (envelope[1]), a.hold = value (envelope[2]), a.decay = value (envelope[3]);
        a.sustain = value (envelope[4]), a.release = value (envelope[5]), a.amount = value (envelope[6]);
        a.attackTension = value (tension[0]), a.decayTension = value (tension[1]), a.releaseTension = value (tension[2]);
        a.lfoDelay = value (lfo[0]), a.lfoAttack = value (lfo[1]), a.lfoAmount = value (lfo[2]), a.lfoSpeed = value (lfo[3]);
        a.lfoShape = juce::jlimit (0, 2, shape.getSelectedId() - 1);
        next.modX = value (modX), next.modY = value (modY);
        next.filterType = juce::jlimit (0, 7, filterType.getSelectedId() - 1);
        if (next == current)
            return;
        current = next;
        write (next);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    AudioEngine& engine;
    std::function<std::optional<flvoice::Settings>()> read;
    std::function<void (const flvoice::Settings&)> write;
    flvoice::Settings current;
    int target = flvoice::volume;
    bool updating = true; // while the controls are being built or filled: nothing is written
    std::array<juce::TextButton, flvoice::targetCount> tabs;
    juce::Label envelopeTitle, tensionTitle, lfoTitle, filterTitle, modXLabel, modYLabel, unusedLabel;
    juce::ToggleButton envelopeOn, envelopeTempo, lfoTempo, lfoGlobal;
    std::array<juce::Slider, 7> envelope;
    std::array<juce::Label, 7> envelopeLabels;
    std::array<juce::Slider, 3> tension;
    std::array<juce::Slider, 4> lfo;
    std::array<juce::Label, 4> lfoLabels;
    juce::Slider modX, modY;
    juce::ComboBox shape, filterType;
};
} // namespace

std::unique_ptr<juce::Component> makeFlVoicePanel (AudioEngine& engine, std::function<std::optional<flvoice::Settings>()> read,
                                                   std::function<void (const flvoice::Settings&)> write)
{
    return std::make_unique<FlVoicePanel> (engine, std::move (read), std::move (write));
}
} // namespace lynx
