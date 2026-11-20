#include "core/ParametricEqFl.h"
#include "core/ParametricEqLinear.h"
#include "core/Simd.h"
#include "core/CpuFeatures.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

// bit-exactness needs every product and sum rounded on its own (no fused multiply-add)
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

// Exact Fruity Parametric EQ 2 / Fruity Parametric EQ engines (docs/analise/REA_rodada50.md). Every float/double
// rounding below is deliberate: it follows the order of operations observed in the plugins (§2–§3).

namespace lynx::peqfl
{
namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr double ln2 = 0.69314718055994529;
constexpr double halfLn2 = 0.34657359027997264;
constexpr double invSqrt2 = 0.70710678118654746;
constexpr double freqStep = 0.00015206580024203624; // log2(1000) / 65536 (§2.1)
constexpr float dbToLn = 0.115129255f;              // ln(10) / 20 as a float
constexpr float flushBelow = 5.9604644775390625e-08f; // 2^-24

// FL's UI type → internal design type (§2.2)
constexpr int internalType[8] = { 0, 1, 2, 4, 3, 6, 7, 8 };

// Per-stage tables of the "steep" orders, row = |order| (§2.2)
constexpr float steepCutQ[4][4] = { { 1, 1, 1, 1 }, { 1.98340166f, 0.749214649f, 1, 1 }, { 2.64303994f, 1.08151865f, 0.694244742f, 1 },
                                    { 3.57645154f, 1.14173281f, 1.04471076f, 0.655118585f } };
constexpr float steepShelfS[4][4] = { { 1, 1, 1, 1 }, { 2.63044715f, 0.615644455f, 1, 1 }, { 5.16122818f, 1.10927105f, 0.509867311f, 1 },
                                      { 10.3191233f, 1.51443529f, 0.694032848f, 0.552996695f } };
constexpr float steepPeakOffset[4][4] = { { 0, 0, 0, 0 }, { -0.25f, 0.25f, 0, 0 }, { -0.475f, 0, 0.475f, 0 },
                                          { -0.775f, -0.3f, 0.3f, 0.775f } };
constexpr float steepPeakWidth[4][4] = { { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1.57079637f, 1, 1 }, { 1, 1.57079637f, 1.57079637f, 1 } };
constexpr float steepPeakGain[4] = { 0.5f, 0.32125f, 0.2442f, 0.214445f };
constexpr float steepPeakScale[4] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f };

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t offset)
{
    std::int32_t v = 0;
    if (offset + 4 <= d.size())
        std::memcpy (&v, d.data() + offset, 4);
    return v;
}

// hyperbolic sine as the plugin's run-time library computes it
double sinhRtl (double x)
{
    if (std::abs (x) <= 1.0e-12)
        return 0.0;
    return (std::exp (x) - std::exp (-x)) / 2.0;
}

// Power(base, e) on floats for a non-integral exponent: exp(e·ln base)
float powerF (float base, float e)
{
    if (e == 0.0f)
        return 1.0f;
    if (base == 0.0f)
        return e > 0.0f ? 0.0f : 1.0f;
    if (std::trunc (e) == e && std::abs (e) < 2147483647.0f)
    {
        // integral exponent: repeated multiplication (never reached by the EQ's 1/(n+1), n ≥ 1)
        double r = 1.0, b = base;
        auto k = (long long) std::abs (e);
        while (k > 0)
        {
            if (k & 1)
                r *= b;
            b *= b;
            k >>= 1;
        }
        return (float) (e < 0 ? 1.0 / r : r);
    }
    return (float) std::exp ((double) e * std::log ((double) base));
}

// Biquad design from (A, alpha, sin ω, cos ω), per internal type (§2.3)
void designStage (Stage& st, int type, double A, double alpha, double sn, double cs)
{
    st.type = type;
    if (type >= 1 && type <= 5)
    {
        const double d = 1.0 / (alpha + 1.0);
        st.a1 = d * cs * -2.0;
        st.a2 = d * (1.0 - alpha);
        switch (type)
        {
            case 1: st.b1 = d * (1.0 - cs); st.b0 = st.b1 * 0.5; st.b2 = st.b0; break;
            case 2: st.b1 = 0.0; st.b0 = d * alpha; st.b2 = d * -alpha; break;
            case 3: st.b1 = st.a1; st.b0 = d; st.b2 = d; break;
            case 4: st.b1 = d * -(cs + 1.0); st.b0 = -st.b1 * 0.5; st.b2 = st.b0; break;
            default: st.b1 = st.a1; st.b0 = st.a2; st.b2 = 1.0; break;
        }
        return;
    }
    if (type == 6 || type == 8)
    {
        const double k = (A - 1.0) * cs;
        const double beta = alpha * sn;
        if (type == 6)
        {
            const double d = 1.0 / (A + 1.0 + k + beta);
            st.a1 = d * ((A - 1.0) + (A + 1.0) * cs) * -2.0;
            st.a2 = d * ((A + 1.0 + k) - beta);
            st.b1 = d * A * 2.0 * ((A - 1.0) - (A + 1.0) * cs);
            st.b0 = d * A * (((A + 1.0) - k) + beta);
            st.b2 = d * A * (((A + 1.0) - k) - beta);
        }
        else
        {
            const double d = 1.0 / (((A + 1.0) - k) + beta);
            st.a1 = d * ((A - 1.0) - (A + 1.0) * cs) * 2.0;
            st.a2 = d * (((A + 1.0) - k) - beta);
            st.b1 = d * A * -2.0 * ((A - 1.0) + (A + 1.0) * cs);
            st.b0 = d * A * (A + 1.0 + k + beta);
            st.b2 = d * A * ((A + 1.0 + k) - beta);
        }
        return;
    }
    if (type == 7)
    {
        const double d = 1.0 / (alpha / A + 1.0);
        st.a1 = d * cs * -2.0;
        st.a2 = d * (1.0 - alpha / A);
        st.b1 = st.a1;
        st.b0 = d * (alpha * A + 1.0);
        st.b2 = d * (1.0 - alpha * A);
        return;
    }
    st.b0 = 1.0, st.b1 = st.b2 = st.a1 = st.a2 = 0.0;
}

// ---------------------------------------------------------------------------------- "alternative design" (§4)
// With HQ on and the design option 1, Parametric EQ 2 asks Frequency Splitter's engine for each stage's
// coefficients: a magnitude-matched design (poles placed as in the analog prototype, zeros fitted so that the gain
// at DC, at Nyquist and at the centre/corner frequency follows the analog response; no frequency warping).
// Inputs as the plugin passes them: f = frequency / (host rate / 2), Q and gain in dB, each rounded to float.

double sqrtPos (double x) { return x > 0.0 ? std::sqrt (x) : 0.0; }
double minOf (double a, double b) { return a < b ? a : b; }
double maxOf (double a, double b) { return a > b ? a : b; }
constexpr double pi = 3.141592653589793;
constexpr double halfPi = 1.5707963267948966;

// the analog prototype's pole pair, sampled (impulse invariance): a1, a2
void analogPoles (double f, double q, double& a1, double& a2)
{
    const double decay = std::exp ((f * -halfPi) / q);
    const double d = 1.0 / ((q * 4.0) * q);
    const double c = q <= 0.5 ? std::cosh ((std::sqrt (d - 1.0) * pi) * f) : std::cos ((std::sqrt (1.0 - d) * pi) * f);
    a1 = (decay * c) * -2.0;
    a2 = decay * decay;
}

// common final step: numerator and denominator from their squared-magnitude terms (P0, P1 and the cross terms),
// normalised so that a0 = 1
void finishMatched (double pole0, double poleX, double zero0, double zeroX, double ampScale, Stage& st)
{
    const double rp = sqrtPos (pole0);
    const double hp = (rp + 1.0) * 0.5;
    const double norm = 1.0 / (sqrtPos (hp * hp + poleX) + hp);
    const double rz = sqrtPos (zero0);
    const double hz = (rz + 1.0) * 0.5;
    const double n0 = sqrtPos (hz * hz + zeroX) + hz;
    const double gain = norm * ampScale;
    st.b0 = gain * n0;
    st.b1 = gain * (1.0 - rz);
    st.b2 = gain * -(zeroX / n0);
    st.a1 = norm * (1.0 - rp);
    st.a2 = norm * -(norm * poleX);
}

