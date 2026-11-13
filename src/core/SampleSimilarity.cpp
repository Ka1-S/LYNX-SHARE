#include "core/SampleSimilarity.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <utility>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif defined(__APPLE__)
 #include <pthread.h>
 #include <pthread/qos.h>
#elif defined(__linux__)
 #include <sys/resource.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

namespace lynx::similarity
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int melBands = 26;
constexpr int mfccCount = 12;

// The layout of the vector (featureCount = 49)
constexpr int fMfccMean = 0, fMfccDev = 12, fCentre = 24, fCentreDev = 25, fFlatness = 26, fLength = 27, fAttack = 28, fEnergyCentre = 29,
              fDecay = 30, fSustain = 31, fOnsets = 32, fOnsetRate = 33, fRegular = 34, fPeriod = 35, fChroma = 36, fTonal = 48;

// The weight of each dimension: a group's weight shared by its dimensions (tuned on FL's packs: the share of the
// 10 nearest files that sit in the query's own folder, tests/Similar190asTests.inc and the REA notes)
struct Group
{
    int first, count;
    float weight;
};
constexpr Group groups[] = {
    { fMfccMean, 12, 1.0f }, // tone
    { fMfccDev, 12, 0.2f },  // how the tone moves
    { fCentre, 3, 0.5f },    // brightness, noisiness
    { fLength, 5, 3.0f },    // shape in time
    { fOnsets, 4, 0.4f },    // rhythm (kept above what the drum-heavy tuning set asks for: loops)
    { fChroma, 12, 0.15f },  // pitch classes (the same)
    { fTonal, 1, 0.05f },
};

Features makeWeights()
{
    Features w {};
    for (const auto& g : groups)
        for (int i = 0; i < g.count; ++i)
            w[(std::size_t) (g.first + i)] = g.weight / (float) g.count;
    return w;
}

void fft (std::vector<std::complex<float>>& a)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto angle = -2.0 * pi / (double) len;
        const std::complex<float> step ((float) std::cos (angle), (float) std::sin (angle));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
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

double hzToMel (double hz) { return 2595.0 * std::log10 (1.0 + hz / 700.0); }

void put32 (std::string& s, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        s.push_back ((char) ((v >> (8 * i)) & 0xff));
}
void put64 (std::string& s, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        s.push_back ((char) ((v >> (8 * i)) & 0xff));
}
bool get32 (const std::string& s, std::size_t& at, std::uint32_t& v)
{
    if (at + 4 > s.size())
        return false;
    v = 0;
    for (int i = 0; i < 4; ++i)
        v |= (std::uint32_t) (unsigned char) s[at + (std::size_t) i] << (8 * i);
    at += 4;
    return true;
}
bool get64 (const std::string& s, std::size_t& at, std::uint64_t& v)
{
    if (at + 8 > s.size())
        return false;
    v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (std::uint64_t) (unsigned char) s[at + (std::size_t) i] << (8 * i);
    at += 8;
    return true;
}

// The worker never competes with the audio or the UI: the lowest priority the system has
void lowerThreadPriority() noexcept
{
   #if defined(_WIN32)
    if (! SetThreadPriority (GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)) // (also lowers its disk priority)
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_IDLE);
   #elif defined(__APPLE__)
    pthread_set_qos_class_self_np (QOS_CLASS_BACKGROUND, 0);
   #elif defined(__linux__)
    setpriority (PRIO_PROCESS, (id_t) syscall (SYS_gettid), 19);
   #endif
}

constexpr char magic[] = "LYNXSIM1";
} // namespace

