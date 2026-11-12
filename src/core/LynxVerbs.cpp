#include "core/LynxVerbs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/NebulaCloud.h"

namespace lynx::verbs
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int lineCount = 8;

// Line-length sets of the late tank, ms at lineScale 1 (spread over about an octave, no common factors, so the
// modes of the lines do not line up). The last set is a ratio set (× lineScale in ms) for delay networks.
constexpr double shapes[][lineCount] = {
    { 37.1, 41.9, 47.3, 53.9, 61.1, 67.7, 74.3, 83.9 }, // 0 hall
    { 21.1, 24.7, 28.3, 32.9, 37.3, 41.9, 46.1, 51.7 }, // 1 room / chamber
    { 29.3, 33.1, 38.9, 44.3, 51.7, 57.1, 63.7, 71.3 }, // 2 plate
    { 0.031, 0.117, 0.229, 0.357, 0.493, 0.641, 0.809, 1.000 }, // 3 delay network (fractions of the delay, see lineSpread)
};
constexpr int shapeCount = (int) (sizeof (shapes) / sizeof (shapes[0]));

// input diffusers: J. Dattorro's lengths (142, 107, 379, 277 samples at 29761 Hz), the right channel slightly apart
constexpr double inApMs[2][4] = { { 4.771, 3.595, 12.735, 9.307 }, { 4.513, 3.871, 11.913, 8.711 } };
constexpr double inApCoef[4] = { 0.75, 0.75, 0.625, 0.625 };
// in-loop all-pass length as a fraction of its line, early smearing all-passes (ms)
constexpr double loopApFraction = 0.16;
constexpr double earlyApMs[2][2] = { { 7.13, 2.31 }, { 6.47, 2.83 } };

constexpr int earlyTaps = 14;
constexpr int buildTaps = 40;
constexpr int maxBandSections = 16;

// a small deterministic generator for tap patterns (the same pattern on every machine)
struct Lcg
{
    std::uint32_t s;
    double next() noexcept
    {
        s = s * 1664525u + 1013904223u;
        return (double) (s >> 8) / 16777216.0;
    }
};

inline int nextPow2 (int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

struct Line
{
    std::vector<float> buf;
    int mask = 0, pos = 0;

    void allocate (int samples)
    {
        const auto n = nextPow2 (std::max (4, samples + 4));
        buf.assign ((std::size_t) n, 0.0f);
        mask = n - 1;
        pos = 0;
    }
    void clear() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        pos = 0;
    }
    void write (float x) noexcept
    {
        buf[(std::size_t) pos] = x;
        pos = (pos + 1) & mask;
    }
    // d ≥ 1: the sample written d writes ago
    float read (int d) const noexcept { return buf[(std::size_t) ((pos - d) & mask)]; }
    // fractional delay (≥ 2), 4-point Hermite
    float readFrac (double d) const noexcept
    {
        const auto i = (int) d;
        const auto f = (float) (d - i);
        const auto xm1 = read (i - 1), x0 = read (i), x1 = read (i + 1), x2 = read (i + 2);
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

struct Allpass
{
    Line line;
    int len = 1;
    float process (float x, float g) noexcept
    {
        const auto d = line.read (len);
        const auto v = x + g * d;
        line.write (v);
        return d - g * v;
    }
};

struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setLowpass (double hz, double sr) noexcept { a = (float) std::exp (-2.0 * pi * std::clamp (hz, 1.0, sr * 0.49) / sr); }
    float lp (float x) noexcept { return z = x + a * (z - x); }
};

// the two-head pitch shifter (also plays backwards: ratio −1)
struct Shifter
{
    Line line;
    double phase = 0.0, window = 2400.0;
    void prepare (double sr, double windowSec)
    {
        window = std::max (64.0, windowSec * sr);
        line.allocate ((int) (window * 2.5) + 8);
        phase = 0.0;
    }
    void clear() noexcept
    {
        line.clear();
        phase = 0.0;
    }
    float process (float x, double ratio) noexcept
    {
        line.write (x);
        phase += (1.0 - ratio) / window;
        phase -= std::floor (phase);
        float y = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            auto p = phase + 0.5 * h;
            p -= std::floor (p);
            const auto w = (float) std::sin (pi * p);
            y += w * w * line.readFrac (2.0 + p * window);
        }
        return y;
    }
};

double clampPos (double v, double lo, double hi) noexcept { return std::isfinite (v) ? std::clamp (v, lo, hi) : lo; }

// ---- engine law 1 (0.1.27, round 190bz) ------------------------------------------------------------------------
// Per-band decay (Jot & Chaigne 1991: one absorption filter per line whose attenuation in dB is proportional to the
// line's length gives the same RT60 on every line; the filter here is a cascade of first-order shelves whose gains are
// solved by least squares on the octave bands, the way proportional graphic equalisers are designed: Välimäki &
// Liski 2017, Prawda, Välimäki & Schlecht 2019), a tone corrector of the same kind after the loop, a diffusion-step
// onset (delay bank → polarity → Hadamard, three steps), rotation scattering inside the halves and a swinging
// rotation between them (Schlecht & Habets 2015: a time-varying unitary matrix moves all bands alike).
constexpr double bandHz[engineBands] = { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
constexpr int loopShelves = 6;
constexpr double loopShelfHz[loopShelves] = { 90.0, 250.0, 700.0, 1600.0, 3200.0, 6000.0 };
constexpr int maxShelves = 7;
constexpr int onsetSteps = 3, onsetChans = 4;
constexpr double onsetFrac[onsetSteps][onsetChans] = { { 0.23, 0.52, 0.77, 1.0 }, { 0.19, 0.47, 0.71, 1.0 }, { 0.27, 0.44, 0.83, 1.0 } };
constexpr double onsetSpan[onsetSteps] = { 1.0 / 7.0, 2.0 / 7.0, 4.0 / 7.0 };
constexpr float onsetSign[onsetSteps][onsetChans] = { { 1.0f, -1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, -1.0f, 1.0f }, { -1.0f, 1.0f, 1.0f, 1.0f } };
constexpr double maxOnsetSec = 0.25;
constexpr double maxLoopGain = 0.99995;

// dB of the symmetric first-order low shelf (gain gDb at 0 Hz, 0 dB at the top, half of it at the corner) at the
// warped frequency t = tan (π f / sr), corner tc: |H|² = (t² + tc² G) / (t² + tc² / G)
double shelfDbAt (double gDb, double tc, double t) noexcept
{
    const auto g = std::pow (10.0, gDb / 20.0);
    return 10.0 * std::log10 ((t * t + tc * tc * g) / (t * t + tc * tc / g));
}

// least squares: a broadband gain and n low shelves (dB) whose sum meets the dB targets at the eight band centres.
// The shelves' shapes depend a little on their gains, so the solve is repeated on the shapes of the last answer
void fitShelves (const double* tc, int n, const double* tb, const double* target, double ridge, double& gainDb, double* shelfDb) noexcept
{
    const int m = n + 1;
    for (int k = 0; k < n; ++k)
        shelfDb[k] = 0.0;
    gainDb = 0.0;
    for (int pass = 0; pass < 3; ++pass)
    {
        double a[engineBands][maxShelves + 1];
        for (int b = 0; b < engineBands; ++b)
        {
            a[b][0] = 1.0;
            for (int k = 0; k < n; ++k)
                a[b][k + 1] = std::abs (shelfDb[k]) < 0.05 ? tc[k] * tc[k] / (tb[b] * tb[b] + tc[k] * tc[k])
                                                           : shelfDbAt (shelfDb[k], tc[k], tb[b]) / shelfDb[k];
        }
        double nm[maxShelves + 1][maxShelves + 2] {};
        for (int i = 0; i < m; ++i)
        {
            for (int j = 0; j < m; ++j)
                for (int b = 0; b < engineBands; ++b)
                    nm[i][j] += a[b][i] * a[b][j];
            for (int b = 0; b < engineBands; ++b)
                nm[i][m] += a[b][i] * target[b];
            if (i > 0)
                nm[i][i] *= 1.0 + ridge;
        }
        for (int c = 0; c < m; ++c) // Gauss–Jordan with partial pivoting
        {
            int piv = c;
            for (int r = c + 1; r < m; ++r)
                if (std::abs (nm[r][c]) > std::abs (nm[piv][c]))
                    piv = r;
            if (std::abs (nm[piv][c]) < 1e-14)
                continue;
            if (piv != c)
                for (int j = 0; j <= m; ++j)
                    std::swap (nm[piv][j], nm[c][j]);
            for (int r = 0; r < m; ++r)
                if (r != c)
                {
                    const auto f = nm[r][c] / nm[c][c];
                    for (int j = c; j <= m; ++j)
                        nm[r][j] -= f * nm[c][j];
                }
        }
        gainDb = std::abs (nm[0][0]) > 1e-14 ? nm[0][m] / nm[0][0] : 0.0;
        for (int k = 0; k < n; ++k)
            shelfDb[k] = std::abs (nm[k + 1][k + 1]) > 1e-14 ? std::clamp (nm[k + 1][m] / nm[k + 1][k + 1], -48.0, 48.0) : 0.0;
    }
}

// dB per pass, at the eight band centres, of the classic three-band absorption of a line of `len` samples (its two
// one-pole crossovers included), divided by the per-band multipliers: what law 1's loop filter is designed to
void bandLossDb (const Settings& s, double sr, double len, double out[engineBands]) noexcept
{
    const auto rt = std::max (0.01, s.gateSec > 0.0 ? std::min (s.rt60, 0.12) : s.rt60);
    const auto gain = [&] (double mult) { return std::min (maxLoopGain, std::pow (10.0, -3.0 * len / (rt * std::max (0.02, mult) * sr))); };
    const auto gM = gain (1.0), gL = gain (s.lowMult), gH = gain (s.highMult);
    const auto aLo = std::exp (-2.0 * pi * std::clamp (std::max (20.0, s.lowXover), 1.0, sr * 0.49) / sr);
    const auto aHi = std::exp (-2.0 * pi * std::clamp (std::max (20.0, s.highXover), 1.0, sr * 0.49) / sr);
    for (int b = 0; b < engineBands; ++b)
    {
        const auto w = 2.0 * pi * std::min (bandHz[b], 0.45 * sr) / sr;
        const auto cw = std::cos (w), sw = std::sin (w);
        // one-pole low-pass (1 − a) / (1 − a z⁻¹) at e^{jw}
        const auto lp = [&] (double a, double& re, double& im)
        {
            const auto dr = 1.0 - a * cw, di = a * sw, d = dr * dr + di * di;
            re = (1.0 - a) * dr / d;
            im = -(1.0 - a) * di / d;
        };
        double lr, li, hr, hi;
        lp (aLo, lr, li);
        lp (aHi, hr, hi);
        const auto re = gM + (gL - gM) * lr + (gH - gM) * (1.0 - hr), im = (gL - gM) * li - (gH - gM) * hi;
        const auto db = 10.0 * std::log10 (std::max (1e-12, re * re + im * im));
        out[b] = std::clamp (db / std::clamp (s.bandRtMult[b], 0.02, 50.0), -60.0, -1e-7);
    }
}
} // namespace

void bandTargets (const Settings& s, double sampleRate, double lineMs, double out[engineBands]) noexcept
{
    const auto len = lineMs * sampleRate / 1000.0;
    double db[engineBands];
    bandLossDb (s, sampleRate, len, db);
    // the loop's own one-pole band limits (both laws have them) take their share on top of the designed filter
    const auto onePoleDb = [&] (double hz, double at)
    {
        const auto a = std::exp (-2.0 * pi * hz / sampleRate), w = 2.0 * pi * std::min (at, 0.45 * sampleRate) / sampleRate;
        return 10.0 * std::log10 ((1.0 - a) * (1.0 - a) / (1.0 - 2.0 * a * std::cos (w) + a * a));
    };
    for (int b = 0; b < engineBands; ++b)
    {
        auto loss = db[b];
        if (s.loopBandwidth > 0.0 && s.loopBandwidth < 0.49 * sampleRate)
            loss += onePoleDb (s.loopBandwidth, bandHz[b]);
        if (s.loopLowCut > 0.0)
        {
            // the high-pass is the input minus its low-pass: a (1 − z⁻¹) / (1 − a z⁻¹)
            const auto a = std::exp (-2.0 * pi * s.loopLowCut / sampleRate), w = 2.0 * pi * std::min (bandHz[b], 0.45 * sampleRate) / sampleRate;
            loss += 10.0 * std::log10 (std::max (1e-12, a * a * (2.0 - 2.0 * std::cos (w)) / (1.0 - 2.0 * a * std::cos (w) + a * a)));
        }
        out[b] = -60.0 * len / (std::min (loss, -1e-7) * sampleRate);
    }
}

// =================================================================================================================
// laws
double skewLaw (double v, double lo, double hi, double k) noexcept
{
    return lo + (hi - lo) * std::pow (std::clamp (v, 0.0, 1.0), k);
}

double skewInverse (double unit, double lo, double hi, double k) noexcept
{
    return std::pow (std::clamp ((unit - lo) / (hi - lo), 0.0, 1.0), 1.0 / k);
}

double knotLaw (double v, const double* knots, int count) noexcept
{
    v = std::clamp (v, knots[0], knots[2 * (count - 1)]);
    for (int i = 1; i < count; ++i)
        if (v <= knots[2 * i] || i == count - 1)
        {
            const auto v0 = knots[2 * i - 2], v1 = knots[2 * i];
            const auto u0 = knots[2 * i - 1], u1 = knots[2 * i + 1];
            return v1 > v0 ? u0 + (u1 - u0) * (v - v0) / (v1 - v0) : u1;
        }
    return knots[1];
}

double knotInverse (double unit, const double* knots, int count) noexcept
{
    unit = std::clamp (unit, knots[1], knots[2 * count - 1]);
    for (int i = 1; i < count; ++i)
        if (unit <= knots[2 * i + 1] || i == count - 1)
        {
            const auto v0 = knots[2 * i - 2], v1 = knots[2 * i];
            const auto u0 = knots[2 * i - 1], u1 = knots[2 * i + 1];
            return u1 > u0 ? v0 + (v1 - v0) * (unit - u0) / (u1 - u0) : v0;
        }
    return knots[0];
}

int choice (float v, int count) noexcept
{
    if (count <= 1)
        return 0;
    return std::clamp ((int) std::lround (std::clamp ((double) v, 0.0, 1.0) * (count - 1)), 0, count - 1);
}

void mixGains (float mix, float& dry, float& wet) noexcept
{
    const auto m = std::clamp ((double) mix, 0.0, 1.0) * pi * 0.5;
    dry = (float) std::cos (m);
    wet = (float) std::sin (m);
    if (mix >= 1.0f)
        dry = 0.0f;
    if (mix <= 0.0f)
        wet = 0.0f;
}

// =================================================================================================================
// engine
struct Engine::Impl
{
    double sr = 48000.0, maxLineSec, maxPreSec;

    Line pre[2], raw[2], dif[2];
    Allpass inAp[2][4], earlyAp[2][2];
    Line lines[lineCount];
    Allpass loopAp[lineCount];
    struct Absorb
    {
        OnePole lo, hi, bw, lc;
        float gL = 1, gM = 1, gH = 1;
    } absorb[lineCount];
    double curLen[lineCount] {}; // glided line lengths (samples)
    double lfo[lineCount] {};
    Shifter shifters[4]; // inside lines 0, 2, 4, 6 (shimmer)
    float shiftHp[4][2] {}; // round tp172d: the shifted branch's two one-pole high-pass states (energy-keeping shimmer)
    Biquad outLow, outHigh, shelfLow, shelfHigh, toneHigh, toneMid, toneLow, band[maxBandSections];
    bool outLowOn = false, outHighOn = false, shelfLowOn = false, shelfHighOn = false, toneHighOn = false, toneMidOn = false, toneLowOn = false, bandOn = false;
    int bandSections = 0;

    // tap patterns (unit times and gains), drawn once
    double eT[2][earlyTaps] {}, eG[2][earlyTaps] {};
    double bT[2][buildTaps] {}, bG[2][buildTaps] {};
    int eD[2][earlyTaps] {}, bD[2][buildTaps] {};
    float eW[2][earlyTaps] {}, bW[2][buildTaps] {};

    Settings last;
    bool first = true;
    float shiftState = 0.0f;
    float apG[lineCount] {}, inG[4] {}, eApG = 0;
    double modDepth = 0, modInc[lineCount] {};
    double eModDepth = 0, eModInc = 0, eModPhase = 0; // round 170c: the early taps' modulation (samples, rad / sample)
    double feedbackGain = -1;

    // ---- law 1 (round 190bz) ----
    float shB0[loopShelves][lineCount] {}, shB1[loopShelves][lineCount] {}, shA1[loopShelves][lineCount] {}, shZ[loopShelves][lineCount] {};
    double loopKey[14 + lineCount] {};  // the settings and lengths the loop filters were designed for
    bool loopDesigned = false;
    Biquad geq[engineBands];
    bool geqOn[engineBands] {};
    double toneKey[engineBands] {};
    bool toneDesigned = false, toneOn = false;
    double lfoC[lineCount] {}, lfoS[lineCount] {}, lfoRc[lineCount] {}, lfoRs[lineCount] {}, lfoInc[lineCount] {};
    double mmC = 1.0, mmS = 0.0, mmRc = 1.0, mmRs = 0.0, mmInc = -1.0;
    unsigned renorm = 0;
    Line onsetLine[onsetSteps][onsetChans];
    Biquad bell[3];
    bool bellOn[3] {};
    int onsetD[onsetSteps][onsetChans] {};

