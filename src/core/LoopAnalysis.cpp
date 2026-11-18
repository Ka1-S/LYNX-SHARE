#include "core/LoopAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <sstream>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif defined(__APPLE__)
 #include <pthread.h>
#elif defined(__linux__)
 #include <sys/resource.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

#include "core/AudioEditTools.h"
#include "core/Instruments.h"
#include "core/PreviewTempo.h"
#include "core/TempoDetect.h"

namespace lynx::loopanalysis
{
namespace
{
constexpr double pi = 3.141592653589793;

// --- tempo -------------------------------------------------------------------------------------------------------------
// Onset strength per band (Scheirer 1998, "Tempo and beat analysis of acoustic musical signals", JASA 103: band
// envelopes; Klapuri 1999, "Sound onset detection by applying psychoacoustic knowledge", ICASSP: the half-wave
// rectified difference of the log energy). Bands from one-pole low-passes: below ~150 Hz (kick), ~150 Hz–2 kHz (body:
// snare, chords), above ~2 kHz (hats); and a "beat" band below ~300 Hz (kick and snare body) that decides which pulse
// level is the beat. Each curve has its local mean (~0.4 s) removed, as tempodetect::onsetCurve does.
struct Bands
{
    std::array<std::vector<float>, 4> flux; // low, mid, high, beat
    // the rise of each hop's energy (not its log): what is loud counts — the kick (below ~150 Hz) and the snare or clap
    // (above ~1 kHz, where the kick has little)
    std::vector<float> kick, snare;
    double hopSeconds = 0.0;
};

Bands bandFlux (const SampleData& audio, std::size_t frames)
{
    Bands out;
    const auto rate = audio.sampleRate;
    const auto hop = (std::size_t) std::max<long long> (16, std::llround (0.005 * rate));
    out.hopSeconds = (double) hop / rate;
    const auto hops = frames / hop;
    if (hops < 8)
        return out;
    const auto* l = audio.leftData();
    const auto* r = audio.isStereo() ? audio.rightData() : nullptr;
    const auto coefficient = [rate] (double hz) { return (float) (1.0 - std::exp (-2.0 * pi * hz / rate)); };
    const auto aLow = coefficient (150.0), aMid = coefficient (2000.0), aBeat = coefficient (300.0), aSnare = coefficient (1000.0);
    float low = 0.0f, mid = 0.0f, beat = 0.0f, snareLow = 0.0f;
    float previous[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    double lastLow = 0.0, lastRest = 0.0;
    for (auto& f : out.flux)
        f.reserve (hops);
    out.kick.reserve (hops), out.snare.reserve (hops);
    for (std::size_t h = 0; h < hops; ++h)
    {
        double e[4] = { 0.0, 0.0, 0.0, 0.0 }, eSnare = 0.0;
        for (std::size_t i = h * hop, end = i + hop; i < end; ++i)
        {
            const auto x = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
            low += aLow * (x - low);
            mid += aMid * (x - mid);
            beat += aBeat * (x - beat);
            snareLow += aSnare * (x - snareLow);
            const float b[4] = { low, mid - low, x - mid, beat };
            for (int k = 0; k < 4; ++k)
                e[k] += (double) b[k] * b[k];
            eSnare += (double) (x - snareLow) * (x - snareLow);
        }
        for (int k = 0; k < 4; ++k)
        {
            const auto level = (float) std::log (1.0e-10 + e[k] / (double) hop);
            out.flux[(std::size_t) k].push_back (h > 0 ? std::max (0.0f, level - previous[k]) : 0.0f);
            previous[k] = level;
        }
        const auto lowNow = e[0] / (double) hop, restNow = eSnare / (double) hop;
        out.kick.push_back ((float) std::max (0.0, lowNow - lastLow));
        out.snare.push_back ((float) std::max (0.0, restNow - lastRest));
        lastLow = lowNow, lastRest = restNow;
    }
    const auto half = std::max<std::size_t> (1, (std::size_t) std::llround (0.2 / out.hopSeconds));
    for (auto& curve : out.flux)
    {
        std::vector<double> prefix (curve.size() + 1, 0.0);
        for (std::size_t i = 0; i < curve.size(); ++i)
            prefix[i + 1] = prefix[i] + curve[i];
        std::vector<float> o (curve.size());
        for (std::size_t i = 0; i < curve.size(); ++i)
        {
            const auto a = i > half ? i - half : 0, b = std::min (curve.size(), i + half + 1);
            o[i] = std::max (0.0f, curve[i] - (float) ((prefix[b] - prefix[a]) / (double) (b - a)));
        }
        curve = std::move (o);
    }
    return out;
}

// a short triangular blur (±15 ms) so pulses a fraction of a hop apart still meet
std::vector<float> blur (const std::vector<float>& in, double hopSeconds)
{
    const auto radius = std::max<int> (1, (int) std::lround (0.015 / hopSeconds));
    std::vector<float> out (in.size(), 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        double sum = 0.0, weights = 0.0;
        for (int k = -radius; k <= radius; ++k)
        {
            const auto j = (long long) i + k;
            if (j < 0 || j >= (long long) in.size())
                continue;
            const auto w = (double) (radius + 1 - std::abs (k));
            sum += w * in[(std::size_t) j];
            weights += w;
        }
        out[i] = (float) (sum / weights);
    }
    return out;
}

// the curve's value at a fractional index, read around the loop when `circular`
double sampleAt (const std::vector<float>& c, double x, bool circular)
{
    const auto n = (double) c.size();
    if (circular)
        x = std::fmod (std::fmod (x, n) + n, n);
    if (x < 0.0 || x > n - 1.0)
        return 0.0;
    const auto i = (std::size_t) x;
    const auto f = x - (double) i;
    const auto j = std::min (c.size() - 1, i + 1);
    return c[i] * (1.0 - f) + c[j] * f;
}

// How strongly a pulse of `period` hops (best phase) is in `curve`: the mean of the curve on the pulse's places minus its
// mean over all (Percival & Tzanetakis 2014, "Streamlined tempo estimation based on autocorrelation and
// cross-correlation with pulses", IEEE/ACM TASLP 22(12): candidates scored by cross-correlation with pulse trains).
struct Pulse
{
    double strength = 0.0, phase = 0.0;
};
Pulse pulseStrength (const std::vector<float>& curve, double period, bool circular)
{
    Pulse best;
    if (curve.empty() || period < 2.0)
        return best;
    const auto mean = std::accumulate (curve.begin(), curve.end(), 0.0) / (double) curve.size();
    const auto steps = std::max (8, (int) std::ceil (period));
    for (int s = 0; s < steps; ++s)
    {
        const auto phase = period * s / steps;
        double sum = 0.0;
        int count = 0;
        for (auto x = phase; x <= (double) curve.size() - 1.0; x += period)
            sum += sampleAt (curve, x, circular), ++count;
        if (count == 0)
            continue;
        if (const auto v = sum / count - mean; v > best.strength)
            best = { v, phase };
    }
    return best;
}

// The pulse comb: the autocorrelation of the onset curve (around the loop when `circular`) summed over each period's
// first four multiples with weights 1/k (as tempodetect::detect; Davies & Plumbley 2007, "Context-dependent beat
// tracking of musical audio", IEEE TASLP 15(3): a comb over the autocorrelation), with no tempo preference of its own.
struct Comb
{
    std::vector<double> r;
    double hopSeconds = 0.0;

    Comb (const std::vector<float>& curve, double hop, double minBpm, bool circular) : hopSeconds (hop)
    {
        const auto n = curve.size();
        constexpr int harmonics = 4;
        const auto maxLag = (std::size_t) std::ceil (harmonics * 60.0 / (minBpm * hop)) + 2;
        r.assign (std::min (maxLag + 1, circular ? n : (n > maxLag + 8 ? maxLag + 1 : 0)), 0.0);
        for (std::size_t lag = 1; lag < r.size(); ++lag)
        {
            double sum = 0.0;
            if (circular)
                for (std::size_t i = 0; i < n; ++i)
                    sum += (double) curve[i] * curve[(i + n - lag) % n];
            else
                for (std::size_t i = lag; i < n; ++i)
                    sum += (double) curve[i] * curve[i - lag];
            r[lag] = sum / (double) (circular ? n : n - lag);
        }
    }
    double at (double lag) const
    {
        const auto i = (std::size_t) lag;
        if (i + 1 >= r.size())
            return 0.0;
        const auto f = lag - (double) i;
        return r[i] * (1.0 - f) + r[i + 1] * f;
    }
    double score (double bpm) const
    {
        const auto period = 60.0 / (bpm * hopSeconds);
        double s = 0.0;
        for (int k = 1; k <= 4; ++k)
            s += at (period * k) / (double) k;
        return s;
    }
};

// the fine tempo: the correlation of the curve with itself over up to 16 periods (a long lag reads the tempo far more
// precisely than one period; as tempodetect::detect's refinement), searched within ±1 % of `bpm`
double refine (const std::vector<float>& curve, double hopSeconds, double bpm, bool circular)
{
    const auto n = curve.size();
    const auto direct = [&] (double lag) {
        const auto whole = (std::size_t) lag;
        const auto f = (float) (lag - (double) whole);
        if (whole + 2 >= n)
            return 0.0;
        double sum = 0.0;
        if (circular)
            for (std::size_t i = 0; i < n; ++i)
                sum += (double) curve[i] * (curve[(i + n - whole) % n] * (1.0f - f) + curve[(i + 2 * n - whole - 1) % n] * f);
        else
            for (std::size_t i = whole + 1; i < n; ++i)
                sum += (double) curve[i] * (curve[i - whole] * (1.0f - f) + curve[i - whole - 1] * f);
        return sum / (double) (circular ? n : n - whole);
    };
    const auto period = 60.0 / (bpm * hopSeconds);
    const auto periods = std::clamp ((int) ((double) n * (circular ? 1.0 : 0.5) / period), 1, 16);
    const auto fine = [&] (double b) {
        const auto p = 60.0 / (b * hopSeconds);
        double s = 0.0;
        for (int k = 1; k <= periods; ++k)
            s += direct (p * k);
        return s;
    };
    auto best = bpm, bestScore = fine (bpm);
    for (int step = -50; step <= 50; ++step)
        if (const auto b = bpm * (1.0 + 0.0002 * step); step != 0)
            if (const auto v = fine (b); v > bestScore)
                bestScore = v, best = b;
    return best;
}
} // namespace

Tempo detectTempo (const SampleData& audio)
{
    Tempo out;
    const auto& o = tempoOptions();
    const auto rate = audio.sampleRate;
    if (! (rate > 0.0) || audio.frames() < (std::size_t) (2.0 * rate))
        return out;
    const auto frames = std::min (audio.frames(), (std::size_t) (maxSeconds * rate));
    const auto seconds = (double) audio.frames() / rate;
    const auto bands = bandFlux (audio, frames);
    if (bands.flux[0].size() < 16)
        return out;
    const auto hopSeconds = bands.hopSeconds;
    const auto n = bands.flux[0].size();
    std::vector<float> all (n);
    for (std::size_t i = 0; i < n; ++i)
        all[i] = bands.flux[0][i] + bands.flux[1][i] + bands.flux[2][i];
    all = blur (all, hopSeconds);
    if (std::inner_product (all.begin(), all.end(), all.begin(), 0.0) <= 1.0e-9)
        return out;

    // a file short enough to be a loop is read as one: its curve wraps around (its last beat leads to its first), so
    // every lag of the autocorrelation uses the whole file
    const bool circular = seconds <= maxSeconds && o.circular;
    const auto minBpm = o.minBpm, maxBpm = o.maxBpm;
    const Comb comb (all, hopSeconds, 50.0, circular);
    if (comb.r.empty())
        return out;

    // 1. the pulse: the best of the comb over 50–220 BPM
    double best = 0.0, bestScore = 0.0, mean = 0.0;
    int count = 0;
    for (auto b = 50.0; b <= 220.0; b *= 1.0005)
    {
        const auto v = comb.score (b);
        mean += v, ++count;
        if (v > bestScore)
            bestScore = v, best = b;
    }
    mean /= std::max (1, count);
    if (bestScore <= 0.0)
        return out;
    out.confidence = std::clamp ((bestScore - mean) / bestScore, 0.0, 1.0);
    auto bpm = best;

    // 2. a loop is cut to whole bars: the tempos that make the file 1, 2, 4, 8 or 16 bars of four beats (or 3, 6, 12
    //    bars, or 2 beats) are tried, each by the comb with a preference for powers of two of bars; one that the comb
    //    finds nearly as strong as the free pulse decides the pulse's family (a 3:4 or 2:3 relation the comb alone gets
    //    wrong: dotted rhythms, busy hats). LYNX's own use of the loop's length; the length test is as tempodetect's.
    if (o.loopCandidates && seconds <= maxSeconds)
    {
        static constexpr std::pair<int, double> counts[] = { { 4, 1.0 },  { 8, 1.0 },   { 16, 1.0 },  { 32, 1.0 },  { 64, 1.0 },
                                                             { 12, 0.9 }, { 24, 0.9 },  { 48, 0.9 },  { 2, 0.85 }, { 6, 0.8 } };
        double loopBest = 0.0, loopScore = 0.0;
        for (const auto& [beats, prior] : counts)
        {
            const auto b = 60.0 * beats / seconds;
            if (b < 50.0 || b > 220.0)
                continue;
            if (const auto v = comb.score (b) * prior; v > loopScore)
                loopScore = v, loopBest = b;
        }
        if (loopBest > 0.0 && loopScore >= o.loopShare * bestScore)
        {
            bpm = loopBest;
            // a pulse the loop's own length agrees with is trusted more (chords changing once a bar give a weak comb)
            if (loopScore >= 0.9 * bestScore)
                out.confidence = std::max (out.confidence, o.loopConfidence);
        }
    }

    // 3. the beat level among the pulse's octaves. Drums: the backbeat — the level at which the loud hits above the kick
    //    (the snare, the clap) fall on beats 2 and 4 of each pair of beats, between the kicks on 1 and 3 (the
    //    convention tempos are written in; a half-time groove with its snare on 3 reads at half its written tempo).
    //    Without such a backbeat (no drums, or only hats): the octave the comb finds strongest under a mild preference
    //    for the middle of the range (as tempodetect::detect; the perceived-tempo resonance near 120 BPM of Moelants &
    //    McKinney 2004, "Tempo perception and musical content", ICMPC).
    const auto period = [hopSeconds] (double b) { return 60.0 / (b * hopSeconds); };
    const auto kick = blur (bands.kick, hopSeconds), snare = blur (bands.snare, hopSeconds);
    const auto meanOf = [] (const std::vector<float>& c) { return c.empty() ? 0.0 : std::accumulate (c.begin(), c.end(), 0.0) / (double) c.size(); };
    const auto snareMean = meanOf (snare), kickMean = meanOf (kick);
    // the backbeat's strength at beat level b: the snare on the off-beats of the kick's two-beat pulse against the snare
    // on its beats (0 when there is none)
    const auto backbeat = [&] (double b) {
        const auto p = period (b);
        const auto bar = pulseStrength (kick, 2.0 * p, circular);
        double on = 0.0, off = 0.0, kickOn = 0.0, kickOff = 0.0;
        int nOn = 0, nOff = 0;
        for (auto x = bar.phase; x <= (double) n - 1.0; x += 2.0 * p)
        {
            on += sampleAt (snare, x, circular), kickOn += sampleAt (kick, x, circular), ++nOn;
            off += sampleAt (snare, x + p, circular), kickOff += sampleAt (kick, x + p, circular), ++nOff;
        }
        if (nOn == 0 || nOff == 0 || snareMean <= 0.0 || kickMean <= 0.0)
            return 0.0;
        on /= nOn, off /= nOff, kickOn /= nOn, kickOff /= nOff;
        // the off-beats must hold clear snare hits (well above the snare curve's mean) and beat the on-beats, and the
        // kicks must fall on the on-beats rather than the off-beats (melodies and chords above 1 kHz are not a backbeat)
        if (! (off > o.backbeatLevel * snareMean && off > o.backbeatRatio * on))
            return 0.0;
        if (o.kickRatio > 0.0 && ! (kickOn > o.kickLevel * kickMean && kickOn > o.kickRatio * kickOff))
            return 0.0;
        return off / snareMean;
    };
    const auto prior = [&o] (double b) {
        const auto octaves = std::log2 (b / o.priorCentre);
        return std::exp (-0.5 * octaves * octaves / (o.priorSigma * o.priorSigma));
    };
    if (o.octaveWalk)
    {
        std::vector<double> family;
        for (auto b = bpm; b >= minBpm * 0.99; b /= 2.0)
            family.push_back (b);
        for (auto b = bpm * 2.0; b <= maxBpm * 1.01; b *= 2.0)
            family.push_back (b);
        double chosen = 0.0, strongest = 0.0;
        for (const auto b : family)
            if (b >= minBpm * 0.99 && b <= maxBpm * 1.01)
                if (const auto v = backbeat (b); v > strongest)
                    strongest = v, chosen = b;
        // no backbeat: the octave tempodetect::detect (FL's "Detect tempo" in LYNX) reads, when it found this pulse
        if (chosen <= 0.0 && o.detectorOctave)
        {
            const auto legacy = tempodetect::detect (audio.leftData(), audio.isStereo() ? audio.rightData() : nullptr, frames, rate).bpm;
            for (const auto b : family)
                if (legacy > 0.0 && b >= minBpm * 0.99 && b <= maxBpm * 1.01 && std::abs (legacy / b - 1.0) < 0.02)
                    chosen = b;
        }
        if (chosen <= 0.0)
        {
            double bestPrior = -1.0;
            for (const auto b : family)
                if (b >= minBpm * 0.99 && b <= maxBpm * 1.01)
                    if (const auto v = comb.score (b) * prior (b); v > bestPrior)
                        bestPrior = v, chosen = b;
        }
        if (chosen > 0.0)
            bpm = chosen;
    }
    while (bpm < minBpm)
        bpm *= 2.0;
    while (bpm > maxBpm)
        bpm /= 2.0;

    // 4. the exact tempo: refined on many periods; a loop of whole beats takes the tempo its length gives
    const auto fine = refine (all, hopSeconds, bpm, circular);
    out.bpm = fine;
    const auto beats = seconds * fine / 60.0;
    const auto whole = std::round (beats);
    if (whole >= 2.0 && std::abs (beats - whole) <= o.wholeTolerance * std::max (1.0, whole / 8.0))
        out.bpm = whole * 60.0 / seconds;
    out.beats = seconds * out.bpm / 60.0;
    return out;
}

// --- key ---------------------------------------------------------------------------------------------------------------
namespace
{
// Key profiles. Krumhansl & Kessler 1982 ("Tracing the dynamic changes in perceived tonal organization in a spatial
// representation of musical keys", Psychological Review 89) — what keyfollow::detectKey uses; Temperley 2007 ("Music
// and Probability", MIT Press: the Kostka–Payne corpus profiles); Sapp 2011 ("Computational methods for the analysis
// of musical structure", PhD thesis, Stanford: the "simple" weights).
struct Profile
{
    std::array<double, 12> major, minor;
};
const Profile& profile (int which)
{
    static const Profile kk { { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 },
                              { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 } };
    static const Profile temperley { { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 },
                                     { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 } };
    static const Profile simple { { 2, 0, 1, 0, 1, 1, 0, 2, 0, 1, 0, 1 }, { 2, 0, 1, 1, 0, 1, 0, 2, 1, 0, 0.5, 0.5 } };
    return which == 1 ? temperley : which == 2 ? simple : kk;
}

double correlate (const std::array<double, 12>& x, const std::array<double, 12>& p, int root)
{
    double mx = 0, my = 0;
    for (int i = 0; i < 12; ++i)
        mx += x[(std::size_t) i], my += p[(std::size_t) i];
    mx /= 12.0, my /= 12.0;
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < 12; ++i)
    {
        const auto a = x[(std::size_t) ((i + root) % 12)] - mx, b = p[(std::size_t) i] - my;
        sxy += a * b, sxx += a * a, syy += b * b;
    }
    return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
}

int relativeMajorRoot (const keyfollow::LoopKey& k) noexcept { return (k.root + (k.minor ? 3 : 0)) % 12; }
} // namespace

KeyOptions& keyOptions()
{
    static KeyOptions o;
    return o;
}

// A pitch-class profile (chroma) of spectral peaks (Fujishima 1999, "Realtime chord recognition of musical sound",
// ICMC: the pitch class profile; Gómez 2006, "Tonal description of music audio signals", PhD thesis, UPF: the harmonic
// pitch class profile — each peak also counts for the notes it may be a harmonic of, with a falling weight — and key
// estimation by profile correlation), scored against key profiles on the 12 roots. The bass (below ~200 Hz) is also
// kept as its own profile and added with a weight, since a loop's bass mostly plays the chords' roots (Pauws 2004,
// "Musical key extraction from audio", ISMIR: a low-frequency emphasis for the tonic).
keyfollow::Detection detectKey (const SampleData& audio)
{
    const auto& o = keyOptions();
    keyfollow::Detection out;
    out.chroma.assign (12, 0.0f);
    const auto rate = audio.sampleRate > 0.0 ? audio.sampleRate : 44100.0;
    const auto total = std::min<std::size_t> (audio.frames(), (std::size_t) (rate * 30.0));
    constexpr int n = 8192, hop = 4096;
    if (total < (std::size_t) n)
        return out;
    const auto* l = audio.leftData();
    const auto* r = audio.isStereo() ? audio.rightData() : nullptr;
    std::vector<double> window (n);
    for (int i = 0; i < n; ++i)
        window[(std::size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / (n - 1));
    const auto lo = std::max (2, (int) std::floor (o.lowHz * n / rate));
    const auto hi = std::min (n / 2 - 2, (int) std::ceil (o.highHz * n / rate));
    // the magnitude spectrogram of the bins that are read (with a margin for the neighbourhoods)
    const auto first = std::max (1, lo - 26), last = std::min (n / 2 - 1, hi + 26);
    const auto width = (std::size_t) (last - first + 1);
    std::vector<std::vector<float>> frames;
    std::vector<double> re (n), im (n);
    for (std::size_t start = 0; start + (std::size_t) n <= total; start += (std::size_t) hop)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto x = r != nullptr ? 0.5 * ((double) l[start + (std::size_t) i] + (double) r[start + (std::size_t) i]) : (double) l[start + (std::size_t) i];
            re[(std::size_t) i] = x * window[(std::size_t) i];
            im[(std::size_t) i] = 0.0;
        }
        audioedit::fftInPlace (re, im, false);
        std::vector<float> m (width);
        for (std::size_t k = 0; k < width; ++k)
            m[k] = (float) std::hypot (re[(std::size_t) first + k], im[(std::size_t) first + k]);
        frames.push_back (std::move (m));
    }
    // harmonic / percussive separation by median filtering (Fitzgerald 2010, "Harmonic/percussive separation using
    // median filtering", DAFx-10): a bin is kept where its median over neighbouring frames (sustained tones) is at least
    // its median over neighbouring bins (hits, noise); drums then leave few peaks
    if (o.hpss && frames.size() >= 3)
    {
        const auto t = (int) frames.size();
        std::vector<std::vector<float>> harmonic (frames.size(), std::vector<float> (width));
        std::vector<float> buf;
        for (int f = 0; f < t; ++f)
            for (std::size_t k = 0; k < width; ++k)
            {
                buf.clear();
                for (int g = std::max (0, f - o.hpssFrames); g <= std::min (t - 1, f + o.hpssFrames); ++g)
                    buf.push_back (frames[(std::size_t) g][k]);
                std::nth_element (buf.begin(), buf.begin() + (std::ptrdiff_t) (buf.size() / 2), buf.end());
                harmonic[(std::size_t) f][k] = buf[buf.size() / 2];
            }
        for (int f = 0; f < t; ++f)
        {
            auto& row = frames[(std::size_t) f];
            std::vector<float> masked (width);
            for (std::size_t k = 0; k < width; ++k)
            {
                buf.clear();
                for (std::size_t j = k >= 8 ? k - 8 : 0; j <= std::min (width - 1, k + 8); ++j)
                    buf.push_back (row[j]);
                std::nth_element (buf.begin(), buf.begin() + (std::ptrdiff_t) (buf.size() / 2), buf.end());
                const auto h = harmonic[(std::size_t) f][k], pmed = buf[buf.size() / 2];
                masked[k] = h >= pmed ? row[k] : 0.0f;
            }
            row = std::move (masked);
        }
    }
    std::array<double, 12> chroma {}, bass {}, start {};
    const auto startFrames = std::max<std::size_t> (1, (std::size_t) std::lround (o.startSeconds * rate / hop));
    std::vector<int> chords; // each sounding frame's three strongest pitch classes (a bit mask): one held chord sets no key
    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex)
    {
        const auto& row = frames[frameIndex];
        const auto mag = [&row, first] (int k) { return (double) row[(std::size_t) (k - first)]; };
        const auto before = chroma;
        for (int k = lo; k <= hi; ++k)
        {
            const auto m = mag (k);
            if (m <= 0.0 || m <= mag (k - 1) || m < mag (k + 1))
                continue;
            double local = 0.0;
            int count = 0;
            for (int j = std::max (first, k - 24); j <= std::min (last, k + 24); ++j)
                if (std::abs (j - k) > 2)
                    local += mag (j), ++count;
            local /= std::max (1, count);
            if (m < 4.0 * local)
                continue;
            const auto a = mag (k - 1), c = mag (k + 1);
            const auto denom = a - 2.0 * m + c;
            const auto delta = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
            const auto freq = ((double) k + delta) * rate / n;
            const auto midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
            auto w = m - local;
            if (o.compress)
                w = std::sqrt (w);
            const auto pc = ((int) std::lround (midi) % 12 + 12) % 12;
            chroma[(std::size_t) pc] += w;
            // the harmonic pitch class profile: the peak may be harmonic 2, 3, 4 of a lower note
            if (o.harmonics > 1)
            {
                static constexpr int shift[5] = { 0, 0, 12, 19, 24 }; // semitones below for harmonic 1..4
                for (int h = 2; h <= o.harmonics && h <= 4; ++h)
                {
                    const auto below = ((int) std::lround (midi) - shift[h]) % 12;
                    chroma[(std::size_t) ((below + 12) % 12)] += w * std::pow (o.harmonicWeight, h - 1);
                }
            }
            if (freq < o.bassHz)
                bass[(std::size_t) pc] += w;
            if (frameIndex < startFrames)
                start[(std::size_t) pc] += w;
        }
        std::array<double, 12> added {};
        double addedSum = 0.0;
        for (int i = 0; i < 12; ++i)
            addedSum += (added[(std::size_t) i] = chroma[(std::size_t) i] - before[(std::size_t) i]);
        if (addedSum > 0.0)
        {
            int mask = 0;
            for (int pick = 0; pick < 3; ++pick)
            {
                const auto top = (int) (std::max_element (added.begin(), added.end()) - added.begin());
                if (added[(std::size_t) top] <= 0.0)
                    break;
                mask |= 1 << top;
                added[(std::size_t) top] = -1.0;
            }
            if (std::find (chords.begin(), chords.end(), mask) == chords.end())
                chords.push_back (mask);
        }
    }
    double sum = 0.0, bassSum = 0.0;
    for (int i = 0; i < 12; ++i)
        sum += chroma[(std::size_t) i], bassSum += bass[(std::size_t) i];
    if (sum <= 0.0)
        return out;
    std::array<double, 12> x {};
    for (int i = 0; i < 12; ++i)
    {
        x[(std::size_t) i] = chroma[(std::size_t) i] / sum + (bassSum > 0.0 ? o.bassWeight * bass[(std::size_t) i] / bassSum : 0.0);
        out.chroma[(std::size_t) i] = (float) (chroma[(std::size_t) i] / sum);
    }
    const auto& p = profile (o.profile);
    struct Score
    {
        keyfollow::LoopKey key;
        double r;
    };
    std::vector<Score> scores;
    for (int root = 0; root < 12; ++root)
    {
        scores.push_back ({ { root, false }, correlate (x, p.major, root) });
        scores.push_back ({ { root, true }, correlate (x, p.minor, root) });
    }
    std::sort (scores.begin(), scores.end(), [] (const Score& a, const Score& b) { return a.r > b.r; });
    const auto& best = scores.front();
    double other = -1.0;
    for (const auto& s : scores)
        if (relativeMajorRoot (s.key) != relativeMajorRoot (best.key))
        {
            other = s.r;
            break;
        }
    out.correlation = (float) best.r;
    out.margin = (float) (best.r - other);
    int strong = 0;
    for (auto v : out.chroma)
        strong += v >= 0.05f ? 1 : 0;
    if (best.r >= o.minCorrelation && best.r - other >= o.minMargin && strong >= o.minPitchClasses && (int) chords.size() >= o.minChords)
        out.key = best.key;
    // the major key or its relative minor (the same notes; profiles barely tell them apart): the tonic's weight with its
    // fifth decides, counted over the whole loop, its bass and its first moments (a loop starts on its downbeat, which
    // mostly holds the tonic chord) — the tonic-and-fifth test of LYNX's own key guess (core/KeyGuess.h, round 184a)
    if (out.key && o.tonicDecision)
    {
        double startSum = 0.0;
        for (auto v : start)
            startSum += v;
        std::array<double, 12> t {};
        for (int i = 0; i < 12; ++i)
            t[(std::size_t) i] = chroma[(std::size_t) i] / sum + (bassSum > 0.0 ? o.tonicBass * bass[(std::size_t) i] / bassSum : 0.0)
                                 + (startSum > 0.0 ? o.tonicStart * start[(std::size_t) i] / startSum : 0.0);
        const auto majorRoot = relativeMajorRoot (*out.key), minorRoot = (majorRoot + 9) % 12;
        const auto tonic = [&t] (int root) { return t[(std::size_t) root] + 0.5 * t[(std::size_t) ((root + 7) % 12)]; };
        out.key = tonic (minorRoot) > tonic (majorRoot) ? keyfollow::LoopKey { minorRoot, true } : keyfollow::LoopKey { majorRoot, false };
    }
    return out;
}

