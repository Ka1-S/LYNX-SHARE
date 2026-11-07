#include "core/Vocodex.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>

#include "core/DelphiMath.h"
#include "core/FlEffects.h"
#include "core/Instruments.h"
#include "core/SytrusEngine.h"

// The engine's rounding is part of its behaviour (the reference matches FL's DLL to the last bits): no fused
// multiply-add contraction, whatever the target CPU.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

namespace lynx::vocodex
{
namespace
{
constexpr double ln2 = 0.69314718055994529;
constexpr double pi = 3.14159265358979323846;
constexpr int maxVoices = 6;         // voices v = −4…1 at index v + 4 (v ≤ 0 analyse the modulator, v = 1 the carrier)
constexpr int maxOrder = 4;
constexpr int levelCount = 6;        // signal levels: 0 = 2× up-sampled, 1 = native rate, 2… = successive halves
constexpr int smootherStages = 8;
constexpr int fifoFrames = 16;       // Draft re-blocking (and its latency)
constexpr double lnHundredth = -4.6051701859880918; // ln 0.01: a smoother stage covers 99 % of a step in t samples
constexpr float tiny = 5.9604644775390625e-08f;      // 2⁻²⁴: state values below it are flushed to 0
constexpr int noTail = INT_MAX;

// --- FL's frequency scale (REA_rodada20 §4.2.3): map y ↔ Hz -------------------------------------------------------
struct FreqScale
{
    double a, b, p, k4, k5, k6, k7;

    FreqScale()
    {
        a = std::exp (0.057762265046662105 * -33.0) * 440.0;
        b = std::exp (0.057762265046662105 * 67.0) * 440.0;
        const double p2 = 1.0;
        p = (std::log (2.0) / std::log (b * 2.0 / a)) * p2 + a * (1.0 / b) * (1.0 - p2);
        const double q = (p2 * b) / a, l3 = std::log (q + 1.0);
        k4 = 1.0 / l3;
        k5 = (1.0 / (1.0 - p)) * l3;
        k6 = q / (b - a);
        k7 = 1.0 / q;
    }
    double toHz (double y) const noexcept { return p <= y ? (std::exp ((y - p) * k5) - 1.0) * (b - a) * k7 + a : (y * a) / p; }
    double toY (double f) const noexcept { return a <= f ? p + std::log ((f - a) * k6 + 1.0) * k4 * (1.0 - p) : (f * p) / a; }
};

const FreqScale& freqScale()
{
    static const FreqScale scale;
    return scale;
}

// maps 1 (pan), 2 (gain offset) and 10 (pitch) are read centred (y − 0.5)
constexpr std::array<double, 13> mapCentre { 0, 0.5, 0.5, 0, 0, 0, 0, 0, 0, 0, 0.5, 0, 0 };

// --- the maps: FL's envelope library (REA_rodada35 §12; the same code as Love Philter's maps, REA_rodada66 §3.2) -------
// "phase ≥ 1" as the library tests it: the high word of the double compared unsigned (a negative phase passes too)
bool phaseWrap (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return (std::uint32_t) (b >> 32) > 0x3fefffffu;
}
bool belowHalf (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return (std::uint32_t) (b >> 32) < 0x3fe00000u;
}
std::uint32_t bitsOf (float f) noexcept
{
    std::uint32_t b;
    std::memcpy (&b, &f, 4);
    return b;
}
long long roundEven (double x) noexcept { return (long long) std::nearbyint (x); }

// A map prepared for reading: Δx in double, levels in float (as FL keeps them), and per point the curve constants
class MapReader
{
public:
    MapReader() { pts.push_back ({}); }

    MapReader (const Map& m, double centre)
    {
        const auto n = m.x.size();
        float prevY = 0.0f;
        double prevX = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            Pt p;
            p.dx = m.x[i] - prevX;
            prevX = m.x[i];
            p.y = (float) (i < m.y.size() ? m.y[i] : 0.0);
            if (std::isnan (p.y))
                p.y = prevY; // a NaN level takes the previous point's
            prevY = p.y;
            p.t = i < m.tension.size() ? m.tension[i] : 0.0f;
            p.mode = i < m.mode.size() ? m.mode[i] : 0;
            pts.push_back (p);
        }
        if (pts.empty())
            pts.push_back ({});
        offset = centre;
        normalise();
        prepare();
    }

    // FL's envelope cursor: the segment it is in, its phase and rate, the segment's start level and step, the value
    struct Cursor
    {
        int idx = 0;
        double phase = 0.0, rate = 0.0, start = 0.0, delta = 0.0, value = 0.0;
    };

    Cursor begin() const noexcept
    {
        Cursor c;
        if (pts[0].dx == 0.0)
            c.rate = 1.0, c.phase = 1.0;
        else
            c.rate = 1.0 / pts[0].dx;
        c.start = c.value = level (0);
        return c;
    }

    void advance (Cursor& c, double by) const noexcept
    {
        const int n = (int) pts.size();
        c.phase += by * c.rate;
        while (phaseWrap (c.phase))
        {
            const int prev = c.idx;
            c.phase = c.rate == 0.0 ? by : (c.phase - 1.0) / c.rate;
            c.start = level (c.idx);
            if (c.idx < n - 1)
                ++c.idx;
            c.delta = level (c.idx) - c.start;
            if (prev == c.idx)
                c.phase = c.rate = 0.0;
            else
            {
                const double dx = pts[(std::size_t) c.idx].dx;
                if (dx <= 0.0)
                    c.rate = 1.0, c.phase += 1.0;
                else
                    c.rate = 1.0 / dx, c.phase *= c.rate;
            }
        }
        c.value = c.start + shape (c.idx, c.phase) * c.delta;
    }

    // FL's map read: a cursor set at the start, then advanced by x
    double at (double x) const noexcept
    {
        auto c = begin();
        advance (c, x);
        return c.value;
    }

    // the sign byte FL computes for each point (saved with it): 0, ±1 = sign of the tension, 2 = a straight segment
    std::int8_t signOf (std::size_t i) const noexcept { return i < pts.size() ? pts[i].sign : 0; }

private:
    struct Pt
    {
        double dx = 0.0;
        float y = 0.0f, t = 0.0f;
        std::uint8_t mode = 0;
        std::int8_t sign = 0;
        float p4 = 0.0f, p5 = 0.0f, p6 = 0.0f; // the curve constants (mode 0/1: k, 1/|k|, ln(|k| + 1))
    };
    std::vector<Pt> pts;
    double offset = 0.0;

    double level (int i) const noexcept { return (double) pts[(std::size_t) i].y - offset; }

    // a point at x = 0 when the first Δx is positive, then every Δx scaled so the map spans 0…1
    void normalise()
    {
        if (pts[0].dx > 0.0)
        {
            Pt p0;
            p0.y = pts[0].y;
            pts.insert (pts.begin(), p0);
        }
        double total = 0.0;
        for (const auto& p : pts)
            total += p.dx;
        if (0.0 < total)
        {
            const double f = 1.0 / total;
            if (f != 1.0)
                for (auto& p : pts)
                    p.dx = p.dx * f;
        }
        else
        {
            Pt end;
            end.dx = 1.0 - total;
            end.y = pts.back().y;
            pts.push_back (end);
        }
    }

    static int stepCount (const Pt& p) noexcept
    {
        if (p.mode > 7 || ((0x78u >> p.mode) & 1u) == 0) // stairs, smooth stairs, pulse, wave
            return -1;
        const auto a = (double) (float) (1.0 - (double) std::abs (p.t) * 0.6);
        auto n = (int) roundEven ((double) (float) (dmath::exp (a * dmath::ln (1000001.0)) - 1.0) * 0.001);
        if (p.mode == curveStairs && p.t >= 0.0f)
            ++n;
        return n;
    }

    void prepare()
    {
        constexpr float eps = 5.960464477539063e-08f;
        const int n = (int) pts.size();
        for (int i = 0; i < n; ++i)
        {
            auto& p = pts[(std::size_t) i];
            if (p.dx < 0.0)
                p.dx = 0.0;
            if (i == 0)
                continue;
            const float t = p.t;
            p.sign = (bitsOf (t) & 0x7fffffffu) == 0 ? 0 : (std::signbit (t) ? -1 : 1);
            const int cnt = stepCount (p);
            switch (p.mode)
            {
                case curveSingle:
                case curveDouble:
                {
                    const auto e = (float) (dmath::exp ((double) std::abs (t) * dmath::ln (1000001.0)) - 1.0);
                    const float k = e * (t > 0.0f ? 1.0f : (t < 0.0f ? -1.0f : 0.0f));
                    p.p4 = k;
                    if (k != 0.0f)
                    {
                        p.p5 = (float) (1.0 / std::abs ((double) k));
                        p.p6 = (float) dmath::ln (std::abs ((double) k) + 1.0);
                    }
                    break;
                }
                case curveStairs:
                {
                    const int sg = t < 0.0f ? -1 : 1;
                    p.p6 = (float) sg * 0.5f;
                    p.p4 = (float) (cnt + 1);
                    p.p5 = 1.0f / (p.p4 - (float) sg);
                    break;
                }
                case curveSmoothStairs:
                    p.p4 = (float) (cnt + 1);
                    p.p5 = 1.0f / p.p4;
                    break;
                case curvePulse:
                {
                    const int sg = t < 0.0f ? -1 : 1;
                    p.p6 = (float) sg * 0.5f;
                    p.p4 = (float) ((cnt + 1) * 2);
                    p.p5 = 6.2831855f;
                    break;
                }
                case curveWave:
                    p.p4 = (float) ((double) cnt + 0.5);
                    p.p5 = 6.2831855f;
                    if (cnt == 0 && p.sign > 0)
                        p.sign = 2;
                    break;
                case curveSingle2:
                case curveDouble2: p.p4 = t; break;
                case curveHalfSine: p.p4 = (float) std::abs ((double) t); break;
                case curveSmooth: prepareSmooth (i, eps); break;
                case curveSingle3:
                case curveDouble3:
                {
                    double d = std::abs ((double) t) * 0.9;
                    if (d <= 1.52587890625e-05)
                        d = 1.52587890625e-05;
                    const double u = 1.0 - d;
                    p.p4 = (float) (1.0 - u * u * u);
                    p.p5 = (1.0f - p.p4) / p.p4;
                    p.p6 = p.p5;
                    if (p.sign > 0)
                    {
                        p.p5 = -p.p5;
                        p.p6 = p.p5 - 1.0f;
                    }
                    if (p.mode == curveDouble3)
                    {
                        p.p5 = (float) ((double) p.p5 * 0.5);
                        p.p6 = (float) ((double) p.p6 * 0.5);
                    }
                    break;
                }
                default: break;
            }
            // straight when the tension is 0: single/double curve (all three), half sine, smooth
            if (p.mode < 16 && ((0x1b83u >> p.mode) & 1u) != 0 && t == 0.0f)
                p.sign = 2;
        }
    }

    // mode 10: slopes from the neighbouring points (those that are "smooth" too)
    void prepareSmooth (int i, float eps) noexcept
    {
        auto& p = pts[(std::size_t) i];
        const int n = (int) pts.size();
        const double h = (double) eps < p.dx ? p.dx : (double) eps;
        const double y1 = p.y;
        double y0 = y1, ypp = y1, rp = 1.0;
        if (i - 1 >= 0)
        {
            y0 = ypp = pts[(std::size_t) (i - 1)].y;
            if (pts[(std::size_t) (i - 1)].mode == curveSmooth)
            {
                rp = pts[(std::size_t) (i - 1)].dx / h;
                if (i - 2 >= 0)
                    ypp = pts[(std::size_t) (i - 2)].y;
            }
        }
        double rn = 1.0, yn = y1;
        if (i + 1 < n && pts[(std::size_t) (i + 1)].mode == curveSmooth)
        {
            rn = pts[(std::size_t) (i + 1)].dx / h;
            yn = pts[(std::size_t) (i + 1)].y;
        }
        const double d = y1 - y0;
        if (! ((double) eps <= std::abs (d)))
        {
            p.sign = 2;
            return;
        }
        const double a = (ypp - y0) / d, b = (yn - y0) / d;
        rp = rp > (double) eps ? rp : (double) eps;
        rn = rn > (double) eps ? rn : (double) eps;
        const double sr = (b - 1.0) / rn, sl = -a / rp;
        const double one = 1.0 / 1.0;
        p.p4 = 0.0f;
        if (0.0f < (float) sr * (float) one)
        {
            const float w1 = (float) rn * 2.0f + 1.0f, w2 = (float) rn + 2.0f;
            p.p4 = ((float) sr * (float) one * (w1 + w2)) / (w1 * (float) sr + w2 * (float) one);
        }
        p.p5 = 0.0f;
        if (0.0f < (float) one * (float) sl)
        {
            const float w1 = 1.0f * 2.0f + (float) rp, w2 = 1.0f + (float) rp * 2.0f;
            p.p5 = ((float) one * (float) sl * (w1 + w2)) / (w1 * (float) one + w2 * (float) sl);
        }
        p.p6 = (float) (1.0 - (double) std::abs (p.t));
    }

