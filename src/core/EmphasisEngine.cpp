#include "core/EmphasisEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

#include "core/Denormals.h"
#include "core/IldspMath.h"

// Every expression keeps the library's operation order: a fused multiply-add would change the results
#pragma GCC optimize ("fp-contract=off")

// docs/analise/REA_rodada167f.md §1.2. Field names follow the spec; "max0" is the library's clamp x <= 0 ? 0 : x and
// "f2u64" its float → unsigned 64-bit conversion (truncation; the 2^63 split of the compiler).

namespace lynx::emphasis
{
using ildsp::expD_;
using ildsp::expf_;
using ildsp::fmaxf_;
using ildsp::logf_;
using ildsp::powf_;

namespace
{
constexpr float tiny = 0x1.ff19e2p-24f;           // 1.19e-7
constexpr double m2Pi = -0x1.921fb54442d18p+2;    // −2π
constexpr double twoPi = 0x1.921fb54442d18p+2;
constexpr double kLp20 = -0x1.f6a7a2955385ep+6;   // −2π · 20
constexpr double kEs50 = -0x1.f6a7a217a99d5p+6;   // −2π / 0.05 as the library rounds it
constexpr double k5Hz = -0x1.f6a7a2955385ep+4;    // −2π · 5
constexpr double kLevel = -0x1.921fb54442d18p-1;  // −2π · 0.125
constexpr double kSplit = -0x1.f6a7a2955385ep+10; // −2π · 320
constexpr double kMeter = -0x1.4f1a6bdd82723p+4;  // −2π · 3.333
constexpr double kFade = -0x1.3a28c59d5433bp+9;   // −2π · 100
constexpr double k8ms = -0x1.88b2f5cbbaa4cp+9;    // −2π / 0.008
constexpr double k1000 = -0x1.88b2f704a9409p+12;  // −2π · 1000
constexpr double kEs8 = 0x1.4e29762573604p+4;     // 8-stage smoother time scale (2π / √(2^(1/8) − 1))

// the hardness tables: release-envelope time scale by envelope order, M/S limiter time, static attack multiplier
constexpr float orderScale[4] = { 1.0f, 0x1.a8279ap-2f, 0x1.0a28bep-2f, 0x1.837f06p-3f };
constexpr float msTime[10] = { 0x1.0624dep-5f, 0x1.0624dep-6f, 0x1.0624dep-7f, 0x1.0624dep-8f, 0x1.0624dep-9f,
                               0x1.0624dep-10f, 0x1.0624dep-11f, 0x1.0624dep-12f, 0x1.0624dep-13f, 0.0f };
constexpr float attackMul[10] = { 3.0f, 2.0f, 1.5f, 1.25f, 1.125f, 1.0625f, 1.03125f, 0x1.03ffacp+0f, 0x1.01ffd6p+0f,
                                  0x1.01003ep+0f };

// the knee / emphasis constants (in the library they live in the object)
constexpr float kLn10 = 0x1.26bb1cp+1f, kNinth = -0x1.c71c72p-4f, kTen = 10.0f, kKnee = 0x1.ab0a8ap-2f;

// halfband all-pass coefficients (12, 6 and 4 coefficients: chain A, chain B)
constexpr float hbA12[6] = { 0x1.12d60ep-6f, 0x1.1cb74cp-3f, 0x1.547b2cp-2f, 0x1.13485p-1f, 0x1.715dcp-1f, 0x1.c3addcp-1f };
constexpr float hbB12[6] = { 0x1.0a4b46p-4f, 0x1.d8f7cp-3f, 0x1.bef858p-2f, 0x1.441376p-1f, 0x1.9b892ap-1f, 0x1.eb8106p-1f };
constexpr float hbA6[3] = { 0x1.edf4ccp-5f, 0x1.a67e2ep-2f, 0x1.8ba162p-1f };
constexpr float hbB6[3] = { 0x1.ba4f3cp-3f, 0x1.356e7ep-1f, 0x1.d9079ap-1f };
constexpr float hbA4[2] = { 0x1.472204p-4f, 0x1.173898p-1f };
constexpr float hbB4[2] = { 0x1.22a428p-2f, 0x1.ab380ap-1f };

inline float max0 (float x) noexcept { return x <= 0.0f ? 0.0f : x; }

// cvttss2si (64-bit): truncation, 0x8000000000000000 for NaN and out of range
inline std::int64_t cvtt64 (float x) noexcept
{
    if (! (x >= -0x1p63f && x < 0x1p63f))
        return INT64_MIN;
    return (std::int64_t) x;
}

inline std::uint64_t f2u64 (float x) noexcept
{
    std::uint64_t add = 0;
    if (0x1p63f <= x)
    {
        x -= 0x1p63f;
        if (x < 0x1p63f)
            add = 0x8000000000000000ull;
    }
    return (std::uint64_t) cvtt64 (x) + add;
}

inline float u64f (std::uint64_t u) noexcept { return (float) u; }

// floor through a 32-bit truncation, like the library (values out of the int range stay as they are)
inline float floorTrick (float x) noexcept
{
    std::int32_t i = (x >= -0x1p31f && x < 0x1p31f) ? (std::int32_t) x : INT32_MIN;
    if (i != INT32_MIN && (float) i != x)
        return (float) (i - (std::signbit (x) ? 1 : 0));
    return i == INT32_MIN ? x : (float) i;
}

// --- sin to double precision, correctly rounded in practice (double-double evaluation) for the FIR design ---
struct DD
{
    double hi, lo;
};
inline DD quickTwoSum (double a, double b) noexcept
{
    const double s = a + b;
    return { s, b - (s - a) };
}
inline DD twoSum (double a, double b) noexcept
{
    const double s = a + b, bb = s - a;
    return { s, (a - (s - bb)) + (b - bb) };
}
inline DD twoProd (double a, double b) noexcept
{
    const double p = a * b;
    return { p, std::fma (a, b, -p) };
}
inline DD add (DD a, DD b) noexcept
{
    DD s = twoSum (a.hi, b.hi);
    const DD t = twoSum (a.lo, b.lo);
    s.lo += t.hi;
    s = quickTwoSum (s.hi, s.lo);
    s.lo += t.lo;
    return quickTwoSum (s.hi, s.lo);
}
inline DD mul (DD a, DD b) noexcept
{
    DD p = twoProd (a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return quickTwoSum (p.hi, p.lo);
}
inline DD divD (DD a, double b) noexcept
{
    const double q = a.hi / b;
    const DD p = twoProd (q, b);
    const double r = (((a.hi - p.hi) - p.lo) + a.lo) / b;
    return quickTwoSum (q, r);
}

double sinAccurate (double a) noexcept
{
    if (a == 0.0 || ! std::isfinite (a))
        return std::sin (a);
    // π/2 in three parts
    constexpr double p1 = 3.141592653589793116 / 2, p2 = 1.224646799147353207e-16 / 2, p3 = -2.994769809718339666e-33 / 2;
    const double k = std::nearbyint (a * (2.0 / 3.141592653589793116));
    DD r { a, 0.0 };
    const DD t1 = twoProd (k, p1), t2 = twoProd (k, p2);
    r = add (r, { -t1.hi, -t1.lo });
    r = add (r, { -t2.hi, -t2.lo });
    r = add (r, { -(k * p3), 0.0 });
    const DD r2 = mul (r, r);
    const long long q = (((long long) k % 4) + 4) % 4;
    DD sum, term;
    double n;
    if (q == 0 || q == 2)
    {
        sum = r;
        term = r;
        n = 1.0;
    }
    else
    {
        sum = { 1.0, 0.0 };
        term = { 1.0, 0.0 };
        n = 0.0;
    }
    for (int i = 0; i < 30; ++i)
    {
        term = mul (term, r2);
        term = divD (term, -(n + 1.0) * (n + 2.0));
        n += 2.0;
        sum = add (sum, term);
        if (std::fabs (term.hi) < 1e-40 * std::fabs (sum.hi))
            break;
    }
    return (q >= 2) ? -sum.hi : sum.hi;
}
} // namespace

namespace detail
{
// --- one-pole filters ---
void Lp1::setRate (float r) noexcept
{
    sr = r;
    isr = 1.0f / r;
    c = (float) expD_ ((double) isr * kLp20);
}
void Lp1::prepare (float r) noexcept
{
    setRate (r);
    c = (float) expD_ (((double) cut * m2Pi) * (double) isr);
}
void Lp1::setCoef (float hz, double k) noexcept
{
    cut = hz;
    c = (float) expD_ ((double) isr * k);
}
void Lp1::process (const float* in, float* out, int n) noexcept
{
    float y = s;
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        y = (y - x) * c + x;
        out[i] = y;
    }
    s = y;
}

void Hp1::setRate (float r) noexcept
{
    sr = r;
    isr = 1.0f / r;
    c = (float) expD_ ((double) isr * kLp20);
}
void Hp1::prepare (float r) noexcept
{
    setRate (r);
    c = (float) expD_ (((double) cut * m2Pi) * (double) isr);
}
void Hp1::process (const float* in, float* out, int n) noexcept
{
    float y = s;
    const int n4 = n & ~3;
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        if (i < n4 && (i & 3) != 3)
        {
            y = (y - x) * c + x;
            out[i] = x - y;
        }
        else
        {
            const float t = (y - x) * c;
            y = x + t;
            out[i] = -t;
        }
    }
    s = y;
}

// --- smoothers ---
void Es1::setRate (float r) noexcept
{
    sr = r;
    isr = 1.0f / r;
    const double d = (double) isr * m2Pi;
    c44 = (float) expD_ (d / (double) att);
    c40 = (float) expD_ (d / (double) rel);
}
void Es1::process (const float* in, float* out, int n) noexcept
{
    float y = s;
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        y = (y - x) * (y < x ? c44 : c40) + x;
        out[i] = y;
    }
    s = y;
}

