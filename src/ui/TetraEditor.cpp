// The Tetra editor (ui/TetraEditor.h): LYNX's own look, drawn by LYNX code.

#include "ui/TetraEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/FmSynth.h"
#include "core/PluginLayout.h"
#include "engine/AudioEngine.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/PluginSkin.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
namespace
{
namespace f = fm;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// the operators' own colours (LYNX's palette: the accent red, amber, teal, violet)
juce::Colour opColour (int k)
{
    static const juce::Colour c[f::opCount] { juce::Colour (0xffe5383b), juce::Colour (0xfff2a33a), juce::Colour (0xff2ec4b6), juce::Colour (0xff9b7bff) };
    return c[std::clamp (k, 0, f::opCount - 1)];
}

// How a knob's travel maps onto the stored value
enum class Curve { linear, time, freq, fixedFreq, level };

double toValue (Curve c, double x, const EngineParam& p)
{
    x = std::clamp (x, 0.0, 1.0);
    switch (c)
    {
        case Curve::time: return x <= 0.0 ? 0.0 : 0.1 * std::pow (200000.0, x); // 0.1 ms … 20 s
        case Curve::freq: return 20.0 * std::pow (1000.0, x);                  // 20 Hz … 20 kHz
        case Curve::fixedFreq: return std::pow (1000.0, x);                    // 1 … 1000
        case Curve::level: return x <= 0.0 ? (double) p.low : std::pow (10.0, (70.0 * x - 70.0) / 20.0); // −70 … 0 dB
        case Curve::linear: break;
    }
    const auto v = p.low + x * (p.high - p.low);
    return p.stepped ? std::round (v) : v;
}

double toPosition (Curve c, double v, const EngineParam& p)
{
    switch (c)
    {
        case Curve::time: return v <= 0.1 ? 0.0 : std::log (v / 0.1) / std::log (200000.0);
        case Curve::freq: return std::log (std::max (v, 20.0) / 20.0) / std::log (1000.0);
        case Curve::fixedFreq: return std::log (std::max (v, 1.0)) / std::log (1000.0);
        case Curve::level: return v <= p.low * 1.0001 ? 0.0 : std::clamp ((20.0 * std::log10 (v) + 70.0) / 70.0, 0.0, 1.0);
        case Curve::linear: break;
    }
    return p.high > p.low ? std::clamp ((v - p.low) / (p.high - p.low), 0.0, 1.0) : 0.0;
}

// the page's shared state: the channel, its values and edits
struct Page
{
    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel = -1;

    const ChannelDef* def() const
    {
        const auto* c = locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr;
        return c != nullptr && c->instrument == InstrumentKind::tetra ? c : nullptr;
    }
    double value (int index) const
    {
        if (channel >= 0 && index < f::paramCount)
            if (const auto v = const_cast<AudioEngine&> (engine).getAutomationDisplay().valueOf ({ AutomationTargetKind::channelParam, channel, 0, index }))
                return tetraEngineParams()[(std::size_t) index].raw (*v);
        if (const auto* c = def(); c != nullptr && index < (int) c->modelParams.size())
            return c->modelParams[(std::size_t) index];
        return f::defaults()[(std::size_t) index];
    }
    void set (int index, double v, bool undoable = true)
    {
        engine.editSong ([l = locate, index, v] (Song& s) {
            auto* c = l ? l (s) : nullptr;
            if (c == nullptr || c->instrument != InstrumentKind::tetra)
                return;
            if ((int) c->modelParams.size() < f::stateSize)
            {
                const auto d = tetraDefaults();
                const auto have = c->modelParams.size();
                c->modelParams.resize ((std::size_t) f::stateSize);
                std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), c->modelParams.begin() + (std::ptrdiff_t) have);
            }
            c->modelParams[(std::size_t) index] = (float) v;
        }, undoable);
    }
    juce::String name() const
    {
        const auto* c = def();
        return c != nullptr && ! c->name.empty() ? u8 (c->name) : juce::String ("Tetra");
    }
};

skin::Style pageStyle() { return skin::resolve (skin::Style::glow); }

// --- controls ----------------------------------------------------------------------------------------------------------

