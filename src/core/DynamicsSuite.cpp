#include "core/DynamicsSuite.h"

#include <numbers>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>

#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/LrCrossover.h"
#include "core/MinPhaseBand.h"
#include "core/MixSuite.h"
#include "core/ParametricEqLinear.h"

// REA_rodada108. Published material the designs rest on:
//  - feed-forward compressor with a log-domain level detector (branching attack / release), soft-knee gain computer:
//    D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis",
//    JAES 60(6), 2012 (§2.2 gain computer, §3 detectors, §4 auto make-up gain);
//  - look-ahead by delaying the audio against the side chain: U. Zölzer (ed.), "DAFX", 2nd ed., ch. 4.2;
//  - upward compression / expansion as mirrored gain curves of the same computer: ibid. ch. 4.2.2;
//  - Linkwitz-Riley crossovers (two cascaded Butterworth sections per side): S. Linkwitz, "Active Crossover
//    Networks for Non-coincident Drivers", JAES 24(1), 1976;
//  - waveshaping families (smooth odd polynomial, asymmetric clipping, even-order rectifier terms) and their
//    harmonic signatures: DAFX ch. 4.3, J. Pakarinen / D. T. Yeh, CMJ 33(2), 2009.
// Every constant that shapes a "style" was fitted by us to the black-box measurements (MEASURE_fabfilter.md); the
// fitted values and their residuals are in REA_rodada108.md.

namespace lynx::dyn
{
namespace
{
constexpr double pi = 3.141592653589793;

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-6)); }
inline double logMap (float v, double lo, double hi) noexcept { return lo * std::pow (hi / lo, (double) std::clamp (v, 0.0f, 1.0f)); }
inline float logValue (double x, double lo, double hi) noexcept { return (float) std::clamp (std::log (x / lo) / std::log (hi / lo), 0.0, 1.0); }
inline double lin (float v, double lo, double hi) noexcept { return lo + (hi - lo) * (double) std::clamp (v, 0.0f, 1.0f); }
inline bool on (float v) noexcept { return v >= 0.5f; }
inline double coef (double ms, double sr) noexcept { return ms <= 0.0 ? 0.0 : std::exp (-1.0 / (ms * 0.001 * sr)); }
inline int pick (float v, int n) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * (float) (n - 1)), 0, n - 1); }

// the shared ratio law: 1 .. 100 : 1, 0.5 ≈ 2.7 : 1, 0.6 ≈ 4.5 : 1, 0.75 ≈ 11 : 1
double ratioLaw (float v) noexcept { return std::exp (std::log (100.0) * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 2.2)); }
float ratioLawValue (double r) noexcept { return (float) std::clamp (std::pow (std::log (std::max (1.0, r)) / std::log (100.0), 1.0 / 2.2), 0.0, 1.0); }

// Giannoulis et al. eq. (4): reduction (dB, ≥ 0) for an overshoot `o` (dB above the threshold), slope s = 1 − 1/R
double kneeCurve (double o, double s, double knee) noexcept
{
    if (2.0 * o <= -knee)
        return 0.0;
    if (knee > 0.0 && 2.0 * std::abs (o) < knee)
        return s * (o + knee * 0.5) * (o + knee * 0.5) / (2.0 * knee);
    return s * o;
}

float blockPeak (const float* l, const float* r, int n) noexcept
{
    float p = 0.0f;
    for (int i = 0; i < n; ++i)
        p = std::max (p, std::max (std::abs (l[i]), std::abs (r[i])));
    return std::min (p, 1.0f);
}

// Stereo delay line, fixed capacity (no allocation after construction).
struct StereoDelay
{
    static constexpr int capacity = 8192; // 20 ms look-ahead at 192 kHz + the oversampler, with room
    std::array<float, capacity> l {}, r {};
    int pos = 0, len = 0;
    void setLength (int n) noexcept
    {
        n = std::clamp (n, 0, capacity - 1);
        if (n != len)
        {
            len = n;
            reset();
        }
    }
    void reset() noexcept { l.fill (0.0f); r.fill (0.0f); pos = 0; }
    void process (float& a, float& b) noexcept
    {
        if (len == 0)
            return;
        l[(std::size_t) pos] = a;
        r[(std::size_t) pos] = b;
        auto rd = pos - len;
        if (rd < 0)
            rd += capacity;
        a = l[(std::size_t) rd];
        b = r[(std::size_t) rd];
        if (++pos == capacity)
            pos = 0;
    }
};

// mono float delay (the side chain's gain against the look-ahead)
struct MonoDelay
{
    static constexpr int capacity = 8192;
    std::array<float, capacity> d {};
    int pos = 0, len = 0;
    void setLength (int n) noexcept { len = std::clamp (n, 0, capacity - 1); }
    void reset() noexcept { d.fill (0.0f); pos = 0; }
    float process (float x) noexcept
    {
        if (len == 0)
            return x;
        d[(std::size_t) pos] = x;
        auto rd = pos - len;
        if (rd < 0)
            rd += capacity;
        const auto y = d[(std::size_t) rd];
        if (++pos == capacity)
            pos = 0;
        return y;
    }
};

// first-order DC blocker (stereo)
struct DcBlock
{
    double a = 0.9995, x1[2] {}, y1[2] {};
    void set (double sr) noexcept { a = std::exp (-2.0 * pi * 5.0 / sr); }
    void reset() noexcept { x1[0] = x1[1] = y1[0] = y1[1] = 0.0; }
    float process (float x, int c) noexcept
    {
        const auto y = (double) x - x1[c] + a * y1[c];
        x1[c] = x;
        y1[c] = y;
        return (float) y;
    }
};

struct LiveMeters
{
    std::array<std::atomic<float>, 14> v {};
    float read (int k) const noexcept { return k >= 0 && k < 14 ? v[(std::size_t) k].load (std::memory_order_relaxed) : 0.0f; }
    void set (int k, float x) noexcept { v[(std::size_t) k].store (x, std::memory_order_relaxed); }
};

std::string fmt (const char* f, double v)
{
    char t[48];
    std::snprintf (t, sizeof (t), f, v);
    return t;
}

std::string hzText (double f)
{
    return f >= 1000.0 ? fmt ("%.2f kHz", f / 1000.0) : fmt ("%.0f Hz", f);
}

std::string msText (double ms)
{
    if (ms >= 1000.0)
        return fmt ("%.2f s", ms / 1000.0);
    if (ms >= 100.0)
        return fmt ("%.0f ms", ms);
    if (ms >= 10.0)
        return fmt ("%.1f ms", ms);
    return fmt ("%.2f ms", ms);
}

std::string ratioText (double r) { return r >= 99.5 ? "100 : 1" : fmt (r < 10.0 ? "%.2f : 1" : "%.1f : 1", r); }
} // namespace

// ================================================================== Crest Compressor

namespace crest
{
Style styleOf (float v) noexcept { return (Style) pick (v, styles); }
Style styleOf (const EffectParams& p) noexcept
{
    const auto v = pick (p[voicing], voicings);
    return v > 0 ? (Style) (styles + v - 1) : styleOf (p[style]);
}
float styleValue (Style s) noexcept { return (int) s < styles ? (float) (int) s / (float) (styles - 1) : styleValue (Style::allround); }
float voicingValue (Style s) noexcept { return (int) s < styles ? 0.0f : (float) ((int) s - styles + 1) / (float) (voicings - 1); }
double thresholdDb (float v) noexcept { return lin (v, -60.0, 0.0); }
// round tp172b6 [EXEC] (Pro-C 3, Clean, 1 kHz stair, threshold -30, knee 0): 24 -> 30 dB over adds 5.96 dB at the very top,
// 5.89 dB at 91.9 : 1, so the top is a true limit whatever its parameter text says (100.00:1). Only the exact top changes.
double ratioOf (float v) noexcept { return v >= 1.0f ? 1.0e9 : ratioLaw (v); }
float ratioValue (double r) noexcept { return ratioLawValue (r); }
double kneeDb (float v) noexcept { return lin (v, 0.0, 72.0); }
double rangeDb (float v) noexcept { return lin (v, 0.0, 60.0); }
double attackMs (float v) noexcept { return logMap (v, 0.005, 250.0); }
float attackValue (double ms) noexcept { return logValue (std::clamp (ms, 0.005, 250.0), 0.005, 250.0); }
double releaseMs (float v) noexcept { return logMap (v, 10.0, 2500.0); }
float releaseValue (double ms) noexcept { return logValue (std::clamp (ms, 10.0, 2500.0), 10.0, 2500.0); }
double holdMs (float v) noexcept { return 500.0 * (double) v * (double) v; }
float holdValue (double ms) noexcept { return (float) std::sqrt (std::clamp (ms / 500.0, 0.0, 1.0)); }
double lookaheadMs (float v) noexcept { return lin (v, 0.0, 20.0); }
double maxLookaheadMs (float v) noexcept
{
    static constexpr double ms[] { 0.0, 1.0, 2.0, 5.0, 10.0, 20.0 };
    return ms[pick (v, 6)];
}
Character characterOf (float v) noexcept { return (Character) pick (v, 4); }
double driveDb (float v) noexcept { return lin (v, -24.0, 24.0); }
double scLevelDb (float v) noexcept { return lin (v, -36.0, 36.0); }
int oversamplingOf (float v) noexcept { return 1 << pick (v, 4); }
double panOf (float v) noexcept { return std::clamp ((double) v * 2.0 - 1.0, -1.0, 1.0); }
double trimDb (float v) noexcept { return lin (v, -36.0, 36.0); }
LinkMode linkModeOf (float v) noexcept { return (LinkMode) pick (v, linkModes); }
Trigger triggerOf (float v) noexcept { return (Trigger) pick (v, 3); }
double triggerBeats (float v) noexcept { return 4.0 / (double) (1 << pick (v, 5)); }
double triggerRateOf (float v) noexcept { return std::pow (2.0, 2.0 * (double) std::clamp (v, 0.0f, 1.0f) - 1.0); }
double triggerLengthOf (float v) noexcept { return (double) std::clamp (v, 0.0f, 1.0f); }
int triggerChannelOf (float v) noexcept
{
    const auto k = (int) std::lround (std::clamp (v, 0.0f, 1.0f) * 128.0f);
    return k <= 0 ? -1 : k - 1;
}
// [EXEC] Pro-C 3 hosted at 120 BPM, a steady sine under the threshold: with Host Sync the gain reduction starts on
// every note × rate (1/4 at 100 % = every 500 ms, 50 % = 250 ms, 200 % = 1 s; 1/8 at 100 % = 250 ms) and holds for
// Length of that period (10 % = 50 ms, 50 % = 250 ms); its depth follows a 0 dBFS key (threshold −18 / −40 / 0 dB,
// ratio 4:1, knee 18 dB: 14 / 32 / 2 dB of reduction)
float hostTrigger (const EffectParams& p, double beats) noexcept
{
    const auto period = triggerBeats (p[triggerSync]) * triggerRateOf (p[triggerRate]);
    if (period <= 0.0 || beats < 0.0)
        return 0.0f;
    const auto phase = beats / period - std::floor (beats / period);
    return phase < triggerLengthOf (p[triggerLength]) ? 1.0f : 0.0f;
}
float linkModeValue (LinkMode m) noexcept { return (float) (int) m / (float) (linkModes - 1); }
double dryDb (float v) noexcept
{
    if (v <= 0.0f)
        return -120.0;
    if (v >= 0.25f)
        return 72.0 * (double) v - 36.0;
    return -18.0 + 40.0 * std::log10 ((double) v / 0.25); // amplitude ∝ v² below −18 dB
}
float dryValue (double db) noexcept
{
    if (db <= -119.0)
        return 0.0f;
    if (db >= -18.0)
        return (float) std::clamp ((db + 36.0) / 72.0, 0.25, 1.0);
    return (float) (0.25 * std::pow (10.0, (db + 18.0) / 40.0));
}
double meterRangeDb (float v) noexcept
{
    static constexpr double r[] { 9.0, 18.0, 36.0, 72.0, 90.0 };
    return r[pick (v, 5)];
}
double autoThresholdDb (double knobDb, double programRmsDb) noexcept
{
    return std::clamp (programRmsDb + knobDb + 18.0, -60.0, 0.0);
}

namespace
{
// The style families (fitted to MEASURE_fabfilter §2, see REA_rodada108 §3): time-constant scales of the level
// detector (τ = scale × knob), the knee every curve gets on top of the knob, and the curve family.
enum class Curve { downward, upward, twoWay, vocal, variMu, twoWayClassic, variMuClassic, console, fet };
struct StyleDef
{
    double attackScale;  // detector attack τ = scale × knob
    double releaseScale; // detector release τ = scale × 200 ms × (knob / 200 ms)^releaseExp
    double releaseExp;   // how much the release follows its knob (1 = proportional, 0 = fixed)
    double kneeAdd, kneeScale; // knee = add + scale × knob
    Curve curve;
    double windowMs;     // > 0: the level is the peak over this window (a ripple-free detector for fast releases)
    // round tp172b2 [EXEC] (Pro-C 3 trial, 1 kHz stairs over a ratio × knee grid per style): the downward curve's
    // threshold offset (dB, + = the curve starts later), its slope relative to the ratio's 1 − 1/R, and a knee that
    // grows with that slope (dB per unit of slope) on top of add + scale × knob
    double offsetDb = 0.0, slopeScale = 1.0, kneeSlope = 0.0;
    // the detector never follows faster than this (ms; round tp172b measured ~1 ms on Clean / Allround / Opto)
    double attackFloorMs = 0.002;
    // 0: the level is smoothed (attack / release on the detector level, then the curve); 1: the curve's gain is
    // smoothed instead (the time constants act on the gain change, whatever the ratio); in between, a blend
    double gainBlend = 0.0;
    // two-way: the fixed gain (dB per unit of slope) and the upward part's slope under threshold − 6 dB
    double baseDb = 0.0, upSlope = 0.0;
};
constexpr StyleDef styleDefs[allStyles] {
    { 0.334, 0.850, 1.080, 5.30, 0.607, Curve::downward, 0.0, 1.09, 1.000, 5.68, 0.464, 1.00, 0.00, 0.000 }, // clean
    { 0.796, 0.288, 0.814, 18.87, 0.360, Curve::downward, 0.0, 0.00, 1.000, 2.88, 1.616, 1.00, 0.00, 0.000 }, // allround
    { 0.310, 0.580, 0.722, 6.21, 0.800, Curve::downward, 0.0, 0.00, 1.000, 0.00, 0.002, 0.71 }, // punch
    // round trial187b [EXEC]: upward and classic refitted (tp172b's crestfit3, 5 ms gain trajectories) on tp172b's bank plus
    // the trial's new programme captures at threshold −24 / ratio 4 / auto gain off (stair, bursts, drums, music, vocal):
    // upward 2.85 → 2.60 dB rms over 68 jobs (above the threshold it held −6.2 dB where the trial holds −2.8), classic
    // 2.28 → 1.67 dB over 80 jobs (its release was too fast: −3.3 dB where the trial is still at −5.4 on the stair's way
    // down; the attack scale stays round 108's, whose 50 % attack time it measured); both now smooth the gain rather
    // than the level (gainBlend 1)
    { 0.000, 1.787, 0.496, 0.96, 0.000, Curve::upward, 0.0, -3.84, 0.812, 0.00, 0.393, 1.00, -0.80, 0.000 }, // upward
    // round 190v [EXEC] (trial190k's threshold × ratio × knee × attack grid and release sweep, 1 kHz / pink stairs and
    // level steps): two-way and vari-mu have their own measured laws (twoWayLaw / variMuLaw below; of this row they use
    // only the attack floor, vari-mu also the attack / release scales, which stay round 108's)
    { 0.120, 0.190, 1.000, 0.00, 1.000, Curve::twoWay, 0.0, 0.00, 1.000, 0.00, 0.002, 0.00, 0.00, 0.000 }, // two-way
    { 0.335, 0.650, 0.980, 0.00, 1.000, Curve::variMu, 0.0, 0.00, 1.000, 0.00, 0.300, 0.00, 0.00, 0.000 }, // vari-mu
    { 0.300, 0.555, 0.682, 6.11, 0.242, Curve::downward, 0.0, 0.29, 1.000, 5.18, 0.002, 1.00, 0.00, 0.000 }, // classic (round trial187b; attack as round 108 measured it)
    { 0.169, 0.789, 0.880, 7.27, 0.722, Curve::downward, 0.0, 0.77, 1.000, 2.88, 0.750, 0.39 }, // opto
    { 1.611, 0.712, 0.000, 8.40, 0.000, Curve::vocal, 0.0, -0.17, 3.000, 0.00, 0.002, 0.96, 0.00, 0.000 }, // vocal
    { 0.240, 3.570, 0.190, 0.00, 1.000, Curve::downward, 0.0, 0.00, 1.000, 0.00, 0.002, 0.00, 0.00, 0.000 }, // bus (round 108's fit)
    { 0.080, 2.360, 1.000, 0.00, 1.000, Curve::downward, 0.0, 0.00, 1.000, 0.00, 0.002, 0.00, 0.00, 0.000 }, // pumping (round 108's fit)
    { 0.878, 0.102, 0.750, 17.92, 0.176, Curve::downward, 0.0, 1.86, 1.000, 28.04, 3.320, 1.00, 0.00, 0.000 }, // smooth (voicing)
    { 0.570, 0.223, 0.990, 13.98, 0.488, Curve::downward, 0.0, 0.00, 1.000, 4.61, 1.000, 1.00, 0.0, 0.0 }, // op-el (voicing)
    { 0.023, 3.667, 0.190, 1.69, 0.808, Curve::downward, 0.0, -0.77, 1.000, 0.00, 3.165, 0.83, 0.00, 0.000 }, // mastering (voicing)
};

// round 190v: the two-way and vari-mu laws of projects saved before that round (round 108's fits; the `law` parameter
// off). They render exactly as they did.
constexpr StyleDef classicDefs[2] {
    { 0.235, 0.010, 0.000, 0.00, 1.000, Curve::twoWayClassic, 1.3, 0.00, 0.808, 0.00, 0.002, 0.00, -5.75, 0.473 },
    { 0.335, 0.650, 0.980, 0.00, 1.000, Curve::variMuClassic, 0.0, 0.00, 1.000, 0.00, 0.002, 0.00, 0.00, 0.000 },
};
// the definition a style runs with: its own, or the classic one of two-way / vari-mu when the measured law is off
const StyleDef& defOf (Style st, bool measured) noexcept
{
    if (! measured && st == Style::twoWay)
        return classicDefs[0];
    if (! measured && st == Style::variMu)
        return classicDefs[1];
    return styleDefs[(int) st];
}
// round 190kt: the Bus style's console law (the hidden busLaw parameter); its own detector in Crest::detect
constexpr StyleDef consoleDef { 1.0, 1.0, 1.0, 0.0, 1.0, Curve::console, 0.0, 0.0, 1.0, 0.0, 0.002, 0.0, 0.0, 0.0 };
bool consoleLaw (const EffectParams& p) noexcept { return styleOf (p) == Style::bus && on (p[busLaw]); }
// round 190ky: the FET law (the hidden fetLaw parameter, any style); its own detector in Crest::detect
constexpr StyleDef fetDef { 1.0, 1.0, 1.0, 0.0, 1.0, Curve::fet, 0.0, 0.0, 1.0, 0.0, 0.002, 0.0, 0.0, 0.0 };
int fetVoiceOf (const EffectParams& p) noexcept { return p[fetLaw] > 0.0f ? std::clamp ((int) std::lround (p[fetLaw] * 16.0f) - 1, 0, fetVoices - 1) : -1; }
// round 190kw's revision 1 (hidden); the console law (190kt) and the FET law (190ky) take precedence when on (rollup 0.1.30
// step 8: a translated SSLComp / CLA-76 state keeps revision 0 anyway; this keeps a hand-set mix on those laws)
bool revisionOn (const EffectParams& p) noexcept { return on (p[revision]) && ! consoleLaw (p) && fetVoiceOf (p) < 0; }
const StyleDef& defOf (const EffectParams& p) noexcept
{
    if (fetVoiceOf (p) >= 0)
        return fetDef;
    return consoleLaw (p) ? consoleDef : defOf (styleOf (p), on (p[law]));
}

// ---- round 190ky: the FET law (REA_rodada190ky §2–§3) ----
// LYNX's own model of a FET feedback limiter's structure, learnt by REA of the Waves CLA-76 DSP (clean-room: the
// structure only; the vendor's transfer tables and time-constant tables are not read or used — every constant below is
// fitted on our own black-box renders, /mnt/e/lynx-work/waves190ky/lab):
//   - FEEDBACK detection: the detector hears the gained signal (the input level × the gain applied now), full-wave;
//   - three parallel peak followers (attack / release one-poles) summed with weights into a control level (dB);
//   - the gain (dB) = −S1·W1·softplus((L − T1) / W1) − S2·W2·softplus((L − T2) / W2), floored at −range: a knee and
//     a slope per term (a hard-knee feedback loop of slope S settles at ratio 1 + S); the second term bends the top.
// Voicing = two revisions × the four ratio buttons and all buttons; the knobs give T1 (threshold), S1 (ratio − 1), W1
// (knee) and the followers' attack / release times, each follower at its fitted multiples of them.
struct FetVoiceFit
{
    double t2, w2, s2;                // the second term: its knee relative to T1 (dB), width (dB), slope
    double a1, a2, a3, r1, r2, r3;    // each follower's attack and release as multiples of the knobs' times
    double w1, w2f;                   // the first / second follower's weight (the third gets the rest)
};
constexpr FetVoiceFit fetVoiceFits[fetVoices] {
    // Blacky 20 / 12 / 8 / 4 : 1, all buttons; Bluey 20 / 12 / 8 / 4 : 1, all buttons [EXEC fits, lab/fit76s.py, fit76t.py]
// fit76:begin (FETFITS: lab/gen76.py)
    { 12.4297, 0.4157, 0.03962, 0.082994, 2.7665, 1.0, 6.4919, 201.248, 1.0, 0.0386, 0.9614 }, // Blacky 20
    { 26.2217, 2.4921, 1.08445, 0.061288, 2.0429, 1.0, 4.6520, 144.212, 1.0, 0.0535, 0.9465 }, // Blacky 12
    { 13.4436, 1.3593, 1.59831, 0.042060, 1.4020, 1.0, 3.0222, 93.689, 1.0, 0.0811, 0.9189 }, // Blacky 8
    { 34.7504, 9.6349, -0.02494, 0.013129, 1.0000, 1.0, 2.0000, 18.214, 1.0, 0.0541, 0.9459 }, // Blacky 4
    { 18.1440, 8.0353, 0.01076, 0.075075, 1.0000, 1.0, 2.0000, 14.748, 1.0, 0.0944, 0.9056 }, // Blacky A
    { 1.6568, 2.2631, 0.28439, 0.123429, 4.1143, 1.0, 9.9192, 307.494, 1.0, 0.0254, 0.9746 }, // Bluey 20
    { 14.9234, 4.0104, 0.66515, 0.079247, 2.6416, 1.0, 6.1743, 191.402, 1.0, 0.0406, 0.9594 }, // Bluey 12
    { 27.0542, 0.8988, 1.08752, 0.048540, 1.6180, 1.0, 3.5715, 110.716, 1.0, 0.0691, 0.9309 }, // Bluey 8
    { 38.8183, 18.4511, -0.23685, 0.012877, 0.9808, 1.0, 1.9512, 17.770, 1.0, 0.0554, 0.9446 }, // Bluey 4
    { 23.2384, 2.4234, 0.20916, 0.077045, 1.0262, 1.0, 2.0573, 15.170, 1.0, 0.0919, 0.9081 }, // Bluey A
// fit76:end
};
double fetSoftplus (double z) noexcept { return z > 30.0 ? z : std::log1p (std::exp (z)); }
double fetLawDb (double levelDb, double t1, double w1, double s1, const FetVoiceFit& v, double range) noexcept
{
    const auto t2 = t1 + v.t2;
    const auto g = -s1 * w1 * fetSoftplus ((levelDb - t1) / w1) - v.s2 * v.w2 * fetSoftplus ((levelDb - t2) / v.w2);
    return std::clamp (g, -range, 0.0);
}
// the wet path's static tone under the FET law: a first-order low shelf and a first-order high shelf (bilinear,
// prewarped), fitted on the third-octave balance of refcap182's CLA-76 programme against LYNX without it (−0.35 dB at
// 60 Hz, −0.12 dB at 200 Hz; the probes' low-level response: −0.37 / −0.25 dB) [EXEC: lab/tone76.py, lab/fittone.py]
constexpr double fetLowShelfDb = -0.423, fetLowShelfHz = 131.0, fetHighShelfDb = -0.816, fetHighShelfHz = 16906.0;
// round 190mg: the output amplifier (the hidden fetAmp parameter) on the signal s at the amplifier's reference level
// (0 dBFS there = 0 dBFS at the Output knob's −30 mark): z = s + q(n) + lead·(q(n) − q(n − 1)), q = a·s² + b·s·|s| — the
// even part H2 (and a DC part, kept as the reference keeps it), the odd part H3 / H5 (−9 dB under H2, both 1 dB per dB),
// led by lead samples at 48 kHz (the harmonics' measured phase lead: +14° / +20° / +34° at 2 / 3 / 5 kHz). Fitted on the
// complex H2 / H3 / H5 of the Comp Off stairs at Input −36 … 0 (below the clip): 0.2 dB / 1 dB / 1 dB, the phases within
// 5° [EXEC: waves3_190mg/dyn/lab/ampfit.py]. The amplifier's clip (above about −11 dBFS at the reference level) is not modelled.
constexpr double fetAmpA = 0.139751, fetAmpB = 0.116562, fetAmpLead = 1.10438;
struct FetTone
{
    double b0[2] {}, b1[2] {}, a1[2] {};
    double x1[2][2] {}, y1[2][2] {};
    void set (double rate) noexcept
    {
        for (int k = 0; k < 2; ++k)
        {
            const bool low = k == 0;
            const auto g = std::pow (10.0, (low ? fetLowShelfDb : fetHighShelfDb) / 20.0);
            const auto hz = std::min (low ? fetLowShelfHz : fetHighShelfHz, 0.45 * rate);
            const auto w = std::tan (pi * hz / rate); // H(s) = (s + w g) / (s + w) low, (g s + w) / (s + w) high
            const auto nb0 = low ? 1.0 + w * g : g + w, nb1 = low ? w * g - 1.0 : w - g, d0 = 1.0 + w, d1 = w - 1.0;
            b0[k] = nb0 / d0;
            b1[k] = nb1 / d0;
            a1[k] = d1 / d0;
        }
    }
    void reset() noexcept { for (auto& c : x1) c[0] = c[1] = 0.0; for (auto& c : y1) c[0] = c[1] = 0.0; }
    float process (float v, int c) noexcept
    {
        double x = v;
        for (int k = 0; k < 2; ++k)
        {
            const auto y = b0[k] * x + b1[k] * x1[c][k] - a1[k] * y1[c][k];
            x1[c][k] = x;
            y1[c][k] = y;
            x = y;
        }
        return (float) x;
    }
};

// ---- round 190kt: the console law (REA_rodada190kt §2–§3) ----
// LYNX's own model of a console bus compressor's structure, learnt by REA of the Waves SSLComp DSP (clean-room: the
// structure only; every constant below is fitted on our own black-box renders, /mnt/e/lynx-work/rea190kt/lab):
//   - FEEDBACK detection: the detector hears the output, the input level × the gain it is applying now, clipped at
//     full scale, in the log domain over a small floor;
//   - the over-threshold amount o (nats) through the loop gain K = ratio − 1 and a rational soft knee
//     g = y² / (y − k), y = −K·o (k = the knee in nepers: twice the knee knob's dB in nepers, so 72 dB reaches 16.6): quadratic onset, asymptote y + k (a hard-knee feedback loop
//     settles at ratio 1 + K);
//   - RC ballistics on the log gain: the attack charges the timing capacitor towards that target while the release
//     resistor discharges it (state += a·(target − state) − r·state); the release lets the gain recover towards 0 dB,
//     never past the target. Auto release: a second, larger capacitor in series with its own slow discharge path (the
//     gain is the sum of the two voltages), so short peaks recover fast and a long reduction slowly.
constexpr double consoleFloor = 1.0e-3;          // the detector's floor (linear, −60 dBFS) [EXEC fit, lab/fitstatic2.py]
constexpr double consoleAutoCapRatio = 0.1697;     // Auto: the slow capacitor charges this much slower [EXEC fit, lab/fitdyn.py]
constexpr double consoleAutoSlowMs = 2527.0;     // Auto: the slow capacitor's discharge time constant [EXEC fit]
constexpr double nepersPerDb = 0.11512925464970228; // ln 10 / 20
double consoleTarget (double over, double loop, double kneeN) noexcept
{
    if (over <= 0.0)
        return 0.0;
    const auto y = -loop * over;
    return y * y / (y - kneeN);
}

double releaseTau (const StyleDef& d, double knobMs) noexcept
{
    return d.releaseScale * 200.0 * std::pow (knobMs / 200.0, d.releaseExp);
}

// ---- round 190v: the two-way and vari-mu laws, fitted black-box to the reference's static grid (thresholds −48 … −12 dB,
// ratios 2 / 4 / 10, knees 0 … 72 dB, attacks 0.25 / 10 / 100 ms: 1 kHz stairs −60 … 0 dBFS; round 108's −18 dB
// stairs are one line of the same grid) ----

// a value over the slope s = 1 − 1/R: straight lines through (0, 0), the three measured ratios and a top at s = 1
double overSlope (double s, double v2, double v4, double v10, double top) noexcept
{
    if (s <= 0.5)
        return v2 * s / 0.5;
    if (s <= 0.75)
        return v2 + (v4 - v2) * (s - 0.5) / 0.25;
    if (s <= 0.9)
        return v4 + (v10 - v4) * (s - 0.75) / 0.15;
    return v10 + (top - v10) * (std::min (s, 1.0) - 0.9) / 0.1;
}

// Two-way: a fixed gain P, upward compression (slope `up`) under the lower corner and downward compression (slope
// `down`) over the upper one, both corners softened over the same knee `width`. Measured: the corners sit 6.3 dB
// under and 3.0 dB over the threshold; the slopes are 0.24 / 0.35 / 0.49 up and 0.25 / 0.60 / 0.80 down at ratio
// 2 / 4 / 10; the knee is the knob itself (2.9 dB at 0); the fixed gain falls as the threshold rises above −36 dB
// (nothing below it) and faster with a slow attack: P = P36 − s · (0.172 + 0.172 · log10 (1 + attack / 1 ms)) ·
// (threshold + 36). 186 curves: 0.08 dB rms, 0.7 dB worst.
struct TwoWayLaw
{
    double base, up, down, lowCorner, highCorner, width;
};
//
// round 190al [EXEC] (the same grid's 100 Hz and 8 kHz stairs and its pink-noise stairs): the reference's two-way works
// in three bands. Pink noise comes out with one gain under about 90 Hz, another up to 2.5 kHz and a third above (up
// to 4 dB apart), the steps between them following a fourth-order Linkwitz-Riley split (2.5 kHz: within 0.02 of the
// transition), and its phase turns 360 degrees around 88 Hz and again around 2.5 kHz (measured −57 / −145 / −205 /
// −257 degrees at 30 / 70 / 100 / 140 Hz and −480 / −534 / −587 at 1.8 / 2.5 / 3.5 kHz: two such splits in series).
// Each band has the law above with its own constants: the 1 kHz stairs are the mid band's (the table of round 190v);
// the high band's were fitted on the 8 kHz stairs (162 curves, 0.20 dB rms: its downward part starts 5.3 dB lower
// and is steeper, 0.48 / 0.71 / 0.83); the low band's on the 100 Hz stairs, a tone that sits in both the low
// (−8.5 dB) and the mid band (−4.1 dB).
struct TwoWayBand
{
    double lowCorner, highCorner, up2, up4, up10, upTop, down2, down4, down10, downTop, base2, base4, base10, baseTop,
           baseThr, baseAtt, width0, widthKnee;
};
constexpr int twoWayBandCount = 3; // mid, low, high
constexpr double twoWayLowHz = 88.0, twoWayHighHz = 2500.0;
// round 190mj: the reference's own split frequencies (2^6.462166786 Hz, 2^11.287712097 Hz; REA_rodada190mj), TTM under revision 1
constexpr double ttmLowHz = 88.167, ttmHighHz = 2500.0;
constexpr TwoWayBand twoWayBands[twoWayBandCount] {
    { -7.2144, 3.0058, 0.2458, 0.3632, 0.5006, 0.5935, 0.2497, 0.6238, 0.8442, 0.9853, -0.0066, -0.0137, -0.0487, -0.0617, 0.1750, 0.1680, 0.0, 1.0112 }, // mid
    { -6.7508, 0.4605, 0.2400, 0.3538, 0.4901, 0.5810, 0.2523, 0.6282, 0.8564, 0.9994, 0.0595, 0.0678, 0.0614, 0.0778, 0.1724, 0.1717, 1.8325, 0.9783 }, // low
    { -6.9355, -1.6324, 0.2469, 0.3653, 0.5069, 0.6009, 0.5046, 0.7605, 0.9150, 1.0000, -0.0417, -0.0635, -0.1086, -0.1377, 0.1671, 0.1755, 0.0, 1.0077 }, // high
};
TwoWayLaw twoWayLaw (double thr, double r, double knee, double attMs, int band = 0) noexcept
{
    const auto& b = twoWayBands[band];
    const auto s = 1.0 - 1.0 / r;
    TwoWayLaw w;
    w.up = overSlope (s, b.up2, b.up4, b.up10, b.upTop);
    w.down = overSlope (s, b.down2, b.down4, b.down10, b.downTop);
    w.base = overSlope (s, b.base2, b.base4, b.base10, b.baseTop)
             - s * (b.baseThr + b.baseAtt * std::log10 (1.0 + std::max (0.0, attMs))) * (std::max (thr, -36.0) + 36.0);
    w.lowCorner = thr + b.lowCorner;
    w.highCorner = thr + b.highCorner;
    w.width = std::sqrt (b.width0 * b.width0 + b.widthKnee * b.widthKnee * knee * knee);
    return w;
}

// the three bands of a signal: fourth-order Linkwitz-Riley splits at 88 Hz and 2.5 kHz (their sum is the input
// through the two splits' all-pass phase, as the reference's output is)
struct TwoWaySplit
{
    Biquad lowLp[2], lowHp[2], highLp[2], highHp[2], lowAp;
    // (round 190mj: the splits' frequencies as parameters; revision 0 keeps 88 Hz / 2.5 kHz)
    void set (double sr, double lowHz = twoWayLowHz, double highHz = twoWayHighHz) noexcept
    {
        constexpr double q = 0.7071067811865476, pi = 3.141592653589793;
        for (int k = 0; k < 2; ++k)
        {
            lowLp[k].filter (sr, 0, lowHz, q);
            lowHp[k].filter (sr, 1, lowHz, q);
            highLp[k].filter (sr, 0, highHz, q);
            highHp[k].filter (sr, 1, highHz, q);
        }
        const auto w = 2.0 * pi * std::min (highHz, sr * 0.45) / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        lowAp.set (1.0 - alpha, -2.0 * cw, 1.0 + alpha, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }
    void reset() noexcept
    {
        for (auto* set : { lowLp, lowHp, highLp, highHp })
            for (int k = 0; k < 2; ++k)
                set[k].reset();
        lowAp.reset();
    }
    // round 190mj: one sample of one channel → out[0] mid, out[1] low, out[2] high (the same filters as process)
    void split1 (float v, int ch, float* out) noexcept
    {
        out[1] = lowAp.process (lowLp[1].process (lowLp[0].process (v, ch), ch), ch);
        const auto hp = lowHp[1].process (lowHp[0].process (v, ch), ch);
        out[0] = highLp[1].process (highLp[0].process (hp, ch), ch);
        out[2] = highHp[1].process (highHp[0].process (hp, ch), ch);
    }
    // a block of both channels → band[0] mid, band[1] low, band[2] high (each band[b][channel] holds n samples)
    template <typename Bands>
    void process (const float* l, const float* r, int n, Bands& band) noexcept
    {
        const auto copy = [n] (const float* a, const float* b, auto& to) {
            std::copy (a, a + n, to[0].data());
            std::copy (b, b + n, to[1].data());
        };
        copy (l, r, band[1]);
        for (auto* f : { &lowLp[0], &lowLp[1], &lowAp })
            f->processBlock (band[1][0].data(), band[1][1].data(), n);
        copy (l, r, band[2]);
        for (auto* f : { &lowHp[0], &lowHp[1] })
            f->processBlock (band[2][0].data(), band[2][1].data(), n);
        copy (band[2][0].data(), band[2][1].data(), band[0]);
        for (auto* f : { &highLp[0], &highLp[1] })
            f->processBlock (band[0][0].data(), band[0][1].data(), n);
        for (auto* f : { &highHp[0], &highHp[1] })
            f->processBlock (band[2][0].data(), band[2][1].data(), n);
    }
};
// the lift is measured down to 48 dB under its corner; below that it holds, and it never starts on silence (under
// −90 dBFS: the original's first hit after digital silence comes out turned down, never lifted — round tp172b3)
constexpr double twoWayLiftSpanDb = 48.0;
double twoWayGain (const TwoWayLaw& w, double x) noexcept
{
    const auto up = x < -90.0 ? 0.0 : w.up * std::min (twoWayLiftSpanDb, kneeCurve (w.lowCorner - x, 1.0, w.width));
    return w.base + up - w.down * kneeCurve (x - w.highCorner, 1.0, w.width);
}

// Vari-mu: the reduction grows smoothly out of nothing and its ratio keeps rising with it, −slope · width ·
// ln (1 + e^((x − corner) / width)). Measured per threshold × ratio × knee (54 curves at attack 10 ms / release
// 200 ms, each within 0.23 dB): the corner stops following the threshold below about −30 dB (the reduction then
// depends on the level alone: −23 dBFS at ratio 4, −15 at 2, −31 at 10) and a larger knee moves it up, so the
// reduction FALLS as the knee grows; the tables hold slope, corner − threshold and width in the detector's own level
// (which reads a 1 kHz sine 1.04 dB low at those times). Between the nodes: straight lines.
struct VariMuLaw
{
    double slope, corner, width;
};
VariMuLaw variMuLaw (double thr, double r, double knee) noexcept
{
    static constexpr double thrs[6] { -48.0, -36.0, -30.0, -24.0, -18.0, -12.0 };
    static constexpr double knees[3] { 0.0, 18.0, 72.0 };
    // [ratio 2 / 4 / 10][knee 0 / 18 / 72][threshold −48 … −12]
    static constexpr double slopeT[3][3][6] {
        { { 0.629, 0.616, 0.609, 0.609, 0.621, 0.649 }, { 0.600, 0.584, 0.573, 0.568, 0.582, 0.592 }, { 0.553, 0.544, 0.539, 0.533, 0.527, 0.521 } },
        { { 0.693, 0.692, 0.702, 0.727, 0.765, 0.806 }, { 0.675, 0.671, 0.678, 0.700, 0.717, 0.722 }, { 0.646, 0.645, 0.645, 0.645, 0.644, 0.643 } },
        { { 0.740, 0.763, 0.793, 0.833, 0.874, 0.915 }, { 0.727, 0.745, 0.770, 0.788, 0.801, 0.808 }, { 0.711, 0.714, 0.716, 0.719, 0.722, 0.724 } } };
    static constexpr double cornerT[3][3][6] {
        { { 32.00, 19.99, 14.28, 9.17, 5.11, 2.30 }, { 35.04, 22.88, 16.94, 11.49, 7.04, 3.38 }, { 40.44, 28.37, 22.40, 16.55, 10.85, 5.37 } },
        { { 23.87, 12.66, 7.97, 4.37, 1.95, 0.47 }, { 26.46, 14.95, 9.93, 5.89, 2.54, -0.35 }, { 30.57, 18.86, 13.21, 7.75, 2.51, -2.47 } },
        { { 15.94, 6.54, 3.43, 1.43, 0.23, -0.34 }, { 18.28, 8.29, 4.64, 1.54, -1.07, -3.25 }, { 22.01, 10.77, 5.45, 0.38, -4.41, -8.91 } } };
    static constexpr double widthT[3][3][6] {
        { { 4.88, 4.30, 3.75, 2.99, 2.16, 1.44 }, { 4.84, 4.39, 3.93, 3.33, 2.77, 2.35 }, { 4.84, 4.66, 4.52, 4.35, 4.16, 3.94 } },
        { { 4.94, 3.91, 3.10, 2.23, 1.47, 0.83 }, { 4.95, 4.12, 3.49, 2.94, 2.47, 2.03 }, { 4.97, 4.71, 4.53, 4.31, 4.06, 3.79 } },
        { { 4.70, 3.07, 2.17, 1.42, 0.71, 0.33 }, { 4.84, 3.60, 3.08, 2.61, 2.17, 1.74 }, { 5.20, 4.83, 4.60, 4.34, 4.05, 3.74 } } };
    const auto s = 1.0 - 1.0 / r;
    // the ratio: between the measured 2 / 4 / 10; outside them the nearest one (scaled below)
    int ri = 0;
    double rt = 0.0;
    if (s > 0.75)
    {
        ri = 1;
        rt = std::min (1.0, (s - 0.75) / 0.15);
    }
    else if (s > 0.5)
        rt = (s - 0.5) / 0.25;
    const auto kn = std::clamp (knee, 0.0, 72.0);
    const int ki = kn < 18.0 ? 0 : 1;
    const auto kt = (kn - knees[ki]) / (knees[ki + 1] - knees[ki]);
    // the threshold: under −48 dB the corner stays where it is (the level alone decides); over −12 dB the last
    // segment carries on
    const auto tc = std::clamp (thr, -48.0, 0.0);
    int ti = 0;
    while (ti < 4 && tc > thrs[ti + 1])
        ++ti;
    const auto tt = (tc - thrs[ti]) / (thrs[ti + 1] - thrs[ti]);
    const auto at = [&] (const double (&t)[3][3][6], bool extend) {
        const auto node = [&] (int a, int b) {
            const auto v = t[a][b][ti] + (t[a][b][ti + 1] - t[a][b][ti]) * (extend ? tt : std::min (tt, 1.0));
            return v;
        };
        const auto row = [&] (int a) { return node (a, ki) + (node (a, ki + 1) - node (a, ki)) * kt; };
        return row (ri) + (row (ri + 1) - row (ri)) * rt;
    };
    VariMuLaw w;
    w.slope = at (slopeT, false);
    w.corner = tc + at (cornerT, true) + (thr < -48.0 ? 0.0 : thr - tc);
    w.width = std::max (0.3, at (widthT, false));
    // round 108's −18 dB stairs at ratios 1.5 … 100 (the same law: its ratio 2 and 4 lines are this table's): under
    // ratio 2 the slope shrinks (0.52 at 1.5) and the corner moves up 2.75 dB by 1.5; over ratio 10 the slope grows
    // 5.6 % by 100, the corner drops 0.5 dB and the knee narrows
    if (s < 0.5)
    {
        const auto t = std::clamp ((0.5 - s) / (0.5 - 1.0 / 3.0), 0.0, 1.0);
        w.slope *= s < 1.0 / 3.0 ? 0.834 * s * 3.0 : 1.0 - 0.166 * t;
        w.corner += 2.75 * t;
        w.width += 0.43 * t;
    }
    else if (s > 0.9)
    {
        const auto t = std::min (1.0, (s - 0.9) / 0.09);
        w.slope *= 1.0 + 0.056 * t;
        w.corner -= 0.53 * t;
        w.width = std::max (0.3, w.width * (1.0 - 0.44 * t));
    }
    return w;
}
// ln (1 + eᶻ) from a table (the engine asks once per sample): exact to 2e-5 between −12 and 12, its asymptotes outside
double softplus (double z) noexcept
{
    constexpr int size = 1024;
    constexpr double span = 12.0, perUnit = (double) size / (2.0 * span);
    static const auto table = [] {
        std::array<float, size + 2> t {};
        for (int i = 0; i < size + 2; ++i)
            t[(std::size_t) i] = (float) std::log1p (std::exp ((double) i / perUnit - span));
        return t;
    }();
    if (z >= span)
        return z;
    if (z <= -span)
        return 0.0;
    const auto pos = (z + span) * perUnit;
    const auto i = (std::size_t) pos;
    const auto f = pos - (double) i;
    return (double) table[i] + f * ((double) table[i + 1] - (double) table[i]);
}
double variMuGain (const VariMuLaw& w, double x) noexcept
{
    return -w.slope * w.width * softplus ((x - w.corner) / w.width);
}

// (the classic vari-mu law, projects saved before round 190v)
// vari-mu: the share of the full ratio reached at an overshoot o (dB), by slope s = 1 − 1/R — measured (§2.1 data,
// slopes 1/3 … 0.99, overshoots 0 … 18 dB) and divided by our detector's own shortfall (0.96)
double variMuShare (double s, double o) noexcept
{
    static constexpr double slopes[6] { 0.3333, 0.5, 0.75, 0.875, 0.95, 0.99 };
    static constexpr double table[6][10] {
        { 0, 0.071, 0.118, 0.189, 0.28, 0.383, 0.491, 0.599, 0.701, 0.797 },
        { 0, 0.102, 0.2, 0.311, 0.422, 0.524, 0.615, 0.695, 0.764, 0.824 },
        { 0, 0.263, 0.442, 0.561, 0.645, 0.708, 0.758, 0.797, 0.829, 0.856 },
        { 0, 0.478, 0.637, 0.723, 0.776, 0.813, 0.841, 0.863, 0.881, 0.896 },
        { 0, 0.545, 0.677, 0.742, 0.783, 0.812, 0.833, 0.85, 0.863, 0.875 },
        { 0, 0.665, 0.767, 0.811, 0.839, 0.857, 0.871, 0.882, 0.892, 0.9 } };
    if (o <= 0.0)
        return 0.0;
    const auto row = [&] (int k) {
        if (o >= 18.0)
            return 1.0 - (1.0 - table[k][9]) * 18.0 / o;
        const auto x = o / 2.0;
        const int i = std::min (8, (int) x);
        return table[k][i] + (table[k][i + 1] - table[k][i]) * (x - i);
    };
    double f;
    if (s <= slopes[0])
        f = row (0) * s / slopes[0];
    else if (s >= slopes[5])
        f = row (5);
    else
    {
        int k = 0;
        while (s > slopes[k + 1])
            ++k;
        const auto t = (s - slopes[k]) / (slopes[k + 1] - slopes[k]);
        f = row (k) + (row (k + 1) - row (k)) * t;
    }
    return std::min (1.0, f / 0.96);
}

// the most an upward curve lifts (dB), whatever the range (round tp172b3, measured)
constexpr double upwardCapDb = 30.0;

// target gain change (dB; negative = reduction) for a detector level `x` (dB); `attMs`: the attack knob (two-way's
// fixed gain follows it)
double curveGain (const StyleDef& d, double x, double thr, double r, double knee, double range, double attMs = 10.0) noexcept
{
    const auto s = 1.0 - 1.0 / r;
    switch (d.curve)
    {
        case Curve::console:
            return std::max (-range, consoleSteadyDb (thr, r, knee, x));
        case Curve::upward:
        {
            // upward: quiet parts are lifted toward a point `offsetDb` from the threshold (−10.5 dB: under it); above
            // the threshold the gain holds; the corner is softened over the style's knee
            const auto kn = d.kneeAdd + d.kneeScale * knee + d.kneeSlope * s;
            const auto softMin = x - kneeCurve (x - thr, 1.0, kn);
            // round tp172b2 [EXEC] (Pro-C 3 trial, 1 kHz stairs at thresholds −36 … −18): the level held above the
            // threshold sinks as the threshold rises (−1.4 dB at −30, −7.9 dB at −18, ratio 4): baseDb per dB over −30
            const auto g = s * d.slopeScale * (thr + d.offsetDb + d.baseDb * (thr + 30.0) - softMin);
            // round tp172b3 [EXEC] (the Pro-C 3 trial, Upward, drums and bursts after digital silence): the lift stops
            // at 30 dB whatever the range (a 30 ms attack lets the first hit through 29.5 dB up, as the original)
            return std::clamp (g, -range, std::min (range, upwardCapDb));
        }
        case Curve::twoWay:
            return std::clamp (twoWayGain (twoWayLaw (thr, r, knee, attMs), x), -range, std::min (range, upwardCapDb));
        case Curve::vocal:
        {
            // a long smooth curve that already reduces far below the threshold (softplus in dB), steeper on top
            const auto k = std::max (0.5, d.kneeAdd + d.kneeScale * knee + d.kneeSlope * s);
            const auto sv = std::min (1.0, d.slopeScale * s);
            const auto z = (x - (thr + d.offsetDb)) / k;
            const auto soft = z > 30.0 ? z : std::log1p (std::exp (z));
            return -std::min (range, sv * k * soft);
        }
        case Curve::variMu:
            return std::max (-range, variMuGain (variMuLaw (thr, r, knee), x));
        case Curve::twoWayClassic:
        {
            // two-way: a fixed offset, upward compression below (threshold − 6 dB), downward above (threshold + 4 dB)
            const auto kn = d.kneeAdd + d.kneeScale * knee + d.kneeSlope * s;
            // round tp172b3: the upward part only over the 30 dB under its corner and not on silence (under −90 dBFS:
            // the original's first hit after digital silence comes out turned down, never lifted)
            const auto up = x < -90.0 ? 0.0 : d.upSlope * s * std::clamp (thr - 6.0 - x, 0.0, upwardCapDb);
            const auto g = d.baseDb * s + up - d.slopeScale * kneeCurve (x - thr - 4.0 - d.offsetDb, s, kn);
            return std::clamp (g, -range, std::min (range, upwardCapDb));
        }
        case Curve::variMuClassic:
        {
            // the ratio grows with the reduction (variable-mu tube behaviour), as measured
            const auto o = std::max (0.0, x - thr);
            const auto soft = knee > 0.0 ? kneeCurve (x - thr, 1.0, knee) : o;
            return -std::min (range, s * soft * variMuShare (s, o + knee * 0.25));
        }
        case Curve::downward:
        default:
            return -std::min (range, kneeCurve (x - thr - d.offsetDb, s * d.slopeScale, d.kneeAdd + d.kneeScale * knee + d.kneeSlope * s));
    }
}

void scEqParams (const EffectParams& p, EffectParams& out) noexcept
{
    out.fill (0.0f);
    for (int b = 0; b < scBands; ++b)
        for (int f = 0; f < 5; ++f)
            out[(std::size_t) facet::param (b, f)] = p[(std::size_t) scParam (b, f)];
    out[(std::size_t) facet::outputParam] = 0.5f;
    out[(std::size_t) facet::scaleParam] = 0.5f;
}
} // namespace

double consoleSteadyDb (double thresholdDb, double ratio, double kneeDb, double peakDb) noexcept
{
    // the loop settles where the gain equals the curve's answer to the output level: s − F(s) = 0, s − F(s) rises
    const auto a = std::exp (peakDb * nepersPerDb), lnT = thresholdDb * nepersPerDb;
    const auto loop = std::max (0.0, std::min (ratio, 1.0e4) - 1.0), kneeN = std::max (1.0e-4, 2.0 * kneeDb * nepersPerDb);
    auto f = [&] (double g) { return consoleTarget (std::log (std::min (1.0, a * std::exp (g)) + consoleFloor) - lnT, loop, kneeN); };
    double lo = -30.0, hi = 0.0;
    for (int i = 0; i < 60; ++i)
    {
        const auto m = 0.5 * (lo + hi);
        (m - f (m) < 0.0 ? lo : hi) = m;
    }
    return 0.5 * (lo + hi) / nepersPerDb;
}

namespace
{
// round 190kw: the static curve of the revision-1 styles (defined with their laws below); false when the style keeps its
// fitted law
bool revisionStaticDb (const EffectParams& p, double inputDb, double& gainDb) noexcept;
} // namespace

double staticGainDb (const EffectParams& p, double inputDb)
{
    if (double g = 0.0; revisionStaticDb (p, inputDb, g))
        return g;
    const auto& def = defOf (p);
    const auto att = attackMs (p[attack]);
    if (def.curve == Curve::twoWay)
    {
        // round 190v: two-way's detector (a linear attack / release follower) settles under a steady sine's peak by up
        // to 3.9 dB as the attack nears the release (0.5 dB at 10 / 200 ms, 2 dB at 100 / 200 ms, measured)
        const auto rho = att * def.attackScale / std::max (0.5, releaseMs (p[release]) * def.releaseScale);
        inputDb -= std::min (3.9, 4.0 * std::pow (rho, 0.55));
        // round 190al: the curve of a 1 kHz tone, as the reference was measured: the mid band's law, plus the little
        // of the tone that the 2.5 kHz split sends to the high band (2.5 %, lifted there by the upward part)
        const auto rng = std::max (0.01, rangeDb (p[range]));
        const auto band = [&] (int b, double amplitude) {
            const auto law = twoWayLaw (thresholdDb (p[threshold]), ratioOf (p[ratio]), kneeDb (p[knee]), att, b);
            const auto g = std::clamp (twoWayGain (law, inputDb + 20.0 * std::log10 (amplitude)), -rng, std::min (rng, upwardCapDb));
            return amplitude * dbToGain (g);
        };
        return 20.0 * std::log10 (band (0, 0.97504) + band (2, 0.02496));
    }
    if (def.curve == Curve::fet)
        return fetSteadyDb (p, inputDb);
    return curveGain (def, inputDb, thresholdDb (p[threshold]), ratioOf (p[ratio]), kneeDb (p[knee]),
                      std::max (0.01, rangeDb (p[range])), att);
}

float fetLawValue (int voice) noexcept { return (float) (std::clamp (voice, 0, fetVoices - 1) + 1) / 16.0f; }
double fetAmpDb (float v) noexcept { return (double) v * 128.0 - 64.0; }
float fetAmpValue (double db) noexcept { return (float) std::clamp ((db + 64.0) / 128.0, 1.0 / 128.0, 1.0); }

double fetSteadyDb (const EffectParams& p, double peakDb) noexcept
{
    // a steady tone: the followers settle at its peak, so the loop settles where g = law (peak + g); g − law (peak + g)
    // rises with g
    const auto v = fetVoiceOf (p);
    if (v < 0)
        return 0.0;
    const auto& f = fetVoiceFits[v];
    const auto t1 = thresholdDb (p[threshold]), w1 = std::max (0.1, kneeDb (p[knee])), s1 = std::max (0.0, std::min (ratioOf (p[ratio]), 1.0e4) - 1.0);
    const auto rng = std::max (0.01, rangeDb (p[range]));
    double lo = -rng, hi = 0.0;
    for (int i = 0; i < 60; ++i)
    {
        const auto m = 0.5 * (lo + hi);
        (m - fetLawDb (peakDb + m, t1, w1, s1, f, rng) < 0.0 ? lo : hi) = m;
    }
    return 0.5 * (lo + hi);
}

float characterCurve (Character c, float x) noexcept
{
    switch (c)
    {
        case Character::warm:
        {
            // clean up to about +9 dBFS, then an asymmetric soft limit (the negative side gives first): even + odd
            constexpr float tp = 6.5f, tn = 3.0f, sp = 2.0f, sn = 1.5f;
            if (x > tp)
                return tp + sp * std::tanh ((x - tp) / sp);
            if (x < -tn)
                return -tn - sn * std::tanh ((-x - tn) / sn);
            return x;
        }
        case Character::crisp:
        {
            // a slightly expanding cubic (odd harmonics growing with the square of the level), then an asymmetric
            // limit far above full scale
            const auto u = x + 0.0083f * x * x * x;
            return u >= 0.0f ? 16.0f * std::tanh (u / 16.0f) : 12.0f * std::tanh (u / 12.0f);
        }
        case Character::bright:
        {
            // an even, rectifier-like term (second harmonic from low levels) and a gentle, slightly asymmetric limit
            constexpr float s = 0.1f;
            const auto u = x + 0.007f * s * (std::sqrt (1.0f + (x / s) * (x / s)) - 1.0f);
            const auto limit = u >= 0.0f ? 7.75f : 5.5f;
            const auto a = std::abs (u / limit);
            const auto d = 1.0f + a * std::sqrt (a);
            return u / std::cbrt (d * d); // u / (1 + |u/L|^1.5)^(1/1.5)
        }
        case Character::off:
        default: return x;
    }
}

namespace
{
// ---- round 190kw (REA_rodada190kw §1, static analysis of the reference's binary; LYNX's own code): the three "V1"
// styles of the reference (Clean, Classic, Opto) compute an instantaneous gain reduction from the side chain's sample
// level in dB and smooth that reduction (not the level) with a two-stage, program-dependent follower.
//  - Times: every follower step is c = 1 − exp(−ln 10 / T), T in samples (the time to cover 90 % of a step).
//  - Attack time = lo + span · a³ (a = the attack knob's position, 0.005 + 250 · a³ ms on the display); release time =
//    rel · relScale + relBase − 0.9 · relDip · √((attack − attackMin) / (lo + span − attackMin)), at least relBase / 10
//    (rel = the release knob's 0 … 1 share: 10 ms + 2490 ms · rel on the display).
//  - Curve: overshoot d = level − threshold through the knee (below), × (1 − 1 / ratio) under 100 : 1, ≤ range, ≥ 0.
//    Classic is a feedback design: it reads (min (d, range) − its own reduction) × ratio instead of d.
//  - Knee: a quadratic knee of the knob's width blended with the style's own shape K(d) = B((d − off) / 48) ·
//    (d − off) · scale (B a cubic Bézier through (0, 0), P1, P2, (1, 1), straight beyond its ends): K alone at 24 dB,
//    the quadratic knee alone at 72 dB, under 24 dB the shape narrowed by knee / 24.
struct V1Law
{
    double lo, attackMin, span, relBase, relDip, relScale; // seconds
    double p1x, p1y, p2x, p2y, off, scale;                 // the knee shape
    int kind;                                              // 0 clean, 1 classic (feedback), 2 opto
};
constexpr V1Law v1Laws[3] {
    { 0.0001, 0.0002, 0.2499, 0.025, 0.09, 1.975, 0.093, 0.287, 0.52, 0.822, -11.0, 0.83, 0 },
    { 0.003, 0.003, 0.997, 0.04, 0.13, 2.46, 0.0, 1.492, 0.541, 1.0, -3.0, 0.9, 1 },
    { 0.001, 0.004, 0.249, 0.005, 0.02, 1.195, 0.022, 0.284, 0.76, 0.943, -13.3, 0.7338, 2 },
};
// the knee shape, sampled once (−20 … 100 dB, 8192 points, linear in between; straight past 100 dB)
struct V1Knee
{
    static constexpr int points = 8192;
    static constexpr double lo = -20.0, hi = 100.0;
    std::array<double, points + 1> t {};
    double top = 0.0, slope = 0.0;
    explicit V1Knee (const V1Law& w) noexcept
    {
        const auto bez = [&] (double u) {
            // y on the Bézier where x = u (bisection on its parameter); straight lines past the ends
            if (u <= 0.0)
                return w.p1x > 0.0 ? u * w.p1y / w.p1x : 0.0;
            if (u >= 1.0)
                return 1.0 + (u - 1.0) * (1.0 - w.p2y) / (1.0 - w.p2x);
            const auto cx = 3.0 * w.p1x, bx = 3.0 * (w.p2x - w.p1x) - cx, ax = 1.0 - cx - bx;
            const auto cy = 3.0 * w.p1y, by = 3.0 * (w.p2y - w.p1y) - cy, ay = 1.0 - cy - by;
            double a = 0.0, b = 1.0, m = 0.5;
            for (int k = 0; k < 60; ++k)
            {
                m = 0.5 * (a + b);
                ((ax * m + bx) * m + cx) * m < u ? a = m : b = m;
            }
            return ((ay * m + by) * m + cy) * m;
        };
        const auto shape = [&] (double x) {
            const auto v = std::max (0.0, x - w.off);
            return bez (v / 48.0) * v * w.scale;
        };
        for (int i = 0; i <= points; ++i)
            t[(std::size_t) i] = shape (lo + (hi - lo) * i / (points - 1));
        top = shape (hi);
        slope = top - shape (hi - 1.0);
    }
    double operator() (double x) const noexcept
    {
        if (x > lo && x < hi)
        {
            const auto f = (x - lo) * (points - 1) / (hi - lo);
            const auto i = (int) f;
            return t[(std::size_t) i] + (t[(std::size_t) i + 1] - t[(std::size_t) i]) * (f - i);
        }
        return x <= lo ? 0.0 : top + (x - hi) * slope;
    }
};
const V1Knee& v1Knee (int kind) noexcept
{
    static const V1Knee k[3] { V1Knee (v1Laws[0]), V1Knee (v1Laws[1]), V1Knee (v1Laws[2]) };
    return k[kind];
}
double v1KneeGain (const V1Knee& shape, double d, double knee) noexcept
{
    constexpr double eps = 1.1920928955078125e-07;
    if (knee < eps)
        return d;
    if (std::abs (knee - 24.0) <= eps)
        return shape (d);
    const auto half = 0.5 * knee;
    auto q = d;
    if (d >= -half && d <= half)
        q = (half + d) / knee * ((half + d) / knee) * half;
    const auto k72 = knee / 72.0;
    if (k72 < 1.0 / 3.0)
    {
        const auto s = 3.0 * k72;
        return (shape (d / s) * s - q) * (s * s) + q;
    }
    const auto at = shape (d);
    return (k72 - 1.0 / 3.0) * 1.5 * (q - at) + at;
}
inline double v1Coef (double samples) noexcept
{
    const auto t = (double) (float) samples;
    return t <= 1.1920928955078125e-07 ? 1.0 : 1.0 - std::exp (-2.302585124969482 / t);
}

// ---- round 190kw (REA_rodada190kw §2): the reference's V2 styles (Vocal, Mastering, Bus, Punch, Pumping) are small
// graphs of two generic elements, rebuilt here in LYNX's own code:
//  - a gain computer (quadratic knee of the knob's width, × (1 − 1 / ratio) under 100 : 1, ≤ range, ≥ 0), in dB or as
//    a reduction share 1 − 10^(−GR / 20);
//  - a follower: y += (x − y) · c, c = 1 − exp(−ln 10 / (T · rate)); rising uses the attack time, blended towards
//    0.1 · T · step by (1 − p1) and lengthened by p2 · e^(−step) for small steps; falling uses the release time,
//    blended towards −0.1 · T · step by (1 − p5) and lengthened by p6 · e^(step) for steps under 10; with auto release
//    T = k / (√y + 1).
struct V2Follow
{
    double att = 0.0, rel = 0.0, p1 = 1.0, p2 = 0.0, p5 = 1.0, p6 = 0.0, autoK = 0.0;
    bool autoRel = false;
};
// the last coefficient of each direction, reused while the time (in samples, as a float) does not change
struct V2Cache
{
    float t[2] { -1.0f, -1.0f };
    double c[2] { 1.0, 1.0 };
};
inline double v2Step (double& y, double x, const V2Follow& f, double rate, V2Cache& cache) noexcept
{
    const auto d = x - y;
    double t;
    if (d <= -1.1920928955078125e-07)
    {
        t = f.autoRel ? (double) (float) (f.autoK / (std::sqrt (std::max (y, 0.0)) + 1.0)) : f.rel;
        if (f.p5 != 1.0)
        {
            const auto b = (double) (float) (t * d * -0.1);
            t = (t - b) * f.p5 + b;
        }
        if (f.p6 != 0.0 && d > -10.0)
            t += std::exp (std::max (d, -25.0)) * f.p6;
    }
    else
    {
        t = f.att;
        if (f.p1 != 1.0)
        {
            const auto b = (double) (float) (t * d * 0.1);
            t = (t - b) * f.p1 + b;
        }
        if (f.p2 != 0.0 && d < 10.0)
            t += std::exp (std::max (-d, -25.0)) * f.p2;
    }
    const auto k = d <= -1.1920928955078125e-07 ? 1 : 0;
    const auto samples = (float) ((double) (float) t * rate);
    if (samples != cache.t[k])
    {
        cache.t[k] = samples;
        cache.c[k] = v1Coef (samples);
    }
    y += d * cache.c[k];
    return y;
}
// the quadratic knee (width k) of an overshoot
inline double v2Knee (double over, double k) noexcept
{
    if (k > 1.1920928955078125e-07 && over >= -0.5 * k && over <= 0.5 * k)
        return (0.5 * k + over) / k * ((0.5 * k + over) / k) * (0.5 * k);
    return over;
}
// the running maximum of the last n samples (a monotone queue on fixed storage)
struct SlidingMax
{
    std::vector<double> v;
    std::vector<long long> at;
    int head = 0, count = 0;
    long long now = 0;
    void setup (int capacity)
    {
        v.assign ((std::size_t) capacity, 0.0);
        at.assign ((std::size_t) capacity, 0);
        head = count = 0;
        now = 0;
    }
    void clear() noexcept { head = count = 0; now = 0; }
    double push (double x, int n) noexcept
    {
        const int cap = (int) v.size();
        if (cap == 0)
            return x;
        while (count > 0 && v[(std::size_t) ((head + count - 1) % cap)] <= x)
            --count;
        if (count == cap)
        {
            head = (head + 1) % cap;
            --count;
        }
        v[(std::size_t) ((head + count) % cap)] = x;
        at[(std::size_t) ((head + count) % cap)] = now;
        ++count;
        while (count > 0 && at[(std::size_t) head] < now - n)
        {
            head = (head + 1) % cap;
            --count;
        }
        ++now;
        return v[(std::size_t) head];
    }
};
// round 190mj: the mean of the last n samples (n within the capacity; a running sum on fixed storage)
struct Boxcar
{
    std::vector<double> v;
    int pos = 0, len = 0;
    double sum = 0.0;
    void setup (int capacity)
    {
        v.assign ((std::size_t) std::max (1, capacity), 0.0);
        clear();
    }
    void clear() noexcept
    {
        std::fill (v.begin(), v.end(), 0.0);
        pos = len = 0;
        sum = 0.0;
    }
    double push (double x, int n) noexcept
    {
        const int cap = (int) v.size();
        n = std::clamp (n, 1, cap);
        if (n != len) // (the length changed: the sum of the last n − 1 samples again)
        {
            len = n;
            sum = 0.0;
            for (int k = 1; k < n; ++k)
                sum += v[(std::size_t) ((pos - k + cap) % cap)];
        }
        else
            sum -= v[(std::size_t) ((pos - n + cap) % cap)]; // (the oldest of the window leaves)
        v[(std::size_t) pos] = x;
        sum += x;
        pos = (pos + 1) % cap;
        return sum / n;
    }
};

// ---- round 190lg (REA_rodada190lg, Versatile): Pro-C 3's default style (LYNX's Allround), rebuilt from the reference's
// own graph of generic elements: two gain computers (the usual quadratic knee, ratio and range, as the reduction share
// 1 − 10^(−GR / 20)) on two slightly different readings of the side chain's level,
//  - slow: 0.979 × the level + 0.0203 × the level low-passed at 204 Hz → the gain computer → the knob-timed follower
//    (attack 0.8 ms + 249.2 ms · a³, release 10 ms + 2.49 s · tan (1.2 r²) / tan (1.2); auto release T = k / (√y + 1),
//    k = 2.0494 r² + 0.0905);
//  - fast: 0.91 × the level low-passed at 6.39 kHz → the gain computer → a slew limiter (the share rises by at most
//    1 per T3 s and falls by at most 1 per T4 s, both from the knob times);
//  the reduction is max (slow, w · fast), w = 0.9 + 0.0398 · fast (within 0.7 … 1.06). The reference drifts two mixer
//  gains with slow random modulators: LYNX runs their mean. T3 / T4 are LYNX's own smooth fits of the reference's
//  piecewise-linear knob curves; T4 runs at half that fit: round 108's measured burst (attack 10 / release 200 ms: the
//  gain back half-way after 61.6 ms) reads 59.5 ms with it (120 ms with the full fit, 28 ms without the fast path), and
//  the full fit held the fast path up on the bank's music files (0.3 … 0.6 dB too much reduction). The attack knob's two
//  step-dependent follower terms are not built (every reading of them scored lower on the bank than leaving them neutral)
struct VersLaw
{
    double slowLevel = 0.979424, slowLowMix = 0.020348, slowLowHz = 204.0;
    double fastLevel = 0.91, fastHz = 6390.0;
    double wBase = 0.9, wSlope = 0.0397797, wMin = 0.7, wMax = 1.06;
    double attBase = 0.0008, attSpan = 0.2492, autoA = 2.049379, autoB = 0.0905348;
    // T3 (attack s): linear up to t3At s (t3Lin · s / t3At), then t3Pow · (s / t3Ref)^t3Exp
    double t3At = 0.02, t3Lin = 0.08, t3Pow = 0.15, t3Ref = 0.2, t3Exp = 0.273;
    // T4 (release s) = t4A + t4B s + t4C s²
    double t4A = 0.19, t4B = 0.55, t4C = 0.03;
};
inline const VersLaw& versLaw() noexcept
{
    static const VersLaw law;
    return law;
}
inline double versT3 (const VersLaw& v, double s) noexcept
{
    s = std::max (s, 0.0);
    return s <= v.t3At ? v.t3Lin * s / v.t3At : v.t3Pow * std::pow (s / v.t3Ref, v.t3Exp);
}
inline double versT4 (const VersLaw& v, double s) noexcept
{
    s = std::max (s, 0.0);
    return v.t4A + v.t4B * s + v.t4C * s * s;
}

#ifdef LYNX_CLAB
// lab build only: LYNX_CLAB="key=value …" overrides a law constant (fitting on the bank; never in a product build)
inline double clab (const char* key, double def) noexcept
{
    const char* e = std::getenv ("LYNX_CLAB");
    if (e == nullptr)
        return def;
    const std::string t (e), k = std::string (key) + "=";
    for (std::size_t i = 0; i < t.size();)
    {
        auto j = t.find (' ', i);
        if (j == std::string::npos)
            j = t.size();
        if (t.compare (i, k.size(), k) == 0)
            return std::atof (t.c_str() + i + k.size());
        i = j + 1;
    }
    return def;
}
#else
constexpr double clab (const char*, double def) noexcept { return def; }
#endif

// ---- round 190lp (REA_rodada190lp, crest): Pro-C 3's Smooth (a voicing in LYNX), rebuilt from the reference's own
// graph of generic elements (the usual gain computer: quadratic knee, ratio, range, read as the reduction share
// 1 − 10^(−GR / 20); followers y += (x − y)(1 − e^(−ln 10 / (T · rate))) on that share):
//  - a feed-forward stand-in for a feedback design: a first gain computer E (threshold + 0.21 dB, its ratio as the knob's
//    up to 10 : 1, then easing towards 16 : 1) drives two followers F0 (the knob times, the release × 0.44) and F1 (attack
//    × 9, release × 2.26); the side chain is turned down by max (0.761 F0, 0.769 F1) before the main gain computer D;
//  - D (its threshold × a factor of the ratio, 1 at 1 : 1 rising to about 1.17 near 20 : 1) drives C0 (the knob's attack,
//    the release × 0.43) and C1 (attack × 9, release × 2.25);
//  - the reduction is max (A · C0, C1), A = 1.0081 − 0.0123 · (0.610 · C0 · F0).
// Knob laws: attack 0.25 s · a³, release 0.02 s + 2.48 s · tan (1.2 r²) / tan (1.2); the auto release's constant
// k = K (3.6 r² + 0.1) · 0.847, K LYNX's smooth fit of the reference's curve (identity up to 1.6, then rising faster).
// The ratio factor, the ratio easing and K are LYNX's own smooth fits of the reference's piecewise-linear curves. The
// reference also low-shelves E's input by 0.12 dB around 880 Hz and can drift two values randomly (off by default):
// neither is built.
struct SmoothLaw
{
    double attScale = 0.25, relBase = 0.02, relSpan = 2.48;
    double c0Rel = 0.434842, c1Att = 9.0, c1Rel = 2.250114, c1P1 = 0.904, c1P6 = 0.0042, c0Auto = 0.851851;
    double f0Att = 0.999086, f0Rel = 0.434842 * 1.008688, f0P5 = 0.986694, f0Auto = 1.090460;
    double f1Att = 8.994057, f1Rel = 2.250114 * 1.002287, f1P6 = 0.0042, f1Auto = 1.235025;
    double eThr = 0.214906, eRatTop = 10.0, eRatSpan = 6.15, eRatTau = 14.4;
    double g0 = 0.760859, g1 = 0.768633, bGain = 0.610426, aBase = 1.008093, aSlope = -0.012346;
    // the ratio factor of D's threshold: 1 + p1A (1 − R^−p1B) − p1C · max (0, ln R − p1D)² + p1E · ln R / R (R ≤ 50)
    double p1A = 0.241503, p1B = 2.25, p1C = 0.067647, p1D = 2.72, p1E = -0.353175;
    // the auto release: K (k) = k + kA · max (0, k − kKnee)^kP, k = 3.6 r² + 0.1, then × kScale
    double kA = 0.82, kP = 0.72, kKnee = 1.6, kScale = 0.846823;
};
inline const SmoothLaw& smoothLaw() noexcept
{
    static const SmoothLaw law;
    return law;
}
inline double smoothThrFactor (const SmoothLaw& s, double ratio) noexcept
{
    const auto l = std::log (std::clamp (ratio, 1.0, 50.0));
    const auto o = std::max (0.0, l - s.p1D);
    return 1.0 + s.p1A * (1.0 - std::exp (-s.p1B * l)) - s.p1C * o * o + s.p1E * l * std::exp (-l);
}
inline double smoothERatio (const SmoothLaw& s, double ratio) noexcept
{
    return ratio <= s.eRatTop ? ratio : s.eRatTop + s.eRatSpan * (1.0 - std::exp (-(ratio - s.eRatTop) / s.eRatTau));
}

// ---- round 190lp (REA_rodada190lp, crest): Pro-C 3's Op-El (a voicing in LYNX), rebuilt from the reference's own graph:
// two gain computers on the side chain's level, read as the reduction share (see SmoothLaw): D1 with the knobs' knee,
// ratio and range, D2 with the knob's ratio but a fixed 30 dB knee and 60 dB range. Each share is squared and followed:
// D1's by A (attack 0.998 × the knob + 0.5 ms, release 0.596 × + 10 ms) and B (9.99 × + 2 ms, 1.788 × + 30 ms), D2's by
// C (149.9 × + 15 ms, 4.768 × + 80 ms); the reduction share is √(0.8 A + 0.15 B + 0.05 C) — a fast, a slower and a very
// slow memory, summed in power, as an opto cell. Knob laws: attack 0.25 s · a³, release 2.5 s · tan (1.2 r²) / tan (1.2).
// D1 reads the side chain through the reference's wide high shelf (−11 dB around 26.7 kHz, Q 0.245): LYNX runs its own
// second-order shelf (−11 dB, Q 0.245) at 26.7 kHz or 0.4167 × the rate, whichever is lower (20 kHz at 48 kHz: the
// placement that matched the reference's 8 kHz stairs; it also takes the sampling dips off a high tone's level). The
// reference can drift its threshold randomly (off by default): not built.
struct OpElLaw
{
    double aAtt = 0.998, aAttBase = 0.0005, aRel = 0.596, aRelBase = 0.01;
    double bAtt = 9.992, bAttBase = 0.002, bRel = 1.788, bRelBase = 0.03;
    double cAtt = 149.94, cAttBase = 0.015, cRel = 4.768, cRelBase = 0.08;
    double wA = 0.8, wB = 0.15, wC = 0.05, d2Knee = 30.0, d2Range = 60.0;
    double shelfDb = -11.0, shelfHz = 26700.0, shelfAtRate = 0.4167, shelfQ = 0.245; // D1's high shelf
};
inline const OpElLaw& opElLaw() noexcept
{
    static const OpElLaw law;
    return law;
}

// ---- round 190mb (REA_rodada190mb, crest): Pro-C 3's Upward, rebuilt from the reference's own graph:
//  - the level: the side chain's magnitude through a follower that rises at once and falls in 5 ms, in dB (floor −180);
//  - an expansion gain computer E on that level: the dB under the threshold, o = threshold − level, through a ONE-SIDED
//    quadratic knee (nothing at the threshold, (o / k)² · k / 2 up to o = k, o − k / 2 beyond), × (1 − 1 / ratio),
//    within 0 … the upward range (the knob's range, at most 36 dB);
//  - the same gain computer on silence (−180 dB) gives the lift at silence, L = E (−180) (the upward range);
//  - one knob-timed follower smooths L − E (level): it rises (the lift goes away) at the attack time, falls (the lift
//    comes back) at the release time; the lift is L − that follower.
// [EXEC] (refcap030 / tp172b 1 kHz stairs, ratio 2 / 4 / 10, knee 0 … 72, thresholds −48 … −12, five attacks): the lift is
// further lowered by a constant, level-independent amount, (1 − 1 / ratio) · √a · (threshold + the upward range) dB when
// positive (a = the attack knob's position 0 … 1): 0 / 2.63 / 5.26 / 10.53 dB at thresholds −48 / −30 / −24 / −12 (ratio 4,
// attack 10 ms), 1.21 … 3.16 dB at threshold −30 over attacks 0.1 … 30 ms, within 0.02 dB on every capture. The
// reference's graph does not show where it comes from (its loud-signal output is the lift minus this); LYNX applies it
// as measured.
// Knob laws: attack 0.25 s · a³; release 0.2 s + 2.3 s · F (r), F LYNX's smooth fit of the reference's curve (0.625 r up
// to r = 0.8, then rising to 1 at r = 1); auto release T = k / (√y + 1), k = 9 r² + 1.
struct UpLaw
{
    double attScale = 0.25, relBase = 0.2, relSpan = 2.3, autoA = 9.0, autoB = 1.0;
    double fSlope = 0.625, fKnee = 0.8, fBend = 1.875, fWidth = 0.02; // F (r) = fSlope r + fBend · w · ln (1 + e^((r − fKnee) / w))
    double levelRel = 0.005, rangeMax = 36.0, silenceDb = -180.0, cutScale = 1.0;
};
inline const UpLaw& upLaw() noexcept
{
    static const UpLaw law;
    return law;
}
inline double upRelShape (const UpLaw& u, double r) noexcept
{
    const auto z = (r - u.fKnee) / u.fWidth;
    return u.fSlope * r + u.fBend * u.fWidth * (z > 30.0 ? z : std::log1p (std::exp (z)));
}
// the expansion gain computer: o dB under the threshold → dB of lift (0 … lim)
inline double upExpand (double under, double knee, double slope, double lim) noexcept
{
    if (under <= 0.0)
        return 0.0;
    auto v = under - 0.5 * knee;
    if (knee > 1.1920928955078125e-07 && under <= knee)
        v = under / knee * (under / knee) * (0.5 * knee);
    return std::clamp (v * slope, 0.0, lim);
}

// ---- round 190mb (REA_rodada190mb, crest): Pro-C 3's Vari-Mu, rebuilt from the reference's own node (a feedback tube
// stage, not a graph of generic elements):
//  - a push-pull stage: the side chain's sample x enters 6 dB down (d = 0.501 |x|) on both sides of a bias b, each side
//    through the tube curve T and a soft clip (tanh); the stage's gain is (tanh T (b + d) − tanh T (b − d)) / 2d — the
//    curve's slope at the bias, taken over the signal's own swing (so loud peaks also bend: a little odd distortion);
//  - FEEDBACK: the stage's last output y (= x · that gain) is the detector's signal; the amount over the threshold,
//    max (0, |y| − 10^(threshold / 20)) (with a knee: the usual quadratic knee in dB, read back as (1 − 10^(−over / 20)) |y|),
//    goes through one follower (attack / release; auto release T = k / (√b + 1)); the bias is b = (r' − 1) × that follower,
//    r' = (1 − 0.5 √(knee / 60)) × the ratio's own scale (1 at 1 : 1, 40 at 10 : 1, 80 at 100 : 1);
//  - the reduction is −20 log10 of the gain, at most the range.
// T (u) = 0.5 u + 0.5 Q (u), Q LYNX's own quintic fitted to the reference's curve over its working span (−11.4 … 18.03,
// flat beyond: Q (0) = 0, slope 1 there, a little steeper below, bending to flat at the top; 0.055 worst over the span,
// 0.009 within |u| < 8, its slope within 0.002); the ratio scale is LYNX's smooth fit of the reference's straight lines.
// Knob laws: attack 0.249 (a² + a³ − a⁴) + 0.001 s (0.001 … 0.25), release 0.005 + 0.97 tan (1.2 r²) s, auto release's
// constant 3.6 r² + 0.1.
struct VmuLaw
{
    double inGain = 0.5011872336272722; // −6 dB
    double uLo = -11.4, uHi = 18.03, q2 = -2.78613777e-02, q3 = -1.52802139e-03, q4 = 1.27100459e-04, q5 = -2.92540601e-06;
    double rLoSlope = 39.0 / 9.0, rKnee = 10.0, rKneeValue = 40.0, rHiSlope = 40.0 / 90.0, rTop = 80.0, rSoft = 0.5;
    double kneeShrink = 0.5, kneeRef = 60.0;
    double attA = 0.249, attMin = 0.001, attMax = 0.25, relBase = 0.005, relSpan = 0.97, autoA = 3.6, autoB = 0.1;
};
inline const VmuLaw& vmuLaw() noexcept
{
#ifdef LYNX_CLAB
    static const VmuLaw law = [] {
        VmuLaw v;
        v.q2 = clab ("vq2", v.q2); v.q3 = clab ("vq3", v.q3); v.q4 = clab ("vq4", v.q4); v.q5 = clab ("vq5", v.q5);
        v.inGain = clab ("vIn", v.inGain); v.rSoft = clab ("vSoft", v.rSoft);
        return v;
    }();
#else
    static const VmuLaw law;
#endif
    return law;
}
inline double vmuTube (const VmuLaw& v, double u) noexcept
{
    const auto c = std::clamp (u, v.uLo, v.uHi);
    return 0.5 * u + 0.5 * (c + c * c * (v.q2 + c * (v.q3 + c * (v.q4 + c * v.q5))));
}
// the stage's gain for a swing d around the bias b
inline double vmuGain (const VmuLaw& v, double b, double d) noexcept
{
    return (std::tanh (vmuTube (v, b + d)) - std::tanh (vmuTube (v, b - d))) / (2.0 * d);
}
// the ratio's own scale (1 at 1 : 1, 40 at 10 : 1, 80 at 100 : 1): a smooth minimum of the two straight lines
inline double vmuRatio (const VmuLaw& v, double ratio) noexcept
{
    const auto r = std::clamp (ratio, 1.0, 100.0);
    const auto a = 1.0 + v.rLoSlope * (r - 1.0), b = v.rKneeValue + v.rHiSlope * (r - v.rKnee);
    const auto m = std::min (a, b);
    return std::min (v.rTop, m - v.rSoft * std::log1p (std::exp (-std::abs (a - b) / v.rSoft)));
}
// the detector's amount over the threshold (linear) of the stage's output y
inline double vmuOver (double y, double thrDb, double thrLin, double knee) noexcept
{
    y = std::abs (y);
    if (knee <= 1.1920928955078125e-07)
        return std::max (0.0, y - thrLin);
    auto over = (y > 1e-10 ? std::max (-180.0, 20.0 * std::log10 (y)) : -180.0) - thrDb;
    over = v2Knee (over, knee);
    return over <= 0.0 ? 0.0 : (1.0 - std::exp (-over * 0.1151292546497023)) * y;
}

// ---- round 190mj (REA_rodada190mj, crest): Pro-C 3's TTM (LYNX's Two-way), rebuilt from the reference's band processors.
// The three bands (fourth-order Linkwitz-Riley splits at the reference's own 2^6.4622 = 88.17 Hz and 2^11.2877 = 2.5 kHz:
// round 190al's measured 88 Hz left a 0.3 degree phase error at 100 Hz) each run the same graph on their own band of the
// side chain, and the band's audio is turned by that band's gain (a second split object of the reference, set to the same
// frequencies at run time, is the side chain's):
//  - the level: the magnitude, held for the Hold time (then falling at a rate set by the held level), held again over
//    the look-ahead time, SQUARED, one follower on that power (attack / release per band, auto release T = k / (√y + 1)),
//    back to the amplitude and to dB — an RMS-like reading;
//  - two gain computers on that level: a downward one (the usual centred quadratic knee, × (1 − 1 / ratio) below 100 : 1,
//    within the range) and an upward one (the expansion computer of round 190mb: a one-sided knee, × (1 − 1 / ratio_u),
//    within 0.6 × the range), the gain = lift − reduction, smoothed by two moving averages of half the look-ahead time;
//  - per band (mid / low / high): the downward threshold is the knob + 3 / + 0 / − 2 dB, its ratio 2/3 of the knob's
//    (at least 1 : 1) / 2/3 / the knob's; the follower's attack 1/4 / 1/2 / 1/8 of the attack knob's time, its release
//    1/4 / 1/4 / 0.1125 of the release knob's; the auto release's constant 3.6 (T_band / 2.5 s)² + 0.1 (T_band = the
//    knob's release, × 0.45 on the high band);
//  - the upward computer is the same for every band: threshold the knob − 7 dB + half the knee, ratio_u = 1 + 3.17 ·
//    √((ratio − 1) / 99) (ratio within 1 … 100).
// [EXEC] The reference's output also carries a level-independent cut, the same in every band and knee-independent, as
// Upward's (round 190mb): 0.6 · (1 − 1 / ratio) · √a · (threshold + the upward range) dB when positive (a = the attack knob's
// position 0 … 1; trial190k's threshold × ratio × knee × attack grid on the 1 kHz / 100 Hz / 8 kHz stairs: the scale fits
// 0.601, 0.04 dB rms over 189 settings × 4 regions). The graph does not show where it is applied; LYNX applies it as
// measured, with the upward range (0.6 × the range: 36 dB at every capture's 60) as Upward's inference.
// LYNX keeps issue #77's fade under −80 dBFS and its threshold bound (a lift never takes the input's peak past the threshold).
struct TtmLaw
{
    // per band in LYNX's order (mid, low, high)
    double attShare[3] { 0.25, 0.5, 0.125 }, relShare[3] { 0.25, 0.25, 0.1125 }, autoShare[3] { 1.0, 1.0, 0.45 };
    double downOffset[3] { 3.0, 0.0, -2.0 }, ratioScale[3] { 0.6667, 0.6667, 1.0 };
    double upOffset = -7.0, upKnee = 0.5, upSpan = 3.17, upRange = 0.6, rangeMax = 60.0, ratioMax = 100.0;
    double autoA = 3.6, autoB = 0.1, autoRef = 2.5;
    double holdRel = 1.0e-4;      // the hold's fall: T = the held level × the hold time (ms) × this (s)
    double smoothTrim = 0.125;    // the look-ahead smoothing: two moving averages of L / 2 − this (ms)
    double cutScale = 0.6;        // the measured cut's scale (see above)
    double liftSafe = 1.0;                 // issue #77's threshold bound (1 = on; see detectV2)
    double liftFade = 1.0, fadeFloor = -100.0, fadeSpan = 20.0; // issue #77's fade: no lift under −100 dBFS, full from −80
    double liftCap = 1.0e9;                // (lab) revision 0's 30 dB ceiling: off (the upward computer's 0.6 × range bounds it)
    double baseAdd[3] { 0.0, 0.0, 0.0 };   // (lab) per-band additions to the cut (dB)
};
inline const TtmLaw& ttmLaw() noexcept
{
#ifdef LYNX_CLAB
    static const TtmLaw law = [] {
        TtmLaw t;
        for (int b = 0; b < 3; ++b)
        {
            const std::string k = std::to_string (b);
            t.attShare[b] = clab (("tA" + k).c_str(), t.attShare[b]);
            t.relShare[b] = clab (("tR" + k).c_str(), t.relShare[b]);
            t.downOffset[b] = clab (("tD" + k).c_str(), t.downOffset[b]);
            t.baseAdd[b] = clab (("tB" + k).c_str(), t.baseAdd[b]);
        }
        t.upOffset = clab ("tUp", t.upOffset);
        t.upRange = clab ("tUR", t.upRange);
        t.cutScale = clab ("tCut", t.cutScale);
        t.liftSafe = clab ("tSafe", t.liftSafe);
        t.liftFade = clab ("tFade", t.liftFade);
        t.liftCap = clab ("tCap", t.liftCap);
        return t;
    }();
#else
    static const TtmLaw law;
#endif
    return law;
}
// one band's static settings
struct TtmBand
{
    double t1 = 0.0, r1 = 1.0, t2 = 0.0, s2 = 0.0, lim1 = 0.0, lim2 = 0.0, base = 0.0;
};
inline TtmBand ttmBand (const TtmLaw& t, int b, double thr, double rat, double kn, double rng, double attMs) noexcept
{
    TtmBand o;
    const auto r = std::clamp (rat, 1.0, t.ratioMax);
    o.t1 = thr + t.downOffset[b];
    o.r1 = std::clamp (r * t.ratioScale[b], 1.0, t.ratioMax);
    o.t2 = thr + t.upOffset + t.upKnee * kn;
    o.s2 = 1.0 - 1.0 / (1.0 + t.upSpan * std::sqrt ((r - 1.0) / (t.ratioMax - 1.0)));
    o.lim1 = rng;
    o.lim2 = t.upRange * std::clamp (rng, 0.0, t.rangeMax);
    // the measured cut (the same in every band): cutScale · (1 − 1 / ratio) · √a · (threshold + the upward range), when positive
    const auto a = std::cbrt (std::clamp ((attMs - 0.005) / 250.0, 0.0, 1.0));
    o.base = -t.cutScale * (rat < 1.0e8 ? 1.0 - 1.0 / rat : 1.0) * std::sqrt (a) * std::max (0.0, thr + o.lim2) + t.baseAdd[b];
    return o;
}
// the band's gain (dB, + = up) for a level (dB) from the follower
inline double ttmGain (const TtmBand& o, double lev, double kn) noexcept
{
    auto down = v2Knee (lev - o.t1, kn);
    if (o.r1 < 100.0)
        down -= down / o.r1;
    down = std::max (0.0, std::min (down, o.lim1));
    return upExpand (o.t2 - lev, kn, o.s2, o.lim2) - down + o.base;
}

bool revisionStaticDb (const EffectParams& p, double inputDb, double& gainDb) noexcept
{
    if (! revisionOn (p))
        return false;
    const auto st = styleOf (p);
    const auto thr = thresholdDb (p[threshold]), rat = ratioOf (p[ratio]), kn = kneeDb (p[knee]);
    const auto rng = std::max (0.01, rangeDb (p[range]));
    const auto d = inputDb - thr;
    if ((st == Style::clean || st == Style::opto) && ! on (p[autoRelease]))
    {
        // the per-sample peak detector settles at the peak's target (REA_rodada190kw §1)
        auto t = v1KneeGain (v1Knee (st == Style::clean ? 0 : 2), d, kn);
        if (rat < 100.0)
            t *= 1.0 - 1.0 / rat;
        gainDb = -std::max (0.0, std::min (t, rng));
        return true;
    }
    if (st == Style::classic && ! on (p[autoRelease]))
    {
        // the feedback design settles where both stages hold the target t and the reduction is 2t:
        // t = knee ((min (d, range) − 2t) · ratio) · (1 − 1/ratio)
        const auto& shape = v1Knee (1);
        const auto r = std::min (rat, 100.0);
        const auto f = [&] (double t) {
            auto v = v1KneeGain (shape, (std::min (d, rng) - 2.0 * t) * r, kn);
            if (rat < 100.0)
                v *= 1.0 - 1.0 / rat;
            return std::max (0.0, v) - t;
        };
        double lo = 0.0, hi = std::max (0.0, d) + 1.0;
        for (int k = 0; k < 60; ++k)
            (f (0.5 * (lo + hi)) > 0.0 ? lo : hi) = 0.5 * (lo + hi);
        gainDb = -2.0 * 0.5 * (lo + hi);
        return true;
    }
    if (st == Style::vocal)
    {
        auto g = 0.0;
        if (thr < -1.1920928955078125e-07)
            g = 20.0 * std::log10 (dbToGain (inputDb) * (dbToGain (-thr) - 1.0) + 1.0);
        gainDb = -std::max (0.0, std::min (g, rng));
        return true;
    }
    if (st == Style::allround)
    {
        // round 190lg: a steady sine: the slow reading ≈ 0.979 peak + 0.0203 · its mean (2 / π), the fast one 0.91 peak
        const auto& v = versLaw();
        const auto curve = [&] (double lev) {
            auto g = v2Knee (lev - thr, kn);
            if (rat < 100.0)
                g -= g / rat;
            return std::max (0.0, std::min (g, rng));
        };
        const auto sl = curve (inputDb + 20.0 * std::log10 (v.slowLevel + v.slowLowMix * 0.6366197723675814));
        const auto fa = curve (inputDb + 20.0 * std::log10 (v.fastLevel));
        const auto sh = [] (double g) { return 1.0 - std::exp (-g * 0.1151292546497023); };
        const auto r = std::max (sh (sl), std::clamp (v.wBase + v.wSlope * sh (fa), v.wMin, v.wMax) * sh (fa));
        gainDb = 20.0 * std::log10 (std::max (1.0 - r, 1e-9));
        return true;
    }
    if (st == Style::smooth)
    {
        // round 190lp: a steady level: E and D settle, both followers of each at their target
        const auto& s = smoothLaw();
        const auto curve = [&] (double lev, double t, double r) {
            auto g = v2Knee (lev - t, kn);
            if (r < 100.0)
                g -= g / r;
            return std::max (0.0, std::min (g, rng));
        };
        const auto sh = [] (double g) { return 1.0 - std::exp (-g * 0.1151292546497023); };
        const auto e = sh (curve (inputDb, thr + s.eThr, smoothERatio (s, rat)));
        const auto down = std::clamp (std::max (s.g0, s.g1) * e, 0.0, 1.0);
        const auto dIn = inputDb + 20.0 * std::log10 (std::max (1.0 - down, 1e-9));
        const auto c = sh (curve (dIn, thr * smoothThrFactor (s, rat), rat));
        const auto r = std::max ((s.aBase + s.aSlope * s.bGain * c * e) * c, c);
        gainDb = 20.0 * std::log10 (std::max (1.0 - r, 1e-9));
        return true;
    }
    if (st == Style::opEl)
    {
        // round 190lp: a steady level: every follower at its target
        const auto& o = opElLaw();
        const auto curve = [&] (double k, double r) {
            auto g = v2Knee (d, k);
            if (rat < 100.0)
                g -= g / rat;
            return 1.0 - std::exp (-std::max (0.0, std::min (g, r)) * 0.1151292546497023);
        };
        const auto s1 = curve (kn, rng), s2 = curve (o.d2Knee, o.d2Range);
        const auto r = std::sqrt ((o.wA + o.wB) * s1 * s1 + o.wC * s2 * s2);
        gainDb = 20.0 * std::log10 (std::max (1.0 - r, 1e-9));
        return true;
    }
    if (st == Style::upward)
    {
        // round 190mb: a steady level: the follower at L − E (level), so the lift is E (level) less the measured cut
        const auto& u = upLaw();
        const auto lim = std::min (rng, u.rangeMax), s = rat < 1.0e8 ? 1.0 - 1.0 / rat : 1.0;
        const auto a = std::cbrt (std::clamp ((attackMs (p[attack]) - 0.005) / 250.0, 0.0, 1.0));
        gainDb = upExpand (thr - inputDb, kn, s, lim) - u.cutScale * s * std::sqrt (a) * std::max (0.0, thr + lim);
        return true;
    }
    if (st == Style::twoWay && on (p[law]))
    {
        // round 190mj: a steady level in the mid band (the follower reads a steady power exactly)
        const auto& t = ttmLaw();
        gainDb = ttmGain (ttmBand (t, 0, thr, rat, kn, rng, attackMs (p[attack])), inputDb, kn);
        if (gainDb > 0.0 && t.liftFade > 0.0) // (issue #77's fade, as the detector)
            gainDb *= std::clamp ((inputDb - t.fadeFloor) / t.fadeSpan, 0.0, 1.0);
        return true;
    }
    if (st == Style::variMu && on (p[law]))
    {
        // round 190mb: a steady level (the stage's gain at the peak sample): the loop settles where the follower holds
        // the amount its own output sets over the threshold
        const auto& v = vmuLaw();
        const auto k = std::max (0.0, (1.0 - v.kneeShrink * std::sqrt (std::max (0.0, kn) / v.kneeRef)) * vmuRatio (v, rat) - 1.0);
        const auto A = dbToGain (inputDb), dl = std::max (A * v.inGain, 1.0e-4), thrLin = dbToGain (thr);
        const auto h = [&] (double f) { return vmuOver (A * std::abs (vmuGain (v, k * f, dl)), thr, thrLin, kn) - f; };
        double lo = 0.0, hi = A + 1.0;
        for (int i = 0; i < 80; ++i)
            (h (0.5 * (lo + hi)) > 0.0 ? lo : hi) = 0.5 * (lo + hi);
        const auto g = std::abs (vmuGain (v, k * 0.5 * (lo + hi), dl));
        gainDb = -std::max (0.0, std::min (rng, g > 1e-10 ? -20.0 * std::log10 (g) : 200.0)); // (never a boost, as the detector)
        return true;
    }
    if (st == Style::mastering || st == Style::bus || st == Style::punch || st == Style::pumping)
    {
        auto g = v2Knee (d, kn);
        if (rat < 100.0)
            g -= g / rat;
        gainDb = -std::max (0.0, std::min (g, rng));
        return true;
    }
    return false;
}

class Compressor final : public Effect
{
public:
    explicit Compressor (const EffectParams* initial)
    {
        for (auto& d : gainDelay)
            d.assign ((std::size_t) gainCapacity, 1.0f);
        for (auto& b : bandGainDelay)
            for (auto& d : b)
                d.fill (1.0f);
        if (initial != nullptr)
        {
            // kept for prepare(), which configures again at the real rate: the latency stays the slot's (issue #113)
            lastParams = *initial;
            configure (*initial);
        }
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        dc.set (sr);
        fetTone.set (sr);
        splitAudio.set (sr);
        splitSide.set (sr);
        // round 190mj: TTM's own split (the reference's 2^6.4622 Hz / 2^11.2877 Hz)
        splitAudio1.set (sr, clab ("tLo", ttmLowHz), clab ("tHi", ttmHighHz));
        splitSide1.set (sr, clab ("tLo", ttmLowHz), clab ("tHi", ttmHighHz));
        for (auto& h : holdMax) // round 190kw: Mastering's 10 ms hold at up to 8x oversampling
            h.setup ((int) (rate * 0.01 * 8.0) + 8);
        for (auto& band : ttmLaMax) // round 190mj: TTM's look-ahead (up to 20 ms) and its smoothing (up to 10 ms), base rate
            for (auto& h : band)
                h.setup ((int) (rate * 0.02) + 8);
        for (auto& band : ttmAvg)
            for (auto& ch : band)
                for (auto& a : ch)
                    a.setup ((int) (rate * 0.01) + 8);
        ttmUp.assign ((std::size_t) (6 * maxBlock * maxFactor), 0.0f); // round 190mj: TTM's band gains at the oversampled rate
        latencyMs = -1.0; // re-apply the delays
        configure (lastParams);
        reset();
    }

    void reset() noexcept override
    {
        audioDelay.reset();
        dryLa.reset();
        dryDelay.reset();
        for (auto& d : gainDelay)
            std::fill (d.begin(), d.end(), 1.0f);
        gainPos = 0;
        for (auto& o : os)
            o.reset();
        for (auto& o : osSide)
            o.reset();
        for (auto& o : ttmOsGain)
            o.reset();
        splitAudioUp.reset();
        for (auto& b : scEq)
            for (auto& s : b)
                s.reset();
        for (auto& d : det)
            d = {};
        for (auto& h : holdMax)
            h.clear();
        for (auto& band : ttmLaMax)
            for (auto& h : band)
                h.clear();
        for (auto& band : ttmAvg)
            for (auto& ch : band)
                for (auto& a : ch)
                    a.clear();
        for (auto& pk : peaks)
            pk.fill (0.0f);
        dc.reset();
        fetTone.reset();
        ampPrev[0] = ampPrev[1] = 0.0;
        splitAudio.reset();
        oeZ = {};
        splitSide.reset();
        splitAudio1.reset();
        splitSide1.reset();
        for (auto& b : bandDet)
            for (auto& d : b)
                d = {};
        for (auto& b : bandGainDelay)
            for (auto& d : b)
                d.fill (1.0f);
        bandGainPos = {};
    }

    int latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }

    void setSidechain (const float* l, const float* r) noexcept override
    {
        scL = l;
        scR = r;
    }
    // round 137: the trigger's clock and notes
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }
    void setPlaying (bool playing) noexcept override { transportPlaying = playing; }
    void setHeldNotes (const NoteMask* channels, int count) noexcept override
    {
        held = channels;
        heldCount = count;
    }
    bool followsHeldNotes() const noexcept override { return true; } // (round 190kz)
    bool followsHeldNotesWith (const EffectParams& p) const noexcept override // (the MIDI trigger on a channel: chunk())
    {
        return triggerOf (p[trigger]) == Trigger::midi && triggerChannelOf (p[triggerChannel]) >= 0;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        lastParams = p;
        configure (p);
        designScEq (p);
        blockBpm = bpm > 1.0 ? bpm : 120.0;
        for (int off = 0; off < n; off += maxBlock)
            chunk (l + off, r + off, std::min (maxBlock, n - off), p, off);
        scL = scR = nullptr;
    }

private:
    static constexpr int maxBlock = 1024, maxFactor = 8;
    static constexpr int gainCapacity = 1 << 17; // the side chain's lead at up to 8x the rate (20 ms at 768 kHz)
    static constexpr int peakWindow = 4096;

    // the detector of one channel
    struct Detector
    {
        double env = -120.0, sustain = 0.0, gain = 0.0;
        double lin = 1e-6; // round 190v: two-way's follower (linear amplitude)
        double peak = -120.0; // round 173d: the input's peak (instant attack, the detector's release): the lift's ceiling
        double cs = 0.0, c1 = 0.0, c2 = 0.0; // round 190kt: the console law's log gain (nepers) and its two capacitors
        double fe[3] {}, fg = 1.0; // round 190ky: the FET law's three followers and the gain it applies now (linear)
        int holdLeft = 0, gainHold = 0, peakPos = 0;
        double v0 = 0.0, v1 = 0.0, v2 = 0.0, held = 0.0; // round 190kw: the V1 styles' follower (reduction, dB) and hold
        double q0 = 0.0, q1 = 0.0, q2 = 0.0, lp1 = 0.0, lp2 = 0.0; // round 190kw: the V2 styles' followers, Mastering's smoother (q*: c* are the console law's)
        V2Cache k0, k1, k2;
        double va1 = 0.0, va2 = 0.0, vb1 = 0.0, vb2 = 0.0, vF = 0.0; // round 190lg: Versatile's two low-passes, its slew limiter
        double q3 = 0.0; // round 190lp: a fourth follower (Smooth's F1)
        V2Cache k3;
        double tHeld = 0.0, tHold = 0.0; // round 190mj: TTM's hold (the held peak, the processor's output)
        int tHoldLeft = 0;
    };

    // per-chunk settings at the rate the detector runs at
    struct Ctx
    {
        Style st;
        double thr, rat, kn, rng, aA, relMs, rate, slowCoef, gainBlend, aP = 0.0;
        int holdSamples, window;
        bool autoRel;
        // round 190v: two-way / vari-mu run their measured laws, set up once per chunk
        Curve curve = Curve::downward;
        const StyleDef* def = &styleDefs[0];
        TwoWayLaw twoWay {};
        VariMuLaw variMu {};
        double aRl = 0.0; // two-way: the linear follower's release
        // round 190kt: the console law (threshold nepers, loop gain, knee nepers, attack / release / slow-cap rates)
        double cT = 0.0, cK = 1.0, cKnee = 1.0, cA = 0.0, cR = 0.0, cA2 = 0.0, cR2 = 0.0;
        // round 190ky: the FET law (its voicing, the first term's knee / width / slope, the followers' rates and weights)
        const FetVoiceFit* fv = nullptr;
        double fT1 = 0.0, fW1 = 1.0, fS1 = 0.0;
        double fA[3] {}, fR[3] {}, fWt[3] {};
        // round 190kw: the V1 law (revision 1, Clean / Classic / Opto): attack and release in samples
        const V1Law* v1 = nullptr;
        const V1Knee* v1Shape = nullptr;
        double v1A = 0.0, v1R = 0.0;
        // round 190kw: the V2 law (revision 1): 3 vocal, 4 mastering, 5 bus, 6 punch, 7 pumping (the reference's order)
        int v2 = 0;
        V2Follow f0, f1, f2;
        double vocalBase = 0.0; // Vocal: the second follower's release before the programme adds to it
        int holdN = 0;          // Mastering: the reduction's 10 ms peak hold
        double lpB0 = 0.0, lpB1 = 0.0, lpB2 = 0.0, lpA1 = 0.0, lpA2 = 0.0;
        // round 190lg: Versatile (v2 = 8): the two low-passes (b0, b1, b2, a1, a2), the slew limiter's steps (dB per sample)
        std::array<double, 5> vA {}, vB {};
        double vUp = 0.0, vDown = 0.0;
        // round 190lp: Smooth (v2 = 9): F1 (f3), E's threshold and ratio, D's threshold
        V2Follow f3;
        double sThrE = 0.0, sRatE = 1.0, sThrD = 0.0;
        // round 190lp: Op-El's high shelf on D1's input (the signed side chain, per channel): b0 b1 b2 a1 a2
        std::array<double, 5> oeEq {};
        bool oeShelf = false;
        // round 190mb: Upward (v2 = 11): the expansion's slope (1 − 1/ratio), the upward range, the lift at silence, the cut
        double uSlope = 0.0, uLim = 0.0, uSilence = 0.0, uCut = 0.0;
        bool uSafe = true;
        // round 190mb: Vari-Mu (v2 = 12): the loop gain r' − 1, the threshold (linear), the follower's coefficients
        double vmK = 0.0, vmThrLin = 1.0, vmAtt = 1.0, vmRel = 1.0, vmAutoK = 1.0;
        bool vmAuto = false;
        // round 190mj: TTM (v2 = 13, Two-way under the revision): each band's computers and follower, the band being run,
        // the hold (samples, fall factor), the look-ahead hold and smoothing lengths (samples)
        std::array<TtmBand, 3> ttm {};
        std::array<V2Follow, 3> ttmF {};
        int ttmB = 0, ttmHoldN = 0, ttmLaN = 0, ttmAvgN = 1;
        double ttmHoldRel = 0.0;
    };
    double styleGain (const Ctx& x, double level) const noexcept
    {
        if (x.curve == Curve::twoWay)
            return std::clamp (twoWayGain (x.twoWay, level), -x.rng, std::min (x.rng, upwardCapDb));
        if (x.curve == Curve::variMu)
            return std::max (-x.rng, variMuGain (x.variMu, level));
        return curveGain (*x.def, level, x.thr, x.rat, x.kn, x.rng);
    }

    // latency = the chosen maximum look-ahead (+ the oversampler when on), as the reference does (§2: 960 samples
    // for "maximum 20 ms" whatever the look-ahead knob says)
    void configure (const EffectParams& p) noexcept
    {
        const auto la = maxLookaheadMs (p[maxLookahead]);
        const auto factor = oversamplingOf (p[oversampling]);
        if (la == latencyMs && factor == osFactor)
            return;
        latencyMs = la;
        osFactor = factor;
        laSamples = (int) std::lround (la * 0.001 * sr);
        const auto osLat = osFactor > 1 ? mixsuite::Oversampler::latency() : 0;
        audioDelay.setLength (laSamples);
        dryLa.setLength (laSamples);
        dryDelay.setLength (osLat);
        for (auto* set : { &os, &osSide })
            for (auto& o : *set)
                o.setFactor (std::max (2, osFactor));
        for (auto& o : ttmOsGain) // round 190mj: TTM's band gains to the oversampled rate, and its split there
            o.setFactor (std::max (2, osFactor));
        splitAudioUp.set (sr * std::max (2, osFactor), ttmLowHz, ttmHighHz);
        splitAudioUp.reset();
        latency.store (laSamples + osLat, std::memory_order_relaxed);
    }

    void designScEq (const EffectParams& p) noexcept
    {
        EffectParams fp;
        scEqParams (p, fp);
        bool any = false;
        for (int b = 0; b < scBands; ++b)
        {
            std::array<facet::Section, facet::maxSections> sec;
            const auto count = facet::design (fp, b, sr, sec);
            scCount[(std::size_t) b] = count;
            for (int k = 0; k < count; ++k)
            {
                auto& q = scEq[(std::size_t) b][(std::size_t) k];
                q.b0 = sec[(std::size_t) k].b0; q.b1 = sec[(std::size_t) k].b1; q.b2 = sec[(std::size_t) k].b2;
                q.a1 = sec[(std::size_t) k].a1; q.a2 = sec[(std::size_t) k].a2;
            }
            any = any || count > 0;
        }
        scEqOn = any;
    }

    Ctx context (const EffectParams& p, double rate) const noexcept
    {
        Ctx c;
        c.st = styleOf (p);
        const auto& sd = defOf (p);
        c.def = &sd;
        c.thr = on (p[autoThreshold]) ? autoThresholdDb (thresholdDb (p[threshold]), programDb) : thresholdDb (p[threshold]);
        c.rat = ratioOf (p[ratio]);
        c.kn = kneeDb (p[knee]);
        c.rng = std::max (0.01, rangeDb (p[range]));
        // round tp172b [EXEC] (Pro-C 3 trial, equal-RMS sine / noise stairs and −40 → −6 → −40 dBFS steps): Clean's, Allround's and
        // Opto's detector never follows faster than τ = 1 ms (noise at the fastest attack read only ~1.5 dB over a sine of the same RMS,
        // not ~4.5), and the release shortens as the attack lengthens — 50 % release 63 / 84 / 66 ms at attack 0.1 / 10 /
        // 30 ms (release 100 / 200 / 200), ~17 ms at attack = release = 100 ms, where a steady sine sags 4.5 dB
        const auto att = attackMs (p[attack]);
        const auto releaseByAttack = (1.0 + std::pow (10.0 / 50.0, 1.5)) / (1.0 + std::pow (att / 50.0, 1.5));
        c.aA = coef (std::max (sd.attackFloorMs, att * sd.attackScale), rate);
        c.relMs = std::max (0.5, releaseTau (sd, releaseMs (p[release])) * releaseByAttack);
        c.curve = sd.curve;
        if (sd.curve == Curve::twoWay)
        {
            // round 190v [EXEC] (the reference's level steps at releases 10 … 2500 ms and attacks 0.25 … 100 ms): a
            // follower in linear amplitude, attack τ = 0.12 × knob, release τ = 0.19 × knob (the gain is back half-way
            // after about a quarter of the release knob; the release does not shorten with the attack). On a steady
            // sine it settles under the peak as the two get closer, which is the reference's reading too
            c.relMs = std::max (0.5, releaseTau (sd, releaseMs (p[release])));
            c.aRl = coef (c.relMs, rate);
            c.twoWay = twoWayLaw (c.thr, c.rat, c.kn, att);
        }
        else if (sd.curve == Curve::variMu)
        {
            // round 190v [EXEC]: vari-mu's release does not shorten with a slow attack (a 1 kHz sine at attack 100 ms
            // reads 3 dB lower than at 0.25 ms on the reference; with the shortened release it read 8.5 dB lower)
            c.relMs = std::max (0.5, releaseTau (sd, releaseMs (p[release])));
            c.variMu = variMuLaw (c.thr, c.rat, c.kn);
        }
        c.aP = coef (c.relMs, rate);
        c.autoRel = on (p[autoRelease]) && sd.curve != Curve::twoWay && sd.curve != Curve::twoWayClassic;
        if (sd.curve == Curve::console) // round 190kt: the RC rates of the knobs' time constants, per sample
        {
            c.relMs = releaseMs (p[release]);
            c.cT = c.thr * nepersPerDb;
            c.cK = std::max (0.0, std::min (c.rat, 1.0e4) - 1.0);
            c.cKnee = std::max (1.0e-4, 2.0 * c.kn * nepersPerDb); // (the knee in nepers = twice the knob's)
            c.cA = std::min (1.0, 1000.0 / (att * rate));
            c.cR = std::min (1.0, 1000.0 / (c.relMs * rate));
            c.cA2 = c.cA * consoleAutoCapRatio;
            c.cR2 = std::min (1.0, 1000.0 / (consoleAutoSlowMs * rate));
        }
        if (sd.curve == Curve::fet) // round 190ky: the followers' one-pole rates per sample
        {
            const auto& f = fetVoiceFits[std::max (0, fetVoiceOf (p))];
            c.fv = &f;
            c.fT1 = c.thr;
            c.fW1 = std::max (0.1, c.kn);
            c.fS1 = std::max (0.0, std::min (c.rat, 1.0e4) - 1.0);
            const auto relMs = releaseMs (p[release]);
            const double am[3] { f.a1, f.a2, f.a3 }, rm[3] { f.r1, f.r2, f.r3 };
            for (int k = 0; k < 3; ++k)
            {
                c.fA[k] = 1.0 - std::exp (-1000.0 / (std::max (1.0e-4, att * am[k]) * rate));
                c.fR[k] = std::exp (-1000.0 / (std::max (1.0e-3, relMs * rm[k]) * rate));
            }
            c.fWt[0] = f.w1;
            c.fWt[1] = f.w2f;
            c.fWt[2] = 1.0 - f.w1 - f.w2f;
        }
        c.rate = rate;
        c.slowCoef = coef (1000.0, rate);
        c.gainBlend = sd.gainBlend;
        if (sd.curve == Curve::variMu)
        {
            // round 190al [EXEC] (the reference's −40 → −6 dBFS step at attack 100 ms): the reduction starts at once and
            // closes in on its value like a smoothed gain (−2.1 / −4.8 / −6.3 / −7.0 dB after 15 / 30 / 45 / 60 ms, towards
            // −7.8), where the smoothed level did nothing for 45 ms and settled 1 dB short. At 30 ms and under, the
            // smoothed level is the better match (10 ms: step and stairs within 0.22 dB; the 30 ms captures get worse
            // with any share of it). At 100 ms a share of 0.6 is the most that makes no capture worse (pink noise
            // loses beyond it); in between, by the logarithm of the attack
            constexpr double fromMs = 30.0, toMs = 100.0, share = 0.6;
            c.gainBlend = share * std::clamp (std::log (std::max (att, 1e-3) / fromMs) / std::log (toMs / fromMs), 0.0, 1.0);
        }
        c.holdSamples = (int) (holdMs (p[hold]) * 0.001 * rate);
        c.window = sd.windowMs > 0.0 ? std::clamp ((int) (sd.windowMs * 0.001 * rate), 1, peakWindow) : 0;
        // (with Auto release on, the V1 styles keep the fitted law: the reference's auto-release table is not rebuilt)
        if (revisionOn (p) && ! on (p[autoRelease]) && (c.st == Style::clean || c.st == Style::classic || c.st == Style::opto))
        {
            const auto& w = v1Laws[c.st == Style::clean ? 0 : (c.st == Style::classic ? 1 : 2)];
            c.v1 = &w;
            c.v1Shape = &v1Knee (w.kind);
            const auto a3 = std::clamp ((att - 0.005) / 250.0, 0.0, 1.0);
            const auto rel = std::clamp ((releaseMs (p[release]) - 10.0) / 2490.0, 0.0, 1.0);
            const auto attS = w.lo + w.span * a3;
            const auto clampA = std::clamp (attS, w.attackMin, w.lo + w.span);
            const auto relS = std::max (w.relBase * 0.1, rel * w.relScale + w.relBase
                                        - std::sqrt ((clampA - w.attackMin) / (w.lo + w.span - w.attackMin)) * 0.9 * w.relDip);
            c.v1A = attS * rate;
            c.v1R = relS * rate;
            c.window = 0;
        }
        else if (revisionOn (p) && (c.st == Style::vocal || c.st == Style::mastering || c.st == Style::bus
                                      || c.st == Style::punch || c.st == Style::pumping))
        {
            // the knob laws (REA_rodada190kw §2): attack a³ · 0.25 s, release 2.5 s · tan (1.2 r²) / tan (1.2), the
            // auto release's constant 3.6 r² + 0.1
            const auto a3 = std::clamp ((att - 0.005) / 250.0, 0.0, 1.0);
            const auto rel = std::clamp ((releaseMs (p[release]) - 10.0) / 2490.0, 0.0, 1.0);
            const auto r2 = std::atan (rel * std::tan (1.2)) / 1.2;
            const auto attS = a3 * 0.25, relS = rel * 2.5;
            auto autoK = r2 * 3.6 + 0.1;
            c.v2 = c.st == Style::vocal ? 3 : c.st == Style::mastering ? 4 : c.st == Style::bus ? 5 : c.st == Style::punch ? 6 : 7;
            c.f0 = c.f1 = c.f2 = {};
            V2Follow* last = &c.f0;
            switch (c.v2)
            {
                case 3:
                    c.f0.att = 0.25; c.f0.rel = 0.5;
                    c.f1.att = a3 * 0.4; c.f1.p5 = 0.25;
                    c.vocalBase = std::min (0.5 * std::sqrt (a3), 10.0) + rel + 0.005;
                    autoK *= 0.5;
                    last = &c.f1;
                    break;
                case 4:
                    c.f0.att = std::max (0.0, 0.025 - relS * 0.1) + attS + 0.025; c.f0.p6 = 1.0;
                    c.f1.att = attS + 5e-5; c.f1.rel = relS + 0.1;
                    c.holdN = (int) (rate * 0.01);
                    {
                        // a 10 Hz two-pole low-pass on the held reduction (Butterworth, bilinear)
                        const auto k = std::tan (3.141592653589793 * 10.0 / rate), q = 1.0 / std::sqrt (2.0);
                        const auto n = 1.0 / (1.0 + k / q + k * k);
                        c.lpB0 = k * k * n; c.lpB1 = 2.0 * c.lpB0; c.lpB2 = c.lpB0;
                        c.lpA1 = 2.0 * (k * k - 1.0) * n; c.lpA2 = (1.0 - k / q + k * k) * n;
                    }
                    last = &c.f1;
                    break;
                case 5:
                    c.f0.att = 0.35; c.f0.rel = 0.8;
                    c.f1.rel = relS * 0.1 + 0.015; c.f1.p5 = 0.0;
                    c.f2.att = attS + 0.0003; c.f2.rel = relS + 0.015; c.f2.p1 = 0.5; c.f2.p5 = 0.75; c.f2.p6 = 0.3;
                    last = &c.f2;
                    break;
                case 6:
                    c.f0.att = attS + 0.0001; c.f0.rel = relS + 0.05; c.f0.p1 = 0.9;
                    break;
                default:
                    c.f0.rel = 0.5 * relS + 0.025;
                    c.f1.att = attS; c.f1.rel = 0.5 * relS + 0.025;
                    c.f2.att = 5e-5; c.f2.rel = 0.5 * relS + 0.005; c.f2.p5 = 0.25;
                    last = &c.f2;
                    break;
            }
            last->autoRel = on (p[autoRelease]);
            last->autoK = autoK;
            c.window = 0;
        }
        else if (on (p[revision]) && c.st == Style::allround)
        {
            // round 190lg: Versatile (see VersLaw)
            const auto& v = versLaw();
            const auto a3 = std::clamp ((att - 0.005) / 250.0, 0.0, 1.0);
            const auto rel = std::clamp ((releaseMs (p[release]) - 10.0) / 2490.0, 0.0, 1.0);
            const auto r2 = std::atan (rel * std::tan (1.2)) / 1.2;
            const auto attS = v.attBase + v.attSpan * a3, relS = 0.01 + 2.49 * rel;
            c.v2 = 8;
            c.f0 = {};
            c.f0.att = attS; c.f0.rel = relS;
            c.f0.autoRel = on (p[autoRelease]);
            c.f0.autoK = v.autoA * r2 + v.autoB;
            const auto lowPass = [rate] (double hz, std::array<double, 5>& q) {
                // a two-pole low-pass (Butterworth, bilinear)
                const auto k = std::tan (3.141592653589793 * std::min (hz, 0.45 * rate) / rate), qq = 1.0 / std::sqrt (2.0);
                const auto n = 1.0 / (1.0 + k / qq + k * k);
                q = { k * k * n, 2.0 * k * k * n, k * k * n, 2.0 * (k * k - 1.0) * n, (1.0 - k / qq + k * k) * n };
            };
            lowPass (v.slowLowHz, c.vA);
            lowPass (v.fastHz, c.vB);
            c.vUp = 1.0 / std::max (0.1, rate * versT3 (v, attS));
            c.vDown = 1.0 / std::max (0.1, rate * versT4 (v, relS));
            c.window = 0;
        }
        else if (on (p[revision]) && c.st == Style::smooth)
        {
            // round 190lp: Smooth (see SmoothLaw)
            const auto& s = smoothLaw();
            const auto a3 = std::clamp ((att - 0.005) / 250.0, 0.0, 1.0);
            const auto rel = std::clamp ((releaseMs (p[release]) - 10.0) / 2490.0, 0.0, 1.0);
            const auto r2 = std::atan (rel * std::tan (1.2)) / 1.2;
            const auto attS = clab ("sAtt", s.attScale) * a3, relS = s.relBase + s.relSpan * rel;
            auto k = r2 * 3.6 + 0.1;
            k = (k + s.kA * std::pow (std::max (0.0, k - s.kKnee), s.kP)) * s.kScale;
            const auto autoOn = on (p[autoRelease]);
            c.v2 = 9;
            c.f0 = c.f1 = c.f2 = c.f3 = {};
            c.f0.att = attS; c.f0.rel = relS * s.c0Rel * clab ("c0Rel", 1.0);
            c.f0.autoRel = autoOn; c.f0.autoK = k * s.c0Auto;
            c.f1.att = attS * s.c1Att * clab ("c1Att", 1.0); c.f1.rel = relS * s.c1Rel * clab ("c1Rel", 1.0);
            c.f1.p1 = clab ("c1P1", s.c1P1); c.f1.p6 = clab ("c1P6", s.c1P6);
            c.f1.autoRel = autoOn; c.f1.autoK = k;
            c.f2.att = attS * s.f0Att; c.f2.rel = relS * s.f0Rel * clab ("f0Rel", 1.0); c.f2.p5 = clab ("f0P5", s.f0P5);
            c.f2.autoRel = autoOn; c.f2.autoK = k * s.f0Auto;
            c.f3.att = attS * s.f1Att; c.f3.rel = relS * s.f1Rel * clab ("f1Rel", 1.0); c.f3.p6 = clab ("f1P6", s.f1P6);
            c.f3.autoRel = autoOn; c.f3.autoK = k * s.f1Auto;
            c.sThrE = c.thr + clab ("eThr", s.eThr);
            c.sRatE = c.rat >= 1.0e8 ? s.eRatTop + s.eRatSpan : smoothERatio (s, c.rat);
            c.sThrD = c.thr * (clab ("p1", 1.0) > 0.0 ? smoothThrFactor (s, c.rat) : 1.0);
            c.window = 0;
        }
        else if (revisionOn (p) && c.st == Style::upward)
        {
            // round 190mb: Upward (see UpLaw)
            const auto& u = upLaw();
            const auto a3 = std::clamp ((att - 0.005) / 250.0, 0.0, 1.0);
            const auto rel = std::clamp ((releaseMs (p[release]) - 10.0) / 2490.0, 0.0, 1.0);
            const auto r2 = std::atan (rel * std::tan (1.2)) / 1.2; // the release knob's position, squared
            c.v2 = 11;
            c.f0 = c.f1 = {};
            c.f0.att = u.attScale * a3;
            c.f0.rel = u.relBase + u.relSpan * upRelShape (u, std::sqrt (r2)) * clab ("uRel", 1.0);
            c.f0.autoRel = on (p[autoRelease]);
            c.f0.autoK = u.autoA * r2 + u.autoB;
            c.f1.rel = clab ("uLev", u.levelRel);
            c.uSlope = c.rat < 1.0e8 ? 1.0 - 1.0 / c.rat : 1.0;
            c.uLim = std::min (c.rng, clab ("uMax", u.rangeMax));
            c.uSilence = upExpand (c.thr - u.silenceDb, c.kn, c.uSlope, c.uLim);
            c.uSafe = clab ("uSafe", 1.0) > 0.0;
            c.uCut = clab ("uCut", u.cutScale) * c.uSlope * std::sqrt (std::cbrt (a3)) * std::max (0.0, c.thr + c.uLim);
            c.window = 0;
        }
        else if (revisionOn (p) && c.st == Style::variMu && on (p[law])) // (law off: a project saved before round 190v)
        {
            // round 190mb: Vari-Mu (see VmuLaw)
            const auto& v = vmuLaw();
            const auto a = std::cbrt (std::clamp ((att - 0.005) / 250.0, 0.0, 1.0));
            const auto rel = std::clamp ((releaseMs (p[release]) - 10.0) / 2490.0, 0.0, 1.0);
            const auto r2 = std::atan (rel * std::tan (1.2)) / 1.2;
            const auto attS = std::clamp (v.attA * (a * a + a * a * a - a * a * a * a) + v.attMin, v.attMin, v.attMax);
            const auto relS = v.relBase + v.relSpan * std::tan (1.2 * r2);
            c.v2 = 12;
            c.vmK = std::max (0.0, (1.0 - v.kneeShrink * std::sqrt (std::max (0.0, c.kn) / v.kneeRef)) * vmuRatio (v, c.rat) - 1.0)
                    * clab ("vmK", 1.0);
            c.vmThrLin = std::exp (c.thr * 0.1151292546497023);
            c.vmAtt = 1.0 - std::exp (-2.302585092994046 / (attS * clab ("vmAtt", 1.0) * rate));
            c.vmRel = 1.0 - std::exp (-2.302585092994046 / (relS * clab ("vmRel", 1.0) * rate));
            c.vmAuto = on (p[autoRelease]);
            c.vmAutoK = v.autoA * r2 + v.autoB;
            c.window = 0;
        }
        else if (revisionOn (p) && c.st == Style::twoWay && on (p[law]))
        {
            // round 190mj: TTM (see TtmLaw); the bands run at the base rate
            const auto& t = ttmLaw();
            const auto attS = 0.25 * std::clamp ((att - 0.005) / 250.0, 0.0, 1.0);
            const auto relS = 0.01 + 2.49 * std::clamp ((releaseMs (p[release]) - 10.0) / 2490.0, 0.0, 1.0);
            c.v2 = 13;
            for (int b = 0; b < 3; ++b)
            {
                c.ttm[(std::size_t) b] = ttmBand (t, b, c.thr, c.rat, c.kn, c.rng, att);
                auto& f = c.ttmF[(std::size_t) b];
                f = {};
                f.att = t.attShare[b] * attS;
                f.rel = t.relShare[b] * relS;
                f.autoRel = on (p[autoRelease]);
                const auto tb = std::min (t.autoShare[b] * relS, t.autoRef) / t.autoRef;
                f.autoK = t.autoA * tb * tb + t.autoB;
            }
            const auto holdT = holdMs (p[hold]);
            c.ttmHoldN = (int) (holdT * 0.001 * rate);
            c.ttmHoldRel = holdT * t.holdRel;
            const auto la = std::max (0.0, std::min (lookaheadMs (p[lookahead]), latencyMs));
            c.ttmLaN = (int) (la * 0.001 * rate);
            c.ttmAvgN = (int) (rate * 0.001 * std::clamp (0.5 * la - t.smoothTrim, 0.0, 20.0)) + 1;
            c.holdSamples = 0;
            c.window = 0;
        }
        else if (on (p[revision]) && c.st == Style::opEl)
        {
            // round 190lp: Op-El (see OpElLaw)
            const auto& o = opElLaw();
            const auto a3 = std::clamp ((att - 0.005) / 250.0, 0.0, 1.0);
            const auto rel = std::clamp ((releaseMs (p[release]) - 10.0) / 2490.0, 0.0, 1.0);
            const auto attS = 0.25 * a3, relS = 2.5 * rel;
            c.v2 = 10;
            c.f0 = c.f1 = c.f2 = {};
            c.f0.att = o.aAtt * attS + o.aAttBase; c.f0.rel = o.aRel * relS + o.aRelBase;
            c.f1.att = o.bAtt * attS + o.bAttBase; c.f1.rel = o.bRel * relS + o.bRelBase;
            c.f2.att = o.cAtt * attS + o.cAttBase; c.f2.rel = o.cRel * relS + o.cRelBase;
            if (const auto gDb = clab ("oeShelf", o.shelfDb); gDb != 0.0)
            {
                // a high shelf (RBJ cookbook form), LYNX's own reading of the reference's wide shelf
                const auto A = std::pow (10.0, gDb / 40.0);
                const auto w = 2.0 * 3.141592653589793 * std::min (clab ("oeHz", o.shelfHz), clab ("oeAt", o.shelfAtRate) * rate) / rate;
                const auto cw = std::cos (w), al = std::sin (w) / (2.0 * clab ("oeQ", o.shelfQ)), sa = 2.0 * std::sqrt (A) * al;
                const auto a0 = (A + 1.0) - (A - 1.0) * cw + sa;
                c.oeEq = { A * ((A + 1.0) + (A - 1.0) * cw + sa) / a0, -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0,
                           A * ((A + 1.0) + (A - 1.0) * cw - sa) / a0, 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0,
                           ((A + 1.0) - (A - 1.0) * cw - sa) / a0 };
                c.oeShelf = true;
            }
            c.window = 0;
        }
        return c;
    }

    // one detector step: the level → the gain (dB) the compressor applies now
    // round 190kw: the V1 styles (REA_rodada190kw §1): the instantaneous reduction (dB, ≥ 0) of the sample level, then
    // the style's two-stage follower; returns the gain (dB, ≤ 0)
    double detectV1 (int c, double level, const Ctx& x) noexcept
    {
        auto& d = detCur[c];
        const auto& w = *x.v1;
        const auto in = std::max (-200.0, 20.0 * std::log10 (level + 1e-30));
        auto over = in - x.thr;
        const auto r = std::min (x.rat, 100.0);
        if (w.kind == 1)
            over = (std::min (over, x.rng) - d.v0) * r;
        auto t = v1KneeGain (*x.v1Shape, over, x.kn);
        if (x.rat < 100.0)
            t *= 1.0 - 1.0 / x.rat;
        if (w.kind != 1)
            t = std::min (t, x.rng);
        t = std::max (t, 0.0);
        const auto P = x.rate, A = x.v1A, R = x.v1R;
        if (w.kind == 0) // Clean: a slow stage (memory) under the main one
        {
            d.v1 += (t - d.v1) * v1Coef (t <= d.v1 ? 0.2 * P + R : 100.0 * A + 2.0 * P);
            const auto y = std::max (d.v1, t);
            const auto dt = y <= d.v0 ? R : std::exp (std::max (d.v0 - y, -25.0)) * (0.05 * P - 0.2 * A) + A;
            d.v0 += (y - d.v0) * v1Coef (dt);
            d.v2 = d.v0 - d.v1;
        }
        else if (w.kind == 1) // Classic: the reduction is the two stages' sum
        {
            d.v1 += (t - d.v1) * v1Coef (t <= d.v1 ? 2.0 * P - 1.8 * A : 0.43 * P + 100.0 * A);
            const auto y = std::max (d.v1, t);
            const auto dt = y <= d.v2 ? R : std::exp (std::max (d.v2 - y, -25.0)) * 10.0 * (2.0 * P - A) + A;
            d.v2 += (y - d.v2) * v1Coef (dt);
            d.v0 = d.v2 + d.v1;
        }
        else // Opto: the release slows for large steps (square root of the distance past 1 dB), then a 4 ms smoother
        {
            if (t <= d.v0)
            {
                auto v = t - d.v1;
                if (v < -1.0)
                    v = -std::sqrt (-v);
                d.v1 += v1Coef (R) * v;
                d.v0 += v1Coef (0.004 * P) * (d.v1 - d.v0);
            }
            else
            {
                d.v0 += (t - d.v0) * v1Coef (std::exp (std::max (d.v0 - t, -25.0)) * (0.05 * P - 0.2 * A) + A);
                d.v1 = d.v0;
            }
        }
        auto gr = d.v0;
        if (x.holdSamples > 0) // the reduction's peaks held for the Hold time
        {
            if (gr >= d.held)
            {
                d.held = gr;
                d.holdLeft = x.holdSamples;
            }
            else if (d.holdLeft > 0)
            {
                --d.holdLeft;
                gr = d.held;
            }
            else
                d.held = gr;
        }
        d.sustain = x.slowCoef * d.sustain + (1.0 - x.slowCoef) * gr;
        return -gr;
    }

    // round 190kw: the V2 styles (REA_rodada190kw §2); `x0`: the side chain's level (linear magnitude, after the link)
    double detectV2 (int c, double x0, const Ctx& x, double alt = -1.0) noexcept
    {
        auto& d = detCur[c];
        const auto rate = x.rate;
        const auto mag = std::abs (x0);
        const auto levelDb = mag > 1e-10 ? std::max (-180.0, 20.0 * std::log10 (mag)) : -180.0;
        const auto curve = [&] (double lev) {
            auto g = v2Knee (lev - x.thr, x.kn);
            if (x.rat < 100.0)
                g -= g / x.rat;
            return std::max (0.0, std::min (g, x.rng));
        };
        const auto share = [] (double grDb) { return 1.0 - std::exp (-grDb * 0.1151292546497023); };
        const auto toDb = [] (double r) { return std::min (180.0, -20.0 * std::log10 (std::max (std::abs (1.0 - r), 1e-9))); };
        double gr = 0.0;
        switch (x.v2)
        {
            case 3: // Vocal: its own curve, 20 log10 (1 + |x| (10^(−thr / 20) − 1)), no ratio
            {
                auto g = 0.0;
                if (x.thr < -1.1920928955078125e-07)
                    g = std::max (-180.0, 20.0 * std::log10 (mag * (std::exp (-x.thr * 0.1151292546497023) - 1.0) + 1.0));
                const auto r = share (std::max (0.0, std::min (g, x.rng)));
                v2Step (d.q0, r, x.f0, rate, d.k0);
                auto f1 = x.f1;
                f1.rel = x.vocalBase + 10.0 * std::pow (std::max (d.q0, 0.0), 5.5);
                v2Step (d.q1, std::max (std::abs (r), std::abs (d.q0)), f1, rate, d.k1);
                gr = toDb (d.q1);
                break;
            }
            case 4: // Mastering: the reduction share, held 10 ms, smoothed, its own slow follower, the main follower
            {
                const auto r = share (curve (levelDb));
                const auto h = holdMax[(std::size_t) c].push (r, x.holdN);
                const auto e = x.lpB0 * h + d.lp1;
                d.lp1 = x.lpB1 * h - x.lpA1 * e + d.lp2;
                d.lp2 = x.lpB2 * h - x.lpA2 * e;
                v2Step (d.q0, e, x.f0, rate, d.k0);
                v2Step (d.q1, std::max (std::abs (r), std::abs (d.q0)), x.f1, rate, d.k1);
                gr = toDb (d.q1);
                break;
            }
            case 5: // Bus: dB domain, three followers behind maxima
            {
                const auto g = curve (levelDb);
                v2Step (d.q0, g, x.f0, rate, d.k0);
                v2Step (d.q1, std::max (std::abs (g), std::abs (d.q0)), x.f1, rate, d.k1);
                gr = v2Step (d.q2, std::max (std::abs (g), std::abs (d.q1)), x.f2, rate, d.k2);
                break;
            }
            case 8: // round 190lg: Versatile (see VersLaw)
            {
                const auto& v = versLaw();
                const auto lp = [] (const std::array<double, 5>& q, double in, double& z1, double& z2) {
                    const auto y = q[0] * in + z1;
                    z1 = q[1] * in - q[3] * y + z2;
                    z2 = q[2] * in - q[4] * y;
                    return y;
                };
                const auto low = lp (x.vA, x0, d.va1, d.va2);
                const auto sA = v.slowLevel * x0 + v.slowLowMix * std::max (low, 0.0);
                const auto sB = lp (x.vB, v.fastLevel * x0, d.vb1, d.vb2);
                const auto dbOf = [] (double m) { m = std::abs (m); return m > 1e-10 ? std::max (-180.0, 20.0 * std::log10 (m)) : -180.0; };
                const auto slow = v2Step (d.q0, share (curve (dbOf (sA))), x.f0, rate, d.k0);
                const auto step = share (curve (dbOf (sB))) - d.vF;
                d.vF += step > 0.0 ? std::min (step, x.vUp) : std::max (step, -x.vDown);
                gr = toDb (std::max (slow, std::clamp (v.wBase + v.wSlope * d.vF, v.wMin, v.wMax) * d.vF));
                break;
            }
            case 9: // round 190lp: Smooth (see SmoothLaw)
            {
                const auto& s = smoothLaw();
                const auto at = [&] (double lev, double t, double r) {
                    auto g = v2Knee (lev - t, x.kn);
                    if (r < 100.0)
                        g -= g / r;
                    g = std::max (0.0, std::min (g, x.rng));
                    return g > 0.0 ? share (g) : 0.0;
                };
                const auto e = at (levelDb, x.sThrE, x.sRatE);
                const auto f0 = v2Step (d.q2, e, x.f2, rate, d.k2);
                const auto f1 = v2Step (d.q3, e, x.f3, rate, d.k3);
                const auto down = std::clamp (std::max (s.g0 * f0, s.g1 * f1), 0.0, 1.0);
                const auto m = mag * (1.0 - down);
                const auto dd = at (down == 0.0 ? levelDb : m > 1e-10 ? std::max (-180.0, 20.0 * std::log10 (m)) : -180.0, x.sThrD, x.rat);
                const auto c0 = v2Step (d.q0, dd, x.f0, rate, d.k0);
                const auto c1 = v2Step (d.q1, dd, x.f1, rate, d.k1);
                const auto a = s.aBase + s.aSlope * (s.bGain * c0 * f0);
                gr = toDb (std::max (std::abs (a * c0), std::abs (c1)));
                break;
            }
            case 10: // round 190lp: Op-El (see OpElLaw)
            {
                const auto& o = opElLaw();
                const auto at = [&] (double lev, double k, double r) {
                    auto g = v2Knee (lev - x.thr, k);
                    if (x.rat < 100.0)
                        g -= g / x.rat;
                    return share (std::max (0.0, std::min (g, r)));
                };
                // D1 reads the shelved side chain (alt), D2 the side chain itself
                const auto lev1 = alt < 0.0 ? levelDb : alt > 1e-10 ? std::max (-180.0, 20.0 * std::log10 (alt)) : -180.0;
                const auto s1 = at (lev1, x.kn, x.rng), s2 = at (levelDb, o.d2Knee, o.d2Range);
                const auto a = v2Step (d.q0, s1 * s1, x.f0, rate, d.k0);
                const auto b = v2Step (d.q1, s1 * s1, x.f1, rate, d.k1);
                const auto cc = v2Step (d.q2, s2 * s2, x.f2, rate, d.k2);
                gr = toDb (std::sqrt (std::max (0.0, o.wA * a + o.wB * b + o.wC * cc)));
                break;
            }
            case 11: // round 190mb: Upward (see UpLaw): returns the lift (dB, + = up), no hold
            {
                const auto lev = v2Step (d.q1, mag, x.f1, rate, d.k1);
                const auto ld = lev > 1e-10 ? std::max (-180.0, 20.0 * std::log10 (lev)) : -180.0;
                const auto o = v2Step (d.q0, x.uSilence - upExpand (x.thr - ld, x.kn, x.uSlope, x.uLim), x.f0, rate, d.k0);
                auto lift = x.uSilence - o - x.uCut;
                // LYNX's own bound (issue #77, kept from the fitted law): a lift never takes the level (instant attack)
                // above the threshold, so a hit arriving in a lifted gap is not boosted past it. The reference lets the
                // first attack time of every such hit through up to 30 dB louder; the bound costs the bank's Upward files
                // 9.92 → 8.65 (round 190mb, owner decision). It does not act on a steady level (the lift stays under
                // 0.75 · (threshold − level) there)
                if (lift > 0.0 && x.uSafe)
                    lift = std::min (lift, std::max (0.0, x.thr - ld));
                d.sustain = x.slowCoef * d.sustain + (1.0 - x.slowCoef) * std::max (0.0, -lift);
                return lift;
            }
            case 12: // round 190mb: Vari-Mu (see VmuLaw): the feedback tube stage
            {
                const auto& v = vmuLaw();
                const auto e = vmuOver (d.q2, x.thr, x.vmThrLin, x.kn);
                double k = e > d.q0 ? x.vmAtt : x.vmRel;
                if (x.vmAuto && e <= d.q0)
                    k = 1.0 - std::exp (-2.302585092994046 * (std::sqrt (x.vmK * d.q0) + 1.0) / (x.vmAutoK * rate));
                d.q0 += k * (e - d.q0);
                const auto dl = std::max (mag * v.inGain, 1.0e-4);
                const auto g = std::abs (vmuGain (v, x.vmK * d.q0, dl));
                d.q2 = mag * g;
                gr = std::min (x.rng, g > 1e-10 ? -20.0 * std::log10 (g) : 200.0);
                break;
            }
            case 13: // round 190mj: TTM (see TtmLaw): one band of one channel; returns the band's gain (dB, + = up); its own hold
            {
                const auto b = (std::size_t) x.ttmB;
                auto lev = mag;
                if (x.ttmHoldN > 0)
                {
                    // the hold processor: the peak over the hold time, then a fall timed by the held level
                    if (lev >= d.tHeld)
                    {
                        d.tHeld = lev;
                        d.tHoldLeft = x.ttmHoldN;
                    }
                    else if (d.tHoldLeft > 0)
                        --d.tHoldLeft;
                    else
                        d.tHeld = lev;
                    d.tHold = d.tHeld >= d.tHold ? d.tHeld : d.tHold + (d.tHeld - d.tHold) * v1Coef (d.tHeld * x.ttmHoldRel * rate);
                    lev = d.tHold;
                }
                if (x.ttmLaN > 0) // the look-ahead: the peak over its time
                    lev = ttmLaMax[b][(std::size_t) c].push (lev, x.ttmLaN);
                // the follower on the power, read back in dB (√, then 20 log10, floor −180)
                const auto y = v2Step (d.q0, lev * lev, x.ttmF[b], rate, d.k0);
                const auto ld = y > 1.0e-18 ? std::max (-180.0, 10.0 * std::log10 (y)) : -180.0;
                auto g = ttmGain (x.ttm[b], ld, x.kn);
                if (x.ttmAvgN > 1) // the look-ahead smoothing: two moving averages
                    g = ttmAvg[b][(std::size_t) c][1].push (ttmAvg[b][(std::size_t) c][0].push (g, x.ttmAvgN), x.ttmAvgN);
                // issue #77's bounds on a lift (revision 0's two-way): it fades out under −80 dBFS (gone by −100: silence and
                // noise are never raised; the reference lifts them, the bank's TTM files 8.62 without the fade, 8.54 with it,
                // owner decision) and never takes the band's peak (instant attack) past the threshold, so a hit arriving into
                // a lifted gap is pulled down on its first sample. Revision 0's 30 dB ceiling is not kept: the upward
                // computer's own 0.6 × the range bounds the lift (36 dB at most)
                d.peak = levelDb > d.peak ? levelDb : x.aP * d.peak + (1.0 - x.aP) * levelDb;
                if (g > 0.0)
                {
                    const auto& tl = ttmLaw();
                    if (tl.liftFade > 0.0)
                        g *= std::clamp ((ld - tl.fadeFloor) / tl.fadeSpan, 0.0, 1.0);
                    g = std::min (g, tl.liftCap);
                    if (tl.liftSafe > 0.0)
                        g = std::min (g, std::max (0.0, x.thr - d.peak));
                }
                d.sustain = x.slowCoef * d.sustain + (1.0 - x.slowCoef) * std::max (0.0, -g);
                return g;
            }
            case 6: // Punch: one follower on the dB reduction
                gr = v2Step (d.q0, curve (levelDb), x.f0, rate, d.k0);
                break;
            default: // Pumping: two followers on the side chain's level itself, then the curve, then a fast follower
            {
                v2Step (d.q0, x0, x.f0, rate, d.k0);
                v2Step (d.q1, d.q0, x.f1, rate, d.k1);
                const auto m = std::abs (d.q1);
                gr = v2Step (d.q2, curve (m > 1e-10 ? std::max (-180.0, 20.0 * std::log10 (m)) : -180.0), x.f2, rate, d.k2);
                break;
            }
        }
        gr = std::max (gr, 0.0);
        if (x.holdSamples > 0)
        {
            if (gr >= d.held)
            {
                d.held = gr;
                d.holdLeft = x.holdSamples;
            }
            else if (d.holdLeft > 0)
            {
                --d.holdLeft;
                gr = d.held;
            }
            else
                d.held = gr;
        }
        d.sustain = x.slowCoef * d.sustain + (1.0 - x.slowCoef) * gr;
        return -gr;
    }

    double detect (int c, double level, const Ctx& x) noexcept
    {
        if (x.v1 != nullptr)
            return detectV1 (c, level, x);
        auto& d = detCur[c];
        if (x.window > 0) // the peak of the last `window` samples
        {
            auto& ring = peaks[(std::size_t) c];
            ring[(std::size_t) (d.peakPos % x.window)] = (float) level;
            d.peakPos = (d.peakPos + 1) % x.window;
            for (int k = 0; k < x.window; ++k)
                level = std::max (level, (double) ring[(std::size_t) k]);
        }
        if (x.curve == Curve::fet) // round 190ky: the FET law (feedback, three followers); no hold / window / blend
        {
            const auto u = level * d.fg;
            double cv = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                d.fe[k] = u > d.fe[k] ? d.fe[k] + x.fA[k] * (u - d.fe[k]) : d.fe[k] * x.fR[k];
                cv += x.fWt[k] * d.fe[k];
            }
            const auto g = fetLawDb (20.0 * std::log10 (cv + 1.0e-9), x.fT1, x.fW1, x.fS1, *x.fv, x.rng);
            d.fg = std::exp (g * nepersPerDb);
            return g;
        }
        if (x.curve == Curve::console) // round 190kt: the console law (feedback, RC ballistics); no hold / lift / blend
        {
            const auto t = consoleTarget (std::log (std::min (1.0, level * std::exp (d.cs)) + consoleFloor) - x.cT, x.cK, x.cKnee);
            if (! x.autoRel)
                d.cs = d.cs > t ? d.cs + x.cA * (t - d.cs) - x.cR * d.cs : std::min (d.cs * (1.0 - x.cR), t);
            else if (d.cs > t)
            {
                const auto e = t - d.c1 - d.c2;
                d.c1 += x.cA * e - x.cR * d.c1;
                d.c2 += x.cA2 * e - x.cR2 * d.c2;
                d.cs = d.c1 + d.c2;
            }
            else
            {
                d.c1 *= 1.0 - x.cR;
                d.c2 *= 1.0 - x.cR2;
                d.cs = std::min (d.c1 + d.c2, t);
            }
            return std::max (-x.rng, d.cs / nepersPerDb);
        }
        const auto in = std::max (-120.0, 20.0 * std::log10 (level + 1e-12));
        if (x.curve == Curve::twoWay) // round 190v: the follower works on the amplitude, not on its level in dB
        {
            if (level > d.lin)
            {
                d.lin = x.aA * d.lin + (1.0 - x.aA) * level;
                d.holdLeft = x.holdSamples;
            }
            else if (d.holdLeft > 0)
                --d.holdLeft;
            else
                d.lin = x.aRl * d.lin + (1.0 - x.aRl) * level;
            d.env = std::max (-120.0, 20.0 * std::log10 (d.lin + 1e-12));
        }
        else if (in > d.env)
        {
            d.env = x.aA * d.env + (1.0 - x.aA) * in;
            d.holdLeft = x.holdSamples;
        }
        else if (d.holdLeft > 0)
            --d.holdLeft;
        else
        {
            auto ms = x.relMs;
            if (x.autoRel) // program-dependent: short peaks recover faster than sustained reduction
            {
                const auto now = -styleGain (x, d.env);
                const auto fraction = now > 0.1 ? std::clamp (d.sustain / now, 0.0, 1.0) : 1.0;
                ms *= 0.35 + 0.65 * fraction;
            }
            const auto aR = coef (ms, x.rate);
            d.env = aR * d.env + (1.0 - aR) * in;
        }
        auto g = styleGain (x, d.env);
        if (x.gainBlend > 0.0) // the gain-domain detector: the instantaneous curve gain, smoothed
        {
            const auto target = styleGain (x, in);
            if (target < d.gain)
            {
                d.gain = x.aA * d.gain + (1.0 - x.aA) * target;
                d.gainHold = x.holdSamples;
            }
            else if (d.gainHold > 0)
                --d.gainHold;
            else
            {
                const auto aR = coef (x.relMs, x.rate);
                d.gain = aR * d.gain + (1.0 - aR) * target;
            }
            g += x.gainBlend * (d.gain - g);
        }
        d.peak = in > d.peak ? in : x.aP * d.peak + (1.0 - x.aP) * in;
        if (g > 0.0) // round 173d (issue #77): the upward lift of Upward / Two-way, kept safe
        {
            // the detector lags a hit by its attack, so a gap lifted by up to Range (60 dB) met the next drum hit at
            // full lift (+45 dBFS peaks on −18 dBFS drums). Three bounds: (1) the lift fades out below a −80 dBFS
            // floor (gone by −100 dBFS), so silence and noise are never raised; (2) it never exceeds maxLiftDb;
            // (3) it never takes the input's peak (instant attack) above the threshold, so a hit arriving into a
            // lifted gap is pulled down on its first sample
            constexpr double floorDb = -80.0, fadeDb = 20.0, maxLiftDb = 30.0;
            g *= std::clamp ((d.env - (floorDb - fadeDb)) / fadeDb, 0.0, 1.0);
            g = std::min ({ g, maxLiftDb, std::max (0.0, x.thr - d.peak) });
        }
        d.sustain = x.slowCoef * d.sustain + (1.0 - x.slowCoef) * std::max (0.0, -g);
        return g;
    }

    // the gains of a run of side-chain samples (per channel, or linked), through the look-ahead's gain delay
    // (band > 0: round 190al's low / high band of the two-way style, with their own detectors and gain delays)
    double gains (const float* a, const float* b, int count, double link, int lag, const Ctx& x, float* outL, float* outR,
                  int band = 0) noexcept
    {
        double maxGr = 0.0;
        const bool both = link >= 0.999;
        detCur = band == 0 ? det.data() : bandDet[(std::size_t) (band - 1)].data();
        const auto capacity = band == 0 ? gainCapacity : bandGainCapacity;
        auto& pos = band == 0 ? gainPos : bandGainPos[(std::size_t) (band - 1)];
        for (int i = 0; i < count; ++i)
        {
            const auto la = std::abs (a[i]), lb = std::abs (b[i]);
            const auto m = (double) std::max (la, lb);
            float g[2];
            // round 190lp: Op-El's D1 reads the side chain through its shelf (per channel, then linked); −1 = no shelf
            double altA = -1.0, altB = -1.0;
            if (x.oeShelf)
            {
                const auto shelf = [&] (int ch, double in) {
                    auto& z = oeZ[(std::size_t) ch];
                    const auto y = x.oeEq[0] * in + z[0];
                    z[0] = x.oeEq[1] * in - x.oeEq[3] * y + z[1];
                    z[1] = x.oeEq[2] * in - x.oeEq[4] * y;
                    return std::abs (y);
                };
                const auto fa = shelf (0, a[i]), fb = shelf (1, b[i]), fm = std::max (fa, fb);
                altA = link * fm + (1.0 - link) * fa;
                altB = link * fm + (1.0 - link) * fb;
            }
            // round 190kw: the V2 styles read the same linked magnitude (Pumping's first followers included: measured,
            // the signed sample scored lower on every block)
            const auto g0 = x.v2 > 0 ? detectV2 (0, link * m + (1.0 - link) * la, x, altA)
                                     : detect (0, link * m + (1.0 - link) * la, x);
            g[0] = (float) dbToGain (g0);
            maxGr = std::max (maxGr, -g0);
            if (both)
                g[1] = g[0];
            else
            {
                const auto g1 = x.v2 > 0 ? detectV2 (1, link * m + (1.0 - link) * lb, x, altB)
                                         : detect (1, link * m + (1.0 - link) * lb, x);
                g[1] = (float) dbToGain (g1);
                maxGr = std::max (maxGr, -g1);
            }
            for (int c = 0; c < 2; ++c)
            {
                auto* line = band == 0 ? gainDelay[(std::size_t) c].data() : bandGainDelay[(std::size_t) (band - 1)][(std::size_t) c].data();
                line[pos] = g[c];
                const auto rd = (pos - lag + capacity) % capacity;
                (c == 0 ? outL : outR)[i] = line[rd];
            }
            pos = (pos + 1) % capacity;
        }
        detCur = det.data();
        return maxGr;
    }

    void chunk (float* l, float* r, int n, const EffectParams& p, int offset) noexcept
    {
        const auto inPeak = blockPeak (l, r, n);
        const auto ch = characterOf (p[character]);
        const bool pre = on (p[characterPre]);
        const auto drv = (float) dbToGain (driveDb (p[drive]));
        const auto scGain = (float) dbToGain (scLevelDb (p[scLevel]));
        const bool external = on (p[scExternal]) && scL != nullptr && scR != nullptr;
        // round 137: the trigger replaces the detector's signal (a 0 dBFS key × the side-chain level)
        const auto trig = triggerOf (p[trigger]);
        float midiKey = 0.0f;
        if (trig == Trigger::midi)
            if (const auto c = triggerChannelOf (p[triggerChannel]); c >= 0 && held != nullptr && c < heldCount)
                midiKey = (held[c][0] | held[c][1]) != 0 ? 1.0f : 0.0f;
        const auto beat0 = songBeats + (double) offset * blockBpm / 60.0 / sr, beatStep = blockBpm / 60.0 / sr;
        const auto link = (double) std::clamp (p[stereoLink], 0.0f, 1.0f);
        const auto inGain = (float) dbToGain (trimDb (p[input]));
        const auto outGain = (float) dbToGain (trimDb (p[output]));
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        const auto st = styleOf (p);
        float makeup = 1.0f;
        if (on (p[autoGain]) && defOf (p).curve != Curve::upward)
        {
            const auto rng = std::max (0.01, rangeDb (p[range]));
            // round tp172b [EXEC] (1 kHz stairs through the reference, 32 settings): its auto gain makes up ~0.36 of the
            // plain downward curve's reduction at 0 dBFS (threshold, ratio, knee, range), the same for every downward
            // style, scaled down by a slow attack: 1 / (1 + (attack / 12 ms)^1.1) (+5.1 dB at 0.005 ms, +2.8 at 10 ms,
            // +0.9 at 50 ms, +0.0 at 250 ms with the defaults; release and look-ahead don't change it). It was half of
            // the style's own curve whatever the attack (+6.8 dB at the defaults, 2 dB louder)
            const double slowAttack = 1.0 / (1.0 + std::pow (attackMs (p[attack]) / 12.0, 1.1));
            // (the plain curve it was measured against: knee 6 dB + half the knob, no style offsets)
            const auto plain = std::min (rng, kneeCurve (-thresholdDb (p[threshold]), 1.0 - 1.0 / ratioOf (p[ratio]), 6.0 + 0.5 * kneeDb (p[knee])));
            // round 190v [EXEC]: two-way makes up more — tp172b's one two-way capture with auto gain on (threshold −18,
            // ratio 2, knee 24, attack 30 ms) sits 2.9 dB over the same setting without it, where 0.36 gives 0.9 dB
            // (never more than 3 dB over the other styles' make-up: one capture does not carry a deep setting, and the
            // make-up also lifts the pauses)
            const auto common = 0.36 * slowAttack * plain;
            const auto made = defOf (p).curve == Curve::twoWay ? std::min (1.19 * slowAttack * plain, common + 3.0) : common;
            makeup = (float) dbToGain (std::min (rng, made));
        }
        const auto mode = linkModeOf (p[linkMode]);
        const bool ms = mode != LinkMode::stereo;
        const auto dryG = (float) (p[dryGain] > 0.0f ? dbToGain (dryDb (p[dryGain])) : 0.0);
        const bool autoThr = on (p[autoThreshold]);
        const auto la = std::min (lookaheadMs (p[lookahead]), latencyMs);
        const auto factor = osFactor > 1 ? osFactor : 1;
        const auto lag = std::clamp ((int) std::lround ((latencyMs - la) * 0.001 * sr * factor), 0, gainCapacity - 1);

        // the side chain (base rate) and the audio through the look-ahead delay
        for (int i = 0; i < n; ++i)
        {
            float a = l[i] * inGain, b = r[i] * inGain;
            float sa, sb;
            if (external)
            {
                sa = scL[offset + i] * inGain;
                sb = scR[offset + i] * inGain;
            }
            else if (pre && ch != Character::off)
            {
                sa = characterCurve (ch, a * drv) / drv;
                sb = characterCurve (ch, b * drv) / drv;
            }
            else
            {
                sa = a;
                sb = b;
            }
            sa *= scGain;
            sb *= scGain;
            if (ms)
            {
                const auto m = 0.5f * (sa + sb), d = 0.5f * (sa - sb);
                sa = m;
                sb = d;
            }
            if (scEqOn)
                for (int q = 0; q < scBands; ++q)
                    for (int k = 0; k < scCount[(std::size_t) q]; ++k)
                    {
                        auto& f = scEq[(std::size_t) q][(std::size_t) k];
                        sa = f.process (sa, 0);
                        sb = f.process (sb, 1);
                    }
            if (trig != Trigger::off)
                sa = sb = scGain * (trig == Trigger::midi ? midiKey : (transportPlaying ? hostTrigger (p, beat0 + beatStep * (double) i) : 0.0f));
            sideL[(std::size_t) i] = sa;
            sideR[(std::size_t) i] = sb;
            if (autoThr)
                programSum += 0.5 * ((double) sa * sa + (double) sb * sb);
            dl[(std::size_t) i] = a;
            dr[(std::size_t) i] = b;
            if (ms)
            {
                const auto m = 0.5f * (a + b), d = 0.5f * (a - b);
                a = m;
                b = d;
            }
            audioDelay.process (a, b);
            wl[(std::size_t) i] = a;
            wr[(std::size_t) i] = b;
        }

        if (on (p[scListen])) // the detector's signal
        {
            for (int i = 0; i < n; ++i)
                l[i] = r[i] = 0.5f * (sideL[(std::size_t) i] + sideR[(std::size_t) i]);
            meters.set (0, 0.0f);
            meters.set (2, inPeak);
            meters.set (3, blockPeak (l, r, n));
            return;
        }

        if (autoThr) // the programme's slow RMS level (3 s), block by block
        {
            const auto ms2 = programSum / std::max (1, n);
            programSum = 0.0;
            const auto k = std::exp (-(double) n / (3.0 * sr));
            programMs = k * programMs + (1.0 - k) * ms2;
            programDb = std::max (-120.0, 10.0 * std::log10 (programMs + 1e-15));
        }
        const auto detLink = ms ? 0.0 : link; // mid / side: each detector on its own channel
        double maxGr = 0.0;
        if (defOf (p).curve == Curve::twoWay)
        {
            // round 190al: the measured two-way law works in three bands (at the base rate; with oversampling on, the
            // summed bands go through the oversampler for the character stage and the same latency)
            auto x = context (p, sr);
            const auto att = attackMs (p[attack]);
            const auto bandLag = std::clamp ((int) std::lround ((latencyMs - la) * 0.001 * sr), 0, bandGainCapacity - 1);
            // (the detector's signal is the audio itself unless a side-chain option or the look-ahead sets them apart:
            // then one split serves both)
            const bool own = ! std::equal (sideL.begin(), sideL.begin() + n, wl.begin()) || ! std::equal (sideR.begin(), sideR.begin() + n, wr.begin());
            const bool ttm = x.v2 == 13; // round 190mj: TTM's own split
            if (own)
                (ttm ? splitSide1 : splitSide).process (sideL.data(), sideR.data(), n, bandSide);
            (ttm ? splitAudio1 : splitAudio).process (wl.data(), wr.data(), n, bandAudio);
            const auto& side = own ? bandSide : bandAudio;
            if (ttm && factor > 1 && ! ttmUp.empty() && clab ("tOs", 1.0) > 0.0)
            {
                // round 190mj: TTM oversampled — the reference runs its graph at the oversampled rate: the audio is split
                // there (the split's phase and shares at 8 kHz move by up to 0.06 rad / 0.2 dB between 48 and 96 kHz), the
                // band gains (at the base rate) are brought there through the audio's own up path, and the sum through the
                // stage comes back down
                for (int b = 0; b < twoWayBandCount; ++b)
                {
                    x.twoWay = twoWayLaw (x.thr, x.rat, x.kn, att, b);
                    x.ttmB = b;
                    auto& g = ttmBandGain[(std::size_t) b];
                    maxGr = std::max (maxGr, gains (side[(std::size_t) b][0].data(), side[(std::size_t) b][1].data(), n, detLink, bandLag, x,
                                                    g[0].data(), g[1].data(), b));
                    if (ms)
                        routeMs (mode, g[0].data(), g[1].data(), n);
                }
                const int m = n * factor, stride = maxBlock * maxFactor;
                for (int b = 0; b < twoWayBandCount; ++b)
                    for (int c = 0; c < 2; ++c)
                    {
                        auto* dst = ttmUp.data() + (std::size_t) ((b * 2 + c) * stride);
                        int k = 0;
                        ttmOsGain[(std::size_t) (b * 2 + c)].process (ttmBandGain[(std::size_t) b][(std::size_t) c].data(), n, [&] (float v) noexcept {
                            dst[std::min (k++, m - 1)] = v;
                            return 0.0f;
                        });
                    }
                for (int c = 0; c < 2; ++c)
                {
                    const auto* g0 = ttmUp.data() + (std::size_t) (c * stride);
                    const auto* g1 = ttmUp.data() + (std::size_t) ((2 + c) * stride);
                    const auto* g2 = ttmUp.data() + (std::size_t) ((4 + c) * stride);
                    int k = 0;
                    os[(std::size_t) c].process (c == 0 ? wl.data() : wr.data(), n, [&] (float v) noexcept {
                        float band[3];
                        splitAudioUp.split1 (v, c, band);
                        const auto i = std::min (k++, m - 1);
                        return stage (band[0] * g0[i] + band[1] * g1[i] + band[2] * g2[i], makeup, ch, pre, drv);
                    });
                }
            }
            else
            {
                std::fill (wl.begin(), wl.begin() + n, 0.0f);
                std::fill (wr.begin(), wr.begin() + n, 0.0f);
                for (int b = 0; b < twoWayBandCount; ++b)
                {
                    x.twoWay = twoWayLaw (x.thr, x.rat, x.kn, att, b);
                    x.ttmB = b; // round 190mj
                    maxGr = std::max (maxGr, gains (side[(std::size_t) b][0].data(), side[(std::size_t) b][1].data(), n, detLink, bandLag, x,
                                                    gl.data(), gr.data(), b));
                    if (ms)
                        routeMs (mode, gl.data(), gr.data(), n);
                    for (int i = 0; i < n; ++i)
                    {
                        wl[(std::size_t) i] += bandAudio[(std::size_t) b][0][(std::size_t) i] * gl[(std::size_t) i];
                        wr[(std::size_t) i] += bandAudio[(std::size_t) b][1][(std::size_t) i] * gr[(std::size_t) i];
                    }
                }
                if (factor == 1)
                    for (int i = 0; i < n; ++i)
                    {
                        wl[(std::size_t) i] = stage (wl[(std::size_t) i], makeup, ch, pre, drv);
                        wr[(std::size_t) i] = stage (wr[(std::size_t) i], makeup, ch, pre, drv);
                    }
                else
                    for (int c = 0; c < 2; ++c)
                        os[(std::size_t) c].process (c == 0 ? wl.data() : wr.data(), n, [&] (float v) noexcept {
                            return stage (v, makeup, ch, pre, drv);
                        });
            }
        }
        else if (factor == 1)
        {
            maxGr = gains (sideL.data(), sideR.data(), n, detLink, lag, context (p, sr), gl.data(), gr.data());
            if (ms)
                routeMs (mode, gl.data(), gr.data(), n);
            for (int i = 0; i < n; ++i)
            {
                wl[(std::size_t) i] = stage (wl[(std::size_t) i], gl[(std::size_t) i] * makeup, ch, pre, drv);
                wr[(std::size_t) i] = stage (wr[(std::size_t) i], gr[(std::size_t) i] * makeup, ch, pre, drv);
            }
        }
        else
        {
            // oversampled: the side chain is upsampled (the same path as the audio, so both stay aligned), the
            // detector and the gain run at the high rate, and the gain multiplies the upsampled audio
            const auto m = n * factor;
            for (int c = 0; c < 2; ++c)
            {
                auto* src = c == 0 ? sideL.data() : sideR.data();
                auto* dst = c == 0 ? upL.data() : upR.data();
                int k = 0;
                osSide[(std::size_t) c].process (src, n, [&] (float v) noexcept {
                    dst[std::min (k++, m - 1)] = v;
                    return 0.0f;
                });
            }
            maxGr = gains (upL.data(), upR.data(), m, detLink, lag, context (p, sr * factor), upGl.data(), upGr.data());
            if (ms)
                routeMs (mode, upGl.data(), upGr.data(), m);
            for (int c = 0; c < 2; ++c)
            {
                const auto* g = c == 0 ? upGl.data() : upGr.data();
                int k = 0;
                os[(std::size_t) c].process (c == 0 ? wl.data() : wr.data(), n, [&] (float v) noexcept {
                    return stage (v, g[std::min (k++, m - 1)] * makeup, ch, pre, drv);
                });
            }
        }

        float wetA = 1.0f, wetB = 1.0f, dryA = 1.0f, dryB = 1.0f;
        {
            const auto balance = [] (double pan, float& a, float& b) {
                if (pan > 0.0) a = (float) (1.0 - pan);
                else if (pan < 0.0) b = (float) (1.0 + pan);
            };
            balance (panOf (p[wetPan]), wetA, wetB);
            balance (panOf (p[dryPan]), dryA, dryB);
        }
        const bool panned = wetA != 1.0f || wetB != 1.0f || dryA != 1.0f || dryB != 1.0f;
        const bool fetToneOn = fetVoiceOf (p) >= 0;
        const bool ampOn = fetToneOn && p[fetAmp] > 0.0f;
        const double ampK = ampOn ? std::pow (10.0, fetAmpDb (p[fetAmp]) / 20.0) : 1.0, ampLead = fetAmpLead * sr / 48000.0;
        for (int i = 0; i < n; ++i)
        {
            float a = wl[(std::size_t) i], b = wr[(std::size_t) i];
            if (ampOn) // round 190mg: the output amplifier at its reference level
            {
                const auto amp = [&] (float v, int c) noexcept {
                    const double s = (double) v * ampK, q = fetAmpA * s * s + fetAmpB * s * std::abs (s);
                    const double z = s + q + ampLead * (q - ampPrev[c]);
                    ampPrev[c] = q;
                    return (float) (z / ampK);
                };
                a = amp (a, 0);
                b = amp (b, 1);
            }
            if (fetToneOn) // round 190ky: the FET law's static tone, on the compressed path only
            {
                a = fetTone.process (a, 0);
                b = fetTone.process (b, 1);
            }
            if (ms)
            {
                const auto m = a, d = b;
                a = m + d;
                b = m - d;
            }
            if (ch != Character::off)
            {
                a = dc.process (a, 0);
                b = dc.process (b, 1);
            }
            float da = dl[(std::size_t) i], db = dr[(std::size_t) i];
            dryLa.process (da, db);
            dryDelay.process (da, db);
            if (panned) // round 135: the wet and dry paths balanced on their own
            {
                l[i] = (a * mx * wetA + da * ((1.0f - mx) + dryG) * dryA) * outGain;
                r[i] = (b * mx * wetB + db * ((1.0f - mx) + dryG) * dryB) * outGain;
            }
            else if (dryG > 0.0f) // round 128: the dry level added on top (parallel compression)
            {
                l[i] = (da + (a - da) * mx + da * dryG) * outGain;
                r[i] = (db + (b - db) * mx + db * dryG) * outGain;
            }
            else
            {
                l[i] = (da + (a - da) * mx) * outGain;
                r[i] = (db + (b - db) * mx) * outGain;
            }
        }
        meters.set (0, (float) std::clamp (maxGr / 30.0, 0.0, 1.0));
        meters.set (2, inPeak);
        meters.set (3, blockPeak (l, r, n));
    }

    // mid / side link modes: the gains computed per channel (0 = mid, 1 = side) → where they apply
    static void routeMs (LinkMode mode, float* gm, float* gs, int count) noexcept
    {
        for (int i = 0; i < count; ++i)
            switch (mode)
            {
                case LinkMode::mid: gs[i] = 1.0f; break;
                case LinkMode::side: gm[i] = 1.0f; break;
                case LinkMode::midToSide: gs[i] = gm[i]; gm[i] = 1.0f; break;
                case LinkMode::sideToMid: gm[i] = gs[i]; gs[i] = 1.0f; break;
                case LinkMode::stereo: break;
            }
    }

    static float stage (float x, float g, Character ch, bool pre, float drv) noexcept
    {
        if (ch == Character::off)
            return x * g;
        if (pre)
            return characterCurve (ch, x * drv) / drv * g;
        return characterCurve (ch, x * g * drv) / drv;
    }

    double sr = 48000.0, latencyMs = -1.0;
    FetTone fetTone; // round 190ky: the FET law's wet-path tone
    double ampPrev[2] {}; // round 190mg: the output amplifier's previous non-linear term
    double programSum = 0.0, programMs = 1e-4, programDb = -40.0; // auto threshold: the programme's slow RMS
    int osFactor = 0, laSamples = 0;
    std::atomic<int> latency { 0 };
    EffectParams lastParams = [] {
        EffectParams p {};
        const auto& info = crestInfo();
        for (std::size_t i = 0; i < info.params.size(); ++i)
            p[i] = info.params[i].defaultValue;
        return p;
    }();
    StereoDelay audioDelay, dryLa, dryDelay;
    std::array<std::vector<float>, 2> gainDelay;
    int gainPos = 0;
    std::array<mixsuite::Oversampler, 2> os, osSide;
    std::array<std::array<Biquad, facet::maxSections>, scBands> scEq;
    std::array<int, scBands> scCount {};
    bool scEqOn = false;
    std::array<Detector, 2> det {};
    std::array<SlidingMax, 2> holdMax; // round 190kw: Mastering's 10 ms peak hold (sized in prepare for up to 8x)
    std::array<std::array<float, peakWindow>, 2> peaks {};
    // round 190al: the two-way style's low and high bands (the mid band uses the detectors and gain delay above)
    static constexpr int bandGainCapacity = 8192; // the look-ahead at the base rate (20 ms at 192 kHz), with room
    Detector* detCur = det.data();
    std::array<std::array<Detector, 2>, 2> bandDet {};
    std::array<std::array<double, 2>, 2> oeZ {}; // round 190lp: Op-El's shelf state per side-chain channel
    // round 190mj: TTM's look-ahead hold and smoothing per band and channel (sized in prepare)
    std::array<std::array<SlidingMax, 2>, 3> ttmLaMax;
    std::array<std::array<std::array<Boxcar, 2>, 2>, 3> ttmAvg;
    std::array<std::array<std::array<float, bandGainCapacity>, 2>, 2> bandGainDelay {};
    std::array<int, 2> bandGainPos {};
    TwoWaySplit splitAudio, splitSide, splitAudio1, splitSide1; // (1: round 190mj's TTM, revision 1)
    // round 190mj: TTM oversampled — the audio's split at the oversampled rate, each band's gain brought there through the
    // audio's own up path (so they stay aligned), the band gains at the base rate and at the oversampled rate
    TwoWaySplit splitAudioUp;
    std::array<mixsuite::Oversampler, 6> ttmOsGain;
    std::array<std::array<std::array<float, maxBlock>, 2>, 3> ttmBandGain {};
    std::vector<float> ttmUp;
    std::array<std::array<std::array<float, maxBlock>, 2>, twoWayBandCount> bandSide {}, bandAudio {};
    DcBlock dc;
    const float* scL = nullptr;
    const float* scR = nullptr;
    // round 137: the trigger's clock (quarter notes at the block start) and the held notes
    double songBeats = 0.0, blockBpm = 120.0;
    bool transportPlaying = false;
    const NoteMask* held = nullptr;
    int heldCount = 0;
    std::array<float, maxBlock> wl {}, wr {}, gl {}, gr {}, dl {}, dr {}, sideL {}, sideR {};
    std::array<float, maxBlock * maxFactor> upL {}, upR {}, upGl {}, upGr {};
    LiveMeters meters;
};
} // namespace
} // namespace crest

// ================================================================== Prism Multiband

namespace prism
{
double thresholdDb (float v) noexcept { return -90.0 * (1.0 - std::sqrt ((double) std::clamp (v, 0.0f, 1.0f))); }
float thresholdValue (double db) noexcept
{
    const auto u = 1.0 - std::clamp (-db / 90.0, 0.0, 1.0);
    return (float) (u * u);
}
double ratioOf (float v) noexcept { return ratioLaw (v); }
bool isInfiniteRatio (float v) noexcept { return v >= 1.0f; }
int auditionBand (const EffectParams& p) noexcept { return (int) std::lround (std::clamp (p[audition], 0.0f, 1.0f) * bands) - 1; }
float levelMeterValue (double db) noexcept { return (float) std::clamp ((db + 120.0) / 120.0, 0.0, 1.0); }
double levelMeterDb (float v) noexcept { return (double) v * 120.0 - 120.0; }
double rangeDb (float v) noexcept { return lin (v, -30.0, 30.0); }
double kneeDb (float v) noexcept { return lin (v, 0.0, 48.0); }
double attackMs (float v) noexcept { return 0.11 + 89.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 3.9); }
double releaseMs (float v) noexcept { return 130.0 + 3500.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 3.0); }
double levelDb (float v) noexcept { return lin (v, -30.0, 30.0); }
double crossoverHz (float v) noexcept { return logMap (v, 30.0, 18000.0); }
float crossoverValue (double hz) noexcept { return logValue (std::clamp (hz, 30.0, 18000.0), 30.0, 18000.0); }
int slopeDbOct (float v) noexcept
{
    static constexpr int s[] { 12, 24, 48 };
    return s[pick (v, 3)];
}
double lookaheadMs (float v) noexcept { return lin (v, 0.0, 20.0); }
double trimDb (float v) noexcept { return lin (v, -36.0, 36.0); }
Phase phaseOf (float v) noexcept
{
    const auto k = pick (v, 3);
    return k == 0 ? Phase::linear : (k == 1 ? Phase::dynamic : Phase::minimum);
}
bool measuredDynamic (const EffectParams& p) noexcept { return on (p[dynamicEngine]); }
double linkAmount (float v) noexcept { return std::clamp (2.0 * (double) v, 0.0, 1.0); }
double linkOnly (float v) noexcept { return std::clamp (2.0 * (double) v - 1.0, 0.0, 1.0); }
double scFilterHz (float v) noexcept { return 30.0 * std::pow (1000.0, (double) std::clamp (v, 0.0f, 1.0f)); }
int crossoverSlopeDbOct (const EffectParams& p, int k) noexcept
{
    static constexpr int own[4] { 0, 12, 24, 48 };
    const auto s = own[pick (p[(std::size_t) (xoverSlope0 + std::clamp (k, 0, bands - 2))], 4)];
    return s > 0 ? s : slopeDbOct (p[Global::slope]);
}
float crossoverSlopeValue (int dbOct) noexcept { return dbOct <= 0 ? 0.0f : (dbOct <= 12 ? 1.0f / 3.0f : (dbOct <= 24 ? 2.0f / 3.0f : 1.0f)); }
float scFilterValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 1.0) / 30.0) / std::log (1000.0), 0.0, 1.0); }
int oversamplingOf (float v) noexcept { return 1 << pick (v, 3); }
int linearFftSize (double sampleRate) noexcept
{
    int n = 4096;
    for (double r = 50000.0; sampleRate > r && n < 16384; r *= 2.0)
        n *= 2;
    return n;
}
int linearLatency (double sampleRate) noexcept { return 3 * linearFftSize (sampleRate) / 4; }


// round 150 (REA_rodada150 §1–3): the free layout, Pro-MB's edge / slope laws, the detector law
bool freeLayout (const EffectParams& p) noexcept { return on (p[bandLayout]); }
double edgeHz (float v) noexcept { return 30.0 * std::pow (1000.0, (double) std::clamp (v, 0.0f, 1.0f)); }
float edgeValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 1.0) / 30.0) / std::log (1000.0), 0.0, 1.0); }
double slopeOfValue (float v) noexcept
{
    const auto x = (double) std::clamp (v, 0.0f, 1.0f);
    return x < 0.25 ? 6.0 + 24.0 * x : 48.0 * x;
}
float slopeValue (double dbOct) noexcept
{
    const auto d = std::clamp (dbOct, 6.0, 48.0);
    return (float) (d < 12.0 ? (d - 6.0) / 24.0 : d / 48.0);
}
int iirSlope (double dbOct) noexcept
{
    static constexpr int s[5] { 6, 12, 24, 36, 48 };
    const auto code = minphase::dynamicSideCode (dbOct);
    return s[code == 1 ? 0 : code / 2];
}
bool measuredDetector (const EffectParams& p) noexcept { return on (p[detectorLaw]); }
// round 190mg: the C4 law's hidden values (1 / 8 electro, 2 / 8 opto; 0 = Prism's own engine)
float c4LawValue (bool opto) noexcept { return opto ? 0.25f : 0.125f; }
bool c4Active (const EffectParams& p) noexcept { return std::lround (std::clamp (p[c4Law], 0.0f, 1.0f) * 8.0f) >= 1; }
bool c4Opto (const EffectParams& p) noexcept { return std::lround (std::clamp (p[c4Law], 0.0f, 1.0f) * 8.0f) == 2; }
float c4QValue (double q) noexcept { return (float) std::clamp ((q - 0.1) / 0.65, 0.0, 1.0); }
double c4QOf (const EffectParams& p) noexcept { return p[c4Q] <= 0.0f ? 0.6 : 0.1 + 0.65 * (double) std::clamp (p[c4Q], 0.0f, 1.0f); }
float c6FreqValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 16.0) / 16.0) / std::log (21357.0 / 16.0), 0.0, 1.0); }
float c6WidthValue (double q) noexcept { return (float) std::clamp (std::log (std::max (q, 0.35) / 0.35) / std::log (60.0 / 0.35), 0.0, 1.0); }
float c4AttackValue (double ms) noexcept { return (float) std::clamp (std::log (std::max (ms, 0.5) / 0.5) / std::log (1000.0), 0.0, 1.0); }
float c4ReleaseValue (double ms) noexcept { return (float) std::clamp (std::log (std::max (ms, 5.0) / 5.0) / std::log (1000.0), 0.0, 1.0); }
int c4Lookahead (double sampleRate) noexcept { return sampleRate <= 50000.0 ? 64 : (sampleRate <= 100000.0 ? 128 : 256); }

Layout layoutOf (const EffectParams& p) noexcept
{
    Layout L;
    L.segmentOf.fill (-1);
    const auto nb = bandCount (p);
    if (! freeLayout (p))
    {
        // the split layout: band b between crossovers b − 1 and b (the old engine)
        const auto f = crossovers (p);
        L.segments = nb;
        for (int k = 0; k < nb - 1; ++k)
        {
            L.xoverHz[(std::size_t) k] = f[(std::size_t) k];
            L.xoverSlope[(std::size_t) k] = crossoverSlopeDbOct (p, k);
        }
        for (int b = 0; b < nb; ++b)
        {
            L.bandOf[(std::size_t) b] = b;
            L.segmentOf[(std::size_t) b] = b;
            L.loHz[(std::size_t) b] = b > 0 ? f[(std::size_t) (b - 1)] : 0.0;
            L.hiHz[(std::size_t) b] = b < nb - 1 ? f[(std::size_t) b] : 0.0;
            L.loSlope[(std::size_t) b] = b > 0 ? L.xoverSlope[(std::size_t) (b - 1)] : 24.0;
            L.hiSlope[(std::size_t) b] = b < nb - 1 ? L.xoverSlope[(std::size_t) b] : 24.0;
        }
        return L;
    }
    // the free layout: the bands in order of their low edge, no overlap (a band starts at least where the one below
    // ends), the room between them passes untouched; an edge at 30 Hz (30 kHz) or beyond is no edge
    std::array<int, bands> order {};
    for (int b = 0; b < nb; ++b)
        order[(std::size_t) b] = b;
    std::stable_sort (order.begin(), order.begin() + nb, [&] (int a, int c) {
        return edgeHz (p[(std::size_t) (bandLo0 + a)]) < edgeHz (p[(std::size_t) (bandLo0 + c)]);
    });
    int seg = 0, nx = 0;
    double cursor = 0.0; // the upper edge of what is laid out (0 = nothing yet)
    bool open = false;   // a band reached 30 kHz: nothing fits above it
    for (int k = 0; k < nb && ! open; ++k)
    {
        const auto b = order[(std::size_t) k];
        auto lo = edgeHz (p[(std::size_t) (bandLo0 + b)]);
        auto hi = edgeHz (p[(std::size_t) (bandHi0 + b)]);
        const auto ls = slopeOfValue (p[(std::size_t) (bandLoSlope0 + b)]), hs = slopeOfValue (p[(std::size_t) (bandHiSlope0 + b)]);
        lo = lo <= 30.5 ? 0.0 : lo;
        if (cursor > 0.0)
            lo = std::max (lo, cursor);
        const bool hasHi = hi < 29500.0;
        if (hasHi && lo > 0.0) // round 152 [EXEC]: Pro-MB keeps a band at least 0.8 octave wide (its upper edge moves)
            hi = std::max (hi, lo * (on (p[rev152]) ? 1.7411011265922482 : 1.05));
        if (lo > 0.0 && (seg == 0 || lo > cursor * 1.0001))
        {
            L.bandOf[(std::size_t) seg++] = -1; // the gap below the band
            L.xoverHz[(std::size_t) nx] = lo;
            L.xoverSlope[(std::size_t) nx++] = ls;
        }
        L.segmentOf[(std::size_t) b] = seg;
        L.bandOf[(std::size_t) seg++] = b;
        L.loHz[(std::size_t) b] = lo;
        L.loSlope[(std::size_t) b] = ls;
        L.hiSlope[(std::size_t) b] = hs;
        if (hasHi)
        {
            L.hiHz[(std::size_t) b] = hi;
            L.xoverHz[(std::size_t) nx] = hi;
            L.xoverSlope[(std::size_t) nx++] = hs;
            cursor = hi;
        }
        else
            open = true;
    }
    if (! open)
        L.bandOf[(std::size_t) seg++] = -1; // the room above the last band
    L.segments = std::max (1, seg);
    return L;
}

namespace
{
// round 150 [EXEC] Pro-MB's range has a soft knee of 12 dB around it (a 9 dB change under a 12 dB range gives 8.61 dB,
// 9 under 6 gives 5.62): x − (x − R + W/2)² / (2W) inside R ± W/2 (W = 12, at most 2R)
double softRange (double x, double range) noexcept
{
    const auto w = std::min (12.0, 2.0 * range);
    if (w <= 0.0)
        return 0.0;
    if (x <= range - 0.5 * w)
        return x;
    if (x >= range + 0.5 * w)
        return range;
    const auto d = x - range + 0.5 * w;
    return x - d * d / (2.0 * w);
}

// gain change (dB) of a band for a detector level x (dB); round 150: `soft` = the measured detector's range knee
double bandCurve (const EffectParams& p, int b, double x, bool soft = false) noexcept
{
    const auto at = [&] (int f) { return p[(std::size_t) param (b, f)]; };
    const auto thr = thresholdDb (at (fThreshold));
    const auto r = ratioOf (at (fRatio));
    const auto rng = rangeDb (at (fRange));
    const auto kn = kneeDb (at (fKnee));
    if (std::abs (rng) < 0.01)
        return 0.0;
    const bool expand = on (at (fMode));
    const auto limit = [&] (double v) { return soft ? softRange (v, std::abs (rng)) : std::min (std::abs (rng), v); };
    // round tp172b6 [EXEC] (Pro-MB, 1 kHz stair, threshold -30, knee 0: -6.01 / -12.01 / -18.01 / -24.01 dB at 6 ... 24 dB
    // over): the very top of the ratio is a true limit (slope 1, printed infinity : 1), whatever its parameter text
    // says; 0.99 is still 91.9 : 1 (-5.95 dB). Only the exact top changes here; every value below keeps its law. The
    // downward expander's top stays as it was: 54.8 : 1 and the top differ by under 0.1 dB on the same stair.
    const auto squeeze = isInfiniteRatio (at (fRatio)) ? 1.0 : 1.0 - 1.0 / r;
    if (! expand)
    {
        if (rng < 0.0) // downward compression above the threshold
            return -limit (kneeCurve (x - thr, squeeze, kn));
        return limit (kneeCurve (thr - x, squeeze, kn)); // upward compression below it
    }
    if (rng < 0.0) // downward expansion below the threshold
        return -limit (kneeCurve (thr - x, r - 1.0, kn));
    return limit (kneeCurve (x - thr, squeeze, kn)); // upward expansion above it
}

// round 150 [EXEC] the measured detector's time constants (fitted to Pro-MB's gain on 1 kHz steps −30 → −6 → −30 →
// −18 → −30 dBFS, threshold −30, REA_rodada150 §3): the level follower's attack and release (dB domain) and the gain's
// own release after the curve (so a range-limited band recovers at once); table over 0 / 10 / 20 / 35 / 50 / 75 / 100 %
double measuredLaw (const double* table, float v) noexcept
{
    static constexpr double at[7] { 0.0, 0.1, 0.2, 0.35, 0.5, 0.75, 1.0 };
    const auto x = (double) std::clamp (v, 0.0f, 1.0f);
    int k = 0;
    while (k < 5 && x > at[k + 1])
        ++k;
    const auto t = (x - at[k]) / (at[k + 1] - at[k]);
    return std::exp (std::log (table[k]) + (std::log (table[k + 1]) - std::log (table[k])) * t);
}
constexpr double measuredAttack[7] { 0.065, 0.081, 0.166, 1.22, 8.35, 35.2, 99.4 };
// round 152 [EXEC] (REA_rodada152 §1, the same renders refitted): the reference's attack lives in the gain stage — a
// fast level follower (τ below), then the growing gain change smoothed with its own τ (`measuredGainAttack`) in the
// domain 10^(k·|gain| / 20) (k = `measuredGainAttackK`; 0 = dB). Worst error over the first 400 ms after a step:
// 0.03 – 0.41 dB at every attack 0 … 100 % (the level-follower attack of round 150: up to 3 dB from 50 %)
constexpr double measuredAttackLevel[7] { 0.012, 0.012, 0.022, 0.092, 0.418, 0.539, 1.302 };
constexpr double measuredGainAttack[7] { 0.134, 0.166, 0.695, 6.95, 30.3, 115.6, 271.3 };
constexpr double measuredGainAttackK[7] { 0.0, 0.0, 0.0, 0.66, 0.84, 0.36, 0.0 };
double measuredLinear (const double* table, float v) noexcept
{
    static constexpr double at[7] { 0.0, 0.1, 0.2, 0.35, 0.5, 0.75, 1.0 };
    const auto x = (double) std::clamp (v, 0.0f, 1.0f);
    int k = 0;
    while (k < 5 && x > at[k + 1])
        ++k;
    return table[k] + (table[k + 1] - table[k]) * (x - at[k]) / (at[k + 1] - at[k]);
}
constexpr double measuredRelease1[7] { 8.5, 9.2, 56.0, 123.0, 172.0, 298.0, 905.0 };
constexpr double measuredRelease2[7] { 103.0, 104.0, 125.0, 176.0, 243.0, 681.0, 2449.0 };

// round 150 [EXEC] Pro-MB's linear-phase band shape for any slope: |high pass| = x^ml / (1 + x^ml) at the low edge,
// |low pass| = 1 / (1 + x^mh) at the high edge, m = slope / 6 (continuous; every slope 6 … 48 matched within 0.01 dB);
// x on LYNX's prewarped axis (the reference's analog axis differs by ≤ 0.35 dB at a 3 kHz crossover)
double linearShare (double wl, double wh, double ml, double mh, double w) noexcept
{
    double a = 1.0;
    if (wh > 0.0)
        a /= 1.0 + std::pow (w / wh, mh);
    if (wl > 0.0)
    {
        const auto x = std::pow (w / wl, ml);
        a *= x / (1.0 + x);
    }
    return a;
}

// round tp172b7 [EXEC] Pro-MB's Linear phase puts that shape on the analog axis x = f / fc, not the prewarped one: a
// −12 dB band from 500 Hz to 8 kHz (24 dB/oct, 48 kHz) leaves 12 kHz at −1.2 dB and 16 kHz at −0.4 dB (analog: −1.14 /
// −0.39; prewarped: −0.67 / −0.08); Minimum and Dynamic phase follow the prewarped curve within 0.1 dB (probe p18b).
// analogBin: the frequency (radians / sample) where a bilinear crossover at fcHz shows its analog prototype's value at
// w / wc, so the same LrSplit gives the analog magnitude of any slope.
double analogBin (double w, double fcHz, double sr) noexcept
{
    const auto wc = 2.0 * pi * std::min (fcHz, sr * 0.45) / sr;
    return 2.0 * std::atan (w / wc * std::tan (0.5 * wc));
}

using Split = LrSplit;

} // namespace

int bandCount (const EffectParams& p) noexcept { return 1 + pick (p[bandCountParam], bands); }
float bandCountValue (int n) noexcept { return (float) (std::clamp (n, 1, bands) - 1) / (float) (bands - 1); }
double panOf (float v) noexcept { return std::clamp ((double) v * 2.0 - 1.0, -1.0, 1.0); }

std::array<double, bands - 1> crossovers (const EffectParams& p) noexcept
{
    std::array<double, bands - 1> f {};
    for (int k = 0; k < bands - 1; ++k)
    {
        f[(std::size_t) k] = crossoverHz (p[(std::size_t) crossoverParam (k)]);
        if (k > 0)
            f[(std::size_t) k] = std::max (f[(std::size_t) k], f[(std::size_t) (k - 1)] * 1.05);
    }
    return f;
}

double bandStaticDb (const EffectParams& p, int b, double inputDb)
{
    if (! on (p[(std::size_t) param (b, fEnabled)]))
        return 0.0;
    return bandCurve (p, b, inputDb, measuredDetector (p)) + levelDb (p[(std::size_t) param (b, fLevel)]);
}

double responseDb (const EffectParams& p, const double* bandGainDb, double sampleRate, double hz)
{
    // round 150: the bands' segments (the split layout: band b = segment b; the free layout: gaps between)
    const auto L = layoutOf (p);
    const auto ns = L.segments;
    const auto w = 2.0 * pi * hz / sampleRate;
    const auto segGain = [&] (int k) { return L.bandOf[(std::size_t) k] >= 0 ? bandGainDb[L.bandOf[(std::size_t) k]] : 0.0; };
    // segments of the tree: S0 = L1, S1 = H1·L2, S2 = H1·H2·L3, …, the last = H1·…·H(n−1); out = x + Σ (g − 1)·Sk
    std::array<std::complex<double>, maxSegments> band {};
    std::complex<double> rest { 1.0, 0.0 };
    for (int k = 0; k < ns - 1; ++k)
    {
        Split s;
        s.design (sampleRate, L.xoverHz[(std::size_t) k], iirSlope (L.xoverSlope[(std::size_t) k]));
        band[(std::size_t) k] = rest * s.response (false, w);
        rest *= s.response (true, w);
    }
    band[(std::size_t) (ns - 1)] = rest;
    // round 144: the processing mode — linear phase: the same magnitudes, no phase; minimum phase: the lower bands
    // pass the all-passes of the crossovers above them, the output is the sum of the bands
    const auto mode = phaseOf (p[processingMode]);
    const auto nb = bandCount (p);
    bool solo = false;
    for (int b = 0; b < nb; ++b)
        solo = solo || on (p[(std::size_t) param (b, fSolo)]);
    const auto om = minphase::warp (hz, sampleRate);
    const auto wOf = [&] (double edge) { return edge > 0.0 ? minphase::warp (std::min (edge, sampleRate * 0.45), sampleRate) : 0.0; };
    if (mode == Phase::linear && freeLayout (p))
    {
        // round 150: each band's own linear-phase shape (Pro-MB's 1 / (1 + x^m), m = slope / 6, continuous); round
        // tp172b7: on the analog axis
        const auto aOf = [&] (double edge) { return edge > 0.0 ? edge : 0.0; };
        std::complex<double> y = solo ? std::complex<double> {} : std::complex<double> { 1.0, 0.0 };
        for (int b = 0; b < nb; ++b)
        {
            if (L.segmentOf[(std::size_t) b] < 0)
                continue;
            const auto a = linearShare (aOf (L.loHz[(std::size_t) b]), aOf (L.hiHz[(std::size_t) b]), L.loSlope[(std::size_t) b] / 6.0, L.hiSlope[(std::size_t) b] / 6.0, hz);
            const auto g = dbToGain (bandGainDb[b]);
            if (solo)
            {
                if (on (p[(std::size_t) param (b, fSolo)]))
                    y += g * a;
            }
            else
                y += (g - 1.0) * a;
        }
        return gainToDb (std::abs (y)) + trimDb (p[output]);
    }
    if (mode == Phase::linear)
    {
        // the segments' magnitudes (zero phase), round tp172b7: each crossover on the analog axis, as the FIRs
        double rest = 1.0;
        for (int k = 0; k < ns - 1; ++k)
        {
            Split s;
            s.design (sampleRate, L.xoverHz[(std::size_t) k], iirSlope (L.xoverSlope[(std::size_t) k]));
            const auto wk = analogBin (w, L.xoverHz[(std::size_t) k], sampleRate);
            band[(std::size_t) k] = rest * std::abs (s.response (false, wk));
            rest *= std::abs (s.response (true, wk));
        }
        band[(std::size_t) (ns - 1)] = rest;
    }
    else if (mode == Phase::minimum)
        for (int k = 0; k < ns - 2; ++k)
            for (int j = k + 1; j < ns - 1; ++j)
            {
                Split s;
                s.design (sampleRate, L.xoverHz[(std::size_t) j], iirSlope (L.xoverSlope[(std::size_t) j]));
                band[(std::size_t) k] *= s.response (false, w) + s.response (true, w);
            }
    if (mode == Phase::dynamic && measuredDynamic (p) && ! solo)
    {
        // round 147: the measured engine — the product of each band's 1 + (g − 1)·A (its own crossovers' magnitudes);
        // round 150: any slope (the 6 dB/oct models), each band's own edges
        double m = 1.0;
        for (int b = 0; b < nb; ++b)
        {
            if (L.segmentOf[(std::size_t) b] < 0)
                continue;
            m *= minphase::bandMagnitudeM (wOf (L.loHz[(std::size_t) b]), wOf (L.hiHz[(std::size_t) b]),
                                           minphase::dynamicSideCode (L.loSlope[(std::size_t) b]), minphase::dynamicSideCode (L.hiSlope[(std::size_t) b]),
                                           dbToGain (bandGainDb[b]), om, on (p[rev152]));
        }
        return gainToDb (std::abs (m)) + trimDb (p[output]);
    }
    std::complex<double> y = solo || mode == Phase::minimum ? std::complex<double> {} : std::complex<double> { 1.0, 0.0 };
    for (int k = 0; k < ns; ++k)
    {
        const auto bi = L.bandOf[(std::size_t) k];
        const auto g = dbToGain (segGain (k));
        if (solo)
        {
            if (bi >= 0 && on (p[(std::size_t) param (bi, fSolo)]))
                y += g * band[(std::size_t) k];
        }
        else if (mode == Phase::minimum)
            y += g * band[(std::size_t) k];
        else
            y += (g - 1.0) * band[(std::size_t) k];
    }
    return gainToDb (std::abs (y)) + trimDb (p[output]);
}

namespace
{
// ---- round 190lg (REA_rodada190lg §7, static REA of Pro-MB's BandCompander; LYNX's own code from the laws): revision 1.
// Per band and detector channel: the level in dB → the gain computer (the usual quadratic knee and ratio, then a soft
// range of width 0.6 × knee + 12 dB) → the maximum over a hold window → a low pass timed by the look-ahead → follower A
// (slow: it sets a floor) → max (A, the low-passed reduction) → follower B (the knob times) = the band's reduction.
#ifdef LYNX_MBLAB
// lab build only: LYNX_MBLAB="key=value …" scales the laws (fitting on the bank; never in a product build)
struct MbLab
{
    double aAtk = 1, aRel = 1, bAtk = 1, bRel = 1, p6 = 1, hold = 1, lead = 1, lpOrder = 2, soft = 1, oldSoft = 0;
    double inv = 1, invSlope = 0, invW = 1;
    MbLab()
    {
        if (const char* e = std::getenv ("LYNX_MBLAB"))
        {
            std::string t (e);
            std::size_t i = 0;
            while (i < t.size())
            {
                auto j = t.find (' ', i);
                if (j == std::string::npos)
                    j = t.size();
                const auto kv = t.substr (i, j - i);
                const auto eq = kv.find ('=');
                if (eq != std::string::npos)
                {
                    const auto k = kv.substr (0, eq);
                    const auto v = std::atof (kv.c_str() + eq + 1);
                    double* f = k == "aAtk" ? &aAtk : k == "aRel" ? &aRel : k == "bAtk" ? &bAtk : k == "bRel" ? &bRel : k == "p6" ? &p6
                              : k == "hold" ? &hold : k == "lead" ? &lead : k == "lp" ? &lpOrder : k == "soft" ? &soft
                              : k == "oldSoft" ? &oldSoft : k == "inv" ? &inv
                              : k == "invSlope" ? &invSlope : k == "invW" ? &invW : nullptr;
                    if (f != nullptr)
                        *f = v;
                }
                i = j + 1;
            }
        }
    }
};
const MbLab& mbLab() { static const MbLab l; return l; }
#endif
// the release knob (0 … 1) in seconds: LYNX's own smooth law fitted to the shape of the reference's curve (5 ms … 5 s:
// a power-law rise, then a steep step around 88 % into the long times; fitted on 15 … 100 %, within 13 % there)
double mbReleaseS (float v) noexcept
{
    const auto x = (double) std::clamp (v, 0.0f, 1.0f);
    const auto step = [] (double t) { return 1.0 / (1.0 + std::exp (-(t - 0.878) / 0.044)); };
    const auto lo = step (0.0), hi = step (1.0);
    return 0.005 + 1.64 * std::pow (x, 1.87) + (4.995 - 1.64) * (step (x) - lo) / (hi - lo);
}
// the band's detector lead (ms) under the reference's look-ahead switch: half a period of the band's centre (at most 5 ms)
// plus the band's own look-ahead, 0.1 … 20 ms
double mbLeadMs (double loHz, double hiHz, double ownMs) noexcept
{
    const auto lo = loHz > 0.0 ? loHz : 20.0, hi = hiHz > 0.0 ? hiHz : 30000.0;
    return std::clamp (500.0 / std::max (std::sqrt (lo * hi), 100.0) + ownMs, 0.1, 20.0);
}
// the gain computer of a band that compresses downward or expands upward (≥ 0 dB): knee, ratio, the soft range
double mbReduction (double over, double squeeze, double knee, double range, double soft) noexcept
{
    auto g = kneeCurve (over, squeeze, knee);
    if (soft > 1.1920928955078125e-07)
    {
        const auto v = range - g;
        if (v >= -0.5 * soft && v <= 0.5 * soft)
            g = range - (0.5 * soft + v) / soft * ((0.5 * soft + v) / soft) * (0.5 * soft);
    }
    return std::max (0.0, std::min (g, range));
}
// the inverted modes' gain computer (0 … 36 dB): below the threshold, a one-sided knee (the overshoot's square inside the
// knee's width, then the overshoot − half the width) × (ratio − 1), the soft range of the knee's width under 36 dB
double mbBelow (double under, double slope, double knee) noexcept
{
    if (under <= 0.0)
        return 0.0;
    auto v = under - 0.5 * knee;
    if (knee > 1.1920928955078125e-07 && v <= 0.5 * knee)
        v = under / knee * (under / knee) * (0.5 * knee);
    return mbReduction (v * slope, 1.0, 0.0, 36.0, knee);
}
// one detector channel's state
struct MbDetector
{
    crest::SlidingMax hold;
    double lp1 = 0.0, lp2 = 0.0, a = 0.0, b = 0.0;
    crest::V2Cache ka, kb;
    void reset() noexcept
    {
        hold.clear();
        lp1 = lp2 = a = b = 0.0;
        ka = kb = crest::V2Cache {};
    }
};
// a band's revision-1 settings for one chunk
struct MbBand
{
    bool on = false;
    bool inverted = false;       // upward compression / downward expansion: the graph runs on 36 − the detector's output
    double sign = -1.0, thr = 0.0, squeeze = 0.0, knee = 0.0, range = 0.0, soft = 12.0;
    double slope = 1.0, scale = 1.0, relA = 0.0, rel = 0.0;
    int holdN = 0;
    int lpOrder = 2;             // the low pass: two-pole Butterworth (1: one pole; the lab compares)
    double lp[5] { 1, 0, 0, 0, 0 }; // b0, b1, b2, a1, a2 (one pole: lp[0] = its coefficient)
    crest::V2Follow fa, fb;
};

// a delay line sized in prepare() (the linear-phase path's dry signal: up to 12 288 samples at 192 kHz)
struct LongDelay
{
    std::vector<float> l, r;
    int pos = 0, len = 0;
    void allocate (int n) { l.assign ((std::size_t) n + 1, 0.0f); r.assign ((std::size_t) n + 1, 0.0f); len = n; pos = 0; }
    void reset() noexcept { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); pos = 0; }
    void process (float& a, float& b) noexcept
    {
        if (len == 0 || l.empty())
            return;
        const auto size = (int) l.size();
        l[(std::size_t) pos] = a;
        r[(std::size_t) pos] = b;
        auto rd = pos - len;
        if (rd < 0)
            rd += size;
        a = l[(std::size_t) rd];
        b = r[(std::size_t) rd];
        if (++pos == size)
            pos = 0;
    }
};

// round 147: Pro-MB's free side-chain filter, measured black-box (REA_rodada147 §2): a 4th-order high pass at the low
// frequency and low pass at the high one (−24 dB one octave out), their sections' Q 1.25 × Butterworth (the fit to the
// reference's band shapes of one octave and wider; narrower bands boost more in the reference)
struct ScFilter
{
    std::array<Biquad, 4> s;
    Biquad bell;
    double lo = -1.0, hi = -1.0, rate = 0.0;
    bool useHp = false, useLp = false, useBell = false, law = false;
    // round 150 (REA_rodada150 §4, the round-147 sweeps refitted): with the measured detector the free filter is the
    // 4th-order Butterworth high / low pass (section Q × a) times a bell at the band's centre whose gain grows as the
    // band narrows (the reference's +13.7 dB for 2 – 2.5 kHz): fitted per width (0.32 / 0.58 / 1 / 1.58 octaves, rms
    // 0.09 / 0.10 / 0.21 / 0.28 dB), interpolated, none from 3 octaves up
    static void narrowLaw (double octaves, double& a, double& gainDb, double& q) noexcept
    {
        static constexpr double w[5] { 0.32, 0.58, 1.0, 1.58, 3.0 };
        static constexpr double av[5] { 0.993, 0.992, 1.058, 0.984, 1.0 };
        static constexpr double gv[5] { 16.94, 12.63, 4.85, 1.36, 0.0 };
        static constexpr double qv[5] { 4.811, 2.567, 0.762, 0.195, 0.195 };
        const auto x = std::clamp (octaves, w[0], w[4]);
        int k = 0;
        while (k < 3 && x > w[k + 1])
            ++k;
        const auto t = (x - w[k]) / (w[k + 1] - w[k]);
        a = av[k] + (av[k + 1] - av[k]) * t;
        gainDb = gv[k] + (gv[k + 1] - gv[k]) * t;
        q = std::exp (std::log (qv[k]) + (std::log (qv[k + 1]) - std::log (qv[k])) * t);
    }
    void design (double sampleRate, double loHz, double hiHz, bool measured = false) noexcept
    {
        if (loHz == lo && hiHz == hi && sampleRate == rate && measured == law)
            return;
        lo = loHz;
        hi = hiHz;
        rate = sampleRate;
        law = measured;
        static constexpr double bw[2] { 0.54119610, 1.30656296 };
        useHp = loHz > 30.5;
        useLp = hiHz < sampleRate * 0.45;
        useBell = false;
        double scale = 1.25; // round 147: Q 1.25 × Butterworth (the fit to bands of one octave and wider)
        if (measured)
        {
            double g = 0.0, bq = 1.0;
            narrowLaw (useHp && useLp ? std::log2 (std::max (hiHz, loHz * 1.001) / loHz) : 3.0, scale, g, bq);
            if (useHp && useLp && g > 0.01)
            {
                useBell = true;
                bell.peaking (sampleRate, std::min (std::sqrt (loHz * hiHz), sampleRate * 0.45), bq, g);
            }
        }
        for (int k = 0; k < 2; ++k)
        {
            s[(std::size_t) k].filter (sampleRate, 1, loHz, bw[k] * scale);
            s[(std::size_t) (2 + k)].filter (sampleRate, 0, hiHz, bw[k] * scale);
        }
    }
    void reset() noexcept
    {
        for (auto& b : s)
            b.reset();
        bell.reset();
    }
    void run (float& l, float& r) noexcept
    {
        for (int k = 0; k < 4; ++k)
            if (k < 2 ? useHp : useLp)
            {
                l = s[(std::size_t) k].process (l, 0);
                r = s[(std::size_t) k].process (r, 1);
            }
        if (useBell)
        {
            l = bell.process (l, 0);
            r = bell.process (r, 1);
        }
    }
};

// ---------------------------------------------------------------- round 190mg: the C4 law (REA_rodada190mg.md §2)
// LYNX's own engine with the Waves C4's structure (static analysis of the structure, every number fitted on our demo-mode
// probes, no vendor table read):
//  * crossover k: Q(s) = 1 + s / (q w) + (s / w)², w set so that the low and high outputs are both -6 dB at the nominal
//    frequency (bilinear, warped there); low = 1 / Q(s)², all-pass = Q(-s) / Q(s), high = all-pass - low: the two are in
//    phase, the four bands (low, low-mid, high-mid, high; each compensated by the later all-passes) sum to the all-pass
//    chain exactly (probes: < -86 dB ESR);
//  * per band: peak of the band (stereo linked) → g = 1 / (1 + k · peak / threshold), the knee's cubic
//    g + 2 knee · g² (1 − g) (≤ 1), the minimum over the look-ahead, the smoother with its programme-dependent release
//    (TimeLaw) and its slow follower, the deeper of the two (unlike RComp, which takes the smoother), two box filters, then the band's gain range + (1 − range) · g (linear), times its make-up; the audio is
//    delayed by the look-ahead.
//  k = 0.5 · 2^(-R / 12) at knee 0 (R = range dB; probes: ±0.5 %), times exp (s(R) · (knee / 0.5)^0.78) with s(R)
//  measured at knee 0.5 (c4KneeNodes) — 0.003 dB rms on every stair.
namespace c4
{
constexpr int bandsN = 4;
constexpr double kneeR[9] { -24.0, -18.0, -12.0, -6.0, -3.0, 3.0, 6.0, 12.0, 18.0 };
constexpr double kneeS[9] { 0.1608, 0.2068, 0.2601, 0.3216, 0.3525, 0.4330, 0.4737, 0.5603, 0.6567 };
// time law: the Renaissance family's (the same structure round 190mg ren read in RComp: a one-pole smoother s on the gain,
// a slow follower q of s, s released with tau = release · (1 − v^vpow)^vexp (v = q electro, 1 − q opto) times the factor
// 2^(−d (1/(m + pe) − 1/(q + pe))), m = min (s, q), both read at 255 steps), every constant fitted on the C4's own probes
struct TimeLaw { double attackScale, releaseScale, d, pe, qFallMs, qRiseMs, vpow, vexp; };
// c4fit:begin
constexpr TimeLaw c4Time { 0.999677, 0.998302, 0.003912, 0.005823, 149.78441, 56.044957, 4.015778, 4.006325 }; // c4/lab/fit5.py (19 probes, −65 … −84 dB)
// c4fit:end
// round 190mm (dyn): the C6's floating bands — their band-pass Q against the control's and their threshold offset (dB),
// fitted on the C6's X2 renders (lab/c6var.sh: Q × 1.0 / 0.8 / 0.7 / 0.65 / 0.5 → 0.67); threshold offset 0 / −3 / +3 dB → 0 (−3: one bank file down)
#ifndef C6_QSCALE
#define C6_QSCALE 0.67
#endif
#ifndef C6_THRDB
#define C6_THRDB 0.0
#endif
constexpr double c6QScale = C6_QSCALE, c6ThrDb = C6_THRDB;
constexpr int boxA = 27, boxB = 38; // the two box filters (27 + 38 − 1 = 64 samples at 48 kHz, × the look-ahead's rate factor)

inline double kOf (double rangeDb, double knee) noexcept
{
    const double r = std::clamp (rangeDb, kneeR[0], kneeR[8]);
    int i = 0;
    while (i < 7 && r > kneeR[i + 1])
        ++i;
    const double t = (r - kneeR[i]) / (kneeR[i + 1] - kneeR[i]);
    const double s = kneeS[i] + t * (kneeS[i + 1] - kneeS[i]);
    return 0.5 * std::pow (2.0, -rangeDb / 12.0) * (knee > 0.0 ? std::exp (s * std::pow (knee / 0.5, 0.78)) : 1.0);
}

struct Bq
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, 4> z {};
    double run (double x) noexcept
    {
        const double y = b0 * x + b1 * z[0] + b2 * z[1] - a1 * z[2] - a2 * z[3];
        z[1] = z[0]; z[0] = x; z[3] = z[2]; z[2] = y;
        return y;
    }
    void reset() noexcept { z.fill (0.0); }
};

struct Crossover
{
    Bq lpA, lpB, ap;
    void design (double fc, double q, double sr) noexcept
    {
        const double b = 1.0 / (q * q) - 2.0, l2 = 0.5 * (-b + std::sqrt (b * b + 4.0));
        const double K = 2.0 * sr, w = K * std::tan (pi * std::clamp (fc, 10.0, 0.49 * sr) / sr) / std::sqrt (l2);
        const double al = K / (q * w), be = (K / w) * (K / w);
        const double p0 = 1.0 + al + be, p1 = (2.0 - 2.0 * be) / p0, p2 = (1.0 - al + be) / p0;
        for (auto* s : { &lpA, &lpB })
        {
            s->a1 = p1; s->a2 = p2; s->b0 = 1.0 / p0; s->b1 = 2.0 / p0; s->b2 = 1.0 / p0;
        }
        ap.a1 = p1; ap.a2 = p2; ap.b0 = p2; ap.b1 = p1; ap.b2 = 1.0;
    }
    void reset() noexcept { lpA.reset(); lpB.reset(); ap.reset(); }
};

class Engine
{
public:
    void prepare (double rate) noexcept
    {
        sr = rate;
        la = c4Lookahead (rate);
        boxLenA = boxA * la / 64;
        boxLenB = la + 1 - boxLenA;
        key.fill (-1.0);
        reset();
    }
    int latency() const noexcept { return la; }
    void reset() noexcept
    {
        for (auto& ch : xo)
            for (auto& x : ch)
                x.reset();
        for (auto& ch : comp)
            for (auto& x : ch)
                x.reset();
        for (auto& ch : delay)
            for (auto& d : ch)
                d.fill (0.0);
        dpos = 0;
        for (int b = 0; b < bandsN; ++b)
        {
            s1[(std::size_t) b] = s2[(std::size_t) b] = 1.0;
            qHead[(std::size_t) b] = qTail[(std::size_t) b] = 0;
            for (auto& v : box1[(std::size_t) b]) v = one;
            for (auto& v : box2[(std::size_t) b]) v = one;
            acc1[(std::size_t) b] = one * boxLenA;
            acc2[(std::size_t) b] = one * boxLenB;
        }
        bposA = bposB = 0;
        tick = 0;
        fOn.fill (false); // round 190mm (dyn): the floating bands start again at their next configure
    }

    // returns the four bands' gain reductions (dB, for the meters) of the last sample
    void process (float* l, float* r, int n, const EffectParams& p, std::array<double, 6>& extreme) noexcept
    {
        configure (p);
        for (int i = 0; i < n; ++i)
        {
            double bandS[2][bandsN];
            const double in[2] { (double) l[i] * inGain, (double) r[i] * inGain };
            for (int c = 0; c < 2; ++c)
            {
                auto& x = xo[(std::size_t) c];
                auto& k = comp[(std::size_t) c];
                const double v = in[c];
                const double l1 = x[0].lpB.run (x[0].lpA.run (v)), h1 = x[0].ap.run (v) - l1;
                const double l2 = x[1].lpB.run (x[1].lpA.run (h1)), h2 = x[1].ap.run (h1) - l2;
                const double l3 = x[2].lpB.run (x[2].lpA.run (h2)), h3 = x[2].ap.run (h2) - l3;
                bandS[c][0] = k[1].ap.run (k[0].ap.run (l1));
                bandS[c][1] = k[2].ap.run (l2);
                bandS[c][2] = l3;
                bandS[c][3] = h3;
            }
            double y[2] { 0.0, 0.0 };
            for (int b = 0; b < bandsN; ++b)
            {
                const auto bi = (std::size_t) b;
                const double lev = std::max (std::abs (bandS[0][b]), std::abs (bandS[1][b]));
                double g = 1.0 / (1.0 + kb[bi] * lev * invT[bi]);
                g = std::min (g + cKnee[bi] * g * g * (1.0 - g), 1.0);
                // sliding minimum over the look-ahead (monotonic queue)
                auto& qv = qVal[bi];
                auto& qt = qTime[bi];
                auto& h = qHead[bi];
                auto& t = qTail[bi];
                while (t != h && qv[(std::size_t) ((t + qcap - 1) % qcap)] >= g)
                    t = (t + qcap - 1) % qcap;
                qv[(std::size_t) t] = g;
                qt[(std::size_t) t] = tick;
                t = (t + 1) % qcap;
                while (qt[(std::size_t) h] + la <= tick)
                    h = (h + 1) % qcap;
                const double m = qv[(std::size_t) h];
                double co = cAtt[bi];
                if (m >= s1[bi]) // release: the programme-dependent law
                {
                    const double q = s2[bi];
                    const double qv8 = std::floor (q * 255.0) / 256.0, mv8 = std::floor (std::min (s1[bi], q) * 255.0) / 255.0;
                    const double qq = std::floor (q * 255.0) / 255.0;
                    const double v = std::clamp (opto ? 1.0 - qv8 : qv8, 0.0, 1.0);
                    const double tau = std::max (0.002, relMs[bi] * std::pow (1.0 - std::pow (v, c4Time.vpow), c4Time.vexp));
                    co = std::exp (-1.0 / (tau * 1.0e-3 * sr)) * std::exp2 (-c4Time.d * (1.0 / (mv8 + c4Time.pe) - 1.0 / (qq + c4Time.pe)));
                }
                s1[bi] = (s1[bi] - m) * co + m;
                s2[bi] = (s2[bi] - s1[bi]) * (s1[bi] < s2[bi] ? cqFall : cqRise) + s1[bi];
                // two box filters (64-bit fixed point: no drift)
                const auto qn = (std::int64_t) std::llround (std::min (s1[bi], s2[bi]) * (double) one); // the deeper of the two
                acc1[bi] += qn - box1[bi][(std::size_t) bposA];
                box1[bi][(std::size_t) bposA] = qn;
                const auto v1 = acc1[bi] / boxLenA;
                acc2[bi] += v1 - box2[bi][(std::size_t) bposB];
                box2[bi][(std::size_t) bposB] = v1;
                const double gs = (double) acc2[bi] / (double) boxLenB / (double) one;
                const double G = (active[bi] ? gr[bi] + (1.0 - gr[bi]) * gs : 1.0) * mk[bi];
                extreme[bi] = std::min (extreme[bi], 20.0 * std::log10 (std::max (G / mk[bi], 1.0e-6)));
                const double w = (soloAny && ! solo[bi]) ? 0.0 : mix * G + (1.0 - mix);
                for (int c = 0; c < 2; ++c)
                {
                    auto& d = delay[(std::size_t) c][bi];
                    const double old = d[(std::size_t) dpos];
                    d[(std::size_t) dpos] = bandS[c][b];
                    y[c] += old * w;
                }
            }
            if (floatAny) // round 190mm (dyn): the C6's floating bands on the crossovers' sum
                floating (bandS, y);
            dpos = (dpos + 1) % la;
            bposA = (bposA + 1) % boxLenA;
            bposB = (bposB + 1) % boxLenB;
            ++tick;
            l[i] = (float) (y[0] * outGain);
            r[i] = (float) (y[1] * outGain);
        }
    }

private:
    // round 190mm (dyn): a floating band = a band-pass of the crossovers' sum (in phase with the bands), its level through
    // the same gain law and smoother as the bands, added back as (G − 1) · the delayed band-pass
    void floating (const double (&bandS)[2][bandsN], double (&y)[2]) noexcept
    {
        for (int f = 0; f < floatN; ++f)
        {
            const auto fi = (std::size_t) f;
            if (! fOn[fi])
                continue;
            double bp[2];
            for (int c = 0; c < 2; ++c)
                bp[c] = fBp[(std::size_t) c][fi].run (bandS[c][0] + bandS[c][1] + bandS[c][2] + bandS[c][3]);
            const double lev = std::max (std::abs (bp[0]), std::abs (bp[1]));
            double g = 1.0 / (1.0 + fkb[fi] * lev * fInvT[fi]);
            g = std::min (g + fCKnee[fi] * g * g * (1.0 - g), 1.0);
            auto& qv = fqVal[fi];
            auto& qt = fqTime[fi];
            auto& h = fqHead[fi];
            auto& t = fqTail[fi];
            while (t != h && qv[(std::size_t) ((t + qcap - 1) % qcap)] >= g)
                t = (t + qcap - 1) % qcap;
            qv[(std::size_t) t] = g;
            qt[(std::size_t) t] = tick;
            t = (t + 1) % qcap;
            while (qt[(std::size_t) h] + la <= tick)
                h = (h + 1) % qcap;
            const double m = qv[(std::size_t) h];
            double co = fAtt[fi];
            if (m >= fs1[fi])
            {
                const double q = fs2[fi];
                const double qv8 = std::floor (q * 255.0) / 256.0, mv8 = std::floor (std::min (fs1[fi], q) * 255.0) / 255.0;
                const double qq = std::floor (q * 255.0) / 255.0;
                const double v = std::clamp (opto ? 1.0 - qv8 : qv8, 0.0, 1.0);
                const double tau = std::max (0.002, fRel[fi] * std::pow (1.0 - std::pow (v, c4Time.vpow), c4Time.vexp));
                co = std::exp (-1.0 / (tau * 1.0e-3 * sr)) * std::exp2 (-c4Time.d * (1.0 / (mv8 + c4Time.pe) - 1.0 / (qq + c4Time.pe)));
            }
            fs1[fi] = (fs1[fi] - m) * co + m;
            fs2[fi] = (fs2[fi] - fs1[fi]) * (fs1[fi] < fs2[fi] ? cqFall : cqRise) + fs1[fi];
            const auto qn = (std::int64_t) std::llround (std::min (fs1[fi], fs2[fi]) * (double) one);
            facc1[fi] += qn - fbox1[fi][(std::size_t) bposA];
            fbox1[fi][(std::size_t) bposA] = qn;
            const auto v1 = facc1[fi] / boxLenA;
            facc2[fi] += v1 - fbox2[fi][(std::size_t) bposB];
            fbox2[fi][(std::size_t) bposB] = v1;
            const double gs = (double) facc2[fi] / (double) boxLenB / (double) one;
            const double G = (fgr[fi] + (1.0 - fgr[fi]) * gs) * fmk[fi];
            for (int c = 0; c < 2; ++c)
            {
                auto& d = fDelay[(std::size_t) c][fi];
                const double old = d[(std::size_t) dpos];
                d[(std::size_t) dpos] = bp[c];
                y[c] += old * (G - 1.0) * mix;
            }
        }
    }
    void configureFloating (const EffectParams& p) noexcept
    {
        floatAny = false;
        for (int f = 0; f < floatN; ++f)
        {
            const auto fi = (std::size_t) f;
            const auto at = [&] (int field) { return p[(std::size_t) c6Param (f, field)]; };
            const bool was = fOn[fi];
            fOn[fi] = at (c6On) >= 0.5f;
            if (! fOn[fi])
                continue;
            floatAny = true;
            if (! was)
            {
                fs1[fi] = fs2[fi] = 1.0;
                fqHead[fi] = fqTail[fi] = 0;
                for (auto& v : fbox1[fi]) v = one;
                for (auto& v : fbox2[fi]) v = one;
                facc1[fi] = one * boxLenA;
                facc2[fi] = one * boxLenB;
                for (int c = 0; c < 2; ++c)
                {
                    fBp[(std::size_t) c][fi].reset();
                    fDelay[(std::size_t) c][fi].fill (0.0);
                }
            }
            const double hz = 16.0 * std::pow (21357.0 / 16.0, (double) std::clamp (at (c6Freq), 0.0f, 1.0f));
            const double q = c6QScale * 0.35 * std::pow (60.0 / 0.35, (double) std::clamp (at (c6Width), 0.0f, 1.0f));
            if (hz != fHz[fi] || q != fQ[fi])
            {
                fHz[fi] = hz;
                fQ[fi] = q;
                // a band-pass with 0 dB at its centre (bilinear, warped there)
                const double w0 = 2.0 * std::numbers::pi * std::clamp (hz, 10.0, 0.49 * sr) / sr, al = std::sin (w0) / (2.0 * q), a0 = 1.0 + al;
                for (int c = 0; c < 2; ++c)
                {
                    auto& b = fBp[(std::size_t) c][fi];
                    b.b0 = al / a0; b.b1 = 0.0; b.b2 = -al / a0; b.a1 = -2.0 * std::cos (w0) / a0; b.a2 = (1.0 - al) / a0;
                }
            }
            const double rng = std::clamp ((double) at (c6Range) * 60.0 - 30.0, -30.0, 30.0), knee = kneeDb (p[(std::size_t) param (0, fKnee)]) / 64.0;
            fgr[fi] = dbToGain (rng);
            fInvT[fi] = 1.0 / dbToGain (thresholdDb (at (c6Thr)) + c6ThrDb);
            fkb[fi] = kOf (rng, knee);
            fCKnee[fi] = 2.0 * knee;
            fmk[fi] = dbToGain ((double) at (c6Gain) * 60.0 - 30.0);
            const double attMs = 0.5 * std::pow (1000.0, (double) std::clamp (at (c6Att), 0.0f, 1.0f));
            const double relMs0 = 5.0 * std::pow (1000.0, (double) std::clamp (at (c6Rel), 0.0f, 1.0f));
            fAtt[fi] = std::exp (-1.0 / (c4Time.attackScale * attMs * 1.0e-3 * sr));
            fRel[fi] = c4Time.releaseScale * relMs0;
        }
    }

    void configure (const EffectParams& p) noexcept
    {
        std::array<double, 4 + 6 * bandsN + 4> k {};
        std::size_t n = 0;
        const auto xs = crossovers (p);
        for (int j = 0; j < 3; ++j)
            k[n++] = xs[(std::size_t) j];
        k[n++] = c4QOf (p);
        if (k[0] != key[0] || k[1] != key[1] || k[2] != key[2] || k[3] != key[3])
        {
            for (int c = 0; c < 2; ++c)
            {
                for (int j = 0; j < 3; ++j)
                    xo[(std::size_t) c][(std::size_t) j].design (xs[(std::size_t) j], k[3], sr);
                comp[(std::size_t) c][0].design (xs[1], k[3], sr);
                comp[(std::size_t) c][1].design (xs[2], k[3], sr);
                comp[(std::size_t) c][2].design (xs[2], k[3], sr);
            }
            for (int j = 0; j < 4; ++j)
                key[(std::size_t) j] = k[(std::size_t) j];
        }
        const auto at = [&] (int b, int f) { return p[(std::size_t) param (b, f)]; };
        opto = c4Opto (p);
        soloAny = false;
        for (int b = 0; b < bandsN; ++b)
        {
            const auto bi = (std::size_t) b;
            const double rng = rangeDb (at (b, fRange)), knee = kneeDb (at (b, fKnee)) / 64.0;
            gr[bi] = dbToGain (rng);
            invT[bi] = 1.0 / dbToGain (thresholdDb (at (b, fThreshold)));
            kb[bi] = kOf (rng, knee);
            cKnee[bi] = 2.0 * knee;
            mk[bi] = dbToGain (levelDb (at (b, fLevel)));
            active[bi] = on (at (b, fEnabled));
            solo[bi] = on (at (b, fSolo));
            soloAny = soloAny || solo[bi];
            const double attMs = 0.5 * std::pow (1000.0, (double) std::clamp (at (b, fAttack), 0.0f, 1.0f));
            const double relMs0 = 5.0 * std::pow (1000.0, (double) std::clamp (at (b, fRelease), 0.0f, 1.0f));
            cAtt[bi] = std::exp (-1.0 / (c4Time.attackScale * attMs * 1.0e-3 * sr));
            relMs[bi] = c4Time.releaseScale * relMs0;
        }
        cqFall = std::exp (-1.0 / (c4Time.qFallMs * 1.0e-3 * sr));
        cqRise = std::exp (-1.0 / (c4Time.qRiseMs * 1.0e-3 * sr));
        mix = std::clamp ((double) p[prism::mix], 0.0, 1.0);
        inGain = dbToGain (trimDb (p[input]));
        outGain = dbToGain (trimDb (p[output]));
        configureFloating (p);
    }

    static constexpr std::int64_t one = (std::int64_t) 1 << 40;
    static constexpr int maxLa = 256, qcap = maxLa + 2, maxBox = 160;
    double sr = 48000.0;
    int la = 64, boxLenA = boxA, boxLenB = boxB, dpos = 0, bposA = 0, bposB = 0;
    std::int64_t tick = 0;
    std::array<double, 4> key {};
    std::array<std::array<Crossover, 3>, 2> xo {}, comp {};
    std::array<std::array<std::array<double, maxLa>, bandsN>, 2> delay {};
    std::array<double, bandsN> s1 {}, s2 {}, gr {}, invT {}, kb {}, cKnee {}, mk {}, cAtt {}, relMs {};
    bool opto = false;
    std::array<bool, bandsN> active {}, solo {};
    bool soloAny = false;
    double cqFall = 0.0, cqRise = 0.0, mix = 1.0, inGain = 1.0, outGain = 1.0;
    std::array<std::array<double, qcap>, bandsN> qVal {};
    std::array<std::array<std::int64_t, qcap>, bandsN> qTime {};
    std::array<int, bandsN> qHead {}, qTail {};
    std::array<std::array<std::int64_t, maxBox>, bandsN> box1 {}, box2 {};
    std::array<std::int64_t, bandsN> acc1 {}, acc2 {};
    // round 190mm (dyn): the C6's floating bands
    static constexpr int floatN = 2;
    bool floatAny = false;
    std::array<bool, floatN> fOn {};
    std::array<double, floatN> fHz {}, fQ {}, fs1 {}, fs2 {}, fgr {}, fInvT {}, fkb {}, fCKnee {}, fmk {}, fAtt {}, fRel {};
    std::array<std::array<Bq, floatN>, 2> fBp {};
    std::array<std::array<std::array<double, maxLa>, floatN>, 2> fDelay {};
    std::array<std::array<double, qcap>, floatN> fqVal {};
    std::array<std::array<std::int64_t, qcap>, floatN> fqTime {};
    std::array<int, floatN> fqHead {}, fqTail {};
    std::array<std::array<std::int64_t, maxBox>, floatN> fbox1 {}, fbox2 {};
    std::array<std::int64_t, floatN> facc1 {}, facc2 {};
};
} // namespace c4

class Multiband final : public Effect
{
public:
    explicit Multiband (const EffectParams* initial)
    {
        if (initial != nullptr)
        {
            // kept for prepare(), which configures again at the real rate: the latency stays the slot's (issue #113)
            lastParams = *initial;
            configure (*initial);
        }
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        laMs = -1.0;
        c4e.prepare (rate); // round 190mg
        // round 144: the linear-phase crossovers (one stereo FFT convolver per band below the top one) and the dry
        // delay that matches them, allocated here (process() never allocates)
        const auto size = linearFftSize (rate);
        if (size != linSize)
        {
            linSize = size;
            for (auto& c : lin)
                c.init (size);
            for (auto& t : linTable)
                t.assign ((std::size_t) (size / 2 + 1), 0.0f);
            linDry.allocate (linearLatency (rate));
            linSc.allocate (linearLatency (rate));
        }
        // round 190lg: revision 1's hold windows (up to 1.1 × 20 + 102 ms)
        for (auto& row : mbDet)
            for (auto& d : row)
                d.hold.setup ((int) std::ceil (0.13 * rate) + 2);
        mode = -1;
        osFactor = 0;
        configure (lastParams);
        designed = false;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& s : split)
            s.reset();
        for (auto& s : scSplit)
            s.reset();
        for (auto& row : comp)
            for (auto& s : row)
                s.reset();
        delay.reset();
        auditionDelay.reset();
        for (auto& d : bandDelay)
            d.reset();
        for (auto& d : gainDelay)
            d.reset();
        for (auto& d : gainDelayB)
            d.reset();
        for (auto& f2 : scFilter)
            f2.reset();
        for (auto& c : lin)
            if (c.n > 0)
                c.clear();
        linDry.reset();
        linSc.reset();
        resetOversamplers();
        for (auto& row : dynBand)
            for (auto& fl : row)
                fl.reset();
        dynWorks.fill (false);
        env.fill (-120.0);
        env2.fill (-120.0);
        for (auto& row : mbDet)
            for (auto& d : row)
                d.reset();
        held.fill (0.0);
        held2.fill (0.0);
        gains.fill (1.0f);
        c4e.reset();
    }

    int latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }
    void setSidechain (const float* l, const float* r) noexcept override
    {
        scL = l;
        scR = r;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        lastParams = p;
        // round 190mg: the C4 law runs its own engine (Prism's is untouched while it is off)
        if (c4Active (p))
        {
            if (! c4On)
            {
                c4e.reset();
                c4On = true;
            }
            latency.store (c4e.latency(), std::memory_order_relaxed);
            const auto inPk = blockPeak (l, r, n);
            extreme.fill (0.0);
            c4e.process (l, r, n, p, extreme);
            for (int b = 0; b < bands; ++b)
            {
                meters.set (bandMeter (b), b < c4::bandsN ? (float) std::clamp (extreme[(std::size_t) b] / 30.0, -1.0, 1.0) : 0.0f);
                meters.set (bandLevelMeter (b), 0.0f);
            }
            meters.set (4, inPk);
            meters.set (5, blockPeak (l, r, n));
            scL = scR = nullptr;
            return;
        }
        if (c4On)
        {
            c4On = false;
            laMs = -1.0;
            mode = -1;
        }
        configure (p);
        const auto inPeak = blockPeak (l, r, n);
        extreme.fill (0.0);
        const auto* sl = scL;
        const auto* sr2 = scR;
        for (int i = 0; i < n; i += chunk)
        {
            const auto m = std::min (chunk, n - i);
            const bool sc = sl != nullptr && sr2 != nullptr;
            processChunk (l + i, r + i, sc ? sl + i : nullptr, sc ? sr2 + i : nullptr, m, p);
            // round tp172b6: Audition replaces the output with what the band's detector hears
            if (auditionBand (p) >= 0 && auditionValid)
            {
                const auto outGain = (float) dbToGain (trimDb (p[output]));
                for (int k = 0; k < m; ++k)
                {
                    l[i + k] = auditionBuffer[0][(std::size_t) k] * outGain;
                    r[i + k] = auditionBuffer[1][(std::size_t) k] * outGain;
                }
            }
        }
        const auto nb = bandCount (p);
        for (int b = 0; b < bands; ++b)
        {
            meters.set (bandMeter (b), b < nb ? (float) std::clamp (extreme[(std::size_t) b] / 30.0, -1.0, 1.0) : 0.0f);
            // round tp172b6: the level each band's detector reads (peak), for the arc on the Threshold knob
            meters.set (bandLevelMeter (b), b < nb ? levelMeterValue (20.0 * std::log10 (detectorPeak[(std::size_t) b] + 1.0e-12)) : 0.0f);
            detectorPeak[(std::size_t) b] = 0.0;
        }
        meters.set (4, inPeak);
        meters.set (5, blockPeak (l, r, n));
        scL = scR = nullptr;
    }

private:
    static constexpr int chunk = 256, maxFactor = 4;

    void processChunk (float* l, float* r, const float* scl, const float* scr, int n, const EffectParams& p) noexcept
    {
        // round 150: the layout's segments (the split layout: band b = segment b, the old engine; the free layout: the
        // gaps between the bands are segments that pass untouched)
        const auto L = layoutOf (p);
        const auto ns = L.segments;
        std::array<int, maxSegments - 1> slope {};
        for (int k = 0; k < ns - 1; ++k)
            slope[(std::size_t) k] = iirSlope (L.xoverSlope[(std::size_t) k]);
        const auto nb = bandCount (p);
        const auto auditionNow = auditionBand (p) < nb ? auditionBand (p) : -1;
        auditionValid = false;
        const auto md = (Phase) mode;
        const bool linear = md == Phase::linear, minimum = md == Phase::minimum;
        const bool freeLin = linear && freeLayout (p); // round 150: each band its own linear-phase FIR
        if (! designed || L.xoverHz != lastF || slope != lastSlope || ns != lastNs)
        {
            for (int k = 0; k < ns - 1; ++k)
            {
                split[(std::size_t) k].design (sr, L.xoverHz[(std::size_t) k], slope[(std::size_t) k]);
                scSplit[(std::size_t) k].design (sr, L.xoverHz[(std::size_t) k], slope[(std::size_t) k]);
                for (int j = 0; j < ns - 1; ++j)
                    comp[(std::size_t) j][(std::size_t) k].design (sr, L.xoverHz[(std::size_t) k], slope[(std::size_t) k]);
            }
            lastF = L.xoverHz;
            lastSlope = slope;
            lastNs = ns;
            designed = true;
            kernelsDirty = true;
        }
        const auto key = kernelKey (L, freeLin, nb);
        if (key != lastKernelKey)
        {
            lastKernelKey = key;
            kernelsDirty = true;
            for (auto& c : lin)
                if (c.n > 0)
                    c.clear();
        }
        if (linear && kernelsDirty)
        {
            if (freeLin)
                designFreeKernels (L, nb);
            else
                designKernels (L, ns);
        }
        // the units the output sums: the segments, or (free linear phase) the bands' own FIR outputs
        const int nu = freeLin ? nb : ns;
        std::array<int, maxSegments> unitBand {};
        for (int u = 0; u < nu; ++u)
            unitBand[(std::size_t) u] = freeLin ? (L.segmentOf[(std::size_t) u] >= 0 ? u : -1) : L.bandOf[(std::size_t) u];
        // round 135: the detectors hear the slot's side chain, split by the same crossovers; round 150: each band chooses
        // the plug-in input or the side chain (or follows the global switch)
        const bool haveSc = scl != nullptr && scr != nullptr;
        const bool globalExt = on (p[scExternal]) && haveSc;
        std::array<bool, bands> bandExt {};
        bool external = false;
        for (int b = 0; b < nb; ++b)
        {
            const auto v = p[(std::size_t) (bandScInput0 + b)];
            bandExt[(std::size_t) b] = v < 0.25f ? globalExt : (v >= 0.75f && haveSc);
            external = external || bandExt[(std::size_t) b];
        }
        const auto inGain = (float) dbToGain (trimDb (p[input]));
        const auto outGain = (float) dbToGain (trimDb (p[output]));
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        const bool msPan = on (p[panMode]);
        const bool measuredDet = measuredDetector (p);

        std::array<double, bands> aA {}, aR {}, aR2 {}, aG {}, kG {}, level {};
        const bool gainAttack = measuredDet && on (p[rev152]);
        const bool rev190 = on (p[revision]); // round 190lg: the reference's band compander graph (hidden revision)
        std::array<bool, bands> enabled {}, soloed {}, growsWithLevel {};
        std::array<float, bands> panL {}, panR {};
        bool anySolo = false;
        for (int b = 0; b < nb; ++b)
        {
            const auto pan = (float) panOf (p[(std::size_t) bandPanParam (b)]);
            panL[(std::size_t) b] = pan > 0.0f ? 1.0f - pan : 1.0f;
            panR[(std::size_t) b] = pan < 0.0f ? 1.0f + pan : 1.0f;
            if (measuredDet) // round 150: the measured detector's three time constants
            {
                aA[(std::size_t) b] = coef (measuredLaw (gainAttack ? measuredAttackLevel : measuredAttack, p[(std::size_t) param (b, fAttack)]), sr);
                aG[(std::size_t) b] = gainAttack ? coef (measuredLaw (measuredGainAttack, p[(std::size_t) param (b, fAttack)]), sr) : 0.0;
                kG[(std::size_t) b] = gainAttack ? measuredLinear (measuredGainAttackK, p[(std::size_t) param (b, fAttack)]) * 0.11512925464970229 : 0.0;
                aR[(std::size_t) b] = coef (measuredLaw (measuredRelease1, p[(std::size_t) param (b, fRelease)]), sr);
                aR2[(std::size_t) b] = coef (measuredLaw (measuredRelease2, p[(std::size_t) param (b, fRelease)]), sr);
            }
            else
            {
                aA[(std::size_t) b] = coef (attackMs (p[(std::size_t) param (b, fAttack)]), sr);
                aR[(std::size_t) b] = coef (releaseMs (p[(std::size_t) param (b, fRelease)]), sr);
            }
            level[(std::size_t) b] = levelDb (p[(std::size_t) param (b, fLevel)]);
            // the change grows with the level (downward compression, upward expansion) or shrinks (the other two)
            growsWithLevel[(std::size_t) b] = on (p[(std::size_t) param (b, fMode)]) == (rangeDb (p[(std::size_t) param (b, fRange)]) > 0.0);
            enabled[(std::size_t) b] = on (p[(std::size_t) param (b, fEnabled)]) && L.segmentOf[(std::size_t) b] >= 0;
            soloed[(std::size_t) b] = on (p[(std::size_t) param (b, fSolo)]) && L.segmentOf[(std::size_t) b] >= 0;
            anySolo = anySolo || soloed[(std::size_t) b];
            // round 128: a band's own look-ahead (≤ the global one): its gain waits for the difference
            auto bandLa = std::min (laMs, lookaheadMs (p[(std::size_t) bandLookaheadParam (b)]));
            if (rev190 && laMs > 0.0) // round 190lg: the reference's lead (half a period of the band's centre + its own)
            {
                bandLa = mbLeadMs (L.loHz[(std::size_t) b], L.hiHz[(std::size_t) b], lookaheadMs (p[(std::size_t) bandLookaheadParam (b)]));
#ifdef LYNX_MBLAB
                bandLa = std::clamp (bandLa * mbLab().lead, 0.0, laMs);
#endif
            }
            gainDelay[(std::size_t) b].setLength (std::max (0, laSamples - (int) std::lround (bandLa * 0.001 * sr)));
            mb[(std::size_t) b].on = false;
            if (rev190)
                setupRevision1 (p, b, laMs > 0.0 ? bandLa : 0.0);
            gainDelayB[(std::size_t) b].setLength (gainDelay[(std::size_t) b].len);
            // round 147: the stereo link and the free side-chain filter (the expert panel)
            const auto lv = p[(std::size_t) (bandLink0 + b)];
            linkA[(std::size_t) b] = linkAmount (lv);
            linkM[(std::size_t) b] = linkOnly (lv);
            linkSide[(std::size_t) b] = on (p[(std::size_t) (bandLinkMode0 + b)]);
            fullLink[(std::size_t) b] = linkA[(std::size_t) b] >= 1.0 && linkM[(std::size_t) b] <= 0.0;
            // the band's gains act on left / right, or on mid / side (the pan mode; a mid- / side-only link)
            bandMs[(std::size_t) b] = linkM[(std::size_t) b] > 0.0 ? true : (linkA[(std::size_t) b] < 1.0 ? false : msPan);
            if (bandMs[(std::size_t) b] == msPan)
            {
                panA[(std::size_t) b] = panL[(std::size_t) b];
                panB[(std::size_t) b] = panR[(std::size_t) b];
            }
            else // the pan's two gains belong to the other pair: their mean (a rare combination)
                panA[(std::size_t) b] = panB[(std::size_t) b] = 0.5f * (panL[(std::size_t) b] + panR[(std::size_t) b]);
            scFree[(std::size_t) b] = on (p[(std::size_t) (bandScFree0 + b)]);
            if (scFree[(std::size_t) b])
                scFilter[(std::size_t) b].design (sr, scFilterHz (p[(std::size_t) (bandScLow0 + b)]), scFilterHz (p[(std::size_t) (bandScHigh0 + b)]), measuredDet);
        }

        // 1. the split, the detectors and the gains at the base rate; the dry signal, the units and the gains are kept
        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i] * inGain, xr = r[i] * inGain;
            float bl[maxSegments], br[maxSegments], sl0[maxSegments], sr0[maxSegments];
            float dl = xl, dr = xr;
            if (freeLin)
            {
                // round 150: each band's FIR (its own edges and slopes), the gaps pass with the dry signal
                linDry.process (dl, dr);
                for (int b = 0; b < nb; ++b)
                {
                    if (L.segmentOf[(std::size_t) b] < 0)
                    {
                        bl[b] = br[b] = 0.0f;
                        continue;
                    }
                    float a = xl, c = xr;
                    lin[(std::size_t) b].step (a, c);
                    bl[b] = a;
                    br[b] = c;
                }
            }
            else if (linear)
            {
                // round 144: FIR bands (zero phase, delayed by the convolvers' latency); the top band is the rest, so
                // the bands always add up to the delayed input
                linDry.process (dl, dr);
                float restL = dl, restR = dr;
                for (int k = 0; k < ns - 1; ++k)
                {
                    float a = xl, c = xr;
                    lin[(std::size_t) k].step (a, c);
                    bl[k] = a;
                    br[k] = c;
                    restL -= a;
                    restR -= c;
                }
                bl[ns - 1] = restL;
                br[ns - 1] = restR;
            }
            else
            {
                // the crossover tree: each split takes the high part of the one before
                float hl = xl, hr = xr;
                for (int k = 0; k < ns - 1; ++k)
                {
                    float nl, nr;
                    split[(std::size_t) k].run (hl, 0, bl[k], nl);
                    split[(std::size_t) k].run (hr, 1, br[k], nr);
                    hl = nl;
                    hr = nr;
                }
                bl[ns - 1] = hl;
                br[ns - 1] = hr;
            }
            // round 147: what a free side-chain filter hears — the input (in time with the bands) or the side chain
            float srcL = linear ? dl : xl, srcR = linear ? dr : xr, extL = srcL, extR = srcR;
            if (external)
            {
                float el = scl[i] * inGain, er = scr[i] * inGain;
                if (linear)
                    linSc.process (el, er); // the detectors stay in time with the delayed bands
                extL = el;
                extR = er;
                for (int k = 0; k < ns - 1; ++k)
                {
                    float nl, nr;
                    scSplit[(std::size_t) k].run (el, 0, sl0[k], nl);
                    scSplit[(std::size_t) k].run (er, 1, sr0[k], nr);
                    el = nl;
                    er = nr;
                }
                sl0[ns - 1] = el;
                sr0[ns - 1] = er;
            }

            delay.process (dl, dr); // the look-ahead: the audio waits, the detectors don't
            dry[0][(std::size_t) i] = dl;
            dry[1][(std::size_t) i] = dr;
            for (int b = 0; b < nb; ++b)
            {
                float g = 1.0f, g2 = 1.0f;
                if (! enabled[(std::size_t) b] && b == auditionNow && L.segmentOf[(std::size_t) b] >= 0)
                {
                    // [EXEC] a switched-off band can still be auditioned: the same signals, its detector stays idle
                    const auto unit = freeLin ? b : L.segmentOf[(std::size_t) b];
                    float cl = bandExt[(std::size_t) b] ? sl0[L.segmentOf[(std::size_t) b]] : bl[unit];
                    float cr = bandExt[(std::size_t) b] ? sr0[L.segmentOf[(std::size_t) b]] : br[unit];
                    if (scFree[(std::size_t) b])
                    {
                        cl = bandExt[(std::size_t) b] ? extL : srcL;
                        cr = bandExt[(std::size_t) b] ? extR : srcR;
                        scFilter[(std::size_t) b].run (cl, cr);
                    }
                    auditionDelay.process (cl, cr);
                    auditionBuffer[0][(std::size_t) i] = cl;
                    auditionBuffer[1][(std::size_t) i] = cr;
                    auditionValid = true;
                }
                if (enabled[(std::size_t) b])
                {
                    const auto unit = freeLin ? b : L.segmentOf[(std::size_t) b];
                    const auto seg = L.segmentOf[(std::size_t) b];
                    const bool ext = bandExt[(std::size_t) b];
                    // the detector's two channels: the band (split), or the free side-chain filter (round 147)
                    float cl, cr;
                    if (scFree[(std::size_t) b])
                    {
                        cl = ext ? extL : srcL;
                        cr = ext ? extR : srcR;
                        scFilter[(std::size_t) b].run (cl, cr);
                    }
                    else
                    {
                        cl = ext ? sl0[seg] : bl[unit];
                        cr = ext ? sr0[seg] : br[unit];
                    }
                    if (b == auditionNow)
                    {
                        float al = cl, ar = cr;
                        auditionDelay.process (al, ar); // in time with the audio it replaces
                        auditionBuffer[0][(std::size_t) i] = al;
                        auditionBuffer[1][(std::size_t) i] = ar;
                        auditionValid = true;
                    }
                    detectorPeak[(std::size_t) b] = std::max (detectorPeak[(std::size_t) b], (double) std::max (std::abs (cl), std::abs (cr)));
                    const auto follow = [&] (double& e, double& hold, double m) noexcept {
                        if (mb[(std::size_t) b].on) // round 190lg: revision 1
                        {
                            const auto gdb = runRevision1 (mb[(std::size_t) b], mbDet[(std::size_t) b][&e == &env2[(std::size_t) b] ? 1 : 0], m);
                            if (std::abs (gdb) > std::abs (extreme[(std::size_t) b]))
                                extreme[(std::size_t) b] = gdb;
                            return gdb;
                        }
                        const auto x = std::max (-120.0, 20.0 * std::log10 (m + 1e-12));
                        e = x > e ? aA[(std::size_t) b] * e + (1.0 - aA[(std::size_t) b]) * x
                                  : aR[(std::size_t) b] * e + (1.0 - aR[(std::size_t) b]) * x;
                        auto gdb = bandCurve (p, b, e, measuredDet);
                        if (measuredDet)
                        {
                            // round 150: the gain's own release after the curve and the range (the reference's
                            // immediate recovery); what a rising level does goes through at once (a compressor's
                            // growing reduction, an expander's opening)
                            if (growsWithLevel[(std::size_t) b] ? std::abs (gdb) >= std::abs (hold) : std::abs (gdb) <= std::abs (hold))
                            {
                                // round 152: the attack (instant before), in the 10^(k·|gain| / 20) domain
                                const auto a = aG[(std::size_t) b], k = kG[(std::size_t) b];
                                if (k > 1e-6)
                                    hold = std::copysign (std::log (a * std::exp (k * std::abs (hold)) + (1.0 - a) * std::exp (k * std::abs (gdb))) / k, gdb);
                                else
                                    hold = a * hold + (1.0 - a) * gdb;
                            }
                            else
                                hold = aR2[(std::size_t) b] * hold + (1.0 - aR2[(std::size_t) b]) * gdb;
                            gdb = hold;
                        }
                        if (std::abs (gdb) > std::abs (extreme[(std::size_t) b]))
                            extreme[(std::size_t) b] = gdb;
                        return gdb;
                    };
                    const auto peak = (double) std::max (std::abs (cl), std::abs (cr));
                    const auto lev = level[(std::size_t) b];
                    if (fullLink[(std::size_t) b]) // 100 % linked: one detector on the louder channel (the old engine)
                        g = g2 = (float) dbToGain (follow (env[(std::size_t) b], held[(std::size_t) b], peak) + lev);
                    else if (linkM[(std::size_t) b] <= 0.0)
                    {
                        // [EXEC] partly linked: each channel's detector hears its own level moved towards the louder one
                        const auto k = linkA[(std::size_t) b];
                        g = (float) dbToGain (follow (env[(std::size_t) b], held[(std::size_t) b], (1.0 - k) * std::abs (cl) + k * peak) + lev);
                        g2 = (float) dbToGain (follow (env2[(std::size_t) b], held2[(std::size_t) b], (1.0 - k) * std::abs (cr) + k * peak) + lev);
                    }
                    else
                    {
                        // [EXEC] 100 % + mid- (side-) only: the mid's (side's) detector moves from the louder channel to the mid
                        // (side) itself, the other part's gain from the linked one to unity
                        const auto u = linkM[(std::size_t) b];
                        const auto one = std::abs (0.5 * ((double) cl + (linkSide[(std::size_t) b] ? -(double) cr : (double) cr)));
                        const auto gOne = dbToGain (follow (env2[(std::size_t) b], held2[(std::size_t) b], (1.0 - u) * peak + u * one));
                        const auto gLinked = dbToGain (follow (env[(std::size_t) b], held[(std::size_t) b], peak));
                        const auto gOther = (1.0 - u) * gLinked + u;
                        const auto lg = dbToGain (lev);
                        g = (float) ((linkSide[(std::size_t) b] ? gOther : gOne) * lg);  // mid
                        g2 = (float) ((linkSide[(std::size_t) b] ? gOne : gOther) * lg); // side
                    }
                }
                gain[(std::size_t) b][(std::size_t) i] = gainDelay[(std::size_t) b].process (g);
                gainB[(std::size_t) b][(std::size_t) i] = gainDelayB[(std::size_t) b].process (g2);
            }
            for (int u = 0; u < nu; ++u)
            {
                float sl = bl[u], sr2 = br[u];
                if (minimum) // round 144: the segment passes the all-passes of the crossovers above it
                    for (int j = u + 1; j < ns - 1; ++j)
                    {
                        sl = comp[(std::size_t) u][(std::size_t) j].allpass (sl, 0);
                        sr2 = comp[(std::size_t) u][(std::size_t) j].allpass (sr2, 1);
                    }
                bandDelay[(std::size_t) u].process (sl, sr2);
                band[(std::size_t) u][0][(std::size_t) i] = sl;
                band[(std::size_t) u][1][(std::size_t) i] = sr2;
            }
        }

        // round 147: Dynamic phase's measured engine — the (look-ahead delayed) input through each working band's
        // minimum-phase filter in cascade (core/MinPhaseBand.h); soloed bands are still listened to through the split
        if (md == Phase::dynamic && measuredDynamic (p) && ! anySolo)
        {
            if (L.loHz != dynLo || L.hiHz != dynHi || L.loSlope != dynLoS || L.hiSlope != dynHiS || nb != dynNb || osFactor != dynOs || bandMs != dynMs)
            {
                // a new layout: every filter is designed again (their states are kept: no click)
                dynLo = L.loHz;
                dynHi = L.hiHz;
                dynLoS = L.loSlope;
                dynHiS = L.hiSlope;
                dynNb = nb;
                dynOs = osFactor;
                dynMs = bandMs;
                for (auto& g2 : dynGain)
                    g2.fill (-1.0);
            }
            std::array<bool, bands> works {};
            for (int b = 0; b < nb; ++b)
            {
                const auto i0 = (std::size_t) param (b, 0);
                const bool w = enabled[(std::size_t) b]
                               && (std::abs (rangeDb (p[i0 + fRange])) >= 0.01 || level[(std::size_t) b] != 0.0
                                   || panL[(std::size_t) b] != 1.0f || panR[(std::size_t) b] != 1.0f);
                if (w && ! dynWorks[(std::size_t) b])
                {
                    for (auto& fl : dynBand[(std::size_t) b])
                        fl.reset();
                    dynGain[(std::size_t) b].fill (-1.0);
                }
                works[(std::size_t) b] = dynWorks[(std::size_t) b] = w;
            }
            for (int b = nb; b < bands; ++b)
                dynWorks[(std::size_t) b] = false;
            std::array<int, bands> codeL {}, codeH {};
            for (int b = 0; b < nb; ++b)
            {
                codeL[(std::size_t) b] = minphase::dynamicSideCode (L.loSlope[(std::size_t) b]);
                codeH[(std::size_t) b] = minphase::dynamicSideCode (L.hiSlope[(std::size_t) b]);
            }
            const bool narrow152 = on (p[rev152]); // round 152: the measured narrow-band law
            const auto run = [&] (int len, double rate, int every, const float* d0, const float* d1, auto&& gainAt, float* o0, float* o1) noexcept {
                std::array<double, bands> wl {}, wh {};
                for (int b = 0; b < nb; ++b)
                {
                    const auto lo = L.loHz[(std::size_t) b], hi = L.hiHz[(std::size_t) b];
                    wl[(std::size_t) b] = lo > 0.0 ? minphase::warp (std::min (lo, sr * 0.45), rate) : 0.0;
                    wh[(std::size_t) b] = hi > 0.0 ? minphase::warp (std::min (hi, sr * 0.45), rate) : 0.0;
                }
                for (int i = 0; i < len; ++i)
                {
                    if (i % every == 0)
                        for (int b = 0; b < nb; ++b)
                        {
                            if (! works[(std::size_t) b])
                                continue;
                            const auto [ga, gb] = gainAt (b, i);
                            const double gc[2] { (double) ga * panA[(std::size_t) b], (double) gb * panB[(std::size_t) b] };
                            for (int c = 0; c < 2; ++c)
                                if (std::abs (gc[c] - dynGain[(std::size_t) b][(std::size_t) c]) > 1e-7)
                                {
                                    dynGain[(std::size_t) b][(std::size_t) c] = gc[c];
                                    dynBand[(std::size_t) b][(std::size_t) c].designM (wl[(std::size_t) b], wh[(std::size_t) b],
                                                                                       codeL[(std::size_t) b], codeH[(std::size_t) b], std::max (gc[c], 1e-6), narrow152);
                                }
                        }
                    const double xl = d0[i], xr = d1[i];
                    // each band's two gains act on left / right, or on mid / side (the pan mode, a mid- / side-only link)
                    double yl = xl, yr = xr;
                    for (int b = 0; b < nb; ++b)
                        if (works[(std::size_t) b])
                        {
                            if (bandMs[(std::size_t) b])
                            {
                                const auto a = dynBand[(std::size_t) b][0].process (0.5 * (yl + yr));
                                const auto c = dynBand[(std::size_t) b][1].process (0.5 * (yl - yr));
                                yl = a + c;
                                yr = a - c;
                            }
                            else
                            {
                                yl = dynBand[(std::size_t) b][0].process (yl);
                                yr = dynBand[(std::size_t) b][1].process (yr);
                            }
                        }
                    o0[i] = (float) ((xl + (yl - xl) * mx) * outGain);
                    o1[i] = (float) ((xr + (yr - xr) * mx) * outGain);
                }
            };
            if (osFactor <= 1)
            {
                run (n, sr, 8, dry[0].data(), dry[1].data(),
                     [&] (int b, int i) { return std::pair<float, float> { gain[(std::size_t) b][(std::size_t) i], gainB[(std::size_t) b][(std::size_t) i] }; }, l, r);
                return;
            }
            // oversampled: the input and the gains interpolated (the same half-band filters), the filters designed at
            // the high rate, the result decimated back
            const int hi = n * osFactor;
            const auto upsample = [&] (mixsuite::Oversampler& os, const float* src, float* dst) noexcept {
                std::array<float, chunk> tmp {};
                std::copy (src, src + n, tmp.begin());
                int k = 0;
                os.process (tmp.data(), n, [&] (float v) { dst[k++] = v; return 0.0f; });
            };
            for (int c = 0; c < 2; ++c)
                upsample (upDry[(std::size_t) c], dry[(std::size_t) c].data(), hiDry[(std::size_t) c].data());
            for (int b = 0; b < nb; ++b)
            {
                upsample (upGain[(std::size_t) b], gain[(std::size_t) b].data(), hiGain[(std::size_t) b].data());
                upsample (upGainB[(std::size_t) b], gainB[(std::size_t) b].data(), hiGainB[(std::size_t) b].data());
            }
            run (hi, sr * osFactor, 8 * osFactor, hiDry[0].data(), hiDry[1].data(),
                 [&] (int b, int k) { return std::pair<float, float> { hiGain[(std::size_t) b][(std::size_t) k], hiGainB[(std::size_t) b][(std::size_t) k] }; },
                 hiDry[0].data(), hiDry[1].data());
            for (int c = 0; c < 2; ++c)
            {
                auto* out = c == 0 ? l : r;
                const auto* src = hiDry[(std::size_t) c].data();
                std::fill (out, out + n, 0.0f);
                int k = 0;
                down[(std::size_t) c].process (out, n, [&] (float) { return src[k++]; });
            }
            return;
        }

        // 2. the units' gains applied and summed (at the oversampled rate when oversampling is on)
        const auto combine = [&] (float dl, float dr, auto&& unitAt, auto&& gainAt, float& outL, float& outR) noexcept {
            float yl = anySolo || minimum ? 0.0f : dl, yr = anySolo || minimum ? 0.0f : dr;
            for (int u = 0; u < nu; ++u)
            {
                const auto b = unitBand[(std::size_t) u];
                float sl, sr2;
                unitAt (u, sl, sr2);
                if (b < 0) // a gap (round 150): unity — only the minimum-phase sum needs it
                {
                    if (minimum && ! anySolo)
                    {
                        yl += sl;
                        yr += sr2;
                    }
                    continue;
                }
                const auto [ga, gb] = gainAt (b);
                const auto gl = ga * panA[(std::size_t) b], gr = gb * panB[(std::size_t) b];
                if (bandMs[(std::size_t) b]) // round 144: the pan's two gains on the band's mid and side (round 147: or the link's)
                {
                    const auto mid = 0.5f * (sl + sr2), side = 0.5f * (sl - sr2);
                    if (anySolo && ! soloed[(std::size_t) b])
                        continue;
                    if (anySolo || minimum)
                    {
                        yl += gl * mid + gr * side;
                        yr += gl * mid - gr * side;
                    }
                    else if (gl != 1.0f || gr != 1.0f)
                    {
                        yl += (gl - 1.0f) * mid + (gr - 1.0f) * side;
                        yr += (gl - 1.0f) * mid - (gr - 1.0f) * side;
                    }
                    continue;
                }
                if (anySolo)
                {
                    if (soloed[(std::size_t) b])
                    {
                        yl += gl * sl;
                        yr += gr * sr2;
                    }
                }
                else if (minimum)
                {
                    yl += gl * sl;
                    yr += gr * sr2;
                }
                else if (gl != 1.0f || gr != 1.0f)
                {
                    yl += (gl - 1.0f) * sl;
                    yr += (gr - 1.0f) * sr2;
                }
            }
            outL = (dl + (yl - dl) * mx) * outGain;
            outR = (dr + (yr - dr) * mx) * outGain;
        };

        if (osFactor <= 1)
        {
            for (int i = 0; i < n; ++i)
                combine (dry[0][(std::size_t) i], dry[1][(std::size_t) i],
                         [&] (int u, float& a, float& c) { a = band[(std::size_t) u][0][(std::size_t) i]; c = band[(std::size_t) u][1][(std::size_t) i]; },
                         [&] (int b) { return std::pair<float, float> { gain[(std::size_t) b][(std::size_t) i], gainB[(std::size_t) b][(std::size_t) i] }; }, l[i], r[i]);
            return;
        }
        // round 144: oversampling — every stream is interpolated by the same half-band filters (mixsuite::Oversampler:
        // only its upsampling half is used here), the gains are applied at the high rate (no aliasing from fast gain
        // changes), and one more Oversampler's decimating half brings the sum back
        const int hi = n * osFactor;
        const auto upsample = [&] (mixsuite::Oversampler& os, const float* src, float* dst) noexcept {
            std::array<float, chunk> tmp {};
            std::copy (src, src + n, tmp.begin());
            int k = 0;
            os.process (tmp.data(), n, [&] (float s) { dst[k++] = s; return 0.0f; });
        };
        for (int c = 0; c < 2; ++c)
            upsample (upDry[(std::size_t) c], dry[(std::size_t) c].data(), hiDry[(std::size_t) c].data());
        for (int u = 0; u < nu; ++u)
            for (int c = 0; c < 2; ++c)
                upsample (upBand[(std::size_t) u][(std::size_t) c], band[(std::size_t) u][(std::size_t) c].data(), hiBand[(std::size_t) u][(std::size_t) c].data());
        for (int b = 0; b < nb; ++b)
        {
            upsample (upGain[(std::size_t) b], gain[(std::size_t) b].data(), hiGain[(std::size_t) b].data());
            upsample (upGainB[(std::size_t) b], gainB[(std::size_t) b].data(), hiGainB[(std::size_t) b].data());
        }
        for (int k = 0; k < hi; ++k)
            combine (hiDry[0][(std::size_t) k], hiDry[1][(std::size_t) k],
                     [&] (int u, float& a, float& c) { a = hiBand[(std::size_t) u][0][(std::size_t) k]; c = hiBand[(std::size_t) u][1][(std::size_t) k]; },
                     [&] (int b) { return std::pair<float, float> { hiGain[(std::size_t) b][(std::size_t) k], hiGainB[(std::size_t) b][(std::size_t) k] }; },
                     hiDry[0][(std::size_t) k], hiDry[1][(std::size_t) k]);
        for (int c = 0; c < 2; ++c)
        {
            auto* out = c == 0 ? l : r;
            const auto* src = hiDry[(std::size_t) c].data();
            std::fill (out, out + n, 0.0f);
            int k = 0;
            down[(std::size_t) c].process (out, n, [&] (float) { return src[k++]; });
        }
    }

    // what the linear-phase kernels depend on (the layout, and in the free layout the bands' own edges and slopes)
    std::array<double, 4 * bands + 2> kernelKey (const Layout& L, bool freeLin, int nb) const noexcept
    {
        std::array<double, 4 * bands + 2> k {};
        k[0] = freeLin ? 1.0 : 0.0;
        k[1] = freeLin ? (double) nb : (double) L.segments;
        if (freeLin)
            for (int b = 0; b < bands; ++b)
            {
                const bool use = b < nb && L.segmentOf[(std::size_t) b] >= 0;
                k[(std::size_t) (2 + 4 * b)] = use ? L.loHz[(std::size_t) b] : -1.0;
                k[(std::size_t) (3 + 4 * b)] = use ? L.hiHz[(std::size_t) b] : -1.0;
                k[(std::size_t) (4 + 4 * b)] = use ? L.loSlope[(std::size_t) b] : -1.0;
                k[(std::size_t) (5 + 4 * b)] = use ? L.hiSlope[(std::size_t) b] : -1.0;
            }
        return k;
    }

    // the FIR of each segment below the top one: the magnitude of the segment in the IIR tree (the same crossovers),
    // zero phase; round tp172b7: each crossover read on the analog axis (analogBin), as the reference's Linear phase
    void designKernels (const Layout& L, int nb) noexcept
    {
        kernelsDirty = false;
        if (linSize <= 0)
            return;
        const auto bins = linSize / 2 + 1;
        for (int k = 0; k < nb - 1; ++k)
        {
            auto& t = linTable[(std::size_t) k];
            for (int i = 0; i < bins; ++i)
            {
                const auto w = pi * (double) i / (double) (bins - 1);
                double m = 1.0;
                for (int j = 0; j < k; ++j)
                    m *= std::abs (split[(std::size_t) j].response (true, analogBin (w, L.xoverHz[(std::size_t) j], sr)));
                m *= std::abs (split[(std::size_t) k].response (false, analogBin (w, L.xoverHz[(std::size_t) k], sr)));
                t[(std::size_t) i] = (float) m;
            }
            lin[(std::size_t) k].setKernel (t);
        }
    }

    // round 150: the free layout's FIRs — each band's own shape (Pro-MB's 1 / (1 + x^m), continuous slopes)
    void designFreeKernels (const Layout& L, int nb) noexcept
    {
        kernelsDirty = false;
        if (linSize <= 0)
            return;
        const auto bins = linSize / 2 + 1;
        for (int b = 0; b < nb; ++b)
        {
            if (L.segmentOf[(std::size_t) b] < 0)
                continue;
            // round tp172b7: the analog axis (Hz), as the reference's Linear phase
            const auto wl = std::max (L.loHz[(std::size_t) b], 0.0);
            const auto wh = std::max (L.hiHz[(std::size_t) b], 0.0);
            auto& t = linTable[(std::size_t) b];
            for (int i = 0; i < bins; ++i)
            {
                const auto w = 0.5 * sr * (double) i / (double) (bins - 1);
                t[(std::size_t) i] = (float) linearShare (wl, wh, L.loSlope[(std::size_t) b] / 6.0, L.hiSlope[(std::size_t) b] / 6.0, w);
            }
            lin[(std::size_t) b].setKernel (t);
        }
    }

    void resetOversamplers() noexcept
    {
        for (auto& o : upDry) o.reset();
        for (auto& row : upBand)
            for (auto& o : row) o.reset();
        for (auto& o : upGain) o.reset();
        for (auto& o : upGainB) o.reset();
        for (auto& o : down) o.reset();
    }

    // round 190lg (REA_rodada190lg §7): revision 1's settings of band b; `leadMs` = its detector lead (0: no look-ahead)
    void setupRevision1 (const EffectParams& p, int b, double leadMs) noexcept
    {
        const auto at = [&] (int f) { return p[(std::size_t) param (b, f)]; };
        auto& m = mb[(std::size_t) b];
        const auto rng = rangeDb (at (fRange));
        const bool expand = on (at (fMode));
        if (std::abs (rng) < 0.01)
            return;
        // a downward compressor or an upward expander runs the graph on the detector's reduction; an upward compressor
        // or a downward expander on 36 dB − it (the detector then reads how far the level is under the threshold)
        m.inverted = expand != (rng > 0.0);
#ifdef LYNX_MBLAB
        if (m.inverted && mbLab().inv < 0.5)
            return;
#endif
        m.on = true;
        m.sign = rng > 0.0 ? 1.0 : -1.0;
        m.slope = ratioOf (at (fRatio)) - 1.0;
        m.scale = std::abs (rng) / 36.0;
        m.thr = thresholdDb (at (fThreshold));
        m.squeeze = isInfiniteRatio (at (fRatio)) ? 1.0 : 1.0 - 1.0 / ratioOf (at (fRatio));
        m.knee = kneeDb (at (fKnee));
        m.range = std::abs (rng);
        m.soft = 0.6 * m.knee + 12.0;
        const auto a = 100.0 * (double) std::clamp (at (fAttack), 0.0f, 1.0f); // the reference's knobs, 0 … 100 %
        const auto r = 100.0 * (double) std::clamp (at (fRelease), 0.0f, 1.0f);
        const auto rel = mbReleaseS (at (fRelease));
        const auto slow = 1.0 - std::min (1.0, 0.05 * a); // 1 at attack 0 %, 0 from 20 %
        auto holdMs = (leadMs > 0.0 ? 1.1 * leadMs : 0.11) + (r + 2.0) * slow;
        const auto lpHz = 650.0 / (leadMs > 0.0 ? leadMs : 0.1);
        m.fa = {};
        m.fa.att = slow * 0.5 + 0.12;
        m.fa.rel = (rel + 1.0) * 0.25;
        m.fb = {};
        m.fb.att = 6.0e-9 * a * a * a * a + 0.00025;
        m.fb.rel = rel;
        m.fb.p5 = 1.0 - 0.7 * slow;
        m.fb.p6 = 0.1 * rel;
        m.rel = rel;
        m.relA = m.inverted ? 0.007 : 0.0; // follower B's release grows with follower A (s per dB)
        m.lpOrder = 2;
#ifdef LYNX_MBLAB
        const auto& lab = mbLab();
        m.fa.att *= lab.aAtk;
        m.fa.rel *= lab.aRel;
        m.fb.att *= lab.bAtk;
        m.fb.rel *= lab.bRel;
        m.rel *= lab.bRel;
        m.fb.p6 *= lab.p6;
        holdMs *= lab.hold;
        m.relA *= lab.invW;
        if (lab.invSlope > 0.5) // the slope read as the gain's own (ratio − 1 per dB under the threshold)
            m.slope /= m.scale;
        m.lpOrder = (int) lab.lpOrder;
        m.soft *= lab.soft;
        if (lab.oldSoft > 0.5)
            m.soft = std::min (12.0, 2.0 * m.range);
#endif
        m.holdN = (int) std::lround (holdMs * 0.001 * sr);
        const auto fc = std::min (lpHz, 0.45 * sr);
        if (m.lpOrder == 1)
            m.lp[0] = 1.0 - std::exp (-2.0 * pi * fc / sr);
        else if (m.lpOrder >= 2)
        {
            // two-pole Butterworth (bilinear)
            const auto k = std::tan (pi * fc / sr), q = 0.7071067811865476, n = 1.0 / (1.0 + k / q + k * k);
            m.lp[0] = k * k * n;
            m.lp[1] = 2.0 * m.lp[0];
            m.lp[2] = m.lp[0];
            m.lp[3] = 2.0 * (k * k - 1.0) * n;
            m.lp[4] = (1.0 - k / q + k * k) * n;
        }
    }

    // round 190lg: one sample of revision 1's graph for a detector channel reading magnitude x; returns the gain (dB)
    double runRevision1 (MbBand& m, MbDetector& d, double x) noexcept
    {
        const auto level = std::max (-180.0, 20.0 * std::log10 (x + 1e-30));
        auto g = m.inverted ? 36.0 - mbBelow (m.thr - level, m.slope, m.knee)
                            : mbReduction (level - m.thr, m.squeeze, m.knee, m.range, m.soft);
        g = d.hold.push (g, m.holdN);
        if (m.lpOrder == 1)
            g = d.lp1 += (g - d.lp1) * m.lp[0];
        else if (m.lpOrder >= 2)
        {
            const auto y = m.lp[0] * g + d.lp1;
            d.lp1 = m.lp[1] * g - m.lp[3] * y + d.lp2;
            d.lp2 = m.lp[2] * g - m.lp[4] * y;
            g = y;
        }
        crest::v2Step (d.a, g, m.fa, sr, d.ka);
        if (m.inverted)
            m.fb.rel = m.rel + m.relA * d.a;
        crest::v2Step (d.b, std::max (d.a, g), m.fb, sr, d.kb);
        return m.inverted ? m.sign * m.scale * (36.0 - d.b) : m.sign * d.b;
    }

    void configure (const EffectParams& p) noexcept
    {
        if (c4Active (p)) // round 190mg: the C4 law's own look-ahead
        {
            latency.store (c4Lookahead (sr), std::memory_order_relaxed);
            return;
        }
        // round 144: the processing mode and the oversampling (at most 4x, and only up to 400 kHz)
        const int m = linSize > 0 ? (int) phaseOf (p[processingMode]) : (int) Phase::dynamic;
        int factor = oversamplingOf (p[oversampling]);
        while (factor > 1 && sr * factor > 400000.0)
            factor /= 2;
        // round 190lg: under revision 1 the reference's switch sets the look-ahead: the audio waits 20 ms, or nothing
        const auto la = on (p[revision]) ? (on (p[lookaheadSwitch]) ? 20.0 : 0.0) : lookaheadMs (p[lookahead]);
        if (la == laMs && m == mode && factor == osFactor)
            return;
        if (m != mode)
        {
            mode = m;
            kernelsDirty = true;
            for (auto& c : lin)
                if (c.n > 0)
                    c.clear();
            linDry.reset();
            linSc.reset();
        }
        if (factor != osFactor)
        {
            osFactor = factor;
            const auto set = [&] (mixsuite::Oversampler& o) { o.setFactor (std::max (2, factor)); o.reset(); };
            for (auto& o : upDry) set (o);
            for (auto& row : upBand)
                for (auto& o : row) set (o);
            for (auto& o : upGain) set (o);
            for (auto& o : upGainB) set (o);
            for (auto& o : down) set (o);
        }
        if (la != laMs)
        {
            laMs = la;
            const auto s = (int) std::lround (la * 0.001 * sr);
            laSamples = s;
            delay.setLength (s);
            auditionDelay.setLength (s);
            for (auto& d : bandDelay)
                d.setLength (s);
        }
        latency.store (laSamples + ((Phase) mode == Phase::linear ? linearLatency (sr) : 0)
                           + (osFactor > 1 ? mixsuite::Oversampler::latency() : 0),
                       std::memory_order_relaxed);
    }

    double sr = 48000.0, laMs = -1.0;
    std::atomic<int> latency { 0 };
    c4::Engine c4e; // round 190mg
    bool c4On = false;
    EffectParams lastParams = [] {
        EffectParams p {};
        const auto& info = prismInfo();
        for (std::size_t i = 0; i < info.params.size(); ++i)
            p[i] = info.params[i].defaultValue;
        return p;
    }();
    // round 150: up to 2·bands + 1 segments (the free layout's gaps), 2·bands crossovers
    std::array<Split, maxSegments - 1> split, scSplit;
    std::array<std::array<Split, maxSegments - 1>, maxSegments - 1> comp; // minimum phase: segment k's copy of split j's all-pass
    std::array<double, maxSegments - 1> lastF {};
    const float* scL = nullptr;
    const float* scR = nullptr;
    std::array<int, maxSegments - 1> lastSlope {};
    std::array<double, 4 * bands + 2> lastKernelKey {};
    bool designed = false, kernelsDirty = true;
    int mode = -1, osFactor = 0, linSize = 0, lastNs = 0;
    std::array<peqfl::FftConvolver, bands> lin; // the split layout: one per segment below the top; free: one per band
    std::array<std::vector<float>, bands> linTable;
    LongDelay linDry, linSc;
    StereoDelay delay, auditionDelay;
    std::array<std::array<float, chunk>, 2> auditionBuffer {};
    bool auditionValid = false;
    std::array<double, bands> detectorPeak {};
    std::array<StereoDelay, maxSegments> bandDelay;
    std::array<MonoDelay, bands> gainDelay, gainDelayB;
    // round 147: the stereo link (linked part, mid- / side-only part, side mode), the free side-chain filters
    std::array<double, bands> linkA {}, linkM {};
    std::array<bool, bands> linkSide {}, fullLink {}, bandMs {}, scFree {};
    std::array<float, bands> panA {}, panB {};
    std::array<ScFilter, bands> scFilter {};
    int laSamples = 0;
    std::array<double, bands> env {}, env2 {}, extreme {};
    std::array<double, bands> held {}, held2 {}; // round 150: the measured detector's gain after its own release
    std::array<MbBand, bands> mb {};                     // round 190lg: revision 1's settings and detector states
    std::array<std::array<MbDetector, 2>, bands> mbDet {};
    std::array<float, bands> gains {};
    // one chunk of the base-rate streams, and the same at the oversampled rate
    std::array<std::array<float, chunk>, 2> dry {};
    std::array<std::array<std::array<float, chunk>, 2>, maxSegments> band {};
    std::array<std::array<float, chunk>, bands> gain {}, gainB {}; // each band's two gains (left / right or mid / side)
    std::array<std::array<float, chunk * maxFactor>, 2> hiDry {};
    std::array<std::array<std::array<float, chunk * maxFactor>, 2>, maxSegments> hiBand {};
    std::array<std::array<float, chunk * maxFactor>, bands> hiGain {}, hiGainB {};
    std::array<mixsuite::Oversampler, 2> upDry, down;
    std::array<std::array<mixsuite::Oversampler, 2>, maxSegments> upBand;
    std::array<mixsuite::Oversampler, bands> upGain, upGainB;
    // round 147: the measured Dynamic phase — each band's two filters (left / right or mid / side), their last gains
    std::array<std::array<minphase::Band, 2>, bands> dynBand {};
    std::array<std::array<double, 2>, bands> dynGain {};
    std::array<bool, bands> dynWorks {};
    std::array<double, bands> dynLo {}, dynHi {}, dynLoS {}, dynHiS {};
    int dynNb = 0, dynOs = 0;
    std::array<bool, bands> dynMs {};
    LiveMeters meters;
};
} // namespace
} // namespace prism

// ================================================================== Sentry Gate

namespace sentry
{
Style styleOf (float v) noexcept { return (Style) pick (v, styles); }
float styleValue (Style s) noexcept { return (float) (int) s / (float) (styles - 1); }
double thresholdDb (float v) noexcept { return lin (v, -60.0, 0.0); }
double ratioOf (float v) noexcept { return ratioLaw (v); }
double rangeDb (float v) noexcept { return lin (v, 0.0, 100.0); }
double attackMs (float v) noexcept { return 1000.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 4.0); }
double releaseMs (float v) noexcept { return 5000.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 3.0); }
double holdMs (float v) noexcept { return 250.0 * (double) v * (double) v; }
double kneeDb (float v) noexcept { return lin (v, 0.0, 30.0); }
double lookaheadMs (float v) noexcept { return lin (v, 0.0, 10.0); }
double filterHz (float v) noexcept { return logMap (v, 5.0, 30000.0); }
float filterValue (double hz) noexcept { return logValue (std::clamp (hz, 5.0, 30000.0), 5.0, 30000.0); }
double upThresholdDb (float v) noexcept { return lin (v, -30.0, 0.0); }
double upRatioOf (float v) noexcept { return lin (v, 1.0, 3.0); }
double scGainDb (float v) noexcept { return lin (v, -36.0, 36.0); }
double mixOf (float v) noexcept { return lin (v, -1.0, 1.0); }
double panOf (float v) noexcept { return lin (v, -1.0, 1.0); }

namespace
{
// style families (fitted to MEASURE_fabfilter §7): where an expander starts under the threshold (divided by
// ratio − 1, so a gate of high ratio opens right at the threshold), the fastest opening, release = base + scale ×
// knob, the share of the hold knob that delays the release
struct StyleDef
{
    double offsetDb, attackMinMs, attackScale, releaseBaseMs, releaseScale, holdScale;
};
constexpr StyleDef styleDefs[styles] {
    { 7.9, 0.0, 0.49, 45.0, 0.10, 0.50 },   // classic
    { 4.3, 0.15, 0.49, 120.0, 0.25, 0.50 }, // clean
    { 0.2, 17.0, 0.49, 100.0, 0.20, 0.50 }, // vocal
    { 0.0, 0.7, 0.49, 20.0, 0.05, 0.60 },   // guitar (its release waits ~110 ms, then falls fast)
    // upward (round tp172b, measured on the reference: the lifted gain falls back slowly — a linear-domain time constant
    // of ~0.3 s at release 10 ms, ~0.7 s at 150 ms, ~3 s at 1 s)
    { 0.0, 0.0, 0.49, 273.0, 2.73, 0.50 },
};
constexpr double guitarWaitMs = 110.0;

// round 190lp, revision 1 (hidden `revision`): the level gate refitted black-box on the reference's renders (refcap030 /
// refcap182 / tp172b; gain trajectories in 1 … 10 ms frames around the tone bursts' onsets and ends). The gain moves in
// dB: it opens with the attack (attackMinMs … attackScale × Attack), waits the hold (holdScale × Hold) plus the style's
// own wait (waitMs + waitRelease × Release), then closes along two cascaded one-pole curves (time releaseBaseMs +
// releaseScale × Release, the second one pole2 × as long: a smooth start, a straight middle in dB and a soft landing on
// the range); attackPole2 likewise smooths the opening
struct GateLaw1
{
    double attackMinMs, attackScale, releaseBaseMs, releaseScale, holdScale, waitMs, pole2, waitRelease = 0.0, attackPole2 = 0.0;
    double slewDb = 0.0; // > 0: the closing falls at most this many dB per release time (straight in dB, then lands)
    double attackLinear = 0.0; // ≥ 0.5: the opening moves in the linear domain (fast through the deep dBs, then settles)
    double rmsMs = 0.0; // > 0: the level is the mean square over this time (× 2: a sine reads its peak), not the peak
    bool own = true;    // false: the style keeps revision 0's measured gate (Upward: its lift already matches)
};
#ifdef LYNX_LGLAB
GateLaw1 gateLaws1[styles] {
#else
constexpr GateLaw1 gateLaws1[styles] {
#endif
    // attack min / scale, release base / scale, hold scale, wait, pole2, wait × release, attack pole2, slew, linear attack
    { 0.0, 0.49, 16.0, 0.3, 0.50, 0.0, 0.5, 0.08, 0.0, 20.0, 1.0 },    // classic: waits 0.08 × release, falls 20 dB per release
    { 0.15, 0.49, 60.0, 0.5, 0.50, 0.0, 0.5, 0.05, 0.0, 10.0, 1.0 },   // clean: the slow fall toward its expansion
    // vocal: the fit (opening along one pole in dB, closing along two) lifted the tones but took refcap182's S3 drums a
    // step down with every variant tried: it keeps revision 0's gate
    { 4.0, 0.49, 66.7, 0.5, 0.50, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, false },
    { 0.25, 0.2, 3.75, 0.4, 0.60, 20.0, 0.0, 2.3, 1.0, 50.0, 0.0 },   // guitar: waits ~2.3 × release, falls 50 dB per release
    { 0.0, 0.49, 273.0, 2.73, 0.50, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false }, // upward: revision 0's lift (it matches)
};
#ifdef LYNX_LGLAB
// lab only: LYNX_LGLAB="g0.wait=30,g3.pole2=1,..." overrides the revision-1 table
struct GateLab
{
    GateLab()
    {
        const char* e = std::getenv ("LYNX_LGLAB");
        if (e == nullptr) return;
        std::string str (e);
        std::size_t pos = 0;
        while (pos < str.size())
        {
            auto end = str.find (',', pos);
            if (end == std::string::npos) end = str.size();
            const auto item = str.substr (pos, end - pos);
            pos = end + 1;
            const auto dot = item.find ('.'), eq = item.find ('=');
            if (dot == std::string::npos || eq == std::string::npos || item[0] != 'g') continue;
            const auto who = item.substr (0, dot), field = item.substr (dot + 1, eq - dot - 1);
            const double v = std::atof (item.c_str() + eq + 1);
            for (int k = 0; k < styles; ++k)
            {
                if (who != "gall" && who != "g" + std::to_string (k)) continue;
                auto& l = gateLaws1[k];
                if (field == "amin") l.attackMinMs = v; else if (field == "ascale") l.attackScale = v;
                else if (field == "rbase") l.releaseBaseMs = v; else if (field == "rscale") l.releaseScale = v;
                else if (field == "hscale") l.holdScale = v; else if (field == "wait") l.waitMs = v;
                else if (field == "pole2") l.pole2 = v; else if (field == "wrel") l.waitRelease = v;
                else if (field == "apole2") l.attackPole2 = v; else if (field == "slew") l.slewDb = v;
                else if (field == "alin") l.attackLinear = v; else if (field == "rms") l.rmsMs = v;
                else if (field == "own") l.own = v >= 0.5;
            }
        }
    }
};
const GateLab gateLab;
#endif

// round 190v [EXEC] (trial190k's noise probes through the reference's Vocal style, threshold −60 so the gate itself
// stays out of it): after a pause, content above about 4 kHz comes back turned down and recovers on its own: white
// noise is 0.375 dB down per dB over −62 dBFS at its onset (−7.6 dB at −42 dBFS, −15 at −20, −21 at −6) and the gain
// climbs back (in linear gain) with a time constant that grows with the noise's amplitude (0.15 s at −42 dBFS, 0.6 s
// at −30, 1.9 s at −20). Sines and noise low-passed at 2 kHz pass untouched; the Range limits it softly (range 10 dB:
// −5.7 dB where it was −11), ratio 2 gives 0.7 of it, a hold of 200 ms keeps it open across a 0.4 s pause; an "s"
// 50 ms after a note, or a hat on a kick's tail, is not touched (it needs the pause). The other styles have none of it.
// Only sound that is mostly above 4 kHz counts (music and voiced notes after a pause pass on the reference), a longer
// hold takes it away, and the deeper of the gate and this reduction applies (they don't add).
namespace vocalNr
{
constexpr double highPassHz = 4000.0, floorDb = -62.8, dbPerDb = 0.375;
constexpr double quietAmp = 0.001;                       // −60 dBFS: under it the input counts as a pause
constexpr double armAfterMs = 60.0, armOverMs = 250.0;   // the pause it takes (plus 1.5 × the hold) and how long to arm fully
constexpr double tauAtRef = 1.9, refAmp = 0.1 * 0.912;   // recovery τ (s) at −20 dBFS white noise (its part above 4 kHz)
constexpr double tauMin = 0.03, tauMax = 10.0;
constexpr int step = 32;                                 // samples per control step
constexpr double noiseShareLow = 0.1, noiseShareHigh = 0.75; // the share of the power above 4 kHz from which it works / works fully
} // namespace vocalNr

// round tp172b [EXEC] (a 1 kHz ramp −60 → 0 → −60 dBFS at 10 dB/s through the reference, threshold −30, ratio 100): an
// open gate closes this far below the point where it opened (Classic −37.3, Clean −41.0, Vocal −37.8, Guitar −31.9
// dBFS, less the release's lag on the ramp). The gap shrinks with the ratio: at ratio 2 the reference opens and closes
// at the same expansion point (−43.9 / −45.9 dB at −6 dB of gain, the rest is the release), hence × (1 − 1 / (ratio − 1)).
constexpr double closeGapDb[styles] { 6.9, 9.9, 6.7, 0.6, 0.0 };

double closeGap (const EffectParams& p) noexcept
{
    const auto st = styleOf (p[style]);
    const auto r = ratioOf (p[ratio]);
    return r <= 2.0 ? 0.0 : closeGapDb[(int) st] * (1.0 - 1.0 / (r - 1.0));
}

// target gain (dB) for a detector level x; `gap`: how far the curve sits below its opening point (an open gate)
double gateCurve (const EffectParams& p, double x, double gap = 0.0) noexcept
{
    const auto st = styleOf (p[style]);
    if (st == Style::upward)
    {
        const auto ut = upThresholdDb (p[upThreshold]);
        // round tp172b [EXEC]: the reference lifts by (ratio − 1) dB per dB above the upward threshold (threshold −12,
        // ratio 1.25 / 1.5 / 2 / 3: +1.5 / +3 / +6 / +12 dB at −6 dBFS), not 1 − 1 / ratio
        return kneeCurve (x - ut, upRatioOf (p[upRatio]) - 1.0, kneeDb (p[knee]));
    }
    const auto r = ratioOf (p[ratio]);
    if (r <= 1.0001)
        return 0.0;
    const auto start = thresholdDb (p[threshold]) - styleDefs[(int) st].offsetDb / std::max (1.0, r - 1.0) - gap;
    return -std::min (rangeDb (p[range]), kneeCurve (start - x, r - 1.0, kneeDb (p[knee])));
}
} // namespace

double hysteresisDb (Style s) noexcept { return styleDefs[(int) s].offsetDb; }

// round 190dm [EXEC] (trial190k's tone / stair / pink / stereo probes through a drum-gate reference, black-box; docs/PARITY.md
// "Round 190dm"): an onset over the threshold opens the gate at once and its gain then follows the same curve whatever the
// input does — a hold of 18.75 ms + 5.6 % of the release, then −slope · u dB (u = time / release) times a fade to silence
// (1 − (u / end)^power) for Mid and Sharp; Smooth is a straight line in dB. The curve is clamped at the range (range 40 and
// 100 run the same curve until −40). Fitted on 6 recoveries (release 52 ms … 3.5 s, the three shapes): rms 0.06 … 0.31 dB.
namespace
{
struct ShapeDef
{
    double slopeDb, end, power;
};
constexpr ShapeDef shapeDefs[3] { { 16.1, 0.0, 1.0 }, { 15.8, 1.45, 3.5 }, { 10.0, 0.86, 2.5 } };
// the transient detector (fitted on the same probes: which onsets open it and which do not): the side chain's first
// difference (normalised to unity at 1 kHz — or at the side chain's low-pass when lower — so a 100 Hz note's onset counts
// 20 dB less and a click or a hat's much more),
// a peak follower (fast) and a slower follower of it (slow); an onset is fast over max (slow, threshold) by more than
// `ratioDb` (the default sensitivity's), re-armed once that falls `hysteresisDb` lower
constexpr double detFastAttackMs = 0.0, detFastReleaseMs = 10.0, detSlowAttackMs = 5.0, detSlowReleaseMs = 50.0;
constexpr double detRatioDb = 8.0, detHysteresisDb = 3.0, detEmphasis = 0.85;
} // namespace

bool transientTrigger (const EffectParams& p) noexcept { return on (p[trigger]); }
double sensitivityOf (float v) noexcept { return lin (std::clamp (v, 0.0f, 1.0f), 1.0, 10.0); }
float sensitivityValue (double s) noexcept { return (float) ((std::clamp (s, 1.0, 10.0) - 1.0) / 9.0); }
Shape shapeOf (float v) noexcept { return (Shape) pick (v, 3); }

double transientCurveDb (Shape s, double tau, double releaseSec) noexcept
{
    if (tau <= 0.0)
        return 0.0;
    const auto& d = shapeDefs[(int) s];
    const auto u = tau / std::max (1.0e-4, releaseSec);
    auto g = -d.slopeDb * u;
    if (d.end > 0.0)
        g += 20.0 * std::log10 (std::max (1.0e-6, 1.0 - std::pow (std::min (1.0, u / d.end), d.power)));
    return std::max (-120.0, g);
}

double staticGainDb (const EffectParams& p, double inputDb, bool)
{
    const auto g = gateCurve (p, inputDb);
    if (on (p[ducking]) && styleOf (p[style]) != Style::upward) // ducking: the gate turned upside down
        return -rangeDb (p[range]) - g;
    return g;
}

namespace
{
class Gate final : public Effect
{
public:
    explicit Gate (const EffectParams* initial)
    {
        if (initial != nullptr)
        {
            // kept for prepare(), which configures again at the real rate: the latency stays the slot's (issue #113)
            lastParams = *initial;
            configure (*initial);
        }
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        laOn = -1;
        osFactor = 0;
        configure (lastParams);
        reset();
    }

    void reset() noexcept override
    {
        delay.reset();
        gainDelay.reset();
        hp.reset();
        lp.reset();
        for (auto& o : os) o.reset();
        osGain.reset();
        level = 0.0;
        gain = 0.0;
        gainDb1 = gainDb2 = -200.0;
        shownDb = -200.0;
        shownGain = 0.0;
        meanSq = 0.0;
        isOpen = false;
        holdLeft = 0;
        nrHp.reset();
        nrMs = nrAll = nrPeak = 0.0;
        nrRecovered = 0.0;
        nrGapMs = 1e9;
        nrTarget = nrGain = 1.0;
        nrCount = 0;
        tFast = tSlow = 0.0;
        tPrev[0] = tPrev[1] = 0.0;
        tSince = -1.0;
        tArmed = true;
    }

    int latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }
    void setSidechain (const float* l, const float* r) noexcept override
    {
        scL = l;
        scR = r;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (n > maxBlock) // the oversampled path works on blocks of at most maxBlock samples
        {
            const auto* sl = scL;
            const auto* sr2 = scR;
            for (int i = 0; i < n; i += maxBlock)
            {
                if (sl != nullptr && sr2 != nullptr) { scL = sl + i; scR = sr2 + i; }
                processBlock (l + i, r + i, std::min (maxBlock, n - i), p, bpm);
            }
            scL = scR = nullptr;
            return;
        }
        processBlock (l, r, n, p, bpm);
    }

private:
    void processBlock (float* l, float* r, int n, const EffectParams& p, double) noexcept
    {
        lastParams = p;
        configure (p);
        const auto inPeak = blockPeak (l, r, n);
        const auto st = styleOf (p[style]);
        const auto& sd = styleDefs[(int) st];
        const auto hpHz = filterHz (p[highPass]), lpHz = filterHz (p[lowPass]);
        const bool hpOn = hpHz > 6.0, lpOn = lpHz < std::min (29000.0, sr * 0.45);
        if (hpOn)
            hp.filter (sr, 1, std::min (hpHz, sr * 0.45), 0.70710678);
        if (lpOn)
            lp.filter (sr, 0, std::min (lpHz, sr * 0.45), 0.70710678);
        const bool external = on (p[scExternal]) && scL != nullptr && scR != nullptr;
        const bool listen = on (p[scListen]);
        // round 128: input level, the expert side-chain gain / mix per detector channel (mid / side in M/S mode),
        // ducking, wet / dry pan
        const auto inG = (float) dbToGain (lin (p[input], -24.0, 24.0));
        const bool msMode = on (p[channelMode]);
        const auto gL = (float) dbToGain (scGainDb (p[scGainL])), gR = (float) dbToGain (scGainDb (p[scGainR]));
        const auto mL = (float) mixOf (p[scMixL]), mR = (float) mixOf (p[scMixR]);
        const bool expertSc = msMode || gL != 1.0f || gR != 1.0f || mL != 0.0f || mR != 0.0f;
        const bool duck = on (p[ducking]) && st != Style::upward;
        const auto duckFloor = dbToGain (-rangeDb (p[range]));
        const auto balance = [] (float pan, float& a, float& b) {
            if (pan > 0.0f) a = 1.0f - pan;
            else if (pan < 0.0f) b = 1.0f + pan;
        };
        float wetA = 1.0f, wetB = 1.0f, dryA = 1.0f, dryB = 1.0f;
        balance ((float) panOf (p[wetPan]), wetA, wetB);
        balance ((float) panOf (p[dryPan]), dryA, dryB);
        const bool panned = wetA != 1.0f || wetB != 1.0f || dryA != 1.0f || dryB != 1.0f;
        if (inG != 1.0f)
            for (int i = 0; i < n; ++i)
            {
                l[i] *= inG;
                r[i] *= inG;
            }
        const auto attMs = std::max (sd.attackMinMs, attackMs (p[attack]) * sd.attackScale);
        const auto aA = coef (attMs, sr);
        const auto aR = coef (sd.releaseBaseMs + sd.releaseScale * releaseMs (p[release]), sr);
        const auto holdSamples = (int) (holdMs (p[hold]) * sd.holdScale * 0.001 * sr)
                                 + (st == Style::guitar ? (int) (guitarWaitMs * 0.001 * sr) : 0);
        const auto levelRel = coef (5.0, sr);
        // round 190lp: revision 1 (hidden) — the gain moves in dB along the style's own curves (gateLaws1)
        const auto& g1 = gateLaws1[(int) st];
        const bool rev1 = p[revision] >= 0.5f && g1.own;
        const auto kA1 = rev1 ? 1.0 - coef (std::max (g1.attackMinMs, attackMs (p[attack]) * g1.attackScale), sr) : 0.0; // (one pole)
        const auto relMs1 = g1.releaseBaseMs + g1.releaseScale * releaseMs (p[release]);
        const auto kR1 = rev1 ? 1.0 - coef (relMs1, sr) : 0.0;
        const auto kR2 = rev1 ? (g1.pole2 > 0.0 ? 1.0 - coef (relMs1 * g1.pole2, sr) : 1.0) : 0.0;
        const bool attLin1 = rev1 && g1.attackLinear >= 0.5;
        const bool rms1 = rev1 && g1.rmsMs > 0.0;
        const auto kRms1 = rms1 ? coef (g1.rmsMs, sr) : 0.0;
        const auto slew1 = rev1 && g1.slewDb > 0.0 ? g1.slewDb / (std::max (1.0, relMs1) * 0.001 * sr) : 1.0e9;
        const auto hold1 = rev1 ? (int) ((holdMs (p[hold]) * g1.holdScale + g1.waitMs + g1.waitRelease * releaseMs (p[release])) * 0.001 * sr) : 0;
        const auto attMs1 = std::max (g1.attackMinMs, attackMs (p[attack]) * g1.attackScale);
        const auto kA2 = rev1 && g1.attackPole2 > 0.0 ? 1.0 - coef (attMs1 * g1.attackPole2, sr) : 1.0;
        if (! rev1)
            gainDb1 = gainDb2 = -200.0; // (revision 1's state follows the gain it takes over when it is switched on)
        else if (gainDb2 <= -199.0 && gain > 1.0e-10)
            gainDb1 = gainDb2 = gainToDb (gain);
        const auto gap = st == Style::upward ? 0.0 : closeGap (p);
        if (gap <= 0.0)
            isOpen = false;
        const auto dryGain = std::clamp (p[dry], 0.0f, 1.0f);
        const auto outGain = (float) dbToGain (lin (p[output], -24.0, 24.0));
        const auto la = laOn > 0 ? lookaheadMs (p[lookahead]) : 0.0;
        gainDelay.setLength (std::max (0, laSamples - (int) std::lround (la * 0.001 * sr)));
        double maxGr = 0.0;
        const bool over = osFactor > 1;
        // round 190dm: the transient trigger (a drum gate); off = the level gate below, untouched
        const bool trans = transientTrigger (p);
        double tAttack = 0.0, tFastA = 0.0, tFastR = 0.0, tSlowA = 0.0, tSlowR = 0.0, tThr = 0.0, tRatio = 0.0, tHold = 0.0;
        double tRelease = 0.0, tFloor = 0.0, tEmph = 0.0, tStep = 1.0 / sr;
        Shape tShape = Shape::mid;
        if (trans)
        {
            tAttack = coef (std::max (0.01, attackMs (p[attack])), sr);
            tFastA = coef (detFastAttackMs, sr);
            tFastR = coef (detFastReleaseMs, sr);
            tSlowA = coef (detSlowAttackMs, sr);
            tSlowR = coef (detSlowReleaseMs, sr);
            tThr = dbToGain (thresholdDb (p[threshold]));
            // sensitivity 5 is the measured onset ratio; more sensitive asks less of an onset (own law)
            tRatio = detRatioDb * 10.0 / (sensitivityOf (p[sensitivity]) + 5.0);
            tHold = holdMs (p[hold]) * 0.001;
            tRelease = releaseMs (p[release]) * 0.001;
            tFloor = -rangeDb (p[range]);
            // the first difference is unity at 1 kHz, or at the side chain's low-pass when that is lower (a kick's side
            // chain low-passed at 150 Hz still sees its onsets; the reference's probes agree with both, see PARITY.md)
            tEmph = detEmphasis * sr / (2.0 * pi * (lpOn ? std::min (1000.0, std::min (lpHz, sr * 0.45)) : 1000.0));
            tShape = shapeOf (p[releaseShape]);
        }
        // round 190v: the Vocal style's noise reduction (vocalNr above); nothing of it runs in the other styles
        const bool nr = st == Style::vocal && ! duck && ! trans && on (p[law]);
        double nrDepthScale = 0.0, nrRange = 0.0, nrArmMs = 0.0, nrMsCoef = 0.0, nrPeakCoef = 0.0, nrSmooth = 0.0;
        if (nr)
        {
            nrHp.filter (sr, 1, std::min (vocalNr::highPassHz, sr * 0.45), 0.70710678);
            const auto rt = ratioOf (p[ratio]);
            nrDepthScale = rt <= 1.0001 ? 0.0 : vocalNr::dbPerDb * std::sqrt (1.0 - 1.0 / rt);
            nrDepthScale *= std::max (0.0, 1.0 - holdMs (p[hold]) / 250.0); // a long hold takes it away
            nrRange = rangeDb (p[range]);
            nrArmMs = vocalNr::armAfterMs + 1.5 * holdMs (p[hold]);
            nrMsCoef = coef (5.0, sr);
            nrPeakCoef = coef (5.0, sr);
            nrSmooth = coef (2.0, sr);
        }

        for (int i = 0; i < n; ++i)
        {
            float sa = external ? scL[i] : l[i], sb = external ? scR[i] : r[i];
            if (nr)
            {
                const auto ha = (double) nrHp.process (sa, 0), hb = (double) nrHp.process (sb, 1);
                nrMs = nrMsCoef * nrMs + (1.0 - nrMsCoef) * 0.5 * (ha * ha + hb * hb);
                nrAll = nrMsCoef * nrAll + (1.0 - nrMsCoef) * 0.5 * ((double) sa * sa + (double) sb * sb);
                const auto pk = (double) std::max (std::abs (sa), std::abs (sb));
                nrPeak = pk > nrPeak ? pk : nrPeakCoef * nrPeak + (1.0 - nrPeakCoef) * pk;
                if (++nrCount >= vocalNr::step)
                {
                    nrCount = 0;
                    const auto dt = (double) vocalNr::step / sr;
                    if (nrPeak < vocalNr::quietAmp) // a pause: it arms again
                    {
                        nrGapMs += dt * 1000.0;
                        nrRecovered = std::min (nrRecovered, 1.0 - std::clamp ((nrGapMs - nrArmMs) / vocalNr::armOverMs, 0.0, 1.0));
                    }
                    else // sound: the gain climbs back, slower the louder its part above 4 kHz
                    {
                        nrGapMs = 0.0;
                        const auto amp = std::sqrt (nrMs);
                        const auto tau = std::clamp (vocalNr::tauAtRef * amp / vocalNr::refAmp, vocalNr::tauMin, vocalNr::tauMax);
                        nrRecovered += (1.0 - nrRecovered) * (1.0 - std::exp (-dt / tau));
                    }
                    auto depth = nrDepthScale * std::max (0.0, 10.0 * std::log10 (nrMs + 1e-30) - vocalNr::floorDb);
                    // only sound that is mostly above 4 kHz counts as noise (music and voiced notes pass)
                    depth *= std::clamp ((nrMs / (nrAll + 1e-30) - vocalNr::noiseShareLow) / (vocalNr::noiseShareHigh - vocalNr::noiseShareLow), 0.0, 1.0);
                    if (depth > 0.0)
                        depth = depth * nrRange / (depth + nrRange + 1e-9); // the range limits it softly
                    nrTarget = dbToGain (-(1.0 - nrRecovered) * depth);
                }
                nrGain = nrSmooth * nrGain + (1.0 - nrSmooth) * nrTarget;
            }
            if (expertSc)
            {
                float a0 = sa, b0 = sb;
                if (msMode)
                {
                    a0 = 0.5f * (sa + sb);
                    b0 = 0.5f * (sa - sb);
                }
                // each detector channel: its own input at mix 0, moving to the first (−1) or second (+1) channel
                const auto pick = [] (float own, float first, float second, float m) {
                    return m < 0.0f ? own + (-m) * (first - own) : own + m * (second - own);
                };
                sa = gL * pick (a0, a0, b0, mL);
                sb = gR * pick (b0, a0, b0, mR);
            }
            if (hpOn)
            {
                sa = hp.process (sa, 0);
                sb = hp.process (sb, 1);
            }
            if (lpOn)
            {
                sa = lp.process (sa, 0);
                sb = lp.process (sb, 1);
            }
            if (listen)
            {
                l[i] = sa;
                r[i] = sb;
                continue;
            }
            // peak detector: instant attack, 5 ms release
            const auto m = (double) std::max (std::abs (sa), std::abs (sb));
            if (rms1) // round 190lp, revision 1: the style's mean-square level
            {
                meanSq = kRms1 * meanSq + (1.0 - kRms1) * m * m;
                level = std::sqrt (2.0 * meanSq);
            }
            else
                level = m > level ? m : levelRel * level + (1.0 - levelRel) * m;
            if (trans)
            {
                // round 190dm: the onset detector on the side chain's first difference (unity at 1 kHz)
                const auto dl = (double) sa - tPrev[0], dr = (double) sb - tPrev[1];
                tPrev[0] = sa;
                tPrev[1] = sb;
                const auto e = std::max (std::abs ((1.0 - detEmphasis) * sa + tEmph * dl), std::abs ((1.0 - detEmphasis) * sb + tEmph * dr));
                tFast = e > tFast ? tFastA * tFast + (1.0 - tFastA) * e : tFastR * tFast + (1.0 - tFastR) * e;
                tSlow = tFast > tSlow ? tSlowA * tSlow + (1.0 - tSlowA) * tFast : tSlowR * tSlow + (1.0 - tSlowR) * tFast;
                const auto rise = gainToDb (tFast / std::max (tSlow, tThr));
                if (tArmed && tFast > tThr && rise > tRatio)
                {
                    tSince = 0.0;
                    tArmed = false;
                }
                else if (! tArmed && rise < tRatio - detHysteresisDb)
                    tArmed = true;
                double targetDb = tFloor;
                if (tSince >= 0.0)
                {
                    targetDb = std::max (tFloor, transientCurveDb (tShape, tSince - tHold, tRelease));
                    tSince = targetDb <= tFloor ? -1.0 : tSince + tStep; // closed: it waits for the next onset
                }
                auto target = dbToGain (targetDb);
                if (duck)
                    target = duckFloor / std::max (1e-9, target);
                gain = target > gain ? tAttack * gain + (1.0 - tAttack) * target : target;
                const auto g = gainDelay.process ((float) gain);
                maxGr = std::max (maxGr, -gainToDb (gain));
                float a = l[i], b = r[i];
                delay.process (a, b);
                if (over)
                {
                    gains[(std::size_t) i] = g;
                    l[i] = a;
                    r[i] = b;
                    continue;
                }
                l[i] = panned ? (a * g * wetA + a * dryGain * dryA) * outGain : (a * g + a * dryGain) * outGain;
                r[i] = panned ? (b * g * wetB + b * dryGain * dryB) * outGain : (b * g + b * dryGain) * outGain;
                continue;
            }
            // round tp172b: hysteresis — the gate opens on the opening curve and, once open, follows the curve shifted
            // down by the style's gap until the level falls under it
            const auto levelDb = gainToDb (level);
            if (gap > 0.0)
            {
                if (gateCurve (p, levelDb) >= -1.0)
                    isOpen = true;
                else if (gateCurve (p, levelDb, gap) < -1.0)
                    isOpen = false;
            }
            const auto curveDb = gateCurve (p, levelDb, isOpen ? gap : 0.0);
            auto target = dbToGain (curveDb);
            if (duck) // ducking: an open gate is the deepest cut
                target = duckFloor / std::max (1e-9, target);
            // the gain moves in the linear domain: opening with the attack, closing (after hold) with the release
            if (rev1) // round 190lp: in dB — opening with the attack, closing after the hold + wait along two poles
            {
                const auto tDb = std::max (-200.0, duck ? -rangeDb (p[range]) - curveDb : curveDb);
                if (tDb >= gainDb2)
                {
                    gainDb1 = std::max (gainDb1, gainDb2); // (after a closing the first pole sits lower)
                    if (gainDb1 == tDb && gainDb2 == tDb)
                        ; // (settled: nothing moves)
                    else if (attLin1)
                    {
                        auto l1 = gainDb1 <= -199.0 ? 0.0 : dbToGain (gainDb1), l2 = gainDb2 <= -199.0 ? 0.0 : dbToGain (gainDb2);
                        l1 += (target - l1) * kA1;
                        l2 += (l1 - l2) * kA2;
                        gainDb1 = std::max (-200.0, gainToDb (std::max (l1, 1e-10)));
                        gainDb2 = std::max (-200.0, gainToDb (std::max (l2, 1e-10)));
                    }
                    else
                    {
                        gainDb1 += (tDb - gainDb1) * kA1;
                        gainDb2 += (gainDb1 - gainDb2) * kA2;
                    }
                    holdLeft = hold1;
                }
                else if (holdLeft > 0)
                    --holdLeft;
                else
                {
                    gainDb1 += std::max ((tDb - gainDb1) * kR1, -slew1);
                    gainDb2 += (gainDb1 - gainDb2) * kR2;
                }
                if (gainDb2 != shownDb) // (the gain only when its dB changed)
                {
                    shownDb = gainDb2;
                    shownGain = gainDb2 <= -199.0 ? 0.0 : dbToGain (gainDb2);
                }
                gain = shownGain;
            }
            else if (target >= gain)
            {
                gain = aA * gain + (1.0 - aA) * target;
                holdLeft = holdSamples;
            }
            else if (holdLeft > 0)
                --holdLeft;
            else
                gain = aR * gain + (1.0 - aR) * target;
            const auto total = nr ? std::min (gain, nrGain) : gain; // the deeper of the gate and the noise reduction
            maxGr = std::max (maxGr, -gainToDb (total));
            const auto g = gainDelay.process ((float) total);
            float a = l[i], b = r[i];
            delay.process (a, b);
            if (over) // round 135: the gain is upsampled with the audio and applied at the high rate (below)
            {
                gains[(std::size_t) i] = g;
                l[i] = a;
                r[i] = b;
                continue;
            }
            if (panned)
            {
                l[i] = (a * g * wetA + a * dryGain * dryA) * outGain;
                r[i] = (b * g * wetB + b * dryGain * dryB) * outGain;
            }
            else
            {
                l[i] = (a * g + a * dryGain) * outGain;
                r[i] = (b * g + b * dryGain) * outGain;
            }
        }
        if (over)
        {
            const auto m = n * osFactor;
            if (listen) // the side chain heard through the same path (the latency stays)
                for (int c = 0; c < 2; ++c)
                    os[(std::size_t) c].process (c == 0 ? l : r, n, [] (float x) noexcept { return x; });
            else
            {
                int k = 0;
                osGain.process (gains.data(), n, [&] (float x) noexcept { upGain[(std::size_t) std::min (k++, m - 1)] = x; return 0.0f; });
                for (int c = 0; c < 2; ++c)
                {
                    const auto wet = (c == 0 ? wetA : wetB) * outGain, dryK = (c == 0 ? dryA : dryB) * dryGain * outGain;
                    int j = 0;
                    os[(std::size_t) c].process (c == 0 ? l : r, n, [&] (float x) noexcept {
                        return x * upGain[(std::size_t) std::min (j++, m - 1)] * wet + x * dryK;
                    });
                }
            }
        }
        meters.set (0, (float) std::clamp (maxGr / 30.0, 0.0, 1.0));
        meters.set (2, inPeak);
        meters.set (3, blockPeak (l, r, n));
        scL = scR = nullptr;
    }

    // the look-ahead switch sets the latency (10 ms), the knob only how much of it the detector leads; the
    // oversampler adds its own (round 135)
    void configure (const EffectParams& p) noexcept
    {
        const int want = on (p[lookaheadOn]) ? 1 : 0;
        const int factor = crest::oversamplingOf (p[oversampling]);
        if (want == laOn && factor == osFactor)
            return;
        if (factor != osFactor)
        {
            osFactor = factor;
            for (auto& o : os) { o.setFactor (std::max (2, factor)); o.reset(); }
            osGain.setFactor (std::max (2, factor));
            osGain.reset();
        }
        laOn = want;
        laSamples = want ? (int) std::lround (0.010 * sr) : 0;
        delay.setLength (laSamples);
        latency.store (laSamples + (osFactor > 1 ? mixsuite::Oversampler::latency() : 0), std::memory_order_relaxed);
    }

    static constexpr int maxBlock = 1024, maxFactor = 8;
    int osFactor = 0;
    std::array<mixsuite::Oversampler, 2> os;
    mixsuite::Oversampler osGain;
    std::array<float, maxBlock> gains {};
    std::array<float, maxBlock * maxFactor> upGain {};

    double sr = 48000.0;
    int laOn = -1, laSamples = 0;
    std::atomic<int> latency { 0 };
    EffectParams lastParams = [] {
        EffectParams p {};
        const auto& info = sentryInfo();
        for (std::size_t i = 0; i < info.params.size(); ++i)
            p[i] = info.params[i].defaultValue;
        return p;
    }();
    StereoDelay delay;
    MonoDelay gainDelay;
    Biquad hp, lp;
    // round 190v: the Vocal style's noise reduction — the high band's mean square, the input's peak, how far the gain
    // has recovered (0 … 1), the pause so far, the gain it applies
    Biquad nrHp;
    double nrMs = 0.0, nrAll = 0.0, nrPeak = 0.0, nrRecovered = 0.0, nrGapMs = 1e9, nrTarget = 1.0, nrGain = 1.0;
    int nrCount = 0;
    double level = 0.0, gain = 0.0;
    double gainDb1 = -200.0, gainDb2 = -200.0, meanSq = 0.0; // round 190lp: revision 1's two poles (dB), mean square
    double shownDb = -200.0, shownGain = 0.0; // round 190lp: the last pole value turned into a gain
    bool isOpen = false; // round tp172b: past the opening point (the hysteresis)
    int holdLeft = 0;
    // round 190dm: the transient trigger — the detector's followers, the last side-chain samples, the time since the
    // onset (s; −1 = closed, waiting), whether a new onset may open it
    double tFast = 0.0, tSlow = 0.0, tPrev[2] {}, tSince = -1.0;
    bool tArmed = true;
    const float* scL = nullptr;
    const float* scR = nullptr;
    LiveMeters meters;
};
} // namespace
} // namespace sentry

// ================================================================== registry

const EffectInfo& crestInfo()
{
    static const EffectInfo info { "Crest Compressor",
                                   { { "Style", 0.0f }, { "Threshold", 0.7f }, { "Ratio", 0.6f }, { "Knee", 0.0f }, { "Range", 1.0f },
                                     { "Attack", 0.68f }, { "Release", 0.55f }, { "Auto release", 0.0f }, { "Hold", 0.0f },
                                     { "Lookahead", 0.0f }, { "Max lookahead", 0.0f },
                                     { "Character", 0.0f }, { "Character pre", 0.0f }, { "Drive", 0.5f },
                                     { "SC level", 0.5f }, { "External SC", 0.0f }, { "Stereo link", 1.0f }, { "SC listen", 0.0f },
                                     { "SC1 shape", 0.0f }, { "SC1 freq.", 0.30f }, { "SC1 gain", 0.5f }, { "SC1 Q", 0.5f }, { "SC1 slope", 0.125f },
                                     { "SC2 shape", 0.0f }, { "SC2 freq.", 0.575f }, { "SC2 gain", 0.5f }, { "SC2 Q", 0.5f }, { "SC2 slope", 0.125f },
                                     { "SC3 shape", 0.0f }, { "SC3 freq.", 0.80f }, { "SC3 gain", 0.5f }, { "SC3 Q", 0.5f }, { "SC3 slope", 0.125f },
                                     { "Oversampling", 0.0f }, { "Auto gain", 1.0f }, { "Mix", 1.0f }, { "Input", 0.5f }, { "Output", 0.5f },
                                     { "Dry", 0.0f }, { "Auto threshold", 0.0f }, { "Link mode", 0.0f }, { "Meter scale", 0.75f },
                                     { "Input meter", 1.0f },
                                     { "Wet pan", 0.5f }, { "Dry pan", 0.5f }, // round 135
                                     // round 137: the side-chain trigger (off), 1/4 note, rate 100 %, length 10 %, no channel
                                     { "Trigger", 0.0f }, { "Trigger sync", 0.5f }, { "Trigger rate", 0.5f }, { "Trigger length", 0.1f },
                                     { "Trigger channel", 0.0f },
                                     { "Voicing", 0.0f }, // round tp172b2
                                     // round 190v: two-way and vari-mu on the reference grid (new instances; projects saved before keep
                                     // round 108's laws: upgradeLoadedEffectParams)
                                     { "Law", 1.0f } } };
    return info;
}

const EffectInfo& prismInfo()
{
    static const EffectInfo info = [] {
        EffectInfo i { "Prism Multiband", {} };
        static const char* names[prism::bands][prism::fieldsPerBand] {
            { "B1 threshold", "B1 ratio", "B1 range", "B1 expand", "B1 knee", "B1 attack", "B1 release", "B1 level", "B1 on", "B1 solo" },
            { "B2 threshold", "B2 ratio", "B2 range", "B2 expand", "B2 knee", "B2 attack", "B2 release", "B2 level", "B2 on", "B2 solo" },
            { "B3 threshold", "B3 ratio", "B3 range", "B3 expand", "B3 knee", "B3 attack", "B3 release", "B3 level", "B3 on", "B3 solo" },
            { "B4 threshold", "B4 ratio", "B4 range", "B4 expand", "B4 knee", "B4 attack", "B4 release", "B4 level", "B4 on", "B4 solo" },
            { "B5 threshold", "B5 ratio", "B5 range", "B5 expand", "B5 knee", "B5 attack", "B5 release", "B5 level", "B5 on", "B5 solo" },
            { "B6 threshold", "B6 ratio", "B6 range", "B6 expand", "B6 knee", "B6 attack", "B6 release", "B6 level", "B6 on", "B6 solo" }
        };
        // defaults: −18 dB, 4.5 : 1, range −12 dB (downward), knee 24 dB, attack 20 %, release 20 %, 0 dB, on
        const float defaults[prism::fieldsPerBand] { prism::thresholdValue (-18.0), 0.6f, 0.3f, 0.0f, 0.5f, 0.2f, 0.2f, 0.5f, 1.0f, 0.0f };
        for (int b = 0; b < prism::legacyBands; ++b)
            for (int f = 0; f < prism::fieldsPerBand; ++f)
                i.params.push_back ({ names[b][f], defaults[f] });
        i.params.push_back ({ "Crossover 1", prism::crossoverValue (120.0) });
        i.params.push_back ({ "Crossover 2", prism::crossoverValue (1000.0) });
        i.params.push_back ({ "Crossover 3", prism::crossoverValue (6000.0) });
        i.params.push_back ({ "Slope", 0.5f });
        i.params.push_back ({ "Lookahead", 0.0f });
        i.params.push_back ({ "Mix", 1.0f });
        i.params.push_back ({ "Input", 0.5f });
        i.params.push_back ({ "Output", 0.5f });
        for (const auto* n : { "B1 lookahead", "B2 lookahead", "B3 lookahead", "B4 lookahead" })
            i.params.push_back ({ n, 1.0f }); // 20 ms: each band leads by the whole global look-ahead (as before)
        // round 135: bands 5 and 6, crossovers 4 and 5, their look-aheads, the band count (4), each band's output pan,
        // the external side chain
        for (int b = prism::legacyBands; b < prism::bands; ++b)
            for (int f = 0; f < prism::fieldsPerBand; ++f)
                i.params.push_back ({ names[b][f], defaults[f] });
        i.params.push_back ({ "Crossover 4", prism::crossoverValue (10000.0) });
        i.params.push_back ({ "Crossover 5", prism::crossoverValue (14000.0) });
        i.params.push_back ({ "B5 lookahead", 1.0f });
        i.params.push_back ({ "B6 lookahead", 1.0f });
        i.params.push_back ({ "Bands", prism::bandCountValue (4) });
        for (const auto* n : { "B1 pan", "B2 pan", "B3 pan", "B4 pan", "B5 pan", "B6 pan" })
            i.params.push_back ({ n, 0.5f });
        i.params.push_back ({ "External SC", 0.0f });
        // round 144: the processing mode (dynamic phase = the old engine) and the oversampling (off)
        i.params.push_back ({ "Processing", 0.5f });
        i.params.push_back ({ "Oversampling", 0.0f });
        i.params.push_back ({ "Pan mode", 0.0f });
        // round 147: the measured Dynamic phase engine for new instances; projects saved before it load the old one
        // (engine/ProjectFile.cpp sets 0 when the saved parameter list ends before this index)
        i.params.push_back ({ "Dynamic engine", 1.0f });
        // round 147: the expert panel — stereo link (100 %), its mode (mid), side-chain filtering (band), free low / high
        for (const auto* n : { "B1 link", "B2 link", "B3 link", "B4 link", "B5 link", "B6 link" })
            i.params.push_back ({ n, 0.5f });
        for (const auto* n : { "B1 link mode", "B2 link mode", "B3 link mode", "B4 link mode", "B5 link mode", "B6 link mode" })
            i.params.push_back ({ n, 0.0f });
        for (const auto* n : { "B1 SC filter", "B2 SC filter", "B3 SC filter", "B4 SC filter", "B5 SC filter", "B6 SC filter" })
            i.params.push_back ({ n, 0.0f });
        for (const auto* n : { "B1 SC low", "B2 SC low", "B3 SC low", "B4 SC low", "B5 SC low", "B6 SC low" })
            i.params.push_back ({ n, 0.0f });
        for (const auto* n : { "B1 SC high", "B2 SC high", "B3 SC high", "B4 SC high", "B5 SC high", "B6 SC high" })
            i.params.push_back ({ n, 1.0f });
        // round 147: each crossover's own slope (0 = the global Slope, the old sound)
        for (const auto* n : { "Crossover 1 slope", "Crossover 2 slope", "Crossover 3 slope", "Crossover 4 slope", "Crossover 5 slope" })
            i.params.push_back ({ n, 0.0f });
        // round 150: the band layout (split), each band's own edges / slopes for the free layout (the split layout's
        // defaults: 30 – 120 – 1000 – 6000 – 30k, then 10k – 14k – 30k; 24 dB/oct), each band's side-chain input (the
        // global switch) and the detector law (measured for new instances; projects saved before keep the old one)
        i.params.push_back ({ "Band layout", 0.0f });
        {
            static constexpr double lo[prism::bands] { 30.0, 120.0, 1000.0, 6000.0, 10000.0, 14000.0 };
            static constexpr double hi[prism::bands] { 120.0, 1000.0, 6000.0, 30000.0, 14000.0, 30000.0 };
            static const char* loN[prism::bands] { "B1 low", "B2 low", "B3 low", "B4 low", "B5 low", "B6 low" };
            static const char* hiN[prism::bands] { "B1 high", "B2 high", "B3 high", "B4 high", "B5 high", "B6 high" };
            static const char* lsN[prism::bands] { "B1 low slope", "B2 low slope", "B3 low slope", "B4 low slope", "B5 low slope", "B6 low slope" };
            static const char* hsN[prism::bands] { "B1 high slope", "B2 high slope", "B3 high slope", "B4 high slope", "B5 high slope", "B6 high slope" };
            static const char* scN[prism::bands] { "B1 SC input", "B2 SC input", "B3 SC input", "B4 SC input", "B5 SC input", "B6 SC input" };
            for (int b = 0; b < prism::bands; ++b)
                i.params.push_back ({ loN[b], prism::edgeValue (lo[b]) });
            for (int b = 0; b < prism::bands; ++b)
                i.params.push_back ({ hiN[b], prism::edgeValue (hi[b]) });
            for (int b = 0; b < prism::bands; ++b)
                i.params.push_back ({ lsN[b], 0.5f });
            for (int b = 0; b < prism::bands; ++b)
                i.params.push_back ({ hsN[b], 0.5f });
            for (int b = 0; b < prism::bands; ++b)
                i.params.push_back ({ scN[b], 0.0f });
        }
        i.params.push_back ({ "Detector", 1.0f });
        i.params.push_back ({ "Revision", 1.0f }); // round 152: gain-stage attack, narrow bands, minimum width (projects saved before keep round 150's)
        i.params.push_back ({ "Audition", 0.0f }); // round tp172b6: off, or the band whose detector signal replaces the output
        return i;
    }();
    return info;
}

const EffectInfo& sentryInfo()
{
    static const EffectInfo info { "Sentry Gate",
                                   { { "Style", 0.0f }, { "Threshold", 0.5f }, { "Ratio", 1.0f }, { "Range", 1.0f }, { "Attack", 0.1f },
                                     { "Release", 0.31f }, { "Hold", 0.28f }, { "Knee", 0.0f }, { "Lookahead", 0.0f },
                                     { "Lookahead on", 0.0f }, { "High pass", 0.0f }, { "Low pass", 1.0f }, { "External SC", 0.0f },
                                     { "SC listen", 0.0f }, { "Up threshold", 0.8f }, { "Up ratio", 0.25f }, { "Dry", 0.0f },
                                     { "Output", 0.5f },
                                     { "Channel mode", 0.0f }, { "SC gain L", 0.5f }, { "SC mix L", 0.5f }, { "SC gain R", 0.5f },
                                     { "SC mix R", 0.5f }, { "Wet pan", 0.5f }, { "Dry pan", 0.5f }, { "Ducking", 0.0f },
                                     { "Input", 0.5f },
                                     { "Oversampling", 0.0f }, // round 135
                                     // round 190v: the Vocal style's noise reduction (new instances; projects saved before have none)
                                     { "Law", 1.0f },
                                     // round 190dm: the trigger (Level = every gate before), the transient detector's
                                     // sensitivity (5 of 1 … 10) and the transient gate's release shape (Mid)
                                     { "Trigger", 0.0f }, { "Sensitivity", 4.0f / 9.0f }, { "Release shape", 0.5f } } };
    return info;
}

bool isDynKind (EffectKind kind) noexcept
{
    return kind == EffectKind::crestCompressor || kind == EffectKind::prismMultiband || kind == EffectKind::sentryGate;
}

std::unique_ptr<Effect> make (EffectKind kind, const EffectParams* initial)
{
    switch (kind)
    {
        case EffectKind::crestCompressor: return std::make_unique<crest::Compressor> (initial);
        case EffectKind::prismMultiband: return std::make_unique<prism::Multiband> (initial);
        case EffectKind::sentryGate: return std::make_unique<sentry::Gate> (initial);
        default: return nullptr;
    }
}

std::string paramText (EffectKind kind, int param, float v)
{
    const auto db = [] (double x) { return fmt ("%+.1f dB", x); };
    if (kind == EffectKind::crestCompressor)
    {
        using namespace crest;
        if (param >= sc1Shape && param <= sc3Slope)
        {
            const auto field = (param - sc1Shape) % 5;
            switch (field)
            {
                case 0: return facet::shapeName (facet::shape (v));
                case 1: return hzText (facet::hz (v));
                case 2: return db (facet::gainDb (v));
                case 3: return fmt ("Q %.2f", facet::q (v));
                default: return std::to_string (facet::slopeDb (v)) + " dB/oct";
            }
        }
        switch (param)
        {
            case style:
            {
                static const char* names[styles] { "Clean", "Allround", "Punch", "Upward", "Two-way", "Vari-mu", "Classic", "Opto",
                                                   "Vocal", "Bus", "Pumping" };
                return names[(int) styleOf (v)];
            }
            case threshold: return fmt ("%.1f dB", thresholdDb (v));
            case ratio: return v >= 1.0f ? std::string ("\xe2\x88\x9e : 1") : ratioText (ratioOf (v));
            case knee: return fmt ("%.1f dB", kneeDb (v));
            case range: return fmt ("%.1f dB", rangeDb (v));
            case attack: return msText (attackMs (v));
            case release: return msText (releaseMs (v));
            case hold: return msText (holdMs (v));
            case lookahead: return msText (lookaheadMs (v));
            case maxLookahead: return maxLookaheadMs (v) <= 0.0 ? "off" : msText (maxLookaheadMs (v));
            case character:
            {
                static const char* names[] { "off", "warm", "crisp", "bright" };
                return names[(int) characterOf (v)];
            }
            case characterPre: return on (v) ? "pre" : "post";
            case drive: return db (driveDb (v));
            case scLevel: return db (scLevelDb (v));
            case stereoLink: case mix: return fmt ("%.0f%%", v * 100.0);
            case oversampling: return oversamplingOf (v) == 1 ? "off" : std::to_string (oversamplingOf (v)) + "x";
            case input: case output: return db (trimDb (v));
            case dryGain: return v <= 0.0f ? "-inf dB" : db (dryDb (v));
            case linkMode:
            {
                static const char* names[linkModes] { "Stereo", "Mid only", "Side only", "Mid > side", "Side > mid" };
                return names[(int) linkModeOf (v)];
            }
            case meterScale: return fmt ("%.0f dB", meterRangeDb (v));
            case inputMeter: return on (v) ? "shown" : "hidden";
            case wetPan: case dryPan:
            {
                const auto x = panOf (v);
                return std::abs (x) < 0.005 ? std::string ("C") : fmt (x < 0 ? "%.0f%% L" : "%.0f%% R", std::abs (x) * 100.0);
            }
            case trigger:
            {
                static const char* names[3] { "Off", "Host sync", "MIDI" };
                return names[(int) triggerOf (v)];
            }
            case triggerSync:
            {
                static const char* names[5] { "1/1", "1/2", "1/4", "1/8", "1/16" };
                return names[pick (v, 5)];
            }
            case triggerRate: return fmt ("%.0f%%", 100.0 * triggerRateOf (v));
            case triggerLength: return fmt ("%.0f%%", 100.0 * triggerLengthOf (v));
            case triggerChannel:
                return triggerChannelOf (v) < 0 ? std::string ("Off") : fmt ("Channel %.0f", (double) (triggerChannelOf (v) + 1));
            case voicing:
            {
                static const char* names[voicings] { "None", "Smooth", "Op-El", "Mastering" };
                return names[pick (v, voicings)];
            }
            case law: return on (v) ? "measured" : "classic";
            default: return on (v) ? "on" : "off";
        }
    }
    if (kind == EffectKind::prismMultiband)
    {
        using namespace prism;
        const bool bandField = param < legacyBands * fieldsPerBand || (param >= band5 && param < band5 + 2 * fieldsPerBand);
        if (bandField)
        {
            switch ((param < band5 ? param : param - band5) % fieldsPerBand)
            {
                case fThreshold: return fmt ("%.1f dB", thresholdDb (v));
                case fRatio: return isInfiniteRatio (v) ? std::string ("\xe2\x88\x9e : 1") : ratioText (ratioOf (v));
                case fRange: return db (rangeDb (v));
                case fMode: return on (v) ? "expand" : "compress";
                case fKnee: return fmt ("%.1f dB", kneeDb (v));
                case fAttack: case fRelease: return fmt ("%.0f%%", v * 100.0);
                case fLevel: return db (levelDb (v));
                default: return on (v) ? "on" : "off";
            }
        }
        switch (param)
        {
            case xover1: case xover2: case xover3: case xover4: case xover5: return hzText (crossoverHz (v));
            case bandCountParam: return std::to_string (1 + pick (v, bands));
            case scExternal: return on (v) ? "external" : "internal";
            case processingMode:
            {
                static const char* const names[3] { "Linear Phase", "Dynamic Phase", "Minimum Phase" };
                return names[(int) phaseOf (v)];
            }
            case oversampling: return oversamplingOf (v) == 1 ? "Off" : std::to_string (oversamplingOf (v)) + "x";
            case panMode: return on (v) ? "mid / side" : "left / right";
            case dynamicEngine: return on (v) ? "measured" : "classic";
            case bandLink0: case bandLink0 + 1: case bandLink0 + 2: case bandLink0 + 3: case bandLink0 + 4: case bandLink0 + 5:
                return v <= 0.5f ? fmt ("%.0f%%", 100.0 * linkAmount (v)) : fmt ("100%%, %.0f%% only", 100.0 * linkOnly (v));
            case bandLinkMode0: case bandLinkMode0 + 1: case bandLinkMode0 + 2: case bandLinkMode0 + 3: case bandLinkMode0 + 4: case bandLinkMode0 + 5:
                return on (v) ? "side" : "mid";
            case bandScFree0: case bandScFree0 + 1: case bandScFree0 + 2: case bandScFree0 + 3: case bandScFree0 + 4: case bandScFree0 + 5:
                return on (v) ? "free" : "band";
            case bandScLow0: case bandScLow0 + 1: case bandScLow0 + 2: case bandScLow0 + 3: case bandScLow0 + 4: case bandScLow0 + 5:
            case bandScHigh0: case bandScHigh0 + 1: case bandScHigh0 + 2: case bandScHigh0 + 3: case bandScHigh0 + 4: case bandScHigh0 + 5:
                return hzText (scFilterHz (v));
            case xoverSlope0: case xoverSlope0 + 1: case xoverSlope0 + 2: case xoverSlope0 + 3: case xoverSlope0 + 4:
            {
                static const char* const names[4] { "global", "12 dB/oct", "24 dB/oct", "48 dB/oct" };
                return names[pick (v, 4)];
            }
            case slope: return std::to_string (slopeDbOct (v)) + " dB/oct";
            case bandLayout: return on (v) ? "free" : "split";
            case detectorLaw: return on (v) ? "measured" : "classic";
            case rev152: return on (v) ? "round 152" : "round 150";
            case audition: return std::lround (v * bands) > 0 ? fmt ("Band %.0f", (double) std::lround (v * bands)) : std::string ("off");
            case lookahead: return v <= 0.0f ? "off" : msText (lookaheadMs (v));
            case mix: return fmt ("%.0f%%", v * 100.0);
            case input: case output: return db (trimDb (v));
            default:
                if (param >= bandLo0 && param < bandLoSlope0) // round 150
                    return hzText (edgeHz (v));
                if (param >= bandLoSlope0 && param < bandScInput0)
                    return fmt ("%.1f dB/oct", slopeOfValue (v));
                if (param >= bandScInput0 && param < bandScInput0 + bands)
                    return v < 0.25f ? "global" : (v < 0.75f ? "plug-in input" : "external");
                if (param >= bandPan0 && param < bandPan0 + bands)
                {
                    const auto x = panOf (v);
                    return std::abs (x) < 0.005 ? std::string ("C") : fmt (x < 0 ? "%.0f%% L" : "%.0f%% R", std::abs (x) * 100.0);
                }
                return msText (lookaheadMs (v)); // the bands' own look-ahead
        }
    }
    if (kind == EffectKind::sentryGate)
    {
        using namespace sentry;
        switch (param)
        {
            case style:
            {
                static const char* names[styles] { "Classic", "Clean", "Vocal", "Guitar", "Upward" };
                return names[(int) styleOf (v)];
            }
            case threshold: return fmt ("%.1f dB", thresholdDb (v));
            case ratio: return ratioText (ratioOf (v));
            case range: return fmt ("%.1f dB", rangeDb (v));
            case attack: return msText (attackMs (v));
            case release: return msText (releaseMs (v));
            case hold: return msText (holdMs (v));
            case knee: return fmt ("%.1f dB", kneeDb (v));
            case lookahead: return msText (lookaheadMs (v));
            case highPass: return v <= 0.0f ? "off" : hzText (filterHz (v));
            case lowPass: return v >= 1.0f ? "off" : hzText (filterHz (v));
            case upThreshold: return fmt ("%.1f dB", upThresholdDb (v));
            case upRatio: return fmt ("%.2f : 1", upRatioOf (v));
            case dry: return fmt ("%.0f%%", v * 100.0);
            case output: case input: return db (lin (v, -24.0, 24.0));
            case oversampling: return crest::oversamplingOf (v) == 1 ? "off" : std::to_string (crest::oversamplingOf (v)) + "x";
            case law: return on (v) ? "measured" : "classic";
            case trigger: return on (v) ? "transient" : "level";
            case sensitivity: return fmt ("%.1f", sensitivityOf (v));
            case releaseShape:
            {
                static const char* names[3] { "smooth", "mid", "sharp" };
                return names[(int) shapeOf (v)];
            }
            case channelMode: return on (v) ? "Mid/Side" : "Left/Right";
            case scGainL: case scGainR: return db (scGainDb (v));
            case scMixL: case scMixR: return fmt ("%+.2f", mixOf (v));
            case wetPan: case dryPan:
            {
                const auto x = panOf (v);
                return std::abs (x) < 0.005 ? std::string ("C") : fmt (x < 0 ? "%.0f%% L" : "%.0f%% R", std::abs (x) * 100.0);
            }
            default: return on (v) ? "on" : "off";
        }
    }
    return fmt ("%.2f", v);
}
} // namespace lynx::dyn
