// The Helix editor (ui/HelixEditor.h): Live's Wavetable layout (round 190ja shots), drawn by LYNX code.

#include "ui/HelixEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Song.h"
#include "core/WavetableSynth.h"
#include "engine/AudioEngine.h"
#include "ui/DetailView.h"
#include "ui/FileChoosers.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/LiveLook.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

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

enum class Curve { linear, freq, time, gain, rate, logRange };

double toValue (Curve c, double x, const EngineParam& p)
{
    x = std::clamp (x, 0.0, 1.0);
    switch (c)
    {
        case Curve::freq: return w::filterHz (x);
        case Curve::time: return x <= 0.0 ? 0.0 : std::max ((double) p.low, 0.0001 * std::pow (200000.0, x)); // 0.1 ms … 20 s
        case Curve::gain: return x <= 0.0 ? 0.0 : std::pow (10.0, (70.0 * x - 70.0) / 20.0);                 // −70 … 0 dB
        case Curve::rate: return 0.01 * std::pow (3000.0, x);                                                 // 0.01 … 30 Hz
        case Curve::logRange: return p.low * std::pow ((double) p.high / p.low, x);                         // (190ml: EQ Mid Freq)
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
        case Curve::logRange: return std::clamp (std::log (std::max (v, (double) p.low) / p.low) / std::log ((double) p.high / p.low), 0.0, 1.0);
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
    double xv2 (int ext2Index) const { return value (w::ext2Base + ext2Index); }
    bool plusLive() const { return value (w::extBase) >= 0.5; }
    bool plus2Live() const { return value (w::extBase) >= 1.5; }

    // --- Helix+ 2 (round 190ml): the channel's user tables and sample (its pluginState), decoded once per change
    struct UserCache
    {
        std::vector<std::uint8_t> bytes;
        w::HelixUserData data;
        std::array<std::shared_ptr<const w::Table>, w::oscCount> tables {};
    };
    std::shared_ptr<UserCache> cache = std::make_shared<UserCache>();
    const w::HelixUserData& userData() const
    {
        const auto* c = def();
        const auto& bytes = c != nullptr ? c->pluginState : cache->bytes;
        if (c != nullptr && bytes != cache->bytes)
        {
            cache->bytes = bytes;
            cache->data = {};
            (void) w::decodeUserData (cache->bytes, cache->data);
            cache->tables = {};
        }
        return cache->data;
    }
    const w::Table* userTable (int k) const
    {
        const auto& d = userData();
        if (k < 0 || k >= w::oscCount || d.tables[(std::size_t) k].empty())
            return nullptr;
        auto& t = cache->tables[(std::size_t) k];
        if (t == nullptr)
            t = w::userTableBuilt (d.tables[(std::size_t) k]);
        return t.get();
    }
    bool playsUser (int k) const { return plus2Live() && value (w::ext2Base + w::ext2::userTable + k) > 0.5 && userTable (k) != nullptr; }
    int factoryTable (int k) const { return std::clamp ((int) std::lround (value (w::oscTableIndex (k))), 0, w::tableCount() - 1); }
    // the table oscillator k plays (its user table or its factory one)
    const w::Table& tableOf (int k) const { return playsUser (k) ? *userTable (k) : w::table (factoryTable (k)); }
    juce::String tableNameOf (int k) const
    {
        return playsUser (k) ? u8 (userData().tables[(std::size_t) k].name.empty() ? std::string ("User") : userData().tables[(std::size_t) k].name)
                             : tr (w::tableName (factoryTable (k)));
    }
    // new user data for the channel (one undoable edit; `also` changes its values in the same step)
    void setUserData (const w::HelixUserData& d, std::function<void (ChannelDef&)> also = {}) const
    {
        auto bytes = std::make_shared<std::vector<std::uint8_t>> (w::encodeUserData (d));
        engine.editSong ([l = locate, bytes, also] (Song& s) {
            auto* c = l ? l (s) : nullptr;
            if (c == nullptr || c->instrument != InstrumentKind::helix)
                return;
            padHelixState (*c, true);
            c->pluginState = *bytes;
            if (also)
                also (*c);
        }, true);
    }
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
    bool liveKnob = false; // (Helix+ 2's card panel: Live's own knob, theme::livelook)

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
        if (liveKnob) // Live's knob (the Live-look card rules: ring #111111, needle, value arc from the default)
        {
            if (label.isNotEmpty())
                caption (g, l, r.removeFromTop (12), label, juce::Justification::centred);
            const auto valueRow = r.removeFromBottom (12);
            const auto s = (float) std::min (r.getWidth(), r.getHeight());
            theme::livelook::drawKnob (g, r.toFloat().withSizeKeepingCentre (s, s), (float) position(), (float) spec().defaultValue);
            g.setColour (l.accent);
            g.setFont (theme::font (9.5f));
            g.drawFittedText (text, valueRow, juce::Justification::centred, 1, 0.7f);
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
    bool powerStyle = false;  // (Helix+ 2's card panel) Live's power disc and the name beside it
    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        const auto on = isOn();
        if (powerStyle)
        {
            const auto h = (float) std::min (13, getHeight());
            theme::livelook::drawPower (g, { 1.0f, ((float) getHeight() - h) * 0.5f, h, h }, on);
            g.setColour (on ? l.text : l.dim);
            g.setFont (theme::font (10.0f));
            g.drawFittedText (text, getLocalBounds().withTrimmedLeft ((int) h + 5), juce::Justification::centredLeft, 1, 0.7f);
            return;
        }
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

// --- Helix+ 2 (round 190ml): audio files into user tables and the sample oscillator ------------------------------------
const char* audioPatterns() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"; }
bool isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}
// a file's audio (planar, up to `maxSeconds`); false with `error` when it cannot be read
bool readAudio (const juce::File& file, std::vector<float>& left, std::vector<float>& right, double& rate, double maxSeconds, juce::String& error,
                bool* cut = nullptr)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        error = tr ("could not read {0}", { file.getFullPathName() });
        return false;
    }
    rate = reader->sampleRate;
    const auto limit = (juce::int64) (maxSeconds * rate);
    const auto frames = (int) std::min (reader->lengthInSamples, limit);
    if (cut != nullptr)
        *cut = reader->lengthInSamples > limit;
    juce::AudioBuffer<float> buffer ((int) std::max (2u, reader->numChannels), frames);
    reader->read (&buffer, 0, frames, 0, true, true);
    left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    right.clear();
    if (reader->numChannels > 1)
        right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
    return true;
}
// a file into oscillator k's user table (it plays it); `note` says what the slicing found
bool importTable (const Page& page, int k, const juce::File& file, const w::ImportOptions& options, juce::String& note)
{
    std::vector<float> l, r;
    double rate = 48000.0;
    if (! readAudio (file, l, r, rate, 60.0, note))
        return false;
    for (std::size_t i = 0; i < r.size() && i < l.size(); ++i)
        l[i] = 0.5f * (l[i] + r[i]);
    std::string text;
    auto table = w::userTableFromAudio (l.data(), (int) l.size(), rate, options, file.getFileNameWithoutExtension().toStdString(), &text);
    note = u8 (text);
    if (table.empty())
        return false;
    auto d = page.userData();
    d.tables[(std::size_t) k] = std::move (table);
    page.setUserData (d, [k] (ChannelDef& c) { c.modelParams[(std::size_t) (w::ext2Base + w::ext2::userTable + k)] = 1.0f; });
    return true;
}
// a file into the channel's sample (10 s at most; its partials analysed and kept with it, round 190mt): `onIndex` (the
// sample oscillator's, the granular or the spectral oscillator's switch) switches on
bool loadSample (const Page& page, const juce::File& file, juce::String& note, int onIndex = w::ext2Base + w::ext2::sampleOn)
{
    std::vector<float> l, r;
    double rate = 48000.0;
    bool cut = false;
    if (! readAudio (file, l, r, rate, 10.0, note, &cut))
        return false;
    auto d = page.userData();
    d.sample.l = std::move (l);
    d.sample.r = std::move (r);
    d.sample.rate = rate;
    d.sampleName = file.getFileName().toStdString();
    d.spectral = w::analyseSpectrum (d.sample);
    page.setUserData (d, [onIndex] (ChannelDef& c) { c.modelParams[(std::size_t) onIndex] = 1.0f; });
    note = cut ? tr ("Only the first 10 seconds were loaded.") : juce::String();
    return true;
}
// (round 190mt) the spectral oscillator's switch: on a sample whose partials are not kept yet, they are analysed and kept
// with it in the same undoable step (so the sound stays the same in later versions)
void switchSpectral (const Page& page)
{
    const auto index = w::ext3Base + w::ext3::specOn;
    const bool on = page.value (index) > 0.5;
    const auto& data = page.userData();
    if (! on && data.sample.frames() > 0 && data.spectral.empty())
    {
        auto d = data;
        d.spectral = w::analyseSpectrum (d.sample);
        page.setUserData (d, [index] (ChannelDef& c) { c.modelParams[(std::size_t) index] = 1.0f; });
        return;
    }
    const_cast<Page&> (page).set (index, on ? 0.0 : 1.0);
}
void openTableEditor (const Page& page, int osc); // (below)

