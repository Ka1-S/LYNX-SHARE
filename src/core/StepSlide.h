#pragma once

// FL's slide notes of no length (issue #272, round 190ri): an .flp note of length 0 with the slide flag. FL 2026 moves the
// sounding notes to it at once (renders of 3x Osc and the Sampler: at the key from the slide note's tick on, where a slide of 24
// ticks takes its 24) and draws it as a pill, 7 px wide at any zoom, centred on its start, with no name and no slide mark. LYNX
// imported it as one step long, like a step note: it glided for a step and looked stretched.
// Kept as 1 tick (0.5 ms at 120 BPM, what an older LYNX plays) with Note::held, so everything that reads notes sees a length;
// with any other length it is an ordinary slide note.

#include "core/Song.h"

namespace lynx::stepslide
{
inline bool is (const Note& n) noexcept { return n.slide && n.held && n.length == 1; }
inline void make (Note& n) noexcept
{
    n.length = 1;
    n.held = true;
}
constexpr float pillWidth = 7.0f; // pixels, FL's
} // namespace lynx::stepslide
