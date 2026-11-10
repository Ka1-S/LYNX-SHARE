#pragma once

// What the two undo shortcuts do (round 190b, issue #140; FL evidence in docs/analise/REA_rodada167k.md §4).
// - FL's Edit › Undo (UndoAction, Ctrl+Z): undoes the last step, or redoes one when something is undone, so pressing
//   it again and again swaps between the last two states ("undo/redo on the same shortcut").
// - FL's Edit › Undo backwards (UndoBackwardsAction, Ctrl+Alt+Z): one more step back each time.
// - Settings › General › "Alternate undo mode" (AlternateUndoBox, off by default) swaps the two shortcuts: Ctrl+Z
//   walks back through the history (classic undo) and Ctrl+Alt+Z is the undo/redo key.
// - Redo (Ctrl+Y / Ctrl+Shift+Z, a LYNX extra) always redoes one step, in both modes.
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
} // namespace lynx::undokeys
