// Round 190kw (VintageVerb → Epoch): Epoch Verb's level follows the reference's loop law (static REA of
// ValhallaVintageVerb, docs/analise/REA_rodada190kw.md; docs/PARITY.md "Round 190kw (VintageVerb → Epoch)"). A hidden step
// past round 190it's revision, `verbs::epoch::loopLaw` (no control, no string, saved while non-zero): a new instance and a
// translated state run it; a project saved before loads 0 and renders as the base commit did (fingerprints below).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/Effects.h"
#include "core/LynxVerbs.h"

namespace parity190kwvv
{
// 4 s of a lone impulse (0.5 at 0.05 s), Epoch rendered at 48 kHz in blocks of 512; returns the energy from sample `from`
// over `count` samples (all when count < 0)
inline double impulseEnergy (const lynx::EffectSlot& slot, int from = 0, int count = -1)
{
    const double sr = 48000.0;
    const int n = (int) (4.0 * sr);
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    l[(std::size_t) (0.05 * sr)] = r[(std::size_t) (0.05 * sr)] = 0.5f;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
    double e = 0.0;
    const int end = count < 0 ? n : std::min (n, from + count);
    for (int i = from; i < end; ++i)
        e += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
    return e;
}
} // namespace parity190kwvv

TEST (parity190kw_vv_loop_law_is_hidden_new_instances_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace ep = verbs::epoch;
    CHECK (effectInfo (EffectKind::epochVerb).params.size() == (std::size_t) ep::revision); // hidden: no control, no string
    CHECK (ep::loopLaw == ep::revision + 1);
    auto s = makeEffectSlot (EffectKind::epochVerb);
    CHECK (s.params[(std::size_t) ep::revision] == 1.0f);
    CHECK (s.params[(std::size_t) ep::loopLaw] == 1.0f);
    CHECK (savedEffectParamCount (s) == (std::size_t) ep::loopLaw + 2); // (+ round 190ly's shortTrim after it, since the 190mb merge)
    auto now = s; // saved with it: kept
    upgradeLoadedEffectParams (now, (std::size_t) ep::loopLaw + 1);
    CHECK (now.params[(std::size_t) ep::loopLaw] == 1.0f && now.params[(std::size_t) ep::revision] == 1.0f);
    auto rev1 = s; // saved by 0.1.30 before this round: revision 1 as round 190it left it, and saved again it stays so
    upgradeLoadedEffectParams (rev1, (std::size_t) ep::revision + 1);
    CHECK (rev1.params[(std::size_t) ep::revision] == 1.0f && rev1.params[(std::size_t) ep::loopLaw] == 0.0f);
    CHECK (savedEffectParamCount (rev1) == (std::size_t) ep::revision + 1);
    auto rev0 = s; // saved before round 190it: the 0.1.27 Measured law
    upgradeLoadedEffectParams (rev0, (std::size_t) ep::revision);
    CHECK (rev0.params[(std::size_t) ep::revision] == 0.0f && rev0.params[(std::size_t) ep::loopLaw] == 0.0f);
    CHECK (savedEffectParamCount (rev0) == (std::size_t) ep::revision);
}

TEST (parity190kw_vv_revision1_renders_as_the_base_commit_did)
{
    using namespace lynx;
    // as parity190it's gold (its input, 1.6 s at 48 kHz in blocks of 512), revision 1 with the loop law off: FNV-1a of the
    // float bits and the energy, from the base commit's objects (/mnt/e/lynx-work/rea190kw/vvimpl/gold: gold_base.txt, 72
    // renders of revisions 0 and 1 at 44.1 / 48 / 96 kHz; the round's objects print the same lines)
    struct Gold { float mode, decay, size, colour; std::uint64_t hash; double energy; };
    const Gold gold[] = {
        { 0.0f, 0.2f, 1.0f, 0.0f, 0xac9b83d53bfa4302ull, 2029.58948 }, { 0.0f, 0.42f, 1.0f, 0.0f, 0x16a6a0b88aee2fcaull, 6494.94988 },
        { 0.0f, 0.6f, 0.5f, 0.0f, 0xd01018386897af7cull, 18699.7875 }, { 0.0f, 0.38f, 1.0f, 0.5f, 0x08da2423c259f418ull, 4819.14982 },
        { 0.0476f, 0.2f, 1.0f, 0.0f, 0x46439aa4b0225b0bull, 4115.08016 }, { 0.0952f, 0.3f, 1.0f, 0.0f, 0xf5d13de4bf3712e5ull, 612.278442 },
        { 0.2857f, 0.5f, 0.3f, 1.0f, 0xd8e4c8fdf2cdc3feull, 3991.09401 }, { 0.3333f, 0.25f, 1.0f, 0.0f, 0x087242ba1c22c586ull, 1768.27475 },
        { 0.4762f, 0.2f, 0.7f, 0.5f, 0xf760a82427f3c386ull, 1902.43853 }, { 0.6667f, 0.1f, 1.0f, 0.0f, 0x06b97686273eaf2bull, 1232.13499 },
        { 0.8571f, 0.2f, 1.0f, 0.0f, 0x4e057dfef717aa13ull, 300.733223 }, { 1.0f, 0.35f, 0.0f, 0.0f, 0xc8d8347322b65d6dull, 2312.93265 },
    };
    for (const auto& g : gold)
    {
        auto slot = makeEffectSlot (EffectKind::epochVerb);
        slot.params[(std::size_t) verbs::epoch::revision] = 1.0f;
        slot.params[(std::size_t) verbs::epoch::loopLaw] = 0.0f;
        slot.params[(std::size_t) verbs::epoch::shortTrim] = 0.0f; // (rollup step 10: round 190ly's trim off too, as at the base commit)
        slot.params[16] = g.mode; slot.params[2] = g.decay; slot.params[3] = g.size; slot.params[15] = g.colour;
        const int n = (int) (1.6 * 48000.0);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        parity190it::signal (l, r, 48000.0);
        parity190it::render (slot, 48000.0, l, r);
        std::uint64_t h = 0;
        const double e = parity190it::energy (l, r, &h);
       #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 43))
        CHECK (h == g.hash); // the libm the fingerprints were taken with
       #endif
        CHECK (std::abs (e - g.energy) < 1e-6 * g.energy + 1e-9);
    }
}

TEST (parity190kw_vv_concert_hall_level_follows_the_loop_law)
{
    using namespace lynx;
    namespace ep = verbs::epoch;
    // the reference's Concert Hall against revision 1 (refcap030 pairs / sr48, impulse): +2.0 dB at Decay 0.51 s, −1.2 …
    // −1.3 dB at 7 s, −1.3 … −1.9 dB at 33 s (its loop gain sees Decay only after the first half-lap)
    const auto db = [] (float decay) {
        auto a = makeEffectSlot (EffectKind::epochVerb);
        a.params[2] = decay;
        auto b = a;
        b.params[(std::size_t) ep::loopLaw] = 0.0f;
        return 10.0 * std::log10 (parity190kwvv::impulseEnergy (a) / parity190kwvv::impulseEnergy (b));
    };
    const auto at051 = db (0.2f), at7 = db (0.5f);
    std::printf ("  Concert Hall, loop law against revision 1: %+.2f dB at 0.51 s, %+.2f dB at 7 s\n", at051, at7);
    CHECK (at051 > 1.4 && at051 < 2.6);
    CHECK (at7 < -0.8 && at7 > -2.4);
}
