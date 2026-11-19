#pragma once

// Kilohearts snapin laws on Effector (round 190nb): the five kHs snapins LYNX had no stand-in for — Ring Mod, Reverser,
// Resonator, Phase Distortion and Shaper — as hidden laws of FL's Effector (core/Effector.h), set only by the Kilohearts
// translator (core/KiloheartsStates.inc). Their structures were read statically in Kilohearts' HeartCore 2.4 (REA,
// docs/analise/REA_rodada190nb.md) and measured on the licensed kHs Essentials (round 190nb); everything below is
// LYNX's own code restating those laws as formulas. No Kilohearts code, table or preset is used (Phase Distortion's
// 90° all-pass pair is LYNX's own minimax design; the Shaper's curve is read from the plug-in's state as points).
//
// Effector's hidden parameters (after its 12 named ones; saved only while non-zero, so every project saved before
// keeps law 0 = FL's Effector, bit for bit):
//   [12] law: k / 8 (1 Ring Mod, 2 Reverser, 3 Resonator, 4 Phase Distortion, 5 Shaper), 0 = none
//   [13 …] the law's values (encodings below); the mix is Effector's own Wet knob [2]
//   Shaper: [17] point count / 16, [18 + 2 k] x, [19 + 2 k] y as (v + 1) / 2, k < 12

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lynx::khsdsp
{
enum class Law { none = 0, ringMod, reverser, resonator, phaseDistortion, shaper };
constexpr int lawParam = 12;
constexpr int valueParam = 13; // + k
constexpr int shaperCountParam = 17, shaperPointParam = 18, maxShaperPoints = 12;
constexpr int lastParam = shaperPointParam + 2 * maxShaperPoints; // one past

inline Law lawOf (float v) noexcept
{
    const int k = (int) std::lround (v * 8.0f);
    return k >= 1 && k <= 5 ? (Law) k : Law::none;
}
inline float lawValue (Law l) noexcept { return (float) (int) l / 8.0f; }

constexpr double pi = 3.141592653589793, twoPi = 6.283185307179586;

// ---- value encodings (normalised hidden values <-> the snapins' own units) ----------------------------------------
namespace enc
{
inline double clamp01 (double v) noexcept { return std::clamp (v, 0.0, 1.0); }
// Ring Mod: [13] frequency 10 Hz · 2048^v (10 Hz … 20.48 kHz), [14] spread, [15] bias, [16] rectify (r + 1) / 2
inline double ringHz (float v) noexcept { return 10.0 * std::pow (2048.0, clamp01 (v)); }
inline float ringHzValue (double hz) noexcept { return (float) clamp01 (std::log (std::max (hz, 1e-3) / 10.0) / std::log (2048.0)); }
// Reverser: [13] time 0 … 5 s, [14] sync (1), [15] steps / 64, [16] denominator index / 6, [17] crossfade
inline double reverserBeats (int index) noexcept // 4, 4t, 8, 8t, 16, 16t, 32 (a quarter note = 1 beat)
{
    static constexpr double beats[7] { 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return beats[std::clamp (index, 0, 6)];
}
// Resonator: [13] pitch (MIDI note) / 128, [14] decay 1 ms · 10^(4v) (1 ms … 10 s), [15] intensity, [16] timbre (1 = odd)
inline double resonatorDecay (float v) noexcept { return 0.001 * std::pow (10.0, 4.0 * clamp01 (v)); }
inline float resonatorDecayValue (double s) noexcept { return (float) clamp01 (std::log10 (std::max (s, 1e-6) / 0.001) / 4.0); }
// Phase Distortion: [13] drive, [14] spread (0 … 1 = 0 … 180° between the channels), [15] normalize,
// [16] tone 20 Hz · 500^v (20 Hz … 10 kHz), [17] bias (v − 0.5 turns, ±180°)
inline double toneHz (float v) noexcept { return 20.0 * std::pow (500.0, clamp01 (v)); }
inline float toneValue (double hz) noexcept { return (float) clamp01 (std::log (std::max (hz, 1e-3) / 20.0) / std::log (500.0)); }
// Shaper: [13] drive (linear gain / 10), [14] DC filter (1), [15] loop mode / 2 (0 hold, 1 mirror, 2 repeat)
} // namespace enc

// ---- a delay line read back with a 4-point Catmull-Rom interpolation (Resonator) -----------------------------------
struct Ring
{
    std::vector<float> d;
    std::uint32_t mask = 0, w = 0;
    void allocate (std::size_t minSize)
    {
        std::size_t n = 16;
        while (n < minSize)
            n <<= 1;
        d.assign (n, 0.0f);
        mask = (std::uint32_t) n - 1;
        w = 0;
    }
    void clear() noexcept { std::fill (d.begin(), d.end(), 0.0f); w = 0; }
    bool ready() const noexcept { return ! d.empty(); }
    void push (float v) noexcept { d[w = (w + 1) & mask] = v; }
    float back (std::uint32_t i) const noexcept { return d[(w - i) & mask]; } // i = 0: the newest
    // the value `delay` samples before the next sample (delay >= 2)
    float read (double delay) const noexcept
    {
        delay = std::clamp (delay, 2.0, (double) mask - 4.0);
        const auto i0 = (std::uint32_t) delay;           // newer of the two middle taps is back (i0 - 1)
        const auto t = (float) (1.0 - (delay - (double) i0)); // position from the older middle tap
        const float p0 = back (i0 + 1), p1 = back (i0), p2 = back (i0 - 1), p3 = back (i0 - 2);
        // Catmull-Rom through p1 → p2
        return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
    }
};

// ---- the engine: one law at a time, stereo ------------------------------------------------------------------------
class Engine
{
public:
    // buffers (Reverser, Resonator) are allocated only for a slot that holds a Kilohearts law when it is created
    void prepare (double sampleRate, bool allocate)
    {
        fs = sampleRate;
        if (allocate)
        {
            for (auto& c : rev)
                c.allocate ((std::size_t) (4.0 * 5.0 * fs) + 16);
            for (auto& c : res)
                c.allocate ((std::size_t) (fs / 8.0) + 16);
        }
        reset();
    }
    void reset() noexcept
    {
        for (auto& c : rev)
            c.clear();
        for (auto& c : res)
            c.clear();
        ringPhase = 0.0;
        ringAsleep = true;
        ringSilent = 0;
        revA = 0.0;
        revCountA = -1;
        revCountB = -1;
        revFill = 0;
        revWaiting = true;
        for (auto& h : hil)
            h = {};
        for (auto& b : tone)
            b = {};
        shaperState = {};
    }
    void setSongBeats (double b) noexcept { songBeats = b; songKnown = true; }

    void process (Law law, float* l, float* r, int n, const float* p, float mix, double bpm) noexcept
    {
        switch (law)
        {
            case Law::ringMod: ringMod (l, r, n, p, mix); break;
            case Law::reverser: reverser (l, r, n, p, mix, bpm); break;
            case Law::resonator: resonator (l, r, n, p, mix); break;
            case Law::phaseDistortion: phaseDistortion (l, r, n, p, mix); break;
            case Law::shaper: shaper (l, r, n, p, mix); break;
            default: break;
        }
    }

private:
    double fs = 48000.0, songBeats = 0.0;
    bool songKnown = false;

    // ---- Ring Mod ------------------------------------------------------------------------------------------------
    // y = x · (1 + mix · (bias − 1 + f (m))), m = sin (2π (φ ± spread / 4)) (the right channel's phase −, the left's +);
    // f: the rectifier, gains min (1, 1 + 2 rect) on the positive half and min (1, 1 − 2 rect) on the negative half
    // (rect = 1: full-wave |m|, 0.5: half-wave, −1: −|m|)
    // [MEASURED] the oscillator leads the input by 3.5 samples (half its 2× oversampler's 7-sample latency), and it
    // sleeps on silence like the plug-in: from a reset until the first block with sound, and from the second all-zero
    // block on (the first one still runs)
    double ringPhase = 0.0;
    bool ringAsleep = true;
    int ringSilent = 0;
    void ringMod (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        bool silent = true;
        for (int i = 0; i < n && silent; ++i)
            silent = l[i] == 0.0f && r[i] == 0.0f;
        if (silent)
        {
            if (ringAsleep || ++ringSilent > 1)
            {
                ringAsleep = true;
                return; // (the output is the silent input times anything)
            }
        }
        else
        {
            ringAsleep = false;
            ringSilent = 0;
        }
        const double inc = enc::ringHz (p[valueParam]) / fs;
        const double lead = 3.5 * enc::ringHz (p[valueParam]) / fs;
        const double spread = 0.25 * std::clamp ((double) p[valueParam + 1], 0.0, 1.0);
        const double bias = std::clamp ((double) p[valueParam + 2], 0.0, 1.0);
        const double rect = 2.0 * std::clamp ((double) p[valueParam + 3], 0.0, 1.0) - 1.0;
        const double gp = std::min (1.0, 1.0 + 2.0 * rect), gn = std::min (1.0, 1.0 - 2.0 * rect);
        auto f = [&] (double m) { return m > 0.0 ? gp * m : gn * m; };
        for (int i = 0; i < n; ++i)
        {
            const double ml = f (std::sin (twoPi * (ringPhase + lead + spread))), mr = f (std::sin (twoPi * (ringPhase + lead - spread)));
            l[i] = (float) (l[i] * (1.0 + mix * (bias - 1.0 + ml)));
            r[i] = (float) (r[i] * (1.0 + mix * (bias - 1.0 + mr)));
            ringPhase += inc;
            ringPhase -= std::floor (ringPhase);
        }
    }

    // ---- Reverser ------------------------------------------------------------------------------------------------
    // Sections of L samples (the time, or steps × note at the tempo, locked to the song position when synced). Two
    // heads read the input backwards at twice the write speed from each section's start: the new head plays the last
    // L samples reversed, the old one goes on into the section before and fades out; equal-power fades over the
    // crossfade's share of the section: new = sin θ, old = cos θ, θ = π/2 · min (1, phase / crossfade).
    std::array<Ring, 2> rev;
    double revA = 0.0; // the section's phase 0 … 1
    long long revCountA = -1, revCountB = -1;
    std::uint32_t revFill = 0;
    bool revWaiting = true;
    void reverser (float* l, float* r, int n, const float* p, float mix, double bpm) noexcept
    {
        if (! rev[0].ready())
            return;
        const bool sync = p[valueParam + 1] >= 0.5f;
        double L, beats = 1.0;
        if (sync)
        {
            beats = std::max (1.0, std::round (64.0 * std::clamp ((double) p[valueParam + 2], 0.0, 1.0)))
                    * enc::reverserBeats ((int) std::lround (6.0 * std::clamp ((double) p[valueParam + 3], 0.0, 1.0)));
            L = beats * 60.0 / std::max (bpm, 1.0) * fs;
        }
        else
            L = 5.0 * std::clamp ((double) p[valueParam], 0.0, 1.0) * fs;
        L = std::clamp (L, 1.0, ((double) rev[0].mask - 8.0) / 4.0);
        // [MEASURED] synced sections start two samples after the song's grid; the heads read two samples behind
        constexpr double gridOffset = 2.0;
        auto lockToSong = [&] (int i) {
            const double pos = songBeats + (double) i * std::max (bpm, 1.0) / (60.0 * fs);
            double ph = pos / beats - gridOffset / L;
            ph -= std::floor (ph);
            revA = ph;
            revCountA = (long long) std::floor (ph * L);
            revCountB = revCountA + (long long) std::floor (L);
        };
        // a synced Reverser follows a jump of the song position at once
        if (sync && songKnown && ! revWaiting)
        {
            double ph = songBeats / beats - gridOffset / L;
            ph -= std::floor (ph);
            const double d = std::abs (ph - revA);
            if (d > 2.0 / L && d < 1.0 - 2.0 / L)
                lockToSong (0);
        }
        const double xf = std::clamp ((double) p[valueParam + 4], 0.0, 1.0);
        const double step = 1.0 / L;
        constexpr float quiet = 9.5367431640625e-07f; // 2^-20
        for (int i = 0; i < n; ++i)
        {
            // [MEASURED] after a reset it waits for the first sample above −120 dB (2^-20), and its sections start there
            // (or on the song's grid when synced)
            if (revWaiting)
            {
                if (std::abs (l[i]) < quiet && std::abs (r[i]) < quiet)
                    continue;
                revWaiting = false;
                if (sync && songKnown)
                    lockToSong (i);
                else
                {
                    revA = 0.0;
                    revCountA = 0;
                    revCountB = (long long) std::floor (L);
                }
            }
            if (revA >= 1.0)
            {
                revCountB = revCountA;
                revCountA = 0;
                revA = 0.0;
            }
            rev[0].push (l[i]);
            rev[1].push (r[i]);
            revFill = std::min (revFill + 1, rev[0].mask);
            const auto a = (std::uint32_t) std::min<long long> (2 * revCountA + 2, revFill - 1);
            const auto b = (std::uint32_t) std::min<long long> (2 * revCountB + 2, revFill - 1);
            const double th = xf <= 0.0 ? 0.5 * pi : 0.5 * pi * std::clamp (revA / xf, 0.0, 1.0);
            const float gNew = (float) std::sin (th), gOld = (float) std::cos (th);
            const float yl = gNew * rev[0].back (a) + gOld * rev[0].back (b);
            const float yr = gNew * rev[1].back (a) + gOld * rev[1].back (b);
            l[i] += mix * (yl - l[i]);
            r[i] += mix * (yr - r[i]);
            ++revCountA;
            ++revCountB;
            revA += step;
        }
        // (the plug-in also goes back to sleep after long silences — its grid then restarts with the next sound, a few
        // samples off this model's; not modelled: the measured renders score best without it)
    }

    // ---- Resonator -----------------------------------------------------------------------------------------------
    // A feedback comb of one period P = 1 / (c f) (c = 2 for the odd series: the feedback then changes sign), its
    // feedback g = ±2^(−P / decay); the output blends the comb's tap r and its input w = x + g r:
    // wet = ∓ (g (intensity − 1) w + r) (intensity 0: an all-pass, 1: the full resonance), out = (1 − mix) x + mix wet
    std::array<Ring, 2> res;
    void resonator (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        if (! res[0].ready())
            return;
        const bool odd = p[valueParam + 3] >= 0.5f;
        const double hz = 440.0 * std::pow (2.0, (128.0 * (double) p[valueParam] - 69.0) / 12.0) * (odd ? 2.0 : 1.0);
        const double period = 1.0 / std::max (hz, 1.0);
        double g = std::exp2 (-period / std::max (1e-6, enc::resonatorDecay (p[valueParam + 1])));
        if (odd)
            g = -g;
        const double k = g * (std::clamp ((double) p[valueParam + 2], 0.0, 1.0) - 1.0);
        const double delay = period * fs;
        const double wet = odd ? mix : -mix;
        float* io[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            auto& line = res[(std::size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const double x = io[c][i];
                const double tap = line.read (delay);
                const double w = x + g * tap;
                line.push ((float) w);
                io[c][i] = (float) (wet * (k * w + tap) + (1.0 - mix) * x);
            }
        }
    }

    // ---- Phase Distortion ----------------------------------------------------------------------------------------
    // The input's analytic pair (re, im) from a 90° all-pass pair; the modulator m = LP_tone (g · re), g = 3 + normalize
    // · (1 / |re + j im| − 3); the pair rotated by φ = 4π drive² m + bias ∓ spread / 2: y = cos φ · re − sin φ · im
    struct Hilbert
    {
        std::array<double, 4> xa {}, ya {}, xb {}, yb {}; // per section: x[n-1], x[n-2] and y likewise (in z^-2 form)
        std::array<double, 4> xa2 {}, ya2 {}, xb2 {}, yb2 {};
        double bDelay = 0.0;
    };
    std::array<Hilbert, 2> hil {};
    struct Biquad
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };
    std::array<Biquad, 2> tone {};
    static double section (double a, double x, double& x1, double& x2, double& y1, double& y2) noexcept
    {
        // (a − z^-2) / (1 − a z^-2)
        const double y = a * x - x2 + a * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
    void analytic (Hilbert& h, double x, double& re, double& im) noexcept
    {
        // LYNX's own minimax pair (lab/hilbert.py, round 190nb): 2.6° worst error over 15 Hz … 0.46 fs at 48 kHz
        static constexpr double A[4] { 0.2103138494, 0.8090148606, 0.9688736554, 0.9928531669 };
        static constexpr double B[4] { 0.5546664279, 0.9274817547, 0.9841642629, 0.9981473638 };
        double a = x, b = x;
        for (int k = 0; k < 4; ++k)
        {
            a = section (A[k], a, h.xa[(std::size_t) k], h.xa2[(std::size_t) k], h.ya[(std::size_t) k], h.ya2[(std::size_t) k]);
            b = section (B[k], b, h.xb[(std::size_t) k], h.xb2[(std::size_t) k], h.yb[(std::size_t) k], h.yb2[(std::size_t) k]);
        }
        re = a;
        im = h.bDelay; // branch B one sample later
        h.bDelay = b;
    }
    void phaseDistortion (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        const double drive = std::clamp ((double) p[valueParam], 0.0, 1.0);
        const double depth = 4.0 * pi * drive * drive;
        const double spread = 0.5 * pi * std::clamp ((double) p[valueParam + 1], 0.0, 1.0);
        const double norm = std::clamp ((double) p[valueParam + 2], 0.0, 1.0);
        const double bias = twoPi * (std::clamp ((double) p[valueParam + 4], 0.0, 1.0) - 0.5);
        // tone: a 2-pole low pass (Q 1/√2, bilinear)
        const double fc = std::min (enc::toneHz (p[valueParam + 3]), 0.45 * fs);
        const double w0 = twoPi * fc / fs, cs = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + al, b0 = (1.0 - cs) / 2.0 / a0, b1 = (1.0 - cs) / a0, a1 = -2.0 * cs / a0, a2 = (1.0 - al) / a0;
        float* io[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            const double off = c == 0 ? bias - spread : bias + spread;
            auto& h = hil[(std::size_t) c];
            auto& t = tone[(std::size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const double x = io[c][i];
                double re, im;
                analytic (h, x, re, im);
                const double g = 3.0 + norm * (1.0 / std::sqrt (re * re + im * im + 1e-6) - 3.0);
                const double v = g * re;
                const double m = b0 * v + b1 * t.x1 + b0 * t.x2 - a1 * t.y1 - a2 * t.y2;
                t.x2 = t.x1;
                t.x1 = v;
                t.y2 = t.y1;
                t.y1 = m;
                const double ph = depth * m + off;
                const double y = std::cos (ph) * re - std::sin (ph) * im;
                io[c][i] = (float) (x + mix * (y - x));
            }
        }
    }

    // ---- Shaper --------------------------------------------------------------------------------------------------
    // x · drive folded into −1 … 1 by the loop mode (hold: clipped, mirror: reflected, repeat: wrapped), through the
    // curve (its points joined by straight lines; none = the identity), then the DC filter
    static double fold (double v, int mode) noexcept
    {
        if (mode == 0)
            return std::clamp (v, -1.0, 1.0);
        if (mode == 1)
        {
            double u = std::fmod (v + 1.0, 4.0);
            if (u < 0.0)
                u += 4.0;
            return u <= 2.0 ? u - 1.0 : 3.0 - u;
        }
        double u = std::fmod (v + 1.0, 2.0);
        if (u < 0.0)
            u += 2.0;
        return u - 1.0;
    }
    void shaper (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        const double drive = 10.0 * std::clamp ((double) p[valueParam], 0.0, 1.0);
        const bool dc = p[valueParam + 1] >= 0.5f;
        const int mode = std::clamp ((int) std::lround (2.0 * p[valueParam + 2]), 0, 2);
        const int count = std::clamp ((int) std::lround (16.0 * p[shaperCountParam]), 0, maxShaperPoints);
        double px[maxShaperPoints] {}, py[maxShaperPoints] {};
        for (int k = 0; k < count; ++k)
        {
            px[k] = 2.0 * p[shaperPointParam + 2 * k] - 1.0;
            py[k] = 2.0 * p[shaperPointParam + 2 * k + 1] - 1.0;
        }
        auto curve = [&] (double v) {
            if (count < 2)
                return v;
            if (v <= px[0])
                return py[0];
            for (int k = 1; k < count; ++k)
                if (v <= px[k])
                    return px[k] > px[k - 1] ? py[k - 1] + (py[k] - py[k - 1]) * (v - px[k - 1]) / (px[k] - px[k - 1]) : py[k];
            return py[count - 1];
        };
        // [MEASURED on kHs Shaper's identity curve] its 2× oversampler: ≈ 3.5 samples of delay (here 1.5: 3-tap droop
        // + a half-sample Thiran all-pass; the integer part is the alignment's) with a gentle top-octave droop
        // (−0.6 dB at 10 kHz, −2 dB at 20 kHz); the DC filter: a 30 Hz 2-pole (Butterworth) high pass
        constexpr double droop = 0.048, thiran = 1.0 / 3.0;
        const double w0 = twoPi * std::min (30.0, 0.45 * fs) / fs, cs = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + al, hb0 = (1.0 + cs) / 2.0 / a0, hb1 = -(1.0 + cs) / a0, ha1 = -2.0 * cs / a0, ha2 = (1.0 - al) / a0;
        float* io[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            auto& st = shaperState[(std::size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const double in = io[c][i];
                const double fir = droop * (in + st.f2) + (1.0 - 2.0 * droop) * st.f1;
                st.f2 = st.f1;
                st.f1 = in;
                const double x = thiran * fir + st.t1 - thiran * st.ty; // (a + z^-1) / (1 + a z^-1)
                st.t1 = fir;
                st.ty = x;
                double y = curve (fold (x * drive, mode));
                if (dc)
                {
                    const double h = hb0 * y + hb1 * st.x1 + hb0 * st.x2 - ha1 * st.y1 - ha2 * st.y2;
                    st.x2 = st.x1;
                    st.x1 = y;
                    st.y2 = st.y1;
                    st.y1 = h;
                    y = h;
                }
                io[c][i] = (float) (x + mix * (y - x));
            }
        }
    }
    struct ShaperState
    {
        double f1 = 0, f2 = 0, t1 = 0, ty = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };
    std::array<ShaperState, 2> shaperState {};
};
} // namespace lynx::khsdsp
