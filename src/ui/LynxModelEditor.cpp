// The LYNX Model editor (ui/LynxModelEditor.h): LYNX's own look, drawn by LYNX code.

#include "ui/LynxModelEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/physmod/LynxModel.h"
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
namespace p = lm::p;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// the models' own tints (the bar's material in the view; the knobs keep the accent)
juce::Colour material (int model)
{
    switch (model)
    {
        case lm::marimba: return juce::Colour (0xffb0703f);      // rosewood
        case lm::xylophone: return juce::Colour (0xffc58d52);    // padauk
        case lm::vibraphone: return juce::Colour (0xffb8bec8);   // aluminium
        case lm::glockenspiel: return juce::Colour (0xffd6dbe3); // steel
        case lm::tubularBells: return juce::Colour (0xffd9b25f); // brass
        case lm::kalimba: return juce::Colour (0xffc9ccd2);      // spring steel
        default: break;
    }
    return theme::colour::textSecondary;
}

const char* modelCaption (int model)
{
    static const char* n[lm::modelCount] = { TR_NOOP ("Marimba"), TR_NOOP ("Vibraphone"), TR_NOOP ("Xylophone"), TR_NOOP ("Glockenspiel"),
                                             TR_NOOP ("Tubular Bells"), TR_NOOP ("Kalimba") };
    return n[std::clamp (model, 0, lm::modelCount - 1)];
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
        return c != nullptr && c->instrument == InstrumentKind::lynxModel ? c : nullptr;
    }
    double value (int index) const
    {
        if (channel >= 0 && index < lm::paramCount)
            if (const auto v = const_cast<AudioEngine&> (engine).getAutomationDisplay().valueOf ({ AutomationTargetKind::channelParam, channel, 0, index }))
                return lynxModelEngineParams()[(std::size_t) index].raw (*v);
        if (const auto* c = def(); c != nullptr && index < (int) c->modelParams.size())
            return c->modelParams[(std::size_t) index];
        return lm::defaults()[(std::size_t) index];
    }
    int model() const { return std::clamp ((int) std::lround (value (p::model)), 0, lm::modelCount - 1); }
    void set (int index, double v, bool undoable = true)
    {
        engine.editSong ([l = locate, index, v] (Song& s) {
            auto* c = l ? l (s) : nullptr;
            if (c == nullptr || c->instrument != InstrumentKind::lynxModel)
                return;
            if ((int) c->modelParams.size() < lm::stateSize)
            {
                const auto d = lynxModelDefaults();
                const auto have = c->modelParams.size();
                c->modelParams.resize ((std::size_t) lm::stateSize);
                std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), c->modelParams.begin() + (std::ptrdiff_t) have);
            }
            c->modelParams[(std::size_t) index] = (float) v;
        }, undoable);
    }
    // a new model: its own starting values for the strike point and release (the knobs the tables define); the rest
    // stays as the user set it
    void setModel (int m)
    {
        const auto preset = lm::presetValues (m);
        engine.editSong ([l = locate, m, preset] (Song& s) {
            auto* c = l ? l (s) : nullptr;
            if (c == nullptr || c->instrument != InstrumentKind::lynxModel)
                return;
            if ((int) c->modelParams.size() < lm::stateSize)
            {
                const auto d = lynxModelDefaults();
                const auto have = c->modelParams.size();
                c->modelParams.resize ((std::size_t) lm::stateSize);
                std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), c->modelParams.begin() + (std::ptrdiff_t) have);
            }
            c->modelParams[(std::size_t) p::model] = (float) m;
            c->modelParams[(std::size_t) p::position] = (float) preset[(std::size_t) p::position];
            c->modelParams[(std::size_t) p::release] = (float) preset[(std::size_t) p::release];
        }, true);
    }
    juce::String name() const
    {
        const auto* c = def();
        return c != nullptr && ! c->name.empty() ? u8 (c->name) : juce::String ("LYNX Model");
    }
};

skin::Style pageStyle() { return skin::resolve (skin::Style::glow); }

// --- a knob of one parameter ---------------------------------------------------------------------------------------------
class Knob final : public juce::Component, public ui::HintSource
{
public:
    Knob (Page& pg, int i, juce::String caption) : page (pg), index (i), label (std::move (caption)) { setRepaintsOnMouseActivity (true); }

    const EngineParam& param() const { return lynxModelEngineParams()[(std::size_t) index]; }
    double position() const { return param().normalised ((float) page.value (index)); }
    void setPosition (double x)
    {
        page.set (index, param().raw ((float) std::clamp (x, 0.0, 1.0)));
        repaint();
        if (auto* parent = getParentComponent())
            parent->repaint();
    }

