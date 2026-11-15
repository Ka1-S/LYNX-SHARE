#pragma once

// More Piano Roll tools (round 83): quantizer with templates, chopper, flamer, LFO, claw machine, articulator,
// riff machine and chord progression. The behaviour of the first six follows FL Studio's tools as analysed in
// docs/analise/REA_rodada83.md (formulas, ranges, defaults; our own code). Score templates (.fsc) are read from
// the user's FL Studio installation at run time; built-in templates are used when there is none.
// Pure functions over Pattern, no JUCE.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/NoteTools.h"
#include "core/Song.h"

namespace lynx::prtools
{
// --- score templates (.fsc) -------------------------------------------------------------------
struct ScoreNote
{
    std::int64_t start = 0, length = 0; // ticks (LYNX PPQ)
    int key = 60;
    float velocity = 100.0f / 128.0f;   // 0..1 (FL byte / 128)
    float pan = 0.0f;                   // -1..1
    float release = 0.5f;               // 0..1
    float finePitch = 0.0f;             // cents
    float modX = 128.0f / 255.0f, modY = 128.0f / 255.0f;
    int colour = 0;                     // FL note colour / MIDI channel 0..15
    bool slide = false;
    bool muted = false, porta = false;  // the record's extra byte 0x20 / 0x10 (round 168g: Open score keeps them)
    int group = 0;                      // note group (24-byte records only)
};

struct Score
{
    std::string name;
    std::vector<ScoreNote> notes; // sorted by start

