#pragma once
// Kepler Exo: the three-band EQ of the effect chain (FL Studio 2026, Plugins/Fruity/Generators/Kepler Exo/engine_x64.dll),
// re-created clean-room from the observed behaviour of the engine (REA round 167d; decompile + disassembly of the DLL,
// checked bit for bit against the DLL's own functions, see the verification notes at the end of this file).
//
//   global parameters (setter FUN_1800198a0):  0x64 EQ on (x >= 0.5)
//                                              0x65..0x67 band 1..3 Q     = powf (13, powf (x, 1.28)) * 0.28
//                                              0x68..0x6a band 1..3 gain  = x * 32 - 16 (dB)
//                                              0x6b..0x6d band 1..3 freq  = (float) (pow (2, 10.036174 x) * 20) (Hz)
//   bands (container ctor FUN_180006a50): band 1 = peak (designer kind 5), band 2 = low shelf (kind 6), band 3 = high
//   shelf (kind 8); the band setter FUN_180006950 maps the band type 0/1/2 to the designer kinds 5/6/8 and calls the
//   double-precision designer FUN_180004320 with (freq * 2/fs, Q, gain dB). The designer is a "matched" second-order
//   design (the analogue magnitude response matched at DC, at a mid frequency and near Nyquist); its coefficients are
//   rounded to float. Rate set-up FUN_180007500: per band, if fs changed, 1/fs and 2/fs (float) and a redesign.
//   Render (FUN_18001b7d0, 0x18001c7a3): if on, for each band, per channel, the direct-form-I biquad
//   y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2 in float, in that order.
//
// The C-runtime routines the designer and the parameter laws call are a template parameter (Crt):
//   powf (float, float)    FUN_1800c1630  (Q law)
//   pow2 (double)          FUN_1800bff90 with base 2 (frequency law)
//   pow (double, double)   FUN_1800bff90  (10^(dB/20) and one power inside the designer)
//   sin, cos, cosh, exp, log (double)  FUN_1800c2500, FUN_1800bdce0, FUN_1800be760, FUN_1800befb0, FUN_1800bf660
// see ExoEqStdCrt below for the default and the notes at the end for how exact each is.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include "core/IldspMath.h"

#pragma clang fp contract(off)

