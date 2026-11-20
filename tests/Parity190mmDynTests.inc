// Round 190mm (sub-round dyn): Waves Renaissance Vox, RDeEsser, C6, API-2500, MaxxBass, Renaissance Bass and Vitamin
// get LYNX stand-ins (/mnt/e/lynx-work/waves4_190mm/dyn/HANDOFF.md, docs/analise/REA_rodada190mm_dyn.md). Each runs a
// hidden law (LYNX's own model of the structure REA read in the reference's DSP, every constant fitted on our
// demo-mode probes); 0 for new instances and every project saved before. The numbers checked below are the
// references', read from the round's bank (cap/out/<tag>/<S>_stim.wav: 1 kHz stairs, the sweep's magnitude).
#include <cmath>
#include <vector>

#include "core/Effects.h"
#include "core/DynamicsSuite.h"
#include "core/MixSuite.h"
#include "core/LowLifterFl.h"

namespace parity190mmdyn
{
inline std::vector<float> render (const lynx::EffectSlot& s, std::vector<float> l, double sr = 48000.0)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    fx->reset();
    std::vector<float> r = l;
    for (std::size_t off = 0; off < l.size(); off += 256)
        fx->process (l.data() + off, r.data() + off, (int) std::min<std::size_t> (256, l.size() - off), s.params, 120.0);
    return l;
}
inline std::vector<float> sine (double hz, double db, double seconds = 0.8, double sr = 48000.0)
{
    std::vector<float> x ((std::size_t) (seconds * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, db / 20.0) * std::sin (2.0 * M_PI * hz * (double) i / sr));
    return x;
}
// the steady gain (dB) of the last 0.2 s
inline double gainDb (const lynx::EffectSlot& s, double hz, double db, double seconds = 0.8)
{
    const auto x = sine (hz, db, seconds);
    const auto y = render (s, x);
    double ey = 0.0, ex = 0.0;
    for (auto i = x.size() - 9600; i < x.size(); ++i)
    {
        ey += (double) y[i] * y[i];
        ex += (double) x[i] * x[i];
    }
    return 10.0 * std::log10 (ey / ex);
}
// the share (dB) of a render's energy near `hz` (a ±3 Hz DFT window over the last second) against its total
inline double toneDb (const std::vector<float>& y, double hz, double sr = 48000.0)
{
    const std::size_t n = 48000, a = y.size() - n;
    double re = 0.0, im = 0.0, tot = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        re += y[a + i] * std::cos (2.0 * M_PI * hz * (double) i / sr);
        im += y[a + i] * std::sin (2.0 * M_PI * hz * (double) i / sr);
        tot += (double) y[a + i] * y[a + i];
    }
    return 10.0 * std::log10 ((re * re + im * im) * 2.0 / n / tot + 1e-30);
}
} // namespace parity190mmdyn

TEST (parity190mm_dyn_laws_are_hidden_and_off_for_new_and_old_instances)
{
    using namespace lynx;
    auto c = makeEffectSlot (EffectKind::compressor);
    CHECK (effectInfo (EffectKind::compressor).params.size() == 20 && savedEffectParamCount (c) == 20);
    for (int k = compressorVoxLaw; k < compressorApiEnd; ++k)
        CHECK (c.params[(std::size_t) k] == 0.0f);
    c.params[(std::size_t) compressorVoxLaw] = 1.0f;
    c.params[(std::size_t) compressorApiLaw] = 1.0f;
    upgradeLoadedEffectParams (c, 20); // a project saved before the round: both laws off
    CHECK (c.params[(std::size_t) compressorVoxLaw] == 0.0f && c.params[(std::size_t) compressorApiLaw] == 0.0f);
    auto h = makeEffectSlot (EffectKind::hushDeEsser);
    CHECK (h.params[(std::size_t) mixsuite::hush::essLaw] == 0.0f);
    h.params[(std::size_t) mixsuite::hush::essLaw] = 1.0f;
    upgradeLoadedEffectParams (h, (std::size_t) mixsuite::hush::essLaw);
    CHECK (h.params[(std::size_t) mixsuite::hush::essLaw] == 0.0f);
    auto ll = makeEffectSlot (EffectKind::flLowLifter);
    CHECK (ll.params[(std::size_t) lowlifter::bassLaw] == 0.0f && savedEffectParamCount (ll) == (std::size_t) lowlifter::numParams);
    auto a = makeEffectSlot (EffectKind::aether);
    CHECK (a.params[(std::size_t) mixsuite::aether::vitLaw] == 0.0f);
    auto p = makeEffectSlot (EffectKind::prismMultiband);
    CHECK (p.params[(std::size_t) dyn::prism::c6Param (0, dyn::prism::c6On)] == 0.0f);
}

