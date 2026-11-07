#include "core/LovePhilter.h"

// Fruity Love Philter, exact engine (docs/analise/REA_rodada66.md; earlier decoding in rounds 16–20 and 27).
// Clean-room: written from the behaviour, formulas and data layouts described there; nothing of Image-Line's code or
// data is copied.
//
// Arithmetic follows the plugin operation by operation: float and double steps in their order with one rounding each
// (no contraction), the four-lane ramps of its vector kernels, and the x87 kernels (Lime filters, CENTER, waveshaper,
// pan law) in 80-bit long double under the plugin's control word.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#ifdef LP_DEBUG
#include <cstdio>
#endif

#include "core/DelphiMath.h"
#include "core/Inflate.h"
#include "core/Instruments.h"

#if defined(__clang__)
#pragma clang fp contract(off)
#pragma STDC FENV_ACCESS ON
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off", "rounding-math")
#endif

namespace lynx::philter
{
namespace
{
using ld = long double;
using u32 = std::uint32_t;

inline float F (double v) noexcept { return (float) v; }

// SSE conversions of the plugin's runtime: Round = round half to even (MXCSR default), Trunc = toward zero
inline long long rnd (double x) noexcept { return (long long) std::nearbyint (x); }
inline long long trunc64 (double x) noexcept { return (long long) x; }

// "x ≥ 1" as the plugin tests a phase: the high word of the double compared unsigned (a negative phase also passes)
inline bool phaseWrap (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return (u32) (b >> 32) > 0x3fefffffu;
}
inline bool hiBelowHalf (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return (u32) (b >> 32) < 0x3fe00000u;
}
inline std::int32_t bitsOf (float f) noexcept
{
    std::int32_t b;
    std::memcpy (&b, &f, 4);
    return b;
}

constexpr float kTiny = 5.960464477539063e-08f; // 2^-24, the plugin's denormal threshold
const float kLn11f = 2.39789534f;               // ln 11 as the runtime stores it (float)
const float kSqrt2f = 1.41421354f;              // √2 (float)
const double kLn2Half = 0.34657359027997264;    // ln 2 / 2
const double kPi = 3.1415926535897931;

// ------------------------------------------------------------------------------------------- x87 control word
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
struct X87Mode
{
    unsigned short saved = 0;
    explicit X87Mode (bool truncate) noexcept
    {
        asm volatile ("fnstcw %0" : "=m"(saved) : : "memory");
        unsigned short cw = (unsigned short) ((saved & ~0x0F00u) | 0x0300u | (truncate ? 0x0C00u : 0u));
        asm volatile ("fldcw %0" : : "m"(cw) : "memory");
    }
    ~X87Mode() { asm volatile ("fldcw %0" : : "m"(saved) : "memory"); }
    X87Mode (const X87Mode&) = delete;
    X87Mode& operator= (const X87Mode&) = delete;
};
inline long long x87Int (long double v) noexcept
{
    long long r;
    asm ("fistpll %0" : "=m"(r) : "t"(v) : "st");
    return r;
}
inline long double x87Cos (long double v) noexcept
{
    long double r;
    asm ("fcos" : "=t"(r) : "0"(v));
    return r;
}
inline long double x87Sin (long double v) noexcept
{
    long double r;
    asm ("fsin" : "=t"(r) : "0"(v));
    return r;
}
#else
struct X87Mode
{
    explicit X87Mode (bool) noexcept {}
};
inline long long x87Int (long double v) noexcept { return (long long) std::nearbyint (v); }
inline long double x87Cos (long double v) noexcept { return dmath::cos (v); }
inline long double x87Sin (long double v) noexcept { return dmath::sin (v); }
#endif

// x87 flush of the plugin's integrators: |x| ≤ 2^-24 → 0
inline ld flushX87 (ld v) noexcept { return std::fabs (v) <= (ld) kTiny ? 0.0L : v; }
// double state flush (x87, no rounding): |x| < 2^-24 → 0
inline double flushD (double v) noexcept { return std::abs (v) < (double) kTiny ? 0.0 : v; }
// float state flush (bit compare of |x| with 2^-24)
inline float flushF (float v) noexcept
{
    return ((u32) bitsOf (v) << 1) < ((u32) bitsOf (kTiny) << 1) ? 0.0f : v;
}

// FL's level law (11^x − 1)/10 with 1 kept exact, x = raw/128 (float), and its signed variant
float curve11 (int raw) noexcept
{
    const float x = (float) raw * 0.0078125f;
    if (x == 1.0f)
        return 1.0f;
    return F ((dmath::exp ((double) (x * kLn11f)) - 1.0) * (double) 0.1f);
}
float scurve11 (int raw) noexcept
{
    const float x = (float) raw * 0.0078125f;
    float a = std::abs (x);
    if (a != 1.0f)
        a = F ((dmath::exp ((double) (a * kLn11f)) - 1.0) * (double) 0.1f);
    const float s = x == 0.0f ? 0.0f : (std::signbit (x) ? -1.0f : 1.0f);
    return a * s;
}

// ------------------------------------------------------------------------------------------------- state
struct Pt
{
    float dt = 0, y = 0, t = 0;
    std::uint8_t mode = 0;
    // prepared (§ envelope points)
    int sign = 0; // 0 none, ±1 sign of the tension, 2 = straight line
    float k = 0, ilk = 0, L = 0, p4 = 0, p5 = 0, p6 = 0, w = 0;
};

struct Env
{
    int kind = 1, ver = 3;
    std::vector<Pt> pts;
    int flags = 0, on = 0, decay = -1, loop = -1, sus = -1;
};
} // namespace

struct Art
{
    float base = 0, envDepth = 1, lfoDepth = 0.5f;
    std::array<Env, 10> pat;
    Env lfo;
    std::array<int, 5> lfoKnobs { 40000, 0, 0, 0, 0 };
    std::array<Env, 3> maps;
};
struct Unit
{
    std::array<int, 12> p {};
    int mode = 1, type = 0;
    bool flat = false, enabled = false, wsOn = false;
    Env ws;
    int wsBip = 0;
    std::array<Art, 8> art;
};
struct State
{
    int version = 0;
    std::array<int, 6> g { 128, 128, 128, 4096, 8192, 1 };
    int modX = 512, modY = 512;
    int solo = -1, hq = 0, os = 1;
    bool smooth = true, center = false, syncPatterns = true, wsInterp = false;
    int use1 = 0;
    std::array<Unit, 8> u;
};

namespace
{
struct Reader
{
    const std::uint8_t* data;
    std::size_t size, pos = 0;
    bool failed = false;
    const std::uint8_t* raw (std::size_t n)
    {
        if (failed || n > size - pos)
        {
            failed = true;
            return nullptr;
        }
        const auto* p = data + pos;
        pos += n;
        return p;
    }
    template <typename T>
    T get()
    {
        T v {};
        if (const auto* p = raw (sizeof (T)))
            std::memcpy (&v, p, sizeof (T));
        return v;
    }
    std::int32_t i32() { return get<std::int32_t>(); }
    std::uint8_t u8() { return get<std::uint8_t>(); }
};

// TMEnvelope file record (the plugin's own loader): kind, version, n points, the points, five header ints
bool readEnv (Reader& r, Env& e)
{
    e.kind = r.i32();
    e.ver = r.i32();
    const auto n = r.i32();
    if (r.failed || n < 0 || n > 100000)
        return false;
    e.pts.assign ((std::size_t) n, Pt {});
    float prevY = 0.0f;
    for (auto& p : e.pts)
    {
        if (e.ver < 2)
        {
            if (e.kind == 6)
                p.dt = (float) r.get<double>();
            else
                p.dt = r.get<float>();
            p.y = r.get<float>();
        }
        else
        {
            p.dt = (float) r.get<double>();
            p.y = (float) r.get<double>();
        }
        if (std::isnan (p.y))
            p.y = prevY;
        prevY = p.y;
        p.t = r.get<float>();
        if (e.ver >= 1)
        {
            p.mode = r.u8();
            r.raw (3);
        }
    }
    e.flags = r.i32();
    e.on = r.i32();
    e.decay = r.i32();
    e.loop = r.i32();
    e.sus = r.i32();
    return ! r.failed;
}
} // namespace

std::shared_ptr<const State> parse (const std::vector<std::uint8_t>& blob)
{
    if (blob.size() < 8)
        return nullptr;
    std::uint32_t v = 0;
    std::memcpy (&v, blob.data(), 4);
    if (v == 0x7fffffffu || (std::int32_t) v >= 0xc0004)
        return nullptr;
    if (v > 0xffffu)
        v >>= 16;
    if (v < 1 || v > 12)
        return nullptr;
    const auto body = inflateZlib (blob.data() + 4, blob.size() - 4, false, 16u << 20);
    if (! body)
        return nullptr;
    auto s = std::make_shared<State>();
    s->version = (int) v;
    Reader r { body->data(), body->size() };
    for (auto& g : s->g)
        g = r.i32();
    s->g[5] = std::clamp (s->g[5], 1, 10);
    const std::size_t g2n = v >= 11 ? 0x21 : v == 10 ? 0x20 : 0x1f;
    const auto* g2 = r.raw (g2n);
    if (g2 == nullptr)
        return nullptr;
    auto gi = [g2] (int o) {
        std::int32_t x;
        std::memcpy (&x, g2 + o, 4);
        return x;
    };
    s->modX = gi (0);
    s->modY = gi (4);
    s->solo = gi (16);
    s->hq = gi (20);
    s->os = gi (24);
    s->smooth = g2[28] != 0;
    s->center = g2[29] != 0;
    s->syncPatterns = g2[30] != 0;
    s->wsInterp = false;
    if (v >= 2)
    {
        s->use1 = r.u8();
        r.i32();
    }
    if (v < 6)
    {
        const auto n = r.i32();
        if (n < 0)
            return nullptr;
        r.raw ((std::size_t) n);
    }
    else
    {
        std::size_t n = r.u8();
        if (n == 255)
        {
            const auto big = r.i32();
            if (big < 0)
                return nullptr;
            n = (std::size_t) big;
        }
        r.raw (2 * n);
    }
    for (auto& u : s->u)
    {
        for (auto& p : u.p)
            p = r.i32();
        u.mode = r.i32();
        u.type = r.i32();
        u.flat = r.u8() != 0;
        u.enabled = r.u8() != 0;
        u.wsOn = r.u8() != 0;
        if (! readEnv (r, u.ws))
            return nullptr;
        u.wsBip = r.u8();
        for (auto& a : u.art)
        {
            r.i32();
            a.base = r.get<float>();
            a.envDepth = r.get<float>();
            a.lfoDepth = r.get<float>();
            for (auto& e : a.pat)
            {
                if (! readEnv (r, e))
                    return nullptr;
                r.raw (16); // the knobs are not read for the pattern envelopes (factors stay 1, 1, 0, 1)
            }
            if (! readEnv (r, a.lfo))
                return nullptr;
            for (int k = 0; k < (a.lfo.ver >= 3 ? 5 : 4); ++k)
                a.lfoKnobs[(std::size_t) k] = r.i32();
            for (auto& m : a.maps)
                if (! readEnv (r, m))
                    return nullptr;
        }
    }
    if (v > 11)
        s->wsInterp = r.u8() != 0;
    if (r.failed)
        return nullptr;
    return s;
}

namespace
{
// ------------------------------------------------------------------------------- envelope curves (TMEnvelope)
struct Curve
{
    std::vector<Pt> pts;
    double offset = 0.0; // 0.5 for additive articulators (and the LFO's amplitude envelope)
    double scale = 1.0;  // time units per tick
    int flags = 0, on = 0, decay = -1, loop = -1, sus = -1, endpt = -1;
    bool zeroloop = false;

    static int stepCount (const Pt& p) noexcept
    {
        if (p.mode > 7 || ((0x78u >> p.mode) & 1u) == 0)
            return -1;
        const double a = (double) F (1.0 - std::abs ((double) p.t) * 0.6);
        auto n = (int) rnd ((double) F (dmath::exp (a * dmath::ln (1000001.0)) - 1.0) * 0.001);
        if (p.mode == 3 && p.t >= 0.0f)
            ++n;
        return n;
    }

    // `noDecay`: the decay point is dropped at load (pattern envelopes, LFO envelopes, maps); `map`: decay, loop and
    // sustain are dropped and the x axis is normalised to a length of 1 (TMMap)
    void build (const Env& e, double off, bool map)
    {
        pts = e.pts;
        offset = off;
        flags = e.flags;
        on = e.on;
        decay = -1;
        loop = map ? -1 : e.loop;
        sus = map ? -1 : e.sus;
        if (map)
            normalise();
        if (pts.empty())
            pts.push_back (Pt {});
        prepare();
    }

    void normalise()
    {
        if (pts.empty())
        {
            pts.push_back (Pt {});
            return;
        }
        if (pts[0].dt > 0.0f)
        {
            Pt p0;
            p0.y = pts[0].y;
            pts.insert (pts.begin(), p0);
        }
        double total = 0.0;
        for (const auto& p : pts)
            total += (double) p.dt;
        if (total > 0.0)
        {
            const double f = 1.0 / total;
            if (f != 1.0)
                for (auto& p : pts)
                    p.dt = (float) ((double) p.dt * f);
        }
        else
        {
            Pt p1;
            p1.dt = 1.0f;
            p1.y = pts.back().y;
            pts.push_back (p1);
        }
    }

