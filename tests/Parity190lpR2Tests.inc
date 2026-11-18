// Round 190lp (Pro-R 2 → LuxeVerb Space revision 1): the reference's fixed early taps. Its impulse captures put
// single-sample arrivals at the same times at 44.1 / 48 / 96 kHz and at every Space, Decay Rate and Distance up to ½
// (left 6.67, 11.31, 18.33 … ms, right 8.29, 12.48, 21.13 … ms; silence before the first and between the early ones);
// only their levels move: a few stand out at some Spaces (switching with the integer k of the line-length law),
// they fall with Distance and are gone from ¾ on, the standing-out ones rise ~3 dB from Character ¼ to ½. Revision 1
// was never released (tp172b's twelve taps of revision 0 are untouched: gold fingerprints in Parity190kwR2Tests.inc).
namespace p190lpr2t
{
// a Pro-R 2 state (Modern, 100 % wet): Space, Distance, Character; Decay Rate 0 (× 1), Brightness ½, Stereo Width 0.7
inline lynx::EffectSlot state (float space, float distance, float character)
{
    std::vector<float> v (136, 0.0f);
    v[0] = space; v[1] = 0.0f; v[2] = distance; v[3] = 0.5f; v[5] = character; v[7] = 0.7f; v[9] = 100.0f;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::luxeVerb);
    if (const auto st = lynx::fabfilter::parseState (p190kwr2t::ffbs (v))) lynx::fabfilter::fromProR2 (*st, slot);
    return slot;
}
// an impulse at 0.2 s (after the parameters' smoothing has settled) on both channels through the slot
constexpr double t0 = 0.2;
inline int i0 (double sr) { return (int) std::lround (t0 * sr); }
inline void render (const lynx::EffectSlot& slot, std::vector<float>& l, std::vector<float>& r, double sr = 48000.0)
{
    const int n = (int) (0.5 * sr);
    l.assign ((std::size_t) n, 0.0f); r.assign ((std::size_t) n, 0.0f);
    l[(std::size_t) i0 (sr)] = r[(std::size_t) i0 (sr)] = 0.5f;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
}
// the sample at a tap's onset (ms after the impulse)
inline double at (const std::vector<float>& x, double ms, double sr = 48000.0) { return x[(std::size_t) (i0 (sr) + std::lround (ms * 0.001 * sr))]; }
inline double dbAt (const std::vector<float>& x, double ms, double sr = 48000.0) { return 20.0 * std::log10 (std::abs (at (x, ms, sr)) + 1e-12); }
} // namespace p190lpr2t

TEST (parity190lpR2_the_early_taps_sit_on_the_reference_times)
{
    using namespace p190lpr2t;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<float> l, r;
        render (state (0.0f, 0.5f, 0.3f), l, r, sr);
        // nothing before the first tap (6.67 ms left, 8.29 ms right)
        float before = 0.0f;
        for (int i = i0 (sr) + 1; i < i0 (sr) + (int) std::lround (6.6667e-3 * sr); ++i) before = std::max ({ before, std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]) });
        std::printf ("  %.0f Hz: before %.2g, L 6.67 %+.4f, L 11.31 %+.4f, R 8.29 %+.4f, R 12.48 %+.4f\n", sr, (double) before,
                     at (l, 6.6667, sr), at (l, 11.3125, sr), at (r, 8.2917, sr), at (r, 12.4792, sr));
        CHECK (before < 1e-6f);
        // the polarities of the reference's first taps
        CHECK (at (l, 6.6667, sr) > 0.01);
        CHECK (at (l, 11.3125, sr) < -0.01);
        CHECK (at (r, 8.2917, sr) > 0.01);
        CHECK (at (r, 12.4792, sr) > 0.01);
        // right is silent until its own first tap
        float rBefore = 0.0f;
        for (int i = i0 (sr) + 1; i < i0 (sr) + (int) std::lround (8.2917e-3 * sr); ++i) rBefore = std::max (rBefore, std::abs (r[(std::size_t) i]));
        CHECK (rBefore < 1e-6f);
    }
}

