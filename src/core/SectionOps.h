#pragma once

// The Sections lane's commands (round 190ol, core/Sections.h): move, copy, duplicate, clear, remove, resize, replace and
// swap a song section with everything under it; sections <-> markers; scenes from sections. Plain edits on a Song, each
// run inside AudioEngine::editSong (one undo step). Header only, no JUCE.
//
// Content travels through core/ArrangeOps.h's time commands: the clips of every track (cut at the section's edges), the
// markers inside it and the lane automation. Copies of track clips are unique (core/TrackClips.h), as every copy in
// LYNX. A section's time signature goes with it: before a command, each edge it touches gets a pinned signature change
// (the change already there, else one with the meter in force there); afterwards the pins that change nothing are taken
// away (a change at an edge that repeats the signature before it goes too) and the others are ordinary signature changes
// again. So a 7/8 chorus moved into a 4/4 song stays 7/8, and the 4/4 bar after it stays 4/4.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core/ArrangeOps.h"
#include "core/ClipLauncher.h"
#include "core/MarkerPlan.h"
#include "core/Sections.h"
#include "core/SessionOps.h"
#include "core/Song.h"
#include "core/TrackClips.h"

namespace lynx::sections
{
namespace detail
{
// A pinned signature change: its name starts with this byte (then its own name, "" for one the command added)
inline constexpr char pinMark = '\x01';
inline bool isPin (const Marker& m) noexcept { return m.isSignature() && ! m.name.empty() && m.name.front() == pinMark; }

// pins the signature in force at each tick of `ticks`: the signature changes already there are pinned, else a pin with
// the signature in force there is added
inline void pinMeters (Song& song, std::vector<std::int64_t> ticks)
{
    std::sort (ticks.begin(), ticks.end());
    ticks.erase (std::unique (ticks.begin(), ticks.end()), ticks.end());
    std::vector<Marker> pins;
    for (const auto t : ticks)
    {
        if (t < 0)
            continue;
        auto found = false;
        for (auto& m : song.markers())
            if (m.isSignature() && m.tick == t)
            {
                found = true;
                if (! isPin (m))
                    m.name = std::string (1, pinMark) + m.name;
            }
        if (found)
            continue;
        const auto sig = markers::signatureAt (song, t);
        Marker m;
        m.tick = t;
        m.name = std::string (1, pinMark);
        m.numerator = sig.first;
        m.denominator = sig.second;
        pins.push_back (m);
    }
    for (auto& m : pins)
        song.addMarker (std::move (m));
}

// the pins that change nothing go (where an unpinned change shares their tick, it wins); the others keep their own name
// again, or are named as FL names a signature change ("7/8")
inline void settleMeters (Song& song)
{
    auto& list = song.markers();
    std::stable_sort (list.begin(), list.end(), [] (const Marker& a, const Marker& b) { return a.tick < b.tick; });
    std::pair<int, int> current { std::max (1, song.beatsPerBar), meterUnit (song.beatUnit) };
    std::vector<Marker> out;
    out.reserve (list.size());
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        auto m = list[i];
        if (! m.isSignature())
        {
            out.push_back (std::move (m));
            continue;
        }
        if (isPin (m))
        {
            const auto t = m.tick;
            const auto other = std::any_of (list.begin(), list.end(), [t] (const Marker& x) { return x.isSignature() && x.tick == t && ! isPin (x); });
            const auto twin = std::any_of (out.begin(), out.end(), [t] (const Marker& x) { return x.isSignature() && x.tick == t; });
            if (other || twin || std::pair { m.numerator, m.denominator } == current)
                continue;
            m.name = m.name.substr (1);
            if (m.name.empty())
                m.name = std::to_string (m.numerator) + "/" + std::to_string (m.denominator);
        }
        current = { m.numerator, m.denominator };
        out.push_back (std::move (m));
    }
    list = std::move (out);
}

// what a copy of a section takes along: its plain markers, signature changes and key markers (FL's transport actions —
// Start, loops, skips, punch — stay single)
inline void keepCopyMarkers (std::vector<Marker>& list)
{
    list.erase (std::remove_if (list.begin(), list.end(),
                                [] (const Marker& m) { return ! (m.isSignature() || m.action == Marker::none || m.action == Marker::keyScale); }),
                list.end());
}

inline bool valid (const Song& song, int index) noexcept { return index >= 0 && index < (int) song.sections().size(); }

inline std::int64_t boundaryBefore (const Song& song, int before)
{
    const auto& list = song.sections();
    if (list.empty())
        return 0;
    return before >= 0 && before < (int) list.size() ? list[(std::size_t) before].start : list.back().end();
}

inline void insertEntry (Song& song, SongSection s)
{
    auto& list = song.sections();
    list.push_back (std::move (s));
    std::stable_sort (list.begin(), list.end(), [] (const SongSection& a, const SongSection& b) { return a.start < b.start; });
}

// a copy of section `index`'s content at `at` (what is there moves right); the new section's index
inline int copyTo (Song& song, int index, std::int64_t at)
{
    if (! valid (song, index) || at < 0)
        return -1;
    const auto s = song.sections()[(std::size_t) index];
    pinMeters (song, { s.start, s.end(), at });
    auto time = arrange::copyTime (song, s.start, s.end());
    keepCopyMarkers (time.markers);
    trackclips::makeCopiesUnique (song, time.clips);
    arrange::pasteTime (song, at, time);
    auto copy = s;
    copy.start = at;
    copy.uid = newUid (song.sections());
    insertEntry (song, copy);
    settleMeters (song);
    return indexOfUid (song.sections(), copy.uid);
}
} // namespace detail

