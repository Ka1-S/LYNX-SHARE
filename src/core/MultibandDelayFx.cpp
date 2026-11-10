#include "core/StateHandoff.h"
#include "core/MultibandDelayFx.h"

#include "core/Effects.h"
#include "core/IldspMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) // x86 SSE2, or ARM64 through core/Simd.h (sse2neon)
#include "core/Simd.h"
#define LYNX_MBD_MXCSR 1
#endif

// The plug-in's float arithmetic is reproduced operation by operation (REA_rodada71 §3): no contraction into FMA,
// and the render runs with the plug-in's own SSE mode (flush to zero, denormals as zero, rounding toward zero).
#if defined(__clang__)
#pragma clang fp contract(off)
#pragma STDC FENV_ACCESS ON
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off", "rounding-math")
#endif

namespace lynx::mbdelay
{
namespace
{
// ------------------------------------------------------------------------------------------------ front end
struct Range
{
    int min, max;
};
// the twelve global controls (engine parameters 384…395), FL's control ranges
constexpr Range globalRange[numGlobals] = { { -6000, 6000 }, { 0, 1 }, { 0, 6000 }, { 0, 6000 }, { 0, 2 }, { 0, 1 },
                                            { 0, 7000 }, { -128, 128 }, { 0, 1 }, { 0, 1 }, { 1000, 100000 }, { 0, 2 } };
// a control's value when a state does not store it (FL's editor defaults)
constexpr int globalDefault[numGlobals] = { 0, 0, 0, 1500, 0, 0, 0, 128, 0, 0, 1000, 0 };

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}
void putI32 (std::vector<std::uint8_t>& d, std::int32_t v)
{
    std::uint8_t b[4];
    std::memcpy (b, &v, 4);
    d.insert (d.end(), b, b + 4);
}

// ------------------------------------------------------------------------------------------------ math
// the plug-in's CRT tanf for |x| ≤ π/4 (the high pass never leaves that range at audio rates)
float tanfCrt (float x) noexcept
{
    const double xd = (double) x, ax = std::fabs (xd);
    if (! std::isfinite (xd) || ax > 0.7853981633974483)
        return (float) std::tan (xd);
    if (ax < 7.450580596923828e-09)
        return x;
    if (ax < 0.0001220703125)
        return (float) (((xd * xd) * xd) * 0.3333333333333333 + xd);
    const double x2 = xd * xd;
    const double num = -0.017203248047148168 * x2 + 0.3852960712639954;
    const double den = (0.01844239256901656 * x2 + -0.5139650547885454) * x2 + 1.1558882143468838;
    return (float) (xd + (x2 * xd) * (num / den));
}

// the plug-in's CRT tanhf (single precision throughout). Its exp table 2^(j/32) as a head (cut to 9 fraction bits) and
// a tail (2^(j/32) cut to 33 fraction bits, minus the head, rounded to float), generated from that definition
constexpr float tanhHi[32] = {0x1.0000000000000p+0f, 0x1.0580000000000p+0f, 0x1.0b00000000000p+0f, 0x1.1100000000000p+0f, 0x1.1700000000000p+0f, 0x1.1d00000000000p+0f, 0x1.2380000000000p+0f, 0x1.2980000000000p+0f, 0x1.3000000000000p+0f, 0x1.3700000000000p+0f, 0x1.3d80000000000p+0f, 0x1.4480000000000p+0f, 0x1.4b80000000000p+0f, 0x1.5300000000000p+0f, 0x1.5a80000000000p+0f, 0x1.6200000000000p+0f, 0x1.6a00000000000p+0f, 0x1.7180000000000p+0f, 0x1.7a00000000000p+0f, 0x1.8200000000000p+0f, 0x1.8a80000000000p+0f, 0x1.9300000000000p+0f, 0x1.9c00000000000p+0f, 0x1.a500000000000p+0f, 0x1.ae80000000000p+0f, 0x1.b780000000000p+0f, 0x1.c180000000000p+0f, 0x1.cb00000000000p+0f, 0x1.d580000000000p+0f, 0x1.df80000000000p+0f, 0x1.ea00000000000p+0f, 0x1.f500000000000p+0f};
constexpr float tanhLo[32] = {0x0.0p+0f, 0x1.b0d3100000000p-12f, 0x1.561b3e0000000p-10f, 0x1.80e8080000000p-11f, 0x1.5c1e3c0000000p-11f, 0x1.21cc5a0000000p-10f, 0x1.e9b9c00000000p-14f, 0x1.a77d460000000p-10f, 0x1.bf828c0000000p-10f, 0x1.a737380000000p-12f, 0x1.a993040000000p-10f, 0x1.8218180000000p-10f, 0x1.f6b54c0000000p-10f, 0x1.0ad5a60000000p-10f, 0x1.83eea40000000p-11f, 0x1.1fac0e0000000p-10f, 0x1.3cccf00000000p-13f, 0x1.dd7a3a0000000p-10f, 0x1.1473e80000000p-12f, 0x1.6266520000000p-10f, 0x1.39508a0000000p-10f, 0x1.cdec320000000p-10f, 0x1.2460a80000000p-10f, 0x1.40ec8e0000000p-10f, 0x1.3f32b00000000p-13f, 0x1.ddbcbe0000000p-10f, 0x1.9bdd800000000p-12f, 0x1.c8373a0000000p-10f, 0x1.8dcf800000000p-16f, 0x1.25ccde0000000p-10f, 0x1.2be8a80000000p-10f, 0x1.d96da00000000p-14f};
float tanhfCrt (float x) noexcept
{
    const std::uint32_t bits = ildsp::fbits (x), ax = bits & 0x7fffffffu;
    if (ax < 0x39000000u || ax > 0x7f800000u)
        return x; // tiny or NaN
    const float sgn = 1.0f - (float) (ax != bits ? 1 : 0) * 2.0f;
    const float a = sgn * x;
    float r;
    if (a > 10.0f)
        r = 1.0f;
    else if (! (1.0f < a))
    {
        const float x2 = a * a, x3 = x2 * a;
        float num, den;
        if (0.899999976f > a)
        {
            num = ((x2 * 4.89163103e-05f - 0.00146283559f) * x2) - 0.281928062f;
            den = x2 * 0.342701793f + 0.845784187f;
        }
        else
        {
            num = ((x2 * 3.82753497e-05f - 0.00123256445f) * x2) - 0.240698591f;
            den = x2 * 0.292529076f + 0.722097397f;
        }
        r = (num * x3) / den + a;
    }
    else
    {
        float t = a + a;
        float y = t * 46.1662407f;
        y = y > 0.0f ? y + 0.5f : y - 0.5f;
        const int n = (int) y;
        const int j = n & 31, m = n & ~31;
        const float hi = tanhHi[j], lo = tanhLo[j];
        t = t - (float) n * 0.0216598511f;
        const float tl = (float) (-n) * 9.98318228e-07f;
        const int k = m / 32, k2 = k / 2;
        const float s1 = ildsp::ffrom ((std::uint32_t) (k2 + 127) << 23), s2 = ildsp::ffrom ((std::uint32_t) (k - k2 + 127) << 23);
        const float rr = tl + t;
        float q = rr * 0.166666672f + 0.5f;
        q = q * (rr * rr);
        q = q + tl;
        q = q + t;
        q = q * (lo + hi);
        q = q + lo;
        q = q + hi;
        q = q * s1;
        q = q * s2;
        q = q + 1.0f;
        r = 1.0f - 2.0f / q;
    }
    return r * sgn;
}

// the plug-in's double sine (a two-lane polynomial kernel; its sincos evaluates it at x and |x| + π/2)
double vsin (double x) noexcept
{
    using ildsp::dbits;
    using ildsp::dfrom;
    const std::uint64_t sgn = dbits (x) & 0x8000000000000000ull;
    const double ax = dfrom (dbits (x) & 0x7fffffffffffffffull);
    const double t = ax * 0.3183098861837907 + 6755399441055744.0;
    const double n = t - 6755399441055744.0;
    const std::uint64_t par = dbits (t) << 63;
    double r = ax - n * 3.141592502593994;
    r = r - n * 1.5099578831723193e-07;
    r = r - n * 1.078060505991553e-14;
    r = r - n * 6.564007085747001e-22;
    const double r2 = r * r;
    const double rp = dfrom (dbits (r) ^ par);
    double p = -7.373344533110665e-13 * r2 + 1.6048095404959637e-10;
    p = p * r2 + -2.505188027386288e-08;
    p = p * r2 + 2.7557316600552628e-06;
    p = p * r2 + -0.00019841269825056256;
    p = p * r2 + 0.008333333333285542;
    p = p * r2 + -0.16666666666666186;
    double q = r2 * p;
    q = q * rp;
    return dfrom (dbits (rp + q) ^ sgn);
}
void sincosD (double x, double& s, double& c) noexcept
{
    const double ax = std::fabs (x);
    if (ax >= 7.450580596923828e-09)
    {
        s = vsin (x);
        c = vsin (ax + 1.5707963267948966);
    }
    else
    {
        s = x;
        c = 1.0 - (ax * ax) * 0.5;
    }
}

float maxF (float a, float b) noexcept { return a > b ? a : b; } // maxss/maxps operand order
float minF (float a, float b) noexcept { return a < b ? a : b; }

constexpr double smooth01 = 0.09999999999999998; // the base of the plug-in's smoothing coefficients
constexpr int crossover[16] = { 31, 47, 71, 112, 172, 264, 413, 624, 958, 1502, 2267, 3611, 5458, 8391, 12663, 18994 };
constexpr float weightTime[8] = { 0.0f, 0.02f, 0.1f, 0.5f, 2.5f, 10.0f, 10.0f, 0.0f }; // weighted feedback, seconds

struct RenderMode
{
#ifdef LYNX_MBD_MXCSR
    unsigned saved;
    explicit RenderMode (unsigned bits) noexcept : saved (_mm_getcsr()) { _mm_setcsr ((saved & ~0x6000u) | bits); }
    ~RenderMode() { _mm_setcsr (saved); }
#else
    explicit RenderMode (unsigned) noexcept {}
#endif
    RenderMode (const RenderMode&) = delete;
    RenderMode& operator= (const RenderMode&) = delete;
};

// ------------------------------------------------------------------------------------------------ DSP blocks
// four-lane biquad (lanes: band A left/right, band B left/right)
struct Biquad4
{
    float b0[4] {}, b1[4] {}, b2[4] {}, a1[4] {}, a2[4] {};
    float y1[4] {}, y2[4] {}, x1[4] {}, x2[4] {};
    double fs = 44100.0;

