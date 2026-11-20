#pragma once

// Facet EQ — LYNX's precision parametric equalizer (8 bands, zero latency).
//
// Each band is an analog prototype (bell, shelves, cuts up to 96 dB/oct, notch, band pass, tilt shelf) turned into
// digital sections with a magnitude-matched design (poles by impulse invariance, zeros fitted so the digital
// magnitude equals the analog one at DC, at Nyquist and at the band frequency), so bands near Nyquist keep their
// analog shape instead of being squeezed as bilinear-transform EQs are. The prototype family, the Q conventions and the
// slope law follow our black-box measurements of the reference EQ (docs/analise/MEASURE_fabfilter.md §1).
//
// Parameters (0..1, EffectKind::facetEq): band b uses [5b .. 5b+4] = shape, frequency, gain, Q, slope;
// [40] output gain, [41] gain scale; (round 103, appended so older projects load unchanged — every default is the old
// behaviour) [42 + b] dynamic range of band b (0.5 = 0 dB = a static band), [50 + b] its threshold, [58] attack,
// [59] release, [60] phase mode (zero latency / natural / linear), [61] solo band (0 = none), [62] dynamics detector
// (the EQ's input / the slot's sidechain), [63] analyser display (editor only: off, pre, post, pre + post).
// (round 128, Pro-Q parity, appended the same way) [64 + b] band bypass (0 = on; 1 = switched off, keeping its
// settings — stored this way so a zeroed parameter array, e.g. Crest's side-chain EQ, has every band on), [72 + b] stereo placement (stereo, left, right, mid, side), [80] auto gain, [81] output polarity
// inverted, [82] output pan (0.5 = centre), [83] pan mode (left / right or mid / side), and editor-only display
// settings: [84] analyser range (60 / 90 / 120 dB), [85] analyser speed, [86] analyser tilt (0 … 6 dB/oct),
// [87] display range (3 / 6 / 12 / 30 dB). Round 135 (Pro-Q 4 parity): two more shapes — Flat Tilt (the whole
// spectrum tilted at a constant slope: the gain is the level 5 octaves above the band frequency, the opposite 5 below)
// and All Pass (a 2nd-order phase rotation at the band frequency, width by Q) — stored at shape values between the old
// ones (1/16 and 3/16), so every saved shape keeps its meaning; [88] character (Clean = the old sound, Subtle, Warm:
// a gentle analog-style saturation of the output, harmonics only on hot signals). Round 144: [89] linear-phase
// resolution (see resolution()).
//
// Dynamic EQ (docs/analise/REA_rodada103.md §2, measured on the reference): each band with a gain (bell, shelves,
// tilt) whose range is not 0 listens to its own region of the detector signal (band pass at the band frequency and
// Q; low pass / high pass for the shelves) and moves its gain by up to `range` dB as that level rises over the
// threshold — 1 dB per dB above it (an infinite ratio) with a 10 dB soft knee, clamped at the range, smoothed by the
// attack / release times. Phase modes: zero latency (the IIR sections), natural phase (the analog prototype's
// magnitude and phase as an FIR, 320 samples at 48 kHz) and linear phase (the analog magnitude, zero phase, 4608
// samples at 48 kHz), both run as a uniformly partitioned FFT convolution and reported to PDC.

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Effects.h"

