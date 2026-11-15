#pragma once

// The settings of Ableton Live 12's MIDI Tools *generators* (round 190fm, LIVE-FACTS §4: miditools-002, -005, -007,
// -066…-079) as LYNX keeps them: one struct per generator with Live's defaults (Live's own set file holds them in
// `NoteAlgorithms`, one element per tool [DATA DefaultLiveSet; SCHEMA 12402 RhythmAlgorithm / SeedAlgorithm /
// ShapeAlgorithm / StacksAlgorithm]). Song::midiGenerate holds them, so they are saved with the project like Live's.
// Pure data (no Song, no JUCE): Song.h includes it. What the values do is core/MidiGenerate.h.
//
// Tags: [DATA] Live's default value; [SHOT] the Generate panels' picture (ableton190o/shots/live-04-generate-panels.png);
// [INFERRED] LYNX's reading where neither says (each one is "needs live check" in docs/analise/REA_rodada190fm-mt.md).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lynx::midigen
{
// The note values LYNX's Rhythm "Step Duration" and Shape "Rate" step through, shortest first. [INFERRED] Live saves
// Step Duration as an enum whose default 7 reads "1/8" in the picture; the list 1/64T, 1/64, 1/32T, 1/32, 1/16T, 1/16,
// 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1 puts 1/8 at 7 (and starts where Seed's duration range starts, at 1/64).
constexpr int noteValueCount = 13;
const char* noteValueName (int index) noexcept;          // "1/8T"
std::int64_t noteValueTicks (int index) noexcept;        // in ticks (Song.h ppq), clamped index

struct RhythmOptions // [DATA] RhythmAlgorithm's defaults
{
    int pitch = 36;          // the key every hit plays (Live's C1)
    int steps = 8;           // PatternLength: steps per pattern, 1..32 [range INFERRED]
    int pattern = 14;        // which of the C(steps, density) rhythms, in LYNX's own order (rhythmHits()) [order INFERRED]
    int density = 4;         // hits per pattern, 0..steps
    int stepDuration = 7;    // noteValueTicks() index: 1/8
    float split = 0.0f;      // 0..1: share of the hits played as two notes of half a step [INFERRED]
    int shift = 0;           // rotates the pattern by steps (> 0 later)
    int velocity = 100;      // 1..127
    int accent = 127;        // the velocity of an accented hit
    int period = 4;          // "Freq": every period-th hit is accented (0 = none) [counted in hits: INFERRED]
    int offset = 0;          // the accent's place within the period (the panel's arrows)
    int repetitions = 1;     // each hit as this many equal notes within its step, 1..8 [INFERRED; not in the picture]
    bool operator== (const RhythmOptions&) const = default;
};

struct SeedOptions // [DATA] SeedAlgorithm's defaults
{
    float density = 0.5f;    // NotesDensity: the chance a free voice starts a note on a grid step [law INFERRED]
    int minPitch = 60, maxPitch = 84;
    float minDuration = -6.0f, maxDuration = -3.0f; // log2 of a whole note: -6 = 1/64 … -3 = 1/8 [SHOT]
    int minVelocity = 30, maxVelocity = 100;
    int voices = 4;          // VerticalLimit: notes sounding at once, 1..8
    bool operator== (const SeedOptions&) const = default;
};

struct ShapeOptions // [DATA] ShapeAlgorithm's defaults
{
    static constexpr int levelCount = 21; // [DATA] 21 ShapeLevels
    // LYNX's presets [STR: Live names Flat, Arc Up, Arc Down, Bounce Up, Bounce Down; Up / Down from the picture]. The
    // formulas are LYNX's (shapePreset()); Live's order is unknown, LYNX's puts Up at Live's default index 2.
    enum Preset { custom, flat, up, down, arcUp, arcDown, bounceUp, bounceDown, presetCount };
    std::array<float, levelCount> levels {};  // 0..1, filled from the preset by the constructor
    int preset = up;
    int rate = 0;            // 0 = the editor's grid ("Grid" [SHOT]); k > 0 = noteValueTicks (k - 1) [INFERRED]
    float tie = 0.0f;        // 0..1: chance a note lasts over the next step instead of a new note [INFERRED]
    float density = 1.0f;    // 0..1: chance each step gets its note
    int minPitch = 60, maxPitch = 84;
    float jitter = 0.0f;     // PitchVariation 0..1: random pitch offset up to ± jitter × half the range [INFERRED]
    int velocity = 100;      // LYNX: Live's Shape panel shows no velocity; 100 like Rhythm's default
    ShapeOptions();
    bool operator== (const ShapeOptions&) const = default;
};
// The 21 levels of a preset (custom: all 0.5).
std::array<float, ShapeOptions::levelCount> shapePreset (int preset) noexcept;
const char* shapePresetName (int preset) noexcept; // English, for tr()

struct StackChord // [DATA] StacksAlgorithm Sequence/Chord defaults
{
    int rootDegree = 0;      // with a project key: the root's scale degree…
    int octave = 3;          // …and octave (Live's numbering: octave 3 holds key 60)
    int rootPitch = 60;      // without a key: the root's key
    int inversion = 0;       // > 0: the lowest note up an octave that many times; < 0: the highest down
    int rule = 0;            // RuleNumber: the chord shape, 0..15 in the chord bank
    float duration = 1.0f;   // in bars [unit INFERRED]
    float offset = 0.0f;     // in 1/16 steps: the chord starts later (< 0 earlier), its end stays [unit INFERRED]
    bool operator== (const StackChord&) const = default;
};

struct StacksOptions
{
    std::vector<StackChord> sequence { StackChord {} };
    int selected = 0;        // the chord the panel edits (LYNX)
    std::string bankFile;    // a user's .stacks chord bank (read at run time; empty = LYNX's own bank)
    bool operator== (const StacksOptions&) const = default;
};

// LYNX addition beside Live's Max for Live "Euclidean" generator: one voice of core/NoteTools.h's euclideanHit().
struct EuclidOptions
{
    int steps = 16, density = 8, rotation = 0; // [SHOT] Steps 16, Density 8
    int division = 5;        // noteValueTicks() index: 1/16 [SHOT "1/16"]
    int pitch = 36, velocity = 100;
    bool operator== (const EuclidOptions&) const = default;
};

// Every generator's settings: Song::midiGenerate. Default-constructed = a project that never used them (saved as nothing).
struct Settings
{
    enum Tool { rhythm, seed, shape, stacks, euclidean, toolCount }; // Live's chooser order [SHOT]
    int tool = rhythm;
    RhythmOptions rhythmOptions;
    SeedOptions seedOptions;
    ShapeOptions shapeOptions;
    StacksOptions stacksOptions;
    EuclidOptions euclidOptions;
    bool operator== (const Settings&) const = default;
    bool isDefault() const { return *this == Settings {}; }
};
const char* toolName (int tool) noexcept; // English, for tr()

// The settings as text (key=value lines) and back; unknown keys are skipped and missing ones keep their defaults, so
// newer and older LYNX read each other's projects. ProjectFile stores the text as the project's "midiGenerate".
std::string encode (const Settings& settings);
Settings decode (const std::string& text);
} // namespace lynx::midigen
