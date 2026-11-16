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
} // namespace lynx::stripinserts
