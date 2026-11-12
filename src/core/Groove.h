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
// How many clips of the grid play through groove `id`.
int users (const Song& song, std::uint32_t id) noexcept;
// The groove of a clip of the grid (nullptr: none, or not a grid clip).
const Groove* ofClip (const Song& song, const Clip& clip) noexcept;
// Sets (id > 0) or clears (0) the groove of the clip in (track, scene). False when there is no clip or no such groove.
bool setClipGroove (Song& song, int track, int scene, std::uint32_t id);

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

// A groove from a pattern's notes ("Extract groove"): one point per occupied Base slot (the loudest note's place and
// velocity), over `length` ticks.
Groove extract (const Pattern& pattern, std::int64_t length, GrooveBase base, std::string name);

// --- content --------------------------------------------------------------------------------------------------------
struct BankEntry
{
    const char* folder; // "Swing", "Feel", "Accent", "Utility"
    Groove groove;
};
// LYNX's own grooves, generated: straight swing in steps, triplet shuffles, pushed and laid-back feels, accent
// shapes and the plain quantise grids.
const std::vector<BankEntry>& factoryBank();

// Reads a groove file (.agr: XML, the template is a MIDI clip inside it). Only for files the user owns: nothing of
// another program ships with LYNX. False (and `error`) when the text is not such a file.
bool parseAgr (std::string text, Groove& out, std::string* error = nullptr);
} // namespace lynx::groove
