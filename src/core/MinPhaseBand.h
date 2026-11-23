#pragma once

// Round 147 (Pro-MB parity, REA_rodada147 §1): Prism Multiband's measured Dynamic phase. Measured black-box on the
// official Pro-MB trial: a band's gain g acts as a MINIMUM-PHASE filter whose magnitude is the linear-phase one,
// |F| = 1 + (g − 1)·A(ω), A = the band's crossover magnitudes (|low-pass of its upper crossover| · |high-pass of its
// lower one|), and the bands are in CASCADE (two adjacent bands at −12 dB give −8.2 dB at their shared crossover,
// the product of the two bands' −4.1 dB, not a flat −12). So at rest (g = 1) the effect is exactly transparent,
// and only a working band shifts the phase.
//
// Construction (LYNX's own, textbook spectral factorisation): with Linkwitz-Riley crossovers of order 2n,
// |L| = 1 / (1 + x_h), |H| = x_l / (1 + x_l), x = (Ω / Ωc)^{2n}, Ω = tan (πf / fs) (the bilinear
// transform's prewarped axis, so the digital magnitudes are exactly those of LYNX's digital crossovers). Then
//   |F| = Num (u) / ((1 + x_l)(1 + x_h)),  u = Ω^{2n},  Num = (1 + x_l)(1 + x_h) + (g − 1)·x_l   (middle band)
// and F = S², |S|² = |F|: S's poles are the two Butterworth sets of the crossovers, its zeros the left-half-plane
// roots of Num (−s²): Num is a quadratic in u, so each of its roots t gives the 2n solutions of s^{2n} = (−1)^n u_t
// on a circle — no general root finder. Low band: Num = g + x_h (a Butterworth set of radius g^{1/2n}·Ω_h); high
// band: Num = 1 + g·x_l (radius Ω_l·g^{−1/2n}). The analog sections go through the bilinear transform, the DC gain
// is set exactly (g for the low band, 1 for the others), and S is run twice.

#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <complex>
#include <vector>

