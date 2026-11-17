#pragma once

// Soundboard (round 190kk): LYNX's resonant body — the input rings a bank of tuned modes laid out like one of seven
// objects (beam, marimba bar, string, membrane, plate, pipe, tube). It is the engine an imported Ableton Live "Corpus"
// opens on. Clean-room: the mode tables and laws below come from LYNX's own analysis of Live 12.4's impulse responses
// (round 190kk, /mnt/e/lynx-work/livedev190kk RESULTS.md §3: 7 objects, Tune, Decay, Material, Brightness, Inharmonics,
// Ratio, Radius, Hit, Listening, Quality, Spread, Gain, Bleed, Dry/Wet, Width, Filter); textbook physics where it fits
// (the free beam's modes, a rectangular membrane / plate), nothing of Live's code or content.
//
// Measured facts the engine keeps:
//   - each channel rings its own bank (a left-only input gives a left-only output) and is linear at any level;
//   - Tune (0..1): 66.1 / 261.7 / 4186 Hz at 0.25 / 0.5 / 1 for the first mode (≈ C0 … C8); Spread ±1 = ±1 semitone, left up;
//   - modes: beam (1, 2.758, 5.406, 8.936 …), marimba (1, 3.962, 10.673, 19.177, then twice the beam's), string (1, 2, 3 …),
//     membrane √(m² + (r n)²), plate m² + (r n)² (r = 1 / (1 + 7 · Ratio), fitted at 0 / 0.5 / 1 within 0.3 %), pipe (all
//     harmonics, the pitch falling with Radius), tube (odd harmonics); Inharmonics i raises a mode's ratio to the power
//     1 + i (i = 1: squared, i = −1: every mode on the first);
//   - Quality keeps 4 / 16 / 31 / 72 modes;
//   - Decay: the first mode's T60 0.10 / 0.20 / 1.25 / 7 / 31 s at 0 / 0.25 / 0.5 / 0.75 / 1 (pipe twice as long);
//     Material m tilts the damping over the modes (m = −1: the rate grows as the mode ratio, m = +1: the first mode dies
//     9× faster and the rate falls as ratio^−0.57); Brightness b scales mode amplitudes by ratio^b;
//   - Hit / Listening positions as sin (k π p) products with p = 0.5 + 0.225 · x [FITTED on the string, 3 dB mean];
//   - Gain: +30 dB at 1 (linear in dB), a limit at 0 dBFS; Bleed: the dry input high-passed (≈ 4 kHz), 40 dB per unit,
//     −35 dB at 1; Dry/Wet 0.5 = dry × 0.849, wet × 0.876; Width 0 = the two banks summed.
// Header-only DSP, no allocation in process(); the Effect wrapper lives in core/MixSuite.cpp (core/LiveFxSuite.inc).

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace lynx::soundboard
{
enum Param
{
    type, quality, tune, decay, material, brightness, inharmonics, radius, opening, ratio, hit, listenL, listenR, spread,
    gain, bleed, dryWet, width, filterOn, filterHz, filterWidth, count
};
enum class Kind { beam, marimba, string, membrane, plate, pipe, tube };
constexpr int kinds = 7;
constexpr int maxModes = 160; // (pipes and tubes at the top Quality: their harmonics reach the top of the audio band)

// ---- the knob laws (all parameters 0..1) ---------------------------------------------------------------------------
inline Kind kindOf (float v) noexcept { return (Kind) std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 6.0f), 0, 6); }
inline float kindValue (Kind k) noexcept { return (float) (int) k / 6.0f; }
inline const char* kindName (Kind k) noexcept
{
    static const char* const names[kinds] { "Beam", "Marimba", "String", "Membrane", "Plate", "Pipe", "Tube" };
    return names[(int) k];
}
inline int qualityOf (float v) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 3.0f), 0, 3); }
inline float qualityValue (int q) noexcept { return (float) std::clamp (q, 0, 3) / 3.0f; }
inline int modeCount (int q) noexcept
{
    static constexpr int counts[4] { 4, 16, 31, 72 };
    return counts[std::clamp (q, 0, 3)];
}
// Tune: the first mode's Hz [MEASURED 32.6 / 66.1 / 132.9 / 261.7 / 1058 / 4186 Hz at 0.125 / 0.25 / 0.375 / 0.5 / 0.75 / 1;
// 0 extrapolated], log-linear between the points (close to 261.6 Hz · 256^(v − 0.5), C0 … C8)
inline double tuneHz (float v) noexcept
{
    static constexpr double at[7] { 0.0, 0.125, 0.25, 0.375, 0.5, 0.75, 1.0 }, hz[7] { 16.07, 32.6, 66.1, 132.9, 261.7, 1058.0, 4186.0 };
    const auto x = std::clamp ((double) v, 0.0, 1.0);
    int k = 0;
    while (k < 5 && x > at[k + 1])
        ++k;
    const auto f = (x - at[k]) / (at[k + 1] - at[k]);
    return std::exp (std::log (hz[k]) + f * (std::log (hz[k + 1]) - std::log (hz[k])));
}
inline double bipolar (float v) noexcept { return std::clamp ((double) v, 0.0, 1.0) * 2.0 - 1.0; } // −1 … +1
inline float bipolarValue (double x) noexcept { return (float) std::clamp ((x + 1.0) * 0.5, 0.0, 1.0); }
inline double filterHzOf (float v) noexcept { return 50.0 * std::pow (360.0, (double) std::clamp (v, 0.0f, 1.0f)); } // 50 Hz … 18 kHz
inline float filterHzValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 1.0) / 50.0) / std::log (360.0), 0.0, 1.0); }
inline double filterOctaves (float v) noexcept { return 0.5 + 8.5 * (double) std::clamp (v, 0.0f, 1.0f); } // 0.5 … 9
inline float filterOctavesValue (double o) noexcept { return (float) std::clamp ((o - 0.5) / 8.5, 0.0, 1.0); }
inline double gainDb (float v) noexcept { return 30.0 * (double) std::clamp (v, 0.0f, 1.0f); }
inline bool on (float v) noexcept { return v >= 0.5f; }

