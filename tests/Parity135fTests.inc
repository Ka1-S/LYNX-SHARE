// --- round 135 (parity135f): FabFilter-role parity — Apex Limiter's loudness meter, true-peak metering, oversampling and
// side-chain triggering (docs/PARITY.md). Every new parameter is appended; its default leaves the sound unchanged.

#include "core/ApexLimiter.h"
#include "core/Loudness.h"
#include "core/MixSuite.h"

namespace parity135f
{
inline lynx::EffectParams defaults (lynx::EffectKind kind) { return lynx::makeEffectSlot (kind).params; }

inline void sine (std::vector<float>& x, int n, double sr, double hz, double amp, double phase = 0.0)
{
    x.resize ((std::size_t) n);
    for (int i = 0; i < n; ++i) x[(std::size_t) i] = (float) (amp * std::sin (2.0 * M_PI * hz * i / sr + phase));
}

inline void run (lynx::loudness::Meter& m, std::vector<float>& l, std::vector<float>& r, int block = 512)
{
    for (int i = 0; i < (int) l.size(); i += block)
        m.process (l.data() + i, r.data() + i, std::min (block, (int) l.size() - i));
}

inline double db (double x) { return 20.0 * std::log10 (x); }
} // namespace parity135f

TEST (parity135f_kweighting_48k_matches_bs1770)
{
    lynx::loudness::Biquad a, b;
    lynx::loudness::kWeighting (48000.0, a, b);
    CHECK (std::abs (a.b0 - 1.53512485958697) < 1e-9 && std::abs (a.b1 + 2.69169618940638) < 1e-9 && std::abs (a.b2 - 1.19839281085285) < 1e-9);
    CHECK (std::abs (a.a1 + 1.69065929318241) < 1e-9 && std::abs (a.a2 - 0.73248077421585) < 1e-9);
    CHECK (std::abs (b.a1 + 1.99004745483398) < 1e-9 && std::abs (b.a2 - 0.99007225036621) < 1e-9);
}

TEST (parity135f_loudness_ebu_cases)
{
    using namespace parity135f;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // EBU Tech 3341 case 1: a stereo 1 kHz sine at −23 dBFS = −23.0 LUFS (M, S, I)
        lynx::loudness::Meter m;
        m.prepare (sr);
        std::vector<float> l, r;
        sine (l, (int) (20 * sr), sr, 1000.0, std::pow (10.0, -23.0 / 20.0));
        r = l;
        run (m, l, r);
        std::printf ("  loudness %.0f Hz: M %.2f S %.2f I %.2f LRA %.2f\n", sr, m.momentary(), m.shortTermLoudness(), m.integratedLoudness(), m.loudnessRange());
        CHECK (std::abs (m.momentary() + 23.0) < 0.1 && std::abs (m.shortTermLoudness() + 23.0) < 0.1 && std::abs (m.integratedLoudness() + 23.0) < 0.1);
        CHECK (m.loudnessRange() < 0.3);
    }
    {
        // case 3: −36 / −23 / −36 dBFS for 10 / 20 / 10 s → I = −23 (the relative gate drops the quiet parts)
        const double sr = 48000.0;
        lynx::loudness::Meter m;
        m.prepare (sr);
        for (const auto [secs, level] : { std::pair { 10, -36.0 }, std::pair { 20, -23.0 }, std::pair { 10, -36.0 } })
        {
            std::vector<float> l, r;
            sine (l, (int) (secs * sr), sr, 1000.0, std::pow (10.0, level / 20.0));
            r = l;
            run (m, l, r);
        }
        std::printf ("  loudness gated: I %.2f\n", m.integratedLoudness());
        CHECK (std::abs (m.integratedLoudness() + 23.0) < 0.1);
    }
    {
        // EBU Tech 3342 case 1: 20 s at −20 then 20 s at −30 LUFS → LRA 10 LU (±1)
        const double sr = 48000.0;
        lynx::loudness::Meter m;
        m.prepare (sr);
        for (const double level : { -20.0, -30.0 })
        {
            std::vector<float> l, r;
            sine (l, (int) (20 * sr), sr, 1000.0, std::pow (10.0, level / 20.0));
            r = l;
            run (m, l, r);
        }
        std::printf ("  loudness range: %.2f LU\n", m.loudnessRange());
        CHECK (std::abs (m.loudnessRange() - 10.0) < 1.0);
    }
}

