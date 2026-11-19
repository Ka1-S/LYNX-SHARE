// Round 190mj (ValhallaFutureVerb → Horizon, part 2). Included after Parity190mbTests.inc (Parity190lyTests.inc's helpers).
// Hidden horizon::reaLaw (#26; new instances and translated states, read with the engine law and revision 1): the reference's
// echo modes (the Digital mode's slope-limited coder, four-head granular shifts, Analog / Reverse Up/Down / Lo-fi voicings)
// and Room / Chamber's law over Size. A project saved before keeps revision 1 as it was, bit for bit.
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Effects.h"
#include "core/LynxVerbs.h"
#include "core/PluginSubstitutes.h"

namespace parity190mjhz
{
// Horizon at 48 kHz: Mix 100 %, the given parameters, one block-wise render of `seconds` from a reset
inline void run (std::initializer_list<std::pair<int, float>> set, float rea, double seconds, std::vector<float>& l, std::vector<float>& r,
                 const std::vector<float>* input = nullptr)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::horizon);
    slot.params[0] = 1.0f;
    for (const auto& [i, v] : set)
        slot.params[(std::size_t) i] = v;
    slot.params[(std::size_t) verbs::horizon::reaLaw] = rea;
    const auto n = (std::size_t) (seconds * 48000.0);
    l.assign (n, 0.0f), r.assign (n, 0.0f);
    if (input != nullptr)
        for (std::size_t i = 0; i < n && i < input->size(); ++i)
            l[i] = r[i] = (*input)[i];
    else
        l[(std::size_t) (0.05 * 48000.0)] = r[(std::size_t) (0.05 * 48000.0)] = 0.5f;
    parity190ly::render (slot, 48000.0, l, r);
}
} // namespace parity190mjhz

TEST (parity190mj_horizon_rea_law_is_hidden_new_and_translated)
{
    using namespace lynx;
    namespace hz = verbs::horizon;
    CHECK (hz::reaLaw == hz::revision + 1 && effectInfo (EffectKind::horizon).params.size() == (std::size_t) hz::paramCount);
    const auto s = makeEffectSlot (EffectKind::horizon);
    CHECK (s.params[(std::size_t) hz::reaLaw] == 1.0f && savedEffectParamCount (s) == (std::size_t) hz::reaLaw + 1);
    auto reopened = s;
    upgradeLoadedEffectParams (reopened, savedEffectParamCount (s));
    CHECK (reopened.params[(std::size_t) hz::reaLaw] == 1.0f);
    auto old = s; // saved with revision 1, before this round
    upgradeLoadedEffectParams (old, (std::size_t) hz::reaLaw);
    CHECK (old.params[(std::size_t) hz::reaLaw] == 0.0f && old.params[(std::size_t) hz::revision] == 1.0f);
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "ValhallaFutureVerb";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->effect == EffectKind::horizon);
    if (entry != nullptr)
        CHECK (makeSubstituteSlot (*entry, &id).params[(std::size_t) hz::reaLaw] == 1.0f);
}

