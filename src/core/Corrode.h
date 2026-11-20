#pragma once

// Corrode (round 190kk): LYNX's degrader — a very short delay whose time is moved by a sine or by band-passed noise, so
// the input picks up phase noise, buzz and grit that grow with frequency. It is the engine an imported Ableton Live
// "Erosion" (Live 12's Erosion and the legacy one) opens on. Clean-room: every law below comes from LYNX's own
// measurements of Live 12.4's output on test tones (round 190kk, /mnt/e/lynx-work/livedev190kk RESULTS.md §1): the
// device is pure phase modulation (the carrier falls as J0 / exp(−σ²ω²/2) of the same delay deviation at 500 Hz, 2 kHz
// and 8 kHz), adds nothing on silence and is exactly transparent at Amount 0; nothing of Live's code or content is used.
//
//   delay(t) = base + (1 − noise) · sineDepth · sin (2π f t + φ) + noise · noiseDepth · n(t)
//   - sineDepth = 158 µs · Amount (peak) [MEASURED: J0 fits at 2 kHz give 160 / 158 / 163 µs per unit at 0.25 / 0.5 /
//     1, the phase demodulation 153]; Stereo turns the right channel's sine towards the opposite phase (180° at 1
//     [MEASURED]);
//   - n(t): white noise through a band pass at Frequency, Width octaves wide [MEASURED half-power width 0.26 oct at
//     Width 0.3 (1 and 5 kHz), 1.6 oct at 1.5 (60 Hz)], normalised to unit rms, so the depth does not follow the
//     band's place [MEASURED: the same deviation at 1 and 5 kHz]; noiseDepth = 81 µs · Amount · (Width / 0.5)^k rms, k
//     from 0.58 (60 Hz) to 0.31 (12 kHz) [MEASURED 40.5 µs at 0.5 / 0.5, 80 µs at 1 / 0.5 and at 0.5 / 2.5 (5 kHz),
//     75 µs at 0.5 / 1.5 (60 Hz)];
//     Stereo blends the right channel's noise towards an independent one (uncorrelated at 1 [MEASURED]);
//   - base: a fixed 1 ms, reported as latency, so the modulation swings both ways round the dry timing (Live's render
//     is centred the same way [MEASURED: mean deviation 0]).
// Header-only DSP, no allocation in process(); the Effect wrapper lives in core/MixSuite.cpp (core/LiveFxSuite.inc).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace lynx::corrode
{
enum Param { amount, frequency, width, noise, stereo, count };

// ---- the knob laws (all parameters 0..1) ---------------------------------------------------------------------------
inline double frequencyHz (float v) noexcept { return 20.0 * std::pow (900.0, (double) std::clamp (v, 0.0f, 1.0f)); } // 20 Hz … 18 kHz (log)
inline float frequencyValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 1.0) / 20.0) / std::log (900.0), 0.0, 1.0); }
inline double widthOct (float v) noexcept { return 0.1 + 2.4 * (double) std::clamp (v, 0.0f, 1.0f); } // 0.1 … 2.5 octaves
inline float widthValue (double oct) noexcept { return (float) std::clamp ((oct - 0.1) / 2.4, 0.0, 1.0); }

// ---- the measured laws ---------------------------------------------------------------------------------------------
constexpr double sinePeakUs = 158.0;   // at Amount 1
constexpr double noiseRmsUs = 81.0;    // at Amount 1, Width 0.5
constexpr double baseMs = 1.0;
inline double sineDepthUs (double amount) noexcept { return sinePeakUs * std::clamp (amount, 0.0, 1.0); }
// the depth's growth with Width, (Width / 0.5)^k, flattens towards high frequencies [FITTED, least squares on the five measured widths:
// k 0.58 at 60 Hz, 0.48 at 1 kHz, 0.39 … 0.42 at 5 kHz, 0.31 at 12 kHz]
inline double widthExponent (double hz) noexcept { return std::clamp (0.771 - 0.103 * std::log10 (std::max (hz, 20.0)), 0.2, 0.65); }
inline double noiseDepthUs (double amount, double widthOctaves, double hz) noexcept
{
    return noiseRmsUs * std::clamp (amount, 0.0, 1.0) * std::pow (std::max (widthOctaves, 0.05) / 0.5, widthExponent (hz));
}
// The legacy Erosion's Amplitude (0 … 200) as Amount: its sine's peak deviation is 0.81 µs per unit [MEASURED 40.5 µs
// at 50, 162 µs at 200]; its noise's rms deviation at Width 0.3 is 0.34 µs per unit [MEASURED by the residual around a
// 2 kHz tone against Corrode's own at a known depth: 3.4 / 16.9 / 36.6 / 67 µs at 10 / 50 / 100 / 200, mono and wide,
// at 300 Hz … 5 kHz], scaled to Corrode's Width law
inline double legacySineAmount (double amplitude) noexcept { return std::clamp (amplitude * 0.81 / sinePeakUs, 0.0, 1.0); }
inline double legacyNoiseAmount (double amplitude, double hz) noexcept
{
    const auto rms = 0.34 * std::clamp (amplitude, 0.0, 200.0);
    return std::clamp (rms / noiseDepthUs (1.0, 0.3, hz), 0.0, 1.0); // (the depth holds at Width 0.3; both follow the same width law)
}