TEST (parity135f_true_peak)
{
    using namespace parity135f;
    // a sine at a quarter of the rate, 45° off the samples: sample peak −3 dB, true peak 0 dB
    lynx::loudness::Meter m;
    m.prepare (48000.0);
    std::vector<float> l, r;
    sine (l, 48000, 48000.0, 12000.0, 1.0, M_PI / 4.0);
    r = l;
    run (m, l, r);
    std::printf ("  true peak %.2f dBTP, sample peak %.2f dB\n", m.truePeakDb(), m.samplePeakDb());
    CHECK (std::abs (m.samplePeakDb() + 3.01) < 0.05);
    CHECK (std::abs (m.truePeakDb()) < 0.3);
}

TEST (parity135f_apex_new_params)
{
    using namespace lynx::apex;
    const auto& inf = info();
    CHECK ((int) inf.params.size() == paramCount && paramCount == 22);
    auto p = parity135f::defaults (lynx::EffectKind::apexLimiter);
    CHECK (oversamplingFactor (p[pOversampling]) == 1 && p[pSidechain] < 0.5f && displayMode (p[pDisplayMode]) == 0);
    CHECK (std::abs (loudnessTarget (p[pLoudnessTarget]) + 14.0) < 1e-9);
    CHECK (paramText (pOversampling, 1.0f) == "8x" && paramText (pPeakMeter, 1.0f) == "True peak" && paramText (pDisplayMode, 0.75f) == "Infinite");
    CHECK (lynx::formatEffectParam (lynx::EffectKind::apexLimiter, pLoudnessTarget, 0.0f, 120.0) == "-23 LUFS");
}

TEST (parity135f_apex_oversampling)
{
    using namespace lynx::apex;
    const double sr = 48000.0;
    for (const float os : { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f })
    {
        auto p = parity135f::defaults (lynx::EffectKind::apexLimiter);
        p[pOversampling] = os;
        p[pGain] = 12.0f / 30.0f;
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::apexLimiter);
        slot.params = p;
        auto fx = lynx::makeEffect (slot);
        CHECK (fx->latencySamples() == latencyFor (sr, oversamplingFactor (os)));
        fx->prepare (sr);
        const auto lat = fx->latencySamples();
        CHECK (lat == latencyFor (sr, oversamplingFactor (os)));
        // a hot two-tone signal: never above the ceiling (−0.1 dB)
        std::vector<float> l, r;
        parity135f::sine (l, 24000, sr, 997.0, 0.6);
        parity135f::sine (r, 24000, sr, 3001.0, 0.6);
        for (int i = 0; i < 24000; ++i) l[(std::size_t) i] += r[(std::size_t) i];
        r = l;
        for (int i = 0; i < 24000; i += 300) fx->process (l.data() + i, r.data() + i, 300, p, 120.0);
        float peak = 0.0f;
        for (int i = 4800; i < 24000; ++i) peak = std::max (peak, std::abs (l[(std::size_t) i]));
        std::printf ("  apex oversampling %dx: latency %d, peak %.4f dB\n", oversamplingFactor (os), lat, parity135f::db (peak));
        CHECK (peak <= std::pow (10.0, -0.1 / 20.0) * 1.0001);
        // alignment: a quiet click comes out after exactly the reported latency
        auto q = p;
        q[pGain] = 0.0f;
        fx->reset();
        std::vector<float> a (4096, 0.0f), b (4096, 0.0f);
        a[100] = b[100] = 0.25f;
        fx->process (a.data(), b.data(), 4096, q, 120.0);
        int at = 0;
        for (int i = 1; i < 4096; ++i) if (std::abs (a[(std::size_t) i]) > std::abs (a[(std::size_t) at])) at = i;
        CHECK (at == 100 + lat);
    }
}

