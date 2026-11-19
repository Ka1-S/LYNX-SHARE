#pragma once
// Round 190mt: LYNX's model of a ten-band graphic equaliser (the Waves API-560 stand-in on Facet EQ's hidden API-560 law;
// docs/analise/REA_rodada190mt.md §4). Structure from REA (the reference's DSP read for its structure only): the input
// is zero-stuffed to about 192 kHz, two groups of five bands run as a graphic-EQ network — each band a second-order
// resonator R_k fed by the group's node through one sample of delay, its slider a potentiometer p = 0.5 − g / B that
// sends (1 − p) R_k forward and p R_k back, H = (1 + Σ (1 − p_k) z⁻¹ R_k) / (1 + Σ p_k z⁻¹ R_k), the resonator itself
// loaded by the slider (R_k = a D_k / (N_k + p (1 − p) C a D_k)) — then a linear-phase low pass decimates back; the
// whole path is 65 samples late at every rate and plain delay with every slider at 0. Every constant below is LYNX's
// own fit on our demo-mode impulse responses (eq/lab/g560c.py, gen560.py; −60 … −105 dB per probe); the low pass is
// LYNX's own Kaiser design. Header-only; FacetEq.cpp owns the one instance per Facet.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "core/Api560Law.inc" // api560Group[2][5][6], api560Pot[2], api560Load[2] (generated, LYNX's fits)

namespace lynx::api560
{
constexpr int latency = 65;   // samples at every rate (the reference's)
constexpr int groups = 2, perGroup = 5;
// the bands of each group in the slider order 31 Hz … 16 kHz (0 … 9): group 0 = 63, 250, 1k, 4k, 16k; group 1 = 31, 125,
// 500, 2k, 8k
constexpr int bandOf[groups][perGroup] = { { 1, 3, 5, 7, 9 }, { 0, 2, 4, 6, 8 } };

class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        M = std::max (1, (int) std::lround (192000.0 / sr));
        // the decimating low pass at M·sr: a Kaiser-windowed sinc (β 8), its cut-off at 0.4924 × the base rate and its
        // length (40 base samples each side) fitted to the reference's measured roll-off (−0.15 dB at 22.5 kHz, −1.1 at
        // 23, −5.9 at 23.9 kHz at 48 kHz: 0.03 dB rms; passband within 0.0003 dB); the rest of the latency is a line
        taps.clear();
        if (M > 1)
        {
            const int half = firHalf * M;
            const double fc = 0.4924 / M, beta = 8.0;
            auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
            double sum = 0.0;
            for (int i = -half; i <= half; ++i)
            {
                const double x = (double) i, sinc = i == 0 ? 2.0 * fc : std::sin (2.0 * 3.14159265358979323846 * fc * x) / (3.14159265358979323846 * x);
                const double r = x / half, w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
                taps.push_back (sinc * w);
                sum += sinc * w;
            }
            for (auto& t : taps) t *= (double) M / sum; // unity gain after the zero-stuffing
        }
        firDelay = M > 1 ? firHalf : 0;
        ring.assign ((std::size_t) std::max (2, 2 * (int) taps.size()), V2 {});
        for (auto& d : line) d.assign ((std::size_t) latency + 1, 0.0f);
        designed = false;
        reset();
    }

    void reset() noexcept
    {
        for (auto& g : st) for (auto& s : g) s = {};
        for (auto& v : node) v = V2 {};
        for (auto& g : lfMem) for (auto& v : g) v = V2 {};
        std::fill (ring.begin(), ring.end(), V2 {});
        for (auto& d : line) std::fill (d.begin(), d.end(), 0.0f);
        lp = 0; rw = 0;
    }

    // slider gains (dB, −12 … +12) in the order 31 Hz … 16 kHz
    void set (const double (&gainDb)[10]) noexcept
    {
        bool same = designed;
        for (int i = 0; i < 10; ++i) same = same && gainDb[i] == gains[i];
        if (same) return;
        flat = true;
        for (int i = 0; i < 10; ++i) { gains[i] = gainDb[i]; flat = flat && std::abs (gainDb[i]) < 1e-9; }
        const double fs = sr * M, K = 2.0 * fs;
        for (int gi = 0; gi < groups; ++gi)
            for (int k = 0; k < perGroup; ++k)
            {
                const double* c = api560Group[gi][k];            // ln w, ln Q, ln a, d0, d2, ln (C scale)
                const double w = std::exp (c[0]), Q = std::exp (c[1]), a = std::exp (c[2]), d0 = c[3], d2 = c[4];
                const double C = api560Load[gi] * std::exp (c[5]);
                const double p = std::clamp (0.5 - gains[bandOf[gi][k]] / api560Pot[gi], 1e-4, 1.0 - 1e-4);
                pot[gi][k] = p;
                // R(s') = a (d2 s'² + s' + d0) / (s'² + s'/Q + 1 + q a (d2 s'² + s' + d0)), s' = s / w, q = p (1 − p) C
                const double q = p * (1.0 - p) * C;
                const double n2 = a * d2, n1 = a, n0 = a * d0;
                const double e2 = 1.0 + q * n2, e1 = 1.0 / Q + q * n1, e0 = 1.0 + q * n0;
                const double kk = K / w, k2 = kk * kk;               // s' = kk (1 − z⁻¹) / (1 + z⁻¹)
                const double A0 = e2 * k2 + e1 * kk + e0;
                auto& s = sec[gi][k];
                s.b0 = (n2 * k2 + n1 * kk + n0) / A0; s.b1 = 2.0 * (n0 - n2 * k2) / A0; s.b2 = (n2 * k2 - n1 * kk + n0) / A0;
                s.a1 = 2.0 * (e0 - e2 * k2) / A0; s.a2 = (e2 * k2 - e1 * kk + e0) / A0;
            }
        for (int gi = 0; gi < groups; ++gi) // the per-group first-order low-end stage (s + z0) / (s + p0), if fitted
        {
            lfOn[gi] = api560Lf[gi][0] != 0.0 || api560Lf[gi][1] != 0.0;
            if (! lfOn[gi]) continue;
            const double z0 = std::exp (api560Lf[gi][0]), p0 = std::exp (api560Lf[gi][1]), a0 = K + p0;
            lf[gi][0] = (K + z0) / a0; lf[gi][1] = (z0 - K) / a0; lf[gi][2] = (p0 - K) / a0;
        }
        designed = true;
    }

    void process (float* l, float* r, int n) noexcept
    {
        // every slider at 0: a plain delay of the whole latency (the reference's), else the rest after the low pass
        const int dly = flat ? latency : latency - firDelay;
        int p = lp;
        for (int i = 0; i < n; ++i)
        {
            V2 y = { (double) l[i], (double) r[i] };
            if (! flat)
                y = M > 1 ? oversampled (y) : network (y);
            line[0][(std::size_t) p] = (float) y[0];
            line[1][(std::size_t) p] = (float) y[1];
            int q = p - dly; if (q < 0) q += latency + 1;
            l[i] = line[0][(std::size_t) q];
            r[i] = line[1][(std::size_t) q];
            if (++p > latency) p = 0;
        }
        lp = p;
    }

