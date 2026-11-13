// FL Studio's Kepler engine (docs/analise/REA_rodada46.md). Clean-room: our own code from the behaviour, data layouts and
// formulas described in that document. Section numbers (§) below refer to it.

#include "core/Kepler.h"
#include "core/ModelParams.h"
#include "core/HostSlide.h"
#include "core/KeplerExoDelay.h"
#include "core/KeplerExoChorus.h"
#include "core/KeplerExoEq.h"
#include "core/KeplerExoReverb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

#include "core/Sha256.h"
#include "core/Song.h"

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE, or ARM64 through core/Simd.h (sse2neon)
#include "core/Simd.h"
#define LYNX_KEPLER_MXCSR 1
#endif

// the engine's float operations, one rounding each, in its order and rounding mode: no contraction, no reordering
// across rounding-mode changes
#if defined(__clang__)
#pragma clang fp contract(off)
#pragma STDC FENV_ACCESS ON
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off", "rounding-math")
#endif

namespace lynx
{
namespace
{
// ------------------------------------------------------------------------------------------------------ rounding (§1.2)
// The engine renders with the SSE rounding mode set to "toward zero" and denormals flushed (MXCSR | 0xE040); note-on,
// parameter and state-load code runs outside the render, in the default mode. Library functions (pow, tan, sin) are
// evaluated in round-to-nearest; their result is converted in the current mode.
#ifdef LYNX_KEPLER_MXCSR
struct RenderMode
{
    unsigned saved;
    RenderMode() noexcept : saved (_mm_getcsr()) { _mm_setcsr (saved | 0xE040u); }
    ~RenderMode() { _mm_setcsr (saved); }
};
struct NearestMode
{
    unsigned saved;
    NearestMode() noexcept : saved (_mm_getcsr()) { _mm_setcsr (saved & ~0x6000u); }
    ~NearestMode() { _mm_setcsr (saved); }
};
#else
struct RenderMode
{
};
struct NearestMode
{
};
#endif

// pow with base 2 as the DLL's statically linked library computes it (REA_rodada165g-kepler §1): the engine calls pow
// (double) for the oscillator increment and powf for the cutoff inside the render, where the rounding mode is "toward
// zero"; both libraries' results then differ from exp2 by one unit in the last place on ~0.1–2 % of the inputs, which
// self-oscillating patches amplify. The exponential is rebuilt step by step: x·ln 2 (as a head/tail pair for pow), the
// index n = x·64/ln 2 converted in the current rounding mode, the reduced argument r = x − n·ln 2/64 (fused on CPUs
// with FMA, the path FL takes on them), a Taylor polynomial (degree 3 for powf, 6 for pow) and the 2^(j/64) table
// (pow: head + tail; heads truncated to 24 bits) read from the user's engine DLL. Bit-exact against the DLL's pow/powf
// on 10⁶ random inputs in both rounding modes [EXEC].
struct CrtExp
{
    std::array<double, 64> full {}, head {}, tail {};
};
std::atomic<const CrtExp*> crtExp { nullptr };

// the polynomial steps are fused multiply-adds: hardware FMA where the CPU has it (x86: selected at run time; ARM64:
// always), std::fma otherwise
#define LYNX_KEPLER_CRT_POW(NAME, FMA, ATTR)                                                                               \
    ATTR double NAME##D (const CrtExp& t, double y) noexcept                                                                \
    {                                                                                                                    \
        constexpr double ln2h = 0x1.62e42ep-1, ln2t = 0x1.efa39ef35793cp-25, k64 = 0x1.71547652b82fep+6,                \
                         c64h = 0x1.62e42fp-7, c64t = -0x1.df473de6af278p-32;                                           \
        auto top = [] (double d) noexcept {                                                                              \
            std::uint64_t u;                                                                                             \
            std::memcpy (&u, &d, 8);                                                                                     \
            u &= 0xfffffffff8000000ull;                                                                                  \
            std::memcpy (&d, &u, 8);                                                                                     \
            return d;                                                                                                    \
        };                                                                                                               \
        /* log(2) as head + tail, then y·log(2) as zh + zl */                                                            \
        const double L = ln2h + ln2t, Lh = top (L);                                                                      \
        double l = ln2h - L;                                                                                             \
        l = l + ln2t;                                                                                                    \
        l = l + (L - Lh);                                                                                                \
        const double yh = top (y), yl = y - yh;                                                                          \
        double a = yl * l;                                                                                               \
        a = a + yl * Lh;                                                                                                 \
        a = a + l * yh;                                                                                                  \
        const double hh = Lh * yh, zh = hh + a;                                                                          \
        double zl = hh - zh;                                                                                             \
        zl = zl + a;                                                                                                     \
        const double kk = zh * k64;                                                                                      \
        if (! (kk <= 65536.0 && kk >= -68800.0))                                                                         \
            return std::exp2 (y);                                                                                        \
        const int n = (int) std::lrint (kk);                                                                             \
        const double dn = (double) n;                                                                                    \
        const int j = n & 63, m = (n - j) >> 6;                                                                          \
        double r = FMA (-dn, c64h, zh) + dn * c64t;                                                                      \
        r = r + zl;                                                                                                      \
        double p = FMA (0x1.6c16c16c16c17p-10, r, 0x1.1111111111111p-7);                                                \
        p = FMA (p, r, 0x1.5555555555555p-5);                                                                            \
        p = FMA (p, r, 0x1.5555555555555p-3);                                                                            \
        p = FMA (p, r, 0.5);                                                                                             \
        p = FMA (p, r, 1.0);                                                                                             \
        const double q = p * r;                                                                                          \
        double s5 = t.tail[(std::size_t) j] * q;                                                                         \
        double s1 = t.head[(std::size_t) j] * q;                                                                         \
        s5 = s5 + t.tail[(std::size_t) j];                                                                               \
        s1 = s1 + s5;                                                                                                    \
        s1 = s1 + t.head[(std::size_t) j];                                                                               \
        if (m <= -1022 || m >= 1024)                                                                                     \
            return std::ldexp (s1, m);                                                                                   \
        const std::uint64_t sc = (std::uint64_t) (m + 0x3ff) << 52;                                                      \
        double scale;                                                                                                    \
        std::memcpy (&scale, &sc, 8);                                                                                    \
        return s1 * scale;                                                                                               \
    }                                                                                                                    \
    ATTR float NAME##F (const CrtExp& t, float y) noexcept                                                                \
    {                                                                                                                    \
        const double z = (double) y * 0x1.62e42fefa39efp-1;                                                              \
        if (! (z <= 88.72283935546875 && z > -103.2789306640625))                                                        \
            return (float) std::exp2 ((double) y);                                                                       \
        const int n = (int) std::lrint (z * 0x1.71547652b82fep+6);                                                       \
        const double dn = (double) n;                                                                                    \
        const double r = FMA (-dn, 0x1.62e42fefa39efp-7, z);                                                             \
        double p = FMA (0x1.5555555555555p-3, r, 0.5);                                                                   \
        p = FMA (p, r, 1.0);                                                                                             \
        const double q = r * p;                                                                                          \
        const double T = t.full[(std::size_t) (n & 63)];                                                                 \
        double v = FMA (q, T, T);                                                                                        \
        std::uint64_t u;                                                                                                 \
        std::memcpy (&u, &v, 8);                                                                                         \
        u += (std::uint64_t) (std::int64_t) (n >> 6) << 52;                                                              \
        std::memcpy (&v, &u, 8);                                                                                         \
        return (float) v;                                                                                                \
    }

LYNX_KEPLER_CRT_POW (crtPow2Sw, std::fma, )
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__clang__) || defined(__GNUC__))
#define LYNX_KEPLER_HW_FMA 1
LYNX_KEPLER_CRT_POW (crtPow2Hw, __builtin_fma, __attribute__ ((target ("fma"))))
bool detectFma() noexcept
{
    // CPUID.1:ECX bit 12 (FMA) and bit 27 (OSXSAVE), and the OS saving the YMM state (XCR0 bits 1, 2)
    unsigned a = 1, b = 0, c = 0, d = 0;
    __asm__ ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    (void) b, (void) d;
    if ((c & (1u << 12)) == 0 || (c & (1u << 27)) == 0)
        return false;
    unsigned lo = 0, hi = 0;
    __asm__ ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return (lo & 6u) == 6u;
}
const bool cpuHasFma = detectFma();
#endif
#undef LYNX_KEPLER_CRT_POW

double crtPow2 (const CrtExp& t, double y) noexcept
{
#ifdef LYNX_KEPLER_HW_FMA
    if (cpuHasFma)
        return crtPow2HwD (t, y);
#endif
    return crtPow2SwD (t, y);
}

float crtPow2f (const CrtExp& t, float y) noexcept
{
#ifdef LYNX_KEPLER_HW_FMA
    if (cpuHasFma)
        return crtPow2HwF (t, y);
#endif
    return crtPow2SwF (t, y);
}

// powers of two through the library above once a DLL's tables are loaded (exp2 before that); other bases (parameter
// laws, outside the render) in extended precision rounded to double
double powD (double a, double b) noexcept
{
    if (a == 2.0)
    {
        if (const auto* t = crtExp.load (std::memory_order_acquire))
            return crtPow2 (*t, b);
        return std::exp2 (b);
    }
    return (double) std::pow ((long double) a, (long double) b);
}

float powF (float a, float b) noexcept
{
    if (a == 2.0f)
        if (const auto* t = crtExp.load (std::memory_order_acquire))
            return crtPow2f (*t, b);
    return (float) powD ((double) a, (double) b);
}
float tanF (float x) noexcept
{
    double r;
    {
        NearestMode m;
        r = std::tan ((double) x);
    }
    return (float) r;
}
float sinF (float x) noexcept
{
    double r;
    {
        NearestMode m;
        r = std::sin ((double) x);
    }
    return (float) r;
}
float lnF (float x) noexcept
{
    double r;
    {
        NearestMode m;
        r = std::log ((double) x);
    }
    return (float) r;
}

float bitsF (std::uint32_t u) noexcept
{
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}

std::uint32_t floatBits (float f) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}

// Kepler Exo's C runtime logf and tanhf (REA_rodada167d §5.1): the saturation's HQ mode divides their results by a
// small step, so their last bits matter. logf: a 129-entry table of 1/c and ln c (c = 1 + j/128) split in a high part
// (12 low bits cleared) and a low part (truncated), a degree-3 tail; the library takes its FMA variant on CPUs with
// FMA. tanhf: an odd rational below 1, else 1 − 2/(e^{2x} + 1) with e^{2x} from a 32-entry 2^{j/32} table.
struct ExoCrtTables
{
    float rcp[129], lnHi[129], lnLo[129], expHi[32];
    ExoCrtTables() noexcept
    {
        NearestMode m; // built on first use, which can be inside the render's rounding mode
        for (int j = 0; j < 129; ++j)
        {
            rcp[j] = (float) (1.0 / (0.5 * (1.0 + j / 128.0)));
            const auto L = std::log1p (j / 128.0);
            std::uint32_t u;
            const auto lf = (float) L;
            std::memcpy (&u, &lf, 4);
            u &= 0xfffff000u;
            std::memcpy (&lnHi[j], &u, 4);
            const auto d = L - (double) lnHi[j];
            auto t = (float) d;
            if (std::abs ((double) t) > std::abs (d))
                t = std::nextafter (t, 0.0f);
            lnLo[j] = t;
        }
        for (int j = 0; j < 32; ++j)
        {
            const auto ef = (float) std::exp2 (j / 32.0);
            std::uint32_t u;
            std::memcpy (&u, &ef, 4);
            u &= 0xffffc000u;
            std::memcpy (&expHi[j], &u, 4);
        }
    }
};
const ExoCrtTables& exoCrt() noexcept
{
    static const ExoCrtTables t;
    return t;
}
// the low parts of 2^{j/32} as the library stores them (not a plain rounding of the remainder: measured, §5.1)
constexpr std::uint32_t exoExpLoBits[32] = {
    0x00000000u, 0x39d86988u, 0x3aab0d9fu, 0x3a407404u, 0x3a2e0f1eu, 0x3a90e62du, 0x38f4dce0u, 0x3ad3bea3u, 0x3adfc146u, 0x39d39b9cu, 0x3ad4c982u, 0x3ac10c0cu, 0x3afb5aa6u, 0x3a856ad3u, 0x3a41f752u, 0x3a8fd607u, 0x391e6678u, 0x3aeebd1du, 0x398a39f4u, 0x3ab13329u, 0x3a9ca845u, 0x3ae6f619u, 0x3a923054u, 0x3aa07647u, 0x391f9958u, 0x3aeede5fu, 0x39cdeec0u, 0x3ae41b9du, 0x37c6e7c0u, 0x3a92e66fu, 0x3a95f454u, 0x38ecb6d0u };

// coshf of the same library: double evaluation cosh n·cosh f + sinh n·sinh f (n = trunc |x|, degree-13/12 polynomials
// in f, the library's 37-entry tables of sinh n, cosh n), rounded to float in the current mode; |x| < 2^-14 → 1
constexpr double exoSinhN[37] = { 0.0, 1.1752011936438014, 3.6268604078470186, 10.0178749274099, 27.28991719712775, 74.20321057778875, 201.71315737027922, 548.3161232732465, 1490.47882578955, 4051.54190208279, 11013.232874703392, 29937.070849248055, 81377.39570642984, 221206.6960033301, 601302.1420819726, 1634508.6862359024, 4443055.260253879, 12077476.376787627, 32829984.568665247, 89241150.48159362, 242582597.7048951, 659407867.2416073, 1792456423.0657957, 4872401723.124451, 13244561064.921736, 36002449668.69293, 97864804714.41937, 266024120300.8993, 723128532145.7375, 1965667148572.021, 5343237290762.23, 14524424832623.71, 39481480091340.34, 107321789892958.03, 291730871263727.44, 793006726156715.2, 2155615773557597.5 };
constexpr double exoCoshN[37] = { 1.0, 1.5430806348152437, 3.7621956910836314, 10.067661995777765, 27.308232836016487, 74.20994852478783, 201.7156361224559, 548.317035155212, 1490.479161252178, 4051.542025492594, 11013.232920103323, 29937.070865949758, 81377.39571257406, 221206.6960055904, 601302.1420828041, 1634508.686236208, 4443055.260253992, 12077476.376787668, 32829984.568665262, 89241150.48159362, 242582597.70489514, 659407867.2416073, 1792456423.0657957, 4872401723.124451, 13244561064.921736, 36002449668.69293, 97864804714.41937, 266024120300.8993, 723128532145.7375, 1965667148572.021, 5343237290762.23, 14524424832623.71, 39481480091340.34, 107321789892958.03, 291730871263727.44, 793006726156715.2, 2155615773557597.5 };
float exoCoshf (float xf) noexcept
{
    auto x = std::abs ((double) xf);
    if (x < 6.103515625e-05)
        return 1.0f;
    if (! (x < 36.12359947967774))
        return std::isnan (x) ? xf : (float) (0.5 * std::exp (x));
    const auto n = (int) x;
    const auto f = x - (double) n;
    const auto f2 = f * f;
    const auto f3 = f2 * f;
    auto s = f2 * 7.746188980094184e-13 + 1.605767931219399e-10;
    s = s * f2 + 2.5052117699413348e-08;
    s = s * f2 + 2.7557319191363643e-06;
    s = s * f2 + 0.0001984126984132424;
    s = s * f2 + 0.008333333333333299;
    s = s * f2 + 0.16666666666666666;
    s = s * f3;
    s = s + f;
    s = s * exoSinhN[n];
    auto c = f2 * 1.1639213881721737e-11 + 2.0874434983147137e-09;
    c = c * f2 + 2.755733507560166e-07;
    c = c * f2 + 2.4801587246062242e-05;
    c = c * f2 + 0.0013888888888981485;
    c = c * f2 + 0.04166666666666609;
    c = c * f2 + 0.5;
    c = c * f2 + 1.0;
    c = c * exoCoshN[n];
    return (float) (s + c);
}

bool exoFma() noexcept
{
#if defined(LYNX_KEPLER_HW_FMA)
    return cpuHasFma;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}

// a fused multiply-add in the current rounding mode (the hardware instruction where the library would use it)
#if defined(LYNX_KEPLER_HW_FMA)
__attribute__ ((target ("fma"))) float exoFmaHw (float a, float b, float c) noexcept { return __builtin_fmaf (a, b, c); }
float exoFmaf (float a, float b, float c) noexcept { return exoFmaHw (a, b, c); }
#else
float exoFmaf (float a, float b, float c) noexcept { return std::fmaf (a, b, c); }
#endif

float exoLogf (float x) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &x, 4);
    if (! (x > 0.0f) || (u & 0x7f800000u) == 0x7f800000u)
        return (float) std::log ((double) x);
    auto e = (float) ((int) (u >> 23) - 127);
    auto m = u & 0x007fffffu;
    if (e == -127.0f) // subnormal: normalise through 1 | m − 1
    {
        auto v = bitsF (m | 0x3f800000u) - 1.0f;
        std::uint32_t w;
        std::memcpy (&w, &v, 4);
        e = (float) ((int) (w >> 23) - 253);
        m = w & 0x007fffffu;
        u = w;
    }
    const auto& T = exoCrt();
    const bool fused = exoFma();
    if (std::abs (x - 1.0f) < 0.0625f)
    {
        const auto f = x - 1.0f;
        const auto t = f / (2.0f + f);
        const auto ft = f * t;
        const auto w = t + t;
        const auto w2 = w * w;
        const auto w3 = w * w2;
        if (fused)
            return f + exoFmaf (w3, exoFmaf (w2, 0.012500000186264515f, 0.0833333358168602f), -ft);
        return f + ((w2 * 0.012500000186264515f + 0.0833333358168602f) * w3 - ft);
    }
    const auto v = (u & 0x007f0000u) + ((u & 0x8000u) << 1);
    const auto j = v >> 16;
    const auto r = (bitsF (v | 0x3f000000u) - bitsF (m | 0x3f000000u)) * T.rcp[j];
    if (fused)
    {
        const auto pq = exoFmaf (r, 0.3333333432674408f, 0.5f);
        const auto q = exoFmaf (pq, r * r, r);
        auto s = exoFmaf (e, 3.194618329871446e-05f, -q);
        const auto hi = exoFmaf (e, 0.693115234375f, T.lnHi[j]);
        s = s + T.lnLo[j];
        return hi + s;
    }
    auto pq = r * 0.3333333432674408f + 0.5f;
    pq = pq * (r * r);
    const auto q = r + pq;
    auto s = 3.194618329871446e-05f * e - q;
    s = s + T.lnLo[j];
    const auto hi = 0.693115234375f * e + T.lnHi[j];
    return hi + s;
}

float exoTanhf (float x) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &x, 4);
    const auto a = u & 0x7fffffffu;
    if (a < 0x39000000u)
        return x;
    if (a > 0x7f800000u)
        return x + x;
    const auto sg = 1.0f - (float) (a != u ? 1 : 0) * 2.0f;
    const auto y = sg * x;
    float r = 1.0f;
    if (y > 10.0f)
        r = 1.0f;
    else if (! (1.0f < y))
    {
        const auto y2 = y * y;
        const auto y3 = y2 * y;
        float num, den;
        if (0.9f > y)
        {
            num = (y2 * bitsF (0x384d2b7du) - bitsF (0x3abfbc9eu)) * y2 - bitsF (0x3e9058e0u);
            den = y2 * bitsF (0x3eaf769cu) + bitsF (0x3f588550u);
        }
        else
        {
            num = (y2 * bitsF (0x382089d8u) - bitsF (0x3aa18e00u)) * y2 - bitsF (0x3e7679b1u);
            den = y2 * bitsF (0x3e95c65fu) + bitsF (0x3f38db60u);
        }
        r = num * y3 / den + y;
    }
    else
    {
        auto t = y + y;
        auto k = t * 46.16624069213867f;
        k = k > 0.0f ? k + 0.5f : k - 0.5f;
        const auto n = (int) k;
        const auto j = n & 31;
        const auto mm = n & ~31;
        const auto& T = exoCrt();
        const auto t1 = T.expHi[j], t2 = bitsF (exoExpLoBits[j]);
        t = t - (float) n * 0.02165985107421875f;
        const auto lo = (float) (-n) * 9.983182280848268e-07f;
        const auto ex = mm / 32;
        const auto half = ex / 2;
        const auto s1 = bitsF ((std::uint32_t) (half + 0x7f) << 23), s2 = bitsF ((std::uint32_t) (ex - half + 0x7f) << 23);
        const auto rr = lo + t;
        auto p = (rr * 0.1666666716337204f + 0.5f) * (rr * rr);
        p = p + lo;
        p = p + t;
        p = p * (t2 + t1);
        p = p + t2;
        p = p + t1;
        p = p * s1 * s2;
        r = 1.0f - 2.0f / (p + 1.0f);
    }
    return r * sg;
}


// sine and cosine as the engine's library computes them for the chorus filter design (REA_rodada165g-flbeta165k §2):
// for |x| ≥ 2⁻¹³ a four-lane float sine run on (x, |x| + π/2) — Cody-Waite reduction by π in four float products, an
// odd degree-9 polynomial, the parity of the quotient flipping the sign; below 2⁻¹³ sin = x, cos = 1 − x²/2. Every step
// in float, in the current rounding mode. Bit-exact against the DLL on 5·10⁵ inputs |x| < 3.2, both rounding modes
// [EXEC]; the correctly rounded sin/cos differ from it by one ulp on 16–27 % of the inputs (at 44.1 kHz on both).
// Valid for |x| < 10⁴ (the library's large-argument path is not needed: the argument is 2π·7 kHz/rate).
float engineSinLane (float x) noexcept
{
    const auto ax = bitsF (floatBits (x) & 0x7fffffffu);
    const auto sign = floatBits (x) & 0x80000000u;
    auto t = ax * bitsF (0x3ea2f983u);
    t = t + bitsF (0x4b400000u);
    const auto parity = floatBits (t) << 31;
    const auto n = t - bitsF (0x4b400000u);
    const auto a = bitsF (0x40490000u) * n, b = bitsF (0x3a7da000u) * n, c = bitsF (0x34222000u) * n, d = bitsF (0x2cb4611au) * n;
    auto r = ax - a;
    r = r - b;
    r = r - c;
    r = r - d;
    const auto r2 = r * r;
    r = bitsF (floatBits (r) ^ parity);
    auto p = bitsF (0x362edef8u) * r2;
    p = p + bitsF (0xb94fb7ffu);
    p = p * r2;
    p = p + bitsF (0x3c088766u);
    p = p * r2;
    p = p + bitsF (0xbe2aaaa6u);
    p = r2 * p;
    p = p * r;
    r = r + p;
    return bitsF (floatBits (r) ^ sign);
}

