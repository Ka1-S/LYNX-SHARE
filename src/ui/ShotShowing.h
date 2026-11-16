#pragma once

// Round 190im: the off-screen picture routes draw views that are not on the desktop, so juce::Component::isShowing() is
// false for them and their live frame steps (playhead, picker marks, activity strips) would do nothing. A route that
// needs those steps sets this flag; the steps then take the view as shown. Never set by the app itself.

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::ui
{
inline bool& showingForShots() noexcept
{
    static bool on = false;
    return on;
}
inline bool showingOrShot (const juce::Component& c) { return c.isShowing() || showingForShots(); }
} // namespace lynx::ui
