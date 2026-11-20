// Round 190kt: REA on third-party binaries (owner's permission of 2026-11-17; /mnt/e/lynx-work/rea190kt/HANDOFF.md,
// docs/analise/REA_rodada190kt.md).
// - Crest Compressor's Bus style gets a console law (hidden busLaw): a feedback compressor with a rational soft knee and
//   RC ballistics, LYNX's own model of the structure REA showed in the Waves SSLComp DSP, fitted on our demo-mode probes.
//   Off for new instances and every project saved before (bit for bit: lab/gold.sh).
// - Waves SSLComp translates onto it (was Compressor's classic voicing, round 190jh).
#include <cmath>
#include <cstring>

#include "core/DynamicsSuite.h"
#include "core/Effects.h"

namespace parity190kt
{
namespace cr = lynx::dyn::crest;
inline lynx::EffectSlot console (double thrDb, double ratio, double kneeDb, double attMs, double relMs, bool autoRel)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::crestCompressor);
    s.params[(std::size_t) cr::style] = cr::styleValue (cr::Style::bus);
    s.params[(std::size_t) cr::busLaw] = 1.0f;
    s.params[(std::size_t) cr::threshold] = (float) ((thrDb + 60.0) / 60.0);
    s.params[(std::size_t) cr::ratio] = cr::ratioValue (ratio);
    s.params[(std::size_t) cr::knee] = (float) (kneeDb / 72.0);
    s.params[(std::size_t) cr::range] = 1.0f;
    s.params[(std::size_t) cr::attack] = cr::attackValue (attMs);
    s.params[(std::size_t) cr::release] = cr::releaseValue (relMs);
    s.params[(std::size_t) cr::autoRelease] = autoRel ? 1.0f : 0.0f;
    s.params[(std::size_t) cr::autoGain] = 0.0f;
    s.params[(std::size_t) cr::stereoLink] = 1.0f;
    s.params[(std::size_t) cr::mix] = 1.0f;
    s.params[(std::size_t) cr::output] = 0.5f;
    return s;
}
// a 1 kHz tone whose level (dBFS peak) changes at the given times (s)
inline void tone (std::vector<float>& l, std::vector<float>& r, double sr, std::initializer_list<std::pair<double, double>> steps)
{
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        const double t = (double) i / sr;
        double db = -120.0;
        for (const auto& [at, lv] : steps)
            if (t >= at) db = lv;
        l[i] = r[i] = (float) (std::pow (10.0, db / 20.0) * std::sin (2.0 * M_PI * 1000.0 * t));
    }
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
}
// the gain (dB) of out against a tone of known peak over [a, b) s
inline double gainDb (const std::vector<float>& out, double sr, double a, double b, double peakDb)
{
    double e = 0.0;
    const auto i0 = (std::size_t) (a * sr), i1 = (std::size_t) (b * sr);
    for (auto i = i0; i < i1; ++i) e += (double) out[i] * out[i];
    return 10.0 * std::log10 (e / (double) (i1 - i0) + 1e-30) - (peakDb - 3.0103);
}
} // namespace parity190kt

TEST (parity190kt_crest_console_law_is_hidden_and_off_for_new_and_old_crests)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    const auto& info = effectInfo (EffectKind::crestCompressor);
    CHECK (info.params.size() == (std::size_t) cr::count && cr::busLaw == cr::count); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::crestCompressor);
    // (rollup step 8: a new Crest has round 190kw's revision 1, the last hidden parameter, so it saves up to there)
    CHECK (s.params[(std::size_t) cr::busLaw] == 0.0f && s.params[(std::size_t) cr::fetLaw] == 0.0f
           && savedEffectParamCount (s) == (std::size_t) cr::revision + 1);
    s.params[(std::size_t) cr::revision] = 0.0f; // (as every Crest saved before round 190kw)
    CHECK (savedEffectParamCount (s) <= (std::size_t) cr::count);
    s.params[(std::size_t) cr::busLaw] = 1.0f;
    CHECK (savedEffectParamCount (s) == (std::size_t) cr::busLaw + 1);
}

TEST (parity190kt_console_law_is_a_feedback_curve_with_a_soft_knee)
{
    namespace cr = lynx::dyn::crest;
    // a hard-ish knee settles at ratio 1 + loop gain: 4:1 → 10 dB more in = 2.5 dB more out, well over the threshold
    const auto g1 = cr::consoleSteadyDb (-40.0, 4.0, 0.5, -10.0), g2 = cr::consoleSteadyDb (-40.0, 4.0, 0.5, 0.0);
    CHECK (std::abs ((g2 + 10.0) - (g1 + 0.0) - 2.5) < 0.4);
    CHECK (std::abs (cr::consoleSteadyDb (-20.0, 4.0, 0.5, -40.0)) < 1e-6); // under the threshold: nothing
    // the knee starts at the threshold (nothing under it, whatever its width) and softens the curve over it: a wide knee
    // reduces less just over the threshold and approaches the same slope higher up
    CHECK (std::abs (cr::consoleSteadyDb (-20.0, 4.0, 60.0, -24.0)) < 1e-6);
    CHECK (cr::consoleSteadyDb (-20.0, 4.0, 60.0, -14.0) > cr::consoleSteadyDb (-20.0, 4.0, 0.5, -14.0) + 1.0);
    CHECK (cr::consoleSteadyDb (-20.0, 4.0, 60.0, 0.0) > cr::consoleSteadyDb (-20.0, 4.0, 0.5, 0.0));
    // monotonic in the input level: the output never falls as the input rises
    double last = -1e9;
    for (int db = -60; db <= 0; db += 2)
    {
        const auto out = db + cr::consoleSteadyDb (-30.0, 3.0, 20.0, db);
        CHECK (out >= last - 1e-6);
        last = out;
    }
}