Result analyse (const SampleData& audio, const std::string& fileName, bool useNameAndAcid)
{
    Result out;
    if (audio.frames() == 0 || ! (audio.sampleRate > 0.0))
        return out;
    out.seconds = (double) audio.frames() / audio.sampleRate;
    if (out.seconds > maxSeconds)
        return out;
    if (useNameAndAcid)
        if (const auto quick = previewtempo::quick (audio, fileName); quick.bpm > 0.0)
        {
            out.bpm = quick.bpm;
            out.tempoConfidence = 1.0;
            out.tempoSource = quick.source == previewtempo::Source::acid ? TempoSource::acid : TempoSource::name;
        }
    if (out.bpm <= 0.0 && out.seconds >= previewtempo::minDetectSeconds)
    {
        const auto t = detectTempo (audio);
        if (t.bpm > 0.0 && t.confidence >= tempoOptions().minConfidence)
        {
            out.bpm = t.bpm;
            out.tempoConfidence = t.confidence;
            out.tempoSource = TempoSource::detected;
        }
    }
    if (out.bpm > 0.0)
    {
        out.beats = out.seconds * out.bpm / 60.0;
        out.wholeBeats = out.beats >= 3.9 && std::abs (out.beats - std::round (out.beats)) <= 0.04;
    }
    if (useNameAndAcid)
        if (const auto k = keyfollow::keyInName (fileName))
        {
            out.key = k;
            out.keyConfidence = 1.0f;
            out.keySource = KeySource::name;
        }
    if (! out.key)
    {
        const auto d = detectKey (audio);
        if (d.key)
        {
            out.key = d.key;
            out.keyConfidence = std::clamp (d.margin * 5.0f, 0.0f, 1.0f);
            out.keySource = KeySource::detected;
        }
    }
    return out;
}