void matchedLowPass (double f, double q, Stage& st)
{
    double k = 1.0 / (q * q);
    if (! (k < 1.99) && ! (k >= 2.0))
        k = 1.99;
    const double c0 = 0.6366197723675814 / f;
    const double nyq = (c0 * c0) * (k - 2.0);
    const double f2 = f * f, f4 = f2 * f2;
    const double fm1 = f2 - 1.0;
    const double den = ((k * f) * f) + fm1 * fm1;
    const double atNyq = f4 / den;                 // analog |H|² at the Nyquist frequency
    const double atNyqR = atNyq / (atNyq - 1.0);
    // general solution, fitted at min(f, 0.5)
    const double fm = minOf (f, 0.5);
    const double fmf = fm * f;
    const double dm = f2 - fm * fm;
    const double atM = f4 / (((fmf * fmf) * k) + dm * dm);
    const double atMR = atM / (atM - 1.0);
    const double sm = std::sin (fm * halfPi);
    const double pm = sm * sm;
    const double g1 = ((((atMR - atNyqR) * nyq) + (1.0 / pm)) - 1.0) / (atNyqR - (atMR * pm));
    const double g2 = (g1 + nyq) * atNyqR;
    double pole0 = (nyq - g2) + g1, poleX = g1 * -0.25, zero0 = -g2, zeroX = -0.0;
    // resonant solution (k < 2 and the peak below Nyquist), fitted at the resonance
    const double h = 1.0 - (k * 0.5);
    if (! (k >= 2.0) && ! (((h * f) * f) >= 1.0))
    {
        const double sr = std::sin (((sqrtPos (h) * f) * halfPi));
        const double pr = sr * sr;
        const double cr = 1.0 - pr;
        const double peak = 1.0 / (k - ((k * 0.25) * k));
        const double peakM1 = peak - 1.0;
        const double p0 = ((cr * peakM1) * cr) / (((peak - atNyq) * sr) * (pr * sr));
        const double z0 = (p0 / den) * f4;
        const double ipr2 = 1.0 / (pr * pr);
        const double px = ((((p0 - z0) - nyq) * 0.25) / peakM1) - (ipr2 * 0.25);
        pole0 = p0, poleX = px, zero0 = z0;
        zeroX = ((peakM1 * 0.25) * ipr2) + (px * peak);
    }
    finishMatched (pole0, poleX, zero0, zeroX, 1.0, st);
}

void matchedHighPass (double f, double q, Stage& st)
{
    const double k = 1.0 / (q * q);
    const double f2 = f * f;
    const double fm1 = f2 - 1.0;
    const double kf = f2 * k;
    const double c5 = 0.16425571607494938 / (f2 * f2);
    const double nyq = ((fm1 * fm1) + kf) * c5;
    const double hk = k * 0.5;
    const double h = 1.0 - hk;
    // general solution, fitted at min(f, 0.5)
    const double fm = minOf (f, 0.5);
    const double fm2 = fm * fm;
    const double dm = f2 - fm2;
    const double sm = std::sin (fm * halfPi);
    const double pm = sm * sm;
    const double gm = ((((((((kf * fm) * fm) + (dm * dm)) * (pm * c5)) / (fm2 * fm2)) - nyq) * 0.25) / (1.0 - pm)) - (0.25 / pm);
    const double gn = -0.25 - (nyq * 0.25);
    double pole0, poleX;
    if (k >= 2.0)
        pole0 = nyq, poleX = gm;
    else if (f2 >= h)
        pole0 = nyq, poleX = ((gm - gn) * minOf (((fm1 + hk) * q), 1.0)) + gn;
    else
    {
        // resonant solution, fitted at the resonance
        const double sr = std::sin (((f / sqrtPos (h)) * halfPi));
        const double pr = sr * sr;
        const double peak = 1.0 / (1.0 / (k - ((k * 0.25) * k)));
        const double px = (-0.25 / (pr * pr)) - ((peak * c5) * 0.25);
        pole0 = ((((c5 + c5) * pr) * peak) + 1.0) - ((4.0 - (pr * 8.0)) * px);
        poleX = px;
    }
    const double rz = sqrtPos (c5);
    // numerator: (1 − z⁻¹)² scaled
    const double rp = sqrtPos (pole0);
    const double hp = (rp + 1.0) * 0.5;
    const double norm = 1.0 / (sqrtPos (hp * hp + poleX) + hp);
    const double n0 = rz * 0.5;
    st.b0 = norm * n0;
    st.b1 = norm * -rz;
    st.b2 = norm * ((c5 * 0.25) / n0);
    st.a1 = norm * (1.0 - rp);
    st.a2 = norm * -(norm * poleX);
}

void matchedPeak (double f, double q, double A, Stage& st)
{
    if (std::abs (A - 1.0) < 0.001)
    {
        st.b0 = 1.0, st.b1 = st.b2 = st.a1 = st.a2 = 0.0;
        return;
    }
    const double q2 = q * q;
    const double bwRoot = sqrtPos ((q2 + 0.5) + sqrtPos (q2 + 0.25));
    const double iq2 = 1.0 / q2;
    const double f2 = f * f;
    const double fm1 = f2 - 1.0;
    const double fm1sq = fm1 * fm1;
    const double ef = ((f2 * A) * iq2) + fm1sq;
    const double fq = f2 * iq2;
    const double iA = 1.0 / A;
    double pole0, poleX, zero0, zeroX;
    if (f >= 1.0)
    {
        // centre at or above Nyquist: fitted at the band edge
        const double e = q / bwRoot;
        const double se = std::sin (e * halfPi);
        const double pe = se * se;
        const double e2 = e * e;
        const double ef2 = e2 * f2;
        const double de = f2 - e2;
        const double dsq = de * de;
        const double atE = ((((ef2 * A) * iq2) + dsq) / (((ef2 * iq2) * iA) + dsq));
        const double atN = ef / ((fq * iA) + fm1sq);
        pole0 = ((1.0 - pe) * (atE - 1.0)) / ((atN - atE) * pe);
        poleX = 0.0;
        zero0 = atN * pole0;
        zeroX = 0.0;
    }
    else
    {
        const double sc = std::sin (f * halfPi);
        const double pc = sc * sc;
        const double ipc = 1.0 / pc;
        const double se = std::sin ((((f * q) / bwRoot) * halfPi));
        const double pe = se * se;
        const double A2 = A * A;
        const double t29 = 1.0 - A2;
        const double t30 = t29 * ipc;
        const double t31 = ((((ipc * pe) - 2.0) * t30) + ((1.0 - A) / pe)) / (A - A2);
        const double atN = ef / ((fq / A) + fm1sq);
        const double x = (((((ipc - 2.0) * t30) + (1.0 - atN)) / (atN - A2)) - t31) / (1.0 - pe);
        const double t34 = t31 - (x * pe);
        const double t41 = (t29 / (pc * pc)) + (x * A2);
        pole0 = (t34 + 1.0) + x;
        poleX = x * -0.25;
        zero0 = ((((((A2 - 1.0) + A2) - 1.0) * ipc) + (t34 * A2)) + 1.0) + t41;
        zeroX = t41 * -0.25;
    }
    finishMatched (pole0, poleX, zero0, zeroX, 1.0, st);
}

// magnitude-squared ratio of the shelf's analog response at normalised frequency² m2 (zero term over pole term)
double shelfRatio (double zeroBase, double poleBase, double m2, double q2, double scale)
{
    const double dz = zeroBase - m2, dp = poleBase - m2;
    return ((((dz * dz) * q2) + (zeroBase * m2)) * scale) / (((dp * dp) * q2) + (poleBase * m2));
}

