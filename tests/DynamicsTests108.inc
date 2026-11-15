// REA_rodada108: LYNX's dynamics suite (core/DynamicsSuite.h: Crest Compressor, Prism Multiband, Sentry Gate),
// Ember's multiband mode and the FabFilter state translation (core/FabFilterStates.h). The targets are our black-box
// measurements of the reference plugins (docs/analise/MEASURE_fabfilter.md §2, §6, §7, §12); tolerances are wide
// enough for any IEEE compiler (gcc / clang / zig, -O2) — the checks compare levels in dB, never bit patterns, except
// where the design promises exact transparency.

#include "core/DynamicsSuite.h"
#include "core/FabFilterStates.h"

namespace dyn108
{
using lynx::EffectKind;
using lynx::EffectParams;
constexpr double sr = 48000.0;
constexpr double pi = 3.141592653589793;

inline EffectParams defaults (EffectKind kind) { return lynx::makeEffectSlot (kind).params; }

// runs a slot-built effect (latency known before the first block) on a dual-mono signal; returns the left output
// with the latency removed (the tail is zero-padded)
inline std::vector<float> run (EffectKind kind, const EffectParams& p, const std::vector<float>& x, int* latency = nullptr,
                               const std::vector<float>* sidechain = nullptr)
{
    auto slot = lynx::makeEffectSlot (kind);
    slot.params = p;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    fx->reset();
    auto l = x, r = x;
    for (std::size_t off = 0; off < l.size(); off += 512)
    {
        const auto m = (int) std::min<std::size_t> (512, l.size() - off);
        if (sidechain != nullptr)
            fx->setSidechain (sidechain->data() + off, sidechain->data() + off);
        fx->process (l.data() + off, r.data() + off, m, p, 120.0);
    }
    const auto lat = fx->latencySamples();
    if (latency != nullptr)
        *latency = lat;
    std::vector<float> y (l.size(), 0.0f);
    for (std::size_t i = 0; i + (std::size_t) lat < l.size(); ++i)
        y[i] = l[i + (std::size_t) lat];
    return y;
}

inline std::vector<float> stair (const std::vector<double>& levels, double hold, double hz, std::size_t& per)
{
    per = (std::size_t) (hold * sr);
    std::vector<float> x (per * levels.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, levels[i / per] / 20.0) * std::sin (2.0 * pi * hz * (double) i / sr));
    return x;
}

inline double rmsDb (const std::vector<float>& y, std::size_t a, std::size_t b)
{
    double s = 0.0;
    for (std::size_t i = a; i < b; ++i)
        s += (double) y[i] * y[i];
    return 10.0 * std::log10 (2.0 * s / (double) (b - a) + 1e-30);
}

// gain (dB) at the end of each stair step (last 0.2 s)
inline std::vector<double> stairGains (const std::vector<float>& x, const std::vector<float>& y, std::size_t per, std::size_t steps)
{
    std::vector<double> g;
    const auto tail = (std::size_t) (0.2 * sr);
    for (std::size_t k = 0; k < steps; ++k)
    {
        const auto e = (k + 1) * per;
        g.push_back (rmsDb (y, e - tail, e) - rmsDb (x, e - tail, e));
    }
    return g;
}

// amplitude of harmonic h of f over [a, a + n) (Hann window)
inline double harmonic (const std::vector<float>& y, double f, int h, std::size_t a, std::size_t n)
{
    double re = 0.0, im = 0.0, ws = 0.0;
    for (std::size_t i = a; i < a + n; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) (i - a) / (double) n);
        re += w * y[i] * std::cos (2.0 * pi * f * h * (double) i / sr);
        im += w * y[i] * std::sin (2.0 * pi * f * h * (double) i / sr);
        ws += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / ws;
}

inline std::vector<float> tone (double hz, double dbfs, double seconds)
{
    std::vector<float> x ((std::size_t) (seconds * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, dbfs / 20.0) * std::sin (2.0 * pi * hz * (double) i / sr));
    return x;
}

// time (ms) after `start` until the 1-period RMS gain has covered `frac` of its change (as the measurement script)
inline double timeTo (const std::vector<float>& x, const std::vector<float>& y, std::size_t start, double frac)
{
    const auto gainAt = [&] (std::size_t i) { return rmsDb (y, i - 24, i + 24) - rmsDb (x, i - 24, i + 24); };
    const auto g0 = gainAt (start - 48), g1 = gainAt (start + (std::size_t) (0.9 * sr));
    const auto target = g0 + frac * (g1 - g0);
    for (std::size_t i = start; i < start + (std::size_t) (0.95 * sr); i += 12)
        if ((g1 < g0 && gainAt (i) <= target) || (g1 >= g0 && gainAt (i) >= target))
            return (double) (i - start) / sr * 1000.0;
    return 1e9;
}

inline std::vector<std::uint8_t> ffbs (std::uint32_t count, const std::vector<std::pair<int, float>>& values)
{
    std::vector<float> v (count + 3, 0.0f);
    for (const auto& [k, x] : values)
        v[(std::size_t) k] = x;
    std::vector<std::uint8_t> b { 'F', 'F', 'B', 'S', 1, 0, 0, 0 };
    for (int s = 0; s < 4; ++s)
        b.push_back ((std::uint8_t) (count >> (8 * s)));
    for (auto f : v)
    {
        std::uint32_t u;
        std::memcpy (&u, &f, 4);
        for (int s = 0; s < 4; ++s)
            b.push_back ((std::uint8_t) (u >> (8 * s)));
    }
    return b;
}

inline std::vector<std::uint8_t> fabf (std::uint32_t count, const std::vector<std::pair<int, float>>& values)
{
    const std::string name = "Default Setting";
    std::vector<std::uint8_t> b { 'F', 'a', 'b', 'F', 2, 0, 0, 0, (std::uint8_t) name.size(), 0, 0, 0 };
    b.insert (b.end(), name.begin(), name.end());
    for (int s = 0; s < 4; ++s)
        b.push_back (0);
    for (int s = 0; s < 4; ++s)
        b.push_back ((std::uint8_t) (count >> (8 * s)));
    std::vector<float> v (count + 2, 0.0f);
    for (const auto& [k, x] : values)
        v[(std::size_t) k] = x;
    for (auto f : v)
    {
        std::uint32_t u;
        std::memcpy (&u, &f, 4);
        for (int s = 0; s < 4; ++s)
            b.push_back ((std::uint8_t) (u >> (8 * s)));
    }
    return b;
}

