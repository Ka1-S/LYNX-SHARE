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

// Chop's pattern row [LIVE round 190aa, live/x-H04.png]: one cell per part (16 at most), the upper half lit when the
// part plays, the lower half lit when it is emphasised (it then lasts `stretch` parts). Click either half.
class ChopPatternRow final : public juce::Component
{
public:
    ChopPatternRow (miditools::ChopOptions& o, std::function<void()> onChange) : options (o), changed (std::move (onChange))
    {
        ui::setHint (*this, tr ("Upper row: the part plays or is a gap. Lower row: the part lasts as long as Stretch says"));
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::bg0);
        g.fillRect (getLocalBounds());
        const auto n = cells();
        const auto w = (float) getWidth() / (float) n, h = (float) getHeight() * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const auto plays = miditools::chopPlays (options, i);
            const auto stressed = (options.emphasis >> i & 1u) != 0;
            const juce::Rectangle<float> top ((float) i * w + 1.0f, 1.0f, w - 2.0f, h - 1.5f), bottom ((float) i * w + 1.0f, h + 0.5f, w - 2.0f, h - 1.5f);
            g.setColour (plays ? (stressed ? colour::accentPressed : colour::accent) : (stressed ? colour::accentMuted : colour::bg4));
            g.fillRect (top);
            g.setColour (stressed ? colour::automation : colour::bg4.interpolatedWith (colour::automation, 0.18f));
            g.fillRect (bottom);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto n = cells();
        const auto i = std::clamp (e.x * n / std::max (1, getWidth()), 0, n - 1);
        if (e.y < getHeight() / 2)
        {
            if (! options.custom) // (the pattern Gaps made becomes the hand-made one)
                options.pattern = miditools::chopPattern (std::clamp (options.parts, 2, 64), options.gaps);
            options.custom = true;
            options.pattern = (std::uint16_t) (options.pattern ^ (1u << i));
        }
        else
            options.emphasis = (std::uint16_t) (options.emphasis ^ (1u << i));
        repaint();
        changed();
    }

private:
    int cells() const noexcept { return std::min (std::clamp (options.parts, 2, 64), 16); }
    miditools::ChopOptions& options;
    std::function<void()> changed;
};

// Time Warp's display [LIVE round 190aa, live/x-W04.png]: the range's ends as two lines, the speed line through the
// breakpoints that are on (up = faster, x0.25 … x4 on a log scale), each breakpoint a numbered circle. Click one to
// select it; drag it sideways for its time (1 and 3 stay at the ends) and up / down for its speed.
class TimeWarpCurve final : public juce::Component
{
public:
    TimeWarpCurve (miditools::TimeWarpOptions& o, int& sel, std::function<void()> onChange) : options (o), selected (sel), changed (std::move (onChange))
    {
        ui::setHint (*this, tr ("Drag a breakpoint: sideways for its time, up or down for its speed"));
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colour::bg0);
        g.fillRect (r);
        const auto a = area();
        g.setColour (colour::border);
        g.fillRect (a.getX(), a.getCentreY(), a.getWidth(), 1.0f);
        g.setColour (colour::automation);
        g.fillRect (a.getX(), r.getY() + 2.0f, 1.0f, r.getHeight() - 4.0f);
        g.fillRect (a.getRight() - 1.0f, r.getY() + 2.0f, 1.0f, r.getHeight() - 4.0f);
        juce::Path line;
        bool started = false;
        for (int i = 0; i < 3; ++i)
            if (options.points[i].on)
            {
                const auto p = at (i);
                if (! started)
                    line.startNewSubPath (a.getX(), p.y);
                line.lineTo (p);
                started = true;
            }
        if (started)
        {
            line.lineTo (a.getRight(), line.getCurrentPosition().y);
            g.setColour (colour::automation);
            g.strokePath (line, juce::PathStrokeType (1.2f));
        }
        g.setFont (font (type::caption, true));
        for (int i = 0; i < 3; ++i)
            if (options.points[i].on)
            {
                const auto c = juce::Rectangle<float> (11.0f, 11.0f).withCentre (at (i));
                g.setColour (i == selected ? colour::accent : colour::bg3);
                g.fillEllipse (c);
                g.setColour (colour::accent);
                g.drawEllipse (c, 1.0f);
                g.setColour (i == selected ? colour::onAccent : colour::textPrimary);
                g.drawText (juce::String (i + 1), c.toNearestInt(), juce::Justification::centred);
            }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto best = -1;
        auto distance = 1.0e9f;
        for (int i = 0; i < 3; ++i)
            if (options.points[i].on)
                if (const auto d = at (i).getDistanceFrom (e.position); d < distance)
                {
                    distance = d;
                    best = i;
                }
        if (best >= 0)
            selected = best;
        changed();
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (selected < 0 || selected > 2 || ! options.points[selected].on)
            return;
        const auto a = area();
        auto& p = options.points[selected];
        if (selected == 1)
            p.time = juce::jlimit (0.05, 0.95, (double) ((e.position.x - a.getX()) / a.getWidth()));
        const auto octaves = juce::jlimit (-2.0, 2.0, (double) ((a.getCentreY() - e.position.y) / (a.getHeight() * 0.5f)) * 2.0);
        p.speed = std::round (std::pow (2.0, octaves) * 10.0) / 10.0;
        changed();
        repaint();
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (8.0f, 7.0f); }
    juce::Point<float> at (int i) const
    {
        const auto a = area();
        const auto& p = options.points[i];
        const auto y = (float) (std::log2 (std::clamp (p.speed, 0.25, 4.0)) / 2.0);
        return { a.getX() + (float) p.time * a.getWidth(), a.getCentreY() - y * a.getHeight() * 0.5f };
    }
    miditools::TimeWarpOptions& options;
    int& selected;
    std::function<void()> changed;
};