void matchedShelf (bool low, double f, double q, double A, Stage& st)
{
    if (std::abs (A - 1.0) < 0.001)
    {
        st.b0 = 1.0, st.b1 = st.b2 = st.a1 = st.a2 = 0.0;
        return;
    }
    const double q2 = q * q;
    const double spread = maxOf (1.0 / A, A);
    const double s1 = sqrtPos (spread);
    const double s2 = spread > 0.0 ? sqrtPos (std::sqrt (spread)) : 0.0;
    const double x = low ? s2 * f : f / s2;
    const double base = low ? (x * x) / (s1 * s1) : x * x;
    const double lo = base / minOf (A, 1.0), hi = maxOf (A, 1.0) * base;
    const double P = low ? lo : hi, Z = low ? hi : lo;           // pole / zero corner (squared)
    const double scale = low ? 1.0 / (A * A) : A * A;
    auto ratio = [&] (double m2) { return shelfRatio (Z, P, m2, q2, scale); };

    // analog-pole solution (Q² ≤ 0.6)
    const double iq2 = 1.0 / q2;
    const double wp = sqrtPos (P) * halfPi;
    const double decay = std::exp ((-1.0 / q) * wp);
    const double c4 = 4.0 - iq2;
    const double osc = q <= 0.5 ? std::cosh (wp * sqrtPos (iq2 - 4.0)) : (c4 > 0.0 ? std::cos (wp * std::sqrt (c4)) : std::cos (wp * 0.0));
    const double pa1 = (osc * decay) * -2.0, pa2 = decay * decay;
    const double sumN = (1.0 - pa1) + pa2;
    const double sumN2 = sumN * sumN;
    const double sumD = (pa1 + 1.0) + pa2;
    const double isumD2 = 1.0 / (sumD * sumD);

    // matched solution: fit points
    const double k2 = iq2 - 2.0;
    const double rr = -((s1 * s1 + 1.0) / (k2 * s1));
    const double r = rr - sqrtPos ((rr * rr) - 1.0);
    const double v = low ? (1.0 / s1) * r : s1 * r;
    const double y = x * sqrtPos (v);
    const double c2 = k2 * 0.4052847345693511;
    const double ends = ((0.4052847345693511 / P) - (0.4052847345693511 / Z)) * k2;
    const double m = minOf (low ? (1.0 / s1) * x : x, 0.5);
    const double fm = ratio (m * m);
    const double fmR = fm / (fm - 1.0);
    const double f1 = ratio (1.0);
    const double f1R = f1 / (f1 - 1.0);
    const double sm = std::sin (m * halfPi);
    const double pm = sm * sm;
    const double g1 = ((((fmR - f1R) * ends) + (1.0 / pm)) - 1.0) / (f1R - (fmR * pm));
    const double g2 = -((ends + g1) * f1R) - 1.0;
    const double sy = std::sin (y * halfPi);
    const double py = sy * sy;
    const double sr = std::sin (r * halfPi);
    const double ipr = 1.0 / (sr * sr);
    const double wv = low ? v : r / s1;
    const double zv = low ? maxOf (A, 1.0) * wv : wv / minOf (A, 1.0);
    const double pv = low ? wv / minOf (A, 1.0) : wv * maxOf (A, 1.0);
    const double fv1 = shelfRatio (zv, pv, 1.0, q2, scale);
    const double r2 = r * r;
    const double fvr = shelfRatio (zv, pv, r2, q2, scale);
    const double expo = std::log (((r * sqrtPos (A)) * ((((2.0 - (ipr + ipr)) * (fv1 - 1.0)) * (fvr - 1.0)) / (fv1 - fvr)))
                                  / ((low ? A - 1.0 : 1.0 - A) * c2))
                        / std::log (r);
    const double h = std::pow (y, expo) * (((Z - P) * c2) / (P * Z));
    const double fy = ratio (y * y);
    const double fyM1 = fy - 1.0;
    const double u = (-2.0 / py) - (h / fyM1);
    const double yr = y / r;
    const double fyr = ratio (yr * yr);
    const double fyrM1 = fyr - 1.0;
    const double syr = std::sin (yr * halfPi);
    const double pyr = syr * syr;
    const double dyr = fyr - fy;
    const double w87 = (((py * fyrM1) - (fyM1 * pyr)) * -2.0) / ((py * dyr) * pyr);
    const double py2 = py * py;
    const double t89 = fyM1 / py2;
    const double t90 = u - h;
    const double t91 = (((((f1 + t89) - 1.0) - t90) + (f1 * u)) / (fy - f1));
    const double t92 = pyr * py;
    const double t93 = ((py2 * fyrM1) - ((pyr * pyr) * fyM1)) / ((t92 * t92) * dyr);
    const bool above = y >= r, atNyq = y >= 1.0;
    const double t94 = above ? (atNyq ? g1 : t91) : t93;
    const double t104 = above ? (atNyq ? 0.0 : (fy * t91) - t89) : (fy * t93) - t89;
    const double pz = (f1 * sumN2);

    double pole0, poleX, zero0, zeroX;
    if (q2 <= 0.6)
    {
        const double mz = minOf (low ? x / sqrtPos (s1) : x * sqrtPos (s1), 0.2);
        const double mz2 = mz * mz;
        const double fz = ratio (mz2);
        const double sz = std::sin (mz * halfPi);
        const double pz2 = sz * sz;
        pole0 = sumN2 * isumD2;
        poleX = (pa2 * -4.0) * isumD2;
        zero0 = pz * isumD2;
        zeroX = (((((((sumN2 * fz) - pz) / (1.0 - pz2)) + ((fz - 1.0) / (isumD2 * pz2))) * 0.25) + ((pa2 * -4.0) * fz)) * isumD2);
    }
    else
    {
        pole0 = ((above ? (atNyq ? (ends + g2) : u) : w87) + 1.0) + t94;
        poleX = t94 * -0.25;
        zero0 = ((above ? (atNyq ? g2 : t90) : (((fyM1 + fyM1) / py) + (fy * w87))) + 1.0) + t104;
        zeroX = t104 * -0.25;
    }
    finishMatched (pole0, poleX, zero0, zeroX, low ? A : 1.0, st);
}

// FL internal type → the engine's design (§4.1); q, gain as the plugin computes them per stage
void designAlternative (Stage& st, int type, double freqHz, double q, double gain, double hostRate)
{
    st.type = type;
    const double f = (double) (float) ((freqHz / hostRate) * 2.0);
    const double qq = (double) (float) q;
    const double A = std::pow (10.0, (double) (float) gain * 0.05);
    switch (type)
    {
        case 1: matchedLowPass (f, qq, st); break;
        case 4: matchedHighPass (f, qq, st); break;
        case 7: matchedPeak (f, qq, A, st); break;
        case 6: matchedShelf (true, f, qq, A, st); break;
        case 8: matchedShelf (false, f, qq, A, st); break;
        case 2: // band pass: analog poles, numerator fitted to the response at DC, Nyquist and f/2
        {
            double a1, a2;
            analogPoles (f, qq, a1, a2);
            const double s = std::sin (f * halfPi);
            const double p1 = s * s, p0 = 1.0 - p1;
            const double e1 = (1.0 - a1) + a2, n1 = e1 * e1;
            const double e0 = (a1 + 1.0) + a2, n0 = e0 * e0;
            const double n2 = a2 * -4.0;
            const double t14 = (((p0 - p1) * 4.0) * n2) + (n1 - n0);
            const double r1 = ((((p1 * n1) + (p0 * n0)) + (((p0 * 4.0) * p1) * n2)) - (t14 * p1)) / ((p1 * 4.0) * p1);
            const double t16 = (((p1 - p0) * 4.0) * r1) + t14;
            st.b1 = std::sqrt (t16) * -0.5;
            st.b0 = (std::sqrt ((t16 * 0.25) + r1) - st.b1) * 0.5;
            st.b2 = -st.b0 - st.b1;
            st.a1 = a1, st.a2 = a2;
            break;
        }
        case 3: // notch: analog poles, zeros on the unit circle at f, unity gain at DC
        {
            double a1, a2;
            analogPoles (f, qq, a1, a2);
            const double c = std::cos (f * pi) * -2.0;
            const double g = ((a1 + 1.0) + a2) / ((1.0 + c) + 1.0);
            st.b0 = 1.0 * g, st.b1 = g * c, st.b2 = g * 1.0;
            st.a1 = a1, st.a2 = a2;
            break;
        }
        default: // not handled by the engine: the plugin's stage keeps whatever the call left (not confirmed)
            st.b0 = 1.0, st.b1 = st.b2 = st.a1 = st.a2 = 0.0;
            break;
    }
}

