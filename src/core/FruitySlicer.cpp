#include "core/FruitySlicer.h"

#include "core/Instruments.h"
#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
        slice.extraA = in.read<std::int32_t>();
        slice.extraB = in.read<float>();
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
        out.put (slice.extraA);
        out.put (slice.extraB);
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
    if (state.fadeInMs != 0 || state.fadeOutMs != 0) // FUN_0076a830
    {
        const auto perMs = (float) (data->sampleRate / 1000.0);
        const auto in = (std::int64_t) std::nearbyint ((double) ((float) state.fadeInMs * perMs));
        const auto out = (std::int64_t) std::nearbyint ((double) ((float) state.fadeOutMs * perMs));
        each ([&] (std::size_t, std::int64_t start, std::int64_t length) {
            const auto a = std::min (in, length);
            fadeRegion (*data, start, start + a - 1, true);
            const auto b = std::min (out, length);
            const auto from = start + length - b;
            fadeRegion (*data, from, from + b - 1, false);
        });
    }
    return data;
}

void configureSampler (ChannelDef& def, const State& state, std::int64_t frames)
{
    def.sampler.slices.clear();
    if (frames > 0)
        for (const auto& slice : state.slices)
            if (slice.position >= 0 && slice.position < frames)
                def.sampler.slices.push_back ((float) ((double) slice.position / (double) frames));
    def.sampler.sliceFirstKey = state.firstNote;
    def.sampler.slicesToEnd = state.playToEnd != 0;
}

State fresh (const std::string& path, int sampleRate)
{
    State s;
    s.path = path;
    s.sampleRate = sampleRate;
    s.slices.push_back ({});
    return s;
}
} // namespace lynx::fslicer