    static double singleK (const Pt& p, double x) noexcept
    {
        const auto kb = bitsOf (p.p4);
        if (kb == 0)
            return x;
        if ((kb & 0x80000000u) == 0)
            return 1.0 - (dmath::exp ((1.0 - x) * (double) p.p6) - 1.0) * (double) p.p5;
        return (dmath::exp (x * (double) p.p6) - 1.0) * (double) p.p5;
    }

    // the fraction (0…1) of the segment ending at point i, at phase ph (double precision throughout, as Vocodex runs it)
    double shape (int i, double ph) const noexcept
    {
        if (i < 0 || i >= (int) pts.size())
            return 0.0;
        const auto& p = pts[(std::size_t) i];
        if (p.sign == 2)
            return ph;
        const double p4 = p.p4, p5 = p.p5, p6 = p.p6;
        switch (p.mode)
        {
            case curveSingle: return singleK (p, ph);
            case curveDouble:
                if (belowHalf (ph))
                    return singleK (p, ph * 2.0) * 0.5;
                return 1.0 - singleK (p, (1.0 - ph) * 2.0) * 0.5;
            case curveStairs: return (double) ((float) (int) roundEven (ph * p4 - p6) * p.p5);
            case curveSmoothStairs:
            {
                const double v = ph * p4 - 0.5;
                const auto k = (int) roundEven (v - 0.5);
                double d = (v - (double) k) * 2.0 - 1.0;
                if (p.sign < 0)
                {
                    const double z = 1.0 - std::abs (d);
                    d = (1.0 - z * z * z) * (std::signbit (d) ? -1.0 : 1.0);
                }
                else
                    d = d * d * d;
                return ((double) k + d * 0.5 + 1.0) * p5;
            }
            case curvePulse: return (double) (roundEven (ph * p4 - p6) & 1);
            case curveWave:
            {
                if (p.sign < 0)
                    return (dmath::sin ((ph * p4 + 0.75) * p5) + 1.0) * 0.5;
                const double v = ph * p4 - 0.5;
                const auto k = (int) roundEven (v - 0.5);
                return std::abs ((v - (double) k) * 2.0 - 1.0);
            }
            case curveSingle2:
            {
                const double d = p.sign > 0 ? 1.0 - ph : ph;
                const double d3 = d * d * d;
                return ph - (d3 * d3 - d) * p4;
            }
            case curveDouble2:
            {
                if (belowHalf (ph))
                {
                    const double x = ph * 2.0;
                    const double d = p.sign > 0 ? 1.0 - x : x;
                    const double d3 = d * d * d;
                    return (x - (d3 * d3 - d) * p4) * 0.5;
                }
                const double x = (1.0 - ph) * 2.0;
                const double d = p.sign > 0 ? 1.0 - x : x;
                const double d3 = d * d * d;
                return 1.0 - (x - (d3 * d3 - d) * p4) * 0.5;
            }
            case curveHalfSine:
            {
                const double q = (double) 1.5707963705062866; // π/2 in float
                const double d = p.sign < 1 ? 1.0 - dmath::cos (ph * q) : dmath::sin (ph * q);
                return ph - (ph - d) * p4;
            }
            case curveSmooth:
            {
                const double x2 = ph * ph, x1 = ph - 1.0;
                return ph + ((((x2 * 3.0 - x2 * 2.0 * ph) + x2 * x1 * p4) + ph * x1 * x1 * p5) - ph) * p6;
            }
            case curveSingle3:
                if (p.sign < 1)
                    return p5 / (1.0 - p4 * ph) - p6;
                return p5 / (1.0 - p4 * (1.0 - ph)) - p6;
            case curveDouble3:
                if (belowHalf (ph))
                {
                    if (p.sign < 1)
                        return p5 / (1.0 - p4 * ph * 2.0) - p6;
                    return p5 / (1.0 - p4 * (1.0 - ph * 2.0)) - p6;
                }
                else
                {
                    const double x = (1.0 - ph) * 2.0;
                    if (p.sign < 1)
                        return (1.0 - p5 / (1.0 - p4 * x)) + p6;
                    return (1.0 - p5 / (1.0 - p4 * (1.0 - x))) + p6;
                }
            default: return 0.0; // hold (and unknown modes): the level stays until the point
        }
    }
};

// 2^x for the float exponents of the engine: integer exponents by repeated products, others through exp
float powFloat (float base, float x) noexcept
{
    if (x == 0.0f)
        return 1.0f;
    const float r = std::rint (x);
    if (r == x)
    {
        const int k = (int) r;
        double p = 1.0, b = base;
        for (int a = k < 0 ? -k : k; a != 0; a >>= 1)
        {
            if ((a & 1) != 0)
                p *= b;
            b *= b;
        }
        return (float) (k < 0 ? 1.0 / p : p);
    }
    return (float) std::exp ((double) x * std::log ((double) base));
}

// (11^l − 1)/10 with l = v·scale
double curve11 (int v, float scale) noexcept
{
    const float l = (float) v * scale;
    if (l == 1.0f)
        return 1.0;
    return (float) ((std::exp ((double) (l * 2.39789534f)) - 1.0) * (double) 0.100000001f);
}

// (7^l − 1)/6
float curve7 (float l) noexcept
{
    if (l == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (l * 1.9459101f)) - 1.0) * (double) 0.166666672f);
}

// (3^(2m) − 1)/2
double curve3 (double m) noexcept
{
    const float h = (float) (m * 2.0);
    if (h == 1.0f)
        return 1.0;
    return (float) ((std::exp ((double) (h * 1.09861231f)) - 1.0) * (double) 0.5f);
}

double flushToFloat (double v) noexcept
{
    float f = (float) v;
    if (std::fabs (f) < tiny)
        f = 0.0f;
    return (double) f;
}

// the x87 in extended precision for one of FL's x87 kernels (a no-op where long double is not the x87 format)
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
struct X87Extended
{
    unsigned short saved = 0;
    X87Extended() noexcept
    {
        asm volatile ("fnstcw %0" : "=m"(saved) : : "memory");
        const auto cw = (unsigned short) ((saved & ~0x0F00u) | 0x0300u); // 64-bit mantissa, round to nearest
        asm volatile ("fldcw %0" : : "m"(cw) : "memory");
    }
    ~X87Extended() { asm volatile ("fldcw %0" : : "m"(saved) : "memory"); }
    X87Extended (const X87Extended&) = delete;
    X87Extended& operator= (const X87Extended&) = delete;
};
#else
struct X87Extended
{
};
#endif

// --- biquads ---------------------------------------------------------------------------------------------------------
struct Biquad
{
    float f = 0.0f, bw = 0.0f; // design frequency (Hz) and band width (octaves)
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, 8> s {}; // x1 L/R, x2 L/R, y1 L/R, y2 L/R

    // band-pass with a constant 0 dB peak; w0 = 2π/fs
    void bandPass (double w0) noexcept
    {
        const double w = (double) f * w0, sn = std::sin (w), cs = std::cos (w);
        const double x = 0.34657359027997264 * (double) bw * w / sn;
        const double alpha = sn * ((std::exp (x) - std::exp (-x)) * 0.5);
        const double d = 1.0 / (alpha + 1.0);
        a1 = d * cs * -2.0;
        a2 = d * (1.0 - alpha);
        b1 = 0.0;
        b0 = d * alpha;
        b2 = d * -alpha;
    }

    // low-pass / high-pass with α = sin ω · q
    void lowHighPass (bool high, double w0, double q) noexcept
    {
        const double w = (double) f * w0, sn = std::sin (w), cs = std::cos (w);
        const double alpha = sn * q, d = 1.0 / (alpha + 1.0);
        a1 = d * cs * -2.0;
        a2 = d * (1.0 - alpha);
        if (! high)
        {
            b1 = d * (1.0 - cs);
            b0 = b1 * (double) 0.5f;
        }
        else
        {
            b1 = d * -(cs + 1.0);
            b0 = -b1 * (double) 0.5f;
        }
        b2 = b0;
    }

    // stereo, 64-bit signal (band filters: b1 = 0)
    void run64 (const double* in, double* out, int n) noexcept
    {
        double x1L = s[0], x1R = s[1], x2L = s[2], x2R = s[3], y1L = s[4], y1R = s[5], y2L = s[6], y2R = s[7];
        for (int k = 0; k < n; ++k)
        {
            const double xL = in[2 * k], xR = in[2 * k + 1];
            const double yL = xL * b0 + ((x2L * b2 - y2L * a2) - y1L * a1);
            const double yR = xR * b0 + ((x2R * b2 - y2R * a2) - y1R * a1);
            out[2 * k] = yL;
            out[2 * k + 1] = yR;
            x2L = x1L, x2R = x1R, x1L = xL, x1R = xR, y2L = y1L, y2R = y1R, y1L = yL, y1R = yR;
        }
        s = { x1L, x1R, x2L, x2R, y1L, y1R, y2L, y2R };
    }

    // stereo, float signal; the state is rounded to float once per call
    void run32 (const float* in, float* out, int n) noexcept
    {
        double x1L = s[0], x1R = s[1], x2L = s[2], x2R = s[3], y1L = s[4], y1R = s[5], y2L = s[6], y2R = s[7];
        for (int k = 0; k < n; ++k)
        {
            const double xL = in[2 * k], xR = in[2 * k + 1];
            const double yL = xL * b0 + (((x1L * b1 - y2L * a2) + x2L * b2) - y1L * a1);
            const double yR = xR * b0 + (((x1R * b1 - y2R * a2) + x2R * b2) - y1R * a1);
            out[2 * k] = (float) yL;
            out[2 * k + 1] = (float) yR;
            x2L = x1L, x2R = x1R, x1L = xL, x1R = xR, y2L = y1L, y2R = y1R, y1L = yL, y1R = yR;
        }
        s = { x1L, x1R, x2L, x2R, y1L, y1R, y2L, y2R };
        for (auto& v : s)
            v = flushToFloat (v);
    }

    void run32Mono (const float* in, float* out, int n) noexcept
    {
        double x1 = s[0], x2 = s[2], y1 = s[4], y2 = s[6];
        for (int k = 0; k < n; ++k)
        {
            const double x = in[k];
            const double y = x * b0 + (((x1 * b1 - y2 * a2) + x2 * b2) - y1 * a1);
            out[k] = (float) y;
            x2 = x1, x1 = x, y2 = y1, y1 = y;
        }
        s[0] = x1, s[2] = x2, s[4] = y1, s[6] = y2;
        for (auto& v : s)
            v = flushToFloat (v);
    }
};

// --- polyphase all-pass half-band pair (two 3-section all-pass chains) ---------------------------------------------
constexpr double halfA[3] { 0.14737711360104661, 0.48246854276970014, 0.88300502576937312 };
constexpr double halfB[3] { 0.039151597734460045, 0.30264684832849342, 0.6746159185469639 };

struct HalfBand
{
    double s[12][2] {};

    void clear() noexcept { std::memset (s, 0, sizeof (s)); }

    void roundState() noexcept
    {
        for (int i = 0; i < 6; ++i)
            for (int c = 0; c < 2; ++c)
                s[i][c] = flushToFloat (s[i][c]);
    }

    // n frames in → 2n frames out
    void up (const float* in, float* out, int n) noexcept
    {
        for (int k = 0; k < n; ++k)
            for (int c = 0; c < 2; ++c)
            {
                const double x = in[2 * k + c];
                const double t0 = s[6][c] + (x - s[0][c]) * halfA[0];
                s[6][c] = x, s[0][c] = t0;
                const double t3 = s[9][c] + (x - s[3][c]) * halfB[0];
                s[9][c] = x, s[3][c] = t3;
                const double t1 = s[7][c] + (s[0][c] - s[1][c]) * halfA[1];
                s[7][c] = s[0][c], s[1][c] = t1;
                const double t4 = s[10][c] + (s[3][c] - s[4][c]) * halfB[1];
                s[10][c] = s[3][c], s[4][c] = t4;
                const double t2 = s[8][c] + (s[1][c] - s[2][c]) * halfA[2];
                s[2][c] = t2;
                const double t5 = s[11][c] + (s[4][c] - s[5][c]) * halfB[2];
                s[5][c] = t5;
                out[4 * k + 2 + c] = (float) t2;
                out[4 * k + c] = (float) t5;
                s[8][c] = s[1][c], s[11][c] = s[4][c];
            }
        roundState();
    }

