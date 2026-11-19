// Helix's wave tables (round 190ja, core/WavetableSynth.h): LYNX's own, generated here from additive and wave-shape
// recipes the first time a table is used, kept as mipmaps (half an octave apart, each distinct frame once) and never
// freed. No table data comes from another product: the recipes are textbook shapes (sine, triangle, saw, pulse), harmonic
// series, formant envelopes, filter responses, FM / sync / fold sweeps and seeded noise. Live's table names map onto the
// closest table here (matchLiveTable), chosen by measuring Live's rendered output black-box (docs/analise/REA_rodada190ja.md §4).

#include "core/WavetableSynth.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace lynx::wt
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int maxPartials = 1023;
constexpr int shapeLength = 4096; // the time-domain recipes are sampled at this many points per cycle

// --- a radix-2 FFT (table building only, never on the audio thread)
void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto ang = 2.0 * pi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// a frame's spectrum: amplitude and phase of partials 1…maxPartials (sample = Σ a sin (2π h x + φ))
struct Spectrum
{
    std::array<double, maxPartials + 1> amp {}, phase {};
    void clear() noexcept { amp.fill (0.0), phase.fill (0.0); }
};

// a cycle's samples (shapeLength) → its spectrum (DC dropped)
void spectrumOf (const std::vector<double>& wave, Spectrum& s)
{
    std::vector<std::complex<double>> a (wave.begin(), wave.end());
    fft (a, false);
    s.clear();
    const auto n = (double) wave.size();
    for (int h = 1; h <= maxPartials && h < (int) wave.size() / 2; ++h)
    {
        const auto c = a[(std::size_t) h] * (2.0 / n);
        s.amp[(std::size_t) h] = std::abs (c);
        s.phase[(std::size_t) h] = std::arg (c) + pi * 0.5; // (cos (θ) = sin (θ + π/2))
    }
}

// --- recipe helpers (x: 0…1 through the cycle, t: 0…1 through the table)
using Shape = std::function<double (double x, double t)>;
using Additive = std::function<void (double t, Spectrum& s)>;

double sine (double x) { return std::sin (2.0 * pi * x); }
double saw (double x) { return 1.0 - 2.0 * (x - std::floor (x)); }               // falling from 1 to −1
double tri (double x)
{
    x -= std::floor (x);
    return x < 0.25 ? 4.0 * x : x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0;
}
double pulse (double x, double width) { x -= std::floor (x); return x < width ? 1.0 : -1.0; }
double lerp (double a, double b, double f) { return a + (b - a) * f; }
// a morph through key shapes spaced evenly over the table
double morph (const std::vector<std::function<double (double)>>& keys, double x, double t)
{
    const auto pos = t * (double) (keys.size() - 1);
    const auto i = std::min ((int) pos, (int) keys.size() - 2);
    return lerp (keys[(std::size_t) i] (x), keys[(std::size_t) i + 1] (x), pos - (double) i);
}
// the textbook series (amplitudes of a falling saw / square / triangle)
double sawAmp (int h) { return 2.0 / (pi * h); }
double squareAmp (int h) { return (h & 1) ? 4.0 / (pi * h) : 0.0; }
std::uint32_t hash (std::uint32_t x)
{
    x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
    return x;
}
double rnd (std::uint32_t seed) { return (double) (hash (seed) >> 8) / 16777216.0; } // 0…1
// a resonant two-pole response at harmonic h (cutoff c in partials, q ≥ 0.5)
double lowpass (double h, double c, double q)
{
    const auto r = h / std::max (c, 0.25);
    return 1.0 / std::sqrt ((1.0 - r * r) * (1.0 - r * r) + (r / q) * (r / q));
}
// a vowel's formants (Hz, bandwidth Hz, gain) — textbook averages of adult speech
struct Formant
{
    double f, bw, g;
};
using Vowel = std::array<Formant, 3>;
const Vowel vowelA { { { 730, 90, 1.0 }, { 1090, 110, 0.5 }, { 2440, 160, 0.25 } } };
const Vowel vowelE { { { 530, 60, 1.0 }, { 1840, 100, 0.45 }, { 2480, 140, 0.3 } } };
const Vowel vowelI { { { 270, 60, 1.0 }, { 2290, 100, 0.3 }, { 3010, 150, 0.25 } } };
const Vowel vowelO { { { 570, 70, 1.0 }, { 840, 80, 0.6 }, { 2410, 160, 0.15 } } };
const Vowel vowelU { { { 300, 60, 1.0 }, { 870, 80, 0.4 }, { 2240, 150, 0.1 } } };
double formantGain (const Vowel& v, double hz)
{
    double g = 0.0;
    for (const auto& f : v)
        g += f.g / (1.0 + std::pow ((hz - f.f) / (f.bw * 0.5), 2.0));
    return g;
}
Vowel vowelAt (const std::vector<Vowel>& seq, double t)
{
    const auto pos = t * (double) (seq.size() - 1);
    const auto i = std::min ((int) pos, (int) seq.size() - 2);
    const auto f = pos - (double) i;
    Vowel v {};
    for (int k = 0; k < 3; ++k)
        v[(std::size_t) k] = { lerp (seq[(std::size_t) i][(std::size_t) k].f, seq[(std::size_t) i + 1][(std::size_t) k].f, f),
                               lerp (seq[(std::size_t) i][(std::size_t) k].bw, seq[(std::size_t) i + 1][(std::size_t) k].bw, f),
                               lerp (seq[(std::size_t) i][(std::size_t) k].g, seq[(std::size_t) i + 1][(std::size_t) k].g, f) };
    return v;
}
// the partials of a sung vowel at fundamental `f0` Hz (a glottal-like 1/h source through the formants)
Additive vowels (std::vector<Vowel> seq, double f0, double breath = 0.0)
{
    return [seq, f0, breath] (double t, Spectrum& s) {
        const auto v = vowelAt (seq, t);
        for (int h = 1; h * f0 < 8000.0 && h <= maxPartials; ++h)
        {
            s.amp[(std::size_t) h] = formantGain (v, h * f0) / (double) h + breath * rnd ((std::uint32_t) (h * 7919 + 13)) / (double) h;
            s.phase[(std::size_t) h] = 0.0;
        }
    };
}
// noise frames: random phases, amplitude law `tilt` (0 white, 0.5 pink, 1 brown), each frame its own draw
Additive noise (std::uint32_t seed, double tilt, double highpass = 0.0)
{
    return [seed, tilt, highpass] (double t, Spectrum& s) {
        const auto f = (std::uint32_t) std::lround (t * (frameCount - 1));
        for (int h = 1; h <= maxPartials; ++h)
        {
            const auto k = seed * 1000003u + f * 7919u + (std::uint32_t) h;
            auto a = (0.5 + rnd (k)) / std::pow ((double) h, tilt);
            if (highpass > 0.0)
                a *= (double) h / std::sqrt ((double) h * h + highpass * highpass);
            s.amp[(std::size_t) h] = a;
            s.phase[(std::size_t) h] = 2.0 * pi * rnd (k ^ 0x9e3779b9u);
        }
    };
}

