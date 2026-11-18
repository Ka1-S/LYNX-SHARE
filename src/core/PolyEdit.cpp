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
// 1 = round 190lk; 2 = round 190lo (candidates: Note::active; Edit::modulation / drift / curve)
constexpr std::uint32_t formatVersion = 2;

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
        if (k < 0 || k >= (int) m.notes.size() || e.identity() || ! m.notes[(std::size_t) k].active)
            continue;
        polyphonic::NoteEdit n;
        n.pitchCurve = pitchCurveOf (m.notes[(std::size_t) k], e, m.frameSeconds());
        n.semitones = e.semitones;
        n.gainDb = e.gainDb;
        n.mute = e.mute;
        n.timeOffsetSeconds = e.moveSeconds;
        n.stretch = std::clamp (e.stretch, 0.25f, 4.0f);
        n.takeBackground = m.takeBackground;
        out[analysisIndex (m, k)] = n;
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

bool Edit::pitchMoves() const noexcept
{
    return modulation != 1.0f || drift != 1.0f || std::any_of (curve.begin(), curve.end(), [] (float v) { return v != 0.0f; });
}

bool Model::edited() const noexcept
{
    return std::any_of (edits.begin(), edits.end(), [this] (const auto& e) {
        return e.first >= 0 && e.first < (int) notes.size() && notes[(std::size_t) e.first].active && ! e.second.identity();
    });
}

int analysisIndex (const Model& m, int k)
{
    if (k < 0 || k >= (int) m.notes.size())
        return -1;
    int before = 0, active = 0;
    for (int i = 0; i < (int) m.notes.size(); ++i)
    {
        const auto a = m.notes[(std::size_t) i].active;
        active += a ? 1 : 0;
        if (i < k && a == m.notes[(std::size_t) k].active)
            ++before;
    }
    return m.notes[(std::size_t) k].active ? before : active + before;
}

std::vector<int> activeNotes (const Model& m)
{
    std::vector<int> out;
    for (int i = 0; i < (int) m.notes.size(); ++i)
        if (m.notes[(std::size_t) i].active)
            out.push_back (i);
    return out;
}

namespace
{
// two passes of a centred moving average of `width` frames (190li's drift filter), over the voiced frames
std::vector<float> smoothed (const std::vector<float>& x, int width)
{
    auto pass = [width] (const std::vector<float>& in) {
        std::vector<float> o (in.size());
        const int h = std::max (0, width / 2);
        for (int i = 0; i < (int) in.size(); ++i)
        {
            double sum = 0.0;
            int n = 0;
            for (int j = std::max (0, i - h); j <= std::min ((int) in.size() - 1, i + h); ++j)
                sum += in[(std::size_t) j], ++n;
            o[(std::size_t) i] = (float) (sum / std::max (1, n));
        }
        return o;
    };
    return pass (pass (x));
}
} // namespace

PitchParts pitchParts (const Note& n, double frameSeconds)
{
    PitchParts out;
    out.centre = n.medianMidi();
    if (n.f0.empty() || frameSeconds <= 0.0)
        return out;
    std::vector<float> dev (n.f0.size());
    for (std::size_t i = 0; i < dev.size(); ++i)
        dev[i] = n.f0[i] > 0.0f ? 69.0f + 12.0f * std::log2 (n.f0[i] / 440.0f) - out.centre : 0.0f;
    // drift: 2 x 180 ms (a 5.5 Hz vibrato's period: it is removed); modulation: the rest after 2 x 35 ms (the frame-to-frame
    // noise of the pitch track stays as it is, it is not scaled)
    const auto slow = smoothed (dev, std::max (1, (int) std::lround (0.18 / frameSeconds)));
    const auto fine = smoothed (dev, std::max (1, (int) std::lround (0.035 / frameSeconds)));
    out.drift = slow;
    out.modulation.resize (dev.size());
    for (std::size_t i = 0; i < dev.size(); ++i)
        out.modulation[i] = fine[i] - slow[i];
    return out;
}

