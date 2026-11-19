// Round 190mg (coordinator): the Waves C4 on Prism Multiband's hidden C4 law (owner's permission of 2026-11-17 for
// clean-room REA of third-party binaries; /mnt/e/lynx-work/waves3_190mg/HANDOFF.md, docs/analise/REA_rodada190mg.md §2).
// - Prism gets a hidden C4 law ([157], with the crossover Q at [158]): 0 for new instances and every project saved before
//   (bit for bit: waves3_190mg/lab/gold.sh); only the C4 translator sets it.
// - C4 had no entry before (such projects lost the plug-in): it now stands in on Prism's four bands.
#include <cmath>
#include <vector>

#include "core/DynamicsSuite.h"
#include "core/Effects.h"

namespace parity190mgc4
{
inline lynx::EffectSlot c4Slot()
{
    using namespace lynx;
    namespace pr = dyn::prism;
    auto s = makeEffectSlot (EffectKind::prismMultiband);
    s.params[(std::size_t) pr::bandLayout] = 0.0f;
    s.params[(std::size_t) pr::bandCountParam] = pr::bandCountValue (4);
    const double hz[3] { 92.0, 4000.0, 11071.0 };
    for (int k = 0; k < 3; ++k)
        s.params[(std::size_t) pr::crossoverParam (k)] = pr::crossoverValue (hz[k]);
    for (int b = 0; b < 4; ++b)
    {
        s.params[(std::size_t) pr::param (b, pr::fRange)] = 0.5f; // 0 dB: no dynamics
        s.params[(std::size_t) pr::param (b, pr::fLevel)] = 0.5f;
        s.params[(std::size_t) pr::param (b, pr::fEnabled)] = 1.0f;
        s.params[(std::size_t) pr::param (b, pr::fSolo)] = 0.0f;
    }
    s.params[(std::size_t) pr::mix] = 1.0f;
    s.params[(std::size_t) pr::input] = 0.5f;
    s.params[(std::size_t) pr::output] = 0.5f;
    s.params[(std::size_t) pr::c4Law] = pr::c4LawValue (false);
    return s;
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r, int& latency)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
    latency = fx->latencySamples();
}
} // namespace parity190mgc4

TEST (parity190mg_c4_law_is_hidden_and_off_for_new_and_old_instances)
{
    using namespace lynx;
    namespace pr = dyn::prism;
    // (rollup 0.1.30 step 11: after round 190lg's hidden revision / lookaheadSwitch [count, count + 1])
    CHECK (effectInfo (EffectKind::prismMultiband).params.size() == (std::size_t) pr::count && pr::c4Law == pr::lookaheadSwitch + 1 && pr::c4Q == pr::c4Law + 1);
    auto s = makeEffectSlot (EffectKind::prismMultiband);
    const auto saved = savedEffectParamCount (s);
    CHECK (s.params[(std::size_t) pr::c4Law] == 0.0f && s.params[(std::size_t) pr::c4Q] == 0.0f && ! pr::c4Active (s.params));
    s.params[(std::size_t) pr::c4Law] = pr::c4LawValue (true);
    CHECK (pr::c4Active (s.params) && pr::c4Opto (s.params) && saved <= (std::size_t) pr::revision && savedEffectParamCount (s) == (std::size_t) pr::c4Law + 1);
    s.params[(std::size_t) pr::c4Q] = pr::c4QValue (0.6);
    CHECK (savedEffectParamCount (s) == (std::size_t) pr::c4Q + 1 && std::abs (pr::c4QOf (s.params) - 0.6) < 1e-6);
    CHECK (std::abs (pr::c4AttackValue (0.5)) < 1e-6f && std::abs (pr::c4ReleaseValue (5000.0) - 1.0f) < 1e-6f);
}

TEST (parity190mg_c4_is_translated_onto_prism_with_the_c4_law)
{
    using namespace lynx;
    namespace pr = dyn::prism;
    int n = 0;
    // refcap182's X3: crossovers 250 / 2500 / 8000, the four bands' thresholds and ranges
    const auto s = parity190jh::waves ("C4 Stereo", "C4", parity190jh::list (70, { { 0, "250" }, { 1, "2500" }, { 2, "8000" }, { 3, "0.6" },
        { 19, "-36" }, { 20, "-6" }, { 21, "0.5" }, { 22, "34.99" }, { 23, "40.27" }, { 33, "-28" }, { 34, "-4" }, { 35, "0.5" },
        { 47, "-34" }, { 48, "-4" }, { 61, "-42" }, { 62, "-3" }, { 67, "1" } }), n);
    CHECK (s.kind == EffectKind::prismMultiband && n == 40 && pr::c4Active (s.params) && pr::c4Opto (s.params));
    CHECK (std::abs (pr::crossovers (s.params)[0] - 250.0) < 0.5 && std::abs (pr::crossovers (s.params)[2] - 8000.0) < 5.0);
    CHECK (std::abs (pr::thresholdDb (s.params[(std::size_t) pr::param (0, pr::fThreshold)]) + 36.0) < 0.05);
    CHECK (std::abs (pr::rangeDb (s.params[(std::size_t) pr::param (3, pr::fRange)]) + 3.0) < 1e-4);
    CHECK (std::abs (pr::kneeDb (s.params[(std::size_t) pr::param (1, pr::fKnee)]) / 64.0 - 0.5) < 1e-5);
    CHECK (std::abs (pr::c4QOf (s.params) - 0.6) < 1e-6);
}

