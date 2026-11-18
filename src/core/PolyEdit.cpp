#include "core/PolyEdit.h"

#include "core/Instruments.h"

#include "../../third_party/pitchnet-poly/PolyEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <mutex>

namespace lynx::polyedit
{
namespace
{
constexpr std::uint32_t formatVersion = 1;

std::vector<float> monoOf (const SampleData& s)
{
    std::vector<float> m (s.left.size());
    if (s.right.empty())
        std::copy (s.left.begin(), s.left.end(), m.begin());
    else
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (s.left[i] + (i < s.right.size() ? s.right[i] : s.left[i]));
    return m;
}

std::vector<std::vector<float>> channelsOf (const SampleData& s)
{
    std::vector<std::vector<float>> c { s.left };
    if (! s.right.empty())
        c.push_back (s.right);
    return c;
}

std::map<int, polyphonic::NoteEdit> editsOf (const Model& m)
{
    std::map<int, polyphonic::NoteEdit> out;
    for (const auto& [k, e] : m.edits)
    {
        if (k < 0 || k >= (int) m.notes.size() || e.identity())
            continue;
        polyphonic::NoteEdit n;
        n.semitones = e.semitones;
        n.gainDb = e.gainDb;
        n.mute = e.mute;
        n.timeOffsetSeconds = e.moveSeconds;
        n.stretch = std::clamp (e.stretch, 0.25f, 4.0f);
        n.takeBackground = m.takeBackground;
        out[k] = n;
    }
    return out;
}

bool fits (const Model& m, const SampleData& s) noexcept
{
    return m.sampleRate > 0.0 && std::abs (m.sampleRate - s.sampleRate) < 0.5 && m.frames == (std::int64_t) s.left.size() && m.hop > 0 && m.fftSize > 0;
}

// little-endian writer / reader
struct Out
{
    std::vector<std::uint8_t>& b;
    template <typename T> void put (T v)
    {
        std::uint8_t raw[sizeof (T)];
        std::memcpy (raw, &v, sizeof (T));
        b.insert (b.end(), raw, raw + sizeof (T));
    }
};
struct In
{
    const std::vector<std::uint8_t>& b;
    std::size_t at = 0;
    bool ok = true;
    template <typename T> T get()
    {
        T v {};
        if (at + sizeof (T) > b.size())
        {
            ok = false;
            return v;
        }
        std::memcpy (&v, b.data() + at, sizeof (T));
        at += sizeof (T);
        return v;
    }
};

std::uint64_t fnv (const std::vector<std::uint8_t>& b) noexcept
{
    std::uint64_t h = 1469598103934665603ull;
    for (auto c : b)
        h = (h ^ c) * 1099511628211ull;
    return h;
}
} // namespace

float Note::f0At (int frame) const
{
    if (f0.empty())
        return 0.0f;
    return f0[(std::size_t) std::clamp (frame - startFrame, 0, (int) f0.size() - 1)];
}

float Note::medianMidi() const
{
    std::vector<float> v;
    for (auto f : f0)
        if (f > 0.0f)
            v.push_back (f);
    if (v.empty())
        return 0.0f;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return 69.0f + 12.0f * std::log2 (v[v.size() / 2] / 440.0f);
}

bool Model::edited() const noexcept
{
    return std::any_of (edits.begin(), edits.end(), [this] (const auto& e) { return e.first >= 0 && e.first < (int) notes.size() && ! e.second.identity(); });
}

bool analyse (const SampleData& source, const polyphonic::NoteEvidence* evidence, Model& out, const std::atomic<bool>* cancel,
              const std::function<void (float)>& progress)
{
    out = {};
    if (source.left.empty() || source.sampleRate <= 0.0)
        return true;
    const auto mono = monoOf (source);
    auto params = polyphonic::Params::forSampleRate (source.sampleRate);
    params.deferMasks = true; // (LYNX: the masks of the edited blocks are built when rendering)
    const auto useEvidence = evidence != nullptr && ! evidence->empty();
    const auto a = polyphonic::analyze (mono.data(), (int) mono.size(), params, useEvidence ? evidence : nullptr, cancel, progress);
    if (cancel != nullptr && cancel->load())
        return false;
    out.sampleRate = source.sampleRate;
    out.frames = (std::int64_t) source.left.size();
    out.fftSize = params.fftSize;
    out.hop = params.hop;
    out.evidence = useEvidence;
    for (const auto& n : a.notes)
    {
        if (! n.active)
            break; // (active notes come first; the rejected candidates are not kept)
        Note note;
        note.startFrame = n.startFrame;
        note.endFrame = n.endFrame;
        note.f0 = n.f0;
        note.inharmonicity = n.inharmonicity;
        note.energy = n.energy;
        note.probability = n.probability;
        out.notes.push_back (std::move (note));
    }
    return true;
}

std::vector<std::uint8_t> write (const Model& m)
{
    std::vector<std::uint8_t> b { 'L', 'X', 'P', 'E' };
    Out o { b };
    o.put (formatVersion);
    o.put (m.sampleRate);
    o.put ((std::int64_t) m.frames);
    o.put ((std::int32_t) m.fftSize);
    o.put ((std::int32_t) m.hop);
    o.put ((std::uint8_t) ((m.evidence ? 1u : 0u) | (m.takeBackground ? 2u : 0u)));
    o.put ((std::uint32_t) m.notes.size());
    for (const auto& n : m.notes)
    {
        o.put ((std::int32_t) n.startFrame);
        o.put ((std::int32_t) n.endFrame);
        o.put (n.inharmonicity);
        o.put (n.energy);
        o.put (n.probability);
        o.put ((std::uint32_t) n.f0.size());
        for (auto f : n.f0)
            o.put (f);
    }
    std::uint32_t edits = 0;
    for (const auto& [k, e] : m.edits)
        edits += ! e.identity() ? 1u : 0u;
    o.put (edits);
    for (const auto& [k, e] : m.edits)
    {
        if (e.identity())
            continue;
        o.put ((std::int32_t) k);
        o.put (e.semitones);
        o.put (e.gainDb);
        o.put (e.moveSeconds);
        o.put (e.stretch);
        o.put ((std::uint8_t) (e.mute ? 1 : 0));
    }
    return b;
}

std::optional<Model> parse (const std::vector<std::uint8_t>& blob)
{
    if (blob.size() < 8 || blob[0] != 'L' || blob[1] != 'X' || blob[2] != 'P' || blob[3] != 'E')
        return std::nullopt;
    In in { blob, 4 };
    const auto version = in.get<std::uint32_t>();
    if (version < 1 || version > formatVersion)
        return std::nullopt;
    Model m;
    m.sampleRate = in.get<double>();
    m.frames = in.get<std::int64_t>();
    m.fftSize = in.get<std::int32_t>();
    m.hop = in.get<std::int32_t>();
    const auto flags = in.get<std::uint8_t>();
    m.evidence = (flags & 1u) != 0;
    m.takeBackground = (flags & 2u) != 0;
    const auto count = in.get<std::uint32_t>();
    if (! in.ok || count > blob.size())
        return std::nullopt;
    for (std::uint32_t i = 0; i < count && in.ok; ++i)
    {
        Note n;
        n.startFrame = in.get<std::int32_t>();
        n.endFrame = in.get<std::int32_t>();
        n.inharmonicity = in.get<float>();
        n.energy = in.get<double>();
        n.probability = in.get<float>();
        const auto frames = in.get<std::uint32_t>();
        if (! in.ok || (std::size_t) frames * 4 > blob.size() - in.at)
            return std::nullopt;
        n.f0.resize (frames);
        for (auto& f : n.f0)
            f = in.get<float>();
        m.notes.push_back (std::move (n));
    }
    const auto edits = in.get<std::uint32_t>();
    for (std::uint32_t i = 0; i < edits && in.ok; ++i)
    {
        const auto k = in.get<std::int32_t>();
        Edit e;
        e.semitones = in.get<float>();
        e.gainDb = in.get<float>();
        e.moveSeconds = in.get<double>();
        e.stretch = in.get<float>();
        e.mute = in.get<std::uint8_t>() != 0;
        if (k >= 0 && k < (int) m.notes.size())
            m.edits[k] = e;
    }
    if (! in.ok || m.fftSize <= 0 || m.hop <= 0 || m.sampleRate <= 0.0)
        return std::nullopt;
    return m;
}

std::unique_ptr<polyphonic::Analysis> toAnalysis (const Model& m)
{
    auto a = std::make_unique<polyphonic::Analysis>();
    a->params = polyphonic::Params::forSampleRate (m.sampleRate);
    a->params.fftSize = m.fftSize;
    a->params.hop = m.hop;
    a->params.deferMasks = true;
    a->numSamples = (int) m.frames;
    a->numFrames = 1 + (int) ((m.frames + m.hop - 1) / m.hop); // (PitchNet's StftFrames)
    a->numBins = m.fftSize / 2 + 1;
    int id = 0;
    for (const auto& n : m.notes)
    {
        polyphonic::Note p;
        p.id = id++;
        p.startFrame = n.startFrame;
        p.endFrame = n.endFrame;
        p.f0 = n.f0;
        p.inharmonicity = n.inharmonicity;
        p.energy = n.energy;
        p.probability = n.probability;
        p.active = true;
        a->notes.push_back (std::move (p));
    }
    polyphonic::releaseMasks (*a);
    return a;
}

namespace
{
bool prepare (const SampleData& source, const std::map<int, polyphonic::NoteEdit>& edits, polyphonic::Analysis& a, const std::vector<float>& mono,
              const std::atomic<bool>* cancel)
{
    for (const auto& [lo, hi] : polyphonic::framesForEdits (a, edits))
        if (! polyphonic::ensureMasks (a, mono.data(), (int) mono.size(), lo, hi, cancel))
            return false;
    (void) source;
    return true;
}

std::shared_ptr<SampleData> fromChannels (const SampleData& source, std::vector<std::vector<float>> channels)
{
    auto out = std::make_shared<SampleData>();
    out->sampleRate = source.sampleRate;
    out->loopStart = source.loopStart;
    out->loopEnd = source.loopEnd;
    out->markerStarts = source.markerStarts;
    out->acidBeats = source.acidBeats;
    out->sliceMarkers = source.sliceMarkers;
    out->bitsPerSample = source.bitsPerSample;
    if (! channels.empty())
        out->left = std::move (channels[0]);
    if (channels.size() > 1)
        out->right = std::move (channels[1]);
    return out;
}
} // namespace

std::shared_ptr<SampleData> render (const SampleData& source, const Model& model, polyphonic::Analysis* analysis, const std::atomic<bool>* cancel)
{
    if (! fits (model, source) || ! model.edited())
        return nullptr;
    std::unique_ptr<polyphonic::Analysis> own;
    if (analysis == nullptr)
    {
        own = toAnalysis (model);
        analysis = own.get();
    }
    const auto edits = editsOf (model);
    const auto mono = monoOf (source);
    if (! prepare (source, edits, *analysis, mono, cancel))
        return nullptr;
    auto channels = polyphonic::render (*analysis, channelsOf (source), edits, cancel);
    if (cancel != nullptr && cancel->load())
        return nullptr;
    return fromChannels (source, std::move (channels));
}

std::shared_ptr<SampleData> renderNote (const SampleData& source, const Model& model, int note, polyphonic::Analysis& analysis, std::int64_t* from)
{
    if (! fits (model, source) || note < 0 || note >= (int) model.notes.size())
        return nullptr;
    auto edits = editsOf (model);
    if (edits.count (note) == 0)
    {
        polyphonic::NoteEdit plain;
        plain.takeBackground = model.takeBackground;
        edits[note] = plain;
    }
    const std::map<int, polyphonic::NoteEdit> one { { note, edits[note] } };
    const auto mono = monoOf (source);
    // (framesForEdits ignores identity edits for the span: ask for the note's own span with a tiny edit)
    auto span = one;
    if (span.begin()->second.isIdentity())
        span.begin()->second.gainDb = 1e-6f;
    if (! prepare (source, span, analysis, mono, nullptr))
        return nullptr;
    auto part = polyphonic::renderNotes (analysis, channelsOf (source), { note }, one);
    if (part.channels.empty() || part.channels[0].empty())
        return nullptr;
    if (from != nullptr)
        *from = part.start;
    return fromChannels (source, std::move (part.channels));
}

// --- the render cache --------------------------------------------------------------------------------------------------
namespace
{
struct Cached
{
    std::string key;
    std::uint64_t state = 0;
    std::shared_ptr<const SampleData> audio;
};
std::mutex cacheLock;
std::list<Cached> cache; // most recent first
// at most 6 renders, two edit states per sample (now and the one before: undo), and ~400 MB in all (a 5-minute stereo
// render is ~110 MB); the newest one always stays
constexpr std::size_t cacheEntries = 6, statesPerKey = 2, cacheBytes = (std::size_t) 400 << 20;

std::size_t bytesOf (const SampleData& s) noexcept
{
    return (s.left.size() + s.right.size()) * sizeof (float);
}

std::shared_ptr<const SampleData> lookup (const std::string& key, std::uint64_t state)
{
    const std::lock_guard<std::mutex> lock (cacheLock);
    for (auto it = cache.begin(); it != cache.end(); ++it)
        if (it->key == key && it->state == state)
        {
            cache.splice (cache.begin(), cache, it);
            return cache.front().audio;
        }
    return nullptr;
}

void store (const std::string& key, std::uint64_t state, std::shared_ptr<const SampleData> audio)
{
    const std::lock_guard<std::mutex> lock (cacheLock);
    cache.remove_if ([&] (const Cached& c) { return c.key == key && c.state == state; });
    cache.push_front ({ key, state, std::move (audio) });
    std::size_t sameKey = 0, total = 0;
    for (auto it = cache.begin(); it != cache.end();)
    {
        const auto bytes = it->audio != nullptr ? bytesOf (*it->audio) : 0;
        const auto keep = it == cache.begin()
                          || ((it->key != key || ++sameKey < statesPerKey) && total + bytes <= cacheBytes
                              && (std::size_t) std::distance (cache.begin(), it) < cacheEntries);
        if (! keep)
        {
            it = cache.erase (it);
            continue;
        }
        if (it == cache.begin() && it->key == key)
            sameKey = 0;
        total += bytes;
        ++it;
    }
}
} // namespace

std::shared_ptr<const SampleData> renderedFor (const std::string& key, std::shared_ptr<const SampleData> source, const std::vector<std::uint8_t>& blob)
{
    if (blob.empty() || source == nullptr)
        return source;
    const auto state = fnv (blob);
    if (auto hit = lookup (key, state))
        return hit;
    const auto model = parse (blob);
    if (! model || ! model->edited() || ! fits (*model, *source))
        return source;
    std::shared_ptr<const SampleData> out = render (*source, *model);
    if (out == nullptr)
        return source;
    store (key, state, out);
    return out;
}

void remember (const std::string& key, const std::vector<std::uint8_t>& blob, std::shared_ptr<const SampleData> rendered)
{
    if (! blob.empty() && rendered != nullptr)
        store (key, fnv (blob), std::move (rendered));
}

std::string stateKey (const std::vector<std::uint8_t>& blob)
{
    if (blob.empty())
        return {};
    char text[24];
    std::snprintf (text, sizeof text, "pe%016llx", (unsigned long long) fnv (blob));
    return text;
}

void clearCache()
{
    const std::lock_guard<std::mutex> lock (cacheLock);
    cache.clear();
}
} // namespace lynx::polyedit