    // curve mode 10 ("smooth"): slopes from the neighbouring points (the same envelope library as Fruity
    // WaveShaper's maps, REA_rodada51 §2.4)
    void prepareSmooth (int i) noexcept
    {
        constexpr double kEps = 5.960464477539063e-08;
        auto& p = pts[(std::size_t) i];
        const int n = (int) pts.size();
        const double h = (double) p.dt > kEps ? (double) p.dt : kEps;
        const double y1 = p.y;
        const int j = i - 1;
        const double y0 = j >= 0 ? (double) pts[(std::size_t) j].y : y1;
        double rp = 1.0, ypp = y0;
        if (j >= 0 && pts[(std::size_t) j].mode == 10)
        {
            rp = (double) pts[(std::size_t) j].dt / h;
            if (i - 2 >= 0)
                ypp = pts[(std::size_t) (i - 2)].y;
        }
        double rn = 1.0, yn = y1;
        if (i + 1 < n && pts[(std::size_t) (i + 1)].mode == 10)
        {
            rn = (double) pts[(std::size_t) (i + 1)].dt / h;
            yn = pts[(std::size_t) (i + 1)].y;
        }
        const double d = y1 - y0;
        if (std::abs (d) < kEps)
        {
            p.sign = 2;
            return;
        }
        const double a = (ypp - y0) / d, b = (yn - y0) / d;
        rp = rp > kEps ? rp : kEps;
        rn = rn > kEps ? rn : kEps;
        const double sr = (b - 1.0) / rn, sl = -a / rp;
        if (! (F (sr) * 1.0f > 0.0f))
            p.p4 = 0.0f;
        else
        {
            const float w1 = F (rn) * 2.0f + 1.0f, w2 = F (rn) + 2.0f;
            p.p4 = (F (sr) * 1.0f * (w1 + w2)) / (w1 * F (sr) + w2 * 1.0f);
        }
        if (! (1.0f * F (sl) > 0.0f))
            p.p5 = 0.0f;
        else
        {
            const float w1 = 2.0f + F (rp), w2 = 1.0f + F (rp) * 2.0f;
            p.p5 = (1.0f * F (sl) * (w1 + w2)) / (w1 * 1.0f + w2 * F (sl));
        }
        p.p6 = F (1.0 - std::abs ((double) p.t));
    }

    double time (int i) const noexcept
    {
        double s = 0.0;
        if (i >= 0 && i < (int) pts.size())
            for (int k = 0; k <= i; ++k)
                s += (double) pts[(std::size_t) k].dt;
        return s;
    }

    void prepare()
    {
        const int n = (int) pts.size();
        endpt = sus;
        if (endpt < 0 && loop >= 0)
            endpt = n - 1;
        const int D = std::max (decay, 0);
        const int S = sus >= 0 ? sus : n - 1;
        const int L = loop >= 0 ? loop : S;
        double A = 0, M = 0, R = 0;
        for (int i = 0; i < n; ++i)
        {
            auto& p = pts[(std::size_t) i];
            if (p.dt <= 0.0f)
                p.dt = 0.0f;
            if (i > D)
            {
                if (i <= L)
                    M += (double) p.dt;
            }
            else
                A += (double) p.dt;
            if (i > S)
                R += (double) p.dt;
            if (i == 0)
                continue;
            const float t = p.t;
            p.sign = t == 0.0f ? 0 : (std::signbit (t) ? -1 : 1);
            const int cnt = stepCount (p);
            const int m = p.mode;
            if (m == 6)
            {
                p.p4 = F ((double) cnt + 0.5);
                p.p5 = 6.2831855f;
                if (cnt == 0 && p.sign > 0)
                    p.sign = 2;
            }
            else if (m < 2)
            {
                const auto e1 = F (dmath::exp ((double) std::abs (t) * dmath::ln (1000001.0)) - 1.0);
                const float sg = t > 0.0f ? 1.0f : (t < 0.0f ? -1.0f : 0.0f);
                p.k = e1 * sg;
                if (p.k != 0.0f)
                {
                    p.ilk = F (1.0 / std::abs ((double) p.k));
                    p.L = F (dmath::ln (std::abs ((double) p.k) + 1.0));
                }
            }
            else if (m == 3)
            {
                const int sg = t < 0.0f ? -1 : 1;
                p.p6 = (float) sg * 0.5f;
                p.p4 = (float) (cnt + 1);
                p.p5 = 1.0f / (p.p4 - (float) sg);
            }
            else if (m == 4)
            {
                p.p4 = (float) (cnt + 1);
                p.p5 = 1.0f / p.p4;
            }
            else if (m == 5)
            {
                const int sg = t < 0.0f ? -1 : 1;
                p.p6 = (float) sg * 0.5f;
                p.p4 = (float) ((cnt + 1) * 2);
                p.p5 = 6.2831855f;
            }
            else if (m == 7 || m == 8)
                p.p4 = t;
            else if (m == 9)
                p.p4 = F (std::abs ((double) t));
            else if (m == 10)
                prepareSmooth (i);
            else if (m == 11 || m == 12)
            {
                auto dd = std::abs ((double) t) * 0.9;
                if (dd <= 1.52587890625e-05)
                    dd = 1.52587890625e-05;
                const auto uu = 1.0 - dd;
                p.p4 = F (1.0 - uu * uu * uu);
                p.p5 = (1.0f - p.p4) / p.p4;
                p.p6 = p.p5;
                if (p.sign > 0)
                {
                    p.p5 = -p.p5;
                    p.p6 = p.p5 - 1.0f;
                }
                if (m == 12)
                {
                    p.p5 = F ((double) p.p5 * 0.5);
                    p.p6 = F ((double) p.p6 * 0.5);
                }
            }
            if (m < 16 && ((0x1b83u >> m) & 1u) != 0 && t == 0.0f)
                p.sign = 2;
        }
        // modulation weights (only used with a sustain offset; kept for completeness)
        double r = A;
        for (int i = 0; i < n; ++i)
        {
            auto& p = pts[(std::size_t) i];
            double nr = 0.0;
            p.w = 0.0f;
            if (i > D)
            {
                p.w = 1.0f;
                if (i > S)
                {
                    nr = r - (double) p.dt;
                    p.w = F (nr / std::max (R, 1.52587890625e-05));
                }
                else
                {
                    if (i <= L)
                        p.w = F ((r + (double) p.dt) / std::max (M, 1.52587890625e-05));
                    nr = r + (double) p.dt;
                    if (i == S)
                        nr = R;
                }
            }
            else if (i != D)
            {
                nr = r - (double) p.dt;
                p.w = F (nr / std::max (A, 1.52587890625e-05));
            }
            r = nr;
        }
        zeroloop = false;
        if (endpt >= 0 && loop >= 0)
            zeroloop = time (endpt) - time (loop) <= 0.0;
    }

    double level (int i) const noexcept
    {
        if (i < 0 || i >= (int) pts.size())
            return 0.0;
        return (double) pts[(std::size_t) i].y - offset;
    }

    static double curveK (const Pt& p, double x) noexcept
    {
        if (p.k == 0.0f)
            return x;
        if (p.k > 0.0f)
            return 1.0 - (dmath::exp ((1.0 - x) * (double) p.L) - 1.0) * (double) p.ilk;
        return (dmath::exp (x * (double) p.L) - 1.0) * (double) p.ilk;
    }

    // fraction (0…1) of the segment ending at point i at phase ph
    float shape (int i, double ph) const noexcept
    {
        if (i < 0 || i >= (int) pts.size())
            return 0.0f;
        const auto& p = pts[(std::size_t) i];
        if (p.sign == 2)
            return F (ph);
        const double p4 = p.p4, p5 = p.p5, p6 = p.p6;
        switch (p.mode)
        {
            case 0: return F (curveK (p, ph));
            case 1:
                if (hiBelowHalf (ph))
                    return F (curveK (p, ph * 2.0) * 0.5);
                return F (1.0 - curveK (p, (1.0 - ph) * 2.0) * 0.5);
            case 3: return (float) (int) rnd (ph * p4 - p6) * p.p5;
            case 4:
            {
                const auto v = ph * p4 - 0.5;
                const auto i2 = (int) rnd (v - 0.5);
                auto dd = (v - (double) i2) * 2.0 - 1.0;
                if (p.sign < 0)
                {
                    const auto z = 1.0 - std::abs (dd);
                    dd = (1.0 - z * z * z) * (std::signbit (dd) ? -1.0 : 1.0);
                }
                else
                    dd = dd * dd * dd;
                return F (((double) i2 + dd * 0.5 + 1.0) * p5);
            }
            case 5: return (float) (rnd (ph * p4 - p6) & 1);
            case 6:
            {
                if (p.sign < 0)
                    return F ((dmath::sin ((ph * p4 + 0.75) * p5) + 1.0) * 0.5);
                const auto v = ph * p4 - 0.5;
                const auto i2 = (int) rnd (v - 0.5);
                return F (std::abs ((v - (double) i2) * 2.0 - 1.0));
            }
            case 7:
            {
                const auto dd = p.sign > 0 ? 1.0 - ph : ph;
                const auto d3 = dd * dd * dd;
                return F (ph - (d3 * d3 - dd) * p4);
            }
            case 8:
            {
                if (hiBelowHalf (ph))
                {
                    const auto x = ph * 2.0;
                    const auto dd = p.sign > 0 ? 1.0 - x : x;
                    const auto d3 = dd * dd * dd;
                    return F ((x - (d3 * d3 - dd) * p4) * 0.5);
                }
                const auto x = (1.0 - ph) * 2.0;
                const auto dd = p.sign > 0 ? 1.0 - x : x;
                const auto d3 = dd * dd * dd;
                return F (1.0 - (x - (d3 * d3 - dd) * p4) * 0.5);
            }
            case 9:
            {
                const auto hp = (double) 1.5707963705062866;
                const auto dd = p.sign > 0 ? dmath::sin (ph * hp) : 1.0 - dmath::cos (ph * hp);
                return F (ph - (ph - dd) * p4);
            }
            case 10:
            {
                const auto x2 = ph * ph, x1 = ph - 1.0;
                return F (ph + ((((x2 * 3.0 - x2 * 2.0 * ph) + x2 * x1 * p4) + ph * x1 * x1 * p5) - ph) * p6);
            }
            case 11:
                if (p.sign < 1)
                    return F (p5 / (1.0 - p4 * ph) - p6);
                return F (p5 / (1.0 - p4 * (1.0 - ph)) - p6);
            case 12:
                if (hiBelowHalf (ph))
                {
                    if (p.sign < 1)
                        return F (p5 / (1.0 - p4 * ph * 2.0) - p6);
                    return F (p5 / (1.0 - p4 * (1.0 - ph * 2.0)) - p6);
                }
                else
                {
                    const auto x = (1.0 - ph) * 2.0;
                    if (p.sign < 1)
                        return F ((1.0 - p5 / (1.0 - p4 * x)) + p6);
                    return F ((1.0 - p5 / (1.0 - p4 * (1.0 - x))) + p6);
                }
            default: return 0.0f; // 2 (hold) and unknown modes
        }
    }

    // "default" map: flat at the neutral value (1 multiplicative, 0.5 additive)
    bool isDefault() const noexcept
    {
        for (const auto& p : pts)
            if (p.y != pts[0].y)
                return false;
        return (double) pts[0].y == (offset == 0.5 ? 0.5 : 1.0);
    }

    // the map iterator stepped through x = 0, 1/1024, … (the plugin's table builder)
    void table (float* out, int count, double step) const noexcept
    {
        const int n = (int) pts.size();
        int stage = 1, idx = 0;
        double phase, rate;
        if (pts[0].dt == 0.0f)
            rate = 1.0, phase = 1.0;
        else
            rate = scale / (double) pts[0].dt, phase = 0.0;
        float start = F (level (0)), delta = 0.0f, value = start;
        for (int k = 0; k < count; ++k)
        {
            out[k] = value;
            // step
            if (! (idx >= 0 && idx <= n - 1))
                continue;
            phase += step * rate;
            while (phaseWrap (phase))
            {
                const int prev = idx;
                if (rate == 0.0)
                    phase = step;
                else
                    phase = (phase - 1.0) / rate;
                start = F (level (idx));
                if (idx < n - 1)
                    ++idx;
                else
                    stage = 9;
                delta = F (level (idx) - (double) start);
                if (prev == idx)
                    phase = rate = 0.0;
                else
                {
                    const double dt = (double) pts[(std::size_t) idx].dt;
                    if (dt <= 0.0)
                        rate = 1.0, phase += 1.0;
                    else
                        rate = 1.0 / dt, phase *= rate;
                }
            }
            (void) stage;
            value = start + shape (idx, phase) * delta;
        }
    }
};

// envelope state (the plugin's per-instance evaluation record)
struct EnvSt
{
    int stage = 9, idx = 0, sub = 9;
    double phase = 0, rate = 0;
    float start = 0, delta = 0, out = 0;
    bool hold = false;

    void begin (const Curve& c) noexcept
    {
        if (c.on == 0)
        {
            stage = sub = 9;
            start = delta = out = 0.0f;
            return;
        }
        idx = 0;
        stage = c.decay < 0 ? 3 : 1;
        if (c.pts[0].dt == 0.0f)
            rate = 1.0, phase = 1.0;
        else
            rate = c.scale / (double) c.pts[0].dt, phase = 0.0;
        sub = stage;
        start = F (c.level (0));
        delta = 0.0f;
        out = start;
        hold = c.zeroloop; // the D knob is 1 for these envelopes
    }

    void eval (const Curve& c, double adv) noexcept
    {
        const int n = (int) c.pts.size();
        if (! (idx >= 0 && idx <= n - 1))
            return;
        phase += adv * rate;
        while (phaseWrap (phase))
        {
            if (rate == 0.0)
                phase = adv;
            else
                phase = (phase - 1.0) / rate;
            start = F (c.level (idx));
            bool stop;
            if (idx < c.endpt || c.endpt < 0 || stage > 5)
            {
                if (c.decay <= idx && c.decay >= 0 && sub < 3)
                {
                    stage = std::max (stage, 3);
                    sub = 3;
                }
                if (idx < n - 1)
                {
                    ++idx;
                    stop = false;
                }
                else
                {
                    stage = 9;
                    stop = true;
                }
            }
            else if (c.loop < 0)
                stop = true;
            else
            {
                sub = 4;
                idx = c.loop + 1;
                stop = hold;
            }
            delta = F (c.level (idx) - (double) start);
            if (! stop)
            {
                const double dur = (double) c.pts[(std::size_t) idx].dt; // × knob factor 1
                if (dur <= 0.0)
                {
                    rate = 1.0;
                    phase += 1.0;
                }
                else
                {
                    rate = c.scale / dur;
                    phase *= rate;
                }
            }
            else
                phase = rate = 0.0;
        }
        if (delta == 0.0f)
            out = start;
        else
            out = start + c.shape (idx, phase) * delta;
    }

