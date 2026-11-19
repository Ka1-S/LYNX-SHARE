// Round 190lp (sub-round lg): Apex Limiter's and Sentry Gate's revision 1 (hidden apex::pRevision / dyn::sentry::revision),
// refitted black-box on the reference limiter's and gate's renders (docs/PARITY.md "Round 190lp (Pro-L 2 → Apex, Pro-G →
// Sentry)"). A new instance and a translated Pro-L 2 / Pro-G state get 1; a project or preset saved before, the Live maps,
// the substitutes and LYNX's own chains (mastering looks, reference match, demo songs, Clean Voice) keep 0. After
// DynamicsTests108 (its helpers).
#include "core/ApexLimiter.h"
#include "core/FabFilterStates.h"

namespace parity190lplg
{
using lynx::EffectKind;
using lynx::EffectParams;
using namespace dyn108;
namespace ap = lynx::apex;
namespace sg = lynx::dyn::sentry;

// a tone at `hz`: −40 dBFS for 0.2 s, `loud` dBFS for `burst` s, −40 dBFS for `tail` s
inline std::vector<float> burst (double loud, double burstS, double tail, double hz = 1000.0, double quiet = -40.0)
{
    std::vector<float> x ((std::size_t) ((0.2 + burstS + tail) * sr));
    const auto a = (std::size_t) (0.2 * sr), b = (std::size_t) ((0.2 + burstS) * sr);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, (i >= a && i < b ? loud : quiet) / 20.0) * std::sin (2.0 * pi * hz * (double) i / sr));
    return x;
}
inline double levelDb (const std::vector<float>& y, double from, double to)
{
    return rmsDb (y, (std::size_t) (from * sr), (std::size_t) (to * sr));
}
// the third harmonic of a steady tone at `hz` relative to its fundamental (dB) over [from, to) s
inline double h3Db (const std::vector<float>& y, double hz, double from, double to)
{
    double c1 = 0.0, s1 = 0.0, c3 = 0.0, s3 = 0.0;
    for (auto i = (std::size_t) (from * sr); i < (std::size_t) (to * sr); ++i)
    {
        const double t = (double) i / sr;
        c1 += y[i] * std::cos (2.0 * pi * hz * t); s1 += y[i] * std::sin (2.0 * pi * hz * t);
        c3 += y[i] * std::cos (6.0 * pi * hz * t); s3 += y[i] * std::sin (6.0 * pi * hz * t);
    }
    return 10.0 * std::log10 ((c3 * c3 + s3 * s3 + 1e-30) / (c1 * c1 + s1 * s1 + 1e-30));
}
inline EffectParams apex (ap::Style style, double gainDb, double ceilingDb, float revision)
{
    auto p = defaults (EffectKind::apexLimiter);
    p[ap::pStyle] = (float) style / (float) (ap::styleCount - 1);
    p[ap::pGain] = (float) (gainDb / 30.0);
    p[ap::pCeiling] = (float) (1.0 + ceilingDb / 30.0);
    p[ap::pRevision] = revision;
    return p;
}
} // namespace parity190lplg

