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
//   [12] law: code / 64 — 8 Ring Mod, 16 Reverser, 24 Resonator, 32 Phase Distortion, 40 Shaper (round 190nb's k / 8,
//        the same floats), round 190nh: 41 Comb Filter, 42 Chorus, 43 Trance Gate, 44 Distortion, 45 Bitcrush,
//        46 Frequency Shifter, 47 Phaser, 48 Ensemble, 49 Nonlinear Filter; 0 = none
//   [13 …] the law's values (encodings below); the mix is Effector's own Wet knob [2]
//   Shaper: [17] point count / 16, [18 + 2 k] x, [19 + 2 k] y as (v + 1) / 2, k < 12
//   Trance Gate: [24 + k] step k as (on + 2 tie) / 3, k < 64

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lynx::khsdsp
{
enum class Law { none = 0, ringMod, reverser, resonator, phaseDistortion, shaper,
                 comb, chorus, tranceGate, distortion, bitcrush, freqShift, phaser, ensemble, nonlinearFilter };
constexpr int lawParam = 12;
constexpr int valueParam = 13; // + k
constexpr int shaperCountParam = 17, shaperPointParam = 18, maxShaperPoints = 12;
constexpr int gateStepParam = 24, maxGateSteps = 64;
constexpr int lastParam = gateStepParam + maxGateSteps; // one past

// the law's code in [12] (× 64): round 190nb's five keep their k / 8 values
inline int lawCode (Law l) noexcept
{
    const int k = (int) l;
    return k <= 5 ? 8 * k : 41 + (k - 6);
}
inline Law lawOf (float v) noexcept
{
    const int c = (int) std::lround (v * 64.0f);
    if (c >= 8 && c <= 40 && c % 8 == 0)
        return (Law) (c / 8);
    return c >= 41 && c <= 49 ? (Law) (6 + c - 41) : Law::none;
}
inline float lawValue (Law l) noexcept { return (float) lawCode (l) / 64.0f; }
// the latency a law reports (the comb filter's 16-tap sinc read is centred 7 samples late, dry and wet alike)
inline int lawLatency (Law l) noexcept { return l == Law::comb ? 7 : 0; }

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
// round 190nh: a value on a log scale lo … hi, and a time 0 … 10 s as √(t / 10)
inline double logv (float v, double lo, double hi) noexcept { return lo * std::pow (hi / lo, clamp01 (v)); }
inline float logValue (double x, double lo, double hi) noexcept { return (float) clamp01 (std::log (std::max (x, lo) / lo) / std::log (hi / lo)); }
inline double timeS (float v) noexcept { const double c = clamp01 (v); return 10.0 * c * c; }
inline float timeValue (double t) noexcept { return (float) clamp01 (std::sqrt (std::max (t, 0.0) / 10.0)); }
// Comb Filter: [13] cutoff logv 10 Hz … 24 kHz, [14] polarity minus (1), [15] stereo (1)
constexpr double combLo = 10.0, combHi = 24000.0;
// Chorus: [13] delay timeS, [14] rate logv 0.01 … 20 Hz, [15] depth timeS, [16] spread, [17] three taps (1)
constexpr double rateLo = 0.01, rateHi = 20.0;
// Trance Gate: [13] attack, [14] decay timeS, [15] sustain, [16] release timeS, [17] resolution index / 6 (2, 3, 4, 6,
// 8, 12, 16 steps a beat), [18] swing, [19] pattern length / 64; the Mix (depth) is Wet
// Phaser: [13] order / 7 (2nd-order all-pass stages), [14] cutoff as the plug-in's own position 40 Hz · 250^v,
// [15] depth, [16] rate logv 0.01 … 20 Hz, [17] spread
// Distortion: [13] drive logv 0.25 … 16 (linear gain), [14] bias (v + 1) / 2, [15] spread, [16] type / 5 (overdrive,
// saturate, foldback, sine, hard clip, quantize), [17] dynamics, [18] DC filter (1)
constexpr double driveLo = 0.25, driveHi = 16.0;
// Bitcrush: [13] sample rate logv 100 Hz … 50 kHz, [14] bits / 16, [15] ADC quality, [16] DAC quality, [17] dither,
// [18] quantize; the mix is Wet
constexpr double crushLo = 100.0, crushHi = 50000.0;
// Frequency Shifter: [13] shift (v − ½) · 10 kHz (±5 kHz)
// Ensemble: [13] voices / 16, [14] detune, [15] spread, [16] motion / 2 (random, symmetric, sine); the mix is Wet
// round 190oc — Nonlinear Filter: [13] type / 4 (low, band, high pass, notch, all-pass), [14] mode / 8 (clean, saturated,
// tubular, clipped, warm, biased, fuzzy, metallic, digital: the plug-in's order), [15] cutoff logv 20 … 20 480 Hz,
// [16] Q logv 0.5 … 24, [17] drive; the mix is Wet
constexpr double nlfCutLo = 20.0, nlfCutHi = 20480.0, nlfQLo = 0.5, nlfQHi = 24.0;
inline double gateStepsPerBeat (float v) noexcept
{
    static constexpr double steps[7] { 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0 };
    return steps[std::clamp ((int) std::lround (6.0 * clamp01 (v)), 0, 6)];
}
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
            for (auto& c : line) // round 190nh: comb (1 / 10 Hz), chorus (0.1 + 0.1 s) and ensemble (31 ms) lines
                c.allocate ((std::size_t) (0.25 * fs) + 64);
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
        for (auto& c : line)
            c.clear();
        combFill = 0;
        lfoPhase = 0.0f;
        gatePos = 0.0;
        gateStep = -1;
        gateEnvState = 4;
        gateEnv = 0.0f;
        gateOpen = false;
        lastSong = -1e300;
        quietRun = 1LL << 40;
        phaserPhase = 0.0f;
        for (auto& st : phaserStages)
            st = {};
        for (auto& o : overs)
            o.clear();
        distEnv[0] = distEnv[1] = 1e-4f;
        distPrev[0] = distPrev[1] = 0.0f;
        distPrevSet = false;
        distUp.clear();
        distDown.clear();
        for (auto& b : distDc)
            b = {};
        crushSeed = 1;
        crush = {};
        shiftPhase = 0.0;
        for (auto& h : shiftHp)
            h.clear();
        shiftHpFor = -1.0;
        ensPos = 0.0;
        for (auto& v : ensAp)
            v = {};
        nlfReset();
    }
    void setSongBeats (double b) noexcept { songBeats = b; songKnown = true; }
    // round 190oc: LYNX's own half-band (the Nonlinear Filter's 2× resampler): its all-pass coefficients, A0's at the even
    // places, A1's at the odd ones (tests, docs)
    static std::vector<double> nonlinearHalfBand() { const auto& a = nlfHalfBand().a; return { a.begin(), a.end() }; }
    // round 190nh: the Trance Gate follows a song position only when it moves (a stopped transport: free running)

    void process (Law law, float* l, float* r, int n, const float* p, float mix, double bpm) noexcept
    {
        switch (law)
        {
            case Law::ringMod: ringMod (l, r, n, p, mix); break;
            case Law::reverser: reverser (l, r, n, p, mix, bpm); break;
            case Law::resonator: resonator (l, r, n, p, mix); break;
            case Law::phaseDistortion: phaseDistortion (l, r, n, p, mix); break;
            case Law::shaper: shaper (l, r, n, p, mix); break;
            case Law::comb: comb (l, r, n, p, mix); break;
            case Law::chorus: chorus (l, r, n, p, mix); break;
            case Law::tranceGate: tranceGate (l, r, n, p, mix, bpm); break;
            case Law::phaser: phaser (l, r, n, p, mix); break;
            case Law::distortion: distortion (l, r, n, p, mix); break;
            case Law::bitcrush: bitcrush (l, r, n, p, mix); break;
            case Law::freqShift: freqShift (l, r, n, p, mix); break;
            case Law::ensemble: ensemble (l, r, n, p, mix); break;
            case Law::nonlinearFilter: nonlinearFilter (l, r, n, p, mix); break;
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

    // ==== round 190nh =============================================================================================
    // Read statically in HeartCore 2.4 (CombFilterDsp, ChorusDsp, TranceGateDsp; docs/analise/REA_rodada190nh.md) and
    // measured on kHs Essentials; LYNX's own code restating the laws as formulas.
    std::array<Ring, 2> line; // comb / chorus delay lines
    std::uint32_t combFill = 0;

    // ---- Comb Filter -------------------------------------------------------------------------------------------
    // y = x (t − 7) + g · x (t − 7 − P), P = fs / cutoff (whole samples above 1000), read through a 16-tap sinc in a Hann
    // window 8 samples wide on each side (the 7 samples: the read's look-ahead; the plug-in reports them as latency);
    // g = mix, negative with Minus polarity; Stereo inverts the right channel's g
    std::array<double, 16> combW {};
    double combWFor = -1.0;
    void comb (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        if (! line[0].ready())
            return;
        double period = fs / enc::logv (p[valueParam], enc::combLo, enc::combHi);
        if (period > 1000.0)
            period = std::floor (period + 0.5);
        const double d = std::min (period + 7.0, (double) line[0].mask - 24.0);
        const auto i0 = (std::uint32_t) d;
        const double f = d - (double) i0;
        if (f != combWFor)
        {
            combWFor = f;
            for (int k = 0; k < 16; ++k)
            {
                const double u = f - 8.0 + k; // the tap's distance from the read point
                const double win = 0.5 * std::cos (std::clamp (u * pi / 8.0, -pi, pi)) + 0.5;
                combW[(std::size_t) k] = win * (u == 0.0 ? 1.0 : std::sin (pi * u) / (pi * u));
            }
        }
        const double gl = (p[valueParam + 1] >= 0.5f ? -1.0 : 1.0) * mix;
        const double gr = (p[valueParam + 2] >= 0.5f ? -1.0 : 1.0) * gl;
        for (int i = 0; i < n; ++i)
        {
            line[0].push (l[i]);
            line[1].push (r[i]);
            combFill = std::min (combFill + 1, line[0].mask);
            double yl = 0.0, yr = 0.0;
            for (int k = 0; k < 16; ++k)
            {
                const auto age = i0 + 8 - (std::uint32_t) k; // tap k = the sample (i0 + 8 − k) back
                yl += combW[(std::size_t) k] * line[0].back (age);
                yr += combW[(std::size_t) k] * line[1].back (age);
            }
            l[i] = (float) (line[0].back (7) + gl * yl);
            r[i] = (float) (line[1].back (7) + gr * yr);
        }
    }

    // ---- Chorus ------------------------------------------------------------------------------------------------
    // One sine LFO φ (from 0 at a reset, += 2π rate / fs before each sample, kept in float and wrapped once a block as
    // the plug-in does, so its phase stays sample-true over minutes); a Catmull-Rom read of each channel's line.
    // Two taps: the delay d_L = (½ sin φ + ½) depth + delay + 1 / fs, d_R at φ + spread · π; out = x + ½ mix (wet − x).
    // Three taps (multiTap): A = (½ sin φ + ½) depth + 1 / fs, B = delay / 2 + 1 / fs + (½ sin (φ + 2π/3) + ½) depth,
    // C = delay + 1 / fs + (½ sin (φ − 2π/3) + ½) depth; wet_L = 0.6 (B_L + A_R + (1 − spread) C_L),
    // wet_R = 0.6 (C_R + A_L + (1 − spread) B_R); out = x + mix (wet − x).
    float lfoPhase = 0.0f;
    static float catmull (const Ring& c, double delaySamples) noexcept
    {
        // the plug-in's read: taps from (int d + 2) back, t = 1 − frac (from the older middle tap)
        const auto i0 = (std::uint32_t) delaySamples;
        const float t = (float) (1.0 - (delaySamples - (double) i0)), t2 = t * t;
        const float p0 = c.back (i0 + 2), p1 = c.back (i0 + 1), p2 = c.back (i0), p3 = c.back (i0 - 1);
        return (-0.5f * t + (-0.5f * t + 1.0f) * t2) * p0 + (1.0f + (1.5f * t - 2.5f) * t2) * p1
               + (0.5f * t + (-1.5f * t + 2.0f) * t2) * p2 + ((0.5f * t - 0.5f) * t2) * p3;
    }
    // [MEASURED] HeartCore puts a snapin to sleep while its input has been silent for longer than its tail (from a reset
    // too): the block is then not processed, and an LFO only advances by the block's length in one step
    long long quietRun = 1LL << 40; // zero input samples before this block
    bool sleeping (const float* l, const float* r, int n, double tailSamples) noexcept
    {
        int last = -1;
        for (int i = n - 1; i >= 0 && last < 0; --i)
            if (l[i] != 0.0f || r[i] != 0.0f)
                last = i;
        const bool asleep = last < 0 && (double) quietRun >= tailSamples;
        quietRun = last < 0 ? std::min (quietRun + n, 1LL << 40) : (long long) (n - 1 - last);
        return asleep;
    }
    static constexpr float twoPiF = 6.2831854820251465f, invTwoPiF = 0.15915493667125702f;
    static void lfoAdvance (float& ph, int n, float rate, double fs) noexcept
    {
        const float v = (float) n * twoPiF * rate / (float) fs + ph;
        ph = v - std::floor (invTwoPiF * v) * twoPiF;
    }
    void chorus (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        if (! line[0].ready())
            return;
        const double delay = enc::timeS (p[valueParam]), depth = enc::timeS (p[valueParam + 2]);
        const float rate = (float) enc::logv (p[valueParam + 1], enc::rateLo, enc::rateHi);
        const double spread = std::clamp ((double) p[valueParam + 3], 0.0, 1.0);
        const bool taps3 = p[valueParam + 4] >= 0.5f;
        if (sleeping (l, r, n, (delay + depth) * fs + 3.0))
        {
            for (int i = 0; i < n; ++i)
                line[0].push (0.0f), line[1].push (0.0f);
            lfoAdvance (lfoPhase, n, rate, fs);
            return;
        }
        const float inc = rate * twoPiF / (float) fs;
        lfoPhase -= std::floor (lfoPhase * invTwoPiF) * twoPiF; // the block's wrap
        const double maxD = (double) line[0].mask - 8.0, oneS = 1.0 / fs;
        auto at = [&] (double seconds) { return std::clamp (seconds * fs, 1.0, maxD); };
        for (int i = 0; i < n; ++i)
        {
            lfoPhase += inc;
            line[0].push (l[i]);
            line[1].push (r[i]);
            const double ph = (double) lfoPhase;
            if (! taps3)
            {
                const double dl = (0.5 * std::sin (ph) + 0.5) * depth + delay + oneS;
                const double dr = (0.5 * std::sin (ph + spread * pi) + 0.5) * depth + delay + oneS;
                l[i] += 0.5f * mix * (catmull (line[0], at (dl)) - l[i]);
                r[i] += 0.5f * mix * (catmull (line[1], at (dr)) - r[i]);
            }
            else
            {
                const double a = (0.5 * std::sin (ph) + 0.5) * depth + oneS;
                const double b = 0.5 * delay + oneS + (0.5 * std::sin (ph + 2.0 * pi / 3.0) + 0.5) * depth;
                const double c = delay + oneS + (0.5 * std::sin (ph - 2.0 * pi / 3.0) + 0.5) * depth;
                const auto ks = (float) (1.0 - spread);
                const float wl = 0.6f * (catmull (line[0], at (b)) + catmull (line[1], at (a)) + ks * catmull (line[0], at (c)));
                const float wr = 0.6f * (catmull (line[1], at (c)) + catmull (line[0], at (a)) + ks * catmull (line[1], at (b)));
                l[i] += mix * (wl - l[i]);
                r[i] += mix * (wr - r[i]);
            }
        }
    }

    // ---- Trance Gate -------------------------------------------------------------------------------------------
    // The step under the play head: u = song beats (followed while the song position moves, else running on its own),
    // swing warps each beat (its first half [0, swing) → [0, ½)), step = ⌊(⌊u⌋ + warp (frac u)) · steps a beat⌋ mod
    // length. Entering an "on" step opens the gate and restarts the attack (from the envelope's current value) unless
    // the step before was tied into it with the gate open; an "off" step closes it. The envelope: a linear attack, then
    // an exponential decay towards the sustain (e^(−3 t / decay)), on release e^(−3 t / release) (and never above the
    // decay's curve), off below 2^-20. gain = 1 + mix (env − 1) on both channels.
    double gatePos = 0.0, lastSong = -1e300;
    int gateStep = -1, gateEnvState = 4; // 0 attack, 1 decay, 2 sustain, 3 release, 4 off
    float gateEnv = 0.0f;
    bool gateOpen = false;
    float gateEnvelope (float dt, float a, float d, float s, float rl) noexcept
    {
        float& v = gateEnv;
        switch (gateEnvState)
        {
            case 0:
                if (gateOpen)
                {
                    const float rate = std::abs (a) >= 1e-10f ? dt / a : 1.0f;
                    const float step = std::min (1.0f, (1.0f - v) / rate);
                    v += rate * step;
                    if (step == 1.0f)
                        return v;
                    dt -= step * dt;
                }
                gateEnvState = 1;
                [[fallthrough]];
            case 1:
                if (s < 1.0f && gateOpen)
                {
                    const float k = d != 0.0f ? std::exp (dt * -3.0f / d) : 0.0f;
                    v = (v - s) * k + s;
                    return v;
                }
                gateEnvState = 2;
                [[fallthrough]];
            case 2:
                if (gateOpen)
                {
                    v = s;
                    return v;
                }
                gateEnvState = 3;
                [[fallthrough]];
            case 3:
            {
                if (v <= s)
                {
                    const float k = rl != 0.0f ? std::exp (dt * -3.0f / rl) : 0.0f;
                    v *= k;
                }
                else
                {
                    const float kd = d != 0.0f ? std::exp (-3.0f * dt / d) : 0.0f;
                    const float kr = rl != 0.0f ? std::exp (dt * -3.0f / rl) : 0.0f;
                    v = std::min (kd * (v - s) + s, kr * v);
                }
                if (v > 9.5367431640625e-07f)
                    return v;
                gateEnvState = 4;
                [[fallthrough]];
            }
            default:
                v = 0.0f;
                return v;
        }
    }
    void tranceGate (float* l, float* r, int n, const float* p, float mix, double bpm) noexcept
    {
        const float a = (float) enc::timeS (p[valueParam]), d = (float) enc::timeS (p[valueParam + 1]);
        const float s = (float) std::clamp ((double) p[valueParam + 2], 0.0, 1.0), rl = (float) enc::timeS (p[valueParam + 3]);
        const double spb = enc::gateStepsPerBeat (p[valueParam + 4]);
        const double swing = std::clamp ((double) p[valueParam + 5], 0.01, 0.99);
        const int len = std::clamp ((int) std::lround (64.0 * p[valueParam + 6]), 1, maxGateSteps);
        auto stepOn = [&] (int k) { return (int) std::lround (3.0 * p[gateStepParam + k]) & 1; };
        auto stepTie = [&] (int k) { return ((int) std::lround (3.0 * p[gateStepParam + k]) & 2) != 0; };
        if (songKnown && songBeats != lastSong)
        {
            gatePos = songBeats * spb; // the host's play head (in steps)
            lastSong = songBeats;
        }
        const double inc = std::max (bpm, 1.0) / 60.0 * spb / fs;
        const float dt = (float) (1.0 / fs);
        for (int i = 0; i < n; ++i)
        {
            const double u = gatePos / spb, whole = std::floor (u), fr = u - whole;
            const double w = fr >= swing ? (fr - swing) * 0.5 / (1.0 - swing) + 0.5 : fr * 0.5 / swing;
            int k = (int) std::floor ((whole + w) * spb);
            k = ((k % len) + len) % len;
            gatePos += inc;
            if (k != gateStep)
            {
                const bool notTied = gateStep < 0 || ! stepTie (gateStep);
                gateStep = k;
                if (stepOn (k) && (! gateOpen || notTied))
                {
                    gateOpen = true;
                    gateEnvState = 0;
                }
                else if (! stepOn (k))
                    gateOpen = false;
            }
            const float env = gateEnvelope (dt, a, d, s, rl);
            const float g = (env - 1.0f) * mix + 1.0f;
            l[i] *= g;
            r[i] *= g;
        }
    }

    // ---- Phaser ------------------------------------------------------------------------------------------------
    // A triangle LFO updated once a block (its phase in turns: φ += rate · n / fs at each block's start, in float),
    // tri (u) = 4 · (u ≤ ½ ? u : 1 − u) − 1, the right channel at φ + spread / 2. The cutoff moves on the plug-in's own
    // log scale: f = 40 Hz · 250^(c + depth / 2 · tri) (c: the Cutoff's position), at most 0.49 fs. "order" 2nd-order
    // all-passes (the cookbook's, Q = 1/√2) in a row, run as state-variable filters whose g = tan (π f / fs) moves
    // linearly across the block from the last block's value; out = x + mix / 2 · (all-pass − x).
    float phaserPhase = 0.0f;
    struct PhaserStage
    {
        double s1[2] {}, s2[2] {}, g[2] {};
        bool fresh = true;
    };
    std::array<PhaserStage, 7> phaserStages {};
    void phaser (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        const int order = std::clamp ((int) std::lround (7.0 * p[valueParam]), 1, 7);
        const float c = (float) std::clamp ((double) p[valueParam + 1], 0.0, 1.0);
        const float depth = (float) std::clamp ((double) p[valueParam + 2], 0.0, 1.0);
        const float rate = (float) enc::logv (p[valueParam + 3], enc::rateLo, enc::rateHi);
        const float spread = (float) std::clamp ((double) p[valueParam + 4], 0.0, 1.0);
        phaserPhase -= std::floor (phaserPhase);
        phaserPhase = rate * (float) n / (float) fs + phaserPhase;
        auto tri = [] (float v) { const float u = v - std::floor (v); return (0.5f < u ? 1.0f - u : u) * 4.0f - 1.0f; };
        const float lo = 3.6888794541139363f, span = 5.521460917862246f; // ln 40, ln 250
        const double fmax = 0.49 * fs;
        const double fq[2] { std::min ((double) std::exp ((tri (phaserPhase) * (depth * 0.5f) + c) * span + lo), fmax),
                             std::min ((double) std::exp ((tri (spread * 0.5f + phaserPhase) * (depth * 0.5f) + c) * span + lo), fmax) };
        double gNew[2];
        for (int ch = 0; ch < 2; ++ch)
            gNew[ch] = std::tan (pi * std::max (fq[ch], 1.0) / fs);
        constexpr double R = 0.7071067811865476; // 1 / (2 Q), Q = 1/√2
        for (int k = order; k < 7; ++k)
            phaserStages[(std::size_t) k] = {};
        float* io[2] { l, r };
        for (int ch = 0; ch < 2; ++ch)
        {
            double g[7], dg[7];
            for (int k = 0; k < order; ++k)
            {
                const auto& st = phaserStages[(std::size_t) k];
                g[k] = st.fresh ? gNew[ch] : st.g[ch];
                dg[k] = st.fresh ? 0.0 : (gNew[ch] - st.g[ch]) / n;
            }
            float* x = io[ch];
            for (int i = 0; i < n; ++i)
            {
                double v = x[i];
                for (int k = 0; k < order; ++k)
                {
                    auto& st = phaserStages[(std::size_t) k];
                    const double gk = g[k];
                    const double hp = (v - (2.0 * R + gk) * st.s1[ch] - st.s2[ch]) / (1.0 + 2.0 * R * gk + gk * gk);
                    const double bp = gk * hp + st.s1[ch], lp = gk * bp + st.s2[ch];
                    st.s1[ch] = gk * hp + bp;
                    st.s2[ch] = gk * bp + lp;
                    v = hp - 2.0 * R * bp + lp;
                    g[k] += dg[k];
                }
                x[i] += 0.5f * mix * ((float) v - x[i]);
            }
        }
        for (int k = 0; k < order; ++k)
        {
            auto& st = phaserStages[(std::size_t) k];
            st.g[0] = gNew[0];
            st.g[1] = gNew[1];
            st.fresh = false;
        }
    }

    // ---- the snapins' oversampler (Distortion, Bitcrush) ---------------------------------------------------------
    // N = ⌈150 kHz / fs⌉ (1 … 16); up: each sample held N times, then a 10th-order Chebyshev II low pass (55 dB stop band
    // from 25 kHz, at most 0.55 fs; five sections, the bilinear transform prewarped at the edge, unity at DC); down: the
    // same low pass, then every N-th sample (the first of each group). The design is the textbook one (LYNX's own code).
    struct Cheby2
    {
        double b0[5] {}, b1[5] {}, b2[5] {}, a1[5] {}, a2[5] {};
        double x1[5] {}, x2[5] {}, y1[5] {}, y2[5] {};
        void design (double edgeHz, double rate, double stopDb) noexcept
        {
            constexpr int order = 10;
            const double eps = 1.0 / std::sqrt (std::pow (10.0, stopDb / 10.0) - 1.0);
            const double mu = std::asinh (1.0 / eps) / order;
            const double k = 1.0 / std::tan (pi * edgeHz / rate);
            for (int s = 0; s < 5; ++s)
            {
                const double th = pi * (2.0 * s + 1.0) / (2.0 * order);
                const double sr = -std::sinh (mu) * std::sin (th), si = std::cosh (mu) * std::cos (th);
                const double den = sr * sr + si * si;
                const double pr = sr / den, pim = -si / den; // the pole 1 / (sr + j si)
                const double Z = 1.0 / (std::cos (th) * std::cos (th)), B = -2.0 * pr, C = pr * pr + pim * pim;
                const double g = C / Z; // unity at DC
                const double a0 = k * k + B * k + C;
                b0[s] = g * (k * k + Z) / a0;
                b1[s] = g * 2.0 * (Z - k * k) / a0;
                b2[s] = b0[s];
                a1[s] = 2.0 * (C - k * k) / a0;
                a2[s] = (k * k - B * k + C) / a0;
            }
        }
        void clear() noexcept
        {
            for (int s = 0; s < 5; ++s)
                x1[s] = x2[s] = y1[s] = y2[s] = 0.0;
        }
        float run (float in) noexcept
        {
            float v = in;
            for (int s = 0; s < 5; ++s)
            {
                const double x = v;
                const double y = b0[s] * x + b1[s] * x1[s] + b2[s] * x2[s] - a1[s] * y1[s] - a2[s] * y2[s];
                x2[s] = x1[s];
                x1[s] = x;
                y2[s] = y1[s];
                y1[s] = y;
                v = (float) y; // (the plug-in keeps each section's output in float)
            }
            return v;
        }
    };
    // both channels' sections in one pass (the same coefficients; the compiler pairs the two lanes)
    struct Cheby2x2
    {
        const Cheby2* c = nullptr;
        double x1[5][2] {}, x2[5][2] {}, y1[5][2] {}, y2[5][2] {};
        void clear() noexcept
        {
            for (int s = 0; s < 5; ++s)
                for (int k = 0; k < 2; ++k)
                    x1[s][k] = x2[s][k] = y1[s][k] = y2[s][k] = 0.0;
        }
        void run (float& a, float& b) noexcept
        {
            float v[2] { a, b };
            for (int s = 0; s < 5; ++s)
            {
                const double b0 = c->b0[s], b1 = c->b1[s], b2 = c->b2[s], a1 = c->a1[s], a2 = c->a2[s];
                for (int k = 0; k < 2; ++k)
                {
                    const double x = v[k];
                    const double y = b0 * x + b1 * x1[s][k] + b2 * x2[s][k] - a1 * y1[s][k] - a2 * y2[s][k];
                    x2[s][k] = x1[s][k];
                    x1[s][k] = x;
                    y2[s][k] = y1[s][k];
                    y1[s][k] = y;
                    v[k] = (float) y;
                }
            }
            a = v[0];
            b = v[1];
        }
    };
    struct Oversampler
    {
        Cheby2 up, down;
        int factor = 1;
        double forRate = 0.0, forUp = -1.0, forDown = -1.0, forStop = -1.0;
        // the stop band edges (Hz, clamped to 1 … 0.55 fs) and attenuation (Distortion 55 dB, Bitcrush 45 dB)
        void configure (double rate, double upHz, double downHz, double stopDb) noexcept
        {
            if (rate == forRate && upHz == forUp && downHz == forDown && stopDb == forStop)
                return;
            forRate = rate;
            forUp = upHz;
            forDown = downHz;
            forStop = stopDb;
            factor = std::clamp ((int) std::ceil (150000.0f / (float) rate), 1, 16);
            up.design (std::min (std::max (upHz, 1.0), 0.55 * rate), factor * rate, stopDb);
            down.design (std::min (std::max (downHz, 1.0), 0.55 * rate), factor * rate, stopDb);
        }
        void clear() noexcept { up.clear(); down.clear(); }
    };
    std::array<Oversampler, 2> overs {};

    // ---- Distortion --------------------------------------------------------------------------------------------
    // At the oversampled rate: an envelope follower e on |u| (floor 1e-4, attack 4 ms / release 100 ms, the coefficients
    // 1 − e^(−1 / (t fs)) of the base rate), g = 1 + dynamics (e − 1); v = u · drive / g + b (b = bias ± spread, the left
    // channel +), y = (f (v) − f (b)) · g, out = u + mix (y − u); then down, and the DC filter (a cookbook 30 Hz high pass,
    // Q 1/√2, at the base rate). f: tanh (overdrive), the quintic smoothstep 2t³ (6t² − 15t + 10) − 1 on t = (clip (v) + 1) / 2
    // (saturate), 2v / (1 + v²) (foldback), and with first-order antiderivative anti-aliasing (the plug-in's own scheme:
    // (F (v) − F (v₋₁)) / (v − v₋₁), f at the midpoint when they are closer than 2^-20) sin (πv / 2) (sine), clip (hard
    // clip) and floor (z) + ½ (quantize: z = u · 8 / drive / g + b, y scaled back by drive / 8).
    std::array<float, 2> distEnv { 1e-4f, 1e-4f }, distPrev {};
    Cheby2x2 distUp, distDown;
    bool distPrevSet = false;
    std::array<Biquad, 2> distDc {};
    static double quantF (double z) noexcept // ∫ (2 ⌊z⌋ + 1)
    {
        const double a = std::floor (z);
        return a * a + ((a + 1.0) * (a + 1.0) - a * a) * (z - a);
    }
    static double distShape (int type, double v, double vp) noexcept
    {
        switch (type)
        {
            case 0: return std::tanh ((float) v);
            case 1:
            {
                const double t = std::clamp (v, -1.0, 1.0) * 0.5 + 0.5;
                return 2.0 * t * t * t * ((6.0 * t - 15.0) * t + 10.0) - 1.0;
            }
            case 2: return 2.0 * v / (v * v + 1.0);
            default: break;
        }
        constexpr double close = 9.5367431640625e-07, h = 4.76837158203125e-07;
        double a = v, b = vp;
        if (std::abs (a - b) < close)
        {
            const double mid = 0.5 * (a + b);
            a = mid - h;
            b = mid + h;
        }
        if (type == 3)
            return (std::cos (a * 1.5707963705062866) - std::cos (b * 1.5707963705062866)) * -0.6366197546520227 / (a - b);
        if (type == 4)
        {
            auto F = [] (double z) { return std::abs (z) < 1.0 ? 0.5 * z * z : std::abs (z) - 0.5; };
            return (F (a) - F (b)) / (a - b);
        }
        return 0.5 * (quantF (a) - quantF (b)) / (a - b);
    }
    void distortion (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        const double drive = enc::logv (p[valueParam], enc::driveLo, enc::driveHi);
        const double bias = 2.0 * std::clamp ((double) p[valueParam + 1], 0.0, 1.0) - 1.0;
        const int type = std::clamp ((int) std::lround (5.0 * p[valueParam + 3]), 0, 5);
        const double spread = std::clamp ((double) p[valueParam + 2], 0.0, 1.0) * (type == 5 ? 0.5 : 1.0);
        const auto dyn = (float) std::clamp ((double) p[valueParam + 4], 0.0, 1.0);
        const bool dcOn = p[valueParam + 5] >= 0.5f;
        overs[0].configure (fs, 25000.0, 25000.0, 55.0);
        overs[1].configure (fs, 25000.0, 25000.0, 55.0);
        const int N = overs[0].factor;
        const auto att = (float) (1.0 - std::exp (-1.0 / (0.004 * fs))), rel = (float) (1.0 - std::exp (-1.0 / (0.1 * fs)));
        const double k = type == 5 ? 8.0 / drive : drive;
        // the DC filter: a cookbook high pass at 30 Hz
        const double w0 = twoPi * 30.0 / fs, cs = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + al, hb0 = (1.0 + cs) / 2.0 / a0, hb1 = -(1.0 + cs) / a0, ha1 = -2.0 * cs / a0, ha2 = (1.0 - al) / a0;
        double bb[2], fb[2];
        for (int c = 0; c < 2; ++c)
        {
            bb[c] = c == 0 ? bias + spread : bias - spread;
            if (type == 5)
                fb[c] = dcOn ? 0.5 * bb[c] : std::floor (0.5 * bb[c]) + 0.5;
            else
                fb[c] = distShape (type, bb[c], bb[c]);
            if (! distPrevSet)
                distPrev[(std::size_t) c] = (float) bb[c];
        }
        distUp.c = &overs[0].up;
        distDown.c = &overs[0].down;
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            float outS[2] {};
            for (int j = 0; j < N; ++j)
            {
                float u[2] { io[0][i], io[1][i] };
                distUp.run (u[0], u[1]);
                float m[2];
                for (int c = 0; c < 2; ++c)
                {
                    float& env = distEnv[(std::size_t) c];
                    const float au = std::max (1e-4f, std::abs (u[c]));
                    env += (au - env) * (env < au ? att : rel);
                    const float g = dyn * (env - 1.0f) + 1.0f;
                    const auto v = (float) ((double) u[c] * k / g + bb[c]);
                    double y = distShape (type, v, distPrev[(std::size_t) c]);
                    distPrev[(std::size_t) c] = v;
                    y = (y - fb[c]) * g;
                    if (type == 5)
                        y /= k;
                    m[c] = (float) (u[c] + mix * (y - u[c]));
                }
                distDown.run (m[0], m[1]);
                if (j == 0)
                    outS[0] = m[0], outS[1] = m[1];
            }
            for (int c = 0; c < 2; ++c)
            {
                double o = outS[c];
                if (dcOn)
                {
                    auto& dc = distDc[(std::size_t) c];
                    const double h = hb0 * o + hb1 * dc.x1 + hb0 * dc.x2 - ha1 * dc.y1 - ha2 * dc.y2;
                    dc.x2 = dc.x1;
                    dc.x1 = o;
                    dc.y2 = dc.y1;
                    dc.y1 = h;
                    o = h;
                }
                io[c][i] = (float) o;
            }
        }
        distPrevSet = true;
    }

    // ---- Bitcrush ----------------------------------------------------------------------------------------------
    // The oversampler (45 dB stop band) with its edges set by the qualities: q = 1 puts the edge at the new rate's
    // Nyquist, q = 0 at 22 kHz (log between them; at most 0.499 fs, 22 kHz). At the oversampled rate a sample-and-hold
    // runs at the new rate: a phase falls by rate / (N fs) a sample; when it crosses zero (f = how far past), the new
    // value is the input at the crossing (linear between the last two samples), quantized to 2^(bits − 1) steps as
    // ⌊v L + noise⌋ / L (blended by Quantize; noise uniform in ½ ± dither / 2 from a 48-bit linear congruential
    // generator seeded with 1 at a reset, one draw per base sample), and the output step is split at the crossing
    // (old (1 − f) + new f). From 44.1 kHz on (or at the oversampled rate) only the quantizer runs. out = x + mix (y − x).
    std::uint64_t crushSeed = 1;
    struct CrushState
    {
        float held[2] {}, prev[2] {};
        double phase[2] {};
    } crush;
    void bitcrush (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        const auto rate = (float) enc::logv (p[valueParam], enc::crushLo, enc::crushHi);
        const auto bits = (float) std::clamp (16.0 * p[valueParam + 1], 1.0, 16.0);
        const double adc = std::clamp ((double) p[valueParam + 2], 0.0, 1.0), dac = std::clamp ((double) p[valueParam + 3], 0.0, 1.0);
        const auto dither = (float) std::clamp ((double) p[valueParam + 4], 0.0, 1.0);
        const auto amount = (float) std::clamp ((double) p[valueParam + 5], 0.0, 1.0);
        if (sleeping (l, r, n, 1.0)) // asleep, its sample-and-hold grid stands still (no advance call)
            return;
        // the edges need the factor first (the rate of the oversampled stream)
        overs[0].configure (fs, overs[0].forUp < 0.0 ? 25000.0 : overs[0].forUp, overs[0].forDown < 0.0 ? 25000.0 : overs[0].forDown, 45.0);
        const int N = overs[0].factor;
        const double fos = (double) ((float) N * (float) fs);
        const double step = (double) rate / fos;
        const bool plain = rate >= 44100.0f - 9.5367431640625e-07f || step >= 1.0;
        const double top = std::min (fs * 0.499, 22000.0);
        double upHz = top, downHz = top;
        if (! plain)
        {
            const double span = std::log ((double) (rate * 0.5f)) - 9.998797732340453;
            upHz = std::min (top, std::exp (adc * span + 9.998797732340453));
            downHz = std::min (top, std::exp (dac * span + 9.998797732340453));
        }
        overs[0].configure (fs, upHz, downHz, 45.0);
        overs[1].configure (fs, upHz, downHz, 45.0);
        const bool quantOn = amount >= 9.5367431640625e-07f && bits < 16.0f - 9.5367431640625e-07f;
        const float L = std::exp2 (bits - 1.0f), invL = 1.0f / L;
        const float lo = 0.5f - 0.5f * dither, hi = 0.5f + 0.5f * dither;
        auto quant = [&] (float v, float noise) {
            const float t = v * L + noise;
            return v + amount * (std::floor (t) * invL - v);
        };
        for (int i = 0; i < n; ++i)
        {
            crushSeed = (crushSeed * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
            const float noise = (float) (int) (crushSeed >> 24) * ((hi - lo) * 5.960464477539063e-08f) + lo;
            float* io[2] { l + i, r + i };
            for (int c = 0; c < 2; ++c)
            {
                auto& ov = overs[(std::size_t) c];
                const float x = *io[c];
                float out0 = 0.0f;
                for (int j = 0; j < N; ++j)
                {
                    const float sIn = ov.up.run (x);
                    float o = sIn;
                    if (plain)
                    {
                        if (quantOn)
                            o = quant (sIn, noise);
                    }
                    else
                    {
                        double& ph = crush.phase[c];
                        float& h = crush.held[c];
                        ph -= step;
                        o = h;
                        if (ph <= 0.0)
                        {
                            const auto f = (float) (-ph * (1.0 / step));
                            float v = (crush.prev[c] - sIn) * f + sIn;
                            if (quantOn)
                                v = quant (v, noise);
                            o = (v - h) * f + h;
                            h = v;
                            ph += 1.0;
                        }
                        crush.prev[c] = sIn;
                    }
                    const float d = ov.down.run (o);
                    if (j == 0)
                        out0 = d;
                }
                *io[c] = x + mix * (out0 - x);
            }
        }
    }

    // ---- Frequency Shifter -------------------------------------------------------------------------------------
    // A 12th-order Chebyshev II high pass (55 dB, stop band edge max (20 Hz, −0.95 · shift): a downward shift loses what
    // would fold through 0 Hz), then the analytic pair (re, im) and y = cos 2πφ · re − sin 2πφ · im, φ += shift / fs
    // before each sample (in turns, wrapped once a block). The plug-in's own 90° pair is a fixed table; LYNX uses its own
    // minimax pair (Phase Distortion's), so the waveform's phase differs while the spectrum matches.
    struct Cheby2Hp
    {
        double b0[6] {}, b1[6] {}, b2[6] {}, a1[6] {}, a2[6] {}, x1[6] {}, x2[6] {}, y1[6] {}, y2[6] {};
        void design (double edgeHz, double rate) noexcept
        {
            constexpr int order = 12;
            constexpr double stopDb = 55.0;
            const double eps = 1.0 / std::sqrt (std::pow (10.0, stopDb / 10.0) - 1.0);
            const double mu = std::asinh (1.0 / eps) / order;
            const double k = 1.0 / std::tan (pi * std::min (edgeHz, 0.49 * rate) / rate);
            for (int s = 0; s < 6; ++s)
            {
                const double th = pi * (2.0 * s + 1.0) / (2.0 * order);
                const double sr = -std::sinh (mu) * std::sin (th), si = std::cosh (mu) * std::cos (th);
                const double den = sr * sr + si * si;
                const double pr = sr / den, pim = -si / den;
                const double Z = 1.0 / (std::cos (th) * std::cos (th)), B = -2.0 * pr, C = pr * pr + pim * pim;
                // s → 1 / s (high pass), then the bilinear transform; unity at Nyquist
                const double g = C / Z, kk = k * k;
                const double a0 = 1.0 + B * k + C * kk;
                b0[s] = g * (1.0 + Z * kk) / a0;
                b1[s] = g * (2.0 - 2.0 * Z * kk) / a0;
                b2[s] = b0[s];
                a1[s] = (2.0 - 2.0 * C * kk) / a0;
                a2[s] = (1.0 - B * k + C * kk) / a0;
            }
        }
        void clear() noexcept
        {
            for (int s = 0; s < 6; ++s)
                x1[s] = x2[s] = y1[s] = y2[s] = 0.0;
        }
        double run (double v) noexcept
        {
            for (int s = 0; s < 6; ++s)
            {
                const double y = b0[s] * v + b1[s] * x1[s] + b2[s] * x2[s] - a1[s] * y1[s] - a2[s] * y2[s];
                x2[s] = x1[s];
                x1[s] = v;
                y2[s] = y1[s];
                y1[s] = y;
                v = y;
            }
            return v;
        }
    };
    std::array<Cheby2Hp, 2> shiftHp {};
    double shiftHpFor = -1.0, shiftPhase = 0.0;
    void freqShift (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        const double shift = 10000.0 * (std::clamp ((double) p[valueParam], 0.0, 1.0) - 0.5);
        const double edge = std::max (20.0, (double) ((float) shift * -0.95f));
        if (edge != shiftHpFor)
        {
            shiftHpFor = edge;
            for (auto& h : shiftHp)
                h.design (edge, fs);
        }
        shiftPhase -= std::floor (shiftPhase);
        const double inc = shift / fs;
        for (int i = 0; i < n; ++i)
        {
            shiftPhase += inc;
            const double c = std::cos (twoPi * shiftPhase), sn = std::sin (twoPi * shiftPhase);
            float* io[2] { l + i, r + i };
            for (int ch = 0; ch < 2; ++ch)
            {
                const double x = *io[ch];
                double re, im;
                analytic (hil[(std::size_t) ch], shiftHp[(std::size_t) ch].run (x), re, im);
                *io[ch] = (float) (x + mix * (c * re - sn * im - x));
            }
        }
    }

    // ---- Ensemble ----------------------------------------------------------------------------------------------
    // [STATIC] UnisonDsp: up to 16 voices read one stereo delay line at (B − D) + 2D · m_v (B = ⌈0.016 fs⌉, D = 0.015 fs),
    // m_v a slow per-voice motion in 0 … 1 whose speed grows with the detune (8 detune² + 0.4 steps a second of its
    // course); each voice through its own 2nd-order all-pass (cutoff 50 … 1000 Hz); voice gains mix / √n · √2 · (cos θ,
    // sin θ), θ = π/4 ± spread · π/4 (odd voices left, even right, the first one centred when n is odd), voices 2, 3, 6, 7 …
    // inverted; out = (1 − mix) · x (t − B) + Σ voices. The plug-in's motion curves are tables built at run time;
    // LYNX's are its own smooth sums of two sines per voice (symmetric: voice pairs mirror each other, sine: one sine
    // a voice), so the voices' pitch drifts (about ±0.5 % at 25 % detune) match in size and speed, not sample for sample.
    double ensPos = 0.0;
    struct EnsVoice
    {
        double s1[2] {}, s2[2] {};
    };
    std::array<EnsVoice, 16> ensAp {};
    static double ensMotion (int v, int motion, double u) noexcept
    {
        const int pair = motion == 1 ? v / 2 : v;
        const double r1 = 0.047 * (1.0 + 0.173 * pair), r2 = 0.031 * (1.0 + 0.291 * pair);
        const double p1 = 2.39996 * pair, p2 = 1.1 + 3.7 * pair;
        double m = motion == 2 ? 0.5 + 0.5 * std::sin (twoPi * 0.06 * u + twoPi * v / 16.0)
                               : 0.5 + 0.3 * std::sin (twoPi * r1 * u + p1) + 0.2 * std::sin (twoPi * r2 * u + p2);
        if (motion == 1 && (v & 1) != 0)
            m = 1.0 - m;
        return m;
    }
    void ensemble (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        if (! line[0].ready())
            return;
        const int voices = std::clamp ((int) std::lround (16.0 * p[valueParam]), 2, 16);
        const double detune = std::clamp ((double) p[valueParam + 1], 0.0, 1.0);
        const double spread = std::clamp ((double) p[valueParam + 2], 0.0, 1.0);
        const int motion = std::clamp ((int) std::lround (2.0 * p[valueParam + 3]), 0, 2);
        const double B = std::ceil (0.016 * fs), D = 0.015 * fs;
        const double speed = (8.0 * detune * detune + 0.4) / fs;
        double gl[16], gr[16], g[16];
        const double gain = mix / std::sqrt ((double) voices) * 1.4142135381698608;
        for (int v = 0; v < voices; ++v)
        {
            double th = 0.7853981852531433;
            if (! (v == 0 && (voices & 1) != 0))
                th += spread * ((v & 1) == 0 ? 0.7853981852531433 : -0.7853981852531433);
            const double sg = (v & 2) != 0 && ! (v == 0 && (voices & 1) != 0) ? -gain : gain;
            gl[v] = sg * std::cos (th);
            gr[v] = sg * std::sin (th);
            // the voice's all-pass: its own fixed cutoff in 50 … 1000 Hz (log-spread over the voices)
            g[v] = std::tan (pi * std::min (50.0 * std::pow (20.0, (v + 0.5) / 16.0), 0.4999 * fs) / fs);
        }
        constexpr double R = 0.7071067811865476;
        // the motions are slow (a fraction of a hertz): evaluated every 32 samples, linear between (CPU)
        constexpr int hop = 32;
        double dNow[16] {}, dInc[16] {};
        for (int i = 0; i < n; ++i)
        {
            line[0].push (l[i]);
            line[1].push (r[i]);
            ensPos += speed;
            if (i % hop == 0)
                for (int v = 0; v < voices; ++v)
                {
                    const double d0 = (B - D) + 2.0 * D * ensMotion (v, motion, ensPos);
                    const double d1 = (B - D) + 2.0 * D * ensMotion (v, motion, ensPos + hop * speed);
                    dNow[v] = d0;
                    dInc[v] = (d1 - d0) / hop;
                }
            double sl = 0.0, sr = 0.0;
            for (int v = 0; v < voices; ++v)
            {
                const double d = std::clamp (dNow[v], 2.0, (double) line[0].mask - 8.0);
                dNow[v] += dInc[v];
                const double in[2] { catmull (line[0], d), catmull (line[1], d) };
                auto& st = ensAp[(std::size_t) v];
                double out[2];
                for (int c = 0; c < 2; ++c)
                {
                    const double gk = g[v];
                    const double hp = (in[c] - (2.0 * R + gk) * st.s1[c] - st.s2[c]) / (1.0 + 2.0 * R * gk + gk * gk);
                    const double bp = gk * hp + st.s1[c], lp = gk * bp + st.s2[c];
                    st.s1[c] = gk * hp + bp;
                    st.s2[c] = gk * bp + lp;
                    out[c] = hp - 2.0 * R * bp + lp;
                }
                sl += gl[v] * out[0];
                sr += gr[v] * out[1];
            }
            const auto b = (std::uint32_t) B;
            l[i] = (float) ((1.0 - mix) * line[0].back (b) + sl);
            r[i] = (float) ((1.0 - mix) * line[1].back (b) + sr);
        }
    }
    // ---- Nonlinear Filter (round 190oc) ------------------------------------------------------------------------
    // DirtyFilterDsp read in HeartCore 2.4 (REA_rodada190oc): a state-variable filter of the Chamberlin form at eight
    // times the rate, hp = x − lp + k · bp, bp += g · hp, lp += g · bp (k = (1 − e^(g / Q)) / g; g solves g + g³ / 24 = ω,
    // ω = 2π · cutoff · scale / 8 fs: the first two terms of 2 asin (g / 2)); the mode bends it with drive d = 10^(dB / 20),
    // dB = lo + Drive · (hi − lo) per mode:
    //   0 clean: linear (0 dB);
    //   1 saturated: the low pass fed back through f (d · lp) / d, f (a) = a + a³ / 3 + a⁵ / 5 (|a| ≤ 1, then 3a ∓ 22/15),
    //     −12 … +24 dB, cutoff × 0.66^(D²), the low-pass output × 10^(0.35 D²);
    //   2 tubular: both states scaled back onto the circle d · |(lp, bp)| ≤ 1, −12 … +16 dB;
    //   3 clipped: both states clipped to ±d, +3 … −16 dB;
    //   4 warm: x − lp through e (v) / d, e (v) = v − 0.9 tanh v (tanh as its [7/6] Padé fraction, |v| ≤ 4.373), v = d (x − lp),
    //     +24 … 0 dB, cutoff × 1.5^(D²), band pass × 10^(D / 2), high pass × 10^(0.75 D + 0.1);
    //   5 biased: the low pass fed back as (1 − h (−d · lp)) / d, h = exp on |a| ≤ 1, e · a above, (a + 2) / e below, −16 … +24 dB;
    //   6 fuzzy: the band pass clipped to ±1 / d, 0 … +36 dB, band pass × 10^(D²);
    //   7 metallic: x − lp through (0.75 sin v + 0.25 v) / d, −12 … +24 dB, cutoff × 1.2^(D²), band pass × 10^(D² / 4),
    //     high pass × 10^(0.35 D⁴);
    //   8 digital: x − lp through ⌊v⌋ + ½ averaged over v ± 0.05 (a box-filtered staircase), +24 … 0 dB.
    // Out = cIn · x + cLp · lp[n−1] + cBp · bp[n−1] + cHp · hp[n] by type (low / band / high pass, notch = low + high or, in
    // the fuzzy and digital modes, x − bp / Q, all-pass = low + high − bp / Q (digital: − 2 bp / Q)); the coefficients ramp
    // across a block and jump when the mode changes. Around it: up 2× through a half-band, up 4× by Catmull-Rom between
    // the 2× samples, down through a 6th-order Chebyshev II low pass (85 dB from 1.5 fs, the snapins' textbook design, as
    // round 190nh's oversampler) keeping every 4th sample, down 2× through the half-band, then a cookbook 17 Hz high pass
    // (Q 1/√2). The plug-in's half-band is a fixed coefficient table: LYNX designs its own (nlfHalfBand below).
    //
    // LYNX's own half-band: a two-branch polyphase IIR half-band, H (z) = ½ (A0 (z²) + z^-1 A1 (z²)), each branch a cascade
    // of first-order all-passes (a + z^-1) / (1 + a z^-1); its coefficients come from the elliptic design of Valenzuela and
    // Constantinides (IEE Proc. G 130, 1983: selectivity from the transition band, the nome's theta series), worked out
    // here at first use with LYNX's own specification: nlfHalfBandCoefs coefficients (alternately to A0 and A1) and a
    // transition band of nlfHalfBandTbw of the 2× rate (chosen on the references: lab/halfband.py, REA_rodada190oc §3).
    static constexpr int nlfHalfBandCoefs = 8;        // (17th order: 109 dB stop band)
    static constexpr double nlfHalfBandTbw = 0.054;   // (of the 2× rate: pass band to 0.223 × 2 fs, stop band from 0.277)
    struct NlfHalfBand
    {
        std::array<double, nlfHalfBandCoefs> a {};
        NlfHalfBand() noexcept
        {
            const int n = nlfHalfBandCoefs, order = 2 * n + 1;
            const double k = std::pow (std::tan ((1.0 - 2.0 * nlfHalfBandTbw) * pi / 4.0), 2.0); // the selectivity
            const double kp = std::pow (1.0 - k * k, 0.25);
            const double e = 0.5 * (1.0 - kp) / (1.0 + kp);
            const double q = e + 2.0 * std::pow (e, 5.0) + 15.0 * std::pow (e, 9.0) + 150.0 * std::pow (e, 13.0); // the nome
            for (int i = 1; i <= n; ++i)
            {
                double num = 0.0, den = 0.0;
                for (int m = 0; m < 12; ++m)
                    num += (m % 2 == 0 ? 1.0 : -1.0) * std::pow (q, (double) (m * (m + 1))) * std::sin ((2 * m + 1) * i * pi / order);
                for (int m = 1; m < 12; ++m)
                    den += (m % 2 == 0 ? 1.0 : -1.0) * std::pow (q, (double) (m * m)) * std::cos (2.0 * m * i * pi / order);
                const double w = 2.0 * std::pow (q, 0.25) * num / (1.0 + 2.0 * den), w2 = w * w;
                const double x = std::sqrt ((1.0 - w2 * k) * (1.0 - w2 / k)) / (1.0 + w2);
                a[(std::size_t) (i - 1)] = (1.0 - x) / (1.0 + x);
            }
        }
    };
    static const NlfHalfBand& nlfHalfBand() noexcept
    {
        static const NlfHalfBand h;
        return h;
    }
    // one branch's all-pass chain (state x[n−1], y[n−1] per section), at the low rate
    struct NlfBranch
    {
        std::array<double, nlfHalfBandCoefs> x1 {}, y1 {};
        double run (const double* a, int first, double v) noexcept
        {
            for (int s = first, j = 0; s < nlfHalfBandCoefs; s += 2, ++j)
            {
                const double y = a[s] * (v - y1[(std::size_t) j]) + x1[(std::size_t) j];
                x1[(std::size_t) j] = v;
                y1[(std::size_t) j] = y;
                v = y;
            }
            return v;
        }
    };
    struct NlfChannel
    {
        NlfBranch up0, up1, dn0, dn1;
        std::array<float, 4> hist {};        // the last four 2× samples (oldest first)
        double lp = 0.0, bp = 0.0;           // the filter's states
        double c1[3] {}, c2[3] {}, d1[3] {}, d2[3] {}; // the Chebyshev II sections (x and y history)
        double dc[4] {};                     // the 17 Hz high pass (x1, x2, y1, y2)
    };
    std::array<NlfChannel, 2> nlfCh {};
    struct NlfCoefs
    {
        double g = 0.0, k = 0.0, drive = 1.0, cLp = 0.0, cBp = 0.0, cHp = 0.0, cIn = 0.0;
    };
    NlfCoefs nlfNow {};
    int nlfMode = -1;                        // (−1: nothing played yet — the first block takes its values at once)
    double nlfForRate = 0.0;
    double nlfB0[3] {}, nlfB1[3] {}, nlfA1[3] {}, nlfA2[3] {}; // (b2 = b0)
    double nlfDc[5] {};
    static NlfCoefs nlfDesign (int type, int mode, double cutoff, double q, double drive, double rate8) noexcept
    {
        NlfCoefs c;
        double bp = 1.0;          // the band pass's coefficient the modes scale
        bool lpOn = false, hpOn = false;
        switch (type)
        {
            case 1: c.cBp = 1.0; break;
            case 2: c.cHp = 1.0, hpOn = true, bp = 0.0; break;
            case 3:
                if (mode == 6 || mode == 8)
                    c.cBp = bp = -1.0 / q, c.cIn = 1.0;
                else
                    c.cLp = c.cHp = 1.0, lpOn = hpOn = true, bp = 0.0;
                break;
            case 4:
                c.cLp = c.cHp = 1.0, lpOn = hpOn = true;
                c.cBp = bp = (mode == 8 ? -2.0 : -1.0) / q;
                break;
            default: c.cLp = 1.0, lpOn = true, bp = 0.0; break;
        }
        const double D = std::clamp (drive, 0.0, 1.0), D2 = D * D;
        const auto db = [] (double v) { return std::pow (10.0, v / 20.0); };
        double scale = 1.0, lo = 0.0, hi = 0.0;
        switch (mode)
        {
            case 1: scale = std::pow (0.66, D2), lo = -12.0, hi = 24.0; if (lpOn) c.cLp = db (7.0 * D2); break;
            case 2: lo = -12.0, hi = 16.0; break;
            case 3: lo = 3.0, hi = -16.0; break;
            case 4:
                scale = std::pow (1.5, D2), lo = 24.0, hi = 0.0;
                if (type != 2 && bp != 0.0) c.cBp = bp * db (10.0 * D);
                if (hpOn) c.cHp = db (15.0 * D + 2.0);
                break;
            case 5: lo = -16.0, hi = 24.0; break;
            case 6: lo = 0.0, hi = 36.0; if (bp != 0.0) c.cBp = bp * db (20.0 * D2); break;
            case 7:
                scale = std::pow (1.2, D2), lo = -12.0, hi = 24.0;
                if (bp != 0.0) c.cBp = bp * db (5.0 * D2);
                if (hpOn) c.cHp = db (7.0 * D2 * D2);
                break;
            case 8: lo = 24.0, hi = 0.0; break;
            default: break;
        }
        c.drive = db (lo + D * (hi - lo));
        const double w = scale * twoPi * cutoff / rate8;
        // g + g³ / 24 = ω (Cardano: the one real root)
        const double s = std::sqrt (144.0 * w * w + 512.0);
        c.g = std::cbrt (12.0 * w + s) + std::cbrt (12.0 * w - s);
        c.k = (1.0 - std::exp (c.g / q)) / c.g;
        return c;
    }
    // the Catmull-Rom weights of the four 2× samples at t = ¼, ½, ¾, 1 between the second and the third: ½ (−t + 2t² − t³),
    // ½ (2 − 5t² + 3t³), ½ (t + 4t² − 3t³), ½ (t³ − t²) (exact in float: multiples of 1/128)
    static constexpr std::array<std::array<float, 4>, 4> nlfCatmull = [] {
        std::array<std::array<float, 4>, 4> w {};
        for (int j = 0; j < 4; ++j)
        {
            const double t = 0.25 * (j + 1), t2 = t * t, t3 = t2 * t;
            w[(std::size_t) j] = { (float) (0.5 * (-t + 2.0 * t2 - t3)), (float) (0.5 * (2.0 - 5.0 * t2 + 3.0 * t3)), (float) (0.5 * (t + 4.0 * t2 - 3.0 * t3)),
                                   (float) (0.5 * (t3 - t2)) };
        }
        return w;
    }();
    static double nlfTanh (double v) noexcept // the [7/6] Padé fraction of tanh (Lambert's continued fraction), |v| ≤ 4.373
    {
        v = std::clamp (v, -4.373000144958496, 4.373000144958496);
        const double v2 = v * v;
        return v * ((21.0 * v2 + 1260.0) * v2 + 10395.0) / (((v2 + 210.0) * v2 + 4725.0) * v2 + 10395.0);
    }
    static double nlfFloorInt (double u) noexcept // ∫₀ᵘ ⌊s⌋ ds
    {
        const double f = std::floor (u);
        return 0.5 * f * (2.0 * u - f - 1.0);
    }
    // one step of the filter at 8 fs: x in, the output by type
    static float nlfStep (NlfChannel& ch, double x, int mode, const NlfCoefs& c) noexcept
    {
        const double lp = ch.lp, bp = ch.bp, g = c.g, d = c.drive;
        double hp;
        switch (mode)
        {
            case 1:
            {
                const double a = d * lp;
                const double f = a < -1.0 ? 3.0 * a + 22.0 / 15.0 : a > 1.0 ? 3.0 * a - 22.0 / 15.0 : a + a * a * a * (a * a / 5.0 + 1.0 / 3.0);
                hp = c.k * bp + (x - f / d);
                break;
            }
            case 4: { const double v = d * (x - lp); hp = c.k * bp + (v - 0.9 * nlfTanh (v)) / d; break; }
            case 5:
            {
                const double a = -d * lp;
                const double h = a < -1.0 ? (a + 2.0) / 2.718281828459045 : a > 1.0 ? 2.718281828459045 * a : std::exp (a);
                hp = c.k * bp + x + (h - 1.0) / d;
                break;
            }
            case 7: { const double v = d * (x - lp); hp = c.k * bp + (0.75 * std::sin (v) + 0.25 * v) / d; break; }
            case 8:
            {
                const double v = d * (x - lp);
                hp = c.k * bp + (10.0 * (nlfFloorInt (v + 0.05) - nlfFloorInt (v - 0.05)) + 0.5) / d;
                break;
            }
            default: hp = c.k * bp + (x - lp); break;
        }
        double nbp = bp + g * hp, nlp;
        switch (mode)
        {
            case 2:
            {
                nlp = lp + g * nbp;
                const double r = d * std::sqrt (nlp * nlp + nbp * nbp);
                const double s = r < 1.0 ? 1.0 : 1.0 / r;
                nbp *= s, nlp *= s;
                break;
            }
            case 3:
                nbp = std::clamp (nbp, -d, d);
                nlp = std::clamp (lp + g * nbp, -d, d);
                break;
            case 6:
                nbp = std::clamp (d * nbp, -1.0, 1.0) / d;
                nlp = lp + g * nbp;
                break;
            default: nlp = lp + g * nbp; break;
        }
        ch.lp = nlp;
        ch.bp = nbp;
        return (float) c.cIn * (float) x + (float) (c.cLp * lp) + (float) (c.cBp * bp) + (float) c.cHp * (float) hp;
    }
    void nonlinearFilter (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        if (n <= 0)
            return;
        const int type = std::clamp ((int) std::lround (4.0 * p[valueParam]), 0, 4);
        const int mode = std::clamp ((int) std::lround (8.0 * p[valueParam + 1]), 0, 8);
        const double rate8 = 8.0 * fs;
        const auto target = nlfDesign (type, mode, enc::logv (p[valueParam + 2], enc::nlfCutLo, enc::nlfCutHi),
                                       enc::logv (p[valueParam + 3], enc::nlfQLo, enc::nlfQHi), p[valueParam + 4], rate8);
        if (nlfForRate != fs)
        {
            nlfForRate = fs;
            // the decimator: a 6th-order Chebyshev II low pass at 8 fs, 85 dB from 1.5 fs (textbook, unity at DC)
            constexpr int order = 6;
            const double eps = 1.0 / std::sqrt (std::pow (10.0, 85.0 / 10.0) - 1.0);
            const double mu = std::asinh (1.0 / eps) / order;
            const double kk = 1.0 / std::tan (pi * 1.5 * fs / rate8);
            for (int s = 0; s < 3; ++s)
            {
                const double th = pi * (2.0 * s + 1.0) / (2.0 * order);
                const double sr = -std::sinh (mu) * std::sin (th), si = std::cosh (mu) * std::cos (th);
                const double den = sr * sr + si * si;
                const double pr = sr / den, pim = -si / den;
                const double Z = 1.0 / (std::cos (th) * std::cos (th)), B = -2.0 * pr, C = pr * pr + pim * pim;
                const double gg = C / Z, a0 = kk * kk + B * kk + C;
                nlfB0[s] = gg * (kk * kk + Z) / a0;
                nlfB1[s] = gg * 2.0 * (Z - kk * kk) / a0;
                nlfA1[s] = 2.0 * (C - kk * kk) / a0;
                nlfA2[s] = (kk * kk - B * kk + C) / a0;
            }
            // the output's DC filter: a cookbook high pass, 17 Hz, Q 1/√2
            const double w0 = twoPi * std::min (17.0, 0.49 * fs) / fs, cs = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071067811865476);
            const double a0 = 1.0 + al;
            nlfDc[0] = (1.0 + cs) / 2.0 / a0, nlfDc[1] = -(1.0 + cs) / a0, nlfDc[2] = nlfDc[0];
            nlfDc[3] = -2.0 * cs / a0, nlfDc[4] = (1.0 - al) / a0;
        }
        NlfCoefs step {};
        if (mode != nlfMode)
            nlfNow = target, nlfMode = mode;
        else
        {
            const double inv = 1.0 / (8.0 * n);
            step = { (target.g - nlfNow.g) * inv, (target.k - nlfNow.k) * inv, (target.drive - nlfNow.drive) * inv, (target.cLp - nlfNow.cLp) * inv,
                     (target.cBp - nlfNow.cBp) * inv, (target.cHp - nlfNow.cHp) * inv, (target.cIn - nlfNow.cIn) * inv };
        }
        const bool ramp = mode == nlfMode && (step.g != 0.0 || step.k != 0.0 || step.drive != 0.0 || step.cLp != 0.0 || step.cBp != 0.0
                                              || step.cHp != 0.0 || step.cIn != 0.0);
        const double* a = nlfHalfBand().a.data();
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            // the coefficients this sample's eight steps use (the same for both channels; held: all eight the same)
            NlfCoefs ramped[8];
            const NlfCoefs* cs = ramped;
            if (ramp)
                for (int j = 0; j < 8; ++j)
                {
                    nlfNow.g += step.g, nlfNow.k += step.k, nlfNow.drive += step.drive, nlfNow.cLp += step.cLp;
                    nlfNow.cBp += step.cBp, nlfNow.cHp += step.cHp, nlfNow.cIn += step.cIn;
                    ramped[j] = nlfNow;
                }
            const int stride = ramp ? 1 : 0;
            if (! ramp)
                cs = &nlfNow;
            for (int c = 0; c < 2; ++c)
            {
                auto& ch = nlfCh[(std::size_t) c];
                const float x = io[c][i];
                // up 2×: the two branches give the even and the odd 2× sample
                const double u[2] { ch.up0.run (a, 0, x), ch.up1.run (a, 1, x) };
                double half[2];
                for (int h = 0; h < 2; ++h)
                {
                    auto& hs = ch.hist;
                    hs[0] = hs[1], hs[1] = hs[2], hs[2] = hs[3], hs[3] = (float) u[h];
                    // up 4× (Catmull-Rom between hs[1] and hs[2] at ¼, ½, ¾, 1), the filter, the decimator's low pass
                    double y = 0.0;
                    for (int j = 0; j < 4; ++j)
                    {
                        const auto& wj = nlfCatmull[(std::size_t) j];
                        const float xi = wj[0] * hs[0] + wj[1] * hs[1] + wj[2] * hs[2] + wj[3] * hs[3];
                        double v = nlfStep (ch, xi, mode, cs[(4 * h + j) * stride]);
                        for (int s = 0; s < 3; ++s)
                        {
                            const double out = nlfB0[s] * v + nlfB1[s] * ch.c1[s] + nlfB0[s] * ch.c2[s] - nlfA1[s] * ch.d1[s] - nlfA2[s] * ch.d2[s];
                            ch.c2[s] = ch.c1[s], ch.c1[s] = v;
                            ch.d2[s] = ch.d1[s], ch.d1[s] = out;
                            v = (double) (float) out;
                        }
                        y = v;
                    }
                    half[h] = y;
                }
                // down 2×: the odd 2× sample through A0, the even one through A1 (H's odd outputs kept)
                const double down = 0.5 * (ch.dn0.run (a, 0, half[1]) + ch.dn1.run (a, 1, half[0]));
                auto& z = ch.dc;
                const double o = nlfDc[0] * down + nlfDc[1] * z[0] + nlfDc[2] * z[1] - nlfDc[3] * z[2] - nlfDc[4] * z[3];
                z[1] = z[0], z[0] = down, z[3] = z[2], z[2] = o;
                io[c][i] = (float) (x + mix * ((float) o - x));
            }
        }
    }
    void nlfReset() noexcept
    {
        for (auto& ch : nlfCh)
            ch = {};
        nlfMode = -1;
    }
};
} // namespace lynx::khsdsp
