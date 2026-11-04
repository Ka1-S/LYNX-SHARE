// Round 168t (issue #34): the typing keyboard to piano uses FL's name and shortcut (TShortcutsModule DFM:
// OptionsTypingKbToPianoAction, Caption "Typing keyboard to piano", ShortCut 16468 = Ctrl+T). Included from tests.cpp.

TEST (typing168t_action_has_fl_name_and_ctrl_t)
{
    using namespace lynx::keys;
    const auto* a = findAction ("view.typingPiano");
    CHECK (a != nullptr);
    if (a == nullptr)
        return;
    CHECK (std::string (a->description).rfind ("Typing keyboard to piano", 0) == 0);
    KeyMap map;
    const auto chords = map.chordsFor ("view.typingPiano");
    CHECK (! chords.empty() && toString (chords.front()) == "Ctrl+T");
    // Ctrl+T itself never plays a note, so it can always switch the piano off again
    CHECK (! KeyMap::playsNote (chords.front(), true));
}

// Round 168t2: measured in FL 2026 (one session). The typing keyboard is on by default (DFM Checked = True); its
// notes start at FL's octave 4 (Z = MIDI 48; R played F5, P played E6 in FL's names, C5 = 60); the button's
// right-click menu moves them by Root note (0..11) and Octave (2..6); with the Playlist focused Ctrl+T adds an
// "Auto" time marker and the piano stays on; while it is on, plain letters are the piano's (K did nothing).
TEST (typing168t2_fl_octave_root_and_ctrl_t_in_playlist)
{
    using namespace lynx::keys;
    CHECK (typingPianoNote ('Z') == 48 && typingPianoNote ('M') == 59);
    CHECK (typingPianoNote ('Q') == 60 && typingPianoNote ('E') == 64 && typingPianoNote ('R') == 65 && typingPianoNote ('P') == 76);
    CHECK (typingPianoNote ('Z', 5) == 60 && typingPianoNote ('Z', 2) == 24 && typingPianoNote ('Z', 6) == 72);
    CHECK (typingPianoNote ('Z', 4, 2) == 50 && typingPianoNote ('Q', 4, 11) == 71);
    CHECK (typingPianoNote ('Z', 9) == typingPianoNote ('Z', 6) && typingPianoNote ('Z', 0) == typingPianoNote ('Z', 2)); // clamped as FL's menu
    CHECK (typingPianoNote ('P', 6, 11) == 72 + 11 + 12 + 16);
    CHECK (typingPianoNote ('K') == -1 && typingPianoNote ('A') == -1);

    KeyMap map;
    // Ctrl+T: the editors' "Add auto" marker wins while they have the focus (Playlist measured in FL; the Piano roll's
    // own Ctrl+T marker is round 168g's), piano on or off; elsewhere the toggle
    CHECK (map.find ({ 'T', ctrl }, Context::playlist, true) == "playlist.markerAuto");
    CHECK (map.find ({ 'T', ctrl }, Context::pianoRoll, true) == "pianoRoll.markerAuto");
    CHECK (map.find ({ 'T', ctrl }, Context::channelRack, true) == "view.typingPiano");
    CHECK (map.find ({ 'T', ctrl }, Context::mixer, true) == "view.typingPiano");
    // piano on: a non-note letter is swallowed too (FL: K, next channel, did nothing in the Piano roll)
    CHECK (map.find ({ 'K', none }, Context::pianoRoll, false) == "pianoRoll.nextChannel");
    CHECK (map.find ({ 'K', none }, Context::pianoRoll, true).empty());
    CHECK (KeyMap::playsNote ({ 'P', none }, true) && KeyMap::playsNote ({ 'R', none }, true));
}

// FL's typing keyboard layouts (discussion #43): the Layout list of the button's right-click menu, from FL 2026's
// System/Config/Typing to piano/*.scr. Note names as FL's (C5 = 60).
TEST (typing_fl_layouts_from_scr_files)
{
    using namespace lynx::keys;
    namespace tp = typingpiano;
    CHECK (tp::layoutCount() == 31); // FL's 31 files (round 169f: Performance mode too)
    CHECK (std::string (tp::layoutName (0)) == "Layout FPC (C4)" && std::string (tp::layoutName (tp::pianoLayout())) == "Layout Piano");
    CHECK (tp::layoutIndex ("Major") >= 0 && tp::layoutIndex ("nope") == tp::pianoLayout() && std::string (tp::layoutName (99)).empty());

    // Piano: FL's Layout Piano.scr (round 169f; K is LOCK there)
    const auto piano = tp::pianoLayout();
    CHECK (typingPianoNotes ('Z', 4, 0, piano) == std::vector<int> { 48 } && typingPianoNotes ('K', 4, 0, piano).empty());

    // Major.scr: Z row C4.., A row C5.., Q row C6.., number row C7..
    const auto major = tp::layoutIndex ("Major");
    CHECK (typingPianoNotes ('Z', 4, 0, major) == std::vector<int> { 48 } && typingPianoNotes ('/', 4, 0, major) == std::vector<int> { 64 });
    CHECK (typingPianoNotes ('a', 4, 0, major) == std::vector<int> { 60 } && typingPianoNotes ('\'', 4, 0, major) == std::vector<int> { 77 });
    CHECK (typingPianoNotes ('Q', 4, 0, major) == std::vector<int> { 72 } && typingPianoNotes ('1', 4, 0, major) == std::vector<int> { 84 });
    CHECK (typingPianoNotes ('=', 4, 0, major) == std::vector<int> { 103 } && typingPianoNotes (' ', 4, 0, major).empty());
    // moved by Octave and Root note like the Piano; notes past 127 are dropped
    CHECK (typingPianoNotes ('Z', 5, 2, major) == std::vector<int> { 62 } && typingPianoNotes ('=', 6, 11, major).empty());

    // a chord layout plays all the notes of the line; FREE / LOCK play none
    CHECK (typingPianoNotes ('1', 4, 0, tp::layoutIndex ("Major + Chords")) == (std::vector<int> { 60, 64, 67 }));
    CHECK (typingPianoNotes ('P', 4, 0, tp::layoutIndex ("Layout Slicex (C4)")).empty());
    CHECK (typingPianoNotes ('1', 4, 0, tp::layoutIndex ("Layout Janko")) == std::vector<int> { 57 });

    // every layout has a token for each of the 45 keys (the last one, '/', parses)
    for (int l = 0; l < tp::layoutCount(); ++l)
        if (l != piano)
            CHECK (! typingPianoNotes ('/', 4, 0, l).empty() || ! typingPianoNotes ('.', 4, 0, l).empty() || std::string (tp::layoutName (l)).find ("Layout") == 0);
}

