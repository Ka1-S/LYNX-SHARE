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
extern const float lnHi[130];    // ln (1 + j/128) to 12 significant bits (truncated) — round 167f, logf
extern const float lnLo[130];    // the rest, rounded toward zero

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

// logf for finite x > 0 (round 167f, REA_rodada167f-transporter.md §A.1): the same reduction as log10f (a 128-entry
// table around the mantissa, a cubic in the scaled remainder) with natural-log tables and ln 2 split in two halves;
// near 1 (|x − 1| < 1/16) a series in u = f / (2 + f)
inline float logf_ (float xf)
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
        xb = tb;
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
            b = fmaF (e, 0x1.0bfbe8p-15f, -s);
            a = fmaF (0x1.62ep-1f, e, lnHi[idx]);
        }
        else
        {
            const float q = r * 0.333333343f + 0.5f;
            s = q * (r * r) + r;
            b = 0x1.0bfbe8p-15f * e - s;
            a = 0x1.62ep-1f * e + lnHi[idx];
        }
        b = b + lnLo[idx];
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
        t = v * 0.0125000002f + 0.0833333358f;
        t = t * sv - fu;
    }
    return f + t;
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
} // namespace lynx::ildsp
