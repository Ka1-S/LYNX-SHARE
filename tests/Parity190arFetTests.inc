// Round 190ar part 2: the Compressor's FET law (classic voicing, parameter "Classic law"; off by default). Measured
// black-box on FET-style references' tone-burst probes (docs/PARITY.md "Round 190ar, part 2"): a 5 dB knee and a release
// that falls by G / tail + G² / (10 dB · release) per second.
namespace p190arfet
{
using lynx::EffectKind;
using lynx::EffectParams;
using namespace dyn108;
constexpr int law = 19;

// a 1 kHz tone: `loud` dBFS for `burst` s, then −40 dBFS for `tail` s
inline std::vector<float> burstThenQuiet (double loud, double burst, double tail)
{
    std::vector<float> x ((std::size_t) ((burst + tail) * sr));
    const auto nb = (std::size_t) (burst * sr);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, (i < nb ? loud : -40.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    return x;
}
// the gain (dB) around `seconds`: output against input over 10 ms
inline double gainAt (const std::vector<float>& x, const std::vector<float>& y, double seconds)
{
    const auto a = (std::size_t) ((seconds - 0.005) * sr), b = (std::size_t) ((seconds + 0.005) * sr);
    return rmsDb (y, a, b) - rmsDb (x, a, b);
}
inline EffectParams settings (bool fet)
{
    auto p = defaults (EffectKind::compressor);
    p[0] = 0.7f;            // threshold −18 dB
    p[1] = 3.0f / 19.0f;    // 4 : 1
    p[2] = 0.0f;            // attack 0.1 ms
    p[3] = 0.5396f;         // release 120 ms
    p[law] = fet ? 1.0f : 0.0f;
    return p;
}
} // namespace p190arfet

TEST (p190ar_fet_law_is_off_by_default_and_changes_only_the_classic_compression)
{
    using namespace p190arfet;
    const auto info = lynx::effectInfo (EffectKind::compressor);
    CHECK (info.params.size() == 20 && std::string (info.params[law].name) == "Classic law" && info.params[law].defaultValue == 0.0f);
    CHECK (defaults (EffectKind::compressor)[law] == 0.0f);
    const auto x = burstThenQuiet (-3.0, 0.3, 0.6);
    // on: another sound
    CHECK (run (EffectKind::compressor, settings (true), x) != run (EffectKind::compressor, settings (false), x));
    // the vintage voicing and the expansion (ratio under 1 with the wide ranges) do not have it
    auto v = settings (false);
    v[8] = 1.0f;
    auto v1 = v;
    v1[law] = 1.0f;
    CHECK (run (EffectKind::compressor, v, x) == run (EffectKind::compressor, v1, x));
    auto e = settings (false);
    e[13] = 1.0f;
    e[14] = 0.9f; // 0.6 : 1
    auto e1 = e;
    e1[law] = 1.0f;
    CHECK (run (EffectKind::compressor, e, x) == run (EffectKind::compressor, e1, x));
}

TEST (p190ar_fet_law_knee_and_static_curve)
{
    using namespace p190arfet;
    // well over the threshold both laws reduce the same; at the threshold the FET knee already takes slope · 5 / 8 dB
    for (const double level : { -6.0, 0.0 })
    {
        const auto x = burstThenQuiet (level, 0.5, 0.05);
        const auto gc = gainAt (x, run (EffectKind::compressor, settings (false), x), 0.45);
        const auto gf = gainAt (x, run (EffectKind::compressor, settings (true), x), 0.45);
        CHECK (std::abs (gc - gf) < 0.35);
        CHECK (gc < -0.75 * (level + 18.0) + 1.5 && gc > -0.75 * (level + 18.0) - 0.5);
    }
    const auto x = burstThenQuiet (-18.0, 0.5, 0.05);
    const auto gc = gainAt (x, run (EffectKind::compressor, settings (false), x), 0.45);
    const auto gf = gainAt (x, run (EffectKind::compressor, settings (true), x), 0.45);
    CHECK (std::abs (gc) < 0.05);
    CHECK (gf < -0.25 && gf > -0.6); // 0.75 · 5 / 8 = 0.47 dB at the peak
    // under the knee: nothing
    const auto q = burstThenQuiet (-24.0, 0.3, 0.05);
    CHECK (std::abs (gainAt (q, run (EffectKind::compressor, settings (true), q), 0.25)) < 0.02);
}

TEST (p190ar_fet_law_release_is_fast_from_deep_reduction_and_ends_like_the_classic)
{
    using namespace p190arfet;
    // 13.5 dB of reduction, then quiet: after one release time (120 ms) the classic law still holds 13.5 / e = 5.0 dB,
    // the FET law 1 / ((1 / 13.5 + 1 / 10) e − 1 / 10) = 2.7 dB
    const auto x = burstThenQuiet (0.0, 0.5, 1.5);
    const auto yc = run (EffectKind::compressor, settings (false), x), yf = run (EffectKind::compressor, settings (true), x);
    const auto c1 = -gainAt (x, yc, 0.62), f1 = -gainAt (x, yf, 0.62);
    CHECK (c1 > 4.3 && c1 < 5.6);
    CHECK (f1 > 2.2 && f1 < 3.2);
    // a small reduction recovers with the release time itself: from 1.5 dB, e-fold in 120 ms within the G² term (1.15 ×)
    const auto s = burstThenQuiet (-16.0, 0.5, 1.0);
    const auto ys = run (EffectKind::compressor, settings (true), s);
    const auto g0 = -gainAt (s, ys, 0.49), g1 = -gainAt (s, ys, 0.62);
    CHECK (g0 > 1.2 && g0 < 2.3);
    CHECK (g1 / g0 > 0.25 && g1 / g0 < 0.40);
    // and it ends: no reduction left after a second, nothing but finite samples
    CHECK (std::abs (gainAt (x, yf, 1.9)) < 0.02);
    for (const auto v : yf)
        CHECK (std::isfinite (v));
    // the slowest release of the wide ranges (5 s): the tail is held to 2.7 s, so 10 dB is not held for ever
    auto w = settings (true);
    w[13] = 1.0f;
    w[14] = 0.2f;  // about 14 : 1
    w[16] = 0.0f;
    w[17] = 1.0f;  // 5 s
    const auto l = burstThenQuiet (0.0, 0.5, 6.0);
    const auto yl = run (EffectKind::compressor, w, l);
    const auto d0 = -gainAt (l, yl, 0.49), d3 = -gainAt (l, yl, 3.5);
    CHECK (d0 > 14.0);
    CHECK (d3 > 2.0 && d3 < 0.45 * d0);
}
