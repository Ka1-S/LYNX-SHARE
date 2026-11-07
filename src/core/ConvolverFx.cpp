// FL Studio's Fruity Convolver (REA_rodada60, clean-room): state, impulse-response preparation and the engine.
// Our own code, written from the behaviour documented in docs/analise/REA_rodada60.md.

#include "core/ConvolverFx.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

#include "core/WavPack.h"

namespace lynx
{
namespace convolver
{
const std::array<Range, convolverParamCount> ranges { { { 0, 1000 }, { 0, 1000 }, { -1000, 1000 }, { -1000, 1000 } } };

namespace
{
using cd = std::complex<double>;

constexpr float silence = 5.96046448e-08f;  // 2^-24: the kernel's trimming threshold
constexpr float quietInput = 1.00000001e-06f; // an input block at or below this level is "silent"
constexpr double ln2 = 0.69314718055994529;
constexpr double pi = 3.14159265358979323846;

inline float f32 (double x) noexcept { return (float) x; }
inline int roundEven (double x) noexcept { return (int) std::nearbyint (x); }

std::int32_t i32 (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}
std::uint32_t u32 (const std::vector<std::uint8_t>& d, std::size_t o) noexcept { return (std::uint32_t) i32 (d, o); }
double f64 (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    double v;
    std::memcpy (&v, d.data() + o, 8);
    return v;
}
float f32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    float v;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

// (a + bi)(c + di) for finite values, without std::complex's NaN/infinity recovery branch (a hot loop)
inline cd mul (const cd& z, const cd& w) noexcept
{
    const double a = z.real(), b = z.imag(), c = w.real(), d = w.imag();
    return { a * c - b * d, a * d + b * c };
}

// ---------------------------------------------------------------------------------------------- radix-2 FFT
class Fft
{
public:
    explicit Fft (int size) : n (size), tw ((std::size_t) size / 2), rev ((std::size_t) size)
    {
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * pi * (double) k / (double) n;
            tw[(std::size_t) k] = cd (std::cos (a), std::sin (a));
        }
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 1; i < n; ++i)
            rev[(std::size_t) i] = (rev[(std::size_t) (i >> 1)] >> 1) | ((i & 1) << (bits - 1));
    }
    int size() const noexcept { return n; }
    // in place; the inverse is unscaled
    void run (cd* a, bool inverse) const noexcept
    {
        bitReverse (a);
        for (int len = 2; len <= n; len <<= 1)
            pass (a, len, inverse);
    }
    // the transform in pieces (a large one spread over an audio block): the reordering, then log2(n) passes
    int passes() const noexcept
    {
        int p = 0;
        for (int len = 2; len <= n; len <<= 1)
            ++p;
        return p;
    }
    void bitReverse (cd* a) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(std::size_t) i])
                std::swap (a[i], a[rev[(std::size_t) i]]);
    }
    void pass (cd* a, int len, bool inverse) const noexcept
    {
        const int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; ++j)
            {
                cd w = tw[(std::size_t) (j * step)];
                if (inverse)
                    w = std::conj (w);
                const cd u = a[i + j], v = mul (a[i + j + half], w);
                a[i + j] = u + v;
                a[i + j + half] = u - v;
            }
    }

private:
    int n;
    std::vector<cd> tw;
    std::vector<int> rev;
};

int pow2AtLeast (std::size_t v) noexcept
{
    int p = 1;
    while ((std::size_t) p < v)
        p <<= 1;
    return p;
}

// ------------------------------------------------------------------------------------- kernel preparation
// FL's normalisation (the plugin's level analysis): the loudest 300 ms window of L² + R² gives the RMS `pk`;
// the gain brings it to 0.05
float normaliseGain (const std::vector<float>& l, const std::vector<float>& r, double rate)
{
    const int win = roundEven (300.0 * rate * 0.001);
    if (win <= 0 || l.empty())
        return 0.0f;
    std::vector<double> c (l.size() + 1, 0.0);
    for (std::size_t i = 0; i < l.size(); ++i)
        c[i + 1] = c[i] + (double) l[i] * (double) l[i] + (double) r[i] * (double) r[i];
    double best = 0.0;
    for (std::size_t j = 0; j < l.size(); ++j)
    {
        const std::size_t lo = j + 1 >= (std::size_t) win ? j + 1 - (std::size_t) win : 0;
        best = std::max (best, c[j + 1] - c[lo]);
    }
    const auto pk = f32 (std::sqrt (best / ((double) win * 2.0)));
    if (! (pk > 0.0f))
        return 0.0f;
    return f32 (0.05 / (double) pk);
}

void scale (std::vector<float>& x, float g) noexcept
{
    for (auto& v : x)
        v = v * g;
}

// "Blend" knob: x·(1 − k/2) + (k/2)·g·(x ∗ x), the self-convolution cut to the sample's length, g = 1/√(max_c Σx_c²)
void selfConvolve (std::vector<float>& l, std::vector<float>& r, float k)
{
    const std::size_t n = l.size();
    if (n == 0)
        return;
    double el = 0.0, er = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        el += (double) l[i] * (double) l[i], er += (double) r[i] * (double) r[i];
    const double e = std::max (el, er);
    const float g = e == 0.0 ? 1.0f : f32 (std::sqrt (1.0) / std::sqrt (e));
    const float b = k * 0.5f, a = 1.0f - k * 0.5f, bb = b * g;
    const int size = pow2AtLeast (2 * n);
    Fft fft (size);
    std::vector<cd> z ((std::size_t) size);
    // both channels in one complex transform: Z = L + iR, then each spectrum squared on its own
    for (std::size_t i = 0; i < n; ++i)
        z[i] = cd (l[i], r[i]);
    fft.run (z.data(), false);
    std::vector<cd> y ((std::size_t) size);
    for (int k2 = 0; k2 < size; ++k2)
    {
        const cd zk = z[(std::size_t) k2], zc = std::conj (z[(std::size_t) ((size - k2) % size)]);
        const cd xl = (zk + zc) * 0.5, xr = (zk - zc) * cd (0.0, -0.5);
        y[(std::size_t) k2] = xl * xl + cd (0.0, 1.0) * (xr * xr);
    }
    fft.run (y.data(), true);
    const double inv = 1.0 / (double) size;
    for (std::size_t i = 0; i < n; ++i)
    {
        l[i] = l[i] * a + f32 (y[i].real() * inv) * bb;
        r[i] = r[i] * a + f32 (y[i].imag() * inv) * bb;
    }
}

// FL's sample-player sinc resampler (quality "256 points"): a Blackman-like window over ±256 taps, the cutoff lowered
// to the new rate when shrinking; frames past either end repeat the first / last one
void resample (std::vector<float>& l, std::vector<float>& r, double ratio)
{
    constexpr int N = 256;
    const long n = (long) l.size();
    if (n == 0)
        return;
    const double speed = 1.0 / ratio;
    const long nout = (long) std::nearbyint ((double) n * ratio);
    const double w = speed <= 1.0 ? pi : pi / speed;
    std::vector<float> ol ((std::size_t) std::max (nout, 0L)), orr ((std::size_t) std::max (nout, 0L));
    // read positions: blocks of 1024; far from both ends pos + k·speed, else accumulated step by step
    std::vector<double> pos ((std::size_t) std::max (nout, 0L));
    {
        double p = 0.0;
        long j = 0;
        while (j < nout)
        {
            const long cnt = std::min<long> (1024, nout - j);
            const long rp = (long) std::nearbyint (p);
            if (rp < N - 1 + 3 || (double) (n - N - 3) <= p + speed * (double) cnt)
                for (long k = 0; k < cnt; ++k)
                    pos[(std::size_t) (j + k)] = p, p = p + speed;
            else
            {
                for (long k = 0; k < cnt; ++k)
                    pos[(std::size_t) (j + k)] = p + (double) k * speed;
                p = p + speed * (double) cnt;
            }
            j += cnt;
        }
    }
    const double cw = std::cos (w), sw = std::sin (w), cn = std::cos (pi / N), sn = std::sin (pi / N);
    for (long j = 0; j < nout; ++j)
    {
        const double p = pos[(std::size_t) j];
        const long i = (long) std::floor (p);
        const double f = p - (double) i + 1e-25;
        // taps m = −N+1 … N, d = f − m: the sines and cosines of w·d and π·d/N stepped by rotations
        const double d0 = f + (double) (N - 1);
        double s1 = std::sin (w * d0), c1 = std::cos (w * d0);
        double s2 = std::sin (pi * d0 / N), c2 = std::cos (pi * d0 / N);
        double accL = 0.0, accR = 0.0;
        for (int m = -N + 1; m <= N; ++m)
        {
            const long idx = i + m;
            {
                const long q = idx < 0 ? 0 : (idx < n ? idx : n - 1);
                const double d = f - (double) m;
                double sv = s1, cv = c2;
                if (std::abs (d) < 0.5) // next to the read position: exact values (d may be ~1e-25)
                    sv = std::sin (w * d), cv = std::cos (pi * d / N);
                const double h = sv / (pi * d) * (0.36 + cv + 0.64 * cv * cv) * 0.5;
                accL += h * (double) l[(std::size_t) q];
                accR += h * (double) r[(std::size_t) q];
            }
            // angle − w and − π/N
            const double ns1 = s1 * cw - c1 * sw, nc1 = c1 * cw + s1 * sw;
            const double ns2 = s2 * cn - c2 * sn, nc2 = c2 * cn + s2 * sn;
            s1 = ns1, c1 = nc1, s2 = ns2, c2 = nc2;
        }
        ol[(std::size_t) j] = f32 (accL);
        orr[(std::size_t) j] = f32 (accR);
    }
    l.swap (ol);
    r.swap (orr);
}

