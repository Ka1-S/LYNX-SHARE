// Round 190js: plugin parity 5 (black-box; /mnt/e/lynx-work/parity190js/HANDOFF.md).
// - The Waves reverbs (RVerb, H-Reverb, TrueVerb) translated onto their stand-in Reeverb 2: decay, pre-delay, the wet level
//   by the decay, each reverb's measured tone, H-Reverb's ducking, TrueVerb's direct path and levels (refcap182).
// - Hush De-esser's Single Vocal gets a "stands out" revision (hidden vocalRevision; a project saved before keeps round
//   148's Single Vocal bit for bit: fingerprints from the base commit's objects, /mnt/e/lynx-work/parity190js/gold).
// - Facet EQ's automatic threshold refitted at 5 ms (knee by the range; the Pro-Q 4 translator's 50 % = 9.2 / 268 ms).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"
#include "core/Reeverb2.h"

namespace parity190js
{
namespace hu = lynx::mixsuite::hush;
// the gold input (as gold/gold.cpp): a 500 Hz body (-14 dBFS) with resonant 7 kHz hiss bursts of rising level, the body off
// after 1.75 s
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 19034u;
    double y1 = 0.0, y2 = 0.0;
    const double w = 2.0 * M_PI * 7000.0 / sr, rr = 0.93, a1 = 2.0 * rr * std::cos (w), a2 = -rr * rr;
    const int n = (int) l.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        s = s * 1664525u + 1013904223u;
        const double nz = (double) (s >> 8) / 8388608.0 - 1.0;
        const double y = nz + a1 * y1 + a2 * y2; y2 = y1; y1 = y;
        const int k = (int) (t / 0.25);
        const double ph = t - 0.25 * k, lvl = std::pow (10.0, (-36.0 + 6.0 * (k % 7)) / 20.0);
        const double burst = ph > 0.05 && ph < 0.2 ? lvl * 0.05 * y : 0.0;
        const double body = t < 1.75 ? 0.2 * std::sin (2.0 * M_PI * 500.0 * t) : 0.0;
        const double v = body + burst;
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (0.9 * v);
    }
}
inline lynx::EffectSlot vocal (float revision)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::hushDeEsser);
    s.params[(std::size_t) hu::rangeLaw] = 1.0f;
    s.params[(std::size_t) hu::vocalVoicing] = 1.0f;
    s.params[(std::size_t) hu::allround] = 0.0f;
    s.params[(std::size_t) hu::lookaheadTime] = 10.0f / 15.0f;
    s.params[(std::size_t) hu::vocalRevision] = revision;
    return s;
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
}
// band-limited level change (dB) of out against in over [a, b) s, through an FFT-free one-pole pair (lo … hi Hz)
inline double bandGainDb (const std::vector<float>& in, const std::vector<float>& out, int latency, double sr, double a, double b)
{
    double x = 0.0, y = 0.0;
    for (int i = (int) (a * sr); i < (int) (b * sr); ++i)
    {
        x += (double) in[(std::size_t) i] * in[(std::size_t) i];
        y += (double) out[(std::size_t) (i + latency)] * out[(std::size_t) (i + latency)];
    }
    return 10.0 * std::log10 ((y + 1e-30) / (x + 1e-30));
}
} // namespace parity190js

TEST (parity190js_hush_single_vocal_revision_is_hidden_new_and_translated_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace hu = mixsuite::hush;
    const auto& info = effectInfo (EffectKind::hushDeEsser);
    CHECK (info.params.size() == (std::size_t) hu::timing && hu::vocalRevision == hu::timing + 1); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::hushDeEsser);
    CHECK (s.params[(std::size_t) hu::vocalRevision] == 1.0f && savedEffectParamCount (s) == (std::size_t) hu::vocalRevision + 1);
    auto now = s;
    upgradeLoadedEffectParams (now, (std::size_t) hu::vocalRevision + 1);
    CHECK (now.params[(std::size_t) hu::vocalRevision] == 1.0f);
    // saved by round 190iq (the timing, not the revision) or before: round 148's Single Vocal
    auto iq = s;
    upgradeLoadedEffectParams (iq, (std::size_t) hu::vocalRevision);
    CHECK (iq.params[(std::size_t) hu::vocalRevision] == 0.0f && iq.params[(std::size_t) hu::timing] == 1.0f);
    auto old = s;
    upgradeLoadedEffectParams (old, (std::size_t) hu::timing);
    CHECK (old.params[(std::size_t) hu::vocalRevision] == 0.0f && old.params[(std::size_t) hu::timing] == 0.0f);
    // a translated Pro-DS state (her first state's values) runs it
    std::vector<float> st { 0.0f, 0.2715f, 6.0f, 0.0f, 0.75f, 1.0f, 12.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                            0.0f, 0.0f, 0.0f, 0.2f, 0.0f, -1.0f, 0.0f, 0.0f };
    int n = 0;
    const auto t = m124x::substitute ("FabFilter Pro-DS", p165w::fabf (st), n);
    CHECK (t.kind == EffectKind::hushDeEsser && t.params[(std::size_t) hu::vocalRevision] == 1.0f);
}

