#include "core/PocketFx.h"

// Pocket Reverb Classic, Pocket Reverb 2 and Pocket Reverb: FL Studio Mobile's three reverb modules (12 "Reverb
// (legacy)", 35 "Reverb 2", 32 "Reverb") on FLM's own plate, rebuilt in round 190dw (docs/analise/FLM_FORMAT.md §7, round 190dw).
// Clean-room: FLM's laws restated as formulas in our own words; no Image-Line code or data.
//
// The plate is the same network as FL's Fruity Reeverb 2 (core/Reeverb2.cpp: line proportions, taps, decay, bass shelf
// and damping laws) with FLM's differences: the input is ½(L + R); the second and fourth input diffusers feed back
// through one coefficient and forward through another; the early-reflection line takes the second diffuser's output;
// the early-reflection taps are truncated from ⌊(room + 3)·fs/103⌋; the predelay is fs·ms/1000 samples; the wet gain
// glides by 0.001 per sample; the outputs pass a mid/side width stage. Everything runs in single precision with FLM's
// operation order, so a project imported from FL Studio Mobile renders as FLM renders it (verified against FLM inside
// FL, round 190dw). The three modules wrap the plate differently (see each class).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Denormals.h"

namespace lynx::pocket
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr int lineN[12] { 142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
constexpr int tapLN[7] { 266, 2974, 1913, 2496, 1690, 187, 1566 };
constexpr int tapRN[7] { 353, 3627, 1728, 3173, 1811, 335, 621 };
constexpr int erLN[8] { 1102, 2904, 3784, 4873, 5878, 7470, 9062, 9263 };
constexpr int erRN[8] { 837, 1423, 2865, 3938, 5278, 5978, 7532, 9263 };
constexpr int modSize = 2048; // each tank half's modulated line (the largest depth is 0.0045·fs: 871 samples at 192 kHz)
constexpr int chunk = 512;    // the host block the modules run in

float fillValue() noexcept // what FLM's flush writes into the delay memory (about 1e-30)
{
    const std::uint32_t bits = 0x0da24260u;
    float f;
    std::memcpy (&f, &bits, 4);
    return f;
}

std::string format (const char* fmt, double v)
{
    char text[48];
    std::snprintf (text, sizeof (text), fmt, v);
    return text;
}

// The plate's integer controls, in its table order, and the inner setter ids that reach them
enum Control { input, lowCut, highCut, predelay, room, diffusion, bass, crossover, decay, damping, separation, dry, early,
               wet, muteInput, muteDry, muteEarly, muteWet, controls };
struct Range
{
    float lo, hi, init;
};
using Table = std::array<Range, controls>;
// the legacy Reverb's table (also the Reverb (32)'s); Reverb 2's differs in the predelay and the four level maxima
const Table legacyTable { { { -361, 60, 0 }, { 19, 3000, 75 }, { 5, 221, 40 }, { 0, 250, 0 }, { 1, 100, 50 }, { 0, 100, 100 },
                            { 2, 30, 10 }, { 25, 2000, 500 }, { 1, 200, 15 }, { 5, 221, 40 }, { 0, 200, 100 }, { -361, 60, 0 },
                            { -361, 60, -35 }, { -361, 60, -120 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 } } };
const Table reverb2Table { { { -361, 0, 0 }, { 19, 3000, 75 }, { 5, 221, 40 }, { 0, 1000, 0 }, { 1, 100, 50 }, { 0, 100, 100 },
                             { 2, 30, 10 }, { 25, 2000, 500 }, { 1, 200, 15 }, { 5, 221, 40 }, { 0, 200, 100 }, { -361, 0, 0 },
                             { -361, 0, -35 }, { -361, 0, -120 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 } } };
// inner setter id → control (ids 0…9 and 14…17 are the control itself; 10 dry, 11 early reflections, 12 wet, 13 width)
constexpr int idControl (int id) noexcept { return id == 10 ? dry : id == 11 ? early : id == 12 ? wet : id == 13 ? separation : id; }
constexpr int idModAmount = 18, idModSpeed = 19;

// a room length × one of FLM's line / tap constants / 20000, in 64 bits: at device rates above ~480 kHz the 32-bit product
// wrapped (round 190eb: issue134's 768 kHz run read before a line's start); the same values below that
constexpr int scaled (int room, int n) noexcept { return (int) ((long long) room * n / 20000); }

// FLM's integer control from a 0…1 value: ⌊(hi − lo)·v + lo + ½⌋ in single precision
int control (const Range& r, float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    return (int) std::floor ((r.hi - r.lo) * v + r.lo + 0.5f);
}

// FLM's plate ("fxReverb")
class Plate
{
public:
    explicit Plate (const Table& t, bool cubeSpeed) : table (t), cube (cubeSpeed)
    {
        for (int k = 0; k < controls; ++k)
            ctl[(std::size_t) k] = (int) table[(std::size_t) k].init;
    }

    void prepare (double sampleRate)
    {
        fsD = sampleRate > 0.0 ? sampleRate : 44100.0;
        fsI = (int) fsD;
        const int maxRoom = (115 * fsI) / 115;
        for (int k = 0; k < 12; ++k)
            line[(std::size_t) k].assign ((std::size_t) std::max (1, scaled (maxRoom, lineN[k])), 0.0f);
        pre.assign ((std::size_t) std::max (1, (int) ((long long) fsI * (long long) table[predelay].hi / 1000)) + 1, 0.0f);
        int need = (int) (0.316 * fsD) + 2, size = 1;
        while (size < need)
            size <<= 1;
        er.assign ((std::size_t) size, 0.0f);
        erMask = size - 1;
        for (auto& m : mod)
            m.assign ((std::size_t) modSize, 0.0f);
        // the depth's smoothing (as FL's plate: 1 % left after 0.1 s)
        modSmooth = 1.0f - std::pow (0.01f, (float) (1.0 / (fsD * 0.1)));
        storedRoom = -1;
        dirty = true;
        flush();
    }

    // FLM's flush: the delay memory to the fill value, the filters to zero, the wet gain "unset"
    void flush() noexcept
    {
        const auto f = fillValue();
        for (auto& b : line)
            std::fill (b.begin(), b.end(), f);
        std::fill (pre.begin(), pre.end(), f);
        std::fill (er.begin(), er.end(), f);
        for (auto& m : mod)
            std::fill (m.begin(), m.end(), f);
        hc = lcY = lcX = 0.0f;
        shA1 = shA2 = shB1 = shB2 = dampA = dampB = 0.0f;
        wetNow = -1000.0f;
    }

    void set (int id, float v) noexcept
    {
        v = std::clamp (v, 0.0f, 1.0f);
        if (id == idModAmount)
            modAmountIn = v;
        else if (id == idModSpeed)
            modSpeedIn = v;
        else
        {
            const auto k = idControl (id);
            ctl[(std::size_t) k] = control (table[(std::size_t) k], v);
        }
        dirty = true;
    }
    int value (int k) const noexcept { return ctl[(std::size_t) k]; }

    // Reverb 2 / Reverb (32): input → A/B = the full output after the width stage, C/D = the raw early reflections
    void process4 (const float* inL, const float* inR, float* a, float* b, float* c, float* d, int n) noexcept
    {
        run<false> (inL, inR, a, b, c, d, nullptr, nullptr, n);
    }
    // the legacy Reverb: in place, the plate's output and the dry added onto the buffer
    void processInPlace (float* l, float* r, int n) noexcept { run<true> (l, r, nullptr, nullptr, nullptr, nullptr, l, r, n); }

private:
    void update() noexcept
    {
        dirty = false;
        const float fs = (float) fsD;
        const double fsd = (double) fs;
        const float inv = 1.0f / fs;
        const auto level = [&] (int k) {
            return (int) table[(std::size_t) k].lo < ctl[(std::size_t) k] ? std::pow (10.0f, (float) ctl[(std::size_t) k] * 0.005f) : 0.0f;
        };
        gIn = ctl[muteInput] != 0 ? 0.0f : level (input);
        if ((float) ctl[lowCut] > table[lowCut].lo)
        {
            const float f = std::min (fs * 0.5f, (float) ctl[lowCut]);
            const float t = std::tan ((float) ((double) f * pi / fsd));
            cLc = (t - 1.0f) / (t + 1.0f);
        }
        else
            cLc = 0.0f;
        const auto onePole = [] (float arg) {
            const float y = 2.0f - std::cos (arg);
            return 1.0f - (y - std::sqrt (y * y - 1.0f));
        };
        if ((float) ctl[highCut] < table[highCut].hi)
        {
            float x = (float) ctl[highCut] * 100.0f;
            x = x + x;
            cHc = onePole ((float) ((double) x * pi / fsd));
        }
        else
            cHc = 1.0f;
        preLen = std::clamp ((int) ((long long) fsI * ctl[predelay] / 1000), 1, (int) pre.size());
        preP %= preLen;

        const int rs = ((ctl[room] + 15) * fsI) / 115;
        for (int k = 0; k < 12; ++k)
        {
            L[(std::size_t) k] = std::clamp (scaled (rs, lineN[k]), 1, (int) line[(std::size_t) k].size());
            P[(std::size_t) k] %= L[(std::size_t) k];
        }
        for (int k = 0; k < 7; ++k)
            tl[(std::size_t) k] = scaled (rs, tapLN[k]), tr[(std::size_t) k] = scaled (rs, tapRN[k]);
        const float x8 = (float) (((ctl[room] + 3) * fsI) / 103);
        for (int k = 0; k < 8; ++k)
        {
            el[(std::size_t) k] = (int) ((float) erLN[k] * 3.4013603e-05f * x8);
            erw[(std::size_t) k] = (int) ((float) erRN[k] * 3.4013603e-05f * x8);
        }
        // only the first tap of each side is weighted (the weights of the others come out 0)
        const float m = (float) (el[0] < erw[0] ? el[0] : erw[0]);
        wL0 = el[0] != 0 ? (m * 0.5f) / (float) el[0] : 0.0f;
        wR0 = erw[0] != 0 ? (m * 0.36f) / (float) erw[0] : 0.0f;
        if (ctl[room] != storedRoom)
        {
            storedRoom = ctl[room];
            for (int k = 0; k < 12; ++k)
                std::fill (line[(std::size_t) k].begin(), line[(std::size_t) k].begin() + L[(std::size_t) k], 0.0f);
            std::fill (er.begin(), er.end(), 0.0f);
        }

        const float dif = (float) ctl[diffusion] * 0.01f;
        if (dif == 0.0f)
            c4 = c8 = cT1 = cT2 = 0.0f;
        else
        {
            c4 = std::exp ((float) (L[0] + L[1]) * (-49.34111f / dif) * inv);
            c8 = std::exp ((-31.398888f / dif) * (float) (L[2] + L[3]) * inv);
            cT1 = std::exp ((float) (L[4] + L[8]) * -3.4538777f / fs * (2.857143f / dif));
            cT2 = std::exp (-3.4538777f / fs * (float) (L[6] + L[10]) * (1.9607843f / dif));
        }
        const float dd = (float) ctl[decay] * 0.1f;
        const float s = (float) (L[11] + L[9] + L[7] + L[5]) / fs * -6.9077554f;
        g = std::pow (std::exp (s / dd), 0.25f);
        const float rho = std::pow (std::exp (10.0f / ((float) ctl[bass] * dd) * s), 0.25f) / g;
        cShelf = (rho - 1.0f) * 0.5f;
        const float rhoC = std::min (rho, 1.0f);
        const float t = std::tan ((float) ((double) (float) ctl[crossover] * pi * (1.0 / fsd)));
        cAllpass = (t - rhoC) / (rhoC + t);
        if ((float) ctl[damping] < table[damping].hi)
        {
            float x = (float) ctl[damping] * 100.0f;
            x = x + x;
            cDamp = onePole ((float) ((double) x * pi * (1.0 / fsd)));
        }
        else
            cDamp = 1.0f;
        gDry = ctl[muteDry] != 0 ? 0.0f : level (dry);
        gEr = ctl[muteEarly] != 0 ? 0.0f : level (early) * 0.5f;
        wetTarget = ctl[muteWet] != 0 ? 0.0f : level (wet) * 0.3f;
        sigma = (float) ctl[separation];
    }

    template <bool inPlace>
    void run (const float* inL, const float* inR, float* outA, float* outB, float* outC, float* outD, float* ioL, float* ioR,
              int n) noexcept
    {
        if (dirty)
            update();
        const float t1 = 1.0f - g;
        const float w60 = t1 * 0.6f + g, w25 = t1 * 0.25f + g, w70 = t1 * 0.7f + g, w50 = t1 * 0.5f + g, w20 = t1 * 0.2f + g;
        const float speed = cube ? modSpeedIn * modSpeedIn * modSpeedIn : modSpeedIn;
        const float rate = speed * 9.9f + 0.1f;
        const float depthTarget = modAmountIn * 0.0045351475f * (float) fsI;
        const float fsF = (float) fsI;
        std::array<float*, 12> ln {};
        for (int k = 0; k < 12; ++k)
            ln[(std::size_t) k] = line[(std::size_t) k].data();
        auto& p = P;
        const auto& len = L;
        float* const erb = er.data();
        float* const preb = pre.data();
        float* const mA = mod[0].data();
        float* const mB = mod[1].data();
        constexpr int modMask = modSize - 1;

        const auto allPass = [&] (int k, float x, float cBack, float cFwd) {
            float* const b = ln[(std::size_t) k];
            int& q = p[(std::size_t) k];
            const float y = b[q] - cFwd * x;
            b[q] = cBack * y + x;
            if (--q < 0)
                q = len[(std::size_t) k] - 1;
            return y;
        };
        const auto tap = [&] (int k, int off) {
            int j = off + p[(std::size_t) k];
            if (j >= len[(std::size_t) k])
                j -= len[(std::size_t) k];
            return ln[(std::size_t) k][j];
        };

        for (int i = 0; i < n; ++i)
        {
            // modulation: the sine of the phase before its advance, the depth before its glide
            const float cur = modNow;
            const float arg = modPhase * 6.2831855f;
            const float ph = modPhase + rate / fsF;
            modPhase = ph - (float) (int) ph;
            const float sn = std::sin (arg);
            const float diff = depthTarget - cur;
            const float dly = (sn + 1.0f) * 0.5f * cur;
            modNow = std::abs (diff) >= 0.01f ? diff * modSmooth + cur + 1.0e-30f : depthTarget;

            const float xl = gIn * inL[i], xr = gIn * inR[i];
            float a = (((xr + xl) * 0.5f + 1.0e-30f) - hc) * cHc + hc;
            hc = a;
            const float lc = (a - lcY) * cLc + lcX;
            lcX = a;
            lcY = lc;
            if (cLc != 0.0f)
                a = (a - lc) * 0.5f + 1.0e-30f;

            float erl = 0.0f, err = 0.0f;
            if (gEr != 0.0f)
            {
                erl = erb[(el[0] + erP) & erMask] * wL0;
                err = erb[(erw[0] + erP) & erMask] * wR0;
            }
            // input diffusion: the second and fourth stages feed forward through c8
            const float y0 = allPass (0, a, c4, c4);
            const float y1 = allPass (1, y0, c4, c8);
            erb[erP] = y1;
            if (--erP < 0)
                erP = erMask;
            const float y2 = allPass (2, y1, c4, c4);
            const float y3 = allPass (3, y2, c4, c8);
            float z = preb[preP];
            preb[preP] = y3;
            if (--preP < 0)
                preP = preLen - 1;
            z = z + 1.0e-20f;

            const int whole = (int) dly;
            const float fr = dly - (float) whole;
            // tank, half A
            float u = ln[11][p[11]] * g;
            float f = (u - shA2) * cAllpass + shA1;
            shA1 = u, shA2 = f;
            float v = ((((f + u) * cShelf + u) + z) - dampA) * cDamp + dampA;
            dampA = v;
            mA[modWA] = v;
            modWA = (modWA + 1) & modMask;
            int j = modWA - whole;
            float r = fr * mA[(j - 2) & modMask] + (1.0f - fr) * mA[(j - 1) & modMask];
            float w = allPass (4, r, cT1, cT1);
            r = ln[5][p[5]];
            ln[5][p[5]] = w;
            if (--p[5] < 0)
                p[5] = len[5] - 1;
            w = allPass (6, r * g, cT2, cT2);
            ln[7][p[7]] = w;
            if (--p[7] < 0)
                p[7] = len[7] - 1;
            // tank, half B
            u = ln[7][p[7]] * g;
            f = (u - shB2) * cAllpass + shB1;
            shB1 = u, shB2 = f;
            v = ((((f + u) * cShelf + u) + z) - dampB) * cDamp + dampB;
            dampB = v;
            mB[modWB] = v;
            modWB = (modWB + 1) & modMask;
            j = modWB - whole;
            r = (1.0f - fr) * mB[(j - 1) & modMask] + fr * mB[(j - 2) & modMask];
            w = allPass (8, r, cT1, cT1);
            r = ln[9][p[9]];
            ln[9][p[9]] = w;
            if (--p[9] < 0)
                p[9] = len[9] - 1;
            w = allPass (10, r * g, cT2, cT2);
            ln[11][p[11]] = w;
            if (--p[11] < 0)
                p[11] = len[11] - 1;

            // the wet gain glides (in double) by 0.001 per sample, from "unset" straight to its target
            float wg = wetNow, target = wetTarget;
            if (wg < -99.0f)
                wg = target;
            wetNow = (float) ((double) (target - wg) * 0.001 + (double) wg);
            const float tapL = (((((tap (9, tl[0]) - w25 * tap (9, tl[1])) - w70 * tap (10, tl[2])) - w20 * tap (11, tl[3]))
                                 + w60 * tap (5, tl[4]))
                                + tap (6, tl[5]))
                               + w50 * tap (7, tl[6]);
            const float tapR = (((((w25 * tap (5, tr[1]) + tap (5, tr[0])) - w70 * tap (6, tr[2])) - w20 * tap (7, tr[3]))
                                 - w60 * tap (9, tr[4]))
                                + tap (10, tr[5]))
                               + w50 * tap (11, tr[6]);
            const float l = gEr * erl + wetNow * tapL;
            const float rr = gEr * err + wetNow * tapR;
            const float sum = rr + l;
            const float side = (l - rr) * sigma * 0.01f;
            if constexpr (inPlace)
            {
                ioL[i] = ((sum + side) + xl * gDry) + ioL[i];
                ioR[i] = (xr * gDry + (sum - side)) + ioR[i];
            }
            else
            {
                outA[i] = sum + side;
                outB[i] = sum - side;
                outC[i] = erl;
                outD[i] = err;
            }
        }
    }

    const Table& table;
    const bool cube;
    std::array<int, controls> ctl {};
    float modAmountIn = 0.0f, modSpeedIn = 0.0f;
    double fsD = 44100.0;
    int fsI = 44100;
    bool dirty = true;

    std::array<std::vector<float>, 12> line;
    std::array<int, 12> L { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, P {};
    std::array<int, 7> tl {}, tr {};
    std::array<int, 8> el {}, erw {};
    std::vector<float> pre, er;
    std::array<std::vector<float>, 2> mod;
    int preLen = 1, preP = 0, erP = 0, erMask = 0, modWA = 0, modWB = 0, storedRoom = -1;
    float gIn = 1, gDry = 1, gEr = 0, wetTarget = 0, wetNow = -1000.0f, sigma = 100;
    float cLc = 0, cHc = 1, c4 = 0, c8 = 0, cT1 = 0, cT2 = 0, g = 0, cShelf = 0, cAllpass = 0, cDamp = 1, wL0 = 0, wR0 = 0;
    float hc = 0, lcY = 0, lcX = 0, shA1 = 0, shA2 = 0, shB1 = 0, shB2 = 0, dampA = 0, dampB = 0;
    float modPhase = 0, modNow = 0, modSmooth = 0;
};

// FLM's module gate (the wrapper every module runs through): a module that has had no input and a quiet output
// (≤ 1e-5) for half a second stops and is reset; it starts again with the next input. Freshly reset, it waits for input.
class Gate
{
public:
    void prepare (double fs) noexcept
    {
        limit = (unsigned) (0.5 * fs);
        count = limit + 1;
        stopped = false;
    }
    void reset() noexcept { count = limit + 1, stopped = false; }
    // true = run the module on this block
    bool begin (const float* l, const float* r, int n) noexcept
    {
        bool active = false;
        for (int i = 0; i < n && ! active; ++i)
            active = l[i] != 0.0f || r[i] != 0.0f;
        if (active)
        {
            count = 0;
            return true;
        }
        if (count > limit)
            return false;
        count += (unsigned) n;
        if (count <= limit)
            return true;
        stopped = true; // (FLM resets the module at its next call)
        return false;
    }
    void heard (const float* l, const float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            if (std::abs (l[i]) > 1.0e-5f || std::abs (r[i]) > 1.0e-5f)
            {
                count = 0;
                return;
            }
    }
    bool takeStop() noexcept
    {
        const auto s = stopped;
        stopped = false;
        return s;
    }

private:
    unsigned limit = 22050, count = 22051;
    bool stopped = false;
};

// Shared host side: parameter changes reach the plate as FLM's setter calls; mono input is played as both sides
class Base : public Effect
{
public:
    Base (const Table& t, bool cube, int params) : plate (t, cube), count (params) { last.fill (-1.0f); }

    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        plate.prepare (fs);
        gate.prepare (fs);
        for (auto* b : { &a, &b2, &c, &d, &monoR })
            b->assign ((std::size_t) chunk, 0.0f);
        resetModule();
    }
    void reset() noexcept override
    {
        plate.flush();
        gate.reset();
        resetModule();
    }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        ScopedFlushDenormals ftz;
        for (int at = 0; at < n; at += chunk)
        {
            const int m = std::min (chunk, n - at);
            float* l = left + at;
            float* r = right != nullptr ? right + at : nullptr;
            if (r == nullptr)
            {
                std::copy (l, l + m, monoR.begin());
                r = monoR.data();
            }
            if (gate.takeStop())
            {
                plate.flush();
                resetModule();
            }
            apply (p, bpm);
            if (gate.begin (l, r, m))
            {
                render (l, r, m);
                gate.heard (l, r, m);
            }
        }
    }

protected:
    // module parameter k (FLM's k + 1) → plate setter id; −1 = the module uses it itself
    virtual int target (int k) const noexcept = 0;
    virtual float shape (int k, float v) const noexcept { (void) k; return v; }
    virtual void apply (const EffectParams& p, double bpm) noexcept
    {
        (void) bpm;
        for (int k = 0; k < count; ++k)
            if (p[(std::size_t) k] != last[(std::size_t) k])
            {
                last[(std::size_t) k] = p[(std::size_t) k];
                if (const auto id = target (k); id >= 0)
                    plate.set (id, shape (k, p[(std::size_t) k]));
            }
    }
    virtual void render (float* l, float* r, int m) noexcept = 0;
    virtual void resetModule() noexcept {}

    Plate plate;
    Gate gate;
    int count;
    double fs = 44100.0;
    std::array<float, 24> last {};
    std::vector<float> a, b2, c, d, monoR;
};

