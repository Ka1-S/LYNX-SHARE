#pragma once

// Round 190mm (sub-round dyn): LYNX's model of a five-band harmonic enhancer (the Waves Vitamin role). Clean-room: the
// reference's control code was read for its crossover design only (the in-phase squared-biquad crossover with a Q, the
// same structure as Prism's C4 law; docs/analise/REA_rodada190mm_dyn.md §6); the five bands are split as a tree whose
// sum is the product of the four crossovers' all-passes (each band carries the all-passes of the crossovers above it).
// The direct path is that sum times the direct gain; each band with a gain adds our own enhancement (a soft-saturated
// copy of the band, scaled by the gain). Every constant fitted on our demo-mode probes (§6.2): the four
// crossovers' effective frequency and Q (from the default state's impulse) and the enhancement's drive and level.

#include <numbers>
#include <algorithm>
#include <array>
#include <cmath>

namespace lynx::vitamin
{
struct Fit
{
    // per crossover: its pole frequency against the control and its Q (the default state's impulse, 12000 samples:
    // −56 dB; lab/fitvitap.py)
    double fScale[4] { 0.94396, 0.95661, 0.93832, 0.93357 };
    double q[4] { 0.6172, 0.57424, 0.56248, 0.62559 };
    double drive = 0.24040; // the enhancement's saturation drive (lab/fitvit2.py: the 20 X renders, ESR + spectrum)
    double level[5] { 0.01995, 0.01816, 0.01669, 0.03159, 0.03301 }; // per band: its level at a band gain of 0 dB (the gain adds dB)
};

struct Settings
{
    double crossHz[4] { 90.0, 500.0, 2594.0, 9310.0 };
    double bandGainDb[5] { -200.0, -200.0, -200.0, -200.0, -200.0 }; // ≤ −144 = Off
    double directDb = 0.0;   // ≤ −144 = Off
    double inDb = 0.0, outDb = 0.0;
};

class Engine
{
public:
    void prepare (double sampleRate, const Fit& f = {}) noexcept
    {
        sr = sampleRate;
        fit = f;
        key.fill (-1.0);
        reset();
    }
    void reset() noexcept
    {
        for (auto& c : xo)
            for (auto& x : c)
                x.reset();
        for (auto& c : comp)
            for (auto& x : c)
                x.reset();
    }

    void process (float* l, float* r, int n, const Settings& st) noexcept
    {
        bool changed = false;
        for (int k = 0; k < 4; ++k)
            changed = changed || st.crossHz[k] != key[(std::size_t) k];
        if (changed)
            design (st);
        const double in = std::pow (10.0, st.inDb / 20.0), out = std::pow (10.0, st.outDb / 20.0);
        const double direct = st.directDb > -143.9 ? std::pow (10.0, st.directDb / 20.0) : 0.0;
        std::array<double, 5> g {};
        bool any = false;
        for (int b = 0; b < 5; ++b)
        {
            g[(std::size_t) b] = st.bandGainDb[b] > -143.9 ? fit.level[b] * std::pow (10.0, st.bandGainDb[b] / 20.0) : 0.0;
            any = any || g[(std::size_t) b] > 0.0;
        }
        const double d = std::max (1e-3, fit.drive);
        for (int i = 0; i < n; ++i)
        {
            float* io[2] { l + i, r + i };
            for (int c = 0; c < 2; ++c)
            {
                auto& x = xo[(std::size_t) c];
                auto& k = comp[(std::size_t) c];
                const double v = (double) *io[c] * in;
                const double l1 = x[0].lo (v), h1 = x[0].ap.run (v) - l1;
                const double l2 = x[1].lo (h1), h2 = x[1].ap.run (h1) - l2;
                const double l3 = x[2].lo (h2), h3 = x[2].ap.run (h2) - l3;
                const double l4 = x[3].lo (h3), h4 = x[3].ap.run (h3) - l4;
                const double band[5] { k[2].ap.run (k[1].ap.run (k[0].ap.run (l1))), k[4].ap.run (k[3].ap.run (l2)), k[5].ap.run (l3), l4, h4 };
                double y = 0.0;
                for (int b = 0; b < 5; ++b)
                {
                    y += band[b] * direct;
                    if (any && g[(std::size_t) b] > 0.0)
                        y += g[(std::size_t) b] * std::tanh (d * band[b]) / d;
                }
                *io[c] = (float) (y * out);
            }
        }
    }

private:
    struct Bq
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 4> z {};
        double run (double x) noexcept
        {
            const double y = b0 * x + b1 * z[0] + b2 * z[1] - a1 * z[2] - a2 * z[3];
            z[1] = z[0]; z[0] = x; z[3] = z[2]; z[2] = y;
            return y;
        }
        void reset() noexcept { z.fill (0.0); }
    };
    struct Crossover
    {
        Bq lpA, lpB, ap;
        double lo (double x) noexcept { return lpB.run (lpA.run (x)); }
        void design (double fc, double q, double sr) noexcept
        {
            const double b = 1.0 / (q * q) - 2.0, l2 = 0.5 * (-b + std::sqrt (b * b + 4.0));
            const double K = 2.0 * sr, w = K * std::tan (std::numbers::pi * std::clamp (fc, 10.0, 0.49 * sr) / sr) / std::sqrt (l2);
            const double al = K / (q * w), be = (K / w) * (K / w);
            const double p0 = 1.0 + al + be, p1 = (2.0 - 2.0 * be) / p0, p2 = (1.0 - al + be) / p0;
            for (auto* s : { &lpA, &lpB })
            {
                s->a1 = p1; s->a2 = p2; s->b0 = 1.0 / p0; s->b1 = 2.0 / p0; s->b2 = 1.0 / p0;
            }
            ap.a1 = p1; ap.a2 = p2; ap.b0 = p2; ap.b1 = p1; ap.b2 = 1.0;
        }
        void reset() noexcept { lpA.reset(); lpB.reset(); ap.reset(); }
    };
    void design (const Settings& st) noexcept
    {
        for (int k = 0; k < 4; ++k)
            key[(std::size_t) k] = st.crossHz[k];
        const double f[4] { st.crossHz[0] * fit.fScale[0], st.crossHz[1] * fit.fScale[1], st.crossHz[2] * fit.fScale[2], st.crossHz[3] * fit.fScale[3] };
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 4; ++k)
                xo[(std::size_t) c][(std::size_t) k].design (f[k], fit.q[k], sr);
            // the compensation all-passes: band 1 carries crossovers 2 … 4, band 2 crossovers 3 and 4, band 3 crossover 4
            comp[(std::size_t) c][0].design (f[1], fit.q[1], sr);
            comp[(std::size_t) c][1].design (f[2], fit.q[2], sr);
            comp[(std::size_t) c][2].design (f[3], fit.q[3], sr);
            comp[(std::size_t) c][3].design (f[2], fit.q[2], sr);
            comp[(std::size_t) c][4].design (f[3], fit.q[3], sr);
            comp[(std::size_t) c][5].design (f[3], fit.q[3], sr);
        }
    }
    double sr = 48000.0;
    Fit fit;
    std::array<double, 4> key {};
    std::array<std::array<Crossover, 4>, 2> xo {};
    std::array<std::array<Crossover, 6>, 2> comp {};
};
} // namespace lynx::vitamin
