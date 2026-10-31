#pragma once

// FL Studio's Sampler "Precomputed effects" (docs/analise/REA_rodada167h.md §2, from FLEngine's FUN_00ff0db0): the
// played region is cut out of the file and processed once, in FL's order — region crop with a declick at each cut
// edge, ping pong loop, reverse, fade in / fade out, fade stereo, remove DC, reverse polarity, normalize, loop
// crossfade, swap stereo, trim. The result is a new SampleData holding only that region (SampleData::cropped).
// Header-only, no JUCE: the rack builds it on the message thread when a channel's settings change.

#include "core/Instruments.h"
#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace lynx::precompute
{
// FL's region declick (FUN_006a0760): from the edge's first non-zero frame inward over `len` frames, a one-pole
// follower whose coefficient sweeps from 2/rate·1 Hz up to 1 (22050^(k/len)) is subtracted from the signal, and the
// last round(rate·0.001) frames before the edge get a raised-cosine window. `x` walks from the edge inward with `step`.
inline void declickEdge (float* x, std::int64_t frames, std::int64_t step, int len, double rate) noexcept
{
    std::int64_t skip = 0;
    while (skip < frames && x[skip * step] == 0.0f)
        ++skip;
    x += skip * step;
    const auto avail = frames - skip;
    if (avail < len)
        len = (int) avail;
    if (len <= 0)
        return;
    auto* p = x + step * (len - 1);
    const auto k0 = std::log (22050.0) / (double) len; // (FL: ln 22050 / (ln 2 · len) · ln 2)
    const auto m = (int) std::min<std::int64_t> ((std::int64_t) std::nearbyint (rate * 0.001), len);
    const auto windowFrom = len - m;
    double follow = 0.0, window = 1.0;
    for (int k = 1; k <= len; ++k, p -= step)
    {
        const auto c = std::min (1.0, 2.0 / rate * std::exp ((double) k * k0));
        follow += c * ((double) *p - follow);
        if (k > windowFrom)
            window = (std::cos ((double) (k - windowFrom) * (1.0 / (double) (m + 1)) * 3.141592653589793) + 1.0) * 0.5;
        *p = (float) (((double) *p - follow) * window);
    }
}

// Whether a channel's sample goes through the precompute: any effect on, or (an FL sampler channel, which FL crops
// with declicked edges) a region narrower than the file.
inline bool needed (const SamplerSettings& s) noexcept
{
    const auto region = std::min (s.regionStart, s.regionEnd) > 0.0f || s.regionEnd < 1.0f;
    return s.precompute.any() || (region && ! s.flChannel.empty());
}

// The processed region of `in` (see the file comment). Returns nullptr when nothing is to be done.
inline std::shared_ptr<SampleData> apply (const SampleData& in, const SamplerSettings& s)
{
    const auto frames = (std::int64_t) in.frames();
    if (frames < 2 || ! needed (s))
        return nullptr;
    const auto& p = s.precompute;
    const auto rate = in.sampleRate > 0.0 ? in.sampleRate : 44100.0;

    // 1. the region (FL: s0 = round(start·(N−1)), s1 = s0 + round(end'·(1−start)·(N−1)) with FL's relative end' —
    // LYNX's End knob is absolute, end − start is the same span); regions shorter than 41 frames grow by 41
    // (FUN_00ff0db0)
    const auto start = (double) std::clamp (std::min (s.regionStart, s.regionEnd), 0.0f, 1.0f);
    const auto end = (double) std::clamp (s.regionEnd, 0.0f, 1.0f);
    const auto total = (double) (frames - 1);
    auto s0 = (std::int64_t) std::nearbyint (start * total);
    auto s1 = std::min<std::int64_t> (frames - 1, s0 + (std::int64_t) std::nearbyint (std::max (0.0, end - start) * total));
    if (s1 - s0 < 41)
    {
        if (s0 < frames - 42)
            s1 = std::min<std::int64_t> (frames - 1, s1 + 41);
        else
            s0 = std::max<std::int64_t> (0, s0 - 41);
    }
    const auto n0 = s1 - s0 + 1;
    const auto stereo = in.isStereo() || p.fadeStereo; // fade stereo makes a mono sample stereo
    std::vector<float> L (in.leftData() + s0, in.leftData() + s0 + n0), R;
    if (stereo)
        R.assign (in.rightData() + s0, in.rightData() + s0 + n0);

    std::int64_t ls = -1, le = -1; // the loop, inclusive frames (SampleData's end is exclusive)
    if (s.useLoopPoints && in.loopStart >= 0 && in.loopEnd > in.loopStart + 1 && in.loopStart >= s0 && in.loopEnd - 1 <= s1)
        ls = in.loopStart - s0, le = in.loopEnd - 1 - s0;

    auto forEach = [&] (auto&& fn) {
        fn (L);
        if (stereo)
            fn (R);
    };
    // the region's cut edges: declick in 0.5 ms, out 4 ms (FUN_00699ca0 / FUN_006a09b0: rate · 0.005 / 0.04 · 0.1)
    if (start > 0.0)
        forEach ([&] (std::vector<float>& x) { declickEdge (x.data(), (std::int64_t) x.size(), 1, (int) std::nearbyint (rate * 0.005 * 0.1), rate); });
    if (end < 1.0)
        forEach ([&] (std::vector<float>& x) { declickEdge (x.data() + x.size() - 1, (std::int64_t) x.size(), -1, (int) std::nearbyint (rate * 0.04 * 0.1), rate); });

    // 3. ping pong: the loop's frames le−1 … ls+1 go after its end, reversed; the loop end follows
    if (p.pingPong && ls >= 0 && ls + 1 < le)
    {
        const auto extra = le - ls - 1;
        forEach ([&] (std::vector<float>& x) {
            std::vector<float> y;
            y.reserve (x.size() + (std::size_t) extra);
            y.insert (y.end(), x.begin(), x.begin() + le + 1);
            for (auto k = le - 1; k > ls; --k)
                y.push_back (x[(std::size_t) k]);
            y.insert (y.end(), x.begin() + le + 1, x.end());
            x.swap (y);
        });
        le += extra;
    }

    // the effects run in playback order: a reversed sample is reversed here, and back at the end (the voice reads it
    // backwards as before)
    const auto n = (std::int64_t) L.size();
    if (s.reverse)
    {
        forEach ([] (std::vector<float>& x) { std::reverse (x.begin(), x.end()); });
        if (ls >= 0)
            std::tie (ls, le) = std::pair { n - 1 - le, n - 1 - ls };
    }

    // 6. fade in / fade out: gains 1 − (N−1−i)·a/(256·(N−1)) and 1 − i·b/(256·(N−1)), floored at 0
    if ((p.fadeIn > 0 || p.fadeOut > 0) && n > 1)
    {
        const auto d = 1.0 / (double) (n - 1) / 256.0;
        forEach ([&] (std::vector<float>& x) {
            for (std::int64_t i = 0; i < n; ++i)
            {
                auto v = x[(std::size_t) i];
                if (p.fadeIn > 0)
                {
                    const auto g = (float) (1.0 - d * (double) (n - 1 - i) * (double) p.fadeIn);
                    v = g <= 0.0f ? 0.0f : v * g;
                }
                if (p.fadeOut > 0)
                {
                    const auto g = (float) (1.0 - (double) i * d * (double) p.fadeOut);
                    v = g <= 0.0f ? 0.0f : v * g;
                }
                x[(std::size_t) i] = v;
            }
        });
    }
    // fade stereo: left × cos(i·π/2/(N−1)), right × cos((N−1−i)·π/2/(N−1))
    if (p.fadeStereo && n > 1)
    {
        const auto a = 1.5707963267948966 / (double) (n - 1);
        for (std::int64_t i = 0; i < n; ++i)
        {
            L[(std::size_t) i] = (float) ((double) L[(std::size_t) i] * std::cos ((double) i * a));
            R[(std::size_t) i] = (float) ((double) R[(std::size_t) i] * std::cos ((double) (n - 1 - i) * a));
        }
    }
    // remove DC: each channel minus its mean
    if (p.removeDc)
        forEach ([&] (std::vector<float>& x) {
            double sum = 0.0;
            for (auto v : x)
                sum += (double) v;
            const auto mean = sum / (double) x.size();
            for (auto& v : x)
                v = (float) ((double) v - mean);
        });
    if (p.reversePolarity)
        forEach ([] (std::vector<float>& x) {
            for (auto& v : x)
                v = -v;
        });
    // normalize: × 1 / the peak of both channels
    if (p.normalize)
    {
        float peak = 0.0f;
        forEach ([&] (std::vector<float>& x) {
            for (auto v : x)
                peak = std::max (peak, std::fabs (v));
        });
        if (peak > 0.0f)
        {
            const auto g = 1.0f / peak;
            forEach ([&] (std::vector<float>& x) {
                for (auto& v : x)
                    v = v * g;
            });
        }
    }
    // loop crossfade: the loop's last c frames blend into its first ones (c = loop length · x / 2048), the loop start
    // moves past them; without a loop the whole sample becomes the loop first
    if (p.crossfade > 0)
    {
        if (ls < 0)
            ls = 0, le = n - 1;
        const auto c = (std::int64_t) ((le - ls + 1) * (std::int64_t) p.crossfade / 2048);
        if (c > 0)
        {
            const auto from = le - c + 1;
            const auto step = 1.0 / (double) c;
            forEach ([&] (std::vector<float>& x) {
                for (std::int64_t k = 0; k < c; ++k)
                {
                    const auto a = (double) k * step;
                    x[(std::size_t) (from + k)] = (float) ((double) x[(std::size_t) (from + k)] * (1.0 - a) + (double) x[(std::size_t) (ls + k)] * a);
                }
            });
            ls += c;
        }
    }
    // 7. swap stereo
    if (p.swapStereo && stereo)
        L.swap (R);

    // 9. trim: the first frame at or above t/32768 (looking up to the loop start) and the last one (down to the loop
    // end) become the sample's ends
    std::int64_t t0 = 0, t1 = n - 1;
    if (p.trim > 0)
    {
        const auto threshold = (float) p.trim * 3.0517578e-05f;
        auto level = [&] (std::int64_t i) {
            auto a = std::fabs (L[(std::size_t) i]);
            if (stereo)
                a = std::max (a, std::fabs (R[(std::size_t) i]));
            return a;
        };
        const auto firstLimit = ls >= 0 ? ls : n - 1;
        while (t0 < firstLimit && level (t0) < threshold)
            ++t0;
        const auto lastLimit = ls >= 0 ? le : t0;
        while (t1 > lastLimit && level (t1) < threshold)
            --t1;
    }

    if (s.reverse)
    {
        forEach ([] (std::vector<float>& x) { std::reverse (x.begin(), x.end()); });
        std::tie (t0, t1) = std::pair { n - 1 - t1, n - 1 - t0 };
        if (ls >= 0)
            std::tie (ls, le) = std::pair { n - 1 - le, n - 1 - ls };
    }

    auto out = std::make_shared<SampleData>();
    out->sampleRate = in.sampleRate;
    out->left.assign (L.begin() + t0, L.begin() + t1 + 1);
    if (stereo)
        out->right.assign (R.begin() + t0, R.begin() + t1 + 1);
    if (ls >= 0 && ls >= t0 && le <= t1 && le > ls)
        out->loopStart = ls - t0, out->loopEnd = le - t0 + 1;
    out->acidBeats = in.acidBeats;
    out->cropped = true;
    // the fractions of the file this buffer covers (a ping pong loop makes it longer than its region)
    const auto span = std::max<std::int64_t> (1, n0 - 1);
    out->cropFrom = std::clamp (((double) s0 + (double) std::min (t0, n0 - 1)) / total, 0.0, 1.0);
    out->cropTo = std::clamp (((double) s0 + (double) std::min (t1, span)) / total, out->cropFrom, 1.0);
    return out;
}
} // namespace lynx::precompute