namespace lynx::facet
{
constexpr int bands = 8;
constexpr int fieldsPerBand = 5;
constexpr int outputParam = 40;
constexpr int scaleParam = 41;
constexpr int rangeParam0 = 42;     // + band
constexpr int thresholdParam0 = 50; // + band
constexpr int attackParam = 58;
constexpr int releaseParam = 59;
constexpr int phaseParam = 60;
constexpr int soloParam = 61;
constexpr int detectorParam = 62;
constexpr int analyzerParam = 63;
constexpr int powerParam0 = 64;     // + band (round 128)
constexpr int placementParam0 = 72; // + band
constexpr int autoGainParam = 80;
constexpr int invertParam = 81;
constexpr int panParam = 82;
constexpr int panModeParam = 83;
constexpr int specRangeParam = 84;  // editor only
constexpr int specSpeedParam = 85;  // editor only
constexpr int specTiltParam = 86;   // editor only
constexpr int displayRangeParam = 87; // editor only
constexpr int characterParam = 88; // round 135
constexpr int resolutionParam = 89; // round 144: the linear-phase resolution (Low … Maximum, Pro-Q 4's list)
// round 152 (parity152c, Pro-Q 4's spectral dynamics): [90 + b] spectral on, [98 + b] density, [106 + b] tilt
constexpr int spectralParam0 = 90;  // + band
constexpr int densityParam0 = 98;   // + band (default 50 %)
constexpr int spectralTiltParam0 = 106; // + band
// round 166f (issue #16, LYNX's own comparative EQ): [114] listen (the EQ's output / the source = this track without
// the EQ / the target = the slot's sidechain track, both delayed by the EQ's latency), [115] target level match
constexpr int listenParam = 114;
constexpr int targetLevelParam = 115;
// round 190cx (.als import, measured in Live 12.4): [116] cut Q law — 0 = the highest-Q section of a steep cut carries
// the band's Q (the reference EQ's law, the default: every older project keeps it), 1 = every section's Q is scaled
// by the band's Q (Live's EQ Eight 48 dB cuts: an 8th-order Butterworth at Q 1, all four section Qs scaled with Q).
// 6 and 12 dB/oct cuts have one section and sound the same under both.
constexpr int cutLawParam = 116;
constexpr int paramCount = 117;
// round 190jh (Pro-Q 4 parity), hidden — past paramCount, not in the info: no control, no text; saved while non-zero, so
// every project saved before loads 0 and plays as before: [117 + b] band b's automatic threshold (Pro-Q 4's "Auto",
// the default of its dynamic bands; set by the Pro-Q 4 translator). See autoThreshold below.
constexpr int autoThresholdParam0 = paramCount; // + band
// round 190lg (Pro-Q 4 parity), hidden: [125] the automatic threshold's revision. 1 = the reference's own structure
// (static REA, docs/analise/REA_rodada190lg.md): the band level against a peak follower of itself (instant rise, 10 s fall,
// floor −40 dB) per sample, then three followers on the reduction whose times follow the band's region and the attack /
// release knobs. 0 = round 190jh / 190js's fitted law (every project saved before; bit for bit). A new Facet and a
// translated Pro-Q 4 state get 1.
constexpr int autoRevisionParam = autoThresholdParam0 + 8;
// round 190ln (the Kilohearts Disperser role), hidden the same way — saved while non-zero, so every project saved before
// loads 0 (off) and plays bit for bit as before: a cascade of second-order all-pass sections (the cookbook all-pass,
// bilinear) after the bands, [126] how many (value × 64 sections, 0 = off), [127] their frequency (hz law), [128] their
// Q (q law). Set only by the Disperser translator (core/KiloheartsStates.inc). [125] stays free: round 190lg's Pro-Q 4
// automatic-threshold revision (branch rea2_190lg) takes it.
constexpr int dispersionCountParam = autoThresholdParam0 + 9;
constexpr int dispersionFreqParam = dispersionCountParam + 1;
constexpr int dispersionQParam = dispersionCountParam + 2;
constexpr int maxDispersionSections = 64;
int dispersionSections (float v) noexcept; // 0 … 64
// round 190mg (REA of the Waves PuigTec EQP1A + our demo-mode probes, docs/analise/REA_rodada190mg_dyn.md §1), hidden like
// the above (saved while non-zero; 0 = every older project, bit for bit): [129] the PuigTec law (rollup 0.1.30 step 11: after round 190lg's [125] and round 190ln's [126 … 128]) — after the output stage,
// the passive-EQ model's output stage as LYNX measured it: a static tube-style waveshaper (level-dependent H2 / H3 …,
// −50 dB at −4 dBFS), the resampler's fractional delay (a 3rd-order all-pass; 3 samples of latency) and, with the Mains
// switch, a hum generator (a triangle at the mains frequency with an even term through a low and a high pass) plus a
// coloured noise floor. k / 16: 1 = the stage, 2 = + mains 60 Hz, 3 = + mains 50 Hz (set by the PuigTec translator).
constexpr int puigLawParam = dispersionCountParam + 3;
// round 190mm (Waves EQ parity; after the PuigTec law [129] since the 0.1.30 rollup, step 12, docs/analise/REA_rodada190mm.md §2), hidden like the above (saved while non-zero; 0 = every
// older project, bit for bit): [130] the Waves design law (1 = on; set only by the Waves EQ translators: Renaissance EQ /
// RChannel, Q10) and [131 + b] band b's design code (k / 64, waveCode below). With the law on and a band's code non-zero,
// that band is not designed from Facet's own prototypes: its digital sections come from the reference's own structure,
// measured black-box on our demo-mode impulse responses (every section identified exactly, −100 dB and better): the
// band's frequency / gain / Q fields hold the reference's own control values. Facet's phase modes, dynamics and every
// other band keep their behaviour.
constexpr int wavesLawParam = puigLawParam + 1;
constexpr int wavesBandParam0 = wavesLawParam + 1; // + band
// round 190mt (REA_rodada190mt §4), hidden like the above (saved while non-zero; 0 = every older project, bit for bit): [139]
// (rollup 0.1.30 step 13: after the Waves design law's codes [131 … 138]; 135 on round 190mt's base)
// the API-560 law (1 = on; set only by the Waves API-560 translator) and [140 + k] slider k's gain (31 Hz … 16 kHz,
// (dB + 12) / 24). With the law on, Facet's bands are not run: core/Api560.h's graphic-EQ network (oversampled, a
// linear-phase decimator) plays the ten sliders, 65 samples late at every rate (reported to PDC).
constexpr int api560LawParam = wavesBandParam0 + bands;
constexpr int api560GainParam0 = api560LawParam + 1; // + slider (10)
constexpr int storedParamCount = api560GainParam0 + 10;
float api560GainValue (double db) noexcept; // −12 … +12 dB -> 0 … 1
double api560GainDb (float v) noexcept;
// the band design codes of the Waves law
enum class WaveCode
{
    off = 0,
    reqBell = 1,      // Renaissance EQ bell: (s² + s G/q + 1) / (s² + s/q + 1), digitised with a prescribed Nyquist gain
    q10Bell = 2,      // Q10 bell: symmetric, its Q scaled by the gain (Q field = the Q10's Q / 4), bandwidth-prewarped bilinear
    q10PqBell = 3,    // Q10 "PQ" bell: the same with the steeper constant-Q law
    reqLowShelf = 4,  // Renaissance EQ shelves: shared s-term, width from Q (clamped 0.707 … 1.414), bilinear at the centre
    reqHighShelf = 5,
    rbjLowShelf = 6,  // resonant shelves: the cookbook shelf, Q clamped 0.707 … 1.414, bilinear at the band frequency
    rbjHighShelf = 7,
    reqHighPass = 8,  // Renaissance EQ cuts: 18 dB/oct, a pole at the band frequency × a second-order section whose Q and
    reqLowPass = 9,   //   compensating zeros follow the Q control (clamped 0.707 … 1.414)
    lowShelf1 = 10,   // Q10 shelves: first order, centred on the band frequency, bilinear
    highShelf1 = 11,
    butterHighPass = 12, // Q10 cuts: 12 dB/oct Butterworth, bilinear
    butterLowPass = 13,
    // round 190mt: the Q10 bell's exact law (REA_rodada190mt §3): the cookbook section with alpha = tan (min (π f / (Q fs),
    // π / 4)) / (0.090904 (√G + 1/√G)) (Q = the Q10's Q; Q field = Q / 4) — the bandwidth follows f / Q up to fs / 4 and is
    // capped there (wide bands above 3 kHz). Set by the Q10 translator; code 2 keeps round 190mm's law for its projects
    q10BellTan = 14,
    // round 190mt: the Q10 PQ-Bell's law (REA_rodada190mt §3): the same section with alpha = θ (1 + β θ²) / k, θ as above,
    // k = round 190mm's constant-Q factor and β fitted every 1.5 dB on our probes; code 3 keeps round 190mm's law
    q10PqBellX = 15,
    count
};
int waveCode (float v) noexcept;           // 0 … count - 1
float waveCodeValue (WaveCode c) noexcept; // k / 64
int puigLaw (float v) noexcept;          // 0 off, 1 stage, 2 stage + 60 Hz mains, 3 stage + 50 Hz mains
float puigLawValue (int k) noexcept;     // k / 16
constexpr int puigLatency = 3;           // the fractional delay's all-pass order (samples of latency at every rate)
// the stage's waveshaper on one sample (y = the linear output, ±1 = 0 dBFS); unity gain at low level
double puigShape (double y) noexcept;
constexpr int maxSections = 8; // 96 dB/oct = 16th order

enum class Shape { off, bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tiltShelf, flatTilt, allPass };
constexpr int shapeCount = 11;
constexpr std::array<int, 9> slopes { 6, 12, 18, 24, 30, 36, 48, 72, 96 }; // dB/oct

inline int param (int band, int field) noexcept { return band * fieldsPerBand + field; } // 0 shape 1 freq 2 gain 3 Q 4 slope

Shape shape (float v) noexcept;
float shapeValue (Shape s) noexcept;
double hz (float v) noexcept;          // 10 Hz .. 30 kHz (log)
float hzValue (double hz) noexcept;
double gainDb (float v) noexcept;      // ±30 dB
float gainValue (double db) noexcept;
double q (float v) noexcept;           // 0.025 .. 40 (log, 0.5 = 1)
float qValue (double q) noexcept;
int slopeDb (float v) noexcept;        // 6 .. 96 dB/oct
float slopeValue (int dbPerOct) noexcept;
double outputDb (float v) noexcept;    // ±24 dB
double gainScale (float v) noexcept;   // 0 .. 200 %
const char* shapeName (Shape s) noexcept;
bool hasGain (Shape s) noexcept;       // bell, shelves, tilts: the shapes a gain (and dynamics) applies to
// the shapes in the order of the editor's menu (the reference's order: bell … tilt shelf, flat tilt, all pass)
const std::array<Shape, shapeCount - 1>& menuShapes() noexcept;
int character (float v) noexcept;      // 0 clean, 1 subtle, 2 warm
float characterSample (int character, float x) noexcept; // the character stage's transfer curve

enum class Phase { zeroLatency, natural, linear };
constexpr int phaseCount = 3;
enum class Analyzer { off, pre, post, both };

double rangeDb (float v) noexcept;     // dynamic range ±30 dB (0.5 = 0 = static)
float rangeValue (double db) noexcept;
double thresholdDb (float v) noexcept; // −80 … 0 dB (sine peak)
float thresholdValue (double db) noexcept;
double attackMs (float v) noexcept;    // 0.25 … 50 ms
double releaseMs (float v) noexcept;   // 20 … 4000 ms
Phase phase (float v) noexcept;
float phaseValue (Phase p) noexcept;
int soloBand (float v) noexcept;       // −1 = none
float soloValue (int band) noexcept;
bool sidechainDetector (float v) noexcept;
Analyzer analyzer (float v) noexcept;
float analyzerValue (Analyzer a) noexcept;
bool isDynamic (const EffectParams& params, int band) noexcept;
const char* phaseName (Phase p) noexcept;

// round 128 (Pro-Q parity)
enum class Placement { stereo, left, right, mid, side };
constexpr int placementCount = 5;
Placement placement (float v) noexcept;
float placementValue (Placement p) noexcept;
const char* placementName (Placement p) noexcept;
bool bandPower (const EffectParams& params, int band) noexcept; // the band's power switch (on by default)
double panAmount (float v) noexcept;   // −1 (left / mid only) … +1 (right / side only)
bool panMidSide (float v) noexcept;
double analyzerRangeDb (float v) noexcept;  // 60 / 90 / 120
int analyzerSpeed (float v) noexcept;       // 0 very slow … 4 very fast
double analyzerTiltDb (float v) noexcept;   // 0 / 1.5 / 3 / 4.5 / 6 dB per octave
double displayRangeDb (float v) noexcept;   // 3 / 6 / 12 / 30
float displayRangeValue (double db) noexcept;
// The level change of the EQ's static curve (dB) that auto gain takes back: the mean of the curve over log-spaced
// frequencies (20 Hz … 20 kHz) with the dynamics at rest.
double autoGainDb (const EffectParams& params, double sampleRate) noexcept;

// Round 190jh: the automatic threshold, measured black-box on refcap030's Pro-Q 4 captures (round 190hv; Dynamic Range
// -30 / -15 / +15 dB x the stairs and time-constant probes at 1 kHz; the threshold control has no effect there). The band
// tracks a "top" level: it jumps to the detector level when that is higher, falls by autoDecayDbPerSecond otherwise and
// never sits below autoFloorDb (where it starts); the threshold is the top minus the range's size, so a band reaches its
// full range at the loudest recent level and moves 1 dB per dB below it, over a soft knee of autoKneeDb. The band's
// total gain (static + dynamic) stays within +-30 dB as the reference's does. Fitted on the gain trajectories (10 ms
// windows, Nelder-Mead): 1.06 dB rms over 6 trajectories (a fixed threshold at the floor: 4.9 dB) [model: the reference's
// own detector is not known; its top settles ~9 dB under short loud bursts, which this follower does not do].
constexpr double autoFloorDb = -39.7, autoDecayDbPerSecond = 1.70, autoKneeDb = 30.5;
// Round 190js: refitted at 5 ms on the C++ renders themselves (the 10 ms windows above smeared the bursts): the knee
// scales with the range (kneePerRange x |range|: the band starts moving |range| x kneePerRange / 2 under the threshold),
// so after a loud burst a -30 dB band keeps ~5 dB of cut 30 dB below it as the reference does while a -15 dB band
// lets go (autoLaw(); the floor and the fall are round 190jh's constants above). The law also has a slower
// rise toward the level + an offset and its own gain timing (autoTopRiseMs / autoTopOffsetDb / attackMs / releaseMs:
// all 0 = off, which the fit chose). See docs/PARITY.md "Round 190js".
struct AutoLaw
{
    double floorDb, decayDbPerSecond, kneeDb, topOffsetDb, topRiseMs, attackMs, releaseMs;
    double kneePerRange; // > 0: the knee is this many times the range's size (kneeDb unused)
};
const AutoLaw& autoLaw() noexcept;
// round 190lg: revision 1 of the automatic threshold (Pro-Q 4's BandCompander structure; autoRevisionParam): the band's
// per-sample magnitude against a peak follower of it (instant rise, envSec fall to 10 %, never under `floor`), in dB,
// through the gain law below; the reduction then runs through three followers whose times come from the band's region
// and the attack / release knobs (FacetEq.cpp: autoTimes1). relCurve: LYNX's own smooth fit of the reference's release
// knob curve ((e^(k x) − 1) / (e^k − 1)); t0 / t1 / t2a / t2r: scales on the reference's times, fitted on refcap030
// grid2's gain trajectories (50 ms frames, ranges −30 / −15 / +15 / +30, tc + stairs: mean error 0.33 dB for round 190js's
// law, 0.16 dB here): the knob-percent link from Facet's ms controls is the uncertain part (the reference's own times
// with scale 1 released 3× too slowly and attacked over 250 ms where the captures settle in under 100 ms)
struct AutoLaw1
{
    double thrK = 1.0, thrOff = 0.0, kneeK = 1.53, envSec = 10.0, floor = 0.01;
    double t0 = 1.0, t1 = 0.65, t2a = 0.05, t2r = 0.6, relCurve = 3.32;
};
const AutoLaw1& autoLaw1() noexcept;
double autoDynamicGainDb1 (double relDb, double rangeDb) noexcept;
bool autoThreshold (const EffectParams& params, int band) noexcept;
// the auto mode's gain law: dynamicGainDb with the auto knee
double autoDynamicGainDb (double levelDb, double topDb, double rangeDb) noexcept;
// The dynamic gain (dB) a band applies for a detector level (dB, sine peak): 0 below the knee, then 1 dB per dB over the
// threshold (10 dB soft knee), clamped at the range; the sign follows the range.
double dynamicGainDb (double levelDb, double thresholdDb, double rangeDb) noexcept;
// Round 144: the linear-phase resolution, measured black-box on the Pro-Q 4 trial (REA_rodada144a §2): Low, Medium
// (the default — older projects load it: the same 8192-tap FIR as before), High, Very High, Maximum. The FIR has 2 h
// taps (h = 2048, 4096, 8192, 16384, 65536 at 48 kHz, ×2 / ×4 at 96 / 192 kHz) and runs in blocks of 1024 (× the rate
// factor): latency = 1024 + h = the trial's 3072 / 5120 / 9216 / 17408 / 66560 samples at 48 kHz.
constexpr int resolutionCount = 5;
int resolution (float v) noexcept;     // 0 … 4 (round (4 v), the trial's normalised values)
float resolutionValue (int r) noexcept;
const char* resolutionName (int r) noexcept;
int linearHalfTaps (int resolution, double sampleRate) noexcept; // h
// Latency (samples) of a phase mode at a sample rate (linear phase: at the given resolution; Medium by default)
int latencyFor (Phase p, double sampleRate, int resolution = 1) noexcept;

// Round 152 (REA_rodada152c, measured black-box on the Pro-Q 4 trial): a dynamic band with Spectral on works per
// frequency (an STFT, 4096 points at 48 kHz, hop 1024; latency 5120 samples = the trial's): each bin's level (time-
// smoothed by the band's attack / release) is weighted by the band's shape, compared with the threshold — and with the
// band's loudest bin 11.6 dB down (so a loud partial does not drag the quieter ones in) — and moved 1.07 dB per dB, up
// to the range × the shape^2.18 at that frequency. Density smooths the gains across bins; Tilt slopes the threshold
// 4.5 dB/oct around the band frequency (pink-noise balance) [model].
bool isSpectral (const EffectParams& params, int band) noexcept;
double density (float v) noexcept;     // 0 … 1
int spectralLatency (double sampleRate) noexcept;
// the steady-state gain (dB) of a spectral band on a spectrum of partials: levelDb = each partial's sine-peak level
// at hz (one per STFT bin); writes the gain of each
void spectralGainsDb (const EffectParams& params, int band, double sampleRate, const double* hz, const double* levelDb, int n, double* outDb) noexcept;

// Round 152: EQ match [model] — LYNX's own: the difference between a reference spectrum and the current one (dB at
// log-spaced `hz`), its mean level removed, smoothed by `smoothingOct` and scaled by `amount`, fitted greedily with up to
// `maxBands` bells (shelves at the ends) in the EQ's unused bands; writes the parameter changes (index, value) and
// returns the bands placed. The editor captures both spectra from the analyser (Pro-Q 4's match is not reachable
// through host parameters, so its rules are not measured).
int matchEq (const EffectParams& params, const double* hz, const double* currentDb, const double* referenceDb, int n, double amount,
             int maxBands, double smoothingOct, std::vector<std::pair<int, float>>& changes);

// Round 166f (issue #16, "dragging near a bell's centre changes its range instead of moving it"): what a press on the
// graph grabs. Every band's node is a target of `nodeRadius`; the range handle of a dynamic band is a smaller explicit
// target (`rangeRadius`) that only counts where it does not sit on its node, so a press anywhere on a node moves the
// band; a static band (range 0) has no range handle at all. Positions in pixels; `rangeX/Y` only read for bands with
// `dynamic[b]`. Returns the band and whether its range handle was hit (band -1: nothing).
struct GraphHit { int band = -1; bool range = false; };
GraphHit pickGraphTarget (const float* nodeX, const float* nodeY, const bool* on, const bool* dynamic, const float* rangeX,
                          const float* rangeY, int count, int selectedBand, float px, float py, float nodeRadius = 11.0f,
                          float rangeRadius = 6.0f) noexcept;

const EffectInfo& info();
std::string paramText (int param, float v);

// Round 166f (issue #16): comparative EQ — LYNX's own (no FL equivalent). The slot's sidechain track is the *target*
// (a reference mix, the other take); "listen" switches what the slot outputs between the EQ's output, the *source* (this
// track before the EQ) and the target, crossfaded over 10 ms and delayed by the EQ's latency so a switch never jumps in
// time. With target level match on, the target is scaled to the source's loudness (slow RMS, 3 s; ±24 dB at most) so
// the comparison is about tone, not level. The editor draws the target's spectrum over the source's, and EQ match can
// capture both at once.
enum class Listen { eq, source, target };
Listen listen (float v) noexcept;
float listenValue (Listen l) noexcept;
const char* listenName (Listen l) noexcept;
bool listensToTarget (const EffectParams& params) noexcept;
// The level-match gain (linear) for the target from the two mean powers (0 power: unity)
double targetMatchGain (double sourcePower, double targetPower) noexcept;

// One digital section: b0 + b1 z^-1 + b2 z^-2 over 1 + a1 z^-1 + a2 z^-2 (first-order sections have b2 = a2 = 0).
struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// The sections of one band at a sample rate; returns how many were written (0 = the band is off / flat).
// `extraDb` is added to the band's gain (before the gain scale): the dynamic EQ's movement.
int design (const EffectParams& params, int band, double sampleRate, std::array<Section, maxSections>& out, double extraDb = 0.0) noexcept;
// The analog prototype's response of one band (dB) — the target the digital design matches.
double analogDb (const EffectParams& params, int band, double hz, double extraDb = 0.0) noexcept;
// Response of the whole EQ (dB) at hz: the digital sections in zero-latency mode, the analog prototype in the FIR
// modes. `dynamicDb` (optional, 8 values) are the bands' live dynamic gains.
double responseDb (const EffectParams& params, double sampleRate, double hz, const float* dynamicDb = nullptr) noexcept;
// One band's response (dB) at `n` frequencies at once (designed once; for the editor's curves), same rules
void bandCurveDb (const EffectParams& params, int band, double sampleRate, const double* hz, int n, double* outDb, double extraDb = 0.0) noexcept;
// One band's own response (dB) at hz, same rules
double bandResponseDb (const EffectParams& params, int band, double sampleRate, double hz, double extraDb = 0.0) noexcept;

// The effect; with the slot's parameters its latency is known from the start (PDC plans it before the first block).
std::unique_ptr<Effect> make (const EffectParams* initial = nullptr);
// controlOutput (b), b = 0 … 7: band b's live dynamic gain in dB / 60 (signed: 0 = no movement, also when cleared)
} // namespace lynx::facet