    // positioning at `pos` ticks (FUN of the transport resync): negative = not started yet
    void position (const Curve& c, double pos) noexcept
    {
        begin (c);
        if (pos < 0.0)
        {
            if (stage != 9)
            {
                rate = -1.0 / pos;
                phase = 0.0;
            }
            return;
        }
        double t = pos * c.scale;
        if (c.loop >= 0)
        {
            const double t0 = c.time (c.loop), len = c.time (c.endpt) - t0;
            if (len > 0.0)
            {
                const auto q = trunc64 ((t - t0) / len);
                t = (t0 + (t - t0)) - (double) q * len;
            }
        }
        eval (c, t / c.scale);
    }
};

// LFO waveform (shape and tension of the LFO object)
struct LfoWave
{
    float k = 0, ik = 0, L = 0;
    bool uni = false;
    u32 p5 = 0, p6 = 0, p7 = 0;
    float s0 = 0, s1 = 0;
    u32 startPhase = 0;

    void build (int tension, int skew, int pw, bool unipolar)
    {
        const float at = (float) std::abs (tension) * 0.0078125f;
        const double a = (double) F (dmath::exp ((double) at * dmath::ln (21.0)) - 1.0) * 0.05;
        const float sg = tension == 0 ? 0.0f : (tension < 0 ? -1.0f : 1.0f);
        k = sg * F (dmath::exp ((double) F (a) * dmath::ln (9.999999843067494e17)) - 1.0);
        if (k != 0.0f)
        {
            ik = F (1.0 / std::abs ((double) k));
            L = F (dmath::ln (std::abs ((double) k) + 1.0));
        }
        const float sk = (float) skew * 0.00390625f;
        const float pwf = (float) pw * 0.0078125f;
        uni = unipolar;
        if (0.0078125f >= pwf)
            p7 = 0xFFFFFFFFu;
        else
            p7 = (u32) rnd ((double) F (F (1.0f - pwf) * F (4294967296.0) - F (1.0)));
        auto dd = ((double) F ((1.0f - pwf) / 2.0f) - (double) sk * (1.0 - std::abs ((double) pwf))) * 4294967296.0;
        const auto hi = (u32) (p7 - 1u);
        if (dd > (double) hi)
            dd = (double) hi;
        else if (dd < 1.0)
            dd = 1.0;
        p5 = (u32) (std::uint64_t) rnd (dd);
        if (pwf < 0.0078125f)
        {
            p6 = (u32) (std::uint64_t) rnd (dd + (double) F (pwf * 4294967296.0f));
            if (p6 == 0)
                p6 = 1;
        }
        else
            p6 = p5;
        s0 = F (2.0 / (double) p6);
        s1 = F (2.0 / (double) (u32) (p7 - p5));
        startPhase = (u32) (p5 + (u32) (p7 - p5) / 2 + 1);
    }

    double curveK (double x) const noexcept
    {
        if (k == 0.0f)
            return x;
        if (k > 0.0f)
            return 1.0 - (dmath::exp ((1.0 - x) * (double) L) - 1.0) * (double) ik;
        return (dmath::exp (x * (double) L) - 1.0) * (double) ik;
    }

    float value (u32 u) const noexcept
    {
        float v;
        if (u > p7)
            return 1.0f;
        if (u < p6)
            v = 1.0f - (float) u * s0;
        else if (u < p5)
            return -1.0f;
        else
            v = (float) (u32) (u - p5) * s1 - 1.0f;
        if (! uni)
        {
            const auto r = F (curveK (std::abs ((double) v)));
            return std::signbit (v) ? -r : r;
        }
        const float x = (v + 1.0f) * 0.5f;
        return F (curveK ((double) x) * 2.0 - 1.0);
    }
};

// ---------------------------------------------------------------------------------------------- vector kernels
// out = in · g with g ramping in the plugin's four lanes (frames 2i, 2i+1 start at g and g + inc, stepping 2·inc)
void rampMul (const float* in, float* out, int n, float g, float inc) noexcept
{
    float l0 = g + 0.0f, l1 = g + inc;
    const float st = inc + inc;
    const int full = (n >> 3) << 3;
    for (int f = 0; f < full; f += 2)
    {
        out[2 * f] = in[2 * f] * l0;
        out[2 * f + 1] = in[2 * f + 1] * l0;
        out[2 * f + 2] = in[2 * f + 2] * l1;
        out[2 * f + 3] = in[2 * f + 3] * l1;
        l0 = l0 + st;
        l1 = l1 + st;
    }
    for (int f = full; f < n; ++f)
    {
        out[2 * f] = in[2 * f] * l0;
        out[2 * f + 1] = in[2 * f + 1] * l0;
        l0 = l0 + inc;
    }
}
// out += in · (gL, gR) ramping the same way
void rampAddProduct (const float* in, float* out, int n, float gL, float gR, float incL, float incR) noexcept
{
    float a0 = gL + 0.0f, b0 = gR + 0.0f, a1 = gL + incL, b1 = gR + incR;
    const float sa = incL + incL, sb = incR + incR;
    const int full = (n >> 3) << 3;
    for (int f = 0; f < full; f += 2)
    {
        out[2 * f] = in[2 * f] * a0 + out[2 * f];
        out[2 * f + 1] = in[2 * f + 1] * b0 + out[2 * f + 1];
        out[2 * f + 2] = in[2 * f + 2] * a1 + out[2 * f + 2];
        out[2 * f + 3] = in[2 * f + 3] * b1 + out[2 * f + 3];
        a0 = a0 + sa, b0 = b0 + sb, a1 = a1 + sa, b1 = b1 + sb;
    }
    for (int f = full; f < n; ++f)
    {
        out[2 * f] = in[2 * f] * a0 + out[2 * f];
        out[2 * f + 1] = in[2 * f + 1] * b0 + out[2 * f + 1];
        a0 = a0 + incL, b0 = b0 + incR;
    }
}
// in-place gain with the plugin's fast paths (0 clears, 1 keeps)
void gainInPlace (float* buf, int n, float g, float inc) noexcept
{
    if (inc != 0.0f)
    {
        rampMul (buf, buf, n, g, inc);
        return;
    }
    if (g == 0.0f && bitsOf (g) == 0)
        std::fill (buf, buf + 2 * n, 0.0f);
    else if (bitsOf (g) != bitsOf (1.0f))
        for (int i = 0; i < 2 * n; ++i)
            buf[i] = buf[i] * g;
}
void addGained (const float* src, float* dst, int n, float g, float inc) noexcept
{
    if (inc != 0.0f)
    {
        rampAddProduct (src, dst, n, g, g, inc, inc);
        return;
    }
    if (bitsOf (g) == 0)
        return;
    if (bitsOf (g) == bitsOf (1.0f))
        for (int i = 0; i < 2 * n; ++i)
            dst[i] = src[i] + dst[i];
    else
        for (int i = 0; i < 2 * n; ++i)
            dst[i] = std::fma (src[i], g, dst[i]); // FL's IPP library (AVX2 path): fused multiply-add
}

// the plugin's block ramp of a control: start = previous target, per-sample increment limited to maxStep
struct Ramp
{
    float prev = 0, start = 0, inc = 0;
    void to (float target, float maxStep, int n) noexcept
    {
        start = prev;
        inc = target - start;
        if (inc != 0.0f)
        {
            prev = target;
            inc = inc / (float) n;
            if (maxStep < std::abs (inc))
            {
                inc = std::copysign (maxStep, inc);
                prev = start + inc * (float) n;
                if (bitsOf (std::abs (prev)) < bitsOf (kTiny))
                {
                    prev = 0.0f;
                    inc = (-start) / (float) n;
                }
            }
        }
    }
};

// 2:1 polyphase IIR half-band stage (two chains of three first-order all-passes), up and down
constexpr double kHA[3] = { 0.14737711360104661, 0.48246854276970014, 0.88300502576937312 };
constexpr double kHB[3] = { 0.039151597734460045, 0.30264684832849342, 0.6746159185469639 };
struct HalfBand
{
    double s[12][2] {}; // per channel: A1 A2 A3 B1 B2 B3 outputs, then the previous inputs of each all-pass
    void clear() noexcept { std::memset (s, 0, sizeof s); }
    void flush() noexcept
    {
        for (int i = 0; i < 6; ++i)
            for (int c = 0; c < 2; ++c)
                s[i][c] = flushD (s[i][c]);
    }
    void up (const float* x, float* y, int m) noexcept // m input frames → 2m output frames
    {
        for (int n = 0; n < m; ++n)
            for (int c = 0; c < 2; ++c)
            {
                const double v = x[2 * n + c];
                const double a1 = s[6][c] + (v - s[0][c]) * kHA[0];
                s[6][c] = v, s[0][c] = a1;
                const double b1 = s[9][c] + (v - s[3][c]) * kHB[0];
                s[9][c] = v, s[3][c] = b1;
                const double a2 = s[7][c] + (a1 - s[1][c]) * kHA[1];
                s[7][c] = a1, s[1][c] = a2;
                const double b2 = s[10][c] + (b1 - s[4][c]) * kHB[1];
                s[10][c] = b1, s[4][c] = b2;
                const double a3 = s[8][c] + (a2 - s[2][c]) * kHA[2];
                s[2][c] = a3;
                const double b3 = s[11][c] + (b2 - s[5][c]) * kHB[2];
                s[5][c] = b3;
                s[8][c] = a2, s[11][c] = b2;
                y[4 * n + c] = (float) b3;
                y[4 * n + 2 + c] = (float) a3;
            }
        flush();
    }
    void down (const float* x, float* y, int m) noexcept // 2m input frames → m output frames
    {
        for (int n = 0; n < m; ++n)
            for (int c = 0; c < 2; ++c)
            {
                const double e = x[4 * n + c], o = x[4 * n + 2 + c];
                const double a1 = s[6][c] + (e - s[0][c]) * kHA[0];
                s[6][c] = e, s[0][c] = a1;
                const double b1 = s[9][c] + (o - s[3][c]) * kHB[0];
                s[9][c] = o, s[3][c] = b1;
                const double a2 = s[7][c] + (a1 - s[1][c]) * kHA[1];
                s[7][c] = a1, s[1][c] = a2;
                const double b2 = s[10][c] + (b1 - s[4][c]) * kHB[1];
                s[10][c] = b1, s[4][c] = b2;
                const double a3 = s[8][c] + (a2 - s[2][c]) * kHA[2];
                s[8][c] = a2, s[2][c] = a3;
                const double b3 = s[11][c] + (b2 - s[5][c]) * kHB[2];
                s[11][c] = b2, s[5][c] = b3;
                y[2 * n + c] = (float) ((b3 + a3) * 0.5);
            }
        flush();
    }
};

// ------------------------------------------------------------------------------------------------ filter stage
struct Stage
{
    int code = 0;              // biquad design code
    float gain = 0;            // Mango output gain
    float f = 0, q = 0;        // SVF / Lime coefficients (+8, +0xC)
    double st[10] {};          // kernel state (doubles for SVF/biquad/Lime; Mango uses mst)
    float mst[10] {};
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // biquad (normalised)
    double coef = 0, drive = 0; // Mango
    Ramp rf, rq;               // ramps of f and q (rf.prev starts at 0.5, rq.prev at 1.0 / 0.1)
};

void biquadDesign (Stage& s, int code, double A, double alpha, double sn, double cs) noexcept
{
    if (code < 6)
    {
        const double d = 1.0 / (alpha + 1.0);
        s.a1 = d * cs * -2.0;
        s.a2 = d * (1.0 - alpha);
        if (code == 5)
        {
            s.b1 = s.a1;
            s.b0 = s.a2;
            s.b2 = 1.0;
        }
        else if (code == 1)
        {
            s.b1 = d * (1.0 - cs);
            s.b0 = s.b1 * (double) 0.5f;
            s.b2 = s.b0;
        }
        else if (code == 2)
        {
            s.b1 = 0.0;
            s.b0 = d * alpha;
            s.b2 = d * -alpha;
        }
        else if (code == 3)
        {
            s.b1 = s.a1;
            s.b0 = d;
            s.b2 = d;
        }
        else if (code == 4)
        {
            s.b1 = d * -(cs + 1.0);
            s.b0 = -s.b1 * (double) 0.5f;
            s.b2 = s.b0;
        }
    }
    else if (code == 6)
    {
        const double e = (A - 1.0) * cs;
        const double t = alpha * sn;
        const double d = 1.0 / (A + 1.0 + e + t);
        s.a1 = d * ((A - 1.0) + (A + 1.0) * cs) * -2.0;
        s.a2 = d * ((A + 1.0 + e) - t);
        s.b1 = d * A * 2.0 * ((A - 1.0) - (A + 1.0) * cs);
        s.b0 = d * A * (((A + 1.0) - e) + t);
        s.b2 = d * A * (((A + 1.0) - e) - t);
    }
    else if (code == 7)
    {
        const double d = 1.0 / (alpha / A + 1.0);
        s.a1 = d * cs * -2.0;
        s.a2 = d * (1.0 - alpha / A);
        s.b1 = s.a1;
        s.b0 = d * (alpha * A + 1.0);
        s.b2 = d * (1.0 - alpha * A);
    }
    else if (code == 8)
    {
        const double e = (A - 1.0) * cs;
        const double t = alpha * sn;
        const double d = 1.0 / (((A + 1.0) - e) + t);
        s.a1 = d * ((A - 1.0) - (A + 1.0) * cs) * 2.0;
        s.a2 = d * (((A + 1.0) - e) - t);
        s.b1 = d * A * -2.0 * ((A - 1.0) + (A + 1.0) * cs);
        s.b0 = d * A * (A + 1.0 + e + t);
        s.b2 = d * A * ((A + 1.0 + e) - t);
    }
}

// the plugin runtime's math (core/DelphiMath.h): Power for doubles and singles
inline double powD (double x, double y) noexcept { return dmath::power (x, y); }
float powf32 (float x, float y) noexcept
{
    if (y == 0.0f)
        return 1.0f;
    if (x == 0.0f && y > 0.0f)
        return 0.0f;
    if (std::trunc (y) == y)
        return (float) dmath::intPower ((double) x, (long long) rnd ((double) y));
    return (float) dmath::exp ((double) y * dmath::ln ((double) x));
}
inline double sinhD (double x) noexcept { return dmath::sinh (x); }
inline double tanhD (double x) noexcept { return dmath::tanh (x); }

// ---------------------------------------------------------------------------------------- articulator runtime
struct ArtRt
{
    int mode = 0;        // 1 additive (pan, cutoff, resonance), 0 multiplicative
    float base = 0, envDepth = 1, lfoDepth = 0;
    Curve env, lfoEnv;
    LfoWave wave;
    bool envOn = false, lfoOn = false;
    double lfoInc = 0, lfoIncPos = 0; // per tick (0 when frozen) / for positioning
    u32 lfoPhase = 0, phaseOffset = 0;
    EnvSt es, ls;
    float lfoOut = 0;
    std::array<std::vector<float>, 3> maps; // X, Y, follower (1025 points; empty = default map)
    float out = 0;
    bool active = false;

