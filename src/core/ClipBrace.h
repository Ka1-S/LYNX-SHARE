#pragma once

// The Clip View's warp time line (round gaps190kf; coverage tempo_warp-0062 / tech-0001): Live's Sample Editor draws, under
// its beat-time ruler, the clip's loop brace and its Start / End markers, and they are dragged there in beats (in time
// for an unwarped clip) [DOC Live 12 manual 8.8 "The markers can be clicked and dragged to new positions in the Sample
// Editor", 8.9 "the loop region is represented by the loop brace. You can click and drag on the loop start and end markers
// to change the position and length of the loop brace", "The left and right arrow keys nudge the loop brace to the
// left/right by the current grid setting", "The up and down arrow keys shift the loop brace left/right in steps the size
// of its length", "Regardless of the position of the loop brace, clip will begin playing at the start marker position";
// SHOT /mnt/e/lynx-work/live190gu/warp/live-clipview-beats-defaults-full.png and gaps190kf/raw (Live 12.4)].
//
// LYNX's grid clip already holds those values (round 190hr): with Loop on, Clip::offset / length are the loop (Live's
// Loop Position / Length) and ClipGridSlot::startMarker is the Start marker inside it; with Loop off, offset / length are
// the Start / End markers. Everything here is in content ticks (Clip::offset's units: content beats × ppq for a warped
// clip, the file's seconds at the song tempo for an unwarped one). JUCE-free, so the tests check the model.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/Song.h"

namespace lynx::clipbrace
{
// Where the markers are, in content ticks
struct Marks
{
    bool loop = false;
    std::int64_t loopStart = 0, loopEnd = 0; // the loop brace (with Loop off: the same span as Start / End, as Live draws it)
    std::int64_t start = 0, end = 0;         // the Start marker; the End marker (Loop off only: with Loop on the loop's end)
};
inline Marks of (const Clip& c, const ClipGridSlot& slot) noexcept
{
    Marks m;
    m.loop = slot.loop;
    m.loopStart = c.offset;
    m.loopEnd = c.offset + std::max<std::int64_t> (1, c.length);
    m.start = slot.loop ? c.offset + std::clamp<std::int64_t> (slot.startMarker, 0, std::max<std::int64_t> (0, c.length - 1)) : c.offset;
    m.end = m.loopEnd;
    return m;
}

enum class Part
{
    none,
    loopStart, // the brace's left end
    loopEnd,   // its right end
    loopBody,  // the brace itself (moves it, its length kept)
    start,     // the Start marker
    end        // the End marker (Loop off)
};

// The clip's fades stay inside it
inline void keepFades (Clip& c) noexcept
{
    c.fadeIn = std::min (c.fadeIn, c.length);
    c.fadeOut = std::min (c.fadeOut, c.length - c.fadeIn);
}

// Part `part` dragged to content tick `tick` (already snapped by the caller; for loopBody the brace's new start). The clip
// keeps at least `minLength` ticks. Returns false when nothing changed.
inline bool move (Clip& c, ClipGridSlot& slot, Part part, std::int64_t tick, std::int64_t minLength) noexcept
{
    const auto before = c;
    const auto beforeMarker = slot.startMarker;
    minLength = std::max<std::int64_t> (1, minLength);
    const auto loopEnd = c.offset + c.length;
    const auto absStart = c.offset + slot.startMarker; // (the Start marker stays where it is when the loop moves under it)
    const auto keepStartIn = [&] {
        slot.startMarker = absStart >= c.offset && absStart < c.offset + c.length ? absStart - c.offset : 0;
    };
    switch (part)
    {
        case Part::none:
            return false;
        case Part::loopStart:
            if (slot.loop)
            {
                c.offset = std::clamp<std::int64_t> (tick, 0, loopEnd - minLength);
                c.offsetFraction = 0.0f;
                c.length = loopEnd - c.offset;
                keepStartIn();
            }
            else // (Loop off: the brace is the clip's span, its left end the Start marker)
                return move (c, slot, Part::start, tick, minLength);
            break;
        case Part::loopEnd:
            c.length = std::max (minLength, tick - c.offset);
            if (slot.startMarker >= c.length)
                slot.startMarker = 0;
            break;
        case Part::loopBody:
            c.offset = std::max<std::int64_t> (0, tick);
            c.offsetFraction = 0.0f;
            if (slot.loop)
                keepStartIn();
            break;
        case Part::start:
            if (slot.loop)
                slot.startMarker = std::clamp<std::int64_t> (tick - c.offset, 0, std::max<std::int64_t> (0, c.length - 1));
            else
            {
                c.offset = std::clamp<std::int64_t> (tick, 0, loopEnd - minLength);
                c.offsetFraction = 0.0f;
                c.length = loopEnd - c.offset;
            }
            break;
        case Part::end:
            c.length = std::max (minLength, tick - c.offset);
            break;
    }
    keepFades (c);
    return ! (c == before) || slot.startMarker != beforeMarker;
}

// The keys of a selected brace [DOC Live 8.9]: Left / Right nudge it by one grid step, Up / Down shift it by its length
// (Up = later, as Live's "up" moves it right). `steps` = ±1.
inline bool nudge (Clip& c, ClipGridSlot& slot, std::int64_t gridTicks, int steps, bool byLength) noexcept
{
    const auto by = byLength ? c.length : std::max<std::int64_t> (1, gridTicks);
    return move (c, slot, Part::loopBody, c.offset + by * steps, 1);
}

// The nearest multiple of `grid` ticks (no snap for grid ≤ 0)
inline std::int64_t snap (double tick, std::int64_t grid) noexcept
{
    if (grid <= 0)
        return (std::int64_t) std::llround (tick);
    return (std::int64_t) std::llround (tick / (double) grid) * grid;
}
} // namespace lynx::clipbrace
