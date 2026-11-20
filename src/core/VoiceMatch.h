#pragma once

// Matched voicings (REA round 123): engines that reproduce, as closely as black-box measurement allows, the
// plug-ins her vocal projects use when they are missing. They are selected by a "Voicing" parameter of the LYNX
// effect that substitutes the plug-in (Ridge Strip, Hush De-esser, Compressor, Glow Leveler); the effect's own
// engine stays untouched at voicing 0. Every law here was measured on the vendors' official trials hosted
// headless (tools/fxmeasure: impulses, sine stairs, tone bursts, pink noise, her own takes) and fitted; no
// plug-in code or data was read. Evidence: docs/analise/REA_rodada123.md.

#include <array>
#include <cstdint>

#include "core/Biquad.h"

namespace lynx::vmatch
{
// ---- E-type console channel strip (Ridge Strip, voicing "E") ----
namespace echannel
{
// Controls in the units the console's own dials show (the measured behaviour follows the displayed, rounded values)
struct Settings
{
    double inputDb = 0.0, outputDb = 0.0;
    bool phase = false;
    double hpHz = 0.0;  // 0 = out, else 17 .. 350
    double lpKHz = 0.0; // 0 = out, else 19.5 .. 3
    double lfGain = 0.0, lfHz = 60.0;
    bool lfBell = false;
    double lmfGain = 0.0, lmfKHz = 0.5, lmfQ = 2.5;
    double hmfGain = 0.0, hmfKHz = 3.5, hmfQ = 1.5;
    double hfGain = 0.0, hfKHz = 8.0;
    bool hfBell = false;
    bool eqOn = true;
    double compThreshold = 0.0, compRatio = 1.0, compRelease = 0.4; // dB on the console's scale (+10 .. -20), x:1, s
    bool compFast = false;
    double expThreshold = -30.0, expRange = 0.0, expRelease = 0.2; // console dB (-30 .. +10), dB, s
    bool expFast = false, gate = false;
    bool dynOn = true;
    bool filtersBeforeDynamics = true; // "split": the filters run before the dynamics (otherwise with the EQ)
    bool dynamicsAfterEq = false;      // "dynamics to channel out"
    // round 148: the console the settings drive — 0 the E-type channel of round 123, 1 the "4K" console measured on the
    // SSL 4K E trial (REA_rodada148 §2). The 4K's own controls (ignored by model 0):
    int model = 0;
    int colour = 0;               // EQ colour: 0 Brown, 1 Black, 2 Orange
    bool pre = false;             // the mic pre: drive `micDb` (0 … 50 dB, level-compensated)
    double micDb = 0.0;
    bool vca = false;             // the Analogue VCA (the dynamics' VCA colour)
    bool autoMakeup = true;       // the compressor's automatic make-up …
    double makeupOffsetDb = 0.0;  // … and its offset (−12 … +12 dB)
    double compMix = 1.0;         // the compressor's dry / wet mix 0 … 1
    // round 165 (model 0): "EQ to Dyn S.C." — [EXEC] the filters (unless split) and the EQ leave the audio and feed only
    // the dynamics' side chain; with the filter split on, the filters stay at the input of the audio path
    bool eqToSidechain = false;
    // round 190lb (model 1, off = the old sound): the "Pink" EQ colour of the SSL 4K G, measured black-box on its
    // impulse grid (docs/analise/BLACKBOX_rodada190lb.md §2). With `pink` the four bands take the dials in the
    // original's own units (LF ±18, mids ±22, HF ±20 dB; LMF 0.2 … 2.5 kHz before ÷3, HMF 0.6 … 7 kHz before ×3) and
    // the LF / HF Bell switches do nothing (the reference's Pink bands are shelves only)
    bool pink = false, lmfDiv3 = false, hmfMult3 = false;
    // the SSL 4K G's own measured details (either colour; off = the 4K E's): its 18 dB/oct high-pass, its low-pass
    // without bilinear cramping (a matched second-order section; the dial reaches 23.8 kHz), and its output ceiling
    // (hard clip at 1.1108, both polarities, before the output trim)
    bool k4g = false;
    // round 190lb: the 4K E's own high-pass in Black / Orange, third order (18 dB/oct; its trial's K4E_ir renders — Brown
    // keeps the measured 12 dB/oct one). Off = the old 12 dB/oct high-pass for every colour
    bool e18HighPass = false;
    // round 190mt (model 0): the G-Channel EQ law (Ridge Strip's hidden gEqLaw, set only by the Waves SSL G-Channel
    // translator; 0 = the E bands above). The four bands become the G's own gain-dependent sections (proportional-Q
    // bells, shelves whose corners and slope move with the gain; tables fitted on our probes, core/SslGTables.inc) on the
    // G's ranges (LF / HF ±17 dB, LMF / HMF ±20 dB; no LF / HF bell), with its LMF ÷3 / HMF ×3 switches (lmfDiv3 / hmfMult3 above, shared with the
    // 4K G's Pink colour since the 0.1.30 rollup step 13) and flat gain
    int gLaw = 0;
};

// Q of the mid bands as the console's dial shows it, for a 0..1 dial position (3.5 at 0 .. 0.1 at 1)
double qFromDial (double v) noexcept;
double dialFromQ (double q) noexcept;

// One EQ band / filter as a digital biquad at `sampleRate` (band 0 LF, 1 LMF, 2 HMF, 3 HF). False = flat (skip it).
bool bandBiquad (int band, const Settings& s, double sampleRate, Biquad& out);
// The 3rd-order high-pass as a first-order section and a biquad
bool highPass (const Settings& s, double sampleRate, Biquad& first, Biquad& second);
bool lowPass (const Settings& s, double sampleRate, Biquad& out);

// Magnitude of the filters + EQ (dB) at `hz`
double responseDb (const Settings& s, double sampleRate, double hz);
// The compressor's static output level (dBFS peak of a sine) for a steady sine input level, from the model
double compStaticGainDb (const Settings& s, double sineDbfs);
// round 147b: the console's automatic make-up (dB) for its threshold and ratio (measured with the dynamics law; 0 at 1:1)
double compMakeupDb (const Settings& s) noexcept;

class Channel
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (float* l, float* r, int n, const Settings& s) noexcept;
    float compReductionDb() const noexcept { return lastComp; }
    float gateReductionDb() const noexcept { return lastGate; }

private:
    void design (const Settings& s) noexcept;
    double sr = 48000.0;
    Settings designed {};
    bool designedOnce = false;
    Biquad hp1, hp2, lp, bands[4], vcaHp;
    // round 165: the side chain's own copies of the filters / EQ (same coefficients, own state): [0] the gate's
    // detector (it hears the input), [1] the compressor's (it hears the compressor's output: a feedback design)
    struct ScChain { Biquad hp1, hp2, lp, bands[4]; } sc[2];
    bool hpOn = false, lpOn = false, bandOn[4] {};
    // dynamics state
    double env = 0.0, gr = 0.0, gr1 = 0.0, yl = 0.0, yr = 0.0;
    double genv = 0.0, ggr = 0.0;
    bool gateOpen = false;
    float lastComp = 0.0f, lastGate = 0.0f;
    std::uint32_t noise[2] { 0x9e3779b9u, 0x85ebca6bu }; // round 148: the 4K's VCA / mic pre hiss
};
} // namespace echannel