// ---- the measured laws ---------------------------------------------------------------------------------------------
// T60 of the first mode (s) for Decay: log-linear between the measured knots
inline double decaySeconds (double d) noexcept
{
    static constexpr double at[5] { 0.0, 0.25, 0.5, 0.75, 1.0 }, t60[5] { 0.10, 0.20, 1.25, 7.0, 31.0 };
    const auto x = std::clamp (d, 0.0, 1.0);
    int k = 0;
    while (k < 3 && x > at[k + 1])
        ++k;
    const auto f = (x - at[k]) / (at[k + 1] - at[k]);
    return std::exp (std::log (t60[k]) + f * (std::log (t60[k + 1]) - std::log (t60[k])));
}
// how much faster mode `ratio` dies than the first one, for Material m (−1 … 1)
inline double materialRate (double ratio, double m) noexcept
{
    const auto r = std::max (ratio, 1.0);
    const auto slope = m < 0.0 ? 0.04 - 0.96 * m : 0.04 - 0.61 * m;
    const auto first = m > 0.0 ? std::pow (8.9, m) : 1.0;
    return first * std::pow (r, slope);
}
// the pipe's / tube's first mode against Tune, by Radius [MEASURED 0.931 / 0.990 / 0.997 (pipe), 0.869 / 0.980 / 0.994 (tube)]
inline double pipePitch (double radius, bool tube) noexcept
{
    const auto r = std::clamp (radius, 0.0, 1.0);
    return tube ? 0.869 + 0.319 * r - 0.194 * r * r : 0.931 + 0.170 * r - 0.104 * r * r;
}
// the pipe's / tube's harmonic roll-off (dB down at harmonic k) at Radius 0.5; Radius 1 is flat, 0 twice as steep
inline double pipeRolloffDb (double k) noexcept { return 30.0 * (1.0 - std::exp (-(std::max (k, 1.0) - 1.0) / 18.0)); }
// the level of each object against Live's, dB [FITTED on the base impulse responses (Tune 0.25, Decay 0.5), round 190kk:
// third-octave band levels within 40 dB of the loudest]
constexpr double kindGainDb[kinds] { -16.5, -17.9, -16.8, -8.8, -15.4, -17.65, -15.3 };

// The membrane's level against Tune, dB [FITTED on its Tune sweep: Live's level against Soundboard's at −2 … +6 octaves
// from 66 Hz]
inline double membraneTuneDb (double hz) noexcept
{
    static constexpr double oct[7] { -2.0, -1.0, 0.0, 1.0, 2.0, 4.0, 6.0 }, dbs[7] { 10.15, 5.43, 0.0, -5.37, -7.13, -10.6, -11.03 };
    const auto x = std::clamp (std::log2 (std::max (hz, 1.0) / 66.1), -2.0, 6.0);
    int k = 0;
    while (k < 5 && x > oct[k + 1])
        ++k;
    return dbs[k] + (dbs[k + 1] - dbs[k]) * (x - oct[k]) / (oct[k + 1] - oct[k]);
}