std::vector<float> pitchCurveOf (const Note& n, const Edit& e, double frameSeconds)
{
    if (! e.pitchMoves() || n.f0.empty())
        return {};
    std::vector<float> c (n.f0.size(), 0.0f);
    if (e.modulation != 1.0f || e.drift != 1.0f)
    {
        const auto parts = pitchParts (n, frameSeconds);
        for (std::size_t i = 0; i < c.size(); ++i)
            c[i] = (e.drift - 1.0f) * parts.drift[i] + (e.modulation - 1.0f) * parts.modulation[i];
    }
    for (std::size_t i = 0; i < c.size() && i < e.curve.size(); ++i)
        c[i] += e.curve[i];
    return c;
}

namespace
{
Note noteOf (const polyphonic::Note& n)
{
    Note note;
    note.startFrame = n.startFrame;
    note.endFrame = n.endFrame;
    note.f0 = n.f0;
    note.inharmonicity = n.inharmonicity;
    note.energy = n.energy;
    note.probability = n.probability;
    note.active = n.active;
    return note;
}

bool covers (const Note& n, double midi, int frame, float tolerance = 0.6f)
{
    return frame >= n.startFrame && frame <= n.endFrame && n.f0At (frame) > 0.0f
           && std::abs (69.0f + 12.0f * std::log2 (n.f0At (frame) / 440.0f) - (float) midi) < tolerance;
}

// Round 190lo: the regions (frame ranges) where Basic Pitch hears a note no active note covers
std::vector<std::pair<int, int>> missedRegions (const std::vector<Note>& notes, const polyphonic::NoteEvidence& ev, double frameSeconds, int numFrames)
{
    constexpr float sure = 0.5f;     // posterior of a sounding note
    constexpr double minSeconds = 0.25, joinSeconds = 0.3, padSeconds = 0.5;
    std::vector<char> missed ((std::size_t) std::max (0, numFrames), 0);
    const auto bands = ev.frameTimes.empty() ? 0 : ev.note.size() / ev.frameTimes.size();
    for (std::size_t f = 0; f < ev.frameTimes.size(); ++f)
    {
        const int frame = (int) std::lround (ev.frameTimes[f] / frameSeconds);
        if (frame < 0 || frame >= numFrames)
            continue;
        for (std::size_t b = 0; b < bands; ++b)
        {
            if (ev.note[f * bands + b] < sure)
                continue;
            const double midi = 21.0 + (double) b;
            const auto hit = std::any_of (notes.begin(), notes.end(), [&] (const Note& n) { return n.active && covers (n, midi, frame); });
            if (! hit)
                missed[(std::size_t) frame] = 1;
        }
    }
    std::vector<std::pair<int, int>> runs;
    const int join = (int) std::lround (joinSeconds / frameSeconds), minLen = (int) std::lround (minSeconds / frameSeconds);
    for (int t = 0; t < numFrames; ++t)
    {
        if (! missed[(std::size_t) t])
            continue;
        if (! runs.empty() && t - runs.back().second <= join)
            runs.back().second = t;
        else
            runs.push_back ({ t, t });
    }
    std::vector<std::pair<int, int>> out;
    const int pad = (int) std::lround (padSeconds / frameSeconds);
    for (const auto& [a, b] : runs)
    {
        // (count the missed frames: a few stray posteriors are not a missed note)
        int count = 0;
        for (int t = a; t <= b; ++t)
            count += missed[(std::size_t) t];
        if (count < minLen)
            continue;
        const auto lo = std::max (0, a - pad), hi = std::min (numFrames - 1, b + pad);
        if (! out.empty() && lo <= out.back().second)
            out.back().second = hi;
        else
            out.push_back ({ lo, hi });
    }
    return out;
}
} // namespace