    void tick (float* v) noexcept
    {
        for (int l = 0; l < 4; ++l)
        {
            const float x = v[l];
            const float y = (((x1[l] * b1[l] + x * b0[l]) + b2[l] * x2[l]) + y1[l] * a1[l]) + a2[l] * y2[l];
            x2[l] = x1[l], x1[l] = x, y2[l] = y1[l], y1[l] = y;
            v[l] = y;
        }
    }
    void identity (int pair) noexcept
    {
        for (int l = 2 * pair; l < 2 * pair + 2; ++l)
            b0[l] = 1.0f, b1[l] = b2[l] = a1[l] = a2[l] = 0.0f;
    }
    void set (int lane, float c0, float c1, float c2, float c3, float c4) noexcept
    {
        b0[lane] = c0, b1[lane] = c1, b2[lane] = c2, a1[lane] = c3, a2[lane] = c4;
    }
    // the plug-in's filter design (types 0 identity, 1 low pass, 2 high pass, 6 band pass, 9 all pass,
    // 10 / 11 first-order low / high pass) into one lane pair
    void design (int pair, int type, double f, double q, float gain) noexcept
    {
        double nb0 = 1.0, nb1 = 0.0, nb2 = 0.0, na0 = 1.0, na1 = 0.0, na2 = 0.0;
        if (type == 10 || type == 11)
        {
            const float t = (float) std::tan ((f * 3.141592653589793) / fs);
            const double td = (double) t;
            nb0 = type == 10 ? td : 1.0;
            nb1 = type == 10 ? td : -1.0;
            na1 = (double) (float) (t - 1.0f);
            na0 = (double) (float) (t + 1.0f);
            nb2 = na2 = 0.0;
        }
        else if (type != 0)
        {
            double s, c;
            sincosD ((f * 6.283185307179586) * (1.0 / fs), s, c);
            const double alpha = (0.5 / q) * s;
            const double m2c = c * -2.0;
            na0 = alpha + 1.0;
            na1 = m2c;
            na2 = 1.0 - alpha;
            switch (type)
            {
                case 1: nb0 = (1.0 - c) * 0.5, nb1 = 1.0 - c, nb2 = (1.0 - c) * 0.5; break;
                case 2: nb0 = (c + 1.0) * 0.5, nb1 = -(c + 1.0), nb2 = (c + 1.0) * 0.5; break;
                case 6: nb0 = s * 0.5, nb1 = 0.0, nb2 = s * -0.5; break;
                case 9: nb0 = 1.0 - alpha, nb1 = m2c, nb2 = alpha + 1.0; break;
                default: break;
            }
        }
        const double inv = 1.0 / na0;
        const float c0 = (float) (inv * nb0), c1 = (float) (inv * nb1), c2 = (float) (inv * nb2);
        const float c3 = -(float) (inv * na1), c4 = -(float) (inv * na2);
        for (int l = 2 * pair; l < 2 * pair + 2; ++l)
            set (l, gain * c0, gain * c1, gain * c2, c3, c4);
    }
};

// FFT convolver of the linear-phase crossover: 1024-tap FIR, blocks of 1024, FFT size 2048 (overlap-add). Our own
// double-precision radix-2 FFT (FL uses IPP's single-precision transforms; REA_rodada71 §4)
struct Convolver
{
    static constexpr int N = 2048, H = 1024;
    std::vector<double> kre, kim;          // spectrum of the zero-padded FIR
    std::vector<double> wre, wim;          // scratch
    std::vector<float> in[2], out[2];      // per channel: input block, output accumulator (N)
    int pos = 0;