TEST (parity190lp_lg_revisions_are_hidden_new_and_translated_get_one)
{
    using namespace lynx;
    namespace ap = apex;
    namespace sg = dyn::sentry;
    CHECK (ap::info().params.size() == (std::size_t) ap::paramCount && ap::pRevision == ap::paramCount); // no new control
    CHECK (effectInfo (EffectKind::sentryGate).params.size() == (std::size_t) sg::count && sg::revision == sg::count);
    // a new instance runs revision 1 and saves it
    const auto a = makeEffectSlot (EffectKind::apexLimiter), g = makeEffectSlot (EffectKind::sentryGate);
    CHECK (a.params[(std::size_t) ap::pRevision] == 1.0f && savedEffectParamCount (a) == (std::size_t) ap::pRevision + 1);
    CHECK (g.params[(std::size_t) sg::revision] == 1.0f && savedEffectParamCount (g) == (std::size_t) sg::revision + 1);
    // saved before this round: back to revision 0; saved after: kept
    auto oldA = a, oldG = g, nowA = a, nowG = g;
    upgradeLoadedEffectParams (oldA, (std::size_t) ap::paramCount);
    upgradeLoadedEffectParams (oldG, (std::size_t) sg::count);
    upgradeLoadedEffectParams (nowA, (std::size_t) ap::pRevision + 1);
    upgradeLoadedEffectParams (nowG, (std::size_t) sg::revision + 1);
    CHECK (oldA.params[(std::size_t) ap::pRevision] == 0.0f && oldG.params[(std::size_t) sg::revision] == 0.0f);
    CHECK (nowA.params[(std::size_t) ap::pRevision] == 1.0f && nowG.params[(std::size_t) sg::revision] == 1.0f);
    CHECK (savedEffectParamCount (oldA) == (std::size_t) ap::paramCount && savedEffectParamCount (oldG) == (std::size_t) sg::count);
    // the translators set 1 (Pro-L 2: FabF v2, 32 values; Pro-G: 34)
    auto l2 = makeEffectSlot (EffectKind::apexLimiter);
    l2.params[(std::size_t) ap::pRevision] = 0.0f;
    const auto stL2 = fabfilter::parseState (dyn108::fabf (32, { { 0, 6.0f }, { 1, 4.0f }, { 2, 0.2f }, { 18, -1.0f } }));
    CHECK (stL2.has_value() && fabfilter::translatorNamed ("pro-l2") != nullptr);
    if (stL2 && fabfilter::translatorNamed ("pro-l2") != nullptr)
    {
        CHECK (fabfilter::translatorNamed ("pro-l2") (*stL2, l2) > 0);
        CHECK (l2.params[(std::size_t) ap::pRevision] == 1.0f);
    }
    auto pg = makeEffectSlot (EffectKind::sentryGate);
    pg.params[(std::size_t) sg::revision] = 0.0f;
    const auto stG = fabfilter::parseState (dyn108::fabf (34, { { 0, -30.0f }, { 5, 3.0f } }));
    if (stG && fabfilter::translatorNamed ("pro-g") != nullptr)
    {
        CHECK (fabfilter::translatorNamed ("pro-g") (*stG, pg) > 0);
        CHECK (pg.params[(std::size_t) sg::revision] == 1.0f);
    }
}

TEST (parity190lp_lg_apex_output_level_is_a_gain_after_the_limiter)
{
    // revision 1: a ceiling of −1 dB is the reference's output level — what stays under the limit comes out 1 dB lower
    // too (refcap030 S3: the quiet carrier at gain + level); revision 0 keeps it as a ceiling (the quiet part untouched)
    using namespace parity190lplg;
    const auto x = burst (-6.0, 0.5, 0.5);
    const auto y1 = run (EffectKind::apexLimiter, apex (ap::Style::clean, 8.0, -1.0, 1.0f), x);
    const auto y0 = run (EffectKind::apexLimiter, apex (ap::Style::clean, 8.0, -1.0, 0.0f), x);
    const auto q1 = levelDb (y1, 0.02, 0.15) - levelDb (x, 0.02, 0.15), q0 = levelDb (y0, 0.02, 0.15) - levelDb (x, 0.02, 0.15);
    std::printf ("  apex output level: quiet part %+.2f dB (revision 0: %+.2f dB)\n", q1, q0);
    CHECK (std::abs (q1 - 7.0) < 0.02 && std::abs (q0 - 8.0) < 0.02);
    float peak1 = 0.0f;
    for (std::size_t i = (std::size_t) (0.3 * sr); i < (std::size_t) (0.7 * sr); ++i)
        peak1 = std::max (peak1, std::abs (y1[i]));
    CHECK (20.0 * std::log10 ((double) peak1) < -0.95);
}

