#include "core/HarmlessFx.h"
#include "core/PlateReverb.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

// Specified as float32 / double operations in the order of the reference model (REA_rodada36); contraction of a·b + c
// into one FMA would change the roundings, so it stays off in this file.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::hlfx
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr float flushLevel = 5.960464477539063e-08f; // 2^-24: FL's denormal guard
const double ln001 = std::log (0.01);

inline float f32 (double x) noexcept { return (float) x; }
inline double rint (double x) noexcept { return std::nearbyint (x); } // round half to even (cvtsd2si)
// FUN_00627cc0 (x87): a double state below 2^-24 in magnitude becomes 0; the others are kept as they are (the
// decompiler shows a float conversion that the instructions do not do: fldl / fcmovb / fstpl, REA_rodada40 §2.4)
inline double flushState (double v) noexcept { return std::fabs (v) < (double) flushLevel ? 0.0 : v; }


// (11^x − 1)/10, exactly 1 at x = 1 (ln 11 as the float of the plugin's run-time global)
float g11 (float x) noexcept
{
    if (x == 1.0f)
        return 1.0f;
    return f32 ((std::exp ((double) x * (double) (float) std::log (11.0)) - 1.0) * (double) 0.1f);
}

// 1 − (5^x − 1)/4 (chorus dry and wet gains)
float g5 (float x) noexcept
{
    return f32 (1.0 - (double) f32 (std::exp ((double) x * std::log (5.0)) - 1.0) * 0.25);
}

// FUN_00612db0: start value, per-sample step (limited to maxStep) and new current value
struct Ramp
{
    double start = 0.0, step = 0.0, end = 0.0;
};

Ramp ramp (double cur, double target, int n, double maxStep) noexcept
{
    const auto d = (double) f32 (target - cur);
    if (d == 0.0)
        return { cur, 0.0, cur };
    auto step = (double) f32 (d / n);
    auto end = target;
    if (std::fabs (step) > maxStep)
    {
        step = std::copysign (maxStep, step);
        end = (double) f32 (cur + step * n);
    }
    return { cur, step, end };
}

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t at) noexcept
{
    std::int32_t v = 0;
    if (at + 4 <= d.size())
        std::memcpy (&v, d.data() + at, 4);
    return v;
}
} // namespace

// =================================================================================================================
// Maximus (REA_rodada36 §2)
// =================================================================================================================

namespace
{
// 2·(501^(v/10000) − 1) ms, kept as the float of (501^x − 1)
double tms (int v) noexcept
{
    return (double) f32 (std::exp ((double) f32 ((float) v * 0.0001f) * std::log (501.0)) - 1.0);
}

// L7: (7^x − 1)/6, exactly 1 at x = 1
float l7 (int v) noexcept
{
    const auto x = (float) v * 0.001f;
    if (x == 1.0f)
        return 1.0f;
    return f32 ((std::exp ((double) (x * (float) std::log (7.0))) - 1.0) * (double) (1.0f / 6.0f));
}

double coefFor (double n) noexcept // 1 % in n samples
{
    return n > 0.0 ? std::exp (ln001 / std::max (1.0, n)) : 0.0;
}

struct Point
{
    double dx = 0.0;
    float y = 0.0f, tension = 0.0f, q = 0.0f, b = 0.0f, c = 0.0f;
    int type = 0, sign = 0;
};

// one drawn envelope (the compression curve) and its incremental evaluation (FUN_007885d0)
class Curve
{
public:
    explicit Curve (std::vector<Point> pts) : p (std::move (pts)) {}

    bool supported() const noexcept
    {
        if (p.empty())
            return false;
        for (std::size_t i = 1; i < p.size(); ++i)
            if (p[i].type != 0 && p[i].type != 7)
                return false;
        return true;
    }

    void start() noexcept
    {
        idx = 0;
        y0 = p[0].y;
        dy = 0.0;
        if (p[0].dx == 0.0)
            rate = 1.0, t = 1.0;
        else
            rate = 1.0 / p[0].dx, t = 0.0;
    }

    double step (double dx) noexcept
    {
        const auto n = (int) p.size();
        t += dx * rate;
        while (t >= 1.0)
        {
            const auto old = idx;
            t = rate == 0.0 ? dx : (t - 1.0) / rate;
            y0 = p[(std::size_t) idx].y;
            if (idx < n - 1)
                ++idx;
            dy = (double) p[(std::size_t) idx].y - y0;
            if (old == idx)
                t = 0.0, rate = 0.0;
            else if (const auto d = p[(std::size_t) idx].dx; d <= 0.0)
                rate = 1.0, t += 1.0;
            else
                rate = 1.0 / d, t *= rate;
        }
        auto s = t;
        const auto& pt = p[(std::size_t) idx];
        if (pt.sign != 2)
        {
            if (pt.type == 0)
            {
                if (pt.q > 0.0f)
                    s = 1.0 - (std::exp ((1.0 - s) * (double) pt.c) - 1.0) * (double) pt.b;
                else if (pt.q < 0.0f)
                    s = (std::exp (s * (double) pt.c) - 1.0) * (double) pt.b;
            }
            else // type 7
            {
                const auto u = pt.sign > 0 ? 1.0 - s : s;
                const auto u3 = u * u * u;
                s = s - (u3 * u3 - u) * (double) pt.tension;
            }
        }
        return y0 + s * dy;
    }

private:
    std::vector<Point> p;
    int idx = 0;
    double t = 0.0, rate = 0.0, y0 = 0.0, dy = 0.0;
};
} // namespace

struct MaximusPreset
{
    std::array<int, 59> p {};
    int oversampling = 0; // order: ×2^order
    float scale = 0.975f; // output ceiling of the curves (−0.22 dB)
    struct Band
    {
        std::vector<float> gain;  // static gain (output / input) for input levels i/8192
        int size = 0;             // last entry before the curve reaches the ceiling
        double end = 0.0, peak = 1.0;
        std::vector<float> sat;   // soft-saturation table (mode A), 65 538 entries; empty = none
        float satOut = 0.0f, satIn = 0.0f;
        int satB = 0;             // 1: saturation mode B (REA_rodada40 §2.2) with threshold satT and range satR
        float satT = 0.0f, satR = 1.0f, satK = 1.0f;
        int mode = 3;             // effective mode (solo applied, FUN_008417e0)
        bool rms = false;         // envelope mode RMS (REA_rodada40 §2.1)
        float pre = 1.0f, post = 1.0f, g20 = 1.0f;
        std::array<float, 4> matrix { 1.0f, 1.0f, 0.0f, 0.0f };
    };
    std::array<Band, 4> band {};
    bool valid = false;
    bool legacyFir = false; // "Linear-phase filters (legacy mode)": windowed-sinc FIR band split (FUN_0083c200)
    bool linearPhaseSplit = false; // the v14+ linear-phase band split option: not decoded, the normal crossover is used
    MaximusBuild build = MaximusBuild::shared;
};



namespace
{
Point makePoint (double dx, double y, float ten, int type, bool first)
{
    Point pt;
    pt.dx = std::isfinite (dx) && dx > 0.0 ? dx : 0.0;
    pt.y = std::isfinite (y) ? (float) y : 0.0f;
    pt.tension = std::isfinite (ten) ? ten : 0.0f;
    pt.type = type;
    pt.sign = pt.tension == 0.0f ? 0 : (pt.tension < 0.0f ? -1 : 1);
    if (! first)
    {
        if (pt.type < 2)
        {
            auto q = f32 (std::exp ((double) std::fabs (pt.tension) * std::log (1000001.0)) - 1.0);
            if (pt.tension < 0.0f)
                q = -q;
            pt.q = q;
            if (q != 0.0f)
            {
                pt.b = f32 (1.0 / std::fabs ((double) q));
                pt.c = f32 (std::log (std::fabs ((double) q) + 1.0));
            }
        }
        if ((pt.type == 0 || pt.type == 7) && pt.tension == 0.0f)
            pt.sign = 2; // straight segment
    }
    return pt;
}

std::vector<Point> readCurve (const std::vector<std::uint8_t>& d, std::size_t& pos, bool& ok)
{
    std::vector<Point> pts;
    const auto n = i32At (d, pos + 8);
    if (n < 1 || n > 4096 || pos + 12 + 24 * (std::size_t) n + 20 > d.size())
    {
        ok = false;
        return pts;
    }
    for (int k = 0; k < n; ++k)
    {
        const auto at = pos + 12 + 24 * (std::size_t) k;
        double dx = 0.0, y = 0.0;
        float ten = 0.0f;
        std::memcpy (&dx, d.data() + at, 8);
        std::memcpy (&y, d.data() + at + 8, 8);
        std::memcpy (&ten, d.data() + at + 16, 4);
        pts.push_back (makePoint (dx, y, ten, d[at + 20], k == 0));
    }
    pos += 12 + 24 * (std::size_t) n + 20;
    return pts;
}

// FUN_00845b50
void buildGainTable (Curve& curve, float scale, MaximusPreset::Band& b, bool soundgoodizer)
{
    if (soundgoodizer)
    {
        // Soundgoodizer_x64.dll's older build (FUN_0083b050, REA_rodada40 §3.2): the saturated output snaps to the
        // double 0.975 (not the float), the stored end value is the last output before the snap and the peak is
        // divided by the float 0.975
        curve.start();
        b.gain.assign (32770, 0.0f);
        const auto dv9 = 0.5 / std::log (3.0), dv10 = std::log (3.0) * 2.0, half = 0.48750001192092896;
        double prev = 0.0, out = 0.0, mx = 0.0, last = 0.0;
        int lastOk = 0;
        for (int i = 0; i < 32769; ++i)
        {
            const auto lv = (double) std::max (i, 1);
            const auto c = std::log (lv * 0.0001220703125 * 2.0 + 1.0) * dv9;
            const auto y = curve.step (c - prev);
            prev = c;
            out = (std::exp (y * dv10) - 1.0) * half;
            auto idx = i;
            auto keep = out;
            if (std::fabs (out - 0.975) < 5.960464477539063e-08)
                out = 0.975, idx = lastOk, keep = last;
            mx = std::max (mx, out);
            b.gain[(std::size_t) i] = f32 (out / (lv * 0.0001220703125));
            lastOk = idx;
            last = keep;
        }
        b.gain[(std::size_t) lastOk + 1] = b.gain[(std::size_t) lastOk];
        b.gain.resize ((std::size_t) lastOk + 2);
        b.size = lastOk;
        b.end = last;
        b.peak = mx / 0.9750000238418579;
        return;
    }
    curve.start();
    b.gain.assign (32770, 0.0f);
    const auto dv9 = 0.5 / std::log (3.0), dv10 = std::log (3.0) * 2.0, dv14 = (double) scale * 0.5;
    double prev = 0.0, out = 0.0, mx = 0.0;
    int lastOk = 0;
    for (int i = 0; i < 32769; ++i)
    {
        const auto lv = (double) std::max (i, 1);
        const auto c = std::log (lv * 0.0001220703125 * 2.0 + 1.0) * dv9;
        const auto y = curve.step (c - prev);
        prev = c;
        out = (std::exp (y * dv10) - 1.0) * dv14;
        auto idx = i;
        if (std::fabs (out - (double) scale) < 5.960464477539063e-08)
            out = (double) scale, idx = lastOk;
        mx = std::max (mx, out);
        b.gain[(std::size_t) i] = f32 (out / (lv * 0.0001220703125));
        lastOk = idx;
    }
    b.gain[(std::size_t) lastOk + 1] = b.gain[(std::size_t) lastOk];
    b.gain.resize ((std::size_t) lastOk + 2);
    b.size = lastOk;
    b.end = out;
    b.peak = mx / (double) scale;
}

// FUN_00841380, mode A (threshold < 0)
void buildSaturation (int thr, float ceil, double top, MaximusPreset::Band& b, bool soundgoodizer)
{
    const auto p = (double) f32 ((float) (20.0f - f32 ((float) std::log ((double) f32 (f32 (thr * -0.1) + 1.0f)) / std::log (101.0)) * 19.0f));
    const auto ip = 1.0 / p;
    auto d = 1.0;
    if (ceil > 0.0f)
        d = top / ((double) ceil * 65536.0);
    const auto norm = 1.0 / std::pow (std::atan (std::pow (d * 65536.0, p)), ip);
    b.sat.assign (65538, 0.0f);
    for (int i = 0; i <= 65536; ++i)
    {
        auto v = std::pow (std::atan (std::pow (i * d, p)), ip) * norm;
        if (soundgoodizer)
        {
            // FUN_00837d90: a 16-bit table, round(v·32767) clamped to ±32767, read back ×ceiling/32768
            const auto q = std::clamp (std::nearbyint (v * 32767.0), -32767.0, 32767.0);
            b.sat[(std::size_t) i] = (float) q;
            continue;
        }
        if (v < 5.960464477539063e-08)
            v = 0.0;
        b.sat[(std::size_t) i] = (float) v;
    }
    b.sat[65537] = b.sat[65536];
    b.satOut = soundgoodizer ? f32 ((double) ceil * 3.0517578125e-05) : ceil;
    // (post gain and ceiling both at 0 left top = 0: an infinite input scale, then 0 · inf = NaN read as a table index.
    // Round 111: the scale stays finite; that band is silent anyway, its output scale being the ceiling 0.)
    b.satIn = top > 0.0 ? f32 (1.0 / top) : 0.0f;
}
} // namespace

