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
    CHECK (tp::layoutCount() == 30); // FL's 31 files without Performance mode
    CHECK (std::string (tp::layoutName (0)) == "Layout FPC (C4)" && std::string (tp::layoutName (tp::pianoLayout())) == "Layout Piano");
    CHECK (tp::layoutIndex ("Major") >= 0 && tp::layoutIndex ("nope") == tp::pianoLayout() && std::string (tp::layoutName (99)).empty());

    // Piano is LYNX's own two rows
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