    void tick() noexcept
    {
        if (envOn)
        {
            es.eval (env, 1.0);
            if (mode == 1)
                out = out + es.out * envDepth;
            else
                out = out * es.out * envDepth;
        }
        if (lfoOn)
        {
            double adv2 = 1.0;
            if (ls.idx == 0 && ls.stage < 9 && ! phaseWrap (ls.phase))
            {
                const double x = 1.0 + (ls.phase - 1.0) / ls.rate;
                adv2 = 0.0 < x ? x : 0.0;
            }
            ls.eval (lfoEnv, 1.0);
            if (ls.idx != 0 || ls.stage > 8 || phaseWrap (ls.phase))
            {
                lfoPhase = (u32) (lfoPhase + (u32) rnd (adv2 * lfoInc));
                lfoOut = ls.out * wave.value (lfoPhase);
            }
            if (mode == 1)
                out = out + lfoOut * lfoDepth;
            else
                out = out * (lfoOut * lfoDepth * 2.0f + 1.0f);
        }
    }

    // the transport resync of one articulator (with `full`, the LFO too)
    void position (double pos, bool full) noexcept
    {
        if (full)
        {
            ls.begin (lfoEnv);
            lfoOut = 0.0f;
        }
        if (full && lfoOn)
        {
            ls.position (lfoEnv, pos);
            lfoPhase = (u32) (wave.startPhase + phaseOffset);
            lfoPhase = (u32) (lfoPhase + (u32) (std::int32_t) rnd (pos * lfoIncPos));
        }
        es.position (env, pos);
    }
};

// ------------------------------------------------------------------------------------------------ unit runtime
struct UnitRt
{
    int index = 0;
    bool enabled = false;
    int type = 0, mode = 1, n = 0; // n = index of the last stage (mode − 1, −2 for the alternate modes)
    bool alt = false, flat = false;
    float qExp = 1, bwMode = 0, k = 1;
    float c0 = 0, r0 = 0, L = 0, B = 0, H = 0, wsPre = 0, wsMix = 0, in = 1, next = 0, pan = 0, vol = 0;
    std::array<ArtRt, 8> art;
    std::array<Stage, 3> st;
    float cut = 0, res = 0; // +0x238/+0x23C
    std::array<float, 3> gain {}; // L/B/H (or shelf gain / Mango drive in gain[0])
    std::array<Ramp, 3> gr;
    Ramp qr0; // stage-0 q ramp of the SVF (shares the stage record)
    // waveshaper
    std::vector<std::int16_t> wsTab; // entry k at wsTab[k + 1]
    int wsN = 0;
    bool wsUni = false;
    float mix = 0;
    Ramp mixRamp;
    bool follow1 = false;
    // pan matrix (LL, RR, R→L, L→R)
    std::array<float, 4> pm {};
    std::array<Ramp, 4> pr;
};

} // namespace

// ================================================================================================== the engine
struct Engine::Impl
{
    std::shared_ptr<const State> S;
    State s; // working copy (parameters change it)
    bool render = false;
    int fs = 44100, order = 0, factor = 1, fsInt = 44100;
    double kRate = 1.0;
    float maxStep = 0.005f;
    int tickN = 64, counter = 0;
    double tau = 0; // +0x16A4
    float smoothCoef = 0.968f;
    float bpm = 140.0f;
    bool playing = false;
    double songBeats = 0.0;
    float master = 1, masterLfo = 1, follLevel = 1;
    std::array<float, 2> follCoef {};
    float follEnv = 0, follPeak = 0;
    int follMode = 0;
    std::array<float, 2> modSm {};
    std::array<int, 3> mapIdx {};
    int silence = 0, silenceInit = 8820;
    bool firstTick = false;
    std::array<UnitRt, 8> u;
    std::vector<int> list;         // enabled units in order
    std::vector<ArtRt*> activeArts;
    HalfBand up[4], dn[4];
    // CENTER
    ld dcY[2] {}, dcV[2] {};
    float dcA = 0, dcB = 0.96f;
    std::vector<float> bin, bout, bacc, b2, b3, b4;

    explicit Impl (std::shared_ptr<const State> st) : S (std::move (st))
    {
        if (S)
            s = *S;
        load();
    }

    // ---------------------------------------------------------------------------------------- setup
    void globals()
    {
        master = curve11 (s.g[0]);
        masterLfo = scurve11 (s.g[1]);
        follLevel = curve11 (s.g[2]);
        for (auto& un : u)
            for (auto& a : un.art)
                a.lfoDepth = masterLfo;
        for (int i = 0; i < 8; ++i)
            unitParams (i);
    }

    void unitParams (int i)
    {
        auto& un = u[(std::size_t) i];
        const auto& p = s.u[(std::size_t) i].p;
        auto& cutArt = un.art[2];
        cutArt.envDepth = (float) p[0] * 0.0078125f;
        cutArt.lfoDepth = cutArt.envDepth * masterLfo;
        un.c0 = (float) p[1] * 0.00390625f;
        un.r0 = (float) p[2] * 0.00390625f;
        un.L = curve11 (p[3]);
        un.B = curve11 (p[4]);
        un.H = curve11 (p[5]);
        un.wsPre = (float) p[6] * 0.0078125f;
        un.wsMix = (float) p[7] * 0.0078125f;
        un.in = scurve11 (p[10]);
        un.pan = (float) p[8] * 0.0078125f;
        un.vol = (s.solo < 0 || s.solo == i) ? curve11 (p[9]) * master : 0.0f;
        buildList();
    }

    void buildList()
    {
        list.clear();
        for (int i = 0; i < 8; ++i)
        {
            auto& un = u[(std::size_t) i];
            un.next = 0.0f;
            un.follow1 = false;
            if (! s.u[(std::size_t) i].enabled)
                continue;
            list.push_back (i);
            if (i > 0 && s.u[(std::size_t) (i - 1)].enabled)
                un.next = scurve11 (s.u[(std::size_t) (i - 1)].p[11]);
            un.follow1 = follMode == 2 && i == 0;
        }
    }

    void unitSetup (int i, bool resetGains)
    {
        auto& un = u[(std::size_t) i];
        const auto& su = s.u[(std::size_t) i];
        un.type = su.type;
        un.mode = su.mode;
        un.flat = su.flat;
        un.n = su.mode - 1;
        const float limeQ = (su.type == 1 || su.type == 2) ? 0.1f : 1.0f;
        float bm = (float) su.mode;
        if (un.n >= 0)
        {
            un.alt = un.n > 2;
            if (un.alt)
                un.n -= 2;
            if (su.type == 11)
                un.qExp = (float) (1.0 / ((double) un.n / 4.0 + 1.0));
            else
                un.qExp = (float) (1.0 / (double) (un.n + 1));
            if (su.type == 5)
                bm = 1.0f / bm;
            un.k = (float) kRate;
            for (int k = 0; k <= un.n; ++k)
            {
                auto& stg = un.st[(std::size_t) k];
                std::fill (std::begin (stg.st), std::end (stg.st), 0.0);
                std::fill (std::begin (stg.mst), std::end (stg.mst), 0.0f);
                stg.rq.inc = 0.0f;
            }
            for (int k = 1; k <= un.n; ++k)
                un.st[(std::size_t) k].q = limeQ;
        }
        un.bwMode = (float) ((double) bm * kLn2Half);
        for (auto& stg : un.st)
        {
            stg.rf.prev = 0.5f;
            stg.rq.prev = limeQ;
        }
        if (resetGains)
        {
            for (int k = 0; k < 4; ++k)
                un.pr[(std::size_t) k].prev = un.pm[(std::size_t) k];
            for (auto& g : un.gr)
                g.prev = 0.0f;
            un.mixRamp.prev = 0.0f;
        }
    }

    void wsTable (int i)
    {
        auto& un = u[(std::size_t) i];
        const auto& su = s.u[(std::size_t) i];
        un.wsTab.clear();
        if (! su.wsOn)
            return;
        Curve c;
        c.build (su.ws, su.wsBip != 0 ? 0.5 : 0.0, true);
        c.scale = 1.0;
        const int bip = su.wsBip != 0 ? 1 : 0;
        un.wsUni = bip != 0;
        int N = (bip + 1) * 0x4000;
        N -= 1;
        un.wsN = N;
        un.wsTab.assign ((std::size_t) N + 3, 0);
        std::vector<float> v ((std::size_t) N + 1);
        c.table (v.data(), N + 1, (double) F (1.0 / (double) N));
        const float full = (float) ((bip + 1) * 0x8000 - 1);
        for (int k = 0; k <= N; ++k)
        {
            auto q = rnd ((double) (v[(std::size_t) k] * full));
            q = std::clamp<long long> (q, -0x8000, 0x7fff);
            un.wsTab[(std::size_t) k + 1] = (std::int16_t) q;
        }
        un.wsTab[0] = un.wsTab[1];
        un.wsTab[(std::size_t) N + 2] = un.wsTab[(std::size_t) N + 1];
    }

    void artSetup (int i)
    {
        auto& un = u[(std::size_t) i];
        const auto& su = s.u[(std::size_t) i];
        for (int a = 0; a < 8; ++a)
        {
            auto& ar = un.art[(std::size_t) a];
            const auto& sa = su.art[(std::size_t) a];
            ar.mode = (a == 0 || a == 2 || a == 3) ? 1 : 0;
            const double off = ar.mode * 0.5;
            ar.base = sa.base;
            if (a != 2)
                ar.envDepth = sa.envDepth;
            ar.env.build (sa.pat[(std::size_t) (s.g[5] - 1)], off, false);
            ar.envOn = ar.env.on != 0;
            ar.lfoEnv.build (sa.lfo, 0.5, false);
            ar.lfoOn = ar.lfoEnv.on != 0;
            ar.wave.build (sa.lfoKnobs[1], sa.lfoKnobs[2], sa.lfoKnobs[3], (sa.lfo.flags & 4) != 0);
            ar.phaseOffset = (u32) sa.lfoKnobs[4];
            for (int m = 0; m < 3; ++m)
            {
                Curve c;
                c.build (sa.maps[(std::size_t) m], off, true);
                c.scale = 1.0;
                auto& tab = ar.maps[(std::size_t) m];
                if (c.isDefault())
                    tab.clear();
                else
                {
                    tab.assign (1025, 0.0f);
                    c.table (tab.data(), 1025, (double) 0.0009765625f);
                }
            }
        }
    }

    void lfoSetup (int ui, int ai)
    {
        auto& ar = u[(std::size_t) ui].art[(std::size_t) ai];
        const auto& sa = s.u[(std::size_t) ui].art[(std::size_t) ai];
        ar.wave.build (sa.lfoKnobs[1], sa.lfoKnobs[2], sa.lfoKnobs[3], (sa.lfo.flags & 4) != 0);
        timeScales();
    }

    // a new pattern number: every articulator switches to that envelope, repositioned (envelopes only)
    void selectPattern()
    {
        for (int i = 0; i < 8; ++i)
            for (int a = 0; a < 8; ++a)
            {
                auto& ar = u[(std::size_t) i].art[(std::size_t) a];
                ar.env.build (s.u[(std::size_t) i].art[(std::size_t) a].pat[(std::size_t) (s.g[5] - 1)], ar.mode * 0.5, false);
                ar.envOn = ar.env.on != 0;
            }
        timeScales();
        const bool keep = playing;
        if (! s.syncPatterns)
            playing = false;
        resync (false);
        playing = keep;
    }

    void follSetup()
    {
        follMode = 0;
        for (auto& un : u)
            for (auto& a : un.art)
                if (! a.maps[2].empty())
                    follMode = 1;
        if (follMode == 0)
        {
            mapIdx[2] = 0;
            follEnv = follPeak = 0.0f;
        }
        else
            follMode += s.use1;
        buildList();
    }

    void follCoefs()
    {
        const double perMs = (double) fsInt * 0.0125;
        for (int k = 0; k < 2; ++k)
        {
            const double x = (double) s.g[(std::size_t) (3 + k)] * 6.103515625e-05;
            double t = dmath::exp ((double) F (x) * dmath::ln (81.0));
            t = (double) F (t - 1.0) * perMs;
            follCoef[(std::size_t) k] = t > 0.0 ? F (dmath::exp (dmath::ln (0.01) / t)) : 0.0f;
        }
    }

    void timeScales()
    {
        const double t = tau * 96.0 / 44100.0;
        const double freeScale = t * 2.0, tempoScale = t * (double) bpm / 60.0;
        for (auto& un : u)
            for (auto& a : un.art)
            {
                a.env.scale = (a.env.flags & 1) ? tempoScale : freeScale;
                a.lfoEnv.scale = (a.lfoEnv.flags & 1) ? tempoScale : freeScale;
                // LFO rate per tick from the speed knob (the frozen flag zeroes the running increment)
                const auto& sa = s.u[(std::size_t) (&un - u.data())].art[(std::size_t) (&a - un.art.data())];
                const double x = (double) sa.lfoKnobs[0] * 1.52587890625e-05;
                double inc = (double) F (dmath::exp ((double) F (x) * dmath::ln (10001.0)) - 1.0) * 6871947.6736 * a.lfoEnv.scale;
                if (805306368.0 < inc)
                    inc = 805306368.0;
                a.lfoIncPos = (double) (u32) rnd (inc);
                a.lfoInc = (a.lfoEnv.flags & 8) ? 0.0 : a.lfoIncPos;
            }
    }

    void rateSetup()
    {
        order = render ? std::min (s.os * 2, 4) : s.os;
        order = std::clamp (order, 0, 4);
        factor = 1 << order;
        fsInt = std::max (1, fs * factor);
        kRate = 44100.0 / (double) fsInt;
        maxStep = F (F (kRate * 0.001) * 10.0f);
        silenceInit = (int) rnd ((double) fsInt / 5.0);
        // control tick
        const int v = (s.hq > 0 || render) ? 8 : 64;
        const double t0 = (double) (v * fsInt) / 44100.0;
        tickN = (int) std::max<long long> (4, rnd (t0 / 4.0) * 4);
        tau = (double) (tickN * v) / (t0 * 96.0);
        smoothCoef = F (dmath::exp (dmath::ln (0.01) * tau / 96.0));
        timeScales();
        follCoefs();
        dcA = F (1.0070234999999998 / (double) fs);
        dcB = 0.96f;
        dcY[0] = dcY[1] = dcV[0] = dcV[1] = 0.0L;
        for (auto& h : up)
            h.clear();
        for (auto& h : dn)
            h.clear();
        for (int i = 0; i < 8; ++i)
            unitSetup (i, true);
        resync (true);
    }