void EsN::setRate (float r) noexcept
{
    sr = r;
    isr = 1.0f / r;
    factor = (float) (((double) isr * twoPi) * (double) k);
    c44 = expf_ (-(factor / att));
    c40 = expf_ (-(factor / rel));
}
void EsN::setOrder (std::uint64_t n) noexcept
{
    order = n;
    k = 1.0f / std::sqrt (powf_ (2.0f, 1.0f / (float) n) - 1.0f);
    factor = (float) (((double) isr * twoPi) * (double) k);
    c44 = expf_ (-(factor / att));
    c40 = expf_ (-(factor / rel));
}
void EsN::prepare (float r) noexcept
{
    setRate (r);
    setOrder (1);
    c44 = expf_ (-(factor / att));
    c40 = expf_ (-(factor / rel));
}

void Es8::setRate (float r) noexcept
{
    sr = r;
    isr = 1.0f / r;
    factor = (float) ((double) isr * kEs8);
    c44 = expf_ (-(factor / att));
    c40 = expf_ (-(factor / rel));
}
void Es8::process (const float* in, float* out, int n) noexcept
{
    float s[8]; // the stages in locals (in / out may alias them otherwise)
    for (int j = 0; j < 8; ++j)
        s[j] = st[(std::size_t) j];
    const float ca = c44, cr = c40;
    for (int i = 0; i < n; ++i)
    {
        float x = in[i];
        for (int j = 0; j < 8; ++j)
        {
            x = (s[j] - x) * (s[j] < x ? ca : cr) + x;
            s[j] = x;
        }
        out[i] = x;
    }
    for (int j = 0; j < 8; ++j)
        st[(std::size_t) j] = s[j];
}

// --- peak holds ---
void Ph1::setRate (float r) noexcept
{
    sr = r;
    hold = max0 (hold);
    len = hold * r;
}

void Ph8::setRate (float r) noexcept
{
    sr = r;
    hold = max0 (hold);
    len = (hold * 0.125f) * r;
}
void Ph8::process (const float* in, float* out, int n) noexcept
{
    // counters stay far below 2^63, so the signed conversion equals the library's unsigned one
    float vv[8];
    std::int64_t cc[8];
    for (std::size_t j = 0; j < 8; ++j)
    {
        vv[j] = v[j];
        cc[j] = (std::int64_t) cnt[j];
    }
    const float l = len;
    for (int i = 0; i < n; ++i)
    {
        float x = in[i];
        for (int j = 0; j < 8; ++j)
        {
            if (vv[j] <= x || l <= (float) cc[j])
                cc[j] = 0;
            else
            {
                ++cc[j];
                x = vv[j];
            }
            vv[j] = x;
        }
        out[i] = x;
    }
    for (std::size_t j = 0; j < 8; ++j)
    {
        v[j] = vv[j];
        cnt[j] = (std::uint64_t) cc[j];
    }
}
void Ph8::reset() noexcept
{
    cnt.fill (0);
    v.fill (0.0f);
}

// --- static gain computers (peak hold → smoother → 1/x) ---
template <typename Smoother>
void GrStatic<Smoother>::setRate (float r) noexcept
{
    sr = r;
    es.setRate (r);
    ph.setRate (r);
    const float h = max0 (hold);
    es.att = att;
    es.rel = rel;
    if constexpr (std::is_same_v<Smoother, Es1>)
    {
        const double d = (double) es.isr * m2Pi;
        es.c44 = (float) expD_ (d / (double) att);
        es.c40 = (float) expD_ (d / (double) rel);
    }
    else
    {
        es.c44 = expf_ (-(es.factor / att));
        es.c40 = expf_ (-(es.factor / rel));
    }
    ph.hold = h;
    ph.len = (h * 0.125f) * ph.sr;
    act.setRate (r);
    act.len = act.sr * 0x1.99999ap-6f; // 0.025 s
    act.hold = 0x1.99999ap-6f;
}

template <typename Smoother>
void GrStatic<Smoother>::process (const float* in, float* out, int n) noexcept
{
    // share of the block where a 25 ms peak hold of the input exceeds +0.016 dB
    float count = 0.0f, v = act.v;
    std::uint64_t cnt = act.cnt;
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        if (v <= x || act.len <= u64f (cnt))
        {
            cnt = 0;
            v = x;
        }
        else
            ++cnt;
        count = (float) (0x1.007a62p+0f <= v) + count;
    }
    act.v = v;
    act.cnt = cnt;
    activity = count / (float) n;

    ph.process (in, out, n);
    const int n4 = n & ~3;
    for (int i = 0; i < n; ++i)
        out[i] = i < n4 ? (out[i] > 1.0f ? out[i] : 1.0f) : (out[i] <= 1.0f ? 1.0f : out[i]);
    es.process (out, out, n);
    for (int i = 0; i < n; ++i)
    {
        const float g = 1.0f / out[i];
        out[i] = 1.0f <= g ? 1.0f : g;
    }
    last = out[n - 1];
}

template struct GrStatic<Es8>;
template struct GrStatic<Es1>;

// --- adaptive-release gain computer ---
void GrAdaptive::setRate (float r) noexcept
{
    sr = r;
    isr = 1.0f / r;
    ph.setRate (r);
    es.setRate (r);
    es1.setRate (r);
    lp.setRate (r);
    es.att = att;
    es.c44 = expf_ (-(es.factor / att));
    es1.att = att;
    es1.c44 = (float) expD_ (((double) es1.isr * m2Pi) / (double) att);
    lp.cut = 20.0f;
    const float h = max0 (hold);
    rel1 = w * 0x1.333334p-3f; // 0.15
    ph.hold = h;
    ph.len = (h * 0.125f) * ph.sr;
    rel2 = (rel1 * 4.0f) * w;
    lp.c = (float) expD_ ((double) lp.isr * kLp20);
}

void GrAdaptive::prepare (float r) noexcept
{
    // the library: rate, sub-module prepares, order 1, then the rate-dependent fields again
    setRate (r);
    es.setOrder (1);
    setRate (r);
}

