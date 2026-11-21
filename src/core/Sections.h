#pragma once

// The Sections lane (round 190ol, P1 of /mnt/e/lynx-work/research190oe/TOP25.md): named song sections (Intro, Verse,
// Chorus...) over the whole arrangement, and the commands that move, copy, duplicate, replace or delete a section with
// everything under it — the clips of every track, the lane automation and the time markers — in one undo step. A LYNX
// extra modelled on GarageBand's Arrangement track (Apple's guide: "Add arrangement markers" — a new marker is eight
// bars long and starts at the end of the last one; "Edit arrangement markers" — drag moves a marker with its regions,
// Option-drag copies it, Command-drag onto another replaces that one, Delete once empties it and again removes it) and
// Studio Pro's Arranger track (moving a block moves everything under it). FL and Live have none; Live users ask for
// "markers to scenes" (createScenes below).
//
// The model is Song::sections() (SongSection in Song.h): sorted by start, no overlaps, gaps allowed, each on whole bars
// of the meter in force. The content commands reuse core/ArrangeOps.h's time commands (copyTime / pasteTime / deleteTime
// carry the clips, the markers and the lane automation); those commands keep the sections in step themselves
// (insertTime / deleteTime here). A section's meter goes with it: where a move or copy would change the time signature
// of what it lands next to, signature changes are added so every bar keeps the meter it had.
//
// Header only, no JUCE. Tested in tests/Sections190olTests.inc.

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx
{
namespace sections
{
inline constexpr int defaultBars = 8; // GarageBand: "When you add an arrangement marker, it is eight bars long"

// GarageBand's names, in its order for new sections (the Name menu offers them too); after the fifth: "Section N"
// (the UI shows them translated with the context "@song section: ", ui/SectionsLane190ol.h: "Chorus" is also the effect)
inline constexpr const char* presetNames[] = { "Intro", "Verse", "Chorus", "Bridge", "Outro" };

// The automatic colours (SongSection::colour 0): pastel tones in GarageBand's manner, by the section's place. LYNX's own.
inline constexpr std::uint32_t palette[] = { 0xFF5B8DEF, 0xFF4FB286, 0xFFE0A33A, 0xFFD5577A, 0xFF8E6BE0, 0xFF3FB3C4, 0xFFE07A4F, 0xFF9AAF3B };
inline std::uint32_t colourOf (const std::vector<SongSection>& list, int index) noexcept
{
    if (index >= 0 && index < (int) list.size() && list[(std::size_t) index].colour != 0)
        return list[(std::size_t) index].colour;
    return palette[(std::size_t) std::max (0, index) % (sizeof (palette) / sizeof (palette[0]))];
}

// The section that holds `tick` (start <= tick < end), or -1.
inline int indexAt (const std::vector<SongSection>& list, std::int64_t tick) noexcept
{
    for (int i = 0; i < (int) list.size(); ++i)
        if (tick >= list[(std::size_t) i].start && tick < list[(std::size_t) i].end())
            return i;
    return -1;
}

inline int indexOfUid (const std::vector<SongSection>& list, std::uint64_t uid) noexcept
{
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(std::size_t) i].uid == uid)
            return i;
    return -1;
}

inline std::uint64_t newUid (const std::vector<SongSection>& list) noexcept
{
    std::uint64_t most = 0;
    for (const auto& s : list)
        most = std::max (most, s.uid);
    return most + 1;
}

// Sorted, no empty section, no overlap (a section that runs into the next one ends where it starts), a uid on each.
inline void normalize (std::vector<SongSection>& list)
{
    list.erase (std::remove_if (list.begin(), list.end(), [] (const SongSection& s) { return s.length <= 0 || s.start < 0; }), list.end());
    std::stable_sort (list.begin(), list.end(), [] (const SongSection& a, const SongSection& b) { return a.start < b.start; });
    for (std::size_t i = 0; i + 1 < list.size(); ++i)
        if (list[i].end() > list[i + 1].start)
            list[i].length = list[i + 1].start - list[i].start;
    list.erase (std::remove_if (list.begin(), list.end(), [] (const SongSection& s) { return s.length <= 0; }), list.end());
    for (auto& s : list)
        if (s.uid == 0 || std::count_if (list.begin(), list.end(), [&s] (const SongSection& o) { return o.uid == s.uid; }) > 1)
            s.uid = newUid (list);
}

// The name a new section gets: the first of GarageBand's names no section has yet, else "Section N".
inline std::string nextName (const std::vector<SongSection>& list)
{
    for (const auto* name : presetNames)
        if (std::none_of (list.begin(), list.end(), [name] (const SongSection& s) { return s.name == name; }))
            return name;
    for (int n = (int) list.size() + 1;; ++n)
    {
        auto name = "Section " + std::to_string (n);
        if (std::none_of (list.begin(), list.end(), [&name] (const SongSection& s) { return s.name == name; }))
            return name;
    }
}

