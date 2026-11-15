#pragma once

// Editor maths added by the second UI polish round (UI-D), without JUCE (unit-tested in
// tests/UiPolish2Tests.inc). Behaviour interpreted from the FL Studio manual's Piano Roll and Playlist
// pages (docs/WORKFLOW_FL.md R5, R12, P18), clean-room.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::polish
{
// --- Piano Roll: the selection's stretch handle (R5) -----------------------------------------------

// Scales `originals` in time around `anchor`: a note at anchor + d starts at anchor + d * factor and its
// length is scaled too (at least `minimumLength`). factor = (newEnd - anchor) / (oldEnd - anchor).
inline std::vector<Note> stretchNotes (const std::vector<Note>& originals, std::int64_t anchor, std::int64_t oldEnd, std::int64_t newEnd,
                                       std::int64_t minimumLength = 1)
{
    std::vector<Note> out = originals;
    const auto oldSpan = oldEnd - anchor;

    if (oldSpan <= 0)
        return out;

    const auto factor = (double) std::max<std::int64_t> (minimumLength, newEnd - anchor) / (double) oldSpan;

    for (auto& n : out)
    {
        const auto start = anchor + (std::int64_t) std::llround ((double) (n.start - anchor) * factor);
        const auto end = anchor + (std::int64_t) std::llround ((double) (n.start + n.length - anchor) * factor);
        n.start = std::max<std::int64_t> (0, start);
        n.length = std::max (minimumLength, end - n.start);
    }

    return out;
}

// --- Piano Roll: drum paint (R1, "paint drums") ------------------------------------------------------

// True when the channel already has a note on `key` starting inside [cellStart, cellStart + cell): drum
// paint lays at most one note per grid cell and key.
inline bool hasNoteInCell (const Pattern& pattern, int channel, int key, std::int64_t cellStart, std::int64_t cell)
{
    return std::any_of (pattern.notes.begin(), pattern.notes.end(), [&] (const Note& n) {
        return n.channel == channel && n.key == key && n.start >= cellStart && n.start < cellStart + std::max<std::int64_t> (1, cell);
    });
}

// Round 177d (pain point 25): the song tick of the pattern's tick 0 as it plays in the Playlist (its first unmuted
// clip, as for the clip ghosts; negative when the clip's offset reaches past the song's start), or nothing when no clip
// of it is on the Playlist.
inline std::optional<std::int64_t> patternSongOrigin (const Song& song, int pattern)
{
    const Clip* home = nullptr;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern == pattern && ! c.muted && (home == nullptr || c.start < home->start))
            home = &c;
    if (home == nullptr)
        return std::nullopt;
    return home->start - home->offset;
}

// The number the ruler prints for bar `bar` of the pattern, for a song origin: as the Playlist counts, 0 or less when
// that bar of the pattern falls before the song's first bar (no number then).
inline std::int64_t songBarNumber (std::int64_t origin, std::int64_t bar, std::int64_t barTicks)
{
    return (std::int64_t) std::floor ((double) (origin + bar * barTicks) / (double) barTicks) + 1;
}

// --- Piano Roll: hollow ghost notes from other clips (R12) -------------------------------------------

// The notes of `channel` that play at the same time as the first Playlist clip of `pattern`, coming from
// other pattern clips, mapped into `pattern`'s own time. FL draws these hollow. Muted clips are skipped; a clip longer
// than its pattern loops it with stretchedClipsLoop() (LYNX extra), else plays it once (FL, round 168k). Empty when
// the pattern is not in the Playlist.
inline std::vector<Note> clipGhostNotes (const Song& song, int pattern, int channel, std::size_t limit = 2000)
{
    std::vector<Note> out;
    const Clip* home = nullptr;

    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern == pattern && ! c.muted && (home == nullptr || c.start < home->start))
            home = &c;

    if (home == nullptr)
        return out;

    // pattern tick 0 of the home clip, in song ticks
    const auto homeOrigin = home->start - home->offset;

    for (const auto& c : song.playlist)
    {
        if (c.kind != ClipKind::pattern || c.pattern == pattern || c.muted || c.pattern < 0 || c.pattern >= (int) song.patterns.size())
            continue;

        const auto from = std::max (c.start, home->start), to = std::min (c.end(), home->end());

        if (from >= to)
            continue;

        const auto& other = song.patterns[(std::size_t) c.pattern];
        const auto origin = c.start - c.offset;
        const auto length = std::max<std::int64_t> (1, other.effectiveLength (song.meter()));
        // a stretched clip loops its pattern when stretchedClipsLoop() (LYNX extra, issue #25); off = FL: once from
        // the clip's offset (round 168k)
        const auto loops = stretchedClipsLoop() && c.offset + c.length > length;
        const auto firstPass = loops ? std::max<std::int64_t> (0, (from - origin) / length - 1) : 0;

        for (auto pass = firstPass; pass == firstPass || (loops && origin + pass * length < to); ++pass)
            for (const auto& n : other.notes)
            {
                if (n.channel != channel || (loops && n.start >= length))
                    continue;

                const auto at = origin + pass * length + n.start;

                if (at < from || at >= to)
                    continue;

                auto ghost = n;
                ghost.start = at - homeOrigin;
                ghost.length = std::min (n.length, to - at);
                out.push_back (ghost);

                if (out.size() >= limit)
                    return out;
            }
    }

    return out;
}

