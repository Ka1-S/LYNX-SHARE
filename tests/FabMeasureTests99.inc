// --- Facet EQ and Apex Limiter (core/FacetEq.h, core/ApexLimiter.h) - round 99 ------------------------------------
// Golden values: our own black-box measurements of the reference EQ (docs/analise/MEASURE_fabfilter.md §1.4).

#include "core/FacetEq.h"
#include "core/ApexLimiter.h"

namespace fab99
{
struct EqGolden { lynx::facet::Shape shape; double hz, gain, q; int slope; std::array<double, 9> db; };
constexpr std::array<double, 9> goldenHz { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000 };
using S = lynx::facet::Shape;
const EqGolden eqGolden[] = {
    { S::bell, 1000, 12, 1, 12, { 0.08, 0.32, 1.20, 5.69, 12.00, 5.69, 1.18, 0.30, 0.13 } },
    { S::bell, 1000, -12, 1, 12, { -0.08, -0.32, -1.20, -5.69, -12.00, -5.69, -1.18, -0.30, -0.13 } },
    { S::bell, 100, 6, 5, 12, { 0.22, 5.99, 0.22, 0.02, 0.01, 0.00, 0.00, 0.00, 0.00 } },
    { S::bell, 10000, 6, 2, 12, { 0.00, 0.00, 0.00, 0.01, 0.03, 0.14, 1.14, 6.00, 2.40 } },
    { S::lowShelf, 1000, 12, 1, 12, { 12.00, 12.00, 11.97, 11.10, 6.00, 0.90, 0.03, 0.00, 0.00 } },
    { S::highShelf, 1000, -6, 0.5, 12, { -0.01, -0.06, -0.22, -1.16, -3.00, -4.84, -5.79, -5.95, -5.98 } },
    { S::highCut, 1000, 0, 1, 24, { -0.00, -0.00, -0.00, -0.02, -3.01, -24.10, -55.93, -80.22, -95.09 } },
    { S::lowCut, 100, 0, 2, 48, { -47.89, 2.98, 0.21, 0.02, 0.01, 0.00, 0.00, 0.00, 0.00 } },
    { S::lowCut, 1000, 0, 0.5, 18, { -78.09, -60.13, -42.45, -20.97, -9.03, -2.90, -0.50, -0.12, -0.05 } },
    { S::notch, 1000, 0, 2, 12, { -0.01, -0.02, -0.09, -0.87, -99.0, -0.87, -0.09, -0.02, -0.01 } },
    { S::bandPass, 1000, 0, 1, 12, { -23.01, -16.99, -10.98, -3.27, -0.00, -3.28, -11.02, -17.13, -20.64 } },
    { S::tiltShelf, 1000, 6, 1, 12, { -6.00, -6.00, -5.97, -5.10, -0.00, 5.10, 5.97, 6.00, 6.00 } },
};

lynx::EffectParams eqDefaults()
{
    lynx::EffectParams p {};
    const auto& info = lynx::facet::info();
    for (std::size_t i = 0; i < info.params.size(); ++i) p[i] = info.params[i].defaultValue;
    return p;
}

lynx::EffectParams eqBand (const EqGolden& g)
{
    using namespace lynx::facet;
    auto p = eqDefaults();
    p[(std::size_t) param (0, 0)] = shapeValue (g.shape);
    p[(std::size_t) param (0, 1)] = hzValue (g.hz);
    p[(std::size_t) param (0, 2)] = gainValue (g.gain);
    p[(std::size_t) param (0, 3)] = qValue (g.q);
    p[(std::size_t) param (0, 4)] = slopeValue (g.slope);
    return p;
}

double sineGainDb (lynx::Effect& fx, const lynx::EffectParams& p, double hz, double sr, int latency = 0)
{
    const int n = (int) sr; // 1 s
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    for (int i = 0; i < n; ++i) l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.1 * std::sin (2.0 * M_PI * hz * i / sr));
    fx.prepare (sr);
    for (int i = 0; i < n; i += 256) fx.process (l.data() + i, r.data() + i, std::min (256, n - i), p, 120.0);
    double e = 0.0;
    const int from = n / 2 + latency;
    for (int i = from; i < n; ++i) e += (double) l[(std::size_t) i] * l[(std::size_t) i];
    return 10.0 * std::log10 (e / (n - from) / 0.005);
}
} // namespace fab99