    // 2n frames in → n frames out
    void down (const float* in, float* out, int n) noexcept
    {
        for (int k = 0; k < n; ++k)
            for (int c = 0; c < 2; ++c)
            {
                const double x0 = in[4 * k + c], x1 = in[4 * k + 2 + c];
                const double t0 = s[6][c] + (x0 - s[0][c]) * halfA[0];
                s[6][c] = x0, s[0][c] = t0;
                const double t3 = s[9][c] + (x1 - s[3][c]) * halfB[0];
                s[9][c] = x1, s[3][c] = t3;
                const double t1 = s[7][c] + (s[0][c] - s[1][c]) * halfA[1];
                s[7][c] = s[0][c], s[1][c] = t1;
                const double t4 = s[10][c] + (s[3][c] - s[4][c]) * halfB[1];
                s[10][c] = s[3][c], s[4][c] = t4;
                const double t2 = s[8][c] + (s[1][c] - s[2][c]) * halfA[2];
                s[8][c] = s[1][c], s[2][c] = t2;
                const double t5 = s[11][c] + (s[4][c] - s[5][c]) * halfB[2];
                s[11][c] = s[4][c], s[5][c] = t5;
                out[2 * k + c] = (float) ((t5 + t2) * 0.5);
            }
        roundState();
    }
};

// --- followers -------------------------------------------------------------------------------------------------------
// maximum over the current sample and the W − 1 previous ones (monotonic queue; the inputs are ≥ 0, so the zero
// history at the start never wins)
struct SlidingMax
{
    int window = 1;
    std::vector<float> value;
    std::vector<std::uint32_t> when;
    int head = 0, count = 0;
    std::uint32_t clock = 0;

    void setup (int w)
    {
        window = std::max (1, w);
        value.assign ((std::size_t) window, 0.0f);
        when.assign ((std::size_t) window, 0);
        clear();
    }
    void clear() noexcept { head = count = 0, clock = 0; }

    void run (float* e, int n) noexcept
    {
        if (window <= 1)
            return;
        for (int k = 0; k < n; ++k, ++clock)
        {
            while (count > 0 && clock - when[(std::size_t) head] >= (std::uint32_t) window)
            {
                head = head + 1 == window ? 0 : head + 1;
                --count;
            }
            const float x = e[k];
            while (count > 0)
            {
                auto back = head + count - 1;
                if (back >= window)
                    back -= window;
                if (value[(std::size_t) back] > x)
                    break;
                --count;
            }
            auto pos = head + count;
            if (pos >= window)
                pos -= window;
            value[(std::size_t) pos] = x;
            when[(std::size_t) pos] = clock;
            ++count;
            e[k] = value[(std::size_t) head];
        }
    }
};

// √(mean over W samples of the input) with a double running sum and a float ring
struct MovingRms
{
    int window = 1, pos = 0;
    std::vector<float> ring;
    double sum = 0.0;
    float scale = 1.0f;

    void setup (int w)
    {
        window = std::max (1, w);
        ring.assign ((std::size_t) window, 0.0f);
        scale = (float) (1.0 / (double) (window * 2));
        clear();
    }
    void clear() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        pos = 0, sum = 0.0;
    }

    void run (float* e, int n) noexcept // e: L² + R² in, rms out
    {
        double s = sum;
        int p = pos;
        for (int k = 0; k < n; ++k)
        {
            if (window <= ++p)
                p = 0;
            s = (s - (double) ring[(std::size_t) p]) + (double) e[k];
            ring[(std::size_t) p] = e[k];
            e[k] = (float) (s * (double) scale);
        }
        sum = s, pos = p;
        for (int k = 0; k < n; ++k)
            e[k] = std::sqrt (std::max (e[k], 0.0f));
    }
};

// one-pole attack/release stage, running value in double precision
struct Smoother
{
    double ca = 0.0, cr = 0.0, y = 0.0;
    bool single = true;

    void run (float* e, int n) noexcept
    {
        double v = y;
        if (! single)
            for (int k = 0; k < n; ++k)
            {
                const double x = e[k];
                const double c = x >= v ? ca : cr;
                v = x + c * (v - x);
                e[k] = (float) v;
            }
        else
            for (int k = 0; k < n; ++k)
            {
                const double x = e[k];
                v = (v - x) * ca + x;
                e[k] = (float) v;
            }
        if (! ((double) tiny < std::fabs (v)))
            v = 0.0;
        y = v;
    }
};

// stage count round(order + 0.5), each stage t/√order, the last one scaled by its fractional part
int setupSmoother (std::array<Smoother, smootherStages>& stages, double att, double attOrder, double rel, double relOrder) noexcept
{
    if (attOrder <= 1.0)
        attOrder = 1.0;
    const double ro = 1.0 < relOrder ? relOrder : 1.0;
    const int n1 = (int) std::nearbyint (attOrder + 0.5), n2 = (int) std::nearbyint (ro + 0.5);
    const int n = std::min (std::max (n1, n2), smootherStages);
    att = att / std::sqrt (attOrder);
    rel = rel / std::sqrt (ro);
    for (int i = 0; i < n; ++i)
    {
        if (n1 - 1 <= i)
            att = i < n1 ? att * (1.0 - ((double) n1 - attOrder)) : 0.0;
        if (n2 - 1 <= i)
            rel = i < n2 ? rel * (1.0 - ((double) n2 - ro)) : 0.0;
        double ca = 0.0, cr = 0.0;
        if (0.0 < att)
            ca = std::exp (lnHundredth / std::max (att, 1.0));
        if (0.0 < rel)
            cr = std::exp (lnHundredth / std::max (rel, 1.0));
        auto& st = stages[(std::size_t) i];
        st.ca = ca, st.cr = cr, st.single = ca == cr, st.y = 0.0;
    }
    return n;
}

// --- gain ramps: linear over the block, slope limited to 44.1/fs per sample ------------------------------------------
float ramp (float& current, float target, float maxSlope, int n, float& start) noexcept
{
    start = current;
    float d = target - start;
    if (d != 0.0f)
    {
        current = target;
        d = d / (float) n;
        if (maxSlope < std::fabs (d))
        {
            d = std::copysign (maxSlope, d);
            current = start + d * (float) n;
            if (std::fabs (current) < 1e-30f)
                current = 0.0f;
        }
    }
    return d;
}

// per-sample gains of a ramp, two interleaved accumulators (even/odd samples) as the engine sums them
void rampGains (float g, float d, int n, float* gg) noexcept
{
    float ge = g + 0.0f, go = g + d;
    const float d2 = d + d;
    int k = 0;
    for (; k + 8 <= n; k += 8)
    {
        gg[k] = ge;
        gg[k + 1] = go;
        gg[k + 2] = ge + d2;
        gg[k + 3] = go + d2;
        gg[k + 4] = gg[k + 2] + d2;
        gg[k + 5] = gg[k + 3] + d2;
        gg[k + 6] = gg[k + 4] + d2;
        gg[k + 7] = gg[k + 5] + d2;
        ge = gg[k + 6] + d2;
        go = gg[k + 7] + d2;
    }
    for (; k < n; ++k)
    {
        gg[k] = ge;
        ge += d;
    }
}

// --- the uniform generator of the installation's IPP (ippsRandUniform_Direct_32f), bit-exact ------------------------
void randUniform (float* out, int n, float lo, float hi, std::uint32_t& seed) noexcept
{
    constexpr std::uint32_t inc = 0x3c6ef373u, mul = 69069u;
    const double scale = (double) (hi - lo) * 2.3283064365387e-10, offset = (double) (lo + hi) * 0.5;
    std::uint32_t r10 = seed, lcg = 0x436cbae9u, r14 = r10 * mul + inc, r12 = r14 * mul + inc;
    std::int32_t borrow = -1;
    int i = 0;
    for (int k = 0; k < n / 2; ++k)
    {
        const auto t = (std::int32_t) (r14 - r12 + (std::uint32_t) borrow);
        borrow = t < 0 ? -1 : 0;
        const std::uint32_t x1 = (std::uint32_t) t - (borrow != 0 ? 18u : 0u);
        lcg = lcg * mul;
        const auto o1 = (std::int32_t) (x1 + lcg + inc);
        const auto t2 = (std::int32_t) (r10 - r14 + (std::uint32_t) borrow);
        borrow = t2 < 0 ? -1 : 0;
        const std::uint32_t x2 = (std::uint32_t) t2 - (borrow != 0 ? 18u : 0u);
        lcg = (lcg + inc) * mul;
        const auto o2 = (std::int32_t) (x2 + lcg + inc);
        lcg = lcg + inc;
        out[i] = (float) ((double) o1 * scale + offset);
        out[i + 1] = (float) ((double) o2 * scale + offset);
        i += 2;
        r12 = r10, r14 = x1, r10 = x2;
    }
    std::uint32_t last = r10;
    if ((n & 1) != 0)
    {
        const auto t = (std::int32_t) (r14 - r12 + (std::uint32_t) borrow);
        last = (std::uint32_t) t - (t < 0 ? 18u : 0u);
        lcg = lcg * mul;
        out[i] = (float) ((double) (std::int32_t) (last + lcg + inc) * scale + offset);
    }
    seed = last;
}

// --- Draft FIFOs (frames of interleaved stereo) ----------------------------------------------------------------------
struct Fifo
{
    float buf[2 * fifoFrames] {};
    int pos = 0;

    // input side: releases whole multiples of 16 frames into `out`; returns how many
    int push (const float* in, float* out, int n) noexcept
    {
        const int k = std::min (fifoFrames - pos, n);
        std::memcpy (buf + 2 * pos, in, 8 * (std::size_t) k);
        pos += k;
        if (pos < fifoFrames)
            return 0;
        std::memcpy (out, buf, 8 * fifoFrames);
        const int r = (n - k) % fifoFrames, m = n - k - r;
        std::memcpy (out + 2 * fifoFrames, in + 2 * k, 8 * (std::size_t) m);
        std::memcpy (buf, in + 2 * (k + m), 8 * (std::size_t) r);
        pos = r;
        return fifoFrames + m;
    }

    // output side: `nout` frames from what is queued, then from `in` (nin frames); the rest is queued
    void pull (const float* in, int nin, float* out, int nout) noexcept
    {
        const int k = std::min (pos, nout);
        std::memcpy (out, buf, 8 * (std::size_t) k);
        std::memmove (buf, buf + 2 * k, 8 * (std::size_t) (fifoFrames - k));
        pos -= k;
        const int c = std::min (nout - k, nin);
        std::memcpy (out + 2 * k, in, 8 * (std::size_t) c);
        const int rest = nin - c;
        std::memcpy (buf + 2 * pos, in + 2 * c, 8 * (std::size_t) rest);
        pos += rest;
    }
};

// one analysis (modulator) or filtering (carrier) voice of a band
struct Voice
{
    double oct = 0, width = 0, f = 0, bwf = 0, pitch = 1, pitchMap = 0, gainMap = 0, panMap = 0, offsetMap = 0;
    float pan = 0, gain = 0, inv = 0, threshold = 0, gainL = 0, gainR = 0;
    int oversample = 1;
    std::array<Biquad, maxOrder> filters {};
    SlidingMax peak;
    MovingRms rms;
    std::array<Smoother, smootherStages> smoother {};
    int stages = 0;
    float last = 0.0f;
};

struct Band
{
    std::array<Voice, maxVoices> voices;
    double modPass = 0, satMix = 0, holdMul = 1, attMul = 1, relMul = 1;
    bool saturate = false;
    float offset = 0, carrierGain = 0;
    int level = 0;
};
} // namespace