TEST (parity135f_apex_sidechain_and_meters)
{
    using namespace lynx::apex;
    const double sr = 48000.0;
    auto p = parity135f::defaults (lynx::EffectKind::apexLimiter);
    p[pSidechain] = 1.0f;
    auto fx = make();
    fx->prepare (sr);
    std::vector<float> l, r, sl, sr2;
    parity135f::sine (l, 9600, sr, 440.0, 0.1);
    r = l;
    parity135f::sine (sl, 9600, sr, 440.0, 2.0); // a side chain 26 dB louder: 6 dB over the ceiling
    sr2 = sl;
    for (int i = 0; i < 9600; i += 480)
    {
        fx->setSidechain (sl.data() + i, sr2.data() + i);
        fx->process (l.data() + i, r.data() + i, 480, p, 120.0);
    }
    float peak = 0.0f;
    for (int i = 4800; i < 9600; ++i) peak = std::max (peak, std::abs (l[(std::size_t) i]));
    std::printf ("  apex side chain: quiet programme %.2f dB, gain reduction meter %.1f dB\n", parity135f::db (peak), fx->controlOutput (0) * 30.0);
    CHECK (peak < 0.1f * 0.6f && fx->controlOutput (0) * 30.0f > 5.0f);
    // without a side-chain block the programme is left alone (no limiting at −20 dB)
    for (int k = 0; k < 400; ++k) fx->process (l.data(), r.data(), 480, p, 120.0);
    CHECK (fx->controlOutput (0) < 0.01f);

    // the loudness meter on the output: a −23 dBFS 1 kHz sine, no limiting → I ≈ −23 LUFS; the reset button clears it
    auto q = parity135f::defaults (lynx::EffectKind::apexLimiter);
    auto g = make();
    g->prepare (sr);
    std::vector<float> a, b;
    parity135f::sine (a, (int) (5 * sr), sr, 1000.0, std::pow (10.0, -23.0 / 20.0));
    b = a;
    for (int i = 0; i < (int) a.size(); i += 512) g->process (a.data() + i, b.data() + i, std::min (512, (int) a.size() - i), q, 120.0);
    std::printf ("  apex loudness: M %.2f S %.2f I %.2f LRA %.2f peak %.2f\n", g->controlOutput (1), g->controlOutput (4), g->controlOutput (5), g->controlOutput (6), g->controlOutput (7));
    CHECK (std::abs (g->controlOutput (5) + 23.0f) < 0.15f && std::abs (g->controlOutput (1) + 23.0f) < 0.15f);
    CHECK (std::abs (g->controlOutput (7) + 23.0f) < 0.2f);
    q[pLoudnessReset] = 1.0f;
    std::vector<float> z (512, 0.0f), z2 (512, 0.0f);
    g->process (z.data(), z2.data(), 512, q, 120.0);
    CHECK (g->controlOutput (5) < -100.0f);
}

TEST (parity135f_sentry_and_hush_oversampling)
{
    // with oversampling on, the latency grows by the oversampler's and the result matches the base-rate one (delayed)
    const double sr = 48000.0;
    struct Case { lynx::EffectKind kind; int param; };
    for (const auto c : { Case { lynx::EffectKind::sentryGate, lynx::dyn::sentry::oversampling },
                          Case { lynx::EffectKind::hushDeEsser, lynx::mixsuite::hush::oversampling } })
    {
        auto slot = lynx::makeEffectSlot (c.kind);
        CHECK (slot.params[(std::size_t) c.param] == 0.0f);
        CHECK (lynx::formatEffectParam (c.kind, c.param, 2.0f / 3.0f, 120.0) == "4x");
        auto base = lynx::makeEffect (slot);
        auto q = slot;
        q.params[(std::size_t) c.param] = 2.0f / 3.0f;
        auto over = lynx::makeEffect (q);
        base->prepare (sr);
        over->prepare (sr);
        // a bursty test signal: 3 kHz + 7 kHz bursts over a quiet floor (opens / closes the gate, triggers the de-esser)
        const int n = 48000;
        std::vector<float> l ((std::size_t) n), r;
        for (int i = 0; i < n; ++i)
        {
            const double a = (i / 4800) % 2 == 0 ? 0.5 : 0.003;
            l[(std::size_t) i] = (float) (a * (std::sin (2.0 * M_PI * 3000.0 * i / sr) + 0.6 * std::sin (2.0 * M_PI * 7000.0 * i / sr)));
        }
        r = l;
        auto l2 = l, r2 = r;
        for (int i = 0; i < n; i += 2000) // blocks bigger than the oversampled path's 1024 too
        {
            base->process (l.data() + i, r.data() + i, std::min (2000, n - i), slot.params, 120.0);
            over->process (l2.data() + i, r2.data() + i, std::min (2000, n - i), q.params, 120.0);
        }
        const int extra = over->latencySamples() - base->latencySamples();
        CHECK (extra == lynx::mixsuite::Oversampler::latency());
        double e = 0.0, s = 0.0;
        for (int i = 9600; i < n - extra; ++i)
        {
            const double d = (double) l2[(std::size_t) (i + extra)] - l[(std::size_t) i];
            e += d * d;
            s += (double) l[(std::size_t) i] * l[(std::size_t) i];
        }
        std::printf ("  %s oversampling 4x: latency +%d, difference %.1f dB\n", lynx::effectInfo (c.kind).name, extra, 10.0 * std::log10 (e / s));
        CHECK (10.0 * std::log10 (e / s) < -20.0);
    }
}