// --- the oscillator's table: its frames stacked (the playing one lit); drag up / down moves the position ---------------
class WaveDisplay final : public juce::Component, public juce::SettableTooltipClient, public juce::FileDragAndDropTarget
{
public:
    explicit WaveDisplay (Page& p) : page (p) { setTooltip (tr ("Wave table: drag up / down to move the position")); }
    // Helix+ 2 (LYNX's look): an audio file dropped here becomes the oscillator's user table; a double-click opens the editor
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return ! page.live && files.size() == 1 && isAudioFile (files[0]); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        juce::String note;
        if (! page.live && files.size() == 1)
            (void) importTable (page, page.osc, juce::File (files[0]), {}, note);
        if (page.refresh)
            page.refresh();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (! page.live)
            openTableEditor (page, page.osc);
    }
    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.setColour (l.dark);
        g.fillRect (getLocalBounds());
        const auto& t = page.tableOf (page.osc);
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
            case 4: // (LYNX's look, Helix+ 2: the eight macros and the chaos sources)
            {
                std::vector<int> c;
                for (int j = 0; j < w::macroCount; ++j)
                    c.push_back (w::src::macro1 + j);
                for (int k = 0; k < w::chaosCount; ++k)
                    c.push_back (w::src::chaos1 + k);
                return c;
            }
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
                if (s >= w::src::chaos1)
                    return "C" + juce::String (s - w::src::chaos1 + 1);
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
        category = menu (std::make_unique<Menu> (page, [this] { return page.playsUser (page.osc) ? tr ("User") : tr (w::tableCategory (currentTable())); },
                                                 [this] (juce::PopupMenu& m) { tableMenu (m, true); }));
        tableName = menu (std::make_unique<Menu> (page, [this] { return page.tableNameOf (page.osc); }, [this] (juce::PopupMenu& m) { tableMenu (m, false); }));
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
        static const char* modTabs[5] { "Mod Sources", "Matrix", "MIDI", "MPE", "Macros" };
        for (int t = 0; t < 5; ++t)
            modTab[(std::size_t) t] = tab (tr (modTabs[t]), [this, t] { return page.modTab == t; }, [this, t] { page.modTab = t; layoutMod(); });
        modTab[4]->setVisible (! page.live); // (Helix+ 2, LYNX's look: the macros' and chaos sources' columns)
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
        page.modTab = std::clamp (modTab, 0, page.live ? 3 : 4);
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
                m.addItem (tr (u8 (cat)), true, cat == w::tableCategory (current) && ! page.playsUser (page.osc), [this, first] { setTable (first); });
            else
                m.addSubMenu (tr (u8 (cat)), sub, true, nullptr, cat == w::tableCategory (current));
        }
        if (! page.live) // (Helix+ 2: the oscillator's own table)
        {
            m.addSeparator();
            if (page.userTable (page.osc) != nullptr)
                m.addItem (tr ("User table") + ": " + u8 (page.userData().tables[(std::size_t) page.osc].name), true, page.playsUser (page.osc), [this] {
                    page.set (w::ext2Base + w::ext2::userTable + page.osc, 1.0);
                    refreshAll();
                });
            m.addItem (tr ("Edit table..."), [this] { openTableEditor (page, page.osc); });
        }
    }
    void setTable (int t)
    {
        (void) w::table (t); // (built here, off the audio thread)
        page.set (w::tableBase + page.osc, (double) t);
        if (page.playsUser (page.osc)) // (a factory table chosen: the user table rests)
            page.set (w::ext2Base + w::ext2::userTable + page.osc, 0.0);
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
        if (! page.live) // (LYNX's look: five tabs, Helix+ 2's "Macros" last)
        {
            const int widths[5] { 74, 46, 40, 40, 52 };
            auto tx = x + wdt - 4 - (74 + 46 + 40 + 40 + 52 + 4 * 3);
            for (int t = 0; t < 5; ++t)
            {
                modTab[(std::size_t) t]->setBounds (tx, y + 1, widths[t], 15);
                tx += widths[t] + 3;
            }
        }
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
    std::array<Tab*, 5> modTab {};
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
class HelixPlusStrip final : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener
{
public:
    static constexpr int stripHeight = 150 + 3 * (72 + 4); // (190ml: two rows more for Helix+ 2; 190mt: one for round 3)
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
        makeHelixPlus2();
        page.engine.addChangeListener (this);
        setSize (helixDeviceWidth, stripHeight);
    }

    // --- Helix+ 2 (round 190ml): oscillator 3, the sample oscillator, the user tables, macros 5…8, the chaos sources and
    // the rack's shifter / EQ / multiband, in two more rows
    void makeHelixPlus2()
    {
        const auto Y = [] (int i) { return w::ext2Base + i; };
        const auto O = [Y] (int f) { return Y (w::ext2::osc3 + f); };
        osc3On = toggle (O (w::osc::on), tr ("On"));
        osc3Table = menu (std::make_unique<Menu> (page, [this] { return page.tableNameOf (2); }, [this] (juce::PopupMenu& m) {
            const auto current = page.factoryTable (2);
            for (const auto& cat : w::tableCategories())
            {
                juce::PopupMenu sub;
                for (int t = 0; t < w::tableCount(); ++t)
                    if (cat == w::tableCategory (t))
                        sub.addItem (tr (w::tableName (t)), true, t == current && ! page.playsUser (2), [this, t] {
                            (void) w::table (t);
                            page.set (w::oscTableIndex (2), (double) t);
                            if (page.playsUser (2))
                                page.set (w::ext2Base + w::ext2::userTable + 2, 0.0);
                            refreshAll();
                        });
                m.addSubMenu (tr (u8 (cat)), sub, true, nullptr, cat == w::tableCategory (current) && ! page.playsUser (2));
            }
            m.addSeparator();
            m.addItem (tr ("Edit table..."), [this] { openTableEditor (page, 2); });
        }));
        osc3Table->setTooltip (tr ("Oscillator 3's table"));
        osc3Edit = tab (tr ("Edit..."), [] { return false; }, [this] { openTableEditor (page, 2); });
        osc3Effect = menu (Menu::of (page, [O] { return O (w::osc::effectMode); }));
        osc3Warp = menu (Menu::of (page, [Y] { return Y (w::ext2::osc3WarpMode); }));
        osc3Warp->setTooltip (tr ("Second warp: bends, syncs or cross-modulates the oscillator's read of its table"));
        static const char* osc3Caps[8] { "Pos", "Gain", "Pan", "Semi", "Det", "FX 1", "FX 2", "Amount" };
        const int osc3Params[8] { O (w::osc::position), O (w::osc::gain), O (w::osc::pan), O (w::osc::transpose), O (w::osc::detune),
                                  O (w::osc::effect1), O (w::osc::effect2), Y (w::ext2::osc3WarpAmount) };
        for (int i = 0; i < 8; ++i)
            osc3Boxes.push_back (box (osc3Params[i], tr (osc3Caps[i]), i == 1 ? Curve::gain : Curve::linear));
        sampleOn = toggle (Y (w::ext2::sampleOn), tr ("On"));
        sampleLoad = tab (tr ("Load..."), [] { return false; }, [this] { chooseSample(); });
        sampleLoad->setTooltip (tr ("Load a sample (or drop an audio file on this section)"));
        sampleLoop = toggle (Y (w::ext2::sampleLoop), tr ("Loop"));
        sampleWarp = menu (Menu::of (page, [Y] { return Y (w::ext2::sampleWarpMode); }));
        sampleWarp->setTooltip (tr ("On a sample: Sync restarts it at the note, FM / AM / RM read oscillator 1, Crush steps it"));
        static const char* sampleCaps[6] { "Gain", "Pan", "Semi", "Det", "Start", "Amount" };
        const int sampleParams[6] { Y (w::ext2::sampleGain), Y (w::ext2::samplePan), Y (w::ext2::sampleTranspose), Y (w::ext2::sampleDetune),
                                    Y (w::ext2::sampleStart), Y (w::ext2::sampleWarpAmount) };
        for (int i = 0; i < 6; ++i)
            sampleBoxes.push_back (box (sampleParams[i], tr (sampleCaps[i]), i == 0 ? Curve::gain : Curve::linear));
        sampleRoot = menu (std::make_unique<Menu> (
            page, [this] { return juce::MidiMessage::getMidiNoteName ((int) std::lround (page.xv2 (w::ext2::sampleRoot)), true, true, 4); },
            [this] (juce::PopupMenu& m) {
                const auto now = (int) std::lround (page.xv2 (w::ext2::sampleRoot));
                for (int oct = 0; oct < 11; ++oct)
                {
                    juce::PopupMenu sub;
                    for (int k = oct * 12; k < std::min (128, oct * 12 + 12); ++k)
                        sub.addItem (juce::MidiMessage::getMidiNoteName (k, true, true, 4), true, k == now, [this, k] {
                            page.set (w::ext2Base + w::ext2::sampleRoot, (double) k);
                            refreshAll();
                        });
                    m.addSubMenu (juce::MidiMessage::getMidiNoteName (oct * 12, true, true, 4) + "...", sub, true, nullptr, now / 12 == oct);
                }
            }));
        sampleRoot->setTooltip (tr ("Root key: the note that plays the sample at its own pitch"));
        for (int k = 0; k < w::oscCount; ++k)
        {
            userOn[(std::size_t) k] = tab (tr ("User"), [this, k] { return page.playsUser (k); }, [this, k] {
                if (page.userTable (k) == nullptr)
                    openTableEditor (page, k);
                else
                    page.set (w::ext2Base + w::ext2::userTable + k, page.playsUser (k) ? 0.0 : 1.0);
            });
            userOn[(std::size_t) k]->switchStyle = true;
            userOn[(std::size_t) k]->setTooltip (tr ("The oscillator plays its user table (drop an audio file here to make one)"));
            userEdit[(std::size_t) k] = tab (tr ("Edit..."), [] { return false; }, [this, k] { openTableEditor (page, k); });
        }
        for (int j = w::plusMacroCount; j < w::macroCount; ++j)
        {
            macros2.push_back (knob (w::macroIndex (j), juce::String (j + 1)));
            macroHandle2.push_back (tab ("M" + juce::String (j + 1), [] { return false; }, [] {}));
            macroHandle2.back()->dragSource = w::src::macro1 + j;
            macroHandle2.back()->setTooltip (tr ("Drag onto a knob to modulate it"));
        }
        for (int k = 0; k < w::chaosCount; ++k)
        {
            const auto b = w::ext2::chaosMode + k * w::ext2::chaosStride;
            chaosHandle[(std::size_t) k] = tab ("C" + juce::String (k + 1), [] { return false; }, [] {});
            chaosHandle[(std::size_t) k]->dragSource = w::src::chaos1 + k;
            chaosHandle[(std::size_t) k]->setTooltip (tr ("Drag onto a knob to modulate it"));
            chaosMode[(std::size_t) k] = menu (Menu::of (page, [Y, b] { return Y (b); }));
            chaosBoxes.push_back (box (Y (b + 1), tr ("Rate"), Curve::rate));
            chaosBoxes.push_back (box (Y (b + 2), tr ("Amount")));
        }
        fx2On[0] = toggle (Y (w::ext2::shiftOn), tr ("On"));
        fx2Knobs.push_back (knob (Y (w::ext2::shift), tr ("Shift")));
        fx2Knobs.push_back (knob (Y (w::ext2::shiftMix), tr ("Mix")));
        fx2On[1] = toggle (Y (w::ext2::eqOn), tr ("On"));
        fx2Knobs.push_back (knob (Y (w::ext2::eqLow), tr ("Low")));
        fx2Knobs.push_back (knob (Y (w::ext2::eqMidFreq), tr ("Freq"), Curve::logRange));
        fx2Knobs.push_back (knob (Y (w::ext2::eqMid), tr ("Mid")));
        fx2Knobs.push_back (knob (Y (w::ext2::eqHigh), tr ("High")));
        fx2On[2] = toggle (Y (w::ext2::mbOn), tr ("On"));
        static const char* mbCaps[5] { "Depth", "Time", "Up", "Down", "Gain" };
        for (int i = 0; i < 5; ++i)
            fx2Knobs.push_back (knob (Y (w::ext2::mbDepth + i), tr (mbCaps[i])));
        makeRound3();
    }

    // --- Helix round 3 (round 190mt): the granular and spectral oscillators (a fifth row) and the sample's Clean switch
    void makeRound3()
    {
        const auto Z = [] (int i) { return w::ext3Base + i; };
        sampleClean = toggle (Z (w::ext3::sampleClean), tr ("Clean"));
        sampleClean->setTooltip (tr ("Clean: the sample pitched up reads band-limited copies of itself (no fold-over)"));
        grainOn = toggle (Z (w::ext3::grainOn), tr ("On"));
        grainLoad = tab (tr ("Load..."), [] { return false; }, [this] { chooseSample (w::ext3Base + w::ext3::grainOn); });
        grainLoad->setTooltip (tr ("Load a sample (or drop an audio file on this section)"));
        static const char* grainCaps[8] { "Position", "Size", "Density", "Spray", "Pitch", "Spread", "Gain", "Pan" };
        const int grainParams[8] { w::ext3::grainPosition, w::ext3::grainSize, w::ext3::grainDensity, w::ext3::grainSpray, w::ext3::grainPitch,
                                   w::ext3::grainSpread, w::ext3::grainGain, w::ext3::grainPan };
        for (int i = 0; i < 8; ++i)
            grainKnobs.push_back (knob (Z (grainParams[i]), tr (grainCaps[i]), i == 1 || i == 2 ? Curve::logRange : i == 6 ? Curve::gain : Curve::linear));
        specOn = tab (tr ("On"), [this] { return page.value (w::ext3Base + w::ext3::specOn) > 0.5; }, [this] { switchSpectral (page); });
        specOn->switchStyle = true;
        specLoad = tab (tr ("Load..."), [] { return false; }, [this] { chooseSample (w::ext3Base + w::ext3::specOn); });
        specLoad->setTooltip (tr ("Load a sample (or drop an audio file on this section)"));
        static const char* specCaps[10] { "Position", "Stretch", "Shift", "Tilt", "Band", "Width", "Partials", "Semi", "Gain", "Pan" };
        const int specParams[10] { w::ext3::specPosition, w::ext3::specStretch, w::ext3::specShift, w::ext3::specTilt, w::ext3::specBand,
                                   w::ext3::specWidth, w::ext3::specPartials, w::ext3::specTranspose, w::ext3::specGain, w::ext3::specPan };
        for (int i = 0; i < 10; ++i)
            specKnobs.push_back (knob (Z (specParams[i]), tr (specCaps[i]), i == 4 || i == 5 ? Curve::logRange : i == 8 ? Curve::gain : Curve::linear));
        grainOn->setTooltip (tr ("Granular: grains of the channel's sample (the Sample section's)"));
        specOn->setTooltip (tr ("Spectral: the sample's partials played as sines, stretched, shifted and filtered"));
    }

    // an audio file dropped on oscillator 3's section or a user table's row makes that oscillator's table; on the
    // sample's section it becomes the sample
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() == 1 && isAudioFile (files[0]); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        if (files.size() != 1)
            return;
        const juce::File file (files[0]);
        juce::String note;
        if (sections[11].contains (x, y))
            (void) loadSample (page, file, note);
        else if (sections[18].contains (x, y)) // (190mt: the granular / spectral section: the channel's sample, that one on)
            (void) loadSample (page, file, note, w::ext3Base + w::ext3::grainOn);
        else if (sections[19].contains (x, y))
            (void) loadSample (page, file, note, w::ext3Base + w::ext3::specOn);
        else if (sections[10].contains (x, y))
            (void) importTable (page, 2, file, {}, note);
        else if (sections[12].contains (x, y))
            (void) importTable (page, std::clamp ((y - sections[12].getY() - 14) / 18, 0, 2), file, {}, note);
        if (note.isNotEmpty()) // (what the slicing found, or why the file could not be read)
            ui::flashValueTip (*this, juce::Rectangle<int> (x - 150, y - 10, 300, 20), note);
        refreshAll();
    }
    ~HelixPlusStrip() override { page.engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.fillAll (l.outer);
        static const char* names[20] { "Warp 2", "Noise", "Unison+", "Filter+", "Macros", "LFO Draw", "Distortion", "Chorus", "Delay", "Reverb",
                                       "Oscillator 3", "Sample", "User tables", "Macros 5-8", "Chaos", "Shift", "EQ", "Multiband", "Granular",
                                       "Spectral" };
        for (int i = 0; i < 20; ++i)
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
        // Helix+ 2: the sample's name, the user tables' rows
        const auto s0 = sections[11];
        const auto& data = page.userData();
        g.setColour (data.sample.frames() > 0 ? l.text : l.dim);
        g.drawFittedText (data.sample.frames() > 0 ? u8 (data.sampleName) : tr ("No sample: drop an audio file here"), sampleName, juce::Justification::centredLeft, 1, 0.8f);
        // (190mt) the granular and spectral sections say what they play: the channel's sample, its partials
        g.setColour (l.dim);
        const auto spectralNote = data.sample.frames() <= 0 ? tr ("No sample")
                                  : data.spectral.empty() ? tr ("Partials made when switched on")
                                                          : tr ("{0} partials", { juce::String (data.spectral.partials) });
        g.drawFittedText (data.sample.frames() > 0 ? u8 (data.sampleName) : tr ("No sample"), grainName, juce::Justification::centredRight, 1, 0.7f);
        g.drawFittedText (spectralNote, specName, juce::Justification::centredRight, 1, 0.7f);
        const auto t0 = sections[12];
        for (int k = 0; k < w::oscCount; ++k)
        {
            const auto y = t0.getY() + 16 + k * 18;
            g.setColour (l.dim);
            g.drawText ("Osc " + juce::String (k + 1), t0.getX() + 6, y, 38, 16, juce::Justification::centredLeft, false);
            const auto& t = data.tables[(std::size_t) k];
            g.setColour (t.empty() ? l.dim : l.text);
            g.drawFittedText (t.empty() ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : u8 (t.name), juce::Rectangle<int> (t0.getX() + 140, y, t0.getWidth() - 144, 16),
                              juce::Justification::centredLeft, 1, 0.7f);
        }
    }

    void resized() override
    {
        // two rows of sections, left to right
        const int rowH = 72, gap = 4;
        const int top[5] { 2, 2 + rowH + gap, 2 + 2 * (rowH + gap), 2 + 3 * (rowH + gap), 2 + 4 * (rowH + gap) };
        const int widths[20] { 264, 136, 220, 120, 236, 112, 208, 212, 236, 216, 440, 360, 188, 236, 204, 128, 200, 216, 430, 562 }; // (190mt: the fifth row fits the strip's 1000 px)
        const int rowOf[20] { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4 };
        int x = 2;
        for (int i = 0; i < 20; ++i)
        {
            if (i > 0 && rowOf[i] != rowOf[i - 1])
                x = 2;
            sections[(std::size_t) i] = { x, top[rowOf[i]], widths[i], rowH };
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
        layoutHelixPlus2();
    }

    void layoutHelixPlus2()
    {
        const auto line = [] (juce::Rectangle<int> s) { return s.getY() + 16; };
        // oscillator 3: on, table, edit, effect, warp; then its values
        const auto o = sections[10];
        int x = o.getX() + 6;
        const auto place = [&x] (juce::Component* c, int y, int wdt, int h = 16) {
            c->setBounds (x, y, wdt, h);
            x += wdt + 4;
        };
        place (osc3On, line (o), 34);
        place (osc3Table, line (o), 150);
        place (osc3Edit, line (o), 44);
        place (osc3Effect, line (o), 66);
        place (osc3Warp, line (o), 100);
        for (int i = 0; i < (int) osc3Boxes.size(); ++i)
            osc3Boxes[(std::size_t) i]->setBounds (o.getX() + 6 + i * 54, o.getY() + 36, 50, 34);
        // the sample: on, load, its name, loop, warp; then its values and the root key
        const auto s = sections[11];
        x = s.getX() + 6;
        place (sampleOn, line (s), 34);
        place (sampleLoad, line (s), 46);
        sampleName = { x, line (s), 84, 16 };
        x += 88;
        place (sampleLoop, line (s), 40);
        place (sampleClean, line (s), 40);
        place (sampleWarp, line (s), 92);
        for (int i = 0; i < (int) sampleBoxes.size(); ++i)
            sampleBoxes[(std::size_t) i]->setBounds (s.getX() + 6 + i * 50, s.getY() + 36, 46, 34);
        sampleRoot->setBounds (s.getX() + 6 + 6 * 50, s.getY() + 50, 48, 18);
        // the user tables: a row per oscillator
        const auto t = sections[12];
        for (int k = 0; k < w::oscCount; ++k)
        {
            userOn[(std::size_t) k]->setBounds (t.getX() + 44, t.getY() + 16 + k * 18, 44, 16);
            userEdit[(std::size_t) k]->setBounds (t.getX() + 92, t.getY() + 16 + k * 18, 44, 16);
        }
        // macros 5…8 as 1…4 above
        const auto m = sections[13];
        for (int j = 0; j < (int) macros2.size(); ++j)
        {
            macros2[(std::size_t) j]->setBounds (m.getX() + 4 + j * 58, m.getY() + 16, 38, 54);
            macroHandle2[(std::size_t) j]->setBounds (m.getX() + 42 + j * 58, m.getY() + 34, 14, 16);
        }
        // chaos: a column each (handle + mode, then rate and amount)
        const auto c = sections[14];
        for (int k = 0; k < w::chaosCount; ++k)
        {
            const auto cx = c.getX() + 6 + k * 100;
            chaosHandle[(std::size_t) k]->setBounds (cx, line (c), 22, 16);
            chaosMode[(std::size_t) k]->setBounds (cx + 24, line (c), 70, 16);
            chaosBoxes[(std::size_t) (2 * k)]->setBounds (cx, c.getY() + 36, 46, 34);
            chaosBoxes[(std::size_t) (2 * k + 1)]->setBounds (cx + 48, c.getY() + 36, 46, 34);
        }
        // shift, EQ, multiband: the switch, then knobs
        const auto kn = [] (Ctl* k, juce::Rectangle<int> sec, int col, int from, int wdt) { k->setBounds (sec.getX() + from + col * wdt, sec.getY() + 16, wdt, 54); };
        const auto f0 = sections[15], f1 = sections[16], f2 = sections[17];
        fx2On[0]->setBounds (f0.getX() + 6, f0.getY() + 34, 34, 16);
        kn (fx2Knobs[0], f0, 0, 42, 42);
        kn (fx2Knobs[1], f0, 1, 42, 42);
        fx2On[1]->setBounds (f1.getX() + 6, f1.getY() + 34, 34, 16);
        for (int i = 0; i < 4; ++i)
            kn (fx2Knobs[(std::size_t) (2 + i)], f1, i, 42, 39);
        fx2On[2]->setBounds (f2.getX() + 6, f2.getY() + 34, 34, 16);
        for (int i = 0; i < 5; ++i)
            kn (fx2Knobs[(std::size_t) (6 + i)], f2, i, 42, 34);
        // (190mt) granular and spectral: the switch and Load... at the left, then their knobs; what they play at the top right
        const auto g0 = sections[18], s0 = sections[19];
        grainOn->setBounds (g0.getX() + 6, g0.getY() + 22, 44, 16);
        grainLoad->setBounds (g0.getX() + 6, g0.getY() + 46, 44, 16);
        for (int i = 0; i < (int) grainKnobs.size(); ++i)
            kn (grainKnobs[(std::size_t) i], g0, i, 56, 46);
        specOn->setBounds (s0.getX() + 6, s0.getY() + 22, 44, 16);
        specLoad->setBounds (s0.getX() + 6, s0.getY() + 46, 44, 16);
        for (int i = 0; i < (int) specKnobs.size(); ++i)
            kn (specKnobs[(std::size_t) i], s0, i, 56, 50);
        grainName = { g0.getX() + 80, g0.getY() + 1, g0.getWidth() - 86, 14 };
        specName = { s0.getX() + 80, s0.getY() + 1, s0.getWidth() - 86, 14 };
    }

private:
    Ctl* knob (int index, juce::String cap, Curve curve = Curve::linear)
    {
        ctls.push_back (std::make_unique<Ctl> (page, index, Ctl::Kind::knob, std::move (cap), curve));
        addAndMakeVisible (*ctls.back());
        return ctls.back().get();
    }
    Ctl* box (int index, juce::String cap, Curve curve = Curve::linear)
    {
        ctls.push_back (std::make_unique<Ctl> (page, index, Ctl::Kind::box, std::move (cap), curve));
        addAndMakeVisible (*ctls.back());
        return ctls.back().get();
    }
    void chooseSample (int onIndex = w::ext2Base + w::ext2::sampleOn)
    {
        chooser = ui::makeFileChooser (tr ("Load a sample"), {}, audioPatterns());
        ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<HelixPlusStrip> (this), onIndex] (const juce::FileChooser& c) {
                               if (safe == nullptr || ! c.getResult().existsAsFile())
                                   return;
                               juce::String note;
                               const auto at = onIndex == w::ext3Base + w::ext3::grainOn ? 18 : onIndex == w::ext3Base + w::ext3::specOn ? 19 : 11;
                               if (! loadSample (safe->page, c.getResult(), note, onIndex) || note.isNotEmpty())
                                   ui::flashValueTip (*safe, safe->sections[(std::size_t) at], note);
                               safe->refreshAll();
                           });
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
    std::array<juce::Rectangle<int>, 20> sections {};
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
    // Helix+ 2 (round 190ml)
    Tab *osc3On = nullptr, *osc3Edit = nullptr, *sampleOn = nullptr, *sampleLoad = nullptr, *sampleLoop = nullptr;
    Menu *osc3Table = nullptr, *osc3Effect = nullptr, *osc3Warp = nullptr, *sampleWarp = nullptr, *sampleRoot = nullptr;
    std::vector<Ctl*> osc3Boxes, sampleBoxes, macros2, chaosBoxes, fx2Knobs;
    std::vector<Tab*> macroHandle2;
    std::array<Tab*, w::oscCount> userOn {}, userEdit {};
    std::array<Tab*, w::chaosCount> chaosHandle {};
    std::array<Menu*, w::chaosCount> chaosMode {};
    std::array<Tab*, 3> fx2On {};
    juce::Rectangle<int> sampleName;
    // Helix round 3 (round 190mt)
    Tab *sampleClean = nullptr, *grainOn = nullptr, *grainLoad = nullptr, *specOn = nullptr, *specLoad = nullptr;
    std::vector<Ctl*> grainKnobs, specKnobs;
    juce::Rectangle<int> grainName, specName;
    std::unique_ptr<juce::FileChooser> chooser;
};

