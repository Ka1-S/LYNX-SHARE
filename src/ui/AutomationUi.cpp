#include "core/FlCurve.h"
#include "ui/FlMods.h"
#include "ui/WindowOwner.h"
#include "ui/AutomationUi.h"
#include "ui/RemoteControl.h"
#include "core/PlaylistOps.h"
#include "core/LastTweaked.h"

#include <map>

#include "core/Effects.h"
#include "core/ControlSurface.h"
#include "core/VoltageController.h"
#include "core/XyController.h"
#include "core/MidiControls.h"
#include "core/MidiOut.h"
#include "core/FlSynths.h"
#include "engine/Text.h"
#include "core/EditorGestures.h"
#include "ui/EditorTools.h"
#include "ui/EventEditor.h"
#include "ui/I18n.h"
#include "ui/HintBar.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/ControllerKnob.h"

namespace lynx
{
namespace
{
juce::String nameOf (const std::string& s)
{
    return juce::String::fromUTF8 (s.c_str());
}

juce::AudioPluginInstance* pluginFor (AudioEngine& engine, const AutomationTarget& t)
{
    if (t.kind == AutomationTargetKind::channelParam)
        if (auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (t.index).get()))
            return &p->getInstance();

    if (t.kind == AutomationTargetKind::effectParam)
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (t.index, t.slot).get()))
            return &p->getInstance();

    return nullptr;
}

// "Main L offset", "Out 3 R scale" (the Voltage Controller's own parameters, round 165)
juce::String voltageParamLabel (int param)
{
    static const char* outputs[] = { TR_NOOP ("Main L"), TR_NOOP ("Main R"), TR_NOOP ("Out 2 L"), TR_NOOP ("Out 2 R"),
                                     TR_NOOP ("Out 3 L"), TR_NOOP ("Out 3 R"), TR_NOOP ("Out 4 L"), TR_NOOP ("Out 4 R") };
    const auto output = tr (outputs[param % voltage::outputs]);
    return param < voltage::outputs ? tr ("{0} offset", { output }) : tr ("{0} scale", { output });
}

// The CC / aftertouch / pitch bend value the plugin was last sent (or the controller's rest value).
float midiControlValue (AudioEngine& engine, const AutomationTarget& t)
{
    if (t.kind == AutomationTargetKind::channelParam)
        if (auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (t.index).get()))
            return p->midiControlValue (t.param);
    if (t.kind == AutomationTargetKind::effectParam)
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (t.index, t.slot).get()))
            return p->midiControlValue (t.param);
    return midictl::defaultValue (t.param);
}

// Point editor: time horizontal (bars), value vertical.
//  - click on empty space creates a point; drag moves; right-click deletes
//  - Alt+drag over a segment changes the curve (tension)
//  - Ctrl+wheel zooms at the mouse; wheel scrolls; middle-drag pans
//  - the cursor and the hint bar show what a click would do
// FL's small LED check box with its label on the right (the automation editor's "MUL"): a round light, lit when on
class LedToggle final : public juce::Button
{
public:
    explicit LedToggle (const juce::String& text) : juce::Button (text) { setClickingTogglesState (false); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto led = juce::Rectangle<float> (2.0f, (float) getHeight() * 0.5f - 4.0f, 8.0f, 8.0f);
        const auto on = getToggleState();
        g.setColour (on ? theme::colour::accent : theme::colour::bg0);
        g.fillEllipse (led);
        g.setColour ((on ? theme::colour::accent.brighter (0.3f) : theme::colour::textSecondary).withAlpha (over ? 1.0f : 0.8f));
        g.drawEllipse (led, 1.0f);
        g.setColour (over ? theme::colour::textPrimary : theme::colour::textSecondary);
        g.setFont (theme::font (10.0f, true));
        g.drawText (getButtonText(), getLocalBounds().withTrimmedLeft (13), juce::Justification::centredLeft);
    }
};

