#include "ui/FlMods.h"
#include "ui/EventEditor.h"
#include "core/MidiControls.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>

#include "core/EventEdit.h"
#include "core/MidiImport.h"
#include "engine/FlStudio.h"
#include "engine/MidiFileIo.h"
#include "core/PianoRollTools.h"
#include "ui/FileChoosers.h"
#include "ui/AutomationUi.h"
#include "ui/Dialogs.h"
#include "ui/MidiClipboard.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
bool sameTarget (const AutomationTarget& a, const AutomationTarget& b) noexcept
{
    return a == b;
}

bool bipolarTarget (const AutomationTarget& t) noexcept
{
    return t.kind == AutomationTargetKind::channelPan || t.kind == AutomationTargetKind::mixerPan
        || (t.kind == AutomationTargetKind::channelAdjust && t.param != leveladjust::volume) // (Levels adjustment offsets, round 190dz)
        || ((t.kind == AutomationTargetKind::channelParam || t.kind == AutomationTargetKind::effectParam) && midictl::isPitchBend (t.param));
}

class EventEditor final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener,
                          private juce::Timer
{
public:
    EventEditor (AudioEngine& e, const AutomationTarget& t)
        : engine (e), target (t)
    {
        engine.addChangeListener (this);

        title.setFont (theme::font (theme::type::title, true));
        addAndMakeVisible (title);
        addAndMakeVisible (targetBox);
        targetBox.onChange = [this] {
            const auto i = targetBox.getSelectedItemIndex();
            if (i >= 0 && i < (int) targets.size())
            {
                target = targets[(std::size_t) i];
                selectionStart = selectionEnd = -1;
                targetChanged();
                repaint();
            }
        };

        const std::pair<juce::TextButton*, const char*> tools[] = { { &drawButton, TR_NOOP ("Draw") }, { &lineButton, TR_NOOP ("Line") },
                                                                    { &selectButton, TR_NOOP ("Select") } };
        for (const auto& [b, name] : tools)
        {
            b->setButtonText (tr (name));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (8301);
            b->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
            addAndMakeVisible (*b);
        }

        drawButton.setToggleState (true, juce::dontSendNotification);
        drawButton.setTooltip (tr ("Draw: drag to set the values; right-drag erases"));
        lineButton.setTooltip (tr ("Line: drag a straight ramp"));
        selectButton.setTooltip (tr ("Select: drag a time range for the tools below"));

        gridLabel.setText (tr ("Snap"), juce::dontSendNotification);
        addAndMakeVisible (gridLabel);
        gridBox.addItemList ({ tr ("1/2 step"), tr ("1 step"), tr ("1/2 beat"), tr ("1 beat"), tr ("1 bar") }, 1);
        gridBox.setSelectedItemIndex (1, juce::dontSendNotification);
        gridBox.onChange = [this] { repaint(); };
        addAndMakeVisible (gridBox);

        // selection tools
        for (auto* l : { &scaleLabel, &offsetLabel, &randomLabel, &lfoLabel })
        {
            l->setFont (theme::font (theme::type::small));
            l->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            addAndMakeVisible (*l);
        }
        scaleLabel.setText (tr ("Scale"), juce::dontSendNotification);
        offsetLabel.setText (tr ("Offset"), juce::dontSendNotification);
        randomLabel.setText (tr ("Randomise"), juce::dontSendNotification);
        lfoLabel.setText ("LFO", juce::dontSendNotification);

        setupSlider (scaleSlider, 0.0, 2.0, 1.0, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; });
        setupSlider (offsetSlider, -1.0, 1.0, 0.0, [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%"; });
        setupSlider (amountSlider, 0.0, 0.5, 0.1, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; });

        scaleSlider.onValueChange = offsetSlider.onValueChange = [this] { previewScale(); };
        scaleSlider.onDragStart = offsetSlider.onDragStart = [this] { beginGesture(); };
        scaleSlider.onDragEnd = offsetSlider.onDragEnd = [this] {
            commitGesture();
            const juce::ScopedValueSetter<bool> quiet (resettingSliders, true);
            scaleSlider.setValue (1.0, juce::dontSendNotification);
            offsetSlider.setValue (0.0, juce::dontSendNotification);
        };

        randomButton.setButtonText (tr ("Apply"));
        randomButton.onClick = [this] {
            ++seed;
            edit ([this] (PatternEvents& lane, std::int64_t from, std::int64_t to) {
                eventedit::randomize (lane, from, to, (float) amountSlider.getValue(), seed);
            });
        };
        addAndMakeVisible (randomButton);

        shapeBox.addItemList ({ tr ("Sine"), tr ("Triangle"), tr ("Square") }, 1);
        shapeBox.setSelectedItemIndex (0, juce::dontSendNotification);
        addAndMakeVisible (shapeBox);
        periodBox.addItemList ({ tr ("1 beat"), tr ("2 beats"), tr ("1 bar"), tr ("2 bars"), tr ("4 bars") }, 1);
        periodBox.setSelectedItemIndex (2, juce::dontSendNotification);
        addAndMakeVisible (periodBox);
        lfoButton.setButtonText (tr ("Apply"));
        lfoButton.onClick = [this] { applyLfo(); };
        addAndMakeVisible (lfoButton);

        clearButton.setButtonText (tr ("Clear"));
        clearButton.setTooltip (tr ("Remove the events of the selection (or of the whole pattern)"));
        clearButton.onClick = [this] {
            edit ([] (PatternEvents& lane, std::int64_t from, std::int64_t to) { eventedit::erase (lane, from, to); });
        };
        addAndMakeVisible (clearButton);

        // FL's "Convert to automation" (round 165l): the lane becomes an automation clip in the Playlist
        toAutoButton.setButtonText (tr ("To automation..."));
        toAutoButton.setTooltip (tr ("Turn these events into an automation clip (the events are removed from the pattern)"));
        toAutoButton.onClick = [this] { showConvertToAutomation(); };
        addAndMakeVisible (toAutoButton);

        // FL's event editor File menu: Open automation / Save automation as (issue #86, seen in FL 2026): an event .fsc of the shown target
        fileButton.setButtonText (tr ("File"));
        fileButton.setTooltip (tr ("Open or save the events of this control as a .fsc file"));
        fileButton.onClick = [this] { showFileMenu(); };
        addAndMakeVisible (fileButton);

        valueBar.addListener (this);
        addChildComponent (valueBar);
        rebuildTargets();
        setSize (980, 400);
        targetChanged();
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    ~EventEditor() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg2);

        const auto& song = engine.getSong();
        const auto& pattern = currentPattern();
        const auto area = plotArea();
        const auto length = patternLength();
        const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);

        g.setColour (colour::bg0);
        g.fillRect (area.withTop (area.getY() - rulerHeight));

        // beyond the pattern's end
        const auto endX = (int) tickToX ((double) length);
        if (endX < area.getRight())
        {
            g.setColour (colour::bg2.withAlpha (0.75f));
            g.fillRect (area.withLeft (juce::jmax (area.getX(), endX)));
        }

        // grid: steps, beats, bars (with numbers on the ruler)
        const auto lastTick = (std::int64_t) xToTick (area.getRight());
        for (auto tick = (std::int64_t) std::floor (scroll / ticksPerStep) * ticksPerStep; tick <= lastTick; tick += ticksPerStep)
        {
            const auto x = (float) tickToX ((double) tick);
            const auto isBar = tick % bar == 0, isBeat = tick % ppq == 0;

            if (! isBeat && ticksPerStep * pixelsPerTick < 5.0)
                continue;

            g.setColour (isBar ? colour::gridBar : isBeat ? colour::gridBeat : colour::gridStep);
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());

            if (isBar)
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (type::caption));
                g.drawText (juce::String (tick / bar + 1), (int) x + 3, area.getY() - rulerHeight, 40, rulerHeight, juce::Justification::centredLeft);
            }
        }

        // (a zoomed lane has values above its top: nothing of them may reach the toolbar)
        g.saveState();
        g.reduceClipRegion (area);

        for (auto v : { 0.25f, 0.5f, 0.75f })
        {
            g.setColour (colour::gridStep.brighter (v == 0.5f && bipolarTarget (target) ? 0.4f : 0.15f));
            g.drawHorizontalLine ((int) valueToY (v), (float) area.getX(), (float) area.getRight());
        }

        // the channel's notes, faint, along the bottom (where the events will play)
        if (target.kind == AutomationTargetKind::channelVolume || target.kind == AutomationTargetKind::channelPan
            || target.kind == AutomationTargetKind::channelParam || target.kind == AutomationTargetKind::channelAdjust)
        {
            g.setColour (colour::noteHard.withAlpha (0.35f));
            for (const auto& n : pattern.notes)
                if (n.channel == target.index)
                {
                    const auto x0 = (float) tickToX ((double) n.start), x1 = (float) tickToX ((double) (n.start + n.length));
                    if (x1 >= (float) area.getX() && x0 <= (float) area.getRight())
                        g.fillRect (juce::Rectangle<float> (x0, (float) area.getBottom() - 5.0f, juce::jmax (2.0f, x1 - x0 - 1.0f), 4.0f));
                }
        }

        // selection
        if (selectionEnd > selectionStart)
        {
            const auto x0 = (float) tickToX ((double) selectionStart), x1 = (float) tickToX ((double) selectionEnd);
            g.setColour (colour::selectionFill);
            g.fillRect (juce::Rectangle<float> (x0, (float) area.getY(), x1 - x0, (float) area.getHeight()));
            g.setColour (colour::selection);
            g.drawVerticalLine ((int) x0, (float) area.getY(), (float) area.getBottom());
            g.drawVerticalLine ((int) x1, (float) area.getY(), (float) area.getBottom());
        }

        // the events: a bar per event, held until the next one
        const auto* lane = eventedit::findLane (pattern, target);
        const auto base = bipolarTarget (target) ? valueToY (0.5f) : (float) area.getBottom();

        if (lane != nullptr && ! lane->events.empty())
        {
            const auto& ev = lane->events;
            for (std::size_t i = 0; i < ev.size(); ++i)
            {
                // the last value holds to the pattern's end; a Tempo event's to the visible edge, as in FL (seen in two projects)
                const auto holdTo = target.kind == AutomationTargetKind::tempo ? juce::jmax (length, (std::int64_t) std::ceil (xToTick (area.getRight()))) : length;
                const auto end = i + 1 < ev.size() ? ev[i + 1].tick : juce::jmax (holdTo, ev[i].tick + gridTicks());
                const auto x0 = (float) tickToX ((double) ev[i].tick) + 1.0f, x1 = (float) tickToX ((double) end) - 1.0f;

                if (x1 < (float) area.getX() || x0 > (float) area.getRight())
                    continue;

                const auto y = valueToY (ev[i].value);
                const auto top = juce::jmin (y, base), bottom = juce::jmax (y, base);
                g.setColour (colour::automation.withAlpha (0.28f));
                g.fillRect (juce::Rectangle<float> (x0, top, juce::jmax (1.0f, x1 - x0), juce::jmax (1.0f, bottom - top)));
                g.setColour (colour::automation);
                g.fillRect (juce::Rectangle<float> (x0, y - 1.0f, juce::jmax (2.0f, x1 - x0), 2.5f));
            }
        }
        else
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::body));
            g.drawText (tr ("No events yet for {0}. Draw to add them; they play with the pattern.", { describeAutomationTarget (engine, target) }),
                        area.reduced (20), juce::Justification::centred);
        }

        // line tool preview
        if (lineFrom)
        {
            g.setColour (colour::textPrimary.withAlpha (0.8f));
            g.drawLine ((float) tickToX ((double) lineFrom->first), valueToY (lineFrom->second), (float) lineTo.x, (float) lineTo.y, 1.5f);
        }

        g.restoreState();

        // playback position
        if (playhead >= 0.0)
        {
            g.setColour (colour::playhead);
            g.drawVerticalLine ((int) tickToX (playhead), (float) area.getY() - rulerHeight, (float) area.getBottom());
        }

        // value read-out under the mouse
        if (hoverValue >= 0.0f)
        {
            g.setColour (colour::textPrimary);
            g.setFont (font (type::small));
            g.drawText (target.kind == AutomationTargetKind::tempo ? juce::String (tempoFromValue (hoverValue), 3) + " BPM" // (issue #86: as FL's hint)
                                                                : juce::String (juce::roundToInt (hoverValue * 100.0f)) + "%",
                        area.getRight() - 90, area.getY() + 4, 84, 16,
                        juce::Justification::centredRight);
        }

        if (endX >= area.getX() && endX < area.getRight())
        {
            g.setColour (colour::textDisabled);
            g.drawVerticalLine (endX, (float) area.getY() - rulerHeight, (float) area.getBottom());
        }

        g.setColour (colour::border);
        g.drawRect (area.withTop (area.getY() - rulerHeight));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto top = r.removeFromTop (30);
        // round 165o: buttons and labels as wide as their (translated) text needs; the title and sliders give way
        int modeWidth[3], i = 0;
        for (auto* b : { &drawButton, &lineButton, &selectButton })
            modeWidth[i++] = theme::buttonWidth (*b, 64, 120);
        const auto clearWidth = theme::buttonWidth (clearButton, 70, 120);
        const auto toAutoWidth = theme::buttonWidth (toAutoButton, 120, 180); // (round 165l)
        const auto fileWidth = theme::buttonWidth (fileButton, 60, 110);      // (issue #86)
        const auto gridWidth = theme::labelColumnWidth ({ &gridLabel }, 40, 110);
        const auto fixedTop = 240 + 12 + modeWidth[0] + modeWidth[1] + modeWidth[2] + 12 + gridWidth + 110 + clearWidth + 4 + toAutoWidth + 4 + fileWidth;
        auto titleWidth = juce::jlimit (120, 230, top.getWidth() - fixedTop), boxWidth = 240;
        // a title longer than its room ("Événements - Pattern 1") takes up to 80 px from the target box
        const auto titleNeeds = juce::jmin (230, theme::labelColumnWidth ({ &title }, 0, 400));
        if (titleNeeds > titleWidth)
        {
            const auto taken = juce::jmin (80, titleNeeds - titleWidth);
            titleWidth += taken;
            boxWidth -= taken;
        }
        // the target box gives way down to 150 px; below that the toolbar wraps, so nothing becomes unreachable (issue #86)
        boxWidth = juce::jmax (150, boxWidth - juce::jmax (0, titleWidth + boxWidth + fixedTop - 240 - top.getWidth()));
        const auto modesWidth = modeWidth[0] + modeWidth[1] + modeWidth[2];
        const auto rightWidth = fileWidth + 4 + toAutoWidth + 4 + clearWidth;
        const auto roomFor = [&r, &top] (int width) {
            if (width > top.getWidth())
                top = r.removeFromTop (30);
        };
        title.setBounds (top.removeFromLeft (titleWidth));
        targetBox.setBounds (top.removeFromLeft (boxWidth).reduced (0, 2));
        roomFor (12 + modesWidth);
        top.removeFromLeft (12);
        i = 0;
        for (auto* b : { &drawButton, &lineButton, &selectButton })
            b->setBounds (top.removeFromLeft (modeWidth[i++]).reduced (1, 2));
        roomFor (12 + gridWidth + 110);
        top.removeFromLeft (12);
        gridLabel.setBounds (top.removeFromLeft (gridWidth));
        gridBox.setBounds (top.removeFromLeft (110).reduced (0, 2));
        roomFor (rightWidth);
        clearButton.setBounds (top.removeFromRight (clearWidth).reduced (0, 2));
        top.removeFromRight (4);
        toAutoButton.setBounds (top.removeFromRight (toAutoWidth).reduced (0, 2));
        top.removeFromRight (4);
        fileButton.setBounds (top.removeFromRight (fileWidth).reduced (0, 2));

        auto tools = r.removeFromBottom (32);
        r.removeFromBottom (6);
        canvas = r.withTrimmedTop (4);
        if (valueBar.isVisible())
        {
            auto bar = canvas.removeFromRight (14);
            valueBar.setBounds (bar.withTrimmedTop (rulerHeight));
        }

        const auto scaleW = theme::labelColumnWidth ({ &scaleLabel }, 42, 120), offsetW = theme::labelColumnWidth ({ &offsetLabel }, 44, 120),
                   randomW = theme::labelColumnWidth ({ &randomLabel }, 66, 140), lfoW = theme::labelColumnWidth ({ &lfoLabel }, 28, 80);
        const auto randomButtonW = theme::buttonWidth (randomButton, 52, 106) + 4, lfoButtonW = theme::buttonWidth (lfoButton, 52, 106) + 4;
        const auto grown = (scaleW - 42) + (offsetW - 44) + (randomW - 66) + (lfoW - 28) + (randomButtonW - 56) + (lfoButtonW - 56);
        const auto spare = tools.getWidth() - 874; // the widths below at their minimum (was 834: 40 px short)
        // the three sliders give the room back first, then the LFO boxes (round 165o: German / Russian captions)
        const auto overflow = juce::jmax (0, grown - spare);
        const auto shrink = juce::jmin (70, overflow / 3);
        const auto boxShrink = juce::jmin (16, juce::jmax (0, overflow - 3 * shrink) / 2);
        scaleLabel.setBounds (tools.removeFromLeft (scaleW));
        scaleSlider.setBounds (tools.removeFromLeft (140 - shrink).reduced (0, 4));
        tools.removeFromLeft (8);
        offsetLabel.setBounds (tools.removeFromLeft (offsetW));
        offsetSlider.setBounds (tools.removeFromLeft (140 - shrink).reduced (0, 4));
        tools.removeFromLeft (12);
        randomLabel.setBounds (tools.removeFromLeft (randomW));
        amountSlider.setBounds (tools.removeFromLeft (juce::jmax (50, 110 - shrink)).reduced (0, 4));
        randomButton.setBounds (tools.removeFromLeft (randomButtonW).reduced (2, 3));
        tools.removeFromLeft (10);
        lfoLabel.setBounds (tools.removeFromLeft (lfoW));
        shapeBox.setBounds (tools.removeFromLeft (84 - boxShrink).reduced (1, 3));
        periodBox.setBounds (tools.removeFromLeft (78 - boxShrink).reduced (1, 3));
        lfoButton.setBounds (tools.removeFromLeft (lfoButtonW).reduced (2, 3));

        if (! fitted && plotArea().getWidth() > 50) // the whole pattern, with a little room after it
        {
            fitted = true;
            pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 80.0 / ticksPerStep, (plotArea().getWidth() - 8) / (patternLength() * 1.15));
        }
    }

    // --- mouse ---------------------------------------------------------------------------------

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! plotArea().contains (e.getPosition()))
            return;

        grabKeyboardFocus();
        const auto tick = snap (xToTick (e.x));

        if (selectButton.getToggleState())
        {
            selectionAnchor = tick;
            selectionStart = selectionEnd = -1;
            repaint();
            return;
        }

        beginGesture();

        if (lineButton.getToggleState() && ! e.mods.isRightButtonDown())
        {
            lineFrom = std::make_pair (tick, yToValue (e.y));
            lineTo = e.getPosition();
            return;
        }

        lastDraw = std::make_pair (tick, yToValue (e.y));
        drawCells (tick, yToValue (e.y), tick, yToValue (e.y), e.mods.isRightButtonDown());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto tick = snap (xToTick (juce::jlimit (plotArea().getX(), plotArea().getRight() - 1, e.x)));
        hoverValue = yToValue (e.y);

        if (selectButton.getToggleState() && selectionAnchor >= 0)
        {
            selectionStart = juce::jmin (selectionAnchor, tick);
            selectionEnd = juce::jmax (selectionAnchor, tick) + gridTicks();
            repaint();
            return;
        }

        if (lineFrom)
        {
            lineTo = e.getPosition();
            repaint();
            return;
        }

        if (lastDraw)
        {
            drawCells (lastDraw->first, lastDraw->second, tick, yToValue (e.y), e.mods.isRightButtonDown());
            lastDraw = std::make_pair (tick, yToValue (e.y));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (selectButton.getToggleState())
        {
            if (selectionEnd <= selectionStart) // a click clears the selection
                selectionStart = selectionEnd = -1;
            selectionAnchor = -1;
            repaint();
            return;
        }

        if (lineFrom)
        {
            const auto from = *lineFrom;
            const auto to = std::make_pair (snap (xToTick (e.x)), yToValue (e.y));
            lineFrom.reset();
            const auto step = gridTicks();
            previewGesture ([&] (PatternEvents& lane) {
                if (to.first >= from.first)
                    eventedit::line (lane, from.first, to.first + step, from.second, to.second, step);
                else
                    eventedit::line (lane, to.first, from.first + step, to.second, from.second, step);
            });
        }

        lastDraw.reset();
        commitGesture();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto inside = plotArea().contains (e.getPosition());
        hoverValue = inside ? yToValue (e.y) : -1.0f;
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverValue = -1.0f;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (flmods::ctrl (e.mods))
        {
            const auto at = xToTick (e.x);
            pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 80.0 / ticksPerStep, pixelsPerTick * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
            scroll = juce::jmax (0.0, at - (e.x - plotArea().getX()) / pixelsPerTick);
        }
        else
            scroll = juce::jmax (0.0, scroll - (w.deltaY + w.deltaX) * 400.0 / (pixelsPerTick * ticksPerStep) * ticksPerStep / 16.0);

        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            edit ([] (PatternEvents& lane, std::int64_t from, std::int64_t to) { eventedit::erase (lane, from, to); });
            return true;
        }

        if (target.kind != AutomationTargetKind::none && key == juce::KeyPress ('m', juce::ModifierKeys::commandModifier, 0))
        {
            chooseMidi();
            return true;
        }

        if (target.kind != AutomationTargetKind::none && midiclipboard::hasMidi()
            && key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
        {
            pasteMidi();
            return true;
        }

        if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
        {
            selectionStart = 0;
            selectionEnd = patternLength();
            repaint();
            return true;
        }

        return false;
    }