namespace lynx::minphase
{
inline constexpr double pi = 3.14159265358979323846;

// the prewarped axis of a frequency at a sample rate (capped like LrSplit: 0.45·fs)
inline double warp (double hz, double sampleRate) noexcept
{
    return std::tan (pi * std::min (hz, sampleRate * 0.45) / sampleRate);
}

// the band's crossover magnitude A at Ω (wl ≤ 0: no lower crossover, wh ≤ 0: no upper one), orders 2·nl / 2·nh
inline double bandShare (double wl, double wh, int nl, int nh, double w) noexcept
{
    double a = 1.0;
    if (wh > 0.0)
        a /= 1.0 + std::pow (w / wh, 2.0 * nh);
    if (wl > 0.0)
    {
        const auto x = std::pow (w / wl, 2.0 * nl);
        a *= x / (1.0 + x);
    }
    return a;
}
inline double bandShare (double wl, double wh, int n, double w) noexcept { return bandShare (wl, wh, n, n, w); }

// the magnitude of one band's filter at Ω: 1 + (g − 1)·A
inline double bandMagnitude (double wl, double wh, int nl, int nh, double g, double w) noexcept
{
    return 1.0 + (g - 1.0) * bandShare (wl, wh, nl, nh, w);
}
inline double bandMagnitude (double wl, double wh, int n, double g, double w) noexcept { return bandMagnitude (wl, wh, n, n, g, w); }

// ---- round 150: Pro-MB's other slopes in Dynamic phase (REA_rodada150 §1), measured black-box on the trial ----
// A side's slope (6 … 48 dB/oct, continuous in the reference) runs as one of five shapes: m = 1 (6 dB/oct, below 9),
// 2 (12, 9 … < 18), 4 (24, 18 … < 30), 6 (36, 30 … < 42), 8 (48) — the exponent of x in the linear-phase shape
// 1 / (1 + x^m) (every case matched within 0.01 dB). m = 2n runs the construction above (LR order 2n); m = 1 is a
// first-order design of its own [EXEC]:
//  - an edge band: the first-order shelf (s + g·ω)/(s + ω) (low band) or (g·s + ω)/(s + ω) (high band), its pole AT
//    the crossover (exact);
//  - a middle band with two 6 dB/oct sides: two first-order shelves at ω_l / k and k·ω_h with gain g, i.e. the bell
//    (s² + a_n·s + ω0²)/(s² + a_d·s + ω0²), ω0² = ω_l·ω_h, whose centre gain a_n / a_d is the linear-phase shape's
//    T = 1 + (g − 1)·(√r / (1 + √r))², r = ω_h / ω_l: with u = k·√r, u² = (1 − T·g) / (g·(T − g)), a_d = ω0·(u + 1/u),
//    a_n = ω0·(g·u + 1/(g·u)); in a cut a_n stays ≤ 2.345·ω0 (a_d = a_n / T then) — within 0.02 dB of 60 cases;
//  - a middle band with one 6 dB/oct side: the first-order shelf at its corner times the other side's Butterworth
//    shelf squared, both of gain G, the corners moved out by k6 / kn; for boosts G = g and no move (exact), for cuts
//    G = g^e, e / k6 / kn interpolated in the measured table (ProMbSixDb.inc: levels −30 … 0 dB, r = 2 … 34).
inline int dynamicSideCode (double dbOct) noexcept
{
    return dbOct < 9.0 ? 1 : (dbOct < 18.0 ? 2 : (dbOct < 30.0 ? 4 : (dbOct < 42.0 ? 6 : 8)));
}

namespace detail
{
#include "core/ProMbSixDb.inc"
// e, k6, kn for a cut of gDb (≤ 0) on a band of edge ratio r, the other side of order n (1 … 4)
inline void sixParams (double gDb, double r, int n, double& e, double& k6, double& kn) noexcept
{
    e = k6 = kn = 1.0;
    if (gDb >= 0.0)
        return;
    const auto lv = std::clamp ((gDb + 30.0) / 2.0, 0.0, 14.0 + 1.0); // index in −30, −28 … −2, 0
    const int li = std::min ((int) lv, 14);
    const auto lf = lv - li;
    const auto lr = std::clamp (std::log (std::max (r, 1.0)), std::log (sixRatio[0]), std::log (sixRatio[3]));
    int ri = 0;
    while (ri < 2 && lr > std::log (sixRatio[ri + 1]))
        ++ri;
    const auto rf = (lr - std::log (sixRatio[ri])) / (std::log (sixRatio[ri + 1]) - std::log (sixRatio[ri]));
    const auto& t = sixTable[std::clamp (n, 1, 4) - 1];
    double v[3];
    for (int c = 0; c < 3; ++c)
    {
        const auto a = t[ri][li][c] + (t[ri][li + 1][c] - t[ri][li][c]) * lf;
        const auto b = t[ri + 1][li][c] + (t[ri + 1][li + 1][c] - t[ri + 1][li][c]) * lf;
        v[c] = a + (b - a) * rf;
    }
    e = v[0];
    k6 = v[1];
    kn = v[2];
}

#include "core/ProMbNarrow.inc"
} // namespace detail

// round 152 [EXEC] (REA_rodada152 §2): Pro-MB's Dynamic phase makes a middle band with even-order sides wider and
// moves its gain (narrow / deep cuts most: 1.7 dB at the edges of a 24 dB/oct band 300 – 900 Hz at −30 dB): the
// band becomes the linear shape 1 + (G − 1)·A at ω_l / k, ω_h · k. k and G from the measured table (trilinear in
// slope, gain dB and log r; beyond r = 80 the change fades out by r = 320 [model]; mixed slopes use their mean [model])
inline void narrowAdjust (double& wl, double& wh, int ml, int mh, double& g) noexcept
{
    using namespace detail;
    if (wl <= 0.0 || wh <= 0.0 || ml < 2 || mh < 2 || g <= 0.0)
        return;
    const auto slope = std::clamp (3.0 * (ml + mh), 12.0, 48.0);
    const auto sf = (slope - 12.0) / 12.0;
    const int si = std::min ((int) sf, narrowSlopes - 2);
    const auto st = sf - si;
    const auto gd = std::clamp (20.0 * std::log10 (g), -30.0, 30.0);
    int gi = 0;
    while (gi < narrowGains - 2 && gd > narrowGainAxis[gi + 1])
        ++gi;
    const auto gt = (gd - narrowGainAxis[gi]) / (narrowGainAxis[gi + 1] - narrowGainAxis[gi]);
    const auto lr = std::log (std::max (wh / wl, narrowRatioAxis[0]));
    int ri = 0;
    while (ri < narrowRatios - 2 && lr > std::log (narrowRatioAxis[ri + 1]))
        ++ri;
    const auto rt = std::min (1.0, (lr - std::log (narrowRatioAxis[ri])) / (std::log (narrowRatioAxis[ri + 1]) - std::log (narrowRatioAxis[ri])));
    const auto fade = 1.0 - std::clamp ((lr - std::log (narrowRatioAxis[narrowRatios - 1])) / std::log (4.0), 0.0, 1.0);
    const auto look = [&] (const float (&t)[narrowSlopes][narrowGains][narrowRatios]) noexcept {
        double v = 0.0;
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
                for (int c = 0; c < 2; ++c)
                    v += (a ? st : 1.0 - st) * (b ? gt : 1.0 - gt) * (c ? rt : 1.0 - rt) * (double) t[si + a][gi + b][ri + c];
        return v;
    };
    const auto k = 1.0 + (look (narrowK) - 1.0) * fade;
    const auto dg = look (narrowDg) * fade;
    wl /= k;
    wh *= k;
    g *= std::pow (10.0, dg / 20.0);
}

// the analog model of a band with a 6 dB/oct side (m = 1 on one side or both; the other m = 2n or absent), as poles,
// zeros and a high-frequency gain; also used for the editor's curve
struct SixDbModel
{
    std::array<std::complex<double>, 10> zeros {}, poles {};
    int nz = 0, np = 0;
    double k = 1.0;        // the leading coefficient (the gain at high frequencies)
    double dcTarget = 1.0; // the band's gain at DC (g for a low edge band, else 1)

