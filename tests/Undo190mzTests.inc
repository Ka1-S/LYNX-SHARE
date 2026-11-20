// Round 190mz, issue #214 ("Ctrl+Z seems to go only one step back, and pressed twice it redoes"): FL 2026 ships with
// "Alternate undo mode" on, so Ctrl+Z steps back through the history by default; FL's undo/redo toggle stays one switch
// away. Also the saved setting's migration and the Edit menu's FL / Live items (core/UndoKeys.h).
namespace u190mz
{
// u190b's history, plus a new edit (which drops what was undone)
struct History : u190b::History
{
    void edit()
    {
        size = size - undone + 1;
        undone = 0;
    }
};
} // namespace u190mz

TEST (u190mz_alternate_mode_is_on_by_default_and_old_files_migrate)
{
    using lynx::undokeys::SettingsReader;
    CHECK (lynx::undokeys::alternateModeDefault); // FL 2026's default (FLEngine .data 0x15452d0 = 1)
    {
        SettingsReader r; // no settings file, or a file without either key
        CHECK (r.alternate);
    }
    {
        SettingsReader r; // an older LYNX wrote "alternateUndo=0" (its default, never chosen): FL 2026's default now
        r.legacyKey (false);
        CHECK (r.alternate);
    }
    {
        SettingsReader r; // "alternateUndo=1" was the user's choice: kept
        r.legacyKey (true);
        CHECK (r.alternate);
    }
    {
        SettingsReader r; // round 190mz's key wins, before or after the old one
        r.currentKey (false);
        r.legacyKey (true);
        CHECK (! r.alternate);
        SettingsReader s;
        s.legacyKey (true);
        s.currentKey (false);
        CHECK (! s.alternate);
        SettingsReader t;
        t.legacyKey (false);
        t.currentKey (true);
        CHECK (t.alternate);
    }
}

TEST (u190mz_issue214_ctrl_z_keeps_going_back_by_default)
{
    const auto mode = lynx::undokeys::alternateModeDefault;
    u190mz::History h;
    h.size = 4;
    // the reporter's case: Ctrl+Z twice is two steps back, not undo + redo
    h.ctrlZ (mode);
    h.ctrlZ (mode);
    CHECK (h.undone == 2);
    h.ctrlZ (mode);
    h.ctrlZ (mode);
    h.ctrlZ (mode); // (at the first state: nothing more, and no redo either)
    CHECK (h.undone == 4);
    // Ctrl+Y / Ctrl+Shift+Z step forward one at a time
    h.redo();
    CHECK (h.undone == 3);
    h.redo();
    h.redo();
    h.redo();
    h.redo();
    CHECK (h.undone == 0);
    // a new edit after some undos drops the undone steps: Ctrl+Z then undoes the new edit
    h.ctrlZ (mode);
    h.ctrlZ (mode);
    h.edit();
    CHECK (h.size == 3 && h.undone == 0);
    h.ctrlZ (mode);
    CHECK (h.undone == 1);
    h.ctrlZ (mode);
    CHECK (h.undone == 2);
}

TEST (u190mz_toggle_mode_edge_cases_from_the_report)
{
    // FL's other mode (Alternate undo mode off): what the reporter saw, kept exact for those who want it
    u190mz::History h;
    h.size = 4;
    h.ctrlZ (false);
    CHECK (h.undone == 1);
    h.ctrlZ (false); // "sometimes when I press Ctrl+Z twice, it seems like it redoes the step": it does, by design
    CHECK (h.undone == 0);
    // Ctrl+Y right after the first Ctrl+Z redoes too; then Ctrl+Z undoes again
    h.ctrlZ (false);
    h.redo();
    CHECK (h.undone == 0);
    h.ctrlZ (false);
    CHECK (h.undone == 1);
    // after walking back with Ctrl+Alt+Z, Ctrl+Z comes forward one step per press (FL: position - 1), then undoes again
    h.ctrlAltZ (false);
    h.ctrlAltZ (false);
    CHECK (h.undone == 3);
    h.ctrlZ (false);
    h.ctrlZ (false);
    h.ctrlZ (false);
    CHECK (h.undone == 0);
    h.ctrlZ (false);
    CHECK (h.undone == 1);
    // a new edit after an undo: the undone step is gone, so Ctrl+Z undoes the new edit (it can't redo the old one)
    h.edit();
    h.ctrlZ (false);
    CHECK (h.undone == 1);
    h.ctrlZ (false);
    CHECK (h.undone == 0);
    // switching the mode in the middle: the next key follows the new mode
    h.ctrlZ (false);
    h.ctrlZ (true);
    CHECK (h.undone == 2);
    h.ctrlZ (false);
    CHECK (h.undone == 1);
}

