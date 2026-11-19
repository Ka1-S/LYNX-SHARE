// Round 190lg (sub-round mb): Prism Multiband's revision 1 (hidden dyn::prism::revision / lookaheadSwitch) on Pro-MB's own
// BandCompander graph (static REA, docs/analise/REA_rodada190lg.md §7): the detector with its soft range (0.6 × knee +
// 12 dB wide), a peak hold and a low pass timed by the look-ahead, follower A, max, follower B; the inverted graph (36 dB
// − the reduction) for upward compression and downward expansion. Only the Pro-MB translator sets it; a project saved
// before keeps the measured detector (0). After DynamicsTests108 (its helpers).
namespace parity190lgmb
{
// one Pro-MB band over the whole range: threshold −30 dB, the given range / mode, ratio 4 : 1, attack / release 20 %,
// knee 24 dB, look-ahead 1 ms (switch `la`); the other bands unused
inline lynx::EffectSlot proMb (float rangeDb, float mode, float la)
{
    using namespace lynx;
    std::vector<std::pair<int, float>> v { { 0, 1.0f }, { 1, (float) std::log2 (30.0) }, { 2, 0.5f }, { 3, (float) std::log2 (30000.0) },
                                           { 4, 0.5f }, { 5, mode }, { 6, 0.5f }, { 7, rangeDb }, { 8, 0.6f }, { 9, 20.0f },
                                           { 10, 20.0f }, { 11, 24.0f }, { 12, 1.0f }, { 133, 1.0f }, { 139, 1.0f }, { 141, la } };
    for (int b = 1; b < 6; ++b)
        v.push_back ({ 22 * b, 2.0f });
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "FabFilter Pro-MB";
    id.state = dyn108::fabf (151, v);
    const auto* entry = findPluginSubstitute (id, false);
    return entry != nullptr ? makeSubstituteSlot (*entry, &id, nullptr) : makeEffectSlot (EffectKind::prismMultiband);
}
// the gain (dB) of a steady 200 Hz tone at `db` dBFS over its last half second
inline double toneGain (const lynx::EffectParams& p, double db)
{
    std::vector<float> x (96000);
    const auto a = std::pow (10.0, db / 20.0);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (a * std::sin (2.0 * dyn108::pi * 200.0 * (double) i / dyn108::sr));
    const auto y = dyn108::run (lynx::EffectKind::prismMultiband, p, x);
    double ex = 0.0, ey = 0.0;
    for (std::size_t i = 48000; i < 90000; ++i)
    {
        ex += (double) x[i] * x[i];
        ey += (double) y[i] * y[i];
    }
    return 10.0 * std::log10 (ey / ex);
}
} // namespace parity190lgmb

TEST (parity190lg_prism_revision_is_hidden_set_by_the_translator_only)
{
    using namespace lynx;
    namespace m = dyn::prism;
    CHECK (effectInfo (EffectKind::prismMultiband).params.size() == (std::size_t) m::count); // no new control or string
    CHECK (m::revision == m::count && m::lookaheadSwitch == m::count + 1);
    auto fresh = makeEffectSlot (EffectKind::prismMultiband);
    CHECK (fresh.params[(std::size_t) m::revision] == 0.0f && savedEffectParamCount (fresh) <= (std::size_t) m::count);
    const auto on = parity190lgmb::proMb (-12.0f, 0.0f, 1.0f), off = parity190lgmb::proMb (-12.0f, 0.0f, 0.0f);
    CHECK (on.kind == EffectKind::prismMultiband && on.params[(std::size_t) m::revision] == 1.0f && on.params[(std::size_t) m::lookaheadSwitch] == 1.0f);
    CHECK (off.params[(std::size_t) m::revision] == 1.0f && off.params[(std::size_t) m::lookaheadSwitch] == 0.0f);
    CHECK (savedEffectParamCount (on) == (std::size_t) m::lookaheadSwitch + 1);
    // saved before this round: back to the measured detector
    auto old = on;
    upgradeLoadedEffectParams (old, (std::size_t) m::count);
    CHECK (old.params[(std::size_t) m::revision] == 0.0f && old.params[(std::size_t) m::lookaheadSwitch] == 0.0f);
    auto now = on;
    upgradeLoadedEffectParams (now, (std::size_t) m::lookaheadSwitch + 1);
    CHECK (now.params[(std::size_t) m::revision] == 1.0f && now.params[(std::size_t) m::lookaheadSwitch] == 1.0f);
    // latency: the reference's 20 ms with its switch on, none with it off (revision 0 keeps the band's own 1 ms)
    int la1 = 0, la0 = 0, laOld = 0;
    std::vector<float> x (4800, 0.0f);
    dyn108::run (EffectKind::prismMultiband, on.params, x, &la1);
    dyn108::run (EffectKind::prismMultiband, off.params, x, &la0);
    dyn108::run (EffectKind::prismMultiband, old.params, x, &laOld);
    CHECK (la1 == 960 && la0 == 0 && laOld == 48);
}

TEST (parity190lg_prism_revision1_soft_range_and_inverted_modes)
{
    // the static law (24 dB over a −30 dB threshold at 4 : 1 is 18 dB before the range): the reference's soft range of
    // 0.6 × 24 + 12 = 26.4 dB around −12 leaves 11.0 dB; round 150's 12 dB knee gives the full 12
    using namespace lynx;
    namespace m = dyn::prism;
    auto rev1 = parity190lgmb::proMb (-12.0f, 0.0f, 1.0f).params, rev0 = rev1;
    rev0[(std::size_t) m::revision] = rev0[(std::size_t) m::lookaheadSwitch] = 0.0f;
    const auto g1 = parity190lgmb::toneGain (rev1, -6.0), g0 = parity190lgmb::toneGain (rev0, -6.0);
    std::printf ("  prism revision 1: %.2f dB (revision 0: %.2f dB) at 24 dB over\n", g1, g0);
    CHECK (g1 > -11.6 && g1 < -10.4 && g0 < g1 - 0.5);
    // downward expansion (−12 dB): the inverted graph scales 36 dB of detector by 12 / 36 — at −42 dBFS (12 dB under, knee
    // 24: 3 dB × (4 − 1) = 9 dB of 36) the band loses 3 dB
    auto ex = parity190lgmb::proMb (-12.0f, 1.0f, 1.0f).params;
    const auto ge = parity190lgmb::toneGain (ex, -42.0), gLoud = parity190lgmb::toneGain (ex, -6.0);
    std::printf ("  prism revision 1 expander: %.2f dB at 12 dB under, %.2f dB over\n", ge, gLoud);
    CHECK (ge < -2.0 && ge > -4.5 && std::abs (gLoud) < 0.3);
}
