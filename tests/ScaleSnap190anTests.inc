// Round 190an (research190ak TOP-20 #3, CROSS-DAW F38): Snap to scale, one meaning for every note editor
// (core/ScaleSnap.h). Off by default; drawn notes land on the scale, moved notes go by scale steps, off-scale notes
// are left alone until they are moved in pitch.

namespace scalesnap190an
{
inline int majorScale()
{
    for (int i = 0; i < (int) lynx::notetools::scales().size(); ++i)
        if (std::string (lynx::notetools::scales()[(std::size_t) i].name) == "Major")
            return i;
    return 0;
}

inline lynx::Pattern patternOf (std::initializer_list<int> keys)
{
    lynx::Pattern p;
    for (const auto key : keys)
    {
        lynx::Note n;
        n.key = key;
        p.notes.push_back (n);
    }
    return p;
}
} // namespace scalesnap190an

TEST (scalesnap190an_off_by_default_and_without_a_scale_nothing_snaps)
{
    using namespace lynx;
    const scalesnap::Scale none;
    CHECK (! none.active());
    CHECK (! (scalesnap::Scale { 0, scalesnap190an::majorScale(), false }).active()); // a scale highlighted, the option off
    CHECK (! (scalesnap::Scale { 0, 0, true }).active());                             // the option on, no scale
    CHECK (! (scalesnap::Scale { 0, 100000, true }).active());
    // FL's free drawing and semitone moves
    CHECK (scalesnap::drawKey (none, 61) == 61);
    CHECK (scalesnap::dragSteps (none, 60, 61) == 1 && scalesnap::dragSteps (none, 60, 49) == -11);
    auto p = scalesnap190an::patternOf ({ 60, 61, 64 });
    CHECK (scalesnap::moveBySteps (p, { 0, 1, 2 }, 1, none) == 1);
    CHECK (p.notes[0].key == 61 && p.notes[1].key == 62 && p.notes[2].key == 65);
}

TEST (scalesnap190an_drawn_notes_land_on_the_scale)
{
    using namespace lynx;
    const scalesnap::Scale cMajor { 0, scalesnap190an::majorScale(), true };
    CHECK (cMajor.active());
    for (int key = 0; key <= 127; ++key)
    {
        const auto drawn = scalesnap::drawKey (cMajor, key);
        CHECK (notetools::inScale (drawn, 0, cMajor.scale));
        CHECK (std::abs (drawn - key) <= 1);
        if (notetools::inScale (key, 0, cMajor.scale))
            CHECK (drawn == key);
    }
    CHECK (scalesnap::drawKey (cMajor, 61) == 60); // a tie goes down
    const scalesnap::Scale dMajor { 2, cMajor.scale, true };
    CHECK (scalesnap::drawKey (dMajor, 65) == 64 && scalesnap::drawKey (dMajor, 66) == 66); // F → E, F# stays
}

TEST (scalesnap190an_scale_steps_between_keys)
{
    using namespace lynx;
    const scalesnap::Scale cMajor { 0, scalesnap190an::majorScale(), true };
    CHECK (scalesnap::stepsBetween (cMajor, 60, 60) == 0);
    CHECK (scalesnap::stepsBetween (cMajor, 60, 62) == 1 && scalesnap::stepsBetween (cMajor, 62, 60) == -1);
    CHECK (scalesnap::stepsBetween (cMajor, 60, 72) == 7 && scalesnap::stepsBetween (cMajor, 72, 60) == -7);
    CHECK (scalesnap::stepsBetween (cMajor, 64, 65) == 1); // E → F: a semitone, one step
    // an off-scale key is one step from each in-scale neighbour
    CHECK (scalesnap::stepsBetween (cMajor, 61, 62) == 1 && scalesnap::stepsBetween (cMajor, 61, 60) == -1);
    CHECK (scalesnap::stepsBetween (cMajor, 61, 64) == 2 && scalesnap::stepsBetween (cMajor, 61, 59) == -2);
    // stepKey is its inverse on the scale
    for (int steps = -9; steps <= 9; ++steps)
        CHECK (scalesnap::stepsBetween (cMajor, 60, scalesnap::stepKey (cMajor, 60, steps)) == steps);
    int done = 0;
    CHECK (scalesnap::stepKey (cMajor, 125, 3, &done) == 127 && done == 1); // 125 (F) → 127 (G), then the keyboard ends
    CHECK (scalesnap::stepKey (cMajor, 0, -2, &done) == 0 && done == 0);
}

