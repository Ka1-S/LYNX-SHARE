#pragma once

// LYNX's dynamics suite (REA_rodada108): three native processors designed against our own black-box measurements of
// the reference mixing plugins (docs/analise/MEASURE_fabfilter.md §2, §6, §7 and §12) and published DSP literature.
// Own names, own code, own parameter laws:
//
//   Crest Compressor   11 styles (detector / curve / timing families), knee, range, hold, look-ahead, auto release,
//                      a 3-band side-chain EQ (Facet EQ prototypes), side-chain level / external input / listen,
//                      stereo link, character stage (warm / crisp / bright, pre or post), 2x-8x oversampling,
//                      auto gain, mix
//   Prism Multiband    4-band dynamics (compress / expand, downward or upward by the sign of the range), crossovers
//                      12 / 24 / 48 dB/oct, transparent at rest (a band without gain change leaves the signal
//                      untouched), per-band level, solo and bypass, look-ahead
//   Sentry Gate        gate / expander with 5 styles (hysteresis and timing families, upward expansion), knee,
//                      range, hold, look-ahead, side-chain high / low pass, external side chain, listen, dry mix
//
// Parameters are 0..1 in the model (EffectParams); the laws to real units are below. Nothing allocates after
// construction. Latency depends only on the "maximum look-ahead" / look-ahead switches and the oversampling choice and
// is reported through latencySamples() (known after prepare(), and for slots built by make (kind, &params) right
// away, so PDC is planned before the first block).
//
// Meters (Effect::controlOutput): Crest / Sentry: 0 = gain reduction 0..1 over 0..30 dB, 2 = input peak, 3 = output
// peak; Prism: 0..3 = the bands' gain change in dB / 30 (negative = reduction, positive = boost), 4 = input peak,
// 5 = output peak.

#include <array>
#include <memory>
#include <string>

#include "core/Song.h"