TempoOptions& tempoOptions()
{
    static TempoOptions o;
    return o;
}

std::string tempoText (const Result& r)
{
    if (! (r.bpm > 0.0))
        return {};
    const auto whole = std::abs (r.bpm - std::round (r.bpm)) < 0.05;
    char buf[32];
    std::snprintf (buf, sizeof (buf), whole ? "%.0f BPM" : "%.1f BPM", r.bpm);
    return buf;
}

std::string keyText (const Result& r, bool shortForm)
{
    if (! r.key)
        return {};
    return projectkey::rootName (r.key->root) + (shortForm ? (r.key->minor ? " min" : " maj") : (r.key->minor ? " minor" : " major"));
}

Cache& Cache::shared()
{
    static Cache cache;
    return cache;
}

std::optional<Result> Cache::find (const Stamp& stamp) const
{
    const std::scoped_lock l (lock);
    if (const auto it = entries.find (stamp.path); it != entries.end() && it->second.first == stamp)
        return it->second.second;
    return std::nullopt;
}

std::optional<Result> Cache::findPath (const std::string& path) const
{
    const std::scoped_lock l (lock);
    if (const auto it = entries.find (path); it != entries.end())
        return it->second.second;
    return std::nullopt;
}

std::vector<std::pair<Stamp, Result>> Cache::all() const
{
    const std::scoped_lock l (lock);
    std::vector<std::pair<Stamp, Result>> out;
    out.reserve (entries.size());
    for (const auto& path : order)
        if (const auto it = entries.find (path); it != entries.end())
            out.push_back (it->second);
    return out;
}

