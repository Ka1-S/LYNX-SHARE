#include "ui/FlVoicePanel.h"

#include <array>
#include <cmath>

#include "core/FlControlMenus.h"
#include "core/FlVoiceProcessor.h"
#include "engine/AudioEngine.h"
#include "flp/Flp.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/PluginSkin.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// FL's page (FL Studio 2026, a Fruity Slicer's channel window; shots in /mnt/e/lynx-work/fpc190m/shots/fl-*.png):
// the target tabs, then three boxes side by side — Envelope (switch, graph, six knobs and the amount, the three
// tensions under attack / decay / release, Tempo), LFO (three shape buttons, graph, four knobs, Tempo and Global)
// and Filter (Mod X, Mod Y, type). Panning has no envelope box; the volume envelope has no amount.
// Positions are FL's own pixels at 100 % (the window's client area), drawn 1.2 times larger.
constexpr float scale = 1.2f;
constexpr int originY = 76; // FL's y of the page's top edge

juce::Rectangle<int> at (int x, int y, int w, int h)
{
    return juce::Rectangle<float> ((float) x * scale, (float) (y - originY) * scale, (float) w * scale, (float) h * scale).toNearestInt();
}

juce::Rectangle<int> around (int cx, int cy, int size)
{
    return at (cx - size / 2, cy - size / 2, size, size);
}

// How wide a time knob's segment is in a graph, as a fraction of the graph: fitted to FL's shots (release 6500 =
// 8 of 272 px; 20000 and 30000 = a tenth of the width each), not decompiled
float graphSpan (std::int32_t knob) noexcept
{
    return juce::jlimit (0.0f, 0.1f, (float) (knob - 100) / 65436.0f * 0.33f);
}

// The envelope of the target as FL draws it: delay, attack, hold, decay to the sustain level, release — with the
// three tensions — and a node at each corner; the volume envelope in grey behind the others
class EnvelopeGraph final : public juce::Component
{
public:
    void show (const flvoice::Articulator& a, const flvoice::Articulator& volume, int target)
    {
        shown = a;
        reference = volume;
        isVolume = target == flvoice::volume;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto box = getLocalBounds().toFloat();
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (box, 3.0f);
        const auto area = box.reduced (5.0f, 6.0f);
        if (! isVolume)
        {
            g.setColour (theme::colour::bg6);
            g.strokePath (curve (reference, 1.0f, area, nullptr), juce::PathStrokeType (1.0f));
        }
        std::vector<juce::Point<float>> nodes;
        const auto level = isVolume ? 1.0f : std::abs ((float) shown.amount) / 128.0f;
        const auto path = curve (shown, juce::jlimit (0.0f, 1.0f, level), area, &nodes);
        const auto colour = shown.envelope && isEnabled() ? theme::colour::accent : theme::colour::textDisabled;
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.5f));
        for (const auto& n : nodes)
        {
            g.setColour (theme::colour::bg0);
            g.fillEllipse (n.x - 3.0f, n.y - 3.0f, 6.0f, 6.0f);
            g.setColour (colour);
            g.drawEllipse (n.x - 3.0f, n.y - 3.0f, 6.0f, 6.0f, 1.2f);
        }
    }