    ui::ValueSpec spec() const
    {
        ui::ValueSpec s;
        s.name = page.name() + " - " + u8 (param().name); // (engine parameter names stay English, as plugin parameters)
        s.minimum = 0.0;
        s.maximum = 1.0;
        s.defaultValue = param().normalised ((float) lm::presetValues (page.model())[(std::size_t) index]);
        s.pixelsForRange = 180.0;
        const auto i = index;
        s.toText = [i] (double x) { return tr (u8 (lm::paramText (i, lynxModelEngineParams()[(std::size_t) i].raw ((float) x)))); };
        s.fromText = ui::inverseOf (s.toText, 0.0, 1.0);
        if (page.channel >= 0)
            s.target = AutomationTarget { AutomationTargetKind::channelParam, page.channel, 0, index };
        return s;
    }

    ui::Hint hintAt (juce::Point<int>) override { return ui::valueHint (spec(), position(), ui::valueGestures()); }

    void paint (juce::Graphics& g) override
    {
        const skin::StyleScope scope { pageStyle() };
        auto area = getLocalBounds().toFloat();
        const auto caption = area.removeFromBottom (12.0f);
        const auto valueArea = area.removeFromBottom (12.0f);
        // (round 190oj WG-24: 6 px each side for the lit knob's glow, which the knob's own bounds cut with a hard edge)
        const auto size = juce::jmin (area.getWidth() - 12.0f, area.getHeight());
        skin::KnobLook look;
        look.value = (float) position();
        look.bipolar = param().low < 0.0f && param().high > 0.0f;
        look.accent = theme::colour::accent;
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
    juce::String label;
    ui::ValueDrag drag;
};

// --- the model row: six segments -------------------------------------------------------------------------------------------
class ModelRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModelRow (Page& pg) : page (pg)
    {
        setRepaintsOnMouseActivity (true);
        setTooltip (tr ("Model: the instrument the physical model plays"));
    }
    juce::Rectangle<float> segment (int m) const
    {
        const auto w = (float) getWidth() / (float) lm::modelCount;
        return { w * (float) m, 0.0f, w, (float) getHeight() };
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (r, 5.0f);
        const auto now = page.model();
        const auto mouse = getMouseXYRelative();
        for (int m = 0; m < lm::modelCount; ++m)
        {
            const auto s = segment (m).reduced (2.0f);
            const bool on = m == now, hot = isMouseOver() && segment (m).contains (mouse.toFloat());
            if (on || hot)
            {
                g.setColour (on ? theme::colour::bg5 : theme::colour::bg3);
                g.fillRoundedRectangle (s, 4.0f);
            }
            if (on)
            {
                g.setColour (theme::colour::accent);
                g.fillRoundedRectangle (s.withTop (s.getBottom() - 2.0f).reduced (8.0f, 0.0f), 1.0f);
            }
            g.setColour (on ? theme::colour::textPrimary : theme::colour::textSecondary);
            g.setFont (theme::font (11.0f, on));
            g.drawFittedText (tr (modelCaption (m)), s.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int m = 0; m < lm::modelCount; ++m)
            if (segment (m).contains (e.position) && m != page.model())
            {
                page.setModel (m);
                if (auto* parent = getParentComponent())
                    parent->repaint();
            }
    }

private:
    Page& page;
};

// --- the struck bar, its strike point and the fundamental's nodes ------------------------------------------------------------
class BarView final : public juce::Component, public ui::HintSource
{
public:
    explicit BarView (Page& pg) : page (pg) { setRepaintsOnMouseActivity (true); }

    const lm::Table& table() const { return lm::builtInTable (page.model()); }
    juce::Rectangle<float> barArea() const
    {
        auto r = getLocalBounds().toFloat().reduced (24.0f, 0.0f);
        const auto m = page.model();
        const auto h = m == lm::tubularBells ? 22.0f : m == lm::kalimba ? 12.0f : m == lm::glockenspiel ? 18.0f : 26.0f;
        return r.withSizeKeepingCentre (r.getWidth(), h).translated (0.0f, -6.0f);
    }
    // u (0…1 along the bar / tube / tine) of a position
    double uOf (double position) const
    {
        const auto& t = table();
        return t.posU0 + (t.posU1 - t.posU0) * std::clamp (position, 0.0, 1.0);
    }
    int xOfPosition (double position) const
    {
        const auto b = barArea();
        return (int) std::lround (b.getX() + b.getWidth() * uOf (position));
    }
    double positionAtX (int x) const
    {
        const auto& t = table();
        const auto b = barArea();
        auto u = (x - b.getX()) / std::max (b.getWidth(), 1.0f);
        if (t.beam == 0 && t.posU0 >= 0.45f && u < 0.5)
            u = 1.0 - u; // (a free-free bar is symmetric: either half moves the strike point)
        return std::clamp ((u - t.posU0) / (double) (t.posU1 - t.posU0), 0.0, 1.0);
    }