// --- Helix+ 2 (round 190ml): the table editor (LYNX's look): an oscillator's own table, drawn, typed as a formula,
// edited by its harmonics or imported from audio. Every finished gesture is one undoable edit of the channel (its
// pluginState); the editor follows the channel (undo, redo, another editor) between gestures.
class TableEditor final : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener
{
public:
    enum Mode
    {
        drawMode,
        harmonicsMode
    };
    TableEditor (const Page& p, int oscillator) : page (p), osc (std::clamp (oscillator, 0, w::oscCount - 1))
    {
        page.live = false;
        page.refresh = {};
        const auto button = [this] (juce::TextButton& b, const juce::String& text, std::function<void()> click, const juce::String& tip = {}) {
            b.setButtonText (text);
            b.onClick = std::move (click);
            if (tip.isNotEmpty())
                b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        button (fromFactory, tr ("From factory table"), [this] { startFromFactory(); }, tr ("Start from the oscillator's factory table"));
        button (importButton, tr ("Import audio..."), [this] { chooseImport(); }, tr ("Slice an audio file into the table's 64 frames (or drop a file here)"));
        button (exportButton, tr ("Export WAV..."), [this] { chooseExport(); }, tr ("Save the table as a WAV file: 64 frames of 2048 samples"));
        button (drawTab, tr ("Draw"), [this] { setMode (drawMode); });
        button (harmonicsTab, tr ("Harmonics"), [this] { setMode (harmonicsMode); });
        button (copyAll, tr ("Copy to all frames"), [this] { frameToAll(); });
        button (morph, tr ("Morph 1 to 64"), [this] { morphFrames(); }, tr ("Every frame between the first and the last a crossfade of the two"));
        button (applyFormula, tr ("Apply formula"), [this] { runFormula(); });
        drawTab.setClickingTogglesState (false);
        harmonicsTab.setClickingTogglesState (false);
        use.setButtonText (tr ("Oscillator plays this table"));
        use.onClick = [this] { page.set (w::ext2Base + w::ext2::userTable + osc, use.getToggleState() ? 1.0 : 0.0); };
        addAndMakeVisible (use);
        allFrames.setButtonText (tr ("All frames"));
        allFrames.setTooltip (tr ("A harmonic set on one frame is set on every frame"));
        addAndMakeVisible (allFrames);
        normalise.setButtonText (tr ("Normalise"));
        normalise.setToggleState (true, juce::dontSendNotification);
        phaseAlign.setButtonText (tr ("Phase-align"));
        phaseAlign.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (normalise);
        addAndMakeVisible (phaseAlign);
        slicing.addItem (tr ("Slice by pitch"), 1);
        slicing.addItem (tr ("Fixed frame size"), 2);
        slicing.addItem (tr ("Spread over the file"), 3);
        slicing.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (slicing);
        for (int size : { 256, 512, 1024, 2048, 4096 })
            frameSize.addItem (juce::String (size), size);
        frameSize.setSelectedId (2048, juce::dontSendNotification);
        frameSize.setTooltip (tr ("Samples per frame (fixed size and spread)"));
        addAndMakeVisible (frameSize);
        formula.setMultiLine (true, true);
        formula.setReturnKeyStartsNewLine (false);
        formula.setText ("sin(pi*x) * (1 - t) + saw(p) * t", false);
        formula.onReturnKey = [this] { runFormula(); };
        addAndMakeVisible (formula);
        frameSlider.setRange (1.0, (double) w::frameCount, 1.0);
        frameSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        frameSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 18);
        frameSlider.onValueChange = [this] {
            frame = (int) frameSlider.getValue() - 1;
            cycleFrame = -1;
            repaint();
        };
        addAndMakeVisible (frameSlider);
        reload();
        setMode (drawMode);
        page.engine.addChangeListener (this);
        setSize (820, 470);
    }
    ~TableEditor() override { page.engine.removeChangeListener (this); }

