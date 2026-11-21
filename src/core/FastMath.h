#pragma once

// Round 190ok (owner decision D3): fast per-sample maths for LYNX's own devices (Ember, Prism, Crest, Echoform), used
// from EffectSlot::engineRev 2 on (Effect::setEngineRev). The C library's tanhf / exp / log10 / pow are calls that the
// compiler cannot inline or vectorise (inserts50: tanhf alone was 22 % of the render, Patience: pow / exp / log10 13 %);
// these are inline, branch-free and close to the C library's own accuracy, so the sound does not move:
//
//   tanh (float)          at most 2.4 ulp from the exact tanh (glibc's tanhf: 1.4), odd, bounded by 1, monotonic
//                         (checked over 4.8 M points, d3/fit_tanh2.py of the round folder)
//   exp2 / exp (double)   Taylor series of 2^f on |f| <= 1/2 to the 13th power: under 1e-15 relative
//   log2 / log10 (double) the atanh series of log ((1 + s) / (1 - s)) on |s| <= 0.1716 to s^17: under 1e-15
//   pow (double)          exp2 (y * log2 (x)) for x > 0
//
// The coefficients are LYNX's own (a numpy fit in this round for tanh; textbook series elsewhere). Rounding to an
// integer uses the 1.5 * 2^52 (2^23) addition, so the default round-to-nearest mode is assumed (as on every LYNX
// thread: FTZ / DAZ only). Header-only, no JUCE.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lynx::fastmath
{
namespace detail
{
inline float floatFromBits (std::uint32_t u) noexcept
{
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}
inline double doubleFromBits (std::uint64_t u) noexcept
{
    double d;
    std::memcpy (&d, &u, 8);
    return d;
}
inline std::uint64_t bitsOf (double d) noexcept
{
    std::uint64_t u;
    std::memcpy (&u, &d, 8);
    return u;
}
inline std::uint32_t bitsOf (float f) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}
} // namespace detail

// e^y in float for y in [-87, 88] (2^k 2^f, f in [-1/2, 1/2], a degree-6 fit of 2^f: 1.9e-9 relative)
inline float expf (float y) noexcept
{
    y = std::min (std::max (y, -87.0f), 88.0f);
    const float t = y * 1.44269504088896341f;
    const float k = (t + 12582912.0f) - 12582912.0f; // (t rounded to the nearest integer: no floor call on plain SSE2)
    const float f = t - k;
    float p = 0.00015345808058040214f;
    p = p * f + 0.0013399931188867002f;
    p = p * f + 0.009618488971821895f;
    p = p * f + 0.05550328777060705f;
    p = p * f + 0.24022646890497418f;
    p = p * f + 0.6931472057372039f;
    p = p * f + 1.0000000005541934f;
    return p * detail::floatFromBits ((std::uint32_t) ((int) k + 127) << 23);
}

// tanh in float: an odd fit (x + x^3 P(x^2)) under |x| = 1/2, 1 - 2 / (e^(2|x|) + 1) above, the sign put back
inline float tanh (float x) noexcept
{
    const float a = std::min (std::fabs (x), 10.0f); // (from 10 on the float tanh is 1)
    const float e = fastmath::expf (a + a);
    const float big = 1.0f - 2.0f / (e + 1.0f);
    const float u = a * a;
    float p = -0.006943068987444083f;
    p = p * u + 0.02147074808399354f;
    p = p * u - 0.05393347970561276f;
    p = p * u + 0.13333225832720852f;
    p = p * u - 0.3333333279724467f;
    const float small = a + a * (u * p);
    // (the choice and the sign as bit masks: no branch, so a loop of these vectorises)
    const std::uint32_t pick = 0u - (std::uint32_t) (a < 0.5f);
    const std::uint32_t r = (detail::bitsOf (small) & pick) | (detail::bitsOf (big) & ~pick);
    return detail::floatFromBits (r | (detail::bitsOf (x) & 0x80000000u));
}

