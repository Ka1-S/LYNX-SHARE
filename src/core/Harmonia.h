#pragma once

// Harmonia (round 190ko): LYNX's spectral resonator. The input's spectrum (core/SpectralCore.h, after an optional
// shift of its pitch) excites a bank of partials — the harmonics of a frequency, stretched or squeezed, up to 256 of
// them — that ring on with a decay, damped at the high and low ends; the partials can be chorused, made to wander, or
// broken into random grains, doubled by detuned unison copies, and the result is limited and mixed with the dry
// signal. It is the engine an imported Ableton Live "Spectral Resonator" opens on. Clean-room: every law below comes
// from LYNX's own measurements of Live 12.4's output on impulses, tones and drums (round 190ko,
// /mnt/e/lynx-work/meld190ko, set s1, RESULTS.md §3); nothing of Live's code or content is used. The partials are an
// oscillator bank (complex rotators, amplitudes moved once a hop), so they sound at their exact frequencies whatever
// the analysis size. Header-only DSP, no allocation in process().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/SpectralCore.h"

namespace lynx::harmonia
{
// (the Device View's card shows the first seven)
enum Param
{
    frequency, decay, stretch, shift, hfDamp, lfDamp, mix, modMode, modRate, pitchMod, harmonics, unison, unisonAmount, input, count
};

// ---- the knob laws (all parameters 0..1) ---------------------------------------------------------------------------
inline double logLaw (float v, double lo, double hi) noexcept { return lo * std::pow (hi / lo, (double) std::clamp (v, 0.0f, 1.0f)); }
inline float logValue (double x, double lo, double hi) noexcept
{
    return (float) std::clamp (std::log (std::max (x, lo) / lo) / std::log (hi / lo), 0.0, 1.0);
}
inline double linLaw (float v, double lo, double hi) noexcept { return lo + (hi - lo) * (double) std::clamp (v, 0.0f, 1.0f); }
inline float linValue (double x, double lo, double hi) noexcept { return (float) std::clamp ((x - lo) / (hi - lo), 0.0, 1.0); }
inline int stepOf (float v, int steps) noexcept { return std::clamp ((int) std::lround (v * (float) (steps - 1)), 0, steps - 1); }
inline float stepValue (int i, int steps) noexcept { return steps > 1 ? (float) std::clamp (i, 0, steps - 1) / (float) (steps - 1) : 0.0f; }

inline double frequencyHz (float v) noexcept { return logLaw (v, 8.2, 2000.0); } // Live's Freq range [DATA: 8.2 … 2000 Hz]
inline float frequencyValue (double hz) noexcept { return logValue (hz, 8.2, 2000.0); }
inline double shiftSemis (float v) noexcept { return linLaw (v, -48.0, 48.0); }
inline double stretchOf (float v) noexcept { return linLaw (v, -1.0, 1.0); }
inline double decaySeconds (float v) noexcept { return logLaw (v, 0.001, 20.0); }
inline double pitchModSemis (float v) noexcept { return linLaw (v, 0.0, 4.0); }
inline int harmonicCount (float v) noexcept { return std::clamp ((int) std::lround (linLaw (v, 1.0, 256.0)), 1, 256); }
inline float harmonicValue (int n) noexcept { return linValue (n, 1.0, 256.0); }
inline int unisonVoices (float v) noexcept { return 1 + stepOf (v, 4); } // Unison 0…3 → 1…4 voices
inline double inputDb (float v) noexcept { return linLaw (v, -24.0, 24.0); }
inline int modeOf (float v) noexcept { return stepOf (v, 4); } // None, Chorus, Wander, Granular

// ---- the measured laws ---------------------------------------------------------------------------------------------
// the partial's frequency: n-th harmonic, stretched — Stretch 1 = odd harmonics only, −1 = squeezed towards the first
// [MEASURED s1 r04 stretch ±0.5 / ±1]
inline double partialRatio (int n, double s) noexcept { return 1.0 + (double) (n - 1) * (1.0 + s); }
// the decay of partial n: Decay is the first partial's T60; HF / LF damping shorten the high / low partials
// [MEASURED s1 r02, r03]
inline double partialT60 (double decay, double hz, double hf, double lf) noexcept
{
    const auto hi = std::pow (std::max (hz, 20.0) / 1000.0, -3.0 * hf);
    const auto lo = std::pow (std::max (hz, 20.0) / 1000.0, 3.0 * lf);
    return std::max (1.0e-3, decay * std::min (1.0, hi) * std::min (1.0, lo));
}

constexpr int frameSize = 2048;
constexpr int maxPartials = 256, maxUnison = 4;

class Engine
{
public:
    void prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        fft.prepare (frameSize);
        window.resize ((std::size_t) frameSize);
        for (int i = 0; i < frameSize; ++i)
            window[(std::size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * spectral::pi * i / frameSize));
        inL.assign ((std::size_t) frameSize, 0.0f);
        inR.assign ((std::size_t) frameSize, 0.0f);
        work.assign ((std::size_t) frameSize, {});
        specL.assign ((std::size_t) frameSize / 2 + 1, {});
        specR.assign ((std::size_t) frameSize / 2 + 1, {});
        dry.assign ((std::size_t) frameSize, {});
        reset();
    }
    void reset() noexcept
    {
        std::fill (inL.begin(), inL.end(), 0.0f);
        std::fill (inR.begin(), inR.end(), 0.0f);
        std::fill (dry.begin(), dry.end(), std::array<float, 2> {});
        for (auto& b : bank)
            b = Bank {};
        fill = 0, sinceHop = 0, dryPos = 0;
        limGain = 1.0f;
        rng.s = 0x13579bdu;
    }
    int latency() const noexcept { return latencySamples; }

