#pragma once

// Time signature changes for the Simple layout (round 190ol, P6 of /mnt/e/lynx-work/research190oe/TOP25.md: GarageBand's
// most common complaint, G1, is one time signature per project). LYNX's core has signature changes since the FL
// Playlist's time signature markers (Marker with numerator > 0: Song::barsBetween, the metronome's table, the bar
// numbers); this is the Simple layout's way to set them:
//  - the ruler's right-click "Change time signature here..." at the bar under the pointer, and its "Delete" item;
//  - the top bar's "4 / 4" box edits the signature at the song's start.
// A change sits on a bar line; at most one signature change per tick (a new one replaces the one there), and none
// that would repeat the signature already in force. Header only, no JUCE.

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx::signatureedit
{
// The Simple ruler dialog's choices: numerator 1-16, denominator 2, 4, 8 or 16 (research190oe P6). BandLab's top bar (live
// check 190ol, s/ANSWERS.md) offers numerators 1-6 and denominators 1, 2, 4, 8 in two separate lists: topBarNumerators /
// topBarDenominators; LYNX adds 7-16 and 16 after a separator there.
inline constexpr int minNumerator = 1, maxNumerator = 16;
inline constexpr int denominators[] = { 2, 4, 8, 16 };
inline constexpr int bandlabMaxNumerator = 6;
inline constexpr int bandlabDenominators[] = { 1, 2, 4, 8 };
inline int clampDenominator (int d) noexcept
{
    return d <= 1 ? 1 : d <= 2 ? 2 : d <= 4 ? 4 : d <= 8 ? 8 : 16; // (1: BandLab's top bar offers n/1)
}

// The signature in force at `tick`, without the change(s) at exactly `tick` (what is "before" it).
inline std::pair<int, int> before (const Song& song, std::int64_t tick)
{
    std::pair<int, int> sig { std::max (1, song.beatsPerBar), meterUnit (song.beatUnit) };
    for (const auto& m : song.markers())
        if (m.isSignature() && m.tick < tick)
            sig = { m.numerator, m.denominator };
    return sig;
}

// The signature change at exactly `tick` (the last one there), or -1.
inline int changeAt (const Song& song, std::int64_t tick)
{
    int found = -1;
    for (int i = 0; i < (int) song.markers().size(); ++i)
        if (const auto& m = song.markers()[(std::size_t) i]; m.isSignature() && m.tick == tick)
            found = i;
    return found;
}

// The signature at the song's start: a change at tick 0 wins over the song's meter (as Song::barsBetween reads it).
inline std::pair<int, int> atStart (const Song& song)
{
    if (const auto i = changeAt (song, 0); i >= 0)
        return { song.markers()[(std::size_t) i].numerator, song.markers()[(std::size_t) i].denominator };
    return { std::max (1, song.beatsPerBar), meterUnit (song.beatUnit) };
}

// The bar line at or before `tick` (where a change put there takes effect).
inline std::int64_t barLine (const Song& song, std::int64_t tick) { return song.barAt (std::max<std::int64_t> (0, tick)).tick; }

// Removes every signature change at `tick`. True when one went.
inline bool remove (Song& song, std::int64_t tick)
{
    auto& list = song.markers();
    const auto size = list.size();
    list.erase (std::remove_if (list.begin(), list.end(), [tick] (const Marker& m) { return m.isSignature() && m.tick == tick; }), list.end());
    return list.size() != size;
}

// The signature from the bar at `tick` on is n / d. At the song's start it is the song's own meter (a change at tick 0,
// as FL can write, is updated instead); elsewhere a change replaces the one at that bar, and a change that would repeat
// the signature before it is not kept. Returns the bar's tick.
inline std::int64_t set (Song& song, std::int64_t tick, int numerator, int denominator)
{
    numerator = std::clamp (numerator, minNumerator, 32);
    denominator = clampDenominator (denominator);
    const auto at = barLine (song, tick);
    if (at <= 0)
    {
        if (const auto i = changeAt (song, 0); i >= 0)
        {
            auto& m = song.markers()[(std::size_t) i];
            m.numerator = numerator;
            m.denominator = denominator;
            m.name = std::to_string (numerator) + "/" + std::to_string (denominator);
        }
        else
        {
            song.beatsPerBar = numerator;
            song.beatUnit = denominator;
        }
        return 0;
    }
    remove (song, at);
    if (before (song, at) != std::pair { numerator, denominator })
    {
        Marker m;
        m.tick = at;
        m.name = std::to_string (numerator) + "/" + std::to_string (denominator);
        m.numerator = numerator;
        m.denominator = denominator;
        song.addMarker (m);
    }
    return at;
}

// "n / d" as the top bar shows it ("7 / 8")
inline std::string text (std::pair<int, int> sig) { return std::to_string (sig.first) + " / " + std::to_string (sig.second); }
} // namespace lynx::signatureedit
