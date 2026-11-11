// --- Round 189g: FL's Playlist Edit / Select options: Magic lasso, Allow resizing from left, wheel nudging ----------
// (FLEngine 2026 [DECOMP] FUN_00e6ff80 / FUN_00e6f330 magic lasso, FUN_00e8e840 left resizing, FUN_00e82270 nudging;
// REA notes in /mnt/e/lynx-work/flgap189g/NOTES.md)

#include "core/ClipOps.h"
#include "core/EditorGestures.h"
#include "core/KeyMap.h"
#include "core/MagicLasso.h"

namespace
{
// a circle of `n` trail points around (cx, cy), one every 20 ms
std::vector<lynx::magiclasso::Point> lasso189gCircle (double cx, double cy, double radius, int n)
{
    std::vector<lynx::magiclasso::Point> pts;
    for (int i = 0; i < n; ++i)
    {
        const auto a = 6.283185307179586 * (double) i / (double) (n - 4); // a little past one turn
        pts.push_back ({ cx + radius * std::cos (a), cy + radius * std::sin (a) });
    }
    return pts;
}
} // namespace

TEST (playlist189g_magic_lasso_closes_a_loop_and_ignores_pauses)
{
    using namespace lynx::magiclasso;
    Trail trail;
    std::vector<Point> loop;
    std::uint32_t ms = 1000;
    for (const auto& p : lasso189gCircle (200.0, 100.0, 80.0, 30))
        if (auto l = trail.move (p, ms += 20, false); ! l.empty() && loop.empty())
            loop = l;
    CHECK (loop.size() >= 10);
    CHECK (insidePolygon (loop, { 200.0, 100.0 }));
    CHECK (! insidePolygon (loop, { 400.0, 100.0 }));

    // the same circle with a pause of more than 200 ms half way never closes
    Trail paused;
    auto closed = false;
    ms = 5000;
    const auto pts = lasso189gCircle (200.0, 100.0, 80.0, 30);
    for (std::size_t i = 0; i < pts.size(); ++i)
        closed = ! paused.move (pts[i], ms += (i == 15 ? 250u : 20u), false).empty() || closed;
    CHECK (! closed);

    // a button down (a drag) never draws a trail
    Trail dragged;
    closed = false;
    for (const auto& p : pts)
        closed = ! dragged.move (p, ms += 20, true).empty() || closed;
    CHECK (! closed && dragged.getPoints().size() <= 1);
}

TEST (playlist189g_magic_lasso_needs_the_clip_middle_inside)
{
    using namespace lynx::magiclasso;
    const std::vector<Point> square { { 0, 0 }, { 300, 0 }, { 300, 200 }, { 0, 200 } };
    // FL's inset: 200 x 40 px clip -> 24 px at the sides ((200 - 6) / 8), 8 px top and bottom
    const auto inner = innerRect ({ 50, 50, 250, 90 });
    CHECK (inner.left == 74 && inner.right == 226 && inner.top == 58 && inner.bottom == 82);
    CHECK (rectInsidePolygon (square, inner));
    // a clip sticking out on the left, its middle still inside
    CHECK (rectInsidePolygon (square, innerRect ({ -10, 50, 190, 90 })));
    // half out: not inside
    CHECK (! rectInsidePolygon (square, innerRect ({ 200, 50, 400, 90 })));
    // a U-shaped trail whose notch cuts through the clip: not inside
    const std::vector<Point> notch { { 0, 0 }, { 300, 0 }, { 300, 200 }, { 160, 200 }, { 160, 60 }, { 140, 60 }, { 140, 200 }, { 0, 200 } };
    CHECK (! rectInsidePolygon (notch, innerRect ({ 50, 50, 250, 90 })));
}

TEST (playlist189g_magic_lasso_selects_two_clips_or_zooms_to_the_selection)
{
    using namespace lynx::magiclasso;
    // nothing selected: two enclosed clips are selected, one is not enough without Shift
    CHECK (decide ({ 3, 4 }, false, 0, { false, false }) == Result::select);
    CHECK (decide ({ 3 }, false, 0, { false }) == Result::nothing);
    CHECK (decide ({ 3 }, true, 2, { false }) == Result::select);
    // a selection: circling exactly the selected clips zooms to them; anything else does nothing
    CHECK (decide ({ 3, 4 }, false, 2, { true, true }) == Result::zoomToSelection);
    CHECK (decide ({ 3, 4 }, false, 3, { true, true }) == Result::nothing);
    CHECK (decide ({ 3, 4 }, false, 2, { true, false }) == Result::nothing);
}

TEST (playlist189g_left_resizing_off_makes_the_left_edge_move_the_clip)
{
    using namespace lynx;
    gesture::EdgeRules rules;
    rules.minimumForLeft = 24.0;
    CHECK (gesture::zoneAt (101.0, 100.0, 300.0, rules) == gesture::Zone::leftEdge);
    rules.allowLeft = false; // FL's Edit › "Allow resizing from left" off (FUN_00e8e840: zone 7, a move)
    CHECK (gesture::zoneAt (101.0, 100.0, 300.0, rules) == gesture::Zone::body);
    CHECK (gesture::zoneAt (299.0, 100.0, 300.0, rules) == gesture::Zone::rightEdge);
}

TEST (playlist189g_left_resizing_key_is_fl_ctrl_alt_home)
{
    using namespace lynx;
    keys::KeyMap map;
    CHECK (map.find (keys::Chord { keys::home, (std::uint8_t) (keys::ctrl | keys::alt) }, keys::Context::playlist, false) == "playlist.leftResizing");
    for (const char* id : { "playlist.makeUniqueAsSample", "playlist.wheelNudging", "playlist.manualFades", "playlist.snapFadeHandles", "playlist.magicLasso" })
        CHECK (keys::findAction (id) != nullptr);
}

TEST (playlist189g_wheel_nudge_shifts_the_content_not_the_clip)
{
    using namespace lynx;
    std::vector<Clip> clips (3);
    clips[0].start = 384; clips[0].length = 384; clips[0].offset = 10;
    clips[1].start = 768; clips[1].length = 384; clips[1].offset = 50;
    clips[2].start = 0;   clips[2].length = 96;  clips[2].offset = 0;
    // up: the content moves earlier, the clips stay
    CHECK (clipops::shiftClipContent (clips, { 0, 1 }, 35) == 35);
    CHECK (clips[0].offset == 45 && clips[1].offset == 85 && clips[0].start == 384 && clips[1].start == 768 && clips[2].offset == 0);
    // down: held at the first offset reaching 0
    CHECK (clipops::shiftClipContent (clips, { 0, 1 }, -100) == -45);
    CHECK (clips[0].offset == 0 && clips[1].offset == 40);
}
