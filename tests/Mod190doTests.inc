// Round 190do: deterministic modulation laws of Spiral Echo, Orbit, Lattice Mod and Gemini Doubler, measured black-box
// on the tp172d reference renders (/mnt/e/lynx-work/mod190do/HANDOFF.md). Each effect gets an appended law parameter:
// new instances run the measured law, a project or preset saved before keeps the classic one. What is checked: the
// parameters and their load rule, that the classic laws still render what the base commit rendered (fingerprints from
// the base commit's objects, /mnt/e/lynx-work/mod190do/gold), the measured laws' own behaviour, and their stability.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/VoiceTools.h"

namespace mod190do
{
using lynx::EffectKind;

struct Stereo
{
    std::vector<float> l, r;
};

struct LawOf
{
    EffectKind kind;
    std::size_t index;
    const char* name;
};

inline const LawOf laws[] = {
    { EffectKind::spiralEcho, (std::size_t) lynx::echosuite::spiral::law, "Law" },
    { EffectKind::orbit, (std::size_t) lynx::echosuite::orbit::law, "Law" },
    { EffectKind::latticeMod, (std::size_t) lynx::echosuite::lattice::law, "Law" },
    { EffectKind::geminiDoubler, (std::size_t) lynx::voicetools::gemini::headLaw, "Head law" },
};

// `seconds` of the effect's answer to `in` (both channels), from a fresh instance, in blocks
inline Stereo render (EffectKind kind, const lynx::EffectParams& p, const Stereo& in, double sr = 48000.0, int block = 256)
{
    auto fx = lynx::makeEffect (kind);
    fx->prepare (sr);
    auto out = in;
    const auto n = (int) out.l.size();
    for (int off = 0; off < n; off += block)
        fx->process (out.l.data() + off, out.r.data() + off, std::min (block, n - off), p, 120.0);
    return out;
}

inline Stereo impulse (double seconds, double sr = 48000.0, float a = 0.5f)
{
    const auto n = (std::size_t) (seconds * sr);
    Stereo s { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
    s.l[0] = s.r[0] = a;
    return s;
}

inline bool finite (const Stereo& s)
{
    for (const auto* v : { &s.l, &s.r })
        for (const float x : *v)
            if (! std::isfinite (x) || std::abs (x) > 64.0f)
                return false;
    return true;
}
} // namespace mod190do

TEST (mod190do_new_instances_run_the_measured_laws_and_saved_projects_keep_the_classic_ones)
{
    using namespace lynx;
    for (const auto& law : mod190do::laws)
    {
        const auto& info = effectInfo (law.kind).params;
        CHECK (info.size() == law.index + 1 && std::string (info[law.index].name) == law.name);
        CHECK (formatEffectParam (law.kind, (int) law.index, 0.0f, 120.0) == "Classic");
        CHECK (formatEffectParam (law.kind, (int) law.index, 1.0f, 120.0) == "Measured");
        auto fresh = makeEffectSlot (law.kind);
        CHECK (fresh.params[law.index] >= 0.5f);
        auto old = makeEffectSlot (law.kind);
        upgradeLoadedEffectParams (old, law.index); // saved by 0.1.26: one parameter short
        CHECK (old.params[law.index] < 0.5f);
        for (const float v : { 0.0f, 1.0f })
        {
            auto again = makeEffectSlot (law.kind);
            again.params[law.index] = v;
            const auto count = savedEffectParamCount (again);
            CHECK (count == law.index + 1);
            auto loaded = makeEffectSlot (law.kind);
            for (std::size_t i = 0; i < count; ++i)
                loaded.params[i] = std::clamp ((float) savedEffectParamValue (again.params[i]), 0.0f, 1.0f);
            upgradeLoadedEffectParams (loaded, count);
            CHECK (loaded.params[law.index] == v);
        }
    }
}

TEST (mod190do_gemini_measured_head_reads_one_tap_that_drifts_and_jumps_back)
{
    using namespace lynx;
    namespace gm = voicetools::gemini;
    // voice 1 alone (left), direct off, its default delay (9.4 ms) and +6 cents; impulses every 0.25 s
    auto p = makeEffectSlot (EffectKind::geminiDoubler).params;
    p[gm::directOn] = 0.0f;
    p[(std::size_t) gm::voiceParam (1, gm::vOn)] = 0.0f;
    p[gm::lowOn] = p[gm::highOn] = 0.0f;
    const double sr = 48000.0, every = 0.25 * sr, delay = 0.094 * 100.0 * 0.001 * sr; // 451.2 samples
    const int pulses = 24;
    mod190do::Stereo in { std::vector<float> ((std::size_t) (every * pulses + 2000), 0.0f), {} };
    for (int k = 0; k < pulses; ++k)
        in.l[(std::size_t) (k * every)] = 0.5f;
    in.r = in.l;
    for (const float law : { 1.0f, 0.0f })
    {
        p[gm::headLaw] = law;
        const auto y = mod190do::render (EffectKind::geminiDoubler, p, in, sr);
        int single = 0;
        std::vector<double> at;
        for (int k = 0; k < pulses; ++k)
        {
            // the answer to pulse k: its strongest sample and the energy more than 4 samples away from it
            const auto a = (std::size_t) (k * every);
            std::size_t best = 0;
            double e = 0.0, peak = 0.0;
            for (std::size_t i = 1; i < 1500; ++i)
            {
                const auto v = (double) y.l[a + i];
                e += v * v;
                if (std::abs (v) > peak)
                    peak = std::abs (v), best = i;
            }
            double near = 0.0;
            for (std::size_t i = best - 4; i <= best + 4; ++i)
                near += (double) y.l[a + i] * y.l[a + i];
            single += near > 0.97 * e ? 1 : 0;
            at.push_back ((double) best);
        }
        if (law >= 0.5f)
        {
            // one head: all its energy at one place (a few pulses land on a jump's short crossfade)
            CHECK (single >= pulses - 3);
            // it reads earlier than the delay (pitch up), never more than the range ahead of it, and drifts by
            // (1 − 2^(6/1200)) per sample between pulses except across a jump back
            int drifts = 0, jumps = 0;
            for (int k = 1; k < pulses; ++k)
            {
                const auto d = at[(std::size_t) k] - at[(std::size_t) k - 1];
                drifts += std::abs (d + (std::pow (2.0, 6.0 / 1200.0) - 1.0) * every) < 1.5 ? 1 : 0;
                jumps += d > 200.0 ? 1 : 0;
            }
            CHECK (drifts >= pulses - 4 && jumps >= 1);
            for (const auto v : at)
                CHECK (v <= delay + 2.0 && v >= delay - 260.0);
        }
        else
            CHECK (single < pulses / 2); // the classic law reads two taps half a 13.3 ms window apart
    }
}

namespace mod190do
{
// the delay (samples) of the strongest tap of an Orbit's answer to impulses every `every` samples, mix 100 %, per channel
inline std::array<std::vector<double>, 2> orbitTaps (float mode, float law, int pulses, int every)
{
    using namespace lynx;
    namespace ob = echosuite::orbit;
    auto p = makeEffectSlot (EffectKind::orbit).params;
    p[ob::mix] = 1.0f;
    p[ob::feedback] = 0.5f; // none
    p[ob::rate] = 0.75f;    // about +1 Hz
    p[ob::depth] = 0.371f;  // about 5 ms
    p[ob::mode] = mode;
    p[ob::law] = law;
    Stereo in { std::vector<float> ((std::size_t) (pulses * every), 0.0f), {} };
    for (int k = 0; k < pulses; ++k)
        in.l[(std::size_t) (k * every)] = 0.5f;
    in.r = in.l;
    const auto y = render (EffectKind::orbit, p, in);
    std::array<std::vector<double>, 2> at;
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k + 1 < pulses; ++k)
        {
            const auto& v = c == 0 ? y.l : y.r;
            std::size_t best = 0;
            double peak = 0.0;
            for (int i = 1; i < every; ++i)
                if (std::abs ((double) v[(std::size_t) (k * every + i)]) > peak)
                    peak = std::abs ((double) v[(std::size_t) (k * every + i)]), best = (std::size_t) i;
            at[(std::size_t) c].push_back ((double) best);
        }
    return at;
}

// the shortest and the longest delay (samples) at which any tap of the left channel reads with at least 10 % weight
// (an answer above 0.05 to the 0.5 impulses), over the whole run
inline std::pair<double, double> orbitReach (float law, int pulses, int every)
{
    using namespace lynx;
    namespace ob = echosuite::orbit;
    auto p = makeEffectSlot (EffectKind::orbit).params;
    p[ob::mix] = 1.0f, p[ob::feedback] = 0.5f, p[ob::rate] = 0.75f, p[ob::depth] = 0.371f, p[ob::mode] = 1.0f / 11.0f;
    p[ob::law] = law;
    Stereo in { std::vector<float> ((std::size_t) (pulses * every), 0.0f), {} };
    for (int k = 0; k < pulses; ++k)
        in.l[(std::size_t) (k * every)] = 0.5f;
    in.r = in.l;
    const auto y = render (EffectKind::orbit, p, in);
    double lo = 1e9, hi = -1e9;
    for (int k = 0; k + 1 < pulses; ++k)
        for (int i = 1; i < every; ++i)
            if (std::abs (y.l[(std::size_t) (k * every + i)]) > 0.05f)
                lo = std::min (lo, (double) i), hi = std::max (hi, (double) i);
    return { lo, hi };
}

// the share of steps where the tap moved the given way (down = shorter delay = rising pitch), ignoring wraps
inline double moving (const std::vector<double>& at, bool down)
{
    int n = 0, ok = 0;
    for (std::size_t k = 1; k < at.size(); ++k)
    {
        const auto d = at[k] - at[k - 1];
        if (std::abs (d) > 60.0 || d == 0.0)
            continue;
        ++n;
        ok += (d < 0.0) == down ? 1 : 0;
    }
    return n > 0 ? (double) ok / n : 0.0;
}
} // namespace mod190do