// Parametric EQ 2 band design (§2.1–§2.2)
void designPeq2 (const Settings& s, int b, double rate, Band& band)
{
    const int ui = s.type[(std::size_t) b];
    const int t = ui >= 0 && ui < 8 ? internalType[ui] : 0;
    const auto freq = (float) (std::exp ((double) (float) s.freq[(std::size_t) b] * freqStep * ln2) * 20.0);
    const int order = std::clamp (s.order[(std::size_t) b], -3, 3);
    const int n = std::abs (order);
    const bool steep = order < 0;
    const auto inv = (float) (1.0 / (n + 1));
    const auto kbp = (float) ((double) ((float) std::exp ((double) (dbToLn * 3.0f)) / (float) std::exp ((double) (dbToLn * 6.0f)))
                              * std::sqrt ((double) (n + 1)));
    const auto knotch = 1.0f / kbp;

    band.type = t;
    band.stages = 0;
    if (t < 1)
    {
        band.active = 0;
        return;
    }

    const auto w = (float) s.width[(std::size_t) b] * 1.52587890625e-05f;
    float gain = 0.0f;
    if (t < 6)
        band.active = -1;
    else
    {
        gain = (float) s.gain[(std::size_t) b] * 0.01f;
        band.active = gain == 0.0f ? (int) rate / 5 : -1;
    }

    const double wk = (1.0 / rate) * twoPi;
    const bool alternative = s.processing == Processing::hqAlternative;
    band.stages = n + 1;
    for (int k = 0; k <= n; ++k)
    {
        double A = 0.0, alpha = 0.0, sn = 0.0, cs = 0.0;
        // what the plugin hands to the alternative design: frequency, Q and gain of the stage (§4.1)
        double altFreq = (double) freq, altQ = 0.0, altGain = 0.0;
        if (t == 2 || t == 3)
        {
            const double om = std::min ((double) freq * wk, 3.138451099395752);
            sn = std::sin (om), cs = std::cos (om);
            const double bw = ((double) w * 4.9 + 0.1) * (double) (t == 2 ? kbp : knotch);
            alpha = sn * sinhRtl ((halfLn2 * bw * om) / sn);
            altQ = 1.0 / (sinhRtl (halfLn2 * bw) * 2.0);
        }
        else if (t == 1 || t == 4)
        {
            const double om = std::min ((double) freq * wk, 3.138451099395752);
            sn = std::sin (om), cs = std::cos (om);
            double q = (double) (1.0f - w);
            q = q * q * q * 9.9 + 0.1;
            if (n > 0)
                q = (double) powerF ((float) q, inv);
            if (steep)
                q = q * (double) steepCutQ[n][k];
            alpha = (sn * invSqrt2) / q;
            altQ = q * std::sqrt (0.5);
        }
        else if (t == 7)
        {
            double om = std::min ((double) freq * wk, 2.1991147994995117);
            double bw = (double) w * 4.9 + 0.1;
            double g;
            if (n < 1 || ! steep)
                g = (double) gain * (double) inv * 0.5;
            else
            {
                const double scale = steepPeakScale[n];
                const double shifted = (double) freq * std::exp ((double) steepPeakOffset[n][k] * bw * scale * ln2);
                bw = bw * scale * (double) steepPeakWidth[n][k];
                g = n == 1 ? (double) steepPeakGain[1] - (bw - 1.0) * 0.01 : (double) steepPeakGain[n];
                g = (double) gain * g;
                om = std::min ((double) (float) shifted * wk, 2.5132741928100586);
                altFreq = shifted;
            }
            sn = std::sin (om), cs = std::cos (om);
            A = (double) (float) std::exp ((double) ((float) g * dbToLn));
            alpha = sn * sinhRtl ((halfLn2 * bw * om) / sn);
            altQ = 1.0 / (sinhRtl (halfLn2 * bw) * 2.0);
            altGain = g * 2.0;
        }
        else // shelves (and the unused all-pass type)
        {
            const double om = std::min ((double) freq * wk, 3.138451099395752);
            sn = std::sin (om), cs = std::cos (om);
            A = (double) (float) std::exp ((double) (gain * inv * 0.5f * dbToLn));
            double S = (double) (1.0f - w) * 1.7192982456140353 + 0.3333333333333333;
            if (steep)
                S = S * (double) steepShelfS[n][k];
            alpha = std::sqrt (A) * std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
            altQ = 1.0 / std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
            altGain = (double) (gain * inv);
        }
        if (alternative)
            designAlternative (band.stage[(std::size_t) k], t, altFreq, altQ, altGain, rate);
        else
            designStage (band.stage[(std::size_t) k], t, A, alpha, sn, cs);
    }
}

// Parametric EQ (v1): one biquad per band (§3)
void designPeq1 (const Settings& s, int b, double rate, Band& band)
{
    const int ui = s.type[(std::size_t) b];
    const int t = ui >= 0 && ui < 8 ? internalType[ui] : 0;
    band.type = t;
    band.stages = 0;
    if (t < 1)
    {
        band.active = 0;
        return;
    }
    const auto x = (float) ((double) s.freq[(std::size_t) b] * 1.52587890625e-05);
    const auto e = (float) (std::exp ((double) x * std::log (101.0)) - 1.0);
    auto freq = (float) ((double) e * 159.9 + 10.0);
    const auto w = (float) ((double) s.width[(std::size_t) b] * 1.52587890625e-05);
    const auto gain = (float) s.gain[(std::size_t) b] * 0.01f;
    band.active = gain == 0.0f && t > 5 ? (int) rate / 5 : -1;

    const auto nyquist = (float) ((double) (int) rate * 0.4999); // §3.2: the rate object's limit
    if (freq > nyquist)
        freq = nyquist;
    const double om = (double) freq * ((1.0 / rate) * twoPi);
    const double sn = std::sin (om), cs = std::cos (om);
    double A = 0.0, alpha;
    if (t >= 1 && t <= 5)
        alpha = sn / (((double) w * 18.9 + 0.1) * 2.0);
    else
    {
        const double ge = (double) gain * 0.025;
        A = ge == 0.0 ? 1.0 : std::exp (ge * std::log (10.0));
        if (t == 7)
        {
            const auto q = (float) (std::exp ((double) w * std::log ((double) (1000.0f + 1.0f))) - 1.0);
            alpha = sn / (((double) q * 0.0999 + 0.1) * 2.0);
        }
        else
            alpha = std::sqrt ((A * A + 1.0) / ((double) w * 1.9 + 0.1) - (A - 1.0) * (A - 1.0));
    }
    band.stages = 1;
    designStage (band.stage[0], t, A, alpha, sn, cs);
}

// One stage over the block, the plain loop: the reference runStages is checked against (stagesMatchReference)
void runStage (Stage& st, float* io, int n) noexcept
{
    double x1l = st.s[0], x1r = st.s[1], x2l = st.s[2], x2r = st.s[3];
    double y1l = st.s[4], y1r = st.s[5], y2l = st.s[6], y2r = st.s[7];
    const double b0 = st.b0, b1 = st.b1, b2 = st.b2, a1 = st.a1, a2 = st.a2;
    for (int i = 0; i < n; ++i)
    {
        const double xl = io[2 * i], xr = io[2 * i + 1];
        const double yl = xl * b0 + (((x1l * b1 - y2l * a2) + x2l * b2) - y1l * a1);
        const double yr = xr * b0 + (((x1r * b1 - y2r * a2) + x2r * b2) - y1r * a1);
        x2l = x1l, x2r = x1r, x1l = xl, x1r = xr;
        y2l = y1l, y2r = y1r, y1l = yl, y1r = yr;
        io[2 * i] = (float) yl;
        io[2 * i + 1] = (float) yr;
    }
    const double out[8] = { x1l, x1r, x2l, x2r, y1l, y1r, y2l, y2r };
    // end of call: state values below 2^-24 in magnitude are cleared (§2.4)
    for (int k = 0; k < 8; ++k)
        st.s[k] = std::abs (out[k]) < (double) flushBelow ? 0.0 : out[k];
}

// A cascade of `count` stages over the block (round 190kr): the same as runStage on each one after another (each stage's
// float output is the next one's input, the end-of-call flush per stage), but sample by sample, so the stages' recursions
// overlap, with the interleaved left / right pair in the two lanes of an SSE2 double vector: per lane the same IEEE
// double operations in the same order as runStage.
// (always inlined: runStagesAvx2 below takes the same body in VEX encoding)
template <int count>
__attribute__ ((always_inline)) inline void runStagesN (Stage* const* st, float* io, int n) noexcept
{
    __m128d x1[count], x2[count], y1[count], y2[count];
    for (int k = 0; k < count; ++k)
    {
        const auto* s = st[k]->s; // x1l x1r x2l x2r y1l y1r y2l y2r
        x1[k] = _mm_loadu_pd (s), x2[k] = _mm_loadu_pd (s + 2), y1[k] = _mm_loadu_pd (s + 4), y2[k] = _mm_loadu_pd (s + 6);
    }
    for (int i = 0; i < n; ++i)
    {
        __m128 f = _mm_castpd_ps (_mm_load_sd ((const double*) (io + 2 * i)));
        for (int k = 0; k < count; ++k)
        {
            const auto& c = *st[k];
            const __m128d x = _mm_cvtps_pd (f);
            const __m128d inner = _mm_sub_pd (_mm_add_pd (_mm_sub_pd (_mm_mul_pd (x1[k], _mm_set1_pd (c.b1)), _mm_mul_pd (y2[k], _mm_set1_pd (c.a2))),
                                                          _mm_mul_pd (x2[k], _mm_set1_pd (c.b2))),
                                               _mm_mul_pd (y1[k], _mm_set1_pd (c.a1)));
            const __m128d y = _mm_add_pd (_mm_mul_pd (x, _mm_set1_pd (c.b0)), inner);
            x2[k] = x1[k], x1[k] = x, y2[k] = y1[k], y1[k] = y;
            f = _mm_cvtpd_ps (y);
        }
        _mm_store_sd ((double*) (io + 2 * i), _mm_castps_pd (f));
    }
    for (int k = 0; k < count; ++k)
    {
        double out[8];
        _mm_storeu_pd (out, x1[k]), _mm_storeu_pd (out + 2, x2[k]), _mm_storeu_pd (out + 4, y1[k]), _mm_storeu_pd (out + 6, y2[k]);
        for (int j = 0; j < 8; ++j) // end of call: state values below 2^-24 in magnitude are cleared (§2.4)
            st[k]->s[j] = std::abs (out[j]) < (double) flushBelow ? 0.0 : out[j];
    }
}