// --- state ------------------------------------------------------------------------------------------------------------
std::optional<State> parseState (const std::vector<std::uint8_t>& d)
{
    auto i32 = [&d] (std::size_t o) {
        std::int32_t v = 0;
        std::memcpy (&v, d.data() + o, 4);
        return v;
    };
    auto f64 = [&d] (std::size_t o) {
        double v = 0;
        std::memcpy (&v, d.data() + o, 8);
        return v;
    };
    if (d.size() < 4)
        return std::nullopt;
    State s;
    auto version = (std::uint32_t) i32 (0);
    if (version > 0xffff)
        version >>= 16;
    if (version < 5 || version > 8) // older blocks are laid out differently
        return std::nullopt;
    s.version = (int) version;
    std::size_t o = 4;
    for (auto& m : s.maps)
    {
        if (o + 12 > d.size())
            return std::nullopt;
        const auto n = i32 (o + 8);
        if (n < 0 || n > 4096 || o + 12 + 24 * (std::size_t) n + 20 > d.size())
            return std::nullopt;
        o += 12;
        double x = 0.0;
        for (int i = 0; i < n; ++i, o += 24)
        {
            x += f64 (o);
            m.x.push_back (x);
            m.y.push_back (f64 (o + 8));
            float t = 0.0f;
            std::memcpy (&t, d.data() + o + 16, 4);
            m.tension.push_back (t);
            m.mode.push_back (d[o + 20]);
        }
        o += 20;
    }
    if (o + 20 * 4 + 0x24 > d.size())
        return std::nullopt;
    s.paramsAt = o;
    for (auto& p : s.params)
        p = i32 (o), o += 4;
    s.holdMode = std::clamp (i32 (o), 0, 1);
    s.order = std::clamp (i32 (o + 4), 1, maxOrder);
    s.unison = std::clamp (i32 (o + 8), 0, 4);
    s.bands = std::clamp (i32 (o + 12), 5, 100);
    s.draftField = i32 (o + 16);
    s.modIn = i32 (o + 20);
    s.carIn = i32 (o + 24);
    s.lrEncoding = d[o + 28] != 0;
    s.minTimes = d[o + 29] != 0;
    s.toMod = d[o + 30] != 0;
    s.threaded = d[o + 31] != 0;
    s.subtractiveGate = d[o + 32] != 0;
    s.toNoise = d[o + 33] != 0;
    s.fromCar = d[o + 34] != 0;
    s.fromConsonants = d[o + 35] != 0;
    o += 0x24;
    if (o + 13 <= d.size())
    {
        std::memcpy (s.mapAxis.data(), d.data() + o, 13);
        o += 13;
        if (o < d.size() && o + 1 + d[o] <= d.size())
        {
            s.tone.assign ((const char*) d.data() + o + 1, d[o]);
            o += 1 + (std::size_t) d[o];
            if (o + 4 <= d.size())
            {
                const auto k = i32 (o);
                o += 4;
                for (int i = 0; i < k && i < 128 && o + 4 <= d.size(); ++i, o += 4)
                    s.notes.push_back (i32 (o));
            }
        }
    }
    return s;
}

// --- map editing -----------------------------------------------------------------------------------------------------
namespace
{
// byte offset and length of each of the 13 map records in FL's state
std::optional<std::array<std::pair<std::size_t, std::size_t>, 13>> mapRecords (const std::vector<std::uint8_t>& d)
{
    if (! parseState (d))
        return std::nullopt;
    std::array<std::pair<std::size_t, std::size_t>, 13> out {};
    std::size_t o = 4;
    for (auto& r : out)
    {
        std::int32_t n = 0;
        std::memcpy (&n, d.data() + o + 8, 4);
        r = { o, 12 + 24 * (std::size_t) n + 20 };
        o += r.second;
    }
    return out;
}

// FL's state and LYNX's install-data trailer that follows it
std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> splitTrailer (const std::vector<std::uint8_t>& slotState)
{
    auto fl = splitInstallData (slotState).first;
    std::vector<std::uint8_t> trailer (slotState.begin() + (std::ptrdiff_t) fl.size(), slotState.end());
    return { std::move (fl), std::move (trailer) };
}

Map toMap (const std::vector<MapPoint>& points)
{
    Map m;
    for (const auto& p : points)
    {
        m.x.push_back (p.x);
        m.y.push_back (p.y);
        m.tension.push_back (p.tension);
        m.mode.push_back (p.mode);
    }
    return m;
}
} // namespace

const MapInfo& mapInfo (int map) noexcept
{
    static const std::array<MapInfo, 13> infos { {
        { "Band gain multiplier", "Equalizer", false, 0.5 },
        { "Band panning", "Equalizer", true, 0.5 },
        { "Band gain offset", "Equalizer", true, 0.5 },
        { "Modulator noise level", "Equalizer", false, 0.0 },
        { "Modulator pass-through", "Equalizer", false, 0.0 },
        { "Hold", "Envelope follower", false, 0.5 },
        { "Envelope attack", "Envelope follower", false, 0.5 },
        { "Envelope release", "Envelope follower", false, 0.5 },
        { "Band distribution", "Spectral distribution", false, 0.0 },
        { "Band width", "Spectral distribution", false, 0.5 },
        { "Modulator pitch shift", "Spectral distribution", true, 0.5 },
        { "Saturation mix", "Envelope saturation", false, 0.0 },
        { "Saturation curve", "Envelope saturation", false, 0.0 },
    } };
    return infos[(std::size_t) std::clamp (map, 0, 12)];
}

std::vector<MapPoint> defaultMap (int map)
{
    map = std::clamp (map, 0, 12);
    if (map == 8) // E2 (440·2^(−29/12) Hz) … 20 kHz on FL's frequency scale
    {
        const auto& scale = freqScale();
        const double low = std::exp (0.057762265046662105 * -29.0) * 440.0;
        return { { 0.0, (double) (float) scale.toY (low) }, { 1.0, (double) (float) scale.toY (20000.0) } };
    }
    if (map == 12) // the identity curve
        return { { 0.0, 0.0 }, { 1.0, 1.0 } };
    const double y = mapInfo (map).defaultLevel;
    return { { 0.0, y }, { 1.0, y } };
}

std::vector<MapPoint> mapPoints (const Map& map)
{
    std::vector<MapPoint> out;
    for (std::size_t i = 0; i < map.x.size(); ++i)
        out.push_back ({ map.x[i], i < map.y.size() ? map.y[i] : 0.0, i < map.tension.size() ? map.tension[i] : 0.0f,
                         i < map.mode.size() ? map.mode[i] : (std::uint8_t) curveSingle });
    return out;
}

std::optional<std::array<std::vector<MapPoint>, 13>> readMaps (const std::vector<std::uint8_t>& slotState)
{
    const auto s = parseState (splitInstallData (slotState).first);
    if (! s)
        return std::nullopt;
    std::array<std::vector<MapPoint>, 13> out;
    for (std::size_t m = 0; m < out.size(); ++m)
        out[m] = mapPoints (s->maps[m]);
    return out;
}

std::vector<std::uint8_t> withMap (const std::vector<std::uint8_t>& slotState, int map, const std::vector<MapPoint>& points)
{
    auto [fl, trailer] = splitTrailer (slotState);
    const auto records = mapRecords (fl);
    if (! records || map < 0 || map > 12 || points.empty())
        return slotState;

    // FL's editor keeps the ends at 0 and 1, levels in 0…1
    auto pts = points;
    std::stable_sort (pts.begin(), pts.end(), [] (const MapPoint& a, const MapPoint& b) { return a.x < b.x; });
    if (pts.size() == 1)
        pts.push_back (pts[0]);
    for (auto& p : pts)
    {
        p.x = std::isfinite (p.x) ? std::clamp (p.x, 0.0, 1.0) : 0.0;
        p.y = std::isfinite (p.y) ? (double) (float) std::clamp (p.y, 0.0, 1.0) : 0.0;
        p.tension = std::isfinite (p.tension) ? std::clamp (p.tension, -1.0f, 1.0f) : 0.0f;
        p.mode = std::min<std::uint8_t> (p.mode, curveDouble3);
    }
    pts.front().x = 0.0;
    pts.back().x = 1.0;
    pts.front().tension = 0.0f, pts.front().mode = curveSingle;

    // the sign byte FL saves with each point is the one its loader computes (2 = a straight segment)
    const MapReader reader (toMap (pts), 0.0);
    const auto [at, length] = (*records)[(std::size_t) map];
    std::vector<std::uint8_t> rec (fl.begin() + (std::ptrdiff_t) at, fl.begin() + (std::ptrdiff_t) (at + 12));
    const auto n = (std::int32_t) pts.size();
    std::memcpy (rec.data() + 8, &n, 4);
    double prevX = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i)
    {
        std::uint8_t p[24] {};
        const double dx = pts[i].x - prevX, y = pts[i].y;
        prevX = pts[i].x;
        std::memcpy (p, &dx, 8);
        std::memcpy (p + 8, &y, 8);
        std::memcpy (p + 16, &pts[i].tension, 4);
        p[20] = pts[i].mode;
        p[23] = i == 0 ? 0 : (std::uint8_t) reader.signOf (i);
        rec.insert (rec.end(), p, p + 24);
    }
    rec.insert (rec.end(), fl.begin() + (std::ptrdiff_t) (at + length - 20), fl.begin() + (std::ptrdiff_t) (at + length));

    std::vector<std::uint8_t> out (fl.begin(), fl.begin() + (std::ptrdiff_t) at);
    out.insert (out.end(), rec.begin(), rec.end());
    out.insert (out.end(), fl.begin() + (std::ptrdiff_t) (at + length), fl.end());
    if (! parseState (out))
        return slotState;
    out.insert (out.end(), trailer.begin(), trailer.end());
    return out;
}

std::vector<std::uint8_t> withMapFollowsPitch (const std::vector<std::uint8_t>& slotState, int map, bool follows)
{
    const auto fl = splitInstallData (slotState).first;
    const auto s = parseState (fl);
    const auto at = s ? s->paramsAt + 80 + 0x24 + (std::size_t) map : 0;
    if (! s || map < 0 || map > 12 || at >= fl.size())
        return slotState;
    auto out = slotState;
    out[at] = follows ? 1 : 0;
    return out;
}

double mapValue (const Map& map, double x, bool centred)
{
    return MapReader (map, centred ? 0.5 : 0.0).at (x);
}

// --- engine ----------------------------------------------------------------------------------------------------------
struct Engine::Impl
{
    State st;
    double fs = 44100.0, w0 = 0.0;
    int maxBlock = 0;
    float maxSlope = 0.001f;
    bool draft = false;
    int levels = 1; // signal levels in use: 5 with Draft, 1 without

    std::vector<Band> bands;
    int bandCount = 0, unison = 0, order = 2;
    float wet = 0, wetNow = 0, carPass = 0, carPassNow = 0, modPass = 0, modPassNow = 0, noiseAmp = 0, widthFactor = 0;
    double flat = 0, attOrder = 1, relOrder = 1;
    std::array<Biquad, 2> modHpFilters {}, carLpFilters {}, noiseHpFilters {};
    int modHpCount = 0, carLpCount = 0, noiseHpCount = 0;
    std::array<std::array<HalfBand, levelCount>, 2> analysis {};
    std::array<HalfBand, levelCount> synthesis {};
    int tail = noTail, tailCount = 0;
    float tailMax = 0.0f;

    // contour: consonant (zero-crossing) detector and/or carrier → 12 ms peak → 16/44 ms smoother
    bool contour = false;
    SlidingMax contourPeak;
    std::array<Smoother, smootherStages> contourSmoother {};
    int contourStages = 0;
    Biquad consonantHp;
    int zcWindow = 1, zcPos = 0, zcCount = 0;
    float zcOffset = 0.0f, zcPrev = 0.0f;
    std::vector<int> zcRing;

    std::uint32_t seed = 0;
    std::vector<std::int16_t> satTable; // index 0 and size + 2 are guards
    int satSize = 0x3fff;

    std::array<Fifo, 2> fifoIn {};
    Fifo fifoOut {};
    bool snap = false; // after a Flush the gains jump to their targets

    std::array<std::array<std::vector<double>, levelCount>, 2> level;
    std::array<std::vector<double>, levelCount> acc;
    std::array<bool, levelCount> touched {};
    std::vector<double> tmp64a, tmp64b;
    std::vector<float> tmp32, env, envSum, x32a, x32b, voc, outBuf, modPassBuf, carPassBuf, noise, contourBuf, mono, carBuf, modBuf, gains;

    std::array<MapReader, 13> maps;
    double map (int m, double x) const noexcept { return maps[(std::size_t) m].at (x); }

    void setupBands();
    void setupPassFilters();
    void setupSaturation();
    void clear() noexcept;
    bool takeOver (const Impl& o) noexcept;
    void filterBand (Voice& u, int sig, int lvl, int n, double* out) noexcept;
    void accumulate (int lv, const double* a, const double* b, double scale, int n2) noexcept;
    void saturate (float* e, int n, float dry, float wetGain, float sc) const noexcept;
    void process (const float* own, const float* mod, const float* car, float* dst, int n) noexcept;
};