class AutomationEditor final : public juce::Component,
                               public ui::HintSource,
                               private juce::ChangeListener,
                               private juce::Timer
{
public:
    AutomationEditor (AudioEngine& e, int index)
        : engine (e),
          automation (index)
    {
        engine.addChangeListener (this);
        buildLfoPanel();
        refreshLfoPanel();
        setSize (760, 330);
        startTimerHz (30);
    }

    // round 177b: FL's LFO mode (REA FUN_00f62690 / FUN_00f6b6a0): with the LFO on, the editor edits the LFO's own points
    // (its amplitude over the clip) instead of the clip's points
    static const std::vector<AutomationPoint>& editedPoints (const Automation& a) { return a.lfo.on ? a.lfo.amp : a.points; }
    static std::vector<AutomationPoint>& editedPoints (Automation& a) { return a.lfo.on ? a.lfo.amp : a.points; }
    static float curveAt (const Automation& a, double tick) { return automationCurveAt (editedPoints (a), tick); }

    void resized() override
    {
        // FL's layout: the "Automation editor | LFO" row above the grid; in LFO mode the small knobs, MUL and the
        // options arrow sit under the grid, bottom-left
        auto top = getLocalBounds().removeFromTop (tabRowHeight).reduced (8, 2);
        top.removeFromLeft (editorCaptionWidth());
        lfoButton.setBounds (top.removeFromLeft (64));
        const auto* a = current();
        const auto on = a != nullptr && a->lfo.on;
        auto bar = getLocalBounds().removeFromBottom (lfoBarHeight()).reduced (8, 2);
        // FL: the MUL light right of the knobs; the options arrow on the row below, under SPD
        auto row = bar.removeFromBottom (lfoArrowRow);
        lfoOptions.setVisible (on);
        lfoOptions.setBounds (row.removeFromLeft (38).withSizeKeepingCentre (10, 8));
        for (auto* k : lfoKnobs)
        {
            k->setVisible (on);
            k->setBounds (bar.removeFromLeft (38));
        }
        bar.removeFromLeft (4);
        mulButton.setVisible (on);
        mulButton.setBounds (bar.removeFromLeft (40).withSizeKeepingCentre (40, 14));
    }

    ~AutomationEditor() override { engine.removeChangeListener (this); }

    int getIndex() const noexcept { return automation; }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg1);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::small));
        g.drawText (tr ("Automation editor"), 8, 0, editorCaptionWidth(), tabRowHeight, juce::Justification::centredLeft);
        const auto* a = current();

        if (a == nullptr)
            return;

        const auto& song = engine.getSong();
        const auto area = plotArea();
        const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);

        g.setColour (colour::bg0);
        g.fillRect (area);

        // grid: beats and bars
        const auto beatPixels = ppq * pixelsPerTick;
        const auto lastTick = (std::int64_t) xToTick (area.getRight());

        for (auto tick = (std::int64_t) std::floor (scroll / ppq) * ppq; tick <= lastTick; tick += ppq)
        {
            const auto isBar = tick % bar == 0;

            if (! isBar && beatPixels < 8.0)
                continue;

            const auto x = (int) tickToX ((double) tick);
            g.setColour (isBar ? colour::gridBar : colour::gridBeat);
            g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());

            if (isBar)
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (type::caption));
                g.drawText (juce::String (tick / bar + 1), x + 3, tabRowHeight + 2, 40, 16, juce::Justification::centredLeft);
            }
        }

        for (auto v : { 0.25f, 0.5f, 0.75f })
        {
            g.setColour (colour::gridStep.brighter (0.15f));
            g.drawHorizontalLine ((int) valueToY (v), (float) area.getX(), (float) area.getRight());
        }

        // curve: the clip's points, or in LFO mode (FL-exact) the LFO's wave around the middle, with the LFO's
        // amplitude points as handles on a thin line
        juce::Path curve, amp;
        bool ampStarted = false; // (Path::isEmpty stays true after a lone startNewSubPath)
        const auto& points = editedPoints (*a);
        const auto lfo = a->lfo.on;

        // (a sample per pixel; round 190bw: Hold, Stairs and Pulse segments keep square corners)
        auto lastY = 0.0f, lastAmpY = 0.0f;
        for (auto x = area.getX(); x <= area.getRight(); ++x)
        {
            const auto tick = xToTick (x);
            // FL draws the LFO alone around the middle: 0.5 + the swing (half the waveform × the amplitude) / 2
            const auto y = valueToY (lfo ? 0.5f + 0.25f * a->lfo.output (tick, tick) : curveAt (*a, tick));

            if (x == area.getX())
                curve.startNewSubPath ((float) x, y);
            else if (lfo)
                curve.lineTo ((float) x, y);
            else
                automationLineTo (curve, (float) x - 1.0f, lastY, (float) x, y, [&] { return valueToY (curveAt (*a, xToTick ((double) x - 0.5))); });
            lastY = y;

            // FL draws the amplitude line from the first point to the last one, nothing past it
            if (lfo && ! points.empty() && tick >= (double) points.front().tick && tick <= (double) points.back().tick)
            {
                const auto ya = valueToY (curveAt (*a, tick));
                if (! ampStarted)
                    amp.startNewSubPath ((float) x, ya), ampStarted = true;
                else
                    automationLineTo (amp, (float) x - 1.0f, lastAmpY, (float) x, ya, [&] { return valueToY (curveAt (*a, xToTick ((double) x - 0.5))); });
                lastAmpY = ya;
            }
        }

        if (lfo)
        {
            // FL's envelope region: a lighter band over the upper half, from the clip start to the last amplitude point
            const auto x0 = (float) juce::jmax ((double) area.getX(), tickToX (0.0));
            const auto x1 = (float) juce::jmin ((double) area.getRight(), tickToX (points.empty() ? 0.0 : (double) points.back().tick));
            if (x1 > x0)
            {
                g.setColour (colour::textSecondary.withAlpha (0.07f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, (float) area.getY(), x1, valueToY (0.5f)));
            }
        }
        else
        {
            // soft fill under the curve (not in LFO mode: FL draws the wave alone)
            auto fill = curve;
            fill.lineTo ((float) area.getRight(), (float) area.getBottom());
            fill.lineTo ((float) area.getX(), (float) area.getBottom());
            fill.closeSubPath();
            g.setColour (colour::automation.withAlpha (0.10f));
            g.fillPath (fill);
        }
        g.setColour (colour::automation);
        g.strokePath (curve, juce::PathStrokeType (lfo ? 1.5f : 2.0f, lfo ? juce::PathStrokeType::curved : juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        if (lfo)
        {
            g.setColour (colour::textSecondary.withAlpha (0.45f));
            g.strokePath (amp, juce::PathStrokeType (1.0f));
        }

        // round 165: FL's tension handles, hollow, in the middle of each segment
        for (int i = 0; i + 1 < (int) points.size(); ++i)
            if (const auto h = handlePosition (i); h.x >= 0.0f)
            {
                g.setColour (i == hoverHandle || i == tensionPoint ? colour::textPrimary : colour::automation);
                g.drawEllipse (h.x - 4.5f, h.y - 4.5f, 9.0f, 9.0f, 1.5f);
            }

        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const auto p = pointPosition (points[i]);
            const auto active = (int) i == hoverPoint || (int) i == dragPoint;
            const auto r = lfo ? 3.5f : 4.5f; // LFO mode: the amplitude points stay small, the wave is the main line
            g.setColour (colour::bg0);
            g.fillEllipse (p.x - r - 1.5f, p.y - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);
            g.setColour (active ? colour::textPrimary : (lfo ? colour::textSecondary : colour::automation));
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        }

        // playback cursor (at the position of the clip currently playing)
        if (playhead >= 0.0)
        {
            g.setColour (colour::playhead);
            g.drawVerticalLine ((int) tickToX (playhead), (float) area.getY(), (float) area.getBottom());
        }


        if (hoverPoint >= 0 && hoverPoint < (int) points.size())
        {
            const auto p = pointPosition (points[(std::size_t) hoverPoint]);
            g.setColour (colour::textPrimary);
            g.setFont (font (type::small));
            const auto value = points[(std::size_t) hoverPoint].value;
            g.drawText (a->target.kind == AutomationTargetKind::tempo && ! a->lfo.on ? juce::String (tempoFromValue (value), 1) + " BPM"
                                                                                       : juce::String (juce::roundToInt (value * 100.0f)) + "%",
                        (int) p.x + 8, (int) p.y - 18, 60, 14, juce::Justification::centredLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hit = pointAt (e.position);
        const auto handle = hit < 0 ? handleAt (e.position) : -1;

        if (hit != hoverPoint || handle != hoverHandle)
        {
            hoverPoint = hit;
            hoverHandle = handle;
            repaint();
        }

        updateCursor (e.mods);
    }

    void modifierKeysChanged (const juce::ModifierKeys& mods) override
    {
        if (dragPoint < 0 && tensionPoint < 0 && ! panning)
            updateCursor (mods);
    }

    void updateCursor (juce::ModifierKeys mods)
    {
        using C = gesture::Cursor;
        const auto inPlot = plotArea().contains (getMouseXYRelative());

        if (inPlot && hoverPoint < 0 && (mods.isAltDown() || hoverHandle >= 0))
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor); // drag up / down bends the segment
        else
            setMouseCursor (editorCursor (! inPlot ? C::normal : hoverPoint >= 0 ? C::move : C::draw));
    }

    ui::Hint hintAt (juce::Point<int> position) override
    {
        ui::Hint hint;

        if (! plotArea().contains (position))
            return hint;

        const auto* a = current();
        const auto point = pointAt (position.toFloat());
        hint.hasMenu = true;

        if (point >= 0)
            hint.text = tr ("Point: [LMB] drag moves, then [Shift] keeps time, [Ctrl] keeps value  [Alt] no snap  [RMB] menu");
        else if (handleAt (position.toFloat()) >= 0)
            hint.text = tr ("Tension: [LMB] drag up / down bends the segment ([Ctrl] fine)  [RMB] straight");
        else if (juce::ModifierKeys::currentModifiers.isAltDown())
            hint.text = tr ("[Alt][LMB] drag up / down bends this segment");
        else
            hint.text = tr ("[LMB] or [RMB] new point (hold and drag)  [Shift][RMB] point on the curve  [Alt][LMB] bend  [Ctrl][Wheel] zoom  [MMB] scroll");

        if (a != nullptr)
            hint.value = valueText (point >= 0 ? editedPoints (*a)[(std::size_t) point].value : yToValue (position.y));
        return hint;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* a = current();

        if (a == nullptr || ! plotArea().contains (e.getPosition()))
            return;

        const auto hit = pointAt (e.position);

        downMods = e.mods;

        if (e.mods.isMiddleButtonDown()) // middle-drag scrolls
        {
            panning = true;
            panStart = scroll;
            setMouseCursor (editorCursor (gesture::Cursor::pan));
            return;
        }

        const auto handle = hit < 0 ? handleAt (e.position) : -1;

        if (flmods::popup (e.mods))
        {
            if (hit >= 0)
            {
                showPointMenu (hit);
                return;
            }
            if (handle >= 0) // FL: right-click on the tension handle = straight segment
            {
                automationPointMemory().tension = 0.0f; // FL's "default tension" also clears the editor's memory
                edit ([handle] (Automation& x) { setAutomationSegmentTension (editedPoints (x), handle, 0.0f); });
                return;
            }
            // FL: right-click adds a point there, dragged while the button is held (Shift: on the curve, keeping its
            // level); the whole-curve commands are in the point menu
            AutomationPoint p;
            p.tick = snap (xToTick (e.x), ! e.mods.isAltDown());
            p.value = e.mods.isShiftDown() ? curveAt (*a, (double) p.tick) : yToValue (e.y);
            addAndDrag (p);
            return;
        }

        if (hit >= 0 && e.mods.isAltDown()) // FL: Alt + drag moves the point without snap
        {
            dragPoint = hit;
            return;
        }

        if (handle >= 0) // the tension handle: up / down bends the segment
        {
            tensionPoint = handle;
            rememberSegment (editedPoints (*a)[(std::size_t) handle]);
            tensionStart = editedPoints (*a)[(std::size_t) handle].tension;
            tensionSign = editedPoints (*a)[(std::size_t) handle + 1].value >= editedPoints (*a)[(std::size_t) handle].value ? -1.0f : 1.0f;
            return;
        }

        if (e.mods.isAltDown())
        {
            // segment under the mouse: the point on the left holds the tension
            tensionPoint = -1;
            const auto tick = xToTick (e.x);

            for (int i = 0; i + 1 < (int) editedPoints (*a).size(); ++i)
                if (tick >= (double) editedPoints (*a)[(std::size_t) i].tick && tick < (double) editedPoints (*a)[(std::size_t) i + 1].tick)
                    tensionPoint = i;

            if (tensionPoint >= 0)
            {
                rememberSegment (editedPoints (*a)[(std::size_t) tensionPoint]);
                tensionStart = editedPoints (*a)[(std::size_t) tensionPoint].tension;
                tensionSign = editedPoints (*a)[(std::size_t) tensionPoint + 1].value >= editedPoints (*a)[(std::size_t) tensionPoint].value ? -1.0f : 1.0f;
            }
            return;
        }

        if (hit >= 0)
        {
            dragPoint = hit;
            return;
        }

        AutomationPoint p;
        p.tick = snap (xToTick (e.x), ! e.mods.isShiftDown());
        p.value = yToValue (e.y);
        addAndDrag (p);
    }

    // FL: a new point takes the mode and tension last used in the editor (the segment that ends at it); the rest of
    // the segment it splits keeps its curve
    void addAndDrag (const AutomationPoint& p)
    {
        int added = -1;
        edit ([p, &added, m = automationPointMemory()] (Automation& x) { added = insertAutomationPoint (editedPoints (x), p, m.mode, m.tension); });
        dragPoint = added;
    }

    // the tension drag: FL's editor remembers the dragged segment's mode and tension for the next new point
    static void rememberSegment (const AutomationPoint& p)
    {
        automationPointMemory() = { p.mode < flcurve::modeCount ? (int) p.mode : 0, p.tension };
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto* a = current();

        if (a == nullptr)
            return;

        if (panning)
        {
            scroll = juce::jmax (0.0, panStart - e.getDistanceFromDragStartX() / pixelsPerTick);
            repaint();
            return;
        }

        if (tensionPoint >= 0 && tensionPoint < (int) editedPoints (*a).size())
        {
            const auto up = -(float) e.getDistanceFromDragStartY() / (flmods::ctrl (e.mods) ? 400.0f : 100.0f);
            const auto tension = juce::jlimit (-1.0f, 1.0f, tensionStart + tensionSign * up); // the segment follows the mouse
            automationPointMemory().tension = tension;
            edit ([i = tensionPoint, tension] (Automation& x) { setAutomationSegmentTension (editedPoints (x), i, tension); });
            return;
        }

        if (dragPoint < 0 || dragPoint >= (int) editedPoints (*a).size())
            return;

        // the point can't pass its neighbours (order doesn't change while dragging)
        const auto& points = editedPoints (*a);
        auto tick = snap (xToTick (e.x), ! e.mods.isShiftDown() && ! e.mods.isAltDown());
        const auto lo = dragPoint > 0 ? points[(std::size_t) dragPoint - 1].tick : 0;
        const auto hi = dragPoint + 1 < (int) points.size() ? points[(std::size_t) dragPoint + 1].tick : std::numeric_limits<std::int64_t>::max();
        tick = juce::jlimit (lo, hi, tick);
        auto value = yToValue (e.y);

        // locks: Shift pressed after the click keeps the time, Ctrl keeps the value
        if (e.mods.isShiftDown() && ! downMods.isShiftDown())
            tick = points[(std::size_t) dragPoint].tick;
        if (flmods::ctrl (e.mods) && ! flmods::ctrl (downMods))
            value = points[(std::size_t) dragPoint].value;

        edit ([i = dragPoint, tick, value] (Automation& x) {
            editedPoints (x)[(std::size_t) i].tick = tick;
            editedPoints (x)[(std::size_t) i].value = value;
        });
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        dragPoint = tensionPoint = -1;
        panning = false;
        updateCursor (e.mods);
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (flmods::ctrl (e.mods))
        {
            const auto anchor = xToTick (e.x);
            juce::ignoreUnused (anchor);
            const auto zoomed = juce::jlimit (4.0 / (4 * ppq), 600.0 / (4 * ppq), pixelsPerTick * gesture::wheelZoomFactor (wheel.deltaY, wheel.isSmooth));
            scroll = gesture::zoomAnchored (scroll, pixelsPerTick, zoomed, (double) juce::jmax (0, e.x - plotArea().getX()));
            pixelsPerTick = zoomed;
        }
        else
            scroll = juce::jmax (0.0, scroll - (wheel.deltaY + wheel.deltaX) * 300.0 / pixelsPerTick);

        repaint();
    }

private:
    juce::ModifierKeys downMods;

    juce::String valueText (float v) const
    {
        const auto* a = current();
        if (a != nullptr && a->lfo.on) // the LFO's amplitude
            return tr ("LFO amplitude") + " " + juce::String (juce::roundToInt (v * 100.0f)) + "%";
        return a != nullptr ? automationPointValueText (a->target, v) : juce::String();
    }

    void showPointMenu (int point)
    {
        // FL's point menu [RES FLEngine EEACPointMenu; the look: the envelope editor's menu, shot fl-lp-point-menu]:
        // Delete, the Mode block, then Copy value / Paste value / Type in value...; LYNX's own commands follow
        const auto* a = current();
        const auto value = editedPoints (*a)[(std::size_t) point].value;
        juce::PopupMenu menu;
        // FL's header is the point's place in the clip, "Point 2/14" (live check 190cq, shot curves190bw-01)
        menu.addSectionHeader (automationPointMenuHeader (point, (int) editedPoints (*a).size()));
        menu.addItem (tr ("Delete"), editedPoints (*a).size() > 1, false, [this, point] {
            edit ([point] (Automation& x) {
                if (editedPoints (x).size() > 1)
                    eraseAutomationPoint (editedPoints (x), point);
            });
        });
        addAutomationModeItems (menu, automationPointMode (editedPoints (*a), point), point > 0,
                                [this, point] (int mode) { edit ([point, mode] (Automation& x) { setAutomationPointMode (editedPoints (x), point, mode); }); });
        menu.addSeparator();
        menu.addItem (tr ("Copy value"), [value] { copiedAutomationValue() = value; });
        menu.addItem (tr ("Paste value"), copiedAutomationValue() >= 0.0f, false, [this, point] {
            edit ([point, v = copiedAutomationValue()] (Automation& x) {
                if (point < (int) editedPoints (x).size())
                    editedPoints (x)[(std::size_t) point].value = v;
            });
        });
        menu.addItem (tr ("Type in value..."), [this, point] { typeValue (point); });
        menu.addSeparator();
        menu.addItem (tr ("Straight segment (reset the curve)"), [this, point] {
            edit ([point] (Automation& x) { setAutomationSegmentTension (editedPoints (x), point, 0.0f); });
        });
        menu.addSubMenu (tr ("Whole curve"), curveMenu());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void typeValue (int point) { typeAutomationPointValue (engine, automation, point); }

    juce::PopupMenu curveMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (tr ("Scale the values into a range..."), [this] { rerange(); });
        menu.addItem (tr ("Flip the values upside down"), [this] {
            edit ([] (Automation& x) {
                for (auto& p : editedPoints (x))
                    p.value = 1.0f - p.value;
            });
        });
        menu.addItem (tr ("All segments straight"), [this] {
            edit ([] (Automation& x) {
                for (int i = 0; i < (int) editedPoints (x).size(); ++i)
                    setAutomationSegmentTension (editedPoints (x), i, 0.0f);
            });
        });
        return menu;
    }

    void rerange()
    {
        auto* window = new juce::AlertWindow (tr ("Scale the values into a range"), tr ("The lowest point goes to the minimum and the highest to the maximum (percent)."),
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("min", "0", tr ("Minimum"));
        window->addTextEditor ("max", "100", tr ("Maximum"));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result) {
                                     if (result != 1)
                                         return;
                                     const auto lo = juce::jlimit (0.0f, 1.0f, window->getTextEditorContents ("min").getFloatValue() / 100.0f);
                                     const auto hi = juce::jlimit (0.0f, 1.0f, window->getTextEditorContents ("max").getFloatValue() / 100.0f);
                                     edit ([lo, hi] (Automation& x) {
                                         if (editedPoints (x).empty())
                                             return;
                                         auto [mn, mx] = std::minmax_element (editedPoints (x).begin(), editedPoints (x).end(), [] (auto& a, auto& b) { return a.value < b.value; });
                                         const auto from = mn->value, span = mx->value - mn->value;
                                         for (auto& p : editedPoints (x))
                                             p.value = span > 1e-6f ? lo + (p.value - from) / span * (hi - lo) : lo;
                                     });
                                 }),
                                 true);
    }

    const Automation* current() const
    {
        const auto& song = engine.getSong();
        return automation >= 0 && automation < (int) song.automations.size() ? &song.automations[(std::size_t) automation] : nullptr;
    }

    void edit (const std::function<void (Automation&)>& fn)
    {
        engine.editSong ([this, &fn] (Song& s) {
            if (automation >= 0 && automation < (int) s.automations.size())
                fn (s.automations[(std::size_t) automation]);
        });
    }

    juce::Rectangle<int> plotArea() const
    {
        return getLocalBounds().withTrimmedTop (tabRowHeight + 18).withTrimmedBottom (lfoBarHeight() + 4).reduced (8, 4);
    }

    // --- the LFO panel (FL's channel settings "LFO" panel and the editor's LFO button; REA round 177b) ---------------
    static constexpr int tabRowHeight = 24;
    static int editorCaptionWidth() { return 120; }
    int lfoBarHeight() const
    {
        const auto* a = current();
        return a != nullptr && a->lfo.on ? 44 + lfoArrowRow : 0;
    }

    void editLfo (const std::function<void (AutomationLfo&)>& fn)
    {
        edit ([&fn] (Automation& x) { fn (x.lfo); });
    }

    void addLfoKnob (const juce::String& name, const juce::String& shortName, double lo, double hi, double initial,
                     std::function<juce::String (double)> text, std::function<double (const AutomationLfo&)> get,
                     std::function<void (AutomationLfo&, double)> set)
    {
        ui::ValueSpec spec;
        spec.name = name;
        spec.minimum = lo;
        spec.maximum = hi;
        spec.defaultValue = initial;
        spec.interval = 1.0;
        spec.toText = std::move (text);
        auto knob = std::make_unique<ctlui::Knob> (
            &engine, spec, [this, get] { const auto* a = current(); return a != nullptr ? get (a->lfo) : 0.0; },
            [this, set] (double v) { editLfo ([&set, v] (AutomationLfo& l) { set (l, v); }); });
        knob->shortName = shortName;
        addChildComponent (*knob);
        lfoKnobs.push_back (knob.get());
        ownedKnobs.push_back (std::move (knob));
    }

    void buildLfoPanel()
    {
        lfoButton.setButtonText (tr ("LFO"));
        lfoButton.setClickingTogglesState (false);
        lfoButton.setTooltip (tr ("LFO mode: a waveform shapes the clip's value; the editor then edits the LFO's amplitude"));
        lfoButton.onClick = [this] { editLfo ([] (AutomationLfo& l) { l.on = ! l.on; }); };
        addAndMakeVisible (lfoButton);

        auto plain = [] (double v) { return juce::String ((int) v); };
        addLfoKnob (tr ("LFO speed"), tr ("SPD"), 512, 65536, 45819, [] (double v) {
                auto beats = 1.0 / AutomationLfo::cyclesPerBeat ((int) v);
                if (std::abs (beats - std::round (beats)) < 0.005) // (FL's speeds for whole beats land a hair off)
                    beats = std::round (beats);
                return beats >= 1.0 ? tr ("{0} beats", { juce::String (beats, beats < 10.0 ? 2 : 1) })
                                    : "1/" + juce::String (1.0 / beats, 1) + " " + tr ("beat");
            },
            [] (const AutomationLfo& l) { return (double) l.speed; }, [] (AutomationLfo& l, double v) { l.speed = (int) v; });
        addLfoKnob (tr ("LFO tension"), tr ("TNS"), -128, 128, 0, plain, [] (const AutomationLfo& l) { return (double) l.tension; },
                    [] (AutomationLfo& l, double v) { l.tension = (int) v; });
        addLfoKnob (tr ("LFO skew"), tr ("SKEW"), -128, 128, 0, plain, [] (const AutomationLfo& l) { return (double) l.skew; },
                    [] (AutomationLfo& l, double v) { l.skew = (int) v; });
        addLfoKnob (tr ("LFO pulse width"), tr ("PW"), -112, 112, 0, plain, [] (const AutomationLfo& l) { return (double) l.pulseWidth; },
                    [] (AutomationLfo& l, double v) { l.pulseWidth = (int) v; });
        // FL's AMNT: the LFO's level (the clip's PHASE stays a passthrough value: FL shows no control for it)
        addLfoKnob (tr ("LFO amount"), tr ("AMNT"), -128, 128, 64, plain, [] (const AutomationLfo& l) { return (double) l.level; },
                    [] (AutomationLfo& l, double v) { l.level = (int) v; });

        mulButton.setButtonText (tr ("MUL"));
        mulButton.setTooltip (tr ("Multiply with envelope level (off: the LFO is added)"));
        mulButton.onClick = [this] { editLfo ([] (AutomationLfo& l) { l.add = ! l.add; }); };
        addChildComponent (mulButton);

        lfoOptions.setTooltip (tr ("LFO options"));
        lfoOptions.onClick = [this] {
            const auto* a = current();
            if (a == nullptr)
                return;
            const auto flags = a->lfo.flags;
            juce::PopupMenu menu;
            menu.addItem (tr ("Bipolar LFO tension"), true, (flags & 4) == 0, [this] { editLfo ([] (AutomationLfo& l) { l.flags ^= 4; }); });
            menu.addItem (tr ("Frozen LFO"), true, (flags & 8) != 0, [this] { // (FL sets the global phase with it)
                editLfo ([] (AutomationLfo& l) { l.flags = (l.flags & 8) != 0 ? (l.flags & ~8) : (l.flags | 8 | 2); });
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lfoOptions));
        };
        addChildComponent (lfoOptions);
    }

    void refreshLfoPanel()
    {
        const auto* a = current();
        const auto on = a != nullptr && a->lfo.on;
        lfoButton.setToggleState (on, juce::dontSendNotification);
        mulButton.setToggleState (a != nullptr && ! a->lfo.add, juce::dontSendNotification);
        if (on != lfoShown)
        {
            lfoShown = on;
            resized();
        }
        for (auto* k : lfoKnobs)
            k->repaint();
    }

    juce::ToggleButton lfoButton; // FL's "LFO" radio next to "Automation editor"
    static constexpr int lfoArrowRow = 16;
    LedToggle mulButton { "MUL" };
    juce::ArrowButton lfoOptions { "LFO options", 0.25f, theme::colour::textSecondary }; // FL's small arrow, pointing down
    std::vector<std::unique_ptr<ctlui::Knob>> ownedKnobs;
    std::vector<ctlui::Knob*> lfoKnobs;
    bool lfoShown = false;
    double tickToX (double tick) const { return plotArea().getX() + (tick - scroll) * pixelsPerTick; }
    double xToTick (double x) const { return juce::jmax (0.0, (x - plotArea().getX()) / pixelsPerTick + scroll); }
    float valueToY (float v) const { return (float) plotArea().getBottom() - v * (float) plotArea().getHeight(); }
    float yToValue (int y) const { return juce::jlimit (0.0f, 1.0f, (float) (plotArea().getBottom() - y) / (float) plotArea().getHeight()); }

    std::int64_t snap (double tick, bool on) const
    {
        const auto grid = on ? (double) ticksPerStep : 1.0;
        return (std::int64_t) std::round (tick / grid) * (std::int64_t) grid;
    }

    juce::Point<float> pointPosition (const AutomationPoint& p) const { return { (float) tickToX ((double) p.tick), valueToY (p.value) }; }

    // round 165: the tension handle in the middle of segment i → i+1 (x < 0: too narrow to show one)
    juce::Point<float> handlePosition (int i) const
    {
        const auto* a = current();
        if (a == nullptr || i < 0 || i + 1 >= (int) editedPoints (*a).size())
            return { -1.0f, -1.0f };
        const auto p0 = pointPosition (editedPoints (*a)[(std::size_t) i]), p1 = pointPosition (editedPoints (*a)[(std::size_t) i + 1]);
        if (p1.x - p0.x < 20.0f)
            return { -1.0f, -1.0f };
        const auto mid = 0.5 * (double) (editedPoints (*a)[(std::size_t) i].tick + editedPoints (*a)[(std::size_t) i + 1].tick);
        return { 0.5f * (p0.x + p1.x), valueToY (curveAt (*a, mid)) };
    }

    int handleAt (juce::Point<float> pos) const
    {
        const auto* a = current();
        if (a == nullptr)
            return -1;
        for (int i = 0; i + 1 < (int) editedPoints (*a).size(); ++i)
            if (const auto h = handlePosition (i); h.x >= 0.0f && h.getDistanceFrom (pos) < 7.0f)
                return i;
        return -1;
    }

    int pointAt (juce::Point<float> pos) const
    {
        const auto* a = current();

        if (a == nullptr)
            return -1;

        for (int i = (int) editedPoints (*a).size() - 1; i >= 0; --i)
            if (pointPosition (editedPoints (*a)[(std::size_t) i]).getDistanceFrom (pos) < 8.0f)
                return i;

        return -1;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (current() == nullptr) // automation removed: close
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Component> (getParentComponent())] {
                if (auto* w = dynamic_cast<juce::DocumentWindow*> (safe.getComponent()))
                    w->closeButtonPressed();
            });
            return;
        }

        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName (tr ("Automation - {0}", { juce::String::fromUTF8 (current()->name.c_str()) }));

        refreshLfoPanel();
        repaint();
    }

    void timerCallback() override
    {
        // position within the automation, if one of its clips is under the cursor
        auto tick = -1.0;
        const auto& song = engine.getSong();

        if (song.mode == PlayMode::song && engine.getTransport().playing.load())
        {
            const auto position = engine.songPlayheadTicks();

            for (const auto& c : song.playlist)
                if (c.kind == ClipKind::automation && c.automation == automation && position >= (double) c.start && position < (double) c.end())
                    tick = position - (double) c.start + (double) c.offset;
        }

        if (tick != playhead)
        {
            playhead = tick;
            repaint();
        }
    }

    AudioEngine& engine;
    int automation;
    double pixelsPerTick = 120.0 / (4 * ppq);
    double scroll = 0.0;
    int dragPoint = -1, hoverPoint = -1, tensionPoint = -1, hoverHandle = -1;
    bool panning = false;
    double panStart = 0.0;
    float tensionStart = 0.0f, tensionSign = 1.0f;
    double playhead = -1.0;
};

