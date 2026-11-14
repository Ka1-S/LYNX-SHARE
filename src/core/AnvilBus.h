#pragma once

// Anvil Bus (round 190fk): LYNX's drum-bus processor — one device that colours, drives, shapes and glues a drum group,
// and the engine an imported Ableton Live "Drum Buss" opens on (owner decision O20). Clean-room: every law below comes
// from LYNX's own measurements of Live 12.4's output on test signals (rounds 190ei / 190ek / 190fk: noise, a 1 kHz
// staircase, an impulse, a sweep and a drum loop through each setting); nothing of Live's code or content is used.
//
// Signal flow (one stereo path, every stage per channel):
//   Trim → Comp (round 190gx: before the curve, as Live's renders show) → colour pre-emphasis → Drive (×gain, the
//        type's curve, 4× oversampled) → Damp (one-pole low pass) → colour / type / Crunch EQ (magnitude-matched
//        sections, core/FacetEq.h) and Boom's resonant peak → Transients → Output → Dry/Wet against the raw input
//        (delayed by the oversampler's latency; Live's dry branch is the raw input with Comp on too, 190gu)
//   - the colour is there at every setting (the device is never transparent: a "smile" EQ and a soft saturator);
//   - Drive: three types — Soft folds the loudest peaks over (a sine folder at high drive), Medium and Hard clip with
//     their own EQ under the drive;
//   - Crunch lifts and grits the mid band, Boom is a tuned low resonance (frequency, decay), Comp a fixed drum
//     compressor (≈ 4 : 1 with make-up), Transients a level-independent attack / sustain shaper above the lows
//     (core/StrikeShaper.h's detector), Damp a gentle 6 dB/oct top cut.
// Header-only DSP, no allocation in process(); the Effect wrapper lives in core/MixSuite.cpp (core/AnvilSuite.inc).

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <utility>

#include "core/Biquad.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"
#include "core/StrikeShaper.h"

namespace lynx::anvil
{
enum Param { drive, driveType, crunch, damp, transients, boom, boomFreq, boomDecay, comp, trim, output, dryWet, count };
enum class Type { soft, medium, hard };

// ---- the knob laws (all parameters 0..1) ---------------------------------------------------------------------------
inline Type typeOf (float v) noexcept { return (Type) std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 2.0f), 0, 2); }
inline float typeValue (Type t) noexcept { return (float) (int) t * 0.5f; }
inline const char* typeName (Type t) noexcept
{
    static const char* const names[3] { "Soft", "Medium", "Hard" };
    return names[(int) t];
}
inline double dampHz (float v) noexcept { return 500.0 * std::pow (40.0, (double) std::clamp (v, 0.0f, 1.0f)); } // 500 Hz … 20 kHz (log)
inline float dampValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 1.0) / 500.0) / std::log (40.0), 0.0, 1.0); }
inline double transientsAmount (float v) noexcept { return std::clamp ((double) v * 2.0 - 1.0, -1.0, 1.0); } // −1 … +1
inline float transientsValue (double t) noexcept { return (float) std::clamp ((t + 1.0) * 0.5, 0.0, 1.0); }
inline double boomHz (float v) noexcept { return 30.0 + 60.0 * (double) std::clamp (v, 0.0f, 1.0f); } // 30 … 90 Hz
inline float boomHzValue (double hz) noexcept { return (float) std::clamp ((hz - 30.0) / 60.0, 0.0, 1.0); }
// Trim −70 … 0 dB, finer near 0 dB (0.5 = −17.5 dB)
inline double trimDb (float v) noexcept
{
    const auto u = 1.0 - (double) std::clamp (v, 0.0f, 1.0f);
    return -70.0 * u * u;
}
inline float trimValue (double db) noexcept { return (float) std::clamp (1.0 - std::sqrt (std::clamp (-db, 0.0, 70.0) / 70.0), 0.0, 1.0); }
// Output −40 … +12 dB, linear in dB (0 dB at 40 / 52)
inline double outputDb (float v) noexcept { return -40.0 + 52.0 * (double) std::clamp (v, 0.0f, 1.0f); }
inline float outputValue (double db) noexcept { return (float) std::clamp ((db + 40.0) / 52.0, 0.0, 1.0); }
inline bool on (float v) noexcept { return v >= 0.5f; }

// ---- the measured / fitted laws ------------------------------------------------------------------------------------
// The colour at every setting [FITTED to the all-zero device on noise at −20 dBFS rms: three bells and a gain over
// the pre-emphasis below, rms 0.14 dB from 31 Hz to 16 kHz (round 190ek)]
struct Bell
{
    double hz, db, q;
};
// The drive per type (Soft, Medium, Hard) and amount (0, 25, 50, 100 %): the small-signal gain into the curve, dB
// [MEASURED: 1 kHz at −60 dBFS, the all-zero device's +1.97 dB included]
// (round 190gx: a knot at 75 %, fitted for Soft on Live's 75 % staircase and noise — 0 dBFS folds to −12.1 dBFS there;
// Medium and Hard keep 190fk's straight line between 50 and 100 %)
constexpr int amountKnots = 5;
constexpr double driveAmounts[amountKnots] { 0.0, 0.25, 0.5, 0.75, 1.0 };
constexpr double driveDbTable[3][amountKnots] { { 1.97, 3.92, 8.91, 10.16, 11.52 }, { 1.97, 5.73, 9.48, 13.23, 16.98 }, { 1.97, 5.94, 7.75, 10.99, 14.23 } };

