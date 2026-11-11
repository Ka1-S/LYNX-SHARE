#include "ui/MidiToolsPanel.h"

#include <algorithm>
#include <cmath>

#include "core/NoteTools.h"
#include "core/ProjectKey.h"
#include "ui/ControllerKnob.h"
#include "ui/I18n.h"
#include "ui/NumBox.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
using namespace theme;

namespace
{
// note values a Rate knob steps through (ticks); Live's default 1/32 is index 2
constexpr int rateCount = 8;
constexpr const char* rateNames[rateCount] = { "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
constexpr std::int64_t rateTicks (int index) noexcept { return ppq * 4 / (128 >> std::clamp (index, 0, rateCount - 1)); }
int rateIndex (std::int64_t ticks) noexcept
{
    for (int i = 0; i < rateCount; ++i)
        if (rateTicks (i) >= ticks)
            return i;
    return rateCount - 1;
}

ui::ValueSpec makeSpec (const juce::String& name, double lo, double hi, double def, double interval, std::function<juce::String (double)> text)
{
    ui::ValueSpec s;
    s.name = name;
    s.minimum = lo;
    s.maximum = hi;
    s.defaultValue = def;
    s.interval = interval;
    s.toText = std::move (text);
    return s;
}
juce::String percent1 (double v) { return juce::String (v * 100.0, 1) + " %"; } // Live writes "0.0 %"
juce::String percent0 (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }
juce::String whole (double v) { return juce::String (juce::roundToInt (v)); }

using ui::NumBox;

// Strum's display: the chord's notes bottom (low) to top (high) against the time they move by, bent by the tension
// (miditools::strumCurve, the curve Live draws); the two handles are Low and High, dragged sideways.
class StrumCurve final : public juce::Component
{
public:
    StrumCurve (miditools::StrumOptions& o, std::function<void()> onChange) : options (o), changed (std::move (onChange)) {}
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colour::bg0);
        g.fillRect (r);
        g.setColour (colour::borderStrong);
        g.drawRect (r);
        const auto area = r.reduced (8.0f, 7.0f);
        g.setColour (colour::border);
        g.fillRect (area.getCentreX(), area.getY(), 1.0f, area.getHeight());
        juce::Path path;
        for (int i = 0; i <= 32; ++i)
        {
            const auto p = at (area, (double) i / 32.0);
            if (i == 0)
                path.startNewSubPath (p);
            else
                path.lineTo (p);
        }
        g.setColour (colour::knobArc);
        g.strokePath (path, juce::PathStrokeType (1.5f));
        g.setColour (colour::accent);
        for (auto v : { 0.0, 1.0 })
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (at (area, v)));
    }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto area = getLocalBounds().toFloat().reduced (9.0f, 8.0f);
        const auto v = juce::jlimit (-1.0f, 1.0f, ((float) e.x - area.getCentreX()) / (area.getWidth() * 0.5f));
        (e.getMouseDownY() > getHeight() / 2 ? options.low : options.high) = std::round (v * 100.0f) / 100.0f;
        changed();
        repaint();
    }

private:
    // pitch share 0 (the chord's lowest note) … 1 (its highest) → the point on the display
    juce::Point<float> at (juce::Rectangle<float> area, double pitch) const
    {
        // the core's own rule (round 190aa): each side's curve runs from the note that stays
        const auto offset = (double) options.low * miditools::strumCurve (1.0 - pitch, options.tension)
                          + (double) options.high * miditools::strumCurve (pitch, options.tension);
        return { area.getCentreX() + (float) offset * area.getWidth() * 0.5f, area.getBottom() - (float) pitch * area.getHeight() };
    }
    miditools::StrumOptions& options;
    std::function<void()> changed;
};

// Recombine's Rotate display: one bar per chord of the notes, the bar the first chord goes to lit; click a bar.
class RotateBars final : public juce::Component
{
public:
    RotateBars (std::function<int()> countOf, std::function<int()> get, std::function<void (int)> set)
        : count (std::move (countOf)), getter (std::move (get)), setter (std::move (set)) {}
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::bg0);
        g.fillRect (getLocalBounds());
        const auto n = std::clamp (count(), 1, 32);
        const auto lit = ((getter() % n) + n) % n;
        const auto w = (float) (getWidth() - 4) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            g.setColour (i == lit ? colour::accent : colour::bg5);
            g.fillRect (juce::Rectangle<float> (2.0f + (float) i * w, 2.0f, std::max (1.0f, w - 1.0f), (float) getHeight() - 4.0f));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto n = std::clamp (count(), 1, 32);
        setter (std::clamp (e.x * n / std::max (1, getWidth()), 0, n - 1));
        repaint();
    }