TEST (scalesnap190an_a_drag_moves_the_group_by_scale_steps)
{
    using namespace lynx;
    const scalesnap::Scale cMajor { 0, scalesnap190an::majorScale(), true };
    // the grabbed C under a mouse one row up (C#): the nearest scale key is still C, nothing moves (the old
    // per-note snap turned C E G into C F G here)
    CHECK (scalesnap::dragSteps (cMajor, 60, 61) == 0);
    CHECK (scalesnap::dragSteps (cMajor, 60, 62) == 1 && scalesnap::dragSteps (cMajor, 60, 63) == 1 && scalesnap::dragSteps (cMajor, 60, 64) == 2);
    CHECK (scalesnap::dragSteps (cMajor, 60, 59) == -1 && scalesnap::dragSteps (cMajor, 60, 48) == -7);

    auto p = scalesnap190an::patternOf ({ 60, 64, 67 }); // C E G
    CHECK (scalesnap::moveBySteps (p, { 0, 1, 2 }, scalesnap::dragSteps (cMajor, 60, 62), cMajor) == 1);
    CHECK (p.notes[0].key == 62 && p.notes[1].key == 65 && p.notes[2].key == 69); // D F A: the triad one step up
    CHECK (scalesnap::moveBySteps (p, { 0, 1, 2 }, 3, cMajor) == 3);
    CHECK (p.notes[0].key == 67 && p.notes[1].key == 71 && p.notes[2].key == 74); // G B D
    CHECK (scalesnap::moveBySteps (p, { 0, 1, 2 }, -4, cMajor) == -4);
    CHECK (p.notes[0].key == 60 && p.notes[1].key == 64 && p.notes[2].key == 67);
    // only the listed notes move; a bad index is skipped
    CHECK (scalesnap::moveBySteps (p, { 1, 99, -1 }, 1, cMajor) == 1);
    CHECK (p.notes[0].key == 60 && p.notes[1].key == 65 && p.notes[2].key == 67);
}

TEST (scalesnap190an_off_scale_notes_are_left_alone_until_moved_in_pitch)
{
    using namespace lynx;
    const scalesnap::Scale cMajor { 0, scalesnap190an::majorScale(), true };
    // an off-scale note (C#) dragged along the time axis: the mouse stays on its row, no pitch change
    CHECK (scalesnap::dragSteps (cMajor, 61, 61) == 0);
    auto p = scalesnap190an::patternOf ({ 61, 66, 64 }); // C# F# off the scale, E on it
    CHECK (scalesnap::moveBySteps (p, { 0, 1, 2 }, 0, cMajor) == 0);
    CHECK (p.notes[0].key == 61 && p.notes[1].key == 66 && p.notes[2].key == 64);
    // dragged one row up or down it reaches the scale with that step
    CHECK (scalesnap::dragSteps (cMajor, 61, 62) == 1 && scalesnap::dragSteps (cMajor, 61, 60) == -1);
    CHECK (scalesnap::moveBySteps (p, { 0, 1, 2 }, 1, cMajor) == 1);
    CHECK (p.notes[0].key == 62 && p.notes[1].key == 67 && p.notes[2].key == 65); // D G F
    for (const auto& n : p.notes)
        CHECK (notetools::inScale (n.key, 0, cMajor.scale));
    // an octave (FL's Ctrl+Up/Down, 12 semitones through noteedit::move) keeps every note where it is on the scale
    auto q = scalesnap190an::patternOf ({ 61, 64 });
    auto ticks = std::int64_t (0);
    auto keys = 12;
    noteedit::move (q, { 0, 1 }, ticks, keys);
    CHECK (q.notes[0].key == 73 && q.notes[1].key == 76);
    CHECK (! notetools::inScale (q.notes[0].key, 0, cMajor.scale) && notetools::inScale (q.notes[1].key, 0, cMajor.scale));
}

