// Helix, LYNX's own wavetable synth (round 190ja, core/WavetableSynth.h). Evidence tags: [DATA] read in Live's saved
// sets / presets (the parameter set, ranges and defaults), [MEASURED] black-box Live 12.4 renders of the 190ja render
// sets (docs/analise/REA_rodada190ja.md, the rows named), [MODEL] LYNX's own choice where no measurement decides.

#include "core/WavetableSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <type_traits>

#include "core/HostSlide.h"
#include "core/ModelParams.h"
#include "core/Simd.h"
#include "core/Song.h"

// the per-sample helpers stay inlined in the voice loops (190mi: Helix+'s second call sites made GCC call them, +14 % on
// the default patch's instructions)
#if defined(__GNUC__) || defined(__clang__)
#define HELIX_INLINE inline __attribute__ ((always_inline))
#define HELIX_LAMBDA_INLINE __attribute__ ((always_inline))
#else
#define HELIX_INLINE __forceinline
#define HELIX_LAMBDA_INLINE
#endif

namespace lynx
{
namespace wt
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int controlRate = 16; // samples per modulation step (envelopes, LFOs, the matrix)

// --- measured constants (REA_rodada190ja §2; each names the render rows that fix it)
struct Laws
{
    double centreGain = 0.70710678; // an oscillator at Gain 1, pan centre: each side −3 dB [MEASURED a0, a1: a sine peaks −9.03 dBFS at Volume −6 dB]
    double oscPitchSemis = 24.0;    // Osc Pitch ← 1 [MEASURED m1 osc1pitch: 0.25 × velocity 127 = +6 st]
    double pitchSemis = 48.0;       // Pitch ← 1 [MEASURED m1 pitch: 0.25 → +12 st; Live's MIDI tab shows Pitch ← PB 1/24 as "2"]
    double noteSpan = 48.0;         // the Note source: (key − 60) / 48 [MEASURED m2 pitch note0.25: 3 st per octave, C1 … C8]
    double keyTrackOctaves = 4.0;   // Filter Freq ← Note 1: one octave per octave [MEASURED m2 f1 note1: C1 … C6]
    double filterOctaves = 10.0;    // Filter Freq ← 1 (other sources): the knob's whole range, 20 Hz … 20.48 kHz [MEASURED n4, approx.]
    double fmIndex = 10.0;          // FM Amt 1: index 10 rad, a sine at the ratio moving the read phase [MEASURED x1: exact on Amt 0.25 / 0.5 / 1]
    double fmOctaves = 2.0;         // FM Pitch ±1: the modulator 2 octaves up / down [MEASURED x1: ratios 0.25 / 0.5 / 1 / 2 / 4]
    double syncSpan = 3.0;          // Sync: the synced wave at 1 + 3 × Sync times the note [MEASURED x2: 1.75 / 2.5 / 4, exact]
    double foldSpan = 7.0;          // Fold: the read phase folded at 1 + 7 × Fold times its speed, mirrored at half the cycle
                                    // [MEASURED x3 fold0.25 / 0.5 / 1: exact]
    double warpCurve = 32.0;        // Warp w: the phase x / (x + c (1 − x)), c = 1 / (1 + 32 w²) (w < 0: mirrored) [MEASURED x3: exact]
    double unisonCents = 50.0;      // Classic unison, Amount 1: the outer voices ± cents [MEASURED u*]
    double lfoRateOctaves = 11.55;  // an LFO rate row ← 1: × 2^this (the 0.01 … 30 Hz knob) [MODEL]
    double slopeCurve = 7.6;        // envelope slope ±1: the exponential's steepness [MEASURED e0 / e1 / e2: slopes ±0.5 / ±1]
    double subLevel = 1.0;          // the sub at Gain 1 = an oscillator at Gain 1 [MEASURED s1]
};
constexpr Laws laws {};

// Live's synced LFO rates, index → bars per cycle [MEASURED l2: index 6 = 1 bar, 11 = 5/16, 15 = 1/8 at 120 BPM; the rest
// follows the menu's order, MODEL]
constexpr double syncBars[22] { 8.0, 6.0, 4.0, 3.0, 2.0, 1.5, 1.0, 0.75, 0.5, 0.375, 1.0 / 3.0, 0.3125, 0.25, 0.1875, 1.0 / 6.0, 0.125,
                                1.0 / 12.0, 0.0625, 1.0 / 24.0, 0.03125, 1.0 / 48.0, 0.015625 };

inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
inline float clampBi (float v) noexcept { return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v; }

} // namespace

double filterHz (double normalised) noexcept { return 20.0 * std::pow (2.0, 10.0 * std::clamp (normalised, 0.0, 1.0)); }
double filterNorm (double hz) noexcept { return std::log2 (std::clamp (hz, 20.0, 20480.0) / 20.0) / 10.0; }
double lfoSyncBeats (int index) noexcept { return 4.0 * syncBars[std::clamp (index, 0, 21)]; }
int polyVoiceCount (int index) noexcept { return std::clamp (index, 0, 6) + 2; } // [DATA: 0…6, default 6 = 8 voices]
double fmRatio (double fmPitch) noexcept { return std::pow (2.0, laws.fmOctaves * std::clamp (fmPitch, -1.0, 1.0)); }
double syncRatio (double sync) noexcept { return 1.0 + laws.syncSpan * std::clamp (sync, 0.0, 1.0); }

double unisonCents (int mode, double amount) noexcept
{
    amount = std::clamp (amount, 0.0, 1.0);
    switch (mode)
    {
        case unisonNone: return 0.0;
        case unisonPositionSpread: return laws.unisonCents * 0.1 * amount;
        default: return laws.unisonCents * amount;
    }
}

namespace
{
// segment()'s body, inlined into the envelopes' step (190mi: the same arithmetic, so the same values)
HELIX_INLINE float segmentInline (float x, float slope) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);
    if (std::abs (slope) < 1.0e-4f)
        return x;
    // slope > 0: fast at first (an exponential approach); < 0: slow at first [MEASURED e1*, e2*]
    const auto k = (float) laws.slopeCurve * std::abs (slope);
    const auto e = 1.0f - std::exp (-k);
    if (slope > 0.0f)
        return (1.0f - std::exp (-k * x)) / e;
    return 1.0f - (1.0f - std::exp (-k * (1.0f - x))) / e;
}
// std::lround's value (half away from zero) without the library call, for the finite values a state holds
HELIX_INLINE std::int64_t roundHalfAway (double x) noexcept
{
    if (! (std::abs (x) < 1.0e9)) // (beyond any value a state holds: the library's)
        return (std::int64_t) std::lround (x);
    auto r = (std::int64_t) x;
    const auto f = x - (double) r;
    if (f >= 0.5)
        ++r;
    else if (f <= -0.5)
        --r;
    return r;
}
} // namespace

float segment (float x, float slope) noexcept { return segmentInline (x, slope); }

// --- Helix+ laws (round 190mi; LYNX's own [MODEL], no measurement decides them)
namespace
{
// the asymmetric split point of Asym + / − / ± (b −1…1)
inline double asymSplit (double b) noexcept { return 0.5 + 0.48 * std::clamp (b, -1.0, 1.0); }
// Bend's exponent (b −1…1: < 0 the middle of the cycle shrinks, > 0 it stretches)
inline double bendPower (double b) noexcept { return b >= 0.0 ? 1.0 + 4.0 * b : 1.0 / (1.0 - 4.0 * b); }
inline double warpBipolar (int mode, double a) noexcept
{
    return mode == warp::bendMinus || mode == warp::asymMinus ? -a : mode == warp::bendBoth || mode == warp::asymBoth ? 2.0 * a - 1.0 : a;
}
} // namespace

double warpStretch (int mode, double a, double otherRatio) noexcept
{
    a = std::clamp (a, 0.0, 1.0);
    auto s = 1.0;
    switch (mode)
    {
        case warp::sync: case warp::windowSync: s = 1.0 + 15.0 * a; break;
        case warp::bendPlus: case warp::bendMinus: case warp::bendBoth:
        {
            // the curve's steepest slope: the power at the ends (> 1) or near the middle (< 1: capped, its steep part is short)
            const auto pw = bendPower (warpBipolar (mode, a));
            s = pw >= 1.0 ? pw : std::min (8.0, 1.0 / pw);
            break;
        }
        case warp::pwm: s = 1.0 / std::max (1.0 - 0.95 * a, 0.05); break;
        case warp::asymPlus: case warp::asymMinus: case warp::asymBoth:
        {
            const auto m = asymSplit (warpBipolar (mode, a));
            s = std::max (0.5 / m, 0.5 / (1.0 - m));
            break;
        }
        case warp::mirror: s = a > 1.0e-4 ? 2.0 : 1.0; break;
        case warp::remap: s = 1.0 + 0.94 * a; break;
        case warp::fm: s = 1.0 + 4.0 * pi * a * std::max (otherRatio, 0.0); break;
        case warp::am: case warp::rm: s = a > 1.0e-4 ? 1.0 + std::max (otherRatio, 0.0) : 1.0; break;
        default: break;
    }
    return std::clamp (s, 1.0, 64.0);
}

double warpPhase (int mode, double a, double x) noexcept
{
    a = std::clamp (a, 0.0, 1.0);
    x = std::clamp (x, 0.0, 1.0);
    switch (mode)
    {
        case warp::sync: case warp::windowSync:
        {
            const auto q = x * (1.0 + 15.0 * a);
            return std::min (q - std::floor (q), 0.9999999);
        }
        case warp::bendPlus: case warp::bendMinus: case warp::bendBoth:
        {
            const auto u = 2.0 * x - 1.0;
            return 0.5 + 0.5 * std::copysign (std::pow (std::abs (u), bendPower (warpBipolar (mode, a))), u);
        }
        case warp::pwm:
        {
            const auto w = 1.0 - 0.95 * a;
            return x < w ? x / w : 1.0;
        }
        case warp::asymPlus: case warp::asymMinus: case warp::asymBoth:
        {
            const auto m = asymSplit (warpBipolar (mode, a));
            return x < m ? 0.5 * x / m : 0.5 + 0.5 * (x - m) / (1.0 - m);
        }
        case warp::mirror: return a > 1.0e-4 ? (x < 0.5 ? 2.0 * x : 2.0 - 2.0 * x) : x;
        case warp::remap: return std::clamp (x - a * 0.15 * std::sin (2.0 * pi * x), 0.0, 0.9999999);
        default: return x;
    }
}

float drawnLfo (const double* pts, double x) noexcept
{
    const auto n = std::clamp ((int) std::lround (pts[0]), 0, ext::maxPoints);
    if (n == 0)
        return 0.0f;
    const auto px = [pts] (int i) { return std::clamp (pts[1 + i * ext::pointStride], 0.0, 1.0); };
    const auto py = [pts] (int i) { return std::clamp (pts[2 + i * ext::pointStride], -1.0, 1.0); };
    const auto pc = [pts] (int i) { return std::clamp (pts[3 + i * ext::pointStride], -1.0, 1.0); };
    if (n == 1)
        return (float) py (0);
    x -= std::floor (x);
    int i = -1;
    for (int j = 0; j < n; ++j)
        if (px (j) <= x)
            i = j;
    // the segment from the last point at or before x to the next one (after the last point: around to the first)
    const auto from = i < 0 ? n - 1 : i, to = i < 0 ? 0 : (i + 1) % n;
    const auto x0 = i < 0 ? px (n - 1) - 1.0 : px (from);
    const auto x1 = (i < 0 || i + 1 < n) ? px (to) : px (to) + 1.0;
    const auto t = x1 - x0 > 1.0e-9 ? std::clamp ((x - x0) / (x1 - x0), 0.0, 1.0) : 1.0;
    return (float) (py (from) + (py (to) - py (from)) * (double) segment ((float) t, (float) pc (from)));
}

int unisonStackSemis (int stack, int voice) noexcept
{
    static const std::array<std::vector<int>, 7> stacks { { { 0 }, { 0, 12 }, { 0, 12, -12 }, { 0, 12, 24 }, { 0, 7 }, { 0, 7, 12 }, { 0, -12, 12, -24, 24 } } };
    const auto& s = stacks[(std::size_t) std::clamp (stack, 0, 6)];
    return s[(std::size_t) (std::max (voice, 0) % (int) s.size())];
}

double delayBeats (int index) noexcept
{
    static constexpr double beats[10] { 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 4.0 };
    return beats[std::clamp (index, 0, 9)];
}

std::array<double, 3> formantHz (double vowel) noexcept
{
    // A E I O U: textbook averages of adult vowel formants (F1, F2, F3), LYNX's own rounding
    static constexpr double f[5][3] { { 800, 1150, 2800 }, { 450, 1900, 2600 }, { 300, 2250, 3000 }, { 450, 850, 2600 }, { 330, 750, 2400 } };
    const auto v = std::clamp (vowel, 0.0, 1.0) * 4.0;
    const auto i = std::min ((int) v, 3);
    const auto t = v - i;
    return { f[i][0] + (f[i + 1][0] - f[i][0]) * t, f[i][1] + (f[i + 1][1] - f[i][1]) * t, f[i][2] + (f[i + 1][2] - f[i][2]) * t };
}

// --- Helix+ 2 laws (round 190ml; LYNX's own [MODEL])
void ChaosState::start (std::uint32_t s) noexcept
{
    seed = s * 2654435761u + 0x9e3779b9u;
    if (seed == 0u)
        seed = 1u;
    const auto r = [this] {
        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
        return (float) ((double) (seed >> 8) / 8388608.0 - 1.0);
    };
    // a point on each attractor's basin (the voices' own, so two notes do not move together)
    x = 1.0f + 4.0f * r(), y = 1.0f + 4.0f * r(), z = 20.0f + 5.0f * r();
    from = r(), to = r(), phase = 0.0f;
}

float chaosStep (int mode, ChaosState& c, double cycles) noexcept
{
    cycles = std::clamp (cycles, 0.0, 4.0);
    switch (mode)
    {
        case 1: // Lorenz (σ 10, ρ 28, β 8/3): about 0.75 time units per turn of a wing
        case 2: // Rössler (a 0.2, b 0.2, c 5.7): about 6 time units per turn
        {
            const bool lorenz = mode == 1;
            auto t = cycles * (lorenz ? 0.75 : 6.0);
            const auto hmax = lorenz ? 0.004 : 0.03;
            double x = c.x, y = c.y, z = lorenz ? c.z : c.z - 20.0;
            const auto f = [lorenz] (double px, double py, double pz, double& dx, double& dy, double& dz) noexcept {
                if (lorenz)
                    dx = 10.0 * (py - px), dy = px * (28.0 - pz) - py, dz = px * py - 8.0 / 3.0 * pz;
                else
                    dx = -py - pz, dy = px + 0.2 * py, dz = 0.2 + pz * (px - 5.7);
            };
            while (t > 1.0e-12)
            {
                const auto h = std::min (t, hmax);
                double ax, ay, az, bx, by, bz; // (midpoint: second order, steady at these steps)
                f (x, y, z, ax, ay, az);
                f (x + 0.5 * h * ax, y + 0.5 * h * ay, z + 0.5 * h * az, bx, by, bz);
                x += h * bx, y += h * by, z += h * bz;
                t -= h;
            }
            if (! std::isfinite (x) || ! std::isfinite (y) || ! std::isfinite (z) || std::abs (x) > 1.0e3)
                x = 1.0, y = 1.0, z = lorenz ? 20.0 : 0.0;
            c.x = (float) x, c.y = (float) y, c.z = (float) (lorenz ? z : z + 20.0);
            return std::clamp ((float) (x / (lorenz ? 18.0 : 11.0)), -1.0f, 1.0f);
        }
        default: // Smooth Random: a new value every cycle, a raised-cosine glide to it
        {
            c.phase += (float) cycles;
            while (c.phase >= 1.0f)
            {
                c.phase -= 1.0f;
                c.from = c.to;
                c.seed ^= c.seed << 13, c.seed ^= c.seed >> 17, c.seed ^= c.seed << 5;
                c.to = (float) ((double) (c.seed >> 8) / 8388608.0 - 1.0);
            }
            const auto k = 0.5f - 0.5f * std::cos ((float) pi * c.phase);
            return c.from + (c.to - c.from) * k;
        }
    }
}

double shiftHz (double shift) noexcept
{
    // ±1 → ±5 kHz, cubic: fine near 0 (0.1 = 5 Hz, 0.3 = 135 Hz, 0.5 = 625 Hz)
    shift = std::clamp (shift, -1.0, 1.0);
    return 5000.0 * shift * shift * shift;
}

// --- Helix round 3 laws (round 190mt; LYNX's own [MODEL])
double spectralShiftHz (double shift) noexcept
{
    // ±1 → ±2 kHz, cubic like the shifter's: fine near 0 (0.1 = 2 Hz, 0.3 = 54 Hz, 0.5 = 250 Hz)
    shift = std::clamp (shift, -1.0, 1.0);
    return 2000.0 * shift * shift * shift;
}
double spectralExponent (double stretch) noexcept { return 1.0 + 0.5 * std::clamp (stretch, -1.0, 1.0); } // (0.5 … 1.5)

// --- Helix round 4 laws (round 190oc; LYNX's own [MODEL])
double oscUnisonSpread (int u, int n) noexcept { return n > 1 ? 2.0 * (double) std::clamp (u, 0, n - 1) / (double) (n - 1) - 1.0 : 0.0; }
int spectralUnisonStride (int n) noexcept
{
    // n voices share the bank (maxSpectralBank partials a voice: the CPU of two full banks at most); each its own block
    // of a multiple of 4 partials (SIMD), 64 at most
    n = std::clamp (n, 1, maxOscUnison);
    return std::min (maxSpectralPartials, (maxSpectralBank / n) & ~3);
}

int sampleLevelFor (double ratio) noexcept
{
    // level k cuts above 0.5 × 2^(−k/2) of the sample's rate; read at `ratio`, its band reaches 0.5 × ratio × 2^(−k/2) of the
    // output's rate. The level is the first whose images fold back no lower than 0.4 of the output's rate (19.2 kHz at
    // 48 kHz): c × ratio ≤ 0.6, so up to a ratio of 1.2 the sample itself plays [MODEL]
    if (! (ratio > 1.2))
        return 0;
    return std::min (2 * 12, (int) std::ceil (2.0 * std::log2 (ratio / 1.2) - 1.0e-9));
}

namespace
{
struct GrainWindowTable
{
    static constexpr int size = 1024;
    std::array<float, size + 2> v {};
    GrainWindowTable()
    {
        for (int i = 0; i <= size + 1; ++i) // (a Hann window; the guard past 1 is 0)
            v[(std::size_t) i] = i > size ? 0.0f : (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / size));
    }
};
const GrainWindowTable& grainTable() noexcept
{
    static const GrainWindowTable t;
    return t;
}
HELIX_INLINE float grainWindowInline (const float* tab, float x) noexcept
{
    const auto f = x * (float) GrainWindowTable::size;
    const auto i = (int) f;
    return tab[i] + (tab[i + 1] - tab[i]) * (f - (float) i);
}
} // namespace

float grainWindow (float x) noexcept { return grainWindowInline (grainTable().v.data(), std::clamp (x, 0.0f, 1.0f)); }

namespace
{
// (round 190mt) the band-limited levels' read: an 8-point windowed sinc (Kaiser β 5, cut at 0.45 of the level's rate: flat
// to 0.25, images from 0.646 down by ≥ 49 dB), 2048 phases (the nearest taken), each phase summing to 1; SIMD
struct SincTable
{
    static constexpr int phases = 2048, taps = 8;
    alignas (16) std::array<float, (phases + 1) * taps> k {};
    SincTable()
    {
        const auto i0 = [] (double x) {
            double sum = 1.0, term = 1.0;
            for (int j = 1; j < 40; ++j)
            {
                term *= (x / (2.0 * j)) * (x / (2.0 * j));
                sum += term;
            }
            return sum;
        };
        for (int p = 0; p <= phases; ++p)
        {
            const auto frac = (double) p / phases;
            double row[taps], sum = 0.0;
            for (int j = 0; j < taps; ++j)
            {
                const auto x = (double) (j - 3) - frac; // (the tap's distance from the read position)
                const auto sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * 0.9 * x) / (pi * 0.9 * x);
                const auto r = x / 4.0;
                row[j] = sinc * i0 (5.0 * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (5.0);
                sum += row[j];
            }
            for (int j = 0; j < taps; ++j)
                k[(std::size_t) (p * taps + j)] = (float) (row[j] / sum);
        }
    }
};
const SincTable& sincTable() noexcept
{
    static const SincTable t;
    return t;
}
// d: a level's samples (guards around), pos ≥ 0 inside them
HELIX_INLINE float sincRead (const float* d, double pos, const float* table) noexcept
{
    const auto i = (int) pos;
    const auto p = (int) ((pos - (double) i) * SincTable::phases + 0.5);
    const float* kr = table + p * SincTable::taps;
    const float* x = d + i - 3;
    auto a = _mm_add_ps (_mm_mul_ps (_mm_loadu_ps (x), _mm_load_ps (kr)), _mm_mul_ps (_mm_loadu_ps (x + 4), _mm_load_ps (kr + 4)));
    a = _mm_add_ps (a, _mm_movehl_ps (a, a));
    a = _mm_add_ss (a, _mm_shuffle_ps (a, a, 0x55));
    return _mm_cvtss_f32 (a);
}
} // namespace

int targetOfParam (int index) noexcept
{
    if (index < 0)
        return -1;
    if (index >= ext4Base) // Helix round 4 (round 190oc): no matrix rows
        return -1;
    if (index >= ext3Base) // Helix round 3 (round 190mt)
    {
        switch (index - ext3Base)
        {
            case ext3::grainPosition: return tgt::grainPos;
            case ext3::grainSize: return tgt::grainSize;
            case ext3::grainDensity: return tgt::grainDensity;
            case ext3::grainSpray: return tgt::grainSpray;
            case ext3::grainPitch: return tgt::grainPitch;
            case ext3::grainGain: return tgt::grainGain;
            case ext3::specPosition: return tgt::specPos;
            case ext3::specStretch: return tgt::specStretch;
            case ext3::specShift: return tgt::specShift;
            case ext3::specTilt: return tgt::specTilt;
            case ext3::specBand: return tgt::specBand;
            case ext3::specGain: return tgt::specGain;
            default: return -1;
        }
    }
    if (index >= ext2Base) // Helix+ 2 (round 190ml)
    {
        const auto e = index - ext2Base;
        if (e >= ext2::osc3 && e < ext2::osc3 + osc::stride)
            switch (e - ext2::osc3)
            {
                case osc::transpose: case osc::detune: return tgt::osc3 + tgt::oscPitch;
                case osc::position: return tgt::osc3 + tgt::oscPos;
                case osc::effect1: return tgt::osc3 + tgt::oscFx1;
                case osc::effect2: return tgt::osc3 + tgt::oscFx2;
                case osc::pan: return tgt::osc3 + tgt::oscPan;
                case osc::gain: return tgt::osc3 + tgt::oscGain;
                default: return -1;
            }
        switch (e)
        {
            case ext2::osc3WarpAmount: return tgt::osc3Warp;
            case ext2::sampleTranspose: case ext2::sampleDetune: return tgt::samplePitch;
            case ext2::sampleGain: return tgt::sampleGain;
            case ext2::samplePan: return tgt::samplePan;
            case ext2::shift: return tgt::shift;
            case ext2::mbDepth: return tgt::mbDepth;
            default: return -1;
        }
    }
    if (index < subOn)
    {
        const auto k = index / osc::stride, r = index % osc::stride, row = k * tgt::oscStride;
        switch (r)
        {
            case osc::transpose: case osc::detune: return row + tgt::oscPitch;
            case osc::position: return row + tgt::oscPos;
            case osc::effect1: return row + tgt::oscFx1;
            case osc::effect2: return row + tgt::oscFx2;
            case osc::pan: return row + tgt::oscPan;
            case osc::gain: return row + tgt::oscGain;
            default: return -1;
        }
    }
    if (index == subTone) return tgt::subTone;
    if (index == subGain) return tgt::subGain;
    if (index < routing)
    {
        if (index < filterBase (0))
            return -1;
        const auto k = (index - filterBase (0)) / flt::stride, r = (index - filterBase (0)) % flt::stride, row = tgt::filterFreq + k * tgt::filterStride;
        return r == flt::freq ? row : r == flt::res ? row + 1 : r == flt::drive ? row + 2 : r == flt::morph ? row + 3 : -1;
    }
    if (index >= ampEnv && index < envBase (1))
    {
        const auto e = index - ampEnv;
        return e == env::attack ? tgt::ampAttack : e == env::decay ? tgt::ampDecay : e == env::release ? tgt::ampRelease : e == env::sustain ? tgt::ampSustain : -1;
    }
    if (index >= envBase (1) && index < lfoBase (0))
    {
        const auto k = index < envBase (2) ? 1 : 2, e = index - envBase (k), row = k == 1 ? tgt::env2 : tgt::env3;
        switch (e)
        {
            case env::attack: return row;
            case env::decay: return row + 1;
            case env::release: return row + 2;
            case env::initial: return row + 3;
            case env::peak: return row + 4;
            case env::sustain: return row + 5;
            case env::final: return row + 6;
            default: return -1;
        }
    }
    if (index >= lfoBase (0) && index < timeScale)
    {
        const auto k = (index - lfoBase (0)) / lfo::stride, e = (index - lfoBase (0)) % lfo::stride, row = k == 0 ? tgt::lfo1 : tgt::lfo2;
        return e == lfo::amount ? row : e == lfo::shaping ? row + 1 : e == lfo::rate || e == lfo::syncedRate ? row + 2 : -1;
    }
    switch (index)
    {
        case timeScale: return tgt::time;
        case modAmount: return tgt::globalMod;
        case unisonAmount: return tgt::unison;
        case volume: return tgt::amp;
        case transpose: return tgt::pitch;
        case glide: return tgt::glide;
        case extBase + ext::warpAmount: return tgt::warpAmount;
        case extBase + ext::warpAmount + ext::warpStride: return tgt::warpAmount + 1;
        case extBase + ext::noiseLevel: return tgt::noiseLevel;
        case extBase + ext::unisonBlend: return tgt::unisonBlend;
        default: return -1;
    }
}

const std::array<double, stateSize>& defaults() noexcept
{
    // Live's default Wavetable [DATA: Core Library Defaults/Instruments/Wavetable.adv]
    static const auto d = [] {
        std::array<double, stateSize> s {};
        for (int k = 0; k < 2; ++k)
        {
            const auto b = oscBase (k);
            s[(std::size_t) (b + osc::on)] = k == 0 ? 1.0 : 0.0;
            s[(std::size_t) (b + osc::effectMode)] = k == 0 ? effectModern : effectNone;
            s[(std::size_t) (b + osc::gain)] = 1.0;
        }
        s[(std::size_t) subGain] = 0.5011875033;
        s[(std::size_t) subTranspose] = 1.0;
        for (int k = 0; k < 2; ++k)
        {
            const auto b = filterBase (k);
            s[(std::size_t) (b + flt::on)] = k == 0 ? 1.0 : 0.0;
            s[(std::size_t) (b + flt::type)] = k == 0 ? 0.0 : 1.0;
            s[(std::size_t) (b + flt::freq)] = k == 0 ? 20479.998 : 20.000309;
        }
        for (int k = 0; k < 3; ++k)
        {
            const auto b = envBase (k);
            s[(std::size_t) (b + env::attack)] = 0.001;
            s[(std::size_t) (b + env::decay)] = 0.6;
            s[(std::size_t) (b + env::release)] = 0.6;
            s[(std::size_t) (b + env::decaySlope)] = 0.5;
            s[(std::size_t) (b + env::releaseSlope)] = 0.5;
            s[(std::size_t) (b + env::sustain)] = k == 0 ? 0.5011875629 : 0.5;
            if (k > 0)
                s[(std::size_t) (b + env::peak)] = 1.0;
        }
        for (int k = 0; k < 2; ++k)
        {
            const auto b = lfoBase (k);
            s[(std::size_t) (b + lfo::retrigger)] = 1.0;
            s[(std::size_t) (b + lfo::amount)] = 1.0;
            s[(std::size_t) (b + lfo::rate)] = 1.0;
            s[(std::size_t) (b + lfo::syncedRate)] = 15.0;
        }
        s[(std::size_t) modAmount] = 1.0;
        s[(std::size_t) unisonVoices] = 3.0;
        s[(std::size_t) unisonAmount] = 0.3;
        s[(std::size_t) volume] = 0.3548134267;
        s[(std::size_t) monoPoly] = 1.0;
        s[(std::size_t) polyVoices] = 6.0;
        s[(std::size_t) tableBase] = 0.0;
        s[(std::size_t) tableBase + 1] = 0.0;
        s[(std::size_t) matrixIndex (tgt::oscPos, src::modWheel)] = 1.0;
        s[(std::size_t) matrixIndex (tgt::oscPos, src::slide)] = 0.33;
        s[(std::size_t) matrixIndex (tgt::oscFx1, src::pressure)] = 0.07;
        s[(std::size_t) matrixIndex (tgt::amp, src::velocity)] = 0.5;
        s[(std::size_t) matrixIndex (tgt::pitch, src::pitchBend)] = 1.0 / 24.0;
        s[(std::size_t) matrixIndex (tgt::pitch, src::notePitchBend)] = 1.0;
        // Helix+ (round 190mi): revision 0 here (a channel saved before 190mi reads its missing block as these); new
        // channels take helixDefaults() (revision 1). Everything off; the effects' knobs at usable settings.
        const auto e = [&s] (int i) -> double& { return s[(std::size_t) (extBase + i)]; };
        e (ext::noiseLevel) = 0.5, e (ext::noiseColour) = 0.5;
        e (ext::unisonBlend) = 1.0, e (ext::unisonWidth) = 1.0;
        e (ext::distDrive) = 0.3, e (ext::distMix) = 1.0;
        e (ext::chorusRate) = 0.8, e (ext::chorusDepth) = 0.5, e (ext::chorusMix) = 0.5;
        e (ext::delayTime) = 5.0, e (ext::delayFeedback) = 0.35, e (ext::delayPingPong) = 1.0, e (ext::delayMix) = 0.25;
        e (ext::reverbSize) = 0.5, e (ext::reverbDamp) = 0.5, e (ext::reverbMix) = 0.25;
        // Helix+ 2 (round 190ml): oscillator 3 and the sample off (at full gain when switched on), the chaos sources at 1 Hz,
        // the new effects at usable settings
        const auto e2 = [&s] (int i) -> double& { return s[(std::size_t) (ext2Base + i)]; };
        e2 (ext2::osc3 + osc::gain) = 1.0;
        e2 (ext2::sampleGain) = 1.0, e2 (ext2::sampleRoot) = 60.0;
        for (int k = 0; k < chaosCount; ++k)
            e2 (ext2::chaosRate + k * ext2::chaosStride) = 1.0, e2 (ext2::chaosAmount + k * ext2::chaosStride) = 1.0;
        e2 (ext2::shift) = 0.2, e2 (ext2::shiftMix) = 1.0;
        e2 (ext2::eqMidFreq) = 1000.0;
        e2 (ext2::mbDepth) = 0.5, e2 (ext2::mbTime) = 0.3, e2 (ext2::mbUp) = 0.6, e2 (ext2::mbDown) = 0.6;
        // Helix round 3 (round 190mt): the granular and spectral oscillators off (at usable settings when switched on), Clean
        // off (new channels: helixDefaults() switches it on)
        const auto e3 = [&s] (int i) -> double& { return s[(std::size_t) (ext3Base + i)]; };
        e3 (ext3::grainSize) = 0.08, e3 (ext3::grainDensity) = 20.0, e3 (ext3::grainSpray) = 0.1, e3 (ext3::grainSpread) = 0.5;
        e3 (ext3::grainGain) = 1.0;
        e3 (ext3::specBand) = 1000.0, e3 (ext3::specWidth) = 10.0, e3 (ext3::specPartials) = 32.0, e3 (ext3::specGain) = 1.0;
        // Helix round 4 (round 190oc): one voice each (Detune and Width at usable settings for when voices are added), no
        // reverse grains
        const auto e4 = [&s] (int i) -> double& { return s[(std::size_t) (ext4Base + i)]; };
        e4 (ext4::grainUnison) = 1.0, e4 (ext4::grainDetune) = 0.2, e4 (ext4::grainWidth) = 0.6;
        e4 (ext4::specUnison) = 1.0, e4 (ext4::specDetune) = 0.2, e4 (ext4::specWidth) = 0.6;
        for (int k = 0; k < 2; ++k) // a drawn LFO starts as a triangle: 0 → +1 → −1 → 0
        {
            const auto b = ext::points + k * ext::lfoPointsSize;
            e (b) = 3.0;
            const double pts[3][3] { { 0.0, 0.0, 0.0 }, { 0.25, 1.0, 0.0 }, { 0.75, -1.0, 0.0 } };
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    e (b + 1 + i * ext::pointStride + j) = pts[i][j];
        }
        return s;
    }();
    return d;
}

