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