// Every constant the engine runs on, in one place (the defaults are the fitted values; the measurement harness may
// try others)
struct Tuning
{
    // colour pre-emphasis (before the curve): a peaking bell
    Bell pre { 100.0, 2.5, 0.8 };
    // colour after the curve: three bells (textbook Q) and the output gain
    Bell low { 96.1, 11.03, 0.288 }, high { 4578.3, 8.14, 0.262 }, dip { 89.0, -6.37, 0.711 };
    double colourGainDb = -2.63;
    double colourAt1kHzDb = 2.17; // what those bells give at 1 kHz (the curve's output is set around it)
    double stageOutDb = -0.04;    // the curve's output gain before the colour's 1 kHz gain is taken off
    // the type's EQ under drive, at 100 % (in proportion below; normalised at 1 kHz): Soft and Medium a bell, Hard a low
    // shelf (cookbook Q) [FITTED on noise at −20 dBFS rms, drive 100 %, round 190fk: Soft 1.00 → 0.78 dB rms, Medium
    // 3.29 → 3.20 (Live's estimate itself swings ±12 dB between 100 and 250 Hz there), Hard 1.13 → 0.74]
    double typeEqDb[3] { -4.196, -16.41, 13.98 }, typeEqHz[3] { 37.21, 180.6, 119.5 }, typeEqQ[3] { 2.229, 0.8927, 0.9781 };
    // the curve per type and amount (0, 25, 50, 100 %): y = soft (u) + (sin (k·u) / k − soft (u)) · m, u = the driven
    // input — a gentle saturation blended with a sine folder (unit slope at 0 for every m and k) [FITTED per measured
    // staircase, round 190fk: level rms 0.04 … 0.46 dB; at 0 % the plain soft curve (the blend fits the 1 kHz staircase a
    // little better, 0.115 vs 0.124 dB, but lifts the drum loop's kick attack: the plain curve keeps every 0 % case)]
    // (round 190fk, second pass: where the level-only fit raised the staircase's THD above the rack's, (m, k) were refitted
    // for the lowest level error with THD at or under the rack's: Soft 25 %, Medium 100 %, Hard 25 / 50 / 100 %)
    double curveM[3][amountKnots] { { 0.0, 0.1911, 0.28, 0.8045, 0.82 }, { 0.0, 0.40, 0.18, 0.14605, 0.1121 }, { 0.0, 0.1740, 0.0587, 0.06655, 0.0744 } };
    double curveK[3][amountKnots] { { 1.0, 1.5246, 1.40, 1.1705, 1.12 }, { 1.0, 1.26, 1.36, 1.26805, 1.1761 }, { 1.0, 1.4394, 1.491, 1.3283, 1.1656 } };
    // round 190gx: the type EQ's depth below 100 % = typeEqDb · amount^typeEqExp [FITTED on Live's noise at 25 / 50 / 75 %:
    // Soft and Medium keep the neutral device's shape until close to 100 % (noise TF rms Soft 0.34 / 0.58 / 0.72 →
    // 0.19 / 0.22 / 0.20 dB, Medium 1.49 / 2.95 → 0.23 / 0.41); Hard grows in proportion (190fk's law fits best)]
    double typeEqExp[3] { 8.0, 8.0, 1.0 };
    double driveDb[3][amountKnots] { { 1.97, 3.92, 8.91, 10.16, 11.52 }, { 1.97, 5.73, 9.48, 13.23, 16.98 }, { 1.97, 5.94, 7.75, 10.99, 14.23 } };
    // Crunch: the mid band's lift 20·log10 (1 + crunchK · amount) between two shelves, and its grit
    double crunchK = 2.14, crunchLowHz = 95.2, crunchLowQ = 0.743, crunchHighHz = 9041.0, crunchHighQ = 0.826;
    double crunchGrit = 0.0;
    // round 190gx: where Crunch's band lift sits — 0 = after the curve (190fk), 1 = before it, so the always-on curve
    // grits the lifted mid band; crunchDriveDb · amount more gain into the curve (taken off after it)
    int crunchPre = 0;
    double crunchDriveDb = 0.0;
    // Boom (amount 0.5 fitted at decay 0.2 and 1.0): a bell at the frequency and a resonant 12 dB/oct high pass
    double boomBellDb02 = 13.75, boomBellDb10 = 18.65, boomBellQ02 = 3.32, boomBellQ10 = 2.18;
    double boomCutQ02 = 0.581, boomCutQ10 = 0.476, boomCutAt02 = 1.470, boomCutAt10 = 0.816;
    // round 190gx: the same four at decay 0 and 0.5 (the defaults continue 190fk's straight line through 0.2 and 1.0),
    // the bell's growth with the amount ((amount / 0.5)^boomAmtExp of its linear lift) and the cut's follow of it
    // [FITTED on Live's decay 0 / 0.5 renders, sig + drums: 0.84 / 3.22 → 0.70 / 3.08 and 0.53 / 1.87 → 0.43 / 1.75 dB]
    double boomBellDb00 = 14.055, boomBellQ00 = 2.807, boomCutQ00 = 0.5143, boomCutAt00 = 1.659;
    double boomBellDb05 = 17.582, boomBellQ05 = 2.384, boomCutQ05 = 0.4297, boomCutAt05 = 1.191;
    double boomCutAmtExp = 1.0;
    // the bell's lift (its linear part, A − 1) and its Q at amount 0.25 and 1.0 relative to amount 0.5, straight in
    // between and to 0 below 0.25 [FITTED on Live's amount 0.25 / 1.0 renders, sig + drums: 1.66 / 4.98 → 0.27 / 0.43 and
    // 0.90 / 7.82 → 0.25 / 3.32 dB; Live's ring at 1.0 lasts: −20 dB/s at 50 Hz after an impulse (190fk: in proportion,
    // the Q fixed: lift 0.5 / 2.0, Q 1 / 1)]
    double boomGs25 = 0.1145, boomGs100 = 5.502, boomQs25 = 0.6161, boomQs100 = 2.138;
    // Damp: a first-order low pass; 0 = bilinear (zero at Nyquist), 1 = one-pole (impulse invariant: no zero at Nyquist,
    // the top flattens like Live's) [MEASURED re the all-zero device: −3 dB at 1020 / 6662 Hz for 1 / 6 kHz; the one-pole
    // fits 1 / 6 kHz at 0.36 / 0.36 dB rms against 1.44 / 1.18 for the bilinear one, round 190fk]
    // (round 190gx: the corner sits 6 % above the set frequency [FITTED on Damping 500 Hz / 2 kHz (190gu, sig + noise) and
    // 1 / 6 kHz (190ei): noise TF rms 0.38 / 0.36 / 0.39 / 0.38 → 0.24 / 0.26 / 0.26 / 0.24 dB])
    int dampKind = 1;
    double dampScale = 1.0604;
    // Comp: fixed feed-forward compressor (dB), peak detector, stereo linked; pre = before the curve. Round 190gx: Live's
    // renders with Comp + Drive 50 % and Comp + Trim −12 dB put it BEFORE the curve (right after Trim): the staircase with
    // Drive 50 % 1.63 → 0.17 dB, its THD rms 11.6 → 2.8 dB; refitted on all eight Comp cases (190ei d70 / d71, 190gu
    // k / w: every one closer, e.g. drum loop 0.73 → 0.48, Drive 50 % drums 2.65 → 0.73 dB)
    double compThresholdDb = -19.70, compRatio = 2.903, compKneeDb = 5.353, compAttackMs = 1.498, compReleaseMs = 283.8, compMakeupDb = 11.11;
    int compPre = 1;
    // Transients: attack / sustain dB per unit of the amount (positive and negative side) above the split, on Strike
    // Shaper's detector (its knobs end at ±15 / ±24 dB) [FITTED on the drum loop at −0.5 / +0.5 / +1.0, round 190fk:
    // segment levels rms 1.07 / 1.14 / 2.41 dB]
    // Round 190gx: Live's Transients move steady material too (a sustained pad, rms re the neutral device: −1.0 → −3.92
    // dB, −0.25 → −0.75, +0.25 → +1.15; the detector alone leaves a pad all but untouched), so a broadband level per unit
    // of the amount joins the shaper, refitted with it on the drum loop and the pad (190ei d40–d42 + 190gu t-cases, no
    // case worse: pad −1.0 3.53 → 3.05 dB, +0.25 0.98 → 0.80, drums −1.0 3.76 → 3.62, +0.25 0.75 → 0.60). Live's full
    // steady-state cut at −1.0 is not reached: a larger level term moves the drum loop's segments off (190ei renders).
    double tAttackPos = 22.96, tSustainPos = 17.18, tAttackNeg = 15.49, tSustainNeg = -15.47, tSplitHz = 330.5;
    double tLevelPos = 0.80, tLevelNeg = -0.633;
    // an extra fixed low cut after the colour (12 dB/oct, Q 0.707; 0 = none: Boom's high pass does that job)
    double lowCutHz = 0.0;
    // the output stage's ceiling (0 = none): a soft clip whose peaks stay under `ceiling`
    double ceiling = 0.0, ceilingKnee = 0.7;
};

