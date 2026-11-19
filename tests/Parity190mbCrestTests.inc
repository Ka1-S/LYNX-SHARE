// Round 190mb (part crest): Crest's Upward and Vari-mu under revision 1 run Pro-C 3's own Upward graph and Vari-Mu tube
// node (static REA, docs/analise/REA_rodada190mb.md): Upward = an expansion gain computer (one-sided knee) on a fast
// level, its lift L − follower (L − E), less the measured cut (1 − 1/ratio) · √a · (threshold + the upward range);
// Vari-mu = a push-pull tube stage whose bias is a follower of its own output over the threshold (feedback). Revision 0
// keeps the fitted laws (the revision-0 fingerprints below are the base's).
namespace parity190mb
{
namespace cr = lynx::dyn::crest;
inline lynx::EffectSlot crest (cr::Style st, float thr, float ratio, float knee, float rev, float att = 0.3f)
{
    auto s = parity190kw::crest (cr::styleValue (st), cr::voicingValue (st), thr, ratio, knee, att, 0.3f, rev);
    s.params[(std::size_t) cr::range] = 1.0f; // 60 dB
    return s;
}
// LYNX's own tube curve (the law's quintic, written out again here) and the stage's gain for a swing d around a bias b
inline double tube (double u)
{
    const auto c = std::clamp (u, -11.4, 18.03);
    return 0.5 * u + 0.5 * (c + c * c * (-2.78613777e-02 + c * (-1.52802139e-03 + c * (1.27100459e-04 + c * -2.92540601e-06))));
}
inline double stage (double b, double d) { return (std::tanh (tube (b + d)) - std::tanh (tube (b - d))) / (2.0 * d); }
} // namespace parity190mb

TEST (parity190mb_crest_upward_static_law_and_editor_curve)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // threshold −30 dB, 4 : 1, knee 18 dB, range 60 (the upward range: 36 dB)
    const auto r4 = cr::ratioValue (4.0);
    const auto s = parity190mb::crest (cr::Style::upward, 0.5f, r4, 0.25f, 1.0f);
    const auto a = std::cbrt ((cr::attackMs (0.3f) - 0.005) / 250.0);
    const auto cut = 0.75 * std::sqrt (a) * (-30.0 + 36.0);
    // a −50 dBFS square: 20 dB under the threshold, past the 18 dB one-sided knee: (20 − 9) · 0.75 dB up, less the cut
    const auto quiet = cr::staticGainDb (s.params, -50.0);
    const auto settledQuiet = parity190kw::settledGainDb (s, -50.0, 6.0);
    std::printf ("  Upward on a −50 dBFS square: settled %.3f dB, editor curve %.3f dB (cut %.3f)\n", settledQuiet, quiet, cut);
    CHECK (parity190kw::near (quiet, 11.0 * 0.75 - cut, 0.01));
    CHECK (parity190kw::near (settledQuiet, quiet, 0.05));
    // inside the knee (12 dB under): (12 / 18)² · 9 · 0.75
    CHECK (parity190kw::near (cr::staticGainDb (s.params, -42.0), std::pow (12.0 / 18.0, 2.0) * 9.0 * 0.75 - cut, 0.01));
    // loud signals: no lift, only the cut, whatever the level
    const auto loud = parity190kw::settledGainDb (s, -10.0, 6.0);
    CHECK (parity190kw::near (loud, -cut, 0.05));
    CHECK (parity190kw::near (cr::staticGainDb (s.params, -3.0), -cut, 0.001));
    // silence: at most the upward range (36 dB), less the cut
    CHECK (parity190kw::near (cr::staticGainDb (s.params, -150.0), 36.0 - cut, 0.01));
    // a lower threshold (−48 dB) has no cut; a slower attack a larger one
    CHECK (parity190kw::near (cr::staticGainDb (parity190mb::crest (cr::Style::upward, 0.2f, r4, 0.25f, 1.0f).params, -10.0), 0.0, 0.001));
    CHECK (cr::staticGainDb (parity190mb::crest (cr::Style::upward, 0.5f, r4, 0.25f, 1.0f, 0.6f).params, -10.0) < -cut - 0.1);
    // revision 0 keeps the fitted upward curve
    CHECK (! parity190kw::near (cr::staticGainDb (parity190mb::crest (cr::Style::upward, 0.5f, r4, 0.25f, 0.0f).params, -50.0), quiet, 0.05));
}