// =====================================================================================================================
namespace
{
struct EnvParams
{
    float attack = 0.001f, decay = 0.6f, release = 0.6f, aSlope = 0.0f, dSlope = 0.5f, rSlope = 0.5f;
    float initial = 0.0f, peak = 1.0f, sustain = 0.5f, final = 0.0f;
    int loop = 0; // 0 none, 1 trigger, 2 loop
};

enum Stage : std::uint8_t
{
    attackStage,
    decayStage,
    sustainStage,
    releaseStage,
    offStage
};

struct Env
{
    Stage stage = offStage;
    float pos = 0.0f;   // 0…1 through the stage
    float from = 0.0f;  // the level the stage started at
    float value = 0.0f;
    bool looping = false; // Loop mode: this release goes back to the attack while the key is held
    void start (const EnvParams& e, bool fromNow) noexcept
    {
        from = fromNow ? value : e.initial;
        if (! fromNow)
            value = e.initial;
        stage = attackStage;
        pos = 0.0f;
        looping = false;
    }
    void release() noexcept
    {
        if (stage == releaseStage)
            looping = false;
        if (stage == offStage || stage == releaseStage)
            return;
        from = value;
        stage = releaseStage;
        pos = 0.0f;
    }
};

// one step of `dt` seconds; `gate`: the key is held; `scale` multiplies every time
void stepEnv (Env& v, const EnvParams& e, float dt, float scale, bool gate, const float* times) noexcept
{
    auto advance = [&] (float t) {
        t = std::max (t * scale, 1.0e-4f);
        v.pos += dt / t;
    };
    for (int guard = 0; guard < 4; ++guard)
    {
        switch (v.stage)
        {
            case attackStage:
                advance (times[0]);
                if (v.pos >= 1.0f)
                {
                    v.value = e.peak, v.from = e.peak, v.stage = decayStage, v.pos = 0.0f;
                    continue;
                }
                v.value = v.from + (e.peak - v.from) * segmentInline (v.pos, e.aSlope);
                return;
            case decayStage:
                advance (times[1]);
                if (v.pos >= 1.0f)
                {
                    v.value = e.sustain;
                    v.pos = 0.0f;
                    // Trigger: the sustain is skipped, the release follows at once (the key's release is ignored); Loop: the
                    // same while the key is held, then the attack again from the final value [MEASURED e3, e4]
                    if (e.loop == 1 || (e.loop == 2 && gate))
                    {
                        v.from = v.value, v.stage = releaseStage, v.looping = e.loop == 2;
                        return;
                    }
                    v.stage = sustainStage;
                    return;
                }
                v.value = v.from + (e.sustain - v.from) * segmentInline (v.pos, e.dSlope);
                return;
            case sustainStage:
                v.value = e.sustain;
                return;
            case releaseStage:
                advance (times[2]);
                if (v.pos >= 1.0f)
                {
                    v.value = e.final;
                    if (v.looping && gate)
                    {
                        v.from = e.final, v.stage = attackStage, v.pos = 0.0f;
                        return;
                    }
                    v.stage = offStage;
                    return;
                }
                v.value = v.from + (e.final - v.from) * segmentInline (v.pos, e.rSlope);
                return;
            case offStage: return;
        }
    }
}

// a TPT state-variable filter stage (two make 24 dB/oct); Live's filter family, laws as Tetra's (round 190hs) [MEASURED
// 190hs f0…f5: Operator's filter is the same circuit set; 190ja f* re-checked on Wavetable]
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    HELIX_INLINE void process (float x, float k, float a1, float a2, float a3, float& lp, float& bp, float& hp) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

struct FilterCoefs
{
    bool on = false, steep = false;
    int type = 0, circuit = 0;
    float k = 2.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, morph = 0.0f, drive = 1.0f;
    // Helix+ (round 190mi): the filter's own type (xfilter::live = Live's type above) and its coefficients
    int xtype = 0;
    float combDelay = 100.0f, combGain = 0.0f;              // comb: the delay (samples) and the feedback
    std::array<float, 3> fk {}, fa1 {}, fa2 {}, fa3 {}, fgain {}; // formant: three band passes
    std::array<float, 4> ap {};                             // phaser: the four all-pass coefficients
    float apFeedback = 0.0f;
    // the inputs the coefficients above were made from (190mi: a step with the same inputs keeps them: the same values,
    // without the pow / tan of every step)
    double keyFreq = -1.0, keyRate = 0.0, hz = 20.0;
    float keyMod = 0.0f, keyRes = -1.0f, keyDrive = -1.0f;
    int keyCircuit = -1;
    bool keySteep = false;
};

struct FilterState
{
    std::array<Svf, 2> l {}, r {};
    // Helix+: the formant's band passes, the phaser's all-passes and feedback, the comb's write position
    std::array<Svf, 3> xl {}, xr {};
    std::array<float, 4> apl {}, apr {};
    float fbl = 0.0f, fbr = 0.0f;
    int combPos = 0;
};

HELIX_INLINE float filterStage (Svf& s, float x, const FilterCoefs& c) noexcept
{
    float lp, bp, hp;
    s.process (x, c.k, c.a1, c.a2, c.a3, lp, bp, hp);
    switch (c.type)
    {
        case 0: return lp;
        case 1: return hp;
        case 2: return bp;
        case 3: return lp + hp;
        default:
        {
            const auto m = c.morph * 4.0f; // low → band → high → notch → low
            const float outs[5] { lp, bp, hp, lp + hp, lp };
            const auto i = std::min ((int) m, 3);
            const auto f = m - (float) i;
            return outs[i] + (outs[i + 1] - outs[i]) * f;
        }
    }
}

HELIX_INLINE float filterSample (std::array<Svf, 2>& s, float x, const FilterCoefs& c) noexcept
{
    if (c.circuit > 0)
        x = 2.0f * std::tanh (x * c.drive * 0.5f); // (the driven circuits saturate softly; Clean ignores Drive [190hs c0]; the ceiling
                                                   // fitted on the 135 presets of 190ja W3: ±3 left LYNX +1.3 dB louder, ±1 −1.5 dB)
    auto y = filterStage (s[0], x, c);
    if (c.steep)
        y = filterStage (s[1], y, c);
    return y;
}

constexpr int combLength = 4096; // a comb's delay line (samples; its lowest note at 48 kHz: 11.7 Hz)

// Helix+'s filter types (round 190mi) [MODEL]; `comb` is this voice's line for this filter and side
inline float filterExt (FilterState& f, bool right, float x, const FilterCoefs& c, float* comb) noexcept
{
    if (c.drive > 1.001f)
        x = 2.0f * std::tanh (x * c.drive * 0.5f);
    switch (c.xtype)
    {
        case xfilter::combPlus:
        case xfilter::combMinus:
        {
            // a feedback comb: y = x + g · y[n − D] (D fractional, linear), normalised so the peaks sit near 0 dB
            const auto read = (float) f.combPos - c.combDelay;
            const auto i0 = (int) std::floor (read);
            const auto fr = read - (float) i0;
            const auto a = comb[i0 & (combLength - 1)], b = comb[(i0 + 1) & (combLength - 1)];
            const auto y = x + c.combGain * (a + (b - a) * fr);
            comb[f.combPos & (combLength - 1)] = y;
            return y * (1.0f - std::abs (c.combGain));
        }
        case xfilter::formant:
        {
            auto& bands = right ? f.xr : f.xl;
            auto y = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                float lp, bp, hp;
                bands[(std::size_t) b].process (x, c.fk[(std::size_t) b], c.fa1[(std::size_t) b], c.fa2[(std::size_t) b], c.fa3[(std::size_t) b], lp, bp, hp);
                y += bp * c.fgain[(std::size_t) b];
            }
            return y;
        }
        case xfilter::phaser:
        {
            auto& z = right ? f.apr : f.apl;
            auto& fb = right ? f.fbr : f.fbl;
            auto u = x + c.apFeedback * fb;
            for (int i = 0; i < 4; ++i)
            {
                const auto y = c.ap[(std::size_t) i] * u + z[(std::size_t) i];
                z[(std::size_t) i] = u - c.ap[(std::size_t) i] * y;
                u = y;
            }
            fb = u;
            return 0.5f * (x + u);
        }
        default: return x;
    }
}

struct UnisonVoice
{
    std::uint32_t phase = 0, fmPhase = 0;
    double syncPhase = 0.0;  // the synced wave's phase (turns)
    float held = 0.0f;       // sync: the sample held one step for the polyBLEP correction
    float drift = 0.0f, driftTarget = 0.0f; // Shimmer / Noise: the voice's moving detune (cents)
    float randomCents = 0.0f;                // Random Note: the note's own detune
    // Helix+ 2 (round 190ml): the anti-aliased warps' memory: the last warp input phase, the correction held for the next
    // sample (polyBLEP / polyBLAMP), Crush's last input (its antiderivative's difference)
    float px = -1.0f, wheld = 0.0f, crushPrev = 0.0f, crushG = 0.0f;
};

struct OscState
{
    std::array<UnisonVoice, maxUnison> u {};
    float pos = 0.0f, posStep = 0.0f;   // position, ramped over the control step
    float gainL = 0.0f, gainR = 0.0f;   // (per unison voice: below)
};

struct Voice
{
    bool active = false, gate = false;
    int key = 60;
    float velocity = 1.0f, pitch = 0.0f;       // the note's own offset (semitones, MPE per-note bend included)
    float slide = 0.0f, pressure = 0.0f;       // MPE
    float random = 0.0f;                       // the Random source (−1…1, drawn at the note's start)
    std::uint64_t age = 0;
    std::array<Env, 3> env {};
    std::array<double, 2> lfoPhase {};
    std::array<float, 2> lfoHeld {}, lfoAge {};
    float glideFrom = 0.0f, glidePos = 1.0f;   // semitones below / above the note at the start, 0…1 through the glide
    HostSlide host;                            // FL's channel porta and slide notes (rollup 0.1.30, core/HostSlide.h): cents
    std::array<OscState, oscCount> osc {};
    std::uint32_t subPhase = 0;
    FilterState f1, f2;
    std::array<FilterCoefs, 2> fc {};
    int counter = 0;
    std::uint32_t noise = 1;
    // the step's values
    float amp = 0.0f, ampStep = 0.0f;
    std::array<std::array<float, maxUnison>, oscCount> uGainL {}, uGainR {};
    std::array<std::array<std::uint32_t, maxUnison>, oscCount> uInc {};
    std::array<std::array<float, maxUnison>, oscCount> uPosOffset {};
    std::array<int, oscCount> mip {};
    std::array<float, oscCount> fx1 {}, fx2 {}, gain {};
    std::array<int, oscCount> mode {};
    float subGain = 0.0f, subTone = 0.0f;
    std::uint32_t subInc = 0;
    // Helix+ (round 190mi): the step's second warps, unison count, noise, the other oscillator's phases at the step's start
    bool plus = false;
    int uCount = 1;
    std::array<int, oscCount> warpMode {};
    std::array<float, oscCount> warpAmount {}, otherRatio {};
    std::array<std::array<std::uint32_t, maxUnison>, oscCount> phaseAtStep {};
    float noiseGain = 0.0f, noiseCoef = 1.0f, noiseLp = 0.0f;
    bool noiseHigh = false;
    // (190mi) the inputs of the last step's increments: equal inputs keep them (same values, no pow)
    struct IncKey
    {
        double hz = -1.0, rate = 0.0;
        float cents = 0.0f;
    };
    std::array<std::array<IncKey, maxUnison>, oscCount> incKey {};
    std::array<double, oscCount> keySemis { -1.0e9, -1.0e9, -1.0e9 }, keyOscHz {};
    double keySubSemis = -1.0e9, keySubRate = 0.0;
    // Helix+ 2 (round 190ml): the step's oscillator count (3 with oscillator 3 on), the anti-aliased warps, the chaos
    // sources, the sample oscillator (each unison voice's read position and speed, its gains, its sync phase)
    bool plus2 = false;
    int oscs = 2;
    std::array<ChaosState, chaosCount> chaos {};
    std::array<float, chaosCount> chaosValue {};
    bool sampleOn = false, sampleDone = false;
    std::array<double, maxUnison> sPos {}, sInc {};
    std::array<float, maxUnison> sGainL {}, sGainR {}, sHeld {}, sCrush {};
    std::array<std::uint32_t, maxUnison> sSync {}, sSyncInc {};
    int sWarp = 0;
    float sWarpAmount = 0.0f;
    // Helix round 3 (round 190mt): the step's switches (round 3 live, Clean, the granular and spectral oscillators), the
    // grains (each from its own read position of the sample or of a band-limited level), the partials (each a rotating
    // (cos, sin) pair with its amplitude, ramped over the step; SIMD four at a time)
    bool plus3 = false, sClean = false, grainOn = false, specOn = false;
    struct Grain
    {
        double pos = 0.0, inc = 0.0; // (the level's samples)
        float w = 0.0f, wInc = 0.0f, gl = 0.0f, gr = 0.0f;
        int level = 0, start = 0;    // (start: the step's sample it begins at)
    };
    std::array<Grain, maxGrainsUnison> grains {}; // (190oc: room for unison grains; one voice keeps 190mt's maxGrains)
    int grainCount = 0;
    float grainNext = 0.0f;          // samples to the next grain
    std::uint32_t grainSeed = 1u;
    double gInc = 0.0;
    float gPos = 0.0f, gSpray = 0.0f, gSpread = 0.0f, gPan = 0.0f, gGain = 0.0f, gInterval = 1.0f, gWinInc = 1.0f;
    int gUnison = 1;                 // (190oc: grain unison voices, their detune (semitones at the outer voice) and width;
    float gDetune = 0.0f, gWidth = 0.0f, gReverse = 0.0f; // the share of grains played backwards)
    int spCount = 0, spLive = 0;     // the partials rendered this step / wanted after it (multiples of 4)
    bool spStarted = false;
    alignas (16) std::array<float, maxSpectralBank> spC {};
    alignas (16) std::array<float, maxSpectralBank> spS {};
    alignas (16) std::array<float, maxSpectralBank> spA {};
    alignas (16) std::array<float, maxSpectralBank> spDA {};
    alignas (16) std::array<float, maxSpectralBank> spCr {};
    alignas (16) std::array<float, maxSpectralBank> spSr {};
    float spGainL = 0.0f, spGainR = 0.0f;
    std::array<double, 9> spKey {}; // (the inputs of the last step's targets: equal inputs keep them)
    // (190oc) spectral unison: the voices, each a block of spStride partials from u · spStride, its own gains
    int spUnison = 1, spStride = maxSpectralPartials;
    std::array<float, maxOscUnison> spUL {}, spUR {};
    std::array<double, 3> spKey4 {};
};

constexpr int maxVoices = maxPolyVoices;
// the oscillator another one's FM / AM / RM reads: 1 ↔ 2 (as 190mi), 3 ← 1
constexpr int otherOsc (int k) noexcept { return k == 0 ? 1 : 0; }
constexpr bool hardEdged (int w) noexcept { return w == warp::sync || w == warp::pwm || w == warp::mirror || w == warp::flip || w == warp::crush; }
} // namespace

struct Engine::Impl
{
    double rate = 48000.0;
    std::array<double, stateSize> p {};
    std::array<const Table*, oscCount> tables { nullptr, nullptr, nullptr };
    std::array<const Table*, oscCount> factory { nullptr, nullptr, nullptr }; // (the factory tables the values name)
    std::array<const Table*, oscCount> user { nullptr, nullptr, nullptr };    // Helix+ 2: the channel's user tables
    const SampleBuffer* sample = nullptr;                                       // Helix+ 2: the sample oscillator's sample
    const SampleLevels* levels = nullptr;                                       // round 3: the sample band-limited
    const SpectralData* spectral = nullptr;                                     // round 3: the sample's partials
    std::array<bool, chaosCount> chaosUsed {};
    bool newMacrosUsed = false; // (a cell reads Macro 5…8)
    std::array<Voice, maxVoices> voices {};
    std::array<EnvParams, 3> envP {};
    std::uint64_t clock = 0;
    float pitchOffset = 0.0f, bend = 0.0f, bendSource = 0.0f, modWheel = 0.0f, chanPressure = 0.0f;
    float lastNote = -1.0f;
    std::uint32_t rng = 0x2545f491u;
    std::array<double, 2> lfoFree {};
    // the matrix's cells that are not 0 (rebuilt with the values)
    struct Cell
    {
        std::uint8_t target, source;
        // (190ml: what applyCells asks of every cell, worked out once here)
        std::uint8_t flags;
        float amount;
    };
    enum CellFlag : std::uint8_t
    {
        lfoRowCell = 1, wheelCell = 2, internalCell = 4, levelCell = 8, bipolarCell = 16, keyTrackCell = 32
    };
    static Cell makeCell (int t, int s, float a) noexcept
    {
        std::uint8_t f = 0;
        if (t >= tgt::lfo1 && t < tgt::lfo2 + 3)
            f |= lfoRowCell;
        if (t == tgt::pitch && s == src::pitchBend) // (Pitch ← Pitch Bend is the channel's wheel in LYNX [MODEL])
            f |= wheelCell;
        if (s <= src::lfo2 || s >= src::chaos1)
            f |= internalCell;
        if (t == tgt::amp || t == tgt::subGain || t == tgt::oscGain || t == tgt::oscGain + tgt::oscStride || t == tgt::osc3 + tgt::oscGain || t == tgt::sampleGain
            || t == tgt::grainGain || t == tgt::specGain)
            f |= levelCell;
        if (s == src::lfo1 || s == src::lfo2 || s == src::velocity || s == src::note || s == src::pitchBend || s == src::random || s == src::notePitchBend
            || s >= src::chaos1)
            f |= bipolarCell;
        if (s == src::note && (t == tgt::filterFreq || t == tgt::filterFreq + tgt::filterStride))
            f |= keyTrackCell;
        return { (std::uint8_t) t, (std::uint8_t) s, f, a };
    }
    std::array<Cell, allTargetCount * allSourceCount> cells {};
    int cellCount = 0;
    float bufL[controlRate] {}, bufR[controlRate] {};

    // --- Helix+ (round 190mi)
    struct FreeDeleter
    {
        void operator() (float* f) const noexcept { std::free (f); }
    };
    using Buffer = std::unique_ptr<float, FreeDeleter>;
    // calloc'd: the pages of a line nobody uses are never touched (no RAM until a comb / effect plays)
    static Buffer zeroed (std::size_t n) { return Buffer ((float*) std::calloc (std::max<std::size_t> (n, 1), sizeof (float))); }
    Buffer comb = zeroed ((std::size_t) maxVoices * 2 * 2 * combLength); // [voice][filter][side][combLength]
    float* combLine (const Voice& v, int filter) noexcept { return comb.get() + ((std::size_t) (&v - voices.data()) * 4 + (std::size_t) filter * 2) * combLength; }
    struct Fx
    {
        double preparedRate = 0.0;
        // chorus
        Buffer chorus;
        int chorusLen = 0, chorusPos = 0;
        double chorusPhase = 0.0;
        // delay
        Buffer delay;
        int delayLen = 0, delayPos = 0;
        float delayLpL = 0.0f, delayLpR = 0.0f;
        // reverb: 8 lines
        Buffer reverb;
        std::array<int, 8> rvLen {}, rvPos {}, rvOffset {};
        std::array<float, 8> rvLp {};
        int rvMax = 0;
        // distortion's crush
        float holdL = 0.0f, holdR = 0.0f, holdCount = 0.0f;
        std::array<bool, 4> was {};
        bool tail = false;
        double quiet = 0.0;
    } fx;
    static constexpr int fxBlock = 256;
    float fxL[fxBlock] {}, fxR[fxBlock] {};

    bool plus() const noexcept { return p[(std::size_t) extBase] >= 0.5; }
    bool plus2() const noexcept { return p[(std::size_t) extBase] >= 1.5; } // (Helix+ 2, round 190ml)
    double xp2 (int i) const noexcept { return p[(std::size_t) (ext2Base + i)]; }
    bool osc3On() const noexcept { return plus2() && xp2 (ext2::osc3 + osc::on) > 0.5; }
    bool sampleLive() const noexcept { return plus2() && xp2 (ext2::sampleOn) > 0.5 && sample != nullptr && sample->frames() > 1; }
    double xp (int i) const noexcept { return p[(std::size_t) (extBase + i)]; }
    // (Helix round 3, round 190mt)
    bool plus3() const noexcept { return p[(std::size_t) extBase] >= 2.5; }
    double xp3 (int i) const noexcept { return p[(std::size_t) (ext3Base + i)]; }
    bool grainLive() const noexcept { return xp3 (ext3::grainOn) > 0.5 && sample != nullptr && sample->frames() > 8; }
    bool specLive() const noexcept { return xp3 (ext3::specOn) > 0.5 && spectral != nullptr && ! spectral->empty(); }
    // (Helix round 4, round 190oc)
    bool plus4() const noexcept { return p[(std::size_t) extBase] >= 3.5; }
    double xp4 (int i) const noexcept { return p[(std::size_t) (ext4Base + i)]; }
    int unisonOf (int i) const noexcept { return plus4() ? std::clamp ((int) roundHalfAway (xp4 (i)), 1, maxOscUnison) : 1; }
    // (worked out when the values change, so a voice step of a channel that plays none of round 3 asks nothing more)
    bool r3Live = false;  // round 3 live and something of it on (Clean on a playing sample, the grains, the partials)
    bool r3Fill = false;  // its matrix rows are read or written (r3Live, or a cell on them)
    bool r3Cells = false; // a cell on round 3's rows
    void refreshRound3() noexcept
    {
        const auto p3 = plus3();
        r3Live = p3 && ((xp3 (ext3::sampleClean) > 0.5 && xp2 (ext2::sampleOn) > 0.5) || xp3 (ext3::grainOn) > 0.5 || xp3 (ext3::specOn) > 0.5);
        r3Fill = r3Live || (p3 && r3Cells);
        // (Helix round 4, 190oc: the unison voices and the reverse share, worked out here so a step asks nothing more)
        grainU = unisonOf (ext4::grainUnison);
        specU = unisonOf (ext4::specUnison);
        grainRev = plus4() ? clamp01 ((float) xp4 (ext4::grainReverse)) : 0.0f;
    }
    int grainU = 1, specU = 1;
    float grainRev = 0.0f;
    bool fxOn() const noexcept
    {
        return plus() && (xp (ext::distOn) > 0.5 || xp (ext::chorusOn) > 0.5 || xp (ext::delayOn) > 0.5 || xp (ext::reverbOn) > 0.5 || fx2On());
    }
    bool fx2On() const noexcept { return plus2() && (xp2 (ext2::shiftOn) > 0.5 || xp2 (ext2::eqOn) > 0.5 || xp2 (ext2::mbOn) > 0.5); }

    void prepareFx()
    {
        if (fx.preparedRate == rate)
            return;
        fx.preparedRate = rate;
        fx.chorusLen = 1;
        while (fx.chorusLen < (int) (0.04 * rate))
            fx.chorusLen *= 2;
        fx.chorus = zeroed ((std::size_t) fx.chorusLen * 2);
        fx.delayLen = (int) (2.05 * rate) + 4;
        fx.delay = zeroed ((std::size_t) fx.delayLen * 2);
        static constexpr int primes[8] { 1031, 1327, 1523, 1787, 1999, 2239, 2477, 2741 };
        int total = 0;
        for (int i = 0; i < 8; ++i)
        {
            fx.rvOffset[(std::size_t) i] = total;
            total += (int) (primes[i] * 1.6 * rate / 48000.0) + 4;
        }
        fx.rvMax = total;
        fx.reverb = zeroed ((std::size_t) total);
        fx.chorusPos = fx.delayPos = 0;
        fx.rvPos = {};
    }

    Impl()
    {
        (void) sincTable(), (void) grainTable(); // (190mt: built here, never on the audio thread)
        const auto& d = defaults();
        std::copy (d.begin(), d.end(), p.begin());
        for (int m = 0; m < mipCount; ++m)
            partialsAt[(std::size_t) m] = mipPartials (m);
        refresh();
        prepareFx();
    }

    HELIX_INLINE int ip (int i) const noexcept { return (int) roundHalfAway (p[(std::size_t) i]); }
    std::array<int, mipCount> partialsAt {}; // (mipPartials, read once)
    float fp (int i) const noexcept { return (float) p[(std::size_t) i]; }

    void refresh() noexcept
    {
        for (int k = 0; k < 3; ++k)
        {
            const auto b = envBase (k);
            auto& e = envP[(std::size_t) k];
            e.attack = fp (b + env::attack), e.decay = fp (b + env::decay), e.release = fp (b + env::release);
            e.aSlope = fp (b + env::attackSlope), e.dSlope = fp (b + env::decaySlope), e.rSlope = fp (b + env::releaseSlope);
            e.sustain = fp (b + env::sustain);
            e.loop = std::clamp (ip (b + env::loopMode), 0, 2);
            if (k == 0)
                e.initial = 0.0f, e.peak = 1.0f, e.final = 0.0f;
            else
                e.initial = fp (b + env::initial), e.peak = fp (b + env::peak), e.final = fp (b + env::final);
        }
        cellCount = 0;
        for (int t = 0; t < targetCount; ++t)
            for (int s = 0; s < sourceCount; ++s)
                if (const auto a = (float) p[(std::size_t) matrixIndex (t, s)]; a != 0.0f && std::isfinite (a))
                    cells[(std::size_t) cellCount++] = makeCell (t, s, a);
        if (plus()) // Helix+: the macros' columns, then Helix+'s rows (after Live's cells: revision 0 sums exactly as before)
            for (int t = 0; t < plusTargetCount; ++t)
                for (int s = t < targetCount ? sourceCount : 0; s < plusSourceCount; ++s)
                    if (const auto a = (float) p[(std::size_t) cellIndex (t, s)]; a != 0.0f && std::isfinite (a))
                        cells[(std::size_t) cellCount++] = makeCell (t, s, a);
        chaosUsed = {};
        newMacrosUsed = false;
        if (plus2()) // Helix+ 2: the new sources on the older rows, then the new rows (after: revision 1 sums as before)
            for (int t = 0; t < rev2TargetCount; ++t)
                for (int s = t < plusTargetCount ? plusSourceCount : 0; s < allSourceCount; ++s)
                    if (const auto a = (float) p[(std::size_t) cellIndex (t, s)]; a != 0.0f && std::isfinite (a))
                    {
                        cells[(std::size_t) cellCount++] = makeCell (t, s, a);
                        if (s >= src::chaos1)
                            chaosUsed[(std::size_t) (s - src::chaos1)] = true;
                        else if (s >= plusSourceCount)
                            newMacrosUsed = true;
                    }
        r3Cells = false;
        if (plus3()) // Helix round 3 (190mt): its rows, after (revision 2 sums as before)
            for (int t = rev2TargetCount; t < allTargetCount; ++t)
                for (int s = 0; s < allSourceCount; ++s)
                    if (const auto a = (float) p[(std::size_t) cellIndex (t, s)]; a != 0.0f && std::isfinite (a))
                    {
                        r3Cells = true;
                        cells[(std::size_t) cellCount++] = makeCell (t, s, a);
                        if (s >= src::chaos1)
                            chaosUsed[(std::size_t) (s - src::chaos1)] = true;
                        else if (s >= plusSourceCount)
                            newMacrosUsed = true;
                    }
        refreshRound3();
    }

