#pragma once
// Kepler Exo: the delay of the effect chain (FL Studio 2026, Plugins/Fruity/Generators/Kepler Exo/engine_x64.dll),
// re-created clean-room from the observed behaviour of the engine (REA round 167d; decompile + disassembly of the DLL,
// checked bit for bit against the DLL's own functions, see the notes at the end of this file).
//
// The engine object at synth+0x1bdf8 is a small wrapper (process FUN_1800107b0, parameter pass FUN_180010650 = the global
// setter's cases 0x6e..0x78) around Image-Line's class ILDelay3 at +0x30 (ctor FUN_180010b30, vtable 0x1800eab38:
// [0] FUN_180012300 LFO rate, [1] FUN_180011ff0 setRate; reset FUN_180011120; sample FUN_180011210).
//   0x6e on (x >= 0.5)                 0x6f tempo sync (x >= 0.5)
//   0x70 input level                    0x71 time: free (float) (x * 0.49 + 0.01) s, synced table[(int) (17 x + 0.5)] * 4
//                                            beats (60 / tempo s per beat)
//   0x72 stereo offset (crossfaded)     0x73 feedback        0x74 filter cutoff     0x75 filter type (int) (3 x + 0.5):
//                                                                                    0 low-pass, 1 high-pass (state
//                                                                                    variable), 2 band-pass (biquad), 3 off
//   0x76 wet balance L/R ((x + 1) / 2)  0x77 cross-feedback of the echo lines    0x78 tone (one-pole LP below 0.5, HP above)
// Time and offset changes are picked up every (int) (fs * 0.002) + 1 samples (89 at 44100; 11 before the first rate
// set-up) and applied with a 1000-sample linear crossfade between two read-head pairs.
// ILDelay3's own LFO (modulation depth fixed at 0 in Exo), its second filter set and its unused buffers are omitted: they
// never reach the output.
//
// C-runtime routines (template parameter Crt): sinf FUN_1800c2a70, tanf FUN_1800c3990, powf FUN_1800c1630 (float), cos
// FUN_1800bdce0 (double, band-pass type only).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include "core/IldspMath.h"

#pragma clang fp contract(off)