// ---- split-band de-esser (Hush De-esser, voicing "Classic split") ----
namespace deesser
{
struct Settings
{
    double hz = 5506.0, thresholdDb = -24.2; // the dial's frequency and threshold (-80 .. 0 dB)
    bool bandPass = false, wideband = false, listen = false;
    double outputDb = 0.0;
};

class DeEsser
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (float* l, float* r, int n, const Settings& s) noexcept;
    float reductionDb() const noexcept { return last; }
    // round 147: the block's highest detector energy in the threshold's own scale (DeEsser's Energy Detector meter)
    float detectorDb() const noexcept { return lastDet; }

private:
    void design (const Settings& s) noexcept;
    double sr = 48000.0, designedHz = -1.0;
    float lastDet = -120.0f;
    bool designedBp = false;
    Biquad sc1, sc2, bp1, bp2; // round 165: bp1 / bp2 = BandPass's low-pass after the high-pass
    double lpB = 0.0, lpA = 0.0, lpX[2] {}, lpY[2] {};
    double env = 0.0, gr = 0.0;
    float last = 0.0f;
};
} // namespace deesser

// ---- round 148: a single-vocal de-esser (Hush De-esser, "Vocal voicing"), fitted black-box on the official FabFilter
// Pro-DS trial at her two states (REA_rodada124_extras §5, REA_rodada148 §3): the band between the high-pass and the
// low-pass (4th-order each) feeds a power detector, a second detector compares that band with the signal below it, and
// the reduction only switches on for a band that is loud AND stands out (a vocal sibilant, not a steady bright tone);
// the gain follows range x (level - threshold + 4.1) / (0.1 - threshold), looked ahead; out = in (delayed) x gain
namespace vocalds
{
struct Settings
{
    double thresholdDb = -20.0, rangeDb = 6.0; // threshold -60 .. 0 dB, range 0 .. 24 dB
    double hpHz = 7000.0, lpHz = 14000.0;      // the detection band
    double lookaheadMs = 12.0;                 // 0 .. 15 ms (within the fixed 15 ms latency)
    bool lookahead = true, wideband = true, listen = false;
    // round 165x (REA_rodada165x §2) [EXEC]: the reference's Stereo Link — link 0 … 1 detects each channel on
    // (1 − link/2) own + link/2 other (1 = the mid signal, as before); linkOnly 0 … 1 = its 100 … 200 % half (that share
    // processed on the mid — linkSide: the side — signal only; Side: each channel detects on (1 − m) mid ∓ m side and
    // processes (1 − m) itself ± m side). Off = the mid signal (her states)
    bool proLink = false, linkSide = false;
    double link = 1.0, linkOnly = 0.0;
    bool gate = true; // round tp172b: false = the reference's Allround mode (no single-vocal gate, the same level law)
    int timing = 0;   // round 190iq: 1 = the Allround law's measured timing revision (detector, gain, look-ahead); Single Vocal ignores it
    // round 190js: 1 = Single Vocal's "stands out" revision (the band must stand out from the signal below it by the
    // measured margin, the gate decides a fixed delay late, the measured level law and gain timing); Allround ignores it
    int vocalRevision = 0;
};

class DeEsser
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (float* l, float* r, int n, const Settings& s) noexcept;
    int latencySamples() const noexcept { return latency; }
    float reductionDb() const noexcept { return last; }
    float detectorDb() const noexcept { return lastDet; }

private:
    void design (const Settings& s) noexcept;
    static constexpr int ring = 4096;
    double sr = 48000.0, designedHp = -1.0, designedLp = -1.0;
    int latency = 720;
    Biquad hp1, hp2, lp1, lp2, low1, low2, split1, split2;
    double eh = 0.0, qh = 0.0, ql = 0.0, sm = 0.0;
    bool open = false;
    // round 165x: the second detector lane (the right channel's blend, or the side signal)
    Biquad bhp1, bhp2, blp1, blp2, blow1, blow2;
    double beh = 0.0, bqh = 0.0, bql = 0.0, bsm = 0.0;
    // round 190js: Single Vocal's revision compares the signal above a split under the band with the signal below it
    Biquad shi1, shi2, slo1, slo2, bshi1, bshi2, bslo1, bslo2, sbody, bsbody;
    double designedSplit = -1.0;
    bool bopen = false;
    std::array<float, ring> blevel {}, bratio {};
    std::array<float, ring> dl {}, dr {}, level {}, ratio {};
    std::int64_t pos = 0;
    float last = 0.0f, lastDet = -120.0f;
};
} // namespace vocalds

