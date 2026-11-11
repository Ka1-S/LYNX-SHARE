#pragma once

// Image-Line's plugin voice processor as FL's own Delphi generators carry it (REA_rodada42 §3, REA_rodada43 §1.4–1.6):
// the five channel articulators of the 364-byte settings snapshot (pan, volume, cutoff, resonance, pitch: envelopes with
// tensions and LFOs, stepped once per sequencer tick), the channel filter (fast LP, 12 dB LP/BP/HP/BS, 24 dB LP, SVF LP
// ×1/×2, Mod X/Y), FL's x87 pan law and the gain ramps. Shared by Fruity DX10 (core/Dx10.cpp), Fruit Kick
// (core/FruitKick.cpp) and 3x Osc (core/ThreeOsc.cpp, REA_rodada47 §2.3/§3: the release path, the library's rate ratio
// and ramp limit, the end fade and the mix — mixVoice); a 1:1 port of the round-42/47 reference models
// (r42/py/dxproc.py, r47/py/osc3model.py), checked against the three DLLs.
// The float/double operation order is the plugins': includers keep FP contraction off.

#include "core/IppCos.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::flvp
{
constexpr int snapshotSize = 364;

inline std::int32_t bitsOf (float x) noexcept
{
    std::int32_t b;
    std::memcpy (&b, &x, 4);
    return b;
}

// cvtsd2si: round half to even (default rounding mode), 64-bit
inline std::int64_t roundEven (double x) noexcept { return (std::int64_t) std::nearbyint (x); }

// The host's WaveTables[0]: 16384 floats sin(2i·π/16384) (FLEngine FUN_009cdfc0, REA_rodada42 §1.6)
inline const std::array<float, 16384>& sineTable() noexcept
{
    static const auto table = [] {
        std::array<float, 16384> t {};
        for (int i = 0; i < 16384; ++i)
            t[(std::size_t) i] = (float) std::sin ((double) (2 * i) * 3.141592653589793 / 16384.0);
        return t;
    }();
    return table;
}

// FUN_005fad20 (x87 path): t = (1 + pan)·π/4 in extended precision; the FPU's fcos/fsin reduce arguments above π/4 with
// its 66-bit π, i.e. cos t = sin(P/2 − t), sin t = cos(P/2 − t) with P = round(π·2^64)/2^64 (EXEC: pan = 1 gives a left
// gain of 6.1230318e-17·vol). P/2 = hi + lo, hi = 0xC90FDAA22168C234·2^-63, lo = 3·2^-65; hi − t is exact (Sterbenz).
inline void panLaw (float pan, float vol, float& gl, float& gr) noexcept
{
    using ld = long double;
    pan = std::min (std::max (pan, -1.0f), 1.0f);
    const ld quarter = (ld) 0.7853981633974483;
    const ld t = ((ld) 1 + (ld) pan) * quarter;
    ld c, s;
    if (t > quarter)
    {
        const ld hi = std::ldexp ((ld) 0xC90FDAA22168C234ull, -63);
        const ld lo = std::ldexp ((ld) 3, -65);
        const ld r = (hi - t) + lo;
        c = std::sin (r);
        s = std::cos (r);
    }
    else
    {
        c = std::cos (t);
        s = std::sin (t);
    }
    gl = (float) (c * (ld) vol);
    gr = (float) (s * (ld) vol);
}

// FUN_005fac70: per-sample step towards the target, limited to `limit` (0.001, or the rate-scaled limit of
// Settings::rampLimit); a limited ramp that would end below 2^-24 ends at 0
inline float rampStep (float& target, float prev, int n, float limit = 0.001f) noexcept
{
    auto step = target - prev;
    if (step != 0.0f)
    {
        step = step / (float) n;
        if (std::fabs (step) > limit)
        {
            step = std::copysign (limit, step);
            target = prev + step * (float) n;
            if ((bitsOf (target) & 0x7fffffff) < 864026624)
            {
                target = 0.0f;
                step = -prev / (float) n;
            }
        }
    }
    return step;
}


// ------------------------------------------------------------------------------------------------------- x87

// The processor's filters run on the x87 FPU with 64-bit precision (control word 0x33F, FUN_005a2840): long double
// arithmetic reproduces them where long double is the x87 format; the guard makes sure the precision field is 64-bit.
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
struct X87Extended
{
    unsigned short saved = 0;
    X87Extended() noexcept
    {
        asm volatile ("fnstcw %0" : "=m"(saved));
        unsigned short cw = (unsigned short) (saved | 0x300);
        asm volatile ("fldcw %0" : : "m"(cw));
    }
    ~X87Extended() { asm volatile ("fldcw %0" : : "m"(saved)); }
};
#else
struct X87Extended
{
};
#endif

// ------------------------------------------------------------------------------------------------------- processor

// LFO tables of the plugin's voice processor (FUN_0071f4e0): sine (= the host's), triangle, square
struct LfoTables
{
    std::array<float, 16384> tri {}, sqr {};
    LfoTables()
    {
        for (int i = 0; i < 8192; ++i)
            tri[(std::size_t) ((i + 0x3000) & 0x3fff)] = (float) ((double) (i << 2) / 16384.0 + -1.0);
        for (int i = 8192; i < 16384; ++i)
            tri[(std::size_t) ((i + 0x3000) & 0x3fff)] = (float) (1.0 - (double) ((i - 0x2000) * 4) / 16384.0);
        for (int i = 0; i < 16384; ++i)
            sqr[(std::size_t) i] = i < 8192 ? 1.0f : -1.0f;
    }
};

inline const float* lfoTable (int shape) noexcept
{
    static const LfoTables t;
    shape = std::min (std::max (shape, 0), 2);
    return shape == 0 ? sineTable().data() : shape == 1 ? t.tri.data() : t.sqr.data();
}

// FUN_0071fe70: knob value → ticks
inline int toTicks (int v, double E) noexcept
{
    if (v < 0x65)
        return 0;
    const auto x = (float) ((double) v * 1.52587890625e-05);
    const auto d = std::exp ((double) x * std::log (1001.0));
    return (int) roundEven ((double) (float) (d - 1.0) * E);
}

// FUN_0071fdb0 + FUN_005f5ac0: a tension knob → (value, 1/|value|, ln(|value| + 1))
struct Tension
{
    float v = 0.0f, inv = 0.0f, L = 0.0f;
};

inline Tension makeTension (int t) noexcept
{
    const auto sign = t == 0 ? 0 : (t < 0 ? -1 : 1);
    const auto a = std::fabs ((double) ((float) t * 0.0078125f));
    const auto d = std::exp ((double) (float) a * std::log (6347.0));
    Tension r;
    r.v = ((float) sign * (float) (d - 1.0)) * 0.1f;
    if (r.v != 0.0f)
    {
        r.inv = (float) (1.0 / std::fabs ((double) r.v));
        r.L = (float) std::log (std::fabs ((double) r.v) + 1.0);
    }
    return r;
}

inline float shape (float x, const Tension& t) noexcept
{
    auto d = (double) x;
    if (bitsOf (t.v) != 0)
    {
        if (bitsOf (t.v) >= 0)
            d = 1.0 - (std::exp ((1.0 - d) * (double) t.L) - 1.0) * (double) t.inv;
        else
            d = (std::exp (d * (double) t.L) - 1.0) * (double) t.inv;
    }
    return (float) d;
}

inline float unshape (float y, const Tension& t) noexcept
{
    auto d = (double) y;
    if (t.v != 0.0f)
    {
        if (bitsOf (t.v) >= 0)
            d = 1.0 - std::log ((1.0 - d) * std::fabs ((double) t.v) + 1.0) / (double) t.L;
        else
            d = std::log (d * std::fabs ((double) t.v) + 1.0) / (double) t.L;
    }
    return (float) d;
}

// One articulator's settings: a 0xDA record (17 ints) converted by FUN_0071ff00
struct Rec
{
    std::int32_t flags = 0, enabled = 0, attackRaw = 0, amountRaw = 0, lfoAmountRaw = 0;
    int delay = 0, hold = 0, lfoDelay = 0;
    float att = 0, sus = 0, dec = 0, rel = 0, amt = 0, lfoFade = 0, lfoAmt = 0;
    std::uint32_t lfoInc = 0, globalPhase = 0;
    const float* table = nullptr;
    Tension tens[3];
    // FL Studio Mobile's build of the envelope (its 3x Osc voice, FUN_1807d9640; Settings::setMobile, build() leaves
    // it alone): the predelay and the hold last one tick more (the counter has to pass the tick count) and the
    // attack's position is not clamped to 1, so an attack whose float steps do not land on 1 overshoots by up to a
    // step, holds above 1 and starts the decay from there (A/B inside FL, round 190ab)
    bool mobile = false;

    void build (const std::int32_t* r, int ppq, double tempo) noexcept
    {
        flags = r[0], enabled = r[1], attackRaw = r[3], amountRaw = r[8], lfoAmountRaw = r[11];
        const auto d6 = (((double) (ppq >> 2) / 48.0) * 24576.0) / 1000.0;
        const auto E = (flags & 1) != 0 ? d6 : (d6 * tempo) / 120.0;
        delay = toTicks (r[2], E);
        att = (float) (1.0 / (double) (toTicks (r[3], E) + 1));
        hold = toTicks (r[4], E);
        dec = (float) (-1.0 / (double) (toTicks (r[5], E) + 1));
        sus = (float) r[6] * 0.0078125f;
        rel = (float) (-1.0 / (double) (toTicks (r[7], E) + 1));
        amt = (float) r[8] * 0.0078125f;
        for (int k = 0; k < 3; ++k)
            tens[k] = makeTension (r[14 + k]);
        const auto E2 = (flags & 2) != 0 ? d6 : (d6 * tempo) / 120.0;
        lfoDelay = toTicks (r[9], E2);
        lfoFade = (float) (1.0 / (double) (toTicks (r[10], E2) + 1));
        lfoInc = (std::uint32_t) (0x100000000ull / (std::uint64_t) std::max (toTicks (r[12], E2), 3));
        lfoAmt = (float) r[11] * 0.0078125f;
        table = lfoTable (r[13]);
    }
};

// The 364-byte snapshot: records pan, volume, cutoff, resonance, pitch; Mod X (0x15c), Mod Y (0x160), type (0x168)
struct Settings
{
    std::array<Rec, 5> recs {};
    std::int32_t modX = 256, modY = 0, type = 0;
    bool hasGlobal = false;
    // The library's rate ratio float(44100/fs) (0x77e6f4) and ramp limit float(ratio·0.001) (0x77e6f8), set when the
    // plugin hands the library its rate (FUN_005f8470: 3x Osc does, DX10 and Fruit Kick do not — 1 and 0.001 then); the
    // filter coefficients scale with the ratio (REA_rodada47 §2.3). build() leaves them alone.
    float rateRatio = 1.0f, rampLimit = 0.001f;

    void setMobile (bool on) noexcept
    {
        for (auto& r : recs)
            r.mobile = on;
    }

    void setRate (double fs) noexcept
    {
        rateRatio = (float) (44100.0 / fs);
        rampLimit = (float) ((double) rateRatio * 0.001);
    }

    // ProcessEvent 0 / SetTimeSig: new tick counts, the global LFO phases kept (FUN_00722d80)
    void retime (const std::uint8_t* s, int ppq, double tempo) noexcept
    {
        const auto keep = hasGlobal;
        std::array<std::uint32_t, 5> phases {};
        for (int k = 0; k < 5; ++k)
            phases[(std::size_t) k] = recs[(std::size_t) k].globalPhase;
        build (s, ppq, tempo);
        if (keep)
            for (int k = 0; k < 5; ++k)
                recs[(std::size_t) k].globalPhase = phases[(std::size_t) k];
    }

    // NewTick: the global LFOs advance (once the processor has listed them)
    void advanceGlobal() noexcept
    {
        for (auto& r : recs)
            if ((r.flags & 0x20) != 0)
                r.globalPhase += r.lfoInc;
    }

    void build (const std::uint8_t* s, int ppq, double tempo) noexcept
    {
        hasGlobal = false;
        for (int k = 0; k < 5; ++k)
        {
            std::int32_t r[17];
            std::memcpy (r, s + 0x44 * k, sizeof (r));
            recs[(std::size_t) k].build (r, ppq, tempo);
            hasGlobal = hasGlobal || (recs[(std::size_t) k].flags & 0x20) != 0;
        }
        std::memcpy (&modX, s + 0x15c, 4);
        std::memcpy (&modY, s + 0x160, 4);
        std::memcpy (&type, s + 0x168, 4);
    }
};

// the plugin's defaults (FUN_00722bc0): neutral records, the volume one with flag 4 and release tension −101
inline std::array<std::uint8_t, snapshotSize> defaultSnapshot() noexcept
{
    std::array<std::uint8_t, snapshotSize> s {};
    for (int k = 0; k < 5; ++k)
    {
        const std::int32_t r[17] = { k == 1 ? 4 : 0, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, k == 1 ? -101 : 0 };
        std::memcpy (s.data() + 0x44 * k, r, sizeof (r));
    }
    const std::int32_t modX = 256;
    std::memcpy (s.data() + 0x15c, &modX, 4);
    return s;
}

// Per-voice articulator state (FUN_007202c0 / FUN_0071f9b0)
struct Art
{
    int cnt = 0, state = 1, lfoDelay = 0;
    std::uint32_t phase = 0;
    float pos = 0, val = 0, fade = 0, lfo = 0;

    void init (const Rec& s) noexcept
    {
        cnt = 0;
        if (s.delay < 1 || s.enabled == 0)
        {
            state = 1;
            if (((s.flags & 4) == 0 || s.enabled != 0) && (s.enabled == 0 || s.attackRaw != 100))
                pos = 0.0f;
            else
            {
                pos = 1.0f;
                state = s.hold < 1 ? 3 : 2;
            }
        }
        else
            state = 0, pos = 0.0f;
        val = pos;
        lfoDelay = s.lfoDelay;
        phase = 0, fade = 0.0f, lfo = 0.0f;
    }

    void step (const Rec& s) noexcept
    {
        if (s.enabled != 0)
        {
            switch (state)
            {
                case 2:
                    if (s.mobile ? s.hold < ++cnt : s.hold <= ++cnt)
                        state = 3;
                    break;
                case 0:
                    if (s.mobile ? s.delay < ++cnt : s.delay <= ++cnt)
                        state = 1;
                    break;
                case 1:
                    pos = pos + s.att;
                    if (1.0f <= pos)
                    {
                        if (! s.mobile)
                            pos = 1.0f;
                        if (s.hold < 1)
                            state = 3;
                        else
                            cnt = 0, state = 2;
                    }
                    val = shape (pos, s.tens[0]);
                    break;
                case 3:
                    pos = pos + s.dec;
                    val = shape (pos, s.tens[1]);
                    if (val <= s.sus)
                    {
                        val = s.sus;
                        pos = unshape (val, s.tens[1]);
                        cnt = 0, state = 4;
                    }
                    break;
                case 6:
                    pos = pos + s.rel;
                    val = shape (pos, s.tens[2]);
                    break;
                default:
                    break;
            }
            if (state > 2 && val < 5.96046448e-08f)
                val = 0.0f, state = 9;
        }
        if (lfoDelay == 0)
        {
            phase = (s.flags & 0x20) != 0 ? s.globalPhase : phase + s.lfoInc;
            lfo = s.table[phase >> 18];
            if (fade != 1.0f)
            {
                fade = fade + s.lfoFade;
                if (1.0f < fade)
                    fade = 1.0f;
                lfo = lfo * fade;
            }
        }
        else
            --lfoDelay;
    }

    // FUN_00720070 (Fruit Kick; DX10 never releases its articulators): the release stage starts from the current level
    void release (const Rec& s) noexcept
    {
        if (state < 7)
        {
            pos = unshape (val, s.tens[2]);
            cnt = 0, state = 6;
        }
    }
};

// filter kinds by type index (DAT_0078cc6c): kind, second stage
constexpr int filterKind[8][2] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 1, 1 }, { 6, 0 }, { 6, 1 } };

