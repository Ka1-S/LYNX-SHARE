#pragma once

// Loudness and true-peak metering after ITU-R BS.1770-4 / EBU R 128 (round 135, Apex Limiter's loudness meter).
//
// - K-weighting: the two-stage pre-filter of BS.1770 (high shelf ≈ +4 dB above 1.5 kHz, then a 38 Hz high pass),
//   coefficients derived for any sample rate (at 48 kHz they are the standard's table values).
// - Momentary loudness = mean square over the last 400 ms, short-term = the last 3 s, refreshed every 100 ms;
//   L = −0.691 + 10 log10 (Σ channel mean squares) (stereo: both channels weight 1).
// - Integrated loudness: 400 ms blocks with 75 % overlap, gated at −70 LUFS (absolute) and 10 LU below the mean of
//   the blocks that pass (relative). Blocks are kept in a 0.1 LU histogram (no allocation, unlimited length), so the
//   relative gate is exact to the bin.
// - Loudness range (EBU Tech 3342): the short-term values, gated at −70 LUFS and 20 LU below their mean; LRA = the
//   95th minus the 10th percentile.
// - True peak: 4× oversampling (48-tap windowed-sinc polyphase interpolator, BS.1770 annex 2), the highest
//   absolute value (dBTP), and the plain sample peak beside it.
//
// No JUCE: tested in tests/Parity135fTests.inc. Not real-time hostile: nothing allocates after construction.

#include <algorithm>
#include <array>
#include <cmath>

namespace lynx::loudness
{
constexpr double silence = -120.0; // reported while nothing was measured

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    double run (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void clear() noexcept { z1 = z2 = 0; }
};

// the two K-weighting stages at `sr`
inline void kWeighting (double sr, Biquad& shelf, Biquad& highPass) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    {
        const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan (pi * f0 / sr);
        const double vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan (pi * f0 / sr);
        const double a0 = 1.0 + k / q + k * k;
        highPass.b0 = 1.0; highPass.b1 = -2.0; highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (k * k - 1.0) / a0;
        highPass.a2 = (1.0 - k / q + k * k) / a0;
    }
}

inline double lufsOf (double meanSquare) noexcept { return meanSquare > 1e-14 ? -0.691 + 10.0 * std::log10 (meanSquare) : silence; }

// gated statistics in 0.1 LU bins from −70 to +10 LUFS
class Histogram
{
public:
    static constexpr double lo = -70.0, step = 0.1;
    static constexpr int bins = 800;
    void clear() noexcept { count.fill (0); energy.fill (0.0); total = 0; }
    void add (double meanSquare) noexcept
    {
        const double l = lufsOf (meanSquare);
        if (l < lo) return; // absolute gate
        const int b = std::clamp ((int) ((l - lo) / step), 0, bins - 1);
        ++count[(std::size_t) b];
        energy[(std::size_t) b] += meanSquare;
        ++total;
    }
    long long size() const noexcept { return total; }
    // integrated: the mean of the blocks at or above (mean of all − gate) LU
    double gatedMean (double relativeGateLu) const noexcept
    {
        if (total == 0) return silence;
        double e = 0.0;
        long long c = 0;
        for (int b = 0; b < bins; ++b) { e += energy[(std::size_t) b]; c += count[(std::size_t) b]; }
        const double gate = lufsOf (e / (double) c) - relativeGateLu;
        e = 0.0; c = 0;
        for (int b = firstBin (gate); b < bins; ++b) { e += energy[(std::size_t) b]; c += count[(std::size_t) b]; }
        return c > 0 ? lufsOf (e / (double) c) : silence;
    }
    // LRA: the spread between two percentiles of the blocks above the relative gate
    double range (double relativeGateLu, double lowPct, double highPct) const noexcept
    {
        if (total == 0) return 0.0;
        double e = 0.0;
        long long c = 0;
        for (int b = 0; b < bins; ++b) { e += energy[(std::size_t) b]; c += count[(std::size_t) b]; }
        const int first = firstBin (lufsOf (e / (double) c) - relativeGateLu);
        long long n = 0;
        for (int b = first; b < bins; ++b) n += count[(std::size_t) b];
        if (n < 2) return 0.0;
        auto percentile = [&] (double pct) {
            const auto want = (long long) std::ceil (pct * (double) n);
            long long seen = 0;
            for (int b = first; b < bins; ++b)
            {
                seen += count[(std::size_t) b];
                if (seen >= std::max (1LL, want)) return lo + (b + 0.5) * step;
            }
            return lo + (bins - 0.5) * step;
        };
        return std::max (0.0, percentile (highPct) - percentile (lowPct));
    }

private:
    int firstBin (double gate) const noexcept
    {
        // a bin is in when its centre is at or above the gate
        return std::clamp ((int) std::ceil ((gate - lo) / step - 0.5), 0, bins);
    }
    std::array<int, bins> count {};
    std::array<double, bins> energy {};
    long long total = 0;
};