// a value of a per-amount table (0, 25, 50, 100 %), piecewise linear in the amount
inline double byAmount (const double* ys, double amount) noexcept
{
    const auto x = std::clamp (amount, 0.0, 1.0);
    for (int i = 1; i < amountKnots; ++i)
        if (x <= driveAmounts[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - driveAmounts[i - 1]) / (driveAmounts[i] - driveAmounts[i - 1]);
    return ys[amountKnots - 1];
}
// The curve's small-signal drive gain (dB) for a type and amount
inline double driveGainDb (Type type, double amount) noexcept { return byAmount (driveDbTable[(int) type], amount); }
// Crunch's lift of its band, dB
inline double crunchDb (const Tuning& t, double amount) noexcept { return 20.0 * std::log10 (1.0 + t.crunchK * std::clamp (amount, 0.0, 1.0)); }
// Boom's bands for an amount, frequency and decay
struct BoomBands
{
    double hz = 50.0, bellDb = 0.0, bellQ = 2.0, cutHz = 10.0, cutQ = 0.5;
};
// a value at decay 0 / 0.2 / 0.5 / 1.0, piecewise linear in the decay
inline double byDecay (double d0, double d02, double d05, double d10, double decay) noexcept
{
    const auto d = std::clamp (decay, 0.0, 1.0);
    if (d <= 0.2) return d0 + (d02 - d0) * d / 0.2;
    if (d <= 0.5) return d02 + (d05 - d02) * (d - 0.2) / 0.3;
    return d05 + (d10 - d05) * (d - 0.5) / 0.5;
}
inline BoomBands boomBands (const Tuning& t, double amount, double hz, double decay) noexcept
{
    BoomBands b;
    const auto a = std::clamp (amount, 0.0, 1.0);
    b.hz = std::clamp (hz, 30.0, 90.0);
    const auto atHalf = std::pow (10.0, byDecay (t.boomBellDb00, t.boomBellDb02, t.boomBellDb05, t.boomBellDb10, decay) / 20.0) - 1.0;
    const auto rel = a / 0.5;
    // the amount's scale of the lift and of the Q (1 at 0.5; knots at 0.25 and 1.0; the lift → 0 at 0)
    const auto byAmt = [a] (double at25, double at100, bool toZero) {
        if (a <= 0.25) return toZero ? at25 * a / 0.25 : at25;
        if (a <= 0.5) return at25 + (1.0 - at25) * (a - 0.25) / 0.25;
        return 1.0 + (at100 - 1.0) * (a - 0.5) / 0.5;
    };
    b.bellDb = std::clamp (20.0 * std::log10 (1.0 + atHalf * byAmt (t.boomGs25, t.boomGs100, true)), 0.0, 40.0);
    b.bellQ = std::clamp (byDecay (t.boomBellQ00, t.boomBellQ02, t.boomBellQ05, t.boomBellQ10, decay) * byAmt (t.boomQs25, t.boomQs100, false), 0.5, 80.0);
    b.cutQ = std::clamp (byDecay (t.boomCutQ00, t.boomCutQ02, t.boomCutQ05, t.boomCutQ10, decay), 0.3, 1.5);
    const auto follow = rel <= 1.0 ? rel : (t.boomCutAmtExp == 1.0 ? 1.0 : std::pow (rel, t.boomCutAmtExp - 1.0));
    b.cutHz = std::max (10.0, b.hz * std::clamp (byDecay (t.boomCutAt00, t.boomCutAt02, t.boomCutAt05, t.boomCutAt10, decay), 0.5, 2.0) * follow);
    return b;
}
// The transient shaper's attack / sustain gains (dB) for an amount −1 … +1
inline std::pair<double, double> transientGainsDb (const Tuning& t, double amount) noexcept
{
    const auto a = std::clamp (amount, -1.0, 1.0);
    if (a >= 0.0)
        return { t.tAttackPos * a, t.tSustainPos * a };
    return { t.tAttackNeg * -a, t.tSustainNeg * -a };
}

// the base curve: a gentle symmetric saturation, unit slope at 0
inline float softCurve (float u) noexcept { return u / std::sqrt (1.0f + u * u); }
// sin (x) for the folder, without the library call (the curve runs at 4× the rate): x reduced to [−π/2, π/2], then a
// 9th-order Taylor polynomial (error under 4e-6 there)
inline float fastSin (float x) noexcept
{
    constexpr float twoPi = 6.28318530718f, pi = 3.14159265359f, halfPi = 1.57079632679f;
    x -= twoPi * std::nearbyint (x * (1.0f / twoPi));
    if (x > halfPi) x = pi - x;
    else if (x < -halfPi) x = -pi - x;
    const auto x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f)))));
}