TEST (parity190mm_dyn_rvox_static_law_matches_the_reference_stairs)
{
    using namespace lynx;
    // the bank's X1 (Compression −10 dB) / X2 (−20 dB) / X4 (−25 dB, Gain −3 dB) states: 1 kHz stairs (top steps, settled)
    int n = 0;
    const auto x1 = vm123::wavesSlot ("Renaissance Vox", "-10 -80 130 * * * * 0 *", n);
    CHECK (n == 4 && x1.kind == EffectKind::compressor && x1.params[(std::size_t) compressorVoxLaw] == 1.0f);
    CHECK (std::abs (parity190mmdyn::gainDb (x1, 1000.0, -6.0) - 3.5) < 0.15);
    CHECK (std::abs (parity190mmdyn::gainDb (x1, 1000.0, 0.0) + 0.1) < 0.1);
    const auto x2 = vm123::wavesSlot ("Renaissance Vox", "-20 -80 130 * * * * 0 *", n);
    CHECK (std::abs (parity190mmdyn::gainDb (x2, 1000.0, -6.0) - 5.1) < 0.2);
    const auto x4 = vm123::wavesSlot ("Renaissance Vox", "-25 -80 130 * * * * -3 *", n);
    CHECK (std::abs (parity190mmdyn::gainDb (x4, 1000.0, 0.0) + 3.1) < 0.15);
    // the look-ahead window = the latency (64 samples at 48 kHz, 128 at 96 kHz)
    auto fx = makeEffect (x1);
    fx->prepare (96000.0);
    std::vector<float> l (512), r (512);
    fx->process (l.data(), r.data(), 512, x1.params, 120.0);
    CHECK (fx->latencySamples() == 128);
    // the gate: a quiet tone under the gate threshold (X3: −50 dB) is pulled down, a louder one is not
    const auto x3 = vm123::wavesSlot ("Renaissance Vox", "-15 -50 130 * * * * 0 *", n);
    CHECK (parity190mmdyn::gainDb (x3, 1000.0, -60.0) < parity190mmdyn::gainDb (x3, 1000.0, -40.0) - 6.0);
}

TEST (parity190mm_dyn_rdeesser_splits_and_reduces_the_band_by_the_range)
{
    using namespace lynx;
    namespace h = mixsuite::hush;
    int n = 0;
    // the default state: −40 dB, range −16 dB, 5506 Hz, high-pass, split
    const auto s = vm123::wavesSlot ("RDeEsser", "-40 -16 -12 0 24.9 248.7 0.5 1 * * * 5506 0.42 0 0 0 * * * *", n);
    CHECK (n == 5 && s.kind == EffectKind::hushDeEsser && s.params[(std::size_t) h::essLaw] == 1.0f);
    CHECK (std::abs (parity190mmdyn::gainDb (s, 200.0, -10.0)) < 0.05);           // the low band passes
    CHECK (std::abs (parity190mmdyn::gainDb (s, 9000.0, -10.0) + 15.5) < 0.6);     // the band: the range (+ the low band's leak)
    auto fx = makeEffect (s);
    fx->prepare (48000.0);
    std::vector<float> l (512), r (512);
    fx->process (l.data(), r.data(), 512, s.params, 120.0);
    CHECK (fx->latencySamples() == 64);
    // wide-band mode reduces the whole signal (a 9 kHz tone pulls a 200 Hz one down with it)
    const auto w = vm123::wavesSlot ("RDeEsser", "-40 -16 -12 0 24.9 248.7 0.5 1 * * * 5506 0.42 0 1 0 * * * *", n);
    std::vector<float> x (38400);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.3 * std::sin (2.0 * M_PI * 9000.0 * (double) i / 48000.0) + 0.3 * std::sin (2.0 * M_PI * 200.0 * (double) i / 48000.0));
    const auto y = parity190mmdyn::render (w, x);
    double ey = 0.0, ex = 0.0;
    for (auto i = x.size() - 9600; i < x.size(); ++i)
        ey += (double) y[i] * y[i], ex += (double) x[i] * x[i];
    CHECK (10.0 * std::log10 (ey / ex) < -10.0);
}

TEST (parity190mm_dyn_maxxbass_and_rbass_share_the_crossover)
{
    using namespace lynx;
    int n = 0;
    // MaxxBass default (80 Hz, harmonics off): the crossover's sum is an all-pass (the reference's sweep: 0.0 dB)
    const auto d = vm123::wavesSlot ("MaxxBass", "0 80 -15 20 1 0 * * 16 16 1 0 0 -144", n);
    CHECK (n == 4 && d.kind == EffectKind::flLowLifter && d.params[(std::size_t) lowlifter::bassLaw] == 1.0f);
    for (const double hz : { 40.0, 80.0, 1000.0 })
        CHECK (std::abs (parity190mmdyn::gainDb (d, hz, -12.0)) < 0.02);
    // X2: 100 Hz, original bass −6 dB → the reference's sweep: −6.0 dB at 25 Hz, −2.0 dB at 100 Hz, −0.3 dB at 150 Hz
    const auto x2 = vm123::wavesSlot ("MaxxBass", "0 100 -15 20 1 0 * * 16 16 1 0 -6 -144", n);
    CHECK (std::abs (parity190mmdyn::gainDb (x2, 25.0, -12.0, 1.5) + 6.0) < 0.1);
    CHECK (std::abs (parity190mmdyn::gainDb (x2, 100.0, -12.0) + 2.0) < 0.15);
    CHECK (std::abs (parity190mmdyn::gainDb (x2, 150.0, -12.0) + 0.3) < 0.15);
    // Renaissance Bass: a 40 Hz tone gains harmonics above the crossover (80 Hz), the original kept
    const auto rb = vm123::wavesSlot ("Renaissance Bass", "0 80 30 0.63 0 6 0 0 1 * * * * 20 20 10 1 *", n);
    CHECK (n == 4 && rb.params[(std::size_t) lowlifter::bassMaxx] == 0.0f);
    const auto y = parity190mmdyn::render (rb, parity190mmdyn::sine (40.0, -12.0, 2.0));
    CHECK (parity190mmdyn::toneDb (y, 40.0) > -6.0);
    CHECK (parity190mmdyn::toneDb (y, 80.0) > -30.0 || parity190mmdyn::toneDb (y, 120.0) > -30.0);
}