// Room / Chamber over Size: at Size 0 the first 100 ms come up (the reference's are 4 / 9 … 12 dB over LYNX's 0.1.30
// revision), Chamber's tail at Size 50 % goes down; Size 100 % Chamber stays where it was (its knot is the mode's own)
TEST (parity190mj_horizon_room_chamber_size_law)
{
    using namespace parity190mjhz;
    namespace hz = lynx::verbs::horizon;
    const auto win = [] (const std::vector<float>& l, const std::vector<float>& r, double a, double b) { return parity190ly::windowDb (l, r, 48000.0, a, b); };
    std::vector<float> l0, r0, l1, r1;
    const float decay3s = 0.2917f;
    // Room, Size 0
    run ({ { 22, 0.0f }, { 11, decay3s }, { 12, 0.0f } }, 0.0f, 2.5, l0, r0);
    run ({ { 22, 0.0f }, { 11, decay3s }, { 12, 0.0f } }, 1.0f, 2.5, l1, r1);
    const auto roomEarly = win (l1, r1, 0.05, 0.15) - win (l0, r0, 0.05, 0.15);
    std::printf ("  Room Size 0: first 100 ms %+.2f dB\n", roomEarly);
    CHECK (roomEarly > 3.0 && roomEarly < 12.0);
    // Chamber, Size 0 and 50 %
    run ({ { 22, 1.0f / 7.0f }, { 11, decay3s }, { 12, 0.0f } }, 0.0f, 2.5, l0, r0);
    run ({ { 22, 1.0f / 7.0f }, { 11, decay3s }, { 12, 0.0f } }, 1.0f, 2.5, l1, r1);
    const auto chEarly = win (l1, r1, 0.05, 0.15) - win (l0, r0, 0.05, 0.15);
    run ({ { 22, 1.0f / 7.0f }, { 11, decay3s }, { 12, 0.5f } }, 0.0f, 2.5, l0, r0);
    run ({ { 22, 1.0f / 7.0f }, { 11, decay3s }, { 12, 0.5f } }, 1.0f, 2.5, l1, r1);
    const auto chTail = win (l1, r1, 0.45, 2.0) - win (l0, r0, 0.45, 2.0);
    std::printf ("  Chamber Size 0: first 100 ms %+.2f dB; Size 50 %%: tail %+.2f dB\n", chEarly, chTail);
    CHECK (chEarly > 1.0 && chEarly < 6.0);
    CHECK (chTail < -0.3 && chTail > -3.0);
    run ({ { 22, 1.0f / 7.0f }, { 11, decay3s }, { 12, 1.0f } }, 0.0f, 2.5, l0, r0);
    run ({ { 22, 1.0f / 7.0f }, { 11, decay3s }, { 12, 1.0f } }, 1.0f, 2.5, l1, r1);
    CHECK (std::abs (win (l1, r1, 0.0, 2.5) - win (l0, r0, 0.0, 2.5)) < 0.05);
    // the other reverb modes do not read it
    run ({ { 22, 3.0f / 7.0f } }, 0.0f, 1.5, l0, r0);
    run ({ { 22, 3.0f / 7.0f } }, 1.0f, 1.5, l1, r1);
    CHECK (l0 == l1 && r0 == r1);
    (void) hz::reaLaw;
}

// the Digital echo alone (Reverb level 0, Echo level 100 %): an impulse at 0.5 leaves a small pulse (the reference: ~19 dB
// under the clean echo), a 1 kHz tone at −20 dB passes within a few dB; Reverse Up's four heads take ~3 dB off a click
TEST (parity190mj_horizon_echo_modes)
{
    using namespace parity190mjhz;
    const auto win = [] (const std::vector<float>& l, const std::vector<float>& r, double a, double b) { return parity190ly::windowDb (l, r, 48000.0, a, b); };
    std::vector<float> l0, r0, l1, r1;
    const float digital = 2.0f / 11.0f, revUp = 6.0f / 11.0f, clean = 0.0f;
    run ({ { 16, 0.0f }, { 9, 1.0f }, { 21, digital } }, 0.0f, 0.4, l0, r0);
    run ({ { 16, 0.0f }, { 9, 1.0f }, { 21, digital } }, 1.0f, 0.4, l1, r1);
    const auto imp = win (l1, r1, 0.0, 0.4) - win (l0, r0, 0.0, 0.4);
    std::vector<float> tone ((std::size_t) (0.5 * 48000.0));
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.1f * (float) std::sin (2.0 * 3.14159265358979323846 * 1000.0 * (double) i / 48000.0);
    run ({ { 16, 0.0f }, { 9, 1.0f }, { 21, digital } }, 0.0f, 0.5, l0, r0, &tone);
    run ({ { 16, 0.0f }, { 9, 1.0f }, { 21, digital } }, 1.0f, 0.5, l1, r1, &tone);
    const auto tn = win (l1, r1, 0.15, 0.45) - win (l0, r0, 0.15, 0.45);
    std::printf ("  Digital echo: impulse %+.2f dB, 1 kHz at -20 dB %+.2f dB\n", imp, tn);
    CHECK (imp < -10.0 && imp > -30.0);
    CHECK (tn < 0.0 && tn > -6.0);
    run ({ { 16, 0.0f }, { 9, 1.0f }, { 21, revUp } }, 0.0f, 0.6, l0, r0);
    run ({ { 16, 0.0f }, { 9, 1.0f }, { 21, revUp } }, 1.0f, 0.6, l1, r1);
    const auto heads = win (l1, r1, 0.0, 0.6) - win (l0, r0, 0.0, 0.6);
    std::printf ("  Reverse Up, four heads: impulse %+.2f dB\n", heads);
    CHECK (heads < -1.0 && heads > -6.0);
    run ({ { 16, 0.0f }, { 9, 1.0f }, { 21, clean } }, 0.0f, 0.4, l0, r0); // Clean: as it was
    run ({ { 16, 0.0f }, { 9, 1.0f }, { 21, clean } }, 1.0f, 0.4, l1, r1);
    CHECK (l0 == l1 && r0 == r1);
}
