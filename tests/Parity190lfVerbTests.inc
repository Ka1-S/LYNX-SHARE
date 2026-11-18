// Round 190lf, sub-round "verb": the Waves H-Delay loop and TrueVerb's direct sound (owner's permission of 2026-11-17 for
// REA on third-party binaries; /mnt/e/lynx-work/waves2_190lf/verb/HANDOFF.md, docs/analise/REA_rodada190lf_verb.md).
// - Echoform's hybrid engine gets a hidden law (echoform::hybridLaw): H-Delay's HiPass / LoPass / LoFi inside the loop and
//   its smoothed-triangle delay modulation, LYNX's own code fitted on demo-mode probes. 0 for new instances and every
//   project saved before (bit for bit: lab/gold.sh).
// - Reeverb 2 gets a hidden direct-path law (reeverb2DirectLaw): TrueVerb's direct sound, a first-order low shelf.
#include <cmath>

#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/Reeverb2.h"

namespace parity190lfv
{
// one channel's impulse response of a slot (left impulse of `amp` at sample 0)
inline std::vector<float> impulse (const lynx::EffectSlot& s, double sr, int n, float amp, std::vector<float>* right = nullptr)
{
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    l[0] = amp;
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
    if (right != nullptr) *right = r;
    return l;
}
inline lynx::EffectSlot hybridLaw1 (double ms, double fb)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    auto s = makeEffectSlot (EffectKind::echoform);
    s.params[e::hybrid] = e::hybridValue (1);
    s.params[e::hybridLaw] = 1.0f;
    s.params[e::style] = e::styleValue (e::Style::single);
    s.params[e::syncL] = s.params[e::syncR] = 0.0f;
    s.params[e::timeL] = s.params[e::timeR] = e::timeValue (ms);
    s.params[e::feedback] = (float) (fb / 2.0);
    s.params[e::mix] = 1.0f;
    s.params[e::output] = 0.5f;
    return s;
}
inline double bandDb (const std::vector<float>& x, int from, int len, double hz, double sr)
{
    double re = 0.0, im = 0.0;
    for (int k = 0; k < len; ++k)
    {
        const double w = 2.0 * M_PI * hz * k / sr;
        re += x[(std::size_t) (from + k)] * std::cos (w);
        im -= x[(std::size_t) (from + k)] * std::sin (w);
    }
    return 10.0 * std::log10 (re * re + im * im + 1e-30);
}
} // namespace parity190lfv

TEST (parity190lf_hybrid_law_is_hidden_and_off_for_new_and_old_echoforms)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const auto& info = effectInfo (EffectKind::echoform);
    CHECK (info.params.size() == (std::size_t) e::paramCount && e::hybridLaw == e::lineLaw + 1 && e::hybridRate == e::lineLaw + 5);
    auto s = makeEffectSlot (EffectKind::echoform);
    for (int k = e::hybridLaw; k <= e::hybridRate; ++k)
        CHECK (s.params[(std::size_t) k] == 0.0f);
    s.params[e::hybridLaw] = 1.0f;
    CHECK (savedEffectParamCount (s) == (std::size_t) e::hybridLaw + 1);
    CHECK (std::abs (e::hybridFilterHz (e::hybridFilterValue (203.0)) - 203.0) < 0.01);
    CHECK (std::abs (e::hybridFilterHz (e::hybridFilterValue (7860.0)) - 7860.0) < 0.5);
}