TEST (parity190mm_dyn_vitamin_default_is_an_all_pass_and_direct_gain_scales)
{
    using namespace lynx;
    int n = 0;
    std::string vals;
    // 145 values: crossovers 90 / 500 / 2594 / 9310, bands A … E Off (91 … 95), direct 0 dB (100)
    for (int i = 0; i < 145; ++i)
    {
        const double v = i == 0 ? 90.0 : i == 1 ? 500.0 : i == 2 ? 2594.0 : i == 3 ? 9310.0 : 0.0;
        vals += (i ? " " : "") + std::to_string (v);
    }
    const auto d = vm123::wavesSlot ("Vitamin", vals.c_str(), n);
    CHECK (n == 10 && d.kind == EffectKind::aether && d.params[(std::size_t) mixsuite::aether::vitLaw] == 1.0f);
    for (const double hz : { 100.0, 1000.0, 5000.0 })
        CHECK (std::abs (parity190mmdyn::gainDb (d, hz, -12.0)) < 0.02);
    auto x3 = d;
    x3.params[(std::size_t) mixsuite::aether::vitDirect] = mixsuite::aether::vitDirectValue (-3.0);
    CHECK (std::abs (parity190mmdyn::gainDb (x3, 1000.0, -12.0) + 3.0) < 0.05);
}

TEST (parity190mm_dyn_c6_floating_band_and_api2500_law)
{
    using namespace lynx;
    namespace pr = dyn::prism;
    int n = 0;
    // C6 X2: floating band 1 at 300 Hz, −30 dB, range −6 dB; band 6 at 6 kHz, −40 dB, range −6 dB
    std::vector<double> rw (123, 0.0);
    rw[0] = 92; rw[1] = 4000; rw[2] = 11071; rw[3] = 0.6;
    for (int b = 0; b < 6; ++b)
    {
        const int o = 14 + 14 * b;
        rw[(std::size_t) o + 6] = b < 4 ? -8.0 : -6.0; rw[(std::size_t) o + 7] = 0.5; rw[(std::size_t) o + 8] = 16.03;
        rw[(std::size_t) o + 9] = 160.31; rw[(std::size_t) o + 10] = 1; rw[(std::size_t) o + 11] = 1;
    }
    rw[75] = -30; rw[89] = -40; rw[98] = 300; rw[99] = 1.65; rw[102] = 6000; rw[103] = 1.65;
    std::string vals;
    for (std::size_t i = 0; i < rw.size(); ++i)
        vals += (i ? " " : "") + std::to_string (rw[i]);
    const auto c6 = vm123::wavesSlot ("C6", vals.c_str(), n);
    CHECK (n == 50 && c6.kind == EffectKind::prismMultiband);
    CHECK (c6.params[(std::size_t) pr::c6Param (0, pr::c6On)] == 1.0f && c6.params[(std::size_t) pr::c6Param (1, pr::c6On)] == 1.0f);
    const double at300 = parity190mmdyn::gainDb (c6, 300.0, -10.0, 1.5);
    CHECK (at300 < -4.0 && at300 > -6.5); // the band-pass's centre: down by about its range
    // API-2500 X1 (−10, 4:1, 10 ms, 0.2 s, Med knee, Med thrust): a compressor with its automatic make-up
    const auto api = vm123::wavesSlot ("API-2500",
        "-10 3 5 2 0 1 0 1 1 6 * 0.5 0 0 0 0 6 0 * * * * * * * * * * 0 0 0 1 2 1 0 10 0.1 100 10 0 1 1", n);
    CHECK (n == 12 && api.kind == EffectKind::compressor && api.params[(std::size_t) compressorApiLaw] == 1.0f);
    const double lo = parity190mmdyn::gainDb (api, 1000.0, -40.0, 1.5), hi = parity190mmdyn::gainDb (api, 1000.0, -6.0, 1.5);
    CHECK (lo > 3.0 && hi < lo - 8.0); // make-up below the threshold, compression above it (the reference: +5.9 / −13.7 dB)
}
