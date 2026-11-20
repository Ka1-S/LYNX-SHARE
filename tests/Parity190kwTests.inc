// Round 190kw: static REA of the references (docs/analise/REA_rodada190kw.md; lab /mnt/e/lynx-work/rea190kw/).
// - Crest Compressor's revision 1 (hidden `crest::revision`): the reference's own laws for Clean / Classic / Opto (the
//   reduction smoothed by each style's two-stage follower, its Bézier knee shape, its attack / release scaling) and for
//   Vocal / Mastering / Bus / Punch / Pumping (the generic gain computer + followers, wired as the reference wires them).
//   A new Crest and a translated Pro-C 3 state run it; a project or preset saved before keeps 0 = the base, bit for bit
//   (fingerprints: tests/Parity190kwGold.inc; the base commit's fxrender renders the same paths byte-identically, gold/gold.sh).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/DynamicsSuite.h"
#include "core/Effects.h"
#include "core/FabFilterStates.h"

namespace parity190kw
{
namespace cr = lynx::dyn::crest;
// the gold input (as gold/gold.cpp): a 220 Hz tone + noise in 0.3 s steps of −40 … −5 dBFS, right at 0.8 × left
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 4242u;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        const double t = (double) i / sr;
        s = s * 1664525u + 1013904223u;
        const double nz = (double) (s >> 8) / 8388608.0 - 1.0;
        const int k = (int) (t / 0.3);
        const double lvl = std::pow (10.0, (-40.0 + 7.0 * (k % 6)) / 20.0);
        const double v = lvl * (0.7 * std::sin (2.0 * M_PI * 220.0 * t) + 0.3 * nz);
        l[i] = (float) v;
        r[i] = (float) (0.8 * v);
    }
}
inline std::uint64_t fnv (const std::vector<float>& a, const std::vector<float>& b)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const auto* v : { &a, &b })
        for (float x : *v)
        {
            std::uint32_t u;
            std::memcpy (&u, &x, 4);
            for (int k = 0; k < 4; ++k)
            {
                h ^= (u >> (8 * k)) & 0xffu;
                h *= 1099511628211ull;
            }
        }
    return h;
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
}
inline lynx::EffectSlot crest (float style, float voicing, float thr, float ratio, float knee, float att, float rel, float rev)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::crestCompressor);
    s.params[(std::size_t) cr::style] = style;
    s.params[(std::size_t) cr::voicing] = voicing;
    s.params[(std::size_t) cr::threshold] = thr;
    s.params[(std::size_t) cr::ratio] = ratio;
    s.params[(std::size_t) cr::knee] = knee;
    s.params[(std::size_t) cr::attack] = att;
    s.params[(std::size_t) cr::release] = rel;
    s.params[(std::size_t) cr::revision] = rev;
    return s;
}
// the settled gain (dB) on a square wave (|x| constant, so the detector's level is steady) of `inDb` dBFS
inline double settledGainDb (lynx::EffectSlot s, double inDb, double seconds = 4.0)
{
    s.params[(std::size_t) cr::autoGain] = 0.0f;
    const double sr = 48000.0;
    std::vector<float> l ((std::size_t) (sr * seconds)), r;
    const auto a = (float) std::pow (10.0, inDb / 20.0);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = (i / 24) % 2 == 0 ? a : -a;
    r = l;
    render (s, sr, l, r);
    double peak = 0.0;
    for (std::size_t i = l.size() - 4800; i < l.size(); ++i)
        peak = std::max (peak, (double) std::abs (l[i]));
    return 20.0 * std::log10 (peak) - inDb;
}
inline bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }
} // namespace parity190kw

TEST (parity190kw_crest_revision_is_hidden_new_and_translated_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    const auto& info = effectInfo (EffectKind::crestCompressor);
    // hidden: no control, no string (rollup step 8: after 190kt's busLaw and 190ky's fetLaw)
    CHECK (info.params.size() == (std::size_t) cr::count && cr::revision == cr::fetLaw + 1 && cr::fetLaw == cr::busLaw + 1);
    auto s = makeEffectSlot (EffectKind::crestCompressor);
    CHECK (s.params[(std::size_t) cr::revision] == 1.0f && savedEffectParamCount (s) == (std::size_t) cr::revision + 1);
    auto old = s;
    upgradeLoadedEffectParams (old, (std::size_t) cr::revision); // saved before this round: the fitted laws
    CHECK (old.params[(std::size_t) cr::revision] == 0.0f);
    auto now = s;
    upgradeLoadedEffectParams (now, (std::size_t) cr::revision + 1);
    CHECK (now.params[(std::size_t) cr::revision] == 1.0f);
    // a Pro-C 3 state (FFBS v1, 100 values; style 0 = Clean) translates with the revision on
    fabfilter::State st;
    st.named = false;
    st.version = 1;
    st.count = 100;
    st.values.assign (100, 0.0f);
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
    CHECK (t.params[(std::size_t) cr::revision] == 1.0f);
}

