#pragma once

// Issue #177: entries dragged from the Playlist's Picker panel (patterns, audio clips, automation clips) and dropped on
// the Playlist, as FL Studio 2026 places them. Header only, no JUCE (tests/PickerDrop190hmTests.inc).
//
// FL evidence (FLEngine_x64.dll, image base 0x400000, static decompile; /mnt/e/lynx-work/pickerdrag190hm/HANDOFF.md):
// - PickerPanelMouseMove FUN_00e4daa0: with the left button held past the drag distance and Ctrl up, the drag starts.
//   The entry under the press joins the picker selection (a press on an entry that is not selected makes it the only
//   one); every selected entry is dragged, in selection order. The drag image is centred on the mouse.
// - Playlist DragDrop FUN_00e7a470, picker branch: ONE undo step ("drop"). The time is the mouse position snapped with
//   the Playlist's snap (FUN_00e5a620 → FUN_00e5a350: Cell rounds down, every other snap to the nearest line); over the
//   track-header column it is the view's left edge. Entry k goes to the track under the mouse + k, at that same time.
//   An entry that is not an audio clip is skipped on an audio track. An audio or automation clip on an instrument track
//   asks once ("Can't drop on an instrument track" / "Instrument tracks are not for audio or automation clips." / "Would
//   you like to place the clip(s) on the next empty playlist track(s)?"): Yes moves it, and every later entry with it,
//   to the next empty track (FUN_00e3a710 flags 1: an unassigned track with no clips, from that track down); No stops
//   the drop there. A dropped pattern becomes the current pattern (FUN_012ddae0).
// - With Shift held at the drop (FUN_012f30b0): every entry on the track under the mouse, one after the other from the
//   snapped time (each starts where the previous one ends); audio / automation clips on an instrument track ask the same
//   question first (No: nothing is placed). The current pattern does not change.
// - The dropped clips become the Playlist's selection (flag 0x80 on each new item).
// Live check (FL 2026, round 190hm, shots /mnt/e/lynx-work/pickerdrag190hm/shots fl-07 / fl-08 / fl-12): Bar snap, a drop
// at bar 3.7 lands on bar 4 and one at 2.3 on bar 2 (nearest line); Pattern 2 + 3 picked and dropped on Track 5 → Tracks
// 5 and 6; Shift on drop → Pattern 1, 2, 3 one after the other on one track; the new clips selected, the others not.
// A Shift press on a picked entry renames the picked patterns instead (FL's "Name for patterns"), so Shift is pressed
// once the drag is under way.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::pickerdrop
{
constexpr int maxTracks = 500; // FL's Playlist tracks

// The entries a drag takes: the picker's selection (selection order) when the pressed entry is in it, else that entry.
inline std::vector<int> draggedEntries (const std::vector<int>& selection, int pressed)
{
    if (pressed < 0)
        return {};
    if (std::find (selection.begin(), selection.end(), pressed) == selection.end())
        return { pressed };
    return selection;
}

inline PlaylistTrackMode modeOf (const Song& song, int track) noexcept
{
    return track >= 0 && track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track].mode : PlaylistTrackMode::unassigned;
}

// FL FUN_00e3a710 (flags 1): the first track from `from` down that is unassigned and holds no clip; -1 when none.
inline int nextEmptyTrack (const Song& song, int from)
{
    for (int t = std::max (0, from); t < maxTracks; ++t)
    {
        if (modeOf (song, t) != PlaylistTrackMode::unassigned)
            continue;
        if (std::none_of (song.playlist.begin(), song.playlist.end(), [t] (const Clip& c) { return c.track == t; }))
            return t;
    }
    return -1;
}

// Whether FL's "Can't drop on an instrument track" question comes up for this drop (asked once, before anything moves).
// `clips`: the dragged entries as plain clips (kind, source, length), in drag order.
inline bool asksAboutInstrumentTrack (const Song& song, const std::vector<Clip>& clips, int track, bool chain)
{
    if (clips.empty() || track < 0)
        return false;
    if (chain)
        return clips.front().kind != ClipKind::pattern && modeOf (song, track) == PlaylistTrackMode::instrument;
    for (int k = 0; k < (int) clips.size() && track + k < maxTracks; ++k)
    {
        const auto mode = modeOf (song, track + k);
        if (mode == PlaylistTrackMode::instrument && clips[(std::size_t) k].kind != ClipKind::pattern)
            return true;
    }
    return false;
}

// The clips the drop places (their track and start set), in drag order. `start`: the snapped time; `chain`: Shift held;
// `moveOff`: the answer to the instrument-track question (true = Yes; ignored when there was no question).
inline std::vector<Clip> place (const Song& song, const std::vector<Clip>& clips, int track, std::int64_t start, bool chain, bool moveOff)
{
    std::vector<Clip> out;
    if (track < 0 || track >= maxTracks)
        return out;
    start = std::max<std::int64_t> (0, start);

    if (chain)
    {
        if (! clips.empty() && clips.front().kind != ClipKind::pattern && modeOf (song, track) == PlaylistTrackMode::instrument)
        {
            if (! moveOff)
                return out;
            track = nextEmptyTrack (song, track);
            if (track < 0)
                return out;
        }
        auto at = start;
        for (auto c : clips)
        {
            c.track = track;
            c.start = at;
            at += std::max<std::int64_t> (1, c.length);
            out.push_back (c);
        }
        return out;
    }

    int shift = 0; // tracks gained by moving off instrument tracks
    for (int k = 0; k < (int) clips.size(); ++k)
    {
        auto t = track + k + shift;
        if (t >= maxTracks)
            continue;
        auto c = clips[(std::size_t) k];
        const auto mode = modeOf (song, t);
        if (mode == PlaylistTrackMode::instrument && c.kind != ClipKind::pattern)
        {
            if (! moveOff)
                break;
            // (the clips this drop already placed count as used tracks)
            auto probe = song;
            probe.playlist.insert (probe.playlist.end(), out.begin(), out.end());
            const auto empty = nextEmptyTrack (probe, t);
            if (empty < 0)
                break;
            shift += empty - t;
            t = empty;
        }
        else if (mode == PlaylistTrackMode::audio && c.kind != ClipKind::audio)
            continue;
        c.track = t;
        c.start = start;
        out.push_back (c);
    }
    return out;
}

// The current pattern after a drop: the last dropped pattern (plain drop), unchanged with Shift or with no pattern.
inline int currentPatternAfter (const std::vector<Clip>& placed, bool chain, int current)
{
    if (chain)
        return current;
    for (auto it = placed.rbegin(); it != placed.rend(); ++it)
        if (it->kind == ClipKind::pattern)
            return it->pattern;
    return current;
}
} // namespace lynx::pickerdrop