private:
    using V2 = double __attribute__ ((vector_size (16))); // (left, right)
    struct Sec { double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { V2 s1 {}, s2 {}; };

    // one sample of the network at M·sr, both channels
    V2 network (V2 x) noexcept
    {
        V2 in = x;
        for (int gi = 0; gi < groups; ++gi)
        {
            V2 fwd {}, back {};
            const V2 u0 = node[gi]; // the node one sample ago
            for (int k = 0; k < perGroup; ++k)
            {
                const auto& c = sec[gi][k];
                auto& s = st[gi][k];
                // transposed direct form II
                const V2 y = c.b0 * u0 + s.s1;
                s.s1 = c.b1 * u0 - c.a1 * y + s.s2;
                s.s2 = c.b2 * u0 - c.a2 * y;
                fwd += (1.0 - pot[gi][k]) * y;
                back += pot[gi][k] * y;
            }
            const V2 u = in - back;
            node[gi] = u;
            in = u + fwd;
            if (lfOn[gi])
            {
                const V2 y = lf[gi][0] * in + lf[gi][1] * lfMem[gi][0] - lf[gi][2] * lfMem[gi][1];
                lfMem[gi][0] = in; lfMem[gi][1] = y; in = y;
            }
        }
        return in;
    }

    V2 oversampled (V2 x) noexcept
    {
        // the ring holds every sub-sample twice (at w and w + L) so the last L are always contiguous: [w + 1, w + L]
        V2* b = ring.data();
        const int L = (int) taps.size(), H = (L - 1) / 2;
        V2 acc {};
        for (int m = 0; m < M; ++m)
        {
            const V2 y = network (m == 0 ? x : V2 {});
            if (++rw == L) rw = 0;
            b[rw] = b[rw + L] = y;
            if (m == 0)
            {
                // the low pass's output on the base sample's own sub-sample (the newest): an integer delay of half the
                // taps = firHalf base samples; the taps are symmetric (folded), two partial sums
                const V2* c = b + rw + 1 + H; // the centre of the window
                const double* t = taps.data();
                V2 a0 = t[H] * c[0], a1 {};
                int k = 1;
                for (; k + 1 <= H; k += 2)
                {
                    a0 += t[H - k] * (c[-k] + c[k]);
                    a1 += t[H - k - 1] * (c[-k - 1] + c[k + 1]);
                }
                for (; k <= H; ++k)
                    a0 += t[H - k] * (c[-k] + c[k]);
                acc = a0 + a1;
            }
        }
        return acc;
    }

    double sr = 48000.0;
    static constexpr int firHalf = 40; // base samples (of the 65)
    int M = 4, firDelay = firHalf;
    std::vector<double> taps;
    std::vector<V2> ring;
    std::array<std::vector<float>, 2> line;
    int lp = 0, rw = 0;
    Sec sec[groups][perGroup];
    double pot[groups][perGroup] {};
    State st[groups][perGroup] {};
    V2 node[groups] {};
    double gains[10] {};
    double lf[groups][3] {};
    bool lfOn[groups] {};
    V2 lfMem[groups][2] {};
    bool designed = false, flat = true;
};
} // namespace lynx::api560