    void takeTables() noexcept
    {
        for (int k = 0; k < oscCount; ++k)
        {
            if (const auto* t = tableIfBuilt ((int) std::lround (p[(std::size_t) oscTableIndex (k)])))
                factory[(std::size_t) k] = t;
            // Helix+ 2: the oscillator's user table, when it plays one and the channel has it
            const bool own = plus2() && xp2 (ext2::userTable + k) > 0.5 && user[(std::size_t) k] != nullptr;
            tables[(std::size_t) k] = own ? user[(std::size_t) k] : factory[(std::size_t) k];
        }
    }

    std::uint32_t nextRandom() noexcept
    {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        return rng;
    }
    float bipolarRandom() noexcept { return (float) ((double) (nextRandom() >> 8) / 8388608.0 - 1.0); }

    void startVoice (Voice& v, int key, float velocity, float pitch, float slide, float pressure, bool legato) noexcept
    {
        const auto glideTime = p[(std::size_t) glide];
        const auto target = (float) key + pitch;
        // Glide: Mono only, a straight slide in semitones over the glide time [MEASURED g1, g2: no glide in Poly]
        if (glideTime > 0.0005 && lastNote >= 0.0f && ip (monoPoly) == 0)
        {
            v.glideFrom = lastNote - target + (legato ? v.glideFrom * (1.0f - v.glidePos) : 0.0f);
            v.glidePos = 0.0f;
        }
        else
            v.glideFrom = 0.0f, v.glidePos = 1.0f;
        lastNote = target;
        v.key = key;
        v.pitch = pitch;
        v.velocity = velocity;
        v.slide = slide < 0.0f ? 0.0f : slide;
        v.pressure = pressure;
        v.gate = true;
        v.age = ++clock;
        // FL's channel porta (rollup 0.1.30, as round 190ix's Tetra): the rack starts the note `fromCents` from its own
        // pitch (the note's cents hold it) and the voice glides back to its own pitch (HostSlide::pendingGlide); a slide
        // note moves it later (slideTo). The channel's live pitch moves every voice through pitchOffset, so the slide does
        // not track it (NaN reference).
        v.host = {};
        v.host.anchor (NAN);
        if (legato && v.active)
            return; // (mono legato: the envelopes go on)
        v.active = true;
        v.random = bipolarRandom();
        v.noise = nextRandom() | 1u;
        for (int k = 0; k < 3; ++k)
            v.env[(std::size_t) k].start (envP[(std::size_t) k], false);
        for (int k = 0; k < 2; ++k)
        {
            const auto b = lfoBase (k);
            v.lfoPhase[(std::size_t) k] = p[(std::size_t) (b + lfo::retrigger)] > 0.5 ? p[(std::size_t) (b + lfo::phase)] / 360.0 : lfoFree[(std::size_t) k];
            v.lfoHeld[(std::size_t) k] = bipolarRandom();
            v.lfoAge[(std::size_t) k] = 0.0f;
        }
        const auto mode = ip (unisonMode);
        for (int k = 0; k < 2; ++k) // (Live's two oscillators: oscillator 3 draws after everything, below)
        {
            auto& o = v.osc[(std::size_t) k];
            for (int u = 0; u < liveMaxUnison; ++u)
            {
                auto& uv = o.u[(std::size_t) u];
                // free phases: random per voice (Phase Sync and a lone voice start at 0) [MEASURED u4]
                uv.phase = mode == unisonPhaseSync || mode == unisonNone ? 0u : nextRandom();
                uv.fmPhase = 0u;
                uv.syncPhase = 0.0;
                uv.held = 0.0f;
                uv.drift = uv.driftTarget = 0.0f;
                uv.randomCents = bipolarRandom();
                uv.px = -1.0f, uv.wheld = 0.0f, uv.crushPrev = 0.0f, uv.crushG = 0.0f;
            }
            o.pos = -1.0f;
        }
        // Helix+'s voices 9 … 16 draw after Live's eight, only when the count asks for them (so a channel that leaves
        // Helix+'s count alone keeps the random sequence a channel before 190mi had)
        const bool sixteen = plus() && (int) roundHalfAway (xp (ext::unisonCount)) > liveMaxUnison;
        if (sixteen)
            for (int k = 0; k < 2; ++k)
                for (int u = liveMaxUnison; u < maxUnison; ++u)
                {
                    auto& o = v.osc[(std::size_t) k];
                    auto& uv = o.u[(std::size_t) u];
                    uv.phase = mode == unisonPhaseSync || mode == unisonNone ? 0u : nextRandom();
                    uv.fmPhase = 0u;
                    uv.syncPhase = 0.0;
                    uv.held = 0.0f;
                    uv.drift = uv.driftTarget = 0.0f;
                    uv.randomCents = bipolarRandom();
                    uv.px = -1.0f, uv.wheld = 0.0f, uv.crushPrev = 0.0f, uv.crushG = 0.0f;
                }
        // Helix+ 2 (round 190ml): oscillator 3's voices draw last, only while it plays (a channel without it keeps the
        // random sequence it had); the sample's voices start at Start; the chaos sources from the voice's own seed
        v.plus2 = plus2();
        if (osc3On())
        {
            auto& o = v.osc[2];
            for (int u = 0; u < (sixteen ? maxUnison : liveMaxUnison); ++u)
            {
                auto& uv = o.u[(std::size_t) u];
                uv.phase = mode == unisonPhaseSync || mode == unisonNone ? 0u : nextRandom();
                uv.fmPhase = 0u;
                uv.syncPhase = 0.0;
                uv.held = 0.0f;
                uv.drift = uv.driftTarget = 0.0f;
                uv.randomCents = bipolarRandom();
                uv.px = -1.0f, uv.wheld = 0.0f, uv.crushPrev = 0.0f, uv.crushG = 0.0f;
            }
            o.pos = -1.0f;
        }
        if (v.plus2)
        {
            const auto start = sample != nullptr ? std::clamp (xp2 (ext2::sampleStart), 0.0, 1.0) * (double) std::max (0, sample->frames() - 2) : 0.0;
            v.sPos.fill (start);
            v.sSync.fill (0u);
            v.sHeld.fill (0.0f);
            v.sCrush.fill (0.0f);
            v.sampleDone = false;
            for (int k = 0; k < chaosCount; ++k)
                v.chaos[(std::size_t) k].start ((std::uint32_t) (v.age * 2u + (std::uint64_t) k) ^ v.noise);
        }
        if (plus3()) // Helix round 3 (190mt): no grain yet (the first one at once), the grains' own random from the voice
        {
            v.grainCount = 0;
            v.grainNext = 0.0f;
            v.grainSeed = ((std::uint32_t) (v.age * 2654435761u) ^ v.noise ^ 0x5bd1e995u) | 1u;
            v.spStarted = false;
        }
        v.subPhase = 0u;
        v.f1 = {}, v.f2 = {};
        v.noiseLp = 0.0f;
        if (plus())
            for (int k = 0; k < 2; ++k)
                if (const auto xt = (int) roundHalfAway (xp (ext::filterType + k)); xt == xfilter::combPlus || xt == xfilter::combMinus)
                    std::fill (combLine (v, k), combLine (v, k) + 2 * combLength, 0.0f);
        v.counter = 0;
        v.amp = 0.0f;
    }

    void note (int key, float velocity, float pitch, float slide, float pressure) noexcept
    {
        takeTables();
        if (velocity <= 0.0f)
        {
            for (auto& v : voices)
                if (v.active && v.gate && v.key == key)
                {
                    v.gate = false;
                    for (int k = 0; k < 3; ++k)
                        if (envP[(std::size_t) k].loop != 1) // (Trigger plays through)
                            v.env[(std::size_t) k].release();
                }
            return;
        }
        if (ip (monoPoly) == 0) // Mono: one voice, legato while a key is held
        {
            auto& v = voices[0];
            const bool legato = v.active && v.gate;
            for (std::size_t i = 1; i < voices.size(); ++i)
                voices[i].active = false;
            startVoice (v, key, velocity, pitch, slide, pressure, legato);
            return;
        }
        const auto cap = polyVoiceCount (ip (polyVoices));
        Voice* slot = nullptr;
        int playing = 0;
        for (auto& v : voices)
            playing += v.active ? 1 : 0;
        for (auto& v : voices)
            if (! v.active)
            {
                slot = &v;
                break;
            }
        if (slot == nullptr || playing >= cap)
        {
            // steal: the oldest released voice, else the oldest
            Voice* best = nullptr;
            for (auto& v : voices)
                if (v.active && (best == nullptr || (v.gate == best->gate ? v.age < best->age : ! v.gate)))
                    best = &v;
            slot = best;
        }
        if (slot != nullptr)
            startVoice (*slot, key, velocity, pitch, slide, pressure, false);
    }

    // --- one control step of a voice (n samples)
    void control (Voice& v, int n, double tempo, double songBeats) noexcept
    {
        const auto dt = (float) (n / rate);
        const bool plusOn = plus();
        v.plus = plusOn;
        // sources
        float s[allSourceCount] {};
        s[src::amp] = v.env[0].value;
        s[src::env2] = v.env[1].value;
        s[src::env3] = v.env[2].value;
        s[src::velocity] = 2.0f * v.velocity - 1.0f; // (bipolar around velocity 64 [MEASURED m1 pitch vel / m3 pos vel])
        s[src::note] = (float) ((v.key - 60) / laws.noteSpan);
        s[src::pitchBend] = bendSource;
        s[src::pressure] = std::max (v.pressure, chanPressure);
        s[src::modWheel] = modWheel;
        s[src::random] = v.random;
        s[src::notePitchBend] = v.pitch / 48.0f;
        s[src::slide] = v.slide;
        if (plusOn)
            for (int j = 0; j < plusMacroCount; ++j)
                s[src::macro1 + j] = clamp01 ((float) xp (ext::macro + j));
        const bool plus2On = plus2();
        v.plus2 = plus2On;
        if (plus2On) // Helix+ 2: macros 5…8, the chaos sources (each voice's own, stepped only when a cell reads it)
        {
            if (newMacrosUsed)
                for (int j = plusMacroCount; j < macroCount; ++j)
                    s[src::macro1 + j] = clamp01 ((float) xp2 (ext2::macro5 + j - plusMacroCount));
            for (int k = 0; k < chaosCount; ++k)
                if (chaosUsed[(std::size_t) k])
                {
                    const auto b = ext2::chaosMode + k * ext2::chaosStride;
                    const auto rateHz = std::clamp (xp2 (b + 1), 0.01, 30.0);
                    v.chaosValue[(std::size_t) k] = chaosStep (std::clamp ((int) roundHalfAway (xp2 (b)), 0, 2), v.chaos[(std::size_t) k], rateHz * dt);
                    s[src::chaos1 + k] = v.chaosValue[(std::size_t) k] * clamp01 ((float) xp2 (b + 2));
                }
        }
        // the matrix: a row's sum of amount × source in the row's own unit (each row's law below); the internal sources
        // (envelopes, LFOs) scale with Global Mod Amount, the MIDI ones do not [MEASURED m8: Amount 50 % leaves Pitch ←
        // Velocity whole]. The level rows (Amp, the oscillators' and the sub's Gain) multiply instead: a unipolar view of
        // the source pulls the level down from full as it falls [MEASURED m4, m5, m6, m7, m10, d1]
        // (190ml: Helix+ 2's rows are cleared only when its block is live: nothing reads them otherwise)
        float mod[allTargetCount];
        float level[allTargetCount];
        std::fill (mod, mod + plusTargetCount, 0.0f);
        std::fill (level, level + plusTargetCount, 1.0f);
        if (plus2On)
        {
            std::fill (mod + plusTargetCount, mod + rev2TargetCount, 0.0f);
            std::fill (level + plusTargetCount, level + rev2TargetCount, 1.0f);
            if (r3Fill) // (round 3's rows only when something reads or writes them)
            {
                std::fill (mod + rev2TargetCount, mod + allTargetCount, 0.0f);
                std::fill (level + rev2TargetCount, level + allTargetCount, 1.0f);
            }
        }
        auto internalScale = std::clamp (fp (modAmount), 0.0f, 2.0f);
        auto applyCells = [&] (bool lfoRowsOnly) HELIX_LAMBDA_INLINE {
            for (int i = 0; i < cellCount; ++i)
            {
                const auto& c = cells[(std::size_t) i];
                const bool lfoRow = (c.flags & lfoRowCell) != 0;
                if (lfoRow != lfoRowsOnly)
                    continue;
                // Pitch ← Pitch Bend is the channel's wheel in LYNX (its range is the channel's) [MODEL]
                if (c.flags & wheelCell)
                    continue;
                const auto a = c.amount * ((c.flags & internalCell) ? internalScale : 1.0f);
                const auto t = c.target;
                if (c.flags & levelCell)
                {
                    const auto u = (c.flags & bipolarCell) ? 0.5f * (s[c.source] + 1.0f) : s[c.source];
                    level[t] *= std::max (0.0f, a >= 0.0f ? 1.0f - a * (1.0f - u) : 1.0f + a * u);
                    continue;
                }
                // Filter Freq ← Note is key tracking: 1 = an octave per octave [MEASURED m2 f1 note1]
                mod[t] += a * s[c.source] * ((c.flags & keyTrackCell) ? (float) (laws.keyTrackOctaves / laws.filterOctaves) : 1.0f);
            }
        };
        applyCells (true);
        for (int k = 0; k < 2; ++k)
        {
            const auto b = lfoBase (k);
            const auto row = k == 0 ? tgt::lfo1 : tgt::lfo2;
            const auto hz = p[(std::size_t) (b + lfo::sync)] > 0.5 ? tempo / 60.0 / lfoSyncBeats (ip (b + lfo::syncedRate))
                                                                   : std::clamp (p[(std::size_t) (b + lfo::rate)], 0.01, 30.0);
            const auto rateNow = hz * std::pow (2.0, laws.lfoRateOctaves * (double) mod[row + 2]);
            auto& ph = v.lfoPhase[(std::size_t) k];
            const auto before = ph;
            ph += rateNow * dt;
            if (std::floor (ph) != std::floor (before))
                v.lfoHeld[(std::size_t) k] = bipolarRandom();
            ph -= std::floor (ph);
            const auto x = (float) ph;
            float y;
            switch (std::clamp (ip (b + lfo::type), 0, 4))
            {
                case 0: y = std::sin (2.0f * (float) pi * x); break;
                case 1: y = x < 0.25f ? 4.0f * x : x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f; break;
                case 2: // (falls from 0 at the start, jumps up at half the cycle [MEASURED l0 type2])
                {
                    const auto q = x + 0.5f;
                    y = 1.0f - 2.0f * (q - std::floor (q));
                    break;
                }
                case 3: y = x < 0.5f ? 1.0f : -1.0f; break;
                default: y = v.lfoHeld[(std::size_t) k]; break;
            }
            if (plusOn && xp (ext::lfoDraw + k) > 0.5) // Helix+: the drawn shape
                y = drawnLfo (&p[(std::size_t) (extBase + ext::points + k * ext::lfoPointsSize)], (double) x);
            // Shaping ("Morph" in Live's LFO panel) bends the wave towards its peaks (> 0) or its middle (< 0) [MODEL, l3 approx.]
            const auto shaping = clampBi (fp (b + lfo::shaping) + 2.0f * mod[row + 1]);
            if (shaping != 0.0f)
                y = std::copysign (std::pow (std::abs (y), std::exp2 (-2.0f * shaping)), y);
            v.lfoAge[(std::size_t) k] += dt;
            const auto attack = fp (b + lfo::attack);
            const auto fade = attack > 0.0f ? clamp01 (v.lfoAge[(std::size_t) k] / attack) : 1.0f; // (a straight fade in [MEASURED l4])
            s[k == 0 ? src::lfo1 : src::lfo2] = y * clamp01 (fp (b + lfo::amount) + mod[row]) * fade;
        }
        applyCells (false);
        if (mod[tgt::globalMod] != 0.0f) // (Global Mod Amount modulated: the internal sources' rows again at the new scale)
        {
            const auto g = std::clamp (internalScale + 2.0f * mod[tgt::globalMod], 0.0f, 2.0f);
            if (internalScale > 0.0f)
                for (int i = 0; i < cellCount; ++i)
                    if (const auto& c = cells[(std::size_t) i]; (c.flags & internalCell) && ! (c.flags & levelCell))
                        mod[c.target] += c.amount * s[c.source] * (g - internalScale);
        }
        // envelopes
        // Time: every envelope time × 10^Time [MEASURED e9: ×3.16 / ×0.32 at ±50 %]
        const auto timeAll = (float) std::pow (10.0, std::clamp ((double) fp (timeScale) + 2.0 * mod[tgt::time], -1.0, 1.0));
        for (int k = 0; k < 3; ++k)
        {
            const auto& e = envP[(std::size_t) k];
            const int row = k == 0 ? tgt::ampAttack : k == 1 ? tgt::env2 : tgt::env3;
            float times[3] { e.attack, e.decay, e.release };
            for (int j = 0; j < 3; ++j)
                if (mod[row + j] != 0.0f)
                {
                    // an Attack / Decay row moves the time knob, whose travel is the cube root of the time over 20 s; a
                    // Release row adds its share of the 20 s straight [MEASURED W4 t1 … t4: Amp Decay / Attack ← Velocity
                    // or Env 2 0.25 makes 0.1 s ≥ 1.4 s, at velocity 64 unchanged, −0.25 makes it 0; t5: Amp Release ←
                    // Note 0.25 makes 0.2 s last ≥ 2 s two octaves up]
                    if (j == 2)
                        times[j] = std::clamp (times[j] + 20.0f * mod[row + j], 0.0f, 20.0f);
                    else
                    {
                        const auto x = std::cbrt (std::clamp (times[j], 0.0f, 20.0f) / 20.0f) + mod[row + j];
                        times[j] = 20.0f * std::pow (std::clamp (x, 0.0f, 1.0f), 3.0f);
                    }
                }
            EnvParams ep = e;
            if (k == 0)
                ep.sustain = clamp01 (e.sustain + mod[tgt::ampSustain]);
            else
            {
                ep.initial = clamp01 (e.initial + mod[row + 3]);
                ep.peak = clamp01 (e.peak + mod[row + 4]);
                ep.sustain = clamp01 (e.sustain + mod[row + 5]);
                ep.final = clamp01 (e.final + mod[row + 6]);
            }
            stepEnv (v.env[(std::size_t) k], ep, dt, timeAll, v.gate, times);
        }
        const auto ampTarget = v.env[0].value * level[tgt::amp];
        v.ampStep = (ampTarget - v.amp) / (float) n;
        // pitch: the note, its own offset (through Pitch ← Note PB), glide, the wheel, the Pitch row
        if (v.glidePos < 1.0f)
        {
            // (the Glide row adds its share of the knob's 20 s, straight [MEASURED W4 t7: ← Velocity 0.5 at 127 = 10.1 s])
            const auto g = std::max (1.0e-4, std::clamp (p[(std::size_t) glide] + 20.0 * mod[tgt::glide], 0.0, 20.0));
            v.glidePos = std::min (1.0f, v.glidePos + (float) (dt / g));
        }
        const auto glideNow = v.glideFrom * (1.0f - v.glidePos);
        const auto hostSt = v.host.left > 0.0f || v.host.cents != 0.0f ? v.host.advance ((float) n) * 0.01f : 0.0f; // (FL's porta / slides)
        auto notePitch = (double) v.key + fp (transpose) + glideNow + hostSt + pitchOffset + bend + laws.pitchSemis * mod[tgt::pitch];
        if (p[(std::size_t) matrixIndex (tgt::pitch, src::notePitchBend)] == 0.0)
            notePitch += 0.0; // (the note's own pitch is a matrix source; without its cell it does not move the pitch, as in Live)
        // unison
        const auto uMode = ip (unisonMode);
        auto uCount = uMode == unisonNone ? 1 : std::clamp (ip (unisonVoices), 2, liveMaxUnison);
        const auto uAmount = clamp01 (fp (unisonAmount) + mod[tgt::unison]);
        const auto uCents = (float) unisonCents (uMode, uAmount);
        // Helix+ unison: its count (16 at most), Blend (the side voices' level), Width (how far they sit to the sides),
        // Stack (octaves / fifths on top of the detune)
        auto uBlend = 1.0f, uWidth = 1.0f;
        auto uStack = 0;
        if (plusOn && uMode != unisonNone)
        {
            if (const auto c = (int) roundHalfAway (xp (ext::unisonCount)); c >= 2)
                uCount = std::min (c, maxUnison);
            uBlend = clamp01 ((float) xp (ext::unisonBlend) + mod[tgt::unisonBlend]);
            uWidth = clamp01 ((float) xp (ext::unisonWidth));
            uStack = std::clamp ((int) roundHalfAway (xp (ext::unisonStack)), 0, 6);
        }
        v.uCount = uCount;
        // the centre voices (the one in the middle, or the two) keep their level; the others take Blend, the summed power kept
        const bool blending = uBlend < 1.0f && uCount > 2;
        const auto isCentre = [uCount] (int u) { return uCount % 2 == 1 ? u == uCount / 2 : (u == uCount / 2 - 1 || u == uCount / 2); };
        // Helix+'s second warps: the other oscillator's frequency ratio (FM / AM / RM read it)
        // (Helix+ 2: oscillator 3 joins while it is on, with Live's oscillator laws and its own rows)
        const auto oscN = osc3On() ? 3 : 2;
        v.oscs = oscN;
        std::array<double, oscCount> oscHz {};
        if (plusOn)
            for (int k = 0; k < oscN; ++k)
            {
                v.warpMode[(std::size_t) k] = std::clamp ((int) roundHalfAway (p[(std::size_t) warpModeIndex (k)]), 0, warp::count - 1);
                v.warpAmount[(std::size_t) k] = clamp01 ((float) p[(std::size_t) warpAmountIndex (k)] + mod[tgt::warpRow (k)]);
                const auto b = oscIndex (k, 0);
                oscHz[(std::size_t) k] = (double) fp (b + osc::transpose) + fp (b + osc::detune) + laws.oscPitchSemis * mod[tgt::oscRow (k, tgt::oscPitch)];
            }
        else
            v.warpMode = {};
        if (oscN < 3)
            v.warpMode[2] = warp::off, v.gain[2] = 0.0f;
        for (int k = 0; k < oscN; ++k)
        {
            const auto b = oscIndex (k, 0);
            const auto row = tgt::oscRow (k, 0);
            auto& o = v.osc[(std::size_t) k];
            v.gain[(std::size_t) k] = p[(std::size_t) (b + osc::on)] > 0.5 ? clamp01 (fp (b + osc::gain)) * level[row + tgt::oscGain] : 0.0f;
            const auto pos = clamp01 (fp (b + osc::position) + mod[row + tgt::oscPos]);
            if (o.pos < 0.0f)
                o.pos = pos;
            o.posStep = (pos - o.pos) / (float) n;
            v.mode[(std::size_t) k] = std::clamp (ip (b + osc::effectMode), 0, 3);
            v.fx1[(std::size_t) k] = clampBi (fp (b + osc::effect1) + mod[row + tgt::oscFx1]); // [MEASURED W4 p2: PW ← Velocity 0.5 at 127 is not silent]
            v.fx2[(std::size_t) k] = clamp01 (fp (b + osc::effect2) + mod[row + tgt::oscFx2]);
            const auto pan = clampBi (fp (b + osc::pan) + 2.0f * mod[row + tgt::oscPan]); // [MEASURED m11: Pan ← LFO 1 reaches a side at 0.5]
            const auto semis = notePitch + fp (b + osc::transpose) + fp (b + osc::detune) + laws.oscPitchSemis * mod[row + tgt::oscPitch];
            if (semis != v.keySemis[(std::size_t) k])
                v.keySemis[(std::size_t) k] = semis, v.keyOscHz[(std::size_t) k] = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
            const auto hz = v.keyOscHz[(std::size_t) k];
            // the mip: no partial above Nyquist at the highest frequency the effect reaches
            auto stretch = 1.0;
            const auto mode = v.mode[(std::size_t) k];
            if (mode == effectFm)
                stretch = 1.0 + laws.fmIndex * v.fx2[(std::size_t) k] * fmRatio (v.fx1[(std::size_t) k]);
            else if (mode == effectClassic) // (the squeezed cycle runs 1 / width faster; sync: the ratio)
                stretch = syncRatio (v.fx2[(std::size_t) k]) / std::max (1.0 - std::abs ((double) v.fx1[(std::size_t) k]), 0.02);
            else if (mode == effectModern)
            {
                // warp: the curve's steepest point is 1 + 32 w² times the speed, but only briefly: its geometric middle; fold
                // runs the phase 1 + 7 × Fold times faster
                const auto w = (double) v.fx1[(std::size_t) k];
                stretch = std::sqrt (1.0 + laws.warpCurve * w * w) * (v.fx2[(std::size_t) k] > 1.0e-4f ? 1.0 + laws.foldSpan * v.fx2[(std::size_t) k] : 1.0);
            }
            if (plusOn && v.warpMode[(std::size_t) k] != warp::off)
            {
                const auto ratio = std::pow (2.0, (oscHz[(std::size_t) otherOsc (k)] - oscHz[(std::size_t) k]) / 12.0);
                v.otherRatio[(std::size_t) k] = (float) ratio;
                stretch *= warpStretch (v.warpMode[(std::size_t) k], v.warpAmount[(std::size_t) k], ratio);
            }
            if (uStack != 0)
                stretch *= std::pow (2.0, (uStack == 6 ? 24 : uStack == 3 ? 24 : 12) / 12.0);
            const auto top = hz * (uCount > 1 ? std::pow (2.0, uCents / 1200.0) : 1.0) * stretch;
            auto powerLive = 0.0f, powerNow = 0.0f; // (Blend: the voices' summed power before and after)
            auto m = 0;
            while (m < mipCount - 1 && partialsAt[(std::size_t) m] * top > rate * 0.5)
                ++m;
            v.mip[(std::size_t) k] = m;
            // each unison voice: detune, side, position offset
            for (int u = 0; u < uCount; ++u)
            {
                auto& uv = o.u[(std::size_t) u];
                const auto spread = uCount > 1 ? 2.0f * (float) u / (float) (uCount - 1) - 1.0f : 0.0f; // −1 … 1
                auto cents = 0.0f, side = 0.0f, posOff = 0.0f;
                // Live's unison: the voices alternate hard left / right (the two sides do not correlate), the detune spreads
                // evenly to ± 50 cents × Amount (Classic, Phase Sync); Shimmer wanders slowly over ± 180 cents × Amount, Noise
                // fast over ± 70; Position Spread detunes a little and spreads the position; Random Note gives every note its
                // own detunes [MEASURED u1 … u6: peaks of the spectrum, L / R correlation 0, level = one voice]
                switch (uMode)
                {
                    case unisonClassic:
                    case unisonPhaseSync: cents = uCents * spread; break;
                    case unisonShimmer:
                    case unisonNoise:
                    {
                        const auto depth = uCents * (uMode == unisonShimmer ? 3.6f : 1.4f);
                        const auto speed = uMode == unisonShimmer ? 4.0f : 150.0f;
                        if ((nextRandom() & (uMode == unisonShimmer ? 255u : 3u)) == 0u)
                            uv.driftTarget = bipolarRandom() * depth;
                        uv.drift += (uv.driftTarget - uv.drift) * std::min (1.0f, speed * dt);
                        cents = uv.drift;
                        break;
                    }
                    case unisonPositionSpread: cents = uCents * spread, posOff = 0.5f * uAmount * spread; break;
                    case unisonRandomNote: cents = uCents * 0.4f * uv.randomCents; break;
                    default: break;
                }
                if (uCount > 1)
                    side = (u & 1) ? 1.0f : -1.0f;
                if (uWidth < 1.0f)
                    side *= uWidth;
                if (uBlend < 1.0f && uCount > 2 && isCentre (u)) // (the centre voices move to the middle as the sides fade)
                    side *= uBlend;
                v.uPosOffset[(std::size_t) k][(std::size_t) u] = posOff;
                // (the stack walks the voices in left / right pairs from four voices up, so each side hears every octave)
                const auto voiceCents = uStack != 0 ? cents + 100.0f * (float) unisonStackSemis (uStack, uCount >= 4 ? u / 2 : u) : cents;
                if (auto& key = v.incKey[(std::size_t) k][(std::size_t) u]; key.hz != hz || key.cents != voiceCents || key.rate != rate)
                {
                    key = { hz, rate, voiceCents };
                    const auto f = hz * std::pow (2.0, voiceCents / 1200.0) / rate;
                    v.uInc[(std::size_t) k][(std::size_t) u] = (std::uint32_t) (std::int64_t) (std::clamp (f, 0.0, 0.5) * 4294967296.0);
                }
                // Live's pan: −3 dB each side in the middle; towards a side the near one rises to +3 dB as √|pan| and the far one
                // falls as √(1 − |pan|) [MEASURED a3, a4, m11]; unison voices sit hard on their side, 1 / √voices each
                const auto pp = clampBi (pan + side);
                const auto c = uCount > 1 ? 0.5f / std::sqrt ((float) uCount) : (float) laws.centreGain;
                const auto near = c * (1.0f + std::sqrt (std::abs (pp))), far = c * std::sqrt (1.0f - std::abs (pp));
                v.uGainL[(std::size_t) k][(std::size_t) u] = pp > 0.0f ? far : near;
                v.uGainR[(std::size_t) k][(std::size_t) u] = pp > 0.0f ? near : far;
                if (blending)
                {
                    const auto ref = clampBi (pan + ((u & 1) ? 1.0f : -1.0f) * uWidth); // (where Live's voice would sit)
                    const auto rn = c * (1.0f + std::sqrt (std::abs (ref))), rf = c * std::sqrt (1.0f - std::abs (ref));
                    powerLive += rn * rn + rf * rf;
                    const auto g = isCentre (u) ? 1.0f : uBlend;
                    v.uGainL[(std::size_t) k][(std::size_t) u] *= g;
                    v.uGainR[(std::size_t) k][(std::size_t) u] *= g;
                    powerNow += v.uGainL[(std::size_t) k][(std::size_t) u] * v.uGainL[(std::size_t) k][(std::size_t) u]
                                + v.uGainR[(std::size_t) k][(std::size_t) u] * v.uGainR[(std::size_t) k][(std::size_t) u];
                }
            }
            if (blending && powerNow > 0.0f)
                for (int u = 0; u < uCount; ++u)
                {
                    const auto g = std::sqrt (powerLive / powerNow);
                    v.uGainL[(std::size_t) k][(std::size_t) u] *= g;
                    v.uGainR[(std::size_t) k][(std::size_t) u] *= g;
                }
        }
        // Helix+ 2's sample oscillator: each unison voice reads the sample at the note's speed over the root key, detuned as
        // oscillator 1's voices, on its side of the stereo field, at Live's pan law
        v.sampleOn = sampleLive() && ! v.sampleDone;
        if (v.sampleOn)
        {
            const auto pan = clampBi ((float) xp2 (ext2::samplePan) + 2.0f * mod[tgt::samplePan]);
            const auto g = clamp01 ((float) xp2 (ext2::sampleGain)) * level[tgt::sampleGain];
            const auto semis = notePitch + xp2 (ext2::sampleTranspose) + xp2 (ext2::sampleDetune) + laws.oscPitchSemis * mod[tgt::samplePitch]
                               - std::clamp (xp2 (ext2::sampleRoot), 0.0, 127.0);
            const auto speed = std::pow (2.0, semis / 12.0) * sample->rate / rate;
            v.sWarp = std::clamp ((int) roundHalfAway (xp2 (ext2::sampleWarpMode)), 0, warp::count - 1);
            v.sWarpAmount = clamp01 ((float) xp2 (ext2::sampleWarpAmount));
            const auto noteHz = 440.0 * std::pow (2.0, (notePitch - 69.0) / 12.0);
            for (int u = 0; u < uCount; ++u)
            {
                const auto spread = uCount > 1 ? 2.0f * (float) u / (float) (uCount - 1) - 1.0f : 0.0f;
                const auto& ov = v.osc[0].u[(std::size_t) u];
                auto cents = uMode == unisonShimmer || uMode == unisonNoise ? ov.drift : uMode == unisonRandomNote ? uCents * 0.4f * ov.randomCents : uCents * spread;
                if (uStack != 0)
                    cents += 100.0f * (float) unisonStackSemis (uStack, uCount >= 4 ? u / 2 : u);
                const auto ratio = std::pow (2.0, cents / 1200.0);
                v.sInc[(std::size_t) u] = speed * ratio;
                v.sSyncInc[(std::size_t) u] = (std::uint32_t) (std::int64_t) (std::clamp (noteHz * ratio / rate, 0.0, 0.5) * 4294967296.0);
                auto side = uCount > 1 ? ((u & 1) ? 1.0f : -1.0f) * uWidth : 0.0f;
                const auto pp = clampBi (pan + side);
                const auto c = uCount > 1 ? 0.5f / std::sqrt ((float) uCount) : (float) laws.centreGain;
                const auto near = c * (1.0f + std::sqrt (std::abs (pp))), far = c * std::sqrt (1.0f - std::abs (pp));
                const auto ug = uCount > 2 && uBlend < 1.0f && ! isCentre (u) ? uBlend : 1.0f;
                v.sGainL[(std::size_t) u] = (pp > 0.0f ? far : near) * g * ug;
                v.sGainR[(std::size_t) u] = (pp > 0.0f ? near : far) * g * ug;
            }
        }
        // Helix round 3 (round 190mt): Clean, the grains' and the partials' step values (read only with Helix+ 2 live: the
        // voice loop asks for them only then)
        if (plus2On)
        {
            v.plus3 = r3Live;
            if (v.plus3)
            {
                v.sClean = levels != nullptr && xp3 (ext3::sampleClean) > 0.5;
                v.grainOn = grainLive();
                v.specOn = specLive();
                if (v.grainOn)
                    grainControl (v, notePitch, mod, level);
                if (v.specOn)
                    spectralControl (v, n, notePitch, mod, level);
            }
            else
                v.sClean = false;
        }
        // Helix+'s noise: white through a one-pole (Color < 0.5 darker, > 0.5 brighter) into the oscillators' bus
        v.noiseGain = 0.0f;
        if (plusOn && xp (ext::noiseOn) > 0.5)
        {
            v.noiseGain = clamp01 ((float) xp (ext::noiseLevel) + mod[tgt::noiseLevel]) * (float) laws.centreGain;
            const auto c = std::clamp (xp (ext::noiseColour), 0.0, 1.0);
            v.noiseHigh = c > 0.5;
            const auto hz = c <= 0.5 ? 150.0 * std::pow (20000.0 / 150.0, 2.0 * c) : 20.0 * std::pow (300.0, 2.0 * c - 1.0);
            v.noiseCoef = (float) (1.0 - std::exp (-2.0 * pi * std::min (hz, rate * 0.45) / rate));
        }
        // the sub: an octave or two below oscillator 1's note [MEASURED s*]
        v.subGain = p[(std::size_t) subOn] > 0.5 ? clamp01 (fp (subGain)) * level[tgt::subGain] * (float) (laws.subLevel * laws.centreGain) : 0.0f;
        v.subTone = clamp01 (fp (subTone) + mod[tgt::subTone]);
        {
            const auto semis = notePitch - 12.0 * std::clamp (ip (subTranspose), 0, 2);
            if (semis != v.keySubSemis || rate != v.keySubRate)
            {
                v.keySubSemis = semis, v.keySubRate = rate;
                const auto f = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0) / rate;
                v.subInc = (std::uint32_t) (std::int64_t) (std::clamp (f, 0.0, 0.5) * 4294967296.0);
            }
        }
        // filters
        for (int k = 0; k < 2; ++k)
        {
            const auto b = filterBase (k);
            auto& c = v.fc[(std::size_t) k];
            c.on = p[(std::size_t) (b + flt::on)] > 0.5;
            if (! c.on)
                continue;
            const auto row = tgt::filterFreq + k * tgt::filterStride;
            c.type = std::clamp (ip (b + flt::type), 0, 4);
            c.steep = p[(std::size_t) (b + flt::slope)] > 0.5;
            c.circuit = c.type <= 1 ? std::clamp (ip (b + flt::circuitLpHp), 0, 4) : std::clamp (ip (b + flt::circuitBpNoMo), 0, 1);
            const auto freqKnob = p[(std::size_t) (b + flt::freq)];
            const auto freqMod = mod[row];
            const auto res = std::clamp (fp (b + flt::res) + 1.25f * mod[row + 1], 0.0f, 1.25f);
            const auto driveDb = std::clamp (fp (b + flt::drive) + 24.0f * mod[row + 2], 0.0f, 24.0f);
            c.morph = clamp01 (fp (b + flt::morph) + mod[row + 3]);
            if (freqKnob != c.keyFreq || freqMod != c.keyMod || res != c.keyRes || driveDb != c.keyDrive || c.circuit != c.keyCircuit
                || c.steep != c.keySteep || rate != c.keyRate)
            {
                c.keyFreq = freqKnob, c.keyMod = freqMod, c.keyRes = res, c.keyDrive = driveDb, c.keyCircuit = c.circuit, c.keySteep = c.steep;
                c.keyRate = rate;
                c.hz = filterHz (filterNorm (freqKnob) + freqMod);
                c.drive = std::pow (10.0f, driveDb / 20.0f);
                // resonance → damping [MEASURED 190hs f0x: 24 dB/oct peaks +6.9 / +22.9 dB at Res 0.5 / 1; above 1 = 1]
                static constexpr float resScale[5] { 1.0f, 0.85f, 1.08f, 0.97f, 0.88f };
                const auto r = std::clamp (res * resScale[c.circuit], 0.0f, 1.0f);
                const auto kmin = c.steep ? 0.267f : 0.067f;
                c.k = kmin + (2.0f - kmin) * std::pow (1.0f - r, 2.1f);
                const auto fhz = (float) std::min (c.circuit > 0 ? c.hz * 0.88 : c.hz, rate * 0.45);
                const auto g = std::tan ((float) pi * fhz / (float) rate);
                c.a1 = 1.0f / (1.0f + g * (g + c.k));
                c.a2 = g * c.a1;
                c.a3 = g * c.a2;
            }
            const auto hz = c.hz;
            c.xtype = plusOn ? std::clamp ((int) roundHalfAway (xp (ext::filterType + k)), 0, xfilter::count - 1) : xfilter::live;
            if (c.xtype != xfilter::live)
                extCoefs (c, hz, res, k);
        }
        if (plus2On && v.age >= fxModAge) // (the rack's rows follow the newest note)
            fxModAge = v.age, fxShiftMod = mod[tgt::shift], fxDepthMod = mod[tgt::mbDepth];
        (void) songBeats;
    }

    // Helix+'s filter types: their coefficients from Live's Freq / Res / Morph rows (round 190mi) [MODEL]
    void extCoefs (FilterCoefs& c, double hz, float res, int) const noexcept
    {
        const auto r = std::clamp (res / 1.25f, 0.0f, 1.0f);
        switch (c.xtype)
        {
            case xfilter::combPlus:
            case xfilter::combMinus:
                c.combDelay = (float) std::clamp (rate / std::max (hz, 1.0), 2.0, (double) combLength - 3.0);
                c.combGain = (c.xtype == xfilter::combPlus ? 1.0f : -1.0f) * (0.3f + 0.67f * r);
                break;
            case xfilter::formant:
            {
                // Freq moves the vowel's bands together (640 Hz = as spoken, the knob's middle), Morph walks A E I O U,
                // Res narrows the bands; each band's peak normalised to its own level
                const auto shift = std::clamp (hz / 640.0, 0.25, 4.0);
                const auto f = formantHz (c.morph);
                static constexpr float levels[3] { 1.0f, 0.6f, 0.35f };
                for (int b = 0; b < 3; ++b)
                {
                    const auto fb = (float) std::min (f[(std::size_t) b] * shift, rate * 0.45);
                    const auto g = std::tan ((float) pi * fb / (float) rate);
                    const auto kk = 0.5f - 0.44f * r;
                    c.fk[(std::size_t) b] = kk;
                    c.fa1[(std::size_t) b] = 1.0f / (1.0f + g * (g + kk));
                    c.fa2[(std::size_t) b] = g * c.fa1[(std::size_t) b];
                    c.fa3[(std::size_t) b] = g * c.fa2[(std::size_t) b];
                    c.fgain[(std::size_t) b] = levels[b] * kk * 1.6f;
                }
                break;
            }
            case xfilter::phaser:
                // four all-passes spread around Freq (Morph spreads them up to ±1.5 octaves), Res feeds back
                for (int i = 0; i < 4; ++i)
                {
                    const auto f = std::clamp (hz * std::pow (2.0, ((double) i - 1.5) * c.morph), 20.0, rate * 0.45);
                    const auto t = std::tan (pi * f / rate);
                    c.ap[(std::size_t) i] = (float) ((t - 1.0) / (t + 1.0));
                }
                c.apFeedback = 0.85f * r;
                break;
            default: break;
        }
    }

    // Helix+'s noise oscillator into out L / R (adds)
    void noiseStep (Voice& v, int n, float* outL, float* outR) noexcept
    {
        if (v.noiseGain <= 0.0f)
            return;
        for (int i = 0; i < n; ++i)
        {
            v.noise ^= v.noise << 13, v.noise ^= v.noise >> 17, v.noise ^= v.noise << 5;
            const auto x = (float) ((double) (v.noise >> 8) / 8388608.0 - 1.0);
            v.noiseLp += v.noiseCoef * (x - v.noiseLp);
            const auto y = (v.noiseHigh ? x - v.noiseLp : v.noiseLp) * v.noiseGain;
            outL[i] += y;
            outR[i] += y;
        }
    }

    // Helix+ 2 (round 190ml): the hard-edged warps' corrections for this sample (y: the warped sample, px: the warp's input
    // phase). The next sample's input phase is foreseen from the last step (exact for a steady phase); a jump between this
    // sample and the next one at a fraction tau of the way adds the polyBLEP's two halves (this one now, the next one held),
    // a kink the polyBLAMP's; Crush's steps go through the first-order antiderivative of the quantiser.
    // (w2: the warp as a constant, so each anti-aliased warp's voice loop holds only its own corrections)
    // (the memory: a copy in locals while the voice's samples run, so the stores into the output need not reload it)
    struct AaMemory
    {
        float px, wheld, crushPrev, crushG;
    };
    template <int w2, typename Read, typename ToPhase>
    static HELIX_INLINE float antiAlias (AaMemory& uv, float y, float px, float dt, float wa, float syncK, float pwmW, float flipAt,
                                         float crushL, float delta, bool linear, const Read& read, const ToPhase& toPhase) noexcept
    {
        if constexpr (w2 == warp::crush)
        {
            if (wa < 1.0e-4f)
                return y;
            // F (x) = ∫0^x round (s L) / L ds = G (u) / L², G (u) = k (k − 1) / 2 + k (u − k + ½), u = |x| L, k = round (u)
            // (even in x); the last sample's G is kept
            const auto G = [crushL] (float x) noexcept {
                const auto u = std::abs (x) * crushL;
                const auto k = std::floor (u + 0.5f);
                return k * (k - 1.0f) * 0.5f + k * (u - k + 0.5f);
            };
            const auto x0 = uv.crushPrev, g0 = uv.crushG;
            const auto gy = G (y);
            const bool first = uv.px < 0.0f;
            uv.crushPrev = y, uv.crushG = gy, uv.px = px;
            const auto dx = y - x0;
            if (first || std::abs (dx) < 1.0e-5f)
                return std::round ((first ? y : 0.5f * (y + x0)) * crushL) / crushL;
            return (gy - g0) / (crushL * crushL * dx);
        }
        auto d = dt;
        if (! linear && uv.px >= 0.0f) // (a steady phase moves by dt: nothing to foresee)
        {
            d = px - uv.px;
            if (d < 0.0f)
                d += 1.0f;
            if (d > 0.5f || d <= 0.0f)
                d = dt;
        }
        uv.px = px;
        y += uv.wheld;
        uv.wheld = 0.0f;
        if (d <= 0.0f)
            return y;
        const auto next = px + d;
        const auto blep = [&] (float at, float jump) noexcept { // a jump of `jump` where the input phase reaches `at`
            if (px < at && next >= at)
            {
                const auto tau = std::clamp ((at - px) / d, 0.0f, 1.0f);
                y += jump * (1.0f - tau) * (1.0f - tau) * 0.5f;
                uv.wheld -= jump * tau * tau * 0.5f;
            }
        };
        const auto blamp = [&] (float at, float slope) noexcept { // the slope (per input phase) changes by `slope` at `at`
            if (px < at && next >= at)
            {
                const auto tau = std::clamp ((at - px) / d, 0.0f, 1.0f);
                const auto s = slope * d;
                y += s * (1.0f - tau) * (1.0f - tau) * (1.0f - tau) * (1.0f / 6.0f);
                uv.wheld += s * tau * tau * tau * (1.0f / 6.0f);
            }
        };
        // each cut: the jump in value (polyBLEP) and in slope (polyBLAMP) between the wave before it and after it, both read
        // at the cut (a table's own band-limited edge split by the cut, e.g. a saw's at phase 0, is a large slope there)
        const auto frac = [] (float v) noexcept { return v - std::floor (v); };
        const auto slopeAt = [&] (float x) noexcept { // the frame's slope per turn at x (its interpolated samples either side)
            return (read (toPhase (frac (x + delta))) - read (toPhase (frac (x - delta)))) / (2.0f * delta);
        };
        const auto cut = [&] (float at, float jump, float slope) noexcept {
            blep (at, jump);
            blamp (at, slope);
        };
        switch (w2)
        {
            case warp::sync:
                if (next >= 1.0f)
                {
                    const auto end = syncK - std::floor (syncK);
                    cut (1.0f, read (0u) - read (toPhase (end)), syncK * (slopeAt (0.0f) - slopeAt (end)));
                }
                break;
            // (the cuts' values are read only in the sample before a cut: once or twice a cycle)
            case warp::pwm:
                if (pwmW < 1.0f && ((px < pwmW && next >= pwmW) || next >= 1.0f))
                {
                    const auto r0 = read (0u), s0 = slopeAt (0.0f) / pwmW;
                    cut (pwmW, -r0, -s0);
                    cut (1.0f, r0, s0);
                }
                break;
            case warp::flip:
                if (flipAt < 1.0f && px < flipAt && next >= flipAt)
                    cut (flipAt, -2.0f * read (toPhase (flipAt)), -2.0f * slopeAt (flipAt));
                if (flipAt < 1.0f && next >= 1.0f)
                    cut (1.0f, 2.0f * read (0u), 2.0f * slopeAt (0.0f));
                break;
            case warp::mirror:
                if (wa > 1.0e-4f && ((px < 0.5f && next >= 0.5f) || next >= 1.0f))
                {
                    // the turn reads the table's start backwards: its slope there, from both sides of phase 0
                    const auto slope0 = slopeAt (0.0f);
                    blamp (0.5f, -4.0f * wa * slope0);
                    blamp (1.0f, 4.0f * wa * slope0);
                }
                break;
            default: break;
        }
        return y;
    }

    // oscillator 3's step (190ml): the same body in a call of its own, so oscillators 1 and 2 stay inlined in the voice loop
    // as before (the default patch's instructions)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__ ((noinline))