private:
    static constexpr int rulerHeight = 18;

    // --- model access ---------------------------------------------------------------------------

    const Pattern& currentPattern() const
    {
        const auto& song = engine.getSong();
        return song.patterns[(std::size_t) juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern)];
    }

    static Pattern& currentPattern (Song& s)
    {
        return s.patterns[(std::size_t) juce::jlimit (0, (int) s.patterns.size() - 1, s.currentPattern)];
    }

    std::int64_t patternLength() const
    {
        return juce::jmax<std::int64_t> (ticksPerStep, currentPattern().effectiveLength (engine.getSong().beatsPerBar));
    }

    std::int64_t gridTicks() const
    {
        static const std::int64_t steps[] = { ticksPerStep / 2, ticksPerStep, ppq / 2, ppq, -1 };
        const auto i = juce::jlimit (0, 4, gridBox.getSelectedItemIndex());
        return steps[i] > 0 ? steps[i] : (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    }

    std::int64_t snap (double tick) const
    {
        const auto g = gridTicks();
        return juce::jmax<std::int64_t> (0, (std::int64_t) std::floor (tick / (double) g) * g);
    }

    void rebuildTargets()
    {
        const auto& song = engine.getSong();
        targets.clear();
        const auto add = [this] (const AutomationTarget& t) {
            if (t.kind != AutomationTargetKind::none // (the tempo too: FL's pattern Tempo events, issue #86)
                && std::none_of (targets.begin(), targets.end(), [&] (const AutomationTarget& o) { return sameTarget (o, t); }))
                targets.push_back (t);
        };

        add (target);
        const auto channel = engine.getSelectedChannel();
        if (channel >= 0 && channel < (int) song.channels.size())
        {
            add ({ AutomationTargetKind::channelVolume, channel });
            add ({ AutomationTargetKind::channelPan, channel });
        }
        for (const auto& lane : currentPattern().events)
            if (! lane.events.empty())
                add (lane.target);

        targetBox.clear (juce::dontSendNotification);
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            const auto* lane = eventedit::findLane (currentPattern(), targets[i]);
            const auto marker = lane != nullptr && ! lane->events.empty() ? juce::String (" *") : juce::String();
            targetBox.addItem (describeAutomationTarget (engine, targets[i]) + marker, (int) i + 1);
            if (sameTarget (targets[i], target))
                targetBox.setSelectedItemIndex ((int) i, juce::dontSendNotification);
        }

        updateTitle();
    }

    // FL names the Tempo lane in its title ("Events - Tempo"); LYNX's other lanes keep the pattern's name
    void updateTitle()
    {
        const auto name = target.kind == AutomationTargetKind::tempo ? describeAutomationTarget (engine, target)
                                                                     : juce::String::fromUTF8 (currentPattern().name.c_str());
        const auto newTitle = tr ("Events - {0}", { name });
        if (newTitle != title.getText())
        {
            title.setText (newTitle, juce::dontSendNotification);
            resized(); // the title's width follows its text
        }
    }

    // FL opens the Tempo lane zoomed 7x from its bottom: 10..83.14 BPM, the rest behind a scrollbar (REA_rodada178a §10);
    // the other lanes show their whole range
    void targetChanged()
    {
        const auto zoomed = target.kind == AutomationTargetKind::tempo;
        viewLow = 0.0f;
        viewHigh = zoomed ? 1.0f / 7.0f : 1.0f;
        valueBar.setRangeLimits (0.0, 1.0, juce::dontSendNotification);
        valueBar.setCurrentRange (1.0 - viewHigh, viewHigh - viewLow, juce::dontSendNotification);
        valueBar.setVisible (zoomed);
        updateTitle();
        resized();
    }

    void scrollBarMoved (juce::ScrollBar*, double start) override
    {
        const auto span = viewHigh - viewLow;
        viewHigh = (float) (1.0 - start);
        viewLow = viewHigh - span;
        repaint();
    }

    // --- editing --------------------------------------------------------------------------------

    void beginGesture()
    {
        if (! gestureStart)
            gestureStart = currentPattern().events;
    }

    // edits the live pattern (no undo step yet)
    void previewGesture (const std::function<void (PatternEvents&)>& fn)
    {
        const auto t = target;
        engine.editSong ([&] (Song& s) { fn (eventedit::laneFor (currentPattern (s), t)); }, false);
        repaint();
    }

    // replaces the live lane by `fn` applied to the gesture's starting state
    void previewFromStart (const std::function<void (PatternEvents&)>& fn)
    {
        if (! gestureStart)
            return;

        const auto t = target;
        const auto start = *gestureStart;
        engine.editSong ([&] (Song& s) {
            auto& p = currentPattern (s);
            p.events = start;
            fn (eventedit::laneFor (p, t));
        }, false);
        repaint();
    }

    void commitGesture()
    {
        if (! gestureStart)
            return;

        auto after = currentPattern().events;
        auto before = std::move (*gestureStart);
        gestureStart.reset();

        engine.editSong ([&] (Song& s) { currentPattern (s).events = before; }, false);
        engine.editSong ([&] (Song& s) {
            auto& p = currentPattern (s);
            p.events = std::move (after);
            eventedit::removeEmptyLanes (p);
        });
        rebuildTargets();
        repaint();
    }

    // one undo step: `fn` on the selection (or the whole pattern)
    void edit (const std::function<void (PatternEvents&, std::int64_t, std::int64_t)>& fn)
    {
        const auto from = selectionEnd > selectionStart ? selectionStart : 0;
        const auto to = selectionEnd > selectionStart ? selectionEnd : juce::jmax (patternLength(), lastEventTick() + 1);
        beginGesture();
        previewGesture ([&] (PatternEvents& lane) { fn (lane, from, to); });
        commitGesture();
    }

    // FL's event editor Open automation / Save automation as (REA_rodada178a §4): the shown target's events as an event score
    // (.fsc, values normalised to 0..1); opening one replaces the shown target's events, whatever target it was saved
    // from (FL's scores are target-agnostic: a Tempo curve can go onto a volume).
    void showFileMenu()
    {
        const auto* lane = eventedit::findLane (currentPattern(), target);
        const auto hasEvents = lane != nullptr && ! lane->events.empty();
        juce::PopupMenu menu;
        const auto canEdit = target.kind != AutomationTargetKind::none;
        menu.addItem (theme::mnemonic (tr ("Open automation..."), 'O'), canEdit, false, [this] { openScore(); });
        menu.addItem (theme::mnemonic (tr ("Browse automations"), 'B'), EventEditorHooks::onBrowseAutomations != nullptr, false,
                      [] { EventEditorHooks::onBrowseAutomations(); });
        menu.addItem (theme::mnemonic (tr ("Save automation as..."), 'S'), hasEvents, false, [this] { saveScore(); });
        menu.addSeparator();
        menu.addItem (theme::mnemonic (tr ("Import MIDI file..."), 'I') + "\tCtrl+M", canEdit, false, [this] { chooseMidi(); });
        menu.addItem (theme::mnemonic (tr ("Paste from MIDI clipboard..."), 'P') + "\tShift+Ctrl+V", canEdit && midiclipboard::hasMidi(), false,
                      [this] { pasteMidi(); });
        menu.addSeparator();
        // FL's item writes the same .fsc as Save automation as here: same dialog, byte-identical file (REA_rodada178a §11)
        menu.addItem (theme::mnemonic (tr ("Export as score sheet..."), 'E'), hasEvents, false, [this] { saveScore(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton));
    }

    void openScore()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Open automation"), juce::File(), "*.fsc;*.mid;*.midi");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [safe = juce::Component::SafePointer<EventEditor> (this)] (const juce::FileChooser& fc) {
                                     if (safe != nullptr && fc.getResult().existsAsFile())
                                         safe->loadScore (fc.getResult());
                                 });
    }

    bool loadScore (const juce::File& file)
    {
        juce::MemoryBlock data;
        if (target.kind != AutomationTargetKind::none && (file.hasFileExtension ("mid") || file.hasFileExtension ("midi")))
        {
            // FL's Open automation takes a MIDI file too: the same "Import MIDI data" window (REA_rodada178a §11)
            juce::String error;
            if (const auto midi = MidiFileIo::parseFile (file, error))
                importMidi (*midi, file.getFileName());
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", error);
            return true;
        }
        if (target.kind == AutomationTargetKind::none || ! file.loadFileAsData (data))
            return false;
        const auto events = prtools::parseEventScore (std::span<const std::uint8_t> ((const std::uint8_t*) data.getData(), data.getSize()));
        if (! events)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Open automation"),
                                                    tr ("This file is not an event score."));
            return false;
        }
        beginGesture();
        previewGesture ([&] (PatternEvents& l) { l.events = *events; });
        commitGesture();
        return true;
    }

    void chooseMidi()
    {
        // (FL's starts in the user's Presets\\Scores\\Automation; seen round 178a)
        const auto automationScores = findFlStudio().userData.getChildFile ("Presets").getChildFile ("Scores").getChildFile ("Automation");
        chooser = lynx::ui::makeFileChooser (tr ("Import MIDI file..."), automationScores.isDirectory() ? automationScores : juce::File(),
                                             "*.mid;*.midi");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [safe = juce::Component::SafePointer<EventEditor> (this)] (const juce::FileChooser& fc) {
                                     if (safe == nullptr || ! fc.getResult().existsAsFile())
                                         return;
                                     juce::String error;
                                     if (const auto midi = MidiFileIo::parseFile (fc.getResult(), error))
                                         safe->importMidi (*midi, fc.getResult().getFileName());
                                     else
                                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", error);
                                 });
    }

    void pasteMidi()
    {
        const auto smf = midiclipboard::get();
        if (! smf)
            return;
        juce::String error;
        if (const auto midi = MidiFileIo::parseBytes (smf->getData(), smf->getSize(), error))
            importMidi (*midi, tr ("MIDI from clipboard"));
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", tr ("Error parsing the MIDI data..."));
    }

    // FL replaces the shown lane with the chosen controller, value / 127 (REA_rodada178a §11), and adds the time signature to the
    // pattern's markers; notes aren't imported here.
    void importMidi (const midiimport::ParsedMidi& midi, const juce::String& name)
    {
        // (FL's window starts afresh every time: controller "---", Realign off, time signatures on; seen round 178a)
        showEventLaneMidiImportDialog (name, midi, midiimport::Options {}, [safe = juce::Component::SafePointer<EventEditor> (this), midi] (const midiimport::Options& o) {
            if (safe == nullptr)
                return;
            const auto events = midiimport::controllerEvents (midi, o); // (none in the file: FL leaves the lane empty)
            safe->beginGesture();
            safe->previewGesture ([&] (PatternEvents& l) { l.events = events; });
            safe->commitGesture();
            if (o.timeSignatures)
                safe->engine.editSong ([&] (Song& s) {
                    auto& markers = currentPattern (s).markers;
                    if (std::none_of (markers.begin(), markers.end(), [] (const Marker& m) { return m.tick == 0 && m.numerator > 0; }))
                    {
                        Marker m;
                        m.numerator = midi.numerator > 0 ? midi.numerator : 4;
                        m.denominator = midi.numerator > 0 ? midi.denominator : 4;
                        m.action = Marker::timeSignature;
                        markers.push_back (m);
                    }
                });
        });
    }

    void saveScore()
    {
        const auto name = describeAutomationTarget (engine, target);
        chooser = lynx::ui::makeFileChooser (tr ("Save automation as"),
                                             juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                                 .getChildFile (juce::File::createLegalFileName (name) + ".fsc"),
                                             "*.fsc");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [safe = juce::Component::SafePointer<EventEditor> (this)] (const juce::FileChooser& fc) {
                                     auto file = fc.getResult();
                                     if (safe == nullptr || file == juce::File())
                                         return;
                                     if (! file.hasFileExtension ("fsc"))
                                         file = file.withFileExtension (".fsc");
                                     const auto* l = eventedit::findLane (safe->currentPattern(), safe->target);
                                     const auto bytes = prtools::writeEventScore (l != nullptr ? l->events : std::vector<PatternEvent> {},
                                                                                  safe->target.kind == AutomationTargetKind::tempo ? 5u : 0u);
                                     if (l == nullptr || l->events.empty() || ! file.replaceWithData (bytes.data(), bytes.size()))
                                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Save automation as"),
                                                                                 tr ("There are no events to save, or the file could not be written."));
                                 });
    }

    void showConvertToAutomation()
    {
        const auto* lane = eventedit::findLane (currentPattern(), target);
        if (lane == nullptr || lane->events.empty())
            return;
        juce::Component::SafePointer<EventEditor> safe (this);
        openConvertToAutomationPanel ((int) lane->events.size(), 100.0,
                                      [safe] (float sensitivity) {
                                          if (safe == nullptr)
                                              return 0;
                                          const auto* l = eventedit::findLane (safe->currentPattern(), safe->target);
                                          return l != nullptr ? (int) eventedit::toAutomationPoints (*l, sensitivity).size() : 0;
                                      },
                                      [safe] (float sensitivity) {
                                          if (safe != nullptr)
                                              safe->convertToAutomation (sensitivity);
                                      });
    }

    void convertToAutomation (float sensitivity)
    {
        const auto* lane = eventedit::findLane (currentPattern(), target);
        if (lane == nullptr || lane->events.empty())
            return;
        auto points = eventedit::toAutomationPoints (*lane, sensitivity);
        const auto t = target;
        const auto name = describeAutomationTarget (engine, t).toStdString();
        const auto length = juce::jmax (patternLength(), points.back().tick + 1);
        if (points.back().tick < length)
            points.push_back ({ length, points.back().value, 0.0f });
        int index = -1;
        engine.editSong ([&] (Song& s) {
            Automation a;
            a.name = name;
            a.target = t;
            a.points = points;
            s.automations.push_back (std::move (a));
            index = (int) s.automations.size() - 1;
            // the clip sits where the pattern is first used in the Playlist (else at the start), on a new track
            const auto pattern = juce::jlimit (0, (int) s.patterns.size() - 1, s.currentPattern);
            std::int64_t at = -1;
            int track = 0;
            for (const auto& c : s.playlist)
            {
                track = std::max (track, c.track + 1);
                if (c.kind == ClipKind::pattern && c.pattern == pattern && (at < 0 || c.start < at))
                    at = c.start;
            }
            Clip clip;
            clip.kind = ClipKind::automation;
            clip.automation = index;
            clip.track = track;
            clip.start = std::max<std::int64_t> (0, at);
            clip.length = length;
            s.playlist.push_back (clip);
            auto& p = currentPattern (s);
            eventedit::erase (eventedit::laneFor (p, t), 0, std::numeric_limits<std::int64_t>::max());
            eventedit::removeEmptyLanes (p);
        });
        rebuildTargets();
        repaint();
        if (index >= 0)
            openAutomationEditor (engine, index);
    }

    std::int64_t lastEventTick() const
    {
        const auto* lane = eventedit::findLane (currentPattern(), target);
        return lane != nullptr && ! lane->events.empty() ? lane->events.back().tick : 0;
    }

    void drawCells (std::int64_t fromTick, float fromValue, std::int64_t toTick, float toValue, bool erase)
    {
        const auto step = gridTicks();
        const auto a = juce::jmin (fromTick, toTick), b = juce::jmax (fromTick, toTick);
        const auto va = fromTick <= toTick ? fromValue : toValue, vb = fromTick <= toTick ? toValue : fromValue;

        previewGesture ([&] (PatternEvents& lane) {
            if (erase)
                eventedit::erase (lane, a, b + step);
            else
                eventedit::line (lane, a, b + step, va, vb, step);
        });
    }

    void previewScale()
    {
        if (resettingSliders)
            return;

        const auto standalone = ! gestureStart; // typed into the text box: one step at once
        beginGesture();
        const auto from = selectionEnd > selectionStart ? selectionStart : 0;
        const auto to = selectionEnd > selectionStart ? selectionEnd : std::numeric_limits<std::int64_t>::max();
        const auto factor = (float) scaleSlider.getValue(), offset = (float) offsetSlider.getValue();
        const auto bipolar = bipolarTarget (target);
        previewFromStart ([&] (PatternEvents& lane) { eventedit::scale (lane, from, to, factor, offset, bipolar); });

        if (standalone && ! scaleSlider.isMouseButtonDown() && ! offsetSlider.isMouseButtonDown())
        {
            commitGesture();
            const juce::ScopedValueSetter<bool> quiet (resettingSliders, true);
            scaleSlider.setValue (1.0, juce::dontSendNotification);
            offsetSlider.setValue (0.0, juce::dontSendNotification);
        }
    }

    void applyLfo()
    {
        static const double periodBeats[] = { 1.0, 2.0, 4.0, 8.0, 16.0 };
        prtools::LfoOptions o;
        o.shape = shapeBox.getSelectedItemIndex();
        // FL's speed wheel for the period: beat x 2^(7 - speed/4096)
        o.startSpeed = juce::roundToInt ((7.0 - std::log2 (periodBeats[juce::jlimit (0, 4, periodBox.getSelectedItemIndex())])) * 4096.0);
        o.phase = 0;
        const auto step = gridTicks();
        edit ([&] (PatternEvents& lane, std::int64_t from, std::int64_t to) { eventedit::applyLfo (lane, from, to, o, step); });
    }

    void setupSlider (juce::Slider& s, double min, double max, double initial, std::function<juce::String (double)> format)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        s.setRange (min, max, 0.01);
        s.setValue (initial, juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, initial);
        s.textFromValueFunction = std::move (format);
        s.updateText();
        addAndMakeVisible (s);
    }

    // --- geometry -------------------------------------------------------------------------------

    juce::Rectangle<int> plotArea() const { return canvas.withTrimmedTop (rulerHeight); }
    double tickToX (double tick) const { return plotArea().getX() + (tick - scroll) * pixelsPerTick; }
    double xToTick (int x) const { return scroll + (x - plotArea().getX()) / pixelsPerTick; }
    float valueToY (float v) const
    {
        const auto a = plotArea().reduced (0, 6);
        return (float) a.getBottom() - (v - viewLow) / (viewHigh - viewLow) * (float) a.getHeight();
    }
    float yToValue (int y) const
    {
        const auto a = plotArea().reduced (0, 6);
        const auto inView = juce::jlimit (0.0f, 1.0f, (float) (a.getBottom() - y) / (float) juce::jmax (1, a.getHeight()));
        return viewLow + inView * (viewHigh - viewLow);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (! gestureStart)
        {
            rebuildTargets();
            repaint();
        }
    }

    void timerCallback() override
    {
        auto tick = -1.0;
        const auto& song = engine.getSong();

        if (engine.getTransport().playing.load())
        {
            const auto position = engine.getTransport().positionTicks.load();

            if (song.mode != PlayMode::song)
                tick = std::fmod (position, (double) patternLength());
            else
            {
                const auto p = engine.songPlayheadTicks();
                for (const auto& c : song.playlist)
                    if (c.kind == ClipKind::pattern && c.pattern == song.currentPattern && p >= (double) c.start && p < (double) c.end())
                        tick = patternTickInClip (c, p, patternLength()); // a stretched clip loops its pattern (LYNX extra, issue #25)
            }
        }

        if (tick != playhead)
        {
            playhead = tick;
            repaint();
        }
    }

    AudioEngine& engine;
    AutomationTarget target;
    std::vector<AutomationTarget> targets;

    juce::Label title, gridLabel, scaleLabel, offsetLabel, randomLabel, lfoLabel;
    juce::ComboBox targetBox, gridBox, shapeBox, periodBox;
    juce::TextButton drawButton, lineButton, selectButton, randomButton, lfoButton, clearButton, toAutoButton, fileButton;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Slider scaleSlider, offsetSlider, amountSlider;

    juce::Rectangle<int> canvas;
    double pixelsPerTick = 0.05, scroll = 0.0, playhead = -1.0;
    bool fitted = false, resettingSliders = false;
    float hoverValue = -1.0f;
    float viewLow = 0.0f, viewHigh = 1.0f; // the part of the 0..1 range the lane shows
    juce::ScrollBar valueBar { true };
    std::int64_t selectionStart = -1, selectionEnd = -1, selectionAnchor = -1;
    std::optional<std::pair<std::int64_t, float>> lineFrom, lastDraw;
    juce::Point<int> lineTo;
    std::optional<std::vector<PatternEvents>> gestureStart;
    std::uint32_t seed = 1;
};
} // namespace

