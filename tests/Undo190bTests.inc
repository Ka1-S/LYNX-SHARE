// Round 190b, issue #140: what Ctrl+Z, Ctrl+Alt+Z and the redo keys do to the undo history, in FL's default mode and
// with Settings › General › "Alternate undo mode" (core/UndoKeys.h, used by MainComponent).
namespace u190b
{
// an undo history of `size` steps: `undone` of them are undone (0 = the newest state)
struct History
{
    int size = 0, undone = 0;
    bool canUndo() const { return undone < size; }
    bool canRedo() const { return undone > 0; }
    void apply (lynx::undokeys::Step s)
    {
        if (s == lynx::undokeys::Step::undo && canUndo())
            ++undone;
        else if (s == lynx::undokeys::Step::redo && canRedo())
            --undone;
    }
    void ctrlZ (bool alternate) { apply (lynx::undokeys::undoKey (alternate, canUndo(), canRedo())); }
    void ctrlAltZ (bool alternate) { apply (lynx::undokeys::backwardsKey (alternate, canUndo(), canRedo())); }
    void redo() { apply (canRedo() ? lynx::undokeys::Step::redo : lynx::undokeys::Step::none); }
};
} // namespace u190b

TEST (u190b_default_mode_ctrl_z_toggles_and_ctrl_alt_z_walks_back)
{
    // FL's default: Ctrl+Z undoes the last step and redoes it when pressed again (what issue #140 describes)
    u190b::History h { 5, 0 };
    h.ctrlZ (false);
    CHECK (h.undone == 1);
    h.ctrlZ (false);
    CHECK (h.undone == 0);
    h.ctrlZ (false);
    CHECK (h.undone == 1);
    // Ctrl+Alt+Z ("Undo backwards") walks down the history, to its first step and no further
    for (int i = 2; i <= 5; ++i)
    {
        h.ctrlAltZ (false);
        CHECK (h.undone == i);
    }
    h.ctrlAltZ (false);
    CHECK (h.undone == 5);
    // Ctrl+Z then comes forward one step at a time (FL: history position − 1)
    h.ctrlZ (false);
    CHECK (h.undone == 4);
    h.ctrlZ (false);
    CHECK (h.undone == 3);
    // nothing to undo or redo: nothing happens
    u190b::History empty;
    empty.ctrlZ (false);
    empty.ctrlAltZ (false);
    CHECK (empty.undone == 0);
}

TEST (u190b_alternate_mode_ctrl_z_walks_the_history)
{
    // "Alternate undo mode": Ctrl+Z is the classic undo, every press one more step back
    u190b::History h { 5, 0 };
    for (int i = 1; i <= 5; ++i)
    {
        h.ctrlZ (true);
        CHECK (h.undone == i);
    }
    h.ctrlZ (true);
    CHECK (h.undone == 5); // (the first step: nothing further)
    // the mode swaps the two shortcuts (FL): Ctrl+Alt+Z redoes what was undone...
    h.ctrlAltZ (true);
    CHECK (h.undone == 4);
    h.ctrlAltZ (true);
    CHECK (h.undone == 3);
    // ...and the redo keys redo in both modes
    h.redo();
    h.redo();
    h.redo();
    CHECK (h.undone == 0);
    h.redo();
    CHECK (h.undone == 0);
    // with nothing undone, Ctrl+Alt+Z is FL's undo/redo key: one step back, then forward again
    h.ctrlAltZ (true);
    CHECK (h.undone == 1);
    h.ctrlAltZ (true);
    CHECK (h.undone == 0);
}

TEST (u190b_undo_and_redo_shortcuts)
{
    // Ctrl+Z, Ctrl+Alt+Z (FL), and redo on Ctrl+Y and Ctrl+Shift+Z (asked for in issue #140), with no key clash
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ 'Z', ctrl }, Context::global, false) == "edit.undo");
    CHECK (map.find ({ 'Z', ctrl | alt }, Context::global, false) == "edit.undoBackwards");
    CHECK (map.find ({ 'Z', ctrl | shift }, Context::global, false) == "edit.redo");
    CHECK (map.find ({ 'Y', ctrl }, Context::global, false) == "edit.redo");
    // ...in every window that takes keys (none of them binds these chords to something else)
    for (const auto context : { Context::pianoRoll, Context::playlist, Context::channelRack, Context::mixer })
    {
        CHECK (map.find ({ 'Z', ctrl }, context, false) == "edit.undo");
        CHECK (map.find ({ 'Z', ctrl | alt }, context, false) == "edit.undoBackwards");
        CHECK (map.find ({ 'Z', ctrl | shift }, context, false) == "edit.redo");
    }
}
