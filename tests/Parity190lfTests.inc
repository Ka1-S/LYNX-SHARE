// Round 190lf: REA on the Waves DSP binaries, third pass (owner's permission of 2026-11-17; /mnt/e/lynx-work/waves2_190lf/
// HANDOFF.md, docs/analise/REA_rodada190lf.md).
// - The Limiter gets an L2 law (hidden [3]): LYNX's own model of the look-ahead brickwall REA showed in the Waves L2 DSP
//   (window minimum, ARC's follower and depth-dependent release, two moving averages), fitted on our demo-mode probes.
//   0 for new instances and every project saved before (bit for bit: lab/gold.sh).
// - Waves L2 translates onto it; Waves PuigTec EQP1A gets a substitute entry (Facet EQ, eight fitted bands).
#include <cmath>

#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/LimiterLaw.h"

namespace parity190lf
{
inline lynx::EffectSlot l2 (double thrDb, double ceilDb, bool arc, double releaseMs = 100.0)
{
    namespace ll = lynx::limiterlaw;
    auto s = lynx::makeEffectSlot (lynx::EffectKind::limiter);
    s.params[0] = (float) ((ceilDb + 12.0) / 12.0);
    s.params[1] = (float) ((ceilDb - thrDb) / 24.0);
    s.params[2] = (float) (std::log (releaseMs / 10.0) / std::log (100.0));
    s.params[(std::size_t) ll::lawParam] = ll::l2LawValue;
    s.params[(std::size_t) ll::arcParam] = arc ? 1.0f : 0.0f;
    return s;
}
// a 1 kHz tone of `peak` dBFS from `at` s over a DC carrier of 0.03 (the probes' trick: the gain is readable anywhere)
inline void bursts (std::vector<float>& l, std::vector<float>& r, double sr, std::initializer_list<std::pair<double, double>> on)
{
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        const double t = (double) i / sr;
        double x = 0.03;
        for (const auto& [a, b] : on)
            if (t >= a && t < b) x += std::sin (2.0 * M_PI * 1000.0 * t);
        l[i] = r[i] = (float) x;
    }
}
} // namespace parity190lf

TEST (parity190lf_limiter_l2_law_is_hidden_and_off_for_new_and_old_limiters)
{
    using namespace lynx;
    namespace ll = limiterlaw;
    CHECK (effectInfo (EffectKind::limiter).params.size() == 3 && ll::lawParam == 3); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::limiter);
    CHECK (s.params[(std::size_t) ll::lawParam] == 0.0f && savedEffectParamCount (s) == 3);
    s.params[(std::size_t) ll::lawParam] = ll::l2LawValue;
    CHECK (savedEffectParamCount (s) == 4 && ll::isL2 ((float) savedEffectParamValue (ll::l2LawValue)));
    // latency: the classic 1.5 ms look-ahead, the L2 law's 64-sample window at 48 kHz
    auto classic = makeEffect (makeEffectSlot (EffectKind::limiter));
    classic->prepare (48000.0);
    CHECK (classic->latencySamples() == 72);
    std::vector<float> l (512, 0.0f), r (512, 0.0f);
    auto fx = makeEffect (s);
    fx->prepare (48000.0);
    fx->process (l.data(), r.data(), 512, s.params, 120.0);
    CHECK (fx->latencySamples() == 64);
}

TEST (parity190lf_limiter_l2_law_is_a_brickwall_with_l2s_makeup)
{
    const double sr = 48000.0;
    const int n = (int) (2.0 * sr);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    parity190lf::bursts (l, r, sr, { { 0.5, 1.5 } });
    std::vector<float> in = l;
    auto s = parity190lf::l2 (-12.0, -0.3, true);
    parity190kt::render (s, sr, l, r);
    double peak = 0.0;
    for (float x : l) peak = std::max (peak, (double) std::abs (x));
    const double ceiling = std::pow (10.0, -0.3 / 20.0);
    CHECK (peak <= ceiling * 1.001 && peak > ceiling * 0.98);
    // the small-signal gain is ceiling − threshold (11.7 dB), the audio 64 samples late
    const double g = (double) l[(std::size_t) (0.25 * sr) + 64] / in[(std::size_t) (0.25 * sr)];
    CHECK (std::abs (20.0 * std::log10 (g) - 11.7) < 0.01);
    // ceiling 0 dB: the make-up stops at 0.999 / threshold
    std::vector<float> l0 ((std::size_t) n), r0 ((std::size_t) n);
    parity190lf::bursts (l0, r0, sr, {});
    auto s0 = parity190lf::l2 (-6.0, 0.0, true);
    parity190kt::render (s0, sr, l0, r0);
    CHECK (std::abs (20.0 * std::log10 (l0[(std::size_t) sr] / 0.03) - (6.0 + 20.0 * std::log10 (0.999))) < 0.005);
}