TEST (parity190lp_lg_apex_smooth_styles_hold_a_tone_and_transparent_recovers_faster_when_deep)
{
    using namespace parity190lplg;
    // Loud (the reference's Aggressive), a 100 Hz tone 6 dB over the ceiling: revision 1 holds one gain through the
    // cycle (the hold of ~0.9 half period), revision 0 rides the waveform (its fast stage) — measured third harmonic
    const auto tone = burst (0.0, 0.6, 0.2, 100.0);
    const auto l1 = run (EffectKind::apexLimiter, apex (ap::Style::loud, 6.0, 0.0, 1.0f), tone);
    const auto l0 = run (EffectKind::apexLimiter, apex (ap::Style::loud, 6.0, 0.0, 0.0f), tone);
    const auto h1 = h3Db (l1, 100.0, 0.5, 0.75), h0 = h3Db (l0, 100.0, 0.5, 0.75);
    std::printf ("  apex loud, 100 Hz 6 dB over: H3 %.1f dB (revision 0: %.1f dB)\n", h1, h0);
    CHECK (h1 < -100.0 && h1 < h0 - 30.0);
    // Clean (Transparent), 24 dB of gain on a 1 kHz tone at 0 dBFS, then −30 dBFS: 100 ms after the loud part
    // revision 1 has recovered more (its slow stage speeds up with the reduction it holds)
    const auto deep = burst (0.0, 0.5, 0.6, 1000.0, -30.0);
    const auto c1 = run (EffectKind::apexLimiter, apex (ap::Style::clean, 24.0, 0.0, 1.0f), deep);
    const auto c0 = run (EffectKind::apexLimiter, apex (ap::Style::clean, 24.0, 0.0, 0.0f), deep);
    const auto g1 = levelDb (c1, 0.79, 0.81) - levelDb (deep, 0.79, 0.81), g0 = levelDb (c0, 0.79, 0.81) - levelDb (deep, 0.79, 0.81);
    std::printf ("  apex clean, 100 ms after 24 dB of limiting: gain %+.2f dB (revision 0: %+.2f dB)\n", g1, g0);
    CHECK (g1 > g0 + 0.5);
    // the latency does not change with the revision
    int la1 = 0, la0 = 0;
    run (EffectKind::apexLimiter, apex (ap::Style::loud, 6.0, 0.0, 1.0f), tone, &la1);
    run (EffectKind::apexLimiter, apex (ap::Style::loud, 6.0, 0.0, 0.0f), tone, &la0);
    CHECK (la1 == la0 && la1 == ap::latencyAt (48000.0));
}

namespace parity190lplg
{
// Sentry: threshold −30 dB, ratio 100 : 1, range 40 dB, attack 0.1 ms, the given style / release / hold
inline EffectParams gate (sg::Style style, double releaseMsV, double holdMsV, float revision)
{
    auto p = defaults (EffectKind::sentryGate);
    p[sg::style] = sg::styleValue (style);
    p[sg::threshold] = 0.5f;
    p[sg::ratio] = 1.0f;
    p[sg::range] = 0.4f;
    p[sg::attack] = (float) std::pow (0.1 / 1000.0, 0.25);
    p[sg::release] = (float) std::cbrt (releaseMsV / 5000.0);
    p[sg::hold] = (float) std::sqrt (holdMsV / 250.0);
    p[sg::revision] = revision;
    return p;
}
// the gain (dB) over 1 ms around `t` s, output − input
inline double gainAt (const std::vector<float>& y, const std::vector<float>& x, double t)
{
    return levelDb (y, t - 0.0005, t + 0.0005) - levelDb (x, t - 0.0005, t + 0.0005);
}
} // namespace parity190lplg

