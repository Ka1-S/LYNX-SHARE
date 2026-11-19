// Round 190lw (issues #205, #206, #207): automation point selection / group move / delete / copy-paste and the thinned
// freehand pen (core/AutoPointEdit.h); the Piano roll's selected-row bands (core/RowBands.h); the mixer slot's second
// click (core/SecondClick.h).

#include "core/AutoPointEdit.h"
#include "core/RowBands.h"
#include "core/SecondClick.h"
#include "core/DrawMode.h"

namespace issues190lw
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
inline bool sorted (const std::vector<AutomationPoint>& p)
{
    for (std::size_t i = 1; i < p.size(); ++i)
        if (p[i].tick < p[i - 1].tick)
            return false;
    return true;
}
inline std::vector<AutomationPoint> five()
{
    return { pt (0, 0.1f), pt (100, 0.5f), pt (200, 0.9f), pt (300, 0.4f), pt (400, 0.2f) };
}
} // namespace issues190lw

TEST (issues190lw_rubber_band_selects_the_points_inside_either_way_round)
{
    using namespace issues190lw;
    using namespace lynx::autopoints;
    const auto p = five();
    CHECK (inBox (p, 90, 310, 0.3f, 1.0f) == std::vector<int> ({ 1, 2, 3 }));
    CHECK (inBox (p, 310, 90, 1.0f, 0.3f) == std::vector<int> ({ 1, 2, 3 }));
    CHECK (inBox (p, 90, 310, 0.6f, 1.0f) == std::vector<int> ({ 2 }));
    CHECK (inBox (p, 401, 900, 0.0f, 1.0f).empty());
    CHECK (inBox (p, 0, 0, 0.1f, 0.1f) == std::vector<int> ({ 0 })); // (edges count)
}

TEST (issues190lw_delete_selected_points_keeps_one_and_the_order)
{
    using namespace issues190lw;
    using namespace lynx::autopoints;
    auto p = five();
    CHECK (erase (p, { 3, 1, 1, 42, -1 }) == 2);
    CHECK (p.size() == 3 && p[0].tick == 0 && p[1].tick == 200 && p[2].tick == 400);
    auto all = five();
    CHECK (erase (all, { 0, 1, 2, 3, 4 }) == 4); // a curve keeps one point
    CHECK (all.size() == 1 && all[0].tick == 400);
    // the joined segment takes the curve of the segment that started at the deleted point (FL's delete)
    std::vector<AutomationPoint> bent { pt (0, 0.0f), pt (100, 0.5f, 0.6f, 2), pt (200, 1.0f) };
    CHECK (erase (bent, { 1 }) == 1);
    CHECK (bent.size() == 2 && bent[0].mode == 2 && std::abs (bent[0].tension - 0.6f) < 1.0e-6f);
}

TEST (issues190lw_group_move_keeps_shape_between_the_unselected_neighbours)
{
    using namespace issues190lw;
    using namespace lynx::autopoints;
    const auto p = five();
    // 1 and 2 moved right by 50 and up by 0.05: fine
    auto m = moved (p, { 1, 2 }, { 50, 0.05f });
    CHECK (m[1].tick == 150 && m[2].tick == 250 && std::abs (m[1].value - 0.55f) < 1.0e-6f && std::abs (m[2].value - 0.95f) < 1.0e-6f);
    CHECK (m[0].tick == 0 && m[3].tick == 300 && sorted (m));
    // too far right: stops at the next unselected point (300 - 200 = 100); too high: point 2 stops at 1.0 (shape kept)
    CHECK (clampMove (p, { 1, 2 }, { 500, 0.5f }).ticks == 100);
    CHECK (std::abs (clampMove (p, { 1, 2 }, { 500, 0.5f }).value - 0.1f) < 1.0e-5f);
    // too far left: point 1 stops at point 0's tick
    CHECK (clampMove (p, { 1, 2 }, { -500, -0.9f }).ticks == -100);
    CHECK (std::abs (clampMove (p, { 1, 2 }, { -500, -0.9f }).value + 0.5f) < 1.0e-6f);
    // the first point stays at tick >= 0
    CHECK (clampMove (p, { 0 }, { -50, 0.0f }).ticks == 0);
    // a selection with an unselected point inside it: each selected point stays between its own neighbours
    CHECK (clampMove (p, { 1, 3 }, { 500, 0.0f }).ticks == 100);
    CHECK (sorted (moved (p, { 1, 3 }, { 500, 0.0f })));
    CHECK (clampMove (p, {}, { 10, 0.1f }) == Delta {});
}