TEST (mod190do_orbit_measured_rise_sweeps_the_whole_depth_and_rise_fall_splits_the_channels)
{
    using namespace mod190do;
    const auto depth = lynx::echosuite::orbit::depthMs (0.371f) * 48.0; // about 240 samples
    // Rise, measured: two taps sweep the whole depth (classic: half of it, above a 0.25 ms base); with sin² weights a
    // tap reads at 10 % or more over 0.1 … 0.9 of its sweep
    for (const float law : { 1.0f, 0.0f })
    {
        const auto [lo, hi] = orbitReach (law, 160, 300);
        if (law >= 0.5f)
            CHECK (hi - lo > 0.7 * depth && lo < 0.15 * depth);
        else
            CHECK (hi - lo < 0.5 * depth && lo > 0.2 * depth);
        const auto at = orbitTaps (1.0f / 11.0f, law, 160, 300);
        CHECK (moving (at[0], true) > 0.9 && moving (at[1], true) > 0.9); // both channels rise
    }
    // Rise / fall, measured: the left channel rises, the right one falls
    {
        const auto at = orbitTaps (2.0f / 11.0f, 1.0f, 160, 300);
        CHECK (moving (at[0], true) > 0.9 && moving (at[1], false) > 0.9);
    }
}

TEST (mod190do_spiral_measured_loop_limits_lower_and_glides_slower)
{
    using namespace lynx;
    namespace sp = echosuite::spiral;
    const double sr = 48000.0;
    auto p = makeEffectSlot (EffectKind::spiralEcho).params;
    p[sp::mix] = 1.0f;
    p[sp::shift] = 0.5f;
    p[sp::stereo] = 0.0f;
    p[sp::delay] = sp::delayValue (20.0);
    // loud noise: the measured loop's limiter holds the repeats lower than the classic tanh (the reference is 3 … 4 dB
    // under the classic loop on the loudest 100 ms of programme, tp172d S3); quiet noise passes both alike
    const auto rmsDb = [] (const mod190do::Stereo& s, std::size_t from) {
        double e = 0.0;
        for (std::size_t i = from; i < s.l.size(); ++i)
            e += (double) s.l[i] * s.l[i];
        return 10.0 * std::log10 (e / (double) (s.l.size() - from) + 1e-30);
    };
    for (const double a : { 0.7, 0.003 })
    {
        mod190do::Stereo in { std::vector<float> ((std::size_t) sr, 0.0f), {} };
        std::uint32_t s = 5u;
        for (auto& v : in.l)
        {
            s = s * 1664525u + 1013904223u;
            v = (float) (a * ((double) (s >> 8) / 8388608.0 - 1.0));
        }
        in.r = in.l;
        p[sp::law] = 1.0f;
        const auto m = rmsDb (mod190do::render (EffectKind::spiralEcho, p, in, sr), 24000);
        p[sp::law] = 0.0f;
        const auto c = rmsDb (mod190do::render (EffectKind::spiralEcho, p, in, sr), 24000);
        if (a > 0.1)
            CHECK (c - m > 0.8); // 1.3 dB on this noise (the round's renders: 1.31)
        else
            CHECK (std::abs (c - m) < 0.5); // 0.25 dB: the cuts' bilinear shapes
    }
    // feedback at its top (loop gain 1.5): the measured loop runs away into its limiter and stays bounded
    {
        p[sp::law] = 1.0f;
        p[sp::feedback] = 1.0f;
        mod190do::Stereo in { std::vector<float> ((std::size_t) (3 * sr), 0.0f), {} };
        for (std::size_t i = 0; i < 4800; ++i)
            in.l[i] = (float) (0.9 * std::sin (0.07 * (double) i));
        in.r = in.l;
        const auto y = mod190do::render (EffectKind::spiralEcho, p, in, sr);
        CHECK (mod190do::finite (y));
        float peak = 0.0f;
        for (const float v : y.l)
            peak = std::max (peak, std::abs (v));
        CHECK (peak < 2.0f && peak > 0.05f);
        p[sp::feedback] = 0.5f;
    }
    // a change of delay glides with a time constant of about 0.175 s (the reference's, refcap0124 delay sweeps; the
    // classic loop: 0.05 s): 10 → 20 ms, the echo of an impulse 175 ms after the change is about 63 % of the way
    {
        p[sp::law] = 1.0f;
        p[sp::feedback] = 0.0f;
        auto fx = makeEffect (EffectKind::spiralEcho);
        fx->prepare (sr);
        const int block = 64, n = (int) (1.0 * sr);
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        const int change = 24000, probe = change + (int) (0.175 * sr);
        l[(std::size_t) probe] = r[(std::size_t) probe] = 0.5f;
        for (int off = 0; off < n; off += block)
        {
            p[sp::delay] = sp::delayValue (off < change ? 10.0 : 20.0);
            fx->process (l.data() + off, r.data() + off, block, p, 120.0);
        }
        std::size_t best = 0;
        for (std::size_t i = (std::size_t) probe; i < (std::size_t) probe + 1500; ++i)
            if (std::abs (l[i]) > std::abs (l[best]))
                best = i;
        const auto lag = (double) (best - (std::size_t) probe), way = (lag - 480.0) / 480.0;
        CHECK (way > 0.5 && way < 0.78);
    }
}