inline const float ln601f = (float) std::log (601.0), inv600f = 1.0f / 600.0f; // 0x79e4b8, 0x77a83c
inline const float ln51f = (float) std::log (51.0), ln51r = (float) (std::log (51.0) * 80.0 / 128.0); // 0x800624, 0x800628

// FUN_00720cb0: the fast LP's coefficient and feedback
inline void coefFast (float x, float y, float& c, float& q, float r = 1.0f) noexcept
{
    if (! (0.0f < x))
        c = 0.0f;
    else if (! (x < 1.0f))
        c = 1.0f;
    else
    {
        c = (float) ((std::exp ((double) (x * ln601f)) - 1.0) * (double) inv600f) * r;
        if (! (1.0f > c))
            c = 1.0f;
    }
    if (! (0.0f < y))
        q = 0.0f;
    else
    {
        if (y > 1.0f)
            y = 1.0f;
        const auto e = std::exp ((double) ((1.0f - y * 0.625f) * ln601f));
        q = 1.0f - (float) ((e - 1.0) * (double) inv600f);
    }
}

// FUN_00720b10 + FUN_007204b0: 12 dB biquads (kinds 1 LP, 2 BP, 3 HP, 4 band stop) → coefficients b0 b1 b2 a1 a2
inline void coefBiquad (int kind, float x, float y, float& fc, float& fq, float* b, float r = 1.0f) noexcept
{
    fc = (float) (((std::exp ((double) (x * ln51f)) - 1.0) * (double) 0.0596f + 0.02) * (double) r);
    const auto lo = (float) ((double) r * 0.02);
    if (lo > fc)
        fc = lo;
    else if (fc > 3.0f)
        fc = 3.0f;
    if (0.0f < y)
    {
        if (y > 1.0f)
            y = 1.0f;
        fq = (float) ((std::exp ((double) (y * ln51r)) - 1.0) * (double) 0.4f + 0.1);
    }
    else
        fq = 0.1f;
    const auto s = (float) std::sin ((double) fc), co = (float) std::cos ((double) fc);
    const auto alpha = (double) (s / (fq * 2.0f));
    const auto cs = (double) co;
    const auto d = 1.0 / (alpha + 1.0);
    const auto a1 = (float) (d * cs * -2.0), a2 = (float) (d * (1.0 - alpha));
    float b0, b1, b2;
    if (kind == 1)
        b1 = (float) (d * (1.0 - cs)), b0 = b1 * 0.5f, b2 = b0;
    else if (kind == 2)
        b1 = 0.0f, b0 = (float) (d * alpha), b2 = (float) (d * -alpha);
    else if (kind == 3)
        b1 = (float) (d * -(cs + 1.0)), b0 = -b1 * 0.5f, b2 = b0;
    else
        b1 = a1, b0 = (float) d, b2 = (float) d;
    b[0] = b0, b[1] = b1, b[2] = b2, b[3] = a1, b[4] = a2;
}

