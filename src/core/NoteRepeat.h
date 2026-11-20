#pragma once

// FL's note repeat (issue #208): the Channel Rack graph editor's "Repeat" lane and the Piano roll's note properties
// (FL Studio 2026; REA round 190ma, docs/analise/REA_rodada190ma.md). A note's .flp byte 0x11 is the index of a rate
// in FL's table (FLEngine 0x154a138: "No repeat", "1/4 (Beat)", "1/8  dotted", ...): the note is struck again at that
// rate for as long as it lasts. Header only, no JUCE.

#include <algorithm>
#include <array>
#include <cstdint>

#include "core/Song.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::noterepeat
{
constexpr int rateCount = 14; // 1..14 (0 = no repeat)

// FL's rate names, as its hint bar shows them (index 0 = no repeat)
inline const char* rateName (int index) noexcept
{
    static constexpr std::array<const char*, rateCount + 1> names { TR_NOOP ("No repeat"), "1/4 (Beat)", "1/8 dotted", "1/4 triplet", "1/8",
                                                                    "1/16 dotted", "1/8 triplet", "1/16 (Step)", "1/32 dotted",
                                                                    "1/16 triplet", "1/32 (Half step)", "1/64 dotted", "1/32 triplet",
                                                                    "1/64 (Quarter step)", "1/64 triplet" };
    return index >= 0 && index <= rateCount ? names[(std::size_t) index] : "";
}

// The time between two strikes, in LYNX ticks (ppq a quarter note): 0 for no repeat
constexpr std::int64_t interval (int index) noexcept
{
    // in 1/960 beats: 1/4, 1/8., 1/4T, 1/8, 1/16., 1/8T, 1/16, 1/32., 1/16T, 1/32, 1/64., 1/32T, 1/64, 1/64T
    constexpr std::array<int, rateCount> ticks960 { 960, 720, 640, 480, 360, 320, 240, 180, 160, 120, 90, 80, 60, 40 };
    return index >= 1 && index <= rateCount ? (std::int64_t) ppq * ticks960[(std::size_t) (index - 1)] / 960 : 0;
}

// The strikes of a note that plays from `start` for `length` ticks: strike k at start + k * interval, lasting to the
// next one (the last to the note's end). Calls `strike (startTick, lengthTicks, k)` for each, the first (k = 0) being the
// note itself; a note without repeat is one strike. At most `maxStrikes` (a long note at a fast rate stays bounded).
template <typename Strike>
void forEachStrike (const Note& note, std::int64_t start, std::int64_t length, Strike&& strike, int maxStrikes = 4096)
{
    const auto step = interval (note.repeat);
    if (step <= 0 || length <= step)
    {
        strike (start, length, 0);
        return;
    }
    int k = 0;
    for (std::int64_t t = 0; t < length && k < maxStrikes; t += step, ++k)
        strike (start + t, std::min (step, length - t), k);
}

inline int strikeCount (const Note& note, std::int64_t length) noexcept
{
    const auto step = interval (note.repeat);
    return step <= 0 || length <= step ? 1 : (int) std::min<std::int64_t> (4096, (length + step - 1) / step);
}
} // namespace lynx::noterepeat