namespace lynx
{
namespace kepler_exo
{

// default runtime: the library routines evaluated in round-to-nearest (the engine's routines are correctly rounded or
// within one unit in the last place, which the float coefficients do not see), powf as ildsp::powf_ (same MSVC
// routine, follows the current rounding mode like the DLL's)
struct ExoEqStdCrt
{
    struct Nearest
    {
        unsigned saved;
        Nearest() noexcept : saved (_mm_getcsr()) { _mm_setcsr (saved & ~0x6000u); }
        ~Nearest() { _mm_setcsr (saved); }
    };
    static float powf (float a, float b) noexcept { return ildsp::powf_ (a, b); }
    static double pow2 (double y) noexcept { Nearest n; return std::exp2 (y); }
    static double pow (double a, double b) noexcept { Nearest n; return std::pow (a, b); }
    static double sin (double x) noexcept { Nearest n; return std::sin (x); }
    static double cos (double x) noexcept { Nearest n; return std::cos (x); }
    static double cosh (double x) noexcept { Nearest n; return std::cosh (x); }
    static double exp (double x) noexcept { Nearest n; return std::exp (x); }
    static double log (double x) noexcept { Nearest n; return std::log (x); }
};

namespace eqdetail
{
// SSE maxsd / minsd: the first operand if it compares greater (smaller), else the second (also when unordered)
inline double maxsd (double a, double b) noexcept { return a > b ? a : b; }
inline double minsd (double a, double b) noexcept { return a < b ? a : b; }
inline double sqrt0 (double v) noexcept { return v > 0.0 ? std::sqrt (v) : 0.0; }

constexpr double hpi = 1.5707963267948966;    // pi / 2
constexpr double k4pi2 = 0.4052847345693511;  // 4 / pi^2

// matched second-order design, FUN_180004320 kinds 5 (peak), 6 (low shelf), 8 (high shelf). w = f / (fs / 2),
// out = { a1, a2, b0, b1, b2 }. Every expression follows the engine's operation order (double precision).
template <class Crt>
void design (int kind, double w, double Q, double gainDb, double out[5]) noexcept
{
    const double G = Crt::pow (10.0, gainDb * 0.05);
    if (kind != 5 && kind != 6 && kind != 8)
        return; // not used by Kepler Exo
    if (std::fabs (G - 1.0) < 0.001)
    {
        out[0] = 0.0, out[1] = 0.0, out[2] = 1.0, out[3] = 0.0, out[4] = 0.0;
        return;
    }
    double A0, B0, A2, B2;
    if (kind == 6)
    {
        const double q2 = Q * Q, G2 = G * G, w2 = w * w, iq2 = 1.0 / q2;
        const double qq = q2 + 0.25;
        if (! (w >= 1.0))
        {
            const double s0 = Crt::sin (w * hpi);
            const double phi0 = s0 * s0;
            const double t = (q2 + 0.5) + sqrt0 (qq);
            const double sb = Crt::sin (((w * Q) / sqrt0 (t)) * hpi);
            const double phiB = sb * sb;
            const double wm1 = (w2 - 1.0) * (w2 - 1.0);
            const double ip = 1.0 / phi0;
            const double Hw = (((w2 * G) * iq2) + wm1) / (((w2 * iq2) / G) + wm1);
            const double c0 = (1.0 - G) / phiB;
            const double g1 = (1.0 - G2) * ip;
            double c4 = (ip * phiB - 2.0) * g1;
            c4 = (c4 + c0) / (G - G2);
            double c9 = (ip - 2.0) * g1 + (1.0 - Hw);
            c9 = ((c9 / (Hw - G2)) - c4) / (1.0 - phiB);
            c4 = c4 - c9 * phiB;
            const double c8 = (1.0 - G2) / (phi0 * phi0) + c9 * G2;
            A0 = (c4 + 1.0) + c9;
            B0 = (((((G2 - 1.0) + G2) - 1.0) * ip + c4 * G2) + 1.0) + c8;
            A2 = c9 * -0.25;
            B2 = c8 * -0.25;
        }
        else
        {
            const double t = (q2 + 0.5) + sqrt0 (qq);
            const double y = Q / sqrt0 (t);
            const double sy = Crt::sin (y * hpi);
            const double y2 = y * y, phi = sy * sy;
            const double iG = 1.0 / G;
            const double wm1 = (w2 - 1.0) * (w2 - 1.0);
            const double wy = (w2 - y2) * (w2 - y2);
            const double yw = y2 * w2;
            const double Hw = (((w2 * G) * iq2) + wm1) / (((w2 * iq2) * iG) + wm1);
            const double Hy = (((yw * G) * iq2) + wy) / (((yw * iq2) * iG) + wy);
            A0 = ((1.0 - phi) * (Hy - 1.0)) / ((Hw - Hy) * phi);
            B0 = Hw * A0;
            A2 = 0.0;
            B2 = 0.0;
        }
        const double a = (sqrt0 (A0) + 1.0) * 0.5;
        const double b = (sqrt0 (B0) + 1.0) * 0.5;
        const double n = 1.0 / (sqrt0 (a * a + A2) + a);
        const double nb = sqrt0 (b * b + B2) + b;
        out[0] = (1.0 - sqrt0 (A0)) * n;
        out[1] = -(n * A2) * n;
        out[2] = nb * n;
        out[3] = (1.0 - sqrt0 (B0)) * n;
        out[4] = -((B2 / nb) * n);
        return;
    }

    // kinds 5 and 8 share the structure; the peak response is expressed with the gain-above-one terms in the numerator,
    // the high shelf with the gain-below-one terms (and G^2 in place of 1/G^2)
    const bool peak = kind == 5;
    const double q2 = Q * Q, iq2 = 1.0 / q2;
    const double m = maxsd (1.0 / G, G);
    double r1 = 0.0, r2 = 0.0;
    if (m > 0.0)
    {
        r1 = std::sqrt (m);
        r2 = sqrt0 (r1);
    }
    const double wr = peak ? r2 * w : w / r2;
    const double gmax = maxsd (G, 1.0), igmin = 1.0 / minsd (G, 1.0);
    double N, D, gg; // response numerator / denominator scale (at w = 0: N, D) and gain factor
    if (peak)
    {
        const double u = (wr * wr) / (r1 * r1);
        N = u * gmax;
        D = u * igmin;
        gg = 1.0 / (G * G);
    }
    else
    {
        const double u = wr * wr;
        N = u * igmin;
        D = u * gmax;
        gg = G * G;
    }
    // analogue magnitude-squared at a normalised frequency x^2 (operation order of the engine)
    auto H = [&] (double x2) noexcept {
        return ((((N - x2) * (N - x2)) * q2 + N * x2) * gg) / (((D - x2) * (D - x2)) * q2 + D * x2);
    };
    auto Hr = [&] (double x2) noexcept { // the same with the products written x2 * N, x2 * D
        return ((((N - x2) * (N - x2)) * q2 + x2 * N) * gg) / (((D - x2) * (D - x2)) * q2 + x2 * D);
    };
    const double H0 = ((((N - 1.0) * (N - 1.0)) * q2 + N) * gg) / (((D - 1.0) * (D - 1.0)) * q2 + D);

    if (q2 > 0.6)
    {
        const double k = 1.0 / q2 - 2.0;
        double z = -((r1 * r1 + 1.0) / (k * r1));
        z = z - sqrt0 (z * z - 1.0);
        const double v = peak ? (1.0 / r1) * z : r1 * z;
        const double wv = sqrt0 (v) * wr;
        const double sv = Crt::sin (wv * hpi);
        const double phiV = sv * sv;
        const double Hv = H (wv * wv);
        double e1, e2, c9, c8; // A1-, A2-, B1-, B2-like terms before the final mapping
        if (! (wv >= z))
        {
            const double t = wv / z;
            const double st = Crt::sin (t * hpi);
            const double t2 = t * t, phiT = st * st;
            const double Ht = Hr (t2);
            const double hv1 = Hv - 1.0, ht1 = Ht - 1.0, d = Ht - Hv;
            const double phiV2 = phiV * phiV;
            const double dp = (d * phiV) * phiT;
            c9 = (hv1 + hv1) / phiV;
            e1 = ((ht1 * phiV - hv1 * phiT) * -2.0) / dp;
            c9 = c9 + Hv * e1;
            e2 = (ht1 * phiV2 - (phiT * phiT) * hv1) / (dp * (phiV * phiT));
            c8 = Hv * e2 - hv1 / phiV2;
        }
        else if (! (wv >= 1.0))
        {
            const double z2 = z * z;
            const double vz = peak ? v : z / r1;
            const double vN = peak ? vz * gmax : vz * igmin;
            const double vD = peak ? vz * igmin : vz * gmax;
            const double Hz = ((((vN - z2) * (vN - z2)) * q2 + z2 * vN) * gg) / (((vD - z2) * (vD - z2)) * q2 + z2 * vD);
            const double Hn = ((((vN - 1.0) * (vN - 1.0)) * q2 + vN) * gg) / (((vD - 1.0) * (vD - 1.0)) * q2 + vD);
            const double sz = Crt::sin (z * hpi);
            double t1 = 1.0 / (sz * sz);
            t1 = t1 + t1;
            const double x2 = (((2.0 - t1) * (Hn - 1.0)) * (Hz - 1.0)) / (Hn - Hz);
            const double num = (z * sqrt0 (G)) * x2;
            const double den = (k * k4pi2) * (peak ? G - 1.0 : 1.0 - G);
            const double L = Crt::log (num / den) / Crt::log (z);
            const double p = Crt::pow (wv, L);
            const double c15 = (((iq2 - 2.0) * k4pi2) * (N - D)) / (D * N);
            const double pc = p * c15;
            const double hv1 = Hv - 1.0;
            e1 = -2.0 / phiV - pc / hv1;
            const double hq = hv1 / (phiV * phiV);
            c9 = e1 - pc;
            e2 = ((((hq + H0) - 1.0) - c9) + H0 * e1) / (Hv - H0);
            c8 = Hv * e2 - hq;
        }
        else
        {
            const double c13 = (k4pi2 / D - k4pi2 / N) * (iq2 - 2.0);
            const double nH0 = (((N - 1.0) * (N - 1.0)) * q2 + N) * gg;
            const double i7 = 1.0 / (((D - 1.0) * (D - 1.0)) * q2 + D);
            const double wm = minsd (peak ? (1.0 / r1) * wr : wr, 0.5);
            const double sm = Crt::sin (wm * hpi);
            const double h0 = i7 * nH0;
            const double phiM = sm * sm, wm2 = wm * wm;
            const double numM = (((N - wm2) * (N - wm2)) * q2 + N * wm2) * gg;
            const double i2 = 1.0 / (((D - wm2) * (D - wm2)) * q2 + D * wm2);
            const double Hm = i2 * numM;
            const double x3 = 1.0 / (h0 - 1.0);
            const double h0r = h0 * x3;
            const double x1 = 1.0 / (Hm - 1.0);
            e2 = (((Hm * x1 - h0r) * c13 + 1.0 / phiM) - 1.0) / (h0r - Hm * (phiM * x1));
            c9 = -(((c13 + e2) * nH0) * (x3 * i7)) - 1.0;
            e1 = c13 + c9;
            c8 = 0.0;
        }
        A0 = (e1 + 1.0) + e2;
        B0 = (c9 + 1.0) + c8;
        A2 = e2 * -0.25;
        B2 = c8 * -0.25;
    }
    else
    {
        const double sr = sqrt0 (r1);
        const double wl = minsd (peak ? wr / sr : sr * wr, 0.2);
        const double sl = Crt::sin (wl * hpi);
        const double phiL = sl * sl;
        const double H1 = H (wl * wl);
        const double y = sqrt0 (D) * hpi;
        const double e = Crt::exp ((-1.0 / Q) * y);
        double c;
        if (Q > 0.5)
            c = Crt::cos (y * sqrt0 (4.0 - iq2));
        else
            c = Crt::cosh (sqrt0 (iq2 - 4.0) * y);
        const double a1 = (c * e) * -2.0;
        const double e2 = e * e;
        const double A2c = e2 * -4.0;
        double p13 = (1.0 - a1) + e2;
        p13 = p13 * p13;
        double p0 = (a1 + 1.0) + e2;
        p0 = p0 * p0;
        const double h0p = H0 * p13;
        const double i2 = 1.0 / p0;
        double x8 = (p13 * H1 - h0p) / (1.0 - phiL);
        x8 = (x8 + (H1 - 1.0) / (i2 * phiL)) * 0.25 + A2c * H1;
        A0 = p13 * i2;
        B0 = h0p * i2;
        A2 = A2c * i2;
        B2 = x8 * i2;
    }
    const double sA = sqrt0 (A0), sB = sqrt0 (B0);
    const double a = (sA + 1.0) * 0.5;
    const double b = (sB + 1.0) * 0.5;
    const double n = 1.0 / (a + sqrt0 (a * a + A2));
    const double nb = b + sqrt0 (b * b + B2);
    const double gn = peak ? n * G : n;
    out[0] = n * (1.0 - sA);
    out[1] = n * -(n * A2);
    out[2] = gn * nb;
    out[3] = (1.0 - sB) * gn;
    out[4] = -((B2 / nb) * gn);
}
} // namespace eqdetail

template <class Crt = ExoEqStdCrt>
struct EqT
{
    // one band record (engine stride 0x58): coefficients +4..+0x14, type +0x18, gain dB +0x1c, freq Hz +0x20, Q +0x24,
    // state +0x28.. (x1 x2 y1 y2 for L, then R), rate +0x48, 1/fs +0x50, 2/fs +0x54
    struct Band
    {
        float a1 = 0, a2 = 0, b0 = 1, b1 = 0, b2 = 0;
        int type = 0;
        float gainDb = 0, freq = 1000, q = 0;
        std::array<float, 8> s {}; // xL1 xL2 yL1 yL2 xR1 xR2 yR1 yR2
        double rate = 44100.0;
        float invFs = 0x1.7c6f8cp-16f, twoInvFs = 0x1.7c6f8cp-15f; // the ctor's literals 1/44100, 2/44100