    void addButterworth (std::array<std::complex<double>, 10>& out, int& count, double radius, int order, int times) noexcept
    {
        for (int t = 0; t < times; ++t)
            for (int j = 0; j < order; ++j)
                out[(std::size_t) count++] = std::polar (radius, pi * (2.0 * j + order + 1.0) / (2.0 * order));
    }
    void addQuadratic (std::array<std::complex<double>, 10>& out, int& count, double a, double c) noexcept // s² + a·s + c
    {
        const auto d = a * a - 4.0 * c;
        if (d >= 0.0)
        {
            const auto q = -0.5 * (a + std::sqrt (d));
            out[(std::size_t) count++] = { q, 0.0 };
            out[(std::size_t) count++] = { c / q, 0.0 };
        }
        else
        {
            out[(std::size_t) count++] = { -0.5 * a, 0.5 * std::sqrt (-d) };
            out[(std::size_t) count++] = { -0.5 * a, -0.5 * std::sqrt (-d) };
        }
    }

    // wl / wh ≤ 0: absent; ml / mh: the side codes (1 or 2n); at least one side is 1
    void design (double wl, double wh, int ml, int mh, double g) noexcept
    {
        nz = np = 0;
        k = 1.0;
        dcTarget = wl <= 0.0 ? g : 1.0;
        if (wl <= 0.0) // low edge band, 6 dB/oct: (s + g·ω)/(s + ω)
        {
            zeros[(std::size_t) nz++] = { -g * wh, 0.0 };
            poles[(std::size_t) np++] = { -wh, 0.0 };
            return;
        }
        if (wh <= 0.0) // high edge band: (g·s + ω)/(s + ω)
        {
            k = g;
            zeros[(std::size_t) nz++] = { -wl / g, 0.0 };
            poles[(std::size_t) np++] = { -wl, 0.0 };
            return;
        }
        const auto r = wh / wl;
        if (ml == 1 && mh == 1) // the bell
        {
            const auto w0 = std::sqrt (wl * wh);
            const auto sr = std::sqrt (r);
            const auto share = (sr / (1.0 + sr)) * (sr / (1.0 + sr));
            const auto T = 1.0 + (g - 1.0) * share;
            double ad = 2.0 * w0, an = 2.0 * w0;
            if (std::abs (g - 1.0) > 1e-9)
            {
                const auto u = std::sqrt (std::max (1e-12, (1.0 - T * g) / (g * (T - g))));
                ad = w0 * (u + 1.0 / u);
                an = w0 * (g * u + 1.0 / (g * u));
                if (g < 1.0 && an > 2.345 * w0)
                {
                    an = 2.345 * w0;
                    ad = an / T;
                }
            }
            addQuadratic (zeros, nz, an, w0 * w0);
            addQuadratic (poles, np, ad, w0 * w0);
            return;
        }
        // one 6 dB/oct side, the other of order n (m = 2n)
        const bool sixLow = ml == 1;
        const int n = std::clamp ((sixLow ? mh : ml) / 2, 1, 4);
        double e = 1.0, k6 = 1.0, kn = 1.0;
        if (g < 1.0)
            detail::sixParams (20.0 * std::log10 (std::max (g, 1e-9)), r, n, e, k6, kn);
        const auto G = std::pow (std::max (g, 1e-9), e);
        if (sixLow)
        {
            const auto c = wl / k6;               // (G·s + c)/(s + c)
            k = G;
            zeros[(std::size_t) nz++] = { -c / G, 0.0 };
            poles[(std::size_t) np++] = { -c, 0.0 };
            const auto rp = wh * kn;              // Butterworth shelf squared: DC G, HF 1
            addButterworth (zeros, nz, rp * std::pow (G, 0.5 / n), n, 2);
            addButterworth (poles, np, rp, n, 2);
            k /= G;
        }
        else
        {
            const auto rp = wl / kn;              // squared Butterworth shelf: DC 1, HF G
            addButterworth (zeros, nz, rp * std::pow (G, -0.5 / n), n, 2);
            addButterworth (poles, np, rp, n, 2);
            k = G;
            const auto c = wh * k6;               // (s + G·c)/(s + c): DC G, HF 1
            zeros[(std::size_t) nz++] = { -G * c, 0.0 };
            poles[(std::size_t) np++] = { -c, 0.0 };
            k /= G;
        }
    }