TEST (parity190mg_c4_crossovers_sum_to_an_all_pass_with_the_measured_look_ahead)
{
    using namespace lynx;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto s = parity190mgc4::c4Slot();
        std::vector<float> l (16384, 0.0f), r (16384, 0.0f);
        l[0] = r[0] = 1.0f;
        int lat = 0;
        parity190mgc4::render (s, sr, l, r, lat);
        double e = 0.0;
        int peak = 0;
        for (int i = 0; i < (int) l.size(); ++i)
        {
            e += (double) l[(std::size_t) i] * l[(std::size_t) i];
            if (std::abs (l[(std::size_t) i]) > std::abs (l[(std::size_t) peak]))
                peak = i;
        }
        CHECK (lat == dyn::prism::c4Lookahead (sr) && lat == (sr > 50000.0 ? 128 : 64));
        CHECK (std::abs (e - 1.0) < 1e-3); // all-pass: the impulse's energy is kept
        CHECK (peak >= lat);              // nothing before the look-ahead
    }
}

TEST (parity190mg_c4_static_gain_follows_the_reference_stair)
{
    using namespace lynx;
    namespace pr = dyn::prism;
    // the reference's 1 kHz stair, band 2 (threshold -30, range -12, knee 0.5, 0.5 / 50 ms): -1.66 dB at -36 dBFS peak,
    // -5.61 at -24, -9.72 at -9 (demo-mode probe sk05 of round 190mg)
    auto s = parity190mgc4::c4Slot();
    s.params[(std::size_t) pr::param (1, pr::fThreshold)] = pr::thresholdValue (-30.0);
    s.params[(std::size_t) pr::param (1, pr::fRange)] = (float) ((-12.0 + 30.0) / 60.0);
    s.params[(std::size_t) pr::param (1, pr::fKnee)] = (float) (0.5 * 64.0 / 48.0);
    s.params[(std::size_t) pr::param (1, pr::fAttack)] = pr::c4AttackValue (0.5);
    s.params[(std::size_t) pr::param (1, pr::fRelease)] = pr::c4ReleaseValue (50.0);
    for (const auto [levelDb, gainDb] : { std::pair { -36.0, -1.66 }, std::pair { -24.0, -5.61 }, std::pair { -9.0, -9.72 } })
    {
        const double sr = 48000.0, a = std::pow (10.0, levelDb / 20.0);
        std::vector<float> l (48000), r (48000);
        for (std::size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = (float) (a * std::sin (2.0 * M_PI * 1000.0 * (double) i / sr));
        int lat = 0;
        parity190mgc4::render (s, sr, l, r, lat);
        double e = 0.0;
        for (std::size_t i = 24000; i < 48000; ++i)
            e += (double) l[i] * l[i];
        const double out = 10.0 * std::log10 (2.0 * e / 24000.0) - levelDb;
        CHECK (std::abs (out - gainDb) < 0.15);
    }
}

TEST (parity190mg_c4_release_follows_the_reference_after_a_long_burst)
{
    using namespace lynx;
    namespace pr = dyn::prism;
    // the reference's 1 kHz burst probes (band 2: threshold -30, range -12, knee 0.5): -50 dBFS, 2 s at -10, back to -50;
    // the gain 100 ms after the burst is -3.12 dB (electro, attack 0.5 / release 50 ms) and 50 ms after it -3.08 dB
    // (opto, attack 5 / release 50 ms)
    for (const bool opto : { false, true })
    {
        auto s = parity190mgc4::c4Slot();
        s.params[(std::size_t) pr::c4Law] = pr::c4LawValue (opto);
        s.params[(std::size_t) pr::param (1, pr::fThreshold)] = pr::thresholdValue (-30.0);
        s.params[(std::size_t) pr::param (1, pr::fRange)] = (float) ((-12.0 + 30.0) / 60.0);
        s.params[(std::size_t) pr::param (1, pr::fKnee)] = (float) (0.5 * 64.0 / 48.0);
        s.params[(std::size_t) pr::param (1, pr::fAttack)] = pr::c4AttackValue (opto ? 5.0 : 0.5);
        s.params[(std::size_t) pr::param (1, pr::fRelease)] = pr::c4ReleaseValue (50.0);
        const double sr = 48000.0;
        const int n = 48000 * 4, on = 48000, off = 48000 * 3;
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n), x ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double a = std::pow (10.0, (i >= on && i < off ? -10.0 : -50.0) / 20.0);
            x[(std::size_t) i] = l[(std::size_t) i] = r[(std::size_t) i] = (float) (a * std::sin (2.0 * M_PI * 1000.0 * i / sr));
        }
        int lat = 0;
        parity190mgc4::render (s, sr, l, r, lat);
        const int t = off + (opto ? 2400 : 4800);
        double ey = 0.0, ex = 0.0;
        for (int i = t; i < t + 48; ++i)
        {
            ey += (double) l[(std::size_t) (i + lat)] * l[(std::size_t) (i + lat)];
            ex += (double) x[(std::size_t) i] * x[(std::size_t) i];
        }
        CHECK (std::abs (10.0 * std::log10 (ey / ex) - (opto ? -3.08 : -3.12)) < 0.3);
    }
}
