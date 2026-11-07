#pragma once

// FL Studio's Playlist track tools of round 168f (docs/analise/REA_rodada168f.md): Lock to content / Lock to this size,
// Auto name, Send pattern instances here, the Tools macros (Lock / Unlock all tracks to content, Reset empty tracks,
// Collapse / Expand all grouped tracks, Purge unused audio clips), Merge pattern clips (Ctrl+G), Clone selected tracks
// (Ctrl+Shift+B) and "Create variants when stretching audio clips". Plain edits on a Song, each run inside
// AudioEngine::editSong (one undo step). No JUCE.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::pltracks
{
// --- Lock to content (FL: EEPLTrackClipLockMenuClick → FUN_012e7a90, the paint loop of FUN_00e5cd20 → FUN_01220fe0) ---

// The clip the track holds most: the clips of `track` are counted by their template (kind, source, length, offset) and
// the first clip of the most frequent one is returned; -1 when the track is empty.
int dominantClip (const Song& song, int track);
// FL's auto name (FUN_012ef490 ← FUN_012233c0): the track takes the name and colour of the source most of its clips
// play (pattern, audio clip channel or automation); an empty track gets an empty name and the default colour.
void autoName (Song& song, int track);
// "Lock to content" on / off for `tracks` (the menu's track first: the others follow its new state, FL's mask 4 of
// FUN_012eafa0). Turning it on names a track that has no name yet (auto name).
void toggleLockToContent (Song& song, const std::vector<int>& tracks);
// A clip about to be painted on a locked track: when the track already holds clips and none of them plays the clip's
// source, the clip becomes a copy of the track's dominant clip (FL copies the whole item template, then puts it at the
// click). Returns true when it changed the clip.
bool applyLockToContent (const Song& song, Clip& clip);
// Tools › "Lock all playlist tracks to content" / "Unlock all playlist tracks from content" (FUN_011c2190) on tracks
// 0 … count − 1. `resize` = the "Also resize all playlist tracks to their minimum / default size?" answer Yes: locking
// sets every row to the minimum height (locked), unlocking back to 100 % and unlocked.
void lockAll (Song& song, int count, bool lock, bool resize);

// --- Lock to this size (EEPLTrackLockMenuClick 0x1228cb0, EEPLTrackSize1MenuClick 0x1228f10) ---------------------------

bool isSizeLocked (const Song& song, int track);
// Locks the track at `shownHeight` px (its row's height as shown now), or unlocks it (keeps −height, like FL).
void toggleSizeLock (Song& song, const std::vector<int>& tracks, int shownHeight);
// The Size menu sets a size and unlocks the track (FL negates +0x14).
void unlockSize (PlaylistTrack& track);

// --- Send pattern instances here (EEPLTrackSendPatMenuClick 0x1228ea0 → FUN_012db4e0) ---------------------------------

// The selected pattern clips (`selection`, any pattern) move to `track`; without a selection every clip of the current
// pattern does. Positions stay. A track without a name is auto-named afterwards. Returns how many clips moved.
int sendPatternInstancesHere (Song& song, int track, const std::vector<int>& selection);

// --- Tools macros ------------------------------------------------------------------------------------------------------

// "Reset empty playlist tracks" (FUN_012e33f0): a track without clips whose mode is unassigned and that has a name, a
// colour (or a lock) goes back to the defaults. Returns how many were reset.
int resetEmptyTracks (Song& song);
// "Collapse all grouped tracks" / "Expand all grouped tracks" (0x1214390 / 0x1214450): every group header.
void setAllGroupsCollapsed (Song& song, bool collapsed);
// The audio clip channels nothing uses (FL: "Purge unused audio clips", FUN_011f9a70 ← FUN_011f8880): no clip in any
// arrangement and no note in any pattern plays them.
std::vector<int> unusedAudioClipChannels (const Song& song);
int purgeUnusedAudioClips (Song& song);

// --- Merge pattern clips (Ctrl+G; EEMergeAction → FUN_01302d30 → FUN_012dc060) -----------------------------------------

// Which clips Ctrl+G merges: the selected ones, or every clip when nothing is selected (FL's selection mask).
std::vector<int> mergeCandidates (const Song& song, const std::vector<int>& selection);
// Every clip of `clips` must be a pattern clip and at least two of them must be unmuted (FL's mask 0xa0: muted clips stay
// out of the merge); the content each clip shows goes into a new pattern (inserted after the lowest pattern involved,
// named and coloured after the first clip's pattern — the name made unique), and one clip of it replaces them at the
// first clip's start, on the topmost of their tracks (FL: the largest stored row, stored as 500 − track). Returns the
// new clip's index, or -1 (FL: "Could not find enough valid pattern clips or instances to merge." / "The selection
// contains different clip types, can't merge.").
enum class MergeResult { merged, notEnough, mixedKinds };
MergeResult mergePatternClips (Song& song, const std::vector<int>& clips, int* newClip = nullptr);

// --- Merge similar pattern clips (Ctrl+Shift+G; EEMergeSimAction → FUN_012dc060 (1, 1)) --------------------------------

// Merges `clips` as mergePatternClips does, but other kinds in the list are skipped rather than refused (this action
// doesn't go through FUN_01302d30's check). Then every other group of clips in the arrangement with the same content at
// the same relative times (FUN_012dbb20 / FUN_012dbac0: same pattern, same window, not muted; the tracks don't count)
// becomes one more clip of the new pattern, at that group's first time and topmost track. `similarGroups` = how many.
MergeResult mergeSimilarPatternClips (Song& song, const std::vector<int>& clips, int* similarGroups = nullptr);

// --- Merge automation clips (Ctrl+Alt+G; EEMergeAutoClipsAction → FUN_013009f0, lossy FUN_012ff0d0) --------------------

// In FL's order of checks. tooFew: "Please select at least two automation clips."; notAutomation: "The selection may
// only contain automation clips."; missingEnds: a clip's window lacks control points at its start and end (FUN_013007a0);
// overlapping: clips overlap in time. The last two can only merge lossily, after FL's question. LYNX has no counterpart
// of FL's other refusals: its clips have no per-clip min / max and no LFO mode.
enum class AutomationMergeCheck { ok, tooFew, notAutomation, missingEnds, overlapping };
AutomationMergeCheck checkAutomationMerge (const Song& song, const std::vector<int>& clips);
// FL's "Target links conflict": the clips don't all drive the same parameter (FUN_012e1a80).
bool automationLinksConflict (const Song& song, const std::vector<int>& clips);
// The answers to that question. FL's clip can hold several links, a LYNX automation one target: mergeAll gives every
// other target a clip of its own with the same curve, on the same track.
enum class AutomationLinks { keepFirst, mergeAll, deleteAll };
// A new automation "<first clip's name> - merged" (made unique) holding what each clip shows, one clip of it at the
// first clip's place; the old clips go, their automations stay (as FL's channels do). A gap between two clips holds the
// last value. Lossy: each clip's curve is sampled instead, later clips cutting off overlapping ones (FUN_012ff0d0).
// Returns the new clip's index, or -1 when the clips can't be merged at all.
int mergeAutomationClips (Song& song, const std::vector<int>& clips, AutomationLinks links, bool lossy);

// --- Clone selected playlist track(s) (Ctrl+Shift+B; EEClonePLTracksAction → FUN_012edbe0 / FUN_012ee550) --------------

struct CloneOptions
{
    // FL's "Clone — What do you want to clone along with the track?" (defaults: the first four ticked)
    bool patterns = true, audioClips = true, automations = true, effects = true, routing = false, groupedTracks = false;
};
// The tracks to clone: `selected` grown by the members of the groups whose header is selected when groupedTracks.
std::vector<int> tracksToClone (const Song& song, std::vector<int> selected, bool groupedTracks);
// Clones `tracks` (sorted) right under the last one (after its group); each clone keeps the colour, the locks, the
// mute and the grouping, gets the name made unique ("Name 2", an unnamed track stays unnamed) and the clips of the
// ticked kinds. Audio tracks get a new mixer track (its effects / sends copied when ticked), instrument tracks a clone
// of their channel. Returns the first clone's index, or -1 when every track is empty (FL: "Nothing to clone").
int cloneTracks (Song& song, const std::vector<int>& tracks, const CloneOptions& options);
// FL's unique track name (FUN_012f2850): `base`, else "base 2", "base 3"... the first no other track has.
std::string uniqueTrackName (const Song& song, const std::string& base, int except);

// --- Stretch audio when resizing / Create variants when stretching (FUN_00e66eb0, global 0x15452e5 = 1 by default) -----

// How many clips (all arrangements) play audio channel `channel`.
int clipsOfChannel (const Song& song, int channel);
// The clip already has its own settings (FL's FUN_01065000: clip pitch, stretch, reverse).
bool isVariant (const Clip& clip) noexcept;
// Stretching audio clip `index` from `original` (its state when the drag started) to `newLength` (and `newStart`):
// with Create variants on and another clip on the same channel (or when the clip is already a variant) only this clip
// changes (its own stretch); otherwise the channel's timing changes, so every clip of the channel follows by the same
// ratio. `before` is the clip list when the drag started. Lengths and offsets scale with the stretch.
void stretchAudioClip (Song& song, const std::vector<Clip>& before, int index, std::int64_t newStart, std::int64_t newLength,
                       bool createVariants);
} // namespace lynx::pltracks
