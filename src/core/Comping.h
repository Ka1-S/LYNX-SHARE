#pragma once

// Comping on take lanes (round 177c, docs/analise/REA_rodada177c.md).
//
// The model is the one LYNX's take lanes already have (round 163) and the one Ableton Live uses: a comp group is a
// Playlist track (the comp track: what plays) with the take lanes grouped under it (FL's "Group with above track"; FL
// itself puts loop-recorded takes on grouped tracks, "Group loop recorded takes"). The lanes hold the takes, muted; the
// comp track holds ordinary audio clips that are pieces of them. So the comp costs nothing to play and saves, loads,
// exports and imports like any clip; which take a comp piece came from is not stored but derived (same channel, same
// alignment of the audio to the song).
//
// What this file adds over round 163's "promote":
//   - swipe(): a range of a lane goes to the comp (empty parts of the lane clear the comp there), one undo step;
//   - equal-power crossfades where the comp switches takes: the two pieces overlap by Song::compCrossfadeMs (5 ms by
//     default, 0 = hard cut) centred on the switch, with sin/cos gains (Clip::fadeCurve);
//   - which parts of each lane the comp uses (for the lanes' highlight);
//   - audition: one lane plays instead of the comp (Song::auditionLane, not saved);
//   - flatten: the comp stays, the lanes go.

#include <cstdint>
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

// Flatten: the comp track's clips stay as they are, the lanes (and their clips) are removed, in every arrangement.
bool flatten (Song& song, int compTrack);

// A new take recorded over a comp group (loop passes, or the parts of one take with `oneLane`): the passes become new
// lanes right under comp track `compTrack` (newest first, named "<comp track> (take N)", muted), and the newest one
// goes to the comp over its range, like a swipe (`toComp` false: the comp is left as it is). Makes `compTrack` a comp
// group when it is none yet. Returns the first new lane (−1: nothing done).
int addTakeLanes (Song& song, int compTrack, const std::vector<Clip>& passes, bool oneLane = false, bool toComp = true);

// Ableton's "Insert Take Lane" on a track that has none: its audio clips are copied to a first lane (take 1), so what
// is recorded over it later becomes more lanes and the comp can go back to take 1. Returns the lane (−1: the track has
// no audio clips or is already part of a group).
int makeLanes (Song& song, int track);
} // namespace lynx::comp