// Reverb (legacy, module 12): the plate in place; the input always passes at unity (the plate adds onto the buffer)
class Classic final : public Base
{
public:
    Classic() : Base (legacyTable, false, reverbClassicParams) {}

protected:
    int target (int k) const noexcept override
    {
        // FLM 1 DECAY … 20 MOD AMOUNT → plate ids (21 is unused)
        static constexpr int ids[21] { 8, 9, 12, 3, 4, 5, 13, 1, 2, 0, 11, 10, 14, 15, 16, 17, 6, 7, 19, 18, -1 };
        return k >= 0 && k < 21 ? ids[k] : -1;
    }
    float shape (int k, float v) const noexcept override { return k == 0 ? v * v * v : v; } // DECAY is cubed
    void render (float* l, float* r, int m) noexcept override { plate.processInPlace (l, r, m); }
};

// Reverb 2 (module 35): the plate at 0 dB, mixed by the module: DRY, ER (raw early reflections) and WET (the full output)
// × 1.25, then its own SEPARATION stage and the wet PAN; SIDE INPUT feeds the plate ½(L − R); DELAY may follow the tempo
class Reverb2Fx final : public Base
{
public:
    Reverb2Fx() : Base (reverb2Table, true, reverb2Params)
    {
        for (int id : { 0, 10, 11, 12 })
            plate.set (id, 1.0f);
    }

protected:
    int target (int k) const noexcept override
    {
        // FLM 1 HIGH CUT, 2 LOW CUT, 3 SIZE, 4 DECAY, 5 DIFFUSION, 6 DAMPING, 13 MOD, 14 BASS, 16 SPEED, 17 CROSS;
        // 7 DRY, 8 ER, 9 WET, 10 SIDE, 12 TEMPO, 15 SEPARATION, 18 PAN are the module's; 11 DELAY below
        static constexpr int ids[18] { 2, 1, 4, 8, 5, 9, -1, -1, -1, -1, -1, -1, 18, 6, -1, 19, 7, -1 };
        return k >= 0 && k < 18 ? ids[k] : -1;
    }
    void apply (const EffectParams& p, double bpm) noexcept override
    {
        Base::apply (p, bpm);
        // DELAY: free (0…1000 ms), or with TEMPO a whole number of 1/16 beats, shortened until it fits in 1000 ms
        const float delay = p[10], tempo = p[11];
        const bool synced = tempo >= 0.5f && bpm > 0.0;
        if (delay != lastDelay || synced != lastSynced || (synced && bpm != lastBpm))
        {
            lastDelay = delay, lastSynced = synced, lastBpm = bpm;
            plate.set (3, synced ? syncedDelay (delay, bpm) : delay);
        }
    }
    void render (float* l, float* r, int m) noexcept override
    {
        const bool side = last[9] > 0.5f;
        if (side)
            for (int i = 0; i < m; ++i)
                r[i] = -r[i];
        plate.process4 (l, r, a.data(), b2.data(), c.data(), d.data(), m);
        const float pan = last[17];
        const float panR = std::min (pan * 2.0f, 1.0f);
        const float panL = std::min ((1.0f - pan) + (1.0f - pan), 1.0f);
        const float sep = last[14];
        const float gDry = last[6] * 1.25f, gEr = last[7] * 1.25f, gWet = last[8] * 1.25f;
        for (int i = 0; i < m; ++i)
        {
            float wl = gWet * a[(std::size_t) i] + gEr * c[(std::size_t) i];
            float wr = gWet * b2[(std::size_t) i] + gEr * d[(std::size_t) i];
            if (0.5f <= sep)
            {
                const float mid = (wr + wl) * 0.5f;
                float s = (wl - wr) * 0.5f;
                s = s - ((wl - wr) * sep - s);
                wr = mid - s;
                wl = s + mid;
            }
            else
            {
                wl = wl - (0.5f - sep) * wr;
                wr = wr - (0.5f - sep) * wl;
            }
            if (side)
                r[i] = -r[i];
            l[i] = gDry * l[i] + wl * panL;
            r[i] = gDry * r[i] + wr * panR;
        }
    }
    void resetModule() noexcept override { lastDelay = -1.0f; }

private:
    static float syncedDelay (float delay, double bpm) noexcept
    {
        float steps = std::round (delay * 16.0f), v = 0.0f;
        do
        {
            v = (1.0f / 16.0f) * steps;
            steps = steps - 1.0f;
            v = (float) ((double) v * (1.0 / bpm) * (1.0 / 1000.0) * 60000.0);
            if (v <= 1.0f)
                break;
        } while (0.0f <= steps);
        return v;
    }
    float lastDelay = -1.0f;
    bool lastSynced = false;
    double lastBpm = 0.0;
};