#endif
    void osc3Step (Voice& v, int n, float* outL, float* outR) noexcept { oscStep (v, 2, n, outL, outR); }

    // one oscillator's step into out L / R (adds)
    HELIX_INLINE void oscStep (Voice& v, int k, int n, float* outL, float* outR) noexcept
    {
        const auto* tb = tables[(std::size_t) k];
        const auto g = v.gain[(std::size_t) k];
        auto& o = v.osc[(std::size_t) k];
        if (tb == nullptr || g <= 0.0f)
        {
            // (a silent oscillator that another one's FM / AM / RM reads keeps each voice's own speed, Helix+)
            bool own = false;
            if (v.plus)
                for (int j = 0; j < v.oscs; ++j)
                    if (j != k && otherOsc (j) == k)
                    {
                        const auto xw = v.warpMode[(std::size_t) j];
                        own = own || xw == warp::fm || xw == warp::am || xw == warp::rm;
                    }
            if (k == 0 && v.sampleOn && v.sWarp >= warp::fm) // (the sample's FM / AM / RM read oscillator 1)
                own = true;
            for (std::size_t u = 0; u < o.u.size(); ++u)
                o.u[u].phase += v.uInc[(std::size_t) k][own ? u : 0] * (std::uint32_t) n;
            o.pos += o.posStep * (float) n;
            return;
        }
        const auto m = v.mip[(std::size_t) k];
        const auto len = mipLength (m);
        const auto& frames = tb->frames[(std::size_t) m]; // (a mip's frames; equal ones shared, WavetableTables.cpp)
        const auto shift = len == 4096 ? 20 : len == 2048 ? 21 : len == 1024 ? 22 : 23; // (lengths 512 … 4096)
        const auto fracScale = 1.0f / (float) (1u << shift);
        const auto fracMask = (1u << shift) - 1u;
        const auto uMode = ip (unisonMode);
        const auto uCount = v.plus ? v.uCount : uMode == unisonNone ? 1 : std::clamp (ip (unisonVoices), 2, liveMaxUnison);
        const auto mode = v.mode[(std::size_t) k];
        const auto fx1 = v.fx1[(std::size_t) k], fx2 = v.fx2[(std::size_t) k];
        // effect constants
        const auto fmIdx = (float) (laws.fmIndex * fx2 / (2.0 * pi));       // turns of phase per unit of the modulator
        const auto fmR = fmRatio (fx1);
        const auto width = std::max (1.0f - std::abs (fx1), 1.0e-4f);     // Classic PW: the part of the cycle the wave takes
        const bool pwLate = fx1 < 0.0f;                                     // (PW < 0: the gap comes first [MEASURED x2 pw-0.5])
        const auto sync = syncRatio (fx2);
        const bool syncing = mode == effectClassic && fx2 > 1.0e-4f;
        const auto warpC = fx1 == 0.0f ? 1.0f : std::pow (1.0f + (float) laws.warpCurve * fx1 * fx1, fx1 > 0.0f ? -1.0f : 1.0f);
        const auto foldG = 1.0f + (float) laws.foldSpan * fx2;
        const bool folding = mode == effectModern && fx2 > 1.0e-4f;
        // Helix+'s second warp (round 190mi): its constants; FM / AM / RM read the other oscillator's table, voice by voice
        const auto w2 = v.plus ? v.warpMode[(std::size_t) k] : warp::off;
        const auto wa = v.warpAmount[(std::size_t) k];
        float syncK = 1.0f, bendP = 1.0f, pwmW = 1.0f, asymM = 0.5f, flipAt = 1.0f, crushL = 256.0f, fmDepth = 0.0f;
        if (w2 != warp::off) // (only a second warp pays for its constants)
        {
            syncK = 1.0f + 15.0f * wa;
            bendP = (float) bendPower (warpBipolar (w2, wa));
            pwmW = 1.0f - 0.95f * wa;
            asymM = (float) asymSplit (warpBipolar (w2, wa));
            flipAt = 1.0f - 0.5f * wa;
            crushL = std::exp2 (1.0f + 7.0f * (1.0f - wa));
            fmDepth = 2.0f * wa;
        }
        const bool xmod = w2 == warp::fm || w2 == warp::am || w2 == warp::rm;
        // Helix+ 2 (round 190ml): the hard-edged warps anti-aliased (revision 2): a polyBLEP at each jump (Sync's restart,
        // PWM's gap, Flip's turn), a polyBLAMP at each kink (Mirror's turn), Crush through its antiderivative
        const bool aa = v.plus2 && hardEdged (w2);
        const auto invLen = 1.0f / (float) len;
        const auto ok = otherOsc (k);
        const auto* otherTable = xmod ? tables[(std::size_t) ok] : nullptr;
        const float* oa = nullptr;
        const float* ob = nullptr;
        float ow = 0.0f;
        int oShift = 20;
        if (otherTable != nullptr)
        {
            const auto om = v.mip[(std::size_t) ok];
            const auto olen = mipLength (om);
            oShift = olen == 4096 ? 20 : olen == 2048 ? 21 : olen == 1024 ? 22 : 23;
            const auto fpos = clamp01 (v.osc[(std::size_t) ok].pos) * (float) (frameCount - 1);
            const auto f0 = std::min ((int) fpos, frameCount - 2);
            ow = fpos - (float) f0;
            oa = otherTable->frame (om, f0);
            ob = otherTable->frame (om, f0 + 1);
        }
        const auto oFracScale = 1.0f / (float) (1u << oShift);
        const auto oFracMask = (1u << oShift) - 1u;
        // the voices' loop twice: as before 190mi (no second warp: exactly the code that was) and with Helix+'s second warp
        const auto voicesLoop = [&] (auto warpOn, auto aaWarp) HELIX_LAMBDA_INLINE {
        constexpr bool warping = decltype (warpOn)::value;
        constexpr int aaMode = decltype (aaWarp)::value; // (Helix+ 2: the anti-aliased warp, 0 = none)
        for (int u = 0; u < uCount; ++u)
        {
            auto& uv = o.u[(std::size_t) u];
            const auto inc = v.uInc[(std::size_t) k][(std::size_t) u];
            const auto gl = v.uGainL[(std::size_t) k][(std::size_t) u] * g, gr = v.uGainR[(std::size_t) k][(std::size_t) u] * g;
            auto pos = o.pos + v.uPosOffset[(std::size_t) k][(std::size_t) u];
            const auto fmInc = (std::uint32_t) (std::int64_t) ((double) inc * fmR);
            const auto dt = (float) inc / 4294967296.0f;
            // the frame pair at the position (found once when the position holds still over the step)
            const float* a = nullptr;
            const float* b = nullptr;
            float w = 0.0f;
            const auto findFrames = [&] (float at) noexcept {
                const auto fpos = clamp01 (at) * (float) (frameCount - 1);
                const auto f0 = std::min ((int) fpos, frameCount - 2);
                w = fpos - (float) f0;
                a = frames[(std::size_t) f0];
                b = frames[(std::size_t) f0 + 1];
            };
            const bool moving = o.posStep != 0.0f;
            findFrames (pos);
            auto oph = v.phaseAtStep[(std::size_t) ok][(std::size_t) u];
            AaMemory aaMemory { uv.px, uv.wheld, uv.crushPrev, uv.crushG };
            const auto oinc = v.uInc[(std::size_t) ok][(std::size_t) u];
            for (int i = 0; i < n; ++i)
            {
                if (moving)
                    findFrames (pos);
                auto read = [&] (std::uint32_t ph) noexcept {
                    const auto idx = ph >> shift;
                    const auto fr = (float) (ph & fracMask) * fracScale;
                    const auto va = a[idx] + (a[idx + 1] - a[idx]) * fr;
                    const auto vb = b[idx] + (b[idx + 1] - b[idx]) * fr;
                    return va + (vb - va) * w;
                };
                auto m = 0.0f; // (the other oscillator's sample: FM / AM / RM)
                if (warping && oa != nullptr)
                {
                    const auto idx = oph >> oShift;
                    const auto fr = (float) (oph & oFracMask) * oFracScale;
                    const auto va = oa[idx] + (oa[idx + 1] - oa[idx]) * fr;
                    const auto vb = ob[idx] + (ob[idx + 1] - ob[idx]) * fr;
                    m = va + (vb - va) * ow;
                    oph += oinc;
                }
                const auto toPhase = [] (float px) noexcept { return (std::uint32_t) (std::int64_t) (std::min (px, 0.9999999f) * 4294967296.0f); };
                float wIn = 0.0f; // (the warp's input phase this sample)
                // the read with Helix+'s second warp (off: exactly `read`)
                auto sample = [&] (std::uint32_t ph) noexcept -> float {
                    if constexpr (! warping)
                        return read (ph);
                    else
                    {
                    auto px = (float) ph * (1.0f / 4294967296.0f);
                    if constexpr (aaMode != 0)
                        wIn = px;
                    auto g = 1.0f;
                    switch (w2)
                    {
                        case warp::windowSync: g = std::sin ((float) pi * px); [[fallthrough]];
                        case warp::sync:
                        {
                            const auto q = px * syncK;
                            px = q - std::floor (q);
                            break;
                        }
                        case warp::bendPlus: case warp::bendMinus: case warp::bendBoth:
                        {
                            const auto uu = 2.0f * px - 1.0f;
                            px = 0.5f + 0.5f * std::copysign (std::pow (std::abs (uu), bendP), uu);
                            break;
                        }
                        case warp::pwm:
                            if (px >= pwmW)
                                return 0.0f;
                            px /= pwmW;
                            break;
                        case warp::asymPlus: case warp::asymMinus: case warp::asymBoth:
                            px = px < asymM ? 0.5f * px / asymM : 0.5f + 0.5f * (px - asymM) / (1.0f - asymM);
                            break;
                        case warp::mirror:
                        {
                            const auto y1 = read (ph);
                            const auto y2 = read (toPhase (px < 0.5f ? 2.0f * px : 2.0f - 2.0f * px));
                            return y1 + (y2 - y1) * wa;
                        }
                        case warp::flip:
                        {
                            const auto y = read (ph);
                            return px >= flipAt ? -y : y;
                        }
                        case warp::remap: px = std::clamp (px - wa * 0.15f * std::sin (2.0f * (float) pi * px), 0.0f, 0.9999999f); break;
                        case warp::crush:
                        {
                            const auto y = read (ph);
                            return wa < 1.0e-4f || aaMode != 0 ? y : std::round (y * crushL) / crushL; // (anti-aliased: the steps below)
                        }
                        case warp::fm:
                            px += fmDepth * m;
                            px -= std::floor (px);
                            break;
                        case warp::am: return read (ph) * (1.0f - wa + wa * (0.5f + 0.5f * m));
                        case warp::rm: return read (ph) * (1.0f - wa + wa * m);
                        default: break;
                    }
                    return g * read (toPhase (px));
                    }
                };
                // Classic's PW: the whole cycle squeezed into `width`, the rest holds the cycle's end [MEASURED x2 pw0.5 / pw1 / pw-0.5]
                auto pwPhase = [&] (float px) noexcept {
                    const auto q = pwLate ? std::max (0.0f, px - (1.0f - width)) / width : std::min (px / width, 1.0f);
                    return (std::uint32_t) (std::int64_t) (std::min (q, 0.9999999f) * 4294967296.0f);
                };
                float y;
                switch (mode)
                {
                    case effectFm:
                    {
                        const auto mphase = (float) uv.fmPhase * (1.0f / 4294967296.0f);
                        auto off = fmIdx * std::sin (2.0f * (float) pi * mphase); // (turns; whole turns drop)
                        off -= std::floor (off);
                        y = sample (uv.phase + (std::uint32_t) (std::int64_t) (off * 4294967296.0));
                        uv.fmPhase += fmInc;
                        break;
                    }
                    case effectClassic:
                    {
                        if (syncing)
                        {
                            // the synced wave runs `sync` times faster and restarts with the oscillator; the jump at the
                            // restart is smoothed by a two-sample polyBLEP (this sample now, the next one through `held`)
                            const auto x = (float) uv.phase * (1.0f / 4294967296.0f);
                            const auto sp = uv.syncPhase;
                            y = sample (pwPhase ((float) (sp - std::floor (sp)))) + uv.held;
                            uv.held = 0.0f;
                            if (x + dt >= 1.0f) // the oscillator restarts before the next sample
                            {
                                const auto tau = std::clamp ((1.0f - x) / std::max (dt, 1.0e-9f), 0.0f, 1.0f);
                                const auto end = sp + sync * (1.0 - (double) x);
                                const auto jump = read (0u) - read (pwPhase ((float) (end - std::floor (end))));
                                y += jump * (1.0f - tau) * (1.0f - tau) * 0.5f;
                                uv.held = -jump * tau * tau * 0.5f;
                                uv.syncPhase = sync * std::max (0.0, (double) x + dt - 1.0);
                            }
                            else
                                uv.syncPhase = sp + sync * dt;
                        }
                        else
                            y = sample (pwPhase ((float) uv.phase * (1.0f / 4294967296.0f)));
                        break;
                    }
                    case effectModern:
                    {
                        // Warp bends the read phase (a bilinear curve), Fold runs it 1 + 7 × Fold times faster, reflecting at
                        // the cycle's ends, mirrored about the cycle's middle [MEASURED x3]
                        auto px = (float) uv.phase * (1.0f / 4294967296.0f);
                        if (warpC != 1.0f)
                            px = px / (px + warpC * (1.0f - px));
                        if (folding)
                        {
                            // (below 1/7 the plain fold, which the mirrored one equals at 1/7; Live's small folds are close to
                            // it, 6 … 9 dB apart on W4 x3 fold 0.02 … 0.1; exact from 0.15 up)
                            auto f = foldG < 2.0f ? foldG * px : foldG * std::min (px, 1.0f - px);
                            f -= 2.0f * std::floor (f * 0.5f);
                            px = f <= 1.0f ? f : 2.0f - f;
                        }
                        y = sample ((std::uint32_t) (std::int64_t) (std::min (px, 0.9999999f) * 4294967296.0f));
                        break;
                    }
                    default: y = sample (uv.phase); break;
                }
                if constexpr (warping && aaMode != 0)
                    y = antiAlias<aaMode> (aaMemory, y, wIn, dt, wa, syncK, pwmW, flipAt, crushL, invLen, mode == effectNone, read, toPhase);
                uv.phase += inc;
                outL[i] += y * gl;
                outR[i] += y * gr;
                pos += o.posStep;
            }
            if constexpr (aaMode != 0)
                uv.px = aaMemory.px, uv.wheld = aaMemory.wheld, uv.crushPrev = aaMemory.crushPrev, uv.crushG = aaMemory.crushG;
        }
        };
        using none = std::integral_constant<int, 0>;
        if (w2 == warp::off)
            voicesLoop (std::false_type {}, none {});
        else if (! aa)
            voicesLoop (std::true_type {}, none {});
        else
            switch (w2)
            {
                case warp::sync: voicesLoop (std::true_type {}, std::integral_constant<int, warp::sync> {}); break;
                case warp::pwm: voicesLoop (std::true_type {}, std::integral_constant<int, warp::pwm> {}); break;
                case warp::mirror: voicesLoop (std::true_type {}, std::integral_constant<int, warp::mirror> {}); break;
                case warp::flip: voicesLoop (std::true_type {}, std::integral_constant<int, warp::flip> {}); break;
                default: voicesLoop (std::true_type {}, std::integral_constant<int, warp::crush> {}); break;
            }
        o.pos += o.posStep * (float) n;
    }

    // Helix+ 2's sample oscillator (round 190ml): each unison voice reads the sample (4-point Hermite) from Start, once or
    // looping from Start to the end. Warp 2 on the sample: Sync restarts the read at the note's cycle, 1 + 15 × Amount times
    // faster (a polyBLEP at the restart); FM / AM / RM ← oscillator 1; Crush through its antiderivative; the phase warps
    // of a single cycle (Bend, PWM, Asym, Mirror, Flip, Remap) have no cycle to bend and leave the sample as it is.
    void sampleStep (Voice& v, int n, float* outL, float* outR) noexcept
    {
        if (v.sClean) // (round 3's Clean: the band-limited levels; else 190ml's read, unchanged)
            sampleStepClean (v, n, outL, outR);
        else
            sampleStepT<false> (v, n, outL, outR);
    }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__ ((noinline))