TEST (mod190do_lattice_measured_detune_reaches_the_reference_spread)
{
    using namespace lynx;
    namespace lt = echosuite::lattice;
    // the default (one tap, 10 ms, detune 50 % of 50 % depth): impulses every 50 ms, the strongest tap of each answer.
    // The reference's taps spread over about ±200 samples there (refcap0124 one-parameter captures: 340 … 739 samples);
    // the classic law over ±56
    const double sr = 48000.0;
    const int pulses = 160, every = 2400;
    mod190do::Stereo in { std::vector<float> ((std::size_t) (pulses * every), 0.0f), {} };
    for (int k = 0; k < pulses; ++k)
        in.l[(std::size_t) (k * every)] = 0.5f;
    in.r = in.l;
    double spread[2] {};
    for (const int law : { 0, 1 })
    {
        auto p = makeEffectSlot (EffectKind::latticeMod).params;
        p[lt::mix] = 1.0f;
        p[lt::law] = (float) law;
        const auto y = mod190do::render (EffectKind::latticeMod, p, in, sr);
        CHECK (mod190do::finite (y));
        double lo = 1e9, hi = -1e9;
        for (int k = 0; k + 1 < pulses; ++k)
        {
            std::size_t best = 0;
            for (int i = 2; i < every; ++i)
                if (std::abs (y.l[(std::size_t) (k * every + i)]) > std::abs (y.l[(std::size_t) (k * every) + best]))
                    best = (std::size_t) i;
            lo = std::min (lo, (double) best), hi = std::max (hi, (double) best);
        }
        spread[law] = hi - lo;
    }
    CHECK (spread[1] > 2.0 * spread[0] && spread[1] < 600.0);
}

