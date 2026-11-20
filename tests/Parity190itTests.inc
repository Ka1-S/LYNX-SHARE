// Round 190it: the reverb stand-ins' 0.1.30 revision, measured on refcap030's black-box captures through LYNX's own
// state translators (/mnt/e/lynx-work/parity190it/HANDOFF.md, docs/PARITY.md "Round 190it"). Epoch Verb, Atrium, Foil Plate
// and Halo get a hidden revision of the Measured law (trims per mode / type, Atrium's and Halo's build-up taps in four
// groups above 600 Hz, Halo's Dark colour on the output and its level laws), Lattice Mod a revision of its tap network
// (the longest tap feeds the loop; the type values past the tenth step play the dry signal), LuxeVerb's Space engine a
// hidden Ducking that only the Pro-R 2 translator sets. New instances and translated states get the revisions; a
// project saved before loads them as 0 and renders bit for bit as the base commit did (fingerprints below).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/FabFilterStates.h"
#include "core/LuxeVerb.h"
#include "core/LynxVerbs.h"

namespace parity190it
{
// the gold input (as /mnt/e/lynx-work/parity190it/gold/gold.cpp): an impulse, a noise burst, 220 Hz + 1 kHz tones
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 777u;
    const int n = (int) l.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        s = s * 1664525u + 1013904223u;
        const double nz = (double) (s >> 8) / 8388608.0 - 1.0;
        double v = i == (int) (0.05 * sr) ? 0.5 : 0.0;
        if (t > 0.3 && t < 0.45) v += 0.25 * nz;
        if (t > 0.6 && t < 1.2) v += 0.3 * std::sin (2.0 * M_PI * 220.0 * t) + 0.1 * std::sin (2.0 * M_PI * 1000.0 * t);
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (t > 0.3 && t < 0.45 ? 0.7 * v : v);
    }
}
inline void render (const lynx::EffectSlot& slot, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
}
struct Case { lynx::EffectKind kind; int rev, idx; float val; int idx2; float val2; };
inline std::vector<Case> cases()
{
    using lynx::EffectKind;
    const int lat = (int) lynx::echosuite::lattice::revision, duck = lynx::luxeVerbSpaceDucking;
    return { { EffectKind::epochVerb, 18, -1, 0, -1, 0 }, { EffectKind::epochVerb, 18, 16, 0.0476f, 2, 0.1f },
             { EffectKind::epochVerb, 18, 16, 0.2857f, 4, 0.8f }, { EffectKind::epochVerb, 18, 16, 0.6667f, 15, 0.0f },
             { EffectKind::atrium, 23, -1, 0, -1, 0 }, { EffectKind::atrium, 23, 19, 0.0909f, 20, 0.6f },
             { EffectKind::atrium, 23, 19, 0.6364f, 2, 0.3f }, { EffectKind::atrium, 23, 19, 0.9091f, 5, 0.9f },
             { EffectKind::foilPlate, 17, -1, 0, -1, 0 }, { EffectKind::foilPlate, 17, 11, 0.3636f, -1, 0 },
             { EffectKind::foilPlate, 17, 14, 1.0f, -1, 0 }, { EffectKind::foilPlate, 17, 11, 0.9091f, 16, 0.0f },
             { EffectKind::halo, 13, -1, 0, -1, 0 }, { EffectKind::halo, 13, 11, 1.0f, 9, 0.625f },
             { EffectKind::halo, 13, 9, 0.25f, 3, 0.2f }, { EffectKind::halo, 13, 12, 0.0f, 11, 1.0f },
             { EffectKind::latticeMod, lat, -1, 0, -1, 0 }, { EffectKind::latticeMod, lat, 29, 0.35f, 4, 0.5f },
             { EffectKind::latticeMod, lat, 29, 0.6f, 9, 0.8f },
             { EffectKind::luxeVerb, duck, lynx::luxeVerbSpaceFirst, 1.0f, -1, 0 },
             { EffectKind::luxeVerb, duck, lynx::luxeVerbSpaceFirst, 1.0f, lynx::luxeVerbSpaceFirst + 2, 0.2f } };
}
inline double energy (const std::vector<float>& l, const std::vector<float>& r, std::uint64_t* hash = nullptr)
{
    std::uint64_t h = 1469598103934665603ull;
    double e = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
        for (const float v : { l[i], r[i] })
        {
            std::uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int b = 0; b < 4; ++b) { h ^= (u >> (8 * b)) & 0xffu; h *= 1099511628211ull; }
            e += (double) v * v;
        }
    if (hash != nullptr) *hash = h;
    return e;
}
} // namespace parity190it