// 4× true-peak interpolator for one channel
class TruePeak
{
public:
    static constexpr int phases = 4, tapsPerPhase = 12;
    TruePeak()
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr int n = phases * tapsPerPhase;
        for (int k = 0; k < n; ++k)
        {
            const double t = (k - (n - 1) / 2.0) / phases;
            const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (k + 0.5) / n); // Hann
            h[(std::size_t) (k % phases)][(std::size_t) (k / phases)] = sinc * w;
        }
        for (auto& ph : h) // each phase passes DC at unity
        {
            double s = 0.0;
            for (const auto c : ph) s += c;
            for (auto& c : ph) c /= s;
        }
    }
    void clear() noexcept { hist.fill (0.0); pos = 0; }
    // the highest |value| among the 4 interpolated points of this sample period
    double push (double x) noexcept
    {
        // the history is kept twice, newest first, so the taps read one run of memory with no wrap test (round 190am:
        // a third of the time; the same products added in the same order, so the result is bit-identical)
        pos = pos == 0 ? tapsPerPhase - 1 : pos - 1;
        hist[(std::size_t) pos] = x;
        hist[(std::size_t) (pos + tapsPerPhase)] = x;
        const double* newest = &hist[(std::size_t) pos];
        double m = 0.0;
        for (int ph = 0; ph < phases; ++ph)
        {
            double s = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k)
                s += h[(std::size_t) ph][(std::size_t) k] * newest[k];
            m = std::max (m, std::abs (s));
        }
        return m;
    }

private:
    std::array<std::array<double, tapsPerPhase>, phases> h {};
    std::array<double, tapsPerPhase * 2> hist {};
    int pos = 0;
};

// the whole meter (stereo)
class Meter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c) kWeighting (sr, shelf[c], highPass[c]);
        hop = std::max (1, (int) std::lround (sr * 0.1));
        reset();
    }
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c) { shelf[c].clear(); highPass[c].clear(); tp[c].clear(); }
        sub.fill (0.0);
        subCount = 0; subPos = 0; acc = 0.0; accN = 0;
        integrated.clear(); shortTerm.clear();
        momentaryLufs = shortTermLufs = silence;
        truePeakMax = samplePeakMax = 0.0;
        blockTruePeak = blockSamplePeak = 0.0;
    }

    void process (const float* l, const float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double xl = l[i], xr = r[i];
            const double kl = highPass[0].run (shelf[0].run (xl)), kr = highPass[1].run (shelf[1].run (xr));
            acc += kl * kl + kr * kr;
            const double t = std::max (tp[0].push (xl), tp[1].push (xr));
            blockTruePeak = std::max (blockTruePeak, t);
            blockSamplePeak = std::max (blockSamplePeak, std::max (std::abs (xl), std::abs (xr)));
            if (++accN >= hop) endHop();
        }
        truePeakMax = std::max (truePeakMax, blockTruePeak);
        samplePeakMax = std::max (samplePeakMax, blockSamplePeak);
    }

    double momentary() const noexcept { return momentaryLufs; }
    double shortTermLoudness() const noexcept { return shortTermLufs; }
    double integratedLoudness() const noexcept { return integrated.gatedMean (10.0); }
    double loudnessRange() const noexcept { return shortTerm.range (20.0, 0.10, 0.95); }
    double truePeakDb() const noexcept { return truePeakMax > 1e-6 ? 20.0 * std::log10 (truePeakMax) : silence; }
    double samplePeakDb() const noexcept { return samplePeakMax > 1e-6 ? 20.0 * std::log10 (samplePeakMax) : silence; }
    // the peaks since the last call (for a block meter), then cleared
    double takeBlockTruePeak() noexcept { const auto v = blockTruePeak; blockTruePeak = 0.0; return v; }
    double takeBlockSamplePeak() noexcept { const auto v = blockSamplePeak; blockSamplePeak = 0.0; return v; }

private:
    void endHop() noexcept
    {
        sub[(std::size_t) subPos] = acc / (double) accN;
        subPos = (subPos + 1) % subs;
        subCount = std::min (subCount + 1, subs);
        acc = 0.0; accN = 0;
        auto meanOf = [this] (int k) {
            double s = 0.0;
            for (int j = 0; j < k; ++j) s += sub[(std::size_t) ((subPos - 1 - j + subs * 2) % subs)];
            return s / k;
        };
        if (subCount >= 4)
        {
            const auto m = meanOf (4);
            momentaryLufs = lufsOf (m);
            integrated.add (m);
        }
        if (subCount >= 30)
        {
            const auto s = meanOf (30);
            shortTermLufs = lufsOf (s);
            shortTerm.add (s);
        }
    }

    static constexpr int subs = 30; // 100 ms sub-blocks: 3 s
    double sr = 48000.0;
    int hop = 4800;
    Biquad shelf[2], highPass[2];
    TruePeak tp[2];
    std::array<double, subs> sub {};
    int subCount = 0, subPos = 0, accN = 0;
    double acc = 0.0;
    Histogram integrated, shortTerm;
    double momentaryLufs = silence, shortTermLufs = silence;
    double truePeakMax = 0.0, samplePeakMax = 0.0, blockTruePeak = 0.0, blockSamplePeak = 0.0;
};
} // namespace lynx::loudness