// FUN_00720de0: state-variable filter frequency and damping
inline void coefSvf (float x, float y, float& c, float& q, float r = 1.0f) noexcept
{
    if (! (0.0f < x))
        c = 0.0f;
    else
    {
        c = ((float) (std::exp ((double) x * std::log (26.0)) - 1.0) * 0.04f) * r;
        if (! (1.0f > c))
            c = 1.0f;
    }
    if (! (0.0f < y))
        q = 1.0f;
    else if (! (y < 1.0f))
        q = 0.2f;
    else
        q = 1.0f - y * 0.8f;
}

// FUN_005f91a0: values below 2^-24 in magnitude become 0
inline void flushSmall (float* v, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        if (((std::uint32_t) bitsOf (v[i]) << 1) < (864026624u << 1))
            v[i] = 0.0f;
}

using ld = long double;

// FUN_005f9290 (x87): one-pole with feedback, coefficient ramped per sample; state yL yR vL vR (doubles)
inline void renderFast (double* st, float damp, const float* x, float* y, int n, float c0, float dc) noexcept
{
    X87Extended guard;
    ld yL = st[0], yR = st[1], vL = st[2], vR = st[3], c = c0;
    const ld dcl = dc, dm = damp;
    for (int i = 0; i < n; ++i)
    {
        ld t = ((ld) x[2 * i] - yL) * c;
        vL = vL + t;
        yL = yL + vL;
        vL = vL * dm;
        y[2 * i] = (float) yL;
        t = ((ld) x[2 * i + 1] - yR) * c;
        vR = vR + t;
        yR = yR + vR;
        vR = vR * dm;
        y[2 * i + 1] = (float) yR;
        c = c + dcl;
    }
    auto flush = [] (ld v) { return std::fabs (v) <= (ld) 5.96046448e-08f ? 0.0 : (double) v; };
    st[2] = flush (vL), st[0] = flush (yL), st[3] = flush (vR), st[1] = flush (yR);
}

