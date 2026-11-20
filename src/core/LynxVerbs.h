#pragma once

// LYNX's own algorithmic reverbs (round 100, docs/analise/REA_rodada100.md).
//
// Six effects on one engine, each "inspired by" a well-known plugin whose behaviour we measured from the outside
// (black-box: VST3 parameter API and test signals only) so a project that used that plugin sounds close when LYNX
// stands in for it (core/PluginSubstitutes.h):
//
//   Epoch Verb   multi-mode vintage-digital reverb: 22 algorithms, three eras of converter colour
//   Atrium       room / chamber reverb with separate early and late sections and three-band decay
//   Foil Plate   plate reverb: instant onset, bright, frequency-dependent decay, 12 plate types
//   Nebula       huge delay-reverb network: delay time, feedback, density, warp, 22 network shapes
//   Halo         shimmer reverb: a pitch shifter inside the feedback path
//   Horizon      echo + reverb: an echo (tape / analog / digital / reverse / octave …) into or after a reverb
//
// The DSP is our own, built from the published literature: Schroeder / Moorer all-pass diffusers, Jot's feedback
// delay network with a Hadamard mixing matrix and per-line absorption filters set from the wanted RT60 (Jot &
// Chaigne 1991), Dattorro's input-diffuser lengths (J. Dattorro, "Effect Design Part 1", JAES 1997), a two-head
// crossfaded delay-line pitch shifter. The parameter laws (normalised value → unit) follow the measured laws of the
// reference plugins so their saved values translate one to one.
// One exception (round 190ca): with its Law parameter on, six of Nebula's modes run on core/NebulaCloud — a port of
// an MIT-licensed model of those networks (attribution and notice in that header and in THIRD_PARTY.md).
//
// Every parameter is 0..1 (EffectParams). Nothing allocates after prepare(); no latency.

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct EffectInfo;
class Effect;

