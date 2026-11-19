// The Helix editor (ui/HelixEditor.h): Live's Wavetable layout (round 190ja shots), drawn by LYNX code.

#include "ui/HelixEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/Song.h"
#include "core/WavetableSynth.h"
#include "engine/AudioEngine.h"
#include "ui/DetailView.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
namespace
{
namespace w = wt;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
// an automatable parameter by its engine index (Live's 0 … 99, then Helix+'s after Live's state; round 190mi)
const EngineParam& paramOf (int index)
{
    const auto* p = findEngineParam (helixEngineParams(), index);
    return p != nullptr ? *p : helixEngineParams().front();
}
constexpr const char* modDragPrefix = "helixmod:"; // a modulation source dragged onto a knob (Helix+, round 190mi)

// --- the two looks ----------------------------------------------------------------------------------------------------
struct Look
{
    juce::Colour outer, bg, dark, line, text, dim, accent, accent2, valueBox;
};
// Live 12's dark theme as the shots show it [SHOT 190ja live-wt-*.png: #363636 sections, #181818 displays, #ffad56
// values, #03c3d5 envelopes, #b5b5b5 text]
Look liveLook() { return { juce::Colour (0xff2a2a2a), juce::Colour (0xff363636), juce::Colour (0xff181818), juce::Colour (0xff464646),
                           juce::Colour (0xffb5b5b5), juce::Colour (0xff868686), juce::Colour (0xffffad56), juce::Colour (0xff03c3d5),
                           juce::Colour (0xff1e1e1e) }; }
// LYNX's own (the channel window): its palette tokens, read when painting (user themes)
Look lynxLook() { return { theme::colour::bg1, theme::colour::bg2, theme::colour::bg0, theme::colour::border, theme::colour::textPrimary,
                           theme::colour::textSecondary, theme::colour::accent, theme::colour::automation, theme::colour::bg1 }; }

enum class Curve { linear, freq, time, gain, rate };

double toValue (Curve c, double x, const EngineParam& p)
{
    x = std::clamp (x, 0.0, 1.0);
    switch (c)
    {
        case Curve::freq: return w::filterHz (x);
        case Curve::time: return x <= 0.0 ? 0.0 : std::max ((double) p.low, 0.0001 * std::pow (200000.0, x)); // 0.1 ms … 20 s
        case Curve::gain: return x <= 0.0 ? 0.0 : std::pow (10.0, (70.0 * x - 70.0) / 20.0);                 // −70 … 0 dB
        case Curve::rate: return 0.01 * std::pow (3000.0, x);                                                 // 0.01 … 30 Hz
        case Curve::linear: break;
    }
    const auto v = p.low + x * (p.high - p.low);
    return p.stepped ? std::round (v) : v;
}

double toPosition (Curve c, double v, const EngineParam& p)
{
    switch (c)
    {
        case Curve::freq: return w::filterNorm (v);
        case Curve::time: return v <= 0.0001 ? 0.0 : std::clamp (std::log (v / 0.0001) / std::log (200000.0), 0.0, 1.0);
        case Curve::gain: return v <= 0.000317 ? 0.0 : std::clamp ((20.0 * std::log10 (v) + 70.0) / 70.0, 0.0, 1.0);
        case Curve::rate: return std::clamp (std::log (std::max (v, 0.01) / 0.01) / std::log (3000.0), 0.0, 1.0);
        case Curve::linear: break;
    }
    return p.high > p.low ? std::clamp ((v - p.low) / (p.high - p.low), 0.0, 1.0) : 0.0;
}

// --- the page's shared state ------------------------------------------------------------------------------------------
struct Page
{
    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel = -1;
    bool live = false;
    int osc = 0;      // the oscillator shown (0 / 1)
    int modTab = 0;   // Mod Sources, Matrix, MIDI, MPE
    int source = 0;   // Amp, Env 2, Env 3, LFO 1, LFO 2
    int envView = 0;  // Time / Slope / Value
    std::function<void()> refresh;

    Look look() const { return live ? liveLook() : lynxLook(); }
    const ChannelDef* def() const
    {
        const auto* c = locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr;
        return c != nullptr && c->instrument == InstrumentKind::helix ? c : nullptr;
    }
    double value (int index) const
    {
        if (channel >= 0 && w::automatable (index))
            if (const auto v = const_cast<AudioEngine&> (engine).getAutomationDisplay().valueOf ({ AutomationTargetKind::channelParam, channel, 0, index }))
                return paramOf (index).raw (*v);
        if (const auto* c = def(); c != nullptr && index < (int) c->modelParams.size())
            return c->modelParams[(std::size_t) index];
        return w::defaults()[(std::size_t) index];
    }
    void set (int index, double v, bool undoable = true)
    {
        engine.editSong ([l = locate, index, v] (Song& s) {
            auto* c = l ? l (s) : nullptr;
            if (c == nullptr || c->instrument != InstrumentKind::helix)
                return;
            padHelixState (*c, index >= w::extBase); // (a Helix+ value makes the block live)
            c->modelParams[(std::size_t) index] = (float) v;
        }, undoable);
    }
    juce::String name() const
    {
        const auto* c = def();
        return c != nullptr && ! c->name.empty() ? u8 (c->name) : juce::String ("Helix");
    }
    int oscParam (int p) const { return w::oscBase (osc) + p; }
    double xv (int extIndex) const { return value (w::extBase + extIndex); }
    bool plusLive() const { return value (w::extBase) >= 0.5; }
    // the summed modulation amounts on a matrix row (the knob's ring)
    double rowAmount (int row) const
    {
        double a = 0.0;
        for (int src = 0; src < w::allSourceCount; ++src)
            if (row < w::targetCount || ! live)
                if (src < w::sourceCount || ! live)
                    a += value (w::cellIndex (row, src));
        return a;
    }
};

void caption (juce::Graphics& g, const Look& l, juce::Rectangle<int> r, const juce::String& text, juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (l.text);
    g.setFont (theme::font (10.5f));
    g.drawFittedText (text, r, j, 1, 0.7f);
}

// --- one parameter's control: a knob, a value box or a slider ---------------------------------------------------------
class Ctl final : public juce::Component, public ui::HintSource, public juce::DragAndDropTarget
{
public:
    enum class Kind { knob, box, vslider };
    Ctl (Page& p, int i, Kind k, juce::String cap = {}, Curve c = Curve::linear) : page (p), index (i), kind (k), curve (c), label (std::move (cap))
    {
        setRepaintsOnMouseActivity (true);
    }
    void setIndex (int i) { index = i; repaint(); }
    void setLabel (juce::String s) { label = std::move (s); repaint(); }
    const EngineParam& param() const { return paramOf (index); }

    ui::ValueSpec spec() const
    {
        ui::ValueSpec s;
        s.name = page.name() + " - " + u8 (param().name);
        s.minimum = 0.0;
        s.maximum = 1.0;
        s.defaultValue = toPosition (curve, w::defaults()[(std::size_t) index], param());
        s.interval = param().stepped && curve == Curve::linear ? 1.0 / std::max (1.0, (double) (param().high - param().low)) : 0.0;
        s.pixelsForRange = param().stepped ? std::clamp (8.0 * (param().high - param().low), 60.0, 300.0) : 180.0;
        const auto i = index;
        const auto c = curve;
        s.toText = [i, c] (double x) { return u8 (w::paramText (i, toValue (c, x, paramOf (i)))); };
        s.fromText = ui::inverseOf (s.toText, 0.0, 1.0);
        if (page.channel >= 0)
            s.target = AutomationTarget { AutomationTargetKind::channelParam, page.channel, 0, index };
        return s;
    }
    double position() const { return toPosition (curve, page.value (index), param()); }
    void setPosition (double x)
    {
        page.set (index, toValue (curve, x, param()));
        repaint();
    }
    ui::Hint hintAt (juce::Point<int>) override { return ui::valueHint (spec(), position(), ui::valueGestures()); }

    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        auto r = getLocalBounds();
        const auto text = spec().text (position());
        const bool hot = isMouseOverOrDragging();
        if (kind == Kind::box)
        {
            // Live's value box: caption above (if any), the value in a dark box, accent text
            if (label.isNotEmpty())
                caption (g, l, r.removeFromTop (13), label);
            g.setColour (hot ? l.line : l.valueBox);
            g.fillRect (r.reduced (0, 1));
            g.setColour (l.accent);
            g.setFont (theme::font (10.5f));
            g.drawFittedText (text, r, juce::Justification::centred, 1, 0.7f);
            return;
        }
        if (kind == Kind::vslider)
        {
            const auto track = r.reduced (std::max (0, r.getWidth() / 2 - 2), 2);
            g.setColour (l.line);
            g.fillRect (track);
            const auto y = track.getBottom() - (int) std::lround (position() * track.getHeight());
            g.setColour (l.accent);
            g.fillRect (track.withTop (y));
            g.fillRect (juce::Rectangle<int> (r.getX(), y - 1, r.getWidth(), 3));
            return;
        }
        // knob: caption above, an arc ring with the value arc, the value below
        if (label.isNotEmpty())
            caption (g, l, r.removeFromTop (13), label, juce::Justification::centred);
        const auto valueRow = r.removeFromBottom (13);
        const auto size = (float) std::min (r.getWidth(), r.getHeight()) - 4.0f;
        const auto c = r.toFloat().getCentre();
        constexpr float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, size * 0.5f, size * 0.5f, 0.0f, start, end, true);
        g.setColour (l.line);
        g.strokePath (ring, juce::PathStrokeType (2.5f));
        const bool bipolar = param().low < 0.0f && param().high > 0.0f && curve == Curve::linear;
        const auto a0 = bipolar ? (start + end) * 0.5f : start;
        const auto a1 = start + (end - start) * (float) position();
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, size * 0.5f, size * 0.5f, 0.0f, std::min (a0, a1), std::max (a0, a1), true);
        const auto arcColour = page.live ? l.accent2 : l.accent; // (Live: cyan arcs [SHOT live-wt-preset-*])
        g.setColour (hot ? arcColour.brighter (0.2f) : arcColour);
        g.strokePath (arc, juce::PathStrokeType (2.5f));
        // Helix+ (LYNX's look): the modulation on this knob as a thin outer ring from the value (round 190mi)
        if (! page.live)
            if (const auto row = w::targetOfParam (index); row >= 0)
                if (const auto amount = std::clamp (page.rowAmount (row), -1.0, 1.0); amount != 0.0)
                {
                    const auto b1 = std::clamp (a1 + (end - start) * (float) amount, start, end);
                    juce::Path mod;
                    mod.addCentredArc (c.x, c.y, size * 0.5f + 3.0f, size * 0.5f + 3.0f, 0.0f, std::min (a1, b1), std::max (a1, b1), true);
                    g.setColour (l.accent2);
                    g.strokePath (mod, juce::PathStrokeType (1.6f));
                }
        if (dropHot)
        {
            g.setColour (l.accent2.withAlpha (0.5f));
            g.drawEllipse (c.x - size * 0.5f - 4.0f, c.y - size * 0.5f - 4.0f, size + 8.0f, size + 8.0f, 1.5f);
        }
        const auto tip = c.getPointOnCircumference (size * 0.5f - 1.0f, a1);
        g.drawLine ({ c.getPointOnCircumference (size * 0.15f, a1), tip }, 1.6f);
        g.setColour (l.text);
        g.setFont (theme::font (10.0f));
        g.drawFittedText (text, valueRow, juce::Justification::centred, 1, 0.7f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &page.engine, spec(), position(), [this] (double v) { setPosition (v); });
            return;
        }
        if (ui::isResetClick (e))
        {
            setPosition (spec().defaultValue);
            return;
        }
        drag.begin (e, position());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag.isActive())
            return;
        const auto s = spec();
        setPosition (drag.update (e, s));
        ui::reportTweak (s);
        ui::showValueTip (*this, getLocalBounds(), s.text (position()));
        if (page.refresh)
            page.refresh();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        drag.end();
        ui::hideValueTip();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { setPosition (spec().defaultValue); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wh) override
    {
        if (const auto v = ui::wheelValue (position(), e, wh, spec()))
        {
            setPosition (*v);
            ui::flashValueTip (*this, getLocalBounds(), spec().text (position()));
            if (page.refresh)
                page.refresh();
        }
    }

    // a modulation source dropped on the knob (LYNX's look): the source modulates the knob's row by 50 %, or the
    // amount it already has; the knob shows the ring, the matrix the cell (round 190mi)
    static int sourceOf (const SourceDetails& d)
    {
        const auto t = d.description.toString();
        return t.startsWith (modDragPrefix) ? t.fromFirstOccurrenceOf (modDragPrefix, false, false).getIntValue() : -1;
    }
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const auto src = sourceOf (d);
        return ! page.live && src >= 0 && src < w::allSourceCount && w::targetOfParam (index) >= 0;
    }
    void itemDragEnter (const SourceDetails&) override { dropHot = true, repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHot = false, repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHot = false;
        const auto src = sourceOf (d), row = w::targetOfParam (index);
        if (src < 0 || row < 0)
            return;
        const auto cell = w::cellIndex (row, src);
        if (page.value (cell) == 0.0)
            page.set (cell, 0.5);
        ui::flashValueTip (*this, getLocalBounds(), tr (u8 (w::sourceName (src))) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + tr (u8 (w::targetName (row))));
        if (page.refresh)
            page.refresh();
        repaint();
    }