TEST (mod190do_measured_laws_stay_finite_at_every_extreme)
{
    using namespace lynx;
    for (const auto& law : mod190do::laws)
        for (const double sr : { 44100.0, 48000.0, 96000.0 })
            for (int variant = 0; variant < 6; ++variant)
            {
                auto p = makeEffectSlot (law.kind).params;
                std::uint32_t s = 1234u + (std::uint32_t) variant * 77u;
                for (std::size_t i = 0; i < p.size(); ++i)
                {
                    s = s * 1664525u + 1013904223u;
                    p[i] = variant == 0 ? 0.0f : variant == 1 ? 1.0f : (float) (s >> 8) / 16777216.0f;
                }
                p[law.index] = 1.0f;
                mod190do::Stereo in { std::vector<float> ((std::size_t) (0.6 * sr), 0.0f), {} };
                for (std::size_t i = 0; i < in.l.size() / 3; ++i)
                    in.l[i] = (float) (0.9 * std::sin (0.03 * (double) i + 0.001 * (double) (i * i % 977)));
                in.r = in.l;
                CHECK (mod190do::finite (mod190do::render (law.kind, p, in, sr, 333)));
            }
}

TEST (mod190do_classic_laws_render_as_the_base_commit_did)
{
    using namespace lynx;
    struct Case
    {
        EffectKind kind;
        std::size_t law;
        int idx;
        float val;
        int idx2;
        float val2;
        double sr;
        std::uint64_t hash;
        double energy, a, b;
    };
    // an impulse (0.5 / −0.25) and a noise burst, 1.5 s in blocks of 256: FNV-1a of the float bits, the energy, two
    // samples. The goldens come from the base commit's objects (/mnt/e/lynx-work/mod190do/gold: gold_base, without the
    // law parameters, prints the same lines as gold_new, the round's objects at law = Classic)
    namespace es = echosuite;
    namespace gm = voicetools::gemini;
    const Case cases[] = {
        { EffectKind::spiralEcho, es::spiral::law, -1, 0.0f, -1, 0.0f, 48000.0, 0xd96e67995d390584ull, 41.7843959, 3.93556002e-05, -0.000187082827 },
        { EffectKind::spiralEcho, es::spiral::law, 1, 0.62f, 4, 0.7f, 44100.0, 0x8d2c7b9cde6b4a98ull, 61.6467675, 2.87903231e-06, -0.000423245307 },
        { EffectKind::orbit, es::orbit::law, -1, 0.0f, -1, 0.0f, 48000.0, 0x1bce9ed14615a104ull, 92.4305534, 0.0, 0.0 },
        { EffectKind::orbit, es::orbit::law, 5, 0.27f, 3, 0.8f, 48000.0, 0x69cd4799f484b016ull, 113.427882, 0.0, -2.43045232e-19 },
        { EffectKind::orbit, es::orbit::law, 5, 0.5f, 1, 0.8f, 96000.0, 0x9b656c1afdc07cf7ull, 105.793828, 0.0, 0.0 },
        { EffectKind::geminiDoubler, gm::headLaw, -1, 0.0f, -1, 0.0f, 48000.0, 0xbc5577e0eef5af08ull, 173.873472, 0.0, -5.03066149e-43 },
        { EffectKind::geminiDoubler, gm::headLaw, gm::align, 1.0f, gm::voiceParam (0, gm::vDepth), 0.3f, 48000.0, 0x78231649d3b1c374ull, 164.168348, -2.80259693e-45, -5.36697312e-43 },
        { EffectKind::geminiDoubler, gm::headLaw, gm::voiceParam (0, gm::vOctave), 1.0f, gm::directGain, 0.5f, 44100.0, 0xb514966ee0730f90ull, 53.1590003, 0.0, -2.31074117e-42 },
    };
    int sameBits = 0;
    for (const auto& c : cases)
    {
        auto slot = makeEffectSlot (c.kind);
        slot.params[c.law] = 0.0f;
        if (c.idx >= 0)
            slot.params[(std::size_t) c.idx] = c.val;
        if (c.idx2 >= 0)
            slot.params[(std::size_t) c.idx2] = c.val2;
        auto fx = makeEffect (slot);
        fx->prepare (c.sr);
        const int n = (int) (1.5 * c.sr), block = 256;
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        unsigned s = 777u;
        l[0] = 0.5f;
        r[0] = -0.25f;
        for (int i = n / 3; i < n / 3 + 2000; ++i)
        {
            s = s * 1664525u + 1013904223u;
            l[(std::size_t) i] = ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.3f;
            s = s * 1664525u + 1013904223u;
            r[(std::size_t) i] = ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.3f;
        }
        for (int off = 0; off < n; off += block)
            fx->process (l.data() + off, r.data() + off, std::min (block, n - off), slot.params, 120.0);
        std::uint64_t h = 1469598103934665603ull;
        double e = 0.0;
        for (int i = 0; i < n; ++i)
            for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
            {
                std::uint32_t u;
                std::memcpy (&u, &v, 4);
                for (int b = 0; b < 4; ++b)
                {
                    h ^= (u >> (8 * b)) & 0xffu;
                    h *= 1099511628211ull;
                }
                e += (double) v * v;
            }
        // the same sound with any maths library: energy within 0.01 %, two samples within 2e-6
        CHECK (std::abs (e - c.energy) <= 1e-4 * c.energy);
        CHECK (std::abs ((double) l[(std::size_t) (n / 7)] - c.a) < 2e-6);
        CHECK (std::abs ((double) r[(std::size_t) (n / 2)] - c.b) < 2e-6);
        sameBits += h == c.hash ? 1 : 0;
    }
    // bit for bit where the maths library is the one the goldens were made with (glibc 2.43 on x86-64: the local
    // builds); other libm versions round some transcendental calls differently (round 190bz's note)
    std::printf ("  classic laws: %d of 8 renders hash-identical to the base commit\n", sameBits);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (sameBits == 8);
   #endif
}

