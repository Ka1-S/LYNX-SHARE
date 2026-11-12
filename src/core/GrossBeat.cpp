// FL Studio's Gross Beat (REA_rodada53): state reader, envelope player, time-travel buffer reader and volume stage.
// Behaviour rebuilt from the plugin DLL of the user's installation; written from scratch in LYNX's own words. The order of
// the floating-point operations follows the plugin wherever it changes the result (float vs double vs x87 extended,
// IPP-style vector steps), which is what makes the output sample-identical.

#include "core/GrossBeat.h"
#include "core/FlCurve.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstring>
#include <limits>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) // x86 only: rcpps cannot be reproduced through NEON; ARM64 takes the plain path
    #include "core/Simd.h"
    #define LYNX_GB_SSE 1
#endif

namespace lynx::grossbeat
{
namespace
{
using ext = long double; // the plugin's x87 steps (64-bit mantissa where the platform has it)

constexpr double pi = 3.141592653589793;
constexpr float twoPiF = 6.28318548f;
constexpr int intMax = std::numeric_limits<std::int32_t>::max();
constexpr int holdNow = std::numeric_limits<std::int32_t>::min() + 1;

// Delphi's Round (and cvtsd2si): to nearest, ties to even
inline std::int64_t roundEven (double x) noexcept { return (std::int64_t) std::nearbyint (x); }

// The plugin's vector exp for the volume tension (IPP's 11-bit "A11" exp as dispatched on FMA-capable x86-64; its outputs
// were matched sample for sample over |x| ≤ 7.5, the range the tension uses): x = n·ln2 + r with n the nearest integer to
// x·log2(e) (float), a float cubic in r evaluated with fused multiply-adds, scaled by 2^n.
inline float exp11 (float x) noexcept
{
    const float n = std::nearbyint (x * 1.44269502f);
    const float nl = n * 0.693147182f;
    const float r = x - nl;
    const float p = std::fma (std::fma (std::fma (0.165419281f, r, 0.503705382f), r, 1.00011802f), r, 1.0f);
    return std::ldexp (p, (int) n);
}

inline std::uint32_t floatBits (float f) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}

inline bool atLeastOne (double phase) noexcept // the player's loop test: high word of the double above 0x3fefffff
{
    std::uint64_t u;
    std::memcpy (&u, &phase, 8);
    return (std::uint32_t) (u >> 32) > 0x3fefffffu;
}

std::int32_t i32 (const std::uint8_t* p) noexcept
{
    std::int32_t v;
    std::memcpy (&v, p, 4);
    return v;
}

// ---------------------------------------------------------------------------------------------------------------
// envelope points: normalisation to one 4-beat bar and per-point coefficients (done by the plugin when a slot loads)

constexpr double barLength = 4.0;

void insertPoint (std::vector<Point>& pts, double x, float y)
{
    x = std::clamp (x, 0.0, barLength);
    const auto n = (int) pts.size();
    int idx = n;
    double before = 0.0;
    if (n >= 1)
    {
        double cum = pts[0].dx;
        before = cum;
        for (int i = 1; i < n; ++i)
        {
            const auto next = cum + pts[(std::size_t) i].dx;
            if (x < next)
            {
                idx = i;
                before = cum;
                break;
            }
            cum = next;
            before = cum;
        }
    }
    Point p;
    const auto gap = (float) (x - before);
    p.dx = (double) (0.0f < gap ? gap : 0.0f);
    p.y = y;
    pts.insert (pts.begin() + idx, p);
    if (idx < n)
        pts[(std::size_t) idx + 1].dx -= pts[(std::size_t) idx].dx;
}

int waveCount (const Point& p) noexcept
{
    if (p.type < 3 || p.type > 6)
        return -1;
    const auto v = 1.0 - std::fabs ((double) p.tension) * 0.6;
    const auto e = std::exp ((double) (float) v * std::log (1000001.0));
    auto n = (int) roundEven ((double) (float) (e - 1.0) * 0.001);
    if (p.type == 3 && 0.0f <= p.tension)
        ++n;
    return n;
}

void tensionCurve (float k, float& kOut, float& inv, float& lnK) noexcept
{
    kOut = k;
    if (k != 0.0f)
    {
        inv = (float) (1.0 / std::fabs ((double) k));
        lnK = (float) std::log (std::fabs ((double) k) + 1.0);
    }
}

void precompute (std::vector<Point>& pts) noexcept
{
    for (std::size_t i = 0; i < pts.size(); ++i)
    {
        auto& p = pts[i];
        if (p.dx < 0.0)
            p.dx = 0.0;
        if (i == 0)
            continue;
        const auto bits = floatBits (p.tension);
        p.dir = (std::int8_t) ((bits & 0x7fffffffu) == 0 ? 0 : ((bits >> 31) != 0 ? -1 : 1));
        const auto n = waveCount (p);
        const float sign = p.tension < 0.0f ? -1.0f : 1.0f;
        switch (p.type)
        {
            case 0:
            case 1:
            {
                const auto mag = (double) (float) std::fabs ((double) p.tension);
                auto k = (float) (std::exp (mag * std::log (1000001.0)) - 1.0);
                const int s = (bits << 1) == 0 ? 0 : 1 - 2 * (int) (bits >> 31);
                k *= (float) s;
                tensionCurve (k, p.k14, p.k18, p.k1c);
                break;
            }
            case 3:
                p.k1c = sign * 0.5f;
                p.k14 = (float) (n + 1);
                p.k18 = 1.0f / (p.k14 - sign);
                break;
            case 4:
                p.k14 = (float) (n + 1);
                p.k18 = 1.0f / p.k14;
                break;
            case 5:
                p.k1c = sign * 0.5f;
                p.k14 = (float) ((n + 1) * 2);
                p.k18 = twoPiF;
                break;
            case 6:
                p.k14 = (float) ((double) n + 0.5);
                p.k18 = twoPiF;
                if (n == 0 && p.dir > 0)
                    p.dir = 2;
                break;
            case 7:
            case 8:
                p.k14 = p.tension;
                break;
            case 9:
                p.k14 = (float) std::fabs ((double) p.tension);
                break;
            case 11:
            case 12:
            {
                auto k = std::fabs ((double) p.tension) * 0.9;
                if (k <= 1.52587890625e-05)
                    k = 1.52587890625e-05;
                const auto a = 1.0 - k;
                p.k14 = (float) (1.0 - a * a * a);
                p.k18 = (1.0f - p.k14) / p.k14;
                p.k1c = p.k18;
                if (p.dir > 0)
                {
                    p.k18 = -p.k18;
                    p.k1c = p.k18 - 1.0f;
                }
                if (p.type == 12)
                {
                    p.k18 = (float) ((double) p.k18 * 0.5);
                    p.k1c = (float) ((double) p.k1c * 0.5);
                }
                break;
            }
            case 10:
            {
                // round 190ee: "Smooth" — a cubic Hermite segment whose end slopes come from the neighbouring Smooth
                // segments (the plugin's own preparation, Gross Beat_x64.dll 0x784d60 at 0x785157, is FLEngine's law:
                // core/FlCurve prepareSmooth). Before this round it played as a straight line.
                flcurve::SmoothSide before, after;
                if (pts[i - 1].type == 10)
                {
                    before.smooth = true;
                    before.dx = pts[i - 1].dx;
                    before.hasY = i >= 2;
                    before.y = i >= 2 ? (double) pts[i - 2].y : 0.0;
                }
                if (i + 1 < pts.size() && pts[i + 1].type == 10)
                {
                    after.smooth = true;
                    after.dx = pts[i + 1].dx;
                    after.y = (double) pts[i + 1].y;
                }
                const auto s = flcurve::prepareSmooth (p.tension, p.dx, (double) pts[i - 1].y, (double) p.y, before, after);
                p.k14 = s.c0;
                p.k18 = s.c1;
                p.k1c = s.c2;
                if (s.sign == 2)
                    p.dir = 2;
                break;
            }
            default: // 2 hold
                break;
        }
        // types 0, 1, 7, 8, 9, 11, 12 with no tension: straight line
        if (p.type < 16 && ((0x1b83u >> p.type) & 1u) != 0 && p.tension == 0.0f)
            p.dir = 2;
    }
}

void normalise (std::vector<Point>& pts)
{
    if (pts.empty())
        insertPoint (pts, 0.0, 1.0f);
    else if (0.0 < pts[0].dx)
        insertPoint (pts, 0.0, pts[0].y);
    double total = 0.0;
    for (const auto& p : pts)
        total += p.dx;
    if (0.0 < total)
    {
        const auto f = barLength / total;
        if (f != 1.0)
            for (auto& p : pts)
                p.dx *= f;
    }
    else
        insertPoint (pts, barLength, pts.back().y);
}

// curve of the segment that arrives at `p`, at phase `ph` (0…1)
double shape (const Point& p, double ph) noexcept
{
    if (p.dir == 2)
        return ph;
    std::uint64_t u;
    std::memcpy (&u, &ph, 8);
    const auto firstHalf = (std::uint32_t) (u >> 32) < 0x3fe00000u;
    const auto k14 = (double) p.k14, k18 = (double) p.k18, k1c = (double) p.k1c;
    const auto kBits = floatBits (p.k14);
    switch (p.type)
    {
        case 0:
            if (kBits == 0)
                return ph;
            if ((kBits & 0x80000000u) == 0)
                return 1.0 - (std::exp ((1.0 - ph) * k1c) - 1.0) * k18;
            return (std::exp (ph * k1c) - 1.0) * k18;
        case 1:
        {
            double v;
            if (firstHalf)
            {
                if (kBits == 0)
                    v = ph * 2.0;
                else if ((kBits & 0x80000000u) == 0)
                    v = 1.0 - (std::exp ((1.0 - ph * 2.0) * k1c) - 1.0) * k18;
                else
                    v = (std::exp (ph * 2.0 * k1c) - 1.0) * k18;
                return v * 0.5;
            }
            if (kBits == 0)
                v = (1.0 - ph) * 2.0;
            else if ((kBits & 0x80000000u) == 0)
                v = 1.0 - (std::exp ((1.0 - (1.0 - ph) * 2.0) * k1c) - 1.0) * k18;
            else
                v = (std::exp ((1.0 - ph) * 2.0 * k1c) - 1.0) * k18;
            return 1.0 - v * 0.5;
        }
        case 3:
            return (double) ((float) roundEven (ph * k14 - k1c) * p.k18);
        case 4:
        {
            auto v = ph * k14 - 0.5;
            const auto i = roundEven (v - 0.5);
            v = (v - (double) i) * 2.0 - 1.0;
            if (p.dir < 0)
            {
                const auto a = 1.0 - std::fabs (v);
                v = (1.0 - a * a * a) * (std::signbit (v) ? -1.0 : 1.0);
            }
            else
                v = v * v * v;
            return ((double) i + v * 0.5 + 1.0) * k18;
        }
        case 5:
            return (double) (roundEven (ph * k14 - k1c) & 1);
        case 6:
            if (p.dir < 0)
                return (std::sin ((ph * k14 + 0.75) * k18) + 1.0) * 0.5;
            else
            {
                const auto v = ph * k14 - 0.5;
                const auto i = roundEven (v - 0.5);
                return std::fabs ((v - (double) i) * 2.0 - 1.0);
            }
        case 7:
        {
            const auto c = p.dir > 0 ? 1.0 - ph : ph;
            const auto c3 = c * c * c;
            return ph - (c3 * c3 - c) * k14;
        }
        case 8:
            if (firstHalf)
            {
                const auto h = ph * 2.0;
                const auto c = p.dir > 0 ? 1.0 - h : h;
                const auto c3 = c * c * c;
                return (h - (c3 * c3 - c) * k14) * 0.5;
            }
            else
            {
                const auto h = (1.0 - ph) * 2.0;
                const auto c = p.dir > 0 ? 1.0 - h : h;
                const auto c3 = c * c * c;
                return 1.0 - (h - (c3 * c3 - c) * k14) * 0.5;
            }
        case 9:
        {
            const auto q = ph * (double) 1.57079637f;
            const auto c = p.dir < 1 ? 1.0 - std::cos (q) : std::sin (q);
            return ph - (ph - c) * k14;
        }
        case 10: // round 190ee: Smooth (players 0x789a70 / 0x788d60), the same law as flcurve::evaluate
        {
            const auto x2 = ph * ph, x1 = ph - 1.0;
            return ph + ((((x2 * 3.0 - x2 * 2.0 * ph) + x2 * x1 * k14) + ph * x1 * x1 * k18) - ph) * k1c;
        }
        case 11:
            if (p.dir < 1)
                return k18 / (1.0 - k14 * ph) - k1c;
            return k18 / (1.0 - k14 * (1.0 - ph)) - k1c;
        case 12:
            if (firstHalf)
            {
                if (p.dir < 1)
                    return k18 / (1.0 - k14 * ph * 2.0) - k1c;
                return k18 / (1.0 - k14 * (1.0 - ph * 2.0)) - k1c;
            }
            else
            {
                const auto h = (1.0 - ph) * 2.0;
                if (p.dir < 1)
                    return (1.0 - k18 / (1.0 - k14 * h)) + k1c;
                return (1.0 - k18 / (1.0 - k14 * (1.0 - h))) + k1c;
            }
        default:
            return 0.0; // 2 = hold (and the undecoded ones)
    }
}

// The plugin's envelope player (one per slot kind): loops over the bar, `adv` beats per step
struct Player
{
    int idx = 0;
    double phase = 0.0, rate = 1.0, start = 0.0, delta = 0.0;

