// Round 190jh: plugin parity, the Waves roles and Pro-Q 4's dynamic bands (black-box; /mnt/e/lynx-work/parity190jh).
// - Facet EQ gets an automatic threshold per band (hidden: past the info's parameters, no control, no text), measured on
//   refcap030's Pro-Q 4 captures; the Pro-Q 4 translator now carries a band's dynamics (range, auto / manual threshold,
//   attack / release). A project saved before has the automatic threshold off and renders bit for bit as the base commit
//   (fingerprints from its objects, /mnt/e/lynx-work/parity190jh/gold).
// - Waves states translated (refcap182's programme captures): L2 -> Limiter, SuperTap -> Echoform (was Fruity Delay 3 at FL's
//   default), SSLComp -> Compressor, CLA-76's single ratio buttons re-levelled.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/FacetEq.h"

namespace parity190jh
{
// the gold input (as gold/gold.cpp): a 1 kHz tone stepping -50 … -2 dBFS every 0.1 s over low-passed noise
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 190u;
    double b = 0.0;
    const int n = (int) l.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        s = s * 1664525u + 1013904223u;
        b = 0.98 * b + 0.02 * ((double) (s >> 8) / 8388608.0 - 1.0);
        const double lv = std::pow (10.0, (-50.0 + 6.0 * ((int) (t / 0.1) % 9)) / 20.0);
        const double v = lv * std::sin (2.0 * M_PI * 1000.0 * t) + 0.3 * b;
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (0.8 * v);
    }
}
inline lynx::EffectSlot facetBand (int band, float shape, float gain, float range, float threshold, float attack, float release, float phase)
{
    using namespace lynx;
    auto s = makeEffectSlot (EffectKind::facetEq);
    s.params[(std::size_t) facet::param (band, 0)] = shape;
    s.params[(std::size_t) facet::param (band, 1)] = facet::hzValue (1000.0);
    s.params[(std::size_t) facet::param (band, 2)] = gain;
    s.params[(std::size_t) (facet::rangeParam0 + band)] = range;
    s.params[(std::size_t) (facet::thresholdParam0 + band)] = threshold;
    s.params[(std::size_t) facet::attackParam] = attack;
    s.params[(std::size_t) facet::releaseParam] = release;
    s.params[(std::size_t) facet::phaseParam] = phase;
    return s;
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r, int block = 256)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += block)
        fx->process (l.data() + off, r.data() + off, std::min (block, n - off), s.params, 120.0);
}
inline lynx::EffectSlot waves (const char* name, const char* plugin, const std::string& list, int& translated)
{
    return m124x::substitute (name, substtest::wavesComponent (plugin, list), translated);
}
// a RealWorld list of n values, all "0" but the given ones
inline std::string list (int n, std::initializer_list<std::pair<int, const char*>> set)
{
    std::vector<std::string> v ((std::size_t) n, "0");
    for (const auto& [i, s] : set) v[(std::size_t) i] = s;
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : " ") + s;
    return out;
}
} // namespace parity190jh

TEST (parity190jh_facet_auto_threshold_is_hidden_and_off_in_old_projects)
{
    using namespace lynx;
    const auto& info = effectInfo (EffectKind::facetEq);
    CHECK (info.params.size() == (std::size_t) facet::paramCount);          // hidden: no new control or string
    // (+ round 190lg's revision [125]; round 190ln: three more hidden parameters after it, the all-pass cascade of the Disperser role;
    // round 190mg: + the PuigTec law after those, since the 0.1.30 rollup step 11)
    CHECK (facet::autoThresholdParam0 == facet::paramCount && facet::autoRevisionParam == facet::paramCount + facet::bands
           && facet::dispersionCountParam == facet::paramCount + facet::bands + 1 && facet::puigLawParam == facet::dispersionCountParam + 3
           && facet::wavesLawParam == facet::puigLawParam + 1 && facet::api560LawParam == facet::wavesBandParam0 + facet::bands
           && facet::storedParamCount == facet::api560GainParam0 + 10); // (190mm: + the Waves design law, rollup step 12; 190mt: + the API-560 law, step 13)
    auto s = makeEffectSlot (EffectKind::facetEq);
    for (int b = 0; b < facet::bands; ++b)
        CHECK (! facet::autoThreshold (s.params, b));
    CHECK (savedEffectParamCount (s) <= (std::size_t) facet::paramCount); // nothing new saved for a plain Facet
    s.params[(std::size_t) (facet::autoThresholdParam0 + 2)] = 1.0f;
    CHECK (facet::autoThreshold (s.params, 2) && savedEffectParamCount (s) == (std::size_t) facet::autoThresholdParam0 + 3);
}

