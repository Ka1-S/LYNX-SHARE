#include "core/DynamicsSuite.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>

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
    std::array<std::atomic<float>, 8> v {};
    float read (int k) const noexcept { return k >= 0 && k < 8 ? v[(std::size_t) k].load (std::memory_order_relaxed) : 0.0f; }
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
double ratioOf (float v) noexcept { return ratioLaw (v); }
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
enum class Curve { downward, upward, twoWay, vocal, variMu };
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
    { 0.263, 0.519, 1.082, 7.60, 0.487, Curve::downward, 0.0, 1.09, 1.000, 8.56, 0.464, 0.54 }, // clean
    { 0.382, 0.223, 0.862, 16.28, 0.488, Curve::downward, 0.0, 0.00, 1.000, 4.61, 1.000, 0.86 }, // allround
    { 0.310, 0.580, 0.722, 6.21, 0.800, Curve::downward, 0.0, 0.00, 1.000, 0.00, 0.002, 0.71 }, // punch
    { 0.000, 41.775, 1.200, 0.00, 0.000, Curve::upward, 0.0, -4.87, 0.670, 51.42, 0.002, 0.77, 0.00, 0.000 }, // upward
    { 0.002, 0.002, 0.000, 11.04, 1.000, Curve::twoWay, 1.3, -0.71, 0.800, 3.46, 0.019, 0.70, -0.43, 0.487 }, // two-way (recovers within about a millisecond)
    { 45.680, 0.076, 0.470, 0.00, 0.000, Curve::downward, 0.0, 3.42, 1.000, 39.36, 1.000, 1.00, 0.0, 0.0 }, // vari-mu
    { 1.150, 0.564, 0.402, 20.80, 0.000, Curve::downward, 0.0, -0.58, 1.000, 0.00, 0.002, 1.00, 0.00, 0.000 }, // classic
    { 0.169, 0.789, 0.880, 7.27, 0.722, Curve::downward, 0.0, 0.77, 1.000, 2.88, 0.750, 0.39 }, // opto
    { 1.611, 0.712, 0.000, 8.40, 0.000, Curve::vocal, 0.0, -0.17, 3.000, 0.00, 0.002, 0.96, 0.00, 0.000 }, // vocal
    { 0.027, 1.093, 0.510, 7.39, 0.608, Curve::downward, 0.0, 0.00, 1.000, 9.41, 0.045, 0.93 }, // bus
    { 0.002, 1.582, 0.800, 0.00, 0.952, Curve::downward, 0.0, 0.00, 1.000, 0.00, 0.002, 0.46, 0.0, 0.0 }, // pumping
    { 0.878, 0.102, 0.750, 17.92, 0.176, Curve::downward, 0.0, 1.86, 1.000, 28.04, 3.320, 1.00, 0.00, 0.000 }, // smooth (voicing)
    { 0.570, 0.223, 0.990, 13.98, 0.488, Curve::downward, 0.0, 0.00, 1.000, 4.61, 1.000, 1.00, 0.0, 0.0 }, // op-el (voicing)
    { 0.023, 3.667, 0.190, 1.69, 0.808, Curve::downward, 0.0, -0.77, 1.000, 0.00, 3.165, 0.83, 0.00, 0.000 }, // mastering (voicing)
};

double releaseTau (const StyleDef& d, double knobMs) noexcept
{
    return d.releaseScale * 200.0 * std::pow (knobMs / 200.0, d.releaseExp);
}

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