    void load()
    {
        follMode = 0;
        for (int i = 0; i < 8; ++i)
        {
            u[(std::size_t) i].index = i;
            wsTable (i);
            artSetup (i);
        }
        follSetup();
        globals();
        for (int i = 0; i < 8; ++i)
            unitSetup (i, true);
        rateSetup();
    }

    // transport resync: every articulator of every unit restarted at the song position (ticks)
    void resync (bool full)
    {
        if (full)
            counter = 0;
        double pos = 0.0;
        if (playing) // FL's host time in ticks (PPQ 96) → samples: t·fs·240 / (BPM·PPQ·4), the divisor in floats
            pos = (songBeats * 96.0 * (double) fsInt * 240.0) / (double) (bpm * 96.0f * 4.0f);
        pos = ((pos - (double) tickN) + (double) counter) / (double) tickN;
        activeArts.clear();
        for (auto& un : u)
            for (auto& a : un.art)
            {
                if (! a.envOn && ! a.lfoOn && a.maps[0].empty() && a.maps[1].empty() && a.maps[2].empty())
                {
                    a.out = a.base;
                    continue;
                }
                activeArts.push_back (&a);
                if (full)
                {
                    const float keep = a.out;
                    a.position (pos, true);
                    a.out = keep;
                }
                else
                    a.es.position (a.env, pos);
            }
    }

    // ------------------------------------------------------------------------------------------- per tick
    void controlTick()
    {
        const std::array<int, 2> target { s.modX, s.modY };
        for (int k = 0; k < 2; ++k)
        {
            const float tg = (float) target[(std::size_t) k];
            if (! s.smooth)
                modSm[(std::size_t) k] = tg;
            else
                modSm[(std::size_t) k] = tg + (modSm[(std::size_t) k] - tg) * smoothCoef;
            mapIdx[(std::size_t) k] = (int) std::clamp<long long> (rnd ((double) modSm[(std::size_t) k]), 0, 1024);
        }
        if (follMode > 0)
        {
            mapIdx[2] = (int) std::clamp<long long> (rnd ((double) (follPeak * 1024.0f)), 0, 1024);
#ifdef LP_DEBUG
            std::fprintf (stderr, "tick ief %d %.9g\n", mapIdx[2], (double) follPeak);
#endif
            follPeak = 0.0f;
        }
        for (auto* a : activeArts)
        {
            a->out = a->base;
            for (int m = 0; m < 3; ++m)
            {
                const auto& tab = a->maps[(std::size_t) m];
                if (tab.empty())
                    continue;
                float v = tab[(std::size_t) mapIdx[(std::size_t) m]];
                if (m == 2)
                    v = v * a->envDepth;
                if (a->mode == 0)
                    a->out = a->out * v;
                else
                    a->out = a->out + v;
            }
            a->tick();
        }
    }

    // x87: the follower stays in extended precision within the block (its float copy is what is stored)
    void follower (const float* x, int n)
    {
        const X87Mode mode (false);
        ld env = follEnv;
        const ld lvl = follLevel, att = follCoef[0], rel = follCoef[1];
        for (int i = 0; i < n; ++i)
        {
            const float m = std::max (std::abs (x[2 * i]), std::abs (x[2 * i + 1]));
            const ld v = (ld) m * lvl;
            const ld c = v > env ? att : rel;
            env = v + (env - v) * c;
            float st = (float) env;
            if ((u32) bitsOf (st) < (u32) bitsOf (kTiny))
            {
                st = 0.0f;
                env = 0.0L;
            }
            follPeak = std::max (follPeak, st);
        }
        follEnv = (float) env;
    }

    // ---------------------------------------------------------------------------------------- coefficients
    void coefs (UnitRt& un)
    {
        const float c = un.cut;
        double r = (double) un.res;
        const int t = un.type;
        const double kd = (double) un.k;
        auto alpha = [&] (double x) { return (double) powf32 ((float) x, un.n == 1 ? 0.5f : 0.75f); };
        auto clampW = [] (double w) { return w <= 2.849517146113191 ? (w < 0.0 ? 0.0 : w) : 2.849517146113191; };
        auto curveX = [&] (double base, double mult) {
            return (double) F (dmath::exp ((double) c * dmath::ln (base)) - 1.0) * mult;
        };
        if (t == 0)
        {
            double x = 0.0;
            if (0.0 < (double) c)
                x = curveX (51.0, 0.01590000092983246);
            for (int i = un.n; i >= 0; --i)
            {
                const double w = (x + 0.005) * kd;
                const double f = (w <= 0.625 ? dmath::sin (w) : dmath::sin (0.625)) * 2.0;
                un.st[(std::size_t) i].f = F (f);
                if (un.alt)
                    x = alpha (x);
            }
            double q = 1.0;
            if (0.0 < r)
            {
                q = 0.2;
                if (r < 1.0)
                    q = 1.0 - r * 0.8999999761581421;
            }
            if (un.flat)
                q = q * q;
            un.st[0].q = F (q);
            return;
        }
        if (t == 1 || t == 2)
        {
            if (un.flat)
                r = r * ((double) c + 1.0) * (double) 0.5f;
            double x = 0.0;
            if (0.0 < (double) c)
                x = std::min (curveX (76.0, 0.01324), 1.6);
            double q = 1.0;
            if (0.0 < r)
            {
                q = 50.0;
                if (r < 1.0)
                    q = (double) F (dmath::exp ((double) F (r) * dmath::ln (1001.0)) - 1.0) * 0.049 + 1.0;
            }
            if (un.n > 0)
                q = (double) powf32 ((float) q, un.qExp);
            const double kk = kd * 1.5707963267948966;
            for (int i = un.n; i >= 0; --i)
            {
                auto& stg = un.st[(std::size_t) i];
                double w = (x + 0.007) * kk;
                const double cs = dmath::cos (w <= 1.2566370614359172 ? w : 1.2566370614359172);
                stg.f = F (2.0 - cs * 2.0);
                const double tm = cs - 1.0;
                const double sq = std::sqrt ((-tm) * tm * tm);
                stg.q = F ((sq * (double) kSqrt2f) / (q * tm) + 1.0);
                if ((double) stg.q < 0.1)
                    stg.q = 0.1f;
                if (un.alt)
                    x = alpha (x);
            }
            return;
        }
        static const int codes[11] = { 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
        if (t >= 3 && t <= 10)
        {
            if (un.flat)
                r = r * ((double) c + 1.0) * (double) 0.5f;
            double x = 0.0;
            if (t == 3)
            {
                if (0.0 < (double) c)
                    x = curveX (1501.0, 0.0006646666442975402);
            }
            else if (0.0 < (double) c)
            {
                x = curveX (1501.0, 0.00039993334212340415);
                const double lim = t == 6 ? 0.6001 : 1.0001;
                if (lim < x)
                    x = lim;
            }
            const double kk = kd * 6.283185307179586;
            const double off = t == 3 ? 0.003 : 0.0001;
            double par = 0.0, A = 0.0, gA = 0.0;
            if (t == 3 || t == 6 || t == 7)
            {
                double Q = 0.1;
                if (0.0 < r)
                {
                    Q = 50.0;
                    if (r < 1.0)
                        Q = (double) F (dmath::exp ((double) F (r) * dmath::ln (2001.0)) - 1.0) * 0.024950001388788223 + 0.1;
                }
                if (un.n > 0)
                    Q = (double) powf32 ((float) Q, un.qExp);
                par = 0.5 / Q;
            }
            else if (t == 4 || t == 5)
            {
                par = 0.01;
                if (0.0 < r)
                {
                    par = 12.0;
                    if (r < 1.0)
                        par = (double) F (dmath::exp ((double) F (r) * dmath::ln (101.0)) - 1.0) * 0.11989999562501907 + 0.01;
                }
            }
            else if (t == 8 || t == 10)
            {
                const double rr = 1.0 - r;
                double Sl = 0.1;
                if (0.0 < rr)
                {
                    Sl = 1.0;
                    if (rr < 1.0)
                        Sl = (double) F (dmath::exp ((double) F (rr) * dmath::ln (3.0)) - 1.0) * 0.44999998807907104 + 0.1;
                }
                A = (double) un.gain[0];
                if (un.n > 0)
                    A = powD (A, (double) un.qExp);
                gA = std::sqrt (A);
                par = std::sqrt ((A + 1.0 / A) * (1.0 / Sl - 1.0) + 2.0);
            }
            else // 9
            {
                const double rr = 1.0 - r;
                double bw = 0.1;
                if (0.0 < rr)
                {
                    bw = 100.0;
                    if (rr < 1.0)
                        bw = (double) F (dmath::exp ((double) F (rr) * dmath::ln (2001.0)) - 1.0) * 0.049949999898672104 + 0.1;
                }
                A = (double) un.gain[0];
                if (un.n > 0)
                    A = powD (A, (double) un.qExp);
                par = bw;
            }
            for (int i = un.n; i >= 0; --i)
            {
                auto& stg = un.st[(std::size_t) i];
                const double w = clampW ((x + off) * kk);
                const double sn = dmath::sin (w), cs = dmath::cos (w);
                stg.code = codes[t];
                if (t == 3 || t == 6 || t == 7)
                    biquadDesign (stg, stg.code, 0.0, sn * par, sn, cs);
                else if (t == 4 || t == 5)
                {
                    const double a = sinhD ((par * (double) un.bwMode * w) / sn);
                    biquadDesign (stg, stg.code, 0.0, sn * a, sn, cs);
                }
                else if (t == 8 || t == 10)
                    biquadDesign (stg, stg.code, A, gA * par, sn, cs);
                else
                    biquadDesign (stg, stg.code, A, sn / (par * 2.0), sn, cs);
                if (un.alt)
                    x = alpha (x);
            }
            return;
        }
        if (t == 11)
        {
            double x = 0.0;
            if (0.0 < (double) c)
                x = std::min (curveX (201.0, 0.014949999749660492), 3.0);
            const float G = un.gain[0];
            const double g7 = (double) F (dmath::exp ((double) G * dmath::ln (251.0)) - 1.0) * 0.12;
            const double inv = 0.001 <= g7 ? 1.0 / g7 : 1.0;
            double R = 0.0;
            if (0.0 < r)
            {
                float f5 = 1.0f;
                if (F (g7) < 1.0f)
                    f5 = F (g7);
                R = (double) f5 * 0.6 + 1.9;
                if (r < 1.0)
                    R = r * R;
            }
            if (un.n > 0)
                R = powD (R + 1.0, (double) un.qExp) - 1.0;
            const double kk = kd * 0.5;
            for (int i = un.n; i >= 0; --i)
            {
                auto& stg = un.st[(std::size_t) i];
                double w = (x + 0.01) * kk;
                if (0.65 < w)
                    w = 0.65;
                const double w2 = w * w;
                const double e = dmath::exp (-kPi * (((w * 1.873 * w2 + w2 * 0.4955) - w * 0.649) + 0.9988) * w);
                stg.coef = 1.0 - e;
                stg.q = F (-R * ((w2 * -3.9364 + w * 1.8409) + 0.9968));
                if (0.001 <= g7)
                {
                    stg.drive = g7 * 2.0;
                    stg.coef = stg.coef * inv;
                    stg.gain = F (tanhD (inv * 4.0));
                    if (un.flat)
                        stg.q = stg.q * stg.gain;
                    stg.gain = F ((double) (1.0f / stg.gain) * 0.5 * (double) (1.0f - stg.q));
                }
                else
                {
                    stg.gain = F ((double) (1.0f - stg.q) * 0.5);
                    stg.drive = 0.0;
                }
                if (un.alt)
                    x = alpha (x);
            }
        }
    }

    // ------------------------------------------------------------------------------------------ kernels
    // x87: the integrators stay in extended precision within the block, outputs are stored as floats
    static void svf (Stage& s, const float* in, float* lo, float* bp, float* hp, int n, float df, float dq) noexcept
    {
        const X87Mode mode (false);
        for (int c = 0; c < 2; ++c)
        {
            ld l = s.st[c], b = s.st[2 + c];
            ld f = s.f, q = s.q;
            const ld dF = df, dQ = dq;
            for (int i = 0; i < n; ++i)
            {
                l = l + b * f;
                const ld x = in[2 * i + c];
                lo[2 * i + c] = (float) l;
                const ld h = x - (b * q + l);
                hp[2 * i + c] = (float) h;
                b = h * f + b;
                bp[2 * i + c] = (float) b;
                if (df != 0.0f || dq != 0.0f)
                {
                    q = q + dQ;
                    f = f + dF;
                }
            }
            s.st[2 + c] = (double) b;
            s.st[c] = (double) l;
        }
        for (int k = 0; k < 8; ++k)
            s.st[k] = flushD (s.st[k]);
    }

    static void lime (Stage& s, float* buf, int n, float dA, float dB, bool hp) noexcept
    {
        const X87Mode mode (false);
        ld B = s.q, A = s.f;
        ld yR = s.st[1], vR = s.st[3], yL = s.st[0], vL = s.st[2];
        for (int i = 0; i < n; ++i)
        {
            const ld xl = buf[2 * i];
            vL = vL + (xl - yL) * A;
            yL = yL + vL;
            vL = vL * B;
            buf[2 * i] = hp ? (float) (xl - yL) : (float) yL;
            const ld xr = buf[2 * i + 1];
            vR = vR + (xr - yR) * A;
            yR = yR + vR;
            vR = vR * B;
            buf[2 * i + 1] = hp ? (float) (xr - yR) : (float) yR;
            A = A + (ld) dA;
            B = B + (ld) dB;
        }
        s.st[2] = (double) flushX87 (vL);
        s.st[0] = (double) flushX87 (yL);
        s.st[3] = (double) flushX87 (vR);
        s.st[1] = (double) flushX87 (yR);
    }

    static void biquad (Stage& s, float* buf, int n) noexcept
    {
        double x1L = s.st[0], x1R = s.st[1], x2L = s.st[2], x2R = s.st[3], y1L = s.st[4], y1R = s.st[5], y2L = s.st[6],
               y2R = s.st[7];
        for (int i = 0; i < n; ++i)
        {
            const double xl = buf[2 * i], xr = buf[2 * i + 1];
            const double yl = xl * s.b0 + (((x1L * s.b1 - y2L * s.a2) + x2L * s.b2) - y1L * s.a1);
            const double yr = xr * s.b0 + (((x1R * s.b1 - y2R * s.a2) + x2R * s.b2) - y1R * s.a1);
            buf[2 * i] = (float) yl;
            buf[2 * i + 1] = (float) yr;
            x2L = x1L, x2R = x1R, x1L = xl, x1R = xr;
            y2L = y1L, y2R = y1R, y1L = yl, y1R = yr;
        }
        s.st[0] = x1L, s.st[1] = x1R, s.st[2] = x2L, s.st[3] = x2R, s.st[4] = y1L, s.st[5] = y1R, s.st[6] = y2L, s.st[7] = y2R;
        for (int k = 0; k < 8; ++k)
            s.st[k] = flushD (s.st[k]);
    }

    static ld drv (ld v) noexcept
    {
        const ld a = std::fabs (v);
        const ld m = (3.0L + a) * a + 6.0L;
        return (v * m) / (a * m + 12.0L);
    }

    // x87: the state is kept as floats, the chain through the four poles in extended precision
    static void mango (Stage& s, float* buf, int n) noexcept
    {
        const X87Mode mode (false);
        float* p = s.mst;
        const ld fb = s.q, g = s.coef, og = s.gain, d = s.drive;
        const bool driven = s.drive != 0.0;
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                ld x = (ld) buf[2 * i + c] + (ld) p[8 + c] * fb;
                ld v;
                if (! driven)
                    v = (x - (ld) p[c]) * g + (ld) p[c];
                else
                {
                    const ld p0 = p[c];
                    v = p0 + (drv (x * d) - drv (p0 * d)) * g;
                }
                p[c] = (float) v;
                for (int k = 1; k < 3; ++k)
                {
                    const ld pk = p[2 * k + c];
                    v = driven ? pk + (drv (v * d) - drv (pk * d)) * g : pk + (v - pk) * g;
                    p[2 * k + c] = (float) v;
                }
                const ld old = p[6 + c];
                v = driven ? old + (drv (v * d) - drv (old * d)) * g : old + (v - old) * g;
                p[6 + c] = (float) v;
                const ld sum = v + old;
                buf[2 * i + c] = (float) (og * sum);
                p[8 + c] = (float) sum;
            }
        for (int k = 0; k < 10; ++k)
            p[k] = flushF (p[k]);
    }