namespace
{
// Basic Pitch's mean note posterior at a note's semitone over its frames (-1 = no evidence there)
double support (const Note& n, const polyphonic::NoteEvidence& ev, double frameSeconds)
{
    if (ev.empty())
        return -1.0;
    const auto bands = ev.note.size() / ev.frameTimes.size();
    const auto band = (long) std::lround (n.medianMidi()) - 21;
    if (band < 0 || band >= (long) bands)
        return -1.0;
    double sum = 0.0;
    int count = 0;
    for (std::size_t f = 0; f < ev.frameTimes.size(); ++f)
        if (ev.frameTimes[f] >= n.startFrame * frameSeconds && ev.frameTimes[f] <= n.endFrame * frameSeconds)
            sum += ev.note[f * bands + (std::size_t) band], ++count;
    return count > 0 ? sum / count : -1.0;
}

// Round 190lo: a note split at slow re-attacks (Params::slowReattacks) leaves pieces of one pitch end to end; a piece
// Basic Pitch hardly hears while another piece of the chain is clearly heard is a tail the tracker carried on, not a
// note: it becomes a candidate
void dropUnheardPieces (std::vector<Note>& active, std::vector<Note>& candidates, const polyphonic::NoteEvidence& ev, double frameSeconds)
{
    constexpr double quiet = 0.15, share = 0.25;
    std::vector<int> chain (active.size(), -1);
    int chains = 0;
    for (std::size_t i = 0; i < active.size(); ++i)
    {
        if (chain[i] < 0)
            chain[i] = chains++;
        for (std::size_t j = 0; j < active.size(); ++j)
            if (j != i && chain[j] < 0 && std::abs (active[j].startFrame - (active[i].endFrame + 1)) <= 1
                && std::abs (active[j].medianMidi() - active[i].medianMidi()) < 0.3f)
                chain[j] = chain[i];
    }
    std::vector<double> heard (active.size()), best ((std::size_t) chains, 0.0);
    std::vector<int> pieces ((std::size_t) chains, 0);
    for (std::size_t i = 0; i < active.size(); ++i)
    {
        heard[i] = support (active[i], ev, frameSeconds);
        best[(std::size_t) chain[i]] = std::max (best[(std::size_t) chain[i]], heard[i]);
        ++pieces[(std::size_t) chain[i]];
    }
    std::vector<Note> kept;
    for (std::size_t i = 0; i < active.size(); ++i)
    {
        const auto c = (std::size_t) chain[i];
        if (pieces[c] > 1 && heard[i] >= 0.0 && heard[i] < quiet && heard[i] < share * best[c])
        {
            active[i].active = false;
            candidates.push_back (std::move (active[i]));
        }
        else
            kept.push_back (std::move (active[i]));
    }
    active = std::move (kept);
}
// Round 190lo: a candidate the note filter only just rejected that Basic Pitch clearly hears is a note, unless it sits
// an octave (or two) above a note sounding with it: octave ghosts are where both the filter and Basic Pitch are least sure
void rescueHeardCandidates (std::vector<Note>& active, std::vector<Note>& candidates, const polyphonic::NoteEvidence& ev, double frameSeconds)
{
    constexpr float nearlyNote = 0.35f;   // note filter probability (a note from 0.5)
    constexpr double heard = 0.3;         // Basic Pitch's mean posterior at its semitone
    constexpr double minSeconds = 0.2;
    std::vector<Note> kept;
    const auto before = active.size();
    for (auto& c : candidates)
    {
        const auto midi = c.medianMidi();
        const auto octaveAbove = std::any_of (active.begin(), active.begin() + (long) before, [&] (const Note& a) {
            const auto overlap = std::min (a.endFrame, c.endFrame) - std::max (a.startFrame, c.startFrame);
            const auto up = midi - a.medianMidi();
            return overlap > 0 && (std::abs (up - 12.0f) < 0.5f || std::abs (up - 24.0f) < 0.5f);
        });
        const auto doubles = std::any_of (active.begin(), active.end(), [&] (const Note& a) {
            return std::min (a.endFrame, c.endFrame) > std::max (a.startFrame, c.startFrame) && std::abs (a.medianMidi() - midi) < 0.6f;
        });
        if (c.probability >= nearlyNote && (c.endFrame - c.startFrame) * frameSeconds >= minSeconds && ! octaveAbove && ! doubles
            && support (c, ev, frameSeconds) >= heard)
        {
            c.active = true;
            active.push_back (std::move (c));
        }
        else
            kept.push_back (std::move (c));
    }
    candidates = std::move (kept);
}
} // namespace