namespace lynx
{
namespace kepler_exo
{

// tanf FUN_1800c3990 of the engine's CRT, re-created from its behaviour (constants read from the DLL, .rdata
// 0x1800f13e0..0x1800f1458): the argument is widened to double; |x| < 2^-27 returns x, |x| < 2^-13 x + x^3 / 3, |x| <= pi/4
// a rational approximation r + r^3 P(r^2) / Q(r^2); above that one Cody-Waite step by pi/2 (two-part constant), the
// rational of the remainder and -1/t in odd quadrants. All in the current rounding mode, converted to float at the end.
// The CRT picks an FMA variant at start-up when the CPU has FMA (0x1800c3d00): same steps with fused multiply-adds.
// Arguments of 2^23 and above (never reached by the delay) fall back to std::tan.
inline float exoDelayTanf (float xf) noexcept
{
    if ((ildsp::fbits (xf) & 0x7f800000u) == 0x7f800000u)
        return (float) std::tan ((double) xf);
    const double x = xf;
    const std::uint64_t ax = ildsp::dbits (x) & 0x7fffffffffffffffull;
    const bool fused = ildsp::useFma();
    const auto rat = [fused] (double r) noexcept {
        const double r2 = r * r;
        if (fused)
        {
            const double p = ildsp::fmaD (-0.017203248047148168, r2, 0.3852960712639954);
            const double q = ildsp::fmaD (ildsp::fmaD (0.01844239256901656, r2, -0.5139650547885454), r2, 1.1558882143468838);
            return ildsp::fmaD (r2 * r, p / q, r);
        }
        const double p = -0.017203248047148168 * r2 + 0.3852960712639954;
        const double q = (0.01844239256901656 * r2 + -0.5139650547885454) * r2 + 1.1558882143468838;
        return r + (r2 * r) * (p / q);
    };
    if (ax <= 0x3fe921fb54442d18ull)
    {
        if (ax < 0x3e40000000000000ull)
            return xf;
        if (ax < 0x3f20000000000000ull)
        {
            const double x3 = (x * x) * x;
            return ildsp::d2f (fused ? ildsp::fmaD (x3, 0.3333333333333333, x) : x3 * 0.3333333333333333 + x);
        }
        return ildsp::d2f (rat (x));
    }
    if (ax >= 0x4160000000000000ull)
        return (float) std::tan (x);
    const double a = std::fabs (x);
    int k;
    double r;
    if (fused)
    {
        k = ildsp::cvttD2I (ildsp::fmaD (a, 0.6366197723675814, 0.5));
        r = ildsp::fmaD (-(double) k, 1.5707963267341256, a);
    }
    else
    {
        k = ildsp::cvttD2I (a * 0.6366197723675814 + 0.5);
        r = a - (double) k * 1.5707963267341256;
    }
    r = r - (double) k * 6.077100506506192e-11;
    double t = rat (r);
    if (k & 1)
        t = -1.0 / t;
    if (x < 0.0)
        t = -t;
    return ildsp::d2f (t);
}

struct ExoDelayStdCrt
{
    struct Nearest
    {
        unsigned saved;
        Nearest() noexcept : saved (_mm_getcsr()) { _mm_setcsr (saved & ~0x6000u); }
        ~Nearest() { _mm_setcsr (saved); }
    };
    static float sinf (float x) noexcept { return ildsp::sinf_ (x); }
    static float powf (float a, float b) noexcept { return ildsp::powf_ (a, b); }
    static float tanf (float x) noexcept { return exoDelayTanf (x); }
    static double cos (double x) noexcept { Nearest n; return std::cos (x); }
};

namespace dlydetail
{
inline float maxss (float a, float b) noexcept { return a > b ? a : b; }
inline float minss (float a, float b) noexcept { return a < b ? a : b; }
inline float neg (float v) noexcept { std::uint32_t u; std::memcpy (&u, &v, 4); u ^= 0x80000000u; std::memcpy (&v, &u, 4); return v; }
inline int trunc (float v) noexcept { return (int) (std::int64_t) v; } // cvttss2si r64, low 32 bits used

// filter / tone frequency tables of the engine (.data 0x180254070, 0x180254088)
constexpr float tLow[5] = { 270.0f, 480.0f, 1300.0f, 7000.0f, 22000.0f };
constexpr float tHigh[5] = { 20.0f, 90.0f, 400.0f, 1300.0f, 4200.0f };
// synced time in beats / 4 (.rdata 0x1800d0e40)
constexpr double tSync[18] = { 0.015625, 0.03125, 0.041666666666666664, 0.046875, 0.0625, 0.08333333333333333,
                               0.09375, 0.3333333333333333, 0.125, 0.16666666666666666, 0.1875, 0.25,
                               0.3333333333333333, 0.375, 0.5, 0.6666666666666666, 0.75, 1.0 };

struct Line // power-of-two ring buffer of ILDelay3 (0x40000 floats)
{
    std::vector<float> b;
    std::uint32_t w = 0, mask = 0;
    void alloc (std::uint32_t n) { b.assign (n, 0.0f); mask = n - 1; w = 0; }
    float at (std::uint32_t i) const noexcept { return b[i & mask]; }
    // linear read at a fractional delay p (samples), the engine's two-tap form
    float read (float p) const noexcept
    {
        const int i = trunc (p);
        const float f = p - (float) (std::uint32_t) i;
        const std::uint32_t k = w - (std::uint32_t) i;
        return (1.0f - f) * at (k) + f * at (k - 1u);
    }
    void push (float v) noexcept { b[w] = v; w = (w + 1u) & mask; }
};
} // namespace dlydetail

template <class Crt = ExoDelayStdCrt>
struct DelayT
{
    // ------------------------------------------------------------------------------------------- ILDelay3 (synth+0x1be28)
    struct Core
    {
        double fs = 44100.0;                  // +0x10
        int ftype = 0, fcount = 0;            // +0x30 filter type, +0x34 coefficient-update phase (every 4th sample)
        float drive = 0.1f, makeup = 0x1.40f2a2p+3f; // +0x50, +0x54
        float inGain = 0.5f, res = 0.0f, cutoff = 0.5f; // +0x58, +0x60, +0x64
        std::array<float, 2> time { 0.1f, 0.1f }, off { 0.5f, 0.5f }; // +0x70, +0x78 (read-head pairs A, B)
        float fb = 0.99f, dry = 1.0f;         // +0x80, +0x84
        float mixA = 1.0f, mixB = 0.0f;       // +0x98, +0x9c
        float tone = 0.0f;                    // +0x5c
        dlydetail::Line main, e1, e2, e3, e4; // +0xb8, +0xe8, +0x100, +0x118, +0x130
        // state-variable filter (+0x148)
        float svNl = 0.0f, svLp = 1.0f, svHp = 0.0f, svBp = 0.0f; // +0x148..+0x154
        float svF = -1.0f, svR = -1.0f, svG = 0.0f, svQ = 0.0f;   // +0x164 +0x168 +0x16c +0x170
        std::array<float, 4> svS {};          // +0x174..+0x180
        float svFs = 44100.0f;                // +0x184
        // band-pass biquad (+0x1c8): 1/fs, coefficients b0 a1' b1 b2 a2', state x1 x2 y1 y2 (y = b0 x + .. + a1' y1 + a2' y2)
        float bqInvFs = 0x1.7c6f8cp-16f;
        float bq0 = 0, bqA1 = 0, bq1 = 0, bq2 = 0, bqA2 = 0;
        std::array<float, 4> bqS {};
        // feedback high-pass (+0x298): state y1 x1, coefficients
        float hpY = 0, hpX = 0, hp0 = 0, hp1 = 0, hpA = 0, hpPiFs = 0x1.2acb26p-14f;
        // limiter envelope (+0x30c)
        float env = 0.0f, envAtt = 0x1.554a46p-8f, envRel = 0x1.11ac9p-11f;
        // tone one-poles L (+0x348) and R (+0x380): y1 x1, coefficients b0 b1 a, type, frequency; pi/fs
        float tLy = 0, tLx = 0, tRy = 0, tRx = 0, t0 = 0, t1 = 0, tA = 0;
        int ttype = 0;
        float tfreq = 100.0f, tPiFs = 0x1.2acb26p-14f;
        // read heads (samples) +0x3b8: A, B on the main line; +0x3c0: A, B offset taps; +0x3c8: the unused other side
        std::array<float, 2> head {}, headL {}, headR {};
        float wet = 1.0f;                     // +0x3d0
        // all-pass at +0x3d8 (gain 0 in Exo: a 65-sample delay before the main line)
        int apLen = 65;
        float apG = 0.0f;
        dlydetail::Line ap;
        float xfb = 0.0f;                     // +0x418 echo-line cross-feedback

