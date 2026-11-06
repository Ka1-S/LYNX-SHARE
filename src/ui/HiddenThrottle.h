#pragma once

// A view's polling timer that runs only while the view is on screen (round 174f). A 30 Hz timer that only checks
// whether something on screen changed (a combo box, a lit key, an automated knob, an input level) wakes the message
// thread every 33 ms even with the view hidden behind another panel. JUCE tells a component only about its own
// visibility, so a ComponentMovementWatcher (which also listens to every parent) stops the timer when the view stops
// showing and starts it again when it shows; everything the timer keeps up to date is read again on its first tick.
// (A minimised window does not report a visibility change: the timer keeps running then, and its callback skips the
// work while ! isShowing().)

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::ui
{
class TimerWhileShowing final : private juce::ComponentMovementWatcher
{
public:
    TimerWhileShowing (juce::Component& v, juce::Timer& t, int hz) : juce::ComponentMovementWatcher (&v), view (v), timer (t), rateHz (hz) { update(); }

private:
    void update()
    {
        if (view.isShowing())
        {
            if (! timer.isTimerRunning())
                timer.startTimerHz (rateHz);
        }
        else
            timer.stopTimer();
    }
    void componentMovedOrResized (bool, bool) override {}
    void componentPeerChanged() override { update(); }
    void componentVisibilityChanged() override { update(); }

    juce::Component& view;
    juce::Timer& timer;
    int rateHz;
};
} // namespace lynx::ui