private:
    Page& page;
    int index;
    Kind kind;
    Curve curve;
    juce::String label;
    ui::ValueDrag drag;
    bool dropHot = false;
};

// --- a menu of a stepped parameter (or of anything, through callbacks) -------------------------------------------------
class Menu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Menu (Page& p, std::function<juce::String()> t, std::function<void (juce::PopupMenu&)> b) : page (p), text (std::move (t)), build (std::move (b))
    {
        setRepaintsOnMouseActivity (true);
    }
    static std::unique_ptr<Menu> of (Page& p, std::function<int()> index)
    {
        return std::make_unique<Menu> (
            p, [&p, index] { return tr (u8 (w::paramText (index(), p.value (index())))); },
            [&p, index] (juce::PopupMenu& m) {
                const auto i = index();
                const auto& par = paramOf (i);
                const auto current = (int) std::lround (p.value (i));
                for (int v = (int) par.low; v <= (int) par.high; ++v)
                    m.addItem (tr (u8 (w::paramText (i, (double) v))), true, v == current, [&p, i, v] {
                        p.set (i, (double) v);
                        if (p.refresh)
                            p.refresh();
                    });
            });
    }
    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        auto r = getLocalBounds();
        if (isMouseOver())
        {
            g.setColour (l.line);
            g.fillRect (r);
        }
        g.setColour (l.accent);
        g.setFont (theme::font (10.5f));
        g.drawFittedText (text() + juce::String::fromUTF8 (" \xe2\x96\xbe"), r.reduced (3, 0), juce::Justification::centredLeft, 1, 0.7f);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m;
        build (m);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

private:
    Page& page;
    std::function<juce::String()> text;
    std::function<void (juce::PopupMenu&)> build;
};

// --- a tab or a switch button ------------------------------------------------------------------------------------------
class Tab final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Tab (Page& p, juce::String t, std::function<bool()> on, std::function<void()> click) : page (p), text (std::move (t)), isOn (std::move (on)), onClick (std::move (click))
    {
        setRepaintsOnMouseActivity (true);
    }
    bool switchStyle = false; // a switch (Sub, Poly, the octave): filled with the accent when on, as Live draws them
    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        const auto on = isOn();
        if (text.isEmpty()) // an on / off square (Live: lit in the accent colour when on)
        {
            const auto r = getLocalBounds().toFloat().reduced (1.5f);
            g.setColour (on ? l.accent : l.dark);
            g.fillRect (r);
            g.setColour (on ? l.accent.brighter (0.2f) : l.dim);
            g.drawRect (r, 1.0f);
            return;
        }
        if (switchStyle)
        {
            g.setColour (on ? l.accent : l.dark);
            g.fillRect (getLocalBounds().reduced (1));
            g.setColour (on ? l.dark : l.text);
            g.setFont (theme::font (10.5f));
            g.drawFittedText (text, getLocalBounds(), juce::Justification::centred, 1, 0.7f);
            return;
        }
        g.setColour (on ? l.line : isMouseOver() ? l.bg.brighter (0.08f) : l.bg);
        g.fillRect (getLocalBounds().reduced (1));
        g.setColour (on ? l.accent : l.text);
        g.setFont (theme::font (10.5f));
        g.drawFittedText (text, getLocalBounds(), juce::Justification::centred, 1, 0.7f);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        onClick();
        if (page.refresh)
            page.refresh();
    }
    // a modulation source (LYNX's look): drag it onto a knob (Helix+, round 190mi)
    int dragSource = -1;
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragSource < 0 || page.live || e.getDistanceFromDragStart() < 5)
            return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this); c != nullptr && ! c->isDragAndDropActive())
            c->startDragging (juce::String (modDragPrefix) + juce::String (dragSource), this);
    }

private:
    Page& page;
    juce::String text;
    std::function<bool()> isOn;
    std::function<void()> onClick;
};

// --- the oscillator's table: its frames stacked (the playing one lit); drag up / down moves the position ---------------
class WaveDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveDisplay (Page& p) : page (p) { setTooltip (tr ("Wave table: drag up / down to move the position")); }
    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.setColour (l.dark);
        g.fillRect (getLocalBounds());
        const auto& t = w::table ((int) std::lround (page.value (w::tableBase + page.osc)));
        const bool on = page.value (page.oscParam (w::osc::on)) > 0.5;
        const auto pos = page.value (page.oscParam (w::osc::position));
        auto r = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
        constexpr int shown = 16;
        const int m = 8; // a smooth mip for drawing
        const auto len = w::mipLength (m);
        const auto stepY = r.getHeight() * 0.55f / shown, stepX = r.getWidth() * 0.12f / shown;
        const auto waveH = r.getHeight() * 0.45f, waveW = r.getWidth() * 0.88f;
        const auto drawFrame = [&] (double framePos, juce::Colour c, float thick) {
            const auto fpos = framePos * (w::frameCount - 1);
            const auto f0 = std::min ((int) fpos, w::frameCount - 2);
            const auto wgt = (float) (fpos - f0);
            const auto* a = t.frame (m, f0);
            const auto* b = t.frame (m, f0 + 1);
            const auto y0 = r.getBottom() - waveH * 0.5f - (float) framePos * stepY * shown;
            const auto x0 = r.getX() + (float) framePos * stepX * shown;
            juce::Path path;
            for (int i = 0; i <= 96; ++i)
            {
                const auto idx = std::min (len - 1, i * len / 96);
                const auto v = a[idx] + (b[idx] - a[idx]) * wgt;
                const auto px = x0 + waveW * (float) i / 96.0f, py = y0 - std::clamp (v, -1.2f, 1.2f) * waveH * 0.42f;
                i == 0 ? path.startNewSubPath (px, py) : path.lineTo (px, py);
            }
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (thick));
        };
        for (int k = shown - 1; k >= 0; --k)
            drawFrame ((double) k / (shown - 1), l.line.brighter (0.25f), 1.0f);
        drawFrame (pos, on ? l.accent : l.dim, 2.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override { startPos = page.value (page.oscParam (w::osc::position)), startY = e.y; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        page.set (page.oscParam (w::osc::position), std::clamp (startPos + (startY - e.y) / 150.0, 0.0, 1.0));
        repaint();
        if (page.refresh)
            page.refresh();
    }

private:
    Page& page;
    double startPos = 0.0;
    int startY = 0;
};