void engineSinCos (float x, float& s, float& c) noexcept
{
    const auto ax = bitsF (floatBits (x) & 0x7fffffffu);
    if (! (ax >= bitsF (0x39000000u)))
    {
        auto q = ax * ax;
        q = q * 0.5f;
        s = x;
        c = 1.0f - q;
        return;
    }
    const auto y = ax + bitsF (0x3fc90fdbu);
    s = engineSinLane (x);
    c = engineSinLane (y);
}

// --------------------------------------------------------------------------------------------- installation data (§2)
constexpr const char* engineSha256 = "baf37a1d8266aaafc55016de259be4b95f1a136a1fc13ba97514c72e2afc63ab";
// Kepler Exo's engine (Plugins\Fruity\Generators\Kepler Exo\engine_x64.dll, REA_rodada167d): the same render code with
// its own parameter setters, defaults and state loader; the shape and noise tables at the same addresses, the library's
// 2^(j/64) tables 0x70 bytes higher
constexpr const char* exoEngineSha256 = "10ed2464e1074934c57ad98d89ef3bb235da38ea8df150b3ff18564262a25d6c";
constexpr int lfoPoints = 2170;
constexpr int noisePoints = 0xA6905;

struct Tables
{
    std::array<float, lfoPoints + 1> lfo {};  // LFO1 shape, −1…1, wrap point appended
    std::vector<std::int16_t> noise;          // noise oscillator
    std::vector<float> blit;                  // band-limited impulse rows (1025 × 32, §3.2)
    bool exo = false;                         // read from Kepler Exo's engine
};

std::vector<float> blitRows()
{
    // 16th-order low-pass impulse response (8 biquads in double, cutoff π/1024 at the 1024× resolution), first 768
    // samples dropped, scaled by 1/max, linear fade over the last 1024 samples; row r holds K[j·1024 − r] (j < 16),
    // duplicated so a 16-sample window can start at any ring position (§3.2)
    static constexpr double Q[8] = { 0.50241929, 0.52249861, 0.56694403, 0.64682178, 0.78815462, 1.0606777, 1.7224471, 5.1011486 };
    constexpr int d = 1024, n = 16384, pre = (3 * d) / 4, total = n + pre;
    // the DLL's cosine of π/1024 is one unit in the last place below the correctly rounded value [EXEC: table match]
    const double w = 3.141592653589793 / d, s = std::sin (w), c = std::nextafter (std::cos (w), 0.0);
    struct Stage { double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    std::array<Stage, 8> st {};
    for (int i = 0; i < 8; ++i)
    {
        const auto al = 0.5 / Q[i] * s, a0 = 1.0 + al;
        st[(std::size_t) i] = { (1.0 - c) * 0.5 / a0, (1.0 - c) / a0, (1.0 - c) * 0.5 / a0, (-2.0 * c) / a0, (1.0 - al) / a0 };
    }
    std::vector<float> f ((std::size_t) total);
    double peak = 0.0;
    for (int i = 0; i < total; ++i)
    {
        double x = i == 0 ? 1.0 : 0.0;
        for (auto& g : st)
        {
            const auto y = (((x + g.x2) * g.b0 + g.b2 * g.x1) - g.a1 * g.y1) - g.a2 * g.y2; // x1 weighted by b2 (= b0)
            g.x2 = g.x1, g.x1 = x, g.y2 = g.y1, g.y1 = y;
            x = y;
        }
        peak = std::max (peak, x);
        f[(std::size_t) i] = (float) x;
    }
    // 1/max as the DLL's reciprocal estimate + Newton step gives it on the analysed machine: one unit above 1/max [EXEC]
    const auto inv = std::nextafter (1.0f / (float) peak, 1e30f);
    std::vector<float> k ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        k[(std::size_t) i] = f[(std::size_t) (i + pre)] * 1.0f * inv;
    for (int i = 0; i < 1024; ++i)
        k[(std::size_t) (n - 1024 + i)] *= 1.0f - (float) i / 1024.0f;
    std::vector<float> rows (1025 * 32);
    for (int r = 0; r < 1025; ++r)
        for (int j = 0; j < 16; ++j)
        {
            const auto at = j * 1024 - r;
            rows[(std::size_t) (r * 32 + j)] = rows[(std::size_t) (r * 32 + 16 + j)] = at >= 0 ? k[(std::size_t) at] : 0.0f;
        }
    return rows;
}

std::shared_ptr<const Tables> loadTables (const std::string& path)
{
    static std::mutex lock;
    static std::map<std::string, std::weak_ptr<const Tables>> cache;
    std::lock_guard<std::mutex> g (lock);
    if (auto t = cache[path].lock())
        return t;

    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        auto local = path;
        std::replace (local.begin(), local.end(), '\\', '/');
        in.open (local, std::ios::binary);
    }
    if (! in)
        return nullptr;
    const std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (b.size() < 0x1000)
        return nullptr;
    const auto hash = peImageSha256Hex (b); // (a re-signed copy of the same build is the same image, round 169g)
    const bool exo = hash == exoEngineSha256;
    if (! exo && hash != engineSha256)
        return nullptr;
    auto rd = [&] (std::size_t off, auto& v) { if (off + sizeof (v) <= b.size()) std::memcpy (&v, b.data() + off, sizeof (v)); };
    std::uint32_t pe = 0;
    rd (0x3c, pe);
    std::uint16_t sections = 0, optSize = 0;
    rd (pe + 6, sections);
    rd (pe + 20, optSize);
    struct Section { std::uint32_t va, vs, ro, rs; };
    std::vector<Section> secs;
    for (std::uint32_t i = 0, o = pe + 24 + optSize; i < sections; ++i, o += 40)
    {
        Section s {};
        rd (o + 8, s.vs);
        rd (o + 12, s.va);
        rd (o + 16, s.rs);
        rd (o + 20, s.ro);
        secs.push_back (s);
    }
    auto offset = [&] (std::uint32_t rva) -> std::size_t {
        for (const auto& s : secs)
            if (rva >= s.va && rva < s.va + std::min (s.vs, s.rs))
                return (std::size_t) (s.ro + rva - s.va);
        return b.size();
    };
    auto t = std::make_shared<Tables>();
    t->exo = exo;
    // LFO shape: 2 170 int16, centred on (min + max)/2 and scaled by 1/((max − min)/2) in float (§2.1)
    const auto lo = offset (0x252ED0);
    if (lo + 2 * lfoPoints > b.size())
        return nullptr;
    std::array<std::int16_t, lfoPoints> raw {};
    std::memcpy (raw.data(), b.data() + lo, sizeof (raw));
    const auto mn = (float) *std::min_element (raw.begin(), raw.end()), mx = (float) *std::max_element (raw.begin(), raw.end());
    const auto centre = (mx + mn) * 0.5f, half = (mx - mn) * 0.5f, scale = 1.0f / half;
    for (int i = 0; i < lfoPoints; ++i)
        t->lfo[(std::size_t) i] = ((float) raw[(std::size_t) i] - centre) * scale;
    t->lfo[lfoPoints] = t->lfo[0];
    // noise oscillator: 683 269 int16 (§2.2)
    const auto no = offset (0x105CC0);
    if (no + 2 * (std::size_t) noisePoints > b.size())
        return nullptr;
    t->noise.resize ((std::size_t) noisePoints);
    std::memcpy (t->noise.data(), b.data() + no, 2 * (std::size_t) noisePoints);
    // the library's 2^(j/64) tables (§1 of REA_rodada165g-kepler): powf's rounded values, pow's 24-bit heads and tails
    if (crtExp.load (std::memory_order_acquire) == nullptr)
    {
        const auto fo = offset (exo ? 0xF1A20 : 0xF19B0), ho = offset (exo ? 0xF1C20 : 0xF1BB0), to = offset (exo ? 0xF1E20 : 0xF1DB0);
        if (fo + 512 > b.size() || ho + 512 > b.size() || to + 512 > b.size())
            return nullptr;
        static CrtExp crt;
        std::memcpy (crt.full.data(), b.data() + fo, 512);
        std::memcpy (crt.head.data(), b.data() + ho, 512);
        std::memcpy (crt.tail.data(), b.data() + to, 512);
        crtExp.store (&crt, std::memory_order_release);
    }
    t->blit = blitRows();
    cache[path] = t;
    return t;
}

// ------------------------------------------------------------------------------------------------- parameter laws (§4)
constexpr int paramCount = 258;

// 11-point tables interpolated at 10·x (§4.1)
constexpr float tDcoLfo[11] = { 0.0f, 0.0011639329604804516f, 0.008127881214022636f, 0.015058374963700771f, 0.021955734118819237f, 0.028820276260375977f, 0.05259217321872711f, 0.07818635553121567f, 0.12057366222143173f, 0.17728076875209808f, 0.2138838768005371f };
constexpr float tCutoffLog2[11] = { 2.0f, 2.4594316482543945f, 3.321928024291992f, 4.321928024291992f, 5.683696269989014f, 6.954196453094482f, 8.543031692504883f, 10.471674919128418f, 12.388017654418945f, 15.163492202758789f, 15.609640121459961f };
constexpr float tDecay[11] = { 0.001500000013038516f, 0.0027000000700354576f, 0.0430000014603138f, 0.14100000262260437f, 0.328000009059906f, 0.6510000228881836f, 1.1440000534057617f, 2.052999973297119f, 3.9560000896453857f, 8.465999603271484f, 10.12399959564209f };
constexpr float tRelease[11] = { 0.0010000000474974513f, 0.0020000000949949026f, 0.019999999552965164f, 0.1120000034570694f, 0.32100000977516174f, 0.5720000267028809f, 1.062999963760376f, 1.9639999866485596f, 3.380000114440918f, 6.909999847412109f, 10.967000007629395f };
constexpr float tLfoCutoff[11] = { 0.0f, 0.09139633178710938f, 0.3803834319114685f, 0.6620035171508789f, 0.968460202217102f, 1.2373158931732178f, 1.4824888706207275f, 1.8573511838912964f, 2.2755351066589355f, 2.6644060611724854f, 2.819999933242798f };
constexpr float tAttack[11] = { 0.003000000026077032f, 0.004000000189989805f, 0.013000000268220901f, 0.05000000074505806f, 0.12800000607967377f, 0.24300000071525574f, 0.4009999930858612f, 0.699999988079071f, 1.2999999523162842f, 2.4030001163482666f, 3.0490000247955322f };
constexpr float tSubVol[11] = { 0.0f, 0.009999999776482582f, 0.019999999552965164f, 0.05000000074505806f, 0.09000000357627869f, 0.14000000059604645f, 0.23000000417232513f, 0.46000000834465027f, 0.6800000071525574f, 0.9200000166893005f, 1.0f };
constexpr float tEnvCutoff[11] = { 0.0f, 0.38939768075942993f, 1.6811637878417969f, 2.8879127502441406f, 4.016843318939209f, 5.039211273193359f, 6.068634986877441f, 7.287972927093506f, 8.475396156311035f, 8.956632614135742f, 8.972286224365234f };
constexpr float tLfoDelay[11] = { 0.0f, 0.2290000021457672f, 0.44699999690055847f, 0.546999990940094f, 0.8330000042915344f, 1.0f, 2.0739998817443848f, 4.775000095367432f, 5.611999988555908f, 6.77400016784668f, 6.705999851226807f };
constexpr float tLfoRate[11] = { 7.4422221183776855f, 5.807354927062988f, 4.349882125854492f, 3.6563470363616943f, 3.113062620162964f, 2.6711947917938232f, 2.0462937355041504f, 1.3093280792236328f, 0.8514775037765503f, 0.28379297256469727f, 0.0f };
constexpr float tArpRate[11] = { 0.8109999895095825f, 0.7540000081062317f, 0.574999988079071f, 0.4099999964237213f, 0.210999995470047f, 0.14399999380111694f, 0.10199999809265137f, 0.07699999958276749f, 0.05299999937415123f, 0.029999999329447746f, 0.020999999716877937f };
constexpr int tArpSync[13] = { 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6 };

// table lookup as the engine: i = trunc(10x); i ≥ 10 → last point; (1 − f)·T[i] + f·T[i+1]
float table (const float (&t)[11], float x) noexcept
{
    const auto p = x * 10.0f;
    const auto i = (int) p;
    if (i >= 10)
        return t[10];
    const auto f = p - (float) i;
    const auto hi = f * t[i + 1];
    return (1.0f - f) * t[i] + hi;
}

// panel position → internal parameter ("Kepler-1" layout, §1.1)
int panelToInternal (int k) noexcept
{
    static constexpr int map[36] = { 0x55, 0x59, 0x58, 0x56, 0x57, 0x30, 0x31, 0x32, 0x33, 0x51, 0x00, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x22,
                                     0x24, 0x25, 0x26, 0x2b, 0x2c, 0x2d, 0x27, 0x2f, 0x3d, 0x3e, 0x3f, 0x40, 0x45, 0x52, 0x53, 0x48, 0x34, 0x35 };
    return k >= 0 && k < 36 ? map[k] : 0;
}

float defaultParam (int i) noexcept
{
    switch (i)
    {
        case 3: case 8: case 10: case 0xf: case 0x11: case 0x17: case 0x20: case 0x21: case 0x25: case 0x28: case 0x49: case 0x5a: case 0x9a:
            return 1.0f;
        case 0xb: case 0xd: case 0x12: case 0x2f: case 0x30: case 0x3e: case 0x42: case 0x43: case 0x48: case 0x4a: case 0x53:
        case 0x5c: case 0x5d: case 0x5e: case 0x5f: case 0x60: case 0x61: case 0x62: case 0x63:
        case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d:
        case 0x95: case 0x96: case 0x100: case 0x101:
            return 0.5f;
        case 0xe: case 0x36: case 0x3f:
            return 0.33f;
        default:
            return 0.0f;
    }
}

// Kepler Exo's defaults (REA_rodada167d §2.1: the engine's default table, doubles rounded to float)
float exoDefaultParam (int i) noexcept
{
    switch (i)
    {
        case 3: case 8: case 10: case 0xf: case 0x11: case 0x17: case 0x20: case 0x21: case 0x25: case 0x28: case 0x49: case 0x5a:
        case 0x6e: case 0x76: case 0x9a:
            return 1.0f;
        case 0xb: case 0xd: case 0x12: case 0x2f: case 0x30: case 0x3e: case 0x42: case 0x43: case 0x48: case 0x4a: case 0x53:
        case 0x5c: case 0x5d: case 0x5e: case 0x5f: case 0x60: case 0x61: case 0x62: case 0x63:
        case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d:
        case 0x70: case 0x72: case 0x95: case 0x96: case 0x98: case 0x99: case 0x100: case 0x101:
            return 0.5f;
        case 0xe: case 0x36: case 0x3f:
            return 0.33f;
        case 0x46: return (float) 0.09;
        case 0x71: return (float) 0.607166666537523;
        case 0x74: return (float) 0.749833333306015;
        case 0x78: return (float) 0.75;
        case 0x7a: return (float) 0.201308285817504;
        case 0x7b: return (float) 0.786500000394881;
        case 0x7d: case 0x7e: case 0x82: return (float) 0.52309999987483;
        case 0x7f: return (float) 0.776200000196695;
        case 0x80: return (float) 0.382000000216067;
        case 0x81: return (float) 0.225599999539554;
        case 0x83: return (float) 0.800300000235438;
        case 0x84: return (float) 0.800333333201706;
        case 0x85: return (float) 0.486899999901652;
        default:
            return 0.0f;
    }
}

struct Patch
{
    std::array<float, paramCount> P {};
    std::vector<std::pair<int, float>> order; // the values in the order the state applies them
    bool arpRestartAligned = false; // byte of version > 1 (§1.1)
    bool usable = false;
    // "Kepler-2" (Kepler Exo) states: every value by internal index, the modulation-matrix slots (source, destination)
    bool exo = false;
    int version = 0;
    bool slotsSet = false;
    std::array<std::pair<int, int>, 8> slots {};
};

std::uint32_t u32 (const std::vector<std::uint8_t>& s, std::size_t at) noexcept
{
    std::uint32_t v = 0;
    if (at + 4 <= s.size())
        std::memcpy (&v, s.data() + at, 4);
    return v;
}
float f32At (const std::vector<std::uint8_t>& s, std::size_t at) noexcept
{
    float v = 0.0f;
    if (at + 4 <= s.size())
        std::memcpy (&v, s.data() + at, 4);
    return v;
}

// "Kepler-2" layout (REA_rodada167d §1): u32 version, u32 n, n floats (internal index order, < 0x9b used), u32 matrix
// flag (1: u32 count, count × (i32 source, i32 destination)), version > 2: arpeggiator-alignment byte, ×4-oversampling
// byte; version > 3: the alternative-oscillator byte
Patch parseExoState (const std::vector<std::uint8_t>& s)
{
    Patch p;
    p.exo = true;
    for (int i = 0; i < paramCount; ++i)
        p.P[(std::size_t) i] = exoDefaultParam (i);
    for (auto& sl : p.slots)
        sl = { -1, -1 };
    const auto version = (int) u32 (s, 0x19);
    p.version = version;
    std::size_t at = 0x1D;
    if (version > 0)
    {
        const auto n = (int) u32 (s, at);
        at += 4;
        if (n < 0 || n > 4096 || at + (std::size_t) n * 4 > s.size())
            return p;
        for (int k = 0; k < n; ++k, at += 4)
        {
            const auto v = f32At (s, at);
            if (k < 0x9b)
            {
                if (! std::isfinite (v))
                    return p;
                p.P[(std::size_t) k] = v;
                p.order.emplace_back (k, v);
            }
        }
        if (at + 4 > s.size())
            return p;
        const auto flag = u32 (s, at);
        at += 4;
        if (flag == 1)
        {
            if (at + 4 > s.size())
                return p;
            const auto count = (int) u32 (s, at);
            at += 4;
            if (count < 0 || count > 4096 || at + (std::size_t) count * 8 > s.size())
                return p;
            for (int k = 0; k < count; ++k, at += 8)
                if (k < 8)
                    p.slots[(std::size_t) k] = { (int) (std::int32_t) u32 (s, at), (int) (std::int32_t) u32 (s, at + 4) };
            p.slotsSet = true;
        }
        if (version > 2)
        {
            if (at + 2 > s.size())
                return p;
            p.arpRestartAligned = (std::int8_t) s[at] > 0;
            if ((std::int8_t) s[at + 1] > 0) // ×4 oversampling: not modelled (no factory preset uses it)
                return p;
            at += 2;
        }
        if (version > 3)
        {
            if (at + 1 > s.size() || (std::int8_t) s[at] > 0) // the alternative oscillator path: not modelled
                return p;
        }
    }
    p.usable = true;
    return p;
}

// FL 2026 saves Kepler Exo's engine state inside a 16-byte wrapper (u32 1, u32 1, u32 engine-state size, u32 0), then
// the engine state, then the wrapper's own trailer (REA_rodada167d §1, every factory .fst): the engine state's offset
std::size_t exoWrapperSize (const std::vector<std::uint8_t>& s)
{
    if (s.size() >= 16 + 0x1D && u32 (s, 0) == 1 && u32 (s, 4) == 1 && u32 (s, 12) == 0 && 16 + (std::size_t) u32 (s, 8) <= s.size()
        && u32 (s, 16 + 12) == 9 && std::memcmp (s.data() + 32, "Kepler-2", 9) == 0)
        return 16;
    return 0;
}

Patch parseState (const std::vector<std::uint8_t>& s)
{
    if (const auto w = exoWrapperSize (s); w > 0)
        return parseExoState (std::vector<std::uint8_t> (s.begin() + (std::ptrdiff_t) w, s.begin() + (std::ptrdiff_t) (w + u32 (s, 8))));
    if (s.size() >= 0x1D && u32 (s, 12) == 9 && std::memcmp (s.data() + 16, "Kepler-2", 9) == 0)
        return parseExoState (s);
    Patch p;
    for (int i = 0; i < paramCount; ++i)
        p.P[(std::size_t) i] = defaultParam (i);
    // u32 0, u32 size, u32 0, then the stream: u32 9 + "Kepler-1\0", u32 version, u32 n, n floats, …
    if (s.size() < 0x21 || u32 (s, 12) != 9 || std::memcmp (s.data() + 16, "Kepler-1", 9) != 0)
        return p;
    const auto version = (int) u32 (s, 0x19);
    if (version < 2 || version > 4)
        return p;
    const auto n = (int) u32 (s, 0x1D);
    if (n < 1 || n > 64 || 0x21 + (std::size_t) n * 4 > s.size())
        return p;
    std::size_t at = 0x21;
    for (int k = 0; k < n; ++k, at += 4)
    {
        const auto v = f32At (s, at);
        if (! std::isfinite (v))
            return p;
        if (k < 36)
        {
            p.P[(std::size_t) panelToInternal (k)] = v;
            p.order.emplace_back (panelToInternal (k), v);
        }
    }
    if (at + 9 > s.size())
        return p;
    p.arpRestartAligned = (std::int8_t) s[at] > 0;
    p.P[0x49] = f32At (s, at + 1);
    p.P[0x0b] = f32At (s, at + 5);
    if (! std::isfinite (p.P[0x49]) || ! std::isfinite (p.P[0x0b]))
        return p;
    p.order.emplace_back (0x49, p.P[0x49]);
    p.order.emplace_back (0x0b, p.P[0x0b]);
    at += 9;
    if (version > 2)
    {
        if (at + 3 > s.size())
            return p;
        // gate, chorus noise and ×4 oversampling bytes: only the "off" values are modelled (all FL data)
        if ((std::int8_t) s[at] > 0 || (std::int8_t) s[at + 1] > 0 || (std::int8_t) s[at + 2] > 0)
            return p;
    }
    if (version < 4 && p.P[0x31] >= 0.5f)
    {
        // old states with LFO1 BPM sync: the loader rewrites the stored rate (REA_rodada33 §3.4)
        const auto x = p.P[0x30];
        const auto e = powF (2.0f, x * 7.0f);
        const auto nOld = (int) ((e - 1.0f) * 48.370079f + 1.1f);
        const auto m = (int) (9216.0f / (float) nOld + 0.1f);
        const auto l = lnF ((float) (m - 1) * 0.020673936f + 1.0f);
        p.P[0x30] = 1.0f - l * 0.2060993f;
        p.order.emplace_back (-1, p.P[0x30]); // stored without running the setter (REA_rodada33 §3.4)
    }
    p.usable = true;
    return p;
}