#if LYNX_CPU_DISPATCH
// round 190kz: the same cascade compiled for AVX2 (picked at run time, src/core/CpuFeatures.h): the same SSE2 operations,
// VEX-encoded (three-operand forms, fewer register copies), the same bits; no FMA (the target list has none)
LYNX_TARGET_AVX2 void runStagesAvx2 (Stage* const* st, int count, float* io, int n) noexcept
{
    for (; count >= 4; st += 4, count -= 4)
        runStagesN<4> (st, io, n);
    switch (count)
    {
        case 1: runStagesN<1> (st, io, n); return;
        case 2: runStagesN<2> (st, io, n); return;
        case 3: runStagesN<3> (st, io, n); return;
        default: return;
    }
}
#endif

void runStages (Stage* const* st, int count, float* io, int n) noexcept
{
#if LYNX_CPU_DISPATCH
    if (cpu::hasAvx2())
    {
        runStagesAvx2 (st, count, io, n);
        return;
    }
#endif
    switch (count)
    {
        case 0: return;
        case 1: runStagesN<1> (st, io, n); return;
        case 2: runStagesN<2> (st, io, n); return;
        case 3: runStagesN<3> (st, io, n); return;
        default:
            runStagesN<4> (st, io, n);
            runStages (st + 4, count - 4, io, n);
            return;
    }
}

void clearStages (Band& band) noexcept
{
    for (auto& st : band.stage)
        std::fill (std::begin (st.s), std::end (st.s), 0.0);
}

// 2:1 polyphase half-band (§2.7): states per channel pair: outputs A1 A2 A3 B1 B2 B3 (L,R), then input delays
constexpr double hbA[3] = { 0.14737711360104661, 0.48246854276970014, 0.88300502576937312 };
constexpr double hbB[3] = { 0.039151597734460045, 0.30264684832849342, 0.6746159185469639 };

void flushHalfBand (double* s) noexcept
{
    for (int k = 0; k < 12; ++k)
        if (std::abs (s[k]) < (double) flushBelow)
            s[k] = 0.0;
}

// the plain per-channel loops: the reference upsample / downsample are checked against (stagesMatchReference)
void upsampleReference (double* s, const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            double* o = s + c;      // outputs at o[0], o[2], …, o[10]
            double* d = s + 12 + c; // input delays
            const double v = in[2 * i + c];
            const double a1 = d[0] + (v - o[0]) * hbA[0];
            d[0] = v, o[0] = a1;
            const double b1 = d[6] + (v - o[6]) * hbB[0];
            d[6] = v, o[6] = b1;
            const double a2 = d[2] + (a1 - o[2]) * hbA[1];
            d[2] = a1, o[2] = a2;
            const double b2 = d[8] + (b1 - o[8]) * hbB[1];
            d[8] = b1, o[8] = b2;
            const double a3 = d[4] + (a2 - o[4]) * hbA[2];
            o[4] = a3;
            const double b3 = d[10] + (b2 - o[10]) * hbB[2];
            o[10] = b3;
            d[4] = a2, d[10] = b2;
            out[4 * i + c] = (float) b3;
            out[4 * i + 2 + c] = (float) a3;
        }
    flushHalfBand (s);
}

void downsampleReference (double* s, const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            double* o = s + c;
            double* d = s + 12 + c;
            const double e = in[4 * i + c], od = in[4 * i + 2 + c];
            const double a1 = d[0] + (e - o[0]) * hbA[0];
            d[0] = e, o[0] = a1;
            const double b1 = d[6] + (od - o[6]) * hbB[0];
            d[6] = od, o[6] = b1;
            const double a2 = d[2] + (a1 - o[2]) * hbA[1];
            d[2] = a1, o[2] = a2;
            const double b2 = d[8] + (b1 - o[8]) * hbB[1];
            d[8] = b1, o[8] = b2;
            const double a3 = d[4] + (a2 - o[4]) * hbA[2];
            d[4] = a2, o[4] = a3;
            const double b3 = d[10] + (b2 - o[10]) * hbB[2];
            d[10] = b2, o[10] = b3;
            out[2 * i + c] = (float) ((b3 + a3) * 0.5);
        }
    flushHalfBand (s);
}

// round 190kr: the two channels in the two lanes of an SSE2 double vector (the states are stored as left / right pairs):
// per lane the same operations in the same order as the per-channel loops above
void upsample (double* s, const float* in, float* out, int n) noexcept
{
    __m128d o0 = _mm_loadu_pd (s), o2 = _mm_loadu_pd (s + 2), o4 = _mm_loadu_pd (s + 4), o6 = _mm_loadu_pd (s + 6),
            o8 = _mm_loadu_pd (s + 8), o10 = _mm_loadu_pd (s + 10);
    __m128d d0 = _mm_loadu_pd (s + 12), d2 = _mm_loadu_pd (s + 14), d4 = _mm_loadu_pd (s + 16), d6 = _mm_loadu_pd (s + 18),
            d8 = _mm_loadu_pd (s + 20), d10 = _mm_loadu_pd (s + 22);
    const __m128d A0 = _mm_set1_pd (hbA[0]), A1 = _mm_set1_pd (hbA[1]), A2 = _mm_set1_pd (hbA[2]);
    const __m128d B0 = _mm_set1_pd (hbB[0]), B1 = _mm_set1_pd (hbB[1]), B2 = _mm_set1_pd (hbB[2]);
    for (int i = 0; i < n; ++i)
    {
        const __m128d v = _mm_cvtps_pd (_mm_castpd_ps (_mm_load_sd ((const double*) (in + 2 * i))));
        const __m128d a1 = _mm_add_pd (d0, _mm_mul_pd (_mm_sub_pd (v, o0), A0));
        d0 = v, o0 = a1;
        const __m128d b1 = _mm_add_pd (d6, _mm_mul_pd (_mm_sub_pd (v, o6), B0));
        d6 = v, o6 = b1;
        const __m128d a2 = _mm_add_pd (d2, _mm_mul_pd (_mm_sub_pd (a1, o2), A1));
        d2 = a1, o2 = a2;
        const __m128d b2 = _mm_add_pd (d8, _mm_mul_pd (_mm_sub_pd (b1, o8), B1));
        d8 = b1, o8 = b2;
        const __m128d a3 = _mm_add_pd (d4, _mm_mul_pd (_mm_sub_pd (a2, o4), A2));
        o4 = a3;
        const __m128d b3 = _mm_add_pd (d10, _mm_mul_pd (_mm_sub_pd (b2, o10), B2));
        o10 = b3;
        d4 = a2, d10 = b2;
        _mm_store_sd ((double*) (out + 4 * i), _mm_castps_pd (_mm_cvtpd_ps (b3)));
        _mm_store_sd ((double*) (out + 4 * i + 2), _mm_castps_pd (_mm_cvtpd_ps (a3)));
    }
    _mm_storeu_pd (s, o0), _mm_storeu_pd (s + 2, o2), _mm_storeu_pd (s + 4, o4), _mm_storeu_pd (s + 6, o6);
    _mm_storeu_pd (s + 8, o8), _mm_storeu_pd (s + 10, o10);
    _mm_storeu_pd (s + 12, d0), _mm_storeu_pd (s + 14, d2), _mm_storeu_pd (s + 16, d4), _mm_storeu_pd (s + 18, d6);
    _mm_storeu_pd (s + 20, d8), _mm_storeu_pd (s + 22, d10);
    flushHalfBand (s);
}