    void setMode (Mode m)
    {
        mode = m;
        drawTab.setToggleState (m == drawMode, juce::dontSendNotification);
        harmonicsTab.setToggleState (m == harmonicsMode, juce::dontSendNotification);
        repaint();
    }
    void setFrame (int f)
    {
        frameSlider.setValue (std::clamp (f, 0, w::frameCount - 1) + 1, juce::sendNotificationSync);
    }
    const w::UserTable& current() const { return table; }

    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() == 1 && isAudioFile (files[0]); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.size() == 1)
            importFile (juce::File (files[0]));
    }

    void paint (juce::Graphics& g) override
    {
        const auto l = lynxLook();
        g.fillAll (l.outer);
        g.setColour (l.bg);
        g.fillRoundedRectangle (view.toFloat().expanded (4.0f), 4.0f);
        g.fillRoundedRectangle (side.toFloat(), 4.0f);
        g.setColour (l.text);
        g.setFont (theme::font (13.0f));
        g.drawText (tr ("Oscillator {0}'s table", { juce::String (osc + 1) }) + ": " + u8 (table.name.empty() ? std::string ("-") : table.name), 12, 6, 520, 20,
                    juce::Justification::centredLeft, true);
        // the frame
        g.setColour (l.dark);
        g.fillRect (view);
        g.setColour (l.line);
        g.drawHorizontalLine (view.getCentreY(), (float) view.getX(), (float) view.getRight());
        if (mode == drawMode)
            paintCycle (g, l);
        else
            paintHarmonics (g, l);
        // the side panel's captions
        g.setColour (l.dim);
        g.setFont (theme::font (11.0f));
        g.drawText (tr ("Formula"), side.getX() + 8, side.getY() + 4, 200, 16, juce::Justification::centredLeft, false);
        g.drawFittedText (tr ("x: -1...1 over the cycle, p: 0...1, t: the frame 0...1, f: 0...63. "
                              "+ - * / % ^, < > == ? :, sin cos tan exp log sqrt abs min max clamp mix floor frac pow saw square tri rand"),
                          juce::Rectangle<int> (side.getX() + 8, formula.getBottom() + 30, side.getWidth() - 16, 44), juce::Justification::topLeft, 4, 0.8f);
        g.drawText (tr ("Import audio"), side.getX() + 8, slicing.getY() - 18, 200, 16, juce::Justification::centredLeft, false);
        g.setColour (error.isNotEmpty() ? l.accent : l.text);
        g.drawFittedText (error.isNotEmpty() ? error : note, juce::Rectangle<int> (12, getHeight() - 22, getWidth() - 24, 18), juce::Justification::centredLeft, 1, 0.8f);
    }

    void resized() override
    {
        view = { 16, 64, 500, 300 };
        side = { 532, 32, 276, 400 };
        drawTab.setBounds (16, 34, 70, 22);
        harmonicsTab.setBounds (90, 34, 90, 22);
        use.setBounds (300, 34, 216, 22);
        frameSlider.setBounds (12, 372, 300, 24);
        copyAll.setBounds (318, 372, 110, 24);
        morph.setBounds (432, 372, 88, 24);
        allFrames.setBounds (12, 404, 120, 22);
        fromFactory.setBounds (136, 404, 130, 24);
        exportButton.setBounds (270, 404, 110, 24);
        formula.setBounds (side.getX() + 8, side.getY() + 22, side.getWidth() - 16, 70);
        applyFormula.setBounds (side.getX() + 8, formula.getBottom() + 4, 110, 22);
        slicing.setBounds (side.getX() + 8, side.getY() + 214, side.getWidth() - 16, 22);
        frameSize.setBounds (side.getX() + 8, side.getY() + 242, 90, 22);
        normalise.setBounds (side.getX() + 8, side.getY() + 270, 120, 22);
        phaseAlign.setBounds (side.getX() + 132, side.getY() + 270, 130, 22);
        importButton.setBounds (side.getX() + 8, side.getY() + 300, side.getWidth() - 16, 26);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! view.contains (e.getPosition()))
            return;
        if (mode == drawMode)
        {
            drawing = w::userTableCycle (table, frame, w::userFrameLength);
            last = e.getPosition();
            drawTo (e.getPosition());
        }
        else
            setHarmonic (e.getPosition(), e.mods.isPopupMenu());
        dragging = true;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        if (mode == drawMode)
            drawTo (e.getPosition());
        else
            setHarmonic (e.getPosition(), e.mods.isPopupMenu());
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;
        dragging = false;
        if (mode == drawMode && ! drawing.empty())
        {
            w::setUserTableCycle (table, frame, drawing);
            drawing.clear();
        }
        commit();
    }