    struct Tables
    {
        std::vector<double> c, s; // cos / sin (2πk/N), k < N/2
        std::vector<int> rev;
        Tables() : c ((std::size_t) N / 2), s ((std::size_t) N / 2), rev ((std::size_t) N)
        {
            for (int k = 0; k < N / 2; ++k)
                c[(std::size_t) k] = std::cos (2.0 * 3.141592653589793 * k / N), s[(std::size_t) k] = std::sin (2.0 * 3.141592653589793 * k / N);
            for (int i = 0, j = 0; i < N; ++i)
            {
                rev[(std::size_t) i] = j;
                int bit = N >> 1;
                for (; j & bit; bit >>= 1)
                    j ^= bit;
                j ^= bit;
            }
        }
    };
    static const Tables& tables()
    {
        static const Tables t;
        return t;
    }

    Convolver()
    {
        kre.assign (N, 0.0), kim.assign (N, 0.0), wre.assign (N, 0.0), wim.assign (N, 0.0);
        for (auto& v : in)
            v.assign (H, 0.0f);
        for (auto& v : out)
            v.assign (N, 0.0f);
    }
    void clear() noexcept
    {
        pos = 0;
        for (auto& v : in)
            std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : out)
            std::fill (v.begin(), v.end(), 0.0f);
    }
    // in place; inverse: conjugate twiddles, unscaled
    static void fft (double* re, double* im, bool inverse) noexcept
    {
        const auto& t = tables();
        for (int i = 0; i < N; ++i)
        {
            const int j = t.rev[(std::size_t) i];
            if (i < j)
                std::swap (re[i], re[j]), std::swap (im[i], im[j]);
        }
        const double sg = inverse ? 1.0 : -1.0;
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const double c = t.c[(std::size_t) (k * step)], s = sg * t.s[(std::size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const double xr = re[b] * c - im[b] * s, xi = re[b] * s + im[b] * c;
                    re[b] = re[a] - xr, im[b] = im[a] - xi;
                    re[a] = re[a] + xr, im[a] = im[a] + xi;
                }
        }
    }
    // the band's FIR: the brick-wall bin mask (1025 bins) as a zero-phase response centred on N/2, the middle 1024
    // taps under a sine window
    void design (const std::vector<float>& mask)
    {
        std::vector<double> re ((std::size_t) N), im ((std::size_t) N, 0.0);
        for (int k = 0; k < N; ++k)
        {
            const int kk = k <= N / 2 ? k : N - k;
            re[(std::size_t) k] = (double) mask[(std::size_t) kk] * ((k & 1) ? -1.0 : 1.0); // delay of N/2
        }
        fft (re.data(), im.data(), true);
        std::fill (kre.begin(), kre.end(), 0.0);
        std::fill (kim.begin(), kim.end(), 0.0);
        for (int i = 0; i < H; ++i)
        {
            const double tap = re[(std::size_t) (H / 2 + i)] / N;
            const double w = std::sin ((double) i * 3.141592653589793 / (double) (H - 1));
            kre[(std::size_t) i] = (double) (float) ((float) tap * (float) w);
        }
        fft (kre.data(), kim.data(), false);
    }
    void tick (float& l, float& r) noexcept
    {
        in[0][(std::size_t) pos] = l, in[1][(std::size_t) pos] = r;
        l = out[0][(std::size_t) pos], r = out[1][(std::size_t) pos];
        if (++pos == H)
        {
            pos = 0;
            // both channels in one complex transform (left real, right imaginary; the FIR is real)
            double* xr = wre.data();
            double* xi = wim.data();
            for (int i = 0; i < N; ++i)
            {
                xr[i] = i < H ? (double) in[0][(std::size_t) i] : 0.0;
                xi[i] = i < H ? (double) in[1][(std::size_t) i] : 0.0;
            }
            fft (xr, xi, false);
            for (int k = 0; k < N; ++k)
            {
                const double a = xr[k], b = xi[k], c = kre[(std::size_t) k], d = kim[(std::size_t) k];
                xr[k] = a * c - b * d;
                xi[k] = a * d + b * c;
            }
            fft (xr, xi, true);
            const float inv = 1.0f / (float) N;
            for (int c = 0; c < 2; ++c)
            {
                auto& o = out[c];
                const double* v = c == 0 ? xr : xi;
                std::memmove (o.data(), o.data() + H, (std::size_t) H * sizeof (float));
                std::fill (o.begin() + H, o.end(), 0.0f);
                for (int i = 0; i < N; ++i)
                    o[(std::size_t) i] = inv * (float) v[i] + o[(std::size_t) i];
            }
        }
    }
};

