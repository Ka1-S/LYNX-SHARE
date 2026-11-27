#include "core/FruityDelayBank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Effects.h"
#include "core/StateHandoff.h"

// Bit-exactness with FL's plugin needs every float operation in the plugin's order and precision: no contraction into
// fused multiply-adds, and the x87 extended precision of its pan law kept (long double; 80-bit on every x86 target LYNX
// is built for).
#pragma GCC optimize ("fp-contract=off")
#pragma STDC FP_CONTRACT OFF

namespace lynx::fldbank
{
namespace
{
std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

// cvtsd2si: round to nearest, ties to even (the default SSE mode in FL and in LYNX)
int roundEven (double x) noexcept { return (int) std::nearbyint (x); }

std::uint32_t bitsOf (float f) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}

constexpr float flushLevel = 5.9604644775390625e-08f; // 2^-24: smaller magnitudes are set to 0 (states, loop samples)
constexpr float k128 = 0.0078125f;

const std::array<int, 20> ctlMin { 0, 0, 0, 0, -128, -160, 0, 0, 0, -128, -128, -128, 0, -512, 0, 0, 0, -128, -160, -160 };
const std::array<int, 20> ctlMax { 128, 128, 128, 128, 128, 160, 65536, 65536, 256, 128, 128, 128, 7680, 512, 65536, 65536, 256, 128, 160, 160 };

int subOf (int index) noexcept { return index < 4 ? index : index - 16 * ((index - 4) >> 4); }

// FL's volume law: 1 at the unit position, else (11^x − 1)/10 (x = raw/128, ln 11 held as a float)
float volumeLaw (float x) noexcept
{
    if (x == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (x * 2.39789534f)) - 1.0) * (double) 0.1f);
}
float volume (int raw) noexcept { return volumeLaw ((float) raw * k128); }
// signed controls: the law on |x| with the sign of x
float signedVolume (int raw) noexcept
{
    const auto x = (float) raw * k128;
    const auto mag = (float) std::fabs ((double) x);
    const auto sign = x == 0.0f ? 0.0f : (std::signbit (x) ? -1.0f : 1.0f);
    return volumeLaw (mag) * sign;
}

// ---- 2×2 gain matrices: c = {LL, RR, RL, LR}: outL = L·c0 + R·c2, outR = R·c1 + L·c3 -----------------------------

// sin/cos pan law (x87): L = cos((1 + pan)·π/4)·v√2, R = sin(…)·v√2, in extended precision, stored as floats. The
// plugin uses the FPU's own fsincos under a 64-bit-precision control word; LYNX does the same where it can (the
// library cosl/sinl differ from fsincos near π/2 by ~1e-20, i.e. only in the cross term of a hard pan)
void panLaw (float pan, float vol, float& l, float& r) noexcept
{
    const auto v = vol * 1.41421354f;
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
    unsigned short saved = 0, cw = 0;
    asm volatile ("fnstcw %0" : "=m"(saved) : : "memory");
    cw = (unsigned short) ((saved & ~0x0F00u) | 0x0300u); // 64-bit mantissa, round to nearest
    asm volatile ("fldcw %0" : : "m"(cw) : "memory");
    const long double t = (1.0L + (long double) pan) * (long double) 0.78539816339744828;
    long double c, s;
    asm ("fsincos" : "=t"(c), "=u"(s) : "0"(t));
    l = (float) (c * (long double) v);
    r = (float) (s * (long double) v);
    asm volatile ("fldcw %0" : : "m"(saved) : "memory");
#else
    const long double t = (1.0L + (long double) pan) * (long double) 0.78539816339744828;
    l = (float) (std::cos (t) * (long double) v);
    r = (float) (std::sin (t) * (long double) v);
#endif
}

// input / output stage: pan then the cross-feed that keeps the panned-away channel
void ioMatrix (float* c, float pan, float vol) noexcept
{
    if (bitsOf (pan) == 0)
    {
        c[0] = c[1] = vol;
        c[2] = c[3] = 0.0f;
        return;
    }
    if (vol == 0.0f)
    {
        c[0] = c[1] = c[2] = c[3] = 0.0f;
        return;
    }
    panLaw (pan, vol, c[0], c[1]);
    float x0 = 0.0f, x1 = 0.0f;
    if (pan > 0.0f)
        x1 = 1.0f - c[0] / vol;
    else
        x0 = 1.0f - c[1] / vol;
    x0 = x0 * 0.5f;
    c[2] = c[0] * x0;
    c[0] = c[0] * (1.0f - x0);
    x1 = x1 * 0.5f;
    c[3] = c[1] * x1;
    c[1] = c[1] * (1.0f - x1);
}

// feedback stage: the same with a "cross" amount b (b > 0: 0.25·(5^b − 1))
void feedbackMatrix (float* c, float pan, float vol, float b) noexcept
{
    if ((bitsOf (pan) | bitsOf (b)) == 0)
    {
        c[0] = c[1] = vol;
        c[2] = c[3] = 0.0f;
        return;
    }
    if (vol == 0.0f)
    {
        c[0] = c[1] = c[2] = c[3] = 0.0f;
        return;
    }
    panLaw (pan, vol, c[0], c[1]);
    float x0 = 0.0f, x1 = 0.0f;
    if (pan != 0.0f)
    {
        if (pan > 0.0f)
            x1 = 1.0f - c[0] / vol;
        else
            x0 = 1.0f - c[1] / vol;
    }
    if (b != 0.0f)
    {
        if (b > 0.0f)
        {
            static const double ln5 = std::nextafter (std::log (5.0), 2.0); // the plugin's run-time Ln (5) (EXEC)
            b = (float) (std::exp ((double) b * ln5) - 1.0) * 0.25f;
        }
        x0 = x0 + b;
        x1 = x1 + b;
        if (x0 > 1.0f)
            x0 = 1.0f;
        if (x1 > 1.0f)
            x1 = 1.0f;
    }
    x0 = x0 * 0.5f;
    c[2] = c[0] * x0;
    c[0] = c[0] * (1.0f - x0);
    x1 = x1 * 0.5f;
    c[3] = c[1] * x1;
    c[1] = c[1] * (1.0f - x1);
}