#endif
    void sampleStepClean (Voice& v, int n, float* outL, float* outR) noexcept { sampleStepT<true> (v, n, outL, outR); }
    template <bool clean>
    void sampleStepT (Voice& v, int n, float* outL, float* outR) noexcept
    {
        const auto& smp = *sample;
        const auto frames = smp.frames();
        const float* sl = smp.l.data();
        const float* sr = smp.r.size() == smp.l.size() ? smp.r.data() : sl;
        const bool loop = xp2 (ext2::sampleLoop) > 0.5;
        const auto start = std::clamp (xp2 (ext2::sampleStart), 0.0, 1.0) * (double) std::max (0, frames - 3);
        const auto end = (double) (frames - 3); // (the last position with its four points inside the sample)
        const auto w2 = v.sWarp;
        const auto wa = v.sWarpAmount;
        const bool syncing = w2 == warp::sync && wa > 1.0e-4f;
        const bool xmod = w2 == warp::fm || w2 == warp::am || w2 == warp::rm;
        const auto syncK = 1.0 + 15.0 * wa;
        const auto crushL = std::exp2 (1.0f + 7.0f * (1.0f - wa));
        // oscillator 1's read for FM / AM / RM (its frames at its position, each voice from its phase at the step's start)
        const auto* ot = xmod ? tables[0] : nullptr;
        const float* oa = nullptr;
        const float* ob = nullptr;
        float ow = 0.0f;
        int oShift = 20;
        if (ot != nullptr)
        {
            const auto om = v.mip[0];
            const auto olen = mipLength (om);
            oShift = olen == 4096 ? 20 : olen == 2048 ? 21 : olen == 1024 ? 22 : 23;
            const auto fpos = clamp01 (v.osc[0].pos) * (float) (frameCount - 1);
            const auto f0 = std::min ((int) fpos, frameCount - 2);
            ow = fpos - (float) f0;
            oa = ot->frame (om, f0);
            ob = ot->frame (om, f0 + 1);
        }
        const auto oFracScale = 1.0f / (float) (1u << oShift);
        const auto oFracMask = (1u << oShift) - 1u;
        const auto hermite = [] (const float* d, double pos) noexcept {
            const auto i = (int) pos;
            const auto t = (float) (pos - (double) i);
            const auto xm = d[i > 0 ? i - 1 : 0], x0 = d[i], x1 = d[i + 1], x2 = d[i + 2];
            const auto c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        };
        bool playing = false;
        for (int u = 0; u < v.uCount; ++u)
        {
            auto pos = v.sPos[(std::size_t) u];
            if (pos < 0.0)
                continue;
            const auto inc = v.sInc[(std::size_t) u] * (syncing ? syncK : 1.0);
            const auto gl = v.sGainL[(std::size_t) u], gr = v.sGainR[(std::size_t) u];
            auto& held = v.sHeld[(std::size_t) u];
            auto& sync = v.sSync[(std::size_t) u];
            const auto syncInc = v.sSyncInc[(std::size_t) u];
            auto oph = v.phaseAtStep[0][(std::size_t) u];
            const auto oinc = v.uInc[0][(std::size_t) u];
            // (round 3, Clean: read from the band-limited level the speed asks for; up to 1.2 × the sample's own speed the
            // sample itself, as before. The positions stay in the sample's own samples.)
            const float* rl = sl;
            const float* rr = sr;
            double scale = 1.0;
            bool sinc = false; // (a level: its 8-point read)
            const float* sincK = nullptr;
            if constexpr (clean)
            {
                // round 190rb (crash report b230f196, 0.1.30: sampleStepClean on the audio thread): a voice started with
                // Clean keeps it, but a new sample comes without its levels until the UI thread has made them (null here
                // meanwhile): the voice reads the sample itself until then
                if (const auto lv = levels != nullptr ? std::min (sampleLevelFor (std::abs (inc)), (int) levels->levels.size()) : 0; lv > 0)
                {
                    const auto& lev = levels->levels[(std::size_t) (lv - 1)];
                    rl = lev.l.data() + SampleLevels::guard;
                    rr = sr == sl || lev.r.empty() ? rl : lev.r.data() + SampleLevels::guard;
                    scale = std::ldexp (1.0, -lev.shift);
                    sinc = true;
                    sincK = sincTable().k.data();
                }
            }
            // (Clean's read; without Clean the read below is 190ml's own expression)
            const auto read = [sinc, sincK, &hermite] (const float* d, double at) HELIX_LAMBDA_INLINE {
                return sinc ? sincRead (d, at, sincK) : hermite (d, at);
            };
            (void) read, (void) scale, (void) rl, (void) rr;
            for (int i = 0; i < n; ++i)
            {
                if (pos >= end)
                {
                    if (loop && end - start > 4.0)
                        pos = start + std::fmod (pos - start, end - start);
                    else
                    {
                        pos = -1.0;
                        break;
                    }
                }
                float yl, yr;
                if constexpr (clean)
                    yl = read (rl, pos * scale), yr = rr == rl ? yl : read (rr, pos * scale);
                else
                    yl = hermite (sl, pos), yr = sr == sl ? yl : hermite (sr, pos);
                auto step = inc;
                if (oa != nullptr)
                {
                    const auto idx = oph >> oShift;
                    const auto fr = (float) (oph & oFracMask) * oFracScale;
                    const auto va = oa[idx] + (oa[idx + 1] - oa[idx]) * fr;
                    const auto vb = ob[idx] + (ob[idx + 1] - ob[idx]) * fr;
                    const auto m = va + (vb - va) * ow;
                    oph += oinc;
                    if (w2 == warp::fm)
                        step = inc * std::max (0.0, 1.0 + 2.0 * (double) wa * m);
                    else
                    {
                        const auto g = w2 == warp::am ? 1.0f - wa + wa * (0.5f + 0.5f * m) : 1.0f - wa + wa * m;
                        yl *= g, yr *= g;
                    }
                }
                else if (w2 == warp::crush && wa > 1.0e-4f)
                {
                    // (one antiderivative step on the sides' mean keeps the cost low; the sides keep their difference)
                    const auto F = [crushL] (float x) noexcept {
                        const auto uu = std::abs (x) * crushL;
                        const auto kk = std::floor (uu + 0.5f);
                        return (kk * (kk - 1.0f) * 0.5f + kk * (uu - kk + 0.5f)) / (crushL * crushL);
                    };
                    const auto mid = 0.5f * (yl + yr);
                    auto& prev = v.sCrush[(std::size_t) u];
                    const auto dx = mid - prev;
                    const auto q = std::abs (dx) < 1.0e-5f ? std::round (0.5f * (mid + prev) * crushL) / crushL : (F (mid) - F (prev)) / dx;
                    prev = mid;
                    yl += q - mid, yr += q - mid;
                }
                yl += held, yr += held;
                held = 0.0f;
                if (syncing)
                {
                    // the note's cycle restarts the read: a jump from here to Start, smoothed over two samples
                    if (sync + syncInc < sync)
                    {
                        const auto tau = std::clamp ((float) (4294967296.0 - (double) sync) / (float) std::max (syncInc, 1u), 0.0f, 1.0f);
                        float jump;
                        if constexpr (clean)
                            jump = read (rl, start * scale) - read (rl, std::min (pos + inc * tau, end) * scale);
                        else
                            jump = hermite (sl, start) - hermite (sl, std::min (pos + inc * tau, end));
                        yl += jump * (1.0f - tau) * (1.0f - tau) * 0.5f;
                        yr += jump * (1.0f - tau) * (1.0f - tau) * 0.5f;
                        held = -jump * tau * tau * 0.5f;
                        pos = start + inc * (1.0 - tau) - step;
                    }
                    sync += syncInc;
                }
                outL[i] += yl * gl;
                outR[i] += yr * gr;
                pos += step;
            }
            v.sPos[(std::size_t) u] = pos;
            playing = playing || pos >= 0.0;
        }
        if (! playing)
            v.sampleDone = true, v.sampleOn = false;
    }

    // --- Helix round 3 (round 190mt): the granular oscillator ------------------------------------------------------------
    // Grains of the channel's sample (the sample oscillator's), Density a second, each Size long under a Hann window, from
    // Position ± Spray × half the sample, at the note's speed over the root key + Pitch, on its own side (Pan ± Spread at
    // random). Each grain reads the band-limited level its speed asks for. The randomness is the voice's own (seeded by the
    // voice, no engine random drawn), so a note renders the same every time [MODEL].
    float grainRandom (Voice& v) noexcept // (−1…1)
    {
        auto& x = v.grainSeed;
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        return (float) ((double) (x >> 8) / 8388608.0 - 1.0);
    }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__ ((noinline))
#endif
    void grainControl (Voice& v, double notePitch, const float* mod, const float* level) noexcept
    {
        v.gPos = clamp01 ((float) xp3 (ext3::grainPosition) + mod[tgt::grainPos]);
        // Size and Density rows: ±1 = 4 octaves of their value
        const auto size = std::clamp (xp3 (ext3::grainSize) * std::exp2 (4.0 * mod[tgt::grainSize]), 0.002, 1.0);
        const auto density = std::clamp (xp3 (ext3::grainDensity) * std::exp2 (4.0 * mod[tgt::grainDensity]), 0.5, 400.0);
        v.gSpray = clamp01 ((float) xp3 (ext3::grainSpray) + mod[tgt::grainSpray]);
        v.gSpread = clamp01 ((float) xp3 (ext3::grainSpread));
        v.gPan = clampBi ((float) xp3 (ext3::grainPan));
        const auto semis = notePitch + xp3 (ext3::grainPitch) + laws.oscPitchSemis * mod[tgt::grainPitch] - std::clamp (xp2 (ext2::sampleRoot), 0.0, 127.0);
        v.gInc = std::exp2 (semis / 12.0) * sample->rate / rate;
        v.gInterval = (float) (rate / density);
        v.gWinInc = (float) (1.0 / (size * rate));
        // overlapping grains add up as noise does: the level kept near one grain's
        const auto overlap = density * size;
        v.gGain = clamp01 ((float) xp3 (ext3::grainGain)) * level[tgt::grainGain] * (float) (overlap > 1.0 ? 1.0 / std::sqrt (overlap) : 1.0);
        // Helix round 4 (190oc): unison voices (each spawn makes one grain a voice, detuned ± Detune × 50 cents at the outer
        // voices, spread ± Width around Pan; their sum kept near one voice's level, as uncorrelated grains add up) and the
        // share of grains played backwards. Revision 3 and below: one voice, forwards
        v.gUnison = grainU;
        v.gReverse = grainRev;
        if (v.gUnison > 1)
        {
            v.gDetune = (float) (unisonDetuneCents / 100.0 * clamp01 ((float) xp4 (ext4::grainDetune)));
            v.gWidth = clamp01 ((float) xp4 (ext4::grainWidth));
            // (the grains that sound at once: overlap × voices, at most the voice's room; their sum kept near one grain's)
            const auto sounding = std::clamp (overlap * (double) v.gUnison, 1.0, (double) maxGrainsUnison);
            v.gGain *= (float) (std::sqrt (std::max (overlap, 1.0)) / std::sqrt (sounding));
        }
    }
    void spawnGrain (Voice& v, int start) noexcept
    {
        const auto r1 = grainRandom (v), r2 = grainRandom (v);
        const auto span = (double) (sample->frames() - 4);
        const auto at = std::clamp ((double) v.gPos + 0.5 * (double) v.gSpray * (double) r1, 0.0, 1.0) * span;
        const auto lv = levels != nullptr ? std::min (sampleLevelFor (v.gInc), (int) levels->levels.size()) : 0;
        const auto shift = lv > 0 ? levels->levels[(std::size_t) (lv - 1)].shift : 0;
        auto& g = v.grains[(std::size_t) v.grainCount++];
        g.level = lv;
        g.pos = std::ldexp (at, -shift);
        g.inc = std::ldexp (v.gInc, -shift);
        g.w = 0.0f;
        g.wInc = v.gWinInc;
        g.start = start;
        const auto pp = clampBi (v.gPan + v.gSpread * r2);
        const auto c = (float) laws.centreGain * v.gGain;
        const auto near = c * (1.0f + std::sqrt (std::abs (pp))), far = c * std::sqrt (1.0f - std::abs (pp));
        g.gl = pp > 0.0f ? far : near;
        g.gr = pp > 0.0f ? near : far;
        if (v.gReverse > 0.0f) // (190oc; none drawn without reverse grains, so revision 3 draws the same numbers)
            maybeReverse (v, g);
    }
    // Helix round 4 (190oc): unison voice u of a spawn: as spawnGrain, at its own detune and side
    void spawnUnisonGrain (Voice& v, int start, int u) noexcept
    {
        const auto r1 = grainRandom (v), r2 = grainRandom (v);
        const auto place = (float) oscUnisonSpread (u, v.gUnison);
        const auto inc = v.gInc * std::exp2 ((double) (v.gDetune * place) / 12.0);
        const auto span = (double) (sample->frames() - 4);
        const auto at = std::clamp ((double) v.gPos + 0.5 * (double) v.gSpray * (double) r1, 0.0, 1.0) * span;
        const auto lv = levels != nullptr ? std::min (sampleLevelFor (inc), (int) levels->levels.size()) : 0;
        const auto shift = lv > 0 ? levels->levels[(std::size_t) (lv - 1)].shift : 0;
        auto& g = v.grains[(std::size_t) v.grainCount++];
        g.level = lv;
        g.pos = std::ldexp (at, -shift);
        g.inc = std::ldexp (inc, -shift);
        g.w = 0.0f;
        g.wInc = v.gWinInc;
        g.start = start;
        const auto pp = clampBi (v.gPan + v.gWidth * place + v.gSpread * r2);
        const auto c = (float) laws.centreGain * v.gGain;
        const auto near = c * (1.0f + std::sqrt (std::abs (pp))), far = c * std::sqrt (1.0f - std::abs (pp));
        g.gl = pp > 0.0f ? far : near;
        g.gr = pp > 0.0f ? near : far;
        if (v.gReverse > 0.0f)
            maybeReverse (v, g);
    }
    // a grain played backwards: from the end of the stretch it would have read forwards, back to its start (Reverse 1: every
    // grain; between: that share at random)
    void maybeReverse (Voice& v, Voice::Grain& g) noexcept
    {
        if (v.gReverse < 1.0f && 0.5f * (grainRandom (v) + 1.0f) >= v.gReverse)
            return;
        const auto frames = g.level > 0 ? levels->levels[(std::size_t) (g.level - 1)].frames : sample->frames();
        const auto len = g.inc / (double) g.wInc; // (the level's samples the grain covers)
        g.pos = std::clamp (g.pos + len, 1.0, (double) (frames - 4));
        g.inc = -g.inc;
    }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__ ((noinline))
#endif
    void grainStep (Voice& v, int n, float* outL, float* outR) noexcept
    {
        // the grains due in this step (a quarter of Spray jitters their times too)
        auto t = v.grainNext;
        while (t < (float) n)
        {
            if (v.gUnison <= 1)
            {
                if (v.grainCount < maxGrains)
                    spawnGrain (v, std::max (0, (int) t));
            }
            else // (190oc: one grain a unison voice, while there is room)
                for (int u = 0; u < v.gUnison && v.grainCount < maxGrainsUnison; ++u)
                    spawnUnisonGrain (v, std::max (0, (int) t), u);
            t += v.gInterval * (1.0f + 0.25f * v.gSpray * grainRandom (v));
        }
        v.grainNext = t - (float) n;
        const auto& smp = *sample;
        const float* tab = grainTable().v.data();
        const float* sincK = sincTable().k.data();
        const auto stereo = smp.r.size() == smp.l.size();
        for (int k = 0; k < v.grainCount;)
        {
            auto& g = v.grains[(std::size_t) k];
            const float* gl = smp.l.data();
            const float* gr = stereo ? smp.r.data() : gl;
            auto frames = smp.frames();
            bool alive = g.level <= (levels != nullptr ? (int) levels->levels.size() : 0);
            const bool sinc = alive && g.level > 0; // (a level: its 8-point read; the sample itself: 4-point Hermite)
            if (sinc)
            {
                const auto& lev = levels->levels[(std::size_t) (g.level - 1)];
                gl = lev.l.data() + SampleLevels::guard;
                gr = stereo && ! lev.r.empty() ? lev.r.data() + SampleLevels::guard : gl;
                frames = lev.frames;
            }
            const auto end = (double) (frames - 3);
            // (the grain's values in locals: the output's stores cannot change them)
            auto pos = g.pos, inc = g.inc;
            auto wp = g.w;
            const auto wInc = g.wInc, gainL = g.gl, gainR = g.gr;
            // how many samples it plays in this step: to its window's end or the level's end, at most the step
            int last = alive ? n : g.start;
            if (alive)
            {
                const auto byWindow = (double) (1.0f - wp) / (double) wInc, byEnd = inc > 0.0 ? (end - pos) / inc : (pos - 1.0) / -inc;
                const auto left = std::max (0.0, std::ceil (std::min (byWindow, byEnd)));
                if (left < (double) (n - g.start))
                    last = g.start + (int) left, alive = false;
            }
            const auto read = [sinc, sincK] (const float* d, double at) HELIX_LAMBDA_INLINE {
                if (sinc)
                    return sincRead (d, at, sincK);
                const auto j = (int) at;
                const auto f = (float) (at - (double) j);
                const auto xm = d[j > 0 ? j - 1 : 0], x0 = d[j], x1 = d[j + 1], x2 = d[j + 2];
                const auto c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
                return ((c3 * f + c2) * f + c1) * f + x0;
            };
            if (gr == gl)
                for (int i = g.start; i < last; ++i)
                {
                    const auto y = read (gl, pos) * grainWindowInline (tab, std::min (wp, 0.99999994f));
                    outL[i] += y * gainL;
                    outR[i] += y * gainR;
                    pos += inc;
                    wp += wInc;
                }
            else
                for (int i = g.start; i < last; ++i)
                {
                    const auto win = grainWindowInline (tab, std::min (wp, 0.99999994f));
                    outL[i] += read (gl, pos) * win * gainL;
                    outR[i] += read (gr, pos) * win * gainR;
                    pos += inc;
                    wp += wInc;
                }
            g.pos = pos, g.w = wp;
            g.start = 0;
            if (! alive || g.w >= 1.0f || g.pos >= end || (g.inc < 0.0 && g.pos < 1.0))
            {
                g = v.grains[(std::size_t) --v.grainCount]; // (the last grain takes its place)
                continue;
            }
            ++k;
        }
    }

    // --- Helix round 3 (round 190mt): the spectral oscillator ---------------------------------------------------------------
    // The sample's partials (SpectralData: frames over the sample, tracks per frame) resynthesised as sines: Position moves
    // through the frames, the note moves every partial's frequency over the root key; Stretch bends them around the root
    // (f' = root × (f / root)^(1 + Stretch / 2)), Shift adds Hz to each (breaking the harmonics), the filter tilts them (±6
    // dB an octave around 1 kHz) and keeps a band of them (Band, Width octaves, cosine skirts an octave wide). Partials
    // at or above 0.475 × the rate are dropped (faded from 0.45): anti-aliased by construction [MODEL].
#if defined(__GNUC__) || defined(__clang__)
    __attribute__ ((noinline))
#endif
    void spectralControl (Voice& v, int n, double notePitch, const float* mod, const float* level) noexcept
    {
        const auto& sp = *spectral;
        // Helix round 4 (190oc): unison voices, each its own block of partials (u × stride), detuned ± Detune × 50 cents at
        // the outer voices and spread ± Width around Pan; one voice (revision 3 and below) is 190mt's bank as it was
        const auto U = specU;
        const auto stride = U > 1 ? spectralUnisonStride (U) : maxSpectralPartials;
        if (v.spStarted && U != v.spUnison) // (the voices changed: the bank starts again)
            v.spStarted = false;
        if (! v.spStarted) // (each partial from its own fixed phase: the sum stays smooth, every note starts the same)
        {
            for (int u = 0; u < U; ++u)
                for (int i = 0; i < stride; ++i)
                {
                    auto h = (std::uint32_t) (i + 64 * u) * 2654435761u + 0x9e3779b9u;
                    h ^= h >> 15, h *= 0x2c1b3c6du, h ^= h >> 12;
                    const auto ph = 2.0 * pi * (double) (h >> 8) / 16777216.0;
                    const auto j = (std::size_t) (u * stride + i);
                    v.spC[j] = (float) std::cos (ph);
                    v.spS[j] = (float) std::sin (ph);
                    v.spA[j] = 0.0f;
                    v.spCr[j] = 1.0f, v.spSr[j] = 0.0f;
                }
            v.spStarted = true;
            v.spLive = 0;
            v.spKey.fill (-1.0e300);
            v.spUnison = U;
            v.spStride = stride;
        }
        const auto want = std::min (U > 1 ? std::min (sp.partials, stride) : sp.partials, std::clamp ((int) roundHalfAway (xp3 (ext3::specPartials)), 1, maxSpectralPartials));
        const auto wanted = std::min ((want + 3) & ~3, stride);
        const auto run = std::max (wanted, v.spLive); // (partials dropped since the last step fade out in this one)
        const std::array<double, 9> key { clamp01 ((float) xp3 (ext3::specPosition) + mod[tgt::specPos]),
                                          std::clamp (xp3 (ext3::specStretch) + (double) mod[tgt::specStretch], -1.0, 1.0),
                                          std::clamp (xp3 (ext3::specShift) + (double) mod[tgt::specShift], -1.0, 1.0),
                                          std::clamp (xp3 (ext3::specTilt) + (double) mod[tgt::specTilt], -1.0, 1.0),
                                          filterHz (filterNorm (xp3 (ext3::specBand)) + mod[tgt::specBand]),
                                          std::clamp (xp3 (ext3::specWidth), 0.25, 10.0),
                                          notePitch + xp3 (ext3::specTranspose) - std::clamp (xp2 (ext2::sampleRoot), 0.0, 127.0),
                                          (double) want, rate };
        // the output's gain and side (Live's pan law, as the oscillators')
        const auto pan = clampBi ((float) xp3 (ext3::specPan));
        const auto g = clamp01 ((float) xp3 (ext3::specGain)) * level[tgt::specGain] * (float) laws.centreGain;
        const auto near = g * (1.0f + std::sqrt (std::abs (pan))), far = g * std::sqrt (1.0f - std::abs (pan));
        v.spGainL = pan > 0.0f ? far : near;
        v.spGainR = pan > 0.0f ? near : far;
        auto detune = 0.0;
        auto uWidth = 0.0f;
        std::array<double, 3> key4 {};
        if (U > 1)
        {
            detune = unisonDetuneCents * clamp01 ((float) xp4 (ext4::specDetune));
            uWidth = clamp01 ((float) xp4 (ext4::specWidth));
            key4 = { (double) U, detune, (double) uWidth };
            const auto gu = g * (float) (1.0 / std::sqrt ((double) U)); // (uncorrelated voices: the sum near one voice's level)
            for (int u = 0; u < U; ++u)
            {
                const auto pu = clampBi (pan + uWidth * (float) oscUnisonSpread (u, U));
                const auto nu = gu * (1.0f + std::sqrt (std::abs (pu))), fu = gu * std::sqrt (1.0f - std::abs (pu));
                v.spUL[(std::size_t) u] = pu > 0.0f ? fu : nu;
                v.spUR[(std::size_t) u] = pu > 0.0f ? nu : fu;
            }
        }
        v.spCount = run;
        if (key == v.spKey && (U == 1 || key4 == v.spKey4) && run == wanted) // (nothing moved: the amplitudes sit at their targets)
        {
            std::fill (v.spDA.begin(), v.spDA.begin() + run, 0.0f);
            for (int u = 1; u < U; ++u)
                std::fill (v.spDA.begin() + u * stride, v.spDA.begin() + u * stride + run, 0.0f);
            return;
        }
        v.spKey = key;
        v.spKey4 = key4;
        v.spLive = wanted;
        const auto frames = sp.frames, K = sp.partials;
        const auto fpos = key[0] * (double) (frames - 1);
        const auto f0 = std::min ((int) fpos, std::max (0, frames - 2));
        const auto f1 = std::min (f0 + 1, frames - 1);
        const auto wf = (float) (fpos - (double) f0);
        const auto ratio0 = std::exp2 (key[6] / 12.0);
        const auto rootHz = 440.0 * std::exp2 ((std::clamp (xp2 (ext2::sampleRoot), 0.0, 127.0) - 69.0) / 12.0);
        const auto e = spectralExponent (key[1]);
        const auto add = spectralShiftHz (key[2]);
        const auto tilt = key[3], band = key[4], width = key[5];
        const auto top = 0.475 * rate, fade = 0.45 * rate;
        const auto inv = 1.0f / (float) n;
        for (int u = 0; u < U; ++u)
        {
            const auto ratio = U > 1 ? ratio0 * std::exp2 (detune * oscUnisonSpread (u, U) / 1200.0) : ratio0;
            const auto base = (std::size_t) (u * stride);
            for (int i = 0; i < run; ++i)
            {
                auto target = 0.0f;
                auto w = 0.0;
                if (i < want)
                {
                    const auto a0 = sp.amp[(std::size_t) (f0 * K + i)], a1 = sp.amp[(std::size_t) (f1 * K + i)];
                    const auto h0 = (double) sp.hz[(std::size_t) (f0 * K + i)], h1 = (double) sp.hz[(std::size_t) (f1 * K + i)];
                    const auto a = a0 + (a1 - a0) * wf;
                    // (a track born or ending between the frames keeps its sounding frequency)
                    const auto hz = a0 > 0.0f && a1 > 0.0f ? h0 + (h1 - h0) * (double) wf : a0 > 0.0f ? h0 : h1;
                    auto f = e == 1.0 ? hz * ratio : rootHz * ratio * std::pow (std::max (hz, 1.0) / rootHz, e);
                    f += add;
                    auto gain = 1.0;
                    if (! (f >= 5.0 && f < top))
                        gain = 0.0;
                    else
                    {
                        if (f > fade)
                            gain = (top - f) / (top - fade);
                        if (tilt != 0.0)
                            gain *= std::min (16.0, std::pow (f / 1000.0, tilt));
                        if (width < 9.99)
                        {
                            const auto d = std::abs (std::log2 (f / band)), half = 0.5 * width;
                            gain *= d <= half ? 1.0 : d < half + 1.0 ? 0.5 + 0.5 * std::cos (pi * (d - half)) : 0.0;
                        }
                    }
                    target = (float) (a * gain);
                    if (gain > 0.0)
                        w = 2.0 * pi * f / rate;
                }
                const auto j = base + (std::size_t) i;
                v.spDA[j] = (target - v.spA[j]) * inv;
                if (w > 0.0) // (a silent partial keeps turning at its last speed)
                    v.spCr[j] = (float) std::cos (w), v.spSr[j] = (float) std::sin (w);
            }
        }
    }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__ ((noinline))
