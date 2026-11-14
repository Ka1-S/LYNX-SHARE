#pragma once

// Shared machinery of the display-first editors (ui/ProEditors.cpp, REA round 114): parameter access by name, the
// control contract (drag, Ctrl = fine, wheel, double-click / Alt-click = reset, right-click = value / automation menu),
// LYNX-style knobs, switches, option buttons and bottom-bar items, a scrolling level / gain-reduction history fed by
// the slot's analyser tap (or the effect's live meters), a spectrum analyser, level meters and dB scales.
// Only ProEditors.cpp includes it.

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <vector>

#include "core/Effects.h"
#include "core/PluginLayout.h"
#include "core/RackRenderer.h"
#include "engine/AudioEngine.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/PluginHeader.h"
#include "ui/PluginSkin.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx::pro
{
inline float dbOf (float linear) noexcept { return linear > 1e-6f ? 20.0f * std::log10 (linear) : -120.0f; }

// one column of the scrolling history: input and output peak (dBFS) and gain reduction (dB, ≥ 0)
struct Column
{
    float in = -120.0f, out = -120.0f, gr = 0.0f;
};

class ProEditor : public juce::Component,
                  public ui::HintSource,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    ProEditor (AudioEngine& e, int t, int s, EffectKind k, bool demoMode, int width, int height)
        : engine (e), track (t), slot (s), kind (k), demo (demoMode), accent (skin::accentFor (k)), fft (fftOrder)
    {
        anim.onFrame = [this] { repaint(); };
        history.assign (historyLength, Column {});
        fftData.assign ((std::size_t) fftSize * 2, 0.0f);
        window.resize ((std::size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(std::size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        engine.addChangeListener (this);
        setWantsKeyboardFocus (true);
        setSize (width, height + (int) helpH); // round 135: + the description line under the bottom bar
        claimTap();
        startTimerHz (30);
    }

    ~ProEditor() override
    {
        stopTimer();
        engine.removeChangeListener (this);
        if (auto* times = engine.getProcessTimes(); times != nullptr && times->analyzer.key.load() == tapKey())
            times->analyzer.key.store (-1);
        ui::hideValueTip();
    }

    const EffectSlot* current() const
    {
        const auto& mixer = engine.getSong().mixer;
        if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
            return nullptr;
        const auto& fx = mixer[(std::size_t) track].effects[(std::size_t) slot];
        return fx.kind == kind ? &fx : nullptr;
    }

    // the demo history / spectrum (for --plugui snapshots) is filled once the subclass is complete
    void fillDemo()
    {
        if (! demo) return;
        for (int i = 0; i < historyLength; ++i)
            pushColumn (demoColumn (i));
        updateSpectrum();
    }

protected:
    // --- controls ------------------------------------------------------------------------------------
    enum class K { knob, fader, toggle, menu, tabs, barMenu, barValue, button, custom };
    struct Ctl
    {
        K type = K::knob;
        juce::Rectangle<float> r;
        int param = -1;
        juce::String caption;
        int size = 1;               // knobs: 0 small, 1 medium, 2 big
        bool bipolar = false;
        int id = 0;                 // custom / button identifiers
        juce::Colour colour { theme::colour::accent };
        juce::StringArray options;  // menu / tabs / barMenu
        std::vector<float> values;  // the value of each option (empty: i / (n − 1))
        juce::String hint;          // what it does (the hint bar)
        bool lit = false;           // buttons: drawn as on
        bool showValue = true;
        juce::Colour cap {};        // knobs: the cap's own colour (themes that read KnobLook::capTint; transparent = the theme's)
        juce::String endLow, endHigh; // sliders (round tp172b5): words printed at the scale's ends (HARD / SOFT) instead of its values
        int autoParam = -1;         // knobs (round tp172b5): a toggle shown as an AUTO prefix word in the caption, clicked
                                    // there; dim when off, the accent when on (the compressor reference's AUTO THRESHOLD)
        int look = 0;               // knobs: 0 = a knob, 1 = a short curved bar (round tp172b4: the compressor reference's
                                    // KNEE / RANGE bars), 2 = a chunky grey face with a white pointer (round tp172b6: the
                                    // multiband reference's), 3 = the same, small, inside a ring (`outer`), 4 = that ring;
                                    // menus: 1 = a plain pill button showing the choice (no arrow);
                                    // tabs: 1 = a raised pill with a marker at the chosen end, 2 = small hardware keys,
                                    // muted until hovered, the chosen one lit, the caption small above them (the generic
                                    // pages' compactKeys, as Glow Leveler's Old | Measured; rollup 0.1.28)
        float rowTrim = 0.0f;         // sliders: room kept at the scale row's right end (round tp172b6: the stereo link's MID key)
        bool keepCase = false;        // tabs look 1 (round tp172b6): the options as written (the reference's Band / Free, In / Ext)
        bool bare = false;            // buttons (round tp172b6): no face of their own (the editor paints them in its overlay)
        float levelArc = -1.0f;       // look 2: a level on the knob's own scale, drawn along its ring (0 ... 1; below 0: none)
        juce::Rectangle<float> outer; // look 3: the whole knob, ring included (the labels sit under it)
        int ringParam = -1;           // look 3: the ring's parameter (its value shows while the ring is hovered or dragged)
        juce::String ringLow, ringHigh; // look 3: the ring's own ends, printed outside the knob's (M and S)
    };

    static Ctl knob (juce::Rectangle<float> r, int p, const juce::String& caption, int size = 1, bool bipolar = false)
    {
        Ctl c;
        c.type = K::knob;
        c.r = r;
        c.param = p;
        c.caption = caption;
        c.size = size;
        c.bipolar = bipolar;
        return c;
    }
    static Ctl fader (juce::Rectangle<float> r, int p, const juce::String& caption, bool bipolar = false)
    {
        Ctl c = knob (r, p, caption, 0, bipolar);
        c.type = K::fader;
        return c;
    }
    // round tp172b5: a horizontal slider (the reference multiband's RATIO / KNEE / LOOKAHEAD): the bar, its scale's
    // ends under it (the value there while hovered or dragged), the caption under that; dragged left / right
    static Ctl hslider (juce::Rectangle<float> r, int p, const juce::String& caption, bool bipolar = false)
    {
        Ctl c = fader (r, p, caption, bipolar);
        c.look = 1;
        return c;
    }
    static Ctl toggle (juce::Rectangle<float> r, int p, const juce::String& caption)
    {
        Ctl c;
        c.type = K::toggle;
        c.r = r;
        c.param = p;
        c.caption = caption;
        return c;
    }
    static Ctl menu (juce::Rectangle<float> r, int p, const juce::String& caption, const juce::String& options, std::vector<float> values = {})
    {
        Ctl c;
        c.type = K::menu;
        c.r = r;
        c.param = p;
        c.caption = caption;
        c.options = juce::StringArray::fromTokens (options, "|", "");
        c.values = std::move (values);
        return c;
    }
    static Ctl tabs (juce::Rectangle<float> r, int p, const juce::String& options, std::vector<float> values = {})
    {
        Ctl c = menu (r, p, {}, options, std::move (values));
        c.type = K::tabs;
        return c;
    }
    static Ctl barMenu (juce::Rectangle<float> r, int p, const juce::String& caption, const juce::String& options, std::vector<float> values = {})
    {
        Ctl c = menu (r, p, caption, options, std::move (values));
        c.type = K::barMenu;
        return c;
    }
    static Ctl barValue (juce::Rectangle<float> r, int p, const juce::String& caption)
    {
        Ctl c;
        c.type = K::barValue;
        c.r = r;
        c.param = p;
        c.caption = caption;
        return c;
    }
    static Ctl button (juce::Rectangle<float> r, int id, const juce::String& caption, bool lit, const juce::String& hint)
    {
        Ctl c;
        c.type = K::button;
        c.r = r;
        c.id = id;
        c.caption = caption;
        c.lit = lit;
        c.hint = hint;
        return c;
    }
    static Ctl custom (juce::Rectangle<float> r, int id, int p = -1, const juce::String& hint = {})
    {
        Ctl c;
        c.type = K::custom;
        c.r = r;
        c.id = id;
        c.param = p;
        c.hint = hint;
        return c;
    }

    // --- what a subclass provides --------------------------------------------------------------------
    virtual void layoutControls (std::vector<Ctl>& out) const = 0;
    virtual void paintBody (juce::Graphics& g, const EffectSlot& fx) = 0;      // under the controls
    virtual void paintOverlay (juce::Graphics&, const EffectSlot&) {}          // over the controls (tips)
    virtual juce::String title() const = 0;
    virtual juce::String gestures() const { return {}; }                       // right of the header
    virtual bool bodyDown (const juce::MouseEvent&) { return false; }
    virtual void bodyDrag (const juce::MouseEvent&) {}
    virtual void bodyUp (const juce::MouseEvent&) {}
    virtual bool bodyWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&) { return false; }
    virtual bool bodyDoubleClick (const juce::MouseEvent&) { return false; }
    virtual void bodyMove (juce::Point<float>) {}
    virtual ui::Hint bodyHint (juce::Point<float>) { return {}; }
    virtual void buttonPressed (const Ctl&, const juce::MouseEvent&) {}
    virtual void paintCustom (juce::Graphics&, const Ctl&) {}
    virtual void customDown (const Ctl&, const juce::MouseEvent&) {}
    virtual void customDrag (const Ctl&, const juce::MouseEvent&) {}
    virtual bool usesHistory() const { return false; }
    virtual bool usesSpectrum() const { return false; }
    virtual float grDb() const { return 0.0f; }                                // the effect's current gain reduction
    virtual Column demoColumn (int) const { return {}; }
    virtual double demoSpectrumDb (double hz) const
    {
        return -42.0 - 4.5 * std::log2 (juce::jmax (20.0, hz) / 1000.0) * 0.5 + 9.0 * std::exp (-std::pow (std::log2 (hz / 60.0), 2.0))
               + 4.0 * std::sin (hz * 0.0021) - (hz > 14000.0 ? (hz - 14000.0) * 0.004 : 0.0) + tiltDb (hz);
    }
    virtual void tick() {}

    // the slot's on / off switch (the power button some references carry on the panel)
    void toggleEnabled()
    {
        if (current() == nullptr) return;
        engine.editSong ([tk = track, sl = slot] (Song& song) {
            auto& fx = song.mixer[(std::size_t) tk].effects[(std::size_t) sl];
            fx.enabled = ! fx.enabled;
        });
    }

    // --- parameters ----------------------------------------------------------------------------------
    int P (const char* name) const { return plugui::paramIndex (kind, name); }
    float v (int p) const
    {
        const auto* fx = current();
        if (fx == nullptr || p < 0 || p >= maxEffectParams)
            return 0.0f;
        // an automated knob shows its automation's value while the song plays (issue #12), unless the mouse holds it
        if (! isMouseButtonDown (true))
            if (const auto a = engine.getAutomationDisplay().valueOf ({ AutomationTargetKind::effectParam, track, slot, p }))
                return *a;
        return fx->params[(std::size_t) p];
    }
    // a subclass may add linked changes (Aether's linked knobs) before they are applied
    virtual void linkChanges (std::vector<std::pair<int, float>>&) const {}
    void set (std::vector<std::pair<int, float>> changes, bool undoable = true)
    {
        if (current() == nullptr) return;
        if (undoable) bar.willEdit();
        linkChanges (changes);
        engine.editSong ([tk = track, sl = slot, changes] (Song& song) {
            auto& params = song.mixer[(std::size_t) tk].effects[(std::size_t) sl].params;
            for (const auto& [p, x] : changes)
                if (p >= 0 && p < maxEffectParams) params[(std::size_t) p] = juce::jlimit (0.0f, 1.0f, x);
        }, undoable);
    }
    juce::String text (int p) const { return text (p, v (p)); }
    juce::String text (int p, float x) const
    {
        if (p < 0) return {};
        const auto* fx = current();
        return utf8 ((fx != nullptr ? formatEffectParam (kind, p, x, engine.getSong().bpm, fx->params) : formatEffectParam (kind, p, x, engine.getSong().bpm)).c_str());
    }

    juce::String trackName() const
    {
        const auto& mixer = engine.getSong().mixer;
        if (track == 0) return tr ("Master");
        const auto name = track < (int) mixer.size() ? juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str()) : juce::String();
        return name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) });
    }

    ui::ValueSpec specFor (int p, int steps = 0) const
    {
        ui::ValueSpec spec;
        const auto& in = effectInfo (kind);
        if (p < 0 || p >= (int) in.params.size()) return spec;
        spec.name = trackName() + " - " + utf8 (in.name) + " - " + utf8 (in.params[(std::size_t) p].name);
        spec.defaultValue = in.params[(std::size_t) p].defaultValue;
        spec.pixelsForRange = 200.0;
        if (steps > 0) spec.interval = 1.0 / steps;
        spec.toText = [this, p] (double x) { return text (p, (float) x); };
        spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
        spec.target = AutomationTarget { AutomationTargetKind::effectParam, track, slot, p };
        return spec;
    }

    // the 0..1 value whose real value is `target` (monotonic mapping `f`, rising or falling)
    static float invert (const std::function<double (float)>& f, double target)
    {
        float lo = 0.0f, hi = 1.0f;
        const auto rising = f (1.0f) >= f (0.0f);
        for (int i = 0; i < 40; ++i)
        {
            const auto mid = 0.5f * (lo + hi);
            if ((f (mid) < target) == rising) lo = mid;
            else hi = mid;
        }
        return 0.5f * (lo + hi);
    }

    float liveMeter (int k) const
    {
        const auto* times = engine.getProcessTimes();
        const auto key = (std::size_t) (track * maxEffectSlots + slot) * ProcessTimes::meterOutputs + (std::size_t) juce::jlimit (0, ProcessTimes::meterOutputs - 1, k);
        return times != nullptr && key < times->effectMeters.size() ? times->effectMeters[key].load (std::memory_order_relaxed) : 0.0f;
    }

    // --- geometry ------------------------------------------------------------------------------------
    static constexpr float headerH = 30.0f, barH = 28.0f, helpH = 18.0f;
    juce::Rectangle<float> headerArea() const { return { 0.0f, 0.0f, (float) getWidth(), headerH }; }
    juce::Rectangle<float> barArea() const { return { 0.0f, (float) getHeight() - barH - helpH, (float) getWidth(), barH }; }
    juce::Rectangle<float> bodyArea() const { return { 0.0f, headerH, (float) getWidth(), (float) getHeight() - headerH - barH - helpH }; }
    // round 135: the description line at the very bottom (the reference reverbs' strip): what the control under the
    // mouse does, or the current mode when nothing is under it
    juce::Rectangle<float> helpArea() const { return { 0.0f, (float) getHeight() - helpH, (float) getWidth(), helpH }; }

    std::vector<Ctl> controls() const
    {
        std::vector<Ctl> out;
        layoutControls (out);
        return out;
    }
    bool controlAt (juce::Point<float> p, Ctl& found) const
    {
        const auto all = controls();
        for (auto it = all.rbegin(); it != all.rend(); ++it) // the last drawn is on top
            if (it->r.contains (p))
            {
                found = *it;
                return true;
            }
        return false;
    }

    int optionIndex (const Ctl& c) const
    {
        const auto n = c.options.size();
        if (n <= 0) return 0;
        const auto x = v (c.param);
        auto best = 0;
        auto bestD = 10.0f;
        for (int i = 0; i < n; ++i)
            if (const auto d = std::abs (optionValue (c, i) - x); d < bestD)
            {
                best = i;
                bestD = d;
            }
        return best;
    }
    static float optionValue (const Ctl& c, int i)
    {
        if (i < (int) c.values.size()) return c.values[(std::size_t) i];
        return c.options.size() > 1 ? (float) i / (float) (c.options.size() - 1) : 0.0f;
    }

    void showOptions (const Ctl& c)
    {
        juce::PopupMenu m;
        const auto now = optionIndex (c);
        for (int i = 0; i < c.options.size(); ++i)
            m.addItem (tr (c.options[i]), true, i == now, [this, p = c.param, x = optionValue (c, i)] { set ({ { p, x } }); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (c.r.toNearestInt())));
    }

    // --- mouse ---------------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragCtl = -1;
        if (const auto part = bar.partAt (e.position); part != pluginheader::Part::none)
        {
            bar.click (part, *this);
            return;
        }
        if (e.mods.isPopupMenu() && headerArea().contains (e.position)) // round 126: the editor's style
        {
            juce::PopupMenu m;
            skin::addStyleMenu (m, skin::styleKey (kind), [safe = juce::Component::SafePointer<juce::Component> (this)] {
                if (safe != nullptr) safe->repaint();
            });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        bar.beginGesture();
        Ctl c;
        if (controlAt (e.position, c))
        {
            pressed = c;
            if (c.type == K::knob && c.autoParam >= 0 && autoPrefixAt (c).contains (e.position) && ! e.mods.isPopupMenu())
            {
                set ({ { c.autoParam, v (c.autoParam) >= 0.5f ? 0.0f : 1.0f } }); // the AUTO word: its toggle
                return;
            }
            if (c.type == K::custom)
            {
                customDown (c, e);
                dragCtl = -2;
                return;
            }
            if (c.type == K::button)
            {
                buttonPressed (c, e);
                return;
            }
            if (e.mods.isPopupMenu())
            {
                if (c.type == K::knob || c.type == K::fader || c.type == K::barValue)
                    ui::showValueMenu (*this, &engine, specFor (c.param), v (c.param), [this, p = c.param] (double x) { set ({ { p, (float) x } }); });
                else if (c.type == K::menu || c.type == K::barMenu)
                    showOptions (c);
                return;
            }
            switch (c.type)
            {
                case K::toggle:
                    if (c.id != 0) // (round 190ar: a toggle the editor answers itself: its state is `lit`)
                        buttonPressed (c, e);
                    else
                        set ({ { c.param, v (c.param) >= 0.5f ? 0.0f : 1.0f } });
                    return;
                case K::menu: case K::barMenu: showOptions (c); return;
                case K::tabs:
                {
                    const auto n = juce::jmax (1, c.options.size());
                    const auto i = juce::jlimit (0, n - 1, (int) ((e.position.x - c.r.getX()) / c.r.getWidth() * (float) n));
                    set ({ { c.param, optionValue (c, i) } });
                    return;
                }
                case K::knob: case K::fader: case K::barValue:
                    if (ui::isResetClick (e))
                    {
                        set ({ { c.param, (float) specFor (c.param).defaultValue } });
                        return;
                    }
                    dragCtl = c.param;
                    hDragStart = v (c.param);
                    hDragAnchorX = e.position.x;
                    hDragFine = e.mods.isCtrlDown();
                    valueDrag.begin (e, v (c.param));
                    return;
                default: return;
            }
        }
        if (bodyDown (e)) dragCtl = -3;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragCtl >= 0)
        {
            const auto spec = specFor (dragCtl);
            auto x = valueDrag.update (e, spec);
            if (pressed.type == K::fader && pressed.look == 1) // a horizontal slider follows the pointer left / right
            {
                const auto fine = e.mods.isCtrlDown();
                if (fine != hDragFine) // (Ctrl pressed or released mid-drag: carries on from here, no jump)
                {
                    hDragStart = v (dragCtl);
                    hDragAnchorX = e.position.x;
                    hDragFine = fine;
                }
                x = juce::jlimit (0.0, 1.0, (double) (hDragStart + (fine ? 0.2f : 1.0f) * (e.position.x - hDragAnchorX) / juce::jmax (1.0f, pressed.r.getWidth() - 8.0f)));
            }
            set ({ { dragCtl, (float) x } });
            ui::showValueTip (*this, pressed.r.toNearestInt(), spec.text (x));
            return;
        }
        if (dragCtl == -2) { customDrag (pressed, e); return; }
        if (dragCtl == -3) bodyDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragCtl == -3) bodyUp (e);
        dragCtl = -1;
        valueDrag.end();
        ui::hideValueTip();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        Ctl c;
        if (controlAt (e.position, c)) return; // the controls reset on their own double-click (ValueDrag)
        bodyDoubleClick (e);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        Ctl c;
        if (controlAt (e.position, c) && c.param >= 0 && (c.type == K::knob || c.type == K::fader || c.type == K::barValue || c.type == K::custom))
        {
            const auto spec = specFor (c.param);
            if (const auto x = ui::wheelValue (v (c.param), e, wheel, spec))
            {
                set ({ { c.param, (float) *x } });
                ui::flashValueTip (*this, c.r.toNearestInt(), spec.text (*x));
            }
            return;
        }
        bodyWheel (e, wheel);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        mouse = e.position;
        bar.hover (e.position);
        bodyMove (e.position);
        anim.setHot (controlIndexAt (e.position));
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        mouse = { -1.0f, -1.0f };
        bar.hoverExit();
        bodyMove (mouse);
        anim.setHot (-1);
        repaint();
    }
    int controlIndexAt (juce::Point<float> p) const
    {
        const auto all = controls();
        for (int i = (int) all.size(); --i >= 0;)
            if (all[(std::size_t) i].r.contains (p)) return i;
        return -1;
    }

    ui::Hint hintAt (juce::Point<int> position) override
    {
        const auto p = position.toFloat();
        if (const auto part = bar.partAt (p); part != pluginheader::Part::none)
            return { bar.hintFor (part), bar.presetLabel(), false, true };
        Ctl c;
        if (controlAt (p, c))
        {
            if (c.param >= 0 && (c.type == K::knob || c.type == K::fader || c.type == K::barValue || (c.type == K::custom && c.hint.isEmpty())))
                return ui::valueHint (specFor (c.param), v (c.param));
            ui::Hint h;
            h.text = c.hint.isNotEmpty() ? c.hint : c.caption;
            if (c.param >= 0) h.value = text (c.param);
            h.hasMenu = c.type == K::menu || c.type == K::barMenu;
            return h;
        }
        return bodyHint (p);
    }

    // --- painting ------------------------------------------------------------------------------------