// ---- electro-optical leveler (Glow Leveler, voicing "Opto classic") ----
namespace opto
{
struct Settings
{
    double gain = 40.0, peakReduction = 46.0, hiFreq = 50.0; // the dials, 0 .. 100
    bool limit = false;
    // round 152 (REA_rodada152a §2) [EXEC]: the measured Limit law (a steeper cell, a little of the input in the side
    // chain, the cell's deepest reduction) and AUTO MAKEUP (a make-up that follows the peak reduction dial); off = the
    // round-123 Limit [model] and no auto make-up
    bool measured = false, autoMakeup = false;
    // round 165w (REA_rodada165w §2) [EXEC]: the measured release (the trial's gain recovery after bursts of 0.1 … 10 s,
    // −24 … 0 dBFS); off = round 123's (its slow conductance left ≈ 0.8 dB of reduction for many seconds). Round 165x
    // (REA_rodada165x §1): with it, the measured attack (the trial's first milliseconds of a burst)
    bool measuredRelease = false;
    // round 165x (REA_rodada165x §1) [EXEC]: the peak reduction dial where AUTO MAKEUP was switched on (its make-up is
    // autoMakeupDb (peakReduction) − autoMakeupDb (makeupRef)); 46 = the round-152 reference
    double makeupRef = 46.0;
    // round 190dm [EXEC] (an FG-2A-type reference's tone-burst probes): the cell's memory — the release slows with how long
    // and how hard the cell was lit (an exposure state that charges while lit), so it recovers fast after a short burst
    // and slowly after a long one; Compress only. Off = the release laws above, sample for sample
    bool memory = false;
    // round 190fx [EXEC] (three optical references' 100 Hz / 1 kHz / 8 kHz stairs and pink stairs): the side chain's
    // weighting. 0 = the voicing's own emphasis (every project before); 1 = the even weighting those references measured
    // (much less treble emphasis, a little more bass), its 1 kHz gain the same, so every 1 kHz law fitted before holds
    int scWeighting = 0;
    // round 190lf [EXEC] (REA_rodada190lf_dyn §1): the cell law. 0 = the laws above (every project before); 1 = LYNX's
    // compact opto behaviour model fitted on our black-box probes of an opto-leveler emulation (a feedback side chain
    // through a low and a high peaking filter, a fast and a slow conductance sharing the light, an exposure that slows
    // the release, a high-frequency band that is reduced less, its own peak reduction / Hi Freq / gain laws). Uses
    // gain, peakReduction, hiFreq, limit, autoMakeup / makeupRef; the other switches above are ignored
    int cellLaw = 0;
};
double autoMakeupDb (double peakReduction) noexcept; // round 152: AUTO MAKEUP's gain (dB) at a peak reduction dial
double gainForMakeupDb (double db) noexcept;          // round 165x: the gain dial giving makeupDb (inverse)
double makeupDb (double gainKnob) noexcept;              // the gain dial's level (dB) below the threshold
double sensitivityDb (double peakReduction, double hiFreq) noexcept; // side-chain level shift vs her state (dB)

class Leveler
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (float* l, float* r, int n, const Settings& s) noexcept;
    float reductionDb() const noexcept { return last; }

private:
    double sr = 48000.0;
    double cf = 0.0, cs = 0.0, el = 0.0, mem = 0.0, ctl = 0.0, exposure = 0.0, yc[2] {}, fx1[2] {}, fy1[2] {}, tx[2][4] {}, ty[2][4] {}, xp[2] {};
    Biquad sc, sc172, scLow172; // round 123's emphasis; round 172d's (Compress with the measured release)
    Biquad scHigh190, scLow190; // round 190fx: the even weighting (Settings::scWeighting)
    double scShift190 = 0.0;    // its level shift (dB) that keeps the chain's 1 kHz gain where the emphasis put it
    double scDesignedRate = 0.0;
    // round 190lf: the cell law's state (Settings::cellLaw = 1)
    void processCell (float* l, float* r, int n, const Settings& s) noexcept;
    struct Peak2 { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {}; };
    Peak2 cellLow, cellHigh;
    double cellHiFreq = -1.0, cellRate = 0.0;
    double ccf = 0.0, ccs = 0.0, cc3 = 0.0, cexp = 0.0, cenv = 0.0, cyp[2] {}, chx[2] {}, chy[2] {};
    float last = 0.0f;
};
} // namespace opto