    Impl (double maxLine, double maxPre) : maxLineSec (maxLine), maxPreSec (maxPre)
    {
        Lcg r { 0x2545F491u };
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < earlyTaps; ++k)
            {
                eT[c][k] = (k + 0.15 + 0.7 * r.next()) / earlyTaps;
                eG[c][k] = r.next() < 0.5 ? -1.0 : 1.0;
            }
            for (int k = 0; k < buildTaps; ++k)
            {
                bT[c][k] = (k + 0.1 + 0.8 * r.next()) / buildTaps;
                bG[c][k] = r.next() < 0.5 ? -1.0 : 1.0;
            }
        }
    }

    void prepare (double rate)
    {
        sr = rate;
        const auto ms = sr / 1000.0;
        for (int c = 0; c < 2; ++c)
        {
            pre[c].allocate ((int) (maxPreSec * sr) + 8);
            raw[c].allocate ((int) (1.05 * sr) + 8);
            dif[c].allocate ((int) (2.65 * sr) + 8); // (Halo's build reaches 2.42 s)
            for (int k = 0; k < 4; ++k)
            {
                inAp[c][k].len = std::max (1, (int) std::lround (inApMs[c][k] * ms));
                inAp[c][k].line.allocate (inAp[c][k].len + 2);
            }
            for (int k = 0; k < 2; ++k)
            {
                earlyAp[c][k].len = std::max (1, (int) std::lround (earlyApMs[c][k] * ms));
                earlyAp[c][k].line.allocate (earlyAp[c][k].len + 2);
            }
        }
        const auto maxLine = (int) (maxLineSec * sr) + 16;
        for (int i = 0; i < lineCount; ++i)
        {
            lines[i].allocate (maxLine + (int) (0.05 * sr));
            loopAp[i].line.allocate ((int) (0.2 * maxLineSec * sr) + 8);
        }
        for (auto& sh : shifters)
            sh.prepare (sr, 0.06);
        for (int k = 0; k < onsetSteps; ++k)
            for (auto& l : onsetLine[k])
                l.allocate ((int) (maxOnsetSec * onsetSpan[k] * sr) + 8);
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            pre[c].clear();
            raw[c].clear();
            dif[c].clear();
            for (auto& a : inAp[c])
                a.line.clear();
            for (auto& a : earlyAp[c])
                a.line.clear();
        }
        for (int i = 0; i < lineCount; ++i)
        {
            lines[i].clear();
            loopAp[i].line.clear();
            absorb[i].lo.z = absorb[i].hi.z = absorb[i].bw.z = absorb[i].lc.z = 0.0f;
            lfo[i] = 2.0 * pi * i / lineCount;
        }
        for (auto& sh : shifters)
            sh.clear();
        for (auto& h : shiftHp)
            h[0] = h[1] = 0.0f;
        outLow.reset();
        outHigh.reset();
        shelfLow.reset();
        shelfHigh.reset();
        for (auto& b : band)
            b.reset();
        toneHigh.reset();
        toneLow.reset();
        toneMid.reset();
        shiftState = 0.0f;
        first = true;
        for (int i = 0; i < lineCount; ++i)
        {
            lfoC[i] = std::cos (2.0 * pi * i / lineCount);
            lfoS[i] = std::sin (2.0 * pi * i / lineCount);
            lfoInc[i] = -1.0;
        }
        for (auto& row : shZ)
            for (auto& z : row)
                z = 0.0f;
        for (auto& b : geq)
            b.reset();
        for (auto& step : onsetLine)
            for (auto& l : step)
                l.clear();
        for (auto& b : bell)
            b.reset();
        mmC = 1.0;
        mmS = 0.0;
        mmInc = -1.0;
        renorm = 0;
        loopDesigned = toneDesigned = false;
    }

    double targetLen (const Settings& s, int i) const noexcept
    {
        const auto shape = std::clamp (s.shape, 0, shapeCount - 1);
        const auto spread = std::clamp (s.lineSpread, 0.0, 1.0);
        const auto ms = shape == 3 ? std::max (0.0, s.lineScale) * (1.0 - spread * (1.0 - shapes[3][i]))
                                   : shapes[shape][i] * std::max (0.0, s.lineScale);
        return clampPos (ms * sr / 1000.0, 8.0, maxLineSec * sr);
    }

    // coefficients that depend on the settings (once per block)
    void update (const Settings& s) noexcept
    {
        const auto ms = sr / 1000.0;
        for (int k = 0; k < 4; ++k)
            inG[k] = (float) (inApCoef[k] * std::clamp (s.inDiffusion, 0.0, 1.0));
        eApG = (float) (0.7 * std::clamp (s.earlyDiffusion, 0.0, 1.0));

        // tap delays and gains: early (energy-normalised, falling) and build-up (rising then falling, sum of squares 1)
        for (int c = 0; c < 2; ++c)
        {
            double e = 0, b = 0;
            for (int k = 0; k < earlyTaps; ++k)
            {
                eD[c][k] = std::clamp ((int) (eT[c][k] * std::max (0.0005, s.earlySec) * sr), 1, raw[c].mask - 4);
                const auto a = std::exp (-s.earlyDecay * eT[c][k]);
                eW[c][k] = (float) (a * eG[c][k]);
                e += a * a;
            }
            for (auto& w : eW[c])
                w = (float) (w / std::sqrt (std::max (e, 1e-9)));
            const auto gated = s.gateSec > 0.0;
            const auto span = gated ? s.gateSec : s.buildSec;
            for (int k = 0; k < buildTaps; ++k)
            {
                bD[c][k] = std::clamp ((int) (bT[c][k] * span * sr), 0, dif[c].mask - 4);
                const auto u = bT[c][k];
                const auto a = gated ? 0.6 + 0.4 * u
                                     : s.buildRise >= 1.0 ? std::pow (std::max (u, 1e-6), 0.5 * (s.buildRise - 1.0)) // round tp172d
                                                          : std::sin (pi * std::sqrt (u)); // build: rises, then falls
                bW[c][k] = (float) (a * bG[c][k]);
                b += a * a;
            }
            for (auto& w : bW[c])
                w = (float) (w / std::sqrt (std::max (b, 1e-9)));
        }

        // round 170c: the early taps' modulation (four sines, a tap on each in turn; never past the shortest tap)
        eModDepth = std::max (0.0, s.earlyModDepthMs) * ms;
        if (eModDepth > 0.0)
        {
            int shortest = raw[0].mask;
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < earlyTaps; ++k) shortest = std::min (shortest, eD[c][k]);
            eModDepth = std::min (eModDepth, std::max (0.0, 0.45 * (double) shortest - 1.0));
        }
        eModInc = 2.0 * pi * std::max (0.0, s.earlyModRateHz) / sr;

        // modulation
        modDepth = std::max (0.0, s.modDepthMs) * ms;
        for (int i = 0; i < lineCount; ++i)
            modInc[i] = 2.0 * pi * std::max (0.0, s.modRateHz) * (1.0 + 0.137 * i - 0.019 * i * i * 0.5) / sr;

        // loop filters
        for (int i = 0; i < lineCount; ++i)
        {
            auto& a = absorb[i];
            a.lo.setLowpass (std::max (20.0, s.lowXover), sr);
            a.hi.setLowpass (std::max (20.0, s.highXover), sr);
            if (s.loopBandwidth > 0.0 && s.loopBandwidth < 0.49 * sr)
                a.bw.setLowpass (s.loopBandwidth, sr);
            else
                a.bw.a = 0.0f;
            if (s.loopLowCut > 0.0)
                a.lc.setLowpass (s.loopLowCut, sr);
            else
                a.lc.a = 1.0f;
            apG[i] = (float) (0.6 * std::clamp (s.loopDiffusion, 0.0, 1.0));
        }

        // output filters
        outLowOn = s.outLowCut > 0.0;
        if (outLowOn)
        {
            if (s.outLowOrder == 1) // first order (bilinear)
            {
                const auto k = std::tan (pi * std::min (s.outLowCut, 0.45 * sr) / sr);
                outLow.set (1.0, -1.0, 0.0, 1.0 + k, k - 1.0, 0.0);
            }
            else
                outLow.filter (sr, 1, s.outLowCut, 0.7071);
        }
        outHighOn = s.outHighCut > 0.0 && s.outHighCut < 0.45 * sr;
        if (outHighOn)
        {
            if (s.outHighOrder == 1)
            {
                const auto k = std::tan (pi * s.outHighCut / sr);
                outHigh.set (k, k, 0.0, 1.0 + k, k - 1.0, 0.0);
            }
            else
                outHigh.filter (sr, 0, s.outHighCut, 0.7071);
        }
        bandOn = s.bandLimit > 0.0 && s.bandLimit < 0.45 * sr;
        if (bandOn)
        {
            // Butterworth of order 2 · sections: section k's Q = 1 / (2 cos ((2k + 1) π / (4 · sections)))
            bandSections = std::clamp (s.bandLimitSections, 1, maxBandSections);
            for (int k = 0; k < bandSections; ++k)
                band[k].filter (sr, 0, s.bandLimit, 1.0 / (2.0 * std::cos ((2 * k + 1) * pi / (4.0 * bandSections))));
        }
        toneLowOn = std::abs (s.toneLowDb) > 0.01;
        if (toneLowOn)
            toneLow.shelf (sr, s.toneLowHz, s.toneLowDb, false, 0.7);
        toneHighOn = std::abs (s.toneHighDb) > 0.01;
        if (toneHighOn)
            toneHigh.shelf (sr, std::min (s.toneHighHz, 0.45 * sr), s.toneHighDb, true, 0.7);
        toneMidOn = std::abs (s.toneMidDb) > 0.01;
        if (toneMidOn)
            toneMid.peaking (sr, std::min (s.toneMidHz, 0.45 * sr), std::max (0.1, s.toneMidQ), s.toneMidDb);
        shelfLowOn = std::abs (s.lowShelfDb) > 0.01;
        if (shelfLowOn)
            shelfLow.shelf (sr, s.lowShelfHz, s.lowShelfDb, false, 0.7);
        shelfHighOn = std::abs (s.highShelfDb) > 0.01;
        if (shelfHighOn)
            shelfHigh.shelf (sr, std::min (s.highShelfHz, 0.45 * sr), s.highShelfDb, true, 0.7);
    }

    // per-line gains for the current lengths (cheap: once per block)
    void decayGains (const Settings& s) noexcept
    {
        for (int i = 0; i < lineCount; ++i)
        {
            auto& a = absorb[i];
            const auto len = curLen[i] + (apG[i] != 0.0f ? (double) loopAp[i].len : 0.0);
            if (s.feedback >= 0.0)
            {
                const auto g = std::min (0.99995, s.feedback);
                a.gM = (float) g;
                a.gL = (float) std::min (0.99995, std::pow (g, 1.0 / std::max (0.05, s.lowMult)));
                a.gH = (float) std::min (0.99995, std::pow (g, 1.0 / std::max (0.05, s.highMult)));
                continue;
            }
            const auto rt = std::max (0.01, s.gateSec > 0.0 ? std::min (s.rt60, 0.12) : s.rt60);
            auto gain = [&] (double mult) { return (float) std::min (0.99995, std::pow (10.0, -3.0 * len / (rt * std::max (0.02, mult) * sr))); };
            a.gM = gain (1.0);
            a.gL = gain (s.lowMult);
            a.gH = gain (s.highMult);
        }
    }

    // ---- law 1 ---------------------------------------------------------------------------------------------------
    // the loop filter of every line: its dB per pass at the octave bands (the classic law's, times the per-band
    // multipliers) met by a gain and six shelves; redone only when a setting or a length it depends on moves
    void designLoop (const Settings& s) noexcept
    {
        double key[14 + lineCount] = { s.rt60, s.lowMult, s.lowXover, s.highMult, s.highXover, s.gateSec };
        for (int b = 0; b < engineBands; ++b)
            key[6 + b] = s.bandRtMult[b];
        for (int i = 0; i < lineCount; ++i)
            key[14 + i] = curLen[i] + (apG[i] != 0.0f ? (double) loopAp[i].len : 0.0);
        if (loopDesigned && std::equal (std::begin (key), std::end (key), std::begin (loopKey)))
            return;
        std::copy (std::begin (key), std::end (key), std::begin (loopKey));
        loopDesigned = true;
        double tc[loopShelves], tb[engineBands];
        for (int k = 0; k < loopShelves; ++k)
            tc[k] = std::tan (pi * std::min (loopShelfHz[k], 0.45 * sr) / sr);
        for (int b = 0; b < engineBands; ++b)
            tb[b] = std::tan (pi * std::min (bandHz[b], 0.45 * sr) / sr);
        for (int i = 0; i < lineCount; ++i)
        {
            double target[engineBands], shelf[loopShelves], gainDb = 0.0;
            bandLossDb (s, sr, key[14 + i], target);
            fitShelves (tc, loopShelves, tb, target, 1e-3, gainDb, shelf);
            // never above unity at any frequency: the highest point of the response on a grid of 40 frequencies from
            // 0 Hz to the top (the cascade of first-order shelves is smooth: between two grid points it cannot rise by
            // more than a small share of the shelves' gains, which is added)
            double top = -1000.0, sum = 0.0;
            for (int k = 0; k < loopShelves; ++k)
                sum += std::abs (shelf[k]);
            for (int j = 0; j < 40; ++j)
            {
                const auto t = j == 0 ? 0.0 : j == 39 ? 1.0e6 : std::tan (pi * std::min (10.0 * std::pow (2.0, 0.3 * j), 0.499 * sr) / sr);
                auto db = gainDb;
                for (int k = 0; k < loopShelves; ++k)
                    db += shelfDbAt (shelf[k], tc[k], t);
                top = std::max (top, db);
            }
            top += 0.03 * sum;
            const auto limit = 20.0 * std::log10 (maxLoopGain);
            if (top > limit)
                gainDb -= top - limit;
            const auto g = std::pow (10.0, gainDb / 20.0);
            for (int k = 0; k < loopShelves; ++k)
            {
                const auto r = std::pow (10.0, shelf[k] / 40.0), den = 1.0 + tc[k] / r, fold = k == 0 ? g : 1.0;
                shB0[k][i] = (float) (fold * (1.0 + tc[k] * r) / den);
                shB1[k][i] = (float) (fold * (tc[k] * r - 1.0) / den);
                shA1[k][i] = (float) ((tc[k] / r - 1.0) / den);
            }
        }
    }

    // the tone corrector: dB per octave band after the loop. A proportional graphic equaliser (a low shelf, six bells,
    // a high shelf); the bands overlap, so the gains are solved through their interaction at the band centres (two
    // rounds: the shapes change a little with the gains) — Välimäki & Liski 2017
    void setToneBand (int k, double db) noexcept
    {
        if (k == 0)
            geq[k].shelf (sr, 88.0, db, false, 1.0);
        else if (k == engineBands - 1)
            geq[k].shelf (sr, std::min (5657.0, 0.4 * sr), db, true, 1.0);
        else
            geq[k].peaking (sr, std::min (bandHz[k], 0.4 * sr), 1.25, db);
    }
    void designTone (const Settings& s) noexcept
    {
        if (toneDesigned && std::equal (std::begin (s.bandDb), std::end (s.bandDb), std::begin (toneKey)))
            return;
        std::copy (std::begin (s.bandDb), std::end (s.bandDb), std::begin (toneKey));
        toneDesigned = true;
        toneOn = false;
        double target[engineBands], g[engineBands];
        for (int b = 0; b < engineBands; ++b)
        {
            g[b] = target[b] = std::clamp (s.bandDb[b], -18.0, 18.0);
            toneOn = toneOn || std::abs (target[b]) > 0.01;
        }
        if (! toneOn)
            return;
        const auto magDb = [this] (const Biquad& q, double hz)
        {
            const auto w = 2.0 * pi * std::min (hz, 0.45 * sr) / sr;
            const auto c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
            const auto nr = q.b0 + q.b1 * c1 + q.b2 * c2, ni = -(q.b1 * s1 + q.b2 * s2);
            const auto dr = 1.0 + q.a1 * c1 + q.a2 * c2, di = -(q.a1 * s1 + q.a2 * s2);
            return 10.0 * std::log10 (std::max (1e-20, (nr * nr + ni * ni) / (dr * dr + di * di)));
        };
        for (int round = 0; round < 2; ++round)
        {
            double m[engineBands][engineBands + 1];
            for (int k = 0; k < engineBands; ++k)
            {
                const auto proto = std::abs (g[k]) < 0.5 ? (g[k] < 0.0 ? -0.5 : 0.5) : g[k];
                setToneBand (k, proto);
                for (int j = 0; j < engineBands; ++j)
                    m[j][k] = magDb (geq[k], bandHz[j]) / proto;
            }
            for (int j = 0; j < engineBands; ++j)
                m[j][engineBands] = target[j];
            for (int c = 0; c < engineBands; ++c)
            {
                int piv = c;
                for (int r = c + 1; r < engineBands; ++r)
                    if (std::abs (m[r][c]) > std::abs (m[piv][c]))
                        piv = r;
                if (std::abs (m[piv][c]) < 1e-12)
                    continue;
                if (piv != c)
                    for (int j = 0; j <= engineBands; ++j)
                        std::swap (m[piv][j], m[c][j]);
                for (int r = 0; r < engineBands; ++r)
                    if (r != c)
                    {
                        const auto f = m[r][c] / m[c][c];
                        for (int j = c; j <= engineBands; ++j)
                            m[r][j] -= f * m[c][j];
                    }
            }
            for (int k = 0; k < engineBands; ++k)
                g[k] = std::abs (m[k][k]) > 1e-12 ? std::clamp (m[k][engineBands] / m[k][k], -24.0, 24.0) : 0.0;
        }
        for (int k = 0; k < engineBands; ++k)
        {
            geqOn[k] = std::abs (g[k]) > 0.01;
            setToneBand (k, g[k]);
        }
    }

    void processLaw1 (float* L, float* R, int n, const Settings& s) noexcept
    {
        update (s);
        const auto pd = std::clamp ((int) std::lround (s.predelaySec * sr), 0, pre[0].mask - 2);
        double target[lineCount];
        for (int i = 0; i < lineCount; ++i)
        {
            // whole samples: a still line is then read without interpolation loss (the glide moves in quarter samples
            // and lands on them), so the top band decays as designed
            target[i] = std::floor (targetLen (s, i));
            if (first)
                curLen[i] = target[i];
            loopAp[i].len = std::clamp ((int) (target[i] * loopApFraction), 1, loopAp[i].line.mask - 2);
        }
        first = false;
        designLoop (s);
        designTone (s);
        // modulators as rotating pairs (one rotation per sample instead of a sine), the lines' rates as in law 0
        for (int i = 0; i < lineCount; ++i)
            if (modInc[i] != lfoInc[i])
            {
                lfoInc[i] = modInc[i];
                lfoRc[i] = std::cos (modInc[i]);
                lfoRs[i] = std::sin (modInc[i]);
            }
        const auto mmDepth = std::clamp (s.matrixModDepth, 0.0, 0.7);
        {
            const auto inc = 2.0 * pi * std::clamp (s.matrixModRateHz, 0.0, 20.0) / sr;
            if (inc != mmInc)
            {
                mmInc = inc;
                mmRc = std::cos (inc);
                mmRs = std::sin (inc);
            }
        }
        const auto glide = 0.25;
        const auto norm = (float) (1.0 / std::sqrt ((double) lineCount));
        const auto early = (float) s.earlyLevel, late = (float) s.lateLevel, send = (float) s.earlySend;
        const auto gated = s.gateSec > 0.0, build = gated || s.buildSec > 0.0005;
        const auto crossAngle = 0.25 * pi * std::clamp (s.crossFeed, 0.0, 1.0);
        const auto crossCos0 = std::cos (crossAngle), crossSin0 = std::sin (crossAngle);
        const auto mixA = 0.25 * pi * std::clamp (s.mixAngle, 0.0, 1.0);
        const auto mc = (float) std::cos (mixA), msn = (float) std::sin (mixA);
        const auto ratio = std::pow (2.0, s.shiftSemis / 12.0), ratio2 = std::pow (2.0, s.shiftSemis2 / 12.0);
        const auto voice2 = (float) std::clamp (s.shift2Mix, 0.0, 1.0);
        const auto shimmer = (float) (std::clamp (s.shiftFeedback, 0.0, 1.0) * std::clamp (s.shiftMix, 0.0, 1.0));
        const auto width = (float) std::clamp (s.width, -2.0, 2.0), gain = (float) s.gain;
        float shimmerKeep[2] = { 1.0f, 1.0f }, shimmerB[2] = { 0.0f, 0.0f };
        const auto hpC = (float) (1.0 - std::exp (-2.0 * pi * 120.0 / sr));
        if (s.shimmerEnergy > 0.0)
            for (int v = 0; v < 2; ++v)
            {
                const auto a = (double) (v == 1 ? shimmer * voice2 : shimmer);
                const auto f = std::min (1.0, std::abs (v == 1 ? s.shiftSemis2 : s.shiftSemis) / 2.0);
                shimmerB[v] = (float) (s.shimmerEnergy * a * f);
                shimmerKeep[v] = 1.0f - shimmerB[v] * (float) (1.0 - f);
            }
        // onset network
        const auto onsetLevel = (float) s.onsetLevel, onsetFeed = (float) std::clamp (s.onsetFeed, 0.0, 1.0);
        const auto onsetFront = (float) std::clamp (s.onsetFront, 0.0, 1.0);
        const auto onsetOn = onsetLevel != 0.0f || onsetFeed != 0.0f;
        if (onsetOn)
            for (int k = 0; k < onsetSteps; ++k)
                for (int j = 0; j < onsetChans; ++j)
                    onsetD[k][j] = std::clamp ((int) (std::clamp (s.onsetSec, 0.001, maxOnsetSec) * onsetSpan[k] * onsetFrac[k][j] * sr), 1,
                                               onsetLine[k][j].mask - 2);
        const auto bwOn = absorb[0].bw.a != 0.0f, lcOn = absorb[0].lc.a != 1.0f;
        constexpr float r2 = 0.70710678f; // (two inputs spread over four channels)
        constexpr float tiny = 1.0e-18f; // keeps the recirculating states out of the denormal range

        for (int t = 0; t < n; ++t)
        {
            float x[2] = { L[t], R[t] };
            float e[2], d[2], pin[2];
            for (int c = 0; c < 2; ++c)
            {
                pre[c].write (x[c]);
                const auto p = pd == 0 ? x[c] : pre[c].read (pd);
                pin[c] = p;
                raw[c].write (p);
                float er = 0.0f;
                if ((early != 0.0f || send != 0.0f) && eModDepth > 0.0)
                {
                    double eLfo[4];
                    for (int j = 0; j < 4; ++j)
                        eLfo[j] = 0.5 * eModDepth * (1.0 + std::sin (eModPhase + 1.5707963267948966 * j + 0.9 * c));
                    const auto longest = (double) (raw[c].mask - 3);
                    for (int k = 0; k < earlyTaps; ++k)
                        er += eW[c][k] * raw[c].readFrac (std::min (longest, (double) eD[c][k] + eLfo[k & 3]));
                }
                else if (early != 0.0f || send != 0.0f)
                    for (int k = 0; k < earlyTaps; ++k)
                        er += eW[c][k] * raw[c].read (eD[c][k]);
                if (eApG != 0.0f)
                    for (auto& a : earlyAp[c])
                        er = a.process (er, eApG);
                e[c] = er;
                auto v = p;
                for (int k = 0; k < 4; ++k)
                    if (inG[k] != 0.0f)
                        v = inAp[c][k].process (v, inG[k]);
                if (build)
                {
                    dif[c].write (v);
                    float b = 0.0f;
                    for (int k = 0; k < buildTaps; ++k)
                        b += bW[c][k] * (bD[c][k] == 0 ? v : dif[c].read (bD[c][k]));
                    v = b;
                }
                d[c] = v;
            }
            eModPhase += eModInc;
            if (eModPhase > 2.0 * pi)
                eModPhase -= 2.0 * pi;

            // onset: three diffusion steps on four channels
            float on[2] = { 0.0f, 0.0f }, onOut[2] = { 0.0f, 0.0f };
            if (onsetOn)
            {
                float oc[onsetChans] = { pin[0] * r2, pin[1] * r2, pin[0] * r2, pin[1] * r2 };
                float front[2] = { 0.0f, 0.0f };
                for (int k = 0; k < onsetSteps; ++k)
                {
                    float dl[onsetChans];
                    for (int j = 0; j < onsetChans; ++j)
                    {
                        onsetLine[k][j].write (oc[j]);
                        dl[j] = onsetSign[k][j] * onsetLine[k][j].read (onsetD[k][j]);
                    }
                    const auto a0 = dl[0] + dl[1], a1 = dl[0] - dl[1], a2 = dl[2] + dl[3], a3 = dl[2] - dl[3];
                    oc[0] = 0.5f * (a0 + a2);
                    oc[1] = 0.5f * (a1 + a3);
                    oc[2] = 0.5f * (a0 - a2);
                    oc[3] = 0.5f * (a1 - a3);
                    if (k + 1 < onsetSteps)
                    {
                        front[0] += oc[0];
                        front[1] += oc[1];
                    }
                }
                // two of the four channels (each holds every path), at the input's energy
                on[0] = 1.41421356f * oc[0];
                on[1] = 1.41421356f * oc[1];
                onOut[0] = onsetLevel * (on[0] + onsetFront * 1.41421356f * front[0]);
                onOut[1] = onsetLevel * (on[1] + onsetFront * 1.41421356f * front[1]);
            }

            // late tank
            float sOut[lineCount];
            for (int i = 0; i < lineCount; ++i)
            {
                curLen[i] += std::clamp (target[i] - curLen[i], -glide, glide);
                const auto c = lfoC[i] * lfoRc[i] - lfoS[i] * lfoRs[i];
                lfoS[i] = lfoS[i] * lfoRc[i] + lfoC[i] * lfoRs[i];
                lfoC[i] = c;
                const auto dl = std::max (2.0, curLen[i] - modDepth * 0.5 * (1.0 + lfoS[i]));
                auto y = lines[i].readFrac (dl);
                if (apG[i] != 0.0f)
                    y = loopAp[i].process (y, apG[i]);
                sOut[i] = y;
            }
            for (int k = 0; k < loopShelves; ++k)
                for (int i = 0; i < lineCount; ++i)
                {
                    const auto y = shB0[k][i] * sOut[i] + shZ[k][i];
                    shZ[k][i] = shB1[k][i] * sOut[i] - shA1[k][i] * y;
                    sOut[i] = y;
                }
            if (bwOn)
                for (int i = 0; i < lineCount; ++i)
                    sOut[i] = absorb[i].bw.lp (sOut[i]);
            if (lcOn)
                for (int i = 0; i < lineCount; ++i)
                    sOut[i] -= absorb[i].lc.lp (sOut[i]);

            float lateL = (sOut[0] + sOut[1] + sOut[2] - sOut[3] + sOut[4] + sOut[5] - sOut[6] + sOut[7]) * norm;
            float lateR = (sOut[0] - sOut[1] + sOut[2] + sOut[3] + sOut[4] - sOut[5] + sOut[6] + sOut[7]) * norm;
            if (s.splitSides)
            {
                lateL = (sOut[0] + sOut[1] + sOut[2] - sOut[3]) * norm * 1.41421356f;
                lateR = (sOut[4] - sOut[5] + sOut[6] + sOut[7]) * norm * 1.41421356f;
            }
            const float in[2] = { (1.0f - onsetFeed) * (d[0] + send * e[0]) + onsetFeed * on[0] + tiny,
                                  (1.0f - onsetFeed) * (d[1] + send * e[1]) + onsetFeed * on[1] + tiny };
            if (gated)
            {
                lateL = 0.3f * lateL + d[0];
                lateR = 0.3f * lateR + d[1];
            }

            // mixing: each half through two layers of rotations by mixAngle × 45° (at 45° a normalised 4-point
            // Hadamard up to signs), then the halves rotated into each other by crossFeed × 45°, swinging by the
            // matrix modulator (orthogonal for any amount)
            auto crossCos = (float) crossCos0, crossSin = (float) crossSin0;
            float hc[2] = { mc, mc }, hs[2] = { msn, msn };
            if (mmDepth > 0.0)
            {
                const auto c = mmC * mmRc - mmS * mmRs;
                mmS = mmS * mmRc + mmC * mmRs;
                mmC = c;
                const auto dlt = mmDepth * mmS, d2 = dlt * dlt;
                const auto cd = 1.0 - d2 * (0.5 - d2 * (1.0 / 24.0 - d2 * (1.0 / 720.0)));
                const auto sd = dlt * (1.0 - d2 * (1.0 / 6.0 - d2 * (1.0 / 120.0 - d2 * (1.0 / 5040.0))));
                crossCos = (float) (crossCos0 * cd - crossSin0 * sd);
                crossSin = (float) (crossSin0 * cd + crossCos0 * sd);
                // the rotations inside the halves swing too, the two halves in opposite ways
                hc[0] = (float) (mc * cd - msn * sd);
                hs[0] = (float) (msn * cd + mc * sd);
                hc[1] = (float) (mc * cd + msn * sd);
                hs[1] = (float) (msn * cd - mc * sd);
            }
            float h[lineCount];
            for (int half = 0; half < lineCount; half += 4)
            {
                const auto qc = hc[half / 4], qs = hs[half / 4];
                const auto a0 = qc * sOut[half] + qs * sOut[half + 1], a1 = qc * sOut[half + 1] - qs * sOut[half];
                const auto a2 = qc * sOut[half + 2] + qs * sOut[half + 3], a3 = qc * sOut[half + 3] - qs * sOut[half + 2];
                h[half] = qc * a0 + qs * a2;
                h[half + 2] = qc * a2 - qs * a0;
                h[half + 1] = qc * a1 + qs * a3;
                h[half + 3] = qc * a3 - qs * a1;
            }
            for (int i = 0; i < 4; ++i)
            {
                const auto a = h[i], b = h[i + 4];
                auto u = crossCos * a + crossSin * b, w = -crossSin * a + crossCos * b;
                if (shimmer > 0.0f && (i & 1) == 0)
                {
                    const auto second = i == 2 && voice2 > 0.0f;
                    const auto r = second ? ratio2 : ratio;
                    const auto amount = second ? shimmer * voice2 : shimmer;
                    if (s.shimmerEnergy > 0.0)
                    {
                        const auto bb = shimmerB[second ? 1 : 0], keep = shimmerKeep[second ? 1 : 0];
                        auto hp = [&] (int k, float v)
                        {
                            shiftHp[k][0] += hpC * (v - shiftHp[k][0]);
                            v -= shiftHp[k][0];
                            shiftHp[k][1] += hpC * (v - shiftHp[k][1]);
                            return v - shiftHp[k][1];
                        };
                        u = keep * u + bb * hp (i / 2, shifters[i / 2].process (u, r));
                        w = keep * w + bb * hp (2 + i / 2, shifters[2 + i / 2].process (w, r));
                    }
                    else
                    {
                        u = (1.0f - amount) * u + amount * shifters[i / 2].process (u, r);
                        w = (1.0f - amount) * w + amount * shifters[2 + i / 2].process (w, r);
                    }
                }
                const auto alt = (i & 1) == 0 ? 0.5f : -0.5f;
                lines[i].write (u + alt * in[0]);
                lines[i + 4].write (w + alt * in[1]);
            }
            if ((++renorm & 8191u) == 0) // the rotating pairs drift by rounding: back onto the unit circle
            {
                for (int i = 0; i < lineCount; ++i)
                {
                    const auto m = 1.0 / std::sqrt (lfoC[i] * lfoC[i] + lfoS[i] * lfoS[i]);
                    lfoC[i] *= m;
                    lfoS[i] *= m;
                }
                const auto m = 1.0 / std::sqrt (mmC * mmC + mmS * mmS);
                mmC *= m;
                mmS *= m;
            }

            auto ol = late * lateL + early * e[0] + onOut[0];
            auto orr = late * lateR + early * e[1] + onOut[1];
            const auto mid = 0.5f * (ol + orr), side = 0.5f * (ol - orr) * width;
            L[t] = (mid + side) * gain;
            R[t] = (mid - side) * gain;
        }
        if (toneOn)
            for (int k = 0; k < engineBands; ++k)
                if (geqOn[k])
                    geq[k].processBlock (L, R, n);
        for (int k = 0; k < 3; ++k)
        {
            bellOn[k] = std::abs (s.bellDb[k]) > 0.01;
            if (bellOn[k])
            {
                bell[k].peaking (sr, std::clamp (s.bellHz[k], 20.0, 0.45 * sr), std::clamp (s.bellQ[k], 0.1, 12.0), std::clamp (s.bellDb[k], -18.0, 18.0));
                bell[k].processBlock (L, R, n);
            }
        }
        outputFilters (L, R, n);
        last = s;
    }

    void outputFilters (float* L, float* R, int n) noexcept
    {
        if (outLowOn)
            outLow.processBlock (L, R, n);
        if (outHighOn)
            outHigh.processBlock (L, R, n);
        if (bandOn)
            for (int k = 0; k < bandSections; ++k)
                band[k].processBlock (L, R, n);
        if (toneHighOn)
            toneHigh.processBlock (L, R, n);
        if (toneLowOn)
            toneLow.processBlock (L, R, n);
        if (toneMidOn)
            toneMid.processBlock (L, R, n);
        if (shelfLowOn)
            shelfLow.processBlock (L, R, n);
        if (shelfHighOn)
            shelfHigh.processBlock (L, R, n);
    }

    void process (float* L, float* R, int n, const Settings& s) noexcept
    {
        if (s.law == 1 && s.feedback < 0.0 && ! s.parallelLines)
        {
            processLaw1 (L, R, n, s);
            return;
        }
        update (s);
        const auto pd = std::clamp ((int) std::lround (s.predelaySec * sr), 0, pre[0].mask - 2);
        // loop all-pass lengths follow the line lengths (fixed per block)
        for (int i = 0; i < lineCount; ++i)
        {
            const auto target = targetLen (s, i);
            if (first)
                curLen[i] = target;
            loopAp[i].len = std::clamp ((int) (target * loopApFraction), 1, loopAp[i].line.mask - 2);
        }
        first = false;
        decayGains (s);
        const auto glide = 0.25; // samples per sample: a length change glides like tape
        const auto norm = (float) (1.0 / std::sqrt ((double) lineCount));
        const auto early = (float) s.earlyLevel, late = (float) s.lateLevel, send = (float) s.earlySend;
        const auto gated = s.gateSec > 0.0, build = gated || s.buildSec > 0.0005;
        const auto parallel = s.parallelLines;
        const auto crossAngle = parallel ? 0.0 : 0.25 * pi * std::clamp (s.crossFeed, 0.0, 1.0);
        const auto crossCos = (float) std::cos (crossAngle), crossSin = (float) std::sin (crossAngle);
        const auto ratio = std::pow (2.0, s.shiftSemis / 12.0), ratio2 = std::pow (2.0, s.shiftSemis2 / 12.0);
        const auto voice2 = (float) std::clamp (s.shift2Mix, 0.0, 1.0);
        // shimmer: a part of what four of the lines write back goes through a pitch shifter. (1 − a)² + a² ≤ 1 and
        // the shifted part is decorrelated, so the loop keeps the decay the absorption filters set: always stable
        const auto shimmer = (float) (std::clamp (s.shiftFeedback, 0.0, 1.0) * std::clamp (s.shiftMix, 0.0, 1.0));
        const auto width = (float) std::clamp (s.width, -2.0, 2.0), gain = (float) s.gain;
        // round tp172d: the injected shimmer (Halo). The plain crossfade moved a share of every pass up an octave,
        // where the absorption takes it: a shimmer tail died in ~2 s where the reference's keeps the RT of its size
        // (the reference's RT barely depends on the shift). Here the line keeps all of itself and the shifted voice is
        // added on top (high-passed at 120 Hz: the grains' slow amplitude modulation would pile up below it), small
        // enough that the loop stays below unity at the longest RT; it fades in over the first 2 semitones, where
        // the voices are still correlated (unshifted, the feedback is the RT law's alone)
        float shimmerKeep[2] = { 1.0f, 1.0f }, shimmerB[2] = { 0.0f, 0.0f };
        const auto hpC = (float) (1.0 - std::exp (-2.0 * pi * 120.0 / sr));
        if (s.shimmerEnergy > 0.0)
            for (int v = 0; v < 2; ++v)
            {
                const auto a = (double) (v == 1 ? shimmer * voice2 : shimmer);
                const auto f = std::min (1.0, std::abs (v == 1 ? s.shiftSemis2 : s.shiftSemis) / 2.0);
                shimmerB[v] = (float) (s.shimmerEnergy * a * f);
                shimmerKeep[v] = 1.0f - shimmerB[v] * (float) (1.0 - f); // (unshifted: plain feedback, the RT law's own)
            }
        double target[lineCount];
        for (int i = 0; i < lineCount; ++i)
            target[i] = targetLen (s, i);

        for (int t = 0; t < n; ++t)
        {
            float x[2] = { L[t], R[t] };
            float e[2], d[2];
            for (int c = 0; c < 2; ++c)
            {
                pre[c].write (x[c]);
                const auto p = pd == 0 ? x[c] : pre[c].read (pd);
                raw[c].write (p);
                // early reflections
                float er = 0.0f;
                if ((early != 0.0f || send != 0.0f) && eModDepth > 0.0) // round 170c: modulated taps
                {
                    double eLfo[4];
                    for (int j = 0; j < 4; ++j)
                        eLfo[j] = 0.5 * eModDepth * (1.0 + std::sin (eModPhase + 1.5707963267948966 * j + 0.9 * c));
                    const auto longest = (double) (raw[c].mask - 3);
                    for (int k = 0; k < earlyTaps; ++k)
                        er += eW[c][k] * raw[c].readFrac (std::min (longest, (double) eD[c][k] + eLfo[k & 3]));
                }
                else if (early != 0.0f || send != 0.0f)
                    for (int k = 0; k < earlyTaps; ++k)
                        er += eW[c][k] * raw[c].read (eD[c][k]);
                if (eApG != 0.0f)
                    for (auto& a : earlyAp[c])
                        er = a.process (er, eApG);
                e[c] = er;
                // input diffusion
                auto v = p;
                for (int k = 0; k < 4; ++k)
                    if (inG[k] != 0.0f)
                        v = inAp[c][k].process (v, inG[k]);
                if (build)
                {
                    dif[c].write (v);
                    float b = 0.0f;
                    for (int k = 0; k < buildTaps; ++k)
                        b += bW[c][k] * (bD[c][k] == 0 ? v : dif[c].read (bD[c][k]));
                    v = b;
                }
                d[c] = v;
            }

            // late tank
            eModPhase += eModInc;
            if (eModPhase > 2.0 * pi)
                eModPhase -= 2.0 * pi;
            float sOut[lineCount];
            for (int i = 0; i < lineCount; ++i)
            {
                curLen[i] += std::clamp (target[i] - curLen[i], -glide, glide);
                lfo[i] += modInc[i];
                if (lfo[i] > 2.0 * pi)
                    lfo[i] -= 2.0 * pi;
                const auto dl = std::max (2.0, curLen[i] - modDepth * 0.5 * (1.0 + std::sin (lfo[i])));
                auto y = lines[i].readFrac (dl);
                if (apG[i] != 0.0f)
                    y = loopAp[i].process (y, apG[i]);
                auto& a = absorb[i];
                const auto lo = a.lo.lp (y), hiLp = a.hi.lp (y);
                y = a.gM * y + (a.gL - a.gM) * lo + (a.gH - a.gM) * (y - hiLp);
                if (a.bw.a != 0.0f)
                    y = a.bw.lp (y);
                if (a.lc.a != 1.0f)
                    y -= a.lc.lp (y);
                sOut[i] = y;
            }
            // outputs: two orthogonal sign patterns
            // (neither pattern sums to zero, so low frequencies, where the lines move together, are kept)
            float lateL = (sOut[0] + sOut[1] + sOut[2] - sOut[3] + sOut[4] + sOut[5] - sOut[6] + sOut[7]) * norm;
            float lateR = (sOut[0] - sOut[1] + sOut[2] + sOut[3] + sOut[4] - sOut[5] + sOut[6] + sOut[7]) * norm;
            if (s.splitSides) // two separate halves (round 135): each side hears only its own four lines
            {
                lateL = (sOut[0] + sOut[1] + sOut[2] - sOut[3]) * norm * 1.41421356f;
                lateR = (sOut[4] - sOut[5] + sOut[6] + sOut[7]) * norm * 1.41421356f;
            }

            const float in[2] = { d[0] + send * e[0], d[1] + send * e[1] };
            if (gated)
            {
                // gated: the dense tap block is the sound; the short tank only smooths it
                lateL = 0.3f * lateL + d[0];
                lateR = 0.3f * lateR + d[1];
            }

            // mixing: each half of the tank (lines 0–3 fed from the left, 4–7 from the right) through a normalised 4-point
            // Hadamard, then the halves rotated into each other by crossFeed × 45° (orthogonal for any amount)
            float h[lineCount];
            if (parallel)
                for (int i = 0; i < lineCount; ++i)
                    h[i] = sOut[i];
            for (int half = 0; half < lineCount && ! parallel; half += 4)
            {
                const auto a0 = sOut[half] + sOut[half + 1], a1 = sOut[half] - sOut[half + 1];
                const auto a2 = sOut[half + 2] + sOut[half + 3], a3 = sOut[half + 2] - sOut[half + 3];
                h[half] = 0.5f * (a0 + a2);
                h[half + 1] = 0.5f * (a1 + a3);
                h[half + 2] = 0.5f * (a0 - a2);
                h[half + 3] = 0.5f * (a1 - a3);
            }
            for (int i = 0; i < 4; ++i)
            {
                const auto a = h[i], b = h[i + 4];
                // parallel lines (plain echoes): input signs = the output patterns of their own side, so the left input
                // reaches only the left output and the right only the right
                constexpr float injL[4] = { 0.5f, 0.5f, 0.5f, -0.5f }, injR[4] = { 0.5f, -0.5f, 0.5f, 0.5f };
                auto u = crossCos * a + crossSin * b, w = -crossSin * a + crossCos * b;
                if (shimmer > 0.0f && (i & 1) == 0)
                {
                    // the second voice (dual modes) takes line 2 and line 6
                    const auto r = i == 2 && voice2 > 0.0f ? ratio2 : ratio;
                    const auto amount = i == 2 && voice2 > 0.0f ? shimmer * voice2 : shimmer;
                    if (s.shimmerEnergy > 0.0)
                    {
                        const auto b = shimmerB[i == 2 && voice2 > 0.0f ? 1 : 0], keep = shimmerKeep[i == 2 && voice2 > 0.0f ? 1 : 0];
                        auto hp = [&] (int k, float x)
                        {
                            shiftHp[k][0] += hpC * (x - shiftHp[k][0]);
                            x -= shiftHp[k][0];
                            shiftHp[k][1] += hpC * (x - shiftHp[k][1]);
                            return x - shiftHp[k][1];
                        };
                        u = keep * u + b * hp (i / 2, shifters[i / 2].process (u, r));
                        w = keep * w + b * hp (2 + i / 2, shifters[2 + i / 2].process (w, i == 2 && voice2 > 0.0f ? ratio2 : ratio));
                    }
                    else
                    {
                        u = (1.0f - amount) * u + amount * shifters[i / 2].process (u, r);
                        w = (1.0f - amount) * w + amount * shifters[2 + i / 2].process (w, i == 2 && voice2 > 0.0f ? ratio2 : ratio);
                    }
                }
                const auto alt = (i & 1) == 0 ? 0.5f : -0.5f; // reverbs: decorrelated sides from the first pass on
                lines[i].write (u + (parallel ? injL[i] : alt) * in[0]);
                lines[i + 4].write (w + (parallel ? injR[i] : alt) * in[1]);
            }

            auto ol = late * lateL + early * e[0];
            auto orr = late * lateR + early * e[1];
            // width (M/S)
            const auto mid = 0.5f * (ol + orr), side = 0.5f * (ol - orr) * width;
            L[t] = (mid + side) * gain;
            R[t] = (mid - side) * gain;
        }
        if (outLowOn)
            outLow.processBlock (L, R, n);
        if (outHighOn)
            outHigh.processBlock (L, R, n);
        if (bandOn)
            for (int k = 0; k < bandSections; ++k)
                band[k].processBlock (L, R, n);
        if (toneHighOn)
            toneHigh.processBlock (L, R, n);
        if (toneLowOn)
            toneLow.processBlock (L, R, n);
        if (toneMidOn)
            toneMid.processBlock (L, R, n);
        if (shelfLowOn)
            shelfLow.processBlock (L, R, n);
        if (shelfHighOn)
            shelfHigh.processBlock (L, R, n);
        last = s;
    }
};

