// --- Round 150: Prism Multiband ← Pro-MB, tier 4 (REA_rodada150): slopes 6 … 48 dB/oct, the free layout with gaps,
// per-band side-chain inputs, the narrow free side-chain filter, the measured detector ------------------------------

namespace p150
{
namespace m = lynx::dyn::prism;
inline lynx::EffectParams freeBands (std::initializer_list<std::array<double, 5>> bands) // { lo, hi, lo slope, hi slope, level dB }
{
    auto p = dyn108::defaults (lynx::EffectKind::prismMultiband);
    p[m::bandLayout] = 1.0f;
    int b = 0;
    for (const auto& x : bands)
    {
        p[(std::size_t) (m::bandLo0 + b)] = m::edgeValue (x[0]);
        p[(std::size_t) (m::bandHi0 + b)] = m::edgeValue (x[1]);
        p[(std::size_t) (m::bandLoSlope0 + b)] = m::slopeValue (x[2]);
        p[(std::size_t) (m::bandHiSlope0 + b)] = m::slopeValue (x[3]);
        p[(std::size_t) m::param (b, m::fRange)] = 0.5f;
        p[(std::size_t) m::param (b, m::fLevel)] = (float) ((x[4] + 30.0) / 60.0);
        p[(std::size_t) m::param (b, m::fEnabled)] = 1.0f;
        ++b;
    }
    p[m::bandCountParam] = m::bandCountValue (b);
    return p;
}
inline double curveDb (const lynx::EffectParams& p, double hz)
{
    double g[m::bands] {};
    for (int b = 0; b < m::bands; ++b)
        g[b] = m::levelDb (p[(std::size_t) m::param (b, m::fLevel)]);
    return m::responseDb (p, g, 48000.0, hz);
}
} // namespace p150

TEST (prism_slope_laws_and_layout_round150)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // the reference's value law: 0 → 6, 0.25 → 12, 0.5 → 24, 0.75 → 36, 1 → 48 dB/oct
    CHECK (std::abs (m::slopeOfValue (0.0f) - 6.0) < 1e-9 && std::abs (m::slopeOfValue (0.25f) - 12.0) < 1e-6);
    CHECK (std::abs (m::slopeOfValue (0.5f) - 24.0) < 1e-6 && std::abs (m::slopeOfValue (0.75f) - 36.0) < 1e-6);
    CHECK (std::abs (m::slopeOfValue (m::slopeValue (18.0)) - 18.0) < 1e-4 && std::abs (m::slopeOfValue (m::slopeValue (9.0)) - 9.0) < 1e-4);
    // [EXEC] Dynamic / Minimum phase run a slope as 6 (below 9), 12 (9 … <18), 24 (18 … <30), 36 (30 … <42), 48
    CHECK (m::iirSlope (8.9) == 6 && m::iirSlope (9.0) == 12 && m::iirSlope (17.9) == 12 && m::iirSlope (18.0) == 24);
    CHECK (m::iirSlope (29.9) == 24 && m::iirSlope (30.0) == 36 && m::iirSlope (41.9) == 36 && m::iirSlope (42.0) == 48);
    // the split layout is the old one: band b = segment b
    const auto d = makeEffectSlot (EffectKind::prismMultiband).params;
    CHECK (! m::freeLayout (d) && m::measuredDetector (d));
    const auto L0 = m::layoutOf (d);
    CHECK (L0.segments == 4 && L0.bandOf[2] == 2 && std::abs (L0.xoverHz[1] - 1000.0) < 1.0);
    // the free layout: bands in order of their low edge, the gaps between them are segments, no overlap
    const auto p = p150::freeBands ({ { 1000.0, 3000.0, 24, 24, -12 }, { 100.0, 300.0, 24, 24, -12 }, { 2000.0, 30000.0, 24, 24, 0 } });
    const auto L = m::layoutOf (p);
    CHECK (L.segments == 5); // gap, b2, gap, b1, b3 (b3 starts where b1 ends)
    CHECK (L.bandOf[0] == -1 && L.bandOf[1] == 1 && L.bandOf[2] == -1 && L.bandOf[3] == 0 && L.bandOf[4] == 2);
    CHECK (std::abs (L.loHz[2] - 3000.0) < 1.0 && L.hiHz[2] <= 0.0);
    CHECK (std::abs (L.xoverHz[0] - 100.0) < 0.5 && std::abs (L.xoverHz[3] - 3000.0) < 2.0);
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::bandLoSlope0, 0.375f, 120.0) == "18.0 dB/oct");
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::bandLayout, 1.0f, 120.0) == "free");
    CHECK ((int) effectInfo (EffectKind::prismMultiband).params.size() == m::count && m::count <= maxEffectParams);
}