bool flat (const std::vector<EnvPoint>& e, double& level) noexcept
{
    if (e.empty())
    {
        level = 0.5;
        return true;
    }
    level = e.front().y;
    for (const auto& p : e)
        if (p.y != level)
            return false;
    return true;
}

// ------------------------------------------------------------------------------------------------ EQ curve
// The EQ curve is an FL map over 0…1 (the frequency axis) drawn with FL's shared envelope-curve law (the segment
// modes of REA round 66 §3.2, here evaluated in double precision as this plugin's map iterator does). Steps of
// REA_rodada60 §7 C.7: the curve is sampled on M + 1 frequency bins, each level becomes a gain, the response is smoothed
// along the frequency axis, turned into a centred 2M-tap FIR and the impulse response is filtered by it; the EQ amount
// mixes the filtered and the original impulse response.
class EqCurve
{
public:
    explicit EqCurve (const std::vector<EnvPoint>& src)
    {
        for (const auto& s : src)
            pts.push_back ({ s.dt, s.y, s.tension, s.mode });
        normalise();
        prepare();
        begin();
    }

    // advances the iterator by `adv` (x units) and returns the level there
    double eval (double adv) noexcept
    {
        const int n = (int) pts.size();
        if (! (idx >= 0 && idx <= n - 1))
            return out;
        phase += adv * rate;
        while (phaseWrap (phase))
        {
            const int prev = idx;
            phase = rate == 0.0 ? adv : (phase - 1.0) / rate;
            start = pts[(std::size_t) idx].y;
            if (idx < n - 1)
                ++idx;
            delta = pts[(std::size_t) idx].y - start;
            if (prev == idx)
                phase = rate = 0.0;
            else
            {
                const double dt = pts[(std::size_t) idx].dt;
                if (dt <= 0.0)
                    rate = 1.0, phase += 1.0;
                else
                    rate = 1.0 / dt, phase *= rate;
            }
        }
        out = start + shape (idx, phase) * delta;
        return out;
    }

    // the sign byte FL stores with each point (0 for the first point, 2 = straight segment, ±1 = tension's sign)
    static std::uint8_t signByte (const EnvPoint& p, bool first) noexcept
    {
        if (first)
            return 0;
        if (p.tension == 0.0f)
            return p.mode < 16 && ((0x1b83u >> p.mode) & 1u) != 0 ? 2 : 0;
        return p.tension > 0.0f ? 1 : 0xff;
    }

private:
    struct P
    {
        double dt = 0, y = 0;
        float t = 0;
        std::uint8_t mode = 0;
        int sign = 0;
        float k = 0, ilk = 0, L = 0, p4 = 0, p5 = 0, p6 = 0;
    };
    std::vector<P> pts;
    int idx = 0;
    double phase = 0, rate = 0, start = 0, delta = 0, out = 0;

    static bool phaseWrap (double x) noexcept // "x ≥ 1" tested on the high word (a negative phase also passes)
    {
        std::uint64_t b;
        std::memcpy (&b, &x, 8);
        return (std::uint32_t) (b >> 32) > 0x3fefffffu;
    }
    static bool belowHalf (double x) noexcept
    {
        std::uint64_t b;
        std::memcpy (&b, &x, 8);
        return (std::uint32_t) (b >> 32) < 0x3fe00000u;
    }
    static long long rnd (double x) noexcept { return (long long) std::nearbyint (x); }

    // a map's x axis has length 1: a point at 0 is added when the first one starts later, the Δx are scaled to a sum of 1
    void normalise()
    {
        if (pts.empty())
        {
            pts.push_back ({ 0.0, 0.5 });
            pts.push_back ({ 1.0, 0.5 });
            return;
        }
        if (pts[0].dt > 0.0)
            pts.insert (pts.begin(), P { 0.0, pts[0].y });
        double total = 0.0;
        for (const auto& p : pts)
            total += std::max (p.dt, 0.0);
        if (total > 0.0)
        {
            const double f = 1.0 / total;
            if (f != 1.0)
                for (auto& p : pts)
                    p.dt = p.dt * f;
        }
        else
            pts.push_back ({ 1.0, pts.back().y });
    }

    static int stepCount (const P& p) noexcept
    {
        if (p.mode > 7 || ((0x78u >> p.mode) & 1u) == 0)
            return -1;
        const double a = (double) f32 (1.0 - std::abs ((double) p.t) * 0.6);
        auto n = (int) rnd ((double) f32 (std::exp (a * std::log (1000001.0)) - 1.0) * 0.001);
        if (p.mode == 3 && p.t >= 0.0f)
            ++n;
        return n;
    }

    void prepareSmooth (int i) noexcept
    {
        constexpr double eps = 5.960464477539063e-08;
        auto& p = pts[(std::size_t) i];
        const int n = (int) pts.size();
        const double h = p.dt > eps ? p.dt : eps;
        const double y1 = p.y;
        const int j = i - 1;
        const double y0 = j >= 0 ? pts[(std::size_t) j].y : y1;
        double rp = 1.0, ypp = y0;
        if (j >= 0 && pts[(std::size_t) j].mode == 10)
        {
            rp = pts[(std::size_t) j].dt / h;
            if (i - 2 >= 0)
                ypp = pts[(std::size_t) (i - 2)].y;
        }
        double rn = 1.0, yn = y1;
        if (i + 1 < n && pts[(std::size_t) (i + 1)].mode == 10)
        {
            rn = pts[(std::size_t) (i + 1)].dt / h;
            yn = pts[(std::size_t) (i + 1)].y;
        }
        const double d = y1 - y0;
        if (std::abs (d) < eps)
        {
            p.sign = 2;
            return;
        }
        const double a = (ypp - y0) / d, b = (yn - y0) / d;
        rp = rp > eps ? rp : eps;
        rn = rn > eps ? rn : eps;
        const double sr = (b - 1.0) / rn, sl = -a / rp;
        if (! (f32 (sr) > 0.0f))
            p.p4 = 0.0f;
        else
        {
            const float w1 = f32 (rn) * 2.0f + 1.0f, w2 = f32 (rn) + 2.0f;
            p.p4 = (f32 (sr) * (w1 + w2)) / (w1 * f32 (sr) + w2);
        }
        if (! (f32 (sl) > 0.0f))
            p.p5 = 0.0f;
        else
        {
            const float w1 = 2.0f + f32 (rp), w2 = 1.0f + f32 (rp) * 2.0f;
            p.p5 = (f32 (sl) * (w1 + w2)) / (w1 + w2 * f32 (sl));
        }
        p.p6 = f32 (1.0 - std::abs ((double) p.t));
    }