std::optional<std::vector<std::uint8_t>> maximusStateFromFst (const std::vector<std::uint8_t>& b)
{
    if (b.size() < 22 || std::memcmp (b.data(), "FLhd", 4) != 0)
        return std::nullopt;
    auto pos = 8 + (std::size_t) std::max (0, i32At (b, 4));
    if (pos + 8 > b.size() || std::memcmp (b.data() + pos, "FLdt", 4) != 0)
        return std::nullopt;
    pos += 8;
    while (pos < b.size())
    {
        const auto id = b[pos++];
        std::size_t len = 0;
        if (id < 64)
            len = 1;
        else if (id < 128)
            len = 2;
        else if (id < 192)
            len = 4;
        else
        {
            int shift = 0;
            while (pos < b.size())
            {
                const auto c = b[pos++];
                len |= (std::size_t) (c & 0x7f) << shift;
                shift += 7;
                if (! (c & 0x80) || shift > 28)
                    break;
            }
        }
        if (pos + len > b.size())
            return std::nullopt;
        if (id == 0xD5)
            return std::vector<std::uint8_t> (b.begin() + (std::ptrdiff_t) pos, b.begin() + (std::ptrdiff_t) (pos + len));
        pos += len;
    }
    return std::nullopt;
}

std::shared_ptr<const MaximusPreset> makeMaximusPreset (const std::vector<std::uint8_t>& d)
{
    return makeMaximusPreset (d, MaximusBuild::shared);
}

// round 173d3b: Maximus states of version 1…2 (11 of FL's 41 Maximus presets) as the version-3 layout. FUN_0083ef50
// (SG_x64.dll) reads them in four pieces into the same object: 0x18 bytes, then 0x8000 into the next int, 0xD0 bytes,
// 0x18 bytes, 0x8000 into the next int, then 0xE6 bytes (0xE5 for version 1); the v3 block is the same with the two
// ints stored (0x1EE bytes). What follows (the four bands' curves) is unchanged.
static std::vector<std::uint8_t> maximusPreV3AsV3 (const std::vector<std::uint8_t>& d, int version)
{
    const std::size_t last = version < 2 ? 0xe5 : 0xe6, read = 0x18 + 0xd0 + 0x18 + last;
    if (d.size() < 4 + read)
        return {};
    std::vector<std::uint8_t> v;
    v.reserve (d.size() + 8);
    const std::uint8_t header[4] = { 3, 0, 0, 0 }, half[4] = { 0, 0x80, 0, 0 };
    auto copy = [&] (std::size_t from, std::size_t n) { v.insert (v.end(), d.begin() + (std::ptrdiff_t) from, d.begin() + (std::ptrdiff_t) (from + n)); };
    v.insert (v.end(), header, header + 4);
    copy (4, 0x18);
    v.insert (v.end(), half, half + 4);
    copy (4 + 0x18, 0xd0 + 0x18);
    v.insert (v.end(), half, half + 4);
    copy (4 + 0x18 + 0xd0 + 0x18, last);
    if (last == 0xe5)
        v.push_back (0); // (the byte version 1 does not reach keeps the reset value)
    copy (4 + read, d.size() - 4 - read);
    return v;
}

std::vector<std::uint8_t> maximusStateAsV3 (const std::vector<std::uint8_t>& d)
{
    const auto header = d.size() >= 4 ? (std::uint32_t) i32At (d, 0) : 0u;
    const auto version = header > 0xFFFFu ? (int) (header >> 16) : (int) header;
    if (version < 1 || version >= 3)
        return d;
    auto v = maximusPreV3AsV3 (d, version);
    return v.empty() ? d : v;
}

std::shared_ptr<const MaximusPreset> makeMaximusPreset (const std::vector<std::uint8_t>& original, MaximusBuild build)
{
    std::vector<std::uint8_t> converted;
    {
        const auto header = original.size() >= 4 ? (std::uint32_t) i32At (original, 0) : 0u;
        const auto version = header > 0xFFFFu ? (int) (header >> 16) : (int) header;
        if (version >= 1 && version < 3 && (converted = maximusPreV3AsV3 (original, version)).empty())
            return nullptr;
    }
    const auto& d = converted.empty() ? original : converted;
    const auto sgz = build == MaximusBuild::soundgoodizer;
    if (sgz && (std::uint32_t) i32At (d, 0) > 13)
        return nullptr; // that build's loader stops after the fixed block (no v14+ options)
    if (d.size() < 0x1fe + 12)
        return nullptr;
    auto m = std::make_shared<MaximusPreset>();
    m->build = build;
    const auto header = (std::uint32_t) i32At (d, 0);
    const auto version = header > 0xFFFFu ? (int) (header >> 16) : (int) header;
    if (version < 3)
        return nullptr; // (versions 1…2 arrive converted above)
    // fixed block read raw into the object (FUN_0083ef50): 0x1FA bytes for v ≥ 7, 0x1F9 (v6), 0x1F5 (v5), 0x1F1 (v4),
    // 0x1EE (v3); the bytes the block does not reach keep the plugin's reset values (0)
    const std::size_t block = version >= 7 ? 0x1fa : version == 6 ? 0x1f9 : version == 5 ? 0x1f5 : version == 4 ? 0x1f1 : 0x1ee;
    if (d.size() < 4 + block)
        return nullptr;
    auto byteAt = [&] (std::size_t at) { return at < 4 + block ? (int) d[at] : 0; };
    for (int i = 0; i < 59; ++i)
        m->p[(std::size_t) i] = i32At (d, 4 + 4 * (std::size_t) i);
    m->oversampling = byteAt (0x1f4) | (byteAt (0x1f5) << 8) | (byteAt (0x1f6) << 16) | (byteAt (0x1f7) << 24);
    if (m->oversampling < 0 || m->oversampling > 1)
        return nullptr; // only ×1 and ×2 are used by the presets and the demo projects
    // menu options read with the block: "Master Mid mode" (+0x324) is not decoded for rendering; the legacy
    // linear-phase FIR crossover (+0x325) and solo (+0x317) of the editor's band (+0x308) are (REA_rodada40 §2.5)
    if (byteAt (0x1fc) != 0)
        return nullptr;
    m->legacyFir = byteAt (0x1fd) != 0;
    const auto solo = byteAt (0x1ef) != 0;
    const auto soloBand = i32At (d, 0x1e0);
    std::size_t pos = 4 + block;
    if (version >= 14)
    {
        if ((d[pos + 8] & 0x40) != 0)
            m->scale = 1.0f;
        // linear-phase band split (through Frequency Splitter's engine_x64.dll) is not decoded: the exact engine
        // with its normal crossover is far closer than the generic limiter (REA_rodada89 §4.3: LollieVox −5.0 → +0.5 dB)
        m->linearPhaseSplit = (d[pos + 8] & 0x01) != 0;
        pos += 9 + 20;
    }
    if (version >= 15)
        pos += 4;
    std::array<std::vector<Point>, 8> env;
    bool ok = true;
    for (auto& e : env)
        if (ok)
            e = readCurve (d, pos, ok);
    if (! ok)
        return nullptr;

    const auto& p = m->p;
    for (int b = 0; b < 4; ++b)
    {
        auto q = [&] (int j) { return p[(std::size_t) (13 * b + j)]; };
        auto& B = m->band[(std::size_t) b];
        const auto mode = q (7);
        B.pre = l7 (q (8));
        B.post = l7 (q (9));
        // stereo separation (FUN_00616320 with pan 0, volume 1): positive narrows toward mono, negative widens
        if (const auto sep = (float) q (10) * 0.001f; sep != 0.0f)
        {
            auto s = sep < 0.0f ? sep : f32 ((double) f32 (std::exp ((double) sep * std::log (5.0)) - 1.0) * 0.25);
            s = std::min (1.0f, s);
            const auto h = s * 0.5f;
            B.matrix = { 1.0f - h, 1.0f - h, h, h };
        }
        Curve curve (env[(std::size_t) (2 * b)]);
        if (! curve.supported())
            return nullptr;
        buildGainTable (curve, m->scale, B, sgz);
        B.g20 = (B.pre * B.post) * B.gain[0];
        // effective mode (FUN_008417e0): with solo on and another band selected, the master is at least
        // "compression off" and the other bands are muted
        B.mode = mode;
        if (solo && soloBand != 3 && b != soloBand)
            B.mode = std::max (B.mode, b == 3 ? 1 : 2);
        B.rms = q (19) != 0;
        const auto thr = q (11);
        if (thr < 0)
        {
            const auto top0 = (q (19) == 0 && mode < 1) ? B.peak : 4.0;
            const auto ceil = l7 (q (12));
            buildSaturation (thr, ceil, std::max (top0 * (double) B.post, (double) ceil), B, sgz);
        }
        else if (thr > 0)
        {
            // mode B (FUN_00841380 → FUN_00629fd0): threshold t = (1 − thr/1000)·ceiling, range = ceiling − t
            const auto ceil = l7 (q (12));
            B.satB = 1;
            B.satT = f32 ((double) (1.0f - (float) thr * 0.001f) * (double) ceil);
            B.satR = ceil - B.satT;
            if (B.satR <= 0.0f)
                B.satR = 1.5258789e-05f;
            B.satK = 1.0f / B.satR;
        }
    }
    m->valid = true;
    return m;
}

namespace
{
// where envelope `envelope` (0…7) of a Maximus state starts (its 12-byte map header) and ends (after its trailer)
bool maximusEnvelopeSpan (const std::vector<std::uint8_t>& d, int envelope, std::size_t& start, std::size_t& end)
{
    if (envelope < 0 || envelope > 7 || d.size() < 0x1fe + 12)
        return false;
    const auto header = (std::uint32_t) i32At (d, 0);
    const auto version = header > 0xFFFFu ? (int) (header >> 16) : (int) header;
    if (version < 3)
        return false;
    const std::size_t block = version >= 7 ? 0x1fa : version == 6 ? 0x1f9 : version == 5 ? 0x1f5 : version == 4 ? 0x1f1 : 0x1ee;
    auto pos = 4 + block;
    if (version >= 14)
        pos += 9 + 20;
    if (version >= 15)
        pos += 4;
    bool ok = true;
    for (int e = 0; e <= envelope; ++e)
    {
        start = pos;
        readCurve (d, pos, ok);
        if (! ok)
            return false;
    }
    end = pos;
    return true;
}
} // namespace

std::vector<MaximusCurvePoint> maximusCurve (const std::vector<std::uint8_t>& d, int envelope)
{
    std::vector<MaximusCurvePoint> out;
    std::size_t start = 0, end = 0;
    if (! maximusEnvelopeSpan (d, envelope, start, end))
        return out;
    const auto n = i32At (d, start + 8);
    for (int k = 0; k < n; ++k)
    {
        const auto at = start + 12 + 24 * (std::size_t) k;
        MaximusCurvePoint p;
        double y = 0.0;
        std::memcpy (&p.dx, d.data() + at, 8);
        std::memcpy (&y, d.data() + at + 8, 8);
        std::memcpy (&p.tension, d.data() + at + 16, 4);
        p.type = d[at + 20];
        if (! std::isfinite (p.dx) || p.dx < 0.0)
            p.dx = 0.0;
        p.y = std::isfinite (y) ? (float) y : 0.0f;
        if (! std::isfinite (p.tension))
            p.tension = 0.0f;
        out.push_back (p);
    }
    return out;
}

std::vector<std::uint8_t> withMaximusCurve (const std::vector<std::uint8_t>& d, int envelope, const std::vector<MaximusCurvePoint>& points)
{
    std::size_t start = 0, end = 0;
    if (points.empty() || points.size() > 4096 || ! maximusEnvelopeSpan (d, envelope, start, end))
        return d;
    const auto n = i32At (d, start + 8);
    std::vector<std::uint8_t> out (d.begin(), d.begin() + (std::ptrdiff_t) start + 8); // the map's kind and version
    auto put = [&out] (const void* v, std::size_t len) { const auto* b = (const std::uint8_t*) v; out.insert (out.end(), b, b + len); };
    const auto count = (std::int32_t) points.size();
    put (&count, 4);
    // a point read from the state and given back unchanged keeps its stored record bit for bit (the level is a double
    // there; the direction cache as FL wrote it), searched in order so inserted / removed points don't shift the rest
    std::size_t next = 0;
    auto unchanged = [&] (const MaximusCurvePoint& p, std::size_t& at) {
        for (auto j = next; j < (std::size_t) std::max (0, n); ++j)
        {
            const auto rec = start + 12 + 24 * j;
            double dx = 0.0, y = 0.0;
            float ten = 0.0f;
            std::memcpy (&dx, d.data() + rec, 8);
            std::memcpy (&y, d.data() + rec + 8, 8);
            std::memcpy (&ten, d.data() + rec + 16, 4);
            if (dx == p.dx && (float) y == p.y && ten == p.tension && d[rec + 20] == p.type)
            {
                at = rec;
                next = j + 1;
                return true;
            }
        }
        return false;
    };
    for (std::size_t k = 0; k < points.size(); ++k)
    {
        const auto& p = points[k];
        if (std::size_t rec = 0; unchanged (p, rec))
        {
            out.insert (out.end(), d.begin() + (std::ptrdiff_t) rec, d.begin() + (std::ptrdiff_t) rec + 24);
            continue;
        }
        const auto y = (double) p.y;
        put (&p.dx, 8);
        put (&y, 8);
        put (&p.tension, 4);
        out.push_back (p.type);
        out.push_back (0);
        out.push_back (0);
        // FL's direction cache: 2 = straight (types 0 / 7 without tension), else the tension's sign
        std::uint8_t dir = p.tension == 0.0f ? 0 : (p.tension < 0.0f ? 0xff : 1);
        if (k > 0 && (p.type == 0 || p.type == 7) && p.tension == 0.0f)
            dir = 2;
        out.push_back (dir);
    }
    const auto trailer = start + 12 + 24 * (std::size_t) n;
    out.insert (out.end(), d.begin() + (std::ptrdiff_t) trailer, d.end());
    return out;
}

