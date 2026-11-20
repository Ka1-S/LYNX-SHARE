// Round 190ky: REA on the Waves DSP binaries, second pass (owner's permission of 2026-11-17; /mnt/e/lynx-work/waves190ky/
// HANDOFF.md, docs/analise/REA_rodada190ky.md).
// - Crest Compressor gets a FET law (hidden fetLaw): a feedback limiter with three parallel peak followers and a two-term
//   soft gain law, LYNX's own model of the structure REA showed in the Waves CLA-76 DSP, fitted on our demo-mode probes.
//   Off for new instances and every project saved before (bit for bit: lab/gold.sh).
// - Waves CLA-76 translates onto it (was Crest's Bus style around one factory state, rounds 127 / 148 / 190jh), with the
//   Input / Output knobs' measured gains.
#include <cmath>

#include "core/DynamicsSuite.h"
#include "core/Effects.h"

namespace parity190ky
{
namespace cr = lynx::dyn::crest;
inline lynx::EffectSlot fet (int voice, double thrDb, double ratio, double kneeDb, double attMs, double relMs)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::crestCompressor);
    s.params[(std::size_t) cr::style] = cr::styleValue (cr::Style::bus);
    s.params[(std::size_t) cr::fetLaw] = cr::fetLawValue (voice);
    s.params[(std::size_t) cr::threshold] = (float) ((thrDb + 60.0) / 60.0);
    s.params[(std::size_t) cr::ratio] = cr::ratioValue (ratio);
    s.params[(std::size_t) cr::knee] = (float) (kneeDb / 72.0);
    s.params[(std::size_t) cr::range] = (float) (40.0 / 60.0);
    s.params[(std::size_t) cr::attack] = cr::attackValue (attMs);
    s.params[(std::size_t) cr::release] = cr::releaseValue (relMs);
    s.params[(std::size_t) cr::autoRelease] = 0.0f;
    s.params[(std::size_t) cr::autoGain] = 0.0f;
    s.params[(std::size_t) cr::stereoLink] = 1.0f;
    s.params[(std::size_t) cr::mix] = 1.0f;
    s.params[(std::size_t) cr::output] = 0.5f;
    return s;
}
} // namespace parity190ky

TEST (parity190ky_crest_fet_law_is_hidden_and_off_for_new_and_old_crests)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    const auto& info = effectInfo (EffectKind::crestCompressor);
    CHECK (info.params.size() == (std::size_t) cr::count && cr::fetLaw == cr::count + 1); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::crestCompressor);
    s.params[(std::size_t) cr::revision] = 0.0f; // (rollup step 8: round 190kw's revision, set on a new Crest, comes after)
    CHECK (s.params[(std::size_t) cr::fetLaw] == 0.0f && savedEffectParamCount (s) <= (std::size_t) cr::count);
    s.params[(std::size_t) cr::fetLaw] = cr::fetLawValue (3);
    CHECK (savedEffectParamCount (s) == (std::size_t) cr::fetLaw + 1);
    // the voicing values round-trip through a 4-decimal save
    for (int v = 0; v < cr::fetVoices; ++v)
        CHECK (std::lround (savedEffectParamValue (cr::fetLawValue (v)) * 16.0) == v + 1);
    // off: the static curve is the style's own (the FET law's fixed point only answers with the law on)
    CHECK (cr::fetSteadyDb (makeEffectSlot (EffectKind::crestCompressor).params, 0.0) == 0.0);
}

TEST (parity190ky_fet_law_is_a_feedback_curve)
{
    namespace cr = lynx::dyn::crest;
    // nothing far below the knee; a hard-ish knee with slope S settles at ratio 1 + S well over it
    const auto s = parity190ky::fet (0, -30.0, 11.0, 0.5, 0.05, 60.0);
    CHECK (cr::fetSteadyDb (s.params, -60.0) > -0.2);
    const auto g1 = cr::fetSteadyDb (s.params, -20.0), g2 = cr::fetSteadyDb (s.params, -10.0);
    CHECK (g1 < -5.0 && std::abs ((g2 + 10.0) - (g1 + 0.0) - 10.0 / 11.0) < 0.5);
    // monotonic in the input level; the editor's curve is the same fixed point
    double last = -1e9;
    for (int db = -60; db <= 0; db += 2)
    {
        const auto out = db + cr::fetSteadyDb (s.params, db);
        CHECK (out >= last - 1e-6);
        CHECK (std::abs (cr::staticGainDb (s.params, db) - cr::fetSteadyDb (s.params, db)) < 1e-9);
        last = out;
    }
}