    double magnitude (double w) const noexcept
    {
        const std::complex<double> s { 0.0, w };
        std::complex<double> h { k, 0.0 };
        for (int i = 0; i < nz; ++i)
            h *= s - zeros[(std::size_t) i];
        for (int i = 0; i < np; ++i)
            h /= s - poles[(std::size_t) i];
        return std::abs (h);
    }
};

// round 150: one band's magnitude for any side codes (m = 1 … 8): the formula above for 2n / 2n, the first-order
// models for the 6 dB/oct sides
inline double bandMagnitudeM (double wl, double wh, int ml, int mh, double g, double w, bool narrow152 = false) noexcept
{
    const bool lowSix = wl > 0.0 && ml == 1, highSix = wh > 0.0 && mh == 1;
    if (! lowSix && ! highSix)
    {
        if (narrow152)
            narrowAdjust (wl, wh, ml, mh, g);
        return bandMagnitude (wl, wh, std::max (1, ml / 2), std::max (1, mh / 2), g, w);
    }
    SixDbModel m;
    m.design (wl, wh, ml, mh, g);
    return m.magnitude (w);
}

class Band
{
public:
    static constexpr int maxSections = 8; // S: ≤ 2n = 8 roots → ≤ 4 sections, run twice

    // design for gain g (linear) between Ω_l and Ω_h (≤ 0 = absent), crossovers of order 2n (n = 1, 2, 4)
    void design (double wl, double wh, int n, double g) noexcept { design (wl, wh, n, n, g); }

