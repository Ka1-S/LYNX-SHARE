#pragma once

// Kepler Exo's "hyper chorus" (REA_rodada167d, FX task B), written from the decompile/asm of FL's own engine_x64.dll
// (clean-room) and checked bit for bit against the DLL's functions called directly (harness /mnt/e/lynx-work/exo167/fxb/).
//
// Structure (wrapper at synth+0x1cc78, ctor FUN_18000ea10, process FUN_18000ebf0):
//  - two banks (FUN_18000f3a0 objects, 0x2000d0 bytes each, pointers at synth+0x1cd48 / +0x1cd50, table rows 0–3 and
//    4–7 of DAT_180105a40) of 4 voices per side. Per voice (FUN_18000f880, SSE lanes = voices): two parabolic LFOs
//    (phase p → 4·((m − m²) − (n − n²)), m = min(2p, 1), n = max(2p, 1) − 1), LFO 1 moves the delay time (the right
//    side reads it at phase + offset), LFO 2 pans the input between the left and right lines (equal power, ×1.41443);
//    each line is 65536 floats addressed by a 16-bit write index, with feedback from the voice's own last output and
//    a 4-point cubic read at the integer part of the delay. Initial LFO phases come from the C runtime's rand()
//    (FUN_18000f320: 4 values (float)(rand()·(1/32767)) per LFO, bank A then B: 16 calls at construction).
//  - wet = Σ bank A voices × 0.5623 (or (Σ B + Σ A) × 0.2812 when "double" is on, index 0x90), then a low-pass
//    biquad (index 0x91) and a high-pass biquad (index 0x92) faded in/out over (int)(fs·0.01) samples whenever 0x92
//    becomes non-zero / zero (FUN_18000f050); out = in·dry + wet·wet-gain (index 0x93, equal-power law with sqrt).
//  - biquad designer FUN_18000e570 (double math, types 1 = low-pass and 2 = high-pass with α = sin w, i.e. Q = 0.5),
//    cut-offs FUN_18000f200 (714·2^(4.9454355·x)) and FUN_18000f290 (12·2^(6.5051498·x)).
// Engine side: global setter FUN_1800198a0 cases 0x89 (on, synth+0x1cc70), 0x8a–0x94; rate set-up FUN_18001d190
// (stores fs and the fade length only: the banks' delay / rate values and the biquads are NOT recomputed there, they
// keep the rate of the last parameter change, as in the engine); the engine reset FUN_18001ce10 does not touch the unit.
//
// Parameters run outside the render (default rounding mode), process inside it (RZ | FTZ | DAZ); every expression
// keeps the engine's types and order, conversions follow the current mode (cvtps2dq / cvtpd2ps).

