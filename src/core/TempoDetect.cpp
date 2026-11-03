#include "core/TempoDetect.h"

#include "core/ClipOps.h"
#include "core/Instruments.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lynx::tempodetect
{
std::vector<float> onsetCurve (const float* left, const float* right, std::size_t frames, double sampleRate, double hopSeconds)
{
    std::vector<float> curve;
    if (left == nullptr || frames == 0 || sampleRate <= 0.0)
        return curve;

    const auto hop = std::max<std::size_t> (16, (std::size_t) std::llround (hopSeconds * sampleRate));
    const auto hops = frames / hop;
    if (hops < 2)
        return curve;

    // three bands from two one-pole low-passes (~150 Hz and ~2 kHz): kick, body, hats
    const auto coefficient = [sampleRate] (double hz) { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * hz / sampleRate)); };
    const auto aLow = coefficient (150.0), aMid = coefficient (2000.0);
    float low = 0.0f, mid = 0.0f;
    std::vector<float> previous (3, 0.0f);
    curve.reserve (hops);

    for (std::size_t h = 0; h < hops; ++h)
    {
        double e[3] = { 0.0, 0.0, 0.0 };
        for (std::size_t i = h * hop, end = i + hop; i < end; ++i)
        {
            const auto x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            low += aLow * (x - low);
            mid += aMid * (x - mid);
            const float bands[3] = { low, mid - low, x - mid };
            for (int b = 0; b < 3; ++b)
                e[b] += (double) bands[b] * bands[b];
        }

        auto flux = 0.0f;
        for (int b = 0; b < 3; ++b)
        {
            const auto level = (float) std::log (1.0e-10 + e[b] / (double) hop);
            if (h > 0)
                flux += std::max (0.0f, level - previous[(std::size_t) b]);
            previous[(std::size_t) b] = level;
        }
        curve.push_back (flux);
    }

    // remove the local mean (~0.4 s) so sustained parts do not count as pulses
    const auto half = std::max<std::size_t> (1, (std::size_t) std::llround (0.2 / hopSeconds));
    std::vector<double> prefix (curve.size() + 1, 0.0);
    for (std::size_t i = 0; i < curve.size(); ++i)
        prefix[i + 1] = prefix[i] + curve[i];
    std::vector<float> out (curve.size());
    for (std::size_t i = 0; i < curve.size(); ++i)
    {
        const auto a = i > half ? i - half : 0, b = std::min (curve.size(), i + half + 1);
        const auto mean = (prefix[b] - prefix[a]) / (double) (b - a);
        out[i] = std::max (0.0f, curve[i] - (float) mean);
    }

    // a short triangular blur (~±15 ms): pulses a fraction of a hop apart still correlate, so the period can be
    // read between whole hops
    const auto radius = std::max<int> (1, (int) std::lround (0.015 / hopSeconds));
    std::vector<float> smooth (out.size(), 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        double sum = 0.0, weights = 0.0;
        for (int k = -radius; k <= radius; ++k)
        {
            const auto j = (long long) i + k;
            if (j < 0 || j >= (long long) out.size())
                continue;
            const auto w = (double) (radius + 1 - std::abs (k));
            sum += w * out[(std::size_t) j];
            weights += w;
        }
        smooth[i] = (float) (sum / weights);
    }
    return smooth;
}