// per-band-pair delay line (four lanes per frame)
struct DelayLine
{
    float fb[4] {};
    float coef = 0.001f;
    float dA = 0, dB = 0, tA = 0, tB = 0;
    int tap[4] {}; // keep-pitch taps: A slot 0, A slot 1, B slot 0, B slot 1
    double maxSeconds = 1.0;
    std::vector<float> buf;
    int size = 1, w = 0, mask = 0;
};

// one band pair (bands 2k and 2k + 1)
struct BandPair
{
    Biquad4 f[10];
    Convolver conv[2];
    DelayLine dl;
    int mode = 0; // 0 steep (8 biquads), 1 linear phase, 2 gentle
    bool second = false; // gentle mode runs f[9] after f[8] (set for the top pair only)
    float gCur[4] { 1, 1, 1, 1 }, gTgt[4] { 1, 1, 1, 1 }, gCoef[4] { 0.5f, 0.5f, 0.5f, 0.5f };
};
} // namespace

// ------------------------------------------------------------------------------------------------ state
State::State()
{
    for (int b = 0; b < numBanks; ++b)
        for (int k = 0; k < numBands; ++k)
        {
            raw[(std::size_t) (b * 48 + k)] = 0;
            raw[(std::size_t) (b * 48 + 16 + k)] = 1 << 30;
            raw[(std::size_t) (b * 48 + 32 + k)] = 1 << 29;
        }
    for (int g = 0; g < numGlobals; ++g)
        raw[(std::size_t) (384 + g)] = globalDefault[g];
    for (auto& m : mode)
        m.fill (0);
}

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4)
        return std::nullopt;
    State s;
    std::size_t o = 0;
    const int ver = i32At (d, o);
    o += 4;
    if (ver < 1 || ver > 7)
        return std::nullopt;
    int count = 393;
    if (ver != 1)
    {
        if (d.size() < 8)
            return std::nullopt;
        count = i32At (d, o);
        o += 4;
        if (count < 1)
            return std::nullopt;
    }
    if (d.size() < o + 4 * (std::size_t) count + 8)
        return std::nullopt;
    for (int i = 0; i < count; ++i, o += 4)
        if (i < numEngineParams)
            s.raw[(std::size_t) i] = i32At (d, o);
    s.version = ver;
    s.targetSel = i32At (d, o);
    s.bank = i32At (d, o + 4);
    o += 8;
    if (ver >= 4)
    {
        if (d.size() < o + 1)
            return std::nullopt;
        s.flagA = d[o++];
        if (ver > 4 && o < d.size())
            s.flagB = d[o++];
        if (ver > 5 && o < d.size())
            s.flagC = d[o++];
        for (auto& m : s.mode)
            for (auto& v : m)
            {
                if (o + 4 > d.size())
                    return s;
                v = i32At (d, o);
                o += 4;
            }
    }
    return s;
}

std::vector<std::uint8_t> serialize (const State& s)
{
    std::vector<std::uint8_t> d;
    putI32 (d, 7);
    putI32 (d, numEngineParams);
    for (int v : s.raw)
        putI32 (d, v);
    putI32 (d, s.targetSel);
    putI32 (d, s.bank);
    d.push_back (s.flagA);
    d.push_back (s.flagB);
    d.push_back (s.flagC);
    for (const auto& m : s.mode)
        for (int v : m)
            putI32 (d, v);
    return d;
}

float engineValue (const State& s, int i) noexcept
{
    if (i < 0 || i >= numEngineParams)
        return 0.0f;
    const int raw = s.raw[(std::size_t) i];
    if (i < 384)
    {
        // after the load FL's editor re-sends the selected bank's cells, clamped to 0…2 in its own units (raw / 2^29)
        const int bank = i / 48, target = (i % 48) / 16, band = i % 16;
        if (bank == s.bank && s.mode[(std::size_t) band][(std::size_t) target] == 0)
        {
            float v = (float) ((double) raw / 536870912.0);
            v = 0.0f <= v ? (2.0f < v ? 2.0f : v) : 0.0f;
            const double r = std::nearbyint ((double) v * 536870912.0); // Delphi Round: ties to even
            return (float) (r / 1073741824.0);
        }
        return (float) ((double) raw / 1073741824.0);
    }
    const auto r = globalRange[i - 384];
    double v = (double) (raw - r.min) * (1.0 / (double) (r.max - r.min));
    v = std::clamp (v, 0.0, 1.0);
    return (float) v;
}

void toParams (const State& s, EffectParams& p) noexcept
{
    for (int g = 0; g < numGlobals; ++g)
    {
        const auto r = globalRange[g];
        p[(std::size_t) g] = std::clamp ((float) (s.raw[(std::size_t) (384 + g)] - r.min) / (float) (r.max - r.min), 0.0f, 1.0f);
    }
}

namespace
{
int rawOf (float v, Range r) noexcept
{
    return r.min + (int) std::lround (std::clamp (v, 0.0f, 1.0f) * (float) (r.max - r.min));
}
} // namespace

void applyParams (const EffectParams& p, State& s) noexcept
{
    for (int g = 0; g < numGlobals; ++g)
        s.raw[(std::size_t) (384 + g)] = rawOf (p[(std::size_t) g], globalRange[g]);
}

