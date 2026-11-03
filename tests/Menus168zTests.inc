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
    // holding most of the root menu, then the first one (JUCE lists the main display first)
    const std::vector<Box> overlapping { { 0, 0, 1920, 1080 }, { 0, 0, 2560, 1440 } };
    CHECK (displayFor (overlapping, { 1000, 600, 1200, 600 }) == 1);
    CHECK (displayFor (overlapping, { 100, 100, 400, 300 }) == 0);
    CHECK (displayFor ({ { 0, 0, 2560, 1440 }, { 0, 0, 1920, 1080 } }, { 100, 100, 400, 300 }) == 0);
    // a root menu outside every display: the nearest; no display at all: none
    CHECK (displayFor (displays, { 5000, 200, 100, 100 }) == 1);
    CHECK (displayFor ({}, root) == -1);
    CHECK (displayFor ({ { 0, 0, 0, 0 } }, root) == -1);
    // negative coordinates (a monitor on the left of the primary)
    CHECK (displayFor ({ { 0, 0, 1920, 1080 }, { -1920, 0, 1920, 1080 } }, { -900, 300, 300, 400 }) == 1);
}

// Round 168z2: the reporter's exact setup with its scale factors (KDE Plasma Wayland: 2560 x 1440 @ 115 % primary at
// the top left, 1920 x 1080 @ 100 % on its right, top-aligned, 1920 x 1080 @ 115 % below it, starting ~269 logical px in).
// LYNX (JUCE) talks X11 to XWayland, so the rectangles JUCE works with depend on KDE's "Legacy applications (X11)"
// setting and on JUCE's per-monitor scale; the menus must stay on the root menu's monitor in every variant.
TEST (menus168z2_reporter_layout_with_scales)
{
    using lynx::menuplace::Box;
    using lynx::menuplace::displayFor;
    using lynx::menuplace::placeSubmenu;
    // `k`: JUCE's logical coordinates = X pixels / k (k = JUCE's scale: the monitor scale it reads, times the UI zoom)
    const auto scaled = [] (std::vector<Box> d, double k) {
        for (auto& b : d)
            b = { (int) (b.x / k), (int) (b.y / k), (int) (b.w / k), (int) (b.h / k) };
        return d;
    };
    const std::vector<std::vector<Box>> layouts {
        // "Apply scaling themselves" (Plasma's default): X pixels = logical x 1.15, the 100 % monitor drawn 2208 x 1242
        { { 0, 0, 2560, 1440 }, { 2560, 0, 2208, 1242 }, { 309, 1440, 1920, 1080 } },
        // "Scaled by the system": X pixels = KDE's logical pixels
        { { 0, 0, 2226, 1252 }, { 2226, 0, 1920, 1080 }, { 269, 1252, 1670, 939 } },
        // JUCE reading 1 for the 27" monitors and 2 for a dense laptop panel below (round(dpi / 96) per output)
        { { 0, 0, 2560, 1440 }, { 2560, 0, 1920, 1080 }, { 309, 1440, 960, 540 } },
    };
    for (const auto zoom : { 1.0, 1.15, 1.25, 2.0 }) // LYNX's UI zoom (JUCE's global scale)
        for (const auto& physical : layouts)
        {
            const auto displays = scaled (physical, zoom);
            for (size_t m = 0; m < displays.size(); ++m)
            {
                const auto& screen = displays[m];
                // a window's two-column menu near the monitor's bottom right (Channel Rack of an undocked window)
                const auto rootW = std::min (800, screen.w / 2), rootH = std::min (640, screen.h * 2 / 3);
                const Box root { screen.x + screen.w - rootW - 10, screen.y + screen.h - rootH - 10, rootW, rootH };
                CHECK (displayFor (displays, root) == (int) m);
                // the chain: Automate MIDI controller > MIDI CC 0-31 > MIDI CC #nn, each one beside the previous
                Box parent = root;
                for (int level = 0; level < 3; ++level)
                {
                    const auto h = std::min (level == 1 ? 32 * 23 : 9 * 23, screen.h);
                    for (int row = 0; row * 23 + 23 <= parent.h; row += 7)
                    {
                        const Box item { parent.x + 4, parent.y + 4 + row * 23, parent.w - 8, 23 };
                        const auto s = placeSubmenu (parent, item, 380, h, 4, screen);
                        CHECK (s.x >= screen.x && s.x + s.w <= screen.x + screen.w);
                        CHECK (s.y >= screen.y && s.y + s.h <= screen.y + screen.h);
                    }
                    parent = placeSubmenu (parent, { parent.x + 4, parent.y + parent.h / 2, parent.w - 8, 23 }, 380, h, 4, screen);
                }
            }
        }
    // the phantom JUCE 8.0.4 made of a monitor touching no other (JUCE fix 9 now places it; Theme.cpp skips empty
    // ones): a root menu on the real right monitor is not pulled onto the primary by a 0,0 copy
    const std::vector<Box> withGap { { 0, 0, 2560, 1440 }, { 2562, 0, 2208, 1242 } };
    CHECK (displayFor (withGap, { 3000, 400, 600, 400 }) == 1);
}
