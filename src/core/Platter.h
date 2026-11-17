#pragma once

// Platter (round 190kk): LYNX's record-wear effect — the stylus' tracing error and pinch distortion on a band of the
// input, and surface crackle. It is the engine an imported Ableton Live "Vinyl Distortion" opens on. Clean-room: the laws
// come from LYNX's own analysis of Live 12.4's renders (round 190kk, /mnt/e/lynx-work/livedev190kk RESULTS.md §2: 1 kHz
// staircases, tones, sweeps, noise and silence through 60 settings); nothing of Live's code or content is used.
//
// Measured facts the engine keeps:
//   - nothing happens while Drive is 0, whatever the bands' own drives (the device passes its input bit for bit);
//   - both bands are driven by G = 2 · (the band's drive) · Drive [MEASURED: band drive 0.25 / 1 and Drive 0.25 / 0.5
//     give the same results as G says];
//   - Tracing adds a rectified copy of its band: soft = 1.95 · |G · band|, hard = 8.0 · (G · band)² (the harmonic ratio of
//     soft does not follow the level, hard's rises 6 dB per 6 dB); the left channel gets +, the right − (so a mono sum
//     cancels it) unless Mono is on (both + [MEASURED]);
//   - Pinch (its band a band pass with Q = the width [FITTED: closer to Live's on the staircases than Tracing's band])
//     adds its band's distortion with the same sign on both channels, a function of G · band alone (Hard and the
//     Stereo / Mono switch do not change it), compressing and then folding the band at high levels;
//   - nothing removes the DC the rectification makes;
//   - crackle: sparse clicks ringing around 1 kHz (octave levels peak at 1 kHz, 6–10 dB lower an octave either side),
//     rms −44.5 dB (left) at density 10 / volume 1, ∝ volume and √density; the right channel 3 dB louder, the two
//     correlated about 0.4; the Mono switch does not touch it.
// Header-only DSP, no allocation in process(); the Effect wrapper lives in core/MixSuite.cpp (core/LiveFxSuite.inc).

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

namespace lynx::platter
{
// (the first seven are what the Device View's card shows: Drive, both bands' drive and frequency, the crackle)
enum Param
{
    drive, tracingDrive, tracingFreq, pinchDrive, pinchFreq, crackleVolume, crackleDensity, tracingOn, tracingWidth, pinchOn,
    pinchWidth, hard, mono, count
};

// ---- the knob laws (all parameters 0..1) ---------------------------------------------------------------------------
inline double bandHz (float v) noexcept { return 50.0 * std::pow (360.0, (double) std::clamp (v, 0.0f, 1.0f)); } // 50 Hz … 18 kHz (log)
inline float bandHzValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 1.0) / 50.0) / std::log (360.0), 0.0, 1.0); }
inline double bandQ (float v) noexcept { return 0.1 + 2.9 * (double) std::clamp (v, 0.0f, 1.0f); } // the band's width 0.1 … 3
inline float bandQValue (double q) noexcept { return (float) std::clamp ((q - 0.1) / 2.9, 0.0, 1.0); }
inline double density (float v) noexcept { return 50.0 * (double) std::clamp (v, 0.0f, 1.0f); } // 0 … 50
inline bool on (float v) noexcept { return v >= 0.5f; }

// ---- the measured / fitted laws ------------------------------------------------------------------------------------
struct Tuning
{
    // Tracing: the rectified copy's gain at G 1 [MEASURED 1.949 soft (|x| term, 1.4 % residual), 7.995 hard (x² term)]
    double tracingSoft = 1.95, tracingHard = 8.0;
    // Pinch: y = x + (φ (G · band) − G · band) / G with φ (u) = u + c2 u² + c3 u³ + c4 u⁴ + c5 u⁵ (u held to ±2.5)
    // [FITTED on the 1 kHz staircases at G 0.5 and 2: level, H2, H3 within ~1 dB up to the knee; the fold above it
    // (−13 dB at G 2, −3 dBFS in Live) is approximated]
    double c2 = -1.0976, c3 = -0.8676, c4 = 0.3978, c5 = 0.1527;
    bool pinchHighPass = true; // (the distortion term through a 2nd-order high pass at a quarter of the band's frequency)
    // crackle: clicks per second per unit of density, the click's peak at volume 1, the tail's share and centre
    double clicksPerDensity = 10.0, clickPeak = 1.24, tailMix = 0.9, tailHz = 1150.0, tailQ = 2.6;
};

