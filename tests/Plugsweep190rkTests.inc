// Round 190rk (hosted plugin sweep, 0.1.32): one test per defect the sweep found and fixed.

#include "core/ResizeStorm.h"

// A resizable plugin editor (LSP's Parametric Equalizer at a UI scale of 150 %, Linux) and its LYNX window resized each
// other for ever: the app stopped answering. Past a burst of editor-driven refits the window stops following until the
// editor has been quiet for the window of time; normal resizes (a few a second, or a drag at 60 Hz for a moment) pass.
TEST (plugsweep190rk_resize_storm_stops_a_resize_loop)
{
    using namespace lynx;
    {
        ResizeStorm storm; // (12 in 1000 ms)
        int allowed = 0;
        for (int i = 0; i < 400; ++i) // a loop: a refit every 2 ms for 0.8 s
            allowed += storm.allow (1000.0 + 2.0 * i) ? 1 : 0;
        CHECK (allowed == 12);
        CHECK (storm.isBlocked());
        // (the last refused change was at 1798 ms)
        CHECK (! storm.allow (1798.0 + 999.0)); // the editor changed again within the window: still blocked
        CHECK (storm.allow (1798.0 + 999.0 + 1001.0)); // quiet for the window: the window follows again
        CHECK (! storm.isBlocked());
    }
    {
        ResizeStorm storm;
        int allowed = 0;
        for (int i = 0; i < 60; ++i) // a page switch or a zoom choice now and then: 2 a second for 30 s
            allowed += storm.allow (500.0 * i) ? 1 : 0;
        CHECK (allowed == 60);
        CHECK (storm.refusedCount() == 0);
    }
    {
        ResizeStorm storm;
        int allowed = 0;
        for (int i = 0; i < 12; ++i) // a short drag that resizes the editor 12 times in 200 ms
            allowed += storm.allow (100.0 + 16.0 * i) ? 1 : 0;
        CHECK (allowed == 12);
    }
    // the plugin window's editor wrapper asks it before it follows a resizable editor's own size change
    const auto src = i18ntest::readFile ("src/ui/PluginWindow.cpp");
    const auto at = src.find ("void childBoundsChanged (juce::Component* child) override\n    {\n        // round 190rk");
    CHECK (at != std::string::npos);
    CHECK (src.find ("resizeStorm.allow (juce::Time::getMillisecondCounterHiRes())", at) < src.find ("fitEditor();", at));
}