TEST (prism_dynamic_phase_six_db_round150)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] Pro-MB Dynamic phase, static bands at 48 kHz (magnitude, dB)
    struct Case { std::array<double, 5> band; double hz[5]; double db[5]; double tol; };
    const Case cases[] {
        // a low edge band 30 Hz – 1 kHz, its high side 6 dB/oct, −12 dB: the first-order shelf
        { { 30.0, 1000.0, 24, 6, -12 }, { 125.0, 500.0, 1000.0, 2000.0, 8000.0 }, { -11.10, -6.02, -2.74, -0.89, -0.05 }, 0.05 },
        // 300 Hz – 3 kHz, both sides 6 dB/oct, −12 dB: the bell
        { { 300.0, 3000.0, 6, 6, -12 }, { 150.0, 300.0, 1000.0, 3000.0, 6000.0 }, { -0.81, -2.33, -4.93, -2.33, -0.76 }, 0.06 },
        // 6 / 24 dB/oct, −12 dB (the measured table) and +6 dB (exact)
        { { 300.0, 3000.0, 6, 24, -12 }, { 150.0, 300.0, 1000.0, 3000.0, 6000.0 }, { -0.95, -2.75, -7.23, -3.96, -0.35 }, 0.08 },
        { { 300.0, 3000.0, 6, 24, 6 }, { 150.0, 300.0, 1000.0, 3000.0, 6000.0 }, { 2.03, 3.97, 5.67, 3.48, 0.42 }, 0.06 },
        { { 300.0, 3000.0, 6, 24, -20 }, { 150.0, 300.0, 1000.0, 3000.0, 6000.0 }, { -2.07, -5.03, -10.34, -6.99, -0.89 }, 0.1 },
        { { 300.0, 1200.0, 6, 12, -12 }, { 150.0, 300.0, 1000.0, 3000.0, 6000.0 }, { -1.18, -2.93, -3.85, -1.00, -0.26 }, 0.08 },
    };
    for (const auto& c : cases)
    {
        const auto p = p150::freeBands ({ c.band });
        for (int k = 0; k < 5; ++k)
        {
            const auto curve = p150::curveDb (p, c.hz[k]);
            const auto tone = p147::toneGain (p, c.hz[k]);
            std::printf ("  Prism dynamic %.0f-%.0f Hz %.0f/%.0f dB/oct %+.0f dB: %.0f Hz curve %.2f tone %.2f dB (Pro-MB %.2f)\n",
                         c.band[0], c.band[1], c.band[2], c.band[3], c.band[4], c.hz[k], curve, tone, c.db[k]);
            CHECK (std::abs (curve - c.db[k]) < c.tol);
            CHECK (std::abs (tone - c.db[k]) < c.tol + 0.05);
        }
    }
    // in-between slopes run as the measured shapes: 18 dB/oct = 24, 30 = 36 (Dynamic phase)
    const auto a = p150::freeBands ({ { 30.0, 1000.0, 24, 18, -12 } }), b = p150::freeBands ({ { 30.0, 1000.0, 24, 24, -12 } });
    CHECK (std::abs (p150::curveDb (a, 700.0) - p150::curveDb (b, 700.0)) < 1e-9);
    const auto c36 = p150::freeBands ({ { 30.0, 1000.0, 24, 36, -12 } });
    CHECK (std::abs (p147::toneGain (c36, 700.0) - (-9.62)) < 0.08); // [EXEC] −9.62 dB at 700 Hz
}

