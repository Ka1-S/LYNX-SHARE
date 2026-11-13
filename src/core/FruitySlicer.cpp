#include "core/FruitySlicer.h"

#include "core/FlVoiceProcessor.h"
#include "core/Instruments.h"
#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// FL's Fruity Slicer (Fruity Slicer_x64.dll, docs/analise/REA_rodada180c.md §Fruity Slicer). The sample edits keep FL's
// arithmetic: double-precision ramps, Delphi's Round (half to even, std::nearbyint), the result stored as float (FL turns the sample to float before its declick).

namespace lynx::fslicer
{
namespace
{
struct Reader
{
    const std::vector<std::uint8_t>& d;
    std::size_t pos = 0;
    bool ok = true;

    template <typename T>
    T read()
    {
        T v {};
        if (pos + sizeof (T) > d.size())
        {
            ok = false;
            return v;
        }
        std::memcpy (&v, d.data() + pos, sizeof (T));
        pos += sizeof (T);
        return v;
    }

    std::string str8()
    {
        const auto n = read<std::uint8_t>();
        if (! ok || pos + n > d.size())
        {
            ok = false;
            return {};
        }
        std::string s (reinterpret_cast<const char*> (d.data() + pos), n);
        pos += n;
        return s;
    }
};

struct Writer
{
    std::vector<std::uint8_t> d;

    template <typename T>
    void put (T v)
    {
        const auto at = d.size();
        d.resize (at + sizeof (T));
        std::memcpy (d.data() + at, &v, sizeof (T));
    }