bool analyse (const float* mono, std::size_t frames, double sampleRate, Features& out)
{
    out.fill (0.0f);
    if (mono == nullptr || frames < 16 || ! (sampleRate >= 8000.0) || sampleRate > 768000.0)
        return false;

    float peak = 0.0f;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto a = std::abs (mono[i]);
        if (std::isfinite (a))
            peak = std::max (peak, a);
    }
    if (peak < 1.0e-4f)
        return false;

    // the sound itself: from the first sample above −60 dB of the peak, at most analysedSeconds, to the last one
    const auto floorLevel = peak * 0.001f;
    std::size_t start = 0;
    while (start < frames && ! (std::abs (mono[start]) > floorLevel))
        ++start;
    auto n = std::min (frames - start, (std::size_t) (sampleRate * analysedSeconds));
    while (n > 1 && ! (std::abs (mono[start + n - 1]) > floorLevel))
        --n;
    if (n < 16)
        return false;
    const auto gain = 1.0f / peak;
    const auto sample = [&] (std::size_t i) {
        const auto v = i < n ? mono[start + i] * gain : 0.0f;
        return std::isfinite (v) ? v : 0.0f;
    };

    std::size_t size = 256;
    while ((double) size < sampleRate * 0.04)
        size *= 2;
    const auto hop = size / 4;
    const auto frameCount = n > size ? 1 + (n - size + hop - 1) / hop : (std::size_t) 1;
    const auto bins = size / 2 + 1;
    const auto binHz = sampleRate / (double) size;

    // triangular mel filters, 30 Hz .. 16 kHz (the bands above half the sample rate stay empty)
    std::vector<int> bandOf (bins, -2); // -2: outside the filters; -1: only on the rise of band 0
    std::vector<float> bandUp (bins, 0.0f); // the bin's share of band bandOf+1 (the rest goes to bandOf)
    {
        const auto lo = hzToMel (30.0), hi = hzToMel (16000.0);
        const auto step = (hi - lo) / (double) (melBands + 1);
        for (std::size_t k = 0; k < bins; ++k)
        {
            const auto mel = hzToMel ((double) k * binHz);
            const auto pos = (mel - lo) / step - 1.0; // 0 = the centre of band 0
            if (pos <= -1.0 || pos >= (double) melBands)
                continue;
            const auto below = (int) std::floor (pos);
            bandOf[k] = below;
            bandUp[k] = (float) (pos - (double) below);
        }
    }
    std::vector<int> pitchClass (bins, -1);
    for (std::size_t k = 1; k < bins; ++k)
    {
        const auto hz = (double) k * binHz;
        if (hz >= 65.0 && hz <= 2100.0)
            pitchClass[k] = ((int) std::lround (12.0 * std::log2 (hz / 440.0)) + 69 + 1200) % 12;
    }
    std::vector<float> window (size);
    for (std::size_t i = 0; i < size; ++i)
        window[i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) size));
    float cosTable[mfccCount][melBands];
    for (int c = 0; c < mfccCount; ++c)
        for (int b = 0; b < melBands; ++b)
            cosTable[c][b] = (float) std::cos (pi * (double) (c + 1) * ((double) b + 0.5) / (double) melBands);

    std::vector<std::complex<float>> buffer (size);
    std::vector<float> flux (frameCount, 0.0f);
    float previous[melBands];
    std::fill (std::begin (previous), std::end (previous), -8.0f);
    double weightSum = 0.0, mfccSum[mfccCount] = {}, mfccSquares[mfccCount] = {}, centreSum = 0.0, centreSquares = 0.0, flatSum = 0.0;
    double chroma[12] = {};

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        for (std::size_t i = 0; i < size; ++i)
            buffer[i] = { sample (f * hop + i) * window[i], 0.0f };
        fft (buffer);

        float mel[melBands] = {};
        double energy = 0.0, moment = 0.0;
        for (std::size_t k = 1; k < bins; ++k)
        {
            const auto p = std::norm (buffer[k]);
            energy += p;
            moment += p * (double) k;
            if (const auto b = bandOf[k]; b >= -1)
            {
                if (b >= 0)
                    mel[b] += p * (1.0f - bandUp[k]);
                if (b + 1 < melBands)
                    mel[b + 1] += p * bandUp[k];
            }
            if (pitchClass[k] >= 0)
                chroma[pitchClass[k]] += p;
        }
        float logMel[melBands];
        double logSum = 0.0, linear = 0.0;
        const auto scale = 1.0f / (float) (size * size);
        for (int b = 0; b < melBands; ++b)
        {
            const auto m = mel[b] * scale;
            logMel[b] = std::log10 (m + 1.0e-8f);
            logSum += std::log ((double) m + 1.0e-10);
            linear += (double) m;
            flux[f] += std::max (0.0f, logMel[b] - previous[b]);
            previous[b] = logMel[b];
        }
        const auto weight = std::sqrt (energy) / (double) size; // the frame's level: quiet tails count little
        if (! (weight > 1.0e-6))
            continue;
        weightSum += weight;
        for (int c = 0; c < mfccCount; ++c)
        {
            float v = 0.0f;
            for (int b = 0; b < melBands; ++b)
                v += logMel[b] * cosTable[c][b];
            mfccSum[c] += weight * (double) v;
            mfccSquares[c] += weight * (double) v * (double) v;
        }
        const auto centre = std::log2 (std::max (20.0, moment / energy * binHz) / 1000.0);
        centreSum += weight * centre;
        centreSquares += weight * centre * centre;
        flatSum += weight * (std::exp (logSum / (double) melBands) / (linear / (double) melBands + 1.0e-10));
    }
    if (! (weightSum > 0.0))
        return false;

    for (int c = 0; c < mfccCount; ++c)
    {
        const auto mean = mfccSum[c] / weightSum;
        out[(std::size_t) (fMfccMean + c)] = (float) mean;
        out[(std::size_t) (fMfccDev + c)] = (float) std::sqrt (std::max (0.0, mfccSquares[c] / weightSum - mean * mean));
    }
    const auto centre = centreSum / weightSum;
    out[fCentre] = (float) centre;
    out[fCentreDev] = (float) std::sqrt (std::max (0.0, centreSquares / weightSum - centre * centre));
    out[fFlatness] = (float) (flatSum / weightSum);

    // shape: the level every 2 ms
    const auto seconds = (double) n / sampleRate;
    {
        const auto step = std::max<std::size_t> (1, (std::size_t) (sampleRate * 0.002));
        const auto count = (n + step - 1) / step;
        std::vector<float> level (count, 0.0f);
        for (std::size_t i = 0; i < n; ++i)
            level[i / step] = std::max (level[i / step], std::abs (sample (i)));
        std::size_t top = 0, first10 = count, first90 = count, last10 = 0;
        double sum = 0.0, moment = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (level[i] > level[top])
                top = i;
            sum += level[i];
            moment += level[i] * (double) i;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            if (level[i] >= 0.1f && first10 == count)
                first10 = i;
            if (level[i] >= 0.9f && first90 == count)
                first90 = i;
            if (level[i] >= 0.1f)
                last10 = i;
        }
        const auto stepSeconds = (double) step / sampleRate;
        out[fLength] = (float) std::log10 (seconds + 0.001);
        out[fAttack] = (float) std::log10 ((double) (first90 > first10 && first90 < count ? first90 - first10 : 0) * stepSeconds + 0.001);
        out[fEnergyCentre] = (float) (sum > 0.0 ? moment / sum / (double) count : 0.5);
        out[fDecay] = (float) std::log10 ((double) (last10 > top ? last10 - top : 0) * stepSeconds + 0.001);
        out[fSustain] = (float) (sum / (double) count);
    }

    // rhythm: onsets = peaks of the spectral rise; regularity = how well the rise repeats (60..300 per minute)
    {
        const auto frameSeconds = (double) hop / sampleRate;
        double mean = 0.0, squares = 0.0;
        for (const auto v : flux)
        {
            mean += v;
            squares += (double) v * v;
        }
        mean /= (double) frameCount;
        const auto deviation = std::sqrt (std::max (0.0, squares / (double) frameCount - mean * mean));
        const auto threshold = mean + deviation;
        const auto gap = std::max<std::size_t> (1, (std::size_t) (0.06 / frameSeconds));
        int onsets = 0;
        std::size_t lastOnset = 0;
        for (std::size_t i = 0; i < frameCount; ++i)
        {
            if (flux[i] < threshold || flux[i] <= 0.0f)
                continue;
            bool isPeak = true;
            for (std::size_t j = i > 3 ? i - 3 : 0; j < std::min (frameCount, i + 4) && isPeak; ++j)
                isPeak = flux[j] <= flux[i];
            if (isPeak && (onsets == 0 || i - lastOnset >= gap))
            {
                ++onsets;
                lastOnset = i;
            }
        }
        onsets = std::max (onsets, 1);
        out[fOnsets] = (float) std::log2 ((double) onsets);
        out[fOnsetRate] = (float) std::log2 (1.0 + (double) onsets / std::max (seconds, 0.05));

        double regular = 0.0, period = 0.0;
        if (seconds >= 1.0 && deviation > 0.0)
        {
            const auto minLag = std::max<std::size_t> (1, (std::size_t) (0.2 / frameSeconds));
            const auto maxLag = std::min ((std::size_t) (1.0 / frameSeconds), frameCount / 2);
            const auto zero = deviation * deviation * (double) frameCount;
            for (auto lag = minLag; lag <= maxLag; ++lag)
            {
                double r = 0.0;
                for (std::size_t i = 0; i + lag < frameCount; ++i)
                    r += ((double) flux[i] - mean) * ((double) flux[i + lag] - mean);
                r /= zero;
                if (r > regular)
                {
                    regular = r;
                    period = std::log2 ((double) lag * frameSeconds);
                }
            }
        }
        out[fRegular] = (float) std::min (1.0, regular);
        out[fPeriod] = (float) (regular > 0.15 ? period : -3.0);
    }

    // pitch classes: their share of the energy, and how much one stands out
    {
        double total = 0.0, top = 0.0;
        for (const auto c : chroma)
            total += c;
        for (int i = 0; i < 12; ++i)
        {
            const auto share = total > 0.0 ? chroma[i] / total : 1.0 / 12.0;
            out[(std::size_t) (fChroma + i)] = (float) share;
            top = std::max (top, share);
        }
        out[fTonal] = (float) top;
    }

    for (const auto v : out)
        if (! std::isfinite (v))
            return false;
    return true;
}