public:
    skin::Style editorStyle() const { return skin::styleFor (kind); } // round 126: one style per plugin

protected:

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const skin::StyleScope styleScope (editorStyle(), skin::artForEffect (kind));
        // round 170k: Themed draws in the effect's own art, and the editor's own drawing follows the art's accent
        accent = editorStyle() == skin::Style::themed ? skin::artAccent (skin::artForEffect (kind)) : skin::accentFor (kind);
        skin::canvas (g, getLocalBounds().toFloat(), accent);
        const auto* fx = current();
        if (fx == nullptr)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::body));
            g.drawText (tr ("The effect was removed from this slot."), getLocalBounds(), juce::Justification::centred);
            return;
        }
        paintBody (g, *fx);
        paintHeader (g, *fx);
        skin::bottomBar (g, barArea());
        const auto all = controls();
        for (int i = 0; i < (int) all.size(); ++i)
            paintControl (g, all[(std::size_t) i], i);
        paintOverlay (g, *fx);
        paintHelpLine (g, *fx);
    }

    juce::String helpText (const EffectSlot& fx)
    {
        if (mouse.x < 0.0f)
            return pluginheader::modeLine (kind, fx.params, engine.getSong().bpm);
        if (const auto part = bar.partAt (mouse); part != pluginheader::Part::none)
            return bar.hintFor (part);
        Ctl c;
        if (controlAt (mouse, c))
        {
            const auto& info = effectInfo (kind);
            if (c.param >= 0 && c.param < (int) info.params.size())
            {
                const auto what = pluginheader::describe (kind, info.params[(std::size_t) c.param].name);
                const auto name = c.caption.isNotEmpty() ? c.caption : tr (info.params[(std::size_t) c.param].name);
                if (what.isNotEmpty())
                    return name + juce::String::fromUTF8 (" \xe2\x80\x94 ") + what;
            }
            return c.hint.isNotEmpty() ? c.hint : c.caption;
        }
        if (const auto h = bodyHint (mouse); h.text.isNotEmpty())
            return h.text;
        return pluginheader::modeLine (kind, fx.params, engine.getSong().bpm);
    }

    void paintHelpLine (juce::Graphics& g, const EffectSlot& fx)
    {
        using namespace theme;
        const auto area = helpArea();
        g.setColour (colour::bg0.withAlpha (0.6f));
        g.fillRect (area);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawFittedText (helpText (fx), area.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    }

    void paintHeader (juce::Graphics& g, const EffectSlot& fx)
    {
        using namespace theme;
        const auto h = headerArea();
        skin::headerBar (g, h, accent);
        skin::brandMark (g, juce::Rectangle<float> (8.0f, 0.0f, 20.0f, h.getHeight()));
        skin::powerLed (g, { 36.0f, h.getCentreY() }, 7.0f, fx.enabled);
        const auto name = title();
        const auto w = skin::titleText (g, juce::Rectangle<float> (46.0f, 0.0f, 300.0f, h.getHeight()), name, accent);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        if (! plugui::hostedElsewhere) // (no mixer around a plugin in another host, round 188b)
            g.drawText (trackName() + "  -  " + tr ("slot {0}", { juce::String (slot + 1) }), juce::Rectangle<float> (58.0f + w, 0.0f, 220.0f, h.getHeight()),
                        juce::Justification::centredLeft, true);
        const auto left = bar.place (h.withTrimmedLeft (w + 270.0f));
        bar.paint (g, accent);
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption));
        g.drawText (gestures(), left.withTrimmedRight (8.0f), juce::Justification::centredRight, true);
    }

    // the colour a control is drawn in: the plugin's accent unless the editor gave it its own
    juce::Colour tint (const Ctl& c) const { return c.colour == theme::colour::accent ? accent : c.colour; }

    void paintControl (juce::Graphics& g, const Ctl& c, int index = -1)
    {
        using namespace theme;
        const auto hover = c.r.contains (mouse);
        const auto hot = index >= 0 ? anim.level (index) : (hover ? 1.0f : 0.0f);
        const auto ac = tint (c);
        const auto* fx = current();
        const auto on = fx == nullptr || fx->enabled;
        switch (c.type)
        {
            case K::knob:
            {
                const auto d = c.size == 2 ? juce::jmin (c.r.getWidth() - 4.0f, c.r.getHeight() - 36.0f)
                                           : juce::jmin (c.r.getWidth() - 10.0f, c.r.getHeight() - 34.0f);
                const auto active = dragCtl == c.param && c.param >= 0;
                if (c.look == 1)
                {
                    paintArcBar (g, c, hover, active, hot, ac, on);
                    break;
                }
                if (c.look >= 2)
                {
                    paintFaceKnob (g, c, hover, active, hot, ac, on, d);
                    break;
                }
                const auto knobR = juce::Rectangle<float> (c.r.getCentreX() - d / 2.0f, c.r.getY() + 2.0f, d, d);
                skin::KnobLook look;
                look.value = v (c.param);
                look.bipolar = c.bipolar;
                look.accent = ac;
                look.hover = hot;
                look.active = active;
                look.enabled = on;
                look.capTint = c.cap;
                skin::knob (g, knobR, look);
                if (knobRanges)
                {
                    knobLabels (g, c, knobR, hover, active, hot, ac);
                    break;
                }
                if (hoverValues) // round tp172b5: the caption only; the value under it while hovered or dragged
                {
                    const auto capR = juce::Rectangle<float> (c.r.getX() - 14.0f, knobR.getBottom() + 3.0f, c.r.getWidth() + 28.0f, 14.0f);
                    const auto captionColour = active ? ac.brighter (0.3f) : skin::captionColour().interpolatedWith (colour::textPrimary, hot * 0.5f);
                    g.setColour (captionColour);
                    g.setFont (font (c.size == 2 ? type::small : type::caption, true));
                    if (c.autoParam >= 0) // "AUTO" (dim / lit) then the caption, centred together
                    {
                        const auto prefix = autoPrefixRect (c, capR);
                        const auto f = g.getCurrentFont();
                        const auto lit = v (c.autoParam) >= 0.5f;
                        const auto prefixHot = prefix.contains (mouse);
                        g.setColour (lit ? ac.brighter (prefixHot ? 0.3f : 0.0f)
                                         : colour::textDisabled.interpolatedWith (colour::textSecondary, prefixHot ? 0.7f : 0.0f));
                        g.drawText ("AUTO", prefix, juce::Justification::centredLeft, false);
                        g.setColour (captionColour);
                        g.drawText (caps (c.caption), capR.withLeft (prefix.getRight() + juce::GlyphArrangement::getStringWidth (f, " ")),
                                    juce::Justification::centredLeft, false);
                    }
                    else
                        g.drawText (caps (c.caption), capR, juce::Justification::centred, false);
                    if (c.showValue && (hover || active))
                    {
                        g.setColour (active ? ac.brighter (0.3f) : skin::valueColour (true));
                        g.setFont (font (c.size == 2 ? type::body : type::small, c.size == 2));
                        g.drawText (text (c.param), juce::Rectangle<float> (c.r.getX() - 24.0f, knobR.getBottom() + 17.0f, c.r.getWidth() + 48.0f, 14.0f),
                                    juce::Justification::centred, false);
                    }
                    break;
                }
                g.setColour (active ? ac.brighter (0.3f) : skin::captionColour().interpolatedWith (colour::textPrimary, hot * 0.5f));
                g.setFont (font (c.size == 2 ? type::small : type::caption, true));
                g.drawText (caps (c.caption), juce::Rectangle<float> (c.r.getX() - 14.0f, knobR.getBottom() + 3.0f, c.r.getWidth() + 28.0f, 14.0f),
                            juce::Justification::centred, false);
                if (c.showValue)
                {
                    g.setColour (skin::valueColour (active || hover));
                    g.setFont (font (c.size == 2 ? type::body : type::small, c.size == 2));
                    g.drawText (text (c.param), juce::Rectangle<float> (c.r.getX() - 14.0f, knobR.getBottom() + 17.0f, c.r.getWidth() + 28.0f, 14.0f),
                                juce::Justification::centred, false);
                }
                break;
            }
            case K::fader:
            {
                if (c.look == 1)
                {
                    paintHSlider (g, c, hover, dragCtl == c.param && c.param >= 0, hot, ac, on);
                    break;
                }
                const auto track = juce::Rectangle<float> (c.r.getCentreX() - 2.0f, c.r.getY() + 6.0f, 4.0f, c.r.getHeight() - 42.0f);
                const auto active = dragCtl == c.param && c.param >= 0;
                skin::slider (g, track, v (c.param), true, c.bipolar, ac, active || hot > 0.5f, on, 26.0f, 12.0f);
                g.setColour (active ? ac.brighter (0.3f) : skin::captionColour());
                g.setFont (font (type::caption, true));
                g.drawText (caps (c.caption), juce::Rectangle<float> (c.r.getX() - 12.0f, track.getBottom() + 8.0f, c.r.getWidth() + 24.0f, 13.0f), juce::Justification::centred, false);
                g.setColour (skin::valueColour (active || hover));
                g.setFont (font (type::caption));
                g.drawText (text (c.param), juce::Rectangle<float> (c.r.getX() - 12.0f, track.getBottom() + 21.0f, c.r.getWidth() + 24.0f, 13.0f), juce::Justification::centred, false);
                break;
            }
            case K::toggle:
            {
                const auto lit = c.id != 0 ? c.lit : v (c.param) >= 0.5f;
                skin::button (g, c.r, false, hover, ac);
                skin::powerLed (g, { c.r.getX() + 11.0f, c.r.getCentreY() }, 6.0f, false);
                if (lit)
                {
                    const auto led = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ c.r.getX() + 11.0f, c.r.getCentreY() });
                    g.setColour (ac.withAlpha (0.35f));
                    g.fillEllipse (led.expanded (3.5f));
                    g.setColour (ac);
                    g.fillEllipse (led);
                    g.setColour (ac.withAlpha (0.7f));
                    g.drawRoundedRectangle (c.r.reduced (0.5f), radius::md, 1.0f);
                }
                g.setColour (lit ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (type::small, lit));
                g.drawText (c.caption, c.r.withTrimmedLeft (20.0f).withTrimmedRight (4.0f), juce::Justification::centred, true);
                break;
            }
            case K::button:
            {
                if (c.bare)
                    break;
                skin::button (g, c.r, c.lit, hover, ac);
                g.setColour (c.lit || hover ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (type::small, c.lit));
                g.drawText (c.caption, c.r.reduced (4.0f, 0.0f), juce::Justification::centred, true);
                break;
            }
            case K::menu:
            {
                if (c.look == 1) // round tp172b4: a plain pill showing the choice (the references' style button)
                {
                    skin::button (g, c.r, false, hover, ac);
                    g.setColour (colour::textPrimary);
                    g.setFont (font (c.r.getHeight() < 20.0f ? type::small : type::body, false));
                    g.drawText (tr (c.options[optionIndex (c)]), c.r.reduced (6.0f, 0.0f), juce::Justification::centred, true);
                    if (c.caption.isNotEmpty())
                    {
                        g.setColour (skin::captionColour());
                        g.setFont (font (type::caption, true));
                        g.drawText (caps (c.caption), c.r.translated (0.0f, c.r.getHeight() + 1.0f).withHeight (13.0f).expanded (12.0f, 0.0f), juce::Justification::centred, false);
                    }
                    break;
                }
                skin::selector (g, c.r, hover);
                g.setColour (colour::textPrimary);
                g.setFont (font (type::body, true));
                const auto i = optionIndex (c);
                g.drawText (tr (c.options[i]), c.r.reduced (12.0f, 0.0f).withTrimmedRight (6.0f), juce::Justification::centred, true);
                juce::Path tri;
                const auto ax = c.r.getRight() - 11.0f, ay = c.r.getCentreY();
                tri.addTriangle (ax - 3.5f, ay - 1.5f, ax + 3.5f, ay - 1.5f, ax, ay + 2.5f);
                g.setColour (hover ? ac : colour::textSecondary);
                g.fillPath (tri);
                if (c.caption.isNotEmpty())
                {
                    g.setColour (skin::captionColour());
                    g.setFont (font (type::caption, true));
                    g.drawText (caps (c.caption), c.r.translated (0.0f, c.r.getHeight() + 1.0f).withHeight (13.0f), juce::Justification::centred, false);
                }
                break;
            }
            case K::tabs:
            {
                const auto n = juce::jmax (1, c.options.size());
                const auto sel = optionIndex (c);
                if (c.look == 1) // round tp172b6: a raised grey pill, a marker at the chosen end (the multiband reference's)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.55f));
                    g.fillRoundedRectangle (c.r, 4.0f);
                    const auto pill = c.r.reduced (1.0f);
                    g.setGradientFill (juce::ColourGradient (juce::Colour (on ? 0xff5d5d5du : 0xff444444u), 0.0f, pill.getY(), juce::Colour (on ? 0xff444343u : 0xff343434u), 0.0f, pill.getBottom(), false));
                    g.fillRoundedRectangle (pill, 3.0f);
                    const auto marker = 4.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        auto cell = juce::Rectangle<float> (pill.getX() + pill.getWidth() * (float) i / (float) n, pill.getY(), pill.getWidth() / (float) n, pill.getHeight());
                        if (i > 0)
                        {
                            g.setColour (juce::Colours::black.withAlpha (0.45f));
                            g.drawVerticalLine ((int) cell.getX(), cell.getY(), cell.getBottom());
                        }
                        if (cell.contains (mouse))
                        {
                            g.setColour (juce::Colours::white.withAlpha (0.05f));
                            g.fillRect (cell);
                        }
                        g.setColour (juce::Colour (on ? 0xffd2d2d2u : 0xff8a8a8au)); // (the reference prints both options alike: the marker tells)
                        g.setFont (font (type::small));
                        g.drawText (c.keepCase ? tr (c.options[i]) : caps (tr (c.options[i])), cell, juce::Justification::centred, true);
                    }
                    const auto mx = sel <= 0 ? pill.getX() : pill.getRight() - marker;
                    g.setColour (on ? juce::Colour (0xffdca103) : colour::textDisabled);
                    g.fillRect (juce::Rectangle<float> (mx, pill.getY(), marker, pill.getHeight()));
                    break;
                }
                if (c.look == 2) // (the generic pages' compactKeys: PluginEditors.cpp, Item::tabs)
                {
                    if (c.caption.isNotEmpty())
                    {
                        g.setColour (skin::captionColour().withAlpha (0.8f));
                        g.setFont (font (type::small));
                        g.drawText (caps (c.caption), c.r.withY (c.r.getY() - 12.0f).withHeight (11.0f), juce::Justification::centredLeft, false);
                    }
                    const auto w = c.r.getWidth() / (float) n;
                    for (int i = 0; i < n; ++i)
                    {
                        const auto cell = juce::Rectangle<float> (c.r.getX() + w * (float) i, c.r.getY(), w, c.r.getHeight()).reduced (2.0f, 0.0f);
                        const auto hot = cell.contains (mouse);
                        skin::hwKey (g, cell, i == sel && on, hot);
                        if (i != sel && ! hot)
                        {
                            g.setColour (juce::Colour (0xffc8cbce).withAlpha (0.55f));
                            g.fillRoundedRectangle (cell, 2.0f);
                        }
                        g.setColour (i == sel && on ? juce::Colours::white : juce::Colours::white.withAlpha (hot ? 0.95f : 0.75f));
                        g.setFont (font (juce::jmax (7.0f, juce::jmin (c.r.getHeight() * 0.58f, 8.5f)), true));
                        g.drawFittedText (caps (tr (c.options[i])), cell.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                    }
                    break;
                }
                skin::valuePlate (g, c.r, radius::md);
                for (int i = 0; i < n; ++i)
                {
                    const auto cell = juce::Rectangle<float> (c.r.getX() + c.r.getWidth() * (float) i / (float) n, c.r.getY(), c.r.getWidth() / (float) n, c.r.getHeight()).reduced (1.5f);
                    const auto sel1 = i == sel;
                    if (sel1)
                        skin::button (g, cell, true, cell.contains (mouse), ac, radius::sm);
                    else if (cell.contains (mouse))
                    {
                        g.setColour (colour::bg4);
                        g.fillRoundedRectangle (cell, radius::sm);
                    }
                    g.setColour (sel1 ? colour::textPrimary : colour::textSecondary);
                    g.setFont (font (type::small, sel1));
                    g.drawText (caps (tr (c.options[i])), cell, juce::Justification::centred, true);
                }
                break;
            }
            case K::barMenu: case K::barValue:
            {
                if (hover)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.06f));
                    g.fillRoundedRectangle (c.r, radius::md);
                }
                g.setFont (font (type::small));
                g.setColour (colour::textDisabled);
                const auto cap = c.caption + ":";
                const auto cw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), cap) + 6.0f;
                g.drawText (cap, c.r.withWidth (cw).translated (6.0f, 0.0f), juce::Justification::centredLeft);
                g.setColour (dragCtl == c.param && c.param >= 0 ? ac : colour::textPrimary);
                g.setFont (font (type::small, true));
                const auto value = c.type == K::barMenu ? tr (c.options[optionIndex (c)]) + juce::String::fromUTF8 (" \xe2\x96\xbe") : text (c.param);
                g.drawText (value, c.r.withTrimmedLeft (cw + 6.0f), juce::Justification::centredLeft, true);
                break;
            }
            case K::custom: paintCustom (g, c); break;
        }
    }

    // round tp172b4: the references print each knob's scale under it — the parameter's two ends, left and right — and
    // the caption under that; the value replaces the scale while the knob is hovered or dragged
    bool knobRanges = false;
    // round tp172b5: the references that print captions only (the compressor; the reverb uses the layout flag): no scale,
    // the value under the caption only while the knob is hovered or dragged
    bool hoverValues = false;
    bool referenceLabels = false; // round tp172b6: the multiband reference's panel text: neutral greys, a larger, regular caption
    // the AUTO prefix of a knob's caption (Ctl::autoParam): where it is drawn and clicked, within the caption's row
    juce::Rectangle<float> autoPrefixRect (const Ctl& c, juce::Rectangle<float> captionRow) const
    {
        using namespace theme;
        const auto f = font (c.size == 2 ? type::small : type::caption, true);
        const auto prefixW = juce::GlyphArrangement::getStringWidth (f, "AUTO");
        const auto total = juce::GlyphArrangement::getStringWidth (f, "AUTO " + caps (c.caption));
        return { captionRow.getCentreX() - total * 0.5f, captionRow.getY(), prefixW, captionRow.getHeight() };
    }
    juce::Rectangle<float> autoPrefixAt (const Ctl& c) const // (the same row paintControl uses for a hoverValues knob)
    {
        const auto d = c.size == 2 ? juce::jmin (c.r.getWidth() - 4.0f, c.r.getHeight() - 36.0f) : juce::jmin (c.r.getWidth() - 10.0f, c.r.getHeight() - 34.0f);
        return autoPrefixRect (c, { c.r.getX() - 14.0f, c.r.getY() + 2.0f + d + 3.0f, c.r.getWidth() + 28.0f, 14.0f }).expanded (3.0f, 2.0f);
    }
    // round tp172b5: the reference limiter's panel prints each knob's name ABOVE it (its scale still under it)
    bool captionAbove = false;
    void rangeRow (juce::Graphics& g, int param, juce::Rectangle<float> row, bool showValue, bool big, juce::Colour ac, bool active,
                   const juce::String& endLow = {}, const juce::String& endHigh = {})
    {
        using namespace theme;
        if (showValue)
        {
            g.setColour (active ? ac.brighter (0.3f) : skin::valueColour (true));
            g.setFont (font (big ? type::small : type::caption, true));
            g.drawText (text (param), row.expanded (24.0f, 0.0f), juce::Justification::centred, false);
            return;
        }
        const auto shortText = [] (juce::String t) {
            // round tp172b4: scale ends as the references print them — "1:1", "-60 dB", "5 s" (no zero decimals, no
            // spaces round a ratio's colon)
            t = t.replace (" : ", ":");
            juce::String out;
            for (int i = 0; i < t.length(); ++i)
            {
                if (t[i] == '.' && i > 0 && juce::CharacterFunctions::isDigit (t[i - 1]))
                {
                    int j = i + 1;
                    while (j < t.length() && t[j] == '0') ++j;
                    if (j > i + 1 && (j >= t.length() || ! juce::CharacterFunctions::isDigit (t[j]))) { i = j - 1; continue; }
                }
                out << juce::String::charToString (t[i]);
            }
            if (out.startsWithChar ('-') && out.length() > 1 && out[1] == '0' && ! juce::CharacterFunctions::isDigit (out[2]) && out[2] != '.')
                out = out.substring (1); // "-0 dB": a scale's end reads "0 dB"
            return out;
        };
        g.setColour (referenceLabels ? juce::Colour (0xff6e6e6e) : colour::textDisabled);
        g.setFont (font (type::caption));
        const auto half = row.getWidth() * 0.5f + 6.0f;
        g.drawText (endLow.isNotEmpty() ? endLow : shortText (text (param, 0.0f)), row.withWidth (half).translated (-6.0f, 0.0f), juce::Justification::centredLeft, false);
        g.drawText (endHigh.isNotEmpty() ? endHigh : shortText (text (param, 1.0f)), row.withTrimmedLeft (row.getWidth() - half).translated (6.0f, 0.0f), juce::Justification::centredRight, false);
    }
    void captionStyle (juce::Graphics& g, bool active, float hot, juce::Colour ac, float size) const
    {
        using namespace theme;
        if (referenceLabels)
        {
            g.setColour (active ? ac.brighter (0.3f) : juce::Colour (0xff9c9c9c).interpolatedWith (colour::textPrimary, hot * 0.5f));
            g.setFont (font (12.0f));
            return;
        }
        g.setColour (active ? ac.brighter (0.3f) : skin::captionColour().interpolatedWith (colour::textPrimary, hot * 0.5f));
        g.setFont (font (size, true));
    }
    void knobLabels (juce::Graphics& g, const Ctl& c, juce::Rectangle<float> knobR, bool hover, bool active, float hot, juce::Colour ac)
    {
        using namespace theme;
        // (rangeRow pushes the ends 6 px past the row: the reference keeps them within the knob's ring)
        rangeRow (g, c.param, knobR.withY (knobR.getBottom() + 1.0f).withHeight (12.0f).reduced (referenceLabels ? 6.0f : 0.0f, 0.0f), c.showValue && (hover || active), c.size == 2, ac, active);
        captionStyle (g, active, hot, ac, c.size == 2 ? type::small : type::caption);
        const auto captionY = captionAbove ? c.r.getY() - 14.0f : knobR.getBottom() + 14.0f;
        g.drawText (caps (c.caption), juce::Rectangle<float> (c.r.getX() - 14.0f, captionY, c.r.getWidth() + 28.0f, 14.0f),
                    juce::Justification::centred, false);
    }
    void paintHSlider (juce::Graphics& g, const Ctl& c, bool hover, bool active, float hot, juce::Colour ac, bool on)
    {
        using namespace theme;
        const auto bar = juce::Rectangle<float> (c.r.getX() + 4.0f, c.r.getY() + 2.0f, c.r.getWidth() - 8.0f, 16.0f);
        const auto value = juce::jlimit (0.0f, 1.0f, v (c.param));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar.expanded (1.0f), 3.0f);
        if (referenceLabels) // round tp172b6: the multiband reference's slider: a lighter part up to the value on a grey bar, no handle
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff363636).brighter (0.1f * hot), 0.0f, bar.getY(), juce::Colour (0xff484848).brighter (0.1f * hot), 0.0f, bar.getBottom(), false));
        else
            g.setColour (juce::Colours::white.withAlpha (0.06f + 0.04f * hot));
        g.fillRoundedRectangle (bar, 2.5f);
        const auto from = c.bipolar ? 0.5f : 0.0f;
        const auto x0 = bar.getX() + bar.getWidth() * juce::jmin (from, value), x1 = bar.getX() + bar.getWidth() * juce::jmax (from, value);
        if (x1 - x0 > 0.5f)
        {
            const auto fill = juce::Rectangle<float> (x0, bar.getY(), x1 - x0, bar.getHeight());
            const auto base = on ? juce::Colours::white.withAlpha (active ? 0.42f : 0.30f + 0.06f * hot) : colour::textDisabled.withAlpha (0.4f);
            if (referenceLabels && on)
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6c6c6c).brighter (0.25f * hot), 0.0f, fill.getY(), juce::Colour (0xff484848).brighter (0.25f * hot), 0.0f, fill.getBottom(), false));
            else
                g.setGradientFill (juce::ColourGradient (base.brighter (0.2f), 0.0f, fill.getY(), base.darker (0.3f), 0.0f, fill.getBottom(), false));
            g.fillRoundedRectangle (fill, 2.5f);
        }
        const auto hx = bar.getX() + bar.getWidth() * value;
        if (referenceLabels)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (hx - 0.5f, bar.getY() + 1.0f, 1.0f, bar.getHeight() - 2.0f));
        }
        else
        {
            g.setColour (on ? (active ? ac.brighter (0.25f) : colour::textPrimary.withAlpha (0.85f)) : colour::textDisabled);
            g.fillRect (juce::Rectangle<float> (hx - 1.5f, bar.getY() + 1.0f, 3.0f, bar.getHeight() - 2.0f));
        }
        // (rangeRow pushes the ends 6 px past the row: the slider keeps them within its own bar, clear of its neighbours)
        const auto row = juce::Rectangle<float> (bar.getX() + 6.0f, bar.getBottom() + 2.0f, bar.getWidth() - 12.0f - c.rowTrim, 12.0f);
        rangeRow (g, c.param, row, c.showValue && (hover || active), false, ac, active, c.endLow, c.endHigh);
        captionStyle (g, active, hot, ac, type::caption);
        g.drawText (caps (c.caption), juce::Rectangle<float> (c.r.getX() - 14.0f, row.getBottom() + 1.0f, c.r.getWidth() + 28.0f, 14.0f),
                    juce::Justification::centred, false);
    }
    // round tp172b6: the multiband reference's knob, our own drawing: a chunky grey face, a white pointer cut into its
    // rim with a dark line in it, an outlined scale ring around it with ticks and a gap at the bottom
    static constexpr float faceSweep = juce::MathConstants<float>::pi * 0.75f;
    static void facePointer (juce::Graphics& g, juce::Point<float> ct, float radius, float value, float size, bool on)
    {
        const auto angle = faceSweep * (2.0f * juce::jlimit (0.0f, 1.0f, value) - 1.0f);
        const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
        const auto side = juce::Point<float> (dir.y * -1.0f, dir.x);
        juce::Path disc;
        disc.addEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (ct));
        // a rounded fin: wide at the rim, its convex sides meeting in a round end towards the centre (`size` = half its width)
        const auto depth = size * 1.75f; // from the rim inwards
        const auto at = [&] (float along, float across) { return ct + dir * along + side * across; };
        juce::Path fin;
        fin.startNewSubPath (at (radius + 2.0f, -size * 1.1f));
        fin.quadraticTo (at (radius - depth * 0.55f, -size * 1.1f), at (radius - depth * 0.92f, -size * 0.42f));
        fin.quadraticTo (at (radius - depth * 1.08f, 0.0f), at (radius - depth * 0.92f, size * 0.42f));
        fin.quadraticTo (at (radius - depth * 0.55f, size * 1.1f), at (radius + 2.0f, size * 1.1f));
        fin.closeSubPath();
        juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (disc);
        const auto light = juce::Colour (on ? 0xfff4f4f4u : 0xff9a9a9au), shade = juce::Colour (on ? 0xffb4b4b4u : 0xff707070u);
        const auto mid = at (radius - depth * 0.5f, 0.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (fin, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (juce::ColourGradient (light, mid.x, mid.y - size, shade, mid.x, mid.y + size, false));
        g.strokePath (fin, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillPath (fin);
        g.setColour (juce::Colour (0xff3a3a3a).withAlpha (0.85f));
        g.drawLine (juce::Line<float> (at (radius - depth * 0.72f, 0.0f), at (radius - depth * 0.28f, 0.0f)), 1.0f);
    }
    static void faceDisc (juce::Graphics& g, juce::Point<float> ct, float radius, juce::Colour top, juce::Colour bottom, float hot)
    {
        const auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (ct);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (r.expanded (1.2f));
        g.setGradientFill (juce::ColourGradient (top.brighter (0.12f * hot), ct.x - radius * 0.3f, ct.y - radius, bottom.brighter (0.12f * hot), ct.x + radius * 0.3f, ct.y + radius, false));
        g.fillEllipse (r);
    }
    void paintFaceKnob (juce::Graphics& g, const Ctl& c, bool hover, bool active, float hot, juce::Colour ac, bool on, float d)
    {
        using namespace theme;
        const auto value = v (c.param);
        if (c.look == 3) // the small knob inside the ring; the labels belong to it
        {
            const auto ct = c.r.getCentre();
            const auto radius = c.r.getWidth() * 0.5f;
            faceDisc (g, ct, radius, juce::Colour (0xff868686), juce::Colour (0xff585858), hot);
            facePointer (g, ct, radius, value, radius * 0.38f, on);
            const auto row = juce::Rectangle<float> (c.outer.getX() + 5.0f, c.outer.getBottom() + 1.0f, c.outer.getWidth() - 10.0f, 12.0f);
            const auto ringActive = c.ringParam >= 0 && dragCtl == c.ringParam;
            const auto ringHover = c.ringParam >= 0 && c.outer.contains (mouse) && ! c.r.contains (mouse);
            const auto valueNow = c.showValue && (hover || active);
            if (ringActive || ringHover)
                rangeRow (g, c.ringParam, row, true, false, ac, ringActive);
            else
                rangeRow (g, c.param, row, valueNow, false, ac, active);
            if (! (ringActive || ringHover || valueNow))
            {
                g.setColour (colour::textDisabled.withAlpha (0.6f));
                g.setFont (font (type::caption));
                g.drawText (c.ringLow, row.withWidth (12.0f).translated (-17.0f, 0.0f), juce::Justification::centredRight, false);
                g.drawText (c.ringHigh, row.withX (row.getRight()).withWidth (12.0f).translated (5.0f, 0.0f), juce::Justification::centredLeft, false);
            }
            captionStyle (g, active || ringActive, hot, ac, type::caption);
            g.drawText (caps (c.caption), juce::Rectangle<float> (c.outer.getX() - 14.0f, row.getBottom() + 1.0f, c.outer.getWidth() + 28.0f, 14.0f), juce::Justification::centred, false);
            return;
        }
        if (c.look == 4)
            d = c.r.getWidth(); // (the ring's rectangle is the knob itself, no labels)
        const auto box = c.look == 4 ? c.r : juce::Rectangle<float> (d, d).withCentre ({ c.r.getCentreX(), c.r.getY() + 2.0f + d * 0.5f });
        const auto ct = box.getCentre();
        const auto scaleR = d * 0.5f - 2.5f;
        const auto radius = d * 0.5f - 6.5f;
        // the scale ring: a thin outlined band with the gap at the bottom and its ticks
        juce::Path band;
        band.addCentredArc (ct.x, ct.y, scaleR + 2.5f, scaleR + 2.5f, 0.0f, -faceSweep, faceSweep, true);
        band.addCentredArc (ct.x, ct.y, scaleR - 2.5f, scaleR - 2.5f, 0.0f, faceSweep, -faceSweep, false);
        band.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.fillPath (band);
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.strokePath (band, juce::PathStrokeType (1.0f));
        for (int k = 0; k <= 6; ++k)
        {
            const auto a = faceSweep * (2.0f * (float) k / 6.0f - 1.0f);
            const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
            g.drawLine (juce::Line<float> (ct + dir * (scaleR - 2.5f), ct + dir * (scaleR + 2.5f)), 1.0f);
        }
        if (c.levelArc > 0.0f && on) // round tp172b6: the live level on the scale ring, green turning to gold as it rises
        {
            static constexpr int steps = 28;
            const auto green = juce::Colour (0xff7fa62c), gold = juce::Colour (0xffd9a21f);
            const auto top = juce::jmin (1.0f, c.levelArc);
            for (int k = 0; k < steps; ++k)
            {
                const auto u0 = top * (float) k / (float) steps, u1 = top * (float) (k + 1) / (float) steps;
                if (u1 <= u0) continue;
                juce::Path bit;
                bit.addCentredArc (ct.x, ct.y, scaleR, scaleR, 0.0f, faceSweep * (2.0f * u0 - 1.0f), faceSweep * (2.0f * u1 - 1.0f), true);
                g.setColour (green.interpolatedWith (gold, juce::jlimit (0.0f, 1.0f, (u1 - 0.45f) / 0.25f)));
                g.strokePath (bit, juce::PathStrokeType (4.0f));
            }
        }
        if (c.bipolar) // the centre mark above the ring
        {
            g.setColour (juce::Colours::white.withAlpha (0.28f));
            g.drawLine (juce::Line<float> (ct.x, ct.y - scaleR - 7.0f, ct.x, ct.y - scaleR - 3.5f), 1.0f);
        }
        if (c.look == 4) // the ring knob: a darker face; the small knob lies on top of it
        {
            faceDisc (g, ct, radius, juce::Colour (0xff5e5e5e), juce::Colour (0xff3e3e3e), hot);
            facePointer (g, ct, radius, value, radius * 0.26f, on);
            return;
        }
        faceDisc (g, ct, radius, juce::Colour (0xff6e6e6e), juce::Colour (0xff474747), hot);
        facePointer (g, ct, radius, value, radius * 0.40f, on);
        knobLabels (g, c, box, hover, active, hot, ac);
    }
    // a short curved bar (a smile) filled in the accent up to the value, the scale and the caption under it
    void paintArcBar (juce::Graphics& g, const Ctl& c, bool hover, bool active, float hot, juce::Colour ac, bool on)
    {
        using namespace theme;
        const auto bw = juce::jmin (c.r.getWidth() - 8.0f, 72.0f);
        const auto R = bw * 1.15f;
        const auto theta = std::asin (juce::jlimit (0.0f, 1.0f, bw * 0.5f / R));
        const auto cx = c.r.getCentreX();
        const auto sag = R * (1.0f - std::cos (theta));
        const auto cy = c.r.getY() + 4.0f + sag - R; // the bar's ends at the top, its middle `sag` lower
        const auto left = juce::MathConstants<float>::pi + theta, right = juce::MathConstants<float>::pi - theta;
        const auto value = juce::jlimit (0.0f, 1.0f, v (c.param));
        const auto at = [&] (float u) { return left + (right - left) * u; };
        const auto t = 4.0f;
        juce::Path track;
        track.addCentredArc (cx, cy, R, R, 0.0f, left, right, true);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.strokePath (track, juce::PathStrokeType (t + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.10f + 0.06f * hot));
        g.strokePath (track, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto from = c.bipolar ? at (0.5f) : left, to = at (value);
        if (std::abs (to - from) > 0.002f)
        {
            juce::Path fill;
            fill.addCentredArc (cx, cy, R, R, 0.0f, from, to, true);
            g.setColour (on ? (active ? ac.brighter (0.25f) : ac) : colour::textDisabled);
            g.strokePath (fill, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto barBottom = c.r.getY() + 4.0f + sag + t;
        const auto row = juce::Rectangle<float> (cx - bw * 0.5f, barBottom + 1.0f, bw, 12.0f);
        if (knobRanges)
        {
            rangeRow (g, c.param, row, c.showValue && (hover || active), false, ac, active);
            g.setColour (active ? ac.brighter (0.3f) : skin::captionColour().interpolatedWith (colour::textPrimary, hot * 0.5f));
            g.setFont (font (type::caption, true));
            g.drawText (caps (c.caption), juce::Rectangle<float> (c.r.getX() - 14.0f, row.getBottom() + 1.0f, c.r.getWidth() + 28.0f, 14.0f),
                        juce::Justification::centred, false);
            return;
        }
        // round tp172b5: the caption right under the bar (the reference's), the value under it while hovered or dragged
        g.setColour (active ? ac.brighter (0.3f) : skin::captionColour().interpolatedWith (colour::textPrimary, hot * 0.5f));
        g.setFont (font (type::caption, true));
        g.drawText (caps (c.caption), juce::Rectangle<float> (c.r.getX() - 14.0f, row.getY(), c.r.getWidth() + 28.0f, 14.0f),
                    juce::Justification::centred, false);
        if (c.showValue && (hover || active))
            rangeRow (g, c.param, row.translated (0.0f, 14.0f), true, false, ac, active);
    }

    // a floating panel (the knob panels over the display)
    static void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour edge = theme::colour::borderStrong)
    {
        skin::floatingPanel (g, r, edge);
    }

    // the background of a display (the level history, the analyser): deep, with the accent pooling under the header
    void displayBackground (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        skin::backdrop (g, r, accent); // round 126: aurora blooms of the accent, cached as an image
    }
    // a level area filled with a vertical gradient of `c` (strong at the top, fading down)
    static void fillLevel (juce::Graphics& g, const juce::Path& p, juce::Colour c, juce::Rectangle<float> r, float top, float bottom)
    {
        g.setGradientFill (juce::ColourGradient (c.withAlpha (top), 0.0f, r.getY(), c.withAlpha (bottom), 0.0f, r.getBottom(), false));
        g.fillPath (p);
    }

    static void caption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Justification j = juce::Justification::centred)
    {
        using namespace theme;
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption, true));
        g.drawText (caps (t), r, j, false);
    }

    // --- level history -------------------------------------------------------------------------------
    static constexpr int historyLength = 1400; // columns of 10 ms: 14 s
    std::vector<Column> history;
    int historyPos = 0; // the next column to write
    std::uint32_t lastWritten = 0;
    float pendingIn = -120.0f, pendingOut = -120.0f;
    int pendingCount = 0;

    void pushColumn (Column c)
    {
        silentRun = c.in < -90.0f && c.out < -90.0f && c.gr < 0.05f ? silentRun + 1 : 0;
        history[(std::size_t) historyPos] = c;
        historyPos = (historyPos + 1) % historyLength;
    }
    // column k back from the newest (0 = newest)
    const Column& columnBack (int k) const { return history[(std::size_t) ((historyPos - 1 - k + historyLength * 4) % historyLength)]; }

    void pullHistory()
    {
        if (demo) return;
        auto* times = engine.getProcessTimes();
        const auto gr = grDb();
        if (times != nullptr && times->analyzer.key.load (std::memory_order_relaxed) == tapKey())
        {
            auto& tap = times->analyzer;
            const auto written = tap.written.load (std::memory_order_acquire);
            auto fresh = written - lastWritten;
            lastWritten = written;
            if (fresh == 0 || fresh > (std::uint32_t) ProcessTimes::AnalyzerTap::size / 2)
            {
                if (fresh == 0) pushColumn ({ -120.0f, -120.0f, 0.0f }); // silence keeps the display moving
                return;
            }
            const auto colSamples = juce::jmax (64, (int) (tap.sampleRate.load (std::memory_order_relaxed) / 100.0));
            for (std::uint32_t i = 0; i < fresh; ++i)
            {
                const auto at = (written - fresh + i) & (ProcessTimes::AnalyzerTap::size - 1);
                pendingIn = juce::jmax (pendingIn, std::abs (tap.pre[(std::size_t) at].load (std::memory_order_relaxed)));
                pendingOut = juce::jmax (pendingOut, std::abs (tap.post[(std::size_t) at].load (std::memory_order_relaxed)));
                if (++pendingCount >= colSamples)
                {
                    pushColumn ({ dbOf (pendingIn), dbOf (pendingOut), gr });
                    pendingIn = pendingOut = 0.0f;
                    pendingCount = 0;
                }
            }
            return;
        }
        if (hasKeyboardFocus (true) || isMouseOver (true)) claimTap();
        // without the tap: the effect's block meters, three columns per refresh
        const auto in = dbOf (liveMeter (2)), out = dbOf (liveMeter (3));
        for (int i = 0; i < 3; ++i) pushColumn ({ in, out, gr });
    }

    // a vertical level meter (peak with a slow fall), from `lo` dB at the bottom to 0 dB at the top
    static void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float db, float lo, juce::Colour c)
    {
        const auto f = juce::jlimit (0.0f, 1.0f, (db - lo) / -lo);
        if (c.getHue() > 0.25f && c.getHue() < 0.45f && c.getSaturation() > 0.4f) // the green level meters: the lit gradient
        {
            skin::meter (g, r, f, 0.0f, true);
            return;
        }
        skin::meterSolid (g, r, f, c.withAlpha (1.0f), false);
        g.setColour (juce::Colour (0xff08090b).withAlpha (0.55f));
        for (auto y = r.getBottom() - 3.0f; y > r.getBottom() - r.getHeight() * f; y -= 3.0f) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    }

    // --- spectrum analyser (the slot's input / output through the analyser tap) ----------------------
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
    std::vector<float> specPre, specPost; // dB per column over `specArea`
    juce::Rectangle<float> specArea;
    double specMinHz = 10.0, specMaxHz = 30000.0;
    float specFall = 1.5f;   // dB per frame the analyser falls (its speed; round 128)
    bool specFrozen = false; // the analyser holds its curve (freeze; round 128)

    float xForHz (double f, juce::Rectangle<float> a) const
    {
        return a.getX() + a.getWidth() * (float) (std::log (juce::jlimit (specMinHz, specMaxHz, f) / specMinHz) / std::log (specMaxHz / specMinHz));
    }
    double hzForX (float x, juce::Rectangle<float> a) const
    {
        return specMinHz * std::pow (specMaxHz / specMinHz, juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth())));
    }
    // round 144 (Pro-MB's analyser options): the tilt in dB/oct around 1 kHz (4.5 as before) and the resolution
    // (0 low … 3 maximum = the whole FFT window, as before: lower ones analyse a shorter, faster window)
    double specTilt = 4.5;
    int specResolution = 3;
    double tiltDb (double f) const { return specTilt * std::log2 (juce::jmax (10.0, f) / 1000.0); }

    void updateSpectrum()
    {
        const auto a = specArea;
        if (a.isEmpty()) return;
        const auto columns = juce::jmax (64, (int) a.getWidth() / 3);
        if ((int) specPre.size() != columns)
        {
            specPre.assign ((std::size_t) columns, -120.0f);
            specPost.assign ((std::size_t) columns, -120.0f);
        }
        if (specFrozen && ! demo) return;
        if (demo)
        {
            for (int i = 0; i < columns; ++i)
            {
                const auto f = hzForX (a.getX() + a.getWidth() * (float) i / (float) (columns - 1), a);
                specPre[(std::size_t) i] = specPost[(std::size_t) i] = (float) demoSpectrumDb (f);
            }
            return;
        }
        auto* times = engine.getProcessTimes();
        if (times == nullptr) return;
        auto& tap = times->analyzer;
        if (tap.key.load (std::memory_order_relaxed) != tapKey())
        {
            if (hasKeyboardFocus (true) || isMouseOver (true)) claimTap();
            return;
        }
        const auto written = tap.written.load (std::memory_order_acquire);
        if (written == lastSpectrum)
        {
            for (auto* s : { &specPre, &specPost })
                for (auto& x : *s) x = juce::jmax (-120.0f, x - specFall);
            return;
        }
        lastSpectrum = written;
        const auto sr = tap.sampleRate.load (std::memory_order_relaxed);
        for (int which = 0; which < 2; ++which)
        {
            auto& ring = which == 0 ? tap.pre : tap.post;
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            const int len = fftSize >> (3 - juce::jlimit (0, 3, specResolution)); // the newest `len` samples, zero-padded
            for (int i = 0; i < len; ++i)
            {
                const auto w = len == fftSize ? window[(std::size_t) i]
                                              : 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) len);
                fftData[(std::size_t) i] = ring[(std::size_t) ((written - (std::uint32_t) len + (std::uint32_t) i) & (ProcessTimes::AnalyzerTap::size - 1))]
                                               .load (std::memory_order_relaxed) * w;
            }
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
            auto& out = which == 0 ? specPre : specPost;
            const auto norm = 4.0f / (float) len;
            for (int i = 0; i < columns; ++i)
            {
                const auto f0 = hzForX (a.getX() + a.getWidth() * ((float) i - 0.5f) / (float) (columns - 1), a);
                const auto f1 = hzForX (a.getX() + a.getWidth() * ((float) i + 0.5f) / (float) (columns - 1), a);
                auto k0 = (int) std::floor (f0 * fftSize / sr), k1 = (int) std::ceil (f1 * fftSize / sr);
                k0 = juce::jlimit (1, fftSize / 2 - 1, k0);
                k1 = juce::jlimit (k0, fftSize / 2 - 1, k1);
                float m = 0.0f;
                for (int k = k0; k <= k1; ++k) m = juce::jmax (m, fftData[(std::size_t) k]);
                const auto f = hzForX (a.getX() + a.getWidth() * (float) i / (float) (columns - 1), a);
                const auto db = (float) (20.0 * std::log10 (m * norm + 1e-9) + tiltDb (f));
                auto& x = out[(std::size_t) i];
                x = db > x ? db : juce::jmax (db, x - specFall);
            }
        }
    }

    // the analyser curve as a filled path (dB → y by `yOf`)
    void paintSpectrum (juce::Graphics& g, const std::vector<float>& spec, const std::function<float (float)>& yOf, juce::Colour fill, juce::Colour line) const
    {
        const auto a = specArea;
        if (spec.size() < 2) return;
        juce::Path p;
        p.startNewSubPath (a.getX(), a.getBottom());
        for (std::size_t i = 0; i < spec.size(); ++i)
            p.lineTo (a.getX() + a.getWidth() * (float) i / (float) (spec.size() - 1), juce::jlimit (a.getY(), a.getBottom(), yOf (spec[i])));
        p.lineTo (a.getRight(), a.getBottom());
        p.closeSubPath();
        g.setColour (fill);
        g.fillPath (p);
        g.setColour (line);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    void frequencyLabels (juce::Graphics& g, juce::Rectangle<float> a, float y) const
    {
        using namespace theme;
        g.setFont (font (type::caption));
        for (const auto hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            if (hz < specMinHz * 0.999 || hz > specMaxHz * 1.001) continue; // (outside the display: no label pinned to its edge)
            const auto x = xForHz (hz, a);
            g.setColour (colour::gridBeat);
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (colour::textDisabled);
            g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, y }), juce::Justification::centred, false);
        }
    }

    AudioEngine& engine;
    int track, slot;
    EffectKind kind;
    bool demo = false;
    pluginheader::PresetBar bar { engine, track, slot }; // undo / redo, A / B, presets (round 129)
    juce::Colour accent;   // the plugin family's accent (ui/PluginSkin.h)
    skin::Animator anim;   // hover fades (runs only while something moves)
    int silentRun = historyLength; // consecutive silent history columns (the history starts silent) (an idle display is not repainted)
    juce::Point<float> mouse { -1.0f, -1.0f };
    int dragCtl = -1;
    Ctl pressed;
    float hDragStart = 0.0f, hDragAnchorX = 0.0f; // a horizontal slider's drag: the value and the pointer where it began
    bool hDragFine = false;
    ui::ValueDrag valueDrag;

