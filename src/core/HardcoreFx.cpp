// FL Studio's Hardcore (REA_rodada71, clean-room): state, parameters, the five pedal boxes, cabinet and EQ.
// Our own code, written from the behaviour documented in docs/analise/REA_rodada71.md. The plug-in computes in float32
// with explicit double steps; this file keeps those types and the operation order of every expression (contraction
// off), which is what makes it bit-identical to the plug-in.

#include "core/HardcoreFx.h"

#include "core/Effects.h"
#include "core/IlFxKit.h"
#include "core/IldspMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

#include "core/Simd.h"
#if LYNX_X86
 #include <cpuid.h>
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::hardcore
{
namespace
{
using namespace ilfx;

constexpr float tinyNeg = -1e-9f;

// The plug-in's statically linked CRT float routines are the same code as Shared/ildsp_x64.dll's (FMA path on CPUs
// with FMA, chosen at run time): the re-creations of core/IldspMath.h (REA_rodada60 §4.5) are used
struct HcCrt
{
    static float powf (float x, float y) noexcept { return ildsp::powf_ (x, y); }
    static float sinf (float x) noexcept { return ildsp::sinf_ (x); }
    static float cosf (float x) noexcept { return ildsp::cosf_ (x); }
    static void sincosf (float x, float& s, float& c) noexcept { ildsp::sincosf_ (x, s, c); }
    static float expf (float x) noexcept { return ildsp::expf_ (x); }
    static float tanf (float x) noexcept { return tanfCrt (x); } // no FMA path in this routine
};
using LpBiquad = LpBiquadT<HcCrt>;
using EqBand = EqBandT<HcCrt>;

// cvttsd2si into an unsigned 32-bit count (the plug-in's (uint)(long long) x)
unsigned u32 (double x) noexcept { return (unsigned) (long long) x; }

// float sine/cosine pair of the plug-in's packed kernel (small arguments: x, 1 − x²/2)
void sinCosF (float w, float& s, float& c) noexcept { ildsp::sincosf_ (w, s, c); }

// double pair (sin x, sin(|x| + π/2)) of the plug-in's packed double kernel (small arguments: x, 1 − x²/2)
void sinCosD (double x, double& s, double& c) noexcept
{
    const auto a = std::fabs (x);
    if (a < 7.450580596923828e-09)
    {
        s = x;
        c = 1.0 - (a * a) * 0.5;
        return;
    }
    s = std::sin (x);
    c = std::sin (a + 1.5707963267948966);
}

// ------------------------------------------------------------------------------------------------ small filters
// second-order high-pass with a design cache (the plug-in's FUN_18001d050 / FUN_18001d150)
struct HpBiquad
{
    float f = 0, q = 0, fs = 0, a = 0, b1 = 0, b2 = 0, x2 = 0, x1 = 0, y2 = 0, y1 = 0;

    HpBiquad()
    {
        design (100.0f, 1.0f, 44100.0f);
        clear();
    }
    void clear() noexcept { x2 = x1 = y2 = y1 = 0.0f; }
    void design (float nf, float nq, float nfs) noexcept
    {
        if (nf == f && nq == q && nfs == fs)
            return;
        f = nf, q = nq, fs = nfs;
        const auto w = (float) ((double) f * 6.2831853071795862 / (double) fs);
        float s = 0, c = 0;
        sinCosF (w, s, c);
        const auto inv = 1.0f / (s + q);
        const auto den = s / q + 2.0f;
        const auto na = (c + 1.0f) / den;
        const auto nb1 = (c * inv) * (q * -2.0f);
        const auto nb2 = (q - s) * inv;
        a = tiny0 (na), b1 = tiny0 (nb1), b2 = tiny0 (nb2);
    }
    float tick (float x) noexcept
    {
        x = tiny0 (x);
        auto y = (((x - (x1 + x1)) + x2) * a - y1 * b1) - b2 * y2;
        y = tiny0 (y);
        x2 = x1, y2 = y1, x1 = x, y1 = y;
        return y;
    }
};

// band-pass with a design cache, designed in double (FUN_18001d420 / FUN_18001d580)
struct BpBiquad
{
    float f = 0, q = 0, fs = 0, b0 = 0, a1 = 0, b2 = 0, x2 = 0, x1 = 0, y2 = 0, y1 = 0;

    BpBiquad()
    {
        design (100.0f, 1.0f, 44100.0f);
        clear();
    }
    void clear() noexcept { x2 = x1 = y2 = y1 = 0.0f; }
    void design (float nf, float nq, float nfs) noexcept
    {
        if (nf == f && nq == q && nfs == fs)
            return;
        f = nf, q = nq, fs = nfs;
        const auto w = (double) f * 6.2831853071795862 / (double) fs;
        double s = 0, c = 0;
        sinCosD (w, s, c);
        const auto d = 1.0 / ((0.5 / (double) q) * s + 1.0);
        const auto nb0 = (float) ((double) (float) (s * 0.5) * d);
        const auto na1 = (float) ((double) (float) (c + c) * d);
        const auto nb2 = (float) ((double) (float) (1.0 - s / (double) (q + q)) * d);
        b0 = tiny0 (nb0), a1 = tiny0 (na1), b2 = tiny0 (nb2);
    }
    float tick (float x) noexcept
    {
        x = tiny0 (x);
        auto y = ((x - x2) * b0 + y1 * a1) - b2 * y2;
        y = tiny0 (y);
        x2 = x1, y2 = y1, x1 = x, y1 = y;
        return y;
    }
};

// the soft clipper shared by the two distortions: 1 − 10^(−k·x)·(1 − x) above zero, 10^(k·x)·(x + 1) − 1 below
float clip (float x, float k) noexcept
{
    if (x >= 0.0f)
        return 1.0f - ildsp::powf_ (10.0f, -(k * x)) * (1.0f - x);
    return ildsp::powf_ (10.0f, k * x) * (x + 1.0f) - 1.0f;
}

// a tape line built for a size and start phase (CTapeDelay's constructor: time 0.5, gain 1, no feedback/speed/depth)
void build (Tape& t, unsigned size, double phase)
{
    const auto n = (int) std::max (4u, size);
    t.reserve (n);
    t.init (n, phase);
}

// ------------------------------------------------------------------------------------------------ pedals
struct PedalFx
{
    bool dirty = true; // cleared before the next block (constructor, box switched on)
    virtual ~PedalFx() = default;
    virtual void setRate (double fs) = 0;
    virtual void reset() noexcept = 0;
    virtual void setParam (int k, float v) noexcept = 0;
    virtual void process (float* L, float* R, int n) noexcept = 0;
};

// type 2: three modulated tape lines on the mono sum, added to the input (FUN_18002aff0 …)
struct Chorus final : PedalFx
{
    double fs = 0;
    float depth = 0.5f, rate = 0.5f, mix = 0.5f; // knobs 0, 1, 2
    Tape t[3];
    bool built = false;

    void setRate (double rate_) override
    {
        fs = rate_;
        const auto base = fs * 0.02;
        const auto b = (double) (int) base;
        build (t[0], u32 (b * 1.1000000000000001), 0.0);
        build (t[1], u32 (b * 0.85399999999999998), 0.46700000000000003);
        build (t[2], (unsigned) (int) base, 0.77800000000000002);
        built = true;
        update();
        reset();
    }
    void update() noexcept
    {
        if (! built)
            return;
        const auto rr = (double) (float) ((double) (rate * 5.0f) + 0.10000000000000001);
        t[0].setInc (rr * 0.89600000000000002, fs);
        t[1].setInc (rr, fs);
        t[2].setInc (rr * 0.68899999999999995, fs);
        const auto g = (double) -(mix * mix);
        for (auto& x : t)
            x.setGain (g);
        const auto d = (double) ((depth * depth) * depth);
        for (auto& x : t)
            x.setDepth (d);
    }
    void reset() noexcept override
    {
        if (built)
            for (auto& x : t)
                x.reset();
    }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
            depth = v;
        else if (k == 1)
            rate = v;
        else if (k == 2)
            mix = v;
        update();
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const auto x = L[i] + R[i];
            const auto y0 = t[0].tick (x);
            const auto y2 = t[2].tick (x);
            const auto y1 = t[1].tick (x);
            L[i] = (y1 + y0) + L[i];
            R[i] = (y1 + y2) + R[i];
        }
    }
};