Engine::Engine (double maxLineSec, double maxPredelaySec) : impl (std::make_unique<Impl> (maxLineSec, maxPredelaySec)) {}
Engine::~Engine() = default;
void Engine::prepare (double sampleRate) { impl->prepare (sampleRate); }
void Engine::reset() noexcept { impl->reset(); }
void Engine::process (float* left, float* right, int n, const Settings& s) noexcept { impl->process (left, right, n, s); }

// =================================================================================================================
// front-ends
namespace
{
// a 0..1 control as % text
std::string pct (double v) { char b[32]; std::snprintf (b, sizeof b, "%.0f%%", v * 100.0); return b; }
std::string fmt (const char* f, double v) { char b[48]; std::snprintf (b, sizeof b, f, v); return b; }
std::string hz (double v) { return v >= 1000.0 ? fmt ("%.2f kHz", v / 1000.0) : fmt ("%.0f Hz", v); }
std::string ms (double v) { return v >= 1000.0 ? fmt ("%.2f s", v / 1000.0) : fmt ("%.1f ms", v); }
std::string sec (double v) { return v >= 10.0 ? fmt ("%.1f s", v) : fmt ("%.2f s", v); }

// the decay a front-end asks for, with a floor for modes whose structure cannot decay faster (measured)
double withFloor (double rt, double floor) { return std::sqrt (rt * rt + floor * floor); }

// note lengths of the tempo-synced delays (quarter notes)
constexpr double noteQuarters[] = { 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0 };
const char* const noteNames[] = { "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };

// Delay time of a synced control set: sync 0 = ms, 1 = note, 2 = dotted, 3 = triplet
double syncedMs (int sync, int note, double msValue, double bpm)
{
    if (sync <= 0 || bpm <= 0.0)
        return msValue;
    auto q = noteQuarters[std::clamp (note, 0, 6)] * 60000.0 / bpm;
    if (sync == 2)
        q *= 1.5;
    else if (sync == 3)
        q *= 2.0 / 3.0;
    return q;
}

// ---- law 1 (round 190bz): what a mode / type adds on the 0.1.27 engine law, neutral by default. Fitted with the tail
// lab (scripts/taillab.py) to black-box renders of the reference each effect stands in for.
struct LawVoicing
{
    double rtMult[engineBands] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 }; // decay per octave band × the classic law's
    double db[engineBands] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };     // tone corrector, dB per octave band
    double gainDb = 0.0;                                                     // wet level
    double onset = 0.0, onsetMs = 30.0, front = 0.0, feed = 0.0;             // onset network: level, span, front weight, tank feed
    double early = 1.0;                                                      // × the classic early taps' level
    double angle = 1.0;                                                      // mixing inside the halves (× 45°)
    double mmDepth = 0.0, mmRate = 0.3;                                      // matrix modulation (rad, Hz)
    double mod = 1.0;                                                        // × the delay modulation's depth
    double bandLimit = 1.0;                                                  // × the steep band limit's frequency
    double bellHz[3] = { 100.0, 1000.0, 5000.0 }, bellDb[3] = { 0.0, 0.0, 0.0 }, bellQ[3] = { 1.4, 1.4, 1.4 }; // narrow colour
};
void applyLaw (Settings& s, const LawVoicing& v) noexcept
{
    s.law = 1;
    for (int b = 0; b < engineBands; ++b)
    {
        s.bandRtMult[b] = v.rtMult[b];
        s.bandDb[b] = v.db[b];
    }
    s.gain *= std::pow (10.0, v.gainDb / 20.0);
    s.onsetLevel = v.onset;
    s.onsetSec = v.onsetMs / 1000.0;
    s.onsetFront = v.front;
    s.onsetFeed = v.feed;
    s.earlyLevel *= v.early;
    s.mixAngle = v.angle;
    s.matrixModDepth = v.mmDepth;
    s.matrixModRateHz = v.mmRate;
    s.modDepthMs *= v.mod;
    s.bandLimit *= v.bandLimit;
    for (int k = 0; k < 3; ++k)
    {
        s.bellHz[k] = v.bellHz[k];
        s.bellDb[k] = v.bellDb[k];
        s.bellQ[k] = v.bellQ[k];
    }
}

