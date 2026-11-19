#pragma once

// Round 190mm (sub-round dyn): LYNX's model of the psycho-acoustic bass family (the Renaissance Bass and MaxxBass roles).
// Clean-room: the structure was read from the references' DSP (docs/analise/REA_rodada190mm_dyn.md §3) — a third-order
// Butterworth crossover built from a first- and a second-order all-pass (low = their mean, high = their half difference,
// so low + high is the second-order all-pass), the original low band scaled (or removed), and harmonics of the low band
// made from a level-normalised copy of it, kept above the crossover, added back. Our harmonic generator is our own
// simple one (a rectifying product of the normalised band, high-passed and low-passed, its level restored); every
// constant fitted on our demo-mode probes (§3.2). Header-only.

#include <algorithm>
#include <array>
#include <cmath>

namespace lynx::bassharm
{
struct Fit
{
    double harmDb = 1.294;    // the harmonics' level at intensity 0 dB (RBass) / harmonics gain 0 dB (MaxxBass)
    double third = 0.08184;   // the odd (cubic) share of the harmonics
    double normPow = 0.8445;  // level normalisation: the band divided by its envelope ^ this
    double envMs = 48.77;     // the envelope's release (its attack 1 ms)
    double topRatio = 1.975;  // the harmonics' low-pass: this multiple of the crossover
};

struct Settings
{
    double freqHz = 80.0;
    double inputDb = 0.0;      // MaxxBass input gain
    double origGain = 1.0;     // the original low band (linear; 0 = out)
    double harmDb = -200.0;    // the harmonics' level (dB, ≤ −144 = none) before the fitted offset
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
            c = {};
        h1 = h2 = {};
        env = 0.0;
    }

    void process (float* l, float* r, int n, const Settings& st) noexcept
    {
        if (st.freqHz != designed)
            design (st.freqHz);
        const double in = std::pow (10.0, st.inputDb / 20.0), out = std::pow (10.0, st.outDb / 20.0);
        const bool harm = st.harmDb > -143.9;
        const double hg = harm ? std::pow (10.0, (st.harmDb + fit.harmDb) / 20.0) : 0.0;
        const double eA = std::exp (-1000.0 / (1.0 * sr)), eR = std::exp (-1000.0 / (std::max (0.1, fit.envMs) * sr));
        for (int i = 0; i < n; ++i)
        {
            double lo[2], hi[2];
            float* io[2] { l + i, r + i };
            for (int c = 0; c < 2; ++c)
            {
                const double x = (double) *io[c] * in;
                auto& s = ch[(std::size_t) c];
                const double a1 = c1 * x + s.x1 - c1 * s.y1;
                s.x1 = x;
                s.y1 = a1;
                const double a2 = b2 * x + b1 * s.u1 + s.u2 - b1 * s.v1 - b2 * s.v2;
                s.u2 = s.u1;
                s.u1 = x;
                s.v2 = s.v1;
                s.v1 = a2;
                lo[c] = 0.5 * (a1 + a2);
                hi[c] = 0.5 * (a2 - a1);
            }
            double h = 0.0;
            if (harm)
            {
                const double m = 0.5 * (lo[0] + lo[1]), a = std::abs (m);
                env = a + (env - a) * (a > env ? eA : eR);
                const double e = std::max (env, 1.0e-6), v = m / std::pow (e, fit.normPow);
                const double k = std::pow (e, 1.0 - fit.normPow);
                double g = (v * std::abs (v) + fit.third * v * v * v) * k;
                g = hpA.run (g, h1);
                h = lpB.run (g, h2) * hg;
            }
            for (int c = 0; c < 2; ++c)
                *io[c] = (float) (out * (hi[c] + st.origGain * lo[c] + h));
        }
    }

private:
    struct Bq
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double run (double x, std::array<double, 4>& z) const noexcept
        {
            const double y = b0 * x + b1 * z[0] + b2 * z[1] - a1 * z[2] - a2 * z[3];
            z[1] = z[0]; z[0] = x; z[3] = z[2]; z[2] = y;
            return y;
        }
    };
    void design (double f) noexcept
    {
        designed = f;
        const double fc = std::clamp (f, 10.0, 0.45 * sr), t = std::tan (M_PI * fc / sr);
        c1 = (t - 1.0) / (t + 1.0);
        // the second-order all-pass with the Butterworth pair's Q = 1, bilinear (warped at fc)
        const double K = 1.0 / t, al = K, be = K * K, p0 = 1.0 + al + be;
        b1 = (2.0 - 2.0 * be) / p0;
        b2 = (1.0 - al + be) / p0;
        // the harmonics' band: a second-order high-pass at fc, a second-order low-pass at topRatio · fc
        const auto bw = [this] (double hz, bool high, Bq& q) {
            const double w = std::tan (M_PI * std::clamp (hz, 10.0, 0.45 * sr) / sr), Q = std::sqrt (0.5), nrm = 1.0 / (1.0 + w / Q + w * w);
            q.b0 = (high ? 1.0 : w * w) * nrm;
            q.b1 = (high ? -2.0 : 2.0 * w * w) * nrm;
            q.b2 = q.b0;
            q.a1 = 2.0 * (w * w - 1.0) * nrm;
            q.a2 = (1.0 - w / Q + w * w) * nrm;
        };
        bw (fc, true, hpA);
        bw (fc * fit.topRatio, false, lpB);
    }
    struct Ch { double x1 = 0, y1 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0; };
    double sr = 48000.0, designed = -1.0;
    Fit fit;
    double c1 = 0.0, b1 = 0.0, b2 = 0.0;
    std::array<Ch, 2> ch {};
    Bq hpA, lpB;
    std::array<double, 4> h1 {}, h2 {};
    double env = 0.0;
};
} // namespace lynx::bassharm
