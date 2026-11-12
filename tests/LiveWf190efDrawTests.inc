// Round 190ef: Draw Mode (core/DrawMode.h): steps at the grid width and freehand lines drawn into automation points.
// docs/analise/REA_rodada190ef.md.

#include "core/DrawMode.h"

namespace live190efdraw
{
using lynx::AutomationPoint;
inline AutomationPoint pt (std::int64_t tick, float value, float tension = 0.0f, std::uint8_t mode = 0)
{
    AutomationPoint p;
    p.tick = tick;
    p.value = value;
    p.tension = tension;
    p.mode = mode;
    return p;
}
// a ramp 0.2 → 0.8 over four bars, then down to 0.4 at bar 8
inline std::vector<AutomationPoint> ramp()
{
    const std::int64_t bar = 4 * lynx::ppq;
    return { pt (0, 0.2f), pt (4 * bar, 0.8f), pt (8 * bar, 0.4f) };
}
inline bool sorted (const std::vector<AutomationPoint>& p)
{
    for (std::size_t i = 1; i < p.size(); ++i)
        if (p[i].tick < p[i - 1].tick)
            return false;
    return true;
}
// the old and new curves agree outside [from, to] (sampled every `every` ticks up to `end`)
inline bool sameOutside (const std::vector<AutomationPoint>& before, const std::vector<AutomationPoint>& after, std::int64_t from,
                         std::int64_t to, std::int64_t end, std::int64_t every = 7)
{
    for (std::int64_t t = 0; t <= end; t += every)
    {
        if (t >= from && t <= to)
            continue;
        if (std::abs (lynx::automationCurveAt (before, (double) t) - lynx::automationCurveAt (after, (double) t)) > 1.0e-4f)
            return false;
    }
    // just left of `from` and at / after `to`
    if (from > 0 && std::abs (lynx::automationCurveAt (before, (double) from - 0.5) - lynx::automationCurveAt (after, (double) from - 0.5)) > 1.0e-4f)
        return false;
    return std::abs (lynx::automationCurveAt (before, (double) to) - lynx::automationCurveAt (after, (double) to)) <= 1.0e-4f;
}
inline bool flatAt (const std::vector<AutomationPoint>& p, std::int64_t from, std::int64_t to, float v)
{
    for (auto t = from; t < to; t += std::max<std::int64_t> (1, (to - from) / 17))
        if (std::abs (lynx::automationCurveAt (p, (double) t) - v) > 1.0e-5f)
            return false;
    return std::abs (lynx::automationCurveAt (p, (double) to - 0.25) - v) <= 1.0e-5f;
}
} // namespace live190efdraw

TEST (live190ef_draw_cells_cover_the_drag_at_the_grid_width)
{
    using namespace lynx::drawmode;
    const auto q = (std::int64_t) lynx::ppq, s16 = q / 4, bar = 4 * q, t8 = q / 3; // a sixteenth, a bar, an eighth triplet
    auto cells = cellsBetween (s16 + 5, 3 * s16 + 1, s16);
    CHECK (cells.size() == 3);
    CHECK (cells.front() == (Cell { s16, 2 * s16 }) && cells.back() == (Cell { 3 * s16, 4 * s16 }));
    // the same cells right to left, still in time order
    CHECK (cellsBetween (3 * s16 + 1, s16 + 5, s16) == cells);
    // a move inside one cell: that cell
    CHECK (cellsBetween (bar + 10, bar + 900, bar) == std::vector<Cell> ({ { bar, 2 * bar } }));
    // triplet widths: 0..q covered by three eighth-triplet cells
    cells = cellsBetween (0, q - 1, t8);
    CHECK (cells.size() == 3 && cells[2].to == q);
    for (const auto& c : cells)
        CHECK (c.to - c.from == t8);
    // before the song: counted from 0; no grid: nothing
    CHECK (cellsBetween (-500, 10, s16).front().from == 0);
    CHECK (cellsBetween (0, 1000, 0).empty());
}