int percent (float distance) noexcept
{
    if (! (distance >= 0.0f))
        return 0;
    return (int) std::lround (100.0 * std::exp (-(double) distance / 3.0));
}

// --- the index --------------------------------------------------------------------------------------------------------

bool Index::put (Entry entry)
{
    if (const auto it = byPath.find (entry.path); it != byPath.end())
    {
        auto& old = entries[it->second];
        valid += (entry.valid ? 1u : 0u);
        valid -= (old.valid ? 1u : 0u);
        old = std::move (entry);
        scaleDirty = true;
        return true;
    }
    if (entries.size() >= maxIndexedFiles)
        return false;
    valid += (entry.valid ? 1u : 0u);
    byPath.emplace (entry.path, entries.size());
    entries.push_back (std::move (entry));
    scaleDirty = true;
    return true;
}

const Entry* Index::find (const std::string& path) const
{
    const auto it = byPath.find (path);
    return it != byPath.end() ? &entries[it->second] : nullptr;
}

bool Index::remove (const std::string& path)
{
    const auto it = byPath.find (path);
    if (it == byPath.end())
        return false;
    const auto at = it->second;
    valid -= (entries[at].valid ? 1u : 0u);
    byPath.erase (it);
    if (at + 1 != entries.size())
    {
        entries[at] = std::move (entries.back());
        byPath[entries[at].path] = at;
    }
    entries.pop_back();
    scaleDirty = true;
    return true;
}