        // FUN_1800124a0
        void svSet (float f, float r) noexcept
        {
            if (f == svF)
            {
                if (r == svR)
                    return;
            }
            else
            {
                const float g = Crt::sinf (dlydetail::minss ((0.5f / svFs) * f, 0.25f) * 3.1415927410125732f);
                svF = f;
                svG = g + g;
            }
            const float g = svG;
            const float p = Crt::powf (r, 0.25f);
            float d = 1.0f - p;
            svR = r;
            d = d + d;
            float q = 2.0f / g - g * 0.5f;
            q = dlydetail::minss (q, 2.0f);
            svQ = dlydetail::minss (q, d);
        }
        // FUN_180012560
        float svTick (float x) noexcept
        {
            const float v2 = svS[3];
            const float v6 = svG * v2 + svS[1];
            const float v4 = (x - svQ * v2) - v6;
            const float v5 = (v4 * svG + v2) - (svNl * v2) * (v2 * v2);
            float hp = x - v5 * svQ;
            const float v3 = v5 * svG + v6;
            svS[0] = hp;
            hp = hp - v3;
            svS[1] = v3;
            svS[2] = hp;
            const float n2 = (hp * svG + v5) - (svNl * v5) * (v5 * v5);
            svS[3] = n2;
            return (((hp + v4) * svHp + (v3 + v6) * svLp) + (n2 + v5) * svBp) * 0.5f;
        }
        // FUN_180011b10
        void filterUpdate() noexcept
        {
            if (fcount == 0)
            {
                // the LFO terms (+0x1c times +0x3c / +0x40) are zero in Exo
                float c = cutoff;
                c = c >= 0.0f ? dlydetail::minss (c, 1.0f) : 0.0f;
                float r = res;
                r = r >= 0.0f ? dlydetail::minss (r, 1.0f) : 0.0f;
                if (ftype == 2)
                {
                    float u = c * 3.2799179553985596f;
                    const int i = (int) u;
                    u = u - (float) i;
                    float w = (1.0f - u) * dlydetail::tLow[i] + u * dlydetail::tLow[i + 1];
                    w = (w * 6.2829999923706055f) * bqInvFs;
                    const float s = Crt::sinf (w);
                    const float rr = r * 0.8799999952316284f + 0.10000000149011612f;
                    const float alpha = s * (0.125f / (rr * rr));
                    const float n = 1.0f / (alpha + 1.0f);
                    const float b0 = n * alpha;
                    bq0 = b0;
                    bq1 = 0.0f;
                    bq2 = dlydetail::neg (b0);
                    const double cw = Crt::cos ((double) w);
                    bqA2 = dlydetail::neg ((1.0f - alpha) * n);
                    bqA1 = (float) ((cw + cw) * (double) n);
                }
                else if (ftype == 0 || ftype == 1)
                {
                    const float* t = ftype == 1 ? dlydetail::tHigh : dlydetail::tLow;
                    float u = c * 3.999000072479248f;
                    const int i = (int) u;
                    u = u - (float) i;
                    const float hi = u * t[i + 1];
                    const float lo = (1.0f - u) * t[i];
                    if (ftype == 1)
                        svLp = 0.0f, svHp = 1.0f, svBp = 0.0f;
                    else
                        svLp = 1.0f, svHp = 0.0f, svBp = 0.0f;
                    svSet (lo + hi, r * 0.25f);
                }
            }
            fcount = (fcount + 1) & 3;
        }
        // FUN_1800123e0
        float limit (float x) noexcept
        {
            if (drive != 1.0f)
            {
                const float v = drive * x;
                float s;
                if (! (v >= -3.0f))
                    s = -1.0f;
                else if (v > 3.0f)
                    s = 1.0f;
                else
                    s = ((v * v + 27.0f) * v) / ((v * 9.0f) * v + 27.0f);
                x = s * makeup;
            }
            const float a = std::fabs (x);
            const float e = env;
            env = e + (a - e) * (a < e ? envRel : envAtt);
            if (env > 1.0f)
                x = x / env;
            return x;
        }
        // FUN_1800025c0 for a one-pole: coefficients of type ty at frequency f with piFs = (float) (1 / fs) * pi
        void onePole (int ty, float f, float piFs, float& b0, float& b1, float& a) noexcept
        {
            const float t = Crt::tanf (f * piFs);
            const float n = 1.0f / (t + 1.0f);
            if (ty == 0)
                b0 = n * t, b1 = n * t;
            else
                b0 = n, b1 = dlydetail::neg (n);
            a = dlydetail::neg (n * (t - 1.0f));
        }
        // FUN_180011e20
        void setTone (float x) noexcept
        {
            tone = x;
            float u;
            const float* t;
            if (x >= 0.5f)
                u = (x - 0.5f) * 7.999800205230713f, t = dlydetail::tHigh, ttype = 1;
            else
                u = x * 7.999800205230713f, t = dlydetail::tLow, ttype = 0;
            const int i = (int) u;
            u = u - (float) i;
            tfreq = (1.0f - u) * t[i] + u * t[i + 1];
            const float tt = Crt::tanf (tfreq * tPiFs);
            const float n = 1.0f / (tt + 1.0f);
            if (ttype == 0)
                t0 = n * tt, t1 = n * tt;
            else
                t0 = n, t1 = dlydetail::neg (n);
            tA = dlydetail::neg ((tt - 1.0f) * n);
        }
        // read heads of pair j for its time and stereo offset
        void setHead (int j) noexcept
        {
            const float h = off[(std::size_t) j] * 0.5f;
            float a = (float) fs * time[(std::size_t) j] + 1.0f;
            head[(std::size_t) j] = a;
            a = a - 0.9998999834060669f;
            headL[(std::size_t) j] = (1.0f - h) * a + 1.0f;
            headR[(std::size_t) j] = a * h + 1.0f;
        }
        // FUN_180011ff0
        void setRate (double r) noexcept
        {
            fs = r;
            const float oldR = svR, oldF = svF;
            svF = -1.0f, svR = -1.0f;
            svFs = (float) r;
            if (oldR >= 0.0f && oldF >= 0.0f)
                svSet (-1.0f, -1.0f);
            bqInvFs = (float) (1.0 / r);
            // the feedback high-pass is always designed for 44100 Hz: 10 Hz
            volatile double k44 = 44100.0; // evaluated at run time, in the caller's rounding mode
            hpPiFs = (float) (1.0 / k44) * 3.1415927410125732f;
            {
                const float t = Crt::tanf (hpPiFs * 10.0f);
                const float n = 1.0f / (t + 1.0f);
                hp0 = n;
                hp1 = dlydetail::neg (n);
                hpA = dlydetail::neg ((t - 1.0f) * n);
            }
            tPiFs = (float) (1.0 / r) * 3.1415927410125732f;
            onePole (ttype, tfreq, tPiFs, t0, t1, tA);
            setHead (0);
            setHead (1);
            reset();
        }
        // FUN_180011120 (the echo lines e1..e4 and the filter / tone / limiter states are not cleared by the engine)
        void reset() noexcept
        {
            std::fill (main.b.begin(), main.b.end(), 0.0f);
            std::fill (ap.b.begin(), ap.b.end(), 0.0f);
            svS = {};
            hpY = hpX = 0.0f;
        }
        // FUN_180011210 with the crossfade gains gA, gB of the two read-head pairs (A = index 0, B = index 1)
        void tick (float& L, float& R, float gA, float gB) noexcept
        {
            const float norm = dlydetail::maxss (makeup * drive, 1.0f);
            const float in = ((L + R) * 0.5f) * inGain;
            const float fbk = fb / norm;
            filterUpdate();
            // the read heads carry ILDelay3's LFO offset, which is +0 in Exo
            float S = main.read (head[0]) * gA;
            S = S + main.read (head[1]) * gB;
            float T = main.read (headL[0]) * gA;
            T = T + main.read (headL[1]) * gB;
            const float x = S * fbk;
            const float hy = hpA * hpY;
            float y = x * hp0 + hp1 * hpX;
            hpX = x;
            y = y + hy;
            hpY = y;
            y = y + in;
            float f;
            if (ftype < 2)
                f = svTick (y);
            else
            {
                const float y1 = bqS[2], x1 = bqS[0];
                float v = bqA2 * bqS[3] + y1 * bqA1;
                v = v + bq2 * bqS[1];
                bqS[1] = x1;
                bqS[0] = y;
                bqS[3] = y1;
                v = v + (y * bq0 + x1 * bq1);
                bqS[2] = v;
                f = v;
            }
            f = limit (f);
            {
                const float k1 = apG * 0.49900001287460327f;
                const float k2 = (apG * apG) * 0.24900101125240326f;
                const float d = ap.at (ap.w - (std::uint32_t) apLen);
                ap.b[ap.w] = f - d * k1;
                ap.w = (ap.w + 1u) & ap.mask;
                main.push ((1.0f - k2) * d + k1 * f);
            }
            // echo lines: e1 / e2 at 0.63 / 0.76 of the head delays, e3 / e4 at 0.41 / 0.54
            float u = e1.read (head[0] * 0.6299999952316284f) * gA;
            u = u + e1.read (head[1] * 0.6299999952316284f) * gB;
            S = S + u * xfb;
            u = e2.read (head[0] * 0.7599999904632568f) * gA;
            u = u + e2.read (head[1] * 0.7599999904632568f) * gB;
            T = u * xfb + T;
            e1.push (dlydetail::neg (S * fb));
            e2.push (dlydetail::neg (T * fb));
            u = e3.read (head[0] * 0.4099999964237213f) * gA;
            u = u + e3.read (head[1] * 0.4099999964237213f) * gB;
            S = S + (u * xfb) * xfb;
            u = e4.read (head[1] * 0.5400000214576721f) * gB;
            u = u + e4.read (head[0] * 0.5400000214576721f) * gA;
            T = T + (u * xfb) * xfb;
            e3.push (dlydetail::neg (S * fb));
            e4.push (dlydetail::neg (T * fb));
            // tone one-poles and the wet / dry mix
            float yl = S * t0 + t1 * tLx;
            yl = yl + tA * tLy;
            tLx = S;
            tLy = yl;
            float yr = T * t0 + t1 * tRx;
            yr = yr + tA * tRy;
            tRx = T;
            tRy = yr;
            const float l = (yr * mixB + yl * mixA) * wet + dry * L;
            const float r = (yr * mixA + yl * mixB) * wet + dry * R;
            L = l;
            R = r;
        }
    };