TEST (parity135f_prism_six_bands)
{
    using namespace dyn108;
    namespace m = lynx::dyn::prism;
    // the old parameter block keeps its indices; the new ones are appended
    CHECK (m::bandLookahead0 == 48 && m::band5 == 52 && m::xover4 == 72 && m::bandCountParam == 76 && m::scExternal == 83 && m::processingMode == 84 && m::panMode == 86 && m::dynamicEngine == 87 && m::bandLink0 == 88 && m::count == 157); // (tp172b6: + Audition) // round 144: + processing, oversampling, pan mode
    CHECK ((int) lynx::effectInfo (EffectKind::prismMultiband).params.size() == m::count);
    auto p = defaults (EffectKind::prismMultiband);
    CHECK (m::bandCount (p) == 4 && m::param (4, m::fEnabled) == 60 && m::param (5, m::fSolo) == 71);
    CHECK (lynx::dyn::paramText (EffectKind::prismMultiband, m::param (4, m::fThreshold), m::thresholdValue (-18.0)) == "-18.0 dB");
    CHECK (lynx::dyn::paramText (EffectKind::prismMultiband, m::bandPanParam (2), 0.25f) == "50% L");
    // six bands, every range at 0: the output is the input exactly (transparent at rest)
    p[m::bandCountParam] = m::bandCountValue (6);
    for (int b = 0; b < m::bands; ++b) p[(std::size_t) m::param (b, m::fRange)] = 0.5f;
    std::vector<float> noise ((std::size_t) sr / 2);
    std::uint32_t seed = 7;
    for (auto& v : noise) { seed = seed * 1664525u + 1013904223u; v = (float) ((double) (seed >> 8) / 16777216.0 - 0.5); }
    auto y = run (EffectKind::prismMultiband, p, noise);
    double err = 0.0;
    for (std::size_t i = 0; i < noise.size(); ++i) err = std::max (err, (double) std::abs (y[i] - noise[i]));
    CHECK (err == 0.0);
    // band 6 (above 4 kHz here) compresses a 16 kHz tone; a 1 kHz tone is left alone (tones away from the crossovers:
    // near one, the band's phase-shifted part adds instead of cutting — the transparent structure's known limit)
    p = defaults (EffectKind::prismMultiband);
    p[m::bandCountParam] = m::bandCountValue (6);
    p[m::xover1] = m::crossoverValue (100.0);
    p[m::xover2] = m::crossoverValue (200.0);
    p[m::xover3] = m::crossoverValue (2000.0);
    p[m::xover4] = m::crossoverValue (3000.0);
    p[m::xover5] = m::crossoverValue (4000.0);
    for (int b = 0; b < m::bands; ++b) p[(std::size_t) m::param (b, m::fEnabled)] = b == 5 ? 1.0f : 0.0f;
    p[(std::size_t) m::param (5, m::fThreshold)] = m::thresholdValue (-40.0);
    p[(std::size_t) m::param (5, m::fRange)] = 0.0f; // −30 dB
    auto tone = [] (double hz) {
        std::vector<float> x ((std::size_t) sr);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.3 * std::sin (2.0 * pi * hz * (double) i / sr));
        return x;
    };
    auto level = [] (const std::vector<float>& x) {
        double s = 0.0;
        for (std::size_t i = x.size() / 2; i < x.size(); ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / (double) (x.size() / 2) + 1e-30);
    };
    // (the output level of a tone this far up also depends on the crossover tree's phase there: the band's own gain
    // meter is the check)
    auto bandGain = [&] (double hz) {
        auto slot = lynx::makeEffectSlot (EffectKind::prismMultiband);
        slot.params = p;
        auto fx = lynx::makeEffect (slot);
        fx->prepare (sr);
        auto x = tone (hz), r = x;
        for (std::size_t off = 0; off < x.size(); off += 512)
            fx->process (x.data() + off, r.data() + off, (int) std::min<std::size_t> (512, x.size() - off), p, 120.0);
        return fx->controlOutput (m::bandMeter (5)) * 30.0f;
    };
    const auto hi = bandGain (16000.0), lo = bandGain (1000.0);
    const auto loLevel = level (run (EffectKind::prismMultiband, p, tone (1000.0))) - level (tone (1000.0));
    std::printf ("  prism 6 bands: band 6 gain change at 16 kHz %.1f dB, at 1 kHz %.2f dB (1 kHz level %.2f dB)\n", hi, lo, loLevel);
    CHECK (hi < -6.0f && std::abs (lo) < 0.1f && std::abs (loLevel) < 0.1);
    // the same band in a 4-band setting does not exist: band 6's settings are ignored
    p[m::bandCountParam] = m::bandCountValue (4);
    CHECK (std::abs (level (run (EffectKind::prismMultiband, p, tone (16000.0))) - level (tone (16000.0))) < 0.1);
}