private:
    void reload()
    {
        const auto& d = page.userData();
        const auto& own = d.tables[(std::size_t) osc];
        table = own.empty() ? w::userTableOf (page.factoryTable (osc)) : own;
        cycleFrame = -1;
        use.setToggleState (page.playsUser (osc), juce::dontSendNotification);
        repaint();
    }
    void commit (bool play = true)
    {
        auto d = page.userData();
        d.tables[(std::size_t) osc] = table;
        const auto k = osc;
        page.setUserData (d, [k, play] (ChannelDef& c) {
            if (play)
                c.modelParams[(std::size_t) (w::ext2Base + w::ext2::userTable + k)] = 1.0f;
        });
        cycleFrame = -1;
        use.setToggleState (true, juce::dontSendNotification);
        repaint();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (dragging || page.def() == nullptr)
            return;
        // (an undo, a redo, another editor: the channel's table, or the factory one once the channel has none again)
        const auto& own = page.userData().tables[(std::size_t) osc];
        if (own.empty())
        {
            if (seen)
                seen = false, reload();
        }
        else
        {
            seen = true;
            if (own.partials != table.partials || own.amp != table.amp || own.phase != table.phase || own.name != table.name)
                reload();
        }
        use.setToggleState (page.playsUser (osc), juce::dontSendNotification);
    }

    void startFromFactory()
    {
        table = w::userTableOf (page.factoryTable (osc));
        commit();
    }
    void frameToAll()
    {
        for (int f = 0; f < w::frameCount; ++f)
            if (f != frame)
                for (int h = 0; h < table.partials; ++h)
                {
                    table.amp[(std::size_t) (f * table.partials + h)] = table.amp[(std::size_t) (frame * table.partials + h)];
                    table.phase[(std::size_t) (f * table.partials + h)] = table.phase[(std::size_t) (frame * table.partials + h)];
                }
        commit();
    }
    void morphFrames()
    {
        const auto last = w::frameCount - 1;
        for (int f = 1; f < last; ++f)
        {
            const auto wgt = (double) f / last;
            for (int h = 0; h < table.partials; ++h)
            {
                const auto a = std::polar ((double) table.amp[(std::size_t) h], (double) table.phase[(std::size_t) h]);
                const auto b = std::polar ((double) table.amp[(std::size_t) (last * table.partials + h)], (double) table.phase[(std::size_t) (last * table.partials + h)]);
                const auto c = a + (b - a) * wgt;
                table.amp[(std::size_t) (f * table.partials + h)] = (float) std::abs (c);
                table.phase[(std::size_t) (f * table.partials + h)] = (float) std::arg (c);
            }
        }
        commit();
    }
    void runFormula()
    {
        w::UserTable made;
        std::string why;
        if (! w::userTableFromFormula (formula.getText().toStdString(), made, why))
        {
            error = u8 (why);
            repaint();
            return;
        }
        error.clear();
        note = tr ("Formula applied to the 64 frames.");
        table = std::move (made);
        commit();
    }
    void importFile (const juce::File& file)
    {
        w::ImportOptions o;
        o.slicing = slicing.getSelectedId() == 2 ? w::Slicing::fixed : slicing.getSelectedId() == 3 ? w::Slicing::spread : w::Slicing::pitch;
        o.frameSize = std::max (16, frameSize.getSelectedId());
        o.normalise = normalise.getToggleState();
        o.phaseAlign = phaseAlign.getToggleState();
        juce::String text;
        if (importTable (page, osc, file, o, text))
        {
            error.clear();
            note = text;
            reload();
        }
        else
            error = text.isNotEmpty() ? text : tr ("could not read {0}", { file.getFullPathName() });
        repaint();
    }
    void chooseImport()
    {
        chooser = ui::makeFileChooser (tr ("Import audio as a wave table"), {}, audioPatterns());
        ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<TableEditor> (this)] (const juce::FileChooser& c) {
                               if (safe != nullptr && c.getResult().existsAsFile())
                                   safe->importFile (c.getResult());
                           });
    }
    void chooseExport()
    {
        chooser = ui::makeFileChooser (tr ("Export the wave table"), {}, "*.wav");
        ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                           [safe = juce::Component::SafePointer<TableEditor> (this)] (const juce::FileChooser& c) {
                               if (safe == nullptr || c.getResult() == juce::File())
                                   return;
                               const auto file = c.getResult().withFileExtension ("wav");
                               const auto bytes = w::userTableWav (safe->table);
                               const bool ok = file.replaceWithData (bytes.data(), bytes.size());
                               safe->note = ok ? tr ("Saved {0}", { file.getFullPathName() }) : juce::String();
                               safe->error = ok ? juce::String() : tr ("could not write {0}", { file.getFullPathName() });
                               safe->repaint();
                           });
    }

    // --- drawing
    float yOf (float v) const { return (float) view.getCentreY() - std::clamp (v, -1.2f, 1.2f) * (float) view.getHeight() * 0.4f; }
    float vOf (int y) const { return std::clamp (((float) view.getCentreY() - (float) y) / ((float) view.getHeight() * 0.4f), -1.0f, 1.0f); }
    void paintCycle (juce::Graphics& g, const Look& l)
    {
        // the neighbouring frames faint, this one bright (its drawing while the mouse draws)
        const auto pathOf = [this] (const std::vector<float>& c) {
            juce::Path p;
            const auto n = (int) c.size();
            for (int i = 0; i <= 250; ++i)
            {
                const auto v = c[(std::size_t) std::min (n - 1, i * n / 250)];
                const auto x = (float) view.getX() + (float) view.getWidth() * (float) i / 250.0f;
                i == 0 ? p.startNewSubPath (x, yOf (v)) : p.lineTo (x, yOf (v));
            }
            return p;
        };
        if (cycleFrame != frame)
        {
            cycle = w::userTableCycle (table, frame, 500);
            ghosts.clear();
            for (int f = 0; f < w::frameCount; f += 9)
                if (f != frame)
                    ghosts.push_back (pathOf (w::userTableCycle (table, f, 250)));
            cycleFrame = frame;
        }
        g.setColour (l.line.brighter (0.2f));
        for (const auto& p : ghosts)
            g.strokePath (p, juce::PathStrokeType (1.0f));
        g.setColour (l.accent);
        g.strokePath (pathOf (drawing.empty() ? cycle : drawing), juce::PathStrokeType (2.0f));
    }
    void drawTo (juce::Point<int> p)
    {
        const auto n = (int) drawing.size();
        const auto toIndex = [this, n] (int x) { return std::clamp ((int) std::lround ((double) (x - view.getX()) / view.getWidth() * n), 0, n - 1); };
        const auto i0 = toIndex (last.x), i1 = toIndex (p.x);
        const auto v0 = vOf (last.y), v1 = vOf (p.y);
        const auto lo = std::min (i0, i1), hi = std::max (i0, i1);
        for (int i = lo; i <= hi; ++i)
            drawing[(std::size_t) i] = hi == lo ? v1 : v0 + (v1 - v0) * (float) (i - i0) / (float) (i1 - i0 == 0 ? 1 : i1 - i0);
        last = p;
        repaint (view);
    }
    // --- harmonics: the first 64 partials of the frame as bars (the frame's loudest is the full height)
    static constexpr int shownPartials = 64;
    float frameTop() const
    {
        auto top = 1.0e-6f;
        for (int h = 0; h < table.partials; ++h)
            top = std::max (top, table.amp[(std::size_t) (frame * table.partials + h)]);
        return top;
    }
    void paintHarmonics (juce::Graphics& g, const Look& l)
    {
        const auto top = frameTop();
        const auto bw = (float) view.getWidth() / shownPartials;
        for (int h = 0; h < shownPartials && h < table.partials; ++h)
        {
            const auto a = table.amp[(std::size_t) (frame * table.partials + h)] / top;
            const auto bh = std::clamp (a, 0.0f, 1.0f) * (float) view.getHeight() * 0.95f;
            g.setColour (h == hotPartial ? l.accent.brighter (0.3f) : l.accent);
            g.fillRect ((float) view.getX() + h * bw + 1.0f, (float) view.getBottom() - bh, bw - 2.0f, bh);
        }
        g.setColour (l.dim);
        g.setFont (theme::font (10.0f));
        for (int h : { 1, 8, 16, 32, 64 })
            g.drawText (juce::String (h), (int) ((float) view.getX() + (h - 1) * bw), view.getBottom() + 2, 24, 12, juce::Justification::centredLeft, false);
    }
    void setHarmonic (juce::Point<int> p, bool clear)
    {
        const auto h = std::clamp ((int) ((float) (p.x - view.getX()) / ((float) view.getWidth() / shownPartials)), 0, shownPartials - 1);
        if (h >= table.partials)
        {
            // (the table grows to hold the partial)
            w::UserTable g;
            g.name = table.name;
            g.partials = h + 1;
            g.amp.assign ((std::size_t) w::frameCount * (std::size_t) g.partials, 0.0f);
            g.phase.assign (g.amp.size(), 0.0f);
            for (int f = 0; f < w::frameCount; ++f)
                for (int k = 0; k < table.partials; ++k)
                    g.amp[(std::size_t) (f * g.partials + k)] = table.amp[(std::size_t) (f * table.partials + k)],
                    g.phase[(std::size_t) (f * g.partials + k)] = table.phase[(std::size_t) (f * table.partials + k)];
            table = std::move (g);
        }
        const auto top = std::max (frameTop(), 1.0e-3f);
        const auto a = clear ? 0.0f : std::clamp ((float) (view.getBottom() - p.y) / ((float) view.getHeight() * 0.95f), 0.0f, 1.0f) * top;
        for (int f = 0; f < w::frameCount; ++f)
            if (f == frame || allFrames.getToggleState())
                table.amp[(std::size_t) (f * table.partials + h)] = a;
        hotPartial = h;
        repaint (view.expanded (2, 16));
    }

    Page page;
    int osc = 0, frame = 0, cycleFrame = -1, hotPartial = -1;
    Mode mode = drawMode;
    bool dragging = false, seen = false;
    w::UserTable table;
    std::vector<float> cycle, drawing;
    std::vector<juce::Path> ghosts;
    juce::Point<int> last;
    juce::Rectangle<int> view, side;
    juce::TextButton fromFactory, importButton, exportButton, drawTab, harmonicsTab, copyAll, morph, applyFormula;
    juce::ToggleButton use, allFrames, normalise, phaseAlign;
    juce::ComboBox slicing, frameSize;
    juce::TextEditor formula;
    juce::Slider frameSlider;
    juce::String error, note;
    std::unique_ptr<juce::FileChooser> chooser;
};