class Engine
{
public:
    Tuning tune;

    static int latency() noexcept { return mixsuite::Oversampler::latency(); }

    void prepare (double rate) noexcept
    {
        sr = rate > 0.0 ? rate : 48000.0;
        for (auto& o : os)
            o.setFactor (4);
        shaper.prepare (sr);
        {
            // the DC guard after the curve: a first-order high pass at 8 Hz (bilinear)
            const auto k = std::tan (3.141592653589793 * 8.0 / sr);
            dcHp.set (1.0, -1.0, 0.0, 1.0 + k, k - 1.0, 0.0);
        }
        attackCoef = std::exp (-1.0 / (tune.compAttackMs * 0.001 * sr));
        releaseCoef = std::exp (-1.0 / (tune.compReleaseMs * 0.001 * sr));
        configured = false;
        reset();
    }
    void reset() noexcept
    {
        for (auto& o : os)
            o.reset();
        pre.reset();
        for (auto& b : eq)
            b.reset();
        for (auto& b : crunchEq)
            b.reset();
        dampLp.reset();
        boomBell.reset();
        dcHp.reset();
        splitLow[0] = splitLow[1] = 0.0;
        shaper.reset();
        grDb = 0.0;
        dryPos = 0;
        dryL.fill (0.0f);
        dryR.fill (0.0f);
        rampsReady = false;
        levelReady = false;
    }