TEST (parity190js_hush_single_vocal_revision0_renders_as_the_base_commit_did)
{
    using namespace lynx;
    namespace hu = mixsuite::hush;
    struct Case
    {
        int idx;
        float val;
        int idx2;
        float val2;
        double sr;
        std::uint64_t hash;
        double energy, a, b;
    };
    // 2.2 s of the gold input in blocks of 256: FNV-1a of the float bits, the energy, two samples. From the base commit's
    // objects (origin/parity190jh; /mnt/e/lynx-work/parity190js/gold: gold_base.txt — the round's objects print the same lines)
    const Case cases[] = {
#include "Parity190jsGold.inc" // (beside this file: the CMake test build has no -I for the repository root)
    };
    for (const auto& c : cases)
    {
        auto s = parity190js::vocal (0.0f);
        if (c.idx >= 0) s.params[(std::size_t) c.idx] = c.val;
        if (c.idx2 >= 0) s.params[(std::size_t) c.idx2] = c.val2;
        const int n = (int) (2.2 * c.sr);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        parity190js::signal (l, r, c.sr);
        parity190js::render (s, c.sr, l, r);
        std::uint64_t h = 1469598103934665603ull;
        double e = 0.0;
        for (int i = 0; i < n; ++i)
            for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
            {
                std::uint32_t u;
                std::memcpy (&u, &v, 4);
                for (int b = 0; b < 4; ++b) { h ^= (u >> (8 * b)) & 0xffu; h *= 1099511628211ull; }
                e += (double) v * v;
            }
       #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 43))
        CHECK (h == c.hash); // the libm the fingerprints were taken with
       #endif
        CHECK (std::abs (e - c.energy) < 1e-6 * c.energy + 1e-9);
        CHECK (std::abs ((double) l[(std::size_t) (n / 3)] - c.a) < 1e-6 && std::abs ((double) r[(std::size_t) (n / 2)] - c.b) < 1e-6);
    }
}

TEST (parity190js_hush_single_vocal_acts_on_what_stands_out)
{
    using namespace lynx;
    // a 500 Hz body at -14 dBFS (sine peak) under a 7 kHz burst at 0.3 … 0.6 s: 5 dB over the body (the reference leaves it),
    // 12 dB over (the whole range); range 12 dB, threshold -30 dB, wide band, look-ahead 10 ms, 48 kHz
    const double sr = 48000.0;
    const auto run = [&] (float rev, double burstDb) {
        const int n = (int) sr;
        std::vector<float> in ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            in[(std::size_t) i] = (float) (0.2 * std::sin (2.0 * M_PI * 500.0 * t)
                                           + (t >= 0.3 && t < 0.6 ? 0.2 * std::pow (10.0, burstDb / 20.0) * std::sin (2.0 * M_PI * 7000.0 * t) : 0.0));
        }
        auto s = parity190js::vocal (rev);
        s.params[(std::size_t) mixsuite::hush::mode] = 1.0f;
        s.params[(std::size_t) mixsuite::hush::threshold] = 0.5f;
        s.params[(std::size_t) mixsuite::hush::range] = 0.5f;
        auto l = in, r = in;
        auto fx = makeEffect (s);
        fx->prepare (sr);
        const int lat = fx->latencySamples();
        for (int off = 0; off < n; off += 256)
            fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
        return parity190js::bandGainDb (in, l, lat, sr, 0.4, 0.55);
    };
    const double small1 = run (1.0f, 5.0), big1 = run (1.0f, 12.0), small0 = run (0.0f, 5.0);
    std::printf ("  hush single vocal: +5 dB burst %.2f dB (revision 0: %.2f), +12 dB burst %.2f dB\n", small1, small0, big1);
    CHECK (small1 > -1.0);               // stands out by 5 dB: left alone (the reference: -0.2 dB)
    CHECK (big1 < -10.0 && big1 > -12.5); // by 12 dB: the whole range (12 dB)
    CHECK (small0 < small1 - 1.0);       // round 148's test acted on it
}