// ------------------------------------------------------------------------------------------------ oscillator (§3.2)
struct Osc
{
    float subPoint = 0.5f, pw = 0.0f, saw = 1.0f, pulse = 1.0f, sub = 0.0f, leakMax = 1.0f;
    float out = 0.0f;
    double inc = 0.01, phase = 0.0;
    float sawAtWrap = 0.0f, pulseAtWrap = 0.0f, offset = 0.0f, leak = 0.999f, integ = 0.5f;
    int ring = 0;
    std::array<float, 16> buf {};
    bool pulseHigh = false, subDone = false;
    float subSign = 1.0f;
    // Kepler Exo's DCO2 hard sync (REA_rodada167d §3.2): the master's wrap restarts the slave inside the next sample
    Osc* slave = nullptr;
    bool syncOn = false, reset = false;
    float resetFrac = 0.0f, shapeOut = 0.0f; // shapeOut: the naive waveform (cross-modulation / filter-FM shape 5)

    void impulse (const float* rows, float pos, float amp) noexcept
    {
        pos += offset;
        float x = 0.0f;
        if (pos >= 0.0f)
            x = std::min (pos, 1.0f);
        const auto row = (int) (x * 1024.0f);
        const auto* r = rows + row * 32 + 16 - ring;
        for (int j = 0; j < 16; ++j)
            buf[(std::size_t) j] = r[j] * amp + buf[(std::size_t) j];
    }

    void edges (const float* rows, float prev) noexcept
    {
        if (! pulseHigh && ! ((double) pw > phase))
        {
            const auto pos = (std::max (pw, prev) - prev) / (float) inc;
            pulseHigh = true;
            impulse (rows, pos, -pulseAtWrap);
        }
        if (! subDone && ! ((double) subPoint > phase))
        {
            const auto pos = (subPoint - prev) / (float) inc;
            subDone = true;
            subSign = -subSign;
            impulse (rows, pos, subSign * sub);
        }
        const auto whole = (int) phase;
        if (whole == 0)
            return;
        sawAtWrap = saw;
        phase = phase - (double) whole;
        pulseAtWrap = pulse;
        const auto amp = saw + pulse;
        impulse (rows, (float) (1.0 - phase / inc), amp);
        pulseHigh = subDone = false;
        if (phase >= (double) pw)
        {
            const auto pos = ((pw + 1.0f) - prev) / (float) inc;
            pulseHigh = true;
            impulse (rows, pos, -pulseAtWrap);
        }
        if (phase >= (double) subPoint)
        {
            const auto pos = ((subPoint + 1.0f) - prev) / (float) inc;
            subDone = true;
            subSign = -subSign;
            impulse (rows, pos, subSign * sub);
        }
        if (slave != nullptr && slave->syncOn)
        {
            slave->resetFrac = (float) (phase / inc);
            slave->reset = true;
        }
    }

    void tick (const float* rows) noexcept
    {
        if (! reset)
        {
            const auto prev = (float) phase;
            phase = inc + phase;
            edges (rows, prev);
        }
        else
        {
            // synced restart: run up to the master's wrap, step the waveform back to its start, run the rest
            reset = false;
            const auto prev = phase;
            const auto f = 1.0f - resetFrac;
            const auto part = (float) inc * f;
            phase = (double) part + prev;
            edges (rows, (float) prev);
            auto amp = (float) phase * saw;
            if (pulseHigh)
            {
                amp = amp + pulseAtWrap;
                pulseHigh = false;
            }
            if (0.0f < subSign || subSign == 0.0f)
                amp = amp - sub;
            subSign = -1.0f;
            subDone = false;
            impulse (rows, f, amp);
            offset = f;
            phase = (double) ((float) inc * resetFrac);
            edges (rows, 0.0f);
            offset = 0.0f;
        }
        auto v = integ - (float) inc * saw;
        integ = v;
        v = (v + buf[(std::size_t) ring]) * leak;
        integ = v;
        out = (v + v) - saw;
        buf[(std::size_t) ring] = 0.0f;
        ring = (ring + 1) & 15;
        auto w = (((float) phase + (float) phase) - 1.0f) * saw;
        w = pulseHigh ? w + pulse : w - pulse;
        shapeOut = subSign * sub + w;
    }
};

// ------------------------------------------------------------------------------------------------------- voice (§3.1)
enum class PwSource { lfo, manual, envelope };

struct Voice
{
    std::array<float, paramCount> p {}; // the voice's copy of the parameters
    Osc osc;
    int note = -1, age = 0;
    float gate = 0.0f;
    // envelopes
    float env1 = 0.0f, dcy1 = 0.0f, dcyK1 = 1.0f, atkK1 = 1.0f, sus1 = 0.0f, oneMinusSus1 = 1.0f, relK1 = 1.0f;
    int st1 = 2;
    float env2 = 0.0f, dcy2 = 0.0f, dcyK2 = 1.0f, atkK2 = 1.0f, sus2 = 0.0f, oneMinusSus2 = 1.0f, relK2 = 1.0f;
    int st2 = 2;
    // modulation amounts
    float lfo1Dco = 0.0f, lfo2Dco = 0.0f, env2Dco = 0.0f, pwAmount = 0.0f;
    PwSource pwSource = PwSource::lfo;
    float noiseVol = 0.01f, dco1Level = 1.0f;
    float cutoffHz = 0.0f, resonance = 0.0f, envCutoff = 0.0f, lfo1Cutoff = 0.0f, lfo2Cutoff = 0.0f, polarity = 1.0f, filterFm = 0.0f;
    int slope = 3;
    bool envVca = true, vcaSmooth = false;
    float octave = 0.0f, pitchExpr = 0.0f, bendRange = 0.0f, dcoPitch = 0.0f;
    float glideTarget = 0.0f, glide = 0.0f, glideK = 1.0f, glideStep = 0.0f;
    bool glideLinear = false;
    // mod-matrix inputs (X/Y) and update counter
    float x = 0.0f, y = 0.0f;
    int matrixCount = 0, matrixInterval = 44;
    // VCA
    float velGain = 0.0f, velGainEnv = 0.0f, velocity = 0.0f, vca = 0.0f, vcaK = 0.0f, amp = 0.0f, ampK = 0.0f;
    float panL = 1.0f, panR = 1.0f, panLs = 1.0f, panRs = 1.0f, panK = 0.0f;
    bool fresh = false;
    int noiseIndex = 0;
    float laneCut = 0.0f, laneRes = 0.0f;
    // rate-derived
    float fs = 44100.0f;
    double incScale = 1.0 / 88200.0, rate = 44100.0;
    // Kepler Exo (REA_rodada167d §3): DCO2, cross-modulation, AM, VCA source, matrix sources and amounts
    Osc osc2;
    bool dco2On = false;
    float lfo1Dco2 = 0.0f, lfo2Dco2 = 0.0f, env2Dco2 = 0.0f, pw2Amount = 0.0f, dco2Pitch = 0.0f, fine2 = 0.0f;
    PwSource pw2Source = PwSource::lfo;
    float dco2Level = 1.0f, am = 0.0f, noise = 0.0f;
    int xmodShape12 = 0, xmodShape21 = 0, fmShape = 0;
    int vcaMode = 0; // 0 gate (velocity), 1 envelope 1, 2 envelope 2
    float released = 1.0f, vel2 = 0.0f, keyN = 0.0f, keyC = 0.5f, toggleVal = 0.5f, random = 0.0f;
    float xExpr = 0.0f, yExpr = 0.0f, X = 0.0f, Y = 0.0f, Xp = 0.0f, Yp = 0.0f;
    std::array<float, 8> mxAmount {};
};

// filter group of 4 voices (§3.3)
struct Ladder
{
    std::array<float, 4> cut {}, res {}, in {}, out {}, G {}, g {}, g4 {}, k {}, onePlusRes {}, norm {};
    std::array<float, 4> s1 {}, s2 {}, s3 {}, s4 {};
    float inGain = 0.3642987310886383f, outGain = 2.744999885559082f, piOverFs = 0.0f, maxCut = 0.0f;
    int slope = 3;
};

// tanh-like Padé saturator clamped to ±5 (§3.3)
inline float sat (float u) noexcept
{
    u = std::min (std::max (u, -5.0f), 5.0f);
    const auto q = u * u;
    const auto num = (((q + 378.0f) * q + 17325.0f) * q + 135135.0f) * u;
    const auto den = ((q * 28.0f + 3150.0f) * q + 62370.0f) * q + 135135.0f;
    return num / den;
}

// ---------------------------------------------------------------------------------------------- the synth engine (§3)
class Synth
{
public:
    Synth() = default;

    // the plugin's life up to the first block: constructor (at 44.1 kHz), state load, host sample rate (§1.3)
    void init (const Patch& patch, std::shared_ptr<const Tables> t, double sampleRate)
    {
        if (patch.exo)
        {
            initExo (patch, std::move (t), sampleRate);
            return;
        }
        tabs = std::move (t);
        construct (44100.0);
        for (int i = 0; i < paramCount; ++i) // the constructor's defaults pass: no modulation routes yet
            setParam (i, defaultParam (i), true);
        routes = true;
        reset();
        for (int i = 0; i < paramCount; ++i)
            setParam (i, defaultParam (i), false);
        arpAligned = patch.arpRestartAligned;
        for (const auto& [index, value] : patch.order)
            if (index >= 0)
                setParam (index, value, false);
            else
                P[0x30] = base[0x30] = value; // converted LFO1 rate: stored only, applied at the next rate change
        setRate (sampleRate, false);
    }

    explicit Synth (std::shared_ptr<const Tables> t) : tabs (std::move (t)) {}

    // a parameter moved while the engine plays (a knob of the designed page, an automation clip; round 168q): Kepler's
    // panel position `k` goes through its setters as the loader would apply it, Kepler Exo's internal index through the
    // forwarder (the matrix still drives its destinations). Outside the render, in the setters' rounding mode.
    void hostParam (int k, float x) noexcept
    {
        if (! std::isfinite (x))
            return;
        NearestMode m;
        if (exo)
        {
            if (k >= 0 && k < 0x9b)
            {
                host[(std::size_t) k] = x;
                forwardExo (k, x);
            }
        }
        else if (k >= 0 && k < 36)
            setParam (panelToInternal (k), x, false);
    }

    // ---------------------------------------------------------------- host side (FL wrapper, §5)
    void setTempo (double bpm) noexcept { tempo = bpm; }
    void setTransport (bool isPlaying, double beats) noexcept
    {
        playing = isPlaying;
        if (isPlaying)
            beatPos = beats;
    }

    // FL's channel volume and pan as MIDI events (wrapper ProcessEvent 3 / 2 → engine setters, REA_rodada165g-flbeta165k
    // §3): volume = MIDI value / 127, pan = MIDI pan / 128 + 0.5; output gains √(1 − pan)·k·(a·v³ − b·v² + c·v) and
    // √pan·k·(…), which the render glides to by 0.05 per sample. `snap`: start at the targets (a fresh instance that
    // FL's export warm-up has already run).
    void setMidiLevels (float volume, float pan, bool snap) noexcept
    {
        midiVolume = volume;
        midiPan = pan;
        auto q = 0.0f;
        if (! (midiPan < 0.0f))
            q = std::min (midiPan, 1.0f);
        const auto v = midiVolume;
        const auto a = v * 6.210264205932617f;
        auto b = v * 1.4609940052032471f;
        auto c = v * v;
        b = b * v;
        const auto d = v * 1.5597299337387085f;
        c = c * a;
        c = c - b;
        const auto sl = std::sqrt (1.0f - q) * 1.4144271612167358f;
        c = c + d;
        outTargetL = sl * c;
        outTargetR = (std::sqrt (q) * 1.4144271612167358f) * c;
        if (snap)
            outGL = outTargetL, outGR = outTargetR;
    }

    void noteOn (int key, int velocity) noexcept // velocity 0…127
    {
        if (! heldAdd (key, velocity))
            return;
        bool first = false;
        if ((int) held.size() == 1)
        {
            if (! holdOn || holdFirst)
            {
                if (! lfo1TrigMode)
                {
                    lfo1Ramp = 0.0f;
                    lfo1SyncReset = true;
                    if (P[0x34] >= 0.5f)
                    {
                        lfo1Phase = (double) std::min (std::max (P[0x35], 0.0f), 0.9999989867210388f);
                        lfo1Wrapped = true;
                    }
                }
                if (exo && ! lfo2TrigMode)
                {
                    lfo2Ramp = 0.0f;
                    lfo2SyncReset = true;
                    if (P[0x3b] >= 0.5f)
                    {
                        lfo2Phase = (double) std::min (std::max (P[0x3c], 0.0f), 0.9999989867210388f);
                        lfo2Wrapped = true;
                    }
                }
                holdFirst = false;
                first = true;
                if (holdOn && arpOn)
                    arp.clear();
            }
            else if (arpOn)
                arp.clear();
        }
        arpNoteOn (key, velocity, first, arpAligned && playing && arpSync);
    }

    void noteOff (int key, int velocity) noexcept
    {
        heldRemove (key);
        if (! holdOn)
            arpNoteOff (key, velocity);
    }

    void setPanExpression (int key, float pan) noexcept
    {
        for (auto& v : voices)
            if (v.note == key)
            {
                auto q = (pan + 1.0f) * 0.5f;
                q = q >= 0.0f ? std::min (q, 1.0f) : 0.0f;
                v.panL = std::sqrt (1.0f - q) * 1.4144271612167358f;
                v.panR = std::sqrt (q) * 1.4144271612167358f;
                return;
            }
    }
    void setPitchExpression (int key, float cents) noexcept
    {
        for (auto& v : voices)
            if (v.note == key)
            {
                v.pitchExpr = cents * 0.0008333333535119891f;
                return;
            }
    }

    // ---------------------------------------------------------------- render (§3.4)
    void render (float* left, float* right, int n) noexcept
    {
        RenderMode mode;
        const auto dt = 1.0 / (double) fsF;
        const auto beatsPerSecond = tempo * 0.016666666666666666;
        if (exo)
            fxDelay.setTempo (tempo);
        if (exo && exoResetPending) // the queued engine reset: the reverb's lines to 1e-30 after the rate set-up (REA_rodada167d §5.3)
        {
            exoResetPending = false;
            fxReverb.reset();
        }
        if (exo && slotsDirty)
        {
            slotsDirty = false;
            compileMatrices();
        }
        for (int i = 0; i < n; ++i)
        {
            const auto t = arpSync ? beatPos : seconds;
            arpClock (t);
            float l = 0.0f, r = 0.0f;
            if (exo)
                sampleExo (l, r, beatPos);
            else
                sample (l, r, beatPos);
            left[i] += l;
            right[i] += r;
            beatPos = beatPos + dt * beatsPerSecond;
            seconds = seconds + dt;
        }
    }

    // no voice sounds: gates closed and every amplitude below −600 dB (the followers stall at tiny values with flushed
    // denormals, as in the engine)
    bool silent() const noexcept
    {
        for (const auto& v : voices)
            if (v.gate != 0.0f || std::fabs (v.amp) > 1e-30f)
                return false;
        return held.empty() && arpNote < 0;
    }

private:
    // ---------------------------------------------------------------- parameters (§4)
    void setRate (double sr, bool construct)
    {
        rate = sr;
        fsF = (float) sr;
        invFs = (float) (1.0 / sr);
        mainK = (float) (1.0 - powD (0.09999999999999998, 400.0 / sr));
        chorusS = (float) (1.0 - powD (0.09999999999999998, 20.0 / sr));
        chorusFadeStep = (float) (1.0 / sr * 200.0);
        // chorus filters at this rate (§3.7)
        chorus.piOverFs = (float) (1.0 / sr) * 3.1415927410125732f;
        chorus.fs = fsF;
        chorus.s = chorusS;
        designChorusHp (50.0f);
        {
            const auto t = tanF (chorus.piOverFs * 7000.0f);
            const auto b = 1.0f / (t + 1.0f), c = b * t;
            chorus.lpB0 = c, chorus.lpB1 = c, chorus.lpA1 = -((t - 1.0f) * b);
        }
        designChorusBiquad();
        // high-pass: π/fs (§3.6)
        hpPiOverFs = (float) (1.0 / sr) * 3.1415927410125732f;
        // voices and filter groups at os·fs
        const auto osFs = (float) oversampling * fsF;
        const auto d = (double) osFs;
        const auto inv = (float) (1.0 / d);
        for (auto& g : groups)
        {
            g.maxCut = (float) (d * 0.4995);
            g.piOverFs = inv * 3.1415927410125732f;
        }
        for (auto& v : voices)
        {
            v.rate = (double) fsF;
            v.fs = fsF;
            v.matrixInterval = (int) (sr * 0.001);
            v.incScale = 1.0 / ((double) oversampling * (double) fsF);
            v.vcaK = (float) (1.0 - powD (0.09999999999999998, 400.0 / (double) fsF));
            v.panK = v.ampK = (float) (1.0 - powD (0.09999999999999998, 200.0 / (double) fsF));
            v.osc.leakMax = (float) powD (0.37, 10.0 / (double) fsF);
        }
        if (exo)
        {
            for (auto& v : voices)
                v.osc2.leakMax = v.osc.leakMax;
            gmInterval = (int) (sr * 0.001);
            satK = (float) (1.0 - powD (0.09999999999999998, 20.0 / sr));
            fxEq.setRate (sr);
            fxDelay.setRate (sr);
            fxHyper.setRate (sr);
            fxReverb.setRate (sr);
            // every host value again through the parameter forwarder (REA_rodada167d §1.3)
            for (int i = 0; i < 0x102; ++i)
                forwardExo (i, host[(std::size_t) i]);
            return;
        }
        if (! construct) // every parameter again from the stored values (routed ones through the matrix only)
            for (int i = 0; i < paramCount; ++i)
                setParam (i, base[(std::size_t) i], false);
    }

    void reset() noexcept
    {
        for (auto& v : voices)
        {
            if (v.gate != 0.0f)
            {
                v.gate = 0.0f;
                v.velGain = 0.0f;
            }
            v.st1 = v.st2 = 2;
            v.env1 = v.env2 = 0.0f;
        }
    }

    void construct (double sr)
    {
        for (int i = 0; i < 8; ++i)
            voices[(std::size_t) i].noiseIndex = (int) ((double) i * 85280.625); // spread over the table (§3.2)
        setRate (sr, true);
    }

    void setParam (int i, float x, bool directRoutes) noexcept
    {
        base[(std::size_t) i] = x;
        if (routes && (i == 0x25 || i == 0x26) && ! directRoutes)
            return; // modulation-route destinations reach the voices through the matrix (§4.3)
        setGlobal (i, x);
        for (auto& v : voices)
            setVoice (v, i, x);
    }

    void setGlobal (int i, float x) noexcept
    {
        const auto old = P[(std::size_t) i];
        P[(std::size_t) i] = x;
        switch (i)
        {
            case 0x24: // HP Freq
            {
                const auto f = powF (x, 1.6f) * 2680.0f + 20.0f;
                const auto t = tanF (f * hpPiOverFs);
                const auto b = 1.0f / (t + 1.0f);
                hpB0 = b, hpB1 = -b, hpA1 = (1.0f - t) * b;
                break;
            }
            case 0x30:
            case 0x31:
            {
                lfo1Sync = P[0x31] >= 0.5f;
                const auto rateHz = powF (2.0f, -table (tLfoRate, P[0x30])) * 21.7391300201416f;
                lfo1Inc = (double) (rateHz * invFs);
                const auto e = powF (2.0f, (1.0f - P[0x30]) * 7.0f);
                lfo1Kappa = 192.0 / (double) (int) ((e - 1.0f) * 48.370079040527344f + 1.100000023841858f);
                break;
            }
            case 0x32:
            {
                const auto d = table (tLfoDelay, x);
                lfo1RampInc = d > 0.0f ? 1.0f / (d * fsF) : 1.0f;
                break;
            }
            case 0x33:
                lfo1TrigMode = x >= 0.5f;
                lfo1Gate = (! lfo1TrigMode || P[0x51] >= 0.5f) ? 1.0f : 0.0f;
                break;
            case 0x45:
            {
                const auto m = (int) (x * 3.0f + 0.5f);
                if (m != chorusTarget)
                {
                    chorusTarget = m;
                    chorusFade = 2;
                }
                break;
            }
            case 0x48:
            case 0x4a:
            {
                auto p = P[0x4a];
                p = p >= 0.0f ? std::min (p, 1.0f) : 0.0f;
                const auto v = P[0x48];
                const auto m = ((0.32754313945770264f - v * 1.3922914266586304f) * v - 0.3496789336204529f) * v;
                mainL = std::sqrt (1.0f - p) * m;
                mainR = std::sqrt (p) * m;
                break;
            }
            case 0x51:
                if (old < 0.5f && x >= 0.5f && lfo1TrigMode)
                {
                    lfo1Ramp = 0.0f;
                    lfo1SyncReset = true;
                    if (P[0x34] >= 0.5f)
                    {
                        lfo1Phase = (double) std::min (std::max (P[0x35], 0.0f), 0.9999989867210388f);
                        lfo1Wrapped = true;
                    }
                }
                lfo1Gate = (! lfo1TrigMode || P[0x51] >= 0.5f) ? 1.0f : 0.0f;
                break;
            case 0x52:
            {
                const auto on = x >= 0.5f;
                if (on != holdOn)
                {
                    holdOn = on;
                    if (on)
                        holdFirst = held.empty();
                    else if (! arpOn)
                    {
                        for (auto& v : voices)
                            if (v.gate != 0.0f && ! isHeld (v.note))
                                releaseVoice (v, 0.0f);
                    }
                    else
                    {
                        std::vector<int> drop;
                        for (const auto& e : arp.lists[0])
                            if (! isHeld (e.first))
                                drop.push_back (e.first);
                        for (auto k : drop)
                            arpNoteOff (k, 0);
                    }
                }
                break;
            }
            case 0x55:
            {
                const auto on = x >= 0.5f;
                if (on != arpOn)
                {
                    arpOn = on;
                    if (on)
                    {
                        if (! arp.lists[0].empty())
                            arpRunning = true;
                        for (const auto& h : held)
                            arpNoteOnList (h.first, h.second, true, arpAligned && arpSync && playing);
                        for (auto& v : voices)
                            if (v.gate != 0.0f)
                                releaseVoice (v, 0.0f);
                    }
                    else
                        arpRunning = false;
                }
                break;
            }
            case 0x56:
                arpRate();
                break;
            case 0x57:
            {
                const auto on = x >= 0.5f;
                if (on != arpSync)
                {
                    arpStop();
                    arpSync = on;
                    arpRate();
                }
                break;
            }
            case 0x58:
                arp.range = 1 << ((int) (x + x + 0.5f) & 31);
                break;
            case 0x59:
                arp.mode = x >= 0.3330000042915344f ? (x >= 0.6660000085830688f ? 2 : 1) : 0;
                break;
            case 0x5a:
                arpGate = (double) x;
                arpGateLen = (double) x * arpPeriod;
                break;
            case 0x9a:
                hpOn = x >= 0.5f;
                break;
            default:
                break;
        }
    }