    // round 147: the band's two crossovers may have different orders 2·nl (lower) and 2·nh (upper)
    void design (double wl, double wh, int nl, int nh, double g) noexcept
    {
        const int n = wl <= 0.0 ? nh : nl; // the edge bands have one crossover
        gain = 1.0;
        count = 0;
        if (wl <= 0.0 && wh <= 0.0)
        {
            gain = g; // a single band: a plain gain
            return;
        }
        std::array<std::complex<double>, 8> zeros {}, poles {};
        int nz = 0, np = 0;
        const auto butterworth = [&] (double radius, std::array<std::complex<double>, 8>& out, int& k0, int order) {
            for (int k = 0; k < order; ++k)
                out[(std::size_t) k0++] = std::polar (radius, pi * (2.0 * k + order + 1.0) / (2.0 * order));
        };
        // round 190px (perf 7): the poles, and so their sections, depend on the crossovers and their orders only, not on
        // g: Prism's dynamic bands design again at every gain step, so they are worked out once per (wl, wh, nl, nh) and
        // kept (the same doubles: the comparison is exact, and the sections come from the same roots)
        const bool polesKept = poleKey[0] == wl && poleKey[1] == wh && poleOrders[0] == nl && poleOrders[1] == nh;
        if (! polesKept)
        {
            if (wh > 0.0)
                butterworth (wh, poles, np, nh);
            if (wl > 0.0)
                butterworth (wl, poles, np, nl);
        }
        if (wl > 0.0 && wh > 0.0 && nl != nh)
            mixedZeros (wl, wh, nl, nh, g, zeros, nz);
        else if (wl <= 0.0)
            butterworth (wh * std::pow (g, 0.5 / n), zeros, nz, n);        // Num = g + x_h
        else if (wh <= 0.0)
            butterworth (wl * std::pow (g, -0.5 / n), zeros, nz, n);       // Num = 1 + g·x_l
        else
        {
            // ρ t² + (g + ρ) t + 1 = 0, t = x_l, ρ = (Ω_l / Ω_h)^{2n}; the numerically stable quadratic roots
            const auto rho = std::pow (wl / wh, 2.0 * n);
            const std::complex<double> b { g + rho, 0.0 };
            const auto sq = std::sqrt (std::complex<double> (b.real() * b.real() - 4.0 * rho, 0.0));
            const auto q = -0.5 * (b + sq); // b > 0, so b + √D never cancels
            const std::complex<double> t[2] { q / rho, 1.0 / q };
            for (const auto& tr : t)
            {
                // s^{2n} = (−1)^n · t · Ω_l^{2n}: 2n solutions on a circle, the left-half-plane ones kept
                const auto radius = wl * std::pow (std::abs (tr), 0.5 / n);
                const auto phase = std::arg (tr) + (n % 2 != 0 ? pi : 0.0);
                for (int k = 0; k < 2 * n && nz < 8; ++k)
                {
                    const auto s = std::polar (radius, (phase + 2.0 * pi * k) / (2.0 * n));
                    if (s.real() < 0.0)
                        zeros[(std::size_t) nz++] = s;
                }
            }
        }
        // sections: conjugate pairs, then the real roots two by two, a last real root alone
        std::array<Analog, 4> num {}, den {};
        const int sn = sections (zeros.data(), nz, num);
        int sd = 0;
        if (polesKept)
        {
            den = keptDen;
            sd = keptSections;
        }
        else
        {
            sd = sections (poles.data(), np, den);
            keptDen = den;
            keptSections = sd;
            poleKey = { wl, wh };
            poleOrders = { nl, nh };
        }
        const int m = std::max (sn, sd);
        for (int k = 0; k < m; ++k)
            c[(std::size_t) k] = bilinear (k < sn ? num[(std::size_t) k] : Analog {}, k < sd ? den[(std::size_t) k] : Analog {});
        // the DC gain: g for the low band (no lower crossover), 1 otherwise; S carries its square root
        double dc = 1.0;
        for (int k = 0; k < m; ++k)
        {
            const auto& s = c[(std::size_t) k];
            dc *= (s.b0 + s.b1 + s.b2) / (1.0 + s.a1 + s.a2);
        }
        const auto target = std::sqrt (wl <= 0.0 ? g : 1.0);
        const auto k0 = target / dc;
        c[0].b0 *= k0; c[0].b1 *= k0; c[0].b2 *= k0;
        for (int k = 0; k < m; ++k)
            c[(std::size_t) (m + k)] = c[(std::size_t) k]; // F = S²
        count = 2 * m;
    }