TEST (prism_linear_and_minimum_free_round150)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // linear phase: the reference's continuous slopes, 1 / (1 + x^m), m = slope / 6 (9 dB/oct: m = 1.5); round tp172b7:
    // x = f / fc, the analog axis (Pro-MB probes p18b / p19: Linear phase follows it up to Nyquist)
    auto p = p150::freeBands ({ { 30.0, 1000.0, 24, 9, -12 } });
    p[m::processingMode] = 0.0f;
    const auto g = std::pow (10.0, -12.0 / 20.0);
    for (const double hz : { 250.0, 1000.0, 2000.0, 4000.0 })
    {
        const auto w = hz, wc = 1000.0;
        const auto want = 20.0 * std::log10 (1.0 + (g - 1.0) / (1.0 + std::pow (w / wc, 1.5)));
        const auto tone = p147::toneGain (p, hz);
        std::printf ("  Prism linear 9 dB/oct: %.0f Hz %.2f dB (law %.2f)\n", hz, tone, want);
        CHECK (std::abs (p150::curveDb (p, hz) - want) < 0.01);
        CHECK (std::abs (tone - want) < 0.1);
    }
    // [EXEC] two bands with a gap (100 – 300 Hz and 1 – 3 kHz at −12 dB, 24 dB/oct): Minimum / Linear phase
    auto q = p150::freeBands ({ { 100.0, 300.0, 24, 24, -12 }, { 1000.0, 3000.0, 24, 24, -12 } });
    const double hz[] { 150.0, 300.0, 1000.0, 3000.0, 6000.0 };
    const double minDb[] { -7.72, -4.04, -4.05, -4.01, -0.33 }, linDb[] { -7.66, -4.05, -4.05, -4.01, -0.39 };
    for (int mode = 0; mode < 2; ++mode)
    {
        q[m::processingMode] = mode == 0 ? 1.0f : 0.0f;
        for (int k = 0; k < 5; ++k)
        {
            const auto tone = p147::toneGain (q, hz[k]);
            const auto want = mode == 0 ? minDb[k] : linDb[k];
            std::printf ("  Prism gap %s: %.0f Hz %.2f dB (Pro-MB %.2f)\n", mode == 0 ? "minimum" : "linear", hz[k], tone, want);
            CHECK (std::abs (tone - want) < 0.12);
            CHECK (std::abs (p150::curveDb (q, hz[k]) - want) < 0.12);
        }
    }
    // a gap passes untouched: 600 Hz between the bands in the minimum-phase sum (only the all-pass phase)
    q[m::processingMode] = 1.0f;
    CHECK (std::abs (p147::toneGain (q, 600.0) - (-1.35)) < 0.5);
}