void GrAdaptive::process (float* buf, int n) noexcept
{
    ph.process (buf, buf, n);
    constexpr float dB = 0x1.15f2cep+3f; // 20 / ln 10
    if (es.order == 1)
    {
        float gg = g, ga = a, gb = b;
        for (int i = 0; i < n; ++i)
        {
            const float aPrev = ga;
            float rt = 0x1.47ae14p-8f; // 5 ms (instant recovery)
            if (! flag)
            {
                const float g1 = gg <= tiny ? tiny : gg;
                const float e = scale * buf[i];
                float s = (lp.s - e) * lp.c + e;
                lp.s = s;
                if (s <= tiny)
                    s = tiny;
                float q = s / g1;
                if (q <= tiny)
                    q = tiny;
                const float l1 = logf_ (q) * dB;
                const float lim = 6.0f - thr;
                const float l2 = logf_ (g1) * dB;
                const float k2 = thr * -0x1.51eb86p-2f;
                float factor = 1.0f;
                if (l2 < k2 || l1 < thr * -0.5f)
                    factor = std::sqrt (std::fabs (l2 - k2)) * mult + 1.0f;
                const bool fast = lim < l1 && gb < aPrev;
                rt = (fast ? rel1 : rel2) * factor;
            }
            float x = buf[i];
            if (x <= 1.0f)
                x = 1.0f;
            buf[i] = x;
            es1.rel = rt;
            es1.c40 = (float) expD_ (((double) es1.isr * m2Pi) / (double) rt);
            float s1 = (es1.s - x) * (es1.s < x ? es1.c44 : es1.c40) + x;
            es1.s = s1;
            if (s1 <= tiny)
                s1 = tiny;
            float gain = 1.0f / s1;
            if (1.0f <= gain)
                gain = 1.0f;
            buf[i] = gain;
            gg = gain;
            ga = gain;
            gb = aPrev;
        }
        g = gg;
        a = ga;
        b = gb;
        return;
    }

    float gg = g, ga = a, gb = b;
    const std::size_t ord = (std::size_t) std::min<std::uint64_t> (es.order, 8);
    for (int i = 0; i < n; ++i)
    {
        const float aPrev = ga;
        float rt = 0x1.47ae14p-8f;
        if (! flag)
        {
            const float e = scale * buf[i];
            lp.s = (lp.s - e) * lp.c + e;
            const float num = fmaxf_ (tiny, lp.s), den = fmaxf_ (tiny, gg);
            float q = num / den;
            if (q <= tiny)
                q = tiny;
            const float l1 = logf_ (q) * dB;
            const float g1 = gg <= tiny ? tiny : gg;
            const float lim = 6.0f - thr;
            const float l2 = logf_ (g1) * dB;
            const float k2 = thr * -0x1.51eb86p-2f;
            float factor = 1.0f;
            if (l2 < k2 || l1 < thr * -0.5f)
                factor = std::sqrt (std::fabs (l2 - k2)) * mult + 1.0f;
            const bool fast = lim < l1 && gb < aPrev;
            rt = (fast ? rel1 : rel2) * factor;
        }
        float x = buf[i];
        if (x <= 1.0f)
            x = 1.0f;
        buf[i] = x;
        es.rel = rt;
        es.c40 = expf_ (-(es.factor / rt));
        for (std::size_t j = 0; j < ord; ++j)
        {
            x = (es.st[j] - x) * (es.st[j] < x ? es.c44 : es.c40) + x;
            es.st[j] = x;
        }
        float last = es.st[ord - 1];
        if (last <= tiny)
            last = tiny;
        float gain = 1.0f / last;
        if (1.0f <= gain)
            gain = 1.0f;
        float s0 = es.st[0];
        if (s0 <= tiny)
            s0 = tiny;
        buf[i] = gain;
        ga = 1.0f / s0;
        if (1.0f <= ga)
            ga = 1.0f;
        gb = aPrev;
        gg = gain;
    }
    g = gg;
    a = ga;
    b = gb;
}

// --- delays ---
void DelayInt::setRate (float r) noexcept
{
    sr = r;
    isr = 1.0f / r;
    d = f2u64 (max0 (r * t));
}
void DelayInt::process (const float* in, float* out, int n) noexcept
{
    const auto di = (std::uint32_t) d;
    auto wi = (std::uint32_t) w;
    float* b = buf.data();
    for (int i = 0; i < n; ++i)
    {
        b[wi] = in[i];
        out[i] = b[(wi - di) & (ringSize - 1)];
        wi = (wi + 1) & (ringSize - 1);
    }
    w = wi;
}
void DelayInt::reset() noexcept
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    w = 0;
}

void DelayLerp::setRate (float r) noexcept
{
    sr = r;
    isr = 1.0f / r;
    c = (float) expD_ ((double) isr * k5Hz);
    target = u64f (f2u64 (max0 (r * t)));
}
void DelayLerp::process (const float* in, float* out, int n) noexcept
{
    const float tgt = target, near = tgt * 0x1.0624dep-10f, rounded = std::round (tgt); // 0.1 % of the length
    float y = cur;
    auto wi = (std::uint32_t) w;
    float* b = buf.data();
    for (int i = 0; i < n; ++i)
    {
        b[wi] = in[i];
        y = (y - tgt) * c + tgt;
        const float d = std::fabs (y - tgt) < near ? rounded : y;
        const std::uint64_t di = f2u64 (d);
        const std::uint32_t r = (wi - (std::uint32_t) di) & (ringSize - 1);
        const float x0 = b[r];
        out[i] = (b[(r + 1) & (ringSize - 1)] - x0) * (d - u64f (di)) + x0;
        wi = (wi + 1) & (ringSize - 1);
    }
    cur = y;
    w = wi;
}
void DelayLerp::reset() noexcept
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    w = 0;
}

// --- halfband polyphase filters ---
void Halfband::init (int coefs) noexcept
{
    const float* ca = coefs == 12 ? hbA12 : coefs == 6 ? hbA6 : hbA4;
    const float* cb = coefs == 12 ? hbB12 : coefs == 6 ? hbB6 : hbB4;
    na = nb = coefs / 2;
    for (int k = 0; k < na; ++k)
    {
        a[(std::size_t) k] = { ca[k], 0.0f, 0.0f, 0.0f, 0.0f };
        b[(std::size_t) k] = { cb[k], 0.0f, 0.0f, 0.0f, 0.0f };
    }
    prev = 0.0f;
}
void Halfband::reset() noexcept
{
    for (auto& s : a)
        s.x1 = s.x2 = s.y1 = s.y2 = 0.0f;
    for (auto& s : b)
        s.x1 = s.x2 = s.y1 = s.y2 = 0.0f;
}

namespace
{
int hbBase (int os) noexcept { return os == 2 ? 0 : os == 4 ? 1 : os == 8 ? 3 : 6; }
int hbStages (int os) noexcept { return os == 2 ? 1 : os == 4 ? 2 : os == 8 ? 3 : 4; }
} // namespace