struct Recipe
{
    const char* category;
    const char* name;
    Shape shape;      // time-domain recipe, or
    Additive partials; // an additive one
};

double sync (double x, double ratio) // a hard-synced falling saw (master phase x)
{
    x -= std::floor (x);
    return saw (x * ratio);
}
double fm (double x, double ratio, double index) { return std::sin (2.0 * pi * x + index * std::sin (2.0 * pi * x * ratio)); }
double feedbackFm (double x, double beta)
{
    // y = sin (θ + β y), solved by a few fixed-point steps (β < 1.6 converges)
    double y = 0.0;
    for (int i = 0; i < 24; ++i)
        y = std::sin (2.0 * pi * x + beta * y);
    return y;
}
double fold (double v, double drive) { return std::sin (0.5 * pi * v * drive); }

std::vector<Recipe> makeRecipes()
{
    std::vector<Recipe> r;
    auto shape = [&] (const char* c, const char* n, Shape f) { r.push_back ({ c, n, std::move (f), {} }); };
    auto add = [&] (const char* c, const char* n, Additive f) { r.push_back ({ c, n, {}, std::move (f) }); };

    // ---- Basics
    shape ("Basics", "Shapes", [] (double x, double t) {
        return morph ({ [] (double v) { return sine (v); }, [] (double v) { return tri (v); }, [] (double v) { return saw (v); },
                        [] (double v) { return pulse (v, 0.5); } }, x, t);
    });
    shape ("Basics", "Pulse Width", [] (double x, double t) { return pulse (x, 0.5 - 0.47 * t) + (0.5 - (0.5 - 0.47 * t)) * 2.0; });
    shape ("Basics", "Saw Width", [] (double x, double t) {
        const auto w = 1.0 - 0.9 * t;
        x -= std::floor (x);
        return x < w ? saw (x / w) : 0.0;
    });
    shape ("Basics", "Twin Saw", [] (double x, double t) { return 0.5 * (saw (x) + saw (x + 0.5 * t)); });
    shape ("Basics", "Octave Saws", [] (double x, double t) { return saw (x) + t * 0.7 * saw (2.0 * x + 0.25) + t * t * 0.5 * saw (4.0 * x); });
    add ("Basics", "Harmonic Ladder", [] (double t, Spectrum& s) {
        const auto top = 1.0 + t * 63.0;
        for (int h = 1; h <= 64; ++h)
            s.amp[(std::size_t) h] = std::clamp (top - (double) h + 1.0, 0.0, 1.0);
    });
    add ("Basics", "Odd Ladder", [] (double t, Spectrum& s) {
        const auto top = 1.0 + t * 31.0;
        for (int k = 0; k < 32; ++k)
            s.amp[(std::size_t) (2 * k + 1)] = std::clamp (top - (double) k, 0.0, 1.0) / std::sqrt (2.0 * k + 1.0);
    });
    add ("Basics", "Prime Partials", [] (double t, Spectrum& s) {
        static const int primes[] { 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97 };
        const auto top = 1.0 + t * 25.0;
        for (int k = 0; k < 26; ++k)
            s.amp[(std::size_t) primes[k]] = std::clamp (top - (double) k, 0.0, 1.0) / std::sqrt ((double) primes[k]);
    });
    add ("Basics", "Octave Sines", [] (double t, Spectrum& s) {
        s.amp[1] = 1.0;
        s.amp[2] = std::clamp (t * 3.0, 0.0, 1.0) * 0.8;
        s.amp[4] = std::clamp (t * 3.0 - 1.0, 0.0, 1.0) * 0.6;
        s.amp[8] = std::clamp (t * 3.0 - 2.0, 0.0, 1.0) * 0.45;
    });
    shape ("Basics", "Sync Sweep", [] (double x, double t) { return sync (x, 1.0 + 7.0 * t); });
    shape ("Basics", "FM Sweep", [] (double x, double t) { return fm (x, 1.0, 6.0 * t); });
    shape ("Basics", "FM Ratio Two", [] (double x, double t) { return fm (x, 2.0, 5.0 * t); });
    shape ("Basics", "Feedback FM", [] (double x, double t) { return feedbackFm (x, 1.5 * t); });
    add ("Basics", "Beat Pair", [] (double t, Spectrum& s) {
        for (int h = 1; h <= 24; ++h)
        {
            s.amp[(std::size_t) h] = 1.0 / h;
            s.phase[(std::size_t) h] = 2.0 * pi * t * (h % 3 == 0 ? 1.0 : h % 3 == 1 ? 0.0 : -1.0);
        }
    });
    add ("Basics", "Detuned Pair", [] (double t, Spectrum& s) {
        // two saws a cycle apart in phase drift: the beating of a detuned pair, frozen frame by frame
        for (int h = 1; h <= maxPartials; ++h)
        {
            s.amp[(std::size_t) h] = sawAmp (h) * std::abs (std::cos (pi * h * 0.5 * t));
            s.phase[(std::size_t) h] = -pi * h * 0.5 * t;
        }
    });
    add ("Basics", "Prime Fan", [] (double t, Spectrum& s) {
        // every prime partial up to 89 (and the fundamental), the upper ones fading out across the table
        for (int h = 1; h <= 89; ++h)
        {
            bool isPrime = h > 1;
            for (int d = 2; d * d <= h; ++d)
                if (h % d == 0)
                    isPrime = false;
            if (h == 1 || isPrime)
                s.amp[(std::size_t) h] = 1.0 / std::sqrt ((double) h) * std::clamp (2.0 - t * 2.0 * h / 30.0, 0.0, 1.0);
        }
    });
    add ("Basics", "White Noise", noise (1, 0.0));

    // ---- Formant
    add ("Formant", "Vowels", vowels ({ vowelA, vowelE, vowelI, vowelO, vowelU }, 110.0));
    add ("Formant", "Vowels High", vowels ({ vowelA, vowelE, vowelI, vowelO, vowelU }, 220.0));
    add ("Formant", "Ah Oh Oo", vowels ({ vowelA, vowelO, vowelU }, 130.0));
    add ("Formant", "Breathy Vowels", vowels ({ vowelU, vowelO, vowelA, vowelE }, 110.0, 0.6));
    add ("Formant", "Formant Sweep", [] (double t, Spectrum& s) {
        const auto peak = 2.0 + t * 58.0;
        for (int h = 1; h <= 256; ++h)
            s.amp[(std::size_t) h] = (0.15 + 3.0 / (1.0 + std::pow ((h - peak) / (1.0 + peak * 0.12), 2.0))) / h;
    });
    add ("Formant", "Twin Formants", [] (double t, Spectrum& s) {
        const auto p1 = 6.0 - 4.0 * t, p2 = 9.0 + 30.0 * t;
        for (int h = 1; h <= 256; ++h)
            s.amp[(std::size_t) h] = (0.1 + 2.0 / (1.0 + std::pow ((h - p1) / 1.2, 2.0)) + 1.5 / (1.0 + std::pow ((h - p2) / 2.0, 2.0))) / std::sqrt ((double) h);
    });

    // ---- Harmonics
    add ("Harmonics", "Sine Steps", [] (double t, Spectrum& s) {
        const auto pos = t * 15.0; // a single partial gliding 1 → 16 with crossfades
        const auto i = std::min ((int) pos, 14);
        s.amp[(std::size_t) i + 1] = 1.0 - (pos - i);
        s.amp[(std::size_t) i + 2] = pos - i;
    });
    add ("Harmonics", "Odd Sines", [] (double t, Spectrum& s) {
        for (int k = 0; k < 8; ++k)
            s.amp[(std::size_t) (2 * k + 1)] = std::pow (0.5 + 0.5 * std::cos (2.0 * pi * (t * 1.5 - k / 8.0)), 2.0) / (1.0 + k * 0.3);
    });
    add ("Harmonics", "Even Sines", [] (double t, Spectrum& s) {
        s.amp[1] = 1.0;
        for (int k = 1; k < 9; ++k)
            s.amp[(std::size_t) (2 * k)] = std::pow (0.5 + 0.5 * std::cos (2.0 * pi * (t * 1.5 - k / 9.0)), 2.0) / (1.0 + k * 0.25);
    });
    add ("Harmonics", "Spectral Sweep", [] (double t, Spectrum& s) {
        const auto c = 1.0 + 40.0 * t;
        for (int h = 1; h <= 128; ++h)
            s.amp[(std::size_t) h] = std::exp (-std::pow ((h - c) / (2.0 + c * 0.2), 2.0));
    });
    add ("Harmonics", "Shrinking Saw", [] (double t, Spectrum& s) {
        const auto p = 3.0 - 2.0 * t;
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = 1.0 / std::pow ((double) h, p);
    });
    add ("Harmonics", "Harmonic Blocks", [] (double t, Spectrum& s) {
        // a block of eight neighbouring partials walking up the series
        const auto lo = 1.0 + t * 40.0;
        for (int h = 1; h <= 64; ++h)
            s.amp[(std::size_t) h] = (h >= lo && h < lo + 8.0 ? 1.0 : 0.04) / std::sqrt ((double) h);
    });
    add ("Harmonics", "Bell Partials", [] (double t, Spectrum& s) {
        static const int set[] { 1, 2, 3, 5, 7, 9, 12, 16, 21 };
        for (int k = 0; k < 9; ++k)
            s.amp[(std::size_t) set[k]] = std::exp (-k * (1.2 - t)) * (k % 2 == 0 ? 1.0 : 0.7);
    });
    add ("Harmonics", "Drawbars", [] (double t, Spectrum& s) {
        // organ registrations morphing: flute → full → bright
        static const double regs[3][7] { { 1, 0.8, 0.0, 0.3, 0, 0, 0 }, { 1, 1, 0.8, 0.8, 0.6, 0.6, 0.5 }, { 0.6, 0.3, 1.0, 0.2, 0.9, 0.3, 0.8 } };
        static const int h[7] { 1, 2, 3, 4, 6, 8, 10 };
        const auto pos = t * 2.0;
        const auto i = std::min ((int) pos, 1);
        for (int k = 0; k < 7; ++k)
            s.amp[(std::size_t) h[k]] = lerp (regs[i][k], regs[i + 1][k], pos - i);
    });

    // ---- Filter
    add ("Filter", "Lowpass Saw", [] (double t, Spectrum& s) {
        const auto c = std::pow (2.0, t * 8.0);
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = sawAmp (h) * lowpass (h, c, 0.7);
    });
    add ("Filter", "Resonant Saw", [] (double t, Spectrum& s) {
        const auto c = std::pow (2.0, 1.0 + t * 6.5);
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = sawAmp (h) * lowpass (h, c, 4.0);
    });
    add ("Filter", "Lowpass Square", [] (double t, Spectrum& s) {
        const auto c = std::pow (2.0, t * 8.0);
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = squareAmp (h) * lowpass (h, c, 1.2);
    });
    add ("Filter", "Bandpass Saw", [] (double t, Spectrum& s) {
        const auto c = std::pow (2.0, 0.5 + t * 6.5);
        for (int h = 1; h <= maxPartials; ++h)
        {
            const auto r = h / c;
            s.amp[(std::size_t) h] = sawAmp (h) * (r / 2.0) / std::sqrt ((1 - r * r) * (1 - r * r) + (r / 2.0) * (r / 2.0)) * 2.0;
        }
    });
    add ("Filter", "Highpass Saw", [] (double t, Spectrum& s) {
        const auto c = std::pow (2.0, t * 6.0);
        for (int h = 1; h <= maxPartials; ++h)
        {
            const auto r = h / c;
            s.amp[(std::size_t) h] = sawAmp (h) * r * r / std::sqrt ((1 - r * r) * (1 - r * r) + r * r * 2.0);
        }
    });
    add ("Filter", "Closing Saw", [] (double t, Spectrum& s) {
        const auto c = std::pow (2.0, 8.0 * (1.0 - t)); // bright → dark
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = sawAmp (h) * lowpass (h, c, 0.8);
    });
    add ("Filter", "Closing Resonance", [] (double t, Spectrum& s) {
        const auto c = std::pow (2.0, 1.0 + 6.5 * (1.0 - t));
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = sawAmp (h) * lowpass (h, c, 3.0);
    });
    add ("Filter", "Acid Square", [] (double t, Spectrum& s) {
        const auto c = std::pow (2.0, 1.5 + 5.0 * t);
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = (squareAmp (h) + 0.3 * sawAmp (h)) * lowpass (h, c, 6.0);
    });
    add ("Filter", "Comb Sweep", [] (double t, Spectrum& s) {
        const auto d = 2.0 + 14.0 * t; // notch spacing in partials
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = sawAmp (h) * (0.15 + std::abs (std::cos (pi * h / d)));
    });

    // ---- Distortion
    shape ("Distortion", "Fold", [] (double x, double t) { return fold (sine (x), 1.0 + 9.0 * t); });
    shape ("Distortion", "Clip", [] (double x, double t) {
        const auto g = std::pow (30.0, t);
        return std::tanh (g * sine (x)) / std::tanh (g);
    });
    shape ("Distortion", "Bit Crush", [] (double x, double t) {
        const auto steps = std::pow (2.0, 7.0 * (1.0 - t)) + 1.0;
        return std::round (sine (x) * steps) / steps;
    });
    shape ("Distortion", "Asymmetric Drive", [] (double x, double t) {
        const auto v = sine (x) + 0.6 * t;
        return std::tanh ((1.0 + 6.0 * t) * v) - std::tanh ((1.0 + 6.0 * t) * 0.6 * t);
    });
    shape ("Distortion", "Phase Bend", [] (double x, double t) {
        const auto k = 0.5 - 0.48 * t; // the knee of a two-slope phase
        x -= std::floor (x);
        const auto p = x < k ? 0.5 * x / k : 0.5 + 0.5 * (x - k) / (1.0 - k);
        return std::cos (2.0 * pi * p);
    });
    shape ("Distortion", "Fold Saw", [] (double x, double t) { return fold (saw (x), 1.0 + 5.0 * t); });

    // ---- Complex
    shape ("Complex", "Ring", [] (double x, double t) {
        const auto pos = 1.0 + t * 8.0;
        const auto i = std::min ((int) pos, 8);
        return sine (x) * lerp (sine (x * i), sine (x * (i + 1)), pos - i);
    });
    shape ("Complex", "Cross Mod", [] (double x, double t) { return fm (x, 3.0, 4.0 * t) * lerp (1.0, saw (2.0 * x), t); });
    shape ("Complex", "Phase Steps", [] (double x, double t) {
        const auto steps = 2.0 + std::floor (t * 14.0);
        x -= std::floor (x);
        return sine (std::floor (x * steps) / steps + x * 0.25);
    });
    shape ("Complex", "Metal", [] (double x, double t) { return fm (x, 7.0, 2.0 + 4.0 * t) * 0.6 + 0.4 * fm (x, 3.0, 3.0 * t); });

    // ---- Instrument
    add ("Instrument", "Struck String", [] (double t, Spectrum& s) {
        const auto strike = 0.08 + 0.2 * t, roll = 1.6 - 0.8 * t;
        for (int h = 1; h <= 512; ++h)
            s.amp[(std::size_t) h] = std::abs (std::sin (pi * h * strike)) / std::pow ((double) h, roll);
    });
    add ("Instrument", "Reed", [] (double t, Spectrum& s) {
        for (int h = 1; h <= 256; ++h)
            s.amp[(std::size_t) h] = ((h & 1) ? 1.0 : t * 0.6) / std::pow ((double) h, 1.1) * (1.0 + 2.0 / (1.0 + std::pow ((h - 6.0 - 6.0 * t) / 3.0, 2.0)));
    });
    add ("Instrument", "Bowed", [] (double t, Spectrum& s) {
        for (int h = 1; h <= 512; ++h)
            s.amp[(std::size_t) h] = sawAmp (h) * (0.6 + 0.8 / (1.0 + std::pow ((h - 3.0 - 9.0 * t) / 3.0, 2.0))) * lowpass (h, 20.0 + 60.0 * t, 0.7);
    });
    add ("Instrument", "Mallet", [] (double t, Spectrum& s) {
        s.amp[1] = 1.0;
        s.amp[4] = 0.5 * (1.0 - 0.5 * t);
        s.amp[10] = 0.3 * t;
        s.amp[3] = 0.15 * t;
        s.amp[7] = 0.1 * t;
    });
    add ("Instrument", "Brass", [] (double t, Spectrum& s) {
        const auto p = 2.2 - 1.4 * t;
        for (int h = 1; h <= 256; ++h)
            s.amp[(std::size_t) h] = 1.0 / std::pow ((double) h, p) * (1.0 + 1.5 * t / (1.0 + std::pow ((h - 8.0) / 4.0, 2.0)));
    });
    add ("Instrument", "Plucked", [] (double t, Spectrum& s) {
        for (int h = 1; h <= 512; ++h)
            s.amp[(std::size_t) h] = std::abs (std::sin (pi * h * 0.2)) / (double) h * std::exp (-(double) h * (0.2 - 0.18 * t));
    });

    // ---- Noise
    add ("Noise", "Pink Noise", noise (2, 0.5));
    add ("Noise", "Brown Noise", noise (3, 1.0));
    add ("Noise", "Bright Noise", noise (4, 0.0, 64.0));
    add ("Noise", "Crackle", [] (double t, Spectrum& s) {
        const auto f = (std::uint32_t) std::lround (t * (frameCount - 1));
        for (int h = 1; h <= maxPartials; ++h)
        {
            const auto k = 5u * 1000003u + f * 7919u + (std::uint32_t) h;
            s.amp[(std::size_t) h] = rnd (k) > 0.9 ? 1.0 : 0.05;
            s.phase[(std::size_t) h] = 2.0 * pi * rnd (k ^ 0x51ed27u);
        }
    });

    // ---- Retro
    shape ("Retro", "Stepped Saw", [] (double x, double t) {
        const auto steps = std::pow (2.0, 5.0 - 3.0 * t);
        return std::round (saw (x) * steps) / steps;
    });
    shape ("Retro", "Pulse Sweep", [] (double x, double t) { return pulse (x, 0.5 - 0.45 * std::floor (t * 8.0) / 8.0); });
    add ("Retro", "Organ Cycle", [] (double t, Spectrum& s) {
        for (int h = 1; h <= 16; ++h)
            s.amp[(std::size_t) h] = std::pow (0.5 + 0.5 * std::cos (2.0 * pi * (t * 2.0 + h / 5.0)), 3.0) / std::sqrt ((double) h) + (h == 1 ? 0.5 : 0.0);
    });
    add ("Retro", "Harmonic Echo", [] (double t, Spectrum& s) {
        for (int h = 1; h <= 64; ++h)
            s.amp[(std::size_t) h] = 1.0 / h * (0.3 + 0.7 * std::abs (std::cos (pi * h * (0.05 + 0.2 * t))));
    });

    // ---- Vintage
    add ("Vintage", "Analog Saw", [] (double t, Spectrum& s) {
        for (int h = 1; h <= maxPartials; ++h)
            s.amp[(std::size_t) h] = sawAmp (h) * lowpass (h, 60.0 + 200.0 * t, 0.6) * (1.0 + 0.05 * std::sin ((double) h));
    });
    shape ("Vintage", "Analog Square", [] (double x, double t) {
        x -= std::floor (x);
        const auto tilt = 0.3 * (1.0 - t);
        return (x < 0.5 ? 1.0 - tilt * x * 2.0 : -1.0 + tilt * (x - 0.5) * 2.0);
    });
    shape ("Vintage", "Saw Pulse Mix", [] (double x, double t) { return 0.5 * saw (x) + 0.5 * t * pulse (x, 0.25 + 0.2 * t); });
    shape ("Vintage", "Square Stack", [] (double x, double t) { return pulse (x, 0.5) + t * 0.7 * pulse (2.0 * x, 0.5) + t * 0.5 * pulse (4.0 * x, 0.5); });
    shape ("Vintage", "Double Pulse", [] (double x, double t) { return 0.5 * (pulse (x, 0.5) + pulse (x + 0.1 + 0.3 * t, 0.3)); });

    // ---- Collection
    shape ("Collection", "Ember", [] (double x, double t) { return std::tanh ((1.0 + 3.0 * t) * (saw (x) + 0.5 * sine (3.0 * x))); });
    add ("Collection", "Glass", [] (double t, Spectrum& s) {
        for (int h = 1; h <= 32; ++h)
            s.amp[(std::size_t) h] = (h == 1 ? 1.0 : 0.0) + ((h % 4 == 0) ? 0.5 * t : 0.0) + ((h % 5 == 0) ? 0.4 * (1.0 - t) : 0.0);
    });
    shape ("Collection", "Ink", [] (double x, double t) { return morph ({ [] (double v) { return tri (v); }, [] (double v) { return fold (tri (v), 3.0); },
                                                                          [] (double v) { return fm (v, 2.0, 3.0); } }, x, t); });
    add ("Collection", "Moss", [] (double t, Spectrum& s) {
        for (int h = 1; h <= 128; ++h)
            s.amp[(std::size_t) h] = sawAmp (h) * (0.4 + 0.6 * std::pow (0.5 + 0.5 * std::sin (h * 0.7 + t * 6.0), 2.0)) * lowpass (h, 30.0, 0.8);
    });
    return r;
}