#endif
    void spectralStep (Voice& v, int n, float* outL, float* outR) noexcept
    {
        if (v.spUnison <= 1)
        {
            bankStep (v, 0, v.spCount, n, outL, outR, v.spGainL, v.spGainR);
            return;
        }
        for (int u = 0; u < v.spUnison; ++u) // (190oc: each unison voice's block on its own side)
            bankStep (v, u * v.spStride, v.spCount, n, outL, outR, v.spUL[(std::size_t) u], v.spUR[(std::size_t) u]);
    }
    // a block of partials (from `base`, `count` of them, a multiple of 4) into the output at the gains given
    HELIX_INLINE void bankStep (Voice& v, int base, int count, int n, float* outL, float* outR, float gainL, float gainR) noexcept
    {
        // four partials a vector: each turns its (cos, sin) by its step's rotation, its sine weighted by its ramped
        // amplitude into four running sums per sample; the sums added at the end (the same order every time)
        alignas (16) float acc[controlRate * 4] {};
        const auto finish = [&v, base] (int q, __m128 c, __m128 s, __m128 a) HELIX_LAMBDA_INLINE {
            // (the pair's length back to 1, once a step: 1.5 − 0.5 (c² + s²))
            const auto len = _mm_add_ps (_mm_mul_ps (c, c), _mm_mul_ps (s, s));
            const auto fix = _mm_sub_ps (_mm_set1_ps (1.5f), _mm_mul_ps (_mm_set1_ps (0.5f), len));
            _mm_store_ps (v.spC.data() + base + q, _mm_mul_ps (c, fix));
            _mm_store_ps (v.spS.data() + base + q, _mm_mul_ps (s, fix));
            _mm_store_ps (v.spA.data() + base + q, a);
        };
        int q = 0;
        for (; q + 8 <= count; q += 8) // (eight partials at a time: the sums' memory touched half as often)
        {
            auto c0 = _mm_load_ps (v.spC.data() + base + q), s0 = _mm_load_ps (v.spS.data() + base + q), a0 = _mm_load_ps (v.spA.data() + base + q);
            auto c1 = _mm_load_ps (v.spC.data() + base + q + 4), s1 = _mm_load_ps (v.spS.data() + base + q + 4), a1 = _mm_load_ps (v.spA.data() + base + q + 4);
            const auto da0 = _mm_load_ps (v.spDA.data() + base + q), cr0 = _mm_load_ps (v.spCr.data() + base + q), sr0 = _mm_load_ps (v.spSr.data() + base + q);
            const auto da1 = _mm_load_ps (v.spDA.data() + base + q + 4), cr1 = _mm_load_ps (v.spCr.data() + base + q + 4), sr1 = _mm_load_ps (v.spSr.data() + base + q + 4);
            const auto zero = _mm_setzero_ps();
            if (_mm_movemask_ps (_mm_or_ps (_mm_cmpneq_ps (da0, zero), _mm_cmpneq_ps (da1, zero))) == 0)
            {
                // (the amplitudes hold this step: no ramp, fewer registers)
                for (int i = 0; i < n; ++i)
                {
                    const auto sum = _mm_add_ps (_mm_mul_ps (a0, s0), _mm_mul_ps (a1, s1));
                    _mm_store_ps (acc + 4 * i, _mm_add_ps (_mm_load_ps (acc + 4 * i), sum));
                    const auto n0 = _mm_sub_ps (_mm_mul_ps (c0, cr0), _mm_mul_ps (s0, sr0));
                    s0 = _mm_add_ps (_mm_mul_ps (s0, cr0), _mm_mul_ps (c0, sr0));
                    c0 = n0;
                    const auto n1 = _mm_sub_ps (_mm_mul_ps (c1, cr1), _mm_mul_ps (s1, sr1));
                    s1 = _mm_add_ps (_mm_mul_ps (s1, cr1), _mm_mul_ps (c1, sr1));
                    c1 = n1;
                }
                finish (q, c0, s0, a0);
                finish (q + 4, c1, s1, a1);
                continue;
            }
            for (int i = 0; i < n; ++i)
            {
                a0 = _mm_add_ps (a0, da0);
                a1 = _mm_add_ps (a1, da1);
                const auto sum = _mm_add_ps (_mm_mul_ps (a0, s0), _mm_mul_ps (a1, s1));
                _mm_store_ps (acc + 4 * i, _mm_add_ps (_mm_load_ps (acc + 4 * i), sum));
                const auto n0 = _mm_sub_ps (_mm_mul_ps (c0, cr0), _mm_mul_ps (s0, sr0));
                s0 = _mm_add_ps (_mm_mul_ps (s0, cr0), _mm_mul_ps (c0, sr0));
                c0 = n0;
                const auto n1 = _mm_sub_ps (_mm_mul_ps (c1, cr1), _mm_mul_ps (s1, sr1));
                s1 = _mm_add_ps (_mm_mul_ps (s1, cr1), _mm_mul_ps (c1, sr1));
                c1 = n1;
            }
            finish (q, c0, s0, a0);
            finish (q + 4, c1, s1, a1);
        }
        for (; q < count; q += 4)
        {
            auto c = _mm_load_ps (v.spC.data() + base + q), s = _mm_load_ps (v.spS.data() + base + q);
            auto a = _mm_load_ps (v.spA.data() + base + q);
            const auto da = _mm_load_ps (v.spDA.data() + base + q), cr = _mm_load_ps (v.spCr.data() + base + q), sr = _mm_load_ps (v.spSr.data() + base + q);
            for (int i = 0; i < n; ++i)
            {
                a = _mm_add_ps (a, da);
                _mm_store_ps (acc + 4 * i, _mm_add_ps (_mm_load_ps (acc + 4 * i), _mm_mul_ps (a, s)));
                const auto c2 = _mm_sub_ps (_mm_mul_ps (c, cr), _mm_mul_ps (s, sr));
                s = _mm_add_ps (_mm_mul_ps (s, cr), _mm_mul_ps (c, sr));
                c = c2;
            }
            finish (q, c, s, a);
        }
        for (int i = 0; i < n; ++i)
        {
            const auto y = (acc[4 * i] + acc[4 * i + 1]) + (acc[4 * i + 2] + acc[4 * i + 3]);
            outL[i] += y * gainL;
            outR[i] += y * gainR;
        }
    }

    void subStep (Voice& v, int n, float* outL, float* outR) noexcept
    {
        if (v.subGain <= 0.0f)
        {
            v.subPhase += v.subInc * (std::uint32_t) n;
            return;
        }
        // Tone: the sine crossfades into a 40 % pulse, all of it by Tone 0.4 [MEASURED s2, W4 s2: harmonics 2 / 3 / 4 at −10.5 /
        // −13.6 / −12.4 dB and no 5th from 0.4 up]; the pulse's edges are smoothed (polyBLEP)
        const auto mix = std::min (1.0f, v.subTone / 0.4f);
        const auto dt = (float) v.subInc * (1.0f / 4294967296.0f);
        constexpr float duty = 0.4f;
        auto blep = [dt] (float t) noexcept {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0f;
            }
            if (t > 1.0f - dt)
            {
                t = (t - 1.0f) / dt;
                return t * t + t + t + 1.0f;
            }
            return 0.0f;
        };
        for (int i = 0; i < n; ++i)
        {
            const auto ph = (float) v.subPhase * (1.0f / 4294967296.0f);
            auto y = std::sin (2.0f * (float) pi * ph);
            if (mix > 0.0f)
            {
                auto pulse = (ph < duty ? 1.0f : -1.0f) - (2.0f * duty - 1.0f);
                pulse += blep (ph);
                auto q = ph - duty;
                q -= std::floor (q);
                pulse -= blep (q);
                y += (pulse * 0.92f - y) * mix;
            }
            y *= v.subGain;
            outL[i] += y;
            outR[i] += y;
            v.subPhase += v.subInc;
        }
    }

    void render (float* left, float* right, int n, double tempo, double songBeats) noexcept
    {
        if (! fxOn())
        {
            fx.tail = false;
            renderVoices (left, right, n, tempo, songBeats);
            return;
        }
        // Helix+'s effects (round 190mi): the voices into a block of their own, the rack, then added to the output
        for (int pos = 0; pos < n; pos += fxBlock)
        {
            const auto c = std::min (fxBlock, n - pos);
            std::fill (fxL, fxL + c, 0.0f);
            std::fill (fxR, fxR + c, 0.0f);
            renderVoices (fxL, fxR, c, tempo, songBeats >= 0.0 ? songBeats + tempo / 60.0 * pos / rate : -1.0);
            processFx (fxL, fxR, c, tempo);
            for (int i = 0; i < c; ++i)
            {
                left[pos + i] += fxL[i];
                right[pos + i] += fxR[i];
            }
        }
    }

    // --- Helix+ 2's effects (round 190ml) [MODEL: LYNX's own designs from textbook parts]
    struct Fx2
    {
        // the shifter: four second-order all-pass sections on [path A L, A R, path B L, B R] (SIMD: one vector a stage);
        // the paths stay 90° apart over the band with A one sample later
        __m128 x1[4] {}, x2[4] {}, y1[4] {}, y2[4] {};
        float lateL = 0.0f, lateR = 0.0f;
        double phase = 0.0;
        bool shiftWas = false;
        // the EQ: low shelf 120 Hz, a bell at Mid Freq, high shelf 6 kHz (RBJ's biquads), each side's two delays
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        };
        std::array<Biquad, 3> eq {};
        std::array<std::array<float, 4>, 3> eqZ {}; // [band][z1 L, z2 L, z1 R, z2 R]
        std::array<double, 5> eqKey { -1.0, -1.0, -1.0, -1.0, -1.0 };
        bool eqWas = false;
        // the multiband: two low-pass crossovers (120 Hz, 2.5 kHz; the bands sum back to the input), a follower and a gain
        // per band (both sides linked)
        std::array<float, 4> xo1 {}, xo2 {}; // the crossovers' states (ic1, ic2 per side)
        std::array<float, 3> env {}, gain { 1.0f, 1.0f, 1.0f }, gainStep {};
        int count = 0;
        double mbRate = 0.0;
        float xg1 = 0.0f, xg2 = 0.0f;
        bool mbWas = false;
    } fx2;
    float fxShiftMod = 0.0f, fxDepthMod = 0.0f; // (the newest note's modulation of the rack's rows)
    std::uint64_t fxModAge = 0;

    void shifter (float* l, float* r, int n) noexcept
    {
        if (! fx2.shiftWas)
        {
            for (int s = 0; s < 4; ++s)
                fx2.x1[s] = fx2.x2[s] = fx2.y1[s] = fx2.y2[s] = _mm_setzero_ps();
            fx2.lateL = fx2.lateR = 0.0f;
            fx2.shiftWas = true;
        }
        // the two paths' all-pass coefficients (the textbook 90° pair of four sections each, squared)
        constexpr double ca[4] { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
        constexpr double cb[4] { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
        __m128 coef[4];
        for (int i = 0; i < 4; ++i)
            coef[i] = _mm_setr_ps ((float) (ca[i] * ca[i]), (float) (ca[i] * ca[i]), (float) (cb[i] * cb[i]), (float) (cb[i] * cb[i]));
        const auto hz = shiftHz (std::clamp (xp2 (ext2::shift) + (double) fxShiftMod, -1.0, 1.0));
        const auto mix = (float) std::clamp (xp2 (ext2::shiftMix), 0.0, 1.0);
        auto sn = std::sin (2.0 * pi * fx2.phase), cs = std::cos (2.0 * pi * fx2.phase);
        const auto turnS = std::sin (2.0 * pi * hz / rate), turnC = std::cos (2.0 * pi * hz / rate);
        for (int i = 0; i < n; ++i)
        {
            auto v = _mm_setr_ps (l[i], r[i], l[i], r[i]);
            for (int s = 0; s < 4; ++s)
            {
                const auto y = _mm_sub_ps (_mm_mul_ps (coef[s], _mm_add_ps (v, fx2.y2[s])), fx2.x2[s]);
                fx2.x2[s] = fx2.x1[s];
                fx2.x1[s] = v;
                fx2.y2[s] = fx2.y1[s];
                fx2.y1[s] = y;
                v = y;
            }
            alignas (16) float o[4];
            _mm_store_ps (o, v);
            // I = path A one sample later, Q = path B; up: I cos + Q sin
            const auto c = (float) cs, si = (float) sn;
            const auto yl = fx2.lateL * c + o[2] * si, yr = fx2.lateR * c + o[3] * si;
            fx2.lateL = o[0], fx2.lateR = o[1];
            l[i] += (yl - l[i]) * mix;
            r[i] += (yr - r[i]) * mix;
            const auto s2 = sn * turnC + cs * turnS;
            cs = cs * turnC - sn * turnS, sn = s2;
        }
        fx2.phase += hz / rate * n;
        fx2.phase -= std::floor (fx2.phase);
    }

    void equaliser (float* l, float* r, int n) noexcept
    {
        if (! fx2.eqWas)
            fx2.eqZ = {}, fx2.eqWas = true;
        const std::array<double, 5> key { xp2 (ext2::eqLow), xp2 (ext2::eqMidFreq), xp2 (ext2::eqMid), xp2 (ext2::eqHigh), rate };
        if (key != fx2.eqKey)
        {
            fx2.eqKey = key;
            // RBJ's cookbook: shelves with S = 1, the bell with Q 0.9
            const auto make = [this] (int type, double f, double db) {
                const auto A = std::pow (10.0, std::clamp (db, -18.0, 18.0) / 40.0);
                const auto w0 = 2.0 * pi * std::clamp (f, 20.0, rate * 0.45) / rate;
                const auto cw = std::cos (w0), sw = std::sin (w0);
                double b0, b1, b2, a0, a1, a2;
                if (type == 1)
                {
                    const auto alpha = sw / (2.0 * 0.9);
                    b0 = 1.0 + alpha * A, b1 = -2.0 * cw, b2 = 1.0 - alpha * A, a0 = 1.0 + alpha / A, a1 = -2.0 * cw, a2 = 1.0 - alpha / A;
                }
                else
                {
                    const auto alpha = sw / 2.0 * std::sqrt (2.0);
                    const auto sq = 2.0 * std::sqrt (A) * alpha;
                    const auto sgn = type == 0 ? 1.0 : -1.0; // (low shelf / high shelf)
                    b0 = A * ((A + 1.0) - sgn * (A - 1.0) * cw + sq);
                    b1 = sgn * 2.0 * A * ((A - 1.0) - sgn * (A + 1.0) * cw);
                    b2 = A * ((A + 1.0) - sgn * (A - 1.0) * cw - sq);
                    a0 = (A + 1.0) + sgn * (A - 1.0) * cw + sq;
                    a1 = -sgn * 2.0 * ((A - 1.0) + sgn * (A + 1.0) * cw);
                    a2 = (A + 1.0) + sgn * (A - 1.0) * cw - sq;
                }
                return Fx2::Biquad { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
            };
            fx2.eq[0] = make (0, 120.0, key[0]);
            fx2.eq[1] = make (1, key[1], key[2]);
            fx2.eq[2] = make (2, 6000.0, key[3]);
        }
        for (int band = 0; band < 3; ++band)
        {
            if (fx2.eqKey[(std::size_t) (band == 0 ? 0 : band == 1 ? 2 : 3)] == 0.0) // (a band at 0 dB is a straight wire)
                continue;
            const auto& q = fx2.eq[(std::size_t) band];
            auto& z = fx2.eqZ[(std::size_t) band];
            for (int i = 0; i < n; ++i)
            {
                const auto xl = l[i], xr = r[i];
                const auto yl = q.b0 * xl + z[0], yr = q.b0 * xr + z[2];
                z[0] = q.b1 * xl - q.a1 * yl + z[1], z[1] = q.b2 * xl - q.a2 * yl;
                z[2] = q.b1 * xr - q.a1 * yr + z[3], z[3] = q.b2 * xr - q.a2 * yr;
                l[i] = yl, r[i] = yr;
            }
        }
    }

    void multiband (float* l, float* r, int n) noexcept
    {
        auto& m = fx2;
        if (! m.mbWas || m.mbRate != rate)
        {
            m.xo1 = {}, m.xo2 = {}, m.env = {}, m.gain = { 1.0f, 1.0f, 1.0f }, m.gainStep = {}, m.count = 0;
            m.xg1 = std::tan ((float) pi * 120.0f / (float) rate), m.xg2 = std::tan ((float) pi * 2500.0f / (float) rate);
            m.mbRate = rate, m.mbWas = true;
        }
        const auto depth = std::clamp ((float) xp2 (ext2::mbDepth) + fxDepthMod, 0.0f, 1.0f);
        const auto time = std::clamp ((float) xp2 (ext2::mbTime), 0.0f, 1.0f);
        const auto up = std::clamp ((float) xp2 (ext2::mbUp), 0.0f, 1.0f), down = std::clamp ((float) xp2 (ext2::mbDown), 0.0f, 1.0f);
        const auto out = std::pow (10.0f, std::clamp ((float) xp2 (ext2::mbGain), -12.0f, 12.0f) / 20.0f);
        const auto atk = 1.0f - std::exp (-1.0f / ((0.0005f + 0.0195f * time) * (float) rate));
        const auto rel = 1.0f - std::exp (-1.0f / ((0.01f + 0.29f * time) * (float) rate));
        const auto ratioDown = 1.0f + 9.0f * down, ratioUp = 1.0f + 5.0f * up;
        constexpr float k = 1.41421356f; // (Butterworth crossovers)
        const auto lp = [k] (float x, float g, float& ic1, float& ic2) noexcept {
            const auto a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const auto v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1, ic2 = 2.0f * v2 - ic2;
            return v2;
        };
        for (int i = 0; i < n; ++i)
        {
            if (m.count <= 0)
            {
                // every 16 samples: each band's gain from its level (downward above −20 dB, upward below −36 dB down to
                // −80 dB), reached by a straight ramp over the 16
                for (int b = 0; b < 3; ++b)
                {
                    const auto lev = 20.0f * std::log10 (m.env[(std::size_t) b] + 1.0e-9f);
                    auto g = 0.0f;
                    if (lev > -20.0f)
                        g -= (lev + 20.0f) * (1.0f - 1.0f / ratioDown);
                    else if (lev < -36.0f && lev > -80.0f)
                        g += std::min ((-36.0f - lev) * (1.0f - 1.0f / ratioUp), 24.0f) * std::min (1.0f, (lev + 80.0f) / 10.0f);
                    const auto target = 1.0f + depth * (std::pow (10.0f, g / 20.0f) - 1.0f);
                    m.gainStep[(std::size_t) b] = (target - m.gain[(std::size_t) b]) / 16.0f;
                }
                m.count = 16;
            }
            --m.count;
            float band[2][3];
            const float x[2] { l[i], r[i] };
            for (int side = 0; side < 2; ++side)
            {
                const auto low = lp (x[side], m.xg1, m.xo1[(std::size_t) (2 * side)], m.xo1[(std::size_t) (2 * side + 1)]);
                const auto rest = x[side] - low;
                const auto mid = lp (rest, m.xg2, m.xo2[(std::size_t) (2 * side)], m.xo2[(std::size_t) (2 * side + 1)]);
                band[side][0] = low, band[side][1] = mid, band[side][2] = rest - mid;
            }
            float yl = 0.0f, yr = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const auto e = std::max (std::abs (band[0][b]), std::abs (band[1][b]));
                auto& env = m.env[(std::size_t) b];
                env += (e > env ? atk : rel) * (e - env);
                m.gain[(std::size_t) b] += m.gainStep[(std::size_t) b];
                yl += band[0][b] * m.gain[(std::size_t) b];
                yr += band[1][b] * m.gain[(std::size_t) b];
            }
            l[i] = yl * out, r[i] = yr * out;
        }
    }

    void processFx (float* l, float* r, int n, double tempo) noexcept
    {
        bool any = false;
        for (const auto& v : voices)
            any = any || v.active;
        // distortion
        if (xp (ext::distOn) > 0.5)
        {
            const auto mode = std::clamp ((int) roundHalfAway (xp (ext::distMode)), 0, 3);
            const auto drive = (float) std::clamp (xp (ext::distDrive), 0.0, 1.0);
            const auto mix = (float) std::clamp (xp (ext::distMix), 0.0, 1.0);
            const auto gain = std::pow (10.0f, drive * 36.0f / 20.0f);
            const auto makeUp = std::pow (gain, -0.35f);
            const auto levels = std::exp2 (16.0f - 13.0f * drive);
            const auto hold = 1.0f + 15.0f * drive;
            for (int i = 0; i < n; ++i)
                for (int side = 0; side < 2; ++side)
                {
                    auto& x = side == 0 ? l[i] : r[i];
                    float y;
                    switch (mode)
                    {
                        case 0: y = std::tanh (x * gain) * makeUp; break;
                        case 1: y = std::clamp (x * gain, -1.0f, 1.0f) * makeUp; break;
                        case 2: y = std::sin (x * gain * 1.5707963f) * makeUp; break;
                        default:
                        {
                            // crush: fewer levels and a sample held `hold` samples
                            auto& h = side == 0 ? fx.holdL : fx.holdR;
                            if (side == 0)
                            {
                                fx.holdCount += 1.0f;
                                if (fx.holdCount >= hold)
                                    fx.holdCount -= hold;
                            }
                            if (fx.holdCount < 1.0f)
                                h = std::round (x * levels) / levels;
                            y = h;
                            break;
                        }
                    }
                    x += (y - x) * mix;
                }
        }
        // Helix+ 2 (round 190ml): the frequency shifter after the distortion
        if (plus2() && xp2 (ext2::shiftOn) > 0.5)
            shifter (l, r, n);
        // chorus: one modulated tap a side (7 … 13 ms), the sides 90° apart
        if (xp (ext::chorusOn) > 0.5)
        {
            const auto rateHz = std::clamp (xp (ext::chorusRate), 0.05, 5.0);
            const auto depth = std::clamp (xp (ext::chorusDepth), 0.0, 1.0);
            const auto mix = (float) std::clamp (xp (ext::chorusMix), 0.0, 1.0);
            const auto mask = fx.chorusLen - 1;
            auto* bl = fx.chorus.get();
            auto* br = bl + fx.chorusLen;
            // the LFO as a turning phasor (sin and cos: the sides 90° apart), set from the phase every block
            auto sn = std::sin (2.0 * pi * fx.chorusPhase), cs = std::cos (2.0 * pi * fx.chorusPhase);
            const auto turnS = std::sin (2.0 * pi * rateHz / rate), turnC = std::cos (2.0 * pi * rateHz / rate);
            for (int i = 0; i < n; ++i)
            {
                bl[fx.chorusPos] = l[i];
                br[fx.chorusPos] = r[i];
                float taps[2];
                for (int side = 0; side < 2; ++side)
                {
                    const auto d = (0.007 + 0.006 * depth * (0.5 + 0.5 * (side == 0 ? sn : cs))) * rate;
                    const auto at = (double) fx.chorusPos - d;
                    const auto i0 = (int) std::floor (at);
                    const auto fr = (float) (at - i0);
                    const auto* b = side == 0 ? bl : br;
                    const auto a0 = b[i0 & mask], a1 = b[(i0 + 1) & mask];
                    taps[side] = a0 + (a1 - a0) * fr;
                }
                l[i] = l[i] * (1.0f - 0.5f * mix) + taps[0] * 0.5f * mix;
                r[i] = r[i] * (1.0f - 0.5f * mix) + taps[1] * 0.5f * mix;
                fx.chorusPos = (fx.chorusPos + 1) & mask;
                const auto s2 = sn * turnC + cs * turnS;
                cs = cs * turnC - sn * turnS, sn = s2;
            }
            fx.chorusPhase += rateHz / rate * n;
            fx.chorusPhase -= std::floor (fx.chorusPhase);
        }
        // delay: tempo-synced, damped feedback, ping-pong (the sum enters the left line and crosses)
        const bool delayOn = xp (ext::delayOn) > 0.5;
        if (delayOn)
        {
            if (! fx.was[2])
                std::fill (fx.delay.get(), fx.delay.get() + 2 * (std::size_t) fx.delayLen, 0.0f), fx.delayLpL = fx.delayLpR = 0.0f;
            const auto seconds = std::min (2.0, delayBeats ((int) roundHalfAway (xp (ext::delayTime))) * 60.0 / std::max (tempo, 20.0));
            const auto d = std::clamp ((int) std::lround (seconds * rate), 1, fx.delayLen - 1);
            const auto fb = (float) std::clamp (xp (ext::delayFeedback), 0.0, 0.95);
            const auto mix = (float) std::clamp (xp (ext::delayMix), 0.0, 1.0);
            const bool ping = xp (ext::delayPingPong) > 0.5;
            const auto damp = (float) (1.0 - std::exp (-2.0 * pi * 6000.0 / rate));
            auto* bl = fx.delay.get();
            auto* br = bl + fx.delayLen;
            for (int i = 0; i < n; ++i)
            {
                auto rd = fx.delayPos - d;
                if (rd < 0)
                    rd += fx.delayLen;
                const auto tl = bl[rd], tr = br[rd];
                fx.delayLpL += damp * (tl - fx.delayLpL);
                fx.delayLpR += damp * (tr - fx.delayLpR);
                if (ping)
                {
                    bl[fx.delayPos] = 0.5f * (l[i] + r[i]) + fb * fx.delayLpR;
                    br[fx.delayPos] = fb * fx.delayLpL;
                }
                else
                {
                    bl[fx.delayPos] = l[i] + fb * fx.delayLpL;
                    br[fx.delayPos] = r[i] + fb * fx.delayLpR;
                }
                l[i] += mix * tl;
                r[i] += mix * tr;
                if (++fx.delayPos >= fx.delayLen)
                    fx.delayPos = 0;
            }
        }
        // reverb: eight damped delay lines mixed by a Hadamard matrix (LYNX's own small FDN)
        const bool reverbOn = xp (ext::reverbOn) > 0.5;
        if (reverbOn)
        {
            if (! fx.was[3])
                std::fill (fx.reverb.get(), fx.reverb.get() + (std::size_t) fx.rvMax, 0.0f), fx.rvLp = {};
            static constexpr int primes[8] { 1031, 1327, 1523, 1787, 1999, 2239, 2477, 2741 };
            const auto size = std::clamp (xp (ext::reverbSize), 0.0, 1.0);
            const auto scale = (0.4 + 1.2 * size) * rate / 48000.0;
            const auto rt60 = 0.3 + 7.7 * size * size;
            const auto damp = (float) (1.0 - 0.85 * std::clamp (xp (ext::reverbDamp), 0.0, 1.0));
            const auto mix = (float) std::clamp (xp (ext::reverbMix), 0.0, 1.0);
            std::array<float, 8> gains {};
            for (int j = 0; j < 8; ++j)
            {
                fx.rvLen[(std::size_t) j] = std::max (8, (int) (primes[j] * scale));
                gains[(std::size_t) j] = (float) std::pow (10.0, -3.0 * fx.rvLen[(std::size_t) j] / (rate * rt60));
                if (fx.rvPos[(std::size_t) j] >= fx.rvLen[(std::size_t) j])
                    fx.rvPos[(std::size_t) j] = 0;
            }
            auto* buf = fx.reverb.get();
            for (int i = 0; i < n; ++i)
            {
                std::array<float, 8> o {};
                for (int j = 0; j < 8; ++j)
                {
                    auto& lp = fx.rvLp[(std::size_t) j];
                    lp += damp * (buf[fx.rvOffset[(std::size_t) j] + fx.rvPos[(std::size_t) j]] - lp);
                    o[(std::size_t) j] = lp * gains[(std::size_t) j];
                }
                // Hadamard 8 (normalised): three butterfly stages
                for (int h = 1; h < 8; h *= 2)
                    for (int j = 0; j < 8; j += 2 * h)
                        for (int q = j; q < j + h; ++q)
                        {
                            const auto a = o[(std::size_t) q], b = o[(std::size_t) (q + h)];
                            o[(std::size_t) q] = a + b;
                            o[(std::size_t) (q + h)] = a - b;
                        }
                float wl = 0.0f, wr = 0.0f;
                for (int j = 0; j < 8; ++j)
                {
                    const auto mixed = o[(std::size_t) j] * 0.35355339f;
                    const auto in = (j & 1) ? r[i] : l[i];
                    buf[fx.rvOffset[(std::size_t) j] + fx.rvPos[(std::size_t) j]] = mixed + 0.5f * in;
                    (j & 1 ? wr : wl) += buf[fx.rvOffset[(std::size_t) j] + fx.rvPos[(std::size_t) j]];
                    if (++fx.rvPos[(std::size_t) j] >= fx.rvLen[(std::size_t) j])
                        fx.rvPos[(std::size_t) j] = 0;
                }
                l[i] = l[i] * (1.0f - 0.5f * mix) + 0.3f * wl * mix;
                r[i] = r[i] * (1.0f - 0.5f * mix) + 0.3f * wr * mix;
            }
        }
        // Helix+ 2: the EQ and the multiband compressor close the rack
        if (plus2() && xp2 (ext2::eqOn) > 0.5)
            equaliser (l, r, n);
        else
            fx2.eqWas = false;
        if (plus2() && xp2 (ext2::mbOn) > 0.5)
            multiband (l, r, n);
        else
            fx2.mbWas = false;
        if (! (plus2() && xp2 (ext2::shiftOn) > 0.5))
            fx2.shiftWas = false;
        fx.was = { xp (ext::distOn) > 0.5, xp (ext::chorusOn) > 0.5, delayOn, reverbOn };
        // the tail: the rack keeps playing after the last voice until it falls silent (−120 dB for 0.3 s)
        auto peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
        if (any || peak > 1.0e-6f)
            fx.quiet = 0.0, fx.tail = any || peak > 1.0e-6f;
        else if ((fx.quiet += n) > 0.3 * rate)
            fx.tail = false;
    }

    // the routing of renderVoices with Helix+'s filter types (each filter by its own type)
    void renderExtFilters (Voice& v, int c, int route, const float* o1L, const float* o1R, const float* o2L, const float* o2R, float* left,
                           float* right, float vol) noexcept
    {
        float* combs[2] { combLine (v, 0), combLine (v, 1) };
        FilterState* states[2] { &v.f1, &v.f2 };
        const auto run = [&] (int k, float& l, float& r) noexcept {
            const auto& fc = v.fc[(std::size_t) k];
            auto& st = *states[k];
            if (fc.xtype == xfilter::live)
            {
                l = filterSample (st.l, l, fc), r = filterSample (st.r, r, fc);
                return;
            }
            l = filterExt (st, false, l, fc, combs[k]);
            r = filterExt (st, true, r, fc, combs[k] + combLength);
            ++st.combPos;
        };
        const auto& c1 = v.fc[0];
        const auto& c2 = v.fc[1];
        for (int i = 0; i < c; ++i)
        {
            float l = o1L[i], r = o1R[i];
            if (route == routeSerial)
            {
                if (c1.on)
                    run (0, l, r);
                if (c2.on)
                    run (1, l, r);
            }
            else if (route == routeParallel)
            {
                if (c1.on && c2.on)
                {
                    auto l2 = l, r2 = r;
                    run (0, l, r);
                    run (1, l2, r2);
                    l += l2, r += r2;
                }
                else if (c1.on)
                    run (0, l, r);
                else if (c2.on)
                    run (1, l, r);
            }
            else
            {
                if (c1.on)
                    run (0, l, r);
                auto l2 = o2L[i], r2 = o2R[i];
                if (c2.on)
                    run (1, l2, r2);
                l += l2, r += r2;
            }
            v.amp += v.ampStep;
            left[i] += l * v.amp * vol;
            right[i] += r * v.amp * vol;
        }
    }

    void renderVoices (float* left, float* right, int n, double tempo, double songBeats) noexcept
    {
        for (int k = 0; k < 2; ++k)
        {
            const auto b = lfoBase (k);
            const auto hz = p[(std::size_t) (b + lfo::sync)] > 0.5 ? tempo / 60.0 / lfoSyncBeats (ip (b + lfo::syncedRate))
                                                                   : std::clamp (p[(std::size_t) (b + lfo::rate)], 0.01, 30.0);
            lfoFree[(std::size_t) k] += hz * n / rate;
            lfoFree[(std::size_t) k] -= std::floor (lfoFree[(std::size_t) k]);
        }
        const auto route = std::clamp (ip (routing), 0, 2);
        const auto vol = (float) std::clamp (p[(std::size_t) volume], 0.0, 1.0); // (linear [MEASURED a1])
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            int pos = 0;
            while (pos < n)
            {
                if (v.counter <= 0)
                {
                    const auto beat = songBeats >= 0.0 ? songBeats + tempo / 60.0 * pos / rate : -1.0;
                    control (v, controlRate, tempo, beat);
                    v.counter = controlRate;
                }
                const auto c = std::min (v.counter, n - pos);
                float o1L[controlRate] {}, o1R[controlRate] {}, o2L[controlRate] {}, o2R[controlRate] {};
                if (v.plus && (v.warpMode[0] >= warp::fm || v.warpMode[1] >= warp::fm || v.warpMode[2] >= warp::fm
                               || (v.sampleOn && v.sWarp >= warp::fm))) // (FM / AM / RM read the other's phases)
                    for (int k = 0; k < v.oscs; ++k)
                        for (int u = 0; u < v.uCount; ++u)
                            v.phaseAtStep[(std::size_t) k][(std::size_t) u] = v.osc[(std::size_t) k].u[(std::size_t) u].phase;
                oscStep (v, 0, c, o1L, o1R);
                if (route == routeSplit)
                    oscStep (v, 1, c, o2L, o2R);
                else
                    oscStep (v, 1, c, o1L, o1R);
                if (v.oscs > 2) // (Helix+ 2: oscillator 3 and the sample join oscillator 1's bus)
                    osc3Step (v, c, o1L, o1R);
                if (v.plus2) // (Helix+ 2's sample; round 3's grains and partials join oscillator 1's bus too)
                {
                    if (v.sampleOn)
                        sampleStep (v, c, o1L, o1R);
                    if (v.plus3)
                    {
                        if (v.grainOn)
                            grainStep (v, c, o1L, o1R);
                        if (v.specOn)
                            spectralStep (v, c, o1L, o1R);
                    }
                }
                subStep (v, c, o1L, o1R);
                noiseStep (v, c, o1L, o1R);
                const auto& c1 = v.fc[0];
                const auto& c2 = v.fc[1];
                if (v.plus && (c1.xtype != xfilter::live || c2.xtype != xfilter::live))
                {
                    renderExtFilters (v, c, route, o1L, o1R, o2L, o2R, left + pos, right + pos, vol);
                    pos += c;
                    v.counter -= c;
                    continue;
                }
                for (int i = 0; i < c; ++i)
                {
                    float l, r;
                    if (route == routeSerial)
                    {
                        l = o1L[i], r = o1R[i];
                        if (c1.on)
                            l = filterSample (v.f1.l, l, c1), r = filterSample (v.f1.r, r, c1);
                        if (c2.on)
                            l = filterSample (v.f2.l, l, c2), r = filterSample (v.f2.r, r, c2);
                    }
                    else if (route == routeParallel)
                    {
                        const auto xl = o1L[i], xr = o1R[i];
                        if (c1.on && c2.on)
                            l = filterSample (v.f1.l, xl, c1) + filterSample (v.f2.l, xl, c2), r = filterSample (v.f1.r, xr, c1) + filterSample (v.f2.r, xr, c2);
                        else if (c1.on)
                            l = filterSample (v.f1.l, xl, c1), r = filterSample (v.f1.r, xr, c1);
                        else if (c2.on)
                            l = filterSample (v.f2.l, xl, c2), r = filterSample (v.f2.r, xr, c2);
                        else
                            l = xl, r = xr;
                    }
                    else
                    {
                        // Split: oscillator 1 and the sub through filter 1, oscillator 2 through filter 2 [MEASURED r2*]
                        l = c1.on ? filterSample (v.f1.l, o1L[i], c1) : o1L[i];
                        r = c1.on ? filterSample (v.f1.r, o1R[i], c1) : o1R[i];
                        l += c2.on ? filterSample (v.f2.l, o2L[i], c2) : o2L[i];
                        r += c2.on ? filterSample (v.f2.r, o2R[i], c2) : o2R[i];
                    }
                    v.amp += v.ampStep;
                    left[pos + i] += l * v.amp * vol;
                    right[pos + i] += r * v.amp * vol;
                }
                pos += c;
                v.counter -= c;
            }
            if (v.env[0].stage == offStage)
                v.active = false;
        }
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate)
{
    impl->rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    impl->prepareFx();
    reset();
}

double Engine::sampleRate() const noexcept { return impl->rate; }

void Engine::setState (const double* values)
{
    for (int k = 0; k < oscCount; ++k)
        (void) table ((int) std::lround (values[oscTableIndex (k)])); // (builds it here, off the audio thread)
    setValues (values);
}

void Engine::setUserTable (int osc, const Table* t) noexcept
{
    if (osc < 0 || osc >= oscCount)
        return;
    impl->user[(std::size_t) osc] = t;
    impl->takeTables();
}

void Engine::setSample (const SampleBuffer* s) noexcept { impl->sample = s; }
void Engine::setSampleLevels (const SampleLevels* l) noexcept { impl->levels = l; }
void Engine::setSpectral (const SpectralData* s) noexcept { impl->spectral = s; }

void Engine::setValues (const double* values) noexcept
{
    for (int i = 0; i < stateSize; ++i)
        impl->p[(std::size_t) i] = std::isfinite (values[i]) ? values[i] : defaults()[(std::size_t) i];
    impl->refresh();
    impl->takeTables();
}

void Engine::setParam (int index, double value) noexcept
{
    if (! automatable (index) || ! std::isfinite (value))
        return;
    impl->p[(std::size_t) index] = value;
    if (index >= ampEnv && index < lfoBase (0))
        impl->refresh();
    else if (index >= ext2Base) // (round 3: what plays, worked out again)
        impl->refreshRound3();
}

double Engine::param (int index) const noexcept { return index >= 0 && index < stateSize ? impl->p[(std::size_t) index] : 0.0; }

void Engine::note (int key, float velocity, float pitch, float slide, float pressure) noexcept
{
    impl->note (key, std::clamp (velocity, 0.0f, 1.0f), std::isfinite (pitch) ? pitch : 0.0f, slide, std::clamp (pressure, 0.0f, 1.0f));
}

void Engine::slideTo (float semitones, float samples) noexcept
{
    if (! std::isfinite (semitones) || ! std::isfinite (samples))
        return;
    for (auto& v : impl->voices)
        if (v.active && v.gate)
            v.host.start ((semitones - (float) v.key) * 100.0f, std::max (samples, 1.0f));
}

void Engine::notePitch (int key, float semitones) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate && v.key == key)
            v.pitch = std::isfinite (semitones) ? semitones : 0.0f;
}
void Engine::noteSlide (int key, float slide) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate && v.key == key)
            v.slide = clamp01 (slide);
}
void Engine::notePressure (int key, float pressure) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate && v.key == key)
            v.pressure = clamp01 (pressure);
}
void Engine::setPitchOffset (float semitones) noexcept { impl->pitchOffset = std::isfinite (semitones) ? semitones : 0.0f; }
void Engine::setPitchBend (float semitones, float range) noexcept
{
    impl->bend = std::isfinite (semitones) ? semitones : 0.0f;
    impl->bendSource = clampBi (range > 0.0f ? impl->bend / range : 0.0f);
}
void Engine::setModWheel (float value) noexcept { impl->modWheel = clamp01 (value); }
void Engine::setPressure (float value) noexcept { impl->chanPressure = clamp01 (value); }