void Oversampler::init() noexcept
{
    constexpr int kinds[10] = { 12, 12, 6, 12, 6, 4, 12, 6, 4, 4 };
    for (std::size_t k = 0; k < 10; ++k)
        hb[k].init (kinds[k]);
}
void Oversampler::reset() noexcept
{
    for (auto& h : hb)
        h.reset();
}
namespace
{
// One half-band filter per channel over a whole block, the channels interleaved (their dependency chains overlap),
// state in locals. run(): chain A of all-pass sections v = (v − y2)·c + x2 on x, output ½ (A(x) + the previous
// sample's B), then chain B on x — the library's per-sample operations, so the result is identical.
// up: every input gives run (x), run (0); down: every input pair gives run (first) (run (second) is discarded).
// out may alias in for down.
template <int N>
struct HbLocal
{
    float ac[N], ax1[N], ax2[N], ay1[N], ay2[N], bc[N], bx1[N], bx2[N], by1[N], by2[N], prev;
    void load (const Halfband& h) noexcept
    {
        for (int k = 0; k < N; ++k)
        {
            const HbSection& sa = h.a[(std::size_t) k];
            const HbSection& sb = h.b[(std::size_t) k];
            ac[k] = sa.c; ax1[k] = sa.x1; ax2[k] = sa.x2; ay1[k] = sa.y1; ay2[k] = sa.y2;
            bc[k] = sb.c; bx1[k] = sb.x1; bx2[k] = sb.x2; by1[k] = sb.y1; by2[k] = sb.y2;
        }
        prev = h.prev;
    }
    void store (Halfband& h) const noexcept
    {
        for (int k = 0; k < N; ++k)
        {
            HbSection& sa = h.a[(std::size_t) k];
            HbSection& sb = h.b[(std::size_t) k];
            sa.x1 = ax1[k]; sa.x2 = ax2[k]; sa.y1 = ay1[k]; sa.y2 = ay2[k];
            sb.x1 = bx1[k]; sb.x2 = bx2[k]; sb.y1 = by1[k]; sb.y2 = by2[k];
        }
        h.prev = prev;
    }
    float run (float x) noexcept
    {
        float v = x;
        for (int k = 0; k < N; ++k)
        {
            const float old = ax1[k];
            ax1[k] = v;
            v = (v - ay2[k]) * ac[k] + ax2[k];
            ax2[k] = old;
            ay2[k] = ay1[k];
            ay1[k] = v;
        }
        const float o = (v + prev) * 0.5f;
        v = x;
        for (int k = 0; k < N; ++k)
        {
            const float old = bx1[k];
            bx1[k] = v;
            v = (v - by2[k]) * bc[k] + bx2[k];
            bx2[k] = old;
            by2[k] = by1[k];
            by1[k] = v;
        }
        prev = v;
        return o;
    }
};

template <int N, bool Up>
void hbBlock (Halfband* const* h, const float* const* in, float* const* out, int nCh, int nIn) noexcept
{
    HbLocal<N> f0, f1;
    f0.load (*h[0]);
    if (nCh > 1)
        f1.load (*h[1]);
    if (nCh > 1)
    {
        const float *i0 = in[0], *i1 = in[1];
        float *o0 = out[0], *o1 = out[1];
        if constexpr (Up)
            for (int i = 0; i < nIn; ++i)
            {
                const float x0 = i0[i], x1 = i1[i];
                o0[2 * i] = f0.run (x0);
                o1[2 * i] = f1.run (x1);
                o0[2 * i + 1] = f0.run (0.0f);
                o1[2 * i + 1] = f1.run (0.0f);
            }
        else
            for (int i = 0; i < nIn / 2; ++i)
            {
                const float a0 = i0[2 * i], a1 = i0[2 * i + 1], b0 = i1[2 * i], b1 = i1[2 * i + 1];
                const float v0 = f0.run (a0);
                const float v1 = f1.run (b0);
                f0.run (a1);
                f1.run (b1);
                o0[i] = v0;
                o1[i] = v1;
            }
        f1.store (*h[1]);
    }
    else
    {
        const float* i0 = in[0];
        float* o0 = out[0];
        if constexpr (Up)
            for (int i = 0; i < nIn; ++i)
            {
                const float x = i0[i];
                o0[2 * i] = f0.run (x);
                o0[2 * i + 1] = f0.run (0.0f);
            }
        else
            for (int i = 0; i < nIn / 2; ++i)
            {
                const float x0 = i0[2 * i], x1 = i0[2 * i + 1];
                const float v = f0.run (x0);
                f0.run (x1);
                o0[i] = v;
            }
    }
    f0.store (*h[0]);
}

template <bool Up>
void hbStage (Halfband* const* h, const float* const* in, float* const* out, int nCh, int nIn) noexcept
{
    if (h[0]->na == 6)
        hbBlock<6, Up> (h, in, out, nCh, nIn);
    else if (h[0]->na == 3)
        hbBlock<3, Up> (h, in, out, nCh, nIn);
    else
        hbBlock<2, Up> (h, in, out, nCh, nIn);
}
} // namespace

// Stage-major: each half-band filter runs over the whole block (the filters are in series, so this equals the
// library's sample-major order bit for bit). scratch[c] holds os · n floats.
void Oversampler::up (Oversampler* const* ovs, const float* const* in, float* const* out, float* const* scratch,
                      int nCh, int n, int os) noexcept
{
    const int base = hbBase (os), stages = hbStages (os);
    // stage s writes to out when (stages − 1 − s) is even, so the last one lands in out
    const bool firstInScratch = (stages - 1) % 2 == 0;
    float* src[2];
    for (int c = 0; c < nCh; ++c)
    {
        src[c] = firstInScratch ? scratch[c] : out[c];
        for (int i = 0; i < n; ++i)
            src[c][i] = os == 2 ? in[c][i] + in[c][i] : in[c][i] * (float) os;
    }
    int len = n;
    for (int s = 0; s < stages; ++s)
    {
        float* dst[2];
        Halfband* h[2];
        for (int c = 0; c < nCh; ++c)
        {
            dst[c] = (stages - 1 - s) % 2 == 0 ? out[c] : scratch[c];
            h[c] = &ovs[c]->hb[(std::size_t) (base + s)];
        }
        hbStage<true> (h, src, dst, nCh, len);
        for (int c = 0; c < nCh; ++c)
            src[c] = dst[c];
        len *= 2;
    }
}
void Oversampler::down (Oversampler* const* ovs, float* const* in, float* const* out, int nCh, int n, int os) noexcept
{
    const int base = hbBase (os), stages = hbStages (os);
    int len = n * os;
    for (int s = stages - 1; s >= 0; --s)
    {
        Halfband* h[2];
        float* dst[2];
        for (int c = 0; c < nCh; ++c)
        {
            h[c] = &ovs[c]->hb[(std::size_t) (base + s)];
            dst[c] = s == 0 ? out[c] : in[c];
        }
        hbStage<false> (h, in, dst, nCh, len);
        len /= 2;
    }
}

// --- true peak: windowed-sinc 4x interpolator ---
void TruePeak::init() noexcept
{
    constexpr double pi = 0x1.921fb54442d18p+1;
    const double inv = 1.0 / (double) factor;
    for (int j = 0; j < taps; ++j)
    {
        const double x = (double) (float) (inv * (double) j - 48.0);
        double s = 1.0;
        if (0x1.0c6f7a0b5ed8dp-20 <= std::fabs (x)) // 1e-6
        {
            const double xp = x * pi;
            s = sinAccurate (xp) / xp;
        }
        const double win = sinAccurate (((double) j / (double) (taps - 1)) * pi);
        coef[(std::size_t) j] = (float) (win * s);
    }
    acc.fill (0.0f);
    pos = 0;
}
void TruePeak::process (const float* in, float* out, float* scratch, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        if (bufLen <= taps + pos)
        {
            const int m = bufLen - pos;
            for (int k = 0; k < m; ++k)
                acc[(std::size_t) (pos + k)] = coef[(std::size_t) k] * x + acc[(std::size_t) (pos + k)];
            const int rest = taps + pos - bufLen;
            for (int k = 0; k < rest; ++k)
                acc[(std::size_t) k] = coef[(std::size_t) (m + k)] * x + acc[(std::size_t) k];
        }
        else
            for (int k = 0; k < taps; ++k)
                acc[(std::size_t) (pos + k)] = coef[(std::size_t) k] * x + acc[(std::size_t) (pos + k)];
        int z = pos + taps;
        if (z >= bufLen)
            z -= bufLen;
        for (int q = 0; q < factor; ++q)
        {
            acc[(std::size_t) z] = 0.0f;
            z = z + 1 < bufLen ? z + 1 : 0;
        }
        const int old = pos;
        pos += factor;
        if (pos >= bufLen)
            pos -= bufLen;
        std::memcpy (scratch + (std::size_t) i * factor, acc.data() + old, sizeof (float) * factor);
    }
    for (int i = 0; i < n; ++i)
    {
        const float* q = scratch + (std::size_t) i * factor;
        const float a0 = std::fabs (q[0]), a1 = std::fabs (q[1]), a2 = std::fabs (q[2]), a3 = std::fabs (q[3]);
        const float m1 = a3 <= a2 ? a2 : a3, m2 = a1 <= a0 ? a0 : a1;
        out[i] = m1 <= m2 ? m2 : m1;
    }
}
} // namespace detail

