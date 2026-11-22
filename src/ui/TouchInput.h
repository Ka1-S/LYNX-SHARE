#pragma once

// Touch mode (round 190pm, research190oe P23; a LYNX extra, off by default; Settings › LYNX extras "simple.touchMode",
// View › Touch mode in the Simple layout; suggested once when a finger is first seen on the window).
//
// With it on, a finger on the Playlist, the Piano roll and the Simple dock's knobs is read as a touch (core/TouchGestures.h):
//  - a long press is the context menu of what is under the finger (a clip's menu, a note's properties, a header's
//    menu; FL's right-click delete stays on the mouse's right button);
//  - in the Simple layout two fingers pan the view and a pinch zooms it, around the fingers; the controls are at touch
//    size (Large controls) and a knob's long press opens a value pop-up with − and + buttons.
// A mouse, a pen and a touchpad never go through it. JUCE gives every finger its own MouseInputSource on Windows
// (WM_POINTER, else WM_TOUCH: multi-touch, no system gestures), so the fingers come as separate mouse sources, each with
// its index; Windows' press-and-hold right click does not reach a JUCE window, hence the long press here.

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TouchGestures.h"

namespace lynx::ui::touchmode
{
bool& enabled();            // the switch (saved: ui-options.txt touchMode)
bool& asked();              // the suggestion was answered (saved: touchModeAsked)
bool& simpleLayout();       // the Simple layout is the one shown (MainComponent keeps it): two-finger gestures, knob pop-ups
bool& forceTouchForTests(); // every mouse event counts as a finger (tests, --snapshot)
inline bool isTouch (const juce::MouseEvent& e) { return forceTouchForTests() || e.source.isTouch(); }
inline bool active (const juce::MouseEvent& e) { return enabled() && isTouch (e); }
// how many fingers the screen takes (Windows' SM_MAXIMUMTOUCHES; 0 = no touch screen, or not known here)
int touchScreenPoints();
// what the long press uses for its timer (tests replace it)
double nowMs();
inline double& testClockMs()
{
    static double ms = -1.0; // < 0: the real clock
    return ms;
}

// The same event with other buttons, at `position` (the editor's own coordinates): a long press's right click
juce::MouseEvent withButtons (const juce::MouseEvent& like, juce::Point<float> position, juce::ModifierKeys mods);

// One editor's touch input: first in its mouseDown / mouseDrag / mouseUp, `if (touch.mouseDown (e)) return;`
class Deferral final : private juce::Timer
{
public:
    struct Hooks
    {
        juce::Component* owner = nullptr;
        std::function<void (const juce::MouseEvent&)> longPress; // the context menu at the event's place (default: a right click)
        std::function<void (float dx, float dy)> pan;            // two-finger pan (none: two-finger gestures off)
        std::function<void (float scale, juce::Point<float>)> pinch;
        bool twoFinger = true; // only in the Simple layout (simpleLayout()) and when `pan` is given
    };
    explicit Deferral (Hooks h) : hooks (std::move (h)) {}
    ~Deferral() override { stopTimer(); }

    bool mouseDown (const juce::MouseEvent& e);
    bool mouseDrag (const juce::MouseEvent& e);
    bool mouseUp (const juce::MouseEvent& e);
    bool replaying() const noexcept { return inReplay; }
    // tests: the long press timer's tick at the test clock
    void tickForTests() { timerCallback(); }
    touch::Tracker::State stateForTests() const noexcept { return tracker.state(); }

private:
    void timerCallback() override;
    void run (const std::vector<touch::Event>& events);
    static touch::Point at (const juce::MouseEvent& e) { return { e.position.x, e.position.y }; }
public:
    // tests: the finger id the next events carry (>= 0; one mouse source plays two fingers), -1 = the source's index
    static int& fingerForTests()
    {
        static int id = -1;
        return id;
    }
private:
    static int finger (const juce::MouseEvent& e) { return fingerForTests() >= 0 ? fingerForTests() : e.source.getIndex(); }

    Hooks hooks;
    touch::Tracker tracker;
    std::optional<juce::MouseEvent> held;
    bool inReplay = false;
};

// The value pop-up of a knob in Touch mode (a long press on it): − value + at touch size; `step` per press, `text` the
// value's words, `set` the knob's own move. Shown in a CallOutBox over `knob`.
void showValuePopup (juce::Component& knob, std::function<float()> get, std::function<void (float)> set,
                     std::function<juce::String (float)> text, float step = 0.01f);
// --snapshot: the pop-up's content for a knob at `value`
std::unique_ptr<juce::Component> makeValuePopupForSnapshot (float value, std::function<juce::String (float)> text);

// The suggestion card (shown once, the first time a finger is used on the window): "Turn on Touch mode" / "Not now"
std::unique_ptr<juce::Component> makeSuggestion (std::function<void (bool turnOn)> answered);
} // namespace lynx::ui::touchmode