// FL's linear gain ramp toward a target over n samples, the step limited to `maxStep` per sample
void ramp (float& cur, float target, float maxStep, int n, float& start, float& inc) noexcept
{
    start = cur;
    inc = target - start;
    if (bitsOf (inc) == 0)
        return;
    cur = target;
    inc = inc / (float) n;
    if (std::fabs (inc) > maxStep)
    {
        inc = std::copysign (maxStep, inc);
        cur = start + inc * (float) n;
        if ((bitsOf (cur) & 0x7fffffffu) < 0x33800000u)
        {
            cur = 0.0f;
            inc = -start / (float) n;
        }
    }
}

struct Ramp4
{
    float start[4] {}, inc[4] {};
    bool moving() const noexcept { return (bitsOf (inc[0]) | bitsOf (inc[1]) | bitsOf (inc[2]) | bitsOf (inc[3])) != 0; }
};

void ramp4 (float* cur, const float* target, float maxStep, int n, Ramp4& r) noexcept
{
    for (int k = 0; k < 4; ++k)
        ramp (cur[k], target[k], maxStep, n, r.start[k], r.inc[k]);
}

// dst = M·src (interleaved frames), the coefficients stepping by inc after every frame
void applyMatrix (const float* src, float* dst, int n, const Ramp4& r, bool add) noexcept
{
    float c0 = r.start[0], c1 = r.start[1], c2 = r.start[2], c3 = r.start[3];
    const bool moving = r.moving();
    if (! moving && add && ((bitsOf (c2) | bitsOf (c3)) == 0) && ((bitsOf (c0) | bitsOf (c1)) == 0))
        return;
    for (int i = 0; i < n; ++i)
    {
        const auto l = src[2 * i], rr = src[2 * i + 1];
        const auto a = l * c0, b = rr * c1, e = rr * c2, f = l * c3;
        const auto ol = a + e, orr = b + f;
        if (add)
        {
            dst[2 * i] = ol + dst[2 * i];
            dst[2 * i + 1] = orr + dst[2 * i + 1];
        }
        else
        {
            dst[2 * i] = ol;
            dst[2 * i + 1] = orr;
        }
        if (moving)
        {
            c0 = c0 + r.inc[0];
            c1 = c1 + r.inc[1];
            c2 = c2 + r.inc[2];
            c3 = c3 + r.inc[3];
        }
    }
}

// ---- biquad cascades --------------------------------------------------------------------------------------------

struct Section
{
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};
};

struct Filter
{
    int type = 0;  // state type 0…7 (0 = off)
    int order = 0; // extra sections
    int kernel = 0;
    float f168 = 1.0f, f16c = 1.0f;
    std::vector<Section> sec;

    void run (float* buf, int n) noexcept
    {
        for (auto& s : sec)
        {
            for (int c = 0; c < 2; ++c)
            {
                double x1 = s.x1[c], x2 = s.x2[c], y1 = s.y1[c], y2 = s.y2[c];
                for (int i = 0; i < n; ++i)
                {
                    auto acc = x1 * s.b1;
                    acc = acc - y2 * s.a2;
                    acc = acc + x2 * s.b2;
                    acc = acc - y1 * s.a1;
                    const auto x = (double) buf[2 * i + c];
                    const auto y = x * s.b0 + acc;
                    buf[2 * i + c] = (float) y;
                    x2 = x1;
                    x1 = x;
                    y2 = y1;
                    y1 = y;
                }
                auto flush = [] (double v) { return std::fabs (v) < (double) flushLevel ? 0.0 : v; };
                s.x1[c] = flush (x1);
                s.x2[c] = flush (x2);
                s.y1[c] = flush (y1);
                s.y2[c] = flush (y2);
            }
        }
    }
    void clear() noexcept
    {
        for (auto& s : sec)
            for (int c = 0; c < 2; ++c)
                s.x1[c] = s.x2[c] = s.y1[c] = s.y2[c] = 0.0;
        f168 = (float) (order + 1);
        f16c = (float) (1.0 / (double) (order + 1));
        if (kernel == 3)
            f168 = 1.0f / f168;
    }
};

const int kernelOfType[8] { 0, 1, 2, 3, 4, 6, 7, 8 };

// One section's coefficients (cookbook forms; g = gain, a = the alpha term, s/c = sin/cos of the frequency)
void designSection (Section& q, int kernel, double g, double a, double s, double c) noexcept
{
    (void) s;
    if (kernel >= 1 && kernel <= 5)
    {
        const auto d = 1.0 / (a + 1.0);
        q.a1 = d * c * -2.0;
        q.a2 = d * (1.0 - a);
        switch (kernel)
        {
            case 1:
                q.b1 = d * (1.0 - c);
                q.b0 = q.b1 * (double) 0.5f;
                q.b2 = q.b0;
                break;
            case 2:
                q.b1 = 0.0;
                q.b0 = d * a;
                q.b2 = d * -a;
                break;
            case 3:
                q.b1 = q.a1;
                q.b0 = d;
                q.b2 = d;
                break;
            case 4:
                q.b1 = d * -(c + 1.0);
                q.b0 = -q.b1 * (double) 0.5f;
                q.b2 = q.b0;
                break;
            default:
                q.b1 = q.a1;
                q.b0 = 1.0;
                q.b2 = q.a2;
                break;
        }
        return;
    }
    if (kernel == 6)
    {
        const auto t = (g - 1.0) * c;
        const auto e = a * s;
        const auto d = 1.0 / (g + 1.0 + t + e);
        q.a1 = d * ((g - 1.0) + (g + 1.0) * c) * -2.0;
        q.a2 = d * ((g + 1.0 + t) - e);
        q.b1 = d * ((g * 2.0) * ((g - 1.0) - (g + 1.0) * c));
        q.b0 = d * (g * (((g + 1.0) - t) + e));
        q.b2 = d * (g * (((g + 1.0) - t) - e));
    }
    else if (kernel == 7)
    {
        const auto d = 1.0 / (a / g + 1.0);
        q.a1 = d * c * -2.0;
        q.a2 = d * (1.0 - a / g);
        q.b1 = q.a1;
        q.b0 = d * (a * g + 1.0);
        q.b2 = d * (1.0 - a * g);
    }
    else if (kernel == 8)
    {
        const auto t = (g - 1.0) * c;
        const auto e = a * s;
        const auto d = 1.0 / (((g + 1.0) - t) + e);
        q.a1 = d * ((g - 1.0) - (g + 1.0) * c) * 2.0;
        q.a2 = d * (((g + 1.0) - t) - e);
        q.b1 = d * ((g * -2.0) * ((g - 1.0) + (g + 1.0) * c));
        q.b0 = d * (g * (((g + 1.0) + t) + e));
        q.b2 = d * (g * (((g + 1.0) + t) - e));
    }
}