TEST (parity135f_prism_pan_and_sidechain)
{
    using namespace dyn108;
    namespace m = lynx::dyn::prism;
    auto p = defaults (EffectKind::prismMultiband);
    // band 2 (120 Hz … 1 kHz) with a +6 dB level and no dynamics, panned hard left: the right channel loses the band
    for (int b = 0; b < m::legacyBands; ++b) p[(std::size_t) m::param (b, m::fEnabled)] = b == 1 ? 1.0f : 0.0f;
    p[(std::size_t) m::param (1, m::fRange)] = 0.5f;
    p[(std::size_t) m::bandPanParam (1)] = 0.0f;
    auto slot = lynx::makeEffectSlot (EffectKind::prismMultiband);
    slot.params = p;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    std::vector<float> l ((std::size_t) sr / 2), r;
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = (float) (0.2 * std::sin (2.0 * pi * 400.0 * (double) i / sr));
    r = l;
    const auto x = l;
    for (std::size_t off = 0; off < l.size(); off += 512)
        fx->process (l.data() + off, r.data() + off, (int) std::min<std::size_t> (512, l.size() - off), p, 120.0);
    float pl = 0.0f, pr = 0.0f, px = 0.0f;
    for (std::size_t i = l.size() / 2; i < l.size(); ++i) { pl = std::max (pl, std::abs (l[i])); pr = std::max (pr, std::abs (r[i])); px = std::max (px, std::abs (x[i])); }
    std::printf ("  prism pan: left %.3f right %.3f (input %.3f)\n", pl, pr, px);
    CHECK (std::abs (pl - px) < 0.01f * px && pr < 0.4f * px);
    // external side chain: a quiet programme is compressed when the side chain is loud
    p = defaults (EffectKind::prismMultiband);
    p[m::scExternal] = 1.0f;
    for (int b = 0; b < m::legacyBands; ++b) p[(std::size_t) m::param (b, m::fThreshold)] = m::thresholdValue (-30.0);
    std::vector<float> quiet ((std::size_t) sr), loud ((std::size_t) sr);
    for (std::size_t i = 0; i < quiet.size(); ++i)
    {
        quiet[i] = (float) (0.01 * std::sin (2.0 * pi * 400.0 * (double) i / sr));
        loud[i] = (float) (0.9 * std::sin (2.0 * pi * 400.0 * (double) i / sr));
    }
    const auto with = run (EffectKind::prismMultiband, p, quiet, nullptr, &loud);
    const auto without = run (EffectKind::prismMultiband, p, quiet);
    float a = 0.0f, b = 0.0f;
    for (std::size_t i = quiet.size() / 2; i < quiet.size() - 2000; ++i) { a = std::max (a, std::abs (with[i])); b = std::max (b, std::abs (without[i])); }
    std::printf ("  prism external side chain: %.4f (loud key) vs %.4f (none)\n", a, b);
    CHECK (a < 0.5f * b && std::abs (b - 0.01f) < 0.002f);
}