// ======================================================================================================== Engine

Engine::Engine()
{
    // the library's constructor state (its parameter calls before a sample rate are overwritten by prepare)
    p = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 2.0f, 7.0f, 0.0f,
          4.0f, 1.0f, 1.0f, 1.0f, 0.99966854f, 0.0f, 1.0f, 10.05f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };
    p[18] = 0x1.ffd48ep-1f;
    for (int ch = 0; ch < 2; ++ch)
    {
        upMain[ch].init();
        downMain[ch].init();
        upDry[ch].init();
        downDry[ch].init();
        tp[ch].init();
        grar[ch].es.att = grar[ch].att;
    }
}

void Engine::prepare (float sampleRate, int maxBlockSize)
{
    ScopedFlushDenormals ftz;
    maxBlock = std::max (1, maxBlockSize);
    const auto n = (std::size_t) maxBlock;
    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto* v : { &xin[ch], &b8[ch], &b14[ch], &b16[ch], &b18[ch], &b22[ch], &b24[ch], &b26[ch] })
            v->assign (n, 0.0f);
        b10[ch].assign (16 * n, 0.0f);
        b12[ch].assign (16 * n, 0.0f);
        for (auto* d : { &look[ch], &lookOs[ch] })
            if (d->buf.size() != (std::size_t) detail::ringSize)
                d->buf.assign ((std::size_t) detail::ringSize, 0.0f);
        for (auto* d : { &msDelay[ch], &tpDelay[ch], &dry[ch] })
            if (d->buf.size() != (std::size_t) detail::ringSize)
                d->buf.assign ((std::size_t) detail::ringSize, 0.0f);
    }
    b20.assign (n, 0.0f);
    b21.assign (n, 0.0f);
    tpScratch.assign (n * detail::TruePeak::factor, 0.0f);

    sr = sampleRate;
    isr = 1.0f / sampleRate;
    const std::uint64_t osN = std::max<std::uint64_t> (1, f2u64 (aOsF));
    const float osRate = (float) osN * sampleRate;
    for (int ch = 0; ch < 2; ++ch)
    {
        grar[ch].prepare (sampleRate);
        grOs[ch].setRate (osRate);
        grMs[ch].setRate (sampleRate);
        grTp[ch].setRate (sampleRate);
        look[ch].setRate (sampleRate);
        lookOs[ch].setRate (osRate);
        msDelay[ch].setRate (sampleRate);
        tpDelay[ch].setRate (sampleRate);
        dry[ch].setRate (sampleRate);
        lvlIn[ch].prepare (sampleRate);
        lvlOut[ch].prepare (sampleRate);
        eLo[ch].prepare (sampleRate);
        eHi[ch].prepare (sampleRate);
        split[ch].prepare (sampleRate);
        fade[ch].prepare (sampleRate);
        meter[ch].prepare (sampleRate);
        dc[ch].prepare (sampleRate);
    }
    lpBypass.prepare (sampleRate);
    lpGain.prepare (sampleRate);
    lpOut.prepare (sampleRate);
    lpAtt.prepare (sampleRate);
    esBal.setRate (sampleRate);
    defaults();
    prepared = true;
    resendAll();
}

// the library's per-rate defaults
void Engine::defaults() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& gr = grar[ch];
        gr.hold = 0x1.99999ap-5f; // 0.05
        gr.ph.hold = 0x1.99999ap-5f;
        gr.ph.len = gr.ph.sr * 0x1.99999ap-8f; // 0.00625
        const float la = std::round (sr * 0x1.99999ap-5f);
        look[ch].target = la;
        look[ch].t = la * look[ch].isr;

        auto& g1 = grMs[ch];
        g1.att = g1.es.att = 0x1.0624dep-7f; // 8 ms
        const float c8 = (float) expD_ ((double) g1.es.isr * k8ms);
        g1.es.c44 = c8;
        g1.hold = g1.ph.hold = 0x1.0624dep-7f;
        g1.ph.len = g1.ph.sr * 0x1.0624dep-10f;
        g1.rel = g1.es.rel = 0x1.0624dep-7f;
        g1.es.c40 = c8;
        msDelay[ch].t = 0x1.0624dep-7f;
        msDelay[ch].d = f2u64 (max0 (msDelay[ch].sr * 0x1.0624dep-7f));

        auto& g4 = grTp[ch];
        g4.att = g4.es.att = 0x1.0624dep-8f; // 4 ms
        const float c4 = expf_ (g4.es.factor * -0x1.f3fffep+7f);
        g4.es.c44 = c4;
        g4.hold = g4.ph.hold = 0x1.0624dep-8f;
        g4.ph.len = g4.ph.sr * 0x1.0624dep-11f;
        g4.rel = g4.es.rel = 0x1.0624dep-8f;
        g4.es.c40 = c4;
        tpDelay[ch].t = 0x1.0624dep-8f;
        tpDelay[ch].d = f2u64 (max0 (tpDelay[ch].sr * 0x1.0624dep-8f));

        lvlIn[ch].setCoef (0.125f, kLevel);
        lvlOut[ch].setCoef (0.125f, kLevel);
        eLo[ch].setCoef (5.0f, k5Hz);
        eHi[ch].setCoef (5.0f, k5Hz);
        split[ch].setCoef (320.0f, kSplit);
        meter[ch].setCoef (3.3333333f, kMeter);
        fade[ch].setCoef (100.0f, kFade);
        dc[ch].cut = 5.0f;
        dc[ch].c = (float) expD_ ((double) dc[ch].isr * k5Hz);
    }
    lpAtt.setCoef (1000.0f, k1000);
    esBal.att = 0x1.99999ap-5f;
    esBal.c44 = (float) expD_ ((double) esBal.isr * kEs50);
    esBal.rel = 0x1.99999ap-5f;
    esBal.c40 = esBal.c44;
}

void Engine::resendAll() noexcept
{
    for (int k = 0; k <= 12; ++k)
        set (k, p[(std::size_t) k]);
    set (25, p[25]);
    set (27, p[27]);
    set (26, p[26]);
}

void Engine::defer() noexcept
{
    pending = true;
    enable = false;
}

std::uint64_t Engine::latencyRaw() const noexcept
{
    std::uint64_t lat = 0;
    if (aLa == 0.0f || aInstant == 1.0f)
        lat = f2u64 (look[0].target);
    const std::uint64_t os = std::max<std::uint64_t> (1, f2u64 (aOsF));
    lat += f2u64 (lookOs[0].target) / os;
    if (aMs == 1.0f)
        lat += msDelay[0].d;
    return tpDelay[0].d + lat;
}

int Engine::latency() const noexcept { return (int) latencyRaw(); }

void Engine::updateLatency (bool en) noexcept
{
    const std::uint64_t lat = latencyRaw();
    for (int ch = 0; ch < 2; ++ch)
    {
        dry[ch].d = lat;
        dry[ch].t = u64f (lat) * dry[ch].isr;
    }
    if (en)
        enable = true;
}

void Engine::setParam (int k, float v) noexcept
{
    ScopedFlushDenormals ftz;
    set (k, v);
}