// 2^x in double (|x| < 1022)
inline double exp2 (double x) noexcept
{
    x = std::min (std::max (x, -1022.0), 1023.0);
    const double k = (x + 6755399441055744.0) - 6755399441055744.0; // (x rounded to the nearest integer, no floor call)
    const double f = (x - k) * 0.6931471805599453094; // f ln 2, |.| <= 0.3466
    // e^f, Taylor to f^13 (the next term is under 4e-18)
    double p = 1.0 / 6227020800.0;
    p = p * f + 1.0 / 479001600.0;
    p = p * f + 1.0 / 39916800.0;
    p = p * f + 1.0 / 3628800.0;
    p = p * f + 1.0 / 362880.0;
    p = p * f + 1.0 / 40320.0;
    p = p * f + 1.0 / 5040.0;
    p = p * f + 1.0 / 720.0;
    p = p * f + 1.0 / 120.0;
    p = p * f + 1.0 / 24.0;
    p = p * f + 1.0 / 6.0;
    p = p * f + 0.5;
    p = p * f + 1.0;
    p = p * f + 1.0;
    return p * detail::doubleFromBits ((std::uint64_t) ((std::int64_t) k + 1023) << 52);
}

inline double exp (double x) noexcept { return fastmath::exp2 (x * 1.4426950408889634074); }

// log2 in double for a positive, finite, normal x (anything else: the C library's answer)
inline double log2 (double x) noexcept
{
    if (! (x >= 2.2250738585072014e-308) || ! (x <= 1.7976931348623157e308))
        return std::log2 (x);
    const auto bits = detail::bitsOf (x);
    auto e = (int) ((bits >> 52) & 0x7ff) - 1023;
    auto m = detail::doubleFromBits ((bits & 0x000fffffffffffffull) | 0x3ff0000000000000ull); // [1, 2)
    if (m > 1.4142135623730951)
    {
        m *= 0.5;
        ++e;
    }
    // log2 (m) = (2 / ln 2) atanh (s), s = (m - 1) / (m + 1), |s| <= 0.1716: s (1 + s^2 / 3 + s^4 / 5 + …)
    const double s = (m - 1.0) / (m + 1.0), u = s * s;
    double p = 1.0 / 17.0;
    p = p * u + 1.0 / 15.0;
    p = p * u + 1.0 / 13.0;
    p = p * u + 1.0 / 11.0;
    p = p * u + 1.0 / 9.0;
    p = p * u + 1.0 / 7.0;
    p = p * u + 1.0 / 5.0;
    p = p * u + 1.0 / 3.0;
    p = p * u + 1.0;
    return (double) e + 2.8853900817779268147 * s * p; // (2 / ln 2)
}

inline double log10 (double x) noexcept { return fastmath::log2 (x) * 0.30102999566398119521; }
inline double log (double x) noexcept { return fastmath::log2 (x) * 0.69314718055994530942; }

// sin (2 pi p) in double for any finite phase p (LFOs keep p in [0, 1)): p reduced to the nearest quarter turn, then the
// Taylor series of sin / cos on |2 pi f| <= pi / 4 (to the 19th / 18th power: under 1e-16)
inline double sin2pi (double p) noexcept
{
    const double q = p * 4.0;
    const double k = (q + 6755399441055744.0) - 6755399441055744.0; // nearest quarter turn
    const double x = (q - k) * 1.5707963267948966192;               // |x| <= pi / 4
    const auto quadrant = (int) ((std::int64_t) k & 3);
    const double u = x * x;
    double sn = -1.0 / 121645100408832000.0;
    sn = sn * u + 1.0 / 355687428096000.0;
    sn = sn * u - 1.0 / 1307674368000.0;
    sn = sn * u + 1.0 / 6227020800.0;
    sn = sn * u - 1.0 / 39916800.0;
    sn = sn * u + 1.0 / 362880.0;
    sn = sn * u - 1.0 / 5040.0;
    sn = sn * u + 1.0 / 120.0;
    sn = sn * u - 1.0 / 6.0;
    sn = x + x * (u * sn);
    double cs = 1.0 / 6402373705728000.0;
    cs = cs * u - 1.0 / 20922789888000.0;
    cs = cs * u + 1.0 / 87178291200.0;
    cs = cs * u - 1.0 / 479001600.0;
    cs = cs * u + 1.0 / 3628800.0;
    cs = cs * u - 1.0 / 40320.0;
    cs = cs * u + 1.0 / 720.0;
    cs = cs * u - 1.0 / 24.0;
    cs = cs * u + 0.5;
    cs = 1.0 - u * cs;
    switch (quadrant)
    {
        case 0: return sn;
        case 1: return cs;
        case 2: return -sn;
        default: return -cs;
    }
}

// x^y in double for x > 0 (x = 0: 0 for y > 0, as std::pow; anything else: the C library's answer)
inline double pow (double x, double y) noexcept
{
    if (x > 0.0 && x <= 1.7976931348623157e308)
        return fastmath::exp2 (y * fastmath::log2 (x));
    return std::pow (x, y);
}
} // namespace lynx::fastmath
