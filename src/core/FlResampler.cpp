#include "core/FlResampler.h"

#include <algorithm>
#include <cmath>


#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) // x86 only: rcpps cannot be reproduced through NEON; ARM64 takes the plain path
#include "core/Simd.h"
#define LYNX_FLRS_SSE 1
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::flrs
{
namespace
{
constexpr double pi = 3.141592653589793;

// FL's index → half-width table (index 0 is unused)
constexpr int halfWidths[10] = { 1, 1, 3, 8, 12, 16, 32, 64, 128, 256 };

// The 6-point kernel: y(t) = Σ c_k·t^k, t = frac − ½, each c_k a fixed combination of the outer/middle/inner pair
// sums (even powers) or differences (odd powers): k = 5 … 0, weights for pairs (−2, 3), (−1, 2), (0, 1).
constexpr float hermite[6][3] = {
    { -1.0f / 24.0f, 5.0f / 24.0f, -5.0f / 12.0f },     // t^5, differences
    { 1.0f / 48.0f, -1.0f / 16.0f, 1.0f / 24.0f },      // t^4, sums
    { 5.0f / 48.0f, -11.0f / 16.0f, 37.0f / 24.0f },    // t^3, differences
    { -5.0f / 96.0f, 13.0f / 32.0f, -17.0f / 48.0f },   // t^2, sums
    { -3.0f / 128.0f, 61.0f / 384.0f, -87.0f / 64.0f }, // t^1, differences
    { 3.0f / 256.0f, -25.0f / 256.0f, 75.0f / 128.0f }, // t^0, sums
};

#if defined(__x86_64__) || defined(__i386__)
using ext = long double; // the x87 format with GCC/Clang on x86
#else
using ext = double;
#endif

float linear (const float* t, int n, double frac) noexcept
{
    const ext a = t[n - 1], b = t[n];
    return (float) ((b - a) * (ext) frac + a);
}

float sixPoint (const float* t, double frac) noexcept
{
    // taps 0…5 = frames i−2 … i+3
    const ext x = (ext) frac - (ext) 0.5f;
    const float outerSum = (float) ((ext) t[5] + (ext) t[0]), outerDiff = (float) ((ext) t[0] - (ext) t[5]);
    const ext midSum = (ext) t[4] + (ext) t[1], midDiff = (ext) t[1] - (ext) t[4];
    const ext inSum = (ext) t[3] + (ext) t[2], inDiff = (ext) t[2] - (ext) t[3];
    auto odd = [&] (int k) { return (ext) outerDiff * (ext) hermite[k][0] + midDiff * (ext) hermite[k][1] + inDiff * (ext) hermite[k][2]; };
    auto even = [&] (int k) { return (ext) outerSum * (ext) hermite[k][0] + midSum * (ext) hermite[k][1] + inSum * (ext) hermite[k][2]; };
    ext acc = odd (0) * x;
    acc = (acc + even (1)) * x;
    acc = (acc + odd (2)) * x;
    acc = (acc + even (3)) * x;
    acc = (acc + odd (4)) * x;
    return (float) (acc + even (5));
}

// The kernel's eight float seeds from the four sines (round 190kz, owner decision 4e, done exactly): the sines come
// from an odd polynomial (to r^23 on [−π/2, π/2], error < 1e-15) instead of four library calls, and the seeds are kept
// only when every value within 1e-14 of each one rounds to the same float — then they are the floats the library's
// sines give, because a sine error δ moves a seed by at most 7δ (polynomial ≤ 3.2e-16 measured, a libm within 1 ulp:
// 7δ < 3e-15); otherwise (about one frame in 90,000) the library sines are used. The same samples as before, bit for bit
// (tests/Dsp190kzTests.inc; round 190kz agents/dsp/e).
inline double polySine (double x) noexcept // x in [0, 2π)
{
    const double pi = 3.141592653589793, halfPi = 1.5707963267948966;
    double r = x > pi ? x - 6.283185307179586 : x; // (−π, π]
    r = r > halfPi ? pi - r : r < -halfPi ? -pi - r : r;
    const double r2 = r * r;
    static constexpr double c[12] = { 1.0, -1.0 / 6.0, 1.0 / 120.0, -1.0 / 5040.0, 1.0 / 362880.0, -1.0 / 39916800.0, 1.0 / 6227020800.0,
                                      -1.0 / 1307674368000.0, 1.0 / 355687428096000.0, -1.0 / 121645100408832000.0,
                                      1.0 / 51090942171709440000.0, -1.0 / 25852016738884976640000.0 };
    double q = c[11];
    for (int j = 10; j >= 0; --j)
        q = q * r2 + c[j];
    return q * r;
}

// seeds from s0…s3 (the sines at e0, e0 − step, e2, e2 + cutoff); `guard`: false when a seed lies within 1e-14 of a
// float rounding boundary
inline bool seedsFrom (const Kernel& k, double s0, double s1, double s2, double s3, float* seed, bool guard) noexcept
{
    bool sure = true;
    auto put = [&] (int i, ext v) {
        seed[i] = (float) v;
        if (guard)
            sure = sure && (float) (v - (ext) 1.0e-14) == (float) (v + (ext) 1.0e-14);
    };
    {
        const ext a = s0, b = s1, c2 = (ext) k.twoCosStep;
        put (7, a);
        put (3, b);
        const ext t = a * c2 - b;
        put (6, t);
        put (2, t * c2 - a);
    }
    {
        const ext a = s2, b = s3, c2 = (ext) k.twoCosCutoff;
        put (5, a);
        put (1, b);
        const ext t = a * c2 - b;
        put (4, t);
        put (0, t * c2 - a);
    }
    return sure;
}
} // namespace

void sincSeeds (const Kernel& k, double a0, double a1, double a2, double a3, float* seed, bool plain) noexcept
{
    const auto inRange = [] (double x) { return x >= 0.0 && x < 6.283185307179586; };
    if (! plain && inRange (a0) && inRange (a1) && inRange (a2) && inRange (a3)
        && seedsFrom (k, polySine (a0), polySine (a1), polySine (a2), polySine (a3), seed, true))
        return;
    seedsFrom (k, std::sin (a0), std::sin (a1), std::sin (a2), std::sin (a3), seed, false);
}

namespace
{
// Windowed sinc over 2N taps: weight(d) = sin(w·d)/(π·d) · (0.36 + c + 0.64·c²)/2 with c = cos(π·d/N), d = frac − m
// for tap m (frame i + m, m = −N+1 … N). The sines and cosines start from four library sines per output frame and
// advance by the Chebyshev recurrence s(d−2) = 2·cos·s(d−1) − s(d) in single precision; 1/(π·d) is the SSE estimate
// refined by one Newton step; the four lanes hold the taps m = 1, 0, 2, −1 and step outwards two frames per half.
void sinc (const Kernel& k, const float* tl, const float* tr, double fracIn, float& outL, float& outR) noexcept
{
    const int n = k.halfWidth;
    const double x = fracIn < 1e-25 ? 1e-25 : fracIn;
    const ext ex = (ext) x, na = -(ext) k.step, w = (ext) k.cutoff;
    const ext e0 = ex * na + (ext) 1.5707963267948966;
    const ext e2 = ex * w;
    float seed[8];
    sincSeeds (k, (double) e0, (double) (e0 + na), (double) e2, (double) (e2 + w), seed);
#if defined(__x86_64__) || defined(__i386__)
    const ext piX = 3.14159265358979323846264338327950288L;
#else
    const ext piX = pi;
#endif
    const ext X = ex * piX;
    float denominators[4] = { (float) (X - piX), (float) X, (float) ((X - piX) - piX), (float) (X + piX) };
    const float twoPi = 6.2831855f;
    const float cw = (float) k.twoCosCutoff, cn = (float) k.twoCosStep;
    const int centre = n - 1; // tap index of frame i

#if LYNX_FLRS_SSE
    __m128 den = _mm_loadu_ps (denominators), next = _mm_loadu_ps (seed), cur = _mm_loadu_ps (seed + 4);
    const __m128 twoCos = _mm_setr_ps (cw, cw, cn, cn);
    const __m128 c0 = _mm_setr_ps (0.36f, 0.36f, 0.0f, 0.0f), c2 = _mm_setr_ps (0.64f, 0.64f, 1.0f, 1.0f);
    const __m128 stepDen = _mm_setr_ps (-twoPi, twoPi, -twoPi, twoPi);
    __m128 acc = _mm_setzero_ps(), hi = _mm_setzero_ps();

    for (int j = 0; j < n / 2; ++j)
    {
        const int fwd = centre + 1 + 2 * j, back = centre - 1 - 2 * j;
        __m128 r = _mm_rcp_ps (den);
        const __m128 rr = _mm_mul_ps (_mm_mul_ps (den, r), r);
        __m128 v = cur;
        cur = next;
        next = _mm_sub_ps (_mm_mul_ps (next, twoCos), v);
        r = _mm_sub_ps (_mm_add_ps (r, r), rr);
        hi = _mm_movehl_ps (hi, v);
        __m128 win = _mm_add_ps (_mm_add_ps (_mm_mul_ps (_mm_mul_ps (c2, hi), hi), hi), c0);
        v = _mm_mul_ps (v, r);
        den = _mm_add_ps (den, stepDen);
        r = _mm_movehl_ps (r, r);
        v = _mm_mul_ps (v, win);
        v = _mm_shuffle_ps (v, v, 0x50);
        acc = _mm_add_ps (acc, _mm_mul_ps (v, _mm_setr_ps (tl[fwd], tr[fwd], tl[back + 1], tr[back + 1])));

        hi = _mm_movehl_ps (hi, cur);
        v = cur;
        win = _mm_add_ps (_mm_add_ps (_mm_mul_ps (_mm_mul_ps (c2, hi), hi), hi), c0);
        cur = next;
        next = _mm_sub_ps (_mm_mul_ps (next, twoCos), v);
        v = _mm_mul_ps (_mm_mul_ps (v, r), win);
        v = _mm_shuffle_ps (v, v, 0x50);
        acc = _mm_add_ps (acc, _mm_mul_ps (v, _mm_setr_ps (tl[fwd + 1], tr[fwd + 1], tl[back], tr[back])));
    }
    const __m128 sum = _mm_mul_ps (_mm_add_ps (_mm_movehl_ps (acc, acc), acc), _mm_set1_ps (0.5f));
    float o[4];
    _mm_storeu_ps (o, sum);
    outL = o[0];
    outR = o[1];
#else
    // portable path: same recurrences, exact reciprocal
    float d[4] = { denominators[0], denominators[1], denominators[2], denominators[3] };
    float nx[4] = { seed[0], seed[1], seed[2], seed[3] }, cu[4] = { seed[4], seed[5], seed[6], seed[7] };
    const float tc[4] = { cw, cw, cn, cn }, step4[4] = { -twoPi, twoPi, -twoPi, twoPi };
    float acc[4] = {};
    for (int j = 0; j < n / 2; ++j)
    {
        const int fwd = centre + 1 + 2 * j, back = centre - 1 - 2 * j;
        float r[4], v[4], nn[4];
        for (int q = 0; q < 4; ++q)
        {
            r[q] = 1.0f / d[q];
            v[q] = cu[q];
            nn[q] = nx[q] * tc[q] - v[q];
            d[q] += step4[q];
        }
        const float wA = (0.64f * v[2] * v[2] + v[2]) + 0.36f, wB = (0.64f * v[3] * v[3] + v[3]) + 0.36f;
        const float a0 = v[0] * r[0] * wA, a1 = v[1] * r[1] * wB;
        acc[0] += a0 * tl[fwd];
        acc[1] += a0 * tr[fwd];
        acc[2] += a1 * tl[back + 1];
        acc[3] += a1 * tr[back + 1];
        float v2[4];
        for (int q = 0; q < 4; ++q)
            v2[q] = nx[q];
        for (int q = 0; q < 4; ++q)
        {
            cu[q] = nn[q];
            nx[q] = nn[q] * tc[q] - v2[q];
        }
        const float wC = (0.64f * v2[2] * v2[2] + v2[2]) + 0.36f, wD = (0.64f * v2[3] * v2[3] + v2[3]) + 0.36f;
        const float b0 = v2[0] * r[2] * wC, b1 = v2[1] * r[3] * wD;
        acc[0] += b0 * tl[fwd + 1];
        acc[1] += b0 * tr[fwd + 1];
        acc[2] += b1 * tl[back];
        acc[3] += b1 * tr[back];
    }
    outL = (acc[2] + acc[0]) * 0.5f;
    outR = (acc[3] + acc[1]) * 0.5f;
#endif
}
} // namespace

int halfWidthFor (int quality) noexcept
{
    return halfWidths[std::clamp (quality, 1, 9)];
}

void Kernel::setQuality (int quality) noexcept
{
    baseHalfWidth = halfWidth = halfWidthFor (quality);
    if (halfWidth > 6)
    {
        step = pi / (double) halfWidth;
        twoCosStep = std::cos (step) * 2.0;
    }
    speed = -1.0;
}

void Kernel::setSpeed (double s) noexcept
{
    if (s == speed)
        return;
    speed = s;
    if (halfWidth <= 6)
        return;

    const auto magnitude = std::fabs (s);
    if (magnitude > 1.0)
    {
        cutoff = pi / magnitude;
        twoCosCutoff = std::cos (cutoff) * 2.0;
    }
    else
    {
        cutoff = pi;
        twoCosCutoff = -2.0;
    }

    auto width = (std::int64_t) ((std::llrint (magnitude) + 3) & ~(std::int64_t) 3);
    width = std::min<std::int64_t> (std::max<std::int64_t> (width, baseHalfWidth), 512);
    if ((int) width != halfWidth)
    {
        halfWidth = (int) width;
        step = pi / (double) halfWidth;
        twoCosStep = std::cos (step) * 2.0;
    }
}

void interpolate (const Kernel& k, const float* tl, const float* tr, double frac, float& outL, float& outR) noexcept
{
    if (k.halfWidth < 3)
    {
        outL = linear (tl, k.halfWidth, frac);
        outR = linear (tr, k.halfWidth, frac);
    }
    else if (k.halfWidth == 3)
    {
        outL = sixPoint (tl, frac);
        outR = sixPoint (tr, frac);
    }
    else
        sinc (k, tl, tr, frac, outL, outR);
}

double speedFor (float cents, double ratio) noexcept
{
    if (cents == 0.0f && ratio == 1.0)
        return 1.0;
    static const double perCent = (std::log (2.0) / 12.0) / 100.0;
    return std::exp ((double) cents * perCent) * ratio;
}
} // namespace lynx::flrs