namespace verbs
{
// ---- parameter laws (shared with the substitute maps and the tests) ----
double skewLaw (double v, double lo, double hi, double k) noexcept;     // lo + (hi − lo)·v^k
double skewInverse (double unit, double lo, double hi, double k) noexcept;
// piecewise-linear law through (v, unit) knots, v ascending
double knotLaw (double v, const double* knots, int count) noexcept;
double knotInverse (double unit, const double* knots, int count) noexcept;
int choice (float v, int count) noexcept;                              // index of an option (values i / (count − 1))

// equal-power dry/wet of the six effects' Mix control (measured on the references: dry = cos, wet = sin)
void mixGains (float mix, float& dry, float& wet) noexcept;

// ---- the engine --------------------------------------------------------------------------------------------------
// What a front-end asks of the engine for one block (units, not 0..1). Changes are glided inside the engine.
struct Settings
{
    // input
    double predelaySec = 0.0;
    double inDiffusion = 0.7;      // 0..1: coefficient scale of the four input all-passes (0 = off)
    // early reflections (discrete taps of the pre-delayed input)
    double earlyLevel = 0.0;
    double earlySec = 0.03;        // span of the tap pattern
    double earlyDecay = 3.0;       // tap amplitudes fall by e^(−earlyDecay) over the span
    double earlyDiffusion = 0.0;   // 0..1: two all-passes smearing the taps
    double earlySend = 0.0;        // 0..1: early pattern fed into the late tank as well
    double earlyModRateHz = 0.5;   // round 170c: the early taps' modulation (Atrium's Early mod rate / depth;
    double earlyModDepthMs = 0.0;  //   0 = fixed taps, every other front-end)
    // build-up: the tank is fed through taps spread over buildSec (0 = fed at once)
    double buildSec = 0.0;
    double buildRise = -1.0;       // round tp172d: ≥ 1 = the taps' energy grows as u^(rise − 1) over the span (1 = flat);
                                   //   < 0 = the rise-and-fall bloom every other verb uses
    // late tank (8-line FDN)
    int shape = 0;                 // line-length set (see LynxVerbs.cpp)
    double lineScale = 1.0;        // multiplier of the shape's lengths
    double lineSpread = 1.0;       // delay-network shape only: 0 = every line lineScale ms long, 1 = the full spread
    // round 190ie (issue #185): > 0 = this block's change of the line lengths is a tempo change (Nebula's synced delay
    // under a tempo clip, which steps the tempo at every block): it spreads evenly over this many samples and the loop
    // all-passes follow the gliding lines, instead of the fixed 0.25-sample glide's burst and the all-passes' jump at
    // every block. 0 = the fixed glide (every other change, as before)
    int lengthSpread = 0;
    double rt60 = 2.0;             // mid-band decay (s)
    double lowMult = 1.0, lowXover = 300.0;    // RT multiplier below lowXover
    double highMult = 1.0, highXover = 8000.0; // RT multiplier above highXover
    double loopBandwidth = 20000.0;            // one-pole low-pass inside the loop (Hz)
    double loopLowCut = 0.0;                   // one-pole high-pass inside the loop (Hz, 0 = off)
    double loopDiffusion = 0.5;    // 0..1: in-loop all-pass coefficient scale
    double feedback = -1.0;        // ≥ 0: gain per pass instead of rt60 (delay-network style)
    double crossFeed = 1.0;        // 0..1: how much the two halves of the tank exchange energy
    bool parallelLines = false;    // no mixing at all: eight parallel feedback delays (plain echoes)
    bool splitSides = false;       // with crossFeed 0: the left output reads only the left half of the tank, the right
                                   // only the right (two separate mono reverbs: ValhallaShimmer's stereo modes, round 135)
    double modRateHz = 0.5;
    double modDepthMs = 0.0;       // peak excursion of the line taps
    double lateLevel = 1.0;
    double gateSec = 0.0;          // > 0: gated reverb (dense taps cut after gateSec, short tank)
    // shimmer (pitch shifter in the feedback path)
    double shiftSemis = 0.0;
    double shiftFeedback = 0.0;    // 0..1
    double shiftMix = 1.0;         // 0..1: shifted part of what feeds back (0 = plain feedback)
    double shiftSemis2 = -12.0;    // second voice (dual modes)
    double shift2Mix = 0.0;        // 0..1: level of the second voice in the shifted feedback
    double shimmerEnergy = 0.0;    // round tp172d: > 0 = the line keeps itself and the shifted voice is added, × this (Halo)
    // output
    double width = 1.0;            // −2..2: side gain of the wet output (M/S); 0 = mono, negative swaps L/R
    double outLowCut = 0.0, outHighCut = 0.0;  // Hz (0 = off)
    int outLowOrder = 2, outHighOrder = 2;     // 1 = 6 dB/oct, 2 = 12 dB/oct
    double bandLimit = 0.0;        // Hz (0 = off): a steep (24 dB/oct) low-pass, the reduced bandwidth of some algorithms
    int bandLimitSections = 2;     // its Butterworth order / 2 (2 = 24 dB/oct … 16 = 192 dB/oct)
    double lowShelfHz = 200.0, lowShelfDb = 0.0;
    double highShelfHz = 6000.0, highShelfDb = 0.0;
    double toneHighHz = 8000.0, toneHighDb = 0.0;          // a second high shelf (an algorithm's own tone)
    double toneLowHz = 100.0, toneLowDb = 0.0;             // round 183b: a second low shelf (an algorithm's own tone)
    double toneMidHz = 1000.0, toneMidDb = 0.0, toneMidQ = 0.7; // a bell (an algorithm's own tone)
    double gain = 1.0;

    // ---- engine law 1 (0.1.27, round 190bz). Everything below is read only when law == 1; law 0 is the engine as it
    // was (bit-identical: projects saved before keep it). A delay-network front-end (feedback ≥ 0) stays on law 0.
    int law = 0;
    // per-band decay: RT60 of the octave bands 63 Hz … 8 kHz as a multiple of the decay the three-band law above gives
    // there (the loop filter of every line is designed to it), and the tone corrector after the loop (dB per band)
    double bandRtMult[8] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };
    double bandDb[8] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    // onset: a diffusion-step network (delays → polarity → mixing, three steps) on the pre-delayed input: a dense
    // burst over onsetSec heard at onsetLevel; onsetFront 0..1 adds its first two steps (energy to the very front);
    // onsetFeed 0..1 feeds the tank from it instead of the input all-passes (dense from the first pass)
    double onsetLevel = 0.0, onsetSec = 0.03, onsetFront = 0.0, onsetFeed = 0.0;
    // density as a law: the mixing inside each half of the tank is a rotation scattering by mixAngle × 45° (1 = full
    // mixing, 0 = none: the echo density then builds only through crossFeed and the all-passes)
    double mixAngle = 1.0;
    // modulation that moves every band alike: the rotation between the two halves swings by ± matrixModDepth (rad)
    double matrixModDepth = 0.0, matrixModRateHz = 0.3;
    // three bells after the tone corrector (a mode's own narrow colour, where an octave band is too wide)
    double bellHz[3] = { 100.0, 1000.0, 5000.0 }, bellDb[3] = { 0.0, 0.0, 0.0 }, bellQ[3] = { 1.4, 1.4, 1.4 };