std::string paramText (int param, float v)
{
    char t[48];
    if (param < 0 || param >= numGlobals)
        return {};
    const int raw = rawOf (v, globalRange[param]);
    switch (param)
    {
        case 0: std::snprintf (t, sizeof (t), "%+.1f%%", raw / 60.0); return t;
        case 1:
        case 5:
        case 8:
        case 9: return raw != 0 ? "on" : "off";
        case 2: std::snprintf (t, sizeof (t), "%.1f%%", raw * 95.0 / 6000.0); return t;
        case 3: std::snprintf (t, sizeof (t), "%.0f ms", raw / 6.0); return t;
        case 4: return raw == 0 ? "gentle" : (raw == 1 ? "steep" : "linear phase");
        case 6: std::snprintf (t, sizeof (t), "bank %.2f", 1.0 + raw * 0.001); return t;
        case 7: std::snprintf (t, sizeof (t), "%d%% wet", (int) std::lround ((raw + 128) * 100.0 / 256.0)); return t;
        case 10:
        {
            const double x = (double) (raw - 1000) / 99000.0;
            std::snprintf (t, sizeof (t), "%.0f Hz", 10.0 * std::pow (10.0, 2.0 * x));
            return t;
        }
        case 11: return raw == 0 ? "comp" : (raw == 1 ? "saturate" : "clip");
        default: return {};
    }
}

// ------------------------------------------------------------------------------------------------ engine
struct Engine::Impl
{
    float p[numEngineParams + 64] {}; // morph reads one bank past the last one (always weighted by 0)
    std::uint8_t bandOn[numBands];
    double fs = 44100.0;
    BandPair bp[8];
    bool dirty = false;
    // output stage
    float mixS = 0, hpS = 0, smooth = 0, piOverFs = 0, hpFreq = 100.0f;
    float hpX1L = 0, hpY1L = 0, hpX1R = 0, hpY1R = 0;
    int satType = 0;
    float envL = 0, envR = 0, attL = 0.00520202f, relL = 0.000521f, attR = 0.00520202f, relR = 0.000521f;
    double attT = 0.01, relT = 0.1;
    // keep pitch
    bool keepPitch = false, fading = false, recomputed = false;
    float fadePos = 1.0f, fadeInc = 0.0f, fadeStep = 0.0f;
    int fadeSlot = 0;
    // dry path
    std::vector<float> dryL, dryR;
    int drySize = 0, dryPos = 0, latency = 0;

    Impl()
    {
        std::fill (std::begin (bandOn), std::end (bandOn), (std::uint8_t) 1);
        for (int b = 0; b < numBanks; ++b)
            for (int k = 0; k < numBands; ++k)
            {
                p[b * 48 + 32 + k] = 0.5f;
                p[b * 48 + 16 + k] = 1.0f;
                p[b * 48 + k] = (float) k * 0.0666666701f;
            }
        p[0x187] = 1.0f;
        p[0x180] = 0.5f;
        p[0x183] = 0.25f;
        attL = attR = ildsp::ffrom (0x3baaa523u);
        relL = relR = ildsp::ffrom (0x3a08d648u);
        bp[7].second = true;
        setSampleRate (44100.0); // the constructor's own set-up
    }

    static float ramp (double t, double rate)
    {
        if (! (t > 0.0))
            return 1.0f;
        return 1.0f - (float) std::pow (0.1, 1.0 / (t * rate));
    }

    void setParam (int i, float v) noexcept // FUN_1801d1ff0
    {
        if (i < 0 || i >= numEngineParams)
            return;
        p[i] = v;
        if (i < 0x183 || i == 0x188 || i == 0x189 || i == 0x186)
        {
            dirty = true;
            return;
        }
        switch (i)
        {
            case 0x18b: satType = v < 0.333000004f ? 0 : (v >= 0.666000009f ? 3 : 2); break;
            case 0x183:
            {
                const double c = v == 0.0f ? 1.0 : 1.0 - std::pow (smooth01, 1.0 / ((double) v * fs));
                for (auto& b : bp)
                    b.dl.coef = (float) c;
                break;
            }
            case 0x184:
            {
                const int m = v < 0.333332986f ? 2 : (v >= 0.666665971f ? 1 : 0);
                for (auto& b : bp)
                    b.mode = m;
                latency = m == 1 ? 0x600 : 0;
                break;
            }
            case 0x185:
            {
                keepPitch = 0.5f < v;
                fadePos = 1.0f;
                fading = false;
                if (keepPitch)
                {
                    fadeSlot = 1;
                    recomputed = false;
                    fadeInc = 0.0f;
                    for (auto& b : bp)
                    {
                        auto& dl = b.dl;
                        dl.dA = dl.tA, dl.dB = dl.tB;
                        dl.tap[0] = (int) dl.tA, dl.tap[2] = (int) dl.tB;
                        dl.dA = dl.tA, dl.dB = dl.tB;
                        dl.tap[1] = (int) dl.tA, dl.tap[3] = (int) dl.tB;
                    }
                }
                else
                    fadeSlot = 0;
                break;
            }
            default: break;
        }
    }

