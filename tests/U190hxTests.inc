// Round 190hx (issue #178, "LYNX's own windows flicker while dragged"): a docked window keeps its size in pixels while it
// moves, at every interface size (core/DockStack.h physicalPlace).
#include "core/DockStack.h"

TEST (u190hx_a_docked_window_that_only_moves_keeps_its_size_in_pixels)
{
    using lynx::dockstack::physicalPlace;
    for (const auto scale : { 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 0.8, 0.9, 1.875 }) // (LYNX's sizes x display scales)
        for (const auto width : { 97, 100, 101, 333, 640 })
        {
            const auto first = physicalPlace (0, 0, width, 211, scale);
            for (int x = -40; x <= 400; ++x)
            {
                const auto r = physicalPlace (x, x / 3, width, 211, scale);
                CHECK (r.w == first.w && r.h == first.h);
                CHECK (std::abs (r.x - x * scale) <= 0.5 + 1e-9 && std::abs (r.y - (x / 3) * scale) <= 0.5 + 1e-9);
            }
        }
    // the reporter's case: 150 % interface, a 101-unit window at x = 0 and x = 1 (edge rounding gave 152 then 151 px)
    CHECK (physicalPlace (0, 0, 101, 50, 1.5).w == physicalPlace (1, 0, 101, 50, 1.5).w);
    // at 100 % nothing changes: the exact pixels
    const auto exact = physicalPlace (13, 27, 400, 300, 1.0);
    CHECK (exact.x == 13 && exact.y == 27 && exact.w == 400 && exact.h == 300);
}