        // FUN_180006950
        void set (int t, float f, float qq, float g) noexcept
        {
            type = t, gainDb = g, freq = f, q = qq;
            const int kind = t == 0 ? 5 : t == 1 ? 6 : t == 2 ? 8 : -1;
            if (kind < 0)
                return;
            double c[5];
            eqdetail::design<Crt> (kind, (double) (f * twoInvFs), (double) qq, (double) g, c);
            a1 = (float) c[0], a2 = (float) c[1], b0 = (float) c[2], b1 = (float) c[3], b2 = (float) c[4];
        }
        float tick (float x, int ch) noexcept
        {
            float* st = s.data() + ch * 4;
            const float x1 = st[0];
            float y = b0 * x + b1 * x1;
            y = y + b2 * st[1];
            st[1] = x1;
            const float y1 = st[2];
            y = y - a1 * y1;
            y = y - a2 * st[3];
            st[3] = y1;
            st[2] = y;
            st[0] = x;
            return y;
        }
    };

    bool on = false;            // synth +0x1cb30
    std::array<Band, 3> band;   // synth +0x1cb38, +0x1cb90, +0x1cbe8
    std::array<float, 10> raw {}; // the raw parameter values (synth +0x1cc40.., not used by the DSP)

    EqT() noexcept { construct(); }

