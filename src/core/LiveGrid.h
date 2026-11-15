#pragma once

// Round 190hr (Live's clip editor grid and Quantize, coverage rows detail-055 and miditools-098): the Clip-launch layout's
// docked note editor can use the reference's grid instead of FL's snap box. Header only, no JUCE.
//
// Evidence (clean-room, facts in our own words):
//  - [DATA] every clip of Live's 19 shipped sets saves `Grid { FixedNumerator, FixedDenominator, GridIntervalPixel,
//    Ntoles, SnapToGrid, Fixed }`; DefaultLiveSet: 1 / 16, 20 px, Ntoles 2, snap on, Fixed false (adaptive). Values seen:
//    Fixed false ×432 / true ×100; denominators 16, 4, 8, 32; GridIntervalPixel 20 (529×), 10, 22, 60.
//  - [STR] Live 12.4's grid context menu (AGridContextMenu): sMenuGridSetWidest / Wide / Medium / Narrow / Narrowest,
//    sMenuGridAdaptive, sMenuGridFixed (+ " Bars"), sMenuGridTriplet, sMenuGridQuintuplet / Septuplet (Live 12's odd
//    tuplets, not built here).
//  - [SHOT] /mnt/e/lynx-work/ableton190o/shots/live-01-clipview-notes.png: the editor's grid value "1/16 ▾" at its
//    top right.
//  - [DOC] Live's manual (keys): Ctrl+1 narrower grid, Ctrl+2 wider, Ctrl+3 triplet grid, Ctrl+4 snap to grid on / off,
//    Ctrl+5 fixed / adaptive. (Quantize Ctrl+U / Quantize Settings… Ctrl+Shift+U are the MIDI Tools' Quantize tool in
//    Live 12: ui/MidiToolsPanel, core/MidiTools.h.)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::livegrid
{
// The adaptive grid's densities, widest first, as GridIntervalPixel: the smallest on-screen distance between two grid
// lines. Medium = 20 px, the shipped sets' default [DATA]; the other steps halve / double it [inferred].
constexpr int densityCount = 5;
constexpr int densityPixels[densityCount] = { 80, 40, 20, 10, 5 }; // Widest, Wide, Medium, Narrow, Narrowest
constexpr int mediumDensity = 2;
inline const char* densityName (int d) noexcept
{
    static const char* names[densityCount] = { "Widest", "Wide", "Medium", "Narrow", "Narrowest" };
    return names[std::clamp (d, 0, densityCount - 1)];
}

// The fixed grid values, widest first, as numerator / denominator of a whole note: 8 Bars … 1/32 [DATA: the values sets
// hold; Live's Launch quantisation list has the same steps]
struct Fraction
{
    int num, den;
};
constexpr int fixedCount = 9;
constexpr Fraction fixedValues[fixedCount] = { { 8, 1 }, { 4, 1 }, { 2, 1 }, { 1, 1 }, { 1, 2 }, { 1, 4 }, { 1, 8 }, { 1, 16 }, { 1, 32 } };
constexpr int defaultFixed = 7; // 1/16 [DATA DefaultLiveSet]

struct Grid
{
    bool fixed = false;             // Live's Fixed (Ctrl+5); off = adaptive
    int density = mediumDensity;    // adaptive: 0 Widest … 4 Narrowest
    int fixedIndex = defaultFixed;  // fixed: into fixedValues
    bool triplet = false;           // Ntoles 3 (Ctrl+3)
    bool snap = true;               // SnapToGrid (Ctrl+4)
    bool operator== (const Grid&) const = default;
};

// Ticks of a fixed value (a "bar" here is a whole note: Live's fixed values are note fractions)
inline std::int64_t fractionTicks (Fraction f) noexcept { return (std::int64_t) 4 * ppq * f.num / std::max (1, f.den); }

// The note fractions an adaptive grid picks from, longest first: 8 whole notes … 1/64
inline std::int64_t adaptiveTicks (double pixelsPerTick, int density, bool triplet) noexcept
{
    const auto minPixels = (double) densityPixels[std::clamp (density, 0, densityCount - 1)];
    std::int64_t step = (std::int64_t) 4 * ppq * 8;
    const auto smallest = (std::int64_t) 4 * ppq / 64;
    // halve while the next finer step still keeps the lines `minPixels` apart
    while (step / 2 >= smallest && (double) (step / 2) * (triplet ? 2.0 / 3.0 : 1.0) * pixelsPerTick >= minPixels)
        step /= 2;
    return triplet ? std::max<std::int64_t> (1, step * 2 / 3) : step;
}

// The grid's step in ticks at a zoom of `pixelsPerTick`
inline std::int64_t stepTicks (const Grid& g, double pixelsPerTick) noexcept
{
    if (! g.fixed)
        return adaptiveTicks (pixelsPerTick, g.density, g.triplet);
    const auto t = fractionTicks (fixedValues[std::clamp (g.fixedIndex, 0, fixedCount - 1)]);
    return g.triplet ? std::max<std::int64_t> (1, t * 2 / 3) : t;
}

// Ctrl+1 (narrower) / Ctrl+2 (wider): the adaptive density or the fixed value one step. False at the ends.
inline bool narrower (Grid& g) noexcept
{
    auto& v = g.fixed ? g.fixedIndex : g.density;
    const auto last = g.fixed ? fixedCount - 1 : densityCount - 1;
    if (v >= last)
        return false;
    ++v;
    return true;
}
inline bool wider (Grid& g) noexcept
{
    auto& v = g.fixed ? g.fixedIndex : g.density;
    if (v <= 0)
        return false;
    --v;
    return true;
}

// The grid as the editor's value shows it: "1/16", "1/8T", "1 Bar", "2 Bars" (an adaptive grid shows its current step).
// `triplet`: `ticks` is a triplet step (two thirds of the plain value named).
inline std::string stepText (std::int64_t ticks, bool triplet)
{
    const auto whole = (std::int64_t) 4 * ppq;
    const auto plain = triplet ? ticks * 3 / 2 : ticks;
    std::string text;
    if (plain >= whole && plain % whole == 0)
        text = std::to_string (plain / whole) + (plain == whole ? " Bar" : " Bars");
    else if (plain > 0 && whole % plain == 0)
        text = "1/" + std::to_string (whole / plain);
    else
        return std::to_string (ticks) + " ticks";
    return triplet ? text + "T" : text;
}

// .als: Live's clip Grid element ↔ Grid (GridIntervalPixel → the nearest density)
inline Grid fromLive (int fixedNum, int fixedDen, int intervalPixel, int ntoles, bool snap, bool fixed) noexcept
{
    Grid g;
    g.fixed = fixed;
    g.snap = snap;
    g.triplet = ntoles == 3;
    g.fixedIndex = defaultFixed;
    for (int i = 0; i < fixedCount; ++i)
        if ((std::int64_t) fixedValues[i].num * std::max (1, fixedDen) == (std::int64_t) fixedNum * fixedValues[i].den)
            g.fixedIndex = i;
    int best = mediumDensity;
    for (int d = 0; d < densityCount; ++d)
        if (std::abs (densityPixels[d] - intervalPixel) < std::abs (densityPixels[best] - intervalPixel))
            best = d;
    g.density = best;
    return g;
}

} // namespace lynx::livegrid