// between two voicings (t = 0 … 1): decay multipliers and bell frequencies on a ratio scale, the rest linear
LawVoicing blendLaw (const LawVoicing& a, const LawVoicing& b, double t) noexcept
{
    t = std::clamp (t, 0.0, 1.0);
    const auto lin = [t] (double x, double y) { return x + (y - x) * t; };
    const auto geo = [t] (double x, double y) { return x > 0.0 && y > 0.0 ? x * std::pow (y / x, t) : x + (y - x) * t; };
    LawVoicing v;
    for (int i = 0; i < engineBands; ++i)
    {
        v.rtMult[i] = geo (a.rtMult[i], b.rtMult[i]);
        v.db[i] = lin (a.db[i], b.db[i]);
    }
    v.gainDb = lin (a.gainDb, b.gainDb);
    v.onset = lin (a.onset, b.onset);
    v.onsetMs = lin (a.onsetMs, b.onsetMs);
    v.front = lin (a.front, b.front);
    v.feed = lin (a.feed, b.feed);
    v.early = lin (a.early, b.early);
    v.angle = lin (a.angle, b.angle);
    v.mmDepth = lin (a.mmDepth, b.mmDepth);
    v.mmRate = geo (a.mmRate, b.mmRate);
    v.mod = geo (a.mod, b.mod);
    v.bandLimit = geo (a.bandLimit, b.bandLimit);
    for (int k = 0; k < 3; ++k)
    {
        v.bellHz[k] = geo (a.bellHz[k], b.bellHz[k]);
        v.bellDb[k] = lin (a.bellDb[k], b.bellDb[k]);
        v.bellQ[k] = geo (a.bellQ[k], b.bellQ[k]);
    }
    return v;
}

#ifdef LYNX_VERB_FIT
// the fitting harness: LYNX_VFIT="key=value;key=value" changes law 1's settings after the front-end has set them
void fitOverride (Settings& s)
{
    static const std::string spec = [] { const char* e = std::getenv ("LYNX_VFIT"); return std::string (e != nullptr ? e : ""); }();
    if (spec.empty() || s.law != 1)
        return;
    std::size_t at = 0;
    while (at < spec.size())
    {
        const auto end = std::min (spec.find (';', at), spec.size());
        const auto item = spec.substr (at, end - at);
        at = end + 1;
        const auto eq = item.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto k = item.substr (0, eq);
        const auto v = std::atof (item.substr (eq + 1).c_str());
        if (k.size() == 3 && k[0] == 'r' && k[1] == 't' && k[2] >= '0' && k[2] <= '7') s.bandRtMult[k[2] - '0'] *= v;
        else if (k.size() == 3 && k[0] == 'd' && k[1] == 'b' && k[2] >= '0' && k[2] <= '7') s.bandDb[k[2] - '0'] += v;
        else if (k == "gain") s.gain *= std::pow (10.0, v / 20.0);
        else if (k == "onset") s.onsetLevel = v;
        else if (k == "onsetms") s.onsetSec = v / 1000.0;
        else if (k == "front") s.onsetFront = v;
        else if (k == "feed") s.onsetFeed = v;
        else if (k == "early") s.earlyLevel *= v;
        else if (k == "late") s.lateLevel *= v;
        else if (k == "angle") s.mixAngle = v;
        else if (k == "mmd") s.matrixModDepth = v;
        else if (k == "mmr") s.matrixModRateHz = v;
        else if (k == "mod") s.modDepthMs *= v;
        else if (k == "indiff") s.inDiffusion *= v;
        else if (k == "loopdiff") s.loopDiffusion *= v;
        else if (k == "pre") s.predelaySec += v / 1000.0;
        else if (k == "build") s.buildSec *= v;
        else if (k == "buildms") s.buildSec = v / 1000.0;
        else if (k == "scale") s.lineScale *= v;
        else if (k == "rt") s.rt60 *= v;
        else if (k == "cross") s.crossFeed = v;
        else if (k == "width") s.width *= v;
        else if (k == "earlyms") s.earlySec = v / 1000.0;
        else if (k == "bl") s.bandLimit *= v;
        else if (k.size() == 3 && k[0] == 'b' && (k[1] == 'f' || k[1] == 'g' || k[1] == 'q') && k[2] >= '0' && k[2] <= '2')
            (k[1] == 'f' ? s.bellHz : k[1] == 'g' ? s.bellDb : s.bellQ)[k[2] - '0'] = v;
    }
}
#endif

class VerbEffect : public Effect
{
public:
    VerbEffect (EffectKind k, double maxLineSec, double maxPreSec) : kind (k), engine (maxLineSec, maxPreSec) {}

    void prepare (double sampleRate) noexcept override
    {
        rate = sampleRate;
        engine.prepare (sampleRate);
    }
    void reset() noexcept override
    {
        engine.reset();
        fade = Fade::none;
        started = false;
    }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        // round 173d: a new type / mode re-tunes the whole tank at once (line lengths, taps, band limits): the wet part
        // fades out on the old one, the new one starts and fades in (11 ms each way) instead of a click of 10…15 dB
        const auto st = structure (p);
        if (! started)
            lastStructure = st, started = true;
        if (st != lastStructure && fade == Fade::none)
        {
            fade = Fade::out;
            fadeLeft = fadeLength;
            held = last;
        }
        for (int done = 0; done < n;)
        {
            const auto m = fade == Fade::none ? n - done : std::min (n - done, fadeLeft);
            run (left + done, right + done, m, fade == Fade::out ? held : p, bpm);
            done += m;
            if (fade != Fade::none && (fadeLeft -= m) <= 0)
            {
                if (fade == Fade::out)
                    fade = Fade::in, fadeLeft = fadeLength, lastStructure = st;
                else
                    fade = Fade::none;
            }
        }
        last = p;
    }

protected:
    // the setting that rebuilds the tank's structure (its type / mode), −1 = none
    virtual int structure (const EffectParams&) const noexcept { return -1; }
    virtual void configure (const EffectParams& p, double bpm, Settings& s, float& mix) const noexcept = 0;
    virtual void preStage (float*, float*, int, const EffectParams&, double) noexcept {}
    virtual void postStage (float*, float*, int, const EffectParams&, double) noexcept {}

    void run (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept
    {
        Settings s;
        float mix = 1.0f;
        configure (p, bpm, s, mix);
       #ifdef LYNX_VERB_FIT
        fitOverride (s);
       #endif
        float dry, wet;
        mixGains (mix, dry, wet);
        for (int start = 0; start < n; start += chunk)
        {
            const auto m = std::min (chunk, n - start);
            auto* l = left + start;
            auto* r = right + start;
            std::copy (l, l + m, dl.begin());
            std::copy (r, r + m, dr.begin());
            preStage (l, r, m, p, bpm);
            engine.process (l, r, m, s);
            postStage (l, r, m, p, bpm);
            for (int i = 0; i < m; ++i)
            {
                auto w = wet;
                if (fade != Fade::none)
                {
                    const auto g = (float) (fadeLeft - start - i) / (float) fadeLength; // 1 … 0 over the fade
                    w *= fade == Fade::out ? g : 1.0f - g;
                }
                l[i] = dry * dl[(std::size_t) i] + w * l[i];
                r[i] = dry * dr[(std::size_t) i] + w * r[i];
            }
        }
    }

    static constexpr int chunk = 1024;
    static constexpr int fadeLength = 512;
    enum class Fade { none, out, in };
    Fade fade = Fade::none;
    int fadeLeft = 0, lastStructure = -1;
    bool started = false;
    EffectParams last {}, held {};
    EffectKind kind;
    Engine engine;
    double rate = 48000.0;
    std::array<float, chunk> dl {}, dr {};
};

// ---------------------------------------------------------------------------------------------- Epoch Verb
// Laws [EXEC, REA_rodada100 §1]: predelay 0..500 ms (v^2.32), decay 0.2..70 s (v^3.36), bass mult 0.25..4 X (v^2.32),
// bass crossover 100..10000 Hz (v^4.04), high shelf −24..0 dB, high freq / high cut 100..20000 Hz (v^1.754),
// mod rate 0.1..10 Hz, low cut 10..1500 Hz.
struct EpochMode
{
    const char* name;
    int shape;
    double scale, rtScale, rtFloor, lowRt, inDiff, early, earlyMs, buildMs, modScale, gain, preMs, gate;
};
// shape, line scale, RT / Decay, RT floor (s), bass RT ×, input diffusion, early level / span (ms), build-up (ms),
// modulation scale, output gain, own pre-delay (ms), gate (fraction of the decay; 0 = none) — fitted to §1.
// (LYNX_VERB_FIT: the fitting harness builds this file with the tables writable)
#ifdef LYNX_VERB_FIT
#define LYNX_FIT_TABLE
#else
#define LYNX_FIT_TABLE const
#endif
LYNX_FIT_TABLE EpochMode epochModes[epoch::modeCount] = {
    { "Concert Hall", 0, 0.9394, 1, 1.2, 1, 0.975, 0.4, 42.94, 0.0, 1, 1.031, 3.2, 0.0 },
    { "Plate", 2, 0.80, 0.90, 0.45, 1.0, 1.0, 0.10, 2.0, 60.0, 1.0, 0.70, 2.0, 0.0 },
    { "Room", 1, 0.85, 1.02, 0.45, 1.0, 1.0, 0.10, 20.0, 150.0, 1.0, 0.82, 1.0, 0.0 },
    { "Chamber", 1, 1.00, 1.04, 0.45, 1.0, 1.0, 0.15, 30.0, 140.0, 1.0, 0.70, 1.2, 0.0 },
    { "Random Space", 0, 1.10, 1.00, 0.90, 1.0, 1.0, 0.00, 1.0, 220.0, 2.0, 1.50, 15.0, 0.0 },
    { "Chorus Space", 0, 1.10, 1.00, 1.10, 1.0, 1.0, 0.05, 40.0, 280.0, 3.0, 0.70, 9.0, 0.0 },
    { "Ambience", 1, 0.70, 1.00, 0.45, 1.0, 1.0, 0.15, 40.0, 25.0, 1.0, 0.80, 7.5, 0.0 },
    { "Bright Hall", 0, 1.05, 1.15, 1.20, 1.0, 1.0, 0.50, 9.0, 0.0, 1.0, 1.00, 5.5, 0.0 },
    { "Nave", 0, 1.20, 1.00, 0.80, 1.0, 1.0, 0.35, 14.0, 20.0, 1.0, 0.62, 0.5, 0.0 },
    { "Grit Hall", 0, 1.00, 1.10, 1.20, 1.0, 1.0, 0.55, 9.0, 0.0, 1.3, 0.83, 5.0, 0.0 },
    { "Grit Plate", 2, 0.75, 0.92, 0.45, 1.0, 1.0, 0.10, 6.0, 40.0, 1.3, 1.25, 1.0, 0.0 },
    { "Smooth Plate", 2, 0.85, 1.12, 0.60, 1.0, 1.0, 0.40, 6.0, 0.0, 0.8, 0.46, 4.5, 0.0 },
    { "Smooth Room", 1, 0.95, 0.97, 0.70, 1.0, 1.0, 0.30, 8.0, 6.0, 0.8, 0.53, 9.0, 0.0 },
    { "Smooth Space", 0, 1.00, 1.04, 0.80, 1.0, 1.0, 0.25, 8.0, 6.0, 0.8, 0.81, 7.5, 0.0 },
    { "Gated", 1, 0.60, 0.62, 0.30, 1.0, 1.0, 0.10, 4.0, 0.0, 0.5, 0.90, 1.5, 0.55 },
    { "Drift Chamber", 1, 1.00, 1.08, 0.70, 1.0, 1.0, 0.40, 6.0, 0.0, 2.5, 0.43, 4.5, 0.0 },
    { "Drift Hall", 0, 1.00, 1.13, 1.20, 1.0, 1.0, 0.45, 9.0, 0.0, 2.5, 0.53, 5.0, 0.0 },
    { "Drift Space", 0, 1.30, 1.70, 1.20, 1.0, 1.0, 0.05, 9.0, 110.0, 2.5, 1.00, 4.0, 0.0 },
    { "Cathedral", 0, 1.49, 1, 0.8762, 1.203, 1, 0.4, 84.68, 39.77, 1, 0.756, 0.0, 0.0 },
    { "Grand Hall", 0, 1.50, 1.30, 2.40, 1.7, 1.0, 0.20, 30.0, 20.0, 1.0, 1.30, 55.0, 0.0 },
    { "Vintage Chamber", 1, 1.10, 1.20, 0.90, 1.0, 1.0, 0.05, 25.0, 110.0, 0.7, 1.40, 11.0, 0.0 },
    { "Vintage Hall", 0, 1.00, 1.08, 1.20, 1.0, 1.0, 0.50, 9.0, 0.0, 0.7, 0.95, 5.0, 0.0 },
};
const char* const epochColours[epoch::colourCount] = { "1970s", "1980s", "Now" };
// colour: loop bandwidth (Hz, 0 = none), output high cut (Hz, first order, 0 = none), steep band limit (Hz, 0 = none) and
// its Butterworth sections
struct EpochColour
{
    double loopBw, outCut, bandLimit;
    int sections;
};
LYNX_FIT_TABLE EpochColour epochColours2[3] = { { 9000.0, 0.0, 11800.0, 16 }, { 14000.0, 16000.0, 0.0, 2 }, { 0.0, 0.0, 0.0, 2 } };

// Round 124 (REA_rodada124_extras §3): each algorithm's own voicing, fitted to the reference at her saved settings
// (time–frequency energy of the impulse response and RT60 per octave). Neutral for the modes not refitted.
struct EpochVoicing
{
    double bassExp = 0.35;          // bass multiplier law below 1 × (RT × mult^bassExp)
    double highMult = 1.0;          // × the high-band RT
    double loopBwScale = 1.0;       // × the colour's loop bandwidth
    double earlyDecay = 3.0;        // early taps' fall over their span (e-folds)
    double toneLowHz = 200.0, toneLowDb = 0.0;
    double toneMidHz = 1000.0, toneMidDb = 0.0, toneMidQ = 0.7;
    double toneHighHz = 8000.0, toneHighDb = 0.0;
    // the mid-band RT60 (s) the reference measures at Size 100 % for each Decay (s): (decay, rt) pairs, increasing;
    // empty = the round-100 law (Decay × RT / Decay with the floor)
    int rtKnotCount = 0;
    double rtKnots[2 * 16] {};
};
LYNX_FIT_TABLE EpochVoicing epochVoicing[epoch::modeCount] = {
    /* Concert Hall */ { 0.0, 1.88, 1.455, 0.5, 155.8, -0.6, 1455, -0.6, 2.45, 4852, 1.5, 8, { 0.5, 1.38, 1, 1.6, 1.78, 2.27, 2, 2.46, 2.67, 3.17, 3.28, 3.7, 4, 4.63, 8, 8.68 } },
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    /* Cathedral */ { 0.875, 0.76, 1.868, 0.0, 61.95, -1.8, 200, -0.6, 0.2, 1.164e+04, -3, 13, { 0.53, 1.15, 1, 2.2, 1.85, 2.54, 1.99, 2.72, 2.5, 3.28, 2.9, 3.7, 3.87, 4.62, 4.36, 5.07, 5.15, 5.76, 6.38, 6.85, 7.37, 7.73, 12, 11.86, 14, 13.68 } },
    {},
    {},
    {},
};

// round 190bz: law 1 per mode
// fitted with the tail lab (round 190bz; the reference and the scores per row: docs/PARITY.md); a neutral entry is the classic voicing on the new loop
LYNX_FIT_TABLE LawVoicing epochLaw[epoch::modeCount] = {
    { { 0.998, 1.001, 0.969, 0.971, 0.957, 0.928, 0.881, 0.770 }, { -2.03, -0.49, -0.60, -1.21, -1.43, -0.37, 1.07, 1.53 }, 2.22, 0.000, 30, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 1.00, 1.00, { 100, 1000, 5000 }, { 0.00, 0.00, 0.00 }, { 1.4, 1.4, 1.4 } }, // Concert Hall
    {},
    { { 0.894, 1.486, 0.845, 1.106, 1.151, 1.092, 2.405, 4.000 }, { -1.29, -2.74, -2.79, -0.81, -1.29, -0.33, 1.15, 6.09 }, 1.67, 0.300, 45, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 4.00, 1.00, { 100, 1000, 5000 }, { 0.00, 0.00, 0.00 }, { 1.4, 1.4, 1.4 } }, // Room
};