// type 9: per-channel gain computer (FUN_18002b430)
struct Compressor final : PedalFx
{
    float envL = 0, envR = 0;
    double fs = 44100.0;
    float attack = 0, release = 0, ratio = 0, thr = 0;

    void setRate (double rate) override
    {
        fs = rate;
        reset();
    }
    void reset() noexcept override { envL = envR = 0.0f; }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
            thr = (float) std::pow (10.0, (double) ((1.0f - v) * -3.2f));
        else if (k == 1)
            ratio = v;
        else if (k == 2)
            attack = v;
        else if (k == 3)
            release = v;
    }
    void process (float* L, float* R, int n) noexcept override
    {
        const auto a2 = attack * attack;
        const auto slope = 1.0f / ((1.0f - ratio) * 30.0f + 1.0f);
        const auto att = 8.8199997771880589 / ((double) ((a2 + a2) + 0.1f) * fs);
        const auto rel = 8.8199997771880589 / ((double) ((release * release) * 4.8f + 0.2f) * fs);
        auto gainFor = [&] (float x) {
            const auto a = std::fabs (x);
            auto t = 1.0f;
            if (thr < a)
                t = thr / ((a - thr) * slope + thr);
            return t;
        };
        for (int i = 0; i < n; ++i)
        {
            const auto xl = L[i];
            auto t = gainFor (xl);
            envL = (float) (t < 1.0f ? att : rel) * (t - envL) + envL;
            const auto xr = R[i];
            t = gainFor (xr);
            envR = (float) (t < 1.0f ? att : rel) * (t - envR) + envR;
            L[i] = envL * xl;
            R[i] = envR * xr;
        }
    }
};

// types 6 and 3: one tape line per channel added to it (FUN_18002b730)
struct TapePair : PedalFx
{
    Tape t[2];
    double fs = 0;
    bool built = false;

    void reset() noexcept override
    {
        if (built)
            t[0].reset(), t[1].reset();
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const auto r = R[i];
            L[i] = t[0].tick (L[i]) + L[i];
            R[i] = t[1].tick (r) + R[i];
        }
    }
};

struct Delay final : TapePair
{
    float gain = 0, time = 0, fb = 0;

    void setRate (double rate) override
    {
        fs = rate;
        for (int k = 0; k < 2; ++k)
        {
            build (t[k], u32 (fs), k == 0 ? 0.0 : 0.5);
            t[k].setTime ((double) time);
            t[k].setGain ((double) gain);
            t[k].setFb ((double) fb);
            t[k].setInc (1.0, fs);
            t[k].setDepth ((double) 0.01f);
        }
        built = true;
        reset();
    }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
        {
            time = v < 0.1f ? 0.1f : (v > 1.0f ? 1.0f : v);
            if (built)
                t[0].setTime ((double) time), t[1].setTime ((double) time);
        }
        else if (k == 1)
        {
            fb = (float) ((double) v * -0.94999999999999996 * (double) v);
            if (built)
                t[0].setFb ((double) fb), t[1].setFb ((double) fb);
        }
        else if (k == 2)
        {
            gain = (float) -((double) ((v * 0.9f) * v) + 0.10000000000000001);
            if (built)
                t[0].setGain ((double) gain), t[1].setGain ((double) gain);
        }
    }
};

struct Flanger final : TapePair
{
    float gain = 0, rate = 0, time = 0; // FL's ctor leaves gain/rate 0.5 and time 0.5 until the knobs arrive

    Flanger() { gain = rate = 0.5f; }
    void setRate (double fsIn) override
    {
        fs = fsIn;
        const auto n = u32 (fs * 0.0050000000000000001);
        for (int k = 0; k < 2; ++k)
        {
            build (t[k], n, k == 0 ? 0.0 : 0.5);
            t[k].setTime ((double) time);
            t[k].setGain ((double) gain);
            t[k].setInc ((double) rate, fs);
            t[k].setFb (-0.10000000000000001);
            t[k].setDepth (1.0);
        }
        built = true;
        reset();
    }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
        {
            auto d = v * v;
            d = d < 0.01f ? 0.01f : (d > 1.0f ? 1.0f : d);
            time = d;
            if (built)
                t[0].setTime ((double) d), t[1].setTime ((double) time);
        }
        else if (k == 1)
        {
            rate = (v * v) * (v * 10.0f) + 0.01f;
            if (built)
                t[0].setInc ((double) rate, fs), t[1].setInc ((double) rate, fs);
        }
        else if (k == 2)
        {
            gain = -0.1f - v * 0.9f;
            if (built)
                t[0].setGain ((double) gain), t[1].setGain ((double) gain);
        }
    }
};

// type 1: two band-passed paths, 8× oversampled through cubic interpolation, soft clipped, two short combs, low-pass,
// high-pass and a peaking tone filter (FUN_18002bc00)
struct DistModelA final : PedalFx
{
    float drive = 0.5f, comb = 0.5f, tone = 0.5f, body = 0.5f; // knobs 0, 1, 3 (EQ gain), 2 (high-pass Q)
    float fs = 0, pre = 1, k = 1;
    std::vector<float> buf1, buf2;
    int len1 = 1, idx1 = 0, len2 = 0, alloc2 = 1, idx2 = 0;
    float ha[3] {}, hb[3] {}; // interpolation histories (oldest first)
    BpBiquad bpA, bpB;
    LpBiquad lp;
    HpBiquad hp;
    EqBand eq;
    bool built = false;

