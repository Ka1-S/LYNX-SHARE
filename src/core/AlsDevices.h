#pragma once

// Live's own devices in a set (.als) → LYNX's own instruments and effects, with their settings (round 190bq; the
// "N3 table" of docs/analise/ALS_FORMAT.md §8). Clean-room: only the device's saved state is read (member names,
// values and each parameter's own range, `MidiControllerRange`, as the sets on this machine hold them); the LYNX
// engines have their own names and DSP. Every conversion is a law in a unit (Hz, dB, ms, ratio, Q), written here
// so the tests can pin it; what has no counterpart is returned as a note for the import report, never dropped
// silently. Evidence tags: [DATA] read in saved sets, [DOC] Ableton's manual, [ASSUMED] not confirmed in Live (the
// handoff lists each one for an A/B in Live).
//
//   Simpler (OriginalSimpler)      → the LYNX Sampler channel (what the zones / envelope import did not read)
//   EQ Eight (Eq8)                 → Facet EQ
//   Compressor (Compressor2)       → Crest Compressor, Clean style
//   Glue Compressor                → Crest Compressor, Bus style
//   Utility (StereoGain)           → Utility
//   Reverb                         → Atrium
// Lot 2 (round 190cf), by count in the sets Live ships:
//   Delay, Echo                    → Echoform (Clean mode)
//   Multiband Dynamics             → Prism Multiband (3 bands)
//   Auto Filter (both generations) → Filter
//   Saturator                      → Ember Saturator
//   Overdrive                      → Distortion
//   Limiter                        → Apex Limiter
//   Gate                           → Sentry Gate
//   Compressor in Expand mode      → Sentry Gate, Upward style
//   Channel EQ                     → EQ (3 bands)
//   Chorus-Ensemble, Chorus        → Chorus
//   Auto Pan (as a panner)         → Auto-pan
//   Redux                          → Lo-fi
//   Arpeggiator                    → the channel arpeggiator
//   Pitch, Velocity, Chord, Note Length → written into the notes of the track's clips
// and, for every translated device, which Live parameter drives which LYNX parameter (automation, rack macros).
// Lot 3 (round 190ed):
//   EQ Three                       → Prism Multiband (3 bands, crossover sum, no dynamics)
//   Drum Buss                      → an Audio Effect Rack "Drum Buss": Ember Saturator → Facet EQ → (Crest Compressor) →
//                                    Shell Channel (+ a dry chain); refit to Live's renders in round 190ek
//   Auto Filter's LFO / envelope follower / side chain → a Peak Controller linked to the Filter's cutoff
//   Operator (no FM in use), Drift → the built-in synth

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx
{
namespace xml
{
class Element;
}

namespace alsdev
{
// What the import report shows for one device, besides the device itself
struct Notes
{
    std::vector<std::string> approximated; // kept, but not the same law / range (clamped, fitted, nearest choice)
    std::vector<std::string> dropped;      // a setting with no LYNX counterpart (the device itself is kept)
};

// ---- shared laws ------------------------------------------------------------------------------------------------
double dbOfGain (double linear) noexcept;       // 20·log10; −200 dB at or below 0
double gainOfDb (double db) noexcept;

// ---- EQ Eight → Facet EQ ----------------------------------------------------------------------------------------
// The band's filter type, in the order Live stores it [DATA: Mode 0…7; the defaults of bands 1…8 and the manual's list]
enum class Eq8Mode { lowCut48, lowCut12, lowShelf, bell, notch, highShelf, highCut12, highCut48 };
// Facet EQ's Q for a Live Q, per filter family. Facet's prototypes (docs/analise/MEASURE_fabfilter.md §1) against
// Live 12.4's EQ Eight as rendered in round 190cw (noise through one band at 1 kHz, transfer function vs the dry track):
//   bell / notch   Qb = Qfacet / √2 in the constant-Q biquad; Live = the textbook bell (0.03 … 0.08 dB) → Qfacet = √2 · Q
//   12 dB cuts     section Q = 0.7071 · Qfacet; Live = the textbook 2nd order (0.08 dB)            → Qfacet = √2 · Q
//   shelves        Qs = 0.7071 · Qfacet^0.4697; Live's Q acts as a fixed cookbook Q, the same for both shelves and
//                  every gain [MEASURED 190cz, 13 points Q 0.1 … 18, fit 0.000 dB]: Qs = eq8ShelfCookbookQ (Q),
//                  Qfacet = (Qs / 0.7071)^(1 / 0.4697)
//   48 dB cuts     Live = an 8th-order Butterworth at Q 0.71 with all four section Qs scaled by
//                  eq8CutSectionScale (Q) [MEASURED 190cz at Q 0.1 / 0.71 / 3 / 10, low and high cut: exact; 190cw
//                  Q 0.5 / 1]; Facet holds it with its cut law "all sections" (facet::cutLawParam), Qfacet = that scale
// (clamped to Facet's 0.025 … 40)
double eq8CutSectionScale (double liveQ) noexcept;  // 1 + 0.2632·ln(Q / 0.7071), at least 0.05
// ln Qs = −0.000248 L⁵ + 0.002322 L⁴ − 0.004732 L³ − 0.036516 L² + 0.410151 L − 0.200340, L = ln Q (Q held to Live's
// 0.1 … 18): 0.1 → 0.301, 0.71 → 0.708, 2 → 1.068, 10 → 1.720, 18 → 1.969 (within 0.05 % of the 13 measured points)
double eq8ShelfCookbookQ (double liveQ) noexcept;
// Adaptive Q [MEASURED 190cz, 18 renders: exact]: the band's Q (a shelf's: its cookbook Q above) × factor^(|gain| − 6),
// gain in dB after Scale, factor = the device's AdaptiveQFactor (1.12 in every saved set): wider under 6 dB, the set Q
// at 6 dB, narrower above; bells always, shelves when AdaptiveQAffectsShelves (true in 178 of the 181 shipped devices)
double eq8AdaptiveQ (double q, double gainDb, double factor) noexcept;
// `adaptive`: the Adaptive Q multiplier of the band (eq8AdaptiveQ (1, gain, factor), 1 = none) — on a bell's Q, on a
// shelf's cookbook Q
double facetQ (Eq8Mode mode, double liveQ, double adaptive = 1.0) noexcept;
struct Eq8Band
{
    bool on = true;
    Eq8Mode mode = Eq8Mode::bell;
    double hz = 1000.0, gainDb = 0.0, q = 0.7071;
    double adaptive = 1.0; // the Adaptive Q multiplier (see facetQ)
};
// Writes one band into a Facet EQ parameter set; `gainSign` −1 folds a negative Scale into the gains
void setFacetBand (EffectParams& params, int facetBand, const Eq8Band& band, double gainSign, int placement);
// The device → a Facet EQ slot (false: the element holds no EQ Eight band data)
bool eqEightToFacet (const xml::Element& device, EffectSlot& slot, Notes& notes);

// ---- Compressor / Glue Compressor → Crest Compressor ------------------------------------------------------------
// Glue's stepped controls [DOC]: attack 0.01 / 0.1 / 0.3 / 1 / 3 / 10 / 30 ms, release 0.1 / 0.2 / 0.4 / 0.6 / 0.8 /
// 1.2 s and Auto, ratio 2 / 4 / 10 [DATA: the indices' ranges 0…6, 0…6, 0…2]
double glueAttackMs (int index) noexcept;
double glueReleaseMs (int index) noexcept; // 0 for Auto (index 6)
double glueRatio (int index) noexcept;
// Compressor's look-ahead choices [DOC]: 0, 1, 10 ms
double compressorLookaheadMs (int index) noexcept;
// A side-chain input the device names: the set's track id (−1 = none or not a track), and whether Live taps it
// somewhere LYNX cannot (LYNX's side chain is the insert's post-fader output)
struct SideInput
{
    bool on = false;
    int trackId = -1;
    bool otherTap = false;
};
// (exposed for lot 2: Gate, Multiband Dynamics and the Expand mode read the same routing)
SideInput sideInputOf (const xml::Element& device);
bool compressorToCrest (const xml::Element& device, EffectSlot& slot, Notes& notes, SideInput& side);
bool glueToCrest (const xml::Element& device, EffectSlot& slot, Notes& notes, SideInput& side);
// Glue's Range [DATA: 0 … 70 dB] as Crest's range (0 … 60 dB, its most) [MEASURED 190cz, ratio 2 / 4 / 10 with 20 dB
// more input]: up to 40 the gain reduction stops hard at 1.1727·Range − 2.197 dB (10 → 9.53, 20 → 21.20, 30 → 33.0,
// 40 → 44.71); Range 50 is within 0.3 … 1.4 dB of no limit and 60 … 70 are no limit up to 78.6 dB of gain
// reduction: from 50 up, Crest's 60 dB
double glueRangeDb (double liveRange) noexcept;

// ---- Utility → Utility ------------------------------------------------------------------------------------------
struct LiveUtility
{
    double gain = 1.0;        // linear; 0 = −inf dB [DATA: 0 … 56.23 = +35 dB]
    bool mute = false, phaseL = false, phaseR = false, mono = false, bassMono = false, dcFilter = false;
    int channelMode = 1;      // 0 Left, 1 Stereo, 2 Right, 3 Swap [DATA: 0…3; order seen in Live 12.4, round 190cw]
    double width = 1.0;       // 0 … 4 (0 … 400 %)
    bool midSideOn = false;   // the Width control shows Mid/Side balance instead
    double midSide = 1.0;     // 0 mid only … 1 neutral … 2 side only
    double balance = 0.0;     // −1 … 1
    bool operator== (const LiveUtility&) const = default;
};
// out L = ll·L + rl·R, out R = lr·L + rr·R (before gain; the balance included)
struct StereoMatrix
{
    double ll = 1.0, rl = 0.0, lr = 0.0, rr = 1.0;
};
// Live's balance (−1 … 1) as left / right gains: √2·cos((b + 1)·π/4), √2·sin((b + 1)·π/4) — constant power with a
// boost (+0.5 → L × 0.541, R × 1.307; measured in Live 12.4, round 190cw r15)
std::pair<double, double> utilityBalanceGains (double balance) noexcept;
// Phase switches, then channel mode, then Mono, Width or the mid/side balance (M = (L + R) / 2, S = (L − R) / 2; out =
// M ± width·S; mid/side balance m: M·min(1, 2 − m) ± S·min(1, m)), then the balance gains on the outputs. All measured
// (rounds 190cw r13–r17, 190cz u00–u13: balance after width / mono / mid-side, exact)
StereoMatrix utilityMatrix (const LiveUtility& u) noexcept;
// True when LYNX's Utility does exactly what these settings say: plain gain within ±24 dB or mute, phase switches,
// channel mode, Mono, any width, mid/side balance and balance whose matrix fits LYNX's ±2 coefficients, no Bass Mono /
// DC filter
bool utilityIsExact (const LiveUtility& u) noexcept;
LiveUtility readUtility (const xml::Element& device);
bool utilityToUtility (const xml::Element& device, EffectSlot& slot, Notes& notes);
// The other way, for the export (decision D: only what is exact): a LYNX Utility slot as Live's settings, nullopt
// when it holds anything Live's device cannot say exactly (a balance, a free matrix)
std::optional<LiveUtility> liveUtilityOf (const EffectSlot& slot) noexcept;

// ---- Reverb → Atrium (decay and high damping measured in round 190cz; the rest fitted by reading) --------------
// Live's Decay (s) as Atrium's: 0.94 × [MEASURED: Live's 1 kHz T30 = 0.91 × Decay; fitted with the High mult below]
double reverbAtriumDecaySec (double liveDecaySec) noexcept;
// Live's high shelf in the loop (gain g, on) as Atrium's High mult: 1 / (1 + 0.08 · L · Decay), L = −20·log10 g dB
// lost per pass [MEASURED at g 0.7: T30 at 4 / 8 kHz within 0.1 s at Decay 1, 2.5 and 5 s; the law in g ASSUMED]
double reverbAtriumHighMult (double shelfHiGain, double liveDecaySec) noexcept;
// (Atrium's fully wet tail is louder than Live's: +12.9 / +7.9 / +4.5 dB at Decay 1 / 2.5 / 5 s [MEASURED]; Atrium has no
// wet level of its own, so it is listed)
// Live's room size (0.22 … 500, 100 by default) on Atrium's Late size (0 … 1, 0.5 by default): log on each side
float atriumSizeOf (double liveRoomSize) noexcept;
// Reflect and Diffuse levels (linear gains) as Atrium's Early / late (equal power: 0 early only … 1 late only)
float atriumEarlyLateOf (double reflect, double diffuse) noexcept;
// The input filter (centre Hz, width in octaves [ASSUMED]) as the edges Atrium's Low cut / High cut take
double reverbInputLowHz (double centreHz, double widthOct) noexcept;
double reverbInputHighHz (double centreHz, double widthOct) noexcept;
bool reverbToAtrium (const xml::Element& device, EffectSlot& slot, Notes& notes);

// ================================================================================================ lot 2 (190cf)
// What a translation may need from the set
struct Context
{
    double bpm = 120.0; // the set's tempo: a synced time LYNX can only hold in ms is fixed at this tempo (and reported)
    bool drumBussRack = false; // (round 190fk) true: Drum Buss as round 190ek's rack (the A/B harness, its tests); else Anvil Bus
};
// One Live parameter that drives one LYNX parameter: `live` is the parameter's element path inside the device
// ("Feedback", "Lfo/Frequency"), `value` turns Live's own unit into LYNX's 0..1. Automation envelopes and rack macro
// mappings on that Live parameter are carried onto `param` through it.
struct ParamLink
{
    std::string live;
    int param = 0;
    std::function<float (double)> value;
};

// ---- shared laws of lot 2 ---------------------------------------------------------------------------------------
// A cutoff stored as a pitch [DATA: the older Auto Filter, 20 … 135] in Hz: 440 · 2^((n − 69) / 12) = 26 Hz … 19.9 kHz,
// the range Ableton's manual gives the control
double hzOfLiveNote (double note) noexcept;
// The synced LFO rates of Auto Filter / Auto Pan (`BeatRate` 0 … 21) in quarter notes [DOC list, DATA: the default 4
// is the manual's default 1/16; order ASSUMED ascending]: 1/64, 1/48, 1/32, 1/24, 1/16, 1/12, 1/8, 1/6, 3/16, 1/4,
// 5/16, 1/3, 3/8, 1/2, 3/4, 1, 1.5, 2, 3, 4, 6, 8 bars
double liveBeatRateBeats (int index) noexcept;
// Delay's synced time buttons (`SyncedSixteenth` 0 … 7) in sixteenths [DOC]: 1, 2, 3, 4, 5, 6, 8, 16
int delaySixteenths (int index) noexcept;
// Echoform's note sync for a length in quarter notes: { kind (1 straight, 2 dotted, 3 triplet), note index 0 … 9 =
// 1/64 … 8/1 }; nullopt when no note value has that length
std::optional<std::pair<int, int>> echoformNoteOf (double beats) noexcept;
float echoformSyncValue (int kind) noexcept;  // 0 ms, 1 note, 2 dotted, 3 triplet
float echoformNoteValue (int index) noexcept; // 0 … 9
// Dry/Wet → Echoform's equal-power Mix. Equal-loudness [ASSUMED: cos / sin, as Echoform's]: the same number. Linear
// (dry 1 − x, wet x): the mix with the same wet : dry ratio, (2 / π) · atan (x / (1 − x)) — up to 3 dB louder at 50 %
float echoformMixOf (double dryWet, bool equalLoudness) noexcept;
// Auto Filter's resonance r as the cookbook Q of its 12 dB stage [MEASURED 190cz, low pass at 1 kHz, 14 + 27 renders,
// ≤ 0.003 dB; 200 Hz / 5 kHz, high and band pass at 0.5 the same]: Q = 0.5 + 2x / (1 − x) with an inner x — the older
// device (`AutoFilter`, up to Live 12.1, LegacyQ 0.82 as in every shipped set) x = min(r, 0.9) (Q 0.5 … 18.5); the new
// one (`AutoFilter2`) x from its measured table (Q 0.5 at r 0, 2.49 at 0.5, 35.8 at 1; r over 1 acts as 1). LYNX's
// Filter takes Q 0.5 … 12: above that it is listed. Its 24 dB/oct slope adds the second stage of Live's pair
// (filterSecondStageQ).
double autoFilterX (double resonance, bool legacy) noexcept;
double filterQOfResonance (double resonance, bool legacy) noexcept;
// LYNX's basic effects keep their laws inside Effects.cpp; the inverses used here, each pinned by a test against
// formatEffectParam: Filter (type 0 low-pass, 1 high-pass, 2 band-pass; cutoff 20 Hz … 20 kHz log; Q 0.5 … 12 log),
// Lo-fi (rate = 44100 / (⌊128 v³⌋ + 1) Hz; 65536 / (2048 v² + 1) levels), EQ (±18 dB; mid 200 Hz … 5 kHz log),
// Chorus (0.05 … 5 Hz log; 0 … 8 ms), Distortion (0 … 36 dB)
float filterTypeValue (int type) noexcept;
float filterCutoffValue (double hz) noexcept;
float filterQValue (double q) noexcept;
float lofiRateValue (double hz) noexcept;
float lofiBitsValue (double bits) noexcept;
float eqGainValue (double db) noexcept;
float eqMidValue (double hz) noexcept;
float chorusRateValue (double hz) noexcept;
// Multiband Dynamics stores each band's two actions as slopes [DATA: Above −1 … 1, Below −3 … 1; the manual's
// ratios]: above the threshold the output rises 1 + a dB per dB (a < 0 compresses, −1 = a limit; a > 0 expands
// upward), below it 1 − b dB per dB (b > 0 compresses upward, 1 = flat; b < 0 expands downward, −3 = 1 : 4).
// One Prism band holds one action: expand off / on, the sign of its range, and the ratio that gives the same slope.
struct PrismAction
{
    bool active = false, expand = false, upward = false, limit = false;
    double ratio = 1.0;
};
PrismAction prismAbove (double slope) noexcept;
PrismAction prismBelow (double slope) noexcept;
// Prism's attack / release are detector time constants: 0.11 + 89 v^3.9 ms and 130 + 3500 v³ ms
float prismAttackValue (double ms) noexcept;
float prismReleaseValue (double ms) noexcept;
// Sentry Gate's times: attack 1000 v⁴ ms, hold 250 v² ms, release 5000 v³ ms
float sentryAttackValue (double ms) noexcept;
float sentryHoldValue (double ms) noexcept;
float sentryReleaseValue (double ms) noexcept;
// Limiter's look-ahead choices [DOC]: 1.5, 3, 6 ms; Gate's [DOC]: 0, 1.5, 10 ms
double limiterLookaheadMs (int index) noexcept;
double gateLookaheadMs (int index) noexcept;
// Arpeggiator's synced rates (`SyncedRate` 0 … 13) in quarter notes [DOC list; DATA: 8 in a new device = the
// manual's default 1/8]: 1/128, 1/96, 1/64, 1/48, 1/32, 1/24, 1/16, 1/12, 1/8, 1/6, 1/4, 1/3, 1/2, 1/1
double arpRateBeats (int index) noexcept;
// Arpeggiator's Style [DOC order, DATA 0 … 17] as the channel arpeggiator's direction (1 up, 2 down, 3 up-down, 4
// random, 5 up-down with the ends twice); `exact` false = the nearest one
int arpDirectionOf (int style, bool& exact) noexcept;

// ---- lot 2 devices ----------------------------------------------------------------------------------------------
struct Translated;
bool delayToEchoform (const xml::Element& device, const Context& context, Translated& out);
bool echoToEchoform (const xml::Element& device, const Context& context, Translated& out);
bool multibandToPrism (const xml::Element& device, Translated& out);
bool autoFilterToFilter (const xml::Element& device, Translated& out);  // AutoFilter (up to Live 12.1) and AutoFilter2
bool saturatorToEmber (const xml::Element& device, Translated& out);
// Saturator's curve type (Live's menu order) as Ember: its style (0 tube, 1 tape, 2 transistor) or character (1 … 28,
// 0 = none), Ember's drive = Live's drive + driveDb, Ember's output = Live's output + outputDb
struct SaturatorFit
{
    const char* name;
    int style, character;
    double driveDb, outputDb;
};
const SaturatorFit& saturatorFit (int type) noexcept;
bool overdriveToDistortion (const xml::Element& device, Translated& out);
bool limiterToApex (const xml::Element& device, Translated& out);
bool gateToSentry (const xml::Element& device, Translated& out);
bool expanderToSentry (const xml::Element& device, Translated& out);     // Compressor with Model 2 (Expand)
bool channelEqToEq (const xml::Element& device, Translated& out);
bool chorusToChorus (const xml::Element& device, Translated& out);       // Chorus2 (Chorus-Ensemble) and Chorus
bool autoPanToAutoPan (const xml::Element& device, const Context& context, Translated& out);
bool reduxToLofi (const xml::Element& device, Translated& out);

// ---- MIDI effects -----------------------------------------------------------------------------------------------
// Arpeggiator → the channel arpeggiator (false: the element holds no arpeggiator data)
bool arpeggiatorToArp (const xml::Element& device, const Context& context, ArpSettings& arp, Notes& notes);
// The MIDI effects LYNX has no live note processor for in a channel, written into the notes instead: what one note
// becomes. Pitch: transposed. Velocity: the input range onto Out Low … Out Hi (Clip / Gate / Fixed). Chord: the note
// plus up to six shifted copies. Note Length: a fixed length.
struct NoteEdit
{
    int key = 60;
    float velocity = 0.78f; // 0..1
    double lengthBeats = 0.25;
};
struct NoteEffect
{
    enum class Kind { transpose, velocity, chord, length };
    Kind kind = Kind::transpose;
    std::string name;                 // "Pitch", "Velocity", …: for the report
    int semitones = 0;                // transpose
    int lowest = 1, range = 127, outLow = 1, outHigh = 127, mode = 0; // velocity (0 clip, 1 gate, 2 fixed)
    std::vector<std::pair<int, float>> chord; // semitones, velocity factor
    double lengthBeats = 0.0;         // length
};
std::vector<NoteEdit> applyNoteEffect (const NoteEffect& effect, const NoteEdit& note);
// nullopt: not one of those devices, or a setting that cannot be written into notes (then it stays "unsupported")
std::optional<NoteEffect> noteEffectFromLiveDevice (const xml::Element& device, const Context& context, Notes& notes);

// ---- one entry point for the importer ---------------------------------------------------------------------------
struct Translated
{
    EffectSlot slot;
    std::string from, to; // "EQ Eight", "Facet EQ": the report's line is "<from> -> <to>"
    bool approximate = false; // the pair is fitted, not the same maths (the report lists it under "approximated")
    Notes notes;
    SideInput side;
    int sideParam = -1;            // (190cf) the LYNX parameter that switches the external side chain on (−1 = none)
    std::vector<ParamLink> links;  // (190cf) automation / macro routes
    // (190ed) a modulation the effect needs from outside itself: a Peak Controller slot (FL's internal-controller link;
    // its output 0 = peak + LFO) in front of the effect on the same insert — or, `onSideChain`, at the end of the
    // side-chain source's insert — driving parameter `param` through `curve` (evenly spaced over the output's 0..1).
    // A rack chain cannot hold the link: there the importer lists it instead.
    struct Modulator
    {
        EffectSlot controller;
        int param = 0;
        std::vector<float> curve;
        int sideTrackId = -1;           // the envelope follower listens to this track of the set (Live's track Id)
        bool sideOther = false;         // a side-chain input whose source is not a track (the follower hears the input)
        std::vector<std::string> notes; // the report lines when it is applied (laws)
        std::string what;               // "LFO", "envelope follower", "LFO and envelope follower"
    };
    std::optional<Modulator> modulator;
};
// nullopt: not one of the devices above, or its element lacks the parameters (then the importer does what it did
// before: the device is dropped and listed)
std::optional<Translated> effectFromLiveDevice (const xml::Element& device, const Context& context = {});

// ================================================================================================ lot 3 (190ed)
// ---- EQ Three → Prism Multiband ---------------------------------------------------------------------------------
// The kill switches and −inf gains: Prism's bands that still sound are soloed (Prism's solo passes only those bands); a
// gain at Live's floor (−70 dB, shown as −inf) is a killed band [DATA: GainLo / GainMid / GainHi 0.000316 … 1.995]
bool eqThreeToPrism (const xml::Element& device, Translated& out);
// ---- Drum Buss → a rack -----------------------------------------------------------------------------------------
// The generated rack's macros, in order (each mapped over the whole range of its LYNX control)
enum DrumBussMacro { dbDrive, dbCrunch, dbDamping, dbTransients, dbBoom, dbBoomFreq, dbTrim, dbOutput, dbDryWet, dbMacros };
// (round 190ek) fitted to Live 12.4's renders (round 190ei): the rack's chain is Ember (trim, drive, damping) → Facet EQ
// (the device's own colour, Crunch's band, Boom) → Crest Compressor (only with Comp on) → Shell Channel (transients,
// output). A dedicated drum-bus engine (a later round) would add what these stages cannot hold: the type's own EQ under
// drive, Boom's ringing tail and its decay law at other amounts, Crunch's distortion, the transient shaper's tail lift,
// the follower-based compressor's own timing.
namespace drumbuss
{
// the colour at every setting [FITTED to the all-zero device on noise at −20 dBFS rms, on top of the drive stage's own
// EQ (Ember's Pentode voicing: +2 dB near 110 Hz, −1 dB at 31 Hz): three bells and the output, rms 0.14 dB, 31 Hz … 16 kHz]
constexpr double colourGainDb = -2.63;
constexpr double lowBellHz = 96.1, lowBellDb = 11.03, lowBellQ = 0.288;
constexpr double highBellHz = 4578.3, highBellDb = 8.14, highBellQ = 0.262;
constexpr double dipBellHz = 89.0, dipBellDb = -6.37, dipBellQ = 0.711;
constexpr double colourAt1kHzDb = 2.17; // what those bands give at 1 kHz (the drive stage's levels are set around it)
// Crunch's band edges (cookbook shelves) [FITTED on Crunch 0.5, rms 0.07 dB]
constexpr double crunchLowHz = 95.2, crunchLowQ = 0.743, crunchHighHz = 9041.0, crunchHighQ = 0.826;
// the compressor [FITTED on the staircase and the drum loop with Comp on: level rms 0.11 dB over the 21 steps, drum
// segments rms 1.33 dB; 13 settings of threshold / knee / attack / release tried, round 190ek]
constexpr double compThresholdDb = -19.0, compRatio = 4.0, compKneeDb = 0.0, compAttackMs = 1.0, compReleaseMs = 200.0, compMakeupDb = 11.0;
// the drive stage per type (Soft, Medium, Hard): Ember's Pentode voicing (5) with its drive = the measured small-signal
// gain (dB, all-zero device included: 1.97 dB) at DriveAmount 0 / 0.25 / 0.5 / 1, and its output under the colour's
// 1 kHz gain [FITTED: dbfit.cpp / dbeval.cpp, every Ember style, voicing and character against the ten Live staircases;
// the Pentode at the measured gain is the closest for all three types: summed score 14.3 against 23.7 with the Wavefold
// character on type 0 (the type folds the loudest peaks over, which the Pentode does not), 29.5 … 38.7 for the others]
constexpr int voice = 5;
// the type's own EQ under drive, in proportion to DriveAmount [FITTED at 100 % on noise at −20 dBFS rms, against the
// all-zero device and normalised at 1 kHz: Medium a bell −16 dB, Q 0.75 at 181 Hz (rms 6.8 → 3.3 dB), Hard a low shelf
// +15.7 dB, Q 0.875 at 98 Hz (rms 6.3 → 0.8 dB); Soft none; the proportion ASSUMED]
constexpr double typeEqDb[3] { 0.0, -16.0, 15.69 }, typeEqHz[3] { 1000.0, 181.0, 98.0 }, typeEqQ[3] { 1.0, 0.75, 0.875 };
constexpr double emberDriveDb[3][4] { { 1.97, 3.92, 8.91, 11.52 }, { 1.97, 5.73, 9.48, 16.98 }, { 1.97, 5.94, 7.75, 14.23 } };
constexpr double emberOutputDb = -0.04 - colourAt1kHzDb;
} // namespace drumbuss
// the drive's small-signal gain over the all-zero device, dB [MEASURED, 1 kHz at −60 dBFS], piecewise linear in the amount
double drumBussDriveGainDb (int type, double drive) noexcept;
struct DrumBussDriveStage
{
    int voice = drumbuss::voice;
    double driveDb = 0.0, outputDb = 0.0;
    double eqDb = 0.0; // the type's EQ band (Facet band 7: Medium a bell, Hard a low shelf); Ember's output is lowered by its 1 kHz gain
};
DrumBussDriveStage drumBussDriveStage (int type, double drive) noexcept;
// Boom (amount 0 … 1, 30 … 90 Hz, decay 0 … 1) as Facet's bell and resonant 12 dB/oct high pass
struct BoomBands
{
    double hz = 50.0, bellDb = 0.0, bellQ = 2.0, cutHz = 10.0, cutQ = 0.5;
};
BoomBands drumBussBoom (double amount, double hz, double decay) noexcept;
// Crunch's lift of its band in dB (the shelves cut the same outside it)
double drumBussCrunchDb (double crunch) noexcept;
bool drumBussToRack (const xml::Element& device, Translated& out);
// (round 190fk) Drum Buss onto Anvil Bus, LYNX's drum-bus engine (core/AnvilBus.h): the import's default
bool drumBussToEngine (const xml::Element& device, Translated& out);
// ---- Auto Filter's modulation → a Peak Controller ---------------------------------------------------------------
// The Peak Controller's controls for an amount (its laws, core/Effects.cpp PeakController): the peak's amount k (−2 … 2,
// the output per unit of peak) and the LFO's (−1 … 1, the output per unit of wave)
float peakAmountValue (double k) noexcept;
float peakLfoAmountValue (double k) noexcept;
// the LFO's period in quarter notes → its Speed control (period = (1001^(raw / 65536) − 1) · 0.128, raw = 200 + 65336 v)
float peakLfoSpeedValue (double beats) noexcept;
// a linear decay of the peak from 1 to 0 over `ms` at `bpm` → its Decay control ((10001^(d / 128) − 1) · 0.0192 per
// quarter note, d = 5 + 123 v)
float peakDecayValue (double ms, double bpm) noexcept;
// The cutoff's modulation c(t) = c0 + L·w(t) + E·e(t) (cutoff in the Filter's 0..1, w the bipolar LFO, e the peak) as
// the controller's settings and the link's curve; `invert` true = the LFO runs half a cycle later (a negative E flips
// the curve)
struct CutoffModulation
{
    float lfoAmount = 0.5f, peakAmount = 0.5f; // the controller's [5] and [1]
    bool invert = false;
    std::vector<float> curve;
};
CutoffModulation cutoffModulation (double c0, double lfoDepth, double envDepth, bool turnOver = false) noexcept;
// (round 190ek, measured in Live 12.4 by round 190ei: the cutoff tracked on noise and on a sine staircase) the depths in
// semitones of the cutoff: the older device's LfoAmount is semitones as it stands, its ModHub 0.94 semitone per unit at a
// full-scale peak; the new device's Lfo_Amount 94 and Envelope_Amount 96 semitones per 1.0
constexpr double legacyEnvelopeSemitonesPerUnit = 0.94;
constexpr double af2LfoSemitonesPerUnit = 94.0;
constexpr double af2EnvelopeSemitonesPerUnit = 96.0;
// the new device's synced rates (Lfo_SyncedRate 0 … 21) in quarter notes: the older table turned round, 8 bars … 1/64
// [MEASURED: 9 = 1.5 beats]
double autoFilter2SyncedBeats (int index) noexcept;
// the new device's LFO period in quarter notes for Lfo_TimeMode 0 Hz, 1 time (s), 2 synced, 3 triplet, 4 dotted, 5
// sixteenths [MEASURED]; `synced` = it follows the tempo (modes 2 … 5)
double autoFilter2LfoBeats (int timeMode, double hz, double seconds, int syncedRate, double sixteenths, double bpm, bool* synced = nullptr) noexcept;
// Fills `out.modulator` from the device's LFO / envelope follower / side chain (nothing when both are off)
void autoFilterModulation (const xml::Element& device, const Context& context, Translated& out);

// ---- Live's synths → the built-in synth -------------------------------------------------------------------------
// Operator's coarse ratio [DOC: 0 = 0.5, then 1 … 48] and fine (thousandths of it) as semitones above the note
double operatorRatioSemitones (int coarse, double fine) noexcept;
// false: not translated (FM in use, a waveform LYNX's synth has no exact counterpart for): the caller lists the device
bool operatorToSynth (const xml::Element& device, ChannelDef& channel, double bpm, Notes& notes);
bool driftToSynth (const xml::Element& device, ChannelDef& channel, double bpm, Notes& notes);
// The ratio's semitones as the synth's octave / semitones / cents (octave −3 … 3, semitones −11 … 11, cents ±100)
void setOscillatorPitch (SynthOscillator& osc, double semitones) noexcept;

// ---- Simpler ----------------------------------------------------------------------------------------------------
// Voices [DOC: 1 … 8, 10, 12, 14, 16, 20, 24, 32; DATA: the stored index 0 … 14, 5 in a new Simpler = 6 voices]
int simplerVoices (int index) noexcept;
// The loop of Classic mode in frames: the part's region [sampleStart, sampleEnd), played from `start` for `length`
// of what is left, the last `loopLength` of that looping [DOC: Start, Length, Loop as fractions; ASSUMED how they nest]
struct SimplerRegion
{
    double start = 0.0, end = 0.0, loopStart = 0.0;
};
SimplerRegion simplerRegion (double sampleStart, double sampleEnd, double start, double length, double loopLength) noexcept;
struct SimplerResult
{
    int keyShift = 0;     // Slice mode: Live's first slice plays on C1 (36), LYNX's on key 60 — the notes move by this
    bool loop = false;    // Classic mode with Loop on: `loopStart` / `loopEnd` (frames) go on the zone
    std::int64_t loopStart = 0, loopEnd = 0;
};
// Reads what the zone / envelope import leaves out into `channel` (already a sampler with its sample or zones):
// transpose, volume, pan, filter and its envelope, pitch envelope, LFO, voices, glide, the playback mode (one-shot
// fades, slices), the region and loop, warp. `fileSeconds` 0 = unknown (the set's own DefaultDuration is used).
SimplerResult applySimpler (const xml::Element& device, ChannelDef& channel, double bpm, double fileSeconds, Notes& notes);
} // namespace alsdev
} // namespace lynx
