// Round 190mb (Valhalla → LYNX stand-ins, part 3). Included after Parity190lyTests.inc (its render helpers).
// 1. ValhallaSupermassive → Nebula: the classic networks' per-mode tone and level (hidden nebula::toneLaw, set only by the
//    Supermassive translator's `replaced` items; law 0's loop gets law 1's octave-band tone corrector), from Feedback 65 %
//    to its full value at 80 %; the cloud networks, a new Nebula and every project saved before keep their sound.
#include <cmath>
#include <string>

#include "core/Effects.h"
#include "core/LynxVerbs.h"
#include "core/PluginSubstitutes.h"

TEST (parity190mb_nebula_supermassive_tone_law_is_hidden_and_translator_only)
{
    using namespace lynx;
    namespace nb = verbs::nebula;
    CHECK (nb::toneLaw == nb::law + 1 && effectInfo (EffectKind::nebula).params.size() == (std::size_t) nb::toneLaw);
    const auto fresh = makeEffectSlot (EffectKind::nebula);
    CHECK (fresh.params[(std::size_t) nb::toneLaw] == 0.0f);
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "ValhallaSupermassive";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    CHECK (entry->effect == EffectKind::nebula);
    const auto translated = makeSubstituteSlot (*entry, &id);
    CHECK (translated.params[(std::size_t) nb::toneLaw] == 1.0f && savedEffectParamCount (translated) == (std::size_t) nb::toneLaw + 1);
    auto reopened = translated;
    upgradeLoadedEffectParams (reopened, savedEffectParamCount (translated));
    CHECK (reopened.params[(std::size_t) nb::toneLaw] == 1.0f);
    auto old = translated; // saved before this round
    upgradeLoadedEffectParams (old, (std::size_t) nb::toneLaw);
    CHECK (old.params[(std::size_t) nb::toneLaw] == 0.0f);
    CHECK (makeSubstituteSlot (*entry).params[(std::size_t) nb::toneLaw] == 0.0f); // added by hand
    // renders: Expanse (classic, mode 8) at Feedback 80 % moves, at 50 % and a cloud mode (Ripple) are bit for bit
    const double sr = 48000.0;
    const auto run = [sr] (float mode, float fb, float law, std::vector<float>& l, std::vector<float>& r) {
        auto slot = makeEffectSlot (EffectKind::nebula);
        slot.params[0] = 1.0f;  // Mix
        slot.params[3] = 0.2f;  // Delay (short: the default's first pass lands after the render)
        slot.params[5] = fb;    // Feedback
        slot.params[12] = mode; // Mode
        slot.params[(std::size_t) nb::toneLaw] = law;
        const int n = (int) (2.0 * sr);
        l.assign ((std::size_t) n, 0.0f), r.assign ((std::size_t) n, 0.0f);
        l[(std::size_t) (0.05 * sr)] = r[(std::size_t) (0.05 * sr)] = 0.5f;
        parity190ly::render (slot, sr, l, r);
    };
    std::vector<float> l0, r0, l1, r1;
    const auto expanse = (8.0f + 0.5f) / (float) nb::modeCount, ripple = 0.5f / (float) nb::modeCount;
    run (expanse, 0.8f, 0.0f, l0, r0);
    run (expanse, 0.8f, 1.0f, l1, r1);
    CHECK (l0 != l1);
    const auto d = parity190ly::windowDb (l1, r1, sr, 0.0, 2.0) - parity190ly::windowDb (l0, r0, sr, 0.0, 2.0);
    std::printf ("  Nebula Expanse, Feedback 80 %%: Supermassive tone law %+.2f dB\n", d);
    CHECK (std::isfinite (d) && std::abs (d) < 6.0);
    run (expanse, 0.5f, 0.0f, l0, r0);
    run (expanse, 0.5f, 1.0f, l1, r1);
    CHECK (l0 == l1 && r0 == r1);
    run (ripple, 0.8f, 0.0f, l0, r0);
    run (ripple, 0.8f, 1.0f, l1, r1);
    CHECK (l0 == l1 && r0 == r1);
}