void Cache::put (const Stamp& stamp, const Result& result)
{
    const std::scoped_lock l (lock);
    if (entries.find (stamp.path) == entries.end())
        order.push_back (stamp.path);
    entries[stamp.path] = { stamp, result };
    if (entries.size() > capacity)
    {
        const auto drop = order.size() / 2;
        for (std::size_t i = 0; i < drop; ++i)
            entries.erase (order[i]);
        order.erase (order.begin(), order.begin() + (std::ptrdiff_t) drop);
    }
}

std::size_t Cache::size() const
{
    const std::scoped_lock l (lock);
    return entries.size();
}

void Cache::clear()
{
    const std::scoped_lock l (lock);
    entries.clear();
    order.clear();
}

std::string toText (const Stamp& stamp, const Result& r)
{
    if (stamp.path.find_first_of ("\t\r\n") != std::string::npos)
        return {};
    char buf[256];
    std::snprintf (buf, sizeof (buf), "\t%lld\t%lld\t%.4f\t%.4f\t%.3f\t%.4f\t%d\t%d\t%d\t%d\t%.3f\t%d", (long long) stamp.size, (long long) stamp.modified,
                   r.seconds, r.bpm, r.tempoConfidence, r.beats, r.wholeBeats ? 1 : 0, (int) r.tempoSource, r.key ? r.key->root : -1,
                   r.key && r.key->minor ? 1 : 0, (double) r.keyConfidence, (int) r.keySource);
    return stamp.path + buf;
}