TEST (mod190do_measured_spiral_loop_gain_and_orbit_manual_delay)
{
    using namespace lynx;
    namespace sp = echosuite::spiral;
    namespace ob = echosuite::orbit;
    const double sr = 48000.0;
    // Spiral, measured loop: 1.5 × feedback a pass below the limiter (a 1 kHz tone at 0.02, cuts open, 100 ms)
    for (const float fb : { 0.25f, 0.5f })
    {
        auto p = makeEffectSlot (EffectKind::spiralEcho).params;
        p[sp::mix] = 1.0f, p[sp::lowCut] = 0.0f, p[sp::highCut] = 1.0f, p[sp::feedback] = fb, p[sp::stereo] = 0.0f;
        p[sp::delay] = sp::delayValue (100.0);
        mod190do::Stereo in { std::vector<float> (24000, 0.0f), {} };
        for (std::size_t i = 0; i < 1920; ++i)
            in.l[i] = (float) (0.02 * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * (double) i / sr));
        in.r = in.l;
        const auto y = mod190do::render (EffectKind::spiralEcho, p, in, sr);
        const auto amp = [&y, sr] (std::size_t from, std::size_t len) {
            double c = 0.0, s = 0.0;
            for (std::size_t i = from; i < from + len; ++i)
            {
                const auto ph = 2.0 * 3.14159265358979323846 * 1000.0 * (double) i / sr;
                c += y.l[i] * std::cos (ph), s += y.l[i] * std::sin (ph);
            }
            return 2.0 * std::sqrt (c * c + s * s) / (double) len;
        };
        const auto a1 = amp (4800 + 300, 1440), a2 = amp (9600 + 300, 1440);
        CHECK (std::abs (20.0 * std::log10 (a1 / 0.02)) < 0.3);
        CHECK (std::abs (20.0 * std::log10 (a2 / a1) - 20.0 * std::log10 (1.5 * fb)) < 0.3);
    }
    // Orbit, measured sweep, depth 0: the echo sits at the manual delay itself (the classic voices added 0.25 ms)
    {
        auto p = makeEffectSlot (EffectKind::orbit).params;
        p[ob::mix] = 1.0f, p[ob::feedback] = 0.5f, p[ob::depth] = 0.0f;
        p[ob::manual] = 0.5f + 0.5f * (float) std::pow (20.0 / 50.0, 0.25);
        for (const auto mode : { ob::Mode::rise, ob::Mode::riseFall })
        {
            p[ob::mode] = ob::modeValue (mode);
            const auto y = mod190do::render (EffectKind::orbit, p, mod190do::impulse (0.1, sr));
            std::size_t at = 100;
            for (std::size_t i = 100; i < y.l.size(); ++i)
                if (std::abs (y.l[i]) > std::abs (y.l[at]))
                    at = i;
            CHECK ((double) at > 19.8 * 48.0 && (double) at < 20.3 * 48.0);
        }
    }
}
