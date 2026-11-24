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
//        46 Frequency Shifter, 47 Phaser, 48 Ensemble, 49 Nonlinear Filter; round 190py: 50 Flanger, 51 Dual Delay,
//        52 Ensemble with the plug-in's own voice motions (48 stays for slots saved with it); round 190qz: 53 Ring Mod
//        with the plug-in's own 2× path (8 stays for slots saved with it), 54 Formant Filter, 55 Delay, 56 Clipper
//        (slots saved with their earlier stand-ins, Facet EQ / Echoform / Fruity Soft Clipper, keep them); 0 = none
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
                 comb, chorus, tranceGate, distortion, bitcrush, freqShift, phaser, ensemble, nonlinearFilter,
                 flanger, dualDelay, ensemble2, // round 190py: codes 50, 51, 52 (ensemble2: the plug-in's own voice motions)
                 ringMod2, formant, delay, clipper }; // round 190qz: codes 53 (Ring Mod with the plug-in's own 2× path),
                                                      // 54 (Formant Filter), 55 (Delay), 56 (Clipper)
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
    return c >= 41 && c <= 56 ? (Law) (6 + c - 41) : Law::none;
}
inline float lawValue (Law l) noexcept { return (float) lawCode (l) / 64.0f; }
// the latency a law reports (the comb filter's 16-tap sinc read is centred 7 samples late, dry and wet alike; round
// 190py: the Flanger's dry and wet come 16 samples late, the centre of its 2× up-sampler's 32 taps)
// round 190qz: the Ring Mod's 2× path (its two 15-tap half-bands) makes dry and wet 7 samples late
inline int lawLatency (Law l) noexcept { return l == Law::comb || l == Law::ringMod2 ? 7 : l == Law::flanger ? 16 : 0; }

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
// Ensemble (laws 48 and 52): [13] voices / 16, [14] detune, [15] spread, [16] motion / 2 (random, symmetric, sine); the mix is Wet
// round 190oc — Nonlinear Filter: [13] type / 4 (low, band, high pass, notch, all-pass), [14] mode / 8 (clean, saturated,
// tubular, clipped, warm, biased, fuzzy, metallic, digital: the plug-in's order), [15] cutoff logv 20 … 20 480 Hz,
// [16] Q logv 0.5 … 24, [17] drive; the mix is Wet
constexpr double nlfCutLo = 20.0, nlfCutHi = 20480.0, nlfQLo = 0.5, nlfQHi = 24.0;
// round 190py — Flanger: [13] delay × 64 s, [14] depth × 64 s (powers of two: the snapin's floats come back exactly),
// [15] rate / 32 Hz, [16] offset (v + 1) / 2, [17] motion (v + 10) / 20 Hz, [18] feedback (v + 1) / 2, [19] spread,
// [20] barber-pole (1); the mix is Wet
constexpr float flSeconds = 64.0f, flRateHz = 32.0f;
// Dual Delay: [13] time / 16 s, [14] ratio / 4, [15] steps / 64, [16] denominator index / 6 (4, 4t, 8, 8t, 16, 16t, 32),
// [17] sync (1), [18] feedback, [19] cross-talk, [20] spread, [21] tone (v + 1) / 2, [22] duck; the mix is Wet
constexpr float ddSeconds = 16.0f, ddRatio = 4.0f;
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
    // buffers (Reverser, Resonator) are allocated only for a slot that holds a Kilohearts law when it is created; round
    // 190py: the Dual Delay's second pair of lines only for a slot created with that law
    void prepare (double sampleRate, bool allocate, Law createdWith = Law::none)
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
            if (createdWith == Law::dualDelay) // round 190py: the second time's lines (time × ratio, up to 2.56 s × 2)
                for (auto& c : ddLine2)
                    c.allocate ((std::size_t) (5.2 * fs) + 16);
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
        flReset();
        ddReset();
        ens2Reset();
        rm2Reset();
        fmReset();
        dlReset();
        clReset();
    }
    void setSongBeats (double b) noexcept { songBeats = b; songKnown = true; }
    // round 190oc: LYNX's own half-band (the Nonlinear Filter's 2× resampler): its all-pass coefficients, A0's at the even
    // places, A1's at the odd ones (tests, docs)
    static std::vector<double> nonlinearHalfBand() { const auto& a = nlfHalfBand().a; return { a.begin(), a.end() }; }
    // round 190qz: the Ring Mod law's half-band, its four odd taps on one side (tests, docs)
    static std::vector<float> ringMod2Taps() { const auto& t = rm2Taps(); return { t.begin(), t.end() }; }
    // round 190py: one of the Ensemble law's voice curves (motion 0 random / 1 symmetric / 2 sine, voice 0 … 15, curve 0
    // delay / 1 cutoff), its 1024 points (tests, docs)
    static std::vector<float> ensembleCurve (int motion, int voice, int curve)
    {
        const float* t = ensCurves().data() + ((std::size_t) (std::clamp (motion, 0, 2) * 16 + std::clamp (voice, 0, 15)) * 2 + (std::size_t) std::clamp (curve, 0, 1)) * ensStride;
        return { t, t + ensPoints };
    }
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
            case Law::flanger: flanger (l, r, n, p, mix); break;
            case Law::dualDelay: dualDelay (l, r, n, p, mix, bpm); break;
            case Law::ensemble2: ensemble2 (l, r, n, p, mix); break;
            case Law::ringMod2: ringMod2 (l, r, n, p, mix); break;
            case Law::formant: formant (l, r, n, p, mix); break;
            case Law::delay: delay (l, r, n, p, mix, bpm); break;
            case Law::clipper: clipper (l, r, n, p, mix); break;
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
    // ==== round 190py =============================================================================================
    // FlangerDsp and DualdelayDsp read statically in HeartCore 2.4 (docs/analise/REA_rodada190py-khs.md) and fitted on
    // round 190nb's references; LYNX's own code restating the laws as formulas (the up-sampler's taps are LYNX's own).

    // HeartCore's parameter ramp: the first block after a reset (or a sleeping block) takes the setting at once; later a
    // change moves linearly across the block from the last setting, and a change under 1e-15 holds
    struct Ramp
    {
        float start = 0.0f, target = 0.0f, value = 0.0f, inc = 0.0f, t = 0.0f;
        bool fresh = true;
        void set (float v, int steps) noexcept
        {
            t = 0.0f;
            if (fresh)
            {
                start = target = value = v;
                inc = 0.0f;
                fresh = false;
            }
            else if ((double) std::abs (v - target) < 1e-15)
            {
                start = target = value = v;
                inc = 0.0f;
            }
            else
            {
                start = value = target;
                target = v;
                inc = 1.0f / (float) steps;
            }
        }
        void snap (float v) noexcept { start = target = value = v, inc = 0.0f, t = 0.0f, fresh = false; }
        float step() noexcept
        {
            t += 1.0f;
            value = (target - start) * t * inc + start;
            return value;
        }
    };
    // a delay line as the snapins keep it: read before the write, the read clamped to what has been written (from 3)
    struct Line
    {
        Ring* ring = nullptr;
        std::uint32_t fill = 3;
        void clear() noexcept
        {
            if (ring != nullptr && ring->ready())
                ring->clear();
            fill = 3;
        }
        void push (float v) noexcept
        {
            ring->push (v);
            fill = std::min (fill + 1, ring->mask + 1);
        }
        float at (std::uint32_t d) const noexcept { return ring->back (std::min (d, fill)); } // d + 1 samples late
        // the Catmull-Rom read at `d` samples (taps from ⌊d⌋ + 2 back, t = 1 − frac d)
        float catmull (float d) const noexcept
        {
            const auto i0 = (std::uint32_t) (int) d;
            const std::uint32_t i = std::min (i0 + 2u, fill);
            const float t = (1.0f - d) + (float) (int) d, t2 = t * t;
            const float p0 = ring->back (i), p1 = ring->back (i - 1), p2 = ring->back (i - 2), p3 = ring->back (i - 3);
            return ((-0.5f * t + 0.0f) + (-0.5f * t + 1.0f) * t2) * p0 + ((0.0f * t + 1.0f) + (1.5f * t - 2.5f) * t2) * p1
                   + ((0.5f * t + 0.0f) + (-1.5f * t + 2.0f) * t2) * p2 + ((0.0f * t + 0.0f) + (0.5f * t - 0.5f) * t2) * p3;
        }
    };

    // ---- Flanger -----------------------------------------------------------------------------------------------
    // [STATIC] FlangerDsp: the input up 2× (even samples: the input 16 samples late; odd ones: a 32-tap interpolator),
    // at 2 fs per channel: d = (delay − 1.5 / fs + depth · (½ sin 2π (φ ∓ spread / 4) + ½)) · 2 fs, the line read by
    // Catmull-Rom (wet) and written with x + feedback · wet; only the even 2× samples are kept (no down-sampling filter):
    // out = cos (mix π/4) · x + sin (mix π/4) · wet. φ (in turns, double) += rate / 2 fs a 2× sample, wrapped a block.
    // Barber-pole: an analytic pair (I, Q) of the 2× input feeds two lines per channel (A, B, read at d); with
    // ψ = 2π (θ − (offset ∓ spread / 4) / 2) (left −, right +), θ += −motion / 4 fs a 2× sample (wrapped a block):
    // w = cos ψ · A − sin ψ · B, A ← I + fb · w, B ← Q + fb · (cos ψ · B + sin ψ · A), out = dry · (sin ψ · Q + cos ψ · I) +
    // wet · w. The plug-in's 90° pair is its own design: LYNX uses its own (round 190nb's minimax pair), so the barber-pole
    // matches in kind, not sample for sample. Asleep (HeartCore's tail: 10 ms), φ and θ advance by the block at once.
    // The interpolator: LYNX's own windowed sinc at the odd half-sample positions (a Kaiser window over the 32 taps,
    // β = 5.5, chosen black-box on the references: lab/sweepfl.sh, REA_rodada190py-khs §3), unity at DC.
    static constexpr double flUpBeta = 5.5;
    static const std::array<float, 32>& flUpTaps() noexcept
    {
        static const std::array<float, 32> taps = [] {
            auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
            std::array<double, 32> h {};
            double sum = 0.0;
            for (int k = 0; k < 32; ++k)
            {
                const double u = k - 15.5, r = u / 16.0;
                h[(std::size_t) k] = std::sin (pi * u) / (pi * u) * i0 (flUpBeta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (flUpBeta);
                sum += h[(std::size_t) k];
            }
            std::array<float, 32> f {};
            for (int k = 0; k < 32; ++k)
                f[(std::size_t) k] = (float) (h[(std::size_t) k] / sum);
            return f;
        }();
        return taps;
    }
    struct FlUp
    {
        std::array<float, 32> x {}; // x[k]: the input k samples back
    };
    std::array<FlUp, 2> flUp {};
    std::array<Line, 4> flLines {}; // A left / right (line[0, 1]), B left / right (res[0, 1]: barber-pole)
    std::array<Hilbert, 2> flHil {};
    Ramp flDelay, flDepth, flFeedback, flOffset, flDry, flWet;
    std::array<float, 2> flSpreadOld {}, flSpreadNew {};
    float flSpreadT = 0.0f, flSpreadInc = 0.0f;
    bool flSpreadFresh = true, flBarber = false;
    double flPhase = 0.0, flScroll = 0.0;
    void flReset() noexcept
    {
        flLines[0].ring = &line[0], flLines[1].ring = &line[1], flLines[2].ring = &res[0], flLines[3].ring = &res[1];
        for (auto& ln : flLines)
            ln.fill = 3;
        flUp = {};
        flHil = {};
        for (auto* r : { &flDelay, &flDepth, &flFeedback, &flOffset, &flDry, &flWet })
            *r = {};
        flSpreadOld = flSpreadNew = {};
        flSpreadT = flSpreadInc = 0.0f;
        flSpreadFresh = true;
        flBarber = false;
        flPhase = flScroll = 0.0;
    }
    void flanger (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        if (! line[0].ready() || ! res[0].ready())
            return;
        const float sr = (float) fs, sr2 = sr + sr;
        const float delay = (float) std::clamp ((double) p[valueParam], 0.0, 1.0) / enc::flSeconds;
        const float depth = (float) std::clamp ((double) p[valueParam + 1], 0.0, 1.0) / enc::flSeconds;
        const float rate = (float) std::clamp ((double) p[valueParam + 2], 0.0, 1.0) * enc::flRateHz;
        const float offset = (float) (2.0 * std::clamp ((double) p[valueParam + 3], 0.0, 1.0) - 1.0);
        const float scroll = (float) (20.0 * std::clamp ((double) p[valueParam + 4], 0.0, 1.0) - 10.0);
        const float fb = (float) (2.0 * std::clamp ((double) p[valueParam + 5], 0.0, 1.0) - 1.0);
        const float spread = (float) std::clamp ((double) p[valueParam + 6], 0.0, 1.0);
        const bool barber = p[valueParam + 7] >= 0.5f;
        const float a = mix * 0.7853981852531433f, dryV = std::cos (a), wetV = std::sin (a);
        const std::array<float, 2> sv { spread * -0.25f, spread * 0.25f };
        auto clearLines = [&] { for (auto& ln : flLines) ln.clear(); };
        // [MEASURED as round 190nh's sleep] asleep (HeartCore's tail: 10 ms), the LFOs advance by the block at once
        if (sleeping (l, r, n, std::ceil (0.01 * fs + 0.01) - 61.0))
        {
            flOffset.snap (-offset), flDelay.snap (delay), flDepth.snap (depth), flFeedback.snap (fb), flDry.snap (dryV), flWet.snap (wetV);
            flSpreadOld = flSpreadNew = sv, flSpreadT = flSpreadInc = 0.0f, flSpreadFresh = false;
            const double dS = barber ? (double) -scroll / (double) sr2 : 0.0;
            if (barber != flBarber)
                clearLines();
            flBarber = barber;
            flPhase += (double) n * (double) rate / (double) sr;
            flScroll = barber ? dS * (double) n + flScroll : 0.0;
            flScroll -= std::floor (flScroll);
            flPhase -= std::floor (flPhase);
            return;
        }
        flOffset.set (-offset, 2 * n), flDelay.set (delay, 2 * n), flDepth.set (depth, 2 * n), flFeedback.set (fb, 2 * n);
        if (flSpreadFresh)
            flSpreadOld = flSpreadNew = sv, flSpreadInc = 0.0f, flSpreadFresh = false;
        else
            flSpreadOld = flSpreadNew, flSpreadNew = sv, flSpreadInc = 1.0f / (float) (2 * n);
        flSpreadT = 0.0f;
        flDry.set (dryV, n), flWet.set (wetV, n);
        const double dS = barber ? (double) scroll / (double) (-4.0f * sr) : 0.0;
        if (! barber)
            flScroll = 0.0;
        if (barber != flBarber)
            clearLines();
        flBarber = barber;
        const double dRate = (double) rate / (double) sr2;
        const float lead = 1.5f / sr;
        const auto& taps = flUpTaps();
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            const float dry = flDry.step(), wet = flWet.step();
            float sub[2][2]; // [2× sample][channel]
            for (int c = 0; c < 2; ++c)
            {
                auto& h = flUp[(std::size_t) c].x;
                for (int k = 31; k > 0; --k)
                    h[(std::size_t) k] = h[(std::size_t) k - 1];
                h[0] = io[c][i];
                float odd = 0.0f;
                for (int k = 0; k < 32; ++k)
                    odd += taps[(std::size_t) k] * h[(std::size_t) k];
                sub[0][c] = h[16];
                sub[1][c] = odd;
            }
            for (int s = 0; s < 2; ++s)
            {
                const float dep = flDepth.step(), fbv = flFeedback.step(), del = flDelay.step(), off = flOffset.step();
                flSpreadT += 1.0f;
                const float f = flSpreadT * flSpreadInc, g = 1.0f - f;
                const float vec[2] { f * flSpreadNew[0] + g * flSpreadOld[0], f * flSpreadNew[1] + g * flSpreadOld[1] };
                flPhase += dRate;
                const auto ph = (float) flPhase;
                float d[2];
                for (int c = 0; c < 2; ++c)
                    d[c] = ((del - lead) + dep * (std::sin ((ph + vec[c]) * twoPiF) * 0.5f + 0.5f)) * sr2;
                if (! barber)
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& ln = flLines[(std::size_t) c];
                        const float w = ln.catmull (d[c]);
                        ln.push (fbv * w + sub[s][c]);
                        if (s == 0)
                            io[c][i] = dry * sub[0][c] + wet * w;
                    }
                    continue;
                }
                flScroll += dS;
                const auto th = (float) flScroll;
                for (int c = 0; c < 2; ++c)
                {
                    double re, im;
                    analytic (flHil[(std::size_t) c], (double) sub[s][c], re, im);
                    const auto in = (float) re, q = (float) im;
                    const float psi = (th + (off - vec[c]) * 0.5f) * twoPiF, sn = std::sin (psi), cs = std::cos (psi);
                    auto& A = flLines[(std::size_t) c];
                    auto& B = flLines[(std::size_t) c + 2];
                    const float ra = A.catmull (d[c]), rb = B.catmull (d[c]);
                    const float w = cs * ra - sn * rb;
                    A.push (w * fbv + in);
                    B.push ((rb * cs + ra * sn) * fbv + q);
                    if (s == 0)
                        io[c][i] = dry * (sn * q + cs * in) + wet * w;
                }
            }
        }
        flScroll -= std::floor (flScroll);
        flPhase -= std::floor (flPhase);
    }

    // ---- Dual Delay --------------------------------------------------------------------------------------------
    // [STATIC] DualdelayDsp: two stereo lines at T1 = [time · fs] and T2 = [ratio · time · fs] samples (synced: steps ×
    // the note at the tempo), each read one sample after its time; a time change crossfades the reads over 50 ms
    // (smoothstep). Feedback with cross-talk as a rotation: u1 = x + fb c · a1 − fb s · a2, u2 = x + fb s · a1 + fb c · a2
    // (c, s = cos, sin of cross-talk · π/4), each through a tone shelf (a state-variable filter at min (800 Hz, 0.4999 fs),
    // Q ½: tone ≥ 0 scales its low pass, below 0 its high pass, by 10^(−48 dB · T · |tone| / 20), T the mean time in
    // seconds: 48 dB a second of echo; the band pass × (0.586 g + 1.414)) into its line. Out: wet_L = √2 (sin β · a1_L +
    // cos β · a2_L), wet_R = √2 (cos β · a1_R + sin β · a2_R), β = π/4 (1 + spread) (the block's spread takes effect a
    // block later); ducking: an RMS follower e (e += k (p / e − e), p = the input's mean square, from 0.005) and
    // gain = clamp (1 − 10 duck² · e, 0, 1); out = (1 − mix) · x + mix · gain · wet.
    std::array<Ring, 2> ddLine2;
    std::array<Line, 4> ddLines {}; // line 1 left / right (rev[0, 1]), line 2 left / right (ddLine2)
    int ddT[2] { -1, -1 }, ddPrev[2] { -1, -1 }, ddXfLeft = 0;
    Ramp ddFb, ddCt, ddDry, ddWet, ddDuck;
    std::array<float, 2> ddSpread {};
    bool ddSpreadFresh = true;
    float ddEnv = 0.005f;
    double ddS1[2][2] {}, ddS2[2][2] {}; // the tone filters' states [line][channel]
    void ddReset() noexcept
    {
        ddLines[0].ring = &rev[0], ddLines[1].ring = &rev[1], ddLines[2].ring = &ddLine2[0], ddLines[3].ring = &ddLine2[1];
        for (auto& ln : ddLines)
            ln.clear();
        ddT[0] = ddT[1] = ddPrev[0] = ddPrev[1] = -1;
        ddXfLeft = 0;
        for (auto* r : { &ddFb, &ddCt, &ddDry, &ddWet, &ddDuck })
            *r = {};
        ddSpread = {};
        ddSpreadFresh = true;
        ddEnv = 0.005f;
        for (auto& a : ddS1)
            a[0] = a[1] = 0.0;
        for (auto& a : ddS2)
            a[0] = a[1] = 0.0;
    }
    void dualDelay (float* l, float* r, int n, const float* p, float mix, double bpm) noexcept
    {
        if (! rev[0].ready() || ! ddLine2[0].ready())
            return;
        const float sr = (float) fs;
        const float time = (float) std::clamp ((double) p[valueParam], 0.0, 1.0) * enc::ddSeconds;
        const float ratio = (float) std::clamp ((double) p[valueParam + 1], 0.0, 1.0) * enc::ddRatio;
        const bool sync = p[valueParam + 4] >= 0.5f;
        const float fbk = (float) std::clamp ((double) p[valueParam + 5], 0.0, 1.0);
        const float cross = (float) std::clamp ((double) p[valueParam + 6], 0.0, 1.0);
        const float spread = (float) std::clamp ((double) p[valueParam + 7], 0.0, 1.0);
        const float tone = (float) (2.0 * std::clamp ((double) p[valueParam + 8], 0.0, 1.0) - 1.0);
        const float duck = (float) std::clamp ((double) p[valueParam + 9], 0.0, 1.0);
        const int xfLen = (int) std::round (sr * 0.05f);
        // the lines' times (whole samples), at a block's start when no crossfade runs
        if (ddXfLeft < 1)
        {
            ddPrev[0] = ddT[0], ddPrev[1] = ddT[1];
            float t;
            if (! sync)
                t = time;
            else
            {
                const int steps = std::max (1, (int) std::lround (64.0 * std::clamp ((double) p[valueParam + 2], 0.0, 1.0)));
                const int den = (int) std::lround (6.0 * std::clamp ((double) p[valueParam + 3], 0.0, 1.0));
                t = (float) ((double) steps * enc::reverserBeats (den) / (double) (float) (std::max (bpm, 1.0) / 60.0));
            }
            const float maxT = (float) ((double) ddLines[0].ring->mask / fs) - 0.01f, maxT2 = (float) ((double) ddLine2[0].mask / fs) - 0.01f;
            t = std::min (std::max (t, 0.0f), maxT);
            const float t2 = std::min (std::max (ratio * t, 0.0f), maxT2);
            const int n1 = (int) std::round (t * sr), n2 = (int) std::round (t2 * sr);
            if (n1 != ddT[0] || n2 != ddT[1])
            {
                const bool first = ddT[0] < 0;
                ddT[0] = n1, ddT[1] = n2;
                if (first)
                    ddPrev[0] = n1, ddPrev[1] = n2;
                else
                    ddXfLeft = xfLen;
            }
        }
        const float invFs = 1.0f / sr;
        const float meanNow = (float) (ddT[0] + ddT[1]) * 0.5f * invFs, meanWas = (float) (ddPrev[0] + ddPrev[1]) * 0.5f;
        const float xf = std::min (1.0f, std::max (0.0f, (float) std::max (0, ddXfLeft - n) / (float) xfLen));
        const float meanT = xf * xf * (meanWas * invFs - meanNow) * (3.0f - (xf + xf)) + meanNow;
        // gains: the rotation, the mix, the duck depth (ramped over the block)
        const float ang = cross * 0.7853981852531433f;
        ddFb.set (std::cos (ang) * fbk, n), ddCt.set (std::sin (ang) * fbk, n), ddDry.set (1.0f - mix, n), ddWet.set (mix, n);
        ddDuck.set (duck * duck * 10.0f, n);
        const float be = spread * 0.7853981852531433f + 0.7853981852531433f;
        const std::array<float, 2> sp { std::sin (be) * 1.4142135381698608f, std::cos (be) * 1.4142135381698608f };
        std::array<float, 2> spUse = ddSpread; // the spread set by the block before
        if (ddSpreadFresh)
            spUse = sp, ddSpreadFresh = false;
        ddSpread = sp;
        // the tone shelf
        const float x = meanT * 48.0f * tone;
        double gH = 1.0, gL = 1.0, gB;
        if (x >= 0.0f)
        {
            const float g = std::exp2 (x * -0.16609640419483185f);
            gL = (double) g, gB = (double) (g * 0.5857864618301392f + 1.4142135381698608f);
        }
        else
        {
            const float g = std::exp2 (x * 0.16609640419483185f);
            gH = (double) g, gB = (double) (g * 0.5857864618301392f + 1.4142135381698608f);
        }
        const double gc = std::tan (std::min (0.4999 * (double) sr, 800.0) * pi / (double) sr);
        const double A = 2.0 + gc, D = 1.0 / (A * gc + 1.0), E = D * gc, G = gc + gc;
        const float k = ddFollow();
        // HeartCore's sleep: the input silent for longer than the tail (the longer time + 64, as the plug-in reports it;
        // round 190nh's offset), a block whose echoes reach 2^-20 counting as sound; no advance call
        if (sleeping (l, r, n, (double) std::max (ddT[0], ddT[1]) + 3.0))
            return;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float fbv = ddFb.step(), ctv = ddCt.step(), dry = ddDry.step(), wet = ddWet.step(), dk = ddDuck.step();
            const float x0[2] { l[i], r[i] };
            float a1[2], a2[2];
            for (int c = 0; c < 2; ++c)
            {
                a1[c] = ddLines[(std::size_t) c].at ((std::uint32_t) ddT[0]);
                a2[c] = ddLines[(std::size_t) c + 2].at ((std::uint32_t) ddT[1]);
            }
            if (ddXfLeft > 0)
            {
                --ddXfLeft;
                float f = std::min (1.0f, std::max (0.0f, (float) ddXfLeft / (float) xfLen));
                f = (3.0f - (f + f)) * f * f;
                for (int c = 0; c < 2; ++c)
                {
                    a1[c] += (ddLines[(std::size_t) c].at ((std::uint32_t) ddPrev[0]) - a1[c]) * f;
                    a2[c] += (ddLines[(std::size_t) c + 2].at ((std::uint32_t) ddPrev[1]) - a2[c]) * f;
                }
            }
            for (int c = 0; c < 2; ++c)
            {
                const double u[2] { (double) ((fbv * a1[c] + x0[c]) - a2[c] * ctv), (double) (ctv * a1[c] + fbv * a2[c] + x0[c]) };
                for (int j = 0; j < 2; ++j)
                {
                    double& s1 = ddS1[j][c];
                    double& s2 = ddS2[j][c];
                    const double v = gc * u[j] + s1;
                    const double hp = u[j] - (v * A + s2) * D;
                    const double bp = hp * gc + s1;
                    const double lp = (v - s2 * gc) * E + s2;
                    s1 = ((u[j] - lp) - (bp + bp)) * G + s1;
                    s2 = bp * G + s2;
                    ddLines[(std::size_t) (c + 2 * j)].push ((float) (hp * gH + lp * gL + bp * gB));
                }
            }
            const float wl = a1[0] * spUse[0] + a2[0] * spUse[1], wr = a1[1] * spUse[1] + a2[1] * spUse[0];
            peak = std::max (peak, std::max (std::abs (wl), std::abs (wr)));
            const float pw = std::max ((x0[0] * x0[0] + x0[1] * x0[1]) * 0.5f, 9.5367431640625e-07f);
            ddEnv = (pw / ddEnv - ddEnv) * k + ddEnv;
            const float gain = std::min (1.0f, std::max (0.0f, 1.0f - ddEnv * dk)) * wet;
            l[i] = gain * wl + dry * x0[0];
            r[i] = gain * wr + dry * x0[1];
        }
        if (peak > 9.5367431640625e-07f)
            quietRun = 0;
    }
    // the ducking follower's step: ½ (1 − 2^(−1 / (fs · 50 ms))) [STATIC form; the 50 ms fitted on the Duck renders]
    float ddFollow() const noexcept { return 0.5f - std::exp2 (-1.0f / ((float) fs * 0.05f)) * 0.5f; }
    // ---- Ensemble (round 190py: the plug-in's own voice motions) -----------------------------------------------
    // [STATIC] UnisonDsp, as round 190nh's law 48 but with the plug-in's motion curves and its 2× line: the input goes up
    // 2× (the Flanger's up-sampler) into one stereo line; voice v reads it by Catmull-Rom 2 d_v − 1 2× samples behind
    // the newest (d_v = (B − D) + 2D · m_v (u), B = ⌈0.016 fs⌉, D = 0.015 fs; the up-sampler's 16 samples on top), through
    // its own all-pass (TPT state-variable, Q 1/√2) at f_v = e^(3.912 + 2.996 c_v (w)) Hz (50 … 1000 Hz, ≤ 0.4999 fs, once
    // a block); gains mix / √n · √2 (cos, sin) θ_v, θ = π/4 ± spread · π/4 (odd voices left, even right, the first centred
    // when n is odd), voices 2, 3, 6, 7 … inverted, ramped over the block; out = (1 − mix) · x (t − 15 − B) + Σ voices.
    // u (in curve turns) += (8 detune² + 0.4) / 1024 fs a sample, w += n / 128 fs a block; both wrap.
    // The curves m_v, c_v (16 voices × delay / cutoff × random / symmetric / sine, 1024 points + 4) are generated as the
    // plug-in generates them at start-up: random — 1024 draws of the 48-bit linear congruential generator (a · 0x5DEECE66D
    // + 11, the POSIX drand48 recurrence) from seed 123124439, uniform [0, 1) from bits 24 … 47, three passes of a circular
    // two-point average, scaled to 0 … 1; symmetric — the random ones, except that odd voices' delay curves mirror the
    // voice before (1 − m); sine — ½ sin 2π (k · 100 / 1024 + 0.618034 · (16 · curve + v)) + ½. LYNX's own code of that
    // generator (no table of the plug-in is kept: the curves are made at first use).
    static constexpr int ensPoints = 1024, ensStride = 1028;
    static const std::vector<float>& ensCurves() noexcept
    {
        static const std::vector<float> t = [] {
            std::vector<float> c ((std::size_t) 3 * 16 * 2 * ensStride, 0.0f);
            auto at = [&] (int motion, int voice, int curve) { return c.data() + ((std::size_t) (motion * 16 + voice) * 2 + (std::size_t) curve) * ensStride; };
            constexpr std::uint64_t mask = (1ULL << 48) - 1;
            std::uint64_t s = 0x756bad7ULL & mask;
            auto next = [&] {
                s = (s * 0x5DEECE66DULL + 0xBULL) & mask;
                return (float) (int) (s >> 24) * 1.0f * 5.9604644775390625e-08f + 0.0f;
            };
            std::vector<float> a ((std::size_t) ensPoints);
            for (int v = 0; v < 16; ++v)
                for (int curve = 0; curve < 2; ++curve)
                    for (int motion = 0; motion < 3; ++motion)
                    {
                        float* T = at (motion, v, curve);
                        if (motion == 0 || (motion == 1 && ((v & 1) == 0 || curve == 1)))
                        {
                            for (auto& x : a)
                                x = next();
                            auto smooth = [] (const float* from, float* to) {
                                for (int i = 0; i < ensPoints; ++i)
                                    to[i] = (from[i] + from[(i + 1) & (ensPoints - 1)]) * 0.5f;
                            };
                            smooth (a.data(), T);
                            smooth (T, a.data());
                            smooth (a.data(), T);
                            float lo = 1.0f, hi = 0.0f;
                            for (int i = 0; i < ensPoints; ++i)
                                lo = std::min (lo, T[i]), hi = std::max (hi, T[i]);
                            const float k = 1.0f / (hi - lo);
                            for (int i = 0; i < ensPoints; ++i)
                                T[i] = (T[i] - lo) * k;
                        }
                        else if (motion == 1)
                        {
                            const float* before = at (1, v - 1, curve);
                            for (int i = 0; i < ensPoints; ++i)
                                T[i] = 1.0f - before[i];
                        }
                        else
                        {
                            const float ph = (float) (curve * 16 + v) * 0.6180340051651001f;
                            for (int i = 0; i < ensPoints; ++i)
                                T[i] = std::sin (((float) i * 0.09765625f + ph) * 6.2831854820251465f) * 0.5f + 0.5f;
                        }
                        for (int i = 0; i < 4; ++i)
                            T[ensPoints + i] = T[i];
                    }
            return c;
        }();
        return t;
    }
    // a curve at position u (in points): Catmull-Rom from point ⌊u⌋, between ⌊u⌋ + 1 and ⌊u⌋ + 2
    static float ensAt (const float* T, int i, float f) noexcept
    {
        const float f2 = f * f;
        const float* q = T + (i & (ensPoints - 1));
        const float w0 = (f * -0.5f + 0.0f) + (f * -0.5f + 1.0f) * f2, w1 = (f * 0.0f + 1.0f) + (f * 1.5f - 2.5f) * f2;
        const float w2 = (f * 0.5f + 0.0f) + (f * -1.5f + 2.0f) * f2, w3 = (f * 0.0f + 0.0f) + (f * 0.5f - 0.5f) * f2;
        return w0 * q[0] + w1 * q[1] + w2 * q[2] + w3 * q[3];
    }
    struct RampD
    {
        double start = 0.0, target = 0.0, value = 0.0, inc = 0.0, t = 0.0;
        bool fresh = true;
        void set (double v, int steps) noexcept
        {
            t = 0.0;
            if (fresh)
                start = target = value = v, inc = 0.0, fresh = false;
            else if (std::abs (v - target) < 1e-15)
                start = target = value = v, inc = 0.0;
            else
                start = value = target, target = v, inc = (double) (1.0f / (float) steps);
        }
        double step() noexcept
        {
            t += 1.0;
            value = (target - start) * t * inc + start;
            return value;
        }
    };
    struct EnsVoice2
    {
        std::array<float, 2> gOld {}, gNew {};
        float gT = 0.0f, gInc = 0.0f;
        bool gFresh = true;
        Ramp cut;
        double s1[2] {}, s2[2] {};
    };
    std::array<EnsVoice2, 16> ens2 {};
    FlUp ensUp2[2] {};
    Line ensLine2x[2] {};
    RampD ensSpeed;
    Ramp ensDry;
    double ensU = 0.0, ensW = 0.0;
    int ensVoicesWas = 0;
    void ens2Reset() noexcept
    {
        ensLine2x[0].ring = &line[0], ensLine2x[1].ring = &line[1];
        ens2 = {};
        ensUp2[0] = {}, ensUp2[1] = {};
        ensSpeed = {};
        ensDry = {};
        ensU = ensW = 0.0;
        ensVoicesWas = 0;
    }
    void ensemble2 (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        if (! line[0].ready())
            return;
        const int voices = std::clamp ((int) std::lround (16.0 * p[valueParam]), 2, 16);
        const auto detune = (float) std::clamp ((double) p[valueParam + 1], 0.0, 1.0);
        const auto spread = (float) std::clamp ((double) p[valueParam + 2], 0.0, 1.0);
        const int motion = std::clamp ((int) std::lround (2.0 * p[valueParam + 3]), 0, 2);
        const float sr = (float) fs;
        // HeartCore's sleep: asleep once the input has been silent for 37.5 ms; no advance call (the voices' curves stand
        // still). [MEASURED] the best single tail on the references (B + D … 2 (B + D) tried: lab/ensvar.sh, REA §4); the
        // plug-in's own rule after a long silence is not resolved
        if (sleeping (l, r, n, 0.0375 * fs))
            return;
        ensDry.set (1.0f - mix, n);
        if (ensVoicesWas == 0)
            ensVoicesWas = voices;
        const int active = std::max (ensVoicesWas, voices);
        ensVoicesWas = voices;
        const auto& curves = ensCurves();
        const float* delayCurve0 = curves.data() + (std::size_t) motion * 16 * 2 * ensStride;
        {
            const double w = ensW * 1024.0;
            const int wi = (int) std::floor ((float) w);
            const float wf = (float) w - (float) wi;
            for (int v = 0; v < 16; ++v)
            {
                auto& e = ens2[(std::size_t) v];
                if (v >= active)
                {
                    e.gOld = e.gNew = {}, e.gT = e.gInc = 0.0f, e.gFresh = true;
                    e.cut = {};
                    continue;
                }
                float g = v < voices ? mix / std::sqrt ((float) voices) : 0.0f;
                const bool centred = (voices & 1) != 0 && v == 0;
                if (! centred && (v & 2) != 0)
                    g = -g;
                float th = 0.7853981852531433f;
                if (! centred)
                    th += spread * ((v & 1) == 0 ? 0.7853981852531433f : -0.7853981852531433f);
                g *= 1.4142135381698608f;
                const std::array<float, 2> pair { g * std::cos (th), g * std::sin (th) };
                if (e.gFresh)
                    e.gOld = e.gNew = pair, e.gInc = 0.0f, e.gFresh = false;
                else
                    e.gOld = e.gNew, e.gNew = pair, e.gInc = 1.0f / (float) n;
                e.gT = 0.0f;
                const float c = ensAt (delayCurve0 + ((std::size_t) v * 2 + 1) * ensStride, wi, wf);
                const double hz = std::min ((double) std::exp (c * 2.995732307434082f + 3.9120230674743652f), (double) sr * 0.4999);
                e.cut.set ((float) std::tan (hz * pi / (double) sr), n);
            }
        }
        ensSpeed.set ((double) ((detune * detune * 8.0f + 0.4000000059604645f) / (1024.0f * sr)), n);
        const int B = (int) std::ceil (0.016f * sr);
        const float D = sr * 0.015f;
        const auto& taps = flUpTaps();
        constexpr double sq2 = 1.4142135865763297;
        for (int i = 0; i < n; ++i)
        {
            const double speed = ensSpeed.step();
            const float dry = ensDry.step();
            ensU += speed;
            float* io[2] { l + i, r + i };
            for (int c = 0; c < 2; ++c)
            {
                auto& h = ensUp2[c].x;
                for (int k = 31; k > 0; --k)
                    h[(std::size_t) k] = h[(std::size_t) k - 1];
                h[0] = *io[c];
                float odd = 0.0f;
                for (int k = 0; k < 32; ++k)
                    odd += taps[(std::size_t) k] * h[(std::size_t) k];
                ensLine2x[c].push (h[16]);
                ensLine2x[c].push (odd);
            }
            float wet[2] { 0.0f, 0.0f };
            const double u = ensU * 1024.0;
            const int ui = (int) std::floor ((float) u);
            const float uf = (float) u - (float) ui;
            for (int v = 0; v < active; ++v)
            {
                auto& e = ens2[(std::size_t) v];
                e.gT += 1.0f;
                const float f = e.gT * e.gInc, fo = 1.0f - f;
                const float gl = f * e.gNew[0] + fo * e.gOld[0], gr = f * e.gNew[1] + fo * e.gOld[1];
                const double g = (double) e.cut.step();
                const float m = ensAt (delayCurve0 + (std::size_t) v * 2 * ensStride, ui, uf);
                float pos = (D + D) * m + ((float) B - D);
                pos = pos + pos;
                const int pi0 = (int) std::floor (pos);
                const float t = pos - (float) pi0, t2 = t * t;
                const float w0 = (t * -0.5f + 0.0f) + (t * -0.5f + 1.0f) * t2, w1 = (t * 0.0f + 1.0f) + (t * 1.5f - 2.5f) * t2;
                const float w2 = (t * 0.5f + 0.0f) + (t * -1.5f + 2.0f) * t2, w3 = (t * 0.0f + 0.0f) + (t * 0.5f - 0.5f) * t2;
                const double A = g + sq2, Dn = 1.0 / (A * g + 1.0), g2 = g + g;
                for (int c = 0; c < 2; ++c)
                {
                    const Ring& rg = *ensLine2x[c].ring;
                    const auto b = (std::uint32_t) (pi0 - 2);
                    const double x = (double) ((w0 * rg.back (b) + w1 * rg.back (b + 1)) + (w2 * rg.back (b + 2) + w3 * rg.back (b + 3)));
                    double& s1 = e.s1[c];
                    double& s2 = e.s2[c];
                    const double v1 = g * x + s1;
                    const double hp = x - (A * v1 + s2) * Dn;
                    const double bp = hp * g + s1;
                    const double lp = Dn * g * (v1 - g * s2) + s2;
                    const double bk = bp * -sq2;
                    s1 = ((x - lp) + bk) * g2 + s1;
                    s2 = bp * g2 + s2;
                    wet[c] += (float) (lp + hp + bk) * (c == 0 ? gl : gr);
                }
            }
            // the dry: 2 B + 1 2× samples back as the plug-in indexes its line, two nearer in LYNX's (16 + B − 1 samples
            // late; [MEASURED] the mix-0 renders null bit for bit); the voices' reads likewise
            const auto bd = (std::uint32_t) (std::lround (2.0f * (float) B + 1.0f) - 2);
            for (int c = 0; c < 2; ++c)
                *io[c] = dry * ensLine2x[c].ring->back (bd) + wet[c];
        }
        ensW += (double) n * (double) (0.0078125f / sr);
        ensW -= std::floor (ensW);
        ensU -= std::floor (ensU);
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

    // ---- round 190qz: Ring Mod with the plug-in's own signal path (law 53) ---------------------------------------
    // [STATIC] RingModDsp (REA_rodada190qz-khs §2): the whole effect runs at 2 fs. Up 2×: the even 2× sample is the
    // input 4 samples back, the odd one a half-band interpolation of the 8 samples around it; the sine modulator
    // (phase in turns, double, += f / 2 fs a 2× sample, wrapped once a block; the left channel at φ + s, the right at
    // φ − s, s = spread / 4, evaluated in float) goes through the rectifier with first-order antiderivative
    // anti-aliasing (F (m) = ½ (g+ max (m, 0)² + g− min (m, 0)²), y = (F (m) − F (m₋₁)) / (m − m₋₁), or f at the mean
    // when |m − m₋₁| < 5·10⁻⁶), then m' = 1 + mix · (bias − 1 + y) multiplies the 2× input; down 2× through the same
    // half-band, then a 15 Hz cookbook high pass (Q 1/√2, direct form II in double). Dry and wet come 7 samples late
    // (the law reports 7). The plug-in's half-band coefficients are a fixed table: LYNX uses its own design (a
    // Kaiser-windowed sinc, 15 taps, unity at DC; β = 4.55 chosen black-box on the references, REA §2). Asleep as round
    // 190nb's law: from a reset until the first block with sound, and from the second all-zero block on.
    // Encodings: [13] frequency / 32 768 Hz (a power of two: the plug-in's float comes back exactly), [14] spread,
    // [15] bias, [16] rectify (r + 1) / 2; the mix is Wet.
    static constexpr double rm2Beta = 4.55;
    // the half-band's odd taps t[k] (k = 0 nearest the centre … 3) on one side, Σ t = ¼ (with the centre tap ½ and the
    // other side: unity at DC); the interpolated sample is 2 Σ t[k] (x[c − k] + x[c + 1 + k])
    static const std::array<float, 4>& rm2Taps() noexcept
    {
        static const std::array<float, 4> taps = [] {
            auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
            std::array<double, 4> h {};
            double sum = 0.0;
            for (int k = 0; k < 4; ++k)
            {
                const double u = 2.0 * k + 1.0, r = u / 8.0; // the 2× tap's distance from the centre (15 taps: ±7)
                h[(std::size_t) k] = std::sin (0.5 * pi * u) / (pi * u) * i0 (rm2Beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (rm2Beta);
                sum += h[(std::size_t) k];
            }
            std::array<float, 4> f {};
            for (int k = 0; k < 4; ++k)
                f[(std::size_t) k] = (float) (0.25 * h[(std::size_t) k] / sum);
            return f;
        }();
        return taps;
    }
    struct Rm2Channel
    {
        std::array<float, 8> x {};   // the input, x[0] newest
        std::array<float, 15> u {};  // the 2× stream, u[0] newest
        float mPrev = 0.0f;          // the previous 2× modulator value (anti-aliasing)
        double w1 = 0.0, w2 = 0.0;   // the high pass (direct form II)
    };
    std::array<Rm2Channel, 2> rm2 {};
    RampD rm2Inc;
    Ramp rm2Spread, rm2Bias, rm2Rect, rm2Mix;
    double rm2Phase = 0.0, rm2HpFor = -1.0;
    double rm2Hp[5] {}; // b0, b1, b2, a1, a2
    void rm2Reset() noexcept
    {
        rm2 = {};
        rm2Inc = {};
        rm2Spread = {}, rm2Bias = {}, rm2Rect = {}, rm2Mix = {};
        rm2Phase = 0.0;
    }
    void ringMod2 (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        bool silent = true;
        for (int i = 0; i < n && silent; ++i)
            silent = l[i] == 0.0f && r[i] == 0.0f;
        if (silent)
        {
            if (ringAsleep || ++ringSilent > 1)
            {
                ringAsleep = true;
                return;
            }
        }
        else
        {
            ringAsleep = false;
            ringSilent = 0;
        }
        if (rm2HpFor != fs)
        {
            rm2HpFor = fs;
            const double w0 = twoPi * 15.0 / fs, cs = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071067811865476), a0 = 1.0 + al;
            rm2Hp[0] = (1.0 + cs) / 2.0 / a0, rm2Hp[1] = -(1.0 + cs) / a0, rm2Hp[2] = rm2Hp[0];
            rm2Hp[3] = -2.0 * cs / a0, rm2Hp[4] = (1.0 - al) / a0;
        }
        const int n2 = 2 * n;
        const double hz = std::clamp ((double) p[valueParam], 0.0, 1.0) * 32768.0;
        rm2Inc.set (hz / (2.0 * fs), n2);
        rm2Spread.set (0.25f * std::clamp (p[valueParam + 1], 0.0f, 1.0f), n2);
        rm2Bias.set (std::clamp (p[valueParam + 2], 0.0f, 1.0f), n2);
        rm2Rect.set (2.0f * (2.0f * std::clamp (p[valueParam + 3], 0.0f, 1.0f) - 1.0f), n2); // 2 · rectify (−2 … 2)
        rm2Mix.set (std::clamp (mix, 0.0f, 1.0f), n2);
        const auto& t = rm2Taps();
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            // two 2× samples: the modulator for both channels at each
            float m[2][2]; // [2× sample][channel]
            float g[2];    // the mix at each
            float b[2], rp[2], rn[2];
            for (int k = 0; k < 2; ++k)
            {
                // (the odd 2× sample's phase is φ + the even one's increment; φ moves by both)
                const double inc = rm2Inc.step();
                const double ph = k == 0 ? rm2Phase : rm2Phase + rm2PrevInc;
                if (k == 1)
                    rm2Phase += rm2PrevInc + inc;
                else
                    rm2PrevInc = inc;
                const float s = rm2Spread.step();
                const float pf = (float) ph;
                m[k][0] = std::sin ((pf + s) * 6.2831855f);
                m[k][1] = std::sin ((pf - s) * 6.2831855f);
                b[k] = rm2Bias.step();
                const float rr = rm2Rect.step();
                rp[k] = std::min (1.0f, 1.0f + rr);
                rn[k] = std::min (1.0f, 1.0f - rr);
                g[k] = rm2Mix.step();
            }
            for (int c = 0; c < 2; ++c)
            {
                auto& ch = rm2[(std::size_t) c];
                for (int j = 7; j > 0; --j)
                    ch.x[(std::size_t) j] = ch.x[(std::size_t) (j - 1)];
                ch.x[0] = io[c][i];
                // the even 2× sample (x 4 back) and the odd one between x[4] and x[3]
                const float odd = 2.0f * (t[0] * (ch.x[4] + ch.x[3]) + t[1] * (ch.x[5] + ch.x[2]) + t[2] * (ch.x[6] + ch.x[1]) + t[3] * (ch.x[7] + ch.x[0]));
                const float up[2] { ch.x[4], odd };
                for (int k = 0; k < 2; ++k)
                {
                    const float mv = m[k][c], mp = ch.mPrev;
                    ch.mPrev = mv;
                    float y;
                    const float d = mv - mp;
                    if (std::abs (d) < 4.9999999e-06f)
                    {
                        const float a = (mv + mp) * 0.5f;
                        y = (a < 0.0f ? rn[k] : rp[k]) * a;
                    }
                    else
                    {
                        const float ap = std::max (mv, 0.0f), an = std::min (mv, 0.0f), bp = std::max (mp, 0.0f), bn = std::min (mp, 0.0f);
                        y = ((ap * ap - bp * bp) * rp[k] + (an * an - bn * bn) * rn[k]) * 0.5f / d;
                    }
                    const float mod = g[k] * (b[k] - 1.0f + y) + 1.0f;
                    for (int j = 14; j > 0; --j)
                        ch.u[(std::size_t) j] = ch.u[(std::size_t) (j - 1)];
                    ch.u[0] = up[k] * mod;
                }
                // down 2× (centred 7 2× samples back), then the high pass
                const float dn = 0.5f * ch.u[7] + t[0] * (ch.u[8] + ch.u[6]) + t[1] * (ch.u[10] + ch.u[4]) + t[2] * (ch.u[12] + ch.u[2]) + t[3] * (ch.u[14] + ch.u[0]);
                const double w = (double) dn - (rm2Hp[4] * ch.w2 + rm2Hp[3] * ch.w1);
                const double o = rm2Hp[2] * ch.w2 + rm2Hp[1] * ch.w1 + rm2Hp[0] * w;
                ch.w2 = ch.w1, ch.w1 = w;
                io[c][i] = (float) o;
            }
        }
        rm2Phase -= std::floor (rm2Phase);
    }
    double rm2PrevInc = 0.0;

    // ---- round 190qz: Formant Filter (law 54) ---------------------------------------------------------------------
    // [STATIC] VocalFilterDsp (REA_rodada190qz-khs §3): two cookbook biquads in a row, both at the plug-in's Q: at the
    // lower of the two cutoffs a peak of 20 · log10 (Q) dB (Lows on) or a high pass (Lows off), at the higher one a peak
    // of the same gain (Highs on) or a low pass (Highs off); the cutoffs held at most 0.49 fs. Direct form I in double,
    // the first section's output rounded to float before the second. The designer is the textbook cookbook (peak:
    // A = 10^(dB / 40), α = sin w0 / 2Q; the gain in dB taken in float). Encodings: [13] cutoff 1 / 32 768 Hz,
    // [14] cutoff 2 / 32 768 Hz, [15] Q / 64 (powers of two: the plug-in's floats come back exactly), [16] Lows (1),
    // [17] Highs (1); the mix is Wet (the plug-in has none: 1).
    struct FmSection
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};
        double run (int c, double x) noexcept
        {
            const double y = b2 * x2[c] + b1 * x1[c] + b0 * x - (a2 * y2[c] + a1 * y1[c]);
            x2[c] = x1[c], x1[c] = x, y2[c] = y1[c], y1[c] = y;
            return y;
        }
        void clear() noexcept { for (int c = 0; c < 2; ++c) x1[c] = x2[c] = y1[c] = y2[c] = 0.0; }
    };
    std::array<FmSection, 2> fm {};
    float fmFor[6] { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    void fmReset() noexcept
    {
        for (auto& s : fm)
            s.clear();
    }
    // the cookbook designer for the three shapes the plug-in uses (0 low pass, 2 high pass, 5 peak)
    static void fmDesign (FmSection& s, int shape, double rate, double hz, double q, double db) noexcept
    {
        const double f = std::min (hz, 0.49 * rate);
        const double w0 = f * twoPi / rate, cs = std::cos (w0), sn = std::sin (w0), al = sn / (q + q);
        double b0, b1, b2, a0, a1, a2;
        if (shape == 5)
        {
            const double A = std::exp2 (db * 0.08304820237218406);
            b0 = 1.0 + al * A, b1 = -2.0 * cs, b2 = 1.0 - al * A;
            a0 = 1.0 + al / A, a1 = -2.0 * cs, a2 = 1.0 - al / A;
        }
        else if (shape == 2)
        {
            b0 = (1.0 + cs) * 0.5, b1 = -(1.0 + cs), b2 = (1.0 + cs) * 0.5;
            a0 = 1.0 + al, a1 = -2.0 * cs, a2 = 1.0 - al;
        }
        else
        {
            b0 = (1.0 - cs) * 0.5, b1 = 1.0 - cs, b2 = (1.0 - cs) * 0.5;
            a0 = 1.0 + al, a1 = -2.0 * cs, a2 = 1.0 - al;
        }
        const double g = 1.0 / a0;
        s.b0 = b0 * g, s.b1 = b1 * g, s.b2 = b2 * g, s.a1 = a1 * g, s.a2 = a2 * g;
    }
    void formant (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        const float c1 = std::clamp (p[valueParam], 0.0f, 1.0f) * 32768.0f, c2 = std::clamp (p[valueParam + 1], 0.0f, 1.0f) * 32768.0f;
        const float q = std::max (std::clamp (p[valueParam + 2], 0.0f, 1.0f) * 64.0f, 1.0e-3f);
        const float lows = p[valueParam + 3] >= 0.5f ? 1.0f : 0.0f, highs = p[valueParam + 4] >= 0.5f ? 1.0f : 0.0f;
        const float key[6] { c1, c2, q, lows, highs, (float) fs };
        if (! std::equal (key, key + 6, fmFor))
        {
            std::copy (key, key + 6, fmFor);
            const float db = std::log10 (q) * 20.0f;
            fmDesign (fm[0], lows > 0.0f ? 5 : 2, fs, (double) std::min (c1, c2), (double) q, (double) db);
            fmDesign (fm[1], highs > 0.0f ? 5 : 0, fs, (double) std::max (c1, c2), (double) q, (double) db);
        }
        float* io[2] { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = io[c][i];
                const auto v = (float) fm[0].run (c, (double) x);
                const auto y = (float) fm[1].run (c, (double) v);
                io[c][i] = mix >= 1.0f ? y : x + mix * (y - x);
            }
    }
    // ---- round 190qz: Delay (law 55) ------------------------------------------------------------------------------
    // [STATIC] DelayDsp (REA_rodada190qz-khs §4): one stereo line at T seconds (synced: steps × the note at the tempo,
    // as the Dual Delay), at most 5 s. A change of T glides: the time moves linearly from where it was to the new one
    // over 50 ms (then per sample across the block; while it moves the line is read between samples), else the read is
    // round (T fs − 7) + 7 samples back from the newest before the write (each echo one sample after that), clamped to what
    // has been written (from 15). Into the line: the input through the pan gains (√2 cos, √2 sin of (pan + 1) π/4,
    // crossfaded across a block on a change) plus feedback × the read, Bounce (ping-pong) crossing the channels, each
    // through the Dual Delay's tone shelf (48 dB a second of echo, the current time). Ducking as the Dual Delay's (an RMS
    // follower from 0.005, k = ½ (1 − 2^(−1 / (50 ms · fs))), depth 10 duck²), but here it scales the mix:
    // out = x + clamp (1 − e · depth, 0, 1) · mix · (wet − x). Asleep after T + 64 samples of silence (a block whose
    // echoes reach 2^-20 counts as sound); the follower relaxes while asleep: e = (1 − k)^n (e − 2^-20) + 2^-20.
    // Encodings: [13] time / 16 s, [14] steps / 64, [15] the denominator's index / 6, [16] sync (1), [17] feedback,
    // [18] pan (v + 1) / 2, [19] bounce (1), [20] duck, [21] tone (v + 1) / 2; the mix is Wet.
    std::array<Line, 2> dlLines {};
    float dlTime = 0.0f, dlFrom = 0.0f, dlTarget = -1.0f, dlGlide = 1.0f;
    bool dlGlideFresh = true;
    Ramp dlT, dlFb, dlDuck, dlMix;
    std::array<float, 2> dlPanOld {}, dlPanNew {};
    float dlPanT = 0.0f, dlPanInc = 0.0f;
    bool dlPanFresh = true;
    float dlEnv = 0.005f;
    bool dlAsleep = true;
    double dlS1[2] {}, dlS2[2] {};
    void dlReset() noexcept
    {
        dlLines[0].ring = &rev[0], dlLines[1].ring = &rev[1];
        for (auto& ln : dlLines)
            ln.clear(), ln.fill = 15;
        dlTime = dlFrom = 0.0f, dlTarget = -1.0f, dlGlide = 1.0f;
        dlGlideFresh = true;
        for (auto* r : { &dlT, &dlFb, &dlDuck, &dlMix })
            *r = {};
        dlPanOld = dlPanNew = {};
        dlPanT = dlPanInc = 0.0f;
        dlPanFresh = true;
        dlEnv = 0.005f;
        dlAsleep = true;
        dlS1[0] = dlS1[1] = dlS2[0] = dlS2[1] = 0.0;
    }
    void delay (float* l, float* r, int n, const float* p, float mix, double bpm) noexcept
    {
        if (! rev[0].ready())
            return;
        const float sr = (float) fs;
        float t;
        if (p[valueParam + 3] < 0.5f)
            t = std::clamp (p[valueParam], 0.0f, 1.0f) * enc::ddSeconds;
        else
        {
            const int steps = std::max (1, (int) std::lround (64.0 * std::clamp ((double) p[valueParam + 1], 0.0, 1.0)));
            const int den = (int) std::lround (6.0 * std::clamp ((double) p[valueParam + 2], 0.0, 1.0));
            t = (float) ((double) steps * enc::reverserBeats (den) / (double) (float) (std::max (bpm, 1.0) / 60.0));
        }
        t = std::min (std::max (t, 0.0f), 5.0f);
        // the glide (50 ms) towards a new time
        if (std::abs (t - dlTarget) >= 1.1920928955078125e-07f)
            dlFrom = dlTime, dlGlide = 0.0f;
        dlTarget = t;
        if (dlGlideFresh)
            dlTime = t, dlGlide = 1.0f, dlGlideFresh = false;
        else
        {
            if (dlGlide < 1.0f)
            {
                dlGlide = (float) n / (0.05f * sr) + dlGlide;
                if (dlGlide < 1.0f)
                    t = (t - dlFrom) * dlGlide + dlFrom;
            }
            dlTime = t;
        }
        const bool moving = ! dlT.fresh && std::abs (t - dlT.target) >= 1e-15f;
        dlT.set (t, n);
        const float fbk = std::clamp (p[valueParam + 4], 0.0f, 1.0f);
        dlFb.set (fbk, n);
        const float pan = std::clamp (p[valueParam + 5], 0.0f, 1.0f);
        const float ang = pan * 1.5707963705062866f;
        const std::array<float, 2> pg { (float) ((double) std::cos (ang) * 1.4142135623730951), (float) ((double) std::sin (ang) * 1.4142135623730951) };
        if (dlPanFresh)
            dlPanOld = dlPanNew = pg, dlPanInc = 0.0f, dlPanFresh = false;
        else
            dlPanOld = dlPanNew, dlPanNew = pg, dlPanInc = 1.0f / (float) n;
        dlPanT = 0.0f;
        const bool bounce = p[valueParam + 6] >= 0.5f;
        const float duck = std::clamp (p[valueParam + 7], 0.0f, 1.0f);
        dlDuck.set (duck * duck * 10.0f, n);
        dlMix.set (std::clamp (mix, 0.0f, 1.0f), n);
        const float tone = 2.0f * std::clamp (p[valueParam + 8], 0.0f, 1.0f) - 1.0f;
        // the tone shelf at the current time
        const float x = t * 48.0f * tone;
        double gH = 1.0, gL = 1.0, gB;
        if (x >= 0.0f)
        {
            const float g = std::exp2 (x * -0.16609640419483185f);
            gL = (double) g, gB = (double) (g * 0.5857864618301392f + 1.4142135381698608f);
        }
        else
        {
            const float g = std::exp2 (x * 0.16609640419483185f);
            gH = (double) g, gB = (double) (g * 0.5857864618301392f + 1.4142135381698608f);
        }
        const double gc = std::tan (std::min (0.4999 * (double) sr, 800.0) * pi / (double) sr);
        const double A = 2.0 + gc, D = 1.0 / (A * gc + 1.0), E = D * gc, G = gc + gc;
        const float k = ddFollow();
        const bool wasAsleep = dlAsleep;
        dlAsleep = sleeping (l, r, n, (double) (t * sr) + 3.0);
        // a block that wakes: its leading silence still sleeps (HeartCore wakes on the first sound; the references show no
        // follower steps before it, whatever the host's block grid), so the result does not depend on the block size
        int from = 0;
        if (wasAsleep && ! dlAsleep)
            while (from < n && l[from] == 0.0f && r[from] == 0.0f)
                ++from;
        if (dlAsleep || from > 0)
        {
            dlEnv = (float) std::pow (1.0 - (double) k, (double) (dlAsleep ? n : from)) * (dlEnv - 9.5367431640625e-07f) + 9.5367431640625e-07f;
            if (dlAsleep)
                return;
        }
        const auto maxRead = (std::uint32_t) dlLines[0].ring->mask;
        float peak = 0.0f;
        for (int i = from; i < n; ++i)
        {
            const float tv = dlT.step();
            dlPanT += 1.0f;
            const float pf = dlPanT * dlPanInc, pa = 1.0f - pf;
            const float pl = pf * dlPanNew[0] + pa * dlPanOld[0], pr = pf * dlPanNew[1] + pa * dlPanOld[1];
            const float fb = dlFb.step(), dk = dlDuck.step(), mx = dlMix.step();
            const float x0[2] { l[i], r[i] };
            float a[2];
            if (moving)
            {
                const float d = std::max (tv * sr - 1.0f, 0.0f);
                for (int c = 0; c < 2; ++c)
                    a[c] = dlLines[(std::size_t) c].catmull (std::min (d, (float) maxRead - 4.0f));
            }
            else
            {
                const auto d = (std::uint32_t) std::min ((int) std::round (tv * sr - 7.0f) + 7, (int) maxRead);
                for (int c = 0; c < 2; ++c)
                    a[c] = dlLines[(std::size_t) c].at (d);
            }
            const double u[2] { (double) (fb * a[0] + (bounce ? pr : pl) * x0[0]), (double) (fb * a[1] + (bounce ? pl : pr) * x0[1]) };
            float w[2];
            for (int c = 0; c < 2; ++c)
            {
                double& s1 = dlS1[c];
                double& s2 = dlS2[c];
                const double v = gc * u[c] + s1;
                const double hp = u[c] - (v * A + s2) * D;
                const double bp = hp * gc + s1;
                const double lp = (v - s2 * gc) * E + s2;
                s1 = ((u[c] - lp) - (bp + bp)) * G + s1;
                s2 = bp * G + s2;
                w[c] = (float) (hp * gH + lp * gL + bp * gB);
            }
            if (bounce)
                std::swap (w[0], w[1]);
            dlLines[0].push (w[0]), dlLines[1].push (w[1]);
            peak = std::max (peak, std::max (std::abs (a[0]), std::abs (a[1])));
            const float pw = std::max ((x0[0] * x0[0] + x0[1] * x0[1]) * 0.5f, 9.5367431640625e-07f);
            dlEnv = (pw / dlEnv - dlEnv) * k + dlEnv;
            const float gain = std::min (1.0f, std::max (0.0f, 1.0f - dlEnv * dk)) * mx;
            l[i] = gain * (a[0] - x0[0]) + x0[0];
            r[i] = gain * (a[1] - x0[1]) + x0[1];
        }
        if (peak > 9.5367431640625e-07f)
            quietRun = 0;
    }
    // ---- round 190qz: Clipper (law 56) -----------------------------------------------------------------------------
    // [STATIC] ClipperDsp (REA_rodada190qz-khs §5): memoryless, at the base rate. u = x · pre gain, a = |u|; with the
    // threshold T and knee K: a ≤ (1 − K) T passes, a > (1 + K) T gives T, between them a quadratic knee
    // T (1 − (a / T − 1 − K)² / 4K); the sign restored, × post gain. The four values ramp across a block on a change.
    // Encodings: [13] threshold / 2, [14] knee, [15] pre gain / 16, [16] post gain / 16 (linear gains: the plug-in's
    // floats come back exactly); the mix is Wet (the plug-in has none: 1).
    Ramp clT, clK, clPre, clPost;
    void clReset() noexcept
    {
        for (auto* r : { &clT, &clK, &clPre, &clPost })
            *r = {};
    }
    void clipper (float* l, float* r, int n, const float* p, float mix) noexcept
    {
        clT.set (std::clamp (p[valueParam], 0.0f, 1.0f) * 2.0f, n);
        clK.set (std::clamp (p[valueParam + 1], 0.0f, 1.0f), n);
        clPre.set (std::clamp (p[valueParam + 2], 0.0f, 1.0f) * 16.0f, n);
        clPost.set (std::clamp (p[valueParam + 3], 0.0f, 1.0f) * 16.0f, n);
        auto shape = [] (float u, float T, float K) noexcept {
            const float sg = u < 0.0f ? -1.0f : 1.0f, a = sg * u;
            float y = T;
            if (a <= (K + 1.0f) * T)
            {
                if (a <= (1.0f - K) * T)
                    y = a;
                else
                {
                    const float z = (-1.0f - K) + a / T;
                    y = ((z * z) / (K * -4.0f) + 1.0f) * T;
                }
            }
            return y * sg;
        };
        for (int i = 0; i < n; ++i)
        {
            const float T = clT.step(), K = clK.step(), pre = clPre.step(), post = clPost.step();
            const float xl = l[i], xr = r[i];
            const float yl = shape (xl * pre, T, K) * post, yr = shape (pre * xr, T, K) * post;
            l[i] = mix >= 1.0f ? yl : xl + mix * (yl - xl);
            r[i] = mix >= 1.0f ? yr : xr + mix * (yr - xr);
        }
    }
};
} // namespace lynx::khsdsp
