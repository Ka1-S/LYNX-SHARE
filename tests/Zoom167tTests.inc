// Round 167t (issue #26): FL 2026's middle-drag zooms (core/EditorGestures.h). Evidence: docs/analise/REA_rodada167t.md.
// Included from tests.cpp.

TEST (zoom167t_key_height_range_reaches_fl)
{
    using namespace lynx::gesture;
    // FL: 16 + 2 px per zoom level, levels 1..50 -> 18..116 px; LYNX keeps its shorter keys too
    CHECK (maxKeyHeight == 16 + 2 * 50);
    CHECK (minKeyHeight <= 16 + 2 * 1);
}

TEST (zoom167t_vertical_drag_follows_fl_knob)
{
    using namespace lynx::gesture;
    HeightZoomDrag z;
    z.begin (300);
    CHECK (z.height (14) == 14);
    // 4 px up = one level = 2 px taller (normal speed: the keyboard's middle-drag)
    z.move (296, false);
    CHECK (z.levels() == 1);
    CHECK (z.height (14) == 16);
    // sideways moves do nothing: only y is passed; down = shorter
    z.move (304, false);
    CHECK (z.height (14) == 12);
    // Ctrl held (Ctrl+middle-drag on the notes): one level per 16 px
    z.begin (300);
    z.move (292, true);
    CHECK (z.levels() == 1); // 8 px fine = half a level, rounds up
    z.move (288, true);
    CHECK (z.levels() == 1);
    z.move (276, true);
    CHECK (z.levels() == 2);
    // mixed: Ctrl released mid-drag goes back to the normal speed
    z.move (272, false);
    CHECK (z.levels() == 3);
}

TEST (zoom167t_vertical_drag_clamps_height_not_travel)
{
    using namespace lynx::gesture;
    HeightZoomDrag z;
    z.begin (1000);
    z.move (0, false); // 1000 px up = 250 levels
    CHECK (z.height (14) == maxKeyHeight);
    // the travel is kept (FL's knob accumulator): coming back 100 px still leaves it at the top
    z.move (100, false);
    CHECK (z.height (14) == maxKeyHeight);
    z.begin (0);
    z.move (2000, false);
    CHECK (z.height (14) == minKeyHeight);
    // custom limits (the Playlist's track heights)
    z.begin (0);
    z.move (-40, false);
    CHECK (z.height (38, 8, 640) == 58);
}

TEST (zoom167t_time_zoom_drag)
{
    using namespace lynx::gesture;
    SpanZoomDrag z;
    z.begin (1000.0, 500.0, 0, 0);
    // 100 px up in small moves = twice the zoom (half the span), around the centre
    for (int y = -10; y >= -100; y -= 10)
        z.move (0, y, false);
    CHECK (std::abs (z.span - 500.0) < 1.0e-6);
    CHECK (std::abs (z.centre - 500.0) < 1.0e-9);
    // one jump counts 32 px at most
    SpanZoomDrag j;
    j.begin (1000.0, 500.0, 0, 0);
    j.move (0, -100, false);
    CHECK (std::abs (j.span - 1000.0 * std::pow (2.0, -0.32)) < 1.0e-6);
    // 100 px to the right moves the centre by half a view (later); with Ctrl it does not
    z.move (100, -100, false);
    CHECK (std::abs (z.centre - (500.0 + 0.5 * 500.0)) < 1.0e-6);
    z.move (200, -100, true);
    CHECK (std::abs (z.centre - 750.0) < 1.0e-6);
    // down zooms out, and the centre never goes before the start
    z.begin (100.0, 10.0, 0, 0);
    z.move (-500, 0, false);
    CHECK (z.centre == 0.0);
    for (int y = 10; y <= 100; y += 10)
        z.move (-500, y, false);
    CHECK (std::abs (z.span - 200.0) < 1.0e-6);
}