void Engine::Impl::setupBands()
{
    const auto& s = st;
    const auto& scale = freqScale();
    const int N = s.bands, fat = s.unison;
    bandCount = N, unison = fat, order = s.order;
    bands.assign ((std::size_t) N, Band {});
    widthFactor = (float) (std::exp ((double) ((float) s.params[bandWidth] * 0.00100000005f) * std::log (5.0)) - 1.0) * 0.5f;
    flat = (double) ((float) s.params[flatness] * 0.00999999978f);
    attOrder = (double) powFloat (2.0f, (float) s.params[attackCurve] * 0.00100000005f);
    relOrder = (double) powFloat (2.0f, (float) s.params[releaseCurve] * 0.00100000005f);
    const double c2 = std::exp (0.057762265046662105 * -33.0) * 440.0;
    const double invOrderSpan = order > 1 ? 1.0 / (double) (order - 1) : 0.0;
    // unison detune per voice (v = 0, −1, −2, …) for U = 1…4
    static constexpr float detune[30] { 0, 0, 0, 0, 0, -1, 1, 0, 0, 0, -1, 0, 1, 0, 0, -1, 0.333333343f, -0.333333343f, 1, 0,
                                        -1, 0.5f, 0, -0.5f, 1, 0, 0, 0, 0, 0 };

    for (int v = -fat; v < 2; ++v)
    {
        double g0 = (double) (s.holdMode == 1) * 0.5 + 1.0;
        if (order > 1)
            g0 = g0 / ((double) order - flat * (double) (order - 1));
        const double q = std::sqrt (4.0 / std::sqrt ((double) N));
        g0 = g0 * q * (double) powFloat (2.0f, (float) (order - 1)) * 3.0;

        for (int b = 0; b < N; ++b)
        {
            auto& B = bands[(std::size_t) b];
            auto& u = B.voices[(std::size_t) (v + 4)];
            double f = scale.toHz (map (8, (double) b / (double) (N - 1)));
            if (f <= 20.0)
                f = 20.0;
            u.f = f;
            double pos[2];
            pos[0] = pos[1] = scale.toY (u.f);
            const auto& axis = s.mapAxis;
            if (v < 1)
            {
                u.pitchMap = map (10, pos[0]) * -2.0;
                double t = u.pitchMap;
                if (fat > 0)
                    t = t + (double) (detune[fat * 5 + (-v)] * (float) s.params[unisonPitch] * 0.00100000005f);
                u.pitch = std::exp (((double) s.params[modPitch] * -0.0008333333333333334 + t) * ln2);
                pos[1] = scale.toY (u.f * u.pitch);
                u.gainMap = curve3 (map (0, pos[axis[0]]));
                u.panMap = map (1, pos[axis[1]]) * 2.0;
                {
                    const double m = map (2, pos[axis[2]]) * 2.0;
                    const double e = std::exp ((double) (float) std::fabs (m) * std::log (1025.0));
                    const double sign = m == 0 ? 0 : (m < 0 ? -1 : 1);
                    u.offsetMap = (double) (((float) (e - 1.0) / 1024.0f) * (float) sign);
                }
                {
                    const double m = map (11, pos[axis[11]]);
                    if (m == 0.0)
                        B.saturate = false;
                    else
                        B.saturate = true, B.satMix = m;
                }
                B.holdMul = (double) powFloat (2.0f, (float) (map (5, pos[axis[5]]) * 6.0)) * 0.125;
                B.attMul = (double) powFloat (2.0f, (float) (map (6, pos[axis[6]]) * 6.0)) * 0.125;
                B.relMul = (double) powFloat (2.0f, (float) (map (7, pos[axis[7]]) * 6.0)) * 0.125;
                B.modPass = (double) ((float) curve3 (map (4, pos[axis[4]])) * 0.5f);
                {
                    const double m = map (3, pos[1]); // the gate map is always read at the shifted position
                    const double e = std::exp ((double) (float) m * std::log (1025.0));
                    u.threshold = (float) ((double) (float) (e - 1.0) * 0.0009765625);
                }
                u.bwf = (double) powFloat (2.0f, (float) s.params[modBandWidth] * 0.00100000005f) * 0.25;
                u.gain = (float) (g0 * u.gainMap);
                u.pan = (float) u.panMap;
                B.offset = (float) u.offsetMap;
                if (axis[9] != 0)
                    pos[0] = pos[1];
            }
            else
            {
                u.gain = (float) g0;
                u.pitch = 1.0;
                u.bwf = 1.0;
            }
            const float w9 = powFloat (2.0f, (float) (map (9, pos[0]) * 4.0));
            u.bwf = u.bwf * (double) widthFactor * 0.25 * (double) w9;
            u.oct = std::log (u.f / c2) * 1.4426950408889634;
        }

        for (int b = 0; b < N; ++b)
        {
            auto& B = bands[(std::size_t) b];
            auto& u = B.voices[(std::size_t) (v + 4)];
            double d = 0.0;
            if (b > 0)
                d = 0.0 + std::fabs (bands[(std::size_t) b - 1].voices[(std::size_t) (v + 4)].oct - u.oct);
            if (b < N - 1)
            {
                d = d + std::fabs (bands[(std::size_t) b + 1].voices[(std::size_t) (v + 4)].oct - u.oct);
                if (b > 0)
                    d = d * (double) 0.5f;
            }
            d = d * u.bwf;
            double w = 0.016666666666666666;
            if (0.016666666666666666 <= d)
                w = 8.0 < d ? 8.0 : d;
            u.width = w;
            const double g = std::sqrt (1.0 / u.width);
            u.gain = (float) ((double) u.gain * g);
            float gg = u.gain;
            if (gg <= 1.52587890625e-05f)
                gg = 1.52587890625e-05f;
            u.inv = 1.0f / gg;
            u.threshold = u.threshold * u.inv;
            if (s.subtractiveGate)
                u.threshold = -u.threshold;
            if (v < 1)
                B.modPass = B.modPass * g0 * g * 0.25;
            for (int i = 0; i < order; ++i)
            {
                auto& qq = u.filters[(std::size_t) i];
                const double ww = u.width * (double) order;
                double bw = 2.0;
                if (ww < 2.0)
                    bw = ww;
                qq.bw = (float) bw;
                double off = 0.0;
                if (order > 1)
                {
                    qq.bw = (float) ((double) qq.bw / (flat * (double) 0.5f + 1.0));
                    off = (((double) (i % order)) * invOrderSpan - 0.5) * flat * (double) qq.bw;
                    if (order == 3)
                    {
                        if (i != 1)
                            qq.bw = qq.bw * 0.5f;
                    }
                    else if (order == 4 && (i == 0 || i == 3))
                        qq.bw = (float) ((double) qq.bw * 0.36363636363636365);
                }
                qq.f = (float) (c2 * u.pitch * std::exp ((u.oct + off) * ln2));
                qq.s = {};
            }
        }
    }

    const double att = (double) (float) (std::exp ((double) ((float) s.params[attack] * 9.99999975e-05f) * std::log (501.0)) - 1.0) * 2.0 * 0.001;
    const double rel = (double) (float) (std::exp ((double) ((float) s.params[release] * 9.99999975e-05f) * std::log (501.0)) - 1.0) * 2.0 * 0.001 * fs * 2.0;
    const double holdT = (double) (float) (std::exp ((double) ((float) s.params[hold] * 9.99999975e-05f) * std::log (501.0)) - 1.0) * 2.0 * 0.001 * fs * 0.25;
    tailMax = 0.0f;
    for (int b = 0; b < N; ++b)
    {
        auto& B = bands[(std::size_t) b];
        B.carrierGain = B.voices[5].gain;
        B.offset = B.offset * B.carrierGain;

        // oversampling per voice (any edge above 11 050 Hz) and, in Draft, the band's octave level
        double rr = 0.0;
        for (int v = -fat; v < 2; ++v)
        {
            auto& u = B.voices[(std::size_t) (v + 4)];
            double mx = 0.0;
            for (int i = 0; i < order; ++i)
            {
                const double e = (double) u.filters[(std::size_t) i].f * std::exp ((double) (u.filters[(std::size_t) i].bw * 2.0f) * ln2);
                if (mx <= e)
                    mx = e;
            }
            u.oversample = (11050.0 < mx) + 1;
            double r = 11050.0;
            if (u.oversample < 2)
            {
                r = rr;
                if (rr <= mx)
                    r = mx;
            }
            rr = r;
        }
        B.level = 0;
        if (draft)
            while (B.level < 4 && rr * (double) (1u << (B.level + 1)) < 11050.0)
                ++B.level;
        const double dec = 1.0 / (double) (1u << B.level);

        for (int v = -fat; v < 1; ++v)
        {
            auto& u = B.voices[(std::size_t) (v + 4)];
            if (fat > 0)
                u.pan = (float) ((double) u.pan + ((double) ((b - v) % (fat + 1)) / (double) fat - 0.5) * (double) s.params[unisonPan] * (double) 0.0078125f);
            const double gs = B.saturate ? 1.0 : (double) u.gain;
            const float pan = std::clamp (u.pan, -1.0f, 1.0f);
            const float gain = (float) ((gs * (double) B.carrierGain) / (double) (fat + 1));
            if (! std::signbit (pan))
                u.gainR = gain, u.gainL = (1.0f - pan) * gain;
            else
                u.gainL = gain, u.gainR = (pan + 1.0f) * gain;

            double minT = 0.0;
            if (s.minTimes)
            {
                const auto& qq = u.filters[0];
                const double e = (double) qq.f * std::exp ((double) (-3.0f - qq.bw) * ln2);
                double c = 40.0;
                if (40.0 <= e)
                    c = 16000.0 < e ? 16000.0 : e;
                minT = fs / c;
                if (s.holdMode == 1)
                    minT = minT * 2.0;
            }
            double holdS = holdT * B.holdMul;
            if (holdS <= minT)
                holdS = minT;
            holdS = holdS * dec;
            if (s.holdMode == 0)
                u.peak.setup ((int) std::nearbyint (holdS));
            else
                u.rms.setup ((int) std::nearbyint (holdS));

            double minT2 = 0.0;
            if (s.minTimes)
            {
                const auto& qq = B.voices[5].filters[0];
                const double e = (double) qq.f * std::exp ((double) (float) (-qq.bw) * ln2);
                double c = 40.0;
                if (40.0 <= e)
                    c = 800.0 < e ? 800.0 : e;
                minT2 = fs / c;
            }
            double a = att * fs * B.attMul;
            if (a <= minT2)
                a = minT2;
            double rl = rel * B.relMul;
            if (rl <= minT2)
                rl = minT2;
            rl = rl * dec;
            u.stages = setupSmoother (u.smoother, a * dec, attOrder, rl, relOrder);
            u.last = 0.0f;
            const float t = (float) (holdS + rl);
            if (tailMax <= t)
                tailMax = t;
        }
        for (int v = -fat; v < 2; ++v)
        {
            auto& u = B.voices[(std::size_t) (v + 4)];
            for (int i = 0; i < order; ++i)
            {
                auto& qq = u.filters[(std::size_t) i];
                double f2 = (double) ((qq.f * (float) (1u << B.level)) / (float) u.oversample);
                const double cap = fs * 0.4;
                if (cap <= f2)
                    f2 = cap;
                qq.f = (float) f2;
                qq.bandPass (w0);
            }
        }
    }
}

void Engine::Impl::setupPassFilters()
{
    static constexpr double q[2] { 0.356510361215121, 0.943792598854838 };
    auto hz = [] (int v) { return (float) (std::exp ((((double) v * std::log (375.0)) / (std::log (2.0) * 65536.0)) * ln2) * 40.0); };
    auto design = [&] (std::array<Biquad, 2>& f, int& count, bool on, bool high, int v) {
        count = on ? 2 : 0;
        for (int i = 0; i < 2 && on; ++i)
        {
            f[(std::size_t) i] = Biquad {};
            f[(std::size_t) i].f = hz (v);
            f[(std::size_t) i].lowHighPass (high, w0, q[i]);
        }
    };
    design (modHpFilters, modHpCount, st.params[Param::modHp] != 0, true, st.params[Param::modHp]);
    design (carLpFilters, carLpCount, st.params[Param::carLp] != 0x10000, false, st.params[Param::carLp]);
    design (noiseHpFilters, noiseHpCount, st.params[Param::carNoiseHp] != 0, true, st.params[Param::carNoiseHp]);
}