class AutomationWindow final : public juce::DocumentWindow
{
public:
    AutomationWindow (AudioEngine& engine, int index, const juce::String& title)
        : juce::DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new AutomationEditor (engine, index), true);
        setResizable (true, false);
        setResizeLimits (400, 200, 4000, 2000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        dockOrKeepAbove (*this); // FL's docked plugin window (round 190ev)
    }

    int getIndex() const { return static_cast<AutomationEditor*> (getContentComponent())->getIndex(); }

    void closeButtonPressed() override;
};

std::vector<std::unique_ptr<AutomationWindow>>& windows()
{
    static std::vector<std::unique_ptr<AutomationWindow>> open;
    return open;
}

void AutomationWindow::closeButtonPressed()
{
    auto* self = this;
    juce::MessageManager::callAsync ([self] {
        auto& open = windows();
        open.erase (std::remove_if (open.begin(), open.end(), [self] (auto& w) { return w.get() == self; }), open.end());
    });
}
} // namespace

juce::String automationPointValueText (const AutomationTarget& target, float v)
{
    return target.kind == AutomationTargetKind::tempo ? juce::String (tempoFromValue (v), 2) + " BPM" : juce::String (v * 100.0f, 1) + "%";
}

float& copiedAutomationValue()
{
    static float value = -1.0f;
    return value;
}

