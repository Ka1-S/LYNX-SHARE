// Round 190he (issue #152, Nuelka's list for 0.1.27): docked windows that a click in a plugin view made by another
// thread or process must raise (core/DockStack.h), and the size of LYNX's plugin windows (FL's Scaling, core/EditorScale.h).
#include "core/DockStack.h"
#include "core/EditorScale.h"

TEST (u190he_a_click_from_another_thread_or_process_raises_its_docked_window)
{
    using lynx::dockstack::clickNeedsParentNotice;
    CHECK (clickNeedsParentNotice (4242, 17));   // a plugin view of another thread / a bridged plugin's process
    CHECK (! clickNeedsParentNotice (17, 17));   // LYNX's UI thread: the message hook raises it (once)
    CHECK (! clickNeedsParentNotice (0, 17));    // the window is gone
}

TEST (u190he_scaling_offers_fl_sizes_and_lynx_default_stays_125)
{
    using namespace lynx::editorscale;
    // FL's PluginScalingMenu: 100 % … 400 %; LYNX opens at FL's size × 1.25
    CHECK (menuPercents.front() == 100 && menuPercents.back() == 400);
    CHECK (percentOf (0) == 125); // FL's "Host" (stored 0: nothing written in the project)
    CHECK (storedFor (125) == 125); // (125 % chosen by hand is not Host: FL ticks one or the other)
    CHECK (storedFor (100) == 100);
    CHECK (percentOf (100) == 100);
    // FL's 100 % is smaller than LYNX's default: the window can be made smaller (the reporter's request)
    CHECK (factorOf (100) < 1.0 && std::abs (factorOf (100) - 0.8) < 1e-9);
    CHECK (std::abs (factorOf (0) - 1.0) < 1e-9);
    CHECK (std::abs (factorOf (250) - 2.0) < 1e-9);
    // out of range values (a hand-edited project) are held in range
    CHECK (percentOf (9000) == maxPercent && percentOf (5) == minPercent);
    CHECK (storedFor (-3) == minPercent);
}

TEST (u190he_dragging_the_edge_gives_the_matching_percentage)
{
    using namespace lynx::editorscale;
    CHECK (percentForWidth (800, 800) == 125);  // unchanged
    CHECK (percentForWidth (640, 800) == 100);  // FL's own size
    CHECK (percentForWidth (1600, 800) == 250);
    CHECK (percentForWidth (10, 800) == minPercent);
    CHECK (percentForWidth (100000, 800) == maxPercent);
    CHECK (percentForWidth (500, 0) == defaultPercent); // (no page yet)
}
