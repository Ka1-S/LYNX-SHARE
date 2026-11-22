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

// Round 190pm (research190oe P19): a note's ratchets (Note::ratchets, a LYNX addition) strike it that many times in even
// parts of its length; FL's repeat rate wins when it is set.
constexpr int maxRatchets = 8;

inline int ratchetCount (const Note& note) noexcept { return note.repeat == 0 && note.ratchets > 1 ? std::min (note.ratchets, maxRatchets) : 1; }

// The strikes of a note that plays from `start` for `length` ticks: strike k at start + k * interval, lasting to the
// next one (the last to the note's end). Calls `strike (startTick, lengthTicks, k)` for each, the first (k = 0) being the
// note itself; a note without repeat is one strike. At most `maxStrikes` (a long note at a fast rate stays bounded).
// Ratchets (round 190pm) split `full` ticks (the note's whole played length; -1 = `length`) evenly from `start`: the
// strikes that start before start + length play, the last one cut there (a note cut by the end of its clip).
template <typename Strike>
void forEachStrike (const Note& note, std::int64_t start, std::int64_t length, Strike&& strike, int maxStrikes = 4096, std::int64_t full = -1)
{
    const auto step = interval (note.repeat);
    if (step <= 0 || length <= step)
    {
        if (const auto n = ratchetCount (note); n > 1)
        {
            const auto whole = full > 0 ? std::max (full, length) : length;
            if (whole >= n)
            {
                for (int k = 0; k < n; ++k)
                {
                    const auto from = start + whole * k / n;
                    if (from >= start + length)
                        break;
                    const auto to = std::min (start + whole * (k + 1) / n, start + length);
                    strike (from, std::max<std::int64_t> (1, to - from), k);
                }
                return;
            }
        }
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
    if (step <= 0 || length <= step)
        return length >= ratchetCount (note) ? ratchetCount (note) : 1;
    return (int) std::min<std::int64_t> (4096, (length + step - 1) / step);
}

// Round 190pm: strike k's velocity factor of `count` strikes (Note::ratchetVelocity: a line from 1 to 1 + ratchetVelocity
// on the last strike); exactly 1 when the note has no velocity bend
inline float strikeGain (const Note& note, int k, int count) noexcept
{
    if (note.ratchetVelocity == 0.0f || count <= 1 || k <= 0)
        return 1.0f;
    return 1.0f + note.ratchetVelocity * (float) std::min (k, count - 1) / (float) (count - 1);
}

// A velocity times a strike's factor, within 1/127 … 1 (a factor of exactly 1 leaves it as it is)
inline float strikeVelocity (float velocity, float gain) noexcept
{
    return gain == 1.0f ? velocity : std::clamp (velocity * gain, 1.0f / 127.0f, 1.0f);
}

} // namespace lynx::noterepeat