const std::vector<Recipe>& recipes()
{
    static const auto list = makeRecipes();
    return list;
}

// --- building
// the gain rule of a table's mips (190ml: one builder for the factory recipes and the user tables)
enum class Gain
{
    peak,  // the loudest frame of mip 0 peaks at 1 (additive recipes, user tables)
    fixed, // the shape recipe's own peak (`ref`)
    noise  // the noise tables' level (−8.4 dB RMS, frame 0 of mip 0)
};
std::unique_ptr<Table> buildMips (const std::vector<Spectrum>& frames, Gain rule, double ref);

std::unique_ptr<Table> build (const Recipe& recipe)
{
    std::vector<Spectrum> frames ((std::size_t) frameCount);
    std::vector<double> wave ((std::size_t) shapeLength);
    double shapePeak = 0.0; // a shape recipe's own peak (its band-limited frames may overshoot it a little, as Live's do)
    for (int f = 0; f < frameCount; ++f)
    {
        const auto pos = (double) f / (double) (frameCount - 1);
        auto& s = frames[(std::size_t) f];
        if (recipe.shape)
        {
            double mean = 0.0;
            for (int i = 0; i < shapeLength; ++i)
                mean += (wave[(std::size_t) i] = recipe.shape ((double) i / shapeLength, pos));
            mean /= shapeLength;
            for (const auto v : wave)
                shapePeak = std::max (shapePeak, std::abs (v - mean));
            spectrumOf (wave, s);
        }
        else
        {
            s.clear();
            recipe.partials (pos, s);
            s.amp[0] = 0.0;
        }
    }
    const auto noiseTable = std::string (recipe.category) == "Noise" || std::string (recipe.name) == "White Noise";
    auto t = buildMips (frames, noiseTable ? Gain::noise : recipe.shape ? Gain::fixed : Gain::peak, shapePeak);
    t->category = recipe.category;
    t->name = recipe.name;
    return t;
}