// Delphi's Power for the positive bases used here
double power (double base, double e) noexcept { return std::exp (e * std::log (base)); }

// FL's control → filter design (frequency, resonance / bandwidth / slope, gain) for the bank's three controls
void designFilter (Filter& f, int cutoffRaw, int resRaw, int gainRaw, double rateRatio) noexcept
{
    f.kernel = f.type >= 0 && f.type < 8 ? kernelOfType[f.type] : 0;
    f.sec.resize ((std::size_t) f.order + 1);
    if (f.kernel == 0)
        return;
    const auto cut = (double) cutoffRaw * 1.52587890625e-05;
    auto res = (double) resRaw * 1.52587890625e-05;
    const auto w0 = rateRatio * 6.283185307179586;
    auto freq = [&] (double scale, double top) {
        if (! (0.0 < cut))
            return 0.0;
        auto v = (double) (float) (std::exp ((double) (float) cut * std::log (1501.0)) - 1.0) * scale;
        return v > top ? top : v;
    };
    auto omega = [&] (double fr, double add) {
        auto w = (fr + add) * w0;
        if (w > 2.849517146113191)
            w = 2.849517146113191;
        else if (w < 0.0)
            w = 0.0;
        return w;
    };
    auto law = [] (double x, double lnBase, double scale, double add) {
        return (double) (float) (std::exp ((double) (float) x * lnBase) - 1.0) * scale + add;
    };
    const int n = f.order;
    switch (f.kernel)
    {
        case 1:
        case 4:
        {
            const auto fr = f.kernel == 1 ? freq (0.0006646666442975402, 1e300) : freq (0.00039993334212340415, 0.6001);
            double q = res <= 0.0 ? 0.1 : (res >= 1.0 ? 50.0 : law (res, std::log (2001.0), 0.024950001388788223, 0.1));
            if (n > 0)
                q = power (q, (double) f.f16c);
            const auto k = 0.5 / q;
            const auto w = omega (fr, f.kernel == 1 ? 0.003 : 0.0001);
            const auto s = std::sin (w), c = std::cos (w);
            for (auto& q2 : f.sec)
                designSection (q2, f.kernel, 0.0, s * k, s, c);
            break;
        }
        case 2:
        case 3:
        {
            const auto fr = freq (0.00039993334212340415, 1.0001);
            const double bw = res <= 0.0 ? 0.01 : (res >= 1.0 ? 12.0 : law (res, std::log (101.0), 0.11989999562501907, 0.01));
            const auto w = omega (fr, 0.0001);
            const auto s = std::sin (w), c = std::cos (w);
            // the plugin's run-time Sinh behaves as (e^x − e^−x)/2 (EXEC: matches on 99.7 % of arguments, libm sinh on 39 %)
            const auto x = (bw * (double) f.f168 * w) / s;
            const auto a = s * ((std::exp (x) - std::exp (-x)) * 0.5);
            for (auto& q2 : f.sec)
                designSection (q2, f.kernel, 0.0, a, s, c);
            break;
        }
        case 7:
        {
            const auto fr = freq (0.00039993334212340415, 1.0001);
            res = 1.0 - res;
            const double q = res <= 0.0 ? 0.1 : (res >= 1.0 ? 100.0 : law (res, std::log (2001.0), 0.049949999898672104, 0.1));
            auto g = law ((float) gainRaw * k128, std::log (1.1111111640930176), 8.1, 0.1);
            if (n > 0)
                g = power (g, (double) f.f16c);
            const auto w = omega (fr, 0.0001);
            const auto s = std::sin (w), c = std::cos (w);
            for (auto& q2 : f.sec)
                designSection (q2, 7, g, s / (q * 2.0), s, c);
            break;
        }
        case 6:
        case 8:
        {
            const auto fr = freq (0.00039993334212340415, 1.0001);
            res = 1.0 - res;
            const double slope = res <= 0.0 ? 0.1 : (res >= 1.0 ? 1.0 : law (res, std::log (3.0), 0.44999998807907104, 0.1));
            auto g = law ((float) gainRaw * k128, std::log (1.1111111640930176), 8.1, 0.1);
            if (n > 0)
                g = power (g, (double) f.f16c);
            const auto a = std::sqrt (g) * std::sqrt ((g + 1.0 / g) * (1.0 / slope - 1.0) + 2.0);
            const auto w = omega (fr, 0.0001);
            const auto s = std::sin (w), c = std::cos (w);
            for (auto& q2 : f.sec)
                designSection (q2, f.kernel, g, a, s, c);
            break;
        }
        default: break;
    }
}

// ---- the half-band polyphase all-pass resampler of the oversampling option --------------------------------------

constexpr double halfBand[6] { 0.14737711360104661, 0.039151597734460045, 0.48246854276970014,
                               0.30264684832849342, 0.88300502576937312, 0.6746159185469639 };

struct HalfBandStage
{
    double s[12][2] {}; // all-pass outputs 0…5, then their inputs 6…11 (per channel)