void downsample (double* s, const float* in, float* out, int n) noexcept
{
    __m128d o0 = _mm_loadu_pd (s), o2 = _mm_loadu_pd (s + 2), o4 = _mm_loadu_pd (s + 4), o6 = _mm_loadu_pd (s + 6),
            o8 = _mm_loadu_pd (s + 8), o10 = _mm_loadu_pd (s + 10);
    __m128d d0 = _mm_loadu_pd (s + 12), d2 = _mm_loadu_pd (s + 14), d4 = _mm_loadu_pd (s + 16), d6 = _mm_loadu_pd (s + 18),
            d8 = _mm_loadu_pd (s + 20), d10 = _mm_loadu_pd (s + 22);
    const __m128d A0 = _mm_set1_pd (hbA[0]), A1 = _mm_set1_pd (hbA[1]), A2 = _mm_set1_pd (hbA[2]);
    const __m128d B0 = _mm_set1_pd (hbB[0]), B1 = _mm_set1_pd (hbB[1]), B2 = _mm_set1_pd (hbB[2]);
    const __m128d half = _mm_set1_pd (0.5);
    for (int i = 0; i < n; ++i)
    {
        const __m128d e = _mm_cvtps_pd (_mm_castpd_ps (_mm_load_sd ((const double*) (in + 4 * i))));
        const __m128d od = _mm_cvtps_pd (_mm_castpd_ps (_mm_load_sd ((const double*) (in + 4 * i + 2))));
        const __m128d a1 = _mm_add_pd (d0, _mm_mul_pd (_mm_sub_pd (e, o0), A0));
        d0 = e, o0 = a1;
        const __m128d b1 = _mm_add_pd (d6, _mm_mul_pd (_mm_sub_pd (od, o6), B0));
        d6 = od, o6 = b1;
        const __m128d a2 = _mm_add_pd (d2, _mm_mul_pd (_mm_sub_pd (a1, o2), A1));
        d2 = a1, o2 = a2;
        const __m128d b2 = _mm_add_pd (d8, _mm_mul_pd (_mm_sub_pd (b1, o8), B1));
        d8 = b1, o8 = b2;
        const __m128d a3 = _mm_add_pd (d4, _mm_mul_pd (_mm_sub_pd (a2, o4), A2));
        d4 = a2, o4 = a3;
        const __m128d b3 = _mm_add_pd (d10, _mm_mul_pd (_mm_sub_pd (b2, o10), B2));
        d10 = b2, o10 = b3;
        _mm_store_sd ((double*) (out + 2 * i), _mm_castps_pd (_mm_cvtpd_ps (_mm_mul_pd (_mm_add_pd (b3, a3), half))));
    }
    _mm_storeu_pd (s, o0), _mm_storeu_pd (s + 2, o2), _mm_storeu_pd (s + 4, o4), _mm_storeu_pd (s + 6, o6);
    _mm_storeu_pd (s + 8, o8), _mm_storeu_pd (s + 10, o10);
    _mm_storeu_pd (s + 12, d0), _mm_storeu_pd (s + 14, d2), _mm_storeu_pd (s + 16, d4), _mm_storeu_pd (s + 18, d6);
    _mm_storeu_pd (s + 20, d8), _mm_storeu_pd (s + 22, d10);
    flushHalfBand (s);
}

// Frequency Splitter's 2:1 decimator (§2.9): 176-tap half-band windowed sinc, newest sample first
const std::array<float, 176>& decimatorKernel()
{
    static const auto k = [] {
        std::array<float, 176> h {};
        for (int i = 0; i < 176; ++i)
        {
            const double x = ((double) i - 89.0) * 1.5707963267948966;
            const double sinc = x != 0.0 ? std::sin (x) / x : 1.0;
            h[(std::size_t) i] = (float) (std::sin ((double) i * 0.017849958259032916) * (double) (float) (sinc * 0.5));
        }
        return h;
    }();
    return k;
}

// Σ buf[from + j]·h[k0 + j] over count values: four running float sums over groups of four, folded into acc as
// (((s0 + acc) + s1) + s2) + s3, then the remainder one by one (§2.9)
float convolve (const float* buf, const float* h, int count, float acc) noexcept
{
    if (count < 0)
        return acc;
    float s[4] = { 0, 0, 0, 0 };
    const int groups = count >> 2;
    for (int g = 0; g < groups; ++g)
        for (int j = 0; j < 4; ++j)
            s[j] += buf[4 * g + j] * h[4 * g + j];
    acc = (((s[0] + acc) + s[1]) + s[2]) + s[3];
    for (int j = groups * 4; j < count; ++j)
        acc = buf[j] * h[j] + acc;
    return acc;
}

float decimate (float* ring, int& index, float even, float odd) noexcept
{
    const auto& h = decimatorKernel();
    ring[index] = even;
    index = (index - 1) & 255;
    ring[index] = odd;
    const int start = index;
    int end = start + 176, wrap = -1;
    if (end >= 256)
        wrap = end - 256, end = 256;
    float acc = convolve (ring + start, h.data(), end - start, 0.0f);
    acc = convolve (ring, h.data() + (end - start), wrap, acc);
    index = (index - 1) & 255;
    return acc;
}

bool oversampled (const Settings& s) noexcept
{
    return s.law == Law::peq2 && (s.processing == Processing::hq || s.processing == Processing::hqExternal);
}

float peq2MainGain (int v) noexcept
{
    return (float) std::exp ((double) ((float) v * 0.01f * dbToLn));
}

float peq1MainGain (int v) noexcept
{
    const float x = (float) v * 0.0078125f;
    if (x == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (x * 2.39789534f)) - 1.0) * (double) 0.1f);
}

float raw01 (int v, double range) { return (float) ((double) v / range); }
int fromRaw01 (float p, double range) { return (int) std::lround ((double) p * range); }
} // namespace

// ------------------------------------------------------------------------------------------------------- states

std::optional<Settings> parsePeq2 (const std::vector<std::uint8_t>& d)
{
    // §1: version, 36 parameters, A/B copy, UI flags; optional fields by version
    if (d.size() < 0x12D)
        return std::nullopt;
    Settings s;
    s.law = Law::peq2;
    const auto version = i32At (d, 0);
    auto p = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    for (int b = 0; b < 7; ++b)
    {
        s.gain[(std::size_t) b] = p (b);
        s.freq[(std::size_t) b] = p (7 + b);
        s.width[(std::size_t) b] = p (14 + b);
        s.type[(std::size_t) b] = p (21 + b);
        s.order[(std::size_t) b] = p (28 + b);
    }
    s.main = p (35);
    const auto ui = (std::uint32_t) i32At (d, 0x124);
    const bool hq = (ui & 1) != 0;
    const auto design = (std::uint8_t) (ui >> 1);
    std::uint8_t options = 0;
    if (version >= 4 && d.size() >= 0x142)
    {
        options = d[0x13D];
        s.mask = (std::uint32_t) i32At (d, 0x13E) & 0x7F;
    }
    s.linearPhase = (options & 1) != 0;
    const auto lin = parseLinearOptions (d);
    s.linearQuality = lin.quality, s.linearFade = lin.smoothing;
    if (hq && design == 0)
        s.processing = (options & 2) != 0 ? Processing::hqExternal : Processing::hq;
    else if (hq && design == 1)
        s.processing = Processing::hqAlternative;
    return s;
}

std::optional<Settings> parsePeq1 (const std::vector<std::uint8_t>& d)
{
    // §3.1: 29 parameters (7 gains, 7 frequencies, 7 widths, 7 types, main), no version
    if (d.size() < 4 * 28)
        return std::nullopt;
    Settings s;
    s.law = Law::peq1;
    auto p = [&] (int i) { return i32At (d, 4 * (std::size_t) i); };
    for (int b = 0; b < 7; ++b)
    {
        s.gain[(std::size_t) b] = p (b);
        s.freq[(std::size_t) b] = p (7 + b);
        s.width[(std::size_t) b] = p (14 + b);
        s.type[(std::size_t) b] = p (21 + b);
    }
    s.main = d.size() >= 4 * 29 ? p (28) : 128;
    return s;
}

Law lawOf (const EffectParams& params) noexcept
{
    const auto v = (int) std::lround (params[(std::size_t) lawParam] * 2.0f);
    return v == 1 ? Law::peq2 : v == 2 ? Law::peq1 : Law::lynx;
}

namespace
{
// LYNX's own frequency value of a v1 frequency (the editor's 20 Hz…20 kHz law)
float lynxHzValue (double hz) { return (float) std::clamp (std::log (std::max (hz, 1.0) / 20.0) / std::log (1000.0), 0.0, 1.0); }
double peq1Hz (int raw)
{
    const auto x = (float) ((double) raw * 1.52587890625e-05);
    const auto e = (float) (std::exp ((double) x * std::log (101.0)) - 1.0);
    return (double) (float) ((double) e * 159.9 + 10.0);
}
double peq1MainDb (int raw) { return 20.0 * std::log10 (std::max ((double) peq1MainGain (raw), 1.0e-6)); }
float gainParam (double db) { return (float) std::clamp (db / 36.0 + 0.5, 0.0, 1.0); }
// FL UI type ↔ LYNX band type (core/Effects.h peq::BandType order: off, peak, lowShelf, highShelf, lowCut, highCut,
// bandPass, notch)
constexpr int lynxOfFl[8] = { 0, 5, 6, 4, 7, 2, 1, 3 };
constexpr int flOfLynx[8] = { 0, 6, 5, 7, 3, 1, 2, 4 };
} // namespace