TEST (prism_measured_detector_round150)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] the soft range: threshold −30, 4 : 1, knee 0 — 12 dB over: 9 dB unlimited, 8.61 under a 12 dB range,
    // 5.62 under 6; 24 dB over: 18 / 12.0 / 6.0
    auto p = p147::wholeBand (-30.0, 4.0);
    const auto stat = [&] (double rangeDb, double inDb, bool measured) {
        auto q = p;
        q[(std::size_t) m::param (0, m::fRange)] = (float) ((rangeDb + 30.0) / 60.0);
        q[m::detectorLaw] = measured ? 1.0f : 0.0f;
        return m::bandStaticDb (q, 0, inDb);
    };
    CHECK (std::abs (stat (-12.0, -18.0, true) + 8.625) < 0.01 && std::abs (stat (-6.0, -18.0, true) + 5.625) < 0.01);
    CHECK (std::abs (stat (-12.0, -6.0, true) + 12.0) < 0.01 && std::abs (stat (-30.0, -18.0, true) + 9.0) < 0.01);
    CHECK (std::abs (stat (-12.0, -18.0, false) + 9.0) < 0.01); // the classic detector: a hard limit
    // [EXEC] the gain after a 1 kHz step −6 → −30 dBFS (attack / release 20 %): Pro-MB −14.53 / −10.28 / −4.63 dB after
    // 50 / 100 / 200 ms (range −30), −10.01 / −7.15 / −3.25 with a −12 dB range (no hold at the range)
    for (const double range : { -30.0, -12.0 })
        for (const bool measured : { true, false })
        {
            auto q = p;
            q[(std::size_t) m::param (0, m::fRange)] = (float) ((range + 30.0) / 60.0);
            q[m::detectorLaw] = measured ? 1.0f : 0.0f;
            std::vector<float> x (48000 * 2);
            for (std::size_t i = 0; i < x.size(); ++i)
                x[i] = (float) ((i < 48000 ? 0.5 : 0.0316) * std::sin (2.0 * dyn108::pi * 1000.0 * (double) i / 48000.0));
            const auto y = dyn108::run (EffectKind::prismMultiband, q, x);
            const auto at = [&] (int ms) {
                const auto a = (std::size_t) (48000 + 48 * ms);
                return dyn108::rmsDb (y, a - 24, a + 24) - dyn108::rmsDb (x, a - 24, a + 24);
            };
            const double want[2][3] { { -14.53, -10.28, -4.63 }, { -10.01, -7.15, -3.25 } };
            const auto& w = want[range < -20.0 ? 0 : 1];
            const double got[3] { at (50), at (100), at (200) };
            std::printf ("  Prism %s detector, range %.0f: release %.2f / %.2f / %.2f dB (Pro-MB %.2f / %.2f / %.2f)\n",
                         measured ? "measured" : "classic", range, got[0], got[1], got[2], w[0], w[1], w[2]);
            if (measured)
                for (int k = 0; k < 3; ++k)
                    CHECK (std::abs (got[k] - w[k]) < 0.8);
        }
    // a project saved before round 150 keeps the classic detector
    auto slot = makeEffectSlot (EffectKind::prismMultiband);
    upgradeLoadedEffectParams (slot, (std::size_t) m::detectorLaw);
    CHECK (! m::measuredDetector (slot.params) && m::measuredDynamic (slot.params));
    auto slot2 = makeEffectSlot (EffectKind::prismMultiband);
    upgradeLoadedEffectParams (slot2, (std::size_t) m::count);
    CHECK (m::measuredDetector (slot2.params));
}

TEST (prism_band_sidechain_inputs_round150)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] two bands split at 1 kHz, thresholds −20, 4 : 1; band 1 hears the external side chain, band 2 the
    // plug-in: a loud 200 Hz side chain pulls band 1 down (Pro-MB −10.3 dB), band 2's 3 kHz stays (−0.1)
    auto p = dyn108::defaults (EffectKind::prismMultiband);
    p[m::bandCountParam] = m::bandCountValue (2);
    p[m::xover1] = m::crossoverValue (1000.0);
    for (int b = 0; b < 2; ++b)
    {
        p[(std::size_t) m::param (b, m::fThreshold)] = m::thresholdValue (-20.0);
        p[(std::size_t) m::param (b, m::fRange)] = 0.0f;
        p[(std::size_t) m::param (b, m::fKnee)] = 0.0f;
    }
    p[m::bandScInput0] = 1.0f;
    p[m::bandScInput0 + 1] = 0.5f;
    std::vector<float> x (48000), sc (48000);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const auto t = (double) i / 48000.0;
        x[i] = (float) (0.0316 * std::sin (2.0 * dyn108::pi * 200.0 * t) + 0.0316 * std::sin (2.0 * dyn108::pi * 3000.0 * t));
        sc[i] = (float) (0.5 * std::sin (2.0 * dyn108::pi * 200.0 * t));
    }
    const auto y = dyn108::run (EffectKind::prismMultiband, p, x, nullptr, &sc);
    const auto tone = [] (const std::vector<float>& s, double hz) {
        std::complex<double> acc {};
        for (std::size_t i = 24000; i < 48000; ++i)
            acc += (double) s[i] * std::polar (1.0, -2.0 * dyn108::pi * hz * (double) i / 48000.0);
        return 20.0 * std::log10 (2.0 * std::abs (acc) / 24000.0);
    };
    const auto low = tone (y, 200.0) - tone (x, 200.0), high = tone (y, 3000.0) - tone (x, 3000.0);
    std::printf ("  Prism band side-chain inputs: band 1 (external) %.2f dB, band 2 (plug-in) %.2f dB (Pro-MB -10.31 / -0.12)\n", low, high);
    CHECK (low < -8.0 && low > -14.0);
    CHECK (std::abs (high) < 0.5);
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::bandScInput0, 1.0f, 120.0) == "external");
}