    void arpRate() noexcept
    {
        if (! arpSync)
            arpPeriod = (double) table (tArpRate, P[0x56]);
        else
            arpPeriod = (double) tArpSync[std::min (12, std::max (0, (int) (P[0x56] * 12.0f + 0.5f)))] * 0.010416666666666666;
        arpGateLen = arpPeriod * arpGate;
    }

    void setVoice (Voice& v, int i, float x) noexcept
    {
        v.p[(std::size_t) i] = x;
        const auto& q = v.p;
        switch (i)
        {
            case 0x00:
            case 0x01:
                v.lfo1Dco = table (tDcoLfo, q[0x00]);
                if (q[0x01] >= 0.5f)
                    v.lfo1Dco = v.lfo1Dco * 10.0f;
                break;
            case 0x02:
            case 0x03:
                v.lfo2Dco = table (tDcoLfo, q[0x02]);
                if (q[0x03] >= 0.5f)
                    v.lfo2Dco = v.lfo2Dco * 10.0f;
                break;
            case 0x04:
                v.env2Dco = x * 10.0f;
                break;
            case 0x05:
                v.pwAmount = x;
                break;
            case 0x06:
                v.pwSource = x < 0.3330000042915344f ? PwSource::lfo : (x >= 0.6660000085830688f ? PwSource::envelope : PwSource::manual);
                break;
            case 0x07:
                v.osc.pulse = x < 0.5f ? 0.0f : 1.0f;
                break;
            case 0x08:
                v.osc.saw = x < 0.5f ? 0.0f : 1.0f;
                break;
            case 0x09:
            case 0x0a:
                v.osc.sub = q[0x09] < 0.5f ? 0.0f : table (tSubVol, q[0x0a]) * 1.1111111640930176f;
                break;
            case 0x0b:
                v.osc.subPoint = x * 0.9900000095367432f;
                break;
            case 0x20:
                v.dco1Level = x;
                break;
            case 0x22:
            {
                const auto t = table (tSubVol, x);
                v.noiseVol = t <= 0.009999999776482582f ? 0.009999999776482582f : t; // never below 0.01 (§4.1)
                break;
            }
            case 0x25:
            {
                auto c = q[0x25];
                c = c >= 0.0f ? std::min (c, 1.0f) : 0.0f;
                v.cutoffHz = powF (2.0f, table (tCutoffLog2, c));
                break;
            }
            case 0x26:
            {
                auto c = q[0x26];
                v.resonance = c >= 0.0f ? std::min (c, 1.0f) : 0.0f;
                break;
            }
            case 0x28:
                v.slope = (int) (x * 3.0f + 0.5f);
                break;
            case 0x29:
                v.filterFm = x * x * x * 16.0f;
                break;
            case 0x2b:
                v.polarity = x < 0.5f ? 1.0f : -1.0f;
                break;
            case 0x2c:
                v.envCutoff = table (tEnvCutoff, x);
                break;
            case 0x2d:
                v.lfo1Cutoff = table (tLfoCutoff, x);
                break;
            case 0x2e:
                v.lfo2Cutoff = table (tLfoCutoff, x);
                break;
            case 0x2f:
                v.envVca = x < 0.5f;
                break;
            case 0x3d: // attack: 1 − 0.4118^(1/(T·fs)) (§4.2)
            {
                const auto t = table (tAttack, x);
                double k = 1.0;
                if ((double) t != 0.0)
                    k = 1.0 - powD (0.4117647223819081, 1.0 / ((double) v.fs * (double) t));
                v.atkK1 = (float) k;
                break;
            }
            case 0x3e:
                v.dcyK1 = powF (0.10000000149011612f, 1.0f / (table (tDecay, x) * v.fs));
                break;
            case 0x3f:
                v.sus1 = x, v.oneMinusSus1 = 1.0f - x;
                break;
            case 0x40:
                v.relK1 = powF (0.10000000149011612f, 1.0f / (table (tRelease, x) * v.fs));
                break;
            case 0x41:
            {
                const auto t = table (tAttack, x);
                double k = 1.0;
                if ((double) t != 0.0)
                    k = 1.0 - powD (0.4117647223819081, 1.0 / ((double) v.fs * (double) t));
                v.atkK2 = (float) k;
                break;
            }
            case 0x42:
                v.dcyK2 = powF (0.10000000149011612f, 1.0f / (table (tDecay, x) * v.fs));
                break;
            case 0x43:
                v.sus2 = x, v.oneMinusSus2 = 1.0f - x;
                break;
            case 0x44:
                v.relK2 = powF (0.10000000149011612f, 1.0f / (table (tRelease, x) * v.fs));
                break;
            case 0x4f:
                v.glideLinear = x >= 0.5f;
                break;
            case 0x50:
            {
                const auto d = (double) x * (double) x * (double) x * 10.0;
                if (d == 0.0)
                {
                    v.glideK = 1.0f;
                    v.glideStep = 1.0f;
                }
                else
                {
                    v.glideK = (float) (1.0 - powD (0.09999999999999998, 1.0 / (d * v.rate)));
                    v.glideStep = d > 0.0 ? (float) (1.0 / (d * v.rate)) : 1.0f;
                }
                break;
            }
            case 0x53:
                v.octave = (float) (int) (q[0x53] + q[0x53] + 0.5f) - 1.0f;
                break;
            case 0x54:
                v.bendRange = (float) ((int) (x * 23.0f + 0.5f) + 1) * 0.1666666716337204f;
                break;
            case 0x97:
                v.vcaSmooth = x >= 0.5f;
                break;
            case 0x100:
                v.dcoPitch = (x - 0.5f) * 24.0f;
                break;
            default:
                break;
        }
    }

    // ---------------------------------------------------------------- notes (§5.1)
    bool isHeld (int key) const noexcept
    {
        return std::any_of (held.begin(), held.end(), [key] (const auto& h) { return h.first == key; });
    }
    bool heldAdd (int key, int vel) noexcept
    {
        if (isHeld (key))
            return false;
        held.emplace_back (key, vel);
        return true;
    }
    void heldRemove (int key) noexcept
    {
        held.erase (std::remove_if (held.begin(), held.end(), [key] (const auto& h) { return h.first == key; }), held.end());
    }

    struct Arp
    {
        // played order, ascending and descending lists (§5.3)
        std::array<std::vector<std::pair<int, int>>, 4> lists;
        int mode = 0, range = 1, dir = 1, index = -1, octave = 0, note = 0, vel = 0;
        std::uint32_t lcg = 1793462892u; // random modes (Kepler Exo)
        void clear() noexcept
        {
            for (auto& l : lists)
                l.clear();
        }
        void add (int key, int vel) noexcept
        {
            for (const auto& e : lists[2])
                if (e.first == key)
                    return;
            lists[0].emplace_back (key, vel);
            lists[1].insert (lists[1].begin(), { key, vel });
            {
                auto& l = lists[2];
                int at = 0;
                for (int k = (int) l.size() - 1; k >= 0; --k)
                    if (l[(std::size_t) k].first <= key)
                    {
                        at = k + 1;
                        break;
                    }
                l.insert (l.begin() + at, { key, vel });
            }
            {
                auto& l = lists[3];
                auto at = (int) l.size();
                for (int k = 0; k < (int) l.size(); ++k)
                    if (l[(std::size_t) k].first <= key)
                    {
                        at = k;
                        break;
                    }
                l.insert (l.begin() + at, { key, vel });
            }
        }
        void remove (int key) noexcept
        {
            for (auto& l : lists)
                for (auto it = l.begin(); it != l.end(); ++it)
                    if (it->first == key)
                    {
                        l.erase (it);
                        break;
                    }
        }
    };

    void arpNoteOn (int key, int vel, bool first, bool aligned) noexcept
    {
        if (! arpOn)
        {
            voiceNoteOn (key, vel);
            return;
        }
        arpNoteOnList (key, vel, first, aligned);
    }

    void arpNoteOnList (int key, int vel, bool first, bool aligned) noexcept
    {
        for (const auto& e : arp.lists[2])
            if (e.first == key)
                return;
        arp.add (key, vel);
        if (! first)
        {
            if (! arpJunoIndex)
                return;
            // a note added while the arpeggio runs keeps its place relative to the current step
            const auto& l = arp.dir == 2 ? arp.lists[3] : arp.lists[2];
            int at = -1;
            for (int k = 0; k < (int) l.size(); ++k)
                if (l[(std::size_t) k].first == key)
                {
                    at = k;
                    break;
                }
            if (at <= arp.index)
                ++arp.index;
            return;
        }
        arpRestart = aligned ? 2 : 1;
        arpWaiting = true;
        arpRunning = ! arp.lists[0].empty();
        arp.index = -1;
        if (arp.mode == 2 || arp.mode == 8)
        {
            arp.octave = arp.range - 1;
            arp.dir = 2;
        }
        else
        {
            arp.octave = 0;
            arp.dir = 1;
        }
    }

    void arpNoteOff (int key, int vel) noexcept
    {
        const auto before = (int) arp.lists[0].size();
        arp.remove (key);
        if (! arpOn)
            voiceNoteOff (key, vel);
        if (before == 1 && arp.lists[0].empty())
        {
            if (arpNote >= 0)
            {
                voiceNoteOff (arpNote, 0);
                arpNote = -1;
            }
            arpRunning = false;
        }
    }

    void arpStop() noexcept
    {
        if (arpNote >= 0)
        {
            voiceNoteOff (arpNote, 0);
            arpNote = -1;
        }
        arp.clear();
        arpWaiting = true;
        arpRunning = false;
        arp.index = -1;
        if (arp.mode == 2 || arp.mode == 8)
        {
            arp.octave = arp.range - 1;
            arp.dir = 2;
        }
        else
        {
            arp.octave = 0;
            arp.dir = 1;
        }
        arpRestart = 0;
    }

    void arpStep() noexcept
    {
        // modes 0 (up), 1 (up/down), 2 (down) of the Kepler-1 panel (§5.3); Kepler Exo adds 3–5 (random: any note, no
        // immediate repeat of note and octave, no repeat of the note) and 6–8 (as 0–2 in the played order,
        // REA_rodada167d §5.3)
        if (arp.mode >= 3 && arp.mode <= 5)
        {
            const auto& l = arp.lists[2];
            const auto n = (int) l.size();
            if (n == 0)
                return;
            const auto prevNote = arp.note, prevOct = arp.octave;
            for (;;)
            {
                arp.lcg = arp.lcg * 0x0BB38435u + 0x3619636Bu;
                const auto k = (int) ((double) (float) ((double) arp.lcg * 2.3283064365386963e-10) * (double) n * 0.9999);
                arp.note = l[(std::size_t) k].first;
                arp.vel = l[(std::size_t) k].second;
                arp.lcg = arp.lcg * 0x0BB38435u + 0x3619636Bu;
                arp.octave = (int) ((double) (float) ((double) arp.lcg * 2.3283064365386963e-10) * (double) arp.range * 0.9999);
                if (arp.mode == 4)
                {
                    if (arp.range * n > 1 && arp.note == prevNote && arp.octave == prevOct)
                        continue;
                    break;
                }
                if (arp.mode == 5 && n >= 2 && arp.note == prevNote)
                    continue;
                break;
            }
            arpNote = std::min (127, std::max (0, arp.note + arp.octave * 12));
            voiceNoteOn (arpNote, arp.vel);
            return;
        }
        const bool played = arp.mode >= 6;
        const bool upDown = arp.mode == 1 || arp.mode == 7;
        auto& up = arp.lists[played ? 0 : 2];
        auto& down = arp.lists[played ? 1 : 3];
        ++arp.index;
        const std::vector<std::pair<int, int>>* list = &up;
        if (arp.dir == 2)
        {
            if (arp.index >= (int) down.size())
            {
                --arp.octave;
                arp.index = 0;
                if (arp.octave < 0)
                {
                    if (upDown)
                    {
                        arp.dir = 1;
                        int o = 0;
                        if ((int) up.size() < 2)
                            o = std::min (arp.range - 1, 1);
                        else
                            arp.index = 1;
                        arp.octave = o;
                    }
                    else
                        arp.octave = arp.range - 1;
                }
            }
            list = arp.dir == 2 ? &down : &up;
        }
        else
        {
            if (arp.index >= (int) up.size())
            {
                ++arp.octave;
                arp.index = 0;
                if (arp.octave >= arp.range)
                {
                    if (upDown)
                    {
                        arp.dir = 2;
                        int o;
                        if ((int) down.size() < 2)
                        {
                            o = arp.range - 2;
                            if (o < 0)
                                o = 0;
                        }
                        else
                        {
                            arp.index = 1;
                            o = arp.range - 1;
                        }
                        arp.octave = o;
                        list = &down;
                    }
                    else
                        arp.octave = 0;
                }
            }
        }
        if (list->empty())
            return;
        const auto& e = (*list)[(std::size_t) std::min (std::max (arp.index, 0), (int) list->size() - 1)];
        arp.note = e.first;
        arp.vel = e.second;
        arpNote = std::min (127, std::max (0, e.first + arp.octave * 12));
        voiceNoteOn (arpNote, arp.vel);
    }

    void arpClock (double t) noexcept
    {
        if (! arpRunning)
            return;
        if (arpLast < t)
            arpDelta = t - arpLast;
        arpLast = t;
        arpAcc = arpAcc + arpDelta;
        for (;;)
        {
            while (arpWaiting)
            {
                if (arpRestart != 0)
                {
                    arpLast = t, arpAcc = t;
                    if (arpRestart == 2)
                    {
                        const auto q = (t - 0.001) / arpPeriod;
                        arpNext = (double) (int) std::ceil (q) * arpPeriod; // next grid point (§5.3)
                    }
                    else
                        arpNext = t;
                    arpStart = arpNext;
                    arpRestart = 0;
                }
                if (arpAcc < arpNext)
                    return;
                arpStep();
                arpWaiting = false;
            }
            if (arpAcc < arpNext + arpGateLen)
                return;
            arpNext = arpNext + arpPeriod;
            if (arpNote >= 0)
            {
                voiceNoteOff (arpNote, 0);
                arpNote = -1;
            }
            arpWaiting = true;
        }
    }

    int allocate (int key, bool& reused) noexcept
    {
        reused = false;
        for (int i = 0; i < 8; ++i)
            if (voices[(std::size_t) i].note == key)
            {
                reused = true;
                return i;
            }
        int best = -1, bestAge = 0;
        for (int i = 0; i < 8; ++i)
        {
            const auto& v = voices[(std::size_t) i];
            if (v.gate == 0.0f && (best < 0 || bestAge < v.age))
                best = i, bestAge = v.age;
        }
        if (best >= 0)
            return best;
        best = 0, bestAge = voices[0].age;
        for (int i = 1; i < 8; ++i)
            if (bestAge < voices[(std::size_t) i].age)
                best = i, bestAge = voices[(std::size_t) i].age;
        return best;
    }

    void voiceNoteOn (int key, int vel) noexcept
    {
        const auto prev = order.empty() ? -1 : order.back();
        order.erase (std::remove (order.begin(), order.end(), key), order.end());
        order.push_back (key);
        if (exo && P[0x4b] >= 0.5f)
        {
            // monophonic (REA_rodada167d §5.1): voice 0; glide from where it is unless legato's first note; the
            // envelopes restart on the first note or with "always trigger"
            const auto count = (int) order.size();
            const auto from = (count == 1 && P[0x4c] >= 0.5f) ? key : -1;
            const bool retrigger = P[0x4d] >= 0.5f || count == 1;
            startVoice (voices[0], from, key, (float) vel * 0.007874015718698502f, retrigger, count == 1);
            lastNote = key;
            updateMatrixVoice();
            return;
        }
        bool reused = false;
        const auto i = allocate (key, reused);
        auto& v = voices[(std::size_t) i];
        // glide source: the previous note (legato) or the last note played (§5.1)
        const auto from = P[0x4c] < 0.5f ? lastNote : (prev >= 0 ? prev : key);
        startVoice (v, from, key, (float) vel * 0.007874015718698502f, true, ! reused);
        lastNote = key;
        if (exo)
            updateMatrixVoice();
    }

    void startVoice (Voice& v, int from, int key, float vel, bool retrigger, bool fresh) noexcept
    {
        const auto toggle = lfoToggle;
        lfoToggle = -lfoToggle;
        v.toggleVal = toggle;
        lcg = lcg * 0x0BB38435u + 0x3619636Bu;
        v.random = (float) (std::int32_t) lcg * 2.3283064365386963e-10f;
        v.gate = 1.0f;
        v.released = 0.0f;
        v.fresh = fresh;
        if (from >= 0)
            v.glide = (float) (from - 69) * 0.0833333358168602f;
        v.glideTarget = (float) (key - 69) * 0.0833333358168602f;
        if (vel >= 0.0f)
        {
            const auto a = vel * 0.9843500256538391f, b2 = vel * 0.23157300055027008f, c = vel * 0.24722300469875336f;
            v.velocity = vel;
            const auto vv = P[0x49];
            const auto g = ((vel * vel * a - b2 * vel) + c) * vv + (1.0f - vv);
            v.velGain = g;
            v.velGainEnv = g * 1.0256410837173462f;
        }
        v.note = key;
        v.age = 0;
        v.vel2 = vel;
        v.keyN = (float) key * 0.007874015718698502f;
        v.keyC = v.keyN - 0.5f;
        if (retrigger)
        {
            v.st1 = 0, v.dcy1 = 1.0f;
            v.st2 = 0, v.dcy2 = 1.0f;
        }
        v.matrixCount = v.matrixInterval;
        if (exo)
            voiceMatrixExo (v);
        else
            matrix (v);
    }

    void voiceNoteOff (int key, int vel) noexcept
    {
        order.erase (std::remove (order.begin(), order.end(), key), order.end());
        if (exo && P[0x4b] >= 0.5f)
        {
            // monophonic: back to the newest held note (envelopes restart with "restart on note off"), else release
            if (order.empty())
            {
                if (voices[0].gate != 0.0f)
                    releaseVoice (voices[0], (float) vel * 0.007874015718698502f);
            }
            else
                startVoice (voices[0], -1, order.back(), -0.007874015718698502f, host[0x4e] >= 0.5f, false);
            updateMatrixVoice();
            return;
        }
        if (exo)
        {
            for (auto& v : voices)
                if (v.gate != 0.0f && v.note == key)
                    releaseVoice (v, (float) vel * 0.007874015718698502f);
            updateMatrixVoice();
            return;
        }
        for (auto& v : voices)
            if (v.gate != 0.0f && v.note == key)
                releaseVoice (v, 0.0f);
    }

    void releaseVoice (Voice& v, float vel) noexcept
    {
        v.gate = 0.0f;
        v.released = 1.0f;
        v.vel2 = vel;
        v.velGain = 0.0f;
        v.st1 = v.st2 = 2;
    }

    // per-voice modulation routes X → cutoff, Y → resonance (amount 2·M − 1), clamped to 0…1 (§4.3)
    void matrix (Voice& v) noexcept
    {
        auto c = v.x * ((P[0x5c] + P[0x5c]) - 1.0f) + base[0x25];
        c = c <= 0.0f ? 0.0f : std::min (c, 1.0f);
        setVoice (v, 0x25, c);
        auto r = v.y * ((P[0x5d] + P[0x5d]) - 1.0f) + base[0x26];
        r = r <= 0.0f ? 0.0f : std::min (r, 1.0f);
        setVoice (v, 0x26, r);
    }

    // ---------------------------------------------------------------- one host sample (§3.4)
    void sample (float& outL, float& outR, double beats) noexcept
    {
        const auto& T = *tabs;
        // LFO1 (§3.5)
        if (lfo1SyncReset)
        {
            lfo1LastBeat = beats;
            lfo1SyncReset = false;
        }
        {
            const auto ph = (float) lfo1Phase;
            float tri;
            if (ph < 0.25f)
                tri = ph + ph;
            else if (ph < 0.75f)
                tri = 0.5f - (((ph - 0.25f) + ph) - 0.25f);
            else
                tri = ((ph - 0.75f) + ph) - 1.25f;
            auto x = ph * 2170.0f;
            const auto k = (int) x;
            x = x - (float) k;
            const auto a = (1.0f - x) * T.lfo[(std::size_t) k];
            const auto b = x * T.lfo[(std::size_t) k + 1];
            lfo1Shape = (a + b) * 0.5f;
            lfo1Tri = tri;
            if (lfo1Sync)
            {
                const auto d = (beats - lfo1LastBeat) * lfo1Kappa;
                lfo1LastBeat = beats;
                if (0.0 <= d && d < 1.0)
                    lfo1Inc = d;
            }
            auto p = lfo1Phase + lfo1Inc;
            lfo1Wrapped = p >= 1.0;
            if (lfo1Wrapped)
                p = p - 1.0;
            lfo1Phase = p;
            lfo1Uni = tri + 0.5f;
        }
        const auto rg = lfo1Ramp * lfo1Gate;
        lfo1Ramp = std::min (lfo1Ramp + lfo1RampInc, 1.0f);
        lfo1Out = rg * lfo1Shape;

        for (auto& v : voices)
            control (v);

        // filter coefficients (§3.3), once per host sample
        for (int g = 0; g < 2; ++g)
        {
            auto& G = groups[(std::size_t) g];
            for (int l = 0; l < 4; ++l)
            {
                const auto& v = voices[(std::size_t) (g * 4 + l)];
                G.cut[(std::size_t) l] = v.laneCut;
                G.res[(std::size_t) l] = v.laneRes;
                G.slope = voices[(std::size_t) (g * 4)].slope;
                const auto r = G.res[(std::size_t) l];
                const auto k4 = r * 4.199999809265137f;
                G.onePlusRes[(std::size_t) l] = r + 1.0f;
                G.k[(std::size_t) l] = k4;
                auto c = std::min (G.cut[(std::size_t) l], G.maxCut);
                c = std::max (c, 20.0f);
                const auto w = c * G.piOverFs;
                const auto w2 = w * w;
                const auto t = ((2.4716885089874268f - w2 * 0.18975968658924103f) * w) / (2.4674010276794434f - w2);
                const auto gg = 1.0f / (t + 1.0f);
                G.G[(std::size_t) l] = gg;
                const auto g1 = gg * t;
                G.g[(std::size_t) l] = g1;
                auto g2 = g1 * g1;
                g2 = g2 * g2;
                G.g4[(std::size_t) l] = g2;
                G.norm[(std::size_t) l] = 1.0f / (k4 * g2 + 1.0f);
            }
        }

        std::array<float, 2> osL {}, osR {};
        const auto* rows = T.blit.data();
        for (int os = 0; os < oversampling; ++os)
        {
            for (int vi = 0; vi < 8; ++vi)
            {
                auto& v = voices[(std::size_t) vi];
                v.osc.tick (rows);
                const auto nz = (float) (int) T.noise[(std::size_t) v.noiseIndex] * 8.631591481389478e-05f;
                if (++v.noiseIndex >= noisePoints)
                    v.noiseIndex = 0;
                const auto a = v.osc.out * v.dco1Level;
                const auto b = nz * v.noiseVol;
                groups[(std::size_t) (vi >> 2)].in[(std::size_t) (vi & 3)] = (b + a) * 0.27000001072883606f;
            }
            for (auto& G : groups)
                ladder (G);
            float l = 0.0f, r = 0.0f;
            for (int vi = 0; vi < 8; ++vi)
            {
                auto& v = voices[(std::size_t) vi];
                float a;
                if (! v.envVca)
                    a = v.vca;
                else
                {
                    auto e = v.env1 - 0.02500000037252903f;
                    e = std::max (e, 0.0f);
                    a = e * v.velGainEnv;
                }
                if (v.envVca && v.vcaSmooth)
                    a = (a - v.amp) * v.ampK + v.amp;
                v.amp = a;
                float pl = v.panL, pr = v.panR;
                if (v.fresh)
                    v.fresh = false;
                else
                {
                    pl = (pl - v.panLs) * v.panK + v.panLs;
                    pr = (pr - v.panRs) * v.panK + v.panRs;
                }
                v.panLs = pl, v.panRs = pr;
                const auto y = groups[(std::size_t) (vi >> 2)].out[(std::size_t) (vi & 3)];
                const auto yl = y * pl * a;
                const auto yr = y * pr * a;
                r = r + yr;
                l = l + yl;
            }
            osL[(std::size_t) os] = l;
            osR[(std::size_t) os] = r;
        }
        auto L = 0.0f, R = 0.0f;
        decimate (osL, osR, L, R);

        // high-pass (§3.6)
        if (hpOn)
        {
            const auto yl = (hpB0 * L + hpB1 * hpXL) + hpA1 * hpYL;
            const auto yr = (hpB0 * R + hpB1 * hpXR) + hpA1 * hpYR;
            hpYL = yl, hpYR = yr;
            hpXL = L, hpXR = R;
            L = yl, R = yr;
        }

        // chorus with mode cross-fade (§3.7)
        if (chorusFade == 0)
        {
            if (chorusMode != 0)
                chorusProcess (L, R);
        }
        else
        {
            auto cl = L, cr = R;
            if (chorusMode != 0)
                chorusProcess (cl, cr);
            const auto f = std::max (chorusMix, 0.0f);
            const auto dry = 1.0f - f;
            L = L * dry + f * cl;
            R = dry * R + f * cr;
            if (chorusFade == 1)
            {
                chorusMix = chorusFadeStep + chorusMix;
                if (chorusMix > 1.0f)
                {
                    chorusMix = 1.0f;
                    chorusFade = 0;
                }
            }
            else if (chorusFade == 2)
            {
                chorusMix = chorusMix - chorusFadeStep;
                if (chorusMix < 0.0f)
                {
                    chorusMix = -1.0f;
                    chorusFade = 1;
                    chorusMode = chorusTarget;
                    if (chorusMode != 0)
                        chorusSetMode (chorusMode);
                }
            }
        }

        // main volume / pan (smoothed), then the output gain (§3.8)
        mainSL = (mainL - mainSL) * mainK + mainSL;
        mainSR = (mainR - mainSR) * mainK + mainSR;
        L = mainSL * L;
        R = R * mainSR;
        L = outGL * L;
        R = outGR * R;
        outGL = (outTargetL - outGL) * 0.05000000074505806f + outGL;
        outGR = (outTargetR - outGR) * 0.05000000074505806f + outGR;
        outL = L, outR = R;
    }

