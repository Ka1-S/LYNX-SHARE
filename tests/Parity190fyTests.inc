// Round 190fy: Spiral Echo's measured loop, revision 1 (the loop's read position and its all-pass refitted on the tp172d
// reference renders, black-box; /mnt/e/lynx-work/parityfx190fy/HANDOFF.md). The revision is a hidden parameter: a new
// instance runs revision 1, a project or preset saved by 0.1.27 (Law, no revision) keeps round 190do's loop bit for bit,
// one saved before 0.1.27 keeps the classic loop. Checked: the parameter and its load rules, revision 0 against the
// base commit's renders (fingerprints from its objects, /mnt/e/lynx-work/parityfx190fy/gold), revision 1's own timing,
// and its stability.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/EchoSuite.h"
#include "core/Effects.h"

namespace parity190fy
{
// the first sample (from 0) whose magnitude exceeds `floor` in channel 0 of an impulse answer, with the dry part removed
inline int firstWet (const lynx::EffectParams& p, double sr, int length = 64)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::spiralEcho);
    fx->prepare (sr);
    std::vector<float> l ((std::size_t) length, 0.0f), r ((std::size_t) length, 0.0f);
    l[0] = r[0] = 0.5f;
    fx->process (l.data(), r.data(), length, p, 120.0);
    const auto dry = 1.0f - std::clamp (p[(std::size_t) lynx::echosuite::spiral::mix], 0.0f, 1.0f);
    l[0] -= dry * 0.5f;
    for (int i = 0; i < length; ++i)
        if (std::abs (l[(std::size_t) i]) > 1e-4f)
            return i;
    return -1;
}
} // namespace parity190fy

TEST (parity190fy_spiral_revision_is_hidden_new_instances_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace sp = echosuite::spiral;
    const auto& info = effectInfo (EffectKind::spiralEcho);
    CHECK (info.params.size() == (std::size_t) sp::paramCount); // hidden: not in the info, so no new control or string
    CHECK ((int) sp::revision == (int) sp::paramCount);
    auto slot = makeEffectSlot (EffectKind::spiralEcho);
    CHECK (slot.params[(std::size_t) sp::law] == 1.0f);
    CHECK (slot.params[(std::size_t) sp::revision] == 1.0f);
    CHECK (savedEffectParamCount (slot) == (std::size_t) sp::revision + 1); // saved, since it is not zero
    // saved by this version: all ten values -> stays
    auto now = slot;
    upgradeLoadedEffectParams (now, (std::size_t) sp::revision + 1);
    CHECK (now.params[(std::size_t) sp::revision] == 1.0f && now.params[(std::size_t) sp::law] == 1.0f);
    // saved by 0.1.27 (the nine named values, Law = Measured): round 190do's measured loop
    auto v127 = slot;
    upgradeLoadedEffectParams (v127, (std::size_t) sp::paramCount);
    CHECK (v127.params[(std::size_t) sp::revision] == 0.0f && v127.params[(std::size_t) sp::law] == 1.0f);
    CHECK (savedEffectParamCount (v127) == (std::size_t) sp::paramCount); // saved again: still read as 0.1.27's loop
    // saved before 0.1.27 (no Law): the classic loop
    auto old = slot;
    upgradeLoadedEffectParams (old, (std::size_t) sp::law);
    CHECK (old.params[(std::size_t) sp::revision] == 0.0f && old.params[(std::size_t) sp::law] == 0.0f);
}

