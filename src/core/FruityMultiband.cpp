#include "core/FruityMultiband.h"

#include "core/Effects.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// Rebuilt from the plugin's observed behaviour (docs/analise/REA_rodada56.md). The engine works in single precision
// throughout, one operation at a time; matching FL sample for sample needs the same operation order (no contraction
// into fused multiply-adds), FL's float sine/cosine (its runtime library's FMA path, reproduced below) and FL's
// float power function (see powF).
#if defined(__clang__)
 #pragma STDC FP_CONTRACT OFF
#else
 #pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::flmb
{
//                                               0      1  2  3   4      5      6      7      8   9      10     11 12     13
const std::array<int, controls> controlMin {     0,     0, 0, 0, 20,     0,     0,     0,    1, 100,     0,     0, 20,    20,
                                                 0,     0, 0, 1, 100,    0,     0, 20,     0,     0,     0,  1, 100,     0 };
const std::array<int, controls> controlMax { 65536,     1, 1, 2, 20000, 65536, 65536, 65536, 1000, 10000, 65536, 2, 20000, 20000,
                                             65536, 65536, 65536, 1000, 10000, 65536, 2, 20000, 65536, 65536, 65536, 1000, 10000, 65536 };
// a new FL instance
const std::array<int, controls> controlDefault { 32768, 0, 0, 0, 17000, 21845, 45875, 32768, 100, 1000, 52429,
                                                 0, 17000, 6500, 21845, 48875, 32768, 100, 1000, 52429,
                                                 0, 6500, 21845, 45875, 32768, 100, 1000, 52429 };

namespace
{
constexpr float twoPi = 6.2831854820251465f; // the float constants the engine uses
constexpr float pi = 3.1415927410125732f;
constexpr float eF = 2.7182817459106445f;
constexpr float silence = 1.0000000116860974e-07f;
constexpr int taps = 105;
constexpr int centre = 52;

inline bool zeroExponent (float x) noexcept
{
    std::uint32_t b;
    std::memcpy (&b, &x, 4);
    return (b & 0x7f800000u) == 0;
}
inline float flushed (float x) noexcept { return zeroExponent (x) ? 0.0f : x; }

// The runtime library's float sine and cosine on a CPU with FMA: argument in double, reduced by multiples of π/2
// (two-part constant, fused steps), degree-9 sine / degree-10 cosine polynomials with fused multiply-adds.
double sinPoly (double r) noexcept
{
    const auto r2 = r * r;
    auto p = std::fma (r2, 2.7557319223985893e-06, -0.0001984126984126984);
    p = std::fma (p, r2, 0.008333333333333333);
    p = std::fma (p, r2, -0.16666666666666666);
    return std::fma (p, r * r2, r);
}

double cosPolyTail (double r2) noexcept
{
    auto b = std::fma (r2, -2.755731922398589e-07, 2.4801587301587298e-05);
    b = std::fma (b, r2, -0.0013888888888888887);
    return std::fma (b, r2, 0.041666666666666664);
}

float sineF (float x) noexcept
{
    const auto xd = (double) x;
    const auto ax = std::fabs (xd);
    if (ax <= 0.78539816339744828)
    {
        if (ax >= 0.0078125)
            return (float) sinPoly (xd);
        if (ax >= 0.0001220703125)
            return (float) std::fma (-(xd * xd * xd), 0.16666666666666666, xd);
        return x;
    }
    const auto n = (int) std::fma (ax, 0.6366197723675814, 0.5);
    const auto fn = (double) n;
    const auto r = std::fma (-fn, 1.5707963267341256, ax) - fn * 6.077100506506192e-11;
    double y;
    if ((n & 1) != 0)
    {
        const auto r2 = r * r;
        y = std::fma (cosPolyTail (r2), r2 * r2, std::fma (r2, -0.5, 1.0));
    }
    else
        y = sinPoly (r);
    if ((n & 3) >= 2)
        y = -y;
    if (std::signbit (x))
        y = -y;
    return (float) y;
}

float cosineF (float x) noexcept
{
    const auto xd = (double) x;
    const auto ax = std::fabs (xd);
    if (ax <= 0.78539816339744828)
    {
        if (ax < 0.0078125)
            return ax < 0.0001220703125 ? 1.0f : (float) std::fma (-(xd * 0.5), xd, 1.0);
        const auto r2 = xd * xd;
        return (float) std::fma (cosPolyTail (r2), r2 * r2, 1.0 - r2 * 0.5);
    }
    const auto n = (int) std::fma (ax, 0.6366197723675814, 0.5);
    const auto fn = (double) n;
    const auto r = std::fma (-fn, 1.5707963267341256, ax) - fn * 6.077100506506192e-11;
    double y;
    if ((n & 1) != 0)
        y = sinPoly (r);
    else
    {
        const auto r2 = r * r;
        y = std::fma (cosPolyTail (r2), r2 * r2, 1.0 - r2 * 0.5);
    }
    if ((((n & 3) + 1) & 2) != 0)
        y = -y;
    return (float) y;
}

// The engine's float power, as its runtime library computes it on a CPU with FMA (REA_rodada56 §2.6): ln x from a
// 257-entry table of ln(1 + j/256) and a cubic correction, y·ln x in double, then e^t from a 64-entry table of
// 2^(i/64) and a cubic, the exponent added to the bit pattern, rounded to float. The tables are the exact doubles
// of those formulas. Bases within 1/16 of 1 take another route in the library (a series around 1) that is not
// reproduced: double pow rounded to float is used there (the engine only meets them in its window table).
struct PowTables
{
    std::array<double, 257> ln {}, inv {};
    std::array<double, 64> exp2 {};
    PowTables()
    {
        for (int j = 0; j < 257; ++j)
        {
            const auto f = 1.0 + j / 256.0;
            inv[(std::size_t) j] = 2.0 / f;
            ln[(std::size_t) j] = std::log (f);
        }
        for (int i = 0; i < 64; ++i)
            exp2[(std::size_t) i] = std::exp2 (i / 64.0);
    }
};

float powF (float b, float e) noexcept
{
    static const PowTables t;
    if (e == 0.0f)
        return 1.0f;
    if (e == 1.0f)
        return b;
    const auto x = (double) b, y = (double) e;
    if (! (b > 0.0f) || std::isinf (b) || std::fabs (x - 1.0) < 0.0625)
    {
        const auto r = (float) std::pow (x, y);
        return std::fabs (r) < 1.17549435e-38f ? 0.0f : r;
    }
    std::uint64_t bits;
    std::memcpy (&bits, &x, 8);
    const auto mant = bits & 0xfffffffffffffull;
    const auto exponent = (int) (bits >> 52);
    const auto j = (unsigned) (mant >> 44) + (unsigned) ((mant >> 43) & 1u); // nearest 1/256
    const std::uint64_t fBits = (std::uint64_t) (0x3fe00u | j) << 44, mBits = mant | 0x3fe0000000000000ull;
    double f, m;
    std::memcpy (&f, &fBits, 8);
    std::memcpy (&m, &mBits, 8);
    const auto r = (f - m) * t.inv[j];
    const auto rq = r * std::fma (std::fma (0.3333333333333333, r, 0.5), r, 1.0);
    const auto lnx = ((double) (exponent - 1023) * 0.6931471805599453 + t.ln[j]) - rq;
    const auto tt = y * lnx;
    if (tt > 88.72283935546875)
        return INFINITY;
    if (tt <= -103.2789306640625)
        return 0.0f;
    const auto nn = std::nearbyint (tt * 92.33248261689366); // 64/ln 2
    const auto rr = std::fma (-nn, 0.010830424696249145, tt);
    const auto p = rr * std::fma (std::fma (0.16666666666666666, rr, 0.5), rr, 1.0);
    const auto n = (int) nn;
    const auto tab = t.exp2[(std::size_t) (n & 63)];
    auto res = std::fma (p, tab, tab);
    std::uint64_t rb;
    std::memcpy (&rb, &res, 8);
    rb += (std::uint64_t) (std::int64_t) (n >> 6) << 52;
    std::memcpy (&res, &rb, 8);
    const auto out = (float) res;
    return std::fabs (out) < 1.17549435e-38f ? 0.0f : out;
}

// 19 981^√u + 19 Hz: every crossover control
float crossover (float u) noexcept { return powF (19981.0f, std::sqrt (u)) + 19.0f; }

// --- 105-tap Kaiser window (β ≈ 5.1023), shared by every FIR -----------------------------------------------------

float besselI0 (float x) noexcept
{
    const auto half = x * 0.5f;
    auto sum = 0.0f;
    for (int k = 1; k < 16; ++k)
    {
        auto fact = 1.0f;
        for (int j = k; j > 0; --j)
            fact *= (float) j;
        sum = sum + powF (powF (half, (float) k) / fact, 2.0f);
    }
    return sum + 1.0f;
}

const std::array<float, taps>& kaiser()
{
    static const std::array<float, taps> w = [] {
        constexpr float beta = 5.102260112762451f;
        std::array<float, taps> t {};
        for (int j = 0; j <= centre; ++j)
        {
            const auto n = centre - j;
            const auto q = powF ((float) n * 2.0f / 104.0f, 2.0f);
            const auto s = std::sqrt (std::max (0.0f, 1.0f - q));
            t[(std::size_t) j] = besselI0 (s * beta) / besselI0 (beta);
            t[(std::size_t) (taps - 1 - j)] = t[(std::size_t) j];
        }
        return t;
    }();
    return w;
}

// --- FIR design: ideal pieces of the magnitude response summed tap by tap ------------------------------------------

struct Point
{
    float x, y; // Hz, gain
};

using Taps = std::array<float, taps>;

// adds an ideal low-pass of gain p.y up to p.x
void addLowPass (Taps& h, Point p, float rate) noexcept
{
    const auto inv = 1.0f / rate;
    const auto w = p.x * twoPi * inv;
    for (int k = 0; k < taps; ++k)
    {
        auto v = (p.y + p.y) * p.x * inv;
        if (k != centre)
        {
            const auto a = (float) (k - centre) * w;
            v = sineF (a) / a * v;
        }
        h[(std::size_t) k] = v + h[(std::size_t) k];
    }
}

// adds an ideal high-pass of gain p.y from p.x
void addHighPass (Taps& h, Point p, float rate) noexcept
{
    const auto inv = 1.0f / rate;
    const auto w = p.x * twoPi;
    for (int k = 0; k < taps; ++k)
    {
        const auto c = (p.x + p.x) * inv;
        float v;
        if (k == centre)
            v = 1.0f - c;
        else
        {
            const auto n = (float) (k - centre);
            const auto a = w * inv * n;
            const auto b = n * pi;
            v = sineF (b) / b - sineF (a) / a * c;
        }
        h[(std::size_t) k] = p.y * v + h[(std::size_t) k];
    }
}

// impulse response of a gain a + b·ω over 0…x (ω in rad/s)
void ramp (Taps& out, float a, float b, float x, float rate) noexcept
{
    const auto w = x * twoPi;
    const auto rt = rate * twoPi;
    const auto inv = 1.0f / rate;
    const auto a2 = (a + a) * w / rt;
    const auto wn = w * inv;
    for (int k = 0; k < taps; ++k)
    {
        float v;
        if (k == centre)
            v = w * b * w / rt + a2;
        else
        {
            const auto n = (float) (k - centre);
            const auto b2 = b + b;
            const auto arg = n * wn;
            v = sineF (arg) / arg * (b2 * w * w / rt + a2);
            const auto den = rt * inv * inv * n * n;
            v = v - (1.0f - cosineF (arg)) * (b2 / den);
        }
        out[(std::size_t) k] = v;
    }
}

// adds a linear transition between two points of the response
void addSegment (Taps& h, Point pa, Point pb, float rate) noexcept
{
    const auto lo = pb.x > pa.x ? pa : pb;
    const auto hi = pb.x > pa.x ? pb : pa;
    const auto l2 = lo.x * twoPi;
    const auto h2 = hi.x * twoPi;
    const auto dy = lo.y - hi.y;
    const auto d = l2 - h2;
    const auto slope = dy / d;
    const auto intercept = lo.y - dy * l2 / d;
    Taps r1 {}, r2 {};
    ramp (r1, intercept, slope, lo.x, rate);
    ramp (r2, intercept, slope, hi.x, rate);
    for (int k = 0; k < taps; ++k)
        h[(std::size_t) k] = (r2[(std::size_t) k] - r1[(std::size_t) k]) + h[(std::size_t) k];
}

void applyWindow (Taps& h) noexcept
{
    const auto& w = kaiser();
    for (int k = 0; k < taps; ++k)
        h[(std::size_t) k] = flushed (w[(std::size_t) k] * h[(std::size_t) k]);
}

// transition band ±17 % around the crossover, the upper edge kept below Nyquist
float upperEdge (float f, float rate) noexcept
{
    const auto half = rate * 0.5f;
    const auto e = f * 1.17f;
    return e > half ? half - 1.0f : e;
}

void designLowPass (Taps& h, float f, float rate) noexcept
{
    h.fill (0.0f);
    const Point a { f * 0.83f, 1.0f }, b { upperEdge (f, rate), 0.0f };
    addLowPass (h, a, rate);
    addSegment (h, a, b, rate);
    applyWindow (h);
}

void designHighPass (Taps& h, float f, float rate) noexcept
{
    h.fill (0.0f);
    const Point a { f * 0.83f, 0.0f }, b { upperEdge (f, rate), 1.0f };
    addSegment (h, a, b, rate);
    addHighPass (h, b, rate);
    applyWindow (h);
}

// band-pass between `lower` and `upper`; overlapping transitions meet at their crossing point, and a lower edge
// entirely above the upper one leaves the filter silent
void designBandPass (Taps& h, float upper, float lower, float rate) noexcept
{
    h.fill (0.0f);
    const auto b17 = upperEdge (lower, rate), b83 = lower * 0.83f;
    const auto a83 = upper * 0.83f, a17 = upperEdge (upper, rate);
    if (b83 > a17)
        return;
    const Point lowStart { b83, 0.0f }, lowEnd { b17, 1.0f }, highStart { a83, 1.0f }, highEnd { a17, 0.0f };
    if (a83 > b17)
    {
        addSegment (h, lowStart, lowEnd, rate);
        addSegment (h, lowEnd, highStart, rate);
        addSegment (h, highStart, highEnd, rate);
    }
    else
    {
        const auto d1 = b17 - b83, d2 = a83 - a17;
        const auto x = (b83 / d1 - a17 / d2) / (1.0f / d1 - 1.0f / d2);
        const Point cross { x, (x - b83) * (1.0f / d1) };
        addSegment (h, lowStart, cross, rate);
        addSegment (h, cross, highEnd, rate);
    }
    applyWindow (h);
}

struct Fir
{
    Taps h {};
    std::array<std::array<float, taps>, 2> s {};

    void reset() noexcept
    {
        for (auto& c : s)
            c.fill (0.0f);
    }

    bool active() const noexcept
    {
        for (const auto& c : s)
            for (auto v : c)
                if (v > silence || v < -silence)
                    return true;
        return false;
    }

    // transposed direct form, one channel
    void run (const float* in, float* out, int n, int ch) noexcept
    {
        auto& st = s[(std::size_t) ch];
        for (int i = 0; i < n; ++i)
        {
            const auto x = in[i];
            out[i] = x * h[0] + st[0];
            for (int k = 1; k < taps; ++k)
                st[(std::size_t) (k - 1)] = x * h[(std::size_t) k] + st[(std::size_t) k];
        }
    }
};

// --- 4th-order Butterworth low/high-pass: two biquads (Q 0.541, 1.307) -------------------------------------------

struct Iir
{
    bool highPass = false;
    float rate = 44100.0f, freq = 200.0f;
    std::array<float, 5> c1 {}, c2 {}; // b0 b1 b2 a1 a2 per section
    struct Ch
    {
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0; // section 1 (x, y), section 2 (u, v)
    };
    std::array<Ch, 2> ch {};

    void design() noexcept
    {
        const auto w = freq * twoPi / rate;
        const auto c = cosineF (w);
        const auto s = sineF (w);
        const auto alpha1 = s / 1.0820000171661377f, alpha2 = s / 2.614000082015991f;
        const auto a01 = alpha1 + 1.0f, a02 = alpha2 + 1.0f;
        const auto m2c = c * -2.0f;
        if (highPass)
        {
            const auto k = c + 1.0f;
            const auto half = k * 0.5f, neg = -k;
            c1 = { half / a01, neg / a01, half / a01, m2c / a01, (1.0f - alpha1) / a01 };
            c2 = { half / a02, neg / a02, half / a02, m2c / a02, (1.0f - alpha2) / a02 };
        }
        else
        {
            const auto k = 1.0f - c;
            const auto half = k * 0.5f;
            c1 = { half / a01, k / a01, half / a01, m2c / a01, (1.0f - alpha1) / a01 };
            c2 = { half / a02, k / a02, half / a02, m2c / a02, (1.0f - alpha2) / a02 };
        }
    }

    void reset() noexcept { ch = {}; }

    bool active() const noexcept
    {
        for (const auto& c : ch)
            for (auto v : { c.x1, c.x2, c.u1, c.u2, c.y1, c.y2, c.v1, c.v2 })
                if (v > silence || v < -silence)
                    return true;
        return false;
    }

    void run (const float* in, float* out, int n, int chan) noexcept
    {
        auto& s = ch[(std::size_t) chan];
        for (int i = 0; i < n; ++i)
        {
            const auto x = flushed (in[i]);
            auto y = s.x1 * c1[1] + x * c1[0];
            y = y + s.x2 * c1[2];
            y = y - s.y1 * c1[3];
            y = flushed (y - s.y2 * c1[4]);
            s.x2 = s.x1;
            s.y2 = s.y1;
            s.x1 = x;
            s.y1 = y;
            out[i] = y;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto x = flushed (out[i]);
            auto y = s.u1 * c2[1] + x * c2[0];
            y = y + s.u2 * c2[2];
            y = y - c2[3] * s.v1;
            y = flushed (y - s.v2 * c2[4]);
            s.u2 = s.u1;
            s.v2 = s.v1;
            s.u1 = x;
            s.v1 = y;
            out[i] = y;
        }
    }
};

// --- detector: peak hold with exponential release, one-pole smoothing, knee curve ----------------------------------

struct Detector
{
    float rate = 44100.0f;
    float threshold = 0.0f, ratio = 0.0f, release = 0.01f, knee = 0.005f, smoothing = 0.0f;
    float lo = 0, hi = 0, qa = 0, qb = 0, qc = 0; // knee
    float b = 0, a = 0;                           // smoothing one-pole
    // state
    float hold = 0, env = 0, lpIn = 0, lpOut = 0;
    std::uint32_t count = 0;

    void setKneeCurve() noexcept
    {
        lo = threshold * knee - 0.004999999888241291f;
        hi = (threshold + threshold) - lo;
        qa = 1.0f / ((lo - threshold) * 4.0f);
        qb = hi * -2.0f * qa;
        qc = qa * hi * hi + threshold;
    }

    void setSmoothing (float u) noexcept
    {
        smoothing = (float) (1.0 / ((double) (u * 0.1f) + 5.882352941176471e-05));
        const auto inv = 1.0f / rate;
        const auto t = std::tan ((double) (smoothing * twoPi * inv * 0.5f));
        const auto k = (float) (t * (double) (2.0f / inv)) * pi;
        const auto w = rate * twoPi;
        b = k / (k + w);
        a = (w - k) / (k + w);
    }

    void setThreshold (float u) noexcept { threshold = powF (1000.0f, u) * 0.0010000000474974513f; setKneeCurve(); }
    void setRatio (float u) noexcept { ratio = u; setKneeCurve(); }
    void setRelease (float u) noexcept { release = u * 0.9900000095367432f + 0.009999999776482582f; setKneeCurve(); }
    void setKnee (float u) noexcept { knee = u * u * 0.9950000047683716f + 0.004999999888241291f; setKneeCurve(); }

    void reset() noexcept
    {
        lpIn = lpOut = 0;
        count = 0;
        env = 0;
        hold = 0;
    }

    float smooth (float x) noexcept
    {
        x = flushed (x);
        const auto y = flushed ((x + lpIn) * b + a * lpOut);
        lpIn = x;
        lpOut = y;
        return y;
    }

    float curve (float x) const noexcept
    {
        if (lo > x)
            return 0.0f;
        if (hi >= x)
            return x - ((x * qa * x + x * qb) + qc);
        return x - threshold;
    }

    float gainFor (float level) const noexcept
    {
        const auto over = curve (level);
        return threshold / (over * ratio + threshold);
    }

    // band compressors: release from the held peak
    float peak (float x) noexcept
    {
        if (x > env)
        {
            env = hold = x;
            count = 0;
            return x;
        }
        const auto t = (float) count;
        if (rate * 10.0f >= t)
        {
            env = powF (eF, -(t / rate) / release) * hold;
            ++count;
        }
        return env;
    }

    // the output limiter: release time scaled by a factor that shrinks after loud attacks
    float limiterPeak (float x, float& factor) noexcept
    {
        if (x > env)
        {
            count = 0;
            env = hold = x;
            if (1.0f > factor)
                factor = 1.0f;
            else
            {
                factor = 1.0f - (x - 1.0f) * 0.4000000059604645f;
                if (0.009999999776482582f > factor)
                    factor = 0.009999999776482582f;
            }
            return x;
        }
        const auto t = (float) count;
        if (rate * 10.0f >= t)
        {
            env = powF (eF, -(t / rate) / (factor * release)) * hold;
            ++count;
        }
        return env;
    }
};

// --- a band: crossover filter(s), compressor, gain -----------------------------------------------------------------

enum class BandKind
{
    high,
    mid,
    low
};

struct Band
{
    BandKind kind = BandKind::high;
    bool iirMode = true, compress = true;
    float gain = 1.0f;
    Iir lowPass, highPass; // low: lowPass, high: highPass, mid: both (low-pass first)
    Fir fir;
    float upper = 0.0f, lower = 0.0f; // mid band FIR edges (Hz)
    Detector det;
    std::vector<float> l, r;

    void redesignFir (float rate) noexcept
    {
        if (kind == BandKind::low)
            designLowPass (fir.h, lowPass.freq, rate);
        else if (kind == BandKind::high)
            designHighPass (fir.h, highPass.freq, rate);
        else
            designBandPass (fir.h, upper, lower, rate);
    }

    void reset() noexcept
    {
        lowPass.reset();
        highPass.reset();
        fir.reset();
        det.reset();
    }

    bool active() const noexcept
    {
        if (kind == BandKind::low)
            return lowPass.active() || fir.active();
        if (kind == BandKind::high)
            return highPass.active() || fir.active();
        return lowPass.active() || highPass.active() || fir.active();
    }

    void run (const float* inL, const float* inR, int n) noexcept
    {
        float* out[2] { l.data(), r.data() };
        const float* in[2] { inL, inR };
        for (int c = 0; c < 2; ++c)
        {
            if (! iirMode)
                fir.run (in[c], out[c], n, c);
            else if (kind == BandKind::low)
                lowPass.run (in[c], out[c], n, c);
            else if (kind == BandKind::high)
                highPass.run (in[c], out[c], n, c);
            else
            {
                lowPass.run (in[c], out[c], n, c);
                highPass.run (out[c], out[c], n, c);
            }
        }
        if (compress)
            for (int i = 0; i < n; ++i)
            {
                const auto al = std::fabs (l[(std::size_t) i]), ar = std::fabs (r[(std::size_t) i]);
                const auto g = det.gainFor (det.smooth (det.peak (al > ar ? al : ar)));
                l[(std::size_t) i] = g * l[(std::size_t) i];
                r[(std::size_t) i] = g * r[(std::size_t) i];
            }
        else
            det.reset();
        for (int i = 0; i < n; ++i)
        {
            l[(std::size_t) i] = l[(std::size_t) i] * gain;
            r[(std::size_t) i] = r[(std::size_t) i] * gain;
        }
    }
};

// --- the whole engine -------------------------------------------------------------------------------------------

class Engine
{
public:
    static constexpr int maxBlock = 512;

    Engine()
    {
        bands[0].kind = BandKind::high;
        bands[0].highPass.highPass = true;
        bands[1].kind = BandKind::mid;
        bands[1].highPass.highPass = true;
        bands[2].kind = BandKind::low;
        for (auto& b : bands)
        {
            b.l.assign (maxBlock, 0.0f);
            b.r.assign (maxBlock, 0.0f);
        }
        // the output limiter's fixed detector settings
        for (auto& d : limiter)
        {
            d.setThreshold (0.9800000190734863f);
            d.setRatio (0.75f);
            d.setRelease (0.004999999888241291f);
            d.setKnee (0.8999999761581421f);
        }
    }

    // FL's set-up: the controls, then the sample rate (which re-applies every parameter and clears every state)
    void setup (const std::array<int, controls>& c, int sampleRate) noexcept
    {
        rate = (float) sampleRate;
        for (auto& b : bands)
        {
            b.lowPass.rate = b.highPass.rate = b.det.rate = rate;
        }
        for (auto& d : limiter)
        {
            d.rate = rate;
            d.setSmoothing (2.9999999242136255e-05f);
        }
        raw = c;
        for (int i = 0; i < controls; ++i)
            apply (i, false);
        for (auto& b : bands)
            b.redesignFir (rate);
        reset();
        configured = true;
    }

    void setControls (const std::array<int, controls>& c) noexcept
    {
        for (int i = 0; i < controls; ++i)
            if (c[(std::size_t) i] != raw[(std::size_t) i])
            {
                raw[(std::size_t) i] = c[(std::size_t) i];
                apply (i, true);
            }
    }

    void reset() noexcept
    {
        for (auto& b : bands)
            b.reset();
        for (auto& d : limiter)
            d.reset();
    }

    bool isConfigured() const noexcept { return configured; }
    bool linearPhase() const noexcept { return ! bands[0].iirMode; }

    // one FL block (≤ maxBlock), separate channels, in place
    void process (float* left, float* right, int n) noexcept
    {
        bool signal = false;
        for (int i = 0; i < n && ! signal; ++i)
            signal = left[i] > silence || left[i] < -silence || right[i] > silence || right[i] < -silence;
        if (! signal)
            for (const auto& b : bands)
                signal = signal || b.active();
        if (! signal)
        {
            reset(); // the plugin clears its states and passes the block through untouched
            return;
        }
        for (auto& b : bands)
            b.run (left, right, n);
        const auto& hb = bands[0];
        const auto& mb = bands[1];
        const auto& lb = bands[2];
        for (int i = 0; i < n; ++i)
        {
            const auto s = (std::size_t) i;
            left[i] = (mb.l[s] * enable[1] + lb.l[s] * enable[2] + hb.l[s] * enable[0]) * master;
            right[i] = (lb.r[s] * enable[2] + mb.r[s] * enable[1] + hb.r[s] * enable[0]) * master;
        }
        if (limiterOn)
            for (int i = 0; i < n; ++i)
            {
                const auto gl = limiterGain (limiter[0], left[i]);
                const auto yl = gl * left[i];
                const auto gr = limiterGain (limiter[1], right[i]);
                const auto yr = gr * right[i];
                left[i] = softClip (yl) * 0.9999899864196777f;
                right[i] = softClip (yr) * 0.9999899864196777f;
            }
    }

private:
    float u (int i) const noexcept
    {
        const auto mn = controlMin[(std::size_t) i], mx = controlMax[(std::size_t) i];
        return (float) ((double) (raw[(std::size_t) i] - mn) / (double) (mx - mn));
    }

    float limiterGain (Detector& d, float x) noexcept
    {
        return d.gainFor (d.smooth (d.limiterPeak (std::fabs (x), limiterFactor)));
    }

    static float softClip (float y) noexcept
    {
        constexpr float knee = 0.9980000257492065f, width = 0.0029999613761901855f, top = 0.001999974250793457f;
        if (knee > std::fabs (y))
            return y;
        const auto t = ((y > 0.0f ? y : -y) - knee) / width;
        float s;
        if (1.0f > std::fabs (t))
            s = (1.5f - t * 0.5f * t) * t;
        else
            s = t > 0.0f ? 1.0f : -1.0f;
        const auto v = s * top + knee;
        return y > 0.0f ? v : -v;
    }

    // one control as the plugin forwards it (u = raw over the control's range); `live` = a change while running
    void apply (int i, bool live) noexcept
    {
        const auto v = u (i);
        auto& hb = bands[0];
        auto& mb = bands[1];
        auto& lb = bands[2];
        const auto mode = [&] (Band& b, int e) {
            b.compress = ! ((double) v >= 0.33);
            enable[(std::size_t) e] = (v >= 0.33000001311302185f && v <= 0.6600000262260437f) ? 0.0f : 1.0f;
        };
        switch (i)
        {
            case 0: master = powF (4.0f, v) - 1.0f; break;
            case 1: limiterOn = v >= 0.5f; break;
            case 2:
            {
                const auto iir = 1.0f - v >= 0.5f;
                reset(); // the plugin clears every state when the mode is set
                for (auto& b : bands)
                    b.iirMode = iir;
                break;
            }
            case 3: mode (hb, 0); break;
            case 4:
                hb.highPass.freq = crossover (v);
                hb.highPass.design();
                if (live)
                    hb.redesignFir (rate);
                break;
            case 5: hb.gain = powF (8.0f, v) - 1.0f; break;
            case 6: hb.det.setThreshold (v); break;
            case 7: hb.det.setRatio (v); break;
            case 8: hb.det.setSmoothing (v); break;
            case 9: hb.det.setRelease (v); break;
            case 10: hb.det.setKnee (v); break;
            case 11: mode (mb, 1); break;
            case 12:
                mb.lowPass.freq = mb.upper = crossover (v);
                mb.lowPass.design();
                if (live)
                    mb.redesignFir (rate);
                break;
            case 13:
                mb.highPass.freq = mb.lower = crossover (v);
                mb.highPass.design();
                if (live)
                    mb.redesignFir (rate);
                break;
            case 14: mb.gain = powF (8.0f, v) - 1.0f; break;
            case 15: mb.det.setThreshold (v); break;
            case 16: mb.det.setRatio (v); break;
            case 17: mb.det.setSmoothing (v); break;
            case 18: mb.det.setRelease (v); break;
            case 19: mb.det.setKnee (v); break;
            case 20: mode (lb, 2); break;
            case 21:
                lb.lowPass.freq = crossover (v);
                lb.lowPass.design();
                if (live)
                    lb.redesignFir (rate);
                break;
            case 22: lb.gain = powF (8.0f, v) - 1.0f; break;
            case 23: lb.det.setThreshold (v); break;
            case 24: lb.det.setRatio (v); break;
            case 25: lb.det.setSmoothing (v); break;
            case 26: lb.det.setRelease (v); break;
            case 27: lb.det.setKnee (v); break;
            default: break;
        }
    }

    std::array<Band, 3> bands {};   // high, mid, low
    std::array<Detector, 2> limiter {};
    float limiterFactor = 1.0f;     // FL keeps this one value for the whole process (all instances); LYNX per instance
    std::array<float, 3> enable { 1.0f, 1.0f, 1.0f };
    float master = 1.0f, rate = 44100.0f;
    bool limiterOn = false, configured = false;
    std::array<int, controls> raw = controlDefault;
};

class MultibandEffect final : public Effect
{
public:
    // `initial` (may be null): the slot's parameters, so the engine is set up in prepare() and its linear-phase
    // latency is known before the first block (issue #113)
    MultibandEffect (std::optional<State> fl, const EffectParams* initial) : imported (std::move (fl))
    {
        if (initial != nullptr)
            params0 = *initial;
        hasParams0 = initial != nullptr;
    }

    void prepare (double r) noexcept override
    {
        sampleRate = std::max (1, (int) std::lround (r));
        prepared = true;
        configured = false;
        if (hasParams0)
        {
            std::array<int, controls> c {};
            for (int i = 0; i < controls; ++i)
                c[(std::size_t) i] = control (i, params0[(std::size_t) i]);
            engine.setup (c, sampleRate);
            configured = true;
        }
    }

    void reset() noexcept override { engine.reset(); }

    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (! prepared)
            return;
        std::array<int, controls> c {};
        for (int i = 0; i < controls; ++i)
            c[(std::size_t) i] = control (i, p[(std::size_t) i]);
        if (! configured)
        {
            engine.setup (c, sampleRate);
            configured = true;
        }
        else
            engine.setControls (c);
        for (int off = 0; off < n; off += Engine::maxBlock)
            engine.process (left + off, right + off, std::min (Engine::maxBlock, n - off));
    }

    int latencySamples() const noexcept override { return configured && engine.linearPhase() ? linearPhaseLatency : 0; }

private:
    // a parameter still at its imported value (within the project file's 4-decimal rounding) keeps FL's exact control
    int control (int i, float v) const noexcept
    {
        const auto mn = controlMin[(std::size_t) i], mx = controlMax[(std::size_t) i];
        if (imported)
        {
            const auto fl = imported->c[(std::size_t) i];
            if (std::fabs (normalised (fl, mn, mx) - v) <= 0.00006f)
                return fl;
        }
        return raw (v, mn, mx);
    }

    Engine engine;
    std::optional<State> imported;
    EffectParams params0 {};
    int sampleRate = 44100;
    bool hasParams0 = false, prepared = false, configured = false;
};

std::int32_t i32 (const std::vector<std::uint8_t>& d, std::size_t at) noexcept
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + at, 4);
    return v;
}
} // namespace

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    // the loader reads the 30 words only when the version is ≤ 0 (the only version FL writes)
    if (d.size() < 4 + controls * 4)
        return std::nullopt;
    State s;
    s.version = i32 (d, 0);
    if (s.version > 0)
        return std::nullopt;
    for (int i = 0; i < controls; ++i)
        s.c[(std::size_t) i] = std::clamp (i32 (d, 4 + 4 * (std::size_t) i), controlMin[(std::size_t) i], controlMax[(std::size_t) i]);
    return s;
}

