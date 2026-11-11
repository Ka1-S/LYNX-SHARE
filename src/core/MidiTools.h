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
#include <utility>
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
    // -1..1: every note's length moves by a random amount of up to |variation| half grid steps, either way [LIVE round 190aa:
    // at +-50 % the 14 measured notes moved by -0.052..+0.061 beats on a 1/16 grid, both ways for either sign; the sign's
    // own effect is not proven]
    float variation = 0.0f;
    std::uint32_t seed = 1;
};
// Legato: every note ends where the next later note starts (the last ones keep their length) [LIVE]. Staccato: every
// note gets half the smallest distance between two start times [LIVE]. Tenuto: lengths stay.
Notes span (Notes notes, const SpanOptions& options, std::int64_t grid);

// --- Strum -------------------------------------------------------------------------------------------------------
struct StrumOptions
{
    float low = 0.0f;     // -1..1: the lowest note of each chord moves by this much of a grid step, the highest stays [LIVE]
    float high = 0.0f;    // -1..1: the same from the highest note, the lowest stays [LIVE]; both add up [LIVE round 190aa]
    float tension = 0.0f; // -1..1: bends the spacing [LIVE: a cubic Bézier, strumCurve()]
};
// The share of the full offset a chord note gets at `position` (0 = the note that stays … 1 = the note that moves the
// whole way) [LIVE: ten measured points within 2e-6]: the cubic Bézier from (0,0) to (1,1) whose control points are the
// diagonal's quarter points pulled towards the corner (0,1) by `tension` (towards (1,0) when it is negative).
double strumCurve (double position, double tension) noexcept;
// A note `u` of the way from the chord's highest note to its lowest moves by low * strumCurve (u) + high * strumCurve (1 - u)
// grid steps: the curve always runs from the note that stays [LIVE round 190aa: High 100 % with Tension 50 % gave the
// middle note the share 0.829 that Low 100 % gives it]. Notes keep their length (they move) [LIVE].
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
// The notes are held keys of an arpeggiator: one note per `rate` while any is held, the pattern starting again whenever
// the set of held notes changes (a key added or released) [LIVE round 190aa, overlapping notes]. All 18 styles are
// Live's, note for note on a 3-note chord and single notes [LIVE round 190aa]; Random picks any note of any pass,
// Random Other a new order every pass, Random Once one order for all passes (distributions, not exact notes). Each new
// note takes the velocity of the note it comes from.
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
// Fills the silence between one chord's end and the next chord's start with notes of `rate`, their pitch and velocity
// stepping evenly from each note to the next one (pitch rounded half up) [LIVE]. Every voice of a chord draws its own
// line, voice i (low first) to voice min (i, n - 1) of the next chord, and equal notes at one time are kept once [LIVE
// round 190aa: a C major triad to one C gave 3, 3, 3, 2 notes]. A tied note lasts until the next chord and has the
// pitch of the last step it covers and the velocity of its first [LIVE round 190aa, one tie measured]. Density, Spread
// and Tie are random in Live (ranges checked, not notes).
Notes connect (Notes notes, const ConnectOptions& options, std::int64_t grid);

// --- Chop --------------------------------------------------------------------------------------------------------
struct ChopOptions
{
    int parts = 4;  // 2..64 parts per note [LIVE default 4]
    int gaps = 0;   // > 0: a gap after every `gaps` parts; < 0: `-gaps` gaps after every part [LIVE round 190aa: 2, -1]
    // Live's pattern row (round 190aa): one toggle per part, at most 16, repeated over more parts [MAN]. Gaps writes it;
    // a toggle clicked by hand makes it `custom` (Live's Gaps then shows "-") until Gaps is turned again.
    bool custom = false;
    std::uint16_t pattern = 0xFFFF; // bit i: part i plays (when custom) [LIVE: part 2 off → 1 0 1 1]
    std::uint16_t emphasis = 0;     // bit i: part i (a note or a gap) lasts `stretch` parts [LIVE: 2 1 1 1 / 7ths at x4]
    int stretch = 2;                // 2..8 [LIVE default x2]
    float variation = 0.0f;         // 0..1: each part's share is scaled by a random 1 +- variation, the same for every note
                                    // [LIVE round 190aa at 50 %: shares 0.61 0.08 0.13 0.17 of 0.57 0.14 0.14 0.14; the
                                    // law of the draw INFERRED]
    std::uint32_t seed = 1;
};
// Does part `part` play? (the pattern from Gaps, or the hand-made one)
bool chopPlays (const ChopOptions& options, int part) noexcept;
// The pattern Gaps makes, as `pattern` bits (for the panel's row).
std::uint16_t chopPattern (int parts, int gaps) noexcept;
Notes chop (Notes notes, const ChopOptions& options);