// FUN_00720ee0 (x87): direct-form-I biquad; state x1L x1R x2L x2R y1L y1R y2L y2R (floats)
inline void renderBiquad (float* st, const float* b, const float* x, float* y, int n) noexcept
{
    X87Extended guard;
    const ld b0 = b[0], b1 = b[1], b2 = b[2], a1 = b[3], a2 = b[4];
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const ld x2 = st[2 + ch], x1 = st[ch];
            st[2 + ch] = st[ch];
            ld acc = x1 * b1 + x2 * b2;
            const ld y2 = st[6 + ch], y1 = st[4 + ch];
            st[6 + ch] = st[4 + ch];
            acc = acc - y1 * a1;
            acc = acc - y2 * a2;
            const auto in = x[2 * i + ch];
            st[ch] = in;
            const auto out = (float) (acc + (ld) in * b0);
            st[4 + ch] = out;
            y[2 * i + ch] = out;
        }
    flushSmall (st, 8);
}

// FUN_00721000 (x87): state-variable low-pass; state lowL lowR bandL bandR (floats, extended within the block)
inline void renderSvf (float* st, float fc, float fq, const float* x, float* y, int n) noexcept
{
    X87Extended guard;
    const ld f = fc, q = fq;
    ld lowL = st[0], lowR = st[1], bL = st[2], bR = st[3];
    for (int i = 0; i < n; ++i)
    {
        lowL = lowL + bL * f;
        bL = bL + (-(bL * q + lowL) + (ld) x[2 * i]) * f;
        y[2 * i] = (float) lowL;
        lowR = lowR + bR * f;
        bR = bR + (-(bR * q + lowR) + (ld) x[2 * i + 1]) * f;
        y[2 * i + 1] = (float) lowR;
    }
    st[0] = (float) lowL, st[1] = (float) lowR, st[2] = (float) bL, st[3] = (float) bR;
    flushSmall (st, 8); // (the plugin flushes 8 floats: the second stage's state too)
}

