// Round 190mg (sub-round ren): REA of the Waves Renaissance Compressor's DSP and control code (owner's permission of
// 2026-11-17; /mnt/e/lynx-work/waves3_190mg/ren/HANDOFF.md, docs/analise/REA_rodada190mg_ren.md).
// - Compressor's vintage voicing gets a hidden law [20] (compressorRenLaw): 1 = the Renaissance law (LYNX's model of the
//   reference's structure, every constant fitted on our demo-mode probes). 0 for new instances and every project saved
//   before (bit for bit: ren/lab/gold.sh); only the Renaissance Compressor translator sets it.
// - The numbers below are the reference's, read from our probes (ren/cap/p1: 1 kHz stairs, 1 kHz bursts −40 → −10 dBFS).
#include <cmath>
#include <vector>

#include "core/Effects.h"
#include "core/VoiceMatch.h"

namespace parity190mgren
{
// a Renaissance Compressor state (its RealWorld list) through the product translator
inline lynx::EffectSlot rcomp (double rel, double thr, double gain, double att, double ratio, bool smooth, bool manual, bool opto,
                               double mix, int& n)
{
    char text[400];
    std::snprintf (text, sizeof (text), "%g %g %g * * * * * %g %g 56.23 10 0 0 149.62 120 %d %d 1 0.4 0 1.5 %d * * * %g 0", rel, thr, gain,
                   att, ratio, smooth ? 1 : 0, manual ? 1 : 0, opto ? 1 : 0, mix);
    return vm123::wavesSlot ("RComp", text, n);
}
inline std::vector<float> render (const lynx::EffectSlot& s, const std::vector<float>& x, double sr = 48000.0)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    std::vector<float> l = x, r = x;
    for (std::size_t off = 0; off < l.size(); off += 256)
        fx->process (l.data() + off, r.data() + off, (int) std::min<std::size_t> (256, l.size() - off), s.params, 120.0);
    return l;
}
// the bursts probe: 1 kHz at −40 dBFS, a burst to −10 dBFS of `ms`, then −40 dBFS again; returns the signal and the burst's end
inline std::vector<float> burst (double ms, std::size_t& end)
{
    const std::size_t a = 48000, b = a + (std::size_t) (ms * 48.0), n = b + 48000;
    std::vector<float> x (n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = (float) ((i >= a && i < b ? 0.316228 : 0.01) * std::sin (2.0 * M_PI * 1000.0 * (double) i / 48000.0));
    end = b;
    return x;
}
// the gain (dB) over one 1 ms cycle starting `ms` after sample `at` (the output is 64 samples late)
inline double gainAt (const std::vector<float>& y, const std::vector<float>& x, std::size_t at, double ms)
{
    const auto i = (std::size_t) ((double) at + ms * 48.0);
    double ey = 0.0, ex = 0.0;
    for (std::size_t k = 0; k < 48; ++k)
    {
        ey += (double) y[i + 64 + k] * y[i + 64 + k];
        ex += (double) x[i + k] * x[i + k];
    }
    return 10.0 * std::log10 (ey / ex);
}
} // namespace parity190mgren

TEST (parity190mg_ren_law_is_hidden_and_off_for_new_and_old_instances)
{
    using namespace lynx;
    CHECK (effectInfo (EffectKind::compressor).params.size() == 20 && compressorRenLaw == 20); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::compressor);
    CHECK (s.params[(std::size_t) compressorRenLaw] == 0.0f && savedEffectParamCount (s) == 20);
    // a project saved before the law (20 values or fewer) loads with it off, even if the slot held a stray value
    s.params[(std::size_t) compressorRenLaw] = 1.0f;
    CHECK (savedEffectParamCount (s) == 21);
    upgradeLoadedEffectParams (s, 20);
    CHECK (s.params[(std::size_t) compressorRenLaw] == 0.0f);
}

TEST (parity190mg_ren_rcomp_is_translated_with_the_law)
{
    using namespace lynx;
    int n = 0;
    const auto s = parity190mgren::rcomp (160.31, -24.0, 0.0, 16.03, 4.0, false, false, false, 100.0, n);
    CHECK (n == 6 && s.kind == EffectKind::compressor && s.params[8] == 1.0f && s.params[13] == 1.0f);
    CHECK (s.params[(std::size_t) compressorRenLaw] == 1.0f && savedEffectParamCount (s) == 21);
    // the look-ahead window = the reported latency: 64 samples under 64 kHz, 128 at 96 kHz
    for (const auto [sr, lat] : { std::pair { 44100.0, 64 }, std::pair { 48000.0, 64 }, std::pair { 96000.0, 128 } })
    {
        auto fx = makeEffect (s);
        fx->prepare (sr);
        CHECK (fx->latencySamples() == lat);
    }
}