void Engine::set (int k, float v) noexcept
{
    switch (k)
    {
        case 0: p[0] = v; break;
        case 1:
            p[1] = std::round (v);
            if (! muted) { defer(); break; }
            aDelta = p[1];
            set (9, p[9]);
            enable = true;
            break;
        case 2:
        {
            p[2] = std::round (v);
            const std::uint64_t ord = f2u64 (p[2]) + 1;
            for (auto& gr : grar)
            {
                gr.es.setOrder (ord);
                for (std::size_t j = 1; j < 8; ++j)
                    gr.es.st[j] = gr.es.st[0];
            }
            set (8, p[8]);
            set (26, p[26]);
            break;
        }
        case 3:
            p[3] = v;
            if (! muted) { defer(); break; }
            aTp = v;
            updateLatency (true);
            break;
        case 4:
            p[4] = v;
            if (! muted) { defer(); break; }
            aMs = v;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& g1 = grMs[ch];
                if (v <= 0.5f)
                {
                    g1.att = g1.es.att = 0x1.0624dep-7f;
                    const float c8 = (float) expD_ ((double) g1.es.isr * k8ms);
                    g1.es.c44 = c8;
                    g1.hold = g1.ph.hold = 0x1.0624dep-7f;
                    g1.ph.len = g1.ph.sr * 0x1.0624dep-10f;
                    g1.rel = g1.es.rel = 0x1.0624dep-7f;
                    g1.es.c40 = c8;
                    msDelay[ch].t = 0x1.0624dep-7f;
                    msDelay[ch].d = f2u64 (max0 (msDelay[ch].sr * 0x1.0624dep-7f));
                }
                else
                {
                    const std::uint64_t idx = std::min<std::uint64_t> (9, std::max<std::uint64_t> (2, f2u64 (p[12])));
                    const float tm = msTime[idx];
                    g1.att = g1.es.att = tm;
                    const double d = m2Pi / (double) tm;
                    const float c = (float) expD_ ((double) g1.es.isr * d);
                    g1.es.c44 = c;
                    const float h = max0 (tm);
                    g1.hold = tm;
                    g1.ph.hold = h;
                    g1.ph.len = (h * 0.125f) * g1.ph.sr;
                    g1.rel = g1.es.rel = tm;
                    g1.es.c40 = c;
                    msDelay[ch].t = tm;
                    msDelay[ch].d = f2u64 (max0 (tm * msDelay[ch].sr));
                }
            }
            updateLatency (true);
            break;
        case 5:
            p[5] = std::round (v);
            pModeLow = 1.0f < p[5] ? 0.0f : 1.0f;
            set (8, p[8]);
            set (6, p[6]);
            break;
        case 6:
        {
            p[6] = v;
            pGin = expf_ (v * 0x1.d791c6p-4f);
            pGinInv = 1.0f / pGin;
            const float sc = expf_ (v * -0x1.d791c6p-4f);
            for (auto& gr : grar)
            {
                gr.thr = v;
                gr.scale = sc;
            }
            set (9, p[9]);
            break;
        }
        case 7: p[7] = v; break;
        case 8:
        {
            p[8] = v;
            const float mode = p[5];
            if (mode == 0.0f) { set (22, 0.0f); set (20, 1.0f); set (19, 1.0f); }
            else if (mode == 2.0f) { set (22, 0.0f); set (20, p[8]); set (19, p[8]); }
            else if (mode == 1.0f) { set (22, v); set (20, 0.0f); set (19, 1.0f); }
            else { set (22, v); set (20, 0.0f); set (19, p[8]); }
            break;
        }
        case 9:
        {
            const bool plain = p[1] <= 0.5f;
            p[9] = v;
            if (plain)
            {
                const float x = expf_ (v * 0x1.d791c6p-4f);
                pOut = x < 0x1.ffd48ep-1f ? x : 0x1.ffd48ep-1f;
            }
            else
                pOut = p[10] < 0.5f ? pGinInv : 1.0f;
            break;
        }
        case 10:
            p[10] = std::round (v);
            set (9, p[9]);
            break;
        case 11:
        {
            p[11] = std::round (v);
            if (! muted) { defer(); break; }
            aOs = p[11];
            float f = p[11];
            if (0x1p63f <= f)
                f -= 0x1p63f;
            const std::uint32_t u = 1u << ((std::uint32_t) cvtt64 (f) & 31u);
            f = (std::int32_t) u < 0 ? (float) (std::uint64_t) (std::int64_t) (std::int32_t) u : (float) (std::int32_t) u;
            pOs = f;
            aOsF = f;
            float rate = f * sr;
            if (rate <= 44100.0f)
                rate = 44100.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                grOs[ch].setRate (rate);
                lookOs[ch].setRate (rate);
            }
            updateLatency (true);
            break;
        }
        case 12:
        {
            p[12] = std::round (v);
            aHard = p[12];
            const float pw = powf_ (2.0f, -v);
            const float tTot = pw * 0x1.0624dep-5f; // 32 ms · 2^-hardness
            const float tm = tTot < 0x1.0624dep-7f ? tTot : 0x1.0624dep-7f;
            const float f1 = floorTrick (sr * tTot);
            const float f2 = floorTrick (sr * 0x1.99999ap-5f);
            float vi = v;
            if (0x1p63f <= vi)
                vi -= 0x1p63f;
            const auto idx = (std::size_t) std::clamp<std::int64_t> (cvtt64 (vi), 0, 9);
            const float tLa = f2 - f1;
            const float tOs = f1 * aOsF;
            const float tm0 = max0 (tm), tt0 = max0 (tTot);
            const float hm = tm0 * 0.125f, ht = tt0 * 0.125f;
            const float att = attackMul[idx] * tTot;
            const double invTm = 1.0 / (double) tm;
            const float nA = -1.0f / att;
            for (int ch = 0; ch < 2; ++ch)
            {
                look[ch].target = tLa;
                look[ch].t = tLa * look[ch].isr;
                auto& g8 = grOs[ch];
                g8.att = g8.es.att = att;
                g8.es.c44 = expf_ (nA * g8.es.factor);
                g8.hold = tTot;
                g8.ph.hold = tt0;
                g8.ph.len = ht * g8.ph.sr;
                g8.rel = g8.es.rel = tTot;
                g8.es.c40 = expf_ ((-1.0f / tTot) * g8.es.factor);
                lookOs[ch].target = tOs;
                lookOs[ch].t = tOs * lookOs[ch].isr;
                auto& g1 = grMs[ch];
                g1.att = g1.es.att = tm;
                const float c = (float) expD_ (((double) g1.es.isr * invTm) * m2Pi);
                g1.es.c44 = c;
                g1.hold = tm;
                g1.ph.hold = tm0;
                g1.ph.len = hm * g1.ph.sr;
                g1.rel = g1.es.rel = tm;
                g1.es.c40 = c;
                msDelay[ch].t = tm;
                msDelay[ch].d = f2u64 (max0 (tm * msDelay[ch].sr));
            }
            updateLatency (false);
            break;
        }
        case 13: p[13] = 0.5f < v ? 1.0f : 0.0f; break;
        case 19:
        {
            float x = 1.0f - v;
            if (p[5] != 0.0f && p[5] != 1.0f)
                x = x * x;
            p19 = x * 0.5f;
            const float w = x * x * 2.5f + 1.0f;
            const float r1 = w * 0x1.333334p-3f;
            const float r2 = (r1 * 4.0f) * w;
            for (auto& gr : grar)
            {
                gr.w = w;
                gr.rel1 = r1;
                gr.rel2 = r2;
            }
            break;
        }
        case 20:
        {
            p20 = v;
            const float e = expf_ ((v * 0x1.99999ap-1f + 0x1.99999ap-3f) * kLn10);
            const auto idx = (std::size_t) std::min<std::uint64_t> (3, f2u64 (p[2]));
            const float a = ((1.0f - e) * kNinth) * (orderScale[idx] * 10.0f) + 0x1.99999ap-5f;
            pAtt = a;
            const float na = -1.0f / a;
            for (auto& gr : grar)
            {
                gr.att = a;
                gr.es.att = a;
                gr.es.c44 = expf_ (na * gr.es.factor);
                gr.es1.att = gr.att;
                gr.es1.c44 = (float) expD_ ((double) gr.es1.isr * (m2Pi / (double) gr.att));
            }
            break;
        }
        case 22:
        {
            p22 = v;
            const float r = 1.0f - logf_ (v * kTen + 1.0f) * kKnee;
            kneeR = r;
            kneeT = (pGin - 1.0f) * r + 1.0f;
            break;
        }
        case 25:
            p[25] = v;
            if (! muted) { defer(); break; }
            aLa = v;
            updateLatency (true);
            break;
        case 26:
        {
            const float b = 0.5f < v ? 1.0f : 0.0f;
            p[26] = b;
            if (! muted) { defer(); break; }
            aInstant = b;
            const float m = 10.0f / (p[2] + 1.0f);
            for (auto& gr : grar)
                gr.mult = m;
            set (8, p[8]);
            for (auto& gr : grar)
                gr.flag = aInstant != 0.0f;
            updateLatency (true);
            break;
        }
        case 27: p[27] = 0.5f < v ? 1.0f : 0.0f; break;
        default: break;
    }
}

