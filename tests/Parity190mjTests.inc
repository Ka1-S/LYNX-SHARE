// Round 190mj (Valhalla → LYNX stand-ins, part 4). Included after Parity190mbTests.inc (uses Parity190lyTests.inc's helpers).
// ValhallaSpaceModulator → Orbit: the reference's own voices (hidden orbit::voiceLaw, set only by the SpaceModulator
// translator), measured black-box on click-train and still probes: one sweep phase that runs on from load (a reset does
// not restart it) at 10 |2 v − 1|^(10/3) Hz, first-order all-pass (Thiran) fractional delays, the through-zero pair
// (5 … 25 ms at Depth 10 ms, both at 17.5 ms at load), Ocho's four-tap barber pole at 1/√2; the mode by the reference's
// own value law floor (11 v) − 1. Modes whose new voices did not gate (Up, Up / Down, Tri, Doubler, Vari Up) keep the
// round-190mb voices under the law.
#include <cmath>
#include <vector>

#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/PluginSubstitutes.h"

namespace parity190mj
{
inline lynx::EffectSlot orbitSlot (lynx::echosuite::orbit::Mode m, float rate, float fb, float law)
{
    using namespace lynx;
    namespace ob = lynx::echosuite::orbit;
    auto slot = makeEffectSlot (EffectKind::orbit);
    slot.params[(std::size_t) ob::mix] = 1.0f;
    slot.params[(std::size_t) ob::rate] = rate;
    slot.params[(std::size_t) ob::depth] = 0.5f; // 10 ms
    slot.params[(std::size_t) ob::feedback] = fb;
    slot.params[(std::size_t) ob::manual] = 0.5f;
    slot.params[(std::size_t) ob::mode] = ob::modeValue (m);
    slot.params[(std::size_t) ob::law] = 1.0f;
    slot.params[(std::size_t) ob::smLaw] = law;
    slot.params[(std::size_t) ob::voiceLaw] = law;
    return slot;
}
// the impulse response (both sides) of a slot to a unit impulse on the left (`right`: on the right) at 0.1 s
inline void impulse (const lynx::EffectSlot& slot, std::vector<float>& l, std::vector<float>& r, bool right = false, double secs = 0.4)
{
    const double sr = 48000.0;
    l.assign ((std::size_t) (secs * sr), 0.0f);
    r.assign (l.size(), 0.0f);
    (right ? r : l)[(std::size_t) (0.1 * sr)] = 1.0f;
    parity190ly::render (slot, sr, l, r);
}
inline double energy (const std::vector<float>& x) { double e = 0.0; for (auto v : x) e += (double) v * v; return e; }
} // namespace parity190mj

TEST (parity190mj_orbit_voice_law_is_hidden_and_translator_only)
{
    using namespace lynx;
    namespace ob = lynx::echosuite::orbit;
    CHECK ((int) ob::voiceLaw == (int) ob::smLaw + 1 && effectInfo (EffectKind::orbit).params.size() == (std::size_t) ob::paramCount);
    CHECK (makeEffectSlot (EffectKind::orbit).params[(std::size_t) ob::voiceLaw] == 0.0f);
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "ValhallaSpaceModulator";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    const auto sm = makeSubstituteSlot (*entry, &id);
    CHECK (sm.params[(std::size_t) ob::voiceLaw] == 1.0f && sm.params[(std::size_t) ob::smLaw] == 1.0f);
    CHECK (savedEffectParamCount (sm) == (std::size_t) ob::voiceLaw + 1);
    auto old = sm; // a SpaceModulator translated by round 190mb's build (saved up to smLaw) keeps its voices
    upgradeLoadedEffectParams (old, (std::size_t) ob::voiceLaw);
    CHECK (old.params[(std::size_t) ob::voiceLaw] == 0.0f && old.params[(std::size_t) ob::smLaw] == 1.0f);
}