    void process (float* l, float* r, int n, const float* p) noexcept
    {
        const auto wet = std::clamp (p[mix], 0.0f, 1.0f);
        const auto send = (float) std::pow (10.0, inputDb (p[input]) / 20.0);
        for (int i = 0; i < n; ++i)
        {
            // the analysis history
            inL[(std::size_t) fill] = l[i] * send;
            inR[(std::size_t) fill] = r[i] * send;
            fill = (fill + 1) % frameSize;
            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                analyse (p);
            }
            // the dry path, delayed like the wet one
            auto& d = dry[(std::size_t) dryPos];
            const auto dl = d[0], dr = d[1];
            d = { l[i], r[i] };
            dryPos = (dryPos + 1) % latencySamples;
            // the partials
            float yl = 0.0f, yr = 0.0f;
            synth (yl, yr);
            // the limiter (Live's: no clipping whatever Input Send does [MEASURED s1 r09])
            const auto peak = std::max (std::abs (yl), std::abs (yr));
            const auto want = peak * limGain > 0.98f ? 0.98f / peak : 1.0f;
            limGain = want < limGain ? want : limGain + (1.0f - limGain) * 0.0002f;
            yl *= limGain;
            yr *= limGain;
            l[i] = dl * (float) dryLaw (wet) + yl * (float) wetLaw (wet);
            r[i] = dr * (float) dryLaw (wet) + yr * (float) wetLaw (wet);
        }
    }

    // Dry/Wet [MEASURED s1 r10: MODEL until fitted — linear]
    static double dryLaw (double w) noexcept { return 1.0 - w; }
    static double wetLaw (double w) noexcept { return w; }