TEST (issues190lw_copy_and_paste_points_replaces_the_span_at_the_target)
{
    using namespace issues190lw;
    using namespace lynx::autopoints;
    auto p = five();
    const auto clip = copy (p, { 1, 2 });
    CHECK (clip.points.size() == 2 && clip.points[0].tick == 0 && clip.points[1].tick == 100 && clip.span == 100);
    const auto pasted = paste (p, clip, 350); // replaces the point at 400 (inside 350..450)
    CHECK (pasted == std::vector<int> ({ 4, 5 }));
    CHECK (p.size() == 6 && sorted (p) && p[4].tick == 350 && p[5].tick == 450);
    CHECK (std::abs (p[4].value - 0.5f) < 1.0e-6f && std::abs (p[5].value - 0.9f) < 1.0e-6f);
    CHECK (paste (p, Clipboard {}, 10).empty());
    // pasted at a negative tick: at 0
    auto q = five();
    CHECK (paste (q, clip, -40).front() == 0 && q[0].tick == 0);
}

TEST (issues190lw_freehand_stroke_is_thinned_but_keeps_its_shape)
{
    using namespace issues190lw;
    using namespace lynx::autopoints;
    // a pen stroke: a straight rise 0 → 1 over 0..1000, a mouse event every 10 ticks, then a fall back to 0 by 2000
    std::vector<AutomationPoint> points;
    for (std::int64_t t = 0; t <= 1000; t += 10)
        points.push_back (pt (t, (float) t / 1000.0f));
    for (std::int64_t t = 1010; t <= 2000; t += 10)
        points.push_back (pt (t, (float) (2000 - t) / 1000.0f));
    const auto before = points;
    const auto removed = thin (points, 0, 2000);
    CHECK (removed > 150);
    CHECK (points.size() == 3 && points[1].tick == 1000); // the corner stays
    for (std::int64_t t = 0; t <= 2000; t += 5)
        CHECK (std::abs (lynx::automationCurveAt (before, (double) t) - lynx::automationCurveAt (points, (double) t)) <= 0.0101f);

    // outside the range nothing changes; steps (two points on one tick) and bent points stay
    std::vector<AutomationPoint> s { pt (0, 0.2f), pt (50, 0.2f), pt (100, 0.2f), pt (100, 0.7f), pt (150, 0.7f), pt (200, 0.7f, 0.5f),
                                     pt (300, 0.7f), pt (400, 0.7f), pt (500, 0.7f) };
    thin (s, 0, 300);
    CHECK (sorted (s));
    CHECK (std::find_if (s.begin(), s.end(), [] (const AutomationPoint& p) { return p.tick == 50; }) == s.end());
    CHECK (std::count_if (s.begin(), s.end(), [] (const AutomationPoint& p) { return p.tick == 100; }) == 2);
    CHECK (std::find_if (s.begin(), s.end(), [] (const AutomationPoint& p) { return p.tick == 200; }) != s.end());
    CHECK (std::find_if (s.begin(), s.end(), [] (const AutomationPoint& p) { return p.tick == 400; }) != s.end());

    // a real pen stroke through Draw Mode's freehand line, then thinned
    std::vector<AutomationPoint> lane { pt (0, 0.5f), pt (4000, 0.5f) };
    float v = 0.5f;
    for (std::int64_t t = 1000; t < 2000; t += 12)
    {
        const auto next = 0.5f + 0.4f * std::sin ((float) (t - 1000) / 160.0f);
        lynx::drawmode::drawLine (lane, t, v, t + 12, next);
        v = next;
    }
    const auto drawn = lane.size();
    thin (lane, 1000, 2000);
    CHECK (lane.size() < drawn / 2 && lane.size() >= 10 && sorted (lane));
    CHECK (std::abs (lynx::automationCurveAt (lane, 3000.0) - 0.5f) < 1.0e-5f);
}

TEST (issues190lw_selected_note_rows_are_banded)
{
    using namespace lynx;
    std::vector<Note> notes (4);
    notes[0].key = 60;
    notes[1].key = 64;
    notes[2].key = 67;
    notes[3].key = 60;
    const auto bands = rowbands::of (notes, { 0, 2, 3, 9, -1 });
    CHECK (bands.test (60) && bands.test (67) && ! bands.test (64) && bands.count() == 2);
    CHECK (rowbands::of (notes, {}).none());
}

TEST (issues190lw_second_click_counts_only_near_and_soon)
{
    using namespace lynx::secondclick;
    Press first { 100, 200, 1000 };
    CHECK (isSecond (first, { 102, 199, 1300 }, 400));
    CHECK (! isSecond (first, { 102, 199, 1500 }, 400)); // too late
    CHECK (! isSecond (first, { 110, 200, 1100 }, 400)); // too far
    CHECK (! isSecond (first, { 100, 200, 900 }, 400));  // before it
    CHECK (! isSecond (Press {}, { 0, 0, 10 }, 400));    // no first press
}
