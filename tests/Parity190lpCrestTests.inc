// Round 190lp (part crest): Crest's Smooth and Op-El voicings under revision 1 run Pro-C 3's own graphs for those styles
// (static REA, docs/analise/REA_rodada190lp.md): Smooth = a gain computer on a side chain already turned down by its own
// copy (two followers each, the reduction share max (A · C0, C1)); Op-El = two gain computers, their squared shares
// followed by a fast, a slower and a very slow follower, summed in power. Revision 0 keeps the fitted laws (the
// revision-0 fingerprints below are the base's).
namespace parity190lp
{
namespace cr = lynx::dyn::crest;
inline lynx::EffectSlot crest (cr::Style st, float thr, float ratio, float knee, float rev)
{
    return parity190kw::crest (cr::styleValue (st), cr::voicingValue (st), thr, ratio, knee, 0.3f, 0.3f, rev);
}
inline double shareOf (double grDb) { return 1.0 - std::pow (10.0, -grDb / 20.0); }
inline double grOf (double share) { return -20.0 * std::log10 (1.0 - share); }
} // namespace parity190lp

TEST (parity190lp_crest_smooth_static_law_and_editor_curve)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // threshold −30 dB, 4 : 1, hard knee, a steady −10 dBFS square (a constant level: every follower settles)
    const auto r4 = cr::ratioValue (4.0);
    const auto s = parity190lp::crest (cr::Style::smooth, 0.5f, r4, 0.0f, 1.0f);
    const auto settled = parity190kw::settledGainDb (s, -10.0, 6.0);
    const auto curve = cr::staticGainDb (s.params, -10.0);
    std::printf ("  Smooth settled on a −10 dBFS square: %.3f dB, editor curve %.3f dB\n", settled, curve);
    CHECK (parity190kw::near (settled, curve, 0.05));
    // the law by hand: E (threshold + 0.215 dB) turns the side chain down by 0.769 × its share; D (threshold × the ratio
    // factor, 1.108 near 4 : 1) reads that; the reduction is max (A · C, C), A = 1.0081 − 0.0123 · 0.610 · C · E
    const auto e = parity190lp::shareOf ((-10.0 - (-30.0 + 0.214906)) * 0.75);
    const auto lev = -10.0 + 20.0 * std::log10 (1.0 - 0.768633 * e);
    const auto c = parity190lp::shareOf (std::max (0.0, lev - (-30.0 * 1.1084)) * 0.75);
    const auto r = (1.008093 - 0.012346 * 0.610426 * c * e) * c;
    CHECK (parity190kw::near (curve, -parity190lp::grOf (r), 0.02));
    // softer than the plain 4 : 1 curve (15 dB here): the main detector reads an already reduced side chain
    CHECK (-curve < 14.0 && -curve > 8.0);
    // revision 0 keeps the fitted Smooth curve
    CHECK (! parity190kw::near (cr::staticGainDb (parity190lp::crest (cr::Style::smooth, 0.5f, r4, 0.0f, 0.0f).params, -10.0), curve, 0.01));
}

TEST (parity190lp_crest_opel_static_law_and_editor_curve)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // threshold −30 dB, 4 : 1, knee 18 dB, a steady −10 dBFS square: D1 (the knobs) and D2 (knee 30, range 60) both read
    // 20 dB over → 15 dB; the shares' power sum √(0.95 s1² + 0.05 s2²) = the same share → 15 dB
    const auto r4 = cr::ratioValue (4.0);
    const auto s = parity190lp::crest (cr::Style::opEl, 0.5f, r4, 0.25f, 1.0f);
    const auto settled = parity190kw::settledGainDb (s, -10.0, 8.0);
    const auto curve = cr::staticGainDb (s.params, -10.0);
    std::printf ("  Op-El settled on a −10 dBFS square: %.3f dB, editor curve %.3f dB\n", settled, curve);
    CHECK (parity190kw::near (curve, -15.0, 0.02));
    CHECK (parity190kw::near (settled, curve, 0.1));
    // inside D2's 30 dB knee (8 dB over at −22 dBFS) the two detectors differ: the power sum sits between them
    const auto near8 = -cr::staticGainDb (s.params, -22.0);
    const auto s1 = parity190lp::shareOf (std::pow (18.0 / 2.0 + 8.0, 2.0) / (2.0 * 18.0) * 0.75);
    const auto s2 = parity190lp::shareOf (std::pow (30.0 / 2.0 + 8.0, 2.0) / (2.0 * 30.0) * 0.75);
    CHECK (parity190kw::near (near8, parity190lp::grOf (std::sqrt (0.95 * s1 * s1 + 0.05 * s2 * s2)), 0.02));
    // revision 0 keeps the fitted Op-El curve (equal at 20 dB over, apart inside the knees)
    const auto old = parity190lp::crest (cr::Style::opEl, 0.5f, r4, 0.25f, 0.0f);
    bool differs = false;
    for (double in : { -22.0, -26.0, -30.0, -34.0 })
        differs = differs || ! parity190kw::near (cr::staticGainDb (old.params, in), cr::staticGainDb (s.params, in), 0.01);
    CHECK (differs);
}

TEST (parity190lp_crest_translated_states_run_the_new_laws)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // Pro-C 3 states (FFBS v1, 100 values): style 2 Smooth, 6 Op-El → the matching Crest voicing, revision 1
    const std::pair<int, cr::Style> map[] { { 2, cr::Style::smooth }, { 6, cr::Style::opEl } };
    for (const auto& [theirs, ours] : map)
    {
        fabfilter::State st;
        st.named = false;
        st.version = 1;
        st.count = 100;
        st.values.assign (100, 0.0f);
        st.values[0] = (float) theirs;
        st.values[1] = -18.0f;
        st.values[4] = 0.6f;
        st.values[5] = 18.0f;
        st.values[6] = 60.0f;
        st.values[7] = 0.4f;
        st.values[8] = 0.5f;
        st.values[26] = 0.5f;
        auto t = makeEffectSlot (EffectKind::crestCompressor);
        t.params[(std::size_t) cr::revision] = 0.0f;
        CHECK (fabfilter::fromProC3 (st, t) > 0);
        CHECK (cr::styleOf (t.params) == ours);
        CHECK (t.params[(std::size_t) cr::revision] == 1.0f);
    }
}

TEST (parity190lp_crest_fingerprints_revision_0_unchanged_revision_1_pinned)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    struct Case { float rev; cr::Style st; float threshold, ratio, knee, attack, release; double sr; std::uint64_t hash; };
    static const Case cases[] {
#include "Parity190lpCrestGold.inc"
    };
    for (const auto& c : cases)
    {
        auto s = parity190kw::crest (cr::styleValue (c.st), cr::voicingValue (c.st), c.threshold, c.ratio, c.knee, c.attack, c.release, c.rev);
        std::vector<float> l ((std::size_t) (c.sr * 2.0)), r (l.size());
        parity190kw::signal (l, r, c.sr);
        parity190kw::render (s, c.sr, l, r);
        const auto h = parity190kw::fnv (l, r);
        if (h != c.hash)
            std::printf ("  fingerprint rev %.0f style %d: 0x%016llxull\n", c.rev, (int) c.st, (unsigned long long) h);
        CHECK (h == c.hash);
    }
}
