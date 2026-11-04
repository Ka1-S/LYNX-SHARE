#pragma once

// Round 170e3: FL's "Loop record" switch (toolbar LoopRecordBtn, Options › Loop record). FL toggles one global flag
// [DECOMP 0xf1e2a0]; while recording with it off, the play end becomes "never" [DECOMP 0x11d4800], so the song plays
// on instead of going back. LYNX: on (the default, as before), Song-mode recording loops the loop region, one take per
// pass; off, it plays straight through the region and keeps what was played where it was played.

#include "core/Song.h"

namespace lynx::recordloop
{
// Whether a Song-mode take records linearly (the song plays on; nothing folds back into a loop).
inline bool linear (PlayMode mode, bool hasLoopRegion, bool loopRecord, bool punchLoopRegion = false) noexcept
{
    return mode == PlayMode::song && (! hasLoopRegion || ! loopRecord || punchLoopRegion);
}

// The notes' rule: punch in/out is for audio and does not change where notes go.
inline bool notesLinear (PlayMode mode, bool hasLoopRegion, bool loopRecord) noexcept
{
    return linear (mode, hasLoopRegion, loopRecord);
}
} // namespace lynx::recordloop