    // one all-pass step: y = (x − y_prev)·c + x_prev
    static double ap (double x, double& y, double& xPrev, double c) noexcept
    {
        const auto old = xPrev;
        xPrev = x;
        y = (x - y) * c + old;
        return y;
    }
    void up (const float* src, float* dst, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const auto x = (double) src[2 * i + c];
                const auto a1 = ap (x, s[0][c], s[6][c], halfBand[0]);
                const auto b1 = ap (x, s[3][c], s[9][c], halfBand[1]);
                const auto a2 = ap (a1, s[1][c], s[7][c], halfBand[2]);
                const auto b2 = ap (b1, s[4][c], s[10][c], halfBand[3]);
                const auto a3 = ap (a2, s[2][c], s[8][c], halfBand[4]);
                const auto b3 = ap (b2, s[5][c], s[11][c], halfBand[5]);
                dst[4 * i + 2 + c] = (float) a3;
                dst[4 * i + c] = (float) b3;
            }
        flush();
    }
    void down (const float* src, float* dst, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const auto a1 = ap ((double) src[4 * i + c], s[0][c], s[6][c], halfBand[0]);
                const auto b1 = ap ((double) src[4 * i + 2 + c], s[3][c], s[9][c], halfBand[1]);
                const auto a2 = ap (a1, s[1][c], s[7][c], halfBand[2]);
                const auto b2 = ap (b1, s[4][c], s[10][c], halfBand[3]);
                const auto a3 = ap (a2, s[2][c], s[8][c], halfBand[4]);
                const auto b3 = ap (b2, s[5][c], s[11][c], halfBand[5]);
                dst[2 * i + c] = (float) ((b3 + a3) * 0.5);
            }
        flush();
    }
    void flush() noexcept // the plugin clears the six all-pass outputs below 2^-24 after every block
    {
        for (int k = 0; k < 6; ++k)
            for (int c = 0; c < 2; ++c)
                if (std::fabs (s[k][c]) < (double) flushLevel)
                    s[k][c] = 0.0;
    }
};

// ---- gain pattern: one raised-cosine bump per step, shaped by the amount ---------------------------------------

std::vector<float> patternTable (int len, float amount)
{
    std::vector<float> t ((std::size_t) std::max (len, 0));
    const int m = len - 1;
    const int h = m / 2, h2 = m - h;
    const auto s1 = (float) (3.141592653589793 / (double) h);
    const auto s2 = (float) (3.141592653589793 / (double) h2);
    auto e = (float) (std::exp ((double) (float) std::fabs ((double) amount) * std::log (10001.0)) - 1.0);
    const auto sign = bitsOf (amount) << 1 == 0 ? 0.0f : (std::signbit (amount) ? -1.0f : 1.0f);
    e = e * sign;
    float inv = 0.0f, lnE = 0.0f;
    if (e != 0.0f)
    {
        inv = (float) (1.0 / std::fabs ((double) e));
        lnE = (float) std::log (std::fabs ((double) e) + 1.0);
    }
    for (int i = 0; i <= m; ++i)
    {
        float x;
        if (i < h)
            x = s1 * (float) i;
        else
            x = (float) ((double) (s2 * (float) (i - h)) + 3.141592653589793);
        auto v = 0.5 - std::cos ((double) x) * 0.5;
        if (bitsOf (e) != 0)
        {
            if (! std::signbit (e))
                v = 1.0 - (std::exp ((1.0 - v) * (double) lnE) - 1.0) * (double) inv;
            else
                v = (std::exp (v * (double) lnE) - 1.0) * (double) inv;
        }
        t[(std::size_t) i] = (float) v;
    }
    return t;
}
} // namespace

// =====================================================================================================================

State defaultState()
{
    State s;
    s.p.fill (0);
    s.p[0] = s.p[1] = s.p[2] = s.p[3] = 128;
    for (int b = 0; b < banks; ++b)
    {
        const int o = 16 * b;
        const int vals[16] { 0, 128, 33145, 17500, 0, 0, 64, 0, 1440, 0, 33145, 17500, 0, 0, 128, 0 };
        for (int k = 0; k < 16; ++k)
            if (o + 4 + k < stateControls)
                s.p[(std::size_t) (o + 4 + k)] = vals[k];
        s.bank[(std::size_t) b] = BankOptions {};
    }
    s.p[131] = -1;
    s.p[132] = 0;
    s.bank[0].enabled = true;
    return s;
}

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4 + 4 * (std::size_t) stateControls + 32 * banks)
        return std::nullopt;
    State s;
    s.version = i32At (d, 0);
    if (s.version < 15 || s.version > 16)
        return std::nullopt;
    for (int i = 0; i < stateControls; ++i)
        s.p[(std::size_t) i] = i32At (d, 4 + 4 * (std::size_t) i);
    for (int b = 0; b < banks; ++b)
    {
        const std::size_t o = 4 + 4 * (std::size_t) stateControls + 32 * (std::size_t) b;
        auto& k = s.bank[(std::size_t) b];
        k.filterType[0] = i32At (d, o);
        k.filterOrder[0] = i32At (d, o + 4);
        k.filterType[1] = i32At (d, o + 8);
        k.filterOrder[1] = i32At (d, o + 12);
        k.filterAfter = d[o + 16] != 0;
        k.mode = i32At (d, o + 17);
        k.steps = i32At (d, o + 21);
        k.amount = i32At (d, o + 25);
        k.enabled = d[o + 29] != 0;
        k.tapBeforeFeedback = d[o + 30] != 0;
        k.sync = d[o + 31] != 0;
        for (int f = 0; f < 2; ++f)
        {
            if (k.filterType[f] < 0 || k.filterType[f] > 7)
                k.filterType[f] = 0;
            k.filterOrder[f] = std::clamp (k.filterOrder[f], 0, 2); // the plugin has room for three sections
        }
        k.mode = std::clamp (k.mode, 0, 3);
        if (s.version < 16) // version 15 times were in units of 1/384 s (or 1/192 beat)
            s.p[(std::size_t) (16 * b + 12)] *= 10;
    }
    s.p[132] = std::clamp (s.p[132], 0, 2);
    s.version = 16;
    return s;
}

