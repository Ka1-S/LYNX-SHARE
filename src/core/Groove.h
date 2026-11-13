#pragma once

// The groove pool (round 190bv): timing and accent templates that the clips of the grid play through. The model
// (Groove, GroovePool) is in Song.h; a project that never used a groove carries an empty pool and its clips play exactly
// as before.
//
// What a groove does follows the published manual of the session-style program the Ableton-style layout studies
// (chapter "Using Grooves"; docs/analise/REA_rodada190bv.md lists each statement and what is still unmeasured):
//   - Base: the grid the template's notes are measured against; a template note on the grid moves nothing;
//   - Quantize: straight quantisation to the Base, applied before the template;
//   - Timing: how far the template moves the notes;
//   - Random: random timing, different for every note and every pass;
//   - Velocity: how far the template's accents change the velocities, −100…+100 % (negative: loud becomes quiet);
//   - the pool's Amount (0…130 %) scales Timing, Random and Velocity of every groove, not Quantize;
//   - Commit writes the result into the clip's notes and takes the groove off the clip.
// The grooves LYNX ships are its own (factoryBank(): made by the formulas below, no file of another program);
// a groove file the user owns can be read (parseAgr).
//
// Everything here is pure: no allocation in apply() (the scheduler calls it on the audio thread).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/Song.h"

namespace lynx::groove
{
constexpr float maxAmount = 1.3f;

constexpr std::int64_t baseTicks (GrooveBase base) noexcept
{
    switch (base)
    {
        case GrooveBase::quarter: return ppq;
        case GrooveBase::eighth: return ppq / 2;
        case GrooveBase::eighthTriplet: return ppq / 3;
        case GrooveBase::sixteenth: return ppq / 4;
        case GrooveBase::sixteenthTriplet: return ppq / 6;
        case GrooveBase::thirtySecond: return ppq / 8;
    }
    return ppq / 4;
}
const char* baseName (GrooveBase base) noexcept; // "1/4" … "1/32"

// --- the pool (message thread) -------------------------------------------------------------------------------------
const Groove* find (const GroovePool& pool, std::uint32_t id) noexcept;
Groove* find (GroovePool& pool, std::uint32_t id) noexcept;
// Adds a groove (its points sorted, its values clamped) under a new id, which is returned.
std::uint32_t add (GroovePool& pool, Groove groove);
// Removes a groove; the clips that used it play straight again. False when it is not in the pool.
bool remove (Song& song, std::uint32_t id);
// How many clips play through groove `id`: those of the grid and (round 190fm) the pattern clips of the Playlist and of
// the other arrangements.
int users (const Song& song, std::uint32_t id) noexcept;
// The groove of a clip: a grid clip's is in its slot, a Playlist clip's in Clip::groove (round 190fm). nullptr: none.
const Groove* ofClip (const Song& song, const Clip& clip) noexcept;
// Sets (id > 0) or clears (0) the groove of the clip in (track, scene). False when there is no clip or no such groove.
bool setClipGroove (Song& song, int track, int scene, std::uint32_t id);
// Round 190fm: the same for pattern clips of the Playlist (indices into Song::playlist). False when no index names a
// pattern clip or there is no such groove; audio and automation clips are skipped.
bool setPlaylistClipGroove (Song& song, const std::vector<int>& clips, std::uint32_t id);

// --- the maths ------------------------------------------------------------------------------------------------------
struct Played
{
    std::int64_t start = 0;
    float velocity = 0.0f;
};
// A note at `start` (ticks from the clip's start) with `velocity`, played through `g` with the pool's `amount`.
// `randomBits`: 32 random bits of this note in this pass (ignored while Random is 0).
Played apply (const Groove& g, float amount, std::int64_t start, float velocity, std::uint32_t randomBits) noexcept;
// The furthest apply() can move a note of any position, in ticks (the scheduler's look-around).
std::int64_t maxShift (const Groove& g, float amount) noexcept;
// The random bits the scheduler and commit() use: stateless, the same for a note in a pass every time it is asked.
std::uint32_t randomBits (std::uint32_t seed, std::size_t noteIndex, std::int64_t pass) noexcept;

// Commit: the groove of the clip in (track, scene) is written into its pattern's notes (the random part as of pass 0)
// and taken off the clip. A pattern other clips use too is copied first, so only this clip changes. False when the
// slot has no pattern clip with a groove.
bool commit (Song& song, int track, int scene);
// Round 190fm: Commit for a pattern clip of the Playlist (index into Song::playlist), the same way: a pattern that
// another clip plays too is copied first and only this clip moves to the copy.
bool commitPlaylistClip (Song& song, std::size_t index);

// A groove from a pattern's notes ("Extract groove"): one point per occupied Base slot (the loudest note's place and
// velocity), over `length` ticks.
Groove extract (const Pattern& pattern, std::int64_t length, GrooveBase base, std::string name);
// Round 190fm: from what a pattern clip plays (grid or Playlist): its pattern's notes from the clip's offset on, over the
// clip's length. An empty groove (no points) for a clip that is not a pattern clip.
Groove extractFromClip (const Song& song, const Clip& clip, GrooveBase base, std::string name);

// --- content --------------------------------------------------------------------------------------------------------
struct BankEntry
{
    const char* folder; // "Swing", "Classic swing", "Feel", "Accent", "Utility"
    Groove groove;
};
// LYNX's own grooves, generated: straight swing in steps, triplet shuffles, pushed and laid-back feels, accent
// shapes and the plain quantise grids. Round 190fm: "Classic swing", 8ths / 16ths / 32nds at the swing steps drum
// machines publish (50, 54, 58, 62, 63, 66, 67, 71, 75 %: the second note of each pair that far through the pair).
const std::vector<BankEntry>& factoryBank();
// The swing percents of "Classic swing" (round 190fm).
const std::vector<int>& classicSwingSteps();

// Reads a groove file (.agr: XML, the template is a MIDI clip inside it). Only for files the user owns: nothing of
// another program ships with LYNX. False (and `error`) when the text is not such a file.
bool parseAgr (std::string text, Groove& out, std::string* error = nullptr);
} // namespace lynx::groove

namespace lynx::xml
{
class Element;
}

namespace lynx::groove
{
// Round 190fm: one <Groove> element (of a groove file, or of a Live set's GroovePool): Name, the template clip
// (Clip/Value/MidiClip: its notes inside its loop), Grid → base, the four amounts in percent. False (and `error`) when
// the element has no template or its template no notes.
bool fromGrooveElement (const xml::Element& node, Groove& out, std::string* error = nullptr);
} // namespace lynx::groove