    DistModelA() { eq.init (1000.0f, 1); }
    void setRate (double rate) override
    {
        fs = (float) rate;
        len1 = std::max (1, (int) ((double) (fs * 8.0f) * 0.001));
        buf1.assign ((std::size_t) len1, 0.0f);
        alloc2 = std::max (1, (int) ((double) (fs * 8.0f) * 0.00020000000000000001));
        buf2.assign ((std::size_t) alloc2, 0.0f);
        bpA.design (200.0f, 0.99f, fs);
        bpB.design (1000.0f, 0.99f, fs);
        eq.type = 1;
        eq.setBandwidth (3.0f);
        eq.setFreq (2500.0f);
        lp.design (500.0f, 4.0f, fs);
        built = true;
        reset();
        update();
    }
    void reset() noexcept override
    {
        bpA.clear(), bpB.clear();
        std::fill (buf1.begin(), buf1.end(), 0.0f);
        idx1 = 0;
        std::fill (buf2.begin(), buf2.end(), 0.0f);
        idx2 = 0;
        lp.clear(), hp.clear();
        for (int i = 0; i < 3; ++i)
            ha[i] = hb[i] = 0.0f;
    }
    void update() noexcept // FUN_18002c2f0
    {
        k = (drive * drive) * 99.0f + 1.0f;
        pre = 1.0f / (drive * 8.0f + 1.0f);
        hp.design (85.0f, body * 30.0f + 1.0f, fs);
        eq.setGain ((tone - 0.5f) * 20.0f);
        eq.design (fs);
        auto l = (int) ((float) alloc2 * comb);
        if (l >= alloc2)
            l = alloc2 - 1;
        len2 = l < 0 ? 0 : l;
    }
    void setParam (int kk, float v) noexcept override
    {
        if (kk == 0)
            drive = v;
        else if (kk == 1)
            comb = v;
        else if (kk == 2)
            body = v;
        else if (kk == 3)
            tone = v;
        if (built)
            update();
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const auto x = L[i] + R[i];
            const auto a = (float) ((double) bpA.tick (x) * 0.050000000000000003);
            const auto b = bpB.tick (x) * 5.0f;
            const auto y0 = ha[0], y1 = ha[1], y2 = ha[2], y3 = a;
            ha[0] = y1, ha[1] = y2, ha[2] = y3;
            const auto z0 = hb[0], z1 = hb[1], z2 = hb[2], z3 = b;
            hb[0] = z1, hb[1] = z2, hb[2] = z3;
            const auto c3 = (y3 - y0) * 0.5f + (y1 - y2) * 1.5f;
            const auto c1 = (y2 - y0) * 0.5f;
            const auto c2 = ((y0 - y1) + c1) - c3;
            const auto d1 = (z2 - z0) * 0.5f;
            const auto d3 = (z3 - z0) * 0.5f + (z1 - z2) * 1.5f;
            const auto d2 = ((z0 - z1) + d1) - d3;
            float t = 0.0f, y = 0.0f;
            for (int s = 0; s < 8; ++s)
            {
                auto va = ((t * c3 + c2) * t + c1) * t + y1;
                auto vb = ((t * d3 + d2) * t + d1) * t + z1;
                t = t + 0.125f;
                va = clip (va, k);
                vb = clip (vb, k);
                const auto old = buf1[(std::size_t) idx1];
                buf1[(std::size_t) idx1] = vb - old * 0.25f;
                if (++idx1 >= len1)
                    idx1 = 0;
                auto m = (float) ((double) old * 0.69999999999999996 + (double) va);
                if (len2 != 0)
                {
                    const auto o2 = buf2[(std::size_t) idx2];
                    buf2[(std::size_t) idx2] = m;
                    if (++idx2 >= len2)
                        idx2 = 0;
                    m = (float) ((double) m - (double) o2 * 0.29999999999999999);
                }
                y = lp.tick (m) * pre;
            }
            const auto out = eq.tick (hp.tick (y));
            L[i] = out;
            R[i] = out;
        }
    }
};

// type 11: shelf → low-pass → soft clip (fixed 8) → slow DC follower removed → shelf, mono (FUN_18002c520)
struct TubeDrive final : PedalFx
{
    LpBiquad lp;
    EqBand lo, hi;
    float hpState = 0, drive = 1.0f, knob1 = 0;
    double fs = 0;

    TubeDrive()
    {
        lo.init (1000.0f, 1);
        hi.init (1000.0f, 1);
        lo.type = 0, lo.setBandwidth (2.5f), lo.setFreq (200.0f), lo.dirty = true;
        hi.type = 2, hi.setBandwidth (2.0f), hi.setFreq (7000.0f), hi.dirty = true;
    }
    void setRate (double rate) override
    {
        fs = rate;
        lp.design (4000.0f, 0.5f, (float) rate);
        lo.design ((float) rate);
        hi.design ((float) rate);
        reset();
    }
    void reset() noexcept override
    {
        lp.clear();
        lo.clear(), hi.clear();
        hpState = 0.0f;
    }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
        {
            lo.setGain ((1.0f - v) * -28.0f);
            lo.design ((float) fs);
        }
        else if (k == 1)
        {
            knob1 = v;
            hi.setGain ((1.0f - v) * -28.0f);
            hi.design ((float) fs);
        }
        else if (k == 2)
            drive = (float) ((double) ((v * v) * (v * 10.0f)) + 0.01);
    }
    void process (float* L, float* R, int n) noexcept override
    {
        const auto h = drive * 0.5f;
        const auto comp = 1.0f - ildsp::powf_ (10.0f, h * -8.0f) * (1.0f - h);
        const auto outGain = 0.5f / comp;
        for (int i = 0; i < n; ++i)
        {
            auto x = lo.tick ((R[i] + L[i]) * drive);
            x = lp.tick (x);
            x = clip (x, 8.0f);
            hpState = (float) ((double) (x - hpState) * 0.01 + (double) hpState);
            const auto y = hi.tick (x - hpState) * outGain;
            L[i] = y;
            R[i] = y;
        }
    }
};

// type 8: four bands per channel, low shelf 300 Hz, peaks 700 Hz and 1.8 kHz, high shelf 7 kHz (FUN_18002cbc0)
struct Equalizer final : PedalFx
{
    EqBand l[4], r[4];
    double fs = 0;

