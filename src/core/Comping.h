#pragma once

// Comping on take lanes (round 177c, docs/analise/REA_rodada177c.md).
//
// Recording never makes a comp: loop passes land as FL leaves them, an ordinary track group (TakeRecording.h). "Comp
// these takes" (compTakes) then inserts a comp track above a group: what plays, marked PlaylistTrack::compTrack. The group's
// tracks become the take lanes, muted; the comp track holds ordinary audio clips that are pieces of them. So the comp costs
// nothing to play and saves, loads, exports and imports like any clip; which take a comp piece came from is not stored but
// derived (same channel, same alignment of the audio to the song).
//
// What this file adds over round 163's "promote":
//   - swipe(): a range of a lane goes to the comp (empty parts of the lane clear the comp there), one undo step;
//   - equal-power crossfades where the comp switches takes: the two pieces overlap by Song::compCrossfadeMs (5 ms by
//     default, 0 = hard cut) centred on the switch, with sin/cos gains (Clip::fadeCurve);
//   - which parts of each lane the comp uses (for the lanes' highlight);
//   - audition: one lane plays instead of the comp (Song::auditionLane, not saved);
//   - flatten: the comp stays, the lanes go.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::comp
{
// A comp group: the comp track and its lanes [first, last] (the grouped tracks right under it).
struct Group
{
    int comp = -1, first = -1, last = -1;
    bool valid() const noexcept { return comp >= 0 && first == comp + 1 && last >= first; }
    bool isLane (int track) const noexcept { return valid() && track >= first && track <= last; }
    int lanes() const noexcept { return valid() ? last - first + 1 : 0; }
};

// The group `track` belongs to (the comp track or one of its lanes); invalid when the track has no lanes.
Group groupOf (const Song& song, int track);

// Song::compCrossfadeMs in ticks at the song's tempo (rounded to an even number: the crossfade is centred).
std::int64_t crossfadeTicks (const Song& song);

// The lane clip comp clip `compClip` plays a piece of: an audio clip on a lane of `g` with the same channel and the same
// audio-to-song alignment (offset − start, sub-tick fraction) that holds the piece's switch range. −1: none.
int sourceOf (const Song& song, const Group& g, int compClip);

// What the comp takes from each lane: [from, to) = the part between the switch points (the crossfades' centres).
struct Used
{
    int lane = -1;       // Playlist track
    int laneClip = -1;   // index in song.playlist
    int compClip = -1;   // index in song.playlist
    std::int64_t from = 0, to = 0;
};
std::vector<Used> usedRanges (const Song& song, const Group& g);

// Swipe: [from, to) of take lane `lane` goes to its comp track, replacing what played there. Parts of the range where the
// lane has no take clear the comp there. Crossfades follow (applyCrossfades). Returns false when nothing changed.
bool swipe (Song& song, int lane, std::int64_t from, std::int64_t to);

// Swipe of one lane clip only (the clip's range ∩ [from, to)); round 163's "Promote this take to the comp track".
bool swipeClip (Song& song, int clipIndex, std::int64_t from, std::int64_t to);

// The comp track's switch points get crossfades of `ticks` (equal power, centred, shortened where a take has no audio
// beyond the switch or a piece is shorter; 0 = hard cuts). Existing comp crossfades are taken back to their centres
// first, so this can run again with another length. Touching audio clips on the comp track count as switch points.
void applyCrossfades (Song& song, int compTrack, std::int64_t ticks);

// The comp track's clips without crossfade overlaps: each switch back at the crossfade's centre (applyCrossfades undoes
// this).
void removeCrossfades (Song& song, int compTrack);

// Audition (Ableton's "Audition Take Lane"): `lane` plays instead of its comp track; −1 (or a track that is no lane):
// off. Only one lane at a time. Changes nothing saved.
void setAudition (Song& song, int lane);

// The audition follows a row, not a take: an edit that moves rows ends it. What an edit has to compare, taken before it
// (empty without an audition, so a song that auditions nothing costs a compare).
struct AuditionedTrack
{
    int lane = -1;
    std::string name;
};
AuditionedTrack auditionedTrack (const Song& song);
void checkAudition (Song& song, const AuditionedTrack& before);

// How a take lane is comped (user decision 2026-11-07: both). A drag on a lane clip's body swipes (Logic's Quick Swipe,
// and Live's Draw mode); a time selection on a lane goes to the comp by "Send the time selection to the comp" (Live's
// select + Enter; no default key, Enter stays FL's "Maximize or restore the Playlist").
enum class SwipeStyle { quickSwipe, liveSelectEnter, both };
inline constexpr SwipeStyle swipeStyle = SwipeStyle::both;
// whether a drag swipes with this tool (`drawTool`: the brush, Live's Draw mode; else the pencil)
constexpr bool dragSwipes (SwipeStyle style, bool drawTool) noexcept { return style != SwipeStyle::liveSelectEnter || drawTool; }
constexpr bool selectionSwipes (SwipeStyle style) noexcept { return style != SwipeStyle::quickSwipe; }

// The song's time selection (loopStart..loopEnd) of take lane `lane` goes to its comp (a swipe). False when there is no
// time selection or `lane` is not a lane of a comp.
bool swipeSelection (Song& song, int lane);

// Flatten: the comp track's clips stay as they are, the lanes (and their clips) are removed, in every arrangement.
bool flatten (Song& song, int compTrack);

// Whether `track` heads a comp ("Comp these takes" was done on it, and it still has lanes).
bool isComp (const Song& song, int track);

// Whether "Comp these takes" can run on the group of `track`: a group with audio that has no comp yet.
bool canComp (const Song& song, int track);

// "Comp these takes": one new comp track right above the group of `track` (the only time comping moves tracks), the
// group's tracks become its lanes, their audio muted, unfolded, named "<name> (take N)" in recording order. The comp plays
// the last pass (the latest recorded: highest channel, then latest position in its file). Returns the comp track (−1: see
// canComp).
int compTakes (Song& song, int track);
} // namespace lynx::comp
