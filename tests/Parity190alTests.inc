// --- Round 190al: Crest's Two-way works in three bands, as the reference does (fourth-order Linkwitz-Riley splits at
// 88 Hz and 2.5 kHz, each band with its own measured law; docs/PARITY.md "Round 190al"). The numbers below are round
// trial190k's reference renders: the 100 Hz and 8 kHz stair gains (dB, the last 110 ms of each 0.3 s step). ---------------

#include "core/DynamicsSuite.h"

namespace p190al
{
using lynx::EffectKind;
using lynx::EffectParams;
using namespace dyn108;

// tone (Hz), threshold (dB), ratio, knee (dB), attack (ms) and the gain at −48, −36, −24, −12, −6 and 0 dBFS (release 200 ms)
struct Row
{
    double hz, thr, ratio, knee, att;
    double gain[6];
};

inline EffectParams twoWay (double thr, double ratio, double knee, double att)
{
    namespace c = lynx::dyn::crest;
    auto p = p190v::defaults (EffectKind::crestCompressor);
    p[c::style] = c::styleValue (c::Style::twoWay);
    p[c::threshold] = (float) ((thr + 60.0) / 60.0);
    p[c::ratio] = c::ratioValue (ratio);
    p[c::knee] = (float) (knee / 72.0);
    p[c::attack] = c::attackValue (att);
    p[c::release] = c::releaseValue (200.0);
    p[c::autoGain] = 0.0f;
    return p;
}
} // namespace p190al

TEST (p190al_crest_two_way_bands_follow_the_reference)
{
    using namespace dyn108;
    // a 100 Hz tone sits in the low and the mid band, an 8 kHz tone in the high band, whose downward part starts
    // 4.6 dB lower than the mid band's and follows the ratio itself (the single-band law of round 190v read the 8 kHz
    // stairs up to 6.7 dB too loud)
    const p190al::Row rows[] {
        { 100, -24, 4, 0, 10, { 5.23, 0.96, -2.83, -5.43, -9.18, -12.93 } },
        { 8000, -24, 4, 0, 10, { 3.48, -0.80, -3.79, -12.58, -16.93, -21.24 } },
        { 100, -36, 4, 0, 10, { 4.12, 0.33, -2.27, -9.77, -13.52, -17.27 } },
        { 8000, -36, 4, 0, 10, { 2.37, -0.63, -9.43, -18.08, -22.34, -26.52 } },
        { 100, -24, 10, 18, 10, { 7.75, 1.89, -2.75, -7.58, -11.97, -17.07 } },
        { 8000, -24, 10, 18, 10, { 5.55, -0.22, -5.71, -14.79, -19.91, -24.96 } },
        { 100, -24, 2, 18, 10, { 3.58, 0.68, -1.59, -3.22, -4.52, -6.02 } },
        { 8000, -24, 2, 18, 10, { 2.35, -0.49, -3.35, -8.49, -11.44, -14.39 } },
        { 100, -12, 4, 72, 0.25, { 9.07, 5.13, 0.93, -3.81, -6.38, -9.09 } },
        { 8000, -12, 4, 72, 0.25, { 7.49, 3.02, -2.18, -8.14, -11.38, -14.79 } },
    };
    const std::vector<double> levels { -48, -36, -24, -12, -6, 0 };
    double worst = 0.0;
    for (const auto& row : rows)
    {
        std::size_t per = 0;
        const auto x = stair (levels, 0.5, row.hz, per);
        const auto g = stairGains (x, run (lynx::EffectKind::crestCompressor, p190al::twoWay (row.thr, row.ratio, row.knee, row.att), x), per, levels.size());
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            const auto err = std::abs (g[i] - row.gain[i]);
            worst = std::max (worst, err);
            if (err > 0.8)
                std::printf ("  %g Hz threshold %g ratio %g knee %g attack %g at %g dBFS: %.2f dB (measured %.2f)\n", row.hz, row.thr,
                             row.ratio, row.knee, row.att, levels[i], g[i], row.gain[i]);
        }
    }
    std::printf ("  Two-way bands against the grid: worst error %.2f dB over %d points\n", worst, (int) std::size (rows) * 6);
    CHECK (worst <= 0.8);
}