float normalised (int r, int min, int max) noexcept
{
    return max > min ? (float) (std::clamp (r, min, max) - min) / (float) (max - min) : 0.0f;
}

int raw (float v, int min, int max) noexcept
{
    return min + (int) std::lround (std::clamp ((double) v, 0.0, 1.0) * (double) (max - min));
}

void toParams (const State& s, float* p) noexcept
{
    for (int i = 0; i < controls; ++i)
        p[i] = normalised (s.c[(std::size_t) i], controlMin[(std::size_t) i], controlMax[(std::size_t) i]);
}

const char* paramName (int i) noexcept
{
    static const char* names[params] { "Output gain", "Limiter", "Linear phase",
                                       "High mode", "High crossover", "High gain", "High threshold", "High ratio",
                                       "High attack", "High release", "High knee",
                                       "Mid mode", "Mid upper crossover", "Mid lower crossover", "Mid gain",
                                       "Mid threshold", "Mid ratio", "Mid attack", "Mid release", "Mid knee",
                                       "Low mode", "Low crossover", "Low gain", "Low threshold", "Low ratio",
                                       "Low attack", "Low release", "Low knee" };
    return i >= 0 && i < params ? names[i] : "";
}

std::string paramText (int i, float value)
{
    if (i < 0 || i >= params)
        return {};
    char t[48];
    const auto r = raw (value, controlMin[(std::size_t) i], controlMax[(std::size_t) i]);
    const auto v = (double) (r - controlMin[(std::size_t) i]) / (double) (controlMax[(std::size_t) i] - controlMin[(std::size_t) i]);
    const auto db = [&] (double g) {
        if (g <= 0.0)
            std::snprintf (t, sizeof (t), "-inf dB");
        else
            std::snprintf (t, sizeof (t), "%+.1f dB", 20.0 * std::log10 (g));
    };
    switch (i)
    {
        case 0: db (std::pow (4.0, v) - 1.0); break;
        case 1: case 2: return r != 0 ? "on" : "off";
        case 3: case 11: case 20: return r == 0 ? "compress" : r == 1 ? "off" : "bypass";
        case 4: case 12: case 13: case 21: std::snprintf (t, sizeof (t), "%.0f Hz", std::pow (19981.0, std::sqrt (v)) + 19.0); break;
        case 5: case 14: case 22: db (std::pow (8.0, v) - 1.0); break;
        case 6: case 15: case 23: std::snprintf (t, sizeof (t), "%.1f dB", 60.0 * v - 60.0); break;
        case 7: case 16: case 24: std::snprintf (t, sizeof (t), "%.0f%%", 100.0 * v); break;
        case 8: case 17: case 25: std::snprintf (t, sizeof (t), "%.1f ms", 1000.0 * (v * 0.1 + 5.882352941176471e-05)); break;
        case 9: case 18: case 26: std::snprintf (t, sizeof (t), "%.0f ms", 1000.0 * (v * 0.99 + 0.01)); break;
        case 10: case 19: case 27: std::snprintf (t, sizeof (t), "%.0f%%", 100.0 * v); break;
        default: return {};
    }
    return t;
}

std::unique_ptr<Effect> make (const EffectParams* params) { return std::make_unique<MultibandEffect> (std::nullopt, params); }
std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& flState, const EffectParams* params)
{
    return std::make_unique<MultibandEffect> (parse (flState), params);
}
} // namespace lynx::flmb