    // FUN_180006a50: band types 0/1/2 at 100 / 2000 / 10000 Hz, Q 1, 0 dB (designed at 44100), then the container's
    // own parameter pass with 0.5 for its ten parameters
    void construct() noexcept
    {
        for (auto& b : band)
            b = Band {};
        band[0].type = 0, band[0].freq = 100.0f, band[0].q = 1.0f;
        band[1].type = 1, band[1].freq = 2000.0f, band[1].q = 1.0f;
        band[2].type = 2, band[2].freq = 10000.0f, band[2].q = 1.0f;
        for (auto& b : band)
            b.set (b.type, b.freq, b.q, b.gainDb);
        for (int i = 0; i < 10; ++i)
            setParam (0x64 + i, 0.5f);
        on = false;
    }

    // FUN_180007500: every band whose rate differs is redesigned with 1/fs, 2/fs in float
    void setRate (double fs) noexcept
    {
        for (auto& b : band)
            if (fs != b.rate)
            {
                b.rate = fs;
                const float inv = 1.0f / (float) fs;
                b.invFs = inv;
                b.twoInvFs = inv + inv;
                b.set (b.type, b.freq, b.q, b.gainDb);
            }
    }

    // the engine never clears the EQ state after construction
    void reset() noexcept
    {
        for (auto& b : band)
            b.s = {};
    }