bool maximusCurveTypeSupported (int type) noexcept { return type == 0 || type == 7; }

std::vector<float> maximusCurveShape (const std::vector<MaximusCurvePoint>& points, int n)
{
    std::vector<float> out;
    if (points.empty() || n < 1)
        return out;
    std::vector<Point> pts;
    for (std::size_t k = 0; k < points.size(); ++k)
    {
        if (k > 0 && ! maximusCurveTypeSupported (points[k].type))
            return out;
        pts.push_back (makePoint (points[k].dx, (double) points[k].y, points[k].tension, points[k].type, k == 0));
    }
    Curve curve (std::move (pts));
    curve.start();
    out.reserve ((std::size_t) n + 1);
    double prev = 0.0;
    for (int i = 0; i <= n; ++i)
    {
        const auto x = (double) i / (double) n;
        out.push_back ((float) curve.step (x - prev));
        prev = x;
    }
    return out;
}

std::vector<std::vector<std::pair<double, float>>> maximusEnvelopes (const std::vector<std::uint8_t>& d)
{
    // the same walk as makeMaximusPreset: fixed block by version, the v14 / v15 options, then 8 envelopes
    std::vector<std::vector<std::pair<double, float>>> out;
    if (d.size() < 0x1fe + 12)
        return out;
    const auto header = (std::uint32_t) i32At (d, 0);
    const auto version = header > 0xFFFFu ? (int) (header >> 16) : (int) header;
    if (version < 3)
        return out;
    const std::size_t block = version >= 7 ? 0x1fa : version == 6 ? 0x1f9 : version == 5 ? 0x1f5 : version == 4 ? 0x1f1 : 0x1ee;
    auto pos = 4 + block;
    if (version >= 14)
        pos += 9 + 20;
    if (version >= 15)
        pos += 4;
    bool ok = true;
    for (int e = 0; e < 8 && ok; ++e)
    {
        const auto pts = readCurve (d, pos, ok);
        if (! ok)
            break;
        std::vector<std::pair<double, float>> curve;
        for (const auto& p : pts)
            curve.emplace_back (p.dx, p.y);
        out.push_back (std::move (curve));
    }
    if (! ok)
        out.clear();
    return out;
}

namespace
{
// FUN_00624580 (types 1 low-pass, 4 high-pass, 5 all-pass) + FUN_00624e90 (stereo direct form I in double, the
// states flushed below 2^-24 at the end of every block, kept in double)
struct Biquad
{
    int type = 1;
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, 8> s {}; // per channel: x1 x2 y1 y2

    void design (int kind, double w, double alpha) noexcept
    {
        type = kind;
        const auto sn = std::sin (w), cs = std::cos (w);
        const auto a = alpha * sn;
        const auto norm = 1.0 / (a + 1.0);
        a1 = norm * cs * -2.0;
        a2 = norm * (1.0 - a);
        if (kind == 1)
            b1 = norm * (1.0 - cs), b0 = b1 * 0.5, b2 = b0;
        else if (kind == 4)
            b1 = norm * -(cs + 1.0), b0 = -b1 * 0.5, b2 = b0;
        else
            b1 = a1, b0 = a2, b2 = 1.0;
    }

    void reset() noexcept { s = {}; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        // both channels in one loop (two independent recursions; the same arithmetic per channel)
        auto lx1 = s[0], lx2 = s[1], ly1 = s[2], ly2 = s[3], rx1 = s[4], rx2 = s[5], ry1 = s[6], ry2 = s[7];
        for (int i = 0; i < n; ++i)
        {
            const auto xl = (double) inL[i], xr = (double) inR[i];
            const auto yl = xl * b0 + (((lx1 * b1 - ly2 * a2) + lx2 * b2) - ly1 * a1);
            const auto yr = xr * b0 + (((rx1 * b1 - ry2 * a2) + rx2 * b2) - ry1 * a1);
            outL[i] = (float) yl, outR[i] = (float) yr;
            lx2 = lx1, lx1 = xl, ly2 = ly1, ly1 = yl;
            rx2 = rx1, rx1 = xr, ry2 = ry1, ry1 = yr;
        }
        s = { flushState (lx1), flushState (lx2), flushState (ly1), flushState (ly2),
              flushState (rx1), flushState (rx2), flushState (ry1), flushState (ry2) };
    }
};

// alpha = sin·k: order 0 (12 dB stage) k = 1, order 1 (24 dB, two stages) k = 1/√2
double crossoverAlpha (int order) noexcept { return order == 1 ? 0.70710678118654746 : 1.0; }

// one 2:1 stage (FUN_0083a9c0 up, FUN_0083ab90 down): two cascades of three first-order all-passes
struct Halfband
{
    static constexpr double ka[3] { 0.14737711360104661, 0.48246854276970014, 0.88300502576937312 };
    static constexpr double kb[3] { 0.039151597734460045, 0.30264684832849342, 0.6746159185469639 };
    std::array<double, 24> s {}; // per channel 12: A outputs, A inputs, B outputs, B inputs

    void reset() noexcept { s = {}; }

    static double chain (double* st, double x, const double* k) noexcept
    {
        const auto y1 = st[3] + (x - st[0]) * k[0];
        st[3] = x;
        const auto y2 = st[4] + (y1 - st[1]) * k[1];
        st[4] = y1;
        const auto y3 = st[5] + (y2 - st[2]) * k[2];
        st[5] = y2;
        st[0] = y1, st[1] = y2, st[2] = y3;
        return y3;
    }

    void endBlock() noexcept
    {
        for (int c = 0; c < 2; ++c)
            for (int k : { 0, 1, 2, 6, 7, 8 })
                s[(std::size_t) (12 * c + k)] = flushState (s[(std::size_t) (12 * c + k)]);
    }

    void up (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        const float* in[2] { inL, inR };
        float* out[2] { outL, outR };
        for (int c = 0; c < 2; ++c)
        {
            auto* st = s.data() + 12 * c;
            for (int i = 0; i < n; ++i)
            {
                const auto v = (double) in[c][i];
                const auto a = chain (st, v, ka);
                const auto b = chain (st + 6, v, kb);
                out[c][2 * i] = (float) b;
                out[c][2 * i + 1] = (float) a;
            }
        }
        endBlock();
    }

    void down (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept // n output frames
    {
        const float* in[2] { inL, inR };
        float* out[2] { outL, outR };
        for (int c = 0; c < 2; ++c)
        {
            auto* st = s.data() + 12 * c;
            for (int i = 0; i < n; ++i)
            {
                const auto a = chain (st, (double) in[c][2 * i], ka);
                const auto b = chain (st + 6, (double) in[c][2 * i + 1], kb);
                out[c][i] = (float) ((b + a) * 0.5);
            }
        }
        endBlock();
    }
};

// FUN_00605410 (x87): one-pole on a control signal, c0 while the input is ≥ the state, c1 otherwise. The state stays
// on the x87 stack (64-bit mantissa) through the block, the coefficients are doubles and only the output samples are
// rounded to float; between blocks the state is kept as a double, 0 below 2^-24 (REA_rodada40 §2.4). long double is
// the 80-bit x87 format on the x86-64 GCC/Clang/MinGW targets LYNX is built for.
struct Smoother
{
    double c0 = 0.0, c1 = 0.0, s = 0.0;

    void run (float* x, int n) noexcept
    {
        long double st = s;
        if (c0 == c1)
        {
            const long double c = c0;
            for (int i = 0; i < n; ++i)
            {
                const long double v = x[i];
                st = (st - v) * c + v;
                x[i] = (float) st;
            }
        }
        else
        {
            const long double k0 = c0, k1 = c1;
            for (int i = 0; i < n; ++i)
            {
                const long double v = x[i];
                const auto c = v >= st ? k0 : k1;
                st = (st - v) * c + v;
                x[i] = (float) st;
            }
        }
        s = std::fabs ((double) st) > (double) flushLevel ? (double) st : 0.0;
    }
};

// FUN_00629710 + FUN_007806b0: max(|L|, |R|), then the running maximum over the last W samples
struct PeakWindow
{
    int W = 1, pos = 0, head = 0, tail = 0, count = 0;
    std::vector<std::int32_t> ring, dq;

    void setLength (int w)
    {
        W = std::max (1, w);
        ring.assign ((std::size_t) W, 0);
        dq.assign ((std::size_t) W, 0);
        pos = 0, head = W > 1 ? 1 : 0, tail = 0, count = 0;
    }

    void run (const float* L, const float* R, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            out[i] = std::max (std::fabs (L[i]), std::fabs (R[i]));
        if (W <= 1)
            return;
        for (int i = 0; i < n; ++i)
        {
            std::int32_t x = 0;
            std::memcpy (&x, out + i, 4);
            auto next = pos + 1;
            if (next >= W)
                next = 0;
            const auto old = count < 1 ? ring[(std::size_t) pos] : ring[(std::size_t) dq[(std::size_t) head]];
            if (ring[(std::size_t) pos] < x)
            {
                while (count > 0)
                {
                    if (x <= ring[(std::size_t) dq[(std::size_t) tail]])
                    {
                        const auto j = dq[(std::size_t) head] - 1;
                        if (next == j || next == j + W)
                        {
                            --count;
                            if (++head >= W)
                                head = 0;
                        }
                        break;
                    }
                    --count;
                    if (--tail < 0)
                        tail = W - 1;
                }
            }
            else
            {
                if (++tail >= W)
                    tail = 0;
                dq[(std::size_t) tail] = pos;
                const auto j = dq[(std::size_t) head] - 1;
                if (next == j || next == j + W)
                {
                    if (++head >= W)
                        head = 0;
                }
                else
                    ++count;
            }
            ring[(std::size_t) next] = x;
            if (x < old)
                x = old;
            std::memcpy (out + i, &x, 4);
            pos = next;
        }
    }
};

// the RMS detector (envelope mode 1; SG class at 0x780020, process FUN_00780b90 with the window kernel FUN_00780a60;
// REA_rodada38, REA_rodada40 §2.1): L² + R² summed over the last W samples in an x87 running sum (kept as a double
// between blocks), scaled by 1/(2W), clamped below at 0 (ippsThreshold_LT_32f_I) and square-rooted (ippsSqrt_32f_I)
struct RmsWindow
{
    int W = 1, idx = 0;
    double sum = 0.0;
    float scale = 0.5f;
    std::vector<float> ring;

    void setLength (int w)
    {
        W = std::max (1, w);
        ring.assign ((std::size_t) W, 0.0f);
        scale = (float) (1.0 / (double) (W * 2));
        reset();
    }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        idx = 0;
        sum = 0.0;
    }

    void run (const float* L, const float* R, float* out, int n) noexcept
    {
        // the running sum stays in an x87 register over the block (fldl … fstpl): extended precision, saved as a double
        auto acc = (long double) sum;
        auto k = idx;
        for (int i = 0; i < n; ++i)
        {
            const auto x = L[i] * L[i] + R[i] * R[i];
            if (++k >= W)
                k = 0;
            acc = (acc - (long double) ring[(std::size_t) k]) + (long double) x;
            ring[(std::size_t) k] = x;
            out[i] = (float) (acc * (long double) scale);
        }
        sum = (double) acc;
        idx = k;
        for (int i = 0; i < n; ++i)
            out[i] = std::sqrt (std::max (out[i], 0.0f));
    }
};

// saturation mode B (FUN_0062a090, x87): above the threshold t, |y| = t + range·(1 − e^((t − |x|)/range)), sign kept
inline float saturateB (float x, float t, float range, float k) noexcept
{
    const auto a = std::fabs (x);
    if (! (a > t))
        return x;
    const auto e = std::exp ((long double) ((long double) t - (long double) a) * (long double) k);
    const auto y = (float) ((1.0L - e) * (long double) range + (long double) t);
    return std::signbit (x) ? -y : y;
}

// FUN_0077f350: L-frame delay (swap ring)
struct FrameDelay
{
    std::vector<float> l, r;
    int L = 0, p = 0;

    void setLength (int n)
    {
        L = std::max (0, n);
        l.assign ((std::size_t) std::max (1, L), 0.0f);
        r.assign ((std::size_t) std::max (1, L), 0.0f);
        p = 0;
    }

    void reset() noexcept
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        p = 0;
    }

    void run (float* xl, float* xr, int n) noexcept
    {
        if (L == 0)
            return;
        for (int i = 0; i < n; ++i)
        {
            std::swap (xl[i], l[(std::size_t) p]);
            std::swap (xr[i], r[(std::size_t) p]);
            if (++p >= L)
                p = 0;
        }
    }
};
} // namespace