Result detect (const float* left, const float* right, std::size_t frames, double sampleRate, const Options& options)
{
    Result result;
    if (sampleRate <= 0.0 || frames < (std::size_t) (2.0 * sampleRate))
        return result;
    // the hop in whole frames (as onsetCurve rounds it): its exact length in seconds keeps the tempo unbiased
    const auto hopFrames = std::max<long long> (16, std::llround (0.005 * sampleRate));
    const auto hopSeconds = (double) hopFrames / sampleRate;

    // at most two minutes are read (enough for a steady tempo; a long file stays quick)
    const auto analysed = std::min (frames, (std::size_t) (120.0 * sampleRate));
    const auto curve = onsetCurve (left, right, analysed, sampleRate, hopSeconds);
    const auto n = curve.size();
    const auto minBpm = std::clamp (options.minBpm, 20.0, 400.0);
    const auto maxBpm = std::clamp (options.maxBpm, minBpm + 1.0, 800.0);
    constexpr int harmonics = 4;
    const auto maxLag = (std::size_t) std::ceil (harmonics * 60.0 / (minBpm * hopSeconds)) + 2;
    if (n < maxLag + 8)
        return result;

    const auto energy = std::inner_product (curve.begin(), curve.end(), curve.begin(), 0.0);
    if (energy <= 1.0e-9)
        return result;

    // autocorrelation per lag, normalised by the overlap
    std::vector<double> r (maxLag + 1, 0.0);
    for (std::size_t lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = lag; i < n; ++i)
            sum += (double) curve[i] * curve[i - lag];
        r[lag] = sum / (double) (n - lag);
    }
    const auto at = [&r] (double lag) {
        const auto i = (std::size_t) lag;
        if (i + 1 >= r.size())
            return 0.0;
        const auto f = lag - (double) i;
        return r[i] * (1.0 - f) + r[i + 1] * f;
    };

    // a comb over the period and its multiples, with a mild preference for the middle of the range (geometric)
    const auto middle = std::sqrt (minBpm * maxBpm);
    auto score = [&] (double bpm) {
        const auto period = 60.0 / (bpm * hopSeconds);
        double s = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            s += at (period * k) / (double) k;
        const auto octaves = std::log2 (bpm / middle);
        return s * std::exp (-0.5 * octaves * octaves / (1.2 * 1.2));
    };

    auto best = minBpm, bestScore = -1.0, total = 0.0;
    int count = 0;
    for (auto bpm = minBpm; bpm <= maxBpm + 1.0e-9; bpm += 0.05)
    {
        const auto s = score (bpm);
        total += s;
        ++count;
        if (s > bestScore)
        {
            bestScore = s;
            best = bpm;
        }
    }
    // refine the period on many beats (fractional-lag correlation of the curve itself, up to 16 periods):
    // a long lag reads the tempo far more precisely than one period
    const auto direct = [&curve, n] (double lag) {
        const auto whole = (std::size_t) lag;
        const auto f = (float) (lag - (double) whole);
        if (whole + 2 >= n)
            return 0.0;
        double sum = 0.0;
        for (std::size_t i = whole + 1; i < n; ++i)
            sum += (double) curve[i] * (curve[i - whole] * (1.0f - f) + curve[i - whole - 1] * f);
        return sum / (double) (n - whole);
    };
    {
        const auto period = 60.0 / (best * hopSeconds);
        const auto periods = std::clamp ((int) ((double) n * 0.5 / period), 1, 16);
        auto fine = [&] (double bpm) {
            const auto p = 60.0 / (bpm * hopSeconds);
            double s = 0.0;
            for (int k = 1; k <= periods; ++k)
                s += direct (p * k);
            return s;
        };
        auto bestFine = fine (best);
        const auto centre = best;
        for (int step = -40; step <= 40; ++step)
        {
            const auto bpm = centre * (1.0 + 0.0002 * step);
            if (const auto s = fine (bpm); s > bestFine)
            {
                bestFine = s;
                best = bpm;
            }
        }
    }

    const auto mean = total / std::max (1, count);
    if (bestScore <= 0.0)
        return result;
    result.confidence = std::clamp ((bestScore - mean) / bestScore, 0.0, 1.0);
    result.bpm = best;

    const auto seconds = (double) frames / sampleRate;
    if (options.snapToLength)
    {
        // a loop of whole beats: the length gives the exact tempo
        const auto beats = seconds * best / 60.0;
        const auto whole = std::round (beats);
        if (whole >= 4.0)
        {
            const auto exact = whole * 60.0 / seconds;
            if (std::abs (exact - best) / best < 0.002 && exact >= minBpm - 0.5 && exact <= maxBpm + 0.5)
                result.bpm = exact;
        }
    }
    if (options.wholeBpm)
        result.bpm = std::round (result.bpm);
    result.beats = seconds * result.bpm / 60.0;
    return result;
}

float fitBeatsOf (const SampleData* sample, double bpm)
{
    if (sample == nullptr || sample->frames() == 0 || ! (sample->sampleRate > 0.0))
        return 0.0f;
    const auto seconds = (double) sample->frames() / sample->sampleRate;
    double found = 0.0;
    if (sample->acidBeats <= 0.0f && seconds <= 120.0)
    {
        Options options;
        options.minBpm = 75.0; // FL's middle range; fitBeats takes the octave nearest the project tempo
        options.maxBpm = 150.0;
        found = detect (sample->leftData(), sample->isStereo() ? sample->rightData() : nullptr, sample->frames(), sample->sampleRate, options).bpm;
    }
    return clipops::fitBeats (seconds, bpm, sample->acidBeats, found);
}
} // namespace lynx::tempodetect

