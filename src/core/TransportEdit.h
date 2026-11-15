#pragma once

// Transport and editing helpers of round 162 (issue #9): partial quantize ("Quantize 50%"), the Piano Roll's
// playhead in Song mode, the editors' "Follow" (auto scrolling) and the meter scale of the Mixer. Pure logic,
// no JUCE (unit-tested in tests/Transport162Tests.inc). Behaviour interpreted from FL Studio (Playlist / Piano Roll
// "Auto scrolling", quick quantize), clean-room.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::transportedit
{
// --- quantize -------------------------------------------------------------------------------------------------

// `tick` moved `strength` (0..1) of the way to the nearest multiple of `grid`: 1 = onto the grid line, 0.5 = halfway
// ("Quantize 50%": applied again it keeps halving the distance). Never below 0.
inline std::int64_t quantizeTick (std::int64_t tick, std::int64_t grid, double strength) noexcept
{
    grid = std::max<std::int64_t> (1, grid);
    strength = std::clamp (strength, 0.0, 1.0);
    const auto target = (tick + grid / 2) / grid * grid;
    const auto moved = tick + (std::int64_t) std::llround ((double) (target - tick) * strength);
    return std::max<std::int64_t> (0, moved);
}

// Moves the starts of the notes `indices` toward the grid (lengths kept).
inline void quantizeNoteStarts (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid, double strength)
{
    for (auto i : indices)
        if (i >= 0 && i < (int) pattern.notes.size())
        {
            auto& n = pattern.notes[(std::size_t) i];
            n.start = quantizeTick (n.start, grid, strength);
        }
}

// Moves the starts of the clips `indices` toward the grid (lengths and contents kept): the Playlist's quick
// quantize (FL: Ctrl+Q in the Playlist quantizes the clips' start times).
inline void quantizeClipStarts (std::vector<Clip>& clips, const std::vector<int>& indices, std::int64_t grid, double strength)
{
    for (auto i : indices)
        if (i >= 0 && i < (int) clips.size())
        {
            auto& c = clips[(std::size_t) i];
            c.start = quantizeTick (c.start, grid, strength);
        }
}

// --- playhead -------------------------------------------------------------------------------------------------

// Where the Piano Roll's playhead is inside `pattern` (in ticks of the pattern), or -1 when that pattern is not
// playing: in Pattern mode the current pattern loops; in Song mode the playhead shows while the song position is
// inside a clip of that pattern (the clip's offset counts; the clip shown first in the Playlist wins).
// `linearOrigin` >= 0: a Loop-record-off take in Pattern mode plays the pattern once from there (round 170e5), so the
// playhead runs on past the pattern's end instead of going back
inline double patternPlayhead (const Song& song, int pattern, double transportTick, double linearOrigin = -1.0)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size() || transportTick < 0.0)
        return -1.0;

    if (song.mode == PlayMode::pattern)
    {
        if (pattern != song.currentPattern)
            return -1.0;
        if (linearOrigin >= 0.0 && transportTick >= linearOrigin)
            return transportTick - linearOrigin;
        const auto length = (double) std::max<std::int64_t> (1, song.patterns[(std::size_t) pattern].effectiveLength (song.meter()));
        return std::fmod (transportTick, length);
    }

    const auto inSong = std::fmod (transportTick, (double) std::max<std::int64_t> (1, song.songLength())); // the song loops
    const Clip* best = nullptr;

    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern == pattern && inSong >= (double) c.start && inSong < (double) c.end())
            if (best == nullptr || c.track < best->track)
                best = &c;

    // a stretched clip loops its pattern (LYNX extra stretchedClipsLoop, issue #25): the playhead wraps with it
    return best != nullptr ? patternTickInClip (*best, inSong, song.patterns[(std::size_t) pattern].effectiveLength (song.meter())) : -1.0;
}

// Where the transport goes when the Piano Roll's ruler is clicked at `patternTick` (round 163, FL: clicking the
// Piano Roll's timeline moves the position): in Pattern mode that pattern position; in Song mode the same place in
// the clip of `pattern` that plays at `transportTick`, else in its earliest clip (upper track first). -1 in Song
// mode when the pattern has no clip in the Playlist.
inline double patternSeekTarget (const Song& song, int pattern, double patternTick, double transportTick)
{
    patternTick = std::max (0.0, patternTick);
    if (song.mode == PlayMode::pattern)
        return patternTick;

    const auto inSong = transportTick >= 0.0 ? std::fmod (transportTick, (double) std::max<std::int64_t> (1, song.songLength())) : -1.0;
    const Clip *playing = nullptr, *earliest = nullptr;
    for (const auto& c : song.playlist)
    {
        if (c.kind != ClipKind::pattern || c.pattern != pattern)
            continue;
        if (inSong >= (double) c.start && inSong < (double) c.end() && (playing == nullptr || c.track < playing->track))
            playing = &c;
        if (earliest == nullptr || c.start < earliest->start || (c.start == earliest->start && c.track < earliest->track))
            earliest = &c;
    }
    const auto* clip = playing != nullptr ? playing : earliest;
    if (clip == nullptr)
        return -1.0;
    const auto inClip = std::clamp (patternTick - (double) clip->offset, 0.0, (double) std::max<std::int64_t> (0, clip->length - 1));
    return (double) clip->start + inClip;
}

// --- follow (auto scrolling) ----------------------------------------------------------------------------------