#include "core/IldspMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace kepler_exo
{
// The engine's CRT routines the chorus calls (in the parameter path): powf FUN_1800c1630 (= ildsp::powf_ bit for bit)
// and the double sine/cosine pair FUN_1800ba020 (= std::sin / std::cos, see the report of the round)
struct ChorusCrt
{
    static float powf (float x, float y) noexcept { return ildsp::powf_ (x, y); }
    static void sincos (double w, double& s, double& c) noexcept
    {
        s = std::sin (w);
        c = std::cos (w);
    }
};

template <class Crt = ChorusCrt>
struct HyperChorusT
{
    // per-voice table of DAT_180105a40 (measured values): delay ms lo/hi, LFO 1 Hz lo/hi, depth lo/hi, LFO 2 Hz lo/hi
    static constexpr float table[8][8] = {
        { 2.0f, 6.0f, 1.5f, 2.4700000286102295f, 20.0f, 80.0f, 0.33000001311302185f, 3.559999942779541f },
        { 3.5f, 8.25f, 1.5700000524520874f, 3.4100000858306885f, 20.0f, 80.0f, 0.28999999165534973f, 2.6600000858306885f },
        { 6.5f, 10.0f, 0.3799999952316284f, 3.640000104904175f, 20.0f, 80.0f, 0.25999999046325684f, 3.7799999713897705f },
        { 5.0f, 12.800000190734863f, 1.2899999618530273f, 3.680000066757202f, 20.0f, 80.0f, 0.14000000059604645f, 2.9800000190734863f },
        { 7.099999904632568f, 16.600000381469727f, 3.4100000858306885f, 5.03000020980835f, 20.0f, 80.0f, 0.4399999976158142f, 5.059999942779541f },
        { 9.100000381469727f, 15.100000381469727f, 2.4700000286102295f, 5.369999885559082f, 20.0f, 80.0f, 0.6600000262260437f, 8.079999923706055f },
        { 8.300000190734863f, 16.799999237060547f, 3.009999990463257f, 6.019999980926514f, 20.0f, 80.0f, 0.9300000071525574f, 6.019999980926514f },
        { 9.600000381469727f, 17.700000762939453f, 3.299999952316284f, 6.5f, 20.0f, 80.0f, 0.4099999964237213f, 10.220000267028809f },
    };
    static constexpr int lineSize = 65536;

    static int cvt (float x) noexcept { return ildsp::cvtD2I ((double) x); } // cvtps2dq: current rounding mode
    static float frac (float x) noexcept { return x - (float) cvt (x); }
    static float tri (float p) noexcept
    {
        const auto a = p + p;
        const auto m = a < 1.0f ? a : 1.0f;            // minps
        const auto n = (a > 1.0f ? a : 1.0f) - 1.0f;   // maxps
        return (m - m * m) - (n - n * n);
    }
    // MSVC rand(): the C runtime's per-thread state
    static int crtRand (std::uint32_t& s) noexcept
    {
        s = s * 0x343fdu + 0x269ec3u;
        return (int) ((s >> 16) & 0x7fffu);
    }

    struct Bank // FUN_18000f3a0 object
    {
        float out[8] {}; // last output of the left voices 0–3, then the right ones
        float dly[4] {}, fb[4] {}, depth[4] {}, ph1[4] {}, inc1[4] {}, off[4] {}, ph2[4] {}, inc2[4] {}, amt[4] {};
        std::vector<float> line; // 8 lines (left 0–3, right 0–3) of 65536 floats
        std::uint16_t widx = 0;
        double fs = 44100.0;
        int mode = 0; // first table row (0 or 4)

        void construct (int firstRow, std::uint32_t& rnd)
        {
            fs = 44100.0;
            mode = firstRow;
            std::fill (std::begin (dly), std::end (dly), 0.0f);
            std::fill (std::begin (fb), std::end (fb), 0.0f);
            std::fill (std::begin (depth), std::end (depth), 0.0f);
            std::fill (std::begin (inc1), std::end (inc1), 0.0f);
            std::fill (std::begin (off), std::end (off), 0.0f);
            std::fill (std::begin (inc2), std::end (inc2), 0.0f);
            std::fill (std::begin (out), std::end (out), 0.0f);
            randomPhases (ph1, rnd);
            randomPhases (ph2, rnd);
            widx = 0;
            line.assign ((std::size_t) 8 * lineSize, 0.0f);
        }
        static void randomPhases (float* p, std::uint32_t& rnd) // FUN_18000f320
        {
            for (int k = 0; k < 4; ++k)
            {
                auto v = ildsp::d2f ((double) crtRand (rnd) * 3.051850947599719e-05);
                if (0.0f <= v)
                {
                    if (1.0f <= v)
                        v = 1.0f;
                }
                else
                    v = 0.0f;
                p[k] = v;
            }
        }
        void set (int which, float x) noexcept // FUN_18000f440
        {
            const auto fsf = ildsp::d2f (fs);
            for (int k = 0; k < 4; ++k)
            {
                const auto* t = table[mode + k];
                switch (which)
                {
                    case 0: dly[k] = (((t[1] - t[0]) * x + t[0]) * 0.0010000000474974513f) * fsf; break;
                    case 1: fb[k] = x * 0.699999988079071f; break;
                    case 2: inc1[k] = ((t[3] - t[2]) * x + t[2]) / fsf; break;
                    case 3:
                    {
                        const auto l = (t[5] - t[4]) * x + t[4];
                        depth[k] = (l * l) * 4.999999873689376e-05f;
                        break;
                    }
                    case 4: amt[k] = x * 0.49900001287460327f; break;
                    case 5: inc2[k] = ((t[7] - t[6]) * x + t[6]) / fsf; break;
                    case 10: off[k] = ((x * x) * x) * 0.25f; break;
                    default: return;
                }
            }
        }
        static float cubic (const float* b, std::uint16_t w, float d) noexcept
        {
            const auto i = (std::uint16_t) (w - (std::uint16_t) cvt (d));
            const auto x0 = b[i], x1 = b[(std::uint16_t) (i - 1)], x2 = b[(std::uint16_t) (i - 2)], x3 = b[(std::uint16_t) (i - 3)];
            const auto c1 = (x2 - x0) * 0.5f;
            const auto g = x1 - x2;
            const auto dd = g + c1;
            const auto e = ((x3 - x1) * 0.5f) + (dd + g);
            const auto f = frac (d);
            return ((f * e - (e + dd)) * f + c1) * f + x1;
        }
        void tick (float inL, float inR) noexcept // FUN_18000f880
        {
            constexpr float eqp = 1.4144271612167358f;
            float dL[4], dR[4], pan[4];
            for (int k = 0; k < 4; ++k)
            {
                const auto t1 = tri (ph1[k]);
                const auto tR = tri (frac (ph1[k] + off[k]));
                const auto t2 = tri (ph2[k]);
                pan[k] = ((t2 * 4.0f) * amt[k]) + 0.5f;
                const auto dd = dly[k] * depth[k];
                dL[k] = ((t1 * 4.0f) * dd) + dly[k];
                dR[k] = ((tR * 4.0f) * dd) + dly[k];
            }
            float* L = line.data();
            float* R = L + (std::size_t) 4 * lineSize;
            for (int k = 0; k < 4; ++k)
                L[(std::size_t) k * lineSize + widx] = ((std::sqrt (1.0f - pan[k]) * eqp) * inL) + (fb[k] * out[k]);
            for (int k = 0; k < 4; ++k)
                out[k] = cubic (L + (std::size_t) k * lineSize, widx, dL[k]);
            for (int k = 0; k < 4; ++k)
                R[(std::size_t) k * lineSize + widx] = ((std::sqrt (pan[k]) * eqp) * inR) + (fb[k] * out[4 + k]);
            for (int k = 0; k < 4; ++k)
                out[4 + k] = cubic (R + (std::size_t) k * lineSize, widx, dR[k]);
            ++widx;
            for (int k = 0; k < 4; ++k)
            {
                ph1[k] = frac (inc1[k] + ph1[k]);
                ph2[k] = frac (inc2[k] + ph2[k]);
            }
        }
        float sumL() const noexcept { return ((out[0] + out[1]) + out[2]) + out[3]; }
        float sumR() const noexcept { return ((out[4] + out[5]) + out[6]) + out[7]; }
    };

    struct Biquad // coefficients b0 b1 b2 −a1 −a2, state y1 y2 x1 x2 per side
    {
        double fs = 44100.0;
        float q[5] {};
        float sL[4] {}, sR[4] {};
        static float run (const float* q, float* s, float x) noexcept
        {
            const auto y = (((q[3] * s[0] + q[4] * s[1]) + q[2] * s[3]) + (q[0] * x + q[1] * s[2]));
            s[1] = s[0], s[0] = y, s[3] = s[2], s[2] = x;
            return y;
        }
        // FUN_18000e570, types 1 (low-pass) and 2 (high-pass), gain 1
        void design (int type, double f) noexcept
        {
            const auto w = (f * 6.283185307179586) * (1.0 / fs);
            double s, c;
            Crt::sincos (w, s, c);
            const auto a0 = s + 1.0, a1 = c * -2.0, a2 = 1.0 - s;
            double b0, b1;
            if (type == 1)
                b1 = 1.0 - c, b0 = b1 * 0.5;
            else
                b0 = (c + 1.0) * 0.5, b1 = -(c + 1.0);
            const auto k = 1.0 / a0;
            q[0] = ildsp::d2f (k * b0);
            q[1] = ildsp::d2f (k * b1);
            q[2] = ildsp::d2f (k * b0);
            q[3] = -ildsp::d2f (k * a1);
            q[4] = -ildsp::d2f (k * a2);
            for (int i = 0; i < 3; ++i)
                q[i] = 1.0f * q[i];
        }
    };

    Bank A, B;
    Biquad lp, hp;
    int count = 0, fadeLen = 441;
    float fadeInv = 1.0f / 441.0f;
    float dry = 1.0f, wet = 0.0f;
    bool dbl = false;  // index 0x90
    bool on = false;   // index 0x89 (synth+0x1cc70)
    double fs = 44100.0;
    float raw[11] { 0.4000000059604645f, 0.5f, 0.4000000059604645f, 0.4000000059604645f, 0.30000001192092896f,
                    0.30000001192092896f, 0.0f, 1.0f, 0.0f, 0.5f, 0.10000000149011612f }; // 0x8a–0x94 (ctor values)

    // randState: the C runtime's rand() state of the constructing thread (1 for a fresh thread: a first instance)
    explicit HyperChorusT (std::uint32_t randState = 1u) { construct (randState); }

    // FUN_18000ea10 (the bank constructors draw the LFO phases); returns the rand() state after the 16 draws
    std::uint32_t construct (std::uint32_t randState)
    {
        A.construct (0, randState);
        B.construct (4, randState);
        lp = Biquad {};
        hp = Biquad {};
        count = 0;
        fadeLen = 441;
        fadeInv = 1.0f / 441.0f;
        dry = 1.0f, wet = 0.0f;
        dbl = false;
        fs = 44100.0;
        return randState;
    }

    // the chorus part of FUN_18001d190
    void setRate (double rate)
    {
        fs = rate;
        fadeLen = ildsp::cvttD2I (rate * 0.01);
        fadeInv = 1.0f / (float) fadeLen;
        A.fs = B.fs = rate;
        lp.fs = hp.fs = rate;
    }

    // the engine reset (FUN_18001ce10) leaves the chorus alone
    void reset() noexcept {}

    void setParam (int index, float x) noexcept
    {
        if (index == 0x89)
        {
            on = 0.5f <= x;
            return;
        }
        if (index < 0x8a || index > 0x94)
            return;
        raw[index - 0x8a] = x;
        switch (index)
        {
            case 0x8a: A.set (0, x), B.set (0, x); break;
            case 0x8b: A.set (1, x), B.set (1, x); break;
            case 0x8c: A.set (2, x), B.set (2, x); break;
            case 0x8d: A.set (3, x), B.set (3, x); break;
            case 0x8e: A.set (4, x), B.set (4, x); break;
            case 0x8f: A.set (5, x), B.set (5, x); break;
            case 0x90: dbl = 0.5f <= x; break;
            case 0x91: lp.design (1, (double) (Crt::powf (2.0f, 4.945435523986816f * x) * 714.0f)); break;
            case 0x92: hp.design (2, (double) (Crt::powf (2.0f, 6.505149841308594f * x) * 12.0f)); break;
            case 0x93:
            {
                float f;
                if (0.5f <= x)
                {
                    const auto t = 1.0f - (((x - 0.5f) + x) - 0.5f);
                    f = 1.0f - (t * t) * 0.5f;
                }
                else
                    f = ((x + x) * (x + x)) * 0.5f;
                wet = std::sqrt (f);
                dry = std::sqrt (1.0f - f);
                break;
            }
            case 0x94: A.set (10, x), B.set (10, x); break;
            default: break;
        }
    }

    void process (float& L, float& R) noexcept // FUN_18000ebf0, when the unit is on
    {
        if (! on)
            return;
        const auto inL = L, inR = R;
        A.tick (inL, inR);
        auto wl = A.sumL(), wr = A.sumR();
        if (! dbl)
        {
            wr = wr * 0.5623409748077393f;
            wl = wl * 0.5623409748077393f;
        }
        else
        {
            B.tick (inL, inR);
            wr = (B.sumR() + wr) * 0.28117048740386963f;
            wl = (B.sumL() + wl) * 0.28117048740386963f;
        }
        auto yl = Biquad::run (lp.q, lp.sL, wl);
        auto yr = Biquad::run (lp.q, lp.sR, wr);
        auto fade = [&] {
            const auto g = (float) count * fadeInv;
            const auto hl = Biquad::run (hp.q, hp.sL, yl), hr = Biquad::run (hp.q, hp.sR, yr);
            const auto ng = 1.0f - g;
            yl = yl * ng + hl * g;
            yr = yr * ng + hr * g;
        };
        if (raw[8] == 0.0f)
        {
            if (count > 0)
                fade(), --count;
        }
        else if (count < fadeLen)
            fade(), ++count;
        else
        {
            yl = Biquad::run (hp.q, hp.sL, yl);
            yr = Biquad::run (hp.q, hp.sR, yr);
        }
        L = inL * dry + yl * wet;
        R = inR * dry + yr * wet;
    }
};

using HyperChorus = HyperChorusT<>;
} // namespace kepler_exo
} // namespace lynx
