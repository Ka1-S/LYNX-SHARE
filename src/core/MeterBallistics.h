#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Display ballistics of a peak meter (mixer strips): a fast-rising bar with a smooth falloff, a
// peak-hold line that stays put for a while before it falls, and a clip latch that stays lit until
// the user clicks the meter. Pure logic, ticked by the UI timer; independent of JUCE.
namespace lynx::meters
{
struct PeakHold
{
    float level = 0.0f;  // the bar (linear gain)
    float peak = 0.0f;   // the hold line (linear gain)
    int holdLeft = 0;    // ticks before the hold line starts to fall
    bool clipped = false;

    // One display tick with the loudest sample since the previous tick (linear gain).
    // decay: bar falloff per tick; holdTicks: how long the hold line stays; peakFall: its falloff per tick.
    void tick (float blockPeak, float decay = 0.86f, int holdTicks = 45, float peakFall = 0.92f) noexcept
    {
        blockPeak = std::max (0.0f, blockPeak);
        level = std::max (blockPeak, level * decay);
        if (level < 0.001f)
            level = 0.0f;

        if (blockPeak >= peak)
        {
            peak = blockPeak;
            holdLeft = holdTicks;
        }
        else if (holdLeft > 0)
            --holdLeft;
        else
        {
            peak = std::max (level, peak * peakFall);
            if (peak < 0.001f)
                peak = 0.0f;
        }

        if (blockPeak > 1.0f)
            clipped = true;
    }

    // The same ballistics at any frame rate (round 174e: the mixer meters follow the display's refresh, not a 30 Hz
    // timer): `elapsedMs` of display time since the previous call. The bar and the hold line fall as many dB per
    // second as tick() at 30 Hz, and the hold lasts as long (holdTicks / 30 s).
    void advance (float blockPeak, double elapsedMs, float decay = 0.86f, int holdTicks = 45, float peakFall = 0.92f) noexcept
    {
        constexpr double tickMs = 1000.0 / 30.0;
        const auto ticks = std::clamp (elapsedMs / tickMs, 0.0, 30.0);
        blockPeak = std::max (0.0f, blockPeak);
        level = std::max (blockPeak, level * (float) std::pow ((double) decay, ticks));
        if (level < 0.001f)
            level = 0.0f;

        if (blockPeak >= peak)
        {
            peak = blockPeak;
            holdMsLeft = holdTicks * tickMs;
        }
        else if (holdMsLeft > 0.0)
            holdMsLeft = std::max (0.0, holdMsLeft - elapsedMs);
        else
        {
            peak = std::max (level, peak * (float) std::pow ((double) peakFall, ticks));
            if (peak < 0.001f)
                peak = 0.0f;
        }
        holdLeft = (int) std::ceil (holdMsLeft / tickMs);

        if (blockPeak > 1.0f)
            clipped = true;
    }

    void resetClip() noexcept
    {
        clipped = false;
        peak = level;
        holdLeft = 0;
        holdMsLeft = 0.0;
    }

    double holdMsLeft = 0.0; // advance(): the hold left, in ms
};

// The rows of a meter `heightPx` tall that must be redrawn when its bar goes from `before` to `after` (round 174e):
// the band between the two bar tops and around the two hold lines (2 px each), from the top (0) down; empty when
// nothing moved by a whole pixel. `position` maps a level to 0..1 of the height (the meter's scale).
struct DirtyRows
{
    int top = 0, bottom = 0; // [top, bottom)
    bool empty() const noexcept { return bottom <= top; }
    void add (int from, int to) noexcept
    {
        if (to <= from)
            return;
        if (empty())
        {
            top = from;
            bottom = to;
            return;
        }
        top = std::min (top, from);
        bottom = std::max (bottom, to);
    }
};

template <typename Position>
DirtyRows dirtyRows (const PeakHold& before, const PeakHold& after, int heightPx, Position position) noexcept
{
    DirtyRows rows;
    const auto barTop = [&] (float level) { return heightPx - (int) (position (level) * (float) heightPx); };
    const auto a = barTop (before.level), b = barTop (after.level);
    if (a != b)
        rows.add (std::min (a, b), std::max (a, b));
    // the hold line: 2 px from one above its top (drawn only above 0.001)
    const auto line = [&] (float peak) { return peak > 0.001f ? barTop (peak) - 1 : -1000; };
    const auto pa = line (before.peak), pb = line (after.peak);
    if (pa != pb || (before.peak >= 0.5f) != (after.peak >= 0.5f) || (before.peak >= 1.0f) != (after.peak >= 1.0f))
    {
        if (pa > -1000)
            rows.add (std::max (0, pa), std::max (0, pa) + 2);
        if (pb > -1000)
            rows.add (std::max (0, pb), std::max (0, pb) + 2);
    }
    if (before.clipped != after.clipped) // the clip latch: the top 4 px
        rows.add (0, 4);
    rows.top = std::max (0, rows.top);
    rows.bottom = std::min (heightPx, rows.bottom);
    return rows;
}
// The mixer meters' dB scale: -60 dB at the bottom, +6 dB at the top, linear in dB. 0..1 from the bottom.
inline float scalePosition (float db) noexcept
{
    return std::clamp ((db + 60.0f) / 66.0f, 0.0f, 1.0f);
}

// The dB values printed beside a meter `heightPx` tall (round 162, issue #9: FL prints +3, 0, -3, -6, -12...):
// tried in order of importance, a value is kept when its label is at least `minGapPx` from every kept one, so a
// short meter shows only 0 / -12 / -24 and a tall one all of them. Returned from the top down.
inline std::vector<int> scaleMarks (float heightPx, float minGapPx)
{
    static const int candidates[] = { 0, -12, -24, -48, -6, -36, +3, -3, -18, -9 };
    std::vector<int> kept;

    for (auto db : candidates)
    {
        const auto y = scalePosition ((float) db) * heightPx;
        const bool fits = std::all_of (kept.begin(), kept.end(), [&] (int k) { return std::abs (scalePosition ((float) k) * heightPx - y) >= minGapPx; });
        if (fits)
            kept.push_back (db);
    }

    std::sort (kept.begin(), kept.end(), [] (int a, int b) { return a > b; });
    return kept;
}
} // namespace lynx::meters