std::vector<std::uint8_t> serialise (const State& s)
{
    std::vector<std::uint8_t> d (4 + 4 * (std::size_t) stateControls + 32 * banks, 0);
    auto put = [&] (std::size_t o, std::int32_t v) { std::memcpy (d.data() + o, &v, 4); };
    put (0, 16);
    for (int i = 0; i < stateControls; ++i)
        put (4 + 4 * (std::size_t) i, s.p[(std::size_t) i]);
    for (int b = 0; b < banks; ++b)
    {
        const std::size_t o = 4 + 4 * (std::size_t) stateControls + 32 * (std::size_t) b;
        const auto& k = s.bank[(std::size_t) b];
        put (o, k.filterType[0]);
        put (o + 4, k.filterOrder[0]);
        put (o + 8, k.filterType[1]);
        put (o + 12, k.filterOrder[1]);
        d[o + 16] = k.filterAfter ? 1 : 0;
        put (o + 17, k.mode);
        put (o + 21, k.steps);
        put (o + 25, k.amount);
        d[o + 29] = k.enabled ? 1 : 0;
        d[o + 30] = k.tapBeforeFeedback ? 1 : 0;
        d[o + 31] = k.sync ? 1 : 0;
    }
    return d;
}

const char* controlName (int index) noexcept
{
    static const char* names[20] { "Dry", "Output", "Input", "Feedback", "Input pan", "Input volume", "Filter 1 cutoff",
                                   "Filter 1 resonance", "Filter 1 gain", "Feedback pan", "Feedback level", "Feedback cross",
                                   "Time", "Stereo offset", "Filter 2 cutoff", "Filter 2 resonance", "Filter 2 gain",
                                   "Output pan", "Output volume", "Send to next" };
    return index >= 0 && index < automatable ? names[subOf (index)] : "";
}
int controlMin (int index) noexcept { return index >= 0 && index < automatable ? ctlMin[(std::size_t) subOf (index)] : 0; }
int controlMax (int index) noexcept { return index >= 0 && index < automatable ? ctlMax[(std::size_t) subOf (index)] : 0; }

// =====================================================================================================================

struct Engine::Impl
{
    struct Bank
    {
        BankOptions opt;
        // control values
        float inPan = 0, inVolBase = 0, outPan = 0, outVolBase = 0, fbPan = 0, fbLevel = 0, fbCross = 0, send = 0, offsetMs = 0;
        // gain matrices: input / output (computed when dirty), feedback (every block)
        float inTarget[4] {}, inCur[4] {}, outTarget[4] {}, outCur[4] {}, fbTarget[4] {}, fbCur[4] {};
        bool inDirty = true, outDirty = true;
        Filter filter[2];
        int filterSlot = -1; // filter 1: −1 off, 0 before the delay, 1 after
        // delay line (interleaved frames)
        std::vector<float> line;
        int lineFrames = 0, readPos = 0, writePos = 0, offL = 0, offR = 0;
        bool hasLine = false;
        float time = 0;
        // pattern
        std::vector<float> table;
        double period = 0, pos = 0;
    };

    State st;
    double rate = 44100;
    int levels = 0, maxBlock = 512, rateOs = 44100;
    double rateRatio = 1;
    float maxStep = 0.001f, tempo = 140.0f;
    float dry = 1, outMaster = 1, inMaster = 1, fbMaster = 1;
    Bank bank[banks];
    std::vector<HalfBandStage> upStages, downStages;
    std::vector<float> buf[4], ioBuf;

    int p (int b, int k) const noexcept { const int i = 16 * b + k; return i < stateControls ? st.p[(std::size_t) i] : 0; }

    void applyControl (int index)
    {
        const int v = st.p[(std::size_t) index];
        if (index < 4)
        {
            if (index == 0)
                dry = volume (v);
            else if (index == 1)
            {
                outMaster = volume (v);
                for (auto& k : bank)
                    k.outDirty = true;
            }
            else if (index == 2)
            {
                inMaster = volume (v);
                for (auto& k : bank)
                    k.inDirty = true;
            }
            else
                fbMaster = (float) v * k128;
            return;
        }
        const int b = (index - 4) >> 4, k = index - 16 * b;
        auto& bk = bank[b];
        switch (k)
        {
            case 4: bk.inPan = (float) v * k128; bk.inDirty = true; break;
            case 5: bk.inVolBase = signedVolume (v); bk.inDirty = true; break;
            case 6: case 7: case 8: setUpFilter (b, 0); break;
            case 9: bk.fbPan = (float) v * k128; break;
            case 10: case 11:
            {
                bk.fbLevel = (float) p (b, 10) * k128;
                bk.fbCross = (float) p (b, 11) * k128;
                if (0.0f > bk.fbCross)
                    bk.fbLevel = bk.fbLevel * (bk.fbCross * 0.5f + 1.0f);
                break;
            }
            case 12: setUpTime (b); setUpPattern (b); break;
            case 13:
                bk.offsetMs = (float) ((double) (float) (std::exp ((double) (float) ((double) std::abs (v) * 0.001953125) * std::log (1001.0)) - 1.0) * 50.0);
                setUpTime (b);
                break;
            case 14: case 15: case 16: setUpFilter (b, 1); break;
            case 17: bk.outPan = (float) v * k128; bk.outDirty = true; break;
            case 18:
                bk.outVolBase = (st.p[131] < 0 || st.p[131] == b) ? signedVolume (v) : 0.0f;
                bk.outDirty = true;
                break;
            case 19: bk.send = signedVolume (v); break;
            default: break;
        }
    }