    void recompute() noexcept // FUN_1801d2250 (runs inside the render, rounding toward zero)
    {
        recomputed = true;
        const float fsMs = (float) (fs * 0.001);
        const double xScale = 0.5f > p[0x181] ? 1.0 : 0.10000000149011612;
        const bool harmonic = 0.5f <= p[0x188];
        const bool weighted = 0.1f <= p[0x189];
        float m = p[0x186] * 7.0f;
        const int bank = (int) m;
        const float f = m - (float) bank, g = 1.0f - f;
        const float fbPlain = p[0x182] * 0.949999988f;
        const float* P0 = p + bank * 48;
        const float* P1 = P0 + 48;
        for (int k = 0; k < numBands; ++k)
        {
            auto& b = bp[k >> 1];
            const int ln = (k & 1) * 2;
            if (bandOn[k] != 0)
            {
                const float vol = g * P0[16 + k] + f * P1[16 + k];
                float pan = g * P0[32 + k] + f * P1[32 + k];
                pan = minF (maxF (pan, 0.0f), 1.0f);
                const float sl = std::sqrt (1.0f - pan), sr = std::sqrt (pan);
                const float v2 = (vol * 0.327543139f) * vol;
                const float v3 = (vol * vol) * (vol * 1.39229143f);
                b.gTgt[ln] = ((v3 * sl) - (v2 * sl)) + (vol * sl) * 0.349678934f;
                b.gTgt[ln + 1] = ((v3 * sr) - (v2 * sr)) + (vol * sr) * 0.349678934f;
            }
            else
                b.gTgt[ln] = b.gTgt[ln + 1] = 0.0f;
            const float dly = f * P1[k] + g * P0[k];
            int samples;
            if (harmonic)
            {
                const int semis = (int) ((p[0x180] - 0.5f) * 96.0f);
                const float e = (float) semis * -0.0833333358f - (dly - 0.5f) * 4.0f;
                float x = ildsp::powf_ (2.0f, e) * 0.00191112794f;
                x = minF (x, 1.0f);
                samples = std::max (1, (int) ((double) x * fs));
            }
            else
            {
                const float s = p[0x180];
                float d = dly;
                const float sc = std::fabs ((s + s) - 1.0f);
                if (! (s >= 0.5f))
                    d = 1.0f - d;
                samples = std::max (1, (int) ((double) (sc * d) * fs * xScale));
            }
            if (ln == 0)
                b.dl.tA = (float) samples;
            else
                b.dl.tB = (float) samples;
            float fb = fbPlain;
            if (weighted)
            {
                const float x5 = fbPlain * 5.0f;
                const int ix = (int) x5;
                const float fr = x5 - (float) ix;
                float t = (1.0f - fr) * weightTime[ix] + fr * weightTime[ix + 1];
                t = t * (float) fs;
                const int den = std::max (samples, (int) fsMs);
                t = t / (float) den;
                fb = ildsp::powf_ (0.1f, 1.0f / t);
            }
            b.dl.fb[ln] = b.dl.fb[ln + 1] = fb;
        }
    }

    void designCrossover() // FUN_1801d28c0
    {
        // gentle: one filter per band (first-order low pass, band passes, first-order high pass)
        for (int k = 0; k < numBands; ++k)
        {
            auto& b = bp[k >> 1];
            const int pair = k & 1;
            b.f[9].identity (pair);
            const int type = k == 0 ? 10 : (k == 15 ? 11 : 6);
            const int freq = k == 0 ? 31 : (k == 15 ? 18994 : crossover[k]);
            b.f[8].design (pair, type, (double) freq, 0.5, k == 0 ? 1.0f : 0.5f);
        }
        // the top pair's second gentle filter: band 15 also runs through twice an all pass at 18994 Hz
        {
            auto& f9 = bp[7].f[9];
            f9.identity (0);
            double s, c;
            sincosD (119342.82172456906 / f9.fs, s, c);
            const double inv = 1.0 / (s + 1.0);
            const float c0 = (float) ((1.0 - s) * inv), c1 = (float) ((c * -2.0) * inv), c2 = (float) (inv * (s + 1.0));
            f9.set (2, c0 + c0, c1 + c1, c2 + c2, (float) ((c + c) * inv), (float) ((s - 1.0) * inv));
            f9.set (3, c0 + c0, c1 + c1, c2 + c2, (float) ((c + c) * inv), (float) ((s - 1.0) * inv));
        }
        // linear phase: brick-wall FIRs between the crossover frequencies
        for (int k = 0; k < numBands; ++k)
        {
            std::vector<float> mask (1025, 0.0f);
            const double lo = k == 0 ? 0.0 : (double) crossover[k - 1];
            const double hi = k < 15 ? (double) crossover[k] : fs * 0.5;
            if (k == 15)
                mask[1024] = 1.0f;
            const double scale = 2048.0 / fs;
            // (below 44.1 kHz the upper crossovers lie above Nyquist: the mask stops at its last bin, round 111)
            const long a = std::min ((long) (scale * lo), 1025L), z = std::min ((long) (scale * hi), 1025L);
            for (long i = a; i < z; ++i)
                mask[(std::size_t) i] = 1.0f;
            bp[k >> 1].conv[k & 1].design (mask);
        }
        // steep: band 0 = four low-pass sections at 31 Hz; bands 1…15 = four high-pass sections at the lower crossover
        // then four low-pass sections at the upper one (band 15: four all passes at 18994 Hz instead)
        {
            const float gain = 0.5f * 2.8283999f;
            const double qs[4] = { 0.92387972283855013, 0.38268341629224528, 0.92387972283855013, 0.38268341629224528 };
            for (int j = 0; j < 4; ++j)
            {
                auto& fl = bp[0].f[j];
                double s, c;
                sincosD (194.77874452256717 / fl.fs, s, c);
                const double k2 = s * qs[j];
                const double inv = 1.0 / (k2 + 1.0);
                const double omc = 1.0 - c;
                float c0, c1, c2;
                if (j == 0)
                {
                    c0 = (float) ((omc * 0.5) * inv) * gain;
                    c1 = (float) (inv * omc) * gain;
                    c2 = (float) ((omc * 0.5) * inv) * gain;
                }
                else
                {
                    c0 = (float) ((omc * 0.5) * inv);
                    c1 = (float) (inv * omc);
                    c2 = c0;
                }
                const float a1 = (float) ((c + c) * inv), a2 = (float) ((k2 - 1.0) * inv);
                fl.set (0, c0, c1, c2, a1, a2);
                fl.set (1, c0, c1, c2, a1, a2);
            }
            for (int j = 4; j < 7; ++j)
                bp[0].f[j].identity (0);
            bp[0].f[7].design (0, 0, 0.0, 0.0, 1.0f);
            const double qa = 0.54119598865509033, qb = 1.3065630197525024;
            for (int k = 1; k < numBands; ++k)
            {
                auto& b = bp[k >> 1];
                const int pair = k & 1;
                const double lo = crossover[k - 1], hi = crossover[k];
                b.f[0].design (pair, 2, lo, qa, gain);
                b.f[1].design (pair, 2, lo, qb, 1.0f);
                b.f[2].design (pair, 2, lo, qa, 1.0f);
                b.f[3].design (pair, 2, lo, qb, 1.0f);
                const int t = k == 15 ? 9 : 1;
                b.f[4].design (pair, t, hi, qa, 1.0f);
                b.f[5].design (pair, t, hi, qb, 1.0f);
                b.f[6].design (pair, t, hi, qa, 1.0f);
                b.f[7].design (pair, t, hi, qb, 1.0f);
            }
        }
    }