TEST (parity190js_facet_auto_threshold_knee_follows_the_range)
{
    using namespace lynx;
    // a +6 dB bell at 1 kHz, automatic threshold: a -6 dBFS tone for 1 s, then -40 dBFS. The reference keeps ~5 dB of cut
    // at -40 dBFS with a -30 dB range (time-constant probe: 4.5 s), and none with -15 dB; round 190jh's 30.5 dB knee took
    // 2.7 / 0.6 dB there
    const double sr = 48000.0;
    const auto cutAt = [&] (double rangeDb) {
        auto s = parity190jh::facetBand (0, facet::shapeValue (facet::Shape::bell), facet::gainValue (6.0), facet::rangeValue (rangeDb),
                                         facet::thresholdValue (-80.0), 0.5f, 0.5f, 0.0f);
        s.params[(std::size_t) facet::autoThresholdParam0] = 1.0f;
        const int n = (int) (2.5 * sr);
        std::vector<float> l ((std::size_t) n), r;
        for (int i = 0; i < n; ++i)
            l[(std::size_t) i] = (float) (std::pow (10.0, (i < (int) sr ? -6.0 : -40.0) / 20.0) * std::sin (2.0 * M_PI * 1000.0 * i / sr));
        r = l;
        const auto in = l;
        parity190jh::render (s, sr, l, r);
        return 6.0 - parity190js::bandGainDb (in, l, 0, sr, 2.0, 2.2);
    };
    const double c30 = cutAt (-30.0), c15 = cutAt (-15.0);
    std::printf ("  facet auto: cut %.2f dB (range -30) / %.2f dB (range -15) 1 s after a -6 dBFS tone, at -40 dBFS\n", c30, c15);
    CHECK (c30 > 3.5 && c30 < 8.0);
    CHECK (c15 < 0.5);
    CHECK (std::abs (facet::autoLaw().kneePerRange - 1.53) < 1e-9 && std::abs (facet::autoLaw().decayDbPerSecond - 1.70) < 1e-9);
}

TEST (parity190js_waves_reverbs_are_translated_onto_reeverb2)
{
    using namespace lynx;
    int n = 0;
    // RVerb: decay 2.4 s, pre-delay 30 ms (index 2 / 17)
    const auto rv = parity190jh::waves ("RVerb Stereo", "RVerb", parity190jh::list (96, { { 2, "2.4" }, { 17, "30" } }), n);
    CHECK (rv.kind == EffectKind::reeverb2 && n == 10 && rv.mix == 1.0f);
    auto s = reeverb2::fromParams (rv.params);
    CHECK (s.p[5] == 24 && s.p[2] == 12 && s.p[10] == 0 && s.p[1] == 96 && s.p[6] == 29 && s.p[11] == 160);
    CHECK (s.p[12] == (int) std::lround (160.0 * 0.54 * std::pow (10.0, -8.25 * std::log10 (2.4 / 1.5) / 20.0)));
    // a longer RVerb keeps about its level: a lower wet control
    const auto rv4 = parity190jh::waves ("RVerb Stereo", "RVerb", parity190jh::list (96, { { 2, "4" } }), n);
    CHECK (reeverb2::fromParams (rv4.params).p[12] < s.p[12]);
    // H-Reverb: reverb time 2.16 (x 1.45 = 3.1 s), pre-delay 24 ms; ducking on (249) with a -9 dB range (59): 2.25 dB less wet
    const auto hr = parity190jh::waves ("H-Reverb Stereo", "H-Reverb", parity190jh::list (269, { { 5, "2.16" }, { 16, "24" }, { 59, "-18" } }), n);
    const auto hd = parity190jh::waves ("H-Reverb Stereo", "H-Reverb", parity190jh::list (269, { { 5, "2.16" }, { 16, "24" }, { 59, "-9" }, { 249, "1" } }), n);
    const auto h0 = reeverb2::fromParams (hr.params), h1 = reeverb2::fromParams (hd.params);
    CHECK (h0.p[5] == 31 && h0.p[2] == 9 && h0.p[1] == 44 && h0.p[10] == 0);
    CHECK (std::abs (20.0 * std::log10 ((double) h1.p[12] / h0.p[12]) + 2.25) < 0.2);
    // TrueVerb: decay 1.6 s, distance 4.5, pre-delay 63 ms, reverb -10 dB, early -12 dB, the direct path on / off
    const auto tv = parity190jh::waves ("TrueVerb Stereo", "TrueVerb",
                                        parity190jh::list (50, { { 0, "1.6" }, { 5, "4.5" }, { 24, "63" }, { 42, "-10" }, { 43, "-12" } }), n);
    const auto t0 = reeverb2::fromParams (tv.params);
    // round 190lf: the direct sound is Reeverb's hidden direct law (TrueVerb's measured low shelf); Reeverb's own dry at 0
    CHECK (t0.p[10] == 0 && tv.params[(std::size_t) reeverb2DirectLaw] == 1.0f && t0.p[1] == 221);
    CHECK (t0.p[11] == (int) std::lround (160.0 * 0.36 * std::pow (10.0, -12.0 / 20.0)));
    const auto td = parity190jh::waves ("TrueVerb Stereo", "TrueVerb", parity190jh::list (50, { { 0, "1.6" }, { 29, "1" } }), n);
    CHECK (reeverb2::fromParams (td.params).p[10] == 0);
    // another Waves reverb name, or no readable state: the entry's preset at the slot's 25 % as before
    const auto other = parity190jh::waves ("RVerb Stereo", "Renaissance Reverb", parity190jh::list (96, { { 2, "2.4" } }), n);
    CHECK (other.kind == EffectKind::reeverb2 && std::abs (other.mix - 0.25f) < 1e-6f && n == 0);
}