    // ---- revision (round 190it), law 1 only: 0 = the 0.1.27 law as it was (projects saved before keep it, bit for bit);
    // 1 = the 0.1.30 revision (the front-ends' trims; see each effect)
    int revision = 0;
    bool buildSplit = false; // round 190it: above ~600 Hz the build-up taps reach the tank in four independent groups (a
                             // tail without the one FIR's ripple: a steady tone meets the tail's mean gain); Atrium, Halo
    // round 190ln: 1 = the panel shelves (lowShelf* / highShelf*) are first order: the bilinear transform of
    // (gLow + gHigh·s/ω0) / (1 + s/ω0), ω0 = 2π·hz (not pre-warped), gLow or gHigh the shelf's gain (Foil Plate's ValhallaPlate
    // role, read by static REA); 2 = the second-order shelves every front-end had (projects saved before, every other effect)
    int shelfOrder = 2;
    // round 190ls, law 1 only: 1 = the late tank is the dense network (32 lines in two halves of 16, a 16-point Hadamard
    // inside each half, the halves rotated into each other by crossFeed; no loop all-passes): Atrium's ValhallaRoom role,
    // whose late network has 32 lines (static REA). Needs an engine made with dense = true; 0 = the 8-line tank
    int dense = 0;
    // round 190mb, law 0 only: 1 = the tone corrector (bandDb, the octave-band EQ law 1 runs after its loop) also after the
    // classic engine's loop: Nebula's Supermassive role (the reference's per-mode tone, which the classic networks miss).
    // 0 = the classic engine as it was (every other effect, every project saved before)
    int classicTone = 0;
};
constexpr int engineBands = 8;
// the octave-band RT60 (s, 63 Hz … 8 kHz) the engine aims at for a line of lineMs under law 1 (tests, fitting)
void bandTargets (const Settings& s, double sampleRate, double lineMs, double out[engineBands]) noexcept;