    void setSampleRate (double rate) // FUN_1801d3240
    {
        fs = rate;
        piOverFs = (float) (1.0 / rate) * 3.14159274f;
        hpFreq = (float) std::fmin ((double) hpFreq, (double) ((float) rate * 0.49000001f));
        const float gc = (float) (1.0 - std::pow (smooth01, 20.0 / rate));
        for (auto& b : bp)
        {
            auto& dl = b.dl;
            int want = (int) (rate * dl.maxSeconds + 0.5), size = 1;
            while (size < want)
                size *= 2;
            dl.size = size;
            dl.mask = size - 1;
            dl.buf.assign ((std::size_t) size * 4, 0.0f);
            dl.w = 0;
            for (int j = 0; j < 8; ++j) // the gentle filters f[8], f[9] keep the constructor's 44.1 kHz (as in FL)
                b.f[j].fs = rate;
            std::fill (std::begin (b.gCoef), std::end (b.gCoef), gc);
        }
        for (int i = 0; i < numEngineParams; ++i)
            setParam (i, p[i]);
        attL = attR = ramp (attT, rate);
        relL = relR = ramp (relT, rate);
        designCrossover();
        fadeStep = 1.0f / (float) (rate * 0.10000000000000001);
        drySize = 0x60a;
        smooth = (float) (1.0 - std::pow (smooth01, 200.0 / rate));
        dryL.assign ((std::size_t) drySize, 0.0f);
        dryR.assign ((std::size_t) drySize, 0.0f);
        if (dryPos >= drySize)
            dryPos = 0;
    }

    void suspend() noexcept // FUN_1801d6e00
    {
        hpX1L = hpY1L = hpX1R = hpY1R = 0.0f;
        for (auto& b : bp)
        {
            std::fill (b.dl.buf.begin(), b.dl.buf.end(), 0.0f);
            for (auto& c : b.conv)
                c.clear();
        }
    }

    void processBand (BandPair& b, float* v) noexcept // FUN_1801d3f90
    {
        if (b.mode == 1)
        {
            b.conv[0].tick (v[0], v[1]);
            b.conv[1].tick (v[2], v[3]);
        }
        else if (b.mode == 0)
            for (int j = 0; j < 8; ++j)
                b.f[j].tick (v);
        else
        {
            b.f[8].tick (v);
            if (b.second)
                b.f[9].tick (v);
        }
        auto& dl = b.dl;
        float r[4];
        if (keepPitch)
        {
            const float a = fadePos, bb = 1.0f - fadePos;
            auto at = [&] (int tap, int lane) { return dl.buf[(std::size_t) ((((dl.w - tap) & dl.mask)) * 4 + lane)]; };
            if (a == 1.0f)
                r[0] = at (dl.tap[0], 0), r[1] = at (dl.tap[0], 1), r[2] = at (dl.tap[2], 2), r[3] = at (dl.tap[2], 3);
            else if (bb == 1.0f)
                r[0] = at (dl.tap[1], 0), r[1] = at (dl.tap[1], 1), r[2] = at (dl.tap[3], 2), r[3] = at (dl.tap[3], 3);
            else
            {
                r[0] = bb * at (dl.tap[1], 0) + a * at (dl.tap[0], 0);
                r[1] = bb * at (dl.tap[1], 1) + a * at (dl.tap[0], 1);
                r[2] = bb * at (dl.tap[3], 2) + a * at (dl.tap[2], 2);
                r[3] = bb * at (dl.tap[3], 3) + a * at (dl.tap[2], 3);
            }
        }
        else
        {
            dl.dA = (dl.tA - dl.dA) * dl.coef + dl.dA;
            dl.dB = (dl.tB - dl.dB) * dl.coef + dl.dB;
            const int iA = (int) dl.dA, iB = (int) dl.dB;
            const float fA = dl.dA - (float) iA, fB = dl.dB - (float) iB;
            const int jA = (dl.w - iA) & dl.mask, jB = (dl.w - iB) & dl.mask;
            const int kA = (jA - 1) & dl.mask, kB = (jB - 1) & dl.mask;
            const float* bA0 = &dl.buf[(std::size_t) jA * 4];
            const float* bA1 = &dl.buf[(std::size_t) kA * 4];
            const float* bB0 = &dl.buf[(std::size_t) jB * 4];
            const float* bB1 = &dl.buf[(std::size_t) kB * 4];
            const float gA = 1.0f - fA, gB = 1.0f - fB;
            r[0] = gA * bA0[0] + fA * bA1[0];
            r[1] = fA * bA1[1] + gA * bA0[1];
            r[2] = fB * bB1[2] + gB * bB0[2];
            r[3] = fB * bB1[3] + gB * bB0[3];
        }
        float* wp = &dl.buf[(std::size_t) dl.w * 4];
        for (int l = 0; l < 4; ++l)
            wp[l] = minF (maxF (r[l] * dl.fb[l], -1.0f), 1.0f) + v[l];
        dl.w = (dl.w + 1) & dl.mask;
        for (int l = 0; l < 4; ++l)
        {
            v[l] = r[l] * b.gCur[l];
            b.gCur[l] = (b.gTgt[l] - b.gCur[l]) * b.gCoef[l] + b.gCur[l];
        }
    }

