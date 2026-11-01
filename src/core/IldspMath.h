#pragma once
// Float math of FL Studio's shared DSP library (Shared/ildsp_x64.dll, REA_rodada60 §2.5): re-creations of the
// expf / powf / log10f / sinf / cosf / sincosf / fminf / fmaxf routines of its statically linked C runtime, written
// from their observed behaviour (clean-room) and checked bit for bit against the library in three rounding modes.
// They evaluate in the thread's current SSE rounding mode like the originals; the FMA variants are taken when the
// CPU has FMA (the library's own dispatch), else the plain ones.
#include <cmath>
#include <cstdint>
#include <cstring>
#include "core/Simd.h"
#if LYNX_X86
 #include <cpuid.h>
#endif

#pragma clang fp contract(off)
#pragma STDC FENV_ACCESS ON

namespace lynx::ildsp
{
inline std::uint64_t dbits (double v) { std::uint64_t u; std::memcpy (&u, &v, 8); return u; }
inline double dfrom (std::uint64_t u) { double v; std::memcpy (&v, &u, 8); return v; }
inline std::uint32_t fbits (float v) { std::uint32_t u; std::memcpy (&u, &v, 4); return u; }
inline float ffrom (std::uint32_t u) { float v; std::memcpy (&v, &u, 4); return v; }

#if LYNX_X86
inline bool cpuHasFma()
{
    static const bool f = [] {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (! __get_cpuid (1, &a, &b, &c, &d)) return false;
        if ((c & (1u << 12)) == 0 || (c & (1u << 27)) == 0 || (c & (1u << 28)) == 0) return false;
        unsigned lo = 0, hi = 0;
        asm volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        return (lo & 6u) == 6u;
    }();
    return f;
}
#else
inline bool cpuHasFma() { return true; } // ARM64 always has a fused multiply-add (same IEEE result as x86 FMA)
#endif
extern bool forceNoFma; // test switch (false: follow the CPU)
inline bool useFma() { return cpuHasFma() && ! forceNoFma; }

#if LYNX_X86
__attribute__ ((target ("fma"))) inline double fmaD (double a, double b, double c)
{ return _mm_cvtsd_f64 (_mm_fmadd_sd (_mm_set_sd (a), _mm_set_sd (b), _mm_set_sd (c))); }
__attribute__ ((target ("fma"))) inline float fmaF (float a, float b, float c)
{ return _mm_cvtss_f32 (_mm_fmadd_ss (_mm_set_ss (a), _mm_set_ss (b), _mm_set_ss (c))); }
#else
inline double fmaD (double a, double b, double c) { return std::fma (a, b, c); }
inline float fmaF (float a, float b, float c) { return std::fmaf (a, b, c); }
#endif
inline int cvtD2I (double v) { return _mm_cvtsd_si32 (_mm_set_sd (v)); }   // current rounding mode
inline int cvttD2I (double v) { return _mm_cvttsd_si32 (_mm_set_sd (v)); } // truncation
inline float d2f (double v) { return _mm_cvtss_f32 (_mm_cvtsd_ss (_mm_setzero_ps(), _mm_set_sd (v))); }

extern const double exp2T[64];   // 2^(j/64)
extern const double rcpT[258];   // 1 / (0.5 (1 + j/256)), then 0
extern const double lnT[258];    // ln (1 + j/256), then 0
extern const float lgRcp[130];   // float (2 / (1 + j/128))
extern const float lgHi[130];    // log10 (1 + j/128) to 10 mantissa bits (truncated)
extern const float lgLo[130];    // the rest, rounded toward zero

// expf
inline float expf_ (float xf)
{
    const std::uint32_t ax = fbits (xf) & 0x7fffffffu;
    if (ax >= 0x7f800000u) return xf != xf ? xf : (fbits (xf) >> 31 ? 0.0f : xf);
    const double x = xf;
    const double k = x * 92.332482616893657;
    if (k >= 8192.0) return INFINITY;
    if (k < -9600.0) return 0.0f;
    const int N = cvtD2I (k);
    const double nd = N;
    const int j = N & 63, m = (N - j) >> 6;
    const double T = exp2T[j];
    double p;
    if (useFma())
    {
        const double r = fmaD (-nd, 0.010830424696249145, x);
        const double q = fmaD (r, 0.16666666666666666, 0.5);
        p = fmaD (r * r, q, r);
        p = fmaD (p, T, T);
    }
    else
    {
        const double r = x - nd * 0.010830424696249145;
        const double q = r * 0.16666666666666666 + 0.5;
        p = (r * r) * q + r;
        p = p * T + T;
    }
    return d2f (p * dfrom ((std::uint64_t) (std::int64_t) (m + 1023) << 52));
}

// exp of a double t (powf's tail), FMA or plain variant
inline double powExpTail (double t, bool fused)
{
    const double k = t * 92.332482616893657;
    const int N = cvtD2I (k);
    const double nd = N;
    double r, p;
    if (fused)
    {
        r = fmaD (-nd, 0.010830424696249145, t);
        double q = fmaD (0.16666666666666666, r, 0.5);
        q = fmaD (q, r, 1.0);
        p = r * q;
        const double T = exp2T[N & 63];
        p = fmaD (p, T, T);
    }
    else
    {
        r = t - nd * 0.010830424696249145;
        const double r0 = r;
        double q = 0.16666666666666666 * r + 0.5;
        p = (r * r0) * q + r0;
        const double T = exp2T[N & 63];
        p = p * T + T;
    }
    return dfrom (dbits (p) + ((std::uint64_t) (std::int64_t) (N >> 6) << 52));
}

// powf for finite x > 0 and finite y (what the engine uses); other cases follow the C standard
inline float powf_ (float xf, float yf)
{
    const std::uint32_t ay = fbits (yf) & 0x7fffffffu, ax = fbits (xf) & 0x7fffffffu;
    if (ay >= 0x7f800000u || ax >= 0x7f800000u || ! (xf > 0.0f) || ay == 0)
        return std::pow (xf, yf); // not used by the engine
    if (ay == 0x3f800000u && ! (fbits (yf) >> 31)) return xf;
    const double x = xf, y = yf;
    const bool fused = useFma();
    double lg;
    const std::int32_t xi = (std::int32_t) fbits (xf);
    if (xi < 0x3f880000 && std::fabs (x - 1.0) < 0.0625)
    {
        const double f = x - 1.0;
        const double u = f / (f + 2.0);
        const double uf = u * f;
        const double s = u + u;
        const double v = s * s;
        const double a0 = v * 0.012500000003771751 + 0.083333333333331788;
        const double a1 = v * 0.00043488777770761457 + 0.0022321399879194482;
        const double sv = s * v;
        const double sv3 = (v * v) * sv;
        double l = (sv3 * a1) + (sv * a0);
        l = l - uf;
        lg = f + l;
        return d2f (powExpTail (y * lg, false));
    }
    const std::uint64_t xb = dbits (x);
    const std::uint64_t mant = xb & 0x000fffffffffffffull;
    const std::uint32_t j = (std::uint32_t) (mant >> 44) + (std::uint32_t) ((mant >> 43) & 1u);
    const double c = dfrom (((std::uint64_t) (j | 0x3fe00u)) << 44);
    const double m = dfrom (mant | 0x3fe0000000000000ull);
    const double e = (double) (std::int32_t) ((std::int64_t) (xb >> 52) - 1023);
    double r = (c - m) * rcpT[j];
    double s;
    if (fused)
    {
        double q = fmaD (0.33333333333333331, r, 0.5);
        q = fmaD (q, r, 1.0);
        s = r * q;
    }
    else
    {
        double q = 0.33333333333333331 * r + 0.5;
        q = q * r + 1.0;
        s = r * q;
    }
    lg = (e * 0.69314718055994529 + lnT[j]) - s;
    const double t = y * lg;
    if (t > 88.72283935546875) return INFINITY;
    if (! (t > -103.2789306640625)) return 0.0f;
    return d2f (powExpTail (t, fused));
}

// log10f for finite x > 0
inline float log10f_ (float xf)
{
    std::uint32_t xb = fbits (xf);
    if ((xb & 0x7f800000u) == 0x7f800000u) return xf != xf ? xf : (xb >> 31 ? NAN : xf);
    if (! (xf > 0.0f)) return xf == 0.0f ? -INFINITY : NAN;
    float e = (float) ((std::int32_t) (xb >> 23) - 127);
    std::uint32_t mb = xb & 0x007fffffu;
    const float d1 = std::fabs (xf - 1.0f);
    const bool fused = useFma();
    if (e == -127.0f) // denormal input (not reached with DAZ)
    {
        const float t = ffrom (mb | 0x3f800000u) - 1.0f;
        const std::uint32_t tb = fbits (t);
        e = (float) ((std::int32_t) (tb >> 23) - 253);
        mb = tb & 0x007fffffu;
        xb = tb; // the index below uses the normalised bits
    }
    if (! (d1 < 0.0625f))
    {
        const std::uint32_t idx = ((xb & 0x007f0000u) + ((xb & 0x00008000u) << 1)) >> 16;
        const float cm = ffrom ((xb & 0x007f0000u) + ((xb & 0x00008000u) << 1) | 0x3f000000u);
        const float mm = ffrom (mb | 0x3f000000u);
        const float r = (cm - mm) * lgRcp[idx];
        float s, a, b;
        if (fused)
        {
            const float q = fmaF (0.333333343f, r, 0.5f);
            s = fmaF (q, r * r, r);
            s = s * 0.434294492f;
            b = fmaF (e, 0.000248745637f, -s);
            a = fmaF (0.30078125f, e, lgHi[idx]);
        }
        else
        {
            const float q = 0.333333343f * r + 0.5f;
            s = q * (r * r) + r;
            s = s * 0.434294492f;
            b = e * 0.000248745637f - s;
            a = 0.30078125f * e + lgHi[idx];
        }
        b = b + lgLo[idx];
        return a + b;
    }
    const float f = xf - 1.0f;
    const float u = f / (2.0f + f);
    const float fu = f * u;
    const float s = u + u;
    const float v = s * s;
    const float sv = s * v;
    float t;
    if (fused)
    {
        t = fmaF (0.0125000002f, v, 0.0833333358f);
        t = fmaF (t, sv, -fu);
    }
    else
    {
        t = 0.0125000002f * v + 0.0833333358f;
        t = t * sv - fu;
    }
    const float fh = ffrom (fbits (f) & 0xffff0000u);
    const float fl = f - fh;
    t = t + fl;
    const float a = t * 0.43359375f;
    float b = fh * 0.000700731878f;
    const float c = fh * 0.43359375f;
    if (fused) b = fmaF (t, 0.000700731878f, b);
    else b = t * 0.000700731878f + b;
    b = b + a;
    return b + c;
}

// sinf / cosf (double evaluation after a reduction by π/2), |x| < 2^24
inline double sinPoly (double r, bool fused)
{
    const double r2 = r * r;
    if (fused)
    {
        double p = fmaD (r2, 2.7557319223985893e-06, -0.00019841269841269841);
        p = fmaD (p, r2, 0.0083333333333333332);
        p = fmaD (p, r2, -0.16666666666666666);
        return fmaD (p, r * r2, r);
    }
    double p = r2 * 2.7557319223985893e-06 + -0.00019841269841269841;
    p = p * r2 + 0.0083333333333333332;
    p = p * r2 + -0.16666666666666666;
    return p * (r * r2) + r;
}
inline double cosPoly (double r, bool fused, bool fusedC)
{
    const double r2 = r * r;
    if (fused)
    {
        const double c = fusedC ? fmaD (r2, -0.5, 1.0) : 1.0 - r2 * 0.5;
        double p = fmaD (r2, -2.7557319223985888e-07, 2.4801587301587298e-05);
        p = fmaD (p, r2, -0.0013888888888888887);
        p = fmaD (p, r2, 0.041666666666666664);
        return fmaD (p, r2 * r2, c);
    }
    const double c = 1.0 - r2 * 0.5;
    double p = r2 * -2.7557319223985888e-07 + 2.4801587301587298e-05;
    p = p * r2 + -0.0013888888888888887;
    p = p * r2 + 0.041666666666666664;
    return p * (r2 * r2) + c;
}
inline void reduce (double ax, bool fused, int& n, double& r)
{
    if (fused)
    {
        n = cvttD2I (fmaD (ax, 0.63661977236758138, 0.5));
        const double nd = n;
        const double a = fmaD (-nd, 1.5707963267341256, ax);
        r = a - nd * 6.0771005065061922e-11;
    }
    else
    {
        n = cvttD2I (ax * 0.63661977236758138 + 0.5);
        const double nd = n;
        const double a = ax - nd * 1.5707963267341256;
        r = a - nd * 6.0771005065061922e-11;
    }
}
// the plain (no FMA) variants: polynomials in two halves, a second reduction step under heavy cancellation
inline double sinPolyNf (double r)
{
    const double z = r * r;
    double a = 2.7557319223985893e-06 * z;
    const double z2 = z * z;
    double b = 0.0083333333333333332 * z;
    const double rz = r * z;
    a = a + -0.00019841269841269841;
    a = a * z2;
    b = b + -0.16666666666666666;
    a = a + b;
    a = a * rz;
    return r + a;
}
inline double cosPolyNf (double r)
{
    const double z = r * r;
    double a = -2.7557319223985888e-07 * z;
    const double z2 = z * z;
    double b = -0.0013888888888888887 * z;
    double c = -0.5 * z;
    a = a + 2.4801587301587298e-05;
    a = a * z2;
    b = b + 0.041666666666666664;
    a = a + b;
    a = a * z2;
    c = c + 1.0;
    return c + a;
}
// reduction by π/2 for π/4 < ax < 2^19 (plain variant); returns n, r and the remainder's biased exponent
inline double reduceNf (double ax, int& n, int& er)
{
    const double t0 = ax * 0.63661977236758138 + 0.5;
    n = cvttD2I (t0);
    const double nd = n;
    double t = ax - 1.5707963267341256 * nd;
    double r = t - 6.0771005065061922e-11 * nd;
    er = (int) ((dbits (r) << 1) >> 53);
    const int ex = (int) (dbits (ax) >> 52);
    if (ex - er > 15)
    {
        const double p2 = 6.077100506303966e-11 * nd;
        double p2t = 2.0222662487959506e-21 * nd;
        const double t2 = t - p2;
        double w = t - t2;
        w = w - p2;
        p2t = p2t - w;
        r = t2 - p2t;
        er = (int) ((dbits (r) << 1) >> 53);
    }
    return r;
}
inline float sinf_ (float xf)
{
    const double x = xf;
    const bool fused = useFma();
    if (! fused)
    {
        const double ax = std::fabs (x);
        if (! (ax > 0.78539816339744828))
        {
            if (ax < 0.0078125)
            {
                if (ax < 0.0001220703125) return xf;
                double t = x * x;
                t = t * x;
                t = t * 0.16666666666666666;
                return d2f (x - t);
            }
            return d2f (sinPolyNf (x));
        }
        const bool negIn = std::signbit (x);
        int n, er;
        const double r = reduceNf (ax, n, er);
        double y;
        if (er >= 0x3f2) y = (n & 1) ? cosPolyNf (r) : sinPolyNf (r);
        else if (er > 0x3de) { const double z = r * r; y = (n & 1) ? 1.0 - z * 0.5 : r - (0.16666666666666666 * r) * z; }
        else y = (n & 1) ? 1.0 : r;
        if ((((n >> 1) & 1) != 0) != negIn) y = 0.0 - y;
        return d2f (y);
    }
    const double ax = std::fabs (x);
    if (! (ax > 0.78539816339744828))
    {
        if (ax < 0.0078125)
        {
            if (ax < 0.0001220703125) return xf;
            const double x3 = (x * x) * x;
            return d2f (fmaD (x3, -0.16666666666666666, x));
        }
        return d2f (sinPoly (x, true));
    }
    int n; double r;
    reduce (ax, true, n, r);
    double y = (n & 1) ? cosPoly (r, true, true) : sinPoly (r, true);
    const bool neg = ((n & 3) >= 2) != (std::signbit (x));
    return d2f (neg ? -y : y);
}
inline float cosf_ (float xf)
{
    const double x = xf;
    const bool fused = useFma();
    if (! fused)
    {
        const double ax = std::fabs (x);
        if (! (ax > 0.78539816339744828))
            return d2f (cosPolyNf (x));
        int n, er;
        const double r = reduceNf (ax, n, er);
        double y;
        if (er >= 0x3f2) y = (n & 1) ? sinPolyNf (r) : cosPolyNf (r);
        else if (er > 0x3de) { const double z = r * r; y = (n & 1) ? r - (0.16666666666666666 * r) * z : 1.0 - z * 0.5; }
        else y = (n & 1) ? r : 1.0;
        if (((n + 1) & 2) != 0) y = 0.0 - y;
        return d2f (y);
    }
    const double ax = std::fabs (x);
    if (! (ax > 0.78539816339744828))
    {
        if (ax < 0.0078125)
        {
            if (ax < 0.0001220703125) return 1.0f;
            return d2f (fmaD (-(x * 0.5), x, 1.0));
        }
        return d2f (cosPoly (x, true, false));
    }
    int n; double r;
    reduce (ax, true, n, r);
    double y = (n & 1) ? sinPoly (r, true) : cosPoly (r, true, false);
    const bool neg = (((n + 1) >> 1) & 1) != 0;
    return d2f (neg ? -y : y);
}

// sincosf: the library's vector float sine on (x, |x| + π/2), |x| < 10000
inline float vsin1 (float x)
{
    const float a = std::fabs (x);
    const float k = a * 0.318309873f + 12582912.0f;
    const std::uint32_t par = fbits (k) << 31;
    const float n = k - 12582912.0f;
    float r = a - n * 3.140625f;
    r = r - n * 0.000967502594f;
    r = r - n * 1.50990672e-07f;
    r = r - n * 5.12668814e-12f;
    const float r2 = r * r;
    const float rs = ffrom (fbits (r) ^ par);
    float p = 2.60578054e-06f * r2 + -0.000198096022f;
    p = p * r2 + 0.00833306275f;
    p = p * r2 + -0.166666597f;
    const float t = (r2 * p) * rs;
    return ffrom (fbits (rs + t) ^ (fbits (x) & 0x80000000u));
}
inline void sincosf_ (float x, float& s, float& c)
{
    const float a = std::fabs (x);
    if (! (a >= 0.000122070312f))
    {
        s = x;
        c = 1.0f - (a * a) * 0.5f;
        return;
    }
    s = vsin1 (x);
    c = vsin1 (a + 1.57079637f);
}

// tanf (round 167f, REA_rodada167f-transporter.md §A.2): double evaluation, x + x³·P(x²)/Q(x²) on |x| ≤ π/4 after a
// two-step reduction by π/2 (odd quarter → −1/t); |x| < 2⁻¹³ uses x + x³/3, |x| < 2⁻²⁷ returns x. Reduction for
// arguments ≥ 2²³ (never reached by the engines) falls back to the C library.
inline double tanPoly (double r, bool fused)
{
    const double r2 = r * r;
    double num, den;
    if (fused)
    {
        num = fmaD (-0.017203248047148168, r2, 0.38529607126399540);
        den = fmaD (fmaD (0.018442392569016560, r2, -0.51396505478854540), r2, 1.1558882143468838);
        return fmaD (r2 * r, num / den, r);
    }
    num = -0.017203248047148168 * r2 + 0.38529607126399540;
    den = (0.018442392569016560 * r2 + -0.51396505478854540) * r2 + 1.1558882143468838;
    return r + (r2 * r) * (num / den);
}
inline float tanf_ (float xf)
{
    if ((fbits (xf) & 0x7f800000u) == 0x7f800000u) return xf - xf; // ±inf → NaN, NaN stays
    const double x = xf;
    const double ax = std::fabs (x);
    const bool fused = useFma();
    if (ax <= 0.78539816339744828)
    {
        if (ax < 0x1p-27) return xf;
        if (ax < 0x1p-13)
            return d2f (fused ? fmaD (x * x * x, 0.33333333333333331, x) : (x * x * x) * 0.33333333333333331 + x);
        return d2f (tanPoly (x, fused));
    }
    if (ax >= 8388608.0) return (float) std::tan (x);
    int n;
    double r;
    if (fused)
    {
        n = cvttD2I (fmaD (ax, 0.63661977236758138, 0.5));
        const double nd = n;
        const double a = fmaD (-nd, 1.5707963267341256, ax);
        r = a - nd * 6.0771005065061922e-11;
    }
    else
    {
        n = cvttD2I (ax * 0.63661977236758138 + 0.5);
        const double nd = n;
        const double a = ax - nd * 1.5707963267341256;
        r = a - nd * 6.0771005065061922e-11;
    }
    double t = tanPoly (r, fused);
    if (n & 1) t = -1.0 / t;
    return d2f (x < 0 ? -t : t);
}

// fminf / fmaxf of the library (NaN → the other operand; −0 < +0)
inline float fmaxf_ (float x, float y)
{
    if (x != x) return y;
    if (y != y) return x;
    if (y > x) return y;
    if (x == y && (fbits (x) >> 31)) return y;
    return x;
}
inline float fminf_ (float x, float y)
{
    if (x != x) return y;
    if (y != y) return x;
    if (y > x) return x;
    if (x != y) return y;
    return (fbits (x) >> 31) ? x : y;
}

// --- round 167f (the limiter behind Emphasis, REA_rodada167f.md §1.2): the library's double exp and float ln ---
// Tables, from their definitions: expHiT[j] = 2^(j/64) cut to 28 significant bits, expLoT[j] = 2^(j/64) − expHiT[j]
// rounded toward zero; lnHiT[j] = ln (1 + j/128) cut to 11 significant bits, lnLoT[j] = the rest rounded toward zero
// (j = 128: ln 2; j = 129: 0). The reciprocals of logf_ are lgRcp.
inline constexpr double expHiT[64] = {0x1.0000000000000p+0, 0x1.02c9a30000000p+0, 0x1.059b0d0000000p+0, 0x1.0874510000000p+0, 0x1.0b55860000000p+0, 0x1.0e3ec30000000p+0, 0x1.11301d0000000p+0, 0x1.1429aa0000000p+0, 0x1.172b830000000p+0, 0x1.1a35be0000000p+0, 0x1.1d48730000000p+0, 0x1.2063b80000000p+0, 0x1.2387a60000000p+0, 0x1.26b4560000000p+0, 0x1.29e9df0000000p+0, 0x1.2d285a0000000p+0, 0x1.306fe00000000p+0, 0x1.33c08b0000000p+0, 0x1.371a730000000p+0, 0x1.3a7db30000000p+0, 0x1.3dea640000000p+0, 0x1.4160a20000000p+0, 0x1.44e0860000000p+0, 0x1.486a2b0000000p+0, 0x1.4bfdad0000000p+0, 0x1.4f9b270000000p+0, 0x1.5342b50000000p+0, 0x1.56f4730000000p+0, 0x1.5ab07d0000000p+0, 0x1.5e76f10000000p+0, 0x1.6247eb0000000p+0, 0x1.6623880000000p+0, 0x1.6a09e60000000p+0, 0x1.6dfb230000000p+0, 0x1.71f75e0000000p+0, 0x1.75feb50000000p+0, 0x1.7a11470000000p+0, 0x1.7e2f330000000p+0, 0x1.8258990000000p+0, 0x1.868d990000000p+0, 0x1.8ace540000000p+0, 0x1.8f1ae90000000p+0, 0x1.93737b0000000p+0, 0x1.97d8290000000p+0, 0x1.9c49180000000p+0, 0x1.a0c6670000000p+0, 0x1.a5503b0000000p+0, 0x1.a9e6b50000000p+0, 0x1.ae89f90000000p+0, 0x1.b33a2b0000000p+0, 0x1.b7f76f0000000p+0, 0x1.bcc1e90000000p+0, 0x1.c199bd0000000p+0, 0x1.c67f120000000p+0, 0x1.cb720d0000000p+0, 0x1.d072d40000000p+0, 0x1.d5818d0000000p+0, 0x1.da9e600000000p+0, 0x1.dfc9730000000p+0, 0x1.e502ee0000000p+0, 0x1.ea4afa0000000p+0, 0x1.efa1be0000000p+0, 0x1.f507650000000p+0, 0x1.fa7c180000000p+0};
inline constexpr double expLoT[64] = {0x0.0p+0, 0x1.cef00c1dcdef9p-25, 0x1.8ac2ba1d73e2ap-27, 0x1.0eb37901186bep-25, 0x1.9f3121ec53172p-25, 0x1.69e8d10103a17p-27, 0x1.25b50a4ebbf1ap-32, 0x1.d525bbf668203p-25, 0x1.8faa2f5b9bef9p-25, 0x1.6df96ea796d31p-25, 0x1.68b9aa7805b80p-28, 0x1.0c519ac771dd6p-25, 0x1.ceac470cd83f5p-25, 0x1.789f37495e99cp-26, 0x1.47f7b84b09745p-26, 0x1.b900c2d002475p-26, 0x1.4636e2a5bd1abp-25, 0x1.320b7fa64e430p-27, 0x1.ceaa72a9c5154p-26, 0x1.3967fdba86f24p-26, 0x1.82468446b6824p-25, 0x1.f72e29f84325bp-28, 0x1.8624b40c4dbd0p-30, 0x1.704f3404f068ep-26, 0x1.4d8a89c750e5ep-26, 0x1.a74b29ab4cf62p-26, 0x1.a753e077c2a0fp-26, 0x1.ad49f699bb2c0p-26, 0x1.a90a852b19260p-25, 0x1.6b48521ba6f93p-26, 0x1.d2ac258f87d03p-31, 0x1.2a91124893ecfp-27, 0x1.9fcef32422cbep-26, 0x1.8ca345de441c5p-25, 0x1.1d8bee7ba46e1p-25, 0x1.9099f22fdba6ap-26, 0x1.f580c36bea881p-27, 0x1.b3d398841740ap-26, 0x1.2999c25159f11p-25, 0x1.68925d901c83bp-25, 0x1.15506dadd3e2ap-27, 0x1.22aee6c57304ep-25, 0x1.9b8bc9e8a0387p-29, 0x1.fbc9c9f173d24p-25, 0x1.51f8480e3e235p-27, 0x1.6bbcac96535b5p-25, 0x1.1f12ae45a1224p-27, 0x1.5e7f6fd0fac90p-26, 0x1.2b5a75abd0e69p-25, 0x1.09e2bf5ed7fa1p-25, 0x1.7daf237553d84p-27, 0x1.2f074891ee83dp-30, 0x1.b0aa538444196p-25, 0x1.cafa29694426fp-25, 0x1.9df20d22a0797p-25, 0x1.40f12f71a1e45p-25, 0x1.9f7490e4bb40bp-25, 0x1.ed9942b84600dp-27, 0x1.bdcdaf5cb4656p-27, 0x1.e2cffd89cf44cp-26, 0x1.52486cc2c7b9dp-27, 0x1.cc2b44eee3fa4p-25, 0x1.6dc8a80ce9f09p-25, 0x1.9e90d82e90a7ep-28};
inline constexpr float lnHiT[130] = {0x0.0p+0f, 0x1.fe00000000000p-8f, 0x1.fc00000000000p-7f, 0x1.7b80000000000p-6f, 0x1.f820000000000p-6f, 0x1.39e0000000000p-5f, 0x1.7740000000000p-5f, 0x1.b420000000000p-5f, 0x1.f0a0000000000p-5f, 0x1.1640000000000p-4f, 0x1.3400000000000p-4f, 0x1.51a0000000000p-4f, 0x1.6f00000000000p-4f, 0x1.8c20000000000p-4f, 0x1.a920000000000p-4f, 0x1.c5e0000000000p-4f, 0x1.e260000000000p-4f, 0x1.fec0000000000p-4f, 0x1.0d60000000000p-3f, 0x1.1b60000000000p-3f, 0x1.2940000000000p-3f, 0x1.3700000000000p-3f, 0x1.44c0000000000p-3f, 0x1.5260000000000p-3f, 0x1.5fe0000000000p-3f, 0x1.6d60000000000p-3f, 0x1.7aa0000000000p-3f, 0x1.87e0000000000p-3f, 0x1.9520000000000p-3f, 0x1.a220000000000p-3f, 0x1.af20000000000p-3f, 0x1.bc20000000000p-3f, 0x1.c8e0000000000p-3f, 0x1.d5c0000000000p-3f, 0x1.e260000000000p-3f, 0x1.ef00000000000p-3f, 0x1.fb80000000000p-3f, 0x1.0400000000000p-2f, 0x1.0a20000000000p-2f, 0x1.1040000000000p-2f, 0x1.1660000000000p-2f, 0x1.1c80000000000p-2f, 0x1.2280000000000p-2f, 0x1.2880000000000p-2f, 0x1.2e80000000000p-2f, 0x1.3460000000000p-2f, 0x1.3a60000000000p-2f, 0x1.4040000000000p-2f, 0x1.4600000000000p-2f, 0x1.4be0000000000p-2f, 0x1.51a0000000000p-2f, 0x1.5760000000000p-2f, 0x1.5d00000000000p-2f, 0x1.62c0000000000p-2f, 0x1.6860000000000p-2f, 0x1.6e00000000000p-2f, 0x1.7380000000000p-2f, 0x1.7920000000000p-2f, 0x1.7ea0000000000p-2f, 0x1.8420000000000p-2f, 0x1.89a0000000000p-2f, 0x1.8f00000000000p-2f, 0x1.9460000000000p-2f, 0x1.99c0000000000p-2f, 0x1.9f20000000000p-2f, 0x1.a480000000000p-2f, 0x1.a9c0000000000p-2f, 0x1.af00000000000p-2f, 0x1.b440000000000p-2f, 0x1.b980000000000p-2f, 0x1.bea0000000000p-2f, 0x1.c3c0000000000p-2f, 0x1.c8e0000000000p-2f, 0x1.ce00000000000p-2f, 0x1.d320000000000p-2f, 0x1.d820000000000p-2f, 0x1.dd40000000000p-2f, 0x1.e240000000000p-2f, 0x1.e740000000000p-2f, 0x1.ec20000000000p-2f, 0x1.f120000000000p-2f, 0x1.f600000000000p-2f, 0x1.fae0000000000p-2f, 0x1.ffc0000000000p-2f, 0x1.0240000000000p-1f, 0x1.04a0000000000p-1f, 0x1.0720000000000p-1f, 0x1.0980000000000p-1f, 0x1.0be0000000000p-1f, 0x1.0e40000000000p-1f, 0x1.1080000000000p-1f, 0x1.12e0000000000p-1f, 0x1.1540000000000p-1f, 0x1.1780000000000p-1f, 0x1.19e0000000000p-1f, 0x1.1c20000000000p-1f, 0x1.1e80000000000p-1f, 0x1.20c0000000000p-1f, 0x1.2300000000000p-1f, 0x1.2540000000000p-1f, 0x1.2780000000000p-1f, 0x1.29c0000000000p-1f, 0x1.2c00000000000p-1f, 0x1.2e40000000000p-1f, 0x1.3060000000000p-1f, 0x1.32a0000000000p-1f, 0x1.34e0000000000p-1f, 0x1.3700000000000p-1f, 0x1.3920000000000p-1f, 0x1.3b60000000000p-1f, 0x1.3d80000000000p-1f, 0x1.3fa0000000000p-1f, 0x1.41c0000000000p-1f, 0x1.43e0000000000p-1f, 0x1.4600000000000p-1f, 0x1.4820000000000p-1f, 0x1.4a40000000000p-1f, 0x1.4c60000000000p-1f, 0x1.4e60000000000p-1f, 0x1.5080000000000p-1f, 0x1.52a0000000000p-1f, 0x1.54a0000000000p-1f, 0x1.56a0000000000p-1f, 0x1.58c0000000000p-1f, 0x1.5ac0000000000p-1f, 0x1.5cc0000000000p-1f, 0x1.5ee0000000000p-1f, 0x1.60e0000000000p-1f, 0x1.62e0000000000p-1f, 0x0.0p+0f};
inline constexpr float lnLoT[130] = {0x0.0p+0f, 0x1.5358820000000p-23f, 0x1.5161f80000000p-20f, 0x1.1b07d40000000p-18f, 0x1.361cf00000000p-19f, 0x1.0f73fc0000000p-18f, 0x1.63d8ca0000000p-19f, 0x1.bae2320000000p-18f, 0x1.86008a0000000p-20f, 0x1.36eea20000000p-16f, 0x1.d7961a0000000p-16f, 0x1.073f060000000p-16f, 0x1.a515ca0000000p-17f, 0x1.45d6300000000p-16f, 0x1.b4e92a0000000p-18f, 0x1.523d6e0000000p-18f, 0x1.076e2a0000000p-16f, 0x1.2263b60000000p-17f, 0x1.7e7cd00000000p-15f, 0x1.2ad52e0000000p-15f, 0x1.52f81e0000000p-15f, 0x1.fc201e0000000p-15f, 0x1.2b6cca0000000p-15f, 0x1.cbc7420000000p-16f, 0x1.3070a60000000p-15f, 0x1.fce33a0000000p-20f, 0x1.8902100000000p-15f, 0x1.a065200000000p-15f, 0x1.6a73d00000000p-17f, 0x1.bc1fe20000000p-15f, 0x1.c94e800000000p-15f, 0x1.0ce85a0000000p-16f, 0x1.f7c79a0000000p-15f, 0x1.0b5a7c0000000p-18f, 0x1.076e2a0000000p-15f, 0x1.5b97b80000000p-16f, 0x1.186d5e0000000p-15f, 0x1.2ca5a60000000p-17f, 0x1.24e2720000000p-14f, 0x1.8bf9ae0000000p-14f, 0x1.5cabaa0000000p-14f, 0x1.3182d20000000p-15f, 0x1.41fbce0000000p-14f, 0x1.5a13de0000000p-14f, 0x1.c575c20000000p-15f, 0x1.dd9a980000000p-14f, 0x1.3155a40000000p-16f, 0x1.8434340000000p-17f, 0x1.8bc21c0000000p-14f, 0x1.7e55dc0000000p-16f, 0x1.5b0e5a0000000p-15f, 0x1.dc5d140000000p-16f, 0x1.bdbf580000000p-14f, 0x1.05e5720000000p-15f, 0x1.903d360000000p-15f, 0x1.1d54560000000p-15f, 0x1.d7f6ba0000000p-14f, 0x1.4abfba0000000p-15f, 0x1.f077040000000p-15f, 0x1.a3b43c0000000p-15f, 0x1.9c360a0000000p-17f, 0x1.1e87360000000p-14f, 0x1.941c200000000p-14f, 0x1.9581160000000p-14f, 0x1.23ecbe0000000p-14f, 0x1.0243960000000p-16f, 0x1.d935340000000p-15f, 0x1.2932460000000p-14f, 0x1.eef7980000000p-15f, 0x1.625a4c0000000p-16f, 0x1.4d9da60000000p-14f, 0x1.d7a7cc0000000p-14f, 0x1.f7c79a0000000p-14f, 0x1.af0b840000000p-14f, 0x1.fcfc000000000p-15f, 0x1.e7258a0000000p-14f, 0x1.a813060000000p-16f, 0x1.1034f80000000p-15f, 0x1.09875a0000000p-16f, 0x1.99d2460000000p-14f, 0x1.1ebf5e0000000p-15f, 0x1.23fa700000000p-14f, 0x1.588f780000000p-14f, 0x1.2e08560000000p-14f, 0x1.52a5a40000000p-13f, 0x1.df9da80000000p-13f, 0x1.f2e0e60000000p-16f, 0x1.bd3d5c0000000p-15f, 0x1.cb90940000000p-15f, 0x1.2617460000000p-15f, 0x1.f39e2c0000000p-13f, 0x1.7195920000000p-13f, 0x1.87a5e80000000p-14f, 0x1.eabbd80000000p-13f, 0x1.cd68ce0000000p-14f, 0x1.b81f700000000p-13f, 0x1.7d79c00000000p-15f, 0x1.b9a3240000000p-14f, 0x1.30d7be0000000p-13f, 0x1.5bce980000000p-13f, 0x1.5e12880000000p-13f, 0x1.37fec20000000p-13f, 0x1.d3da880000000p-14f, 0x1.d0db900000000p-15f, 0x1.d7334e0000000p-13f, 0x1.1339120000000p-13f, 0x1.44ece60000000p-16f, 0x1.17b5460000000p-13f, 0x1.e0d3560000000p-13f, 0x1.0893fe0000000p-14f, 0x1.026a700000000p-13f, 0x1.5b84d00000000p-13f, 0x1.8fe8460000000p-13f, 0x1.9fe2f80000000p-13f, 0x1.8bc21c0000000p-13f, 0x1.53d1ea0000000p-13f, 0x1.f0bb600000000p-14f, 0x1.e6bf320000000p-15f, 0x1.d811b60000000p-13f, 0x1.13cc000000000p-13f, 0x1.6932de0000000p-16f, 0x1.2467980000000p-13f, 0x1.f9d5b20000000p-13f, 0x1.5b6b9a0000000p-14f, 0x1.404c340000000p-13f, 0x1.b1dc6c0000000p-13f, 0x1.54920a0000000p-20f, 0x1.97a23c0000000p-16f, 0x1.0bfbe80000000p-15f, 0x0.0p+0f};

// exp of a double (the library's own, not the C runtime's): 2^(N/64) e^y with N = trunc (x · 64/ln 2), the table
// value split in three, a degree-6 polynomial; FMA or plain variant like the originals
inline double expD_ (double x)
{
    const std::uint64_t ax = dbits (x) & 0x7fffffffffffffffull;
    if (ax >= 0x7ff0000000000000ull)
    {
        if (x != x) return x + x;
        return (dbits (x) >> 63) ? 0.0 : x;
    }
    if (! (x <= 0x1.62e42fefa39efp+9 && ! (x < -0x1.74046dfefd9d0p+9)))
    {
        if (x > 0x1.62e42fefa39efp+9) return INFINITY;
        return x > -0x1.74910d52d3051p+9 ? 4.9406564584124654e-324 : 0.0;
    }
    if (ax <= 0x3e50000000000000ull) return x + 1.0; // |x| ≤ 2^-26
    const double t = x * 0x1.71547652b82fep+6;
    const int N = cvttD2I (t);
    const double nd = N;
    double q;
    if (useFma())
    {
        const double x1 = fmaD (nd, -0x1.62e42fefa0000p-7, x);
        const double y = nd * -0x1.cf79abc9e3b39p-46 + x1;
        double p = fmaD (0x1.6c16c16c16c17p-10, y, 0x1.1111111111111p-7);
        p = fmaD (p, y, 0x1.5555555555555p-5);
        p = fmaD (p, y, 0x1.5555555555555p-3);
        p = fmaD (p, y, 0.5);
        q = fmaD (y * y, p, y);
    }
    else
    {
        const double x1 = x + nd * -0x1.62e42fefa0000p-7;
        const double y = nd * -0x1.cf79abc9e3b39p-46 + x1;
        double a = 0x1.6c16c16c16c17p-10 * y + 0x1.1111111111111p-7;
        double b = 0x1.5555555555555p-3 * y;
        const double y2 = y * y;
        b = b + 0.5;
        const double y4 = y2 * y2;
        a = a * y;
        b = b * y2;
        a = a + 0x1.5555555555555p-5;
        b = b + y;
        a = a * y4;
        q = b + a;
    }
    const int j = N & 63, m = N >> 6;
    double r = q * exp2T[j];
    r = r + expLoT[j];
    r = r + expHiT[j];
    if (m > -1022 || (m == -1022 && r >= 1.0))
        return dfrom (dbits (r) + ((std::uint64_t) (std::int64_t) m << 52));
    const int sh = m + 1074;
    return sh >= 0 ? r * dfrom (1ull << sh) : 0.0;
}

// natural log of a float x > 0 (log10f_'s twin: same reduction, ln tables and constants)
inline float logf_ (float xf)
{
    std::uint32_t xb = fbits (xf);
    if ((xb & 0x7fffffffu) >= 0x7f800000u) return xf != xf ? ffrom (xb | 0x400000u) : (xb >> 31 ? NAN : xf);
    if (! (xf > 0.0f)) return xf == 0.0f ? -INFINITY : NAN;
    float e = (float) ((std::int32_t) (xb >> 23) - 127);
    std::uint32_t mb = xb & 0x007fffffu;
    const float d1 = std::fabs (xf - 1.0f);
    const bool fused = useFma();
    if (e == -127.0f) // denormal input (not reached with DAZ)
    {
        const float t = ffrom (mb | 0x3f800000u) - 1.0f;
        const std::uint32_t tb = fbits (t);
        e = (float) ((std::int32_t) (tb >> 23) - 253);
        mb = tb & 0x007fffffu;
        xb = tb;
    }
    if (! (d1 < 0.0625f))
    {
        const std::uint32_t ib = (xb & 0x007f0000u) + ((xb & 0x00008000u) << 1);
        const std::uint32_t idx = ib >> 16;
        const float cm = ffrom (ib | 0x3f000000u);
        const float mm = ffrom (mb | 0x3f000000u);
        const float r = (cm - mm) * lgRcp[idx];
        float s, a, b;
        if (fused)
        {
            const float q = fmaF (0.333333343f, r, 0.5f);
            s = fmaF (q, r * r, r);
            a = fmaF (0x1.0bfbe8p-15f, e, -s);
            b = fmaF (0x1.62ep-1f, e, lnHiT[idx]);
        }
        else
        {
            float q = r * 0.333333343f + 0.5f;
            q = q * (r * r);
            s = r + q;
            a = 0x1.0bfbe8p-15f * e;
            a = a - s;
            b = 0x1.62ep-1f * e + lnHiT[idx];
        }
        a = a + lnLoT[idx];
        return b + a;
    }
    const float f = xf - 1.0f;
    const float u = f / (2.0f + f);
    const float fu = f * u;
    const float s = u + u;
    const float v = s * s;
    const float sv = s * v;
    float t;
    if (fused)
    {
        t = fmaF (v, 0.0125000002f, 0.0833333358f);
        t = fmaF (t, sv, -fu);
    }
    else
    {
        t = v * 0.0125000002f + 0.0833333358f;
        t = t * sv - fu;
    }
    return f + t;
}
} // namespace lynx::ildsp