// The editors' "Follow": when the playhead at `tick` leaves the view [viewStart, viewStart + viewTicks), the view
// jumps a page so the playhead is `leadTicks` from its left edge (FL's page-wise auto scrolling). Returns the new
// view start, or `viewStart` when the playhead is in view (or there is none).
inline double followViewStart (double viewStart, double viewTicks, double tick, double leadTicks, double rightMarginTicks = 0.0)
{
    if (tick < 0.0 || viewTicks <= 0.0)
        return viewStart;

    if (tick < viewStart || tick > viewStart + viewTicks - rightMarginTicks)
        return std::max (0.0, tick - leadTicks);

    return viewStart;
}

// Follow inside a loop of `loopTicks` (the Piano Roll in Pattern mode: the pattern's length; feedback 190bm: "it always
// jumps when it reaches the end"). The playhead never passes the loop's end, so:
//  - a view that already shows the loop's end doesn't page forward when the playhead nears its right edge (the right
//    margin is for a playhead that goes on; here the next page would show nothing and the wrap would page straight
//    back: two full repaints, a blink). A loop that fits the view never moves it.
//  - a page forward stops with the loop's end at the view's right edge instead of running past it;
//  - the wrap is one jump back (the playhead is left of the view). `loopTicks` <= 0: followViewStart as it is.
inline double followViewStartInLoop (double viewStart, double viewTicks, double tick, double leadTicks, double rightMarginTicks, double loopTicks)
{
    if (loopTicks <= 0.0 || tick < 0.0 || viewTicks <= 0.0)
        return followViewStart (viewStart, viewTicks, tick, leadTicks, rightMarginTicks);

    const auto endInView = loopTicks <= viewStart + viewTicks;
    if (tick >= viewStart && tick <= viewStart + viewTicks - (endInView ? 0.0 : rightMarginTicks))
        return viewStart;
    if (tick < viewStart && tick < viewTicks - rightMarginTicks) // (the wrap, or a seek near the start: the first page)
        return 0.0;
    return std::max (0.0, std::min (tick - leadTicks, loopTicks - viewTicks));
}

// --- Stop ------------------------------------------------------------------------------------------------------

// Where Stop sends the playhead. `alreadyStopped`: Stop pressed while nothing plays (or its second click of a
// double click) always goes back to the very start (the song or the pattern start), even with "Remember seek
// time" on; otherwise the remembered seek position (song mode with the option on), else the start.
inline std::int64_t stopTarget (bool alreadyStopped, bool rememberSeek, std::int64_t seekMark) noexcept
{
    if (alreadyStopped)
        return 0;
    return rememberSeek && seekMark > 0 ? seekMark : 0;
}

// Where Stop or Space (while playing) sends the playhead, as FL 2026 does (issue #169, REA round 190gy, FLEngine):
// FL keeps a seek mark (FUN_011c9300, global 0x15e0458), set by every position the user sets while "Remember seek
// time" is on: a ruler click in the Playlist or the Piano roll, also one made during playback, and the position
// playback was started from. Its Stop (the toolbar's StopBtnClick 0x1030470 = FUN_011ca520 (0, 0), also Space and the
// Piano roll play button's right-click) goes back to that mark; Stop while stopped goes to the very start and
// forgets it (FUN_011c9de0); switching Pattern / Song forgets it (FUN_011ca630). Pause (the Play button pressed while
// playing = FUN_011ca520 (2, …), LYNX's Ctrl+Space and the Piano roll's play button) keeps the position and leaves
// the mark alone: a later Stop still goes back to the mark. "Remember seek time" is on in a fresh FL (its variable
// 0x15d89a0 starts as 1). With it off LYNX keeps its earlier rule: Space goes back to where playback started, the
// Stop button to the start.
// `mark` = the remembered position for the current Pattern / Song mode (-1 = none), `playStart` = where this playback
// was started from (not a resume after a pause).
inline std::int64_t stopReturnTarget (bool alreadyStopped, bool rememberSeek, bool spaceKey, std::int64_t mark,
                                      std::int64_t playStart) noexcept
{
    if (alreadyStopped)
        return 0;
    if (rememberSeek)
        return std::max<std::int64_t> (0, mark >= 0 ? mark : playStart);
    return spaceKey ? std::max<std::int64_t> (0, playStart) : 0;
}

// FL 2026's Shift+Space (measured in FL, round 165 issue11c, issue #11): while playing or recording it stops and puts
// the position at the start of the time selection (Song mode with a time selection) or else of the song / pattern —
// not where playback started and not where Shift+Space was pressed. While stopped it plays from the current position,
// like Space (FL ignores neither the ruler mark nor the time selection there).
inline std::int64_t shiftSpaceStopTarget (bool songMode, bool hasTimeSelection, std::int64_t selectionStart) noexcept
{
    return songMode && hasTimeSelection ? std::max<std::int64_t> (0, selectionStart) : 0;
}

// Right-click on the Piano roll's play button (FL 2026, tried in FL on macOS for WORKFLOW_FL R29): it stops and goes
// back to where playback last started (a ruler click, not always bar 1); stopped there already, to the pattern start.
inline double rollPlayRightClickTarget (bool playing, double position, double lastStart) noexcept
{
    return playing || position != lastStart ? std::max (0.0, lastStart) : 0.0;
}
} // namespace lynx::transportedit
