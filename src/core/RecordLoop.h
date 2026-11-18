#pragma once

// Round 170e3: FL's "Loop record" switch (toolbar LoopRecordBtn, Options › Loop record). FL toggles one global flag
// [DECOMP 0xf1e2a0]; while recording with it off, the play end becomes "never" [DECOMP 0x11d4800], so the song plays
// on instead of going back. LYNX: on (the default, as before), Song-mode recording loops the loop region, one take per
// pass; off, it plays straight through the region and keeps what was played where it was played.

#include <cmath>

#include "core/Song.h"

namespace lynx::recordloop
{
// Whether a Song-mode take records linearly (the song plays on; nothing folds back into a loop).
inline bool linear (PlayMode mode, bool hasLoopRegion, bool loopRecord, bool punchLoopRegion = false) noexcept
{
    return mode == PlayMode::song && (! hasLoopRegion || ! loopRecord || punchLoopRegion);
}

// Round 190lx (DL-61, Live's separate Punch-In / Punch-Out switches, manual 19.3.1): the range an audio take keeps.
// `punching`: punch is on and the song has a loop region (Song mode). In: the take starts at the region's start (else
// where recording starts, `here`); out: it ends at the region's end (else it runs on: `open`), and only then does the
// post-roll stop playback.
struct PunchEnds
{
    std::int64_t in = 0, out = 0;
    bool stopsAtOut = false;
};
inline PunchEnds punchEnds (std::int64_t here, std::int64_t loopStart, std::int64_t loopEnd, bool punching, bool punchIn, bool punchOut,
                            std::int64_t open) noexcept
{
    const auto in = punching && punchIn, out = punching && punchOut;
    return { in ? loopStart : here, out ? loopEnd : open, out };
}

// The notes' rule: punch in/out is for audio and does not change where notes go.
inline bool notesLinear (PlayMode mode, bool hasLoopRegion, bool loopRecord) noexcept
{
    return linear (mode, hasLoopRegion, loopRecord);
}
// Round 170e5: Pattern mode with Loop record off. FL sets the play end to "never" for any recording with the switch
// off, in Pattern mode too (the end otherwise comes from the pattern length, `0ef0 == 0`) [DECOMP 0x11d4800]; only an
// editor time selection would still bound it [DECOMP 0xe4e970] (LYNX's Pattern mode has no selection loop). So the
// position runs on past the pattern's end, the pattern does not start over, and the notes stay where they were
// played, which makes the pattern longer (its length follows its notes).
inline bool patternLinear (PlayMode mode, bool loopRecord) noexcept
{
    return mode == PlayMode::pattern && ! loopRecord;
}

// Where such a take's pattern starts on the transport: the start of the pass the take begins in.
inline double patternOrigin (double transportTick, std::int64_t patternLength) noexcept
{
    const auto length = (double) (patternLength > 0 ? patternLength : 1);
    return std::floor ((transportTick > 0.0 ? transportTick : 0.0) / length) * length;
}
} // namespace lynx::recordloop