    // ------------------------------------------------------------------------------------- wrapper (synth+0x1bdf8)
    std::array<float, 11> raw {};
    float timeSync = 0.5f, timeFree = 0.5f, curTime = 0.0f; // +0x450 +0x454 +0x458
    bool on = true, sync = false, xfading = false;          // +0x45c +0x45d +0x45e
    int active = 0, next = 1, xfLen = 1000, xfCount = 0;    // +0x460 +0x464 +0x468 +0x470
    float xfStep = 0x1.0624dep-10f;                         // +0x46c (0.001)
    float gA = 1.0f, gB = 0.0f, offCur = 0.0f, offTarget = 0.0f; // +0x478 +0x47c +0x480 +0x484
    int upd = 0, updEvery = 10;                             // +0x488 +0x48c
    double tempo = 120.0;                                   // +0x490 host tempo (set per block by the render)
    Core c;

    DelayT() { construct(); }

    // engine construction: ILDelay3's ctor (FUN_180010b30: buffers, SVF at 1000 Hz, setRate 44100, tone 0.5), the
    // wrapper's fields (ctor 0x180016700..) and its parameter pass with 0.5 for the eleven parameters
    void construct()
    {
        c = Core {};
        c.main.alloc (0x40000), c.e1.alloc (0x40000), c.e2.alloc (0x40000), c.e3.alloc (0x40000), c.e4.alloc (0x40000);
        c.ap.alloc (0x8000);
        c.svSet (1000.0f, 0.0f);
        c.setRate (44100.0);
        c.setTone (0.5f);
        c.cutoff = 0.5f;
        c.wet = 1.0f, c.dry = 1.0f;
        raw = {};
        timeSync = timeFree = 0.5f, curTime = 0.0f;
        on = true, sync = false, xfading = false;
        active = 0, next = 1, xfLen = 1000, xfCount = 0, xfStep = 0x1.0624dep-10f;
        gA = 1.0f, gB = 0.0f, offCur = offTarget = 0.0f, upd = 0, updEvery = 10, tempo = 120.0;
        c.ftype = 0;
        for (int i = 0; i < 11; ++i)
            setParam (0x6e + i, 0.5f);
    }