double epochDecay (float v) { return skewLaw (v, 0.2, 70.0, 3.3596); }
double epochPredelay (float v) { return skewLaw (v, 0.0, 500.0, 2.3219); }
double epochBassMult (float v) { return skewLaw (v, 0.25, 4.0, 2.3219); }
double epochBassXover (float v) { return skewLaw (v, 100.0, 10000.0, 4.0444); }
double epochFreq (float v) { return skewLaw (v, 100.0, 20000.0, 1.7540); }

class EpochFx final : public VerbEffect
{
public:
    EpochFx() : VerbEffect (EffectKind::epochVerb, 0.25, 0.62) {}

protected:
    int structure (const EffectParams& p) const noexcept override { return choice (p[16], epoch::modeCount) + (p[epoch::engineLaw] >= 0.5f ? 1000 : 0); }
    void configure (const EffectParams& p, double, Settings& s, float& mix) const noexcept override
    {
        const auto modeIndex = choice (p[16], epoch::modeCount);
        const auto& m = epochModes[modeIndex];
        const auto& vo = epochVoicing[modeIndex];
        const auto colour = choice (p[15], epoch::colourCount);
        const auto& co = epochColours2[colour];
        mix = p[0];
        const auto size = (double) p[3];
        // a very slow attack also delays the onset (the reference: +45 ms at 100 %)
        s.predelaySec = (epochPredelay (p[1]) + m.preMs * (0.4 + 0.6 * size) + 225.0 * std::max (0.0, p[4] - 0.8)) / 1000.0;
        const auto decay = epochDecay (p[2]);
        s.shape = m.shape;
        s.lineScale = m.scale * (0.3 + 0.7 * size);
        if (vo.rtKnotCount >= 2)
        {
            // measured RT per Decay, scaled like the law below for Size and late diffusion
            const auto rt = knotLaw (std::clamp (decay, vo.rtKnots[0], vo.rtKnots[2 * (vo.rtKnotCount - 1)]), vo.rtKnots, vo.rtKnotCount);
            const auto beyond = decay > vo.rtKnots[2 * (vo.rtKnotCount - 1)] ? decay / vo.rtKnots[2 * (vo.rtKnotCount - 1)] : 1.0;
            s.rt60 = rt * beyond * m.rtScale * (0.85 + 0.15 * size) * (0.84 + 0.16 * p[10]);
        }
        else
            s.rt60 = withFloor (decay * m.rtScale, m.rtFloor * (0.4 + 0.6 * size)) * (0.84 + 0.16 * p[10]);
        const auto bass = epochBassMult (p[5]);
        s.lowMult = (bass < 1.0 ? std::pow (bass, vo.bassExp) : bass) * m.lowRt;
        s.lowXover = epochBassXover (p[6]);
        const auto shelf = -24.0 + 24.0 * p[7];
        s.highXover = epochFreq (p[8]);
        s.highMult = 1.1 * std::pow (10.0, std::max (shelf, -14.0) / 50.0) * vo.highMult;
        s.highShelfHz = s.highXover;
        s.highShelfDb = std::max (shelf, -16.0);
        s.inDiffusion = m.inDiff * p[9];
        s.loopDiffusion = 0.2 + 0.8 * p[10];
        s.modRateHz = 0.1 + 9.9 * p[11];
        s.modDepthMs = 0.6 * m.modScale * p[12] / std::sqrt (std::max (0.3, s.modRateHz));
        s.outHighCut = epochFreq (p[13]);
        s.outHighOrder = 1;
        if (co.outCut > 0.0)
            s.outHighCut = std::min (s.outHighCut, co.outCut);
        s.loopBandwidth = co.loopBw * vo.loopBwScale;
        s.bandLimit = co.bandLimit;
        s.bandLimitSections = co.sections;
        s.earlyDecay = vo.earlyDecay;
        s.lowShelfHz = vo.toneLowHz;
        s.lowShelfDb = vo.toneLowDb;
        s.toneMidHz = vo.toneMidHz;
        s.toneMidDb = vo.toneMidDb;
        s.toneMidQ = vo.toneMidQ;
        s.toneHighHz = vo.toneHighHz;
        s.toneHighDb = vo.toneHighDb;
        s.outLowCut = 10.0 + 1490.0 * p[14];
        s.outLowOrder = 1;
        s.earlyLevel = m.early;
        s.earlySec = m.earlyMs / 1000.0 * (0.4 + 0.6 * size);
        s.buildSec = m.buildMs / 1000.0 * (0.25 + 1.5 * p[4]);
        s.gateSec = m.gate > 0.0 ? m.gate * decay + 0.05 : 0.0;
        s.gain = m.gain;
        // round 183b (the reference's impulse at −20 dB, 17 settings; it is linear): its first 80 ms hold more of the
        // energy at longer decays (−3.8 dB of the total at its default 4 s, ours −7.2; at 2 s round 100's −3.1 dB in the
        // first 100 ms still holds), its modes sit denser low down (lines × 0.85: the −20 dB impulses and music both
        // closer), and it is 0.7 dB lower
        s.lineScale *= 0.85;
        s.earlyLevel *= std::min (1.6, 1.13 * std::sqrt (std::max (1.0, decay / 2.0)));
        s.gain *= 0.92;
        if (p[epoch::engineLaw] >= 0.5f)
            applyLaw (s, epochLaw[modeIndex]);
    }
};

// ---------------------------------------------------------------------------------------------- Atrium
// Laws: like the reference room (linear: predelay 0..500 ms, high cut 100..15000 Hz, sizes / crosses 0..1, mod rates
// 0.05..5 Hz, bass mult 0.5..2 X, bass xover 100..10000 Hz, high mult 0.1..1 X, high xover 100..15000 Hz, early size
// 1..1000 ms, low cut 0..1000 Hz) except Decay: 0.1..100 s with 2 s at the middle (the reference is linear).
struct AtriumType
{
    const char* name;
    int shape;
    double scale, rtScale, inDiff, early, lateGain, cut, modScale;
};
const AtriumType atriumTypes[atrium::typeCount] = {
    { "Large Room", 1, 1.00, 1.00, 1.0, 1.00, 1.00, 0.0, 1.0 },
    { "Medium Room", 1, 0.80, 1.00, 1.0, 1.00, 1.00, 0.0, 1.0 },
    { "Bright Room", 1, 0.90, 0.88, 0.9, 0.80, 0.90, 0.0, 1.0 },
    { "Large Chamber", 1, 1.20, 1.00, 1.0, 0.85, 1.05, 0.0, 1.0 },
    { "Dark Room", 1, 1.00, 1.00, 1.0, 1.00, 1.00, 0.0, 1.0 },
    { "Dark Chamber", 1, 1.15, 1.00, 1.0, 0.90, 1.00, 11000.0, 1.0 },
    { "Dark Space", 0, 1.10, 1.00, 1.0, 0.90, 1.00, 10000.0, 1.0 },
    { "Deep Space", 0, 1.20, 1.00, 1.0, 0.85, 1.00, 10500.0, 1.2 },
    { "Mirror Space", 0, 0.90, 1.00, 0.7, 1.10, 1.00, 10500.0, 1.0 },
    { "Vast Space", 0, 1.30, 1.00, 0.8, 1.00, 1.00, 10000.0, 1.2 },
    { "Distant Space", 0, 1.25, 1.00, 1.0, 0.70, 1.10, 10500.0, 1.0 },
    { "Dense Room", 1, 1.00, 1.00, 1.0, 1.05, 1.00, 10500.0, 1.0 },
};

// round 190bz: law 1 per type
// fitted with the tail lab (round 190bz; the reference and the scores per row: docs/PARITY.md); a neutral entry is the classic voicing on the new loop
LYNX_FIT_TABLE LawVoicing atriumLaw[atrium::typeCount] = {
    { { 1.161, 0.971, 1.153, 0.990, 1.001, 1.113, 1.069, 1.374 }, { 1.17, 0.78, 1.30, 1.29, -0.71, 0.20, -0.93, -1.68 }, -1.09, 0.650, 30, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 1.00, 1.00, { 100, 159, 5000 }, { 1.26, -1.21, 0.00 }, { 2.0, 2.0, 1.4 } }, // Large Room
    {},
    { { 0.695, 0.920, 0.713, 0.900, 0.910, 1.075, 1.257, 1.773 }, { 2.40, 1.43, 0.65, 1.22, -1.46, -0.47, -1.43, -1.80 }, 0.40, 0.650, 30, 0.00, 0.00, 1.00, 0.60, 0.00, 0.30, 2.50, 1.00, { 100, 1000, 5000 }, { 0.00, 0.00, 0.00 }, { 1.4, 1.4, 1.4 } }, // Bright Room
};

double atriumDecaySec (float v) { return skewLaw (v, 0.1, 100.0, 5.717); }

class AtriumFx final : public VerbEffect
{
public:
    AtriumFx() : VerbEffect (EffectKind::atrium, 0.25, 1.05) {}

protected:
    int structure (const EffectParams& p) const noexcept override { return choice (p[19], atrium::typeCount) + (p[atrium::engineLaw] >= 0.5f ? 1000 : 0); }
    void configure (const EffectParams& p, double, Settings& s, float& mix) const noexcept override
    {
        const auto& t = atriumTypes[choice (p[19], atrium::typeCount)];
        mix = p[0];
        s.predelaySec = 0.5 * p[1];
        s.rt60 = atriumDecaySec (p[2]) * t.rtScale;
        s.outHighCut = 100.0 + 14900.0 * p[3];
        s.bandLimit = t.cut;
        const auto earlyLate = (double) p[4]; // 0 = early only, 1 = late only
        // round 183b: an equal-power crossfade (the reference's total level moves only 2.6 dB from early only to late
        // only: −28.7 / −27.2 / −26.1 dB at 0 / 50 / 100 %, impulse at −20 dB; the old linear one moved 10.7 dB). The
        // late level holds over Decay (the reference: −27.2 / −26.8 dB at RT 2.1 / 8.6 s, ours rose with the RT) and
        // over Late size (0 % ran +3 dB, 100 % −1.2 dB)
        const auto fade = 0.5 * pi * earlyLate;
        s.earlyLevel = t.early * 0.92 * 1.46 * std::cos (fade);
        const auto lateSizeDb = p[5] <= 0.5f ? -3.1 * (1.0 - 2.0 * p[5]) : 2.4 * (p[5] - 0.5);
        s.lateLevel = t.lateGain * 3.8 * 0.575 * std::sin (fade) * std::sqrt (2.1 / std::max (0.1, atriumDecaySec (p[2]) * t.rtScale))
                    * std::pow (10.0, lateSizeDb / 20.0);
        s.shape = t.shape;
        s.lineScale = t.scale * (0.35 + 0.9 * p[5]);
        s.crossFeed = 0.35 + 0.65 * p[6];
        s.modRateHz = 0.05 + 4.95 * p[7];
        s.modDepthMs = 0.8 * t.modScale * p[8] / std::sqrt (std::max (0.3, s.modRateHz));
        s.lowMult = 0.5 + 1.5 * p[9];
        s.lowXover = 100.0 + 9900.0 * p[10];
        s.highMult = 1.15 * (0.1 + 0.9 * p[11]); // (+15 %: what interpolation and diffusion take away)
        s.highXover = 100.0 + 14900.0 * p[12];
        s.earlySec = (1.0 + 999.0 * p[13]) / 1000.0;
        s.earlyDecay = 2.0 + 3.0 * p[14];
        s.earlyDiffusion = 0.3 + 0.7 * p[14];
        s.earlySend = p[17];
        // round 170c: Early mod rate / depth were read by nothing (the "shell" audit); the early taps now move like the
        // late lines (same laws: 0.05 … 5 Hz, depth falling with the rate)
        s.earlyModRateHz = 0.05 + 4.95 * p[15];
        s.earlyModDepthMs = 0.8 * t.modScale * p[16] / std::sqrt (std::max (0.3, s.earlyModRateHz));
        s.inDiffusion = t.inDiff * p[18];
        s.loopDiffusion = 0.3 + 0.6 * p[18];
        const auto space = (double) p[20];
        s.buildSec = 0.02 + 0.06 * space;
        s.gain = 0.47 * (1.0 + 0.6 * space) * 1.25 * 1.18; // round 172d: +2 dB with the crossfade below (music / drums RMS within 1 dB at the defaults); 183b: +1.4 dB with the equal-power fade and the lower shelf
        // round 172d: the reference's late field holds far more below 100 Hz (impulse and music, octave bands: 31 / 63 Hz
        // +8 … +15 dB at the defaults) than this tank's few low modes give
        s.lowShelfHz = 90.0;
        s.lowShelfDb = 5.0; // round 183b: +10 dB was 4 … 7 dB too much below 80 Hz with the louder early part (impulse at −20 dB, music)
        s.toneMidHz = 140.0; // (the tank's own hump around 125 … 160 Hz taken back)
        s.toneMidDb = -5.0;
        s.toneMidQ = 1.4;
        s.highShelfHz = 9000.0; // and keeps more air above 9 kHz (third octaves 10 … 16 kHz −4 … −7 dB without it)
        s.highShelfDb = 4.0;
        s.outLowCut = 1000.0 * p[21];
        s.outLowOrder = 1;
        if (s.outLowCut < 5.0)
            s.outLowCut = 0.0;
        if (p[atrium::engineLaw] >= 0.5f)
            applyLaw (s, atriumLaw[choice (p[19], atrium::typeCount)]);
    }
};

// ---------------------------------------------------------------------------------------------- Foil Plate
// Laws: predelay 0..500 ms (v^2.32), decay 0.5..30 s (measured knots), size / width 0..200 %, mod rate 0.05..5 Hz
// (v^2.38), low EQ 20..2000 Hz, high EQ 200..20000 Hz, EQ gains ±12 dB.
constexpr double foilDecayKnots[] = { 0.0, 0.5, 0.5, 3.0, 0.75, 5.9, 0.775, 6.7, 0.8, 8.0, 0.825, 9.4, 0.85, 11.3, 0.875, 13.1,
                                      0.9, 15.7, 0.925, 18.4, 0.95, 21.7, 0.975, 25.8, 1.0, 30.0 };
constexpr int foilDecayCount = (int) (sizeof (foilDecayKnots) / sizeof (double) / 2);
struct FoilType
{
    const char* name;
    double scale, preMs, lowRt, damp, bw, early, gain;
};
// plate types: line scale, own onset (ms), bass RT ×, high-frequency absorption (1/s at 8 kHz), band limit (Hz, 0 = full band),
// early level, gain
const FoilType foilTypes[foil::typeCount] = {
    { "Silver", 1.00, 20.5, 1.0, 0.34, 11000.0, 0.35, 1.00 },
    { "Pewter", 0.85, 1.0, 1.0, 0.30, 11000.0, 0.45, 1.13 },
    { "Nickel", 1.10, 56.0, 1.0, 0.25, 11500.0, 0.35, 0.83 },
    { "Bronze", 0.90, 17.0, 1.0, 0.30, 11000.0, 0.55, 1.05 },
    { "Bismuth", 1.20, 6.5, 1.0, 0.20, 11000.0, 0.30, 1.00 },
    { "Tin", 1.00, 18.5, 1.0, 0.32, 11000.0, 0.35, 1.13 },
    { "Zinc", 1.30, 6.5, 1.1, 0.18, 11300.0, 0.30, 1.16 },
    { "Platinum", 1.20, 81.0, 1.3, 0.35, 10800.0, 0.25, 0.62 },
    { "Tungsten", 1.00, 21.0, 1.0, 0.30, 0.0, 0.35, 0.68 },
    { "Iron", 1.10, 56.0, 1.1, 0.30, 0.0, 0.30, 0.60 },
    { "Gold", 1.00, 20.5, 1.0, 0.30, 0.0, 0.35, 0.68 },
    { "Lead", 1.00, 1.5, 1.0, 0.45, 11000.0, 0.40, 1.00 },
};
double foilDecaySec (float v) { return knotLaw (v, foilDecayKnots, foilDecayCount); }
// round 190bz: law 1 per plate type, and for the measured plate voicing
// fitted with the tail lab (round 190bz; the reference and the scores per row: docs/PARITY.md); a neutral entry is the classic voicing on the new loop
LYNX_FIT_TABLE LawVoicing foilLaw[foil::typeCount] = {
    { { 1.062, 0.891, 0.982, 0.948, 0.906, 0.886, 0.789, 0.832 }, { -1.25, -1.92, -1.60, -1.46, -0.93, -0.35, 1.42, -1.23 }, 2.01, 0.150, 30, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 1.00, 1.00, { 10159, 8064, 5000 }, { 1.95, -1.15, 0.00 }, { 2.0, 2.0, 1.4 } }, // Silver
    { { 1.111, 0.990, 1.033, 0.957, 0.940, 0.947, 0.889, 1.042 }, { -1.42, -0.59, -0.09, -0.79, 0.47, -0.56, 0.42, -4.09 }, -0.29, 0.650, 30, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 1.00, 1.00, { 10159, 400, 5000 }, { 1.90, -1.79, 0.00 }, { 2.0, 2.0, 1.4 } }, // Pewter
};
// the measured plate voicing: fitted at three decay times, blended between them on the decay's logarithm (the third
// bell fills a 2 … 3 dB dip around 190 Hz that every setting showed against the reference)
constexpr double foilMeasuredLawSec[3] = { 1.0, 3.0, 10.0 };
LYNX_FIT_TABLE LawVoicing foilMeasuredLaw[3] = {
    { { 1.416, 0.965, 1.094, 0.917, 1.058, 1.076, 1.001, 0.856 }, { 1.92, 1.10, -0.87, -3.09, -3.11, -2.49, -0.60, -0.84 }, -2.73, 0.150, 30, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 1.00, 1.00, { 318, 63, 188 }, { -1.49, -1.43, 3.00 }, { 2.0, 2.0, 4.0 } }, // decay 1 s
    { { 1.106, 0.936, 1.018, 1.009, 0.992, 0.971, 1.011, 0.839 }, { -0.36, -0.71, -1.22, -2.71, -2.48, -1.30, 0.30, -0.04 }, -0.02, 0.300, 30, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 1.00, 1.00, { 318, 63, 188 }, { -1.41, -1.15, 3.00 }, { 2.0, 2.0, 4.0 } }, // decay 3 s
    { { 1.231, 0.924, 0.967, 1.069, 0.969, 0.945, 0.968, 0.830 }, { -3.00, -1.28, 0.11, -0.43, -0.67, -0.06, -0.15, -2.05 }, 0.10, 0.450, 30, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 1.00, 1.00, { 318, 100, 188 }, { -1.34, 1.20, 3.00 }, { 2.0, 2.0, 4.0 } }, // decay 10 s
};

// round 150 (parity150a) [EXEC]: the measured plate voicing (the Little Plate role, official trial, impulses at Decay 0.5 …
// 20 s and ∞, REA_rodada150a §7): the plate speaks after 0.7 ms (no plate delay), and its decay per band against the
// Decay label — the bass rings longer than the label at long settings (20 s: 35 s at 125 Hz), the highs shorter
// (20 s: 5.5 s at 8 kHz), short settings ring a little longer than the label in the presence band, "∞" is ≈ 80 s.
// Rows: label (s), the engine's RT60 (mid band), bass / high multipliers (fitted through Foil's own engine, plate.py)
struct FoilVoicingRow { double label, rt, low, high; };
LYNX_FIT_TABLE FoilVoicingRow foilVoicingRows[] = {
#include "core/FoilVoicing.inc"
};
constexpr int foilVoicingCount = (int) (sizeof (foilVoicingRows) / sizeof (foilVoicingRows[0]));
FoilVoicingRow foilVoicingAt (double label) noexcept
{
    const auto& a = foilVoicingRows[0];
    if (label <= a.label)
        return a;
    for (int i = 1; i < foilVoicingCount; ++i)
    {
        const auto& lo = foilVoicingRows[i - 1];
        const auto& hi = foilVoicingRows[i];
        if (label <= hi.label || i == foilVoicingCount - 1)
        {
            const auto t = std::clamp (std::log (label / lo.label) / std::log (hi.label / lo.label), 0.0, 1.0);
            const auto lg = [t] (double x, double y) { return std::exp (std::log (x) + t * (std::log (y) - std::log (x))); };
            return { label, lg (lo.rt, hi.rt), lg (lo.low, hi.low), lg (lo.high, hi.high) };
        }
    }
    return foilVoicingRows[foilVoicingCount - 1];
}