TEST (parity190lf_limiter_l2_law_releases_by_programme_or_by_the_knob)
{
    const double sr = 48000.0;
    const int n = (int) (6.0 * sr);
    // the gain 100 ms after a burst ends, against the small-signal gain (dB)
    const auto after = [&] (const lynx::EffectSlot& s, double len) {
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        parity190lf::bursts (l, r, sr, { { 1.0, 1.0 + len } });
        parity190kt::render (s, sr, l, r);
        const double small = l[(std::size_t) (0.5 * sr) + 64] / 0.03;
        const auto at = (std::size_t) ((1.0 + len + 0.1) * sr);
        return 20.0 * std::log10 (l[at + 64] / 0.03 / small);
    };
    const auto arc = parity190lf::l2 (-12.0, 0.0, true);
    const auto shortBurst = after (arc, 0.03), longBurst = after (arc, 1.0);
    CHECK (shortBurst > -2.5 && longBurst < shortBurst - 3.0); // ARC: fast after a short peak, slow after sustained limiting
    // the manual Release: its time is the 90 % recovery of the linear gain
    const double manual100 = after (parity190lf::l2 (-12.0, 0.0, false, 100.0), 0.2);
    const double expect = 20.0 * std::log10 (1.0 - (1.0 - std::pow (10.0, -12.0 / 20.0)) * 0.1);
    CHECK (std::abs (manual100 - expect) < 0.3);
    CHECK (after (parity190lf::l2 (-12.0, 0.0, false, 1000.0), 0.2) < manual100 - 3.0);
}

TEST (parity190lf_waves_l2_and_puigtec_are_translated)
{
    using namespace lynx;
    namespace ll = limiterlaw;
    int n = 0;
    // refcap182's X2: ARC, threshold -6, ceiling -0.3 → make-up 5.7 dB
    auto x2 = parity190jh::waves ("L2 Stereo", "L2", "1 -6 -0.3 * * * * * 0 0\n0 1 2 1 0 0.1", n);
    CHECK (x2.kind == EffectKind::limiter && n == 6 && ll::isL2 (x2.params[(std::size_t) ll::lawParam]));
    CHECK (x2.params[(std::size_t) ll::arcParam] == 1.0f && std::abs (x2.params[1] * 24.0f - 5.7f) < 1e-4f);
    // a threshold above the ceiling: the make-up goes below 0 dB through the hidden trim
    auto neg = parity190jh::waves ("L2 Stereo", "L2", "100 0 -3 * * * * * 0 0\n0 1 2 0 0 0.1", n);
    CHECK (neg.params[1] == 0.0f && std::abs (neg.params[(std::size_t) ll::trimParam] * 24.0f - 3.0f) < 1e-4f && neg.params[(std::size_t) ll::arcParam] == 0.0f);

    // PuigTec EQP1A (a substitute entry since this round): Facet EQ
    const auto puig = [&] (std::initializer_list<std::pair<int, const char*>> set) {
        auto v = parity190jh::list (20, { { 1, "20" }, { 5, "1" }, { 8, "1" }, { 10, "3" }, { 11, "4" }, { 13, "1" }, { 15, "1" } });
        auto items = std::vector<std::string>();
        std::string cur;
        for (char c : v) { if (c == ' ') { items.push_back (cur); cur.clear(); } else cur += c; }
        items.push_back (cur);
        for (const auto& [i, s] : set) items[(std::size_t) i] = s;
        std::string list;
        for (const auto& s : items) list += (list.empty() ? "" : " ") + s;
        return parity190jh::waves ("PuigTec EQP1A Stereo", "PuigTec EQP1A", list, n);
    };
    auto flat = puig ({});
    CHECK (flat.kind == EffectKind::facetEq && n == 8);
    // the reference's flat response: +1.1 dB of insertion gain, a small roll-off at 20 kHz
    CHECK (std::abs (facet::responseDb (flat.params, 48000.0, 1000.0) - 1.1) < 0.05);
    CHECK (facet::responseDb (flat.params, 48000.0, 20000.0) < facet::responseDb (flat.params, 48000.0, 1000.0) - 0.3);
    // low boost 5.5 at 100 Hz: about +7 dB at 50 Hz; boost and atten together dip near 1 kHz (the interaction)
    auto lb = puig ({ { 12, "5.5" } });
    CHECK (std::abs (facet::responseDb (lb.params, 48000.0, 50.0) - facet::responseDb (flat.params, 48000.0, 50.0) - 7.2) < 0.5);
    auto both = puig ({ { 12, "11" }, { 9, "11" } });
    CHECK (facet::responseDb (both.params, 48000.0, 1000.0) < facet::responseDb (flat.params, 48000.0, 1000.0) - 3.0);
    CHECK (facet::responseDb (both.params, 48000.0, 30.0) > facet::responseDb (flat.params, 48000.0, 30.0) + 10.0);
    // high boost 11 at 10 kHz (bandwidth 5.5): +12 dB or so at 10 kHz; the In switch off: back to the flat response
    auto hb = puig ({ { 6, "11" }, { 7, "5.5" }, { 8, "5" } });
    CHECK (facet::responseDb (hb.params, 48000.0, 10000.0) - facet::responseDb (flat.params, 48000.0, 10000.0) > 10.0);
    auto out = puig ({ { 6, "11" }, { 7, "5.5" }, { 8, "5" }, { 15, "0" } });
    CHECK (std::abs (facet::responseDb (out.params, 48000.0, 10000.0) - facet::responseDb (flat.params, 48000.0, 10000.0)) < 0.01);
}
