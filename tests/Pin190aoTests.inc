// Round 190ao (pain point 11, LYNX's own): "Pin on top" of a plugin window. core/PinOnTop.h decides which windows to
// bring forward; PluginRef::pinned is saved with the plugin (off by default).

TEST (pin190ao_nothing_moves_without_a_pinned_window_or_when_it_is_in_front)
{
    using lynx::pinontop::toRaise;
    CHECK (toRaise ({}).empty());
    CHECK (toRaise ({ false, false, false }).empty());
    CHECK (toRaise ({ true }).empty());
    CHECK (toRaise ({ false, false, true }).empty());       // the pinned window is already in front
    CHECK (toRaise ({ false, true, true }).empty());        // two pinned ones in front, in their own order
    CHECK (toRaise ({ true, true }).empty());
}

TEST (pin190ao_a_covered_pinned_window_comes_back_in_front)
{
    using lynx::pinontop::toRaise;
    // back to front: the pinned plugin window, then a window that was just clicked
    CHECK (toRaise ({ true, false }) == std::vector<int> ({ 0 }));
    CHECK (toRaise ({ false, true, false }) == std::vector<int> ({ 1 }));
    // two pinned windows keep their own order: raised back to front
    CHECK (toRaise ({ true, false, true, false }) == std::vector<int> ({ 0, 2 }));
    // a pinned window already above every plain one stays where it is
    CHECK (toRaise ({ true, false, true }) == std::vector<int> ({ 0 }));

    // after those raises (each one goes to the front, in that order) nothing is left to do: no raise loop
    std::vector<bool> after { false, false, false, true, true };
    CHECK (toRaise (after).empty());
}

TEST (pin190ao_the_pin_is_off_by_default_and_part_of_the_plugin)
{
    lynx::PluginRef a, b;
    CHECK (! a.pinned);
    CHECK (a == b);
    b.pinned = true;
    CHECK (! (a == b));
}