    // last block's gain reduction of Comp (dB, ≥ 0) and the transient gains (dB), for the editor
    double lastCompDb = 0.0, lastAttackDb = 0.0, lastSustainDb = 0.0;

    void process (float* l, float* r, int n, const float* p) noexcept
    {
        for (int at = 0; at < n; at += maxBlock)
            chunk (l + at, r + at, std::min (maxBlock, n - at), p);
    }

private:
    static constexpr int maxBlock = 256;
    static constexpr int maxDry = 512; // a power of two (the ring's index is masked)

    struct Settings
    {
        Type type = Type::soft;
        float drive = -1.0f, crunch = -1.0f, damp = -1.0f, boom = -1.0f, boomFreq = -1.0f, boomDecay = -1.0f;
        bool operator== (const Settings& o) const noexcept
        {
            return type == o.type && drive == o.drive && crunch == o.crunch && damp == o.damp && boom == o.boom && boomFreq == o.boomFreq
                   && boomDecay == o.boomDecay;
        }
    };

    // one Facet-style band (core/FacetEq.h's analog prototypes and magnitude-matched sections) into eq[k]
    int band (int k, facet::Shape shape, double hz, double db, double q) noexcept { return band (eq, k, shape, hz, db, q); }
    template <std::size_t N>
    int band (std::array<Biquad, N>& dst, int k, facet::Shape shape, double hz, double db, double q) noexcept
    {
        const int cap = (int) N;
        EffectParams p {};
        p[(std::size_t) facet::scaleParam] = 0.5f; // 100 %
        p[(std::size_t) facet::param (0, 0)] = facet::shapeValue (shape);
        p[(std::size_t) facet::param (0, 1)] = facet::hzValue (hz);
        p[(std::size_t) facet::param (0, 2)] = facet::gainValue (facet::hasGain (shape) ? db : 0.0);
        const auto shelf = shape == facet::Shape::lowShelf || shape == facet::Shape::highShelf;
        // textbook Q → Facet's Q (bell and 12 dB cut: Qfacet / √2; shelf: cookbook Q = 0.7071 · Qfacet^0.4697)
        p[(std::size_t) facet::param (0, 3)] = facet::qValue (shelf ? std::pow (q / 0.7071, 1.0 / 0.4697) : std::sqrt (2.0) * q);
        p[(std::size_t) facet::param (0, 4)] = facet::slopeValue (12);
        std::array<facet::Section, facet::maxSections> s {};
        const int count = facet::design (p, 0, sr, s);
        for (int i = 0; i < count && k < cap; ++i, ++k)
        {
            auto& b = dst[(std::size_t) k];
            b.b0 = s[(std::size_t) i].b0;
            b.b1 = s[(std::size_t) i].b1;
            b.b2 = s[(std::size_t) i].b2;
            b.a1 = s[(std::size_t) i].a1;
            b.a2 = s[(std::size_t) i].a2;
        }
        return k;
    }