int Index::prune (const std::string& folder, const std::vector<std::string>& names, char separator)
{
    auto prefix = folder;
    if (prefix.empty() || prefix.back() != separator)
        prefix.push_back (separator);
    std::vector<std::string> gone;
    for (const auto& e : entries)
    {
        if (e.path.size() <= prefix.size() || e.path.compare (0, prefix.size(), prefix) != 0)
            continue;
        const auto name = e.path.substr (prefix.size());
        if (name.find (separator) != std::string::npos)
            continue; // in a subfolder
        if (std::find (names.begin(), names.end(), name) == names.end())
            gone.push_back (e.path);
    }
    for (const auto& path : gone)
        remove (path);
    return (int) gone.size();
}

void Index::clear()
{
    entries.clear();
    byPath.clear();
    valid = 0;
    scaleDirty = true;
}

void Index::updateScale() const
{
    if (! std::exchange (scaleDirty, false))
        return;
    std::array<double, featureCount> sum {}, squares {};
    for (const auto& e : entries)
    {
        if (! e.valid)
            continue;
        for (std::size_t d = 0; d < (std::size_t) featureCount; ++d)
        {
            sum[d] += e.features[d];
            squares[d] += (double) e.features[d] * e.features[d];
        }
    }
    for (std::size_t d = 0; d < (std::size_t) featureCount; ++d)
    {
        const auto m = valid > 0 ? sum[d] / (double) valid : 0.0;
        mean[d] = (float) m;
        const auto dev = valid > 1 ? std::sqrt (std::max (0.0, squares[d] / (double) valid - m * m)) : 0.0;
        deviation[d] = (float) std::max (dev, 1.0e-3);
    }
}

