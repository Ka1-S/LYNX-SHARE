#pragma once

// Kepler Exo's reverb (REA_rodada167d, FX task B): the engine embeds class fxReverb at synth+0x1c290, the same plate
// LYNX already ports as ilfx::ReverbT (GMS / Toxic Biohazard / Hardcore, REA_rodada39 §1.13, 45 §FX.5, 71). This
// build of the class differs from those in two places, everything else (parameter quantisation, set-up, the
// coefficient pass, the tank) being the same code:
//  1. parameter 4 (unnamed, raw float at +0x28, absent from the older builds) is a feedback gain on the pre-delay line:
//     the slot read this sample is rewritten as  old·fb + input  instead of  input  (process FUN_18000d630);
//  2. the early reflections always sum the full 7 left / 8 right tap tables (as Hardcore's build), but the coefficient
//     pass (FUN_18000ba00) gives a gain only to the first tap of each side; the others are multiplied by +0.
// Behaviour written from the decompile/asm of FL's own engine_x64.dll (clean-room); checked bit for bit against the
// DLL's fxReverb methods called directly (harness /mnt/e/lynx-work/exo167/fxb/).
//
// DLL map: ctor FUN_18000a3d0; vtable 0x1800eaac8: +0x08 process FUN_18000d630 (in/out pointer pairs, n samples),
// +0x28 setParam FUN_18000aaf0, +0x50 reset FUN_18000a8f0 (whole buffer ← 1e-30, filter states 0, level −1000),
// +0x58 FUN_18000aad0 (dirty flag), +0x60 setRate FUN_180009e00 → FUN_180009e20 (one block, zero-filled, positions 0);
// coefficient pass FUN_18000ba00 (run from process when a parameter changed). Engine side: global setter
// FUN_1800198a0 cases 0x79 (on, synth+0x1c648) and 0x7a–0x85 → setParam (1, 2, 3, 4, 5, 6, 9, 10, 0xe, 0xb, 0xc, 0xd)
// with 0x7d → x·0.9 (DAT_1800ed6fc) and 0x80 → x²; rate set-up FUN_18001d190 → setRate((float) fs) (it also stores
// (int)(fs·0.002) at synth+0x1c284, a field nothing reads); engine reset FUN_18001ce10 → +0x50 then +0x58; render
// FUN_18001b7d0 calls process (n = 1) after the delay when the flag is on. Index 0x9a (synth+0x1c649) is not part of
// this unit: it switches the post-voice high-pass that Kepler.cpp already ports (hpOn).