TEST (parity135f_crest_wet_dry_pan)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    auto p = defaults (EffectKind::crestCompressor);
    CHECK (p[c::wetPan] == 0.5f && p[c::dryPan] == 0.5f && c::trigger == c::dryPan + 1);
    CHECK (lynx::dyn::paramText (EffectKind::crestCompressor, c::wetPan, 1.0f) == "100% R");
    // all wet, the wet path panned hard right: the left channel is silent, the right one is the compressed signal
    auto run2 = [] (const EffectParams& q, std::vector<float>& l, std::vector<float>& r) {
        auto slot = lynx::makeEffectSlot (EffectKind::crestCompressor);
        slot.params = q;
        auto fx = lynx::makeEffect (slot);
        fx->prepare (sr);
        for (std::size_t off = 0; off < l.size(); off += 512)
            fx->process (l.data() + off, r.data() + off, (int) std::min<std::size_t> (512, l.size() - off), q, 120.0);
    };
    std::vector<float> l ((std::size_t) sr / 2), r;
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = (float) (0.3 * std::sin (2.0 * pi * 300.0 * (double) i / sr));
    r = l;
    auto l0 = l, r0 = r;
    run2 (p, l0, r0);
    p[c::wetPan] = 1.0f;
    run2 (p, l, r);
    float pl = 0.0f, dr = 0.0f;
    for (std::size_t i = 0; i < l.size(); ++i) { pl = std::max (pl, std::abs (l[i])); dr = std::max (dr, std::abs (r[i] - r0[i])); }
    std::printf ("  crest wet pan right: left peak %.5f, right vs centred %.6f\n", pl, dr);
    CHECK (pl < 1e-6f && dr < 1e-5f);
}

TEST (parity135f_facet_new_shapes_and_character)
{
    using namespace lynx::facet;
    // the nine original shapes keep their values; the new ones sit between them and round-trip
    for (int k = 0; k <= 8; ++k) CHECK (shape ((float) k / 8.0f) == (Shape) k && shapeValue ((Shape) k) == (float) k / 8.0f);
    CHECK (shape (shapeValue (Shape::flatTilt)) == Shape::flatTilt && shape (shapeValue (Shape::allPass)) == Shape::allPass);
    CHECK (paramText (param (0, 0), shapeValue (Shape::allPass)) == "All Pass" && paramText (characterParam, 1.0f) == "Warm");
    CHECK ((int) info().params.size() == paramCount && info().params[(std::size_t) characterParam].defaultValue == 0.0f);
    auto p = parity135f::defaults (lynx::EffectKind::facetEq);
    // all pass at 1 kHz: flat magnitude (digital and analog)
    p[(std::size_t) param (0, 0)] = shapeValue (Shape::allPass);
    p[(std::size_t) param (0, 1)] = hzValue (1000.0);
    double worst = 0.0;
    for (const double f : { 50.0, 300.0, 1000.0, 3000.0, 12000.0 }) worst = std::max (worst, std::abs (responseDb (p, 48000.0, f)));
    CHECK (worst < 0.01);
    // ... but it does move the phase: a 1 kHz sine comes out shifted (not equal to the input)
    {
        auto fx = make (&p);
        fx->prepare (48000.0);
        std::vector<float> l, r;
        parity135f::sine (l, 4800, 48000.0, 1000.0, 0.5);
        r = l;
        const auto x = l;
        fx->process (l.data(), r.data(), 4800, p, 120.0);
        double d = 0.0;
        for (int i = 2400; i < 4800; ++i) d = std::max (d, (double) std::abs (l[(std::size_t) i] - x[(std::size_t) i]));
        CHECK (d > 0.3);
    }
    // flat tilt +6 dB: 0 dB at the band frequency, about +6 dB five octaves above, −6 dB five below, a steady slope
    p[(std::size_t) param (0, 0)] = shapeValue (Shape::flatTilt);
    p[(std::size_t) param (0, 2)] = gainValue (6.0);
    const auto at = [&] (double f) { return responseDb (p, 96000.0, f); };
    std::printf ("  facet flat tilt +6: %.2f / %.2f / %.2f / %.2f / %.2f dB at 31 Hz / 250 Hz / 1 kHz / 4 kHz / 32 kHz\n", at (31.25), at (250.0), at (1000.0), at (4000.0), at (32000.0));
    CHECK (std::abs (at (1000.0)) < 0.2 && std::abs (at (32000.0) - 6.0) < 1.0 && std::abs (at (31.25) + 6.0) < 1.0);
    CHECK (std::abs (at (4000.0) - 2.4) < 0.6 && std::abs (at (250.0) + 2.4) < 0.6);
    // character: Clean leaves the signal alone; Subtle bends a 0 dBFS peak, not a −30 dBFS one
    CHECK (characterSample (0, 0.9f) == 0.9f);
    CHECK (std::abs (characterSample (1, 0.0316f) - 0.0316f) < 0.0316f * 0.001f && characterSample (1, 1.0f) < 0.97f);
    CHECK (std::abs (characterSample (2, 0.0316f) - 0.0316f) < 0.0316f * 0.002f && characterSample (2, 1.0f) < characterSample (1, 1.0f));
}