    void setParam (int index, float x) noexcept
    {
        const int i = index - 0x64;
        if (i < 0 || i > 9)
            return;
        raw[(std::size_t) i] = x;
        if (i == 0)
        {
            on = x >= 0.5f;
            return;
        }
        Band& b = band[(std::size_t) ((i - 1) % 3)];
        switch ((i - 1) / 3)
        {
            case 0: b.set (b.type, b.freq, Crt::powf (13.0f, Crt::powf (x, 1.28f)) * 0.28f, b.gainDb); break;
            case 1: b.set (b.type, b.freq, b.q, x * 32.0f - 16.0f); break;
            default: b.set (b.type, (float) (Crt::pow2 ((double) (10.036173820495605f * x)) * 20.0), b.q, b.gainDb); break;
        }
    }

    void process (float& L, float& R) noexcept
    {
        if (! on)
            return;
        for (auto& b : band)
        {
            L = b.tick (L, 0);
            R = b.tick (R, 1);
        }
    }
};
using Eq = EqT<>;

// ---------------------------------------------------------------------------------------------- verification notes
// Harnesses (Windows, our own code; the DLL loaded unmodified and its functions called at their RVAs; LYNX side the
// same steps on this header, built with -ffp-contract=off), every float compared bit for bit:
//  * band setter FUN_180006950 on 60000 random records (types 0/1/2, 20..22000 Hz, Q 0.28..3.64, -16..+16 dB incl. 0 dB,
//    2/fs for 44100 and 48000): round-to-nearest 0 mismatches; MXCSR | 0xE040 (render mode) 159 of 300000 floats differ
//    (up to 12 ulp), all from the double routines below.
//  * container: ctor FUN_180006a50, setRate FUN_180007500 (44100 / 48000 / 96000), then 20000 random parameter steps
//    through FUN_180006da0 (Q, gain, frequency of the three bands, incl. 0 and 1), all coefficients, type, gain,
//    frequency and Q of the three bands after each step: nearest 0 mismatches at all three rates; RZ 215 / 66 / 0 floats
//    of 580054 differ (again only through the double routines).
//  * powf FUN_1800c1630 vs ildsp::powf_ on 100000 (x, 1.28) and 100000 (13, x) pairs: 0 mismatches in both modes.
//  * the render biquad (inline in the synth render at 0x18001c7a3, not callable on its own) was transcribed from the
//    disassembly in operation order; it is plain float arithmetic.
// Not exact: the engine's double sin / cos / cosh / exp / log / pow differ from correctly rounded ones when called with
// MXCSR in round-toward-zero (exp / log / pow by one ulp on ~50 % of inputs, cosh up to 3 ulp, sin / cos with a range
// reduction error near 1e-11 relative). The default ExoEqStdCrt evaluates them in round-to-nearest, where the engine's
// results match the correctly rounded ones (all within one ulp, which the float coefficients absorbed in every test).
// Parameter changes in the engine normally run outside the render (nearest); a host that applies them inside the
// render (RZ) needs a Crt with the engine's RZ behaviour for a bit-exact match (Kepler.cpp's pow re-creation covers pow /
// pow2; sin / cos / cosh / exp / log would need the same treatment).

} // namespace kepler_exo
} // namespace lynx
