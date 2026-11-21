#pragma once

// Round 190ok (owner decision D1): an FL project keeps each effect slot's "Smart disable" exactly as FL saved it (0xD4
// bit 0x20; FL processes a slot with it off even in silence, and so does LYNX). Some demos were saved with it off on
// nearly every slot (Exoplanet: 116 of 118), so the project keeps 16 % of a core busy with the song stopped where FL's
// own macro Tools › Macros › "Switch smart disable for all plugins" brings it down by 77 % (round 190of). When an
// imported project is like that, LYNX shows a tip once (the hint bar) and keeps it in the CPU meter's hint while it
// holds; nothing is switched by itself. Header-only, no JUCE.

#include "core/Song.h"

namespace lynx::smarttip
{
struct Count
{
    int off = 0;   // effect slots in use with Smart disable off
    int total = 0; // effect slots in use
};

// The effect slots (every insert, the Master included) that hold an effect, and how many of them have Smart disable off.
inline Count count (const Song& song) noexcept
{
    Count c;
    for (const auto& track : song.mixer)
        for (const auto& slot : track.effects)
        {
            if (! slot.enabled)
                continue;
            ++c.total;
            if (! slot.smartDisable)
                ++c.off;
        }
    return c;
}

// the thresholds: at least this many slots with it off, at least half of the slots, and the stopped project costing at
// least this share of the audio deadline (Exoplanet: 116 / 118 and ~16 %; Thief: 4 / 77 and ~1 %)
inline constexpr int minSlots = 8;
inline constexpr double minStoppedLoad = 0.05;

constexpr bool due (Count c, double stoppedLoad) noexcept
{
    return c.off >= minSlots && c.off * 2 >= c.total && stoppedLoad >= minStoppedLoad;
}
} // namespace lynx::smarttip