namespace
{
// the legacy linear-phase band split (REA_rodada40 §2.5): FUN_0083c0d0 (windowed sinc), FUN_0062ac70 type 0 (4-term
// Blackman-Harris), FUN_0083c070 (spectral inversion), FUN_0083c200 (placement in the N-tap buffer), taps converted to
// float and run by ippsFIR_32f per channel (FUN_0083bce0)
std::vector<double> sincLowpass (int L, double fc)
{
    std::vector<double> h ((std::size_t) L);
    const auto w = fc * 6.283185307179586;
    for (int i = 0; i < L; ++i)
    {
        if (i == (L - 1) / 2)
            h[(std::size_t) i] = fc * 2.0;
        else
        {
            const auto d = (double) ((float) i - (float) (L - 1) * 0.5f);
            h[(std::size_t) i] = std::sin (w * d) / (d * 3.141592653589793);
        }
    }
    const auto a = 6.283185307179586 / (double) (L - 1);
    for (int i = 0; i < L; ++i)
    {
        const auto c1 = std::cos (a * (double) i), c2 = std::cos (a * 2.0 * (double) i), c3 = std::cos (a * 3.0 * (double) i);
        h[(std::size_t) i] *= ((0.35875 - c1 * 0.48829) + c2 * 0.14128) - c3 * 0.01168;
    }
    return h;
}

void invert (std::vector<double>& h)
{
    for (auto& v : h)
        v *= -1.0;
    h[(h.size() - 1) / 2] += 1.0;
}

// kind 0 low-pass (f1, L1), 3 high-pass (f1, L1), 1 band-pass (f1, L1 / f2, L2); placed in the middle of N taps
std::vector<float> legacyTaps (int N, int kind, double f1, int L1, double f2, int L2)
{
    std::vector<double> buf ((std::size_t) N, 0.0);
    f1 = std::min (f1, 0.5), f2 = std::min (f2, 0.5);
    L1 |= 1, L2 |= 1;
    auto place = [&] (std::vector<double>& to, const std::vector<double>& h) {
        const auto at = (N - (int) h.size()) / 2;
        for (std::size_t i = 0; i < h.size(); ++i)
            to[(std::size_t) at + i] = h[i];
    };
    if (kind == 0)
        place (buf, sincLowpass (L1, f1));
    else if (kind == 3)
    {
        auto h = sincLowpass (L1, f1);
        invert (h);
        place (buf, h);
    }
    else
    {
        std::vector<double> hi ((std::size_t) N, 0.0);
        place (buf, sincLowpass (L1, f1));
        auto h = sincLowpass (L2, f2);
        invert (h);
        place (hi, h);
        for (int i = 0; i < N; ++i)
            buf[(std::size_t) i] += hi[(std::size_t) i];
        if (kind == 1)
            invert (buf);
    }
    std::vector<float> taps ((std::size_t) N);
    for (int i = 0; i < N; ++i)
        taps[(std::size_t) i] = (float) buf[(std::size_t) i];
    return taps;
}

struct Fir
{
    std::vector<float> taps, rev;           // rev: the taps in time order of the history (oldest first)
    std::array<std::vector<float>, 2> hist; // the last N − 1 inputs, then the block

    void reset() noexcept
    {
        for (auto& h : hist)
            std::fill (h.begin(), h.end(), 0.0f);
    }

    void prepare (std::vector<float> t, int maxFrames)
    {
        taps = std::move (t);
        rev.assign (taps.rbegin(), taps.rend());
        for (auto& h : hist)
            h.assign (taps.size() - 1 + (std::size_t) maxFrames, 0.0f);
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        const auto N = (int) taps.size();
        const float* in[2] { inL, inR };
        float* out[2] { outL, outR };
        for (int c = 0; c < 2; ++c)
        {
            auto& h = hist[(std::size_t) c];
            std::copy (in[c], in[c] + n, h.begin() + (N - 1));
            // double accumulation in four interleaved partial sums (IPP's own summation order is not known; the
            // difference stays far below the float output's rounding)
            const float* rt = rev.data();
            for (int i = 0; i < n; ++i)
            {
                const float* x = h.data() + i;
                double a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
                int k = 0;
                for (; k + 4 <= N; k += 4)
                {
                    a0 += (double) rt[k] * (double) x[k];
                    a1 += (double) rt[k + 1] * (double) x[k + 1];
                    a2 += (double) rt[k + 2] * (double) x[k + 2];
                    a3 += (double) rt[k + 3] * (double) x[k + 3];
                }
                for (; k < N; ++k)
                    a0 += (double) rt[k] * (double) x[k];
                out[c][i] = (float) ((a0 + a1) + (a2 + a3));
            }
            std::copy (h.begin() + n, h.begin() + n + (N - 1), h.begin());
        }
    }
};
} // namespace

class Maximus
{
public:
    static constexpr int maxFrames = Rack::maxBlock * 8;

    void prepare (std::shared_ptr<const MaximusPreset> preset, double rate, int mixParam)
    {
        pr = std::move (preset);
        fs = rate;
        const auto& P = *pr;
        const auto& p = P.p;
        K = 1 << P.oversampling;
        mix = (float) mixParam * 0.001f;
        mixOn = mix != 1.0f;
        std::array<int, 4> mode {};
        for (int b = 0; b < 4; ++b)
            mode[(std::size_t) b] = P.band[(std::size_t) b].mode;
        const auto anyComp = mode[0] < 1 || mode[1] < 1 || mode[2] < 1;
        int latency = 0;
        for (int b = 0; b < 4; ++b)
        {
            auto q = [&] (int j) { return p[(std::size_t) (13 * b + j)]; };
            auto& B = band[(std::size_t) b];
            B.mode = mode[(std::size_t) b];
            const auto k = b == 3 ? 1 : K;
            int L = 0;
            if (B.mode < 2 && (b != 3 || B.mode < 1) && (b > 2 || anyComp))
                L = (int) rint (tms (b == 3 ? q (13) : p[4]) * 2.0 * 0.001 * fs) * k;
            B.L = L;
            if (b < 3)
                latency = std::max (latency, L);
            const auto a = q (13);
            const auto A = a == 0 ? (double) L : tms (a) * 2.0 * 0.001 * fs * k;
            const auto na = std::max (1, q (15));
            const auto attN = (A * 0.800000011920929) / std::sqrt ((double) na);
            const auto rel2N = q (14) == 0 ? attN : (tms (q (14)) * 2.0 * 0.001 * fs * k * 2.0) / std::sqrt ((double) na);
            const auto sus = (int) rint (tms (q (18)) * 2.0 * 0.001 * fs);
            B.det.setLength (std::max (std::max (L, 1), sus * k));
            B.rms = P.band[(std::size_t) b].rms;
            if (B.rms)
                B.rmsDet.setLength (std::max (std::max (L, 1), sus * k));
            B.att.assign ((std::size_t) na, Smoother { coefFor (rel2N), coefFor (attN), 0.0 });
            const auto R = tms (q (16)) * 2.0 * 0.001 * fs * k;
            B.rel.clear();
            if (R > 0.0)
            {
                const auto nr = std::max (1, q (17));
                B.rel.assign ((std::size_t) nr, Smoother { 0.0, coefFor ((R * 2.0) / std::sqrt ((double) nr)), 0.0 });
            }
            B.delay.setLength (L);
        }
        const auto latencyBands = latency;
        latency = latency / K + band[3].L;
        reportedLatency = latency;
        tailLength = latency + (int) rint (fs * 1.5);

        // crossover (FUN_00844e10)
        auto freq = [&] (int v) { return f32 (std::exp ((double) v * 0.00013822774794088088 * 0.69314718055994529) * 30.0); };
        auto fL = freq (p[0]), fH = freq (p[2]);
        const auto sL = mode[0] != 3 ? std::clamp (p[1], 0, 1) : -1;
        const auto sH = mode[2] != 3 ? std::clamp (p[3], 0, 1) : -1;
        if (sL >= 0 && sH >= 0 && fH < fL)
            std::swap (fL, fH);
        first = 1;
        for (int b = 0; b < 3; ++b)
            if (mode[(std::size_t) b] != 3)
            {
                first = b;
                break;
            }
        auto polarity = [] (int s) { return s < 0 ? 0.0f : s == 0 ? -1.0f : 1.0f; };
        band[0].g18 = polarity (sL), band[1].g18 = 1.0f, band[2].g18 = polarity (sH), band[3].g18 = 1.0f;
        const auto rateB = fs * K, fmax = (double) f32 (0.4999 * rateB), w = 2.0 * pi / rateB;
        auto stage = [&] (int type, float f, int order) {
            Biquad q;
            const auto fc = std::min ((double) f, fmax);
            q.design (type, fc * w, crossoverAlpha (order));
            return q;
        };
        for (auto& s : stages)
            s.clear();
        // the low band's all-pass at the high crossover (phase match with a 24 dB high split) is absent from the
        // older build in Soundgoodizer_x64.dll (FUN_0083a4a0: sL + 1 low-pass stages only)
        const auto allPass = sH > 0 && P.build == MaximusBuild::shared;
        const auto n0 = sL + (allPass ? 1 : 0) + 1;
        for (int i = 0; i < n0; ++i)
            stages[0].push_back (i == n0 - 1 && allPass ? stage (5, fH, 0) : stage (1, fL, sL));
        for (int i = 0; i <= sL; ++i)
            stages[1].push_back (stage (4, fL, sL));
        for (int i = 0; i <= sH; ++i)
            stages[1].push_back (stage (1, fH, sH));
        for (int i = 0; i <= sH; ++i)
            stages[2].push_back (stage (4, fH, sH));
        inPlaceStages = std::min (sL + 1, (int) stages[1].size() - 1);
        firOn = P.legacyFir;
        if (firOn)
        {
            // FUN_00840bb0: N = round(0.004·band rate) | 1 (≥ 5); FUN_00844e10: per split round(K·c·(slope + 1)/f) | 1
            // (≥ 5, ≤ N) with c = 27 500 (the high split of Soundgoodizer_x64.dll's build: 110 000)
            auto N = (int) rint (rateB * 0.004) | 1;
            N = std::max (N, 5);
            auto len = [&] (double c, int slope, float f) {
                const auto v = (double) K * c * (double) (slope + 1) / (double) f;
                auto L = (int) rint (v);
                L = (L | 1) < 6 ? 5 : (L | 1);
                return std::min (L, N);
            };
            const auto Ll = len (27500.0, sL, fL);
            const auto Lh = len (P.build == MaximusBuild::soundgoodizer ? 110000.0 : 27500.0, sH, fH);
            const auto nl = (double) (float) (fL / (float) rateB), nh = (double) (float) (fH / (float) rateB);
            fir[0].prepare (legacyTaps (N, 0, nl, Ll, 0.0, 0), maxFrames);
            if (sL < 0)
                fir[1].prepare (sH >= 0 ? legacyTaps (N, 0, nh, Lh, 0.0, 0) : std::vector<float> ((std::size_t) N, 0.0f), maxFrames);
            else if (sH < 0)
                fir[1].prepare (legacyTaps (N, 3, nl, Ll, 0.0, 0), maxFrames);
            else
                fir[1].prepare (legacyTaps (N, 1, nl, Ll, nh, Lh), maxFrames);
            fir[2].prepare (legacyTaps (N, 3, nh, Lh, 0.0, 0), maxFrames);
            band[0].g18 = band[2].g18 = 1.0f;
            firLatency = (N - 1) / 2;
            reportedLatency = (latencyBands + firLatency) / K + band[3].L;
            tailLength = reportedLatency + (int) rint (fs * 1.5);
        }

        // low cut: four high-pass stages at 0.8·f (FUN_00840b40, FUN_0083a5c0)
        lowCut.clear();
        if (p[6] >= 0)
        {
            const auto f = f32 (std::exp ((double) p[6] * 6.103515625e-05 * 0.69314718055994529) * 5.0);
            for (const auto k : { 0.19771184027194977, 0.61932772397995, 0.6768445372581482, 1.0793569087982178 })
            {
                Biquad q;
                q.design (4, ((double) f * 0.8) * (2.0 * pi / fs), k);
                lowCut.push_back (q);
            }
        }
        for (auto& h : up)
            h.reset();
        for (auto& h : down)
            h.reset();
        reset();
        counter = -1073741823;
    }

    void reset() noexcept
    {
        for (int b = 0; b < 4; ++b)
        {
            auto& B = band[(std::size_t) b];
            for (auto& s : B.att)
                s.s = pr->band[(std::size_t) b].g20;
            for (auto& s : B.rel)
                s.s = 0.0f;
            B.delay.reset(); // (the detector keeps its window: SG's reset FUN_00845940 does not touch it)
        }
        for (auto& st : stages)
            for (auto& q : st)
                q.reset();
        for (auto& q : lowCut)
            q.reset();
        for (auto& f : fir)
            f.reset();
        for (auto& h : up)
            h.reset();
        for (auto& h : down)
            h.reset();
    }

    // silence fed while the rack is idle (Eff_Render's silent-block counter, then the reset after 1.5 s)
    void silence (int n) noexcept
    {
        counter -= n;
        if (! (counter > -n))
        {
            if (counter > -0x3fffffff)
                reset();
            counter = -1073741823;
        }
    }