    void setUpTime (int b)
    {
        auto& bk = bank[b];
        bk.offL = roundEven ((double) ((bk.offsetMs * (float) rateOs) / 100000.0f));
        bk.time = (float) ((std::int64_t) p (b, 12) * rateOs); // (64-bit: the product overflows an int at 192 kHz)
        if (bk.opt.sync)
            bk.time = (float) ((double) bk.time * 0.03125 / (double) tempo);
        else
            bk.time = bk.time / 3840.0f;
        const int delay = roundEven ((double) bk.time);
        if (! bk.opt.enabled || bk.opt.mode == 0)
        {
            bk.hasLine = false;
            bk.line.clear();
            bk.lineFrames = 0;
            bk.offR = 0;
            return;
        }
        bk.hasLine = true;
        const int frames = (maxBlock << levels) + bk.offL + std::max (delay, 1);
        if (frames != bk.lineFrames)
        {
            bk.lineFrames = frames;
            bk.line.assign (2 * (std::size_t) frames, 0.0f);
            bk.readPos = 0;
            bk.writePos = std::max (delay, 1);
        }
        bk.offR = 0;
        if (p (b, 13) > 0)
            std::swap (bk.offL, bk.offR);
    }

    void setUpPattern (int b)
    {
        auto& bk = bank[b];
        if (bk.opt.steps < 1 || ! bk.opt.enabled)
        {
            bk.table.clear();
            return;
        }
        bk.period = (double) (bk.time / (float) bk.opt.steps);
        const int len = roundEven (bk.period);
        if (len < 1) // FL allocates no table (and applies no pattern) when a step rounds to 0 samples
        {
            bk.table.clear();
            return;
        }
        bk.table = patternTable (len, (float) bk.opt.amount * k128);
    }

    void setUpFilter (int b, int which)
    {
        auto& bk = bank[b];
        auto& f = bk.filter[which];
        const int base = which == 0 ? 6 : 14;
        designFilter (f, p (b, base), p (b, base + 1), p (b, base + 2), rateRatio);
        if (which == 0)
            bk.filterSlot = f.type == 0 ? -1 : (bk.opt.filterAfter ? 1 : 0);
    }

    void build (const State& s, double sampleRate, double bpm, int block, bool settled)
    {
        st = s;
        rate = sampleRate;
        maxBlock = std::max (block, 1);
        tempo = (float) bpm;
        levels = std::clamp (st.p[132], 0, 2);
        rateOs = (int) std::lround (rate) << levels;
        rateRatio = 44100.0 / (double) rateOs;
        maxStep = (float) (rateRatio * 0.001);
        upStages.assign ((std::size_t) levels, {});
        downStages.assign ((std::size_t) levels, {});
        for (auto& b : buf)
            b.assign (2 * ((std::size_t) maxBlock << levels), 0.0f);
        ioBuf.assign (2 * (std::size_t) maxBlock, 0.0f);
        for (int b = 0; b < banks; ++b)
        {
            auto& bk = bank[b];
            bk = Bank {};
            bk.opt = st.bank[(std::size_t) b];
            for (int f = 0; f < 2; ++f)
            {
                bk.filter[f].type = bk.opt.filterType[f];
                bk.filter[f].order = bk.opt.filterOrder[f];
                bk.filter[f].kernel = kernelOfType[bk.filter[f].type];
                bk.filter[f].sec.assign ((std::size_t) bk.filter[f].order + 1, {});
                bk.filter[f].clear();
            }
        }
        for (int i = 0; i < automatable; ++i)
            applyControl (i);
        for (int b = 0; b < banks; ++b)
        {
            setUpTime (b);
            setUpPattern (b);
            setUpFilter (b, 0);
            setUpFilter (b, 1);
            if (settled)
                settle (b);
        }
    }

    void settle (int b)
    {
        auto& bk = bank[b];
        ioMatrix (bk.inTarget, bk.inPan, bk.inVolBase * inMaster);
        ioMatrix (bk.outTarget, bk.outPan, bk.outVolBase * outMaster);
        bk.inDirty = bk.outDirty = false;
        std::copy (bk.inTarget, bk.inTarget + 4, bk.inCur);
        std::copy (bk.outTarget, bk.outTarget + 4, bk.outCur);
        fbTargets (bk);
        std::copy (bk.fbTarget, bk.fbTarget + 4, bk.fbCur);
    }

    void fbTargets (Bank& bk) noexcept
    {
        feedbackMatrix (bk.fbTarget, bk.fbPan, bk.fbLevel * fbMaster, bk.fbCross);
        if (bk.opt.mode == 3)
        {
            std::swap (bk.fbTarget[0], bk.fbTarget[3]);
            std::swap (bk.fbTarget[1], bk.fbTarget[2]);
        }
    }

    // adds `src` (one channel, interleaved) into the line from frame `pos`, wrapping
    void writeLine (Bank& bk, const float* src, int channel, int pos, int n) noexcept
    {
        if (pos >= bk.lineFrames)
            pos -= bk.lineFrames;
        for (int i = 0; i < n; ++i)
        {
            bk.line[2 * (std::size_t) pos + (std::size_t) channel] = src[2 * i + channel] + bk.line[2 * (std::size_t) pos + (std::size_t) channel];
            if (++pos == bk.lineFrames)
                pos = 0;
        }
    }

