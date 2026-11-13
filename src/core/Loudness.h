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
// - True peak: 4× oversampling (BS.1770 annex 2; a half-band cascade with a 128-tap first stage, the samples
//   included, see TruePeak), the highest absolute value (dBTP), and the plain sample peak beside it.
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

// ---- true peak (BS.1770-4 annex 2), round 190co ----
// The annex asks for the highest absolute value of the signal oversampled 4×. What that value is depends on the
// interpolation filter wherever the programme has energy next to half the sample rate (a limited or clipped signal
// always has): a short filter (the old one here: 12 taps per phase, Hann) read up to 1.35 dB under and 0.25 dB over a
// long one on such material, and its four points sat between the samples, so a peak lying on a sample was missed too.
// This one is a half-band cascade: 1× → 2× with a 128-tap Kaiser (β = 10) windowed sinc (the same reconstruction as
// the 16× / 128-taps-per-phase reference meter of round 190cn, flat to ≈ 0.475 of the rate), 2× → 4× with 12 taps (the
// 2× signal only reaches half of its band), then a parabola through the three 4× points around every local maximum
// for the last hundredths of a dB. The samples themselves are part of the 4× sequence. Against the 16× reference on
// round 190cn's limited / clipped material (44.1, 48 and 96 kHz): −0.02 … +0.01 dB (tests/ApexFix190coTests.inc).

// Σ a[k] · b[k], n a multiple of 4: four separate sums (lanes), so the compiler may use SIMD without reordering
// anything, and the result is the same with or without it
inline float dot4 (const float* a, const float* b, int n) noexcept
{
    float s[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < n; k += 4)
        for (int j = 0; j < 4; ++j)
            s[j] += a[k + j] * b[k + j];
    return (s[0] + s[1]) + (s[2] + s[3]);
}

// Σ c[k] · (a[k] + b[k]), n a multiple of 4: a symmetric filter folded in half (a = the newest half, newest first,
// b = the oldest half, oldest first)
inline float dotFolded4 (const float* c, const float* a, const float* b, int n) noexcept
{
    float s[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < n; k += 4)
        for (int j = 0; j < 4; ++j)
            s[j] += c[k + j] * (a[k + j] + b[k + j]);
    return (s[0] + s[1]) + (s[2] + s[3]);
}

// modified Bessel function I0 (for the Kaiser window)
inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-18 * sum) break;
    }
    return sum;
}

// Kaiser window at `u` (−1 … 1 across the window)
inline double kaiser (double u, double beta) noexcept
{
    return std::abs (u) >= 1.0 ? (std::abs (u) > 1.0 ? 0.0 : 1.0 / besselI0 (beta)) : besselI0 (beta * std::sqrt (1.0 - u * u)) / besselI0 (beta);
}

// the highest point of the parabola through three successive absolute values, fed one at a time
struct PeakFit
{
    float p0 = 0.0f, p1 = 0.0f;
    void clear() noexcept { p0 = p1 = 0.0f; }
    // `m` rises to |v| and, when the value before it was a local maximum above `skip`, to that maximum's interpolated
    // height (at most 1.125 × the maximum: a caller that only wants peaks above a level passes level / 1.125)
    void feed (float v, float& m, float skip) noexcept
    {
        const float a = std::abs (v);
        if (p1 > skip && p1 >= p0 && p1 >= a)
        {
            const float den = 2.0f * p1 - p0 - a; // ≥ |p0 − a|, so the correction is at most |p0 − a| / 8
            if (den > 0.0f) m = std::max (m, p1 + 0.125f * (p0 - a) * (p0 - a) / den);
        }
        m = std::max (m, a);
        p0 = p1;
        p1 = a;
    }
};

// true-peak estimator for one channel
class TruePeak
{
public:
    static constexpr int phases = 4;             // the points per sample period (the sample + three between)
    static constexpr int taps = 128, taps2 = 12; // the 1× → 2× and the 2× → 4× half-band interpolators
    // push (x[n]) returns the peak of the sample period centred on x[n − delay] (half a period each side)
    static constexpr int delay = taps / 2 + taps2 / 4 + 1;
    static constexpr double fitReach = 1.125;    // the parabola never adds more than an eighth

    TruePeak() { (void) table(); } // (the coefficients are built here, never on the audio thread)
    void clear() noexcept
    {
        h1.fill (0.0f); h1r.fill (0.0f); h2.fill (0.0f);
        pos = pos2 = posR = 0;
        fit.clear();
        quietRun = taps;
    }
    // Inputs at or under `level` for a whole filter length are not interpolated: push() then returns an upper bound
    // of what the filters could give (level × gateBound()). The meter's gate is far under anything shown (silence costs
    // nothing); a limiter sets it under its ceiling.
    void setGate (double level) noexcept
    {
        const auto g = (float) level;
        if (g != gate) { gate = g; quietRun = 0; }
    }
    static double gateBound() noexcept { return table().bound; }
    bool quiet() const noexcept { return quietRun >= taps; }
    // the input, newest first: [k] = x[n − k], k < taps
    const float* newestFirst() const noexcept { return &h1[(std::size_t) pos]; }