private:
    static juce::Path curve (const flvoice::Articulator& a, float level, juce::Rectangle<float> area, std::vector<juce::Point<float>>* nodes)
    {
        const auto w = area.getWidth(), bottom = area.getBottom(), h = area.getHeight();
        const auto sustain = juce::jlimit (0.0f, 1.0f, (float) a.sustain / 128.0f);
        auto x = area.getX();
        juce::Path p;
        p.startNewSubPath (x, bottom);
        const auto node = [&] (float y) {
            if (nodes != nullptr)
                nodes->push_back ({ x, y });
        };
        // one stretch of the envelope from `from` to `to` (0…1), bent by a tension as the voice bends it
        const auto segment = [&] (std::int32_t knob, float from, float to, std::int32_t tension) {
            const auto span = graphSpan (knob) * w;
            const auto t = flvp::makeTension (tension);
            constexpr int steps = 16;
            if (span > 0.5f)
                for (int i = 1; i <= steps; ++i)
                {
                    const auto f = (float) i / (float) steps;
                    // a rising stretch runs the curve forward, a falling one backward (as the voice's ramps do)
                    const auto progress = to >= from ? flvp::shape (f, t) : 1.0f - flvp::shape (1.0f - f, t);
                    const auto value = from + (to - from) * progress;
                    p.lineTo (x + span * f, bottom - h * level * value);
                }
            else
                p.lineTo (x + span, bottom - h * level * to);
            x += span;
            node (bottom - h * level * to);
        };
        node (bottom);
        segment (a.delay, 0.0f, 0.0f, 0);
        segment (a.attack, 0.0f, 1.0f, a.attackTension);
        segment (a.hold, 1.0f, 1.0f, 0);
        segment (a.decay, 1.0f, sustain, a.decayTension);
        segment (a.release, sustain, 0.0f, a.releaseTension);
        return p;
    }

    flvoice::Articulator shown, reference;
    bool isVolume = true;
};