// Reverb (module 32): the legacy controls on the plate (early reflections −3.5 dB, wet −12 dB: the table's defaults), mixed
// by the module: dry × level × (MIX ≤ ½ ? 1 : 2 − 2·MIX), the full output × wet level × min(2·MIX, 1) through the PAN's
// balance, the raw early reflections × EARLY REFL.²; the four gains glide by 0.01 per sample
class ReverbFx final : public Base
{
public:
    ReverbFx() : Base (legacyTable, false, reverbParams) {}

protected:
    int target (int k) const noexcept override
    {
        // FLM 1 DECAY, 2 HIGH DAMP, 6 PREDELAY, 7 ROOM, 8 DIFFUSION, 9 WIDTH, 10 LO-CUT, 11 HI-CUT, 12 MOD SPEED,
        // 13 MOD AMOUNT; 3 MIX, 4 PAN, 5 EARLY REFL., 14 wet level, 15 dry level are the module's
        static constexpr int ids[15] { 8, 9, -1, -1, -1, 3, 4, 5, 13, 1, 2, 19, 18, -1, -1 };
        return k >= 0 && k < 15 ? ids[k] : -1;
    }
    float shape (int k, float v) const noexcept override { return k == 0 ? v * v * v : v; }
    void render (float* l, float* r, int m) noexcept override
    {
        plate.process4 (l, r, a.data(), b2.data(), c.data(), d.data(), m);
        const float mix = last[2];
        const float wetScale = std::min (mix + mix, 1.0f);
        const float dryScale = 0.5f < mix ? 2.0f - (mix + mix) : 1.0f;
        const float wetLevel = last[13] * wetScale;
        const float dryT = last[14] * dryScale;
        const float pan = last[3];
        const float wetRT = std::min (std::max (pan * 2.0f, 0.0f), 1.0f) * wetLevel;
        const float wetLT = std::min (std::max ((1.0f - pan) + (1.0f - pan), 0.0f), 1.0f) * wetLevel;
        const float erT = last[4] * last[4];
        if (gDry < 0.0f)
            gDry = dryT;
        if (gWetL < 0.0f)
            gWetL = wetLT;
        if (gWetR < 0.0f)
            gWetR = wetRT;
        if (gEr < 0.0f)
            gEr = erT;
        for (int i = 0; i < m; ++i)
        {
            l[i] = (gWetL * a[(std::size_t) i] + gDry * l[i]) + gEr * c[(std::size_t) i];
            r[i] = (b2[(std::size_t) i] * gWetR + r[i] * gDry) + d[(std::size_t) i] * gEr;
            if (gDry != dryT)
                gDry = (dryT - gDry) * 0.01f + gDry;
            if (gWetL != wetLT)
                gWetL = (wetLT - gWetL) * 0.01f + gWetL;
            if (gWetR != wetRT)
                gWetR = (wetRT - gWetR) * 0.01f + gWetR;
            if (gEr != erT)
                gEr = (erT - gEr) * 0.01f + gEr;
        }
    }
    void resetModule() noexcept override { gDry = gWetL = gWetR = gEr = -1.0f; }

private:
    float gDry = -1.0f, gWetL = -1.0f, gWetR = -1.0f, gEr = -1.0f;
};

