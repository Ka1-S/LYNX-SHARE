#pragma once

// Round 190mr (0.1.30, details pass 8): small pure helpers of the round, header-only (kept out of the widely included
// NoteEdit.h / Rack.h so that their users don't rebuild).
//   - FL's click-and-hold functions in the Piano roll (DE-63; F10 › General › Click-and-hold): the glue neighbour and
//     the slide / porta cycle of a note being added;
//   - Live's rack chain list "Auto Select" (DL-92): which chains process now.

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "core/Rack.h"
#include "core/Song.h"

namespace lynx::details190mr
{
// The note on the same channel and key that touches `note` on its right (`after`: it starts where `note` ends) or on its
// left (it ends where `note` starts): the pair a hold at the resize cursor glues; −1 = none.
inline int touchingNeighbour (const Pattern& pattern, int note, bool after)
{
    if (note < 0 || note >= (int) pattern.notes.size())
        return -1;
    const auto& n = pattern.notes[(std::size_t) note];
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
    {
        const auto& o = pattern.notes[(std::size_t) i];
        if (i == note || o.channel != n.channel || o.key != n.key)
            continue;
        if (after ? o.start == n.start + n.length : o.start + o.length == n.start)
            return i;
    }
    return -1;
}

// The slide / porta cycle of a note being added and held: step 0 plain, 1 slide, 2 porta (then plain again).
inline void setHoldCycle (Note& n, int step) noexcept
{
    n.slide = step % 3 == 1;
    n.porta = step % 3 == 2;
}

// Live's "Auto Select" ("every chain that is currently processing signals becomes selected in the Chain List"): the
// chains that process now, in list order — audible (active; soloed when a chain is soloed), the chain selector in their
// chain-select zone and, in an Instrument / MIDI Effect Rack, a sounding key (`keys`: 128 bits) in their key zone (a Drum
// Rack pad: its Receive note, or any key for "All Notes"). Velocity zones are not checked (the engine tells which keys
// sound, not how hard they were struck). An Audio Effect Rack's chains process whenever the selector opens them.
inline std::vector<int> processingChains (const RackDoc& rack, const std::array<std::uint64_t, 2>& keys)
{
    std::vector<int> out;
    const auto anySolo = std::any_of (rack.chains.begin(), rack.chains.end(), [] (const RackChain& c) { return c.solo; });
    const auto sounding = [&keys] (int k) { return k >= 0 && k < 128 && (keys[(std::size_t) (k >> 6)] >> (k & 63) & 1u) != 0; };
    const auto anyKey = keys[0] != 0 || keys[1] != 0;
    for (int i = 0; i < (int) rack.chains.size(); ++i)
    {
        const auto& c = rack.chains[(std::size_t) i];
        if (! c.active || (anySolo && ! c.solo) || ! rack::inZone (c.select, rack.selector))
            continue;
        auto plays = rack.kind == RackDoc::Kind::audioEffect;
        if (! plays && rack.drum)
            plays = c.receive < 0 ? anyKey : sounding (c.receive);
        else if (! plays)
            for (int k = std::max (0, c.key.lo); k <= std::min (127, c.key.hi) && ! plays; ++k)
                plays = sounding (k);
        if (plays)
            out.push_back (i);
    }
    return out;
}
} // namespace lynx::details190mr
