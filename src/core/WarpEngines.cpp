#include "core/WarpEngines.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>

#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wpedantic"
 #pragma clang diagnostic ignored "-Wextra-semi"
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wsign-compare"
 #pragma clang diagnostic ignored "-Wimplicit-float-conversion"
 #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wsign-compare"
#elif defined(_MSC_VER)
 #pragma warning(push, 0)
#endif
#include "../../third_party/signalsmith/signalsmith-linear/fft.h"
#if defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
 #pragma warning(pop)
#endif

namespace lynx::warpengine
{
namespace
{
constexpr double pi = 3.141592653589793;

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

// xorshift32: the flux scatter's numbers (a fixed sequence per seed)
struct Random
{
    std::uint32_t s;
    explicit Random (std::uint32_t seed) : s (seed != 0 ? seed : 0x9e3779b9u) {}
    double bipolar() noexcept // −1…1
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (double) s / 2147483647.5 - 1.0;
    }
};

double median (std::vector<float> v)
{
    if (v.empty())
        return std::numeric_limits<double>::quiet_NaN();
    const auto mid = v.begin() + (std::ptrdiff_t) (v.size() / 2);
    std::nth_element (v.begin(), mid, v.end());
    if (v.size() % 2 == 1)
        return *mid;
    const auto lower = *std::max_element (v.begin(), mid);
    return 0.5 * ((double) lower + (double) *mid);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Band-limited resampling

Channels resampleTo (const Channels& in, long outFrames)
{
    Channels out;
    if (in.empty() || outFrames <= 0)
        return out;
    const auto n = (long) in[0].size();
    if (n == 0)
    {
        out.assign (in.size(), std::vector<float> ((std::size_t) outFrames, 0.0f));
        return out;
    }
    // the prototype kernel in units of the cut-off: 32 zero crossings each side, Kaiser β = 9 (stop band beyond −90 dB)
    constexpr int zeros = 32, phases = 512;
    constexpr double beta = 9.0;
    const auto step = (double) n / (double) outFrames;   // input frames per output frame
    const auto cutoff = std::min (1.0, 1.0 / step) * (step > 1.0 ? 0.97 : 1.0); // (reading faster: below the new Nyquist)
    const auto norm = 1.0 / besselI0 (beta);
    std::vector<float> table ((std::size_t) (2 * zeros * phases + 2));
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        const auto u = (double) i / phases - zeros; // in cut-off units
        const auto r = std::abs (u) / zeros;
        const auto w = r >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) * norm;
        table[i] = (float) ((u == 0.0 ? 1.0 : std::sin (pi * u) / (pi * u)) * w * cutoff);
    }
    const auto half = (double) zeros / cutoff; // input frames each side
    for (const auto& x : in)
    {
        std::vector<float> y ((std::size_t) outFrames);
        for (long j = 0; j < outFrames; ++j)
        {
            const auto pos = (double) j * step;
            const auto first = std::max (0L, (long) std::ceil (pos - half));
            const auto last = std::min (n - 1, (long) std::floor (pos + half));
            double acc = 0.0;
            for (long i = first; i <= last; ++i)
            {
                const auto t = ((double) i - pos) * cutoff + zeros; // 0…2·zeros
                const auto ti = t * phases;
                const auto k = (std::size_t) ti;
                if (k + 1 >= table.size())
                    continue;
                const auto f = (float) (ti - (double) k);
                acc += (double) x[(std::size_t) i] * (double) (table[k] + (table[k + 1] - table[k]) * f);
            }
            y[(std::size_t) j] = (float) acc;
        }
        out.push_back (std::move (y));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Complex Pro: formants

int formantLifter (double envelope, double sampleRate) noexcept
{
    // Live's Envelope 8…256 (default 128): a quarter of it is the lifter in samples at 44.1 kHz [fitted on the W2 renders,
    // Envelope 32 / 64 / 128 / 256 at +7 st on the vowel: little, most, all of the formants kept: REA_rodada190ik §2]
    const auto e = std::clamp (envelope, 8.0, 256.0);
    return std::max (2, (int) std::lround (e * 0.25 * sampleRate / 44100.0));
}

void keepFormants (Channels& y, double sampleRate, double factor, double amount, double envelope)
{
    if (y.empty() || y[0].empty() || ! (factor > 0.0) || std::abs (factor - 1.0) < 1e-6 || ! (amount > 0.0))
        return;
    const int size = sampleRate <= 50000.0 ? 2048 : sampleRate <= 100000.0 ? 4096 : 8192;
    const int hop = size / 4, bins = size / 2;
    const auto frames = (long) y[0].size();
    const auto channels = y.size();
    const auto shift = std::pow (factor, std::clamp (amount, 0.0, 1.0)); // the envelope is read this much higher
    const auto lifter = std::min (formantLifter (envelope, sampleRate), bins - 2);
    // the most the correction raises a bin (nepers): 6 dB when shifted up, 60 dB down [the W2 grids, REA_rodada190ik §2:
    // shifted down, Live raises the faint top of the spectrum back to the formant-kept level — octave-down chords keep
    // their highs (spectrum 6.2 → 5.4 dB vs Live); shifted up, a larger raise overshoots the first formant]
    const double maxBoost = (factor > 1.0 ? 6.0 : 60.0) / 8.685889638;

    signalsmith::linear::RealFFT<float> fft (size);
    std::vector<float> window ((std::size_t) size), frame ((std::size_t) size), ceps ((std::size_t) size);
    for (int i = 0; i < size; ++i)
        window[(std::size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
    // the window's own autocorrelation (normalised), for the voicing measure below
    const auto lagMin = std::max (1, (int) (sampleRate / 1000.0)), lagMax = std::min (size / 2, (int) (sampleRate / 60.0));
    std::vector<double> windowAc ((std::size_t) lagMax + 1, 1.0);
    {
        double zero = 0.0;
        for (int i = 0; i < size; ++i)
            zero += (double) window[(std::size_t) i] * window[(std::size_t) i];
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            double acc = 0.0;
            for (int i = 0; i + lag < size; ++i)
                acc += (double) window[(std::size_t) i] * window[(std::size_t) i + (std::size_t) lag];
            windowAc[(std::size_t) lag] = std::max (1e-6, acc / zero);
        }
    }
    std::vector<std::vector<std::complex<float>>> spectra (channels, std::vector<std::complex<float>> ((std::size_t) bins));
    std::vector<std::complex<float>> cs ((std::size_t) bins);
    std::vector<double> logMag ((std::size_t) bins + 1), target ((std::size_t) bins + 1), env ((std::size_t) bins + 1);
    Channels out (channels, std::vector<float> ((std::size_t) frames, 0.0f));
    const auto scale = 1.0f / ((float) size * 1.5f); // (ifft unscaled; Hann² at a quarter hop sums to 1.5)

    // the smooth (liftered) cepstral envelope of `in` (log magnitudes, bins + 1 values) into `outEnv`
    const auto smoothWith = [&] (const std::vector<double>& in, std::vector<double>& outEnv, int lifter) {
        cs[0] = { (float) in[0], (float) in[(std::size_t) bins] };
        for (int k = 1; k < bins; ++k)
            cs[(std::size_t) k] = { (float) in[(std::size_t) k], 0.0f };
        fft.ifft (cs.data(), ceps.data());
        for (int i = lifter + 1; i < size - lifter; ++i)
            ceps[(std::size_t) i] = 0.0f;
        for (auto& c : ceps)
            c /= (float) size;
        fft.fft (ceps.data(), cs.data());
        outEnv[0] = cs[0].real();
        outEnv[(std::size_t) bins] = cs[0].imag();
        for (int k = 1; k < bins; ++k)
            outEnv[(std::size_t) k] = cs[(std::size_t) k].real();
    };
    const auto smooth = [&] (const std::vector<double>& in, std::vector<double>& outEnv) { smoothWith (in, outEnv, lifter); };
    std::vector<double> power ((std::size_t) bins + 1);
    // Complex Pro's level after the correction (Live 12.4, W2 voice / vowel / chords at −12…+12 st, Formants 0 / 50 / 100,
    // Envelope 32…256): a law of the effective shift u = semitones × Formants × h(Envelope) — −0.69·u^0.75 dB up,
    // +0.016·u² dB down — blended (0.65) with the level the correction itself gives; only as far as the frame is pitched
    const auto shiftSt = 12.0 * std::log2 (factor) * std::clamp (amount, 0.0, 1.0);
    const auto envOctaves = std::log2 (std::clamp (envelope, 8.0, 256.0));
    const auto envFactor = envOctaves <= 5.0 ? 0.0 : envOctaves <= 6.0 ? 0.6 * (envOctaves - 5.0)
                         : envOctaves <= 7.0 ? 0.6 + 0.4 * (envOctaves - 6.0) : 1.0 + 0.2 * (envOctaves - 7.0);
    const auto effective = shiftSt * envFactor;
    const auto lawDb = effective >= 0.0 ? -0.69 * std::pow (effective, 0.75) : 0.016 * effective * effective;
    constexpr double lawShare = 0.65;

    for (long start = -size + hop; start < frames; start += hop)
    {
        // every channel's spectrum, and the mono magnitude the envelope comes from
        double peak = 0.0;
        for (std::size_t c = 0; c < channels; ++c)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto at = start + i;
                frame[(std::size_t) i] = at >= 0 && at < frames ? y[c][(std::size_t) at] * window[(std::size_t) i] : 0.0f;
            }
            fft.fft (frame.data(), spectra[c].data());
        }
        for (int k = 0; k <= bins; ++k)
        {
            double power = 0.0;
            for (std::size_t c = 0; c < channels; ++c)
            {
                const auto v = k == 0 ? (double) spectra[c][0].real() : k == bins ? (double) spectra[c][0].imag() : (double) std::abs (spectra[c][(std::size_t) k]);
                power += v * v;
            }
            logMag[(std::size_t) k] = std::sqrt (power / (double) channels);
            peak = std::max (peak, logMag[(std::size_t) k]);
        }
        const auto silent = peak < 1e-7;
        // the frame's fundamental: the highest normalised autocorrelation at a lag of 1…16.7 ms (60 Hz…1 kHz), from the
        // power spectrum, the window's own autocorrelation divided out. How tonal it is: the spectral flatness over
        // 300 Hz…8 kHz (0.003 and below: tonal, also polyphonic; 0.03 and above: noise and hits). Live corrects the
        // formants of tonal sound only [W2: its drums render at +7 st with Formants 100 differs from Formants 0 by 0.03 dB,
        // spectrum 0.78 dB; the voice by 1.4 dB / 3.9 dB, the chords by 2.7 / 3.7]
        double weight = 0.0;
        int bestLag = lagMax;
        if (! silent)
        {
            cs[0] = { (float) (logMag[0] * logMag[0]), (float) (logMag[(std::size_t) bins] * logMag[(std::size_t) bins]) };
            for (int k = 1; k < bins; ++k)
                cs[(std::size_t) k] = { (float) (logMag[(std::size_t) k] * logMag[(std::size_t) k]), 0.0f };
            fft.ifft (cs.data(), ceps.data());
            double best = 0.0;
            if (ceps[0] > 0.0f)
                for (int lag = lagMin; lag <= lagMax; ++lag)
                    if (const auto r = (double) ceps[(std::size_t) lag] / (double) ceps[0] / windowAc[(std::size_t) lag]; r > best)
                    {
                        best = r;
                        bestLag = lag;
                    }
            const auto k0 = std::max (1, (int) (300.0 * size / sampleRate)), k1 = std::min (bins, (int) (8000.0 * size / sampleRate));
            double logSum = 0.0, sum = 0.0;
            for (int k = k0; k < k1; ++k)
            {
                const auto p = logMag[(std::size_t) k] * logMag[(std::size_t) k] + 1e-20;
                logSum += std::log (p);
                sum += p;
            }
            const auto flat = std::exp (logSum / (k1 - k0)) / (sum / (k1 - k0));
            weight = std::clamp ((std::log10 (std::max (flat, 1e-9)) - std::log10 (0.03)) / (std::log10 (0.003) - std::log10 (0.03)), 0.0, 1.0);
        }
        if (! silent && weight > 0.0)
        {
            const auto floorMag = peak * 1e-5;
            for (int k = 0; k <= bins; ++k)
                power[(std::size_t) k] = logMag[(std::size_t) k] * logMag[(std::size_t) k];
            for (auto& v : logMag)
                v = std::log (std::max (v, floorMag));
            // the "true envelope": the cepstral envelope raised until it passes through the spectral peaks (Röbel & Rodet)
            target = logMag;
            for (int it = 0; it < 16; ++it)
            {
                smooth (target, env);
                double above = 0.0;
                for (int k = 0; k <= bins; ++k)
                {
                    above = std::max (above, logMag[(std::size_t) k] - env[(std::size_t) k]);
                    target[(std::size_t) k] = std::max (logMag[(std::size_t) k], env[(std::size_t) k]);
                }
                if (above < 0.23) // (2 dB)
                    break;
            }
            // the gain that moves the envelope back down (or up) by `shift`: the bin k wants the envelope found at k · shift
            // below the frame's fundamental there is no envelope to read (the floor would pull the bass away when the sound is
            // shifted down): the envelope is held at the fundamental there
            const auto lowest = std::min ((double) bins, (double) size / (double) std::max (1, bestLag));
            for (int k = 0; k <= bins; ++k)
            {
                const auto from = std::clamp ((double) k * shift, std::min (lowest, (double) k), (double) bins);
                const auto k0 = (int) from;
                const auto f = from - k0;
                const auto wanted = k0 >= bins ? env[(std::size_t) bins] : env[(std::size_t) k0] + (env[(std::size_t) k0 + 1] - env[(std::size_t) k0]) * f;
                target[(std::size_t) k] = std::exp (weight * std::clamp (wanted - env[(std::size_t) k], -4.6, maxBoost)); // (−40 dB…the limit)
            }
            // the level law (above)
            {
                double total = 0.0, now = 0.0;
                for (int k = 0; k <= bins; ++k)
                {
                    total += power[(std::size_t) k];
                    now += power[(std::size_t) k] * target[(std::size_t) k] * target[(std::size_t) k];
                }
                if (total > 0.0 && now > 0.0)
                {
                    const auto naturalDb = 10.0 * std::log10 (now / total);
                    const auto wantDb = (1.0 - lawShare) * naturalDb + lawShare * lawDb * weight;
                    const auto sc = std::clamp (std::pow (10.0, (wantDb - naturalDb) / 20.0), 0.125, 8.0);
                    for (auto& g : target)
                        g *= sc;
                }
            }
            for (std::size_t c = 0; c < channels; ++c)
            {
                auto& s = spectra[c];
                s[0] = { s[0].real() * (float) target[0], s[0].imag() * (float) target[(std::size_t) bins] };
                for (int k = 1; k < bins; ++k)
                    s[(std::size_t) k] *= (float) target[(std::size_t) k];
            }
        }
        for (std::size_t c = 0; c < channels; ++c)
        {
            fft.ifft (spectra[c].data(), frame.data());
            for (int i = 0; i < size; ++i)
            {
                const auto at = start + i;
                if (at >= 0 && at < frames)
                    out[c][(std::size_t) at] += frame[(std::size_t) i] * window[(std::size_t) i] * scale;
            }
        }
    }
    y = std::move (out);
}

// ---------------------------------------------------------------------------------------------------------------
// Texture: granular

double textureGrainPeriodMs (double grainMs) noexcept
{
    // Live 12.4 (W1 flux-0 renders): one grain every Grain Size ms [the grains' source jumps sit exactly 65.0 ms apart at
    // Grain Size 65, both speeds]
    // below 10 ms the period no longer follows Grain Size one to one [W1 Grain Size 2 renders: the comb of a 2 ms period
    // is not Live's; 10 ms × (size / 10)^0.2, i.e. 7.2 ms at 2, matched them best of 0.1 / 0.2 / 0.35 — voice spectrum 7.7 →
    // 3.4 dB; at 10 and up the W2 renders keep one grain per Grain Size ms]
    if (grainMs < 10.0)
        return 10.0 * std::pow (std::max (grainMs, 1.0) / 10.0, 0.2);
    return std::clamp (grainMs, 1.0, 263.0);
}

double textureFluxSpreadMs (double grainMs, double flux) noexcept
{
    // the half-width of the uniform scatter of each grain's source position. Live 12.4 (W1 / W2 voice and chords, the
    // spectrogram decorrelation from the flux-0 grains): Flux 25, 50 and 100 scatter alike — about an eighth of the grain
    // period from Grain Size 65 up, relatively less below; Flux 0 none. Between 0 and 25 the scatter is assumed to grow
    // linearly (not rendered) [REA_rodada190ik §3]
    const auto period = textureGrainPeriodMs (grainMs);
    return 0.125 * period * std::min (1.0, period / 65.0) * std::clamp (flux / 25.0, 0.0, 1.0);
}

Channels texture (const Channels& in, long outFrames, double sampleRate, const std::function<double (double)>& toSource, double grainMs,
                  double flux, std::uint32_t seed, double factor)
{
    factor = std::clamp (factor, 0.0625, 16.0);
    Channels out (in.size(), std::vector<float> ((std::size_t) std::max (0L, outFrames), 0.0f));
    if (in.empty() || outFrames <= 0)
        return out;
    const auto n = (long) in[0].size();
    const auto period = textureGrainPeriodMs (grainMs) * sampleRate / 1000.0; // frames between grain anchors
    const auto length = 2.0 * period;                                        // two grains overlap everywhere
    const auto spread = textureFluxSpreadMs (grainMs, flux) * sampleRate / 1000.0;
    Random random (seed);
    // 4-point Hermite read of channel c at source position t (silence outside the source)
    const auto read = [&] (const std::vector<float>& x, double t) {
        const auto i = (long) std::floor (t);
        const auto f = (float) (t - (double) i);
        const auto get = [&] (long j) { return j >= 0 && j < n ? x[(std::size_t) j] : 0.0f; };
        const auto y0 = get (i - 1), y1 = get (i), y2 = get (i + 1), y3 = get (i + 2);
        const auto c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    };
    for (long k = -2;; ++k)
    {
        // grain k: anchored at (k + ½) periods, it reads the source from where the map puts the anchor (± the flux scatter)
        // at the source's own speed; its sine window spans half a period before the anchor to 1½ after, so two grains
        // overlap everywhere and their windows' squares add to 1 (the level of uncorrelated grains is kept) [W1 flux 0:
        // sine windows, no normalisation — Live's level within 0.01 dB, sample correlation 0.996 / 0.998 on the voice]
        const auto anchor = ((double) k + 0.5) * period;
        const auto begin = anchor - 0.5 * period;
        if (begin >= (double) outFrames)
            break;
        const auto jitter = spread > 0.0 ? spread * random.bipolar() : 0.0;
        const auto from = toSource (anchor) + jitter;
        const auto first = std::max (0L, (long) std::ceil (begin));
        const auto last = std::min (outFrames - 1, (long) std::floor (begin + length));
        for (long t = first; t <= last; ++t)
        {
            const auto u = ((double) t - begin) / length;
            const auto w = (float) std::sin (pi * u);
            if (w <= 0.0f)
                continue;
            const auto src = from + ((double) t - anchor) * factor;
            if (src < -2.0 || src > (double) n + 2.0)
                continue;
            for (std::size_t c = 0; c < in.size(); ++c)
                out[c][(std::size_t) t] += w * read (in[c], src);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Beats: the pitch-aware detector

PitchTrack pitchTrack (const float* mono, long frames, double sampleRate)
{
    PitchTrack track;
    track.hop = std::max (1L, (long) std::lround (sampleRate * 0.005));
    if (frames <= 0 || ! (sampleRate > 0.0))
        return track;
    // decimate to about 11 kHz (a 63-tap windowed-sinc low pass first)
    const auto down = std::max (1, (int) std::lround (sampleRate / 11025.0));
    const auto rate = sampleRate / down;
    std::vector<float> x;
    if (down == 1)
        x.assign (mono, mono + frames);
    else
    {
        constexpr int taps = 63, half = taps / 2;
        std::vector<float> h (taps);
        const auto fc = 0.45 / down;
        double sum = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const auto t = i - half;
            const auto s = t == 0 ? 2.0 * fc : std::sin (2.0 * pi * fc * t) / (pi * t);
            h[(std::size_t) i] = (float) (s * (0.54 - 0.46 * std::cos (2.0 * pi * i / (taps - 1))));
            sum += h[(std::size_t) i];
        }
        for (auto& v : h)
            v = (float) (v / sum);
        x.resize ((std::size_t) (frames / down + 1));
        for (std::size_t j = 0; j < x.size(); ++j)
        {
            const auto c = (long) j * down;
            double acc = 0.0;
            for (int i = 0; i < taps; ++i)
                if (const auto at = c + i - half; at >= 0 && at < frames)
                    acc += (double) mono[at] * h[(std::size_t) i];
            x[j] = (float) acc;
        }
    }
    const auto m = (long) x.size();
    const auto window = (long) std::lround (rate * 0.035);
    const auto tauMin = std::max (2L, (long) std::floor (rate / 1000.0));
    const auto tauMax = (long) std::ceil (rate / 60.0);
    const auto count = frames / track.hop + 1;
    track.semitones.assign ((std::size_t) count, std::numeric_limits<float>::quiet_NaN());
    // the loudest frame's energy (frames 40 dB below it are not pitched)
    double loudest = 0.0;
    std::vector<double> energy ((std::size_t) count, 0.0);
    for (long i = 0; i < count; ++i)
    {
        const auto c = (long) ((double) (i * track.hop) / down) - window / 2;
        double e = 0.0;
        for (long j = std::max (0L, c); j < std::min (m, c + window); ++j)
            e += (double) x[(std::size_t) j] * x[(std::size_t) j];
        energy[(std::size_t) i] = e;
        loudest = std::max (loudest, e);
    }
    std::vector<double> d ((std::size_t) tauMax + 2);
    for (long i = 0; i < count; ++i)
    {
        if (! (energy[(std::size_t) i] > loudest * 1e-4))
            continue;
        const auto c = (long) ((double) (i * track.hop) / down) - window / 2; // (the frame is centred on the hop's time)
        if (c < 0 || c + window + tauMax >= m)
            continue;
        const auto* s = x.data() + c;
        // YIN: difference function and its cumulative mean normalisation
        double running = 0.0;
        d[0] = 1.0;
        for (long tau = 1; tau <= tauMax; ++tau)
        {
            double acc = 0.0;
            for (long j = 0; j < window; ++j)
            {
                const auto v = (double) s[j] - s[j + tau];
                acc += v * v;
            }
            running += acc;
            d[(std::size_t) tau] = running > 0.0 ? acc * (double) tau / running : 1.0;
        }
        long best = -1;
        for (long tau = tauMin; tau < tauMax; ++tau)
            if (d[(std::size_t) tau] < 0.15)
            {
                while (tau + 1 < tauMax && d[(std::size_t) tau + 1] < d[(std::size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
            continue;
        // parabolic refinement
        const auto a = d[(std::size_t) best - 1], b = d[(std::size_t) best], e = d[(std::size_t) best + 1];
        const auto den = a - 2.0 * b + e;
        const auto t = (double) best + (std::abs (den) > 1e-12 ? 0.5 * (a - e) / den : 0.0);
        const auto f0 = rate / t;
        track.semitones[(std::size_t) i] = (float) (69.0 + 12.0 * std::log2 (f0 / 440.0));
    }
    return track;
}

std::vector<long> pitchAwareCuts (const float* mono, long frames, double sampleRate, const std::vector<long>& levelCuts)
{
    std::vector<long> cuts;
    if (frames < 256 || ! (sampleRate > 0.0))
        return levelCuts;
    const auto track = pitchTrack (mono, frames, sampleRate);
    const auto& p = track.semitones;
    const auto count = (long) p.size();
    const auto hop = track.hop;
    const auto at = [&] (long i) { return i >= 0 && i < count ? p[(std::size_t) i] : std::numeric_limits<float>::quiet_NaN(); };
    const auto voiced = [&] (long from, long to, std::vector<float>& v) {
        v.clear();
        for (long i = from; i <= to; ++i)
            if (const auto s = at (i); ! std::isnan (s))
                v.push_back (s);
        return (long) v.size();
    };

    // 1. the level detector's cuts, without the ones a pitch glide or vibrato explains: no rise of the plain level (≥ 6 dB
    //    within 25 ms over the 40 ms before; a vibrato's own level swing stays under it) while the sound is pitched and its
    //    pitch moves
    std::vector<double> sum ((std::size_t) frames + 1, 0.0);
    for (long i = 0; i < frames; ++i)
        sum[(std::size_t) i + 1] = sum[(std::size_t) i] + (double) mono[i] * mono[i];
    const auto w = std::max (8L, (long) std::lround (sampleRate * 0.010));
    const auto rms = [&] (long from) {
        from = std::clamp (from, 0L, std::max (0L, frames - w));
        return std::sqrt ((sum[(std::size_t) (from + w)] - sum[(std::size_t) from]) / (double) w);
    };
    const auto maxRms = [&] (long from, long to) {
        double m = 0.0;
        for (long i = from; i <= to; i += std::max (1L, w / 4))
            m = std::max (m, rms (i));
        return m;
    };
    std::vector<float> v1, v2;
    for (const auto c : levelCuts)
    {
        const auto before = maxRms (c - (long) (sampleRate * 0.040), c - (long) (sampleRate * 0.005) - w);
        const auto after = maxRms (c, c + (long) (sampleRate * 0.025));
        const auto riseDb = 20.0 * std::log10 ((after + 1e-12) / (before + 1e-12));
        const auto i = c / hop;
        const auto span = (long) std::lround (0.030 * sampleRate / (double) hop);
        const auto nb = voiced (i - 2 * span, i - 1, v1), na = voiced (i + 1, i + 2 * span, v2);
        const auto pitched = nb >= span && na >= span;
        const auto moves = pitched && std::abs (median (v2) - median (v1)) > 0.25;
        if (riseDb < 6.0 && moves)
            continue;
        cuts.push_back (c);
    }

    // 2. legato pitch steps: the median pitch 15…60 ms after differs from the one 15…60 ms before by ≥ 0.8 semitone, the
    //    change lasts (60…150 ms either side the same way) and happens within 60 ms (a glide takes longer, vibrato returns)
    const auto f = [&] (double ms) { return (long) std::lround (ms * 0.001 * sampleRate / (double) hop); };
    // how far the pitch moves across frames [from, to] (least-squares slope × the span), over its pitched frames
    const auto drift = [&] (long from, long to) {
        double n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (long k = from; k <= to; ++k)
            if (const auto y = at (k); ! std::isnan (y))
            {
                n += 1.0;
                sx += (double) k;
                sy += (double) y;
                sxx += (double) k * (double) k;
                sxy += (double) k * (double) y;
            }
        const auto den = n * sxx - sx * sx;
        return n >= 3.0 && den > 0.0 ? (n * sxy - sx * sy) / den * (double) (to - from) : 0.0;
    };
    const auto n3 = f (15.0), n12 = f (60.0), n30 = f (150.0);
    std::vector<double> jump ((std::size_t) count, 0.0);
    std::vector<double> base ((std::size_t) count, 0.0);
    for (long i = n12; i + n12 < count; ++i)
    {
        if (voiced (i - n12, i - n3, v1) < (n12 - n3) * 2 / 3 || voiced (i + n3, i + n12, v2) < (n12 - n3) * 2 / 3)
            continue;
        const auto mb = median (v1), ma = median (v2), j = ma - mb;
        if (std::abs (j) < 0.8)
            continue;
        // flat on both sides: a glide still moves inside the windows (its slope across a window is a good part of the jump)
        if (std::abs (drift (i - n12, i - n3)) > 0.35 * std::abs (j) || std::abs (drift (i + n3, i + n12)) > 0.35 * std::abs (j))
            continue;
        // lasting: the farther windows move the same way by at least half as much (a sound that ends or stops being pitched
        // there is not a step: its pitch track stumbles as it fades)
        if (voiced (i - n30, i - n12, v1) < (n30 - n12) / 2 || voiced (i + n12, i + n30, v2) < (n30 - n12) / 2)
            continue;
        {
            const auto far = median (v2) - median (v1);
            if (far * j <= 0.0 || std::abs (far) < 0.5 * std::abs (j))
                continue;
        }
        // not the end of a sound (its pitch track stumbles as it fades): the level after holds within 6 dB of the one before
        if (rms (std::min (frames - w, (i + n3) * hop)) < 0.5 * rms (std::max (0L, (i - n12) * hop))
            || rms (std::min (frames - w, (i + n12) * hop)) < 0.5 * rms (std::max (0L, (i - n12) * hop)))
            continue;
        // fast: few frames between 15 % and 85 % of the step
        long between = 0;
        for (long k = i - n12; k <= i + n12; ++k)
            if (const auto s = at (k); ! std::isnan (s))
            {
                const auto u = ((double) s - mb) / j;
                between += u > 0.15 && u < 0.85 ? 1 : 0;
            }
        if (between > f (60.0))
            continue;
        jump[(std::size_t) i] = j;
        base[(std::size_t) i] = mb;
    }
    std::vector<long> steps;
    const auto lag = (long) std::lround (0.012 * sampleRate);
    for (long i = 0; i < count; ++i)
    {
        const auto j = jump[(std::size_t) i];
        if (j == 0.0)
            continue;
        bool peak = true;
        for (long k = std::max (0L, i - n12); k <= std::min (count - 1, i + n12) && peak; ++k)
            if (k != i && (std::abs (jump[(std::size_t) k]) > std::abs (j) || (std::abs (jump[(std::size_t) k]) == std::abs (j) && k < i)))
                peak = false;
        if (! peak)
            continue;
        // the cut: where the pitch crosses the middle of the step
        long cross = i;
        for (long k = i - n12; k <= i + n12; ++k)
            if (const auto s = at (k); ! std::isnan (s) && ((double) s - base[(std::size_t) i]) / j >= 0.5)
            {
                cross = k;
                break;
            }
        steps.push_back (std::max (0L, cross * hop - lag)); // (the YIN window reports a step ~12 ms late)
    }
    // 3. re-articulations of a held sound: the level dips ≥ 12 dB under a steady level and is back within 3 dB of it within
    //    60 ms (a sung syllable, a re-bowed note) — the level detector, which looks for fast rises of the high frequencies,
    //    misses these soft restarts. The cut: the steepest rise out of the dip.
    {
        const auto step = std::max (1L, w / 4);
        const auto ms = [&] (double v) { return (long) std::lround (v * 0.001 * sampleRate); };
        for (long t = ms (60.0); t + ms (80.0) < frames; t += step)
        {
            const auto here = rms (t);
            if (here <= 0.0 || rms (t - step) < here || rms (t + step) < here) // (the dip's bottom)
                continue;
            double before = 1e30, beforeMax = 0.0;
            for (long u = t - ms (50.0); u <= t - ms (25.0); u += step)
            {
                before = std::min (before, rms (u));
                beforeMax = std::max (beforeMax, rms (u));
            }
            if (! (before > 0.0) || beforeMax > 1.4 * before || here > before * 0.25) // (steady before, ≥ 12 dB deep)
                continue;
            // (a held pitched sound: pitched before the dip and after it)
            if (std::isnan (at ((t - ms (40.0)) / hop)) || std::isnan (at ((t + ms (60.0)) / hop)))
                continue;
            long back = -1;
            for (long u = t; u <= t + ms (60.0); u += step)
                if (rms (u) >= before * 0.7)
                {
                    back = u;
                    break;
                }
            if (back < 0)
                continue;
            long steepest = t;
            double rise = -1.0;
            for (long u = t; u < back; u += step)
                if (const auto d = rms (u + step) - rms (u); d > rise)
                {
                    rise = d;
                    steepest = u + w; // (where the window starting at u + step meets the new level)
                }
            steps.push_back (steepest);
            t = back;
        }
    }
    const auto near = (long) (sampleRate * 0.050);
    for (const auto s : steps)
        if (std::none_of (cuts.begin(), cuts.end(), [&] (long c) { return std::abs (c - s) < near; }))
            cuts.push_back (s);
    std::sort (cuts.begin(), cuts.end());
    cuts.erase (std::remove_if (cuts.begin(), cuts.end(), [frames] (long c) { return c <= 0 || c >= frames; }), cuts.end());
    return cuts;
}
} // namespace lynx::warpengine