class Engine
{
public:
    // maxLineSec: longest tank line the effect may ask for (sets the memory)
    // dense: also allocates the dense late tank (Settings::dense; round 190ls)
    explicit Engine (double maxLineSec = 0.5, double maxPredelaySec = 0.6, bool dense = false);
    ~Engine();
    void prepare (double sampleRate);
    void reset() noexcept;
    // processes the wet signal in place (the dry/wet is the front-end's)
    void process (float* left, float* right, int n, const Settings& s) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// ---- the six effects ---------------------------------------------------------------------------------------------
namespace epoch
{
constexpr int paramCount = 18;
constexpr int engineLaw = 17; // round 190bz: 0 = the classic engine (projects saved before), 1 = the 0.1.27 law
constexpr int revision = paramCount; // round 190it, hidden (no control, saved while non-zero): 1 = the 0.1.30 revision of
                                     //   the Measured law (new instances, translated states); 0 = projects saved before
constexpr int loopLaw = paramCount + 1; // round 190kw, hidden (read only with revision 1): 1 = the level follows the
                                        //   reference's loop law over Decay and Size, the early part holds (new instances,
                                        //   translated states); 0 = revision 1 as round 190it left it (projects saved before)
constexpr int shortTrim = paramCount + 2; // round 190ly, hidden (read with the engine law and revision 1): 1 = Concert Hall's
                                          //   short-decay trim (ValhallaVintageVerb's at 0.51 s, faded out by 2.9 s; new
                                          //   instances, translated states); 0 = revision 1 as before
constexpr int rateLaw = paramCount + 3; // round 190mb, hidden (read with the engine law and revision 1): 1 = the reference's
                                        //   own level change away from 48 kHz (its early field and tail per colour; only a
                                        //   translated ValhallaVintageVerb sets it); 0 = the same sound at every rate (as before)
constexpr int modeCount = 22;
constexpr int colourCount = 3;
const char* modeName (int index) noexcept;
const char* colourName (int index) noexcept;
double predelayMs (float v) noexcept;
double decaySec (float v) noexcept;
}
namespace atrium
{
constexpr int paramCount = 23;
constexpr int engineLaw = 22; // round 190bz: 0 = the classic engine (projects saved before), 1 = the 0.1.27 law
constexpr int revision = paramCount; // round 190it, hidden (no control, saved while non-zero): 1 = the 0.1.30 revision of
                                     //   the Measured law (new instances, translated states); 0 = projects saved before
constexpr int reaLaw = paramCount + 1; // round 190ls, hidden (read only with the engine law and revision 1): 1 = the
                                       //   ValhallaRoom role on the dense late tank (32 lines, the reference's count, static REA)
                                       //   with its laws refitted (new instances, translated states); 0 = revision 1 as before
constexpr int typeCount = 12;
const char* typeName (int index) noexcept;
double decaySec (float v) noexcept;
float decayValue (double sec) noexcept;
}
namespace foil
{
constexpr int paramCount = 17; // round 135: + Infinite, Low cut; round 150: + Voicing; 152: + View; 190bz: + Law
constexpr int engineLaw = 16; // round 190bz: 0 = the classic engine (projects saved before), 1 = the 0.1.27 law
constexpr int revision = paramCount; // round 190it, hidden (no control, saved while non-zero): 1 = the 0.1.30 revision of
                                     //   the Measured law (new instances, translated states); 0 = projects saved before
constexpr int reaLaw = paramCount + 1; // round 190ln, hidden (read only with revision 1 and the plate voicing off): 1 = the
                                       //   reference's own EQ (first-order shelves) and bass decay over Decay, read by static REA
                                       //   (new instances, translated states); 0 = revision 1 as round 190it left it
constexpr int typeCount = 12;
const char* typeName (int index) noexcept;
double decaySec (float v) noexcept;
double lowCutHz (float v) noexcept; // 0 = off, else 20 Hz … 1 kHz
double reaBassRtMult (double decaySec) noexcept; // round 190ln: the bass decay's multiple of Decay (1 up to 2.5 s, 1.4 from 10 s)
}
namespace nebula
{
constexpr int paramCount = 13;
constexpr int modeCount = 22;
constexpr int law = 14; // round 190ca: 1 = the cloud law (core/NebulaCloud.h; new instances), 0 = the classic engine
constexpr int toneLaw = law + 1; // round 190mb, hidden (no control, no string; saved while non-zero): 1 = the classic
                                 //   networks' per-mode tone and level of ValhallaSupermassive (set only by its translator, the
                                 //   substitute table's `replaced` items); 0 = as before (a new Nebula, every project saved before)
const char* modeName (int index) noexcept;
double delayMs (float v) noexcept;
bool cloudMode (int mode) noexcept; // whether the cloud law has a network for this mode (the others stay classic)
}
namespace halo
{
constexpr int paramCount = 13;
constexpr int engineLaw = 12; // round 190bz: 0 = the classic engine (projects saved before), 1 = the 0.1.27 law
constexpr int revision = paramCount; // round 190it, hidden (no control, saved while non-zero): 1 = the 0.1.30 revision of
                                     //   the Measured law (new instances, translated states); 0 = projects saved before
}
namespace horizon
{
constexpr int paramCount = 25;
constexpr int engineLaw = 24; // round 190bz: 0 = the classic engine (projects saved before), 1 = the 0.1.27 law
constexpr int revision = paramCount; // round 190mb, hidden (no control, no string; saved while non-zero; read with the engine
                                     //   law): 1 = the 0.1.30 revision, ValhallaFutureVerb's per-mode tone and level (new
                                     //   instances, translated states); 0 = projects saved before, bit for bit
constexpr int reaLaw = paramCount + 1; // round 190mj, hidden (no control, no string; saved while non-zero; read with the engine
                                       //   law and revision 1): 1 = ValhallaFutureVerb's echo modes and Room / Chamber laws
                                       //   (new instances, translated states); 0 = projects saved before, bit for bit
constexpr int echoModeCount = 12;
constexpr int reverbModeCount = 8;
const char* echoModeName (int index) noexcept;
const char* reverbModeName (int index) noexcept;
double decaySec (float v) noexcept;
double echoMs (float v) noexcept;
}

const EffectInfo& info (EffectKind kind);
std::string paramText (EffectKind kind, int param, float v, double bpm);
std::unique_ptr<Effect> make (EffectKind kind);
bool isLynxVerb (EffectKind kind) noexcept;
} // namespace verbs
} // namespace lynx