TEST (prism_narrow_sidechain_filter_round150)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] the free side-chain filter's narrow ranges boost (detector level of a −20 dB probe, relative):
    // 2 – 2.5 kHz: 1 kHz −23.8, 2 kHz +4.1, 2240 Hz +13.7, 3 kHz −5.0; 2 – 3 kHz: 2.5 kHz +10.6; 1 – 2 kHz: 1.5 kHz +5.7
    struct Case { double lo, hi, hz, rel; };
    const Case cases[] { { 2000, 2500, 1000, -23.8 }, { 2000, 2500, 2000, 4.1 }, { 2000, 2500, 2240, 13.7 }, { 2000, 2500, 3000, -5.0 },
                         { 2000, 3000, 2500, 10.6 }, { 1000, 2000, 1500, 5.7 }, { 2000, 4000, 3000, 5.5 } };
    for (const auto& c : cases)
    {
        auto p = p147::wholeBand (-60.0, 2.0);
        p[m::bandScFree0] = 1.0f;
        p[m::bandScLow0] = m::scFilterValue (c.lo);
        p[m::bandScHigh0] = m::scFilterValue (c.hi);
        // a −20 dBFS probe as in the measurement (louder ones reach the range's soft knee)
        std::vector<float> x (24000);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (0.1 * std::sin (2.0 * dyn108::pi * c.hz * (double) i / dyn108::sr));
        const auto y = dyn108::run (EffectKind::prismMultiband, p, x);
        const auto g = dyn108::rmsDb (y, 12000, 18000) - dyn108::rmsDb (x, 12000, 18000);
        const auto det = -2.0 * g - 60.0 - (-20.0);
        std::printf ("  Prism free side chain %.0f-%.0f Hz: %.0f Hz detector %+.1f dB (Pro-MB %+.1f)\n", c.lo, c.hi, c.hz, det, c.rel);
        CHECK (std::abs (det - c.rel) < 1.5);
    }
}