TEST (parity190fy_spiral_revision0_renders_as_the_base_commit_did)
{
    using namespace lynx;
    namespace sp = echosuite::spiral;
    struct Case
    {
        int idx;
        float val;
        int idx2;
        float val2;
        double sr;
        std::uint64_t hash;
        double energy, a, b;
    };
    // Law = Measured, an impulse (0.5 / −0.25) and a noise burst, 1.5 s in blocks of 256: FNV-1a of the float bits, the
    // energy, two samples. From the base commit's objects (/mnt/e/lynx-work/parityfx190fy/gold: gold_base = g0.txt; the
    // round's objects with the revision forced to 0 print the same lines, g1.txt)
    const Case cases[] = {
        { -1, 0.0f, -1, 0.0f, 48000.0, 0x0947f3ef58f653e9ull, 40.1863497, 3.8063019e-05, -0.000101014259 },
        { sp::delay, 0.25f, sp::feedback, 0.600000024f, 48000.0, 0xb93cf0caff37f9e6ull, 43.0458008, 0.000148446969, 0.000262136193 },
        { sp::shift, 0.620000005f, sp::delay, 0.300000012f, 44100.0, 0xc1be12add1f5479bull, 41.0434486, 5.7030742e-05, 0.00099845184 },
        { sp::lowCut, 0.0f, sp::highCut, 1.0f, 96000.0, 0x411bc2b47cd4b492ull, 45.115946, 0.000142635792, -0.000864983886 },
    };
    int sameBits = 0;
    for (const auto& c : cases)
    {
        auto slot = makeEffectSlot (EffectKind::spiralEcho);
        slot.params[(std::size_t) sp::law] = 1.0f;
        slot.params[(std::size_t) sp::revision] = 0.0f;
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
    std::printf ("  Spiral revision 0: %d of 4 renders hash-identical to the base commit\n", sameBits);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (sameBits == 4);
   #endif
}

TEST (parity190fy_spiral_revision1_reads_the_loop_sooner)
{
    using namespace lynx;
    namespace sp = echosuite::spiral;
    auto p = makeEffectSlot (EffectKind::spiralEcho).params; // feedback 50 %, mix 50 %
    p[(std::size_t) sp::delay] = 0.0f;                         // the reference's default, 0.01 ms (LYNX's is 100 ms)
    // [EXEC] the reference answers its 0.01 ms floor one sample after the input (tp172d D, refcap0124 default); revision 1
    // two (its one-sample read + the shifter's own sample), revision 0 three
    CHECK (parity190fy::firstWet (p, 48000.0) == 2);
    p[(std::size_t) sp::revision] = 0.0f;
    CHECK (parity190fy::firstWet (p, 48000.0) == 3);
    // a longer time: the read moves 1.8 samples (48 kHz) sooner, the echo's first sample with it
    p[(std::size_t) sp::delay] = sp::delayValue (10.0); // 480 samples
    p[(std::size_t) sp::feedback] = 0.0f;
    const auto r0 = parity190fy::firstWet (p, 48000.0, 1024);
    p[(std::size_t) sp::revision] = 1.0f;
    const auto r1 = parity190fy::firstWet (p, 48000.0, 1024);
    CHECK (r0 > 470 && r0 < 490);
    CHECK (r1 == r0 - 2 || r1 == r0 - 1);
    // Classic ignores the revision: the same render either way
    p[(std::size_t) sp::law] = 0.0f;
    const auto c1 = parity190fy::firstWet (p, 48000.0, 1024);
    p[(std::size_t) sp::revision] = 0.0f;
    CHECK (parity190fy::firstWet (p, 48000.0, 1024) == c1);
}

TEST (parity190fy_spiral_revision1_stays_finite_and_bounded)
{
    using namespace lynx;
    namespace sp = echosuite::spiral;
    for (const double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const float time : { 0.0f, 0.0005f, 0.25f, 1.0f })
            for (const float fb : { 0.0f, 0.5f, 1.0f })
            {
                auto p = makeEffectSlot (EffectKind::spiralEcho).params;
                p[(std::size_t) sp::delay] = time;
                p[(std::size_t) sp::feedback] = fb;
                p[(std::size_t) sp::shift] = fb > 0.9f ? 0.9f : 0.5f;
                p[(std::size_t) sp::lowCut] = 0.0f;
                p[(std::size_t) sp::highCut] = 1.0f;
                auto fx = makeEffect (EffectKind::spiralEcho);
                fx->prepare (sr);
                const int n = (int) (0.5 * sr), block = 333;
                std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
                for (int i = 0; i < n / 4; ++i)
                    l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.9 * std::sin (0.031 * (double) i + 0.0007 * (double) (i * i % 1013)));
                for (int off = 0; off < n; off += block)
                    fx->process (l.data() + off, r.data() + off, std::min (block, n - off), p, 120.0);
                float peak = 0.0f;
                bool finite = true;
                for (int i = 0; i < n; ++i)
                {
                    finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
                    peak = std::max ({ peak, std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]) });
                }
                CHECK (finite);
                CHECK (peak < 3.0f);
            }
}
