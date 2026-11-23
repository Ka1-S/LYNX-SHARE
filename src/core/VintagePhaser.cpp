#include "core/VintagePhaser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lynx
{
namespace
{
constexpr double pi = 3.141592653589793;

// runtime constants of the engine (REA_rodada29 §3.5): tan(x) ≈ x(A − Bx²)/(C − x²)
constexpr float tanA = 2.47168851f, tanB = 0.189759687f, tanC = 2.46740103f;

float tanhPade (float v) noexcept // (7,6) Padé
{
    v = std::clamp (v, -5.0f, 5.0f);
    const auto x2 = v * v;
    const auto num = (((x2 + 378.0f) * x2 + 17325.0f) * x2 + 135135.0f) * v;
    const auto den = ((x2 * 28.0f + 3150.0f) * x2 + 62370.0f) * x2 + 135135.0f;
    return num / den;
}

float tanG (float f, float k) noexcept // V2 coefficient G = c/(1 + c), c = tan approximation of f·k
{
    const auto x = f * k;
    const auto c = (x * (tanA - tanB * x * x)) / (tanC - x * x);
    return c / (c + 1.0f);
}

float exp2Fast (float e) noexcept
{
    const auto i = (int) e;
    const auto fr = e - (float) i;
    const auto p = ((fr * 0.0790646598f + 0.224328086f) * fr + 0.69659853f) * fr + 0.999862194f;
    return p * (float) std::ldexp (1.0, i);
}

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<std::array<float, 4>, 2> st {};
    bool secondOrder = false; // summation order of the input / feedback high-pass

    float tick (int ch, float x) noexcept
    {
        auto& s = st[(std::size_t) ch];
        const auto x1 = s[0], x2 = s[1], y1 = s[2], y2 = s[3];
        const auto y = secondOrder ? ((a2 * y2 + a1 * y1) + b2 * x2) + (b0 * x + b1 * x1)
                                   : ((a1 * y1 + a2 * y2) + b2 * x2) + (b1 * x1 + b0 * x);
        s = { x, x1, y, y1 };
        return y;
    }
    void highPass1 (double f, double fs) noexcept
    {
        const auto t = std::tan (pi * f / fs), g = 1.0 / (1.0 + t);
        b0 = (float) g, b1 = (float) -g, b2 = 0.0f, a1 = (float) ((1.0 - t) * g), a2 = 0.0f;
    }
    void lowShelf (double f, double fs, double db, double q = 0.5) noexcept
    {
        const auto a = std::pow (10.0, db / 40.0), w = 2.0 * pi * f / fs, c = std::cos (w), sn = std::sin (w);
        const auto al = sn / (2.0 * q), sa = 2.0 * std::sqrt (a) * al;
        const auto nb0 = a * ((a + 1) - (a - 1) * c + sa), nb1 = 2 * a * ((a - 1) - (a + 1) * c), nb2 = a * ((a + 1) - (a - 1) * c - sa);
        const auto na0 = (a + 1) + (a - 1) * c + sa, na1 = -2 * ((a - 1) + (a + 1) * c), na2 = (a + 1) + (a - 1) * c - sa;
        b0 = (float) (nb0 / na0), b1 = (float) (nb1 / na0), b2 = (float) (nb2 / na0), a1 = (float) (-na1 / na0), a2 = (float) (-na2 / na0);
    }
};

// windowed-sinc up-sampler, 88·OS + 1 taps (§3.3)
struct Upsampler
{
    int os = 8, taps = 0, size = 0, pos = 0;
    std::vector<float> h, buf;

    void setup (int oversampling)
    {
        os = oversampling;
        taps = 88 * os + 1;
        h.resize ((std::size_t) taps);
        for (int i = 0; i < taps; ++i)
        {
            const auto u = (double) i / os - 44.0;
            const auto sinc = std::abs (u) < 1e-6 ? 1.0 : std::sin (u * pi) / (u * pi);
            const auto w = std::max (0.0, std::sin ((double) i / (taps - 1) * pi));
            h[(std::size_t) i] = (float) (w * sinc);
        }
        size = taps + 2 * os;
        size -= size % os;
        buf.assign ((std::size_t) size, 0.0f);
        pos = 0;
    }
    void push (float x, float* out) noexcept
    {
        // round 190pz: the same multiply and add per tap, in two contiguous runs (up to the ring's end, then from its
        // start) instead of a wrap test per tap, so the compiler can vectorise them: bit-identical, ~3x less work
        const auto first = std::min (taps, size - pos);
        float* __restrict b = buf.data();
        const float* __restrict k = h.data();
        for (int i = 0; i < first; ++i)
            b[pos + i] += x * k[i];
        for (int i = first; i < taps; ++i)
            b[pos + i - size] += x * k[i];
        for (int i = 0; i < os; ++i)
        {
            const auto j = (pos + i) % size;
            out[i] = buf[(std::size_t) j];
            buf[(std::size_t) j] = 0.0f;
        }
        pos = (pos + os) % size;
    }
};

// static decimator coefficient vectors (§3.3), as (float index → 4 lanes)
struct LaneInit
{
    int at;
    float v[4];
};
constexpr LaneInit hb1Init[] = {
    { 0x00, { -0.934835732f, -0.934835732f, -0.97113204f, -0.97113204f } }, { 0x04, { 0.229390338f, 0.229390338f, 0.277123123f, 0.277123123f } },
    { 0x1c, { -1.02521265f, -1.02521265f, -1.08984041f, -1.08984041f } },  { 0x20, { 0.348243773f, 0.348243773f, 0.433235019f, 0.433235019f } },
    { 0x30, { -1.15824544f, -1.15824544f, -1.22538829f, -1.22538829f } },  { 0x34, { 0.523193598f, 0.523193598f, 0.611492157f, 0.611492157f } },
    { 0x44, { -1.28833306f, -1.28833306f, -1.34602785f, -1.34602785f } },  { 0x48, { 0.694270074f, 0.694270074f, 0.770143688f, 0.770143688f } },
    { 0x58, { -1.39886069f, -1.39886069f, -1.44824684f, -1.44824684f } },  { 0x5c, { 0.839623451f, 0.839623451f, 0.904570639f, 0.904570639f } },
    { 0x6c, { -1.496364f, -1.496364f, -0.461006314f, -0.461006314f } },     { 0x70, { 0.967848837f, 0.967848837f, 0.0f, 0.0f } } };
constexpr LaneInit hb2Init[] = {
    { 0x00, { 0.0366815031f, 0.0366815031f, 0.136547625f, 0.136547625f } }, { 0x18, { 0.274631768f, 0.274631768f, 0.423138618f, 0.423138618f } },
    { 0x28, { 0.561098993f, 0.561098993f, 0.677540064f, 0.677540064f } },  { 0x38, { 0.769741833f, 0.769741833f, 0.839889646f, 0.839889646f } },
    { 0x48, { 0.89226079f, 0.89226079f, 0.931541979f, 0.931541979f } },     { 0x58, { 0.962094545f, 0.962094545f, 0.987816393f, 0.987816393f } } };

using Lanes = std::array<float, 4>;

struct Decimator1 // 6-section 4-lane IIR (FUN_180016ed0)
{
    std::array<float, 0x80> p {};
    void reset() noexcept
    {
        p.fill (0.0f);
        for (const auto& e : hb1Init)
            std::copy (e.v, e.v + 4, p.begin() + e.at);
    }
    Lanes get (int a) const noexcept { return { p[(std::size_t) a], p[(std::size_t) a + 1], p[(std::size_t) a + 2], p[(std::size_t) a + 3] }; }
    void put (int a, const Lanes& v) noexcept { std::copy (v.begin(), v.end(), p.begin() + a); }

    void tick (float& l, float& r) noexcept
    {
        const Lanes in4 { l, r, l, r };
        const auto a14 = get (0x14), a10 = get (0x10), a3c = get (0x3c), a18 = get (0x18);
        put (0x14, a18);
        const auto a2c = get (0x2c);
        put (0x10, in4);
        const auto a40 = get (0x40), a0c = get (0x0c);
        put (0x08, a14);
        const auto a54 = get (0x54);
        put (0x3c, a40);
        put (0x0c, a10);
        const auto a68 = get (0x68);
        Lanes s1, s2, s3, s4, s5, s6;
        for (int i = 0; i < 4; ++i)
            s1[(std::size_t) i] = ((a10[(std::size_t) i] - a18[(std::size_t) i]) * p[(std::size_t) i] + a0c[(std::size_t) i])
                                  + (in4[(std::size_t) i] - a14[(std::size_t) i]) * p[(std::size_t) (4 + i)];
        const auto a28 = get (0x28);
        put (0x28, a2c);
        put (0x24, a28);
        put (0x38, a3c);
        put (0x18, s1);
        const auto a50 = get (0x50);
        put (0x50, a54);
        put (0x4c, a50);
        for (int i = 0; i < 4; ++i)
            s2[(std::size_t) i] = ((a18[(std::size_t) i] - a2c[(std::size_t) i]) * p[(std::size_t) (0x1c + i)] + a14[(std::size_t) i])
                                  + (s1[(std::size_t) i] - a28[(std::size_t) i]) * p[(std::size_t) (0x20 + i)];
        put (0x2c, s2);
        const auto a64 = get (0x64);
        put (0x64, a68);
        put (0x60, a64);
        for (int i = 0; i < 4; ++i)
            s3[(std::size_t) i] = ((a2c[(std::size_t) i] - a40[(std::size_t) i]) * p[(std::size_t) (0x30 + i)] + a28[(std::size_t) i])
                                  + (s2[(std::size_t) i] - a3c[(std::size_t) i]) * p[(std::size_t) (0x34 + i)];
        put (0x40, s3);
        put (0x74, get (0x78));
        for (int i = 0; i < 4; ++i)
            s4[(std::size_t) i] = ((a40[(std::size_t) i] - a54[(std::size_t) i]) * p[(std::size_t) (0x44 + i)] + a3c[(std::size_t) i])
                                  + (s3[(std::size_t) i] - a50[(std::size_t) i]) * p[(std::size_t) (0x48 + i)];
        const auto a7c = get (0x7c);
        put (0x54, s4);
        for (int i = 0; i < 4; ++i)
            s5[(std::size_t) i] = ((a54[(std::size_t) i] - a68[(std::size_t) i]) * p[(std::size_t) (0x58 + i)] + a50[(std::size_t) i])
                                  + (s4[(std::size_t) i] - a64[(std::size_t) i]) * p[(std::size_t) (0x5c + i)];
        put (0x68, s5);
        const auto a78 = get (0x78);
        for (int i = 0; i < 4; ++i)
            s6[(std::size_t) i] = ((a68[(std::size_t) i] - a7c[(std::size_t) i]) * p[(std::size_t) (0x6c + i)] + a64[(std::size_t) i])
                                  + (s5[(std::size_t) i] - a78[(std::size_t) i]) * p[(std::size_t) (0x70 + i)];
        put (0x7c, s6);
        put (0x78, a7c);
        l = (s6[2] + a7c[0]) * 0.5f;
        r = (s6[3] + a7c[1]) * 0.5f;
    }
};

struct Decimator2 // 2:1 all-pass half-band stage (FUN_180001940)
{
    std::array<float, 0x68> q {};
    void reset() noexcept
    {
        q.fill (0.0f);
        for (const auto& e : hb2Init)
            std::copy (e.v, e.v + 4, q.begin() + e.at);
    }
    Lanes get (int a) const noexcept { return { q[(std::size_t) a], q[(std::size_t) a + 1], q[(std::size_t) a + 2], q[(std::size_t) a + 3] }; }
    void put (int a, const Lanes& v) noexcept { std::copy (v.begin(), v.end(), q.begin() + a); }

    void tick (float& l, float& r) noexcept
    {
        const Lanes in4 { l, r, l, r };
        const auto a10 = get (0x10), a20 = get (0x20);
        put (0x1c, a20);
        const auto u = get (0x0c);
        put (0x04, a10);
        const auto a30 = get (0x30);
        put (0x2c, a30);
        put (0x0c, in4);
        Lanes t, t2, t3, t4, t5, t6;
        for (int i = 0; i < 4; ++i)
            t[(std::size_t) i] = (in4[(std::size_t) i] - a10[(std::size_t) i]) * q[(std::size_t) i] + q[(std::size_t) (8 + i)];
        put (0x08, u);
        put (0x10, get (0x14));
        put (0x14, t);
        put (0x20, get (0x24));
        put (0x30, get (0x34));
        for (int i = 0; i < 4; ++i)
            t2[(std::size_t) i] = (t[(std::size_t) i] - a20[(std::size_t) i]) * q[(std::size_t) (0x18 + i)] + a10[(std::size_t) i];
        put (0x24, t2);
        for (int i = 0; i < 4; ++i)
            t3[(std::size_t) i] = (t2[(std::size_t) i] - a30[(std::size_t) i]) * q[(std::size_t) (0x28 + i)] + a20[(std::size_t) i];
        const auto a40 = get (0x40);
        put (0x40, get (0x44));
        put (0x3c, a40);
        put (0x34, t3);
        for (int i = 0; i < 4; ++i)
            t4[(std::size_t) i] = (t3[(std::size_t) i] - a40[(std::size_t) i]) * q[(std::size_t) (0x38 + i)] + a30[(std::size_t) i];
        const auto a50 = get (0x50);
        put (0x50, get (0x54));
        const auto a60 = get (0x60);
        put (0x4c, a50);
        put (0x5c, a60);
        put (0x44, t4);
        for (int i = 0; i < 4; ++i)
            t5[(std::size_t) i] = (t4[(std::size_t) i] - a50[(std::size_t) i]) * q[(std::size_t) (0x48 + i)] + a40[(std::size_t) i];
        const auto old66 = q[0x66], old67 = q[0x67];
        put (0x60, { q[0x64], q[0x65], old66, old67 });
        put (0x54, t5);
        for (int i = 0; i < 4; ++i)
            t6[(std::size_t) i] = (t5[(std::size_t) i] - a60[(std::size_t) i]) * q[(std::size_t) (0x58 + i)] + a50[(std::size_t) i];
        put (0x64, t6);
        l = (t6[0] + old66) * 0.5f;
        r = (t6[1] + old67) * 0.5f;
    }
};

struct Delay // §3.6
{
    double fs = 44100.0;
    std::vector<float> bufL, bufR;
    int w = 0;
    float cs = 0, xstep = 0, target = 0, cur = 0, mixp = 0, stereo = 0.5f, time = 0, d42 = 0, d43 = 0, xw = 1, xd = 0, fb = 0;
    float lp[3] {}, hp[3] {}, stL[4] {}, stR[4] {}, fbL = 0, fbR = 0, tempo = 120;
    bool changed = false, sync = false, keep = false, busy = false, timeChanged = false;
    int tap = 1;

    void setup (double rate)
    {
        fs = rate;
        bufL.assign (65536, 0.0f);
        bufR.assign (65536, 0.0f);
        w = 0;
        cs = (float) (1.0 - std::pow (0.1, 10.0 / fs));
        xstep = 1.0f / (float) (fs * 0.1);
    }
    void setLowPass (float x)
    {
        const auto f = (float) (320.0 * std::pow (2.0, x * 6.0));
        const auto t = (float) std::tan (f * (1.0f / (float) fs) * (float) pi);
        const auto g = 1.0f / (t + 1.0f);
        lp[0] = g * t, lp[1] = g * t, lp[2] = (1.0f - t) * g;
    }
    void setHighPass (float x)
    {
        const auto f = (float) (20.0 * std::pow (2.0, x * 10.0));
        const auto t = (float) std::tan (f * (1.0f / (float) fs) * (float) pi);
        const auto g = 1.0f / (t + 1.0f);
        hp[0] = g, hp[1] = -g, hp[2] = (1.0f - t) * g;
    }
    void tick (float& left, float& right) noexcept
    {
        const auto L = left, R = right;
        if (changed)
        {
            timeChanged = true;
            changed = false;
            target = ! sync ? (float) (fs * time) : (float) (1 - (int) (time * -767.0f)) * (0.3125f / tempo) * (float) fs;
        }
        if (keep && ! busy && timeChanged)
        {
            auto step = xstep;
            timeChanged = false;
            busy = true;
            if (tap == 0)
                tap = 1, d42 = (float) (int) target;
            else
                step = -step, tap = 0, d43 = (float) (int) target;
            xd = step;
        }
        constexpr int m = 65535;
        bufL[(std::size_t) w] = fb * fbL + L;
        bufR[(std::size_t) w] = fb * fbR + R;
        float dL, dR;
        if (! keep)
        {
            cur = (target - cur) * cs + cur;
            const auto di = (int) cur;
            const auto fr = cur - (float) di;
            const auto i1 = (w - di) & m, i2 = (w - di - 1) & m;
            dR = (1.0f - fr) * bufR[(std::size_t) i1] + fr * bufR[(std::size_t) i2];
            dL = (1.0f - fr) * bufL[(std::size_t) i1] + fr * bufL[(std::size_t) i2];
        }
        else
        {
            const auto i1 = (w - (int) d42) & m, i2 = (w - (int) d43) & m;
            dL = xw * bufL[(std::size_t) i1] + (1.0f - xw) * bufL[(std::size_t) i2];
            dR = xw * bufR[(std::size_t) i1] + (1.0f - xw) * bufR[(std::size_t) i2];
            if (busy)
            {
                xw += xd;
                if (xw < 0.0f)
                    xw = 0.0f, busy = false;
                else if (xw > 1.0f)
                    xw = 1.0f, busy = false;
            }
        }
        w = (w + 1) & m;
        auto filt = [&] (float d, float* st, float& feedback) {
            const auto y = (lp[1] * st[0] + d * lp[0]) + lp[2] * st[1];
            const auto hy = (hp[1] * st[1] + y * hp[0]) + hp[2] * st[3];
            st[0] = d, st[1] = y, st[2] = y, st[3] = hy;
            feedback = hy;
        };
        filt (dL, stL, fbL);
        filt (dR, stR, fbR);
        const auto gA = (2.58557272f - (1.0f - stereo) * 1.17114568f) * (1.0f - stereo);
        const auto gB = (2.58557272f - stereo * 1.17114568f) * stereo;
        right = mixp * dR * gB + gA * R;
        left = mixp * dL * gA + gB * L;
    }
};

class VintagePhaser final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        fs = sr;
        last.fill (-1.0f);
        delay.setup (fs);
        rebuild();
    }

    void reset() noexcept override { prepare (fs); }

    void setSongPosition (double ticks) noexcept override { songBeats = ticks / 960.0; }

    void process (float* l, float* r, int n, const EffectParams& params, double bpm) noexcept override
    {
        for (int i = 0; i < vintagePhaserParamCount; ++i)
            if (params[(std::size_t) i] != last[(std::size_t) i])
            {
                last[(std::size_t) i] = params[(std::size_t) i];
                set (i, params[(std::size_t) i]);
            }
        blockStart (songBeats, (float) bpm);
        for (int i = 0; i < n; ++i)
            tick (l[i], r[i]);
    }