    ui::Hint hintAt (juce::Point<int>) override
    {
        ui::Hint h;
        h.text = tr ("Strike point: drag along the bar (Position). At a mode's node that mode is not excited");
        return h;
    }

    void paint (juce::Graphics& g) override
    {
        const auto m = page.model();
        const auto& t = table();
        const auto b = barArea();
        const auto col = material (m);
        // the support / mounting
        g.setColour (theme::colour::bg4);
        if (m == lm::kalimba)
            g.fillRoundedRectangle (juce::Rectangle<float> (b.getX() - 10.0f, b.getY() - 12.0f, 12.0f, b.getHeight() + 24.0f), 2.0f);
        else if (m == lm::tubularBells)
            g.fillEllipse (juce::Rectangle<float> (b.getX() - 6.0f, b.getCentreY() - 15.0f, 10.0f, 30.0f));
        else
            for (const auto node : { 0.224f, 0.776f }) // (bars rest on cords at the fundamental's nodes)
                g.fillRect (juce::Rectangle<float> (b.getX() + b.getWidth() * node - 1.5f, b.getBottom(), 3.0f, 9.0f));
        // the bar
        juce::ColourGradient grad (col.brighter (0.25f), 0.0f, b.getY(), col.darker (0.45f), 0.0f, b.getBottom(), false);
        g.setGradientFill (grad);
        if (m == lm::marimba || m == lm::xylophone || m == lm::vibraphone)
        {
            // undercut bar: thinner in the middle (the arch tunes the overtones)
            juce::Path path;
            const auto arch = b.getHeight() * 0.32f;
            path.startNewSubPath (b.getX(), b.getY());
            path.lineTo (b.getRight(), b.getY());
            path.lineTo (b.getRight(), b.getBottom());
            path.quadraticTo (b.getCentreX(), b.getBottom() - 2.0f * arch, b.getX(), b.getBottom());
            path.closeSubPath();
            g.fillPath (path);
        }
        else
            g.fillRoundedRectangle (b, m == lm::tubularBells ? b.getHeight() * 0.5f : 2.0f);
        // the fundamental's nodes (free-free: 22.4 % from each end)
        g.setColour (theme::colour::textSecondary.withAlpha (0.55f));
        if (t.beam == 0)
            for (const auto node : { 0.224f, 0.776f })
            {
                const auto x = b.getX() + b.getWidth() * node;
                g.drawDashedLine (juce::Line<float> (x, b.getY() - 8.0f, x, b.getY() - 2.0f), std::array<float, 2> { 2.0f, 2.0f }.data(), 2, 1.0f);
            }
        // the strike point
        const auto x = (float) xOfPosition (page.value (p::position));
        const auto hot = isMouseOver() || dragging;
        g.setColour (theme::colour::accent.withAlpha (hot ? 0.35f : 0.22f));
        g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x, b.getY() - 12.0f }));
        g.setColour (theme::colour::accent);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, b.getY() - 12.0f }));
        g.drawLine (x, b.getY() - 7.0f, x, b.getY() + 1.0f, 1.5f);
        // caption
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (9.5f));
        g.drawText (tr ("Strike point") + "  " + tr (u8 (lm::paramText (p::position, page.value (p::position)))),
                    getLocalBounds().removeFromBottom (14).reduced (24, 0), juce::Justification::centredLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = true;
        page.set (p::position, lynxModelEngineParams()[(std::size_t) p::position].raw ((float) positionAtX (e.x)), true);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        page.set (p::position, lynxModelEngineParams()[(std::size_t) p::position].raw ((float) positionAtX (e.x)), false);
        repaint();
        if (auto* parent = getParentComponent())
            parent->repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        repaint();
    }

private:
    Page& page;
    bool dragging = false;
};