// The LFO of the target: flat while its amount is 0, else its shape after the delay, fading in over the attack
class LfoGraph final : public juce::Component
{
public:
    void show (const flvoice::Articulator& a, float fullAmount)
    {
        shown = a;
        full = fullAmount;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto box = getLocalBounds().toFloat();
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (box, 3.0f);
        const auto area = box.reduced (1.0f, 6.0f);
        const auto mid = area.getCentreY(), half = area.getHeight() * 0.5f, w = area.getWidth();
        const auto depth = juce::jlimit (-1.0f, 1.0f, (float) shown.lfoAmount / full);
        const auto delay = graphSpan (shown.lfoDelay) * w * 2.0f, attack = graphSpan (shown.lfoAttack) * w * 2.0f;
        const auto period = juce::jmax (4.0f, (float) (shown.lfoSpeed - 100) / 65436.0f * w);
        juce::Path p;
        for (int i = 0; i <= (int) w; ++i)
        {
            const auto x = (float) i;
            const auto t = x - delay;
            auto wave = 0.0f;
            if (t >= 0.0f)
            {
                const auto phase = std::fmod (t / period, 1.0f);
                wave = shown.lfoShape == 1 ? (phase < 0.25f ? phase * 4.0f : phase < 0.75f ? 2.0f - phase * 4.0f : phase * 4.0f - 4.0f)
                     : shown.lfoShape == 2 ? (phase < 0.5f ? 1.0f : -1.0f)
                                           : std::sin (phase * juce::MathConstants<float>::twoPi);
                wave *= attack > 0.5f ? juce::jmin (1.0f, t / attack) : 1.0f;
            }
            const auto y = mid - half * depth * wave;
            if (i == 0)
                p.startNewSubPath (area.getX() + x, y);
            else
                p.lineTo (area.getX() + x, y);
        }
        g.setColour (theme::colour::accent);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

private:
    flvoice::Articulator shown;
    float full = 128.0f;
};

// One of the LFO's three shape buttons (sine, triangle, pulse), drawn as FL draws them: the wave itself
class ShapeButton final : public juce::Button
{
public:
    explicit ShapeButton (int s) : juce::Button ({}), shape (s) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto box = getLocalBounds().toFloat();
        if (getToggleState() || over)
        {
            g.setColour (getToggleState() ? theme::colour::bg0 : theme::colour::bg4);
            g.fillRoundedRectangle (box, 3.0f);
        }
        const auto area = box.reduced (box.getWidth() * 0.22f, box.getHeight() * 0.28f);
        const auto x = area.getX(), w = area.getWidth(), mid = area.getCentreY(), half = area.getHeight() * 0.5f;
        juce::Path p;
        if (shape == 0)
            for (int i = 0; i <= 16; ++i)
            {
                const auto f = (float) i / 16.0f;
                const auto y = mid - half * std::sin (f * juce::MathConstants<float>::twoPi);
                if (i == 0)
                    p.startNewSubPath (x, y);
                else
                    p.lineTo (x + w * f, y);
            }
        else if (shape == 1)
        {
            p.startNewSubPath (x, mid);
            p.lineTo (x + w * 0.25f, mid - half);
            p.lineTo (x + w * 0.75f, mid + half);
            p.lineTo (x + w, mid);
        }
        else
        {
            p.startNewSubPath (x, mid + half);
            p.lineTo (x, mid - half);
            p.lineTo (x + w * 0.5f, mid - half);
            p.lineTo (x + w * 0.5f, mid + half);
            p.lineTo (x + w, mid + half);
        }
        g.setColour (getToggleState() ? theme::colour::accent : theme::colour::textSecondary);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

private:
    int shape;
};

class FlVoicePanel final : public juce::Component,
                           private juce::ChangeListener
{
public:
    FlVoicePanel (AudioEngine& e, std::function<std::optional<flvoice::Settings>()> r, std::function<void (const flvoice::Settings&)> w)
        : engine (e), read (std::move (r)), write (std::move (w))
    {
        const juce::String targets[] = { tr ("Panning"), tr ("Volume"), tr ("Mod X"), tr ("Mod Y"), tr ("Pitch") };
        for (int k = 0; k < flvoice::targetCount; ++k)
        {
            auto& b = tabs[(std::size_t) k];
            b.setButtonText (targets[k]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (7129);
            b.onClick = [this, k] { target = k; refresh(); };
            addAndMakeVisible (b);
        }
        tabs[(std::size_t) target].setToggleState (true, juce::dontSendNotification);

        title (lfoTitle, tr ("LFO"));
        title (filterTitle, tr ("Filter"));
        for (auto* l : { &tensionLabels[0], &tensionLabels[1] })
            caption (*l, tr ("Tension"));

        toggle (envelopeOn, tr ("Envelope"));
        // FL's ChanEnvPresetBtn (round 190df): top right of the Envelope box, a click lists the channel envelope presets
        presetsButton.setTooltip (tr ("Channel envelope presets"));
        presetsButton.setFlat (true);
        presetsButton.onClick = [this] { presetsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton)); };
        addAndMakeVisible (presetsButton);
        toggle (envelopeTempo, tr ("Tempo"));
        toggle (lfoTempo, tr ("Tempo"));
        toggle (lfoGlobal, tr ("Global"));
        lfoGlobal.setTooltip (tr ("Phase follows the song position instead of restarting on each note"));
        addAndMakeVisible (envelopeGraph);
        addAndMakeVisible (lfoGraph);

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
            t.setTooltip (tr ("Tension"));
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
            s->setDoubleClickReturnValue (true, 0.0);

        const juce::String shapeNames[] = { tr ("Sine"), tr ("Triangle"), tr ("Pulse") };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = shapes[(std::size_t) i];
            b.setTooltip (shapeNames[i]);
            b.setTitle (shapeNames[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (7130);
            b.onClick = [this] { apply(); };
            addAndMakeVisible (b);
        }
        // FL's own names for the processor's filters
        filterType.addItemList ({ "Fast LP", "LP", "BP", "HP", "BS", "LP x2", "SVF LP", "SVF LP x2" }, 1);
        filterType.setTooltip (tr ("Filter type"));
        filterType.onChange = [this] { apply(); };
        addAndMakeVisible (filterType);

        updating = false;
        refresh();
        engine.addChangeListener (this);
        setSize (juce::roundToInt (572.0f * scale), juce::roundToInt ((float) (322 - originY) * scale));
    }

    ~FlVoicePanel() override { engine.removeChangeListener (this); }

    void showTarget (int t)
    {
        target = juce::jlimit (0, (int) flvoice::targetCount - 1, t);
        tabs[(std::size_t) target].setToggleState (true, juce::dontSendNotification);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        // the three boxes
        for (const auto& box : { envelopeBox, lfoBox, filterBox })
            if (! box.isEmpty())
            {
                g.setColour (theme::colour::bg2);
                g.fillRoundedRectangle (box.toFloat(), 4.0f);
                g.setColour (theme::colour::border);
                g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
            }
    }

    void resized() override
    {
        // (FL's pixels: see `at`)
        auto x = 12;
        for (auto& b : tabs)
        {
            const auto w = juce::jmax (40, juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body), b.getButtonText()) / scale) + 14);
            b.setBounds (at (x, 84, w, 24));
            x += w + 2;
        }

        envelopeBox = at (11, 115, 290, 199);
        envelopeOn.setBounds (at (15, 117, 150, 22));
        presetsButton.setBounds (at (271, 117, 22, 22)); // (FL: EnvPanel 260, 2, 22 x 22)
        envelopeGraph.setBounds (at (22, 144, 272, 82));
        static constexpr int envelopeX[] = { 36, 70, 104, 138, 172, 206, 277 };
        for (int i = 0; i < 7; ++i)
        {
            envelope[(std::size_t) i].setBounds (around (envelopeX[i], 247, 30));
            envelopeLabels[(std::size_t) i].setBounds (at (envelopeX[i] - 18, 262, 36, 12));
        }
        static constexpr int tensionX[] = { 70, 138, 206 };
        for (int i = 0; i < 3; ++i)
            tension[(std::size_t) i].setBounds (around (tensionX[i], 289, 24));
        tensionLabels[0].setBounds (at (84, 283, 40, 12));
        tensionLabels[1].setBounds (at (152, 283, 40, 12));
        envelopeTempo.setBounds (at (226, 288, 72, 22));

        lfoBox = at (308, 115, 145, 199);
        for (int i = 0; i < 3; ++i)
            shapes[(std::size_t) i].setBounds (at (311 + i * 23, 117, 22, 22));
        lfoTitle.setBounds (at (382, 117, 60, 22));
        lfoGraph.setBounds (at (314, 144, 133, 82));
        static constexpr int lfoX[] = { 329, 363, 397, 431 };
        for (int i = 0; i < 4; ++i)
        {
            lfo[(std::size_t) i].setBounds (around (lfoX[i], 247, 30));
            lfoLabels[(std::size_t) i].setBounds (at (lfoX[i] - 17, 262, 34, 12));
        }
        lfoTempo.setBounds (at (312, 288, 68, 22));
        lfoGlobal.setBounds (at (381, 288, 70, 22));

        filterBox = at (460, 115, 101, 199);
        filterTitle.setBounds (at (466, 117, 50, 22));
        modXLabel.setBounds (at (502, 130, 56, 12));
        modX.setBounds (around (495, 172, 46));
        modY.setBounds (around (522, 238, 46));
        modYLabel.setBounds (at (462, 262, 50, 12));
        filterType.setBounds (at (465, 286, 91, 22));
    }

    // FL's ChanEnvPresetMenu [0xf5d510]: caption "Presets", the .CHENV files (a click loads one into the shown target,
    // one undo step "Channel envelope preset change" [0xf5aba0]), a line when there are any, "Save as..." (asks
    // "Envelope preset name" [0xf5acd0])
    juce::PopupMenu presetsMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Presets"));
        const auto files = envelopePresetFiles();
        for (const auto& f : files)
            menu.addItem (f.getFileNameWithoutExtension(), [safe = juce::Component::SafePointer<FlVoicePanel> (this), f] {
                if (safe != nullptr)
                    safe->loadEnvelopePreset (f);
            });
        if (! files.isEmpty())
            menu.addSeparator();
        menu.addItem (tr ("Save as..."), [safe = juce::Component::SafePointer<FlVoicePanel> (this)] {
            if (safe != nullptr)
                safe->askEnvelopePresetName();
        });
        return menu;
    }

    // LYNX's folder for them (what "Save as..." writes) and FL's user folder when it is there (read only)
    static juce::File envelopePresetFolder() { return PluginHost::settingsFolder().getChildFile ("Presets").getChildFile ("Channel envelopes"); }
    static juce::Array<juce::File> envelopePresetFiles()
    {
        juce::Array<juce::File> files;
        juce::StringArray names;
        const auto fl = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                            .getChildFile ("Image-Line").getChildFile ("FL Studio").getChildFile ("Presets").getChildFile ("Channel envelopes");
        for (const auto& folder : { envelopePresetFolder(), fl })
        {
            auto found = folder.findChildFiles (juce::File::findFiles, false, "*.chenv;*.CHENV");
            found.sort();
            for (const auto& f : found)
                if (! names.contains (f.getFileNameWithoutExtension(), true))
                {
                    names.add (f.getFileNameWithoutExtension());
                    files.add (f);
                }
        }
        return files;
    }

    bool loadEnvelopePreset (const juce::File& file)
    {
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data) || ! write || ! read || ! read())
            return false;
        const auto a = chenv::read ({ (const std::uint8_t*) data.getData(), (const std::uint8_t*) data.getData() + data.getSize() });
        if (! a)
            return false;
        auto next = current;
        next.articulators[(std::size_t) target] = *a;
        current = next;
        write (next);
        refresh();
        return true;
    }

    bool saveEnvelopePreset (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty() || ! envelopePresetFolder().createDirectory())
            return false;
        const auto bytes = chenv::write (current.articulators[(std::size_t) target]);
        return envelopePresetFolder().getChildFile (clean + ".CHENV").replaceWithData (bytes.data(), bytes.size());
    }