AutomationPointMemory& automationPointMemory()
{
    static AutomationPointMemory memory;
    return memory;
}

juce::String automationPointMenuHeader (int point, int count)
{
    return tr ("Point {0}/{1}", { juce::String (point + 1), juce::String (count) });
}

void addAutomationModeItems (juce::PopupMenu& menu, int current, bool enabled, std::function<void (int)> pick)
{
    // FL's order [RES FLEngine TFruityLoopsMainForm.EEACPointMenu: PtCurve10Menu, 2, 0, 7, 11, 1, 8, 12, 9, 3, 4, 5, 6]
    static const int order[] = { flcurve::smooth, flcurve::hold, flcurve::singleCurve, flcurve::singleCurve2, flcurve::singleCurve3,
                                 flcurve::doubleCurve, flcurve::doubleCurve2, flcurve::doubleCurve3, flcurve::halfSine, flcurve::stairs,
                                 flcurve::smoothStairs, flcurve::pulse, flcurve::wave };
    menu.addSectionHeader (tr ("Mode"));
    for (const auto mode : order)
    {
        juce::PopupMenu::Item item (tr (flcurve::modeName (mode)));
        item.setEnabled (enabled);
        item.setTicked (mode == current);
        item.setAction ([pick, mode] {
            automationPointMemory().mode = mode;
            pick (mode);
        });
        menu.addItem (item);
    }
}

void typeAutomationPointValue (AudioEngine& engine, int automation, int point)
{
    const auto& song = engine.getSong();
    if (automation < 0 || automation >= (int) song.automations.size() || point < 0 || point >= (int) song.automations[(std::size_t) automation].points.size())
        return;
    const auto& a = song.automations[(std::size_t) automation];
    const auto tempo = a.target.kind == AutomationTargetKind::tempo;
    const auto value = a.points[(std::size_t) point].value;
    auto* window = new juce::AlertWindow (tr ("Point value"), tempo ? tr ("BPM") : tr ("Percent (0 to 100)"), juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("value", tempo ? juce::String (tempoFromValue (value), 2) : juce::String (value * 100.0f, 1));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([&engine, automation, point, tempo, window] (int result) {
                                 if (result != 1)
                                     return;
                                 const auto typed = window->getTextEditorContents ("value").retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getDoubleValue();
                                 const auto v = juce::jlimit (0.0f, 1.0f, tempo ? valueFromTempo (typed) : (float) typed / 100.0f);
                                 engine.editSong ([automation, point, v] (Song& s) {
                                     if (automation < (int) s.automations.size() && point < (int) s.automations[(std::size_t) automation].points.size())
                                         s.automations[(std::size_t) automation].points[(std::size_t) point].value = v;
                                 });
                             }),
                             true);
}