    void prepare()
    {
        const int n = (int) pts.size();
        for (int i = 0; i < n; ++i)
        {
            auto& p = pts[(std::size_t) i];
            if (p.dt <= 0.0)
                p.dt = 0.0;
            if (i == 0)
                continue;
            const float t = p.t;
            p.sign = t == 0.0f ? 0 : (std::signbit (t) ? -1 : 1);
            const int cnt = stepCount (p);
            const int m = p.mode;
            if (m == 6)
            {
                p.p4 = f32 ((double) cnt + 0.5);
                p.p5 = 6.2831855f;
                if (cnt == 0 && p.sign > 0)
                    p.sign = 2;
            }
            else if (m < 2)
            {
                const auto e1 = f32 (std::exp ((double) std::abs (t) * std::log (1000001.0)) - 1.0);
                p.k = e1 * (t > 0.0f ? 1.0f : (t < 0.0f ? -1.0f : 0.0f));
                if (p.k != 0.0f)
                {
                    p.ilk = f32 (1.0 / std::abs ((double) p.k));
                    p.L = f32 (std::log (std::abs ((double) p.k) + 1.0));
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
                p.p4 = std::abs (t);
            else if (m == 10)
                prepareSmooth (i);
            else if (m == 11 || m == 12)
            {
                auto dd = std::abs ((double) t) * 0.9;
                if (dd <= 1.52587890625e-05)
                    dd = 1.52587890625e-05;
                const auto uu = 1.0 - dd;
                p.p4 = f32 (1.0 - uu * uu * uu);
                p.p5 = (1.0f - p.p4) / p.p4;
                p.p6 = p.p5;
                if (p.sign > 0)
                {
                    p.p5 = -p.p5;
                    p.p6 = p.p5 - 1.0f;
                }
                if (m == 12)
                {
                    p.p5 = f32 ((double) p.p5 * 0.5);
                    p.p6 = f32 ((double) p.p6 * 0.5);
                }
            }
            if (m < 16 && ((0x1b83u >> m) & 1u) != 0 && t == 0.0f)
                p.sign = 2;
        }
    }

    void begin() noexcept
    {
        idx = 0;
        if (pts[0].dt == 0.0)
            rate = 1.0, phase = 1.0;
        else
            rate = 1.0 / pts[0].dt, phase = 0.0;
        start = out = pts[0].y;
        delta = 0.0;
    }

    static double curveK (const P& p, double x) noexcept
    {
        if (p.k == 0.0f)
            return x;
        if (p.k > 0.0f)
            return 1.0 - (std::exp ((1.0 - x) * (double) p.L) - 1.0) * (double) p.ilk;
        return (std::exp (x * (double) p.L) - 1.0) * (double) p.ilk;
    }

    // fraction (0…1) of the segment ending at point i at phase ph
    double shape (int i, double ph) const noexcept
    {
        const auto& p = pts[(std::size_t) i];
        if (p.sign == 2)
            return ph;
        const double p4 = p.p4, p5 = p.p5, p6 = p.p6;
        switch (p.mode)
        {
            case 0: return curveK (p, ph);
            case 1:
                if (belowHalf (ph))
                    return curveK (p, ph * 2.0) * 0.5;
                return 1.0 - curveK (p, (1.0 - ph) * 2.0) * 0.5;
            case 3: return (double) rnd (ph * p4 - p6) * p5;
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
                return ((double) i2 + dd * 0.5 + 1.0) * p5;
            }
            case 5: return (double) (rnd (ph * p4 - p6) & 1);
            case 6:
            {
                if (p.sign < 0)
                    return (std::sin ((ph * p4 + 0.75) * p5) + 1.0) * 0.5;
                const auto v = ph * p4 - 0.5;
                const auto i2 = (int) rnd (v - 0.5);
                return std::abs ((v - (double) i2) * 2.0 - 1.0);
            }
            case 7:
            {
                const auto dd = p.sign > 0 ? 1.0 - ph : ph;
                const auto d3 = dd * dd * dd;
                return ph - (d3 * d3 - dd) * p4;
            }
            case 8:
            {
                if (belowHalf (ph))
                {
                    const auto x = ph * 2.0;
                    const auto dd = p.sign > 0 ? 1.0 - x : x;
                    const auto d3 = dd * dd * dd;
                    return (x - (d3 * d3 - dd) * p4) * 0.5;
                }
                const auto x = (1.0 - ph) * 2.0;
                const auto dd = p.sign > 0 ? 1.0 - x : x;
                const auto d3 = dd * dd * dd;
                return 1.0 - (x - (d3 * d3 - dd) * p4) * 0.5;
            }
            case 9:
            {
                const auto hp = (double) 1.5707963705062866;
                const auto dd = p.sign > 0 ? std::sin (ph * hp) : 1.0 - std::cos (ph * hp);
                return ph - (ph - dd) * p4;
            }
            case 10:
            {
                const auto x2 = ph * ph, x1 = ph - 1.0;
                return ph + ((((x2 * 3.0 - x2 * 2.0 * ph) + x2 * x1 * p4) + ph * x1 * x1 * p5) - ph) * p6;
            }
            case 11:
                if (p.sign < 1)
                    return p5 / (1.0 - p4 * ph) - p6;
                return p5 / (1.0 - p4 * (1.0 - ph)) - p6;
            case 12:
                if (belowHalf (ph))
                {
                    if (p.sign < 1)
                        return p5 / (1.0 - p4 * ph * 2.0) - p6;
                    return p5 / (1.0 - p4 * (1.0 - ph * 2.0)) - p6;
                }
                else
                {
                    const auto x = (1.0 - ph) * 2.0;
                    if (p.sign < 1)
                        return (1.0 - p5 / (1.0 - p4 * x)) + p6;
                    return (1.0 - p5 / (1.0 - p4 * (1.0 - x))) + p6;
                }
            default: return 0.0; // 2 (hold) and unknown modes
        }
    }
};

// The editor's frequency axis (the plugin's display mapping, a log-like scale from 130.8 Hz = 440·2^(−21/12) to
// 22 050 Hz with a linear stretch below): Hz → x in 0…1 and back
struct FreqAxis
{
    double fmin, fmax, p1, p4, p5, p6, p7;
    FreqAxis()
    {
        fmin = std::exp (0.057762265046662105 * -21.0) * 440.0;
        fmax = 22050.0;
        const double c = fmax / fmin;
        p1 = std::log (2.0) / std::log (fmax * 2.0 / fmin);
        const double p3 = std::log (c + 1.0);
        p4 = 1.0 / p3;
        p5 = 1.0 / (1.0 - p1) * p3;
        p6 = c / (fmax - fmin);
        p7 = 1.0 / c;
    }
    double toX (double hz) const noexcept
    {
        if (fmin <= hz)
            return p1 + std::log ((hz - fmin) * p6 + 1.0) * p4 * (1.0 - p1);
        return hz * p1 / fmin;
    }
    double toHz (double x) const noexcept
    {
        if (p1 <= x)
            return (std::exp ((x - p1) * p5) - 1.0) * (fmax - fmin) * p7 + fmin;
        return x * fmin / p1;
    }
};
const FreqAxis& freqAxis()
{
    static const FreqAxis a;
    return a;
}

// a curve level as a gain: (3^(2v) − 1)/2 — 0 = silence, 0.5 = unity, 1 = ×4 (+12 dB)
float levelGain (double v) noexcept
{
    const float x = f32 (v * 2.0);
    if (x == 1.0f)
        return 1.0f;
    return f32 ((std::exp ((double) (x * 1.09861231f)) - 1.0) * (double) 0.5f);
}

// a real, zero-phase response on M + 1 bins (0 … Nyquist) → the centred FIR of 2M taps: inverse FFT (÷ 2M), the
// response's time 0 moved to tap M, a Hann window of 2M + 2 points without its two end points
std::vector<float> centredFir (const std::vector<float>& mag)
{
    const int M = (int) mag.size() - 1, N = 2 * M;
    std::vector<cd> z ((std::size_t) N);
    for (int k = 0; k <= M; ++k)
        z[(std::size_t) k] = cd (mag[(std::size_t) k], 0.0);
    for (int k = 1; k < M; ++k)
        z[(std::size_t) (N - k)] = z[(std::size_t) k];
    Fft (N).run (z.data(), true);
    std::vector<float> h ((std::size_t) N);
    const double step = 2.0 * pi / (double) (N + 1);
    for (int j = 0; j < N; ++j)
    {
        const auto w = f32 (0.5 - std::cos (step * (double) (j + 1)) * 0.5);
        h[(std::size_t) j] = f32 (z[(std::size_t) ((j + M) % N)].real() / (double) N * (double) w);
    }
    return h;
}

// y[t] = Σ h[m]·x[t + M − m] for t = from … from + count − 1 (M = taps / 2; zeros outside x), two real signals at once
void centredConvolve (const std::vector<float>& h, const float* a, const float* b, std::size_t n, std::size_t from, std::size_t count,
                      float* outA, float* outB)
{
    const std::size_t K = h.size(), M = K / 2;
    const int size = pow2AtLeast (n + K - 1);
    Fft fft (size);
    std::vector<cd> z ((std::size_t) size), hz ((std::size_t) size);
    for (std::size_t i = 0; i < n; ++i)
        z[i] = cd (a[i], b != nullptr ? b[i] : 0.0f);
    for (std::size_t i = 0; i < K; ++i)
        hz[i] = cd (h[i], 0.0);
    fft.run (z.data(), false);
    fft.run (hz.data(), false);
    for (int k = 0; k < size; ++k)
        z[(std::size_t) k] = mul (z[(std::size_t) k], hz[(std::size_t) k]);
    fft.run (z.data(), true);
    const double inv = 1.0 / (double) size;
    for (std::size_t t = 0; t < count; ++t)
    {
        const cd v = z[from + t + M];
        outA[t] = f32 (v.real() * inv);
        if (outB != nullptr)
            outB[t] = f32 (v.imag() * inv);
    }
}

// the response smoother: a Gaussian exp(−½(k/128)²) on 65 537 bins made into its centred FIR (131 072 taps)
const std::vector<float>& smoothingFir()
{
    static const std::vector<float> h = [] {
        constexpr int n = 65537;
        const auto slope = f32 (1.0 / ((double) n * 0.0078125 * 0.25));
        std::vector<float> g ((std::size_t) n);
        for (int k = 0; k < n; ++k)
        {
            const float r = (float) k * slope;
            g[(std::size_t) k] = f32 (std::exp ((double) ((r * r) * -0.5f)));
        }
        return centredFir (g);
    }();
    return h;
}

// the response smoothed along the frequency axis: the plugin extends each end by its own value over one response
// length (zeros beyond) and keeps the middle. The constant ends are added here from running sums of the smoother.
std::vector<float> smoothResponse (const std::vector<float>& g)
{
    const auto& h = smoothingFir();
    const long long L = (long long) g.size(), K = (long long) h.size(), s = K / 2;
    std::vector<double> sum ((std::size_t) K + 1, 0.0); // sum[m] = h[0] + … + h[m − 1]
    for (long long m = 0; m < K; ++m)
        sum[(std::size_t) (m + 1)] = sum[(std::size_t) m] + (double) h[(std::size_t) m];
    auto range = [&] (long long lo, long long hi) { // h[lo] + … + h[hi], clipped to the taps
        lo = std::max (lo, 0LL), hi = std::min (hi, K - 1);
        return hi < lo ? 0.0 : sum[(std::size_t) (hi + 1)] - sum[(std::size_t) lo];
    };
    std::vector<float> mid ((std::size_t) L);
    centredConvolve (h, g.data(), nullptr, (std::size_t) L, 0, (std::size_t) L, mid.data(), nullptr);
    std::vector<float> out ((std::size_t) L);
    const double first = g.front(), last = g.back();
    for (long long t = 0; t < L; ++t)
    {
        // padded index L + t + s − m: below L = the first value (down to index 0), from 2L = the last (up to 3L − 1)
        const double ends = first * range (t + s + 1, L + t + s) + last * range (t + s - 2 * L + 1, t + s - L);
        out[(std::size_t) t] = f32 ((double) mid[(std::size_t) t] + ends);
    }
    return out;
}

// the shaped EQ curve on the impulse response at `rate` (= host rate × speed): FUN_0087b2b0
void applyEqCurve (std::vector<float>& l, std::vector<float>& r, const std::vector<EnvPoint>& curve, double rate, float speed, float amount)
{
    const std::size_t n = l.size();
    if (n == 0)
        return;
    int bits = 1;
    const long long want = (long long) std::nearbyint (rate * 2.0);
    while ((1LL << bits) < want)
        ++bits;
    bits = std::clamp (bits, 4, 20);
    const int M = 1 << bits;
    const auto& ax = freqAxis();
    const double q = rate / ((double) M * ax.fmax * (double) speed * 2.0);
    EqCurve c (curve);
    std::vector<float> g ((std::size_t) M + 1);
    double prev = 0.0;
    for (int k = 0; k <= M; ++k)
    {
        const auto x = f32 ((double) k * q);
        const auto pos = (double) f32 (ax.toX ((double) x * ax.fmax));
        g[(std::size_t) k] = levelGain (c.eval (pos - prev));
        prev = pos;
    }
    const auto h = centredFir (smoothResponse (g));
    std::vector<float> fl (n), fr (n);
    centredConvolve (h, l.data(), r.data(), n, 0, n, fl.data(), fr.data());
    const float dry = 1.0f - amount;
    for (std::size_t i = 0; i < n; ++i)
    {
        l[i] = fl[i] * amount + l[i] * dry;
        r[i] = fr[i] * amount + r[i] * dry;
    }
}

// ------------------------------------------------------------------------------- partitioned convolution
// Zero-latency stereo convolution: a direct head, then uniformly partitioned overlap-save levels. The 128-sample level's
// partitions start at its own size (its output is computed when its block begins); the larger levels' partitions start
// at twice their size or later, so a block's output can be computed during the previous block, a slice every 128
// samples (REA round 87: computing every level at once on the same sample took ~15 ms in one audio callback — a
// crackle — once every 16384 samples).
class Convolver
{
public:
    void setKernel (const std::vector<float>& kl, const std::vector<float>& kr)
    {
        const std::size_t len = std::max (kl.size(), kr.size());
        headL.assign ((std::size_t) head, 0.0f);
        headR.assign ((std::size_t) head, 0.0f);
        for (std::size_t i = 0; i < std::min (len, (std::size_t) head); ++i)
            headL[i] = i < kl.size() ? kl[i] : 0.0f, headR[i] = i < kr.size() ? kr[i] : 0.0f;
        levels.clear();
        static const int sizes[] = { 128, 1024, 8192, 16384 };
        static const int starts[] = { 128, 2048, 16384, 65536 };
        static const int ends[] = { 2048, 16384, 65536, 1 << 30 };
        for (int k = 0; k < 4; ++k)
        {
            if ((std::size_t) starts[k] >= len)
                break;
            Level lv;
            lv.N = sizes[k];
            lv.j0 = starts[k] / sizes[k];
            const std::size_t stop = std::min (len, (std::size_t) ends[k]);
            lv.parts = (int) ((stop - (std::size_t) starts[k] + (std::size_t) lv.N - 1) / (std::size_t) lv.N);
            lv.fft = std::make_shared<Fft> (2 * lv.N);
            const int bins = lv.N + 1;
            lv.hl.assign ((std::size_t) (lv.parts * bins), {});
            lv.hr.assign ((std::size_t) (lv.parts * bins), {});
            std::vector<cd> z ((std::size_t) (2 * lv.N));
            for (int p = 0; p < lv.parts; ++p)
            {
                std::fill (z.begin(), z.end(), cd {});
                for (int i = 0; i < lv.N; ++i)
                {
                    const std::size_t t = (std::size_t) starts[k] + (std::size_t) (p * lv.N + i);
                    if (t < len)
                        z[(std::size_t) i] = cd (t < kl.size() ? kl[t] : 0.0f, t < kr.size() ? kr[t] : 0.0f);
                }
                lv.fft->run (z.data(), false);
                split (z, 2 * lv.N, lv.hl.data() + p * bins, lv.hr.data() + p * bins);
            }
            lv.ages = lv.j0 + lv.parts - 1;
            lv.xl.assign ((std::size_t) (lv.ages * bins), {});
            lv.xr.assign ((std::size_t) (lv.ages * bins), {});
            lv.out.assign ((std::size_t) (2 * lv.N), 0.0f);
            lv.next.assign ((std::size_t) (2 * lv.N), 0.0f);
            lv.spread = lv.j0 >= 2;
            // the job's slices: copy + reorder + passes of the forward transform, one per partition, the inverse's
            // reorder + passes, and the output
            lv.units = 1 + 1 + lv.fft->passes() + lv.parts + 1 + 1 + lv.fft->passes() + 1;
            lv.work.assign ((std::size_t) (2 * lv.N), {});
            lv.accL.assign ((std::size_t) bins, {});
            lv.accR.assign ((std::size_t) bins, {});
            levels.push_back (std::move (lv));
        }
        int maxN = 128;
        for (const auto& lv : levels)
            maxN = std::max (maxN, lv.N);
        ring = pow2AtLeast ((std::size_t) (2 * maxN + head));
        histL.assign ((std::size_t) ring, 0.0f);
        histR.assign ((std::size_t) ring, 0.0f);
        clear();
    }