// The mode ratios of an object (sorted, the first = 1) — Inharmonics not applied
inline int baseRatios (Kind kind, double ratioParam, bool openTube, std::array<double, maxModes>& out, std::array<std::pair<int, int>, maxModes>& index) noexcept
{
    int n = 0;
    const auto beamRatio = [] (int k) {
        static constexpr double beta[5] { 4.7300408, 7.8532046, 10.9956078, 14.1371655, 17.2787597 };
        const auto b = k < 5 ? beta[k] : (2.0 * k + 3.0) * 1.5707963267948966;
        return (b / beta[0]) * (b / beta[0]);
    };
    switch (kind)
    {
        case Kind::beam:
            for (; n < maxModes; ++n) { out[(std::size_t) n] = beamRatio (n); index[(std::size_t) n] = { n + 1, 1 }; }
            break;
        case Kind::marimba:
        {
            static constexpr double first[4] { 1.0, 3.962, 10.673, 19.177 };
            for (; n < maxModes; ++n)
            {
                out[(std::size_t) n] = n < 4 ? first[n] : 2.0 * beamRatio (n);
                index[(std::size_t) n] = { n + 1, 1 };
            }
            break;
        }
        case Kind::string:
            for (; n < maxModes; ++n) { out[(std::size_t) n] = n + 1.0; index[(std::size_t) n] = { n + 1, 1 }; }
            break;
        case Kind::membrane:
        case Kind::plate:
        {
            const auto r = 1.0 / (1.0 + 7.0 * std::clamp (ratioParam, 0.0, 1.0));
            const auto sq = kind == Kind::plate;
            std::array<std::pair<double, std::pair<int, int>>, 16 * 16> all {};
            int k = 0;
            for (int m = 1; m <= 16; ++m)
                for (int j = 1; j <= 16; ++j)
                {
                    const auto e = (double) m * m + (r * j) * (r * j);
                    all[(std::size_t) k++] = { sq ? e : std::sqrt (e), { m, j } };
                }
            std::sort (all.begin(), all.end());
            for (; n < maxModes; ++n)
            {
                out[(std::size_t) n] = all[(std::size_t) n].first / all[0].first;
                index[(std::size_t) n] = all[(std::size_t) n].second;
            }
            break;
        }
        case Kind::pipe:
            for (; n < maxModes; ++n) { out[(std::size_t) n] = n + 1.0; index[(std::size_t) n] = { n + 1, 1 }; }
            break;
        case Kind::tube:
            // closed at one end: odd harmonics; an Opening under 50 % opens it: every harmonic of the octave above
            // [MEASURED at Opening 0: harmonics of 2 × the pitch]
            for (; n < maxModes; ++n)
            {
                out[(std::size_t) n] = openTube ? 2.0 * (n + 1.0) : 2.0 * n + 1.0;
                index[(std::size_t) n] = { openTube ? 2 * (n + 1) : 2 * n + 1, 1 };
            }
            break;
    }
    return n;
}

class Engine
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
        dirty = true;
        fade = 0;
    }
    void reset() noexcept
    {
        for (auto& b : banks)
            for (auto& z : b.state)
                z = {};
        for (auto& f : inFilter)
            f = {};
        for (auto& f : bleedFilter)
            f = {};
    }

    void process (float* l, float* r, int n, const float* p) noexcept
    {
        if (dirty || ! same (p))
        {
            // a new object, Quality or Opening moves every mode: the ringing fades out over 5 ms, the banks restart
            // empty and fade back in (no click); other changes retune the modes in place
            const auto structural = ! dirty && (kindOf (p[type]) != kindOf (last[type]) || qualityOf (p[quality]) != qualityOf (last[quality])
                                                || (p[opening] < 0.5f) != (last[opening] < 0.5f));
            if (structural && fade <= 0)
                fade = fadeLength();
            if (fade <= 0)
                design (p);
        }
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            double fadeGain = 1.0;
            if (fade > 0)
            {
                const auto len = fadeLength();
                if (fade > len / 2)
                    fadeGain = (double) (fade - len / 2) / (double) (len / 2); // out
                else
                {
                    if (fade == len / 2)
                    {
                        reset();
                        design (p);
                    }
                    fadeGain = 1.0 - (double) fade / (double) (len / 2); // in
                }
                --fade;
            }
            double wet[2] {}, dry[2] {};
            for (int ch = 0; ch < 2; ++ch)
            {
                dry[ch] = io[ch][i];
                auto x = dry[ch];
                if (useFilter)
                    x = biquad (inFilter[(std::size_t) ch], filterC, x);
                auto& bank = banks[(std::size_t) ch];
                double acc = 0.0;
                for (int k = 0; k < modes; ++k)
                {
                    auto& z = bank.state[(std::size_t) k];
                    z = bank.pole[(std::size_t) k] * z + x;
                    acc += bank.weight[(std::size_t) k] * z.imag();
                }
                wet[ch] = acc * outGain * fadeGain;
                if (bleedGain > 0.0)
                    wet[ch] += bleedGain * biquad (bleedFilter[(std::size_t) ch], bleedC, dry[ch]);
            }
            // Width: 0 sums the two banks
            const auto mid = 0.5 * (wet[0] + wet[1]), side = 0.5 * (wet[0] - wet[1]) * widthAmount;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto y = (ch == 0 ? mid + side : mid - side) * wetGain + dry[ch] * dryGain;
                // a limit at 0 dBFS (soft above −1 dBFS)
                const auto a = std::abs (y);
                if (a > 0.891)
                    y = std::copysign (0.891 + 0.109 * std::tanh ((a - 0.891) / 0.109), y);
                io[ch][i] = (float) y;
            }
        }
    }