std::vector<Match> Index::rank (const Features& query, const std::string& exclude, const std::vector<std::string>& roots, std::size_t max) const
{
    static const Features weights = makeWeights();
    updateScale();
    Features scale {};
    for (std::size_t d = 0; d < (std::size_t) featureCount; ++d)
        scale[d] = weights[d] / (deviation[d] * deviation[d]);

    std::vector<std::pair<float, std::size_t>> scored;
    scored.reserve (valid);
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (! e.valid || e.path == exclude)
            continue;
        if (! roots.empty() && std::none_of (roots.begin(), roots.end(), [&] (const std::string& r) { return search::Index::isUnder (e.path, r); }))
            continue;
        float distance = 0.0f;
        for (std::size_t d = 0; d < (std::size_t) featureCount; ++d)
        {
            const auto delta = query[d] - e.features[d];
            distance += scale[d] * delta * delta;
        }
        scored.emplace_back (distance, i);
    }
    const auto count = std::min (max, scored.size());
    std::partial_sort (scored.begin(), scored.begin() + (std::ptrdiff_t) count, scored.end(), [&] (const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : entries[a.second].path < entries[b.second].path;
    });
    std::vector<Match> matches;
    matches.reserve (count);
    for (std::size_t i = 0; i < count; ++i)
        matches.push_back ({ entries[scored[i].second].path, scored[i].first });
    return matches;
}

std::string Index::toBytes() const
{
    std::string s (magic, 8);
    put32 (s, (std::uint32_t) featureCount);
    put32 (s, (std::uint32_t) entries.size());
    for (const auto& e : entries)
    {
        put32 (s, (std::uint32_t) e.path.size());
        s += e.path;
        put64 (s, (std::uint64_t) e.size);
        put64 (s, (std::uint64_t) e.time);
        s.push_back (e.valid ? 1 : 0);
        if (e.valid)
            for (const auto v : e.features)
            {
                std::uint32_t bits;
                std::memcpy (&bits, &v, 4);
                put32 (s, bits);
            }
    }
    return s;
}