class Engine
{
public:
    Tuning tuning;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }
    void reset() noexcept
    {
        for (auto& s : bandState)
            s = {};
        for (auto& s : pinchState)
            s = {};
        for (auto& s : pinchHpState)
            s = {};
        for (auto& s : tailState)
            s = {};
        seed = 0x2545f491u;
        last.fill (-1.0f);
    }

    void process (float* l, float* r, int n, const float* p) noexcept
    {
        if (! same (p))
            design (p);
        const auto d = (double) std::clamp (p[drive], 0.0f, 1.0f);
        const auto crackling = crackleGain > 0.0 && clickProb > 0.0;
        if (d <= 0.0 && ! crackling)
            return; // (Drive 0: the input passes untouched [MEASURED bit-exact])
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto x = (double) io[ch][i];
                double y = x;
                if (d > 0.0)
                {
                    if (useTracing)
                    {
                        const auto u = tracingGain * run (tracingC, bandState[(std::size_t) ch], x);
                        const auto t = hardMode ? tuning.tracingHard * u * u : tuning.tracingSoft * std::abs (u);
                        y += ch == 0 || monoTracing ? t : -t; // (Mono: the same sign on both [MEASURED])
                    }
                    if (usePinch)
                    {
                        const auto b = biquad (pinchState[(std::size_t) ch], pinchC, x);
                        if (pinchGain > 0.0)
                        {
                            const auto u = std::clamp (pinchGain * b, -2.5, 2.5), u2 = u * u;
                            const auto dist = u2 * (tuning.c2 + u * (tuning.c3 + u * (tuning.c4 + u * tuning.c5))) / pinchGain;
                            y += tuning.pinchHighPass ? biquad (pinchHpState[(std::size_t) ch], pinchHp, dist) : dist;
                        }
                    }
                }
                if (crackling)
                    y += crackle (ch);
                io[ch][i] = (float) y;
            }
    }

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };
    struct State
    {
        double s1 = 0.0, s2 = 0.0;
    };
    struct Band
    {
        Biquad lp;
        bool useLp = false;
        double hp0 = 1.0, hpA = 0.0, norm = 1.0;
    };
    struct BandState
    {
        State lp;
        double x1 = 0.0, y1 = 0.0;
    };
    static double biquad (State& s, const Biquad& c, double x) noexcept
    {
        const auto y = c.b0 * x + s.s1;
        s.s1 = c.b1 * x - c.a1 * y + s.s2;
        s.s2 = c.b2 * x - c.a2 * y;
        return y;
    }
    // Tracing's band [FITTED on the tones and sweeps through Tracing at widths 0.3 and 3 (14 points, 2.4 dB rms)]: a
    // resonant low pass (Q 1.5) at f · 2^(0.35 w³) after a one-pole high pass at f · 2^(−0.2 w³), scaled to 0 dB at f —
    // a narrow, peaky band at small widths, flat over the audio band at 3
    Band band (double hz, double width) const noexcept
    {
        Band b;
        const auto w3 = width * width * width;
        const auto fh = hz * std::pow (2.0, 0.35 * w3), fl = hz * std::pow (2.0, -0.2 * w3);
        b.useLp = fh < 0.45 * rate;
        if (b.useLp)
        {
            const auto w0 = 6.283185307179586 * fh / rate, c = std::cos (w0), al = std::sin (w0) / (2.0 * 1.5), a0 = 1.0 + al;
            b.lp = { (1.0 - c) / 2.0 / a0, (1.0 - c) / a0, (1.0 - c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
        }
        const auto k = std::tan (3.141592653589793 * std::min (fl, 0.45 * rate) / rate);
        b.hp0 = 1.0 / (1.0 + k);
        b.hpA = (k - 1.0) / (k + 1.0);
        // the gain at f, from the digital responses
        const auto z = std::polar (1.0, -6.283185307179586 * std::min (hz, 0.45 * rate) / rate);
        std::complex<double> h = b.hp0 * (1.0 - z) / (1.0 + b.hpA * z);
        if (b.useLp)
            h *= (b.lp.b0 + b.lp.b1 * z + b.lp.b2 * z * z) / (1.0 + b.lp.a1 * z + b.lp.a2 * z * z);
        b.norm = std::abs (h) > 1.0e-9 ? 1.0 / std::abs (h) : 1.0;
        return b;
    }
    Biquad resonator (double hz, double q) const noexcept
    {
        const auto w0 = 6.283185307179586 * std::min (hz, 0.45 * rate) / rate, al = std::sin (w0) / (2.0 * std::max (q, 0.05)), a0 = 1.0 + al;
        return { al / a0, 0.0, -al / a0, -2.0 * std::cos (w0) / a0, (1.0 - al) / a0 };
    }
    static double run (const Band& b, BandState& s, double x) noexcept
    {
        const auto hp = b.hp0 * (x - s.x1) - b.hpA * s.y1;
        s.x1 = x;
        s.y1 = hp;
        return (b.useLp ? biquad (s.lp, b.lp, hp) : hp) * b.norm;
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
        for (int k = 0; k < count; ++k)
            last[(std::size_t) k] = p[k];
        const auto d = (double) std::clamp (p[drive], 0.0f, 1.0f);
        hardMode = on (p[hard]);
        monoTracing = on (p[mono]);
        useTracing = on (p[tracingOn]);
        usePinch = on (p[pinchOn]);
        // G = 2 · band drive · Drive [MEASURED]
        tracingGain = 2.0 * std::clamp ((double) p[tracingDrive], 0.0, 1.0) * d;
        pinchGain = 2.0 * std::clamp ((double) p[pinchDrive], 0.0, 1.0) * d;
        tracingC = band (bandHz (p[tracingFreq]), bandQ (p[tracingWidth]));
        pinchC = resonator (bandHz (p[pinchFreq]), bandQ (p[pinchWidth]));
        {
            const auto w0 = 6.283185307179586 * std::min (bandHz (p[pinchFreq]) * 0.25, 0.45 * rate) / rate, c = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071), a0 = 1.0 + al;
            pinchHp = { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
        }
        clickProb = tuning.clicksPerDensity * density (p[crackleDensity]) / rate;
        crackleGain = tuning.clickPeak * std::clamp ((double) p[crackleVolume], 0.0, 1.0);
        tailC = resonator (tuning.tailHz, tuning.tailQ);
    }
    double uniform() noexcept
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return (double) seed * (1.0 / 4294967296.0);
    }
    // one click stream: a click lands on both channels (30 %) or on one of them; the right channel is 3 dB louder
    double crackle (int ch) noexcept
    {
        if (ch == 0)
        {
            pending[0] = pending[1] = 0.0;
            if (uniform() < clickProb)
            {
                const auto a = crackleGain * (0.3 + 0.7 * uniform()) * (uniform() < 0.5 ? -1.0 : 1.0);
                const auto where = uniform();
                if (where < 0.65)
                    pending[0] = a;
                if (where < 0.3 || where >= 0.65)
                    pending[1] = a * 1.41;
            }
        }
        const auto click = pending[(std::size_t) ch];
        const auto tail = biquad (tailState[(std::size_t) ch], tailC, click);
        return tuning.tailMix * tail + (1.0 - tuning.tailMix) * click;
    }

    double rate = 48000.0;
    std::array<BandState, 2> bandState {};
    std::array<State, 2> pinchState {}, pinchHpState {};
    std::array<State, 2> tailState {};
    std::array<double, 2> pending {};
    std::array<float, count> last {};
    Band tracingC;
    Biquad pinchC, pinchHp, tailC;
    bool hardMode = false, useTracing = false, usePinch = false, monoTracing = false;
    double tracingGain = 0.0, pinchGain = 0.0, clickProb = 0.0, crackleGain = 0.0;
    std::uint32_t seed = 0x2545f491u;
};
} // namespace lynx::platter