// ---- program-dependent peak compressor (Compressor, voicing "Vintage program") ----
namespace progcomp
{
struct Settings
{
    double thresholdDb = -15.0, ratio = 3.0, attackMs = 0.5, releaseMs = 160.0, gainDb = 0.0;
    double mix = 1.0, trimDb = 0.0; // parallel mix (the dry path is delayed with the wet one) and the trim after it
    // round 135w (the reference's switches; the fit above is ARC + Electro + Warm, her state): Manual = one release
    // time (no program-dependent slow path), Opto = the slow path's release curve inverted (slower near 0 dB of
    // reduction), Smooth = without Warm's extra reduction (ratio as shown) [model: laws not measured yet]
    bool manual = false, opto = false, smooth = false;
    // round 152 (REA_rodada152a §1) [EXEC]: the static curve measured on the trial at every ratio 1.5 … 50 (a table of
    // corrections on top of the fit above; Smooth has Warm's static curve there). Off = the round-123 / 150 curve.
    bool measuredStatic = false;
};
constexpr int lookahead = 64; // samples at 48 kHz (the original's measured delay, scaled with the rate)

class Compressor
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (float* l, float* r, int n, const Settings& s) noexcept;
    int latency() const noexcept { return delayLen; }
    float reductionDb() const noexcept { return last; }
    // round 152: the block's largest gain change, signed (negative = reduction, positive = the expansion's boost)
    float changeDb() const noexcept { return change; }

private:
    double sr = 48000.0;
    int delayLen = lookahead, pos = 0;
    std::array<float, 1024> dl {}, dr {};
    double env = 0.0, grf = 0.0, grs = 0.0;
    float last = 0.0f, change = 0.0f;
};
// round 152: the measured static correction (dB of gain added) at a ratio ≥ 1 and a level `over` dB above the threshold
double measuredStaticDelta (double ratio, double over) noexcept;