bool Index::fromBytes (const std::string& bytes)
{
    clear();
    if (bytes.size() < 16 || bytes.compare (0, 8, magic, 8) != 0)
        return false;
    std::size_t at = 8;
    std::uint32_t features = 0, count = 0;
    if (! get32 (bytes, at, features) || ! get32 (bytes, at, count) || features != (std::uint32_t) featureCount)
        return false;
    for (std::uint32_t i = 0; i < count && entries.size() < maxIndexedFiles; ++i)
    {
        Entry e;
        std::uint32_t length = 0;
        std::uint64_t size = 0, time = 0;
        if (! get32 (bytes, at, length) || length > 32768 || at + length > bytes.size())
            break; // a cut file: what was read stays
        e.path.assign (bytes, at, length);
        at += length;
        if (! get64 (bytes, at, size) || ! get64 (bytes, at, time) || at >= bytes.size())
            break;
        e.size = (std::int64_t) size;
        e.time = (std::int64_t) time;
        e.valid = bytes[at++] != 0;
        if (e.valid)
        {
            bool ok = true;
            for (auto& v : e.features)
            {
                std::uint32_t bits = 0;
                if (! get32 (bytes, at, bits))
                {
                    ok = false;
                    break;
                }
                std::memcpy (&v, &bits, 4);
                ok = ok && std::isfinite (v);
            }
            if (! ok)
                break;
        }
        put (std::move (e));
    }
    return true;
}

// --- the worker --------------------------------------------------------------------------------------------------------

Service::Service (Callbacks cb) : callbacks (std::move (cb))
{
    asker = std::thread ([this] { answer(); });
    worker = std::thread ([this] { run(); });
}

Service::~Service()
{
    {
        const std::lock_guard<std::mutex> hold (lock);
        stopping = true;
        pending.reset();
    }
    wake.notify_all();
    if (asker.joinable())
        asker.join();
    if (worker.joinable())
        worker.join();
    saveIndex(); // (both threads are gone: what either of them analysed is kept)
}

void Service::refresh (std::vector<std::string> roots)
{
    if (roots.empty())
        return;
    {
        const std::lock_guard<std::mutex> hold (lock);
        std::reverse (roots.begin(), roots.end()); // (a stack: the first root is walked first)
        passes.push_back (std::move (roots));
        state.busy = true; // (at once: an answer given before the worker starts is already "partial")
    }
    wake.notify_all();
}

std::uint64_t Service::findSimilar (std::string path, std::vector<std::string> roots, std::size_t max, std::function<void (const Result&)> done)
{
    std::uint64_t id = 0;
    {
        const std::lock_guard<std::mutex> hold (lock);
        id = ++latest;
        pending = Query { id, std::move (path), std::move (roots), max, std::move (done) };
    }
    wake.notify_all();
    return id;
}

void Service::cancel()
{
    {
        const std::lock_guard<std::mutex> hold (lock);
        ++latest;
        pending.reset();
    }
    wake.notify_all(); // (a pass waiting for that query goes on)
    idleWake.notify_all();
}

Status Service::status() const
{
    const std::lock_guard<std::mutex> hold (lock);
    return state;
}

bool Service::idleLocked() const
{
    return ! loading && ! pending && ! answering && ! passRunning && passes.empty();
}

bool Service::waitIdle (int timeoutMs)
{
    std::unique_lock<std::mutex> hold (lock);
    return idleWake.wait_for (hold, std::chrono::milliseconds (timeoutMs), [this] { return idleLocked(); });
}

void Service::setStatus (const std::function<void (Status&)>& change, bool notify)
{
    {
        const std::lock_guard<std::mutex> hold (lock);
        change (state);
    }
    if (notify && callbacks.statusChanged)
        callbacks.statusChanged();
}

bool Service::interrupted() const
{
    const std::lock_guard<std::mutex> hold (lock);
    return stopping;
}