    // per-voice control, once per host sample (§3.1)
    void control (Voice& v) noexcept
    {
        switch (v.st1)
        {
            case 0:
            {
                const auto e = (1.7000000476837158f - v.env1) * v.atkK1 + v.env1;
                v.env1 = e;
                if (e >= 1.0f)
                    v.env1 = 1.0f, v.st1 = 1;
                break;
            }
            case 1:
            {
                const auto d = v.dcyK1 * v.dcy1;
                v.dcy1 = d;
                v.env1 = d * v.oneMinusSus1 + v.sus1;
                break;
            }
            case 2:
                v.env1 = v.relK1 * v.env1;
                break;
            default:
                break;
        }
        switch (v.st2)
        {
            case 0:
            {
                const auto e = (1.7000000476837158f - v.env2) * v.atkK2 + v.env2;
                v.env2 = e;
                if (e >= 1.0f)
                    v.env2 = 1.0f, v.st2 = 1;
                break;
            }
            case 1:
            {
                const auto d = v.dcyK2 * v.dcy2;
                v.dcy2 = d;
                v.env2 = d * v.oneMinusSus2 + v.sus2;
                break;
            }
            case 2:
                v.env2 = v.relK2 * v.env2;
                break;
            default:
                break;
        }
        if (++v.matrixCount >= v.matrixInterval)
        {
            v.matrixCount = 0;
            matrix (v);
        }
        // pulse width (§3.2)
        const auto src = v.pwSource == PwSource::lfo ? lfo1Uni : (v.pwSource == PwSource::manual ? 1.0f : v.env1);
        v.osc.pw = 0.4580000042915344f - src * 0.4320000112056732f * v.pwAmount;
        // glide (§3.1)
        float pitch;
        if (! v.glideLinear)
        {
            pitch = (v.glideTarget - v.glide) * v.glideK + v.glide;
            v.glide = pitch;
        }
        else
        {
            pitch = v.glide;
            if (v.glideTarget > pitch)
            {
                pitch = pitch + v.glideStep;
                if (v.glideTarget <= pitch)
                    pitch = v.glideTarget;
            }
            else if (pitch > v.glideTarget)
            {
                pitch = pitch - v.glideStep;
                if (pitch <= v.glideTarget)
                    pitch = v.glideTarget;
            }
            v.glide = pitch;
        }
        const auto l1 = lfo1Out + lfo1Out;
        const auto l2 = 0.0f;
        auto common = 0.0f * v.bendRange;
        const auto envTerm = v.env2 * v.env2Dco;
        common = common + pitch;
        auto s = l1 * v.lfo1Dco;
        common = common + v.octave;
        const auto lfo2Term = l2 * v.lfo2Dco;
        s = s + v.dcoPitch;
        common = common + v.pitchExpr;
        const auto mod = envTerm + lfo2Term;
        common = common + midiPitch;
        s = s + common;
        s = s + mod;
        auto inc = powD (2.0, (double) s * 1.0005) * 440.0 * v.incScale;
        if (inc >= 0.25)
            inc = 0.25;
        inc = std::max (inc, 0.0);
        v.osc.inc = inc;
        {
            const auto lk = 1.0f - (float) inc * 0.2679489850997925f;
            v.osc.leak = lk >= v.osc.leakMax ? v.osc.leakMax : lk;
        }
        // cutoff (§3.3)
        auto e = v.polarity * v.env1;
        const auto c2 = l2 * v.lfo2Cutoff;
        e = e * v.envCutoff;
        const auto c1 = l1 * v.lfo1Cutoff;
        e = e + c1;
        auto kf = (float) (v.note - 24) * 0.0833333358168602f;
        kf = kf * v.p[0x27];
        kf = kf + c2;
        kf = kf + e;
        auto cut = powF (2.0f, kf) * v.cutoffHz;
        cut = cut >= 5.0f ? std::min (cut, 30000.0f) : 5.0f;
        v.laneCut = cut;
        v.laneRes = v.resonance;
        ++v.age;
        v.vca = (v.velGain - v.vca) * v.vcaK + v.vca;
    }

    void ladder (Ladder& G) noexcept
    {
        for (int l = 0; l < 4; ++l)
        {
            const auto Gc = G.G[(std::size_t) l], g = G.g[(std::size_t) l];
            auto& s1 = G.s1[(std::size_t) l];
            auto& s2 = G.s2[(std::size_t) l];
            auto& s3 = G.s3[(std::size_t) l];
            auto& s4 = G.s4[(std::size_t) l];
            const auto S2 = s2 * Gc, S3 = s3 * Gc, S4 = s4 * Gc;
            const auto a = Gc * s1;
            auto fb = g * a;
            fb = fb + S2;
            fb = fb * g;
            fb = fb + S3;
            fb = fb * g;
            fb = fb + S4;
            auto u = G.onePlusRes[(std::size_t) l] * G.in[(std::size_t) l];
            u = u - fb * G.k[(std::size_t) l];
            u = u * G.inGain;
            auto v = sat (u);
            v = v * G.outGain;
            v = v * G.norm[(std::size_t) l];
            // stage 1
            auto y1 = g * v;
            y1 = y1 + a;
            {
                auto t = sat (v - s1) * g;
                s1 = (t + t) + s1;
            }
            auto y2 = g * y1;
            y2 = y2 + S2;
            {
                auto t = sat (y1 - s2) * g;
                s2 = (t + t) + s2;
            }
            auto y3 = g * y2;
            y3 = y3 + S3;
            {
                auto t = sat (y2 - s3) * g;
                s3 = (t + t) + s3;
            }
            auto y4 = g * y3;
            y4 = y4 + S4;
            {
                auto t = sat (y3 - s4) * g;
                s4 = (t + t) + s4;
            }
            G.out[(std::size_t) l] = G.slope == 0 ? y1 : G.slope == 1 ? y2 : G.slope == 2 ? y3 : y4;
        }
    }

    // 2:1 polyphase half-band (6 allpass sections per path), §3.6
    void decimate (const std::array<float, 2>& inL, const std::array<float, 2>& inR, float& outL, float& outR) noexcept
    {
        static constexpr float c[6][4] = {
            { 0.0366815031f, 0.0366815031f, 0.136547625f, 0.136547625f }, { 0.274631768f, 0.274631768f, 0.423138618f, 0.423138618f },
            { 0.561098993f, 0.561098993f, 0.677540064f, 0.677540064f },   { 0.769741833f, 0.769741833f, 0.839889646f, 0.839889646f },
            { 0.89226079f, 0.89226079f, 0.931541979f, 0.931541979f },     { 0.962094545f, 0.962094545f, 0.987816393f, 0.987816393f },
        };
        for (int i = 0; i < 2; ++i)
        {
            const std::array<float, 4> x { inL[(std::size_t) i], inR[(std::size_t) i], inL[(std::size_t) i], inR[(std::size_t) i] };
            std::array<float, 4> prevOut = dec.y[5][1];
            std::array<float, 4> in = x;
            for (int s = 0; s < 6; ++s)
            {
                std::array<float, 4> y {};
                for (int l = 0; l < 4; ++l)
                    y[(std::size_t) l] = (in[(std::size_t) l] - dec.y[(std::size_t) s][1][(std::size_t) l]) * c[s][l] + dec.x[(std::size_t) s][1][(std::size_t) l];
                dec.x[(std::size_t) s][1] = dec.x[(std::size_t) s][0];
                dec.x[(std::size_t) s][0] = in;
                dec.y[(std::size_t) s][1] = dec.y[(std::size_t) s][0];
                dec.y[(std::size_t) s][0] = y;
                in = y;
            }
            (void) prevOut;
            outL = (in[0] + dec.last[2]) * 0.5f;
            outR = (in[1] + dec.last[3]) * 0.5f;
            dec.last = in;
        }
    }

    // ================================================================ Kepler Exo (REA_rodada167d)
    // The Exo engine runs the same render code as Kepler; its parameter forwarder, global / voice setters, defaults and
    // state loader differ. Everything below is used only for "Kepler-2" states (exo == true).

    // the plugin's life as the harness / FL drive it: constructor at 44.1 kHz (rate set-up with every host value 0,
    // then the defaults reset), state load (reset again, the stored values, the matrix slots), host sample rate (§1.3)
    void initExo (const Patch& patch, std::shared_ptr<const Tables> t, double sampleRate)
    {
        exo = true;
        tabs = std::move (t);
        for (auto& sl : slots)
            sl = { -1, -1 };
        for (int i = 0; i < 8; ++i)
            voices[(std::size_t) i].noiseIndex = (int) ((double) i * 85280.625);
        for (auto& v : voices)
            v.osc.slave = nullptr;
        lfo1Gate = lfo2Gate = 0.0f;
        lfo1RampInc = lfo2RampInc = 0.0f;
        holdFirst = true;
        setRate (44100.0, true);
        resetExo();
        // state load
        resetExo();
        for (const auto& [index, value] : patch.order)
        {
            host[(std::size_t) index] = value;
            forwardExo (index, value);
        }
        if (patch.slotsSet)
        {
            slots = patch.slots;
            slotsDirty = true;
        }
        if (patch.version > 2)
            arpAligned = patch.arpRestartAligned;
        if (patch.version < 2)
        {
            // old states with a BPM-synced LFO: the loader rewrites the stored host value only (applied at the next
            // rate set-up), the law of REA_rodada33 §3.4
            auto convert = [this] (int rateIndex) {
                const auto x = host[(std::size_t) rateIndex];
                const auto e = powF (2.0f, x * 7.0f);
                const auto nOld = (int) ((e - 1.0f) * 48.370079f + 1.1f);
                const auto m = (int) (9216.0f / (float) nOld + 0.1f);
                const auto l = lnF ((float) (m - 1) * 0.020673936f + 1.0f);
                host[(std::size_t) rateIndex] = 1.0f - l * 0.2060993f;
            };
            if (host[0x31] >= 0.5f)
                convert (0x30);
            if (host[0x37] >= 0.5f)
                convert (0x36);
        }
        setRate (sampleRate, false);
    }

    // the defaults reset: every value from the default table (stored, current and forwarded), velocity → volume 1,
    // DCO1 sub phase 0.5, chorus noise 0, oversampling ×2, then the rate set-up again
    void resetExo()
    {
        fxDelay.reset();
        fxReverb.reset();
        for (int i = 0; i < 0x102; ++i)
        {
            const auto v = exoDefaultParam (i);
            host[(std::size_t) i] = base[(std::size_t) i] = P[(std::size_t) i] = v;
            forwardExo (i, v);
        }
        host[0x49] = 1.0f;
        forwardExo (0x49, 1.0f);
        host[0x0b] = 0.5f;
        forwardExo (0x0b, 0.5f);
        arpAligned = false;
        chorusGateOn = false;
        forwardExo (0x46, 0.0f);
        host[0x46] = 0.0f;
        setRate (rate, true);
        for (int i = 0; i < 0x102; ++i)
            forwardExo (i, host[(std::size_t) i]);
    }

    bool slotDestination (int i) const noexcept
    {
        for (const auto& sl : slots)
            if (sl.second == i && sl.first >= 0)
                return true;
        return false;
    }

    // the parameter forwarder: stored value, then the setters unless a matrix slot drives the parameter
    void forwardExo (int i, float x) noexcept
    {
        if (i < 0 || i >= paramCount)
            return;
        base[(std::size_t) i] = x;
        if (slotDestination (i))
            return;
        setGlobalExo (i, x);
    }

    static bool globalDestination (int d) noexcept
    {
        if (d == 0x54)
            return false;
        if (d >= 0x5c && d <= 0x63)
            return true;
        switch (d - 0x24)
        {
            case 0: case 0xc: case 0xe: case 0xf: case 0x12: case 0x14: case 0x15: case 0x16: case 0x21: case 0x22: case 0x24:
            case 0x26: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c:
            case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
            case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
            case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x52: case 0x53: case 0x55: case 0x56: case 0x57:
            case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: case 0x60: case 0x61: case 0x62:
            case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d:
            case 0x6e: case 0x6f: case 0x70: case 0x71: case 0x72:
                return true;
            default:
                return false;
        }
    }

    // both matrices from the slot list (§4.3): one entry per destination (first slot order), every slot to it a term
    void compileMatrices()
    {
        auto build = [this] (std::vector<MatrixDst>& out, bool global) {
            out.clear();
            for (const auto& sl : slots)
            {
                const auto d = sl.second;
                if (d < 0 || sl.first < 0)
                    continue;
                if (std::any_of (out.begin(), out.end(), [d] (const MatrixDst& e) { return e.dst == d; }))
                    continue;
                const bool valid = global ? globalDestination (d) : ((d >= 0x5c && d <= 0x63) || ! globalDestination (d));
                if (! valid || d >= paramCount)
                    continue;
                MatrixDst e;
                e.dst = d;
                for (int k = 0; k < 8; ++k)
                    if (slots[(std::size_t) k].second == d && slots[(std::size_t) k].first >= 0)
                        e.terms.push_back ({ slots[(std::size_t) k].first, k });
                out.push_back (std::move (e));
            }
        };
        build (voiceMx, false);
        build (globalMx, true);
    }

    float voiceSource (const Voice& v, int src) const noexcept
    {
        switch (src)
        {
            case 0: return lfo1Tri;
            case 1: return lfo2Bip;
            case 2: return v.env1;
            case 3: return v.env2;
            case 4: return v.velocity;
            case 5: return v.vel2;
            case 6: return v.X;
            case 7: return v.Y;
            case 8: return v.gate;
            case 9: return v.released;
            case 10: return lfo1Shape;
            case 11: return lfo1Uni;
            case 12: return lfo2Uni;
            case 13: return v.keyC;
            case 14: return lfo1TriW;
            case 15: return lfo2BipW;
            case 16: return lfo1Out;
            case 17: return lfo1UniW;
            case 18: return lfo2UniW;
            case 19: return v.toggleVal;
            case 20: return v.keyN;
            case 21: return v.random;
            case 22: return v.Xp;
            case 23: return v.Yp;
            default: return 0.0f;
        }
    }

    static float clampUnit (float a) noexcept
    {
        auto c = a <= 0.0f ? 0.0f : a;
        return 1.0f <= c ? 1.0f : c;
    }

    void voiceMatrixExo (Voice& v) noexcept
    {
        for (const auto& e : voiceMx)
        {
            auto acc = base[(std::size_t) e.dst];
            for (const auto& t : e.terms)
                acc = voiceSource (v, t.src) * v.mxAmount[(std::size_t) t.slot] + acc;
            setVoiceExo (v, e.dst, clampUnit (acc));
        }
    }

    // the global matrix every millisecond with the sources of the newest note's voice copied first (§4.3)
    void globalMatrixExo() noexcept
    {
        const auto& v = voices[(std::size_t) mxVoice];
        Voice g;
        g.env1 = v.env1, g.env2 = v.env2, g.X = v.X, g.Y = v.Y, g.Xp = v.Xp, g.Yp = v.Yp, g.gate = v.gate, g.released = v.released;
        g.velocity = v.velocity, g.vel2 = v.vel2, g.keyC = v.keyC, g.toggleVal = v.toggleVal, g.keyN = v.keyN, g.random = v.random;
        globalSrc = g;
        for (const auto& e : globalMx)
        {
            auto acc = base[(std::size_t) e.dst];
            for (const auto& t : e.terms)
                acc = voiceSource (globalSrc, t.src) * globalAmount[(std::size_t) t.slot] + acc;
            setGlobalExo (e.dst, clampUnit (acc));
        }
    }

    // ---------------------------------------------------------------- Exo global setter (§4.1)
    void setGlobalExo (int i, float x) noexcept
    {
        const auto old = P[(std::size_t) i];
        switch (i)
        {
            case 0x36:
            case 0x37:
            {
                P[(std::size_t) i] = x;
                if (i == 0x37)
                    lfo2Sync = x >= 0.5f;
                const auto x6 = P[0x36];
                lfo2Inc = (double) (powF (10.0f, x6 * 4.0f) * 0.10000000149011612f * invFs);
                const auto e = powF (2.0f, (1.0f - x6) * 7.0f);
                lfo2Kappa = 192.0 / (double) (int) ((e - 1.0f) * 48.370079040527344f + 1.100000023841858f);
                break;
            }
            case 0x38:
            {
                P[(std::size_t) i] = x;
                const auto d = table (tLfoDelay, x);
                lfo2RampInc = d > 0.0f ? 1.0f / (d * fsF) : 1.0f;
                break;
            }
            case 0x39:
                P[(std::size_t) i] = x;
                lfo2TrigMode = x >= 0.5f;
                lfo2Gate = (! lfo2TrigMode || P[0x51] >= 0.5f) ? 1.0f : 0.0f;
                break;
            case 0x3a:
                P[(std::size_t) i] = x;
                lfo2Shape = (int) (x * 5.0f + 0.5f);
                break;
            case 0x46:
            {
                P[(std::size_t) i] = x;
                const auto q = x * 0.25f;
                chorusNoise = x * x * q * 0.012500000186264515f; // the noise gain (§3.7)
                chorus.gateThreshold = chorusNoise * 8.0f;
                break;
            }
            case 0x47:
                P[(std::size_t) i] = x;
                chorusGateOn = x >= 0.5f;
                break;
            case 0x4b:
                P[(std::size_t) i] = x;
                exoResetPending = true; // the setter queues the engine reset (FUN_18001ce10), run at the next block start
                break;
            case 0x51:
                P[(std::size_t) i] = x;
                if (old < 0.5f && x >= 0.5f)
                {
                    if (lfo1TrigMode)
                    {
                        lfo1Ramp = 0.0f;
                        lfo1SyncReset = true;
                        if (P[0x34] >= 0.5f)
                        {
                            lfo1Phase = (double) std::min (std::max (P[0x35], 0.0f), 0.9999989867210388f);
                            lfo1Wrapped = true;
                        }
                    }
                    if (lfo2TrigMode)
                    {
                        lfo2Ramp = 0.0f;
                        lfo2SyncReset = true;
                        if (P[0x3b] >= 0.5f)
                        {
                            lfo2Phase = (double) std::min (std::max (P[0x3c], 0.0f), 0.9999989867210388f);
                            lfo2Wrapped = true;
                        }
                    }
                }
                lfo1Gate = (! lfo1TrigMode || P[0x51] >= 0.5f) ? 1.0f : 0.0f;
                lfo2Gate = (! lfo2TrigMode || P[0x51] >= 0.5f) ? 1.0f : 0.0f;
                break;
            case 0x59:
                P[(std::size_t) i] = x;
                arp.mode = (int) (x * 8.0f + 0.5f);
                break;
            case 0x5b:
                P[(std::size_t) i] = x;
                arpJunoIndex = x < 0.5f;
                break;
            case 0x5c: case 0x5d: case 0x5e: case 0x5f: case 0x60: case 0x61: case 0x62: case 0x63:
                P[(std::size_t) i] = x;
                globalAmount[(std::size_t) (i - 0x5c)] = (x + x) - 1.0f;
                break;
            case 0x95:
            case 0x96:
                P[(std::size_t) i] = x;
                for (auto& v : voices)
                {
                    auto c = ((i == 0x95 ? P[0x95] : P[0x96]) + (i == 0x95 ? v.xExpr : v.yExpr)) - 0.5f;
                    c = c >= -0.5f ? (c >= 0.5f ? 0.5f : c) : -0.5f;
                    if (i == 0x95)
                        v.X = c, v.Xp = c + 0.5f;
                    else
                        v.Y = c, v.Yp = c + 0.5f;
                }
                break;
            default:
                if ((i >= 0x64 && i <= 0x94) || i == 0x98)
                {
                    P[(std::size_t) i] = x;
                    setEffectExo (i, x);
                }
                else
                    setGlobal (i, x);
                break;
        }
        for (auto& v : voices)
            setVoiceExo (v, i, x);
    }