    void clear() noexcept
    {
        std::fill (histL.begin(), histL.end(), 0.0f);
        std::fill (histR.begin(), histR.end(), 0.0f);
        for (auto& lv : levels)
        {
            std::fill (lv.xl.begin(), lv.xl.end(), cd {});
            std::fill (lv.xr.begin(), lv.xr.end(), cd {});
            std::fill (lv.out.begin(), lv.out.end(), 0.0f);
            std::fill (lv.next.begin(), lv.next.end(), 0.0f);
            lv.newest = 0;
            lv.done = lv.units; // no job in progress
        }
        t = 0;
    }

    // in place, planar
    void process (float* l, float* r, int n) noexcept
    {
        const int mask = ring - 1;
        for (int s = 0; s < n; ++s)
        {
            if ((t & 127) == 0)
                for (auto& lv : levels)
                {
                    const auto phase = (int) (t % lv.N);
                    if (! lv.spread)
                    {
                        if (phase == 0)
                            step (lv);
                        continue;
                    }
                    if (phase == 0)
                    {
                        advance (lv, lv.units); // (normally already complete)
                        std::swap (lv.out, lv.next);
                        startJob (lv);
                    }
                    // this 128-sample slice's share of the job: complete before the level's next block
                    advance (lv, (int) (((long long) lv.units * (phase + 128) + lv.N - 1) / lv.N));
                }
            const int w = (int) (t & mask);
            histL[(std::size_t) w] = l[s];
            histR[(std::size_t) w] = r[s];
            double yl = 0.0, yr = 0.0;
            for (int k = 0; k < head; ++k)
            {
                const auto q = (std::size_t) ((w - k) & mask);
                yl += (double) headL[(std::size_t) k] * (double) histL[q];
                yr += (double) headR[(std::size_t) k] * (double) histR[q];
            }
            for (auto& lv : levels)
            {
                const auto o = (std::size_t) (t % lv.N);
                yl += (double) lv.out[o];
                yr += (double) lv.out[(std::size_t) lv.N + o];
            }
            l[s] = (float) yl;
            r[s] = (float) yr;
            ++t;
        }
    }

private:
    struct Level
    {
        int N = 0, j0 = 1, parts = 0, ages = 0, newest = 0;
        std::shared_ptr<Fft> fft;
        std::vector<std::complex<float>> hl, hr, xl, xr; // spectra (N + 1 bins) of the partitions / of the inputs
        std::vector<cd> work, accL, accR;
        std::vector<float> out; // [0, N) left, [N, 2N) right: the level's output for the current block
        // spread levels: the next block's output, built during the current one by `units` slices (`done` so far)
        std::vector<float> next;
        bool spread = false;
        int units = 0, done = 0;
    };

    // a spread level's block boundary: the newest 2N inputs go into the work buffer now (the history ring is
    // overwritten during the block); the slices then compute the output of the block after this one
    void startJob (Level& lv) noexcept
    {
        const int N = lv.N, mask = ring - 1;
        for (int i = 0; i < 2 * N; ++i)
        {
            const auto q = (std::size_t) ((t - 2 * N + i) & mask);
            lv.work[(std::size_t) i] = cd (histL[q], histR[q]);
        }
        lv.newest = (lv.newest + lv.ages - 1) % lv.ages;
        lv.done = 0;
    }