// The tick `bars` whole bars after `from` (bars of the meter in force, time signature changes included).
inline std::int64_t barsAfter (const Song& song, std::int64_t from, int bars)
{
    auto at = std::max<std::int64_t> (0, from);
    for (int i = 0; i < bars; ++i)
        at += std::max<std::int64_t> (1, song.barAt (at).length);
    return at;
}

// The bar line nearest to `tick` (a section's edges sit on bar lines).
inline std::int64_t nearestBar (const Song& song, std::int64_t tick)
{
    const auto bar = song.barAt (std::max<std::int64_t> (0, tick));
    return tick - bar.tick < bar.length / 2 ? bar.tick : bar.tick + bar.length;
}

// GarageBand's "+": a section of eight bars at the end of the last one (or at the song's start). Its index.
inline int add (Song& song, std::string name = {})
{
    auto& list = song.sections();
    const auto start = list.empty() ? (std::int64_t) 0 : list.back().end();
    SongSection s;
    s.start = start;
    s.length = barsAfter (song, start, defaultBars) - start;
    s.name = name.empty() ? nextName (list) : std::move (name);
    s.uid = newUid (list);
    list.push_back (s);
    normalize (list);
    return indexOfUid (list, s.uid);
}

// A section over [from, to) (snapped to bars); the place must be free. Its index, or -1.
inline int addRange (Song& song, std::int64_t from, std::int64_t to, std::string name = {})
{
    from = nearestBar (song, from);
    to = nearestBar (song, to);
    auto& list = song.sections();
    if (to <= from || std::any_of (list.begin(), list.end(), [=] (const SongSection& s) { return s.start < to && s.end() > from; }))
        return -1;
    SongSection s { from, to - from, name.empty() ? nextName (list) : std::move (name), 0, newUid (list) };
    list.push_back (s);
    normalize (list);
    return indexOfUid (list, s.uid);
}

// --- keeping the sections in step with the time commands -----------------------------------------------------------

// `length` ticks open at `at`: sections from there on move right; one that holds `at` inside grows.
inline void insertTime (std::vector<SongSection>& list, std::int64_t at, std::int64_t length)
{
    if (length <= 0)
        return;
    for (auto& s : list)
        if (s.start >= at)
            s.start += length;
        else if (s.end() > at)
            s.length += length;
}

// [from, to) leaves the arrangement: what of a section lies inside goes (a section entirely inside goes), later ones
// move left.
inline void deleteTime (std::vector<SongSection>& list, std::int64_t from, std::int64_t to)
{
    if (to <= from)
        return;
    const auto cut = to - from;
    for (auto& s : list)
    {
        const auto start = s.start, end = s.end();
        const auto keepStart = start < from ? start : (start < to ? from : start - cut);
        const auto keepEnd = end <= from ? end : (end <= to ? from : end - cut);
        s.start = keepStart;
        s.length = keepEnd - keepStart;
    }
    list.erase (std::remove_if (list.begin(), list.end(), [] (const SongSection& s) { return s.length <= 0; }), list.end());
}

// --- exports -------------------------------------------------------------------------------------------------------

inline bool isPlainMarker (const Marker& m) noexcept { return ! m.isSignature() && m.action == Marker::none; }

// What an export to a format without sections writes (.flp time markers, .als locators, DAWproject markers): the
// markers of the arrangement with a plain marker added at each section's start unless a plain marker of the same name
// is already there; sorted by tick. `added` (optional) gets how many were added.
inline std::vector<Marker> markersWithSections (const std::vector<Marker>& markers, const std::vector<SongSection>& list, int* added = nullptr)
{
    auto out = markers;
    int count = 0;
    for (const auto& s : list)
        if (std::none_of (markers.begin(), markers.end(), [&s] (const Marker& m) { return isPlainMarker (m) && m.tick == s.start && m.name == s.name; }))
        {
            out.push_back ({ s.start, s.name, 0, 4 });
            ++count;
        }
    std::stable_sort (out.begin(), out.end(), [] (const Marker& a, const Marker& b) { return a.tick < b.tick; });
    if (added != nullptr)
        *added = count;
    return out;
}

// The section commands (move, copy, duplicate, clear, remove, resize, replace, swap), markers <-> sections and the
// scenes from sections are in core/SectionOps.h (they need core/ArrangeOps.h, which includes this header).

// --- the lane's visibility (owner decision O-1 of research190oe) ---------------------------------------------------------
// Simple: the lane shows once the song has a section (the title bar's "Sections" button adds the first one and can hide
// it again); Pattern: a LYNX extra, off by default (Playlist menu › View › "Sections lane", Settings › LYNX extras,
// ui-options.txt "sectionsLane"); Clip-launch: off.
inline bool& patternLaneOn() noexcept
{
    static bool on = false;
    return on;
}
} // namespace sections
} // namespace lynx
