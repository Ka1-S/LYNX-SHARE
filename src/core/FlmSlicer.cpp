#include "core/FlmSlicer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>

#include "core/FlmPad.h"
#include "core/Instruments.h"

// FL Studio Mobile's Slicer (round 190de; FLM_FORMAT §7.7). Clean-room: the detector and the voice below are the
// behaviour read in FLM's FL plugin, written in LYNX's own words and code.

namespace lynx::flmslicer
{
const float defaults[paramCount] = { 1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };

bool isChannel (const ChannelDef& c) noexcept
{
    return c.instrument == InstrumentKind::sampler && c.modelParams.size() >= (std::size_t) (head + paramCount) && c.modelParams[0] == marker;
}

std::vector<float> channelParams (const std::vector<float>& values, float sensitivity, int firstKey)
{
    std::vector<float> out { marker, std::max (0.0f, sensitivity), (float) std::clamp (firstKey, 0, 127) };
    for (int k = 0; k < paramCount; ++k)
        out.push_back (k < (int) values.size() && std::isfinite (values[(std::size_t) k]) ? std::clamp (values[(std::size_t) k], 0.0f, 1.0f) : defaults[k]);
    return out;
}

// ------------------------------------------------------------------------------------------------- the detector
namespace
{
void fft128 (std::array<std::complex<double>, 128>& a) noexcept
{
    constexpr int n = 128;
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[(std::size_t) i], a[(std::size_t) j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, -2.0 * 3.14159265358979323846 / len);
        for (int i = 0; i < n; i += len)
        {
            std::complex<double> wn (1.0, 0.0);
            for (int k = 0; k < len / 2; ++k)
            {
                const auto u = a[(std::size_t) (i + k)], v = a[(std::size_t) (i + k + len / 2)] * wn;
                a[(std::size_t) (i + k)] = u + v;
                a[(std::size_t) (i + k + len / 2)] = u - v;
                wn *= w;
            }
        }
    }
}

// one band's onset rise: hold `w` frames, release `c`, rise above the previous level × `g` × 5, floor 1e-10 / `d`
std::vector<double> rise (const std::vector<double>& band, int w, double c, double g, double d)
{
    const auto n = band.size();
    std::vector<double> r (n, 0.0);
    const auto floor = 1.0e-10 / d;
    double prev = 0.0;
    for (std::size_t k = 0; k + 1 < n; ++k)
    {
        double h = 0.0;
        for (std::size_t j = k >= (std::size_t) w ? k - (std::size_t) w : 0; j <= k; ++j)
            h = std::max (h, band[j]);
        const auto e = h <= prev ? h * (1.0 - c) + prev * c : h;
        const auto m = k == 0 ? floor : std::max (floor, prev);
        r[k] = e < m ? 0.0 : (e / m - 1.0) * g * 5.0;
        prev = e;
    }
    return r;
}
} // namespace