    // the effects' parameters (filled in by the effect section)
    void setEffectExo (int i, float x) noexcept
    {
        P[(std::size_t) i] = x;
        switch (i)
        {
            case 0x86: satOn = x >= 0.5f; break;
            case 0x87: satTarget = tanF (powF (x, 0.3333333432674408f) * 1.5499999523162842f); break;
            case 0x88: satPost = x >= 0.5f; break;
            case 0x98: satHq = x >= 0.5f; break;
            default:
                if (i >= 0x64 && i <= 0x6d)
                    fxEq.setParam (i, x);
                else if (i >= 0x6e && i <= 0x78)
                    fxDelay.setParam (i, x);
                else if (i >= 0x79 && i <= 0x85)
                    fxReverb.setParam (i, x);
                else if (i >= 0x89 && i <= 0x94)
                    fxHyper.setParam (i, x);
                break;
        }
    }

    // ---------------------------------------------------------------- Exo voice setter (§4.2)
    void setVoiceExo (Voice& v, int i, float x) noexcept
    {
        v.p[(std::size_t) i] = x;
        const auto& q = v.p;
        auto dcoLfo = [&q] (int a, int b) {
            auto t = table (tDcoLfo, q[(std::size_t) a]);
            if (q[(std::size_t) b] >= 0.5f)
                t = t * 10.0f;
            return t;
        };
        auto pwSource = [] (float y) {
            return y < 0.3330000042915344f ? PwSource::lfo : (y >= 0.6660000085830688f ? PwSource::envelope : PwSource::manual);
        };
        switch (i)
        {
            case 0x0c:
                v.dco2On = x >= 0.5f;
                v.osc.slave = v.dco2On ? &v.osc2 : nullptr;
                break;
            case 0x0d:
            case 0x0e:
                v.dco2Pitch = ((float) (int) ((q[0x0d] - 0.5f) * 96.0f) + (q[0x0e] - 0.5f)) * 0.0833333358168602f;
                break;
            case 0x0f:
                v.osc2.saw = x < 0.5f ? 0.0f : 1.0f;
                break;
            case 0x10:
            case 0x11:
                v.osc2.sub = q[0x10] < 0.5f ? 0.0f : table (tSubVol, q[0x11]) * 1.1111111640930176f;
                break;
            case 0x12:
                v.osc2.subPoint = x * 0.9900000095367432f;
                break;
            case 0x13:
                v.pw2Amount = x;
                break;
            case 0x14:
            case 0x15:
                v.lfo1Dco2 = dcoLfo (0x14, 0x15);
                break;
            case 0x16:
            case 0x17:
                v.lfo2Dco2 = dcoLfo (0x16, 0x17);
                break;
            case 0x18:
                v.env2Dco2 = x * 10.0f;
                break;
            case 0x19:
                v.pw2Source = pwSource (x);
                break;
            case 0x1a:
                v.osc2.pulse = x < 0.5f ? 0.0f : 1.0f;
                break;
            case 0x1b:
                v.osc2.syncOn = x >= 0.5f;
                break;
            case 0x1e:
                v.xmodShape12 = (int) (x * 5.0f + 0.5f);
                break;
            case 0x1f:
                v.xmodShape21 = (int) (x * 5.0f + 0.5f);
                break;
            case 0x21:
                v.dco2Level = x;
                break;
            case 0x23:
                v.am = x;
                break;
            case 0x2a:
                v.fmShape = (int) (x * 5.0f + 0.5f);
                break;
            case 0x2f:
                v.vcaMode = x >= 0.3330000042915344f ? (x < 0.6660000085830688f ? 0 : 2) : 1;
                break;
            case 0x50:
            {
                const auto d = (double) x * (double) x * (double) x * 10.0;
                if (d == 0.0)
                {
                    v.glideK = 1.0f;
                    v.glideStep = 1000.0f;
                }
                else
                {
                    v.glideK = (float) (1.0 - powD (0.09999999999999998, 1.0 / (d * v.rate)));
                    v.glideStep = d > 0.0 ? (float) (1.0 / (d * v.rate)) : 1000.0f;
                }
                break;
            }
            case 0x53:
            case 0x99:
                v.octave = (((float) (int) (q[0x53] + q[0x53] + 0.5f) + q[0x99]) + q[0x99]) - 2.0f;
                break;
            case 0x5c: case 0x5d: case 0x5e: case 0x5f: case 0x60: case 0x61: case 0x62: case 0x63:
                v.mxAmount[(std::size_t) (i - 0x5c)] = (x + x) - 1.0f;
                break;
            case 0x101:
                v.fine2 = (x - 0.5f) * 24.0f;
                break;
            default:
                setVoice (v, i, x);
                break;
        }
    }

    // DCO cross-modulation / filter-FM shapes (§3.2): sine, triangle, square, saw, noise, the other DCO's waveform
    float modShape (const Voice& v, int shape, float phase, float other) const noexcept
    {
        switch (shape)
        {
            case 0: return exo ? ildsp::sinf_ (phase * 6.2831854820251465f) : sinF (phase * 6.2831854820251465f);
            case 1: return 0.5f <= phase ? 1.0f - (phase - 0.5f) * 4.0f : phase * 4.0f - 1.0f;
            case 2: return 0.5f <= phase ? 1.0f : -1.0f;
            case 3: return (phase + phase) - 1.0f;
            case 4: return v.noise;
            case 5: return other;
            default: return 0.0f;
        }
    }

    // LFO2 (§3.5): triangle, saw up, saw down, sine, sample & hold, smoothed random; BPM sync as LFO1
    void lfo2Tick (double beats) noexcept
    {
        if (lfo2SyncReset)
        {
            lfo2LastBeat = beats;
            lfo2SyncReset = false;
        }
        const auto ph = (float) lfo2Phase;
        switch (lfo2Shape)
        {
            case 0:
                if (ph < 0.25f)
                    lfo2Bip = ph + ph;
                else if (ph < 0.75f)
                    lfo2Bip = 0.5f - (((ph - 0.25f) + ph) - 0.25f);
                else
                    lfo2Bip = (((ph - 0.75f) + ph) - 0.75f) - 0.5f;
                break;
            case 1:
                lfo2Bip = ph - 0.5f;
                break;
            case 2:
                lfo2Bip = 0.5f - ph;
                break;
            case 3:
                lfo2Bip = ildsp::sinf_ (ph * 6.2831854820251465f) * 0.5f;
                break;
            case 4:
                if (lfo2Wrapped)
                {
                    lfo2Lcg = lfo2Lcg * 0x0BB38435u + 0x3619636Bu;
                    lfo2Bip = (float) (std::int32_t) lfo2Lcg * 2.3283064365386963e-10f;
                }
                break;
            case 5:
                if (! lfo2Wrapped)
                    lfo2Bip = lfo2Delta * ph + lfo2Hold;
                else
                {
                    const auto prev = lfo2Bip;
                    lfo2Hold = prev;
                    lfo2Lcg = lfo2Lcg * 0x0BB38435u + 0x3619636Bu;
                    lfo2Delta = (float) (std::int32_t) lfo2Lcg * 2.3283064365386963e-10f - prev;
                    lfo2Bip = lfo2Delta * ph + prev;
                }
                break;
            default:
                lfo2Bip = 0.0f;
                break;
        }
        if (lfo2Sync)
        {
            const auto d = (beats - lfo2LastBeat) * lfo2Kappa;
            lfo2LastBeat = beats;
            if (0.0 <= d && d < 1.0)
                lfo2Inc = d;
        }
        auto p = lfo2Phase + lfo2Inc;
        lfo2Wrapped = p >= 1.0;
        if (lfo2Wrapped)
            p = p - 1.0;
        lfo2Phase = p;
        lfo2Uni = lfo2Bip + 0.5f;
    }

    // ---------------------------------------------------------------- Exo voice control, once per host sample (§3.1)
    void controlExo (Voice& v) noexcept
    {
        switch (v.st1)
        {
            case 0:
            {
                const auto e = (1.7000000476837158f - v.env1) * v.atkK1 + v.env1;
                v.env1 = e;
                if (e >= 1.0f)
                    v.env1 = 1.0f, v.st1 = 1;
                break;
            }
            case 1:
            {
                const auto d = v.dcyK1 * v.dcy1;
                v.dcy1 = d;
                v.env1 = d * v.oneMinusSus1 + v.sus1;
                break;
            }
            case 2:
                v.env1 = v.relK1 * v.env1;
                break;
            default:
                break;
        }
        switch (v.st2)
        {
            case 0:
            {
                const auto e = (1.7000000476837158f - v.env2) * v.atkK2 + v.env2;
                v.env2 = e;
                if (e >= 1.0f)
                    v.env2 = 1.0f, v.st2 = 1;
                break;
            }
            case 1:
            {
                const auto d = v.dcyK2 * v.dcy2;
                v.dcy2 = d;
                v.env2 = d * v.oneMinusSus2 + v.sus2;
                break;
            }
            case 2:
                v.env2 = v.relK2 * v.env2;
                break;
            default:
                break;
        }
        if (++v.matrixCount >= v.matrixInterval)
        {
            v.matrixCount = 0;
            voiceMatrixExo (v);
        }
        auto pwSrc = [&v, this] (PwSource s) { return s == PwSource::lfo ? lfo1Uni : (s == PwSource::manual ? 1.0f : v.env1); };
        v.osc.pw = 0.4580000042915344f - pwSrc (v.pwSource) * 0.4320000112056732f * v.pwAmount;
        float pitch;
        if (! v.glideLinear)
        {
            pitch = (v.glideTarget - v.glide) * v.glideK + v.glide;
            v.glide = pitch;
        }
        else
        {
            pitch = v.glide;
            if (v.glideTarget > pitch)
            {
                pitch = pitch + v.glideStep;
                if (v.glideTarget <= pitch)
                    pitch = v.glideTarget;
            }
            else if (pitch > v.glideTarget)
            {
                pitch = pitch - v.glideStep;
                if (pitch <= v.glideTarget)
                    pitch = v.glideTarget;
            }
            v.glide = pitch;
        }
        const auto env2 = v.env2;
        const auto l1 = lfo1Out + lfo1Out;
        const auto l2 = lfo2BipW + lfo2BipW;
        auto common = 0.0f * v.bendRange + pitch;
        auto envTerm = env2 * v.env2Dco;
        auto s = l1 * v.lfo1Dco;
        common = common + v.octave;
        const auto lfo2Term = l2 * v.lfo2Dco;
        s = s + v.dcoPitch;
        common = common + v.pitchExpr;
        envTerm = envTerm + lfo2Term;
        common = common + midiPitch;
        s = s + common;
        s = s + envTerm;
        if (v.dco2On)
            s = s + modShape (v, v.xmodShape21, (float) v.osc2.phase, v.osc2.shapeOut) * v.p[0x1d];
        auto inc = powD (2.0, (double) s * 1.0005) * 440.0 * v.incScale;
        if (inc >= 0.25)
            inc = 0.25;
        inc = std::max (inc, 0.0);
        v.osc.inc = inc;
        {
            const auto lk = 1.0f - (float) inc * 0.2679489850997925f;
            v.osc.leak = lk >= v.osc.leakMax ? v.osc.leakMax : lk;
        }
        if (v.dco2On)
        {
            auto x = common + v.dco2Pitch;
            v.osc2.pw = 0.4580000042915344f - pwSrc (v.pw2Source) * 0.4320000112056732f * v.pw2Amount;
            x = x + l1 * v.lfo1Dco2;
            x = x + l2 * v.lfo2Dco2;
            x = x + env2 * v.env2Dco2;
            x = x + v.fine2;
            x = x + modShape (v, v.xmodShape12, (float) v.osc.phase, v.osc.shapeOut) * v.p[0x1c];
            auto f = powF (2.0f, x) * 440.0f * (float) v.incScale;
            f = f >= 0.25f ? 0.25f : f;
            f = std::max (f, 0.0f);
            v.osc2.inc = (double) f;
            const auto lk = 1.0f - f * 0.2679489850997925f;
            v.osc2.leak = lk >= v.osc2.leakMax ? v.osc2.leakMax : lk;
        }
        auto e = v.polarity * v.env1;
        e = e * v.envCutoff;
        const auto c1 = l1 * v.lfo1Cutoff;
        e = e + c1;
        auto kf = (float) (v.note - 24) * 0.0833333358168602f;
        kf = kf * v.p[0x27];
        kf = kf + l2 * v.lfo2Cutoff;
        kf = kf + e;
        if (v.filterFm != 0.0f)
            kf = kf + modShape (v, v.fmShape, (float) v.osc.phase, v.osc.shapeOut) * v.filterFm;
        auto cut = powF (2.0f, kf) * v.cutoffHz;
        cut = cut >= 5.0f ? std::min (cut, 30000.0f) : 5.0f;
        v.laneCut = cut;
        v.laneRes = v.resonance;
        ++v.age;
        v.vca = (v.velGain - v.vca) * v.vcaK + v.vca;
    }

    // ---------------------------------------------------------------- Exo: one host sample (§3.4)
    void sampleExo (float& outL, float& outR, double beats) noexcept
    {
        const auto& T = *tabs;
        if (++gmCount >= gmInterval)
        {
            gmCount = 0;
            globalMatrixExo();
        }
        if (lfo1SyncReset)
        {
            lfo1LastBeat = beats;
            lfo1SyncReset = false;
        }
        {
            const auto ph = (float) lfo1Phase;
            float tri;
            if (ph < 0.25f)
                tri = ph + ph;
            else if (ph < 0.75f)
                tri = 0.5f - (((ph - 0.25f) + ph) - 0.25f);
            else
                tri = ((ph - 0.75f) + ph) - 1.25f;
            auto x = ph * 2170.0f;
            const auto k = (int) x;
            x = x - (float) k;
            const auto a = (1.0f - x) * T.lfo[(std::size_t) k];
            const auto b = x * T.lfo[(std::size_t) k + 1];
            lfo1Shape = (a + b) * 0.5f;
            lfo1Tri = tri;
            if (lfo1Sync)
            {
                const auto d = (beats - lfo1LastBeat) * lfo1Kappa;
                lfo1LastBeat = beats;
                if (0.0 <= d && d < 1.0)
                    lfo1Inc = d;
            }
            auto p = lfo1Phase + lfo1Inc;
            lfo1Wrapped = p >= 1.0;
            if (lfo1Wrapped)
                p = p - 1.0;
            lfo1Phase = p;
            lfo1Uni = tri + 0.5f;
        }
        lfo2Tick (beats);
        const auto rg1 = lfo1Ramp * lfo1Gate;
        const auto rg2 = lfo2Ramp * lfo2Gate;
        lfo1Ramp = std::min (lfo1Ramp + lfo1RampInc, 1.0f);
        lfo2Ramp = std::min (lfo2Ramp + lfo2RampInc, 1.0f);
        lfo1UniW = rg1 * lfo1Uni;
        lfo1TriW = rg1 * lfo1Tri;
        lfo2UniW = rg2 * lfo2Uni;
        lfo2BipW = rg2 * lfo2Bip;
        lfo1Out = rg1 * lfo1Shape;

        const bool mono = P[0x4b] >= 0.5f;
        const int nv = mono ? 1 : 8, ng = mono ? 1 : 2;
        for (int vi = 0; vi < nv; ++vi)
            controlExo (voices[(std::size_t) vi]);

        for (int g = 0; g < ng; ++g)
        {
            auto& G = groups[(std::size_t) g];
            for (int l = 0; l < 4; ++l)
            {
                const auto& v = voices[(std::size_t) (g * 4 + l)];
                G.cut[(std::size_t) l] = v.laneCut;
                G.res[(std::size_t) l] = v.laneRes;
                G.slope = voices[(std::size_t) (g * 4)].slope;
                const auto r = G.res[(std::size_t) l];
                const auto k4 = r * 4.199999809265137f;
                G.onePlusRes[(std::size_t) l] = r + 1.0f;
                G.k[(std::size_t) l] = k4;
                auto c = std::min (G.cut[(std::size_t) l], G.maxCut);
                c = std::max (c, 20.0f);
                const auto w = c * G.piOverFs;
                const auto w2 = w * w;
                const auto t = ((2.4716885089874268f - w2 * 0.18975968658924103f) * w) / (2.4674010276794434f - w2);
                const auto gg = 1.0f / (t + 1.0f);
                G.G[(std::size_t) l] = gg;
                const auto g1 = gg * t;
                G.g[(std::size_t) l] = g1;
                auto g2 = g1 * g1;
                g2 = g2 * g2;
                G.g4[(std::size_t) l] = g2;
                G.norm[(std::size_t) l] = 1.0f / (k4 * g2 + 1.0f);
            }
        }

        std::array<float, 2> osL {}, osR {};
        const auto* rows = T.blit.data();
        for (int os = 0; os < oversampling; ++os)
        {
            for (int vi = 0; vi < nv; ++vi)
            {
                auto& v = voices[(std::size_t) vi];
                v.osc.tick (rows);
                v.noise = (float) (int) T.noise[(std::size_t) v.noiseIndex] * 8.631591481389478e-05f;
                if (++v.noiseIndex >= noisePoints)
                    v.noiseIndex = 0;
                float a, b;
                if (! v.dco2On)
                {
                    a = v.osc.out * v.dco1Level;
                    b = v.noise * v.noiseVol;
                }
                else
                {
                    v.osc2.tick (rows);
                    a = (v.osc.out * v.am + v.dco2Level) * v.osc2.out + v.osc.out * v.dco1Level;
                    b = v.noise * v.noiseVol;
                }
                groups[(std::size_t) (vi >> 2)].in[(std::size_t) (vi & 3)] = (b + a) * 0.27000001072883606f;
            }
            for (int g = 0; g < ng; ++g)
                ladder (groups[(std::size_t) g]);
            float l = 0.0f, r = 0.0f;
            for (int vi = 0; vi < nv; ++vi)
            {
                auto& v = voices[(std::size_t) vi];
                float a;
                if (v.vcaMode == 0)
                {
                    a = v.vca;
                    v.amp = a;
                }
                else
                {
                    auto e = (v.vcaMode == 1 ? v.env1 : v.env2) - 0.02500000037252903f;
                    if (e <= 0.0f)
                        e = 0.0f;
                    e = e * v.velGainEnv;
                    a = v.vcaSmooth ? (e - v.amp) * v.ampK + v.amp : e;
                    v.amp = a;
                }
                float pl = v.panL, pr = v.panR;
                if (v.fresh)
                    v.fresh = false;
                else
                {
                    pl = (pl - v.panLs) * v.panK + v.panLs;
                    pr = (pr - v.panRs) * v.panK + v.panRs;
                }
                v.panLs = pl, v.panRs = pr;
                const auto y = groups[(std::size_t) (vi >> 2)].out[(std::size_t) (vi & 3)];
                const auto yl = y * pl * a;
                const auto yr = y * pr * a;
                r = r + yr;
                l = l + yl;
            }
            osL[(std::size_t) os] = l;
            osR[(std::size_t) os] = r;
        }
        auto L = 0.0f, R = 0.0f;
        decimate (osL, osR, L, R);
        postExo (L, R);
        outL = L, outR = R;
    }

    // after the decimator: high-pass, chorus, the effects, main volume / pan and the output gain (§3.6–3.8)
    void postExo (float& L, float& R) noexcept
    {
        if (hpOn)
        {
            const auto yl = (hpB0 * L + hpB1 * hpXL) + hpA1 * hpYL;
            const auto yr = (hpB0 * R + hpB1 * hpXR) + hpA1 * hpYR;
            hpYL = yl, hpYR = yr;
            hpXL = L, hpXR = R;
            L = yl, R = yr;
        }
        chorusStage (L, R);
        effectsExo (L, R);
        mainSL = (mainL - mainSL) * mainK + mainSL;
        mainSR = (mainR - mainSR) * mainK + mainSR;
        L = mainSL * L;
        R = R * mainSR;
        L = outGL * L;
        R = outGR * R;
        outGL = (outTargetL - outGL) * 0.05000000074505806f + outGL;
        outGR = (outTargetR - outGR) * 0.05000000074505806f + outGR;
    }

    // the effect chain after the chorus (§5): saturation before or after the EQ, delay and reverb
    void effectsExo (float& L, float& R) noexcept
    {
        if (satOn && ! satPost)
            saturate (L, R);
        fxEq.process (L, R);
        fxHyper.process (L, R);
        fxDelay.process (L, R);
        fxReverb.process (L, R);
        if (satOn && satPost)
            saturate (L, R);
    }

    // the saturation's tanh, a [7/6] Padé fraction in float (§5.1)
    static float pade (float z) noexcept
    {
        const auto z2 = z * z;
        const auto num = (((z2 + 378.0f) * z2 + 17325.0f) * z2 + 135135.0f) * z;
        const auto den = ((z2 * 28.0f + 3150.0f) * z2 + 62370.0f) * z2 + 135135.0f;
        return num / den;
    }

    static float clamp5 (float z) noexcept
    {
        z = z > -5.0f ? z : -5.0f; // maxps / minps: the second operand when not ordered greater / less
        return z < 5.0f ? z : 5.0f;
    }

    // ln(cosh) antiderivative step of the HQ mode, tanh of the midpoint when the step is below 10⁻³ (FUN_180021e00)
    float adaa (float y, int lane) noexcept
    {
        const auto prev = satPrev[lane];
        float r;
        if (std::abs (y - prev) >= 0.001f)
        {
            const auto a = std::min (exoCoshf (y), 3.4028234663852886e+38f);
            const auto b = std::min (exoCoshf (prev), 3.4028234663852886e+38f);
            r = exoLogf (a / b);
            r = r / (y - prev);
        }
        else
            r = exoTanhf ((prev + y) * 0.5f);
        satPrev[lane] = y;
        return r;
    }

    void saturate (float& L, float& R) noexcept
    {
        const auto d = (satTarget - satDrive) * satK + satDrive;
        satDrive = d;
        const auto norm = pade (clamp5 (d)) * 3.0f;
        const auto yl = L * 3.0f * d, yr = R * 3.0f * d;
        if (norm == 0.0f) // the engine divides 0 by 0 here (drive 0); silence instead of NaN
        {
            L = R = 0.0f;
            return;
        }
        if (! satHq)
        {
            L = pade (clamp5 (yl)) / norm;
            R = pade (clamp5 (yr)) / norm;
        }
        else
        {
            const auto g = 1.0f / norm;
            L = adaa (yl, 0) * g;
            R = adaa (yr, 1) * g;
        }
    }