// texts in FLM's units
int ctl (const Range& r, float v) noexcept { return control (r, v); }
std::string seconds (int tenths) { return format ("%.1f s", tenths * 0.1); }
std::string tenthsDb (int t) { return t <= -361 ? "-inf dB" : format ("%.1f dB", t * 0.1); }
std::string cutKhz (int c, bool offAtTop) { return offAtTop && c >= 221 ? std::string ("Off") : format ("%.1f kHz", c * 0.1); }
std::string onOff (float v) { return v >= 0.5f ? "On" : "Off"; }
std::string percent (float v) { return format ("%.0f%%", (double) v * 100.0); }
std::string panText (float v)
{
    const int p = (int) std::lround ((2.0f * v - 1.0f) * 100.0f);
    return p == 0 ? std::string ("Centre") : format (p < 0 ? "%.0f%% L" : "%.0f%% R", std::abs ((double) p));
}
} // namespace

std::unique_ptr<Effect> makeReverbClassic() { return std::make_unique<Classic>(); }
std::unique_ptr<Effect> makeReverb2() { return std::make_unique<Reverb2Fx>(); }
std::unique_ptr<Effect> makeReverb() { return std::make_unique<ReverbFx>(); }

std::string reverbClassicParamText (int param, float v)
{
    const auto& t = legacyTable;
    switch (param)
    {
        case 0: return seconds (ctl (t[decay], v * v * v));
        case 1: return cutKhz (ctl (t[damping], v), true);
        case 2: return tenthsDb (ctl (t[wet], v));
        case 3: return format ("%.0f ms", ctl (t[predelay], v));
        case 4: return format ("%.0f", ctl (t[room], v));
        case 5: return format ("%.0f%%", ctl (t[diffusion], v));
        case 6: return format ("%.0f%%", ctl (t[separation], v));
        case 7: { const int c = ctl (t[lowCut], v); return c <= 19 ? std::string ("Off") : format ("%.0f Hz", c); }
        case 8: return cutKhz (ctl (t[highCut], v), true);
        case 9: return tenthsDb (ctl (t[input], v));
        case 10: return tenthsDb (ctl (t[early], v));
        case 11: return tenthsDb (ctl (t[dry], v));
        case 12: case 13: case 14: case 15: return v >= 0.5f ? "Muted" : "On";
        case 16: return format ("x%.1f", ctl (t[bass], v) * 0.1);
        case 17: return format ("%.0f Hz", ctl (t[crossover], v));
        case 18: return format ("%.2f Hz", (double) (v * 9.9f + 0.1f));
        default: return percent (v);
    }
}