    Equalizer()
    {
        static constexpr float f[4] { 300.0f, 700.0f, 1800.0f, 7000.0f };
        static constexpr int t[4] { 0, 1, 1, 2 };
        for (int b = 0; b < 4; ++b)
            for (auto* e : { &l[b], &r[b] })
            {
                e->init (1000.0f, 1);
                if (e->type != t[b])
                    e->dirty = true;
                e->type = t[b];
                e->setFreq (f[b]);
                e->setBandwidth (0.985f);
            }
    }
    void setRate (double rate) override
    {
        fs = rate;
        for (int b = 0; b < 4; ++b)
            l[b].design ((float) fs), r[b].design ((float) fs);
        reset();
    }
    void reset() noexcept override
    {
        for (int b = 0; b < 4; ++b)
            l[b].clear(), r[b].clear();
    }
    void setParam (int k, float v) noexcept override
    {
        if (k < 0 || k >= 4)
            return;
        const auto x = ((v - 0.5f) + v) - 0.5f;
        auto g = x * x;
        if (x < 0.0f)
            g = -g;
        g = g * 10.0f;
        l[k].setGain (g), r[k].setGain (g);
        l[k].design ((float) fs), r[k].design ((float) fs);
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int b = 0; b < 4; ++b)
            for (int i = 0; i < n; ++i)
                L[i] = l[b].tick (L[i]), R[i] = r[b].tick (R[i]);
    }
};

// type 10: tape vibrato on the mono sum with amplitude and pan modulation by one LFO (FUN_18002d270)
struct Modulator final : PedalFx
{
    double fs = 0, ph = 0, inc = 0;
    float k0 = 0.5f, amDepth = 0.5f, panDepth = 0.5f, k3 = 0.5f, am = 0, pan = 0;
    Tape t;
    bool built = false;

    void setRate (double rate) override
    {
        fs = rate;
        build (t, u32 (fs * 0.01), 0.0);
        built = true;
        update();
        reset();
    }
    void update() noexcept // FUN_18002d580
    {
        const auto r = (k3 * k3) * 30.0f;
        if (built)
        {
            auto d = k0 * k0;
            if (d <= 0.01f)
                d = 0.01f;
            if (1.0f <= d)
                d = 1.0f;
            t.setTime ((double) d);
            t.setGain (1.0);
            t.setFb (0.0);
            t.setInc ((double) r, fs);
            t.setDepth (1.0);
        }
        inc = (double) r / fs;
        if (inc < 0.0)
            inc = 0.0;
    }
    void reset() noexcept override
    {
        if (built)
            t.reset();
        ph = 0.0, inc = 0.0;
        am = pan = 0.0f;
        update();
    }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
            k0 = v;
        else if (k == 1)
            amDepth = v;
        else if (k == 2)
            panDepth = v;
        else if (k == 3)
            k3 = v;
        else
            return;
        update();
    }
    void process (float* L, float* R, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const auto lfo = (float) std::cos (ph * 6.2831853071795862);
            ph = ph + inc;
            if (ph >= 1.0)
                ph = ph - 1.0;
            const auto y = t.tick (L[i] + R[i]);
            const auto a = am, p = pan;
            const auto v = (1.0f - (a * lfo) * (lfo * lfo)) * y;
            const auto pl = p * lfo;
            am = (amDepth - a) * 0.001f + a;
            pan = (panDepth - p) * 0.001f + p;
            if (am < tiny9 && am > tinyNeg)
                am = 0.0f;
            if (pan < tiny9 && pan > tinyNeg)
                pan = 0.0f;
            L[i] = (1.0f - pl) * v;
            R[i] = (pl + 1.0f) * v;
        }
    }
};

// type 7: per-channel gate with hold (FUN_18002d6a0)
struct NoiseGate final : PedalFx
{
    unsigned cntL = 0, cntR = 0;
    float gL = 0, gR = 0, attack = 0, release = 0, thr = 0;
    double fs = 44100.0;

    void setRate (double rate) override
    {
        fs = rate;
        reset();
    }
    void reset() noexcept override
    {
        cntL = cntR = 0;
        gL = gR = 0.0f;
    }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
            attack = v;
        else if (k == 1)
            release = v;
        else if (k == 2)
            thr = (float) std::pow (10.0, (double) ((1.0f - v) * -3.2f));
    }
    void process (float* L, float* R, int n) noexcept override
    {
        const auto hold = u32 (fs * 0.10000000000000001);
        const auto a2 = attack * attack, r2 = release * release;
        const auto att = (float) (8.8200000000000003 / ((double) (float) ((double) (a2 + a2) + 0.050000000000000003) * fs));
        const auto rel = (float) (8.8200000000000003 / ((double) (float) ((double) (r2 + r2) + 0.050000000000000003) * fs));
        auto step = [&] (float x, unsigned& cnt, float& g) {
            float d = 0;
            if (std::fabs (x) <= thr)
            {
                ++cnt;
                d = cnt <= hold ? (1.0f - g) * att : -(g * rel);
            }
            else
            {
                cnt = 0;
                d = (1.0f - g) * att;
            }
            g = g + d;
            if (g < 1e-7f)
                g = 1e-7f;
        };
        for (int i = 0; i < n; ++i)
        {
            step (L[i], cntL, gL);
            step (R[i], cntR, gR);
            L[i] = gL * L[i];
            R[i] = gR * R[i];
        }
    }
};

// type 4: six first-order all-passes per channel swept by a cosine LFO, with feedback (FUN_18002d970)
struct Phaser final : PedalFx
{
    float rate = 0, mixK = 0, freqK = 0, depthK = 0; // knobs 0, 3, 1, 2
    double fs = 44100.0;
    float sL[6] {}, sR[6] {}, fbL = 0, fbR = 0, ph = 0, sfrq = -1.0f, smix = -1.0f;

    void setRate (double r) override
    {
        fs = r;
        reset();
    }
    void reset() noexcept override
    {
        for (int k = 0; k < 6; ++k)
            sL[k] = sR[k] = 0.0f;
        fbL = fbR = 0.0f;
        ph = 0.0f; // the plug-in clears the LFO phase with the right channel's feedback (one 8-byte store)
        sfrq = smix = -1.0f;
    }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
            rate = v;
        else if (k == 1)
            freqK = v;
        else if (k == 2)
            depthK = v;
        else if (k == 3)
            mixK = v;
    }
    void process (float* L, float* R, int n) noexcept override
    {
        const auto inv = 1.0 / fs;
        const auto lo = (float) (inv * 50.0), hi = (float) (inv * 20000.0);
        const auto K = (depthK * depthK) * (depthK * 0.5f);
        const auto frq = std::max (freqK * freqK, 9.99999975e-06f);
        const auto inc = (float) ((double) ((rate * 88200.0f) * rate) * inv) * 0.0005f;
        const auto mix = std::max (mixK, 9.99999975e-06f);
        if (0.0f > smix)
            smix = mix;
        if (0.0f > sfrq)
            sfrq = frq;
        const auto rng = hi - lo;
        for (int i = 0; i < n; ++i)
        {
            auto xl = L[i] - 9.99999997e-07f, xr = R[i] - 9.99999997e-07f;
            const auto c = ildsp::cosf_ (ph);
            auto nph = inc + ph;
            if (nph > 6.28318548f)
                nph = nph - 6.28318548f;
            ph = nph;
            const auto mm = (c + 1.0f) * K;
            auto wv = (sfrq + mm) * rng + lo;
            wv = wv < 0.499000013f ? wv : 0.499000013f;
            sfrq = (frq - sfrq) * 0.001f + sfrq;
            xl = xl + fbL * 0.454875678f;
            xr = xr + fbR * 0.454875678f;
            const auto a = (1.0f - wv) / (wv + 1.0f);
            for (int k = 0; k < 6; ++k)
            {
                const auto tl = sL[k] - a * xl;
                sL[k] = tl * a + xl;
                xl = tl;
                const auto tr = sR[k] - a * xr;
                sR[k] = tr * a + xr;
                xr = tr;
            }
            fbL = xl, fbR = xr;
            const auto sm = smix;
            smix = (mix - sm) * 0.001f + sm;
            L[i] = sm * xl + L[i];
            R[i] = sm * xr + R[i];
        }
    }
};