// --- the filters' response (both, as the routing combines them) ------------------------------------------------------------
class FilterDisplay final : public juce::Component
{
public:
    explicit FilterDisplay (Page& p) : page (p) {}
    double magnitude (int k, double hz) const
    {
        const auto b = w::filterBase (k);
        if (page.value (b + w::flt::on) < 0.5)
            return -1.0;
        const auto fc = page.value (b + w::flt::freq), res = std::clamp (page.value (b + w::flt::res), 0.0, 1.0);
        const auto q = 0.5 + 6.0 * res * res;
        const auto x = hz / fc;
        const auto den = std::sqrt ((1.0 - x * x) * (1.0 - x * x) + (x / q) * (x / q));
        const auto type = (int) std::lround (page.value (b + w::flt::type));
        double m = type == 0 ? 1.0 / den : type == 1 ? x * x / den : type == 2 ? (x / q) / den : type == 3 ? std::abs (1.0 - x * x) / den : 1.0 / den;
        if (page.value (b + w::flt::slope) > 0.5)
            m *= m;
        return m;
    }
    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.setColour (l.dark);
        g.fillRect (getLocalBounds());
        auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
        const auto route = (int) std::lround (page.value (w::routing));
        juce::Path path;
        bool any = false;
        for (int i = 0; i <= 120; ++i)
        {
            const auto hz = 20.0 * std::pow (1024.0, i / 120.0);
            const auto m1 = magnitude (0, hz), m2 = magnitude (1, hz);
            double m;
            if (m1 < 0.0 && m2 < 0.0)
                m = 1.0;
            else if (route == w::routeSerial)
                m = (m1 < 0.0 ? 1.0 : m1) * (m2 < 0.0 ? 1.0 : m2);
            else
                m = 0.5 * ((m1 < 0.0 ? 0.0 : m1) + (m2 < 0.0 ? 0.0 : m2)) * (m1 >= 0.0 && m2 >= 0.0 ? 1.0 : 2.0);
            any = any || m1 >= 0.0 || m2 >= 0.0;
            const auto db = std::clamp (20.0 * std::log10 (std::max (m, 1e-6)), -36.0, 18.0);
            const auto px = r.getX() + r.getWidth() * (float) i / 120.0f, py = r.getY() + r.getHeight() * (float) ((18.0 - db) / 54.0);
            i == 0 ? path.startNewSubPath (px, py) : path.lineTo (px, py);
        }
        if (! any)
            return;
        auto fill = path;
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        g.setColour (l.accent2.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (l.accent2);
        g.strokePath (path, juce::PathStrokeType (1.5f));
        // the cutoffs' markers (1 and 2)
        for (int k = 0; k < 2; ++k)
            if (page.value (w::filterBase (k) + w::flt::on) > 0.5)
            {
                const auto x = r.getX() + r.getWidth() * (float) w::filterNorm (page.value (w::filterBase (k) + w::flt::freq));
                const juce::Rectangle<float> dot (x - 6.0f, r.getBottom() - 14.0f, 12.0f, 12.0f);
                g.setColour (l.accent);
                g.fillEllipse (dot);
                g.setColour (l.dark);
                g.setFont (theme::font (9.0f, true));
                g.drawText (juce::String (k + 1), dot, juce::Justification::centred, false);
            }
    }

private:
    Page& page;
};

// --- an envelope's or an LFO's shape --------------------------------------------------------------------------------------
class SourceDisplay final : public juce::Component
{
public:
    explicit SourceDisplay (Page& p) : page (p) {}
    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.setColour (l.dark);
        g.fillRect (getLocalBounds());
        auto r = getLocalBounds().toFloat().reduced (10.0f, 10.0f);
        juce::Path path;
        if (page.source <= 2)
        {
            const auto b = w::envBase (page.source);
            const auto v = [&] (int e) { return page.value (b + e); };
            const auto a = v (w::env::attack), d = v (w::env::decay), rel = v (w::env::release);
            const auto hold = std::max (0.25, 0.3 * (a + d + rel));
            const auto total = std::max (0.01, a + d + hold + rel);
            const bool values = page.source > 0;
            const auto i0 = values ? v (w::env::initial) : 0.0, pk = values ? v (w::env::peak) : 1.0, sus = v (w::env::sustain), fin = values ? v (w::env::final) : 0.0;
            const auto x = [&] (double t) { return r.getX() + (float) (t / total) * r.getWidth(); };
            const auto y = [&] (double lv) { return r.getBottom() - (float) std::clamp (lv, 0.0, 1.0) * r.getHeight(); };
            std::vector<juce::Point<float>> points;
            const auto seg = [&] (double t0, double t1, double l0, double l1, double slope, bool first) {
                for (int i = first ? 0 : 1; i <= 24; ++i)
                {
                    const auto s = (double) w::segment ((float) i / 24.0f, (float) slope);
                    const auto px = x (t0 + (t1 - t0) * i / 24.0), py = y (l0 + (l1 - l0) * s);
                    path.isEmpty() ? path.startNewSubPath (px, py) : path.lineTo (px, py);
                }
                points.push_back ({ x (t1), y (l1) });
            };
            points.push_back ({ x (0.0), y (i0) });
            seg (0.0, a, i0, pk, v (w::env::attackSlope), true);
            seg (a, a + d, pk, sus, v (w::env::decaySlope), false);
            path.lineTo (x (a + d + hold), y (sus));
            points.push_back ({ x (a + d + hold), y (sus) });
            seg (a + d + hold, total, sus, fin, v (w::env::releaseSlope), false);
            g.setColour (l.accent2);
            g.strokePath (path, juce::PathStrokeType (1.5f));
            for (const auto& pnt : points)
            {
                g.setColour (l.dark);
                g.fillRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pnt));
                g.setColour (l.accent);
                g.drawRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pnt), 1.2f);
            }
            return;
        }
        const auto b = w::lfoBase (page.source - 3);
        const auto type = (int) std::lround (page.value (b + w::lfo::type));
        const auto amount = page.value (b + w::lfo::amount), shaping = page.value (b + w::lfo::shaping), phase = page.value (b + w::lfo::phase) / 360.0;
        if (drawn())
        {
            paintDrawn (g, l, r);
            return;
        }
        const auto cycles = 4.0;
        for (int i = 0; i <= 240; ++i)
        {
            auto x = std::fmod ((double) i / 240.0 * cycles + phase, 1.0);
            double v;
            switch (type)
            {
                case 0: v = std::sin (2.0 * juce::MathConstants<double>::pi * x); break;
                case 1: v = x < 0.25 ? 4.0 * x : x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0; break;
                case 2: v = 1.0 - 2.0 * std::fmod (x + 0.5, 1.0); break;
                case 3: v = x < 0.5 ? 1.0 : -1.0; break;
                default: v = std::sin (std::floor ((double) i / 240.0 * cycles + phase) * 12.9898) ; break;
            }
            if (shaping != 0.0)
                v = std::copysign (std::pow (std::abs (v), std::exp2 (-2.0 * shaping)), v);
            v *= amount;
            const auto px = r.getX() + r.getWidth() * (float) i / 240.0f, py = r.getCentreY() - (float) v * r.getHeight() * 0.48f;
            i == 0 ? path.startNewSubPath (px, py) : path.lineTo (px, py);
        }
        g.setColour (l.accent);
        g.strokePath (path, juce::PathStrokeType (1.8f));
    }

    // --- Helix+'s drawn LFO (round 190mi): one cycle, its points; LYNX's look edits it ---------------------------------
    int lfo() const { return std::max (0, page.source - 3); }
    bool drawn() const { return page.source >= 3 && page.plusLive() && page.xv (w::ext::lfoDraw + lfo()) > 0.5; }
    int pointsBase() const { return w::extBase + w::ext::points + lfo() * w::ext::lfoPointsSize; }
    std::vector<double> points() const
    {
        std::vector<double> p ((std::size_t) w::ext::lfoPointsSize);
        for (int i = 0; i < w::ext::lfoPointsSize; ++i)
            p[(std::size_t) i] = page.value (pointsBase() + i);
        return p;
    }
    int count (const std::vector<double>& p) const { return std::clamp ((int) std::lround (p[0]), 0, w::ext::maxPoints); }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10.0f, 10.0f); }
    juce::Point<float> toScreen (double x, double y) const
    {
        const auto r = area();
        return { r.getX() + (float) x * r.getWidth(), r.getCentreY() - (float) y * r.getHeight() * 0.48f };
    }
    void paintDrawn (juce::Graphics& g, const Look& l, juce::Rectangle<float> r)
    {
        const auto p = points();
        g.setColour (l.line);
        for (int q = 1; q < 8; ++q) // (a grid of eighths)
            g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) q / 8.0f), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        juce::Path path;
        for (int i = 0; i <= 240; ++i)
        {
            const auto x = (double) i / 240.0;
            const auto pt = toScreen (x, (double) w::drawnLfo (p.data(), std::min (x, 0.999999)));
            i == 0 ? path.startNewSubPath (pt) : path.lineTo (pt);
        }
        g.setColour (l.accent);
        g.strokePath (path, juce::PathStrokeType (1.8f));
        for (int i = 0; i < count (p); ++i)
        {
            const auto pt = toScreen (p[(std::size_t) (1 + i * w::ext::pointStride)], p[(std::size_t) (2 + i * w::ext::pointStride)]);
            g.setColour (i == dragging ? l.accent : l.dark);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt));
            g.setColour (l.accent);
            g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt), 1.3f);
        }
    }
    int pointAt (juce::Point<float> at) const
    {
        const auto p = points();
        for (int i = 0; i < count (p); ++i)
            if (toScreen (p[(std::size_t) (1 + i * w::ext::pointStride)], p[(std::size_t) (2 + i * w::ext::pointStride)]).getDistanceFrom (at) < 7.0f)
                return i;
        return -1;
    }
    void store (const std::vector<double>& p)
    {
        for (int i = 0; i < w::ext::lfoPointsSize; ++i)
            if (page.value (pointsBase() + i) != p[(std::size_t) i])
                page.set (pointsBase() + i, p[(std::size_t) i], false);
        repaint();
        if (page.refresh)
            page.refresh();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! drawn() || page.live)
            return;
        auto p = points();
        const auto n = count (p);
        const auto r = area();
        const auto x = std::clamp ((double) (e.position.x - r.getX()) / r.getWidth(), 0.0, 1.0);
        const auto y = std::clamp ((double) (r.getCentreY() - e.position.y) / (r.getHeight() * 0.48), -1.0, 1.0);
        dragging = pointAt (e.position);
        if (dragging >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1))
        {
            // delete (two points stay)
            if (n > 2)
            {
                for (int i = dragging; i < n - 1; ++i)
                    for (int j = 0; j < 3; ++j)
                        p[(std::size_t) (1 + i * 3 + j)] = p[(std::size_t) (1 + (i + 1) * 3 + j)];
                p[0] = n - 1;
                page.set (pointsBase(), p[0]); // (one undo step for the delete)
                store (p);
            }
            dragging = -1;
            return;
        }
        if (dragging >= 0 || e.mods.isAltDown())
            page.set (pointsBase(), p[0]); // (one undo step for the move / bend that follows)
        if (dragging < 0 && e.mods.isAltDown())
        {
            bendFrom = e.position.y, bendSegment = -1;
            for (int i = 0; i < n; ++i)
                if (p[(std::size_t) (1 + i * 3)] <= x)
                    bendSegment = i;
            bendSegment = bendSegment < 0 ? n - 1 : bendSegment;
            bendStart = p[(std::size_t) (3 + bendSegment * 3)];
            return;
        }
        if (dragging < 0 && n < w::ext::maxPoints && ! e.mods.isPopupMenu())
        {
            // add a point, the list kept in x order
            int at = 0;
            while (at < n && p[(std::size_t) (1 + at * 3)] <= x)
                ++at;
            for (int i = n; i > at; --i)
                for (int j = 0; j < 3; ++j)
                    p[(std::size_t) (1 + i * 3 + j)] = p[(std::size_t) (1 + (i - 1) * 3 + j)];
            p[(std::size_t) (1 + at * 3)] = x, p[(std::size_t) (2 + at * 3)] = y, p[(std::size_t) (3 + at * 3)] = 0.0;
            p[0] = n + 1;
            page.set (pointsBase(), p[0]); // (one undo step: the drag that follows is part of it)
            store (p);
            dragging = at;
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drawn() || page.live)
            return;
        auto p = points();
        const auto n = count (p);
        if (bendSegment >= 0 && bendSegment < n)
        {
            p[(std::size_t) (3 + bendSegment * 3)] = std::clamp (bendStart + (bendFrom - e.position.y) / 80.0, -1.0, 1.0);
            store (p);
            return;
        }
        if (dragging < 0 || dragging >= n)
            return;
        const auto r = area();
        auto x = std::clamp ((double) (e.position.x - r.getX()) / r.getWidth(), 0.0, 1.0);
        const auto lo = dragging > 0 ? p[(std::size_t) (1 + (dragging - 1) * 3)] : 0.0;
        const auto hi = dragging + 1 < n ? p[(std::size_t) (1 + (dragging + 1) * 3)] : 1.0;
        x = dragging == 0 && e.mods.isShiftDown() ? 0.0 : std::clamp (x, lo, hi);
        if (e.mods.isCommandDown()) // (snap to sixteenths)
            x = std::round (x * 16.0) / 16.0;
        p[(std::size_t) (1 + dragging * 3)] = x;
        p[(std::size_t) (2 + dragging * 3)] = std::clamp ((double) (r.getCentreY() - e.position.y) / (r.getHeight() * 0.48), -1.0, 1.0);
        store (p);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = -1, bendSegment = -1;
        repaint();
    }

