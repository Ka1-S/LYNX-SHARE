// Round 190ln (ValhallaPlate → Foil Plate): the reference's own EQ and bass-decay laws, read by static REA of ValhallaPlate
// (docs/analise/REA_rodada190ln.md; docs/PARITY.md "Round 190ln (Valhalla reverbs)"). A hidden step past round 190it's
// revision, `verbs::foil::reaLaw` (no control, no string, saved while non-zero): a new instance and a translated state run it;
// a project saved before loads 0 and renders as the base commit did (fingerprints below). Included after Parity190itTests.inc
// (its gold helpers).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/Effects.h"
#include "core/LynxVerbs.h"

namespace parity190lnvr
{
// RMS (dB) of the left channel over the last second of 3 s of a steady sine through Foil Plate at 48 kHz, blocks of 512
inline double sineDb (const lynx::EffectSlot& slot, double hz)
{
    const double sr = 48000.0;
    const int n = (int) (3.0 * sr);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.25 * std::sin (2.0 * M_PI * hz * i / sr));
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
    double e = 0.0;
    for (int i = n - (int) sr; i < n; ++i)
        e += (double) l[(std::size_t) i] * l[(std::size_t) i];
    return 10.0 * std::log10 (e / sr + 1e-30);
}
} // namespace parity190lnvr

TEST (parity190ln_vr_foil_rea_law_is_hidden_new_instances_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace fo = verbs::foil;
    CHECK (effectInfo (EffectKind::foilPlate).params.size() == (std::size_t) fo::revision); // hidden: no control, no string
    CHECK (fo::reaLaw == fo::revision + 1);
    auto s = makeEffectSlot (EffectKind::foilPlate);
    CHECK (s.params[(std::size_t) fo::revision] == 1.0f);
    CHECK (s.params[(std::size_t) fo::reaLaw] == 1.0f);
    CHECK (savedEffectParamCount (s) == (std::size_t) fo::reaLaw + 1);
    auto now = s; // saved with it: kept
    upgradeLoadedEffectParams (now, (std::size_t) fo::reaLaw + 1);
    CHECK (now.params[(std::size_t) fo::reaLaw] == 1.0f && now.params[(std::size_t) fo::revision] == 1.0f);
    auto rev1 = s; // saved by 0.1.30 before this round: revision 1 as round 190it left it, and saved again it stays so
    upgradeLoadedEffectParams (rev1, (std::size_t) fo::revision + 1);
    CHECK (rev1.params[(std::size_t) fo::revision] == 1.0f && rev1.params[(std::size_t) fo::reaLaw] == 0.0f);
    CHECK (savedEffectParamCount (rev1) == (std::size_t) fo::revision + 1);
    auto rev0 = s; // saved before round 190it: the 0.1.27 Measured law
    upgradeLoadedEffectParams (rev0, (std::size_t) fo::revision);
    CHECK (rev0.params[(std::size_t) fo::revision] == 0.0f && rev0.params[(std::size_t) fo::reaLaw] == 0.0f);
    CHECK (savedEffectParamCount (rev0) == (std::size_t) fo::revision);
}

TEST (parity190ln_vr_foil_revision1_renders_as_the_base_commit_did)
{
    using namespace lynx;
    // parity190it's gold input (1.6 s at 48 kHz in blocks of 512), revision 1 with the REA law off: FNV-1a of the float bits
    // and the energy, from the base commit's objects (/mnt/e/lynx-work/rea3_190ln/vr/gold: gold_base.txt, 42 renders of
    // revisions 0 and 1 at 44.1 / 48 / 96 kHz; the round's objects print the same lines, gold_new.txt)
    struct Gold { float type, decay, lowGain, highGain, lowFreq, voicing; std::uint64_t hash; double energy; };
    const Gold gold[] = {
        { 0.0f, 0.5f, 0.5f, 0.5f, 0.3488f, 0.0f, 0xb8d80cc82b8d7851ull, 2952.56788 },
        { 0.0f, 0.25f, 0.5f, 0.3f, 0.3488f, 0.0f, 0x2f06073643a4c474ull, 1193.64334 },
        { 0.0f, 0.8f, 0.8f, 0.5f, 0.3488f, 0.0f, 0xac74e5e4433366acull, 14918.0745 },
        { 0.3636f, 0.2f, 0.2f, 0.7f, 0.1f, 0.0f, 0x3b38e42efece9866ull, 916.640569 },
        { 0.6364f, 0.5f, 0.5f, 0.9f, 0.6f, 0.0f, 0x4dda62d8d36c0e5full, 3605.7175 },
        { 0.9091f, 0.95f, 0.9f, 0.1f, 0.3488f, 0.0f, 0xa12d8585fa3edfeeull, 16777.0401 },
        { 0.0f, 0.5f, 0.8f, 0.2f, 0.3488f, 1.0f, 0x11f9ecfc4503109full, 35789.4085 },
    };
    for (const auto& g : gold)
    {
        auto slot = makeEffectSlot (EffectKind::foilPlate);
        slot.params[(std::size_t) verbs::foil::revision] = 1.0f;
        slot.params[(std::size_t) verbs::foil::reaLaw] = 0.0f;
        slot.params[11] = g.type; slot.params[2] = g.decay; slot.params[8] = g.lowGain;
        slot.params[10] = g.highGain; slot.params[7] = g.lowFreq; slot.params[14] = g.voicing;
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

TEST (parity190ln_vr_foil_low_eq_is_the_references_first_order_shelf)
{
    using namespace lynx;
    namespace fo = verbs::foil;
    // Low EQ at 218 Hz (0.1), +12 dB: the reference's first-order shelf (gLow + s/ω0) / (1 + s/ω0) still lifts 1 kHz by
    // 20·log10 |4 + j·4.59| / |1 + j·4.59| = +2.3 dB; the second-order shelf of revision 1 (half gain near 100 Hz) ~0 dB
    const auto lift = [] (float rea) {
        auto a = makeEffectSlot (EffectKind::foilPlate);
        a.params[(std::size_t) fo::reaLaw] = rea;
        a.params[7] = 0.1f;
        a.params[8] = 0.5f;
        auto b = a;
        b.params[8] = 1.0f;
        return parity190lnvr::sineDb (b, 1000.0) - parity190lnvr::sineDb (a, 1000.0);
    };
    const auto withLaw = lift (1.0f), without = lift (0.0f);
    std::printf ("  Foil Low EQ +12 dB at 218 Hz, at 1 kHz: %+.2f dB (REA law) / %+.2f dB (revision 1)\n", withLaw, without);
    CHECK (std::abs (withLaw - 2.25) < 0.4);
    CHECK (without < 1.0);
}

TEST (parity190ln_vr_foil_bass_decay_law)
{
    using namespace lynx;
    namespace fo = verbs::foil;
    CHECK (fo::reaBassRtMult (1.0) == 1.0 && fo::reaBassRtMult (2.5) == 1.0);
    CHECK (std::abs (fo::reaBassRtMult (6.25) - 1.2) < 1e-12);
    CHECK (std::abs (fo::reaBassRtMult (10.0) - 1.4) < 1e-12 && std::abs (fo::reaBassRtMult (30.0) - 1.4) < 1e-12);
    // the plate voicing (Little Plate's role) never reads the law: the same render with it on or off
    auto a = makeEffectSlot (EffectKind::foilPlate);
    a.params[14] = 1.0f;
    a.params[8] = 0.9f;
    auto b = a;
    b.params[(std::size_t) fo::reaLaw] = 0.0f;
    CHECK (parity190lnvr::sineDb (a, 300.0) == parity190lnvr::sineDb (b, 300.0));
}