namespace lynx
{
struct EffectInfo;
class Effect;

namespace dyn
{
// ---- Crest Compressor ----
namespace crest
{
enum Param
{
    style, threshold, ratio, knee, range, attack, release, autoRelease, hold, lookahead, maxLookahead,
    character, characterPre, drive, scLevel, scExternal, stereoLink, scListen,
    sc1Shape, sc1Freq, sc1Gain, sc1Q, sc1Slope,
    sc2Shape, sc2Freq, sc2Gain, sc2Q, sc2Slope,
    sc3Shape, sc3Freq, sc3Gain, sc3Q, sc3Slope,
    oversampling, autoGain, mix, input, output,
    // round 128 (Pro-C 3 parity): dry level (parallel compression), auto threshold, stereo-link mode, display settings
    dryGain, autoThreshold, linkMode, meterScale, inputMeter,
    // round 135 (Pro-C 3 parity): the wet (compressed) and dry paths' stereo balance
    wetPan, dryPan,
    // round 137 (Pro-C 3 parity, appended; off by default): the side-chain trigger — the detector hears a 0 dBFS
    // pulse (× the side-chain level) instead of the audio: Host sync = a pulse every note × rate, Length of that
    // period long, on the song position; MIDI = while a note is held on the chosen Channel Rack channel.
    // [EXEC] measured on the trial (REA_rodada137 §6)
    trigger, triggerSync, triggerRate, triggerLength, triggerChannel,
    // round tp172b2 (Pro-C 3 parity, appended; none by default): Pro-C 3's styles that Style has no slot for — Smooth,
    // Op-El and Mastering — chosen over the style when set (the style list itself keeps its 11 entries for old projects)
    voicing,
    // round 190v (appended; on for new instances): the two-way and vari-mu laws fitted to the reference's threshold ×
    // ratio × knee × attack grid. Off = round 108's laws: what a project saved before that round loads with
    // (upgradeLoadedEffectParams), so it sounds as it did. The other styles ignore it.
    law,
    count
};
enum class Trigger { off, hostSync, midi };
Trigger triggerOf (float v) noexcept;
double triggerBeats (float v) noexcept;     // whole, 1/2, 1/4, 1/8, 1/16 note (quarter notes: 4 … 0.25)
double triggerRateOf (float v) noexcept;    // 0.5 … 2 (the trial's "offset" 50 … 200 %, log)
double triggerLengthOf (float v) noexcept;  // 0 … 1 of the period
int triggerChannelOf (float v) noexcept;    // −1 = off, else 0 … 127
// the host-sync pulse at a song position (quarter notes): 1 inside the pulse, else 0
float hostTrigger (const EffectParams& p, double beats) noexcept;
constexpr int scBands = 3;
// stereo-link modes: L/R (the classic linked or split stereo detection), mid only, side only, mid triggers the side,
// side triggers the mid (the last four process in mid / side)
enum class LinkMode { stereo, mid, side, midToSide, sideToMid };
constexpr int linkModes = 5;
LinkMode linkModeOf (float v) noexcept;
float linkModeValue (LinkMode m) noexcept;
double dryDb (float v) noexcept;         // −∞ (v = 0) … +36 dB; 0 dB at 0.5 (72 dB/unit above 0.25, square law below)
double panOf (float v) noexcept;         // −1 (left) … +1 (right) balance of the wet / dry path
float dryValue (double db) noexcept;
double meterRangeDb (float v) noexcept;  // the display's range: 9, 18, 36, 72 or 90 dB
// Auto threshold (round 128): the threshold follows the programme — the effective threshold is the detector input's
// slow RMS level (3 s window) + (the threshold knob + 18 dB), so the knob's −18 dB puts it right at the average level.
double autoThresholdDb (double knobDb, double programRmsDb) noexcept;
inline int scParam (int band, int field) noexcept { return sc1Shape + band * 5 + field; } // field: 0 shape .. 4 slope

enum class Style { clean, allround, punch, upward, twoWay, variMu, classic, opto, vocal, bus, pumping,
                   smooth, opEl, mastering }; // the last three only through the voicing parameter
constexpr int styles = 11, allStyles = 14, voicings = 4;
enum class Character { off, warm, crisp, bright };

Style styleOf (float v) noexcept;              // the style parameter alone (its 11 entries)
Style styleOf (const EffectParams& p) noexcept; // the style the engine runs: the voicing when set, else the style
float styleValue (Style s) noexcept;            // the style parameter's value (one of its 11 entries)
float voicingValue (Style s) noexcept;          // the voicing parameter's value (0 = none for the 11 styles)
double thresholdDb (float v) noexcept;   // −60 .. 0 dBFS
double ratioOf (float v) noexcept;       // 1 .. 100 : 1; the exact top is a true limit (round tp172b6)
float ratioValue (double r) noexcept;
double kneeDb (float v) noexcept;        // 0 .. 72 dB
double rangeDb (float v) noexcept;       // 0 .. 60 dB
double attackMs (float v) noexcept;      // 0.005 .. 250 ms (log)
float attackValue (double ms) noexcept;
double releaseMs (float v) noexcept;     // 10 .. 2500 ms (log)
float releaseValue (double ms) noexcept;
double holdMs (float v) noexcept;        // 0 .. 500 ms (square law)
float holdValue (double ms) noexcept;
double lookaheadMs (float v) noexcept;   // 0 .. 20 ms
double maxLookaheadMs (float v) noexcept; // off, 1, 2, 5, 10, 20 ms (the latency)
Character characterOf (float v) noexcept;
double driveDb (float v) noexcept;       // −24 .. +24 dB
double scLevelDb (float v) noexcept;     // −36 .. +36 dB
int oversamplingOf (float v) noexcept;   // 1 (off), 2, 4, 8
double trimDb (float v) noexcept;        // input / output: −36 .. +36 dB

// Static curve: gain (dB, ≤ 0 for downward styles) the compressor settles to for a steady sine of `inputDb` peak
// level (side chain flat, no auto gain). Used by the editor's curve and the tests.
double staticGainDb (const EffectParams& p, double inputDb);
// The character stage's transfer curve at a drive (no compensation), for the editor.
float characterCurve (Character c, float x) noexcept;
} // namespace crest

// ---- Prism Multiband ----
namespace prism
{
// Round 135 (Pro-MB parity): up to 6 bands (was 4). Bands 1–4 and the first three crossovers keep their parameter
// indices; bands 5–6, crossovers 4–5, their look-aheads, the band count, the bands' output pan and the external side
// chain are appended. The band count (default 4) is how many contiguous bands the crossover tree has: 4 is the old
// engine, sample for sample.
constexpr int bands = 6;        // the most bands
constexpr int legacyBands = 4;  // bands of the old parameter block
constexpr int fieldsPerBand = 10;
enum Field { fThreshold, fRatio, fRange, fMode, fKnee, fAttack, fRelease, fLevel, fEnabled, fSolo };
enum Global { xover1 = legacyBands * fieldsPerBand, xover2, xover3, slope, lookahead, mix, input, output,
              // round 128 (Pro-MB parity): each band's own look-ahead (≤ the global one, which sets the latency)
              bandLookahead0,
              // round 135: bands 5 and 6 (10 fields each), crossovers 4–5, their look-aheads, the band count, the pan
              // of each band's output (6), the external side chain
              band5 = bandLookahead0 + legacyBands, xover4 = band5 + 2 * fieldsPerBand, xover5, bandLookahead4, bandLookahead5,
              bandCountParam, bandPan0, scExternal = bandPan0 + bands,
              // round 144 (Pro-MB's bottom bar, measured black-box, REA_rodada144b): the processing mode (the
              // reference's value law: 0 linear, 0.5 dynamic = the old engine, 1 minimum phase) and the oversampling
              // (0 off, 0.5 2x, 1 4x); appended, the defaults are the old sound
              processingMode, oversampling,
              // round 144: what each band's PAN balances — left / right (0, as before) or mid / side (1, the reference's
              // band pan: mid gain = 1 − pan right of centre, side gain = 1 + pan left of it)
              panMode,
              // round 147 (REA_rodada147 §1): Dynamic phase's engine — 1 = the measured one (each working band a
              // minimum-phase filter with the linear-phase magnitude, the bands in cascade: core/MinPhaseBand.h), 0 = the
              // round-108 construction (input + each band's gain change), kept for projects saved before round 147
              dynamicEngine,
              // round 147 (Pro-MB's expert panel, measured black-box): each band's stereo link (the reference's value
              // law: 0 … 0.5 = 0 … 100 % linked, 0.5 … 1 = 100 % + 0 … 100 % mid- / side-only; default 100 % = the old
              // detector), its mode (mid / side), the side-chain filtering (band / free) and the free filter's low /
              // high frequency (30 Hz … 30 kHz)
              bandLink0, bandLinkMode0 = bandLink0 + bands, bandScFree0 = bandLinkMode0 + bands,
              bandScLow0 = bandScFree0 + bands, bandScHigh0 = bandScLow0 + bands,
              // round 147: each crossover's own slope (Pro-MB's per-band slopes): 0 = the global Slope, then 12 / 24 / 48
              xoverSlope0 = bandScHigh0 + bands,
              // round 150 (REA_rodada150): free band placement like Pro-MB — the layout (0 = split: contiguous bands on the
              // crossovers, the old engine; 1 = free: each band its own low / high edge and slopes, gaps pass untouched),
              // each band's edges (30 Hz … 30 kHz, 30 · 1000^v) and slopes (6 … 48 dB/oct, the reference's value law), each
              // band's side-chain input (0 = the global External SC switch, 0.5 = the plug-in input, 1 = external), and
              // the detector law (1 = measured on the trial: soft range, release after the curve; 0 = the round-108 one)
              bandLayout = xoverSlope0 + bands - 1,
              bandLo0, bandHi0 = bandLo0 + bands, bandLoSlope0 = bandHi0 + bands, bandHiSlope0 = bandLoSlope0 + bands,
              bandScInput0 = bandHiSlope0 + bands, detectorLaw = bandScInput0 + bands,
              rev152 = detectorLaw + 1, // round 152: the measured model's revision (gain-stage attack, narrow bands, 0.8-octave minimum band)
              // round tp172b6 [EXEC] (Pro-MB's Audition, Expert panel): the output becomes what the band's detector hears (0 off,
              // k / 6 band k): level, range, threshold and mix don't touch it, the Output does, a switched-off band still sounds
              audition = rev152 + 1,
              count = audition + 1 };
inline int param (int band, int field) noexcept
{
    return band < legacyBands ? band * fieldsPerBand + field : band5 + (band - legacyBands) * fieldsPerBand + field;
}
inline int crossoverParam (int k) noexcept { return k < 3 ? xover1 + k : xover4 + (k - 3); }      // k = 0 … 4
inline int bandLookaheadParam (int b) noexcept { return b < legacyBands ? bandLookahead0 + b : bandLookahead4 + (b - legacyBands); }
inline int bandPanParam (int b) noexcept { return bandPan0 + b; }
// the live meter of band b (Effect::controlOutput): 0 … 3, then 6 and 7 (4 / 5 are the input / output peaks)
inline int bandMeter (int b) noexcept { return b < legacyBands ? b : 6 + (b - legacyBands); }
// round tp172b6: the level band b's detector reads (controlOutput 8 ... 13): levelMeterValue (peak dB), 0 ... 1 over -120 ... 0 dB
inline int bandLevelMeter (int b) noexcept { return 8 + b; }
float levelMeterValue (double db) noexcept;
double levelMeterDb (float v) noexcept;
int auditionBand (const EffectParams& p) noexcept; // the auditioned band, -1 none
int bandCount (const EffectParams& p) noexcept;   // 1 … 6
float bandCountValue (int n) noexcept;
double panOf (float v) noexcept;                   // −1 (left) … +1 (right): the band's output balance
// the crossovers in use (ascending, n − 1 of them)
std::array<double, bands - 1> crossovers (const EffectParams& p) noexcept;

double thresholdDb (float v) noexcept;   // −90 .. 0 dBFS (square-root law, finer near 0 dB)
float thresholdValue (double db) noexcept;
double ratioOf (float v) noexcept;       // 1 .. 100 : 1 (same law as Crest)
bool isInfiniteRatio (float v) noexcept; // round tp172b6: the top of the range is a true limit (the reference's infinity : 1)
double rangeDb (float v) noexcept;       // −30 .. +30 dB (negative = downward, positive = upward)
double kneeDb (float v) noexcept;        // 0 .. 48 dB
double attackMs (float v) noexcept;      // detector τ 0.11 .. 89 ms (shown as 0 .. 100 %)
double releaseMs (float v) noexcept;     // detector τ 130 .. 3630 ms (shown as 0 .. 100 %)
double levelDb (float v) noexcept;       // −30 .. +30 dB
double crossoverHz (float v) noexcept;   // 30 Hz .. 18 kHz (log)
float crossoverValue (double hz) noexcept;
int slopeDbOct (float v) noexcept;       // 12, 24 or 48
double lookaheadMs (float v) noexcept;   // 0 .. 20 ms (0 = off: no latency)
double trimDb (float v) noexcept;        // ±36 dB
// round 144: Dynamic phase (the old engine: the input plus each band's gain change, bit-exact when no band works),
// Linear phase (FIR crossovers with the same magnitudes: no phase change, latency linearLatency()), Minimum phase (the
// classic crossover sum, all-pass compensated: the crossovers' phase is always there)
enum class Phase { linear, dynamic, minimum };
Phase phaseOf (float v) noexcept;
bool measuredDynamic (const EffectParams& p) noexcept; // round 147: Dynamic phase runs the measured engine
// round 147: the stereo link's two parts — linked (0 … 1) and mid- / side-only (0 … 1, only once fully linked)
double linkAmount (float v) noexcept;
double linkOnly (float v) noexcept;
double scFilterHz (float v) noexcept;   // 30 Hz … 30 kHz (30 · 1000^v, the reference's law)
float scFilterValue (double hz) noexcept;
int crossoverSlopeDbOct (const EffectParams& p, int k) noexcept; // round 147: crossover k's slope (its own or the global)
float crossoverSlopeValue (int dbOct) noexcept;                  // 12 / 24 / 48 → its own value (0 = global)
inline int slopeOrder (int dbOct) noexcept { return dbOct <= 12 ? 1 : (dbOct <= 24 ? 2 : 4); } // n of LR(2n)
int oversamplingOf (float v) noexcept;   // 1 (off), 2 or 4
// round 150: the free layout and its laws
bool freeLayout (const EffectParams& p) noexcept;
double edgeHz (float v) noexcept;                  // 30 · 1000^v (Pro-MB's crossover law)
float edgeValue (double hz) noexcept;
double slopeOfValue (float v) noexcept;            // 6 + 24v below 0.25, then 48v (Pro-MB's slope law), dB/oct
float slopeValue (double dbOct) noexcept;
int iirSlope (double dbOct) noexcept;              // the IIR crossovers' slope for a continuous one: 6, 12, 24, 36 or 48
bool measuredDetector (const EffectParams& p) noexcept;
// the segments the bands make: contiguous bands (the split layout, or free bands with the gaps between them as
// segments that pass untouched); at most 2·bands + 1
constexpr int maxSegments = 2 * bands + 1;
struct Layout
{
    int segments = 1;
    std::array<double, maxSegments - 1> xoverHz {};   // ascending
    std::array<double, maxSegments - 1> xoverSlope {}; // dB/oct (continuous: the linear phase's shapes)
    std::array<int, maxSegments> bandOf {};            // the band of a segment, −1 = a gap
    std::array<int, bands> segmentOf {};               // the segment of band b (−1 = not in use)
    // per band (free layout: its own; split layout: its crossovers), ≤ 0 Hz = no edge on that side
    std::array<double, bands> loHz {}, hiHz {}, loSlope {}, hiSlope {};
};
Layout layoutOf (const EffectParams& p) noexcept;
int linearFftSize (double sampleRate) noexcept; // 4096 up to 50 kHz, ×2 per doubling of the rate
int linearLatency (double sampleRate) noexcept; // 3/4 of the FFT size (3072 samples at 48 kHz, as measured)

// Static gain change of band `b` (dB) for a steady sine of `inputDb` inside the band.
double bandStaticDb (const EffectParams& p, int b, double inputDb);
// The magnitude of the whole effect (dB) at `hz` with every band at the gain `bandGainDb[b]` — the editor's curve.
double responseDb (const EffectParams& p, const double* bandGainDb, double sampleRate, double hz);
} // namespace prism

// ---- Sentry Gate ----
namespace sentry
{
enum Param
{
    style, threshold, ratio, range, attack, release, hold, knee, lookahead, lookaheadOn,
    highPass, lowPass, scExternal, scListen, upThreshold, upRatio, dry, output,
    // round 128 (Pro-G parity): channel mode, the expert side-chain gain / mix per detector channel, wet / dry pan,
    // ducking, input level
    channelMode, scGainL, scMixL, scGainR, scMixR, wetPan, dryPan, ducking, input,
    // round 135 (Pro-G's bottom bar): oversampling off / 2x / 4x / 8x — the gate's gain is applied at the high rate
    // (smoother, alias-free fast gating); latency + the oversampler's
    oversampling,
    // round 190v (appended; on for new instances): the Vocal style's noise reduction. Off = none: what a project saved
    // before that round loads with (upgradeLoadedEffectParams). The other styles ignore it.
    law,
    // round 190dm (appended; default Level = every gate before this round, sample for sample): the trigger. Transient: a
    // drum gate — an onset over the threshold opens it fully, it holds for Hold and closes along the release shape
    // over Release, whatever the level does meanwhile (the style's level curve, ratio and knee are not used). Measured
    // black-box on a drum-gate reference's probes (docs/PARITY.md "Round 190dm").
    trigger, sensitivity, releaseShape,
    count
};
enum class Shape { smooth, mid, sharp };
bool transientTrigger (const EffectParams& p) noexcept;
double sensitivityOf (float v) noexcept;   // 1 … 10 (5 = the default)
float sensitivityValue (double s) noexcept;
Shape shapeOf (float v) noexcept;
// the transient gate's closing curve (dB, ≤ 0) `tau` seconds after its hold, for a release of `releaseSec` seconds
double transientCurveDb (Shape s, double tau, double releaseSec) noexcept;
double scGainDb (float v) noexcept;      // −36 … +36 dB
double mixOf (float v) noexcept;         // −1 (left / mid) … +1 (right / side); 0 = the channel's own input
double panOf (float v) noexcept;         // −1 … +1 balance
enum class Style { classic, clean, vocal, guitar, upward };
constexpr int styles = 5;
Style styleOf (float v) noexcept;              // the style parameter alone (its 11 entries)
Style styleOf (const EffectParams& p) noexcept; // the style the engine runs: the voicing when set, else the style
float styleValue (Style s) noexcept;            // the style parameter's value (one of its 11 entries)
float voicingValue (Style s) noexcept;          // the voicing parameter's value (0 = none for the 11 styles)
double thresholdDb (float v) noexcept;   // −60 .. 0
double ratioOf (float v) noexcept;       // 1 .. 100 (same law as Crest)
double rangeDb (float v) noexcept;       // 0 .. 100 dB
double attackMs (float v) noexcept;      // 0 .. 1000 ms (v⁴)
double releaseMs (float v) noexcept;     // 0 .. 5000 ms (v³)
double holdMs (float v) noexcept;        // 0 .. 250 ms (v²)
double kneeDb (float v) noexcept;        // 0 .. 30 dB
double lookaheadMs (float v) noexcept;   // 0 .. 10 ms
double filterHz (float v) noexcept;      // 5 Hz .. 30 kHz (log)
float filterValue (double hz) noexcept;
double upThresholdDb (float v) noexcept; // −30 .. 0
double upRatioOf (float v) noexcept;     // 1 .. 3
double hysteresisDb (Style s) noexcept;  // how far below the threshold an open gate closes

// Static gain (dB) for a steady sine of `inputDb` (the gate open state taken as reached from above when `fromAbove`).
double staticGainDb (const EffectParams& p, double inputDb, bool fromAbove = false);
} // namespace sentry

const EffectInfo& crestInfo();
const EffectInfo& prismInfo();
const EffectInfo& sentryInfo();

bool isDynKind (EffectKind kind) noexcept;
std::string paramText (EffectKind kind, int param, float v);
// The effect; with `initial` the latency already matches those parameters before the first block.
std::unique_ptr<Effect> make (EffectKind kind, const EffectParams* initial = nullptr);
} // namespace dyn
} // namespace lynx