    // the chorus with its mode cross-fade (§3.7), as in sample()
    void chorusStage (float& L, float& R) noexcept
    {
        if (chorusFade == 0)
        {
            if (chorusMode != 0)
                chorusProcess (L, R);
            return;
        }
        auto cl = L, cr = R;
        if (chorusMode != 0)
            chorusProcess (cl, cr);
        const auto f = std::max (chorusMix, 0.0f);
        const auto dry = 1.0f - f;
        L = L * dry + f * cl;
        R = dry * R + f * cr;
        if (chorusFade == 1)
        {
            chorusMix = chorusFadeStep + chorusMix;
            if (chorusMix > 1.0f)
            {
                chorusMix = 1.0f;
                chorusFade = 0;
            }
        }
        else if (chorusFade == 2)
        {
            chorusMix = chorusMix - chorusFadeStep;
            if (chorusMix < 0.0f)
            {
                chorusMix = -1.0f;
                chorusFade = 1;
                chorusMode = chorusTarget;
                if (chorusMode != 0)
                    chorusSetMode (chorusMode);
            }
        }
    }

    // newest note's voice (the global matrix reads its sources)
    void updateMatrixVoice() noexcept
    {
        if (order.empty())
            return;
        for (int i = 0; i < 8; ++i)
            if (voices[(std::size_t) i].note == order.back())
            {
                mxVoice = i;
                return;
            }
    }

    // ---------------------------------------------------------------- chorus (§3.7)
    void designChorusHp (float f) noexcept
    {
        const auto t = tanF (chorus.piOverFs * f);
        const auto b = 1.0f / (t + 1.0f);
        chorus.hpB0 = b, chorus.hpB1 = -b, chorus.hpA1 = -((t - 1.0f) * b);
    }

    void designChorusBiquad() noexcept
    {
        const auto w = 7000.0f * 6.2831854820251465f / chorus.fs;
        float s, c;
        engineSinCos (w, s, c);
        const auto al = 0.5f / 0.7070000171661377f * s;
        const auto one = 1.0f - c, half = one * 0.5f;
        const auto inv = 1.0f / (al + 1.0f);
        chorus.b0 = inv * half;
        chorus.b1 = inv * one;
        chorus.b2 = inv * half;
        chorus.a1 = -(inv * (c * -2.0f));
        chorus.a2 = -(inv * (1.0f - al));
    }

    void chorusSetMode (int m) noexcept
    {
        if (m == 3)
        {
            chorus.inc = 8.0f / chorus.fs;
            chorus.offset = bitsF (0x3cea61dau);
            chorus.sine = true;
            chorus.base = bitsF (0x3b3ac711u), chorus.depth = bitsF (0x3a08509cu);
        }
        else
        {
            chorus.inc = (m == 1 ? 0.4000000059604645f : 0.6000000238418579f) / chorus.fs;
            chorus.offset = 0.5f;
            chorus.sine = false;
            chorus.base = bitsF (0x3a8d8ec9u), chorus.depth = bitsF (0x3b87fcb9u);
        }
        chorus.mixTarget = 0.5f;
        chorus.wetGain = 1.0f;
        {
            const auto t = tanF (chorus.piOverFs * 0.5f);
            const auto b = 1.0f / (t + 1.0f);
            chorus.hpB0 = b, chorus.hpB1 = -b, chorus.hpA1 = (1.0f - t) * b;
        }
    }

    float chorusTap (const std::vector<float>& line, int w, float delay) const noexcept
    {
        const auto samples = delay * chorus.fs;
        const auto d = (int) samples;
        const auto t = samples - (float) d;
        const auto p0 = line[(std::size_t) ((w - d) & 32767)], p1 = line[(std::size_t) ((w - d - 1) & 32767)];
        const auto p2 = line[(std::size_t) ((w - d - 2) & 32767)], p3 = line[(std::size_t) ((w - d - 3) & 32767)];
        const auto a = (p2 - p0) * 0.5f;
        const auto b = p1 - p2;
        const auto c = (p3 - p1) * 0.5f;
        const auto e = b + a;
        auto q = c + (e + b);
        const auto h = q + e;
        q = q * t;
        q = q - h;
        q = q * t;
        q = q + a;
        q = q * t;
        return q + p1;
    }

    void chorusProcess (float& L, float& R) noexcept
    {
        auto& c = chorus;
        const auto m = (L + R) * 0.5f;
        L = R = m;
        // LFO (§3.7)
        const auto ph = c.phase;
        auto ph2 = ph + c.offset;
        ph2 = ph2 - (float) (int) ph2;
        float w1, w2;
        if (c.sine)
        {
            // Exo's engine calls the library's sinf (Kepler's DLL: the correctly rounded value is measured)
            w1 = ((exo ? ildsp::sinf_ (ph * 6.2831854820251465f) : sinF (ph * 6.2831854820251465f)) + 1.0f) * 0.5f;
            w2 = ((exo ? ildsp::sinf_ (ph2 * 6.2831854820251465f) : sinF (ph2 * 6.2831854820251465f)) + 1.0f) * 0.5f;
        }
        else
        {
            auto tri = [] (float p) {
                if (p < 0.25f)
                    return (p + p) + 0.5f;
                if (p < 0.75f)
                    return (1.5f - p) - p;
                return ((p - 0.75f) + p) - 0.75f;
            };
            w1 = tri (ph), w2 = tri (ph2);
        }
        auto d1 = c.depth * w1;
        d1 = d1 + c.base;
        auto d2 = c.depth * w2;
        d2 = d2 + c.base;
        c.d1 = (d1 - c.d1) * c.s + c.d1;
        c.d2 = (d2 - c.d2) * c.s + c.d2;
        {
            auto p = ph + c.inc;
            p = p - (float) (int) p;
            c.phase = p;
        }
        // pre-filter: 1-pole high-pass, two RBJ low-pass biquads (feedback 0)
        const auto xl = 0.0f * c.wetL + L;
        const auto xr = 0.0f * c.wetR + R;
        const auto hl = (c.hpB0 * xl + c.hpB1 * c.hpXL) + c.hpA1 * c.hpYL;
        const auto hr = (c.hpB0 * xr + c.hpB1 * c.hpXR) + c.hpA1 * c.hpYR;
        c.hpXL = xl, c.hpXR = xr, c.hpYL = hl, c.hpYR = hr;
        auto biquad = [&] (std::array<float, 8>& st, float x, bool second) {
            const auto b0 = second ? c.b0 : c.b0, b1 = c.b1, b2 = c.b2, a1 = c.a1, a2 = c.a2;
            // st: y1L, y2L, x1L, x2L
            auto y = a1 * st[0] + a2 * st[1];
            y = y + b2 * st[3];
            const auto ff = b0 * x + b1 * st[2];
            y = y + ff;
            st[1] = st[0], st[0] = y, st[3] = st[2], st[2] = x;
            return y;
        };
        const auto bl1 = biquad (c.bq1L, hl, false);
        const auto br1 = biquad (c.bq1R, hr, false);
        const auto bl2 = biquad (c.bq2L, bl1, true);
        const auto br2 = biquad (c.bq2R, br1, true);
        c.lineL[(std::size_t) c.writeL] = bl2;
        c.writeL = (c.writeL + 1) & 32767;
        const auto wetL = chorusTap (c.lineL, c.writeL, c.d1);
        c.lineR[(std::size_t) c.writeR] = br2;
        c.writeR = (c.writeR + 1) & 32767;
        const auto wetR = chorusTap (c.lineR, c.writeR, c.d2);
        c.wetL = wetL, c.wetR = wetR;
        auto nl = wetL, nr = wetR;
        if (exo && chorusNoise > 0.0f)
            chorusNoiseTick (w1, w2, nl, nr);
        // post low-pass 7 kHz, equal-power mix, output gain
        const auto pl = (c.lpB1 * c.lpXL + c.lpB0 * nl) + c.lpA1 * c.lpYL;
        const auto pr = (c.lpB1 * c.lpXR + c.lpB0 * nr) + c.lpA1 * c.lpYR;
        c.lpXL = nl, c.lpXR = nr, c.lpYL = pl, c.lpYR = pr;
        c.mix = (c.mixTarget - c.mix) * c.s + c.mix;
        c.out = (c.gainTarget - c.out) * c.s + c.out;
        const auto wet = std::sqrt (c.mix), dry = std::sqrt (1.0f - c.mix);
        auto ol = pl * wet;
        auto orr = pr * wet;
        ol = ol * c.wetGain;
        ol = ol + dry * L;
        orr = orr * c.wetGain;
        orr = orr + dry * R;
        if (exo && chorusGateOn)
        {
            int n;
            if (c.gateThreshold <= std::abs (ol) || c.gateThreshold <= std::abs (orr))
                n = c.gateCount = c.gateHold, c.gateGain = 1.0f;
            else
                n = c.gateCount;
            if (n < 1)
                c.gateGain = c.gateDecay * c.gateGain;
            else
                c.gateCount = n - 1;
            ol = c.gateGain * ol;
            orr = c.gateGain * orr;
        }
        L = ol * c.out;
        R = c.out * orr;
    }

    // Kepler Exo's chorus noise (FUN_180001a60 with the designer FUN_180002690 type 2): two LCG white noises through
    // peaking biquads at 6.5 kHz (Q 0.25, gain 2·LFO + 1, redesigned every 16 samples), added to the wet taps
    void chorusNoiseTick (float w1, float w2, float& l, float& r) noexcept
    {
        auto& c = chorus;
        ++c.noiseCount;
        c.noiseA = c.noiseA * 0x0BB38435u + 0x3619636Bu;
        c.noiseB = c.noiseB * 0x0BB38435u + 0x3619636Bu;
        const auto na = (float) (std::int32_t) c.noiseA * 4.656612873077393e-10f;
        const auto nb = (float) (std::int32_t) c.noiseB * 4.656612873077393e-10f;
        if (c.noiseCount > 15)
        {
            c.noiseCount = 0;
            auto design = [&c] (float gain, std::array<float, 5>& q) {
                const auto w = 6500.0f * 6.2831854820251465f / c.fs;
                float sn, cs;
                engineSinCos (w, sn, cs);
                const auto al = 0.5f / 0.25f * sn;
                const auto aA = al * gain, aOverA = al / gain;
                const auto b1 = cs * -2.0f;
                const auto inv = 1.0f / (aOverA + 1.0f);
                q[0] = inv * (aA + 1.0f);
                q[1] = inv * b1;
                q[2] = inv * (1.0f - aA);
                q[3] = -(inv * b1);
                q[4] = -(inv * (1.0f - aOverA));
            };
            design (w1 + w1 + 1.0f, c.nqL);
            design (w2 + w2 + 1.0f, c.nqR);
        }
        auto tick = [] (std::array<float, 4>& st, const std::array<float, 5>& q, float x) {
            auto y = st[0] * q[3] + q[4] * st[1];
            y = y + q[2] * st[3];
            y = y + (x * q[0] + st[2] * q[1]);
            st[1] = st[0], st[0] = y, st[3] = st[2], st[2] = x;
            return y;
        };
        l = l + tick (c.nsL, c.nqL, na) * chorusNoise;
        r = r + tick (c.nsR, c.nqR, nb) * chorusNoise;
    }

    struct Chorus
    {
        float fs = 44100.0f, piOverFs = 0.0f, s = 0.001f;
        float phase = 0.0f, inc = 9.070295e-06f, offset = 0.5f, base = 0.00107999995816499f, depth = 0.0f;
        bool sine = false;
        float d1 = 0.01f, d2 = 0.01f;
        float hpB0 = 1.0f, hpB1 = -1.0f, hpA1 = 1.0f, hpXL = 0, hpXR = 0, hpYL = 0, hpYR = 0;
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<float, 8> bq1L {}, bq1R {}, bq2L {}, bq2R {};
        std::vector<float> lineL = std::vector<float> (32768), lineR = std::vector<float> (32768);
        int writeL = 0, writeR = 0;
        float wetL = 0, wetR = 0;
        float lpB0 = 0, lpB1 = 0, lpA1 = 0, lpXL = 0, lpXR = 0, lpYL = 0, lpYR = 0;
        float mixTarget = 0.5f, mix = 0.0f, gainTarget = 1.0f, out = 0.0f, wetGain = 1.0f;
        // Kepler Exo: filtered noise into the wet taps and the output gate (REA_rodada167d §3.7)
        std::uint32_t noiseA = 333784u, noiseB = 401782u; // the first instance's seeds of the library's counter
        int noiseCount = 16; // the engine's object reads 16 before its first sample (EXEC): the first noise sample designs the filters
        std::array<float, 5> nqL {}, nqR {}; // b0, b1, b2, −a1, −a2
        std::array<float, 4> nsL {}, nsR {}; // y1, y2, x1, x2
        float gateThreshold = 0.0025f, gateGain = 1.0f, gateDecay = 0.99965197f;
        int gateHold = 440, gateCount = 440;
    };

    struct Decimator
    {
        std::array<std::array<std::array<float, 4>, 2>, 6> x {}, y {};
        std::array<float, 4> last {};
    };

public:
    std::shared_ptr<const Tables> tabs;

private:
    std::array<Voice, 8> voices {};
    std::array<Ladder, 2> groups {};
    Decimator dec;
    Chorus chorus;
    std::array<float, paramCount> P {}, base {};
    bool routes = false;
    double rate = 44100.0, tempo = 125.0, beatPos = 0.0, seconds = 0.0;
    bool playing = false;
    float fsF = 44100.0f, invFs = 1.0f / 44100.0f;
    int oversampling = 2;
    // LFO1
    double lfo1Phase = 0.0, lfo1Inc = 0.0, lfo1Kappa = 1.0, lfo1LastBeat = 0.0;
    bool lfo1Sync = false, lfo1Wrapped = false, lfo1SyncReset = false, lfo1TrigMode = false;
    float lfo1Ramp = 1.0f, lfo1RampInc = 1.0f, lfo1Gate = 1.0f, lfo1Shape = 0.0f, lfo1Tri = 0.0f, lfo1Uni = 0.5f, lfo1Out = 0.0f;
    float lfoToggle = 0.5f;
    std::uint32_t lcg = 77784u;
    float midiPitch = 0.0f;
    // high-pass
    bool hpOn = true;
    float hpPiOverFs = 0.0f, hpB0 = 1.0f, hpB1 = -1.0f, hpA1 = 1.0f, hpXL = 0, hpXR = 0, hpYL = 0, hpYR = 0;
    // chorus switching
    int chorusMode = 0, chorusTarget = 0, chorusFade = 0;
    float chorusMix = 1.0f, chorusFadeStep = 0.0f, chorusS = 0.001f;
    // gains
    float mainL = 0.0f, mainR = 0.0f, mainSL = 0.0f, mainSR = 0.0f, mainK = 0.0f;
    float outGL = 6.309000015258789f, outGR = 6.309000015258789f, outTargetL = 6.309000015258789f, outTargetR = 6.309000015258789f;
    float midiVolume = 1.0f, midiPan = 0.5f; // the last MIDI volume / pan events (§3 of REA_rodada165g-flbeta165k)
    // notes, hold, arpeggiator
    std::vector<std::pair<int, int>> held;
    std::vector<int> order;
    int lastNote = 64;
    bool holdOn = false, holdFirst = false;
    Arp arp;
    bool arpOn = false, arpSync = false, arpRunning = false, arpWaiting = true, arpAligned = false;
    int arpRestart = 0, arpNote = -1;
    double arpPeriod = 0.811, arpGate = 1.0, arpGateLen = 0.811, arpLast = 0.0, arpDelta = 0.0, arpAcc = 0.0, arpNext = 0.0, arpStart = 0.0;
    bool arpJunoIndex = true; // a note added mid-arpeggio shifts the step index (the Juno behaviour, Exo's 0x5b off)
    // Kepler Exo
    struct MatrixTerm { int src, slot; };
    struct MatrixDst { int dst = 0; std::vector<MatrixTerm> terms; };
    bool exo = false, slotsDirty = false;
    std::array<float, paramCount> host {};
    std::array<std::pair<int, int>, 8> slots {};
    std::vector<MatrixDst> voiceMx, globalMx;
    std::array<float, 8> globalAmount {};
    Voice globalSrc;
    int gmCount = 0, gmInterval = 44, mxVoice = 0;
    double lfo2Phase = 0.0, lfo2Inc = 0.0, lfo2Kappa = 1.0, lfo2LastBeat = 0.0;
    bool lfo2Sync = false, lfo2Wrapped = false, lfo2SyncReset = false, lfo2TrigMode = false;
    int lfo2Shape = 0;
    std::uint32_t lfo2Lcg = 55555u;
    float lfo2Bip = 0.0f, lfo2Uni = 0.0f, lfo2Hold = 0.0f, lfo2Delta = 0.0f, lfo2Ramp = 1.0f, lfo2RampInc = 0.0f, lfo2Gate = 0.0f;
    float lfo1UniW = 0.0f, lfo1TriW = 0.0f, lfo2UniW = 0.0f, lfo2BipW = 0.0f;
    float chorusNoise = 0.0f;
    bool chorusGateOn = false;
    // effects (§5)
    bool satOn = false, satPost = false, satHq = false;
    kepler_exo::Eq fxEq;
    kepler_exo::Delay fxDelay;
    kepler_exo::HyperChorus fxHyper;
    kepler_exo::Reverb fxReverb;
    bool exoResetPending = false;
    float satTarget = 1.0f, satDrive = 1.0f, satK = 0.01f, satPrev[2] {};
};

// MIDI velocity as FL's wrapper derives it from Init.Vol (§5.1)
int velocityFromInitVol (double initVol) noexcept
{
    auto v = lnF ((float) initVol * 10.0f + 1.0f) * 41.71426010131836f;
    v = v >= 0.0f ? std::min (v, 127.0f) : 0.0f;
    return (int) v;
}

// ------------------------------------------------------------------------------------------------ LYNX instrument
//
// Levels: FL's wrapper reads only Final.Pan (note pan + channel pan, the engine's own pan law), Final.Pitch and Final.FCut/
// FRes; it ignores Final.Vol. FL also sends the channel volume and pan as MIDI events, which set the engine's output
// gains (REA_rodada165g-flbeta165k §3, FL renders: the channel pan counts twice, the volume follows the engine's cubic
// law), so LYNX's channel gains reach this instrument only as those two events — not as an output gain. Init.Vol
// = (11^(1.28·velocity) − 1)/10 from LYNX's velocity (as for GMS, REA_rodada28 inference). The tempo is the song tempo;
// while the transport plays, the song position (beats) drives the synced LFO1 and the arpeggiator clock.

class KeplerInstrument final : public Instrument
{
public:
    ~KeplerInstrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    void prepare (double sr) noexcept override
    {
        delete retired.exchange (nullptr);
        take();
        if (sr == rate.load() && synth != nullptr)
            return;
        rate.store (sr);
        // a new engine at this rate (not on the audio thread's hot path: prepare runs before playback)
        delete incoming.exchange (build().release());
        take();
    }

    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        delete retired.exchange (nullptr);
        tempo.store (bpm > 0.0 ? (float) bpm : 140.0f);
        channelCents.store (channel.sampler.pitch * 100.0f);
        if (channel.pluginState == state && channel.samplePath == dll)
            return;
        // only panel values changed (a knob of the editor, round 168q): the playing engine takes them, notes go on
        if (channel.samplePath == dll && synth != nullptr && liveChange (state, channel.pluginState))
        {
            state = channel.pluginState;
            patch = parseState (state); // (a later rebuild at another rate starts from the new values)
            return;
        }
        state = channel.pluginState;
        dll = channel.samplePath;
        patch = parseState (state);
        tables = loadTables (dll);
        delete incoming.exchange (build().release()); // built here, swapped in by the audio thread
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    void setSongPosition (double ticks) noexcept override
    {
        playing = lastTicks >= 0.0 && ticks != lastTicks;
        lastTicks = ticks;
        songBeats = ticks / (double) ppq;
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        if (synth == nullptr)
            return;
        const auto k = std::clamp (key, 0, 127);
        owners.start (k, startingNoteId());
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        const auto initVol = (std::pow (11.0, 1.28 * vel) - 1.0) / 10.0;
        const auto v7 = velocityFromInitVol (initVol);
        if (sounding[(std::size_t) k])
            synth->noteOff (k, v7); // FL's wrapper releases the previous voice of the key first
        sounding[(std::size_t) k] = true;
        notePan[(std::size_t) k] = nextPan;
        const auto knob = channelCents.load();
        noteCents[(std::size_t) k] = (float) knob + nextCents;
        slides[(std::size_t) k] = {};
        slides[(std::size_t) k].anchor (live.ref (knob));
        synth->noteOn (k, v7);
        synth->setPanExpression (k, nextPan);
        sentPan[(std::size_t) k] = -10000.0f;
        sentCents[(std::size_t) k] = -10000.0f;
        nextPan = 0.0f, nextCents = 0.0f;
        quiet = 0;
    }

    // FL's PITCH knob and main pitch move the sounding notes too (round 167a), at once
    void setChannelPitchNow (float knob, float rest) noexcept override
    {
        live.set (knob, rest);
        for (int k = 0; k < 128; ++k)
            if (sounding[(std::size_t) k])
            {
                auto& sl = slides[(std::size_t) k];
                sl.follow (live.now());
                if (sl.jump != 0.0f)
                    sl.advance (0.0f);
            }
    }

    // a slide note (issue #14): the sounding keys glide their Final.Pitch to the slide's pitch
    bool followsChannelPorta() const noexcept override { return true; } // (round 168l: HostSlide::anchor)
    void slideTo (int key, float cents, float samples) noexcept override
    {
        take();
        const auto to = (float) (100.0 * (std::clamp (key, 0, 127) - 60)) + channelCents.load() + cents;
        for (int k = 0; k < 128; ++k)
            if (sounding[(std::size_t) k])
                slides[(std::size_t) k].start (to - ((float) (100 * (k - 60)) + noteCents[(std::size_t) k]), samples);
    }

    void noteOff (int key) noexcept override
    {
        // (round 190ej: FL's Kepler keeps one note per key: a later note of the key takes it over and only the end of
        // the note holding it ends it; the earlier note's end does nothing [EXEC: /R render, round 190ej])
        if (! owners.ends (key, endingNoteId()))
            return;
        take();
        const auto k = std::clamp (key, 0, 127);
        if (synth == nullptr || ! sounding[(std::size_t) k])
            return;
        sounding[(std::size_t) k] = false;
        synth->noteOff (k, 0);
    }
    KeyOwners owners; // the note that holds each key (round 190ej)

    void allNotesOff() noexcept override
    {
        for (int k = 0; k < 128; ++k)
            noteOff (k);
    }

