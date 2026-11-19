#pragma once

#include "core/MeterScale.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <iterator>

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

    // FL's Mixer peak hold (round 190jd, live check 190iw Q6, notes/q6-peakhold.txt): advance() with the hold line
    // held flPeakHoldMs after the last higher peak, then falling with a constant acceleration (flPeakFallDb) until it
    // meets the bar or the floor. The bar's ballistics are advance()'s. Nothing resets on Stop: the ticks decay on.
    void advanceFl (float blockPeak, double elapsedMs, float decay = 0.86f) noexcept;

    void resetClip() noexcept
    {
        clipped = false;
        peak = level;
        holdLeft = 0;
        holdMsLeft = 0.0;
        heldMs = 0.0;
        heldGain = peak;
    }

    double holdMsLeft = 0.0; // advance(): the hold left, in ms
    double heldMs = 0.0;     // advanceFl(): time since the hold line was set
    float heldGain = 0.0f;   // advanceFl(): the level it was set at
};

// FL's hold time: the tick stays ~2.6 s after the last higher peak (set at 5571 ms, first move at 8161 ms; again
// 2333 -> 4993 ms; 62 fps film, +-90 ms GUI steps).
constexpr double flPeakHoldMs = 2600.0;
// Then it falls with acceleration: per ~90 ms step 1, 1, 2, 1, 2, 2, 2, 1, 1, 2, 1, 3, 5, 5, 6 px, i.e. ~3.2 dB in the
// first 1.3 s and ~4.5-5 dB/s by then: a fall of a * t^2 dB with a = 1.9 dB/s^2 fits it (3.2 dB at 1.3 s, 4.9 dB/s).
constexpr double flPeakFallAccel = 1.9; // dB / s^2 (half the acceleration)

// How far (dB) FL's hold line has fallen `msSinceSet` ms after it was set: 0 during the hold.
inline double flPeakFallDb (double msSinceSet) noexcept
{
    const auto t = std::max (0.0, msSinceSet - flPeakHoldMs) / 1000.0;
    return flPeakFallAccel * t * t;
}

inline void PeakHold::advanceFl (float blockPeak, double elapsedMs, float decay) noexcept
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
        heldGain = blockPeak;
        heldMs = 0.0;
    }
    else
    {
        heldMs = std::min (heldMs + std::max (0.0, elapsedMs), 600000.0);
        if (heldMs > flPeakHoldMs)
        {
            const auto fallen = heldGain * (float) std::pow (10.0, -flPeakFallDb (heldMs) / 20.0);
            peak = std::max (level, fallen);
            if (peak < 0.001f)
                peak = 0.0f;
        }
    }
    holdMsLeft = std::max (0.0, flPeakHoldMs - heldMs);
    holdLeft = (int) std::ceil (holdMsLeft / tickMs);

    if (blockPeak > 1.0f)
        clipped = true;
}

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

// FL draws the hold tick orange above about -3 dB (live check 190iw Q6), in the meter's own colour below.
constexpr float flTickOrange = 0.70795f; // -3 dB

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
    if (pa != pb || (before.peak >= 0.5f) != (after.peak >= 0.5f) || (before.peak >= flTickOrange) != (after.peak >= flTickOrange)
        || (before.peak >= 1.0f) != (after.peak >= 1.0f))
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
// The mixer meters' dB scale, 0..1 from the bottom: FL's normal law in the Pattern layout (round 190mf), -60 … +6 dB
// linear in dB in the Clip-launch and Simple layouts, FL's alternate law when that option is on (round 190lx, DF-61).
inline float scalePosition (float db) noexcept
{
    return meterscale::positionOfDb (db, meterscale::current());
}

// The dB values printed beside a meter `heightPx` tall (round 162, issue #9: FL prints +3, 0, -3, -6, -12...):
// tried in order of importance, a value is kept when its label is at least `minGapPx` from every kept one, so a
// short meter shows only 0 / -12 / -24 and a tall one all of them. Returned from the top down.
inline std::vector<int> scaleMarks (float heightPx, float minGapPx)
{
    static const int candidates[] = { 0, -12, -24, -48, -6, -36, +3, -3, -18, -9 };
    // (round 190mf: FL's normal law prints FL's own marks, every 3 dB from +3 down to -39 as FL's meter scale)
    static const int flCandidates[] = { 0, -12, -24, -6, -18, -36, +3, -3, -9, -15, -21, -30, -27, -33, -39 };
    const auto law = meterscale::current();
    std::vector<int> kept;

    const bool fl = law == meterscale::Law::flNormal;
    const int* const first = fl ? flCandidates : candidates;
    const int* const last = fl ? std::end (flCandidates) : std::end (candidates);

    for (auto* it = first; it != last; ++it)
    {
        const int db = *it;
        if (db > 2 && law == meterscale::Law::flAlternate) // (FL's alternate scale ends at about +2.5 dB: no +3 mark)
            continue;
        const auto y = scalePosition ((float) db) * heightPx;
        const bool fits = std::all_of (kept.begin(), kept.end(), [&] (int k) { return std::abs (scalePosition ((float) k) * heightPx - y) >= minGapPx; });
        if (fits)
            kept.push_back (db);
    }

    std::sort (kept.begin(), kept.end(), [] (int a, int b) { return a > b; });
    return kept;
}
} // namespace lynx::meters