    void advance (Level& lv, int target) noexcept
    {
        const int N = lv.N, bins = N + 1, passes = lv.fft->passes();
        target = std::min (target, lv.units);
        while (lv.done < target)
        {
            int u = lv.done++;
            // 0: (inputs copied at the boundary) reorder, then the forward passes
            if (u == 0)
            {
                lv.fft->bitReverse (lv.work.data());
                continue;
            }
            u -= 1;
            if (u < passes)
            {
                lv.fft->pass (lv.work.data(), 2 << u, false);
                continue;
            }
            u -= passes;
            if (u == 0)
            {
                split (lv.work, 2 * N, lv.xl.data() + lv.newest * bins, lv.xr.data() + lv.newest * bins);
                std::fill (lv.accL.begin(), lv.accL.end(), cd {});
                std::fill (lv.accR.begin(), lv.accR.end(), cd {});
                continue;
            }
            u -= 1;
            if (u < lv.parts)
            {
                // partition p meets the input that will be j0 − 1 + p blocks old in the next block
                const int p = u;
                const int slot = (lv.newest + lv.j0 - 2 + p) % lv.ages;
                const auto* xl = lv.xl.data() + slot * bins;
                const auto* xr = lv.xr.data() + slot * bins;
                const auto* hl = lv.hl.data() + p * bins;
                const auto* hr = lv.hr.data() + p * bins;
                for (int k = 0; k < bins; ++k)
                {
                    lv.accL[(std::size_t) k] += mul (cd (xl[k]), cd (hl[k]));
                    lv.accR[(std::size_t) k] += mul (cd (xr[k]), cd (hr[k]));
                }
                continue;
            }
            u -= lv.parts;
            if (u == 0)
            {
                for (int k = 0; k <= N; ++k)
                    lv.work[(std::size_t) k] = lv.accL[(std::size_t) k] + cd (0.0, 1.0) * lv.accR[(std::size_t) k];
                for (int k = N + 1; k < 2 * N; ++k)
                    lv.work[(std::size_t) k] = std::conj (lv.accL[(std::size_t) (2 * N - k)]) + cd (0.0, 1.0) * std::conj (lv.accR[(std::size_t) (2 * N - k)]);
                continue;
            }
            u -= 1;
            if (u == 0)
            {
                lv.fft->bitReverse (lv.work.data());
                continue;
            }
            u -= 1;
            if (u < passes)
            {
                lv.fft->pass (lv.work.data(), 2 << u, true);
                continue;
            }
            const double inv = 1.0 / (double) (2 * N);
            for (int i = 0; i < N; ++i)
            {
                lv.next[(std::size_t) i] = (float) (lv.work[(std::size_t) (N + i)].real() * inv);
                lv.next[(std::size_t) (N + i)] = (float) (lv.work[(std::size_t) (N + i)].imag() * inv);
            }
        }
    }

    // spectra of the two real signals packed in z = a + ib (bins 0…N of a 2N transform)
    template <typename T>
    static void split (const std::vector<cd>& z, int size, T* a, T* b) noexcept
    {
        for (int k = 0; k <= size / 2; ++k)
        {
            const cd zk = z[(std::size_t) k], zc = std::conj (z[(std::size_t) ((size - k) % size)]);
            const cd x = (zk + zc) * 0.5, y = (zk - zc) * cd (0.0, -0.5);
            a[k] = T (x);
            b[k] = T (y);
        }
    }

    void step (Level& lv) noexcept
    {
        const int N = lv.N, bins = N + 1, mask = ring - 1;
        // spectrum of the last 2N inputs (times t−2N … t−1)
        for (int i = 0; i < 2 * N; ++i)
        {
            const auto q = (std::size_t) ((t - 2 * N + i) & mask);
            lv.work[(std::size_t) i] = cd (histL[q], histR[q]);
        }
        lv.fft->run (lv.work.data(), false);
        lv.newest = (lv.newest + lv.ages - 1) % lv.ages;
        split (lv.work, 2 * N, lv.xl.data() + lv.newest * bins, lv.xr.data() + lv.newest * bins);
        std::fill (lv.accL.begin(), lv.accL.end(), cd {});
        std::fill (lv.accR.begin(), lv.accR.end(), cd {});
        for (int p = 0; p < lv.parts; ++p)
        {
            const int age = lv.j0 - 1 + p;
            const int slot = (lv.newest + age) % lv.ages;
            const auto* xl = lv.xl.data() + slot * bins;
            const auto* xr = lv.xr.data() + slot * bins;
            const auto* hl = lv.hl.data() + p * bins;
            const auto* hr = lv.hr.data() + p * bins;
            for (int k = 0; k < bins; ++k)
            {
                lv.accL[(std::size_t) k] += cd (xl[k]) * cd (hl[k]);
                lv.accR[(std::size_t) k] += cd (xr[k]) * cd (hr[k]);
            }
        }
        // Z = YL + i·YR over the full spectrum (Hermitian halves rebuilt)
        for (int k = 0; k <= N; ++k)
            lv.work[(std::size_t) k] = lv.accL[(std::size_t) k] + cd (0.0, 1.0) * lv.accR[(std::size_t) k];
        for (int k = N + 1; k < 2 * N; ++k)
            lv.work[(std::size_t) k] = std::conj (lv.accL[(std::size_t) (2 * N - k)]) + cd (0.0, 1.0) * std::conj (lv.accR[(std::size_t) (2 * N - k)]);
        lv.fft->run (lv.work.data(), true);
        const double inv = 1.0 / (double) (2 * N);
        for (int i = 0; i < N; ++i)
        {
            lv.out[(std::size_t) i] = (float) (lv.work[(std::size_t) (N + i)].real() * inv);
            lv.out[(std::size_t) (N + i)] = (float) (lv.work[(std::size_t) (N + i)].imag() * inv);
        }
    }