class FoilFx final : public VerbEffect
{
public:
    FoilFx() : VerbEffect (EffectKind::foilPlate, 0.2, 0.62) {}

protected:
    // (round 190cm: the measured plate voicing is another tank too — its own level (+6 … +16 dB), band edges, onset
    // network and, under the Measured law, loop voicing — so switching it while playing takes the same fade as a type)
    int structure (const EffectParams& p) const noexcept override
    {
        return choice (p[11], foil::typeCount) + (p[foil::engineLaw] >= 0.5f ? 1000 : 0) + (p[14] >= 0.5f ? 2000 : 0);
    }
    void configure (const EffectParams& p, double, Settings& s, float& mix) const noexcept override
    {
        const auto& t = foilTypes[choice (p[11], foil::typeCount)];
        mix = p[0];
        const auto size = 2.0 * p[3];
        s.predelaySec = (epochPredelay (p[1]) + t.preMs * (0.55 + 0.45 * size)) / 1000.0;
        s.shape = 2;
        s.lineScale = t.scale * (0.5 + 0.5 * size);
        const auto rt = foilDecaySec (p[2]);
        // round 135 (the Little Plate role): Infinite holds the plate (the decay knob's ∞), damped as at 30 s
        const auto infinite = p[12] >= 0.5f;
        s.rt60 = infinite ? 1000.0 : rt;
        // plates lose highs faster the longer they ring: 1/RT(8 kHz) = 1/RT + damp (measured, §3)
        s.highMult = 1.0 / (1.0 + t.damp * (infinite ? 30.0 : rt));
        s.highXover = 5000.0;
        s.lowMult = t.lowRt;
        s.lowXover = 250.0;
        // round 172d: the reference plate's band edge is higher and far steeper (third octaves: 10 kHz kept, 12.7 kHz
        // −35 dB, 16 kHz −70 dB under 10 kHz, against the old 9 kHz, 4-pole edge): 16 poles
        s.bandLimit = t.bw;
        s.bandLimitSections = 8;
        s.inDiffusion = 1.0;
        s.loopDiffusion = 0.85;
        // 0 % = mono, 100 % = decorrelated, 200 % = slightly anti-correlated (−0.21, measured)
        s.width = p[4] <= 0.5f ? 2.0 * p[4] : 1.0 + 0.48 * (p[4] - 0.5);
        s.modRateHz = skewLaw (p[5], 0.05, 5.0, 2.381);
        s.modDepthMs = 1.0 * p[6] / std::sqrt (std::max (0.3, s.modRateHz));
        // round 183b: the reference's Low / High EQ (Mix 100 %, the impulse at −20 dB, gain at 0 / 100 %): the low shelf
        // spans ±12 dB with its half-gain point at ~0.47 × the frequency law (−6 dB near 330 Hz at the default 711 Hz),
        // the high one +12 dB with its half-gain point at ~0.5 × (5 kHz at the default) but only −4.5 dB down
        s.lowShelfHz = 0.47 * (20.0 + 1980.0 * p[7]);
        s.lowShelfDb = -12.0 + 24.0 * p[8];
        s.highShelfHz = 0.5 * (200.0 + 19800.0 * p[9]);
        s.highShelfDb = p[10] >= 0.5f ? 24.0 * (p[10] - 0.5) : -9.0 * (0.5 - p[10]);
        // the plate speaks at once after its own delay: a short, smeared early burst
        s.earlyLevel = t.early;
        s.earlySec = 0.015;
        s.earlyDiffusion = 0.8;
        s.gain = t.gain * 1.3; // round 172d: +2.3 dB (the reference plate at its defaults: music / drums RMS −3.6 / −4.2 dB)
        // round 183b (the ValhallaPlate role, voicing off; the reference's impulse at −20 dB and music): Width keeps the
        // level (the reference: −27.4 / −27.5 / −27.2 dB at 0 / 50 / 100 %, ours moved 4 dB), its tail is fuller below
        // 80 Hz (+5 dB at 28 … 71 Hz) and less forward at 2.8 … 7 kHz (−3.5 dB), and it sits ~1 dB lower; shorter decays
        // are no louder than the default
        if (p[14] < 0.5f)
        {
            s.gain *= 0.9 * std::sqrt (2.0 / (1.0 + s.width * s.width));
            // and shorter decays a little quieter (Decay 20 / 25 %: this tank ran +1.4 … +2.7 dB on music, +3 dB on the impulse)
            s.gain *= std::pow (10.0, -2.5 * std::max (0.0, 0.5 - (double) p[2]) / 0.3 / 20.0);
            s.toneLowHz = 60.0;
            s.toneLowDb = 5.0;
            s.toneMidHz = 4500.0;
            s.toneMidDb = -3.5;
            s.toneMidQ = 0.8;
        }
        // round 135: a low cut on the plate's output, 20 Hz … 1 kHz (0 = off; 12 dB/oct)
        s.outLowCut = foil::lowCutHz (p[13]);
        s.outLowOrder = 2;
        // round 150: the measured plate voicing (the Little Plate role): no plate delay, the measured decay per band
        if (p[14] >= 0.5f)
        {
            const auto row = foilVoicingAt (infinite ? 1000.0 : rt);
            s.predelaySec = epochPredelay (p[1]) / 1000.0 + 0.0005;
            s.rt60 = row.rt;
            s.lowMult = row.low;
            s.lowXover = 300.0;
            s.highMult = row.high;
            s.highXover = 3000.0;
            s.bandLimit = 0.0;
            // round 172d: re-measured at Mix 100 % (the trial, the impulse's whole response at Decay 1 / 3 / 10 s, with the
            // darker tail and quieter early part below): the plate is 6 … 16 dB louder than round 150's level, more so at
            // short decays (dB per decade of the Decay label)
            static constexpr double levelKnots[] = { 0.0, 15.7, 0.477, 9.0, 1.0, 5.8, 1.5, 4.3 };
            const auto label = infinite ? 30.0 : std::max (1.0, rt);
            s.gain *= 0.82 / 1.3 * std::pow (10.0, knotLaw (std::log10 (label), levelKnots, 4) / 20.0); // (round 150: −1.7 dB)
            // round 172d: it is darker above 2 kHz than this tank (third octaves 2.5 … 8 kHz −5 … −8 dB, both the impulse
            // and music, the decay per band already matched) and its first 80 ms hold 7 … 10 dB less of the energy
            s.toneHighHz = 2500.0;
            // round 172d slice 10: deeper at short decays (the trial's 2 … 8 kHz octaves, sweep and impulses at Decay 0.5 …
            // 19 s, refcap0124: this tank was 7 … 9 dB too bright up to 1.7 s, 5 … 7 dB at 3 s, 2 … 3 dB from 5.7 s on). The
            // shelf goes from −5 dB to −13.5 / −12 / −9 dB and the edge from 7 to 9 kHz (8 … 16 kHz then within 1 … 2.5 dB)
            static constexpr double darkKnots[] = { -0.301, -13.5, 0.225, -13.0, 0.477, -12.0, 0.753, -9.0, 1.28, -9.0 };
            s.toneHighDb = knotLaw (std::log10 (infinite ? 30.0 : std::max (0.5, rt)), darkKnots, 5);
            s.outHighCut = std::min (s.outHighCut > 0.0 ? s.outHighCut : 1.0e9, 9000.0); // (and from 8 kHz up −8 … −14 dB)
            // round 172d slice 10: the trial's plate has no sub-bass (20 … 32 Hz 11 … 16 dB, 32 … 40 Hz 2 … 8 dB under this tank's,
            // 50 … 63 Hz level): a 12 dB/oct cut at 55 Hz unless the panel's Low Cut is higher
            if (s.outLowCut < 55.0)
            {
                s.outLowCut = 55.0;
                s.outLowOrder = 2;
            }
            s.outHighOrder = 2;
            s.earlyLevel *= 0.35;
        }
        if (p[foil::engineLaw] >= 0.5f)
        {
            if (p[14] >= 0.5f)
            {
                const auto at = std::log (std::clamp (infinite ? 30.0 : rt, foilMeasuredLawSec[0], foilMeasuredLawSec[2]));
                const auto k = at < std::log (foilMeasuredLawSec[1]) ? 0 : 1;
                applyLaw (s, blendLaw (foilMeasuredLaw[k], foilMeasuredLaw[k + 1],
                                       (at - std::log (foilMeasuredLawSec[k])) / std::log (foilMeasuredLawSec[k + 1] / foilMeasuredLawSec[k])));
            }
            else
                applyLaw (s, foilLaw[choice (p[11], foil::typeCount)]);
        }
    }
};

// ---------------------------------------------------------------------------------------------- Nebula
// Laws: delay 0..2000 ms (measured knots), feedback / density / warp 0..100 %, width −100..100 %, low cut 10..2000 Hz,
// high cut 200..20000 Hz, mod rate 0.01..10 Hz (v^2.33). Feedback F gives a gain of √F per pass of the delay.
constexpr double nebulaDelayKnots[] = { 0.0, 0.0, 0.5, 300.0, 0.75, 588.5, 0.775, 675.0, 0.925, 1425.0, 0.95, 1583.3, 0.975, 1791.7, 1.0, 2000.0 };
constexpr int nebulaDelayCount = (int) (sizeof (nebulaDelayKnots) / sizeof (double) / 2);
struct NebulaMode
{
    const char* name;
    double dm0, dm100, mult0, mult100, corr, gain;
};
// decay multiplier at density 0 / 100, line length × delay at density 0 / 100, stereo correlation (round 183b: the
// reference's tail at its defaults, Mod depth 50 %; mono modes 1.0, one mode −1),
// wet gain (round 172d: the reference's wet level per mode at Mix 100 %, impulse / bursts / music, defaults otherwise)
LYNX_FIT_TABLE NebulaMode nebulaModes[nebula::modeCount] = {
    { "Ripple", 1.03, 1.26, 1, 1, 0.0, 2.41 },        { "Swell", 3.43, 3.82, 1, 2, 0.04, 1.60 },
    { "Fold", 1.33, 1.39, 1, 2, 0.04, 1.79 },       { "Drift", 2.56, 2.67, 1, 1, 0.09, 1.12 },
    { "Abyss", 3.64, 3.97, 1, 2, -0.01, 1.26 },      { "Vortex", 7.04, 6.77, 2, 2, 0.02, 1.05 },
    { "Glint", 1.03, 1.17, 1, 1, 0.07, 1.67 },        { "Pulse", 1.03, 1.22, 1, 1, 0.0, 1.93 },
    { "Expanse", 4.29, 12.8, 8, 1, 1, 3.57 },       { "Haze", 2.14, 12.3, 4, 1, 1, 3.87 },
    { "Veil", 2.02, 5.04, 2, 1, 0.0, 1.43 },  { "Mist", 1.00, 1.01, 1, 1, 0.0, 1.49 },
    { "Monolith", 2.16, 16.3, 4, 1, 1, 3.84 },       { "Eternity", 20.0, 20.7, 8, 1, -1, 2.06 },
    { "Tide", 1.00, 1.12, 1, 1, 0.0, 1.50 },      { "Shoal", 1.03, 1.30, 1, 1, -0.04, 1.51 },
    { "Spark", 1.00, 1.05, 1, 1, -0.02, 2.43 },       { "Plateau", 2.10, 9.65, 4, 1, 0.95, 3.62 },
    { "Dusk", 4.15, 12.65, 8, 1, 0.99, 2.86 },       { "Mirage", 1.00, 1.35, 1, 2, 0.0, 1.50 },
    { "Cluster", 1.02, 1.01, 1, 1, 0.02, 2.56 },    { "Beacon", 1.16, 1.04, 1, 1, -0.01, 2.27 },
};
double nebulaDelayMs (float v) { return knotLaw (v, nebulaDelayKnots, nebulaDelayCount); }
const char* const syncNames[4] = { "ms", "Note", "Dotted", "Triplet" };

// Round 190ca: the cloud law (parameter 14, on for new instances). Six of the 22 networks — the ones whose structure
// is published — run on core/NebulaCloud (a port of an MIT-licensed model, see that header); the other sixteen and
// every project saved before the round (Law off: upgradeLoadedEffectParams) keep the classic engine below, unchanged.
constexpr int nebulaLaw = 14;
cloud::Topology nebulaTopology (int mode) noexcept
{
    switch (mode)
    {
        case 0: return cloud::Topology::fdn16;
        case 2: return cloud::Topology::serialQuad;
        case 3: return cloud::Topology::serialStereo;
        case 4: return cloud::Topology::loopedQuad;
        case 6: return cloud::Topology::fdn4;
        case 19: return cloud::Topology::fdn4Filtered;
        default: return cloud::Topology::none;
    }
}

class NebulaFx final : public VerbEffect
{
public:
    NebulaFx() : VerbEffect (EffectKind::nebula, 4.2, 0.05) {}

    void prepare (double sampleRate) noexcept override
    {
        VerbEffect::prepare (sampleRate);
        network.prepare (sampleRate);
    }
    void reset() noexcept override
    {
        VerbEffect::reset();
        network.reset();
        change = Fade::none;
        placed = false;
    }

    // CLEAR (round 129): the panel's push button flips parameter 13; a flip empties the delay network at once
    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (p[13] != lastClear)
        {
            if (lastClear >= 0.0f)
                reset();
            lastClear = p[13];
        }
        const auto want = p[nebulaLaw] >= 0.5f ? nebulaTopology (choice (p[12], nebula::modeCount)) : cloud::Topology::none;
        if (! placed)
            placed = true, active = want, before = p;
        if (want != active && change == Fade::none)
        {
            change = Fade::out;
            changeLeft = fadeLength;
            kept = before;
        }
        if (active == cloud::Topology::none && change == Fade::none)
        {
            VerbEffect::process (left, right, n, p, bpm); // the classic engine, as before the round
            before = p;
            return;
        }
        // a cloud network, or the 11 ms either side of a change between the two engines / two cloud networks (the wet
        // part fades out on the old one with its settings held, the new one starts empty and fades in)
        for (int done = 0; done < n;)
        {
            auto m = std::min (n - done, chunk);
            if (change != Fade::none)
                m = std::min (m, changeLeft);
            const auto& q = change == Fade::out ? kept : p;
            auto* l = left + done;
            auto* r = right + done;
            float dry, wet;
            mixGains (q[0], dry, wet);
            std::copy (l, l + m, inL.begin());
            std::copy (r, r + m, inR.begin());
            if (active == cloud::Topology::none)
            {
                VerbEffect::process (l, r, m, q, bpm);
                for (int i = 0; i < m; ++i)
                {
                    l[i] -= dry * inL[(std::size_t) i];
                    r[i] -= dry * inR[(std::size_t) i];
                }
                wet = 1.0f;
            }
            else
                network.process (inL.data(), inR.data(), l, r, m, controls (q, bpm, active));
            for (int i = 0; i < m; ++i)
            {
                auto w = wet;
                if (change != Fade::none)
                {
                    const auto g = (float) (changeLeft - i) / (float) fadeLength; // 1 … 0 over the fade
                    w *= change == Fade::out ? g : 1.0f - g;
                }
                l[i] = dry * inL[(std::size_t) i] + w * l[i];
                r[i] = dry * inR[(std::size_t) i] + w * r[i];
            }
            done += m;
            if (change != Fade::none && (changeLeft -= m) <= 0)
            {
                if (change == Fade::out)
                {
                    change = Fade::in;
                    changeLeft = fadeLength;
                    active = want;
                    if (active == cloud::Topology::none)
                        VerbEffect::reset();
                    else
                        network.reset();
                }
                else
                    change = Fade::none;
            }
        }
        before = p;
    }

private:
    // the cloud law's controls: the same parameters in the reference's units (the model takes them directly)
    cloud::Controls controls (const EffectParams& p, double bpm, cloud::Topology topology) const noexcept
    {
        cloud::Controls c;
        c.topology = topology;
        c.delayMs = syncedMs (choice (p[1], 4), choice (p[2], 7), nebulaDelayMs (p[3]), bpm);
        c.warp = p[4];
        c.feedback = p[5];
        c.density = p[6];
        c.width = -1.0 + 2.0 * p[7];
        c.lowCutHz = 10.0 + 1990.0 * p[8];
        c.highCutHz = 200.0 + 19800.0 * p[9];
        c.modRateHz = skewLaw (p[10], 0.01, 10.0, 2.328);
        c.modDepth = p[11];
        return c;
    }

    float lastClear = -1.0f;
    cloud::Network network;
    cloud::Topology active = cloud::Topology::none;
    Fade change = Fade::none;
    int changeLeft = 0;
    bool placed = false;
    EffectParams before {}, kept {};
    std::array<float, chunk> inL {}, inR {};

protected:
    int structure (const EffectParams& p) const noexcept override { return choice (p[12], nebula::modeCount); }
    void configure (const EffectParams& p, double bpm, Settings& s, float& mix) const noexcept override
    {
        const auto& m = nebulaModes[choice (p[12], nebula::modeCount)];
        mix = p[0];
        const auto delay = std::max (1.0, syncedMs (choice (p[1], 4), choice (p[2], 7), nebulaDelayMs (p[3]), bpm));
        const auto warp = (double) p[4];
        const auto fb = (double) p[5];
        const auto dens = (double) p[6];
        const auto lerpLog = [&] (double a, double b) { return std::exp (std::log (a) + (std::log (b) - std::log (a)) * dens); };
        const auto mult = lerpLog (m.mult0, m.mult100);
        const auto sw = warp * warp * (3.0 - 2.0 * warp); // smoothstep: lines shorten as warp rises
        s.shape = 3;
        s.lineScale = delay * mult;
        s.lineSpread = sw;
        // √F per pass of the delay: RT = 6·D / −log10 F, times the shape's measured decay multiple (which already holds
        // its line-length multiple); warp shortens the lines and so the decay
        const auto meanSec = delay * (1.0 - 0.53 * sw) / 1000.0; // (the warp fractions average 0.47)
        s.rt60 = fb >= 0.9999 ? 1000.0 : 6.0 * meanSec * lerpLog (m.dm0, m.dm100) / std::max (1e-4, -std::log10 (std::max (1e-6, fb)));
        s.rt60 = std::min (s.rt60, 1000.0);
        s.inDiffusion = dens;
        s.loopDiffusion = 0.85 * dens;
        s.crossFeed = 0.3 + 0.7 * dens;
        s.parallelLines = dens < 0.02; // density 0: plain repeats of the delay
        // round 183b: stereo. The reference's tail is decorrelated whenever it modulates (L/R correlation −0.01 at the
        // default Mod depth, 0.13 / 0.34 at 25 / 10 %, 1.0 at 0, at any Density) and this network decorrelates the
        // same way through its modulated lines, so the width is the panel's; only the reference's mono modes (its
        // correlation, measured per mode at the defaults) narrow it. Before, Density 0 played mono
        const auto corr = m.corr;
        const auto w = std::sqrt ((1.0 - corr) / (1.0 + corr + 1e-3));
        s.width = (-1.0 + 2.0 * p[7]) * std::min (2.0, w);
        // round 172d: both cuts are first order in the reference (Mix 100 %, cuts at 5 … 70 % / 10 … 75 %: −3 dB at the
        // linear law's frequency, 6 dB per octave beyond)
        s.outLowCut = 10.0 + 1990.0 * p[8];
        s.outLowOrder = 1;
        s.outHighCut = 200.0 + 19800.0 * p[9];
        s.outHighOrder = 1;
        s.modRateHz = skewLaw (p[10], 0.01, 10.0, 2.328);
        s.modDepthMs = 2.0 * p[11] / std::sqrt (std::max (0.1, s.modRateHz));
        // round 172d: the reference keeps its level nearly flat over Feedback and Density (Mix 100 %, its first mode):
        // the measured wet level against this loop's, in dB per Feedback knot; Density keeps its lows there (this tank's
        // diffusion takes 5 … 10 dB below 250 Hz from the summed sides), so a low shelf comes in with it
        static constexpr double fbKnots[] = { 0.0, 7.0, 0.2, 6.3, 0.5, 0.0, 0.8, -2.7, 0.95, -3.6, 1.0, -4.0 };
        const auto fbDb = knotLaw (fb, fbKnots, 6);
        s.gain = m.gain * std::pow (10.0, fbDb / 20.0);
        s.lowShelfHz = 220.0;
        s.lowShelfDb = 8.0 * std::min (1.0, dens / 0.15);
        // round 183b: the reference's eight voices fall apart as they modulate (its tail ~9 dB louder and mono at Mod
        // depth 0: +8.9 dB against 50 %, −3.6 dB at 10 %); this network's lines part less, so the rest comes in here.
        // The level is re-measured with the full width: −0.9 dB at Density 0, −4.2 dB from Density 50 % up (the dense
        // network's sides were nearly mono before)
        s.gain *= (0.9 - 0.28 * std::min (1.0, dens / 0.5)) * std::pow (10.0, 5.0 * std::exp (-(double) p[11] / 0.1) / 20.0);
    }
};

