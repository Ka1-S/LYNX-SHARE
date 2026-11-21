// Round 190oc: the Kilohearts Nonlinear Filter as a hidden law of Effector (core/KiloheartsDsp.h, code 49 / 64):
// DirtyFilterDsp read in HeartCore (docs/analise/REA_rodada190oc.md), LYNX's own half-band around it, translated from the
// snapin's state (core/KiloheartsStates.inc). Uses parity190lnkhs::state / load / named, parity190nbkhs::run and
// parity190nhkhs::tone / energy / finite.

namespace parity190ockhs
{
inline std::string body (const char* type, const char* mode, double cutoff, double q, double drive)
{
    return std::string ("\"cutoff\": \"") + std::to_string (cutoff) + "\", \"filter_type\": \"" + type + "\", \"dirt_type\": \"" + mode
           + "\", \"q\": \"" + std::to_string (q) + "\", \"drive\": \"" + std::to_string (drive) + "\", \"$type\": \"dirty_filter\"";
}
inline lynx::EffectSlot filter (const char* type, const char* mode = "clean", double cutoff = 1000.0, double q = 0.707, double drive = 0.25)
{
    int n = 0;
    return parity190lnkhs::load ("kHs Nonlinear Filter", parity190lnkhs::state (body (type, mode, cutoff, q, drive)), n);
}
// the steady gain (dB) of a tone through the slot: the last half second's energy against the input's
inline double gainDb (const lynx::EffectSlot& s, double hz, float amp = 0.25f)
{
    const auto x = parity190nhkhs::tone (48000, hz, amp);
    const auto y = parity190nbkhs::run (s, x);
    double ex = 0.0, ey = 0.0;
    for (int i = 24000; i < 48000; ++i)
        ex += (double) x[(std::size_t) i] * x[(std::size_t) i], ey += (double) y[(std::size_t) i] * y[(std::size_t) i];
    return 10.0 * std::log10 (std::max (ey, 1e-30) / ex);
}
// the level (dB re the input's amplitude) of the partial at hz in the last half second (one-bin DFT, Hann)
inline double partialDb (const std::vector<float>& y, double hz, float amp)
{
    std::complex<double> acc {};
    double wsum = 0.0;
    for (int i = 24000; i < 48000; ++i)
    {
        const auto wv = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (i - 24000) / 24000.0);
        acc += wv * (double) y[(std::size_t) i] * std::polar (1.0, -2.0 * 3.14159265358979 * hz * i / 48000.0);
        wsum += wv;
    }
    return 20.0 * std::log10 (std::max (2.0 * std::abs (acc) / wsum / amp, 1e-12));
}
} // namespace parity190ockhs

TEST (parity190ockhs_law_code_and_translation)
{
    using namespace lynx;
    using namespace parity190ockhs;
    namespace k = khsdsp;
    // code 49 (kept for it by round 190nh); round 190nb's and 190nh's codes unchanged
    CHECK (k::lawCode (k::Law::nonlinearFilter) == 49 && k::lawValue (k::Law::nonlinearFilter) == 49.0f / 64.0f);
    CHECK (k::lawOf (49.0f / 64.0f) == k::Law::nonlinearFilter && k::lawOf (48.0f / 64.0f) == k::Law::ensemble && k::lawOf (50.0f / 64.0f) == k::Law::none);
    const auto s = filter ("notch", "fuzzy", 3620.386, 9.11803, 0.75);
    CHECK (s.kind == EffectKind::effector && k::lawOf (s.params[(std::size_t) k::lawParam]) == k::Law::nonlinearFilter);
    CHECK (std::lround (4.0f * s.params[(std::size_t) k::valueParam]) == 3 && std::lround (8.0f * s.params[(std::size_t) k::valueParam + 1]) == 6);
    CHECK (std::abs (k::enc::logv (s.params[(std::size_t) k::valueParam + 2], k::enc::nlfCutLo, k::enc::nlfCutHi) - 3620.386) < 0.01);
    CHECK (std::abs (k::enc::logv (s.params[(std::size_t) k::valueParam + 3], k::enc::nlfQLo, k::enc::nlfQHi) - 9.11803) < 1e-4);
    CHECK (std::abs (s.params[(std::size_t) k::valueParam + 4] - 0.75f) < 1e-6f);
    CHECK (std::lround (11.0f * s.params[(std::size_t) parity190lnkhs::named (s, "Type")]) == 4); // Effector's Filter
    // the plug-in's defaults (no keys): low pass, saturated, 440 Hz, Q 0.707, drive 0.25; no latency reported
    int n = 0;
    const auto d = parity190lnkhs::load ("kHs Nonlinear Filter", parity190lnkhs::state ("\"$type\": \"dirty_filter\""), n);
    CHECK (std::lround (4.0f * d.params[(std::size_t) k::valueParam]) == 0 && std::lround (8.0f * d.params[(std::size_t) k::valueParam + 1]) == 1);
    CHECK (std::abs (k::enc::logv (d.params[(std::size_t) k::valueParam + 2], k::enc::nlfCutLo, k::enc::nlfCutHi) - 440.0) < 0.01);
    CHECK (makeEffect (d)->latencySamples() == 0);
}

