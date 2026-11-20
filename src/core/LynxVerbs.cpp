#include "core/LynxVerbs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>

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

// round 190ls: the dense late tank (Settings::dense): 32 lines, lengths as multiples of the geometric mean of the shape's
// set (× lineScale), spread over 0.55 … 1.6 of it on a near-geometric grid with jitter, alternately in the two halves
// (LYNX's own set: no common factors, every half spanning the whole range)
constexpr int denseCount = 32, denseHalf = 16;
// the outputs read every line through two Walsh rows (22 and 13): sign of line j = (−1)^popcount (j & row)
constexpr float walshSign (int j, unsigned row) noexcept { return (__builtin_popcount ((unsigned) j & row) & 1) != 0 ? -1.0f : 1.0f; }
constexpr std::array<float, 32> walshRow (unsigned row) noexcept
{
    std::array<float, 32> a {};
    for (int j = 0; j < 32; ++j)
        a[(std::size_t) j] = walshSign (j, row);
    return a;
}
constexpr auto denseOutL = walshRow (22u), denseOutR = walshRow (13u);
constexpr double denseRatio[denseCount] = {
    0.553, 0.571, 0.588, 0.611, 0.629, 0.652, 0.673, 0.697, 0.719, 0.743, 0.769, 0.793, 0.821, 0.847, 0.877, 0.905,
    0.937, 0.967, 1.001, 1.033, 1.069, 1.103, 1.142, 1.179, 1.219, 1.259, 1.303, 1.345, 1.391, 1.437, 1.487, 1.597 };

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

// round 190ls: the dense tank's 32 lines in one buffer, frame-major (frame f holds every line's sample at f × 32 … f × 32 + 31):
// every line has the same capacity and moves on one write position, so a sample's 32 writes are one contiguous store (measured
// against one buffer per line and line after line in one buffer: the fewest cycles)
struct DenseBank
{
    static constexpr int lines = 32;
    std::vector<float> buf;
    int mask = 0, pos = 0;