    // waveshaper → dst (dry 0, wet 1)
    void shaper (const UnitRt& un, const float* x, float* y, int m) const noexcept
    {
        const bool interp = render || s.wsInterp;
        const auto* T = un.wsTab.data() + 1;
        const int N = un.wsN;
        // the scale factors are formed before the kernel switches the x87 rounding (nearest)
        ld preN, bK, hN, pHN;
        {
            const X87Mode nearest (false);
            volatile float vpre = un.wsPre;
            preN = (ld) vpre * (ld) N;
            bK = (ld) 1.0f * (ld) 3.0517578125e-05f;
            hN = (ld) N * (ld) 0.5f;
            pHN = (ld) vpre * (ld) N * (ld) 0.5f;
        }
        const X87Mode mode (interp);
        if (! un.wsUni)
        {
            for (int k = 0; k < m; ++k)
            {
                const ld X = x[k];
                const ld v = X * preN;
                ld t;
                if (! interp)
                {
                    const long long i = std::fabs (v) >= 2147483648.0L ? -2147483648LL : x87Int (v);
                    const long long ai = std::min<long long> (i < 0 ? -i : i, N);
                    t = i < 0 ? -(ld) T[ai] : (ld) T[ai];
                }
                else if (std::fabs (v) >= 2147483648.0L)
                    t = 0.0L;
                else
                {
                    const auto i = x87Int (v);
                    const ld fr = v - (ld) i;
                    if (i >= 0)
                    {
                        const auto j = std::min<long long> (i, N);
                        const ld t0 = T[j], t1 = T[j + 1];
                        t = t0 + (t1 - t0) * fr;
                    }
                    else
                    {
                        const auto j = std::min<long long> (-i, N);
                        const ld t0 = T[j], t1 = T[j + 1];
                        t = -(t0 - (t1 - t0) * fr);
                    }
                }
                y[k] = (float) (t * bK + X * 0.0L);
            }
            return;
        }
        for (int k = 0; k < m; ++k)
        {
            const ld X = x[k];
            const ld v = X * pHN + hN;
            if (! interp)
            {
                long long i = std::fabs (v) >= 2147483648.0L ? -2147483648LL : x87Int (v);
                i = std::clamp<long long> (i, 0, N);
                y[k] = (float) ((ld) T[i] * bK + X * 0.0L);
            }
            else
            {
                long long i = std::fabs (v) >= 2147483648.0L ? -2147483648LL : x87Int (v);
                const ld fr = v - (ld) i;
                i = std::clamp<long long> (i, -1, N);
                const ld t0 = T[i], t1 = T[i + 1];
                y[k] = (float) ((t0 + (t1 - t0) * fr) * bK + X * 0.0L);
            }
        }
    }

    static void panLaw (float pan, float vol, std::array<float, 4>& m) noexcept
    {
        if (pan == 0.0f)
        {
            m = { vol, vol, 0.0f, 0.0f };
            return;
        }
        if (vol == 0.0f)
        {
            m = { 0.0f, 0.0f, 0.0f, 0.0f };
            return;
        }
        float gl, gr;
        {
            const X87Mode mode (false);
            const ld t = (1.0L + (ld) pan) * (ld) 0.7853981633974483;
            const ld g = (ld) (vol * kSqrt2f);
            gl = (float) (x87Cos (t) * g);
            gr = (float) (x87Sin (t) * g);
        }
        float f3, f2;
        if (pan <= 0.0f)
        {
            f3 = 1.0f - gr / vol;
            f2 = 0.0f;
        }
        else
        {
            f3 = 0.0f;
            f2 = 1.0f - gl / vol;
        }
        m[2] = gl * f3 * 0.5f;
        m[0] = gl * (1.0f - f3 * 0.5f);
        m[3] = gr * f2 * 0.5f;
        m[1] = gr * (1.0f - f2 * 0.5f);
    }

    static void panMix (const float* x, float* y, int n, const std::array<float, 4>& g, const std::array<float, 4>& inc) noexcept
    {
        float LL = g[0], RR = g[1], RL = g[2], LR = g[3];
        if (inc[0] == 0.0f && inc[1] == 0.0f && inc[2] == 0.0f && inc[3] == 0.0f)
        {
            if (RL == 0.0f && LR == 0.0f)
            {
                if (LL != 0.0f || RR != 0.0f)
                    for (int i = 0; i < n; ++i)
                    {
                        y[2 * i] = x[2 * i] * LL + y[2 * i];
                        y[2 * i + 1] = x[2 * i + 1] * RR + y[2 * i + 1];
                    }
            }
            else
                for (int i = 0; i < n; ++i)
                {
                    const float l = x[2 * i], r = x[2 * i + 1];
                    y[2 * i] = l * LL + r * RL + y[2 * i];
                    y[2 * i + 1] = r * RR + l * LR + y[2 * i + 1];
                }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float l = x[2 * i], r = x[2 * i + 1];
            y[2 * i] = l * LL + r * RL + y[2 * i];
            y[2 * i + 1] = r * RR + l * LR + y[2 * i + 1];
            LL = LL + inc[0], RR = RR + inc[1], RL = RL + inc[2], LR = LR + inc[3];
        }
    }

    // ------------------------------------------------------------------------------------- one unit, one segment
    void runUnit (UnitRt& un, const float* in, float* acc, int n)
    {
        float* b = b2.data();
        // input: previous unit × NEXT + own input × IN
        bool have = false;
        if (bitsOf (un.next) != 0)
        {
            if (bitsOf (un.next) != bitsOf (1.0f))
                for (int i = 0; i < 2 * n; ++i)
                    b[i] = b[i] * un.next;
            have = true;
        }
        if (bitsOf (un.in) == 0)
        {
            if (! have)
                std::fill (b, b + 2 * n, 0.0f);
        }
        else if (have)
        {
            if (bitsOf (un.in) == bitsOf (1.0f))
                for (int i = 0; i < 2 * n; ++i)
                    b[i] = in[i] + b[i];
            else
                for (int i = 0; i < 2 * n; ++i)
                    b[i] = in[i] * un.in + b[i];
        }
        else if (bitsOf (un.in) == bitsOf (1.0f))
            std::memcpy (b, in, sizeof (float) * 2 * (std::size_t) n);
        else
            for (int i = 0; i < 2 * n; ++i)
                b[i] = in[i] * un.in;

        if (un.mode >= 0)
        {
            un.cut = un.c0 + un.art[2].out;
            un.res = un.r0 + un.art[3].out;
            const int t = un.type;
            if (t == 0)
            {
                std::array<float, 3> tg { un.L * un.art[4].out, un.B * un.art[5].out, un.H * un.art[6].out };
                if (un.n > 0)
                    for (auto& g : tg)
                        g = bitsOf (g) < 1 ? 0.0f : powf32 (g, un.qExp);
                coefs (un);
                if (firstTick)
                {
                    for (int k = 0; k < 3; ++k)
                        un.gr[(std::size_t) k].prev = tg[(std::size_t) k];
                    for (int i = 0; i <= un.n; ++i)
                    {
                        un.st[(std::size_t) i].rf.prev = un.st[(std::size_t) i].f;
                        un.st[(std::size_t) i].rq.prev = un.st[(std::size_t) i].q;
                    }
                }
                for (int k = 0; k < 3; ++k)
                    un.gr[(std::size_t) k].to (tg[(std::size_t) k], maxStep, n);
                for (int i = 0; i <= un.n; ++i)
                {
                    auto& stg = un.st[(std::size_t) i];
                    stg.rf.to (stg.f, maxStep, n);
                    stg.f = stg.rf.start;
                }
                {
                    auto& s0 = un.st[0];
                    s0.rq.to (s0.q, maxStep, n);
                    s0.q = s0.rq.start;
                }
                if (un.flat)
                    for (int i = 0; i <= un.n; ++i)
                    {
                        auto& stg = un.st[(std::size_t) i];
                        const float mx = stg.f <= stg.rf.prev ? stg.rf.prev : stg.f;
                        if (i < 1)
                            stg.q = (stg.q * un.k) / mx;
                        else
                            stg.q = un.k / mx;
                    }
                for (int i = 0; i <= un.n; ++i)
                {
                    auto& stg = un.st[(std::size_t) i];
                    const float dq = i == 0 ? stg.rq.inc : 0.0f;
                    svf (stg, b, b, b3.data(), b4.data(), n, stg.rf.inc, dq);
                    gainInPlace (b, n, un.gr[0].start, un.gr[0].inc);
                    addGained (b3.data(), b, n, un.gr[1].start, un.gr[1].inc);
                    addGained (b4.data(), b, n, un.gr[2].start, un.gr[2].inc);
                }
            }
            else if (t == 1 || t == 2)
            {
                coefs (un);
                if (firstTick)
                    for (int i = 0; i <= un.n; ++i)
                    {
                        un.st[(std::size_t) i].rf.prev = un.st[(std::size_t) i].f;
                        un.st[(std::size_t) i].rq.prev = un.st[(std::size_t) i].q;
                    }
                for (int i = 0; i <= un.n; ++i)
                {
                    auto& stg = un.st[(std::size_t) i];
                    stg.rf.to (stg.f, maxStep, n);
                    stg.f = stg.rf.start;
                    stg.rq.to (stg.q, maxStep, n);
                    stg.q = stg.rq.start;
                    lime (stg, b, n, stg.rf.inc, stg.rq.inc, t == 2);
                }
            }
            else if (t >= 3 && t <= 10)
            {
                if (t >= 8)
                {
                    const double x = (double) s.u[(std::size_t) un.index].p[3] * 0.015625;
                    const double e = dmath::exp ((double) F (x) * dmath::ln (8.0));
                    un.gain[0] = F ((double) F (e - 1.0) * 0.125 + 0.125);
                }
                coefs (un);
                for (int i = 0; i <= un.n; ++i)
                    biquad (un.st[(std::size_t) i], b, n);
            }
            else if (t == 11)
            {
                un.gain[0] = (float) s.u[(std::size_t) un.index].p[3] * 0.0078125f;
                coefs (un);
#ifdef LP_DEBUG
                std::fprintf (stderr, "mango %a %a %a %a %a %a\n", (double) un.cut, (double) un.res, un.st[0].coef, (double) un.st[0].q, (double) un.st[0].gain, un.st[0].drive);
#endif
                for (int i = 0; i <= un.n; ++i)
                {
#ifdef LP_DEBUG
                    static int calls = 0;
                    if (calls < 6)
                        std::fprintf (stderr, "mk in %a %a %a %a st %a %a %a %a %a\n", (double) b[0], (double) b[1], (double) b[16], (double) b[17], (double) un.st[0].mst[0], (double) un.st[0].mst[2], (double) un.st[0].mst[4], (double) un.st[0].mst[6], (double) un.st[0].mst[8]);
#endif
                    mango (un.st[(std::size_t) i], b, n);
#ifdef LP_DEBUG
                    if (calls++ < 6)
                        std::fprintf (stderr, "mk out %a %a %a %a\n", (double) b[0], (double) b[1], (double) b[16], (double) b[17]);
#endif
                }
            }
        }

        if (! un.wsTab.empty())
        {
            shaper (un, b, b3.data(), 2 * n);
            float m = un.art[7].out * un.wsMix;
            m = 0.0f <= m ? (1.0f < m ? 1.0f : m) : 0.0f;
            un.mix = m;
            if (firstTick)
                un.mixRamp.prev = m;
            un.mixRamp.to (m, maxStep, n);
            const float st = un.mixRamp.start, inc = un.mixRamp.inc;
            if (bitsOf (inc) == 0)
            {
                if (bitsOf (st) != 0)
                {
                    if (bitsOf (st) == bitsOf (1.0f))
                        std::memcpy (b, b3.data(), sizeof (float) * 2 * (std::size_t) n);
                    else
                    {
                        const float dry = 1.0f - st;
                        for (int i = 0; i < 2 * n; ++i)
                            b[i] = b[i] * dry;
                        for (int i = 0; i < 2 * n; ++i)
                            b[i] = b3[(std::size_t) i] * st + b[i];
                    }
                }
            }
            else
            {
                rampMul (b, b, n, 1.0f - st, -inc);
                rampAddProduct (b3.data(), b, n, st, st, inc, inc);
            }
        }

        if (un.follow1)
            follower (b, n);

        panLaw (un.pan + un.art[0].out * 2.0f, un.vol * un.art[1].out, un.pm);
#ifdef LP_DEBUG
        std::fprintf (stderr, "pan %d %.9g %.9g %.9g %.9g\n", un.index, (double) un.pm[0], (double) un.pm[1], (double) un.pm[2], (double) un.pm[3]);
#endif
        if (firstTick)
            for (int k = 0; k < 4; ++k)
                un.pr[(std::size_t) k].prev = un.pm[(std::size_t) k];
        std::array<float, 4> g, inc;
        for (int k = 0; k < 4; ++k)
        {
            un.pr[(std::size_t) k].to (un.pm[(std::size_t) k], maxStep, n);
            g[(std::size_t) k] = un.pr[(std::size_t) k].start;
            inc[(std::size_t) k] = un.pr[(std::size_t) k].inc;
        }
        panMix (b, acc, n, g, inc);
    }