private:
    void rebuild()
    {
        for (int i = 0; i < vintagePhaserParamCount; ++i)
            set (i, p[(std::size_t) i], true);
        ups[0].setup (oversampling);
        ups[1].setup (oversampling);
        hb1.reset();
        hb2.reset();
        k = (float) pi / (float) (fs * oversampling);
        driveCoef = (float) (1.0 - std::pow (0.05, 20.0 / fs));
        v2s = {};
        yv1 = { 0.0f, 0.0f };
        ap.fill (0.0f);
    }

    void set (int i, float v, bool init = false)
    {
        p[(std::size_t) i] = v;
        switch (i)
        {
            case 0: case 1:
            {
                const auto a = 1.25f * (float) std::pow (2.0, p[0] * 14.0f), b = 1.25f * (float) std::pow (2.0, p[1] * 14.0f);
                fmin = std::min (a, b), fmax = std::max (a, b);
                range = (float) (std::log (fmax / fmin) * 1.4426950408889634);
                break;
            }
            case 2: case 35: fbv = p[2] * 0.9f * (p[35] >= 0.5f ? -1.0f : 1.0f); break;
            case 3: lfoOn = v < 0.5f; break;
            case 4: manual = 1.25f * (float) std::pow (2.0, v * 14.0f); break;
            case 5:
            {
                const auto os = v >= 0.5f ? 8 : 4;
                if (os != oversampling)
                {
                    oversampling = os;
                    if (! init)
                        rebuild();
                }
                break;
            }
            case 7: sync = v >= 0.5f, armed = false, set (6, p[6]); break;
            case 6: case 29: case 30:
            {
                auto rateOf = [] (float x) { return (x * x) * (x * x) * 149.975006f + 0.025f; };
                const auto r1 = rateOf (p[30]), r2 = rateOf (p[29]);
                const auto lo = std::min (r1, r2), hi = std::max (r1, r2);
                const auto rate = (float) std::pow (2.0, std::log (hi / lo) * 1.4426950408889634 * p[6]) * lo;
                inc = rate / fs;
                const auto steps = (int) ((std::pow (2.0f, (1.0f - p[6]) * 7.0f) - 1.0f) * 48.370079f + 1.10000002f);
                syncLength = steps * (1.0 / 192.0);
                if (sync)
                    armed = false;
                break;
            }
            case 8: wave = v >= 0.5f ? 1 : 0; break;
            case 9:
            {
                const auto t = ((v + 1.0f) * 0.5f) * 2.0f - 1.0f;
                const auto sg = t > 0.0f ? 0.1f : -0.1f;
                const auto kk = (float) (std::exp (std::abs (t) * 8.75573753931) - 1.0) * sg;
                if (kk != tens[0])
                {
                    tens[0] = kk;
                    if (kk != 0.0f)
                        tens[1] = 1.0f / std::abs (kk), tens[2] = (float) std::log (std::abs (kk) + 1.0f);
                }
                break;
            }
            case 23: mono = v >= 0.5f; break; // core 23 = VST 22 "Mono Input"
            case 22:                          // core 22 = VST 23 "Noise"
                noise = v == 0.0f ? 0.0f : (float) std::pow (10.0, v * 2.5 - 4.5) * 0.35f;
                threshold = noise * 8.0f;
                break;
            case 24: gate = v >= 0.5f; break;
            case 25: case 26: shelf.lowShelf (p[26] * 600.0f + 50.0f, fs, p[25] * 30.0f); break;
            case 27: inHp.highPass1 (v * v * v * 195.0f + 5.0f, fs); break;
            case 33: v2 = v >= 0.5f, set (34, p[34]); break;
            case 34: driveTarget = v2 ? (float) std::pow (10.0, p[34] * 1.5 - 0.75) : 1.0f; break;
            case 12: delayOn = v >= 0.5f; break;
            case 14: delay.mixp = v; break;
            case 15: delay.stereo = v; break;
            case 16: delay.time = v, delay.changed = true; break;
            case 17: delay.sync = v >= 0.5f; break;
            case 13:
                delay.keep = v > 0.5f, delay.xw = 1.0f, delay.busy = false;
                if (delay.keep)
                    delay.tap = 1, delay.timeChanged = false, delay.xd = 0.0f, delay.d42 = delay.d43 = (float) (int) delay.target;
                else
                    delay.tap = 0;
                break;
            case 18: delay.fb = v; break;
            case 19: delay.setLowPass (v); break;
            case 20: delay.setHighPass (v); break;
            default: break;
        }
    }

    void blockStart (double ppq, float tempo) noexcept
    {
        if (prevPpq < 0.0 && ppq >= 0.0)
            armed = true;
        prevPpq = ppq;
        delay.tempo = tempo;
        if (sync)
        {
            if (armed && ppq >= 0.0)
            {
                const auto x = ppq / syncLength + p[10];
                phase = x - std::floor (x);
            }
            inc = tempo / 60.0 / syncLength / fs;
        }
    }

    float tension (float w) const noexcept
    {
        const auto kk = tens[0], ik = tens[1], c = tens[2];
        if (kk == 0.0f)
            return w;
        if (kk > 0.0f)
            return (float) (1.0 - (std::exp ((1.0f - w) * c) - 1.0) * ik);
        return (float) ((std::exp (c * w) - 1.0) * ik);
    }

    float waveAt (float ph) const noexcept
    {
        if (wave == 1)
            return ((float) std::sin (ph * (float) (2.0 * pi)) + 1.0f) * 0.5f;
        if (ph < 0.25f)
            return ph + ph + 0.5f;
        if (ph < 0.75f)
            return 1.0f - ((ph - 0.25f + ph) - 0.25f);
        return (ph - 0.75f + ph) - 0.75f;
    }

    void lfo() noexcept
    {
        float fL, fR;
        if (lfoOn)
        {
            const auto ph = (float) phase;
            wL = tension (waveAt (ph));
            auto q = ph + p[11];
            q -= (float) (int) q;
            wR = tension (waveAt (q));
            const auto d = (1.0f - p[6]) * (1.0f - p[31]) + p[31];
            const auto o = (1.0f - d) - (p[6] * p[32] * p[6]) * (p[6] * p[6]);
            fL = exp2Fast ((d * wL + o) * range) * fmin;
            fR = exp2Fast ((d * wR + o) * range) * fmin;
            phase += inc;
            phase -= std::floor (phase);
        }
        else
        {
            wL = wR = 0.0f;
            fL = fR = manual;
        }
        if (v2)
            g[0] = tanG (fL, k), g[1] = tanG (fR, k);
        else
        {
            const auto t = 1.0 / (fs * oversampling);
            for (int c = 0; c < 2; ++c)
            {
                const auto tn = std::tan (pi * (c == 0 ? fL : fR) * t);
                a[c] = (tn - 1.0) / (tn + 1.0);
            }
        }
    }

    float v2Core (int ch, float x) noexcept
    {
        const auto gg = g[ch], aa = gg + gg - 1.0f, bb = 1.0f - gg;
        const auto x3 = p[28] * p[28] * p[28];
        const auto g2 = tanG (x3 * 195.0f + 5.0f, k);
        const auto hpc = 1.0f - g2;
        const auto kk = 1.0f / (1.0f - aa * aa * aa * aa * fbv);
        auto& s = v2s[(std::size_t) ch];
        const auto pred = ((((((s[0] + s[0]) * bb) * aa + (s[1] + s[1]) * bb) * aa + (s[2] + s[2]) * bb) * aa + (s[3] + s[3]) * bb)) + (-s[4] * hpc);
        auto u = (pred * fbv + x) * kk - s[0];
        float y = 0.0f;
        for (int j = 0; j < 4; ++j)
        {
            const auto sg = tanhPade (u), t = gg * sg, lp = s[(std::size_t) j] + t;
            s[(std::size_t) j] = lp + t;
            y = lp - bb * sg;
            if (j < 3)
                u = y - s[(std::size_t) j + 1];
        }
        const auto d = (y - s[4]) * g2;
        s[4] = s[4] + d + d;
        return y;
    }

    void v1Core (float& left, float& right) noexcept
    {
        const float in[2] { fbv * yv1[0] + left, fbv * yv1[1] + right };
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            auto v = (double) fbHp.tick (c, in[c]);
            for (int j = 0; j < 4; ++j)
            {
                const auto idx = (std::size_t) (2 * j + c);
                const auto prevX = (double) ap[idx], prevY = (double) ap[idx + 2];
                const auto o = (v - prevY) * a[c] + prevX;
                ap[idx] = (float) v;
                v = o;
            }
            ap[(std::size_t) (8 + c)] = (float) v;
            out[c] = (float) v;
        }
        yv1 = { out[0], out[1] };
        left = out[0];
        right = out[1];
    }

    void tick (float& left, float& right) noexcept
    {
        lfo();
        if (! v2)
            fbHp.highPass1 (p[28] * p[28] * p[28] * 195.0f + 5.0f, fs * oversampling);
        auto L = left, R = right;
        if (mono)
            L = R = (L + R) * 0.5f;
        auto xL = shelf.tick (0, L), xR = shelf.tick (1, R);
        xL = inHp.tick (0, xL);
        xR = inHp.tick (1, xR);
        const auto g0 = drive;
        drive = (driveTarget - g0) * driveCoef + g0;
        float upL[8], upR[8];
        ups[0].push (g0 * xL, upL);
        ups[1].push (g0 * xR, upR);
        const auto m = p[21];
        for (int j = 0; j < oversampling; ++j)
        {
            const auto aL = upL[j], bR = upR[j];
            float yL, yR;
            if (v2)
                yL = v2Core (0, aL), yR = v2Core (1, bR);
            else
            {
                yL = aL, yR = bR;
                v1Core (yL, yR);
            }
            upL[j] = m * yL + (1.0f - m) * aL;
            upR[j] = m * yR + (1.0f - m) * bR;
        }
        if (v2)
        {
            for (int j = 0; j < oversampling; ++j)
                hb1.tick (upL[j], upR[j]);
            if (oversampling == 8)
                for (const int j : { 0, 4 })
                    hb2.tick (upL[j], upR[j]);
        }
        const auto ig = 1.0f / drive;
        auto oL = ig * upL[0], oR = ig * upR[0];
        float nL = 0.0f, nR = 0.0f;
        if (noise > 0.0f)
        {
            noiseShelf[0].lowShelf (exp2Fast (3.21501279f * wL) * 280.0f, fs, 18.0);
            noiseShelf[1].lowShelf (exp2Fast (3.21501279f * wR) * 280.0f, fs, 18.0);
            seedL = seedL * 0x0BB38435u + 0x3619636Bu;
            seedR = seedR * 0x0BB38435u + 0x3619636Bu;
            nL = noiseShelf[0].tick (0, (float) (std::int32_t) seedL * 4.65661287e-10f) * noise;
            nR = noiseShelf[1].tick (0, (float) (std::int32_t) seedR * 4.65661287e-10f) * noise;
            oL += nL;
            oR += nR;
        }
        if (gate)
        {
            if (std::abs (oL) >= threshold || std::abs (oR) >= threshold || std::abs (nL + L) >= threshold || std::abs (nR + R) >= threshold)
                holdCount = 440, holdGain = 1.0f;
            if (holdCount > 0)
                --holdCount;
            else
                holdGain *= gateRelease;
            oL *= holdGain;
            oR *= holdGain;
        }
        if (delayOn)
            delay.tick (oL, oR);
        left = oL;
        right = oR;
    }

    double fs = 44100.0, phase = 0.0, inc = 0.0, syncLength = 1.0, songBeats = 0.0, prevPpq = -1.0;
    std::array<float, vintagePhaserParamCount> p {}, last {};
    int oversampling = 8, wave = 0;
    bool v2 = true, lfoOn = true, mono = false, gate = false, delayOn = false, sync = false, armed = false;
    float fmin = 1.25f, fmax = 20480.0f, range = 14.0f, fbv = 0.0f, manual = 1.25f, k = 0.0f;
    std::array<float, 3> tens {};
    float noise = 0.0f, threshold = 0.0025f, holdGain = 1.0f, wL = 0.0f, wR = 0.0f;
    int holdCount = 440;
    float drive = 1.0f, driveTarget = 1.0f, driveCoef = 0.0f;
    float gateRelease = 0.99965f; // exact value set from FL's constant bits in the constructor
    std::uint32_t seedL = 239894u, seedR = 239894u + 0x1099Eu;
    Biquad shelf, inHp, fbHp, noiseShelf[2];
    Upsampler ups[2];
    Decimator1 hb1;
    Decimator2 hb2;
    Delay delay;
    std::array<std::array<float, 5>, 2> v2s {};
    std::array<float, 2> yv1 {};
    std::array<float, 12> ap {};
    float g[2] { 0.5f, 0.5f };
    double a[2] { 0.0, 0.0 };