    void reset (const std::vector<Point>& p) noexcept
    {
        idx = 0;
        if (p[0].dx == 0.0)
            rate = 1.0, phase = 1.0;
        else
            rate = 1.0 / p[0].dx, phase = 0.0;
        start = (double) p[0].y;
        delta = 0.0;
    }

    double step (const std::vector<Point>& p, double& inc, double adv) noexcept
    {
        phase += inc;
        while (atLeastOne (phase))
        {
            if (rate == 0.0)
                phase = adv;
            else
                phase = (phase - 1.0) / rate;
            start = (double) p[(std::size_t) idx].y;
            idx = idx < (int) p.size() - 1 ? idx + 1 : 0;
            delta = (double) p[(std::size_t) idx].y - start;
            const auto d = p[(std::size_t) idx].dx;
            if (d <= 0.0)
            {
                rate = 1.0;
                phase += 1.0;
            }
            else
            {
                rate = 1.0 / d;
                phase *= rate;
            }
            inc = adv * rate;
        }
        return start + shape (p[(std::size_t) idx], phase) * delta;
    }

    template <typename T>
    void render (const std::vector<Point>& p, T* out, int n, double adv) noexcept
    {
        auto inc = adv * rate;
        for (int i = 0; i < n; ++i)
            out[i] = (T) step (p, inc, adv);
    }

    void advance (const std::vector<Point>& p, double adv) noexcept
    {
        auto inc = adv * rate;
        step (p, inc, adv);
    }
};

// one-pole follower with separate rise/fall coefficients (the plugin's attack/release smoother): x87 arithmetic, the
// running value stays in extended precision within a block and is kept as a double between blocks
struct Follower
{
    double up = 0.0, down = 0.0, state = 0.0;

    void set (double riseSamples, double fallSamples) noexcept
    {
        auto coef = [] (double t) { return t > 0.0 ? std::exp (std::log (0.01) / std::max (t, 1.0)) : 0.0; };
        up = coef (riseSamples);
        down = coef (fallSamples);
    }

    void run (float* x, int n) noexcept
    {
        const auto u = (ext) up, d = (ext) down;
        auto y = (ext) state;
        for (int i = 0; i < n; ++i)
        {
            const auto v = (ext) x[i];
            const auto c = v >= y ? u : d;
            y = (y - v) * c + v;
            x[i] = (float) y;
        }
        state = (double) (std::fabs (y) > (ext) 5.96046448e-08f ? y : 0.0L);
    }
};

// The x87 part of the plugin's sinc reader (phases of the window cosines and band-limited sines, their recurrence seeds
// and the tap distances). The plugin runs this part with the x87 unit at 64-bit precision rounding toward −∞; on
// x86 with an 80-bit long double LYNX does the same, elsewhere it rounds to nearest (a last-bit difference).
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__)) && __LDBL_MANT_DIG__ == 64
    #define LYNX_GB_X87 1
#endif