// still voices (Rate 0): exact taps through the all-pass interpolator, the through-zero pair together at load
TEST (parity190mj_orbit_voice_law_still_taps)
{
    using namespace parity190mj;
    namespace ob = lynx::echosuite::orbit;
    std::vector<float> l, r;
    // Through-zero +: both taps at 1.75 × Depth (17.5 ms + 0.42 samples), 1/√2 each: one tap of gain √2 there
    impulse (orbitSlot (ob::Mode::throughZeroPlus, 0.5f, 0.5f, 1.0f), l, r);
    double sum = 0.0, moment = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
        sum += l[i], moment += (double) i * l[i];
    const double at = moment / sum - 0.1 * 48000.0, want = 0.42 + 1.75 * ob::depthMs (0.5f) * 0.99954 * 48.0;
    std::printf ("  Orbit TZ+ still: gain %.4f at %.3f samples (want %.3f)\n", sum, at, want);
    CHECK (std::abs (sum - std::sqrt (2.0)) < 1e-3 && std::abs (at - want) < 0.05);
    CHECK (energy (r) < 1e-12);
    // Through-zero −: the two taps cancel
    impulse (orbitSlot (ob::Mode::throughZeroMinus, 0.5f, 0.5f, 1.0f), l, r);
    CHECK (energy (l) < 1e-9);
    // Ocho: four taps a quarter cycle apart, 1/√2 each with the sin² window: the gains sum to √2
    impulse (orbitSlot (ob::Mode::octo, 0.5f, 0.5f, 1.0f), l, r);
    sum = 0.0;
    for (auto v : l)
        sum += v;
    CHECK (std::abs (sum - std::sqrt (2.0)) < 1e-3);
}

// the sweep runs on across a reset (the reference's does), at 10 |2 v − 1|^(10/3) Hz; the masked modes keep round 190mb's voices
TEST (parity190mj_orbit_voice_law_sweep_and_masked_modes)
{
    using namespace lynx;
    using namespace parity190mj;
    namespace ob = lynx::echosuite::orbit;
    const double sr = 48000.0;
    auto slot = orbitSlot (ob::Mode::octo, 0.75f, 0.5f, 1.0f);
    auto fx = makeEffect (slot);
    fx->prepare (sr);
    std::vector<float> a ((std::size_t) (0.3 * sr), 0.0f), b (a.size(), 0.0f), z (a.size(), 0.0f), z2 (a.size(), 0.0f);
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = b[i] = (float) std::sin (0.05 * (double) i);
    auto a2 = a, b2 = b;
    for (std::size_t off = 0; off < a.size(); off += 256)
        fx->process (a.data() + off, z.data() + off, (int) std::min<std::size_t> (256, a.size() - off), slot.params, 120.0);
    fx->reset();
    for (std::size_t off = 0; off < b.size(); off += 256)
        fx->process (b.data() + off, z2.data() + off, (int) std::min<std::size_t> (256, b.size() - off), slot.params, 120.0);
    CHECK (a != b); // the second pass reads other tap positions: the phase went on
    // the rate law: 0.75 → 0.992 Hz (the reference's text reads 0.99 Hz), 0.626 → 0.1011 Hz
    CHECK (std::abs (10.0 * std::pow (0.5, 10.0 / 3.0) - 0.99213) < 1e-4 && std::abs (10.0 * std::pow (0.252, 10.0 / 3.0) - 0.10106) < 1e-4);
    // masked modes: the law renders exactly as round 190mb's level law alone
    for (auto m : { ob::Mode::rise, ob::Mode::riseFall, ob::Mode::tri, ob::Mode::doubler, ob::Mode::driftRise })
    {
        auto withLaw = orbitSlot (m, 0.626f, 0.75f, 1.0f);
        auto without = withLaw;
        without.params[(std::size_t) ob::voiceLaw] = 0.0f;
        std::vector<float> l1 (a2.size()), r1 (a2.size()), l0, r0;
        for (std::size_t i = 0; i < l1.size(); ++i)
            l1[i] = r1[i] = 0.3f * (float) std::sin (0.031 * (double) i);
        l0 = l1, r0 = r1;
        parity190ly::render (withLaw, sr, l1, r1);
        parity190ly::render (without, sr, l0, r0);
        CHECK (l1 == l0 && r1 == r0);
    }
}