TEST (parity190ockhs_half_band_is_lynx_own_design)
{
    // LYNX's own elliptic half-band: eight coefficients rising in (0, 1); 109 dB down from 0.277 of the 2× rate, flat to
    // 0.223 (H (z) = ½ (A0 (z²) + z^-1 A1 (z²)), evaluated here)
    const auto a = lynx::khsdsp::Engine::nonlinearHalfBand();
    CHECK (a.size() == 8 && std::is_sorted (a.begin(), a.end()) && a.front() > 0.0 && a.back() < 1.0);
    const auto H = [&a] (double f) {
        const auto z = std::polar (1.0, 2.0 * 3.14159265358979 * f), z2 = z * z;
        std::complex<double> b0 (1.0), b1 (1.0);
        for (std::size_t i = 0; i < a.size(); ++i)
            (i % 2 == 0 ? b0 : b1) *= (a[i] + 1.0 / z2) / (1.0 + a[i] / z2);
        return std::abs (0.5 * (b0 + b1 / z));
    };
    double stop = 0.0, pass = 1.0;
    for (int i = 0; i <= 2000; ++i)
    {
        stop = std::max (stop, H (0.277 + (0.5 - 0.277) * i / 2000.0));
        pass = std::min (pass, H (0.223 * i / 2000.0));
    }
    CHECK (20.0 * std::log10 (stop) < -100.0 && 20.0 * std::log10 (pass) > -1e-6);
}

TEST (parity190ockhs_filter_types_shape_the_spectrum)
{
    using namespace parity190ockhs;
    // clean (linear): the low pass passes 100 Hz and cuts 10 kHz (12 dB an octave above 1 kHz), the high pass the other way
    const auto lp = filter ("low_pass"), hp = filter ("high_pass"), bp = filter ("band_pass"), notch = filter ("notch"), ap = filter ("all_pass");
    CHECK (std::abs (gainDb (lp, 100.0)) < 0.3 && gainDb (lp, 10000.0) < -35.0);
    CHECK (std::abs (gainDb (hp, 10000.0)) < 0.5 && gainDb (hp, 100.0) < -35.0);
    CHECK (gainDb (bp, 1000.0) > gainDb (bp, 100.0) + 15.0 && gainDb (bp, 1000.0) > gainDb (bp, 10000.0) + 15.0);
    CHECK (gainDb (notch, 1000.0) < -30.0 && std::abs (gainDb (notch, 100.0)) < 0.5);
    for (double hz : { 100.0, 1000.0, 5000.0 })
        CHECK (std::abs (gainDb (ap, hz)) < 0.5);
    // linear: twice the input, twice the output
    const auto x = parity190nhkhs::tone (24000, 300.0, 0.2f);
    auto x2 = x;
    for (auto& v : x2)
        v *= 2.0f;
    const auto y = parity190nbkhs::run (lp, x), y2 = parity190nbkhs::run (lp, x2);
    double err = 0.0;
    for (std::size_t i = 0; i < y.size(); ++i)
        err = std::max (err, (double) std::fabs (y2[i] - 2.0f * y[i]));
    CHECK (err < 1e-5);
}

TEST (parity190ockhs_modes_saturate_and_stay_bounded)
{
    using namespace parity190ockhs;
    static const char* modes[9] { "clean", "saturated", "tubular", "clipped", "warm", "biased", "fuzzy", "metallic", "digital" };
    static const char* types[5] { "low_pass", "band_pass", "high_pass", "notch", "all_pass" };
    const auto x = parity190nhkhs::tone (48000, 200.0, 0.5f);
    for (int m = 0; m < 9; ++m)
        for (int t = 0; t < 5; ++t)
            for (double drive : { 0.0, 1.0 })
            {
                const auto s = filter (types[t], modes[m], 800.0, 4.0, drive);
                std::vector<float> r;
                const auto y = parity190nbkhs::run (s, x, &r);
                CHECK (parity190nhkhs::finite (y) && parity190nhkhs::finite (r));
                CHECK (std::all_of (y.begin(), y.end(), [] (float v) { return std::fabs (v) < 8.0f; }));
                CHECK (parity190nhkhs::energy (y) > 1e-3 * parity190nhkhs::energy (x) || t == 3);
                CHECK (y == parity190nbkhs::run (s, x)); // (deterministic)
            }
    // the saturated mode at full drive bends a loud tone (its 3rd harmonic up), clean does not
    const auto sat = parity190nbkhs::run (filter ("low_pass", "saturated", 4000.0, 0.707, 1.0), x);
    const auto clean = parity190nbkhs::run (filter ("low_pass", "clean", 4000.0, 0.707, 1.0), x);
    CHECK (partialDb (sat, 600.0, 0.5f) > -50.0 && partialDb (clean, 600.0, 0.5f) < -90.0);
}