// --- the editor ----------------------------------------------------------------------------------------------------------
class LynxModelEditor final : public juce::Component, private juce::ChangeListener
{
public:
    LynxModelEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> locate, int channel)
        : page { e, std::move (locate), channel }, models (page), bar (page)
    {
        addAndMakeVisible (models);
        addAndMakeVisible (bar);
        models.setComponentID ("model.models"); // (round 190pg: --demo; six equal segments, marimba first)
        bar.setComponentID ("model.bar");
        const auto knob = [this] (std::vector<std::unique_ptr<Knob>>& group, int index, const char* caption) {
            group.push_back (std::make_unique<Knob> (page, index, tr (caption)));
            group.back()->setComponentID ("model." + juce::String (caption).toLowerCase()); // (round 190pg: --demo, e.g. model.hardness)
            addAndMakeVisible (*group.back());
        };
        knob (mallet, p::hardness, TR_NOOP ("Hardness"));
        knob (mallet, p::position, TR_NOOP ("Position"));
        knob (mallet, p::velocity, TR_NOOP ("Velocity"));
        knob (mallet, p::noise, TR_NOOP ("Noise"));
        knob (body, p::damping, TR_NOOP ("Damping"));
        knob (body, p::resonator, TR_NOOP ("Resonator"));
        knob (body, p::motor, TR_NOOP ("Motor"));
        knob (body, p::release, TR_NOOP ("Release"));
        knob (output, p::tone, TR_NOOP ("Tone"));
        knob (output, p::volume, TR_NOOP ("Volume"));
        page.engine.addChangeListener (this);
        setSize (760, 332);
    }

    ~LynxModelEditor() override { page.engine.removeChangeListener (this); }

    juce::Rectangle<int> groupArea (int g) const
    {
        auto r = getLocalBounds().reduced (12).withTop (184).withTrimmedBottom (22);
        const int widths[3] = { 4, 4, 2 };
        const auto unit = (r.getWidth() - 2 * 8) / 10;
        for (int i = 0; i < g; ++i)
            r.removeFromLeft (unit * widths[i] + 8);
        return r.withWidth (unit * widths[g]);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg1);
        auto top = getLocalBounds().removeFromTop (58).toFloat();
        g.setGradientFill (juce::ColourGradient (theme::colour::bg3, 0.0f, 0.0f, theme::colour::bg1, 0.0f, top.getBottom(), false));
        g.fillRect (top);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (24.0f, true));
        g.drawText ("LYNX MODEL", juce::Rectangle<float> (14.0f, 8.0f, 200.0f, 28.0f), juce::Justification::centredLeft, false);
        g.setColour (theme::colour::accent);
        g.fillRect (juce::Rectangle<float> (14.0f, 38.0f, 26.0f, 2.0f));
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (10.0f));
        g.drawText (tr ("Physical model") + juce::String::fromUTF8 (" \xc2\xb7 ") + tr (modelCaption (page.model())),
                    juce::Rectangle<float> (46.0f, 32.0f, 220.0f, 14.0f), juce::Justification::centredLeft, false);
        // the bar's panel
        g.setColour (theme::colour::bg2);
        g.fillRoundedRectangle (bar.getBounds().toFloat().expanded (0.0f, 4.0f), 6.0f);
        const auto m = page.model();
        const juce::String titles[3] = { tr ("Mallet"),
                                         m == lm::tubularBells ? trc ("@physical model: Tube") : m == lm::kalimba ? trc ("@physical model: Tine")
                                                                                                     : trc ("@physical model: Bar"),
                                         tr ("Output") };
        for (int i = 0; i < 3; ++i)
        {
            const auto r = groupArea (i).toFloat();
            g.setColour (theme::colour::bg2);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (9.5f, true));
            g.drawText (theme::caps (titles[i]), r.reduced (10.0f, 5.0f).removeFromTop (12.0f), juce::Justification::centredLeft, false);
        }
        g.setColour (theme::colour::textDisabled);
        g.setFont (theme::font (9.0f));
        g.drawText (tr ("Modes fitted to CC0 recordings (VCSL). No samples are played."), getLocalBounds().removeFromBottom (20).reduced (14, 0),
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        models.setBounds (getWidth() - 12 - 468, 15, 468, 28);
        bar.setBounds (12, 70, getWidth() - 24, 98);
        const auto lay = [this] (std::vector<std::unique_ptr<Knob>>& group, int g) {
            auto r = groupArea (g).reduced (6, 6);
            r.removeFromTop (14);
            const auto w = r.getWidth() / (int) group.size();
            for (auto& k : group)
                k->setBounds (r.removeFromLeft (w).reduced (2, 0));
        };
        lay (mallet, 0);
        lay (body, 1);
        lay (output, 2);
    }

    BarView& barView() { return bar; }

private:
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
    ModelRow models;
    BarView bar;
    std::vector<std::unique_ptr<Knob>> mallet, body, output;
};
} // namespace

std::unique_ptr<juce::Component> makeLynxModelEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    return std::make_unique<LynxModelEditor> (engine, std::move (locate), channel);
}

namespace lynxmodelui
{
int strikeX (juce::Component& editor, double position)
{
    auto* e = dynamic_cast<LynxModelEditor*> (&editor);
    return e != nullptr ? e->barView().getX() + e->barView().xOfPosition (position) : -1;
}

double positionAtX (juce::Component& editor, int x)
{
    auto* e = dynamic_cast<LynxModelEditor*> (&editor);
    return e != nullptr ? e->barView().positionAtX (x - e->barView().getX()) : -1.0;
}
} // namespace lynxmodelui
} // namespace lynx