inline bool move (Song& song, int index, int before)
{
    const auto n = (int) song.sections().size();
    before = std::clamp (before, 0, n);
    if (! detail::valid (song, index) || before == index || before == index + 1)
        return false;
    const auto s = song.sections()[(std::size_t) index];
    auto at = detail::boundaryBefore (song, before);
    detail::pinMeters (song, { s.start, s.end(), at });
    const auto time = arrange::copyTime (song, s.start, s.end());
    arrange::deleteTime (song, s.start, s.end()); // (its section goes with its time: sections::deleteTime)
    if (at >= s.end())
        at -= s.length;
    arrange::pasteTime (song, at, time);
    auto moved = s;
    moved.start = at;
    detail::insertEntry (song, moved);
    detail::settleMeters (song);
    return true;
}

inline int copy (Song& song, int index, int before)
{
    if (! detail::valid (song, index))
        return -1;
    before = std::clamp (before, 0, (int) song.sections().size());
    return detail::copyTo (song, index, detail::boundaryBefore (song, before));
}

inline int duplicate (Song& song, int index)
{
    return detail::valid (song, index) ? detail::copyTo (song, index, song.sections()[(std::size_t) index].end()) : -1;
}

// the Playlist clips that reach into section `index` (indices into song.playlist)
inline std::vector<int> clipsIn (const Song& song, int index)
{
    std::vector<int> out;
    if (! detail::valid (song, index))
        return out;
    const auto& s = song.sections()[(std::size_t) index];
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (const auto& c = song.playlist[(std::size_t) i]; c.length > 0 && c.start < s.end() && c.end() > s.start)
            out.push_back (i);
    return out;
}

inline bool clear (Song& song, int index)
{
    if (clipsIn (song, index).empty())
        return false;
    const auto s = song.sections()[(std::size_t) index];
    std::vector<Clip> out;
    out.reserve (song.playlist.size() + 4);
    for (const auto& c : song.playlist)
    {
        if (c.length <= 0 || c.end() <= s.start || c.start >= s.end())
        {
            out.push_back (c);
            continue;
        }
        if (auto head = arrange::cropped (c, std::numeric_limits<std::int64_t>::min() / 4, s.start))
        {
            head->crossfadeIn = c.crossfadeIn;
            out.push_back (*head);
        }
        if (auto tail = arrange::cropped (c, s.end(), std::numeric_limits<std::int64_t>::max() / 4))
        {
            tail->crossfadeOut = c.crossfadeOut;
            out.push_back (*tail);
        }
    }
    song.playlist = std::move (out);
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return true;
}

inline bool remove (Song& song, int index, bool closeGap)
{
    if (! detail::valid (song, index))
        return false;
    const auto s = song.sections()[(std::size_t) index];
    if (! closeGap)
    {
        song.sections().erase (song.sections().begin() + index);
        return true;
    }
    detail::pinMeters (song, { s.start, s.end() });
    arrange::deleteTime (song, s.start, s.end());
    detail::settleMeters (song);
    return true;
}

inline bool resize (Song& song, int index, std::int64_t end)
{
    if (! detail::valid (song, index))
        return false;
    auto& list = song.sections();
    auto& s = list[(std::size_t) index];
    end = std::max (nearestBar (song, end), barsAfter (song, s.start, 1));
    if (index + 1 < (int) list.size())
        end = std::min (end, list[(std::size_t) index + 1].start);
    if (end - s.start == s.length || end <= s.start)
        return false;
    s.length = end - s.start;
    return true;
}

inline bool replace (Song& song, int source, int target)
{
    if (! detail::valid (song, source) || ! detail::valid (song, target) || source == target)
        return false;
    const auto src = song.sections()[(std::size_t) source];
    const auto dst = song.sections()[(std::size_t) target];
    detail::pinMeters (song, { src.start, src.end(), dst.start, dst.end() });
    auto time = arrange::copyTime (song, src.start, src.end());
    detail::keepCopyMarkers (time.markers);
    trackclips::makeCopiesUnique (song, time.clips);
    arrange::deleteTime (song, dst.start, dst.end()); // (the target's section goes with its time)
    arrange::pasteTime (song, dst.start, time);
    auto replaced = dst;
    replaced.length = src.length;
    detail::insertEntry (song, replaced);
    detail::settleMeters (song);
    return true;
}