TEST (parity190jh_facet_manual_dynamics_render_as_the_base_commit_did)
{
    using namespace lynx;
    struct Case
    {
        int band;
        float shape, gain, range, threshold, attack, release, phase;
        double sr;
        std::uint64_t hash;
        double energy, a, b;
    };
    // 1.2 s of the gold input in blocks of 256: FNV-1a of the float bits, the energy, two samples. From the base commit's
    // objects (/mnt/e/lynx-work/parity190jh/gold: gold_base.txt; the round's objects print the same lines, gold_new.txt)
    const Case cases[] = {
#include "Parity190jhGold.inc"
    };
    for (const auto& c : cases)
    {
        const auto s = parity190jh::facetBand (c.band, c.shape, c.gain, c.range, c.threshold, c.attack, c.release, c.phase);
        const int n = (int) (1.2 * c.sr);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        parity190jh::signal (l, r, c.sr);
        parity190jh::render (s, c.sr, l, r);
        std::uint64_t h = 1469598103934665603ull;
        double e = 0.0;
        for (int i = 0; i < n; ++i)
            for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
            {
                std::uint32_t u;
                std::memcpy (&u, &v, 4);
                for (int k = 0; k < 4; ++k) { h ^= (u >> (8 * k)) & 0xffu; h *= 1099511628211ull; }
                e += (double) v * v;
            }
       #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 43))
        CHECK (h == c.hash); // the libm the fingerprints were taken with (sin / pow / log10 / exp may differ elsewhere)
       #endif
        CHECK (std::abs (e - c.energy) < 1e-6 * c.energy + 1e-9);
        CHECK (std::abs ((double) l[(std::size_t) (n / 3)] - c.a) < 1e-6 && std::abs ((double) r[(std::size_t) (n / 2)] - c.b) < 1e-6);
    }
}

TEST (parity190jh_facet_auto_threshold_follows_the_loudest_recent_level)
{
    using namespace lynx;
    // a +6 dB bell at 1 kHz with a -15 dB dynamic range, automatic threshold: a 1 kHz tone at -12 dBFS gets the whole range
    // (the top is the tone itself); after it falls to -40 dBFS the band recovers (the threshold sits ~27 dB over it) and then
    // creeps down by ~1.7 dB/s as the top falls; never more than the range, and the total gain stays within +-30 dB
    const double sr = 48000.0;
    auto s = parity190jh::facetBand (0, facet::shapeValue (facet::Shape::bell), facet::gainValue (6.0), facet::rangeValue (-15.0),
                                     facet::thresholdValue (-80.0), 0.807f, 0.514f, 0.0f);
    s.params[(std::size_t) facet::autoThresholdParam0] = 1.0f;
    const int n = (int) (6.0 * sr);
    std::vector<float> l ((std::size_t) n), r;
    for (int i = 0; i < n; ++i)
        l[(std::size_t) i] = (float) (std::pow (10.0, (i < (int) (2.0 * sr) ? -12.0 : -40.0) / 20.0) * std::sin (2.0 * M_PI * 1000.0 * i / sr));
    r = l;
    const auto in = l;
    parity190jh::render (s, sr, l, r);
    const auto gainAt = [&] (double t) {
        const int a = (int) (t * sr), w = (int) (0.05 * sr);
        double x = 0, y = 0;
        for (int i = a; i < a + w; ++i) { x += (double) in[(std::size_t) i] * in[(std::size_t) i]; y += (double) l[(std::size_t) i] * l[(std::size_t) i]; }
        return 10.0 * std::log10 (y / x);
    };
    const double loud = gainAt (1.5), back = gainAt (3.0), later = gainAt (5.5);
    std::printf ("  facet auto: %.2f dB at -12 dBFS, %.2f / %.2f dB at -40 dBFS (1 s / 3.5 s later)\n", loud, back, later);
    CHECK (std::abs (loud - (6.0 - 15.0)) < 0.6);          // the whole range
    // round 190js refitted the law (the knee is 1.53 x the range: a -15 dB band lets go 28 dB under the top, the reference's
    // behaviour on the time-constant probe): 1 s later still releasing (305 ms here), then about recovered while the
    // threshold creeps down into the knee
    CHECK (back > 6.0 - 1.0 && back < 6.0 + 0.1);
    CHECK (later > 6.0 - 1.5 && later < 6.0 + 0.1);
    // the manual threshold at the same setting is the old law: -80 dB = the whole range at both levels
    auto m = s;
    m.params[(std::size_t) facet::autoThresholdParam0] = 0.0f;
    std::vector<float> ml = in, mr = in;
    parity190jh::render (m, sr, ml, mr);
    double x = 0, y = 0;
    for (int i = (int) (3.0 * sr); i < (int) (3.05 * sr); ++i) { x += (double) in[(std::size_t) i] * in[(std::size_t) i]; y += (double) ml[(std::size_t) i] * ml[(std::size_t) i]; }
    CHECK (std::abs (10.0 * std::log10 (y / x) - (6.0 - 15.0)) < 0.6);
}

