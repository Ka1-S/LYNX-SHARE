// Round 190lg: Crest's Allround under revision 1 runs Pro-C 3 Versatile's own graph (static REA, docs/analise/
// REA_rodada190lg.md): two gain computers on two readings of the side chain's level (slow: 0.979 × the level + 0.0203 ×
// its 204 Hz low-pass, then the knob-timed follower; fast: 0.91 × its 6.39 kHz low-pass, then a slew limiter), the
// reduction share max (slow, w · fast). Revision 0 keeps the fitted Allround (tests/Parity190kwGold.inc).
TEST (parity190lg_crest_versatile_static_law_and_editor_curve)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    const auto r4 = cr::ratioValue (4.0);
    const auto all = cr::styleValue (cr::Style::allround);
    // threshold −30 dB, 4 : 1, hard knee, a steady −10 dBFS square: the slow path reads 0.9998 × the level (its low-pass
    // of a constant magnitude is the magnitude) → 20 · 3 / 4 = 15 dB; the fast path (−0.82 dB, w ≈ 0.93) stays under it
    const auto settled = parity190kw::settledGainDb (parity190kw::crest (all, 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, 1.0f), -10.0, 6.0);
    std::printf ("  Versatile settled on a −10 dBFS square: %.2f dB\n", settled);
    CHECK (parity190kw::near (settled, -15.0, 0.15));
    // the editor's curve is the law (a sine: the slow path's low-pass reads its mean, 2 / π of the peak)
    const auto curve = cr::staticGainDb (parity190kw::crest (all, 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, 1.0f).params, -10.0);
    CHECK (parity190kw::near (curve, -15.0 + 0.75 * -20.0 * std::log10 (0.979424 + 0.020348 * 2.0 / 3.141592653589793), 0.01));
    // revision 0 keeps the fitted Allround curve
    CHECK (! parity190kw::near (cr::staticGainDb (parity190kw::crest (all, 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, 0.0f).params, -10.0), curve, 0.01));
}

TEST (parity190lg_crest_versatile_translated_state_runs_it)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // a Pro-C 3 state (FFBS v1, 100 values) with style 1 = Versatile translates to Allround with the revision on
    fabfilter::State st;
    st.named = false;
    st.version = 1;
    st.count = 100;
    st.values.assign (100, 0.0f);
    st.values[0] = 1.0f;
    st.values[1] = -18.0f;
    st.values[4] = 0.6f;
    st.values[5] = 18.0f;
    st.values[6] = 60.0f;
    st.values[7] = 0.4f;
    st.values[8] = 0.5f;
    st.values[26] = 0.5f;
    auto t = makeEffectSlot (EffectKind::crestCompressor);
    CHECK (fabfilter::fromProC3 (st, t) > 0);
    CHECK (cr::styleOf (t.params) == cr::Style::allround);
    CHECK (t.params[(std::size_t) cr::revision] == 1.0f);
}
