#pragma once

// The Simple layout's region loop (round 190jr, BUILD-LIST 13; BandLab's loop handle: the circular arrow at a region's
// top right corner, dragged right to repeat the region, PROVEN in bandlab190p shots 05 / 18). A loop is a run of
// repeats: the region, then copies of it end to end on its track (same pattern or audio, same offset and length; the
// last may be shorter where the loop was let go). Copies, not a new clip field, so every engine, export and layout plays
// a loop as it is drawn, whatever Options › "Stretched pattern clips loop their pattern" says. Pattern repeats share the
// pattern (an edit of the notes plays in every pass, as in BandLab). Plain edits for AudioEngine::editSong. No JUCE.

#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::regionloop
{
// Whether a clip can loop: a pattern or audio clip of the Playlist (not the clip grid's), longer than 0.
bool loopable (const Song& song, int clip) noexcept;

struct Run
{
    std::vector<int> clips; // Playlist indices, in time order: [0] the region, then its repeats
    std::int64_t start = 0, end = 0, pass = 0; // the run's span and one pass (the region's length)
    bool empty() const noexcept { return clips.empty(); }
    int passes() const noexcept { return (int) clips.size(); }
};

// The run `clip` belongs to (as the region or a repeat). Empty for a clip that can't loop.
Run runOf (const Song& song, int clip);

// Where the next clip on the run's track starts after the run's region (INT64_MAX: none), ignoring the run's own clips.
std::int64_t nextStart (const Song& song, const Run& run);

// Loops the run of `clip` up to `end` (ticks; at least one whole pass, never past the next clip on the track): the
// repeats are rebuilt as whole passes, the last one shorter when `end` cuts it. Returns the run's new end, or −1 when
// nothing changed. Indices of other clips may move (repeats are erased and appended).
std::int64_t loopTo (Song& song, int clip, std::int64_t end);

// BandLab's "Loop until next": the run fills up to the next clip on its track (or `songEnd` when there is none and
// songEnd is past the run). Returns the new end or −1.
std::int64_t loopUntilNext (Song& song, int clip, std::int64_t songEnd);

// Removes the repeats: the region stays alone. False when it had none.
bool unloop (Song& song, int clip);

// The index of the run's region after an edit that may have moved indices (the clip at `start` on `track` of that kind
// and source), or −1.
int regionAt (const Song& song, int track, std::int64_t start, const Clip& like);
} // namespace lynx::regionloop