// 2. ValhallaVintageVerb → Epoch Verb away from 48 kHz (hidden epoch::rateLaw, the VintageVerb translator only): the
//    reference's own level change per rate octave; nothing at 48 kHz, a new Epoch untouched
TEST (parity190mb_epoch_rate_law_is_translator_only_and_zero_at_48k)
{
    using namespace lynx;
    namespace ep = verbs::epoch;
    CHECK (ep::rateLaw == ep::shortTrim + 1 && effectInfo (EffectKind::epochVerb).params.size() == (std::size_t) ep::paramCount);
    CHECK (makeEffectSlot (EffectKind::epochVerb).params[(std::size_t) ep::rateLaw] == 0.0f);
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "ValhallaVintageVerb";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    const auto vv = makeSubstituteSlot (*entry, &id);
    CHECK (vv.params[(std::size_t) ep::rateLaw] == 1.0f && savedEffectParamCount (vv) == (std::size_t) ep::rateLaw + 1);
    auto old = vv;
    upgradeLoadedEffectParams (old, (std::size_t) ep::rateLaw);
    CHECK (old.params[(std::size_t) ep::rateLaw] == 0.0f);
    const auto run = [] (const EffectSlot& base, float law, double sr, std::vector<float>& l, std::vector<float>& r) {
        auto slot = base;
        slot.params[0] = 1.0f;  // Mix
        slot.params[2] = 0.2f;  // Decay
        slot.params[15] = 0.0f; // the 1970s colour
        slot.params[(std::size_t) ep::rateLaw] = law;
        const int n = (int) (1.0 * sr);
        l.assign ((std::size_t) n, 0.0f), r.assign ((std::size_t) n, 0.0f);
        l[(std::size_t) (0.05 * sr)] = r[(std::size_t) (0.05 * sr)] = 0.5f;
        parity190ly::render (slot, sr, l, r);
    };
    std::vector<float> l0, r0, l1, r1;
    run (vv, 0.0f, 48000.0, l0, r0);
    run (vv, 1.0f, 48000.0, l1, r1);
    CHECK (l0 == l1 && r0 == r1); // 48 kHz: bit for bit
    run (vv, 0.0f, 96000.0, l0, r0);
    run (vv, 1.0f, 96000.0, l1, r1);
    const auto early = parity190ly::windowDb (l1, r1, 96000.0, 0.05, 0.13) - parity190ly::windowDb (l0, r0, 96000.0, 0.05, 0.13);
    std::printf ("  Epoch 1970s at 96 kHz, rate law: first 80 ms %+.2f dB\n", early);
    CHECK (early < -0.2 && early > -2.5);
}

