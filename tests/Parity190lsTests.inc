// Round 190ls (ValhallaRoom → Atrium): the ValhallaRoom role on a dense late tank. Static REA of ValhallaRoom (round 190ln,
// docs/analise/REA_rodada190ls.md) shows a late network of 32 lines (two halves of 16, eight modulators each moving two lines
// in opposite directions) where Atrium had 8. A hidden step past round 190it's revision, `verbs::atrium::reaLaw` (no
// control, no string, saved while non-zero): a new instance and a translated state run the engine's dense tank
// (Settings::dense) with its laws refitted on refcap030; a project saved before loads 0 and renders as the base commit did
// (fingerprints below). Included after Parity190itTests.inc (its gold helpers).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/Effects.h"
#include "core/LynxVerbs.h"

namespace parity190ls
{
// energy (dB) of the left + right output in [a, b) seconds of an impulse at 0.05 s through the slot at 48 kHz, blocks of 512
inline double windowDb (const lynx::EffectSlot& slot, double a, double b, double total = 3.0)
{
    const double sr = 48000.0;
    const int n = (int) (total * sr);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    l[(std::size_t) (0.05 * sr)] = r[(std::size_t) (0.05 * sr)] = 0.5f;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
    double e = 0.0;
    for (int i = (int) (a * sr); i < std::min (n, (int) (b * sr)); ++i)
    {
        if (! std::isfinite (l[(std::size_t) i]) || ! std::isfinite (r[(std::size_t) i]))
            return 1000.0;
        e += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
    }
    return 10.0 * std::log10 (e + 1e-30);
}
} // namespace parity190ls

TEST (parity190ls_atrium_dense_law_is_hidden_new_instances_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace at = verbs::atrium;
    CHECK (effectInfo (EffectKind::atrium).params.size() == (std::size_t) at::revision); // hidden: no control, no string
    CHECK (at::reaLaw == at::revision + 1);
    auto s = makeEffectSlot (EffectKind::atrium);
    CHECK (s.params[(std::size_t) at::revision] == 1.0f);
    CHECK (s.params[(std::size_t) at::reaLaw] == 0.0f); // round 190ly: only a translated ValhallaRoom sets it (Parity190ly)
    s.params[(std::size_t) at::reaLaw] = 1.0f;
    CHECK (savedEffectParamCount (s) == (std::size_t) at::reaLaw + 1);
    auto now = s; // saved with it: kept
    upgradeLoadedEffectParams (now, (std::size_t) at::reaLaw + 1);
    CHECK (now.params[(std::size_t) at::reaLaw] == 1.0f && now.params[(std::size_t) at::revision] == 1.0f);
    auto rev1 = s; // saved by 0.1.30 before this round: revision 1 as round 190it left it
    upgradeLoadedEffectParams (rev1, (std::size_t) at::revision + 1);
    CHECK (rev1.params[(std::size_t) at::revision] == 1.0f && rev1.params[(std::size_t) at::reaLaw] == 0.0f);
    CHECK (savedEffectParamCount (rev1) == (std::size_t) at::revision + 1);
    auto rev0 = s; // saved before round 190it
    upgradeLoadedEffectParams (rev0, (std::size_t) at::revision);
    CHECK (rev0.params[(std::size_t) at::revision] == 0.0f && rev0.params[(std::size_t) at::reaLaw] == 0.0f);
}

TEST (parity190ls_atrium_without_the_dense_law_renders_as_the_base_commit_did)
{
    using namespace lynx;
    // parity190it's gold input (1.6 s at 48 kHz in blocks of 512), revisions 0 and 1 with the dense law off: FNV-1a of the
    // float bits and the energy, from the base commit's objects (/mnt/e/lynx-work/valhalla190ls/gold: gold_base.txt, 42
    // renders at 44.1 / 48 / 96 kHz; the round's objects print the same lines, gold_new.txt)
    struct Gold { int rev; float type, decay, mod, earlyLate; std::uint64_t hash; double energy; };
    const Gold gold[] = {
        { 0, 0.0f, 0.5f, 0.5f, 0.5f, 0x173f6572968912a1ull, 1199.47338 },
        { 0, 0.0f, 0.3f, 0.9f, 0.8f, 0x0e308e738b38d8eeull, 2038.9714 },
        { 0, 0.0909f, 0.6f, 0.2f, 0.3f, 0xa6c8d38e65f2769dull, 1501.31279 },
        { 0, 0.2727f, 0.45f, 0.5f, 0.5f, 0x5549dc24ae60c3a0ull, 4291.82891 },
        { 0, 0.5455f, 0.7f, 0.5f, 0.9f, 0xe562f273d03c62f1ull, 3899.31406 },
        { 0, 0.8182f, 0.55f, 0.7f, 0.6f, 0xa70df14e2891dd0cull, 1969.79882 },
        { 0, 1.0f, 0.5f, 0.5f, 0.5f, 0xd48eb650fbcd2a61ull, 1027.42102 },
        { 1, 0.0f, 0.5f, 0.5f, 0.5f, 0xadf6053f819584e1ull, 1586.0641 },
        { 1, 0.0f, 0.3f, 0.9f, 0.8f, 0x8d150b510eb41026ull, 2684.46644 },
        { 1, 0.0909f, 0.6f, 0.2f, 0.3f, 0xfada21ea78653693ull, 1111.02279 },
        { 1, 0.2727f, 0.45f, 0.5f, 0.5f, 0x25c88e7c85ccd116ull, 2658.18603 },
        { 1, 0.5455f, 0.7f, 0.5f, 0.9f, 0x04f672c8dd52b7e2ull, 8411.05274 },
        { 1, 0.8182f, 0.55f, 0.7f, 0.6f, 0x360a8ead27575c11ull, 8844.68399 },
        { 1, 1.0f, 0.5f, 0.5f, 0.5f, 0x508cf5e39a25f372ull, 1248.59437 },
    };
    for (const auto& g : gold)
    {
        auto slot = makeEffectSlot (EffectKind::atrium);
        slot.params[(std::size_t) verbs::atrium::revision] = (float) g.rev;
        slot.params[(std::size_t) verbs::atrium::reaLaw] = 0.0f;
        slot.params[19] = g.type; slot.params[2] = g.decay; slot.params[8] = g.mod; slot.params[4] = g.earlyLate;
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

TEST (parity190ls_atrium_dense_tank_decays_and_keeps_the_level)
{
    using namespace lynx;
    namespace at = verbs::atrium;
    // every type, a short and a long decay: the dense tank is stable (finite, falling) and its tail sits within a few dB of
    // the 8-line tank's (the front-ends' levels hold: the tank is read at the 8-line tank's 1 / √8)
    for (int type = 0; type < at::typeCount; ++type)
        for (const float decay : { 0.35f, 0.75f })
        {
            auto a = makeEffectSlot (EffectKind::atrium);
            a.params[(std::size_t) at::reaLaw] = 1.0f; // (round 190ly: set by the ValhallaRoom translator only)
            a.params[19] = (float) type / (float) (at::typeCount - 1);
            a.params[2] = decay;
            auto b = a;
            b.params[(std::size_t) at::reaLaw] = 0.0f;
            const auto early = parity190ls::windowDb (a, 0.3, 0.8), late = parity190ls::windowDb (a, 2.0, 3.0);
            const auto ref = parity190ls::windowDb (b, 0.3, 0.8);
            CHECK (early < 100.0 && late < 100.0);
            CHECK (late < early);
            CHECK (std::abs (early - ref) < 4.0);
        }
}