void openTableEditor (const Page& page, int osc)
{
    if (page.def() == nullptr)
        return;
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = page.name() + " - " + tr ("Table editor") + " (Osc " + juce::String (osc + 1) + ")";
    options.content.setOwned (fitContentToScreen (std::make_unique<TableEditor> (page, osc)).release());
    options.dialogBackgroundColour = theme::colour::bg1;
    options.useNativeTitleBar = true;
    options.resizable = false;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

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

// --- Helix+ 2 (round 190ml): Helix+'s controls in the Clip-launch layout's Device View card, in Live's look (the card
// rules of round 190jn: #363636 sections on #2A2A2A, Live's knob and power disc, orange values, #B5B5B5 captions). A
// LYNX-own panel beside Live's device, shown by the card's title-bar well; Live's device itself stays as it was.
class HelixPlusCardPanel final : public juce::Component, private juce::ChangeListener
{
public:
    static constexpr int panelWidth = 600;
    HelixPlusCardPanel (AudioEngine& e, std::function<ChannelDef* (Song&)> locate, int channel) : page { e, std::move (locate), channel, true }
    {
        page.refresh = [this] { refreshAll(); };
        const auto X = [] (int i) { return w::extBase + i; };
        for (int k = 0; k < w::oscCount; ++k)
        {
            warp[(std::size_t) k] = menu (Menu::of (page, [k] { return w::warpModeIndex (k); }));
            warpAmt[(std::size_t) k] = box (w::warpAmountIndex (k), {});
        }
        osc3On = power (w::oscIndex (2, w::osc::on), tr ("Osc 3"));
        osc3Pos = box (w::oscIndex (2, w::osc::position), {});
        sampleOn = power (w::ext2Base + w::ext2::sampleOn, tr ("Sample"));
        sampleGain = box (w::ext2Base + w::ext2::sampleGain, {}, Curve::gain);
        noiseOn = power (X (w::ext::noiseOn), tr ("Noise"));
        noiseLevel = box (X (w::ext::noiseLevel), {});
        grainOn = power (w::ext3Base + w::ext3::grainOn, tr ("Granular")); // (190mt)
        grainPos = box (w::ext3Base + w::ext3::grainPosition, {});
        specOn = power (w::ext3Base + w::ext3::specOn, tr ("Spectral"));
        specPos = box (w::ext3Base + w::ext3::specPosition, {});
        unisonCount = menu (std::make_unique<Menu> (
            page, [this] { return tr (u8 (w::paramText (w::extBase + w::ext::unisonCount, page.xv (w::ext::unisonCount)))); },
            [this] (juce::PopupMenu& m) {
                const auto now = (int) std::lround (page.xv (w::ext::unisonCount));
                m.addItem (tr ("As device"), true, now < 2, [this] { page.set (w::extBase + w::ext::unisonCount, 0.0); });
                for (int c = 2; c <= w::maxUnison; ++c)
                    m.addItem (juce::String (c), true, now == c, [this, c] {
                        page.set (w::extBase + w::ext::unisonCount, (double) c);
                        if ((int) std::lround (page.value (w::unisonMode)) == w::unisonNone)
                            page.set (w::unisonMode, (double) w::unisonClassic);
                    });
            }));
        unisonStack = menu (Menu::of (page, [X] { return X (w::ext::unisonStack); }));
        unisonBlend = box (X (w::ext::unisonBlend), tr ("Blend"));
        unisonWidth = box (X (w::ext::unisonWidth), tr ("Width"));
        for (int j = 0; j < w::macroCount; ++j)
        {
            auto* k = knob (w::macroIndex (j), tr ("Macro") + " " + juce::String (j + 1));
            k->liveKnob = true;
            macros.push_back (k);
        }
        static const int fxOn[7] { w::extBase + w::ext::distOn, w::extBase + w::ext::chorusOn, w::extBase + w::ext::delayOn, w::extBase + w::ext::reverbOn,
                                   w::ext2Base + w::ext2::shiftOn, w::ext2Base + w::ext2::eqOn, w::ext2Base + w::ext2::mbOn };
        static const int fxAmt[7] { w::extBase + w::ext::distMix, w::extBase + w::ext::chorusMix, w::extBase + w::ext::delayMix, w::extBase + w::ext::reverbMix,
                                    w::ext2Base + w::ext2::shiftMix, w::ext2Base + w::ext2::eqLow, w::ext2Base + w::ext2::mbDepth };
        static const char* fxNames[7] { "Distortion", "Chorus", "Delay", "Reverb", "Shift", "EQ", "Multiband" };
        for (int i = 0; i < 7; ++i)
        {
            rack.push_back (power (fxOn[i], tr (fxNames[i])));
            rackAmt.push_back (box (fxAmt[i], {}));
        }
        page.engine.addChangeListener (this);
        setSize (panelWidth, helixDeviceHeight);
    }
    ~HelixPlusCardPanel() override { page.engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const auto l = page.look();
        g.fillAll (l.outer);
        static const char* names[4] { "Oscillators+", "Unison+", "Macros", "Rack" };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (l.bg);
            g.fillRoundedRectangle (sections[(std::size_t) i].toFloat(), 3.0f);
            caption (g, l, sections[(std::size_t) i].withHeight (14).translated (5, 1), tr (names[i]));
        }
        g.setColour (l.text);
        g.setFont (theme::font (10.0f));
        const auto s0 = sections[0];
        for (int k = 0; k < w::oscCount; ++k)
            g.drawText ("Warp " + juce::String (k + 1), s0.getX() + 6, s0.getY() + 18 + k * 20, 46, 16, juce::Justification::centredLeft, false);
        const auto s1 = sections[1];
        g.drawText (tr ("Voices"), s1.getX() + 6, s1.getY() + 18, 46, 16, juce::Justification::centredLeft, false);
        g.drawText (tr ("Stack"), s1.getX() + 6, s1.getY() + 38, 46, 16, juce::Justification::centredLeft, false);
    }
    void resized() override
    {
        const auto h = getHeight() - 4;
        const int widths[4] { 182, 112, 160, 104 }; // (the card's width rounds with its scale: some room kept at the right)
        int x = 2;
        for (int i = 0; i < 4; ++i)
        {
            sections[(std::size_t) i] = { x, 2, widths[i], h };
            x += widths[i] + 3;
        }
        const auto s0 = sections[0];
        for (int k = 0; k < w::oscCount; ++k)
        {
            warp[(std::size_t) k]->setBounds (s0.getX() + 50, s0.getY() + 18 + k * 20, 84, 16);
            warpAmt[(std::size_t) k]->setBounds (s0.getX() + 136, s0.getY() + 18 + k * 20, 42, 16);
        }
        const auto row = [s0] (int i) { return s0.getY() + 80 + i * 18; }; // (190mt: five rows, 18 px apart)
        osc3On->setBounds (s0.getX() + 6, row (0), 96, 16);
        osc3Pos->setBounds (s0.getX() + 136, row (0), 42, 16);
        sampleOn->setBounds (s0.getX() + 6, row (1), 96, 16);
        sampleGain->setBounds (s0.getX() + 136, row (1), 42, 16);
        noiseOn->setBounds (s0.getX() + 6, row (2), 96, 16);
        noiseLevel->setBounds (s0.getX() + 136, row (2), 42, 16);
        grainOn->setBounds (s0.getX() + 6, row (3), 96, 16);
        grainPos->setBounds (s0.getX() + 136, row (3), 42, 16);
        specOn->setBounds (s0.getX() + 6, row (4), 96, 16);
        specPos->setBounds (s0.getX() + 136, row (4), 42, 16);
        const auto s1 = sections[1];
        unisonCount->setBounds (s1.getX() + 48, s1.getY() + 18, 60, 16);
        unisonStack->setBounds (s1.getX() + 48, s1.getY() + 38, 60, 16);
        unisonBlend->setBounds (s1.getX() + 6, s1.getY() + 64, s1.getWidth() - 12, 30);
        unisonWidth->setBounds (s1.getX() + 6, s1.getY() + 100, s1.getWidth() - 12, 30);
        const auto s2 = sections[2];
        for (int j = 0; j < (int) macros.size(); ++j)
            macros[(std::size_t) j]->setBounds (s2.getX() + 4 + (j % 4) * 38, s2.getY() + 16 + (j / 4) * 78, 38, 74);
        const auto s3 = sections[3];
        for (int i = 0; i < (int) rack.size(); ++i)
        {
            rack[(std::size_t) i]->setBounds (s3.getX() + 5, s3.getY() + 17 + i * 22, 62, 16);
            rackAmt[(std::size_t) i]->setBounds (s3.getX() + 68, s3.getY() + 17 + i * 22, 32, 16);
        }
    }

private:
    Ctl* knob (int index, juce::String cap, Curve curve = Curve::linear)
    {
        ctls.push_back (std::make_unique<Ctl> (page, index, Ctl::Kind::knob, std::move (cap), curve));
        addAndMakeVisible (*ctls.back());
        return ctls.back().get();
    }
    Ctl* box (int index, juce::String cap, Curve curve = Curve::linear)
    {
        ctls.push_back (std::make_unique<Ctl> (page, index, Ctl::Kind::box, std::move (cap), curve));
        addAndMakeVisible (*ctls.back());
        return ctls.back().get();
    }
    Tab* power (int index, juce::String text)
    {
        tabs.push_back (std::make_unique<Tab> (page, std::move (text), [this, index] { return page.value (index) > 0.5; },
                                               [this, index] { page.set (index, page.value (index) > 0.5 ? 0.0 : 1.0); }));
        tabs.back()->powerStyle = true;
        addAndMakeVisible (*tabs.back());
        return tabs.back().get();
    }
    Menu* menu (std::unique_ptr<Menu> m)
    {
        menus.push_back (std::move (m));
        addAndMakeVisible (*menus.back());
        return menus.back().get();
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
    std::array<juce::Rectangle<int>, 4> sections {};
    std::array<Menu*, w::oscCount> warp {};
    std::array<Ctl*, w::oscCount> warpAmt {};
    Tab *osc3On = nullptr, *sampleOn = nullptr, *noiseOn = nullptr, *grainOn = nullptr, *specOn = nullptr;
    Ctl *osc3Pos = nullptr, *sampleGain = nullptr, *noiseLevel = nullptr, *unisonBlend = nullptr, *unisonWidth = nullptr, *grainPos = nullptr,
        *specPos = nullptr;
    Menu *unisonCount = nullptr, *unisonStack = nullptr;
    std::vector<Ctl*> macros, rackAmt;
    std::vector<Tab*> rack;
};

// the card's title-bar well that shows / hides the panel (Live's round 13 px well, lit while shown)
class HelixPlusWell final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<bool()> isOn;
    std::function<void()> onClick;
    HelixPlusWell() { setTooltip (tr ("Helix+: LYNX's extra controls beside the device")); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (13.0f, 13.0f);
        const auto ink = theme::livelook::drawWell (g, r, isOn && isOn());
        g.setColour (ink);
        g.fillRect (r.getCentreX() - 3.0f, r.getCentreY() - 0.5f, 6.0f, 1.0f);
        g.fillRect (r.getCentreX() - 0.5f, r.getCentreY() - 3.0f, 1.0f, 6.0f);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
        repaint();
    }
};

