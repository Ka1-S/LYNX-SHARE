#pragma once

// FL Studio's Sampler "Precomputed effects" (docs/analise/REA_rodada167h.md §2, from FLEngine's FUN_00ff0db0): the
// played region is cut out of the file and processed once, in FL's order — region crop with a declick at each cut
// edge, ping pong loop, reverse, fade in / fade out, fade stereo, remove DC, reverse polarity, normalize, loop
// crossfade, swap stereo, trim. The result is a new SampleData holding only that region (SampleData::cropped).
// The legacy effects (REA_rodada167l §1): stereo delay before the reverse; pogo, ring modulation, low-pass, boost and
// clip with the fades; the EQ after them; the reverb after swap stereo.
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

// FL's 16384-entry sine table (FUN_009cdfc0): sin(2π·k/16384) as floats
inline const std::vector<float>& sineTable()
{
    static const std::vector<float> table = [] {
        std::vector<float> t (16384);
        for (int k = 0; k < 16384; ++k)
            t[(std::size_t) k] = (float) std::sin ((double) (k * 2) * 3.141592653589793 / 16384.0);
        return t;
    }();
    return table;
}

// Step 6 of FL's precompute on one channel (`channel` 0 / 1: the ring modulator's phase starts a quarter turn later on
// the second one). REA_rodada167l §1: FUN_00ff0db0 with the constants 1/128, 1/256, 1/1024, 1/600 and ln 601.
inline void legacyChain (std::vector<float>& x, int channel, const SamplePrecompute& p, double d0)
{
    const auto n = (std::int64_t) x.size();
    const auto last = (int) (n - 1);
    const auto pogo = p.pogo != 256;
    const auto ring = (double) ((float) p.ringMix * (1.0f / 256.0f));
    const auto lowpass = p.cutoff < 1024 || p.resonance > 0;
    const auto any = pogo || ring != 0.0 || lowpass || p.fadeIn > 0 || p.fadeOut > 0 || p.boost > 0 || p.clip;
    if (any)
    {
        std::vector<float> src;
        if (pogo)
            src = x;
        const auto k = (float) std::abs (p.pogo - 256) / 256.0f;
        const auto ringStep = ((double) ((p.ringFreq + 8) * 0x4000) * d0) / 5760.0;
        auto phase = (double) ((channel & 0x3ffff) << 12);
        const auto& sine = sineTable();
        const auto ln601 = (float) std::log (601.0);
        const auto cut = (float) p.cutoff * (1.0f / 1024.0f);
        const auto f = (float) ((double) (float) ((std::exp ((double) (cut * ln601)) - 1.0) * (double) (1.0f / 600.0f)) * d0);
        const auto r = (float) (1024 - p.resonance) * (1.0f / 1024.0f);
        const auto q = 1.0f - (float) ((std::exp ((double) (r * ln601)) - 1.0) * (double) (1.0f / 600.0f));
        double v = 0.0, y = 0.0;
        const auto fadeStep = (1.0 / (double) last) / 256.0;
        const auto boost = (float) p.boost / 16.0f + 1.0f;
        auto at = [&] (std::int64_t i) { return i >= 0 && i < n ? src[(std::size_t) i] : 0.0f; };
        for (int i = 0; i < (int) n; ++i)
        {
            auto e = x[(std::size_t) i];
            if (pogo)
            {
                double b;
                if (p.pogo < 256)
                    b = (double) last + std::asin ((double) i / (double) last - 1.0) * (double) last * 0.6366197723675814;
                else
                    b = std::sin (((double) i / (double) last) * 1.5707963267948966) * (double) last;
                b = (double) ((float) i * (1.0f - k)) + b * (double) k;
                const auto fi = (std::int64_t) b;
                const auto t = (float) (b - (double) fi);
                const auto ym1 = fi < 1 ? 0.0f : at (fi - 1), y0 = at (fi), y1 = at (fi + 1), y2 = at (fi + 2);
                const auto c3 = (((y0 - y1) * 3.0f - ym1) + y2) * 0.5f;
                const auto c2 = (y1 * 2.0f + ym1) - (y0 * 5.0f + y2) * 0.5f;
                const auto c1 = (y1 - ym1) * 0.5f;
                e = y0 + ((c3 * t + c2) * t + c1) * t;
            }
            if (ring > 0.0)
            {
                const auto m = (float) ((double) (sine[(std::size_t) (std::int64_t) phase] * e) * ring);
                for (phase += ringStep; phase >= 16384.0; phase -= 16384.0) {}
                e = (float) ((double) m + (double) e * (1.0 - ring));
            }
            if (lowpass)
            {
                v += ((double) e - y) * (double) f;
                y += v;
                v *= (double) q;
                e = (float) y;
            }
            if (p.fadeIn > 0)
            {
                const auto g = (float) (1.0 - fadeStep * (double) (last - i) * (double) p.fadeIn);
                e = g <= 0.0f ? 0.0f : e * g;
            }
            if (p.fadeOut > 0)
            {
                const auto g = (float) (1.0 - (double) i * fadeStep * (double) p.fadeOut);
                e = g <= 0.0f ? 0.0f : e * g;
            }
            if (p.boost > 0)
                e = e * boost;
            if (p.clip)
                e = std::clamp (e, -1.0f, 1.0f);
            x[(std::size_t) i] = e;
        }
    }
    // the EQ (FL's FX, after the loop): y = x + y₋₁ · k, k = (v·192/256 − 96) / 128
    if (p.eq != 128)
    {
        const auto k = (float) (int) (((unsigned) (p.eq * 192) >> 8) - 96) * (1.0f / 128.0f);
        float prev = 0.0f;
        for (auto& e : x)
            prev = e = e + prev * k;
    }
}