private:
    std::function<int()> count;
    std::function<int()> getter;
    std::function<void (int)> setter;
};
} // namespace

const char* MidiToolsPanel::toolName (int t) noexcept
{
    switch (t)
    {
        case arpeggiate: return TR_NOOP ("Arpeggiate");
        case chop:       return TR_NOOP ("Chop");
        case connect:    return TR_NOOP ("Connect");
        case ornament:   return TR_NOOP ("Ornament");
        case quantize:   return TR_NOOP ("Quantize");
        case recombine:  return TR_NOOP ("Recombine");
        case span:       return TR_NOOP ("Span");
        case strum:      return TR_NOOP ("Strum");
        default:         return "";
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The current tool's controls, laid out as Live 12.4's panels [SHOT ableton190o/shots/live-03-transform-panels.png].
struct MidiToolsPanel::Body final : public juce::Component
{
    Body (MidiToolsPanel& p, int width, int height) : panel (p)
    {
        setSize (width, height);
        switch (panel.tool)
        {
            case arpeggiate: buildArpeggiate(); break;
            case chop:       buildChop(); break;
            case connect:    buildConnect(); break;
            case ornament:   buildOrnament(); break;
            case quantize:   buildQuantize(); break;
            case recombine:  buildRecombine(); break;
            case span:       buildSpan(); break;
            case strum:      buildStrum(); break;
            default: break;
        }
    }

    // --- pieces ---
    template <typename T>
    T& add (std::unique_ptr<T> c, juce::Rectangle<int> bounds)
    {
        auto& ref = *c;
        addAndMakeVisible (ref);
        ref.setBounds (bounds);
        items.push_back (std::move (c));
        return ref;
    }
    juce::Label& label (const juce::String& text, juce::Rectangle<int> bounds, juce::Justification j = juce::Justification::centredLeft)
    {
        auto& l = add (std::make_unique<juce::Label>(), bounds);
        l.setText (text, juce::dontSendNotification);
        l.setFont (font (type::caption));
        l.setColour (juce::Label::textColourId, colour::textSecondary);
        l.setJustificationType (j);
        l.setBorderSize ({});
        l.setInterceptsMouseClicks (false, false);
        return l;
    }
    std::function<void (double)> setting (std::function<void (double)> set)
    {
        return [this, set = std::move (set)] (double v) {
            set (v);
            panel.changed();
        };
    }
    void knob (const juce::String& name, juce::Rectangle<int> bounds, ui::ValueSpec spec, std::function<double()> get, std::function<void (double)> set)
    {
        spec.name = name;
        add (std::make_unique<ctlui::Knob> (nullptr, std::move (spec), std::move (get), setting (std::move (set))), bounds);
    }
    NumBox& box (const juce::String& name, juce::Rectangle<int> bounds, ui::ValueSpec spec, std::function<double()> get, std::function<void (double)> set)
    {
        spec.name = name;
        return add (std::make_unique<NumBox> (std::move (spec), std::move (get), setting (std::move (set))), bounds);
    }
    juce::TextButton& toggle (const juce::String& text, juce::Rectangle<int> bounds, bool on, std::function<void (bool)> set, int radioGroup = 0)
    {
        auto& b = add (std::make_unique<juce::TextButton> (text), bounds);
        b.setConnectedEdges (0);
        b.setClickingTogglesState (true);
        if (radioGroup != 0)
            b.setRadioGroupId (radioGroup, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonOnColourId, colour::accent);
        b.setColour (juce::TextButton::textColourOnId, colour::onAccent);
        b.setToggleState (on, juce::dontSendNotification);
        auto* raw = &b;
        b.onClick = [this, raw, radioGroup, set = std::move (set)] {
            if (radioGroup != 0 && ! raw->getToggleState())
                return; // (the one that went off)
            set (raw->getToggleState());
            panel.changed();
        };
        return b;
    }
    juce::ComboBox& choice (juce::Rectangle<int> bounds, const juce::StringArray& names, int index, std::function<void (int)> set)
    {
        auto& c = add (std::make_unique<juce::ComboBox>(), bounds);
        c.addItemList (names, 1);
        c.setSelectedId (index + 1, juce::dontSendNotification);
        auto* raw = &c;
        c.onChange = [this, raw, set = std::move (set)] {
            set (raw->getSelectedId() - 1);
            panel.changed();
        };
        return c;
    }
    static ui::ValueSpec rateSpec (bool withGrid, int def)
    {
        return makeSpec ({}, withGrid ? -1.0 : 0.0, rateCount - 1, def, 1.0, [] (double v) {
            const auto i = juce::roundToInt (v);
            return i < 0 ? tr ("Grid") : juce::String (rateNames[std::clamp (i, 0, rateCount - 1)]);
        });
    }
    static ui::ValueSpec bipolar (double def = 0.0) { return makeSpec ({}, -1.0, 1.0, def, 0.0, percent1); }
    static ui::ValueSpec unipolar (double def) { return makeSpec ({}, 0.0, 1.0, def, 0.0, percent0); }

    // --- the tools ---
    void buildArpeggiate()
    {
        auto& o = panel.arp;
        const auto w = getWidth(), half = w / 2;
        label (tr ("Style"), { 0, 0, half - 4, 12 });
        juce::StringArray styles;
        for (int i = 0; i < miditools::ArpeggiateOptions::styleCount; ++i)
            styles.add (tr (miditools::arpeggiateStyleName (i)));
        choice ({ 0, 13, half - 4, 20 }, styles, o.style, [&o] (int i) { o.style = i; });
        label (tr ("Steps"), { 0, 36, half - 4, 12 });
        box (tr ("Steps"), { 0, 49, half - 4, 16 }, makeSpec ({}, 1, 8, 2, 1.0, whole), [&o] { return (double) o.steps; }, [&o] (double v) { o.steps = juce::roundToInt (v); });
        knob (tr ("Distance"), { half, 0, half, 62 }, makeSpec ({}, -24, 24, 12, 1.0, [] (double v) { return (v > 0 ? "+" : "") + whole (v) + " st"; }),
              [&o] { return (double) o.distance; }, [&o] (double v) { o.distance = juce::roundToInt (v); });
        knob (tr ("Rate"), { 0, 66, half, 58 }, rateSpec (false, 2), [&o] { return (double) rateIndex (o.rate); }, [&o] (double v) { o.rate = rateTicks (juce::roundToInt (v)); });
        knob (tr ("Gate"), { half, 66, half, 58 }, makeSpec ({}, 0.01, 2.0, 1.0, 0.0, percent0), [&o] { return (double) o.gate; }, [&o] (double v) { o.gate = (float) v; });
    }
    void buildChop()
    {
        auto& o = panel.chopOptions;
        const auto half = getWidth() / 2;
        knob (tr ("Parts"), { 0, 4, half, 62 }, makeSpec ({}, 2, 64, 4, 1.0, whole), [&o] { return (double) o.parts; }, [&o] (double v) { o.parts = juce::roundToInt (v); });
        knob (tr ("Gaps"), { half, 4, half, 62 }, makeSpec ({}, -16, 16, 0, 1.0, whole), [&o] { return (double) o.gaps; }, [&o] (double v) { o.gaps = juce::roundToInt (v); });
    }
    void buildConnect()
    {
        auto& o = panel.connectOptions;
        const auto half = getWidth() / 2;
        knob (tr ("Spread"), { 0, 0, half, 60 }, makeSpec ({}, 0, 24, 0, 1.0, [] (double v) { return whole (v) + " st"; }),
              [&o] { return (double) o.spread; }, [&o] (double v) { o.spread = juce::roundToInt (v); });
        knob (tr ("Density"), { half, 0, half, 60 }, unipolar (1.0), [&o] { return (double) o.density; }, [&o] (double v) { o.density = (float) v; });
        knob (tr ("Rate"), { 0, 62, half, 60 }, rateSpec (true, -1), [&o] { return o.rate <= 0 ? -1.0 : (double) rateIndex (o.rate); },
              [&o] (double v) { o.rate = v < -0.5 ? 0 : rateTicks (juce::roundToInt (v)); });
        knob (tr ("Tie"), { half, 62, half, 60 }, makeSpec ({}, 0.0, 1.0, 0.0, 0.0, percent1), [&o] { return (double) o.tie; }, [&o] (double v) { o.tie = (float) v; });
    }
    // "Position [-20 %] Vel. [50 %]"
    void positionVelocityRow (int y, float& position, float& velocity)
    {
        const auto w = getWidth();
        label (tr ("Position"), { 0, y, 50, 16 });
        box (tr ("Position"), { 50, y, 40, 16 }, makeSpec ({}, -1.0, 1.0, -0.2, 0.0, percent0), [&position] { return (double) position; }, [&position] (double v) { position = (float) v; });
        label (tr ("Vel."), { 94, y, 30, 16 });
        box (tr ("Velocity"), { w - 44, y, 44, 16 }, unipolar (0.5), [&velocity] { return (double) velocity; }, [&velocity] (double v) { velocity = (float) v; });
    }
    void buildOrnament()
    {
        auto& o = panel.ornamentOptions;
        const auto w = getWidth();
        toggle (tr ("Flam"), { 0, 0, w, 18 }, o.flam, [&o] (bool on) { o.flam = on; });
        positionVelocityRow (21, o.flamPosition, o.flamVelocity);
        toggle (tr ("Grace notes"), { 0, 42, w, 18 }, o.grace, [&o] (bool on) { o.grace = on; });
        const auto third = (w - 34) / 3;
        label (tr ("Pitch"), { 0, 63, 34, 16 });
        const std::pair<const char*, int> pitches[] = { { TR_NOOP ("High"), miditools::OrnamentOptions::high }, { TR_NOOP ("Same"), miditools::OrnamentOptions::same },
                                                        { TR_NOOP ("Low"), miditools::OrnamentOptions::low } };
        for (int i = 0; i < 3; ++i)
            toggle (tr (pitches[i].first), { 34 + i * third, 63, third - 2, 16 }, o.gracePitch == pitches[i].second,
                    [&o, value = pitches[i].second] (bool) { o.gracePitch = value; }, 1901);
        positionVelocityRow (83, o.gracePosition, o.graceVelocity);
        label (tr ("Chance"), { 0, 103, 50, 16 });
        box (tr ("Chance"), { 50, 103, 40, 16 }, unipolar (1.0), [&o] { return (double) o.graceChance; }, [&o] (double v) { o.graceChance = (float) v; });
        label (tr ("Amount"), { 94, 103, 44, 16 });
        box (tr ("Amount"), { w - 30, 103, 30, 16 }, makeSpec ({}, 1, 8, 3, 1.0, whole), [&o] { return (double) o.graceAmount; }, [&o] (double v) { o.graceAmount = juce::roundToInt (v); });
    }
    void buildQuantize()
    {
        auto& o = panel.quantizeOptions;
        const auto w = getWidth(), half = (w - 4) / 2;
        toggle (tr ("Grid"), { 0, 0, w, 17 }, o.step == 0, [&o] (bool) { o.step = 0; }, 1902);
        const int dens[] = { 4, 8, 16, 32 };
        for (int i = 0; i < 4; ++i)
        {
            const auto ticks = (std::int64_t) (ppq * 4 / dens[i]);
            toggle ("1/" + juce::String (dens[i]), { (i % 2) * (half + 4), 20 + (i / 2) * 20, half, 17 }, o.step == ticks, [&o, ticks] (bool) { o.step = ticks; }, 1902);
        }
        toggle (tr ("Triplets"), { 0, 60, w, 17 }, o.triplets, [&o] (bool on) { o.triplets = on; });
        label (tr ("Adjust note"), { 0, 81, 70, 17 });
        const auto small = (w - 70 - 4) / 2;
        toggle (tr ("Start"), { 70, 81, small, 17 }, o.start, [&o] (bool on) { o.start = on; });
        toggle (tr ("End"), { 74 + small, 81, small, 17 }, o.end, [&o] (bool on) { o.end = on; });
        label (tr ("Amount"), { 0, 103, 70, 16 });
        box (tr ("Amount"), { 70, 103, w - 70, 16 }, unipolar (1.0), [&o] { return (double) o.amount; }, [&o] (double v) { o.amount = (float) v; });
    }
    void buildRecombine()
    {
        auto& o = panel.recombineOptions;
        const auto w = getWidth(), half = (w - 4) / 2;
        choice ({ 0, 0, w, 20 }, { tr ("Position"), tr ("Pitch"), tr ("Duration"), tr ("Velocity") }, o.dimension, [&o] (int i) { o.dimension = i; });
        toggle (tr ("Shuffle"), { 0, 24, half, 17 }, o.shuffle, [this, &o] (bool on) {
            o.shuffle = on;
            if (on)
                panel.seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt(); // (a new order each time it goes on)
        });
        toggle (tr ("Mirror"), { half + 4, 24, half, 17 }, o.mirror, [&o] (bool on) { o.mirror = on; });
        label (tr ("Rotate"), { 0, 44, w, 12 }, juce::Justification::centred);
        const auto chordCount = [this] {
            const auto t = panel.target();
            const auto& song = panel.engine.getSong();
            if (t.pattern < 0 || t.pattern >= (int) song.patterns.size())
                return 1;
            const auto& pattern = panel.session.valid && panel.session.pattern == t.pattern ? panel.session.base : song.patterns[(std::size_t) t.pattern].notes;
            miditools::Notes chosen;
            for (auto i : panel.session.valid ? panel.session.indices : t.indices)
                if (i >= 0 && i < (int) pattern.size())
                    chosen.push_back (pattern[(std::size_t) i]);
            return std::max (1, (int) miditools::chords (std::move (chosen)).size());
        };
        add (std::make_unique<RotateBars> (chordCount, [&o] { return o.rotate; }, setting ([&o] (double v) { o.rotate = juce::roundToInt (v); })), { 0, 57, w, 40 });
        toggle (tr ("Grid"), { 0, 102, 46, 17 }, o.rotateOnGrid, [&o] (bool on) { o.rotateOnGrid = on; });
        auto& value = box (tr ("Rotate"), { w - 66, 102, 40, 17 }, makeSpec ({}, -32, 32, 0, 1.0, whole), [&o] { return (double) o.rotate; },
                           [this, &o] (double v) { o.rotate = juce::roundToInt (v); repaint(); });
        const auto step = [this, &o, &value] (juce::Rectangle<int> bounds, const char* text, int by) {
            auto& b = add (std::make_unique<juce::TextButton> (juce::String::fromUTF8 (text)), bounds);
            b.setConnectedEdges (0);
            b.onClick = [this, &o, &value, by] {
                o.rotate = std::clamp (o.rotate + by, -32, 32);
                value.repaint();
                repaint();
                panel.changed();
            };
        };
        step ({ w - 88, 102, 20, 17 }, "\xe2\x88\x92", -1);
        step ({ w - 22, 102, 22, 17 }, "+", 1);
    }
    void buildSpan()
    {
        auto& o = panel.spanOptions;
        const auto w = getWidth(), left = w / 2 - 4;
        const std::pair<const char*, int> modes[] = { { TR_NOOP ("Legato"), miditools::SpanOptions::legato }, { TR_NOOP ("Tenuto"), miditools::SpanOptions::tenuto },
                                                      { TR_NOOP ("Staccato"), miditools::SpanOptions::staccato } };
        for (int i = 0; i < 3; ++i)
            toggle (tr (modes[i].first), { 0, 58 + i * 21, left, 18 }, o.mode == modes[i].second, [&o, value = modes[i].second] (bool) { o.mode = value; }, 1903);
        knob (tr ("Offset"), { w / 2, 0, w / 2, 60 }, bipolar(), [&o] { return (double) o.offset; }, [&o] (double v) { o.offset = (float) v; });
        knob (tr ("Variation"), { w / 2, 62, w / 2, 60 }, bipolar(), [&o] { return (double) o.variation; }, [&o] (double v) { o.variation = (float) v; });
    }
    void buildStrum()
    {
        auto& o = panel.strumOptions;
        const auto w = getWidth();
        auto* curve = &add (std::make_unique<StrumCurve> (o, [this] { repaint(); panel.changed(); }), { 0, 20, w, 80 });
        const auto redraw = [curve] (float& field) {
            return [curve, &field] (double v) {
                field = (float) v;
                curve->repaint();
            };
        };
        label (tr ("Low"), { 0, 0, 30, 16 });
        box (tr ("Low"), { 30, 0, 50, 16 }, bipolar(), [&o] { return (double) o.low; }, redraw (o.low));
        label (tr ("High"), { 86, 0, 34, 16 });
        box (tr ("High"), { w - 50, 0, 50, 16 }, bipolar(), [&o] { return (double) o.high; }, redraw (o.high));
        label (tr ("Tension"), { 0, 104, 60, 16 });
        box (tr ("Tension"), { 60, 104, 54, 16 }, bipolar(), [&o] { return (double) o.tension; }, redraw (o.tension));
    }

    MidiToolsPanel& panel;
    std::vector<std::unique_ptr<juce::Component>> items;
};

// ---------------------------------------------------------------------------------------------------------------------
MidiToolsPanel::MidiToolsPanel (AudioEngine& e, std::function<Target()> t, std::function<void (const std::vector<int>&)> s)
    : engine (e), target (std::move (t)), select (std::move (s)), seed ((std::uint32_t) juce::Random::getSystemRandom().nextInt())
{
    title.setText (tr ("Transform"), juce::dontSendNotification);
    title.setFont (font (type::small, true));
    title.setColour (juce::Label::textColourId, colour::textSecondary);
    addAndMakeVisible (title);
    for (int i = 0; i < toolCount; ++i)
        chooser.addItem (tr (toolName (i)), i + 1);
    chooser.setSelectedId (tool + 1, juce::dontSendNotification);
    chooser.setTooltip (tr ("MIDI Tools: changes the selected notes, or every note when none is selected"));
    chooser.onChange = [this] { setTool (chooser.getSelectedId() - 1); };
    addAndMakeVisible (chooser);
    resetButton.setButtonText (juce::String::fromUTF8 ("\xe2\x86\xba"));
    resetButton.setTooltip (tr ("Reset the tool's settings"));
    resetButton.setConnectedEdges (0);
    resetButton.onClick = [this] {
        switch (tool)
        {
            case arpeggiate: arp = {}; break;
            case chop:       chopOptions = {}; break;
            case connect:    connectOptions = {}; break;
            case ornament:   ornamentOptions = {}; break;
            case quantize:   quantizeOptions = {}; break;
            case recombine:  recombineOptions = {}; break;
            case span:       spanOptions = {}; break;
            case strum:      strumOptions = {}; break;
            default: break;
        }
        rebuild();
        changed();
    };
    addAndMakeVisible (resetButton);
    autoButton.setButtonText (tr ("Auto"));
    autoButton.setTooltip (tr ("Auto: every change runs the tool again on the notes as they were before it"));
    autoButton.setConnectedEdges (0);
    autoButton.setClickingTogglesState (true);
    autoButton.setColour (juce::TextButton::buttonOnColourId, colour::accent);
    autoButton.setColour (juce::TextButton::textColourOnId, colour::onAccent);
    autoButton.setToggleState (autoApply, juce::dontSendNotification);
    autoButton.onClick = [this] { autoApply = autoButton.getToggleState(); };
    addAndMakeVisible (autoButton);
    transformButton.setButtonText (tr ("Transform"));
    transformButton.setTooltip (tr ("Applies the tool; the next change starts from the notes as they are now"));
    transformButton.setConnectedEdges (0);
    transformButton.onClick = [this] { transformNow(); };
    addAndMakeVisible (transformButton);
    setSize (panelWidth, panelHeight);
    rebuild();
}

MidiToolsPanel::~MidiToolsPanel() = default;

void MidiToolsPanel::setTool (int t)
{
    t = std::clamp (t, 0, (int) toolCount - 1);
    if (t == tool && body != nullptr)
        return;
    tool = t;
    session.valid = false; // (what the tool before did stays)
    chooser.setSelectedId (tool + 1, juce::dontSendNotification);
    rebuild();
}

void MidiToolsPanel::setAuto (bool on)
{
    autoApply = on;
    autoButton.setToggleState (on, juce::dontSendNotification);
}

void MidiToolsPanel::rebuild()
{
    body.reset();
    body = std::make_unique<Body> (*this, std::max (60, getWidth()), std::max (40, panelHeight - 66));
    addAndMakeVisible (*body);
    resized();
}

void MidiToolsPanel::paint (juce::Graphics& g) { g.fillAll (colour::bg2); }

void MidiToolsPanel::resized()
{
    auto r = getLocalBounds();
    title.setBounds (r.removeFromTop (16));
    r.removeFromTop (3);
    auto top = r.removeFromTop (20);
    resetButton.setBounds (top.removeFromLeft (22));
    top.removeFromLeft (4);
    chooser.setBounds (top);
    auto bottom = r.removeFromBottom (21);
    autoButton.setBounds (bottom.removeFromLeft (48));
    transformButton.setBounds (bottom.removeFromRight (84));
    r.removeFromTop (4);
    if (body != nullptr)
        body->setTopLeftPosition (r.getX(), r.getY());
}

void MidiToolsPanel::changed()
{
    if (autoApply)
        run();
}

void MidiToolsPanel::transformNow()
{
    if (! autoApply || ! sessionHolds (target()))
        run();
    session.valid = false;
    seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt(); // (the next random draw differs)
}

bool MidiToolsPanel::sessionHolds (const Target& t) const
{
    const auto& song = engine.getSong();
    return session.valid && session.pattern == t.pattern && t.pattern >= 0 && t.pattern < (int) song.patterns.size()
           && song.patterns[(std::size_t) t.pattern].notes == session.output && t.selected == session.selected
           && (! t.selected || t.indices == session.outIndices);
}

miditools::Notes MidiToolsPanel::apply (const miditools::Notes& notes, std::int64_t grid) const
{
    switch (tool)
    {
        case arpeggiate:
        {
            auto o = arp;
            o.seed = seed;
            return miditools::arpeggiate (notes, o);
        }
        case chop: return miditools::chop (notes, chopOptions);
        case connect:
        {
            auto o = connectOptions;
            o.seed = seed;
            return miditools::connect (notes, o, grid);
        }
        case ornament:
        {
            auto o = ornamentOptions;
            if (const auto key = projectkey::get (engine.getSong()); key && projectkey::lynxScale (*key) > 0)
            {
                o.scaleRoot = key->root; // (High / Low grace notes step by scale degrees when the project has a key)
                o.scale = projectkey::lynxScale (*key);
            }
            return miditools::ornament (notes, o, grid);
        }
        case quantize: return miditools::quantize (notes, quantizeOptions, grid);
        case recombine:
        {
            auto o = recombineOptions;
            o.seed = seed;
            return miditools::recombine (notes, o, grid);
        }
        case span:
        {
            auto o = spanOptions;
            o.seed = seed;
            return miditools::span (notes, o, grid);
        }
        case strum: return miditools::strum (notes, strumOptions, grid);
        default: return notes;
    }
}

void MidiToolsPanel::run()
{
    if (target == nullptr)
        return;
    const auto t = target();
    const auto& song = engine.getSong();
    if (t.pattern < 0 || t.pattern >= (int) song.patterns.size())
        return;
    if (! sessionHolds (t))
    {
        session = {};
        session.valid = true;
        session.pattern = t.pattern;
        session.base = song.patterns[(std::size_t) t.pattern].notes;
        session.indices = t.indices;
        session.selected = t.selected;
    }
    session.grid = std::max<std::int64_t> (1, t.grid);
    if (session.indices.empty())
    {
        session.valid = false;
        return;
    }
    std::vector<int> out;
    engine.editSong ([this, &out] (Song& s) {
        if (session.pattern >= (int) s.patterns.size())
            return;
        auto& p = s.patterns[(std::size_t) session.pattern];
        p.notes = session.base;
        out = miditools::replaceNotes (p, session.indices, apply (miditools::notesAt (p, session.indices), session.grid));
    });
    session.output = engine.getSong().patterns[(std::size_t) session.pattern].notes;
    session.outIndices = out;
    if (session.selected && select != nullptr)
        select (out);
    if (body != nullptr)
        body->repaint();
}
} // namespace lynx