// 16 384-step shaping table from the saturation-curve map (log-spaced x)
void Engine::Impl::setupSaturation()
{
    const int n = satSize;
    satTable.assign ((std::size_t) n + 4, 0);
    auto* t = satTable.data() + 1;
    const double step = 4.0 / (double) n;
    double x = 0.0;
    auto cursor = maps[12].begin(); // one cursor stepped along the table, as FL builds it
    for (int i = 0; i <= n; ++i)
    {
        const double m = curve3 (cursor.value);
        const double v = std::clamp (std::nearbyint (m * 8191.75), -32768.0, 32767.0);
        t[i] = (std::int16_t) v;
        const double l = dmath::ln ((double) ((float) ((double) (i + 1) * step) * 2.0f + 1.0f));
        const double next = (double) (float) (l * (double) 0.91023922f) * 0.5;
        maps[12].advance (cursor, next - x);
        x = next;
    }
    t[-1] = t[0];
    t[n + 1] = t[n];
}

// FL's kernel (FUN_0062bc80) runs on the x87 in extended precision with truncating conversions: the index is the
// truncated x·(sc·size) and the interpolation and the final sum stay in extended precision until the float store
void Engine::Impl::saturate (float* e, int n, float dry, float wetGain, float sc) const noexcept
{
    const auto* t = satTable.data() + 1;
    const int size = satSize;
    const X87Extended precision;
    const long double scale = (long double) sc * (long double) size;
    const long double wet = (long double) wetGain * (long double) 3.0517578125e-05f, dryGain = dry;
    for (int k = 0; k < n; ++k)
    {
        const long double x = e[k];
        const long double f = x * scale;
        long double y = 0.0L;
        if (std::fabs (f) < 2147483648.0L) // (out of range, FL's conversion gives the "indefinite" integer: 0)
        {
            const auto i = (long long) f; // truncation
            const long double fr = f - (long double) i;
            const auto j = (int) std::min<long long> (i < 0 ? -i : i, size);
            const long double a = t[j], b = t[j + 1];
            y = i >= 0 ? a + (b - a) * fr : -(a - (b - a) * fr);
        }
        e[k] = (float) (x * dryGain + y * wet);
    }
}

void Engine::Impl::clear() noexcept
{
    for (auto& B : bands)
        for (int v = -unison; v < 2; ++v)
        {
            auto& u = B.voices[(std::size_t) (v + 4)];
            for (auto& f : u.filters)
                f.s = {};
            u.peak.clear();
            u.rms.clear();
            for (auto& sm : u.smoother)
                sm.y = 0.0;
            u.last = 0.0f;
        }
    for (auto* filters : { &modHpFilters, &carLpFilters, &noiseHpFilters })
        for (auto& f : *filters)
            f.s = {};
    for (auto& side : analysis)
        for (auto& h : side)
            h.clear();
    for (auto& h : synthesis)
        h.clear();
    contourPeak.clear();
    for (auto& sm : contourSmoother)
        sm.y = 0.0;
    consonantHp.s = {};
    std::fill (zcRing.begin(), zcRing.end(), 0);
    zcPos = zcCount = 0;
    zcPrev = 0.0f;
    fifoIn = {};
    fifoOut = {};
    fifoOut.pos = fifoFrames; // 16 zero frames queued: the Draft latency
    seed = 0;
    tailCount = 0;
}

bool Engine::Impl::takeOver (const Impl& o) noexcept
{
    if (o.fs != fs || o.draft != draft || o.bandCount != bandCount || o.order != order || o.unison != unison
        || o.st.holdMode != st.holdMode || o.maxBlock != maxBlock || o.bands.size() != bands.size())
        return false;
    auto copyRing = [] (auto& to, const auto& from) {
        if (to.size() == from.size())
            std::copy (from.begin(), from.end(), to.begin());
        return to.size() == from.size();
    };
    for (std::size_t b = 0; b < bands.size(); ++b)
    {
        auto& B = bands[b];
        const auto& OB = o.bands[b];
        if (B.level != OB.level)
            continue; // (filtered at another octave level: that band starts afresh)
        for (int v = -unison; v < 2; ++v)
        {
            auto& u = B.voices[(std::size_t) (v + 4)];
            const auto& ou = OB.voices[(std::size_t) (v + 4)];
            if (u.oversample == ou.oversample)
                for (int i = 0; i < order; ++i)
                    u.filters[(std::size_t) i].s = ou.filters[(std::size_t) i].s;
            if (u.peak.window == ou.peak.window && copyRing (u.peak.value, ou.peak.value) && copyRing (u.peak.when, ou.peak.when))
                u.peak.head = ou.peak.head, u.peak.count = ou.peak.count, u.peak.clock = ou.peak.clock;
            if (u.rms.window == ou.rms.window && copyRing (u.rms.ring, ou.rms.ring))
                u.rms.pos = ou.rms.pos, u.rms.sum = ou.rms.sum;
            for (std::size_t k = 0; k < u.smoother.size(); ++k)
                u.smoother[k].y = ou.smoother[k].y;
            u.last = ou.last;
        }
    }
    for (std::size_t i = 0; i < 2; ++i)
    {
        modHpFilters[i].s = o.modHpFilters[i].s;
        carLpFilters[i].s = o.carLpFilters[i].s;
        noiseHpFilters[i].s = o.noiseHpFilters[i].s;
    }
    analysis = o.analysis;
    synthesis = o.synthesis;
    if (contour && o.contour && contourPeak.window == o.contourPeak.window && copyRing (contourPeak.value, o.contourPeak.value)
        && copyRing (contourPeak.when, o.contourPeak.when) && copyRing (zcRing, o.zcRing))
    {
        contourPeak.head = o.contourPeak.head, contourPeak.count = o.contourPeak.count, contourPeak.clock = o.contourPeak.clock;
        contourSmoother = o.contourSmoother;
        consonantHp.s = o.consonantHp.s;
        zcPos = o.zcPos, zcCount = o.zcCount, zcPrev = o.zcPrev;
    }
    fifoIn = o.fifoIn;
    fifoOut = o.fifoOut;
    seed = o.seed;
    tailCount = o.tailCount;
    wetNow = o.wetNow, carPassNow = o.carPassNow, modPassNow = o.modPassNow; // the gains glide to the new targets
    snap = o.snap;
    return true;
}

void Engine::Impl::filterBand (Voice& u, int sig, int lvl, int n, double* out) noexcept
{
    if (u.oversample < 2)
    {
        const double* in = level[(std::size_t) sig][(std::size_t) lvl + 1].data();
        for (int i = 0; i < order; ++i)
        {
            u.filters[(std::size_t) i].run64 (in, out, n);
            in = out;
        }
    }
    else // 2× copy, brought back by averaging pairs
    {
        const double* in = level[(std::size_t) sig][0].data();
        double* t = tmp64b.data();
        for (int i = 0; i < order; ++i)
        {
            u.filters[(std::size_t) i].run64 (in, t, 2 * n);
            in = t;
        }
        for (int k = 0; k < n; ++k)
        {
            out[2 * k] = (t[4 * k] + t[4 * k + 2]) * 0.5;
            out[2 * k + 1] = (t[4 * k + 1] + t[4 * k + 3]) * 0.5;
        }
    }
}

// acc[lv] (+)= a·b, or a·scale without b
void Engine::Impl::accumulate (int lv, const double* a, const double* b, double scale, int n2) noexcept
{
    double* d = acc[(std::size_t) lv].data();
    const bool first = ! touched[(std::size_t) lv];
    if (b != nullptr)
    {
        if (first)
            for (int k = 0; k < n2; ++k)
                d[k] = a[k] * b[k];
        else
            for (int k = 0; k < n2; ++k)
                d[k] = d[k] + a[k] * b[k];
    }
    else
    {
        if (first)
            for (int k = 0; k < n2; ++k)
                d[k] = a[k] * scale;
        else
            for (int k = 0; k < n2; ++k)
                d[k] = d[k] + a[k] * scale;
    }
    touched[(std::size_t) lv] = true;
}

namespace
{
bool silent (const float* x, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        if (std::fabs (x[i]) > 9.99999997e-07f)
            return false;
    return true;
}
} // namespace