std::unique_ptr<Table> buildMips (const std::vector<Spectrum>& frames, Gain rule, double shapePeak)
{
    auto t = std::make_unique<Table>();
    // the mips, richest first; mip 0 sets the table's gain (below), then every frame is scaled and kept once: a frame bit for
    // bit equal to one already kept (same length) points at it (190js: a static table's frames, the mips of a table whose
    // partials stop below a mip's limit; the values are the same floats as one array per mip held, so the sound is too)
    std::size_t full = 0;
    for (int m = 0; m < mipCount; ++m)
        full += (std::size_t) frameCount * (std::size_t) (mipLength (m) + 1);
    std::vector<float> pool;
    pool.reserve (full);
    std::array<std::array<std::size_t, frameCount>, mipCount> at {};
    std::unordered_multimap<std::uint64_t, std::pair<std::size_t, std::size_t>> kept; // a frame's hash → its offset in pool, length
    std::vector<float> d;
    float g = 1.0f;
    for (int m = 0; m < mipCount; ++m)
    {
        const auto len = mipLength (m), top = mipPartials (m);
        const auto stride = (std::size_t) (len + 1);
        d.assign ((std::size_t) frameCount * stride, 0.0f);
        std::vector<std::complex<double>> a ((std::size_t) len);
        double peak = 0.0;
        for (int f = 0; f < frameCount; ++f)
        {
            std::fill (a.begin(), a.end(), std::complex<double> {});
            const auto& s = frames[(std::size_t) f];
            for (int h = 1; h <= top && h < len / 2; ++h)
                if (s.amp[(std::size_t) h] != 0.0)
                {
                    // sin (θ + φ) = Im (e^{i(θ + φ)}): the bin holds −i/2 · a e^{iφ}, its mirror the conjugate
                    const auto c = std::polar (s.amp[(std::size_t) h] * 0.5, s.phase[(std::size_t) h] - pi * 0.5);
                    a[(std::size_t) h] = c;
                    a[(std::size_t) (len - h)] = std::conj (c);
                }
            fft (a, true);
            auto* out = d.data() + (std::size_t) f * stride;
            for (int i = 0; i < len; ++i)
            {
                out[i] = (float) a[(std::size_t) i].real();
                if (m == 0)
                    peak = std::max (peak, std::abs (a[(std::size_t) i].real()));
            }
            out[len] = out[0];
        }
        if (m == 0)
        {
            // one gain for the whole table: a shape recipe keeps its own ±1 scale (a sine frame peaks at 1, a square's
            // band-limited edges overshoot it as Live's do [MEASURED 190ja a0: sine −3.0 dB, square's fundamental 4/π at the
            // centre]); an additive one is scaled so its loudest frame peaks at 1
            // noise tables: every frame at Live's level for its white noise table, −8.4 dB RMS full band [MEASURED 190ja a8]
            const auto noiseTable = rule == Gain::noise;
            double rms = 0.0;
            if (noiseTable)
            {
                for (int i = 0; i < len; ++i)
                    rms += (double) d[(std::size_t) i] * d[(std::size_t) i];
                rms = std::sqrt (rms / len) / 0.38;
            }
            const auto ref = noiseTable ? rms : rule == Gain::fixed ? shapePeak : peak;
            g = ref > 1.0e-12 ? (float) (1.0 / ref) : 1.0f;
        }
        for (auto& v : d)
            v *= g;
        for (int f = 0; f < frameCount; ++f)
        {
            const auto* fr = d.data() + (std::size_t) f * stride;
            std::uint64_t h = 1469598103934665603ull ^ (std::uint64_t) len; // FNV-1a over the frame's bits
            for (std::size_t i = 0; i < stride; ++i)
            {
                std::uint32_t bits;
                std::memcpy (&bits, fr + i, sizeof bits);
                h = (h ^ bits) * 1099511628211ull;
            }
            auto found = pool.size();
            for (auto [it, last] = kept.equal_range (h); it != last; ++it)
                if (it->second.second == stride && std::memcmp (pool.data() + it->second.first, fr, stride * sizeof (float)) == 0)
                {
                    found = it->second.first;
                    break;
                }
            if (found == pool.size())
            {
                pool.insert (pool.end(), fr, fr + stride);
                kept.emplace (h, std::make_pair (found, stride));
            }
            at[(std::size_t) m][(std::size_t) f] = found;
        }
    }
    t->pool.assign (pool.begin(), pool.end()); // (exactly the frames kept)
    for (int m = 0; m < mipCount; ++m)
        for (int f = 0; f < frameCount; ++f)
            t->frames[(std::size_t) m][(std::size_t) f] = t->pool.data() + at[(std::size_t) m][(std::size_t) f];
    return t;
}

