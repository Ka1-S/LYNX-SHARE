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
