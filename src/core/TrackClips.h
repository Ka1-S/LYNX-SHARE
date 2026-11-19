#pragma once

// Track clips (round 190ne, LYNX 0.1.31; design A of round 190nd, /mnt/e/lynx-work/study190nd/tracks/DESIGN-tracks.md).
// A LYNX extra on FL's instrument track, asked for on Discord ("keep the rack, just add special tracks where you draw
// MIDI clips without cluttering the pattern list"). A per-track switch, "Clips own their notes"
// (PlaylistTrack::ownClips), makes an instrument track behave like a Live / Bitwig note-clip track:
//  - the pencil on empty space draws a NEW empty clip of the track (FL places the same pattern on every click);
//  - copies (Shift+drag, Ctrl+B, Ctrl+C / Ctrl+V) are unique by default; a linked copy is a choice (Shift+L+drag,
//    Ctrl+Alt+B, the clip menu's "Make linked copy"), and links stay inside the track as in Bitwig;
//  - an unused track clip goes with its last clip, in the same undo step (Live; ordinary patterns stay, as in FL).
// The storage stays FL's: every clip is still a Pattern, flagged Pattern::trackClip, so the engine, the piano roll, the
// step sequencer, every export, Online Sessions and scripts are unchanged and nothing new plays on the audio thread.
// Track clips stay out of the toolbar's pattern selector (unless the Picker group's "shown" switch is on) and sit in the
// Picker's folded "Track clips" group, so the pattern list stays FL's. On .flp export identical track clips are merged
// (FL has no such flag: each becomes an ordinary pattern on FL's instrument track) and more than FL's 999 patterns are
// reported. Owner decisions (2026-11-19, the study's recommended options): the name "Clips own their notes", copies
// unique by default, automatic deletion, a silent merge on export, off by default in the Pattern layout and on for new
// instrument tracks in the Clip-launch and Simple layouts (and on every MIDI track of an imported Live set).
// Plain edits for AudioEngine::editSong; no JUCE. Tests: tests/TrackClips190neTests.inc.

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx::trackclips
{
// FL Studio's pattern limit (the controller API's patterns.patternMax = 999; the toolbar's PatSelect stops at 999)
inline constexpr int flPatternLimit = 999;

// The Picker group's "shown" switch: track clips are listed in the toolbar's pattern selector (and the rack's pattern
// list, the selector's wheel and drag) too. An app setting, off by default (FL's list unchanged).
inline std::atomic<bool>& shownFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool shownInSelector() noexcept { return shownFlag().load (std::memory_order_relaxed); }
inline void setShownInSelector (bool on) noexcept { shownFlag().store (on, std::memory_order_relaxed); }

// The switch a NEW instrument track gets (trackmodes::makeInstrumentTrack): off in the Pattern layout (FL's own
// instrument track), on in the Clip-launch and Simple layouts (layoutstyle::current()).
bool defaultOn() noexcept;

// Track `track` is an instrument track with "Clips own their notes" on.
bool owns (const Song& song, int track) noexcept;
// Pattern `pattern` is a track clip.
bool isTrackClip (const Song& song, int pattern) noexcept;
// How many patterns are track clips.
int count (const Song& song) noexcept;
// Whether the pattern selector (toolbar, rack list, the selector's wheel) lists pattern `pattern`: every ordinary
// pattern, a track clip only while shownInSelector(). The current pattern is shown by name either way.
bool listedInSelector (const Song& song, int pattern) noexcept;
// The selector's step (Numpad +/-, its wheel and drag): rackops' stepPattern over the listed patterns only.
int stepListed (const Song& song, int delta) noexcept;

// The name a track's clips are called after: the track's name, else its channel's (an instrument track), else
// "Track N".
std::string trackLabel (const Song& song, int track);
// "<track> · clip N", N the lowest number no pattern uses yet.
std::string newClipName (const Song& song, int track);
// The colour a new clip of the track takes: the track's, else its channel's, else 0 (automatic).
std::uint32_t trackColour (const Song& song, int track);

// A new empty track clip of `track`, `length` ticks long (> 0; FL's "one bar" when the pencil has drawn nothing else
// yet), named and coloured after the track. Returns its pattern index. Places no clip.
int newClip (Song& song, int track, std::int64_t length);
// A unique copy of pattern `pattern` for track `track` (a track clip, named as a new clip of the track, a uid of its
// own). Returns the copy's index, -1 when `pattern` does not exist.
int clone (Song& song, int pattern, int track);

// Copies were just placed: the clips `clips` (indices into song.playlist) whose pattern is a track clip get unique
// copies, one per source among them (two copies of one linked clip stay linked to each other, not to the original),
// so editing a copy never changes the clip it was copied from. Ordinary patterns keep FL's sharing. Returns how many
// patterns were made.
int makeCopiesUnique (Song& song, const std::vector<int>& clips);
// The same for clips that are not in a list yet (pasted, dropped on the clip grid): each clip whose pattern is a track
// clip gets a copy.
int makeCopiesUnique (Song& song, std::vector<Clip>& clips);

// A clipboard of clips must not point at a track clip by index: cutting the last clip deletes it (collect). The copied
// track clips travel with the clipboard and come back as copies (unique, as every copy) when it is pasted.
struct Carried
{
    std::vector<std::pair<int, Pattern>> patterns; // (index when copied, its content)
    bool empty() const noexcept { return patterns.empty(); }
};
Carried carry (const Song& song, const std::vector<Clip>& clips);
// Before pasting `clips` (copied together with `carried`): each carried source gets one new pattern (a copy of what was
// copied), and the clips that played it play the copy. Clips of other patterns are left as they are.
void materialize (Song& song, std::vector<Clip>& clips, const Carried& carried);

// Per pattern: how many clips play it in every clip list (the arrangements, the clip grid) plus pattern blocks and
// Loop Starter channels that belong to it.
std::vector<int> uses (const Song& song);
// Deletes the track clips nothing uses (uses() == 0), the current pattern following. Ordinary patterns are never touched.
// `removed`, when given, gets the indices removed (as they were before, ascending). Returns how many.
int collect (Song& song, std::vector<int>* removed = nullptr);

// The track a track clip belongs to: the track of the first clip that plays it (the Playlist, then the other
// arrangements, then the grid); -1 when none does.
int ownerTrack (const Song& song, int pattern);
// ownerTrack of every pattern at once (one pass over the clip lists).
std::vector<int> ownerTracks (const Song& song);

// The switch. On: the track's patterns that only this track plays (in every clip list) and whose notes are all its
// channel's become its track clips (adopt). Off: its track clips become ordinary patterns again (they show in the
// pattern list). Nothing else changes; one undo step in the caller.
void setOwnClips (Song& song, int track, bool on);
// The adopt half (also .als import, the Simple layout): returns how many patterns became track clips.
int adopt (Song& song, int track);
// The release half: returns how many track clips became ordinary patterns.
int release (Song& song, int track);

// Two patterns hold the same music (notes, events, length, loops, markers, Loop Starter genre; name and colour may differ).
bool sameContent (const Pattern& a, const Pattern& b) noexcept;
// Export to FL (core/FlpExport.cpp): identical track clips are merged into the first of them (Bitwig's "Merge duplicate
// patterns", silent, the owner's choice), every clip following. Returns how many patterns went.
int mergeIdentical (Song& song);

// A bus track (the design's "bus tracks" = FL's audio tracks linked to a bus insert): an audio track whose insert other
// inserts send into. The header shows a bus icon. False for anything else.
bool isBusTrack (const Song& song, int track) noexcept;
// How many inserts send into a bus track's insert (0: not a bus track).
int busSources (const Song& song, int track) noexcept;
} // namespace lynx::trackclips