juce::String describeAutomationTarget (AudioEngine& engine, const AutomationTarget& t)
{
    const auto& song = engine.getSong();
    const auto channelName = t.index >= 0 && t.index < (int) song.channels.size() ? nameOf (song.channels[(std::size_t) t.index].name) : juce::String ("?");
    const auto trackName = t.index >= 0 && t.index < (int) song.mixer.size() ? nameOf (song.mixer[(std::size_t) t.index].name) : juce::String ("?");

    switch (t.kind)
    {
        case AutomationTargetKind::channelVolume: return channelName + " - " + tr ("Volume");
        case AutomationTargetKind::channelPan: return channelName + " - " + tr ("Pan");
        case AutomationTargetKind::channelAdjust: // the Misc tab's Levels adjustment (round 190dz; FL's knob hints)
            return channelName + " - "
                   + (t.param == leveladjust::pan      ? tr ("Panning offset")
                      : t.param == leveladjust::volume ? tr ("Volume multiplier")
                      : t.param == leveladjust::modX   ? tr ("Modulation X offset")
                                                       : tr ("Modulation Y offset"));
        case AutomationTargetKind::mixerVolume: return trackName + " - " + tr ("Volume");
        case AutomationTargetKind::mixerPan: return trackName + " - " + tr ("Pan");

        case AutomationTargetKind::channelParam:
        case AutomationTargetKind::effectParam:
        {
            juce::String param = tr ("Param {0}", { juce::String (t.param + 1) });

            if (midictl::isMidiParam (t.param))
                param = midiParamLabel (t.param);
            else if (auto* plugin = pluginFor (engine, t))
            {
                if (t.param >= 0 && t.param < plugin->getParameters().size())
                    param = plugin->getParameters()[t.param]->getName (40);
            }
            else if (t.kind == AutomationTargetKind::channelParam && t.index >= 0 && t.index < (int) song.channels.size()
                     && song.channels[(std::size_t) t.index].instrument == InstrumentKind::controller
                     && (t.param == envelopeControllerModXParam || t.param == envelopeControllerModYParam))
                param = t.param == envelopeControllerModXParam ? tr ("Mod X") : tr ("Mod Y"); // (round 164f)
            else if (t.kind == AutomationTargetKind::channelParam && t.index >= 0 && t.index < (int) song.channels.size()
                     && song.channels[(std::size_t) t.index].instrument == InstrumentKind::dashboard) // (round 165)
                param = nameOf (surface::outputName (song.channels[(std::size_t) t.index].patcher.surface, t.param));
            else if (t.kind == AutomationTargetKind::channelParam && t.index >= 0 && t.index < (int) song.channels.size()
                     && song.channels[(std::size_t) t.index].instrument == InstrumentKind::voltage && t.param >= 0 && t.param < voltage::paramCount)
                param = voltageParamLabel (t.param);
            else if (t.kind == AutomationTargetKind::channelParam && t.index >= 0 && t.index < (int) song.channels.size()
                     && song.channels[(std::size_t) t.index].instrument == InstrumentKind::sampler && t.param >= 0
                     && t.param < samplerparam::count) // (issue #12)
                param = tr (samplerparam::name (t.param));
            else if (t.kind == AutomationTargetKind::channelParam && t.index >= 0 && t.index < (int) song.channels.size()
                     && song.channels[(std::size_t) t.index].instrument == InstrumentKind::midiOut && t.param >= 0
                     && t.param <= midiout::controlCount) // (round 167i)
            {
                const auto ms = midiout::stateOf (song.channels[(std::size_t) t.index].pluginState);
                const auto l = t.param == midiout::programParam ? std::string() : midiout::label (ms.controls[(std::size_t) (t.param - 1)]);
                param = t.param == midiout::programParam ? tr ("Patch") : l.empty() ? tr ("Control {0}", { juce::String (t.param) }) : utf8 (l.c_str());
            }
            else if (t.kind == AutomationTargetKind::effectParam && t.index < (int) song.mixer.size()
                     && t.slot < (int) song.mixer[(std::size_t) t.index].effects.size())
            {
                const auto& info = effectInfo (song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind);

                if (t.param >= 0 && t.param < (int) info.params.size())
                    param = utf8 (info.params[(std::size_t) t.param].name);
            }

            return (t.kind == AutomationTargetKind::channelParam ? channelName : trackName) + " - " + param;
        }

        case AutomationTargetKind::tempo: return tr ("Tempo");
        case AutomationTargetKind::vcaLevel: // (round 177a)
            return t.index >= 0 && t.index < (int) song.vcas.size() ? nameOf (song.vcas[(std::size_t) t.index].name) + " - " + tr ("VCA level")
                                                                   : tr ("VCA level");
        case AutomationTargetKind::mixerSend: // (issue #12)
        {
            const auto destination = t.slot >= 0 && t.slot < (int) song.mixer.size() ? nameOf (song.mixer[(std::size_t) t.slot].name) : juce::String ("?");
            return trackName + juce::String::fromUTF8 (" \xe2\x86\x92 ") + destination + " - " + (t.slot == 0 ? tr ("Level to Master") : tr ("Send level"));
        }
        case AutomationTargetKind::effectMix:
        {
            juce::String effect = "FX " + juce::String (t.slot + 1);
            if (t.index < (int) song.mixer.size() && t.slot >= 0 && t.slot < (int) song.mixer[(std::size_t) t.index].effects.size())
                effect = utf8 (effectInfo (song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind).name);
            return trackName + " - " + effect + " - " + tr ("Mix");
        }
        case AutomationTargetKind::none: break;
    }

    return tr ("(no target)");
}

juce::String targetValueTextAt (AudioEngine& engine, const AutomationTarget& target, float value)
{
    value = juce::jlimit (0.0f, 1.0f, value);
    if (target.kind == AutomationTargetKind::channelParam || target.kind == AutomationTargetKind::effectParam)
    {
        if (midictl::isMidiParam (target.param))
            return juce::String (juce::roundToInt (value * 127.0f));
        if (auto* plugin = pluginFor (engine, target))
        {
            if (target.param < 0 || target.param >= plugin->getParameters().size())
                return {};
            auto* p = plugin->getParameters()[target.param];
            auto text = p->getText (value, 40).trim();
            if (text.isNotEmpty())
                if (const auto label = p->getLabel().trim(); label.isNotEmpty() && ! text.endsWith (label))
                    text << " " << label;
            return text;
        }
    }
    return juce::String::fromUTF8 (lasttweak::valueText (engine.getSong(), target, value).c_str());
}

juce::String describeTargetValue (AudioEngine& engine, const AutomationTarget& target)
{
    const auto value = engine.currentTargetValue (target);
    if (value < 0.0f)
        return {};
    if (target.kind == AutomationTargetKind::channelParam || target.kind == AutomationTargetKind::effectParam)
    {
        if (midictl::isMidiParam (target.param))
            return juce::String (juce::roundToInt (value * 127.0f));
        if (auto* plugin = pluginFor (engine, target))
        {
            if (target.param < 0 || target.param >= plugin->getParameters().size())
                return {};
            auto* p = plugin->getParameters()[target.param];
            auto text = p->getCurrentValueAsText().trim();
            if (text.isEmpty())
                text = juce::String (value * 100.0f, 1) + "%";
            else if (const auto label = p->getLabel().trim(); label.isNotEmpty() && ! text.endsWith (label))
                text << " " << label;
            return text;
        }
    }
    const auto text = lasttweak::valueText (engine.getSong(), target, value);
    return text.empty() ? juce::String (juce::roundToInt (value * 100.0f)) + "%" : juce::String::fromUTF8 (text.c_str());
}

namespace
{
float valueToAutomate (AudioEngine& engine, const AutomationTarget& target)
{
    auto value = engine.getSong().targetValue (target);

    if (midictl::isMidiParam (target.param) && (target.kind == AutomationTargetKind::channelParam || target.kind == AutomationTargetKind::effectParam))
        value = midiControlValue (engine, target);
    else if (auto* plugin = pluginFor (engine, target); plugin != nullptr && target.param >= 0 && target.param < plugin->getParameters().size())
        value = plugin->getParameters()[target.param]->getValue();
    return value;
}
} // namespace

void createAutomationClip (AudioEngine& engine, const AutomationTarget& target)
{
    const auto value = valueToAutomate (engine, target);
    if (value < 0.0f)
        return;

    addAutomationClip (engine, target, describeAutomationTarget (engine, target).toStdString(), value);
}

void createAutomationLane (AudioEngine& engine, const AutomationTarget& target, int track)
{
    const auto value = valueToAutomate (engine, target);
    if (value < 0.0f)
        return;

    const auto name = describeAutomationTarget (engine, target).toStdString();
    int placed = -1;
    engine.editSong ([&] (Song& s) {
        placed = track >= 0 ? track : autolanes::trackFor (s, target);
        if (autolanes::addLane (s, placed, target, name, value) < 0)
            placed = -1;
    });
    if (placed >= 0)
        ui::postHint ({ tr ("Automation lane added under {0}", { tr ("Track {0}", { juce::String (placed + 1) }) }), juce::String::fromUTF8 (name.c_str()) }, 3000);
}

void addAutomationClip (AudioEngine& engine, const AutomationTarget& target, const std::string& name, float value)
{
    // FL's placement (round 177b, REA FUN_01191a50): the time selection, else the playback position (Options › General
    // "Create automation clips at the playback position", Song mode) or the song start; the first free track
    const auto playTick = engine.getSong().mode == PlayMode::song ? (std::int64_t) engine.getTransport().positionTicks.load() : 0;
    engine.editSong ([&] (Song& s) {
        const auto place = autoclipplace::forNewClip (s, autoclipplace::atPlayback(), playTick);

        Automation a;
        a.name = name;
        a.target = target;
        a.points = { { 0, value, 0.0f }, { place.length, value, 0.0f } };
        s.automations.push_back (std::move (a));

        Clip clip;
        clip.kind = ClipKind::automation;
        clip.automation = (int) s.automations.size() - 1;
        clip.track = place.track;
        clip.start = place.start;
        clip.length = place.length;
        s.playlist.push_back (clip);
    });

    openAutomationEditor (engine, (int) engine.getSong().automations.size() - 1);
}

void createUnlinkedAutomationClip (AudioEngine& engine)
{
    // (FL's "Automation Clip" generator: a clip linked to nothing yet; link it with its editor's / the control's menu)
    addAutomationClip (engine, AutomationTarget {}, "Automation Clip", 0.5f); // (FL's plugin name)
}

void openAutomationEditor (AudioEngine& engine, int index)
{
    if (index < 0 || index >= (int) engine.getSong().automations.size())
        return;

    for (auto& w : windows())
        if (w->getIndex() == index)
        {
            w->toFront (true);
            return;
        }

    windows().push_back (std::make_unique<AutomationWindow> (
        engine, index, tr ("Automation - {0}", { nameOf (engine.getSong().automations[(std::size_t) index].name) })));
}

void closeAutomationEditors()
{
    windows().clear();
    closeModulators();
}