// Round 169f (PR #57 review): FL's own rules for the layouts, from FLEngine (FUN_00661f70 parses a .scr line: FREE = -2,
// LOCK = -1; FUN_011828c0 plays table + root + 12 x octave offset for every layout and takes every key but FREE).
TEST (typing169f_fl_layout_piano_free_lock_and_performance)
{
    using namespace lynx::keys;
    namespace tp = typingpiano;
    // FL's menu order: by name, so "Major" before "Major + Chords"; Performance mode between Janko and Piano
    CHECK (std::string (tp::layoutName (2)) == "Layout Performance mode" && tp::performanceLayout() == 2 && tp::pianoLayout() == 3);
    CHECK (tp::layoutIndex ("Major") + 1 == tp::layoutIndex ("Major + Chords"));
    for (int l = 1; l < tp::layoutCount(); ++l)
        CHECK (std::string (tp::layoutName (l - 1)) < tp::layoutName (l));

    // FL's Layout Piano: LYNX's two rows plus L ; , . / [ ] = ; 1 4 8 - F K ' are LOCK; A ` \ are FREE
    const auto piano = tp::pianoLayout();
    const std::pair<int, int> extra[] = { { 'L', 61 }, { ';', 63 }, { ',', 60 }, { '.', 62 }, { '/', 64 }, { '[', 77 }, { ']', 79 }, { '=', 78 } };
    for (auto [k, n] : extra)
        CHECK (typingPianoNotes (k, 4, 0, piano) == std::vector<int> { n });
    for (int k : { 'Z', 'S', 'M', 'Q', '2', 'P', '9', '0' }) // the two rows: as typingPianoNote, Octave and Root note included
        for (int o : { 3, 4, 6 })
            CHECK (typingPianoNotes (k, o, 5, piano) == std::vector<int> { typingPianoNote (k, o, 5) });
    for (int k : { '1', '4', '8', '-', 'F', 'K', '\'' })
        CHECK (typingPianoKey (k, 4, 0, piano).taken && typingPianoKey (k, 4, 0, piano).notes.empty());
    for (int k : std::initializer_list<int> { 'A', '`', '\\', ' ', 0x10070 /* F1 */ })
        CHECK (! typingPianoKey (k, 4, 0, piano).taken);
    CHECK (! typingPianoKey ('a', 4, 0, piano).taken && typingPianoKey ('l', 4, 0, piano).taken);

    // a note pushed past 127 plays nothing, but FL still takes the key (not FREE)
    const auto major = tp::layoutIndex ("Major");
    CHECK (typingPianoKey ('=', 6, 11, major).taken && typingPianoKey ('=', 6, 11, major).notes.empty());

    // Root note and Octave move FPC and Slicex too (FL adds them for every layout)
    CHECK (typingPianoNotes ('1', 5, 2, tp::layoutIndex ("Layout FPC (C4)")) == std::vector<int> { 49 + 12 + 2 });
    CHECK (typingPianoNotes ('Z', 3, 0, tp::layoutIndex ("Layout Slicex (C4)")) == std::vector<int> { 60 - 12 });

    // Performance mode: the notes of FL's playlist live mapping (track = note / 12, column = note % 12)
    const auto perfLayout = tp::performanceLayout();
    CHECK (typingPianoNotes ('1', 4, 0, perfLayout) == std::vector<int> { 0 } && typingPianoNotes ('6', 4, 0, perfLayout) == std::vector<int> { 48 });
    CHECK (typingPianoNotes ('Q', 4, 0, perfLayout) == std::vector<int> { 12 } && typingPianoNotes ('/', 4, 0, perfLayout) == std::vector<int> { 88 });
    CHECK (typingPianoKey ('-', 4, 0, perfLayout).taken && typingPianoKey ('-', 4, 0, perfLayout).notes.empty());
    lynx::perf::MidiMapping mapping;
    lynx::perf::Command c;
    CHECK (mapping.note (typingPianoNotes ('Y', 4, 0, perfLayout)[0], true, c) && c.track == 5 && c.column == 0
           && c.type == lynx::perf::Command::Type::press);

    // every layout has one token per key (the last key parses, a key past the end of a short string never reads past it)
    for (int l = 0; l < tp::layoutCount(); ++l)
        CHECK (typingPianoKey ('/', 4, 0, l).taken);
}
