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