private:
    void askEnvelopePresetName()
    {
        auto* box = new juce::AlertWindow (tr ("Save as..."), tr ("Envelope preset name"), juce::MessageBoxIconType::NoIcon);
        box->addTextEditor ("name", {});
        box->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<FlVoicePanel> (this), box] (int result) {
            if (result == 1 && safe != nullptr)
                safe->saveEnvelopePreset (box->getTextEditorContents ("name"));
        }), true);
    }

    void title (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (theme::font (theme::type::body));
        addAndMakeVisible (l);
    }

    void caption (juce::Label& l, const juce::String& text)
    {
        l.setText (text.toUpperCase(), juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (theme::font (theme::type::caption).withHeight (9.5f));
        l.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        l.setMinimumHorizontalScale (0.6f);
        l.setBorderSize ({});
        l.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    }

    void toggle (juce::ToggleButton& b, const juce::String& text)
    {
        b.setButtonText (text);
        b.onClick = [this] { apply(); };
        addAndMakeVisible (b);
    }

    // FL's knobs carry a name under them and show their value in the hint bar; here the value pops up at the knob
    void knob (juce::Slider& s, juce::Label& label, const juce::String& name)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setPopupDisplayEnabled (true, true, this);
        // the same knobs as the Sampler's Envelope / instrument page (round 170j)
        s.getProperties().set (skin::sliderProperty, (juce::int64) skin::artAccent (skin::Art::lynxSynth).getARGB());
        s.getProperties().set (skin::styleProperty, skin::packStyle (skin::Style::themed, skin::Art::lynxSynth));
        s.onValueChange = [this] { apply(); };
        addAndMakeVisible (s);
        if (name.isNotEmpty())
        {
            s.setTitle (name);
            caption (label, name);
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
        shapes[(std::size_t) juce::jlimit (0, 2, (int) a.lfoShape)].setToggleState (true, quiet);
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

        // as FL's page: Panning has no envelope box (its LFO alone moves the pan), and the volume envelope no amount
        const auto hasEnvelope = target != flvoice::pan;
        envelopeBox = hasEnvelope ? at (11, 115, 290, 199) : juce::Rectangle<int>();
        envelopeOn.setVisible (hasEnvelope);
        presetsButton.setVisible (hasEnvelope);
        envelopeTempo.setVisible (hasEnvelope);
        envelopeGraph.setVisible (hasEnvelope);
        for (int i = 0; i < 7; ++i)
        {
            const auto shown = hasEnvelope && (i != 6 || target != flvoice::volume);
            envelope[(std::size_t) i].setVisible (shown);
            envelopeLabels[(std::size_t) i].setVisible (shown);
        }
        for (auto& t : tension)
            t.setVisible (hasEnvelope);
        for (auto& l : tensionLabels)
            l.setVisible (hasEnvelope);
        envelopeGraph.show (a, current.articulators[flvoice::volume], target);
        lfoGraph.show (a, target == flvoice::pitch ? 2400.0f : target == flvoice::pan ? 64.0f : 128.0f);
        repaint();
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
        a.lfoShape = shapes[1].getToggleState() ? 1 : shapes[2].getToggleState() ? 2 : 0;
        next.modX = value (modX), next.modY = value (modY);
        next.filterType = juce::jlimit (0, 7, filterType.getSelectedId() - 1);
        if (next == current)
            return;
        current = next;
        envelopeGraph.show (a, next.articulators[flvoice::volume], target);
        lfoGraph.show (a, target == flvoice::pitch ? 2400.0f : target == flvoice::pan ? 64.0f : 128.0f);
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
    juce::Rectangle<int> envelopeBox, lfoBox, filterBox;
    juce::Label lfoTitle, filterTitle, modXLabel, modYLabel, unusedLabel;
    std::array<juce::Label, 2> tensionLabels;
    juce::ToggleButton envelopeOn, envelopeTempo, lfoTempo, lfoGlobal;
    IconButton presetsButton { "presets", Icon::chevronDown };
    EnvelopeGraph envelopeGraph;
    LfoGraph lfoGraph;
    std::array<juce::Slider, 7> envelope;
    std::array<juce::Label, 7> envelopeLabels;
    std::array<juce::Slider, 3> tension;
    std::array<juce::Slider, 4> lfo;
    std::array<juce::Label, 4> lfoLabels;
    juce::Slider modX, modY;
    std::array<ShapeButton, 3> shapes { ShapeButton (0), ShapeButton (1), ShapeButton (2) };
    juce::ComboBox filterType;
};
bool showTargetIn (juce::Component& c, int target)
{
    if (auto* panel = dynamic_cast<FlVoicePanel*> (&c))
    {
        panel->showTarget (target);
        return true;
    }
    for (auto* child : c.getChildren())
        if (showTargetIn (*child, target))
            return true;
    return false;
}
FlVoicePanel* findPanel (juce::Component& c)
{
    if (auto* panel = dynamic_cast<FlVoicePanel*> (&c))
        return panel;
    for (auto* child : c.getChildren())
        if (auto* p = findPanel (*child))
            return p;
    return nullptr;
}
} // namespace

void showFlVoiceTarget (juce::Component& root, int target)
{
    showTargetIn (root, target);
}

std::optional<juce::PopupMenu> flVoicePresetsMenu (juce::Component& root)
{
    if (auto* p = findPanel (root))
        return p->presetsMenu();
    return std::nullopt;
}

bool flVoiceSavePreset (juce::Component& root, const juce::String& name)
{
    auto* p = findPanel (root);
    return p != nullptr && p->saveEnvelopePreset (name);
}

bool flVoiceLoadPreset (juce::Component& root, const juce::File& file)
{
    auto* p = findPanel (root);
    return p != nullptr && p->loadEnvelopePreset (file);
}

juce::File flVoicePresetFolder()
{
    return FlVoicePanel::envelopePresetFolder();
}

std::unique_ptr<juce::Component> makeFlVoicePanel (AudioEngine& engine, std::function<std::optional<flvoice::Settings>()> read,
                                                   std::function<void (const flvoice::Settings&)> write)
{
    return std::make_unique<FlVoicePanel> (engine, std::move (read), std::move (write));
}
} // namespace lynx