TEST (parity190lf_hybrid_law_filters_sit_inside_the_loop)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const double sr = 48000.0;
    const int t = 18000; // 375 ms
    // HiPass 203 Hz, feedback 30 %: each pass filters again (the measured −24.3 dB at 50 Hz per pass, 12 dB / octave)
    auto s = parity190lfv::hybridLaw1 (375.0, 0.3);
    s.params[e::hybridHp] = e::hybridFilterValue (203.0);
    const auto h = parity190lfv::impulse (s, sr, 3 * t + 8192, 0.02f);
    const auto e1 = parity190lfv::bandDb (h, t - 1, 8192, 50.0, sr) - parity190lfv::bandDb (h, t - 1, 8192, 2000.0, sr);
    const auto e2 = parity190lfv::bandDb (h, 2 * t - 1, 8192, 50.0, sr) - parity190lfv::bandDb (h, 2 * t - 1, 8192, 2000.0, sr);
    std::printf ("  hybrid law: HiPass 203 Hz at 50 Hz: echo 1 %.1f dB, echo 2 %.1f dB (re 2 kHz)\n", e1, e2);
    CHECK (e1 < -22.0 && e1 > -27.0);
    CHECK (e2 < e1 - 20.0);
    // LoFi: the echo lands on T (the low pass's own delay is taken off the echo time) and 10 kHz is gone
    auto lo = parity190lfv::hybridLaw1 (375.0, 0.0);
    lo.params[e::lofi] = 1.0f;
    const auto g = parity190lfv::impulse (lo, sr, t + 8192, 0.02f);
    int peak = 0;
    for (int i = 0; i < (int) g.size(); ++i)
        if (std::abs (g[(std::size_t) i]) > std::abs (g[(std::size_t) peak])) peak = i;
    CHECK (std::abs (peak - t) <= 1);
    CHECK (parity190lfv::bandDb (g, t - 100, 4096, 10000.0, sr) - parity190lfv::bandDb (g, t - 100, 4096, 2000.0, sr) < -45.0);
    CHECK (std::abs (parity190lfv::bandDb (g, t - 100, 4096, 3000.0, sr) - parity190lfv::bandDb (g, t - 100, 4096, 1000.0, sr)) < 0.3);
}

TEST (parity190lf_hybrid_law_modulation_swings_the_delay_from_the_instance_start)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const double sr = 48000.0;
    // depth 50 %, LFO 0.2 Hz (H-Delay's Rate 0.1 with its BPM sync): the delay starts at T and rises; ±T/4 before the
    // smoothing; an impulse sent 1.25 s in (the triangle's top) comes back ≈ 80 ms late
    auto s = parity190lfv::hybridLaw1 (375.0, 0.0);
    s.params[e::hybridDepth] = 0.5f;
    s.params[e::hybridRate] = 0.2f / 20.0f;
    std::vector<float> l ((std::size_t) (2.0 * sr), 0.0f), r = l;
    const int at = (int) (1.25 * sr);
    l[(std::size_t) at] = 0.05f;
    auto fx = makeEffect (s);
    fx->prepare (sr);
    for (int off = 0; off < (int) l.size(); off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, (int) l.size() - off), s.params, 120.0);
    int peak = at;
    for (int i = at; i < (int) l.size(); ++i)
        if (std::abs (l[(std::size_t) i]) > std::abs (l[(std::size_t) peak])) peak = i;
    const double lateMs = ((peak - at) / sr - 0.375) * 1000.0;
    std::printf ("  hybrid law: the echo of an impulse 1.25 s in is %.1f ms late\n", lateMs);
    CHECK (lateMs > 55.0 && lateMs < 95.0);
}

TEST (parity190lf_waves_hdelay_filters_lofi_and_modulation_use_the_hybrid_law)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 17.1_x64";
    int n = 0;
    // the bank's S2: own-BPM sync, 1/8 dotted, feedback 60, depth 50 %, rate 0.1, LoFi on, Analog Mod2
    id.state = substtest::wavesComponent ("H-Delay", "* 0 120 375 11 * 100 60 0 0 0 * * * 0 20100 * 19 * 50 0.1 1 * 2");
    const auto s2 = makeSubstituteSlot (*findPluginSubstitute (id, false), &id, &n);
    CHECK (n == 10 && e::hybridOf (s2.params[e::hybrid]) == 2 && s2.params[e::hybridLaw] == 1.0f);
    CHECK (s2.params[e::lofi] == 1.0f && std::abs (s2.params[e::hybridDepth] - 0.5f) < 1e-6f);
    CHECK (std::abs (s2.params[e::hybridRate] * 20.0f - 0.2f) < 1e-5f && s2.params[e::hybridHp] == 0.0f && s2.params[e::hybridLp] == 0.0f);
    // the bank's X2: host sync 1/8, ping-pong, feedback 35, HiPass 203 Hz
    id.state = substtest::wavesComponent ("H-Delay", "* 1 120 250 10 * 100 35 0 0 1 * * * 0 20100 * 203 * 0 0.1 0 * 2");
    const auto x2 = makeSubstituteSlot (*findPluginSubstitute (id, false), &id, &n);
    CHECK (x2.params[e::hybridLaw] == 1.0f && std::abs (e::hybridFilterHz (x2.params[e::hybridHp]) - 203.0) < 0.1);
    CHECK (e::styleOf (x2.params[e::style]) == e::Style::pingPong && x2.params[e::hybridDepth] == 0.0f);
    // no filter, LoFi or modulation: the round-124 loop as before (law 0)
    id.state = substtest::wavesComponent ("H-Delay", "* 1 120 1000 16 * 100 60 0 0 1 * * * 0 20100 * 19 * 0 0.1 0 * 2");
    const auto d = makeSubstituteSlot (*findPluginSubstitute (id, false), &id, &n);
    CHECK (n == 9 && d.params[e::hybridLaw] == 0.0f);
}