TEST (live190ef_draw_steps_along_a_move_take_the_mouse_value_over_each_cell)
{
    using namespace lynx::drawmode;
    const auto g = (std::int64_t) lynx::ppq;
    // one cell: the value the mouse has now
    auto steps = stepsAlong (10, 0.1f, 20, 0.9f, g);
    CHECK (steps.size() == 1 && std::abs (steps[0].value - 0.9f) < 1.0e-6f);
    // across four cells the values follow the line through the cells' middles
    steps = stepsAlong (0, 0.0f, 4 * g, 1.0f, g);
    CHECK (steps.size() == 5);
    CHECK (std::abs (steps[0].value - 0.125f) < 1.0e-5f && std::abs (steps[1].value - 0.375f) < 1.0e-5f);
    CHECK (std::abs (steps[4].value - 1.0f) < 1.0e-5f); // (its middle is past the mouse: the end value)
    // right to left the same line
    const auto back = stepsAlong (4 * g, 1.0f, 0, 0.0f, g);
    CHECK (back.size() == 5 && std::abs (back[1].value - 0.375f) < 1.0e-5f);
    // values are clamped
    steps = stepsAlong (0, 2.0f, 0, 2.0f, g);
    CHECK (steps.size() == 1 && steps[0].value == 1.0f);
    CHECK (stepsAlong (0, -1.0f, 5, -3.0f, g)[0].value == 0.0f);
}

TEST (live190ef_draw_a_step_is_flat_and_leaves_the_curve_outside_alone)
{
    using namespace live190efdraw;
    const std::int64_t bar = 4 * lynx::ppq, g = lynx::ppq / 4;
    const auto before = ramp();
    for (const auto from : { (std::int64_t) 0, 3 * g, bar, 4 * bar - g, 4 * bar, 6 * bar + 3 * g, 8 * bar, 9 * bar })
    {
        auto p = before;
        lynx::drawmode::drawStep (p, from, from + g, 0.65f);
        CHECK (sorted (p));
        CHECK (flatAt (p, from, from + g, 0.65f));
        CHECK (sameOutside (before, p, from, from + g, 10 * bar));
    }
}

TEST (live190ef_draw_adjacent_steps_and_a_redrawn_cell)
{
    using namespace live190efdraw;
    const std::int64_t bar = 4 * lynx::ppq, g = lynx::ppq / 4;
    const auto before = ramp();
    auto p = before;
    const float values[] = { 0.1f, 0.9f, 0.9f, 0.3f };
    for (int i = 0; i < 4; ++i)
        lynx::drawmode::drawStep (p, bar + i * g, bar + (i + 1) * g, values[i]);
    CHECK (sorted (p));
    for (int i = 0; i < 4; ++i)
        CHECK (flatAt (p, bar + i * g, bar + (i + 1) * g, values[i]));
    CHECK (sameOutside (before, p, bar, bar + 4 * g, 10 * bar));
    // the two equal neighbours are one flat run: no point is left in its middle
    const auto mid = bar + 2 * g;
    CHECK (std::none_of (p.begin(), p.end(), [mid] (const lynx::AutomationPoint& x) { return x.tick == mid; }));
    // drawing the same cell again replaces it
    const auto count = p.size();
    lynx::drawmode::drawStep (p, bar + 3 * g, bar + 4 * g, 0.5f);
    CHECK (flatAt (p, bar + 3 * g, bar + 4 * g, 0.5f) && flatAt (p, bar + 2 * g, bar + 3 * g, 0.9f));
    CHECK (p.size() == count && sorted (p));
    // drawing a cell at the value the curve already has adds no jump
    auto flat = std::vector<lynx::AutomationPoint> { pt (0, 0.5f), pt (bar, 0.5f) };
    lynx::drawmode::drawStep (flat, g, 2 * g, 0.5f);
    CHECK (flat.size() == 2);
}