#if LYNX_GB_X87
__attribute__ ((noinline))
#endif
void readerSetup (double fr, double c80, double cutoff, double cw, double cc, float* A, float* B, float* X) noexcept
{
#if LYNX_GB_X87
    unsigned short saved = 0, down = 0;
    __asm__ volatile ("fnstcw %0" : "=m"(saved));
    down = (unsigned short) ((saved & 0xf0ffu) | 0x0700u);
    __asm__ volatile ("fldcw %0" : : "m"(down) : "memory");
    __asm__ volatile ("" : "+m"(fr), "+m"(c80), "+m"(cutoff), "+m"(cw), "+m"(cc));
    const ext pie = 0xC.90FDAA22168C234p-2L; // the x87 π rounded toward −∞
#else
    const ext pie = 3.14159265358979323846264338327950288L;
#endif
    const ext e0 = (ext) fr * (ext) (-c80) + (ext) 1.5707963267948966;
    const ext e1 = e0 + (ext) (-c80);
    const ext e2 = (ext) fr * (ext) cutoff;
    const ext e3 = e2 + (ext) cutoff;
    volatile double d0 = (double) e0, d1 = (double) e1, d2 = (double) e2, d3 = (double) e3;
    // FL takes these four sines from IPP's 50-bit vector sine; LYNX uses the C library's, which agrees after the float
    // rounding below except at whole-sample positions, where IPP gives 1 − 2⁻⁵³ for the first one (EXEC)
    const double s0 = d0 == 1.5707963267948963 ? 0.99999999999999989 : std::sin (d0);
    const double s1 = std::sin (d1), s2 = std::sin (d2), s3 = std::sin (d3);
    const ext v = (ext) s0 * (ext) cw - (ext) s1;
    const ext v2 = v * (ext) cw - (ext) s0;
    const ext w = (ext) s2 * (ext) cc - (ext) s3;
    const ext w2 = w * (ext) cc - (ext) s2;
    const ext x = (ext) fr * pie;
    const ext xm = x - pie;
    // the sines reach the float lanes through the x87 unit too (rounded with its mode, not SSE's)
    volatile ext t0 = s0, t1 = s1, t2 = s2, t3 = s3;
    A[0] = (float) w, A[1] = (float) t2, A[2] = (float) v, A[3] = (float) t0;
    B[0] = (float) w2, B[1] = (float) t3, B[2] = (float) v2, B[3] = (float) t1;
    X[0] = (float) xm, X[1] = (float) x, X[2] = (float) (xm - pie), X[3] = (float) (x + pie);
#if LYNX_GB_X87
    __asm__ volatile ("fldcw %0" : : "m"(saved) : "memory");
#endif
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// state

std::optional<State> parseState (const std::uint8_t* d, std::size_t size)
{
    if (d == nullptr || size < 8)
        return std::nullopt;
    auto v = (std::int32_t) i32 (d);
    if (v >= 0x90004)
        return std::nullopt;
    if (v > 0xffff)
        v = (std::int32_t) ((std::uint32_t) v >> 16);
    State s;
    s.version = v;
    s.params = { 0, 0, 256, 256, 1765, 5000, 0, 0, 0x10000 };
    s.options = { 0, 0, 0, 0, 0, 0, 0, 1, 1 };
    std::size_t pos = 4;
    const auto paramBytes = (std::size_t) (v < 2 ? 0x20 : 0x24);
    if (pos + paramBytes > size)
        return std::nullopt;
    for (std::size_t i = 0; i < paramBytes / 4; ++i)
        s.params[i] = i32 (d + pos + 4 * i);
    pos += paramBytes;
    auto nopt = v < 3 ? 8 : 9;
    if (v < 5)
        --nopt;
    if (pos + (std::size_t) nopt > size)
        return std::nullopt;
    for (int i = 0; i < nopt; ++i)
        s.options[(std::size_t) i] = d[pos + (std::size_t) i];
    pos += (std::size_t) nopt;

    for (int k = 0; k < 2 * slotsPerKind; ++k)
    {
        auto& slot = s.slots[(std::size_t) k];
        if (pos >= size)
            return std::nullopt;
        std::size_t len = d[pos++];
        if (len == 0xff)
        {
            if (pos + 4 > size)
                return std::nullopt;
            len = (std::size_t) (std::uint32_t) i32 (d + pos);
            pos += 4;
        }
        if (pos + len + 15 + 12 > size)
            return std::nullopt;
        slot.name.assign ((const char*) d + pos, len);
        pos += len;
        if (v < 4 && slot.name == "Slot " + std::to_string (k % slotsPerKind + 1))
            slot.name.clear();
        slot.triggerSync = i32 (d + pos);
        slot.positionSync = i32 (d + pos + 4);
        slot.hold = i32 (d + pos + 8);
        slot.link = d[pos + 12] != 0;
        slot.keyHeld = d[pos + 13] != 0;
        slot.oneShot = d[pos + 14] != 0;
        pos += 15;
        // envelope stream: kind (3 = map), version, point count, points, five integers
        const auto kind = i32 (d + pos);
        if (kind != 3)
            return std::nullopt;
        const auto ev = i32 (d + pos + 4);
        pos += 8;
        if (ev >= 4)
            return std::nullopt; // the plugin skips such a stream entirely (never written by FL)
        const auto n = i32 (d + pos);
        pos += 4;
        const auto pointBytes = (std::size_t) (ev < 2 ? 8 : 16) + (std::size_t) (ev < 1 ? 4 : 8);
        if (n < 0 || n > 100000 || pos + (std::size_t) n * pointBytes + 20 > size)
            return std::nullopt;
        slot.points.resize ((std::size_t) n);
        for (auto& p : slot.points)
        {
            if (ev < 2)
            {
                float x, y;
                std::memcpy (&x, d + pos, 4);
                std::memcpy (&y, d + pos + 4, 4);
                p.dx = x, p.y = y;
                pos += 8;
            }
            else
            {
                double x, y;
                std::memcpy (&x, d + pos, 8);
                std::memcpy (&y, d + pos + 8, 8);
                p.dx = x, p.y = (float) y;
                pos += 16;
            }
            std::memcpy (&p.tension, d + pos, 4);
            if (ev >= 1)
                p.type = d[pos + 4];
            pos += ev < 1 ? 4 : 8;
            if (! std::isfinite (p.dx))
                p.dx = 0.0;
            if (std::isnan (p.y))
                p.y = 1.0f;
        }
        pos += 20;
        normalise (slot.points);
        precompute (slot.points);
        slot.flat = true;
        for (const auto& p : slot.points)
            if (floatBits (p.y) != floatBits (slot.points[0].y))
                slot.flat = false;
        if ((double) slot.points[0].y != 1.0)
            slot.flat = false;
    }
    // round 190ee: "Trigger slots from MIDI notes" — on for states before version 9, else the byte after the slots
    s.midiTrigger = true;
    if (v > 8 && pos < size)
        s.midiTrigger = d[pos] != 0;
    return s;
}

std::pair<int, int> slotForKey (int key) noexcept
{
    // kind 0 first, then kind 1 (the later match wins, as FL's lookup 0x844ae0; the ranges never overlap)
    std::pair<int, int> r { -1, -1 };
    if (key >= timeBaseKey && key < timeBaseKey + slotsPerKind)
        r = { 0, key - timeBaseKey };
    if (key >= volumeBaseKey && key < volumeBaseKey + slotsPerKind)
        r = { 1, key - volumeBaseKey };
    return r;
}

std::vector<std::uint8_t> pack (const std::vector<std::uint8_t>& flState, int ppq)
{
    std::vector<std::uint8_t> out { 'L', 'X', 'G', 'B', 1, 0, 0, 0, 0, 0, 0, 0 };
    std::memcpy (out.data() + 8, &ppq, 4);
    out.insert (out.end(), flState.begin(), flState.end());
    return out;
}

bool unpack (const std::vector<std::uint8_t>& c, std::vector<std::uint8_t>& flState, int& ppq)
{
    if (c.size() < 12 || std::memcmp (c.data(), "LXGB", 4) != 0 || c[4] != 1)
        return false;
    ppq = i32 (c.data() + 8);
    if (ppq <= 0)
        ppq = 96;
    flState.assign (c.begin() + 12, c.end());
    return true;
}

void setPpq (std::vector<std::uint8_t>& c, int ppq)
{
    if (c.size() >= 12 && std::memcmp (c.data(), "LXGB", 4) == 0 && ppq > 0)
        std::memcpy (c.data() + 8, &ppq, 4);
}

// ---------------------------------------------------------------------------------------------------------------
// writing, defaults and point editing (round 168m)

namespace
{
template <typename T>
void put (std::vector<std::uint8_t>& b, T v)
{
    const auto* p = reinterpret_cast<const std::uint8_t*> (&v);
    b.insert (b.end(), p, p + sizeof (T));
}

void finishSlot (Slot& slot)
{
    if (slot.points.empty())
        slot.points = { Point { 0.0, 1.0f, 0.0f, 2 }, Point { barLength, 1.0f, 0.0f, 2 } };
    normalise (slot.points);
    precompute (slot.points);
    slot.flat = true;
    for (const auto& p : slot.points)
        if (floatBits (p.y) != floatBits (slot.points[0].y))
            slot.flat = false;
    if ((double) slot.points[0].y != 1.0)
        slot.flat = false;
}
} // namespace

std::vector<std::uint8_t> writeState (const State& st)
{
    std::vector<std::uint8_t> b;
    b.reserve (72 * 96);
    // version 8 in the high half, as FL's demo projects; version 9 (FL 2026's own) when the state came as version 9 or
    // has "Trigger slots from MIDI notes" off, which only version 9 can say (one byte after the slots, round 190ee)
    const auto v9 = st.version >= 9 || ! st.midiTrigger;
    put<std::int32_t> (b, v9 ? 0x00090003 : 0x00080003);
    for (const auto v : st.params)
        put<std::int32_t> (b, v);
    b.insert (b.end(), st.options.begin(), st.options.end());
    for (const auto& slot : st.slots)
    {
        const auto len = slot.name.size();
        if (len < 0xff)
            b.push_back ((std::uint8_t) len);
        else
        {
            b.push_back (0xff);
            put<std::int32_t> (b, (std::int32_t) len);
        }
        b.insert (b.end(), slot.name.begin(), slot.name.end());
        put<std::int32_t> (b, slot.triggerSync);
        put<std::int32_t> (b, slot.positionSync);
        put<std::int32_t> (b, slot.hold);
        b.push_back (slot.link ? 1 : 0);
        b.push_back (slot.keyHeld ? 1 : 0);
        b.push_back (slot.oneShot ? 1 : 0);
        // TCustomMEnvelope "map" stream, version 3: Δx, level, tension, curve type, two zero bytes, direction cache
        put<std::int32_t> (b, 3);
        put<std::int32_t> (b, 3);
        put<std::int32_t> (b, (std::int32_t) slot.points.size());
        for (const auto& p : slot.points)
        {
            put<double> (b, p.dx);
            put<double> (b, (double) p.y);
            put<float> (b, p.tension);
            b.push_back (p.type);
            b.push_back (0);
            b.push_back (0);
            b.push_back ((std::uint8_t) p.dir);
        }
        put<std::int32_t> (b, 0); // flags, then no sustain / loop points
        for (int i = 0; i < 4; ++i)
            put<std::int32_t> (b, -1);
    }
    if (v9)
        b.push_back (st.midiTrigger ? 1 : 0);
    return b;
}

State emptyState()
{
    State s;
    s.version = 9; // round 190ee: FL 2026's instance, whose state is version 9 with the MIDI byte (off: 0x83fcf0)
    s.midiTrigger = false;
    s.params = { 0, 0, 256, 256, 1765, 5000, 0, 0, 0x10000 };
    s.options = { 0, 0, 0, 0, 0, 0, 1, 1, 1 }; // FUN_0083fcf0: high quality (byte 6), byte 7 and declick (byte 8) on
    for (auto& slot : s.slots)
    {
        slot.triggerSync = slot.positionSync = 5;
        slot.points.clear();
        finishSlot (slot);
    }
    return s;
}

namespace
{
// a slot drawn as LYNX's own steps and lines (x in beats): `step` jumps to y at x (FL's hold segment), `line` reaches y
// at x in a straight (or bent) segment
struct Shape
{
    std::vector<EditPoint> pts;
    explicit Shape (float y0) { pts.push_back ({ 0.0, y0, 0.0f, 2 }); }
    Shape& step (double x, float y) { pts.push_back ({ x, y, 0.0f, 2 }); return *this; }
    Shape& line (double x, float y, float tension = 0.0f) { pts.push_back ({ x, y, tension, 0 }); return *this; }
};

// time travel: a delay of d beats behind the present is level 1 − d/8 (level 0 = two bars back, REA_rodada53 §2.2)
float delayLevel (double beats) { return (float) (1.0 - beats / 8.0); }

// every `period` beats the first `cell` beats of the period play again (a stutter repeating the period's start)
Shape stutter (double period, double cell)
{
    Shape s (1.0f);
    for (double start = 0.0; start < barBeats - 1.0e-9; start += period)
    {
        for (double c = cell; c < period - 1.0e-9; c += cell)
            s.step (start + c, delayLevel (c));
        s.step (start + period, 1.0f);
    }
    return s;
}

// a gate: in every period the sound plays during `open` of it (from the period's start, or after `shift`)
Shape gate (double period, double open, double shift = 0.0)
{
    Shape s (shift > 0.0 ? 0.0f : 1.0f);
    for (double start = 0.0; start < barBeats - 1.0e-9; start += period)
    {
        if (shift > 0.0)
        {
            s.step (start + shift, 1.0f);
            if (shift + open < period - 1.0e-9)
                s.step (start + shift + open, 0.0f);
            s.step (start + period, 0.0f);
        }
        else
        {
            s.step (start + open, 0.0f);
            s.step (start + period, 1.0f);
        }
    }
    return s;
}

// sixteen steps of a bar, on/off
Shape pattern (const char* bits)
{
    Shape s (bits[0] == '1' ? 1.0f : 0.0f);
    for (int i = 1; i <= 16; ++i)
    {
        const auto on = i < 16 ? bits[i] == '1' : bits[0] == '1';
        s.step (i * 0.25, on ? 1.0f : 0.0f);
    }
    return s;
}

void fill (Slot& slot, const char* name, const Shape& shape)
{
    slot.name = name;
    setPoints (slot, shape.pts);
}
} // namespace

State factoryState()
{
    auto s = emptyState();
    auto time = [&s] (int i) -> Slot& { return s.slots[(std::size_t) i]; };
    auto volume = [&s] (int i) -> Slot& { return s.slots[(std::size_t) (slotsPerKind + i)]; };

    // time travel: slot 1 plays the input as it comes
    fill (time (1), "Repeat 1/2 beat", stutter (1.0, 0.5));
    fill (time (2), "Repeat 1/4 beat", stutter (0.5, 0.25));
    fill (time (3), "Stutter 1/8", stutter (1.0, 0.125));
    fill (time (4), "Stutter 1/16", stutter (0.5, 0.0625));
    fill (time (5), "Stutter triplet", stutter (1.0, 1.0 / 3.0));
    {
        Shape roll (1.0f); // beats 1–2 as they come, beat 3 in quarters, beat 4 in sixteenths of a beat
        for (double c = 0.25; c < 1.0 - 1.0e-9; c += 0.25)
            roll.step (2.0 + c, delayLevel (c));
        roll.step (3.0, 1.0f);
        for (double c = 0.0625; c < 1.0 - 1.0e-9; c += 0.0625)
            roll.step (3.0 + c, delayLevel (c));
        roll.step (4.0, 1.0f);
        fill (time (6), "Build-up roll", roll);
    }
    fill (time (7), "Half speed", Shape (1.0f).line (4.0, delayLevel (2.0))); // read at half speed through the bar
    fill (time (8), "Half speed 2 beats", Shape (1.0f).line (2.0, delayLevel (1.0)).step (2.0, 1.0f).line (4.0, delayLevel (1.0)));
    {
        Shape stop (1.0f); // the speed falls from 1 to 0 over the bar: d = t²/8
        for (int i = 1; i <= 16; ++i)
        {
            const auto t = i * 0.25;
            stop.line (t, delayLevel (t * t / 8.0));
        }
        fill (time (9), "Tape stop", stop);
    }
    {
        Shape stop (1.0f); // the last beat slows to a stop: d = u²/2
        stop.step (3.0, 1.0f);
        for (int i = 1; i <= 8; ++i)
        {
            const auto u = i * 0.125;
            stop.line (3.0 + u, delayLevel (u * u / 2.0));
        }
        fill (time (10), "Tape stop last beat", stop);
    }
    {
        Shape rev (1.0f); // every beat backwards: d = 2u
        for (int b = 0; b < 4; ++b)
            rev.line (b + 1.0, delayLevel (2.0)).step (b + 1.0, 1.0f);
        fill (time (11), "Reverse beats", rev);
    }
    fill (time (12), "Reverse 2 beats", Shape (1.0f).line (2.0, delayLevel (4.0)).step (2.0, 1.0f).line (4.0, delayLevel (4.0)).step (4.0, 1.0f));
    fill (time (13), "Reverse last beat", Shape (1.0f).step (3.0, 1.0f).line (4.0, delayLevel (2.0)).step (4.0, 1.0f));
    {
        Shape scratch (1.0f); // back for half a beat, forward again for the other half
        for (int b = 0; b < 4; ++b)
            scratch.line (b + 0.5, delayLevel (1.0), 0.25f).line (b + 1.0, 1.0f, -0.25f);
        fill (time (14), "Scratch", scratch);
    }
    fill (time (15), "Echo beat 1", Shape (1.0f).step (1.0, delayLevel (1.0)).step (2.0, 1.0f).step (4.0, 1.0f));
    fill (time (16), "Repeat half bar", Shape (1.0f).step (2.0, delayLevel (2.0)).step (4.0, 1.0f));
    fill (time (17), "Repeat 1 beat", Shape (1.0f).step (1.0, delayLevel (1.0)).step (2.0, delayLevel (2.0)).step (3.0, delayLevel (3.0)).step (4.0, 1.0f));
    {
        Shape brake (1.0f); // round 168m: the last two beats slow down to a stop (speed 1 − u/2, d = u²/4)
        brake.step (2.0, 1.0f);
        for (int i = 1; i <= 16; ++i)
        {
            const auto u = i * 0.125;
            brake.line (2.0 + u, delayLevel (u * u / 4.0));
        }
        fill (time (18), "Vinyl brake", brake);
    }
    // the last beat rewinds at three times the speed (a DJ's spin back), then the next bar comes in
    fill (time (19), "Spin back", Shape (1.0f).step (3.0, 1.0f).line (4.0, delayLevel (4.0), 0.2f).step (4.0, 1.0f));
    {
        Shape start (1.0f); // the first beat speeds up from a stop (speed u, d = u − u²/2), then plays half a beat late
        for (int i = 1; i <= 8; ++i)
        {
            const auto u = i * 0.125;
            start.line (u, delayLevel (u - u * u / 2.0));
        }
        start.step (4.0, 1.0f);
        fill (time (20), "Tape start", start);
    }
    // the bar's first beat and a half plays three times: 3 + 3 + 2 eighths
    fill (time (21), "Repeat 3-3-2", Shape (1.0f).step (1.5, delayLevel (1.5)).step (3.0, delayLevel (3.0)).step (4.0, 1.0f));
    {
        Shape wobble (1.0f); // a slow, slight pitch wobble (worn tape): d swings between 0 and 0.03 beats every 2 beats
        for (int i = 1; i <= 32; ++i)
        {
            const auto x = i * 0.125;
            wobble.line (x, delayLevel (0.015 * (1.0 - std::cos (6.283185307179586 * x / 2.0))));
        }
        fill (time (22), "Lo-fi wobble", wobble);
    }
    {
        Shape mix (1.0f); // beats 1–2 as they come, beat 3 stutters in eighths, beat 4 backwards
        for (double c = 0.125; c < 1.0 - 1.0e-9; c += 0.125)
            mix.step (2.0 + c, delayLevel (c)); // the first eighth of beat 3, again and again
        mix.step (3.0, 1.0f).line (4.0, delayLevel (2.0)).step (4.0, 1.0f);
        fill (time (23), "Stutter into reverse", mix);
    }
    {
        Shape roll (1.0f); // beats 1–3 as they come, beat 4 in twelfths (a triplet roll)
        roll.step (3.0, 1.0f);
        for (int k = 1; k < 12; ++k)
            roll.step (3.0 + k / 12.0, delayLevel (k / 12.0));
        roll.step (4.0, 1.0f);
        fill (time (24), "Triplet roll", roll);
    }
    fill (time (25), "Half speed last beat", Shape (1.0f).step (3.0, 1.0f).line (4.0, delayLevel (0.5)).step (4.0, 1.0f));

    // volume: slot 1 keeps the level
    fill (volume (1), "Gate 1 beat", gate (1.0, 0.5));
    fill (volume (2), "Gate 1/2 beat", gate (0.5, 0.25));
    fill (volume (3), "Gate 1/4 beat", gate (0.25, 0.125));
    fill (volume (4), "Gate 1/8 beat", gate (0.125, 0.0625));
    fill (volume (5), "Gate triplet", gate (1.0 / 3.0, 1.0 / 6.0));
    fill (volume (6), "Off-beat", gate (1.0, 0.5, 0.5));
    fill (volume (7), "Swing gate", gate (1.0, 2.0 / 3.0));
    fill (volume (8), "Dotted gate", gate (0.75, 0.375));
    {
        Shape pump (0.0f); // each beat: down at the beat, back up quickly (a sidechain-style pump)
        for (int b = 0; b < 4; ++b)
        {
            pump.line (b + 0.75, 1.0f, 0.35f);
            pump.step (b + 1.0, 0.0f);
        }
        fill (volume (9), "Pump", pump);
    }
    {
        Shape pump (0.0f);
        for (int h = 0; h < 8; ++h)
        {
            pump.line (h * 0.5 + 0.375, 1.0f, 0.35f);
            pump.step (h * 0.5 + 0.5, 0.0f);
        }
        fill (volume (10), "Pump 1/2 beat", pump);
    }
    fill (volume (11), "Trance gate A", pattern ("1101101101101011"));
    fill (volume (12), "Trance gate B", pattern ("1011011010110110"));
    fill (volume (13), "Trance gate C", pattern ("1110111011101010"));
    fill (volume (14), "Fade in", Shape (0.0f).line (4.0, 1.0f, -0.3f).step (4.0, 0.0f));
    fill (volume (15), "Fade out", Shape (1.0f).line (4.0, 0.0f, 0.3f).step (4.0, 1.0f));
    fill (volume (16), "Dip and back", Shape (1.0f).line (2.0, 0.0f).line (4.0, 1.0f));
    {
        Shape trem (1.0f), fast (1.0f);
        for (int i = 0; i < 8; ++i)
            trem.line (i * 0.5 + 0.25, 0.35f, 0.4f).line (i * 0.5 + 0.5, 1.0f, -0.4f);
        for (int i = 0; i < 16; ++i)
            fast.line (i * 0.25 + 0.125, 0.35f, 0.4f).line (i * 0.25 + 0.25, 1.0f, -0.4f);
        fill (volume (17), "Tremolo", trem);
        fill (volume (18), "Tremolo fast", fast);
    }
    {
        Shape build (1.0f); // beat 1 in halves, beat 2 in quarters, beat 3 in eighths, beat 4 in sixteenths of a beat
        for (int b = 0; b < 4; ++b)
        {
            const auto period = 0.5 / (double) (1 << b);
            for (double start = b; start < b + 1.0 - 1.0e-9; start += period)
                build.step (start + period * 0.5, 0.0f).step (start + period, 1.0f);
        }
        fill (volume (19), "Build-up gate", build);
    }
    fill (volume (20), "Stutter first beat", Shape (1.0f).step (0.125, 0.0f).step (0.25, 1.0f).step (0.375, 0.0f).step (0.5, 1.0f).step (0.625, 0.0f)
                                                     .step (0.75, 1.0f).step (0.875, 0.0f).step (1.0, 1.0f).step (4.0, 1.0f));
    fill (volume (21), "Mute last 2 beats", Shape (1.0f).step (2.0, 0.0f).step (4.0, 1.0f));
    fill (volume (22), "Mute", Shape (0.0f).step (4.0, 0.0f));
    {
        Shape hard (0.0f), soft (0.3f), half (0.0f); // round 168m: sidechain-style ducking (a kick on every beat)
        for (int b = 0; b < 4; ++b)
        {
            hard.line (b + 0.3, 1.0f, 0.45f).step (b + 1.0, 0.0f);
            soft.line (b + 0.7, 1.0f, 0.3f).step (b + 1.0, 0.3f);
        }
        for (int h = 0; h < 8; ++h)
            half.line (h * 0.5 + 0.2, 1.0f, 0.45f).step (h * 0.5 + 0.5, 0.0f);
        fill (volume (23), "Sidechain hard", hard);
        fill (volume (24), "Sidechain soft", soft);
        fill (volume (25), "Sidechain 1/2 beat", half);
    }
    fill (volume (26), "Gate 3-3-2", pattern ("1101101011011010"));
    {
        Shape swell (0.05f), slow (0.0f); // each beat (or half bar) rises from silence, like a reversed hit
        for (int b = 0; b < 4; ++b)
            swell.line (b + 1.0, 1.0f, -0.45f).step (b + 1.0, 0.05f);
        slow.line (2.0, 1.0f, -0.4f).step (2.0, 0.0f).line (4.0, 1.0f, -0.4f).step (4.0, 0.0f);
        fill (volume (27), "Reverse swell", swell);
        fill (volume (28), "Half-bar swell", slow);
    }
    fill (volume (29), "Mute last beat", Shape (1.0f).step (3.0, 0.0f).step (4.0, 1.0f));
    fill (volume (30), "Stutter gate 1/16", gate (0.0625, 0.03125));
    // round 190ee: LYNX's bank stands in for FL's Default preset, which a new FL instance loads (a version-4 state:
    // "Trigger slots from MIDI notes" on); kept as the version-8 bytes it always had
    s.version = 8;
    s.midiTrigger = true;
    return s;
}

std::vector<std::uint8_t> factoryContainer()
{
    static const auto bytes = pack (writeState (factoryState()), 96);
    return bytes;
}

State editableState (const std::vector<std::uint8_t>& container, int* ppq)
{
    std::vector<std::uint8_t> fl;
    int q = 96;
    if (unpack (container, fl, q))
        if (auto s = parseState (fl.data(), fl.size()))
        {
            if (ppq != nullptr)
                *ppq = q;
            return *s;
        }
    if (ppq != nullptr)
        *ppq = 96;
    return factoryState();
}

std::vector<std::uint8_t> withState (const std::vector<std::uint8_t>& container, const State& state)
{
    std::vector<std::uint8_t> fl;
    int q = 96;
    if (! unpack (container, fl, q))
        q = 96;
    return pack (writeState (state), q);
}

std::vector<EditPoint> editPoints (const Slot& slot)
{
    std::vector<EditPoint> out;
    double x = 0.0;
    for (const auto& p : slot.points)
    {
        x += p.dx;
        out.push_back ({ std::clamp (x, 0.0, barLength), p.y, p.tension, p.type });
    }
    return out;
}

void setPoints (Slot& slot, const std::vector<EditPoint>& points)
{
    slot.points.clear();
    double prev = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        auto x = std::clamp (points[i].x, prev, barLength);
        if (i == 0)
            x = 0.0;
        if (i + 1 == points.size() && points.size() > 1)
            x = barLength;
        Point p;
        p.dx = x - prev;
        p.y = std::clamp (points[i].y, 0.0f, 1.0f);
        p.tension = std::clamp (points[i].tension, -1.0f, 1.0f);
        p.type = points[i].type;
        slot.points.push_back (p);
        prev = x;
    }
    // the Δx add up to exactly one bar, so loading normalises nothing (a write / read round trip keeps every bit)
    if (slot.points.size() > 1)
        for (int pass = 0; pass < 4; ++pass)
        {
            double total = 0.0;
            for (const auto& p : slot.points)
                total += p.dx;
            if (total == barLength)
                break;
            slot.points.back().dx = std::max (0.0, slot.points.back().dx + (barLength - total));
        }
    finishSlot (slot);
}