TEST (scalesnap190an_the_group_keeps_its_shape_at_the_ends_of_the_keyboard)
{
    using namespace lynx;
    const scalesnap::Scale cMajor { 0, scalesnap190an::majorScale(), true };
    auto p = scalesnap190an::patternOf ({ 120, 124, 127 }); // C E G at the top
    CHECK (scalesnap::moveBySteps (p, { 0, 1, 2 }, 2, cMajor) == 0); // G has no step left: nobody moves
    CHECK (p.notes[0].key == 120 && p.notes[1].key == 124 && p.notes[2].key == 127);
    auto low = scalesnap190an::patternOf ({ 2, 5, 9 }); // D F A
    CHECK (scalesnap::moveBySteps (low, { 0, 1, 2 }, -5, cMajor) == -1); // D reaches C (0), then stops
    CHECK (low.notes[0].key == 0 && low.notes[1].key == 4 && low.notes[2].key == 7);
}

TEST (scalesnap190an_arrow_keys_one_scale_step_and_the_toggle_has_no_default_key)
{
    using namespace lynx;
    // Up / Down and FL's Shift+Up / Down are "one up / one down" (noteedit::arrowMove keys ±1): with the option on
    // that is one scale step; FL's Ctrl+Up / Down stays an octave
    CHECK (noteedit::arrowMove (noteedit::Arrow::up, false, false, false).keys == 1);
    CHECK (noteedit::arrowMove (noteedit::Arrow::down, false, true, false).keys == -1);
    CHECK (noteedit::arrowMove (noteedit::Arrow::up, true, false, false).keys == 12);
    const scalesnap::Scale aMinorPenta = [] {
        scalesnap::Scale s { 9, 0, true };
        for (int i = 0; i < (int) notetools::scales().size(); ++i)
            if (notetools::scales()[(std::size_t) i].intervals == std::vector<int> { 0, 3, 5, 7, 10 })
                s.scale = i;
        return s;
    }();
    if (aMinorPenta.active())
    {
        auto p = scalesnap190an::patternOf ({ 57, 60 }); // A C
        scalesnap::moveBySteps (p, { 0, 1 }, 1, aMinorPenta);
        CHECK (p.notes[0].key == 60 && p.notes[1].key == 62); // C D
    }
    const auto& actions = keys::actions();
    const auto it = std::find_if (actions.begin(), actions.end(), [] (const auto& a) { return std::string (a.id) == "pianoRoll.scaleSnap"; });
    CHECK (it != actions.end());
    if (it != actions.end())
        CHECK (it->defaults.empty() && it->context == keys::Context::pianoRoll);
}

// Round 190an (TOP-20 #4, CROSS-DAW F13): "Create automation clip for the last tweaked control" as a key action. FL has
// the menu item (Add › Automation for last tweaked parameter) and no key for it, so LYNX binds none by default.
TEST (autokey190an_automate_last_tweaked_is_a_global_action_without_a_default_key)
{
    const auto* action = lynx::keys::findAction ("automation.lastTweaked");
    CHECK (action != nullptr);
    if (action != nullptr)
    {
        CHECK (action->context == lynx::keys::Context::global); // from any window, also over a hosted plugin's editor
        CHECK (action->defaults.empty());
        CHECK (std::string (action->description) == "Create automation clip for the last tweaked control");
    }
    // one action per id (a second entry would shadow the first in the shortcut editor)
    int count = 0;
    for (const auto& a : lynx::keys::actions())
        count += std::string (a.id) == "automation.lastTweaked" || std::string (a.id) == "pianoRoll.scaleSnap" ? 1 : 0;
    CHECK (count == 2);
}