void toParams (const Settings& s, EffectParams& params) noexcept
{
    for (int b = 0; b < 7; ++b)
    {
        const auto ub = (std::size_t) b;
        const int ui = s.type[ub] >= 0 && s.type[ub] < 8 ? s.type[ub] : 0;
        params[(std::size_t) (4 * b)] = (float) lynxOfFl[ui] / 7.0f;
        params[(std::size_t) (4 * b + 2)] = std::clamp ((float) ((double) s.gain[ub] / 3600.0 + 0.5), 0.0f, 1.0f);
        params[(std::size_t) (4 * b + 3)] = std::clamp (raw01 (s.width[ub], 65536.0), 0.0f, 1.0f);
        params[(std::size_t) (29 + b)] = (float) (std::clamp (s.order[ub], -3, 3) + 3) / 6.0f;
        if (s.law == Law::peq2)
            params[(std::size_t) (4 * b + 1)] = std::clamp (raw01 (s.freq[ub], 65536.0), 0.0f, 1.0f);
        else
        {
            params[(std::size_t) (v1FreqParam + b)] = std::clamp (raw01 (s.freq[ub], 65536.0), 0.0f, 1.0f);
            params[(std::size_t) (4 * b + 1)] = lynxHzValue (peq1Hz (s.freq[ub]));
        }
    }
    if (s.law == Law::peq2)
    {
        params[28] = std::clamp ((float) ((double) s.main / 3600.0 + 0.5), 0.0f, 1.0f);
        params[(std::size_t) linearQualityParam] = (float) std::clamp (s.linearQuality, 0, 7) / 7.0f;
        params[(std::size_t) linearFadeParam] = (float) std::clamp (s.linearFade, 0, 3) / 3.0f;
    }
    else
    {
        params[(std::size_t) v1MainParam] = std::clamp (raw01 (s.main, 256.0), 0.0f, 1.0f);
        params[28] = gainParam (peq1MainDb (s.main));
    }
    params[(std::size_t) lawParam] = s.law == Law::peq1 ? 1.0f : 0.5f;
    params[(std::size_t) processingParam] = (float) (int) s.processing / 3.0f;
    params[(std::size_t) linearParam] = s.linearPhase ? 1.0f : 0.0f;
    params[(std::size_t) maskParam] = (float) (s.mask & 0x7F) / 127.0f;
}

Settings fromParams (const EffectParams& params) noexcept
{
    Settings s;
    s.law = lawOf (params) == Law::peq1 ? Law::peq1 : Law::peq2;
    for (int b = 0; b < 7; ++b)
    {
        const auto ub = (std::size_t) b;
        const int lt = std::clamp ((int) std::lround (params[(std::size_t) (4 * b)] * 7.0f), 0, 7);
        s.type[ub] = flOfLynx[lt];
        s.gain[ub] = (int) std::lround (((double) params[(std::size_t) (4 * b + 2)] - 0.5) * 3600.0);
        s.width[ub] = fromRaw01 (params[(std::size_t) (4 * b + 3)], 65536.0);
        s.order[ub] = std::clamp ((int) std::lround (params[(std::size_t) (29 + b)] * 6.0f) - 3, -3, 3);
        const auto f = params[(std::size_t) (4 * b + 1)];
        if (s.law == Law::peq2)
            s.freq[ub] = fromRaw01 (f, 65536.0);
        else
        {
            // FL's own value while the band has not been moved in the editor; else the nearest v1 value
            const int raw = fromRaw01 (params[(std::size_t) (v1FreqParam + b)], 65536.0);
            if (lynxHzValue (peq1Hz (raw)) == f)
                s.freq[ub] = raw;
            else
            {
                const double hz = 20.0 * std::pow (1000.0, std::clamp ((double) f, 0.0, 1.0));
                const double x = std::log ((std::clamp (hz, 10.0, 16000.0) - 10.0) / 159.9 + 1.0) / std::log (101.0);
                s.freq[ub] = std::clamp ((int) std::lround (x * 65536.0), 0, 65536);
            }
            s.order[ub] = 0;
        }
    }
    if (s.law == Law::peq2)
    {
        s.main = (int) std::lround (((double) params[28] - 0.5) * 3600.0);
        s.linearQuality = (int) std::lround (params[(std::size_t) linearQualityParam] * 7.0f);
        s.linearFade = (int) std::lround (params[(std::size_t) linearFadeParam] * 3.0f);
    }
    else
    {
        const int raw = fromRaw01 (params[(std::size_t) v1MainParam], 256.0);
        if (gainParam (peq1MainDb (raw)) == params[28])
            s.main = raw;
        else
        {
            // nearest raw level to the edited dB value: gain = (11^(raw/128) − 1)/10
            const double g = std::pow (10.0, ((double) params[28] - 0.5) * 36.0 / 20.0);
            s.main = std::clamp ((int) std::lround (128.0 * std::log (g * 10.0 + 1.0) / std::log (11.0)), 0, 256);
        }
    }
    s.processing = (Processing) std::clamp ((int) std::lround (params[(std::size_t) processingParam] * 3.0f), 0, 3);
    s.linearPhase = params[(std::size_t) linearParam] >= 0.5f;
    s.mask = (std::uint32_t) std::clamp ((int) std::lround (params[(std::size_t) maskParam] * 127.0f), 0, 127);
    return s;
}

void designBand (const Settings& s, int b, double rate, Band& band)
{
    if (s.law == Law::peq1)
        designPeq1 (s, b, rate, band);
    else
        designPeq2 (s, b, rate, band);
}

double responseDb (const Settings& s, double sr, double hz)
{
    const double rate = oversampled (s) ? sr * 2.0 : sr;
    const auto w = twoPi * std::clamp (hz, 1.0, sr * 0.499) / rate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    double total = 20.0 * std::log10 (std::max ((double) (s.law == Law::peq1 ? peq1MainGain (s.main) : peq2MainGain (s.main)), 1.0e-9));
    for (int b = 0; b < 7; ++b)
    {
        if (s.law == Law::peq2 && (s.mask & (1u << b)) == 0)
            continue;
        Band band;
        designBand (s, b, rate, band);
        if (band.active == 0)
            continue;
        for (int k = 0; k < band.stages; ++k)
        {
            const auto& c = band.stage[(std::size_t) k];
            const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
            total += 20.0 * std::log10 (std::max (std::abs (h), 1.0e-9));
        }
    }
    return total;
}

// ------------------------------------------------------------------------------------------------------- engine

Engine::Engine() = default;
Engine::~Engine() = default;

namespace
{
bool linearMode (const Settings& s) noexcept { return s.law == Law::peq2 && s.linearPhase; }
} // namespace

double Engine::processingRate() const noexcept
{
    return oversampled (settings) ? sampleRate * 2.0 : sampleRate;
}

void Engine::prepare (double sr) noexcept
{
    sampleRate = sr;
    silenceFrames = (int) sr / 5;
    buffer.assign (2 * 512, 0.0f);
    wide.assign (4 * 512, 0.0f);
    if (configured)
        for (int b = 0; b < 7; ++b)
            designBand (settings, b, processingRate(), bands[(std::size_t) b]);
    if (linear != nullptr)
    {
        linear->prepare ((int) sr);
        linear->set (settings);
    }
    reset();
}

void Engine::reset() noexcept
{
    for (auto& band : bands)
        clearStages (band);
    std::fill (std::begin (up), std::end (up), 0.0);
    std::fill (std::begin (down), std::end (down), 0.0);
    std::fill (&ring[0][0], &ring[0][0] + 512, 0.0f);
    ringIndex[0] = ringIndex[1] = 0;
    silenceCount = 0;
    gainSnap = true;
    if (linear != nullptr)
        linear->reset();
    if (configured)
        for (int b = 0; b < 7; ++b)
            designBand (settings, b, processingRate(), bands[(std::size_t) b]); // restarts the 0 dB countdowns
}