    // The highest |value| of the 4× oversampled signal in one sample period (see `delay`). `above`: the caller only
    // needs peaks over this level (a meter passes its maximum so far): the parabola is skipped under it, the result
    // is then the plain 4× maximum.
    double push (double in, double above = 0.0) noexcept
    {
        // The filters read the history as it was before this sample goes in (their vector loads would otherwise wait
        // for the store just made), hence the 1 in `delay`.
        double result;
        if (quietRun >= taps)
            result = (double) gate * table().bound;
        else
        {
            const auto& t = table();
            const auto skip = (float) (above / fitReach);
            const float* newest = &h1[(std::size_t) pos]; // [k] = x[n − 1 − k]
            float m = 0.0f;
            feed2 (newest[taps / 2], t, m, skip); // x[n − 65]
            // the point half a sample after it: the first stage is symmetric, its two halves are read side by side
            feed2 (dotFolded4 (t.c1.data(), newest, &h1r[(std::size_t) (posR + 1)], taps / 2), t, m, skip);
            result = (double) m;
        }
        // the history is kept twice, newest first, so the taps read one run of memory with no wrap test (round
        // 190am), and once more oldest first for the folded half
        const auto x = (float) in;
        pos = pos == 0 ? taps - 1 : pos - 1;
        h1[(std::size_t) pos] = x;
        h1[(std::size_t) (pos + taps)] = x;
        posR = posR == taps - 1 ? 0 : posR + 1;
        h1r[(std::size_t) posR] = x;
        h1r[(std::size_t) (posR + taps)] = x;
        if (std::abs (x) > gate) quietRun = 0;
        else if (quietRun < taps && ++quietRun == taps) { h2.fill (0.0f); fit.clear(); } // falls asleep: nothing stale is kept
        return result;
    }

private:
    struct Table
    {
        std::array<float, taps / 2> c1 {}; // (the first half; the second mirrors it)
        std::array<float, taps2> c2 {};
        double bound = 1.0;
        Table()
        {
            constexpr double pi = 3.14159265358979323846;
            auto build = [] (float* c, int n, int keep, double beta) {
                double sum = 0.0, l1 = 0.0;
                std::array<double, taps> h {};
                for (int j = 0; j < n; ++j)
                {
                    const double t = j - (n - 1) / 2.0; // half-integers: the point lies midway between two inputs
                    h[(std::size_t) j] = std::sin (pi * t) / (pi * t) * kaiser (t / ((n - 1) / 2.0), beta);
                    sum += h[(std::size_t) j];
                }
                for (int j = 0; j < n; ++j)
                {
                    if (j < keep) c[j] = (float) (h[(std::size_t) j] / sum);
                    l1 += std::abs (h[(std::size_t) j] / sum);
                }
                return l1;
            };
            const double l1 = build (c1.data(), taps, taps / 2, 10.0), l2 = build (c2.data(), taps2, taps2, 8.0);
            bound = std::max (1.0, l1) * std::max (1.0, l2) * fitReach * 1.001;
        }
    };
    static const Table& table() noexcept { static const Table t; return t; }

    // one value of the 2× sequence in: the two 4× values that become known (a 2× value, then the point after it),
    // read before `v` goes into the history
    void feed2 (float v, const Table& t, float& m, float skip) noexcept
    {
        const float* s = &h2[(std::size_t) pos2];
        fit.feed (s[taps2 / 2], m, skip);
        fit.feed (dot4 (t.c2.data(), s, taps2), m, skip);
        pos2 = pos2 == 0 ? taps2 - 1 : pos2 - 1;
        h2[(std::size_t) pos2] = v;
        h2[(std::size_t) (pos2 + taps2)] = v;
    }

    std::array<float, taps * 2> h1 {}, h1r {};
    std::array<float, taps2 * 2> h2 {};
    PeakFit fit;
    int pos = 0, pos2 = 0, posR = 0, quietRun = taps;
    float gate = 1.0e-9f;
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
            const double t = std::max (tp[0].push (xl, blockTruePeak), tp[1].push (xr, blockTruePeak));
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
    // (a true peak is never under the sample peak; the estimator answers `TruePeak::delay` samples late, the sample
    // peak at once)
    double truePeakDb() const noexcept { const auto v = std::max (truePeakMax, samplePeakMax); return v > 1e-6 ? 20.0 * std::log10 (v) : silence; }
    double samplePeakDb() const noexcept { return samplePeakMax > 1e-6 ? 20.0 * std::log10 (samplePeakMax) : silence; }
    // the peaks since the last call (for a block meter), then cleared
    // (call takeBlockTruePeak first: it reads the block's sample peak too)
    double takeBlockTruePeak() noexcept { const auto v = std::max (blockTruePeak, blockSamplePeak); blockTruePeak = 0.0; return v; }
    double takeBlockSamplePeak() noexcept { const auto v = blockSamplePeak; blockSamplePeak = 0.0; return v; }
    // The end of a file: the true-peak estimator still holds the last `TruePeak::delay` samples; this runs them out
    // (as if silence followed) without touching the loudness. For offline measurements; a live meter never needs it.
    void flushTruePeak() noexcept
    {
        for (int i = 0; i <= TruePeak::delay; ++i)
            blockTruePeak = std::max (blockTruePeak, std::max (tp[0].push (0.0), tp[1].push (0.0)));
        truePeakMax = std::max (truePeakMax, blockTruePeak);
    }

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