    // the synth's rate set-up (0x18001d49b..0x18001d4c7) calls ILDelay3::setRate (vtable +8), which also clears the
    // delay lines, then sets the wrapper's update interval to (int) (fs * 0.002) samples (88 at 44100; the ctor's 10
    // only holds until the first rate set-up)
    void setRate (double fs) noexcept
    {
        c.setRate (fs);
        updEvery = (int) (fs * 0.002);
    }
    // the synth's reset calls ILDelay3's reset FUN_180011120
    void reset() noexcept { c.reset(); }
    void setTempo (double bpm) noexcept { tempo = bpm; }

    // global setter cases 0x6e..0x78 (identical to the wrapper's own pass FUN_180010650)
    void setParam (int index, float x) noexcept
    {
        const int i = index - 0x6e;
        if (i < 0 || i > 10)
            return;
        raw[(std::size_t) i] = x;
        switch (i)
        {
            case 0: on = x >= 0.5f; break;
            case 1: sync = x >= 0.5f; break;
            case 2: c.inGain = x; break;
            case 3:
            {
                const double d = (double) x * 0.49 + 0.01;
                const int k = std::clamp ((int) (std::int64_t) (x * 17.0f + 0.5f), 0, 17); // the engine does not clamp
                timeSync = (float) (dlydetail::tSync[k] * 4.0);
                timeFree = (float) d;
                break;
            }
            case 4: offTarget = x; break;
            case 5: c.fb = x; break;
            case 6: c.cutoff = x; break;
            case 7: c.ftype = (int) (std::int64_t) (x * 3.0f + 0.5f); break;
            case 8:
            {
                const float m = (x + 1.0f) * 0.5f;
                c.mixA = m;
                c.mixB = 1.0f - m;
                break;
            }
            case 9: c.xfb = x; break;
            default: c.setTone (x); break;
        }
    }