private:
    Page& page;
    int dragging = -1, bendSegment = -1;
    float bendFrom = 0.0f;
    double bendStart = 0.0;
};

// --- the modulation matrix: the rows with any amount, a column per source -----------------------------------------------
class MatrixView final : public juce::Component
{
public:
    explicit MatrixView (Page& p) : page (p) {}
    std::vector<int> columns() const
    {
        switch (page.modTab)
        {
            case 1:
                if (! page.live) // (LYNX's look: Helix+'s macros too, round 190mi)
                    return { w::src::amp, w::src::env2, w::src::env3, w::src::lfo1, w::src::lfo2, w::src::macro1, w::src::macro1 + 1, w::src::macro1 + 2,
                             w::src::macro1 + 3 };
                return { w::src::amp, w::src::env2, w::src::env3, w::src::lfo1, w::src::lfo2 };
            case 2: return { w::src::velocity, w::src::note, w::src::pitchBend, w::src::pressure, w::src::modWheel, w::src::random };
            default: return { w::src::velocity, w::src::notePitchBend, w::src::slide, w::src::pressure };
        }
    }
    static juce::String columnName (int s)
    {
        switch (s)
        {
            case w::src::note: return tr ("Key");
            case w::src::pitchBend: return "PB";
            case w::src::pressure: return tr ("Press");
            case w::src::modWheel: return tr ("Mod");
            case w::src::random: return tr ("Rand");
            case w::src::velocity: return tr ("Vel");
            default:
                if (s >= w::src::macro1)
                    return "M" + juce::String (s - w::src::macro1 + 1);
                return tr (u8 (w::sourceName (s)));
        }
    }
    std::vector<int> rows() const
    {
        std::vector<int> r;
        for (int t = 0; t < rowCount(); ++t)
        {
            bool any = std::find (extra.begin(), extra.end(), t) != extra.end();
            for (int s = 0; s < (page.live ? w::sourceCount : w::allSourceCount) && ! any; ++s)
                any = page.value (w::cellIndex (t, s)) != 0.0;
            if (any)
                r.push_back (t);
        }
        return r;
    }
    int rowCount() const { return page.live ? w::targetCount : w::allTargetCount; } // (Helix+'s rows in LYNX's look)
    juce::String rowName (int t) const
    {
        return tr (u8 (w::targetName (t, (int) std::lround (page.value (w::oscBase (0) + w::osc::effectMode)),
                                      (int) std::lround (page.value (w::oscBase (1) + w::osc::effectMode)))));
    }
    // a cell's text: Pitch rows in semitones (Live: Pitch ← PB 1/24 = "2"), the rest in percent
    static juce::String cellText (int t, double a)
    {
        if (a == 0.0)
            return {};
        if (t == w::tgt::pitch)
            return juce::String (a * 48.0, std::abs (a * 48.0) < 10.0 ? 1 : 0).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return juce::String (juce::roundToInt (a * 100.0));
    }
    juce::Rectangle<int> cell (int row, int col) const
    {
        const auto cols = (int) columns().size();
        const auto cw = (getWidth() - nameWidth) / std::max (1, cols);
        return { nameWidth + col * cw, header + row * rowHeight - scroll, cw, rowHeight };
    }
    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.setColour (l.dark);
        g.fillRect (getLocalBounds());
        const auto cols = columns();
        g.setFont (theme::font (10.5f));
        g.setColour (l.text);
        g.drawText (tr ("Target"), juce::Rectangle<int> (4, 0, nameWidth - 4, header), juce::Justification::centredLeft, false);
        for (int c = 0; c < (int) cols.size(); ++c)
        {
            const auto r = cell (0, c).withY (0).withHeight (header);
            g.setColour (l.accent2);
            g.drawFittedText (columnName (cols[(std::size_t) c]), r, juce::Justification::centred, 1, 0.7f);
            g.fillRect (r.getX() + 2, header - 2, r.getWidth() - 4, 1);
        }
        const auto rs = rows();
        g.reduceClipRegion (0, header, getWidth(), getHeight() - header);
        for (int i = 0; i < (int) rs.size(); ++i)
        {
            const auto t = rs[(std::size_t) i];
            const auto y = header + i * rowHeight - scroll;
            g.setColour (l.bg);
            g.fillRect (2, y + 1, nameWidth - 4, rowHeight - 2);
            g.setColour (l.text);
            g.drawFittedText (rowName (t), juce::Rectangle<int> (6, y, nameWidth - 8, rowHeight), juce::Justification::centredLeft, 1, 0.7f);
            for (int c = 0; c < (int) cols.size(); ++c)
            {
                const auto r = cell (i, c).reduced (1);
                const auto a = page.value (w::cellIndex (t, cols[(std::size_t) c]));
                g.setColour (l.bg.darker (0.2f));
                g.fillRect (r);
                if (a != 0.0)
                {
                    g.setColour (l.accent);
                    g.drawFittedText (cellText (t, a), r.withTrimmedBottom (3), juce::Justification::centred, 1, 0.7f);
                    const auto bw = (int) std::lround (std::min (1.0, std::abs (a)) * (r.getWidth() / 2 - 2));
                    g.fillRect (a > 0 ? r.getCentreX() : r.getCentreX() - bw, r.getBottom() - 3, bw, 2);
                }
            }
        }
    }
    std::pair<int, int> hit (juce::Point<int> p) const
    {
        const auto rs = rows();
        const auto row = (p.y - header + scroll) / rowHeight;
        if (p.y < header || row < 0 || row >= (int) rs.size() || p.x < nameWidth)
            return { -1, -1 };
        const auto cols = columns();
        const auto col = (p.x - nameWidth) / std::max (1, (getWidth() - nameWidth) / std::max (1, (int) cols.size()));
        if (col < 0 || col >= (int) cols.size())
            return { -1, -1 };
        return { rs[(std::size_t) row], cols[(std::size_t) col] };
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto [t, s] = hit (e.getPosition());
        if (t < 0)
        {
            if (e.mods.isPopupMenu() || e.y >= header)
                showAddMenu();
            return;
        }
        dragTarget = t, dragSource = s, startValue = page.value (w::cellIndex (t, s)), startY = e.y;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (tr ("Reset"), [this, t = t, s = s] { setCell (t, s, 0.0); });
            m.addItem (tr ("Add a modulation target"), [this] { showAddMenu(); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
            dragTarget = -1;
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragTarget >= 0)
            setCell (dragTarget, dragSource, std::clamp (startValue + (startY - e.y) / 150.0, -1.0, 1.0));
    }
    void mouseUp (const juce::MouseEvent&) override { dragTarget = -1; }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const auto [t, s] = hit (e.getPosition()); t >= 0)
            setCell (t, s, 0.0);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wh) override
    {
        const auto maxScroll = std::max (0, (int) rows().size() * rowHeight - (getHeight() - header));
        scroll = std::clamp (scroll - (int) std::lround (wh.deltaY * 60.0f), 0, maxScroll);
        repaint();
    }
    void setCell (int t, int s, double v)
    {
        page.set (w::cellIndex (t, s), v);
        repaint();
        if (page.refresh)
            page.refresh();
    }
    void showAddMenu()
    {
        juce::PopupMenu m;
        const auto rs = rows();
        for (int t = 0; t < rowCount(); ++t)
            if (std::find (rs.begin(), rs.end(), t) == rs.end())
                m.addItem (rowName (t), [this, t] {
                    extra.push_back (t);
                    repaint();
                });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

private:
    static constexpr int nameWidth = 78, header = 16, rowHeight = 19;
    Page& page;
    std::vector<int> extra; // rows added with no amount yet (this view only)
    int scroll = 0, dragTarget = -1, dragSource = 0, startY = 0;
    double startValue = 0.0;
};