// The band pass's output variance for unit white noise in, from its state-space form (Lyapunov, closed form): the
// noise is scaled by its inverse so n(t) has unit rms at any Frequency / Width
inline double biquadNoiseGain (double b0, double b1, double b2, double a1, double a2) noexcept
{
    // DF2T: s1' = −a1 s1 + s2 + (b1 − a1 b0) u ; s2' = −a2 s1 + (b2 − a2 b0) u ; y = s1 + b0 u
    const double A[2][2] { { -a1, 1.0 }, { -a2, 0.0 } };
    const double B[2] { b1 - a1 * b0, b2 - a2 * b0 };
    // P = A P Aᵀ + B Bᵀ, P symmetric: unknowns p00, p01, p11
    // with A = [[α, 1], [β, 0]]: p00 = α² p00 + 2α p01 + p11 + B0², p01 = αβ p00 + β p01 + B0 B1, p11 = β² p00 + B1²
    const auto a1v = A[0][0], a2v = A[1][0];
    const double m[3][3] { { 1.0 - a1v * a1v, -2.0 * a1v, -1.0 }, { -a1v * a2v, 1.0 - a2v, 0.0 }, { -a2v * a2v, 0.0, 1.0 } };
    const double r[3] { B[0] * B[0], B[0] * B[1], B[1] * B[1] };
    // Cramer
    const auto det3 = [] (const double q[3][3]) {
        return q[0][0] * (q[1][1] * q[2][2] - q[1][2] * q[2][1]) - q[0][1] * (q[1][0] * q[2][2] - q[1][2] * q[2][0])
               + q[0][2] * (q[1][0] * q[2][1] - q[1][1] * q[2][0]);
    };
    const auto d = det3 (m);
    if (std::abs (d) < 1.0e-300)
        return 1.0;
    double mx[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            mx[i][j] = j == 0 ? r[i] : m[i][j];
    const auto p00 = det3 (mx) / d;
    return std::max (p00 + b0 * b0, 1.0e-12); // y = s1 + b0 u: var = p00 + b0² (s1 does not hold u[n])
}

class Engine
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        base = std::max (4, (int) std::lround (baseMs * 1.0e-3 * rate));
        reset();
    }
    int latency() const noexcept { return base; }
    void reset() noexcept
    {
        for (auto& line : lines)
            line.fill (0.0f);
        write = 0;
        phase = 0.0;
        for (auto& b : bands)
            b = {};
        smoothed = -1.0;
        seed = 0x9e3779b9u;
        lastF = lastW = -1.0;
    }

    void process (float* l, float* r, int n, const float* p) noexcept
    {
        const auto amt = (double) std::clamp (p[amount], 0.0f, 1.0f);
        const auto hz = std::min (frequencyHz (p[frequency]), 0.45 * rate);
        const auto w = widthOct (p[width]);
        const auto blend = (double) std::clamp (p[noise], 0.0f, 1.0f);
        const auto st = (double) std::clamp (p[stereo], 0.0f, 1.0f);
        if (hz != lastF || w != lastW)
            design (hz, w);
        const auto toSamples = rate * 1.0e-6;
        const auto sineD = (1.0 - blend) * sineDepthUs (amt) * toSamples;
        const auto noiseD = blend * noiseDepthUs (amt, w, hz) * toSamples * noiseNorm;
        const auto c = std::cos (1.5707963267948966 * st), s = std::sqrt (std::max (0.0, 1.0 - c * c));
        const auto rightPhase = 3.141592653589793 * st;
        const auto dphi = 6.283185307179586 * hz / rate;
        const auto limit = (double) base - 2.0;
        if (smoothed < 0.0)
            smoothed = amt;
        const auto glide = 1.0 - std::exp (-1.0 / (0.005 * rate)); // (Amount moves within ~5 ms)
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            smoothed += (amt - smoothed) * glide;
            const auto scale = amt > 1.0e-9 ? smoothed / amt : 0.0;
            // the noise: one white source per channel through the band pass; the right one blends to its own
            const auto n0 = band (0, white()), n1 = band (1, white());
            const auto noiseL = n0, noiseR = c * n0 + s * n1;
            const auto sl = std::sin (phase), sr = std::sin (phase + rightPhase);
            phase += dphi;
            if (phase > 6.283185307179586)
                phase -= 6.283185307179586;
            const double dev[2] { scale * (sineD * sl + noiseD * noiseL), scale * (sineD * sr + noiseD * noiseR) };
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& line = lines[(std::size_t) ch];
                line[(std::size_t) write] = io[ch][i];
                const auto delay = (double) base + std::clamp (dev[ch], -limit, limit);
                io[ch][i] = read (line, delay);
            }
            write = (write + 1) & mask;
        }
    }