std::unique_ptr<juce::Component> makeAutomationEditor (AudioEngine& engine, int automation)
{
    return std::make_unique<AutomationEditor> (engine, automation);
}

// --- modulators (round 177b; LYNX's own, relative: docs/analise/REA_rodada177b.md slice 4) ------------------------
namespace
{
const char* const modShapeNames[] = { TR_NOOP ("Sine"), TR_NOOP ("Triangle"), TR_NOOP ("Saw up"), TR_NOOP ("Saw down"),
                                      TR_NOOP ("Square"), TR_NOOP ("Random"), TR_NOOP ("Smooth random") };
// the synced rates offered (quarter notes per cycle)
constexpr double modRates[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };

juce::String modRateText (double beats)
{
    if (beats >= 4.0)
        return tr ("{0} bar(s)", { juce::String (beats / 4.0, beats / 4.0 == std::floor (beats / 4.0) ? 0 : 2) });
    return "1/" + juce::String (juce::roundToInt (4.0 / beats));
}

juce::String nextModulatorName (const Song& song)
{
    for (int n = 1;; ++n)
    {
        const auto name = "LFO " + std::to_string (n);
        if (std::none_of (song.modulators.begin(), song.modulators.end(), [&name] (const Modulator& m) { return m.name == name; }))
            return juce::String (name);
    }
}

// one modulator: its settings on the first line, its routes (target, depth, remove) under it
class ModulatorRow final : public juce::Component
{
public:
    ModulatorRow (AudioEngine& e, int i) : engine (e), index (i)
    {
        name.onClick = [this] { rename(); };
        name.setTooltip (tr ("Rename"));
        addAndMakeVisible (name);
        for (int s = 0; s < (int) std::size (modShapeNames); ++s)
            shape.addItem (tr (modShapeNames[s]), s + 1);
        shape.onChange = [this] { edit ([v = shape.getSelectedId() - 1] (Modulator& m) { m.shape = (ModShape) juce::jlimit (0, 6, v); }); };
        shape.setTooltip (tr ("Shape"));
        addAndMakeVisible (shape);
        sync.setButtonText (tr ("Sync"));
        sync.setTooltip (tr ("Tempo-synced (off: a rate in Hz)"));
        sync.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent.withAlpha (0.6f));
        sync.onClick = [this] { edit ([] (Modulator& m) { m.sync = ! m.sync; }); };
        addAndMakeVisible (sync);
        for (int r = 0; r < (int) std::size (modRates); ++r)
            rate.addItem (modRateText (modRates[r]), r + 1);
        rate.onChange = [this] {
            if (const auto id = rate.getSelectedId(); id > 0)
                edit ([v = modRates[id - 1]] (Modulator& m) { m.beats = v; });
        };
        rate.setTooltip (tr ("Cycle length"));
        addAndMakeVisible (rate);
        ui::ValueSpec hzSpec;
        hzSpec.name = tr ("Rate");
        hzSpec.minimum = 0.01;
        hzSpec.maximum = 20.0;
        hzSpec.defaultValue = 1.0;
        hzSpec.toText = [] (double v) { return juce::String (v, v < 1.0 ? 2 : 1) + " Hz"; };
        hz = std::make_unique<ctlui::Knob> (&engine, hzSpec, [this] { const auto* m = mod(); return m != nullptr ? m->hz : 1.0; },
                                            [this] (double v) { edit ([v] (Modulator& m) { m.hz = v; }); });
        addChildComponent (*hz);
        ui::ValueSpec phaseSpec;
        phaseSpec.name = tr ("Phase");
        phaseSpec.minimum = 0.0;
        phaseSpec.maximum = 359.0;
        phaseSpec.interval = 1.0;
        phaseSpec.toText = [] (double v) { return juce::String ((int) v) + juce::String::fromUTF8 ("\xc2\xb0"); };
        phase = std::make_unique<ctlui::Knob> (&engine, phaseSpec, [this] { const auto* m = mod(); return m != nullptr ? m->phase * 360.0 : 0.0; },
                                               [this] (double v) { edit ([v] (Modulator& m) { m.phase = (float) (v / 360.0); }); });
        addAndMakeVisible (*phase);
        unipolar.setButtonText (tr ("0..1"));
        unipolar.setTooltip (tr ("Unipolar: the modulator moves the value one way only (off: both ways)"));
        unipolar.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent.withAlpha (0.6f));
        unipolar.onClick = [this] { edit ([] (Modulator& m) { m.unipolar = ! m.unipolar; }); };
        addAndMakeVisible (unipolar);
        remove.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        remove.setTooltip (tr ("Delete the modulator"));
        remove.onClick = [this] {
            engine.editSong ([i = index] (Song& s) {
                if (i < (int) s.modulators.size())
                    s.modulators.erase (s.modulators.begin() + i);
            });
        };
        addAndMakeVisible (remove);
        refresh();
    }

    static constexpr int headHeight = 58, routeHeight = 52;
    int wantedHeight() const
    {
        const auto* m = mod();
        return headHeight + routeHeight * juce::jmax (1, m != nullptr ? (int) m->routes.size() : 0) + 8;
    }

    void refresh()
    {
        const auto* m = mod();
        if (m == nullptr)
            return;
        name.setButtonText (juce::String::fromUTF8 (m->name.c_str()));
        shape.setSelectedId ((int) m->shape + 1, juce::dontSendNotification);
        sync.setToggleState (m->sync, juce::dontSendNotification);
        unipolar.setToggleState (m->unipolar, juce::dontSendNotification);
        auto id = 0;
        for (int r = 0; r < (int) std::size (modRates); ++r)
            if (std::abs (modRates[r] - m->beats) < 1.0e-6)
                id = r + 1;
        rate.setSelectedId (id, juce::dontSendNotification);
        rate.setVisible (m->sync);
        hz->setVisible (! m->sync);

        // the routes: one depth knob each
        if (routeKnobs.size() != m->routes.size())
        {
            routeKnobs.clear();
            routeRemove.clear();
            for (int r = 0; r < (int) m->routes.size(); ++r)
            {
                ui::ValueSpec spec;
                spec.name = tr ("Depth");
                spec.minimum = -100.0;
                spec.maximum = 100.0;
                spec.interval = 1.0;
                spec.toText = [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + "%"; };
                auto knob = std::make_unique<ctlui::Knob> (&engine, spec,
                    [this, r] { const auto* x = mod(); return x != nullptr && r < (int) x->routes.size() ? x->routes[(std::size_t) r].depth * 100.0 : 0.0; },
                    [this, r] (double v) { edit ([r, v] (Modulator& x) { if (r < (int) x.routes.size()) x.routes[(std::size_t) r].depth = (float) (v / 100.0); }); });
                addAndMakeVisible (*knob);
                routeKnobs.push_back (std::move (knob));
                auto x = std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xc3\x97"));
                x->setTooltip (tr ("Remove this routing"));
                x->onClick = [this, r] { edit ([r] (Modulator& mm) { if (r < (int) mm.routes.size()) mm.routes.erase (mm.routes.begin() + r); }); };
                addAndMakeVisible (*x);
                routeRemove.push_back (std::move (x));
            }
            resized();
        }
        for (auto& k : routeKnobs)
            k->repaint();
        hz->repaint();
        phase->repaint();
        repaint();
    }

    void resized() override
    {
        auto head = getLocalBounds().removeFromTop (headHeight).reduced (6, 4);
        name.setBounds (head.removeFromLeft (90).withSizeKeepingCentre (90, 24));
        head.removeFromLeft (6);
        shape.setBounds (head.removeFromLeft (120).withSizeKeepingCentre (120, 24));
        head.removeFromLeft (6);
        sync.setBounds (head.removeFromLeft (48).withSizeKeepingCentre (48, 24));
        head.removeFromLeft (6);
        const auto rateArea = head.removeFromLeft (80);
        rate.setBounds (rateArea.withSizeKeepingCentre (80, 24));
        hz->setBounds (rateArea);
        head.removeFromLeft (6);
        phase->setBounds (head.removeFromLeft (52));
        head.removeFromLeft (6);
        unipolar.setBounds (head.removeFromLeft (44).withSizeKeepingCentre (44, 24));
        remove.setBounds (head.removeFromRight (26).withSizeKeepingCentre (26, 24));
        head.removeFromRight (8);
        scope = head.reduced (0, 4);

        auto routes = getLocalBounds().withTrimmedTop (headHeight).reduced (6, 0);
        for (std::size_t r = 0; r < routeKnobs.size(); ++r)
        {
            auto row = routes.removeFromTop (routeHeight);
            routeRemove[r]->setBounds (row.removeFromRight (24).withSizeKeepingCentre (24, 20));
            routeKnobs[r]->setBounds (row.removeFromRight (52));
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), radius::md);
        const auto* m = mod();
        if (m == nullptr)
            return;

        // one cycle of the shape, and where it is now
        if (! scope.isEmpty())
        {
            const auto r = scope.toFloat();
            g.setColour (colour::bg0);
            g.fillRoundedRectangle (r, radius::sm);
            juce::Path p;
            for (int x = 0; x <= (int) r.getWidth(); ++x)
            {
                const auto v = m->output ((double) x / juce::jmax (1.0f, r.getWidth()));
                const auto y = r.getBottom() - 3.0f - (m->unipolar ? v : (v + 1.0f) * 0.5f) * (r.getHeight() - 6.0f);
                if (x == 0)
                    p.startNewSubPath (r.getX() + (float) x, y);
                else
                    p.lineTo (r.getX() + (float) x, y);
            }
            g.setColour (colour::accent);
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }

        // the routes' targets
        g.setFont (font (type::small));
        auto routes = getLocalBounds().withTrimmedTop (headHeight).reduced (12, 0);
        if (m->routes.empty())
        {
            g.setColour (colour::textSecondary);
            g.drawText (tr ("No target yet: right-click any LYNX knob > Modulate > {0}", { juce::String::fromUTF8 (m->name.c_str()) }),
                        routes.removeFromTop (routeHeight), juce::Justification::centredLeft);
            return;
        }
        for (const auto& route : m->routes)
        {
            g.setColour (Modulator::canModulate (engine.getSong(), route.target) ? colour::textPrimary : colour::textDisabled);
            g.drawText (describeAutomationTarget (engine, route.target), routes.removeFromTop (routeHeight).withTrimmedRight (80),
                        juce::Justification::centredLeft);
        }
    }