    void process (float* L, float* R, int n) noexcept
    {
        bool silent = true;
        for (int i = 0; i < n && silent; ++i)
            silent = std::fabs (L[i]) <= 9.999999974752427e-07f && std::fabs (R[i]) <= 9.999999974752427e-07f;
        if (! silent)
            counter = tailLength;
        else
            counter -= n;
        if (! (counter > -n))
        {
            if (counter > -0x3fffffff)
                reset();
            counter = -1073741823;
            return;
        }
        for (auto& q : lowCut)
            q.process (L, R, L, R, n);
        // X: the signal being split (at the band rate)
        const auto nb = n * K;
        float* XL = L;
        float* XR = R;
        if (K > 1)
        {
            up[0].up (L, R, xl.data(), xr.data(), n);
            XL = xl.data(), XR = xr.data();
        }
        bool haveSum = false;
        for (int b = 0; b < 3; ++b)
        {
            auto& st = stages[(std::size_t) b];
            float* yl = bl.data();
            float* yr = br.data();
            if (st.empty())
                std::copy (XL, XL + nb, yl), std::copy (XR, XR + nb, yr);
            else if (firOn) // the legacy linear-phase split: every band from the input (FUN_0083bce0)
                fir[(std::size_t) b].process (XL, XR, yl, yr, nb);
            else if (b == 1 && b != first)
            {
                const float* sl = XL;
                const float* sr = XR;
                for (int k = 0; k < (int) st.size(); ++k)
                {
                    float* dl = k < inPlaceStages ? XL : yl;
                    float* dr = k < inPlaceStages ? XR : yr;
                    st[(std::size_t) k].process (sl, sr, dl, dr, nb);
                    sl = dl, sr = dr;
                }
                if (st.size() <= (std::size_t) inPlaceStages) // (not reached with the presets: kept literal)
                    std::copy (sl, sl + nb, yl), std::copy (sr, sr + nb, yr);
            }
            else
            {
                const float* sl = XL;
                const float* sr = XR;
                for (auto& q : st)
                {
                    q.process (sl, sr, yl, yr, nb);
                    sl = yl, sr = yr;
                }
            }
            auto& B = band[(std::size_t) b];
            processBand (b, yl, yr, nb);
            if (B.mode < 2)
            {
                if (! haveSum && b == 2)
                {
                    // the high band is the first one on: its buffer is the sum buffer, and the final add (ippsAdd_32f
                    // of the band and the sum) doubles it (FUN_00843a40)
                    for (int i = 0; i < nb; ++i)
                        sl[(std::size_t) i] = yl[i] + yl[i], sr[(std::size_t) i] = yr[i] + yr[i];
                    haveSum = true;
                }
                else if (! haveSum)
                    std::copy (yl, yl + nb, sl.data()), std::copy (yr, yr + nb, sr.data()), haveSum = true;
                else
                    for (int i = 0; i < nb; ++i)
                        sl[(std::size_t) i] = sl[(std::size_t) i] + yl[i], sr[(std::size_t) i] = sr[(std::size_t) i] + yr[i];
            }
            else if (b == first)
            {
                std::fill (sl.begin(), sl.begin() + nb, 0.0f), std::fill (sr.begin(), sr.begin() + nb, 0.0f);
                haveSum = true;
            }
        }
        if (! haveSum)
            std::fill (sl.begin(), sl.begin() + nb, 0.0f), std::fill (sr.begin(), sr.begin() + nb, 0.0f);
        if (K > 1)
            down[0].down (sl.data(), sr.data(), L, R, n);
        else
            std::copy (sl.begin(), sl.begin() + n, L), std::copy (sr.begin(), sr.begin() + n, R);
        processBand (3, L, R, n);
    }

private:
    struct BandState
    {
        int mode = 3, L = 0;
        float g18 = 1.0f;
        bool rms = false;
        PeakWindow det;
        RmsWindow rmsDet;
        std::vector<Smoother> att, rel;
        FrameDelay delay;
    };

    void processBand (int b, float* xl, float* xr, int n) noexcept
    {
        auto& B = band[(std::size_t) b];
        const auto& S = pr->band[(std::size_t) b];
        if (B.mode < 2)
        {
            const auto& m = S.matrix;
            if (m[2] != 0.0f || m[3] != 0.0f)
                for (int i = 0; i < n; ++i)
                {
                    const auto l = xl[i], r = xr[i];
                    xl[i] = l * m[0] + r * m[2];
                    xr[i] = r * m[1] + l * m[3];
                }
            else if (m[0] != 1.0f || m[1] != 1.0f)
                for (int i = 0; i < n; ++i)
                    xl[i] = xl[i] * m[0], xr[i] = xr[i] * m[1];
        }
        float* g = gain.data();
        if (B.mode == 0)
        {
            if (B.rms)
                B.rmsDet.run (xl, xr, g, n);
            else
                B.det.run (xl, xr, g, n);
            for (auto& s : B.rel)
                s.run (g, n);
            const auto k = f32 ((double) S.pre * 8192.25);
            const auto pp = S.pre * S.post;
            const auto hiGain = f32 (S.end * (double) S.post);
            const auto* t = S.gain.data();
            for (int i = 0; i < n; ++i)
            {
                const auto lv = g[i];
                const auto idxf = lv * k;
                auto idx = (int) idxf; // ippsConvert_32f32s_Sfs, rounding toward zero
                if (idx < 0)
                    idx = 0;
                if (idx > S.size)
                    g[i] = hiGain / lv;
                else
                {
                    const auto fr = idxf - (float) idx;
                    g[i] = (t[idx] + (t[idx + 1] - t[idx]) * fr) * pp;
                }
            }
            for (auto& s : B.att)
                s.run (g, n);
        }
        B.delay.run (xl, xr, n);
        const auto keepDry = mixOn && b < 3;
        if (keepDry)
            std::copy (xl, xl + n, dl.data()), std::copy (xr, xr + n, dr.data());
        if (B.mode == 0)
            for (int i = 0; i < n; ++i)
                xl[i] = xl[i] * g[i], xr[i] = xr[i] * g[i];
        else
            for (int i = 0; i < n; ++i)
                xl[i] = xl[i] * S.post, xr[i] = xr[i] * S.post;
        if (! S.sat.empty() && B.mode < 2)
        {
            // FUN_0062a240 (x87, truncating control word): odd table read with linear interpolation
            const auto k = (double) S.satIn * 65536.0;
            const auto* t = S.sat.data();
            auto sat = [&] (float x) {
                const auto v = (double) x * k;
                const auto a = std::fabs (v);
                if (! (a < 65536.0)) // (also NaN / inf: never an index outside the table)
                    return (float) ((v < 0.0 ? -(double) t[65536] : (double) t[65536]) * (double) S.satOut);
                auto i = (long long) a;
                const auto fr = a - (double) i;
                const auto y = (double) t[i] + ((double) t[i + 1] - (double) t[i]) * fr;
                return (float) ((v < 0.0 ? -y : y) * (double) S.satOut);
            };
            for (int i = 0; i < n; ++i)
                xl[i] = sat (xl[i]), xr[i] = sat (xr[i]);
        }
        else if (S.satB != 0 && B.mode < 2)
            for (int i = 0; i < n; ++i)
                xl[i] = saturateB (xl[i], S.satT, S.satR, S.satK), xr[i] = saturateB (xr[i], S.satT, S.satR, S.satK);
        if (b == 3)
        {
            if (B.mode == 2)
                std::fill (xl, xl + n, 0.0f), std::fill (xr, xr + n, 0.0f);
            return;
        }
        if (B.mode < 2)
        {
            if (mixOn)
            {
                const auto a = mix * B.g18, d = (1.0f - mix) * B.g18;
                for (int i = 0; i < n; ++i)
                {
                    xl[i] = xl[i] * a + dl[(std::size_t) i] * d;
                    xr[i] = xr[i] * a + dr[(std::size_t) i] * d;
                }
            }
            else if (B.g18 == -1.0f)
                for (int i = 0; i < n; ++i)
                    xl[i] = -xl[i], xr[i] = -xr[i];
        }
    }

public:
    // FUN_00840a00: the latency the plugin reports to the host (PDC), max(L0…2)/K + L3
    int latency() const noexcept { return reportedLatency; }

private:
    std::shared_ptr<const MaximusPreset> pr;
    double fs = 44100.0;
    int K = 1, first = 0, inPlaceStages = 0, tailLength = 0, counter = 0, reportedLatency = 0, firLatency = 0;
    bool firOn = false;
    std::array<Fir, 3> fir {};
    float mix = 1.0f;
    bool mixOn = false;
    std::array<BandState, 4> band {};
    std::array<std::vector<Biquad>, 3> stages {};
    std::vector<Biquad> lowCut;
    std::array<Halfband, 1> up {}, down {};
    std::array<float, maxFrames> xl {}, xr {}, bl {}, br {}, sl {}, sr {}, dl {}, dr {}, gain {};
};

// =================================================================================================================
// Chorus, delay, reverb (REA_rodada36 §1)
// =================================================================================================================

// the chorus of the shared effects library (REA_rodada31 §7.3): order N, depth, speed, delay, spread, cross (the
// library's fields), dry gain and wet volume
struct ChorusUnit
{
    struct Tap
    {
        int ch = 0;
        float gl = 0, gr = 0, p3 = 0, p8 = 0, p9 = 0, p10 = 0;
        std::uint32_t phase = 0, inc = 0;
    };
    std::vector<Tap> taps;
    float dry = 1.0f;
    int maxDelay = 0;
    std::vector<float> ringL, ringR; // input history, power-of-two length
    int mask = 0, w = 0;

    ChorusUnit (int N, float depth, float speed, float dly, float spread, float cross, float dryGain, float vol, double fs)
    {
        static constexpr float q[20] { 0.967f, 1.039f, 1.021f, 0.1013f, 0.977f, 1.033f, 0.997f, 0.971f, 1.061f, 0.947f,
                                       1.031f, 0.1009f, 1.063f, 0.953f, 0.991f, 1.049f, 0.983f, 1.051f, 1.019f, 1.069f };
        N = std::max (N, 1);
        dry = dryGain;
        const auto A = f32 ((std::pow (11.0, (double) depth) - 1.0) * (1092.5 / 10.0));
        const auto B = (2147483648.0 / ((double) f32 (std::pow (101.0, (double) f32 (1.0f - speed)) - 1.0) * 75000.0 + 50000.0)) * 500.0;
        const auto C = f32 ((std::pow (11.0, (double) dly) - 1.0) * (4410.0 / 10.0));
        const auto sg = f32 (f32 (std::pow (1001.0, (double) spread) - 1.0) / 1000.0f);
        auto mixf = [&] (double a, double b) { return a * (double) sg + b * (1.0 - (double) sg); };
        const auto fsr = f32 (44100.0 / fs);
        float sumAbs = 0.0f, mx = 0.0f;
        for (int i = 0; i < 2 * N; ++i)
        {
            Tap t;
            float f = 0.0f;
            std::uint32_t ph = 0;
            if (i < N)
                f = (float) i, t.ch = 0;
            else
                f = (float) (2 * N - 1 - i), t.ch = 1, ph = 0x80000000u;
            const auto wv = 1.0f - f / (float) N;
            const auto rr = q[i % 20];
            const auto dV = f32 (mixf ((double) (rr * rr), 1.0));
            float gl = 1.0f, gr = cross * wv;
            if (t.ch)
                std::swap (gl, gr);
            sumAbs = f32 ((double) sumAbs + std::fabs ((double) gl) + std::fabs ((double) gr));
            auto p0 = A * wv + 10.0f;
            const auto d11 = mixf (B * (double) dV / (double) p0, B / (double) (A + 10.0f));
            auto p3 = f32 (mixf ((double) ((C * wv) * dV), (double) C));
            ph += (std::uint32_t) ((std::int64_t) rint ((double) ((wv * sg) * 2147483648.0f) / 256.0) << 8);
            t.phase = ph;
            t.inc = (std::uint32_t) (std::int64_t) rint (d11 * (double) fsr);
            p0 = p0 / fsr;
            auto t3 = p3 / fsr;
            if (! (t3 > 8.0f))
                t3 = 8.0f;
            p3 = p0 + t3;
            if (p3 + p0 > mx)
                mx = p3 + p0;
            const auto qq = std::sqrt ((double) (p0 * 4.0f));
            t.gl = gl, t.gr = gr, t.p3 = p3;
            t.p8 = f32 (qq / 2147483648.0), t.p9 = f32 (qq), t.p10 = f32 (qq / 2.0);
            taps.push_back (t);
        }
        const auto norm = f32 ((double) (1.0f - f32 ((double) f32 (std::pow (1001.0, (double) (1.0f - 1.0f / sumAbs)) - 1.0) / 1000.0)) * (double) vol);
        for (auto& t : taps)
            t.gl = t.gl * norm, t.gr = t.gr * norm;
        maxDelay = (int) rint ((double) mx) + 8;
        int size = 1;
        while (size < maxDelay + Rack::maxBlock + 8)
            size <<= 1;
        ringL.assign ((std::size_t) size, 0.0f);
        ringR.assign ((std::size_t) size, 0.0f);
        mask = size - 1;
    }

    void advance (int n) noexcept
    {
        for (auto& t : taps)
            t.phase += t.inc * (std::uint32_t) n;
    }

    // out = Σ taps (no dry); in: this block of the mix
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            ringL[(std::size_t) ((w + i) & mask)] = inL[i], ringR[(std::size_t) ((w + i) & mask)] = inR[i];
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);
        bool firstTap = true;
        for (auto& t : taps)
        {
            const auto& ring = t.ch ? ringR : ringL;
            auto ph = t.phase;
            for (int i = 0; i < n; ++i)
            {
                const auto sp = (double) (std::int32_t) ph;
                auto v = std::fabs (sp * (double) t.p8) - (double) t.p10;
                v = ((double) t.p9 - std::fabs (v)) * v + (double) t.p3;
                ph += t.inc;
                const auto k = (int) v; // x87 fistl with the truncating control word (v > 0)
                const auto at = w + i - k;
                const auto a = (double) ring[(std::size_t) (at & mask)], b = (double) ring[(std::size_t) ((at - 1) & mask)];
                const auto s = (b - a) * (v - (double) k) + a;
                if (firstTap)
                    outL[i] = (float) (s * (double) t.gl), outR[i] = (float) (s * (double) t.gr);
                else
                    outL[i] = (float) (s * (double) t.gl + (double) outL[i]), outR[i] = (float) (s * (double) t.gr + (double) outR[i]);
            }
            t.phase = ph;
            firstTap = false;
        }
        w = (w + n) & mask;
    }
};

