// Round 167p: issue #18 (submenus beside the parent menu's outer edge, level with their item, growing downwards) and
// issue #17 (MIDI Out knobs: cog top-right, name under the knob, three looks).
#include "core/MenuPlacement.h"
#include "core/MidiOutView.h"

TEST (polish167p_submenu_beside_outer_edge)
{
    using lynx::menuplace::Box;
    using lynx::menuplace::placeSubmenu;
    const Box screen { 0, 0, 1920, 1040 };
    // a two-column menu at x 195..525; "Insert" in the first column (right edge 515) at y 398
    const Box parent { 195, 170, 330, 470 }, insert { 200, 398, 315, 21 };
    // beside the OUTER edge (not over the second column), first item level with "Insert" (border 4), downwards
    CHECK ((placeSubmenu (parent, insert, 165, 375, 4, screen) == Box { 525, 394, 165, 375 }));
    // near the bottom of the screen: up only as much as needed (not bottom-aligned to the item like JUCE)
    CHECK ((placeSubmenu (parent, { 200, 900, 315, 21 }, 165, 375, 4, screen) == Box { 525, 665, 165, 375 }));
    // no room on the right: beside the left edge
    CHECK ((placeSubmenu ({ 1600, 100, 300, 400 }, { 1605, 200, 290, 21 }, 200, 300, 4, screen) == Box { 1400, 196, 200, 300 }));
    // taller than the screen: from its top
    CHECK ((placeSubmenu (parent, insert, 165, 1200, 4, screen).y == 0));
    // a second monitor on the left (negative x)
    CHECK ((placeSubmenu ({ -1500, 300, 300, 400 }, { -1495, 350, 290, 21 }, 200, 300, 4, { -1920, 0, 1920, 1040 }) == Box { -1200, 346, 200, 300 }));
}

TEST (polish167p_midiout_knob_cell)
{
    using namespace lynx::midiout::view;
    // the editor's cells (round 167p): 240 x 100
    for (const auto& cellBox : { Rect { 0, 0, 240, 100 }, Rect { 0, 0, 180, 80 }, Rect { 10, 20, 300, 120 } })
    {
        const auto c = layout (cellBox);
        CHECK (c.knob.w == c.knob.h);
        CHECK (c.knob.w >= 40);
        // the cog at the knob's top-right: right of its centre, above its centre, inside the cell
        CHECK (c.gear.x > c.knob.x + c.knob.w / 2);
        CHECK (c.gear.y + c.gear.h / 2 < c.knob.y + c.knob.h / 4);
        CHECK (c.gear.right() <= cellBox.right() && c.gear.y >= cellBox.y);
        // the name under the knob, never on the cog, and a gap before the next row
        CHECK (c.label.y >= c.knob.bottom());
        CHECK (! c.label.intersects (c.gear));
        CHECK (c.label.bottom() + rowGap <= cellBox.bottom());
    }
    lynx::midiout::Control c;
    CHECK (look (c) == Look::unassigned);
    CHECK (readout (c, 0).empty()); // no "0" inside an unset knob
    c.number = 74;
    CHECK (look (c) == Look::off);
    c.active = true;
    CHECK (look (c) == Look::sending);
    CHECK (readout (c, 64) == "64");
}