bool fromText (const std::string& line, Stamp& stamp, Result& r)
{
    const auto tab = line.find ('\t');
    if (tab == std::string::npos || tab == 0)
        return false;
    stamp.path = line.substr (0, tab);
    std::istringstream in (line.substr (tab + 1));
    long long size = 0, modified = 0;
    int whole = 0, tempoSource = 0, key = -1, minor = 0, keySource = 0;
    double keyConfidence = 0.0;
    if (! (in >> size >> modified >> r.seconds >> r.bpm >> r.tempoConfidence >> r.beats >> whole >> tempoSource >> key >> minor >> keyConfidence >> keySource))
        return false;
    if (tempoSource < 0 || tempoSource > 3 || keySource < 0 || keySource > 2 || key > 11)
        return false;
    stamp.size = size, stamp.modified = modified;
    r.wholeBeats = whole != 0;
    r.tempoSource = (TempoSource) tempoSource;
    r.key = key >= 0 ? std::optional<keyfollow::LoopKey> (keyfollow::LoopKey { key, minor != 0 }) : std::nullopt;
    r.keyConfidence = (float) keyConfidence;
    r.keySource = r.key ? (KeySource) keySource : KeySource::none;
    return true;
}

// --- the worker ----------------------------------------------------------------------------------------------------
namespace
{
void lowestPriority() noexcept
{
   #if defined(_WIN32)
    if (! SetThreadPriority (GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN))
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_IDLE);
   #elif defined(__APPLE__)
    pthread_set_qos_class_self_np (QOS_CLASS_BACKGROUND, 0);
   #elif defined(__linux__)
    setpriority (PRIO_PROCESS, (id_t) syscall (SYS_gettid), 19);
   #endif
}
} // namespace