TEST (parity190it_revisions_are_hidden_new_instances_run_them_old_projects_do_not)
{
    using namespace lynx;
    const std::pair<EffectKind, int> revs[] = { { EffectKind::epochVerb, verbs::epoch::revision }, { EffectKind::atrium, verbs::atrium::revision },
                                                { EffectKind::foilPlate, verbs::foil::revision }, { EffectKind::halo, verbs::halo::revision },
                                                { EffectKind::latticeMod, (int) echosuite::lattice::revision } };
    for (const auto& [kind, rev] : revs)
    {
        CHECK (effectInfo (kind).params.size() == (std::size_t) rev); // hidden: no new control, no new string
        auto s = makeEffectSlot (kind);
        CHECK (s.params[(std::size_t) rev] == 1.0f);
        // saved, since it is not zero (round 190ln: Foil Plate's REA law, one further, is saved with it; round 190kw: Epoch's
        // loop law and round 190ly: its Concert Hall short-decay trim, two further, likewise; Atrium's dense law is set only by
        // the ValhallaRoom translator, so a new Atrium saves the revision only)
        CHECK (savedEffectParamCount (s) == (std::size_t) rev + (kind == EffectKind::epochVerb ? 3 : kind == EffectKind::foilPlate ? 2 : 1));
        auto now = s;
        upgradeLoadedEffectParams (now, (std::size_t) rev + 1);
        CHECK (now.params[(std::size_t) rev] == 1.0f);
        auto old = s; // saved before this round: the 0.1.27 law, and saved again it stays that way
        upgradeLoadedEffectParams (old, (std::size_t) rev);
        CHECK (old.params[(std::size_t) rev] == 0.0f);
        CHECK (savedEffectParamCount (old) == (std::size_t) rev);
    }
    CHECK (effectInfo (EffectKind::luxeVerb).params.size() == (std::size_t) luxeVerbSpaceDucking);
    CHECK (makeEffectSlot (EffectKind::luxeVerb).params[(std::size_t) luxeVerbSpaceDucking] == 0.0f);
}

TEST (parity190it_revision0_renders_as_the_base_commit_did)
{
    using namespace lynx;
    // 1.6 s of the gold input at 48 kHz in blocks of 512: FNV-1a of the float bits and the energy. From the base commit's
    // objects (/mnt/e/lynx-work/parity190it/gold: gold_base.txt, 63 renders at 44.1 / 48 / 96 kHz; the round's objects
    // with every revision / Ducking forced to 0 print the same lines, gold_new.txt)
    struct Gold { int k; std::uint64_t hash; double energy; };
    const Gold gold[] = {
        { 0, 0xb6c7e4134cae1bbcull, 6517.55975 },
        { 1, 0x591b61a5c3bb686aull, 355.891712 },
        { 2, 0x74116650d21cde5full, 11192.4753 },
        { 3, 0x3bf139392d87891full, 742.622831 },
        { 4, 0x173f6572968912a1ull, 1199.47338 },
        { 5, 0x0b32b6bf4d3a3c8aull, 5082.16125 },
        { 6, 0xb1635f09bc96f496ull, 772.247104 },
        { 7, 0xd4e489ae0bbe8a93ull, 2035.55809 },
        { 8, 0xb8d80cc82b8d7851ull, 2952.56788 },
        { 9, 0xa43c38a01af9f866ull, 2675.826 },
        { 10, 0x6584841fd046f7d2ull, 10610.4059 },
        { 11, 0x0f2aab07e6b82380ull, 2120.051 },
        { 12, 0xbc7cd2071453d014ull, 1921.89742 },
        { 13, 0x01a506e63b3b0882ull, 2050.69175 },
        { 14, 0x17b2b0b8960aab69ull, 1572.79412 },
        { 15, 0xf8ba8283709e79e6ull, 1359.16207 },
        { 16, 0xe2b921f4a44ed867ull, 2125.82548 },
        { 17, 0x3245731dee6b0558ull, 69221.7243 },
        { 18, 0x46f99f19c681a77bull, 3151.16293 },
        { 19, 0x8739eae59cc1e050ull, 2629.43206 },
        { 20, 0x1c0af975acf851dfull, 2589.13447 },
    };
    const auto cs = parity190it::cases();
    for (const auto& g : gold)
    {
        const auto& c = cs[(std::size_t) g.k];
        auto slot = makeEffectSlot (c.kind);
        slot.params[(std::size_t) c.rev] = 0.0f;
        if (c.kind == EffectKind::luxeVerb) // round 190kw: … and the Space engine's revision (a project saved before)
            slot.params[(std::size_t) luxeVerbSpaceRevision] = 0.0f;
        if (c.idx >= 0) slot.params[(std::size_t) c.idx] = c.val;
        if (c.idx2 >= 0) slot.params[(std::size_t) c.idx2] = c.val2;
        const int n = (int) (1.6 * 48000.0);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        parity190it::signal (l, r, 48000.0);
        parity190it::render (slot, 48000.0, l, r);
        std::uint64_t h = 0;
        const double e = parity190it::energy (l, r, &h);
       #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 43))
        CHECK (h == g.hash); // the libm the fingerprints were taken with (sin / pow / exp may differ elsewhere)
       #endif
        CHECK (std::abs (e - g.energy) < 1e-6 * g.energy + 1e-9);
    }
}

