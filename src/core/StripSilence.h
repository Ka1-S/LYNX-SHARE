#pragma once

// Split at silence (round 190hp, slice B18 of the 0.1.28 pain report, complaint #32: "strip silence"). LYNX's own: FL
// strips silence inside Edison only (Tools › Trim side noise / Trim all noise & slice up, which edit the sample itself;
// LYNX's audio editor has both, FL-exact) and the reference DAW has no such command. The Playlist's audio clip menu,
// LYNX extras › "Split at silence...", cuts the clip where its audio stays below a threshold and leaves those quiet
// parts out: the clips that remain play the same audio at the same time; the file is not changed. One undo step.
//   - threshold: the peak level (dB) below which the audio counts as quiet, measured in 5 ms windows (as Edison's tools);
//   - minimum silence: a quieter stretch shorter than this stays inside the clip (pauses between words);
//   - pre-roll / post-roll: each remaining clip starts this much before the sound and ends this much after it (the
//     attack's lead-in and the tail); clips that would then overlap join.
// JUCE-free; tests in tests/Community190hpTests.inc.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::stripsilence
{
struct Options
{
    float thresholdDb = -40.0f;
    double minSilenceMs = 200.0;
    double preRollMs = 10.0;
    double postRollMs = 50.0;
};

struct Span
{
    std::int64_t from = 0, to = 0; // [from, to)
    bool operator== (const Span&) const = default;
};

// The parts of frames [from, to) to keep, in frames. Empty = all of it is quiet; one span [from, to) = no silence
// to strip. `right` may be the same pointer as `left` (mono).
inline std::vector<Span> keptSpans (const float* left, const float* right, std::int64_t frames, double rate, std::int64_t from, std::int64_t to,
                                    const Options& o)
{
    std::vector<Span> out;
    from = std::clamp<std::int64_t> (from, 0, frames);
    to = std::clamp<std::int64_t> (to, from, frames);
    if (left == nullptr || to <= from || ! (rate > 0.0))
        return out;
    const auto threshold = std::pow (10.0f, std::clamp (o.thresholdDb, -120.0f, 0.0f) / 20.0f);
    const auto window = std::max<std::int64_t> (16, (std::int64_t) (rate * 0.005));
    const auto minGap = (std::int64_t) (std::max (0.0, o.minSilenceMs) * 0.001 * rate);
    const auto pre = (std::int64_t) (std::max (0.0, o.preRollMs) * 0.001 * rate);
    const auto post = (std::int64_t) (std::max (0.0, o.postRollMs) * 0.001 * rate);

    // the sound: runs of windows whose peak reaches the threshold
    std::vector<Span> loud;
    for (auto w = from; w < to; w += window)
    {
        const auto e = std::min (to, w + window);
        float peak = 0.0f;
        for (auto i = w; i < e; ++i)
            peak = std::max ({ peak, std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f });
        if (! (peak >= threshold)) // (a NaN counts as quiet)
            continue;
        if (! loud.empty() && loud.back().to == w)
            loud.back().to = e;
        else
            loud.push_back ({ w, e });
    }
    // a quiet stretch shorter than the minimum stays inside (a quiet start or end is cut whatever its length)
    for (const auto& s : loud)
        if (! out.empty() && s.from - out.back().to < minGap)
            out.back().to = s.to;
        else
            out.push_back (s);
    // the rolls, then the spans that now touch join
    std::vector<Span> rolled;
    for (auto s : out)
    {
        s.from = std::max (from, s.from - pre);
        s.to = std::min (to, s.to + post);
        if (! rolled.empty() && s.from <= rolled.back().to)
            rolled.back().to = std::max (rolled.back().to, s.to);
        else
            rolled.push_back (s);
    }
    return rolled;
}

// Clip `index` becomes the clips of `pieces` (ticks from the clip's start, sorted, inside [0, length)); the parts between
// them go. Each new clip plays its part of the file where it played before (start and offset move together). The first
// piece keeps the clip's place in the list, the others are appended. The clip's fade in stays on the first piece if it
// starts with the clip, its fade out on the last one if it ends with it. Returns the clips' indices (empty: unchanged).
inline std::vector<int> splitClip (std::vector<Clip>& clips, int index, std::vector<Span> pieces)
{
    if (index < 0 || index >= (int) clips.size())
        return {};
    const auto clip = clips[(std::size_t) index];
    for (auto& p : pieces)
    {
        p.from = std::clamp<std::int64_t> (p.from, 0, clip.length);
        p.to = std::clamp<std::int64_t> (p.to, p.from, clip.length);
    }
    pieces.erase (std::remove_if (pieces.begin(), pieces.end(), [] (const Span& p) { return p.to - p.from < 1; }), pieces.end());
    if (pieces.empty() || (pieces.size() == 1 && pieces[0].from == 0 && pieces[0].to == clip.length))
        return {};
    std::vector<int> made;
    for (std::size_t k = 0; k < pieces.size(); ++k)
    {
        auto c = clip;
        c.start = clip.start + pieces[k].from;
        c.offset = clip.offset + pieces[k].from;
        c.length = pieces[k].to - pieces[k].from;
        c.fadeIn = pieces[k].from == 0 ? std::min (clip.fadeIn, c.length) : 0;
        c.fadeOut = pieces[k].to == clip.length ? std::min (clip.fadeOut, c.length - c.fadeIn) : 0;
        if (pieces[k].from != 0)
            c.fadeInTension = 0.0f;
        if (pieces[k].to != clip.length)
            c.fadeOutTension = 0.0f;
        c.crossfadeIn = c.crossfadeOut = 0; // (automatic crossfades are worked out again after the edit)
        if (k == 0)
        {
            clips[(std::size_t) index] = c;
            made.push_back (index);
        }
        else
        {
            clips.push_back (c);
            made.push_back ((int) clips.size() - 1);
        }
    }
    return made;
}
} // namespace lynx::stripsilence