private:
    const Modulator* mod() const
    {
        const auto& song = engine.getSong();
        return index >= 0 && index < (int) song.modulators.size() ? &song.modulators[(std::size_t) index] : nullptr;
    }

    void edit (const std::function<void (Modulator&)>& fn)
    {
        engine.editSong ([this, &fn] (Song& s) {
            if (index >= 0 && index < (int) s.modulators.size())
                fn (s.modulators[(std::size_t) index]);
        });
    }

    void rename()
    {
        const auto* m = mod();
        if (m == nullptr)
            return;
        auto* window = new juce::AlertWindow (tr ("Rename"), {}, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", juce::String::fromUTF8 (m->name.c_str()));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ModulatorRow> safe (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result) {
                                     if (result != 1 || safe == nullptr)
                                         return;
                                     const auto text = window->getTextEditorContents ("name").trim().toStdString();
                                     if (! text.empty())
                                         safe->edit ([&text] (Modulator& m) { m.name = text; });
                                 }),
                                 true);
        keepAboveMainWindow (*window);
    }

    AudioEngine& engine;
    int index;
    juce::TextButton name, sync, unipolar, remove;
    juce::ComboBox shape, rate;
    std::unique_ptr<ctlui::Knob> hz, phase;
    std::vector<std::unique_ptr<ctlui::Knob>> routeKnobs;
    std::vector<std::unique_ptr<juce::TextButton>> routeRemove;
    juce::Rectangle<int> scope;
};

class ModulatorsPanel final : public juce::Component,
                              private juce::ChangeListener,
                              private juce::Timer
{
public:
    explicit ModulatorsPanel (AudioEngine& e) : engine (e)
    {
        add.setButtonText (tr ("+ Add modulator"));
        add.onClick = [this] { addModulator (engine); };
        addAndMakeVisible (add);
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        engine.addChangeListener (this);
        rebuild();
        setSize (720, 360);
        startTimerHz (20);
    }
    ~ModulatorsPanel() override { engine.removeChangeListener (this); }

    // a new modulator (synced sine, one bar), optionally routed to `target`; returns its index
    static int addModulator (AudioEngine& engine, std::optional<AutomationTarget> target = {})
    {
        int index = -1;
        const auto name = nextModulatorName (engine.getSong()).toStdString();
        engine.editSong ([&] (Song& s) {
            Modulator m;
            m.name = name;
            m.seed = (std::uint32_t) s.modulators.size() + 1;
            if (target)
                m.routes.push_back ({ *target, 0.25f });
            s.modulators.push_back (std::move (m));
            index = (int) s.modulators.size() - 1;
        });
        return index;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg1);
        if (rows.empty())
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::body));
            g.drawFittedText (tr ("Modulators move LYNX knobs around their own value (an automation clip on the same knob still sets that value)."),
                              getLocalBounds().withTrimmedTop (44).reduced (16), juce::Justification::centredTop, 3);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        add.setBounds (area.removeFromTop (28).removeFromLeft (160));
        area.removeFromTop (6);
        viewport.setBounds (area);
        layoutList();
    }

private:
    void layoutList()
    {
        auto y = 0;
        const auto w = viewport.getWidth() - viewport.getScrollBarThickness();
        for (auto& r : rows)
        {
            r->setBounds (0, y, w, r->wantedHeight());
            y += r->wantedHeight() + 6;
        }
        list.setSize (w, y);
    }

    void rebuild()
    {
        const auto count = (int) engine.getSong().modulators.size();
        if ((int) rows.size() != count)
        {
            rows.clear();
            for (int i = 0; i < count; ++i)
            {
                rows.push_back (std::make_unique<ModulatorRow> (engine, i));
                list.addAndMakeVisible (*rows.back());
            }
        }
        for (auto& r : rows)
            r->refresh();
        layoutList();
        repaint();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void timerCallback() override
    {
        for (auto& r : rows)
            r->repaint();
    }

    AudioEngine& engine;
    juce::TextButton add;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<ModulatorRow>> rows;
};

class ModulatorsWindow final : public juce::DocumentWindow
{
public:
    explicit ModulatorsWindow (AudioEngine& engine) : juce::DocumentWindow (tr ("Modulators"), theme::colour::bg1, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ModulatorsPanel (engine), true);
        setResizable (true, false);
        setResizeLimits (560, 200, 4000, 2000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }
    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([] { modulatorsWindow().reset(); });
    }
    static std::unique_ptr<ModulatorsWindow>& modulatorsWindow()
    {
        static std::unique_ptr<ModulatorsWindow> window;
        return window;
    }
};
} // namespace

void openModulators (AudioEngine& engine)
{
    auto& window = ModulatorsWindow::modulatorsWindow();
    if (window != nullptr)
        window->toFront (true);
    else
        window = std::make_unique<ModulatorsWindow> (engine);
}

void closeModulators()
{
    ModulatorsWindow::modulatorsWindow().reset();
}

std::unique_ptr<juce::Component> makeModulatorsPanel (AudioEngine& engine)
{
    return std::make_unique<ModulatorsPanel> (engine);
}

void addModulationItems (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target)
{
    const auto& song = engine.getSong();
    if (! Modulator::canModulate (song, target))
        return;
    juce::PopupMenu sub;
    for (int m = 0; m < (int) song.modulators.size(); ++m)
    {
        const auto& mod = song.modulators[(std::size_t) m];
        const auto route = std::find_if (mod.routes.begin(), mod.routes.end(), [&] (const ModRoute& r) { return r.target == target; });
        const auto routed = route != mod.routes.end();
        auto label = juce::String::fromUTF8 (mod.name.c_str());
        if (routed)
            label << "  (" << (route->depth > 0 ? "+" : "") << juce::roundToInt (route->depth * 100.0f) << "%)";
        sub.addItem (label, true, routed, [&engine, target, m, routed] {
            engine.editSong ([target, m, routed] (Song& s) {
                if (m >= (int) s.modulators.size())
                    return;
                auto& routes = s.modulators[(std::size_t) m].routes;
                if (routed)
                    routes.erase (std::remove_if (routes.begin(), routes.end(), [&] (const ModRoute& r) { return r.target == target; }), routes.end());
                else
                    routes.push_back ({ target, 0.25f });
            });
        });
    }
    if (! song.modulators.empty())
        sub.addSeparator();
    sub.addItem (tr ("New modulator (LFO)"), [&engine, target] {
        ModulatorsPanel::addModulator (engine, target);
        openModulators (engine);
    });
    sub.addItem (tr ("Modulators..."), [&engine] { openModulators (engine); });
    menu.addSeparator();
    menu.addSubMenu (tr ("Modulate"), sub);
}

std::vector<ControllerOutput> controllerOutputs (const Song& song)
{
    std::vector<ControllerOutput> out;
    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        const auto& ch = song.channels[(std::size_t) c];
        if (ch.instrument == InstrumentKind::dashboard) // round 164: one output per control
        {
            const auto outputs = surface::firstOutput (ch.patcher.surface, (int) ch.patcher.surface.size());
            for (int k = 0; k < outputs; ++k)
                out.push_back ({ -1, 0, k, c, nameOf (ch.name) + " - " + nameOf (surface::outputName (ch.patcher.surface, k)) });
            continue;
        }
        if (ch.instrument != InstrumentKind::controller)
            continue;
        const auto name = nameOf (ch.name);
        if (parseKeyboardController (ch.controllerState))
        {
            out.push_back ({ -1, 0, 0, c, name + " - " + tr ("Key value") });
            continue;
        }
        for (int k = 0; k < 8; ++k)
            out.push_back ({ -1, 0, k, c, name + " - " + tr ("Output {0}", { juce::String (k + 1) }) });
    }
    for (int t = 0; t < (int) song.mixer.size(); ++t)
        for (int s = 0; s < (int) song.mixer[(std::size_t) t].effects.size(); ++s)
        {
            const auto kind = song.mixer[(std::size_t) t].effects[(std::size_t) s].kind;
            const auto track = t == 0 ? tr ("Master") : nameOf (song.mixer[(std::size_t) t].name);
            const auto where = (track.isNotEmpty() ? track : tr ("Insert {0}", { juce::String (t) })) + " (" + juce::String (s + 1) + ")";
            const auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) s];
            if (kind == EffectKind::formulaController)
                out.push_back ({ t, s, 0, -1, tr ("Formula Controller") + " " + where });
            else if (xy::isXyController (kind)) // round 164: X, Y (, Z)
            {
                static const char* axes[] = { "X", "Y", "Z" };
                for (int a = 0; a < xy::axesOf (kind); ++a)
                    out.push_back ({ t, s, a, -1, juce::String (effectInfo (kind).name) + " " + where + " - " + axes[a] });
            }
            else if (kind == EffectKind::controlSurface) // round 164: one output per control
            {
                const auto outputs = surface::firstOutput (fx.patcher.surface, (int) fx.patcher.surface.size());
                for (int k = 0; k < outputs; ++k)
                    out.push_back ({ t, s, k, -1, "Control Surface " + where + " - " + nameOf (surface::outputName (fx.patcher.surface, k)) });
            }
            else if (kind == EffectKind::peakController)
            {
                out.push_back ({ t, s, 1, -1, tr ("Peak Controller") + " " + where + " - " + tr ("Peak") });
                out.push_back ({ t, s, 2, -1, tr ("Peak Controller") + " " + where + " - " + tr ("LFO") });
                out.push_back ({ t, s, 0, -1, tr ("Peak Controller") + " " + where + " - " + tr ("Peak + LFO") });
            }
        }
    return out;
}

