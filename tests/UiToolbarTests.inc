// UI toolbar round: the user's toolbar layout (core/ToolbarLayout.h). Included at the end of tests.cpp.

#include "core/ToolbarLayout.h"

TEST (toolbar_layout_default_roundtrip)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    CHECK ((int) l.entries.size() == groupCount);
    CHECK (l.isDefault());
    CHECK (l.orderToString() == "-link,transport,master,record,snap,windows,-multilink,file,-key,chord,meters,output"); // (177b-4: hidden by default; 190gl: the Link toggle, hidden in FL's layout)

    l.setShown (Group::meters, false);
    l.move (Group::output, -(groupCount - 1)); // to the front
    CHECK (l.entries.front().group == Group::output);
    CHECK (! l.isDefault());

    Layout back = Layout::makeDefault();
    back.orderFromString (l.orderToString());
    CHECK (back.orderToString() == l.orderToString());
    CHECK (! back.isShown (Group::meters) && back.isShown (Group::output));
}

TEST (toolbar_layout_parse_tolerant)
{
    using namespace lynx::toolbarlayout;
    Layout l;
    // unknown ids ignored, duplicates keep the first, missing groups come back shown at their default place
    l.orderFromString (" snap , -record,bogus,snap,transport\r");
    CHECK ((int) l.entries.size() == groupCount);
    CHECK (l.entries[0].group == Group::snap);
    CHECK (! l.isShown (Group::record) && l.indexOf (Group::record) < l.indexOf (Group::transport));
    CHECK (l.isShown (Group::master) && l.isShown (Group::output));
    CHECK (l.indexOf (Group::master) == l.indexOf (Group::transport) + 1); // after its default predecessor
    CHECK (l.indexOf (Group::windows) == l.indexOf (Group::snap) + 1);
    CHECK (l.indexOf (Group::output) == l.indexOf (Group::meters) + 1);

    l.orderFromString ("");
    CHECK (l.orderToString() == Layout::makeDefault().orderToString());

    CHECK (Layout::iconSizeFromString ("large") == IconSize::large);
    CHECK (Layout::iconSizeFromString ("nonsense") == IconSize::medium);
    CHECK (clampScale (10) == 75 && clampScale (1000) == 250 && clampScale (125) == 125);
}

TEST (toolbar_layout_move_clamps)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    CHECK (! l.move (Group::transport, -1)); // already first
    CHECK (l.move (Group::transport, 1));
    CHECK (l.entries[0].group == Group::master && l.entries[1].group == Group::transport);
    CHECK (! l.move (Group::output, 3));
}

TEST (toolbar_layout_fit_drops_by_priority)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    std::array<int, groupCount> widths;
    widths.fill (100);
    // room for everything
    auto v = fit (l, widths, 10, groupCount * 110 + 300, 300);
    for (int g = 0; g < groupCount; ++g)
        CHECK (v[(std::size_t) g] == l.isShown ((Group) g)); // every shown group (the Multilink one is hidden by default)
    // three groups too many: chord (45), output (50) and meters (55) drop first
    v = fit (l, widths, 10, 6 * 110 + 300, 300);
    CHECK (! v[(std::size_t) Group::output] && ! v[(std::size_t) Group::meters] && v[(std::size_t) Group::record]);
    // nothing fits: only the transport stays
    v = fit (l, widths, 10, 50, 300);
    CHECK (v[(std::size_t) Group::transport] && std::count (v.begin(), v.end(), true) == 1);
    // a hidden group stays hidden even with room
    l.setShown (Group::snap, false);
    v = fit (l, widths, 10, 5000, 0);
    CHECK (! v[(std::size_t) Group::snap]);
}

TEST (toolbar_icon_sizes_grow)
{
    using namespace lynx::toolbarlayout;
    CHECK (rowHeight (IconSize::small) < rowHeight (IconSize::medium) && rowHeight (IconSize::medium) < rowHeight (IconSize::large));
    // medium: 19 px icons with room around them (round 170e3; 16 px read as tiny at 1920)
    CHECK (std::min ((float) rowHeight (IconSize::medium) * iconFraction (IconSize::medium), iconMaxPx (IconSize::medium)) == 19.0f);
    CHECK (iconMaxPx (IconSize::large) <= (float) rowHeight (IconSize::large) * 0.62f);
    CHECK (toolIconPx (IconSize::small) < toolIconPx (IconSize::large));
    CHECK (toolButtonWidth (IconSize::large, 22) > 22);
}
