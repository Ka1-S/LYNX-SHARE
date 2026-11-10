#pragma once

// FL's Spreader engine (REA_rodada54 §5), shared by FL's Spreader (core/StereoFx.cpp) and Pocket Spreader (FL Studio
// Mobile's Spreader runs the same Faust program behind its own control mapping, core/PocketFxC.cpp, round 189h).

#include <algorithm>
#include <cmath>
#include <iterator>

namespace lynx::stereofx
{
// the Spreader engine (REA_rodada54 §5): a Faust-built single-precision stereo widener with smoothed controls
class SpreaderEngine
{
public:
    float width = 1.0f, separation = 0.5f, lowBypass = 10.0f, monoBass = 0.0f, enable = 0.0f; // the engine's controls

    void constants (int sampleRate) noexcept
    {
        const auto r = (float) sampleRate;
        const auto c0 = std::min (r > 1.0f ? r : 1.0f, 192000.0f);
        const auto c1 = 1.0f / c0;
        k = (float) std::exp ((double) (c1 * -500.0f));
        k1 = 1.0f - k;
        wc = c1 * 3.1415927410125732f;
        maxDelay = (int) (c0 * 0.0063492064364254475f);
        maxDelayF = (float) maxDelay;
        fadeStep = 1.0f / (float) (int) (c0 * 0.02321995422244072f);
        const auto t = (float) std::tan ((double) (c1 * 47123.890625f));
        const auto f = 1.0f / t;
        cb4 = 1.0f - f;
        cc4 = f * 0.5f;
        cb8 = 1.0f / (f + 1.0f);
    }
    void clear() noexcept
    {
        lb = en = w = fade = fadeStepNow = 0.0f;
        std::fill (std::begin (s), std::end (s), 0.0f);
        b0 = bc = c8 = d0 = d8 = e0 = sepNow = mbNow = 0.0f;
        idx0 = idx1 = 0;
        iota = 0;
        std::fill (std::begin (lineA), std::end (lineA), 0.0f);
        std::fill (std::begin (lineB), std::end (lineB), 0.0f);
    }

    void compute (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        constexpr float sq2 = 1.4142135381698608f;
        constexpr float c796 = 0.7962143421173096f, c398 = 0.3981071710586548f;
        const auto aLb = k1 * lowBypass, aEn = k1 * enable, aW = k1 * width, aSep = k1 * separation, aMb = k1 * monoBass;
        for (int i = 0; i < n; ++i)
        {
            lb = lb * k + aLb;
            const auto t = (float) std::tan ((double) (lb * wc));
            const auto t2 = t + sq2;
            const auto g = 1.0f / (t2 * t + 1.0f);
            en = en * k + aEn;
            const auto xL = inL[i], xR = inR[i];
            float hpL, lowL, hpR, lowR;
            chain (en * xL, t, t2, g, s, hpL, lowL);
            chain (en * xR, t, t2, g, s + 4, hpR, lowR);
            const auto io = iota & 2047;
            lineA[io] = (lowR + hpL) - (hpR + lowL);
            w = w * k + aW;
            const auto iv = (int) (w * maxDelayF);
            float x = 0.0f;
            if (fadeStepNow != 0.0f)
                x = (fade > 0.0f && fade < 1.0f) ? fadeStepNow : 0.0f;
            else if (fade == 0.0f)
                x = iv != idx0 ? fadeStep : 0.0f;
            else if (fade == 1.0f)
                x = iv != idx1 ? -fadeStep : 0.0f;
            fadeStepNow = x;
            const auto y = fade + x;
            const auto f = std::max (y >= 1.0f ? 1.0f : y, 0.0f);
            const auto n0 = (fade >= 1.0f && idx1 != iv) ? iv : idx0;
            const auto n1 = (fade <= 0.0f && idx0 != iv) ? iv : idx1;
            fade = f;
            const auto d1 = std::min (n0 > 0 ? n0 : 0, maxDelay), d2 = std::min (n1 > 0 ? n1 : 0, maxDelay);
            const auto a = lineA[(iota - d1) & 2047], b = lineA[(iota - d2) & 2047];
            const auto x10 = ((a - b) * f - a) * w;
            const auto nbc = -(((b0 + x10) * 0.5f + cb4 * bc) * cb8);
            const auto nc8 = -(((x10 - b0) * cc4 + cb4 * c8) * cb8);
            const auto hps = hpR + hpL, lows = lowR + lowL;
            const auto hx = nc8 * c796 + hps;
            lineB[io] = hps - lows;
            const auto c = lineB[(iota - d1) & 2047], d = lineB[(iota - d2) & 2047];
            const auto nd0 = (c - (c - d) * f) * w;
            const auto mb = k * mbNow + aMb;
            const auto sep = k * sepNow + aSep;
            const auto ne0 = ((nd0 - d0) * cc4 - cb4 * e0) * cb8;
            const auto nd8 = ((d0 + nd0) * 0.5f - cb4 * d8) * cb8;
            const auto mid = (((mb * lows + nbc) + (hx + nbc)) - lows) * 0.5f;
            const auto side0 = (lows - ((nbc - (hpL - lowL)) * 2.0f + hx)) * 0.5f;
            const auto side = ((side0 + (ne0 + nc8) * c398) + (nd8 + nbc)) * (sep + sep);
            const auto dry = 1.0f - en, mono = 1.0f - mb;
            outL[i] = (dry * xL + mid) + (mono * lowL + side);
            outR[i] = ((mono * lowR + dry * xR) + mid) - side;
            b0 = x10;
            bc = nbc;
            c8 = nc8;
            d0 = nd0;
            d8 = nd8;
            e0 = ne0;
            mbNow = mb;
            sepNow = sep;
            idx0 = n0;
            idx1 = n1;
            ++iota;
        }
    }

private:
    float k = 0, k1 = 0, wc = 0, maxDelayF = 0, fadeStep = 0, cb4 = 0, cc4 = 0, cb8 = 0;
    int maxDelay = 0;
    float lb = 0, en = 0, w = 0, fade = 0, fadeStepNow = 0;
    float s[8] {}; // two cascaded 2-pole sections per channel
    float b0 = 0, bc = 0, c8 = 0, d0 = 0, d8 = 0, e0 = 0, sepNow = 0, mbNow = 0;
    int idx0 = 0, idx1 = 0;
    unsigned iota = 0;
    float lineA[2048] {}, lineB[2048] {};

    static void chain (float e, float t, float t2, float g, float* st, float& hp, float& low) noexcept
    {
        const auto v1 = ((e - (st[0] * t2 + st[1])) * t) * g;
        const auto na = (v1 + v1) + st[0];
        const auto u1 = st[0] + v1;
        hp = e - u1 * sq8c;
        const auto w1 = u1 * t;
        const auto nb = (w1 + w1) + st[1];
        const auto bb = w1 + st[1];
        const auto v2 = ((bb - (st[2] * t2 + st[3])) * t) * g;
        const auto nc = (v2 + v2) + st[2];
        const auto u2 = st[2] + v2;
        const auto w2 = u2 * t;
        const auto nd = (w2 + w2) + st[3];
        low = st[3] + w2;
        st[0] = na;
        st[1] = nb;
        st[2] = nc;
        st[3] = nd;
    }
    static constexpr float sq8c = 2.8284270763397217f;
};
} // namespace lynx::stereofx