// type 5: the plate reverb of Toxic Biohazard's rack (class fxReverb) driven by three knobs (FUN_18002dfc0 …)
struct ReverbPedal final : PedalFx
{
    ReverbT<HcCrt> rev;

    ReverbPedal()
    {
        rev.allTaps = true;
        rev.construct();
    }
    void setRate (double fs) override
    {
        rev.setRate ((float) fs);
        reset();
    }
    void reset() noexcept override
    {
        rev.reset();
        rev.setp (2, 1.0f);
        rev.setp (5, 1.0f);
    }
    void setParam (int k, float v) noexcept override
    {
        if (k == 0)
            rev.setp (8, (float) ((double) ((v * v) * v) * 0.59999999999999998));
        else if (k == 1)
            rev.setp (9, std::fabs (1.0f - v));
        else if (k == 2)
            rev.setp (12, (float) ((double) (v * 0.999000013f) + 0.001));
    }
    void process (float* L, float* R, int n) noexcept override { rev.process (L, R, n); }
};

std::unique_ptr<PedalFx> makePedal (int type)
{
    switch (type)
    {
        case distModelA: return std::make_unique<DistModelA>();
        case chorus: return std::make_unique<Chorus>();
        case flanger: return std::make_unique<Flanger>();
        case phaser: return std::make_unique<Phaser>();
        case reverb: return std::make_unique<ReverbPedal>();
        case delay: return std::make_unique<Delay>();
        case noiseGate: return std::make_unique<NoiseGate>();
        case equalizer: return std::make_unique<Equalizer>();
        case compressor: return std::make_unique<Compressor>();
        case modulator: return std::make_unique<Modulator>();
        case tubeDrive: return std::make_unique<TubeDrive>();
        default: return nullptr;
    }
}

// ------------------------------------------------------------------------------------------------ cabinet
// acc[j] += x·h[j] (a product then a sum, as the plug-in's SSE loop: any vector width gives the same floats)
#if LYNX_X86
bool cpuHasAvx()
{
    static const bool f = [] {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (! __get_cpuid (1, &a, &b, &c, &d))
            return false;
        if ((c & (1u << 27)) == 0 || (c & (1u << 28)) == 0)
            return false;
        unsigned lo = 0, hi = 0;
        asm volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        return (lo & 6u) == 6u;
    }();
    return f;
}
__attribute__ ((target ("avx"))) void macAvx (float* acc, const float* h, float x, std::size_t n) noexcept
{
    const auto vx = _mm256_set1_ps (x);
    std::size_t j = 0;
    for (; j + 8 <= n; j += 8)
        _mm256_storeu_ps (acc + j, _mm256_add_ps (_mm256_mul_ps (vx, _mm256_loadu_ps (h + j)), _mm256_loadu_ps (acc + j)));
    for (; j < n; ++j)
        acc[j] = x * h[j] + acc[j];
}
#endif
void mac (float* acc, const float* h, float x, std::size_t n) noexcept
{
#if LYNX_X86
    if (cpuHasAvx())
    {
        macAvx (acc, h, x, n);
        return;
    }
#endif
    for (std::size_t j = 0; j < n; ++j)
        acc[j] = x * h[j] + acc[j];
}

// impulse resampled to the rate by linear interpolation, ×0.2, run as a direct convolution whose sums add the oldest
// input first (FUN_180026f00 / FUN_180027260)
// the plug-in's RIFF/WAVE reader (FUN_18001c890): integer PCM 16/24/32 bits or float32, planar; its integer scales are
// 2^-15, 1.1920930376163597e-07 (one ulp above 2^-23) and 2^-31. nullopt = not readable (other formats not decoded)
struct WavData
{
    std::vector<float> ch[2];
    int channels = 0;
    float rate = 44100.0f;
};
std::optional<WavData> readWav (const std::vector<std::uint8_t>& b)
{
    auto u32at = [&] (std::size_t o) { std::uint32_t x = 0; std::memcpy (&x, b.data() + o, 4); return x; };
    auto u16at = [&] (std::size_t o) { std::uint16_t x = 0; std::memcpy (&x, b.data() + o, 2); return x; };
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
        return std::nullopt;
    int format = 0, channels = 0, bits = 0, align = 0;
    std::uint32_t rate = 0;
    std::size_t o = 12;
    while (o + 8 <= b.size())
    {
        const auto size = (std::size_t) u32at (o + 4);
        const auto body = o + 8;
        if (body + size > b.size())
            break;
        if (std::memcmp (b.data() + o, "fmt ", 4) == 0 && size >= 16)
        {
            format = u16at (body), channels = u16at (body + 2), rate = u32at (body + 4), align = u16at (body + 12), bits = u16at (body + 14);
            if ((format != 1 && format != 3) || channels < 1 || channels > 2 || align != bits * channels / 8)
                return std::nullopt;
        }
        else if (std::memcmp (b.data() + o, "data", 4) == 0 && align > 0)
        {
            const auto frames = size / (std::size_t) align;
            WavData w;
            w.channels = channels;
            w.rate = (float) rate;
            for (int c = 0; c < channels; ++c)
            {
                auto& v = w.ch[c];
                v.resize (frames);
                for (std::size_t i = 0; i < frames; ++i)
                {
                    const auto* p = b.data() + body + i * (std::size_t) align + (std::size_t) (c * bits / 8);
                    if (format == 3 && bits == 32)
                        std::memcpy (&v[i], p, 4);
                    else if (format == 1 && bits == 16)
                        v[i] = (float) (std::int16_t) (p[0] | (p[1] << 8)) * 3.0517578125e-05f;
                    else if (format == 1 && bits == 24)
                    {
                        auto x = (std::int32_t) (p[0] | (p[1] << 8) | (p[2] << 16));
                        if (x & 0x800000)
                            x |= (std::int32_t) 0xff000000u;
                        v[i] = (float) x * 1.1920930376163597e-07f;
                    }
                    else if (format == 1 && bits == 32)
                    {
                        std::int32_t x = 0;
                        std::memcpy (&x, p, 4);
                        v[i] = (float) x * 4.656612873077393e-10f;
                    }
                    else
                        return std::nullopt;
                }
            }
            return w;
        }
        o = body + size + (size & 1);
    }
    return std::nullopt;
}