// ---- round 190mg (REA_rodada190mg_ren): the "Renaissance law" of the vintage voicing — LYNX's model of the reference's
// structure (clean-room: structure from REA, every constant fitted on our own demo-mode probes). Compressor's hidden law
// parameter selects it; only the Renaissance Compressor translator sets it.
struct RenSettings
{
    double thresholdDb = 0.0, ratio = 1.0, attackMs = 16.0, releaseMs = 160.0, gainDb = 0.0, mix = 1.0, trimDb = 0.0;
    bool manual = false, opto = false, smooth = false, limiter = true;
    // round 190mt: the Renaissance Channel's "RVox" compressor type — the one-knob vocal compressor's curve
    // (g = 1 / (1 + p (1/t − 0.1)), t = the threshold: no ratio, no floor, no bend) on this law's detector and smoothing
    // (its attack / release still apply, with the Opto mode's release shape); the ratio is ignored [EXEC]
    bool voxCurve = false;
};
// the static law: the gain's floor (a < 1 compresses, a > 1 expands), the threshold shift (dB) and the curve
double renFloor (double ratio) noexcept;
double renShiftDb (double ratio) noexcept;
// the gain (linear) of a steady peak `x` (linear) at a ratio and threshold, before the make-up
double renStaticGain (double x, double thresholdDb, double ratio) noexcept;

class Renaissance
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (float* l, float* r, int n, const RenSettings& s) noexcept;
    int latency() const noexcept { return win; }
    float reductionDb() const noexcept { return last; }
    float changeDb() const noexcept { return change; }
    // the look-ahead window (= the audio delay) at a rate: 64 samples under 64 kHz, 128 under 128 kHz, else 256
    static int windowFor (double sampleRate) noexcept;

private:
    static constexpr int cap = 512, tableSize = 256;
    double sr = 48000.0;
    int win = 64, boxA = 27, boxB = 38;
    // detector all-passes (first order) per channel
    double apc[3] {}, apx[2][3] {}, apy[2][3] {};
    // sliding maximum (monotonic queue) of the detector over the window
    std::array<double, cap> qv {};
    std::array<int, cap> qi {};
    int qh = 0, qt = 0, n0 = 0;
    // the gain path: smoothed curve gain s, its slow follower q, the limiter's gain
    double s = 1.0, q = 1.0, lim = 1.0;
    // two running boxcars over the gain (fixed point, exact) and the audio delay
    std::array<std::int64_t, cap> b1 {}, b2 {};
    std::int64_t sum1 = 0, sum2 = 0;
    int p1 = 0, p2 = 0;
    std::array<float, cap> dl {}, dr {};
    int dp = 0;
    double wetG = 1.0, dryG = 0.0;
    bool started = false;
    // release tables (rebuilt when the release, mode or curve change)
    std::array<double, tableSize> relT {}, pdrT {};
    double tRelease = -1.0;
    int tMode = -1;
    float last = 0.0f, change = 0.0f;
};
} // namespace progcomp
} // namespace lynx::vmatch

