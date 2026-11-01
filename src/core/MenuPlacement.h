#pragma once

// Where a popup submenu goes (round 167p, issue #18). Pure logic, no JUCE (unit-tested in tests/tests.cpp).
//
// JUCE puts a submenu right beside the item that opened it. In a two-column menu (the Channel Rack's channel menu,
// the Playlist's, the Mixer's, the Piano Roll's) an item of the first column then opens its submenu OVER the second
// column, and JUCE grows it upwards whenever the item is in the lower half of the screen, far from the item. LYNX puts
// it beside the parent menu's outer edge instead, its first item level with the item that opened it, growing
// downwards; it moves up only as much as the screen needs, and goes to the parent's left when the right has no room.

#include <algorithm>

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
    else if (parent.x - w >= screen.x)
        x = parent.x - w;                  // no room on the right: beside the left edge
    else
        x = std::max (screen.x, right - w); // no room on either side: as far right as it fits
    auto y = item.y - border;               // first item level with the item
    if (y + h > bottom)
        y = bottom - h;                     // up only as much as the screen needs
    y = std::max (y, screen.y);
    return { x, y, w, h };
}
} // namespace lynx::menuplace