struct Cabinet
{
    std::vector<float> hl, hr;           // taps (a multiple of 4, zero padded)
    std::vector<float> accL, accR;       // shifting accumulators, 2·taps each
    int taps = 0, pos = 0;
    std::vector<std::uint8_t> file;      // the loaded file (reloaded on a rate change)
    double fs = 44100.0;

    bool load (const std::vector<std::uint8_t>& wav)
    {
        const auto a = readWav (wav);
        if (! a || a->ch[0].empty())
            return false;
        file = wav;
        const auto fileRate = (double) a->rate;
        const auto scale = (float) (fileRate / fs);
        std::vector<float> l = a->ch[0], r = a->channels > 1 ? a->ch[1] : std::vector<float> {};
        for (auto& v : l)
            v = scale * v;
        for (auto& v : r)
            v = scale * v;
        const auto frames = (unsigned) l.size();
        const auto count = (int) ((double) frames * (fs / fileRate));
        const auto n4 = (count >> 2) + 1;
        taps = n4 * 4;
        hl.assign ((std::size_t) taps, 0.0f);
        hr.assign ((std::size_t) taps, 0.0f);
        auto at = [&] (const std::vector<float>& ch, unsigned idx, float f) {
            if (idx >= frames)
                return 0.0f;
            const auto b = idx + 1 < ch.size() ? ch[idx + 1] : 0.0f;
            return (1.0f - f) * ch[idx] + f * b;
        };
        if (count > 0)
        {
            const auto inv = 1.0 / (double) count;
            for (int i = 0; i < count; ++i)
            {
                const auto p = (double) i * inv * (double) (frames - 1);
                const auto idx = (unsigned) (long long) p;
                const auto f = (float) (p - (double) (int) idx);
                auto v = at (l, idx, f) * 0.2f;
                hl[(std::size_t) i] = v;
                if (! r.empty())
                    v = at (r, idx, f) * 0.2f;
                hr[(std::size_t) i] = v;
            }
        }
        accL.assign ((std::size_t) taps * 2, 0.0f);
        accR.assign ((std::size_t) taps * 2, 0.0f);
        pos = 0;
        return true;
    }
    void unload()
    {
        file.clear(), hl.clear(), hr.clear(), accL.clear(), accR.clear();
        taps = pos = 0;
    }
    void setRate (double rate)
    {
        if (rate != 0.0)
            fs = rate;
        if (! file.empty())
        {
            const auto f = file;
            load (f);
        }
        std::fill (accL.begin(), accL.end(), 0.0f);
        std::fill (accR.begin(), accR.end(), 0.0f);
        pos = 0;
    }
    void process (float* L, float* R, int n) noexcept
    {
        if (taps == 0)
            return;
        const auto T = (std::size_t) taps;
        for (int i = 0; i < n; ++i)
        {
            const auto xl = L[i], xr = R[i];
            float* al = accL.data() + pos;
            float* ar = accR.data() + pos;
            const float* h0 = hl.data();
            const float* h1 = hr.data();
            mac (al, h0, xl, T);
            mac (ar, h1, xr, T);
            L[i] = al[0];
            R[i] = ar[0];
            ++pos;
            if ((std::size_t) pos == T)
            {
                std::memmove (accL.data(), accL.data() + T, T * sizeof (float));
                std::memmove (accR.data(), accR.data() + T, T * sizeof (float));
                std::fill (accL.begin() + (std::ptrdiff_t) T, accL.end(), 0.0f);
                std::fill (accR.begin() + (std::ptrdiff_t) T, accR.end(), 0.0f);
                pos = 0;
            }
            accL[(std::size_t) pos + T - 1] = 0.0f;
            accR[(std::size_t) pos + T - 1] = 0.0f;
        }
    }
};
} // namespace

// ------------------------------------------------------------------------------------------------ engine
struct Engine::Impl
{
    std::array<float, numParams> v {};
    std::unique_ptr<PedalFx> box[numBoxes];
    std::array<std::vector<std::uint8_t>, numCabinets + 1> cabFiles;
    Cabinet cab;
    HpBiquad dcL, dcR;
    EqBand eqL[8], eqR[8];
    bool eqOn = false, eqClear = true;
    float inT = 1.0f, inG = 1.0f, outT = 1.0f, outG = 1.0f;
    double fs = 44100.0;