    void configure (const Settings& s) noexcept
    {
        const auto& t = tune;
        const auto ti = (int) s.type;
        const auto d = std::clamp ((double) s.drive, 0.0, 1.0);
        // the curve's input and output gains
        driveGain = std::pow (10.0, byAmount (t.driveDb[ti], d) / 20.0);
        const auto eqDb = t.typeEqDb[ti] * (t.typeEqExp[ti] == 1.0 ? d : std::pow (d, t.typeEqExp[ti]));
        // the type's EQ at 1 kHz (analog prototype), so the measured 1 kHz levels hold
        double typeAt1k = 0.0;
        if (std::abs (eqDb) > 1e-9)
        {
            const std::complex<double> sj (0.0, 1000.0 / t.typeEqHz[ti]);
            const auto q = t.typeEqQ[ti];
            const auto A = std::pow (10.0, eqDb / 40.0);
            const auto h = ti == 2 ? A * (sj * sj + std::sqrt (A) / q * sj + A) / (A * sj * sj + std::sqrt (A) / q * sj + 1.0)
                                   : (sj * sj + sj * A / q + 1.0) / (sj * sj + sj / (A * q) + 1.0);
            typeAt1k = 20.0 * std::log10 (std::abs (h));
        }
        stageGain = std::pow (10.0, (t.stageOutDb - t.colourAt1kHzDb - typeAt1k) / 20.0);
        // the type's curve at this amount
        foldMix = std::clamp (byAmount (t.curveM[ti], d), 0.0, 1.0);
        foldK = std::max (0.05, byAmount (t.curveK[ti], d));
        foldKf = (float) foldK;
        foldInv = (float) (1.0 / foldK);
        foldMixf = (float) foldMix;
        // Damp
        const auto dhz = dampHz (s.damp) * t.dampScale;
        dampOn = s.damp < 0.9999f;
        if (dampOn)
        {
            const auto f = std::min (dhz, sr * 0.45);
            if (t.dampKind == 1)
            {
                const auto a = 1.0 - std::exp (-2.0 * 3.141592653589793 * f / sr);
                dampLp.set (a, 0.0, 0.0, 1.0, a - 1.0, 0.0);
            }
            else
            {
                const auto k = std::tan (3.141592653589793 * f / sr);
                dampLp.set (k, k, 0.0, 1.0 + k, k - 1.0, 0.0);
            }
        }
        // the colour, the type's band, Crunch's shelves and Boom's bands
        pre.peaking (sr, t.pre.hz, t.pre.q, t.pre.db);
        int k = 0;
        k = band (k, facet::Shape::bell, t.low.hz, t.low.db, t.low.q);
        k = band (k, facet::Shape::bell, t.high.hz, t.high.db, t.high.q);
        k = band (k, facet::Shape::bell, t.dip.hz, t.dip.db, t.dip.q);
        if (t.lowCutHz > 0.0)
            k = band (k, facet::Shape::lowCut, t.lowCutHz, 0.0, 0.7071);
        if (std::abs (eqDb) > 1e-9)
            k = band (k, s.type == Type::hard ? facet::Shape::lowShelf : facet::Shape::bell, t.typeEqHz[ti], eqDb, t.typeEqQ[ti]);
        const auto cDb = crunchDb (t, s.crunch);
        crunchAmount = std::clamp ((double) s.crunch, 0.0, 1.0);
        crunchCount = 0;
        crunchPreGain = crunchPostGain = 1.0;
        if (cDb > 1e-9 && t.crunchPre == 0)
        {
            k = band (k, facet::Shape::lowShelf, t.crunchLowHz, -cDb, t.crunchLowQ);
            k = band (k, facet::Shape::highShelf, t.crunchHighHz, -cDb, t.crunchHighQ);
        }
        else if (cDb > 1e-9)
        {
            // round 190gx: the lift before the curve (the 1 kHz sine at −12 dBFS gains Live's THD: 1.0 → 2.3 / 4.4 / 12 %
            // at Crunch 0.25 / 0.5 / 1.0), its make-up gain there too; crunchDriveDb more into the curve, taken off after
            crunchCount = band (crunchEq, 0, facet::Shape::lowShelf, t.crunchLowHz, -cDb, t.crunchLowQ);
            crunchCount = band (crunchEq, crunchCount, facet::Shape::highShelf, t.crunchHighHz, -cDb, t.crunchHighQ);
            const auto extra = t.crunchDriveDb * crunchAmount;
            crunchPreGain = std::pow (10.0, (cDb + extra) / 20.0);
            crunchPostGain = std::pow (10.0, -extra / 20.0);
        }
        if (! configured || crunchCount != lastCrunchCount)
            for (auto& b : crunchEq)
                b.reset();
        lastCrunchCount = crunchCount;
        // Boom: its bell, and its resonant high pass — there at every setting (at 10 Hz with Boom at 0, which is also
        // what keeps the all-zero device's lowest octave where Live's is: +1.7 dB at 31 Hz)
        {
            const auto b = boomBands (t, s.boom, boomHz (s.boomFreq), s.boomDecay);
            // (round 190gx: the bell is the cookbook peak of the same analog prototype, so Live's long rings at high
            // amounts fit: Q and lift beyond the Facet section's ±30 dB / Q 28 range)
            const auto bellOn = s.boom > 1.0e-4f;
            if (bellOn)
                boomBell.peaking (sr, b.hz, b.bellQ, b.bellDb);
            if (bellOn != boomOn || ! configured)
                boomBell.reset();
            boomOn = bellOn;
            k = band (k, facet::Shape::lowCut, b.cutHz, 0.0, b.cutQ);
        }
        eqCount = k;
        colourGain = std::pow (10.0, (t.colourGainDb + (t.crunchPre == 0 ? cDb : 0.0)) / 20.0) * crunchPostGain;
        if (! configured)
            for (auto& b : eq)
                b.reset();
        configured = true;
    }

    float curve (float u) const noexcept
    {
        const auto y = softCurve (u);
        if (foldMix <= 0.0)
            return y;
        const auto f = fastSin (foldKf * u) * foldInv;
        return y + (f - y) * foldMixf;
    }