    // end of the last note (a note counts at least one tick); 0 when empty
    std::int64_t length() const noexcept;
};

// Reads an FL score file (.fsc: an .flp with one pattern of notes), rescaled to LYNX's PPQ.
std::optional<Score> parseScore (std::span<const std::uint8_t> bytes);
// One note per `step` over one beat (quantizer default), with `swing` (0..1) delaying every other step by
// up to half a step.
Score gridScore (std::int64_t step, float swing = 0.0f);

// The Piano roll's File › Open score / Save score as (round 168g, REA_rodada168g.md §3): the score's notes as notes of
// `channel` (all of them, at their own times), and an .fsc of `notes` as FL writes one from the piano roll
// (FUN_0121b1b0 → the project saver in score mode 0x10: the version text, 0x41 = 0, then every note as a 24-byte 0xE0
// record with the channel field 0; header format 0x10, one channel, LYNX's PPQ).
std::vector<Note> notesFromScore (const Score& score, int channel);
std::vector<std::uint8_t> writeScore (const std::vector<Note>& notes);

// FL's event scores (issue #86, REA_rodada178a §4): the Event editor's Save score as / Open score. One target's events
// as an .fsc of header format 0x18: 0x41 = 0, then 0xDF records {tick, control 0, the value normalised to 0..1 as a
// float}. Target-agnostic: opening one puts its curve on whatever target the editor shows (a Tempo curve onto a
// volume, and back). LYNX's lane values are the normalised values (exactly FL's for the tempo, volumes and pans).
std::vector<std::uint8_t> writeEventScore (const std::vector<PatternEvent>& events, std::uint32_t control = 0);
// The events of an event score at LYNX's PPQ, sorted, one per tick; nothing when the file is not one (a note score).
std::optional<std::vector<PatternEvent>> parseEventScore (std::span<const std::uint8_t> bytes);

// Per-level strengths (0..1) used when a template's levels are blended into the notes (quantizer, chopper).
struct LevelStrengths
{
    float pan = 1.0f, velocity = 1.0f, release = 1.0f, modX = 1.0f, modY = 1.0f, pitch = 1.0f;
};

// --- quantizer --------------------------------------------------------------------------------
struct QuantizeOptions
{
    enum DurationMode { leaveDuration, leaveEnd, quantizeDuration, quantizeEnd };
    float start = 0.5f;        // start time strength 0..1 (FL default 64/128)
    float duration = 0.5f;     // duration strength 0..1
    int sensitivity = 36;      // FL wheel 12..192: the search window is sensitivity/48 steps
    int durationMode = leaveDuration;
    LevelStrengths levels;     // FL default: 100 %
};

// Moves the notes toward the template's repeated note starts (see REA_rodada83 §1). Notes whose velocity
// becomes 0 are removed. Returns the indices of the notes that remain (re-sorted pattern).
void quantize (Pattern& pattern, const std::vector<int>& indices, const Score& groove, const QuantizeOptions& options);

// --- chopper ----------------------------------------------------------------------------------
struct ChopOptions
{
    int timeMul = 4;           // FL wheel 1..7: x1/8, 1/4, 1/2, 1, 2, 3, 4
    bool absolute = true;      // the pattern stays on the song's grid (else it restarts at each note)
    LevelStrengths levels;
};

float chopTimeMultiplier (int wheel) noexcept;
// Replaces each note by the pattern repeated over its length (REA_rodada83 §2).
void chop (Pattern& pattern, const std::vector<int>& indices, const Score& choppingPattern, const ChopOptions& options);

// --- flamer -----------------------------------------------------------------------------------
struct FlamOptions
{
    int time = 192;            // FL wheel -192..192: relative = 1/192 step, absolute = 100/192 ms
    int velocity = 85;         // 0..128: the added note's velocity, relative to the original
    bool absolute = false;
    bool before = false;
};

enum class FlamPreset { flam, earlyFlam, doubleStroke, earlyDoubleStroke };
FlamOptions flamPreset (FlamPreset preset) noexcept;
std::int64_t flamOffset (const FlamOptions& options, double bpm) noexcept; // ticks
void flam (Pattern& pattern, const std::vector<int>& indices, const FlamOptions& options, double bpm);

// --- LFO --------------------------------------------------------------------------------------
struct LfoOptions
{
    enum Shape { sine, triangle, square };
    int startValue = 32768, startRange = 16384, startSpeed = 20480; // FL wheels (0..65536, ±65536, 0..40960)
    bool useEnd = false;
    int endValue = 32768, endRange = 16384, endSpeed = 20480;
    bool logSpeed = false;
    int shape = sine;
    int phase = 32768;         // 0..65535
};

// Period in ticks for a speed wheel value (beat x 2^(7 - speed/4096)).
double lfoPeriod (int speed) noexcept;
// Values 0..1 for each tick of a span of `length` ticks (length >= 1).
std::vector<float> lfoCurve (const LfoOptions& options, std::int64_t length);
// Sets `level` of each note to the LFO value at its start, over the span of the notes.
void lfo (Pattern& pattern, const std::vector<int>& indices, notetools::Level level, const LfoOptions& options);

// --- claw machine -----------------------------------------------------------------------------
struct ClawOptions
{
    int period = 0;            // 0..4: 1, 2, 4, 8, 16 beats
    int trash = 3;             // 0..9: 1/4, 2/4, 3/4, 4/4, 1/3, 2/3, 3/3, 1/2, 2/2, none
    int timeDistortion = 0;    // -1024..1024
    bool stretch = true;       // stretch to compensate for the removed time
    bool removeShort = true;   // remove notes that become shorter than the minimum
};

const std::vector<const char*>& clawTrashNames(); // "1/4" ... "(none)"
// Removes a slice of every period and bends time inside the period (REA_rodada83 §5).
void claw (Pattern& pattern, const std::vector<int>& indices, const ClawOptions& options);

// --- articulator ------------------------------------------------------------------------------
struct ArticulateOptions
{
    int multiply = 100;        // 10..100 %
    int variation = 0;         // 0..100 %
    int gap = 0;               // 0..240, in 1/960 beat
    bool useLengths = false;   // scale the notes' own lengths instead of the room to the next note
    bool chopChords = false;   // cut held notes where a later note starts
    std::uint32_t seed = 0;
};

enum class ArticulatePreset { legato, portato, staccato, smallGaps, chopChordsOnly };
ArticulateOptions articulatePreset (ArticulatePreset preset) noexcept;
// Returns the indices of the processed notes after the change (chop chords adds notes).
void articulate (Pattern& pattern, const std::vector<int>& indices, const ArticulateOptions& options, Meter meter);

// --- chord progression (LYNX's own generator) ------------------------------------------------
struct Chord
{
    int root = 0;              // pitch class 0..11 of the chord root
    std::vector<int> intervals; // semitones above the root (0 first)
    int bass = -1;             // pitch class of a different bass note (slash chord), -1 = none

