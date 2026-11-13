#pragma once

// Piano Roll note tools (docs/WORKFLOW_FL.md P2: strum, arpeggiate, flip, randomise, scale levels, limit,
// scale snap) and the per-note "levels" shared by the Piano Roll lane and the step sequencer graph lanes.
// LYNX's own designs of the behaviours the manual describes; pure functions over Pattern, no JUCE.

#include <cstdint>
#include <array>
#include <vector>

#include "core/Song.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::notetools
{
// --- per-note levels, normalised to 0..1 ------------------------------------------------------
enum class Level
{
    velocity,  // 0..1
    pan,       // -1..1 -> 0..1
    release,   // 0..1
    finePitch, // -120..120 cents -> 0..1
    modX,      // 0..1
    modY       // 0..1
};

constexpr int levelCount = 6;
const char* levelName (Level level); // English, for tr()
float levelValue (const Note& note, Level level) noexcept;
void setLevelValue (Note& note, Level level, float normalised) noexcept;
float levelDefault (Level level) noexcept; // normalised value of a fresh note
bool levelIsBipolar (Level level) noexcept; // drawn from the centre line (pan, fine pitch)

// --- scales -----------------------------------------------------------------------------------
struct ScaleInfo
{
    const char* name; // English, for tr()
    std::vector<int> intervals;
};

const std::vector<ScaleInfo>& scales(); // [0] = no scale
// FL Studio's Piano Roll reference scales (its Data/Patches/Scores/Reference scales folder; the index in this list, FL's
// alphabetical order, is what FL saves in .flp event 0xF3 — REA round 168j): FL's name and the degrees above the root
struct FlReferenceScale
{
    const char* name;
    std::vector<int> intervals;
};
const std::vector<FlReferenceScale>& flReferenceScales();
// The scales() index with the same degrees as FL's reference scale `flIndex`; 0 = none (also FL's −1 Default,
// −2 Black notes, −3 Automatic and the scales LYNX does not list)
int scaleForFlReference (int flIndex) noexcept;
bool inScale (int key, int root, int scale) noexcept;
// The in-scale key nearest to `key` (a tie goes down); `key` itself without a scale. Stays in 0..127.
int snapToScale (int key, int root, int scale) noexcept;
// Moves every note to the nearest in-scale key.
void snapNotesToScale (Pattern& pattern, const std::vector<int>& indices, int root, int scale);
// Snap to scale while moving notes (issue #14): every moved note lands on the scale, not only the one under the mouse.
// `originals` = the notes before the move (same order as `indices`), `deltaKeys` = the semitones the mouse moved;
// each note goes to the in-scale key nearest to its own key + deltaKeys (0 = the pitch is left alone).
void moveKeysOnScale (Pattern& pattern, const std::vector<int>& indices, const std::vector<Note>& originals, int deltaKeys, int root, int scale);
// The keyboard's up / down by one (issue #14): with a scale, a note steps to the next in-scale key in that direction.
int stepOnScale (int key, int direction, int root, int scale) noexcept;

// The Piano Roll's key rows, top to bottom. "Fold to scale" (round 166d, issue #14 — LYNX's own: FL 2026 only
// highlights the scale, its Piano Roll menus have no fold; like Ableton's Scale fold) keeps only the in-scale keys.
struct KeyRows
{
    std::array<int, 128> keyOfRow {}; // the first `count` rows
    std::array<int, 128> rowOfKey {}; // −1 = folded away
    int count = 128;
    bool shown (int key) const noexcept { return key >= 0 && key < 128 && rowOfKey[(std::size_t) key] >= 0; }
    // the key `rows` rows above (+) or below (−) `key` (the nearest shown key for a folded one), clamped to the rows
    int step (int key, int rows) const noexcept;
};
KeyRows keyRows (int root, int scale, bool fold) noexcept;
// Rows of these keys only, highest at the top (round 190el: a Drum Rack's filled pads in the Clip-launch layout's note
// editor, Live's pad-name list); every key when `keys` is empty
KeyRows keyRowsOf (const std::vector<int>& keys) noexcept;

// --- chords -----------------------------------------------------------------------------------
// Groups the notes into chords: same channel and same start. Each group is sorted by key (low first);
// the groups are in start order.
std::vector<std::vector<int>> chordsOf (const Pattern& pattern, const std::vector<int>& indices);

// --- strum ------------------------------------------------------------------------------------
struct StrumOptions
{
    std::int64_t spread = 40;   // ticks from one chord note to the next
    float curve = 0.0f;         // -1..1: < 0 the strum speeds up, > 0 it slows down
    float velocityStep = 0.0f;  // added to each successive note's velocity (-0.25..0.25)
    bool downward = false;      // high note first
    bool alternate = false;     // every other chord strums the other way
    bool keepEnds = true;       // notes keep their end (they get shorter); otherwise they keep their length
};

void strum (Pattern& pattern, const std::vector<int>& indices, const StrumOptions& options);

// --- arpeggiate (offline) ---------------------------------------------------------------------
struct ArpeggiateOptions
{
    enum Direction { up, down, upDown, downUp };
    std::int64_t step = ticksPerStep; // time between arpeggio notes
    float gate = 0.8f;                // fraction of the step each note sounds (0.05..1)
    int direction = up;
    int octaves = 1;                  // 1..4
};

// Replaces each chord (and, with more than one octave, each single note) by a run of notes over its length.
// Returns the indices of the new notes (not re-sorted).
std::vector<int> arpeggiate (Pattern& pattern, const std::vector<int>& indices, const ArpeggiateOptions& options);

// --- flip -------------------------------------------------------------------------------------
// Horizontal: mirrors the notes in time inside their span. Vertical: mirrors the keys around the middle
// of their range. With preserveStarts (FL's "Preserve start times") the horizontal flip mirrors the start
// times between the first and the last start instead of mirroring each note's whole extent, so the set of
// start times stays on the same grid whatever the note lengths are.
void flip (Pattern& pattern, const std::vector<int>& indices, bool horizontal, bool vertical,
           bool preserveStarts = false);

// --- randomise --------------------------------------------------------------------------------
struct RandomiseOptions
{
    float velocity = 0.25f;   // maximum change (0..1 of the full range)
    float pan = 0.0f;
    float finePitch = 0.0f;   // fraction of ±120 cents
    float modX = 0.0f, modY = 0.0f;
    float release = 0.0f;     // (round 190bw; FL's randomizer has Release beside Mod X and Mod Y)
    std::int64_t timing = 0;  // maximum start shift in ticks (never before 0)
    float length = 0.0f;      // maximum relative length change (0..1)
    bool bipolar = true;      // FL's "Bipolar": changes go both ways; off, every level only goes up (pan to the
                              // right, pitch up) and timing only later (live FL check 190cq, round 190ct)
    std::uint32_t seed = 1;
};

void randomise (Pattern& pattern, const std::vector<int>& indices, const RandomiseOptions& options);

// --- scale levels -----------------------------------------------------------------------------
// Each value becomes value * scale + offset (pan and fine pitch around their centre), where `scale` ramps
// from `scaleStart` (first note) to `scaleEnd` (last note) through time: a fade in or out.
void scaleLevels (Pattern& pattern, const std::vector<int>& indices, Level level, float scaleStart, float scaleEnd, float offset);

// FL's "level scaling" law on a normalised value x (0…1) [REA FLEngine 0xdf0a40 / 0xdf0900, round 190bw]:
//   d = |x − center| / max (center, 1 − center)                    (0…1: how far from the centre)
//   f = d,  or bent by the tension t (−1…1) with p = 1001^|t| − 1:  t > 0: 1 − ((p + 1)^(1 − d) − 1) / p
//                                                                   t < 0: ((p + 1)^d − 1) / p
//   y = sign (x − center) · f · multiply · max (center, 1 − center) + offset + center,  limited to 0…1
// center 0…1 (FL's default 0), multiply −2…2 (default 1), offset −1…1, tension −1…1. With tension 0 this is
// (x − center) · multiply + center + offset.
double flLevelScale (double x, double center, double multiply, double offset, double tension) noexcept;

// The same tool with FL's Center and Tension; the multiplier still ramps from scaleStart to scaleEnd through time
// (LYNX's own extra: FL has one Multiply for all notes).
void scaleLevels (Pattern& pattern, const std::vector<int>& indices, Level level, float scaleStart, float scaleEnd, float offset,
                  float center, float tension);

// --- limit ------------------------------------------------------------------------------------
struct LimitOptions
{
    int lowKey = 36, highKey = 84;   // notes outside move by octaves into the range, then are clamped
    float minVelocity = 0.0f, maxVelocity = 1.0f; // LYNX's own extra (FL's limiter has no velocity range)

    // FL's "Key / scale restriction" and "Keyboard range / transpose" (round 190bw, FL's limiter / transposer):
    int root = 0;                    // base key, 0 = C ... 11 = B
    int scale = 0;                   // index into limitRestrictions() (FL's chord / scale list); 0 = (none)
    enum Snap { above, under, alternate };
    int snap = above;                // where a note exactly between two scale keys goes (alternate: up first)
    bool wrap = false;               // "Wrap to top / bottom": notes above the range go to its lowest octave,
                                     // notes below to its highest octave (off: the nearest octave that fits)
    int transpose = 0;               // semitones, applied after the range; folded by octaves onto the keyboard
};

// The order is FL's: scale restriction on the note's own key, then the range, then the transposition.
void limit (Pattern& pattern, const std::vector<int>& indices, const LimitOptions& options);

// FL's chord / scale list of the limiter (and of the Stamp tool): FL's own data file System/Config/Chords.map, in its
// order — (none), then the 66 chords in six groups (triads, 6, 7, 9, 11, 13), then the 22 scales (round 190ct, list
// seen in FL: live check 190cq shot curves190bw-05f). Each entry is the steps in semitones between its notes, above
// the base key, as the file gives them (a chord may span more than an octave).
struct LimitRestriction
{
    enum Kind { none, chord, scale };
    const char* name; // as FL shows it; scale names are English for tr(), chord symbols stay as they are
    Kind kind;
    bool groupStart;  // the first entry of a group (FL draws a separator, or the "Chords" / "Scales" heading)
    std::vector<int> steps;
};
const std::vector<LimitRestriction>& limitRestrictions();
// The index of the entry with this name and kind; 0 when there is none.
int limitRestrictionIndex (const char* name, LimitRestriction::Kind kind) noexcept;
// The keys the restriction allows, worked out as FL's limiter does [REA FLEngine 0xded3b0]: for the octaves −1…10 the
// base key and the keys reached by adding the steps one by one, on FL's 132-key range (0…131). Index 0 allows all.
std::array<bool, 132> limitAllowedKeys (int root, int restriction);

// --- LYNX's own versions of the tools FL ships as piano roll scripts (REA round 143: Tools › Scripts) -----------

// Euclidean rhythm: `pulses` hits spread as evenly as possible over `steps` steps of `stepTicks` (rotated by
// `rotation` steps), repeated for `length` ticks from `start`, on `key` of `channel`. True at a step that hits.
bool euclideanHit (int steps, int pulses, int rotation, int step) noexcept;
struct EuclideanOptions
{
    int steps = 16, pulses = 5, rotation = 0;
    std::int64_t stepTicks = ticksPerStep;
    std::int64_t start = 0, length = 4 * 4 * ticksPerStep;
    int channel = 0, key = 60;
    float velocity = 0.78f;
    float gate = 1.0f;     // note length as a fraction of a step
    float accent = 0.0f;   // extra velocity on the first hit of each cycle
};
// Removes `replace` (note indices) and writes the rhythm; returns the new notes' indices.
std::vector<int> euclidean (Pattern& pattern, const std::vector<int>& replace, const EuclideanOptions& options);

// Humanize: small, musical irregularities. Timing moves each start by up to `timing` ticks (both ways, never
// before 0), with notes of one chord moving together; velocity varies by up to `velocity` (the off-beat notes a
// bit softer when `softerOffbeats`); length by up to `length` (relative).
struct HumanizeOptions
{
    std::int64_t timing = 6;
    float velocity = 0.08f;
    float length = 0.05f;
    bool softerOffbeats = true;
    std::uint32_t seed = 1;
};
void humanize (Pattern& pattern, const std::vector<int>& indices, const HumanizeOptions& options);

// Note repeater: each note becomes repeats every `rate` ticks over its length (gate = repeat length / rate),
// the velocity multiplied by `decay` at each repeat. Returns the indices of all the resulting notes.
struct RepeatOptions
{
    std::int64_t rate = ticksPerStep;
    float gate = 0.5f;
    float decay = 0.9f;
    int pitchStep = 0; // semitones added at each repeat (0 = same key)
};
std::vector<int> repeatNotes (Pattern& pattern, const std::vector<int>& indices, const RepeatOptions& options);

// Select by velocity: the notes of `indices` whose velocity is within [low, high].
std::vector<int> selectByVelocity (const Pattern& pattern, const std::vector<int>& indices, float low, float high);
// The loudest / quietest `fraction` (0..1) of `indices` (at least one note when there are any).
std::vector<int> selectLoudest (const Pattern& pattern, const std::vector<int>& indices, float fraction, bool loudest);

// --- selection helpers (FL's piano roll Select menu, behaviour from its manual; REA round 153) ------------------
// `all` minus `selected` (both index lists; the result is sorted).
std::vector<int> invertSelection (const std::vector<int>& all, const std::vector<int>& selected);
// The notes of `indices` in colour group `colour` (0..15).
std::vector<int> selectByColour (const Pattern& pattern, const std::vector<int>& indices, int colour);
// Every other note in time order (start, then key): the 1st, 3rd, 5th ...
std::vector<int> selectOdd (const Pattern& pattern, const std::vector<int>& indices);
// About `fraction` (0..1) of `indices` chosen at random (each note independently; deterministic for a seed).
std::vector<int> selectRandom (const Pattern& pattern, const std::vector<int>& indices, float fraction, std::uint32_t seed);
std::vector<int> selectMuted (const Pattern& pattern, const std::vector<int>& indices);
// Notes that overlap an earlier note of the same key (every layer except the first of each overlapping run).
std::vector<int> selectOverlapping (const Pattern& pattern, const std::vector<int>& indices);
// Notes that lie completely inside another note of the same key (exact duplicates: all but the first), so deleting
// them leaves what is heard unchanged.
std::vector<int> selectStacked (const Pattern& pattern, const std::vector<int>& indices);
// Note groups (FL's Group Shift+G / Ungroup Alt+G): grouping gives the notes a new group number; ungrouping clears
// it. groupOf: the notes of the same channel in `index`'s group (just `index` when it has none). regroup: notes just
// pasted get new group numbers (each group of the copies its own), so copies never join the originals' groups.
void groupNotes (Pattern& pattern, const std::vector<int>& indices);
void ungroupNotes (Pattern& pattern, const std::vector<int>& indices);
// Live 12's probability groups (round 187c, Note::chanceGroup): the notes become one group that draws together, "Play
// one" (one of them plays) or "Play all", at 100 % as in Live (one Chance marker per group).
// Ungrouping keeps each note's chance.
void groupChance (Pattern& pattern, const std::vector<int>& indices, bool playOne);
void ungroupChance (Pattern& pattern, const std::vector<int>& indices);
// The groups of the `edited` notes take their chance (a group has one Chance marker, as in Live)
void spreadGroupChance (Pattern& pattern, const std::vector<int>& edited);
std::vector<int> groupOf (const Pattern& pattern, int index);
void regroup (Pattern& pattern, const std::vector<int>& indices);

// The General MIDI percussion names (keys 35..81; nullptr elsewhere), for the Piano Roll's key labels.
const char* generalMidiDrumName (int key) noexcept;

// Puts the notes into colour group `colour` (0..15).
void setColour (Pattern& pattern, const std::vector<int>& indices, int colour);
} // namespace lynx::notetools