private:
    int tapKey() const { return track * maxEffectSlots + slot; }
    void claimTap()
    {
        if (auto* times = engine.getProcessTimes()) times->analyzer.key.store (tapKey());
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    void timerCallback() override
    {
        if (! isShowing()) return; // a hidden editor costs nothing
        if (usesHistory()) pullHistory();
        if (usesSpectrum()) updateSpectrum();
        tick();
        // repaint only while the display can change: signal in the visible history, a spectrum above the floor,
        // live meters moving (an idle editor on a silent track stops drawing)
        auto live = usesHistory() && silentRun < historyLength;
        if (usesSpectrum())
            for (const auto* sp : { &specPre, &specPost })
                for (const auto x : *sp)
                    if (x > -119.0f) { live = true; break; }
        float meters = grDb();
        for (int k = 0; k < 6; ++k) meters += liveMeter (k) * (float) (k + 1);
        if (std::abs (meters - lastMeters) > 1e-5f) live = true;
        lastMeters = meters;
        // knobs following their automation (issue #12)
        if (const auto& shown = engine.getAutomationDisplay(); shown.getGeneration() != shownAutomation)
        {
            shownAutomation = shown.getGeneration();
            for (const auto& t : shown.automatedTargets())
                if (t.kind == AutomationTargetKind::effectParam && t.index == track && t.slot == slot)
                    live = true;
        }
        if (live || demo) repaint();
    }
    float lastMeters = 0.0f;
    std::uint32_t shownAutomation = 0;

    juce::dsp::FFT fft;
    std::vector<float> fftData, window;
    std::uint32_t lastSpectrum = 0;
};
} // namespace lynx::pro
