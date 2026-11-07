#include "core/DialogueAlign.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>

namespace lynx::align
{
namespace
{
constexpr int bandCount = 24;
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
        const auto ang = -2.0 * pi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace

std::vector<std::vector<float>> features (const std::vector<float>& x, double sr)
{
    const auto hop = std::max<std::size_t> (1, (std::size_t) std::lround (hopSeconds * sr));
    const auto win = std::max<std::size_t> (16, (std::size_t) std::lround (0.025 * sr));
    std::size_t nfft = 1;
    while (nfft < win)
        nfft <<= 1;
    const auto frames = x.size() < win ? (std::size_t) 1 : (x.size() - win) / hop + 1;
    // band edges (log spaced, 80 Hz … 8 kHz, below Nyquist)
    const auto top = std::min (8000.0, sr * 0.45);
    std::vector<std::size_t> edges (bandCount + 1);
    for (int b = 0; b <= bandCount; ++b)
    {
        const auto f = 80.0 * std::pow (top / 80.0, (double) b / bandCount);
        edges[(std::size_t) b] = std::clamp<std::size_t> ((std::size_t) std::lround (f * (double) nfft / sr), 1, nfft / 2);
    }
    std::vector<double> hann (win);
    for (std::size_t i = 0; i < win; ++i)
        hann[i] = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (win - 1));
    std::vector<std::vector<float>> out (frames, std::vector<float> (bandCount));
    std::vector<std::complex<double>> buf (nfft);
    for (std::size_t f = 0; f < frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), std::complex<double> (0.0, 0.0));
        for (std::size_t i = 0; i < win; ++i)
        {
            const auto k = f * hop + i;
            buf[i] = k < x.size() ? x[k] * hann[i] : 0.0;
        }
        fft (buf);
        for (int b = 0; b < bandCount; ++b)
        {
            double e = 0.0;
            const auto lo = edges[(std::size_t) b], hi = std::max (lo + 1, edges[(std::size_t) b + 1]);
            for (std::size_t k = lo; k < hi && k <= nfft / 2; ++k)
                e += std::norm (buf[k]);
            out[f][(std::size_t) b] = (float) std::log10 (e / (double) (hi - lo) + 1e-10);
        }
    }
    // each band normalised over the utterance (level and microphone colour differences drop out)
    for (int b = 0; b < bandCount; ++b)
    {
        double mean = 0.0, var = 0.0;
        for (const auto& fr : out)
            mean += fr[(std::size_t) b];
        mean /= (double) frames;
        for (const auto& fr : out)
            var += (fr[(std::size_t) b] - mean) * (fr[(std::size_t) b] - mean);
        const auto sd = std::sqrt (var / (double) frames) + 1e-3;
        for (auto& fr : out)
            fr[(std::size_t) b] = (float) ((fr[(std::size_t) b] - mean) / sd);
    }
    return out;
}

