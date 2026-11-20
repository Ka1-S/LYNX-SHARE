// Round 190mj (part crest): Crest's Two-way under revision 1 runs Pro-C 3's own TTM band processors (static REA,
// docs/analise/REA_rodada190mj.md): per band (mid / low / high) an RMS-like level (one follower on the squared magnitude),
// a downward gain computer (threshold + 3 / + 0 / − 2 dB, ratio 2/3 / 2/3 / 1 of the knob's) and an upward one (threshold −
// 7 dB + half the knee, ratio 1 + 3.17 √((ratio − 1) / 99), within 0.6 × the range), the gain = lift − reduction − the
// measured cut 0.6 (1 − 1 / ratio) √a (threshold + 0.6 × the range); the reference's own split frequencies (88.17 Hz /
// 2.5 kHz). Revision 0 keeps the fitted law (the revision-0 fingerprints below are the base's).
namespace parity190mj
{
namespace cr = lynx::dyn::crest;
inline lynx::EffectSlot crest (float thr, float ratio, float knee, float rev, float att, float rel, float range = 1.0f)
{
    auto s = parity190kw::crest (cr::styleValue (cr::Style::twoWay), cr::voicingValue (cr::Style::twoWay), thr, ratio, knee, att, rel, rev);
    s.params[(std::size_t) cr::range] = range;
    s.params[(std::size_t) cr::autoGain] = 0.0f;
    return s;
}
// one band's law by hand (b: 0 mid, 1 low, 2 high): the downward computer (centred quadratic knee; threshold + 3 / + 0 / − 2 dB,
// ratio 2/3 / 2/3 / 1 of the knob's) and the upward one (one-sided knee), plus the measured offset
inline double bandLaw (int b, double level, double thr, double ratio, double knee, double range, double offset)
{
    const double dOff[3] { 3.0, 0.0, -2.0 }, rScale[3] { 0.6667, 0.6667, 1.0 };
    const auto t1 = thr + dOff[b], r1 = std::max (1.0, ratio * rScale[b]), t2 = thr - 7.0 + 0.5 * knee;
    auto over = level - t1;
    if (knee > 0.0 && over >= -0.5 * knee && over <= 0.5 * knee)
        over = (0.5 * knee + over) / knee * ((0.5 * knee + over) / knee) * (0.5 * knee);
    const auto down = std::clamp (over * (r1 < 100.0 ? 1.0 - 1.0 / r1 : 1.0), 0.0, range);
    const auto under = t2 - level;
    auto up = under <= 0.0 ? 0.0 : (knee > 0.0 && under <= knee ? under / knee * (under / knee) * (0.5 * knee) : under - 0.5 * knee);
    up = std::clamp (up * (1.0 - 1.0 / (1.0 + 3.17 * std::sqrt ((ratio - 1.0) / 99.0))), 0.0, 0.6 * std::min (range, 60.0));
    auto g = up - down + offset;
    if (g > 0.0) // issue #77's fade: no lift under −100 dBFS, the full lift from −80
        g *= std::clamp ((level + 100.0) / 20.0, 0.0, 1.0);
    return g;
}
inline double midLaw (double level, double thr, double ratio, double knee, double range, double offset)
{
    return bandLaw (0, level, thr, ratio, knee, range, offset);
}
// a steady sine's gain by hand: each band's share of it (fourth-order Linkwitz-Riley at 88.167 Hz / 2.5 kHz: in phase, the
// shares sum to 1) at that band's RMS level through that band's law; `offsets`: the measured offsets (mid, low, high)
inline double threeBandDb (double hz, double rmsDb, double thr, double ratio, double knee, double range, const double (&offsets)[3])
{
    const auto lp = [] (double f, double fc) { return 1.0 / (1.0 + std::pow (f / fc, 4.0)); };
    const double share[3] { (1.0 - lp (hz, 88.167)) * lp (hz, 2500.0), lp (hz, 88.167), (1.0 - lp (hz, 88.167)) * (1.0 - lp (hz, 2500.0)) };
    double sum = 0.0;
    for (int b = 0; b < 3; ++b)
        sum += share[b] * std::pow (10.0, bandLaw (b, rmsDb + 20.0 * std::log10 (share[b]), thr, ratio, knee, range, offsets[b]) / 20.0);
    return 20.0 * std::log10 (sum);
}
// a steady sine of `rmsDb` dBFS RMS at `hz` through a slot: the output's RMS gain (dB) over the last 0.5 s
inline double sineGainDb (const lynx::EffectSlot& s, double hz, double rmsDb, double seconds = 3.0)
{
    const double sr = 48000.0;
    std::vector<float> l ((std::size_t) (sr * seconds)), r;
    const auto a = std::pow (10.0, rmsDb / 20.0) * std::sqrt (2.0);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = (float) (a * std::sin (2.0 * M_PI * hz * (double) i / sr));
    r = l;
    auto x = l;
    parity190kw::render (s, sr, l, r);
    double ey = 0.0, ex = 0.0;
    for (std::size_t i = l.size() - 24000; i < l.size(); ++i)
    {
        ey += (double) l[i] * l[i];
        ex += (double) x[i] * x[i];
    }
    return 10.0 * std::log10 (ey / ex);
}
} // namespace parity190mj