// FL's delay line of the legacy reverb (FUN_00a990f0 / FUN_00a99230): `size − 1` stereo frames (at least 1); a push
// returns what went in that many pushes before.
struct ReverbDelay
{
    explicit ReverbDelay (int size) : buf ((std::size_t) std::max (1, size - 1) * 2, 0.0f) {}
    std::vector<float> buf;
    std::size_t pos = 0;
    float out[2] {};
    void push (const float* in) noexcept
    {
        out[0] = buf[pos * 2], out[1] = buf[pos * 2 + 1];
        buf[pos * 2] = in[0], buf[pos * 2 + 1] = in[1];
        if (++pos * 2 >= buf.size())
            pos = 0;
    }
};

// FL's comb feedback for a delay of `size` frames and a decay of `seconds` (to −60 dB): 10^(−3·size / (seconds·44100))
inline double combGain (int size, double seconds)
{
    const auto e = (float) ((double) (size * -3) / (seconds * 44100.0));
    if (e == 0.0f)
        return 1.0;
    return (double) (float) std::exp ((double) e * std::log (10.0));
}

// Step 8: FL's two legacy reverbs (FUN_00a99270 type 0, FUN_00a99730 type 1), on a stereo sample that gets 22050 frames
// of silence appended first. Wet × 0.3 and dry × 0.7 (type 1: the wet path is mixed twice).
inline void reverb (std::vector<float>& L, std::vector<float>& R, double seconds, int type)
{
    L.resize (L.size() + 22050, 0.0f);
    R.resize (R.size() + 22050, 0.0f);
    const auto n = L.size();
    if (type == 0)
    {
        constexpr int sizes[] = { 1777, 1847, 1993, 2137, 389, 127, 43 };
        std::vector<ReverbDelay> d;
        for (auto z : sizes)
            d.emplace_back (z);
        double g[4];
        for (int c = 0; c < 4; ++c)
            g[c] = combGain (sizes[c], seconds);
        float h0[2] {}, h1[2] {}, h2[2] {}; // the last three comb sums (newest first)
        for (std::size_t i = 0; i < n; ++i)
        {
            const float in[2] { L[i], R[i] };
            float a[2] { in[0], in[1] };
            for (int k = 4; k < 7; ++k) // three allpasses
            {
                const float o[2] { d[(std::size_t) k].out[0], d[(std::size_t) k].out[1] };
                for (int ch = 1; ch >= 0; --ch)
                    a[ch] = a[ch] + o[ch] * 0.7f;
                d[(std::size_t) k].push (a);
                for (int ch = 1; ch >= 0; --ch)
                    a[ch] = o[ch] - a[ch] * 0.7f;
            }
            float sum[2] {};
            for (int c = 0; c < 4; ++c) // four combs
            {
                float y[2];
                for (int ch = 1; ch >= 0; --ch)
                {
                    y[ch] = (float) ((double) a[ch] + g[c] * (double) d[(std::size_t) c].out[ch]);
                    sum[ch] = sum[ch] + y[ch];
                }
                d[(std::size_t) c].push (y);
            }
            std::copy (h1, h1 + 2, h2);
            std::copy (h0, h0 + 2, h1);
            std::copy (sum, sum + 2, h0);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto w = h1[ch] * 0.5f + (h0[ch] + h2[ch]) * 0.25f;
                (ch == 0 ? L : R)[i] = w * 0.3f + in[ch] * 0.7f;
            }
        }
        return;
    }
    constexpr int sizes[] = { 2467, 2767, 3217, 3533, 3877, 4127, 599, 197, 67, 101, 97, 79 };
    std::vector<ReverbDelay> d;
    for (auto z : sizes)
        d.emplace_back (z);
    double g[6];
    for (int c = 0; c < 6; ++c)
        g[c] = combGain (sizes[c], seconds);
    float lp[2] {};               // a one-pole low-pass on the diffused sum
    float stale10[2] {}, stale11[2] {}; // the unused halves of the last two allpasses' inputs (FL leaves them as they were)
    for (std::size_t i = 0; i < n; ++i)
    {
        const float in[2] { L[i], R[i] };
        float sum[2] {};
        for (int c = 0; c < 6; ++c) // six combs
        {
            float y[2];
            for (int ch = 1; ch >= 0; --ch)
            {
                y[ch] = (float) ((double) in[ch] + g[c] * (double) d[(std::size_t) c].out[ch]);
                sum[ch] = sum[ch] + y[ch];
            }
            d[(std::size_t) c].push (y);
        }
        for (int k = 6; k < 9; ++k) // three allpasses on the sum
        {
            const float o[2] { d[(std::size_t) k].out[0], d[(std::size_t) k].out[1] };
            for (int ch = 1; ch >= 0; --ch)
                sum[ch] = sum[ch] + o[ch] * 0.7f;
            d[(std::size_t) k].push (sum);
            for (int ch = 1; ch >= 0; --ch)
                sum[ch] = o[ch] - sum[ch] * 0.7f;
        }
        for (int ch = 1; ch >= 0; --ch)
            lp[ch] = lp[ch] * 0.7f + sum[ch] * 0.3f;
        float a[2];
        {
            const float o[2] { d[9].out[0], d[9].out[1] };
            a[1] = o[1] * 0.7f + lp[1];
            a[0] = o[0] * 0.7f + lp[0];
            d[9].push (a);
            a[1] = o[1] - a[1] * 0.7f;
            a[0] = o[0] - a[0] * 0.7f;
        }
        float wet[2];
        {
            const auto o = d[10].out[0];
            stale10[0] = o * 0.7f + a[0];
            d[10].push (stale10);
            wet[0] = (o - stale10[0] * 0.7f) * 0.3f;
        }
        {
            const auto o = d[11].out[1];
            stale11[1] = o * 0.7f + a[1];
            d[11].push (stale11);
            wet[1] = (o - stale11[1] * 0.7f) * 0.3f;
        }
        L[i] = wet[0] * 0.3f + in[0] * 0.7f;
        R[i] = wet[1] * 0.3f + in[1] * 0.7f;
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
    auto stereo = in.isStereo(); // fade stereo, the stereo delay and the reverb make a mono sample stereo
    std::vector<float> L (in.leftData() + s0, in.leftData() + s0 + n0), R;
    if (stereo)
        R.assign (in.rightData() + s0, in.rightData() + s0 + n0);
    auto makeStereo = [&] {
        if (! stereo)
            R = L, stereo = true;
    };
    // FL's rate factor for the legacy effects: 44100 / the sample's rate
    const auto d0 = 44100.0 / rate;

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

    // 4. legacy stereo delay (FL: a stereo copy N + d frames long, d = round(|v − 2048| / d0); below 2048 the right
    // channel starts d frames later, above it the left one)
    if (p.stereoDelay != 2048 && L.size() > 3)
    {
        makeStereo();
        const auto d = (std::size_t) std::nearbyint ((double) std::abs (p.stereoDelay - 2048) / d0);
        auto& late = p.stereoDelay < 2048 ? R : L;
        auto& early = p.stereoDelay < 2048 ? L : R;
        late.insert (late.begin(), d, 0.0f);
        early.resize (early.size() + d, 0.0f);
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

    // 6. per channel and frame, in FL's order: pogo (FL's FX3: a warp of the read position, 4-point interpolation),
    // ring modulation, the resonant low-pass, fade in / fade out (gains 1 − (N−1−i)·a/(256·(N−1)) and
    // 1 − i·b/(256·(N−1)), floored at 0), boost × (1 + v/16), clip to ±1; then the EQ (FL's FX: a one-pole feedback)
    if (n > 3)
    {
        legacyChain (L, 0, p, d0);
        if (stereo)
            legacyChain (R, 1, p, d0);
    }
    if (p.fadeStereo)
        makeStereo();
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

    // 8. legacy reverb: stereo, 22050 frames of tail, then the tail's end below 1/512 is cut (down to the loop end)
    if (p.reverb != 0 && n > 3)
    {
        makeStereo();
        reverb (L, R, (double) ((float) p.reverb * (1.0f / 256.0f)), p.reverbType);
        auto keep = (std::int64_t) L.size();
        const auto floor = std::max<std::int64_t> (1, ls >= 0 ? le : 1);
        while (keep > floor && std::fabs (L[(std::size_t) keep - 1]) < 0.001953125f && std::fabs (R[(std::size_t) keep - 1]) < 0.001953125f)
            --keep;
        L.resize ((std::size_t) keep);
        R.resize ((std::size_t) keep);
    }
    const auto nOut = (std::int64_t) L.size();

    // 9. trim: the first frame at or above t/32768 (looking up to the loop start) and the last one (down to the loop
    // end) become the sample's ends
    std::int64_t t0 = 0, t1 = nOut - 1;
    if (p.trim > 0)
    {
        const auto threshold = (float) p.trim * 3.0517578e-05f;
        auto level = [&] (std::int64_t i) {
            auto a = std::fabs (L[(std::size_t) i]);
            if (stereo)
                a = std::max (a, std::fabs (R[(std::size_t) i]));
            return a;
        };
        const auto firstLimit = ls >= 0 ? ls : nOut - 1;
        while (t0 < firstLimit && level (t0) < threshold)
            ++t0;
        const auto lastLimit = ls >= 0 ? le : t0;
        while (t1 > lastLimit && level (t1) < threshold)
            --t1;
    }

    if (s.reverse)
    {
        forEach ([] (std::vector<float>& x) { std::reverse (x.begin(), x.end()); });
        std::tie (t0, t1) = std::pair { nOut - 1 - t1, nOut - 1 - t0 };
        if (ls >= 0)
            std::tie (ls, le) = std::pair { nOut - 1 - le, nOut - 1 - ls };
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