    bool operator== (const Chord&) const = default;
};

// Parses "I-V-vi-IV", "ii7 V7 I∆", "IV/♭VII", "Isus2 add9"... in the key of `tonic` (pitch class) and `minor`.
// Returns nothing when a part cannot be read.
std::optional<std::vector<Chord>> parseProgression (const std::string& text, int tonic, bool minor);
std::string chordName (const Chord& chord); // "Am7", "C/E"...
// Roman numerals of the chords in the key of `tonic` ("I-V-vi-IV"), readable by parseProgression.
std::string toNumerals (const std::vector<Chord>& chords, int tonic);

// Chord detection (FL 2026's chord readout, LYNX's own rules; REA round 153): names the chord of a set of MIDI keys
// (up to 10 are read, the rest ignored). Exact interval templates only (triads, sixths, sevenths, sus, add9, ninths,
// power chord); the lowest key is preferred as the root, otherwise the chord is written over its bass ("Am7/C").
// Returns nothing for fewer than two pitch classes or a set no template fits.
struct DetectedChord
{
    int root = 0;      // pitch class
    int bass = 0;      // pitch class of the lowest key
    std::string name;  // "C", "F#m7", "Bbmaj7"... ("Am7/C" when the bass is not the root)
};
std::optional<DetectedChord> detectChord (std::vector<int> keys, bool preferFlats = false);

struct ProgressionPreset
{
    std::string name;          // "I-V-vi-IV: Hopeful"
    std::string numerals;      // "I-V-vi-IV"
    bool minor = false;
};

// The installation's presets (FL's Chords/presets.cpt text) or, without it, a few common progressions.
std::vector<ProgressionPreset> parseProgressionPresets (const std::string& text);
const std::vector<ProgressionPreset>& builtInProgressions();

// Generates `count` chords in the key: common diatonic moves, more unusual ones as `adventure` (0..1) grows.
std::vector<Chord> generateProgression (int tonic, bool minor, int count, float adventure, std::uint32_t seed);

struct ChordWriteOptions
{
    std::int64_t chordLength = 4 * ppq; // one bar in 4/4
    int octave = 4;            // octave of the chord roots (C4 = 60)
    bool bassNote = true;      // add the root (or the slash bass) an octave below
    bool voiceLead = true;     // inversions that keep the chords close to the first one
    std::string rhythm;        // FL rhythm cells for 4 x chord length: '>' hit, '-' hold, '`' rest; empty = held
    float velocity = 100.0f / 128.0f;
};

// Writes the chords as notes of `channel` from `start`; returns the indices of the new notes.
std::vector<int> writeProgression (Pattern& pattern, int channel, std::int64_t start, const std::vector<Chord>& chords,
                                   const ChordWriteOptions& options);

struct RhythmPreset
{
    std::string name;
    std::string cells;         // e.g. ">---" x 4
};
std::vector<RhythmPreset> parseRhythmPresets (const std::string& text); // FL's rhythm_presets.cpt (CSV)

// --- riff machine -----------------------------------------------------------------------------
struct RiffOptions
{
    int bars = 4;              // 1..64
    int channel = 0;
    std::int64_t start = 0;
    bool onExistingNotes = false;
    int stepsUpTo = 7;         // preview up to this step (0..7)

    // 0 Prog (chop), 1 Chord (chop), 2 Arp, 3 Levels, 4 Mirror, 5 Articulate, 6 Groove (quantize), 7 Fit (limit)
    bool enabled[8] { true, true, false, false, false, true, false, true };
    Score progression, chord;  // chopping patterns of steps 0 and 1 (empty = built-in)
    ChopOptions progressionChop, chordChop;
    notetools::ArpeggiateOptions arp;
    notetools::RandomiseOptions levels;
    bool flipTime = false, flipPitch = true;
    ArticulateOptions articulation;
    Score groove;              // empty = 1/16 grid
    QuantizeOptions grooveOptions;
    notetools::LimitOptions fit;
    int fitRoot = 0, fitScale = 1; // snap to scale (notetools::scales index; 0 = none)
};

// Built-in chopping patterns for the first two steps (`variant` picks one): rhythm + melody, and chord shapes.
Score builtInRiffProgression (int variant);
Score builtInRiffChord (int variant);
int builtInRiffProgressionCount() noexcept;
int builtInRiffChordCount() noexcept;

// Runs the chain. Without `onExistingNotes`, the channel's notes in `indices` are replaced by one note of `bars`
// bars at key 48 before the steps run. Returns the indices of the riff's notes.
std::vector<int> riffMachine (Pattern& pattern, const std::vector<int>& indices, const RiffOptions& options, Meter meter);
} // namespace lynx::prtools