    // idle once every voice is silent and the chorus/filters had a quarter of a second to ring out
    bool idle() const noexcept override { return synth == nullptr || quiet >= (int) (rate.load() * 0.25); }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        if (synth == nullptr || n <= 0)
            return;
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / 3.141592653589793) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        // per-note Final levels, sent when they change (the wrapper compares them once per block)
        for (int k = 0; k < 128; ++k)
            if (sounding[(std::size_t) k])
            {
                const auto p = (float) std::clamp ((double) notePan[(std::size_t) k] + pan, -1.0, 1.0);
                if (p != sentPan[(std::size_t) k])
                    synth->setPanExpression (k, sentPan[(std::size_t) k] = p);
                const auto& slide = slides[(std::size_t) k];
                const auto c = slide.cents != 0.0f ? noteCents[(std::size_t) k] + slide.cents : noteCents[(std::size_t) k];
                if (c != sentCents[(std::size_t) k])
                    synth->setPitchExpression (k, sentCents[(std::size_t) k] = c);
            }
        // the channel volume and pan as FL sends them (§3 of REA_rodada165g-flbeta165k): MIDI volume = raw volume
        // (0…12800)·128/12800 truncated, at most 127; MIDI pan = (raw pan − 6400)/100 truncated. LYNX's gains are
        // channelGain(volume) = (11^v − 1)/10 on its cos/sin pan law, so v comes back as log(10·g + 1)/log 11.
        {
            const auto v = vol > 0.0 ? std::log (10.0 * vol + 1.0) / std::log (11.0) : 0.0;
            const auto midiVolume = (int) std::min<long long> (127, std::max<long long> (0, std::llround (v * 12800.0) / 100));
            const auto midiPan = (int) std::clamp<long long> (std::llround (pan * 6400.0) / 100, -64, 64);
            if (midiVolume != sentVolume || midiPan != sentMidiPan)
            {
                synth->setMidiLevels ((float) midiVolume * 0.007874015718698502f, (float) midiPan * 0.0078125f + 0.5f, sentVolume < 0);
                sentVolume = midiVolume, sentMidiPan = midiPan;
            }
        }
        synth->setTempo ((double) tempo.load());
        synth->setTransport (playing, songBeats);
        for (int done = 0; done < n;)
        {
            const auto m = std::min (n - done, (int) scratchL.size());
            std::fill (scratchL.begin(), scratchL.begin() + m, 0.0f);
            std::fill (scratchR.begin(), scratchR.begin() + m, 0.0f);
            synth->render (scratchL.data(), scratchR.data(), m);
            for (int i = 0; i < m; ++i)
            {
                left[done + i] += scratchL[(std::size_t) i];
                right[done + i] += scratchR[(std::size_t) i];
            }
            done += m;
        }
        quiet = synth->silent() ? std::min (quiet + n, 1 << 30) : 0;
        for (int k = 0; k < 128; ++k) // (issue #14) a slide moves on block by block
            if (sounding[(std::size_t) k] && slides[(std::size_t) k].moving())
                slides[(std::size_t) k].advance ((float) n);
    }

private:
    std::unique_ptr<Synth> build() const
    {
        if (! patch.usable || tables == nullptr || patch.exo != tables->exo) // a Kepler-2 state needs Kepler Exo's DLL
            return nullptr;
        auto s = std::make_unique<Synth>();
        s->init (patch, tables, rate.load());
        return s;
    }

    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* s = incoming.exchange (nullptr))
        {
            retired.store (synth.release());
            synth.reset (s);
            sounding.fill (false);
            quiet = 0;
            sentVolume = sentMidiPan = -1000; // a new engine: the levels are sent again (snapped)
            liveParams.clear(); // (built from the newest state)
        }
        if (synth != nullptr)
            liveParams.drain ([this] (int k, float v) { synth->hostParam (k, v); });
    }

    // queues the panel values that differ between two states of the same layout (UI thread)
    bool liveChange (const std::vector<std::uint8_t>& before, const std::vector<std::uint8_t>& after)
    {
        if (before.size() != after.size() || ! patch.usable)
            return false;
        const auto first = keplerPanelOffset (before, 0), again = keplerPanelOffset (after, 0);
        if (first < 0 || first != again)
            return false;
        auto count = 0;
        while (keplerPanelOffset (after, count) >= 0)
            ++count;
        const auto end = (std::size_t) first + 4 * (std::size_t) count;
        if (std::memcmp (before.data(), after.data(), (std::size_t) first) != 0
            || std::memcmp (before.data() + end, after.data() + end, after.size() - end) != 0)
            return false;
        std::vector<std::pair<int, float>> changed;
        for (int k = 0; k < count; ++k)
            if (std::memcmp (before.data() + first + 4 * k, after.data() + first + 4 * k, 4) != 0)
                changed.emplace_back (k, f32At (after, (std::size_t) first + 4 * (std::size_t) k));
        if (changed.size() > liveParams.space())
            return false;
        for (const auto& [k, v] : changed)
            liveParams.push (k, v);
        return true;
    }

public:
    // automation (round 168q): the clip's 0…1 is the stored value itself
    void setParameter (int index, float value) noexcept override
    {
        take();
        if (synth != nullptr)
            synth->hostParam (index, std::clamp (value, 0.0f, 1.0f));
    }

private:

    std::unique_ptr<Synth> synth;
    LiveParamQueue liveParams;
    std::atomic<Synth*> incoming { nullptr }, retired { nullptr };
    Patch patch;
    std::shared_ptr<const Tables> tables;
    std::atomic<double> rate { 44100.0 };
    std::atomic<float> tempo { 140.0f }, channelCents { 0.0f };
    std::vector<std::uint8_t> state;
    std::string dll;
    double lastTicks = -1.0, songBeats = 0.0;
    bool playing = false;
    int quiet = 0, sentVolume = -1000, sentMidiPan = -1000;
    std::array<bool, 128> sounding {};
    std::array<float, 128> notePan {}, noteCents {}, sentPan {}, sentCents {};
    std::array<HostSlide, 128> slides {};
    LiveChannelPitch live; // (audio thread, round 167a)
    float nextPan = 0.0f, nextCents = 0.0f;
    std::array<float, 512> scratchL {}, scratchR {};
};
} // namespace

bool keplerStateUsable (const std::vector<std::uint8_t>& state) { return parseState (state).usable; }

bool keplerDllUsable (const std::string& path) { return loadTables (path) != nullptr; }

bool keplerExoState (const std::vector<std::uint8_t>& state)
{
    const auto p = parseState (state);
    return p.usable && p.exo;
}

bool keplerExoDllUsable (const std::string& path)
{
    const auto t = loadTables (path);
    return t != nullptr && t->exo;
}

int keplerPanelOffset (const std::vector<std::uint8_t>& state, int k)
{
    const auto w = exoWrapperSize (state);
    if (! parseState (state).usable || k < 0 || k >= (int) u32 (state, w + 0x1D))
        return -1;
    return (int) w + 0x21 + 4 * k;
}

std::unique_ptr<Instrument> makeKeplerInstrument() { return std::make_unique<KeplerInstrument>(); }

namespace
{
std::vector<EngineParam> named (std::initializer_list<std::pair<int, const char*>> list)
{
    std::vector<EngineParam> out;
    for (const auto& [index, name] : list)
    {
        EngineParam p;
        p.index = index;
        p.name = name;
        out.push_back (p);
    }
    std::sort (out.begin(), out.end(), [] (const EngineParam& a, const EngineParam& b) { return a.index < b.index; });
    return out;
}

template <bool exoTable>
std::optional<float> keplerValue (const ChannelDef& c, const EngineParam& p)
{
    if (keplerExoState (c.pluginState) != exoTable)
        return std::nullopt;
    const auto at = keplerPanelOffset (c.pluginState, p.index);
    if (at < 0 && ! exoTable && (p.index == 34 || p.index == 35) && keplerPanelOffset (c.pluginState, 0) >= 0)
        return defaultParam (panelToInternal (p.index)); // (a 34-float state: the LFO retrig panels not saved yet)
    if (at < 0 && exoTable && p.index >= 0 && p.index < 0x9b && keplerPanelOffset (c.pluginState, 0) >= 0)
        return exoDefaultParam (p.index); // (a state saved before the last parameters existed: see keplerSetValue)
    if (at < 0)
        return std::nullopt;
    const auto v = f32At (c.pluginState, (std::size_t) at);
    return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;
}

template <bool exoTable>
bool keplerSetValue (ChannelDef& c, const EngineParam& p, float raw)
{
    if (keplerExoState (c.pluginState) != exoTable)
        return false;
    // round 173d2: every installation preset is a version-2 state of 34 floats, without the LFO retrig panels
    // (34 "Retrig Phase Active", 35 "Phase", REA_rodada46 §1.1), so their edits were lost; FL's later states carry
    // 36 (REA_rodada46 §1.2, "Stroll Up": 4 / 36). The count is explicit in the state: grow it to 36
    if (! exoTable && (p.index == 34 || p.index == 35) && keplerPanelOffset (c.pluginState, p.index) < 0
        && exoWrapperSize (c.pluginState) == 0 && keplerPanelOffset (c.pluginState, 0) >= 0 && u32 (c.pluginState, 0x1D) == 34)
    {
        auto& s = c.pluginState;
        const auto at = (std::ptrdiff_t) (0x21 + 4 * 34);
        std::uint8_t extra[8] {};
        const float retrig = defaultParam (0x34), phase = defaultParam (0x35);
        std::memcpy (extra, &retrig, 4);
        std::memcpy (extra + 4, &phase, 4);
        s.insert (s.begin() + at, extra, extra + 8);
        const std::uint32_t n = 36, size = u32 (s, 4) + 8;
        std::memcpy (s.data() + 0x1D, &n, 4);
        std::memcpy (s.data() + 4, &size, 4);
    }
    // round 173d2: FL's "Kepler Exo" Default preset is a version-2 "Kepler-2" state of 151 floats (0…0x96), without
    // VCA smooth, Saturation HQ, Master detune and HPF on (0x97…0x9a; FL's engine fills them from its defaults), so
    // their edits were lost. Grow the explicit count to 0x9b with the defaults, and the stream and wrapper sizes with it
    if (exoTable && p.index >= 0 && p.index < 0x9b && keplerPanelOffset (c.pluginState, p.index) < 0 && keplerPanelOffset (c.pluginState, 0) >= 0)
    {
        auto& s = c.pluginState;
        const auto w = exoWrapperSize (s);
        const auto n = (int) u32 (s, w + 0x1D);
        if (n > 0 && n < 0x9b)
        {
            std::vector<std::uint8_t> extra ((std::size_t) (0x9b - n) * 4);
            for (int i = n; i < 0x9b; ++i)
            {
                const auto v = exoDefaultParam (i);
                std::memcpy (extra.data() + 4 * (i - n), &v, 4);
            }
            const auto add = (std::uint32_t) extra.size();
            s.insert (s.begin() + (std::ptrdiff_t) (w + 0x21 + 4 * (std::size_t) n), extra.begin(), extra.end());
            const std::uint32_t count = 0x9b, size = u32 (s, w + 4) + add;
            std::memcpy (s.data() + w + 0x1D, &count, 4);
            std::memcpy (s.data() + w + 4, &size, 4);
            if (w > 0)
            {
                const auto outer = u32 (s, 8) + add;
                std::memcpy (s.data() + 8, &outer, 4);
            }
        }
    }
    const auto at = keplerPanelOffset (c.pluginState, p.index);
    if (at < 0 || ! std::isfinite (raw))
        return false;
    std::memcpy (c.pluginState.data() + at, &raw, 4);
    return true;
}
} // namespace

const std::vector<EngineParam>& keplerParams()
{
    // the panel positions of a "Kepler-1" state (REA_rodada46 §1.1), named after the editor's controls
    static const auto table = named ({
        { 0, "Arp on" }, { 1, "Arp mode" }, { 2, "Arp range" }, { 3, "Arp rate" }, { 4, "Arp sync" }, { 5, "LFO rate" },
        { 6, "LFO sync" }, { 7, "LFO delay" }, { 8, "LFO trig mode" }, { 9, "LFO trigger" }, { 10, "DCO LFO" }, { 11, "PW" },
        { 12, "PW source" }, { 13, "Pulse" }, { 14, "Saw" }, { 15, "Sub" }, { 16, "Sub level" }, { 17, "Noise" },
        { 18, "High-pass" }, { 19, "Cutoff" }, { 20, "Resonance" }, { 21, "Env polarity" }, { 22, "Env amount" },
        { 23, "VCF LFO" }, { 24, "Key follow" }, { 25, "VCA source" }, { 26, "Attack" }, { 27, "Decay" }, { 28, "Sustain" },
        { 29, "Release" }, { 30, "Chorus" }, { 31, "Hold" }, { 32, "Octave" }, { 33, "Volume" }, { 34, "LFO retrig" },
        { 35, "LFO phase" } });
    return table;
}

const std::vector<EngineParam>& keplerExoParams()
{
    // the "Kepler-2" list by internal index (REA_rodada167d §1), the controls of Kepler Exo's panel (pitch-bend range
    // 0x54 and the hidden Juno-index option 0x5b are not on it)
    static const auto table = named ({
        { 0x00, "DCO1 LFO1" }, { 0x01, "DCO1 LFO1 wide" }, { 0x02, "DCO1 LFO2" }, { 0x03, "DCO1 LFO2 wide" },
        { 0x04, "DCO1 env2" }, { 0x05, "DCO1 PW" }, { 0x06, "DCO1 PW source" }, { 0x07, "DCO1 pulse" },
        { 0x08, "DCO1 saw" }, { 0x09, "DCO1 sub" }, { 0x0a, "DCO1 sub level" }, { 0x0b, "DCO1 sub phase" },
        { 0x0c, "DCO2 on" }, { 0x0d, "DCO2 semitones" }, { 0x0e, "DCO2 detune" }, { 0x0f, "DCO2 saw" },
        { 0x10, "DCO2 sub" }, { 0x11, "DCO2 sub level" }, { 0x12, "DCO2 sub phase" }, { 0x13, "DCO2 PW" },
        { 0x14, "DCO2 LFO1" }, { 0x15, "DCO2 LFO1 wide" }, { 0x16, "DCO2 LFO2" }, { 0x17, "DCO2 LFO2 wide" },
        { 0x18, "DCO2 env2" }, { 0x19, "DCO2 PW source" }, { 0x1a, "DCO2 pulse" }, { 0x1b, "DCO2 sync" },
        { 0x1c, "Cross mod 1>2" }, { 0x1d, "Cross mod 2>1" }, { 0x1e, "Cross mod 1>2 shape" },
        { 0x1f, "Cross mod 2>1 shape" }, { 0x20, "DCO1 level" }, { 0x21, "DCO2 level" }, { 0x22, "Noise" },
        { 0x23, "AM" }, { 0x24, "High-pass" }, { 0x25, "Cutoff" }, { 0x26, "Resonance" }, { 0x27, "Key follow" },
        { 0x28, "Filter slope" }, { 0x29, "Filter FM" }, { 0x2a, "Filter FM shape" }, { 0x2b, "Env polarity" },
        { 0x2c, "Env amount" }, { 0x2d, "VCF LFO1" }, { 0x2e, "VCF LFO2" }, { 0x2f, "VCA source" },
        { 0x30, "LFO1 rate" }, { 0x31, "LFO1 sync" }, { 0x32, "LFO1 delay" }, { 0x33, "LFO1 trig mode" },
        { 0x34, "LFO1 retrig" }, { 0x35, "LFO1 phase" }, { 0x36, "LFO2 rate" }, { 0x37, "LFO2 sync" },
        { 0x38, "LFO2 delay" }, { 0x39, "LFO2 trig mode" }, { 0x3a, "LFO2 shape" }, { 0x3b, "LFO2 retrig" },
        { 0x3c, "LFO2 phase" }, { 0x3d, "Env1 attack" }, { 0x3e, "Env1 decay" }, { 0x3f, "Env1 sustain" },
        { 0x40, "Env1 release" }, { 0x41, "Env2 attack" }, { 0x42, "Env2 decay" }, { 0x43, "Env2 sustain" },
        { 0x44, "Env2 release" }, { 0x45, "Chorus" }, { 0x46, "Chorus noise" }, { 0x47, "Chorus gate" },
        { 0x48, "Volume" }, { 0x49, "Velocity to volume" }, { 0x4a, "Pan" }, { 0x4b, "Mono" }, { 0x4c, "Legato" },
        { 0x4d, "Retrigger" }, { 0x4e, "Release mode" }, { 0x4f, "Portamento mode" }, { 0x50, "Portamento" },
        { 0x51, "LFO trigger" }, { 0x52, "Hold" }, { 0x53, "Octave" }, { 0x55, "Arp on" }, { 0x56, "Arp rate" },
        { 0x57, "Arp sync" }, { 0x58, "Arp range" }, { 0x59, "Arp mode" }, { 0x5a, "Arp gate" },
        { 0x5c, "Mod 1 amount" }, { 0x5d, "Mod 2 amount" }, { 0x5e, "Mod 3 amount" }, { 0x5f, "Mod 4 amount" },
        { 0x60, "Mod 5 amount" }, { 0x61, "Mod 6 amount" }, { 0x62, "Mod 7 amount" }, { 0x63, "Mod 8 amount" },
        { 0x64, "EQ on" }, { 0x65, "EQ low Q" }, { 0x66, "EQ mid Q" }, { 0x67, "EQ high Q" }, { 0x68, "EQ low gain" },
        { 0x69, "EQ mid gain" }, { 0x6a, "EQ high gain" }, { 0x6b, "EQ low freq" }, { 0x6c, "EQ mid freq" },
        { 0x6d, "EQ high freq" }, { 0x6e, "Delay on" }, { 0x6f, "Delay sync" }, { 0x70, "Delay amount" },
        { 0x71, "Delay time" }, { 0x72, "Delay right tap" }, { 0x73, "Delay feedback" },
        { 0x74, "Delay feedback cutoff" }, { 0x75, "Delay feedback filter" }, { 0x76, "Delay stereo" },
        { 0x77, "Delay blur" }, { 0x78, "Delay tone" }, { 0x79, "Reverb on" }, { 0x7a, "Reverb low cut" },
        { 0x7b, "Reverb high cut" }, { 0x7c, "Reverb predelay" }, { 0x7d, "Reverb predelay feedback" },
        { 0x7e, "Reverb size" }, { 0x7f, "Reverb diffusion" }, { 0x80, "Reverb decay" }, { 0x81, "Reverb damping" },
        { 0x82, "Reverb width" }, { 0x83, "Reverb dry" }, { 0x84, "Reverb early" }, { 0x85, "Reverb wet" },
        { 0x86, "Saturation on" }, { 0x87, "Saturation" }, { 0x88, "Saturation post" }, { 0x89, "Hyper chorus on" },
        { 0x8a, "Hyper chorus delay" }, { 0x8b, "Hyper chorus feedback" }, { 0x8c, "Hyper chorus rate" },
        { 0x8d, "Hyper chorus depth" }, { 0x8e, "Hyper chorus movement" }, { 0x8f, "Hyper chorus movement rate" },
        { 0x90, "Hyper chorus lines" }, { 0x91, "Hyper chorus high cut" }, { 0x92, "Hyper chorus low cut" },
        { 0x93, "Hyper chorus mix" }, { 0x94, "Hyper chorus spread" }, { 0x95, "X offset" }, { 0x96, "Y offset" },
        { 0x97, "VCA smooth" }, { 0x98, "Saturation HQ" }, { 0x99, "Master detune" }, { 0x9a, "HPF on" } });
    return table;
}

namespace modelparams
{
Table keplerModelTable() { return { &keplerParams(), &keplerValue<false>, &keplerSetValue<false> }; }
Table keplerExoModelTable() { return { &keplerExoParams(), &keplerValue<true>, &keplerSetValue<true> }; }
} // namespace modelparams

namespace kepler
{
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::string& dll, const std::vector<RefNote>& notes,
                                    double sampleRate, int length, const RefOptions& options)
{
    const auto patch = parseState (state);
    auto tables = loadTables (dll);
    if (! patch.usable || tables == nullptr || patch.exo != tables->exo || length <= 0)
        return {};
    auto synth = std::make_unique<Synth>();
    synth->init (patch, tables, sampleRate);
    synth->setTempo (options.tempo);
    std::vector<float> out (2 * (std::size_t) length);
    std::vector<float> l ((std::size_t) std::max (1, options.block)), r ((std::size_t) std::max (1, options.block));
    if (options.midiVolume >= 0)
        synth->setMidiLevels ((float) std::min (options.midiVolume, 127) * 0.007874015718698502f,
                              (float) std::clamp (options.midiPan, -64, 64) * 0.0078125f + 0.5f, false);
    for (int w = 0; w < options.warmup;)
    {
        const auto len = std::min (std::max (1, options.block), options.warmup - w);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        synth->render (l.data(), r.data(), len);
        w += len;
    }
    // the harness (and FL's wrapper): note-ons/offs between blocks, per-note Final levels sent at the next block
    struct Pending { int key; float pan, cents; };
    std::vector<Pending> pending;
    std::array<bool, 128> sounding {}; // the wrapper's per-key voice records
    for (int n0 = 0; n0 < length;)
    {
        auto len = std::min (options.block, length - n0);
        for (const auto& nt : notes)
        {
            if (nt.start > n0 && nt.start < n0 + len)
                len = nt.start - n0;
            if (nt.release > n0 && nt.release < n0 + len)
                len = nt.release - n0;
        }
        for (const auto& nt : notes)
            if (nt.release == n0 && sounding[(std::size_t) (nt.key & 127)])
            {
                sounding[(std::size_t) (nt.key & 127)] = false;
                synth->noteOff (nt.key, 0);
            }
        for (const auto& nt : notes)
            if (nt.start == n0)
            {
                const auto v = velocityFromInitVol (nt.initVol);
                if (sounding[(std::size_t) (nt.key & 127)])
                    synth->noteOff (nt.key, v); // FL's wrapper releases the key's previous voice first
                sounding[(std::size_t) (nt.key & 127)] = true;
                synth->noteOn (nt.key, v);
                synth->setPanExpression (nt.key, 0.0f); // Init.Pan
                pending.push_back ({ nt.key, (float) nt.finalPan, (float) nt.pitchCents });
            }
        for (const auto& p : pending)
        {
            synth->setPitchExpression (p.key, p.cents);
            synth->setPanExpression (p.key, p.pan);
        }
        pending.clear();
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        if (options.transport)
            synth->setTransport (true, (double) n0 / sampleRate * options.tempo / 60.0);
        synth->render (l.data(), r.data(), len);
        for (int i = 0; i < len; ++i)
        {
            out[2 * (std::size_t) (n0 + i)] = l[(std::size_t) i];
            out[2 * (std::size_t) (n0 + i) + 1] = r[(std::size_t) i];
        }
        n0 += len;
    }
    return out;
}
} // namespace kepler
} // namespace lynx
