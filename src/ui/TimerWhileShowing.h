#pragma once

// A view's polling timer that runs only while the view is shown (round 174f). A 30 Hz timer that only checks whether
// something on screen changed (a combo box, a lit key, an automated knob, an input level) wakes the message thread
// every 33 ms even with the view hidden behind another panel. JUCE tells a component only about its own visibility, so
// a ComponentMovementWatcher (which also listens to every parent) stops the timer when the view or a parent is hidden
// and starts it again when they show; everything the timer keeps up to date is read again on its first tick.
//
// Minimising the window does not count as hidden: JUCE reports no event when a window is restored, so a timer stopped
// for a minimised window would never start again. It keeps running then, as before this class.

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::ui
{
class TimerWhileShowing final : private juce::ComponentMovementWatcher
{
public:
    TimerWhileShowing (juce::Component& v, juce::Timer& t, int hz) : juce::ComponentMovementWatcher (&v), view (v), timer (t), rateHz (hz) { update(); }

private:
    // the view and all its parents visible, the top one on the desktop (minimised or not)
    bool shown() const
    {
        const juce::Component* c = &view;
        for (;; c = c->getParentComponent())
        {
            if (! c->isVisible())
                return false;
            if (c->getParentComponent() == nullptr)
                return c->isOnDesktop();
        }
    }
    void update()
    {
        if (! shown())
            timer.stopTimer();
        else if (! timer.isTimerRunning())
            timer.startTimerHz (rateHz);
    }
    void componentMovedOrResized (bool, bool) override {}
    void componentPeerChanged() override { update(); }
    void componentParentHierarchyChanged (juce::Component& c) override
    {
        juce::ComponentMovementWatcher::componentParentHierarchyChanged (c); // (registers with the new parents)
        update();
    }
    void componentVisibilityChanged() override {}
    // (the base calls the one above only when isShowing() changes, which a change while minimised does not)
    void componentVisibilityChanged (juce::Component& c) override
    {
        update();
        juce::ComponentMovementWatcher::componentVisibilityChanged (c);
    }

    juce::Component& view;
    juce::Timer& timer;
    int rateHz;
};
} // namespace lynx::ui