    void ensure (int frames)
    {
        const auto need = (std::size_t) frames * 2 + 16;
        for (auto* v : { &bin, &bout, &bacc, &b2, &b3, &b4 })
            if (v->size() < need)
                v->assign (need, 0.0f);
    }

    void block (const float* x, float* y, int n)
    {
        const int ni = n * factor;
        ensure (ni);
        // silence detection on the host input (200 ms countdown) and the up-sampler
        bool silent = true;
        for (int i = 0; i < 2 * n && silent; ++i)
            silent = ((u32) bitsOf (x[i]) << 1) <= ((u32) bitsOf (9.999999974752427e-07f) << 1);
        if (! silent)
            silence = silenceInit;
        else
            silence -= n;
        const float* in = x;
        if (order > 0)
        {
            if (-n < silence)
            {
                const float* src = x;
                int m = n;
                for (int k = order - 1; k >= 0; --k)
                {
                    float* dst = src == bin.data() ? bout.data() : bin.data();
                    up[k].up (src, dst, m);
                    src = dst;
                    m *= 2;
                }
                if (src != bin.data())
                    std::memcpy (bin.data(), src, sizeof (float) * 2 * (std::size_t) ni);
            }
            else
                std::fill (bin.begin(), bin.begin() + 2 * ni, 0.0f);
            in = bin.data();
        }
        float* acc = bacc.data();
        std::fill (acc, acc + 2 * ni, 0.0f);

        int left = ni, pos = 0;
        while (left > 0)
        {
            if (counter < 1)
            {
                counter = tickN;
                controlTick();
            }
            const int seg = std::min (left, counter);
            if (follMode == 1)
                follower (in + 2 * pos, seg);
            for (const int i : list)
                runUnit (u[(std::size_t) i], in + 2 * pos, acc + 2 * pos, seg);
            counter -= seg;
            left -= seg;
            pos += seg;
            firstTick = false;
        }

        const float* res = acc;
        if (order > 0)
        {
            int m = ni;
            float* buf = acc;
            for (int k = order - 1; k >= 0; --k)
            {
                m /= 2;
                dn[k].down (buf, buf, m);
            }
            res = buf;
        }
        if (s.center)
        {
            const X87Mode mode (false);
            const ld A = (ld) dcA, Bc = (ld) dcB;
            for (int c = 0; c < 2; ++c)
            {
                ld yy = dcY[c], vv = dcV[c];
                for (int i = 0; i < n; ++i)
                {
                    const ld xx = res[2 * i + c];
                    vv = vv + (xx - yy) * A;
                    yy = yy + vv;
                    vv = vv * Bc;
                    y[2 * i + c] = (float) (xx - yy);
                }
                dcV[c] = (ld) (double) flushX87 (vv);
                dcY[c] = (ld) (double) flushX87 (yy);
            }
        }
        else
            std::memmove (y, res, sizeof (float) * 2 * (std::size_t) n);
    }
};

// ======================================================================================================= public
Engine::Engine (std::shared_ptr<const State> state) : d (std::make_unique<Impl> (std::move (state))) {}
Engine::~Engine() = default;

void Engine::setRenderMode (bool rendering) noexcept
{
    if (d->render == rendering)
        return;
    d->render = rendering;
    d->rateSetup();
}
void Engine::setRate (int sampleRate) noexcept
{
    d->fs = std::max (1, sampleRate);
    d->rateSetup();
}
void Engine::setTempo (double bpm) noexcept
{
    d->bpm = (float) bpm;
    d->timeScales();
}
void Engine::setPlaying (bool playing, double songBeats) noexcept
{
    d->playing = playing;
    d->songBeats = songBeats;
    d->resync (true);
}
void Engine::jump (double songBeats) noexcept
{
    d->songBeats = songBeats;
    d->resync (true);
}
void Engine::setParam (int k, int raw) noexcept
{
    auto& s = d->s;
    if (k < 0 || k >= 0x167)
        return;
    if (k < 8)
    {
        if (k < 3)
        {
            s.g[(std::size_t) k] = raw;
            d->globals();
        }
        else if (k < 5)
        {
            s.g[(std::size_t) k] = raw;
            d->follCoefs();
        }
        else if (k == 5)
        {
            if (s.g[5] == raw)
                return;
            s.g[5] = std::clamp (raw, 1, 10);
            d->selectPattern();
        }
        else if (k == 6)
            s.modX = raw;
        else
            s.modY = raw;
        return;
    }
    const int u = (k - 8) / 44, i = (k - 8) % 44;
    const int own = u == 7 ? 11 : 12;
    if (i < own)
    {
        s.u[(std::size_t) u].p[(std::size_t) i] = raw;
        d->unitParams (u);
        return;
    }
    const int a = (i - own) / 4, f = (i - own) % 4; // articulator LFO knob: speed, tension, skew, pulse width
    auto& knobs = s.u[(std::size_t) u].art[(std::size_t) a].lfoKnobs;
    knobs[(std::size_t) f] = raw;
    d->lfoSetup (u, a);
}
void Engine::render (const float* in, float* out, int n) noexcept
{
    if (n > 0)
        d->block (in, out, n);
}

// ----------------------------------------------------------------------------------------- slot parameters
namespace
{
constexpr int extraField[unitExtraFields] = { 3, 4, 5, 6, 7, 10, 11 }; // L, B, H, WS pre, WS mix, IN, NEXT
}

int flIndexOf (int param) noexcept
{
    if (param >= 48)
    {
        const int e = param - 48, u = e / unitExtraFields;
        return 8 + 44 * u + extraField[e % unitExtraFields];
    }
    if (param < 8)
        return param;
    const int u = (param - 8) / 5, k = (param - 8) % 5;
    static const int field[5] = { 1, 2, 0, 8, 9 };
    return 8 + 44 * u + field[k];
}
int paramOfFlIndex (int k) noexcept
{
    if (k < 0)
        return -1;
    if (k < 8)
        return k;
    const int u = (k - 8) / 44, i = (k - 8) % 44;
    if (u > 7)
        return -1;
    static const int slot[12] = { 2, 0, 1, -1, -1, -1, -1, -1, 3, 4, -1, -1 };
    static const int extra[12] = { -1, -1, -1, 0, 1, 2, 3, 4, -1, -1, 5, 6 };
    if (i >= 12 || (u == 7 && i == 11))
        return -1;
    if (slot[i] >= 0)
        return 8 + 5 * u + slot[i];
    return 48 + unitExtraFields * u + extra[i];
}
void rawRange (int k, int& lo, int& hi) noexcept
{
    static const int glo[8] = { 0, -128, 0, 0, 0, 1, 0, 0 }, ghi[8] = { 160, 128, 160, 16384, 16384, 10, 1024, 1024 };
    if (k < 8)
    {
        lo = glo[k], hi = ghi[std::max (0, k)];
        return;
    }
    const int i = (k - 8) % 44;
    static const int ulo[12] = { 0, 0, 0, 0, 0, 0, 64, 0, -128, 0, -160, -160 },
                     uhi[12] = { 256, 256, 256, 128, 128, 128, 192, 128, 128, 160, 160, 160 };
    lo = i < 12 ? ulo[i] : 0;
    hi = i < 12 ? uhi[i] : 1;
}
int rawParam (const State& s, int k) noexcept
{
    if (k < 6)
        return s.g[(std::size_t) k];
    if (k == 6)
        return s.modX;
    if (k == 7)
        return s.modY;
    const int u = (k - 8) / 44, i = (k - 8) % 44;
    if (u < 8 && i < 12)
        return s.u[(std::size_t) u].p[(std::size_t) i];
    return 0;
}

namespace
{
int rawFromValue (int k, float v) noexcept
{
    int lo, hi;
    rawRange (k, lo, hi);
    return lo + (int) std::lround (std::clamp ((double) v, 0.0, 1.0) * (hi - lo));
}
float valueFromRaw (int k, int raw) noexcept
{
    int lo, hi;
    rawRange (k, lo, hi);
    return (float) std::clamp ((double) (raw - lo) / (double) (hi - lo), 0.0, 1.0);
}

class PhilterEffect final : public Effect
{
public:
    explicit PhilterEffect (const EffectSlot& slot) : state (parse (slot.state)), last (slot.params)
    {
        if (! state)
            return;
        engine = std::make_unique<Engine> (state);
        // controls edited in LYNX since the import (the state keeps FL's original values)
        for (int i = 0; i < paramCount; ++i)
        {
            const int k = flIndexOf (i);
            if (slot.params[(std::size_t) i] != valueFromRaw (k, rawParam (*state, k)))
                engine->setParam (k, rawFromValue (k, slot.params[(std::size_t) i]));
        }
    }

    void prepare (double sr) noexcept override
    {
        sampleRate = (int) std::lround (sr);
        if (! engine)
            return;
        render = offlineRendering();
        engine->setRenderMode (render);
        engine->setRate (sampleRate);
        lastTicks = -1.0;
        playing = false;
    }

    void reset() noexcept override
    {
        if (engine && playing)
            engine->setPlaying (false, 0.0);
        playing = false;
        lastTicks = -1.0;
    }

    void setSongPosition (double ticks) noexcept override { songTicks = ticks; }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (! engine || n <= 0)
            return;
        if (offlineRendering() != render)
        {
            render = ! render;
            engine->setRenderMode (render);
        }
        // a control moved in LYNX (or automated): FL's raw value from the slot parameter; untouched controls keep the
        // state's own raw values
        for (int i = 0; i < paramCount; ++i)
            if (p[(std::size_t) i] != last[(std::size_t) i])
            {
                last[(std::size_t) i] = p[(std::size_t) i];
                const int k = flIndexOf (i);
                engine->setParam (k, rawFromValue (k, p[(std::size_t) i]));
            }
        if (bpm > 0.0 && bpm != lastBpm)
        {
            lastBpm = bpm;
            engine->setTempo (bpm);
        }
        // LYNX's transport: a moving song position is FL's "playing"; a position that does not follow the previous
        // block is a jump (FL re-syncs the envelopes and LFOs to the song position)
        const double beats = songTicks / 960.0;
        const bool moving = lastTicks >= 0.0 && songTicks != lastTicks;
        if (lastTicks < 0.0 || moving)
        {
            if (! playing)
            {
                playing = true;
                engine->setPlaying (true, beats);
            }
            else if (std::fabs (songTicks - expected) > 1.0)
                engine->jump (beats);
        }
        else if (playing)
        {
            playing = false;
            engine->setPlaying (false, beats);
        }
        lastTicks = songTicks;
        expected = songTicks + (double) n * bpm / 60.0 * 960.0 / (double) sampleRate;
        if ((int) io.size() < 2 * n)
            io.resize ((std::size_t) (2 * n));
        for (int i = 0; i < n; ++i)
        {
            io[(std::size_t) (2 * i)] = l[i];
            io[(std::size_t) (2 * i + 1)] = r[i];
        }
        engine->render (io.data(), io.data(), n);
        for (int i = 0; i < n; ++i)
        {
            l[i] = io[(std::size_t) (2 * i)];
            r[i] = io[(std::size_t) (2 * i + 1)];
        }
    }

private:
    std::shared_ptr<const State> state;
    std::unique_ptr<Engine> engine;
    EffectParams last {};
    std::vector<float> io;
    int sampleRate = 44100;
    bool render = false, playing = false;
    double songTicks = 0.0, lastTicks = -1.0, expected = 0.0, lastBpm = -1.0;
};

const char* const kUnitFields[5] = { "cutoff", "resonance", "envelope amount", "pan", "volume" };
const char* const kUnitExtra[unitExtraFields] = { "low", "band", "high", "shaper pre amp", "shaper mix", "input", "to next" };
} // namespace

const EffectInfo& info()
{
    static const EffectInfo inf = [] {
        static std::vector<std::string> names;
        static const char* const globals[8] = { "Master level", "Master LFO", "Follower level", "Follower attack",
                                                "Follower release", "Pattern", "Mod X", "Mod Y" };
        for (int i = 0; i < paramCount; ++i)
            names.push_back (i < 8    ? std::string (globals[i])
                             : i < 48 ? "Unit " + std::to_string ((i - 8) / 5 + 1) + " " + kUnitFields[(i - 8) % 5]
                                      : "Unit " + std::to_string ((i - 48) / unitExtraFields + 1) + " " + kUnitExtra[(i - 48) % unitExtraFields]);
        EffectInfo e { "Fruity Love Philter", {} };
        static const int defaults[8] = { 128, 128, 128, 4096, 8192, 1, 512, 512 };
        static const int unitDefaults[5] = { 128, 128, 128, 0, 128 };
        for (int i = 0; i < paramCount; ++i)
        {
            const int k = flIndexOf (i);
            static const int extraDefaults[unitExtraFields] = { 128, 0, 0, 128, 128, 128, 0 };
            const int def = i < 8 ? defaults[i] : i < 48 ? unitDefaults[(i - 8) % 5] : extraDefaults[(i - 48) % unitExtraFields];
            e.params.push_back ({ names[(std::size_t) i].c_str(), valueFromRaw (k, def) });
        }
        return e;
    }();
    return inf;
}