// --- the device ----------------------------------------------------------------------------------------------------------
class HelixPanel final : public juce::Component, private juce::ChangeListener
{
public:
    HelixPanel (AudioEngine& e, std::function<ChannelDef* (Song&)> locate, int channel, bool live)
        : page { e, std::move (locate), channel, live }, wave (page), filters (page), sourceView (page), matrix (page)
    {
        page.refresh = [this] { refreshAll(); };
        using K = Ctl::Kind;
        auto ctl = [this] (int i, K k, juce::String cap, Curve c = Curve::linear) {
            ctls.push_back (std::make_unique<Ctl> (page, i, k, std::move (cap), c));
            addAndMakeVisible (*ctls.back());
            return ctls.back().get();
        };
        auto tab = [this] (juce::String t, std::function<bool()> on, std::function<void()> click) {
            tabs.push_back (std::make_unique<Tab> (page, std::move (t), std::move (on), std::move (click)));
            addAndMakeVisible (*tabs.back());
            return tabs.back().get();
        };
        auto menu = [this] (std::unique_ptr<Menu> m) {
            menus.push_back (std::move (m));
            addAndMakeVisible (*menus.back());
            return menus.back().get();
        };
        // sub column
        subOn = tab (tr ("Sub"), [this] { return page.value (w::subOn) > 0.5; }, [this] { page.set (w::subOn, page.value (w::subOn) > 0.5 ? 0.0 : 1.0); });
        subGain = ctl (w::subGain, K::knob, tr ("Gain"), Curve::gain);
        subTone = ctl (w::subTone, K::knob, tr ("Tone"));
        for (int o = 0; o < 3; ++o)
            octave[(std::size_t) o] = tab (o == 0 ? juce::String ("0") : "-" + juce::String (o), [this, o] { return (int) std::lround (page.value (w::subTranspose)) == o; },
                                           [this, o] { page.set (w::subTranspose, (double) o); });
        transpose = ctl (w::transpose, K::box, tr ("Transpose"));
        subOn->switchStyle = true;
        for (auto* o : octave)
            o->switchStyle = true;
        // oscillator
        for (int k = 0; k < 2; ++k)
            oscTabs[(std::size_t) k] = tab ("Osc " + juce::String (k + 1), [this, k] { return page.osc == k; }, [this, k] { selectOsc (k); });
        oscOn = tab (juce::String(), [this] { return page.value (page.oscParam (w::osc::on)) > 0.5; },
                     [this] { page.set (page.oscParam (w::osc::on), page.value (page.oscParam (w::osc::on)) > 0.5 ? 0.0 : 1.0); });
        oscOn->setTooltip (tr ("Oscillator on / off"));
        category = menu (std::make_unique<Menu> (page, [this] { return tr (w::tableCategory (currentTable())); }, [this] (juce::PopupMenu& m) { tableMenu (m, true); }));
        tableName = menu (std::make_unique<Menu> (page, [this] { return tr (w::tableName (currentTable())); }, [this] (juce::PopupMenu& m) { tableMenu (m, false); }));
        prevTable = tab (juce::String::fromUTF8 ("\xe2\x97\x82"), [] { return false; }, [this] { stepTable (-1); });
        nextTable = tab (juce::String::fromUTF8 ("\xe2\x96\xb8"), [] { return false; }, [this] { stepTable (1); });
        addAndMakeVisible (wave);
        oscGain = ctl (w::oscBase (0) + w::osc::gain, K::vslider, {}, Curve::gain);
        gainText = ctl (w::oscBase (0) + w::osc::gain, K::box, {}, Curve::gain);
        oscPan = ctl (w::oscBase (0) + w::osc::pan, K::box, {});
        oscPos = ctl (w::oscBase (0) + w::osc::position, K::vslider, {});
        posText = ctl (w::oscBase (0) + w::osc::position, K::box, {});
        effectMode = menu (Menu::of (page, [this] { return page.oscParam (w::osc::effectMode); }));
        fx1 = ctl (w::oscBase (0) + w::osc::effect1, K::box, {});
        fx2 = ctl (w::oscBase (0) + w::osc::effect2, K::box, {});
        semi = ctl (w::oscBase (0) + w::osc::transpose, K::box, {});
        det = ctl (w::oscBase (0) + w::osc::detune, K::box, {});
        // filters
        for (int k = 0; k < 2; ++k)
        {
            const auto b = w::filterBase (k);
            fOn[(std::size_t) k] = tab (juce::String(), [this, b] { return page.value (b + w::flt::on) > 0.5; },
                                        [this, b] { page.set (b + w::flt::on, page.value (b + w::flt::on) > 0.5 ? 0.0 : 1.0); });
            fOn[(std::size_t) k]->setTooltip (tr ("Filter on / off"));
            fType[(std::size_t) k] = menu (Menu::of (page, [b] { return b + w::flt::type; }));
            fSlope[(std::size_t) k] = tab ("12", [this, b] { return page.value (b + w::flt::slope) > 0.5; },
                                           [this, b] { page.set (b + w::flt::slope, page.value (b + w::flt::slope) > 0.5 ? 0.0 : 1.0); });
            fSlope[(std::size_t) k]->setTooltip (tr ("Slope: 12 dB / 24 dB per octave (lit: 24)"));
            fCircuit[(std::size_t) k] = menu (Menu::of (page, [this, b] {
                return (int) std::lround (page.value (b + w::flt::type)) <= 1 ? b + w::flt::circuitLpHp : b + w::flt::circuitBpNoMo;
            }));
            fRes[(std::size_t) k] = ctl (b + w::flt::res, K::knob, tr ("Res"));
            fFreq[(std::size_t) k] = ctl (b + w::flt::freq, K::knob, tr ("Frequency"), Curve::freq);
            fDrive[(std::size_t) k] = ctl (b + w::flt::drive, K::box, tr ("Drive"));
            fMorph[(std::size_t) k] = ctl (b + w::flt::morph, K::box, tr ("Morph"));
        }
        addAndMakeVisible (filters);
        routing = menu (Menu::of (page, [] { return w::routing; }));
        // modulation
        static const char* modTabs[4] { "Mod Sources", "Matrix", "MIDI", "MPE" };
        for (int t = 0; t < 4; ++t)
            modTab[(std::size_t) t] = tab (tr (modTabs[t]), [this, t] { return page.modTab == t; }, [this, t] { page.modTab = t; layoutMod(); });
        static const char* sources[5] { "Amp", "Env 2", "Env 3", "LFO 1", "LFO 2" };
        for (int s = 0; s < 5; ++s)
        {
            sourceTab[(std::size_t) s] = tab (tr (sources[s]), [this, s] { return page.source == s; }, [this, s] { page.source = s; layoutMod(); });
            if (! page.live) // (Helix+: drag a source onto a knob to modulate it)
            {
                static constexpr int ids[5] { w::src::amp, w::src::env2, w::src::env3, w::src::lfo1, w::src::lfo2 };
                sourceTab[(std::size_t) s]->dragSource = ids[s];
                sourceTab[(std::size_t) s]->setTooltip (tr ("Drag onto a knob to modulate it"));
            }
        }
        addAndMakeVisible (sourceView);
        addAndMakeVisible (matrix);
        static const char* envViews[3] { "Time", "Slope", "Value" };
        for (int v = 0; v < 3; ++v)
            envView[(std::size_t) v] = tab (tr (envViews[v]), [this, v] { return page.envView == v; }, [this, v] { page.envView = v; layoutMod(); });
        for (int i = 0; i < 4; ++i)
            envBoxes[(std::size_t) i] = ctl (w::envBase (0), K::box, {});
        lfoShape = menu (Menu::of (page, [this] { return w::lfoBase (std::max (0, page.source - 3)) + w::lfo::type; }));
        for (int i = 0; i < 4; ++i)
            lfoBoxes[(std::size_t) i] = ctl (w::lfoBase (0), K::box, {});
        lfoSync = tab (tr ("Sync"), [this] { return page.value (w::lfoBase (std::max (0, page.source - 3)) + w::lfo::sync) > 0.5; }, [this] {
            const auto i = w::lfoBase (std::max (0, page.source - 3)) + w::lfo::sync;
            page.set (i, page.value (i) > 0.5 ? 0.0 : 1.0);
            layoutMod();
        });
        lfoRetrig = tab ("R", [this] { return page.value (w::lfoBase (std::max (0, page.source - 3)) + w::lfo::retrigger) > 0.5; }, [this] {
            const auto i = w::lfoBase (std::max (0, page.source - 3)) + w::lfo::retrigger;
            page.set (i, page.value (i) > 0.5 ? 0.0 : 1.0);
        });
        lfoRetrig->setTooltip (tr ("Retrigger: the LFO restarts with every note"));
        lfoAttack = ctl (w::lfoBase (0) + w::lfo::attack, K::box, {}, Curve::time);
        envLoop = menu (Menu::of (page, [this] { return w::envBase (std::min (page.source, 2)) + w::env::loopMode; }));
        globalTime = ctl (w::timeScale, K::box, tr ("Time"));
        globalAmount = ctl (w::modAmount, K::box, tr ("Amount"));
        // global column
        volume = ctl (w::volume, K::box, tr ("Volume"), Curve::gain);
        poly = tab (tr ("Poly"), [this] { return page.value (w::monoPoly) > 0.5; }, [this] { page.set (w::monoPoly, page.value (w::monoPoly) > 0.5 ? 0.0 : 1.0); });
        polyVoices = menu (Menu::of (page, [] { return w::polyVoices; }));
        poly->switchStyle = true;
        glide = ctl (w::glide, K::box, tr ("Glide"), Curve::time);
        unison = menu (Menu::of (page, [] { return w::unisonMode; }));
        unisonVoices = ctl (w::unisonVoices, K::box, tr ("Voices"));
        unisonAmount = ctl (w::unisonAmount, K::box, tr ("Amount"));

        page.engine.addChangeListener (this);
        selectOsc (0);
        layoutMod();
        setSize (helixDeviceWidth, helixDeviceHeight);
    }
    ~HelixPanel() override { page.engine.removeChangeListener (this); }