TEST (parity190mj_crest_ttm_static_law_by_hand)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // threshold −40 dB (no cut under −36 dB), 4 : 1, knee 18 dB, range 60
    const auto s = parity190mj::crest (1.0f / 3.0f, cr::ratioValue (4.0), 0.25f, 1.0f, 0.3f, 0.5f);
    for (const double in : { -80.0, -60.0, -50.0, -45.0, -40.0, -35.0, -30.0, -20.0, -6.0 })
    {
        const auto want = parity190mj::midLaw (in, -40.0, 4.0, 18.0, 60.0, 0.0);
        const auto got = cr::staticGainDb (s.params, in);
        if (! parity190kw::near (got, want, 0.01))
            std::printf ("  TTM static at %.0f dBFS: %.3f dB, by hand %.3f dB\n", in, got, want);
        CHECK (parity190kw::near (got, want, 0.01));
    }
    // above −36 dB the measured cut, 0.6 (1 − 1 / ratio) √a (threshold + 36), the same at every level (threshold −24 dB)
    const auto s24 = parity190mj::crest (0.6f, cr::ratioValue (4.0), 0.25f, 1.0f, 0.3f, 0.5f);
    const auto cut = 0.6 * 0.75 * std::sqrt (std::cbrt ((cr::attackMs (0.3f) - 0.005) / 250.0)) * 12.0;
    for (const double in : { -60.0, -24.0, -6.0 })
        CHECK (parity190kw::near (cr::staticGainDb (s24.params, in), parity190mj::midLaw (in, -24.0, 4.0, 18.0, 60.0, -cut), 0.01));
    // the upward part's ceiling: 0.6 × the range (a 10 dB range lifts by 6 dB at most), faded under −80 dBFS (issue #77)
    const auto r100 = parity190mj::crest (1.0f / 3.0f, cr::ratioValue (100.0), 0.0f, 1.0f, 0.3f, 0.5f, 1.0f / 6.0f);
    CHECK (parity190kw::near (cr::staticGainDb (r100.params, -75.0), 6.0, 0.01));
    CHECK (parity190kw::near (cr::staticGainDb (r100.params, -85.0), 6.0 * 0.75, 0.01));
    CHECK (parity190kw::near (cr::staticGainDb (r100.params, -150.0), 0.0, 0.01));
    // revision 0 keeps the fitted two-way curve (the new law differs from it somewhere along the way)
    double most = 0.0;
    for (const double in : { -80.0, -60.0, -40.0, -20.0, -6.0 })
        most = std::max (most, std::abs (cr::staticGainDb (parity190mj::crest (1.0f / 3.0f, cr::ratioValue (4.0), 0.25f, 0.0f, 0.3f, 0.5f).params, in)
                                         - cr::staticGainDb (s.params, in)));
    CHECK (most > 0.1);
}

TEST (parity190mj_crest_ttm_bands_settle_on_the_law)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // equal attack and release (50 ms): the follower settles on the sine's mean power, so the band reads its RMS level.
    // A 10 dB range keeps the other bands' lift (≤ 6 dB on their small share of the tone) out of the way
    const auto att = cr::attackValue (50.0), rel = cr::releaseValue (50.0);
    const auto s = parity190mj::crest (1.0f / 3.0f, cr::ratioValue (4.0), 0.0f, 1.0f, att, rel, 1.0f / 6.0f);
    // (no cut under −36 dB)
    const double offsets[3] { 0.0, 0.0, 0.0 };
    // 600 Hz (mostly the mid band) and 8 kHz (mostly the high band: its downward computer starts 5 dB lower than the mid
    // band's and runs the knob's full ratio): loud tones reduced, quiet ones lifted
    for (const double hz : { 600.0, 8000.0 })
        for (const double in : { -10.0, -30.0, -55.0 })
        {
            const auto want = parity190mj::threeBandDb (hz, in, -40.0, 4.0, 0.0, 10.0, offsets);
            const auto got = parity190mj::sineGainDb (s, hz, in);
            std::printf ("  TTM %.0f Hz at %.0f dB RMS: %+.3f dB, the bands' laws %+.3f dB\n", hz, in, got, want);
            CHECK (parity190kw::near (got, want, 0.1));
        }
    CHECK (parity190mj::sineGainDb (s, 8000.0, -30.0) < parity190mj::sineGainDb (s, 600.0, -30.0) - 3.0);
}