// --- user tables (Helix+ 2, round 190ml): built from their spectra by the same mip builder (the same anti-aliasing as
// the factory tables), shared by content while anyone holds them
struct UserKey
{
    std::uint64_t hash = 0;
    std::vector<float> amp, phase; // (compared in full on a hash match)
    int partials = 0;
};
std::mutex userLock;
std::vector<std::pair<UserKey, std::weak_ptr<const Table>>>& userBuilt()
{
    static std::vector<std::pair<UserKey, std::weak_ptr<const Table>>> list;
    return list;
}

std::mutex buildLock;
std::vector<std::atomic<const Table*>>& built()
{
    static std::vector<std::atomic<const Table*>> list (recipes().size());
    return list;
}
} // namespace

// half an octave per mip: the partials kept are 1023 / 2^(m/2) (the oscillator reads the richest mip with nothing above
// Nyquist, so its top partial is never more than half an octave below Nyquist); each frame at least 8 samples per cycle of
// its top partial up to 4096 (linear interpolation: its images stay far below the partials)
namespace
{
struct MipSizes
{
    std::array<int, mipCount> partials {}, length {};
    MipSizes()
    {
        for (int m = 0; m < mipCount; ++m)
        {
            partials[(std::size_t) m] = std::max (1, (int) std::floor (maxPartials / std::pow (2.0, 0.5 * m)));
            int len = 512;
            while (len < 8 * partials[(std::size_t) m] && len < 4096)
                len *= 2;
            length[(std::size_t) m] = len;
        }
    }
};
const MipSizes& mipSizes() noexcept
{
    static const MipSizes s; // (made before any table: the first table() builds it, off the audio thread)
    return s;
}
} // namespace
int mipPartials (int mip) noexcept { return mipSizes().partials[(std::size_t) std::clamp (mip, 0, mipCount - 1)]; }
int mipLength (int mip) noexcept { return mipSizes().length[(std::size_t) std::clamp (mip, 0, mipCount - 1)]; }