void Engine::Impl::process (const float* own, const float* mod, const float* car, float* dst, int n) noexcept
{
    const auto& s = st;
    const int fat = unison;

    // tail bypass: both inputs silent for long enough → the own input passes and processing is frozen
    if (tail != noTail)
    {
        if (! silent (car, 2 * n) || ! silent (mod, 2 * n))
            tailCount = tail;
        else
            tailCount -= n;
        if (tailCount <= -n)
        {
            if (dst != own)
                std::memcpy (dst, own, 8 * (std::size_t) n);
            snap = true;
            return;
        }
    }

    int m = n;
    const float* C = car;
    const float* M = mod;
    if (draft)
    {
        m = fifoIn[0].push (car, carBuf.data(), n);
        fifoIn[1].push (mod, modBuf.data(), n);
        C = carBuf.data();
        M = modBuf.data();
    }
    float* out = outBuf.data();

    if (m > 0)
    {
        // contour
        const float* cont = nullptr;
        if (contour)
        {
            const float* src = nullptr;
            float* cb = contourBuf.data();
            float* mono_ = mono.data();
            if (s.fromConsonants)
            {
                src = cb;
                if (s.params[Param::freeze] == 0)
                {
                    for (int k = 0; k < m; ++k)
                        mono_[k] = M[2 * k] + M[2 * k + 1];
                    consonantHp.run32Mono (mono_, mono_, m);
                    for (int k = 0; k < m; ++k)
                        mono_[k] += zcOffset;
                    for (int k = 0; k < m; ++k)
                    {
                        const float y = mono_[k];
                        if (zcWindow <= ++zcPos)
                            zcPos = 0;
                        const int c = y * zcPrev < 0.0f;
                        zcPrev = y;
                        zcCount = (zcCount - zcRing[(std::size_t) zcPos]) + c;
                        zcRing[(std::size_t) zcPos] = c;
                        mono_[k] = (float) zcCount;
                    }
                    for (int k = 0; k < m; ++k)
                    {
                        float v = mono_[k] * 0.00625f;
                        v = v * v;
                        if (v > 1.0f)
                            v = 1.0f;
                        cb[2 * k] = cb[2 * k + 1] = v;
                    }
                }
                else
                    std::memset (cb, 0, 8 * (std::size_t) m);
            }
            if (s.fromCar)
            {
                if (src == nullptr)
                    src = C;
                else
                    for (int k = 0; k < 2 * m; ++k)
                        cb[k] = C[k] * cb[k];
            }
            for (int k = 0; k < m; ++k)
            {
                const float a = std::fabs (src[2 * k]), b = std::fabs (src[2 * k + 1]);
                mono_[k] = a <= b ? b : a;
            }
            contourPeak.run (mono_, m);
            for (int i = 0; i < contourStages; ++i)
                contourSmoother[(std::size_t) i].run (mono_, m);
            for (int k = 0; k < m; ++k)
                cb[2 * k] = cb[2 * k + 1] = mono_[k];
            cont = cb;
        }

        // carrier noise (band filters only)
        const float* CA = C;
        if (noiseAmp != 0.0f)
        {
            float* nz = noise.data();
            randUniform (nz, 2 * m, -noiseAmp, noiseAmp, seed);
            for (int i = 0; i < noiseHpCount; ++i)
                noiseHpFilters[(std::size_t) i].run32 (nz, nz, m);
            if (s.toNoise && cont != nullptr)
                for (int k = 0; k < 2 * m; ++k)
                    nz[k] = cont[k] * nz[k];
            for (int k = 0; k < 2 * m; ++k)
                nz[k] = C[k] + nz[k];
            CA = nz;
        }

        // analysis levels
        const float* sig[2] { CA, M };
        for (int i = 0; i < 2; ++i)
        {
            auto& lv = level[(std::size_t) i];
            analysis[(std::size_t) i][0].up (sig[i], x32a.data(), m);
            for (int k = 0; k < 4 * m; ++k)
                lv[0][(std::size_t) k] = (double) x32a[(std::size_t) k];
            for (int k = 0; k < 2 * m; ++k)
                lv[1][(std::size_t) k] = (double) sig[i][k];
            const float* src = sig[i];
            int len = m;
            for (int j = 2; j <= levels; ++j)
            {
                len >>= 1;
                analysis[(std::size_t) i][(std::size_t) j].down (src, x32b.data(), len);
                for (int k = 0; k < 2 * len; ++k)
                    lv[(std::size_t) j][(std::size_t) k] = (double) x32b[(std::size_t) k];
                std::memcpy (x32a.data(), x32b.data(), 8 * (std::size_t) len);
                src = x32a.data();
            }
        }

        // bands
        touched.fill (false);
        float* e = env.data();
        float* es = envSum.data();
        float* t32 = tmp32.data();
        double* ta = tmp64a.data();
        double* tb = tmp64b.data();
        for (int b = 0; b < bandCount; ++b)
        {
            auto& B = bands[(std::size_t) b];
            const int lv = B.level, nb = m >> lv;
            for (int v = -fat; v < 1; ++v)
            {
                auto& u = B.voices[(std::size_t) (v + 4)];
                filterBand (u, 1, lv, nb, ta);
                if (B.modPass != 0.0)
                    accumulate (lv + 1, ta, nullptr, B.modPass, 2 * nb);
                for (int k = 0; k < 2 * nb; ++k)
                    t32[k] = (float) ta[k];
                if (s.holdMode == 0)
                {
                    for (int k = 0; k < nb; ++k)
                    {
                        const float a = std::fabs (t32[2 * k]), c = std::fabs (t32[2 * k + 1]);
                        e[k] = a <= c ? c : a;
                    }
                    u.peak.run (e, nb);
                }
                else
                {
                    for (int k = 0; k < nb; ++k)
                    {
                        const float a = t32[2 * k];
                        float c = t32[2 * k + 1];
                        c = c * c;
                        e[k] = a * a + c;
                    }
                    u.rms.run (e, nb);
                }
                if (u.threshold != 0.0f)
                {
                    if (! s.subtractiveGate)
                    {
                        for (int k = 0; k < nb; ++k)
                            if (e[k] < u.threshold)
                                e[k] = 0.0f;
                    }
                    else
                        for (int k = 0; k < nb; ++k)
                        {
                            e[k] += u.threshold;
                            if (e[k] < 0.0f)
                                e[k] = 0.0f;
                        }
                }
                if (s.params[Param::freeze] == 0)
                {
                    if (B.saturate)
                    {
                        const double g4 = (double) u.gain * 0.25;
                        saturate (e, nb, (float) (g4 * 4.0 * (1.0 - B.satMix)), (float) (4.0 * B.satMix), (float) g4);
                    }
                    for (int i = 0; i < u.stages; ++i)
                        u.smoother[(std::size_t) i].run (e, nb);
                    if (nb > 0)
                        u.last = e[nb - 1];
                }
                else
                    for (int k = 0; k < nb; ++k)
                        e[k] = u.last;
                if (v == -fat)
                    for (int k = 0; k < nb; ++k)
                    {
                        es[2 * k] = e[k] * u.gainL;
                        es[2 * k + 1] = e[k] * u.gainR;
                    }
                else
                    for (int k = 0; k < nb; ++k)
                    {
                        es[2 * k] = e[k] * u.gainL + es[2 * k];
                        es[2 * k + 1] = e[k] * u.gainR + es[2 * k + 1];
                    }
            }
            if (B.offset != 0.0f)
            {
                for (int k = 0; k < 2 * nb; ++k)
                    es[k] += B.offset;
                if (B.offset < 0.0f)
                    for (int k = 0; k < 2 * nb; ++k)
                        if (es[k] < 0.0f)
                            es[k] = 0.0f;
            }
            filterBand (B.voices[5], 0, lv, nb, ta);
            for (int k = 0; k < 2 * nb; ++k)
                tb[k] = (double) es[k];
            accumulate (lv + 1, tb, ta, 0.0, 2 * nb);
        }

        // pass-through (written to the output base)
        {
            const float* c = C;
            if (carPass != 0.0f || carPassNow != 0.0f)
                for (int i = 0; i < carLpCount; ++i)
                {
                    carLpFilters[(std::size_t) i].run32 (c, carPassBuf.data(), m);
                    c = carPassBuf.data();
                }
            if (snap)
                carPassNow = carPass;
            float start = 0.0f;
            const float d = ramp (carPassNow, carPass, maxSlope, m, start);
            if (d == 0.0f)
            {
                if (start == 0.0f)
                    std::memset (out, 0, 8 * (std::size_t) m);
                else if (start == 1.0f)
                    std::memmove (out, c, 8 * (std::size_t) m);
                else
                    for (int k = 0; k < 2 * m; ++k)
                        out[k] = c[k] * start;
            }
            else
            {
                rampGains (start, d, m, gains.data());
                for (int k = 0; k < m; ++k)
                {
                    out[2 * k] = c[2 * k] * gains[(std::size_t) k];
                    out[2 * k + 1] = c[2 * k + 1] * gains[(std::size_t) k];
                }
            }
        }
        auto addRamped = [&] (const float* src, float start, float d) {
            if (d == 0.0f)
            {
                if (start != 0.0f)
                {
                    if (start == 1.0f)
                        for (int k = 0; k < 2 * m; ++k)
                            out[k] += src[k];
                    else
                        for (int k = 0; k < 2 * m; ++k)
                            out[k] = src[k] * start + out[k];
                }
            }
            else
            {
                rampGains (start, d, m, gains.data());
                for (int k = 0; k < m; ++k)
                {
                    out[2 * k] = src[2 * k] * gains[(std::size_t) k] + out[2 * k];
                    out[2 * k + 1] = src[2 * k + 1] * gains[(std::size_t) k] + out[2 * k + 1];
                }
            }
        };
        {
            if (snap)
                modPassNow = modPass;
            if (modPass != 0.0f || modPassNow != 0.0f)
            {
                const float* mm = M;
                float* mp = modPassBuf.data();
                for (int i = 0; i < modHpCount; ++i)
                {
                    modHpFilters[(std::size_t) i].run32 (mm, mp, m);
                    mm = mp;
                }
                if (s.toMod && cont != nullptr)
                {
                    if (mm != mp)
                    {
                        std::memcpy (mp, mm, 8 * (std::size_t) m);
                        mm = mp;
                    }
                    for (int k = 0; k < 2 * m; ++k)
                        mp[k] = cont[k] * mp[k];
                }
                float start = 0.0f;
                const float d = ramp (modPassNow, modPass, maxSlope, m, start);
                addRamped (mm, start, d);
            }
        }

        // synthesis: the level sums from the coarsest one up
        {
            int len = m >> (levels - 1);
            float* a = x32a.data();
            float* b2 = x32b.data();
            for (int j = levels; j >= 1; --j)
            {
                const double* d = acc[(std::size_t) j].data();
                const bool t = touched[(std::size_t) j];
                if (j == levels)
                    for (int k = 0; k < 2 * len; ++k)
                        a[k] = t ? (float) d[k] : 0.0f;
                else
                    for (int k = 0; k < 2 * len; ++k)
                    {
                        const float x = t ? (float) d[k] : 0.0f;
                        a[k] = x + a[k];
                    }
                if (j > 1)
                {
                    synthesis[(std::size_t) j].up (a, b2, len);
                    std::swap (a, b2);
                    len <<= 1;
                }
            }
            std::memcpy (voc.data(), a, 8 * (std::size_t) m);
        }
        {
            if (snap)
                wetNow = wet;
            float start = 0.0f;
            const float d = ramp (wetNow, wet, maxSlope, m, start);
            addRamped (voc.data(), start, d);
        }
        snap = false;
    }

    if (draft)
        fifoOut.pull (out, m, dst, n);
    else
        std::memcpy (dst, out, 8 * (std::size_t) n);
}

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (const State& state, double sampleRate, int maxBlockFrames, bool draftMode)
{
    auto& p = *impl;
    p.st = state;
    p.fs = sampleRate;
    p.maxBlock = std::max (16, maxBlockFrames);
    p.w0 = 2.0 * pi / sampleRate;
    p.maxSlope = (float) (44.1 / sampleRate);
    p.draft = draftMode;
    p.levels = draftMode ? 5 : 1;
    const auto& s = p.st;

    p.wet = curve7 ((float) s.params[Param::wet] * 0.00100000005f);
    p.carPass = (float) curve11 (s.params[carVol], 7.81249983e-05f);
    p.contour = (s.toMod || s.toNoise) && (s.fromCar || s.fromConsonants);
    p.modPass = (float) curve11 (s.params[modVol], 7.81249983e-05f);
    if (s.toMod && s.fromCar)
        p.modPass = s.params[Param::freeze] == 1 ? 0.0f : p.modPass * 4.0f;
    p.noiseAmp = curve7 ((float) s.params[carNoise] * 7.81249983e-05f) * 0.5f;
    if (s.toNoise && s.fromCar)
        p.noiseAmp *= 4.0f;

    for (std::size_t m = 0; m < p.maps.size(); ++m)
        p.maps[m] = MapReader (s.maps[m], mapCentre[m]);
    p.setupBands();
    p.setupPassFilters();
    p.setupSaturation();
    if (p.contour)
    {
        p.contourPeak.setup ((int) std::nearbyint (sampleRate * 0.012));
        p.contourStages = setupSmoother (p.contourSmoother, sampleRate * 0.016, 3.0, sampleRate * 0.044, 3.0);
        p.consonantHp = Biquad {};
        p.consonantHp.f = 90.0f;
        const double sn = std::sin ((double) p.consonantHp.f * p.w0);
        p.consonantHp.lowHighPass (true, p.w0, sn / sn);
        p.zcOffset = (float) std::exp ((double) (0.115129255f * -42.0f)); // 10^(−42/20)
        p.zcWindow = std::max (1, (int) std::nearbyint (8.0 * 0.001 * sampleRate));
        p.zcRing.assign ((std::size_t) p.zcWindow, 0);
    }

    const auto n = (std::size_t) p.maxBlock + 64;
    for (auto& side : p.level)
        for (auto& v : side)
            v.assign (4 * n, 0.0);
    for (auto& v : p.acc)
        v.assign (4 * n, 0.0);
    p.tmp64a.assign (4 * n, 0.0);
    p.tmp64b.assign (4 * n, 0.0);
    for (auto* v : { &p.tmp32, &p.x32a, &p.x32b })
        v->assign (4 * n, 0.0f);
    for (auto* v : { &p.env, &p.envSum, &p.voc, &p.outBuf, &p.modPassBuf, &p.carPassBuf, &p.noise, &p.contourBuf, &p.mono,
                     &p.carBuf, &p.modBuf, &p.gains })
        v->assign (2 * n, 0.0f);

    p.tail = s.params[carNoise] == 0 && ! s.internalCarrier() ? (int) std::nearbyint (sampleRate * 0.5 + (double) p.tailMax + 16.0) : noTail;
    p.clear();
    p.snap = false;
    p.wetNow = p.carPassNow = p.modPassNow = 0.0f; // FL fades the output in when the plugin is created
}

void Engine::flush() noexcept
{
    impl->clear();
    impl->snap = true;
}

bool Engine::takeOver (const Engine& old) noexcept
{
    return impl->takeOver (*old.impl);
}

void Engine::process (const float* own, const float* mod, const float* car, float* out, int n) noexcept
{
    impl->process (own, mod, car, out, n);
}

int Engine::latencySamples() const noexcept { return impl->draft ? fifoFrames : 0; }
int Engine::maxBlock() const noexcept { return impl->maxBlock; }

// --- install data (LYNX's trailer after FL's state) ------------------------------------------------------------------
namespace
{
constexpr char dataTag[8] { 'L', 'X', 'V', 'X', 'D', 'A', 'T', 'A' };

void putU32 (std::vector<std::uint8_t>& v, std::uint32_t x)
{
    for (int k = 0; k < 4; ++k)
        v.push_back ((std::uint8_t) (x >> (8 * k)));
}

std::uint32_t getU32 (const std::uint8_t* p)
{
    return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24);
}
} // namespace

std::vector<std::uint8_t> appendInstallData (std::vector<std::uint8_t> flState, const InstallData& data)
{
    std::vector<std::uint8_t> body;
    auto record = [&body] (std::uint32_t tag, const std::vector<std::uint8_t>& bytes) {
        if (bytes.empty())
            return;
        putU32 (body, tag);
        putU32 (body, (std::uint32_t) bytes.size());
        body.insert (body.end(), bytes.begin(), bytes.end());
    };
    record (1, data.sgDat);
    record (2, data.carrierState);
    if (body.empty())
        return flState;
    flState.insert (flState.end(), body.begin(), body.end());
    putU32 (flState, (std::uint32_t) body.size());
    flState.insert (flState.end(), dataTag, dataTag + 8);
    return flState;
}