Service::Service (Callbacks cb) : callbacks (std::move (cb))
{
    worker = std::thread ([this] { run(); });
}

Service::~Service()
{
    {
        const std::lock_guard<std::mutex> hold (lock);
        stopping = true;
        queue.clear();
    }
    wake.notify_all();
    if (worker.joinable())
        worker.join();
}

bool Service::stampOf (const std::string& path, Stamp& out) const
{
    out.path = path;
    return callbacks.stat && callbacks.stat (path, out.size, out.modified);
}

std::optional<Result> Service::find (const std::string& path) const
{
    Stamp stamp;
    if (! stampOf (path, stamp))
        return std::nullopt;
    return Cache::shared().find (stamp);
}

std::size_t Service::request (const std::vector<std::string>& paths)
{
    std::size_t added = 0;
    {
        const std::lock_guard<std::mutex> hold (lock);
        if (stopping)
            return 0;
        // the newest request goes first, in its own order: put it at the front, back to front
        for (auto it = paths.rbegin(); it != paths.rend(); ++it)
        {
            if (const auto q = std::find (queue.begin(), queue.end(), *it); q != queue.end())
                queue.erase (q);
            queue.push_front (*it);
            ++added;
        }
        while (queue.size() > maxQueue)
            queue.pop_back();
    }
    wake.notify_all();
    return added;
}