    static constexpr int head = 128;
    std::vector<float> headL, headR, histL, histR;
    std::vector<Level> levels;
    int ring = 256;
    long long t = 0;
};

// ------------------------------------------------------------------------------------ gains and ramps
// FL's level law of the dry control: (e^(2.39789534·x) − 1)·0.1, exactly 1 at full scale
float dryGain (int v) noexcept
{
    const float x = (float) v * 0.001f;
    if (x == 1.0f)
        return 1.0f;
    return f32 ((std::exp ((double) (x * 2.39789534f)) - 1.0) * (double) 0.1f);
}
// the wet control: (1001^x − 1)·0.001
float wetGain (int v) noexcept
{
    const float x = (float) v * 0.001f;
    const auto e = f32 (std::exp ((double) x * std::log (1001.0)) - 1.0);
    return f32 ((double) e * 0.001);
}

// FL's stereo gain matrix (outL = m0·L + m2·R, outR = m1·R + m3·L) for a gain and a separation at centre pan:
// separation > 0 merges ((5^s − 1)/4), < 0 separates
std::array<float, 4> gainMatrix (float gain, float sep) noexcept
{
    if (sep == 0.0f)
        return { gain, gain, 0.0f, 0.0f };
    if (gain == 0.0f)
        return { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> m {};
    const long double a = (1.0L + 0.0L) * (long double) 0.7853981633974483;
    const long double g = (long double) (gain * 1.41421354f);
    m[0] = (float) (cosl (a) * g);
    m[1] = (float) (sinl (a) * g);
    float k = sep;
    if (sep > 0.0f)
        k = (float) (std::exp ((double) sep * std::log (5.0)) - 1.0) * 0.25f;
    const float fa = std::min (0.0f + k, 1.0f), fb = std::min (0.0f + k, 1.0f);
    m[2] = m[0] * fa * 0.5f;
    m[0] = m[0] * (1.0f - fa * 0.5f);
    m[3] = m[1] * fb * 0.5f;
    m[1] = m[1] * (1.0f - fb * 0.5f);
    return m;
}

struct Ramp
{
    float start = 0.0f, inc = 0.0f;
};
Ramp ramp (float& current, float target, int n) noexcept
{
    constexpr float maxStep = 0.001f;
    Ramp r;
    r.start = current;
    r.inc = target - r.start;
    if (r.inc != 0.0f)
    {
        current = target;
        r.inc = r.inc / (float) n;
        if (std::abs (r.inc) > maxStep)
        {
            r.inc = std::copysign (maxStep, r.inc);
            current = r.start + r.inc * (float) n;
        }
    }
    return r;
}

// dst (=, or += when `add`) src × matrix, the matrix ramped per sample in pairs as the plugin's unrolled loop does
void applyMatrix (const float* src, float* dst, int n, const std::array<float, 4>& m0, const std::array<Ramp, 4>& rp, bool add) noexcept
{
    const bool ramping = rp[0].inc != 0.0f || rp[1].inc != 0.0f || rp[2].inc != 0.0f || rp[3].inc != 0.0f;
    auto put = [add] (float& d, float v) { d = add ? v + d : v; };
    std::array<float, 4> m = m0;
    if (! ramping)
    {
        if (m[2] == 0.0f && m[3] == 0.0f)
        {
            if (m[0] == 0.0f && m[1] == 0.0f)
            {
                if (! add)
                    std::fill (dst, dst + 2 * n, 0.0f);
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                put (dst[2 * i], src[2 * i] * m[0]);
                put (dst[2 * i + 1], src[2 * i + 1] * m[1]);
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float a = src[2 * i], b = src[2 * i + 1];
            put (dst[2 * i], a * m[0] + b * m[2]);
            put (dst[2 * i + 1], b * m[1] + a * m[3]);
        }
        return;
    }
    int i = 0;
    for (; i + 1 < n; i += 2)
    {
        std::array<float, 4> m1;
        for (int k = 0; k < 4; ++k)
            m1[(std::size_t) k] = m[(std::size_t) k] + rp[(std::size_t) k].inc;
        float a = src[2 * i], b = src[2 * i + 1];
        put (dst[2 * i], a * m[0] + b * m[2]);
        put (dst[2 * i + 1], b * m[1] + a * m[3]);
        a = src[2 * i + 2], b = src[2 * i + 3];
        put (dst[2 * i + 2], a * m1[0] + b * m1[2]);
        put (dst[2 * i + 3], b * m1[1] + a * m1[3]);
        for (int k = 0; k < 4; ++k)
            m[(std::size_t) k] = m1[(std::size_t) k] + rp[(std::size_t) k].inc;
    }
    if (i < n)
    {
        const float a = src[2 * i], b = src[2 * i + 1];
        put (dst[2 * i], a * m[0] + b * m[2]);
        put (dst[2 * i + 1], b * m[1] + a * m[3]);
    }
}
} // namespace

// ------------------------------------------------------------------------------------------------ state

std::optional<State> parseState (const std::vector<std::uint8_t>& d)
{
    // FUN_00875550: version word (≥ 0x10000: version << 16 | build), 4 controls, the source (path or embedded
    // WAV), a 0x24-byte editor header, the editor's envelopes and EQ curve, 4 flag bytes, an int (version > 14) and
    // the knobs (5 from version 11, else 4 and the EQ amount at its default)
    if (d.size() < 4 + 16 + 2)
        return std::nullopt;
    const auto raw = u32 (d, 0);
    if ((std::int32_t) raw >= 0xf0004 || (std::int32_t) raw <= 9)
        return std::nullopt;
    State s;
    s.version = (int) (raw > 0xffff ? raw >> 16 : raw);
    std::size_t o = 4;
    for (int k = 0; k < 4; ++k)
        s.params[(std::size_t) k] = i32 (d, o + 4 * (std::size_t) k);
    o += 16;
    s.fromFile = d[o++] != 0;
    std::size_t len = d[o++];
    if (len == 0xff)
    {
        if (d.size() < o + 4)
            return std::nullopt;
        len = u32 (d, o);
        o += 4;
    }
    if (d.size() < o + len)
        return std::nullopt;
    s.path.assign ((const char*) d.data() + o, len);
    o += len;
    if (! s.fromFile)
    {
        if (d.size() < o + 4)
            return std::nullopt;
        const auto size = i32 (d, o);
        o += 4;
        if (size > 0)
        {
            if (d.size() < o + (std::size_t) size)
                return std::nullopt;
            s.wav.assign (d.begin() + (std::ptrdiff_t) o, d.begin() + (std::ptrdiff_t) (o + (std::size_t) size));
            o += (std::size_t) size;
        }
    }
    o += 0x24; // editor header (selection/view): no effect on the sound
    for (int e = 0; e < 5; ++e)
    {
        if (e == 4)
            s.eqAt = o;
        if (d.size() < o + 12)
            return std::nullopt;
        const auto n = i32 (d, o + 8);
        o += 12;
        if (n < 0 || n > 100000 || d.size() < o + 24 * (std::size_t) n + 20)
            return std::nullopt;
        for (int i = 0; i < n; ++i, o += 24)
        {
            EnvPoint p;
            p.dt = f64 (d, o);
            p.y = f64 (d, o + 8);
            p.tension = f32At (d, o + 16);
            p.mode = d[o + 20];
            s.envelopes[(std::size_t) e].push_back (p);
        }
        o += 20;
    }
    s.eqEnd = o;
    if (d.size() < o + 4)
        return std::nullopt;
    s.flagsAt = o;
    for (int k = 0; k < 4; ++k)
        s.flags[(std::size_t) k] = d[o + (std::size_t) k];
    o += 4;
    if (s.version > 14)
    {
        if (d.size() < o + 4)
            return std::nullopt;
        s.option = i32 (d, o);
        o += 4;
    }
    const int nk = s.version >= 11 ? 5 : 4;
    if (d.size() < o + 4 * (std::size_t) nk)
        return std::nullopt;
    s.knobsAt = o;
    s.knobCount = nk;
    for (int k = 0; k < nk; ++k)
        s.knobs[(std::size_t) k] = i32 (d, o + 4 * (std::size_t) k);
    if (nk == 4)
        s.knobs[4] = 1000;
    return s;
}

std::optional<Audio> decodeAudio (const std::vector<std::uint8_t>& b)
{
    if (b.size() >= 12 && std::memcmp (b.data(), "RIFF", 4) == 0 && std::memcmp (b.data() + 8, "WAVE", 4) == 0)
    {
        std::size_t o = 12;
        int tag = 0, channels = 0, bits = 0;
        double rate = 0.0;
        const std::uint8_t* data = nullptr;
        std::size_t dataSize = 0;
        while (o + 8 <= b.size())
        {
            const auto n = (std::size_t) u32 (b, o + 4);
            const auto* id = b.data() + o;
            if (std::memcmp (id, "fmt ", 4) == 0 && o + 8 + 16 <= b.size())
            {
                tag = b[o + 8] | (b[o + 9] << 8);
                channels = b[o + 10] | (b[o + 11] << 8);
                rate = (double) u32 (b, o + 12);
                bits = b[o + 22] | (b[o + 23] << 8);
                if (tag == 0xfffe && n >= 40 && o + 8 + 26 <= b.size())
                    tag = b[o + 8 + 24] | (b[o + 8 + 25] << 8);
            }
            else if (std::memcmp (id, "data", 4) == 0)
            {
                data = b.data() + o + 8;
                dataSize = std::min (n, b.size() - (o + 8));
            }
            o += 8 + n + (n & 1);
        }
        if (data == nullptr || channels <= 0 || rate <= 0.0 || bits <= 0)
            return std::nullopt;
        const int bytes = bits / 8;
        if (bytes <= 0 || ! ((tag == 1 && bytes <= 4) || (tag == 3 && (bytes == 4 || bytes == 8))))
            return std::nullopt;
        const std::size_t frames = dataSize / ((std::size_t) bytes * (std::size_t) channels);
        Audio a;
        a.rate = rate;
        a.l.resize (frames);
        a.r.resize (frames);
        auto sample = [&] (const std::uint8_t* p) -> float {
            if (tag == 3)
            {
                if (bytes == 4)
                {
                    float v;
                    std::memcpy (&v, p, 4);
                    return v;
                }
                double v;
                std::memcpy (&v, p, 8);
                return (float) v;
            }
            switch (bytes)
            {
                case 1: return (float) ((int) p[0] - 128) * (1.0f / 128.0f);
                case 2: return (float) (std::int16_t) (p[0] | (p[1] << 8)) * (1.0f / 32768.0f);
                case 3:
                {
                    std::int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
                    if (v & 0x800000)
                        v -= 0x1000000;
                    return (float) v * (1.0f / 8388608.0f);
                }
                default:
                {
                    std::int32_t v;
                    std::memcpy (&v, p, 4);
                    return (float) ((double) v * (1.0 / 2147483648.0));
                }
            }
        };
        for (std::size_t i = 0; i < frames; ++i)
        {
            const auto* p = data + i * (std::size_t) bytes * (std::size_t) channels;
            a.l[i] = sample (p);
            a.r[i] = channels > 1 ? sample (p + bytes) : a.l[i];
        }
        return a;
    }
    DecodedAudio w;
    if (! b.empty() && decodeWavPack (b.data(), b.size(), w) && ! w.channels.empty())
    {
        Audio a;
        a.rate = w.sampleRate;
        a.l = w.channels[0];
        a.r = w.channels.size() > 1 ? w.channels[1] : w.channels[0];
        return a;
    }
    return std::nullopt;
}

bool kernelSupported (const State& s) noexcept
{
    // envelopes that are not decoded must sit at their defaults: the first and third at 0.5, the fourth flat; the
    // volume envelope must be flat (the EQ curve is decoded in every form, REA_rodada60 §7 C.7)
    double lev = 0.5;
    if (! flat (s.envelopes[0], lev) || lev != 0.5 || ! flat (s.envelopes[2], lev) || lev != 0.5 || ! flat (s.envelopes[3], lev))
        return false;
    return flat (s.envelopes[1], lev);
}

std::optional<Kernel> buildKernel (const State& s, const Audio& ir, int fs)
{
    if (! kernelSupported (s) || ir.rate <= 0.0)
        return std::nullopt;
    double volume = 0.5, eq = 0.5;
    flat (s.envelopes[1], volume);
    const bool eqFlat = flat (s.envelopes[4], eq);

    std::vector<float> l = ir.l, r = ir.r;
    r.resize (l.size());
    // normalisation
    if (s.flags[3] != 0)
        if (const auto g = normaliseGain (l, r, ir.rate); g > 0.0f)
            scale (l, g), scale (r, g);
    // volume envelope (flat): level × 2
    if (volume != 0.5)
    {
        const float g = (float) volume * 2.0f;
        scale (l, g), scale (r, g);
    }
    // blend knob: self-convolution
    const float blend = (float) s.knobs[2] * 0.001f;
    if (blend != 0.0f)
        selfConvolve (l, r, blend);
    // speed knob: resampling to fs·2^(k/1000)
    const auto speed = f32 (std::exp ((double) ((float) s.knobs[3] * 0.001f) * ln2));
    const double target = (double) ((float) fs * speed);
    if (target != ir.rate && ! l.empty())
        resample (l, r, target / ir.rate);
    // EQ curve: a flat one at level v multiplies by (3^(2v) − 1)/2 whatever the EQ amount; a shaped one filters the
    // impulse response when the amount is above 0, mixed by the amount (C.7)
    if (eqFlat)
    {
        if (const float g = levelGain (eq); (double) g != 1.0)
            scale (l, g), scale (r, g);
    }
    else if (const float amount = (float) s.knobs[4] * 0.001f; amount > 0.0f)
        applyEqCurve (l, r, s.envelopes[4], (double) ((float) fs * speed), speed, amount);
    // trailing silence
    long n = (long) l.size();
    {
        long i = n;
        while (i > 0 && std::abs (l[(std::size_t) (i - 1)]) < silence && std::abs (r[(std::size_t) (i - 1)]) < silence)
            --i;
        n = i;
        l.resize ((std::size_t) n);
        r.resize ((std::size_t) n);
    }
    // predelay knob: (7^x − 1)·83.33 ms
    const float px = (float) s.knobs[1] * 0.001f;
    const auto pv = f32 (std::exp ((double) px * std::log (7.0)) - 1.0);
    const int pre = roundEven ((double) pv * 83.333333333333329 * 0.001 * (double) fs);
    Kernel k;
    k.tail = pre + (int) n;
    // leading silence and predelay: a power-of-two delay (≥ 512) plus the rest as zeros at the kernel's start
    long lead = 0;
    while (lead < n && std::abs (l[(std::size_t) lead]) < silence && std::abs (r[(std::size_t) lead]) < silence)
        ++lead;
    long rest = (long) pre + lead, big = 0;
    if (rest >= 512)
    {
        big = 512;
        while (big * 2 <= rest)
            big *= 2;
        rest -= big;
    }
    const long cut = lead - rest;
    long zeros = big;
    std::size_t from = 0;
    if (cut > 0)
        from = (std::size_t) cut;
    else if (cut < 0)
        zeros += -cut + 1; // the plugin's insertion adds one sample more than the difference
    k.l.assign ((std::size_t) zeros, 0.0f);
    k.r.assign ((std::size_t) zeros, 0.0f);
    k.l.insert (k.l.end(), l.begin() + (std::ptrdiff_t) std::min (from, l.size()), l.end());
    k.r.insert (k.r.end(), r.begin() + (std::ptrdiff_t) std::min (from, r.size()), r.end());
    if (k.l.empty())
        k.l.assign (1, 0.0f), k.r.assign (1, 0.0f);
    return k;
}

// ------------------------------------------------------------------------------------------------ engine

struct Engine::Impl
{
    static constexpr int maxBlock = 16384; // one FL block (larger host blocks are split)
    Convolver conv;
    bool hasKernel = false;
    int tail = 0, quietLeft = 0;
    double rateFactor = 1.0; // the plugin scales the wet gain by 44100 / host rate
    float dry = 1.0f, dryNow = 1.0f;
    std::array<float, 4> mIn { 1, 1, 0, 0 }, mInNow { 1, 1, 0, 0 }, mWet { 1, 1, 0, 0 }, mWetNow { 1, 1, 0, 0 };
    std::vector<float> in = std::vector<float> (2 * maxBlock), out = std::vector<float> (2 * maxBlock), buf = std::vector<float> (2 * maxBlock),
                       pl = std::vector<float> (maxBlock), pr = std::vector<float> (maxBlock);

    void snap() noexcept
    {
        dryNow = dry;
        mInNow = mIn;
        mWetNow = mWet;
    }

    // FUN_0087a000 on an interleaved block (normal path, no capture / preview)
    void render (const float* src, float* out, int n) noexcept
    {
        bool quiet = true;
        for (int i = 0; i < 2 * n && quiet; ++i)
            quiet = ! (std::abs (src[i]) > quietInput);
        quietLeft = quiet ? quietLeft - n : tail;
        if (! (-n < quietLeft) || ! hasKernel)
        {
            quietLeft = hasKernel ? 0 : quietLeft;
            std::copy (src, src + 2 * n, out);
            snap();
            return;
        }
        // input matrix → kernel
        {
            std::array<Ramp, 4> rp;
            std::array<float, 4> st;
            for (int k = 0; k < 4; ++k)
                rp[(std::size_t) k] = ramp (mInNow[(std::size_t) k], mIn[(std::size_t) k], n), st[(std::size_t) k] = rp[(std::size_t) k].start;
            applyMatrix (src, buf.data(), n, st, rp, false);
        }
        for (int i = 0; i < n; ++i)
            pl[(std::size_t) i] = buf[(std::size_t) (2 * i)], pr[(std::size_t) i] = buf[(std::size_t) (2 * i + 1)];
        conv.process (pl.data(), pr.data(), n);
        for (int i = 0; i < n; ++i)
            buf[(std::size_t) (2 * i)] = pl[(std::size_t) i], buf[(std::size_t) (2 * i + 1)] = pr[(std::size_t) i];
        // dry: the ramp in pairs of samples (FUN_00622140); a silent block passes unchanged
        if (quiet)
        {
            std::copy (src, src + 2 * n, out);
            dryNow = dry;
        }
        else
        {
            const auto dr = ramp (dryNow, dry, n);
            if (dr.inc == 0.0f)
            {
                if (dr.start == 0.0f)
                    std::fill (out, out + 2 * n, 0.0f);
                else if (dr.start == 1.0f)
                    std::copy (src, src + 2 * n, out);
                else
                    for (int i = 0; i < 2 * n; ++i)
                        out[i] = src[i] * dr.start;
            }
            else
            {
                float even = dr.start + 0.0f, odd = dr.start + dr.inc;
                const float inc2 = dr.inc + dr.inc;
                int i = 0;
                for (; i + 8 <= n; i += 8)
                    for (int j = 0; j < 8; j += 2)
                    {
                        out[2 * (i + j)] = src[2 * (i + j)] * even, out[2 * (i + j) + 1] = src[2 * (i + j) + 1] * even;
                        out[2 * (i + j + 1)] = src[2 * (i + j + 1)] * odd, out[2 * (i + j + 1) + 1] = src[2 * (i + j + 1) + 1] * odd;
                        even = even + inc2, odd = odd + inc2;
                    }
                for (; i < n; ++i)
                {
                    out[2 * i] = src[2 * i] * even, out[2 * i + 1] = src[2 * i + 1] * even;
                    even = even + dr.inc;
                }
            }
        }
        // wet matrix, added
        std::array<Ramp, 4> rp;
        std::array<float, 4> st;
        for (int k = 0; k < 4; ++k)
            rp[(std::size_t) k] = ramp (mWetNow[(std::size_t) k], mWet[(std::size_t) k], n), st[(std::size_t) k] = rp[(std::size_t) k].start;
        applyMatrix (buf.data(), out, n, st, rp, true);
    }
};

Engine::Engine() : d (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::setRate (int fs) noexcept { d->rateFactor = 44100.0 / (double) (fs > 0 ? fs : 44100); }

void Engine::setKernel (const Kernel& k)
{
    d->conv.setKernel (k.l, k.r);
    d->tail = k.tail;
    d->hasKernel = true;
    d->quietLeft = 0;
}

void Engine::setParams (const std::array<int, 4>& raw) noexcept
{
    d->dry = dryGain (raw[0]);
    d->mIn = gainMatrix (1.0f, (float) raw[2] * 0.001f);
    d->mWet = gainMatrix (f32 ((double) wetGain (raw[1]) * d->rateFactor), (float) raw[3] * 0.001f);
}

void Engine::reset (bool fresh) noexcept
{
    d->conv.clear();
    d->quietLeft = 0;
    d->snap();
    if (fresh)
    {
        d->dryNow = 0.0f;
        d->mInNow = { 0, 0, 0, 0 };
        d->mWetNow = { 0, 0, 0, 0 };
    }
}

void Engine::process (float* left, float* right, int n) noexcept
{
    auto& m = *d;
    for (int done = 0; done < n;)
    {
        const int len = std::min (Impl::maxBlock, n - done);
        for (int i = 0; i < len; ++i)
            m.in[(std::size_t) (2 * i)] = left[done + i], m.in[(std::size_t) (2 * i + 1)] = right[done + i];
        m.render (m.in.data(), m.out.data(), len);
        for (int i = 0; i < len; ++i)
            left[done + i] = m.out[(std::size_t) (2 * i)], right[done + i] = m.out[(std::size_t) (2 * i + 1)];
        done += len;
    }
}

// ------------------------------------------------------------------------------------------------ blob / params

std::vector<std::uint8_t> packBlob (const std::vector<std::uint8_t>& flState, const std::vector<std::uint8_t>& irFile)
{
    std::vector<std::uint8_t> b { 'L', 'X', 'C', 'V', 1, 0, 0, 0 };
    auto put = [&b] (std::uint32_t v) {
        for (int k = 0; k < 4; ++k)
            b.push_back ((std::uint8_t) (v >> (8 * k)));
    };
    put ((std::uint32_t) flState.size());
    b.insert (b.end(), flState.begin(), flState.end());
    put ((std::uint32_t) irFile.size());
    b.insert (b.end(), irFile.begin(), irFile.end());
    return b;
}

bool unpackBlob (const std::vector<std::uint8_t>& b, std::vector<std::uint8_t>& flState, std::vector<std::uint8_t>& irFile)
{
    if (b.size() < 16 || std::memcmp (b.data(), "LXCV", 4) != 0 || u32 (b, 4) != 1)
        return false;
    const std::size_t n1 = u32 (b, 8);
    if (b.size() < 12 + n1 + 4)
        return false;
    const std::size_t n2 = u32 (b, 12 + n1);
    if (b.size() < 16 + n1 + n2)
        return false;
    flState.assign (b.begin() + 12, b.begin() + (std::ptrdiff_t) (12 + n1));
    irFile.assign (b.begin() + (std::ptrdiff_t) (16 + n1), b.begin() + (std::ptrdiff_t) (16 + n1 + n2));
    return true;
}

// ------------------------------------------------------------------------------------------- EQ curve (editor)

std::optional<std::vector<EnvPoint>> readEqCurve (const std::vector<std::uint8_t>& blob)
{
    std::vector<std::uint8_t> fl, file;
    if (! unpackBlob (blob, fl, file))
        return std::nullopt;
    const auto s = parseState (fl);
    if (! s)
        return std::nullopt;
    return s->envelopes[4];
}

bool writeEqCurve (std::vector<std::uint8_t>& blob, const std::vector<EnvPoint>& points)
{
    std::vector<std::uint8_t> fl, file;
    if (points.empty() || ! unpackBlob (blob, fl, file))
        return false;
    const auto s = parseState (fl);
    if (! s || s->eqAt == 0 || s->eqEnd < s->eqAt + 12 + 20)
        return false;
    // the map: kind and version (kept), the point count, 24-byte points (Δx, level, tension, mode, 0, 0, sign), then
    // the map's 20-byte trailer (kept)
    std::vector<std::uint8_t> m (fl.begin() + (std::ptrdiff_t) s->eqAt, fl.begin() + (std::ptrdiff_t) (s->eqAt + 8));
    auto put = [&m] (const void* p, std::size_t n) {
        const auto* q = (const std::uint8_t*) p;
        m.insert (m.end(), q, q + n);
    };
    const auto count = (std::int32_t) points.size();
    put (&count, 4);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto& p = points[i];
        put (&p.dt, 8);
        put (&p.y, 8);
        put (&p.tension, 4);
        const std::uint8_t tail[4] = { p.mode, 0, 0, EqCurve::signByte (p, i == 0) };
        put (tail, 4);
    }
    m.insert (m.end(), fl.begin() + (std::ptrdiff_t) (s->eqEnd - 20), fl.begin() + (std::ptrdiff_t) s->eqEnd);
    std::vector<std::uint8_t> out (fl.begin(), fl.begin() + (std::ptrdiff_t) s->eqAt);
    out.insert (out.end(), m.begin(), m.end());
    out.insert (out.end(), fl.begin() + (std::ptrdiff_t) s->eqEnd, fl.end());
    blob = packBlob (out, file);
    return true;
}

std::vector<float> eqCurveLevels (const std::vector<EnvPoint>& points, int count)
{
    std::vector<float> out ((std::size_t) std::max (count, 0));
    if (count <= 0)
        return out;
    EqCurve c (points);
    double prev = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double x = count > 1 ? (double) i / (double) (count - 1) : 0.0;
        out[(std::size_t) i] = (float) c.eval (x - prev);
        prev = x;
    }
    return out;
}

double eqFrequencyToX (double hz) noexcept { return freqAxis().toX (hz); }
double eqXToFrequency (double x) noexcept { return freqAxis().toHz (x); }
float eqLevelGain (double level) noexcept { return levelGain (level); }

void toParams (const std::array<int, 4>& raw, EffectParams& params) noexcept
{
    for (int k = 0; k < convolverParamCount; ++k)
    {
        const auto& rg = ranges[(std::size_t) k];
        params[(std::size_t) k] = (float) (std::clamp (raw[(std::size_t) k], rg.lo, rg.hi) - rg.lo) / (float) (rg.hi - rg.lo);
    }
}

std::array<int, 4> fromParams (const EffectParams& params) noexcept
{
    std::array<int, 4> raw {};
    for (int k = 0; k < convolverParamCount; ++k)
    {
        const auto& rg = ranges[(std::size_t) k];
        const float v = std::clamp (params[(std::size_t) k], 0.0f, 1.0f);
        raw[(std::size_t) k] = rg.lo + (int) std::lround ((double) v * (double) (rg.hi - rg.lo));
    }
    return raw;
}
} // namespace convolver