TEST (parity190mb_crest_varimu_static_law_and_editor_curve)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    const auto curve = [] (double ratio, float knee, double in, float rev = 1.0f) {
        return cr::staticGainDb (parity190mb::crest (cr::Style::variMu, 0.5f, cr::ratioValue (ratio), knee, rev).params, in);
    };
    // threshold −30 dB, 4 : 1, hard knee, a steady −10 dBFS square: the loop settles; the editor curve is that state
    const auto s = parity190mb::crest (cr::Style::variMu, 0.5f, cr::ratioValue (4.0), 0.0f, 1.0f);
    const auto settled = parity190kw::settledGainDb (s, -10.0, 6.0);
    const auto g = curve (4.0, 0.0f, -10.0);
    std::printf ("  Vari-mu settled on a −10 dBFS square: %.3f dB, editor curve %.3f dB\n", settled, g);
    CHECK (parity190kw::near (settled, g, 0.1));
    CHECK (g < -3.0 && g > -20.0);
    // the law by hand: the output y = A · gain sits over the threshold by f, the bias is (r' − 1) f, r' = 14 at 4 : 1 with
    // a hard knee, and the stage's gain at that bias (a swing of half the input) is the curve's gain
    const auto A = std::pow (10.0, -0.5), gain = std::pow (10.0, g / 20.0);
    const auto f = A * gain - std::pow (10.0, -1.5);
    CHECK (parity190kw::near (20.0 * std::log10 (parity190mb::stage (13.0 * f, 0.5011872336272722 * A)), g, 0.05));
    // under the threshold: no reduction (the stage alone bends a −50 dBFS signal by nothing measurable)
    CHECK (parity190kw::near (curve (4.0, 0.0f, -50.0), 0.0, 0.01));
    // more ratio, more reduction; a wider knee shrinks the loop gain (1 − 0.5 √(knee / 60)): less reduction
    CHECK (curve (10.0, 0.0f, -10.0) < curve (4.0, 0.0f, -10.0) - 0.5);
    CHECK (curve (2.0, 0.0f, -10.0) > curve (4.0, 0.0f, -10.0) + 0.5);
    CHECK (curve (4.0, 1.0f, -10.0) > curve (4.0, 0.0f, -10.0) + 0.5);
    // revision 0 keeps the measured vari-mu curve
    CHECK (! parity190kw::near (curve (4.0, 0.0f, -10.0, 0.0f), g, 0.05));
}

TEST (parity190mb_crest_translated_states_run_the_new_laws)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // Pro-C 3 states (FFBS v1, 100 values): style 4 Upward, 7 Vari-Mu → revision 1 (5 TTM → Two-way keeps its measured law)
    const std::pair<int, cr::Style> map[] { { 4, cr::Style::upward }, { 7, cr::Style::variMu }, { 5, cr::Style::twoWay } };
    for (const auto& [theirs, ours] : map)
    {
        fabfilter::State st;
        st.named = false;
        st.version = 1;
        st.count = 100;
        st.values.assign (100, 0.0f);
        st.values[0] = (float) theirs;
        st.values[1] = -24.0f;
        st.values[4] = 0.6f;
        st.values[5] = 18.0f;
        st.values[6] = 60.0f;
        st.values[7] = 0.342f;
        st.values[8] = 0.278f;
        st.values[26] = 0.5f;
        auto t = makeEffectSlot (EffectKind::crestCompressor);
        t.params[(std::size_t) cr::revision] = 0.0f;
        CHECK (fabfilter::fromProC3 (st, t) > 0);
        CHECK (cr::styleOf (t.params) == ours);
        CHECK (t.params[(std::size_t) cr::revision] == 1.0f);
        if (ours == cr::Style::upward) // the reference's measured static curve at this setting: −5.26 dB on loud signals
            CHECK (parity190kw::near (cr::staticGainDb (t.params, -6.0), -5.26, 0.05));
    }
}

TEST (parity190mb_crest_fingerprints_revision_0_unchanged_revision_1_pinned)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    struct Case { float rev; cr::Style st; float threshold, ratio, knee, attack, release; double sr; std::uint64_t hash; };
    static const Case cases[] {
#include "Parity190mbCrestGold.inc"
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
