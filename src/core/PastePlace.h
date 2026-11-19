#pragma once

// Where Ctrl+V puts the clipboard's notes or clips (issue #211, round 190mq), per layout. No JUCE here.
//
// - Pattern layout: FL Studio 2026, the same rule in the Piano roll and the Playlist (captured in FL 2026 by round
//   190mq, /mnt/e/lynx-work/issues211_190mq/s/notes.md, shots f04..f28 and p05..p17):
//   1. the anchor: the start of the time selection made on the ruler, else the spot of the last single click on the
//      ruler (the ruler's double click clears both; Stop does not). When it is in view, the earliest note / clip lands
//      on it and the rest keep their distance from it. (One at the very start is none: see anchorInView.)
//   2. no anchor in view: into the bar line nearest the view's left edge (more than half a bar in: the next one), the
//      earliest item keeping its place within its bar (FL's manual: "pasted starting from the leftmost bar that is
//      visible"), the rest keeping their distance from it.
//   3. landing exactly where they were copied from, they move one grid step to the right (so the copy shows).
// - Clip-launch layout (Ableton Live 12, manual 10.5.4 "Notes in the clipboard will be pasted starting at the location
//   of the insert marker"; the insert marker is the time selection's start or the clicked spot, which in LYNX is the
//   song position): the earliest item lands on the time selection's start, else on the song position.
// - Simple layout (BandLab: a pasted region lands on the playhead, the mobile app's centre marker): the song position.
// With no place to go (no position known), everything stays where it was copied from.

#include "core/LayoutStyle.h"

#include <cstdint>

namespace lynx::pasteplace
{
enum class Rule
{
    flView,     // Pattern layout
    liveInsert, // Clip-launch layout
    playhead    // Simple layout
};

constexpr Rule ruleFor (layoutstyle::Style style) noexcept
{
    switch (style)
    {
        case layoutstyle::Style::clipLaunch: return Rule::liveInsert;
        case layoutstyle::Style::simple: return Rule::playhead;
        case layoutstyle::Style::pattern: break;
    }
    return Rule::flView;
}

struct Bar
{
    std::int64_t start = 0, length = 1;
};

struct Where
{
    std::int64_t first = 0;                // the clipboard's earliest start (ticks)
    std::int64_t viewFrom = 0, viewTo = 0; // the visible time range [from, to)
    std::int64_t selection = -1;           // the time selection's start; -1 = none
    std::int64_t rulerMark = -1;           // FL: the last single click on the ruler; -1 = none (or cleared)
    std::int64_t position = -1;            // the song position (Live's insert marker / BandLab's playhead); -1 = unknown
    std::int64_t grid = 0;                 // the snap step (FL rule 3); 0 = none
};

constexpr bool inView (const Where& w, std::int64_t tick) noexcept { return tick >= 0 && tick >= w.viewFrom && tick < w.viewTo; }

// An anchor at the very start does not count: FL keeps "no time selection" as 0..0, so its Ctrl+A selection from bar 1
// (capture f04: Ctrl+A, Ctrl+C, Ctrl+V pasted by rule 2 and 3, not onto bar 1) is no anchor.
constexpr bool anchorInView (const Where& w, std::int64_t tick) noexcept { return tick > 0 && inView (w, tick); }

// The bar line nearest `tick` (FL FUN_01229d20, the same choice as the time markers' key: past half the bar, the next).
template <typename BarAt>
constexpr std::int64_t nearestBarLine (std::int64_t tick, BarAt barAt) noexcept
{
    const Bar bar = barAt (tick < 0 ? 0 : tick);
    return tick - bar.start > bar.length / 2 ? bar.start + bar.length : bar.start;
}

template <typename BarAt>
constexpr std::int64_t flStart (const Where& w, BarAt barAt) noexcept
{
    std::int64_t target = -1;
    if (anchorInView (w, w.selection))
        target = w.selection;
    else if (anchorInView (w, w.rulerMark))
        target = w.rulerMark;
    else
        target = nearestBarLine (w.viewFrom, barAt) + (w.first - barAt (w.first < 0 ? 0 : w.first).start);
    if (target == w.first)
        target += w.grid;
    return target;
}

// The tick the clipboard's earliest item moves to under `rule`.
template <typename BarAt>
constexpr std::int64_t start (Rule rule, const Where& w, BarAt barAt) noexcept
{
    switch (rule)
    {
        case Rule::flView: return flStart (w, barAt);
        case Rule::liveInsert:
            if (w.selection >= 0)
                return w.selection;
            return w.position >= 0 ? w.position : w.first;
        case Rule::playhead: return w.position >= 0 ? w.position : w.first;
    }
    return w.first;
}

// Uniform bars of `length` ticks (a pattern's bars).
struct UniformBars
{
    std::int64_t length = 1;
    constexpr Bar operator() (std::int64_t tick) const noexcept
    {
        const auto n = tick < 0 ? 0 : tick / length;
        return { n * length, length };
    }
};
} // namespace lynx::pasteplace