// Harmless's chorus records (table 0x7ee73c + 0x20·type, DATA): order, depth/1024, speed/1024, delay/128, spread/128,
// cross/128; the rack's mix knob gives the dry gain and the wet volume
struct Rack::Chorus : ChorusUnit
{
    static ChorusUnit make (int type, float mixKnob, double fs)
    {
        static constexpr int rec[8][6] { { 4, 512, 768, 8, 128, -32 }, { 5, 1024, 856, 0, 128, 68 }, { 7, 512, 512, 128, 128, 54 },
                                         { 6, 561, 854, 0, 0, 38 },    { 1, 136, 312, 0, 0, 128 },   { 2, 86, 632, 0, 0, 0 },
                                         { 1, 42, 944, 0, 0, -22 },    { 9, 81, 170, 0, 0, -66 } };
        const auto* r = rec[std::clamp (type, 1, 8) - 1];
        return ChorusUnit (r[0], (float) r[1] * 0.0009765625f, (float) r[2] * 0.0009765625f, (float) r[3] * 0.0078125f,
                           (float) r[4] * 0.0078125f, (float) r[5] * 0.0078125f, g5 (mixKnob), g5 (f32 (1.0 - (double) mixKnob)), fs);
    }
    Chorus (int type, float mixKnob, double fs) : ChorusUnit (make (type, mixKnob, fs)) {}
};

// delay records (table 0x7ee67c + 8·type, DATA): stereo offset in ms (sign = side), ping-pong
struct Rack::Delay
{
    std::vector<float> l, r;
    int S = 1, rp = 0, D = 1, oL = 0, oR = 0;
    bool pingPong = false;
    float vol = 0.0f, fb = 0.0f;
    double cur = 0.0, maxStep = 0.001;

    Delay (int type, int volKnob, int fbKnob, int time, double fs, double bpm)
    {
        static constexpr int rec[3][2] { { 0, 0 }, { 1, 2 }, { -20, 2 } };
        const auto* rc = rec[std::clamp (type, 1, 3) - 1];
        const auto o = (int) rint (std::abs (rc[0]) * 0.001 * fs);
        if (rc[0] > 0)
            oR = o;
        else
            oL = o;
        pingPong = rc[1] != 0;
        D = std::max (1, (int) rint ((double) time * fs * 0.3125 / std::max (1.0, bpm)));
        vol = g11 ((float) volKnob * 0.001f);
        fb = (float) fbKnob * 0.001f;
        cur = fb; // the feedback ramp starts at its target (EXEC with the plugin's state loaded before the rate is set)
        maxStep = (double) f32 (0.001 * 44100.0 / fs);
        S = D + o + Rack::maxBlock + 8;
        l.assign ((std::size_t) S, 0.0f);
        r.assign ((std::size_t) S, 0.0f);
    }

    void write (const float* inL, const float* inR, int n) noexcept
    {
        const auto w = rp + D;
        for (int i = 0; i < n; ++i)
        {
            auto& a = l[(std::size_t) ((w + oL + i) % S)];
            a = a + inL[i] * vol;
            auto& b = r[(std::size_t) ((w + oR + i) % S)];
            b = b + inR[i] * vol;
        }
    }

    void advance (int n) noexcept { cur = ramp (cur, fb, n, maxStep).end; }

    void process (float* outL, float* outR, int n) noexcept
    {
        const auto rr = ramp (cur, fb, n, maxStep);
        cur = rr.end;
        auto g = rr.start;
        for (int i = 0; i < n; ++i)
        {
            const auto a = l[(std::size_t) rp], b = r[(std::size_t) rp];
            l[(std::size_t) rp] = 0.0f, r[(std::size_t) rp] = 0.0f;
            const auto w = (rp + D) % S;
            float fl = a, fr = b;
            if (pingPong)
                outL[i] = b, outR[i] = a, fl = b, fr = a;
            else
                outL[i] = a, outR[i] = b;
            auto u = (float) ((double) fl * g + (double) l[(std::size_t) w]);
            auto v = (float) ((double) fr * g + (double) r[(std::size_t) w]);
            l[(std::size_t) w] = std::fabs (u) < flushLevel ? 0.0f : u;
            r[(std::size_t) w] = std::fabs (v) < flushLevel ? 0.0f : v;
            g += rr.step;
            if (++rp >= S)
                rp = 0;
        }
    }
};

// the shared Reverb_x64.dll (REA_rodada31 §7.5) with the library wrapper's fields: lowcut Hz (0 off), highcut and
// damping in hundreds of Hz (0 off), predelay samples, room, diffusion, decay in samples, colour
struct ReverbUnit : PlateReverb
{
    ReverbUnit (float lowcut, float highcut, int preS, int room, int diffusion, float decaySamples, float damping, int colourIndex, double fs)
    {
        const auto rate = (long long) fs;
        const auto decay = (double) decaySamples;
        const auto f17 = (double) f32 (1.0 / (double) rate), half = (double) f32 ((double) rate * 0.5);
        if (lowcut != 0.0f)
        {
            const auto t = (double) f32 (std::tan (std::min ((double) lowcut, half) * pi * f17));
            cLc = f32 ((t - 1.0) / (t + 1.0));
        }
        auto lp = [&] (float v) {
            if (v == 0.0f)
                return 1.0f;
            const auto x = (double) f32 (2.0 - std::cos (std::min ((double) v, half) * 628.3185307179587 * f17));
            return f32 (1.0 - (x - std::sqrt (x * x - 1.0)));
        };
        cHc = lp (highcut);
        cDamp = lp (damping);
        auto muldiv = [] (long long a, long long b, long long c) { return (int) ((a * b) / c); };
        const auto sc = muldiv (rate, room + 15, 115);
        for (int i = 0; i < 12; ++i)
            L[(std::size_t) i] = std::max (1, muldiv (lines[i], sc, 20000));
        for (int i = 0; i < 7; ++i)
            tl[(std::size_t) i] = muldiv (tapsL[i], sc, 20000), tr[(std::size_t) i] = muldiv (tapsR[i], sc, 20000);
        const auto dv = (double) f32 (diffusion * 0.01), f13 = (double) f32 (f17 * 0.5);
        if (dv != 0.0)
        {
            auto e = [&] (int a, int b, double k) { return f32 (std::exp (std::log (0.001) * (double) f32 ((a + b) * f13) / (dv * k))); };
            cIn = e (L[0], L[1], 0.07), c1 = e (L[4], L[8], 0.35), c2 = e (L[6], L[10], 0.51);
        }
        const auto ssum = (double) (L[5] + L[7] + L[9] + L[11]);
        g = f32 (std::pow (std::exp (std::log (0.001) * ssum / decay), 0.25));
        auto r2 = (double) f32 ((double) f32 (std::pow (std::exp (std::log (0.001) * ssum / (double) f32 (decay * colourFactor[std::clamp (colourIndex, 0, 4)])), 0.25)) / (double) g);
        c48 = f32 ((r2 - 1.0) * 0.5);
        if (r2 > 1.0)
            r2 = 1.0;
        const auto t = (double) f32 (std::tan (500.0 * pi * f17));
        c44 = f32 ((t - r2) / (t + r2));
        allocate (std::max (1, preS));
    }
};

// Harmless's reverb records (table 0x7ee67c + 0x20·type, DATA): lowcut Hz, highcut ×100 Hz, predelay, room,
// diffusion, decay ×0.1 s, damping ×100 Hz, colour
struct Rack::Reverb : ReverbUnit
{
    static ReverbUnit make (int type, double fs)
    {
        static constexpr int rec[6][8] { { 75, 40, 0, 40, 100, 15, 40, 2 },  { 0, 115, 18, 40, 100, 22, 50, 2 },
                                         { 34, 21, 27, 90, 100, 45, 31, 3 }, { 400, 60, 31, 60, 65, 23, 37, 1 },
                                         { 100, 36, 0, 35, 100, 6, 36, 2 },  { 100, 60, 0, 17, 100, 5, 30, 2 } };
        const auto* r = rec[std::clamp (type, 1, 6) - 1];
        const auto rate = (long long) fs;
        return ReverbUnit ((float) r[0], (float) r[1], (int) rint ((double) r[2] * (double) rate / 384.0), r[3], r[4],
                           f32 ((double) r[5] * (double) rate / 10.0), (float) r[6], r[7], fs);
    }
    Reverb (int type, double fs) : ReverbUnit (make (type, fs)) {}
};

Rack::Rack() = default;
Rack::~Rack() = default;

void Rack::prepare (const std::array<int, 100>& p, const std::array<int, 7>& t, double fs, double bpm,
                    std::shared_ptr<const MaximusPreset> preset)
{
    chorus.reset();
    delay.reset();
    reverb.reset();
    comp.reset();
    compPreset.reset();
    if (p[90] > 0)
        chorus = std::make_unique<Chorus> (p[90], (float) p[91] * 0.001f, fs);
    if (p[92] > 0)
        delay = std::make_unique<Delay> (p[92], p[93], p[94], p[95], fs, bpm);
    if (p[96] > 0)
        reverb = std::make_unique<Reverb> (p[96], fs);
    wet = f32 ((double) g11 ((float) p[97] * 0.001f) * 0.5);
    if (t[3] > 0 && preset != nullptr && preset->valid)
    {
        compPreset = std::move (preset);
        comp = std::make_unique<Maximus>();
        comp->prepare (compPreset, fs, p[98]);
    }
    on = chorus || delay || reverb || comp;
    tailLength = (int) rint (fs * 0.004) + (chorus ? maxBlock + chorus->maxDelay : 0);
    tail = 0;
    quietNeeded = (long long) (0.25 * fs) + (chorus ? chorus->maxDelay : 0)
                  + (delay ? (long long) delay->D * 2 + delay->oL + delay->oR : 0)
                  + (reverb ? (long long) reverb->pre.size() + reverb->L[5] + reverb->L[9] : 0);
    quietFor = quietNeeded; // a new rack holds no sound
}

void Rack::process (float* left, float* right, int n, bool voices) noexcept
{
    if (! on || n <= 0)
        return;
    if (voices)
        tail = tailLength;
    else if (tail > 0)
        tail -= n;
    if (chorus)
    {
        if (tail < 1)
            chorus->advance (n);
        else
        {
            chorus->process (left, right, scratchL.data(), scratchR.data(), n);
            if (chorus->dry != 1.0f)
                for (int i = 0; i < n; ++i)
                    left[i] = left[i] * chorus->dry, right[i] = right[i] * chorus->dry;
            for (int i = 0; i < n; ++i)
                left[i] = left[i] + scratchL[(std::size_t) i], right[i] = right[i] + scratchR[(std::size_t) i];
        }
    }
    if (delay)
    {
        if (tail > 0)
            delay->write (left, right, n);
        delay->process (scratchL.data(), scratchR.data(), n);
        for (int i = 0; i < n; ++i)
            left[i] = left[i] + scratchL[(std::size_t) i], right[i] = right[i] + scratchR[(std::size_t) i];
    }
    if (reverb)
    {
        for (int i = 0; i < n; ++i)
            mono[(std::size_t) i] = left[i] + right[i];
        reverb->process (mono.data(), scratchL.data(), scratchR.data(), n);
        for (int i = 0; i < n; ++i)
            left[i] = left[i] + scratchL[(std::size_t) i] * wet, right[i] = right[i] + scratchR[(std::size_t) i] * wet;
    }
    if (comp)
        comp->process (left, right, n);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::fabs (left[i]), std::fabs (right[i])));
    if (voices || peak > 1.0e-7f)
        quietFor = 0;
    else
        quietFor += n;
}

void Rack::advance (int n) noexcept
{
    if (! on || n <= 0)
        return;
    if (tail > 0)
        tail -= n;
    if (chorus)
        chorus->advance (n);
    if (delay)
        delay->advance (n);
    if (comp)
        comp->silence (n);
}
// =================================================================================================================
// Harmor's rack (REA_rodada38): Gen_Render's unit list (FUN_00892e10) over the record mix
// =================================================================================================================

std::vector<std::uint8_t> withMaximusParams (std::vector<std::uint8_t> state, const std::vector<std::pair<int, int>>& params)
{
    for (const auto& [index, value] : params)
        if (index >= 0 && index < 59 && 4 + 4 * (std::size_t) index + 4 <= state.size())
            std::memcpy (state.data() + 4 + 4 * (std::size_t) index, &value, 4);
    return state;
}

namespace
{
const double ln2 = 0.69314718055994529;
constexpr float rampEps = 5.960464477539063e-08f; // 0x91b14c

// the library's stereo gain with a one-pole ramp (FUN_007a4cc0 snap, FUN_007a4a90 / FUN_007a4c00 multiply,
// FUN_007a49b0 / FUN_007a4b10 multiply-add, FUN_007a4c70 values): per sample d ← d·c, gain = target + d
struct StereoRamp
{
    float tL = 0, tR = 0, cL = 0, cR = 0, coef = 1;

    bool settled() noexcept
    {
        if (! (std::fabs (tL - cL) >= rampEps) && ! (std::fabs (tR - cR) >= rampEps))
        {
            cL = tL, cR = tR;
            return true;
        }
        return false;
    }