bool analyse (const SampleData& source, const polyphonic::NoteEvidence* evidence, Model& out, const std::atomic<bool>* cancel,
              const std::function<void (float)>& progress, const AnalyseOptions& options)
{
    out = {};
    if (source.left.empty() || source.sampleRate <= 0.0)
        return true;
    const auto mono = monoOf (source);
    auto params = polyphonic::Params::forSampleRate (source.sampleRate);
    params.deferMasks = true; // (LYNX: the masks of the edited blocks are built when rendering)
    params.slowReattacks = options.slowReattacks;
    const auto useEvidence = evidence != nullptr && ! evidence->empty();
    const auto detector = useEvidence ? options.detector : Detector::harmonic;
    if (detector == Detector::guided)
        params.detectionMode = polyphonic::DetectionMode::NeuralGuided;
    const auto share = detector == Detector::fallback ? 0.8f : 1.0f;
    const auto a = polyphonic::analyze (mono.data(), (int) mono.size(), params, useEvidence ? evidence : nullptr, cancel,
                                        [&progress, share] (float v) { if (progress) progress (share * v); });
    if (cancel != nullptr && cancel->load())
        return false;
    out.sampleRate = source.sampleRate;
    out.frames = (std::int64_t) source.left.size();
    out.fftSize = params.fftSize;
    out.hop = params.hop;
    out.evidence = useEvidence;
    std::vector<Note> active, candidates;
    for (const auto& n : a.notes)
        (n.active ? active : candidates).push_back (noteOf (n));
    if (options.slowReattacks && useEvidence)
        dropUnheardPieces (active, candidates, *evidence, out.frameSeconds());
    if (detector == Detector::fallback)
    {
        // Round 190lo: where Basic Pitch hears notes the harmonic detector missed (pads, detuned saws, slow attacks), the
        // guided detector looks again over that region (whole hops, so its frames are the same frames); its notes that cover
        // a missed note and do not double one already there are added
        const auto regions = missedRegions (active, *evidence, out.frameSeconds(), a.numFrames);
        auto guided = params;
        guided.detectionMode = polyphonic::DetectionMode::NeuralGuided;
        for (std::size_t r = 0; r < regions.size(); ++r)
        {
            const auto [lo, hi] = regions[r];
            const auto from = (std::int64_t) lo * params.hop, to = std::min ((std::int64_t) mono.size(), (std::int64_t) (hi + 1) * params.hop);
            if (to - from < params.fftSize)
                continue;
            polyphonic::NoteEvidence part;
            const auto bands = evidence->note.size() / evidence->frameTimes.size();
            const auto t0 = (double) from / source.sampleRate, t1 = (double) to / source.sampleRate;
            for (std::size_t f = 0; f < evidence->frameTimes.size(); ++f)
                if (evidence->frameTimes[f] >= t0 && evidence->frameTimes[f] <= t1)
                {
                    part.frameTimes.push_back ((float) (evidence->frameTimes[f] - t0));
                    part.note.insert (part.note.end(), evidence->note.begin() + (long) (f * bands), evidence->note.begin() + (long) ((f + 1) * bands));
                }
            if (part.empty())
                continue;
            const auto g = polyphonic::analyze (mono.data() + from, (int) (to - from), guided, &part, cancel);
            if (cancel != nullptr && cancel->load())
                return false;
            for (const auto& gn : g.notes)
            {
                if (! gn.active)
                    continue;
                auto n = noteOf (gn);
                n.startFrame += lo;
                n.endFrame += lo;
                const auto midi = n.medianMidi();
                const auto doubles = std::any_of (active.begin(), active.end(), [&] (const Note& o) {
                    const auto overlap = std::min (o.endFrame, n.endFrame) - std::max (o.startFrame, n.startFrame);
                    return overlap > 0 && std::abs (o.medianMidi() - midi) < 0.6f;
                });
                // (not an octave or two above a note sounding with it: the octave ghosts the detectors are least sure of)
                const auto octaveAbove = std::any_of (active.begin(), active.end(), [&] (const Note& o) {
                    const auto overlap = std::min (o.endFrame, n.endFrame) - std::max (o.startFrame, n.startFrame);
                    const auto up = midi - o.medianMidi();
                    return overlap > 0 && (std::abs (up - 12.0f) < 0.5f || std::abs (up - 24.0f) < 0.5f);
                });
                bool fills = false;
                for (int t = n.startFrame; t <= n.endFrame && ! fills; t += 4)
                    fills = ! std::any_of (active.begin(), active.end(), [&] (const Note& o) { return covers (o, midi, t); });
                if (! doubles && ! octaveAbove && fills)
                    active.push_back (std::move (n));
            }
            if (progress)
                progress (0.8f + 0.2f * (float) (r + 1) / (float) regions.size());
        }
    }
    if (options.rescueHeard && useEvidence)
        rescueHeardCandidates (active, candidates, *evidence, out.frameSeconds());
    std::stable_sort (active.begin(), active.end(), [] (const Note& x, const Note& y) {
        return x.startFrame != y.startFrame ? x.startFrame < y.startFrame : x.medianMidi() < y.medianMidi();
    });
    out.notes = std::move (active);
    if (options.keepCandidates)
        for (auto& c : candidates)
            if (c.probability >= options.candidateFloor && (c.endFrame - c.startFrame + 1) * out.frameSeconds() >= 0.1)
                out.notes.push_back (std::move (c));
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
    o.put ((std::uint8_t) ((m.evidence ? 1u : 0u) | (m.takeBackground ? 2u : 0u) | (m.overlapAwareMoves ? 4u : 0u)));
    o.put ((std::uint32_t) m.notes.size());
    for (const auto& n : m.notes)
    {
        o.put ((std::int32_t) n.startFrame);
        o.put ((std::int32_t) n.endFrame);
        o.put (n.inharmonicity);
        o.put (n.energy);
        o.put (n.probability);
        o.put ((std::uint8_t) (n.active ? 1 : 0)); // (version 2)
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
        // (version 2)
        o.put (e.modulation);
        o.put (e.drift);
        o.put ((std::uint32_t) e.curve.size());
        for (auto v : e.curve)
            o.put (v);
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
    m.overlapAwareMoves = (flags & 4u) != 0;
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
        if (version >= 2)
            n.active = in.get<std::uint8_t>() != 0;
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
        if (version >= 2)
        {
            e.modulation = in.get<float>();
            e.drift = in.get<float>();
            const auto points = in.get<std::uint32_t>();
            if (! in.ok || (std::size_t) points * 4 > blob.size() - in.at)
                return std::nullopt;
            e.curve.resize (points);
            for (auto& v : e.curve)
                v = in.get<float>();
        }
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
    a->params.overlapAwareMoves = m.overlapAwareMoves; // (round 190lo)
    a->numSamples = (int) m.frames;
    a->numFrames = 1 + (int) ((m.frames + m.hop - 1) / m.hop); // (PitchNet's StftFrames)
    a->numBins = m.fftSize / 2 + 1;
    int id = 0;
    // (the engine wants the active notes first, the candidates after them: analysisIndex())
    std::vector<const Note*> order;
    for (const auto& n : m.notes)
        if (n.active)
            order.push_back (&n);
    for (const auto& n : m.notes)
        if (! n.active)
            order.push_back (&n);
    for (const auto* np : order)
    {
        const auto& n = *np;
        polyphonic::Note p;
        p.id = id++;
        p.startFrame = n.startFrame;
        p.endFrame = n.endFrame;
        p.f0 = n.f0;
        p.inharmonicity = n.inharmonicity;
        p.energy = n.energy;
        p.probability = n.probability;
        p.active = n.active;
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
    if (! fits (model, source) || note < 0 || note >= (int) model.notes.size() || ! model.notes[(std::size_t) note].active)
        return nullptr;
    const auto index = analysisIndex (model, note);
    auto edits = editsOf (model);
    if (edits.count (index) == 0)
    {
        polyphonic::NoteEdit plain;
        plain.takeBackground = model.takeBackground;
        edits[index] = plain;
    }
    const std::map<int, polyphonic::NoteEdit> one { { index, edits[index] } };
    const auto mono = monoOf (source);
    // (framesForEdits ignores identity edits for the span: ask for the note's own span with a tiny edit)
    auto span = one;
    if (span.begin()->second.isIdentity())
        span.begin()->second.gainDb = 1e-6f;
    if (! prepare (source, span, analysis, mono, nullptr))
        return nullptr;
    auto part = polyphonic::renderNotes (analysis, channelsOf (source), { index }, one);
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
// --- round 190lo (UI): analyses prepared ahead of time ---------------------------------------------------------------------
bool prepareMasks (const SampleData& source, const Model& model, polyphonic::Analysis& analysis, const std::atomic<bool>* cancel)
{
    if (! fits (model, source))
        return false;
    const auto edits = editsOf (model);
    if (edits.empty())
        return true;
    return prepare (source, edits, analysis, monoOf (source), cancel);
}

namespace
{
struct Prepared
{
    std::string key;
    std::vector<Note> notes;
    std::shared_ptr<polyphonic::Analysis> analysis;
};
std::mutex preparedLock;
std::list<Prepared> preparedStore; // most recent first
constexpr std::size_t preparedEntries = 4, preparedBytes = (std::size_t) 256 << 20;
} // namespace

void keepPrepared (const std::string& key, const Model& model, std::shared_ptr<polyphonic::Analysis> analysis)
{
    if (analysis == nullptr)
        return;
    const std::lock_guard<std::mutex> lock (preparedLock);
    preparedStore.remove_if ([&] (const Prepared& p) { return p.key == key; });
    preparedStore.push_front ({ key, model.notes, std::move (analysis) });
    std::size_t bytes = 0, count = 0;
    for (auto it = preparedStore.begin(); it != preparedStore.end();)
    {
        bytes += polyphonic::maskBytes (*it->analysis);
        if (++count > preparedEntries || (count > 1 && bytes > preparedBytes))
            it = preparedStore.erase (it);
        else
            ++it;
    }
}

std::shared_ptr<polyphonic::Analysis> takePrepared (const std::string& key, const Model& model)
{
    const std::lock_guard<std::mutex> lock (preparedLock);
    for (auto it = preparedStore.begin(); it != preparedStore.end(); ++it)
        if (it->key == key)
        {
            auto out = it->notes == model.notes ? it->analysis : nullptr;
            preparedStore.erase (it);
            return out;
        }
    return nullptr;
}

std::size_t preparedCount()
{
    const std::lock_guard<std::mutex> lock (preparedLock);
    return preparedStore.size();
}

void clearPrepared()
{
    const std::lock_guard<std::mutex> lock (preparedLock);
    preparedStore.clear();
}

bool sameNotes (const Model& a, const Model& b)
{
    if (a.notes.size() != b.notes.size())
        return false;
    for (std::size_t i = 0; i < a.notes.size(); ++i)
    {
        const auto& x = a.notes[i];
        const auto& y = b.notes[i];
        if (x.startFrame != y.startFrame || x.endFrame != y.endFrame || x.f0 != y.f0 || x.inharmonicity != y.inharmonicity)
            return false;
    }
    return true;
}
} // namespace lynx::polyedit