class Knob final : public juce::Component, public ui::HintSource
{
public:
    Knob (Page& p, int i, juce::String caption, Curve c = Curve::linear, juce::Colour a = theme::colour::accent)
        : page (p), index (i), curve (c), label (std::move (caption)), accent (a)
    {
        setRepaintsOnMouseActivity (true);
    }

    void setIndex (int i) { index = i; repaint(); }
    const EngineParam& param() const { return tetraEngineParams()[(std::size_t) index]; }

    ui::ValueSpec spec() const
    {
        ui::ValueSpec s;
        s.name = page.name() + " - " + u8 (param().name); // (engine parameter names stay English, as plugin parameters)
        s.minimum = 0.0;
        s.maximum = 1.0;
        s.defaultValue = toPosition (curve, f::defaults()[(std::size_t) index], param());
        s.interval = param().stepped && curve == Curve::linear ? 1.0 / std::max (1.0, (double) (param().high - param().low)) : 0.0;
        s.pixelsForRange = param().stepped ? std::clamp (8.0 * (param().high - param().low), 60.0, 300.0) : 180.0;
        const auto i = index;
        const auto c = curve;
        s.toText = [i, c] (double x) { return u8 (f::paramText (i, toValue (c, x, tetraEngineParams()[(std::size_t) i]))); };
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
        const skin::StyleScope scope { pageStyle() };
        auto area = getLocalBounds().toFloat();
        const auto caption = area.removeFromBottom (12.0f);
        const auto valueArea = area.removeFromBottom (11.0f);
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        skin::KnobLook look;
        look.value = (float) position();
        look.bipolar = param().low < 0.0f && param().high > 0.0f;
        look.accent = accent;
        look.hover = isMouseOver() ? 1.0f : 0.0f;
        look.active = drag.isActive();
        skin::knob (g, juce::Rectangle<float> (size, size).withCentre (area.getCentre()), look);
        g.setFont (theme::font (9.5f));
        g.setColour (skin::valueColour (isMouseOverOrDragging()));
        g.drawFittedText (spec().text (position()), valueArea.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        g.setColour (skin::captionColour());
        g.setFont (theme::font (9.0f, true));
        g.drawFittedText (theme::caps (label), caption.toNearestInt(), juce::Justification::centred, 1, 0.6f);
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
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        drag.end();
        ui::hideValueTip();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { setPosition (spec().defaultValue); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (const auto v = ui::wheelValue (position(), e, w, spec()))
        {
            setPosition (*v);
            ui::flashValueTip (*this, getLocalBounds(), spec().text (position()));
        }
    }

private:
    Page& page;
    int index;
    Curve curve;
    juce::String label;
    juce::Colour accent;
    ui::ValueDrag drag;
};

// an on / off switch of one parameter
class Toggle final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Toggle (Page& p, int i, juce::String caption, juce::String tip, juce::Colour a = theme::colour::accent)
        : page (p), index (i), label (std::move (caption)), accent (a)
    {
        setTooltip (tip);
        setRepaintsOnMouseActivity (true);
    }
    void setIndex (int i) { index = i; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const skin::StyleScope scope { pageStyle() };
        const auto on = page.value (index) >= 0.5;
        auto r = getLocalBounds().toFloat();
        skin::switchPill (g, r.removeFromLeft (24.0f).withSizeKeepingCentre (22.0f, 12.0f), on, accent, true);
        r.removeFromLeft (4.0f);
        g.setColour (on ? theme::colour::textPrimary : theme::colour::textSecondary);
        g.setFont (theme::font (10.5f));
        g.drawFittedText (label, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        page.set (index, page.value (index) >= 0.5 ? 0.0 : 1.0);
        repaint();
    }

private:
    Page& page;
    int index;
    juce::String label;
    juce::Colour accent;
};

// a menu of one stepped parameter
class Choice final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Choice (Page& p, int i, juce::String caption) : page (p), index (i), label (std::move (caption)) { setRepaintsOnMouseActivity (true); }
    void setIndex (int i) { index = i; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const skin::StyleScope scope { pageStyle() };
        auto r = getLocalBounds().toFloat();
        if (label.isNotEmpty())
        {
            const auto cap = r.removeFromTop (11.0f);
            g.setColour (skin::captionColour());
            g.setFont (theme::font (9.0f, true));
            g.drawFittedText (theme::caps (label), cap.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
        }
        skin::selector (g, r.reduced (0.0f, 1.0f), isMouseOver());
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (10.5f));
        g.drawFittedText (text ((int) std::lround (page.value (index))) + juce::String::fromUTF8 (" \xe2\x96\xbe"), r.reduced (5.0f, 0.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.6f);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        const auto& p = tetraEngineParams()[(std::size_t) index];
        const auto current = (int) std::lround (page.value (index));
        juce::PopupMenu menu;
        for (int v = (int) p.low; v <= (int) p.high; ++v)
            menu.addItem (text (v), true, v == current, [this, v] {
                page.set (index, (double) v);
                repaint();
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

private:
    juce::String text (int v) const { return tr (u8 (f::paramText (index, (double) v))); }
    Page& page;
    int index;
    juce::String label;
};

// --- the algorithm strip: eleven routings, the selected one lit ---------------------------------------------------------
class AlgorithmStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit AlgorithmStrip (Page& p) : page (p)
    {
        setTooltip (tr ("Algorithm: how the four operators modulate each other (click one)"));
        setRepaintsOnMouseActivity (true);
    }

    static void drawRouting (juce::Graphics& g, juce::Rectangle<float> area, int algorithm, bool lit)
    {
        // operators as boxes in rows by depth (a modulator above what it feeds); carriers on the bottom row
        const auto& r = f::routing (algorithm);
        std::array<int, f::opCount> depth {};
        for (int k = 0; k < f::opCount; ++k)
            depth[(std::size_t) k] = 0;
        for (int pass = 0; pass < 4; ++pass)
            for (int k = 0; k < f::opCount; ++k)
                for (int j = 0; j < f::opCount; ++j)
                    if ((r.from[(std::size_t) k] >> j) & 1u)
                        depth[(std::size_t) j] = std::max (depth[(std::size_t) j], depth[(std::size_t) k] + 1);
        int maxDepth = 0;
        for (auto d : depth)
            maxDepth = std::max (maxDepth, d);
        std::array<juce::Rectangle<float>, f::opCount> box {};
        const auto rows = (float) (maxDepth + 1);
        const auto cell = std::min (area.getHeight() / rows, area.getWidth() / 4.0f);
        for (int d = 0; d <= maxDepth; ++d)
        {
            std::vector<int> here;
            for (int k = 0; k < f::opCount; ++k)
                if (depth[(std::size_t) k] == d)
                    here.push_back (k);
            const auto rowW = (float) here.size() * cell;
            for (std::size_t i = 0; i < here.size(); ++i)
                box[(std::size_t) here[i]] = juce::Rectangle<float> (area.getCentreX() - rowW * 0.5f + (float) i * cell, area.getBottom() - (float) (d + 1) * cell,
                                                                     cell, cell).reduced (cell * 0.18f);
        }
        g.setColour (lit ? theme::colour::textPrimary : theme::colour::textDisabled);
        for (int k = 0; k < f::opCount; ++k)
            for (int j = 0; j < f::opCount; ++j)
                if ((r.from[(std::size_t) k] >> j) & 1u)
                    g.drawLine (juce::Line<float> (box[(std::size_t) j].getCentre(), box[(std::size_t) k].getCentre()), 1.0f);
        for (int k = 0; k < f::opCount; ++k)
        {
            const auto b = box[(std::size_t) k];
            g.setColour (lit ? opColour (k) : opColour (k).withMultipliedSaturation (0.35f).withMultipliedBrightness (0.6f));
            g.fillRoundedRectangle (b, 2.0f);
            if ((r.carriers >> k) & 1u)
                g.fillRect (juce::Rectangle<float> (b.getCentreX() - 0.75f, b.getBottom(), 1.5f, std::max (2.0f, cell * 0.16f)));
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto current = (int) std::lround (page.value (f::g::algorithm));
        for (int a = 0; a < f::algorithmCount; ++a)
        {
            const auto cellArea = cellOf (a);
            const bool sel = a == current, hot = isMouseOver() && cellArea.contains (getMouseXYRelative().toFloat());
            g.setColour (sel ? theme::colour::accentMuted : hot ? theme::colour::bg4 : theme::colour::bg0);
            g.fillRoundedRectangle (cellArea, 4.0f);
            if (sel)
            {
                g.setColour (theme::colour::accent);
                g.drawRoundedRectangle (cellArea.reduced (0.5f), 4.0f, 1.0f);
            }
            drawRouting (g, cellArea.reduced (3.0f, 4.0f), a, sel || hot);
        }
    }
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int a = 0; a < f::algorithmCount; ++a)
            if (cellOf (a).contains (e.position))
            {
                page.set (f::g::algorithm, (double) a);
                repaint();
            }
    }

private:
    juce::Rectangle<float> cellOf (int a) const
    {
        const auto w = (float) getWidth() / (float) f::algorithmCount;
        return juce::Rectangle<float> ((float) a * w, 0.0f, w, (float) getHeight()).reduced (2.0f, 0.0f);
    }
    Page& page;
};

// --- the envelope view: the selected envelope's curve, its segments by their own laws -------------------------------
class EnvelopeView final : public juce::Component
{
public:
    explicit EnvelopeView (Page& p) : page (p) {}
    int base = f::opBase (0) + f::op::envBase;
    bool pitch = false;
    juce::Colour colour = theme::colour::accent;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (r, 5.0f);
        r = r.reduced (8.0f, 8.0f);
        g.setColour (theme::colour::border);
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
        const auto v = [this] (int e) { return page.value (base + e); };
        const double a = v (f::env::attack), d = v (f::env::decay), rel = v (f::env::release);
        const double hold = std::max (200.0, 0.3 * (a + d + rel));
        const double total = std::max (1.0, a + d + hold + rel);
        // levels: gains in dB (−70 … 0) or semitones (−48 … 48)
        const auto y = [&] (double level) {
            double t = pitch ? (level + 48.0) / 96.0 : (level <= 0.000317 ? 0.0 : (20.0 * std::log10 (level) + 70.0) / 70.0);
            return r.getBottom() - (float) std::clamp (t, 0.0, 1.0) * r.getHeight();
        };
        const auto x = [&] (double ms) { return r.getX() + (float) (ms / total) * r.getWidth(); };
        juce::Path path;
        const auto seg = [&] (double t0, double t1, double l0, double l1, double slope, bool first) {
            for (int i = 0; i <= 24; ++i)
            {
                const auto s = (double) f::segment ((float) i / 24.0f, (float) slope);
                const auto lv = l0 + (l1 - l0) * s;
                const auto px = x (t0 + (t1 - t0) * i / 24.0), py = y (pitch ? lv : std::max (lv, 0.000317));
                if (first && i == 0)
                    path.startNewSubPath (px, py);
                else
                    path.lineTo (px, py);
            }
        };
        seg (0.0, a, v (f::env::initial), v (f::env::peak), v (f::env::attackSlope), true);
        seg (a, a + d, v (f::env::peak), v (f::env::sustain), v (f::env::decaySlope), false);
        path.lineTo (x (a + d + hold), y (v (f::env::sustain)));
        seg (a + d + hold, total, v (f::env::sustain), v (f::env::end), v (f::env::releaseSlope), false);
        g.setColour (colour.withAlpha (0.18f));
        auto fill = path;
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.6f));
        // the note-off line and the loop mode
        g.setColour (theme::colour::textDisabled);
        g.drawVerticalLine ((int) x (a + d + hold), r.getY(), r.getBottom());
        g.setFont (theme::font (9.5f));
        g.setColour (theme::colour::textSecondary);
        g.drawText (tr (u8 (f::paramText (base + f::env::loopMode, v (f::env::loopMode)))), r.removeFromTop (12.0f), juce::Justification::topRight, false);
    }

private:
    Page& page;
};