std::vector<std::int64_t> slices (const SampleData& sample, float sensitivity)
{
    const auto L = (std::int64_t) sample.left.size();
    std::vector<std::int64_t> out;
    if (L <= 0)
        return out;
    if (! (sensitivity > 0.0f))
    {
        for (const auto m : sample.markerStarts)
            if (m >= 0 && m < L && (out.empty() || m > out.back()) && out.size() < 128)
                out.push_back (m);
        if (out.empty())
            out.push_back (0);
        return out;
    }
    const auto fs = sample.sampleRate > 0.0 ? sample.sampleRate : 44100.0;
    const auto& R = sample.right.empty() ? sample.left : sample.right;
    const auto channels = sample.right.empty() ? 1 : 2;
    float peak = 0.0f;
    for (std::int64_t i = 0; i < L; ++i)
        peak = std::max ({ peak, std::abs (sample.left[(std::size_t) i]), std::abs (R[(std::size_t) i]) });
    if (peak <= 0.0f)
        return { 0 };
    // the analysis copy: the left channel normalised, three passes of a gentle high-pass (float, as FLM)
    std::vector<float> x (sample.left.begin(), sample.left.end());
    const auto inv = 1.0f / peak;
    for (auto& v : x)
        v *= inv;
    for (int pass = 0; pass < 3; ++pass)
    {
        float v = 0.0f, lp = 0.0f;
        for (auto& s : x)
        {
            v += (s - lp) * 9.134e-5f;
            lp += v;
            v *= 0.96f;
            s = s - lp;
        }
    }
    // frames of 128 every 16, a periodic Hamming window, power in two bands
    const auto frames = (std::size_t) ((L + 143) / 16);
    std::array<double, 128> window {};
    double sum = 0.0;
    for (int i = 0; i < 128; ++i)
        sum += window[(std::size_t) i] = 0.53836 - 0.46164 * std::cos (2.0 * 3.14159265358979323846 * i / 128.0);
    const auto scale = 1.0 / sum;
    std::vector<double> low (frames, 0.0), high (frames, 0.0);
    std::array<std::complex<double>, 128> buf {};
    for (std::size_t k = 0; k < frames; ++k)
    {
        for (int i = 0; i < 128; ++i)
        {
            const auto at = (std::int64_t) (16 * k) + i;
            buf[(std::size_t) i] = { at < L ? (double) x[(std::size_t) at] * window[(std::size_t) i] : 0.0, 0.0 };
        }
        fft128 (buf);
        double lo = 0.0, hi = 0.0;
        for (int b = 1; b <= 63; ++b)
        {
            const auto p = std::norm (buf[(std::size_t) b]) * scale * scale;
            (b <= 5 ? lo : hi) += p;
        }
        low[k] = lo * 0.2;
        high[k] = hi / 58.0;
    }
    const auto rl = rise (low, 55, 0.9761027, 0.9, 1.8), rh = rise (high, 41, 0.9643691, 1.0, 20.0);
    std::vector<double> on (frames);
    for (std::size_t k = 0; k < frames; ++k)
        on[k] = std::max (rl[k], rh[k]) * (double) sensitivity;
    // picking: an onset where the strength passes 1, started at the earliest frame of the last 28 above 0.75, at the
    // first sample louder than 5 % of the peak; at least 60 ms apart
    const auto ratio = fs / 44100.0;
    const auto minLen = (std::int64_t) std::round (0.060 * fs);
    const auto threshold = 0.05 * (double) peak;
    std::size_t k = 0;
    while (k < frames && out.size() < 128)
    {
        if (on[k] < 1.0)
        {
            ++k;
            continue;
        }
        auto j = k >= 28 ? k - 28 : 0;
        while (j < k && ! (on[j] > 0.75))
            ++j;
        const auto p = std::clamp ((std::int64_t) std::round ((double) (16 * j) * ratio), (std::int64_t) 0, L - 1);
        std::int64_t start = 0;
        if (p >= 1)
        {
            start = L;
            for (auto q = p; q < L; ++q)
            {
                const auto a = (double) sample.left[(std::size_t) q], b = channels == 2 ? (double) R[(std::size_t) q] : 0.0;
                if (std::sqrt ((a * a + b * b) / channels) > threshold)
                {
                    start = q;
                    break;
                }
            }
        }
        if (start <= L - minLen && (out.empty() || start > out.back()))
            out.push_back (start);
        k = std::max (k + 1, (std::size_t) std::round (std::round ((double) (start + minLen) / ratio + 64.0) / 16.0));
    }
    if (out.empty())
        out.push_back (0);
    return out;
}

