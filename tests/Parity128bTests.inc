// REA_rodada128 §3b: the Pro-C 3 / Pro-G / Pro-MB parity controls of Crest Compressor, Sentry Gate and Prism
// Multiband (core/DynamicsSuite.h) and their translation from the originals' saved states (core/FabFilterStates.h).
// The new parameters are appended and their defaults leave the output bit-identical to the round-108 engines.

namespace parity128b
{
using lynx::EffectKind;
using lynx::EffectParams;
namespace dyn = lynx::dyn;

inline std::vector<float> tone (double db, double hz, std::size_t n, double phase = 0.0)
{
    std::vector<float> x (n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = (float) (std::pow (10.0, db / 20.0) * std::sin (2.0 * dyn108::pi * hz * (double) i / dyn108::sr + phase));
    return x;
}

// runs a stereo signal through a fresh effect; returns (left, right)
inline std::pair<std::vector<float>, std::vector<float>> runStereo (EffectKind kind, const EffectParams& p, std::vector<float> l, std::vector<float> r)
{
    auto slot = lynx::makeEffectSlot (kind);
    slot.params = p;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (dyn108::sr);
    fx->reset();
    for (std::size_t off = 0; off < l.size(); off += 512)
    {
        const auto m = (int) std::min<std::size_t> (512, l.size() - off);
        fx->process (l.data() + off, r.data() + off, m, p, 120.0);
    }
    return { l, r };
}

} // namespace parity128b

TEST (parity128b_new_parameters_are_appended_with_neutral_defaults)
{
    using namespace parity128b;
    CHECK ((int) lynx::effectInfo (EffectKind::crestCompressor).params.size() == dyn::crest::count);
    CHECK ((int) lynx::effectInfo (EffectKind::sentryGate).params.size() == dyn::sentry::count);
    CHECK ((int) lynx::effectInfo (EffectKind::prismMultiband).params.size() == dyn::prism::count);
    CHECK (dyn::crest::dryGain == 38 && dyn::sentry::channelMode == 18 && dyn::prism::bandLookahead0 == 48); // old counts
    // (bit-identical defaults: the round-108 goldens in DynamicsTests108.inc run on the extended engines unchanged)
    // the texts
    const auto& c = lynx::effectInfo (EffectKind::crestCompressor);
    CHECK (c.params[(std::size_t) dyn::crest::dryGain].name == std::string ("Dry"));
    CHECK (dyn::paramText (EffectKind::crestCompressor, dyn::crest::dryGain, 0.0f) == "-inf dB");
    CHECK (dyn::paramText (EffectKind::crestCompressor, dyn::crest::meterScale, 0.75f) == "72 dB");
    CHECK (dyn::paramText (EffectKind::sentryGate, dyn::sentry::channelMode, 1.0f) == "Mid/Side");
    CHECK (dyn::paramText (EffectKind::sentryGate, dyn::sentry::wetPan, 0.5f) == "C");
}

TEST (parity128b_crest_dry_level_adds_the_uncompressed_signal)
{
    using namespace parity128b;
    namespace c = dyn::crest;
    // the dry law of the reference: −∞ at 0, −18 dB at 0.25, 0 dB at 0.5, +36 dB at 1
    CHECK (c::dryDb (0.0f) < -100.0 && std::abs (c::dryDb (0.25f) + 18.0) < 1e-9 && std::abs (c::dryDb (0.5f)) < 1e-9 && std::abs (c::dryDb (1.0f) - 36.0) < 1e-9);
    CHECK (std::abs (c::dryDb (c::dryValue (-30.0)) + 30.0) < 1e-3 && std::abs (c::dryDb (c::dryValue (6.0)) - 6.0) < 1e-3);
    // heavy compression of a loud tone; dry at 0 dB brings back (wet + dry)
    auto p = dyn108::defaults (EffectKind::crestCompressor);
    p[c::threshold] = 0.5f; // −30 dB
    p[c::ratio] = c::ratioValue (20.0);
    p[c::autoGain] = 0.0f;
    const auto x = tone (-6.0, 440.0, 48000);
    const auto wet = dyn108::run (EffectKind::crestCompressor, p, x);
    p[c::dryGain] = c::dryValue (0.0);
    const auto both = dyn108::run (EffectKind::crestCompressor, p, x);
    const auto wDb = dyn108::rmsDb (wet, 36000, 48000), bDb = dyn108::rmsDb (both, 36000, 48000), xDb = dyn108::rmsDb (x, 36000, 48000);
    std::printf ("  Crest dry: wet %.2f dB, wet + dry %.2f dB, input %.2f dB\n", wDb, bDb, xDb);
    CHECK (wDb < xDb - 10.0);
    CHECK (bDb > xDb - 0.1 && bDb < xDb + 2.0); // the dry signal dominates (in phase with the wet)
}

TEST (parity128b_crest_link_modes_work_in_mid_side)
{
    using namespace parity128b;
    namespace c = dyn::crest;
    auto p = dyn108::defaults (EffectKind::crestCompressor);
    p[c::threshold] = 0.5f; // −30 dB
    p[c::ratio] = c::ratioValue (20.0);
    p[c::autoGain] = 0.0f;
    const std::size_t n = 48000;
    // a loud mid (440 Hz, both channels) and a quiet side (3 kHz, opposite)
    auto mid = tone (-6.0, 440.0, n), side = tone (-40.0, 3000.0, n);
    std::vector<float> l (n), r (n);
    for (std::size_t i = 0; i < n; ++i) { l[i] = mid[i] + side[i]; r[i] = mid[i] - side[i]; }
    const auto midOf = [] (const std::pair<std::vector<float>, std::vector<float>>& y) {
        std::vector<float> m (y.first.size());
        for (std::size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (y.first[i] + y.second[i]);
        return m;
    };
    const auto sideOf = [] (const std::pair<std::vector<float>, std::vector<float>>& y) {
        std::vector<float> s (y.first.size());
        for (std::size_t i = 0; i < s.size(); ++i) s[i] = 0.5f * (y.first[i] - y.second[i]);
        return s;
    };
    const auto level = [&] (const std::vector<float>& y) { return dyn108::rmsDb (y, n * 3 / 4, n); };
    const auto inSide = level (side), inMid = level (mid);
    p[c::linkMode] = c::linkModeValue (c::LinkMode::mid); // the mid is compressed, the side passes
    auto y = runStereo (EffectKind::crestCompressor, p, l, r);
    const auto midA = level (midOf (y)), sideA = level (sideOf (y));
    p[c::linkMode] = c::linkModeValue (c::LinkMode::midToSide); // the loud mid ducks the side; the mid passes
    y = runStereo (EffectKind::crestCompressor, p, l, r);
    const auto midB = level (midOf (y)), sideB = level (sideOf (y));
    p[c::linkMode] = c::linkModeValue (c::LinkMode::side); // the quiet side is under the threshold: nothing moves
    y = runStereo (EffectKind::crestCompressor, p, l, r);
    const auto midC = level (midOf (y)), sideC = level (sideOf (y));
    std::printf ("  Crest link modes (in mid %.1f, side %.1f): mid-only %.1f / %.1f, mid>side %.1f / %.1f, side-only %.1f / %.1f dB\n", inMid,
                 inSide, midA, sideA, midB, sideB, midC, sideC);
    CHECK (midA < inMid - 10.0 && std::abs (sideA - inSide) < 0.3);
    CHECK (std::abs (midB - inMid) < 0.3 && sideB < inSide - 10.0);
    CHECK (std::abs (midC - inMid) < 0.3 && std::abs (sideC - inSide) < 0.3);
}

TEST (parity128b_crest_auto_threshold_follows_the_programme)
{
    using namespace parity128b;
    namespace c = dyn::crest;
    CHECK (std::abs (c::autoThresholdDb (-18.0, -20.0) + 20.0) < 1e-9 && std::abs (c::autoThresholdDb (-12.0, -20.0) + 14.0) < 1e-9);
    auto p = dyn108::defaults (EffectKind::crestCompressor);
    p[c::threshold] = 0.7f; // −18 dB: at the programme's level
    p[c::ratio] = c::ratioValue (4.0);
    p[c::autoGain] = 0.0f;
    p[c::autoThreshold] = 1.0f;
    // a quiet programme (−40 dB peak) that a fixed −18 dB threshold would leave alone gets compressed
    const auto x = tone (-40.0, 440.0, 48000 * 8);
    const auto y = dyn108::run (EffectKind::crestCompressor, p, x);
    p[c::autoThreshold] = 0.0f;
    const auto z = dyn108::run (EffectKind::crestCompressor, p, x);
    const auto a = dyn108::rmsDb (y, x.size() - 48000, x.size()), b = dyn108::rmsDb (z, x.size() - 48000, x.size());
    std::printf ("  Crest auto threshold: %.2f dB (fixed: %.2f dB) for a -40 dB tone\n", a, b);
    CHECK (std::abs (b - dyn108::rmsDb (x, x.size() - 48000, x.size())) < 0.1);
    CHECK (a < b - 1.0);
}

TEST (parity128b_sentry_ducking_pans_and_side_chain_mix)
{
    using namespace parity128b;
    namespace s = dyn::sentry;
    auto p = dyn108::defaults (EffectKind::sentryGate);
    p[s::threshold] = 0.5f;           // −30 dB
    p[s::range] = 0.2f;               // 20 dB
    // static curves: ducking turns the gate upside down
    p[s::ducking] = 1.0f;
    CHECK (std::abs (s::staticGainDb (p, -10.0) + 20.0) < 0.01 && std::abs (s::staticGainDb (p, -60.0)) < 0.01);
    // rendered: a loud tone is ducked by the range
    const auto x = tone (-10.0, 440.0, 48000);
    auto y = dyn108::run (EffectKind::sentryGate, p, x);
    CHECK (std::abs (dyn108::rmsDb (y, 36000, 48000) - (dyn108::rmsDb (x, 36000, 48000) - 20.0)) < 0.5);
    p[s::ducking] = 0.0f;
    // wet pan fully right: the open gate's output leaves the left channel
    p[s::wetPan] = 1.0f;
    const auto st = runStereo (EffectKind::sentryGate, p, x, x);
    CHECK (dyn108::rmsDb (st.first, 36000, 48000) < -100.0 && std::abs (dyn108::rmsDb (st.second, 36000, 48000) - dyn108::rmsDb (x, 36000, 48000)) < 0.1);
    p[s::wetPan] = 0.5f;
    // side-chain gain: −36 dB on both detector channels keeps a −10 dB tone under the threshold → closed
    p[s::scGainL] = 0.0f;
    p[s::scGainR] = 0.0f;
    y = dyn108::run (EffectKind::sentryGate, p, x);
    CHECK (dyn108::rmsDb (y, 36000, 48000) < dyn108::rmsDb (x, 36000, 48000) - 19.0);
    // mid / side: a side-only signal, detectors on mid (gain 0 dB) only → the gate stays closed; with the side → open
    p[s::channelMode] = 1.0f;
    p[s::scGainL] = 0.5f;
    std::vector<float> l = x, r (x.size());
    for (std::size_t i = 0; i < x.size(); ++i) r[i] = -x[i];
    const auto closed = runStereo (EffectKind::sentryGate, p, l, r);
    p[s::scGainR] = 0.5f;
    const auto open = runStereo (EffectKind::sentryGate, p, l, r);
    std::printf ("  Sentry M/S detector: mid only %.1f dB, mid + side %.1f dB\n", dyn108::rmsDb (closed.first, 36000, 48000), dyn108::rmsDb (open.first, 36000, 48000));
    CHECK (dyn108::rmsDb (closed.first, 36000, 48000) < -25.0);
    CHECK (std::abs (dyn108::rmsDb (open.first, 36000, 48000) - dyn108::rmsDb (x, 36000, 48000)) < 0.2);
    // the input level
    p = dyn108::defaults (EffectKind::sentryGate);
    p[s::input] = (6.0f + 24.0f) / 48.0f;
    y = dyn108::run (EffectKind::sentryGate, p, x);
    CHECK (std::abs (dyn108::rmsDb (y, 36000, 48000) - dyn108::rmsDb (x, 36000, 48000) - 6.0) < 0.05);
}

TEST (parity128b_prism_band_lookahead_leads_by_its_own_time)
{
    using namespace parity128b;
    namespace m = dyn::prism;
    auto p = dyn108::defaults (EffectKind::prismMultiband);
    p[m::lookahead] = 0.5f; // 10 ms global
    for (int b = 0; b < m::bands; ++b) p[(std::size_t) m::param (b, m::fAttack)] = 0.0f;
    p[(std::size_t) m::param (0, m::fRange)] = 0.0f; // −30 dB
    // a low burst after silence: with the band's full look-ahead the gain is already down when it arrives
    std::vector<float> x (48000, 0.0f);
    for (std::size_t i = 24000; i < x.size(); ++i) x[i] = (float) (0.5 * std::sin (2.0 * dyn108::pi * 60.0 * (double) i / dyn108::sr));
    int lat = 0;
    const auto full = dyn108::run (EffectKind::prismMultiband, p, x, &lat);
    p[(std::size_t) (m::bandLookahead0 + 0)] = 0.0f; // no look-ahead for band 1
    const auto none = dyn108::run (EffectKind::prismMultiband, p, x);
    double peakFull = 0.0, peakNone = 0.0;
    for (std::size_t i = 24000; i < 24000 + 240; ++i) { peakFull = std::max (peakFull, (double) std::abs (full[i])); peakNone = std::max (peakNone, (double) std::abs (none[i])); }
    std::printf ("  Prism band look-ahead: latency %d, first 5 ms peak %.3f (band look-ahead 10 ms) vs %.3f (0 ms)\n", lat, peakFull, peakNone);
    CHECK (lat == 480);
    CHECK (peakNone > peakFull * 1.05); // the band without look-ahead lets the onset through (measured 0.488 vs 0.446)
    CHECK (dyn::paramText (EffectKind::prismMultiband, m::bandLookahead0, 0.5f) == "10.0 ms");
}

TEST (parity128b_translators_read_the_new_controls)
{
    using namespace parity128b;
    namespace fab = lynx::fabfilter;
    // Pro-C 3 (FFBS, 100 values): dry gain 0 dB (17 = 0.0 on the level law), wet 0 dB, link 100 % mid-only (26 = 1.0),
    // mode S → M (27 = 3), auto threshold on, meter scale 18 dB (97 = 1), input meter off
    fab::State c3;
    c3.count = 100;
    c3.values.assign (100, 0.0f);
    c3.values[6] = 60.0f;
    c3.values[17] = 0.0f;
    c3.values[15] = 0.0f;
    c3.values[26] = 1.0f;
    c3.values[27] = 3.0f;
    c3.values[2] = 1.0f;
    c3.values[97] = 1.0f;
    c3.values[99] = 0.0f;
    auto slot = lynx::makeEffectSlot (EffectKind::crestCompressor);
    CHECK (fab::fromProC3 (c3, slot) >= 25);
    namespace c = dyn::crest;
    CHECK (std::abs (c::dryDb (slot.params[c::dryGain])) < 0.01);
    CHECK (c::linkModeOf (slot.params[c::linkMode]) == c::LinkMode::sideToMid);
    CHECK (slot.params[c::autoThreshold] > 0.5f && slot.params[c::inputMeter] < 0.5f);
    CHECK (std::abs (c::meterRangeDb (slot.params[c::meterScale]) - 18.0) < 0.01);
    // Pro-G (FabF, 34 values): side-chain left −12 dB / mix +0.5, right +6 dB, wet pan −0.5, M/S, input +3 dB
    fab::State g;
    g.named = true;
    g.count = 34;
    g.values.assign (34, 0.0f);
    g.values[12] = -12.0f;
    g.values[13] = 0.5f;
    g.values[14] = 6.0f;
    g.values[21] = -0.5f;
    g.values[27] = 1.0f;
    g.values[28] = 3.0f / 36.0f; // the level law: 36 dB per unit above −0.6
    namespace s = dyn::sentry;
    // round trial187b [EXEC trial]: the wet / dry levels and pans only act in Expert mode (26)
    slot = lynx::makeEffectSlot (EffectKind::sentryGate);
    CHECK (fab::fromProG (g, slot) > 20);
    CHECK (std::abs (s::panOf (slot.params[s::wetPan])) < 1e-4 && slot.params[s::dry] == 0.0f);
    g.values[26] = 1.0f;
    slot = lynx::makeEffectSlot (EffectKind::sentryGate);
    CHECK (fab::fromProG (g, slot) > 20);
    CHECK (std::abs (s::scGainDb (slot.params[s::scGainL]) + 12.0) < 0.01 && std::abs (s::scGainDb (slot.params[s::scGainR]) - 6.0) < 0.01);
    CHECK (std::abs (s::mixOf (slot.params[s::scMixL]) - 0.5) < 1e-4 && std::abs (s::panOf (slot.params[s::wetPan]) + 0.5) < 1e-4);
    CHECK (slot.params[s::channelMode] > 0.5f && std::abs (slot.params[s::input] - (3.0f + 24.0f) / 48.0f) < 1e-4f);
}