// JUCE's MemoryBlock::toBase64Encoding (the format LYNX's VST3 host saves)
inline std::string juceBase64 (const std::vector<std::uint8_t>& data)
{
    static const char* alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    std::string out = std::to_string (data.size()) + ".";
    const auto bits = data.size() * 8;
    for (std::size_t bit = 0; bit < bits; bit += 6)
    {
        int v = 0;
        for (int b = 0; b < 6 && bit + (std::size_t) b < bits; ++b)
            if ((data[(bit + (std::size_t) b) >> 3] >> ((bit + (std::size_t) b) & 7)) & 1)
                v |= 1 << b;
        out += alphabet[v];
    }
    return out;
}
} // namespace dyn108

TEST (dyn108_crest_static_curves_follow_the_measurements)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    // MEASURE_fabfilter §2.1 (threshold −18 dB, attack 10 ms, release 200 ms, knee 0, ratio 4): gain at −16 … 0 dBFS
    struct Row { c::Style style; double knee, ratio; double gain[6]; };
    const Row rows[] {
        { c::Style::clean, 0, 4, { -0.85, -2.07, -4.80, -7.67, -10.60, -12.08 } },
        { c::Style::allround, 0, 4, { -1.06, -2.37, -5.12, -7.92, -10.78, -12.20 } },
        { c::Style::punch, 0, 4, { -1.23, -2.61, -5.46, -8.38, -11.34, -12.82 } },
        { c::Style::upward, 0, 4, { -7.89, -7.89, -7.89, -7.89, -7.89, -7.89 } },
        { c::Style::twoWay, 0, 4, { -4.38, -4.54, -6.97, -9.40, -11.81, -13.02 } },
        { c::Style::variMu, 0, 4, { -0.16, -1.33, -3.87, -6.82, -9.95, -11.56 } },
        { c::Style::classic, 0, 4, { -0.64, -1.96, -4.94, -8.04, -11.19, -12.79 } },
        { c::Style::opto, 0, 4, { -1.18, -2.62, -5.57, -8.54, -11.53, -13.03 } },
        { c::Style::vocal, 0, 4, { -5.79, -6.87, -9.47, -12.50, -15.84, -17.59 } },
        { c::Style::bus, 0, 4, { -1.48, -2.98, -5.97, -8.97, -11.97, -13.47 } },
        { c::Style::pumping, 0, 4, { -1.50, -3.00, -6.00, -9.00, -12.00, -13.50 } },
        { c::Style::clean, 48, 4, { -2.90, -4.50, -6.44, -8.68, -11.19, -12.54 } },
        { c::Style::opto, 0, 100, { -1.69, -3.55, -7.49, -11.46, -15.45, -17.45 } },
        { c::Style::variMu, 0, 1.5, { -0.04, -0.16, -0.75, -1.96, -3.74, -4.79 } },
    };
    const std::vector<double> levels { -16, -14, -10, -6, -2, 0 };
    std::size_t per = 0;
    const auto x = stair (levels, 0.5, 1000.0, per);
    double worst = 0.0;
    for (const auto& row : rows)
    {
        auto p = defaults (EffectKind::crestCompressor);
        p[c::style] = c::styleValue (row.style);
        p[c::threshold] = 0.7f;
        p[c::ratio] = c::ratioValue (row.ratio);
        p[c::knee] = (float) (row.knee / 72.0);
        p[c::attack] = c::attackValue (10.0);
        p[c::release] = c::releaseValue (200.0);
        p[c::autoGain] = 0.0f;
        const auto g = stairGains (x, run (EffectKind::crestCompressor, p, x), per, levels.size());
        for (std::size_t k = 0; k < levels.size(); ++k)
        {
            const auto err = std::abs (g[k] - row.gain[k]);
            worst = std::max (worst, err);
            if (err > 1.3)
            {
                std::printf ("  style %d knee %g ratio %g at %g dBFS: %.2f dB (measured %.2f)\n", (int) row.style, row.knee, row.ratio,
                             levels[k], g[k], row.gain[k]);
                CHECK (false);
            }
        }
    }
    std::printf ("  Crest static curves: worst error %.2f dB over %d points\n", worst, (int) (std::size (rows) * levels.size()));
}