inline bool swap (Song& song, int a, int b)
{
    if (! detail::valid (song, a) || ! detail::valid (song, b) || a == b)
        return false;
    if (a > b)
        std::swap (a, b);
    const auto& list = song.sections();
    const auto uidA = list[(std::size_t) a].uid;
    const auto after = b + 1 < (int) list.size() ? list[(std::size_t) b + 1].uid : 0;
    move (song, b, a);
    if (b == a + 1)
        return true;
    const auto at = indexOfUid (song.sections(), uidA);
    return move (song, at, after != 0 ? indexOfUid (song.sections(), after) : (int) song.sections().size());
}

// --- sections and markers --------------------------------------------------------------------------------------------

inline int fromMarkers (Song& song)
{
    std::vector<Marker> plain;
    for (const auto& m : song.markers())
        if (isPlainMarker (m))
            plain.push_back (m);
    std::stable_sort (plain.begin(), plain.end(), [] (const Marker& a, const Marker& b) { return a.tick < b.tick; });
    const auto songEnd = song.songLength();
    int made = 0;
    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        const auto from = song.barAt (std::max<std::int64_t> (0, plain[i].tick)).tick;
        std::int64_t to = 0;
        if (i + 1 < plain.size())
            to = song.barAt (std::max<std::int64_t> (0, plain[i + 1].tick)).tick;
        else
        {
            const auto last = song.barAt (std::max<std::int64_t> (0, songEnd - 1));
            to = std::max (songEnd > from ? last.tick + last.length : 0, barsAfter (song, from, defaultBars));
        }
        if (to <= from)
            continue;
        made += addRange (song, from, to, plain[i].name) >= 0 ? 1 : 0;
    }
    return made;
}

inline int toMarkers (Song& song)
{
    int made = 0;
    const auto list = song.sections();
    for (const auto& s : list)
        if (std::none_of (song.markers().begin(), song.markers().end(), [&s] (const Marker& m) { return isPlainMarker (m) && m.tick == s.start && m.name == s.name; }))
        {
            song.addMarker ({ s.start, s.name, 0, 4 });
            ++made;
        }
    return made;
}

// --- scenes from sections (the Clip-launch layout) -------------------------------------------------------------------

// "Create scenes from sections": one scene per section, named and coloured after it, after the scenes in use (a grid
// with no clip yet starts at its first row). On each Playlist track the clip that covers most of the section (pattern or
// audio clips; the earlier one on a tie), cut to the section, goes into the scene's slot — as "Capture and Insert Scene"
// copies a playing clip; a track without one there gets an empty slot. Returns the first new scene's row, -1 when there
// is no section.
inline int createScenes (Song& song)
{
    const auto list = song.sections();
    if (list.empty())
        return -1;
    const auto fresh = ! song.clipGrid.has_value() || song.clipGrid->clips.empty();
    auto& grid = launcher::ensureGrid (song);
    int first = fresh ? 0 : (int) grid.scenes.size();
    if (! fresh)
        for (const auto& c : grid.clips) // (rows past the scene list can hold clips: start after them too)
            if (c.gridSlot >= 0 && c.gridSlot < (int) grid.slots.size())
                first = std::max (first, grid.slots[(std::size_t) c.gridSlot].scene + 1);
    if (first + (int) list.size() > launcher::maxScenes)
        return -1;
    int tracks = (int) song.playlistTracks.size();
    for (const auto& c : song.playlist)
        tracks = std::max (tracks, c.track + 1);
    for (int k = 0; k < (int) list.size(); ++k)
    {
        const auto& s = list[(std::size_t) k];
        const auto row = first + k;
        auto& g = *song.clipGrid;
        while ((int) g.scenes.size() <= row)
            g.scenes.emplace_back();
        g.scenes[(std::size_t) row] = ClipGridScene {};
        g.scenes[(std::size_t) row].name = s.name;
        g.scenes[(std::size_t) row].colour = colourOf (list, k);
        for (int t = 0; t < tracks && t < launcher::maxTracks; ++t)
        {
            launcher::clearClip (song, t, row);
            const Clip* best = nullptr;
            std::int64_t most = 0;
            for (const auto& c : song.playlist)
            {
                if (c.track != t || (c.kind != ClipKind::pattern && c.kind != ClipKind::audio) || c.muted)
                    continue;
                const auto overlap = std::min (c.end(), s.end()) - std::max (c.start, s.start);
                if (overlap > most)
                {
                    most = overlap;
                    best = &c;
                }
            }
            if (best == nullptr)
                continue;
            if (auto piece = arrange::cropped (*best, s.start, s.end()))
            {
                std::vector<Clip> one { *piece };
                trackclips::makeCopiesUnique (song, one);
                session::putClip (song, one.front(), t, row);
            }
        }
    }
    return first;
}
} // namespace lynx::sections