    // FUN_1800107b0
    void process (float& L, float& R) noexcept
    {
        if (! on)
            return;
        if (++upd > updEvery)
        {
            upd = 0;
            const float t = sync ? (float) ((60.0 / tempo) * (double) timeSync) : timeFree;
            if (curTime == 0.0f || curTime != curTime)
            {
                curTime = t;
                c.time = { t, t };
                c.off = { offCur, offCur };
                c.setHead (0);
                c.setHead (1);
            }
            // ucomiss + jne: a NaN compares as equal here
            const bool moved = t < curTime || t > curTime || offTarget < offCur || offTarget > offCur;
            if (! xfading && moved)
            {
                xfCount = 0;
                xfading = true;
                offCur = offTarget;
                next = active ^ 1;
                curTime = t;
                c.time[(std::size_t) next] = t;
                c.off[(std::size_t) next] = offTarget;
                c.setHead (next);
            }
        }
        c.tick (L, R, gA, gB);
        if (xfading)
        {
            const float up = (float) xfCount * xfStep;
            const float down = 1.0f - up;
            float a = down, b = up;
            if (active != 0)
                a = up, b = down;
            gA = a;
            gB = b;
            if (++xfCount == xfLen)
            {
                xfading = false;
                gA = 0.5f > a ? 0.0f : 1.0f;
                gB = 0.5f > b ? 0.0f : 1.0f;
                active = next;
            }
        }
    }
};
using Delay = DelayT<>;

// ---------------------------------------------------------------------------------------------- verification notes
// Harness (Windows, our own code, DLL unmodified): an object built as the synth ctor builds it (ILDelay3 ctor
// FUN_180010b30 at +0x30, the wrapper fields, FUN_180010650 (i, 0.5) for the eleven parameters), then ILDelay3 setRate
// FUN_180011ff0 plus the synth rate set-up's update interval (+0x48c = (int) (fs * 0.002), 0x18001d4c7), reset
// FUN_180011120 and the Exo defaults; per sample FUN_1800107b0 in MXCSR | 0xE040; events (parameter
// changes, host tempo +0x490, reset, setRate) applied between samples in nearest or in RZ. LYNX side: Delay with the same
// calls. 13 scenarios, 6.8 M output floats (L, R), 0 mismatches:
//  * defaults at 44100 and 48000 (150000 samples each);
//  * six random scenarios of 400000 samples (44100 / 48000 / 96000 / 22050 Hz, one with sample-rate changes) with ~90
//    random events each (all eleven parameters, tempo 60..200 BPM, resets), events in nearest and in RZ;
//  * directed: band-pass type with the cutoff moved every 300 / 500 samples (44100 RZ events, 48000 nearest), high-pass
//    with feedback 0.97, tempo sync with tempo and time changes, frequent time / offset changes (overlapping crossfades);
//    full-scale and sparse (silence between bursts) noise input; the wet signal is non-trivial in all of them.
//  * CRT: sinf FUN_1800c2a70 vs ildsp::sinf_ (400000 args in 0..3.2) and tanf FUN_1800c3990 vs exoDelayTanf (200000 args
//    in 0..1.5): 0 mismatches in both modes (plain tanf rounded from std::tan was 1 ulp off on ~0.02 % of them).
// Not covered bit for bit: the band-pass cos FUN_1800bdce0 runs inside the render (RZ), where the engine's double cos
// carries a ~1e-11 relative range-reduction error; ExoDelayStdCrt::cos is evaluated in nearest. The coefficient is
// rounded to float afterwards and no mismatch showed up in ~1600 band-pass redesigns, but a rare 1-ulp coefficient
// difference is possible. tanf arguments of 2^23 and above (never produced here) use std::tan.

} // namespace kepler_exo
} // namespace lynx