void Engine::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        look[ch].reset();
        lookOs[ch].reset();
        msDelay[ch].reset();
        tpDelay[ch].reset();
        dry[ch].reset();
        upMain[ch].reset();
        downMain[ch].reset();
        upDry[ch].reset();
        downDry[ch].reset();
    }
}

int Engine::latencyFor (float sampleRate, const float* ep) noexcept
{
    const float instant = 0.5f < ep[26] ? 1.0f : 0.0f;
    float f = std::round (ep[11]);
    if (0x1p63f <= f)
        f -= 0x1p63f;
    const std::uint32_t u = 1u << ((std::uint32_t) cvtt64 (f) & 31u);
    const float osF = (std::int32_t) u < 0 ? (float) (std::uint64_t) (std::int64_t) (std::int32_t) u : (float) (std::int32_t) u;
    const float tTot = powf_ (2.0f, -ep[12]) * 0x1.0624dep-5f;
    const float tm = tTot < 0x1.0624dep-7f ? tTot : 0x1.0624dep-7f;
    const float f1 = floorTrick (sampleRate * tTot);
    const float f2 = floorTrick (sampleRate * 0x1.99999ap-5f);
    std::uint64_t lat = 0;
    if (ep[25] == 0.0f || instant == 1.0f)
        lat = f2u64 (f2 - f1);
    lat += f2u64 (f1 * osF) / std::max<std::uint64_t> (1, f2u64 (osF));
    if (ep[4] == 1.0f)
        lat += f2u64 (max0 (tm * sampleRate));
    lat += f2u64 (max0 (sampleRate * 0x1.0624dep-8f));
    return (int) lat;
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    if (n <= 0)
        return;
    if (! prepared)
    {
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);
        return;
    }
    ScopedFlushDenormals ftz;
    for (int done = 0; done < n;)
    {
        const int m = std::min (maxBlock, n - done);
        processBlock (inL + done, inR + done, outL + done, outR + done, m);
        done += m;
    }
}

namespace
{
// the analysis' tanh, a [7/6] Padé form with x² precomputed
inline float tanhP (float x, float x2) noexcept
{
    return ((((x2 + 378.0f) * x2 + 17325.0f) * x2 + 135135.0f) * x)
         / (((x2 * 28.0f + 3150.0f) * x2 + 62370.0f) * x2 + 135135.0f);
}
} // namespace