void Service::rest (double workedSeconds)
{
    if (! callbacks.throttle)
        return;
    std::unique_lock<std::mutex> hold (lock);
    wake.wait_for (hold, std::chrono::duration<double> (std::clamp (workedSeconds, 0.002, 0.5)), [this] { return stopping; });
}

void Service::loadIndex()
{
    const auto bytes = callbacks.load ? callbacks.load() : std::string();
    std::size_t files = 0;
    {
        const std::lock_guard<std::mutex> hold (indexLock);
        if (! bytes.empty())
            index.fromBytes (bytes);
        files = index.validCount();
    }
    setStatus ([files] (Status& s) {
        s.loaded = true;
        s.files = files;
    });
}

void Service::saveIndex()
{
    if (! dirty.exchange (false) || ! callbacks.save)
        return;
    std::string bytes;
    {
        const std::lock_guard<std::mutex> hold (indexLock);
        bytes = index.toBytes();
    }
    callbacks.save (bytes);
}

bool Service::analyseFile (const std::string& path, Entry& entry)
{
    entry = {};
    entry.path = path;
    if (! callbacks.stat || ! callbacks.stat (path, entry.size, entry.time))
        return false; // no such file: nothing to remember
    if (entry.size <= maxFileBytes && callbacks.decode)
    {
        std::vector<float> mono;
        double rate = 0.0;
        // (a little more than is analysed: the leading silence is skipped)
        if (callbacks.decode (path, analysedSeconds + 2.0, mono, rate))
            entry.valid = analyse (mono.data(), mono.size(), rate, entry.features);
    }
    return true;
}

void Service::runQuery (Query& query)
{
    Result result;
    result.id = query.id;
    result.query = query.path;

    std::int64_t size = 0, time = 0;
    const auto exists = callbacks.stat && callbacks.stat (query.path, size, time);
    std::optional<Entry> known;
    {
        const std::lock_guard<std::mutex> hold (indexLock);
        if (const auto* e = index.find (query.path))
            known = *e;
    }
    if (exists && (! known || known->size != size || known->time != time))
    {
        Entry fresh;
        if (analyseFile (query.path, fresh))
        {
            const std::lock_guard<std::mutex> hold (indexLock);
            index.put (fresh); // (a full index keeps answering for the files it has; this one is compared anyway)
            dirty = true;
            known = std::move (fresh);
        }
    }
    if (! known || ! known->valid)
        result.failed = true;
    else
    {
        const std::lock_guard<std::mutex> hold (indexLock);
        result.matches = index.rank (known->features, query.path, query.roots, query.max);
        result.searched = index.validCount();
    }
    {
        const std::lock_guard<std::mutex> hold (lock);
        if (stopping || latest != query.id)
            return; // a newer query (or cancel): this answer is not wanted
        result.partial = ! passes.empty();
    }
    if (query.done)
        query.done (result);
}

