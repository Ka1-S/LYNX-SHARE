#pragma once

// Ember Saturator's measured Tape (round 190fj): a magnetic hysteresis core between a record and a playback EQ, with the
// level law and the speed-dependent losses fitted black-box on the recorded renders of the tape saturator Ember's
// substitute role plays (nothing decompiled, no captured IR, no code of any tape model used).
//
// Why a hysteresis core: the reference's 1 kHz staircases show the fundamental lagging the input as the level rises
// (−8.7° at its Drive +10 dB, −26.6° at +15 dB and the slow speed) and a static curve fitted per step leaves −16 / −9 dB
// of residual there, while any memoryless curve — with any EQ around it — keeps the fundamental's phase exactly. A
// Jiles–Atherton magnetisation loop is the textbook element with memory (D. C. Jiles, D. L. Atherton, "Theory of
// ferromagnetic hysteresis", J. Magn. Magn. Mater. 61, 1986; its use for tape: J. Chowdhury, "Real-time physical
// modelling for analog tape machines", DAFx-19). LYNX's own implementation of the published differential equation,
// solved per oversampled sample (midpoint Runge–Kutta on the field step, the Langevin function from a table), with a
// slow creep back to the anhysteretic curve so that no remanent offset outlives the signal (the reference's tails are
// exactly zero). Fitted on the renders, the loop stays narrow: its Langevin saturation, the record level law, the field
// clamp and the speed losses carry the match; the reference's lag grows as it saturates, which a JA loop's does not
// (it shrinks), so that lag is still open (round 190fj HANDOFF).
//
//   in ─► record EQ (a low and a high first-order shelf) ─► record level ─► field clamp ─► hysteresis M(H) (oversampled
//      by Ember's half-band chain) ─► small-signal make-up ─► playback EQ (the record EQ's inverse) ─► playback loss
//      (one-pole low-pass, head bump) ─► output shelf ─► trim

#include <algorithm>
#include <cmath>

namespace lynx::embertape
{
// one style's fitted constants at one speed (the tables live in MixSuite.cpp next to the style)
struct Model
{
    // hysteresis (M normalised to Ms = 1): anhysteretic width a, pinning k, reversibility c, coupling alpha
    double a = 0.35, k = 0.45, c = 0.25, alpha = 0.02;
    double gain0Db = 0.0;        // record level in dB at the reference's 0 dB drive (the field H = gain · input)
    double gainPerDb = 1.0;      // dB of record level per dB of Ember drive
    double clip = 0.0;           // field clamp (0 = none): H = clip · tanh (H / clip) (tanhFast)
    double preLowHz = 100.0, preLowDb = 0.0, preHighHz = 3000.0, preHighDb = 0.0;
    double lossHz = 0.0;         // playback loss: one-pole low-pass (0 = none)
    double dcHz = 0.0;           // high-pass after the loop (first-order; 0 = none)
    double relaxMs = 0.0;        // the magnetisation creeps back to the anhysteretic curve with this time constant (0 =
                                 // none): what keeps the loop from holding a remanent offset once the signal stops
    double bumpHz = 0.0, bumpDb = 0.0, bumpQ = 1.0; // head bump (peaking, 0 Hz = none)
    double postHighHz = 10000.0, postHighDb = 0.0;   // the output's own high shelf (first order)
    double trimDb = 0.0;
};

// the Langevin function L(q) = coth q − 1/q and its slope L'(q) = 1/q² − 1/sinh² q, exactly (series near 0)
inline void langevinExact (double q, double& value, double& slope) noexcept
{
    const double aq = std::abs (q);
    if (aq < 0.05)
    {
        const double q2 = q * q;
        value = q * (1.0 / 3.0 - q2 * (1.0 / 45.0 - q2 * (2.0 / 945.0)));
        slope = 1.0 / 3.0 - q2 * (1.0 / 15.0 - q2 * (2.0 / 189.0));
        return;
    }
    if (aq > 20.0)
    {
        value = (q > 0.0 ? 1.0 : -1.0) - 1.0 / q;
        slope = 1.0 / (q * q);
        return;
    }
    const double e = std::exp (-2.0 * aq);          // coth |q| = (1 + e) / (1 − e), sinh² q = (1 − e)² / (4 e)
    const double inv = 1.0 / (1.0 - e);
    value = (q > 0.0 ? 1.0 : -1.0) * (1.0 + e) * inv - 1.0 / q;
    slope = 1.0 / (q * q) - 4.0 * e * inv * inv;
}

// the same from a table (|q| ≤ 20 in 4096 steps, linear between them: within 2e-6), filled on first use — prepare()
// reaches it before any audio does (smallSignalGain)
struct LangevinTable
{
    static constexpr int size = 4096;
    static constexpr double range = 20.0, step = range / size;
    double value[size + 2] {}, slope[size + 2] {};
    LangevinTable() noexcept
    {
        for (int i = 0; i <= size + 1; ++i)
            langevinExact (i * step, value[i], slope[i]);
    }
};

inline void langevin (double q, double& value, double& slope) noexcept
{
    static const LangevinTable t;
    const double aq = std::abs (q);
    if (aq >= LangevinTable::range)
    {
        value = (q > 0.0 ? 1.0 : -1.0) - 1.0 / q;
        slope = 1.0 / (q * q);
        return;
    }
    const double x = aq * (1.0 / LangevinTable::step);
    const int i = (int) x;
    const double f = x - i;
    const double v = t.value[i] + f * (t.value[i + 1] - t.value[i]);
    value = q < 0.0 ? -v : v;
    slope = t.slope[i] + f * (t.slope[i + 1] - t.slope[i]);
}

// the Jiles–Atherton loop, one channel; process() takes the field H (any rate) and returns M
struct Hysteresis
{
    double a = 0.35, k = 0.45, c = 0.25, alpha = 0.02;
    double m = 0.0, h = 0.0;
    double invA = 1.0 / 0.35, cA = 0.25 / 0.35, caA = 0.25 * 0.02 / 0.35, omc = 0.75, omcK = 0.75 * 0.45;
    double relax = 0.0;            // per step: m += relax · (Man − m)
    double manNow = 0.0, ldNow = 1.0 / 3.0; // the Langevin function and its slope at the present state