// ---- round tp172c: a modern console channel strip (Ridge Strip, "Modern" voicing) ----
// Fitted black-box on the SSL Native Channel Strip 2 trial (its renders only: impulses on a dense grid of every EQ band,
// both EQ types, the filters; sine ramps, stairs and tone bursts through the dynamics). The E type's shelves are first
// order, the G type's are second-order shelves with the dip / bump either side; every bell is a peaking section whose
// width follows the gain; the filters are a 3rd-order high-pass and a 2nd-order low-pass. The compressor is feed-forward
// (peak or RMS detector, threshold offset and slope per ratio, soft knee in RMS mode), the auto make-up a share of the
// reduction at 0 dBFS; the gate / expander shares the threshold scale, the gate has its hold and hysteresis.
namespace lynx::vmatch::strip2
{
// Controls in the reference's own units (its dials' read-outs)
struct Settings
{
    double inputDb = 0.0, outputDb = 0.0;
    bool phase = false;
    double hpHz = 0.0;  // 0 = out, else 20 .. 500 Hz
    double lpKHz = 0.0; // 0 = out, else 3 .. 27.5 kHz
    bool typeG = false; // the EQ type: E (false) or G
    double lfGain = 0.0, lfHz = 182.0;  // ±16.5 dB, 40 .. 600 Hz
    bool lfBell = false;
    double lmfGain = 0.0, lmfKHz = 0.8, lmfQ = 1.5; // ±20 dB, 0.2 .. 2 kHz, Q 0.5 .. 2.5
    double hmfGain = 0.0, hmfKHz = 2.45, hmfQ = 1.5; // ±20 dB, 0.6 .. 7 kHz
    double hfGain = 0.0, hfKHz = 7.5;  // ±20 dB, 1.5 .. 22 kHz
    bool hfBell = false;
    bool eqOn = true;
    double compRatio = 1.0, compThreshold = 10.0, compRelease = 0.1; // x:1 (>= 1e6 = infinity), -20 .. +10 dB, 0.1 .. 4 s
    bool compFast = false, compPeak = false, autoMakeup = true;
    double makeupOffsetDb = 0.0, compMix = 1.0; // -12 .. +12 dB, 0 .. 1
    bool expander = false, gateFast = false;
    double gateThreshold = 10.0, gateRange = 0.0, gateRelease = 0.1, gateHold = 0.25; // -30 .. +10 dB, 0 .. 40 dB, s, s
    bool dynOn = true;
    bool dynamicsPreEq = false; // the dynamics before the filters and EQ (default: after them)
};

// Magnitude of the filters + EQ (dB) at `hz`
double responseDb (const Settings& s, double sampleRate, double hz);
// The compressor's automatic make-up (dB, offset included; 0 with the compressor at 1:1 or auto make-up off)
double makeupDb (const Settings& s) noexcept;
// The compressor's static output level (dB) for a steady sine at `inputDb` (dBFS peak), make-up included
double compStaticDb (const Settings& s, double inputDb) noexcept;

class Strip
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (float* l, float* r, int n, const Settings& s) noexcept;
    float compReductionDb() const noexcept { return lastComp; }
    float gateReductionDb() const noexcept { return lastGate; }

private:
    void design (const Settings& s) noexcept;
    double sr = 48000.0;
    Settings designed {};
    bool designedOnce = false;
    Biquad hp1, hp2, lp, bands[4];
    bool hpOn = false, lpOn = false, bandOn[4] {};
    double env = 0.0, gr = 0.0, genv = 0.0, ggr = 0.0, holdLeft = 0.0;
    bool gateOpen = false;
    float lastComp = 0.0f, lastGate = 0.0f;
};
} // namespace lynx::vmatch::strip2