int addPoint (std::vector<EditPoint>& points, double x, float y, std::uint8_t type, float tension)
{
    x = std::clamp (x, 0.0, barLength);
    std::size_t at = points.size();
    for (std::size_t i = 1; i < points.size(); ++i)
        if (x < points[i].x || i + 1 == points.size())
        {
            at = i;
            break;
        }
    if (points.empty())
        at = 0;
    points.insert (points.begin() + (std::ptrdiff_t) at, EditPoint { x, std::clamp (y, 0.0f, 1.0f), tension, type });
    return (int) at;
}

bool removePoint (std::vector<EditPoint>& points, int index)
{
    if (index <= 0 || index + 1 >= (int) points.size() || points.size() <= 2)
        return false;
    points.erase (points.begin() + index);
    return true;
}

void movePoint (std::vector<EditPoint>& points, int index, double x, float y)
{
    if (index < 0 || index >= (int) points.size())
        return;
    auto& p = points[(std::size_t) index];
    p.y = std::clamp (y, 0.0f, 1.0f);
    if (index == 0 || index + 1 == (int) points.size())
        return;
    p.x = std::clamp (x, points[(std::size_t) index - 1].x, points[(std::size_t) index + 1].x);
}

float levelAt (const Slot& slot, double beat)
{
    if (slot.points.empty())
        return 1.0f;
    Player player;
    player.reset (slot.points);
    auto inc = std::clamp (beat, 0.0, barLength) * player.rate;
    return (float) player.step (slot.points, inc, beat);
}

