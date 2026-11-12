#pragma once

// Where a popup submenu goes (round 167p, issue #18). Pure logic, no JUCE (unit-tested in tests/tests.cpp).
//
// JUCE puts a submenu right beside the item that opened it. In a two-column menu (the Channel Rack's channel menu,
// the Playlist's, the Mixer's, the Piano Roll's) an item of the first column then opens its submenu OVER the second
// column, and JUCE grows it upwards whenever the item is in the lower half of the screen, far from the item. LYNX puts
// it beside the parent menu's outer edge instead, its first item level with the item that opened it, growing
// downwards; it moves up only as much as the screen needs, and goes to the left (of the parent, or of the item's column
// when that is not the first one) when the right has no room.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/WindowFit.h"

namespace lynx::menuplace
{
using Box = windowfit::Box;

// `parent`: the parent menu window; `item`: the item that opened the submenu (screen coordinates); `w` x `h`: the
// submenu window's size; `border`: the menu windows' border (the submenu's first item sits `border` px below its top);
// `screen`: the usable screen area.
inline Box placeSubmenu (Box parent, Box item, int w, int h, int border, Box screen)
{
    const auto right = screen.x + screen.w, bottom = screen.y + screen.h;
    int x;
    if (parent.x + parent.w + w <= right)
        x = parent.x + parent.w;           // beside the outer edge (the usual case)
    else if (const auto left = item.x - parent.x > border + item.w / 2 ? item.x : parent.x; left - w >= screen.x)
        // no room on the right: beside the left edge of the parent, or of the item's own column when the item is not
        // in the first one (round 190cj: the Mixer's "Disk recording", in the second column, had its submenu on the
        // far side of the first column, 700 px from the item; over the first column it is next to its item, as a
        // system menu's)
        x = left - w;
    else
        x = std::max (screen.x, right - w); // no room on either side: as far right as it fits
    auto y = item.y - border;               // first item level with the item
    if (y + h > bottom)
        y = bottom - h;                     // up only as much as the screen needs
    y = std::max (y, screen.y);
    return { x, y, w, h };
}

// Which display a menu chain lives on (issue #45, round 168z): the display of the ROOT menu (the menu that opened from
// the window), so every submenu stays on the monitor of the window it came from. JUCE took the display under the item
// that opened the submenu; a tall submenu (MIDI CC #0..#31) that reached into the monitor below, or a confused X
// server display list (XWayland with mixed scale factors), sent the next submenu to another monitor, where it closed
// as soon as the mouse left its parent menu.
// The display that contains the root menu's centre; several (overlapping rectangles from the X server): the one that
// holds most of the root menu, then the first (JUCE lists the main display first); none: the one that holds most of
// it; none at all: the nearest. Empty rectangles are skipped. -1 when there is no display.
inline int displayFor (const std::vector<Box>& displays, Box root)
{
    const auto cx = (std::int64_t) root.x + root.w / 2, cy = (std::int64_t) root.y + root.h / 2;
    const auto overlap = [&root] (const Box& d) -> std::int64_t {
        const auto w = std::min ((std::int64_t) root.x + root.w, (std::int64_t) d.x + d.w) - std::max (root.x, d.x);
        const auto h = std::min ((std::int64_t) root.y + root.h, (std::int64_t) d.y + d.h) - std::max (root.y, d.y);
        return w > 0 && h > 0 ? w * h : 0;
    };
    const auto distance = [cx, cy] (const Box& d) -> std::int64_t {
        const auto dx = std::max<std::int64_t> ({ (std::int64_t) d.x - cx, 0, cx - ((std::int64_t) d.x + d.w - 1) });
        const auto dy = std::max<std::int64_t> ({ (std::int64_t) d.y - cy, 0, cy - ((std::int64_t) d.y + d.h - 1) });
        return dx * dx + dy * dy;
    };
    int best = -1;
    // rank: contains the centre, then most overlap, then nearest (ties: the first)
    const auto better = [&] (const Box& a, const Box& b) {
        const auto ca = distance (a) == 0, cb = distance (b) == 0;
        if (ca != cb)
            return ca;
        if (overlap (a) != overlap (b))
            return overlap (a) > overlap (b);
        return distance (a) < distance (b);
    };
    for (int i = 0; i < (int) displays.size(); ++i)
        if (displays[(size_t) i].w > 0 && displays[(size_t) i].h > 0 && (best < 0 || better (displays[(size_t) i], displays[(size_t) best])))
            best = i;
    return best;
}
} // namespace lynx::menuplace
