// Round 190fz (Discord #general, 2026-11-10: "make it able to pull the velocity window, it too small"): the Piano roll's
// event editor (velocity / event lane) gets FL's divider, a TQuickSplitter between the note grid and the event editor
// (REA of FLEngine_x64.dll; the facts in src/core/EventPaneSplit.h and /mnt/e/lynx-work/evpane190fz/HANDOFF.md).
// The rules are checked here; the app route --evpaneshot drives the real mouse handlers and writes a report.

TEST (evpane190fz_fl_defaults)
{
    using namespace lynx::evpane;
    CHECK (barThickness == 10);  // Splitter.Height
    CHECK (minPane == 46);       // SplitterChanging: round (46 * scale)
    CHECK (snapZone == 23);
    const State s;
    CHECK (s.aspect == 0.8f);    // SplitterAspect default for PRForm (read on this machine's registry: 0.8)
    CHECK (s.spare == 1.0f);     // SplitterSpareAspect default (registry: 1.0)
    CHECK (percentOf (s.aspect) == 80);
}

TEST (evpane190fz_clamp_snaps_and_keeps_46_px)
{
    using namespace lynx::evpane;
    const int top = 70, bottom = 690; // a 700 px Piano roll: the bar runs from under the ruler to the bottom edge
    CHECK (clampBar (top, top, bottom) == top);
    CHECK (clampBar (top + 22, top, bottom) == top);          // inside the snap zone: the grid folds away
    CHECK (clampBar (top + 23, top, bottom) == top + 46);     // outside it: the grid keeps 46 px
    CHECK (clampBar (top + 40, top, bottom) == top + 46);
    CHECK (clampBar (400, top, bottom) == 400);               // anywhere in the middle
    CHECK (clampBar (bottom - 46, top, bottom) == bottom - 46);
    CHECK (clampBar (bottom - 30, top, bottom) == bottom - 46); // the event editor keeps 46 px
    CHECK (clampBar (bottom - 23, top, bottom) == bottom - 46);
    CHECK (clampBar (bottom - 22, top, bottom) == bottom);    // inside the snap zone: the event editor folds away
    CHECK (clampBar (bottom + 50, top, bottom) == bottom);
    CHECK (clampBar (-500, top, bottom) == top);
    CHECK (clampBar (300, 100, 100) == 100);                  // no travel (a tiny window)
    CHECK (clampBar (300, 100, 50) == 100);
}

TEST (evpane190fz_place_is_a_fraction_of_the_travel)
{
    using namespace lynx::evpane;
    const int top = 70, bottom = 690;
    CHECK (aspectOf (top, top, bottom) == 0.0f);
    CHECK (aspectOf (bottom, top, bottom) == 1.0f);
    CHECK (aspectOf (bottom + 100, top, bottom) == 1.0f);
    CHECK (aspectOf (10, 10, 10) == 0.5f);                    // (FL: 0.5 when Min == Max)
    CHECK (barTopOf (0.8f, top, bottom) == top + 496);        // FL's default: the event editor gets the bottom fifth
    CHECK (barTopOf (1.0f, top, bottom) == bottom);
    CHECK (barTopOf (0.0f, top, bottom) == top);
    CHECK (barTopOf (0.02f, top, bottom) == top);             // the snap rule applies to a remembered place too
    CHECK (barTopOf (2.0f, top, bottom) == bottom);           // (clamped to 0..1)
    for (int y = top + 46; y <= bottom - 46; ++y)             // every place a drag can leave round-trips
        CHECK (barTopOf (aspectOf (y, top, bottom), top, bottom) == y);
    // a taller window keeps the proportion
    const auto a = aspectOf (400, top, bottom);
    const auto taller = barTopOf (a, top, bottom + 300);
    CHECK (std::abs ((float) (taller - top) / (float) (bottom + 300 - top) - a) < 0.002f);
}