TEST (parity190it_lattice_revision1_longest_tap_loop_and_dry_types)
{
    using namespace lynx;
    namespace la = echosuite::lattice;
    const double sr = 48000.0;
    const int n = (int) (1.6 * sr);
    const auto run = [&] (float taps, float rev, std::vector<float>& l, std::vector<float>& r) {
        auto slot = makeEffectSlot (EffectKind::latticeMod);
        slot.params[(std::size_t) la::taps] = taps;
        slot.params[(std::size_t) la::feedback] = 0.5f;
        slot.params[(std::size_t) la::revision] = rev;
        l.assign ((std::size_t) n, 0.0f); r.assign ((std::size_t) n, 0.0f);
        parity190it::signal (l, r, sr);
        parity190it::render (slot, sr, l, r);
    };
    std::vector<float> l0, r0, l1, r1, in (n), inR (n);
    parity190it::signal (in, inR, sr);
    // 8 taps (the reference's 0.35): the summed loop piled the low end up (its loop gain there is √8 × the feedback)
    run (0.35f, 0.0f, l0, r0);
    run (0.35f, 1.0f, l1, r1);
    const double e0 = parity190it::energy (l0, r0), e1 = parity190it::energy (l1, r1), ein = parity190it::energy (in, inR);
    std::printf ("  lattice 8 taps, feedback 50 %%: energy %.0f -> %.0f (input %.0f)\n", e0, e1, ein);
    CHECK (e1 < 0.25 * e0 && e1 < 3.0 * ein);
    // past the tenth step (0.4375 …): the dry signal alone, at unity
    run (0.6f, 1.0f, l1, r1);
    bool same = true;
    for (int i = 0; i < n; ++i) same = same && l1[(std::size_t) i] == in[(std::size_t) i] && r1[(std::size_t) i] == inR[(std::size_t) i];
    CHECK (same);
}

TEST (parity190it_pro_r2_ducking_is_translated_and_dips_the_tail)
{
    using namespace lynx;
    std::vector<float> v (136, 0.0f);
    v[0] = 0.5f; v[2] = 0.5f; v[3] = 0.5f; v[5] = 0.3f; v[7] = 0.7f; v[9] = 100.0f; v[16] = 0.0f;
    v[8] = 24.0f; // Ducking 50 % (24 dB)
    const auto st = fabfilter::parseState (p172b::ffbs (v));
    CHECK (st.has_value());
    auto slot = makeEffectSlot (EffectKind::luxeVerb);
    if (st) fabfilter::fromProR2 (*st, slot);
    CHECK (std::abs (slot.params[(std::size_t) luxeVerbSpaceDucking] - 0.5f) < 1e-6);
    // a 0.3 s noise burst: during it the wet part sits ~17 dB (0.71 × 24) under the same setting without ducking
    const double sr = 48000.0;
    const int n = (int) (1.0 * sr);
    const auto run = [&] (float d) {
        auto s = slot;
        s.params[(std::size_t) luxeVerbSpaceDucking] = d;
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        unsigned q = 5u;
        for (int i = (int) (0.1 * sr); i < (int) (0.4 * sr); ++i)
        {
            q = q * 1664525u + 1013904223u;
            l[(std::size_t) i] = r[(std::size_t) i] = 0.5f * ((float) (q >> 8) / 8388608.0f - 1.0f);
        }
        parity190it::render (s, sr, l, r);
        double e = 0.0;
        for (int i = (int) (0.3 * sr); i < (int) (0.4 * sr); ++i) e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return 10.0 * std::log10 (e + 1e-30);
    };
    const double dip = run (0.0f) - run (0.5f);
    std::printf ("  pro-r2 ducking 50 %%: the tail %.1f dB down during the burst\n", dip);
    CHECK (dip > 14.0 && dip < 20.0);
}
