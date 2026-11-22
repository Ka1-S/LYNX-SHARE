#pragma once

// Track versions (round 190pe, P20 of research190oe; a LYNX extra: FL Studio and Live have none). Named alternate versions
// of a whole Playlist track, as Cubase's Track Versions and Pro Tools' playlists: a version is the track's clips, its
// automation lanes and its device chain (the effects of the track's own insert). One version is active and is the
// track itself, so playback, the editors, the exports and older files see an ordinary track; the others are kept in
// PlaylistTrack::versions (core/Song.h TrackVersion) and play nothing.
//
// - The clips are those of the arrangement the versions were made in (PlaylistTrack::versionArrangement): an FL project
//   can hold several arrangements, and the versions act on that one only.
// - The device chain is swapped only when the track has an insert of its own (core/TrackStates.h unitsOfTrack: its
//   linked insert, or the one insert its channels play through when no other track's channels play there). A shared
//   insert is the mix of other tracks too, so it stays.
// - Take lanes (core/Comping.h) stay as they are: a version of a comp track is the comp itself; the lanes under it are
//   tracks of their own.
// - The clip grid of the Clip-launch layout is not part of a version (the versions are of the arrangement).
//
// Every function is one edit of the song (inside AudioEngine::editSong / editSongStructural); no JUCE.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::trackversions
{
// Settings › LYNX extras "Track versions in the Playlist track menu" (ui-options.txt "trackVersions"): the Pattern layout's
// track menu offers "Track versions" only while it is on (FL's menu stays FL's), or for a track that already has some;
// the Clip-launch layout always does; the Simple layout has none.
inline bool& patternMenus() noexcept
{
    static bool on = false;
    return on;
}

// Versions of track `track`: 0 when it has none (the track as always), else at least 2.
int count (const Song& song, int track) noexcept;
// The active version's index (0 when the track has none).
int active (const Song& song, int track) noexcept;
// Its name ("" for a track without versions).
std::string nameOf (const Song& song, int track, int version);
// True when the track has versions and they belong to the arrangement shown now: the only time they can be switched.
bool inCurrentArrangement (const Song& song, int track) noexcept;
// True when any track has versions (nothing is drawn or looked up per row otherwise).
bool any (const Song& song) noexcept;

// The insert whose effects are a version's device chain: the track's own insert, -1 when it has none.
int chainInsert (const Song& song, int track);

enum class Start
{
    empty,    // no clips and no lanes; the device chain as it is now (a copy)
    duplicate // a copy of the active version: its clips, its lanes (their curves copied) and its chain
};
// Adds a version named `name` after the others and makes it active; a track without versions first gets version 1,
// named `firstName`, for its present content. A duplicate copies the patterns and automation clips that only this
// track plays (an FL pattern placed on other tracks too stays shared) and every track-clip pattern
// (Pattern::trackClip), so editing the new version leaves the others as they were. Returns the new index, -1 when the
// track can't have versions now (another arrangement is shown, or the track is out of range).
int add (Song& song, int track, Start start, const std::string& name, const std::string& firstName);
// Makes `version` the active one: the track's clips, lanes and (with an own insert) chain go into the version that was
// active, and the chosen one's come out. False when nothing changed.
bool switchTo (Song& song, int track, int version);
bool rename (Song& song, int track, int version, const std::string& name);
// Removes a version. Removing the active one makes its neighbour active first (the next, else the previous). When one
// version is left, the track has no versions any more: its content stays. False when nothing changed.
bool remove (Song& song, int track, int version);
// Pro Tools' "copy selection to main playlist": the part [from, to) of version `version` (not the active one) replaces
// that part of the active version (clips cut at the range's edges; a gap in the version clears the active there).
// Returns how many clips were copied in; -1 when nothing changed.
int promote (Song& song, int track, int version, std::int64_t from, std::int64_t to);
// Every track keeps only its active version (the .flp / .als / DAWproject exports and a copy for an older LYNX): the
// other versions' clips go, and the automations only their lanes used. Returns how many versions were left out.
int keepActiveOnly (Song& song);
// How many lanes of versions that are not active play automation `index` (core/TakeCleanup.h counts them as uses).
int laneUses (const Song& song, int index) noexcept;
// A version name that no other version of the track has: `base`, else "base 2", "base 3"...
std::string freeName (const Song& song, int track, const std::string& base);
} // namespace lynx::trackversions
