// LYNX's own data sets for the FL engines it reproduces (core/NativeData.h, round 190rh). Everything here is computed from
// textbook shapes and public literature; docs/analise/REA_rodada190rh.md cites the sources. No constant comes from FL's
// stored data: the only adjustment is one level per set, from rendered audio.

#include "core/NativeData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lynx::nativedata
{
namespace
{
constexpr double pi = 3.14159265358979323846;
}

std::vector<std::vector<float>> bassPulseCycles (double rate, int count)
{
    // The TB-303's square as Open303 models it (R. Schmidt's rosic::MipMappedWaveTable, fillWithSquare303, MIT licence): the
    // saw into a saturating transistor stage, −tanh (G·saw + O) with G = 36.9 dB and O = 4.37. The engine's own falling
    // saw goes in, so the two line up when they are mixed. The edge the shaper gives (the saw's slow side) and the sharp
    // one (the reset) are its own; a coupling high-pass of 10 Hz makes the flat sides sag, more on the low notes.
    constexpr double lowest = 16.351597831287414; // C0
    const double gain = std::pow (10.0, 36.9 / 20.0);
    constexpr double offset = 4.37, coupling = 10.0;
    constexpr int over = 16; // points per sample, averaged under a raised cosine two samples wide
    std::vector<std::vector<float>> out;
    for (int k = 0; k < count; ++k)
    {
        const auto hz = lowest * std::pow (2.0, (double) k / 12.0);
        const auto n = std::max (8, (int) std::lround (rate / hz));
        const auto shape = [&] (double p) {
            p -= std::floor (p);
            return -std::tanh (gain * (1.0 - 2.0 * p) + offset);
        };
        std::vector<double> x ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0, w = 0.0;
            for (int j = -over + 1; j < over; ++j)
            {
                const auto g = 0.5 + 0.5 * std::cos (pi * (double) j / over);
                v += g * shape (((double) i + (double) j / over) / n);
                w += g;
            }
            x[(std::size_t) i] = v / w;
        }
        // the high-pass in its steady state: run until it repeats
        const auto a = std::exp (-2.0 * pi * coupling / rate);
        const auto turns = 2 + (int) std::ceil (14.0 * rate / (2.0 * pi * coupling) / n);
        std::vector<float> cycle ((std::size_t) n);
        double y = 0.0, last = x.back(), sum = 0.0;
        for (int t = 0; t < turns; ++t)
            for (int i = 0; i < n; ++i)
            {
                y = a * (y + x[(std::size_t) i] - last);
                last = x[(std::size_t) i];
                cycle[(std::size_t) i] = (float) y;
            }
        for (const auto v : cycle)
            sum += v;
        for (auto& v : cycle)
            v = (float) ((v - sum / n) * bassPulseLevel);
        out.push_back (std::move (cycle));
    }
    return out;
}

void fallingSaw (std::vector<float>& amplitude, std::vector<float>& phase)
{
    // ½ − p = Σ sin (2πhp) / (πh): the Fourier series of a falling saw, as cosines (a quarter turn behind)
    amplitude.assign (1024, 0.0f);
    phase.assign (1024, (float) (-0.5 * pi));
    for (int h = 1; h <= 1024; ++h)
        amplitude[(std::size_t) h - 1] = (float) (miniSynthLevel / (pi * h));
}

namespace
{
std::vector<float> makeSawCycle()
{
    // Σ sin (2πhp)·2 / (πh) over the harmonics below a quarter of the table's own, faded by a raised cosine
    constexpr int n = 8192, harmonics = 2048;
    std::vector<double> x ((std::size_t) n, 0.0), sine ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        sine[(std::size_t) i] = std::sin (2.0 * pi * i / n);
    for (int h = 1; h <= harmonics; ++h)
    {
        const auto a = (0.5 + 0.5 * std::cos (pi * h / (harmonics + 1))) * 2.0 / (pi * h);
        for (int i = 0; i < n; ++i)
            x[(std::size_t) i] += a * sine[(std::size_t) (((std::int64_t) h * i) & (n - 1))];
    }
    std::vector<float> out ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        out[(std::size_t) i] = (float) (x[(std::size_t) i] * sawerLevel);
    return out;
}
} // namespace

std::vector<float> sawCycle()
{
    static const auto cycle = makeSawCycle();
    return cycle;
}
} // namespace lynx::nativedata