    Impl()
    {
        for (int b = 0; b < 8; ++b)
            for (auto* e : { &eqL[b], &eqR[b] })
            {
                e->init (1000.0f, 1);
                const auto t = b == 0 ? 0 : (b == 7 ? 2 : 1);
                if (t != e->type)
                    e->dirty = true;
                e->type = t;
                auto f = (float) (std::pow (2.0, (double) b) * 125.0);
                if (f <= 0.0f)
                    f = 0.0f;
                e->setFreq (f);
                e->setBandwidth (1.0f);
            }
        const auto d = defaultValues();
        for (int i = 0; i < numParams; ++i)
            set (i, d[(std::size_t) i]);
        eqClear = true;
    }
    void set (int i, float x)
    {
        if (i < 0 || i >= numParams)
            return;
        v[(std::size_t) i] = x;
        if (i >= pBoxType && i < pBoxType + numBoxes)
        {
            const auto b = i - pBoxType;
            auto t = (int) (float) (int) x;
            t = t < 0 ? -1 : (t > 11 ? 11 : t);
            box[b] = makePedal (t);
            if (box[b])
            {
                box[b]->setRate (fs);
                box[b]->reset();
                for (int k = 0; k < paramsPerBox; ++k)
                    box[b]->setParam (k, v[(std::size_t) (pBoxParam + b * paramsPerBox + k)]);
            }
        }
        else if (i >= pBoxParam && i < pBoxParam + numBoxes * paramsPerBox)
        {
            const auto b = (i - pBoxParam) / paramsPerBox;
            if (box[b])
                box[b]->setParam ((i - pBoxParam) % paramsPerBox, x);
        }
        else if (i == pCabinet)
        {
            const auto c = (int) x;
            if (c > 0 && c <= numCabinets && ! cabFiles[(std::size_t) c].empty())
            {
                if (! cab.load (cabFiles[(std::size_t) c]))
                    cab.unload();
            }
            else
                cab.unload();
        }
        else if (i >= pEqBand && i < pEqBand + 8)
        {
            const auto b = i - pEqBand;
            const auto y = ((x - 0.5f) + x) - 0.5f;
            auto g = y * y;
            if (y < 0.0f)
                g = -g;
            const auto gain = (float) ((double) g * 18.0);
            eqL[b].setGain (gain), eqR[b].setGain (gain);
            eqL[b].design ((float) fs), eqR[b].design ((float) fs);
        }
        else if (i == pEqOn)
        {
            eqOn = x != 0.0f;
            eqClear = true;
        }
        else if (i == pInLevel)
            inT = ildsp::powf_ (10.0f, x * 2.4f - 1.2f);
        else if (i == pOutLevel)
            outT = ildsp::powf_ (10.0f, x * 2.4f - 1.2f);
        else if (i >= pBoxOn && i < pBoxOn + numBoxes)
        {
            if (box[i - pBoxOn])
                box[i - pBoxOn]->dirty = true;
        }
    }
    void prepare (double rate)
    {
        if (rate > 0.0)
            fs = rate;
        for (auto& p : box)
            if (p)
            {
                if (rate != 0.0)
                    p->setRate (rate);
                p->reset();
                p->dirty = false;
            }
        cab.setRate (rate);
        inG = -1.0f, outG = -1.0f;
        dcL.design (30.0f, 1.0f, (float) fs);
        dcR.design (30.0f, 1.0f, (float) fs);
        dcL.clear(), dcR.clear();
        for (int b = 0; b < 8; ++b)
        {
            eqL[b].clear(), eqL[b].design ((float) fs);
            eqR[b].clear(), eqR[b].design ((float) fs);
        }
    }
    void process (float* L, float* R, int n) noexcept
    {
        if (eqClear)
        {
            eqClear = false;
            for (int b = 0; b < 8; ++b)
                eqL[b].clear(), eqR[b].clear();
        }
        if (inG <= 0.0f && inG != 0.0f)
            inG = inT;
        if (outG <= 0.0f && outG != 0.0f)
            outG = outT;
        for (int i = 0; i < n; ++i)
        {
            const auto g = inG;
            const auto l = dcL.tick (L[i]) * g;
            const auto r = dcR.tick (R[i]) * g;
            inG = (inT - g) * 0.00100000005f + g;
            L[i] = l, R[i] = r;
        }
        for (int b = 0; b < numBoxes; ++b)
            if (auto* p = box[b].get())
            {
                if (p->dirty)
                    p->reset();
                p->dirty = false;
                if (v[(std::size_t) (pBoxOn + b)] != 0.0f)
                    p->process (L, R, n);
            }
        cab.process (L, R, n);
        if (eqOn)
            for (int b = 0; b < 8; ++b)
                for (int i = 0; i < n; ++i)
                    L[i] = eqL[b].tick (L[i]), R[i] = eqR[b].tick (R[i]);
        for (int i = 0; i < n; ++i)
        {
            const auto g = outG;
            const auto r = R[i], l = L[i];
            outG = (outT - g) * 0.00100000005f + g;
            L[i] = g * l;
            R[i] = g * r;
        }
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;
void Engine::setCabinetFile (int cabinet, std::vector<std::uint8_t> wav)
{
    if (cabinet >= 1 && cabinet <= numCabinets)
        impl->cabFiles[(std::size_t) cabinet] = std::move (wav);
}
void Engine::set (int param, float raw) { impl->set (param, raw); }
float Engine::get (int param) const noexcept { return param >= 0 && param < numParams ? impl->v[(std::size_t) param] : 0.0f; }
void Engine::prepare (double sampleRate) { impl->prepare (sampleRate); }
void Engine::process (float* left, float* right, int n) noexcept
{
    if (n > 0)
        impl->process (left, right, n);
}

// ------------------------------------------------------------------------------------------------ state, parameters
std::array<float, numParams> defaultValues() noexcept
{
    std::array<float, numParams> d {};
    for (int i = 0; i < numParams; ++i)
        d[(std::size_t) i] = (i >= pBoxOn && i < pBoxOn + numBoxes) ? 1.0f : ((i == pEqOn || i >= pBoxType) ? 0.0f : 0.5f);
    return d;
}

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    std::size_t o = 0;
    if (d.size() >= 8 && std::memcmp (d.data(), "SSLF", 4) == 0)
        o = 8;
    if (d.size() < o + 4 + 4 * numParams)
        return std::nullopt;
    if (std::memcmp (d.data() + o, "1RCH", 4) != 0 && std::memcmp (d.data() + o, "DRCH", 4) != 0)
        return std::nullopt;
    State s;
    std::memcpy (s.values.data(), d.data() + o + 4, 4 * numParams);
    for (auto& x : s.values)
        if (! std::isfinite (x))
            x = 0.0f;
    const auto nameAt = o + 4 + 4 * numParams;
    for (std::size_t i = nameAt; i < d.size() && i < nameAt + 256 && d[i] != 0; ++i)
        s.presetName += (char) d[i];
    return s;
}

float toSlot (int param, float raw) noexcept
{
    // the cabinet and the box types are read by truncation, as FL's plugin does ((int)v: a stored 0.5 is an empty box,
    // FL's render test "Hardcore" holds one; REA_rodada170i §5)
    if (param == pCabinet)
        return std::clamp ((float) (int) raw / (float) numCabinets, 0.0f, 1.0f);
    if (param >= pBoxType && param < pBoxType + numBoxes)
        return std::clamp ((float) (int) raw / (float) numPedalTypes, 0.0f, 1.0f);
    return std::clamp (raw, 0.0f, 1.0f);
}

float fromSlot (int param, float v01) noexcept
{
    if (param == pCabinet)
        return (float) (int) std::lround (std::clamp (v01, 0.0f, 1.0f) * (float) numCabinets);
    if (param >= pBoxType && param < pBoxType + numBoxes)
        return (float) (int) std::lround (std::clamp (v01, 0.0f, 1.0f) * (float) numPedalTypes);
    return v01;
}

void toParams (const State& s, EffectParams& params) noexcept
{
    for (int i = 0; i < numParams; ++i)
        params[(std::size_t) i] = toSlot (i, s.values[(std::size_t) i]);
}

const char* pedalName (int type) noexcept
{
    static const char* names[] = { "Empty", "Distortion", "Chorus", "Flanger", "Phaser", "Reverb", "Delay",
                                   "Noise Gate", "Equalizer", "Compressor", "Modulator", "Tubedrive" };
    return type >= 0 && type <= 11 ? names[type] : "Empty";
}

std::string paramName (int i)
{
    char b[48];
    if (i == pInLevel)
        return "Input level";
    if (i == pOutLevel)
        return "Output level";
    if (i == pEqOn)
        return "EQ on";
    if (i >= pEqBand && i < pEqBand + 8)
    {
        std::snprintf (b, sizeof (b), "EQ band %d", i - pEqBand + 1);
        return b;
    }
    if (i == pCabinet)
        return "Cabinet";
    if (i >= pBoxParam && i < pBoxParam + numBoxes * paramsPerBox)
    {
        std::snprintf (b, sizeof (b), "Box %d knob %d", (i - pBoxParam) / paramsPerBox + 1, (i - pBoxParam) % paramsPerBox + 1);
        return b;
    }
    if (i >= pBoxOn && i < pBoxOn + numBoxes)
    {
        std::snprintf (b, sizeof (b), "Box %d on", i - pBoxOn + 1);
        return b;
    }
    if (i >= pBoxType && i < pBoxType + numBoxes)
    {
        std::snprintf (b, sizeof (b), "Box %d pedal", i - pBoxType + 1);
        return b;
    }
    std::snprintf (b, sizeof (b), "(unused %d)", i); // FL's parameters 12, 38 and 44 do nothing
    return b;
}

std::string paramText (int i, float v01)
{
    char b[48];
    const auto raw = fromSlot (i, v01);
    if (i == pInLevel || i == pOutLevel)
        std::snprintf (b, sizeof (b), "%+.1f dB", 48.0 * (double) raw - 24.0);
    else if (i >= pEqBand && i < pEqBand + 8)
    {
        const auto y = ((double) raw - 0.5) * 2.0;
        std::snprintf (b, sizeof (b), "%+.1f dB", (y < 0 ? -y * y : y * y) * 18.0);
    }
    else if (i == pEqOn || (i >= pBoxOn && i < pBoxOn + numBoxes))
        return raw != 0.0f ? "On" : "Off";
    else if (i == pCabinet)
    {
        const auto c = (int) raw;
        if (c <= 0)
            return "None";
        std::snprintf (b, sizeof (b), "Model %c", (char) ('A' + c - 1));
    }
    else if (i >= pBoxType && i < pBoxType + numBoxes)
        return pedalName ((int) raw);
    else
        std::snprintf (b, sizeof (b), "%.0f%%", (double) raw * 100.0);
    return b;
}

std::string cabinetPath (int cabinet)
{
    if (cabinet < 1 || cabinet > numCabinets)
        return {};
    // the installation's file names (two of them carry a space before the extension)
    std::string name = "Model ";
    name += (char) ('A' + cabinet - 1);
    if (cabinet == 9 || cabinet == 10)
        name += ' ';
    return "Plugins\\Fruity\\Effects\\Hardcore\\Imp\\" + name + ".wav";
}

// ------------------------------------------------------------------------------------------------ LYNX effect
namespace
{
// EffectSlot::state of a Hardcore slot: "LXHC", u32 count, then per cabinet file: u32 index, u32 size, bytes
std::vector<std::pair<int, std::vector<std::uint8_t>>> unpackFiles (const std::vector<std::uint8_t>& s)
{
    std::vector<std::pair<int, std::vector<std::uint8_t>>> out;
    auto u = [&] (std::size_t at) {
        std::uint32_t x = 0;
        std::memcpy (&x, s.data() + at, 4);
        return x;
    };
    if (s.size() < 8 || std::memcmp (s.data(), "LXHC", 4) != 0)
        return out;
    const auto count = u (4);
    std::size_t o = 8;
    for (std::uint32_t k = 0; k < count && o + 8 <= s.size(); ++k)
    {
        const int idx = (int) u (o);
        const std::uint32_t size = u (o + 4);
        o += 8;
        if (o + size > s.size())
            break;
        out.emplace_back (idx, std::vector<std::uint8_t> (s.begin() + (std::ptrdiff_t) o, s.begin() + (std::ptrdiff_t) (o + size)));
        o += size;
    }
    return out;
}

class HardcoreEffect final : public Effect
{
public:
    explicit HardcoreEffect (const std::vector<std::uint8_t>& state)
    {
        for (auto& [idx, bytes] : unpackFiles (state))
            engine.setCabinetFile (idx, std::move (bytes));
    }
    void prepare (double sampleRate) noexcept override
    {
        rate = sampleRate;
        primed = false;
    }
    void reset() noexcept override { primed = false; }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        if (! primed)
        {
            for (int i = 0; i < numParams; ++i)
            {
                last[(std::size_t) i] = params[(std::size_t) i];
                engine.set (i, fromSlot (i, params[(std::size_t) i]));
            }
            engine.prepare (rate);
            primed = true;
        }
        else
            for (int i = 0; i < numParams; ++i)
                if (params[(std::size_t) i] != last[(std::size_t) i])
                {
                    last[(std::size_t) i] = params[(std::size_t) i];
                    const auto raw = fromSlot (i, params[(std::size_t) i]);
                    if (raw != engine.get (i))
                        engine.set (i, raw);
                }
        engine.process (left, right, n);
    }

private:
    Engine engine;
    std::array<float, numParams> last {};
    double rate = 44100.0;
    bool primed = false;
};
} // namespace