TEST (live190ef_draw_a_bent_segment_cut_by_a_step_keeps_its_ends)
{
    using namespace live190efdraw;
    const std::int64_t bar = 4 * lynx::ppq;
    const std::vector<lynx::AutomationPoint> before { pt (0, 0.0f, 0.6f), pt (2 * bar, 1.0f), pt (3 * bar, 0.2f) };
    auto p = before;
    lynx::drawmode::drawStep (p, bar, bar + lynx::ppq, 0.3f);
    CHECK (sorted (p));
    // the values at the edges are the old ones (the cut piece's bend is approximate, as documented)
    CHECK (std::abs (lynx::automationCurveAt (p, (double) bar - 1.0e-3) - lynx::automationCurveAt (before, (double) bar)) < 1.0e-3f);
    CHECK (std::abs (lynx::automationCurveAt (p, (double) (bar + lynx::ppq)) - lynx::automationCurveAt (before, (double) (bar + lynx::ppq))) < 1.0e-5f);
    CHECK (std::abs (lynx::automationCurveAt (p, 2.5 * bar) - lynx::automationCurveAt (before, 2.5 * bar)) < 1.0e-5f);
}

TEST (live190ef_draw_freehand_replaces_the_points_it_passes)
{
    using namespace live190efdraw;
    const std::int64_t bar = 4 * lynx::ppq;
    const auto before = ramp();
    auto p = before;
    // a stroke of three moves, the middle one right to left would be odd: draw left to right, then one backwards
    lynx::drawmode::drawLine (p, bar, 0.1f, bar + 200, 0.4f);
    lynx::drawmode::drawLine (p, bar + 200, 0.4f, bar + 500, 0.2f);
    CHECK (sorted (p));
    CHECK (std::abs (lynx::automationCurveAt (p, (double) bar + 100.0) - 0.25f) < 1.0e-4f);
    CHECK (std::abs (lynx::automationCurveAt (p, (double) bar + 350.0) - 0.3f) < 1.0e-4f);
    CHECK (sameOutside (before, p, bar, bar + 500, 10 * bar));
    // the old point at 4 bars is gone when a stroke crosses it, and the curve after the stroke is the old one
    auto q = before;
    lynx::drawmode::drawLine (q, 5 * bar, 0.5f, 3 * bar, 0.5f); // (right to left)
    CHECK (std::none_of (q.begin(), q.end(), [bar] (const lynx::AutomationPoint& x) { return x.tick == 4 * bar; }));
    CHECK (flatAt (q, 3 * bar, 5 * bar, 0.5f));
    CHECK (sameOutside (before, q, 3 * bar, 5 * bar, 10 * bar));
    // a click without a move draws one tick; values are clamped
    auto r = before;
    lynx::drawmode::drawLine (r, 2 * bar, 3.0f, 2 * bar, 3.0f);
    CHECK (sorted (r) && lynx::automationCurveAt (r, 2.0 * bar) == 1.0f);
    CHECK (sameOutside (before, r, 2 * bar, 2 * bar + 1, 10 * bar));
}

TEST (live190ef_draw_into_an_empty_or_one_point_lane)
{
    using namespace live190efdraw;
    std::vector<lynx::AutomationPoint> one { pt (0, 0.7f) };
    const auto before = one;
    lynx::drawmode::drawStep (one, 960, 1920, 0.2f);
    CHECK (sorted (one) && flatAt (one, 960, 1920, 0.2f) && sameOutside (before, one, 960, 1920, 8000));
    // a step at the very start of the song needs no left edge
    std::vector<lynx::AutomationPoint> start { pt (0, 0.7f) };
    lynx::drawmode::drawStep (start, 0, 240, 0.1f);
    CHECK (start.front().tick == 0 && start.front().value == 0.1f && flatAt (start, 0, 240, 0.1f));
    CHECK (std::abs (lynx::automationCurveAt (start, 300.0) - 0.7f) < 1.0e-6f);
    // nothing to draw
    auto same = before;
    lynx::drawmode::drawStep (same, 500, 500, 0.3f);
    CHECK (same == before);
}

TEST (live190ef_draw_mode_switch_and_listeners)
{
    using namespace lynx::drawmode;
    set (false);
    int calls = 0;
    bool last = false;
    const auto id = addListener ([&] (bool on) { ++calls; last = on; });
    toggle();
    CHECK (isOn() && calls == 1 && last);
    set (true); // no change: no call
    CHECK (calls == 1);
    toggle();
    CHECK (! isOn() && calls == 2 && ! last);
    removeListener (id);
    toggle();
    CHECK (calls == 2);
    set (false);
}