    void showView (int modTab, int source, int osc)
    {
        page.modTab = std::clamp (modTab, 0, 3);
        page.source = std::clamp (source, 0, 4);
        selectOsc (std::clamp (osc, 0, 1));
        layoutMod();
    }

    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.fillAll (l.outer);
        g.setColour (l.bg);
        g.fillRect (subArea);
        g.fillRect (globalArea);
        g.setColour (l.dark);
        g.fillRect (oscArea);
        g.fillRect (modArea);
        g.setColour (l.bg);
        g.fillRect (filterArea);
        // captions drawn by the panel (Live: plain labels next to the boxes)
        caption (g, l, { subArea.getX() + 4, 104, 60, 12 }, tr ("Octave"));
        caption (g, l, { globalArea.getX() + 2, 84, 70, 12 }, tr ("Unison"));
        const auto mode = (int) std::lround (page.value (page.oscParam (w::osc::effectMode)));
        static const char* fxNames[4][2] { { "FX 1", "FX 2" }, { "Tune", "Amt" }, { "PW", "Sync" }, { "Warp", "Fold" } };
        g.setFont (theme::font (10.0f));
        g.setColour (l.text);
        g.drawText (tr (fxNames[std::clamp (mode, 0, 3)][0]), fx1->getBounds().withX (fx1->getX() - 30).withWidth (28), juce::Justification::centredRight, false);
        g.drawText (tr (fxNames[std::clamp (mode, 0, 3)][1]), fx2->getBounds().withX (fx2->getX() - 26).withWidth (24), juce::Justification::centredRight, false);
        g.drawText (tr ("Semi"), semi->getBounds().withX (semi->getX() - 26).withWidth (24), juce::Justification::centredRight, false);
        g.drawText (tr ("Det"), det->getBounds().withX (det->getX() - 18).withWidth (17), juce::Justification::centredRight, false);
        if (page.modTab == 1)
        {
            caption (g, l, { modArea.getX() + 6, modArea.getBottom() - 16, 60, 14 }, tr ("Global"));
        }
        g.setColour (l.line);
        g.drawVerticalLine (oscArea.getRight(), (float) oscArea.getY(), (float) oscArea.getBottom());
        g.drawVerticalLine (filterArea.getRight(), (float) filterArea.getY(), (float) filterArea.getBottom());
    }

    void resized() override
    {
        // the regions of Live's device, left to right [SHOT 190ja live-wt-preset-descending-dreams.png, 1000 px wide]
        const auto h = getHeight();
        subArea = { 4, 2, 64, h - 4 };
        oscArea = { 70, 2, 300, h - 4 };
        filterArea = { 372, 2, 234, h - 4 };
        modArea = { 608, 2, 316, h - 4 };
        globalArea = { 926, 2, 70, h - 4 };
        // sub
        subOn->setBounds (subArea.getX() + 4, 4, 56, 16);
        subGain->setBounds (subArea.getX() + 2, 22, 60, 40);
        subTone->setBounds (subArea.getX() + 2, 62, 60, 40);
        for (int o = 0; o < 3; ++o)
            octave[(std::size_t) o]->setBounds (subArea.getX() + 4 + o * 19, 117, 18, 15);
        transpose->setBounds (subArea.getX() + 4, 136, 56, 30);
        // oscillator
        oscTabs[0]->setBounds (oscArea.getX() + 2, 3, 40, 16);
        oscTabs[1]->setBounds (oscArea.getX() + 44, 3, 40, 16);
        oscOn->setBounds (oscArea.getX() + 86, 5, 12, 12);
        category->setBounds (oscArea.getX() + 102, 3, 76, 16);
        tableName->setBounds (oscArea.getX() + 180, 3, 86, 16);
        prevTable->setBounds (oscArea.getX() + 268, 3, 14, 16);
        nextTable->setBounds (oscArea.getX() + 283, 3, 14, 16);
        oscPan->setBounds (oscArea.getX() + 2, 22, 30, 14);
        oscGain->setBounds (oscArea.getX() + 8, 38, 18, 92);
        gainText->setBounds (oscArea.getX() + 2, 132, 32, 14);
        wave.setBounds (oscArea.getX() + 34, 22, 236, 124);
        posText->setBounds (oscArea.getX() + 266, 130, 34, 14);
        oscPos->setBounds (oscArea.getX() + 274, 24, 18, 104);
        effectMode->setBounds (oscArea.getX() + 2, h - 22, 62, 16);
        fx1->setBounds (oscArea.getX() + 96, h - 22, 34, 16);
        fx2->setBounds (oscArea.getX() + 158, h - 22, 34, 16);
        semi->setBounds (oscArea.getX() + 220, h - 22, 26, 16);
        det->setBounds (oscArea.getX() + 266, h - 22, 32, 16);
        // filters
        for (int k = 0; k < 2; ++k)
        {
            const auto x = filterArea.getX() + 4 + k * 116;
            fOn[(std::size_t) k]->setBounds (x, 5, 12, 12);
            fType[(std::size_t) k]->setBounds (x + 14, 3, 40, 16);
            fSlope[(std::size_t) k]->setBounds (x + 55, 3, 18, 16);
            fCircuit[(std::size_t) k]->setBounds (x + 74, 3, 40, 16);
        }
        filters.setBounds (filterArea.getX() + 2, 22, filterArea.getWidth() - 4, 70);
        routing->setBounds (filterArea.getX() + 6, 94, 70, 15);
        fRes[0]->setBounds (filterArea.getX() + 2, 112, 44, h - 114);
        fFreq[0]->setBounds (filterArea.getX() + 46, 112, 66, h - 114);
        fFreq[1]->setBounds (filterArea.getX() + 120, 112, 66, h - 114);
        fRes[1]->setBounds (filterArea.getX() + 188, 112, 44, h - 114);
        for (int k = 0; k < 2; ++k)
        {
            fDrive[(std::size_t) k]->setBounds (filterArea.getX() + 80 + k * 76, 92, 36, 0); // (shown in the value menus / automation; folded here)
            fMorph[(std::size_t) k]->setBounds (filterArea.getX() + 120 + k * 60, 92, 36, 0);
        }
        // global column
        volume->setBounds (globalArea.getX() + 2, 2, 66, 28);
        poly->setBounds (globalArea.getX() + 2, 34, 30, 15);
        polyVoices->setBounds (globalArea.getX() + 34, 34, 34, 15);
        glide->setBounds (globalArea.getX() + 2, 52, 66, 28);
        unison->setBounds (globalArea.getX() + 2, 97, 66, 15);
        unisonVoices->setBounds (globalArea.getX() + 2, 114, 66, 28);
        unisonAmount->setBounds (globalArea.getX() + 2, 144, 66, 28);
        layoutMod();
    }