    void set (double aIn, double kIn, double cIn, double alphaIn) noexcept
    {
        a = aIn; k = kIn; c = cIn; alpha = alphaIn;
        invA = 1.0 / a; cA = c / a; caA = c * alpha / a; omc = 1.0 - c; omcK = omc * k;
        langevin ((h + alpha * m) * invA, manNow, ldNow);
    }
    void setRelax (double ms, double rate) noexcept { relax = ms > 0.0 ? 1.0 - std::exp (-1000.0 / (ms * rate)) : 0.0; }
    // switched in on a running signal: start on the anhysteretic curve at the field there, not demagnetised
    void prime (double hNow) noexcept
    {
        h = hNow;
        double man, ld;
        langevin (hNow * invA, man, ld);
        langevin ((hNow + alpha * man) * invA, man, ld);
        m = man;
        langevin ((h + alpha * m) * invA, manNow, ldNow);
    }
    void reset() noexcept
    {
        m = h = 0.0;
        manNow = 0.0;
        ldNow = 1.0 / 3.0;
    }

    // dM/dH from the Langevin value / slope at the point (the reversible part's 1 / (1 − c α L' / a) as its series:
    // c α / a stays below 0.04 here, so the cubic term is under 1e-4 of it)
    double slopeFrom (double man, double ld, double mm, double delta) const noexcept
    {
        const double d = man - mm;
        double irr = 0.0;
        if (delta * d > 0.0)
        {
            double den = delta * omcK - alpha * d;
            if (std::abs (den) < 1.0e-9) den = den < 0.0 ? -1.0e-9 : 1.0e-9;
            irr = omc * d / den;
        }
        const double u = caA * ld;
        return (irr + cA * ld) * (1.0 + u * (1.0 + u));
    }