TEST (u190mz_edit_menu_names_the_step_with_the_key_that_does_it)
{
    using namespace lynx::undokeys;
    const auto same = [] (const char* a, const char* b) { return a != nullptr && b != nullptr && std::string (a) == b; };
    // history of 4 states (position 3 = the newest), nothing undone; FL alternate mode (shots: fl-edit-menu-1/-2)
    auto u = undoMenuItem (true, 3, true, false);
    auto r = redoMenuItem (true, false, 3, false);
    CHECK (u.shown && u.enabled && u.step == 3 && same (u.action, "edit.undo"));
    CHECK (! r.shown);
    // one undone: Undo names step 2 with Ctrl+Z, Redo names step 3 with Ctrl+Alt+Z
    u = undoMenuItem (true, 2, true, true);
    r = redoMenuItem (true, false, 2, true);
    CHECK (u.step == 2 && same (u.action, "edit.undo"));
    CHECK (r.shown && r.enabled && r.step == 3 && same (r.action, "edit.undoBackwards"));
    // FL's toggle mode (shots: fl-edit-menu-4/-5): Ctrl+Z until something is undone, then Undo shows Ctrl+Alt+Z and
    // Redo Ctrl+Z
    CHECK (same (undoMenuItem (false, 3, true, false).action, "edit.undo"));
    CHECK (same (undoMenuItem (false, 2, true, true).action, "edit.undoBackwards"));
    CHECK (same (redoMenuItem (false, false, 2, true).action, "edit.undo"));
    // nothing to undo: FL's greyed "Undo" with Ctrl+Z (shot fl-edit-menu-0)
    u = undoMenuItem (true, 0, false, false);
    CHECK (u.shown && ! u.enabled && u.step == -1 && same (u.action, "edit.undo"));
    // Clip-launch / Simple: Live's form, Redo always there (greyed: "Nothing to redo"), on Ctrl+Y in both modes
    for (const auto alternate : { true, false })
    {
        r = redoMenuItem (alternate, true, 3, false);
        CHECK (r.shown && ! r.enabled && r.step == -1 && same (r.action, "edit.redo"));
        r = redoMenuItem (alternate, true, 2, true);
        CHECK (r.enabled && r.step == 3 && same (r.action, "edit.redo"));
    }
}

TEST (u190mz_redo_keys_in_every_window)
{
    // Ctrl+Y and Ctrl+Shift+Z redo, and Ctrl+Z / Ctrl+Alt+Z stay the undo keys, wherever the focus is (both modes use the
    // same chords; only what they do changes)
    using namespace lynx::keys;
    KeyMap map;
    for (const auto context : { Context::global, Context::channelRack, Context::pianoRoll, Context::playlist, Context::mixer, Context::browser })
    {
        CHECK (map.find ({ 'Y', ctrl }, context, false) == "edit.redo");
        CHECK (map.find ({ 'Z', ctrl | shift }, context, false) == "edit.redo");
        CHECK (map.find ({ 'Z', ctrl }, context, false) == "edit.undo");
        CHECK (map.find ({ 'Z', ctrl | alt }, context, false) == "edit.undoBackwards");
    }
}