TEST (parity190lp_lg_sentry_revision1_closes_in_db_after_its_wait)
{
    using namespace parity190lplg;
    // a 1 kHz burst at −6 dBFS over a −50 dBFS bed (it ends at 0.7 s)
    const auto x = burst (-6.0, 0.5, 0.8, 1000.0, -50.0);
    // Classic, release 100 ms, no hold: open until its wait (0.08 × 100 ms), then 20 dB per (16 + 0.3 × 100) ms
    const auto c1 = run (EffectKind::sentryGate, gate (sg::Style::classic, 100.0, 0.0, 1.0f), x);
    const auto c0 = run (EffectKind::sentryGate, gate (sg::Style::classic, 100.0, 0.0, 0.0f), x);
    const auto open1 = gainAt (c1, x, 0.705), a = gainAt (c1, x, 0.74), b = gainAt (c1, x, 0.76);
    const auto slope = (a - b) / 20.0; // dB per ms in the straight part (the reference fell ~0.3 dB/ms at release 100 ms)
    std::printf ("  sentry classic: %.2f dB at +5 ms, fall %.3f dB/ms, revision 0 at +40 ms %.1f dB, revision 1 %.1f dB\n",
                 open1, slope, gainAt (c0, x, 0.74), a);
    CHECK (open1 > -0.5);
    CHECK (slope > 0.2 && slope < 0.4);
    CHECK (gainAt (c1, x, 1.3) < -38.0 && gainAt (c1, x, 1.3) > -40.5); // lands on the range
    // Guitar, release 50 ms, no hold: waits 20 + 2.3 × 50 = 135 ms (revision 0: 110 ms + its release)
    const auto g1 = run (EffectKind::sentryGate, gate (sg::Style::guitar, 50.0, 0.0, 1.0f), x);
    std::printf ("  sentry guitar: %.2f dB at +120 ms, %.1f dB at +170 ms\n", gainAt (g1, x, 0.82), gainAt (g1, x, 0.87));
    CHECK (gainAt (g1, x, 0.82) > -0.5 && gainAt (g1, x, 0.87) < -10.0);
    // Vocal and Upward keep revision 0's gate (their fits did not pass the gate): bit for bit
    for (auto st : { sg::Style::vocal, sg::Style::upward })
        CHECK (run (EffectKind::sentryGate, gate (st, 100.0, 20.0, 1.0f), x) == run (EffectKind::sentryGate, gate (st, 100.0, 20.0, 0.0f), x));
}

TEST (parity190lp_lg_substitutes_and_maps_keep_revision0)
{
    using namespace lynx;
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "SSL X-Limit";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry != nullptr)
    {
        const auto slot = makeSubstituteSlot (*entry, &id, nullptr);
        CHECK (slot.kind == EffectKind::apexLimiter && slot.params[(std::size_t) apex::pRevision] == 0.0f);
    }
    PluginIdentity l2;
    l2.format = ForeignPlugin::Format::vst3;
    l2.name = "FabFilter Pro-L 2";
    l2.state = dyn108::fabf (32, { { 0, 6.0f }, { 1, 4.0f }, { 2, 0.2f } });
    if (const auto* e = findPluginSubstitute (l2, false))
    {
        const auto slot = makeSubstituteSlot (*e, &l2, nullptr);
        CHECK (slot.kind == EffectKind::apexLimiter && slot.params[(std::size_t) apex::pRevision] == 1.0f);
    }
}

TEST (parity190lp_lg_apex_unity_and_audition_with_the_output_level)
{
    // revision 1 with the default ceiling (−0.1 dB, an output level after the limiter): Unity gain still plays the input
    // at its own level, and output + what Audition removes = the input after the gain and the level
    using namespace parity190lplg;
    auto p = apex (ap::Style::clean, 12.0, -1.0, 1.0f);
    const auto x = burst (-6.0, 0.4, 0.4);
    auto q = p;
    q[ap::pUnity] = 1.0f;
    const auto u = run (EffectKind::apexLimiter, q, x);
    double err = 0.0;
    for (std::size_t i = (std::size_t) (0.02 * sr); i < (std::size_t) (0.15 * sr); ++i) err = std::max (err, (double) std::abs (u[i] - x[i]));
    CHECK (err < 1e-5);
    q = p;
    q[ap::pAudition] = 1.0f;
    const auto removed = run (EffectKind::apexLimiter, q, x), out = run (EffectKind::apexLimiter, p, x);
    const auto g = std::pow (10.0, (12.0 - 1.0) / 20.0);
    err = 0.0;
    for (std::size_t i = 0; i + 4800 < x.size(); ++i) err = std::max (err, std::abs ((double) out[i] + removed[i] - g * x[i]));
    std::printf ("  apex revision 1: unity error %.2g, output + removed vs input x gain x level %.2g\n", 0.0, err);
    CHECK (err < 1e-4);
}