int tableCount() noexcept { return (int) recipes().size(); }
const char* tableName (int index) noexcept { return recipes()[(std::size_t) std::clamp (index, 0, tableCount() - 1)].name; }
const char* tableCategory (int index) noexcept { return recipes()[(std::size_t) std::clamp (index, 0, tableCount() - 1)].category; }

std::vector<std::string> tableCategories()
{
    std::vector<std::string> c;
    for (const auto& r : recipes())
        if (std::find (c.begin(), c.end(), r.category) == c.end())
            c.push_back (r.category);
    return c;
}

const Table& table (int index)
{
    index = std::clamp (index, 0, tableCount() - 1);
    auto& slot = built()[(std::size_t) index];
    if (const auto* t = slot.load (std::memory_order_acquire))
        return *t;
    std::lock_guard<std::mutex> lock (buildLock);
    if (const auto* t = slot.load (std::memory_order_acquire))
        return *t;
    const auto* t = build (recipes()[(std::size_t) index]).release(); // (kept for the program's life: the audio thread may hold it)
    slot.store (t, std::memory_order_release);
    return *t;
}

const Table* tableIfBuilt (int index) noexcept
{
    if (index < 0 || index >= tableCount())
        return nullptr;
    return built()[(std::size_t) index].load (std::memory_order_acquire);
}

