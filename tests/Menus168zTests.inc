// Round 168z: issue #45 (multi-monitor Linux: a submenu went to another monitor and closed). Submenus stay on the
// display of their root menu, i.e. of the window the menu came from (core/MenuPlacement.h).
#include "core/MenuPlacement.h"

TEST (menus168z_submenu_display_is_the_root_menus)
{
    using lynx::menuplace::Box;
    using lynx::menuplace::displayFor;
    using lynx::menuplace::placeSubmenu;
    // the reporter's monitors: 2560 x 1440 primary, a 1920 x 1080 on its right, a 1920 x 1080 below it
    const std::vector<Box> displays { { 0, 0, 2560, 1440 }, { 2560, 60, 1920, 1080 }, { 310, 1440, 1920, 1080 } };
    // the Channel Rack's two-column menu on the primary
    const Box root { 300, 380, 800, 640 };
    CHECK (displayFor (displays, root) == 0);
    const auto primary = displays[0];
    // the MIDI CC #0..#31 submenu: tall, near the primary's right edge, reaching below the primary
    const Box ccList { 1700, 10, 380, 1520 };
    // CC #22's submenu (650 x 320): beside the CC list's LEFT edge (no room on the right), inside the primary,
    // never on the monitor on the right or below
    const auto sub = placeSubmenu (ccList, { 1705, 1250, 370, 23 }, 650, 320, 4, primary);
    CHECK ((sub == Box { 1050, 1120, 650, 320 }));
    // any item of the CC list, any submenu size that fits: always inside the root menu's display
    for (int itemY = 10; itemY < 1500; itemY += 23)
        for (const auto [w, h] : { std::pair { 650, 320 }, std::pair { 200, 900 }, std::pair { 1200, 200 } })
        {
            const auto s = placeSubmenu (ccList, { 1705, itemY, 370, 23 }, w, h, 4, primary);
            CHECK (s.x >= primary.x && s.x + s.w <= primary.x + primary.w);
            CHECK (s.y >= primary.y && s.y + s.h <= primary.y + primary.h);
        }

    // a menu opened from a window on the right monitor stays there
    CHECK (displayFor (displays, { 3000, 300, 500, 400 }) == 1);
    const auto right = displays[1];
    const auto s = placeSubmenu ({ 4100, 300, 350, 600 }, { 4105, 500, 340, 21 }, 400, 300, 4, right);
    CHECK (s.x >= right.x && s.x + s.w <= right.x + right.w && s.y >= right.y && s.y + s.h <= right.y + right.h);
    CHECK (s.x == 4100 - 400);
    // ... and from the one below
    CHECK (displayFor (displays, { 400, 1500, 600, 500 }) == 2);
    // a root menu across two monitors: the one with its centre
    CHECK (displayFor (displays, { 2300, 400, 600, 300 }) == 1);

    // a confused display list (overlapping rectangles, as XWayland with mixed scale factors may report): the display
    // holding most of the root menu, then the smaller one
    const std::vector<Box> overlapping { { 0, 0, 1920, 1080 }, { 0, 0, 2560, 1440 } };
    CHECK (displayFor (overlapping, { 1000, 600, 1200, 600 }) == 1);
    CHECK (displayFor (overlapping, { 100, 100, 400, 300 }) == 0);
    // a root menu outside every display: the nearest; no display at all: none
    CHECK (displayFor (displays, { 5000, 200, 100, 100 }) == 1);
    CHECK (displayFor ({}, root) == -1);
    CHECK (displayFor ({ { 0, 0, 0, 0 } }, root) == -1);
    // negative coordinates (a monitor on the left of the primary)
    CHECK (displayFor ({ { 0, 0, 1920, 1080 }, { -1920, 0, 1920, 1080 } }, { -900, 300, 300, 400 }) == 1);
}