// 3. ValhallaSpaceModulator → Orbit (hidden orbit::smLaw, the SpaceModulator translator only): the reference's wet level per
//    mode, scaled by |feedback| (none at Feedback 0); the plug-in's own mode order is Orbit's, so the Mode map stays direct
TEST (parity190mb_orbit_spacemodulator_level_law)
{
    using namespace lynx;
    namespace ob = echosuite::orbit;
    CHECK ((int) ob::smLaw == (int) ob::paramCount && effectInfo (EffectKind::orbit).params.size() == (std::size_t) ob::paramCount);
    CHECK (makeEffectSlot (EffectKind::orbit).params[(std::size_t) ob::smLaw] == 0.0f);
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "ValhallaSpaceModulator";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    const auto sm = makeSubstituteSlot (*entry, &id);
    CHECK (sm.params[(std::size_t) ob::smLaw] == 1.0f && savedEffectParamCount (sm) >= (std::size_t) ob::smLaw + 1); // round 190mj: + voiceLaw
    auto old = sm;
    upgradeLoadedEffectParams (old, (std::size_t) ob::smLaw);
    CHECK (old.params[(std::size_t) ob::smLaw] == 0.0f);
    // the reference's names in its order (round 190mb's capture) land on Orbit's voices of the same kind
    CHECK (ob::modeOf (0.0f) == ob::Mode::rise && ob::modeOf (0.1875f) == ob::Mode::riseFall && ob::modeOf (0.28125f) == ob::Mode::throughZeroPlus
           && ob::modeOf (0.36458f) == ob::Mode::throughZeroMinus && ob::modeOf (0.55208f) == ob::Mode::octo
           && ob::modeOf (0.72917f) == ob::Mode::driftRise && ob::modeOf (0.91667f) == ob::Mode::ensemble && ob::modeOf (1.0f) == ob::Mode::symphonic);
    CHECK (ob::smModeDb (ob::Mode::octo) < -3.0 && ob::smModeDb (ob::Mode::throughZeroPlus) > 3.0 && ob::smModeDb (ob::Mode::rise) == 0.0);
    const double sr = 48000.0;
    const auto run = [sr] (float fb, float law, std::vector<float>& l, std::vector<float>& r) {
        auto slot = makeEffectSlot (EffectKind::orbit);
        slot.params[(std::size_t) ob::mix] = 1.0f;
        slot.params[(std::size_t) ob::feedback] = fb;
        slot.params[(std::size_t) ob::mode] = ob::modeValue (ob::Mode::octo);
        slot.params[(std::size_t) ob::smLaw] = law;
        const int n = (int) (0.5 * sr);
        l.assign ((std::size_t) n, 0.0f), r.assign ((std::size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = 0.25f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * i / sr);
        parity190ly::render (slot, sr, l, r);
    };
    std::vector<float> l0, r0, l1, r1;
    run (0.5f, 0.0f, l0, r0); // Feedback 0: as before
    run (0.5f, 1.0f, l1, r1);
    CHECK (l0 == l1 && r0 == r1);
    run (0.75f, 0.0f, l0, r0); // +50 %: the full trim
    run (0.75f, 1.0f, l1, r1);
    const auto d = parity190ly::windowDb (l1, r1, sr, 0.1, 0.5) - parity190ly::windowDb (l0, r0, sr, 0.1, 0.5);
    std::printf ("  Orbit Octo at +50 %% feedback, SpaceModulator level law: %+.2f dB\n", d);
    CHECK (d < -4.0 && d > -9.0);
}

// 4. ValhallaDelay → Echoform (valhallaLaw): round 190ly's probe renders at 80 … 140 % — the loop gain cut holds −1.1 dB
//    from 100 % to 120 % and eases to 0 by 160 %; up to 100 % the law is round 190ly's
TEST (parity190mb_echoform_valhalla_loop_law_over_feedback)
{
    using namespace lynx::echosuite::echoform;
    CHECK (std::abs (valhallaLoopDb (0.35f)) < 1e-5 && std::abs (valhallaLoopDb (0.5f) + 1.1) < 1e-5);
    CHECK (std::abs (valhallaLoopDb (0.425f) + 1.1 * 0.5) < 1e-6);             // 85 %: on round 190ly's line
    CHECK (std::abs (valhallaLoopDb (0.6f) + 1.1) < 1e-5 && std::abs (valhallaLoopDb (0.7f) + 0.5) < 1e-5); // (float knob values)
    CHECK (std::abs (valhallaLoopDb (0.8f)) < 1e-5 && std::abs (valhallaLoopDb (1.0f)) < 1e-9);
}

// 5. ValhallaFutureVerb → Horizon (hidden horizon::revision: new instances, translated states): the reference's tone and
//    level per reverb mode; a project saved before keeps the 0.1.27 law; Room and Chamber (the default) are untouched
TEST (parity190mb_horizon_revision_per_mode_trim)
{
    using namespace lynx;
    namespace hz = verbs::horizon;
    CHECK (hz::revision == hz::paramCount && effectInfo (EffectKind::horizon).params.size() == (std::size_t) hz::paramCount);
    const auto s = makeEffectSlot (EffectKind::horizon);
    CHECK (s.params[(std::size_t) hz::revision] == 1.0f && savedEffectParamCount (s) >= (std::size_t) hz::revision + 1); // (round 190mj: reaLaw follows)
    auto old = s;
    upgradeLoadedEffectParams (old, (std::size_t) hz::revision);
    CHECK (old.params[(std::size_t) hz::revision] == 0.0f);
    const double sr = 48000.0;
    const auto run = [sr] (int mode, float rev, std::vector<float>& l, std::vector<float>& r) {
        auto slot = makeEffectSlot (EffectKind::horizon);
        slot.params[0] = 1.0f; // Mix
        slot.params[22] = (float) mode / (float) (hz::reverbModeCount - 1); // Reverb mode
        slot.params[(std::size_t) hz::revision] = rev;
        slot.params[(std::size_t) hz::reaLaw] = 0.0f; // (round 190mj: revision 1 alone, as this round left it)
        const int n = (int) (1.5 * sr);
        l.assign ((std::size_t) n, 0.0f), r.assign ((std::size_t) n, 0.0f);
        l[(std::size_t) (0.05 * sr)] = r[(std::size_t) (0.05 * sr)] = 0.5f;
        parity190ly::render (slot, sr, l, r);
    };
    std::vector<float> l0, r0, l1, r1;
    for (const int mode : { 0, 1 }) // Room, Chamber: as before
    {
        run (mode, 0.0f, l0, r0);
        run (mode, 1.0f, l1, r1);
        CHECK (l0 == l1 && r0 == r1);
    }
    run (4, 0.0f, l0, r0); // Cathedral: the reference's sits ~3 dB lower
    run (4, 1.0f, l1, r1);
    const auto d = parity190ly::windowDb (l1, r1, sr, 0.0, 1.5) - parity190ly::windowDb (l0, r0, sr, 0.0, 1.5);
    std::printf ("  Horizon Cathedral, revision 1: %+.2f dB\n", d);
    CHECK (d < -1.0 && d > -6.0);
}