TEST (prism_promb_free_translation_round150)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // two Pro-MB bands with a gap: 100 – 300 Hz (6 / 36 dB/oct, −6 dB level, side chain external, Expert on) and
    // 1 – 3 kHz (24 / 18 dB/oct); the third band disabled (bypassed), the rest unused
    std::vector<std::pair<int, float>> mb { { 0, 1.0f }, { 1, (float) std::log2 (100.0) }, { 2, 0.0f }, { 3, (float) std::log2 (300.0) }, { 4, 0.75f },
                                            { 13, -6.0f }, { 18, 1.0f },
                                            { 22, 1.0f }, { 23, (float) std::log2 (1000.0) }, { 24, 0.5f }, { 25, (float) std::log2 (3000.0) }, { 26, 0.375f },
                                            { 44, 0.0f }, { 45, (float) std::log2 (6000.0) }, { 47, (float) std::log2 (9000.0) },
                                            { 133, 1.0f }, { 139, 1.0f }, { 142, 1.0f } };
    for (int b = 3; b < 6; ++b)
        mb.push_back ({ 22 * b, 2.0f });
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "FabFilter Pro-MB";
    id.state = dyn108::fabf (151, mb);
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    const auto slot = makeSubstituteSlot (*entry, &id, nullptr);
    const auto& p = slot.params;
    CHECK (m::freeLayout (p) && m::bandCount (p) == 3);
    CHECK (std::abs (m::edgeHz (p[m::bandLo0]) - 100.0) < 0.5 && std::abs (m::edgeHz (p[m::bandHi0]) - 300.0) < 1.0);
    CHECK (std::abs (m::slopeOfValue (p[m::bandLoSlope0]) - 6.0) < 0.01 && std::abs (m::slopeOfValue (p[m::bandHiSlope0]) - 36.0) < 0.01);
    CHECK (std::abs (m::edgeHz (p[m::bandLo0 + 1]) - 1000.0) < 2.0 && std::abs (m::slopeOfValue (p[m::bandHiSlope0 + 1]) - 18.0) < 0.01);
    CHECK (p[m::bandScInput0] == 1.0f && p[m::bandScInput0 + 1] == 0.5f);
    CHECK (p[(std::size_t) m::param (2, m::fEnabled)] < 0.5f && std::abs (m::edgeHz (p[m::bandLo0 + 2]) - 6000.0) < 10.0);
    CHECK (m::measuredDetector (p) && m::phaseOf (p[m::processingMode]) == m::Phase::dynamic);
    const auto L = m::layoutOf (p);
    CHECK (L.segments == 7 && L.bandOf[0] == -1 && L.bandOf[1] == 0 && L.bandOf[2] == -1 && L.bandOf[3] == 1);
}

