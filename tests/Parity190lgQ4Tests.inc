// Round 190lg: Facet's automatic threshold, revision 1 (hidden facet::autoRevisionParam), on Pro-Q 4's own BandCompander
// structure (static REA, docs/analise/REA_rodada190lg.md): the band's per-sample magnitude against a peak follower of it
// (instant rise, 10 s fall, floor −40 dB), the range's gain law, then three followers on the reduction (their times from the
// region and the knobs, scaled as fitted on refcap030's gain trajectories). Only the Pro-Q 4 translator sets it; a project
// saved before keeps round 190js's law (0). After Parity190jh / 190js: their helpers.
namespace parity190lgq4
{
inline lynx::EffectSlot autoBand (double rangeDb, float rev)
{
    using namespace lynx;
    auto s = parity190jh::facetBand (0, facet::shapeValue (facet::Shape::bell), facet::gainValue (6.0), facet::rangeValue (rangeDb),
                                     facet::thresholdValue (-80.0), 0.5f, 0.5f, 0.0f);
    s.params[(std::size_t) facet::autoThresholdParam0] = 1.0f;
    s.params[(std::size_t) facet::autoRevisionParam] = rev;
    return s;
}
// a 1 kHz tone at `loud` dBFS for 1 s, then −40 dBFS; returns the band's dynamic gain (dB) over [a, b) s
inline double gainAt (double rangeDb, float rev, double loud, double a, double b)
{
    const double sr = 48000.0;
    const int n = (int) (2.5 * sr);
    std::vector<float> l ((std::size_t) n), r;
    for (int i = 0; i < n; ++i)
        l[(std::size_t) i] = (float) (std::pow (10.0, (i < (int) sr ? loud : -40.0) / 20.0) * std::sin (2.0 * M_PI * 1000.0 * i / sr));
    r = l;
    const auto in = l;
    parity190jh::render (autoBand (rangeDb, rev), sr, l, r);
    return parity190js::bandGainDb (in, l, 0, sr, a, b) - 6.0;
}
} // namespace parity190lgq4

TEST (parity190lg_facet_auto_revision_is_hidden_set_by_the_translator_only)
{
    using namespace lynx;
    CHECK (facet::autoRevisionParam == facet::autoThresholdParam0 + facet::bands && facet::dispersionCountParam == facet::autoRevisionParam + 1); // (rollup step 9: round 190ln's dispersion [126 … 128] follows)
    auto s = makeEffectSlot (EffectKind::facetEq);
    CHECK (s.params[(std::size_t) facet::autoRevisionParam] == 0.0f && savedEffectParamCount (s) <= (std::size_t) facet::paramCount);
    // a translated Pro-Q 4 band with Auto gets the revision
    std::vector<float> v (600, 0.0f);
    v[0] = 1; v[1] = 1; v[2] = (float) std::log2 (1000.0); v[3] = 6.0f; v[4] = 0.5f; v[6] = 2; v[7] = 2;
    v[9] = -30.0f; v[10] = 1.0f; v[11] = 1.0f; v[13] = 50; v[14] = 50;
    v[555] = 1.0f;
    int n = 0;
    const auto t = m124x::substitute ("FabFilter Pro-Q 4", m124x::ffbs (v), n);
    CHECK (facet::autoThreshold (t.params, 0) && t.params[(std::size_t) facet::autoRevisionParam] == 1.0f);
    // saved before this round: back to round 190js's law
    auto old = t;
    upgradeLoadedEffectParams (old, (std::size_t) facet::autoRevisionParam);
    CHECK (old.params[(std::size_t) facet::autoRevisionParam] == 0.0f);
    auto now = t;
    upgradeLoadedEffectParams (now, (std::size_t) facet::autoRevisionParam + 1);
    CHECK (now.params[(std::size_t) facet::autoRevisionParam] == 1.0f);
}

TEST (parity190lg_facet_auto_revision1_timing)
{
    // the reference on refcap030's time-constant probe (Dynamic Range −30, 1 kHz): the whole range within 100 ms of a steady
    // tone, then 34 dB under it about 5 dB of cut stays (inside the 1.53 × range knee) and the band lets most of it go within
    // ~0.6 s (−9.8 / −3.3 / −0.5 dB of the 24 dB back after 0.2 / 0.4 / 0.6 s)
    using namespace parity190lgq4;
    const double full = gainAt (-30.0, 1.0f, -6.0, 0.15, 0.25), after = gainAt (-30.0, 1.0f, -6.0, 1.9, 2.1);
    const double early = gainAt (-30.0, 1.0f, -6.0, 1.15, 1.25), rest15 = gainAt (-15.0, 1.0f, -6.0, 1.9, 2.1);
    std::printf ("  facet auto revision 1: %.2f dB in the tone, %.2f dB 0.2 s after it, %.2f dB 1 s after (range -15: %.2f)\n", full, early, after, rest15);
    CHECK (std::abs (full + 30.0) < 1.0);
    CHECK (early > -24.0 && early < -6.0);
    CHECK (after < -3.0 && after > -8.0);
    CHECK (rest15 > -1.0);
}
