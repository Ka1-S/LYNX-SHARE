// Round 173d (issue #77): Crest Compressor's upward lift is bounded (core/DynamicsSuite.cpp, Compressor::detect).
// Uses dyn108::run / defaults (DynamicsTests108.inc, included before this file).

// Round 173d (issue #77): Crest's Upward / Two-way lifted the gaps between drum hits by up to Range (60 dB) and the
// next hit came through at full lift (+45 / +13 dBFS peaks on −18 dBFS drums). No style may take a signal's peak more
// than 3 dB over max(input peak, threshold) (the auto gain off, the output trim at 0 dB, so no make-up): a drum
// loop, silence then a burst, low noise then a burst, at the default threshold and at 0 dBFS, Range at its default
// and at 60 dB
TEST (crest173d_styles_never_boost_peaks)
{
    using namespace dyn108;
    namespace c = lynx::dyn::crest;
    std::vector<std::vector<float>> signals;
    {
        // 2 s of a drum loop at −18 dBFS peak: 16ths of a kick (80 Hz) with a hat (noise) between, gaps in between
        std::vector<float> x ((std::size_t) (2.0 * sr), 0.0f);
        std::uint32_t seed = 17;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const auto t = (double) (i % (std::size_t) (sr / 8.0)) / sr, h = (double) ((i + (std::size_t) (sr / 16.0)) % (std::size_t) (sr / 8.0)) / sr;
            const auto noise = (double) ((seed = seed * 1664525u + 1013904223u) >> 8) / 8388608.0 - 1.0;
            x[i] = (float) (0.1 * std::exp (-t * 30.0) * std::sin (2.0 * pi * 80.0 * t) + 0.03 * std::exp (-h * 200.0) * noise);
        }
        signals.push_back (x);
    }
    {
        // 1 s of silence, then 16ths of a decaying 80 Hz kick at −12 dBFS
        std::vector<float> x ((std::size_t) (2.0 * sr), 0.0f);
        for (std::size_t i = (std::size_t) sr; i < x.size(); ++i)
        {
            const auto t = (double) ((i - (std::size_t) sr) % (std::size_t) (sr / 8.0)) / sr;
            x[i] = (float) (0.25 * std::exp (-t * 30.0) * std::sin (2.0 * pi * 80.0 * t));
        }
        signals.push_back (x);
        // the same over −70 dBFS noise
        std::uint32_t seed = 173;
        for (auto& v : x)
            v += (float) (0.000316 * ((double) ((seed = seed * 1664525u + 1013904223u) >> 8) / 8388608.0 - 1.0));
        signals.push_back (x);
    }
    double worst = -300.0;
    for (int s = 0; s < c::styles; ++s)
        for (const float thr : { -1.0f, 1.0f })
            for (const float rng : { -1.0f, 1.0f })
                for (const auto& x : signals)
                {
                    auto p = defaults (EffectKind::crestCompressor);
                    p[c::style] = c::styleValue ((c::Style) s);
                    p[c::autoGain] = 0.0f;
                    if (thr > 0.0f) p[c::threshold] = 1.0f;
                    if (rng > 0.0f) p[c::range] = 1.0f;
                    const auto y = run (EffectKind::crestCompressor, p, x);
                    float inPeak = 0.0f, outPeak = 0.0f;
                    bool finite = true;
                    for (std::size_t i = 0; i < x.size(); ++i)
                    {
                        inPeak = std::max (inPeak, std::abs (x[i]));
                        outPeak = std::max (outPeak, std::abs (y[i]));
                        finite = finite && std::isfinite (y[i]);
                    }
                    CHECK (finite);
                    const auto bound = std::max (20.0 * std::log10 ((double) inPeak), c::thresholdDb (p[c::threshold]));
                    const auto over = 20.0 * std::log10 ((double) outPeak + 1e-12) - bound;
                    worst = std::max (worst, over);
                    if (over > 3.0)
                    {
                        std::printf ("  Crest style %d (threshold %.1f dB, range %.0f dB): peak %.1f dB over max(input peak, threshold)\n", s,
                                     c::thresholdDb (p[c::threshold]), c::rangeDb (p[c::range]), over);
                        CHECK (false);
                    }
                }
    std::printf ("  Crest styles: the worst peak %.2f dB over max(input peak, threshold)\n", worst);
}