TEST (dyn108_crest_timing_follows_the_measurements)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    // §2.2: a 1 kHz burst −40 → −6 → −40 dBFS, threshold −30, ratio 4: 50 % attack and release times (ms)
    struct Row { c::Style style; double att, rel, att50, rel50; };
    const Row rows[] { { c::Style::clean, 10, 200, 4.08, 83.9 },  { c::Style::clean, 10, 1500, 4.42, 348.0 },
                       { c::Style::allround, 10, 200, 8.04, 61.6 }, { c::Style::punch, 10, 200, 4.38, 72.6 },
                       { c::Style::classic, 10, 200, 4.52, 103.9 }, { c::Style::opto, 10, 200, 4.83, 101.0 },
                       { c::Style::opto, 10, 50, 4.65, 24.1 },      { c::Style::vocal, 10, 200, 13.62, 127.7 },
                       { c::Style::bus, 10, 200, 3.73, 238.7 },     { c::Style::bus, 10, 1500, 3.69, 461.6 },
                       { c::Style::pumping, 10, 200, 1.29, 158.1 }, { c::Style::variMu, 10, 200, 5.60, 35.9 },
                       { c::Style::clean, 30, 200, 11.15, 66.0 } };
    std::vector<float> x ((std::size_t) (4 * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, (i >= (std::size_t) sr && i < (std::size_t) (2 * sr) ? -6.0 : -40.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    for (const auto& row : rows)
    {
        auto p = defaults (EffectKind::crestCompressor);
        p[c::style] = c::styleValue (row.style);
        p[c::threshold] = 0.5f;
        p[c::ratio] = c::ratioValue (4.0);
        p[c::attack] = c::attackValue (row.att);
        p[c::release] = c::releaseValue (row.rel);
        p[c::autoGain] = 0.0f;
        const auto y = run (EffectKind::crestCompressor, p, x);
        const auto a = timeTo (x, y, (std::size_t) sr, 0.5), r = timeTo (x, y, (std::size_t) (2 * sr), 0.5);
        std::printf ("  style %d attack %g release %g: 50 %% attack %.1f ms (%.1f), release %.1f ms (%.1f)\n", (int) row.style, row.att,
                     row.rel, a, row.att50, r, row.rel50);
        CHECK (a > row.att50 * 0.55 - 0.6 && a < row.att50 * 1.6 + 0.6);
        CHECK (r > row.rel50 * 0.6 && r < row.rel50 * 1.6);
    }
}

TEST (dyn108_crest_latency_transparency_and_lookahead)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    // below the threshold, without character / oversampling / look-ahead / auto gain: the input, exactly
    auto p = defaults (EffectKind::crestCompressor);
    p[c::autoGain] = 0.0f;
    const auto x = tone (440.0, -40.0, 0.5);
    int latency = -1;
    auto y = run (EffectKind::crestCompressor, p, x, &latency);
    CHECK (latency == 0);
    double err = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
        err = std::max (err, (double) std::abs (y[i] - x[i]));
    CHECK (err == 0.0);

    // the latency is the maximum look-ahead (20 ms = 960 samples at 48 kHz, whatever the look-ahead knob says, as
    // measured), plus the oversampler when on; known as soon as the slot's effect exists
    p[c::maxLookahead] = 1.0f;
    p[c::lookahead] = 0.25f;
    auto slot = lynx::makeEffectSlot (EffectKind::crestCompressor);
    slot.params = p;
    CHECK (lynx::makeEffect (slot)->latencySamples() == 960);
    y = run (EffectKind::crestCompressor, p, x, &latency);
    CHECK (latency == 960);
    err = 0.0;
    for (std::size_t i = 0; i + 960 < x.size(); ++i)
        err = std::max (err, (double) std::abs (y[i] - x[i]));
    CHECK (err == 0.0);
    p[c::oversampling] = 2.0f / 3.0f;
    run (EffectKind::crestCompressor, p, x, &latency);
    CHECK (latency == 960 + lynx::mixsuite::Oversampler::latency());

    // look-ahead: the gain is already down when a step arrives (fast attack, 5 ms ahead)
    p = defaults (EffectKind::crestCompressor);
    p[c::autoGain] = 0.0f;
    p[c::threshold] = 0.5f;
    p[c::attack] = c::attackValue (1.0);
    p[c::maxLookahead] = 0.6f; // 5 ms
    p[c::lookahead] = 0.25f;   // 5 ms
    std::vector<float> step ((std::size_t) sr);
    for (std::size_t i = 0; i < step.size(); ++i)
        step[i] = (float) (std::pow (10.0, (i >= 24000 ? -6.0 : -60.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    y = run (EffectKind::crestCompressor, p, step, &latency);
    CHECK (latency == 240);
    const auto firstPeak = rmsDb (y, 24000, 24048) - rmsDb (step, 24000, 24048);
    std::printf ("  look-ahead 5 ms: gain over the first period of the step %.1f dB\n", firstPeak);
    CHECK (firstPeak < -6.0);
}

TEST (dyn108_crest_side_chain_eq_shapes_the_detector)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    // §12.1: the side-chain EQ applies Pro-Q prototypes to the detector: a 12 dB/oct low cut at 1 kHz keeps a 100 Hz
    // tone 20 dB over the threshold uncompressed; a +12 dB bell at 1 kHz moves the onset 12 dB lower for a 1 kHz tone
    auto p = defaults (EffectKind::crestCompressor);
    p[c::threshold] = 0.5f;
    p[c::autoGain] = 0.0f;
    p[c::attack] = c::attackValue (1.0);
    p[c::release] = c::releaseValue (50.0);
    p[c::scParam (0, 0)] = lynx::facet::shapeValue (lynx::facet::Shape::lowCut);
    p[c::scParam (0, 1)] = lynx::facet::hzValue (1000.0);
    p[c::scParam (0, 4)] = lynx::facet::slopeValue (12);
    auto x = tone (100.0, -10.0, 0.6);
    auto y = run (EffectKind::crestCompressor, p, x);
    const auto lowGain = rmsDb (y, 19200, 28800) - rmsDb (x, 19200, 28800);
    p[c::scParam (0, 0)] = 0.0f;
    y = run (EffectKind::crestCompressor, p, x);
    const auto flatGain = rmsDb (y, 19200, 28800) - rmsDb (x, 19200, 28800);
    std::printf ("  100 Hz at -10 dBFS: %.2f dB with the 1 kHz low cut on the side chain, %.2f dB without\n", lowGain, flatGain);
    CHECK (lowGain > -0.3 && flatGain < -10.0);

    // bell +12 dB, Q 1 at 1 kHz: a 1 kHz tone at −30 dBFS reduces as a −18 dBFS one does without it
    p[c::scParam (0, 0)] = lynx::facet::shapeValue (lynx::facet::Shape::bell);
    p[c::scParam (0, 2)] = lynx::facet::gainValue (12.0);
    p[c::scParam (0, 3)] = lynx::facet::qValue (1.0);
    x = tone (1000.0, -30.0, 0.6);
    y = run (EffectKind::crestCompressor, p, x);
    const auto bellGain = rmsDb (y, 19200, 28800) - rmsDb (x, 19200, 28800);
    p[c::scParam (0, 0)] = 0.0f;
    const auto x2 = tone (1000.0, -18.0, 0.6);
    y = run (EffectKind::crestCompressor, p, x2);
    const auto refGain = rmsDb (y, 19200, 28800) - rmsDb (x2, 19200, 28800);
    std::printf ("  1 kHz bell +12 dB: %.2f dB at -30 dBFS vs %.2f dB at -18 dBFS without\n", bellGain, refGain);
    CHECK (std::abs (bellGain - refGain) < 0.5 && refGain < -5.0);

    // the side-chain level does the same as moving the input
    p[c::scLevel] = (float) ((12.0 + 36.0) / 72.0);
    y = run (EffectKind::crestCompressor, p, x);
    CHECK (std::abs ((rmsDb (y, 19200, 28800) - rmsDb (x, 19200, 28800)) - refGain) < 0.5);

    // an external side chain drives the reduction of a quiet input
    p = defaults (EffectKind::crestCompressor);
    p[c::threshold] = 0.5f;
    p[c::autoGain] = 0.0f;
    p[c::scExternal] = 1.0f;
    const auto quiet = tone (1000.0, -40.0, 0.6), loud = tone (200.0, -6.0, 0.6);
    y = run (EffectKind::crestCompressor, p, quiet, nullptr, &loud);
    CHECK (rmsDb (y, 19200, 28800) - rmsDb (quiet, 19200, 28800) < -12.0);
}

TEST (dyn108_crest_character_and_oversampling)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    // the character stage, with no compression: clean warm at moderate levels, crisp = odd (H3) growing with the
    // square of the level, bright = second harmonic from low levels (§12.2)
    const auto measure = [] (c::Character ch, double driveDb, double level, double& h2, double& h3, double& fund) {
        auto p = defaults (EffectKind::crestCompressor);
        p[c::ratio] = 0.0f;
        p[c::threshold] = 1.0f;
        p[c::autoGain] = 0.0f;
        p[c::character] = (float) (int) ch / 3.0f;
        p[c::drive] = (float) ((driveDb + 24.0) / 48.0);
        const auto x = tone (1000.0, level, 0.5);
        const auto y = run (EffectKind::crestCompressor, p, x);
        const auto h1 = harmonic (y, 1000.0, 1, 9600, 9600);
        fund = 20.0 * std::log10 (h1 / std::pow (10.0, level / 20.0));
        h2 = 20.0 * std::log10 (harmonic (y, 1000.0, 2, 9600, 9600) / h1 + 1e-15);
        h3 = 20.0 * std::log10 (harmonic (y, 1000.0, 3, 9600, 9600) / h1 + 1e-15);
    };
    double h2, h3, f;
    measure (c::Character::warm, 0.0, -6.0, h2, h3, f);
    std::printf ("  warm  0 dB, -6 dBFS: H2 %.1f H3 %.1f fund %+.2f\n", h2, h3, f);
    CHECK (h2 < -90.0 && h3 < -90.0 && std::abs (f) < 0.05);
    measure (c::Character::warm, 24.0, -6.0, h2, h3, f);
    std::printf ("  warm +24 dB, -6 dBFS: H2 %.1f H3 %.1f fund %+.2f (measured -17.6 / -22.0 / -1.34)\n", h2, h3, f);
    CHECK (h2 > -30.0 && h3 > -35.0 && f < -0.3 && f > -4.0);
    double a2, a3, b2, b3;
    measure (c::Character::crisp, 0.0, -18.0, a2, a3, f);
    measure (c::Character::crisp, 0.0, -6.0, b2, b3, f);
    std::printf ("  crisp: H3 %.1f at -18 dBFS, %.1f at -6 dBFS (measured -89.6 / -65.7)\n", a3, b3);
    CHECK (a3 > -97.0 && a3 < -83.0 && b3 > -73.0 && b3 < -59.0 && b3 - a3 > 18.0 && b3 - a3 < 30.0);
    measure (c::Character::bright, 0.0, -18.0, h2, h3, f);
    std::printf ("  bright: H2 %.1f H3 %.1f at -18 dBFS (measured -56.3 / -65.2)\n", h2, h3);
    CHECK (h2 > -66.0 && h2 < -46.0 && h2 > h3);

    // oversampling removes the aliases of a fast gain on a 15 kHz tone
    const auto spur = [] (float os) {
        auto p = defaults (EffectKind::crestCompressor);
        p[c::ratio] = c::ratioValue (8.0);
        p[c::threshold] = 0.5f;
        p[c::attack] = 0.0f;
        p[c::release] = 0.0f;
        p[c::autoGain] = 0.0f;
        p[c::character] = 2.0f / 3.0f; // crisp, driven: the nonlinearity whose harmonics fold back without oversampling
        p[c::drive] = 1.0f;
        p[c::oversampling] = os;
        const auto x = tone (15000.0, -6.0, 0.5);
        const auto y = run (EffectKind::crestCompressor, p, x);
        // the strongest component on a 100 Hz grid away from 15 kHz (its odd harmonics alias to 3, 9, 21 kHz ...)
        double worst = 0.0;
        for (double hz = 1000.0; hz < 23000.0; hz += 1000.0)
            if (std::abs (hz - 15000.0) > 50.0)
                worst = std::max (worst, harmonic (y, hz, 1, 9600, 9600));
        return 20.0 * std::log10 (worst / harmonic (y, 15000.0, 1, 9600, 9600) + 1e-15);
    };
    const auto off = spur (0.0f), eight = spur (1.0f);
    std::printf ("  15 kHz, driven crisp + fast gain: worst alias %.1f dB without oversampling, %.1f dB at 8x\n", off, eight);
    CHECK (eight < off - 10.0);
}

TEST (dyn108_prism_transparent_at_rest_and_band_dynamics)
{
    using namespace dyn108;
    namespace m = lynx::dyn::prism;
    // with every band's range at 0 dB the output is the input, exactly (the bands only add their gain change)
    auto p = defaults (EffectKind::prismMultiband);
    for (int b = 0; b < m::bands; ++b)
        p[(std::size_t) m::param (b, m::fRange)] = 0.5f;
    std::vector<float> noise ((std::size_t) sr / 2);
    std::uint32_t seed = 1;
    for (auto& v : noise)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float) ((double) (seed >> 8) / 16777216.0 - 0.5);
    }
    auto y = run (EffectKind::prismMultiband, p, noise);
    double err = 0.0;
    for (std::size_t i = 0; i < noise.size(); ++i)
        err = std::max (err, (double) std::abs (y[i] - noise[i]));
    CHECK (err == 0.0);

    // one band 300 Hz … 3 kHz (24 dB/oct), threshold −30, ratio 4, range −30, knee 0 (§6 / §12.4): the static gain of
    // a 1 kHz tone, against the measured −1.34 / −5.70 / −9.98 / −14.11 / −18.03 dB at −28 / −22 / −16 / −10 / −4 dBFS
    // (ours stays a little shallower at large reductions: what the transparent band structure leaves of the band's
    // phase-shifted part, REA_rodada108 §3); a 100 Hz tone stays untouched
    p = defaults (EffectKind::prismMultiband);
    for (int b = 0; b < m::bands; ++b)
        p[(std::size_t) m::param (b, m::fEnabled)] = b == 1 ? 1.0f : 0.0f;
    p[m::xover1] = m::crossoverValue (300.0);
    p[m::xover2] = m::crossoverValue (3000.0);
    p[m::xover3] = m::crossoverValue (12000.0);
    p[(std::size_t) m::param (1, m::fThreshold)] = m::thresholdValue (-30.0);
    p[(std::size_t) m::param (1, m::fRatio)] = lynx::dyn::crest::ratioValue (4.0);
    p[(std::size_t) m::param (1, m::fRange)] = 0.0f; // −30 dB
    p[(std::size_t) m::param (1, m::fKnee)] = 0.0f;
    const std::vector<double> levels { -40, -28, -22, -16, -10, -4 };
    const double measured[] { 0.0, -1.34, -5.70, -9.98, -14.11, -18.03 };
    std::size_t per = 0;
    auto x = stair (levels, 0.6, 1000.0, per);
    auto g = stairGains (x, run (EffectKind::prismMultiband, p, x), per, levels.size());
    for (std::size_t k = 0; k < levels.size(); ++k)
    {
        std::printf ("  band compression at %g dBFS: %.2f dB (measured %.2f)\n", levels[k], g[k], measured[k]);
        CHECK (std::abs (g[k] - measured[k]) < 2.2);
    }
    x = stair (levels, 0.6, 100.0, per);
    g = stairGains (x, run (EffectKind::prismMultiband, p, x), per, levels.size());
    CHECK (std::abs (g.back()) < 0.3);

    // downward expansion below the threshold (range −30): measured −10.05 dB at −34 dBFS, −22.87 dB at −40 dBFS
    p[(std::size_t) m::param (1, m::fMode)] = 1.0f;
    const std::vector<double> low { -20, -34, -40, -60 };
    x = stair (low, 0.6, 1000.0, per);
    g = stairGains (x, run (EffectKind::prismMultiband, p, x), per, low.size());
    std::printf ("  band expansion: %.2f / %.2f / %.2f / %.2f dB at -20 / -34 / -40 / -60 dBFS\n", g[0], g[1], g[2], g[3]);
    CHECK (std::abs (g[0]) < 0.3 && g[1] < -7.0 && g[1] > -13.5 && g[2] < -17.0 && g[3] < -17.0 && g[3] > -31.0);

    // upward expansion above it (range +12): measured +5.78 dB at −22 dBFS, +11.87 at −4 dBFS
    p[(std::size_t) m::param (1, m::fRange)] = (float) ((12.0 + 30.0) / 60.0);
    const std::vector<double> high { -40, -22, -4 };
    x = stair (high, 0.6, 1000.0, per);
    g = stairGains (x, run (EffectKind::prismMultiband, p, x), per, high.size());
    std::printf ("  band upward expansion: %.2f / %.2f / %.2f dB at -40 / -22 / -4 dBFS\n", g[0], g[1], g[2]);
    CHECK (std::abs (g[0]) < 0.3 && g[1] > 4.0 && g[1] < 7.5 && g[2] > 10.0 && g[2] < 12.5);

    // the band's level alone: −12 dB in the band's middle (measured −11.15 at 24 dB/oct), little at 100 Hz / 10 kHz
    p[(std::size_t) m::param (1, m::fRange)] = 0.5f;
    p[(std::size_t) m::param (1, m::fLevel)] = (float) ((-12.0 + 30.0) / 60.0);
    const double gains[m::bands] { 0.0, -12.0, 0.0, 0.0 };
    const auto mid = lynx::dyn::prism::responseDb (p, gains, sr, 1000.0);
    const auto lo = lynx::dyn::prism::responseDb (p, gains, sr, 60.0), hi = lynx::dyn::prism::responseDb (p, gains, sr, 15000.0);
    std::printf ("  band level -12 dB: %.2f dB at 1 kHz, %.2f at 60 Hz, %.2f at 15 kHz\n", mid, lo, hi);
    CHECK (mid < -10.0 && mid > -12.5 && lo > -1.0 && hi > -1.0);
    x = tone (1000.0, -20.0, 0.5);
    y = run (EffectKind::prismMultiband, p, x);
    CHECK (std::abs ((rmsDb (y, 12000, 24000) - rmsDb (x, 12000, 24000)) - mid) < 0.2);
}