private:
    static constexpr int hop = frameSize / 4;
    static constexpr int latencySamples = frameSize / 2; // the analysis frame's centre
    struct Bank
    {
        int count = 0;
        std::array<float, maxPartials> re {}, im {}, wr {}, wi {}, ampL {}, ampR {}, dAmpL {}, dAmpR {}, targetL {}, targetR {}, hz {};
        std::array<float, maxPartials> modPhase {}, modValue {}, grain {};
    };

    void analyse (const float* p)
    {
        for (int j = 0; j < frameSize; ++j)
        {
            const auto s = (std::size_t) ((fill + j) % frameSize);
            work[(std::size_t) j] = spectral::cpx (inL[s] * window[(std::size_t) j], inR[s] * window[(std::size_t) j]);
        }
        fft.run (work.data(), false);
        spectral::unpack (work.data(), frameSize, specL.data(), specR.data());
        const auto binHz = rate / frameSize;
        const auto f0 = frequencyHz (p[frequency]);
        const auto st = stretchOf (p[stretch]);
        const auto sh = std::exp2 (shiftSemis (p[shift]) / 12.0);
        const auto H = harmonicCount (p[harmonics]);
        const auto U = unisonVoices (p[unison]);
        const auto uAmt = (double) std::clamp (p[unisonAmount], 0.0f, 1.0f);
        const auto dec = decaySeconds (p[decay]);
        const auto hf = (double) std::clamp (p[hfDamp], 0.0f, 1.0f), lf = (double) std::clamp (p[lfDamp], 0.0f, 1.0f);
        const auto mode = modeOf (p[modMode]);
        const auto mRate = (double) std::clamp (p[modRate], 0.0f, 1.0f);
        const auto pm = pitchModSemis (p[pitchMod]);
        const auto hopS = (double) hop / rate;
        const auto norm = 2.0f / (0.5f * (float) frameSize); // a sine of amplitude a gives a·N/4 at its bin (Hann)
        for (int u = 0; u < U; ++u)
        {
            auto& b = bank[(std::size_t) u];
            // unison copies detuned symmetrically [MEASURED s1 r08: MODEL]
            const auto cents = U > 1 ? (-1.0 + 2.0 * u / (U - 1)) * 50.0 * uAmt : 0.0;
            const auto detune = std::exp2 (cents / 1200.0);
            int count = 0;
            for (int n = 1; n <= H && count < maxPartials; ++n)
            {
                const auto fn = f0 * partialRatio (n, st) * detune;
                if (fn >= rate * 0.45 || fn <= 0.0)
                    break;
                auto& mphase = b.modPhase[(std::size_t) count];
                double freq = fn;
                float grainGain = 1.0f;
                if (mode == 1) // chorus: a triangle per partial on its pitch
                {
                    mphase = (float) std::fmod (mphase + hopS * (0.1 + 8.0 * mRate) * (1.0 + 0.1 * n), 1.0);
                    freq *= std::exp2 (pm * (1.0 - 4.0 * std::abs (mphase - 0.5)) / 12.0 * 0.5);
                }
                else if (mode == 2) // wander: a random saw per partial
                {
                    mphase += (float) (hopS * (0.1 + 8.0 * mRate));
                    if (mphase >= 1.0f)
                        mphase -= 1.0f, b.modValue[(std::size_t) count] = rng.bi();
                    freq *= std::exp2 (pm * b.modValue[(std::size_t) count] * mphase / 12.0);
                }
                else if (mode == 3) // granular: random decaying bursts per partial
                {
                    auto& g = b.grain[(std::size_t) count];
                    g *= (float) std::exp (-hopS / 0.05);
                    if (rng.uni() < (float) (hopS * (1.0 + 60.0 * mRate)))
                        g = 1.0f;
                    grainGain = g;
                }
                // the excitation: the input's magnitude at the partial's place in the (pitch-shifted) input
                const auto at = fn / sh / binHz;
                const auto k = (int) at;
                double eL = 0.0, eR = 0.0;
                if (k >= 0 && k + 1 < frameSize / 2)
                {
                    const auto fr = at - k;
                    eL = std::abs (specL[(std::size_t) k]) * (1.0 - fr) + std::abs (specL[(std::size_t) k + 1]) * fr;
                    eR = std::abs (specR[(std::size_t) k]) * (1.0 - fr) + std::abs (specR[(std::size_t) k + 1]) * fr;
                }
                const auto t60 = partialT60 (dec, fn, hf, lf);
                const auto g = (float) std::exp (-6.907755 * hopS / t60);
                auto& tl = b.targetL[(std::size_t) count];
                auto& tr = b.targetR[(std::size_t) count];
                // a partial rings on at least as loud as its excitation now, then decays [MODEL: a peak-hold resonance]
                tl = std::max (tl * g, (float) eL * norm * coupling) * (mode == 3 ? 1.0f : 1.0f);
                tr = std::max (tr * g, (float) eR * norm * coupling);
                const auto ampTL = tl * grainGain / (float) std::sqrt ((double) U), ampTR = tr * grainGain / (float) std::sqrt ((double) U);
                b.dAmpL[(std::size_t) count] = (ampTL - b.ampL[(std::size_t) count]) / (float) hop;
                b.dAmpR[(std::size_t) count] = (ampTR - b.ampR[(std::size_t) count]) / (float) hop;
                // the rotator for this hop's frequency
                const auto w = 2.0 * spectral::pi * freq / rate;
                b.wr[(std::size_t) count] = (float) std::cos (w);
                b.wi[(std::size_t) count] = (float) std::sin (w);
                if (count >= b.count) // a new partial starts at phase 0
                    b.re[(std::size_t) count] = 1.0f, b.im[(std::size_t) count] = 0.0f;
                else // renormalise the rotator against drift
                {
                    const auto m = std::sqrt (b.re[(std::size_t) count] * b.re[(std::size_t) count] + b.im[(std::size_t) count] * b.im[(std::size_t) count]);
                    if (m > 0.0f)
                        b.re[(std::size_t) count] /= m, b.im[(std::size_t) count] /= m;
                }
                b.hz[(std::size_t) count] = (float) freq;
                ++count;
            }
            for (int i = count; i < b.count; ++i)
                b.ampL[(std::size_t) i] = b.ampR[(std::size_t) i] = 0.0f, b.targetL[(std::size_t) i] = b.targetR[(std::size_t) i] = 0.0f;
            b.count = count;
        }
        for (int u = U; u < maxUnison; ++u)
            bank[(std::size_t) u].count = 0;
    }

    void synth (float& yl, float& yr) noexcept
    {
        for (auto& b : bank)
        {
            const auto n = b.count;
            float sl = 0.0f, sr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const auto re = b.re[(std::size_t) i], im = b.im[(std::size_t) i];
                const auto nr = re * b.wr[(std::size_t) i] - im * b.wi[(std::size_t) i];
                const auto ni = re * b.wi[(std::size_t) i] + im * b.wr[(std::size_t) i];
                b.re[(std::size_t) i] = nr;
                b.im[(std::size_t) i] = ni;
                b.ampL[(std::size_t) i] += b.dAmpL[(std::size_t) i];
                b.ampR[(std::size_t) i] += b.dAmpR[(std::size_t) i];
                sl += ni * b.ampL[(std::size_t) i];
                sr += ni * b.ampR[(std::size_t) i];
            }
            yl += sl;
            yr += sr;
        }
    }

    static constexpr float coupling = 1.0f; // [MEASURED s1 r00: MODEL until fitted]
    double rate = 48000.0;
    spectral::Fft fft;
    std::vector<float> window, inL, inR;
    std::vector<spectral::cpx> work, specL, specR;
    std::vector<std::array<float, 2>> dry;
    std::array<Bank, maxUnison> bank {};
    int fill = 0, sinceHop = 0, dryPos = 0;
    float limGain = 1.0f;
    spectral::Rand rng;
};
} // namespace lynx::harmonia