    void process (float* L, float* R, int n) noexcept // FUN_1801d1750
    {
        RenderMode mode (0xE040u);
        if (dirty)
        {
            recompute();
            dirty = false;
        }
        if (keepPitch && ! fading && recomputed)
        {
            fading = true;
            recomputed = false;
            for (auto& b : bp)
            {
                auto& dl = b.dl;
                dl.dA = dl.tA, dl.dB = dl.tB;
                dl.tap[fadeSlot] = (int) dl.tA;
                dl.tap[2 + fadeSlot] = (int) dl.tB;
            }
            if (fadeSlot == 0)
            {
                fadeInc = fadeStep;
                fadeSlot = 1;
            }
            else
            {
                fadeSlot = 0;
                fadeInc = -fadeStep;
            }
        }
        const bool fusedPow = true;
        (void) fusedPow;
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            float s[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (auto& b : bp)
            {
                float v[4] = { inL, inR, inL, inR };
                processBand (b, v);
                for (int l = 0; l < 4; ++l)
                    s[l] = s[l] + v[l];
            }
            if (fading)
            {
                const float x = fadeInc + fadePos;
                fadePos = x;
                if (! (x >= 0.0f))
                    fadePos = 0.0f, fading = false;
                else if (x > 1.0f)
                    fadePos = 1.0f, fading = false;
            }
            hpS = (p[0x18a] - hpS) * smooth + hpS;
            const float wl = s[0] + s[2], wr = s[1] + s[3];
            const float fq = ildsp::powf_ (10.0f, hpS * 2.0f) * 10.0f;
            hpFreq = (float) std::fmin ((double) fq, (double) ((float) fs * 0.49000001f));
            const float t = tanfCrt (fq * piOverFs);
            const float a0 = 1.0f / (t + 1.0f);
            const float a1 = -a0;
            const float c1 = -((t - 1.0f) * a0);
            float yl = ((a0 * wl) + a1 * hpX1L) + c1 * hpY1L;
            float yr = ((a0 * wr) + a1 * hpX1R) + c1 * hpY1R;
            hpX1L = wl, hpX1R = wr, hpY1L = yl, hpY1R = yr;
            if (satType == 0)
            {
                float ax = std::fabs (yl);
                envL = (ax - envL) * (ax < envL ? relL : attL) + envL;
                if (envL > 1.0f)
                    yl = yl / envL;
                ax = std::fabs (yr);
                envR = (ax - envR) * (ax < envR ? relR : attR) + envR;
                if (envR > 1.0f)
                    yr = yr / envR;
            }
            else if (satType == 2)
            {
                yl = tanhfCrt (yl);
                yr = tanhfCrt (yr);
            }
            else if (satType == 3)
            {
                yl = yl >= -1.0f ? minF (yl, 1.0f) : -1.0f;
                yr = yr >= -1.0f ? minF (yr, 1.0f) : -1.0f;
            }
            dryL[(std::size_t) dryPos] = inL;
            dryR[(std::size_t) dryPos] = inR;
            int j = dryPos - latency;
            while (j < 0)
                j += drySize;
            const float dl = dryL[(std::size_t) j], dr = dryR[(std::size_t) j];
            dryPos = dryPos + 1 >= drySize ? 0 : dryPos + 1;
            mixS = (p[0x187] - mixS) * smooth + mixS;
            const float dm = 1.0f - mixS;
            L[i] = mixS * yl + dm * dl;
            R[i] = yr * mixS + dm * dr;
        }
    }
};

Engine::Engine() : d (std::make_unique<Impl>()) {}
Engine::~Engine() = default;
void Engine::setSampleRate (double sampleRate) { d->setSampleRate (sampleRate); }
void Engine::setParameter (int index, float value) noexcept { d->setParam (index, value); }
void Engine::load (const State& s) noexcept
{
    for (int i = 0; i < numEngineParams; ++i)
        d->setParam (i, engineValue (s, i));
}
void Engine::suspend() noexcept { d->suspend(); }
void Engine::process (float* left, float* right, int n) noexcept
{
    if (n > 0)
        d->process (left, right, n);
}
int Engine::latencySamples() const noexcept { return d->latency; }

// ------------------------------------------------------------------------------------------------ LYNX effect
namespace
{
class MultibandDelayEffect final : public Effect
{
public:
    // `initial` (may be null): the slot's parameters, applied over the state as process() does on its first block, so
    // the mode (the linear-phase latency) is known before it (issue #113)
    MultibandDelayEffect (const std::vector<std::uint8_t>& flState, const EffectParams* initial)
    {
        if (auto s = parse (flState))
            state = *s;
        if (initial != nullptr)
            applyParams (*initial, state);
        engine.load (state);
    }

    void prepare (double sampleRate) noexcept override
    {
        engine.load (state);
        engine.setSampleRate (sampleRate);
        engine.suspend();
    }

    void reset() noexcept override { engine.suspend(); }

    // the editor changed band cells or the selected bank: the running engine takes the new cell values
    bool acceptState (const std::vector<std::uint8_t>& bytes) noexcept override
    {
        const auto s = parse (bytes);
        return s && incoming.offer (*s);
    }

    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (incoming.take (next))
        {
            for (int i = 0; i < 384; ++i)
                if (const auto v = engineValue (next, i); v != engineValue (state, i))
                    engine.setParameter (i, v);
            for (int i = 0; i < 384; ++i)
                state.raw[(std::size_t) i] = next.raw[(std::size_t) i];
            state.targetSel = next.targetSel, state.bank = next.bank, state.mode = next.mode;
        }
        if (! haveParams || p != lastParams)
        {
            lastParams = p, haveParams = true;
            auto s = state;
            applyParams (p, s);
            for (int g = 0; g < numGlobals; ++g)
                if (s.raw[(std::size_t) (384 + g)] != state.raw[(std::size_t) (384 + g)])
                {
                    state.raw[(std::size_t) (384 + g)] = s.raw[(std::size_t) (384 + g)];
                    engine.setParameter (384 + g, engineValue (state, 384 + g));
                }
        }
        engine.process (left, right, n);
    }

    int latencySamples() const noexcept override { return engine.latencySamples(); }

private:
    State state, next;
    StateHandoff<State> incoming;
    bool haveParams = false;
    EffectParams lastParams {};
    Engine engine;
};
} // namespace

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& flState, const EffectParams* params)
{
    return std::make_unique<MultibandDelayEffect> (flState, params);
}
} // namespace lynx::mbdelay