std::unique_ptr<Effect> make (const EffectSlot& slot) { return std::make_unique<HardcoreEffect> (slot.state); }

const EffectInfo& info()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (int i = 0; i < numParams; ++i)
            n.push_back (paramName (i));
        return n;
    }();
    static const EffectInfo i = [] {
        EffectInfo e { "Hardcore", {} };
        const auto d = defaultValues();
        for (int k = 0; k < numParams; ++k)
            e.params.push_back ({ names[(std::size_t) k].c_str(), toSlot (k, d[(std::size_t) k]) });
        return e;
    }();
    return i;
}

std::vector<std::uint8_t> packCabinets (const std::vector<std::pair<int, std::vector<std::uint8_t>>>& files)
{
    std::vector<std::uint8_t> out { 'L', 'X', 'H', 'C' };
    auto put = [&] (std::uint32_t x) {
        for (int b = 0; b < 4; ++b)
            out.push_back ((std::uint8_t) (x >> (8 * b)));
    };
    put ((std::uint32_t) files.size());
    for (const auto& [idx, bytes] : files)
    {
        put ((std::uint32_t) idx);
        put ((std::uint32_t) bytes.size());
        out.insert (out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

bool hasCabinet (const EffectSlot& slot, int cabinet)
{
    for (const auto& f : unpackFiles (slot.state))
        if (f.first == cabinet && ! f.second.empty())
            return true;
    return false;
}

std::optional<EffectSlot> fromFl (const std::vector<std::uint8_t>& state, const FileReader& read)
{
    const auto s = parse (state);
    if (! s)
        return std::nullopt;
    EffectSlot slot;
    slot.kind = EffectKind::hardcore;
    toParams (*s, slot.params);
    // every impulse of the installation travels with the slot (about 290 kB) so the cabinet stays switchable
    std::vector<std::pair<int, std::vector<std::uint8_t>>> files;
    if (read)
        for (int c = 1; c <= numCabinets; ++c)
            if (auto b = read (cabinetPath (c)); ! b.empty() && readWav (b))
                files.emplace_back (c, std::move (b));
    if (! files.empty())
        slot.state = packCabinets (files);
    return slot;
}
} // namespace lynx::hardcore