    void chunk (float* l, float* r, int n, const float* p) noexcept
    {
        Settings s;
        s.type = typeOf (p[driveType]);
        s.drive = p[drive];
        s.crunch = p[crunch];
        s.damp = p[damp];
        s.boom = p[boom];
        s.boomFreq = p[boomFreq];
        s.boomDecay = p[boomDecay];
        if (! configured || ! (s == last))
        {
            configure (s);
            last = s;
        }
        const auto trimTarget = (float) std::pow (10.0, trimDb (p[trim]) / 20.0);
        const auto outTarget = (float) std::pow (10.0, outputDb (p[output]) / 20.0);
        const auto wetTarget = std::clamp (p[dryWet], 0.0f, 1.0f);
        if (! rampsReady)
        {
            trimGain = trimTarget;
            outGain = outTarget;
            wet = wetTarget;
            rampsReady = true;
        }
        const auto& t = tune;

        // the raw input, for Dry/Wet (delayed by the oversampler's latency)
        std::array<float, maxBlock> inL, inR;
        std::copy (l, l + n, inL.begin());
        std::copy (r, r + n, inR.begin());

        // Trim
        const auto trimStep = (trimTarget - trimGain) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            trimGain += trimStep;
            l[i] *= trimGain;
            r[i] *= trimGain;
        }
        trimGain = trimTarget;

        const bool compOn = on (p[comp]);
        if (compOn && t.compPre != 0)
            compress (l, r, n);

        // pre-emphasis, (Crunch's lift when it sits before the curve), drive, the curve (oversampled), the DC guard, the
        // stage's output
        pre.processBlock (l, r, n);
        for (int k = 0; k < crunchCount; ++k)
            crunchEq[(std::size_t) k].processBlock (l, r, n);
        const auto g = (float) (driveGain * crunchPreGain);
        for (int i = 0; i < n; ++i)
        {
            l[i] *= g;
            r[i] *= g;
        }
        auto f = [this] (float u) noexcept { return curve (u); };
        os[0].process (l, n, f);
        os[1].process (r, n, f);
        const auto sg = (float) stageGain;
        dcHp.processBlock (l, r, n);
        for (int i = 0; i < n; ++i)
        {
            l[i] *= sg;
            r[i] *= sg;
        }
        if (dampOn)
            dampLp.processBlock (l, r, n);

        // the EQ
        for (int k = 0; k < eqCount; ++k)
            eq[(std::size_t) k].processBlock (l, r, n);
        if (boomOn)
            boomBell.processBlock (l, r, n);
        const auto cg = (float) colourGain;
        for (int i = 0; i < n; ++i)
        {
            l[i] *= cg;
            r[i] *= cg;
        }

        if (compOn && t.compPre == 0)
            compress (l, r, n);

        // Transients
        const auto amount = transientsAmount (p[transients]);
        lastAttackDb = lastSustainDb = 0.0;
        if (std::abs (amount) > 1.0e-4)
            shape (l, r, n, amount);
        else
            idleSplit (l, r, n);
        {
            const auto levelDb = amount >= 0.0 ? t.tLevelPos * amount : t.tLevelNeg * -amount;
            const auto target = (float) std::pow (10.0, levelDb / 20.0);
            if (! levelReady)
            {
                levelGain = target;
                levelReady = true;
            }
            if (target != 1.0f || levelGain != 1.0f)
            {
                const auto step = (target - levelGain) / (float) n;
                for (int i = 0; i < n; ++i)
                {
                    levelGain += step;
                    l[i] *= levelGain;
                    r[i] *= levelGain;
                }
            }
            levelGain = target;
        }