// target gain change (dB; negative = reduction) for a detector level `x` (dB)
double curveGain (Style st, double x, double thr, double r, double knee, double range) noexcept
{
    const auto& d = styleDefs[(int) st];
    const auto s = 1.0 - 1.0 / r;
    switch (d.curve)
    {
        case Curve::upward:
        {
            // upward: quiet parts are lifted toward a point `offsetDb` from the threshold (−10.5 dB: under it); above
            // the threshold the gain holds; the corner is softened over the style's knee
            const auto kn = d.kneeAdd + d.kneeScale * knee + d.kneeSlope * s;
            const auto softMin = x - kneeCurve (x - thr, 1.0, kn);
            const auto g = s * d.slopeScale * (thr + d.offsetDb - softMin);
            // round tp172b3 [EXEC] (the Pro-C 3 trial, Upward, drums and bursts after digital silence): the lift stops
            // at 30 dB whatever the range (a 30 ms attack lets the first hit through 29.5 dB up, as the original)
            return std::clamp (g, -range, std::min (range, upwardCapDb));
        }
        case Curve::twoWay:
        {
            // two-way: a fixed offset, upward compression below (threshold − 6 dB), downward above (threshold + 4 dB)
            const auto kn = d.kneeAdd + d.kneeScale * knee + d.kneeSlope * s;
            // round tp172b3: the upward part only over the 30 dB under its corner and not on silence (under −90 dBFS:
            // the original's first hit after digital silence comes out turned down, never lifted)
            const auto up = x < -90.0 ? 0.0 : d.upSlope * s * std::clamp (thr - 6.0 - x, 0.0, upwardCapDb);
            const auto g = d.baseDb * s + up - d.slopeScale * kneeCurve (x - thr - 4.0 - d.offsetDb, s, kn);
            return std::clamp (g, -range, std::min (range, upwardCapDb));
        }
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

double staticGainDb (const EffectParams& p, double inputDb)
{
    return curveGain (styleOf (p), inputDb, thresholdDb (p[threshold]), ratioOf (p[ratio]), kneeDb (p[knee]),
                      std::max (0.01, rangeDb (p[range])));
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
class Compressor final : public Effect
{
public:
    explicit Compressor (const EffectParams* initial)
    {
        for (auto& d : gainDelay)
            d.assign ((std::size_t) gainCapacity, 1.0f);
        if (initial != nullptr)
            configure (*initial);
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        dc.set (sr);
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
        for (auto& b : scEq)
            for (auto& s : b)
                s.reset();
        for (auto& d : det)
            d = {};
        for (auto& pk : peaks)
            pk.fill (0.0f);
        dc.reset();
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
        int holdLeft = 0, gainHold = 0, peakPos = 0;
    };

    // per-chunk settings at the rate the detector runs at
    struct Ctx
    {
        Style st;
        double thr, rat, kn, rng, aA, relMs, rate, slowCoef, gainBlend;
        int holdSamples, window;
        bool autoRel;
    };

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
        const auto& sd = styleDefs[(int) c.st];
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
        c.autoRel = on (p[autoRelease]) && sd.curve != Curve::twoWay;
        c.rate = rate;
        c.slowCoef = coef (1000.0, rate);
        c.gainBlend = sd.gainBlend;
        c.holdSamples = (int) (holdMs (p[hold]) * 0.001 * rate);
        c.window = sd.windowMs > 0.0 ? std::clamp ((int) (sd.windowMs * 0.001 * rate), 1, peakWindow) : 0;
        return c;
    }

    // one detector step: the level → the gain (dB) the compressor applies now
    double detect (int c, double level, const Ctx& x) noexcept
    {
        auto& d = det[(std::size_t) c];
        if (x.window > 0) // the peak of the last `window` samples
        {
            auto& ring = peaks[(std::size_t) c];
            ring[(std::size_t) (d.peakPos % x.window)] = (float) level;
            d.peakPos = (d.peakPos + 1) % x.window;
            for (int k = 0; k < x.window; ++k)
                level = std::max (level, (double) ring[(std::size_t) k]);
        }
        const auto in = std::max (-120.0, 20.0 * std::log10 (level + 1e-12));
        if (in > d.env)
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
                const auto now = -curveGain (x.st, d.env, x.thr, x.rat, x.kn, x.rng);
                const auto fraction = now > 0.1 ? std::clamp (d.sustain / now, 0.0, 1.0) : 1.0;
                ms *= 0.35 + 0.65 * fraction;
            }
            const auto aR = coef (ms, x.rate);
            d.env = aR * d.env + (1.0 - aR) * in;
        }
        auto g = curveGain (x.st, d.env, x.thr, x.rat, x.kn, x.rng);
        if (x.gainBlend > 0.0) // the gain-domain detector: the instantaneous curve gain, smoothed
        {
            const auto target = curveGain (x.st, in, x.thr, x.rat, x.kn, x.rng);
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
        d.sustain = x.slowCoef * d.sustain + (1.0 - x.slowCoef) * std::max (0.0, -g);
        return g;
    }

    // the gains of a run of side-chain samples (per channel, or linked), through the look-ahead's gain delay
    double gains (const float* a, const float* b, int count, double link, int lag, const Ctx& x, float* outL, float* outR) noexcept
    {
        double maxGr = 0.0;
        const bool both = link >= 0.999;
        for (int i = 0; i < count; ++i)
        {
            const auto la = std::abs (a[i]), lb = std::abs (b[i]);
            const auto m = (double) std::max (la, lb);
            float g[2];
            const auto g0 = detect (0, link * m + (1.0 - link) * la, x);
            g[0] = (float) dbToGain (g0);
            maxGr = std::max (maxGr, -g0);
            if (both)
                g[1] = g[0];
            else
            {
                const auto g1 = detect (1, link * m + (1.0 - link) * lb, x);
                g[1] = (float) dbToGain (g1);
                maxGr = std::max (maxGr, -g1);
            }
            for (int c = 0; c < 2; ++c)
            {
                auto& line = gainDelay[(std::size_t) c];
                line[(std::size_t) gainPos] = g[c];
                const auto rd = (gainPos - lag + gainCapacity) % gainCapacity;
                (c == 0 ? outL : outR)[i] = line[(std::size_t) rd];
            }
            gainPos = (gainPos + 1) % gainCapacity;
        }
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
        if (on (p[autoGain]) && styleDefs[(int) st].curve != Curve::upward)
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
            makeup = (float) dbToGain (std::min (rng, 0.36 * slowAttack * plain));
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
        if (factor == 1)
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
        for (int i = 0; i < n; ++i)
        {
            float a = wl[(std::size_t) i], b = wr[(std::size_t) i];
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
    std::array<std::array<float, peakWindow>, 2> peaks {};
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
    if (! expand)
    {
        if (rng < 0.0) // downward compression above the threshold
            return -limit (kneeCurve (x - thr, 1.0 - 1.0 / r, kn));
        return limit (kneeCurve (thr - x, 1.0 - 1.0 / r, kn)); // upward compression below it
    }
    if (rng < 0.0) // downward expansion below the threshold
        return -limit (kneeCurve (thr - x, r - 1.0, kn));
    return limit (kneeCurve (x - thr, 1.0 - 1.0 / r, kn)); // upward expansion above it
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
        // round 150: each band's own linear-phase shape (Pro-MB's 1 / (1 + x^m), m = slope / 6, continuous)
        std::complex<double> y = solo ? std::complex<double> {} : std::complex<double> { 1.0, 0.0 };
        for (int b = 0; b < nb; ++b)
        {
            if (L.segmentOf[(std::size_t) b] < 0)
                continue;
            const auto a = linearShare (wOf (L.loHz[(std::size_t) b]), wOf (L.hiHz[(std::size_t) b]), L.loSlope[(std::size_t) b] / 6.0, L.hiSlope[(std::size_t) b] / 6.0, om);
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
        for (auto& b : band)
            b = std::abs (b);
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

class Multiband final : public Effect
{
public:
    explicit Multiband (const EffectParams* initial)
    {
        if (initial != nullptr)
            configure (*initial);
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        laMs = -1.0;
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
        held.fill (0.0);
        held2.fill (0.0);
        gains.fill (1.0f);
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
        }
        const auto nb = bandCount (p);
        for (int b = 0; b < bands; ++b)
            meters.set (bandMeter (b), b < nb ? (float) std::clamp (extreme[(std::size_t) b] / 30.0, -1.0, 1.0) : 0.0f);
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
                designKernels (ns);
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
            const auto bandLa = std::min (laMs, lookaheadMs (p[(std::size_t) bandLookaheadParam (b)]));
            gainDelay[(std::size_t) b].setLength (std::max (0, laSamples - (int) std::lround (bandLa * 0.001 * sr)));
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
                    const auto follow = [&] (double& e, double& hold, double m) noexcept {
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
    // zero phase
    void designKernels (int nb) noexcept
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
                    m *= std::abs (split[(std::size_t) j].response (true, w));
                m *= std::abs (split[(std::size_t) k].response (false, w));
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
            const auto wl = L.loHz[(std::size_t) b] > 0.0 ? minphase::warp (L.loHz[(std::size_t) b], sr) : 0.0;
            const auto wh = L.hiHz[(std::size_t) b] > 0.0 ? minphase::warp (L.hiHz[(std::size_t) b], sr) : 0.0;
            auto& t = linTable[(std::size_t) b];
            for (int i = 0; i < bins; ++i)
            {
                const auto w = std::tan (0.5 * pi * std::min ((double) i / (double) (bins - 1), 0.9999));
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

    void configure (const EffectParams& p) noexcept
    {
        // round 144: the processing mode and the oversampling (at most 4x, and only up to 400 kHz)
        const int m = linSize > 0 ? (int) phaseOf (p[processingMode]) : (int) Phase::dynamic;
        int factor = oversamplingOf (p[oversampling]);
        while (factor > 1 && sr * factor > 400000.0)
            factor /= 2;
        const auto la = lookaheadMs (p[lookahead]);
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
            for (auto& d : bandDelay)
                d.setLength (s);
        }
        latency.store (laSamples + ((Phase) mode == Phase::linear ? linearLatency (sr) : 0)
                           + (osFactor > 1 ? mixsuite::Oversampler::latency() : 0),
                       std::memory_order_relaxed);
    }

    double sr = 48000.0, laMs = -1.0;
    std::atomic<int> latency { 0 };
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
    StereoDelay delay;
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
            configure (*initial);
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
        isOpen = false;
        holdLeft = 0;
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
        const auto gap = st == Style::upward ? 0.0 : closeGap (p);
        if (gap <= 0.0)
            isOpen = false;
        const auto dryGain = std::clamp (p[dry], 0.0f, 1.0f);
        const auto outGain = (float) dbToGain (lin (p[output], -24.0, 24.0));
        const auto la = laOn > 0 ? lookaheadMs (p[lookahead]) : 0.0;
        gainDelay.setLength (std::max (0, laSamples - (int) std::lround (la * 0.001 * sr)));
        double maxGr = 0.0;
        const bool over = osFactor > 1;

        for (int i = 0; i < n; ++i)
        {
            float sa = external ? scL[i] : l[i], sb = external ? scR[i] : r[i];
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
            level = m > level ? m : levelRel * level + (1.0 - levelRel) * m;
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
            auto target = dbToGain (gateCurve (p, levelDb, isOpen ? gap : 0.0));
            if (duck) // ducking: an open gate is the deepest cut
                target = duckFloor / std::max (1e-9, target);
            // the gain moves in the linear domain: opening with the attack, closing (after hold) with the release
            if (target >= gain)
            {
                gain = aA * gain + (1.0 - aA) * target;
                holdLeft = holdSamples;
            }
            else if (holdLeft > 0)
                --holdLeft;
            else
                gain = aR * gain + (1.0 - aR) * target;
            maxGr = std::max (maxGr, -gainToDb (gain));
            const auto g = gainDelay.process ((float) gain);
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
    double level = 0.0, gain = 0.0;
    bool isOpen = false; // round tp172b: past the opening point (the hysteresis)
    int holdLeft = 0;
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
                                     { "Voicing", 0.0f } } }; // round tp172b2
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
                                     { "Oversampling", 0.0f } } }; // round 135
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
            case ratio: return ratioText (ratioOf (v));
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
                case fRatio: return ratioText (ratioOf (v));
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