private:
    static constexpr int size = 1 << 12; // (1 ms base at 192 kHz = 192 samples; room for the swing)
    static constexpr int mask = size - 1;
    struct Band
    {
        double s1 = 0.0, s2 = 0.0;
    };

    void design (double hz, double w) noexcept
    {
        lastF = hz;
        lastW = w;
        // RBJ band pass, 0 dB peak, bandwidth in octaves
        const auto w0 = 6.283185307179586 * hz / rate;
        const auto sn = std::sin (w0), cs = std::cos (w0);
        const auto alpha = sn * std::sinh (0.34657359027997264 * w * w0 / std::max (sn, 1.0e-9)); // ln2/2 · BW · w0 / sin w0
        const auto a0 = 1.0 + alpha;
        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = -2.0 * cs / a0;
        a2 = (1.0 - alpha) / a0;
        noiseNorm = 1.0 / std::sqrt (biquadNoiseGain (b0, 0.0, b2, a1, a2));
    }
    double band (int k, double x) noexcept
    {
        auto& b = bands[(std::size_t) k];
        const auto y = b0 * x + b.s1;
        b.s1 = -a1 * y + b.s2; // b1 = 0
        b.s2 = b2 * x - a2 * y;
        return y;
    }
    // unit-variance noise (sum of four uniforms, scaled: close enough to Gaussian for a delay's jitter)
    double white() noexcept
    {
        double acc = 0.0;
        for (int k = 0; k < 4; ++k)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            acc += (double) seed * (1.0 / 4294967296.0) - 0.5;
        }
        return acc * 1.7320508075688772; // √(12 / 4)
    }
    // 4-point Lagrange read `delay` samples behind the newest sample
    float read (const std::array<float, size>& line, double delay) const noexcept
    {
        const auto pos = (double) write - delay;
        const auto ip = (int) std::floor (pos);
        const auto f = pos - (double) ip;
        const auto at = [&] (int k) { return (double) line[(std::size_t) ((ip + k) & mask)]; };
        const auto xm1 = at (-1), x0 = at (0), x1 = at (1), x2 = at (2);
        const auto c0 = -f * (f - 1.0) * (f - 2.0) / 6.0, c1 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
        const auto c2 = -(f + 1.0) * f * (f - 2.0) / 2.0, c3 = (f + 1.0) * f * (f - 1.0) / 6.0;
        return (float) (c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2);
    }

    double rate = 48000.0;
    int base = 48;
    std::array<std::array<float, size>, 2> lines {};
    int write = 0;
    double phase = 0.0;
    std::array<Band, 2> bands {};
    double b0 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, noiseNorm = 1.0;
    double lastF = -1.0, lastW = -1.0;
    double smoothed = -1.0;
    std::uint32_t seed = 0x9e3779b9u;
};
} // namespace lynx::corrode
