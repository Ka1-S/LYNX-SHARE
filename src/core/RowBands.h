#pragma once

// Issue #205 (round 190lw): the Piano roll's row bands. FL lights the rows (keys) of the selected notes across the whole
// Piano roll, so a chord can be read against the notes elsewhere in the pattern. No JUCE here.

#include <bitset>
#include <vector>

#include "core/Song.h"

namespace lynx::rowbands
{
// The keys (0..127) of the notes at `indices` (indices out of range are skipped).
inline std::bitset<128> of (const std::vector<Note>& notes, const std::vector<int>& indices)
{
    std::bitset<128> rows;
    for (const auto i : indices)
        if (i >= 0 && i < (int) notes.size())
            if (const auto key = notes[(std::size_t) i].key; key >= 0 && key < 128)
                rows.set ((std::size_t) key);
    return rows;
}
} // namespace lynx::rowbands