// Per-voice processor (FUN_00722070 …)
struct Proc
{
    std::array<Art, 5> arts {};
    int first = 2; // +0x170: 2 = the volume articulator starts above "attack" (gains start at their target)
    bool gainsSet = false, released = false;
    float gL = 0, gR = 0;
    float pan = 0, vol = 1, cut = 0, res = 0, pitch = 0;
    // the note's Init.FCut / Init.FRes (REA_rodada76 §1: the note's Mod X / Mod Y byte b → (b − 128) / 128), added to
    // the channel's filter X / Y (issue #63, round 178a; 0 = a note at Mod X / Y centre)
    float noteCut = 0, noteRes = 0;
    int kind = 0, stage2 = 0;
    bool active = false;
    float fc = 0, fq = 0;
    float b[5] {};
    double fst[4] {};
    float st1[16] {}, st2[16] {};
    int endPos = -1;

    void init (const Settings& S) noexcept
    {
        for (int k = 0; k < 5; ++k)
            arts[(std::size_t) k].init (S.recs[(std::size_t) k]);
        first = arts[1].state > 1 ? 2 : 1;
        gainsSet = false, released = false;
        gL = gR = 0.0f;
        pan = cut = res = pitch = 0.0f, vol = 1.0f;
        noteCut = noteRes = 0.0f;
        const auto t = S.type >= 0 && S.type < 8 ? S.type : 0;
        kind = filterKind[t][0], stage2 = filterKind[t][1];
        active = kind > 0;
        fc = fq = 0.0f;
        std::fill (std::begin (b), std::end (b), 0.0f);
        std::fill (std::begin (fst), std::end (fst), 0.0);
        std::fill (std::begin (st1), std::end (st1), 0.0f);
        std::fill (std::begin (st2), std::end (st2), 0.0f);
        endPos = -1;
    }

