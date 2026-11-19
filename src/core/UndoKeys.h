#pragma once

// What the two undo shortcuts do (round 190b, issue #140; FL evidence in docs/analise/REA_rodada167k.md §4).
// - FL's Edit › Undo (UndoAction, Ctrl+Z): undoes the last step, or redoes one when something is undone, so pressing
//   it again and again swaps between the last two states ("undo/redo on the same shortcut").
// - FL's Edit › Undo backwards (UndoBackwardsAction, Ctrl+Alt+Z): one more step back each time.
// - Settings › General › "Alternate undo mode" (AlternateUndoBox) swaps the two shortcuts: Ctrl+Z walks back through
//   the history (classic undo) and Ctrl+Alt+Z is the undo/redo key.
// - Redo (Ctrl+Y / Ctrl+Shift+Z, a LYNX extra) always redoes one step, in both modes.
//
// Round 190mz (issue #214): FL 2026 (26.1.7) ships with "Alternate undo mode" ON, so Ctrl+Z steps back through the
// history out of the box (as in Live, BandLab and most programs). Evidence: the option's variable is a typed constant
// initialised to 1 (FLEngine_x64.dll .data 0x15452d0, reached through 0x15e2f18); its loader (FUN_0119f820) reads
// HKCU\Software\Image-Line\Shared\General "Alternate undo mode" with the variable's own value as the default, and only
// the check box (FUN_0135d5d0) writes it otherwise. Rounds 167k / 190b had read the form's check box (no "Checked" in
// the DFM) as "off". Shots of FL's General › Undo history panel and its Edit menu in both modes:
// /mnt/e/lynx-work/issue214_190mz/shots/ref.
namespace lynx::undokeys
{
enum class Step
{
    none,
    undo,
    redo
};

// FL's Undo: undo, or redo when the last thing done was an undo
constexpr Step toggle (bool canUndo, bool canRedo) noexcept { return canRedo ? Step::redo : canUndo ? Step::undo : Step::none; }
// FL's Undo backwards: always a step further back
constexpr Step backwards (bool canUndo) noexcept { return canUndo ? Step::undo : Step::none; }

// The "Undo" shortcut (edit.undo, Ctrl+Z) and the toolbar's Undo button
constexpr Step undoKey (bool alternateMode, bool canUndo, bool canRedo) noexcept
{
    return alternateMode ? backwards (canUndo) : toggle (canUndo, canRedo);
}

// The "Undo backwards" shortcut (edit.undoBackwards, Ctrl+Alt+Z)
constexpr Step backwardsKey (bool alternateMode, bool canUndo, bool canRedo) noexcept
{
    return alternateMode ? toggle (canUndo, canRedo) : backwards (canUndo);
}

// FL 2026's default for "Alternate undo mode" (round 190mz): on, in every layout
inline constexpr bool alternateModeDefault = true;

// The saved setting (ui-options.txt). "alternateUndoMode" is round 190mz's key and wins wherever it is in the file.
// Before it, every version wrote "alternateUndo" with the old default (off) whether or not anyone had touched the
// switch, so an old 0 means "never chosen" and gives the new default; an old 1 was a choice and stays.
struct SettingsReader
{
    bool alternate = alternateModeDefault;
    bool sawCurrentKey = false;

    void legacyKey (bool on) noexcept
    {
        if (! sawCurrentKey)
            alternate = on || alternateModeDefault;
    }
    void currentKey (bool on) noexcept
    {
        alternate = on;
        sawCurrentKey = true;
    }
};

// FL 2026's Edit menu (round 190mz): "Undo <step>", and "Redo <step>" while something is undone. The shortcut shown
// beside each is the key that does it now, so it changes with the mode and the history:
//   - default mode: Undo shows Ctrl+Z while nothing is undone, then Ctrl+Alt+Z; Redo shows Ctrl+Z (the undo/redo key);
//   - alternate mode: Undo shows Ctrl+Z, Redo shows Ctrl+Alt+Z.
// The Clip-launch and Simple layouts show Live's form: both items always, "Nothing to undo" / "Nothing to redo" when
// there is none, and Redo with Ctrl+Y (edit.redo), the redo key of Live and BandLab.
struct MenuItem
{
    bool shown = false;
    bool enabled = false;
    int step = -1;                // the history entry it names (AudioEngine::getHistoryLabel); -1: none
    const char* action = nullptr; // the key map action whose key is shown beside it
};

constexpr MenuItem undoMenuItem (bool alternateMode, int position, bool canUndo, bool canRedo) noexcept
{
    return { true, canUndo, canUndo ? position : -1, alternateMode || ! canRedo ? "edit.undo" : "edit.undoBackwards" };
}

constexpr MenuItem redoMenuItem (bool alternateMode, bool liveStyle, int position, bool canRedo) noexcept
{
    return { canRedo || liveStyle, canRedo, canRedo ? position + 1 : -1,
             liveStyle ? "edit.redo" : (alternateMode ? "edit.undoBackwards" : "edit.undo") };
}
} // namespace lynx::undokeys