void Engine::processBlock (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    float* x[2] = { xin[0].data(), xin[1].data() };
    std::memcpy (x[0], inL, sizeof (float) * (std::size_t) n);
    std::memcpy (x[1], inR, sizeof (float) * (std::size_t) n);
    float* out[2] = { outL, outR };

    if (p[27] != 0.0f)
        for (int ch = 0; ch < 2; ++ch)
            dc[ch].process (x[ch], x[ch], n);

    // (modes 0/1) balance of the bands below / above 320 Hz → emphasis knee and (mode 0) the envelope attack
    if (0.5f < pModeLow)
    {
        float m = 0.0f, bal = 0.0f, att = 0.0f;
        const float k = p[5] == 0.0f ? 10.0f : 40.0f;
        const float floorV = aInstant != 0.0f ? 0x1.99999ap-5f : 0.5f;
        const bool busy = 0x1.ccccccp-1f < grOs[1].activity && 0x1.ccccccp-1f < grOs[0].activity;
        for (int i = 0; i < n; ++i)
        {
            const float x0 = x[0][i], x1 = x[1][i];
            const float lo0 = (split[0].s - x0) * split[0].c + x0;
            split[0].s = lo0;
            const float lo1 = (split[1].s - x1) * split[1].c + x1;
            split[1].s = lo1;
            float h0 = x0 - lo0;
            h0 = h0 * h0;
            float h1 = x1 - lo1;
            h1 = h1 * h1;
            const float el0 = (eLo[0].s - lo0 * lo0) * eLo[0].c + lo0 * lo0;
            eLo[0].s = el0;
            const float el1 = (eLo[1].s - lo1 * lo1) * eLo[1].c + lo1 * lo1;
            eLo[1].s = el1;
            const float eh0 = (eHi[0].s - h0) * eHi[0].c + h0;
            eHi[0].s = eh0;
            float d0 = std::sqrt (eh0);
            d0 = tiny < d0 ? d0 : tiny; // the library's max / min keep the second operand on NaN
            const float eh1 = (eHi[1].s - h1) * eHi[1].c + h1;
            eHi[1].s = eh1;
            const float r0 = (1.0f / d0) * std::sqrt (el0);
            float d1 = std::sqrt (eh1);
            d1 = tiny < d1 ? d1 : tiny;
            const float r1 = (1.0f / d1) * std::sqrt (el1);
            const float a0 = k * (1.0f - tanhP (r0, r0 * r0));
            const float a1 = k * (1.0f - tanhP (r1, r1 * r1));
            const float t0 = tanhP (a0, a0 * a0), t1 = tanhP (a1, a1 * a1);
            m = t1 < t0 ? t1 : t0;
            float cand = std::sqrt (m) * pAtt; // NaN when m < 0 (huge low / high ratio) → floorV
            cand = floorV < cand ? cand : floorV;
            const float sel = busy ? cand : pAtt;
            att = (lpAtt.s - sel) * lpAtt.c + sel;
            lpAtt.s = att;
            const float cand2 = 0x1.0c6f7ap-20f < m ? m : 0x1.0c6f7ap-20f;
            const float sel2 = busy ? cand2 : 1.0f;
            const float y = (esBal.s - sel2) * (esBal.s < sel2 ? esBal.c44 : esBal.c40) + sel2;
            esBal.s = y;
            bal = 0.0f < y ? y : 0.0f;
        }
        if (p[5] == 0.0f)
        {
            const float na = -1.0f / att;
            for (auto& gr : grar)
            {
                gr.att = att;
                gr.es.att = att;
                gr.es.c44 = expf_ (na * gr.es.factor);
                gr.es1.att = gr.att;
                gr.es1.c44 = (float) expD_ ((m2Pi / (double) gr.att) * (double) gr.es1.isr);
            }
            float g = (24.0f - p[6]) * 0x1.555556p-5f;
            if (1.0f <= g)
                g = 1.0f;
            set (22, std::sqrt (std::sqrt (m) * g));
        }
        else
            set (22, std::sqrt (std::sqrt (bal)));
    }

    // dry path: delayed by the latency (and through the halfbands when delta listens to an oversampled engine)
    const std::uint64_t osN = std::max<std::uint64_t> (1, f2u64 (aOsF));
    const int os = (int) std::min<std::uint64_t> (osN, 16);
    for (int ch = 0; ch < 2; ++ch)
        dry[ch].process (x[ch], b14[ch].data(), n);
    if (os > 1 && 0.0f < p[1])
    {
        float* dr[2] = { b14[0].data(), b14[1].data() };
        const float* drc[2] = { dr[0], dr[1] };
        float* u[2] = { b12[0].data(), b12[1].data() };
        float* sc[2] = { b10[0].data(), b10[1].data() };
        detail::Oversampler* upO[2] = { &upDry[0], &upDry[1] };
        detail::Oversampler* downO[2] = { &downDry[0], &downDry[1] };
        detail::Oversampler::up (upO, drc, u, sc, 2, n, os);
        detail::Oversampler::down (downO, u, dr, 2, n, os);
    }
    for (int ch = 0; ch < 2; ++ch)
        std::memcpy (b16[ch].data(), x[ch], sizeof (float) * (std::size_t) n);

    // input gain
    const bool ms = 0.5f < aMs;
    if (! ms)
    {
        std::fill (b20.begin(), b20.begin() + n, pGin);
        lpGain.process (b20.data(), b20.data(), n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                out[ch][i] = b20[(std::size_t) i] * x[ch][i];
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            b8[0][(std::size_t) i] = x[0][i] + x[1][i];
            b8[1][(std::size_t) i] = x[0][i] - x[1][i];
        }
        std::fill (b20.begin(), b20.begin() + n, pGin * 0.5f);
        lpGain.process (b20.data(), b20.data(), n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                out[ch][i] = b20[(std::size_t) i] * b8[ch][(std::size_t) i];
    }

    // detector: |x|, stereo linking, emphasis knee
    float* d0 = b8[0].data();
    float* d1 = b8[1].data();
    for (int i = 0; i < n; ++i)
    {
        d0[i] = std::fabs (out[0][i]);
        d1[i] = std::fabs (out[1][i]);
    }
    const float link = p[7];
    for (int i = 0; i < n; ++i)
    {
        const float a = d0[i], b = d1[i];
        const float mx = a < b ? b : a;
        d0[i] = (mx - a) * link + a;
        d1[i] = (mx - b) * link + b;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = b8[ch].data();
        for (int i = 0; i < n; ++i)
            d[i] = kneeT < d[i] ? (d[i] - kneeT) * kneeR + kneeT : d[i];
    }

    // per channel: adaptive release, look-ahead, oversampled peak limiter
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = b8[ch].data();
        float* o = out[ch];
        grar[ch].process (d, n);
        std::memcpy (b22[ch].data(), d, sizeof (float) * (std::size_t) n);
        std::memcpy (b26[ch].data(), d, sizeof (float) * (std::size_t) n);
        if (aLa == 0.0f || aInstant != 0.0f)
            look[ch].process (o, o, n);
        for (int i = 0; i < n; ++i)
            o[i] = d[i] * o[i];
        if (os < 2)
        {
            for (int i = 0; i < n; ++i)
                d[i] = std::fabs (o[i]);
            grOs[ch].process (d, d, n);
            std::memcpy (b24[ch].data(), d, sizeof (float) * (std::size_t) n);
            for (int i = 0; i < n; ++i)
                b26[ch][(std::size_t) i] = b22[ch][(std::size_t) i] * b24[ch][(std::size_t) i];
            lookOs[ch].process (o, o, n);
            for (int i = 0; i < n; ++i)
                o[i] = d[i] * o[i];
        }
    }
    if (os >= 2)
    {
        // the channels are independent, so both go through each half-band stage together
        float* u[2] = { b12[0].data(), b12[1].data() };
        float* gu[2] = { b10[0].data(), b10[1].data() };
        const float* oc[2] = { out[0], out[1] };
        detail::Oversampler* upO[2] = { &upMain[0], &upMain[1] };
        detail::Oversampler* downO[2] = { &downMain[0], &downMain[1] };
        detail::Oversampler::up (upO, oc, u, gu, 2, n, os);
        const int nu = n * os;
        for (int ch = 0; ch < 2; ++ch)
        {
            float* uc = u[ch];
            float* g = gu[ch];
            for (int i = 0; i < nu; ++i)
                g[i] = std::fabs (uc[i]);
            grOs[ch].process (g, g, nu);
            for (int i = 0; i < n; ++i)
            {
                const float* q = g + (std::size_t) i * (std::size_t) os;
                float mn = 1.0f;
                if (os == 2)
                {
                    for (int j = 0; j < 2; ++j)
                        mn = mn <= q[j] ? mn : q[j];
                }
                else
                    for (int j = 0; j < os; j += 4)
                    {
                        mn = mn <= q[j] ? mn : q[j];
                        mn = q[j + 1] <= mn ? q[j + 1] : mn;
                        mn = q[j + 2] <= mn ? q[j + 2] : mn;
                        mn = q[j + 3] <= mn ? q[j + 3] : mn;
                    }
                b24[ch][(std::size_t) i] = mn;
                b26[ch][(std::size_t) i] = b22[ch][(std::size_t) i] * mn;
            }
            lookOs[ch].process (uc, uc, nu);
            for (int i = 0; i < nu; ++i)
                uc[i] = g[i] * uc[i];
        }
        detail::Oversampler::down (downO, u, out, 2, n, os);
    }

    if (0.5f < aMs)
        for (int i = 0; i < n; ++i)
        {
            const float mid = out[0][i], side = out[1][i];
            out[0][i] = mid + side;
            out[1][i] = mid - side;
        }

    // M/S limiter, then true-peak or sample-peak limiter
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = b8[ch].data();
        float* o = out[ch];
        float* gr = b26[ch].data();
        if (1.0f == aMs)
        {
            for (int i = 0; i < n; ++i)
                d[i] = std::fabs (o[i]);
            grMs[ch].process (d, d, n);
            for (int i = 0; i < n; ++i)
                gr[i] = d[i] * gr[i];
            msDelay[ch].process (o, o, n);
            for (int i = 0; i < n; ++i)
                o[i] = d[i] * o[i];
        }
        if (tiny <= std::fabs (aTp - 1.0f))
            for (int i = 0; i < n; ++i)
                d[i] = std::fabs (o[i]);
        else
            tp[ch].process (o, d, tpScratch.data(), n);
        grTp[ch].process (d, d, n);
        for (int i = 0; i < n; ++i)
            gr[i] = d[i] * gr[i];
        tpDelay[ch].process (o, o, n);
        for (int i = 0; i < n; ++i)
            o[i] = d[i] * o[i];
    }

    // output ceiling gain
    std::fill (b21.begin(), b21.begin() + n, pOut);
    lpOut.process (b21.data(), b21.data(), n);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            out[ch][i] = b21[(std::size_t) i] * out[ch][i];

    const bool bypass = 0.5f < p[0];

    // level matching: × √(input power) / √(output power), 0.125 Hz averages, at most × 2
    if (0.5f < p[10])
        for (int ch = 0; ch < 2; ++ch)
        {
            float* pi = b16[ch].data();
            float* po = b18[ch].data();
            float* o = out[ch];
            for (int i = 0; i < n; ++i)
            {
                pi[i] = pi[i] * pi[i];
                po[i] = o[i] * o[i];
            }
            lvlIn[ch].process (pi, pi, n);
            lvlOut[ch].process (po, po, n);
            for (int i = 0; i < n; ++i)
            {
                pi[i] = std::sqrt (pi[i]);
                po[i] = std::sqrt (po[i]);
            }
            for (int i = 0; i < n; ++i)
            {
                float r = pi[i] / po[i];
                r = r < 2.0f ? r : 2.0f; // like the library's min: 0 / 0 (silence) gives 2, not NaN
                o[i] = r * o[i];
            }
        }

    if (std::fabs (aDelta - 1.0f) < tiny)
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                out[ch][i] = b14[ch][(std::size_t) i] - out[ch][i];

    // bypass cross-fade (20 Hz) and the fade out / in around deferred parameter changes (2 × 100 Hz)
    const float byp = bypass ? 1.0f : 0.0f;
    const float startAfter = sr * 0x1.99999ap-4f; // 0.1 s
    for (int i = 0; i < n; ++i)
    {
        const float f = (lpBypass.s - byp) * lpBypass.c + byp;
        lpBypass.s = f;
        out[0][i] = (b14[0][(std::size_t) i] - out[0][i]) * f + out[0][i];
        out[1][i] = (b14[1][(std::size_t) i] - out[1][i]) * f + out[1][i];
        if (! bypass)
        {
            fadeCount = (std::uint64_t) (enable ? 1 : 0) * (fadeCount + 1);
            const float step = startAfter <= u64f (fadeCount) ? 1.0f : 0.0f;
            const float f1 = (fade[0].s - step) * fade[0].c + step;
            fade[0].s = f1;
            const float f2 = (fade[1].s - f1) * fade[1].c + f1;
            fade[1].s = f2;
            out[0][i] = f2 * out[0][i];
            out[1][i] = f2 * out[1][i];
        }
    }
    muted = fade[1].s < tiny;
    if (pending && muted)
    {
        reset();
        resendAll();
        pending = false;
    }
}
} // namespace lynx::emphasis