    // FUN_00722830
    void control (const Settings& S) noexcept
    {
        for (int k = 0; k < 5; ++k)
            arts[(std::size_t) k].step (S.recs[(std::size_t) k]);
        const auto& A = arts;
        const auto& R = S.recs;
        pan = (A[0].lfo * R[0].lfoAmt) * 2.0f;
        vol = A[1].val * ((A[1].lfo * R[1].lfoAmt) + 1.0f);
        if (vol < 0.0f)
            vol = 0.0f;
        cut = A[2].val * R[2].amt + A[2].lfo * R[2].lfoAmt;
        res = A[3].val * R[3].amt + A[3].lfo * R[3].lfoAmt;
        pitch = A[4].val * (float) R[4].amountRaw + A[4].lfo * (float) R[4].lfoAmountRaw;
        if (A[1].state == 9 && endPos < 0)
            endPos = 0;
    }

    // FUN_0072c0d0: released, end fade started (3x Osc's voice stealing)
    void kill() noexcept
    {
        released = true;
        if (endPos < 0)
            endPos = 0;
    }

    // FUN_0072c050 (3x Osc's Voice_Release): the volume, cutoff, resonance and pitch articulators go to their release
    // stage; with the volume envelope off the end fade starts at once (REA_rodada47 §3.1)
    void release (const Settings& S) noexcept
    {
        released = true;
        for (int k = 1; k < 5; ++k)
            arts[(std::size_t) k].release (S.recs[(std::size_t) k]);
        if (S.recs[1].enabled == 0)
            kill();
    }
};

