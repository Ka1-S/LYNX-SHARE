#pragma once

// Ableton Live 12's MIDI Tools generators (round 190fm, LIVE-FACTS §4) as LYNX additions beside FL's Piano roll tools:
// Rhythm, Seed, Shape, Stacks and a one-voice Euclidean. Like Live's Generate, a generator writes new notes into a time
// range (the editor's time selection, else the whole pattern) and they replace the notes of that channel starting in
// the range. Every random draw comes from `Context::seed`, so a preview and its Generate give the same notes; the
// panel draws a new seed for each Generate press (Live: every press gives a new result).
//
// The settings and their defaults are core/MidiGenerateOptions.h (Live's [DATA]); what each control does is LYNX's
// reading where only the names and defaults are known ([INFERRED] in the comments; "needs live check" in
// docs/analise/REA_rodada190fm-mt.md). Clean-room: LYNX's own rhythm order, shape formulas and chord bank; a user's
// own .stacks chord bank can be read at run time (readChordBank). No JUCE.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/MidiGenerateOptions.h"
#include "core/Song.h"

namespace lynx::midigen
{
using Notes = std::vector<Note>;

// The scale the generated pitches keep to: `intervals` above `root` (pitch class 0 = C); empty = none (every key).
struct ScaleRef
{
    int root = 0;
    std::vector<int> intervals;
    bool on() const noexcept { return ! intervals.empty(); }
};

struct Context
{
    std::int64_t from = 0, to = 4 * ppq;  // the time range (ticks); to > from
    int channel = 0;                      // the channel the new notes get
    std::int64_t grid = ppq / 4;          // the editor's grid (Shape's and Seed's "Grid")
    ScaleRef scale;
    std::uint32_t seed = 1;
    int beatsPerBar = 4;                  // a bar, for Stacks' durations: the song's meter (beatsPerBar / beatUnit,
    int beatUnit = 4;                     // round 190fg; a 6/8 bar = 3 quarters)
};

// --- Rhythm ------------------------------------------------------------------------------------------------------
// How many rhythms Pattern chooses from: C(steps, density) (saturates at INT64_MAX).
std::int64_t rhythmCount (int steps, int density) noexcept;
// The hit steps (ascending, before Shift) of rhythm `pattern` (taken modulo rhythmCount). LYNX's own order [INFERRED:
// Live's table is unknown]: rank the steps by priority — first the `density` hits of the Euclidean rhythm (euclideanHit,
// in time order), then the other steps in time order — and take the pattern-th `density`-subset of that ranking in
// lexicographic order. Pattern 0 is the Euclidean rhythm; neighbouring patterns differ in few steps.
std::vector<int> rhythmHits (int steps, int density, std::int64_t pattern);
// The pattern (steps × step duration) repeats from `from` until `to`; notes reaching past `to` are cut there. Hit h
// (counted from the range's start) is accented when period > 0 and (h - offset) is a multiple of period. Split plays
// an even share of the hits (h where floor ((h + 1) × split) > floor (h × split)) as two notes; Repetitions divides
// every hit's step into that many notes (both together multiply).
Notes rhythm (const RhythmOptions& options, const Context& context);

// --- Seed --------------------------------------------------------------------------------------------------------
// [INFERRED law] On every grid step of the range, each free voice (at most `voices` notes sound at once) starts a
// note with chance `density`: pitch drawn evenly from the in-range keys (the scale's keys when one is on; a key that
// already sounds is not drawn twice), length 2^d whole notes with d drawn evenly in [minDuration, maxDuration],
// velocity drawn evenly in [minVelocity, maxVelocity]. Notes are cut at the range's end.
Notes seed (const SeedOptions& options, const Context& context);

// --- Shape -------------------------------------------------------------------------------------------------------
// The shape's value 0..1 at x = 0..1 of the range (straight lines between the 21 levels).
double shapeAt (const ShapeOptions& options, double x) noexcept;
// One note per step of the rate (the grid at 0): key = minPitch + shapeAt (step start) × (maxPitch - minPitch), moved
// by the jitter, then to the nearest key of the scale when one is on (kept in [minPitch, maxPitch]). A step is left
// empty with chance 1 - density; a note is tied over the next step (one longer note, that step's own note dropped)
// with chance `tie`.
Notes shape (const ShapeOptions& options, const Context& context);

// --- Stacks ------------------------------------------------------------------------------------------------------
// A chord bank: per scale size (notes per octave), up to 16 rules; a rule is a chord as scale-degree offsets from the
// root (0 = the root, 2 = a third up in a 7-note scale, 7 = the octave...).
struct ChordBank
{
    struct RuleSet
    {
        int scaleSize = 7;
        std::vector<std::vector<int>> rules;
    };
    std::vector<RuleSet> sets;
    // The rules for a scale of `scaleSize` notes: that size's set, else the nearest smaller one, else the first.
    const std::vector<std::vector<int>>* rulesFor (int scaleSize) const noexcept;
};
// LYNX's own bank (sizes 5, 6, 7, 16 rules each), written from basic chord theory: triads, sevenths, sus, power,
// quartal, added-note and open voicings (MidiGenerate.cpp lists them).
const ChordBank& lynxChordBank();
// Reads a chord bank in Live's .stacks layout (JSON: "chord_rulesets": [{ "scale_cardinality": n, "rules": [[...], ...]
// }, ...]); nullopt (with `error`) when the text has no usable rule set. For a file the user owns, read at run time.
std::optional<ChordBank> readChordBank (const std::string& json, std::string& error);
// A chord's keys, low to high: its root (the scale's degree + octave with a scale, else rootPitch; without a scale the
// rules read the major scale on rootPitch), the rule's degrees, then the inversion. Keys stay in 0..127.
std::vector<int> stackKeys (const StackChord& chord, const ScaleRef& scale, const ChordBank& bank);
// The chord's root key (what the panel shows).
int stackRoot (const StackChord& chord, const ScaleRef& scale) noexcept;
// A short chord name for the panel ("C", "Am", "G7", "Dsus4", or the root and "+n" notes when it is none of those).
std::string stackChordName (const StackChord& chord, const ScaleRef& scale, const ChordBank& bank);
// The sequence plays from `from`, each chord for its duration (bars), and repeats until `to` [repeat INFERRED].
// Offset moves a chord's start by 1/16 steps keeping its end (never before the range or after its end). Velocity 100.
Notes stacks (const StacksOptions& options, const Context& context, const ChordBank& bank);

// --- Euclidean (LYNX, one voice) ---------------------------------------------------------------------------------
// euclideanHit (steps, density, rotation) on every step of `division`, repeated over the range.
Notes euclid (const EuclidOptions& options, const Context& context);

// --- the panel's call --------------------------------------------------------------------------------------------
// The current tool of `settings` (Settings::tool) with its options.
Notes generate (const Settings& settings, const Context& context, const ChordBank& bank);
// The indices of `pattern`'s notes a generator replaces: those of `channel` that start in [from, to).
std::vector<int> notesInRange (const Pattern& pattern, int channel, std::int64_t from, std::int64_t to);
// Replaces those notes by `generated` (miditools::replaceNotes); returns the new notes' indices after sorting.
std::vector<int> writeGenerated (Pattern& pattern, const Context& context, const Notes& generated);
} // namespace lynx::midigen