    void allocate (int samples)
    {
        const auto n = nextPow2 (std::max (4, samples + 4));
        buf.assign ((std::size_t) n * lines, 0.0f);
        mask = n - 1;
        pos = 0;
    }
    void clear() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        pos = 0;
    }
    // line j's sample written d writes ago (d ≥ 1)
    float read (int j, int d) const noexcept { return buf[(std::size_t) ((((pos - d) & mask) * lines) + j)]; }
    // fractional delay (≥ 2), 4-point Hermite (as Line::readFrac)
    float readFrac (int j, double d) const noexcept
    {
        const auto i = (int) d;
        const auto f = (float) (d - i);
        const auto xm1 = read (j, i - 1), x0 = read (j, i), x1 = read (j, i + 1), x2 = read (j, i + 2);
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    void write (const float* x) noexcept
    {
        std::copy (x, x + lines, buf.begin() + (std::ptrdiff_t) pos * lines);
        pos = (pos + 1) & mask;
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
    // round 190mj (Horizon's reaLaw granular echoes): four heads a quarter window apart, each sin²-windowed and the sum
    // scaled by ½ (the windows sum to 2): low tones keep their level, while a click — or a high tone, whose heads add
    // without coherence — carries half the energy of the two-head pitch shifter's (ValhallaFutureVerb's reverse / octave
    // modes: the impulse 3 … 4 dB under LYNX's two heads, the 4 … 10 kHz bursts 1 … 2 dB under, the low bursts level)
    float process4 (float x, double ratio) noexcept
    {
        line.write (x);
        phase += (1.0 - ratio) / window;
        phase -= std::floor (phase);
        float y = 0.0f;
        for (int h = 0; h < 4; ++h)
        {
            auto p = phase + 0.25 * h;
            p -= std::floor (p);
            const auto w = (float) std::sin (pi * p);
            y += w * w * line.readFrac (2.0 + p * window);
        }
        return 0.5f * y;
    }
};

double clampPos (double v, double lo, double hi) noexcept { return std::isfinite (v) ? std::clamp (v, lo, hi) : lo; }

// round 190it: where revision 1's build groups part (Hz; 250 / 400 / 600 / 800 tried on refcap030's room bank: 600 kept
// the long-decay music renders where they were); the lab build reads LYNX_SPLITHZ
double splitHz() noexcept
{
   #ifdef LYNX_VERB_FIT
    static const double hz = [] { const char* e = std::getenv ("LYNX_SPLITHZ"); return e != nullptr ? std::atof (e) : 600.0; }();
    return hz;
   #else
    return 600.0;
   #endif
}

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
    float splitLp[2][4] {};  // round 190it: the build groups' low band (one-pole states), which still reaches the lines as one sum
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

    // ---- the dense late tank (round 190ls; allocated only for an engine made with dense = true) ----
    bool denseAlloc = false;
    DenseBank dLine;
    double dCur[denseCount] {}, dKey[14 + denseCount] {};
    bool dDesigned = false;
    float dB0[loopShelves][denseCount] {}, dB1[loopShelves][denseCount] {}, dA1[loopShelves][denseCount] {}, dZ[loopShelves][denseCount] {};
    float dBw[denseCount] {}, dLc[denseCount] {};

    Impl (double maxLine, double maxPre, bool dense) : maxLineSec (maxLine), maxPreSec (maxPre), denseAlloc (dense)
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
        if (denseAlloc)
            dLine.allocate (maxLine + (int) (0.05 * sr));
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
        for (auto& c : splitLp)
            for (auto& z : c)
                z = 0.0f;
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
        if (denseAlloc)
        {
            dLine.clear();
            for (auto& row : dZ)
                for (auto& z : row)
                    z = 0.0f;
            for (int j = 0; j < denseCount; ++j)
                dBw[j] = dLc[j] = 0.0f;
            dDesigned = false;
        }
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
        shelfHighOn = std::abs (s.highShelfDb) > 0.01;
        if (s.shelfOrder == 1)
        {
            // round 190ln: first-order shelves, (gLow + gHigh·s/ω0) / (1 + s/ω0) through the bilinear transform with
            // k = 2·fs / ω0 (the corner not pre-warped): b0 = gLow + gHigh·k, b1 = gLow − gHigh·k, a0 = 1 + k, a1 = 1 − k
            const auto firstOrder = [this] (Biquad& f, double hz, double gLow, double gHigh)
            {
                const auto k = sr / (pi * std::max (1.0, hz));
                f.set (gLow + gHigh * k, gLow - gHigh * k, 0.0, 1.0 + k, 1.0 - k, 0.0);
            };
            if (shelfLowOn)
                firstOrder (shelfLow, s.lowShelfHz, std::pow (10.0, s.lowShelfDb / 20.0), 1.0);
            if (shelfHighOn)
                firstOrder (shelfHigh, s.highShelfHz, 1.0, std::pow (10.0, s.highShelfDb / 20.0));
            return;
        }
        if (shelfLowOn)
            shelfLow.shelf (sr, s.lowShelfHz, s.lowShelfDb, false, 0.7);
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
            float b0[loopShelves], b1[loopShelves], a1[loopShelves];
            designLine (s, sr, key[14 + i], tc, tb, b0, b1, a1);
            for (int k = 0; k < loopShelves; ++k)
            {
                shB0[k][i] = b0[k];
                shB1[k][i] = b1[k];
                shA1[k][i] = a1[k];
            }
        }
    }

    // round 190ls: the dense tank's line j (samples) for the shape and lineScale, whole samples
    double denseTarget (const Settings& s, int j) const noexcept
    {
        const auto shape = std::clamp (s.shape, 0, 2);
        double mean = 0.0;
        for (int i = 0; i < lineCount; ++i)
            mean += std::log (shapes[shape][i]);
        const auto ms = std::exp (mean / lineCount) * denseRatio[j] * std::max (0.0, s.lineScale);
        return std::floor (clampPos (ms * sr / 1000.0, 8.0, maxLineSec * sr));
    }
    // the dense tank's loop filters (as designLoop's, per line, redone only when a setting or a length moves)
    void designDense (const Settings& s) noexcept
    {
        double key[14 + denseCount] = { s.rt60, s.lowMult, s.lowXover, s.highMult, s.highXover, s.gateSec };
        for (int b = 0; b < engineBands; ++b)
            key[6 + b] = s.bandRtMult[b];
        for (int j = 0; j < denseCount; ++j)
            key[14 + j] = dCur[j];
        if (dDesigned && std::equal (std::begin (key), std::end (key), std::begin (dKey)))
            return;
        std::copy (std::begin (key), std::end (key), std::begin (dKey));
        dDesigned = true;
        double tc[loopShelves], tb[engineBands];
        for (int k = 0; k < loopShelves; ++k)
            tc[k] = std::tan (pi * std::min (loopShelfHz[k], 0.45 * sr) / sr);
        for (int b = 0; b < engineBands; ++b)
            tb[b] = std::tan (pi * std::min (bandHz[b], 0.45 * sr) / sr);
        for (int j = 0; j < denseCount; ++j)
        {
            float b0[loopShelves], b1[loopShelves], a1[loopShelves];
            designLine (s, sr, key[14 + j], tc, tb, b0, b1, a1);
            for (int k = 0; k < loopShelves; ++k)
            {
                dB0[k][j] = b0[k];
                dB1[k][j] = b1[k];
                dA1[k][j] = a1[k];
            }
        }
    }

    // one line's loop filter (law 1): a gain folded into six first-order shelves meeting the per-band loss of a line of
    // len samples (designLoop; round 190ls: also the dense tank's lines)
    static void designLine (const Settings& s, double sr, double len, const double* tc, const double* tb, float* b0, float* b1, float* a1) noexcept
    {
        {
            double target[engineBands], shelf[loopShelves], gainDb = 0.0;
            bandLossDb (s, sr, len, target);
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
                b0[k] = (float) (fold * (1.0 + tc[k] * r) / den);
                b1[k] = (float) (fold * (tc[k] * r - 1.0) / den);
                a1[k] = (float) ((tc[k] / r - 1.0) / den);
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
        // round 190ls: the dense tank (Atrium's ValhallaRoom role; not with a gate, a shimmer or split sides)
        const auto dense = denseAlloc && s.dense == 1 && s.gateSec <= 0.0 && s.splitSides == false
                           && s.shiftFeedback * s.shiftMix <= 0.0;
        double dTarget[denseCount] {};
        if (dense)
        {
            for (int j = 0; j < denseCount; ++j)
            {
                dTarget[j] = denseTarget (s, j);
                if (first || dCur[j] < 8.0)
                    dCur[j] = dTarget[j];
            }
            designDense (s);
        }
        bool dGlide = false; // (a length gliding: every line read through the interpolator, the glide run per sample)
        int dInt[denseCount] {};
        if (dense)
            for (int j = 0; j < denseCount; ++j)
            {
                dGlide = dGlide || dCur[j] != dTarget[j];
                dInt[j] = (int) dTarget[j];
            }
        first = false;
        double step[lineCount]; // round 190ie: the glide per sample (fixed, or a tempo change spread over the block)
        for (int i = 0; i < lineCount; ++i)
            step[i] = s.lengthSpread > 0 ? std::min (0.25, std::abs (target[i] - curLen[i]) / (double) s.lengthSpread) : 0.25; // (0.25: the fixed glide)
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
        const auto norm = (float) (1.0 / std::sqrt ((double) lineCount));
        const auto early = (float) s.earlyLevel, late = (float) s.lateLevel, send = (float) s.earlySend;
        const auto gated = s.gateSec > 0.0, build = gated || s.buildSec > 0.0005;
        const auto split = s.buildSplit && ! gated; // round 190it: the build-up taps in four groups (below)
        const auto splitCoef = (float) (1.0 - std::exp (-2.0 * pi * splitHz() / sr));
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
        const auto splitShare = 0.5f * (1.0f - onsetFeed); // (revision 1's build groups, below)
        if (onsetOn)
            for (int k = 0; k < onsetSteps; ++k)
                for (int j = 0; j < onsetChans; ++j)
                    onsetD[k][j] = std::clamp ((int) (std::clamp (s.onsetSec, 0.001, maxOnsetSec) * onsetSpan[k] * onsetFrac[k][j] * sr), 1,
                                               onsetLine[k][j].mask - 2);
        const auto bwOn = absorb[0].bw.a != 0.0f, lcOn = absorb[0].lc.a != 1.0f;
        const auto bwA = absorb[0].bw.a, lcA = absorb[0].lc.a;
        // (the tank's stored energy does not depend on its size, so 32 lines read at 1 / √32 would be 6 dB under the
        // 8-line tank: read at its 1 / √8, the levels of the front-ends hold)
        const auto denseNorm = (float) (1.0 / std::sqrt ((double) lineCount));
        constexpr float r2 = 0.70710678f; // (two inputs spread over four channels)
        constexpr float tiny = 1.0e-18f; // keeps the recirculating states out of the denormal range

        for (int t = 0; t < n; ++t)
        {
            float x[2] = { L[t], R[t] };
            float e[2], d[2], pin[2];
            float dg[2][4] {};
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
                if (build && split)
                {
                    // revision 1 (round 190it): four interleaved groups of the build-up taps, each spanning the whole
                    // build, reach the four lines of the half through a 4-point Hadamard (below) instead of one FIR
                    // into every line: one fixed random-sign FIR left its ripple (±3 … 4 dB, 50 … 200 Hz wide) on the
                    // whole tail; four independent ones average it out (a steady sine meets the tail's mean gain)
                    dif[c].write (v);
                    // (round 190kg, perf: the four groups' sums in registers, each still added in the taps' order — the
                    // indexed array went through memory at every tap)
                    static_assert (buildTaps % 4 == 0);
                    const auto tap = [&] (int k) noexcept { return bW[c][k] * (bD[c][k] == 0 ? v : dif[c].read (bD[c][k])); };
                    float g4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    {
                        float g0 = 0.0f, g1 = 0.0f, g2 = 0.0f, g3 = 0.0f;
                        for (int k = 0; k < buildTaps; k += 4)
                        {
                            g0 += tap (k);
                            g1 += tap (k + 1);
                            g2 += tap (k + 2);
                            g3 += tap (k + 3);
                        }
                        g4[0] = g0;
                        g4[1] = g1;
                        g4[2] = g2;
                        g4[3] = g3;
                    }
                    // below ~600 Hz the groups go on as one sum (the build's low band as before: there the few modes
                    // of the tank, not the taps, set the tone, and music read them as before); above, apart
                    float low = 0.0f;
                    for (int g = 0; g < 4; ++g)
                    {
                        splitLp[c][g] += splitCoef * (g4[g] - splitLp[c][g]);
                        low += splitLp[c][g];
                        dg[c][g] = g4[g] - splitLp[c][g];
                    }
                    v = low;
                }
                else if (build)
                {
                    dif[c].write (v);
                    float b = 0.0f;
                    for (int k = 0; k < buildTaps; ++k)
                        b += bW[c][k] * (bD[c][k] == 0 ? v : dif[c].read (bD[c][k]));
                    v = b;
                }
                d[c] = v;
            }
            // revision 1's build groups into the lines of each half: a 4-point Hadamard (signs per line as below), × the
            // build's share (round 190kg, perf: once per sample here instead of inside the line loop; the same operations)
            float splitIn[2][4];
            if (build && split)
                for (int c = 0; c < 2; ++c)
                {
                    const auto* q = dg[c];
                    splitIn[c][0] = splitShare * (q[0] + q[1] + q[2] + q[3]);
                    splitIn[c][1] = splitShare * (q[0] + (-q[1]) + q[2] + (-q[3]));
                    splitIn[c][2] = splitShare * (q[0] + q[1] + (-q[2]) + (-q[3]));
                    splitIn[c][3] = splitShare * (q[0] + (-q[1]) + (-q[2]) + q[3]);
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
            float lateL, lateR;
            if (dense)
            {
                // round 190ls: the dense tank (32 lines, two halves of 16)
                float y[denseCount];
                // the reference's layout: in each group of four lines two move — modulator j / 4 of the 8-line tank's
                // eight, one line each way — and two stay (read at whole samples unless their length glides)
                if (dGlide)
                    for (int j = 0; j < denseCount; ++j)
                        dCur[j] += std::clamp (dTarget[j] - dCur[j], -0.25, 0.25);
                const auto half = 0.5 * modDepth;
                for (int g = 0; g < denseCount; g += 4)
                {
                    const auto m = half * lfoS[g >> 2];
                    y[g] = dLine.readFrac (g, std::max (2.0, dCur[g] - half - m));
                    y[g + 1] = dLine.readFrac (g + 1, std::max (2.0, dCur[g + 1] - half + m));
                    if (dGlide)
                    {
                        y[g + 2] = dLine.readFrac (g + 2, dCur[g + 2]);
                        y[g + 3] = dLine.readFrac (g + 3, dCur[g + 3]);
                    }
                    else
                    {
                        y[g + 2] = dLine.read (g + 2, dInt[g + 2]);
                        y[g + 3] = dLine.read (g + 3, dInt[g + 3]);
                    }
                }
                for (int i = 0; i < lineCount; ++i)
                {
                    const auto c = lfoC[i] * lfoRc[i] - lfoS[i] * lfoRs[i];
                    lfoS[i] = lfoS[i] * lfoRc[i] + lfoC[i] * lfoRs[i];
                    lfoC[i] = c;
                }
                for (int k = 0; k < loopShelves; ++k)
                    for (int j = 0; j < denseCount; ++j)
                    {
                        const auto v = dB0[k][j] * y[j] + dZ[k][j];
                        dZ[k][j] = dB1[k][j] * y[j] - dA1[k][j] * v;
                        y[j] = v;
                    }
                if (bwOn)
                    for (int j = 0; j < denseCount; ++j)
                        y[j] = dBw[j] = y[j] + bwA * (dBw[j] - y[j]);
                if (lcOn)
                    for (int j = 0; j < denseCount; ++j)
                    {
                        dLc[j] = y[j] + lcA * (dLc[j] - y[j]);
                        y[j] -= dLc[j];
                    }
                // outputs: two Walsh rows (22 and 13) over all 32 lines
                float oL = 0.0f, oR = 0.0f;
                for (int j = 0; j < denseCount; ++j)
                {
                    oL += denseOutL[j] * y[j];
                    oR += denseOutR[j] * y[j];
                }
                lateL = oL * denseNorm;
                lateR = oR * denseNorm;
                const float in[2] = { (1.0f - onsetFeed) * (d[0] + send * e[0]) + onsetFeed * on[0] + tiny,
                                      (1.0f - onsetFeed) * (d[1] + send * e[1]) + onsetFeed * on[1] + tiny };
                // mixing: a 16-point Hadamard inside each half (four butterfly stages, × 1/4), then the halves rotated
                // into each other by crossFeed × 45° (swinging by the matrix modulator)
                {
                    // (stage by stage over both halves at once; a stage of span len pairs k and k + len in every block of
                    // 2 · len: the inner loops run over contiguous lines, so the wide stages are vector operations)
                    const auto stage = [&y] (auto span) noexcept
                    {
                        constexpr int len = decltype (span)::value;
                        for (int b = 0; b < denseCount; b += 2 * len)
                            for (int k = b; k < b + len; ++k)
                            {
                                const auto u = y[k], w = y[k + len];
                                y[k] = u + w;
                                y[k + len] = u - w;
                            }
                    };
                    stage (std::integral_constant<int, 1> {});
                    stage (std::integral_constant<int, 2> {});
                    stage (std::integral_constant<int, 4> {});
                    stage (std::integral_constant<int, 8> {});
                }
                auto crossCos = (float) crossCos0, crossSin = (float) crossSin0;
                if (mmDepth > 0.0)
                {
                    const auto c = mmC * mmRc - mmS * mmRs;
                    mmS = mmS * mmRc + mmC * mmRs;
                    mmC = c;
                    const auto dlt = mmDepth * mmS;
                    crossCos = (float) std::cos (crossAngle + dlt);
                    crossSin = (float) std::sin (crossAngle + dlt);
                }
                crossCos *= 0.25f;
                crossSin *= 0.25f;
                float w[denseCount];
                for (int j = 0; j < denseHalf; ++j)
                {
                    const auto a = y[j], bb = y[j + denseHalf];
                    // the input: ±1/4 on every line of its half; revision 1's four build groups on lines j & 3, ±1/2
                    const auto sgIn = (j & 1) == 0 ? 0.25f : -0.25f;
                    auto inL = sgIn * in[0], inR = sgIn * in[1];
                    if (build && split)
                    {
                        const auto sg = (j & 4) == 0 ? 0.5f : -0.5f;
                        inL += sg * splitIn[0][j & 3];
                        inR += sg * splitIn[1][j & 3];
                    }
                    w[j] = crossCos * a + crossSin * bb + inL;
                    w[j + denseHalf] = -crossSin * a + crossCos * bb + inR;
                }
                dLine.write (w);
            }
            else
            {
                float sOut[lineCount];
                for (int i = 0; i < lineCount; ++i)
                {
                    curLen[i] += std::clamp (target[i] - curLen[i], -step[i], step[i]);
                    if (s.lengthSpread > 0) // (round 190ie: the loop all-pass follows its gliding line)
                        loopAp[i].len = std::clamp ((int) (curLen[i] * loopApFraction), 1, loopAp[i].line.mask - 2);
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

                lateL = (sOut[0] + sOut[1] + sOut[2] - sOut[3] + sOut[4] + sOut[5] - sOut[6] + sOut[7]) * norm;
                lateR = (sOut[0] - sOut[1] + sOut[2] + sOut[3] + sOut[4] - sOut[5] + sOut[6] + sOut[7]) * norm;
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
                    if (build && split)
                    {
                        lines[i].write (u + alt * in[0] + splitIn[0][i]);
                        lines[i + 4].write (w + alt * in[1] + splitIn[1][i]);
                    }
                    else
                    {
                        lines[i].write (u + alt * in[0]);
                        lines[i + 4].write (w + alt * in[1]);
                    }
                }
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
        double target[lineCount], step[lineCount];
        for (int i = 0; i < lineCount; ++i)
        {
            target[i] = targetLen (s, i);
            // round 190ie: the glide per sample (fixed, or a tempo change spread over the block)
            step[i] = s.lengthSpread > 0 ? std::min (glide, std::abs (target[i] - curLen[i]) / (double) s.lengthSpread) : glide;
        }

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
                curLen[i] += std::clamp (target[i] - curLen[i], -step[i], step[i]);
                if (s.lengthSpread > 0) // (round 190ie: the loop all-pass follows its gliding line)
                    loopAp[i].len = std::clamp ((int) (curLen[i] * loopApFraction), 1, loopAp[i].line.mask - 2);
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
        if (s.classicTone == 1) // round 190mb: law 1's tone corrector after the classic loop (Nebula's Supermassive role)
        {
            designTone (s);
            if (toneOn)
                for (int k = 0; k < engineBands; ++k)
                    if (geqOn[k])
                        geq[k].processBlock (L, R, n);
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

Engine::Engine (double maxLineSec, double maxPredelaySec, bool dense) : impl (std::make_unique<Impl> (maxLineSec, maxPredelaySec, dense)) {}
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
        else if (k == "rev") s.revision = (int) v;
        else if (k == "dense") s.dense = (int) v;
        else if (k == "mrate") s.modRateHz *= v;
        else if (k == "split") s.buildSplit = v >= 0.5;
        else if (k == "lbw") s.loopBandwidth = v;
        else if (k == "hcut") s.outHighCut = v;
        else if (k == "lcut") s.outLowCut = v;
        else if (k == "lsdb") s.lowShelfDb = v;
        else if (k == "lshz") s.lowShelfHz = v;
        else if (k == "hsdb") s.highShelfDb = v;
        else if (k == "hshz") s.highShelfHz = v;
        else if (k == "tmdb") s.toneMidDb = v;
        else if (k == "ebuild") s.buildRise = v;
        else if (k == "shim") s.shimmerEnergy *= v;
        else if (k == "shfb") s.shiftFeedback *= v;
        else if (k == "lowmult") s.lowMult *= v;
        else if (k == "highmult") s.highMult *= v;
        else if (k.size() == 3 && k[0] == 'b' && (k[1] == 'f' || k[1] == 'g' || k[1] == 'q') && k[2] >= '0' && k[2] <= '2')
            (k[1] == 'f' ? s.bellHz : k[1] == 'g' ? s.bellDb : s.bellQ)[k[2] - '0'] = v;
    }
}
#endif

class VerbEffect : public Effect
{
public:
    VerbEffect (EffectKind k, double maxLineSec, double maxPreSec, bool dense = false) : kind (k), engine (maxLineSec, maxPreSec, dense) {}

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
            if (s.lengthSpread > 0) // (round 190ie: a tempo change spreads over each chunk it covers)
            {
                auto spread = s;
                spread.lengthSpread = m;
                engine.process (l, r, m, spread);
            }
            else
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

// round 190it: revision 1's trims per type / mode, measured on refcap030's black-box captures (each type / mode at the
// capture's base setting against the reference's impulse): the wet level (dB) and the tone corrector (dB added per octave
// band, 63 Hz … 8 kHz). Neutral = the 0.1.27 Measured law
struct Rev1Trim
{
    double gainDb = 0.0;
    double db[engineBands] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
};
// Epoch Verb ← ValhallaVintageVerb's ReverbMode sweep (base Decay 0.51 s; two passes of the closed loop; Concert Hall, the
// mode of every other capture, keeps the 0.1.27 voicing); faded out by Decay 1.41 s (the front-end)
LYNX_FIT_TABLE Rev1Trim epochTrim[epoch::modeCount] = {
    {}, // Concert Hall
    { 9.61, { -4.69, -4.20, -0.99, -0.50, -0.90, 0.78, 5.83, 8.39 } }, // Plate
    { 2.64, { 1.64, 1.89, -0.39, -1.30, 0.48, 0.52, -1.21, -1.82 } }, // Room
    { 5.75, { -5.15, -3.53, 0.12, -0.79, -1.89, 0.86, 5.24, 8.18 } }, // Chamber
    { 0.78, { -0.70, -0.01, -1.31, -2.10, -0.64, 1.80, 2.26, -1.39 } }, // Random Space
    { 6.89, { -1.34, 0.87, -0.46, -1.63, -0.84, 1.22, 0.85, -4.69 } }, // Chorus Space
    { 8.59, { -3.62, -4.93, -1.21, -0.82, -2.59, 2.12, 7.44, 9.41 } }, // Ambience
    { 5.04, { -3.57, -2.60, -2.05, -1.92, -1.13, 0.71, 6.98, 12.96 } }, // Bright Hall
    { 5.90, { 2.10, -2.48, -3.26, 0.65, 2.22, 3.39, -0.52, -14.57 } }, // Nave
    { 2.98, { -1.90, -5.90, -1.84, 2.74, 2.75, 5.34, -3.07, -20.56 } }, // Grit Hall
    { -0.75, { -0.85, -1.41, -1.33, -1.20, -0.41, 3.00, 1.35, -10.24 } }, // Grit Plate
    { 5.73, { -2.66, -3.85, -0.89, 1.00, 0.17, 2.85, 0.72, -12.61 } }, // Smooth Plate
    { 9.37, { 7.68, 7.33, 3.15, -4.53, -3.14, 1.07, -3.86, -17.78 } }, // Smooth Room
    { 6.79, { 8.10, 6.32, 0.66, -3.45, -1.97, 1.36, -2.91, -16.40 } }, // Smooth Space
    { -0.63, { 0.87, 1.07, 0.89, 0.50, 0.43, 2.94, -5.83, -17.37 } }, // Gated
    { 6.21, { -3.32, -3.60, -0.98, -1.01, -1.09, 1.08, 5.59, 10.46 } }, // Drift Chamber
    { 7.74, { -3.33, -6.49, -2.05, 2.00, 1.74, 3.35, 1.45, -11.92 } }, // Drift Hall
    { -3.48, { 1.08, 1.38, 0.80, -0.64, 0.29, 4.17, -6.00, -14.52 } }, // Drift Space
    { -4.48, { -0.55, -1.86, 0.11, 0.50, -0.27, 2.86, -1.36, -11.65 } }, // Cathedral
    { -3.93, { 3.64, -2.34, -0.88, 2.28, 2.00, 4.49, -5.56, -14.07 } }, // Grand Hall
    { -6.17, { 1.19, -1.22, -1.27, 0.88, 0.86, 2.55, -1.82, -11.16 } }, // Vintage Chamber
    { -2.85, { 0.07, -5.03, -1.68, 1.48, 1.22, 4.32, -0.31, -12.19 } }, // Vintage Hall
};
// round 190ly (epoch::shortTrim): Concert Hall at short decays ← ValhallaVintageVerb's ColorMode sweep and Decay pairs at
// 0.51 s (Concert Hall, the 1970s / 1980s and Now colours; closed loop on the impulses). Revision 1 left Concert Hall on its
// 0.1.27 voicing, fitted at 2 … 4 s: at 0.51 s the reference's impulse sat 2 dB over ours. Full at 0.51 s and under, faded
// out on the decay's logarithm by `epochShortEnd` s (S3 at 2.9 s already sits on the reference)
// (three passes of lab/vvtrim.py on 11 impulse renders, 500 Hz up; the loop's cuts under 500 Hz — −5.4 dB at 125 Hz — took
// the other banks' bass and impulse renders of the same setting 2 … 3 dB under their reference (tp172d S1, refcap182 S1_bass),
// so the lows keep the 0.1.27 voicing. The level stays where the loop put it: +0.7 … +1.9 dB more lifted the 1 kHz staircase
// files a step but took sr96 S1's impulse one down — Epoch sits 1.3 dB higher at 96 kHz than at 48 kHz there, a finding of its own)
// Round 190mb (the merge with round 190kw's loopLaw, which lifts Concert Hall's level at 0.51 s by its own law): the trim
// above stacked on the loop law's level took the impulses 1.9 dB over the reference (refcap030 mode / pairs files 4 → 3).
// Refitted on top of the loop law (three more passes of lab/vvtrim.py): −2.7 dB with the 500 Hz … 8 kHz bands, the lows
// cancelled out (they stay where the loop law puts them, as round 190ly kept them), and the 1 kHz band 2.5 dB over the
// impulse fit — the test signal's 1 kHz staircase sat 2.8 … 4.3 dB under the reference where the impulse sat on it
// (+1.5 / +2.5 / +3.5 dB tried: 2.5 takes every refcap030 / tp172d / refcap182 / verb183b file up or level)
LYNX_FIT_TABLE Rev1Trim epochShort = { -2.70, { 2.70, 2.70, 2.70, 4.56, 5.26, 2.58, 3.47, 2.03 } };
LYNX_FIT_TABLE double epochShortEnd = 2.9;
// round 190mb (nebula::toneLaw): ValhallaSupermassive's per-mode tone and level on Nebula's classic networks (law 0; the
// six cloud networks keep theirs), from refcap030's Mode sweep (impulse and test signal: Mix 100 %, Feedback 80 %,
// Density 0, Delay 50 %). There the classic networks' lows ran 2 … 8 dB over the reference and their top 1 … 3 dB under.
// At Feedback 50 % (tp172d's mode probes, the same states otherwise) the same kind of fit took 12 files down (the
// time-window spectra of music and bursts, not the long-term one the fit sees): the trim comes in from Feedback 65 % to
// its full value at 80 % and holds above. Gain (dB) + the tone corrector's octave bands 63 Hz … 8 kHz (dB)
// (three closed-loop passes of lab/nbtrim.py, each channel's power; Haze and Plateau 1 dB under the fit: their test-signal
// renders sat 2 dB louder than their impulse ones against the reference, the fit's full level took them a step down)
LYNX_FIT_TABLE Rev1Trim nebulaTone[nebula::modeCount] = {
    {}, // Ripple (a cloud network)
    { 1.14, { -2.89, -2.20, -1.08, -0.15, 0.76, 0.95, 1.72, 4.26 } }, // Swell
    {}, // Fold (a cloud network)
    {}, // Drift (a cloud network)
    {}, // Abyss (a cloud network)
    { 1.72, { -2.19, -2.35, -1.18, 0.20, 1.00, 0.94, 1.37, 3.41 } }, // Vortex
    {}, // Glint (a cloud network)
    { -2.46, { 2.69, -0.18, -1.46, -0.83, -0.36, 0.80, 2.03, 3.01 } }, // Pulse
    { 2.19, { -4.92, -4.71, -2.46, 1.62, 2.66, 1.34, 1.54, 2.05 } }, // Expanse
    { 0.54, { -4.89, -3.53, -0.91, 0.69, 0.68, 1.41, 1.65, 1.80 } }, // Haze
    { 0.34, { -3.16, -2.50, -1.07, 0.32, 0.58, 0.98, 1.67, 1.62 } }, // Veil
    { -0.10, { -2.62, -1.76, -0.74, 0.17, 0.89, 0.73, 0.69, 1.66 } }, // Mist
    { 1.53, { -4.93, -3.55, -0.86, 0.70, 0.65, 1.42, 1.64, 1.80 } }, // Monolith
    { -1.10, { 0.34, -0.42, -0.71, 0.33, 0.07, -0.26, 0.98, 0.78 } }, // Eternity
    { -0.25, { -2.52, -1.68, -0.70, 0.12, 0.71, 0.74, 0.82, 1.75 } }, // Tide
    { -0.37, { -2.05, -1.61, -0.89, 0.05, 0.87, 0.66, 0.94, 2.67 } }, // Shoal
    { -3.40, { 4.95, 3.28, 0.17, -1.24, -1.19, -0.86, -0.16, 0.80 } }, // Spark
    { 1.20, { -5.99, -2.90, 0.01, 0.69, 0.26, 1.33, 0.61, -1.10 } }, // Plateau
    { 3.94, { -6.25, -4.96, -2.22, 1.93, 2.96, 1.63, 0.65, -1.52 } }, // Dusk
    {}, // Mirage (a cloud network)
    { -0.87, { -0.80, -1.64, -1.29, -0.19, 0.94, 1.12, 1.05, 1.82 } }, // Cluster
    { -0.43, { -2.64, -1.81, -0.77, 0.13, 0.88, 0.76, 0.80, 1.89 } }, // Beacon
};
constexpr double nebulaToneFrom = 0.65, nebulaToneFull = 0.8; // Feedback
// round 190mb (horizon::revision): ValhallaFutureVerb's tone and level per reverb mode on Horizon's 0.1.27 law, from this
// round's probe renders of the reference (every reverb mode at its own value, Mix 100 %, Decay 25 %: impulse + test signal;
// three closed-loop passes of lab/hztrim.py). Gain (dB) + the tone corrector's octave bands 63 Hz … 8 kHz (dB). The bands
// under 500 Hz stay 0 (the fit's lows took the music files 1 … 2 dB loud); Room and Chamber keep no trim (Chamber, the
// default, took verb183b's music captures and Reverse's echo files down at any share tried; Room its music files), Space
// keeps its tone only (its +5.9 dB at the probe's value took tp172d's Space impulse 2 → 1: the level differs inside the mode)
LYNX_FIT_TABLE Rev1Trim horizonTrim[horizon::reverbModeCount] = {
    {}, // Room
    {}, // Chamber
    { -0.80, { 0.00, 0.00, 0.00, -0.68, -1.41, -0.67, 0.61, 3.13 } }, // Plate
    { 0.00, { 0.00, 0.00, 0.00, 1.15, -2.49, -1.76, -1.08, -0.43 } }, // Hall
    { -3.36, { 0.00, 0.00, 0.00, -2.01, -0.90, -0.10, 0.40, 2.23 } }, // Cathedral
    { 0.00, { 0.00, 0.00, 0.00, -0.16, 1.21, 0.98, 0.20, -0.17 } }, // Space
    { 1.95, { 0.00, 0.00, 0.00, -0.18, 0.19, 0.03, 0.64, 3.18 } }, // Freeze
    { -3.95, { 0.00, 0.00, 0.00, -0.92, 2.42, 0.89, -1.49, -0.54 } }, // Gated
};
// round 190mj (horizon::reaLaw) [EXEC]: ValhallaFutureVerb's echo modes against this round's probe renders of the echo alone
// (Reverb level 0, Echo level 100 %, Feedback 0 / 40 %; impulses at 0.5 / 0.05 / 0.005, tone bursts 100 Hz … 10 kHz at −40 /
// −20 / −6 dB, pink bursts, a sweep; lab/echoan.py): each mode's voicing (level of the heard echo, low-pass, drive)
struct HorizonEcho
{
    double gainDb = 0.0, cutHz = 0.0; // the echo's level (dB) and its low-pass (Hz; 0 = the mode's own, as before)
    int poles = 1;                    // the low-pass's order (1 or 2 one-pole sections)
    double driveMult = 1.0;           // × the Drive control's dB (the plain modes)
};
// Fitted on the echo alone (lab/echofit.py: gain, low-pass, order, drive per mode), then kept only where the probe bank's
// echo-mode renders (echo + Chamber, the reference's S2: Level 50 %, Feedback 40 %) gained without a file down: Analog a
// darker two-pole 3 kHz low-pass, Reverse Up/Down a two-pole 8 kHz one and +2 dB, Lo-fi a two-pole 4 kHz one. (The level the
// echo-alone fit asks for, +2 … +5 dB on most modes, took those renders down: the reverb behind the echo was measured with
// LYNX's echo level; the voicing's gain is the heard echo's only)
LYNX_FIT_TABLE HorizonEcho horizonEcho[horizon::echoModeCount] = {
    {}, {}, {},                    // Clean, Tape, Digital (its own coder)
    { 1.0, 3000.0, 2, 1.0 },       // Analog
    {}, {}, {}, {},                // Detune, Reverse, Reverse Up, Reverse Down
    { 2.0, 8000.0, 2, 1.0 },       // Reverse Up/Down
    {}, {},                        // Sparkle, Swarm
    { 1.0, 4000.0, 2, 1.0 },       // Lo-fi
};
// round 190mj (horizon::reaLaw) [EXEC]: Room and Chamber against this round's probe renders of the reference (each at Size
// 0 / 25 / 50 / 75 / 100 % × Decay 0.15 / 0.29 / 0.45: an impulse at 0.5 and 0.05, a pink burst, music). Their errors move
// with Size, not Decay: at Size 0 LYNX's first 100 ms sit 4 dB (Chamber) / 9 … 12 dB (Room) under the reference, at Size 100 %
// 2 … 3 dB over, and the tail at Size 25 … 50 % (Chamber) 2 … 3 dB over — which is why one trim per mode (round 190mb) took
// files down at the other sizes. The law, per mode at five Sizes (linear in between): the onset network's level (its span
// stays the mode's), the late tank's level (×) and the wet gain (dB)
struct HorizonRoomKnot
{
    double onset = -1.0, late = 1.0, gainDb = 0.0; // onset < 0: the mode's own
    double rt = 1.0, early = 1.0;                  // × the decay, × the early taps' level
};
// Fitted per Size on the probe renders (lab/roomfit.py: window levels 0-0.1 / 0.1-0.3 / 0.3-0.8 / 0.8-2 s after the impulse,
// the quiet impulse, the pink burst, the music), then eased until no file of the gate banks went down (Room: Size 25 % from
// +1 to +0.5 dB and its onset 0.45 → 0.3, Size 50 … 100 % without the fit's +1 … +2 dB, which the short decays' music did not
// want; Chamber: Size 50 % from late 0.6 / +3 dB to 0.75 / +1.7 dB, Size 75 % halfway, Size 100 % as it was)
LYNX_FIT_TABLE HorizonRoomKnot horizonRoomLaw[2][5] = {
    { { 0.7, 1.0, 3.0 }, { 0.3, 1.0, 0.5 }, {}, {}, { -1.0, 1.0, 0.5, 1.0, 0.5 } },                          // Room (its onset: 0)
    { { 0.7, 0.8, 2.0 }, { 0.45, 0.7, 2.0 }, { 0.45, 0.75, 1.7 }, { 0.3, 0.8, 1.5 }, {} },                   // Chamber (0.45)
};
// round 190mj: which granular echo modes run the four-head shifter (Shifter::process4) under reaLaw
// (Reverse Up, Reverse Down and Sparkle gained on the probe bank; Detune, Reverse, Reverse Up/Down and Swarm lost: two heads)
LYNX_FIT_TABLE bool horizonHeads4[horizon::echoModeCount] = { false, false, false, false, false, false, true, true, false, true, false, false };
// The Digital mode's coder (LYNX's own law of what the probe renders show: a single-sample impulse leaves the reference's
// Digital echo as the same small ramp-shaped pulse, ~19 dB under the clean echo's, whatever its height, while tones pass
// within ~1.3 dB up to −6 dB at 1 kHz, 4 kHz loses ~4 dB and 10 kHz ~20 dB — a coder that slews towards its input by a
// limited step): a two-pole pre low-pass, the slew limiter (Coder: stepMin per sample at 48 kHz, growing towards stepMax over
// tauMs while it stays at its limit), a post low-pass and a gain; fitted on the probe bank's Digital renders (1 / 2 / 1 → 2 /
// 3 / 3). Not reproduced: the reference's repeats under Feedback die after the first (its coder drops the smooth small pulse)
struct HorizonCoder
{
    double gainDb, preHz, postHz, stepMin, stepMax, tauMs, leak;
};
LYNX_FIT_TABLE HorizonCoder horizonCoder { -3.5, 6000.0, 10000.0, 0.002, 0.1, 1.0, 1.0 };
// round 190mb (epoch::rateLaw): ValhallaVintageVerb is not rate-independent where Epoch is (Epoch's impulse at 96 kHz is its
// 48 kHz one to 0.05 dB). Against its own 48 kHz renders the reference's sr96 captures (refcap182's D / S1 / S2 / S3 states)
// lose 1.4 … 1.7 dB in the first 80 ms and ~0.9 dB to 300 ms in its 1970s colour (ColorMode 0.33: D / S1 / S2), nothing in the
// tail, while in its 1980s colour (0.8: S3) its tail sits ~0.5 dB higher (colour names read back from the plug-in this round). Per colour: the early field's and the whole wet level's change per octave of rate (dB)
struct EpochRate
{
    double earlyDb, levelDb;
};
// Fitted on refcap030's sr96 block (20 files; lab/erate.sh grid): 1970s (D / S1 / S2 / S4) −2 dB a rate octave on the early
// field, 1980s (S3) +0.5 dB on the level; Now has no capture and stays. Every file keeps its score (the step scorer), the mean |level| error 0.88 → 0.76 dB and spectrum 2.18 → 2.11 dB;
// more early cut or a level cut on colour 0 took 1 … 3 files a step down
LYNX_FIT_TABLE EpochRate epochRate[epoch::colourCount] = { { -2.0, 0.0 }, { 0.0, 0.5 }, { 0.0, 0.0 } };
// round 190kw (revision 1 + loopLaw): Epoch's level over Decay and Size from the reference's loop (static REA of
// ValhallaVintageVerb, docs/analise/REA_rodada190kw.md §4 / §11). Each of its modes is one loop whose gain is the RT60 law
// 10^(−3 L / (fs T)) split over N gain stages, the input entering after a stage, with no output gain of its own: its wet
// energy is E(T, s) = E0 / (1 − 10^(−6 ℓ(s) / T)), ℓ = c · L(s) / N, and the first 80 ms do not move with Decay. Per mode:
// ℓ at Size 100 % (s), the loop's size law L(s) / L(1) = a + b · s^e, the level at the capture's base Decay (0.513 s) against
// revision 1 (dB, refcap030's mode sweep, impulse) and Epoch's own wet-energy growth over Decay (dB at the knots below,
// measured on the impulse with the law's level off). The trim is the reference's growth minus Epoch's own. Fitted on the
// scores after that (the stand-in's spectrum is not the reference's, so the level that scores best is not always the
// energy match): Concert Hall +0.3 dB, and its own growth 0.1 / 0.6 / 0.2 / 0.9 dB less at 2 / 4 / 7 / 15 s and 0.3 / 0.3 dB
// more at 33 / 70 s (refcap030's pairs and sr48 / sr96 captures, refcap182's programme and tp172d at 0.51 / 2.9 / 4 / 7 /
// 12.8 / 33 s), Room −0.4 dB (its 1.41 s captures), Bright Hall +1.25 dB, Grit Plate −3.25 dB, Gated −2 dB (its impulse).
// A negative ℓ: the mode keeps revision 1 (early part and level as round 190it left them).
struct EpochLoop
{
    double ell, a, b, e, atDb;
    double grow[8];
};
constexpr double epochLoopKnots[8] = { 0.513, 1.0, 2.0, 4.0, 7.0, 15.0, 33.0, 70.0 }; // Decay (s)
LYNX_FIT_TABLE EpochLoop epochLoop[epoch::modeCount] = {
    { 0.39520, 0.200, 0.800, 1.0, 0.85, { 0.00, 0.35, 1.39, 2.79, 4.78, 6.48, 10.11, 12.31 } }, // Concert Hall: 0.5605 s (0.2 + 0.8 s), N 2, c 1.41
    { 0.18654, 0.16155, 0.83845, 1.0, -2.05, { 0.00, 0.93, 3.42, 6.14, 8.22, 10.84, 13.33, 15.50 } }, // Plate: 5100 (1 + 5.19 s) / 33850, N 5
    { 0.13013, 0.250, 0.750, 1.0, -1.22, { 0.00, 1.76, 3.86, 5.93, 7.57, 9.72, 11.82, 13.71 } }, // Room: 26770 (0.5 + 1.5 s) / 48000, N 6, c 0.7
    { 0.15545, 0.16155, 0.83845, 1.0, -1.68, { 0.00, 1.09, 3.60, 6.26, 8.29, 10.85, 13.28, 15.42 } }, // Chamber: as Plate, N 6
    { 0.26899, 0.19750, 0.80250, 1.0, 1.43, { 0.00, 1.14, 3.53, 6.35, 8.53, 11.28, 13.87, 16.12 } }, // Random Space: 5100 (3 + 12.19 s) / 48000, N 6
    { 0.26899, 0.19750, 0.80250, 1.0, 0.97, { 0.00, 1.00, 3.10, 5.71, 7.84, 10.58, 13.18, 15.44 } }, // Chorus Space: as Random Space
    { 0.20566, 0.36630, 0.63370, 1.0, -2.95, { 0.00, 0.18, 2.39, 4.89, 6.81, 9.25, 11.59, 13.67 } }, // Ambience: 5100 (3 + 5.19 s) / 33850, N 6
    { 0.30797, 0.200, 0.800, 1.0, -3.41, { 0.00, -2.44, -2.10, -0.65, 0.79, 2.92, 5.15, 7.22 } }, // Bright Hall: 11210 (0.2 + 0.8 s) / 18200, N 2
    { 1.13677, 0.200, 0.800, 1.0, -2.93, { 0.00, -0.33, 0.74, 2.65, 4.39, 6.81, 9.25, 11.46 } }, // Nave ← Sanctuary: 21826 (1 + 4 s) / 48000, N 2
    { 0.28025, 0.200, 0.800, 1.0, -2.47, { 0.00, -1.33, -1.02, 0.30, 1.65, 3.68, 5.86, 7.89 } }, // Grit Hall ← Dirty Hall: as Concert Hall, c 1
    { 0.08252, 0.09434, 0.90566, 1.0, -0.33, { 0.00, 1.65, 4.25, 6.99, 9.06, 11.66, 14.12, 16.29 } }, // Grit Plate ← Dirty Plate: 11210 (0.8 s fs / 20000 + 0.2) / fs (48 kHz), N 6
    { 0.47326, 0.05263, 0.94737, 1.0, -2.39, { 0.00, 0.29, 1.66, 3.60, 5.29, 7.60, 9.90, 11.97 } }, // Smooth Plate: 4250 (1 + 18 s) / 34125, N 5
    { 0.32854, 0.07582, 0.92418, 1.0, -3.36, { 0.00, 1.43, 3.90, 6.05, 7.84, 10.21, 12.47, 14.33 } }, // Smooth Room: 5100 (1 + 12.19 s) / 34125, N 6
    { 0.26899, 0.19750, 0.80250, 1.0, -2.55, { 0.00, 1.10, 3.35, 5.62, 7.53, 10.07, 12.52, 14.59 } }, // Smooth Space ← Smooth Random: as Random Space
    { 0.0, 0.07582, 0.92418, 2.0, 0.43, { 0.00, -0.44, -0.36, -0.36, -2.06, -4.42, -7.61, -7.28 } }, // Gated ← Nonlin: 5100 (1 + 12.19 s²) / 34125; its gate, not the loop, sets the level
    { -0.39438, 0.15789, 0.84211, 1.0, -3.96, { 0.00, -0.95, 0.21, 2.17, 3.87, 6.15, 8.39, 10.40 } }, // Drift Chamber ← Chaotic Chamber: 4250 (3 + 16 s) / 34125, N 6; keeps revision 1 (its test signal lost a step)
    { 0.28025, 0.200, 0.800, 1.0, -1.57, { 0.00, -0.73, -0.13, 1.48, 3.03, 5.26, 7.56, 9.66 } }, // Drift Hall ← Chaotic Hall: as Grit Hall
    { 0.44835, 0.11111, 0.88889, 1.0, -0.02, { 0.00, 0.96, 3.35, 6.09, 8.20, 10.87, 13.39, 15.59 } }, // Drift Space ← Chaotic Neutral: 5100 (2 + 16 s) / 34125, N 6
    { 0.38147, 0.200, 0.800, 1.0, 1.39, { 0.00, 0.91, 1.36, 2.99, 4.45, 6.83, 9.64, 12.25 } }, // Cathedral: 30000 (0.5 + 2 s) / 32768, N 6
    { 0.32005, 0.03125, 0.96875, 1.5, 0.43, { 0.00, -0.82, -0.12, 1.73, 3.59, 6.23, 8.84, 11.17 } }, // Grand Hall ← Palace: 61449 (0.125 + 3.875 s^1.5) / 48000, N 16
    { 0.41550, 0.500, 0.500, 1.0, 2.93, { 0.00, 0.97, 3.13, 5.73, 7.76, 10.34, 12.79, 14.97 } }, // Vintage Chamber ← Chamber1979: 9972 (0.5 + 0.5 s) / 20000 per line, RT 1.2 × Decay
    { 0.46667, 0.500, 0.500, 1.0, 2.33, { 0.00, -0.79, -0.33, 1.10, 2.54, 4.67, 6.91, 8.97 } }, // Vintage Hall ← Hall1984: 11200 (0.5 + 0.5 s) / 20000 per line, RT 1.2 × Decay
};
// the level trim (dB) of revision 1 + loopLaw for mode k at Decay T (s) and Size s (0..1): the base level, plus the
// reference's growth from (0.513 s, 100 %) to (T, s) through its own size law, minus Epoch's own growth over Decay and Size
double epochLoopDb (int k, double decay, double size) noexcept
{
    const auto& m = epochLoop[k];
    if (m.ell <= 0.0)
        return m.atDb;
    const auto t = std::clamp (decay, epochLoopKnots[0], epochLoopKnots[7]);
    const auto grow = [t] (double ell) { return -10.0 * std::log10 (std::max (1.0e-9, 1.0 - std::pow (10.0, -6.0 * ell / t))); };
    const auto lawSize = m.a + m.b * std::pow (std::clamp (size, 0.0, 1.0), m.e);
    // Epoch's own lines scale by 0.3 + 0.7 s, and its wet energy rises by that factor^−0.64 at any Decay (measured:
    // +1.2 / +2.0 dB at Size 50 / 25 %, Concert Hall and Room, 0.51 … 12.8 s)
    const auto ownSizeDb = -6.4 * std::log10 (0.3 + 0.7 * std::clamp (size, 0.0, 1.0));
    const auto ref = grow (m.ell * lawSize) - ownSizeDb + 10.0 * std::log10 (1.0 - std::pow (10.0, -6.0 * m.ell / epochLoopKnots[0]));
    int i = 0;
    while (i < 6 && t > epochLoopKnots[i + 1])
        ++i;
    const auto f = std::log (t / epochLoopKnots[i]) / std::log (epochLoopKnots[i + 1] / epochLoopKnots[i]);
    const auto own = m.grow[i] + (m.grow[i + 1] - m.grow[i]) * std::clamp (f, 0.0, 1.0);
    return m.atDb + ref - own;
}

// Atrium ← ValhallaRoom's type sweep (3-point smoothed over the bands, half the 63 Hz band's; Large Room, the type of
// every other capture, keeps its 0.1.27 voicing; Medium Room's level a compromise between its impulse (+2 dB) and its
// test signal (−3 dB))
LYNX_FIT_TABLE Rev1Trim atriumTrim[atrium::typeCount] = {
    {}, // Large Room
    { -1.50, { 0.77, -1.36, -0.63, 0.39, 0.02, 0.45, 1.13, 1.24 } }, // Medium Room
    {}, // Bright Room (its 0.1.27 law was fitted on tp172d's S2, which the sweep's trim cost a step)
    { -0.75, { 0.04, 0.01, 0.26, 1.05, 0.45, -0.63, -1.14, -1.63 } }, // Large Chamber
    { 0.40, { 0.01, 1.43, 0.90, 0.45, 0.00, -0.87, -1.90, -3.50 } }, // Dark Room
    { -0.20, { 0.08, 0.20, 0.11, 0.92, 0.43, -0.85, -0.82, -0.22 } }, // Dark Chamber
    { 2.30, { -1.15, -0.14, 0.89, 1.18, 0.04, -1.07, -0.91, -0.23 } }, // Dark Space
    { 3.75, { -1.13, 0.74, 1.19, 0.98, -0.26, -1.38, -1.26, -0.93 } }, // Deep Space
    { 2.42, { -0.56, 0.82, 0.80, 0.56, 0.07, -0.91, -1.35, -1.30 } }, // Mirror Space
    { 3.05, { -0.86, 0.03, 1.16, 1.51, 0.16, -1.32, -1.54, -1.17 } }, // Vast Space
    { 5.16, { -1.21, 1.24, 1.62, 1.05, -0.22, -1.62, -2.06, -2.00 } }, // Distant Space
    {}, // Dense Room
};
// Foil Plate ← ValhallaPlate's type sweep (as Atrium's; Silver and Pewter, the types of the other captures, keep theirs:
// Pewter's trim at the sweep's decay cost the 0.5 decay of refcap182's S2 a step); the last entry: the measured plate voicing
LYNX_FIT_TABLE Rev1Trim foilTrim[foil::typeCount + 1] = {
    {}, // Silver
    {}, // Pewter
    { 3.26, { -1.89, -0.54, 0.24, 0.47, 0.04, -0.13, -0.08, -0.79 } }, // Nickel
    { 1.65, { -0.36, 0.27, 0.31, 0.61, 0.10, -0.82, -0.46, 0.09 } }, // Bronze
    { 1.87, { -1.12, -0.78, 0.06, 1.34, 0.90, -0.35, -1.18, -2.21 } }, // Bismuth
    { 0.39, { 1.63, 1.84, 0.02, -0.88, -0.69, -0.38, 0.09, 0.21 } }, // Tin
    { 0.28, { -0.85, -0.82, -0.67, -0.31, -0.10, 0.50, 1.39, 1.48 } }, // Zinc
    { 3.88, { 0.55, 1.93, 1.04, -0.30, -0.64, -1.11, -0.92, -0.18 } }, // Platinum
    { 2.14, { 0.71, 0.58, -1.25, -1.52, -0.19, 1.17, 1.20, -0.70 } }, // Tungsten
    { 3.74, { -0.55, 0.54, 0.41, 0.29, -0.02, -0.26, -0.96, -3.08 } }, // Iron
    { 2.39, { -0.35, -0.03, -1.06, -0.94, -0.02, 0.97, 1.08, -0.80 } }, // Gold
    { 0.68, { 0.93, 0.69, -0.59, -0.32, 0.30, 0.22, -0.31, -1.42 } }, // Lead
    {}, // the measured plate voicing
};
LYNX_FIT_TABLE Rev1Trim haloTrim[7] = {};                   // per reverb mode
// round 190ls: Atrium's ValhallaRoom role on the dense tank (atrium::reaLaw), per type on top of revision 1's trims: the
// dense tank's level and tone against the reference's impulses (refcap030's type sweep, two closed-loop passes, 3-point
// smoothed over the bands; Large Room, the type of every other capture, 0.65 dB under the sweep's level and half its lift at
// 63 / 125 Hz: at the sweep's trim the music, bass and test signals of the pairs / sr blocks and of tp172d / refcap182 /
// verb183b sat 0.5 … 2 dB over the reference)
LYNX_FIT_TABLE Rev1Trim roomTrim[atrium::typeCount] = {
    { -0.60, { 1.03, 0.50, -0.10, 0.05, -0.27, -0.79, 0.09, 0.54 } }, // Large Room
    { -2.19, { 4.15, -0.45, -0.07, 0.99, -0.27, -0.42, 0.23, 0.60 } }, // Medium Room
    { -0.50, { 1.74, -0.45, -0.11, 0.45, -0.00, -0.30, 0.42, 1.86 } }, // Bright Room
    { -0.90, { 5.12, 1.05, -0.79, -0.20, -0.22, -0.17, 0.33, 0.91 } }, // Large Chamber
    { -0.22, { 2.35, -0.07, -0.42, 0.41, -0.23, -0.19, 0.50, 0.02 } }, // Dark Room
    { -0.14, { 3.70, 1.76, 0.92, 0.09, -0.98, -1.17, -0.62, 0.23 } }, // Dark Chamber
    { -0.90, { 5.38, 0.64, -0.91, 0.26, -0.09, -0.27, 0.36, 0.77 } }, // Dark Space
    { -0.55, { 4.95, 0.34, -0.59, 0.62, -0.06, -0.37, 0.06, 0.44 } }, // Deep Space
    { -0.57, { 4.55, 0.62, -0.71, 0.55, 0.20, -0.52, -0.15, 0.46 } }, // Mirror Space
    { -0.40, { 4.47, 0.77, 0.41, 0.10, -0.67, -0.45, -0.16, 0.43 } }, // Vast Space
    { -0.38, { 5.46, 0.52, -0.40, 0.80, -0.20, -0.65, -0.08, 0.34 } }, // Distant Space
    {}, // Dense Room (no capture)
};
#ifdef LYNX_VERB_FIT
// the fitting harness: LYNX_VTRIM=<file>, lines "<epoch|atrium|foil|halo> <index> <gain dB> <8 × dB>"
bool loadTrims()
{
    if (const char* f = std::getenv ("LYNX_VTRIM"))
        if (auto* in = std::fopen (f, "r"))
        {
            char name[32];
            int i;
            Rev1Trim t;
            while (std::fscanf (in, "%31s %d %lf %lf %lf %lf %lf %lf %lf %lf %lf", name, &i, &t.gainDb, &t.db[0], &t.db[1], &t.db[2],
                                &t.db[3], &t.db[4], &t.db[5], &t.db[6], &t.db[7]) == 11)
            {
                const std::string n = name;
                if (n == "erate") // round 190mb: "erate <colour> <early dB> <level dB> + 7 unused"
                {
                    if (i >= 0 && i < epoch::colourCount)
                        epochRate[i] = { t.gainDb, t.db[0] };
                    continue;
                }
                if (n == "hecho") // round 190mj: "hecho <echo mode> <dB> <cut Hz> <poles> <drive ×> + 5 unused"
                {
                    if (i >= 0 && i < horizon::echoModeCount)
                        horizonEcho[i] = { t.gainDb, t.db[0], (int) t.db[1], t.db[2] };
                    continue;
                }
                if (n == "hroom") // round 190mj: "hroom <mode 0 | 1> <onset> <late ×> <size knot 0 … 4> <gain dB> <rt × (0 = 1)> <1 + early × (0 = 1)> + 3 unused"
                {
                    const auto k = (int) t.db[1];
                    if (i >= 0 && i < 2 && k >= 0 && k < 5)
                        horizonRoomLaw[i][k] = { t.gainDb, t.db[0], t.db[2], t.db[3] > 0.0 ? t.db[3] : 1.0, t.db[4] > 0.0 ? t.db[4] - 1.0 : 1.0 };
                    continue;
                }
                if (n == "hheads") // round 190mj: "hheads <echo mode> <0 | 1> + 8 unused"
                {
                    if (i >= 0 && i < horizon::echoModeCount)
                        horizonHeads4[i] = t.gainDb >= 0.5;
                    continue;
                }
                if (n == "hcoder") // round 190mj: "hcoder 0 <gain dB> <pre Hz> <post Hz> <step min> <step max> <tau ms> <leak> + 2 unused"
                {
                    horizonCoder = { t.gainDb, t.db[0], t.db[1], t.db[2], t.db[3], t.db[4], t.db[5] };
                    continue;
                }
                if (n == "eshort") // round 190ly: "eshort <end s> <gain> <8 × dB>"
                {
                    epochShortEnd = i > 0 ? (double) i / 10.0 : epochShortEnd; // (index = the end in tenths of a second)
                    epochShort = t;
                    continue;
                }
                Rev1Trim* table = n == "epoch" ? epochTrim : n == "atrium" ? atriumTrim : n == "foil" ? foilTrim : n == "halo" ? haloTrim : n == "room" ? roomTrim
                                : n == "nebula" ? nebulaTone : n == "horizon" ? horizonTrim : nullptr;
                const int count = n == "epoch" ? epoch::modeCount : n == "nebula" ? nebula::modeCount : n == "horizon" ? horizon::reverbModeCount : n == "atrium" || n == "room" ? atrium::typeCount : n == "foil" ? foil::typeCount + 1 : 7;
                if (table != nullptr && i >= 0 && i < count)
                    table[i] = t;
            }
            std::fclose (in);
        }
    return true;
}
#endif
void applyTrim (Settings& s, const Rev1Trim& t, double weight = 1.0) noexcept
{
   #ifdef LYNX_VERB_FIT
    static const bool loaded = loadTrims();
    (void) loaded;
   #endif
    s.gain *= std::pow (10.0, weight * t.gainDb / 20.0);
    for (int b = 0; b < engineBands; ++b)
        s.bandDb[b] += weight * t.db[b];
}
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
        // round 190kw (loopLaw): the reference's first 80 ms do not move with Decay (−10.65 dB at 0.51 / 7 / 33 s): the
        // early level holds at its long-decay value
        const auto loopLaw = p[epoch::engineLaw] >= 0.5f && p[epoch::revision] >= 0.5f && p[epoch::loopLaw] >= 0.5f
                          && epochLoop[modeIndex].ell >= 0.0;
        s.earlyLevel *= loopLaw ? 1.6 : std::min (1.6, 1.13 * std::sqrt (std::max (1.0, decay / 2.0)));
        s.gain *= 0.92;
        if (p[epoch::engineLaw] >= 0.5f)
            applyLaw (s, epochLaw[modeIndex]);
        if (p[epoch::engineLaw] >= 0.5f && p[epoch::revision] >= 0.5f)
        {
            s.revision = 1; // round 190it
            // Epoch's trims were measured at the capture's base Decay (0.51 s); its Room mode needs none at 1.41 s
            // (refcap182 S2): they fade out between the two on the decay's logarithm
            const auto fade = std::clamp (std::log (1.41 / std::max (0.01, decay)) / std::log (1.41 / 0.51), 0.0, 1.0);
            if (! loopLaw)
                applyTrim (s, epochTrim[modeIndex], fade);
            else
            {
                // round 190kw: the tone trims fade as before; the level is the loop law's at every Decay and Size
                auto tone = epochTrim[modeIndex];
                const auto levelDb = tone.gainDb;
                tone.gainDb = 0.0;
                applyTrim (s, tone, fade);
                s.gain *= std::pow (10.0, (levelDb + epochLoopDb (modeIndex, decay, size)) / 20.0);
            }
            if (p[epoch::shortTrim] >= 0.5f && modeIndex == 0) // round 190ly: Concert Hall's short-decay trim (tone only)
                if (const auto w = std::clamp (std::log (epochShortEnd / std::max (0.01, decay)) / std::log (epochShortEnd / 0.51), 0.0, 1.0); w > 0.0)
                    applyTrim (s, epochShort, w);
            if (p[epoch::rateLaw] >= 0.5f && rate != 48000.0) // round 190mb: the reference's level away from 48 kHz
            {
               #ifdef LYNX_VERB_FIT
                applyTrim (s, Rev1Trim {}, 0.0); // (loads the fit file's erate lines)
               #endif
                const auto octaves = std::log2 (rate / 48000.0);
                s.earlyLevel *= std::pow (10.0, epochRate[colour].earlyDb * octaves / 20.0);
                s.gain *= std::pow (10.0, epochRate[colour].levelDb * octaves / 20.0);
            }
        }
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

// round 190ls (atrium::reaLaw): the ValhallaRoom role on the dense late tank, after revision 1. The reference's late network
// is 32 lines whose eight modulators move half of them (static REA); its steady 1 kHz tone (refcap030's staircase) then
// swings by 2.0 dB (sd of 10 ms levels) where the 8-line tank's swung 1.2 dB and sat 2 dB under it on average. The dense tank,
// its moving lines at four times revision 1's depth, meets it (2.3 dB; Late mod depth 50 %, 0.5 Hz: ±1.1 ms), and the trims
// per type (roomTrim) take its tone back onto the reference's impulses (docs/analise/REA_rodada190ls.md)
LYNX_FIT_TABLE double roomModScale = 4.0;
void applyRoomLaw (const EffectParams& p, Settings& s) noexcept
{
    s.dense = 1;
    s.modDepthMs *= roomModScale;
    applyTrim (s, roomTrim[choice (p[19], atrium::typeCount)]);
}

class AtriumFx final : public VerbEffect
{
public:
    AtriumFx() : VerbEffect (EffectKind::atrium, 0.25, 1.05, true) {} // (round 190ls: with the dense tank)

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
        if (p[atrium::engineLaw] >= 0.5f && p[atrium::revision] >= 0.5f)
        {
            s.revision = 1; // round 190it
            applyTrim (s, atriumTrim[choice (p[19], atrium::typeCount)]);
            s.buildSplit = true;
            if (p[atrium::reaLaw] >= 0.5f)
                applyRoomLaw (p, s);
        }
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
            if (p[foil::revision] >= 0.5f)
            {
                s.revision = 1; // round 190it
                applyTrim (s, foilTrim[p[14] >= 0.5f ? foil::typeCount : choice (p[11], foil::typeCount)]);
                // round 190ln (reaLaw, the ValhallaPlate role): the reference's own laws, read by static REA
                // (docs/analise/REA_rodada190ln.md). Its Low / High EQ are first-order shelves with the corner at the
                // panel's frequency (20 … 2000 / 200 … 20000 Hz) and ±12 dB (the gain linear in dB); ours were second-order
                // shelves placed by ear (half-gain near 0.47 / 0.5 × the frequency, the cut of the high one −9 dB). Its
                // bass decay is longer than Decay from ~2.5 s on (foil::reaBassRtMult); the voicings above were fitted at
                // the default Decay (3 s), so the bass decay moves by the law's ratio to that point (its loop's bass
                // crossover, a one-pole at 200 Hz, was tried for ours at 250 Hz: one file a step lower, none higher)
                if (p[foil::reaLaw] >= 0.5f && p[14] < 0.5f)
                {
                    s.shelfOrder = 1;
                    s.lowShelfHz = 20.0 + 1980.0 * p[7];
                    s.lowShelfDb = -12.0 + 24.0 * p[8];
                    s.highShelfHz = 200.0 + 19800.0 * p[9];
                    s.highShelfDb = -12.0 + 24.0 * p[10];
                    if (! infinite)
                        s.lowMult *= foil::reaBassRtMult (rt) / foil::reaBassRtMult (3.0);
                }
            }
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
        // round 190ie (issue #185): a synced delay whose tempo moved (and nothing else of its time): the lines' change
        // spreads over the block (cloud::Controls::spread, Settings::lengthSpread) instead of the fixed glide's burst
        tempoGlide = choice (p[1], 4) > 0 && lastBpm > 0.0 && bpm != lastBpm && p[1] == before[1] && p[2] == before[2] && p[3] == before[3];
        lastBpm = bpm;
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
            {
                auto c = controls (q, bpm, active);
                c.spread = tempoGlide && change == Fade::none ? m : 0;
                network.process (inL.data(), inR.data(), l, r, m, c);
            }
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
    double lastBpm = 0.0;
    bool tempoGlide = false; // (this block: the synced delay's tempo moved)
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
        s.lengthSpread = tempoGlide ? 1 : 0; // (round 190ie: VerbEffect::run spreads it over each chunk)
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
        if (p[nebula::toneLaw] >= 0.5f) // round 190mb: the Supermassive role's per-mode tone and level (classic networks)
        {
            s.classicTone = 1;
            const auto k = choice (p[12], nebula::modeCount);
            applyTrim (s, nebulaTone[k], std::clamp ((fb - nebulaToneFrom) / (nebulaToneFull - nebulaToneFrom), 0.0, 1.0));
        }
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

// round 190it (revision 1): the RT per reverb mode against the reference's (refcap030's mode sweep, impulses: ours ran
// ×1.13 / ×0.75 / ×1.0 / ×2.85 its RT in Mono / Small / Medium / Big stereo)
LYNX_FIT_TABLE double haloRev1Rt[7] = { 1.0, 0.88, 1.33, 1.0, 1.0, 0.35, 1.0 };
#ifdef LYNX_VERB_FIT
const double* haloRtTable()
{
    static const bool once = []
    {
        if (const char* e = std::getenv ("LYNX_HALORT"))
            std::sscanf (e, "%lf,%lf,%lf,%lf,%lf,%lf,%lf", &haloRev1Rt[0], &haloRev1Rt[1], &haloRev1Rt[2], &haloRev1Rt[3], &haloRev1Rt[4], &haloRev1Rt[5], &haloRev1Rt[6]);
        return true;
    }();
    (void) once;
    return haloRev1Rt;
}
#else
const double* haloRtTable() { return haloRev1Rt; }
#endif

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
            if (p[halo::engineLaw] >= 0.5f && p[halo::revision] >= 0.5f)
                s.rt60 *= haloRtTable()[haloModeIndex (p[9])];
        }
        // (the energy of its impulse matched before the RT did: a mode's RT moves with the energy kept, gain × 1 / √RT ratio)
        const auto rtKeep = p[halo::engineLaw] >= 0.5f && p[halo::revision] >= 0.5f ? 1.0 / std::sqrt (haloRtTable()[haloModeIndex (p[9])]) : 1.0;
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
        // round 190it (revision 1): the reference's Dark colour is a tone on the output, not in the loop: against
        // Bright its tail keeps the RT at every octave (8.4 / 7.8 s at 4 / 8 kHz against 8.8 / 8.7) and its spectrum
        // falls −3 / −6.6 / −11.7 dB at 4 / 8 / 16 kHz and −4.7 … −7.9 dB below 50 Hz, the mids −0.5 dB (refcap030's
        // colour sweep); the loop low-pass took the highs' RT down to 2.2 s and 2 dB of the level
        const auto rev1 = p[halo::engineLaw] >= 0.5f && p[halo::revision] >= 0.5f;
        s.loopBandwidth = dark && ! rev1 ? 5000.0 : 0.0;
        if (dark && rev1)
        {
            s.toneHighHz = 6500.0;
            s.toneHighDb = -13.0;
            s.toneLowHz = 70.0;
            s.toneLowDb = -6.0;
        }
        s.modRateHz = 0.05 + 4.95 * p[7];
        s.modDepthMs = 1.2 * p[8] / std::sqrt (std::max (0.3, s.modRateHz));
        // round tp172d: the wet level measured on music and drums (refs/halo): −3 dB up to Feedback 75 %, rising to +1 dB
        // at 100 % (the reference swells). Its impulse has a fuller low end than ours (+9 / +5 dB at 63 / 125 Hz) but its
        // music and drums don't (a shelf for the impulse made drums 5 dB loud there): left flat
        {
            const auto fb = (double) p[2];
            const auto dn = std::clamp ((double) p[3] / 0.92, 0.0, 1.0); // less diffusion: louder (+4.5 / +5.4 dB measured at 0 / 50 %)
            // round 190it (revision 1, refcap030: Feedback × Diffusion pairs, music and impulse): half that lift (at 20 /
            // 50 % diffusion ours ran 2.5 dB over its level at 80 %, where the reference's does not), and 0.5 dB less overall
            // (its music and test signal sat 2.6 dB under ours at the capture's base setting, the impulse 0.1 dB: −1 dB
            // scored higher on refcap030 but cost tp172d's impulses, −0.5 dB is the cut no bank loses a block to)
            const auto trimDb = (fb <= 0.75 ? -3.0 : -3.0 + 16.0 * (fb - 0.75)) + (rev1 ? 2.5 : 5.0) * (1.0 - dn * dn * dn) - (rev1 ? 0.5 : 0.0);
            s.gain = (dark ? (rev1 ? 0.95 * 0.94 : 0.75) : 0.95) * std::pow (10.0, trimDb / 20.0) * rtKeep;
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
        if (p[halo::engineLaw] >= 0.5f && p[halo::revision] >= 0.5f)
        {
            s.revision = 1; // round 190it
            applyTrim (s, haloTrim[haloModeIndex (p[9])]);
            s.buildSplit = true;
        }
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
        tone2[0].z = tone2[1].z = 0.0f;
        lowCutState[0].z = lowCutState[1].z = 0.0f;
        compEnv = 0.0f;
        compGain = 1.0f;
        compTick = 0;
        wow = 0.0;
        crush[0] = crush[1] = 0.0f;
        crushPhase = 0.0;
        for (auto& c : codeState)
            c.clear();
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
        if (p[horizon::engineLaw] >= 0.5f && p[horizon::revision] >= 0.5f) // round 190mb: the reference's tone and level per mode
            applyTrim (s, horizonTrim[mode]);
        if (p[horizon::engineLaw] >= 0.5f && p[horizon::revision] >= 0.5f && p[horizon::reaLaw] >= 0.5f && mode <= 1)
        {
            // round 190mj: Room / Chamber's law over Size (horizonRoomLaw)
           #ifdef LYNX_VERB_FIT
            static const bool loaded = loadTrims();
            (void) loaded;
           #endif
            const auto at = std::clamp (size, 0.0, 1.0) * 4.0;
            const auto k0 = std::min (3, (int) at);
            const auto f = at - k0;
            const auto& a = horizonRoomLaw[mode][k0];
            const auto& b = horizonRoomLaw[mode][k0 + 1];
            const auto onsetA = a.onset < 0.0 ? s.onsetLevel : a.onset, onsetB = b.onset < 0.0 ? s.onsetLevel : b.onset;
            s.onsetLevel = onsetA + (onsetB - onsetA) * f;
            s.lateLevel *= a.late + (b.late - a.late) * f;
            s.gain *= std::pow (10.0, (a.gainDb + (b.gainDb - a.gainDb) * f) / 20.0);
            s.rt60 *= a.rt + (b.rt - a.rt) * f;
            s.earlyLevel *= a.early + (b.early - a.early) * f;
        }
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
        // round 190mj (reaLaw): the reference's voicing per echo mode (level, low-pass, drive) and its Digital mode's coder
        const auto rea = p[horizon::engineLaw] >= 0.5f && p[horizon::revision] >= 0.5f && p[horizon::reaLaw] >= 0.5f;
       #ifdef LYNX_VERB_FIT
        static const bool loaded = loadTrims();
        (void) loaded;
       #endif
        const HorizonEcho voicing = rea ? horizonEcho[mode] : HorizonEcho {};
        const auto drive = granular ? 1.0 : std::pow (10.0, 24.0 * p[7] * voicing.driveMult / 20.0);
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
        if (voicing.cutHz > 0.0)
            cut = voicing.cutHz;
        tone[0].setLowpass (cut, rate);
        tone[1].a = tone[0].a;
        const auto twoPole = voicing.poles >= 2;
        const auto fourHeads = rea && granular && horizonHeads4[mode];
        tone2[0].a = tone2[1].a = tone[0].a;
        const auto lowCut = skewLaw (p[19], 20.0, 1000.0, 1.807);
        lowCutState[0].setLowpass (lowCut, rate);
        lowCutState[1].a = lowCutState[0].a;
        // (the voicing's level is the heard echo's — Level's share beside the reverb —, not what the loop or the reverb gets:
        // the reverb's own level law was measured with the echo as it was)
        const auto heard = rea ? level * (float) std::pow (10.0, voicing.gainDb / 20.0) : level;
        const auto coder = rea && mode == 2;
        if (coder)
        {
            const auto& k = horizonCoder;
            for (int c = 0; c < 2; ++c)
            {
                codeState[c].pre.setLowpass (k.preHz, rate);
                codeState[c].pre2.a = codeState[c].pre.a;
                codeState[c].post.setLowpass (k.postHz, rate);
            }
            codeGain = (float) std::pow (10.0, k.gainDb / 20.0);
            const auto toRate = 48000.0 / rate; // (the steps are per sample at 48 kHz: the same slew per second at any rate)
            codeMin = (float) (k.stepMin * toRate);
            codeMax = (float) (k.stepMax * toRate);
            codeTau = (float) (1.0 - std::exp (-1.0 / (std::max (0.01, k.tauMs) * 0.001 * rate)));
            codeLeak = (float) std::pow (k.leak, toRate);
        }
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
                    auto a = fourHeads ? shifters[c].process4 (v, ratio) : shifters[c].process (v, ratio);
                    if (dual)
                        a = 0.5f * (a + (fourHeads ? shifters[c + 2].process4 (v, ratio2) : shifters[c + 2].process (v, ratio2)));
                    v = a;
                }
                if (coder)
                    v = codeState[c].run (v, codeMin, codeMax, codeTau, codeLeak) * codeGain;
                else
                    v = tone[c].lp (v);
                if (twoPole)
                    v = tone2[c].lp (v);
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
                echoOut[0][(std::size_t) i] = heard * y[0];
                echoOut[1][(std::size_t) i] = heard * y[1];
                l[i] = y[0];
                r[i] = y[1];
            }
            else
            {
                l[i] = x[0] + heard * y[0];
                r[i] = x[1] + heard * y[1];
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

    // round 190mj: the Digital echo's slope-limited coder (LYNX's own law, see horizonCoder): the output slews towards the
    // (pre-filtered) input by at most a step per sample; the step grows towards stepMax (syllabic, over tauMs) while the
    // slew stays at its limit for three samples or more, and falls back to stepMin when it does not
    struct Coder
    {
        OnePole pre, pre2, post;
        float y = 0.0f, syl = 0.0f;
        int run1 = 0;
        float run (float x, float stepMin, float stepMax, float tau, float leak) noexcept
        {
            const auto in = pre2.lp (pre.lp (x));
            const auto step = stepMin + (stepMax - stepMin) * syl;
            const auto e = in - y;
            const auto limited = std::abs (e) > step;
            y = leak * (y + std::clamp (e, -step, step));
            run1 = limited ? run1 + 1 : 0;
            syl += tau * ((run1 >= 3 ? 1.0f : 0.0f) - syl);
            return post.lp (y);
        }
        void clear() noexcept { pre.z = pre2.z = post.z = y = syl = 0.0f; run1 = 0; }
    };
    Coder codeState[2];
    OnePole tone2[2]; // round 190mj: the echo low-pass's second section (reaLaw voicings with two poles)
    float codeGain = 1.0f, codeMin = 0.0f, codeMax = 0.0f, codeTau = 0.0f, codeLeak = 1.0f;
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
// round 190ln (static REA of ValhallaPlate, docs/analise/REA_rodada190ln.md): its tank runs two sets of lines, one at Decay
// and one at a longer bass decay that equals Decay up to ~2.5 s and grows to 1.4 × Decay by ~10 s (and stays there). Our
// own law of that shape: a linear rise of the multiple between the two points
double foil::reaBassRtMult (double decay) noexcept { return 1.0 + 0.4 * std::clamp ((decay - 2.5) / 7.5, 0.0, 1.0); }
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