void Engine::allNotesOff() noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate)
            impl->note (v.key, 0.0f, 0.0f, -1.0f, 0.0f);
}

void Engine::reset() noexcept
{
    for (auto& v : impl->voices)
        v.active = false;
    impl->lastNote = -1.0f;
    impl->fx.tail = false;
    impl->fx.was = {}; // (the effects' lines are cleared when they play next)
    impl->fx2.shiftWas = impl->fx2.eqWas = impl->fx2.mbWas = false;
}

void Engine::render (float* left, float* right, int n, double tempo, double songBeats) noexcept
{
    if (n > 0)
        impl->render (left, right, n, tempo > 0.0 ? tempo : 120.0, songBeats);
}

bool Engine::idle() const noexcept { return activeVoices() == 0 && ! impl->fx.tail; }

int Engine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : impl->voices)
        n += v.active ? 1 : 0;
    return n;
}

void Engine::seed (std::uint32_t s) noexcept { impl->rng = s != 0 ? s : 1u; }

std::vector<float> renderReference (const std::vector<double>& state, const std::vector<RefNote>& notes, double sampleRate, int length,
                                    int block, double bpm, const HelixUserData* data)
{
    Engine e;
    e.prepare (sampleRate);
    std::vector<double> s (defaults().begin(), defaults().end());
    for (std::size_t i = 0; i < s.size() && i < state.size(); ++i)
        s[i] = state[i];
    std::array<std::shared_ptr<const Table>, oscCount> own {}; // (Helix+ 2: the user tables, held while it renders)
    if (data != nullptr)
    {
        for (int k = 0; k < oscCount; ++k)
            if (! data->tables[(std::size_t) k].empty())
                own[(std::size_t) k] = userTableBuilt (data->tables[(std::size_t) k]);
        for (int k = 0; k < oscCount; ++k)
            e.setUserTable (k, own[(std::size_t) k].get());
        e.setSample (data->sample.frames() > 0 ? &data->sample : nullptr);
    }
    // (Helix round 3: the sample's levels and partials, made only when the state's round 3 block asks for them)
    std::shared_ptr<const SampleLevels> levels;
    SpectralData partials;
    if (data != nullptr && data->sample.frames() > 3 && s[(std::size_t) extBase] >= 2.5)
    {
        const auto x3 = [&s] (int i) { return s[(std::size_t) (ext3Base + i)] > 0.5; };
        if ((x3 (ext3::sampleClean) && s[(std::size_t) (ext2Base + ext2::sampleOn)] > 0.5) || x3 (ext3::grainOn))
            levels = sampleLevels (data->sample);
        if (x3 (ext3::specOn))
            partials = data->spectral.empty() ? analyseSpectrum (data->sample) : data->spectral;
    }
    else if (data != nullptr)
        partials = data->spectral;
    e.setSampleLevels (levels.get());
    e.setSpectral (partials.empty() ? nullptr : &partials);
    e.setState (s.data());
    std::vector<float> out ((std::size_t) length * 2, 0.0f);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    for (int pos = 0; pos < length; pos += block)
    {
        const auto n = std::min (block, length - pos);
        for (const auto& nt : notes)
        {
            if (nt.start >= pos && nt.start < pos + n)
                e.note (nt.key, nt.velocity);
            if (nt.release >= pos && nt.release < pos + n)
                e.note (nt.key, 0.0f);
        }
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), n, bpm, (double) pos / sampleRate * bpm / 60.0);
        for (int i = 0; i < n; ++i)
        {
            out[(std::size_t) (pos + i) * 2] = l[(std::size_t) i];
            out[(std::size_t) (pos + i) * 2 + 1] = r[(std::size_t) i];
        }
    }
    return out;
}
} // namespace wt
} // namespace lynx

// =====================================================================================================================
// parameters, texts, the channel's values and the instrument
namespace lynx
{
namespace wt
{
namespace
{
struct Spec
{
    std::string name;
    float low, high;
    bool stepped;
};

void envSpecs (std::vector<Spec>& s, const std::string& who, bool values)
{
    s.push_back ({ who + " Attack", 0.0f, 20.0f, false });
    s.push_back ({ who + " Decay", 0.0015f, 20.0f, false });
    s.push_back ({ who + " Release", 0.0015f, 20.0f, false });
    s.push_back ({ who + " Attack Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Decay Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Release Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Sustain", 0.0f, 1.0f, false });
    s.push_back ({ who + " Loop Mode", 0.0f, 2.0f, true });
    if (values)
    {
        s.push_back ({ who + " Initial", 0.0f, 1.0f, false });
        s.push_back ({ who + " Peak", 0.0f, 1.0f, false });
        s.push_back ({ who + " Final", 0.0f, 1.0f, false });
    }
}

const std::vector<Spec>& specs()
{
    static const auto list = [] {
        std::vector<Spec> s;
        for (int k = 0; k < 2; ++k)
        {
            const auto o = "Osc " + std::to_string (k + 1);
            s.push_back ({ o + " On", 0.0f, 1.0f, true });
            s.push_back ({ o + " Transpose", -24.0f, 24.0f, true });
            s.push_back ({ o + " Detune", -0.5f, 0.5f, false });
            s.push_back ({ o + " Pos", 0.0f, 1.0f, false });
            s.push_back ({ o + " Effect Mode", 0.0f, 3.0f, true });
            s.push_back ({ o + " Effect 1", -1.0f, 1.0f, false });
            s.push_back ({ o + " Effect 2", 0.0f, 1.0f, false });
            s.push_back ({ o + " Pan", -1.0f, 1.0f, false });
            s.push_back ({ o + " Gain", 0.0f, 1.0f, false });
        }
        s.push_back ({ "Sub On", 0.0f, 1.0f, true });
        s.push_back ({ "Sub Tone", 0.0f, 1.0f, false });
        s.push_back ({ "Sub Gain", 0.0f, 1.0f, false });
        s.push_back ({ "Sub Octave", 0.0f, 2.0f, true });
        for (int k = 0; k < 2; ++k)
        {
            const auto f = "Filter " + std::to_string (k + 1);
            s.push_back ({ f + " On", 0.0f, 1.0f, true });
            s.push_back ({ f + " Type", 0.0f, 4.0f, true });
            s.push_back ({ f + " Circuit", 0.0f, 4.0f, true });
            s.push_back ({ f + " Circuit BP", 0.0f, 1.0f, true });
            s.push_back ({ f + " Slope", 0.0f, 1.0f, true });
            s.push_back ({ f + " Freq", 20.0f, 20480.0f, false });
            s.push_back ({ f + " Res", 0.0f, 1.25f, false });
            s.push_back ({ f + " Drive", 0.0f, 24.0f, false });
            s.push_back ({ f + " Morph", 0.0f, 1.0f, false });
        }
        s.push_back ({ "Filter Routing", 0.0f, 2.0f, true });
        envSpecs (s, "Amp", false);
        envSpecs (s, "Env 2", true);
        envSpecs (s, "Env 3", true);
        for (int k = 0; k < 2; ++k)
        {
            const auto l = "LFO " + std::to_string (k + 1);
            s.push_back ({ l + " Retrigger", 0.0f, 1.0f, true });
            s.push_back ({ l + " Shape", 0.0f, 4.0f, true });
            s.push_back ({ l + " Amount", 0.0f, 1.0f, false });
            s.push_back ({ l + " Shaping", -1.0f, 1.0f, false });
            s.push_back ({ l + " Phase", 0.0f, 360.0f, false });
            s.push_back ({ l + " Sync", 0.0f, 1.0f, true });
            s.push_back ({ l + " Rate", 0.01f, 30.0f, false });
            s.push_back ({ l + " Sync Rate", 0.0f, 21.0f, true });
            s.push_back ({ l + " Attack", 0.0f, 20.0f, false });
        }
        s.push_back ({ "Time", -1.0f, 1.0f, false });
        s.push_back ({ "Global Mod Amount", 0.0f, 2.0f, false });
        s.push_back ({ "Unison Mode", 0.0f, 6.0f, true });
        s.push_back ({ "Unison Voices", 2.0f, 8.0f, true });
        s.push_back ({ "Unison Amount", 0.0f, 1.0f, false });
        s.push_back ({ "Transpose", -48.0f, 48.0f, true });
        s.push_back ({ "Glide", 0.0f, 20.0f, false });
        s.push_back ({ "Volume", 0.0f, 1.0f, false });
        s.push_back ({ "Poly", 0.0f, 1.0f, true });
        s.push_back ({ "Voices", 0.0f, 6.0f, true });
        s.push_back ({ "HiQ", 0.0f, 1.0f, true });
        return s;
    }();
    return list;
}

// Helix+'s automatable values (round 190mi), ext::revision + 1 … ext::autoEnd − 1, in order
const std::vector<Spec>& extSpecs()
{
    static const auto list = [] {
        std::vector<Spec> s;
        for (int k = 0; k < 2; ++k)
        {
            const auto o = "Osc " + std::to_string (k + 1);
            s.push_back ({ o + " Warp 2", 0.0f, (float) (warp::count - 1), true });
            s.push_back ({ o + " Warp 2 Amount", 0.0f, 1.0f, false });
        }
        s.push_back ({ "Noise On", 0.0f, 1.0f, true });
        s.push_back ({ "Noise Level", 0.0f, 1.0f, false });
        s.push_back ({ "Noise Color", 0.0f, 1.0f, false });
        s.push_back ({ "Unison Count", 0.0f, 16.0f, true });
        s.push_back ({ "Unison Blend", 0.0f, 1.0f, false });
        s.push_back ({ "Unison Width", 0.0f, 1.0f, false });
        s.push_back ({ "Unison Stack", 0.0f, 6.0f, true });
        s.push_back ({ "Filter 1 Type+", 0.0f, (float) (xfilter::count - 1), true });
        s.push_back ({ "Filter 2 Type+", 0.0f, (float) (xfilter::count - 1), true });
        for (int j = 0; j < plusMacroCount; ++j)
            s.push_back ({ "Macro " + std::to_string (j + 1), 0.0f, 1.0f, false });
        s.push_back ({ "LFO 1 Draw", 0.0f, 1.0f, true });
        s.push_back ({ "LFO 2 Draw", 0.0f, 1.0f, true });
        s.push_back ({ "Distortion On", 0.0f, 1.0f, true });
        s.push_back ({ "Distortion Mode", 0.0f, 3.0f, true });
        s.push_back ({ "Distortion Drive", 0.0f, 1.0f, false });
        s.push_back ({ "Distortion Mix", 0.0f, 1.0f, false });
        s.push_back ({ "Chorus On", 0.0f, 1.0f, true });
        s.push_back ({ "Chorus Rate", 0.05f, 5.0f, false });
        s.push_back ({ "Chorus Depth", 0.0f, 1.0f, false });
        s.push_back ({ "Chorus Mix", 0.0f, 1.0f, false });
        s.push_back ({ "Delay On", 0.0f, 1.0f, true });
        s.push_back ({ "Delay Time", 0.0f, 9.0f, true });
        s.push_back ({ "Delay Feedback", 0.0f, 0.95f, false });
        s.push_back ({ "Delay Ping-Pong", 0.0f, 1.0f, true });
        s.push_back ({ "Delay Mix", 0.0f, 1.0f, false });
        s.push_back ({ "Reverb On", 0.0f, 1.0f, true });
        s.push_back ({ "Reverb Size", 0.0f, 1.0f, false });
        s.push_back ({ "Reverb Damp", 0.0f, 1.0f, false });
        s.push_back ({ "Reverb Mix", 0.0f, 1.0f, false });
        return s;
    }();
    return list;
}

// Helix+ 2's automatable values (round 190ml), ext2::reserved + 1 … ext2::autoEnd − 1, in order
const std::vector<Spec>& ext2Specs()
{
    static const auto list = [] {
        std::vector<Spec> s;
        s.push_back ({ "Osc 3 On", 0.0f, 1.0f, true });
        s.push_back ({ "Osc 3 Transpose", -24.0f, 24.0f, true });
        s.push_back ({ "Osc 3 Detune", -0.5f, 0.5f, false });
        s.push_back ({ "Osc 3 Pos", 0.0f, 1.0f, false });
        s.push_back ({ "Osc 3 Effect Mode", 0.0f, 3.0f, true });
        s.push_back ({ "Osc 3 Effect 1", -1.0f, 1.0f, false });
        s.push_back ({ "Osc 3 Effect 2", 0.0f, 1.0f, false });
        s.push_back ({ "Osc 3 Pan", -1.0f, 1.0f, false });
        s.push_back ({ "Osc 3 Gain", 0.0f, 1.0f, false });
        s.push_back ({ "Osc 3 Warp 2", 0.0f, (float) (warp::count - 1), true });
        s.push_back ({ "Osc 3 Warp 2 Amount", 0.0f, 1.0f, false });
        s.push_back ({ "Sample On", 0.0f, 1.0f, true });
        s.push_back ({ "Sample Transpose", -48.0f, 48.0f, true });
        s.push_back ({ "Sample Detune", -0.5f, 0.5f, false });
        s.push_back ({ "Sample Gain", 0.0f, 1.0f, false });
        s.push_back ({ "Sample Pan", -1.0f, 1.0f, false });
        s.push_back ({ "Sample Start", 0.0f, 1.0f, false });
        s.push_back ({ "Sample Loop", 0.0f, 1.0f, true });
        s.push_back ({ "Sample Warp 2", 0.0f, (float) (warp::count - 1), true });
        s.push_back ({ "Sample Warp 2 Amount", 0.0f, 1.0f, false });
        for (int j = plusMacroCount; j < macroCount; ++j)
            s.push_back ({ "Macro " + std::to_string (j + 1), 0.0f, 1.0f, false });
        for (int k = 0; k < chaosCount; ++k)
        {
            const auto c = "Chaos " + std::to_string (k + 1);
            s.push_back ({ c + " Mode", 0.0f, 2.0f, true });
            s.push_back ({ c + " Rate", 0.01f, 30.0f, false });
            s.push_back ({ c + " Amount", 0.0f, 1.0f, false });
        }
        s.push_back ({ "Shift On", 0.0f, 1.0f, true });
        s.push_back ({ "Shift", -1.0f, 1.0f, false });
        s.push_back ({ "Shift Mix", 0.0f, 1.0f, false });
        s.push_back ({ "EQ On", 0.0f, 1.0f, true });
        s.push_back ({ "EQ Low", -18.0f, 18.0f, false });
        s.push_back ({ "EQ Mid Freq", 100.0f, 10000.0f, false });
        s.push_back ({ "EQ Mid", -18.0f, 18.0f, false });
        s.push_back ({ "EQ High", -18.0f, 18.0f, false });
        s.push_back ({ "Multiband On", 0.0f, 1.0f, true });
        s.push_back ({ "Multiband Depth", 0.0f, 1.0f, false });
        s.push_back ({ "Multiband Time", 0.0f, 1.0f, false });
        s.push_back ({ "Multiband Up", 0.0f, 1.0f, false });
        s.push_back ({ "Multiband Down", 0.0f, 1.0f, false });
        s.push_back ({ "Multiband Gain", -12.0f, 12.0f, false });
        return s;
    }();
    return list;
}

// Helix round 4's automatable values (round 190oc), ext4::reserved + 1 … ext4::autoEnd − 1, in order
const std::vector<Spec>& ext4Specs()
{
    static const std::vector<Spec> list {
        { "Grain Unison", 1.0f, (float) maxOscUnison, true },
        { "Grain Unison Detune", 0.0f, 1.0f, false },
        { "Grain Unison Width", 0.0f, 1.0f, false },
        { "Grain Reverse", 0.0f, 1.0f, false },
        { "Spectral Unison", 1.0f, (float) maxOscUnison, true },
        { "Spectral Unison Detune", 0.0f, 1.0f, false },
        { "Spectral Unison Width", 0.0f, 1.0f, false },
    };
    return list;
}

// Helix round 3's automatable values (round 190mt), ext3::reserved + 1 … ext3::autoEnd − 1, in order
const std::vector<Spec>& ext3Specs()
{
    static const std::vector<Spec> list {
        { "Grain On", 0.0f, 1.0f, true },
        { "Grain Position", 0.0f, 1.0f, false },
        { "Grain Size", 0.005f, 1.0f, false },
        { "Grain Density", 1.0f, 200.0f, false },
        { "Grain Spray", 0.0f, 1.0f, false },
        { "Grain Pitch", -24.0f, 24.0f, false },
        { "Grain Spread", 0.0f, 1.0f, false },
        { "Grain Gain", 0.0f, 1.0f, false },
        { "Grain Pan", -1.0f, 1.0f, false },
        { "Spectral On", 0.0f, 1.0f, true },
        { "Spectral Position", 0.0f, 1.0f, false },
        { "Spectral Stretch", -1.0f, 1.0f, false },
        { "Spectral Shift", -1.0f, 1.0f, false },
        { "Spectral Tilt", -1.0f, 1.0f, false },
        { "Spectral Band", 20.0f, 20000.0f, false },
        { "Spectral Width", 0.25f, 10.0f, false },
        { "Spectral Partials", 1.0f, (float) maxSpectralPartials, true },
        { "Spectral Transpose", -48.0f, 48.0f, true },
        { "Spectral Gain", 0.0f, 1.0f, false },
        { "Spectral Pan", -1.0f, 1.0f, false },
        { "Sample Clean", 0.0f, 1.0f, true },
    };
    return list;
}

std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof b, f, v);
    return b;
}
std::string timeText (double s) { return s < 1.0 ? fmt ("%.1f ms", s * 1000.0) : fmt ("%.2f s", s); }
std::string dbText (double lin) { return lin <= 1.0e-5 ? std::string ("-inf dB") : fmt ("%.1f dB", 20.0 * std::log10 (lin)); }
std::string pctText (double v) { return fmt ("%.0f %%", v * 100.0); }
} // namespace