// Velocity Shaper's envelope [LIVE round 190aa, live/x-V01.png]: the shape over one cycle, top = Maximum, bottom =
// Minimum. Drag a point; click elsewhere to add one; double-click a point to remove it; Alt + drag a line to bend it.
class ShaperEnvelope final : public juce::Component
{
public:
    ShaperEnvelope (miditools::VelocityShaperOptions& o, std::function<void()> onChange) : options (o), changed (std::move (onChange))
    {
        ui::setHint (*this, tr ("Drag a point; click to add one, double-click to remove it; Alt + drag a line to bend it"));
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::bg0);
        g.fillRect (getLocalBounds());
        const auto a = area();
        g.setColour (colour::border);
        g.fillRect (a.getCentreX(), a.getY(), 1.0f, a.getHeight());
        g.fillRect (a.getX(), a.getCentreY(), a.getWidth(), 1.0f);
        juce::Path path;
        auto started = ! options.points.empty();
        if (started)
            path.startNewSubPath (toScreen (options.points.front().x, options.points.front().y)); // (a jump at the start shows as an edge)
        for (int i = 0; i <= 96; ++i)
        {
            const auto x = (double) i / 96.0;
            const juce::Point<float> p { a.getX() + (float) x * a.getWidth(), a.getBottom() - (float) miditools::velocityShape (options, x) * a.getHeight() };
            if (! started)
                path.startNewSubPath (p);
            else
                path.lineTo (p);
            started = true;
        }
        g.setColour (colour::automation);
        g.strokePath (path, juce::PathStrokeType (1.4f));
        for (const auto& p : options.points)
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (toScreen (p.x, p.y)));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = nearest (e.position);
        bending = e.mods.isAltDown();
        if (bending)
        {
            segment = segmentAt (e.position.x);
            startCurve = segment >= 0 ? options.points[(std::size_t) segment].curve : 0.0f;
            return;
        }
        if (dragging < 0) // add a point on the line
        {
            const auto [x, y] = fromScreen (e.position);
            auto it = std::upper_bound (options.points.begin(), options.points.end(), x, [] (double v, const auto& p) { return v < p.x; });
            dragging = (int) (options.points.insert (it, { x, y, 0.0f }) - options.points.begin());
            changed();
            repaint();
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (bending)
        {
            if (segment >= 0)
                options.points[(std::size_t) segment].curve = juce::jlimit (-8.0f, 8.0f, startCurve + (float) e.getDistanceFromDragStartY() * 0.08f);
        }
        else if (dragging >= 0)
        {
            auto [x, y] = fromScreen (e.position);
            const auto i = (std::size_t) dragging;
            const auto lo = i == 0 ? 0.0 : options.points[i - 1].x, hi = i + 1 >= options.points.size() ? 1.0 : options.points[i + 1].x;
            options.points[i].x = i == 0 ? 0.0 : i + 1 == options.points.size() ? 1.0 : juce::jlimit (lo, hi, x);
            options.points[i].y = y;
        }
        changed();
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto i = nearest (e.position);
        if (i > 0 && i + 1 < (int) options.points.size())
        {
            options.points.erase (options.points.begin() + i);
            changed();
            repaint();
        }
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (6.0f, 6.0f); }
    juce::Point<float> toScreen (double x, double y) const
    {
        const auto a = area();
        return { a.getX() + (float) x * a.getWidth(), a.getBottom() - (float) y * a.getHeight() };
    }
    std::pair<double, double> fromScreen (juce::Point<float> p) const
    {
        const auto a = area();
        return { juce::jlimit (0.0, 1.0, (double) ((p.x - a.getX()) / a.getWidth())), juce::jlimit (0.0, 1.0, (double) ((a.getBottom() - p.y) / a.getHeight())) };
    }
    int nearest (juce::Point<float> p) const
    {
        for (std::size_t i = 0; i < options.points.size(); ++i)
            if (toScreen (options.points[i].x, options.points[i].y).getDistanceFrom (p) < 6.0f)
                return (int) i;
        return -1;
    }
    int segmentAt (float screenX) const
    {
        const auto x = fromScreen ({ screenX, 0.0f }).first;
        for (std::size_t i = 0; i + 1 < options.points.size(); ++i)
            if (x >= options.points[i].x && x <= options.points[i + 1].x)
                return (int) i;
        return -1;
    }
    miditools::VelocityShaperOptions& options;
    std::function<void()> changed;
    int dragging = -1, segment = -1;
    bool bending = false;
    float startCurve = 0.0f;
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
        case timeWarp:   return TR_NOOP ("Time Warp");
        case velocityShaper: return TR_NOOP ("Velocity Shaper");
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
            case timeWarp:   buildTimeWarp(); break;
            case velocityShaper: buildVelocityShaper(); break;
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
    // Live 12.4's Chop [LIVE round 190aa, live/x-H04.png]: Parts and Gaps, the pattern row, Stretch, Variation
    void buildChop()
    {
        auto& o = panel.chopOptions;
        const auto w = getWidth(), half = w / 2;
        label (tr ("Stretch"), { 2, 90, half, 16 });
        auto* stretch = &box (tr ("Stretch"), { w - 40, 90, 40, 16 }, makeSpec ({}, 2, 8, 2, 1.0, [] (double v) { return juce::String::fromUTF8 ("\xc3\x97") + whole (v); }),
                              [&o] { return (double) o.stretch; }, [&o] (double v) { o.stretch = juce::roundToInt (v); });
        stretch->setAlpha (o.emphasis != 0 ? 1.0f : 0.45f); // [LIVE] greyed while nothing is emphasised
        auto* row = &add (std::make_unique<ChopPatternRow> (o, [this, stretch] {
                              stretch->setAlpha (panel.chopOptions.emphasis != 0 ? 1.0f : 0.45f);
                              panel.changed();
                          }), { 2, 60, w - 4, 26 });
        knob (tr ("Parts"), { 0, 0, half, 58 }, makeSpec ({}, 2, 64, 4, 1.0, whole), [&o] { return (double) o.parts; },
              [&o, row] (double v) { o.parts = juce::roundToInt (v); row->repaint(); });
        knob (tr ("Gaps"), { half, 0, half, 58 }, makeSpec ({}, -15, 15, 0, 1.0, [&o] (double v) { return o.custom ? juce::String ("-") : whole (v); }),
              [&o] { return (double) o.gaps; }, [&o, row] (double v) {
                  o.gaps = juce::roundToInt (v);
                  o.custom = false; // [LIVE] turning Gaps replaces the hand-made pattern
                  row->repaint();
              });
        label (tr ("Variation"), { 2, 108, half, 16 });
        box (tr ("Variation"), { w - 54, 108, 54, 16 }, makeSpec ({}, 0.0, 1.0, 0.0, 0.0, percent1), [&o] { return (double) o.variation; }, [&o] (double v) { o.variation = (float) v; });
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
    // Live 12.4's Quantize [LIVE round 190aa, live/x-Q07.png]: with Triplets on the four values read 1/8T, 1/8 + 1/8T,
    // 1/16T, 1/16 + 1/16T; Triplets is greyed while Grid is chosen
    static int quantizeCell (const miditools::QuantizeOptions& o) noexcept
    {
        if (o.step <= 0)
            return -1;
        if (! o.triplets)
        {
            for (int i = 0; i < 4; ++i)
                if (o.step == (std::int64_t) (ppq * 4 / (4 << i)))
                    return i;
            return -1;
        }
        return (o.step == ppq / 2 ? 0 : 2) + (o.straightToo ? 1 : 0);
    }
    static void setQuantizeCell (miditools::QuantizeOptions& o, int cell, bool triplets) noexcept
    {
        o.triplets = triplets && cell >= 0;
        o.straightToo = o.triplets && (cell % 2) == 1;
        o.step = cell < 0 ? 0 : o.triplets ? (cell < 2 ? ppq / 2 : ppq / 4) : (std::int64_t) (ppq * 4 / (4 << cell));
    }
    void buildQuantize()
    {
        auto& o = panel.quantizeOptions;
        const auto w = getWidth(), half = (w - 4) / 2;
        const auto relabel = [this] { // the labels change with Triplets: build the page again once the click is over
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MidiToolsPanel> (&panel)] {
                if (safe != nullptr)
                    safe->rebuild();
            });
        };
        toggle (tr ("Grid"), { 0, 0, w, 17 }, o.step == 0, [&o, relabel] (bool) { setQuantizeCell (o, -1, false); relabel(); }, 1902);
        const char* straight[] = { "1/4", "1/8", "1/16", "1/32" };
        const char* triplet[] = { "1/8T", "1/8 + 1/8T", "1/16T", "1/16 + 1/16T" };
        const auto cell = quantizeCell (o);
        for (int i = 0; i < 4; ++i)
            toggle (o.triplets ? triplet[i] : straight[i], { (i % 2) * (half + 4), 20 + (i / 2) * 20, half, 17 }, cell == i,
                    [&o, i] (bool) { setQuantizeCell (o, i, o.triplets); }, 1902);
        auto& triplets = toggle (tr ("Triplets"), { 0, 60, w, 17 }, o.triplets, [&o, relabel] (bool on) { setQuantizeCell (o, quantizeCell (o), on); relabel(); });
        triplets.setEnabled (cell >= 0);
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

    // Live 12.4's Time Warp [LIVE round 190aa, live/x-W04.png]: Time and Speed of the selected breakpoint, the display,
    // breakpoints 1 2 3 on / off, Quantize, Range (Fit), Note End (Include)
    void buildTimeWarp()
    {
        auto& o = panel.timeWarpOptions;
        const auto w = getWidth(), third = (w - 4) / 3;
        auto& sel = panel.warpSelected;
        label (tr ("Time"), { 0, 0, 30, 14 });
        box (tr ("Time"), { 30, 0, 46, 14 }, makeSpec ({}, 0.0, 1.0, 0.5, 0.0, [this] (double v) { return panel.warpTimeText (v); }),
             [&o, &sel] { return o.points[std::clamp (sel, 0, 2)].time; }, [&o, &sel] (double v) {
                 if (sel == 1) // (1 and 3 sit at the range's ends)
                     o.points[1].time = juce::jlimit (0.05, 0.95, v);
             });
        label (tr ("Speed"), { w - 80, 0, 36, 14 });
        box (tr ("Speed"), { w - 40, 0, 40, 14 }, makeSpec ({}, 0.25, 4.0, 1.0, 0.1, [] (double v) { return juce::String::fromUTF8 ("\xc3\x97") + juce::String (v, 1); }),
             [&o, &sel] { return o.points[std::clamp (sel, 0, 2)].speed; }, [&o, &sel] (double v) { o.points[std::clamp (sel, 0, 2)].speed = v; });
        juce::Component* display = &add (std::make_unique<TimeWarpCurve> (o, sel, [this] { repaint(); panel.changed(); }), { 0, 17, w, 52 });
        const auto redraw = [this, display] {
            display->repaint();
            repaint();
        };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = toggle (juce::String (i + 1), { i * (third + 2), 72, third, 14 }, o.points[i].on, [&o, &sel, i, redraw] (bool on) {
                o.points[i].on = on;
                if (on)
                    sel = i;
                redraw();
            });
            b.setColour (juce::TextButton::buttonOnColourId, colour::automation);
        }
        label (tr ("Quantize"), { 0, 90, third + 2, 12 }, juce::Justification::centred);
        label (tr ("Range"), { third + 2, 90, third + 2, 12 }, juce::Justification::centred);
        label (tr ("Note End"), { 2 * (third + 2), 90, third + 2, 12 }, juce::Justification::centred);
        auto* quantizeButton = &toggle (o.quantize ? tr ("On") : tr ("Off"), { 0, 104, third, 16 }, o.quantize, [&o] (bool on) { o.quantize = on; });
        quantizeButton->onStateChange = [quantizeButton] { quantizeButton->setButtonText (quantizeButton->getToggleState() ? tr ("On") : tr ("Off")); };
        toggle (tr ("Fit"), { third + 2, 104, third, 16 }, o.fit, [&o] (bool on) { o.fit = on; });
        toggle (tr ("Include"), { 2 * (third + 2), 104, third, 16 }, o.includeEnd, [&o] (bool on) { o.includeEnd = on; });
    }
    // Live 12.4's Velocity Shaper [LIVE round 190aa, live/x-V01.png]: the envelope with Maximum (top) and Minimum (bottom)
    // beside it, then Loop, Rotate, Division
    void buildVelocityShaper()
    {
        auto& o = panel.shaperOptions;
        const auto w = getWidth(), third = (w - 4) / 3;
        auto* envelope = &add (std::make_unique<ShaperEnvelope> (o, [this] { panel.changed(); }), { 0, 0, w - 36, 92 });
        box (tr ("Maximum"), { w - 32, 4, 32, 16 }, makeSpec ({}, 1, 127, 127, 1.0, whole), [&o] { return (double) o.maximum; }, [&o] (double v) { o.maximum = juce::roundToInt (v); });
        box (tr ("Minimum"), { w - 32, 72, 32, 16 }, makeSpec ({}, 1, 127, 1, 1.0, whole), [&o] { return (double) o.minimum; }, [&o] (double v) { o.minimum = juce::roundToInt (v); });
        juce::ignoreUnused (envelope);
        label (tr ("Loop"), { 0, 94, third, 12 }, juce::Justification::centred);
        label (tr ("Rotate"), { third + 2, 94, third, 12 }, juce::Justification::centred);
        label (tr ("Division"), { 2 * (third + 2), 94, third, 12 }, juce::Justification::centred);
        box (tr ("Loop"), { 0, 107, third, 16 }, makeSpec ({}, 1, 16, 1, 1.0, whole), [&o] { return (double) o.loops; }, [&o] (double v) { o.loops = juce::roundToInt (v); });
        box (tr ("Rotate"), { third + 2, 107, third, 16 }, makeSpec ({}, -16, 16, 0, 1.0, whole), [&o] { return (double) o.rotate; }, [&o] (double v) { o.rotate = juce::roundToInt (v); });
        choice ({ 2 * (third + 2), 107, third, 16 }, { "1/16", "1/8", tr ("Grid") }, o.division, [&o] (int i) { o.division = i; });
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
            case timeWarp:   timeWarpOptions = {}; warpSelected = 0; break;
            case velocityShaper: shaperOptions = {}; break;
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

juce::String MidiToolsPanel::warpTimeText (double share) const
{
    // the breakpoint's place as Live writes it, bar.beat.sixteenth within the span the tool would use now
    miditools::Notes notes;
    if (target != nullptr)
    {
        const auto t = target();
        const auto& song = engine.getSong();
        if (t.pattern >= 0 && t.pattern < (int) song.patterns.size())
            notes = miditools::notesAt (song.patterns[(std::size_t) t.pattern], t.indices);
    }
    const auto [from, to] = miditools::timeWarpRange (notes, timeWarpOptions);
    const auto ticks = from + (std::int64_t) std::llround (std::clamp (share, 0.0, 1.0) * (double) (to - from));
    const auto sixteenths = ticks / (ppq / 4);
    return juce::String (sixteenths / 16 + 1) + "." + juce::String (sixteenths / 4 % 4 + 1) + "." + juce::String (sixteenths % 4 + 1);
}

void MidiToolsPanel::setLiveShotSettingsForSnapshot()
{
    chopOptions.emphasis = 0x1;
    quantizeOptions.step = ppq / 2;
    quantizeOptions.triplets = quantizeOptions.straightToo = true;
    timeWarpOptions.points[1].on = true;
    timeWarpOptions.points[1].speed = 2.0;
    warpSelected = 1;
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
        case chop:
        {
            auto o = chopOptions;
            o.seed = seed;
            return miditools::chop (notes, o);
        }
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
        case timeWarp: return miditools::timeWarp (notes, timeWarpOptions, grid);
        case velocityShaper: return miditools::velocityShaper (notes, shaperOptions, grid);
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