public:
    VintagePhaser()
    {
        inHp.secondOrder = fbHp.secondOrder = true;
        // engine constructor values (§3.2 / reference model)
        p[0] = 0.4375f;
        std::uint32_t bits = 0x3f469efcu;
        std::memcpy (&p[1], &bits, 4);
        p[2] = 0.5f, p[15] = 0.5f, p[21] = 0.5f, p[30] = 1.0f, p[31] = 1.0f, p[33] = 1.0f;
        std::uint32_t releaseBits = 0x3f7fe931u;
        std::memcpy (&gateRelease, &releaseBits, 4);
        delay.setup (fs);
        rebuild();
    }
};

// DFM ranges of the front-end controls in VST index order (§3.2); unlisted = 0…1 (switches)
constexpr std::array<std::array<int, 2>, 36> frontRanges = { {
    { 125, 2048000 }, { 125, 2048000 }, { 0, 6000 }, { 0, 1 }, { 125, 2048000 }, { 0, 1 }, { 1, 245760 }, { 0, 1 }, { 0, 1 },
    { 0, 6000 }, { 0, 6000 }, { 0, 6000 }, { 0, 1 }, { 0, 1 }, { 0, 6000 }, { 0, 6000 }, { 1, 768 }, { 0, 1 }, { 0, 6000 },
    { 3200, 204800 }, { 2000, 204800 }, { 0, 100 }, { 0, 1 }, { 0, 6000 }, { 0, 1 }, { 0, 6000 }, { 5000, 650000 },
    { 2000, 80000 }, { 2000, 80000 }, { 25, 150000 }, { 25, 150000 }, { 0, 6000 }, { 0, 6000 }, { 0, 1 }, { 5000, 650000 }, { 0, 1 } } };
} // namespace