// FUN_00722450: the channel filter on x (interleaved stereo), result in y; may scale the target gains
inline bool filter (const Settings& S, Proc& pr, const float* x, float* y, int n, float& tL, float& tR) noexcept
{
    const auto old = pr.fc;
    const auto cx = ((float) S.modX * 0.00390625f + pr.noteCut) + pr.cut; // + Final.FCut (the note's Init.FCut)
    const auto cy = ((float) S.modY * 0.00390625f + pr.noteRes) + pr.res;
    if (pr.kind == 0)
    {
        coefFast (cx, cy, pr.fc, pr.fq, S.rateRatio);
        if (bitsOf (pr.fc) != 0x3f800000 || bitsOf (pr.fq) != 0)
            pr.active = true;
    }
    else if (pr.kind >= 1 && pr.kind <= 4)
        coefBiquad (pr.kind, cx, cy, pr.fc, pr.fq, pr.b, S.rateRatio);
    else if (pr.kind == 6)
        coefSvf (cx, cy, pr.fc, pr.fq, S.rateRatio);
    if (! pr.active)
        return false;
    if (pr.kind == 0)
    {
        const auto start = pr.first > 0 ? pr.fc : old;
        const auto step = rampStep (pr.fc, start, n, S.rampLimit);
        renderFast (pr.fst, pr.fq, x, y, n, start, step);
    }
    else if (pr.kind >= 1 && pr.kind <= 4)
    {
        renderBiquad (pr.st1, pr.b, x, y, n);
        if (pr.stage2 != 0)
        {
            renderBiquad (pr.st2, pr.b, y, y, n);
            const auto g = (float) (1.0 - (double) pr.fq * (3.2 - (double) pr.fc) * 0.046875);
            tL = tL * g, tR = tR * g;
        }
    }
    else if (pr.kind == 6)
    {
        renderSvf (pr.st1, pr.fc, pr.fq, x, y, n);
        if (pr.stage2 != 0)
            renderSvf (pr.st2, pr.fc, pr.fq + (1.0f - pr.fq) * 0.25f, y, y, n);
    }
    else
        return false;
    return true;
}