TEST (evpane190fz_alternate_size_and_default)
{
    using namespace lynx::evpane;
    State s;
    toggleAlternate (s, 0.8f);  // the first right-click: the spare place (1.0) = the event editor folded away
    CHECK (s.aspect == 1.0f && s.spare == 0.8f);
    toggleAlternate (s, 1.0f);  // and back
    CHECK (s.aspect == 0.8f && s.spare == 1.0f);
    s.aspect = 0.45f;           // after a drag the swap remembers the dragged place
    toggleAlternate (s, 0.45f);
    CHECK (s.aspect == 1.0f && s.spare == 0.45f);
    toggleAlternate (s, 1.0f);
    CHECK (s.aspect == 0.45f && s.spare == 1.0f);
    resetToDefault (s);         // middle-click: the default place, the spare one stays
    CHECK (s.aspect == 0.8f && s.spare == 1.0f);
    State none { 0.3f, -1.0f }; // no spare place (FL: -1): the default place, nothing remembered
    toggleAlternate (none, 0.3f);
    CHECK (none.aspect == 0.8f && none.spare == -1.0f);
}

TEST (evpane190fz_settings_text_round_trip)
{
    using namespace lynx::evpane;
    const State s { 0.4375f, 1.0f };
    CHECK (fromText (toText (s)) == s);
    CHECK (toText (State {}) == "aspect=0.8\nspare=1\n");
    CHECK (fromText ("") == State {});                              // no file: FL's defaults
    CHECK (fromText ("aspect=abc\nspare=\n") == State {});           // unreadable values keep the defaults
    CHECK (fromText ("aspect=0,5\n").aspect == 0.0f || fromText ("aspect=0,5\n").aspect == 0.8f); // never 0.5 from a comma
    CHECK (fromText ("aspect=7\nspare=-3\n") == (State { 1.0f, -1.0f }));
    CHECK (fromText ("junk\nspare=0.25\r\naspect=0.6\n") == (State { 0.6f, 0.25f }));
    CHECK (fromText ("aspect=nan\n").aspect == 0.8f);
}

TEST (evpane190fz_piano_roll_wiring)
{
    using namespace i18ntest;
    const auto source = readFile ("src/ui/PianoRollComponent.cpp");
    const auto header = readFile ("src/ui/PianoRollComponent.h");
    // remembered for the Piano roll in the settings folder (FL: per window kind, not in the project)
    CHECK (source.find ("platform::settingsFile (\"piano-roll-event-pane.txt\")") != std::string::npos);
    // the grid and the lane leave room for the bar; the lane's height comes from the divider in resized()
    CHECK (source.find (".withTrimmedBottom (velocityHeight + eventPaneBarArea().getHeight())") != std::string::npos);
    CHECK (source.find ("layoutEventPane(); // (round 190fz") != std::string::npos);
    // the gestures: drag, right-click on release, double-click, middle-click, the target panel's double-click
    CHECK (source.find ("drag = Drag::eventPaneAlternate;") != std::string::npos);
    CHECK (source.find ("moveEventPaneBar (e.y - eventPaneGrab);") != std::string::npos);
    CHECK (source.find ("resetEventPane();") != std::string::npos);
    CHECK (source.find ("is (\"pianoRoll.eventPaneAlternate\")) toggleEventPaneAlternate();") != std::string::npos);
    CHECK (source.find ("setMouseCursor (juce::MouseCursor::UpDownResizeCursor);") != std::string::npos);
    // the Clip-launch dock keeps its short fixed lane and has no divider
    const auto bar = source.find ("juce::Rectangle<int> PianoRollComponent::eventPaneBarArea() const");
    CHECK (bar != std::string::npos && source.find ("if (fitKeysToNotes)", bar) < source.find ("return { 0, eventPaneBarTop", bar));
    CHECK (header.find ("velocityHeight = docked ? compactVelocityHeight : defaultVelocityHeight;") != std::string::npos);
}

TEST (evpane190fz_shift_enter_in_the_piano_roll)
{
    using namespace lynx::keys;
    const auto* action = findAction ("pianoRoll.eventPaneAlternate");
    CHECK (action != nullptr);
    if (action == nullptr)
        return;
    const Chord shiftEnter { enter, shift };
    CHECK (action->context == Context::pianoRoll);
    CHECK (action->defaults.size() == 1 && action->defaults.front() == shiftEnter);
    int sameChord = 0; // no other Piano roll or global action has Shift+Enter
    for (const auto& a : actions())
        if ((a.context == Context::pianoRoll || a.context == Context::global) && std::string (a.id) != action->id)
            for (const auto& c : a.defaults)
                sameChord += c == shiftEnter ? 1 : 0;
    CHECK (sameChord == 0);
}