// ---------------------------------------------------------------------------------------------- Halo
// The reference exposes bare 0..1 values; the laws below are what the measurements showed (§5): shift −12..+12
// semitones (linear), decay set mainly by Size (≈ 1.5 s + 12.5 s × size), high cut 100..15000 Hz first order (the
// reference room's law), low cut 0..1000 Hz; mod rate / depth laws not confirmed.
struct HaloMode
{
    const char* name;
    double scale, rtScale, bright;
    float value;   // the Reverb mode value that selects it (nearest wins)
    int stereo;    // 0 = LYNX's own wide tank, 1 = mono in (the reference's Mono), 2 = two separate sides (its stereo modes)
    double width;  // side gain of the output
};
// round 135: the reference's four modes [EXEC: its trial, a left-only impulse — below 0.25 the input is summed and the
// tail comes out of both sides, from 0.25 up the left input stays on the left; 0.25 / 0.5 / 0.75 are the steps] beside
// LYNX's own Wide / Dense / Bright, which keep their values 0 / ½ / 1 (older projects sound the same)
const HaloMode haloModes[7] = { { "Wide", 1.2, 1.0, 0.0, 0.0f, 0, 1.0 },          { "Mono", 1.0, 1.0, 0.0, 0.125f, 1, 1.0 },
                                { "Small stereo", 0.75, 0.85, 0.0, 0.25f, 2, 1.0 }, { "Medium stereo", 1.0, 1.0, 0.0, 0.375f, 2, 1.0 },
                                { "Dense", 0.9, 1.05, 0.0, 0.5f, 0, 1.0 },          { "Big stereo", 1.3, 1.15, 0.0, 0.625f, 2, 1.0 },
                                { "Bright", 1.0, 0.75, 1.0, 1.0f, 0, 1.0 } };
int haloModeIndex (float v)
{
    int best = 0;
    for (int i = 1; i < 7; ++i)
        if (std::abs (v - haloModes[i].value) < std::abs (v - haloModes[best].value))
            best = i;
    return best;
}
const char* const haloShiftModes[5] = { "Single", "Dual", "Single, bright", "Dual, bright", "No shift" };
const char* const haloColours[2] = { "Bright", "Dark" };

double haloShiftSemis (float v) { return -12.0 + 24.0 * v; }
// round 190bz: law 1
// fitted with the tail lab (round 190bz; the reference and the scores per row: docs/PARITY.md); a neutral entry is the classic voicing on the new loop
LYNX_FIT_TABLE LawVoicing haloLaw = { { 1.108, 1.268, 1.316, 1.083, 1.164, 1.001, 1.182, 1.427 }, { 1.63, -1.12, -1.41, -0.40, -0.68, -1.46, -1.95, -0.59 }, 2.13, 0.000, 30, 0.00, 0.00, 1.00, 1.00, 0.00, 0.30, 6.00, 1.00, { 63, 159, 5000 }, { -2.29, -1.54, 0.00 }, { 2.0, 2.0, 1.4 } };

class HaloFx final : public VerbEffect
{
public:
    HaloFx() : VerbEffect (EffectKind::halo, 0.3, 0.05) {}

protected:
    int structure (const EffectParams& p) const noexcept override { return p[halo::engineLaw] >= 0.5f ? 1000 : 0; }
    void configure (const EffectParams& p, double, Settings& s, float& mix) const noexcept override
    {
        mix = p[0];
        const auto& m = haloModes[haloModeIndex (p[9])];
        const auto shiftMode = choice (p[10], 5);
        const auto dark = choice (p[11], 2) == 1;
        const auto size = (double) p[4];
        s.shiftSemis = haloShiftSemis (p[1]);
        s.shiftFeedback = shiftMode == 4 ? 0.0 : 0.9 * p[2]; // the shifted share of the recirculation
        s.shiftMix = 1.0;
        s.shift2Mix = shiftMode == 1 || shiftMode == 3 ? 0.7 : 0.0;
        s.shiftSemis2 = s.shiftSemis - 24.0;
        s.inDiffusion = p[3];
        s.loopDiffusion = 0.2 + 0.7 * p[3];
        // round tp172d (refs/halo, 20 s impulses): the reference's energy builds for 0.6 / 0.9 / 1.3 s at Size 30 / 50 / 80 %
        // (Diffusion 92 %) — an even feed over 0.62 + 1.8 × Size seconds into the tank fits its envelope within 0.7 dB.
        // With less diffusion it gathers late (Diffusion 50 %: a cluster near 1.2 s; 0 %: one arrival at 1.217 s): the
        // feed leans to the end of a span that shrinks to 0.8 × (a model between those three measured points)
        {
            const auto d = std::clamp ((double) p[3] / 0.92, 0.0, 1.0);
            s.buildSec = (0.62 + 1.8 * size) * (0.8 + 0.2 * d);
            s.buildRise = 1.0 + 15.0 * (1.0 - d) * (1.0 - d);
        }
        s.shape = 0;
        s.lineScale = m.scale * (0.3 + 1.2 * size);
        // round tp172d: the reference's RT per Size and Feedback, measured (refs/halo: 20 s impulses; Size 30 / 50 / 80 %
        // ≈ 5.6 / 8.4 / 12 s at feedback 0; feedback lengthens it ×1.2 / 1.65 / 2.2 at 50 / 75 / 100 % with the octave
        // shift, ×1.4 / 2.1 / 4.3 unshifted, where the loop's own voice comes back in phase)
        {
            const auto fb = (double) p[2], f = std::min (1.0, std::abs (s.shiftSemis) / 2.0);
            const auto mShift = 1.0 + 1.2 * std::pow (fb, 2.5), mPlain = 1.0 + 3.3 * fb * fb * fb;
            // with less diffusion the reference's tail is shorter (Diffusion 0 / 50 %: ~5.5 / 6 s against 9.8 s at 92 %)
            const auto dn = std::clamp ((double) p[3] / 0.92, 0.0, 1.0);
            s.rt60 = (1.5 + 12.5 * size) * m.rtScale * (mPlain + (mShift - mPlain) * f) * (0.55 + 0.45 * dn * dn * dn);
        }
        // round tp172d: its first 0.3 s are 5–9 dB under ours at 0.45; with less diffusion they are empty (Diffusion 50 %:
        // −55 dB, 0 %: silent until the first arrival at 1.2 s)
        s.earlyLevel = 0.3 * std::pow (std::clamp ((double) p[3] / 0.92, 0.0, 1.0), 8.0);
        s.earlySec = 0.03;
        s.earlyDiffusion = p[3];
        s.outLowCut = 250.0 * p[5]; // gentle: the reference's low cut barely shows at 63 Hz (§5, not confirmed)
        s.outLowOrder = 1;
        if (s.outLowCut < 5.0)
            s.outLowCut = 0.0;
        s.outHighCut = 100.0 + 14900.0 * p[6];
        s.outHighOrder = 1;
        if (shiftMode == 2 || shiftMode == 3 || m.bright > 0.0)
            s.highShelfDb = 4.0, s.highShelfHz = 3000.0;
        s.loopBandwidth = dark ? 5000.0 : 0.0;
        s.modRateHz = 0.05 + 4.95 * p[7];
        s.modDepthMs = 1.2 * p[8] / std::sqrt (std::max (0.3, s.modRateHz));
        // round tp172d: the wet level measured on music and drums (refs/halo): −3 dB up to Feedback 75 %, rising to +1 dB
        // at 100 % (the reference swells). Its impulse has a fuller low end than ours (+9 / +5 dB at 63 / 125 Hz) but its
        // music and drums don't (a shelf for the impulse made drums 5 dB loud there): left flat
        {
            const auto fb = (double) p[2];
            const auto dn = std::clamp ((double) p[3] / 0.92, 0.0, 1.0); // less diffusion: louder (+4.5 / +5.4 dB measured at 0 / 50 %)
            const auto trimDb = (fb <= 0.75 ? -3.0 : -3.0 + 16.0 * (fb - 0.75)) + 5.0 * (1.0 - dn * dn * dn);
            s.gain = (dark ? 0.75 : 0.95) * std::pow (10.0, trimDb / 20.0);
        }
        s.shimmerEnergy = 0.2; // the shifted voice's share added per pass (× the shimmer amount)
        if (m.stereo == 2)
            s.crossFeed = 0.0, s.splitSides = true; // the two sides stay apart
        // round 183b: the reference's Mono mode (mono in) gives a partly correlated tail: L/R correlation 0.25 / 0.31 /
        // 0.43 at Size 30 / 50 / 80 % (its stereo modes: ~0, as this tank's)
        if (m.stereo == 1)
        {
            const auto c = 0.13 + 0.375 * size;
            s.width = std::sqrt ((1.0 - c) / (1.0 + c));
            s.gain *= std::sqrt (2.0 / (1.0 + s.width * s.width)); // (the same energy as the full width)
            s.earlyLevel *= 0.4; // its first 80 ms hold 3.4 dB less of the energy than ours did (impulse at −20 dB)
        }
        if (p[halo::engineLaw] >= 0.5f)
            applyLaw (s, haloLaw);
    }

    // the reference's Mono mode sums the input (mono in, a stereo tail)
    void preStage (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        if (haloModes[haloModeIndex (p[9])].stereo != 1)
            return;
        for (int i = 0; i < n; ++i)
            l[i] = r[i] = 0.5f * (l[i] + r[i]);
    }
};

// ---------------------------------------------------------------------------------------------- Horizon
constexpr double horizonDecayKnots[] = { 0.0, 0.2, 0.5, 5.0, 0.75, 9.81, 0.775, 12.5, 0.8, 16.67, 0.825, 21.25, 0.85, 27.5,
                                         0.875, 33.75, 0.9, 41.67, 0.925, 50.0, 0.95, 57.5, 0.975, 63.75, 1.0, 70.0 };
constexpr int horizonDecayCount = (int) (sizeof (horizonDecayKnots) / sizeof (double) / 2);
constexpr double horizonEchoKnots[] = { 0.0, 0.0, 0.5, 100.0, 0.675, 197.2, 0.7, 250.0, 0.75, 375.0, 0.775, 456.2, 0.8, 550.0,
                                        0.825, 643.8, 0.85, 762.5, 0.875, 918.8, 0.9, 1075.0, 0.925, 1250.0, 0.95, 1500.0, 0.975, 1750.0,
                                        1.0, 2000.0 };
constexpr int horizonEchoCount = (int) (sizeof (horizonEchoKnots) / sizeof (double) / 2);
const char* const horizonEchoModes[horizon::echoModeCount] = { "Clean", "Tape", "Digital", "Analog", "Detune", "Reverse",
                                                                "Reverse Up", "Reverse Down", "Reverse Up/Down", "Sparkle", "Swarm", "Lo-fi" };
struct HorizonReverb
{
    const char* name;
    int shape;
    double scale, rtScale, preMs, buildMs, early, gain;
    double lowMult, lowDb, highDb; // round 183b: bass decay multiple (below 500 Hz), the mode's own low / high shelf
};
// round 183b, measured on the reference (Mix 100 %, Decay 3 s, Size 50 %, its default Chamber otherwise; refcap
// verb183b): RT of each mode against Decay (Room 0.77, Plate 0.97, Hall 0.93, Space 1.06), its bass decay (Room
// 0.82 × the mids, Chamber 1.2, Plate 1.12, Hall 1.3, Space 0.9), its own onset after the echo's pre-delay (Room 23,
// Chamber 13, Plate 6, Hall 31, Space 74 ms; the table holds Size 50 %), Hall's darker top (−7 dB above 4 kHz) and
// Space's fuller lows (+4 dB). gain: the wet level per mode (impulse at −20 dB and music, 100 % wet)
LYNX_FIT_TABLE HorizonReverb horizonReverbs[horizon::reverbModeCount] = {
    { "Room", 1, 1.6, 0.77, 23.0, 60.0, 0.3, 2.4, 0.82, 0.0, 0.0 },
    { "Chamber", 1, 1.2, 1.00, 13.0, 45.0, 0.3, 3.0, 1.20, 0.0, 0.0 },
    { "Plate", 2, 1.0, 0.97, 6.0, 0.0, 0.0, 4.6, 1.12, 0.0, -5.0 },
    { "Hall", 0, 1.0, 0.93, 31.0, 60.0, 0.2, 2.64, 1.30, 0.0, -7.0 },
    { "Cathedral", 0, 1.3, 1.05, 45.0, 80.0, 0.1, 3.2, 1.30, 3.0, -3.0 },
    { "Space", 0, 1.5, 1.06, 24.0, 90.0, 0.0, 1.4, 0.90, 4.0, -4.0 },
    { "Freeze", 0, 1.4, 3.30, 30.0, 300.0, 0.0, 4.0, 1.00, 0.0, 0.0 },
    { "Gated", 1, 0.8, 0.60, 2.0, 0.0, 0.1, 3.6, 1.00, 0.0, 0.0 },
};
// round 183b: the reference's colours (its manual and measured): Bright and Neutral play the same at these decays;
// Dark adds a steep low-pass at 8 kHz (and −1.5 dB from 3 kHz up); the fourth (the reference's Studio) a steep
// 600 Hz low cut (12 dB/oct, −3 dB at ~560 Hz) and a steep 10 kHz high cut (36 dB/oct at 12 kHz)
const char* const horizonColours[4] = { "Bright", "Neutral", "Dark", "Studio" };

double horizonDecaySec (float v) { return knotLaw (v, horizonDecayKnots, horizonDecayCount); }
// round 190bz: law 1 per reverb mode
// fitted with the tail lab (round 190bz; the reference and the scores per row: docs/PARITY.md); a neutral entry is the classic voicing on the new loop
LYNX_FIT_TABLE LawVoicing horizonLaw[horizon::reverbModeCount] = {
    {},
    { { 1.177, 0.994, 1.035, 0.965, 0.987, 0.962, 0.896, 0.980 }, { -2.48, 0.06, 1.67, -1.40, 1.21, -0.06, -1.35, -1.94 }, 0.75, 0.450, 30, 0.00, 0.00, 1.00, 0.40, 0.40, 1.00, 1.00, 1.00, { 63, 200, 5000 }, { -1.86, -1.37, 0.00 }, { 2.0, 2.0, 1.4 } }, // Chamber
};
double horizonEchoMs (float v) { return knotLaw (v, horizonEchoKnots, horizonEchoCount); }

class HorizonFx final : public VerbEffect
{
public:
    HorizonFx() : VerbEffect (EffectKind::horizon, 0.25, 0.25) {}

    void prepare (double sampleRate) noexcept override
    {
        VerbEffect::prepare (sampleRate);
        for (auto& l : echo)
            l.allocate ((int) (2.6 * sampleRate) + 16);
        for (auto& sh : shifters)
            sh.prepare (sampleRate, 0.08);
        reset();
    }
    void reset() noexcept override
    {
        VerbEffect::reset();
        for (auto& l : echo)
            l.clear();
        for (auto& sh : shifters)
            sh.clear();
        tone[0].z = tone[1].z = 0.0f;
        lowCutState[0].z = lowCutState[1].z = 0.0f;
        compEnv = 0.0f;
        compGain = 1.0f;
        compTick = 0;
        wow = 0.0;
        crush[0] = crush[1] = 0.0f;
        crushPhase = 0.0;
    }

protected:
    int structure (const EffectParams& p) const noexcept override { return choice (p[22], horizon::reverbModeCount) + (p[horizon::engineLaw] >= 0.5f ? 1000 : 0); }
    void configure (const EffectParams& p, double, Settings& s, float& mix) const noexcept override
    {
        const auto mode = choice (p[22], horizon::reverbModeCount);
        const auto& m = horizonReverbs[mode];
        mix = p[0];
        const auto size = (double) p[12];
        s.shape = m.shape;
        s.lineScale = m.scale * (0.5 + 0.8 * size);
        // round 183b: the echo is the pre-delay (Echo time); the mode adds its own onset, which grows with Size
        // (measured: 25 % / 100 % / 139 % of the Size 50 % value at 0 / 50 / 100 %)
        s.predelaySec = m.preMs * (0.25 + 1.85 * size - 0.7 * size * size) / 1000.0;
        const auto decay = horizonDecaySec (p[11]);
        s.rt60 = decay * m.rtScale * (1.03 - 0.12 * size) / 0.97; // (the reference's RT shortens ~10 % with Size)
        s.buildSec = m.buildMs / 1000.0 * (0.3 + 1.4 * p[13]);
        const auto earlyLate = (double) p[14];
        s.earlyLevel = m.early * (1.5 - earlyLate);
        s.earlySec = 0.04 * (0.5 + size);
        // round 183b: the reference's level barely moves with Early/Late (+1.6 / −1.3 dB at 0 / 100 %)
        s.lateLevel = 1.1 - 0.2 * earlyLate;
        s.inDiffusion = 0.4 + 0.6 * p[15];
        s.loopDiffusion = 0.1 + 0.8 * p[15];
        // round 183b: the reference's Frozen and Nonlin modes take their length from Size, not Decay (its manual;
        // measured at Size 0 / 50 / 100 %: Nonlin gates after ~0.1 / 0.33 / 1.05 s; Frozen holds with an RT of
        // ~2 / 15 s at Size 0 / 50 %, "tens of seconds" at 100 %)
        s.gateSec = mode == 7 ? 0.1 * std::pow (10.5, size) : 0.0;
        if (mode == 6)
            s.rt60 = 2.0 * std::pow (7.5, 2.0 * size);
        s.modRateHz = skewLaw (p[17], 0.01, 10.0, 2.328);
        s.modDepthMs = 1.0 * p[18] / std::sqrt (std::max (0.1, s.modRateHz));
        // round 183b: Low EQ is the echo section's −6 dB/oct high-pass (Echo → Reverb: the reverb hears the echo,
        // so it is cut once; Reverb → Echo: on the reverb's output here). High EQ is a −6 dB/oct low-pass in both the
        // echo and the reverb and sets the reverb's high decay: measured RT(8 kHz) / RT(1 kHz) 0.47 / 0.65 / 0.90 /
        // 0.99 at High EQ 1 / 4.5 / 11.9 / 20 kHz (this tank's own losses give ~0.65 with no damping)
        const auto lowEq = skewLaw (p[19], 20.0, 1000.0, 1.807), highEq = skewLaw (p[20], 1000.0, 20000.0, 1.078);
        s.outLowCut = choice (p[10], 2) == 1 ? lowEq : 0.0;
        s.outHighCut = highEq;
        s.outLowOrder = 1;
        s.outHighOrder = 1;
        if (highEq < 4500.0)
        {
            s.highXover = highEq;
            s.highMult = 0.5 + 0.45 * std::log2 (highEq / 1000.0) / std::log2 (4.5);
        }
        else
        {
            s.highXover = 3000.0;
            s.highMult = 1.0 + 0.9 * std::log2 (highEq / 4500.0) / std::log2 (20000.0 / 4500.0);
        }
        s.lowMult = m.lowMult;
        s.lowXover = 500.0;
        // round 183b: the reference's tail is fuller below ~1.4 kHz than this tank's (impulse at −20 dB, music)
        s.lowShelfHz = 1400.0;
        s.lowShelfDb = 3.0 + m.lowDb;
        s.highShelfHz = 4000.0;
        s.highShelfDb = m.highDb;
        const auto colour = choice (p[23], 4);
        if (colour == 2) // Dark
        {
            s.bandLimit = 8000.0;
            s.bandLimitSections = 8;
            s.toneHighHz = 3000.0;
            s.toneHighDb = -1.5;
        }
        else if (colour == 3) // the reference's Studio
        {
            s.outLowCut = std::max (s.outLowCut, 560.0);
            s.outLowOrder = 2;
            s.bandLimit = 12000.0;
            s.bandLimitSections = 3;
        }
        s.width = -1.0 + 2.0 * p[1];
        // round 172d: the reference holds its wet energy nearly constant over Decay, so the level follows 1 / √RT
        s.gain = m.gain * p[16] * 0.65 * 1.25 / std::sqrt (std::max (0.2, s.rt60)); // (× 1.25: the compressor's make-up)
        // round 183b: this tank's short lines (small Size) and lower Density run louder than the reference's
        // (measured: Size 0 at −0.9 dB against Size 50 %; Density 0 / 50 % at +2.3 / +1.9 dB against 100 %)
        s.gain *= std::min (1.0, 0.56 + 0.88 * size) * std::pow (10.0, 2.2 * std::min (1.0, 2.0 * (1.0 - (double) p[15])) / 20.0);
        if (p[horizon::engineLaw] >= 0.5f)
            applyLaw (s, horizonLaw[mode]);
    }