TEST (parity190kt_crest_console_law_renders_its_curve_and_rc_timing)
{
    const double sr = 48000.0;
    const int n = (int) (2.0 * sr);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    // the steady gain matches the law's own fixed point (feedback detector on sample peaks)
    parity190kt::tone (l, r, sr, { { 0.0, -6.0 } });
    auto s = parity190kt::console (-30.0, 4.0, 10.0, 0.05, 85.8, false);
    parity190kt::render (s, sr, l, r);
    const auto want = lynx::dyn::crest::consoleSteadyDb (-30.0, 4.0, 10.0, -6.0);
    CHECK (want < -10.0 && std::abs (parity190kt::gainDb (l, sr, 1.0, 1.9, -6.0) - want) < 0.5);
    // release: after the level drops, the reduction decays towards 0 dB with the release resistor's τ (85.8 ms):
    // about e^-1 of it is left after one τ
    parity190kt::tone (l, r, sr, { { 0.0, -6.0 }, { 1.0, -40.0 } });
    s = parity190kt::console (-30.0, 4.0, 10.0, 0.05, 85.8, false);
    parity190kt::render (s, sr, l, r);
    const auto at0 = parity190kt::gainDb (l, sr, 1.001, 1.004, -40.0), atTau = parity190kt::gainDb (l, sr, 1.0848, 1.0878, -40.0);
    CHECK (at0 < -10.0 && atTau / at0 > 0.30 && atTau / at0 < 0.45);
    // Auto: a long reduction recovers more slowly than a short one would (the slow capacitor)
    parity190kt::tone (l, r, sr, { { 0.0, -6.0 }, { 1.0, -40.0 } });
    auto a = parity190kt::console (-30.0, 4.0, 10.0, 0.05, 26.66, true);
    parity190kt::render (a, sr, l, r);
    CHECK (parity190kt::gainDb (l, sr, 1.30, 1.33, -40.0) < -0.5); // the fast capacitor alone would be back by now
}

TEST (parity190kt_waves_sslcomp_translates_onto_crest_console_law)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    int n = 0;
    // threshold +4, 2:1, attack 30 ms, release Auto, make-up 2 dB, IN on, mix 100 %, trim 0, SC high-pass 100 Hz
    auto sc = parity190jh::waves ("SSLComp Stereo", "SSLComp",
                                  parity190jh::list (22, { { 0, "4" }, { 1, "0" }, { 3, "5" }, { 4, "4" }, { 5, "2" }, { 8, "1" }, { 19, "100" }, { 21, "100" } }), n);
    CHECK (sc.kind == EffectKind::crestCompressor && n == 22);
    CHECK (sc.params[(std::size_t) cr::busLaw] == 1.0f && cr::styleOf (sc.params) == cr::Style::bus);
    CHECK (sc.params[(std::size_t) cr::autoRelease] == 1.0f && sc.params[(std::size_t) cr::autoGain] == 0.0f);
    CHECK (std::abs (cr::attackMs (sc.params[(std::size_t) cr::attack]) - 111.0) < 0.5);
    CHECK (std::abs (cr::trimDb (sc.params[(std::size_t) cr::output]) - 2.0) < 0.01);
    CHECK (cr::ratioOf (sc.params[(std::size_t) cr::ratio]) > 2.0 && cr::ratioOf (sc.params[(std::size_t) cr::ratio]) < 5.0);
    CHECK (sc.params[(std::size_t) cr::scParam (0, 0)] == facet::shapeValue (facet::Shape::lowCut));
    // IN off: the slope of 1:1 (nothing reduced)
    auto off = parity190jh::waves ("SSLComp Stereo", "SSLComp", parity190jh::list (22, { { 0, "-15" }, { 1, "2" }, { 8, "0" }, { 19, "100" } }), n);
    CHECK (off.kind == EffectKind::crestCompressor && cr::ratioOf (off.params[(std::size_t) cr::ratio]) < 1.001);
    // the steadier ratios order as the reference's: 2:1 < 4:1 < 10:1 at the same knob
    double last = 1.0;
    for (const char* r : { "0", "1", "2" })
    {
        auto t = parity190jh::waves ("SSLComp Stereo", "SSLComp", parity190jh::list (22, { { 0, "-5" }, { 1, r }, { 8, "1" }, { 19, "100" } }), n);
        const auto ratio = cr::ratioOf (t.params[(std::size_t) cr::ratio]);
        CHECK (ratio > last);
        last = ratio;
    }
}