TEST (parity135f_lynxtune_learn_and_classic)
{
    using namespace lynx::lynxtune;
    CHECK (learnMidi == moreScales + 1 && classicMode == learnMidi + 1 && speedLink == classicMode + 1);
    CHECK ((int) lynx::effectInfo (lynx::EffectKind::lynxTune).params.size() == count);
    CHECK (paramText (classicMode, 1.0f) == "On");
    auto p = parity135f::defaults (lynx::EffectKind::lynxTune);
    p[midiChannel] = 1.0f / 128.0f; // channel 1
    auto fx = make();
    fx->prepare (48000.0);
    lynx::NoteMask held[2] {};
    held[0][0] = 1ull << 60; // C4
    held[0][1] = 1ull << (64 - 64); // E4
    fx->setHeldNotes (held, 2);
    std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
    fx->process (l.data(), r.data(), 2048, p, 120.0);
    const auto mask = (unsigned) std::lround (fx->controlOutput (5) * 4096.0f);
    CHECK (mask == ((1u << 0) | (1u << 4)));
    // classic ([EXEC] Auto-Tune Pro: the same retune, the short-latency DSP): the same pull to A (440 Hz), less latency
    auto run = [] (bool classic, int* latency) {
        auto q = parity135f::defaults (lynx::EffectKind::lynxTune);
        q[scale] = 0.0f; // chromatic
        q[retune] = std::sqrt (150.0f / 400.0f);
        q[classicMode] = classic ? 1.0f : 0.0f;
        auto t = make();
        t->prepare (48000.0);
        std::vector<float> a, b;
        parity135f::sine (a, 96000, 48000.0, 452.0, 0.3);
        b = a;
        for (int i = 0; i < 96000; i += 512) t->process (a.data() + i, b.data() + i, std::min (512, 96000 - i), q, 120.0);
        *latency = t->latencySamples();
        return a;
    };
    int latNormal = 0, latClassic = 0;
    const auto normal = run (false, &latNormal), classic = run (true, &latClassic);
    auto freq = [] (const std::vector<float>& x, int from, int to) {
        int crossings = 0;
        for (int i = from + 1; i < to; ++i) if (x[(std::size_t) i - 1] < 0.0f && x[(std::size_t) i] >= 0.0f) ++crossings;
        return crossings * 48000.0 / (to - from);
    };
    const auto fn = freq (normal, 72000, 96000), fc = freq (classic, 72000, 96000);
    std::printf ("  lynx tune classic: end %.1f Hz (default %.1f Hz), latency %d (default %d)\n", fc, fn, latClassic, latNormal);
    CHECK (std::abs (fc - 440.0) < 3.0 && std::abs (fn - 440.0) < 3.0 && latClassic < latNormal);
}