    // round 150: any side codes (m = 1 … 8, dynamicSideCode); 2n / 2n is the round-147 design above, sample for sample
    void designM (double wl, double wh, int ml, int mh, double g, bool narrow152 = false) noexcept
    {
        const bool lowSix = wl > 0.0 && ml == 1, highSix = wh > 0.0 && mh == 1;
        if (! lowSix && ! highSix)
        {
            if (narrow152) // round 152: the measured narrow-band law
                narrowAdjust (wl, wh, ml, mh, g);
            design (wl, wh, std::max (1, ml / 2), std::max (1, mh / 2), g);
            return;
        }
        gain = 1.0;
        SixDbModel m;
        m.design (wl, wh, ml, mh, g);
        std::array<Analog, maxSections> num {}, den {};
        const int sn = sections10 (m.zeros.data(), m.nz, num);
        const int sd = sections10 (m.poles.data(), m.np, den);
        const int cnt = std::max (sn, sd);
        for (int i = 0; i < cnt; ++i)
            c[(std::size_t) i] = bilinear (i < sn ? num[(std::size_t) i] : Analog {}, i < sd ? den[(std::size_t) i] : Analog {});
        double dc = 1.0;
        for (int i = 0; i < cnt; ++i)
        {
            const auto& q = c[(std::size_t) i];
            dc *= (q.b0 + q.b1 + q.b2) / (1.0 + q.a1 + q.a2);
        }
        const auto k0 = m.dcTarget / dc;
        c[0].b0 *= k0; c[0].b1 *= k0; c[0].b2 *= k0;
        count = cnt;
    }

    void reset() noexcept
    {
        for (auto& s : state)
            s = {};
    }

    double process (double x) noexcept
    {
        for (int k = 0; k < count; ++k)
        {
            const auto& q = c[(std::size_t) k];
            auto& z = state[(std::size_t) k];
            const auto y = q.b0 * x + z[0];
            z[0] = q.b1 * x - q.a1 * y + z[1];
            z[1] = q.b2 * x - q.a2 * y;
            x = y;
        }
        return x * gain;
    }

    // the designed digital response at ω (radians per sample)
    std::complex<double> response (double w) const noexcept
    {
        std::complex<double> h { gain, 0.0 };
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        for (int k = 0; k < count; ++k)
        {
            const auto& q = c[(std::size_t) k];
            h *= (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
        }
        return h;
    }

private:
    struct Analog { double c2 = 0.0, c1 = 0.0, c0 = 1.0; }; // c2·s² + c1·s + c0 (the default: 1)

    // a middle band whose crossovers have different orders: Num in z = (Ω / Ω_l)² is
    // P (z) = 1 + g·z^nl + ρ·z^nh + ρ·z^(nl+nh), ρ = (Ω_l / Ω_h)^(2·nh), of degree ≤ 8: its roots by Aberth's method,
    // started on the two circles the edge factors give (|z| = g^(−1/nl) and (g / ρ)^(1/nh)); each root z gives the
    // left-half-plane zero s = −Ω_l·√(−z)
    static void mixedZeros (double wl, double wh, int nl, int nh, double g, std::array<std::complex<double>, 8>& zeros, int& nz) noexcept
    {
        using C = std::complex<double>;
        const int d = nl + nh;
        const auto rho = std::pow (wl / wh, 2.0 * nh);
        std::array<double, 9> a {}; // monic coefficients, a[k] for z^k
        a[0] += 1.0;
        a[(std::size_t) nl] += g;
        a[(std::size_t) nh] += rho;
        a[(std::size_t) d] += rho;
        for (auto& x : a)
            x /= rho;
        std::array<C, 8> z {};
        const auto r1 = std::pow (g, -1.0 / nl), r2 = std::pow (g / rho, 1.0 / nh);
        for (int k = 0; k < nl; ++k)
            z[(std::size_t) k] = std::polar (r1, pi * (2.0 * k + 1.0) / nl + 0.31);
        for (int k = 0; k < nh; ++k)
            z[(std::size_t) (nl + k)] = std::polar (r2, pi * (2.0 * k + 1.0) / nh + 0.17);
        for (int it = 0; it < 80; ++it)
        {
            double worst = 0.0;
            for (int i = 0; i < d; ++i)
            {
                C p = 1.0, dp = 0.0; // Horner (monic)
                for (int k = d - 1; k >= 0; --k)
                {
                    dp = dp * z[(std::size_t) i] + p;
                    p = p * z[(std::size_t) i] + a[(std::size_t) k];
                }
                if (std::abs (dp) == 0.0)
                    continue;
                const auto ratio = p / dp;
                C sum = 0.0;
                for (int j = 0; j < d; ++j)
                    if (j != i)
                        sum += 1.0 / (z[(std::size_t) i] - z[(std::size_t) j]);
                const auto step = ratio / (1.0 - ratio * sum);
                z[(std::size_t) i] -= step;
                worst = std::max (worst, std::abs (step) / std::max (1e-300, std::abs (z[(std::size_t) i])));
            }
            if (worst < 1e-14)
                break;
        }
        for (int i = 0; i < d && nz < 8; ++i)
        {
            auto s = -wl * std::sqrt (-z[(std::size_t) i]);
            if (s.real() > 0.0)
                s = -s;
            zeros[(std::size_t) nz++] = s;
        }
    }
    struct Digital { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };

    static int sections (const std::complex<double>* roots, int n, std::array<Analog, 4>& out) noexcept
    {
        int k = 0;
        std::array<double, 8> reals {};
        int nr = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& r = roots[i];
            if (std::abs (r.imag()) > 1e-9 * std::abs (r))
            {
                if (r.imag() > 0.0 && k < 4) // one of each conjugate pair
                    out[(std::size_t) k++] = { 1.0, -2.0 * r.real(), std::norm (r) };
            }
            else
                reals[(std::size_t) nr++] = r.real();
        }
        for (int i = 0; i + 1 < nr && k < 4; i += 2)
            out[(std::size_t) k++] = { 1.0, -(reals[(std::size_t) i] + reals[(std::size_t) (i + 1)]), reals[(std::size_t) i] * reals[(std::size_t) (i + 1)] };
        if (nr % 2 != 0 && k < 4)
            out[(std::size_t) k++] = { 0.0, 1.0, -reals[(std::size_t) (nr - 1)] };
        return k;
    }

