// LYNX's own data sets for the FL engines it reproduces (core/NativeData.h, round 190rh). Everything here is computed:
// where each set comes from and what was measured to place its few parameters is in docs/analise/REA_rodada190rh.md.

#include "core/NativeData.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace lynx::nativedata
{
std::vector<std::vector<float>> bassPulseCycles (double rate, int count)
{
    constexpr double lowest = 16.351597831287414; // C0
    constexpr int over = 16;                      // points per sample, averaged under a raised cosine two samples wide
    std::vector<std::vector<float>> out;
    for (int k = 0; k < count; ++k)
    {
        const auto hz = lowest * std::pow (2.0, (double) k / 12.0);
        const auto n = std::max (8, (int) std::lround (rate / hz));
        const auto period = 1.0 / hz;
        // the shaper's threshold moves with the pitch: about two thirds high at the bottom, narrowing 6.5 % per octave
        const auto duty = std::min (0.7, std::max (0.3, 0.68 - 0.065 * std::log2 (hz / lowest)));
        constexpr double sag = 0.010, edge = 25.0e-6; // the coupling's time constant, the transistor's switching time (s)
        const auto on = duty * period;
        const auto shape = [&] (double t) { // t: seconds into the cycle, which starts where the output falls (as the saw's)
            t += on;
            t -= std::floor (t / period) * period;
            const auto high = 1.05 + 1.05 * std::exp (-t / sag);
            if (t < edge)
                return high * (0.5 - 0.5 * std::cos (3.141592653589793 * t / edge));
            if (t < on)
                return high;
            if (t < on + edge)
                return high * (0.5 + 0.5 * std::cos (3.141592653589793 * (t - on) / edge));
            return 0.0;
        };
        std::vector<double> x ((std::size_t) n);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0, w = 0.0;
            for (int j = -over + 1; j < over; ++j)
            {
                const auto g = 0.5 + 0.5 * std::cos (3.141592653589793 * (double) j / over);
                v += g * shape (((double) i + (double) j / over) * period / n);
                w += g;
            }
            x[(std::size_t) i] = v / w;
            sum += x[(std::size_t) i];
        }
        std::vector<float> cycle ((std::size_t) n);
        for (int i = 0; i < n; ++i)
            cycle[(std::size_t) i] = (float) (x[(std::size_t) i] - sum / n);
        out.push_back (std::move (cycle));
    }
    return out;
}

void softResetSaw (std::vector<float>& amplitude, std::vector<float>& phase)
{
    // x(p) = ½ − p − ½·e^(−p/τ), p = 0…1: its Fourier series in closed form. Ramp: Σ sin(2πhp)/(πh); the decay
    // ½·e^(−p/τ): ½·(1 − e^(−1/τ))/(1/τ + i2πh)·2 as the complex amplitude of harmonic h
    constexpr double pi = 3.141592653589793, tau = 0.05;
    amplitude.assign (1024, 0.0f);
    phase.assign (1024, 0.0f);
    for (int h = 1; h <= 1024; ++h)
    {
        const std::complex<double> ramp (0.0, -1.0 / (pi * h)); // (sin = Re (−i·e^{ix}))
        const auto decay = (1.0 - std::exp (-1.0 / tau)) / std::complex<double> (1.0 / tau, 2.0 * pi * h);
        const auto c = ramp - decay;
        amplitude[(std::size_t) h - 1] = (float) std::abs (c);
        phase[(std::size_t) h - 1] = (float) std::arg (c);
    }
}

namespace
{
std::vector<float> makeCoupledSaw()
{
    // harmonic h of a saw: 1/h, quarter-turn phase; the coupling's first-order high-pass (corner at the fundamental) and
    // a constant turn of 0.3 rad. Scaled to an RMS of 0.28 (the level the engine's drive expects)
    constexpr double pi = 3.141592653589793;
    constexpr int n = 8192, harmonics = 2048;
    std::vector<double> x ((std::size_t) n, 0.0);
    std::vector<double> sine ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        sine[(std::size_t) i] = std::sin (2.0 * pi * i / n);
    for (int h = 1; h <= harmonics; ++h)
    {
        const auto fade = 0.5 + 0.5 * std::cos (pi * h / (harmonics + 1));
        const auto a = fade / h * (h / std::sqrt ((double) h * h + 1.0));
        const auto ph = -0.5 * pi - 0.3 - std::atan (1.0 / h);
        const auto c = a * std::cos (ph), s = a * std::sin (ph); // a·cos (hx + ph)
        for (int i = 0; i < n; ++i)
        {
            const auto k = (std::size_t) ((std::int64_t) h * i & (n - 1));
            x[(std::size_t) i] += c * sine[(k + n / 4) & (n - 1)] - s * sine[k];
        }
    }
    double e = 0.0;
    for (const auto v : x)
        e += v * v;
    const auto g = 0.28 / std::sqrt (e / n);
    std::vector<float> out ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        out[(std::size_t) i] = (float) (x[(std::size_t) i] * g);
    return out;
}
} // namespace

std::vector<float> coupledSawCycle()
{
    static const auto cycle = makeCoupledSaw();
    return cycle;
}
} // namespace lynx::nativedata