bool Service::runPass (std::vector<std::string>& folders)
{
    using Clock = std::chrono::steady_clock;
    auto lastSave = Clock::now();
    const auto separator = callbacks.fs.separator;
    while (! folders.empty())
    {
        if (interrupted())
            return false;
        const auto folder = folders.back();
        setStatus ([&] (Status& s) { s.current = folder; });
        const auto listing = callbacks.fs.list ? callbacks.fs.list (folder) : search::FolderListing();
        {
            const std::lock_guard<std::mutex> hold (indexLock);
            if (index.prune (folder, listing.exists ? listing.files : std::vector<std::string>(), separator) > 0)
                dirty = true;
        }
        for (const auto& name : listing.files)
        {
            if (callbacks.accepts && ! callbacks.accepts (name))
                continue;
            auto path = folder;
            if (path.empty() || path.back() != separator)
                path.push_back (separator);
            path += name;
            {
                const std::lock_guard<std::mutex> hold (indexLock);
                if (index.find (path) != nullptr)
                    continue;
                if (index.size() >= maxIndexedFiles)
                {
                    setStatus ([] (Status& s) { s.full = true; }, false);
                    break;
                }
            }
            // LYNX is playing, recording or rendering: the pass waits
            while (callbacks.shouldPause && callbacks.shouldPause())
            {
                setStatus ([] (Status& s) { s.paused = true; }, false);
                std::unique_lock<std::mutex> hold (lock);
                if (wake.wait_for (hold, std::chrono::milliseconds (250), [this] { return stopping; }))
                    break;
            }
            // a query is served first: the pass goes on once it is answered (the answer has its own thread)
            {
                std::unique_lock<std::mutex> hold (lock);
                wake.wait (hold, [this] { return stopping || (! pending && ! answering); });
            }
            if (interrupted())
                return false;
            {
                const std::lock_guard<std::mutex> hold (indexLock);
                if (index.find (path) != nullptr)
                    continue; // (that query analysed it)
            }

            const auto began = Clock::now();
            Entry entry;
            if (analyseFile (path, entry))
            {
                std::size_t files = 0;
                {
                    const std::lock_guard<std::mutex> hold (indexLock);
                    index.put (std::move (entry));
                    files = index.validCount();
                }
                dirty = true;
                bool notify = false;
                setStatus ([&] (Status& s) {
                    s.files = files;
                    s.paused = false;
                    notify = (++s.analysed % 50) == 0;
                }, false);
                if (notify && callbacks.statusChanged)
                    callbacks.statusChanged();
            }
            if (Clock::now() - lastSave > std::chrono::seconds (60))
            {
                saveIndex();
                lastSave = Clock::now();
            }
            rest (std::chrono::duration<double> (Clock::now() - began).count());
        }
        if (interrupted())
            return false; // (the folder stays on the stack: its files already analysed are skipped next time)
        folders.pop_back();
        for (auto it = listing.subfolders.rbegin(); it != listing.subfolders.rend(); ++it)
        {
            auto sub = folder;
            if (sub.empty() || sub.back() != separator)
                sub.push_back (separator);
            folders.push_back (sub + *it);
        }
    }
    return true;
}

void Service::answer()
{
    loadIndex();
    {
        const std::lock_guard<std::mutex> hold (lock);
        loading = false;
    }
    wake.notify_all();
    idleWake.notify_all();
    for (;;)
    {
        std::optional<Query> query;
        {
            std::unique_lock<std::mutex> hold (lock);
            wake.wait (hold, [this] { return stopping || pending.has_value(); });
            if (stopping)
                break;
            query = std::exchange (pending, std::nullopt);
            answering = true;
        }
        runQuery (*query);
        {
            const std::lock_guard<std::mutex> hold (lock);
            answering = false;
        }
        wake.notify_all(); // (the pass goes on)
        idleWake.notify_all();
    }
}

void Service::run()
{
    lowerThreadPriority();
    for (;;)
    {
        std::vector<std::string> folders;
        {
            std::unique_lock<std::mutex> hold (lock);
            wake.wait (hold, [this] { return stopping || (! loading && ! passes.empty()); });
            if (stopping)
                break;
            folders = passes.front();
            passRunning = true;
        }
        setStatus ([] (Status& s) { s.busy = true; });
        const auto finished = runPass (folders);
        bool idle = false;
        {
            const std::lock_guard<std::mutex> hold (lock);
            if (finished)
                passes.pop_front();
            else if (! passes.empty())
                passes.front() = std::move (folders); // (closing: the pass goes on from here next time)
            idle = passes.empty();
        }
        if (finished || idle)
            saveIndex();
        setStatus ([idle] (Status& s) {
            s.busy = ! idle;
            s.paused = false;
            if (idle)
            {
                s.current.clear();
                s.analysed = 0;
            }
        });
        {
            const std::lock_guard<std::mutex> hold (lock);
            passRunning = false;
        }
        idleWake.notify_all();
    }
}
} // namespace lynx::similarity