void openConvertToAutomationPanel (int events, double initialPercent, std::function<int (float)> pointsFor, std::function<void (float)> onAccept)
{
    struct Panel final : public juce::Component
    {
        Panel (int eventCount, double percent, std::function<int (float)> points, std::function<void (float)> acceptFn)
            : events (eventCount), pointsFor (std::move (points)), onAccept (std::move (acceptFn))
        {
            label.setText (tr ("Sensitivity"), juce::dontSendNotification);
            addAndMakeVisible (label);
            slider.setRange (40.0, 100.0, 0.1);
            slider.setValue (percent, juce::dontSendNotification);
            slider.setDoubleClickReturnValue (true, percent);
            slider.setTextValueSuffix ("%");
            slider.onValueChange = [this] { update(); };
            addAndMakeVisible (slider);
            info.setFont (theme::font (theme::type::small));
            info.setColour (juce::Label::textColourId, theme::colour::textSecondary);
            addAndMakeVisible (info);
            accept.setButtonText (tr ("Accept"));
            accept.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
            cancel.setButtonText (tr ("Cancel"));
            accept.onClick = [this] {
                const auto fn = onAccept;
                const auto sensitivity = (float) slider.getValue() / 100.0f;
                closeToolWindowOf (*this); // (deletes this)
                if (fn)
                    fn (sensitivity);
            };
            cancel.onClick = [this] { closeToolWindowOf (*this); };
            addAndMakeVisible (accept);
            addAndMakeVisible (cancel);
            setSize (380, 130);
            update();
        }
        void update()
        {
            const auto n = pointsFor ? pointsFor ((float) slider.getValue() / 100.0f) : 0;
            info.setText (tr ("{0} events become {1} automation points", { juce::String (events), juce::String (n) }), juce::dontSendNotification);
        }
        void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            auto row = r.removeFromTop (28);
            label.setBounds (row.removeFromLeft (90));
            slider.setBounds (row);
            info.setBounds (r.removeFromTop (28));
            auto b = r.removeFromBottom (28);
            cancel.setBounds (b.removeFromRight (84));
            b.removeFromRight (6);
            accept.setBounds (b.removeFromRight (84));
        }
        int events;
        std::function<int (float)> pointsFor;
        std::function<void (float)> onAccept;
        juce::Label label, info;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::TextButton accept, cancel;
    };
    openToolWindow (std::make_unique<Panel> (events, initialPercent, std::move (pointsFor), std::move (onAccept)), tr ("Convert to automation"), "eventeditor-toauto");
}

namespace EventEditorHooks
{
std::function<void()> onBrowseAutomations;
}

void openEventEditor (AudioEngine& engine, const AutomationTarget& target)
{
    // one event editor at a time: the same key replaces an open one
    openToolWindow (std::make_unique<EventEditor> (engine, target), tr ("Event editor"), "eventeditor");
}

std::unique_ptr<juce::Component> makeEventEditor (AudioEngine& engine, const AutomationTarget& target)
{
    return std::make_unique<EventEditor> (engine, target);
}
} // namespace lynx