void tableSpectrum (int index, int frame, float* amplitude, float* phase, int partials)
{
    const auto& r = recipes()[(std::size_t) std::clamp (index, 0, tableCount() - 1)];
    Spectrum s;
    const auto pos = (double) std::clamp (frame, 0, frameCount - 1) / (double) (frameCount - 1);
    if (r.shape)
    {
        std::vector<double> wave ((std::size_t) shapeLength);
        for (int i = 0; i < shapeLength; ++i)
            wave[(std::size_t) i] = r.shape ((double) i / shapeLength, pos);
        spectrumOf (wave, s);
    }
    else
        r.partials (pos, s);
    for (int h = 0; h < partials; ++h)
    {
        amplitude[h] = h + 1 <= maxPartials ? (float) s.amp[(std::size_t) h + 1] : 0.0f;
        if (phase != nullptr)
            phase[h] = h + 1 <= maxPartials ? (float) s.phase[(std::size_t) h + 1] : 0.0f;
    }
}

std::shared_ptr<const Table> userTableBuilt (const UserTable& u)
{
    if (u.empty())
        return nullptr;
    UserKey key;
    key.partials = std::clamp (u.partials, 1, maxPartials);
    const auto n = (std::size_t) frameCount * (std::size_t) u.partials;
    key.amp.assign (u.amp.begin(), u.amp.begin() + (std::ptrdiff_t) n);
    key.phase.assign (u.phase.begin(), u.phase.begin() + (std::ptrdiff_t) std::min (n, u.phase.size()));
    key.phase.resize (n, 0.0f);
    std::uint64_t h = 1469598103934665603ull ^ (std::uint64_t) key.partials;
    for (const auto* v : { &key.amp, &key.phase })
        for (const auto f : *v)
        {
            std::uint32_t bits;
            std::memcpy (&bits, &f, sizeof bits);
            h = (h ^ bits) * 1099511628211ull;
        }
    key.hash = h;
    {
        std::lock_guard<std::mutex> lock (userLock);
        auto& list = userBuilt();
        list.erase (std::remove_if (list.begin(), list.end(), [] (const auto& e) { return e.second.expired(); }), list.end());
        for (const auto& [k, w] : list)
            if (k.hash == key.hash && k.partials == key.partials && k.amp == key.amp && k.phase == key.phase)
                if (auto t = w.lock())
                    return t;
    }
    std::vector<Spectrum> frames ((std::size_t) frameCount);
    for (int f = 0; f < frameCount; ++f)
    {
        auto& s = frames[(std::size_t) f];
        s.clear();
        for (int p = 0; p < u.partials && p + 1 <= maxPartials; ++p)
        {
            const auto a = key.amp[(std::size_t) (f * u.partials + p)];
            s.amp[(std::size_t) p + 1] = std::isfinite (a) ? std::abs ((double) a) : 0.0;
            const auto ph = key.phase[(std::size_t) (f * u.partials + p)];
            s.phase[(std::size_t) p + 1] = std::isfinite (ph) ? (double) ph : 0.0;
        }
    }
    std::shared_ptr<const Table> t (buildMips (frames, Gain::peak, 0.0).release());
    const_cast<Table&> (*t).category = "User";
    const_cast<Table&> (*t).name = u.name;
    std::lock_guard<std::mutex> lock (userLock);
    userBuilt().emplace_back (std::move (key), t);
    return t;
}