TEST (dyn108_prism_attack_release_and_lookahead)
{
    using namespace dyn108;
    namespace m = lynx::dyn::prism;
    auto p = defaults (EffectKind::prismMultiband);
    for (int b = 0; b < m::bands; ++b)
        p[(std::size_t) m::param (b, m::fEnabled)] = b == 1 ? 1.0f : 0.0f;
    p[m::xover1] = m::crossoverValue (300.0);
    p[m::xover2] = m::crossoverValue (3000.0);
    p[(std::size_t) m::param (1, m::fThreshold)] = m::thresholdValue (-30.0);
    p[(std::size_t) m::param (1, m::fRatio)] = lynx::dyn::crest::ratioValue (4.0);
    p[(std::size_t) m::param (1, m::fRange)] = 0.0f;
    p[(std::size_t) m::param (1, m::fKnee)] = 0.0f;
    // attack 50 %: a −50 → −10 dBFS step; half the reduction after ~11 ms (measured 11.3)
    std::vector<float> x ((std::size_t) (2 * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, (i >= (std::size_t) sr ? -10.0 : -50.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    p[(std::size_t) m::param (1, m::fAttack)] = 0.5f;
    p[(std::size_t) m::param (1, m::fRelease)] = 0.0f;
    auto y = run (EffectKind::prismMultiband, p, x);
    const auto a = timeTo (x, y, (std::size_t) sr, 0.5);
    // release 0 %: a −10 → −28 dBFS step (a drop of 18 dB: the band filter's ringing stays small against the new
    // level); half the recovery after ~90 ms as measured (the gain is read once it has settled past the target)
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, (i >= (std::size_t) sr ? -28.0 : -10.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    y = run (EffectKind::prismMultiband, p, x);
    const auto gainAt = [&] (std::size_t i) { return rmsDb (y, i - 24, i + 24) - rmsDb (x, i - 24, i + 24); };
    const auto g0 = gainAt ((std::size_t) sr - 48), g1 = gainAt ((std::size_t) (1.9 * sr));
    double r = 0.0;
    for (std::size_t i = (std::size_t) (1.9 * sr); i > (std::size_t) sr; i -= 12)
        if (gainAt (i) < 0.5 * (g0 + g1))
        {
            r = (double) (i - (std::size_t) sr) / sr * 1000.0;
            break;
        }
    std::printf ("  attack 50 %%: %.1f ms to half the reduction; release 0 %%: %.1f ms to half the recovery\n", a, r);
    CHECK (a > 5.0 && a < 22.0 && r > 40.0 && r < 160.0);
    // the look-ahead adds latency (20 ms = 960 samples) and only then
    int latency = -1;
    run (EffectKind::prismMultiband, p, tone (1000.0, -40.0, 0.1), &latency);
    CHECK (latency == 0);
    p[m::lookahead] = 1.0f;
    run (EffectKind::prismMultiband, p, tone (1000.0, -40.0, 0.1), &latency);
    CHECK (latency == 960);
}

TEST (dyn108_sentry_gate_curves_and_timing)
{
    using namespace dyn108;
    namespace s = lynx::dyn::sentry;
    // §7: threshold −30, ratio 100, range 100: full range below the threshold, unity above; range 20 caps at −20
    auto p = defaults (EffectKind::sentryGate);
    const std::vector<double> levels { -50, -40, -20, -10 };
    std::size_t per = 0;
    auto x = stair (levels, 0.5, 1000.0, per);
    auto g = stairGains (x, run (EffectKind::sentryGate, p, x), per, levels.size());
    std::printf ("  gate: %.1f / %.1f / %.2f / %.2f dB\n", g[0], g[1], g[2], g[3]);
    CHECK (g[0] < -80.0 && g[1] < -80.0 && std::abs (g[2]) < 0.1 && std::abs (g[3]) < 0.1);
    p[s::range] = 0.2f;
    g = stairGains (x, run (EffectKind::sentryGate, p, x), per, levels.size());
    CHECK (std::abs (g[0] + 20.0) < 0.3 && std::abs (g[2]) < 0.1);

    // ratio 2 expands 1 dB per dB below a point that depends on the style: Classic −37.9, Clean −34.3 dB (setting −30)
    p = defaults (EffectKind::sentryGate);
    p[s::ratio] = lynx::dyn::crest::ratioValue (2.0);
    const std::vector<double> exp { -36, -44, -50 };
    x = stair (exp, 0.6, 1000.0, per);
    g = stairGains (x, run (EffectKind::sentryGate, p, x), per, exp.size());
    std::printf ("  Classic 2:1: %.2f / %.2f / %.2f dB at -36 / -44 / -50 dBFS (model: 0 / -6.1 / -12.1)\n", g[0], g[1], g[2]);
    CHECK (std::abs (g[0]) < 0.6 && std::abs (g[1] + 6.1) < 1.2 && std::abs (g[2] + 12.1) < 1.2);
    p[s::style] = s::styleValue (s::Style::clean);
    g = stairGains (x, run (EffectKind::sentryGate, p, x), per, exp.size());
    CHECK (std::abs (g[1] + 9.7) < 1.2);

    // upward style: +3 dB at 0 dBFS with the defaults (upward threshold −6 dB, 1.5 : 1), nothing below — round
    // tp172b measured the reference lifting (ratio − 1) dB per dB above the upward threshold (was 1 − 1 / ratio: +2)
    p = defaults (EffectKind::sentryGate);
    p[s::style] = s::styleValue (s::Style::upward);
    const std::vector<double> up { -40, -12, 0 };
    x = stair (up, 0.5, 1000.0, per);
    g = stairGains (x, run (EffectKind::sentryGate, p, x), per, up.size());
    std::printf ("  upward: %.2f / %.2f / %.2f dB\n", g[0], g[1], g[2]);
    CHECK (std::abs (g[0]) < 0.1 && std::abs (g[1]) < 0.1 && std::abs (g[2] - 3.0) < 0.3);

    // closing after a −10 → −60 dBFS step (attack 0.1 ms, release 150 ms): Classic −6 dB after 39 ms, −20 after 147
    std::vector<float> burst ((std::size_t) (3 * sr));
    for (std::size_t i = 0; i < burst.size(); ++i)
        burst[i] = (float) (std::pow (10.0, (i >= (std::size_t) sr && i < (std::size_t) (2 * sr) ? -10.0 : -60.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    const auto closeAfter = [&] (const lynx::EffectParams& q, double db) {
        const auto y = run (EffectKind::sentryGate, q, burst);
        for (std::size_t i = (std::size_t) (2 * sr); i + 48 < burst.size(); i += 24)
            if (rmsDb (y, i, i + 48) - rmsDb (burst, i, i + 48) < db)
                return (double) (i - (std::size_t) (2 * sr)) / sr * 1000.0;
        return 1e9;
    };
    p = defaults (EffectKind::sentryGate);
    p[s::hold] = 0.0f;
    const auto c6 = closeAfter (p, -6.0), c20 = closeAfter (p, -20.0);
    std::printf ("  Classic closing: -6 dB after %.0f ms (39), -20 dB after %.0f ms (147)\n", c6, c20);
    CHECK (c6 > 25.0 && c6 < 60.0 && c20 > 95.0 && c20 < 220.0);
    // Vocal opens slowly (measured 37 ms to −1 dB at attack 0.1 ms)
    p[s::style] = s::styleValue (s::Style::vocal);
    const auto y = run (EffectKind::sentryGate, p, burst);
    double openMs = 1e9;
    for (std::size_t i = (std::size_t) sr; i < (std::size_t) (1.5 * sr); i += 24)
        if (rmsDb (y, i, i + 48) - rmsDb (burst, i, i + 48) > -1.0)
        {
            openMs = (double) (i - (std::size_t) sr) / sr * 1000.0;
            break;
        }
    std::printf ("  Vocal opens to -1 dB after %.0f ms (37)\n", openMs);
    CHECK (openMs > 20.0 && openMs < 60.0);

    // the look-ahead switch: 10 ms of latency
    int latency = -1;
    p[s::lookaheadOn] = 1.0f;
    run (EffectKind::sentryGate, p, tone (1000.0, -20.0, 0.1), &latency);
    CHECK (latency == 480);
}

TEST (dyn108_ember_multiband_mode)
{
    using namespace dyn108;
    namespace e = lynx::mixsuite::ember;
    // one band: Ember as before (the new parameters at their defaults change nothing)
    auto p = defaults (EffectKind::ember);
    CHECK (e::bandCount (p[e::bands]) == 1);
    p[e::drive] = 0.0f;
    p[e::style] = 1.0f / 3.0f; // tape: odd only, close to linear at drive 0 and low level
    p[e::autoGain] = 0.0f;
    p[e::law] = 0.0f; // round 190fj: the Classic tape curve on one band, as the bands run it
    // two to four bands with every band clean: the crossovers sum flat (Linkwitz-Riley + all-pass compensation)
    for (int nb = 2; nb <= 4; ++nb)
        for (float slope : { 0.0f, 0.5f, 1.0f })
        {
            auto q = p;
            q[e::bands] = (float) (nb - 1) / 3.0f;
            q[e::xoverSlope] = slope;
            for (int b = 1; b < nb; ++b)
                q[(std::size_t) e::bandParam (b, 0)] = 1.0f / 3.0f;
            for (const double hz : { 60.0, 240.0, 1000.0, 2000.0, 5000.0, 12000.0 })
            {
                const auto x = tone (hz, -40.0, 0.4);
                const auto one = run (EffectKind::ember, p, x);
                const auto many = run (EffectKind::ember, q, x);
                const auto diff = (rmsDb (many, 9600, 19200) - rmsDb (one, 9600, 19200));
                if (std::abs (diff) > 0.25)
                {
                    std::printf ("  %d bands, slope %.1f, %g Hz: %+.2f dB vs one band\n", nb, slope, hz, diff);
                    CHECK (false);
                }
            }
        }
    // one band driven hard (band 3 = 2 … 12 kHz, fuzz +30 dB): a 5 kHz tone distorts, a 100 Hz tone stays clean
    auto q = p;
    q[e::bands] = 1.0f;
    q[(std::size_t) e::bandParam (2, 0)] = 1.0f;
    q[(std::size_t) e::bandParam (2, 1)] = 30.0f / 36.0f;
    const auto thd = [&] (double hz) {
        const auto y = run (EffectKind::ember, q, tone (hz, -12.0, 0.5));
        const auto h1 = harmonic (y, hz, 1, 9600, 9600);
        double h = 0.0;
        for (int k = 2; k <= 4; ++k)
            if (hz * k < 20000.0)
                h += std::pow (harmonic (y, hz, k, 9600, 9600), 2.0);
        return std::sqrt (h) / h1;
    };
    const auto low = thd (100.0), mid = thd (1500.0) , top = thd (5000.0);
    std::printf ("  band 3 driven: THD %.2f %% at 100 Hz, %.2f %% at 1.5 kHz, %.1f %% at 5 kHz\n", 100 * low, 100 * mid, 100 * top);
    CHECK (low < 0.01 && top > 0.05);
}

TEST (dyn108_fabfilter_states_translate_to_the_suite)
{
    using namespace dyn108;
    using namespace lynx;
    // Pro-C 3 (FFBS, 100 values): Punch, −30 dB, ratio 8 (norm 0.8), knee 6, range 40, attack norm 0.3 (6.755 ms),
    // release norm 0.5 (309.5 ms), hold norm 0.5 (40 ms), crisp (diode) pre, drive +6, side-chain band 2 used:
    // low cut 1 kHz 24 dB/oct; mix 1, output level 0.2 (+7.2 dB), oversampling 4x, maximum look-ahead 5 ms
    const auto proc = ffbs (100, { { 0, 3.0f }, { 1, -30.0f }, { 4, 0.8f }, { 5, 6.0f }, { 6, 40.0f }, { 7, 0.3f }, { 8, 0.5f },
                                   { 11, 0.5f }, { 12, 2.0f }, { 13, 0.0f }, { 14, 6.0f }, { 22, 0.0f }, { 26, 0.5f },
                                   { 41, 1.0f }, { 42, 1.0f }, { 43, (float) std::log2 (1000.0) }, { 44, 0.0f }, { 45, 0.5f },
                                   { 46, 2.0f }, { 47, 3.0f }, { 88, 1.0f }, { 89, 0.0f }, { 91, 0.2f }, { 94, 2.0f }, { 95, 3.0f } });
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "FabFilter Pro-C 3";
    id.state = proc;
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->effect == EffectKind::crestCompressor && entry->translator == "pro-c3");
    if (entry != nullptr)
    {
        int translated = 0;
        const auto slot = makeSubstituteSlot (*entry, &id, &translated);
        namespace c = dyn::crest;
        std::printf ("  Pro-C 3 state: %d parameters translated\n", translated);
        CHECK (translated >= 25);
        CHECK (c::styleOf (slot.params[c::style]) == c::Style::punch);
        CHECK (std::abs (c::thresholdDb (slot.params[c::threshold]) + 30.0) < 0.01);
        CHECK (std::abs (c::ratioOf (slot.params[c::ratio]) - 8.0) < 0.05);
        CHECK (std::abs (c::kneeDb (slot.params[c::knee]) - 6.0) < 0.01 && std::abs (c::rangeDb (slot.params[c::range]) - 40.0) < 0.01);
        CHECK (std::abs (c::attackMs (slot.params[c::attack]) - 6.755) < 0.01);
        CHECK (std::abs (c::releaseMs (slot.params[c::release]) - 309.5) < 0.5);
        CHECK (std::abs (c::holdMs (slot.params[c::hold]) - 40.0) < 0.1);
        CHECK (c::characterOf (slot.params[c::character]) == c::Character::crisp && slot.params[c::characterPre] > 0.5f);
        CHECK (std::abs (c::driveDb (slot.params[c::drive]) - 6.0) < 0.01);
        CHECK (facet::shape (slot.params[c::scParam (0, 0)]) == facet::Shape::lowCut);
        CHECK (std::abs (facet::hz (slot.params[c::scParam (0, 1)]) - 1000.0) < 1.0);
        CHECK (facet::slopeDb (slot.params[c::scParam (0, 4)]) == 24);
        CHECK (facet::shape (slot.params[c::scParam (1, 0)]) == facet::Shape::off);
        CHECK (std::abs (c::trimDb (slot.params[c::output]) - 7.2) < 0.01);
        CHECK (c::oversamplingOf (slot.params[c::oversampling]) == 4);
        CHECK (std::abs (c::maxLookaheadMs (slot.params[c::maxLookahead]) - 5.0) < 0.01);

        // the same state inside a JUCE host state (LYNX's own VST3 host) reads the same
        const auto xml = "VC2!....<?xml version=\"1.0\" encoding=\"UTF-8\"?> <VST3PluginState><IComponent>" + juceBase64 (proc)
                         + "</IComponent><IEditController>4.....</IEditController></VST3PluginState>";
        PluginIdentity wrapped = id;
        wrapped.state.assign (xml.begin(), xml.end());
        int again = 0;
        const auto slot2 = makeSubstituteSlot (*entry, &wrapped, &again);
        CHECK (again == translated && slot2.params == slot.params);

        // another layout (Pro-C 2, unknown count): defaults, nothing translated
        PluginIdentity old = id;
        old.state = ffbs (90, { { 1, -40.0f } });
        int none = -1;
        const auto slot3 = makeSubstituteSlot (*entry, &old, &none);
        CHECK (none == 0 && slot3.params == makeEffectSlot (EffectKind::crestCompressor).params);

        // the substitute keeps the original plugin, state byte for byte
        EffectSlot fl;
        fl.kind = EffectKind::plugin;
        fl.plugin.name = "FabFilter Pro-C 3";
        fl.plugin.foreign.format = ForeignPlugin::Format::vst3;
        fl.plugin.foreign.name = "FabFilter Pro-C 3";
        fl.plugin.foreign.state = std::vector<std::uint8_t> (proc.begin(), proc.end());
        const auto before = fl.plugin;
        CHECK (substituteEffectSlot (fl, id) && fl.kind == EffectKind::crestCompressor && fl.substituteFor == before
               && fl.substituteFor.foreign.state == proc);
    }

    // Pro-MB (FabF, 151 values): one enabled band 300 Hz … 3 kHz, expansion, threshold norm 0.5 (−30 dB), range −24,
    // ratio norm 0.6 (4 : 1), attack 30 %, release 60 %, knee 12, level +3; mix 1 → Prism: band 2 between 300 Hz and
    // 3 kHz, gap bands 1 and 3 off
    std::vector<std::pair<int, float>> mb { { 0, 1.0f }, { 1, (float) std::log2 (300.0) }, { 2, 0.5f }, { 3, (float) std::log2 (3000.0) },
                                            { 4, 0.5f }, { 5, 1.0f }, { 6, 0.5f }, { 7, -24.0f }, { 8, 0.6f }, { 9, 30.0f }, { 10, 60.0f },
                                            { 11, 12.0f }, { 13, 3.0f }, { 133, 1.0f } };
    for (int b = 1; b < 6; ++b)
        mb.push_back ({ 22 * b, 2.0f }); // unused
    id.name = "FabFilter Pro-MB";
    id.state = fabf (151, mb);
    entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->effect == EffectKind::prismMultiband);
    if (entry != nullptr)
    {
        int translated = 0;
        const auto slot = makeSubstituteSlot (*entry, &id, &translated);
        namespace m = dyn::prism;
        const auto at = [&] (int b, int f) { return slot.params[(std::size_t) m::param (b, f)]; };
        std::printf ("  Pro-MB state: %d parameters translated, crossovers %.0f / %.0f / %.0f Hz\n", translated,
                     m::crossoverHz (slot.params[m::xover1]), m::crossoverHz (slot.params[m::xover2]), m::crossoverHz (slot.params[m::xover3]));
        CHECK (translated > 10);
        // round 150: the free layout — our band 1 is the reference's band with its own edges, the rest passes untouched
        CHECK (m::freeLayout (slot.params) && m::bandCount (slot.params) == 1 && at (0, m::fEnabled) > 0.5f);
        CHECK (std::abs (m::edgeHz (slot.params[m::bandLo0]) - 300.0) < 1.0 && std::abs (m::edgeHz (slot.params[m::bandHi0]) - 3000.0) < 5.0);
        CHECK (at (0, m::fMode) > 0.5f && std::abs (m::thresholdDb (at (0, m::fThreshold)) + 30.0) < 0.05);
        CHECK (std::abs (m::rangeDb (at (0, m::fRange)) + 24.0) < 0.01 && std::abs (m::ratioOf (at (0, m::fRatio)) - 4.0) < 0.05);
        CHECK (std::abs (at (0, m::fAttack) - 0.3f) < 1e-6f && std::abs (at (0, m::fRelease) - 0.6f) < 1e-6f);
        CHECK (std::abs (m::kneeDb (at (0, m::fKnee)) - 12.0) < 0.01 && std::abs (m::levelDb (at (0, m::fLevel)) - 3.0) < 0.01);
    }

    // Pro-G (FabF, 34 values): Vocal, −40 dB, ratio norm 0.4 (2 : 1), range norm 0.5 (16 dB), attack norm 0.3, release
    // norm 0.5 (428.7 ms), hold norm 0.5 (40 ms), knee 6, look-ahead on, high pass norm 0.4
    id.name = "FabFilter Pro-G";
    id.state = fabf (34, { { 0, -40.0f }, { 1, -6.0f }, { 2, 0.4f }, { 3, 0.25f }, { 4, 0.5f }, { 5, 2.0f }, { 6, 0.3f }, { 7, 0.5f },
                           { 8, 0.5f }, { 9, 6.0f }, { 10, 0.5f }, { 11, 1.0f }, { 17, 1.0f }, { 18, 0.4f }, { 20, 0.0f }, { 22, -1.0f },
                           { 30, 0.0f } });
    entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->effect == EffectKind::sentryGate);
    if (entry != nullptr)
    {
        const auto slot = makeSubstituteSlot (*entry, &id);
        namespace s = dyn::sentry;
        CHECK (s::styleOf (slot.params[s::style]) == s::Style::vocal);
        CHECK (std::abs (s::thresholdDb (slot.params[s::threshold]) + 40.0) < 0.01);
        CHECK (std::abs (s::ratioOf (slot.params[s::ratio]) - 2.0) < 0.02);
        CHECK (std::abs (s::rangeDb (slot.params[s::range]) - 16.0) < 0.01);
        CHECK (std::abs (s::attackMs (slot.params[s::attack]) - 8.1) < 0.01);
        CHECK (std::abs (s::releaseMs (slot.params[s::release]) - 428.7) < 0.5);
        CHECK (std::abs (s::holdMs (slot.params[s::hold]) - 40.0) < 0.1);
        CHECK (std::abs (s::kneeDb (slot.params[s::knee]) - 6.0) < 0.01 && slot.params[s::lookaheadOn] > 0.5f);
        CHECK (std::abs (s::filterHz (slot.params[s::highPass]) - 162.27) < 0.5 && slot.params[s::dry] < 1e-4f);
    }

    // Saturn 2 (FFBS, 951 values): 2 bands split at 800 Hz (24 dB/oct), Warm Tape at 50 % and Lead Amp at 20 %
    id.name = "FabFilter Saturn 2";
    id.state = ffbs (951, { { 5, 100.0f }, { 6, 2.0f }, { 7 + 3, 6.0f }, { 7 + 4, 0.5f }, { 7 + 10, 100.0f }, { 7 + 13, 1.0f },
                            { 7 + 15, (float) std::log2 (800.0) }, { 7 + 16, 2.0f }, { 24 + 3, 14.0f }, { 24 + 4, 0.2f },
                            { 24 + 10, 50.0f }, { 24 + 13, 1.0f } });
    entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->effect == EffectKind::ember);
    if (entry != nullptr)
    {
        const auto slot = makeSubstituteSlot (*entry, &id);
        namespace e = mixsuite::ember;
        CHECK (e::bandCount (slot.params[e::bands]) == 2);
        CHECK (std::abs (e::crossoverHz (slot.params[e::xover1]) - 800.0) < 2.0 && e::slopeDbOct (slot.params[e::xoverSlope]) == 24);
        CHECK (e::styleOf (slot.params[e::style]) == e::Style::tape && e::styleOf (slot.params[(std::size_t) e::bandParam (1, 0)]) == e::Style::fuzz);
        CHECK (std::abs (e::driveDb (slot.params[e::drive]) - 15.0) < 0.01);
        CHECK (std::abs (slot.params[(std::size_t) e::bandParam (1, 2)] - 0.5f) < 1e-6f);
    }
}