std::vector<float> renderSlot (const Slot& slot, int n)
{
    std::vector<float> out ((std::size_t) std::max (n, 0), 1.0f);
    if (slot.points.empty() || n <= 0)
        return out;
    Player player;
    player.reset (slot.points);
    const auto adv = barLength / (double) n;
    out[0] = (float) player.start;
    auto inc = 0.0;
    out[0] = (float) player.step (slot.points, inc, adv);
    inc = adv * player.rate;
    for (int i = 1; i < n; ++i)
        out[(std::size_t) i] = (float) player.step (slot.points, inc, adv);
    return out;
}

double snapStep (int selector) noexcept
{
    static constexpr int per[7] { 0, 16, 12, 8, 6, 4, 3 };
    return selector > 0 && selector < 7 ? 1.0 / (double) per[selector] : 0.0;
}

namespace
{
constexpr std::array<int, paramCount> rawMin { 0, 0, 0, 0, 0, 0, -256, 0 };
constexpr std::array<int, paramCount> rawMax { 35, 35, 256, 256, 10000, 10000, 256, 1 };
} // namespace

float paramFromRaw (int index, int raw) noexcept
{
    if (index < 0 || index >= paramCount)
        return 0.0f;
    const auto lo = rawMin[(std::size_t) index], hi = rawMax[(std::size_t) index];
    return (float) (std::clamp (raw, lo, hi) - lo) / (float) (hi - lo);
}

int rawFromParam (int index, float value) noexcept
{
    if (index < 0 || index >= paramCount)
        return 0;
    const auto lo = rawMin[(std::size_t) index], hi = rawMax[(std::size_t) index];
    const auto v = std::clamp ((double) value, 0.0, 1.0);
    return lo + (int) std::lround (v * (double) (hi - lo));
}

// ---------------------------------------------------------------------------------------------------------------
// engine

struct Engine::Impl
{
    State st;
    bool loaded = false;
    int ppq = 96, fs = 44100, maxBlock = 512;
    float tempo = 120.0f;
    std::array<int, 6> syncTicks {};

    // timing (beats per bar of samples etc.)
    double spb = 0.0, span = 0.0, step = 0.0;

    // slots: 0 time, 1 volume
    struct Kind
    {
        int current = -1, requested = 0, pendingIndex = 0, pendingTime = intMax;
        Player player;
        double attackComp = 0.0;
    };
    std::array<Kind, 2> kinds;

    // parameters / derived
    std::array<int, 9> P {};
    float tensionK = 0.0f, tensionInv = 0.0f, tensionLn = 0.0f;
    std::array<Follower, 2> volFollow;
    double attackSamples = 0.0;

    // play state
    bool enabled = false, resetPending = false;
    double hostTicks = 0.0;
    float lastL = 0.0f, lastR = 0.0f;

    // buffer (ring of stereo frames with 16 guard frames on each side) and the reader
    static constexpr int guard = 16;
    std::vector<float> ring; // 2 * (L + 2 guard)
    std::uint32_t L = 0, write = 0, dirty = 0; // dirty: frames written since the buffer was last cleared
    struct Reader
    {
        std::uint32_t base = 0;
        int taps = 8;
        double c80 = 0.0, cw = 0.0;       // π/taps, 2cos(π/taps)
        double cutoff = pi, cc = -2.0;    // per-call cutoff and 2cos(cutoff)
        Follower speed;                   // speed smoothing (high quality)

        void setTaps (int n) noexcept
        {
            if (taps == n)
                return;
            taps = n;
            c80 = pi / (double) n;
            cw = std::cos (c80) * 2.0;
        }
        void setCutoff (double speedRatio) noexcept
        {
            const auto a = std::fabs (speedRatio);
            if (a <= 1.0)
                cutoff = pi, cc = -2.0;
            else
                cutoff = pi / a, cc = std::cos (cutoff) * 2.0;
        }
    } reader, fadeReader;

    // 15-frame dry delay
    std::array<float, 30> delayBuf {};
    int delayPos = 0;

    // time path state
    double prevPos = 0.0;
    float clickOffL = 0.0f, clickOffR = 0.0f, prevOutL = 0.0f, prevOutR = 0.0f;
    float jumpLevel = 0.0f, clickDecay = 0.0f;
    int fadeLen = 44, fadeLeft = 0;
    double fadeSpan = 0.0;
    int quality = 8;

    // hold ("repeat") state
    int holdMask = 0, holdTime = intMax, holdSync = 5, holdBase = 0, savedBase = 0;
    bool frozen = false;

    // DC blocker (option byte 4)
    ext dcS[2] {}, dcV[2] {};
    float dcC2 = 0.0f, dcC1 = 0.96f;

    // scratch
    std::vector<double> pos, frac, posCopy, wide, sines;
    std::vector<std::int64_t> ipos;
    std::vector<float> gain, spd, timeL, timeR, oldL, oldR, dryL, dryR;
    std::vector<double> cutoffs, ccs;

    const Slot& slotOf (int kind, int index) const noexcept
    {
        return st.slots[(std::size_t) (kind * slotsPerKind + std::clamp (index, 0, slotsPerKind - 1))];
    }
    const Slot& currentSlot (int kind) const noexcept { return slotOf (kind, std::max (0, kinds[(std::size_t) kind].current)); }

    int hostPos() const noexcept { return (int) roundEven (hostTicks); }

    // ---- set-up ------------------------------------------------------------------------------------------------
    void timing()
    {
        spb = (double) ((float) (fs * 240) / tempo);
        const auto oldSpan = span;
        span = spb * 2.0;
        step = 4.0 / spb;
        // read base relative to the write head: two bars back plus the 15-frame latency
        auto d = (std::int64_t) roundEven (span + 0.5) + 15;
        if (d < 16)
            d = 15;
        if (d > (std::int64_t) L - 15)
            d = (std::int64_t) L - 15;
        const auto newBase = (std::uint32_t) ((std::int64_t) L + (((std::int64_t) write - d) + 2 * (std::int64_t) L) % (std::int64_t) L);
        if (enabled && oldSpan > 0.0 && std::fabs (span - oldSpan) > 8.0)
        {
            fadeReader.base = reader.base % L + L;
            fadeSpan = oldSpan;
            fadeLeft = fadeLen;
        }
        reader.base = newBase;
        savedBase = (int) newBase;
    }

    void setFollowers()
    {
        const auto ms = [] (int raw) { return (double) (float) (std::exp ((double) ((float) raw * 9.99999975e-05f) * std::log (501.0)) - 1.0); };
        const auto inv = 1.0 / std::sqrt (2.0);
        attackSamples = ms (P[4]) * 0.001 * (double) fs;
        const auto comp = attackSamples * (double) (std::int8_t) st.options[5];
        kinds[0].attackComp = kinds[1].attackComp = comp;
        const auto rel = ms (P[5]) * 0.001 * (double) fs * 2.0;
        for (auto& f : volFollow)
            f.set (attackSamples * inv, rel * inv);
    }

    void setTension()
    {
        const auto mag = (double) (float) std::fabs ((double) ((float) P[6] * 0.00390625f));
        auto k = (float) (std::exp (mag * std::log (1001.0)) - 1.0);
        k *= (float) (P[6] == 0 ? 0 : (P[6] < 0 ? -1 : 1));
        tensionK = 0.0f;
        tensionCurve (k, tensionK, tensionInv, tensionLn);
    }

    void prepare (int sampleRate, int block)
    {
        fs = sampleRate;
        maxBlock = std::max (block, 16);
        // ring: three bars at 10 BPM plus margins (FL's size, independent of the block)
        const auto d = (double) ((float) (fs * 240) / 10.0f);
        const auto size = (std::int64_t) roundEven (d * 2.0 + d) + 16;
        L = (std::uint32_t) (size + 32 + 8192);
        if (ring.size() != (std::size_t) (L + 2 * guard) * 2)
            ring.assign ((std::size_t) (L + 2 * guard) * 2, 0.0f);
        else
            std::fill (ring.begin(), ring.end(), 0.0f);
        write = (std::uint32_t) (size + 16);
        dirty = 0;
        const auto cap = (std::size_t) maxBlock + 8;
        for (auto* v : { &pos, &frac, &posCopy, &cutoffs, &ccs })
            v->assign (cap, 0.0);
        wide.assign (cap * 4, 0.0);
        sines.assign (cap * 4, 0.0);
        ipos.assign (cap, 0);
        for (auto* v : { &gain, &spd, &timeL, &timeR, &oldL, &oldR, &dryL, &dryR })
            v->assign (cap, 0.0f);
        reader.taps = 0;
        reader.setTaps (8);
        fadeReader.taps = 0;
        fadeReader.setTaps (16);
        reader.setCutoff (1.0);
        fadeReader.setCutoff (1.0);
        fadeLen = (int) std::max<std::int64_t> (roundEven ((double) fs * 0.001), 1);
        jumpLevel = (float) ((double) fs * 0.002);
        const auto opt = std::min<int> (st.options[8], 2);
        static constexpr float clickMs[3] { 25.0f, 25.0f, 35.0f };
        static constexpr float speedMs[3] { 1.0f, 1.0f, 5.0f };
        clickDecay = (float) std::exp (std::log (0.01) / ((double) clickMs[opt] * 0.001 * (double) fs));
        const auto sm = (double) speedMs[opt] * 0.001 * (double) fs;
        reader.speed.set (sm, sm);
        dcC2 = (float) (44100.0 / (double) fs * 2.2834999999999997e-05);
        span = 0.0;
        timing();
        setFollowers();
        quality = st.options[6] != 0 ? 16 : 8;
    }