TEST (p190al_crest_two_way_bands_sum_to_the_input_and_the_classic_law_has_none)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    // at ratio 1 no band does anything: the three bands add up to the input's level at every frequency (through the
    // splits' all-pass phase), silence stays silence and nothing is delayed
    auto p = p190al::twoWay (-24.0, 1.0, 0.0, 10.0);
    for (const double hz : { 40.0, 88.0, 300.0, 2500.0, 9000.0 })
    {
        std::size_t per = 0;
        const std::vector<double> levels { -30, -6 };
        const auto x = stair (levels, 0.5, hz, per);
        const auto g = stairGains (x, run (lynx::EffectKind::crestCompressor, p, x), per, levels.size());
        CHECK (std::abs (g[0]) < 0.05 && std::abs (g[1]) < 0.05);
    }
    const std::vector<float> quiet ((std::size_t) sr / 2, 0.0f);
    for (const auto v : run (lynx::EffectKind::crestCompressor, p190al::twoWay (-24.0, 4.0, 0.0, 10.0), quiet))
        CHECK (v == 0.0f);
    // the classic law (a project saved before round 190v) is one band: a tone comes out with the phase it went in with
    auto classic = p190al::twoWay (-24.0, 1.0, 0.0, 10.0);
    classic[c::law] = 0.0f;
    std::size_t per = 0;
    const auto x = stair ({ -20 }, 0.5, 88.0, per);
    const auto yClassic = run (lynx::EffectKind::crestCompressor, classic, x);
    const auto yBands = run (lynx::EffectKind::crestCompressor, p, x);
    double dClassic = 0.0, dBands = 0.0;
    for (std::size_t i = per / 2; i < per; ++i)
    {
        dClassic = std::max (dClassic, (double) std::abs (yClassic[i] - x[i]));
        dBands = std::max (dBands, (double) std::abs (yBands[i] - x[i]));
    }
    CHECK (dClassic < 1e-4);
    CHECK (dBands > 0.05); // 88 Hz sits on the low split: half a turn of phase
}

TEST (p190al_crest_vari_mu_slow_attack_starts_at_once)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    // a −40 → −6 dBFS step at threshold −24, ratio 4, knee 0: at attack 100 ms the reference is 2.1 dB down after 15 ms
    // and 4.8 dB after 30 ms, on its way to −7.8; the smoothed level alone did nothing for 45 ms and ended at −6.7.
    // At attack 10 ms nothing changes (the smoothed level is the match there)
    std::vector<float> x ((std::size_t) (2 * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, (i >= (std::size_t) (0.5 * sr) ? -6.0 : -40.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    auto p = p190v::defaults (lynx::EffectKind::crestCompressor);
    p[c::style] = c::styleValue (c::Style::variMu);
    p[c::threshold] = 0.6f;
    p[c::ratio] = c::ratioValue (4.0);
    p[c::knee] = 0.0f;
    p[c::attack] = c::attackValue (100.0);
    p[c::release] = c::releaseValue (200.0);
    p[c::autoGain] = 0.0f;
    const auto y = run (lynx::EffectKind::crestCompressor, p, x);
    const auto gainAt = [&] (double ms) {
        const auto i = (std::size_t) ((0.5 + ms * 0.001) * sr);
        return rmsDb (y, i - 240, i + 240) - rmsDb (x, i - 240, i + 240);
    };
    std::printf ("  Vari-mu attack 100 ms: %.1f dB after 15 ms, %.1f after 30 ms, %.1f after 60 ms, %.1f settled (reference -2.1 / -4.8 / -7.0 / -7.8)\n",
                 gainAt (15.0), gainAt (30.0), gainAt (60.0), gainAt (1400.0));
    CHECK (gainAt (15.0) < -0.8 && gainAt (30.0) < -2.0);
    CHECK (std::abs (gainAt (1400.0) + 7.8) < 0.8);
}
