#pragma once

#include <memory>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ValueControl.h"

// The hint bar (docs/WORKFLOW_FL.md, G12 and section 5.1): the lower line of the main toolbar's first
// panel says what the control under the mouse does, with its value, and shows the value live while it
// is dragged.
//
// How a panel feeds it (any of these; the first that gives text wins, walking up from the component
// under the mouse):
//   1. a ui::HintSource component describes the control at a point (custom-painted controls);
//   2. ui::setHint (component, text) gives a fixed hint;
//   3. otherwise the tooltip is shown.
// While a gesture is in progress (moving notes, dragging a clip, rubber-band selection...), a panel can
// show what the gesture does right now with ui::postHint(); the bar shows the posted hint instead of the
// hover hint until shortly after the last post.
//
// Hint texts may use the markup of core/HintMarkup.h, drawn as mouse-button and key glyphs:
//   [LMB] click/drag  [2xLMB] double-click  [RMB] right-click  [MMB] middle-click  [Wheel] wheel
//   [Ctrl] [Shift] [Alt] [F5] [L]... (any other short key name: a key cap)
// e.g. tr ("Note: [LMB] drag moves  [Shift][LMB] duplicates  [RMB] deletes  [Alt] no snap").
// Tooltips should stay plain text (they are shown as is); use ui::plainHint() to derive one.
namespace lynx::ui
{
// Shows `hint` in the bar until `holdMs` after the last call (call it on every drag step).
void postHint (const Hint& hint, int holdMs = 1200);
void clearPostedHint(); // (a gesture's own posted hint; a notice stays)
// A message the user must be able to read (an edit refused: "... is frozen: Unfreeze to edit"): it stays in the bar for
// `holdMs` over the hover hints and the gestures' posted hints, and clearPostedHint (a mouse-up) leaves it (round 190bj).
void postNotice (const Hint& hint, int holdMs = 3000);
std::optional<Hint> postedHint(); // the posted hint while it is fresh (the main window polls it)

// The standard gesture line of a value control (knob, fader, number): drag, fine, wheel, reset, menu.
juce::String valueGestures();
// The hint text with the markup turned into words ("[Ctrl][LMB]" -> "Ctrl+click"), for tooltips.
juce::String plainHint (const juce::String& markup);
// Hints written before the markup existed say "drag (Ctrl: fine), wheel, Alt+click resets"; this swaps
// such known phrases for their markup (applied by the bar to every hint).
juce::String withMarkup (const juce::String& text);

// Quick tips: a small dark bubble with a control's name (and its shortcut, if it has one) that appears
// about 400 ms after the mouse rests on it, under the control. It only names the tool, so nobody is lost
// among the icons; what the control does in detail stays in the hint bar. Only components given a name
// with setQuickTip() show one (the toolbar and the panel title bars).
void setQuickTip (juce::Component& c, const juce::String& name, const juce::String& shortcut = {});
juce::String quickTipOf (const juce::Component& c); // "name\tshortcut" ("" when none)

// The bubble: its size for a tip, and its drawing (shared by the window and the UI snapshots).
juce::Rectangle<int> quickTipSize (const juce::String& tip);
void paintQuickTip (juce::Graphics& g, juce::Rectangle<float> bubble, const juce::String& tip);
// Where the bubble goes for a control at `target` (centred under it, above it near the bottom edge).
juce::Rectangle<int> quickTipBounds (const juce::String& tip, juce::Rectangle<int> target, juce::Rectangle<int> area);

// One per application: shows the quick tips of the components under the mouse, in every window.
class QuickTipWindow final : public juce::TooltipWindow
{
public:
    QuickTipWindow();
    ~QuickTipWindow() override;
    juce::String getTipFor (juce::Component& c) override;

private:
    struct Look;
    std::unique_ptr<Look> look;
};

class HintBar final : public juce::Component
{
public:
    // `live`: the value is changing under the mouse (drawn in the accent colour).
    void setHint (const Hint& hint, bool live = false);
    const Hint& getHint() const noexcept { return current; }
    void paint (juce::Graphics& g) override;
    // Round 190kn (DF-47): a right-click on the hint bar opens what FL 2026 opens there, the toolbars menu (captured:
    // details190kn raw/f09), which the owner gives through `onMenu`; LYNX adds the shown value and text for the clipboard
    // at its end (FL's own "copy any value" is the controls' Copy value, which LYNX's value menus have too)
    void mouseDown (const juce::MouseEvent& e) override;
    juce::PopupMenu copyMenu() const; // (tests / shots)
    std::function<juce::PopupMenu()> onMenu;
    // Round 190jo (design190ia FM-7, the Pattern toolbar only): the hint a step larger and brighter, as FL's (cap 8 on one
    // line, cap 7 on two; FL cap 11 / 9); the other toolbars and windows keep the smaller text
    void setLarger (bool shouldBeLarger)
    {
        if (larger != shouldBeLarger)
        {
            larger = shouldBeLarger;
            repaint();
        }
    }

private:
    Hint current;
    bool live = false;
    bool larger = false;
    // rollup 0.1.30 step 13: the text's items and lines as last laid out (HintBar.cpp), made again only when the text, the
    // width or the fonts change — a hint that needs the two-line fallback no longer shapes its words twice per frame
    struct LaidOut;
    std::shared_ptr<LaidOut> laidOut;
};
// Ableton Live's Info View (round 190jn, design190ia CL-23) for the Clip-launch layout's Live look: the same hint as the
// hint bar, as Live shows it at the bottom left — a 17 px #464646 title bar with the control's name, then the text in
// #B5B5B5 on #363636, wrapped on a 15 px line pitch (live-session.png 9..239 × 807..994). The title is the hint's part
// before its first colon ("Volume: drag ..."), else the hint has no title. Repaints only when the hint changes.
class InfoView final : public juce::Component
{
public:
    void setHint (const Hint& hint);
    juce::String getTitleText() const { return title; }
    juce::String getBodyText() const { return body; }
    void paint (juce::Graphics& g) override;

private:
    juce::String title, body;
    juce::String lastText, lastValue; // (the hint as given: the same again changes nothing, with no text work)
    juce::TextLayout layout;          // the body laid out (again only when the text or the width changes)
    float layoutWidth = -1.0f;
};

// FL's Tools › Last tweaked as a toolbar read-out (round 162d, issue #8): the name and value of the last moved
// automatable control (a LYNX knob or a hosted plugin's own parameter); a click opens that parameter's menu (create
// automation clip, link to controller, edit events, init song with this position, value). Handy when a plugin's own
// window has no menu of its own or misbehaves. The owner refreshes it from its timer.
class TweakReadout final : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::Timer
{
public:
    TweakReadout();
    // `name` empty: nothing tweaked yet (draws a dim placeholder)
    void setContent (const juce::String& name, const juce::String& value);
    bool hasContent() const noexcept { return name.isNotEmpty(); }
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void timerCallback() override
    {
        stopTimer();
        repaint(); // the fresh-value highlight ends
    }

    juce::String name, value;
    double changedMs = 0.0; // the value changed at (drawn in the accent colour for a moment)
};
} // namespace lynx::ui