#include "core/IlFxKit.h"
#include "core/IldspMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace kepler_exo
{
// tanf FUN_1800c3990 of the engine's CRT (FMA path at 0x1800c3d00 when the CPU has FMA, the plain one otherwise):
// |x| <= pi/4 a rational approximation in double, pi/4 < |x| < 3.37e9 one Cody-Waite step by pi/2 (odd quadrant
// -1/t); the result converted in the current rounding mode. Huge arguments (Payne-Hanek path) fall back to tan.
inline float exoTanf (float xf) noexcept
{
    const auto xb = ildsp::fbits (xf);
    if ((xb & 0x7f800000u) == 0x7f800000u)
        return (float) std::tan ((double) xf); // inf / NaN: NaN
    const double x = xf;
    const auto ax = ildsp::dbits (x) & 0x7fffffffffffffffull;
    const bool fused = ildsp::useFma();
    auto rational = [fused] (double r) {
        const auto r2 = r * r;
        double num, den;
        if (fused)
        {
            num = ildsp::fmaD (-0.017203248047148168, r2, 0.3852960712639954);
            den = ildsp::fmaD (ildsp::fmaD (0.01844239256901656, r2, -0.5139650547885454), r2, 1.1558882143468838);
            return ildsp::fmaD (r2 * r, num / den, r);
        }
        num = -0.017203248047148168 * r2 + 0.3852960712639954;
        den = (0.01844239256901656 * r2 + -0.5139650547885454) * r2 + 1.1558882143468838;
        return r + (r2 * r) * (num / den);
    };
    if (ax <= 0x3fe921fb54442d18ull) // |x| <= pi/4
    {
        if (ax < 0x3f20000000000000ull) // 2^-13
        {
            if (ax < 0x3e40000000000000ull) // 2^-27
                return xf;
            const auto x3 = (x * x) * x;
            return ildsp::d2f (fused ? ildsp::fmaD (x3, 0.3333333333333333, x) : x3 * 0.3333333333333333 + x);
        }
        return ildsp::d2f (rational (x));
    }
    if (ax >= 0x41e921fb40000000ull) // 3373259264
        return (float) std::tan (x);
    const auto a = std::fabs (x);
    int k;
    double r1;
    if (fused)
    {
        k = ildsp::cvttD2I (ildsp::fmaD (a, 0.6366197723675814, 0.5));
        r1 = ildsp::fmaD (-(double) k, 1.5707963267341256, a);
    }
    else
    {
        k = ildsp::cvttD2I (a * 0.6366197723675814 + 0.5);
        r1 = a - (double) k * 1.5707963267341256;
    }
    const auto r = r1 - (double) k * 6.077100506506192e-11;
    auto t = rational (r);
    if (k & 1)
        t = -1.0 / t;
    if (x < 0.0)
        t = -t;
    return ildsp::d2f (t);
}

// The engine's CRT float routines as the reverb's coefficient pass calls them (inside the render: RZ | FTZ | DAZ):
// powf FUN_1800c1630, expf FUN_1800bf3b0, cosf FUN_1800be260 = ildsp's FMA-aware re-creations bit for bit; tanf above.
struct ExoCrt
{
    static float powf (float x, float y) noexcept { return ildsp::powf_ (x, y); }
    static float sinf (float x) noexcept { return ildsp::sinf_ (x); }
    static float cosf (float x) noexcept { return ildsp::cosf_ (x); }
    static void sincosf (float x, float& s, float& c) noexcept { ildsp::sincosf_ (x, s, c); }
    static float expf (float x) noexcept { return ildsp::expf_ (x); }
    static float tanf (float x) noexcept { return exoTanf (x); }
};

template <class Crt = ExoCrt>
struct ReverbT
{
    ilfx::ReverbT<Crt> core; // state, set-up, quantised parameters and the coefficient pass (shared with the older builds)
    float feedback = 0.0f;   // parameter 4 (+0x28): pre-delay feedback
    bool on = false;         // synth+0x1c648 (index 0x79)

    ReverbT() { construct(); }

    // FUN_18000a3d0: rate 44100, the class's preset (parameter 4 not among them), reset
    void construct()
    {
        core.allTaps = true;
        core.construct();
        feedback = 0.0f;
        core.dirty = true;
    }

    // vtable +0x60 with (float) fs: one zero-filled block, all positions 0, line lengths / taps for the full room;
    // the cached coefficients and the dirty flag are kept
    void setRate (double fs)
    {
        const auto keep = core.dirty;
        core.setRate ((float) fs);
        for (auto& b : core.buf)
            std::fill (b.begin(), b.end(), 0.0f);
        std::fill (core.pre.begin(), core.pre.end(), 0.0f);
        std::fill (core.er.begin(), core.er.end(), 0.0f);
        core.dirty = keep;
    }

    // engine reset FUN_18001ce10: vtable +0x50 (buffers ← 1e-30, states 0, level −1000) then +0x58 (dirty)
    void reset() noexcept
    {
        core.reset();
        core.dirty = true;
    }

    // the class's own setParam (FUN_18000aaf0), index 0..0x12
    void setClassParam (int i, float v) noexcept
    {
        if (i == 4)
        {
            feedback = v;
            core.dirty = true;
        }
        else if (i >= 0 && i <= 0x12)
            core.setp (i < 4 ? i : i - 1, v);
    }

    // global setter FUN_1800198a0, cases 0x79–0x85
    void setParam (int index, float x) noexcept
    {
        switch (index)
        {
            case 0x79: on = 0.5f <= x; break;
            case 0x7a: setClassParam (1, x); break;
            case 0x7b: setClassParam (2, x); break;
            case 0x7c: setClassParam (3, x); break;
            case 0x7d: setClassParam (4, x * 0.8999999761581421f); break;
            case 0x7e: setClassParam (5, x); break;
            case 0x7f: setClassParam (6, x); break;
            case 0x80: setClassParam (9, x * x); break;
            case 0x81: setClassParam (10, x); break;
            case 0x82: setClassParam (0xe, x); break;
            case 0x83: setClassParam (0xb, x); break;
            case 0x84: setClassParam (0xc, x); break;
            case 0x85: setClassParam (0xd, x); break;
            default: break;
        }
    }

    // coefficient pass FUN_18000ba00: ilfx's pass, then the tap gains this build leaves at +0
    void recalc() noexcept
    {
        core.recalc();
        for (int k = 1; k < 8; ++k)
            core.erGL[k] = core.erGR[k] = 0.0f;
    }

    // one sample of FUN_18000d630 (n = 1), when the unit is on
    void process (float& L, float& R) noexcept
    {
        if (! on)
            return;
        auto& c = core;
        if (c.dirty)
            recalc();
        constexpr float tiny = 1.0000000031710769e-30f, half = 0.5f, one = 1.0f;
        const auto d = c.decay, dd = one - d;
        const auto g41 = (dd * 0.6000000238418579f) + d, g42 = (dd * 0.25f) + d, g43 = (dd * 0.699999988079071f) + d;
        const auto g56 = (dd * 0.20000000298023224f) + d, g44 = (dd * half) + d;
        const auto width = (float) c.pi[13] * 0.009999999776482582f;
        const auto erm = c.erSize - 1;
        auto* B = c.buf;
        auto* Ln = c.len;
        auto* P = c.pos;
        const auto* T = c.taps;

        const auto xl = c.gin * L, xr = c.gin * R;
        const auto m = ((xr + xl) * half) + tiny;
        auto l = ((m - c.lp) * c.lpk) + c.lp;
        c.lp = l;
        const auto h = ((l - c.hpx) * c.hpk) + c.hpy;
        c.hpy = l;
        c.hpx = h;
        if (c.hpk != 0.0f)
            l = ((l - h) * half) + tiny;
        float erl = 0.0f, err = 0.0f;
        if (c.erLev != 0.0f)
        {
            auto e = [&] (int tap) { return c.er[(std::size_t) ((tap + c.erPos) & erm)]; };
            erl = e (c.erTL[0]) * c.erGL[0] + e (c.erTL[1]) * c.erGL[1];
            erl = erl - e (c.erTL[2]) * c.erGL[2];
            erl = e (c.erTL[3]) * c.erGL[3] + erl;
            erl = erl + e (c.erTL[4]) * c.erGL[4];
            erl = erl + e (c.erTL[5]) * c.erGL[5];
            erl = erl + e (c.erTL[6]) * c.erGL[6];
            err = e (c.erTR[0]) * c.erGR[0] - e (c.erTR[1]) * c.erGR[1];
            err = err + e (c.erTR[2]) * c.erGR[2];
            err = err + e (c.erTR[3]) * c.erGR[3];
            err = err - e (c.erTR[4]) * c.erGR[4];
            err = err + e (c.erTR[5]) * c.erGR[5];
            err = err + e (c.erTR[6]) * c.erGR[6];
            err = err - e (c.erTR[7]) * c.erGR[7];
        }
        auto t = c.ap (0, l, c.ap1, c.ap1);
        auto u = c.ap (1, t, c.ap2, c.ap1);
        c.er[(std::size_t) c.erPos] = t;
        c.erPos = c.erPos > 0 ? c.erPos - 1 : erm;
        t = c.ap (2, u, c.ap1, c.ap1);
        u = c.ap (3, t, c.ap2, c.ap1);
        // pre-delay with feedback (this build): the slot read now is rewritten as old·fb + input
        const auto pv = c.pre[(std::size_t) c.prepos];
        c.pre[(std::size_t) c.prepos] = pv * feedback + u;
        c.prepos = c.prepos > 0 ? c.prepos - 1 : c.prelen - 1;
        const auto tin = pv + 9.99999968e-21f;
        auto v = B[11][(std::size_t) P[11]] * d;
        auto s2 = ((v - c.dL1) * c.shk) + c.dL0;
        c.dL0 = v, c.dL1 = s2;
        auto w = (((((s2 + v) * c.shelf) + v) + tin) - c.tL) * c.damp;
        w = w + c.tL;
        c.tL = w;
        u = c.ap (4, w, c.tap1, c.tap1);
        auto i = P[5];
        auto o = B[5][(std::size_t) i];
        B[5][(std::size_t) i] = u;
        P[5] = i > 0 ? i - 1 : Ln[5] - 1;
        o = o * d;
        u = c.ap (6, o, c.tap2, c.tap2);
        i = P[7];
        B[7][(std::size_t) i] = u;
        i = i > 0 ? i - 1 : Ln[7] - 1;
        P[7] = i;
        v = B[7][(std::size_t) i] * d;
        s2 = ((v - c.dR1) * c.shk) + c.dR0;
        c.dR0 = v, c.dR1 = s2;
        w = (((((s2 + v) * c.shelf) + v) + tin) - c.tR) * c.damp;
        w = w + c.tR;
        c.tR = w;
        u = c.ap (8, w, c.tap1, c.tap1);
        i = P[9];
        o = B[9][(std::size_t) i];
        B[9][(std::size_t) i] = u;
        P[9] = i > 0 ? i - 1 : Ln[9] - 1;
        o = o * d;
        u = c.ap (10, o, c.tap2, c.tap2);
        i = P[11];
        B[11][(std::size_t) i] = u;
        P[11] = i > 0 ? i - 1 : Ln[11] - 1;
        auto at = [&] (int line, int tap) { return B[line][(std::size_t) c.wrap (tap + P[line], Ln[line])]; };
        const auto fA = at (9, T[0]), fB = at (9, T[1]), fC = at (10, T[2]), fD = at (11, T[3]);
        const auto fE = at (5, T[4]), fF = at (6, T[5]), fG = at (7, T[6]);
        const auto f7 = at (5, T[8]), f8 = at (5, T[7]), f9 = at (6, T[9]), f10 = at (7, T[10]), f11 = at (9, T[11]);
        const auto f12 = at (10, T[12]), f13 = at (11, T[13]);
        auto tl = (((fA - (g42 * fB)) - (g43 * fC)) - (g56 * fD)) + (g41 * fE);
        tl = (tl + fF) + (g44 * fG);
        auto tr = ((((g42 * f7) + f8) - (g43 * f9)) - (g56 * f10)) - (g41 * f11);
        tr = (tr + f12) + (g44 * f13);
        if (! (-99.0f <= c.lev)) // comiss: NaN takes the reset branch too
            c.lev = c.tank;
        c.lev = ((c.tank - c.lev) * 0.0010000000474974513f) + c.lev;
        const auto a = (c.lev * tl) + (c.erLev * erl), b = (c.lev * tr) + (c.erLev * err);
        const auto sm = b + a, df = (a - b) * width;
        L = (df + sm) + (xl * c.dry);
        R = (xr * c.dry) + (sm - df);
    }
};

using Reverb = ReverbT<>;
} // namespace kepler_exo
} // namespace lynx