    // beats into the bar of a kind's player (to keep the place when its slot's points change)
    static double playerBeats (const Player& pl, const std::vector<Point>& p) noexcept
    {
        double x = 0.0;
        const auto idx = std::clamp (pl.idx, 0, (int) p.size() - 1);
        for (int i = 0; i < idx; ++i)
            x += p[(std::size_t) i].dx;
        const auto ph = std::clamp (pl.phase, 0.0, 1.0);
        return x + ph * p[(std::size_t) idx].dx;
    }

    bool update (const State& s) noexcept
    {
        for (const auto& slot : s.slots)
            if (slot.points.empty() || slot.points.size() > (std::size_t) maxEditPoints)
                return false;
        std::array<double, 2> at {};
        for (int k = 0; k < 2; ++k)
            if (kinds[(std::size_t) k].current >= 0)
                at[(std::size_t) k] = playerBeats (kinds[(std::size_t) k].player, currentSlot (k).points);
        for (std::size_t i = 0; i < st.slots.size(); ++i)
        {
            auto& d = st.slots[i];
            const auto& from = s.slots[i];
            if (d.points.capacity() < from.points.size())
                return false; // cannot happen after load(): every slot keeps maxEditPoints
            d.points.assign (from.points.begin(), from.points.end());
            d.triggerSync = from.triggerSync;
            d.positionSync = from.positionSync;
            d.hold = from.hold;
            d.link = from.link;
            d.keyHeld = from.keyHeld;
            d.oneShot = from.oneShot;
            d.flat = from.flat;
        }
        const auto oldOptions = st.options;
        st.options = s.options;
        if (oldOptions[5] != st.options[5])
            setFollowers();
        quality = st.options[6] != 0 ? 16 : 8;
        for (int k = 0; k < 2; ++k)
        {
            auto& K = kinds[(std::size_t) k];
            if (K.current < 0)
                continue;
            const auto& pts = currentSlot (k).points;
            K.player.reset (pts);
            K.player.advance (pts, at[(std::size_t) k]);
        }
        return true;
    }

    void load (const State& s, int q)
    {
        st = s;
        for (auto& slot : st.slots)
            slot.points.reserve ((std::size_t) std::max<int> (maxEditPoints, (int) slot.points.size()));
        ppq = q > 0 ? q : 96;
        syncTicks = { 0, ppq / 4, ppq / 2, ppq, ppq * 2, ppq * 4 };
        P = s.params;
        for (int k = 0; k < 2; ++k)
            kinds[(std::size_t) k].requested = std::clamp (P[(std::size_t) k], 0, slotsPerKind - 1);
        holdMask = 0; // FL writes the stored value before ProcessParam, so a stored hold does not engage by itself
        setTension();
        loaded = true;
    }

    // ---- slots -------------------------------------------------------------------------------------------------
    int syncTime (int combo) const noexcept
    {
        if (combo <= 0 || combo > 5)
            return intMax;
        const auto period = syncTicks[(std::size_t) combo];
        return ((hostPos() + period - 1) / period) * period;
    }

    void switchSlot (int kind, int index, bool cascade)
    {
        auto& K = kinds[(std::size_t) kind];
        const auto oldLink = K.current >= 0 && currentSlot (kind).link;
        if (kind == 1 && kinds[0].current >= 0 && currentSlot (0).link)
        {
            index = kinds[0].current;
            K.pendingTime = intMax;
        }
        K.current = std::clamp (index, 0, slotsPerKind - 1);
        const auto& slot = currentSlot (kind);
        K.player.reset (slot.points);
        const auto bit = 2 << kind;
        if (slot.hold < 1)
        {
            const auto before = holdMask;
            holdMask &= ~bit;
            if (before > 0 && holdMask == 0)
                holdTime = holdNow;
        }
        else
        {
            holdMask |= bit;
            if (holdTime == intMax)
                holdTime = slot.hold == 1 ? holdNow : hostPos() + syncTicks[(std::size_t) holdSync];
        }
        double phase = 0.0;
        if (slot.positionSync > 0 && slot.positionSync <= 5)
        {
            const auto period = syncTicks[(std::size_t) slot.positionSync];
            const auto p = (double) hostPos();
            auto t = p;
            if (holdMask > 0 && holdTime != intMax)
            {
                t = 0.0;
                if (holdTime != holdNow)
                    t = (p + (double) (ppq << 8)) - (double) holdTime;
            }
            if (0.0 < t)
            {
                const auto whole = (std::int64_t) (t / (double) period);
                phase = (t - (double) (whole * period)) / (double) ppq;
            }
        }
        K.player.advance (slot.points, phase + K.attackComp * step);
        if (slot.oneShot)
        {
            K.pendingIndex = 0;
            K.pendingTime = hostPos() + syncTicks[5];
        }
        if (cascade && (slot.link || oldLink))
            switchSlot (1, kinds[1].requested, false);
    }

    void requestSlot (int kind, int index)
    {
        auto& K = kinds[(std::size_t) kind];
        index = std::clamp (index, 0, slotsPerKind - 1);
        if (K.requested == index)
            return;
        K.requested = index;
        if (! enabled)
            return;
        if (kind == 1 && kinds[0].current >= 0 && currentSlot (0).link)
            return;
        const auto t = syncTime (slotOf (kind, index).triggerSync);
        K.pendingTime = t;
        if (t == intMax)
            switchSlot (kind, index, true);
        else
            K.pendingIndex = K.requested;
    }

    void resync()
    {
        if (frozen)
            reader.base = (std::uint32_t) savedBase;
        frozen = false;
        holdTime = intMax;
        if (holdMask > 0)
            holdTime = hostPos() + syncTicks[(std::size_t) holdSync];
        kinds[0].pendingTime = kinds[1].pendingTime = intMax;
        switchSlot (0, kinds[0].requested, true);
        switchSlot (1, kinds[1].requested, true);
    }

    void start (double ticks)
    {
        hostTicks = ticks;
        if (enabled)
            return;
        enabled = true;
        dcS[0] = dcS[1] = dcV[0] = dcV[1] = 0.0L;
        clearRing();
        delayBuf.fill (0.0f);
        resetPending = true;
        prevPos = 0.0;
        reader.speed.state = 1.0;
        clickOffL = clickOffR = prevOutL = prevOutR = 0.0f;
        lastL = lastR = 0.0f;
        fadeLeft = 0;
        resync();
    }

    void stop()
    {
        if (! enabled)
            return;
        enabled = false;
        clearRing();
        fadeLeft = 0;
        holdMask &= 1;
        holdTime = intMax;
        for (auto& K : kinds)
            K.current = -1, K.pendingTime = intMax;
    }

    void setParam (int index, int raw)
    {
        if (index < 0 || index > 8)
            return;
        const auto old = P[(std::size_t) index];
        P[(std::size_t) index] = raw;
        if (index < 2)
            requestSlot (index, raw);
        else if (index == 4 || index == 5)
            setFollowers();
        else if (index == 6)
            setTension();
        else if (index == 7)
        {
            if ((0 < old) != (0 < raw))
            {
                holdMask = (holdMask & ~1) | (raw > 0 ? 1 : 0);
                if (holdTime == intMax)
                    holdTime = syncTime (holdSync);
            }
        }
    }

    // ---- buffer --------------------------------------------------------------------------------------------------
    float* frame (std::uint32_t i) noexcept { return ring.data() + 2 * ((std::size_t) i + guard); }

    // the plugin zeroes the whole buffer; only the part written since the last clear can be non-zero
    void clearRing() noexcept
    {
        if (ring.empty())
            return;
        if (dirty >= L)
            std::fill (ring.begin(), ring.end(), 0.0f);
        else if (dirty > 0)
        {
            const auto first = (write + L - dirty) % L;
            const auto tail = std::min (dirty, L - first);
            std::fill (frame (first), frame (first) + 2 * (std::size_t) tail, 0.0f);
            std::fill (frame (0), frame (0) + 2 * (std::size_t) (dirty - tail), 0.0f);
            std::fill (ring.begin(), ring.begin() + 2 * guard, 0.0f);
            std::fill (ring.end() - 2 * guard, ring.end(), 0.0f);
        }
        dirty = 0;
    }