TEST (parity190mj_crest_translated_ttm_state_runs_the_new_law)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // a Pro-C 3 TTM state (FFBS v1, style 5): threshold −24 dB, 4 : 1, knee 0, range 60, attack 10 ms, release 200 ms
    fabfilter::State st;
    st.named = false;
    st.version = 1;
    st.count = 100;
    st.values.assign (100, 0.0f);
    st.values[0] = 5.0f;
    st.values[1] = -24.0f;
    st.values[4] = 0.6f;
    st.values[5] = 0.0f;
    st.values[6] = 60.0f;
    st.values[7] = 0.342f;
    st.values[8] = 0.278f;
    st.values[26] = 0.5f;
    st.values[13] = 1.0f;  // the character stage after the gain (as captured)
    st.values[17] = -1.0f; // no dry level
    st.values[88] = 1.0f;  // mix 100 %
    auto t = makeEffectSlot (EffectKind::crestCompressor);
    t.params[(std::size_t) cr::revision] = 0.0f;
    CHECK (fabfilter::fromProC3 (st, t) > 0);
    CHECK (cr::styleOf (t.params) == cr::Style::twoWay);
    CHECK (t.params[(std::size_t) cr::revision] == 1.0f);
    t.params[(std::size_t) cr::autoGain] = 0.0f;
    // the reference's 1 kHz stairs at this setting (refcap030 G1 knee 0, the last 140 ms of each step): 0 dBFS −14.9 dB,
    // −24 dBFS −2.7 dB, −48 dBFS +3.8 dB, −60 dBFS +8.1 dB (peak levels: RMS 3 dB lower). At −60 dBFS LYNX stays 0.6 dB
    // under it: issue #77's fade holds the tone's small high-band share (−92 dBFS) out of its full lift (owner decision)
    const std::pair<double, double> ref[] { { 0.0, -14.9 }, { -24.0, -2.7 }, { -48.0, 3.8 }, { -60.0, 8.1 - 0.6 } };
    for (const auto& [peak, want] : ref)
    {
        const auto got = parity190mj::sineGainDb (t, 1000.0, peak - 3.0103);
        std::printf ("  translated TTM, 1 kHz at %.0f dBFS: %+.2f dB (want %+.1f dB)\n", peak, got, want);
        CHECK (parity190kw::near (got, want, 0.15));
    }
}

TEST (parity190mj_crest_ttm_lookahead_hold_and_safety)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    // the look-ahead (its hold and smoothing) and the hold processor run, stay finite and never boost a burst after
    // silence past its own peak (issue #77's threshold bound)
    for (const float la : { 0.0f, 0.5f, 1.0f })
        for (const float hold : { 0.0f, 0.5f })
        {
            auto s = parity190mj::crest (0.0f, 1.0f, 0.0f, 1.0f, 0.3f, 0.5f);
            s.params[(std::size_t) cr::lookahead] = la;
            s.params[(std::size_t) cr::maxLookahead] = 1.0f;
            s.params[(std::size_t) cr::hold] = hold;
            const double sr = 48000.0;
            std::vector<float> l (72000, 0.0f), r;
            std::uint32_t z = 99u;
            double inPeak = 0.0;
            for (std::size_t i = 48000; i < l.size(); ++i)
            {
                z = z * 1664525u + 1013904223u;
                l[i] = (float) (0.5 * ((double) (z >> 8) / 8388608.0 - 1.0));
                inPeak = std::max (inPeak, (double) std::abs (l[i]));
            }
            r = l;
            parity190kw::render (s, sr, l, r);
            double outPeak = 0.0;
            bool finite = true;
            for (const auto v : l)
            {
                finite = finite && std::isfinite (v);
                outPeak = std::max (outPeak, (double) std::abs (v));
            }
            CHECK (finite);
            CHECK (20.0 * std::log10 (outPeak / inPeak) < 1.0);
        }
}

TEST (parity190mj_crest_fingerprints_revision_0_unchanged_revision_1_pinned)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    struct Case { float rev; cr::Style st; float threshold, ratio, knee, attack, release; double sr; std::uint64_t hash; };
    static const Case cases[] {
#include "Parity190mjCrestGold.inc"
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