TEST (parity190lf_reeverb2_direct_law_is_hidden_and_exact)
{
    using namespace lynx;
    const auto& info = effectInfo (EffectKind::reeverb2);
    CHECK (info.params.size() == (std::size_t) reeverb2ParamCount && reeverb2DirectLaw == reeverb2ParamCount);
    auto s = makeEffectSlot (EffectKind::reeverb2);
    CHECK (s.params[(std::size_t) reeverb2DirectLaw] == 0.0f && savedEffectParamCount (s) <= (std::size_t) reeverb2ParamCount);
    // engine dry / early / wet at 0: the output is the direct law alone, y = a·x − b·Σ ρ^(k−1) x[n−k]
    auto r = reeverb2::fromParams (s.params);
    r.p[10] = r.p[11] = r.p[12] = 0;
    reeverb2::toParams (r, s.params);
    s.params[(std::size_t) reeverb2DirectLaw] = 1.0f;
    s.params[(std::size_t) reeverb2DirectLaw + 1] = 0.88f;
    s.params[(std::size_t) reeverb2DirectLaw + 2] = 0.075f;
    s.params[(std::size_t) reeverb2DirectLaw + 3] = 0.7f;
    std::vector<float> right;
    const auto h = parity190lfv::impulse (s, 48000.0, 4096, 1.0f, &right);
    CHECK (std::abs (h[0] - 0.88f) < 1e-5f && std::abs (h[1] + 0.075f) < 1e-5f && std::abs (h[3] + 0.075f * 0.49f) < 1e-5f);
    CHECK (std::abs (right[0]) < 1e-9f);
}

TEST (parity190lf_waves_trueverb_direct_sound_follows_distance)
{
    using namespace lynx;
    int n = 0;
    const auto at = [&] (const char* distance) {
        return parity190jh::waves ("TrueVerb Stereo", "TrueVerb",
                                   parity190jh::list (50, { { 0, "1.6" }, { 5, distance }, { 24, "63" }, { 42, "-10" }, { 43, "-12" } }), n);
    };
    const auto near = at ("2.6"), far = at ("13");
    CHECK (near.params[(std::size_t) reeverb2DirectLaw] == 1.0f && reeverb2::fromParams (near.params).p[10] == 0);
    // measured (X2, 2.6 m: a 0.893, b 0.069, ρ 0.702; X3, 13 m: a 0.851, b 0.0985, ρ 0.728)
    CHECK (std::abs (near.params[(std::size_t) reeverb2DirectLaw + 1] - 0.8930f) < 0.01f && std::abs (far.params[(std::size_t) reeverb2DirectLaw + 1] - 0.8510f) < 0.01f);
    CHECK (std::abs (near.params[(std::size_t) reeverb2DirectLaw + 2] - 0.0691f) < 0.003f && std::abs (far.params[(std::size_t) reeverb2DirectLaw + 2] - 0.0985f) < 0.003f);
    CHECK (std::abs (far.params[(std::size_t) reeverb2DirectLaw + 3] - 0.728f) < 0.003f);
    // the tail's measured decay with the direct sound on: Reeverb's decay 0.54 × the knob (1.6 s → 9), bass ×1.30, damping 120
    const auto r = reeverb2::fromParams (near.params);
    CHECK (r.p[5] == 9 && r.p[7] == 130 && r.p[6] == 120);
    // the direct sound off: no direct law
    const auto off = parity190jh::waves ("TrueVerb Stereo", "TrueVerb", parity190jh::list (50, { { 0, "1.6" }, { 29, "1" } }), n);
    CHECK (off.params[(std::size_t) reeverb2DirectLaw] == 0.0f);
    CHECK (reeverb2::fromParams (off.params).p[5] == 16 && reeverb2::fromParams (off.params).p[7] == 266); // round 190js's tail
}