double Map::takeAt (double g) const noexcept
{
    if (points.empty())
        return g;
    if (points.size() == 1 || g <= points.front().first)
        return points.front().second + (g - points.front().first);
    if (g >= points.back().first)
        return points.back().second + (g - points.back().first);
    const auto it = std::upper_bound (points.begin(), points.end(), g, [] (double v, const std::pair<double, double>& p) { return v < p.first; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const auto span = b.first - a.first;
    return span > 0.0 ? a.second + (b.second - a.second) * (g - a.first) / span : a.second;
}

Map align (const std::vector<float>& guide, const std::vector<float>& take, double sr, const Options& o)
{
    Map map;
    auto G = features (guide, sr), T = features (take, sr);
    const auto n = G.size(), m = T.size();
    // pauses all look the same (their noise must not pull the path): a frame quieter than the utterance's average
    // band level by more than half a deviation becomes one "silence" vector
    std::vector<double> guideLevel (n);
    for (auto* f : { &G, &T })
        for (auto& fr : *f)
        {
            double level = 0.0;
            for (const auto v : fr)
                level += v;
            level /= bandCount;
            if (f == &G)
                guideLevel[(std::size_t) (&fr - G.data())] = level;
            if (level < -0.5)
                std::fill (fr.begin(), fr.end(), -1.5f);
        }
    if (n < 2 || m < 2)
        return map;
    const auto band = std::max<std::size_t> (8, (std::size_t) std::lround (o.maxShift / hopSeconds) + (std::size_t) std::llabs ((long long) m - (long long) n));
    // DTW inside |j − i·m/n| ≤ band, the symmetric step pattern: (1,1) costs 2 d, (1,0) and (0,1) cost d
    constexpr float inf = std::numeric_limits<float>::infinity();
    const auto width = 2 * band + 1;
    std::vector<float> cost (n * width, inf);
    std::vector<std::uint8_t> from (n * width, 0); // 0 diag, 1 from (i−1, j), 2 from (i, j−1)
    auto centre = [&] (std::size_t i) { return (long long) std::llround ((double) i * (double) (m - 1) / (double) (n - 1)); };
    auto idx = [&] (std::size_t i, long long j) -> long long {
        const auto k = j - centre (i) + (long long) band;
        return k >= 0 && k < (long long) width ? (long long) (i * width) + k : -1;
    };
    auto dist = [&] (std::size_t i, std::size_t j) {
        float d = 0.0f;
        for (int b = 0; b < bandCount; ++b)
        {
            const auto e = G[i][(std::size_t) b] - T[j][(std::size_t) b];
            d += e * e;
        }
        return std::sqrt (d);
    };
    const float penalty = 0.0f; // (the symmetric form: a diagonal step costs 2 d, the others d)
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto c = centre (i);
        for (auto j = std::max (0LL, c - (long long) band); j <= std::min ((long long) m - 1, c + (long long) band); ++j)
        {
            const auto here = idx (i, j);
            const auto d = dist (i, (std::size_t) j);
            if (i == 0 && j == 0)
            {
                cost[(std::size_t) here] = d;
                continue;
            }
            float best = inf;
            std::uint8_t step = 0;
            if (i > 0 && j > 0)
                if (const auto k = idx (i - 1, j - 1); k >= 0 && cost[(std::size_t) k] + 2.0f * d < best)
                {
                    best = cost[(std::size_t) k] + 2.0f * d;
                    step = 0;
                }
            if (i > 0)
                if (const auto k = idx (i - 1, j); k >= 0 && cost[(std::size_t) k] + d + penalty < best)
                {
                    best = cost[(std::size_t) k] + d + penalty;
                    step = 1;
                }
            if (j > 0)
                if (const auto k = idx (i, j - 1); k >= 0 && cost[(std::size_t) k] + d + penalty < best)
                {
                    best = cost[(std::size_t) k] + d + penalty;
                    step = 2;
                }
            cost[(std::size_t) here] = best;
            from[(std::size_t) here] = step;
        }
    }
    // the path back from (n−1, m−1)
    std::vector<double> takeOf (n, 0.0); // the take frame per guide frame (the mean of the cells the path visits)
    std::vector<int> visits (n, 0);
    std::size_t i = n - 1;
    long long j = (long long) m - 1;
    if (idx (i, j) < 0 || ! std::isfinite (cost[(std::size_t) idx (i, j)]))
        return map;
    for (;;)
    {
        takeOf[i] += (double) j;
        ++visits[i];
        if (i == 0 && j == 0)
            break;
        const auto step = from[(std::size_t) idx (i, j)];
        if (step == 0)
        {
            --i;
            --j;
        }
        else if (step == 1)
            --i;
        else
            --j;
        if (j < 0 || idx (i, j) < 0)
            break;
    }
    for (std::size_t k = 0; k < n; ++k)
        takeOf[k] = visits[k] > 0 ? takeOf[k] / visits[k] : (k > 0 ? takeOf[k - 1] : 0.0);
    // smoothing, then a monotonic path with a limited slope
    const auto r = std::max<std::size_t> (1, (std::size_t) std::lround (o.smoothing / hopSeconds));
    // (weighted by loudness: in pauses the path is arbitrary — noise matches noise — and must not pull the edges of
    // the words; the offset there is the one of the words around)
    std::vector<double> weight (n);
    for (std::size_t k = 0; k < n; ++k)
    {
        const auto level = std::max (0.0, guideLevel[k] + 1.0);
        weight[k] = 0.02 + level * level;
    }
    std::vector<double> smooth (n);
    for (std::size_t k = 0; k < n; ++k)
    {
        double s = 0.0, w = 0.0;
        for (auto q = k > r ? k - r : 0; q <= std::min (n - 1, k + r); ++q)
        {
            s += (takeOf[q] - (double) q) * weight[q];
            w += weight[q];
        }
        smooth[k] = (double) k + s / w; // (the offsets are averaged, not the positions)
    }
    const auto lo = 1.0 / o.maxRate, hi = o.maxRate;
    for (std::size_t k = 1; k < n; ++k)
        smooth[k] = std::clamp (smooth[k], smooth[k - 1] + lo, smooth[k - 1] + hi);
    map.points.reserve (n);
    for (std::size_t k = 0; k < n; ++k)
    {
        const auto g = (double) k * hopSeconds;
        const auto t = smooth[k] * hopSeconds;
        map.points.emplace_back (g, g + (t - g) * std::clamp (o.strength, 0.0, 1.0));
    }
    return map;
}

std::vector<float> warp (const std::vector<float>& take, double sr, const Map& map, std::size_t outLength)
{
    std::vector<float> out (outLength, 0.0f), norm (outLength, 0.0f);
    if (take.empty() || outLength == 0)
        return out;
    const auto grain = std::max<std::size_t> (32, (std::size_t) std::lround (0.030 * sr));
    const auto hop = grain / 2;
    const auto tolerance = (long long) std::lround (0.008 * sr); // where a grain may slide to fit
    std::vector<float> window (grain);
    for (std::size_t i = 0; i < grain; ++i)
        window[i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) grain));
    auto sample = [&] (long long k) { return k >= 0 && k < (long long) take.size() ? take[(std::size_t) k] : 0.0f; };
    long long previous = -1; // where the previous grain was read from
    for (std::size_t o = 0; o < outLength; o += hop)
    {
        const auto centreSeconds = ((double) o + (double) grain * 0.5) / sr;
        auto target = (long long) std::llround (map.takeAt (centreSeconds) * sr - (double) grain * 0.5);
        if (previous >= 0)
        {
            // WSOLA: the grain that best continues the previous one's natural next half
            const auto natural = previous + (long long) hop;
            double best = -1e30;
            long long bestK = target;
            for (auto d = -tolerance; d <= tolerance; d += 2)
            {
                const auto k = target + d;
                double c = 0.0;
                for (std::size_t q = 0; q < hop; q += 2)
                    c += (double) sample (k + (long long) q) * (double) sample (natural + (long long) q);
                if (c > best)
                {
                    best = c;
                    bestK = k;
                }
            }
            target = bestK;
        }
        for (std::size_t q = 0; q < grain && o + q < outLength; ++q)
        {
            out[o + q] += sample (target + (long long) q) * window[q];
            norm[o + q] += window[q];
        }
        previous = target;
    }
    for (std::size_t k = 0; k < outLength; ++k)
        if (norm[k] > 1e-3f)
            out[k] /= norm[k];
    return out;
}
} // namespace lynx::align