    void str8 (const std::string& s)
    {
        const auto n = std::min<std::size_t> (s.size(), 255);
        put ((std::uint8_t) n);
        d.insert (d.end(), s.begin(), s.begin() + (std::ptrdiff_t) n);
    }
};

float* channel (SampleData& data, int c)
{
    return c == 0 ? data.left.data() : data.right.data();
}

int channels (const SampleData& data)
{
    return data.right.empty() ? 1 : 2;
}
} // namespace

std::optional<State> parse (const std::vector<std::uint8_t>& bytes)
{
    Reader in { bytes };
    State s;
    s.version = in.read<std::uint32_t>();
    // FUN_00766c40: versions before 13 store a slice as its position only; LYNX reads 13…16
    if (! in.ok || s.version < 13 || s.version > 16)
        return std::nullopt;

    s.lengthBeats = in.read<float>();
    s.bpm = in.read<float>();
    s.pitchCents = in.read<std::int32_t>();
    s.stretch = in.read<std::int32_t>();
    s.method = in.read<std::int32_t>();
    if (s.version < 15 && s.method > 1) // FL renumbered the engine's modes in version 15
        s.method += 2;
    s.fadeInMs = in.read<std::int32_t>();
    s.fadeOutMs = in.read<std::int32_t>();
    s.path = in.str8();
    const auto count = in.read<std::uint32_t>();
    if (! in.ok || count > 4096)
        return std::nullopt;
    for (std::uint32_t i = 0; i < count && in.ok; ++i)
    {
        Slice slice;
        slice.name = in.str8();
        slice.position = in.read<std::int32_t>();
        slice.key = in.read<std::int32_t>();
        slice.beat = in.read<float>();
        slice.flags = in.read<std::uint8_t>();
        s.slices.push_back (std::move (slice));
    }
    s.animate = in.read<std::uint8_t>();
    s.firstNote = in.read<std::int32_t>();
    s.playToEnd = in.read<std::uint8_t>();
    s.sampleRate = in.read<std::int32_t>();
    s.autoDump = in.read<std::uint8_t>();
    s.declick = in.read<std::uint8_t>();
    s.autoFit = in.read<std::uint8_t>();
    if (! in.ok)
        return std::nullopt;
    if (s.version >= 14)
        s.display = in.read<std::int32_t>();
    if (! in.ok)
        return std::nullopt;
    if (bytes.size() - in.pos >= 364) // v16: the voice block (FL ignores a shorter one)
        s.voiceBlock.assign (bytes.begin() + (std::ptrdiff_t) in.pos, bytes.begin() + (std::ptrdiff_t) in.pos + 364);
    s.firstNote = std::clamp (s.firstNote, 0, 124);
    return s;
}

std::vector<std::uint8_t> write (const State& s)
{
    Writer out;
    out.put<std::uint32_t> (16);
    out.put (s.lengthBeats);
    out.put (s.bpm);
    out.put (s.pitchCents);
    out.put (s.stretch);
    out.put (s.method);
    out.put (s.fadeInMs);
    out.put (s.fadeOutMs);
    out.str8 (s.path);
    out.put ((std::uint32_t) s.slices.size());
    for (const auto& slice : s.slices)
    {
        out.str8 (slice.name);
        out.put (slice.position);
        out.put (slice.key);
        out.put (slice.beat);
        out.put (slice.flags);
    }
    out.put (s.animate);
    out.put (s.firstNote);
    out.put (s.playToEnd);
    out.put (s.sampleRate);
    out.put (s.autoDump);
    out.put (s.declick);
    out.put (s.autoFit);
    out.put (s.display);
    if (s.voiceBlock.size() == 364)
        out.d.insert (out.d.end(), s.voiceBlock.begin(), s.voiceBlock.end());
    return out.d;
}

std::vector<std::int64_t> sliceLengths (const State& state, std::int64_t frames)
{
    std::vector<std::int64_t> lengths;
    for (std::size_t i = 0; i < state.slices.size(); ++i)
    {
        const auto start = (std::int64_t) state.slices[i].position;
        if (i + 1 < state.slices.size())
            lengths.push_back (std::max<std::int64_t> (1, (std::int64_t) state.slices[i + 1].position - start));
        else
            lengths.push_back (frames - start);
    }
    return lengths;
}

void reverseRegion (SampleData& data, std::int64_t first, std::int64_t last)
{
    const auto frames = (std::int64_t) data.left.size();
    first = std::max<std::int64_t> (0, first);
    last = std::min (last, frames - 1);
    if (first >= last)
        return;
    for (int c = 0; c < channels (data); ++c)
        std::reverse (channel (data, c) + first, channel (data, c) + last + 1);
}

// FUN_00607ed0 with FL's two slice fades (FUN_0076a830): in = shape 0x102 (linear), out = shape 0x103 with 10 (the
// "L11" curve); both become a raised cosine when the faded part is 50 ms or less (flag 0x100)
void fadeRegion (SampleData& data, std::int64_t first, std::int64_t last, bool fadeIn)
{
    const auto frames = (std::int64_t) data.left.size();
    if (frames <= 0 || last < first)
        return;
    const auto ms = (double) (last - first + 1) * 1000.0 / data.sampleRate;
    const bool cosine = ms <= 50.0;
    const double from = fadeIn ? 0.0 : 1.0, to = fadeIn ? 1.0 : 0.0;
    const double span = 1.0 / (double) (last - first + 2);
    const double ln11 = std::log ((double) (10.0f + 1.0f));
    for (auto k = std::max<std::int64_t> (0, first); k <= std::min (last, frames - 1); ++k)
    {
        double t = (double) (k - first + 1) * span;
        if (cosine)
            t = (std::cos ((1.0 - t) * 3.141592653589793) + 1.0) * 0.5;
        else if (! fadeIn)
        {
            // shape 3, 10: falling (to < from), so t runs backwards, then (e^(t·ln 11) − 1)/10 in single precision
            t = 1.0 - t;
            t = (double) (float) (std::exp ((double) (float) t * ln11) - 1.0) * (1.0 / 10.0);
            t = 1.0 - t;
        }
        const auto gain = t * (to - from) + from;
        for (int c = 0; c < channels (data); ++c)
        {
            auto& x = channel (data, c)[k];
            x = (float) ((double) x * gain);
        }
    }
}

// FUN_00608530 (…, 1, 0.7) → FUN_0060d020 → FUN_0060cdd0: over the last 0.04·0.7 s of the slice (after its trailing
// zeros), a one-pole low-pass whose rate climbs from 2/sr to 22050·2/sr is taken away from the sound, and the last
// millisecond fades out on a raised cosine
void declickEnd (SampleData& data, std::int64_t first, std::int64_t last)
{
    const auto frames = (std::int64_t) data.left.size();
    last = std::min (last, frames - 1);
    first = std::max<std::int64_t> (0, first);
    if (frames <= 0 || last <= first)
        return;
    const auto sr = data.sampleRate;
    const auto wanted = (int) std::nearbyint (sr * (double) 0.04f * (double) 0.7f);
    for (int c = 0; c < channels (data); ++c)
    {
        auto* x = channel (data, c);
        // the trailing zeros stay (FL starts at the last non-zero sample)
        auto end = last;
        auto available = (int) (last - first + 1);
        for (int i = 0; i < available; ++i)
            if (x[last - i] != 0.0f)
            {
                end = last - i;
                available -= i;
                break;
            }
        const auto n = std::min (wanted, available);
        if (n <= 0)
            continue;
        const double rateStep = (std::log (22050.0) / (std::log (2.0) * (double) n)) * std::log (2.0);
        const auto fadeLength = (int) std::min<std::int64_t> ((std::int64_t) std::nearbyint (sr * 0.001), n);
        const auto fadeFrom = n - fadeLength;
        double low = 0.0, fade = 1.0;
        auto at = end - (n - 1);
        for (int i = 1; i <= n; ++i, ++at)
        {
            auto rate = (2.0 / sr) * std::exp ((double) i * rateStep);
            if (rate >= 1.0)
                rate = 1.0;
            low += rate * ((double) x[at] - low);
            if (i > fadeFrom)
                fade = (std::cos ((double) (i - fadeFrom) * (1.0 / (double) (fadeLength + 1)) * 3.141592653589793) + 1.0) * 0.5;
            x[at] = (float) (((double) x[at] - low) * fade);
        }
    }
}

std::shared_ptr<SampleData> prepare (const SampleData& sample, const State& state)
{
    auto data = std::make_shared<SampleData>();
    const auto frames = (std::int64_t) sample.frames();
    data->sampleRate = sample.sampleRate;
    data->left.assign (sample.leftData(), sample.leftData() + frames);
    if (sample.isStereo())
        data->right.assign (sample.rightData(), sample.rightData() + frames);
    if (frames <= 0 || state.slices.empty())
        return data;

    const auto lengths = sliceLengths (state, frames);
    const auto each = [&] (auto&& f) {
        for (std::size_t i = 0; i < state.slices.size(); ++i)
            if (lengths[i] > 0)
                f (i, (std::int64_t) state.slices[i].position, lengths[i]);
    };

    each ([&] (std::size_t i, std::int64_t start, std::int64_t length) { // FUN_00769cd0
        if (state.slices[i].reversed())
            reverseRegion (*data, start, start + length - 1);
    });
    if (state.declick != 0) // FUN_0076a790
        each ([&] (std::size_t, std::int64_t start, std::int64_t length) { declickEnd (*data, start, start + length - 1); });

    // FUN_0076aa20: TS with Fill gaps / Alternate fill gaps. The sample goes to 16 bits, each slice moves to its scaled
    // place and grows by FUN_0075b5d0 (or is cut when it shrinks), then back to float
    if (state.stretch != 0 && state.method < 2)
    {
        const auto factor = stretchFactor (state.stretch);
        const auto channels = data->right.empty() ? 1 : 2;
        const auto newFrames = (std::int64_t) std::nearbyint ((double) frames * (double) factor);
        std::vector<std::int16_t> old ((std::size_t) (frames * channels)), fresh ((std::size_t) (std::max<std::int64_t> (0, newFrames) * channels), 0);
        for (std::int64_t i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c)
            {
                const auto v = std::nearbyint ((double) ((c == 0 ? data->left : data->right)[(std::size_t) i] * 32768.0f));
                old[(std::size_t) (i * channels + c)] = (std::int16_t) std::clamp (v, -32768.0, 32767.0);
            }
        const auto starts = playedStarts (state, frames);
        const auto sr = (int) std::nearbyint (data->sampleRate);
        for (std::size_t i = 0; i < state.slices.size(); ++i)
        {
            const auto oldLength = (int) lengths[i];
            const auto newLength = (int) (i + 1 < starts.size() ? std::max<std::int64_t> (1, (std::int64_t) starts[i + 1] - starts[i])
                                                                : newFrames - starts[i]);
            const auto from = (std::int64_t) state.slices[i].position, to = (std::int64_t) starts[i];
            if (oldLength <= 0 || newLength <= 0 || from < 0 || to < 0 || from + oldLength > frames || to + newLength > newFrames)
                continue;
            if (oldLength < newLength)
                fillGaps (old.data() + from * channels, fresh.data() + to * channels, oldLength, newLength, channels, sr,
                          oldLength < 150 ? 0 : state.method);
            else
                std::copy (old.begin() + (std::ptrdiff_t) (from * channels), old.begin() + (std::ptrdiff_t) ((from + newLength) * channels),
                           fresh.begin() + (std::ptrdiff_t) (to * channels));
        }
        data->left.assign ((std::size_t) newFrames, 0.0f);
        if (channels == 2)
            data->right.assign ((std::size_t) newFrames, 0.0f);
        for (std::int64_t i = 0; i < newFrames; ++i)
            for (int c = 0; c < channels; ++c)
                (c == 0 ? data->left : data->right)[(std::size_t) i] = (float) ((double) fresh[(std::size_t) (i * channels + c)] * 3.0517578125e-05);
    }
    const auto playedFrames = (std::int64_t) data->left.size();
    const auto starts = playedStarts (state, frames);
    const auto playedLengths = [&] {
        std::vector<std::int64_t> l;
        for (std::size_t i = 0; i < starts.size(); ++i)
            l.push_back (i + 1 < starts.size() ? std::max<std::int64_t> (1, (std::int64_t) starts[i + 1] - starts[i]) : playedFrames - starts[i]);
        return l;
    }();
    if (state.fadeInMs != 0 || state.fadeOutMs != 0) // FUN_0076a830
    {
        const auto perMs = (float) (data->sampleRate / 1000.0);
        const auto in = (std::int64_t) std::nearbyint ((double) ((float) state.fadeInMs * perMs));
        const auto out = (std::int64_t) std::nearbyint ((double) ((float) state.fadeOutMs * perMs));
        for (std::size_t i = 0; i < starts.size(); ++i)
        {
            const auto start = (std::int64_t) starts[i], length = playedLengths[i];
            if (length <= 0)
                continue;
            const auto a = std::min (in, length);
            fadeRegion (*data, start, start + a - 1, true);
            const auto b = std::min (out, length);
            const auto from = start + length - b;
            fadeRegion (*data, from, from + b - 1, false);
        }
    }
    return data;
}

float stretchFactor (std::int32_t stretch)
{
    return (float) std::exp (std::log (2.0) * (double) ((float) stretch / 10000.0f));
}

std::vector<std::int32_t> playedStarts (const State& state, std::int64_t frames)
{
    std::vector<std::int32_t> starts;
    const auto stretched = state.stretch != 0 && state.method < 2 && frames > 0;
    const auto factor = stretched ? stretchFactor (state.stretch) : 1.0f;
    for (const auto& slice : state.slices)
        starts.push_back (stretched ? (std::int32_t) std::nearbyint ((double) slice.position * (double) factor) : slice.position);
    return starts;
}

void fillGaps (const std::int16_t* from, std::int16_t* to, int oldFrames, int newFrames, int channels, int sampleRate, int method)
{
    // FUN_0075b5d0
    std::vector<float> src ((std::size_t) (oldFrames * channels)), dst ((std::size_t) (newFrames * channels), 0.0f);
    for (std::size_t i = 0; i < src.size(); ++i)
        src[i] = (float) ((double) from[i] * 3.0517578125e-05);
    const int tail = (sampleRate * 10 + 500) / 1000;
    // a slice that ends in silence (its last 10 ms under one step): copied, the rest left silent
    bool silent = true;
    for (int i = oldFrames * channels - 1; i >= std::max (0, oldFrames - tail) * channels; --i)
        if ((double) src[(std::size_t) i] > 3.0517578125e-05)
        {
            silent = false;
            break;
        }
    auto store = [&] (int count) {
        for (int i = 0; i < count; ++i)
        {
            const auto v = std::nearbyint ((double) (dst[(std::size_t) i] * 32768.0f));
            to[i] = (std::int16_t) std::clamp (v, -32768.0, 32767.0);
        }
    };
    if (silent)
    {
        const auto n = std::min (oldFrames, newFrames) * channels;
        for (int i = 0; i < n; ++i)
            dst[(std::size_t) i] = src[(std::size_t) i];
        store (newFrames * channels);
        return;
    }
    const int twice = oldFrames * 2;
    for (int c = 0; c < channels; ++c)
    {
        auto at = [&] (int frame) { return src[(std::size_t) (frame * channels + c)]; };
        int first = (oldFrames + 1) >> 1, last = oldFrames - 1;
        const int back = std::max (1, (sampleRate * 20 + 500) / 1000);
        bool none = true;
        for (int k = std::max (0, first - back); k <= oldFrames - 2; ++k)
            if (k >= 1 && std::fabs ((double) at (k - 1)) < std::fabs ((double) at (k)) && std::fabs ((double) at (k + 1)) <= std::fabs ((double) at (k)))
            {
                if (none)
                {
                    none = false;
                    first = k;
                }
                last = k;
            }
        const int span = std::max (1, last - first);
        for (int k = 0; k < newFrames; ++k)
        {
            float x;
            if (k < first)
                x = at (k);
            else
            {
                const auto idx = ((k - first) + span) % (span * 2);
                x = at (std::abs (idx - span) + first);
            }
            if (k < twice)
            {
                if (oldFrames <= k && oldFrames < twice)
                {
                    const float t = (float) (k - oldFrames) / (float) (twice - oldFrames);
                    float g = (1.0f - t) * (1.0f - t) * (1.0f - t);
                    if ((method & 1) == 0)
                    {
                        if (0.5f < t)
                        {
                            const float u = 1.0f - (t - 0.5f) * 2.0f;
                            g = g + (0.275f - g) * (1.0f - u * u * u);
                        }
                    }
                    else if (0.5f < t)
                    {
                        const float u = 1.0f - (t - 0.5f) * 2.0f, w = (t - 0.5f) * 2.0f;
                        g = (g + (0.1375f - g) * (1.0f - u * u * u)) * (1.0f - w * w * w);
                    }
                    x = x * g;
                }
            }
            else
                x = 0.0f;
            dst[(std::size_t) (k * channels + c)] = x;
        }
    }
    if ((method & 1) == 0)
    {
        // Fill gaps ends with a short linear fade (2 ms) where the grown part stops
        const int end = std::min (twice, newFrames);
        const int fade = std::max (0, std::min ((sampleRate * 2 + 500) / 1000, end - 1));
        if (fade > 0)
            for (int c = 0; c < channels; ++c)
                for (int k = end - fade; k <= end - 1; ++k)
                {
                    const double d = std::max (1.0, (double) (fade - 1));
                    const double r = std::min (1.0, std::max (0.0, (double) (k - (end - fade)) / d));
                    dst[(std::size_t) (k * channels + c)] *= (float) (1.0 - r);
                }
    }
    store (newFrames * channels);
}

void configureSampler (ChannelDef& def, const State& state, std::int64_t frames)
{
    def.sampler.slices.clear();
    // `frames`: the prepared sample's; the starts are FL's scaled ones when Fill gaps stretched it
    const auto scaled = state.stretch != 0 && state.method < 2;
    const auto factor = scaled ? stretchFactor (state.stretch) : 1.0f;
    const auto originalFrames = scaled ? (std::int64_t) std::nearbyint ((double) frames / (double) factor) : frames;
    const auto starts = playedStarts (state, originalFrames);
    std::vector<int> played (starts.size(), -1); // a slice's index among the played ones (a start past the end is left out)
    if (frames > 0)
        for (std::size_t i = 0; i < starts.size(); ++i)
            if (const auto p = starts[i]; p >= 0 && p < frames)
            {
                played[i] = (int) def.sampler.slices.size();
                def.sampler.slices.push_back ((float) ((double) p / (double) frames));
            }
    def.sampler.sliceFirstKey = firstNote (state);
    // which slice each key plays (issue #160): FL's key table, with the slices' own keys
    def.sampler.sliceKeys = keyTable (state);
    for (auto& k : def.sampler.sliceKeys)
        k = k >= 0 && k < (int) played.size() ? played[(std::size_t) k] : -1;
    def.sampler.slicesToEnd = state.playToEnd != 0;
    def.sampler.slicesCutAtEnd = true; // its voice ends with the slice (FL render: exact silence right after it)
    // The voice: Fruity Slicer's voices run the plugin voice processor (FUN_00756d30) with the state's 364-byte block —
    // the five articulators (the volume envelope's release is what a note-off plays, FL render round 180c), Mod X / Y
    // and the filter type. LYNX's sampler runs the same records as FL's channel voice (REA_rodada69 §3, REA_rodada76).
    {
        std::array<std::uint8_t, flvp::snapshotSize> block {};
        if (state.voiceBlock.size() == (std::size_t) flvp::snapshotSize)
            std::memcpy (block.data(), state.voiceBlock.data(), block.size());
        else
            block = flvp::defaultSnapshot(); // FUN_00757bd0: a state without the block keeps the plugin's defaults
        def.sampler.flChannel.assign (89, 0);
        std::memcpy (def.sampler.flChannel.data(), block.data(), 85 * 4);
        std::memcpy (&def.sampler.flChannel[85], block.data() + 0x15c, 4);
        std::memcpy (&def.sampler.flChannel[86], block.data() + 0x160, 4);
        std::memcpy (&def.sampler.flChannel[87], block.data() + 0x168, 4);
        def.sampler.flChannel[88] = 96; // FL's project PPQ (the articulator times are in ticks of it)
    }
    // PS: FL shifts the pitch and keeps the length through its licensed stretch engine; LYNX resamples (the slice gets
    // shorter or longer with the pitch)
    def.sampler.pitch = std::clamp (def.sampler.pitch + (float) state.pitchCents / 100.0f, -24.0f, 24.0f);
}

flvoice::Settings voiceSettings (const State& state)
{
    return flvoice::read (state.voiceBlock);
}

void setVoiceSettings (State& state, const flvoice::Settings& settings)
{
    flvoice::write (state.voiceBlock, settings); // (write() saves version 16, which carries the block)
}

State fresh (const std::string& path, int sampleRate)
{
    State s;
    s.path = path;
    s.sampleRate = sampleRate;
    s.slices.push_back ({});
    // What a Fruity Slicer added in FL 2026 saves (round 190m, /mnt/e/lynx-work/fpc190m/fl/fresh.flp): ANIMATE and
    // DECLICK on, DEC at 4 ms, and the volume envelope on with no delay / attack / hold / decay, full sustain and a
    // short release — the rest of the voice block is the plugin's defaults. FL's stretch method there is 4 (the
    // licensed engine); LYNX keeps Fill gaps, the method its TS wheel can run.
    s.animate = 1;
    s.declick = 1;
    s.fadeOutMs = 4;
    auto voice = flvoice::read ({});
    auto& volume = voice.articulators[flvoice::volume];
    volume.envelope = true;
    volume.delay = volume.attack = volume.hold = volume.decay = 100;
    volume.sustain = 128;
    volume.release = 6500;
    setVoiceSettings (s, voice);
    return s;
}
int firstNote (const State& state)
{
    int note = 60;
    const auto n = (int) state.slices.size();
    while (! (note - 1 + n < 125 || note < 12))
        note -= 12;
    return note;
}

int sliceKey (const State& state, int index)
{
    // FUN_0076b4f0
    auto key = index >= 0 && index < (int) state.slices.size() ? state.slices[(std::size_t) index].key : -1;
    if (key < 0)
        key = firstNote (state) + index;
    return std::clamp (key, 0, 124);
}

std::vector<int> keyTable (const State& state)
{
    // FUN_0076c200
    std::vector<int> table (125, -1);
    for (int i = 0; i < (int) state.slices.size(); ++i)
        table[(std::size_t) sliceKey (state, i)] = i;
    return table;
}

std::vector<DumpNote> dumpNotes (const State& state, int tag, std::int64_t frames, double sampleRate, double bpm, int ppq,
                                 const std::function<int (int)>& random)
{
    // FUN_0076b640 and its two local functions (FUN_0076b570: sample position → ticks, FUN_0076b5b0: rounding,
    // quantize and swing). FL hands it the project's PPQ; P+0x188 is taken as 4 (HYP: the bar's beats, /4 cancels it).
    std::vector<DumpNote> notes;
    const auto n = (int) state.slices.size();
    if (n <= 0)
        return notes;
    const int beatsPerBar = 4;
    float perFrame = 0.0f;
    if (frames != 0)
        perFrame = (((float) (ppq * beatsPerBar) * state.lengthBeats) / (float) frames) / 4.0f;
    int stereo = -16;
    int threshold = -(ppq / 8);
    const int step = ppq / 4;
    const int repeat = tag < 13 ? 1 : 1 << (tag - 12);
    float realTime = 0.0f;
    if (tag == 9)
        realTime = (float) ((double) frames / (sampleRate * 60.0)) * (float) bpm * (float) ppq;

    const auto toTicks = [&] (std::int32_t position) {
        return tag == 9 ? ((float) position * realTime) / (float) frames : (float) position * perFrame;
    };
    const auto place = [&] (float t) {
        if (tag == 2 || tag == 3)
        {
            const auto q = (int) std::nearbyint ((double) (t / (float) step));
            auto r = q * step;
            if (tag == 3 && (q & 1) == 1)
                r += step / 3;
            return r;
        }
        return (int) std::nearbyint ((double) t);
    };

    std::vector<int> times;
    for (const auto& slice : state.slices)
        times.push_back (slice.beat < 0.0f || tag == 9 ? place (toTicks (slice.position)) : place ((float) (unsigned) (slice.beat * (float) ppq)));
    times.push_back (tag == 9 ? (int) std::nearbyint ((double) realTime)
                              : (int) std::nearbyint ((double) ((state.lengthBeats * (float) ppq * (float) beatsPerBar) / 4.0f)));

    for (int i = 0; i < n; ++i)
    {
        DumpNote note;
        note.start = times[(std::size_t) i];
        int length = std::numeric_limits<int>::max();
        for (const auto t : times)
            if (t > note.start)
                length = std::min (length, t - (int) note.start);
        note.length = length == std::numeric_limits<int>::max() ? 1 : length;

        int index = i;
        if (tag == 1)
            index = n - i - 1;
        else if (tag == 4)
            index = random (n);
        else if (tag == 5)
            index = 0;
        else if (tag == 10)
            index = i - 1;
        else if (tag == 11)
            index = i + 1;
        index = (index + n) % n;
        note.key = sliceKey (state, index);

        if (tag == 6)
        {
            if (note.start < threshold)
                note.velocity = 0.7421875f;
            else
            {
                note.velocity = 0.859375f;
                threshold += ppq;
            }
        }
        else if (tag == 7)
        {
            if (threshold <= note.start)
            {
                if (note.start < threshold + ppq)
                    note.pitchCents = 300;
                else
                    threshold += ppq * 2;
            }
        }
        else if (tag == 8)
        {
            note.pan = stereo;
            stereo = -stereo;
        }
        else if (tag == 12)
        {
            note.pan = random (17) - 8;
            note.velocity = (float) (random (20) + 90) / 128.0f;
            note.pitchCents = random (801) - 200;
        }

        if (repeat > 1)
        {
            const auto first = note.start, whole = note.length;
            auto at = first;
            for (int k = 1; k <= repeat; ++k)
            {
                const auto next = first + (k * whole) / repeat;
                auto part = note;
                part.start = at;
                part.length = std::max<std::int64_t> (1, next - at);
                notes.push_back (part);
                at = next;
            }
        }
        else
            notes.push_back (note);
    }
    return notes;
}

std::vector<std::int32_t> gridSlices (const State& state, std::int64_t frames, double beats)
{
    std::vector<std::int32_t> starts;
    if (frames <= 0 || beats <= 0.0 || state.lengthBeats <= 0.0f)
        return { 0 };
    const auto framesPerBeat = (double) frames / (double) state.lengthBeats;
    for (double b = 0.0; b * framesPerBeat < (double) frames - 0.5; b += beats)
        starts.push_back ((std::int32_t) std::nearbyint (b * framesPerBeat));
    return starts;
}

std::vector<std::int32_t> zeroCrossSlices (const std::vector<std::int32_t>& starts, const SampleData& sample)
{
    const auto frames = (std::int64_t) sample.frames();
    const auto* x = sample.leftData();
    auto crossing = [&] (std::int64_t i) { return i <= 0 || i >= frames || (x[i - 1] <= 0.0f) != (x[i] <= 0.0f); };
    std::vector<std::int32_t> out;
    for (const auto s : starts)
    {
        std::int64_t best = s;
        for (std::int64_t d = 0; d < 4096; ++d)
        {
            if (crossing (s - d)) { best = s - d; break; }
            if (crossing (s + d)) { best = s + d; break; }
        }
        out.push_back ((std::int32_t) std::clamp<std::int64_t> (best, 0, std::max<std::int64_t> (0, frames - 1)));
    }
    return out;
}
} // namespace lynx::fslicer