private:
    int fadeLength() const noexcept { return 2 * std::max (8, (int) (0.005 * rate)); }
    int fade = 0;

    struct Bank
    {
        std::array<std::complex<double>, maxModes> pole {}, state {};
        std::array<double, maxModes> weight {};
    };
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };
    struct State
    {
        double s1 = 0.0, s2 = 0.0;
    };
    static double biquad (State& s, const Biquad& c, double x) noexcept
    {
        const auto y = c.b0 * x + s.s1;
        s.s1 = c.b1 * x - c.a1 * y + s.s2;
        s.s2 = c.b2 * x - c.a2 * y;
        return y;
    }
    bool same (const float* p) const noexcept
    {
        for (int k = 0; k < count; ++k)
            if (p[k] != last[(std::size_t) k])
                return false;
        return true;
    }

    void design (const float* p) noexcept
    {
        dirty = false;
        for (int k = 0; k < count; ++k)
            last[(std::size_t) k] = p[k];
        const auto kind = kindOf (p[type]);
        const auto inh = bipolar (p[inharmonics]);
        std::array<double, maxModes> ratios {};
        std::array<std::pair<int, int>, maxModes> index {};
        baseRatios (kind, p[ratio], p[opening] < 0.5f, ratios, index);
        const auto tubeLike = kind == Kind::pipe || kind == Kind::tube;
        const auto f1 = tuneHz (p[tune]) * (tubeLike ? pipePitch (p[radius], kind == Kind::tube) : 1.0);
        const auto t60 = decaySeconds (p[decay]) * (kind == Kind::pipe ? 2.0 : 1.0);
        const auto m = bipolar (p[material]), b = bipolar (p[brightness]);
        const auto sp = bipolar (p[spread]);
        const double shift[2] { std::pow (2.0, sp / 12.0), std::pow (2.0, -sp / 12.0) };
        const auto hitPos = 0.5 + 0.225 * (double) std::clamp (p[hit], 0.0f, 1.0f);
        const double listenPos[2] { 0.5 + 0.225 * (double) std::clamp (p[listenL], 0.0f, 1.0f), 0.5 + 0.225 * (double) std::clamp (p[listenR], 0.0f, 1.0f) };
        // a pipe's / tube's harmonics at the top Quality run as high as they can (Live's wide pipes sound bright up to the top
        // of the band [MEASURED at Radius 1 / Opening < 1]); the other objects keep the measured counts
        const auto wanted = tubeLike && qualityOf (p[quality]) == 3 ? maxModes : modeCount (qualityOf (p[quality]));
        const auto rad = std::clamp ((double) p[radius], 0.0, 1.0);
        modes = 0;
        double energy[2] {};
        for (int k = 0; k < wanted; ++k)
        {
            const auto rk = std::pow (ratios[(std::size_t) k], 1.0 + inh);
            const auto [mi, ni] = index[(std::size_t) k];
            double shape[2] {};
            for (int ch = 0; ch < 2; ++ch)
            {
                double s = 1.0, ref = 1.0;
                if (! tubeLike)
                {
                    s = std::abs (std::sin (mi * 3.141592653589793 * hitPos) * std::sin (mi * 3.141592653589793 * listenPos[ch]));
                    if (kind == Kind::membrane || kind == Kind::plate)
                        s *= std::abs (std::sin (ni * 3.141592653589793 * hitPos) * std::sin (ni * 3.141592653589793 * listenPos[ch]));
                    ref = s;
                }
                else
                {
                    s = std::pow (10.0, -pipeRolloffDb ((double) mi) * (1.0 - rad) / 0.5 / 20.0);
                    // a wide pipe loses its first mode [MEASURED at Radius 1: the harmonics start at the second]
                    if (kind == Kind::pipe && mi == 1)
                        s *= std::clamp (2.0 * (1.0 - rad), 0.0, 1.0);
                    ref = std::pow (10.0, -pipeRolloffDb ((double) mi) * (1.5 - rad) / 2.0 / 0.5 / 20.0); // (half way to Radius 0.5)
                }
                const auto tilt = std::pow (std::max (rk, 1.0e-3), b);
                shape[ch] = s * tilt;
                // the energy the level is set by (the modes that sound): a darkening Brightness counts in full, a
                // brightening one by its square root [FITTED: Brightness +1 / −1 against 0 in Live's renders]
                const auto e = ref * (b < 0.0 ? tilt : std::sqrt (tilt));
                if (f1 * rk < 0.45 * rate)
                    energy[ch] += e * e;
            }
            bool any = false;
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto hz = f1 * rk * shift[ch];
                auto& bank = banks[(std::size_t) ch];
                if (hz >= 0.45 * rate || hz <= 1.0)
                {
                    bank.pole[(std::size_t) modes] = {};
                    bank.weight[(std::size_t) modes] = 0.0;
                    continue;
                }
                any = true;
                const auto t = t60 / materialRate (rk, m);
                const auto radiusPole = std::exp (-6.907755278982137 / (std::max (t, 0.002) * rate));
                bank.pole[(std::size_t) modes] = std::polar (radiusPole, 6.283185307179586 * hz / rate);
                bank.weight[(std::size_t) modes] = shape[ch];
            }
            if (any)
                ++modes;
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto norm = energy[ch] > 1.0e-12 ? 1.0 / std::sqrt (energy[ch]) : 0.0;
            for (int k = 0; k < modes; ++k)
                banks[(std::size_t) ch].weight[(std::size_t) k] *= norm;
        }
        // an opened pipe / tube rings louder [FITTED: +5 dB at Opening 0.5 (pipe), +10 dB at 0 (tube)]
        const auto openDb = tubeLike ? 10.0 * (1.0 - std::clamp ((double) p[opening], 0.0, 1.0)) : 0.0;
        outGain = 0.05 * std::pow (10.0, (kindGainDb[(int) kind] + (kind == Kind::membrane ? membraneTuneDb (f1) : 0.0) + openDb + gainDb (p[gain])) / 20.0);
        // Bleed: the dry input through a 2nd-order high pass at 4 kHz (Q 2), 40 dB per unit, −35 dB at 1
        const auto bl = std::clamp ((double) p[bleed], 0.0, 1.0);
        bleedGain = bl > 0.0 ? std::pow (10.0, (-35.0 + 40.0 * (bl - 1.0)) / 20.0) : 0.0;
        bleedC = highPass (4000.0, 2.0);
        // Dry/Wet [MEASURED at 0.5: dry 0.849, wet 0.876]: power laws through that point
        const auto w = std::clamp ((double) p[dryWet], 0.0, 1.0);
        dryGain = std::pow (1.0 - w, 0.236);
        wetGain = std::pow (w, 0.191);
        widthAmount = std::clamp ((double) p[width], 0.0, 1.0);
        useFilter = on (p[filterOn]);
        filterC = bandPass (filterHzOf (p[filterHz]), filterOctaves (p[filterWidth]));
    }
    Biquad highPass (double hz, double q) const noexcept
    {
        const auto w0 = 6.283185307179586 * std::min (hz, 0.45 * rate) / rate, c = std::cos (w0), al = std::sin (w0) / (2.0 * q), a0 = 1.0 + al;
        return { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    }
    Biquad bandPass (double hz, double octaves) const noexcept
    {
        const auto w0 = 6.283185307179586 * std::min (hz, 0.45 * rate) / rate, sn = std::sin (w0), cs = std::cos (w0);
        const auto al = sn * std::sinh (0.34657359027997264 * octaves * w0 / std::max (sn, 1.0e-9)), a0 = 1.0 + al;
        return { al / a0, 0.0, -al / a0, -2.0 * cs / a0, (1.0 - al) / a0 };
    }

    double rate = 48000.0;
    std::array<Bank, 2> banks {};
    int modes = 0;
    std::array<float, count> last {};
    bool dirty = true;
    double outGain = 0.05, bleedGain = 0.0, dryGain = 0.0, wetGain = 1.0, widthAmount = 1.0;
    bool useFilter = false;
    Biquad bleedC, filterC;
    std::array<State, 2> inFilter {}, bleedFilter {};
};
} // namespace lynx::soundboard