std::size_t Service::queued() const
{
    const std::lock_guard<std::mutex> hold (lock);
    return queue.size();
}

std::size_t Service::done() const
{
    const std::lock_guard<std::mutex> hold (lock);
    return analysedCount;
}

bool Service::waitIdle (int timeoutMs)
{
    std::unique_lock<std::mutex> hold (lock);
    return idleWake.wait_for (hold, std::chrono::milliseconds (timeoutMs), [this] { return queue.empty() && ! working; });
}

void Service::run()
{
    lowestPriority();
    for (;;)
    {
        std::string path;
        {
            std::unique_lock<std::mutex> hold (lock);
            working = false;
            idleWake.notify_all();
            wake.wait (hold, [this] { return stopping || ! queue.empty(); });
            if (stopping)
                return;
            path = std::move (queue.front());
            queue.pop_front();
            working = true;
        }
        // recording or rendering: wait (bounded steps, so closing stays quick)
        while (callbacks.shouldPause && callbacks.shouldPause())
        {
            std::unique_lock<std::mutex> hold (lock);
            wake.wait_for (hold, std::chrono::milliseconds (250), [this] { return stopping; });
            if (stopping)
                return;
        }
        Stamp stamp;
        if (! stampOf (path, stamp))
            continue; // gone
        if (Cache::shared().find (stamp))
        {
            if (callbacks.analysed) // analysed already (this session, or an earlier one's saved result)
                callbacks.analysed (path);
            continue;
        }
        const auto started = std::chrono::steady_clock::now();
        SampleData audio;
        double total = 0.0;
        Result result;
        // one second more than a loop may last: a longer file is a song or a take (its length is all that is kept)
        if (callbacks.decode && callbacks.decode (path, maxSeconds + 1.0, audio, total))
        {
            const auto slash = path.find_last_of ("/\\");
            const auto name = slash == std::string::npos ? path : path.substr (slash + 1);
            if (total > maxSeconds)
                result.seconds = total;
            else
                result = analyse (audio, name);
        }
        Cache::shared().put (stamp, result);
        {
            const std::lock_guard<std::mutex> hold (lock);
            ++analysedCount;
        }
        if (callbacks.analysed)
            callbacks.analysed (path);
        if (callbacks.throttle)
        {
            const auto worked = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
            std::unique_lock<std::mutex> hold (lock);
            wake.wait_for (hold, std::chrono::duration<double> (std::clamp (worked, 0.002, 0.25)), [this] { return stopping; });
        }
    }
}
} // namespace lynx::loopanalysis