TEST (parity190ky_crest_fet_law_renders_its_curve_and_timing)
{
    const double sr = 48000.0;
    const int n = (int) (3.0 * sr);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    // the steady gain matches the law's own fixed point (feedback detector, followers at the tone's peak)
    parity190kt::tone (l, r, sr, { { 0.0, -6.0 } });
    auto s = parity190ky::fet (3, -24.0, 7.0, 6.0, 0.05, 60.0);
    parity190kt::render (s, sr, l, r);
    const auto want = lynx::dyn::crest::fetSteadyDb (s.params, -6.0);
    CHECK (want < -8.0 && std::abs (parity190kt::gainDb (l, sr, 1.5, 2.9, -6.0) - want) < 0.6);
    // release: after the level drops the reduction recovers, slower with the longer knob
    const auto recovered = [&] (double relMs) {
        parity190kt::tone (l, r, sr, { { 0.0, -6.0 }, { 1.5, -40.0 } });
        auto t = parity190ky::fet (3, -24.0, 7.0, 6.0, 0.05, relMs);
        parity190kt::render (t, sr, l, r);
        return parity190kt::gainDb (l, sr, 1.75, 1.78, -40.0);
    };
    const auto fast = recovered (60.0), slow = recovered (600.0);
    CHECK (fast > slow + 1.0 && fast <= 0.1);
    // attack: a slower attack lets more of a step's onset through
    const auto onset = [&] (double attMs) {
        parity190kt::tone (l, r, sr, { { 0.0, -40.0 }, { 1.0, -6.0 } });
        auto t = parity190ky::fet (3, -24.0, 7.0, 6.0, attMs, 60.0);
        parity190kt::render (t, sr, l, r);
        return parity190kt::gainDb (l, sr, 1.0005, 1.0025, -6.0);
    };
    CHECK (onset (0.8) > onset (0.02) + 0.5);
}

TEST (parity190ky_waves_cla76_translates_onto_crest_fet_law)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    int n = 0;
    // refcap182's X3: Input -12, Output -30, attack 7, release 7, Blacky, all buttons, mix 40 %
    auto x3 = parity190jh::waves ("CLA-76 Stereo", "CLA-76",
                                  parity190jh::list (106, { { 0, "12" }, { 1, "30" }, { 2, "7" }, { 3, "7" }, { 56, "1" }, { 59, "4" }, { 103, "40" } }), n);
    CHECK (x3.kind == EffectKind::crestCompressor && n == 13);
    CHECK (x3.params[(std::size_t) cr::fetLaw] == cr::fetLawValue (4) && x3.params[(std::size_t) cr::busLaw] == 0.0f);
    // the mix blends inside Crest (the dry path is the plain input), the slot stays at 100 %
    CHECK (x3.mix == 1.0f && x3.params[(std::size_t) cr::mix] > 0.4f && x3.params[(std::size_t) cr::mix] < 1.0f);
    CHECK (x3.params[(std::size_t) cr::autoGain] == 0.0f && std::abs (cr::trimDb (x3.params[(std::size_t) cr::input])) < 0.01);
    // the Input knob's measured gain lowers the detector's threshold (−12 dB on its scale: +12.2 dB from the −24 dB
    // mark) against the same setting at −24
    auto x3at24 = parity190jh::waves ("CLA-76 Stereo", "CLA-76",
                                      parity190jh::list (106, { { 0, "24" }, { 1, "30" }, { 2, "7" }, { 3, "7" }, { 56, "1" }, { 59, "4" }, { 103, "40" } }), n);
    CHECK (std::abs (cr::thresholdDb (x3at24.params[(std::size_t) cr::threshold]) - cr::thresholdDb (x3.params[(std::size_t) cr::threshold]) - 12.21) < 0.05);
    // Bluey at 12:1, and the attack / release knobs: 1 is the slowest
    auto x4 = parity190jh::waves ("CLA-76 Stereo", "CLA-76",
                                  parity190jh::list (106, { { 0, "24" }, { 1, "24" }, { 2, "1" }, { 3, "1" }, { 56, "0" }, { 59, "1" }, { 103, "100" } }), n);
    CHECK (x4.params[(std::size_t) cr::fetLaw] == cr::fetLawValue (6));
    CHECK (cr::attackMs (x4.params[(std::size_t) cr::attack]) > cr::attackMs (x3.params[(std::size_t) cr::attack]));
    CHECK (cr::releaseMs (x4.params[(std::size_t) cr::release]) > cr::releaseMs (x3.params[(std::size_t) cr::release]));
    // the Output knob is not dB-linear: −24 is 9.7 dB over −30 on the reference (measured), not 6
    auto o24 = parity190jh::waves ("CLA-76 Stereo", "CLA-76", parity190jh::list (106, { { 0, "24" }, { 1, "24" }, { 56, "1" }, { 59, "3" }, { 103, "100" } }), n);
    auto o30 = parity190jh::waves ("CLA-76 Stereo", "CLA-76", parity190jh::list (106, { { 0, "24" }, { 1, "30" }, { 56, "1" }, { 59, "3" }, { 103, "100" } }), n);
    CHECK (std::abs (cr::trimDb (o24.params[(std::size_t) cr::output]) - cr::trimDb (o30.params[(std::size_t) cr::output]) - 9.66) < 0.05);
    // Comp off: no reduction
    auto off = parity190jh::waves ("CLA-76 Stereo", "CLA-76", parity190jh::list (106, { { 0, "24" }, { 1, "24" }, { 4, "1" }, { 56, "1" }, { 59, "3" } }), n);
    CHECK (cr::staticGainDb (off.params, 0.0) > -0.02);
}
