#pragma once

// FL Studio's Piano roll View › Scale highlighting › Automatic (round 190jv, coverage pianoroll-0183; REA of
// FLEngine_x64.dll in docs/analise/REA_rodada190jv.md, "pr" section). FL guesses the key of the edited channel's notes
// and highlights that key's scale; the guess is Krumhansl-Schmuckler key finding, as FL does it:
//  - a pitch-class histogram: every counted note adds 0.25 to its pitch class (FUN_00ea8a80; the note's length and
//    velocity do not count). The notes counted are the edited channel's; with some of them selected, only the selected.
//  - fewer than 3 notes counted: no key (FL then highlights no rows at all).
//  - the histogram's Pearson correlation with the Krumhansl-Kessler major and minor profiles (FL's tables at 0x1526ad8 /
//    0x1526b38, their means 3.4825 / 3.709167) rotated to each of the 12 roots (FUN_00dd3ed0; FL squares the deviations
//    in single precision); the best of the 24 wins, major C … B first, then minor C … B; a tie keeps the earlier one.
//  - the highlighted scale (FUN_00e500c0, the out-of-scale sets at 0xe504ac / 0xe504cc): major = 0 2 4 5 7 9 11,
//    minor = 0 2 3 5 7 8 11 (FL's Automatic minor is the harmonic minor: its out-of-scale set is 1 4 6 9 10).
// Header only, no JUCE.

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "core/Song.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::scaledetect
{
constexpr int none = -1;    // fewer than 3 notes counted
constexpr int minNotes = 3; // FL: `if (iVar6 < 3) return -1`

// The guessed key: 0 … 11 = C … B major, 12 … 23 = C … B minor; `none` without a guess.
inline int detect (const std::array<double, 12>& histogram, int counted)
{
    if (counted < minNotes)
        return none;

    static constexpr std::array<double, 12> majorProfile { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr std::array<double, 12> minorProfile { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    constexpr double majorMean = 3.4825, minorMean = 3.709167; // (FL's constants, as stored)

    const auto square = [] (double v) { const auto f = (float) v; return (double) (float) (f * f); };

    double mean = 0.0;
    for (const auto h : histogram)
        mean += h;
    mean *= 1.0 / 12.0;

    std::array<double, 24> r {};
    for (int root = 0; root < 12; ++root)
    {
        double sumMajor = 0.0, sumMinor = 0.0, histSq = 0.0, majorSq = 0.0, minorSq = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const auto h = histogram[(std::size_t) ((root + i) % 12)] - mean;
            histSq += square (h);
            sumMajor += (majorProfile[(std::size_t) i] - majorMean) * h;
            majorSq += square (majorProfile[(std::size_t) i] - majorMean);
            sumMinor += (minorProfile[(std::size_t) i] - minorMean) * h;
            minorSq += square (minorProfile[(std::size_t) i] - minorMean);
        }
        const auto dMajor = std::sqrt (majorSq * histSq), dMinor = std::sqrt (minorSq * histSq);
        r[(std::size_t) root] = dMajor > 0.0 ? sumMajor / dMajor : 0.0;
        r[(std::size_t) root + 12] = dMinor > 0.0 ? sumMinor / dMinor : 0.0;
    }

    int best = 0;
    for (int k = 1; k < 24; ++k)
        if (r[(std::size_t) k] > r[(std::size_t) best])
            best = k;
    return best;
}

// The notes FL counts: the edited channel's (`edits`), only the selected ones when any of those is selected.
template <typename Edits, typename Selected>
int detect (const std::vector<Note>& notes, Edits&& edits, Selected&& selected)
{
    auto anySelected = false;
    for (int i = 0; i < (int) notes.size() && ! anySelected; ++i)
        anySelected = edits (notes[(std::size_t) i]) && selected (i);

    std::array<double, 12> histogram {};
    int counted = 0;
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(std::size_t) i];
        if (! edits (n) || (anySelected && ! selected (i)))
            continue;
        histogram[(std::size_t) (((n.key % 12) + 12) % 12)] += 0.25;
        ++counted;
    }
    return detect (histogram, counted);
}

inline int rootOf (int key) noexcept { return key < 0 ? -1 : key % 12; }
inline bool isMinor (int key) noexcept { return key >= 12; }

// Whether MIDI `note` is in the guessed key's highlighted scale (always false without a guess: FL highlights nothing).
inline bool inScale (int key, int note) noexcept
{
    if (key < 0 || key > 23)
        return false;
    static constexpr std::array<bool, 12> major { true, false, true, false, true, true, false, true, false, true, false, true };
    static constexpr std::array<bool, 12> minor { true, false, true, true, false, true, false, true, true, false, false, true };
    const auto degree = (std::size_t) (((note - key % 12) % 12 + 12) % 12);
    return isMinor (key) ? minor[degree] : major[degree];
}

// "A Minor": FL's caption for the guess (FUN_00e500c0: the root's name, then "Major" / "Minor"); English, sharps.
inline std::string name (int key)
{
    if (key < 0 || key > 23)
        return {};
    static constexpr std::array<const char*, 12> roots { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (roots[(std::size_t) (key % 12)]) + " " + (isMinor (key) ? TR_NOOP ("Minor") : TR_NOOP ("Major"));
}
} // namespace lynx::scaledetect
