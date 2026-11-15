#include "core/Spectrogram.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace lynx::spectrogram
{
namespace
{
constexpr double pi = 3.14159265358979323846;

void fft (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto angle = -2.0 * pi / (double) len;
        const std::complex<double> step (std::cos (angle), std::sin (angle));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}
} // namespace

Cells compute (const float* left, const float* right, std::int64_t frames, double from, double to, int columns, int fftSize, float rangeDb)
{
    Cells out;
    if (left == nullptr || frames <= 0 || columns <= 0 || to <= from)
        return out;
    auto n = 64;
    while (n < fftSize && n < 8192)
        n <<= 1;
    out.columns = columns;
    out.bins = n / 2;
    out.level.assign ((std::size_t) columns * (std::size_t) out.bins, 0.0f);

    std::vector<double> window ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        window[(std::size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / (double) (n - 1));
    std::vector<std::complex<double>> buffer ((std::size_t) n);
    std::vector<float> db (out.level.size(), -400.0f);
    float loudest = -400.0f;
    for (int c = 0; c < columns; ++c)
    {
        const auto centre = from + (to - from) * ((double) c + 0.5) / (double) columns;
        const auto first = (std::int64_t) std::llround (centre) - n / 2;
        for (int i = 0; i < n; ++i)
        {
            const auto at = first + i;
            double x = 0.0;
            if (at >= 0 && at < frames)
                x = right != nullptr ? 0.5 * ((double) left[at] + (double) right[at]) : (double) left[at];
            buffer[(std::size_t) i] = { x * window[(std::size_t) i], 0.0 };
        }
        fft (buffer);
        for (int b = 0; b < out.bins; ++b)
        {
            const auto magnitude = std::abs (buffer[(std::size_t) b]) * 4.0 / (double) n; // (Hann's coherent gain ½, one side)
            const auto v = magnitude > 1.0e-20 ? (float) (20.0 * std::log10 (magnitude)) : -400.0f;
            db[(std::size_t) c * (std::size_t) out.bins + (std::size_t) b] = v;
            loudest = std::max (loudest, v);
        }
    }
    if (loudest <= -400.0f)
        return out; // (silence: every cell at the floor)
    for (std::size_t i = 0; i < db.size(); ++i)
        out.level[i] = std::clamp ((db[i] - (loudest - rangeDb)) / rangeDb, 0.0f, 1.0f);
    return out;
}

std::uint32_t colourOf (float level) noexcept
{
    // FL's heat scale (live check 190gu shot): black, a dark blue-purple floor, crimson, orange, yellow
    struct Stop
    {
        float at;
        int r, g, b;
    };
    static constexpr Stop stops[] = { { 0.00f, 0, 0, 0 },      { 0.30f, 34, 30, 62 },  { 0.50f, 96, 32, 84 },
                                       { 0.68f, 178, 34, 66 }, { 0.84f, 236, 96, 38 }, { 1.00f, 252, 236, 74 } };
    level = std::clamp (level, 0.0f, 1.0f);
    for (std::size_t i = 1; i < std::size (stops); ++i)
        if (level <= stops[i].at)
        {
            const auto& a = stops[i - 1];
            const auto& b = stops[i];
            const auto t = (level - a.at) / (b.at - a.at);
            const auto mix = [t] (int x, int y) { return (std::uint32_t) std::lround ((float) x + ((float) y - (float) x) * t); };
            return 0xFF000000u | (mix (a.r, b.r) << 16) | (mix (a.g, b.g) << 8) | mix (a.b, b.b);
        }
    return 0xFFFCEC4Au;
}
} // namespace lynx::spectrogram