    void mul (const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
    {
        if (settled())
        {
            if (tL == 0.0f && tR == 0.0f && ! std::signbit (tL) && ! std::signbit (tR))
                std::fill (oL, oL + n, 0.0f), std::fill (oR, oR + n, 0.0f);
            else
                for (int i = 0; i < n; ++i)
                    oL[i] = iL[i] * tL, oR[i] = iR[i] * tR;
            return;
        }
        auto dL = cL - tL, dR = cR - tR;
        for (int i = 0; i < n; ++i)
        {
            dL = dL * coef, dR = dR * coef;
            oL[i] = iL[i] * (dL + tL), oR[i] = iR[i] * (dR + tR);
        }
        cL = dL + tL, cR = dR + tR;
    }

    void mulAdd (const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
    {
        if (settled())
        {
            std::uint32_t bl = 0, br = 0;
            std::memcpy (&bl, &tL, 4);
            std::memcpy (&br, &tR, 4);
            if (bl != 0 || br != 0)
                for (int i = 0; i < n; ++i)
                    oL[i] = iL[i] * tL + oL[i], oR[i] = iR[i] * tR + oR[i];
            return;
        }
        auto dL = cL - tL, dR = cR - tR;
        for (int i = 0; i < n; ++i)
        {
            dL = dL * coef, dR = dR * coef;
            oL[i] = iL[i] * (dL + tL) + oL[i], oR[i] = iR[i] * (dR + tR) + oR[i];
        }
        cL = dL + tL, cR = dR + tR;
    }

    void values (float* oL, float* oR, int n) noexcept
    {
        auto dL = cL - tL, dR = cR - tR;
        for (int i = 0; i < n; ++i)
        {
            dL = dL * coef, dR = dR * coef;
            oL[i] = dL + tL, oR[i] = dR + tR;
        }
        cL = dL + tL, cR = dR + tR;
    }

    bool allZero() const noexcept // FUN_007a4d30 (bit patterns)
    {
        std::uint32_t b[4] {};
        std::memcpy (&b[0], &tL, 4), std::memcpy (&b[1], &tR, 4), std::memcpy (&b[2], &cL, 4), std::memcpy (&b[3], &cR, 4);
        return b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0;
    }
};

// 16.3515987·2^(v/100) Hz (the library's frequency law: float v·0.01, exp in double, × the run-time float 16.35)
float noteHz (int v) noexcept
{
    return f32 (std::exp ((double) ((float) v * 0.01f) * ln2) * (double) 16.3515987f);
}

// FUN_0062d460: L = cos((1 + pan)·π/4)·v, R = sin(…)·v (x87)
void panLaw (float pan, float v, float& l, float& r) noexcept
{
    const auto t = ((long double) 1 + (long double) pan) * (long double) 0.78539816339744830961566;
    l = (float) (std::cos ((double) t) * (double) v);
    r = (float) (std::sin ((double) t) * (double) v);
}
} // namespace

struct HarmorRack::Impl
{
    HarmorRackSettings S;
    double fs = 44100.0;
    std::array<int, 6> list {};
    int count = 0;
    int counter = 0, reload = 0;
    bool on = false;
    long long quietFor = 0, quietNeeded = 0;

    // distortion (FUN_008944a0)
    int distType = 0, stages = 1, F = 2;
    StereoRamp bias, drive, dry, wet;
    bool biasFlag = false, post = false;
    float qMul = 0.0f, qInv = 0.0f; // types ≥ 11 (+0x2cd0, +0x2cd4)
    std::array<Halfband, 3> up {}, down {};
    std::array<Biquad, 2> postLp {};
    float dcC2 = 0.0f, dcC1 = 0.96f;
    long double dcV[2] {}, dcP[2] {};

    // chorus
    std::unique_ptr<ChorusUnit> chorus;

    // delay (FUN_00892f90 set-up, FUN_00893fc0 process; library FUN_007a5xxx)
    bool delayOn = false, delayLoop = false;
    int dMode = 0, D = 1, S0 = 1, rp = 0, oL = 0, oR = 0;
    float fbTarget = 0.5f, fbCur = 0.0f, maxStep = 0.001f;
    std::vector<float> ringL, ringR;
    StereoRamp inGain;
    std::array<Biquad, 2> dFilt {};
    std::array<bool, 2> dFiltOn {};

    // reverb
    std::unique_ptr<ReverbUnit> reverb;
    StereoRamp revWet;

    // compressor
    std::unique_ptr<Maximus> comp;

    std::array<float, maxBlock * 8> xL {}, xR {}, aL {}, aR {}, bL {}, bR {}, cL {}, cR {};
    std::array<float, maxBlock> tL {}, tR {}, mono {};

    void prepare (const HarmorRackSettings& s)
    {
        S = s;
        fs = s.fs;
        const auto& g = s.g;
        const auto rampCoef = f32 (std::exp (ln001 / (0.015 * fs))); // the rate object's +0x40
        // tail counter reload (FUN_008937f0): 30 ms, 530 ms with the distortion on, + the chorus ring
        distType = s.distortion;
        reload = (int) rint ((double) (distType > 0 ? 0x212 : 0x1e) * 0.001 * fs);
        chorus.reset();
        if (g[32] > 0)
        {
            const auto m = (float) std::abs (g[38]) * 0.001f;
            const auto sign = g[38] == 0 ? 0.0f : (g[38] < 0 ? -1.0f : 1.0f);
            chorus = std::make_unique<ChorusUnit> (g[32], (float) g[33] * 0.0009765625f, (float) g[34] * 0.0009765625f,
                                                   (float) g[35] * 0.0078125f, (float) g[36] * 0.0078125f, (float) g[37] * 0.0078125f,
                                                   g5 (m), sign * g5 (1.0f - m), fs);
            reload += 384 + chorus->maxDelay;
        }
        counter = reload;

        // distortion set-up (ProcessParam cases 0x315…0x319, rate set-up)
        stages = std::clamp (s.precision + 1, 1, 3);
        F = 1 << stages;
        for (auto& h : up)
            h.reset();
        for (auto& h : down)
            h.reset();
        dcV[0] = dcV[1] = dcP[0] = dcP[1] = 0;
        dcC2 = f32 ((44100.0 / fs) * 2.2834999999999997e-05);
        if (distType > 0)
            setupDistortion (g);
        post = distType > 0 && g[21] != 1100;
        if (post)
        {
            const auto f = std::min (noteHz (g[21]), f32 (0.4999 * fs) * (float) F);
            for (auto& q : postLp)
            {
                q.design (1, (double) f * ((2.0 * pi / fs) / (double) F), 0.70710678118654746);
                q.reset();
            }
        }
        for (auto* r : { &bias, &drive, &dry, &wet })
            r->coef = f32 (std::exp (ln001 / ((double) (F * 15) * 0.001 * fs)));

        // delay
        delayOn = g[39] > 0;
        if (delayOn)
        {
            dMode = std::clamp (g[40], 0, 2);
            const auto offMs = f32 ((std::exp ((double) std::abs (g[43]) * 0.015625 * ln2) - 1.0) * 1.9607843137254901);
            const auto off = (int) rint ((double) offMs * 0.001 * fs);
            double t = 0.0;
            if (g[42] < 0)
                t = (std::exp ((double) g[42] * -0.010416666666666666 * ln2) - 1.0) * 0.06666666666666667;
            else
                t = ((double) g[42] * 0.3125) / (double) (float) s.bpm;
            D = std::max (1, (int) rint ((double) ((float) t * (float) fs)));
            oL = g[43] > 0 ? 0 : off;
            oR = g[43] > 0 ? off : 0;
            S0 = off + 384 + D;
            ringL.assign ((std::size_t) S0, 0.0f);
            ringR.assign ((std::size_t) S0, 0.0f);
            rp = 0;
            fbTarget = (float) g[41] * 0.001f;
            fbCur = 0.5f; // the library constructor's feedback (FUN_007a5290); the ramp starts there (EXEC)
            maxStep = f32 (0.001 * 44100.0 / fs);
            auto vol = (float) g[44] * 0.001f;
            vol = vol * vol * vol;
            auto pan = (float) g[45] * 0.0078125f;
            if (dMode > 0)
                pan = -pan;
            if (pan == 0.0f && ! std::signbit (pan))
                inGain.tL = inGain.tR = vol;
            else if (vol == 0.0f)
                inGain.tL = inGain.tR = 0.0f;
            else
                panLaw (pan, vol * 1.41421354f, inGain.tL, inGain.tR);
            inGain.cL = inGain.cR = 0.0f; // (from 0 at state load, EXEC)
            inGain.coef = rampCoef;
            delayLoop = g[48] > 0;
            const int types[2] { 4, 1 };
            for (int k = 0; k < 2; ++k)
            {
                dFiltOn[(std::size_t) k] = g[(std::size_t) (46 + k)] != k * 1050;
                if (dFiltOn[(std::size_t) k])
                {
                    const auto f = std::min (noteHz (g[(std::size_t) (46 + k)]), f32 (0.4999 * fs));
                    dFilt[(std::size_t) k].design (types[k], (double) f * (2.0 * pi / fs), 1.0);
                    dFilt[(std::size_t) k].reset();
                }
            }
        }

        // reverb (FUN_00893470)
        reverb.reset();
        if (g[22] > 0)
        {
            const auto rate = (long long) fs;
            double pre = 0.0;
            if (g[25] < 0)
                pre = (std::exp ((double) g[25] * -0.015625 * ln2) - 1.0) * 0.015873015873015872;
            else
                pre = ((double) g[25] * 0.3125) / (double) (float) s.bpm;
            const auto preS = (int) rint (pre * (double) rate);
            const auto decay = f32 (std::exp ((double) ((float) g[28] * 0.01f) * ln2) * 0.1 * (double) rate);
            const auto lc = g[23] > 0 ? noteHz (g[23]) : 0.0f;
            auto hz100 = [&] (int v) {
                return v < 1050 ? f32 (std::exp ((double) ((float) v * 0.01f) * ln2) * (double) 16.3515987f * (double) 0.01f) : 0.0f;
            };
            reverb = std::make_unique<ReverbUnit> (lc, hz100 (g[24]), preS, g[26], g[27], decay, hz100 (g[29]), g[30], fs);
            const auto w = (double) ((float) g[31] * 0.001f);
            revWet.tL = revWet.tR = f32 (w * w * w * 0.5);
            revWet.cL = revWet.cR = 0.0f; // the ramp starts from 0 at state load (EXEC)
            revWet.coef = rampCoef;
        }

        // compressor (hosted SG.dll with Harmor's own preset)
        comp.reset();
        if (s.comp != nullptr && s.comp->valid)
        {
            comp = std::make_unique<Maximus>();
            comp->prepare (s.comp, fs, s.compMix);
        }

        // the unit list (FUN_00892e10)
        count = 0;
        for (const auto t : s.order)
        {
            const bool present = t == 0 ? distType > 0 : t == 1 ? chorus != nullptr : t == 2 ? delayOn : t == 3 ? reverb != nullptr
                                                     : t == 4 ? true : comp != nullptr;
            if (present && count < 6)
                list[(std::size_t) count++] = t;
        }
        on = distType > 0 || chorus || delayOn || reverb || comp;
        quietNeeded = (long long) (0.25 * fs) + reload + (delayOn ? (long long) D * 2 + oL + oR : 0)
                      + (reverb ? (long long) reverb->pre.size() + reverb->L[5] + reverb->L[9] : 0) + (long long) (1.6 * fs);
        quietFor = quietNeeded;
    }

    void setupDistortion (const std::array<int, 53>& g)
    {
        const auto type = distType;
        // volume (g19) and mix (g20): wet = vol·(1 − |mix|)/normaliser, dry = mix
        const auto v = (float) g[19] * 0.001f;
        const auto vol = v == 1.0f ? 1.0f : f32 ((std::exp ((double) (v * 1.09861231f)) - 1.0) * (double) 0.5f);
        const auto mixF = (float) g[20] * 0.001f;
        dry.tL = dry.tR = mixF;
        const auto d12 = (double) vol * (1.0 - std::fabs ((double) mixF));
        const auto g17 = g[17];
        biasFlag = true;
        float dr = 0.0f, wt = 0.0f;
        if (type >= 1 && type <= 3)
        {
            const auto d = std::exp ((double) ((float) g17 * 0.01f - 4.0f) * ln2);
            dr = f32 (d * pi);
            wt = f32 (d12 / (std::tanh (d) * pi));
        }
        else if (type == 4)
        {
            const auto d = std::exp ((double) ((float) g17 * 0.01f - 2.0f) * ln2);
            const auto t = std::tanh (d * 4.0);
            dr = f32 (d * pi);
            wt = f32 (d12 / (t * t * t * pi));
        }
        else if (type == 5)
        {
            const auto d = std::exp (((double) g17 * 0.025 - 5.0) * ln2);
            dr = f32 (d);
            wt = f32 (d12 / (std::log (d + 1.0) * (double) ((float) g17 * 0.001f + 1.0f)));
        }
        else if (type == 6)
        {
            const auto d = std::exp (((double) g17 * 0.005 - 3.0) * ln2);
            dr = f32 (d * pi);
            wt = f32 (d12 / (std::tanh (d) * pi));
        }
        else if (type == 7)
        {
            const auto d = std::exp (((double) g17 * 0.002 - 1.0) * ln2) * pi;
            dr = f32 (d * d);
            wt = f32 (d12 / std::sqrt (d));
        }
        else if (type == 8)
        {
            const auto d = std::exp ((double) ((float) g17 * 0.01f - 4.0f) * ln2);
            dr = f32 (d * pi);
            wt = f32 (d12 / (std::tanh (d) * pi));
        }
        else if (type == 9)
        {
            const auto b = f32 (std::exp ((double) g[18] * 0.004 * ln2));
            bias.tL = bias.tR = b;
            dr = f32 (std::exp ((7.0 - (double) g17 * 0.007) * ln2) * pi);
            wt = f32 (d12 / (double) (dr * b));
            biasFlag = false;
        }
        else if (type == 10)
        {
            dr = f32 (std::exp ((9.0 - (double) g17 * 0.007) * ln2));
            wt = f32 (d12 / (double) dr);
        }
        else
        {
            const auto q = f32 (std::exp ((double) g[18] * 0.02 * ln2) * 1024.0);
            dr = q;
            qMul = f32 (std::exp ((10.0 - (double) g17 * 0.007) * ln2) / std::log ((double) (q + 1.0f)));
            qInv = 1.0f / qMul;
            wt = f32 (d12 / (double) q);
            biasFlag = false;
        }
        drive.tL = drive.tR = dr;
        wet.tL = wet.tR = wt;
        if (biasFlag)
        {
            const auto b = (float) ((double) g[18] * 0.002);
            const auto f = type == 8 ? b * b * dr : f32 ((double) (b * b) * 0.75);
            bias.tL = f;
            bias.tR = b <= 0.0f ? -f : f; // (an integer test on the float's bits: g18 ≤ 0 → negated, EXEC)
            biasFlag = type != 8;
        }
        for (auto* r : { &bias, &drive, &dry, &wet })
            r->cL = r->tL, r->cR = r->tR; // the ramps start at their targets (EXEC)
    }