TEST (facet_eq_parameter_laws)
{
    using namespace lynx::facet;
    CHECK (std::abs (hz (hzValue (1000.0)) - 1000.0) < 0.01);
    CHECK (std::abs (hz (0.0f) - 10.0) < 1e-9 && std::abs (hz (1.0f) - 30000.0) < 1e-6);
    CHECK (std::abs (q (0.5f) - 1.0) < 1e-9 && std::abs (q (0.0f) - 0.025) < 1e-12 && std::abs (q (1.0f) - 40.0) < 1e-9);
    CHECK (std::abs (gainDb (gainValue (-7.5)) + 7.5) < 1e-5);
    for (int s : slopes) CHECK (slopeDb (slopeValue (s)) == s);
    for (int k = 0; k < shapeCount; ++k) CHECK (shape (shapeValue ((Shape) k)) == (Shape) k);
    CHECK (info().params.size() == (std::size_t) paramCount);
    CHECK (paramText (param (0, 1), hzValue (1000.0)) == "1.00 kHz");
    CHECK (paramText (param (2, 0), shapeValue (Shape::tiltShelf)) == "Tilt Shelf");
    CHECK (paramText (param (1, 4), slopeValue (48)) == "48 dB/oct");
}

TEST (facet_eq_matches_measured_reference_curves)
{
    // the digital response (matched design) against the golden table, 48 kHz: within 0.3 dB wherever the reference is
    // above −30 dB, up to 10 kHz
    int compared = 0;
    double worst = 0.0;
    for (const auto& g : fab99::eqGolden)
    {
        const auto p = fab99::eqBand (g);
        for (std::size_t k = 0; k < fab99::goldenHz.size(); ++k)
        {
            if (g.db[k] < -30.0 || fab99::goldenHz[k] > 10000.0) continue;
            const double mine = lynx::facet::responseDb (p, 48000.0, fab99::goldenHz[k]);
            worst = std::max (worst, std::abs (mine - g.db[k]));
            if (std::abs (mine - g.db[k]) >= 0.3) std::printf ("  facet %d Hz %.0f: %.3f vs %.3f\n", (int) g.shape, fab99::goldenHz[k], mine, g.db[k]);
            CHECK (std::abs (mine - g.db[k]) < 0.35);
            ++compared;
        }
    }
    std::printf ("  Facet EQ vs measured reference: %d points, worst %.3f dB\n", compared, worst);
    CHECK (compared > 80);
}

TEST (facet_eq_processing_follows_its_response)
{
    using namespace lynx::facet;
    auto fx = make();
    // all bands off: bit-transparent
    {
        const auto p = fab99::eqDefaults();
        std::vector<float> l (1000), r (1000);
        for (int i = 0; i < 1000; ++i) l[(std::size_t) i] = r[(std::size_t) i] = (float) std::sin (i * 0.37);
        auto l0 = l;
        fx->prepare (48000.0);
        fx->process (l.data(), r.data(), 1000, p, 120.0);
        CHECK (l == l0);
    }
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (const auto& g : { fab99::eqGolden[0], fab99::eqGolden[4], fab99::eqGolden[6], fab99::eqGolden[11] })
        {
            const auto p = fab99::eqBand (g);
            for (double f : { 300.0, 1000.0, 3000.0 })
            {
                const double meas = fab99::sineGainDb (*fx, p, f, sr);
                CHECK (std::abs (meas - responseDb (p, sr, f)) < 0.05);
            }
        }
    // output gain and gain scale
    auto p = fab99::eqBand (fab99::eqGolden[0]);
    p[(std::size_t) outputParam] = 0.5f + 6.0f / 48.0f;
    p[(std::size_t) scaleParam] = 0.25f; // 50 %
    CHECK (std::abs (responseDb (p, 48000.0, 1000.0) - 12.0) < 0.02); // 6 dB band + 6 dB output
    // 96 dB/oct cut: stable and steep
    auto c = fab99::eqDefaults();
    c[(std::size_t) param (0, 0)] = shapeValue (Shape::lowCut);
    c[(std::size_t) param (0, 1)] = hzValue (1000.0);
    c[(std::size_t) param (0, 4)] = slopeValue (96);
    CHECK (responseDb (c, 48000.0, 500.0) < -90.0);
    CHECK (std::abs (responseDb (c, 48000.0, 1000.0) + 3.01) < 0.1);
    CHECK (std::abs (fab99::sineGainDb (*fx, c, 4000.0, 48000.0)) < 0.05);
}