        // Output, the ceiling, Dry/Wet
        const auto outStep = (outTarget - outGain) / (float) n;
        const auto wetStep = (wetTarget - wet) / (float) n;
        const auto lat = latency();
        for (int i = 0; i < n; ++i)
        {
            outGain += outStep;
            wet += wetStep;
            auto a = l[i] * outGain, b = r[i] * outGain;
            if (t.ceiling > 0.0)
            {
                a = ceil (a);
                b = ceil (b);
            }
            dryL[(std::size_t) dryPos] = inL[(std::size_t) i];
            dryR[(std::size_t) dryPos] = inR[(std::size_t) i];
            const auto rd = (dryPos - lat + maxDry) & (maxDry - 1);
            dryPos = (dryPos + 1) & (maxDry - 1);
            l[i] = dryL[(std::size_t) rd] + (a - dryL[(std::size_t) rd]) * wet;
            r[i] = dryR[(std::size_t) rd] + (b - dryR[(std::size_t) rd]) * wet;
        }
        outGain = outTarget;
        wet = wetTarget;
    }

    float ceil (float x) const noexcept
    {
        const auto c = (float) tune.ceiling, knee = (float) tune.ceilingKnee * c, room = c - knee;
        const auto a = std::abs (x);
        if (a <= knee) return x;
        const auto y = knee + room * std::tanh ((a - knee) / room);
        return x < 0.0f ? -y : y;
    }

    // the fixed compressor: stereo-linked peak level in dB, hard or soft knee, attack / release on the reduction
    void compress (float* l, float* r, int n) noexcept
    {
        const auto& t = tune;
        const auto slope = 1.0 - 1.0 / std::max (1.0, t.compRatio);
        const auto makeup = std::pow (10.0, t.compMakeupDb / 20.0);
        double most = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto x = std::max (std::abs ((double) l[i]), std::abs ((double) r[i]));
            const auto lv = 20.0 * std::log10 (x + 1.0e-12);
            const auto over = lv - t.compThresholdDb;
            double want = 0.0;
            if (t.compKneeDb > 0.0 && std::abs (over) < t.compKneeDb * 0.5)
                want = slope * (over + t.compKneeDb * 0.5) * (over + t.compKneeDb * 0.5) / (2.0 * t.compKneeDb);
            else if (over > 0.0)
                want = slope * over;
            grDb = want > grDb ? want + (grDb - want) * attackCoef : want + (grDb - want) * releaseCoef;
            if (grDb < 1.0e-9) grDb = 0.0;
            most = std::max (most, grDb);
            const auto gain = (float) (makeup * std::exp (-grDb * 0.11512925464970229));
            l[i] *= gain;
            r[i] *= gain;
        }
        lastCompDb = most;
    }

    // Transients: the band above the split (a one-pole complement) through the attack / sustain detector
    void shape (float* l, float* r, int n, double amount) noexcept
    {
        const auto [aDb, sDb] = transientGainsDb (tune, amount);
        lastAttackDb = aDb;
        lastSustainDb = sDb;
        std::array<float, maxBlock> lowL, lowR;
        const auto c = 1.0 - std::exp (-2.0 * 3.141592653589793 * tune.tSplitHz / sr);
        for (int i = 0; i < n; ++i)
        {
            splitLow[0] += c * ((double) l[i] - splitLow[0]);
            splitLow[1] += c * ((double) r[i] - splitLow[1]);
            lowL[(std::size_t) i] = (float) splitLow[0];
            lowR[(std::size_t) i] = (float) splitLow[1];
            l[i] -= lowL[(std::size_t) i];
            r[i] -= lowR[(std::size_t) i];
        }
        float sp[strike::count] {};
        sp[strike::attack] = strike::attackValue (std::clamp (aDb, -15.0, 15.0));
        sp[strike::sustain] = strike::sustainValue (std::clamp (sDb, -24.0, 24.0));
        sp[strike::output] = 0.5f;
        sp[strike::guard] = 0.0f;
        sp[strike::mix] = 1.0f;
        sp[strike::link] = 1.0f;
        sp[strike::detectHp] = 0.0f;
        shaper.process (l, r, n, sp);
        for (int i = 0; i < n; ++i)
        {
            l[i] += lowL[(std::size_t) i];
            r[i] += lowR[(std::size_t) i];
        }
    }
    // keeps the split's low pass running while Transients is at 0 (a later move starts clean)
    void idleSplit (const float* l, const float* r, int n) noexcept
    {
        const auto c = 1.0 - std::exp (-2.0 * 3.141592653589793 * tune.tSplitHz / sr);
        for (int i = 0; i < n; ++i)
        {
            splitLow[0] += c * ((double) l[i] - splitLow[0]);
            splitLow[1] += c * ((double) r[i] - splitLow[1]);
        }
        if (std::abs (splitLow[0]) < 1e-20) splitLow[0] = 0.0;
        if (std::abs (splitLow[1]) < 1e-20) splitLow[1] = 0.0;
    }

    static constexpr int eqMax = 12;
    double sr = 48000.0;
    std::array<mixsuite::Oversampler, 2> os;
    Biquad pre, dampLp;
    std::array<Biquad, eqMax> eq {};
    int eqCount = 0;
    Biquad boomBell;
    bool boomOn = false;
    std::array<Biquad, 4> crunchEq {};
    int crunchCount = 0, lastCrunchCount = 0;
    double crunchPreGain = 1.0, crunchPostGain = 1.0;
    Biquad dcHp;
    strike::Shaper shaper;
    double splitLow[2] {};
    double driveGain = 1.0, stageGain = 1.0, colourGain = 1.0, foldMix = 0.0, foldK = 0.0, crunchAmount = 0.0;
    float foldKf = 1.0f, foldInv = 1.0f, foldMixf = 0.0f;
    bool dampOn = false, configured = false, rampsReady = false;
    double grDb = 0.0, attackCoef = 0.0, releaseCoef = 0.0;
    float trimGain = 1.0f, outGain = 1.0f, wet = 1.0f, levelGain = 1.0f;
    bool levelReady = false;
    Settings last;
    std::array<float, maxDry> dryL {}, dryR {};
    int dryPos = 0;
};
} // namespace lynx::anvil