    void distortion (float* L, float* R, int n) noexcept
    {
        // up-sampling cascade (FUN_00863fd0): stage S−1 first
        const float* sl = L;
        const float* sr = R;
        int m = n;
        float* outs[2][2] { { aL.data(), aR.data() }, { bL.data(), bR.data() } };
        int k = 0;
        for (int st = stages - 1; st >= 0; --st, k ^= 1)
        {
            up[(std::size_t) st].up (sl, sr, outs[k][0], outs[k][1], m);
            sl = outs[k][0], sr = outs[k][1];
            m *= 2;
        }
        auto* xl = const_cast<float*> (sl);
        auto* xr = const_cast<float*> (sr);
        float* dl = xL.data();
        float* drr = xR.data();
        dry.mul (xl, xr, dl, drr, m);
        float* yl = cL.data();
        float* yr = cR.data();
        if (biasFlag && ! bias.allZero())
        {
            bias.values (yl, yr, m);
            for (int i = 0; i < m; ++i)
                xl[i] = yl[i] + xl[i], xr[i] = yr[i] + xr[i];
        }
        drive.mul (xl, xr, xl, xr, m);
        shape (xl, xr, yl, yr, m);
        wet.mulAdd (yl, yr, dl, drr, m);
        if (post)
            for (auto& q : postLp)
                q.process (dl, drr, dl, drr, m);
        // down-sampling cascade (FUN_00864070): in place, the last stage into the output
        for (int st = stages - 1; st >= 0; --st)
        {
            m /= 2;
            float* ol = st == 0 ? xl : dl;
            float* orr = st == 0 ? xr : drr;
            down[(std::size_t) st].down (dl, drr, ol, orr, m);
        }
        // DC stage (FUN_0062a3c0, x87): v += (x − p)·c2, p += v, v ·= c1, out = x − p
        float* ch[2] { xl, xr };
        float* out[2] { L, R };
        for (int c = 0; c < 2; ++c)
        {
            auto v = dcV[c], pp = dcP[c];
            for (int i = 0; i < n; ++i)
            {
                const auto x = (long double) ch[c][i];
                v = v + (x - pp) * (long double) dcC2;
                pp = pp + v;
                v = v * (long double) dcC1;
                out[c][i] = (float) (x - pp);
            }
            dcV[c] = (long double) (double) v, dcP[c] = (long double) (double) pp;
        }
    }

    void shape (float* xl, float* xr, float* yl, float* yr, int m) noexcept
    {
        auto sgn1 = [] (float x) { return std::signbit (x) ? -1.0f : 1.0f; }; // AndC 0x80000000, OrC 1.0
        float* ch[2] { xl, xr };
        float* out[2] { yl, yr };
        for (int c = 0; c < 2; ++c)
        {
            auto* x = ch[c];
            auto* y = out[c];
            switch (distType)
            {
                case 1:
                    for (int i = 0; i < m; ++i)
                        y[i] = std::tanh (x[i]);
                    break;
                case 2:
                    for (int i = 0; i < m; ++i)
                        y[i] = std::exp (std::tanh (x[i])) + -1.0f;
                    break;
                case 3:
                    for (int i = 0; i < m; ++i)
                        y[i] = std::tanh (std::exp (x[i]) + -1.0f);
                    break;
                case 4:
                    for (int i = 0; i < m; ++i)
                    {
                        const auto t = std::tanh (x[i]);
                        y[i] = t * (t * t);
                    }
                    break;
                case 5:
                    for (int i = 0; i < m; ++i)
                        y[i] = std::log (std::fabs (x[i]) + 1.0f) * sgn1 (x[i]);
                    break;
                case 6:
                    for (int i = 0; i < m; ++i)
                        y[i] = std::cbrt (std::tanh ((x[i] * x[i]) * x[i]));
                    break;
                case 7:
                    for (int i = 0; i < m; ++i)
                        y[i] = std::sin (std::sqrt (std::fabs (x[i]))) * sgn1 (x[i]);
                    break;
                case 9:
                    for (int i = 0; i < m; ++i)
                        y[i] = std::sin (x[i]);
                    break;
                case 10:
                    for (int i = 0; i < m; ++i)
                        y[i] = std::round (x[i]);
                    break;
                case 8:
                    break;
                default:
                    for (int i = 0; i < m; ++i)
                    {
                        const auto l = std::log (std::fabs (x[i]) + 1.0f) * qMul;
                        y[i] = std::expm1 (std::round (l) * qInv) * sgn1 (x[i]);
                    }
                    break;
            }
        }
        if (distType == 8)
        {
            // tanh(x)·tanh(x + b)·tanh(x + 2b), b = the bias ramp
            float* bl = aL.data() == xl ? bL.data() : aL.data();
            float* br = aR.data() == xr ? bR.data() : aR.data();
            bias.values (bl, br, m);
            for (int i = 0; i < m; ++i)
            {
                yl[i] = std::tanh (xl[i]), yr[i] = std::tanh (xr[i]);
                auto ul = xl[i] + bl[i], ur = xr[i] + br[i];
                yl[i] = yl[i] * std::tanh (ul), yr[i] = yr[i] * std::tanh (ur);
                ul = bl[i] + ul, ur = br[i] + ur;
                yl[i] = yl[i] * std::tanh (ul), yr[i] = yr[i] * std::tanh (ur);
            }
        }
        else if (distType == 9)
            bias.mulAdd (xl, xr, yl, yr, m);
    }

    void delay (float* L, float* R, int n, bool active) noexcept
    {
        for (int at = 0; at < n; at += 384)
        {
            const auto c = std::min (384, n - at);
            float* tl = tL.data() + at;
            float* tr = tR.data() + at;
            if (! active)
                inGain.cL = inGain.tL, inGain.cR = inGain.tR;
            else
            {
                inGain.mul (L + at, R + at, tl, tr, c);
                if (! delayLoop)
                    for (int k = 0; k < 2; ++k)
                        if (dFiltOn[(std::size_t) k])
                            dFilt[(std::size_t) k].process (tl, tr, tl, tr, c);
                const auto w = rp + D;
                for (int i = 0; i < c; ++i)
                {
                    auto& a = ringL[(std::size_t) ((w + oL + i) % S0)];
                    a = a + tl[i];
                    auto& b = ringR[(std::size_t) ((w + oR + i) % S0)];
                    b = b + tr[i];
                }
            }
            int left = c, o = 0;
            while (left > 0)
            {
                auto seg = std::min (D, S0 - rp); // FUN_007a5260: up to the write pointer or the ring end
                seg = std::min (seg, left);
                if (delayLoop)
                    for (int k = 0; k < 2; ++k)
                        if (dFiltOn[(std::size_t) k])
                            dFilt[(std::size_t) k].process (ringL.data() + rp, ringR.data() + rp, ringL.data() + rp, ringR.data() + rp, seg);
                // feedback ramp (FUN_0062d390)
                const auto start = fbCur;
                auto step = fbTarget - start;
                if (step != 0.0f)
                {
                    fbCur = fbTarget;
                    step = step / (float) seg;
                    if (maxStep < std::fabs (step))
                    {
                        step = std::copysign (maxStep, step);
                        fbCur = start + step * (float) seg;
                        if (std::fabs (fbCur) < 5.960464477539063e-08f)
                            fbCur = 0.0f, step = -start / (float) seg;
                    }
                }
                auto g = start;
                for (int i = 0; i < seg; ++i)
                {
                    const auto r = rp;
                    auto w = rp + D;
                    if (w >= S0)
                        w -= S0;
                    const auto a = ringL[(std::size_t) r], b = ringR[(std::size_t) r];
                    ringL[(std::size_t) r] = 0.0f, ringR[(std::size_t) r] = 0.0f;
                    float outL = a, outR = b, fl = a, fr = b;
                    if (dMode == 1)
                        outL = b, outR = a;
                    else if (dMode == 2)
                        outL = b, outR = a, fl = b, fr = a;
                    tl[o + i] = outL, tr[o + i] = outR;
                    auto u = fl * g + ringL[(std::size_t) w];
                    auto v = fr * g + ringR[(std::size_t) w];
                    ringL[(std::size_t) w] = std::fabs (u) < 5.960464477539063e-08f ? 0.0f : u;
                    ringR[(std::size_t) w] = std::fabs (v) < 5.960464477539063e-08f ? 0.0f : v;
                    g = g + step;
                    if (++rp >= S0)
                        rp = 0;
                }
                left -= seg, o += seg;
            }
            for (int i = 0; i < c; ++i)
                L[at + i] = L[at + i] + tl[i], R[at + i] = R[at + i] + tr[i];
        }
    }

    void process (float* L, float* R, const float* fxL, const float* fxR, int n, bool voices) noexcept
    {
        if (voices)
            counter = reload;
        else if (counter > 0)
            counter -= n;
        auto active = counter;
        for (int k = 0; k < count; ++k)
            switch (list[(std::size_t) k])
            {
                case 0:
                    if (active >= 1)
                        distortion (L, R, n);
                    break;
                case 1:
                    if (active < 1)
                        chorus->advance (n);
                    else
                    {
                        chorus->process (L, R, tL.data(), tR.data(), n);
                        if (chorus->dry != 1.0f)
                            for (int i = 0; i < n; ++i)
                                L[i] = L[i] * chorus->dry, R[i] = R[i] * chorus->dry;
                        for (int i = 0; i < n; ++i)
                            L[i] = L[i] + tL[(std::size_t) i], R[i] = R[i] + tR[(std::size_t) i];
                    }
                    break;
                case 2:
                    delay (L, R, n, active >= 1);
                    active = 0x7fffffff;
                    break;
                case 3:
                    for (int i = 0; i < n; ++i)
                        mono[(std::size_t) i] = L[i] + R[i];
                    reverb->process (mono.data(), tL.data(), tR.data(), n);
                    revWet.mulAdd (tL.data(), tR.data(), L, R, n);
                    active = 0x7fffffff;
                    break;
                case 4:
                    if (fxL != nullptr)
                        for (int i = 0; i < n; ++i)
                            L[i] = L[i] + fxL[i], R[i] = R[i] + fxR[i];
                    break;
                default:
                    comp->process (L, R, n);
                    active = 0x7fffffff;
                    break;
            }
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::max (std::fabs (L[i]), std::fabs (R[i])));
        if (voices || peak > 1.0e-7f)
            quietFor = 0;
        else
            quietFor += n;
    }

    void advance (int n) noexcept
    {
        if (counter > 0)
            counter -= n;
        if (chorus)
            chorus->advance (n);
        if (delayOn)
        {
            inGain.cL = inGain.tL, inGain.cR = inGain.tR;
            fbCur = fbTarget;
        }
        if (reverb)
        {
            revWet.cL = revWet.tL, revWet.cR = revWet.tR;
        }
        if (comp)
            comp->silence (n);
    }
};

HarmorRack::HarmorRack() : d (std::make_unique<Impl>()) {}
HarmorRack::~HarmorRack() = default;
void HarmorRack::prepare (const HarmorRackSettings& settings) { d->prepare (settings); }
bool HarmorRack::enabled() const noexcept { return d->on; }
void HarmorRack::process (float* left, float* right, const float* fxL, const float* fxR, int n, bool voices) noexcept
{
    d->process (left, right, fxL, fxR, n, voices);
}
void HarmorRack::advance (int n) noexcept { d->advance (n); }
bool HarmorRack::quiet() const noexcept { return ! d->on || d->quietFor >= d->quietNeeded; }
} // namespace lynx::hlfx

namespace lynx::hlfx
{
// =================================================================================================================
// The engine on its own (mixer Maximus, Soundgoodizer, Vocodex's SG stage; REA_rodada40)
// =================================================================================================================

MaximusProcessor::MaximusProcessor() = default;
MaximusProcessor::~MaximusProcessor() = default;

void MaximusProcessor::prepare (std::shared_ptr<const MaximusPreset> preset, double sampleRate, int mixParameter)
{
    if (preset == nullptr)
    {
        engine.reset();
        return;
    }
    engine = std::make_unique<Maximus>();
    engine->prepare (std::move (preset), sampleRate, mixParameter);
}

void MaximusProcessor::process (float* left, float* right, int n) noexcept
{
    if (engine == nullptr)
        return;
    for (int done = 0; done < n; done += Rack::maxBlock)
    {
        const auto len = std::min (Rack::maxBlock, n - done);
        engine->process (left + done, right + done, len);
    }
}

void MaximusProcessor::reset() noexcept
{
    if (engine != nullptr)
        engine->reset();
}

int MaximusProcessor::latencySamples() const noexcept { return engine != nullptr ? engine->latency() : 0; }

int maximusMixParameter (const MaximusPreset& preset) noexcept { return preset.p[5]; }
} // namespace lynx::hlfx
