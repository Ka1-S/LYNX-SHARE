// LYNX's own data sets for the FL engines it reproduces (core/NativeData.h, round 190rh). Everything here is computed:
// where each set comes from and what was measured to place its few parameters is in docs/analise/REA_rodada190rh.md.

#include "core/NativeData.h"

#include <cmath>

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
} // namespace lynx::nativedata