// --- Time Warp ---------------------------------------------------------------------------------------------------
struct TimeWarpOptions
{
    struct Breakpoint
    {
        bool on;
        double time;  // 0..1 of the range
        double speed; // x0.25 .. x4 [INFERRED range]: > 1 the notes come closer together there
    };
    // [LIVE] defaults: 1 and 3 on at the range's ends, 2 off in the middle, all x1.0
    Breakpoint points[3] { { true, 0.0, 1.0 }, { false, 0.5, 1.0 }, { true, 1.0, 1.0 } };
    bool quantize = false;   // the warped starts go to the nearest grid line, lengths stay [LIVE]
    bool fit = true;         // Range "Fit": the notes keep their first start and last end [LIVE]
    bool includeEnd = true;  // Note End "Include": the ends are warped too (lengths change) [LIVE]
    // the span the breakpoints' times refer to (Live: the clip / time selection); rangeEnd <= rangeStart = the whole
    // bars the notes touch
    std::int64_t rangeStart = 0, rangeEnd = 0;
};
// [LIVE round 190aa, 7 settings within 1e-5 beats] A note at time t moves to first + c * integral from the first start
// to t of r(x), where r is 1 / speed interpolated linearly between the breakpoints that are on (flat beyond them) and
// c makes the last end stay where it was (Fit; else c = 1). With Note End off the lengths stay and Live's c differs a
// little (0.2 %, up to 7 ticks on the measured clip; LYNX keeps Include's c).
// The span the breakpoints refer to for these notes (the range given, else the whole bars they touch).
std::pair<std::int64_t, std::int64_t> timeWarpRange (const Notes& notes, const TimeWarpOptions& options);
Notes timeWarp (Notes notes, const TimeWarpOptions& options, std::int64_t grid);

// --- Velocity Shaper -----------------------------------------------------------------------------------------------
struct VelocityShaperOptions
{
    struct Point
    {
        double x, y;  // 0..1 of the cycle, 0..1 of minimum..maximum
        float curve;  // the bend of the segment to the next point: 0 straight, > 0 fast first (exponential)
    };
    // [LIVE, fitted on 96 measured notes] the default: up the left edge to the top, an exponential down to the floor at
    // 0.503, then flat (Live draws the bottom-left corner as a point too). Points at one x make a jump: the later wins.
    std::vector<Point> points { { 0.0, 0.0, 0.0f }, { 0.0, 1.0, 2.825f }, { 0.503, 0.0, 0.0f }, { 1.0, 0.0, 0.0f } };
    int minimum = 1, maximum = 127; // [LIVE defaults]
    int loops = 1;                  // cycles over the notes' span [LIVE]
    int rotate = 0;                 // the envelope starts this many divisions later (wrapping) [LIVE]
    enum Division { sixteenth, eighth, grid };
    int division = sixteenth;       // [LIVE: 1/16 = a quarter beat, 1/8 = half a beat, Grid = the editor's grid]
};
// The envelope's value 0..1 at `x` (0..1 of a cycle).
double velocityShape (const VelocityShaperOptions& options, double x) noexcept;
// [LIVE round 190aa, 6 settings note for note] Every note's velocity becomes floor (minimum + (maximum - minimum) *
// shape (x)), x = its place in the span from the first start to the last end (times `loops`, shifted by `rotate`),
// whatever its old velocity.
Notes velocityShaper (Notes notes, const VelocityShaperOptions& options, std::int64_t grid);

// --- Quantize ---------------------------------------------------------------------------------------------------
struct QuantizeOptions
{
    std::int64_t step = 0;  // 0 = the editor's grid [LIVE default "Grid"; the panel offers 1/4, 1/8, 1/16, 1/32]
    bool triplets = false;  // two thirds of the step [LIVE round 190aa: 1/16 triplets = a sixth of a beat]
    bool straightToo = false; // with triplets: the straight lines count too ("1/8 + 1/8T" in Live's panel) [LIVE round 190aa]
    bool start = true;      // Adjust Note Start: the note moves, keeping its length [LIVE round 190aa]
    bool end = false;       // Adjust Note End: the note's end moves (its length changes) [LIVE round 190aa]
    float amount = 1.0f;    // 0..1 of the way to the nearest line [LIVE default 100 %]
};
// [LIVE round 190aa] all seven measured settings note for note: Grid, End, Start + End, 50 %, 1/4, 1/16T, 1/8 + 1/8T.
Notes quantize (Notes notes, const QuantizeOptions& options, std::int64_t grid);

// --- on a pattern ------------------------------------------------------------------------------------------------
// Replaces the notes at `indices` by `result` (a tool's output for exactly those notes) and returns the new notes'
// indices after sorting, for the selection.
std::vector<int> replaceNotes (Pattern& pattern, const std::vector<int>& indices, const Notes& result);
// The notes at `indices`, in pattern order.
Notes notesAt (const Pattern& pattern, const std::vector<int>& indices);
} // namespace lynx::miditools