// ------------------------------------------------------------------------------------------------ the effect

namespace
{
class ConvolverEffect final : public Effect
{
public:
    explicit ConvolverEffect (const std::vector<std::uint8_t>& blob)
    {
        std::vector<std::uint8_t> fl, file;
        if (! convolver::unpackBlob (blob, fl, file))
            return;
        if (auto s = convolver::parseState (fl))
        {
            state = *s;
            if (auto a = convolver::decodeAudio (state.fromFile ? file : state.wav))
                ir = std::move (*a), valid = true;
        }
    }

    void prepare (double sampleRate) noexcept override
    {
        const int fs = (int) std::lround (sampleRate);
        if (valid && fs != rate)
        {
            rate = fs;
            engine.setRate (fs);
            if (auto k = convolver::buildKernel (state, ir, fs))
                engine.setKernel (*k), ready = true;
            else
                ready = false;
        }
        lastRaw = { -99999, 0, 0, 0 };
        engine.reset();
    }

    void reset() noexcept override { engine.reset(); }

    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        if (! ready)
            return;
        const auto raw = convolver::fromParams (params);
        if (raw != lastRaw)
        {
            const bool first = lastRaw[0] == -99999;
            engine.setParams (raw);
            if (first)
                engine.reset();
            lastRaw = raw;
        }
        engine.process (left, right, n);
    }

private:
    convolver::State state;
    convolver::Audio ir;
    convolver::Engine engine;
    bool valid = false, ready = false;
    int rate = 0;
    std::array<int, 4> lastRaw { -99999, 0, 0, 0 };
};