TEST (compressor_rcomp_ranges_round150)
{
    using namespace lynx;
    // [EXEC] the Renaissance Compressor's own control laws (the trial's value texts): ratio 50 / 10.46 / 2.19 / 1.00 /
    // 0.79 / 0.50 at 0 / 0.25 / 0.5 / 0.625 / 0.75 / 1, attack 0.50 / 15.8 / 500 ms, release 5.00 / 158 / 5000 ms, gain ±30 dB
    const auto t = [] (int prm, float v) { return formatEffectParam (EffectKind::compressor, prm, v, 120.0); };
    CHECK (t (14, 0.0f) == "50.00:1" && t (14, 0.25f) == "10.46:1" && t (14, 0.5f) == "2.19:1" && t (14, 0.625f) == "1.00:1");
    CHECK (t (14, 0.75f) == "0.79:1" && t (14, 1.0f) == "0.50:1");
    CHECK (t (16, 0.0f) == "0.50 ms" && t (16, 0.5f) == "15.8 ms" && t (16, 1.0f) == "500.0 ms");
    CHECK (t (17, 0.0f) == "5.0 ms" && t (17, 0.5f) == "158 ms" && t (17, 1.0f) == "5000 ms");
    CHECK (t (15, 0.0f) == "-30.0 dB" && t (15, 1.0f) == "+30.0 dB" && t (13, 0.0f) == "LYNX");
    auto p = makeEffectSlot (EffectKind::compressor).params;
    CHECK (p[13] == 0.0f); // older projects and new instances: LYNX's ranges
    // [EXEC] 1 kHz stairs, threshold −30 dB, Manual: below 1 : 1 the reference expands above the threshold —
    // 0.50 : 1 +7.05 / +13.17 dB at −24 / −18 dBFS, 0.79 : 1 +3.57 / +6.00, 0.90 : 1 +1.78 / +2.91; 10.46 : 1 −6.99 / −11.98
    struct Case { float ratioV; double at24, at18; };
    const Case cases[] { { 1.0f, 7.05, 13.17 }, { 0.75f, 3.57, 6.00 }, { 0.68f, 1.78, 2.91 }, { 0.25f, -6.99, -11.98 } };
    for (const auto& c : cases)
    {
        auto q = p;
        q[8] = 1.0f;  // the vintage (measured) voicing
        q[9] = 1.0f;  // Manual
        q[0] = 0.5f;  // −30 dB
        q[13] = 1.0f; // the RComp ranges
        q[14] = c.ratioV;
        double got[2] {};
        for (int k = 0; k < 2; ++k)
        {
            const auto amp = std::pow (10.0, (k == 0 ? -24.0 : -18.0) / 20.0);
            std::vector<float> x (48000);
            for (std::size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (amp * std::sin (2.0 * dyn108::pi * 1000.0 * (double) i / 48000.0));
            const auto y = dyn108::run (EffectKind::compressor, q, x);
            got[k] = dyn108::rmsDb (y, 30000, 46000) - dyn108::rmsDb (x, 30000, 46000);
        }
        std::printf ("  Compressor RComp ranges, ratio %s: %+.2f / %+.2f dB at -24 / -18 dBFS (RComp %+.2f / %+.2f)\n",
                     t (14, c.ratioV).c_str(), got[0], got[1], c.at24, c.at18);
        // (the vintage voicing's compression away from her 2.95 : 1 keeps round 123's fit: 1.2 – 1.7 dB deeper at 10.46 : 1)
        const auto tol = c.ratioV < 0.6f ? 2.0 : 1.0;
        CHECK (std::abs (got[0] - c.at24) < tol && std::abs (got[1] - c.at18) < tol);
    }
    // the classic voicing expands too (textbook: 1 / ratio − 1 dB per dB over the threshold)
    auto q = p;
    q[0] = 0.5f;
    q[13] = 1.0f;
    q[14] = 1.0f; // 0.5 : 1
    std::vector<float> x (48000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.0631 * std::sin (2.0 * dyn108::pi * 1000.0 * (double) i / 48000.0)); // −24 dBFS
    const auto y = dyn108::run (EffectKind::compressor, q, x);
    CHECK (dyn108::rmsDb (y, 30000, 46000) - dyn108::rmsDb (x, 30000, 46000) > 4.0);
    // a translated RComp state uses the reference's ranges with its own numbers
    int n = 0;
    const auto s = vm123::wavesSlot ("RComp", "160.31 -15 -4 * * * * * 0.5 2.96 56.23 10 0 0 149.62 120 0 0 1 0.4 0 1.5 0 * * * 100 0", n);
    CHECK (s.params[13] == 1.0f && t (14, s.params[14]) == "2.96:1" && t (16, s.params[16]) == "0.50 ms");
    CHECK (t (17, s.params[17]) == "160 ms" && t (15, s.params[15]) == "-4.0 dB");
}

// round tp172b7 [EXEC] (Pro-MB, pink noise, band 500 Hz – 8 kHz at −12 dB, 24 dB/oct, 48 kHz; mode set by value): Linear
// phase follows the analog axis above the high edge (12 kHz −1.2, 16 kHz −0.4 dB), Minimum phase the prewarped one
// (−0.7 / −0.1 dB); both match LYNX's earlier Minimum phase within 0.1 dB, which stays as it was
TEST (prism_linear_phase_on_the_analog_axis_round172b7)
{
    using namespace lynx;
    namespace m = dyn::prism;
    auto p = p150::freeBands ({ { 500.0, 8000.0, 24, 24, -12 } });
    const double hz[] { 4000.0, 8000.0, 12000.0, 16000.0 };
    const double linDb[] { -10.6, -4.0, -1.2, -0.4 }, minDb[] { -10.9, -4.1, -0.7, -0.1 };
    for (int mode = 0; mode < 2; ++mode)
    {
        p[m::processingMode] = mode == 0 ? 0.0f : 1.0f;
        for (int k = 0; k < 4; ++k)
        {
            const auto want = mode == 0 ? linDb[k] : minDb[k];
            const auto tone = p147::toneGain (p, hz[k]), curve = p150::curveDb (p, hz[k]);
            std::printf ("  [172b7] Prism %s %.0f Hz: tone %.2f curve %.2f dB (Pro-MB %.1f)\n", mode == 0 ? "linear" : "minimum", hz[k], tone, curve, want);
            CHECK (std::abs (tone - want) < 0.15);
            CHECK (std::abs (curve - want) < 0.15);
        }
    }
}