int tableByName (const std::string& name) noexcept
{
    for (int i = 0; i < tableCount(); ++i)
        if (name == tableName (i))
            return i;
    return -1;
}

namespace
{
int indexOf (const char* name) { return std::max (0, tableByName (name)); }

struct Pair
{
    const char* live;
    const char* lynx;
};
// Live's 194 table names (its browser's categories) → the closest LYNX table. The pairs were chosen by measuring Live's
// output for each table at five positions against every LYNX table (REA_rodada190ja §4)
const Pair pairs[] = {
#include "core/WavetableMatch.inc"
};
} // namespace

std::string liveTableFor (int index)
{
    const std::string name = tableName (index);
    for (const auto& p : pairs)
        if (name == p.lynx)
            return p.live;
    return "Basic Shapes";
}

TableMatch matchLiveTable (const std::string& liveName)
{
    // (names not in the list fall back on keywords)
    for (const auto& p : pairs)
        if (liveName == p.live)
            return { indexOf (p.lynx), std::string ("Live's \"") + p.live + "\" -> LYNX table \"" + p.lynx + "\" (LYNX's own table; its frames differ)" };
    auto has = [&] (const char* k) { return liveName.find (k) != std::string::npos; };
    const char* pick = "Shapes";
    if (has ("Noise") || has ("Rain") || has ("Vinyl") || has ("Junk"))
        pick = "Pink Noise";
    else if (has ("Sync"))
        pick = "Sync Sweep";
    else if (has ("FM"))
        pick = "FM Sweep";
    else if (has ("Fold"))
        pick = "Fold";
    else if (has ("Vowel") || has ("Form") || has ("Vox") || has ("Voice"))
        pick = "Vowels";
    else if (has ("Sine"))
        pick = "Sine Steps";
    else if (has ("Pulse") || has ("Square"))
        pick = "Pulse Width";
    else if (has ("Saw"))
        pick = "Analog Saw";
    else if (has ("Harmonic"))
        pick = "Harmonic Ladder";
    else if (has ("Sweep"))
        pick = "Lowpass Saw";
    if (liveName.empty())
        return { indexOf ("Shapes"), {} };
    return { indexOf (pick), std::string ("Live's \"") + liveName + "\" -> LYNX table \"" + pick + "\" (by name; LYNX's own table, it sounds different)" };
}
} // namespace lynx::wt
