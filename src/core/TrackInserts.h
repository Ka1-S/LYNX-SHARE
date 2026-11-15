#pragma once

// Issue #154 (round 190ht): a linked Playlist track's header can show its mixer track's insert effects as a compact
// list (Settings › LYNX extras, the Playlist menu; off by default). FL keeps an insert's effects in the Mixer's right
// panel only, so this is a LYNX extra; the list is the REAPER-style track-panel FX list the issue shows, drawn in the
// linked header's own column and edited through the Mixer's slot code (the same edits as the Device View, round 183a).
// Design notes: docs/analise/REA_rodada190ht.md. Header only, no JUCE: the list's layout and hit testing.
//
// The list shows the insert's effects in chain order (the Mixer's slots top to bottom, empty slots left out), then an
// "add" row while the insert has a free slot. A row is `rowHeight` px; when there are more rows than fit, the list
// scrolls (the wheel), one row at a time.

#include <algorithm>
#include <vector>

#include "core/MixerOps.h"
#include "core/Song.h"

namespace lynx::trackinserts
{
constexpr int rowHeight = 13;
constexpr int addRow = -2; // the "add" row's item
constexpr int ledWidth = 11; // a row's on/off dot at its left

// How many rows the list has for an insert with `effects` effects: one per effect, plus "add" while a slot is free.
constexpr int itemCount (int effects) noexcept { return effects + (effects < maxEffectSlots ? 1 : 0); }

// How many rows fit in a column `height` px tall (0: the list isn't drawn).
constexpr int rowsThatFit (int height) noexcept { return height >= rowHeight ? height / rowHeight : 0; }

// The first row shown, kept so that the last rows fill the column (never a blank tail while rows are hidden above).
constexpr int clampFirst (int first, int effects, int rows) noexcept
{
    return std::clamp (first, 0, std::max (0, itemCount (effects) - rows));
}

// The item at `y` px from the column's top: an effect index, addRow, or -1 (below the list).
constexpr int itemAt (int effects, int first, int rows, int y) noexcept
{
    if (y < 0 || rows <= 0)
        return -1;
    const auto row = y / rowHeight;
    if (row >= rows)
        return -1;
    const auto item = clampFirst (first, effects, rows) + row;
    return item < effects ? item : item == effects && effects < maxEffectSlots ? addRow : -1;
}

// Where an effect dragged (or dropped) at `y` goes: the effect position it lands before (0 .. effects), by the nearer
// edge of the row under it.
constexpr int dropPosition (int effects, int first, int rows, int y) noexcept
{
    const auto top = clampFirst (first, effects, rows);
    const auto at = top + (std::max (0, y) + rowHeight / 2) / rowHeight;
    return std::clamp (at, 0, effects);
}

// The Mixer slot a new effect added from the list goes into: the first free slot after the last effect, else the first
// free one (as the Device View's "+").
inline int nextFreeRow (const std::vector<EffectSlot>& effects)
{
    const auto rows = mixerops::effectRows (effects);
    for (int r = rows.empty() ? 0 : rows.back() + 1; r < maxEffectSlots; ++r)
        if (mixerops::effectAtRow (effects, r) < 0)
            return r;
    for (int r = 0; r < maxEffectSlots; ++r)
        if (mixerops::effectAtRow (effects, r) < 0)
            return r;
    return 0;
}
} // namespace lynx::trackinserts