    void runBank (Bank& bk, float* in, float* work, float* prev, float* out, float sendIn, int n) noexcept
    {
        Ramp4 r;
        if (bk.inDirty)
        {
            ioMatrix (bk.inTarget, bk.inPan, bk.inVolBase * inMaster);
            bk.inDirty = false;
        }
        ramp4 (bk.inCur, bk.inTarget, maxStep, n, r);
        applyMatrix (in, work, n, r, false);
        if (sendIn != 0.0f)
            for (int i = 0; i < 2 * n; ++i)
                work[i] = prev[i] * sendIn + work[i];
        if (bk.filterSlot == 0)
            bk.filter[0].run (work, n);
        if (bk.hasLine)
        {
            writeLine (bk, work, 0, bk.writePos + bk.offL, n);
            writeLine (bk, work, 1, bk.writePos + bk.offR, n);
            fbTargets (bk);
            int left = n;
            float* o = work;
            do
            {
                int seg = bk.writePos - bk.readPos;
                if (seg < 1)
                    seg = bk.lineFrames - bk.readPos;
                seg = std::min (seg, left);
                float* rd = bk.line.data() + 2 * (std::size_t) bk.readPos;
                if (bk.opt.tapBeforeFeedback)
                    std::memcpy (o, rd, 8 * (std::size_t) seg);
                if (bk.opt.filterType[1] > 0)
                    bk.filter[1].run (rd, seg);
                Ramp4 fr;
                ramp4 (bk.fbCur, bk.fbTarget, maxStep, seg, fr);
                applyMatrix (rd, rd, seg, fr, false);
                for (int i = 0; i < 2 * seg; ++i)
                    if ((bitsOf (rd[i]) & 0x7fffffffu) < 0x33800000u)
                        rd[i] = 0.0f;
                if (! bk.opt.tapBeforeFeedback)
                    std::memcpy (o, rd, 8 * (std::size_t) seg);
                for (int i = 0; i < seg; ++i)
                {
                    float* rp = bk.line.data() + 2 * (std::size_t) bk.readPos;
                    float* wp = bk.line.data() + 2 * (std::size_t) bk.writePos;
                    const float l = rp[0], rr = rp[1];
                    rp[0] = rp[1] = 0.0f;
                    wp[0] = l + wp[0];
                    wp[1] = rr + wp[1];
                    if (++bk.readPos == bk.lineFrames)
                        bk.readPos = 0;
                    if (++bk.writePos == bk.lineFrames)
                        bk.writePos = 0;
                }
                if (bk.opt.mode == 2)
                    for (int i = 0; i < seg; ++i)
                        std::swap (o[2 * i], o[2 * i + 1]);
                left -= seg;
                o += 2 * seg;
            } while (left > 0);
        }
        if (! bk.table.empty())
        {
            const int len = (int) bk.table.size();
            int left = n;
            float* o = work;
            do
            {
                while (bk.period <= bk.pos)
                    bk.pos = bk.pos - bk.period;
                const int idx = (int) ((long long) std::nearbyint (bk.pos) % len);
                const int seg = std::min (len - idx, left);
                for (int i = 0; i < seg; ++i)
                {
                    const float g = bk.table[(std::size_t) (idx + i)];
                    o[2 * i] = g * o[2 * i];
                    o[2 * i + 1] = g * o[2 * i + 1];
                }
                bk.pos = bk.pos + (double) seg;
                left -= seg;
                o += 2 * seg;
            } while (left > 0);
        }
        if (bk.filterSlot == 1)
            bk.filter[0].run (work, n);
        if (bk.outDirty)
        {
            ioMatrix (bk.outTarget, bk.outPan, bk.outVolBase * outMaster);
            bk.outDirty = false;
        }
        ramp4 (bk.outCur, bk.outTarget, maxStep, n, r);
        applyMatrix (work, out, n, r, true);
    }

    void process (float* left, float* right, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            ioBuf[2 * (std::size_t) i] = left[i];
            ioBuf[2 * (std::size_t) i + 1] = right[i];
        }
        float* in = ioBuf.data();
        int m = n;
        if (levels > 0)
        {
            // stage by stage, the last one writing into buf[0]
            const float* src = ioBuf.data();
            for (int s = levels - 1; s >= 0; --s)
            {
                float* dst = buf[0].data() + 2 * (std::size_t) ((n << levels) - (m << 1));
                upStages[(std::size_t) s].up (src, dst, m);
                src = dst;
                m <<= 1;
            }
            in = buf[0].data();
        }
        float* out = buf[1].data();
        for (int i = 0; i < 2 * m; ++i)
            out[i] = in[i] * dry;
        float* work = buf[2].data();
        float* prev = buf[3].data();
        float sendIn = 0.0f;
        for (auto& bk : bank)
        {
            if (! bk.opt.enabled)
            {
                sendIn = 0.0f;
                continue;
            }
            runBank (bk, in, work, prev, out, sendIn, m);
            std::swap (work, prev);
            sendIn = bk.send;
        }
        if (levels > 0)
        {
            for (int s = levels - 1; s >= 0; --s)
            {
                m >>= 1;
                downStages[(std::size_t) s].down (out, s == 0 ? ioBuf.data() : out, m);
            }
            out = ioBuf.data();
        }
        for (int i = 0; i < n; ++i)
        {
            left[i] = out[2 * (std::size_t) i];
            right[i] = out[2 * (std::size_t) i + 1];
        }
    }
};

Engine::Engine (const State& s, double sampleRate, double bpm, int maxBlock, bool settled) : d (std::make_unique<Impl>())
{
    d->build (s, sampleRate, bpm, maxBlock, settled);
}
Engine::~Engine() = default;

void Engine::setControl (int index, int raw)
{
    if (index < 0 || index >= automatable || d->st.p[(std::size_t) index] == raw)
        return;
    d->st.p[(std::size_t) index] = raw;
    d->applyControl (index);
}

void Engine::setTempo (double bpm)
{
    if ((float) bpm == d->tempo)
        return;
    d->tempo = (float) bpm;
    for (int b = 0; b < banks; ++b)
    {
        d->setUpTime (b);
        d->setUpPattern (b);
    }
}

void Engine::process (float* left, float* right, int n) noexcept { d->process (left, right, n); }

// =====================================================================================================================

namespace
{
class DelayBankEffect final : public Effect
{
public:
    explicit DelayBankEffect (const std::vector<std::uint8_t>& slotState) : state (parse (slotState).value_or (defaultState())), accepted (state) {}

