// Round 190cj (QA 190cb N3): a submenu with no room on the right opens beside its item's column, not a column away.
#include "core/MenuPlacement.h"

TEST (dancefix190cj_submenu_next_to_its_item)
{
    using lynx::menuplace::Box;
    using lynx::menuplace::placeSubmenu;
    const Box screen { 0, 0, 1920, 1040 };
    // the Mixer's insert menu in a long language: two columns of 520 at x 760..1808; "Disk recording" in the second
    // column (x 1284); its submenu is 560 wide, so the right has no room (1808 + 560 > 1920)
    const Box parent { 760, 120, 1048, 700 }, item { 1284, 610, 520, 21 };
    const auto sub = placeSubmenu (parent, item, 560, 260, 4, screen);
    // it ends where its item's column begins (it was at x 200: the far side of the first column, 524 px from the item)
    CHECK (sub.x + sub.w == item.x);
    CHECK ((sub == Box { 724, 606, 560, 260 }));
    // an item of the first column: beside the parent's left edge, as before
    CHECK ((placeSubmenu (parent, { 764, 610, 516, 21 }, 560, 260, 4, screen) == Box { 200, 606, 560, 260 }));
    // room on the right: beside the outer edge whatever the column (issue #18's rule stays)
    CHECK ((placeSubmenu (parent, item, 100, 260, 4, screen).x == 1808));
    CHECK ((placeSubmenu (parent, { 764, 610, 516, 21 }, 100, 260, 4, screen).x == 1808));
    // no room at the item's left either: as far right as it fits, on the screen
    const auto wide = placeSubmenu (parent, item, 1500, 260, 4, screen);
    CHECK (wide.x == 420 && wide.x + wide.w <= 1920);
    // every case stays on the screen
    for (int w = 100; w <= 1900; w += 150)
        for (int x = 764; x <= 1284; x += 520)
        {
            const auto s = placeSubmenu (parent, { x, 610, 516, 21 }, w, 260, 4, screen);
            CHECK (s.x >= 0 && s.x + s.w <= 1920);
        }
}