std::string paramName (int index)
{
    if (index >= 0 && index < paramCount)
        return specs()[(std::size_t) index].name;
    if (automatable (index))
        return index > ext4Base   ? ext4Specs()[(std::size_t) (index - ext4Base - 1)].name
               : index > ext3Base ? ext3Specs()[(std::size_t) (index - ext3Base - 1)].name
               : index > ext2Base ? ext2Specs()[(std::size_t) (index - ext2Base - 1)].name
                                  : extSpecs()[(std::size_t) (index - extBase - 1)].name;
    return {};
}

std::string sourceName (int source)
{
    static const char* names[sourceCount] { "Amp", "Env 2", "Env 3", "LFO 1", "LFO 2", "Velocity", "Note", "Pitch Bend", "Pressure",
                                            "Mod Wheel", "Random", "Note PB", "Slide" };
    if (source >= src::chaos1 && source < allSourceCount)
        return "Chaos " + std::to_string (source - src::chaos1 + 1);
    if (source >= sourceCount && source < allSourceCount)
        return "Macro " + std::to_string (source - sourceCount + 1);
    return source >= 0 && source < sourceCount ? names[source] : std::string();
}

std::string targetName (int target, int effectMode1, int effectMode2)
{
    if (target >= rev2TargetCount && target < allTargetCount) // Helix round 3's rows (190mt)
    {
        static const char* rows3[newTargetCount3] { "Grain Pos", "Grain Size", "Grain Density", "Grain Spray", "Grain Pitch", "Grain Gain",
                                                    "Spectral Pos", "Spectral Stretch", "Spectral Shift", "Spectral Tilt", "Spectral Band",
                                                    "Spectral Gain" };
        return rows3[target - rev2TargetCount];
    }
    if (target >= plusTargetCount && target < rev2TargetCount) // Helix+ 2's rows
    {
        static const char* rows2[newTargetCount] { "Osc 3 Pitch", "Osc 3 Pos", "Osc 3 FX 1", "Osc 3 FX 2", "Osc 3 Pan", "Osc 3 Gain", "Osc 3 Warp 2",
                                                   "Sample Pitch", "Sample Gain", "Sample Pan", "Shift", "Multiband Depth" };
        return rows2[target - plusTargetCount];
    }
    if (target >= targetCount && target < plusTargetCount) // Helix+'s rows
    {
        static const char* rows[extTargetCount] { "Osc 1 Warp 2", "Osc 2 Warp 2", "Noise Level", "Unison Blend" };
        return rows[target - targetCount];
    }
    if (target < 0 || target >= targetCount)
        return {};
    if (target < tgt::subTone)
    {
        const auto k = target / tgt::oscStride, r = target % tgt::oscStride;
        const auto o = "Osc " + std::to_string (k + 1) + " ";
        const auto mode = k == 0 ? effectMode1 : effectMode2;
        static const char* fx[4][2] { { "FX 1", "FX 2" }, { "FM Pitch", "FM Amt" }, { "PW", "Sync" }, { "Warp", "Fold" } };
        static const char* rows[] { "Pitch", "Pos", "", "", "Pan", "Gain" };
        if (r == tgt::oscFx1 || r == tgt::oscFx2)
            return o + fx[std::clamp (mode, 0, 3)][r - tgt::oscFx1];
        return o + rows[r];
    }
    static const char* rest[] { "Sub Tone", "Sub Gain", "Filter 1 Freq", "Filter 1 Res", "Filter 1 Drive", "Filter 1 Morph", "Filter 2 Freq",
                                "Filter 2 Res", "Filter 2 Drive", "Filter 2 Morph", "Amp Attack", "Amp Decay", "Amp Release", "Amp Sustain",
                                "Env 2 Attack", "Env 2 Decay", "Env 2 Release", "Env 2 Initial", "Env 2 Peak", "Env 2 Sustain", "Env 2 Final",
                                "Env 3 Attack", "Env 3 Decay", "Env 3 Release", "Env 3 Initial", "Env 3 Peak", "Env 3 Sustain", "Env 3 Final",
                                "LFO 1 Amount", "LFO 1 Shaping", "LFO 1 Rate", "LFO 2 Amount", "LFO 2 Shaping", "LFO 2 Rate", "Time",
                                "Global Mod Amount", "Unison Amount", "Amp", "Pitch", "Glide" };
    return rest[target - tgt::subTone];
}

std::string paramText (int index, double v)
{
    static const char* modes[] { "None", "FM", "Classic", "Modern" };
    static const char* types[] { "Lowpass", "Highpass", "Bandpass", "Notch", "Morph" };
    static const char* circuits[] { "Clean", "OSR", "MS2", "SMP", "PRD" };
    static const char* loops[] { "None", "Trigger", "Loop" };
    static const char* shapes[] { "Sine", "Triangle", "Saw", "Rectangle", "Random" };
    static const char* routes[] { "Serial", "Parallel", "Split" };
    static const char* unison[] { "None", "Classic", "Shimmer", "Noise", "Phase Sync", "Position Spread", "Random Note" };
    // (190mi: the texts of syncBars' own values; from index 11 the list was one off, e.g. 1/8 showed as "3/32")
    static const char* syncs[] { "8 bars", "6 bars", "4 bars", "3 bars", "2 bars", "6/4", "1 bar", "3/4", "1/2", "3/8", "1/3", "5/16", "1/4",
                                 "3/16", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32", "1/48", "1/64" };
    const auto i = (int) std::lround (v);
    const auto onOff = [&] { return std::string (v > 0.5 ? "On" : "Off"); };
    if (index >= ext4Base) // Helix round 4 (round 190oc)
    {
        switch (index - ext4Base)
        {
            case ext4::grainUnison: case ext4::specUnison: return std::to_string (std::clamp (i, 1, maxOscUnison));
            case ext4::grainDetune: case ext4::specDetune: return fmt ("%.0f ct", unisonDetuneCents * std::clamp (v, 0.0, 1.0));
            default: return pctText (v);
        }
    }
    if (index >= ext3Base) // Helix round 3 (round 190mt)
    {
        switch (index - ext3Base)
        {
            case ext3::grainOn: case ext3::specOn: case ext3::sampleClean: return onOff();
            case ext3::grainSize: return timeText (v);
            case ext3::grainDensity: return fmt (v < 10.0 ? "%.1f /s" : "%.0f /s", v);
            case ext3::grainPitch: return fmt ("%+.2f st", v);
            case ext3::grainGain: case ext3::specGain: return dbText (v);
            case ext3::grainPan: case ext3::specPan: return paramText (oscBase (0) + osc::pan, v);
            case ext3::specStretch: return fmt ("%+.0f %%", v * 100.0);
            case ext3::specShift:
            {
                const auto hz = spectralShiftHz (v);
                return std::abs (hz) < 100.0 ? fmt ("%+.1f Hz", hz) : fmt ("%+.0f Hz", hz);
            }
            case ext3::specTilt: return fmt ("%+.1f dB/oct", v * 6.0);
            case ext3::specBand: return v < 1000.0 ? fmt ("%.0f Hz", v) : fmt ("%.2f kHz", v / 1000.0);
            case ext3::specWidth: return v >= 9.99 ? std::string ("Open") : fmt ("%.1f oct", v);
            case ext3::specPartials: return std::to_string (std::clamp (i, 1, maxSpectralPartials));
            case ext3::specTranspose: return fmt ("%+.0f st", v);
            default: return pctText (v);
        }
    }
    if (index >= ext2Base) // Helix+ 2 (round 190ml)
    {
        const auto e = index - ext2Base;
        if (e >= ext2::osc3 && e < ext2::osc3 + osc::stride) // (oscillator 3: as Live's two)
            return paramText (oscBase (0) + e - ext2::osc3, v);
        static const char* chaosModes[3] { "Smooth Random", "Lorenz", "Rossler" };
        switch (e)
        {
            case ext2::osc3WarpMode: case ext2::sampleWarpMode: return paramText (extBase + ext::warpMode, v);
            case ext2::sampleOn: case ext2::sampleLoop: case ext2::shiftOn: case ext2::eqOn: case ext2::mbOn: return onOff();
            case ext2::sampleTranspose: return fmt ("%+.0f st", v);
            case ext2::sampleDetune: return fmt ("%+.0f ct", v * 100.0);
            case ext2::sampleGain: return dbText (v);
            case ext2::samplePan: return paramText (oscBase (0) + osc::pan, v);
            case ext2::chaosMode: case ext2::chaosMode + ext2::chaosStride: return chaosModes[std::clamp (i, 0, 2)];
            case ext2::chaosRate: case ext2::chaosRate + ext2::chaosStride: return fmt (v < 10.0 ? "%.2f Hz" : "%.1f Hz", v);
            case ext2::shift:
            {
                const auto hz = shiftHz (v);
                return std::abs (hz) < 100.0 ? fmt ("%+.1f Hz", hz) : fmt ("%+.0f Hz", hz);
            }
            case ext2::eqLow: case ext2::eqMid: case ext2::eqHigh: case ext2::mbGain: return fmt ("%+.1f dB", v);
            case ext2::eqMidFreq: return v < 1000.0 ? fmt ("%.0f Hz", v) : fmt ("%.2f kHz", v / 1000.0);
            case ext2::sampleRoot: return std::to_string (i);
            default: return pctText (v);
        }
    }
    if (index >= extBase) // Helix+ (round 190mi)
    {
        static const char* warps[warp::count] { "Off", "Sync", "Window Sync", "Bend +", "Bend -", "Bend +/-", "PWM", "Asym +", "Asym -",
                                                "Asym +/-", "Mirror", "Flip", "Remap", "Crush", "FM <- Osc", "AM <- Osc", "RM <- Osc" };
        static const char* stacks[7] { "Off", "+1 oct", "+/-1 oct", "+1 +2 oct", "Fifth", "Fifth + oct", "+/-2 oct" };
        static const char* xtypes[xfilter::count] { "Live type", "Comb +", "Comb -", "Formant", "Phaser" };
        static const char* dists[4] { "Soft", "Hard", "Wavefold", "Crush" };
        static const char* delays[10] { "1/16", "1/8 T", "1/16 D", "1/8", "1/4 T", "1/8 D", "1/4", "1/4 D", "1/2", "1 bar" };
        switch (index - extBase)
        {
            case ext::warpMode: case ext::warpMode + ext::warpStride: return warps[std::clamp (i, 0, warp::count - 1)];
            case ext::noiseOn: case ext::lfoDraw: case ext::lfoDraw + 1: case ext::distOn: case ext::chorusOn: case ext::delayOn:
            case ext::delayPingPong: case ext::reverbOn: return onOff();
            case ext::unisonCount: return i < 2 ? std::string ("As device") : std::to_string (std::min (i, maxUnison));
            case ext::unisonStack: return stacks[std::clamp (i, 0, 6)];
            case ext::filterType: case ext::filterType + 1: return xtypes[std::clamp (i, 0, xfilter::count - 1)];
            case ext::distMode: return dists[std::clamp (i, 0, 3)];
            case ext::chorusRate: return fmt ("%.2f Hz", v);
            case ext::delayTime: return delays[std::clamp (i, 0, 9)];
            case ext::distDrive: return fmt ("%.1f dB", v * 36.0);
            default: return pctText (v);
        }
    }
    if (index < subOn)
    {
        switch (index % osc::stride)
        {
            case osc::on: return onOff();
            case osc::transpose: return fmt ("%+.0f st", v);
            case osc::detune: return fmt ("%+.0f ct", v * 100.0);
            case osc::position: return pctText (v);
            case osc::effectMode: return modes[std::clamp (i, 0, 3)];
            case osc::effect1: case osc::effect2: return pctText (v);
            case osc::pan: return std::abs (v) < 0.005 ? std::string ("C") : fmt (v < 0.0 ? "%.0fL" : "%.0fR", std::abs (v) * 50.0);
            default: return dbText (v);
        }
    }
    if (index == subOn)
        return onOff();
    if (index == subTone)
        return pctText (v);
    if (index == subGain)
        return dbText (v);
    if (index == subTranspose)
        return fmt ("%.0f oct", -v);
    if (index < routing)
    {
        switch ((index - filterBase (0)) % flt::stride)
        {
            case flt::on: return onOff();
            case flt::type: return types[std::clamp (i, 0, 4)];
            case flt::circuitLpHp: return circuits[std::clamp (i, 0, 4)];
            case flt::circuitBpNoMo: return circuits[std::clamp (i, 0, 1)];
            case flt::slope: return v > 0.5 ? "24 dB" : "12 dB";
            case flt::freq: return v < 1000.0 ? fmt ("%.0f Hz", v) : fmt ("%.2f kHz", v / 1000.0);
            case flt::res: return pctText (v);
            case flt::drive: return fmt ("%.1f dB", v);
            default: return pctText (v);
        }
    }
    if (index == routing)
        return routes[std::clamp (i, 0, 2)];
    if (index < lfoBase (0))
    {
        const auto k = index < envBase (1) ? 0 : index < envBase (2) ? 1 : 2;
        const auto e = index - envBase (k);
        if (e == env::attack || e == env::decay || e == env::release)
            return timeText (v);
        if (e == env::loopMode)
            return loops[std::clamp (i, 0, 2)];
        if (e == env::sustain && k == 0)
            return dbText (v);
        if (e >= env::sustain)
            return pctText (v);
        return pctText (v);
    }
    if (index < timeScale)
    {
        const auto e = (index - lfoBase (0)) % lfo::stride;
        switch (e)
        {
            case lfo::retrigger: case lfo::sync: return onOff();
            case lfo::type: return shapes[std::clamp (i, 0, 4)];
            case lfo::phase: return fmt ("%.0f deg", v);
            case lfo::rate: return fmt (v < 10.0 ? "%.2f Hz" : "%.1f Hz", v);
            case lfo::syncedRate: return syncs[std::clamp (i, 0, 21)];
            case lfo::attack: return timeText (v);
            default: return pctText (v);
        }
    }
    switch (index)
    {
        case unisonMode: return unison[std::clamp (i, 0, 6)];
        case unisonVoices: return std::to_string (std::clamp (i, 2, 8));
        case transpose: return fmt ("%+.0f st", v);
        case glide: return timeText (v);
        case volume: return dbText (v);
        case monoPoly: return v > 0.5 ? "Poly" : "Mono";
        case polyVoices: return std::to_string (polyVoiceCount (i));
        case hiQ: return onOff();
        default: return pctText (v);
    }
}
} // namespace wt

// --- the channel's values

std::vector<float> helixDefaults()
{
    const auto& d = wt::defaults();
    std::vector<float> v (d.begin(), d.end());
    v[(std::size_t) (wt::extBase + wt::ext::revision)] = (float) wt::currentRevision; // (a new channel: Helix+ … round 3 live)
    v[(std::size_t) (wt::ext3Base + wt::ext3::sampleClean)] = 1.0f;                   // (190mt: a new channel's sample band-limited)
    return v;
}

std::vector<double> helixState (const ChannelDef& channel)
{
    const auto& d = wt::defaults();
    std::vector<double> s (d.begin(), d.end());
    for (std::size_t i = 0; i < s.size() && i < channel.modelParams.size(); ++i)
        if (std::isfinite (channel.modelParams[i]))
            s[i] = channel.modelParams[i];
    return s;
}

const std::vector<EngineParam>& helixEngineParams()
{
    static const auto list = [] {
        std::vector<EngineParam> v;
        const auto& s = wt::specs();
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            EngineParam p;
            p.index = (int) i;
            p.name = s[i].name;
            p.low = s[i].low;
            p.high = s[i].high;
            p.stepped = s[i].stepped;
            v.push_back (std::move (p));
        }
        const auto& x = wt::extSpecs(); // Helix+ (round 190mi): their own indices after Live's state (sorted)
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            EngineParam p;
            p.index = wt::extBase + 1 + (int) i;
            p.name = x[i].name;
            p.low = x[i].low;
            p.high = x[i].high;
            p.stepped = x[i].stepped;
            v.push_back (std::move (p));
        }
        const auto& x2 = wt::ext2Specs(); // Helix+ 2 (round 190ml): after Helix+'s block
        for (std::size_t i = 0; i < x2.size(); ++i)
        {
            EngineParam p;
            p.index = wt::ext2Base + 1 + (int) i;
            p.name = x2[i].name;
            p.low = x2[i].low;
            p.high = x2[i].high;
            p.stepped = x2[i].stepped;
            v.push_back (std::move (p));
        }
        const auto& x3 = wt::ext3Specs(); // Helix round 3 (round 190mt): after Helix+ 2's block
        for (std::size_t i = 0; i < x3.size(); ++i)
        {
            EngineParam p;
            p.index = wt::ext3Base + 1 + (int) i;
            p.name = x3[i].name;
            p.low = x3[i].low;
            p.high = x3[i].high;
            p.stepped = x3[i].stepped;
            v.push_back (std::move (p));
        }
        const auto& x4 = wt::ext4Specs(); // Helix round 4 (round 190oc): after round 3's block
        for (std::size_t i = 0; i < x4.size(); ++i)
        {
            EngineParam p;
            p.index = wt::ext4Base + 1 + (int) i;
            p.name = x4[i].name;
            p.low = x4[i].low;
            p.high = x4[i].high;
            p.stepped = x4[i].stepped;
            v.push_back (std::move (p));
        }
        return v;
    }();
    return list;
}

void padHelixState (ChannelDef& c, bool helixPlus)
{
    if ((int) c.modelParams.size() < wt::stateSize)
    {
        // the missing values as they read (Live's from the defaults; a missing Helix+ block keeps revision 0)
        const auto& d = wt::defaults();
        const auto have = c.modelParams.size();
        c.modelParams.resize ((std::size_t) wt::stateSize);
        for (auto i = have; i < c.modelParams.size(); ++i)
            c.modelParams[i] = (float) d[i];
    }
    if (helixPlus && c.modelParams[(std::size_t) (wt::extBase + wt::ext::revision)] < (float) wt::currentRevision) // (a Helix+ value set: the blocks go live)
        c.modelParams[(std::size_t) (wt::extBase + wt::ext::revision)] = (float) wt::currentRevision;
}

namespace modelparams
{
namespace
{
std::optional<float> getHelix (const ChannelDef& c, const EngineParam& p)
{
    if (c.instrument != InstrumentKind::helix || ! wt::automatable (p.index))
        return std::nullopt;
    return p.index < (int) c.modelParams.size() ? c.modelParams[(std::size_t) p.index] : (float) wt::defaults()[(std::size_t) p.index];
}

bool setHelix (ChannelDef& c, const EngineParam& p, float raw)
{
    if (c.instrument != InstrumentKind::helix || ! wt::automatable (p.index))
        return false;
    padHelixState (c, p.index >= wt::extBase);
    c.modelParams[(std::size_t) p.index] = raw;
    return true;
}
} // namespace

Table helixModelTable() { return { &helixEngineParams(), &getHelix, &setHelix }; }
} // namespace modelparams

// --- the instrument

namespace
{
class HelixInstrument final : public Instrument
{
public:
    HelixInstrument()
    {
        const auto& d = wt::defaults();
        for (int i = 0; i < wt::stateSize; ++i)
            values[(std::size_t) i].store ((float) d[(std::size_t) i]);
        (void) wt::table (0);
    }

    void prepare (double sampleRate) noexcept override
    {
        engine.prepare (sampleRate);
        rate = sampleRate;
        lastTicks = -1.0;
        taken = 0;
    }

    // UI thread: the channel's values (the tables they name are built here, never on the audio thread)
    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        tempo.store (bpm > 0.0 ? (float) bpm : 120.0f);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        bendRange.store ((float) std::max (1, channel.misc.pitchRange));
        const auto& d = wt::defaults();
        bool changed = false;
        for (int i = 0; i < wt::stateSize; ++i)
        {
            auto v = i < (int) channel.modelParams.size() ? channel.modelParams[(std::size_t) i] : (float) d[(std::size_t) i];
            if (! std::isfinite (v))
                v = (float) d[(std::size_t) i];
            if (i == wt::tableBase || i == wt::tableBase + 1 || i == wt::oscTableIndex (2))
                (void) wt::table ((int) std::lround (v));
            if (values[(std::size_t) i].exchange (v) != v)
                changed = true;
        }
        // Helix+ 2 (round 190ml): the channel's user tables and sample, built here (UI thread) when its data changed; the
        // ones they replace stay alive until the audio thread has taken the new ones
        if (channel.pluginState != dataBytes)
        {
            try
            {
                dataBytes = channel.pluginState;
                wt::HelixUserData d;
                if (! dataBytes.empty())
                    (void) wt::decodeUserData (dataBytes, d);
                Held next;
                for (int k = 0; k < wt::oscCount; ++k)
                    next.tables[(std::size_t) k] = d.tables[(std::size_t) k].empty() ? nullptr : wt::userTableBuilt (d.tables[(std::size_t) k]);
                if (d.sample.frames() > 3)
                    next.sample = std::make_shared<wt::SampleBuffer> (std::move (d.sample));
                if (! d.spectral.empty() && next.sample != nullptr) // (190mt: the partials kept with the data)
                    next.spectral = std::make_shared<wt::SpectralData> (std::move (d.spectral));
                for (int k = 0; k < wt::oscCount; ++k)
                    userPtr[(std::size_t) k].store (next.tables[(std::size_t) k].get());
                samplePtr.store (next.sample.get());
                levelsPtr.store (nullptr);
                spectralPtr.store (next.spectral.get());
                held.tag = version.load() + 1;
                retired.push_back (std::move (held));
                held = std::move (next);
                changed = true;
            }
            catch (...)
            {
            }
        }
        // Helix round 3 (round 190mt): the sample's band-limited levels and its partials, made here (UI thread) once the
        // values ask for them (Clean on a playing sample, the grains, the spectral oscillator); kept with the sample
        if (held.sample != nullptr && values[(std::size_t) wt::extBase].load() >= 2.5f)
        {
            const auto on = [this] (int i) { return values[(std::size_t) (wt::ext3Base + i)].load() > 0.5f; };
            try
            {
                if (held.levels == nullptr && ((on (wt::ext3::sampleClean) && values[(std::size_t) (wt::ext2Base + wt::ext2::sampleOn)].load() > 0.5f)
                                               || on (wt::ext3::grainOn)))
                {
                    held.levels = wt::sampleLevels (*held.sample);
                    levelsPtr.store (held.levels.get());
                    changed = true;
                }
                if (held.spectral == nullptr && on (wt::ext3::specOn))
                {
                    held.spectral = std::make_shared<wt::SpectralData> (wt::analyseSpectrum (*held.sample));
                    spectralPtr.store (held.spectral.get());
                    changed = true;
                }
            }
            catch (...)
            {
            }
        }
        if (changed)
            version.fetch_add (1);
        const auto acked = taken.load();
        retired.erase (std::remove_if (retired.begin(), retired.end(), [acked] (const Held& h) { return h.tag <= acked; }), retired.end());
    }

    void setParameter (int index, float value) noexcept override
    {
        const auto* p = wt::automatable (index) ? findEngineParam (helixEngineParams(), index) : nullptr;
        if (p == nullptr || ! std::isfinite (value))
            return;
        const auto raw = p->raw (value);
        values[(std::size_t) index].store (raw);
        engine.setParam (index, raw);
    }

    void setNextNoteExpression (float, float cents, bool) noexcept override { nextCents = cents; }
    void setNextNoteModulation (float modX, float modY) noexcept override { nextModX = modX, nextModY = modY; }
    void setChannelPitchNow (float knob, float rest) noexcept override { live.set (knob, rest); }
    void pitchBend (float semitones) noexcept override { engine.setPitchBend (std::isfinite (semitones) ? semitones : 0.0f, bendRange.load()); }
    void controlChange (int cc, float value) noexcept override
    {
        if (cc == 1)
            engine.setModWheel (value);
    }
    void midiMessage (const std::uint8_t* bytes, int size) noexcept override
    {
        if (size >= 2 && (bytes[0] & 0xf0) == 0xd0) // channel pressure
            engine.setPressure ((float) bytes[1] / 127.0f);
        else if (size >= 3 && (bytes[0] & 0xf0) == 0xb0 && bytes[1] == 1)
            engine.setModWheel ((float) bytes[2] / 127.0f);
    }

    // MPE (issue #108): a sounding note's own pitch, timbre (Live's Slide) and pressure, as matrix sources
    void voiceProperty (int key, int prop, float value) noexcept override
    {
        if (prop == 1)
            engine.notePitch (key, value * 0.01f);
        else if (prop == 3)
            engine.noteSlide (key, value);
        else if (prop == 4)
            engine.notePressure (key, pressureOf (value));
    }
    void noteExpression (int key, int prop, float value, float) noexcept override
    {
        // (the channel's tuning is the engine's own pitch offset: only the note's cents move the voice)
        voiceProperty (key, prop, value);
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        engine.note (std::clamp (key, 0, 127), std::max (std::clamp (velocity, 0.0f, 1.0f), 1.0f / 127.0f), (nextCents - liveRest()) * 0.01f,
                     slideOf (nextModX), pressureOf (nextModY));
        nextCents = 0.0f, nextModX = 0.5f, nextModY = 0.5f;
    }

    void noteOff (int key) noexcept override { engine.note (std::clamp (key, 0, 127), 0.0f); }
    // FL's channel porta / Mono and slide notes (rollup 0.1.30, as round 190ix's Tetra): the voices take
    // HostSlide::pendingGlide at note on and glide to a slide note's pitch (`cents`: the note's cents, as noteOn's)
    bool followsChannelPorta() const noexcept override { return true; }
    void slideTo (int key, float cents, float samples) noexcept override
    {
        take();
        engine.slideTo ((float) std::clamp (key, 0, 127) + (cents - liveRest()) * 0.01f, samples);
    }
    void allNotesOff() noexcept override { engine.allNotesOff(); }
    void panic() noexcept override { engine.reset(); }
    bool idle() const noexcept override { return engine.idle(); }

    void setSongPosition (double ticks) noexcept override
    {
        playing = lastTicks >= 0.0 && ticks != lastTicks;
        lastTicks = ticks;
        songBeats = ticks / (double) ppq;
    }

    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override
    {
        take();
        if (n <= 0 || engine.sampleRate() != rate)
            return;
        engine.setPitchOffset ((std::isnan (live.knob) ? pitchCents.load() : live.now()) * 0.01f);
        const auto bpm = (double) tempo.load();
        for (int pos = 0; pos < n; pos += chunk)
        {
            const auto c = std::min (chunk, n - pos);
            std::fill (bufL, bufL + c, 0.0f);
            std::fill (bufR, bufR + c, 0.0f);
            engine.render (bufL, bufR, c, bpm, playing ? songBeats + (double) pos * bpm / (60.0 * rate) : -1.0);
            for (int i = 0; i < c; ++i)
            {
                left[pos + i] += bufL[i] * gainLeft;
                right[pos + i] += bufR[i] * gainRight;
            }
        }
    }

private:
    // Mod Y → pressure: 0.5 (FL's neutral, no pressure) … 1 [190hy: Mod Y = 0.5 + pressure / 2]
    static float pressureOf (float modY) noexcept { return std::clamp (2.0f * (modY - 0.5f), 0.0f, 1.0f); }
    // Mod X → Live's Slide (CC 74 / 127); FL's neutral 0.5 is "no slide data" [MEASURED d0: Live's notes without MPE
    // data play the default preset's Pos ← Slide as Slide 0]
    static float slideOf (float modX) noexcept { return modX == 0.5f ? -1.0f : std::clamp (modX, 0.0f, 1.0f); }
    float liveRest() const noexcept { return std::isnan (live.rest) ? 0.0f : live.rest; }

    void take() noexcept
    {
        const auto v = version.load();
        if (v == taken.load (std::memory_order_relaxed))
            return;
        for (int i = 0; i < wt::stateSize; ++i)
            state[(std::size_t) i] = values[(std::size_t) i].load (std::memory_order_relaxed);
        for (int k = 0; k < wt::oscCount; ++k)
            engine.setUserTable (k, userPtr[(std::size_t) k].load());
        engine.setSample (samplePtr.load());
        engine.setSampleLevels (levelsPtr.load());
        engine.setSpectral (spectralPtr.load());
        engine.setValues (state.data());
        taken.store (v);
    }
    // the user tables and the sample the engine may play (UI thread), and the ones replaced, kept until the audio thread
    // has taken the version after them
    struct Held
    {
        std::array<std::shared_ptr<const wt::Table>, wt::oscCount> tables {};
        std::shared_ptr<const wt::SampleBuffer> sample;
        std::shared_ptr<const wt::SampleLevels> levels;   // (190mt)
        std::shared_ptr<const wt::SpectralData> spectral; // (190mt)
        std::uint32_t tag = 0;
    };
    Held held;
    std::vector<Held> retired;
    std::vector<std::uint8_t> dataBytes;
    std::array<std::atomic<const wt::Table*>, wt::oscCount> userPtr {};
    std::atomic<const wt::SampleBuffer*> samplePtr { nullptr };
    std::atomic<const wt::SampleLevels*> levelsPtr { nullptr };
    std::atomic<const wt::SpectralData*> spectralPtr { nullptr };

    static constexpr int chunk = 512;
    wt::Engine engine;
    std::array<std::atomic<float>, wt::stateSize> values {};
    std::array<double, wt::stateSize> state {};
    std::atomic<std::uint32_t> version { 1 };
    std::atomic<std::uint32_t> taken { 0 };
    std::atomic<float> tempo { 120.0f }, pitchCents { 0.0f }, bendRange { 2.0f };
    LiveChannelPitch live;
    float nextCents = 0.0f, nextModX = 0.5f, nextModY = 0.5f;
    double rate = 48000.0, lastTicks = -1.0, songBeats = 0.0;
    bool playing = false;
    float bufL[chunk] {}, bufR[chunk] {};
};
} // namespace

std::unique_ptr<Instrument> makeHelixInstrument() { return std::make_unique<HelixInstrument>(); }
} // namespace lynx