// ------------------------------------------------------------------------------------------------------- 3x Osc's mix

// FUN_005f9760 (SSE path): lanes [L0, R0, L1, R1] start at g and g + s, +2s per pair of frames, single frames +s
inline void mixRamp (const float* x, float* l, float* r, int n, float gL, float gR, float sL, float sR) noexcept
{
    float a0 = gL + 0.0f, a1 = gR + 0.0f, a2 = gL + sL, a3 = gR + sR;
    const float s2L = sL + sL, s2R = sR + sR;
    const int full = (n >> 3) << 3;
    for (int i = 0; i < full; i += 2)
    {
        l[i] = x[2 * i] * a0 + l[i];
        r[i] = x[2 * i + 1] * a1 + r[i];
        l[i + 1] = x[2 * i + 2] * a2 + l[i + 1];
        r[i + 1] = x[2 * i + 3] * a3 + r[i + 1];
        a0 = a0 + s2L, a1 = a1 + s2R, a2 = a2 + s2L, a3 = a3 + s2R;
    }
    for (int i = full; i < n; ++i)
    {
        l[i] = x[2 * i] * a0 + l[i];
        r[i] = x[2 * i + 1] * a1 + r[i];
        a0 = a0 + sL, a1 = a1 + sR;
    }
}

// The end-fade table (FUN_0071e060 mode 3, 10 ms): 0.5·(1 + cos((i + 1)·π/442)), built while the plugin's rate object
// still holds 44 100 Hz, so 441 entries at every rate (REA_rodada42 §3.8), with IPP's cosine (REA_rodada74 §1)
inline const std::array<float, 441>& endTable() noexcept
{
    static const auto table = [] {
        std::array<float, 441> t {};
        ippcos::endFadeTable (t.data(), 441);
        return t;
    }();
    return table;
}

// FUN_00722290 and what follows the plugin's Voice_Render (3x Osc): pan-law gains from Final.Vol/Final.Pan and the
// articulators, the channel filter (into `scratch`), the end fade, the ramped mix added into l/r. x and scratch are
// interleaved stereo blocks of n frames. Returns true when the end fade has finished (the voice is done).
inline bool mixVoice (const Settings& S, Proc& pr, float* x, float* scratch, int n, float finalVol, float finalPan, float* l, float* r) noexcept
{
    float tL, tR;
    panLaw (finalPan + pr.pan, finalVol * pr.vol, tL, tR);
    if (! pr.gainsSet)
        pr.gL = pr.gR = 0.0f, pr.gainsSet = true;
    if (pr.first > 1)
        pr.gL = tL, pr.gR = tR; // the first block starts at the target
    float* w = x;
    if (filter (S, pr, x, scratch, n, tL, tR))
        w = scratch;
    bool ended = false;
    if (pr.endPos >= 0) // end fade (FUN_0071e8b0, mode 3): the table, then silence
    {
        const auto& tab = endTable();
        const auto L = (int) tab.size();
        const auto k = std::min (std::max (L - pr.endPos, 0), n);
        for (int j = 0; j < k; ++j)
        {
            const auto t = tab[(std::size_t) (pr.endPos + j)];
            w[2 * j] = w[2 * j] * t;
            w[2 * j + 1] = w[2 * j + 1] * t;
        }
        std::fill (w + 2 * k, w + 2 * n, 0.0f);
        pr.endPos += n;
        if (pr.endPos >= L)
            ended = true;
    }
    const auto sL = rampStep (tL, pr.gL, n, S.rampLimit), sR = rampStep (tR, pr.gR, n, S.rampLimit);
    if (sL == 0.0f && sR == 0.0f)
    {
        for (int j = 0; j < n; ++j)
        {
            l[j] = w[2 * j] * tL + l[j];
            r[j] = w[2 * j + 1] * tR + r[j];
        }
    }
    else
        mixRamp (w, l, r, n, pr.gL, pr.gR, sL, sR);
    pr.gL = tL, pr.gR = tR;
    pr.first = 0;
    return ended;
}
} // namespace lynx::flvp