// the Device View's card
class HelixCard final : public DetailCard
{
public:
    HelixCard (AudioEngine& e, int channel) : panel (e, locator (channel), channel, true), plus (e, locator (channel), channel)
    {
        addAndMakeVisible (panel);
        addChildComponent (plus);
        plus.setVisible (showPlus);
        well.isOn = [] { return showPlus; };
        well.onClick = [this] { setPlus (! showPlus); };
    }
    void setPlus (bool shown)
    {
        showPlus = shown;
        plus.setVisible (showPlus);
        well.repaint();
        if (onPreferredWidthChanged)
            onPreferredWidthChanged();
        resized();
    }
    // (Helix+ 2, round 190ml: the panel beside the device, shown by the title bar's well; remembered for every Helix card)
    static inline bool showPlus = false;
    juce::Component* titleAccessory() override { return &well; }
    int titleAccessoryWidth() const override { return 17; }
    // Live's device keeps its proportions: the body's height sets the scale (the Device View's body is a little lower
    // than Live's), the card is as wide as that makes it
    int preferredWidth (int bodyHeight) const override
    {
        return juce::roundToInt ((helixDeviceWidth + (showPlus ? HelixPlusCardPanel::panelWidth : 0)) * (float) bodyHeight / (float) helixDeviceHeight);
    }
    void resized() override
    {
        const auto scale = (float) getHeight() / (float) helixDeviceHeight;
        panel.setBounds (0, 0, helixDeviceWidth, helixDeviceHeight);
        panel.setTransform (juce::AffineTransform::scale (scale > 0.0f ? scale : 1.0f));
        plus.setBounds (helixDeviceWidth, 0, HelixPlusCardPanel::panelWidth, helixDeviceHeight);
        plus.setTransform (juce::AffineTransform::scale (scale > 0.0f ? scale : 1.0f));
    }

private:
    static std::function<ChannelDef* (Song&)> locator (int channel)
    {
        return [channel] (Song& s) -> ChannelDef* { return channel >= 0 && channel < (int) s.channels.size() ? &s.channels[(std::size_t) channel] : nullptr; };
    }
    HelixPanel panel;
    HelixPlusCardPanel plus;
    HelixPlusWell well;
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

int showPlusInCard (juce::Component& root, bool shown)
{
    HelixCard::showPlus = shown;
    int found = 0;
    if (auto* c = dynamic_cast<HelixCard*> (&root))
    {
        c->setPlus (shown);
        ++found;
    }
    for (auto* child : root.getChildren())
        found += showPlusInCard (*child, shown);
    return found;
}

std::unique_ptr<juce::Component> makeTableEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel, int osc, int mode, int frame)
{
    Page page { engine, std::move (locate), channel, false };
    auto editor = std::make_unique<TableEditor> (page, osc);
    editor->setMode (mode == 1 ? TableEditor::harmonicsMode : TableEditor::drawMode);
    editor->setFrame (frame);
    return editor;
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