// --- the editor --------------------------------------------------------------------------------------------------------
class TetraEditor final : public juce::Component, private juce::ChangeListener
{
public:
    TetraEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> locate, int channel)
        : page { e, std::move (locate), channel }, algorithms (page), envelope (page)
    {
        addAndMakeVisible (algorithms);
        for (int k = 0; k < f::opCount; ++k)
        {
            const auto b = f::opBase (k);
            const auto c = opColour (k);
            auto& o = ops[(std::size_t) k];
            o.on = std::make_unique<Toggle> (page, b + f::op::on, juce::String::charToString ("ABCD"[k]), tr ("Operator on / off"), c);
            o.wave = std::make_unique<Choice> (page, b + f::op::wave, juce::String());
            o.knobs.push_back (std::make_unique<Knob> (page, b + f::op::coarse, tr ("Coarse"), Curve::linear, c));
            o.knobs.push_back (std::make_unique<Knob> (page, b + f::op::fine, tr ("Fine"), Curve::linear, c));
            o.knobs.push_back (std::make_unique<Knob> (page, b + f::op::level, tr ("Level"), Curve::level, c));
            o.knobs.push_back (std::make_unique<Knob> (page, b + f::op::feedback, tr ("Feedback"), Curve::linear, c));
            o.knobs.push_back (std::make_unique<Knob> (page, b + f::op::velScale, tr ("Vel"), Curve::linear, c));
            o.knobs.push_back (std::make_unique<Knob> (page, b + f::op::keyScale, tr ("Key"), Curve::linear, c));
            o.knobs.push_back (std::make_unique<Knob> (page, b + f::op::phase, tr ("Phase"), Curve::linear, c));
            o.knobs.push_back (std::make_unique<Knob> (page, b + f::op::fixedFreq, tr ("Freq"), Curve::fixedFreq, c));
            o.switches.push_back (std::make_unique<Toggle> (page, b + f::op::fixed, tr ("Fixed"), tr ("Fixed frequency: the operator ignores the key"), c));
            o.switches.push_back (std::make_unique<Toggle> (page, b + f::op::retrig, tr ("Retrig"), tr ("Restart the wave's phase on every note"), c));
            o.switches.push_back (std::make_unique<Toggle> (page, b + f::op::lfoOn, tr ("LFO"), tr ("The LFO moves this operator's pitch"), c));
            o.switches.push_back (std::make_unique<Toggle> (page, b + f::op::pitchEnvOn, tr ("Pitch Env"), tr ("The pitch envelope moves this operator"), c));
            o.range = std::make_unique<Choice> (page, b + f::op::fixedMulti, juce::String());
            addAndMakeVisible (*o.on);
            addAndMakeVisible (*o.wave);
            addAndMakeVisible (*o.range);
            for (auto& x : o.knobs)
                addAndMakeVisible (*x);
            for (auto& x : o.switches)
                addAndMakeVisible (*x);
        }
        // the envelope editor: tabs, the curve, the knobs of the selected envelope
        static const char* tabNames[7] { "A", "B", "C", "D", "Filter", "Pitch", "LFO" };
        for (int t = 0; t < 7; ++t)
        {
            auto b = std::make_unique<juce::TextButton> (t < 4 ? juce::String (tabNames[t]) : tr (tabNames[t]));
            b->setClickingTogglesState (false);
            b->onClick = [this, t] { selectEnvelope (t); };
            addAndMakeVisible (*b);
            tabs.push_back (std::move (b));
        }
        addAndMakeVisible (envelope);
        static const char* envCaptions[f::env::count] { "Attack", "Initial", "A Slope", "Decay", "Peak", "D Slope", "Sustain", "Release", "End",
                                                        "R Slope", "Loop", "Loop Time", "Repeat", "Time < Vel" };
        for (int e = 0; e < f::env::count; ++e)
        {
            if (e == f::env::loopMode || e == f::env::repeat)
            {
                envChoices.push_back (std::make_unique<Choice> (page, f::opBase (0) + f::op::envBase + e, tr (envCaptions[e])));
                addAndMakeVisible (*envChoices.back());
                continue;
            }
            const bool time = e == f::env::attack || e == f::env::decay || e == f::env::release || e == f::env::loopTime;
            const bool level = e == f::env::initial || e == f::env::peak || e == f::env::sustain || e == f::env::end;
            envKnobs.push_back ({ e, std::make_unique<Knob> (page, f::opBase (0) + f::op::envBase + e, tr (envCaptions[e]), time ? Curve::time : level ? Curve::level : Curve::linear) });
            addAndMakeVisible (*envKnobs.back().knob);
        }
        // filter, LFO, pitch envelope, global
        auto knob = [this] (int i, const char* caption, Curve c = Curve::linear) {
            sections.push_back (std::make_unique<Knob> (page, i, tr (caption), c));
            addAndMakeVisible (*sections.back());
            return sections.back().get();
        };
        auto toggle = [this] (int i, const char* caption, const char* tip) {
            switches.push_back (std::make_unique<Toggle> (page, i, tr (caption), tr (tip)));
            addAndMakeVisible (*switches.back());
            return switches.back().get();
        };
        auto choice = [this] (int i, const char* caption) {
            choices.push_back (std::make_unique<Choice> (page, i, tr (caption)));
            addAndMakeVisible (*choices.back());
            return choices.back().get();
        };
        filterOn = toggle (f::g::filterOn, "Filter", "Filter on / off");
        filterType = choice (f::g::filterType, "Type");
        filterSlope = choice (f::g::filterSlope, "Slope");
        filterCircuit = choice (f::g::filterCircuit, "Circuit");
        filterKnobs = { knob (f::g::filterFreq, "Freq", Curve::freq), knob (f::g::filterRes, "Res"), knob (f::g::filterMorph, "Morph"),
                        knob (f::g::filterDrive, "Drive"), knob (f::g::filterEnvAmount, "Envelope"), knob (f::g::filterKey, "Key"),
                        knob (f::g::filterVel, "Vel") };
        lfoOn = toggle (f::g::lfoOn, "LFO", "LFO on / off");
        lfoWave = choice (f::g::lfoType, "Wave");
        lfoRange = choice (f::g::lfoRange, "Range");
        lfoSync = choice (f::g::lfoSynced, "Sync Rate");
        lfoKnobs = { knob (f::g::lfoRate, "Rate"), knob (f::g::lfoAmount, "Amount"), knob (f::g::lfoAmountA, "Amount A") };
        lfoRetrig = toggle (f::g::lfoRetrig, "Retrig", "Restart the LFO on every note");
        lfoFilter = toggle (f::g::lfoFilter, "Filter", "The LFO moves the filter's frequency");
        penvOn = toggle (f::g::penvOn, "Pitch Env", "Pitch envelope on / off");
        penvKnobs = { knob (f::g::penvAmount, "Amount"), knob (f::g::penvAmountA, "Amount A") };
        shaper = choice (f::g::shaperType, "Shaper");
        shaperKnobs = { knob (f::g::shaperDrive, "Drive"), knob (f::g::shaperMix, "Dry/Wet") };
        globalKnobs = { knob (f::g::volume, "Volume", Curve::level), knob (f::g::pan, "Pan"), knob (f::g::transpose, "Transpose"),
                        knob (f::g::tone, "Tone"), knob (f::g::spread, "Spread"), knob (f::g::envTime, "Time"),
                        knob (f::g::glideTime, "Glide", Curve::time) };
        voices = choice (f::g::voices, "Voices");
        glide = toggle (f::g::glideOn, "Glide", "Glide from the last note");
        retrigger = toggle (f::g::retrigger, "Retrig", "A note on a sounding key restarts that voice");

        page.engine.addChangeListener (this);
        selectEnvelope (0);
        setSize (960, 584);
    }

    ~TetraEditor() override { page.engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg1);
        // the title strip
        auto top = getLocalBounds().removeFromTop (58).toFloat();
        g.setGradientFill (juce::ColourGradient (theme::colour::bg3, 0.0f, 0.0f, theme::colour::bg1, 0.0f, top.getBottom(), false));
        g.fillRect (top);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (24.0f, true));
        // (round 190pq: as a plugin in another host the title is the plugin's name, "LYNX Fourfold" -> FOURFOLD: Tetra is
        // another maker's product name)
        auto title = juce::String ("TETRA");
        if (plugui::hostedElsewhere)
            if (const auto* c = page.def(); c != nullptr && ! c->name.empty())
                title = u8 (c->name).fromFirstOccurrenceOf ("LYNX ", false, false).toUpperCase();
        g.drawFittedText (title, juce::Rectangle<float> (14.0f, 8.0f, 150.0f, 28.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (theme::colour::accent);
        g.fillRect (juce::Rectangle<float> (14.0f, 38.0f, 26.0f, 2.0f));
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (10.0f));
        g.drawText (tr ("4-operator FM"), juce::Rectangle<float> (46.0f, 32.0f, 110.0f, 14.0f), juce::Justification::centredLeft, false);
        // panels
        for (int k = 0; k < f::opCount; ++k)
        {
            const auto r = opArea (k).toFloat();
            g.setColour (theme::colour::bg2);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (opColour (k));
            g.fillRoundedRectangle (r.withHeight (3.0f).reduced (6.0f, 0.0f), 1.5f);
        }
        for (const auto& r : { envArea(), filterArea(), lfoArea(), pitchArea(), globalArea() })
        {
            g.setColour (theme::colour::bg2);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (9.5f, true));
        g.drawText (theme::caps (tr ("Envelope")), envArea().reduced (10, 4).removeFromTop (12), juce::Justification::centredLeft, false);
        g.drawText (theme::caps (tr ("Global")), globalArea().reduced (10, 4).removeFromTop (12), juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        algorithms.setBounds (170, 6, 600, 46);
        for (int k = 0; k < f::opCount; ++k)
        {
            auto r = opArea (k).reduced (8, 8);
            auto& o = ops[(std::size_t) k];
            auto head = r.removeFromTop (22);
            o.on->setBounds (head.removeFromLeft (44));
            o.wave->setBounds (head.removeFromLeft (110).reduced (0, 1));
            o.range->setBounds (head.reduced (2, 1));
            r.removeFromTop (6);
            auto row1 = r.removeFromTop (54), row2 = r.removeFromTop (54);
            const auto w = row1.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
            {
                o.knobs[(std::size_t) i]->setBounds (row1.removeFromLeft (w));
                o.knobs[(std::size_t) (4 + i)]->setBounds (row2.removeFromLeft (w));
            }
            r.removeFromTop (6);
            auto sw = r.removeFromTop (18);
            const auto sww = sw.getWidth() / 4;
            for (auto& x : o.switches)
                x->setBounds (sw.removeFromLeft (sww));
        }
        // envelope
        auto e = envArea().reduced (10, 6);
        e.removeFromTop (14);
        auto tabRow = e.removeFromLeft (56);
        const auto tabH = tabRow.getHeight() / (int) tabs.size();
        for (auto& t : tabs)
            t->setBounds (tabRow.removeFromTop (tabH).reduced (0, 1));
        e.removeFromLeft (6);
        envelope.setBounds (e.removeFromLeft (300));
        e.removeFromLeft (8);
        auto k1 = e.removeFromTop (e.getHeight() / 2), k2 = e;
        const auto kw = k1.getWidth() / 7;
        int n = 0;
        for (auto& k : envKnobs)
            k.knob->setBounds ((n++ < 7 ? k1 : k2).removeFromLeft (kw));
        for (auto& c : envChoices)
            c->setBounds (k2.removeFromLeft (kw + 10).reduced (2, 14));
        // filter
        auto fa = filterArea().reduced (8, 6);
        auto fh = fa.removeFromTop (34);
        filterOn->setBounds (fh.removeFromLeft (70).withTrimmedTop (12));
        filterType->setBounds (fh.removeFromLeft (84).reduced (2, 0));
        filterSlope->setBounds (fh.removeFromLeft (64).reduced (2, 0));
        filterCircuit->setBounds (fh.reduced (2, 0));
        fa.removeFromTop (4);
        const auto fw = fa.getWidth() / 7;
        for (auto* k : filterKnobs)
            k->setBounds (fa.removeFromLeft (fw));
        // LFO
        auto la = lfoArea().reduced (8, 6);
        auto lh = la.removeFromTop (34);
        lfoOn->setBounds (lh.removeFromLeft (56).withTrimmedTop (12));
        lfoWave->setBounds (lh.removeFromLeft (76).reduced (2, 0));
        lfoRange->setBounds (lh.removeFromLeft (58).reduced (2, 0));
        lfoSync->setBounds (lh.reduced (2, 0));
        la.removeFromTop (4);
        auto lsw = la.removeFromRight (64);
        lfoRetrig->setBounds (lsw.removeFromTop (lsw.getHeight() / 2).withSizeKeepingCentre (64, 18));
        lfoFilter->setBounds (lsw.withSizeKeepingCentre (64, 18));
        const auto lw = la.getWidth() / 3;
        for (auto* k : lfoKnobs)
            k->setBounds (la.removeFromLeft (lw));
        // pitch envelope + shaper
        auto pa = pitchArea().reduced (8, 6);
        auto ph = pa.removeFromTop (34);
        penvOn->setBounds (ph.removeFromLeft (84).withTrimmedTop (12));
        shaper->setBounds (ph.reduced (2, 0));
        pa.removeFromTop (4);
        const auto pw = pa.getWidth() / 4;
        for (auto* k : penvKnobs)
            k->setBounds (pa.removeFromLeft (pw));
        for (auto* k : shaperKnobs)
            k->setBounds (pa.removeFromLeft (pw));
        // global
        auto ga = globalArea().reduced (10, 6);
        ga.removeFromTop (14);
        auto gs = ga.removeFromRight (150);
        voices->setBounds (gs.removeFromTop (32).reduced (2, 0));
        auto sw = gs.removeFromTop (22);
        glide->setBounds (sw.removeFromLeft (sw.getWidth() / 2).reduced (2, 2));
        retrigger->setBounds (sw.reduced (2, 2));
        const auto gw = ga.getWidth() / 7;
        for (auto* k : globalKnobs)
            k->setBounds (ga.removeFromLeft (gw).withSizeKeepingCentre (gw, 62));
    }

private:
    struct OpControls
    {
        std::unique_ptr<Toggle> on;
        std::unique_ptr<Choice> wave, range;
        std::vector<std::unique_ptr<Knob>> knobs;
        std::vector<std::unique_ptr<Toggle>> switches;
    };
    struct EnvKnob
    {
        int offset;
        std::unique_ptr<Knob> knob;
    };

    static juce::Rectangle<int> opArea (int k) { return { 8 + k * 237, 62, 232, 186 }; }
    static juce::Rectangle<int> envArea() { return { 8, 254, 944, 140 }; }
    static juce::Rectangle<int> filterArea() { return { 8, 400, 400, 96 }; }
    static juce::Rectangle<int> lfoArea() { return { 414, 400, 330, 96 }; }
    static juce::Rectangle<int> pitchArea() { return { 750, 400, 202, 96 }; }
    static juce::Rectangle<int> globalArea() { return { 8, 502, 944, 76 }; }

    void selectEnvelope (int t)
    {
        selected = t;
        const int base = t < 4 ? f::opBase (t) + f::op::envBase : t == 4 ? f::g::filterEnv : t == 5 ? f::g::penv : f::g::lfoEnv;
        envelope.base = base;
        envelope.pitch = t == 5;
        envelope.colour = t < 4 ? opColour (t) : theme::colour::textPrimary;
        for (auto& k : envKnobs)
            k.knob->setIndex (base + k.offset);
        envChoices[0]->setIndex (base + f::env::loopMode);
        envChoices[1]->setIndex (base + f::env::repeat);
        for (int i = 0; i < (int) tabs.size(); ++i)
        {
            tabs[(std::size_t) i]->setColour (juce::TextButton::buttonColourId, i == t ? theme::colour::accentMuted : theme::colour::bg3);
            tabs[(std::size_t) i]->setColour (juce::TextButton::textColourOffId, i == t ? theme::colour::textPrimary : theme::colour::textSecondary);
        }
        repaint();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (page.def() != nullptr)
            repaintAll (*this);
    }
    static void repaintAll (juce::Component& c)
    {
        c.repaint();
        for (auto* child : c.getChildren())
            child->repaint();
    }

    Page page;
    AlgorithmStrip algorithms;
    std::array<OpControls, f::opCount> ops;
    std::vector<std::unique_ptr<juce::TextButton>> tabs;
    EnvelopeView envelope;
    std::vector<EnvKnob> envKnobs;
    std::vector<std::unique_ptr<Choice>> envChoices;
    std::vector<std::unique_ptr<Knob>> sections;
    std::vector<std::unique_ptr<Toggle>> switches;
    std::vector<std::unique_ptr<Choice>> choices;
    Toggle *filterOn = nullptr, *lfoOn = nullptr, *lfoRetrig = nullptr, *lfoFilter = nullptr, *penvOn = nullptr, *glide = nullptr, *retrigger = nullptr;
    Choice *filterType = nullptr, *filterSlope = nullptr, *filterCircuit = nullptr, *lfoWave = nullptr, *lfoRange = nullptr, *lfoSync = nullptr,
           *shaper = nullptr, *voices = nullptr;
    std::vector<Knob*> filterKnobs, lfoKnobs, penvKnobs, shaperKnobs, globalKnobs;
    int selected = 0;
};
} // namespace

std::unique_ptr<juce::Component> makeTetraEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    return std::make_unique<TetraEditor> (engine, std::move (locate), channel);
}
} // namespace lynx
