#pragma once

// FL's legacy Pattern Blocks (round 170f, issue #60; docs/analise/REA_rodada170f.md): an opt-in section of the Playlist
// with one row per pattern, where a click places a whole instance of that row's pattern on a bar. Blocks can't be
// resized, slipped or put on another row: a block is (pattern, bar start) and always plays its whole pattern.
// Playback and export turn them into plain pattern clips (expandSong), so the audio thread never sees them.

#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::patternblocks
{
// The track the clips made from blocks carry: no Playlist track (never muted by one, not drawn in the clip area).
constexpr int blockTrack = -1;

// The length a block of `pattern` plays: the pattern's effectiveLength (0 for a pattern that doesn't exist).
std::int64_t lengthOf (const Song& song, int pattern) noexcept;

// Whether blocks play: the option is on and there is at least one block.
bool active (const Song& song) noexcept;

// Keeps a block list in order (pattern, then start) without duplicates or blocks of missing patterns.
void normalize (std::vector<PatternBlock>& blocks, int patternCount);

// Where block `index` of `blocks` (normalized) stops playing: start + its pattern's length, cut at the next block of
// the same row (a pattern that grew never plays over its own next block).
std::int64_t playedEnd (const Song& song, const std::vector<PatternBlock>& blocks, std::size_t index) noexcept;

// The block of row `pattern` covering `tick` (by its played span), or -1.
int blockAt (const Song& song, int pattern, std::int64_t tick) noexcept;

// The bar start a click at `tick` places a block on (time-signature aware).
std::int64_t snapToBar (const Song& song, std::int64_t tick);

// Whether a block of `pattern` at `start` fits: it would not overlap a block of its row.
bool fits (const Song& song, int pattern, std::int64_t start) noexcept;

// Places a block of `pattern` on the bar at `tick` (snapped); false if the pattern doesn't exist or it doesn't fit.
bool place (Song& song, int pattern, std::int64_t tick);

// Paints along a row from `fromTick` to `toTick` (either order): every bar in between whose block fits gets one, in
// time order (so a 2-bar pattern dragged over 4 bars gets blocks on bars 1 and 3). Returns how many were placed.
int paintRange (Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick);

// Removes the block of row `pattern` under `tick`; false if there is none.
bool erase (Song& song, int pattern, std::int64_t tick);

// Moves the block of row `pattern` under `fromTick` to the bar at `toTick` (snapped to a bar, clamped to >= 0),
// keeping its row. Returns the block's start after the call: the new start on success, the unchanged start when it
// cannot move (collision with another block of the row), or -1 when there is no block of that pattern at `fromTick`
// (invalid and zero-length patterns never have one: `blockAt` finds nothing there).
std::int64_t move (Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick);

// Removes every block of row `pattern` whose played span meets [fromTick, toTick]. Returns how many.
int eraseRange (Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick);

// Removes every block of any row whose played span meets [fromTick, toTick]. Returns how many.
int eraseAnyRow (Song& song, std::int64_t fromTick, std::int64_t toTick);

// Whether a block of row `pattern` plays over [fromTick, toTick] (either order): what eraseRange would remove.
bool anyInRange (const Song& song, int pattern, std::int64_t fromTick, std::int64_t toTick) noexcept;

// A move drag keeps the point where the block was grabbed (issue #127): a 2-bar block pressed on its second bar stays
// under the mouse by that bar instead of jumping to start under it.
// grabOffset: the bar under `tick` minus the start of the block of row `pattern` there; 0 when there is none.
// dragTarget: the bar the block starts on while the mouse is at `tick` (never before bar 1).
std::int64_t grabOffset (const Song& song, int pattern, std::int64_t tick);
std::int64_t dragTarget (const Song& song, std::int64_t tick, std::int64_t grabOffset);

// The blocks of the current arrangement as pattern clips on blockTrack (each its played span), in block order.
std::vector<Clip> asClips (const Song& song);

// The end of the last block (0 without active blocks).
std::int64_t end (const Song& song) noexcept;

// For playback / export copies: when active, appends the blocks to the playlist as clips; then clears every block list
// and the option, so the copy holds plain clips only (calling it twice is harmless).
void expandSong (Song& song);

// FL's "Turn pattern instances into clips": each pattern's blocks become pattern clips on a new track of its own below
// the last used one (named after the pattern, locked to its content). The blocks are removed; the option is unchanged.
// Returns how many clips were made.
int toClips (Song& song);
// toClips in every arrangement (the option going off with blocks left). Returns how many clips were made.
int toClipsInEveryArrangement (Song& song);

// Pattern list edits (keep the blocks of every arrangement on their pattern):
void removePattern (Song& song, int index);              // its blocks go, later rows move up
void shiftPatterns (Song& song, int index, int delta);   // rows at or after `index` move by `delta`
void swapPatterns (Song& song, int a, int b);
} // namespace lynx::patternblocks