void linkToController (AudioEngine& engine, const AutomationTarget& target, const ControllerOutput& source)
{
    const auto name = describeAutomationTarget (engine, target).toStdString() + " <- " + source.label.toStdString();
    engine.editSong ([&] (Song& s) {
        // one controller per target (FL's Link dialog replaces the old link)
        for (int i = (int) s.automations.size() - 1; i >= 0; --i)
            if (s.automations[(std::size_t) i].isLive() && s.automations[(std::size_t) i].target == target)
                s.removeAutomation (i);
        Automation a;
        a.name = name;
        a.target = target;
        a.source.track = source.track;
        a.source.slot = source.slot;
        a.source.output = source.output;
        a.source.channel = source.channel;
        s.automations.push_back (std::move (a));
    });
}

void addControllerLinkItems (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target)
{
    const auto& song = engine.getSong();
    const auto outputs = controllerOutputs (song);
    const auto current = std::find_if (song.automations.begin(), song.automations.end(),
                                       [&] (const Automation& a) { return a.isLive() && a.target == target; });
    juce::PopupMenu sub;
    for (const auto& o : outputs)
    {
        const auto ticked = current != song.automations.end() && current->source.track == o.track && current->source.slot == o.slot
                            && current->source.output == o.output && current->source.channel == o.channel;
        sub.addItem (o.label, true, ticked, [&engine, target, o] { linkToController (engine, target, o); });
    }
    if (outputs.empty())
        sub.addItem (tr ("(add an Envelope, Formula or Peak Controller first)"), false, false, [] {});
    if (current != song.automations.end())
    {
        sub.addSeparator();
        sub.addItem (tr ("Remove controller link"), [&engine, target] {
            engine.editSong ([target] (Song& s) {
                for (int i = (int) s.automations.size() - 1; i >= 0; --i)
                    if (s.automations[(std::size_t) i].isLive() && s.automations[(std::size_t) i].target == target)
                        s.removeAutomation (i);
            });
        });
    }
    menu.addSubMenu (tr ("Link to internal controller"), sub);
}

std::function<void (const AutomationTarget&)> onEditEventsInPianoRoll;

void addAutomationItem (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target, bool headers)
{
    if (headers)
        menu.addSectionHeader (tr ("Automation"));
    // (the tempo too, issue #86: "Edit events" as in FL; "Edit events in piano roll" is a LYNX addition, FL greyed it in our tests)
    menu.addItem (tr ("Edit events in the pattern"), [&engine, target] { openEventEditor (engine, target); });
    if (onEditEventsInPianoRoll)
        menu.addItem (tr ("Edit events in piano roll"), [target] {
            if (onEditEventsInPianoRoll)
                onEditEventsInPianoRoll (target);
        });
    // FL's initialized controls (REA round 146): the value the song resets this control to whenever it starts
    const auto initial = engine.getSong().findInitialValue (target);
    menu.addItem (tr ("Init song with this position"), engine.currentTargetValue (target) >= 0.0f, initial >= 0, [&engine, target] {
        const auto value = engine.currentTargetValue (target);
        if (value >= 0.0f)
            engine.editSong ([target, value] (Song& s) { s.setInitialValue (target, value); });
    });
    menu.addItem (tr ("Delete initial value"), initial >= 0, false, [&engine, target] {
        engine.editSong ([target] (Song& s) { s.removeInitialValue (target); });
    });
    menu.addItem (tr ("Create automation clip"), [&engine, target] { createAutomationClip (engine, target); });
    if (autolanes::shown()) // round 177b: only while Playlist › Automation lanes is on (FL's menu is unchanged otherwise)
        menu.addItem (tr ("Automate in lane"), [&engine, target] { createAutomationLane (engine, target); });
    {
        // FL's "Edit automation clip..." (the clip that automates this control)
        const auto& automations = engine.getSong().automations;
        int clip = -1;
        for (int a = 0; a < (int) automations.size(); ++a)
            if (automations[(std::size_t) a].target == target && ! automations[(std::size_t) a].isLive())
                clip = a;
        menu.addItem (tr ("Edit automation clip..."), clip >= 0, false, [&engine, clip] { openAutomationEditor (engine, clip); });
    }
    if (headers)
        menu.addSectionHeader (tr ("Remote control"));
    menu.addItem (tr ("Link to MIDI controller (move a knob on the controller)"), [&engine, target] {
        engine.learnMidiController (target);
    });
    // FL's "Link to controller..." (round 190bt): the Remote control settings window, where the link is seen and
    // edited (port, channel, controller, formula, smoothing); the item above stays the one-step way
    menu.addItem (tr ("Link to controller..."), [&engine, target] { openRemoteControlSettings (engine, { target }, false); });
    // FL's "Override global link..." (form "Global link settings"): a link for every project, kept in the settings
    menu.addItem (tr ("Override global link... (for all projects: move a knob on the controller)"), [&engine, target] {
        engine.learnGlobalLink (target);
    });
    menu.addItem (tr ("Override global link..."), [&engine, target] { openRemoteControlSettings (engine, { target }, true); });

    if (target.kind != AutomationTargetKind::tempo)
        addControllerLinkItems (menu, engine, target);

    const auto& links = engine.getSong().midiLinks;
    const auto linked = std::find_if (links.begin(), links.end(), [&] (const MidiLink& l) { return l.target == target; });

    if (linked != links.end())
        menu.addItem (tr ("Unlink controller (CC {0})", { juce::String (linked->cc) }), [&engine, target] {
            engine.editSong ([target] (Song& s) {
                s.midiLinks.erase (std::remove_if (s.midiLinks.begin(), s.midiLinks.end(), [&] (const MidiLink& l) { return l.target == target; }),
                                   s.midiLinks.end());
            });
        });
    if (const auto global = engine.globalLinkOf (target))
        menu.addItem (tr ("Remove global link (CC {0})", { juce::String (global->cc) }), [&engine, target] { engine.removeGlobalLink (target); });

    addModulationItems (menu, engine, target); // round 177b: LYNX's modulators (after FL's items; LYNX's own controls only)
}

juce::String midiParamLabel (int param)
{
    if (midictl::isCc (param))
    {
        const auto cc = param - midictl::ccBase;
        const juce::String standard = midictl::ccName (cc);
        return tr ("MIDI CC #{0}", { juce::String (cc) }) + (standard.isEmpty() ? juce::String() : " (" + tr (standard) + ")");
    }
    if (midictl::isPressure (param))
        return tr ("MIDI Channel {0} Aftertouch", { juce::String (param - midictl::pressureBase + 1) });
    if (midictl::isPitchBend (param))
        return tr ("MIDI Channel {0} Pitch Bend", { juce::String (param - midictl::pitchBendBase + 1) });
    return {};
}

void addMidiParameterItems (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& base)
{
    const auto item = [&] (juce::PopupMenu& into, int param) {
        auto target = base;
        target.param = param;
        juce::PopupMenu sub;
        addAutomationItem (sub, engine, target);
        into.addSubMenu (midiParamLabel (param), sub);
    };

    juce::PopupMenu midi;
    for (int group = 0; group < 4; ++group) // CC 0-31, 32-63, 64-95, 96-127
    {
        juce::PopupMenu ccs;
        for (int cc = group * 32; cc < group * 32 + 32; ++cc)
            item (ccs, midictl::ccBase + cc);
        midi.addSubMenu (tr ("MIDI CC {0}-{1}", { juce::String (group * 32), juce::String (group * 32 + 31) }), ccs);
    }
    // the ones people reach for first, without digging
    juce::PopupMenu common;
    for (int cc : { 1, 7, 10, 11, 64, 66, 67, 74 })
        item (common, midictl::ccBase + cc);
    midi.addSubMenu (tr ("Common controllers"), common);

    juce::PopupMenu pressure, bend;
    for (int c = 0; c < 16; ++c)
    {
        item (pressure, midictl::pressureBase + c);
        item (bend, midictl::pitchBendBase + c);
    }
    midi.addSubMenu (tr ("Channel aftertouch"), pressure);
    midi.addSubMenu (tr ("Pitch bend"), bend);
    menu.addSubMenu (tr ("Automate MIDI controller (CC, aftertouch, pitch bend)"), midi);
}

void addPluginParameterItems (juce::PopupMenu& menu, AudioEngine& engine, juce::AudioPluginInstance& plugin,
                              const AutomationTarget& base)
{
    juce::PopupMenu params;
    const auto& list = plugin.getParameters();
    const auto count = juce::jmin (list.size(), midictl::ccBase);

    // a long list (VSL's pianos expose ~950) goes into pages of 64, so the menu fits on the screen
    constexpr int page = 64;
    for (int first = 0; first < count; first += page)
    {
        juce::PopupMenu pageMenu;
        auto& into = count > page ? pageMenu : params;
        for (int i = first; i < juce::jmin (count, first + page); ++i)
        {
            auto target = base;
            target.param = i;
            juce::PopupMenu param;
            addAutomationItem (param, engine, target);
            into.addSubMenu (juce::String (i + 1) + ". " + list[i]->getName (60), param);
        }
        if (count > page)
            params.addSubMenu (tr ("Parameters {0}-{1}", { juce::String (first + 1), juce::String (juce::jmin (count, first + page)) }), pageMenu);
    }

    if (list.isEmpty())
        params.addItem (tr ("(the plugin exposes no parameters)"), false, false, [] {});

    menu.addSubMenu (tr ("Automate plugin parameter"), params);
    addMidiParameterItems (menu, engine, base);
}

void addPatcherParameterItems (juce::PopupMenu& menu, AudioEngine& engine, const PatcherDoc& doc, const AutomationTarget& base)
{
    juce::PopupMenu params;
    for (int k = 0; k < (int) doc.published.size(); ++k)
    {
        const auto& p = doc.published[(std::size_t) k];
        if (p.node < 0 && p.node != patchSurface) // (a rack's macros and selector are Surface controls, round 183c)
            continue;
        auto target = base;
        target.param = k;
        juce::PopupMenu item;
        addAutomationItem (item, engine, target);
        params.addSubMenu (juce::String (k + 1) + ". " + juce::String::fromUTF8 (p.name.c_str()), item);
    }
    if (params.getNumItems() > 0)
        menu.addSubMenu (tr ("Automate Patcher parameter"), params);
}
} // namespace lynx