    void writeRing (const float* l, const float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            auto* f = frame (write);
            f[0] = l[i], f[1] = r[i];
            if (++write >= L)
                write = 0;
        }
        dirty = std::min (L, dirty + (std::uint32_t) n);
        // guard frames: the first frames repeated after the end, the last ones before the start
        std::memcpy (frame (L), frame (0), sizeof (float) * 2 * guard);
        std::memcpy (ring.data(), frame (L - guard), sizeof (float) * 2 * guard);
    }

    // windowed-sinc read of one frame at ring index `idx` + `fr` (0 ≤ fr < 1) with the reader's taps and cutoff
    void kernel (const Reader& R, const float* at, double fr, float& outL, float& outR) noexcept
    {
        fr = fr < 1e-25 ? 1e-25 : fr;
        float A[4], B[4], X[4];
        readerSetup (fr, R.c80, R.cutoff, R.cw, R.cc, A, B, X);
        const float C[4] { (float) R.cc, (float) R.cc, (float) R.cw, (float) R.cw };
#if LYNX_GB_SSE
        __m128 a = _mm_loadu_ps (A), b = _mm_loadu_ps (B), xx = _mm_loadu_ps (X), c = _mm_loadu_ps (C);
        const __m128 w1 = _mm_setr_ps (0.36f, 0.36f, 0.0f, 0.0f), wq = _mm_setr_ps (0.64f, 0.64f, 1.0f, 1.0f);
        const __m128 s = _mm_setr_ps (-twoPiF, twoPiF, -twoPiF, twoPiF);
        __m128 acc = _mm_setzero_ps(), h = _mm_setzero_ps();
        const float* fwd = at + 2;
        const float* bwd = at - 2;
        for (int k = 0; k < R.taps / 2; ++k)
        {
            auto r = _mm_rcp_ps (xx);
            const auto t = _mm_mul_ps (_mm_mul_ps (xx, r), r);
            auto a0 = a;
            a = b;
            b = _mm_sub_ps (_mm_mul_ps (b, c), a0);
            r = _mm_sub_ps (_mm_add_ps (r, r), t);
            h = _mm_movehl_ps (h, a0);
            auto win = _mm_add_ps (_mm_add_ps (_mm_mul_ps (_mm_mul_ps (wq, h), h), h), w1);
            xx = _mm_add_ps (xx, s);
            auto q = _mm_mul_ps (_mm_mul_ps (a0, r), win);
            const auto rhi = _mm_movehl_ps (r, r);
            const auto f14 = _mm_loadu_ps (fwd), f15 = _mm_loadu_ps (bwd);
            q = _mm_shuffle_ps (q, q, 0x50);
            const auto F1 = _mm_shuffle_ps (f14, f15, 0xe4), F2 = _mm_shuffle_ps (f14, f15, 0x4e);
            acc = _mm_add_ps (acc, _mm_mul_ps (q, F1));
            h = _mm_movehl_ps (h, a);
            a0 = a;
            a = b;
            b = _mm_sub_ps (_mm_mul_ps (b, c), a0);
            win = _mm_add_ps (_mm_add_ps (_mm_mul_ps (_mm_mul_ps (wq, h), h), h), w1);
            q = _mm_mul_ps (_mm_mul_ps (a0, rhi), win);
            q = _mm_shuffle_ps (q, q, 0x50);
            acc = _mm_add_ps (acc, _mm_mul_ps (q, F2));
            fwd += 4;
            bwd -= 4;
        }
        const auto sum = _mm_mul_ps (_mm_add_ps (_mm_movehl_ps (acc, acc), acc), _mm_setr_ps (0.5f, 0.5f, 1.0f, 1.0f));
        float o[4];
        _mm_storeu_ps (o, sum);
        outL = o[0], outR = o[1];
#else
        float a[4], b[4], xx[4], acc[4] {}, h0 = 0.0f, h1 = 0.0f;
        std::memcpy (a, A, 16), std::memcpy (b, B, 16), std::memcpy (xx, X, 16);
        const float s[4] { -twoPiF, twoPiF, -twoPiF, twoPiF };
        const float* fwd = at + 2;
        const float* bwd = at - 2;
        for (int k = 0; k < R.taps / 2; ++k)
        {
            float r[4], a0[4];
            for (int j = 0; j < 4; ++j)
            {
                const auto rr = 1.0f / xx[j];
                r[j] = (rr + rr) - (xx[j] * rr) * rr;
                a0[j] = a[j];
                a[j] = b[j];
                b[j] = b[j] * C[j] - a0[j];
                xx[j] += s[j];
            }
            h0 = a0[2], h1 = a0[3];
            auto q0 = (a0[0] * r[0]) * (((0.64f * h0) * h0 + h0) + 0.36f);
            auto q1 = (a0[1] * r[1]) * (((0.64f * h1) * h1 + h1) + 0.36f);
            acc[0] += q0 * fwd[0], acc[1] += q0 * fwd[1], acc[2] += q1 * bwd[2], acc[3] += q1 * bwd[3];
            float a1[4];
            for (int j = 0; j < 4; ++j)
            {
                a1[j] = a[j];
                a[j] = b[j];
                b[j] = b[j] * C[j] - a1[j];
            }
            h0 = a1[2], h1 = a1[3];
            q0 = (a1[0] * r[2]) * (((0.64f * h0) * h0 + h0) + 0.36f);
            q1 = (a1[1] * r[3]) * (((0.64f * h1) * h1 + h1) + 0.36f);
            acc[0] += q0 * fwd[2], acc[1] += q0 * fwd[3], acc[2] += q1 * bwd[0], acc[3] += q1 * bwd[1];
            fwd += 4;
            bwd -= 4;
        }
        outL = (acc[2] + acc[0]) * 0.5f;
        outR = (acc[3] + acc[1]) * 0.5f;
#endif
    }

    // positions (samples, relative to the reader's moving base) → output frames
    void read (Reader& R, const double* p, float* outL, float* outR, int n, bool perSampleCutoff) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto ip = (std::int64_t) p[i];
            const auto fr = p[i] - (double) ip;
            const auto idx = (std::uint32_t) (((std::uint64_t) R.base + (std::uint64_t) ip) % (std::uint64_t) L);
            ++R.base;
            if (perSampleCutoff)
                R.cutoff = cutoffs[(std::size_t) i], R.cc = ccs[(std::size_t) i];
            kernel (R, frame (idx), fr, outL[i], outR[i]);
        }
    }

    // ---- block ---------------------------------------------------------------------------------------------------
    void process (float* l, float* r, int n, double ticks) noexcept
    {
        hostTicks = ticks;
        if (! enabled)
        {
            if (lastL != 0.0f || lastR != 0.0f)
            {
                const auto last = (double) std::max (n - 1, 1);
                for (int i = 0; i < n; ++i)
                {
                    const auto w = (float) ((std::cos ((double) i * pi / last) + 1.0) * 0.5);
                    l[i] = (float) ((double) l[i] * (1.0 - (double) w) + (double) (lastL * w));
                    r[i] = (float) ((double) r[i] * (1.0 - (double) w) + (double) (lastR * w));
                }
                lastL = lastR = 0.0f;
            }
            return;
        }
        while (n > maxBlock)
        {
            block (l, r, maxBlock);
            l += maxBlock, r += maxBlock, n -= maxBlock;
            hostTicks += (double) maxBlock * (double) tempo / 60.0 * (double) ppq / (double) fs;
        }
        block (l, r, n);
    }

    void block (float* l, float* r, int n) noexcept
    {
        if (n <= 0)
            return;
        const auto now = (double) hostPos();
        for (int k = 0; k < 2; ++k)
            if ((double) kinds[(std::size_t) k].pendingTime <= now)
            {
                kinds[(std::size_t) k].pendingTime = intMax;
                switchSlot (k, kinds[(std::size_t) k].pendingIndex, true);
            }

        // hold / repeat
        bool holdStart = false;
        const auto declick = st.options[8] != 0;
        if ((double) holdTime <= now)
        {
            if (holdMask < 1)
            {
                if (frozen)
                {
                    frozen = false;
                    reader.base = (std::uint32_t) savedBase;
                    holdTime = intMax;
                    holdStart = declick;
                }
            }
            else
            {
                if (! frozen)
                {
                    savedBase = (int) reader.base;
                    const auto len = (int) roundEven (spb * (double) syncTicks[(std::size_t) holdSync] / (double) (ppq << 2));
                    holdBase = (int) (reader.base + L) - len;
                    frozen = true;
                }
                reader.base = (std::uint32_t) holdBase;
                holdTime = (int) roundEven (now) + syncTicks[(std::size_t) holdSync];
                holdStart = declick;
            }
        }

        // the dry path is delayed by 15 frames; the input also feeds the buffer
        if (! frozen)
            writeRing (l, r, n);
        for (int i = 0; i < n; ++i)
        {
            auto* d = delayBuf.data() + 2 * delayPos;
            dryL[(std::size_t) i] = d[0], dryR[(std::size_t) i] = d[1];
            d[0] = l[i], d[1] = r[i];
            if (++delayPos >= 15)
                delayPos = 0;
        }
        reader.base = reader.base % L + L;

        const auto& tslot = currentSlot (0);
        const bool timeOn = (! tslot.flat && P[2] != 0) || frozen || clickOffL != 0.0f || clickOffR != 0.0f || holdStart;
        float* outL = dryL.data();
        float* outR = dryR.data();
        if (timeOn)
        {
            auto& K = kinds[0];
            K.player.render (tslot.points, pos.data(), n, step);
            const auto fading = fadeLeft > 0;
            if (fading)
                std::copy (pos.begin(), pos.begin() + n, posCopy.begin());
            for (int i = 0; i < n; ++i)
                pos[(std::size_t) i] *= span;
            if (resetPending)
                prevPos = pos[0];
            bool perSample = false;
            int taps = 8;
            float* dspeed = spd.data();
            if (quality < 9)
            {
                if (declick)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        const auto pv = pos[(std::size_t) i];
                        dspeed[i] = (float) (pv - prevPos);
                        prevPos = pv;
                    }
                    for (int i = 0; i < n; ++i)
                        dspeed[i] = std::fabs (dspeed[i] + 1.0f);
                }
                prevPos = pos[(std::size_t) n - 1];
                reader.setCutoff (1.0);
            }
            else
            {
                for (int i = 0; i < n; ++i)
                {
                    const auto pv = pos[(std::size_t) i];
                    gain[(std::size_t) i] = (float) (pv - prevPos);
                    prevPos = pv;
                }
                for (int i = 0; i < n; ++i)
                    gain[(std::size_t) i] = std::fabs (gain[(std::size_t) i] + 1.0f);
                if (declick)
                    std::copy (gain.begin(), gain.begin() + n, spd.begin());
                reader.speed.run (gain.data(), n);
                auto mn = gain[0], mx = gain[0];
                for (int i = 1; i < n; ++i)
                    mn = std::min (mn, gain[(std::size_t) i]), mx = std::max (mx, gain[(std::size_t) i]);
                const auto eps = 5.96046448e-08f;
                perSample = eps + 1.0f < mx;
                if (! perSample)
                {
                    reader.setCutoff (1.0);
                    taps = 1.0f - eps <= mn ? 8 : 16;
                }
                else
                {
                    taps = 16;
                    for (int i = 0; i < n; ++i)
                    {
                        const auto sp = (double) std::max (gain[(std::size_t) i], 1.0f);
                        cutoffs[(std::size_t) i] = pi / sp;
                        ccs[(std::size_t) i] = std::cos (cutoffs[(std::size_t) i]) * 2.0;
                    }
                }
            }
            reader.setTaps (taps);
            read (reader, pos.data(), timeL.data(), timeR.data(), n, perSample);
            if (fading)
            {
                for (int i = 0; i < n; ++i)
                    posCopy[(std::size_t) i] *= fadeSpan;
                read (fadeReader, posCopy.data(), oldL.data(), oldR.data(), n, perSample);
                const auto len = std::min (fadeLen, n);
                for (int i = 0; i < n; ++i)
                {
                    auto w = (double) i / (double) len;
                    w = w < 0.0 ? 0.0 : (w > 1.0 ? 1.0 : w);
                    const auto o = 1.0 - w;
                    timeL[(std::size_t) i] = (float) ((double) timeL[(std::size_t) i] * w + (double) oldL[(std::size_t) i] * o);
                    timeR[(std::size_t) i] = (float) ((double) timeR[(std::size_t) i] * w + (double) oldR[(std::size_t) i] * o);
                }
                fadeLeft = 0;
            }
            if (declick)
            {
                if (holdStart)
                    dspeed[0] = jumpLevel * 2.0f;
                const auto lim = (std::int32_t) floatBits (jumpLevel);
                for (int i = 0; i < n; ++i)
                {
                    if (lim < (std::int32_t) floatBits (dspeed[i]))
                    {
                        clickOffL = (clickOffL + prevOutL) - timeL[(std::size_t) i];
                        clickOffR = (clickOffR + prevOutR) - timeR[(std::size_t) i];
                    }
                    prevOutL = timeL[(std::size_t) i];
                    prevOutR = timeR[(std::size_t) i];
                    timeL[(std::size_t) i] += clickOffL;
                    timeR[(std::size_t) i] += clickOffR;
                    clickOffL *= clickDecay;
                    clickOffR *= clickDecay;
                }
                const auto flush = [] (float& v) { if (floatBits (v) * 2u < floatBits (5.96046448e-08f) * 2u) v = 0.0f; };
                flush (clickOffL);
                flush (clickOffR);
            }
            if (P[2] != 256)
            {
                const auto m = (float) P[2] * 0.00390625f;
                for (int i = 0; i < n; ++i)
                {
                    timeL[(std::size_t) i] = timeL[(std::size_t) i] * m + dryL[(std::size_t) i] * (1.0f - m);
                    timeR[(std::size_t) i] = timeR[(std::size_t) i] * m + dryR[(std::size_t) i] * (1.0f - m);
                }
            }
            outL = timeL.data();
            outR = timeR.data();
        }
        else
        {
            kinds[0].player.advance (tslot.points, (double) n * step);
            prevPos = span;
            reader.base += (std::uint32_t) n;
            fadeLeft = 0;
        }

        // volume
        const auto& vslot = currentSlot (1);
        auto* g = gain.data();
        if (! vslot.flat && P[3] != 0)
        {
            kinds[1].player.render (vslot.points, g, n, step);
            if (tensionK != 0.0f)
            {
                const auto rising = (floatBits (tensionK) & 0x80000000u) == 0;
                for (int i = 0; i < n; ++i)
                {
                    auto x = g[i];
                    if (rising)
                        x = 1.0f - x;
                    x *= tensionLn;
                    x = exp11 (x) + -1.0f;
                    x *= tensionInv;
                    if (rising)
                        x = 1.0f - x;
                    g[i] = x;
                }
            }
            if (P[3] != 256)
            {
                const auto m = (float) P[3] * 0.00390625f;
                for (int i = 0; i < n; ++i)
                    g[i] = g[i] * m + (1.0f - m);
            }
        }
        else
        {
            kinds[1].player.advance (vslot.points, (double) n * step);
            std::fill (g, g + n, 1.0f);
        }
        if (resetPending)
            for (auto& f : volFollow)
                f.state = (double) g[0];
        for (auto& f : volFollow)
            f.run (g, n);
        for (int i = 0; i < n; ++i)
        {
            l[i] = g[i] * outL[i];
            r[i] = g[i] * outR[i];
        }
        if (st.options[4] != 0)
            for (int i = 0; i < n; ++i)
            {
                ext x = (ext) l[i];
                dcV[0] = dcV[0] + (x - dcS[0]) * (ext) dcC2;
                dcS[0] = dcS[0] + dcV[0];
                dcV[0] = dcV[0] * (ext) dcC1;
                l[i] = (float) (x - dcS[0]);
                x = (ext) r[i];
                dcV[1] = dcV[1] + (x - dcS[1]) * (ext) dcC2;
                dcS[1] = dcS[1] + dcV[1];
                dcV[1] = dcV[1] * (ext) dcC1;
                r[i] = (float) (x - dcS[1]);
            }
        if (st.options[4] != 0) // states kept as doubles between blocks, tiny ones flushed to zero
        {
            const auto keep = [] (ext v) { return (ext) (double) (std::fabs (v) <= (ext) 5.96046448e-08f ? 0.0L : v); };
            for (int c = 0; c < 2; ++c)
                dcV[c] = keep (dcV[c]), dcS[c] = keep (dcS[c]);
        }
        lastL = l[n - 1];
        lastR = r[n - 1];
        resetPending = false;
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