TEST (parity190jh_pro_q4_dynamic_bands_are_translated)
{
    using namespace lynx;
    // band 0: a +6 dB bell at 1 kHz, Q 1, stereo, dynamics on, range -30 dB, Auto, attack / release 50 %; band 1: a -3 dB
    // bell at 4 kHz, dynamics on, range +12 dB, manual threshold 0.5 (-30 dB); band 2: static (dynamics off, range -10)
    std::vector<float> v (600, 0.0f);
    const auto band = [&] (int b, double hz, float gain, float dyn, float range, float autoT, float thr) {
        const auto o = (std::size_t) (23 * b);
        v[o] = 1; v[o + 1] = 1; v[o + 2] = (float) std::log2 (hz); v[o + 3] = gain; v[o + 4] = 0.5f; v[o + 5] = 0; v[o + 6] = 2; v[o + 7] = 2;
        v[o + 9] = range; v[o + 10] = dyn; v[o + 11] = autoT; v[o + 12] = thr; v[o + 13] = 50; v[o + 14] = 50;
    };
    band (0, 1000.0, 6.0f, 1.0f, -30.0f, 1.0f, 0.0f);
    band (1, 4000.0, -3.0f, 1.0f, 12.0f, 0.0f, 0.5f);
    band (2, 200.0, 2.0f, 0.0f, -10.0f, 1.0f, 0.5f);
    v[555] = 1.0f;
    int n = 0;
    const auto s = m124x::substitute ("FabFilter Pro-Q 4", m124x::ffbs (v), n);
    CHECK (s.kind == EffectKind::facetEq);
    CHECK (std::abs (facet::rangeDb (s.params[(std::size_t) facet::rangeParam0]) + 30.0) < 0.01 && facet::autoThreshold (s.params, 0));
    CHECK (std::abs (facet::rangeDb (s.params[(std::size_t) facet::rangeParam0 + 1]) - 12.0) < 0.01 && ! facet::autoThreshold (s.params, 1));
    CHECK (std::abs (facet::thresholdDb (s.params[(std::size_t) facet::thresholdParam0 + 1]) + 30.0) < 0.01);
    CHECK (! facet::isDynamic (s.params, 2) && ! facet::autoThreshold (s.params, 2)); // dynamics off: a static band
    CHECK (std::abs (facet::attackMs (s.params[(std::size_t) facet::attackParam]) - 9.22) < 0.1);   // (round 190js: was 18.1 / 305)
    CHECK (std::abs (facet::releaseMs (s.params[(std::size_t) facet::releaseParam]) - 268.0) < 1.0);
    // the manual threshold law (the parameter's own texts): 0 -90, 0.125 -66, 0.25 -45, 0.875 -7.5 dB
    for (const auto& [x, db] : { std::pair { 0.0f, -80.0 }, std::pair { 0.125f, -66.0 }, std::pair { 0.25f, -45.0 }, std::pair { 0.875f, -7.5 } })
    {
        v[23 + 12] = x;
        const auto t = m124x::substitute ("FabFilter Pro-Q 4", m124x::ffbs (v), n);
        CHECK (std::abs (facet::thresholdDb (t.params[(std::size_t) facet::thresholdParam0 + 1]) - db) < 0.01); // (Facet's floor is -80)
    }
}