TEST (parity190mg_ren_static_curve_matches_the_reference_stairs)
{
    using namespace lynx;
    // [EXEC] steady 1 kHz gain (dB) of the reference (Manual, 0.5 / 50 ms, threshold −30, Warm) at −39 … −3 dBFS
    struct Row { double ratio; double db[7]; };
    static constexpr double levels[7] { -39, -33, -27, -21, -15, -9, -3 };
    static constexpr Row rows[] {
        { 1.5, { -0.3, -1.3, -3.1, -5.3, -7.2, -8.5, -9.2 } },
        { 4.0, { -0.4, -1.9, -4.8, -8.9, -13.8, -18.5, -22.6 } },
        { 50.0, { -0.4, -2.0, -5.0, -9.4, -14.9, -20.8, -26.9 } },
        { 0.9, { 0.1, 0.4, 1.2, 2.4, 3.4, 4.1, 3.0 } }, // expansion; at −3 dBFS the 0 dBFS limiter
    };
    double worst = 0.0;
    for (const auto& row : rows)
    {
        int n = 0;
        const auto s = parity190mgren::rcomp (50.0, -30.0, 0.0, 0.5, row.ratio, false, true, false, 100.0, n);
        for (int k = 0; k < 7; ++k)
            worst = std::max (worst, std::abs (vm123::sineGainDb (s, 1000.0, levels[k]) - row.db[k]));
    }
    std::printf ("  Renaissance law static curve: worst %.3f dB against the reference's stairs\n", worst);
    CHECK (worst < 0.1);
    // the law's own pieces: no floor at 20 : 1 and above, the expansion's floor above 1, the threshold shift at low ratios
    CHECK (vmatch::progcomp::renFloor (20.0) == 0.0 && vmatch::progcomp::renFloor (0.5) > 15.0 && vmatch::progcomp::renFloor (1.0) > 0.99);
    CHECK (std::abs (vmatch::progcomp::renShiftDb (0.5) - 15.0) < 1e-9 && vmatch::progcomp::renShiftDb (2.0) == 0.0);
}

TEST (parity190mg_ren_release_follows_the_reference_bursts)
{
    using namespace lynx;
    namespace t = parity190mgren;
    // [EXEC] the reference at 4 : 1, −30 dB, 16 / 160 ms: gain (dB) 5 / 50 / 200 ms after a burst's end
    struct Case { bool manual, opto; double ms; double db[3]; };
    static constexpr Case cases[] {
        { true, false, 2000.0, { -16.22, -9.01, -2.25 } },  // Manual Electro, 2 s burst: slow from deep reduction
        { false, true, 2000.0, { -2.23, -0.89, -0.51 } },   // ARC Opto: fast from deep reduction, slow near 0 dB
        { false, false, 100.0, { -5.54, -2.86, -0.28 } },   // ARC Electro, 100 ms burst
        { true, true, 100.0, { -10.46, -4.69, -1.63 } },    // Manual Opto, 100 ms burst
    };
    double worst = 0.0;
    for (const auto& c : cases)
    {
        int n = 0;
        const auto s = t::rcomp (160.0, -30.0, 0.0, 16.0, 4.0, false, c.manual, c.opto, 100.0, n);
        std::size_t end = 0;
        const auto x = t::burst (c.ms, end);
        const auto y = t::render (s, x);
        static constexpr double at[3] { 5.0, 50.0, 200.0 };
        for (int k = 0; k < 3; ++k)
            worst = std::max (worst, std::abs (t::gainAt (y, x, end, at[k]) - c.db[k]));
    }
    std::printf ("  Renaissance law release: worst %.3f dB against the reference's bursts\n", worst);
    CHECK (worst < 0.1);
}

TEST (parity190mg_ren_limiter_mix_and_gain)
{
    using namespace lynx;
    // 0.50 : 1 expands upward and its limiter holds the output at 0 dBFS (−9 dBFS in → +9 dB)
    int n = 0;
    const auto e = parity190mgren::rcomp (50.0, -30.0, 0.0, 0.5, 0.5, false, true, false, 100.0, n);
    CHECK (std::abs (vm123::sineGainDb (e, 1000.0, -9.0) - 9.0) < 0.15);
    // mix 40 % with +6 dB of gain: below the threshold 0.4 · 2 + 0.6 = +2.92 dB (the reference: +2.9)
    const auto m = parity190mgren::rcomp (50.0, -30.0, 6.0, 0.5, 10.0, false, true, false, 40.0, n);
    CHECK (std::abs (vm123::sineGainDb (m, 1000.0, -51.0) - 2.92) < 0.05);
    CHECK (std::abs (vm123::sineGainDb (m, 1000.0, -12.0) - (-3.0)) < 0.15);
}
