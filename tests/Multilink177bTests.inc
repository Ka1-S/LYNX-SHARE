// Round 177b-4: FL's "Multilink to controllers" (core/Multilink.h): the controls moved while it is on link to the
// controllers moved next, in the same order; one controller (CC and channel) per target; it ends when all are linked.
TEST (multilink177b_links_in_the_order_moved)
{
    lynx::MultilinkSession m;
    const lynx::AutomationTarget a { lynx::AutomationTargetKind::mixerVolume, 1, 0, 0 };
    const lynx::AutomationTarget b { lynx::AutomationTargetKind::mixerPan, 2, 0, 0 };
    const lynx::AutomationTarget c { lynx::AutomationTargetKind::tempo, 0, 0, 0 };
    CHECK (! m.controller (10, 0).has_value()); // nothing queued: a controller links nothing
    CHECK (m.add (a) && m.add (b));
    CHECK (! m.add (a));                        // moved again: keeps its place
    CHECK (m.add (c));
    CHECK (! m.add ({}));                       // (no target)
    CHECK (m.targets.size() == 3 && ! m.complete());

    const auto first = m.controller (21, 0);
    CHECK (first && first->target == a && first->cc == 21 && first->channel == 0);
    CHECK (! m.controller (21, 0).has_value()); // the same knob turning on: no new link
    const auto second = m.controller (21, 1);   // the same CC on another channel is another controller
    CHECK (second && second->target == b && second->channel == 1);
    CHECK (! m.complete());
    const auto third = m.controller (7, 0);
    CHECK (third && third->target == c && m.complete());
    CHECK (! m.controller (8, 0).has_value()); // all linked
}

TEST (multilink177b_switch_menu_and_toolbar)
{
    // the switch's "Link to controller" / "Override global links": no more controls are queued, the controllers link
    lynx::MultilinkSession m;
    const lynx::AutomationTarget a { lynx::AutomationTargetKind::mixerVolume, 1, 0, 0 };
    CHECK (m.add (a));
    m.collecting = false;
    CHECK (! m.add ({ lynx::AutomationTargetKind::mixerPan, 1, 0, 0 }));
    const auto link = m.controller (30, 2);
    CHECK (link && link->target == a && m.complete());

    // FL keeps its MultiLinkBtn off the default toolbar; LYNX's Multilink group is hidden on a new install, after the
    // window shortcuts, and a toolbar saved by an older version gets it hidden in that place
    using namespace lynx::toolbarlayout;
    const auto d = Layout::makeDefault();
    CHECK (! d.isShown (Group::multilink) && d.isShown (Group::windows));
    CHECK (d.orderToString().find ("windows,-multilink,file") != std::string::npos);
    Layout old;
    old.orderFromString ("transport,master,record,snap,windows,file,chord,meters,output");
    CHECK (! old.isShown (Group::multilink) && old.isDefault());
    auto shown = d;
    shown.setShown (Group::multilink, true);
    Layout back;
    back.orderFromString (shown.orderToString());
    CHECK (back.isShown (Group::multilink) && ! back.isDefault());
    CHECK (matchingPreset (d) == (int) Preset::standard);

    // shown on request, the one-button panel outlasts the file buttons and the meters when room is short
    std::array<int, groupCount> widths;
    widths.fill (100);
    int shownCount = 0;
    for (const auto& e : shown.entries)
        shownCount += e.shown ? 1 : 0;
    const auto v = fit (shown, widths, 10, (shownCount - 1) * 110 + 300, 300);
    CHECK (v[(std::size_t) Group::multilink] && ! v[(std::size_t) Group::chord]);
}