std::pair<std::vector<std::uint8_t>, InstallData> splitInstallData (const std::vector<std::uint8_t>& slotState)
{
    InstallData data;
    const auto n = slotState.size();
    if (n < 12 || std::memcmp (slotState.data() + n - 8, dataTag, 8) != 0)
        return { slotState, data };
    const auto len = (std::size_t) getU32 (slotState.data() + n - 12);
    if (len > n - 12)
        return { slotState, data };
    const auto begin = n - 12 - len;
    for (auto at = begin; at + 8 <= n - 12;)
    {
        const auto tag = getU32 (slotState.data() + at);
        const auto size = (std::size_t) getU32 (slotState.data() + at + 4);
        at += 8;
        if (size > n - 12 - at)
            break;
        std::vector<std::uint8_t> bytes (slotState.begin() + (std::ptrdiff_t) at, slotState.begin() + (std::ptrdiff_t) (at + size));
        if (tag == 1)
            data.sgDat = std::move (bytes);
        else if (tag == 2)
            data.carrierState = std::move (bytes);
        at += size;
    }
    return { std::vector<std::uint8_t> (slotState.begin(), slotState.begin() + (std::ptrdiff_t) begin), data };
}

// --- internal carrier --------------------------------------------------------------------------------------------------
namespace
{
// REA_rodada35 §8 + REA_rodada40 §5: Vocodex hosts its own Sytrus build and triggers one voice per latched key with
// Init/Final levels pan 0, volume 0.5, Final.Pitch = 100·key − 6000 cents (= the key itself) and FCut/FRes 0; its host
// answers the voice events 3 (velocity) 0.78125 and 5 (release) 1.0, colour 0 (FUN_0084c1c0). The keys stay held.
class SytrusCarrier final : public Carrier
{
public:
    SytrusCarrier (std::shared_ptr<const sytrus::Patch> p, std::vector<int> keys) : patch (std::move (p)), notes (std::move (keys)) {}

    void prepare (double sampleRate) override
    {
        engine.prepare (sampleRate, offlineRendering(), block);
        engine.setPatch (patch);
        std::int64_t id = 0;
        for (const auto key : notes)
        {
            sytrus::NoteParams note;
            note.key = (float) key;
            note.cents = 0.0f;
            note.velocity = 0.78125f;
            note.releaseVelocity = 1.0f;
            note.pan = 0.0f;
            note.id = ++id;
            engine.noteOn (note);
        }
        left.assign ((std::size_t) block, 0.0f);
        right.assign ((std::size_t) block, 0.0f);
    }

    void render (float* out, int n) noexcept override
    {
        for (int done = 0; done < n; done += block)
        {
            const auto len = std::min (block, n - done);
            std::fill (left.begin(), left.begin() + len, 0.0f);
            std::fill (right.begin(), right.begin() + len, 0.0f);
            engine.render (left.data(), right.data(), len, 0.5f, 0.0f);
            for (int k = 0; k < len; ++k)
                out[2 * (done + k)] = left[(std::size_t) k], out[2 * (done + k) + 1] = right[(std::size_t) k];
        }
    }

    void setTempo (double bpm) noexcept { engine.setTempo (bpm); }

private:
    static constexpr int block = 512;
    std::shared_ptr<const sytrus::Patch> patch;
    std::vector<int> notes;
    sytrus::Engine engine;
    std::vector<float> left, right;
};
} // namespace

std::unique_ptr<Carrier> makeInternalCarrier (const State&)
{
    return nullptr; // the Sytrus preset comes from the user's installation: see the overload below
}

std::unique_ptr<Carrier> makeInternalCarrier (const State& state, const std::vector<std::uint8_t>& sytrusState)
{
    if (sytrusState.empty() || state.notes.empty())
        return nullptr;
    auto patch = sytrus::parsePatch (sytrusState);
    if (patch == nullptr)
        return nullptr;
    return std::make_unique<SytrusCarrier> (std::move (patch), state.notes);
}

// --- the effect --------------------------------------------------------------------------------------------------------
namespace
{
class VocodexEffect final : public Effect
{
public:
    static constexpr int chunk = 2048;

    explicit VocodexEffect (const std::vector<std::uint8_t>& slotState)
    {
        auto [raw, data] = splitInstallData (slotState);
        if (auto parsed = parseState (raw))
        {
            cur = std::make_unique<Running>();
            cur->st = *parsed;
            accepted = *parsed;
            trailer.assign (slotState.begin() + (std::ptrdiff_t) raw.size(), slotState.end());
        }
        const State* state = cur != nullptr ? &cur->st : nullptr;
        if (state && state->internalCarrier())
            carrier = makeInternalCarrier (*state, data.carrierState);
        if (state && state->params[sg] > 0)
        {
            // FL's SG stage: the shared Maximus build with Vocodex/Data/SG.dat and parameter 5 = SG (REA_rodada40 §4)
            if (! data.sgDat.empty())
                sgPreset = hlfx::makeMaximusPreset (data.sgDat);
            if (sgPreset == nullptr)
            {
                sgSlots = soundgoodizerApproximation (0, state->params[sg]);
                for (const auto& slot : sgSlots)
                    sgEffects.push_back (makeEffect (slot.kind));
            }
        }
        for (auto* v : { &ownBuf, &modBuf, &carBuf, &outBuf, &zeros })
            v->assign (2 * chunk, 0.0f);
    }

    ~VocodexEffect() override
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    void prepare (double sr) noexcept override
    {
        delete pending.exchange (nullptr); // (set up for the previous rate)
        delete retired.exchange (nullptr);
        rate = sr;
        if (runsEngine())
            cur->engine.prepare (cur->st, sr, chunk, cur->st.draft());
        if (carrier != nullptr)
            carrier->prepare (sr);
        if (sgPreset != nullptr)
            sgEngine.prepare (sgPreset, sr, cur->st.params[sg]);
        for (auto& e : sgEffects)
            e->prepare (sr);
    }

    void reset() noexcept override
    {
        if (runsEngine())
            cur->engine.flush();
        sgEngine.reset();
        for (auto& e : sgEffects)
            e->reset();
    }

    int latencySamples() const noexcept override
    {
        if (! runsEngine())
            return 0;
        auto latency = cur->engine.latencySamples() + sgEngine.latencySamples();
        for (const auto& e : sgEffects)
            latency += e->latencySamples(); // the Soundgoodizer approximation's limiter look-ahead
        return latency;
    }

    void setSidechain (const float* left, const float* right) noexcept override
    {
        side[0] = { left, right };
        useSide[0] = true;
    }
    void setSidechain2 (const float* left, const float* right) noexcept override
    {
        side[1] = { left, right };
        useSide[1] = true;
    }

    void process (float* l, float* r, int n, const EffectParams&, double bpm) noexcept override
    {
        const auto sides = side;
        const auto used = useSide;
        useSide = { false, false };
        if (cur == nullptr)
            return;
        // an edited state (maps, options, controls) set up off the audio thread: it continues the running signal
        if (retired.load (std::memory_order_acquire) == nullptr)
            if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                next->engine.takeOver (cur->engine);
                retired.store (cur.release(), std::memory_order_release);
                cur.reset (next);
            }
        const State* state = &cur->st;
        if (auto* s = dynamic_cast<SytrusCarrier*> (carrier.get()))
            s->setTempo (bpm);

        for (int done = 0; done < n; done += chunk)
        {
            const int len = std::min (chunk, n - done);
            float* L = l + done;
            float* R = r + done;
            auto interleave = [len] (const float* a, const float* b, std::vector<float>& to) {
                for (int k = 0; k < len; ++k)
                    to[(std::size_t) (2 * k)] = a[k], to[(std::size_t) (2 * k + 1)] = b[k];
            };
            // a side input (k > 0) that is routed gives that track's signal (silence when it is quiet); one that
            // does not resolve to a track falls back to the own input
            auto input = [&] (int index, int which, std::vector<float>& to) -> const float* {
                if (index == 0 || ! used[(std::size_t) which])
                    return ownBuf.data();
                const auto& s = sides[(std::size_t) which];
                if (s[0] == nullptr || s[1] == nullptr)
                    return zeros.data();
                interleave (s[0] + done, s[1] + done, to);
                return to.data();
            };
            interleave (L, R, ownBuf);
            const float* mod = nullptr;
            const float* car = nullptr;
            if (state->lrEncoding) // own input L → modulator, R → carrier (both channels)
            {
                for (int k = 0; k < len; ++k)
                {
                    modBuf[(std::size_t) (2 * k)] = modBuf[(std::size_t) (2 * k + 1)] = L[k];
                    carBuf[(std::size_t) (2 * k)] = carBuf[(std::size_t) (2 * k + 1)] = R[k];
                }
                mod = modBuf.data();
                car = carBuf.data();
            }
            else
            {
                mod = input (state->modIn, 0, modBuf);
                if (state->carIn < 0)
                {
                    if (carrier == nullptr) // no internal carrier: the modulator passes
                    {
                        for (int k = 0; k < len; ++k)
                            L[k] = mod[2 * k], R[k] = mod[2 * k + 1];
                        continue;
                    }
                    carrier->render (carBuf.data(), len);
                    car = carBuf.data();
                }
                else
                    car = input (state->carIn, 1, carBuf);
            }
            cur->engine.process (ownBuf.data(), mod, car, outBuf.data(), len);
            for (int k = 0; k < len; ++k)
                L[k] = outBuf[(std::size_t) (2 * k)], R[k] = outBuf[(std::size_t) (2 * k + 1)];
        }
        if (runsEngine())
        {
            sgEngine.process (l, r, n); // FL's Eff_Render of the hosted SG.dll, in place on the output
            for (std::size_t i = 0; i < sgEffects.size(); ++i)
                sgEffects[i]->process (l, r, n, sgSlots[i].params, bpm);
        }
    }

    // Off the audio thread: a state that differs only in what FL's band set-up reads (maps, option bytes, controls,
    // switches) is set up here and handed over; a new layout, carrier or Soundgoodizer setting builds a new effect.
    bool acceptState (const std::vector<std::uint8_t>& slotState) noexcept override
    {
        if (cur == nullptr || rate <= 0.0 || ! runsEngine())
            return false;
        delete retired.exchange (nullptr, std::memory_order_acq_rel);
        if (pending.load (std::memory_order_acquire) != nullptr)
            return false;
        try
        {
            const auto raw = splitInstallData (slotState).first;
            const auto s = parseState (raw);
            if (! s || ! std::equal (trailer.begin(), trailer.end(), slotState.begin() + (std::ptrdiff_t) raw.size(), slotState.end()))
                return false;
            const auto& a = accepted;
            if (s->draft() != a.draft() || s->bands != a.bands || s->order != a.order || s->unison != a.unison
                || s->holdMode != a.holdMode || s->internalCarrier() != a.internalCarrier() || s->params[sg] != a.params[sg]
                || (s->internalCarrier() && (s->tone != a.tone || s->notes != a.notes)))
                return false;
            auto next = std::make_unique<Running>();
            next->st = *s;
            next->engine.prepare (next->st, rate, chunk, next->st.draft());
            pending.store (next.release(), std::memory_order_release);
            accepted = *s;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

private:
    bool runsEngine() const noexcept { return cur != nullptr && (! cur->st.internalCarrier() || carrier != nullptr); }

    struct Running
    {
        State st;
        Engine engine;
    };
    std::unique_ptr<Running> cur;                 // the audio thread's
    std::atomic<Running*> pending { nullptr };    // set up by acceptState, taken by process
    std::atomic<Running*> retired { nullptr };    // replaced by process, freed by acceptState / prepare / the destructor
    State accepted;                               // the last state taken (acceptState's thread)
    std::vector<std::uint8_t> trailer;            // the install data the carrier / SG stage were built from
    double rate = 0.0;
    std::unique_ptr<Carrier> carrier;
    std::shared_ptr<const hlfx::MaximusPreset> sgPreset;
    hlfx::MaximusProcessor sgEngine;
    std::vector<EffectSlot> sgSlots;
    std::vector<std::unique_ptr<Effect>> sgEffects;
    std::vector<float> ownBuf, modBuf, carBuf, outBuf, zeros;
    std::array<std::array<const float*, 2>, 2> side {};
    std::array<bool, 2> useSide { false, false };
};
} // namespace

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& state)
{
    return std::make_unique<VocodexEffect> (state);
}
} // namespace lynx::vocodex