// --- Playlist: track heights (P18) -------------------------------------------------------------------

// FL's row limits (FUN_00ea0b30, round 190ft): the smallest row is 16 px, the tallest 512 px more; a row is
// minTrackHeight + size × (default − minTrackHeight), `size` being the factor FL keeps per track and in the .flp.
constexpr int minTrackHeight = 16, maxTrackHeight = minTrackHeight + 512, defaultTrackHeight = 38;

inline int clampTrackHeight (int h)
{
    return std::clamp (h, minTrackHeight, maxTrackHeight);
}

// FL keeps rows at an even height (the layout clears bit 0).
inline int evenTrackHeight (int h)
{
    return clampTrackHeight (h) & ~1;
}

// FL's size factor (Size menu 0.333 … 2, a resize drag any value ≥ 0, the .flp's f32) ↔ LYNX's px at 100 % zoom.
inline int heightForFlSize (double size, int base = defaultTrackHeight)
{
    if (! (size >= 0.0))
        size = 1.0;
    return evenTrackHeight (minTrackHeight + (int) std::lround ((base - minTrackHeight) * size));
}

inline float flSizeForHeight (int height, int base = defaultTrackHeight)
{
    if (height <= 0)
        return 1.0f;
    return (float) std::max (0, clampTrackHeight (height) - minTrackHeight) / (float) (base - minTrackHeight);
}

// Heights of tracks 0..n-1 laid out top to bottom: `heights[i]` (0 or missing = `fallback`).
struct TrackLayout
{
    std::vector<int> heights; // per track; 0 = the default
    int fallback = defaultTrackHeight;

    int heightOf (int track) const
    {
        const auto h = track >= 0 && track < (int) heights.size() ? heights[(std::size_t) track] : 0;
        return h > 0 ? clampTrackHeight (h) : fallback;
    }

    // top of `track` (track 0 at 0)
    int top (int track) const
    {
        int y = 0;
        for (int t = 0; t < track; ++t)
            y += heightOf (t);
        return y;
    }

    // the track under `y` (clamped to 0..count-1)
    int trackAt (int y, int count) const
    {
        if (count <= 0)
            return 0;
        int top = 0;
        for (int t = 0; t < count; ++t)
        {
            top += heightOf (t);
            if (y < top)
                return t;
        }
        return count - 1;
    }

    int total (int count) const { return top (count); }

    void set (int track, int h)
    {
        if (track < 0)
            return;
        if ((int) heights.size() <= track)
            heights.resize ((std::size_t) track + 1, 0);
        heights[(std::size_t) track] = clampTrackHeight (h);
    }

    // all tracks: the fallback changes and per-track sizes are forgotten
    void setAll (int h)
    {
        fallback = clampTrackHeight (h);
        heights.clear();
    }
};

// --- Playlist: make unique (P8; also Shift+V while cloning a clip) -----------------------------------

// The pattern / automation clips `clips` switch to their own copy of their pattern / automation (clips
// sharing one source share one copy). The copy is named after the source with " #2" (" #3"... when taken).
inline void makeClipsUnique (Song& song, const std::vector<int>& clips)
{
    auto freeName = [] (const std::string& base, auto&& taken) {
        for (int n = 2;; ++n)
        {
            auto name = base + " #" + std::to_string (n);
            if (! taken (name))
                return name;
        }
    };
    std::vector<std::pair<int, int>> patterns, automations; // source -> copy

    auto copyOf = [] (std::vector<std::pair<int, int>>& map, int source) {
        for (auto& [from, to] : map)
            if (from == source)
                return to;
        return -1;
    };

    for (auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;

        auto& c = song.playlist[(std::size_t) i];

        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
        {
            auto to = copyOf (patterns, c.pattern);

            if (to < 0)
            {
                auto copy = song.patterns[(std::size_t) c.pattern];
                copy.name = freeName (copy.name, [&song] (const std::string& n) {
                    return std::any_of (song.patterns.begin(), song.patterns.end(), [&n] (const Pattern& p) { return p.name == n; });
                });
                song.patterns.push_back (std::move (copy));
                to = (int) song.patterns.size() - 1;
                patterns.push_back ({ c.pattern, to });
            }

            song.playlist[(std::size_t) i].pattern = to;
        }
        else if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < (int) song.automations.size())
        {
            auto to = copyOf (automations, c.automation);

            if (to < 0)
            {
                auto copy = song.automations[(std::size_t) c.automation];
                copy.name = freeName (copy.name, [&song] (const std::string& n) {
                    return std::any_of (song.automations.begin(), song.automations.end(), [&n] (const Automation& a) { return a.name == n; });
                });
                song.automations.push_back (std::move (copy));
                to = (int) song.automations.size() - 1;
                automations.push_back ({ c.automation, to });
            }

            song.playlist[(std::size_t) i].automation = to;
        }
    }

    if (! patterns.empty())
        song.currentPattern = patterns.front().second;
}

// --- transport: "remember seek time" (G6) ----------------------------------------------------------

// Where Play starts after Stop. Without the option Stop goes back to the start (tick 0); with it, to
// the position last chosen by a click on the timeline (`seekTick`, -1 = none chosen).
inline std::int64_t stopReturnTick (bool rememberSeek, std::int64_t seekTick)
{
    return rememberSeek && seekTick > 0 ? seekTick : 0;
}
} // namespace lynx::polish