private:
    int currentTable() const { return std::clamp ((int) std::lround (page.value (w::tableBase + page.osc)), 0, w::tableCount() - 1); }

    void tableMenu (juce::PopupMenu& m, bool categoriesOnly)
    {
        const auto current = currentTable();
        for (const auto& cat : w::tableCategories())
        {
            juce::PopupMenu sub;
            int first = -1;
            for (int t = 0; t < w::tableCount(); ++t)
                if (cat == w::tableCategory (t))
                {
                    first = first < 0 ? t : first;
                    sub.addItem (tr (w::tableName (t)), true, t == current, [this, t] { setTable (t); });
                }
            if (categoriesOnly)
                m.addItem (tr (u8 (cat)), true, cat == w::tableCategory (current), [this, first] { setTable (first); });
            else
                m.addSubMenu (tr (u8 (cat)), sub, true, nullptr, cat == w::tableCategory (current));
        }
    }
    void setTable (int t)
    {
        (void) w::table (t); // (built here, off the audio thread)
        page.set (w::tableBase + page.osc, (double) t);
        refreshAll();
    }
    void stepTable (int d) { setTable ((currentTable() + d + w::tableCount()) % w::tableCount()); }

    void selectOsc (int k)
    {
        page.osc = k;
        oscGain->setIndex (page.oscParam (w::osc::gain));
        gainText->setIndex (page.oscParam (w::osc::gain));
        oscPan->setIndex (page.oscParam (w::osc::pan));
        oscPos->setIndex (page.oscParam (w::osc::position));
        posText->setIndex (page.oscParam (w::osc::position));
        fx1->setIndex (page.oscParam (w::osc::effect1));
        fx2->setIndex (page.oscParam (w::osc::effect2));
        semi->setIndex (page.oscParam (w::osc::transpose));
        det->setIndex (page.oscParam (w::osc::detune));
        refreshAll();
    }

    void layoutMod()
    {
        const auto x = modArea.getX(), y = modArea.getY(), wdt = modArea.getWidth(), h = getHeight();
        for (int t = 0; t < 4; ++t)
            modTab[(std::size_t) t]->setBounds (x + wdt - 4 - (4 - t) * 62, y + 1, t == 0 ? 80 : 44, 15);
        modTab[0]->setBounds (x + wdt - 4 - 62 * 3 - 82, y + 1, 80, 15);
        const bool sourcesPage = page.modTab == 0;
        for (int s = 0; s < 5; ++s)
        {
            sourceTab[(std::size_t) s]->setVisible (sourcesPage);
            sourceTab[(std::size_t) s]->setBounds (x + 2 + s * (wdt - 4) / 5, y + 18, (wdt - 4) / 5, 15);
        }
        sourceView.setVisible (sourcesPage);
        matrix.setVisible (! sourcesPage);
        matrix.setBounds (x + 2, y + 18, wdt - 4, h - 42);
        const bool env = sourcesPage && page.source <= 2, lfo = sourcesPage && page.source >= 3;
        sourceView.setBounds (x + 2, y + 34, wdt - 4, env ? h - 80 : h - 80);
        for (int v = 0; v < 3; ++v)
        {
            envView[(std::size_t) v]->setVisible (env && (v < 2 || page.source > 0));
            envView[(std::size_t) v]->setBounds (x + wdt / 2 - 66 + v * 44, h - 44, 42, 13);
        }
        envLoop->setVisible (env);
        envLoop->setBounds (x + wdt - 74, y + 36, 70, 14);
        // the four boxes: A D S R as times / slopes / values (Live's Time / Slope / Value views)
        const auto b = w::envBase (std::min (page.source, 2));
        const int times[4] { w::env::attack, w::env::decay, w::env::sustain, w::env::release };
        const int slopes[4] { w::env::attackSlope, w::env::decaySlope, w::env::sustain, w::env::releaseSlope };
        const int values[4] { w::env::initial, w::env::peak, w::env::sustain, w::env::final };
        static const char* timeCaps[4] { "A", "D", "S", "R" };
        static const char* valueCaps[4] { "Initial", "Peak", "S", "Final" };
        for (int i = 0; i < 4; ++i)
        {
            auto* c = envBoxes[(std::size_t) i];
            c->setVisible (env);
            const auto e = page.envView == 0 ? times[i] : page.envView == 1 ? slopes[i] : values[i];
            c->setIndex (b + e);
            c->setLabel (tr (page.envView == 2 ? valueCaps[i] : timeCaps[i]));
            c->setBounds (x + 30 + i * (wdt - 60) / 4, h - 30, (wdt - 60) / 4 - 6, 28);
        }
        // LFO: shape, rate (or sync rate), amount, shaping ("Morph"), phase offset; attack, sync, retrigger
        const auto lb = w::lfoBase (std::max (0, page.source - 3));
        lfoShape->setVisible (lfo);
        lfoShape->setBounds (x + 6, h - 30, 66, 14);
        const bool synced = page.value (lb + w::lfo::sync) > 0.5;
        const int lfoParams[4] { synced ? w::lfo::syncedRate : w::lfo::rate, w::lfo::amount, w::lfo::shaping, w::lfo::phase };
        static const char* lfoCaps[4] { "Rate", "Amount", "Morph", "Offset" };
        for (int i = 0; i < 4; ++i)
        {
            auto* c = lfoBoxes[(std::size_t) i];
            c->setVisible (lfo);
            c->setIndex (lb + lfoParams[i]);
            c->setLabel (tr (lfoCaps[i]));
            c->setBounds (x + 78 + i * 58, h - 30, 54, 28);
        }
        lfoAttack->setVisible (lfo);
        lfoAttack->setIndex (lb + w::lfo::attack);
        lfoAttack->setBounds (x + wdt - 150, y + 36, 58, 14);
        lfoSync->setVisible (lfo);
        lfoSync->setBounds (x + wdt - 88, y + 36, 40, 14);
        lfoRetrig->setVisible (lfo);
        lfoRetrig->setBounds (x + wdt - 44, y + 36, 16, 14);
        globalTime->setVisible (page.modTab == 1);
        globalAmount->setVisible (page.modTab == 1);
        globalTime->setBounds (x + wdt - 150, h - 30, 66, 28);
        globalAmount->setBounds (x + wdt - 80, h - 30, 66, 28);
        if (page.modTab == 1)
            matrix.setBounds (x + 2, y + 18, wdt - 4, h - 52);
        refreshAll();
    }

    void refreshAll()
    {
        repaint();
        for (auto* c : getChildren())
            c->repaint();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (page.def() != nullptr)
            refreshAll();
    }

    Page page;
    WaveDisplay wave;
    FilterDisplay filters;
    SourceDisplay sourceView;
    MatrixView matrix;
    std::vector<std::unique_ptr<Ctl>> ctls;
    std::vector<std::unique_ptr<Tab>> tabs;
    std::vector<std::unique_ptr<Menu>> menus;
    juce::Rectangle<int> subArea, oscArea, filterArea, modArea, globalArea;
    Tab *subOn = nullptr, *oscOn = nullptr, *prevTable = nullptr, *nextTable = nullptr, *poly = nullptr, *lfoSync = nullptr, *lfoRetrig = nullptr;
    std::array<Tab*, 3> octave {};
    std::array<Tab*, 2> oscTabs {}, fOn {}, fSlope {};
    std::array<Tab*, 4> modTab {};
    std::array<Tab*, 5> sourceTab {};
    std::array<Tab*, 3> envView {};
    Ctl *subGain = nullptr, *subTone = nullptr, *transpose = nullptr, *oscGain = nullptr, *gainText = nullptr, *oscPan = nullptr, *oscPos = nullptr, *posText = nullptr,
        *fx1 = nullptr, *fx2 = nullptr, *semi = nullptr, *det = nullptr, *lfoAttack = nullptr, *globalTime = nullptr, *globalAmount = nullptr,
        *volume = nullptr, *glide = nullptr, *unisonVoices = nullptr, *unisonAmount = nullptr;
    std::array<Ctl*, 2> fRes {}, fFreq {}, fDrive {}, fMorph {};
    std::array<Ctl*, 4> envBoxes {}, lfoBoxes {};
    Menu *category = nullptr, *tableName = nullptr, *effectMode = nullptr, *routing = nullptr, *lfoShape = nullptr, *envLoop = nullptr,
         *polyVoices = nullptr, *unison = nullptr;
    std::array<Menu*, 2> fType {}, fCircuit {};
};

// --- Helix+ (round 190mi): LYNX's own strip under the device in the channel window --------------------------------------
// The second warps, the noise oscillator, unison stacking, the extra filter types, the macros (drag their handle onto a
// knob), the drawn LFOs' switches and the effects rack, in LYNX's look. The Live-look device above stays as it is.
class HelixPlusStrip final : public juce::Component, private juce::ChangeListener
{
public:
    static constexpr int stripHeight = 150;
    HelixPlusStrip (AudioEngine& e, std::function<ChannelDef* (Song&)> locate, int channel) : page { e, std::move (locate), channel, false }
    {
        page.refresh = [this] { refreshAll(); };
        const auto X = [] (int i) { return w::extBase + i; };
        for (int k = 0; k < 2; ++k)
        {
            warpMenu[(std::size_t) k] = menu (Menu::of (page, [X, k] { return X (w::ext::warpMode + k * w::ext::warpStride); }));
            warpMenu[(std::size_t) k]->setTooltip (tr ("Second warp: bends, syncs or cross-modulates the oscillator's read of its table"));
            warpAmt[(std::size_t) k] = knob (X (w::ext::warpAmount + k * w::ext::warpStride), tr ("Amount"));
        }
        noiseOn = toggle (X (w::ext::noiseOn), tr ("On"));
        noiseLevel = knob (X (w::ext::noiseLevel), tr ("Level"));
        noiseColour = knob (X (w::ext::noiseColour), tr ("Color"));
        unisonCount = menu (std::make_unique<Menu> (
            page, [this] { return tr (u8 (w::paramText (w::extBase + w::ext::unisonCount, page.xv (w::ext::unisonCount)))); },
            [this] (juce::PopupMenu& m) {
                const auto now = (int) std::lround (page.xv (w::ext::unisonCount));
                m.addItem (tr ("As device"), true, now < 2, [this] { setCount (0); });
                for (int c = 2; c <= w::maxUnison; ++c)
                    m.addItem (juce::String (c), true, now == c, [this, c] { setCount (c); });
            }));
        unisonCount->setTooltip (tr ("Unison voices (Live: the device's Voices; up to 16)"));
        unisonBlend = knob (X (w::ext::unisonBlend), tr ("Blend"));
        unisonWidth = knob (X (w::ext::unisonWidth), tr ("Width"));
        unisonStack = menu (Menu::of (page, [X] { return X (w::ext::unisonStack); }));
        unisonStack->setTooltip (tr ("Stack: unison voices an octave or a fifth apart"));
        for (int k = 0; k < 2; ++k)
            filterType[(std::size_t) k] = menu (Menu::of (page, [X, k] { return X (w::ext::filterType + k); }));
        for (int j = 0; j < w::macroCount; ++j)
        {
            macros[(std::size_t) j] = knob (X (w::ext::macro + j), juce::String (j + 1));
            macroHandle[(std::size_t) j] = tab ("M" + juce::String (j + 1), [] { return false; }, [] {});
            macroHandle[(std::size_t) j]->dragSource = w::src::macro1 + j;
            macroHandle[(std::size_t) j]->setTooltip (tr ("Drag onto a knob to modulate it"));
        }
        for (int k = 0; k < 2; ++k)
        {
            lfoDraw[(std::size_t) k] = toggle (X (w::ext::lfoDraw + k), "LFO " + juce::String (k + 1));
            lfoDraw[(std::size_t) k]->setTooltip (tr ("Drawn LFO: click adds a point, drag moves it, double-click deletes it, Alt-drag bends a segment"));
        }
        fxOn[0] = toggle (X (w::ext::distOn), tr ("On"));
        distMode = menu (Menu::of (page, [X] { return X (w::ext::distMode); }));
        fxKnobs.push_back (knob (X (w::ext::distDrive), tr ("Drive")));
        fxKnobs.push_back (knob (X (w::ext::distMix), tr ("Mix")));
        fxOn[1] = toggle (X (w::ext::chorusOn), tr ("On"));
        fxKnobs.push_back (knob (X (w::ext::chorusRate), tr ("Rate")));
        fxKnobs.push_back (knob (X (w::ext::chorusDepth), tr ("Depth")));
        fxKnobs.push_back (knob (X (w::ext::chorusMix), tr ("Mix")));
        fxOn[2] = toggle (X (w::ext::delayOn), tr ("On"));
        delayTime = menu (Menu::of (page, [X] { return X (w::ext::delayTime); }));
        delayPing = toggle (X (w::ext::delayPingPong), tr ("Ping-Pong"));
        fxKnobs.push_back (knob (X (w::ext::delayFeedback), tr ("Feedback")));
        fxKnobs.push_back (knob (X (w::ext::delayMix), tr ("Mix")));
        fxOn[3] = toggle (X (w::ext::reverbOn), tr ("On"));
        fxKnobs.push_back (knob (X (w::ext::reverbSize), tr ("Size")));
        fxKnobs.push_back (knob (X (w::ext::reverbDamp), tr ("Damp")));
        fxKnobs.push_back (knob (X (w::ext::reverbMix), tr ("Mix")));
        page.engine.addChangeListener (this);
        setSize (helixDeviceWidth, stripHeight);
    }
    ~HelixPlusStrip() override { page.engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.fillAll (l.outer);
        static const char* names[10] { "Warp 2", "Noise", "Unison+", "Filter+", "Macros", "LFO Draw", "Distortion", "Chorus", "Delay", "Reverb" };
        for (int i = 0; i < 10; ++i)
        {
            const auto r = sections[(std::size_t) i];
            g.setColour (l.bg);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            caption (g, l, r.withHeight (14).translated (6, 1).withWidth (r.getWidth() - 12), tr (names[i]));
        }
        g.setColour (l.dim);
        g.setFont (theme::font (9.5f));
        const auto w0 = sections[0];
        g.drawText ("1", w0.getX() + 3, w0.getY() + 34, 10, 16, juce::Justification::centred, false);
        g.drawText ("2", w0.getX() + 131, w0.getY() + 34, 10, 16, juce::Justification::centred, false);
        const auto u0 = sections[2];
        g.drawText (tr ("Voices"), u0.getX() + 8, u0.getY() + 16, 80, 12, juce::Justification::centredLeft, false);
        g.drawText (tr ("Stack"), u0.getX() + 8, u0.getY() + 43, 80, 12, juce::Justification::centredLeft, false);
        const auto f0 = sections[3];
        g.drawText ("1", f0.getX() + 4, f0.getY() + 20, 10, 16, juce::Justification::centred, false);
        g.drawText ("2", f0.getX() + 4, f0.getY() + 42, 10, 16, juce::Justification::centred, false);
    }

