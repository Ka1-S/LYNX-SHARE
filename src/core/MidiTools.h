#pragma once

// Ableton Live 12's MIDI Transformations (round 190o, GAPMAP #8 "MT"), LYNX additions beside FL's Piano roll tools
// (core/NoteTools.h, core/PianoRollTools.h stay as they are). Each tool takes the selected notes and returns what
// replaces them, so a panel can preview from the untouched originals every time a control moves (Live's Auto Apply).
//
// Behaviour proven on a set saved by Live 12.4.16 (/mnt/e/lynx-work/ableton190o/live/mt-saved Project/mt-saved.als,
// notes in docs/analise/REA_rodada190o.md); tests/MidiTools190oTests.inc holds Live's output note for note. Tags:
//   [LIVE]     seen in that save            [MAN] Live 12 manual, "MIDI Tools" (not measured)
//   [INFERRED] LYNX's reading where neither says
// `grid` is the editor's grid in ticks (Live's percentages are of one grid step). Random parts draw from `seed`, so a
// preview and its Apply give the same notes. No JUCE.

#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::miditools
{
using Notes = std::vector<Note>;

// The notes grouped by start time (and channel), each group low key first, groups in time order: Live's "chords".
std::vector<Notes> chords (Notes notes);

// --- Span --------------------------------------------------------------------------------------------------------
struct SpanOptions
{
    enum Mode { legato, tenuto, staccato };
    int mode = tenuto;       // [LIVE] the default
    float offset = 0.0f;     // -1..1 of one grid step added to each note's end [MAN]
    float variation = 0.0f;  // -1..1: > 0 shortens, < 0 lengthens, at random per note [MAN; the amount per note INFERRED]
    std::uint32_t seed = 1;
};
// Legato: every note ends where the next later note starts (the last ones keep their length) [LIVE]. Staccato: every
// note gets half the smallest distance between two start times [LIVE]. Tenuto: lengths stay.
Notes span (Notes notes, const SpanOptions& options, std::int64_t grid);

// --- Strum -------------------------------------------------------------------------------------------------------
struct StrumOptions
{
    float low = 0.0f;     // -1..1: the lowest note of each chord moves by this much of a grid step, the highest stays [LIVE]
    float high = 0.0f;    // -1..1: the same from the highest note [LIVE]
    float tension = 0.0f; // -1..1: bends the spacing [LIVE: a cubic Bézier, strumCurve()]
};
// The share of the full offset a chord note gets at `position` (0 = the note that stays … 1 = the note that moves the
// whole way) [LIVE: ten measured points within 2e-6]: the cubic Bézier from (0,0) to (1,1) whose control points are the
// diagonal's quarter points pulled towards the corner (0,1) by `tension` (towards (1,0) when it is negative).
double strumCurve (double position, double tension) noexcept;
// Notes keep their length (they move) [LIVE].
Notes strum (Notes notes, const StrumOptions& options, std::int64_t grid);

// --- Ornament ----------------------------------------------------------------------------------------------------
struct OrnamentOptions
{
    bool flam = true;            // [LIVE] defaults: Flam on, -20 %, 50 %
    float flamPosition = -0.2f;  // -1..1 of a grid step: < 0 the flam comes before the note, > 0 it takes the note's start
    float flamVelocity = 0.5f;   // of the note's velocity (and of its release velocity)
    bool grace = false;
    enum Pitch { low = -1, same = 0, high = 1 };
    int gracePitch = same;       // high / low: every other grace note (the first one included) a step above / below
    float gracePosition = -0.2f;
    float graceVelocity = 0.5f;
    float graceChance = 1.0f;    // of the note's own chance [MAN]
    int graceAmount = 3;         // equal notes sharing the |position| span
    // the step of a High / Low grace note: one semitone, or one scale degree when a scale is given (scale > 0 =
    // notetools::scales() index) [MAN]
    int scaleRoot = 0, scale = 0;
};
// [LIVE] the ornament notes of a negative position end at the note's start and the note is untouched; with a positive
// position they start where the note started and the note begins after them, keeping its end. Ornaments that would
// begin before tick 0 are left out (Live keeps them at negative times; a LYNX pattern starts at 0).
Notes ornament (Notes notes, const OrnamentOptions& options, std::int64_t grid);

// --- Recombine ---------------------------------------------------------------------------------------------------
struct RecombineOptions
{
    enum Dimension { position, pitch, duration, velocity };
    int dimension = position;
    bool shuffle = false;       // a random permutation (from `seed`)
    bool mirror = false;        // the values in reverse order
    int rotate = 0;             // > 0: each takes the value of the one `rotate` places later [LIVE]
    bool rotateOnGrid = false;  // Position only: rotate by grid cells within the notes' time range [MAN; INFERRED]
    std::uint32_t seed = 1;
};
// Position permutes the chords' start times (a chord moves as one) [LIVE]; the other dimensions permute the notes'
// values in time order [INFERRED]. Order: shuffle, mirror, rotate [MAN].
Notes recombine (Notes notes, const RecombineOptions& options, std::int64_t grid);

// --- Arpeggiate --------------------------------------------------------------------------------------------------
struct ArpeggiateOptions
{
    // Live's 18 styles, in its menu order [BIN: the style names]
    enum Style { up, down, upDown, downUp, upAndDown, downAndUp, converge, diverge, conAndDiverge, pinkyUp, pinkyUpDown,
                 thumbUp, thumbUpDown, playOrder, chordTrigger, random, randomOther, randomOnce, styleCount };
    int style = up;
    int distance = 12;                 // semitones each transposed pass adds [LIVE default +12 st]
    int steps = 2;                     // transposed passes after the plain one [LIVE default 2]
    std::int64_t rate = ppq / 8;       // [LIVE default 1/32]
    float gate = 1.0f;                 // note length as a share of the rate [LIVE default 100 %]
    std::uint32_t seed = 1;
};
const char* arpeggiateStyleName (int style) noexcept; // English, for tr()
// The notes are held keys of an arpeggiator: from each start where no note was sounding, one note per `rate` while any
// is held, the pattern restarting there [LIVE: Up, +12 st, 2 steps, 1/32, 100 %; the other styles follow Live's
// Arpeggiator device as the manual describes it]. Each new note takes the velocity of the note it comes from.
Notes arpeggiate (Notes notes, const ArpeggiateOptions& options);

// --- Connect -----------------------------------------------------------------------------------------------------
struct ConnectOptions
{
    int spread = 0;          // semitones a connecting note may stray, at random [MAN]
    float density = 1.0f;    // share of each gap's steps that get a note [LIVE at 100 %; below it the choice is random]
    std::int64_t rate = 0;   // length of the new notes; 0 = the grid [LIVE default "Grid"]
    float tie = 0.0f;        // chance that a new note lasts until the next note [MAN]
    std::uint32_t seed = 1;
};
// Fills the silence between one note's end and the next start with notes of `rate`, their pitch and velocity stepping
// evenly from the first note's to the next one's (pitch rounded half up) [LIVE]. Of a chord, the lowest note counts
// [INFERRED].
Notes connect (Notes notes, const ConnectOptions& options, std::int64_t grid);

// --- Chop --------------------------------------------------------------------------------------------------------
struct ChopOptions
{
    int parts = 4;  // 2..64 equal parts per note [LIVE default 4]
    int gaps = 0;   // > 0: a gap after every `gaps` parts; < 0: `-gaps` gaps after every part [MAN]
};
Notes chop (Notes notes, const ChopOptions& options);

// --- on a pattern ------------------------------------------------------------------------------------------------
// Replaces the notes at `indices` by `result` (a tool's output for exactly those notes) and returns the new notes'
// indices after sorting, for the selection.
std::vector<int> replaceNotes (Pattern& pattern, const std::vector<int>& indices, const Notes& result);
// The notes at `indices`, in pattern order.
Notes notesAt (const Pattern& pattern, const std::vector<int>& indices);
} // namespace lynx::miditools