    // one field step: second-order Runge–Kutta (midpoint) on the field increment; the Langevin values at the end of the
    // step serve the creep and the next step's first slope
    double process (double hNew) noexcept
    {
        const double dh = hNew - h;
        double mm = m;
        if (dh != 0.0)
        {
            const double delta = dh > 0.0 ? 1.0 : -1.0;
            const double k1 = slopeFrom (manNow, ldNow, m, delta);
            const double mMid = m + 0.5 * dh * k1;
            double man, ld;
            langevin ((h + 0.5 * dh + alpha * mMid) * invA, man, ld);
            mm = m + dh * slopeFrom (man, ld, mMid, delta);
        }
        langevin ((hNew + alpha * mm) * invA, manNow, ldNow);
        if (relax > 0.0)
        {
            mm += relax * (manNow - mm);
            if (std::abs (mm) < 1.0e-30) mm = 0.0; // no denormals in a long silence
        }
        if (! std::isfinite (mm))
            mm = 0.0;
        m = std::clamp (mm, -1.5, 1.5);
        h = hNew;
        return m;
    }
};

// tanh from a table (|x| ≤ 8 in 4096 steps, linear between them: within 4e-7; ±1 beyond), for the field clamp
struct TanhTable
{
    static constexpr int size = 4096;
    static constexpr double range = 8.0, step = range / size;
    double v[size + 2] {};
    TanhTable() noexcept
    {
        for (int i = 0; i <= size + 1; ++i)
            v[i] = std::tanh (i * step);
    }
};

inline double tanhFast (double x) noexcept
{
    static const TanhTable t;
    const double ax = std::abs (x);
    if (ax >= TanhTable::range)
        return x > 0.0 ? 1.0 : -1.0;
    const double p = ax * (1.0 / TanhTable::step);
    const int i = (int) p;
    const double y = t.v[i] + (p - i) * (t.v[i + 1] - t.v[i]);
    return x < 0.0 ? -y : y;
}

// first-order section y = b0 x + b1 x[−1] − a1 y[−1] (double state)
struct First
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0, x1 = 0.0, y1 = 0.0;
    void reset() noexcept { x1 = y1 = 0.0; }
    double process (double x) noexcept
    {
        const double y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    // bilinear first-order shelf: `db` below (low) or above (high) `hz`, unity on the other side
    void shelf (double sr, double hz, double db, bool high) noexcept
    {
        const double g = std::pow (10.0, db / 20.0);
        const double t = std::tan (3.141592653589793 * std::min (hz, sr * 0.45) / sr);
        double c0, c1;
        if (high) { c0 = g + t; c1 = t - g; }
        else { c0 = 1.0 + g * t; c1 = g * t - 1.0; }
        const double d0 = 1.0 + t, d1 = t - 1.0;
        b0 = c0 / d0; b1 = c1 / d0; a1 = d1 / d0;
    }
    void highPass (double sr, double hz) noexcept
    {
        const double t = std::tan (3.141592653589793 * std::min (hz, sr * 0.45) / sr);
        b0 = 1.0 / (1.0 + t);
        b1 = -b0;
        a1 = (t - 1.0) / (1.0 + t);
    }
    void lowPass (double sr, double hz) noexcept
    {
        const double t = std::tan (3.141592653589793 * std::min (hz, sr * 0.45) / sr);
        b0 = b1 = t / (1.0 + t);
        a1 = (t - 1.0) / (1.0 + t);
    }
    void flat() noexcept { b0 = 1.0; b1 = a1 = 0.0; }
    // the state a steady (DC) input x leaves: a section switched in on a running signal starts there, not at zero;
    // returns its output for x
    double prime (double x) noexcept
    {
        x1 = x;
        y1 = x * (b0 + b1) / (1.0 + a1);
        return y1;
    }
};

struct Peak // RBJ peaking biquad (double state)
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
    void set (double sr, double hz, double db, double q) noexcept
    {
        if (hz <= 0.0 || db == 0.0) { b0 = 1.0; b1 = b2 = a1 = a2 = 0.0; return; }
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * 3.141592653589793 * std::min (hz, sr * 0.45) / sr;
        const double al = std::sin (w) / (2.0 * q), cw = std::cos (w);
        const double n = 1.0 + al / A;
        b0 = (1.0 + al * A) / n; b1 = -2.0 * cw / n; b2 = (1.0 - al * A) / n; a1 = -2.0 * cw / n; a2 = (1.0 - al / A) / n;
    }
    double prime (double x) noexcept
    {
        x1 = x2 = x;
        y1 = y2 = x * (b0 + b1 + b2) / (1.0 + a1 + a2);
        return y1;
    }
    double process (double x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// the small-signal gain of a loop (M per unit of field) at a record level: a −60 dBFS 1 kHz sine at `rate`, measured
// after one settling period over the next two (what the references' quiet stairs read as 0 dB)
inline double smallSignalGain (const Model& md, double fieldGain, double rate) noexcept
{
    Hysteresis hy;
    hy.set (md.a, md.k, md.c, md.alpha);
    hy.setRelax (md.relaxMs, rate);
    const double amp = 1.0e-3 * fieldGain;
    const int per = std::max (8, (int) std::lround (rate / 1000.0));
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < 3 * per; ++i)
    {
        const double s = std::sin (2.0 * 3.141592653589793 * i / per);
        const double y = hy.process (amp * s);
        if (i >= per) { sx += s * s; sy += s * y; }
    }
    const double g = sy / sx / amp;
    return g > 1.0e-9 ? g : 1.0;
}
} // namespace lynx::embertape
