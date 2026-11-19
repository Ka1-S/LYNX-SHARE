#pragma once

// The Simple workflow's Drum Machine page (round 190lr, DS-39; BandLab's web Studio drum machine dock, reference
// /mnt/e/lynx-work/bandlab190p/shots/09-drum-machine-dock.png and bandlab182d §"Drum machine dock"): a Drums track's
// step grid in the track dock, with BandLab's patterns A–H, Length, Swing, its own ▶ and a typing key per row.
//
// It is a VIEW of what the Drums track already is (core/SimpleTracks.h, decision D3): the kit's channels in the
// track's Channel Rack group, one insert, and the track's pattern clips. Nothing new is saved:
//  - a row is one kit channel (the channels that play through the track's insert and make notes, in channel order),
//    drawn bottom-up as BandLab draws its kit (the kick at the bottom), each with BandLab's typing key (bottom to top
//    A O F I E J K L: the eight rows a kit shows there);
//  - pattern A is the track's own pattern (named after the track, as addTrack makes it; else the track's first pattern
//    clip's pattern), B…H are the patterns named "<track> B" … "<track> H" (made the first time their letter is
//    picked, empty, as long as A). So the letters survive a save, an .flp export and a rename of the track
//    (simpletracks::rename renames them with it);
//  - a step is FL's step (Pattern::toggleStep, a sixteenth); Length is the pattern's length in bars; Swing is the
//    Channel Rack's swing (Song::swing), which BandLab's drum machine has per track and FL for the project.
// No JUCE.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::simplesteps
{
constexpr int letterCount = 8;  // A … H
constexpr int keyedRows = 8;    // BandLab's kit rows with a typing key

// The kit rows of Drums track `track`: song channel indices in channel order (row 0 = the first kit channel, drawn at
// the bottom). Empty for a track that is not a Drums track.
std::vector<int> kitRows (const Song& song, int track);

// BandLab's typing key of row `row` (0 = the bottom row): A O F I E J K L; 0 above the eighth row.
char rowKey (int row) noexcept;
// The row a typing key plays (any case), −1 when it is none of them.
int rowForKey (int keyCode) noexcept;

// The pattern of letter `letter` (0 = A … 7 = H) of the Drums track, −1 when it does not exist yet.
int letterPattern (const Song& song, int track, int letter);
// The letters that have a pattern (bit n = letter n).
unsigned lettersWithPattern (const Song& song, int track);
// The letter whose pattern is `pattern` on that track, −1 for none.
int letterOf (const Song& song, int track, int pattern);
// The pattern of `letter`, made when missing: empty, as long as A, the track's colour, named "<track> B" … (A is
// made as "<track>" for a Drums track that lost its pattern). −1 for a track that is not a Drums track.
int ensureLetter (Song& song, int track, int letter);
// "Add A": a clip of the letter's pattern on the track at `at` (ticks; BandLab: exactly at the playhead, unsnapped), as
// long as the pattern. It overwrites what lies under it on the track: a clip it starts inside keeps its left part, a clip
// it ends inside goes on from its end, a clip it covers goes. Returns the new clip's index in song.playlist (the last),
// −1 when the letter has no pattern.
int addLetterClip (Song& song, int track, int letter, std::int64_t at);

// The step count of a pattern of that song (its length in sixteenths; at least one bar)
int stepCount (const Song& song, int pattern);
// The pattern's length in whole bars (1 at least)
int lengthBars (const Song& song, int pattern);
// BandLab's Length choices, in bars
inline constexpr int lengthChoices[] { 1, 2, 4 };
// Sets the pattern's length to `bars` bars of the song's meter (1…8); the track's clips of it follow (the engine's
// followPatternLengths). Notes past the new end stay in the pattern (FL keeps them too) and come back when it grows.
void setLengthBars (Song& song, int pattern, int bars);

// The step a velocity 0..1 is drawn with: BandLab draws a step at the default velocity in the track colour and a
// softer step darker; LYNX's law: the share of the colour = 0.45 + 0.55 × velocity / the default (capped at 1).
float shade (float velocity) noexcept;

// The step of `pattern` the song plays now on that track, −1 when none: in Song mode a clip of the pattern on the
// track under the position (the clip's own start offset and repeats counted); in Pattern mode with that pattern
// current, the position in the pattern.
int playingStep (const Song& song, int track, int pattern, double positionTicks, bool playing);
} // namespace lynx::simplesteps
