#include "core/VocalPitch.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if (defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64)) && ! defined(LYNX_BREATH_SCALAR)
 #include "core/Simd.h" // (x86 SSE, or NEON through sse2neon)
 #define LYNX_BREATH_SIMD 1
#else
 #define LYNX_BREATH_SIMD 0
#endif

namespace lynx::vocal
{
namespace
{
constexpr double pi = 3.14159265358979323846;

#if LYNX_BREATH_SIMD
// The exactly rounded square root of four floats. Not _mm_sqrt_ps on ARM64: with SSE2NEON_PRECISE_SQRT (core/Simd.h)
// sse2neon builds it from the reciprocal-square-root estimate and two Newton steps, which is not exactly rounded and
// returns inf / NaN for inputs in the denormal range (a band's power while the voice has next to nothing there) —
// round 190bb, a NaN in LYNX Tune's output on macOS arm64 only.
inline __m128 exactSqrt (__m128 x) noexcept
{
 #if defined(__aarch64__) || defined(_M_ARM64)
    return vreinterpretq_m128_f32 (vsqrtq_f32 (vreinterpretq_f32_m128 (x)));
 #else
    return _mm_sqrt_ps (x);
 #endif
}
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Scales (the usual Western interval sets, from the key's root)

const char* scaleName (Scale s) noexcept
{
    switch (s)
    {
        case Scale::chromatic: return "Chromatic";
        case Scale::major: return "Major";
        case Scale::minor: return "Minor";
        case Scale::harmonicMinor: return "Harmonic minor";
        case Scale::melodicMinor: return "Melodic minor";
        case Scale::majorPentatonic: return "Major pentatonic";
        case Scale::minorPentatonic: return "Minor pentatonic";
        case Scale::blues: return "Blues";
        case Scale::dorian: return "Dorian";
        case Scale::mixolydian: return "Mixolydian";
        case Scale::phrygian: return "Phrygian";
        case Scale::lydian: return "Lydian";
        case Scale::locrian: return "Locrian";
        case Scale::custom: return "Custom";
        case Scale::wholeTone: return "Whole tone";
        case Scale::diminished: return "Diminished";
    }
    return "Chromatic";
}

std::pair<int, Scale> detectKey (const std::array<double, 12>& h) noexcept
{
    // Krumhansl & Kessler (1982) probe-tone profiles, C major / C minor
    static constexpr double major[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double minor[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double total = 0.0;
    for (const auto x : h)
        total += std::max (0.0, x);
    if (! (total > 0.0))
        return { -1, Scale::major };
    auto corr = [&h] (const double* prof, int key) {
        double mx = 0.0, mp = 0.0;
        for (int k = 0; k < 12; ++k)
        {
            mx += std::max (0.0, h[(std::size_t) k]);
            mp += prof[k];
        }
        mx /= 12.0;
        mp /= 12.0;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int k = 0; k < 12; ++k)
        {
            const auto x = std::max (0.0, h[(std::size_t) ((k + key) % 12)]) - mx, y = prof[k] - mp;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
        }
        return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : -2.0;
    };
    int bestKey = 0;
    auto bestScale = Scale::major;
    double best = -3.0;
    for (int key = 0; key < 12; ++key)
        for (const auto mode : { Scale::major, Scale::minor })
            if (const auto c = corr (mode == Scale::major ? major : minor, key); c > best + 1e-12)
            {
                best = c;
                bestKey = key;
                bestScale = mode;
            }
    return { bestKey, bestScale };
}

std::uint16_t scaleMask (Scale s, int key, std::uint16_t customMask) noexcept
{
    // intervals above the root, bit k = k semitones
    auto bits = [] (std::initializer_list<int> steps) {
        std::uint16_t m = 0;
        for (auto k : steps)
            m = (std::uint16_t) (m | (1u << k));
        return m;
    };
    std::uint16_t rel = 0xfff;
    switch (s)
    {
        case Scale::chromatic: return 0xfff;
        case Scale::custom: return (std::uint16_t) (customMask & 0xfff);
        case Scale::major: rel = bits ({ 0, 2, 4, 5, 7, 9, 11 }); break;
        case Scale::minor: rel = bits ({ 0, 2, 3, 5, 7, 8, 10 }); break;
        case Scale::harmonicMinor: rel = bits ({ 0, 2, 3, 5, 7, 8, 11 }); break;
        case Scale::melodicMinor: rel = bits ({ 0, 2, 3, 5, 7, 9, 11 }); break;
        case Scale::majorPentatonic: rel = bits ({ 0, 2, 4, 7, 9 }); break;
        case Scale::minorPentatonic: rel = bits ({ 0, 3, 5, 7, 10 }); break;
        case Scale::blues: rel = bits ({ 0, 3, 5, 6, 7, 10 }); break;
        case Scale::dorian: rel = bits ({ 0, 2, 3, 5, 7, 9, 10 }); break;
        case Scale::mixolydian: rel = bits ({ 0, 2, 4, 5, 7, 9, 10 }); break;
        case Scale::phrygian: rel = bits ({ 0, 1, 3, 5, 7, 8, 10 }); break;
        case Scale::lydian: rel = bits ({ 0, 2, 4, 6, 7, 9, 11 }); break;
        case Scale::locrian: rel = bits ({ 0, 1, 3, 5, 6, 8, 10 }); break;
        case Scale::wholeTone: rel = bits ({ 0, 2, 4, 6, 8, 10 }); break;
        case Scale::diminished: rel = bits ({ 0, 2, 3, 5, 6, 8, 9, 11 }); break; // whole-half
    }
    key = ((key % 12) + 12) % 12;
    std::uint16_t m = 0;
    for (int k = 0; k < 12; ++k)
        if (rel & (1u << k))
            m = (std::uint16_t) (m | (1u << ((k + key) % 12)));
    return m;
}

int nearestNote (double note, std::uint16_t mask, int current, double hysteresis) noexcept
{
    mask &= 0xfff;
    auto allowed = [mask] (int n) { return mask == 0 || (mask & (1u << (((n % 12) + 12) % 12))) != 0; };

    if (current >= 0 && allowed (current) && std::abs (note - current) < 0.5 + hysteresis)
        return current;

    const auto base = (int) std::lround (note);
    int best = base;
    double bestD = std::numeric_limits<double>::max();
    for (int d = -6; d <= 6; ++d)
    {
        const auto n = base + d;
        if (! allowed (n))
            continue;
        const auto dist = std::abs (note - n);
        if (dist < bestD - 1e-12)
        {
            bestD = dist;
            best = n;
        }
    }
    return best;
}

double hzToNote (double hz, double a4) noexcept { return 69.0 + 12.0 * std::log2 (hz / a4); }
double noteToHz (double note, double a4) noexcept { return a4 * std::exp2 ((note - 69.0) / 12.0); }

// ---------------------------------------------------------------------------------------------------------------------
// Pitch detector

PitchDetector::PitchDetector()
    : full ((std::size_t) maxFull, 0.0f), deci ((std::size_t) maxDecimated, 0.0f), diff ((std::size_t) maxDecimated, 0.0f)
{
}

void PitchDetector::prepare (double sampleRate, double fminHz, double fmaxHz, int hop) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    fmin = std::clamp (fminHz, 30.0, 1000.0);
    fmax = std::clamp (fmaxHz, fmin * 2.0, 2000.0);
    dec = std::max (1, (int) std::lround (sr / 11025.0));
    const auto fsd = sr / dec;
    tauMax = std::min ((int) std::ceil (fsd / fmin) + 2, maxDecimated / 2 - 8);
    tauMin = std::max (2, (int) std::floor (fsd / fmax));
    window = tauMax;
    hopDec = std::max (1, (int) std::lround ((double) hop / dec));
    guardMemory = std::max (1, (int) std::lround (0.06 * sr / (hopDec * dec)));
    centre = (window + tauMax) / 2 * dec;

    // 4th-order Butterworth low-pass (two biquads, Q 0.5412 / 1.3066) before decimation; RBJ cookbook form
    const auto fc = std::min (0.35 * fsd, 2000.0);
    const double qs[2] { 0.54119610, 1.30656296 };
    for (int s = 0; s < 2; ++s)
    {
        const auto w = 2.0 * pi * fc / sr;
        const auto cw = std::cos (w), alpha = std::sin (w) / (2.0 * qs[s]);
        const auto a0 = 1.0 + alpha;
        auto& b = lp[(std::size_t) s];
        b.b0 = (float) ((1.0 - cw) * 0.5 / a0);
        b.b1 = (float) ((1.0 - cw) / a0);
        b.b2 = b.b0;
        b.a1 = (float) (-2.0 * cw / a0);
        b.a2 = (float) ((1.0 - alpha) / a0);
    }
    reset();
}

void PitchDetector::reset() noexcept
{
    for (auto& b : lp)
        b.z1 = b.z2 = 0.0f;
    std::fill (full.begin(), full.end(), 0.0f);
    std::fill (deci.begin(), deci.end(), 0.0f);
    written = writtenDec = 0;
    phase = hopCount = 0;
    est = {};
    lastTau = 0.0;
    sinceVoiced = 1 << 20;
}

bool PitchDetector::push (float x) noexcept
{
    auto y = lp[1].run (lp[0].run (x));
    if (! std::isfinite (y))
    {
        reset();
        y = 0.0f;
    }
    full[(std::size_t) (written & (maxFull - 1))] = y;
    ++written;

    if (++phase < dec)
        return false;
    phase = 0;
    deci[(std::size_t) (writtenDec & (maxDecimated - 1))] = y;
    ++writtenDec;

    if (++hopCount < hopDec || writtenDec < window + tauMax + 1)
        return false;
    hopCount = 0;
    analyse();
    return true;
}

void PitchDetector::analyse() noexcept
{
    constexpr auto dm = maxDecimated - 1;
    const auto base = writtenDec - (window + tauMax);
    auto at = [&] (std::int64_t j) { return deci[(std::size_t) ((base + j) & dm)]; };

    // YIN steps 2-3: difference function and its cumulative mean normalised form
    double energy = 0.0;
    for (int j = 0; j < window; ++j)
        energy += (double) at (j) * at (j);
    est.level = (float) std::sqrt (energy / window);

    diff[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const auto e = (double) at (j) - at (j + tau);
            d += e * e;
        }
        running += d;
        diff[(std::size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
    }

    // step 4: the first dip under the threshold (then down to its local minimum); otherwise the global minimum
    int tau = -1;
    for (int t = tauMin; t < tauMax; ++t)
        if (diff[(std::size_t) t] < threshold)
        {
            while (t + 1 < tauMax && diff[(std::size_t) t + 1] < diff[(std::size_t) t])
                ++t;
            tau = t;
            break;
        }
    bool found = tau > 0;
    // Round 157 (setHarmonicGuard): YIN's first dip under the threshold can be a harmonic's — her takes on open vowels
    // ([EXEC] REA_rodada157c §3: a strong ~870 Hz formant over a 290 Hz note) read 3x, 2x or 1.5x the sung pitch for a
    // few estimates; the shifter's grains then shrank to a third of a period (a metallic, gritty burst). A dip at 1.5,
    // 2, 3 or 4 times that lag that is clearly deeper wins when it continues the last voiced period (or, at a phrase's
    // start, when it is much deeper). Only a real margin counts: on a clean tone every multiple's dip is ~0.
    if (found && harmonicGuard)
    {
        const auto prev = sinceVoiced <= guardMemory ? lastTau : 0.0;
        const auto first = diff[(std::size_t) tau];
        int bestTau = tau;
        float bestV = first;
        for (const auto m : { 1.5, 2.0, 3.0, 4.0 })
        {
            const auto c = (int) std::lround (m * tau);
            if (c + 3 >= tauMax)
                break;
            int t2 = c - 2;
            for (int t = c - 2; t <= c + 2; ++t)
                if (diff[(std::size_t) t] < diff[(std::size_t) t2])
                    t2 = t;
            const auto v = diff[(std::size_t) t2];
            const auto continues = prev > 0.0 && std::abs (t2 - prev) < 0.12 * prev && std::abs (t2 - prev) < std::abs (tau - prev);
            const auto deeper = v < 0.6f * first && first - v >= 0.03f && v < bestV; // margin: a clean tone's multiples dip as deep
            if (deeper && (continues || (prev <= 0.0 && m >= 2.0 && v < 0.35f * first)))
            {
                bestTau = t2;
                bestV = v;
            }
        }
        tau = bestTau;
    }
    if (! found)
    {
        tau = tauMin;
        for (int t = tauMin + 1; t < tauMax; ++t)
            if (diff[(std::size_t) t] < diff[(std::size_t) tau])
                tau = t;
    }
    est.clarity = std::clamp (1.0f - diff[(std::size_t) tau], 0.0f, 1.0f);
    est.voiced = found && est.level > gate && tau > tauMin && tau < tauMax - 1;
    if (est.voiced)
    {
        lastTau = tau;
        sinceVoiced = 0;
    }
    else if (sinceVoiced < 1 << 20)
        ++sinceVoiced;
    if (! est.voiced)
        return; // keep the last period (the shifter keeps its marks' spacing through short gaps)

    // step 5: parabolic interpolation
    auto refine = [] (double a, double b, double c) {
        const auto den = a - 2.0 * b + c;
        return std::abs (den) > 1e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
    };
    auto period = ((double) tau + refine (diff[(std::size_t) tau - 1], diff[(std::size_t) tau], diff[(std::size_t) tau + 1])) * dec;

    // full-rate refinement: the raw difference function around the coarse period
    if (dec > 1)
    {
        constexpr auto fm = maxFull - 1;
        const auto lo = std::max (2, (int) std::floor (period) - dec - 1);
        const auto hi = (int) std::ceil (period) + dec + 1;
        const auto wf = std::min (window * dec, 2048);
        if (wf + hi + 4 < maxFull && written > wf + hi + centre + 1)
        {
            // centred like the YIN window (so a gliding pitch is read at the same instant)
            const auto start = std::min (written - centre - (wf + (std::int64_t) period) / 2, written - wf - hi - 1);
            auto y = [&] (std::int64_t j) { return full[(std::size_t) ((start + j) & fm)]; };
            std::array<double, 64> d {};
            const auto count = std::min (hi - lo + 1, 64);
            int bestK = 0;
            for (int k = 0; k < count; ++k)
            {
                double s = 0.0;
                for (int j = 0; j < wf; ++j)
                {
                    const auto e = (double) y (j) - y (j + lo + k);
                    s += e * e;
                }
                d[(std::size_t) k] = s;
                if (s < d[(std::size_t) bestK])
                    bestK = k;
            }
            if (bestK > 0 && bestK < count - 1)
                period = lo + bestK + refine (d[(std::size_t) bestK - 1], d[(std::size_t) bestK], d[(std::size_t) bestK + 1]);
        }
    }

    est.period = period;
    est.hz = sr / period;
}

std::vector<PitchFrame> analysePitch (const float* x, int n, double sampleRate, int hop, double fminHz, double fmaxHz, float threshold)
{
    hop = std::max (1, hop);
    std::vector<PitchFrame> frames ((std::size_t) (n / hop + 1));
    if (n <= 0 || x == nullptr)
        return frames;

    PitchDetector det;
    det.prepare (sampleRate, fminHz, fmaxHz, std::max (1, hop / 2));
    det.setThreshold (threshold);
    const auto c = det.windowCentre();

    // estimates at their window centres
    struct At
    {
        std::int64_t t;
        PitchEstimate e;
    };
    std::vector<At> all;
    all.reserve ((std::size_t) (2 * n / std::max (1, hop / 2) + 4));
    for (int i = 0; i < n + c; ++i)
        if (det.push (i < n ? x[i] : 0.0f))
            all.push_back ({ (std::int64_t) i - c, det.estimate() });

    std::size_t k = 0;
    for (std::size_t f = 0; f < frames.size(); ++f)
    {
        const auto t = (std::int64_t) f * hop;
        while (k + 1 < all.size() && std::llabs (all[k + 1].t - t) <= std::llabs (all[k].t - t))
            ++k;
        if (all.empty())
            break;
        const auto& e = all[k].e;
        frames[f].hz = e.voiced ? e.hz : 0.0;
        frames[f].clarity = e.clarity;
        frames[f].level = e.level;
    }
    return frames;
}

// ---------------------------------------------------------------------------------------------------------------------
// TD-PSOLA

Psola::Psola() : env ((std::size_t) ringSize, 0.0f), wsum ((std::size_t) ringSize, 0.0f)
{
    for (int c = 0; c < 2; ++c)
    {
        in[c].assign ((std::size_t) ringSize, 0.0f);
        acc[c].assign ((std::size_t) ringSize, 0.0f);
    }
}

void Psola::prepare (double sampleRate, double maxPeriod, int latency) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxP = std::clamp (maxPeriod, 16.0, ringSize / 8.0);
    delay = std::clamp (latency, 8, ringSize / 2);
    lag = 0;
    unvoicedHop = std::clamp ((int) std::lround (128.0 * sr / 44100.0), 4, std::max (4, (delay - 4) / 2));
    // round 190dp: the shifted unvoiced grains' hop (longer: their joins modulate the sound at the grain rate, and a
    // slower rate keeps that modulation's sidebands next to what made them instead of under a voice's lowest band)
    shiftHop = std::clamp ((int) std::lround (unvoicedShiftHopMs * 0.001 * sr), 4, std::max (4, (delay - 4) / 3));
    reset();
}

void Psola::reset() noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (in[c].begin(), in[c].end(), 0.0f);
        std::fill (acc[c].begin(), acc[c].end(), 0.0f);
    }
    std::fill (env.begin(), env.end(), 0.0f);
    std::fill (wsum.begin(), wsum.end(), 0.0f);
    envZ = 0.0f;
    now = 0;
    nextMark = 0.0;
    nextSynth = 0.0;
    markHead = markCount = 0;
    period = 0.0;
    voiced = false;
    lastVoiced = false;
    lastMark = 0.0;
    ratio = formant = 1.0;
}

void Psola::setMarkLag (int samples) noexcept
{
    lag = std::clamp (samples, 0, std::max (0, delay - (int) std::ceil (1.5 * maxP)));
}

void Psola::setPitch (double p, bool v) noexcept
{
    voiced = v && p >= 2.0 && p <= maxP * 1.5;
    if (voiced)
        period = p;
}

void Psola::setShift (double r, double f) noexcept
{
    ratio = std::clamp (std::isfinite (r) ? r : 1.0, 0.25, 4.0);
    formant = std::clamp (std::isfinite (f) ? f : 1.0, 0.5, 2.0);
}

float Psola::readIn (int ch, double pos) const noexcept
{
    const auto i = (std::int64_t) std::floor (pos);
    if (i - 1 < now - ringSize + 2 || i + 2 > now - 1 || i - 1 < 0)
        return 0.0f;
    const auto t = (float) (pos - (double) i);
    constexpr auto m = ringSize - 1;
    const auto& b = in[ch];
    const auto y0 = b[(std::size_t) ((i - 1) & m)], y1 = b[(std::size_t) (i & m)], y2 = b[(std::size_t) ((i + 1) & m)],
               y3 = b[(std::size_t) ((i + 2) & m)];
    // 4-point Hermite
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void Psola::placeMarks() noexcept
{
    const auto last = (double) (now - 1 - lag);
    // far behind (start, or after a long period): restart the marks at the present
    if (nextMark < last - 2.0 * maxP)
        nextMark = last;

    auto push = [this] (double pos, float p, bool v) {
        if (markCount == maxMarks)
        {
            markHead = (markHead + 1) % maxMarks;
            --markCount;
        }
        marks[(std::size_t) ((markHead + markCount) % maxMarks)] = { pos, p, v, (float) ratio, (float) formant };
        ++markCount;
    };

    if (! voiced || period <= 0.0)
    {
        if (nextMark <= last)
        {
            push (nextMark, (float) unvoicedHop, false);
            nextMark += unvoicedHop;
        }
    }
    else if (peakAlign)
    {
        const auto reach = period * 0.25;
        if (nextMark + reach <= last)
        {
            constexpr auto m = ringSize - 1;
            const auto from = std::max<std::int64_t> ((std::int64_t) std::ceil (nextMark - reach), now - ringSize + 1),
                       to = (std::int64_t) std::floor (nextMark + reach);
            // Round 148: the mark goes where the waveform repeats the one at the previous mark best (the cross-correlation
            // of the low-passed signal over one period, peak refined to a fraction of a sample), not to the highest
            // single peak. [EXEC] (REA_rodada148 §1) the peak in noise moved by a sample or two from period to period:
            // the held note wobbled 4–11 c on low notes with white noise at 20 / 10 dB SNR or breath noise (Auto-Tune
            // Pro: 0.3–4 c); a whole period integrates the noise away, and the fraction of a sample matters on high
            // notes (the integer marks alone left 2 c of jitter). The first mark of a phrase still takes the peak.
            const auto prev = lastMark;
            const auto gap = (double) from - prev;
            const bool corr = prev > 0.0 && gap > 0.5 * period && gap < 1.5 * period && prev - 2.0 * period > (double) (now - ringSize + 2);
            // the period just before each (it is in even with the shortest latency)
            const auto span = (int) period;
            const auto pm = (std::int64_t) std::lround (prev);
            // long periods: every other sample (or fewer) of the window — the low-passed signal is smooth there
            const auto stride = std::max (1, span / 256);
            auto score = [&] (std::int64_t i) {
                if (! corr)
                    return (double) env[(std::size_t) (i & m)];
                double sxy = 0.0;
                for (int j = -span; j <= 0; j += stride)
                    sxy += (double) env[(std::size_t) ((i + j) & m)] * (double) env[(std::size_t) ((pm + j) & m)];
                return sxy;
            };
            // prefer the predicted position a little: ties and flat tops do not make the marks wander
            const auto pen = corr ? 0.0 : 1e-4;
            std::int64_t best = std::clamp<std::int64_t> ((std::int64_t) std::lround (nextMark), from, std::max (from, to));
            auto top = -std::numeric_limits<double>::max();
            for (auto i = from; i <= to; ++i)
            {
                const auto v = score (i) - pen * std::abs ((double) i - nextMark) / period;
                if (v > top)
                {
                    top = v;
                    best = i;
                }
            }
            auto pos = (double) best;
            if (best > from && best < to)
            {
                const auto y0 = score (best - 1), y1 = score (best), y2 = score (best + 1), den = y0 - 2.0 * y1 + y2;
                if (den < 0.0)
                    pos += std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
            }
            if (corr)
                pos += prev - (double) pm; // the lag is measured from the previous mark's own (fractional) position
            push (pos, (float) period, true);
            lastMark = pos;
            nextMark = pos + period;
        }
    }
    else if (nextMark <= last)
    {
        push (nextMark, (float) period, true);
        nextMark += period;
    }

    // forget the marks no grain can use any more
    const auto oldest = (double) (now - 1 - delay) - 2.0 * maxP - 8.0;
    while (markCount > 1 && marks[(std::size_t) markHead].pos < oldest)
    {
        markHead = (markHead + 1) % maxMarks;
        --markCount;
    }
}

void Psola::process (const float* x, float* y, int channels) noexcept
{
    constexpr auto m = ringSize - 1;
    channels = std::clamp (channels, 1, 2);
    for (int c = 0; c < channels; ++c)
        in[c][(std::size_t) (now & m)] = std::isfinite (x[c]) ? x[c] : 0.0f;
    const auto mono = channels == 2 ? 0.5f * (in[0][(std::size_t) (now & m)] + in[1][(std::size_t) (now & m)]) : in[0][(std::size_t) (now & m)];
    // a gentle low-pass (one pole near 900 Hz) of the input: the marks lock to its peaks
    const auto a = (float) (1.0 - std::exp (-2.0 * pi * 900.0 / sr));
    envZ += a * (mono - envZ);
    if (! std::isfinite (envZ))
        envZ = 0.0f;
    env[(std::size_t) (now & m)] = envZ;
    ++now;

    placeMarks();

    const auto e = now - 1 - delay; // the output sample of this call (input timeline)
    const auto last = (double) (now - 1);

    // overlap-add every grain that starts at or before e
    for (int guard = 0; guard < 64; ++guard)
    {
        if (nextSynth < (double) e - maxP)
            nextSynth = (double) e; // fell behind (start): resume at the present
        if (nextSynth - 2.5 * maxP > (double) e)
            break; // no grain can reach e yet

        // the analysis mark nearest the synthesis mark among those whose whole grain is already in
        int best = -1;
        double bestD = std::numeric_limits<double>::max();
        for (int k = 0; k < markCount; ++k)
        {
            const auto& mk = marks[(std::size_t) ((markHead + k) % maxMarks)];
            const auto need = mk.voiced ? mk.pos + mk.period + 2.0 : mk.pos;
            if (need > last)
                continue;
            const auto d = std::abs (mk.pos - nextSynth);
            if (d < bestD)
            {
                bestD = d;
                best = k;
            }
        }

        const Mark* mk = best >= 0 ? &marks[(std::size_t) ((markHead + best) % maxMarks)] : nullptr;
        const auto isVoiced = mk != nullptr && mk->voiced;
        // round 190dp (setUnvoicedShift, off by default): an unvoiced grain is read at the formant factor too — the
        // same window and spacing on the output, a source span `fu` times as long — so noise, breath and what lies
        // above the detector's range move with the formants instead of passing unshifted. fu = 1 = the old grains
        const auto fu = ! isVoiced && unvoicedShift && mk != nullptr ? (double) mk->formant : 1.0;
        const auto f = isVoiced ? (double) mk->formant : fu;
        const auto r = isVoiced ? (double) mk->ratio : 1.0;
        // analysis half-length (a shifted unvoiced grain: its own longer hop, `fu` times as much source), synthesis half-length
        const auto ha = isVoiced ? (double) mk->period : (fu != 1.0 ? (double) shiftHop * fu : (double) unvoicedHop);
        const auto hs = ha / f;
        // a voiced onset starts on its mark: the voice is not displaced in time from the input
        if (isVoiced && ! lastVoiced && mk->pos - hs > (double) e - 1.0)
            nextSynth = mk->pos;
        const auto ts = nextSynth;

        if (ts - hs > (double) e)
            break; // this grain starts later (a voiced onset may have moved it ahead)

        // unvoiced: read around the synthesis time itself (plain overlap-add, transparent); voiced: around the mark
        const auto ta = isVoiced ? mk->pos : ts;
        if (! isVoiced && ts + std::max (hs, ha) + 2.0 > last)
            break; // not in yet (only with a very short latency)

        const auto gain = (float) std::clamp (isVoiced ? f / r : 1.0, 0.25, 4.0);
        const auto o0 = std::max<std::int64_t> ((std::int64_t) std::ceil (ts - hs), e);
        const auto o1 = std::min<std::int64_t> ((std::int64_t) std::floor (ts + hs), e + ringSize - 1);
        const auto invHs = 1.0 / hs;
        if (fu != 1.0)
        {
            // round 190dp: a resampled unvoiced grain overlaps its neighbour with the source `hs · (fu − 1)` apart, which
            // combs noise where the two windows meet (3 ms Hann grains at +2 st: a notch near 1.4 kHz): a flat-topped window whose
            // raised-cosine ramps take only `unvoicedTaper` of the hop keeps the sum at 1 with that overlap only there
            const auto flat = 0.5 * hs * (1.0 - unvoicedTaper), ramp = unvoicedTaper * hs;
            const auto q0 = std::max<std::int64_t> ((std::int64_t) std::ceil (ts - flat - ramp), e);
            const auto q1 = std::min<std::int64_t> ((std::int64_t) std::floor (ts + flat + ramp), e + ringSize - 1);
            for (auto o = q0; o <= q1; ++o)
            {
                const auto d = std::abs ((double) o - ts) - flat;
                if (d >= ramp)
                    continue;
                const auto w = d <= 0.0 ? 1.0f : (float) (0.5 + 0.5 * std::cos (pi * d / ramp));
                const auto src = ta + ((double) o - ts) * f;
                for (int c = 0; c < channels; ++c)
                    acc[c][(std::size_t) (o & m)] += w * readIn (c, src);
                wsum[(std::size_t) (o & m)] += w;
            }
        }
        else
            for (auto o = o0; o <= o1; ++o)
            {
                const auto u = ((double) o - ts) * invHs;
                if (u <= -1.0 || u >= 1.0)
                    continue;
                const auto w = gain * (float) (0.5 + 0.5 * std::cos (pi * u));
                const auto src = ta + ((double) o - ts) * f;
                for (int c = 0; c < channels; ++c)
                    acc[c][(std::size_t) (o & m)] += w * readIn (c, src);
                wsum[(std::size_t) (o & m)] += w;
            }
        // unshifted voiced grains drift toward their marks, so a voice already on its note comes out as the input
        // itself (delayed), not displaced by up to half a period. Round 148: faded in over the last 3.4 cents of
        // shift and at most 0.05 % of a period per grain (~0.9 c) — the on / off switch at 1.7 cents (0.15 %) turned
        // into a 0.5 c wobble on held notes whose correction hovers around zero
        auto hop = isVoiced ? ha / r : hs;
        if (isVoiced && std::abs (f - 1.0) < 1e-3)
        {
            const auto w = std::max (0.0, 1.0 - std::abs (r - 1.0) / 2e-3);
            if (w > 0.0)
                hop += w * std::clamp (0.5 * (ta - ts), -0.0005 * ha, 0.0005 * ha);
        }
        nextSynth = ts + hop;
        lastVoiced = isVoiced;
    }

    for (int c = 0; c < channels; ++c)
    {
        if (e < 0)
        {
            y[c] = 0.0f;
            continue;
        }
        auto& slot = acc[c][(std::size_t) (e & m)];
        y[c] = slot;
        slot = 0.0f;
    }
    // Round 165j (setNormalise): the overlap-add divided by the sum of the grain windows that reached this sample. In a
    // steady note the windows already sum to ~1 (the f / r grain gain); where grains of different lengths or spacings
    // meet — voiced onsets and ends, voicing dropouts, note changes, the switch between voiced and unvoiced grains —
    // the sum dips or bulges and the level with it ([RUN] REA_rodada165j §4: her takes, bursts 40.9 -> 39.4 / min).
    if (e >= 0)
    {
        auto& ws = wsum[(std::size_t) (e & m)];
        if (normalise > 0.0f && ws > 1e-3f)
        {
            const auto g = 1.0f + normalise * (1.0f / std::clamp (ws, 0.5f, 2.0f) - 1.0f);
            for (int c = 0; c < channels; ++c)
                y[c] *= g;
        }
        ws = 0.0f;
    }
}
// ---------------------------------------------------------------------------------------------------------------------
// Breath noise (round 190bb)

void BreathNoise::prepare (double sampleRate) noexcept
{
    static_assert (sections == 4, "the white-noise power below is |H|^(2 * sections)");
    static_assert (lanes >= maxBands && lanes % 4 == 0);
    const auto fs = std::clamp (sampleRate > 0.0 ? sampleRate : 44100.0, 8000.0, 192000.0);
    count = 0;
    for (int b = 0; b < lanes; ++b)
        b0[b] = a1[b] = a2[b] = scale[b] = weight2[b] = 0.0f;
    constexpr double q = 1.875; // per section; four in a row are a third of an octave wide at −3 dB
    for (int b = 0; b < maxBands; ++b)
    {
        const auto fc = 250.0 * std::exp2 ((double) b / 3.0);
        if (fc > 0.42 * fs)
            break;
        // RBJ band-pass, 0 dB at the centre
        const auto w0 = 2.0 * pi * fc / fs, alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
        b0[b] = (float) (alpha / a0);
        a1[b] = (float) (-2.0 * std::cos (w0) / a0);
        a2[b] = (float) ((1.0 - alpha) / a0);
        centre[(std::size_t) b] = fc;
        weight[(std::size_t) b] = std::pow (10.0, std::clamp (6.0 * std::log2 (fc / 2000.0), -18.0, 12.0) / 20.0);
        // the power a unit-variance white noise keeps through the four sections: the mean of |H|^8 over 0 … Nyquist
        const double B0 = b0[b], A1 = a1[b], A2 = a2[b];
        double sum = 0.0;
        constexpr int points = 4096;
        for (int k = 0; k < points; ++k)
        {
            const auto w = pi * ((double) k + 0.5) / (double) points;
            const auto c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
            const auto nr = B0 * (1.0 - c2), ni = B0 * s2;               // b0 (1 − z^−2)
            const auto dr = 1.0 + A1 * c1 + A2 * c2, di = -A1 * s1 - A2 * s2;
            const auto h2 = (nr * nr + ni * ni) / (dr * dr + di * di);
            sum += h2 * h2 * h2 * h2;
        }
        // (the generator below is uniform in −1 … 1: variance 1/3)
        scale[b] = (float) (weight[(std::size_t) b] / std::sqrt (std::max (1e-30, sum / (double) points / 3.0)));
        weight2[b] = (float) (weight[(std::size_t) b] * weight[(std::size_t) b]);
        count = b + 1;
    }
    follow = (float) (1.0 - std::exp (-1.0 / (0.0015 * fs)));
    reset();
}

void BreathNoise::reset() noexcept
{
    for (int k = 0; k < sections; ++k)
        for (int b = 0; b < lanes; ++b)
            v1[k][b] = v2[k][b] = n1[k][b] = n2[k][b] = 0.0f;
    for (auto& p : power)
        p = 0.0f;
    rng = 0x9E3779B9u;
}

float BreathNoise::process (float voice, float gain) noexcept
{
    // nothing non-finite (or absurdly large) may enter the bank: its states would carry it on
    if (! (std::abs (voice) < 1.0e6f))
        voice = 0.0f;
    if (! (gain > 0.0f && gain < 1.0e6f))
        gain = 0.0f;
    alignas (16) float v[lanes], nz[lanes];
    for (int b = 0; b < lanes; ++b)
    {
        v[b] = voice;
        nz[b] = 0.0f;
    }
    for (int b = 0; b < count; ++b) // an independent white noise per band
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        nz[b] = (float) ((double) rng / 2147483647.5 - 1.0);
    }
#if LYNX_BREATH_SIMD
    // four bands at a time (element-wise mul / add / sqrt only: the same samples as the plain loops below)
    const auto fol = _mm_set1_ps (follow), zero = _mm_setzero_ps();
    auto sumP = zero, sumW = zero;
    for (int b = 0; b < lanes; b += 4)
    {
        const auto B0 = _mm_load_ps (b0 + b), A1 = _mm_load_ps (a1 + b), A2 = _mm_load_ps (a2 + b);
        auto x = _mm_load_ps (v + b), w = _mm_load_ps (nz + b);
        for (int k = 0; k < sections; ++k)
        {
            auto bx = _mm_mul_ps (B0, x);
            auto y = _mm_add_ps (bx, _mm_load_ps (v1[k] + b));
            _mm_store_ps (v1[k] + b, _mm_add_ps (_mm_mul_ps (_mm_sub_ps (zero, A1), y), _mm_load_ps (v2[k] + b)));
            _mm_store_ps (v2[k] + b, _mm_sub_ps (_mm_sub_ps (zero, bx), _mm_mul_ps (A2, y)));
            x = y;
            bx = _mm_mul_ps (B0, w);
            y = _mm_add_ps (bx, _mm_load_ps (n1[k] + b));
            _mm_store_ps (n1[k] + b, _mm_add_ps (_mm_mul_ps (_mm_sub_ps (zero, A1), y), _mm_load_ps (n2[k] + b)));
            _mm_store_ps (n2[k] + b, _mm_sub_ps (_mm_sub_ps (zero, bx), _mm_mul_ps (A2, y)));
            w = y;
        }
        auto pw = _mm_load_ps (power + b);
        pw = _mm_add_ps (pw, _mm_mul_ps (fol, _mm_sub_ps (_mm_mul_ps (x, x), pw)));
        _mm_store_ps (power + b, pw);
        sumP = _mm_add_ps (sumP, pw);
        sumW = _mm_add_ps (sumW, _mm_mul_ps (_mm_load_ps (weight2 + b), pw));
        _mm_store_ps (nz + b, _mm_mul_ps (w, _mm_mul_ps (_mm_load_ps (scale + b), exactSqrt (_mm_max_ps (zero, pw)))));
    }
    alignas (16) float sp[4], sw[4];
    _mm_store_ps (sp, sumP);
    _mm_store_ps (sw, sumW);
#else
    float sp[4] {}, sw[4] {};
    for (int k = 0; k < sections; ++k)
    {
        auto* z1 = v1[k];
        auto* z2 = v2[k];
        for (int b = 0; b < lanes; ++b)
        {
            const auto bx = b0[b] * v[b], y = bx + z1[b];
            z1[b] = (0.0f - a1[b]) * y + z2[b];
            z2[b] = (0.0f - bx) - a2[b] * y;
            v[b] = y;
        }
        z1 = n1[k];
        z2 = n2[k];
        for (int b = 0; b < lanes; ++b)
        {
            const auto bx = b0[b] * nz[b], y = bx + z1[b];
            z1[b] = (0.0f - a1[b]) * y + z2[b];
            z2[b] = (0.0f - bx) - a2[b] * y;
            nz[b] = y;
        }
    }
    for (int b = 0; b < lanes; ++b)
    {
        power[b] += follow * (v[b] * v[b] - power[b]);
        nz[b] *= scale[b] * std::sqrt (std::max (0.0f, power[b]));
        sp[b & 3] += power[b];
        sw[b & 3] += weight2[b] * power[b];
    }
#endif
    float out = 0.0f;
    for (int b = 0; b < count; ++b)
        out += nz[b];
    // never louder than the voice: where the tilt would lift the noise over the voice's own power (a bright sound taken
    // for a voiced one: cymbals in a mix, a voiced fricative) the whole noise is turned down to it
    const auto voicePower = (sp[0] + sp[1]) + (sp[2] + sp[3]);
    const auto noisePower = gain * gain * ((sw[0] + sw[1]) + (sw[2] + sw[3]));
    if (noisePower > voicePower)
        gain *= std::sqrt (std::max (0.0f, voicePower) / noisePower);
    const auto y = gain * out;
    return std::isfinite (y) ? y : 0.0f; // (… nor leave it)
}

void BreathNoise::tidy() noexcept
{
    auto flush = [] (float& x) { if (! (std::abs (x) > 1e-25f)) x = 0.0f; };
    for (int b = 0; b < lanes; ++b)
    {
        for (int k = 0; k < sections; ++k)
        {
            flush (v1[k][b]);
            flush (v2[k][b]);
        }
        flush (power[b]);
    }
}
} // namespace lynx::vocal