    // the editor changed bank controls (0…130 go to the running engine as FL's ProcessParam); options, the solo or the
    // oversampling need a new engine
    bool acceptState (const std::vector<std::uint8_t>& bytes) noexcept override
    {
        const auto s = parse (bytes);
        if (! s || s->p[131] != accepted.p[131] || s->p[132] != accepted.p[132])
            return false;
        for (int b = 0; b < banks; ++b)
        {
            const auto& x = s->bank[(std::size_t) b];
            const auto& y = accepted.bank[(std::size_t) b];
            if (x.filterType[0] != y.filterType[0] || x.filterType[1] != y.filterType[1] || x.filterOrder[0] != y.filterOrder[0]
                || x.filterOrder[1] != y.filterOrder[1] || x.filterAfter != y.filterAfter || x.mode != y.mode || x.steps != y.steps
                || x.amount != y.amount || x.enabled != y.enabled || x.tapBeforeFeedback != y.tapBeforeFeedback || x.sync != y.sync)
                return false;
        }
        if (! incoming.offer (s->p))
            return false;
        accepted = *s;
        return true;
    }

    void prepare (double sampleRate) noexcept override
    {
        rate = sampleRate;
        engine.reset();
    }
    void reset() noexcept override { engine.reset(); }

    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override
    {
        bpm = bpm > 0.0 ? bpm : 120.0;
        int g[globalControls];
        for (int i = 0; i < globalControls; ++i)
            g[i] = std::clamp ((int) std::lround (params[(std::size_t) i] * 128.0f), 0, 128);
        if (engine == nullptr)
        {
            for (int i = 0; i < globalControls; ++i)
                state.p[(std::size_t) i] = g[i];
            engine = std::make_unique<Engine> (state, rate, bpm, block, true);
            tempo = bpm;
        }
        if (std::array<int, stateControls> p {}; incoming.take (p))
            for (int i = globalControls; i < automatable; ++i)
            {
                state.p[(std::size_t) i] = p[(std::size_t) i];
                engine->setControl (i, p[(std::size_t) i]);
            }
        for (int i = 0; i < globalControls; ++i)
            engine->setControl (i, g[i]);
        if (bpm != tempo)
        {
            engine->setTempo (bpm);
            tempo = bpm;
        }
        for (int done = 0; done < n;)
        {
            const auto len = std::min (block, n - done);
            engine->process (left + done, right + done, len);
            done += len;
        }
    }

private:
    static constexpr int block = 512;
    double rate = 44100.0, tempo = 0.0;
    State state;
    State accepted; // the last state taken by acceptState (its thread only)
    StateHandoff<std::array<int, stateControls>> incoming;
    std::unique_ptr<Engine> engine;
};
} // namespace

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& slotState) { return std::make_unique<DelayBankEffect> (slotState); }

const char* globalName (int index) noexcept { return index >= 0 && index < globalControls ? controlName (index) : ""; }

std::string globalText (int index, float value)
{
    char text[32];
    const int raw = std::clamp ((int) std::lround (value * 128.0f), 0, 128);
    if (index == 3)
        std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (raw * 100.0 / 128.0));
    else
    {
        const auto g = volume (raw);
        if (g <= 0.0f)
            std::snprintf (text, sizeof (text), "-inf dB");
        else
            std::snprintf (text, sizeof (text), "%.1f dB", 20.0 * std::log10 ((double) g));
    }
    return text;
}

std::string controlText (const State& s, int index, int raw)
{
    char text[48];
    if (index < 0 || index >= automatable)
        return {};
    if (index < globalControls)
        return globalText (index, (float) raw / 128.0f);
    const int b = (index - 4) >> 4, k = index - 4 - 16 * b;
    const auto db = [&] (float g) {
        if (g == 0.0f)
            std::snprintf (text, sizeof (text), "-inf dB");
        else
            std::snprintf (text, sizeof (text), "%.1f dB%s", 20.0 * std::log10 (std::fabs ((double) g)), g < 0.0f ? " (inv.)" : "");
    };
    const auto pan = [&] {
        if (raw == 0)
            std::snprintf (text, sizeof (text), "Centered");
        else
            std::snprintf (text, sizeof (text), "%d%% %s", (int) std::lround (std::abs (raw) * 100.0 / 128.0), raw < 0 ? "left" : "right");
    };
    switch (k)
    {
        case 0: case 5: case 13: pan(); break;
        case 1: case 14: case 15: db (signedVolume (raw)); break;
        case 2: case 10:
        {
            // the filter's corner (REA_rodada56 §1.5): low-pass and the other types use different scales
            const auto type = s.bank[(std::size_t) b].filterType[k == 2 ? 0 : 1];
            const auto c = (double) raw / 65536.0;
            const auto f = (std::exp (c * std::log (1501.0)) - 1.0) * (type == 1 ? 0.00066466664 : 0.00039993334);
            std::snprintf (text, sizeof (text), "%.0f Hz", (std::min (f, type == 4 ? 0.6001 : 1.0001) + (type == 1 ? 0.003 : 0.0001)) * 44100.0);
            break;
        }
        case 3: case 11: std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (raw * 100.0 / 65536.0)); break;
        case 4: case 12: // the shelf / peak amplitude A (unity at 128), shown as the cookbook gain 40·log10 A
        {
            const auto a = (std::exp (raw / 128.0 * std::log (10.0 / 9.0)) - 1.0) * 8.1 + 0.1;
            std::snprintf (text, sizeof (text), "%+.1f dB", 40.0 * std::log10 (a));
            break;
        }
        case 6: case 7: std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (raw * 100.0 / 128.0)); break;
        case 8:
            if (s.bank[(std::size_t) b].sync) // 1920 = one beat = four steps
                std::snprintf (text, sizeof (text), "%.2f steps", raw / 480.0);
            else // 3840 = one second
                std::snprintf (text, sizeof (text), "%.1f ms", raw * 1000.0 / 3840.0);
            break;
        case 9:
        {
            const auto ms = (std::exp (std::abs (raw) / 512.0 * std::log (1001.0)) - 1.0) * 50.0 / 100.0;
            std::snprintf (text, sizeof (text), raw == 0 ? "0 ms" : "%.2f ms %s", ms, raw < 0 ? "left" : "right");
            break;
        }
        default: std::snprintf (text, sizeof (text), "%d", raw); break;
    }
    return text;
}
} // namespace lynx::fldbank