void Engine::set (const Settings& s) noexcept
{
    const bool rateChange = ! configured || oversampled (s) != oversampled (settings);
    const auto old = settings;
    settings = s;
    mainGain = s.law == Law::peq1 ? peq1MainGain (s.main) : peq2MainGain (s.main);
    for (int b = 0; b < 7; ++b)
    {
        auto& band = bands[(std::size_t) b];
        const auto ub = (std::size_t) b;
        const bool same = configured && old.law == s.law && old.gain[ub] == s.gain[ub] && old.freq[ub] == s.freq[ub]
                          && old.width[ub] == s.width[ub] && old.type[ub] == s.type[ub] && old.order[ub] == s.order[ub];
        if (same && ! rateChange)
            continue;
        if (! configured || old.type[ub] != s.type[ub] || old.order[ub] != s.order[ub])
            clearStages (band); // FL clears a band's filters when its type or order changes
        designBand (s, b, processingRate(), band);
    }
    if (rateChange)
    {
        std::fill (std::begin (up), std::end (up), 0.0);
        std::fill (std::begin (down), std::end (down), 0.0);
    }
    if (linearMode (s))
    {
        // the FIR is rebuilt from the new response and cross-faded in (§5); allocates on the first use
        const bool fresh = linear == nullptr;
        if (fresh)
            linear = std::make_unique<LinearPhase>();
        linear->setOptions ({ s.linearQuality, s.linearFade });
        if (fresh)
            linear->prepare ((int) sampleRate);
        linear->set (s);
    }
    else
        linear.reset();
    configured = true;
}

void Engine::flushFilters() noexcept
{
    // FL's silence reset: only bands still counting down (0 dB) lose their state; the resamplers are cleared
    for (auto& band : bands)
        if (band.active > 0)
            clearStages (band);
    std::fill (std::begin (up), std::end (up), 0.0);
    std::fill (std::begin (down), std::end (down), 0.0);
    gainSnap = true;
}

void Engine::block (float* io, int n) noexcept
{
    if (settings.law == Law::peq2)
    {
        // silence gate (§2.8): a block whose samples are all within ±1e-6 counts towards rate/5 frames of silence
        bool silent = true;
        for (int i = 0; i < 2 * n && silent; ++i)
            silent = std::abs (io[i]) <= 9.99999997e-07f;
        if (! silent || settings.linearPhase)
            silenceCount = silenceFrames;
        else
            silenceCount -= n;
        if (silenceCount <= -n)
        {
            if (silenceCount > -0x3FFFFFFF)
                flushFilters();
            silenceCount = (int) 0xC0000001;
            return; // passes the input unchanged
        }
    }

    if (linear != nullptr && linearMode (settings))
    {
        linear->process (io, n); // main level, then the linear-phase FIR (§5)
        return;
    }

    if (settings.law == Law::peq2)
    {
        // main level with FL's glide (§2.5); the step limit follows the processing rate
        const auto maxStep = (float) (44100.0 / processingRate() * 0.001);
        mainLevel (io, n, mainGain, gainNow, maxStep, gainSnap);
        gainSnap = false;
    }
    else if (mainGain == 0.0f)
        std::fill (io, io + 2 * n, 0.0f);
    else if (mainGain != 1.0f)
        for (int i = 0; i < 2 * n; ++i)
            io[i] *= mainGain;

    float* x = io;
    int m = n;
    const bool os = oversampled (settings);
    const bool external = os && settings.processing == Processing::hqExternal;
    if (external)
    {
        // Frequency Splitter's 1:2 up-sampler: each sample doubled, then a zero (§2.9)
        for (int i = 0; i < n; ++i)
        {
            wide[(std::size_t) (4 * i)] = io[2 * i] + io[2 * i];
            wide[(std::size_t) (4 * i + 1)] = io[2 * i + 1] + io[2 * i + 1];
            wide[(std::size_t) (4 * i + 2)] = 0.0f;
            wide[(std::size_t) (4 * i + 3)] = 0.0f;
        }
        x = wide.data();
        m = 2 * n;
    }
    else if (os)
    {
        upsample (up, io, wide.data(), n);
        x = wide.data();
        m = 2 * n;
    }
    // the bands' stages in order as one cascade (round 190kr: runStages), then each band's 0 dB countdown (it touches only
    // its own band's states, which the cascade has finished with)
    Stage* cascade[7 * 4];
    int count = 0;
    bool on[7] {};
    for (int b = 0; b < 7; ++b)
    {
        auto& band = bands[(std::size_t) b];
        if (band.active == 0 || (settings.law == Law::peq2 && (settings.mask & (1u << b)) == 0))
            continue;
        on[b] = true;
        for (int k = 0; k < band.stages; ++k)
            cascade[count++] = &band.stage[(std::size_t) k];
    }
    runStages (cascade, count, x, m);
    for (int b = 0; b < 7; ++b)
    {
        auto& band = bands[(std::size_t) b];
        if (on[b] && band.active > 0 && (band.active -= m) < 1)
        {
            band.active = 0;
            clearStages (band);
        }
    }
    if (external)
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
                io[2 * i + c] = decimate (ring[c], ringIndex[c], wide[(std::size_t) (4 * i + c)], wide[(std::size_t) (4 * i + 2 + c)]);
    else if (os)
        downsample (down, wide.data(), io, n);
}

int Engine::latencySamples() const noexcept
{
    if (! configured)
        return 0;
    if (linear != nullptr && linearMode (settings))
        return linear->latencySamples();
    return oversampled (settings) && settings.processing == Processing::hqExternal ? 44 : 0;
}

void Engine::process (float* left, float* right, int n) noexcept
{
    if (! configured)
        return;
    for (int done = 0; done < n;)
    {
        const int len = std::min (512, n - done);
        for (int i = 0; i < len; ++i)
        {
            buffer[(std::size_t) (2 * i)] = left[done + i];
            buffer[(std::size_t) (2 * i + 1)] = right[done + i];
        }
        block (buffer.data(), len);
        for (int i = 0; i < len; ++i)
        {
            left[done + i] = buffer[(std::size_t) (2 * i)];
            right[done + i] = buffer[(std::size_t) (2 * i + 1)];
        }
        done += len;
    }
}
// round 190kr: the sample-by-sample SIMD cascade against the plain per-stage loop (tests: perf190kr)
bool stagesMatchReference (std::uint32_t seed) noexcept
{
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (double) (seed >> 8) / 16777216.0; };
    for (int count = 1; count <= 9; ++count)
    {
        Stage a[9], b[9];
        for (int k = 0; k < count; ++k)
        {
            // a stable resonant stage (poles at radius r) with random gains, and random states (some below the flush)
            const auto r = 0.5 + 0.49 * rnd(), w = 3.0 * rnd();
            a[k].a1 = -2.0 * r * std::cos (w), a[k].a2 = r * r;
            a[k].b0 = 2.0 * rnd() - 0.5, a[k].b1 = 2.0 * rnd() - 1.0, a[k].b2 = rnd() - 0.5;
            for (auto& v : a[k].s)
                v = rnd() < 0.2 ? 1.0e-9 * rnd() : rnd() - 0.5;
            b[k] = a[k];
        }
        float x[2 * 300], y[2 * 300];
        for (int block : { 1, 37, 128, 300 })
        {
            for (int i = 0; i < 2 * block; ++i)
                x[i] = y[i] = i % 97 == 0 ? 0.0f : (float) (rnd() - 0.5) * (i % 5 == 0 ? 1.0e-7f : 1.0f);
            for (int k = 0; k < count; ++k)
                runStage (a[k], x, block);
            Stage* list[9];
            for (int k = 0; k < count; ++k)
                list[k] = &b[k];
            runStages (list, count, y, block);
            if (std::memcmp (x, y, sizeof (float) * 2 * (std::size_t) block) != 0)
                return false;
            for (int k = 0; k < count; ++k)
                if (std::memcmp (a[k].s, b[k].s, sizeof (a[k].s)) != 0)
                    return false;
        }
    }
    // the half-band 1:2 / 2:1 resamplers: per-channel loops vs the two-lane ones
    for (int block : { 1, 37, 256 })
    {
        double sa[24], sb[24];
        for (int k = 0; k < 24; ++k)
            sa[k] = sb[k] = rnd() < 0.2 ? 0.0 : rnd() - 0.5;
        float in[2 * 256], wa[4 * 256], wb[4 * 256], oa[2 * 256], ob[2 * 256];
        for (int i = 0; i < 2 * block; ++i)
            in[i] = i % 53 == 0 ? -0.0f : (float) (rnd() - 0.5);
        upsampleReference (sa, in, wa, block);
        upsample (sb, in, wb, block);
        if (std::memcmp (wa, wb, sizeof (float) * 4 * (std::size_t) block) != 0 || std::memcmp (sa, sb, sizeof (sa)) != 0)
            return false;
        downsampleReference (sa, wa, oa, block);
        downsample (sb, wb, ob, block);
        if (std::memcmp (oa, ob, sizeof (float) * 2 * (std::size_t) block) != 0 || std::memcmp (sa, sb, sizeof (sa)) != 0)
            return false;
    }
    return true;
}
} // namespace lynx::peqfl