std::string dbText (float g)
{
    if (g <= 0.0f)
        return "-INFdB";
    char b[32];
    std::snprintf (b, sizeof b, "%.1fdB", 20.0 * std::log10 ((double) g));
    return b;
}
} // namespace

std::unique_ptr<Effect> makeConvolver() { return std::make_unique<ConvolverEffect> (std::vector<std::uint8_t> {}); }
std::unique_ptr<Effect> makeConvolver (const std::vector<std::uint8_t>& blob) { return std::make_unique<ConvolverEffect> (blob); }

std::optional<EffectSlot> convolverFromFl (const std::vector<std::uint8_t>& flState, const ConvolverFileReader& read)
{
    const auto s = convolver::parseState (flState);
    if (! s)
        return std::nullopt;
    std::vector<std::uint8_t> file;
    if (s->fromFile)
    {
        // %FLStudioFactoryData%\X → X, %FLStudioData%\X → Data\X (FL's own data folder); user data is not read
        std::string rel;
        const std::string fac = "%FLStudioFactoryData%\\", dat = "%FLStudioData%\\";
        if (s->path.compare (0, fac.size(), fac) == 0)
            rel = s->path.substr (fac.size());
        else if (s->path.compare (0, dat.size(), dat) == 0)
            rel = "Data\\" + s->path.substr (dat.size());
        if (rel.empty() || rel.find ('%') != std::string::npos || ! read)
            return std::nullopt;
        file = read (rel);
        if (file.empty())
            return std::nullopt;
    }
    const auto audio = convolver::decodeAudio (s->fromFile ? file : s->wav);
    if (! audio || audio->rate <= 0.0 || ! convolver::kernelSupported (*s))
        return std::nullopt;
    auto slot = makeEffectSlot (EffectKind::convolver);
    convolver::toParams (s->params, slot.params);
    slot.state = convolver::packBlob (flState, file);
    return slot;
}

std::string convolverParamText (int param, float value01)
{
    EffectParams p {};
    p[(std::size_t) std::clamp (param, 0, convolverParamCount - 1)] = value01;
    const auto raw = convolver::fromParams (p);
    switch (param)
    {
        case 0: return convolver::dryGain (raw[0]) == 1.0f ? "0.0dB" : dbText (convolver::dryGain (raw[0]));
        case 1: return dbText (convolver::wetGain (raw[1]));
        case 2:
        case 3:
        {
            const int v = raw[(std::size_t) param];
            if (v == 0)
                return "Original";
            char b[32];
            std::snprintf (b, sizeof b, "%d%% %s", (int) std::lround (std::abs (v) / 10.0), v > 0 ? "merged" : "separated");
            return b;
        }
        default: return {};
    }
}
} // namespace lynx