std::string reverb2ParamText (int param, float v)
{
    const auto& t = reverb2Table;
    switch (param)
    {
        case 0: return cutKhz (ctl (t[highCut], v), true);
        case 1: { const int c = ctl (t[lowCut], v); return c <= 19 ? std::string ("Off") : format ("%.0f Hz", c); }
        case 2: return format ("%.0f", ctl (t[room], v));
        case 3: return seconds (ctl (t[decay], v));
        case 4: return format ("%.0f%%", ctl (t[diffusion], v));
        case 5: return cutKhz (ctl (t[damping], v), true);
        case 6: case 7: case 8: return format ("%.0f%%", (double) v * 125.0);
        case 9: case 11: return onOff (v);
        case 10: return format ("%.0f ms", ctl (t[predelay], v));
        case 13: return format ("x%.1f", ctl (t[bass], v) * 0.1);
        case 14: return format ("%.0f%%", (double) v * 200.0);
        case 15: return format ("%.2f Hz", (double) (v * v * v * 9.9f + 0.1f));
        case 16: return format ("%.0f Hz", ctl (t[crossover], v));
        case 17: return panText (v);
        default: return percent (v);
    }
}

std::string reverbParamText (int param, float v)
{
    const auto& t = legacyTable;
    switch (param)
    {
        case 0: return seconds (ctl (t[decay], v * v * v));
        case 1: return cutKhz (ctl (t[damping], v), true);
        case 3: return panText (v);
        case 4: return percent (v * v);
        case 5: return format ("%.0f ms", ctl (t[predelay], v));
        case 6: return format ("%.0f", ctl (t[room], v));
        case 7: return format ("%.0f%%", ctl (t[diffusion], v));
        case 8: return format ("%.0f%%", ctl (t[separation], v));
        case 9: { const int c = ctl (t[lowCut], v); return c <= 19 ? std::string ("Off") : format ("%.0f Hz", c); }
        case 10: return cutKhz (ctl (t[highCut], v), true);
        case 11: return format ("%.2f Hz", (double) (v * 9.9f + 0.1f));
        default: return percent (v);
    }
}
} // namespace lynx::pocket