std::string paramText (int param, float value01)
{
    const int k = flIndexOf (param);
    const int v = rawFromValue (k, value01);
    char t[64];
    if (param < 8)
    {
        if (param == 0 || param == 2)
            std::snprintf (t, sizeof t, "%.1f dB", 20.0 * std::log10 (std::max (1e-6, (double) curve11 (v))));
        else if (param == 1)
            std::snprintf (t, sizeof t, "%.0f%%", 100.0 * scurve11 (v));
        else if (param == 3 || param == 4)
            std::snprintf (t, sizeof t, "%.1f ms", 12.5 * (std::pow (81.0, v / 16384.0) - 1.0));
        else if (param == 5)
            std::snprintf (t, sizeof t, "pattern %d", v);
        else
            std::snprintf (t, sizeof t, "%.0f%%", 100.0 * v / 1024.0);
        return t;
    }
    if (param >= 48)
    {
        const int f = (param - 48) % unitExtraFields;
        if (f == 5 || f == 6) // IN, NEXT: the signed level law
        {
            if (v == 0)
                return "-inf dB";
            std::snprintf (t, sizeof t, "%s%.1f dB", v < 0 ? "-" : "", 20.0 * std::log10 (std::max (1e-6, (double) std::abs (scurve11 (v)))));
            return t;
        }
        if (f == 3)
        {
            std::snprintf (t, sizeof t, "%.0f%%", 100.0 * v / 128.0);
            return t;
        }
        std::snprintf (t, sizeof t, "%.0f%%", 100.0 * v / 128.0);
        return t;
    }
    switch ((param - 8) % 5)
    {
        case 3:
            if (v == 0)
                return "centred";
            std::snprintf (t, sizeof t, "%.0f%% %s", std::abs (v) / 1.28, v < 0 ? "left" : "right");
            return t;
        case 4: std::snprintf (t, sizeof t, "%.1f dB", 20.0 * std::log10 (std::max (1e-6, (double) curve11 (v)))); return t;
        default: std::snprintf (t, sizeof t, "%.0f%%", 100.0 * v / 256.0); return t;
    }
}

std::optional<EffectSlot> slotFromFl (const std::vector<std::uint8_t>& flState)
{
    const auto s = parse (flState);
    if (! s)
        return std::nullopt;
    auto slot = makeEffectSlot (EffectKind::lovePhilter);
    slot.state = flState;
    for (int i = 0; i < paramCount; ++i)
    {
        const int k = flIndexOf (i);
        slot.params[(std::size_t) i] = valueFromRaw (k, rawParam (*s, k));
    }
    return slot;
}

std::unique_ptr<Effect> make (const EffectSlot& slot) { return std::make_unique<PhilterEffect> (slot); }

// ------------------------------------------------------------------------------------------- the editable state
namespace
{
struct DocReader
{
    const std::vector<std::uint8_t>& b;
    std::size_t pos = 0;
    bool failed = false;
    bool take (void* out, std::size_t n)
    {
        if (failed || n > b.size() - pos)
            return ! (failed = true);
        std::memcpy (out, b.data() + pos, n);
        pos += n;
        return true;
    }
    template <typename T>
    T get()
    {
        T v {};
        take (&v, sizeof (T));
        return v;
    }
    std::vector<std::uint8_t> bytes (std::size_t n)
    {
        std::vector<std::uint8_t> v;
        if (failed || n > b.size() - pos)
        {
            failed = true;
            return v;
        }
        v.assign (b.begin() + (std::ptrdiff_t) pos, b.begin() + (std::ptrdiff_t) (pos + n));
        pos += n;
        return v;
    }
};

struct DocWriter
{
    std::vector<std::uint8_t> b;
    void put (const void* p, std::size_t n) { b.insert (b.end(), (const std::uint8_t*) p, (const std::uint8_t*) p + n); }
    template <typename T>
    void add (T v) { put (&v, sizeof (T)); }
};

bool readDocEnv (DocReader& r, DocEnv& e)
{
    e.kind = r.get<std::int32_t>();
    e.ver = r.get<std::int32_t>();
    const auto n = r.get<std::int32_t>();
    if (r.failed || n < 0 || n > 100000)
        return false;
    e.pts.assign ((std::size_t) n, DocPoint {});
    for (auto& p : e.pts)
    {
        if (e.ver < 2)
        {
            p.dt = e.kind == 6 ? r.get<double>() : (double) r.get<float>();
            p.y = (double) r.get<float>();
        }
        else
        {
            p.dt = r.get<double>();
            p.y = r.get<double>();
        }
        p.tension = r.get<float>();
        if (e.ver >= 1)
        {
            p.mode = r.get<std::uint8_t>();
            r.take (p.pad.data(), 3);
        }
    }
    e.flags = r.get<std::int32_t>();
    e.on = r.get<std::int32_t>();
    e.decay = r.get<std::int32_t>();
    e.loop = r.get<std::int32_t>();
    e.sus = r.get<std::int32_t>();
    return ! r.failed;
}

void writeDocEnv (DocWriter& w, const DocEnv& e)
{
    w.add<std::int32_t> (e.kind);
    w.add<std::int32_t> (e.ver);
    w.add<std::int32_t> ((std::int32_t) e.pts.size());
    for (const auto& p : e.pts)
    {
        if (e.ver < 2)
        {
            if (e.kind == 6)
                w.add<double> (p.dt);
            else
                w.add<float> ((float) p.dt);
            w.add<float> ((float) p.y);
        }
        else
        {
            w.add<double> (p.dt);
            w.add<double> (p.y);
        }
        w.add<float> (p.tension);
        if (e.ver >= 1)
        {
            w.add<std::uint8_t> (p.mode);
            w.put (p.pad.data(), 3);
        }
    }
    for (const auto v : { e.flags, e.on, e.decay, e.loop, e.sus })
        w.add<std::int32_t> (v);
}

// the options block's size by version (parse()) and where USE1 sits in `head`
std::size_t optionsSize (int v) noexcept { return v >= 11 ? 0x21 : v == 10 ? 0x20 : 0x1f; }
constexpr std::size_t optionsAt = 24;
} // namespace

int Doc::global (int i) const noexcept
{
    std::int32_t v = 0;
    if (i >= 0 && i < 6 && head.size() >= 24)
        std::memcpy (&v, head.data() + 4 * i, 4);
    return v;
}
void Doc::setGlobal (int i, int raw) noexcept
{
    const std::int32_t v = raw;
    if (i >= 0 && i < 6 && head.size() >= 24)
        std::memcpy (head.data() + 4 * i, &v, 4);
}
int Doc::option (int i) const noexcept
{
    std::int32_t v = 0;
    if (i >= 0 && optionsAt + 4 * (std::size_t) i + 4 <= std::min (head.size(), optionsAt + 28))
        std::memcpy (&v, head.data() + optionsAt + 4 * (std::size_t) i, 4);
    return v;
}
void Doc::setOption (int i, int raw) noexcept
{
    const std::int32_t v = raw;
    if (i >= 0 && optionsAt + 4 * (std::size_t) i + 4 <= std::min (head.size(), optionsAt + 28))
        std::memcpy (head.data() + optionsAt + 4 * (std::size_t) i, &v, 4);
}
int Doc::optionByte (int offset) const noexcept
{
    const auto at = optionsAt + (std::size_t) offset;
    return offset >= 28 && (std::size_t) offset < optionsSize (version) && at < head.size() ? head[at] : 0;
}
void Doc::setOptionByte (int offset, int value) noexcept
{
    const auto at = optionsAt + (std::size_t) offset;
    if (offset >= 28 && (std::size_t) offset < optionsSize (version) && at < head.size())
        head[at] = (std::uint8_t) value;
}
int Doc::use1() const noexcept
{
    const auto at = optionsAt + optionsSize (version);
    return version >= 2 && at < head.size() ? head[at] : 0;
}
void Doc::setUse1 (int v) noexcept
{
    const auto at = optionsAt + optionsSize (version);
    if (version >= 2 && at < head.size())
        head[at] = (std::uint8_t) (v != 0);
}

const char* typeShortName (int type) noexcept
{
    static const char* const names[12] = { "SVF", "LP", "HP", "LP2", "BP", "BS", "HP2", "AP", "LS", "PK", "HS", "LP3" };
    return names[std::clamp (type, 0, 11)];
}

std::optional<Doc> readDoc (const std::vector<std::uint8_t>& blob)
{
    if (parse (blob) == nullptr) // (the same acceptance as the engine's)
        return std::nullopt;
    Doc d;
    std::memcpy (&d.versionWord, blob.data(), 4);
    d.version = (int) (d.versionWord > 0xffffu ? d.versionWord >> 16 : d.versionWord);
    const auto body = inflateZlib (blob.data() + 4, blob.size() - 4, false, 16u << 20);
    if (! body)
        return std::nullopt;
    DocReader r { *body };
    // head: 6 globals, the options block, USE1 + an int (v ≥ 2), the name (v < 6: int n + n bytes; else a length byte
    // or 255 + int, then UTF-16)
    std::size_t n = optionsAt + optionsSize (d.version);
    r.pos = n;
    if (d.version >= 2)
        r.pos += 5;
    if (d.version < 6)
    {
        const auto k = r.get<std::int32_t>();
        if (k < 0)
            return std::nullopt;
        r.pos += (std::size_t) k;
    }
    else
    {
        std::size_t k = r.get<std::uint8_t>();
        if (k == 255)
        {
            const auto big = r.get<std::int32_t>();
            if (big < 0)
                return std::nullopt;
            k = (std::size_t) big;
        }
        r.pos += 2 * k;
    }
    if (r.failed || r.pos > body->size())
        return std::nullopt;
    d.head.assign (body->begin(), body->begin() + (std::ptrdiff_t) r.pos);
    for (auto& u : d.u)
    {
        for (auto& p : u.p)
            p = r.get<std::int32_t>();
        u.mode = r.get<std::int32_t>();
        u.type = r.get<std::int32_t>();
        u.flat = r.get<std::uint8_t>();
        u.enabled = r.get<std::uint8_t>();
        u.wsOn = r.get<std::uint8_t>();
        if (! readDocEnv (r, u.ws))
            return std::nullopt;
        u.wsBip = r.get<std::uint8_t>();
        for (auto& a : u.art)
        {
            a.lead = r.get<std::int32_t>();
            a.base = r.get<float>();
            a.envDepth = r.get<float>();
            a.lfoDepth = r.get<float>();
            for (int e = 0; e < 10; ++e)
            {
                if (! readDocEnv (r, a.pat[(std::size_t) e]))
                    return std::nullopt;
                r.take (a.patKnobs[(std::size_t) e].data(), 16);
            }
            if (! readDocEnv (r, a.lfo))
                return std::nullopt;
            a.lfoKnobs.resize (a.lfo.ver >= 3 ? 5 : 4);
            for (auto& k : a.lfoKnobs)
                k = r.get<std::int32_t>();
            for (auto& m : a.maps)
                if (! readDocEnv (r, m))
                    return std::nullopt;
        }
    }
    if (r.failed)
        return std::nullopt;
    d.tail.assign (body->begin() + (std::ptrdiff_t) r.pos, body->end());
    return d;
}

std::vector<std::uint8_t> writeDoc (const Doc& d)
{
    DocWriter w;
    w.put (d.head.data(), d.head.size());
    for (const auto& u : d.u)
    {
        for (const auto p : u.p)
            w.add<std::int32_t> (p);
        w.add<std::int32_t> (u.mode);
        w.add<std::int32_t> (u.type);
        w.add<std::uint8_t> (u.flat);
        w.add<std::uint8_t> (u.enabled);
        w.add<std::uint8_t> (u.wsOn);
        writeDocEnv (w, u.ws);
        w.add<std::uint8_t> (u.wsBip);
        for (const auto& a : u.art)
        {
            w.add<std::int32_t> (a.lead);
            w.add<float> (a.base);
            w.add<float> (a.envDepth);
            w.add<float> (a.lfoDepth);
            for (int e = 0; e < 10; ++e)
            {
                writeDocEnv (w, a.pat[(std::size_t) e]);
                w.put (a.patKnobs[(std::size_t) e].data(), 16);
            }
            writeDocEnv (w, a.lfo);
            for (const auto k : a.lfoKnobs)
                w.add<std::int32_t> (k);
            for (const auto& m : a.maps)
                writeDocEnv (w, m);
        }
    }
    w.put (d.tail.data(), d.tail.size());
    std::vector<std::uint8_t> out (4);
    std::memcpy (out.data(), &d.versionWord, 4);
    const auto z = zlibStored (w.b);
    out.insert (out.end(), z.begin(), z.end());
    return out;
}

std::vector<float> curveShape (const DocEnv& e, bool map, int count, double span)
{
    std::vector<float> out ((std::size_t) std::max (count, 2), 0.0f);
    Env env;
    env.kind = e.kind, env.ver = e.ver, env.flags = e.flags, env.on = e.on, env.decay = e.decay, env.loop = e.loop, env.sus = e.sus;
    float prevY = 0.0f;
    for (const auto& q : e.pts)
    {
        Pt p;
        p.dt = (float) q.dt;
        p.y = std::isnan (q.y) ? prevY : (float) q.y;
        prevY = p.y;
        p.t = q.tension;
        p.mode = q.mode;
        env.pts.push_back (p);
    }
    Curve c;
    c.build (env, 0.0, map);
    c.scale = 1.0;
    const auto n = (int) out.size();
    c.table (out.data(), n, map ? 1.0 / (double) (n - 1) : std::max (1.0e-9, span / (double) (n - 1)));
    return out;
}

void upgradeParams (EffectSlot& slot, std::size_t savedCount) noexcept
{
    if (savedCount > 48)
        return;
    const auto s = parse (slot.state);
    for (int i = 48; i < paramCount; ++i)
    {
        const int k = flIndexOf (i);
        slot.params[(std::size_t) i] = s ? valueFromRaw (k, rawParam (*s, k)) : info().params[(std::size_t) i].defaultValue;
    }
}
} // namespace lynx::philter
