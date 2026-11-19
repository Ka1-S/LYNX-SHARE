#pragma once

// Round 190mm (sub-round dyn): LYNX's model of the psycho-acoustic bass family (the Renaissance Bass and MaxxBass roles).
// Clean-room: the structure was read from the references' DSP (docs/analise/REA_rodada190mm_dyn.md §3) — a third-order
// Butterworth crossover built from a first- and a second-order all-pass (low = their mean, high = their half difference,
// so low + high is the second-order all-pass), the original low band scaled (or removed), and harmonics of the low band
// made from a level-normalised copy of it (the band times one plus its own clamped high band, fed back), kept in a high
// band, normalised again and added back. Written from that structure in our own code; every constant fitted on our
// demo-mode probes (§3, lab/fitrb2.py). Header-only.

#include <numbers>
#include <algorithm>
#include <array>
#include <cmath>

namespace lynx::bassharm
{
struct Fit
{
    // fitted on the RBass bank (lab/fitrb2.py: 15 renders, ESR + third-octave spectrum; objective 4.24 (our first, simpler
    // generator) → 1.69)
    double pre = 1.45758;       // the low band's level into the generator
    double feedback = 0.0564507; // the product's feedback (its own high band, clamped, × the output envelope)
    double limit = 0.326024;    // the harmonics' peak limit
    double level = 0.756352;    // the harmonics' level at intensity 0 dB
    double floor1 = 0.00398, pow1 = 0.509693; // the input normaliser: (|b| (1 − floor) + floor)^−pow (floors kept as read)
    double floor2 = 0.00398, pow2 = 0.21285;  // the output normaliser
    double attackMs = 5.0, releaseMs = 47.8184; // both normalisers' smoothing (towards less / more gain; attack as read)
    double bandRatio = 4.88893; // the product's own high band: the crossover / this
};

struct Settings
{
    double freqHz = 80.0;
    double inputDb = 0.0;      // MaxxBass input gain
    double origGain = 1.0;     // the original low band (linear; 0 = out)
    double harmDb = -200.0;    // the harmonics' level (dB, ≤ −144 = none)
    double outDb = 0.0;
};

class Engine
{
public:
    void prepare (double sampleRate, const Fit& f = {}) noexcept
    {
        sr = sampleRate;
        fit = f;
        designed = -1.0;
        reset();
    }
    void reset() noexcept
    {
        for (auto& c : ch)
        {
            c.ap1.clear();
            c.ap2.clear();
        }
        hA.clear();
        qA.clear();
        hB.clear();
        qB.clear();
        e1 = g2 = 1.0;
        fb = 0.0;
    }

    void process (float* l, float* r, int n, const Settings& st) noexcept
    {
        if (st.freqHz != designed)
            design (st.freqHz);
        const double in = std::pow (10.0, st.inputDb / 20.0), out = std::pow (10.0, st.outDb / 20.0);
        const bool harm = st.harmDb > -143.9;
        const double hg = harm ? fit.level * std::pow (10.0, st.harmDb / 20.0) : 0.0;
        const double cA = std::exp (-1000.0 / (std::max (0.01, fit.attackMs) * sr)), cR = std::exp (-1000.0 / (std::max (0.01, fit.releaseMs) * sr));
        for (int i = 0; i < n; ++i)
        {
            double lo[2], hi[2];
            float* io[2] { l + i, r + i };
            for (int c = 0; c < 2; ++c)
            {
                const double x = (double) *io[c] * in;
                auto& s = ch[(std::size_t) c];
                const double a1 = s.ap1.run (x), a2 = s.ap2.run (x);
                lo[c] = 0.5 * (a1 + a2);
                hi[c] = 0.5 * (a2 - a1);
            }
            double h = 0.0;
            if (harm)
            {
                // the level-normalised low band times (1 + its own clamped high band): harmonics
                const double b = 0.5 * (lo[0] + lo[1]) * fit.pre;
                const double t1 = std::pow (std::abs (b) * (1.0 - fit.floor1) + fit.floor1, -fit.pow1);
                e1 = t1 + (e1 - t1) * (t1 < e1 ? cA : cR);
                const double x = 0.5 * b * e1 * (1.0 + fb);
                const double hx = 0.5 * (hB.run (x) - hA.run (x));
                const double hy = qB.run (hx) - qA.run (hx);
                const double t2 = std::pow (std::abs (0.5 * hy) * (1.0 - fit.floor2) + fit.floor2, -fit.pow2);
                g2 = t2 + (g2 - t2) * (t2 < g2 ? cA : cR);
                fb = std::clamp (hx, -9.0, 9.0) * fit.feedback * g2;
                h = -0.5 * hy * g2;
                if (std::abs (h) > fit.limit)
                    h *= fit.limit / std::abs (h);
                h *= hg;
            }
            for (int c = 0; c < 2; ++c)
                *io[c] = (float) (out * (hi[c] + st.origGain * lo[c] + h));
        }
    }

private:
    // the Butterworth pair's all-passes: first order (the real pole) and second order (Q = 1), bilinear, warped at f
    struct Ap1
    {
        double c = 0, x1 = 0, y1 = 0;
        void set (double f, double sr) noexcept { const double t = std::tan (std::numbers::pi * std::clamp (f, 5.0, 0.45 * sr) / sr); c = (t - 1.0) / (t + 1.0); }
        double run (double x) noexcept { const double y = c * x + x1 - c * y1; x1 = x; y1 = y; return y; }
        void clear() noexcept { x1 = y1 = 0.0; }
    };
    struct Ap2
    {
        double b1 = 0, b2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0;
        void set (double f, double sr) noexcept
        {
            const double K = 1.0 / std::tan (std::numbers::pi * std::clamp (f, 5.0, 0.45 * sr) / sr), p0 = 1.0 + K + K * K;
            b1 = (2.0 - 2.0 * K * K) / p0;
            b2 = (1.0 - K + K * K) / p0;
        }
        double run (double x) noexcept { const double y = b2 * x + b1 * u1 + u2 - b1 * v1 - b2 * v2; u2 = u1; u1 = x; v2 = v1; v1 = y; return y; }
        void clear() noexcept { u1 = u2 = v1 = v2 = 0.0; }
    };
    void design (double f) noexcept
    {
        designed = f;
        for (auto& c : ch)
        {
            c.ap1.set (f, sr);
            c.ap2.set (f, sr);
        }
        hA.set (f / fit.bandRatio, sr);
        hB.set (f / fit.bandRatio, sr);
        qA.set (f, sr);
        qB.set (f, sr);
    }
    struct Ch { Ap1 ap1; Ap2 ap2; };
    double sr = 48000.0, designed = -1.0;
    Fit fit;
    std::array<Ch, 2> ch {};
    Ap1 hA, qA;
    Ap2 hB, qB;
    double e1 = 1.0, g2 = 1.0, fb = 0.0;
};
} // namespace lynx::bassharm
