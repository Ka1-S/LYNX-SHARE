// Issue #64 (round 170b follow-up): FL's letter keys in an open plugin menu move the highlight, they don't pick.

#include "core/Preferences.h"

TEST (typeahead170b_letters_step_through_the_items_starting_with_them)
{
    using lynx::prefs::nextTypeAhead;
    // a categorized menu: headings and separators can't be highlighted
    const std::vector<std::string> labels { "LYNX SHARE", "Echoform", "EQ", "Halo", "", "Misc", "Effector", "exciter", "Pitch Shifter" };
    const std::vector<bool> selectable { false, true, true, true, false, false, true, true, true };
    // "e" from nothing highlighted: the first E, then the next ones (across the headings, as one menu window), wrapping
    CHECK (nextTypeAhead (labels, selectable, -1, 'e') == 1);
    CHECK (nextTypeAhead (labels, selectable, 1, 'e') == 2);
    CHECK (nextTypeAhead (labels, selectable, 2, 'E') == 6);
    CHECK (nextTypeAhead (labels, selectable, 6, 'e') == 7); // (any case)
    CHECK (nextTypeAhead (labels, selectable, 7, 'e') == 1);
    // from an item the mouse highlighted: the next match after it
    CHECK (nextTypeAhead (labels, selectable, 3, 'e') == 6);
    // a heading never takes the highlight ("LYNX SHARE", "Misc")
    CHECK (nextTypeAhead (labels, selectable, -1, 'l') == -1);
    CHECK (nextTypeAhead (labels, selectable, -1, 'm') == -1);
    // the only match stays highlighted, no match does nothing
    CHECK (nextTypeAhead (labels, selectable, 8, 'p') == 8);
    CHECK (nextTypeAhead (labels, selectable, 2, 'z') == -1);
    CHECK (nextTypeAhead (labels, selectable, 2, ' ') == -1);
    // leading spaces are skipped, a digit works like a letter
    CHECK (nextTypeAhead ({ "  3x Osc", "BooBass" }, { true, true }, -1, '3') == 0);
    CHECK (nextTypeAhead ({}, {}, -1, 'a') == -1);
}