TEST (parity190lpR2_tap_levels_follow_space_distance_and_character)
{
    using namespace p190lpr2t;
    std::vector<float> l0, r0, l5, r5, ln, rn, lf, rf, lc, rc;
    render (state (0.0f, 0.5f, 0.3f), l0, r0);
    render (state (0.5f, 0.5f, 0.3f), l5, r5);
    // Space 0 → ½: the left 11.31 ms tap falls by ~26 dB (the reference: −20 → −46 dB), the left 18.33 ms one stays
    const double s0 = dbAt (l0, 11.3125), s5 = dbAt (l5, 11.3125);
    std::printf ("  L 11.31 ms: Space 0 %.1f dB, Space 50 %% %.1f dB; L 18.33 ms %.1f / %.1f dB\n", s0, s5, dbAt (l0, 18.3333), dbAt (l5, 18.3333));
    CHECK (s0 - s5 > 15.0);
    CHECK (std::abs (dbAt (l0, 18.3333) - dbAt (l5, 18.3333)) < 4.0);
    // Distance 0 → ½ at Space ¼: the standing-out taps by ~15 dB (the reference: −13 → −28 dB); from ¾ on none
    render (state (0.25f, 0.0f, 0.3f), ln, rn);
    render (state (0.25f, 0.8f, 0.3f), lf, rf);
    std::vector<float> lm, rm;
    render (state (0.25f, 0.5f, 0.3f), lm, rm);
    const double dn = dbAt (ln, 11.3125), dm = dbAt (lm, 11.3125);
    double farMax = 0.0;
    for (int i = i0 (48000.0); i < i0 (48000.0) + 480; ++i) farMax = std::max (farMax, (double) std::abs (lf[(std::size_t) i]));
    std::printf ("  L 11.31 ms at Space 25 %%: Distance 0 %.1f dB, 50 %% %.1f dB; Distance 80 %%: first 10 ms peak %.1f dB\n", dn, dm,
                 20.0 * std::log10 (farMax + 1e-12));
    CHECK (dn - dm > 10.0 && dn - dm < 20.0);
    CHECK (20.0 * std::log10 (farMax + 1e-12) < dm - 20.0);
    // Character 0.3 → ½: the standing-out tap ~3 dB up, a background tap (L 8.96 ms) unchanged
    render (state (0.25f, 0.5f, 0.5f), lc, rc);
    const double c1 = dbAt (lc, 11.3125) - dm, c2 = dbAt (lc, 8.9583) - dbAt (lm, 8.9583);
    std::printf ("  Character 30 → 50 %%: L 11.31 ms %+.2f dB, L 8.96 ms %+.2f dB\n", c1, c2);
    CHECK (c1 > 1.5 && c1 < 4.5);
    CHECK (std::abs (c2) < 0.5);
}

TEST (parity190lpR2_revision_rule_and_the_far_early_field)
{
    using namespace lynx;
    using namespace p190lpr2t;
    // the taps live in revision 1 only: a Pro-R 2 state gets it, a project saved before the revision loads 0 and keeps
    // tp172b's spread reflections (something before 6.67 ms at Space 0, Distance ½)
    auto a = state (0.0f, 0.5f, 0.3f);
    CHECK (a.params[(std::size_t) luxeVerbSpaceRevision] == 1.0f);
    auto old = a;
    upgradeLoadedEffectParams (old, (std::size_t) luxeVerbSpaceDucking + 1);
    CHECK (old.params[(std::size_t) luxeVerbSpaceRevision] == 0.0f);
    std::vector<float> l1, r1, l0, r0;
    render (a, l1, r1);
    render (old, l0, r0);
    float b1 = 0.0f, b0 = 0.0f;
    for (int i = i0 (48000.0) + 1; i < i0 (48000.0) + 320; ++i)
    {
        b1 = std::max (b1, std::abs (l1[(std::size_t) i]));
        b0 = std::max (b0, std::abs (l0[(std::size_t) i]));
    }
    std::printf ("  first 6.67 ms: revision 1 %.2g, revision 0 %.2g\n", (double) b1, (double) b0);
    CHECK (b1 < 1e-6f);
    CHECK (b0 > 1e-3f);
    // past Distance ½ the taps fade and the early / 2-line networks take over (the reference's far early field is dense
    // and starts within a few ms): Distance 0.9 has energy in 5 … 20 ms, Distance ½ only the taps there
    std::vector<float> lf, rf;
    render (state (0.25f, 0.9f, 0.3f), lf, rf);
    double e = 0.0;
    for (int i = i0 (48000.0) + 240; i < i0 (48000.0) + 960; ++i) e += (double) lf[(std::size_t) i] * lf[(std::size_t) i];
    std::printf ("  Distance 90 %%: 5 … 20 ms %.1f dB\n", 10.0 * std::log10 (e + 1e-30));
    CHECK (10.0 * std::log10 (e + 1e-30) > -60.0);
}