    // round 150: the same pairing for up to 10 roots (≤ 5 sections; the 6 dB/oct models)
    static int sections10 (const std::complex<double>* roots, int n, std::array<Analog, maxSections>& out) noexcept
    {
        int k = 0;
        std::array<double, 10> reals {};
        int nr = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& r = roots[i];
            if (std::abs (r.imag()) > 1e-9 * std::abs (r))
            {
                if (r.imag() > 0.0 && k < maxSections)
                    out[(std::size_t) k++] = { 1.0, -2.0 * r.real(), std::norm (r) };
            }
            else
                reals[(std::size_t) nr++] = r.real();
        }
        for (int i = 0; i + 1 < nr && k < maxSections; i += 2)
            out[(std::size_t) k++] = { 1.0, -(reals[(std::size_t) i] + reals[(std::size_t) (i + 1)]), reals[(std::size_t) i] * reals[(std::size_t) (i + 1)] };
        if (nr % 2 != 0 && k < maxSections)
            out[(std::size_t) k++] = { 0.0, 1.0, -reals[(std::size_t) (nr - 1)] };
        return k;
    }

    // s = (1 − z⁻¹) / (1 + z⁻¹) (the axis is already prewarped)
    static Digital bilinear (const Analog& n, const Analog& d) noexcept
    {
        const bool second = n.c2 != 0.0 || d.c2 != 0.0;
        const auto map = [second] (const Analog& a, double& c0, double& c1, double& c2) {
            if (! second) // first order: c1·(1 − z⁻¹) + c0·(1 + z⁻¹)
            {
                c0 = a.c1 + a.c0;
                c1 = a.c0 - a.c1;
                c2 = 0.0;
                return;
            }
            c0 = a.c2 + a.c1 + a.c0;
            c1 = 2.0 * (a.c0 - a.c2);
            c2 = a.c2 - a.c1 + a.c0;
        };
        double n0, n1, n2, d0, d1, d2;
        map (n, n0, n1, n2);
        map (d, d0, d1, d2);
        return { n0 / d0, n1 / d0, n2 / d0, d1 / d0, d2 / d0 };
    }

    std::array<Digital, maxSections> c {};
    std::array<std::array<double, 2>, maxSections> state {};
    int count = 0;
    double gain = 1.0;
    // round 190px: the last design's pole sections and what they were made from (NaN: none yet)
    std::array<double, 2> poleKey { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
    std::array<int, 2> poleOrders { 0, 0 };
    std::array<Analog, 4> keptDen {};
    int keptSections = 0;
};
} // namespace lynx::minphase