TEST (parity190kw_crest_fingerprints_old_projects_bit_identical_revision_1_pinned)
{
    struct Case { float rev, style, voicing, threshold, ratio, knee, attack, release; double sr; std::uint64_t hash; };
    static const Case cases[] {
#include "Parity190kwGold.inc"
    };
    for (const auto& c : cases)
    {
        std::vector<float> l ((std::size_t) (c.sr * 2.0)), r (l.size());
        parity190kw::signal (l, r, c.sr);
        parity190kw::render (parity190kw::crest (c.style, c.voicing, c.threshold, c.ratio, c.knee, c.attack, c.release, c.rev), c.sr, l, r);
        CHECK (parity190kw::fnv (l, r) == c.hash);
    }
}

TEST (parity190kw_crest_v1_and_v2_static_laws)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // threshold −30 dB (0.5), ratio 4, hard knee, range 60, a steady −10 dBFS square: overshoot 20 dB
    const auto r4 = cr::ratioValue (4.0);
    // Clean, Opto, Punch, Bus: the plain curve, 20 · (1 − 1/4) = 15 dB down
    for (float style : { cr::styleValue (cr::Style::clean), cr::styleValue (cr::Style::opto), cr::styleValue (cr::Style::punch),
                         cr::styleValue (cr::Style::bus) })
        CHECK (parity190kw::near (parity190kw::settledGainDb (parity190kw::crest (style, 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, 1.0f), -10.0), -15.0, 0.1));
    // Classic: the feedback design whose reduction is the sum of its two stages: 2 · 20 · 3 / 7 = 17.14 dB
    CHECK (parity190kw::near (parity190kw::settledGainDb (parity190kw::crest (cr::styleValue (cr::Style::classic), 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, 1.0f), -10.0, 8.0),
                -2.0 * 20.0 * 3.0 / 7.0, 0.15));
    // Vocal: its own curve, no ratio: 20 log10 (1 + |x| (10^(30/20) − 1)) at |x| = −10 dBFS = 20.4 dB
    CHECK (parity190kw::near (parity190kw::settledGainDb (parity190kw::crest (cr::styleValue (cr::Style::vocal), 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, 1.0f), -10.0, 8.0),
                -20.0 * std::log10 (1.0 + std::pow (10.0, -0.5) * (std::pow (10.0, 1.5) - 1.0)), 0.15));
    // revision 0 keeps the fitted Clean (its own knee offsets: not the plain 15 dB)
    CHECK (std::abs (parity190kw::settledGainDb (parity190kw::crest (cr::styleValue (cr::Style::clean), 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, 0.0f), -10.0) + 15.0) > 0.2);
}

TEST (parity190kw_crest_editor_curve_follows_the_revision)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    const auto r4 = cr::ratioValue (4.0);
    const auto curve = [&] (cr::Style st, float rev, double in) {
        return cr::staticGainDb (parity190kw::crest (cr::styleValue (st), 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, rev).params, in);
    };
    // the editor's static curve is the law that runs: −10 dBFS against −30 dB, 4 : 1, hard knee
    for (auto st : { cr::Style::clean, cr::Style::opto, cr::Style::punch, cr::Style::bus, cr::Style::pumping })
        CHECK (parity190kw::near (curve (st, 1.0f, -10.0), -15.0, 0.01));
    CHECK (parity190kw::near (curve (cr::Style::classic, 1.0f, -10.0), -2.0 * 20.0 * 3.0 / 7.0, 0.01));
    CHECK (parity190kw::near (curve (cr::Style::vocal, 1.0f, -10.0),
                              -20.0 * std::log10 (1.0 + std::pow (10.0, -0.5) * (std::pow (10.0, 1.5) - 1.0)), 0.01));
    // and the settled render agrees with it
    CHECK (parity190kw::near (parity190kw::settledGainDb (parity190kw::crest (cr::styleValue (cr::Style::classic), 0.0f, 0.5f, r4, 0.0f, 0.3f, 0.3f, 1.0f), -10.0, 8.0),
                              curve (cr::Style::classic, 1.0f, -10.0), 0.15));
    // revision 0 keeps the fitted curve (unchanged for projects saved before)
    CHECK (! parity190kw::near (curve (cr::Style::clean, 0.0f, -10.0), -15.0, 0.2));
}
