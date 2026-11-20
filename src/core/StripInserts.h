#pragma once

// Issue #154, part 2 (round 190io): the Mixer view that lists each strip's insert effects, REAPER-style (the reporter's
// second picture: an FX list at the top of every mixer strip). A LYNX extra, off by default (the owner's choice), so
// FL's Mixer is unchanged unless it is switched on: Mixer menu › View › "Insert effects on the strips", Settings ›
// LYNX extras (ui-options mixerStripInserts). The list is the linked track headers' one (core/TrackInserts.h, round
// 190ht): the same rows, hit tests and edits (bypass dot, window, drag to reorder, Ctrl+drag to copy, onto another
// strip to move, the Mixer slot menu, "+ Add"). It sits under the strip's header band. No JUCE.

#include <algorithm>

#include "core/MixerLayout.h"
#include "core/TrackInserts.h"

namespace lynx::stripinserts
{
constexpr bool defaultOn = false;

// The Compact layouts (20-22 px strips) have no room for effect names: the list shows in the Wide layouts and Extra large.
constexpr bool shownIn (mixerlayout::Layout layout) noexcept { return ! mixerlayout::isCompact (layout); }

// How many rows the list has in a Mixer `height` px tall: 3 on a short Mixer, up to 6 on a tall one (scrolls past).
constexpr int rowsFor (int height) noexcept { return std::clamp ((height - 240) / 70, 3, 6); }

// The list's height with its margins (2 px above and below).
constexpr int height (int rows) noexcept { return rows * trackinserts::rowHeight + 4; }

// FL's Extra large Mixer layout (round 190jd, live check 190iw Q6, q6-fl-mixer-extralarge*.png): every strip lists its
// insert's effects as text rows ~23 px apart at the strip's foot, under the pan / stereo knobs: only the used slots in
// slot order (the Master shows just slot 4's effect, no empty rows), names cut in the middle ("1 Fru..EQ 2"), no
// bypass dots or icons, and no "add" row. That list is FL's own, so it shows in Extra large whenever the LYNX view
// above (the list under the band) is off. `Geometry` is a list's rows: the LYNX view's (13 px, a dot, "+ Add") or FL's.
struct Geometry
{
    int rowHeight = trackinserts::rowHeight;
    bool addRow = true;  // a "+ Add" row while a slot is free
    bool dots = true;    // the on / off dot at a row's left
};
constexpr Geometry lynxView {};
constexpr Geometry flExtraLarge { 23, false, false };

// FL's list shows in Extra large unless the LYNX view (the list under the band) is on.
constexpr bool flListShown (mixerlayout::Layout layout, bool lynxViewOn) noexcept
{
    return layout == mixerlayout::Layout::extraLarge && ! lynxViewOn;
}

// The list's height with its margins (2 px above and below).
constexpr int height (Geometry g, int rows) noexcept { return rows * g.rowHeight + 4; }

// The rows a list shows for an insert with `effects` effects (one per effect, plus "add" while a slot is free).
constexpr int itemCount (Geometry g, int effects) noexcept { return effects + (g.addRow && effects < maxEffectSlots ? 1 : 0); }

// How many rows fit in a column `h` px tall.
constexpr int rowsThatFit (Geometry g, int h) noexcept { return h >= g.rowHeight ? h / g.rowHeight : 0; }

// The first row shown, kept so that the last rows fill the column.
constexpr int clampFirst (Geometry g, int first, int effects, int rows) noexcept
{
    return std::clamp (first, 0, std::max (0, itemCount (g, effects) - rows));
}

// The item at `y` px from the column's top: an effect index, trackinserts::addRow, or -1 (below the list).
constexpr int itemAt (Geometry g, int effects, int first, int rows, int y) noexcept
{
    if (y < 0 || rows <= 0)
        return -1;
    const auto row = y / g.rowHeight;
    if (row >= rows)
        return -1;
    const auto item = clampFirst (g, first, effects, rows) + row;
    return item < effects ? item : item == effects && g.addRow && effects < maxEffectSlots ? trackinserts::addRow : -1;
}

// Where an effect dragged at `y` goes: the effect position it lands before (0 .. effects).
constexpr int dropPosition (Geometry g, int effects, int first, int rows, int y) noexcept
{
    const auto top = clampFirst (g, first, effects, rows);
    const auto at = top + (std::max (0, y) + g.rowHeight / 2) / g.rowHeight;
    return std::clamp (at, 0, effects);
}
} // namespace lynx::stripinserts