    void resized() override
    {
        // two rows of sections, left to right
        const int rowH = 72, gap = 4;
        const int top[2] { 2, 2 + rowH + gap };
        const int widths[10] { 264, 136, 220, 120, 236, 112, 208, 212, 236, 216 };
        int x = 2;
        for (int i = 0; i < 10; ++i)
        {
            if (i == 5)
                x = 2;
            sections[(std::size_t) i] = { x, top[i < 5 ? 0 : 1], widths[i], rowH };
            x += widths[i] + gap;
        }
        const auto kn = [] (Ctl* c, juce::Rectangle<int> s, int col, int cols0 = 0) { c->setBounds (s.getX() + cols0 + col * 46, s.getY() + 16, 46, 54); };
        // warp 2
        const auto w0 = sections[0];
        for (int k = 0; k < 2; ++k) // (per oscillator: the mode menu, the amount knob beside it)
        {
            const auto x0 = w0.getX() + 14 + k * 128;
            warpMenu[(std::size_t) k]->setBounds (x0, w0.getY() + 34, 70, 16);
            warpAmt[(std::size_t) k]->setBounds (x0 + 72, w0.getY() + 16, 46, 54);
        }
        // noise
        const auto n0 = sections[1];
        noiseOn->setBounds (n0.getX() + 6, n0.getY() + 34, 36, 16);
        kn (noiseLevel, n0, 0, 44);
        kn (noiseColour, n0, 1, 44);
        // unison+
        const auto u0 = sections[2];
        unisonCount->setBounds (u0.getX() + 6, u0.getY() + 28, 80, 16);
        unisonStack->setBounds (u0.getX() + 6, u0.getY() + 55, 80, 16);
        kn (unisonBlend, u0, 0, 88);
        kn (unisonWidth, u0, 1, 88);
        // filter+
        const auto f0 = sections[3];
        for (int k = 0; k < 2; ++k)
            filterType[(std::size_t) k]->setBounds (f0.getX() + 16, f0.getY() + 20 + k * 22, f0.getWidth() - 22, 16);
        // macros: knob + handle
        const auto m0 = sections[4];
        for (int j = 0; j < w::macroCount; ++j)
        {
            macros[(std::size_t) j]->setBounds (m0.getX() + 4 + j * 58, m0.getY() + 16, 38, 54);
            macroHandle[(std::size_t) j]->setBounds (m0.getX() + 42 + j * 58, m0.getY() + 34, 14, 16);
        }
        // LFO draw
        const auto d0 = sections[5];
        for (int k = 0; k < 2; ++k)
            lfoDraw[(std::size_t) k]->setBounds (d0.getX() + 8, d0.getY() + 20 + k * 24, d0.getWidth() - 16, 18);
        // effects
        const auto e0 = sections[6], e1 = sections[7], e2 = sections[8], e3 = sections[9];
        fxOn[0]->setBounds (e0.getX() + 6, e0.getY() + 20, 36, 16);
        distMode->setBounds (e0.getX() + 6, e0.getY() + 44, 60, 16);
        kn (fxKnobs[0], e0, 0, 72);
        kn (fxKnobs[1], e0, 1, 72);
        fxOn[1]->setBounds (e1.getX() + 6, e1.getY() + 34, 36, 16);
        kn (fxKnobs[2], e1, 0, 44);
        kn (fxKnobs[3], e1, 1, 44);
        kn (fxKnobs[4], e1, 2, 44);
        fxOn[2]->setBounds (e2.getX() + 6, e2.getY() + 20, 36, 16);
        delayTime->setBounds (e2.getX() + 46, e2.getY() + 20, 50, 16);
        delayPing->setBounds (e2.getX() + 6, e2.getY() + 44, 90, 16);
        kn (fxKnobs[5], e2, 0, 100);
        kn (fxKnobs[6], e2, 1, 100);
        fxOn[3]->setBounds (e3.getX() + 6, e3.getY() + 34, 36, 16);
        kn (fxKnobs[7], e3, 0, 44);
        kn (fxKnobs[8], e3, 1, 44);
        kn (fxKnobs[9], e3, 2, 44);
    }

private:
    Ctl* knob (int index, juce::String cap)
    {
        ctls.push_back (std::make_unique<Ctl> (page, index, Ctl::Kind::knob, std::move (cap)));
        addAndMakeVisible (*ctls.back());
        return ctls.back().get();
    }
    Tab* tab (juce::String t, std::function<bool()> on, std::function<void()> click)
    {
        tabs.push_back (std::make_unique<Tab> (page, std::move (t), std::move (on), std::move (click)));
        addAndMakeVisible (*tabs.back());
        return tabs.back().get();
    }
    Tab* toggle (int index, juce::String t)
    {
        auto* b = tab (std::move (t), [this, index] { return page.value (index) > 0.5; }, [this, index] { page.set (index, page.value (index) > 0.5 ? 0.0 : 1.0); });
        b->switchStyle = true;
        return b;
    }
    Menu* menu (std::unique_ptr<Menu> m)
    {
        menus.push_back (std::move (m));
        addAndMakeVisible (*menus.back());
        return menus.back().get();
    }
    void setCount (int c)
    {
        page.set (w::extBase + w::ext::unisonCount, (double) c);
        if (c >= 2 && (int) std::lround (page.value (w::unisonMode)) == w::unisonNone) // (a count needs a unison mode: Classic)
            page.set (w::unisonMode, (double) w::unisonClassic);
        refreshAll();
    }
    void refreshAll()
    {
        repaint();
        for (auto* c : getChildren())
            c->repaint();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (page.def() != nullptr)
            refreshAll();
    }

    Page page;
    std::vector<std::unique_ptr<Ctl>> ctls;
    std::vector<std::unique_ptr<Tab>> tabs;
    std::vector<std::unique_ptr<Menu>> menus;
    std::array<juce::Rectangle<int>, 10> sections {};
    std::array<Menu*, 2> warpMenu {}, filterType {};
    std::array<Ctl*, 2> warpAmt {};
    std::array<Ctl*, 4> macros {};
    std::array<Tab*, 4> macroHandle {};
    std::array<Tab*, 2> lfoDraw {};
    std::array<Tab*, 4> fxOn {};
    std::vector<Ctl*> fxKnobs;
    Tab *noiseOn = nullptr, *delayPing = nullptr;
    Ctl *noiseLevel = nullptr, *noiseColour = nullptr, *unisonBlend = nullptr, *unisonWidth = nullptr;
    Menu *unisonCount = nullptr, *unisonStack = nullptr, *distMode = nullptr, *delayTime = nullptr;
};

// the channel window's page: the device with a margin, Helix+'s strip under it (round 190mi)
class HelixWindowPage final : public juce::Component, public juce::DragAndDropContainer
{
public:
    HelixWindowPage (AudioEngine& e, std::function<ChannelDef* (Song&)> locate, int channel) : panel (e, locate, channel, false), strip (e, locate, channel)
    {
        addAndMakeVisible (panel);
        addAndMakeVisible (strip);
        setSize (helixDeviceWidth + 16, helixDeviceHeight + 16 + 6 + HelixPlusStrip::stripHeight);
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg1); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        panel.setBounds (r.removeFromTop (helixDeviceHeight));
        r.removeFromTop (6);
        strip.setBounds (r.removeFromTop (HelixPlusStrip::stripHeight));
    }

private:
    HelixPanel panel;
    HelixPlusStrip strip;
};

// the Device View's card
class HelixCard final : public DetailCard
{
public:
    HelixCard (AudioEngine& e, int channel) : panel (e, [channel] (Song& s) -> ChannelDef* {
        return channel >= 0 && channel < (int) s.channels.size() ? &s.channels[(std::size_t) channel] : nullptr;
    }, channel, true)
    {
        addAndMakeVisible (panel);
    }
    // Live's device keeps its proportions: the body's height sets the scale (the Device View's body is a little lower
    // than Live's), the card is as wide as that makes it
    int preferredWidth (int bodyHeight) const override { return juce::roundToInt (helixDeviceWidth * (float) bodyHeight / (float) helixDeviceHeight); }
    void resized() override
    {
        const auto scale = (float) getHeight() / (float) helixDeviceHeight;
        panel.setBounds (0, 0, helixDeviceWidth, helixDeviceHeight);
        panel.setTransform (juce::AffineTransform::scale (scale > 0.0f ? scale : 1.0f));
    }

private:
    HelixPanel panel;
};
} // namespace

std::unique_ptr<juce::Component> makeHelixEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    return std::make_unique<HelixWindowPage> (engine, std::move (locate), channel);
}

std::unique_ptr<juce::Component> makeHelixDevice (AudioEngine& engine, int channel)
{
    return std::make_unique<HelixCard> (engine, channel);
}

namespace helixcards
{
void install()
{
    auto previous = DetailView::instrumentCardFactory;
    DetailView::instrumentCardFactory = [previous] (AudioEngine& engine, int channel, const ChannelDef& c) -> std::unique_ptr<DetailCard> {
        if (c.instrument == InstrumentKind::helix)
            return std::make_unique<HelixCard> (engine, channel);
        return previous != nullptr ? previous (engine, channel, c) : nullptr;
    };
}

int showView (juce::Component& root, int modTab, int source, int osc)
{
    int found = 0;
    if (auto* p = dynamic_cast<HelixPanel*> (&root))
    {
        p->showView (modTab, source, osc);
        ++found;
    }
    for (auto* c : root.getChildren())
        found += showView (*c, modTab, source, osc);
    return found;
}
} // namespace helixcards
} // namespace lynx