// ------------------------------------------------------------------------------------------------- the voice
namespace
{
constexpr int voiceCount = 256;

struct Voice
{
    bool active = false;
    int key = 0;
    double pos = 0.0, end = 0.0, sliceEnd = 0.0;
    double level = 0.0, target = 5.0, rate = 1.0;
    float gainL = 1.0f, gainR = 1.0f;
    bool decaying = false, ending = false;
};

class SlicerInstrument final : public Instrument
{
public:
    explicit SlicerInstrument (std::shared_ptr<const SampleData> s) : sample (std::move (s))
    {
        for (int k = 0; k < paramCount; ++k)
            values[(std::size_t) k].store (defaults[k]);
    }

    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& v : voices)
            v = Voice {};
    }

    void configure (const ChannelDef& c, double) noexcept override
    {
        knobCents.store (c.sampler.pitch * 100.0f);
        if (! isChannel (c))
            return;
        for (int k = 0; k < paramCount; ++k)
            values[(std::size_t) k].store (std::clamp (c.modelParams[(std::size_t) (head + k)], 0.0f, 1.0f));
        firstKey.store ((int) c.modelParams[2]);
        sensitivity = c.modelParams[1];
    }

    // (the rack slices the sample once it is decoded: not on the audio thread)
    void settleNew() override
    {
        if (sample == nullptr)
            return;
        starts.clear();
        for (const auto s : flmslicer::slices (*sample, sensitivity))
            starts.push_back ((double) s);
    }

    void setChannelPitchNow (float knob, float rest) noexcept override { liveCents = knob + rest; }
    void setNextNoteExpression (float pan, float, bool) noexcept override { nextPan = pan; }
    void setParameter (int index, float v) noexcept override
    {
        const auto k = index - automationBase;
        if (k >= 0 && k < paramCount && std::isfinite (v))
            values[(std::size_t) k].store (std::clamp (v, 0.0f, 1.0f));
    }

    void noteOn (int key, float velocity) noexcept override
    {
        const auto pan = std::clamp (nextPan, -1.0f, 1.0f);
        nextPan = 0.0f;
        if (P (0) < 0.5f || sample == nullptr || starts.empty())
            return;
        if (P (6) != 0.0f) // MONO: every sounding voice fades out at 0.01
            for (auto& v : voices)
                if (v.active)
                    fade (v);
        const auto index = key - firstKey.load();
        if (index < 0 || index >= (int) starts.size())
            return;
        const auto frames = (double) sample->left.size();
        const auto mode = std::clamp ((int) (P (5) * 2.0f), 0, 2);
        for (auto& v : voices)
            if (! v.active)
            {
                v = Voice {};
                v.active = true;
                v.key = key;
                v.pos = starts[(std::size_t) index];
                v.sliceEnd = index + 1 < (int) starts.size() ? starts[(std::size_t) index + 1] : frames;
                v.end = mode == 0 ? v.sliceEnd : frames;
                v.level = 0.0, v.target = 5.0;
                v.rate = flmpad::rateAt (flmpad::attackRate (P (3)), fs);
                const auto vel = std::clamp (velocity, 0.0f, 1.0f);
                v.gainL = pan > 0.0f ? vel * (1.0f - pan) : vel;
                v.gainR = pan < 0.0f ? vel * (1.0f + pan) : vel;
                break;
            }
    }

    void noteOff (int key) noexcept override
    {
        if (std::clamp ((int) (P (5) * 2.0f), 0, 2) != 1) // (only "to the end while held" ends at the note's end)
            return;
        for (auto& v : voices)
            if (v.active && v.key == key)
                fade (v);
    }
    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            if (v.active)
                fade (v);
    }
    void panic() noexcept override
    {
        for (auto& v : voices)
            v = Voice {};
    }
    bool idle() const noexcept override
    {
        return std::none_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; });
    }

    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override
    {
        if (sample == nullptr || P (0) < 0.5f)
            return;
        const auto& L = sample->left;
        const auto& R = sample->right.empty() ? sample->left : sample->right;
        const auto len = (std::int64_t) L.size();
        const auto semis = (double) flmpad::speedSemitones (P (1)) + (std::isnan (liveCents) ? knobCents.load() : liveCents) * 0.01;
        const auto speed = std::exp2 (semis / 12.0) * sample->sampleRate / fs;
        const auto decay = P (4) > 0.99f ? 0.0 : flmpad::rateAt (flmpad::decayRate (P (4)), fs);
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            for (int i = 0; i < n; ++i)
            {
                v.level += (v.target - v.level) * v.rate;
                if (! v.decaying && v.level > 1.0)
                    v.level = 1.0, v.target = 0.0, v.rate = decay, v.decaying = true;
                const auto idx = (std::int64_t) std::floor (v.pos);
                const auto f = v.pos - (double) idx;
                double xl = 0.0, xr = 0.0;
                if (idx >= 0 && idx < len)
                {
                    xl = (double) L[(std::size_t) idx] * (1.0 - f);
                    xr = (double) R[(std::size_t) idx] * (1.0 - f);
                    if (idx + 1 < len)
                        xl += (double) L[(std::size_t) idx + 1] * f, xr += (double) R[(std::size_t) idx + 1] * f;
                }
                if (const auto toEnd = v.sliceEnd - v.pos; toEnd > 0.0 && toEnd < 512.0)
                    xl *= toEnd / 512.0, xr *= toEnd / 512.0;
                left[i] += (float) (xl * v.level * v.gainL) * gainLeft;
                right[i] += (float) (xr * v.level * v.gainR) * gainRight;
                v.pos += speed;
                if (! v.ending && (v.pos >= v.end || v.pos >= (double) len))
                    fade (v);
                if (v.level < 0.001 && v.target < 0.001)
                {
                    v.active = false;
                    break;
                }
            }
        }
    }

private:
    float P (int k) const noexcept { return values[(std::size_t) k].load (std::memory_order_relaxed); }
    void fade (Voice& v) noexcept
    {
        v.target = 0.0, v.rate = flmpad::rateAt (0.01f, fs), v.ending = true, v.decaying = true;
    }

    std::shared_ptr<const SampleData> sample;
    std::vector<double> starts;
    float sensitivity = 0.25f;
    std::array<std::atomic<float>, paramCount> values {};
    std::atomic<int> firstKey { 60 };
    std::atomic<float> knobCents { 0.0f };
    float liveCents = std::numeric_limits<float>::quiet_NaN(), nextPan = 0.0f;
    double fs = 44100.0;
    std::array<Voice, voiceCount> voices {};
};
} // namespace

std::unique_ptr<Instrument> makeInstrument (std::shared_ptr<const SampleData> sample)
{
    return std::make_unique<SlicerInstrument> (std::move (sample));
}
} // namespace lynx::flmslicer
