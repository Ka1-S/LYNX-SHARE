// Round 170e (Busy Works review #17): trackpad zoom and scrolling — Windows touchpad pieces of a wheel notch become
// smooth deltas of the same size, a pinch zooms by its own factor (limited per event).

#include <limits>

#include "core/EditorGestures.h"

TEST (toolbar170e_windows_touchpad_wheel_pieces_scale_with_the_fingers)
{
    using namespace lynx::gesture;
    const auto notch = windowsWheelNotch; // 120 wheel units in JUCE's units on Windows

    // a notched wheel is unchanged (one notch = one step), on Windows and elsewhere
    for (const auto windows : { true, false })
    {
        const auto w = normaliseWheel (0.0, notch, false, windows);
        CHECK (! w.smooth && w.y == notch);
        CHECK (wheelZoomFactor (w.y, w.smooth) == 1.2);
    }
    // macOS / Linux small notched deltas (a classic mouse reports ~0.04 per notch on a Mac) stay one notch each
    CHECK (! normaliseWheel (0.0, 0.04, false, false).smooth);
    CHECK (wheelZoomFactor (normaliseWheel (0.0, 0.04, false, false).y, false) == 1.2);

    // Windows: a quarter notch (a precision touchpad piece, also a pinch sent as Ctrl+wheel) zooms a quarter step
    {
        const auto w = normaliseWheel (0.0, notch / 4.0, false, true);
        CHECK (w.smooth);
        CHECK (std::abs (wheelZoomFactor (w.y, w.smooth) - std::pow (1.2, 0.25)) < 1e-9);
        CHECK (std::abs (wheelScrollPixels (w.y, w.smooth, 80.0) + 20.0) < 1e-9); // a quarter of 80 px, upwards
    }
    // four quarter pieces zoom exactly as one notch
    {
        auto factor = 1.0;
        for (int i = 0; i < 4; ++i)
        {
            const auto w = normaliseWheel (0.0, -notch / 4.0, false, true);
            factor *= wheelZoomFactor (w.y, w.smooth);
        }
        CHECK (std::abs (factor - 1.0 / 1.2) < 1e-9);
    }
    // a sideways touchpad piece scrolls sideways in proportion
    {
        const auto w = normaliseWheel (notch / 2.0, 0.0, false, true);
        CHECK (w.smooth && w.y == 0.0);
        CHECK (std::abs (wheelScrollPixels (w.x, w.smooth, 80.0) + 40.0) < 1e-9);
    }
    // smooth events (macOS trackpads) pass through untouched
    {
        const auto w = normaliseWheel (0.01, -0.02, true, true);
        CHECK (w.smooth && w.x == 0.01 && w.y == -0.02);
    }
    CHECK (normaliseWheel (0.0, 0.0, false, true).smooth == false);
}

TEST (toolbar170e_pinch_zoom_factor)
{
    using namespace lynx::gesture;
    CHECK (pinchZoomFactor (1.0) == 1.0);
    CHECK (pinchZoomFactor (1.05) == 1.05);
    CHECK (pinchZoomFactor (0.9) == 0.9);
    CHECK (pinchZoomFactor (10.0) == 2.0);   // one event never jumps more than x2
    CHECK (pinchZoomFactor (0.01) == 0.5);
    CHECK (pinchZoomFactor (0.0) == 1.0);    // a broken event does nothing
    CHECK (pinchZoomFactor (-1.0) == 1.0);
    CHECK (pinchZoomFactor (std::numeric_limits<double>::infinity()) == 1.0);
    // a pinch out and back in returns to the same zoom, around the same anchor
    const auto scroll = 960.0, ppt = 0.25, anchor = 300.0;
    const auto zoomed = ppt * pinchZoomFactor (1.25);
    const auto s1 = zoomAnchored (scroll, ppt, zoomed, anchor);
    const auto s2 = zoomAnchored (s1, zoomed, zoomed * pinchZoomFactor (0.8), anchor);
    CHECK (std::abs (s2 - scroll) < 1e-9);
}