bool Engine::load (const State& state, int ppq)
{
    impl->load (state, ppq);
    return true;
}

bool Engine::update (const State& state) noexcept { return impl->loaded && impl->update (state); }

void Engine::prepare (int sampleRate, int maxBlock) { impl->prepare (sampleRate, maxBlock); }

void Engine::setTempo (float bpm)
{
    if (bpm <= 0.0f || bpm == impl->tempo)
        return;
    impl->tempo = bpm;
    impl->timing();
}

void Engine::setParam (int index, int raw) { impl->setParam (index, raw); }
void Engine::start (double ticks) { impl->start (ticks); }
void Engine::stop() { impl->stop(); }
bool Engine::playing() const noexcept { return impl->enabled; }

void Engine::jump (double ticks)
{
    impl->hostTicks = ticks;
    if (impl->enabled)
        impl->resync();
}

void Engine::process (float* left, float* right, int n, double ticks) noexcept { impl->process (left, right, n, ticks); }

// ---------------------------------------------------------------------------------------------------------------
// LYNX effect wrapper

std::optional<EffectSlot> slotFromFl (const std::vector<std::uint8_t>& flState, int ppq)
{
    const auto s = parseState (flState.data(), flState.size());
    if (! s)
        return std::nullopt;
    EffectSlot slot;
    slot.kind = EffectKind::grossBeat;
    for (int i = 0; i < paramCount; ++i)
        slot.params[(std::size_t) i] = paramFromRaw (i, s->params[(std::size_t) i]);
    slot.state = pack (flState, ppq);
    return slot;
}

std::vector<std::string> slotNames (const std::vector<std::uint8_t>& container, bool volume)
{
    std::vector<std::uint8_t> fl;
    int ppq = 96;
    std::vector<std::string> names;
    if (! container.empty() && (! unpack (container, fl, ppq) || ! parseState (fl.data(), fl.size())))
        return names;
    const auto s = editableState (container); // round 168m: an empty container shows LYNX's factory slots
    for (int i = 0; i < slotsPerKind; ++i)
        names.push_back (s.slots[(std::size_t) ((volume ? slotsPerKind : 0) + i)].name);
    return names;
}

namespace
{
class GrossBeatEffect final : public Effect
{
public:
    explicit GrossBeatEffect (const std::vector<std::uint8_t>& container)
    {
        // round 168m: an empty (new) or unreadable container plays LYNX's factory bank instead of passing the signal
        int ppq = 96;
        state = editableState (container, &ppq);
        flPpq = ppq;
        ok = true;
        engine.load (state, flPpq);
        raw = state.params;
        current = { raw[0], raw[1] };
        takeMidiSettings (state);
    }

    // round 190ee: a note on the effect's MIDI input port (FL: Options › "Trigger slots from MIDI notes"); applied at
    // the start of the next block (FL handles it on its editor's thread, not at the note's sample)
    void midiNote (int key, int velocity, int midiChannel) noexcept override
    {
        if (queued < (int) queue.size())
            queue[(std::size_t) queued++] = { key, velocity, midiChannel };
    }

    // the editor changed a slot (points, settings, a preset's bank): the new bank is parsed here, off the audio thread,
    // and copied into the running engine at the start of a later block (buffer and slot positions kept)
    bool acceptState (const std::vector<std::uint8_t>& bytes) noexcept override
    {
        try
        {
            std::vector<std::uint8_t> fl;
            int ppq = 96;
            std::optional<State> s;
            if (bytes.empty())
                s = factoryState();
            else if (unpack (bytes, fl, ppq))
                s = parseState (fl.data(), fl.size());
            if (! s || ppq != flPpq)
                return false;
            for (const auto& slot : s->slots)
                if (slot.points.empty() || slot.points.size() > (std::size_t) maxEditPoints)
                    return false;
            for (int wait = 0; incomingFull.load (std::memory_order_acquire); ++wait)
            {
                if (wait >= 30)
                    return false;
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
            incoming = std::move (*s);
            incomingFull.store (true, std::memory_order_release);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void prepare (double sr) noexcept override
    {
        sampleRate = (int) std::lround (sr);
        if (ok)
            engine.prepare (sampleRate, 4096);
        lastTicks = -1.0;
    }

    void reset() noexcept override
    {
        if (ok)
            engine.stop();
        lastTicks = -1.0;
    }

    void setSongPosition (double ticks) noexcept override { songTicks = ticks; }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (! ok)
            return;
        if (incomingFull.load (std::memory_order_acquire))
        {
            engine.update (incoming);
            takeMidiSettings (incoming);
            incomingFull.store (false, std::memory_order_release);
        }
        for (int i = 0; i < paramCount; ++i)
        {
            const auto v = rawFromParam (i, p[(std::size_t) i]);
            if (v != raw[(std::size_t) i])
            {
                raw[(std::size_t) i] = v;
                engine.setParam (i, v);
                if (i < 2)
                    current[(std::size_t) i] = v;
            }
        }
        // MIDI notes select slots as a click in the grids would (the editor's handler 0x848330); the slot's own
        // parameter keeps its value, so a later change of it still applies
        for (int e = 0; e < queued; ++e)
        {
            const auto& ev = queue[(std::size_t) e];
            if (! midiTrigger)
                continue;
            const auto [kind, slot] = midi.note (ev.velocity > 0, ev.channel, ev.key, current,
                                                 [this] (int k, int s) { return keyHeld[(std::size_t) (k * slotsPerKind + s)]; });
            if (kind >= 0)
            {
                current[(std::size_t) kind] = slot;
                engine.setParam (kind, slot);
            }
        }
        queued = 0;
        engine.setTempo ((float) bpm);
        const auto flTicks = songTicks / (double) ppq * (double) flPpq;
        // LYNX's transport: a moving song position is FL's "playing"; a position that does not follow the previous
        // block is a jump (FL re-syncs the slots)
        const auto moving = lastTicks < 0.0 || songTicks != lastTicks;
        if (! moving)
            engine.stop();
        else if (! engine.playing())
            engine.start (flTicks);
        else if (std::fabs (songTicks - expected) > 1.0)
            engine.jump (flTicks);
        lastTicks = songTicks;
        expected = songTicks + (double) n * bpm / 60.0 * (double) ppq / (double) sampleRate;
        engine.process (l, r, n, flTicks);
    }

private:
    void takeMidiSettings (const State& s) noexcept
    {
        midiTrigger = s.midiTrigger;
        for (std::size_t k = 0; k < keyHeld.size(); ++k)
            keyHeld[k] = s.slots[k].keyHeld;
    }

    struct Note
    {
        int key = 0, velocity = 0, channel = 0;
    };
    std::array<Note, 256> queue {};
    int queued = 0;
    MidiSlots midi;
    bool midiTrigger = true;
    std::array<bool, 2 * slotsPerKind> keyHeld {};
    std::array<int, 2> current {};

    State state, incoming;
    std::atomic<bool> incomingFull { false };
    Engine engine;
    bool ok = false;
    int flPpq = 96, sampleRate = 44100;
    std::array<int, 9> raw {};
    double songTicks = 0.0, lastTicks = -1.0, expected = 0.0;
};
} // namespace

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& container) { return std::make_unique<GrossBeatEffect> (container); }
} // namespace lynx::grossbeat