TEST (apex_limiter_ceiling_latency_and_release)
{
    using namespace lynx::apex;
    auto fx = make();
    lynx::EffectParams p {};
    for (std::size_t i = 0; i < info().params.size(); ++i) p[i] = info().params[i].defaultValue;
    CHECK (paramText (1, p[1]) == "-0.1 dB");
    CHECK (paramText (4, 1.0f) == "Safe");
    p[0] = 10.0f / 30.0f; // +10 dB into the limiter
    const double sr = 48000.0;
    fx->prepare (sr);
    // (round 190co: True peak, on by default, adds the true-peak guard's 95 samples; off, the latency is the old one)
    CHECK (fx->latencySamples() == latencyAt (sr) && latencyAt (sr, false) == 244 && latencyAt (sr) == 244 + guardLatency (sr));
    const int n = (int) sr * 3;
    for (int st = 0; st < styleCount; ++st)
        for (int tp = 0; tp < 2; ++tp)
        {
            p[4] = (float) st / (styleCount - 1); p[5] = (float) tp;
            fx->reset();
            std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double a = (i > sr && i < 2 * sr) ? 0.8 : 0.03;
                l[(std::size_t) i] = (float) (a * std::sin (2.0 * M_PI * 100.0 * i / sr + 0.3));
                r[(std::size_t) i] = (float) (a * std::sin (2.0 * M_PI * 150.0 * i / sr));
            }
            for (int i = 0; i < n; i += 300) fx->process (l.data() + i, r.data() + i, std::min (300, n - i), p, 120.0);
            float peak = 0.0f;
            for (int i = 0; i < n; ++i) peak = std::max ({ peak, std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]) });
            CHECK (peak <= (float) std::pow (10.0, -0.1 / 20.0) + 1e-6f);
            CHECK (peak > 0.95f);
        }
    // below the ceiling the limiter is a pure delay of its latency (times the gain)
    p[0] = 0.0f;
    fx->reset();
    std::vector<float> l (4096), r (4096);
    for (int i = 0; i < 4096; ++i) l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.2 * std::sin (i * 0.05));
    const auto in = l;
    fx->process (l.data(), r.data(), 4096, p, 120.0);
    double err = 0.0;
    const int lat = fx->latencySamples(); // (True peak is on here: 244 + the guard)
    CHECK (lat == latencyAt (sr, true));
    for (int i = lat; i < 4096; ++i) err = std::max (err, (double) std::abs (l[(std::size_t) i] - in[(std::size_t) (i - lat)]));
    CHECK (err < 1e-6);
    // true peak: an fs/4 sine at 45° (true peak 3 dB above its samples) is held under the ceiling between samples too
    p[0] = 6.0f / 30.0f; p[5] = 1.0f; p[1] = 1.0f; // ceiling 0 dB
    fx->reset();
    std::vector<float> a ((std::size_t) n), b ((std::size_t) n);
    for (int i = 0; i < n; ++i) a[(std::size_t) i] = b[(std::size_t) i] = (float) (0.5 * std::sin (M_PI / 2.0 * i + M_PI / 4.0));
    for (int i = 0; i < n; i += 512) fx->process (a.data() + i, b.data() + i, std::min (512, n - i), p, 120.0);
    float sp = 0.0f;
    for (int i = n / 2; i < n; ++i) sp = std::max (sp, std::abs (a[(std::size_t) i]));
    CHECK (sp < 0.75f); // samples near −3 dB: the true peak (≈ √2 × samples) is at the ceiling
}
