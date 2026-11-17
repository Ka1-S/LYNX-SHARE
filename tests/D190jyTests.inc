// Round 190jy (queue item 44): docked LYNX editors drawn by the main window keep their place among the native panels and
// docked plugin windows through a hidden anchor window; the native windows below an anchor get holes where the editor
// is (core/DockStack.h editorsAbove / holesIn / editorRect).
#include "core/DockStack.h"

TEST (d190jy_native_windows_below_an_editor_anchor_get_its_hole)
{
    using lynx::dockstack::editorsAbove;
    // bottom up: Playlist (P), anchor of editor 0 (A0), Channel rack (R), anchor of editor 1 (A1), plugin window (W)
    const std::vector<int> windows { 'P', 'A' * 100 + 0, 'R', 'A' * 100 + 1, 'W' };
    const auto anchorOf = [] (int w) { return w >= 'A' * 100 ? w - 'A' * 100 : -1; };
    const auto both = editorsAbove (windows, anchorOf, [] (int) { return true; });
    CHECK (both.size() == windows.size());
    CHECK ((both[0] == std::vector<int> { 0, 1 })); // the Playlist is below both editors (the nearest first)
    CHECK (both[1].empty());                        // (an anchor gets nothing)
    CHECK ((both[2] == std::vector<int> { 1 }));    // the rack was raised above editor 0: it covers it
    CHECK (both[3].empty());
    CHECK (both[4].empty());                        // the plugin window in front of everything
    // a hidden editor makes no holes
    const auto hidden = editorsAbove (windows, anchorOf, [] (int e) { return e != 1; });
    CHECK ((hidden[0] == std::vector<int> { 0 }));
    CHECK (hidden[2].empty());
    // no editors at all: nothing anywhere
    const std::vector<int> plain { 'P', 'R' };
    for (const auto& h : editorsAbove (plain, anchorOf, [] (int) { return true; }))
        CHECK (h.empty());
}

TEST (d190jy_holes_are_the_editor_rects_clipped_to_each_window)
{
    using lynx::dockstack::holesIn;
    using lynx::dockstack::PhysicalRect;
    const PhysicalRect panel { 100, 50, 400, 300 };
    // inside, partly outside, wholly outside
    const auto holes = holesIn (panel, { { 150, 80, 100, 60 }, { 450, 300, 200, 200 }, { 600, 10, 50, 50 } });
    CHECK (holes.size() == 2);
    CHECK (holes[0].x == 50 && holes[0].y == 30 && holes[0].w == 100 && holes[0].h == 60); // (the window's own coordinates)
    CHECK (holes[1].x == 350 && holes[1].y == 250 && holes[1].w == 50 && holes[1].h == 50);
    // touching edges only: no hole
    CHECK (holesIn (panel, { { 500, 50, 10, 10 }, { 100, 350, 10, 10 } }).empty());
}

TEST (d190jy_an_editor_rect_is_rounded_edge_by_edge_as_its_clip)
{
    using lynx::dockstack::editorRect;
    // 100 %: exact
    const auto exact = editorRect (13, 27, 400, 300, 1.0);
    CHECK (exact.x == 13 && exact.y == 27 && exact.w == 400 && exact.h == 300);
    // 150 %: each edge to the nearest pixel (101 units from x = 1: 1.5 -> 2, 153 -> 153: 151 px)
    const auto r = editorRect (1, 0, 101, 50, 1.5);
    CHECK (r.x == 2 && r.w == 151 && r.y == 0 && r.h == 75);
    // the edges never leave a gap or an overlap between two editors side by side
    for (const auto scale : { 1.0, 1.25, 1.5, 1.75, 2.0 })
        for (int x = 0; x < 40; ++x)
        {
            const auto a = editorRect (x, 0, 37, 10, scale), b = editorRect (x + 37, 0, 20, 10, scale);
            CHECK (a.x + a.w == b.x);
        }
}