TEST (parity190jh_waves_l2_sslcomp_supertap_are_translated)
{
    using namespace lynx;
    int n = 0;
    // L2: release 100 ms (ARC off), threshold -6, ceiling -1
    auto l2 = parity190jh::waves ("L2 Stereo", "L2", "100 -6 -1 * * * * * 0 0\n0 1 2 0 0 0.1", n);
    // (round 190lf: on the Limiter's L2 law — the Gain knob is L2's make-up, ceiling − threshold; tests/Parity190lfTests.inc)
    CHECK (l2.kind == EffectKind::limiter && n == 6);
    CHECK (std::abs (l2.params[0] - 11.0f / 12.0f) < 1e-5f && std::abs (l2.params[1] - 5.0f / 24.0f) < 1e-5f && std::abs (l2.params[2] - 0.5f) < 1e-5f);
    auto arc = parity190jh::waves ("L2 Stereo", "L2", "1 -3 -0.3 * * * * * 0 0\n0 1 2 1 0 0.1", n);
    CHECK (arc.params[2] == 0.0f); // ARC: the Limiter's shortest release
    parity190jh::waves ("L3 Stereo", "L3", "1 -3 -0.3 * * * * * 0 0\n0 1 2 1 0 0.1", n);
    CHECK (n == 0); // L1 / L3 / L4: their own lists, not read
    // SSLComp: threshold +4, 2:1, attack 30 ms, release Auto, make-up 2 dB, mix 100 %
    auto sc = parity190jh::waves ("SSLComp Stereo", "SSLComp", parity190jh::list (22, { { 0, "4" }, { 1, "0" }, { 3, "5" }, { 4, "4" }, { 5, "2" }, { 19, "100" } }), n);
    // (round 190kt: SSLComp now stands in on Crest's console law, its own entry; tests/Parity190ktTests.inc)
    CHECK (sc.kind == EffectKind::crestCompressor && n == 22);
    parity190jh::waves ("CLA-3A Stereo", "CLA-3A", parity190jh::list (22, { { 0, "4" } }), n);
    CHECK (n == 0);
    // SuperTap: every tap off (its default) = the dry signal; one tap; two taps hard left / right with feedback
    namespace e = echosuite::echoform;
    auto dry = parity190jh::waves ("SuperTap 2-Taps Stereo", "SuperTap", parity190jh::list (98, { { 4, "120" } }), n);
    CHECK (dry.kind == EffectKind::echoform && dry.params[e::mix] == 0.0f);
    auto one = parity190jh::waves ("SuperTap 2-Taps Stereo", "SuperTap",
                                   parity190jh::list (98, { { 4, "136.364" }, { 33, "1" }, { 34, "-6" }, { 36, "0.064" }, { 39, "5.3" }, { 41, "1" } }), n);
    CHECK (std::abs (e::timeMs (one.params[e::timeL]) - 112.64) < 0.5 && one.params[e::style] == e::styleValue (e::Style::single));
    CHECK (std::abs (e::highCutHz (one.params[e::highCut]) - 5300.0) < 20.0 && one.params[e::mix] < 0.2f);
    auto two = parity190jh::waves ("SuperTap 2-Taps Stereo", "SuperTap",
                                   parity190jh::list (98, { { 4, "120" }, { 10, "1" }, { 11, "40" }, { 33, "1" }, { 34, "-9" }, { 35, "-45" }, { 36, "0.188" },
                                                            { 43, "1" }, { 44, "-9" }, { 45, "45" }, { 46, "0.375" } }), n);
    CHECK (two.params[e::style] == e::styleValue (e::Style::dual) && two.params[e::width] == 1.0f);
    CHECK (std::abs (e::timeMs (two.params[e::timeL]) - 376.0) < 0.5 && std::abs (e::timeMs (two.params[e::timeR]) - 750.0) < 0.5);
    CHECK (std::abs (two.params[e::feedback] - 0.2f) < 1e-4f);
    // the echo sits at the tap's level over a dry path at unity: an impulse through the one-tap state without its filter
    const auto plain = parity190jh::waves ("SuperTap 2-Taps Stereo", "SuperTap",
                                           parity190jh::list (98, { { 4, "136.364" }, { 33, "1" }, { 34, "-6" }, { 36, "0.064" } }), n);
    // a 200 Hz burst (0 … 50 ms): the dry burst at unity, the echo burst at the tap's level 112.6 ms later
    std::vector<float> l (48000, 0.0f), r;
    for (int i = 0; i < 2400; ++i) l[(std::size_t) i] = (float) (0.5 * std::sin (2.0 * M_PI * 200.0 * i / 48000.0));
    r = l;
    const auto in = l;
    parity190jh::render (plain, 48000.0, l, r);
    double dryOut = 0.0, dryIn = 0.0, echo = 0.0;
    for (int i = 240; i < 2160; ++i) { dryOut += (double) l[(std::size_t) i] * l[(std::size_t) i]; dryIn += (double) in[(std::size_t) i] * in[(std::size_t) i]; }
    for (int i = 5407 + 240; i < 5407 + 2160; ++i) echo += (double) l[(std::size_t) i] * l[(std::size_t) i];
    std::printf ("  supertap one tap: dry %.2f dB, echo %.2f dB (the tap -6 dB)\n", 10.0 * std::log10 (dryOut / dryIn), 10.0 * std::log10 (echo / dryIn));
    CHECK (std::abs (10.0 * std::log10 (dryOut / dryIn)) < 0.1);
    CHECK (std::abs (10.0 * std::log10 (echo / dryIn) - (-6.0)) < 1.0);
}