    // round 183b: the echo section as the reference's manual has it. Echo → Reverb: the echo's output (not the dry
    // input) is the reverb's input, and Level sets only how much of the echo is heard beside the reverb, so at Level 0
    // and Feedback 0 the echo is the reverb's pre-delay (Echo time, 30 ms by default; the reference's onset follows
    // it). Reverb → Echo: the reverb's output feeds the echo and Level sets the echo heard on top of it. The echo's
    // Low EQ is a −6 dB/oct high-pass on what enters its line (input and feedback); Drive is used by the plain modes
    // (Modern, Tape, Digital, Analog, Lo-fi) and Detune by the granular ones (Detune … Swarm), as the manual says
    void echoStage (float* l, float* r, int n, const EffectParams& p, double bpm, bool beforeReverb) noexcept
    {
        const auto level = (float) p[9];
        if (! beforeReverb && level <= 0.0f)
            return;
        const auto delay = std::max (1.0, syncedMs (choice (p[2], 4), std::min (5, choice (p[3], 6)), horizonEchoMs (p[4]), bpm)) * rate / 1000.0;
        const auto fb = (float) (0.98 * p[5]);
        const auto spread = (double) p[6];
        const auto mode = choice (p[21], horizon::echoModeCount);
        const auto granular = mode >= 4 && mode <= 10;
        const auto drive = granular ? 1.0 : std::pow (10.0, 24.0 * p[7] / 20.0);
        const auto detune = granular ? (-30.0 + 60.0 * p[8]) / 100.0 : 0.0; // semitones
        double ratio = 1.0, ratio2 = 1.0;
        bool shift = false, dual = false;
        double cut = 18000.0, wowDepth = 0.0;
        switch (mode)
        {
            case 1: cut = 7000.0; wowDepth = 0.0015; break;               // tape
            case 3: cut = 5000.0; break;                                   // analog
            case 4: shift = true; ratio = std::pow (2.0, (detune + 0.12) / 12.0); ratio2 = std::pow (2.0, -(detune + 0.12) / 12.0); dual = true; break;
            case 5: shift = true; ratio = -1.0; break;                     // reverse
            case 6: shift = true; ratio = -2.0; break;
            case 7: shift = true; ratio = -0.5; break;
            case 8: shift = true; ratio = -2.0; ratio2 = -0.5; dual = true; break;
            case 9: shift = true; ratio = 2.0; break;                      // sparkle: an octave up in the repeats
            case 10: shift = true; ratio = std::pow (2.0, 0.15 / 12.0); ratio2 = std::pow (2.0, -0.15 / 12.0); dual = true; wowDepth = 0.003; break;
            case 11: cut = 4000.0; break;                                  // lo-fi
            default: break;
        }
        if (! shift && std::abs (detune) > 1e-3)
        {
            shift = true;
            ratio = std::pow (2.0, detune / 12.0);
        }
        tone[0].setLowpass (cut, rate);
        tone[1].a = tone[0].a;
        const auto lowCut = skewLaw (p[19], 20.0, 1000.0, 1.807);
        lowCutState[0].setLowpass (lowCut, rate);
        lowCutState[1].a = lowCutState[0].a;
        const auto spreadL = delay * (1.0 - 0.25 * spread), spreadR = delay * (1.0 + 0.25 * spread);
        const auto wowInc = 2.0 * pi * 0.7 / rate;
        const auto hold = mode == 11 ? std::max (1.0, rate / 11025.0) : 1.0;
        for (int i = 0; i < n; ++i)
        {
            wow += wowInc;
            if (wow > 2.0 * pi)
                wow -= 2.0 * pi;
            const auto w = 1.0 + wowDepth * std::sin (wow);
            float y[2] = { echo[0].readFrac (std::max (2.0, spreadL * w)), echo[1].readFrac (std::max (2.0, spreadR * w)) };
            float x[2] = { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                auto v = y[c];
                if (shift)
                {
                    auto a = shifters[c].process (v, ratio);
                    if (dual)
                        a = 0.5f * (a + shifters[c + 2].process (v, ratio2));
                    v = a;
                }
                v = tone[c].lp (v);
                if (drive > 1.0001)
                    v = (float) (std::tanh (drive * v) / drive);
                if (mode == 11)
                {
                    if (c == 0)
                        crushPhase += 1.0;
                    if (crushPhase >= hold)
                        crush[c] = std::round (v * 64.0f) / 64.0f;
                    v = crush[c];
                }
                y[c] = v;
            }
            if (crushPhase >= hold)
                crushPhase -= hold;
            for (int c = 0; c < 2; ++c)
            {
                const auto in = x[c] + fb * y[c];
                echo[c].write (in - lowCutState[c].lp (in));
            }
            if (beforeReverb)
            {
                echoOut[0][(std::size_t) i] = level * y[0];
                echoOut[1][(std::size_t) i] = level * y[1];
                l[i] = y[0];
                r[i] = y[1];
            }
            else
            {
                l[i] = x[0] + level * y[0];
                r[i] = x[1] + level * y[1];
            }
        }
    }

    void preStage (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        // round 183b: the reference's wet runs 1 … 2 dB lower on loud programme than its response at −20 dB predicts
        // (music, drums, noise and a decaying burst; ~1 dB with its clean echo mode): a gentle compressor on what
        // enters the echo and reverb (mean-square follower, 30 ms; −30 dBFS, 1.25 : 1; gain updated every 32 samples)
        const auto follow = (float) (1.0 - std::exp (-1.0 / (0.03 * rate)));
        for (int i = 0; i < n; ++i)
        {
            compEnv += follow * (0.5f * (l[i] * l[i] + r[i] * r[i]) - compEnv);
            if ((compTick++ & 31) == 0)
            {
                const auto level = 10.0f * std::log10 (compEnv + 1e-12f);
                compGain = level > -30.0f ? std::pow (10.0f, -(level + 30.0f) * 0.2f / 20.0f) : 1.0f;
            }
            l[i] *= compGain;
            r[i] *= compGain;
        }
        if (choice (p[10], 2) == 0)
            echoStage (l, r, n, p, bpm, true);
    }
    void postStage (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (choice (p[10], 2) == 1)
            echoStage (l, r, n, p, bpm, false);
        else
            for (int i = 0; i < n; ++i)
            {
                l[i] += echoOut[0][(std::size_t) i];
                r[i] += echoOut[1][(std::size_t) i];
            }
    }

    Line echo[2];
    Shifter shifters[4];
    OnePole tone[2], lowCutState[2];
    float compEnv = 0.0f, compGain = 1.0f;
    unsigned compTick = 0;
    std::array<std::array<float, chunk>, 2> echoOut {};
    double wow = 0.0, crushPhase = 0.0;
    float crush[2] {};
};

// ---------------------------------------------------------------------------------------------- tables for the UI
std::vector<EffectParamInfo> params (std::initializer_list<EffectParamInfo> list) { return list; }
} // namespace

const char* epoch::modeName (int i) noexcept { return epochModes[std::clamp (i, 0, epoch::modeCount - 1)].name; }
const char* epoch::colourName (int i) noexcept { return epochColours[std::clamp (i, 0, epoch::colourCount - 1)]; }
double epoch::predelayMs (float v) noexcept { return epochPredelay (v); }
double epoch::decaySec (float v) noexcept { return epochDecay (v); }
const char* atrium::typeName (int i) noexcept { return atriumTypes[std::clamp (i, 0, atrium::typeCount - 1)].name; }
double atrium::decaySec (float v) noexcept { return atriumDecaySec (v); }
float atrium::decayValue (double s) noexcept { return (float) skewInverse (s, 0.1, 100.0, 5.717); }
double foil::lowCutHz (float v) noexcept { return v <= 0.0f ? 0.0 : 20.0 * std::pow (50.0, (double) std::min (v, 1.0f)); }
const char* foil::typeName (int i) noexcept { return foilTypes[std::clamp (i, 0, foil::typeCount - 1)].name; }
double foil::decaySec (float v) noexcept { return foilDecaySec (v); }
const char* nebula::modeName (int i) noexcept { return nebulaModes[std::clamp (i, 0, nebula::modeCount - 1)].name; }
double nebula::delayMs (float v) noexcept { return nebulaDelayMs (v); }
bool nebula::cloudMode (int mode) noexcept { return nebulaTopology (mode) != cloud::Topology::none; }
const char* horizon::echoModeName (int i) noexcept { return horizonEchoModes[std::clamp (i, 0, horizon::echoModeCount - 1)]; }
const char* horizon::reverbModeName (int i) noexcept { return horizonReverbs[std::clamp (i, 0, horizon::reverbModeCount - 1)].name; }
double horizon::decaySec (float v) noexcept { return horizonDecaySec (v); }
double horizon::echoMs (float v) noexcept { return horizonEchoMs (v); }

bool isLynxVerb (EffectKind k) noexcept
{
    return k == EffectKind::epochVerb || k == EffectKind::atrium || k == EffectKind::foilPlate || k == EffectKind::nebula
           || k == EffectKind::halo || k == EffectKind::horizon;
}

const EffectInfo& info (EffectKind kind)
{
    // defaults = the reference plugins' defaults (their saved normalised values)
    static const EffectInfo epochInfo { "Epoch Verb",
        params ({ { "Mix", 1.0f }, { "Predelay", 0.25f }, { "Decay", 0.42048f }, { "Size", 1.0f }, { "Attack", 0.5f },
                  { "Bass mult", 0.623039f }, { "Bass xover", 0.381065f }, { "High shelf", 0.0f }, { "High freq", 0.5f },
                  { "Early diffusion", 1.0f }, { "Late diffusion", 1.0f }, { "Mod rate", 0.245455f }, { "Mod depth", 0.38f },
                  { "High cut", 0.590539f }, { "Low cut", 0.0f }, { "Color", 0.0f }, { "Mode", 0.0f },
                  { "Law", 1.0f } }) }; // round 190bz: the 0.1.27 engine law (a project saved before keeps the classic engine: upgradeLoadedEffectParams)
    static const EffectInfo atriumInfo { "Atrium",
        params ({ { "Mix", 1.0f }, { "Predelay", 0.02f }, { "Decay", 0.5f }, { "High cut", 0.530201f }, { "Early/late", 0.5f },
                  { "Late size", 0.5f }, { "Late cross", 1.0f }, { "Late mod rate", 0.090909f }, { "Late mod depth", 0.5f },
                  { "Bass mult", 0.333333f }, { "Bass xover", 0.090909f }, { "High mult", 0.444444f }, { "High xover", 0.530201f },
                  { "Early size", 0.029029f }, { "Early cross", 0.1f }, { "Early mod rate", 0.090909f }, { "Early mod depth", 0.0f },
                  { "Early send", 0.0f }, { "Diffusion", 1.0f }, { "Type", 0.0f }, { "Space", 0.0f }, { "Low cut", 0.0f },
                  { "Law", 1.0f } }) }; // round 190bz
    static const EffectInfo foilInfo { "Foil Plate",
        params ({ { "Mix", 1.0f }, { "Predelay", 0.0f }, { "Decay", 0.5f }, { "Size", 0.5f }, { "Width", 0.5f },
                  { "Mod rate", 0.365345f }, { "Mod depth", 0.0f }, { "Low EQ freq", 0.343434f }, { "Low EQ gain", 0.5f },
                  { "High EQ freq", 0.494949f }, { "High EQ gain", 0.5f }, { "Type", 0.0f }, // EQ at 700 Hz / 10 kHz (round 129)
                  { "Infinite", 0.0f }, { "Low cut", 0.0f }, // round 135 (the Little Plate role)
                  { "Voicing", 0.0f }, // round 150: the measured plate voicing (off = the plate types as before)
                  { "View", 0.0f }, // round 152: the editor's view (Full | Plate: the one-knob panel of the Little Plate role)
                  { "Law", 1.0f } }) }; // round 190bz
    static const EffectInfo nebulaInfo { "Nebula",
        params ({ { "Mix", 0.5f }, { "Sync", 0.0f }, { "Note", 0.333333f }, { "Delay", 0.5f }, { "Warp", 0.0f },
                  { "Feedback", 0.5f }, { "Density", 0.0f }, { "Width", 1.0f }, { "Low cut", 0.0f }, { "High cut", 1.0f },
                  { "Mod rate", 0.273834f }, { "Mod depth", 0.5f }, { "Mode", 0.0f },
                  { "Clear", 0.0f }, // round 129: the reference's CLEAR button (any change of the value empties the network)
                  // round 190ca: the cloud law (core/NebulaCloud.h) for new instances; projects saved before keep the
                  // classic engine: upgradeLoadedEffectParams
                  { "Law", 1.0f } }) };
    static const EffectInfo haloInfo { "Halo",
        params ({ { "Mix", 0.5f }, { "Shift", 1.0f }, { "Feedback", 0.0f }, { "Diffusion", 0.919192f }, { "Size", 0.5f },
                  { "Low cut", 0.0f }, { "High cut", 0.530201f }, { "Mod rate", 0.1f }, { "Mod depth", 0.3f },
                  { "Reverb mode", 0.0f }, { "Shift mode", 0.0f }, { "Color", 0.0f },
                  { "Law", 1.0f } }) }; // round 190bz
    static const EffectInfo horizonInfo { "Horizon",
        params ({ { "Mix", 0.5f }, { "Width", 1.0f }, { "Echo sync", 0.0f }, { "Echo note", 0.4f }, { "Echo time", 0.15f },
                  { "Echo feedback", 0.0f }, { "Echo spread", 0.0f }, { "Echo drive", 0.125f }, { "Echo detune", 0.566667f },
                  { "Echo level", 0.0f }, { "Routing", 0.0f }, { "Decay", 0.291667f }, { "Size", 0.5f }, { "Attack", 0.5f },
                  { "Early/late", 0.5f }, { "Density", 1.0f }, { "Reverb level", 1.0f }, { "Mod rate", 0.132226f },
                  { "Mod depth", 0.4f }, { "Low EQ freq", 0.25f }, { "High EQ freq", 0.208197f }, { "Echo mode", 0.090909f },
                  { "Reverb mode", 0.142857f }, { "Color", 0.333333f },
                  { "Law", 1.0f } }) }; // round 190bz
    switch (kind)
    {
        case EffectKind::atrium: return atriumInfo;
        case EffectKind::foilPlate: return foilInfo;
        case EffectKind::nebula: return nebulaInfo;
        case EffectKind::halo: return haloInfo;
        case EffectKind::horizon: return horizonInfo;
        default: return epochInfo;
    }
}

std::string paramText (EffectKind kind, int param, float v, double bpm)
{
    (void) bpm;
    switch (kind)
    {
        case EffectKind::epochVerb:
            switch (param)
            {
                case 1: return ms (epochPredelay (v));
                case 2: return sec (epochDecay (v));
                case 5: return fmt ("%.2f X", epochBassMult (v));
                case 6:
                case 8:
                case 13: return hz (param == 6 ? epochBassXover (v) : epochFreq (v));
                case 7: return fmt ("%.1f dB", -24.0 + 24.0 * v);
                case 11: return fmt ("%.2f Hz", 0.1 + 9.9 * v);
                case 14: return hz (10.0 + 1490.0 * v);
                case 15: return epoch::colourName (choice (v, epoch::colourCount));
                case 16: return epoch::modeName (choice (v, epoch::modeCount));
                case epoch::engineLaw: return v >= 0.5f ? "Measured" : "Classic";
                default: return pct (v);
            }
        case EffectKind::atrium:
            switch (param)
            {
                case 1: return ms (500.0 * v);
                case 2: return sec (atriumDecaySec (v));
                case 3: return hz (100.0 + 14900.0 * v);
                case 5: case 6: case 8: case 14: case 16: case 17: case 18: return fmt ("%.2f", v);
                case 7: case 15: return fmt ("%.2f Hz", 0.05 + 4.95 * v);
                case 9: return fmt ("%.2f X", 0.5 + 1.5 * v);
                case 10: return hz (100.0 + 9900.0 * v);
                case 11: return fmt ("%.2f X", 0.1 + 0.9 * v);
                case 12: return hz (100.0 + 14900.0 * v);
                case 13: return ms (1.0 + 999.0 * v);
                case 19: return atrium::typeName (choice (v, atrium::typeCount));
                case 21: return hz (1000.0 * v);
                case atrium::engineLaw: return v >= 0.5f ? "Measured" : "Classic";
                default: return pct (v);
            }
        case EffectKind::foilPlate:
            switch (param)
            {
                case 1: return ms (epochPredelay (v));
                case 2: return sec (foilDecaySec (v));
                case 3: case 4: return pct (2.0 * v);
                case 5: return fmt ("%.2f Hz", skewLaw (v, 0.05, 5.0, 2.381));
                case 7: return hz (20.0 + 1980.0 * v);
                case 9: return hz (200.0 + 19800.0 * v);
                case 8: case 10: return fmt ("%+.1f dB", -12.0 + 24.0 * v);
                case 11: return foil::typeName (choice (v, foil::typeCount));
                case 12: return v >= 0.5f ? "on" : "off";
                case 13: return v <= 0.0f ? "off" : hz (foil::lowCutHz (v));
                case 14: return v >= 0.5f ? "measured" : "off";
                case 15: return v >= 0.5f ? "Plate" : "Full"; // round 152: the editor's view
                case foil::engineLaw: return v >= 0.5f ? "Measured" : "Classic";
                default: return pct (v);
            }
        case EffectKind::nebula:
            switch (param)
            {
                case 13: return "Clear";
                case 14: return v >= 0.5f ? "measured" : "classic";
                case 1: return syncNames[choice (v, 4)];
                case 2: return noteNames[choice (v, 7)];
                case 3: return ms (nebulaDelayMs (v));
                case 7: return pct (-1.0 + 2.0 * v);
                case 8: return hz (10.0 + 1990.0 * v);
                case 9: return hz (200.0 + 19800.0 * v);
                case 10: return fmt ("%.2f Hz", skewLaw (v, 0.01, 10.0, 2.328));
                case 12: return nebula::modeName (choice (v, nebula::modeCount));
                default: return pct (v);
            }
        case EffectKind::halo:
            switch (param)
            {
                case 1: return fmt ("%+.1f st", haloShiftSemis (v));
                // round 129: what the engine applies (0 … 250 Hz, off below 5 Hz; configure()) — the display said
                // 10 … 1000 Hz, the reference editor's range, while the filter never went above 250 Hz
                case 5: return 250.0 * v < 5.0 ? std::string ("off") : hz (250.0 * v);
                case 6: return hz (100.0 + 14900.0 * v);
                case 7: return fmt ("%.2f Hz", 0.05 + 4.95 * v);
                case 9: return haloModes[haloModeIndex (v)].name;
                case 10: return haloShiftModes[choice (v, 5)];
                case 11: return haloColours[choice (v, 2)];
                case halo::engineLaw: return v >= 0.5f ? "Measured" : "Classic";
                default: return pct (v);
            }
        case EffectKind::horizon:
            switch (param)
            {
                case 1: return pct (-1.0 + 2.0 * v);
                case 2: return syncNames[choice (v, 4)];
                case 3: return noteNames[std::min (5, choice (v, 6))];
                case 4: return ms (horizonEchoMs (v));
                case 7: return fmt ("%.1f dB", 24.0 * v);
                case 8: return fmt ("%+.1f ct", -30.0 + 60.0 * v);
                case 10: return choice (v, 2) == 0 ? "Echo > Reverb" : "Reverb > Echo";
                case 11: return sec (horizonDecaySec (v));
                case 17: return fmt ("%.2f Hz", skewLaw (v, 0.01, 10.0, 2.328));
                case 19: return hz (skewLaw (v, 20.0, 1000.0, 1.807));
                case 20: return hz (skewLaw (v, 1000.0, 20000.0, 1.078));
                case 21: return horizon::echoModeName (choice (v, horizon::echoModeCount));
                case 22: return horizon::reverbModeName (choice (v, horizon::reverbModeCount));
                case 23: return horizonColours[choice (v, 4)];
                case horizon::engineLaw: return v >= 0.5f ? "Measured" : "Classic";
                default: return pct (v);
            }
        default: return pct (v);
    }
}

std::unique_ptr<Effect> make (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::atrium: return std::make_unique<AtriumFx>();
        case EffectKind::foilPlate: return std::make_unique<FoilFx>();
        case EffectKind::nebula: return std::make_unique<NebulaFx>();
        case EffectKind::halo: return std::make_unique<HaloFx>();
        case EffectKind::horizon: return std::make_unique<HorizonFx>();
        default: return std::make_unique<EpochFx>();
    }
}
} // namespace lynx::verbs