std::unique_ptr<Effect> makeVintagePhaser() { return std::make_unique<VintagePhaser>(); }

std::optional<EffectSlot> vintagePhaserFromFl (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4)
        return std::nullopt;
    std::int32_t version = 0;
    std::memcpy (&version, d.data(), 4);
    std::array<std::int32_t, 36> raw {};
    if (version == 1)
    {
        static constexpr int order[32] { 0, 1, 2, 3, 4, 6, 8, 11, 7, 10, 21, 22, 23, 5, 12, 13, 14, 15, 16, 17, 18, 19, 20, 25, 26, 27, 28, 29, 30, 31, 32, 9 };
        if (d.size() < 4 + 32 * 4)
            return std::nullopt;
        for (int i = 0; i < 32; ++i)
            std::memcpy (&raw[(std::size_t) order[i]], d.data() + 4 + 4 * i, 4);
    }
    else if (version == 2 || version == 3)
    {
        if (d.size() < 4 + 36 * 4)
            return std::nullopt;
        std::memcpy (raw.data(), d.data() + 4, 36 * 4);
        if (version < 3)
            raw[33] = raw[34] = raw[35] = 0; // older presets load as engine V1, minimum drive, feedback not inverted
    }
    else
        return std::nullopt;
    auto slot = makeEffectSlot (EffectKind::vintagePhaser);
    for (int i = 0; i < 36; ++i)
    {
        const auto [lo, hi] = frontRanges[(std::size_t) i];
        const auto x = std::clamp ((double) (raw[(std::size_t) i] - lo) / (double) (hi - lo), 0.0, 1.0);
        const auto core = i == 22 ? 23 : i == 23 ? 22 : i; // the engine swaps VST indices 22 and 23
        slot.params[(std::size_t) core] = (float) x;
    }
    return slot;
}
} // namespace lynx
