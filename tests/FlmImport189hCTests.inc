// --- FL Studio Mobile (.flm), round 189h: Pocket Waveshaper, Pocket Compressor and Pocket Spreader (core/PocketFxC.cpp).
// The expected values are worked by hand from FLM's laws in REA_rodada182c.md "F5 effects, round 189h".

namespace flm189hc
{
// `n` frames of the stereo signal f(i) through a fresh engine at 48 kHz, one call
inline std::vector<std::array<float, 2>> run (lynx::EffectKind kind, std::initializer_list<std::pair<int, float>> values, int n,
                                              std::array<float, 2> (*f) (int))
{
    using namespace lynx;
    auto fx = makeEffect (kind);
    fx->prepare (48000.0);
    auto p = makeEffectSlot (kind).params;
    for (const auto& [k, v] : values)
        p[(std::size_t) k] = v;
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto x = f (i);
        l[(std::size_t) i] = x[0];
        r[(std::size_t) i] = x[1];
    }
    fx->process (l.data(), r.data(), n, p, 120.0);
    std::vector<std::array<float, 2>> out;
    for (int i = 0; i < n; ++i)
        out.push_back ({ l[(std::size_t) i], r[(std::size_t) i] });
    return out;
}
inline bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }
} // namespace flm189hc

TEST (flm189h_pocket_waveshaper_curves)
{
    using lynx::pocket::waveshaperCurve;
    // (FLM's float glides stall short of their targets: Y = 5.5 settles about 2e−4 below, hence the tolerances)
    // Soft (k = Y = 9·0.5 + 1, b = X = 0 → 1e−4): ≈ kx/√((kx)² + 1)
    CHECK (flm189hc::near (waveshaperCurve (0, 0.2f, 0.5f, 0.5f), 0.7399769, 2e-5));
    // Tanh: (tanh(Yx − X) + tanh X)/(tanh|X| + 1), X = 2·0.5 − 1 = 0, Y = 5.5; and X = 1, Y = 1
    CHECK (flm189hc::near (waveshaperCurve (1, 0.2f, 0.5f, 0.5f), std::tanh (1.1), 1e-4));
    CHECK (flm189hc::near (waveshaperCurve (1, 0.5f, 1.0f, 0.0f), 0.1700034, 2e-5));
    // Crossover at X = Y = 0.5: points k1 (0.19, 0.1) and k4 (0.75, 0.75) straight; k6 (0, 0) to k2 (0.1, 0.02) bent
    // by k2's curve 0.3: w = t^0.7
    CHECK (flm189hc::near (waveshaperCurve (2, 0.5f, 0.5f, 0.5f), 0.4598214, 2e-5));
    CHECK (flm189hc::near (waveshaperCurve (2, 0.05f, 0.5f, 0.5f), 0.0123114, 2e-5));
    // Fold: sin((x + X)·Y), Y = 7v + 1, X = (2 − 4v)/Y
    CHECK (flm189hc::near (waveshaperCurve (3, 0.3f, 0.5f, 0.5f), std::sin (1.35), 2e-5));
    CHECK (flm189hc::near (waveshaperCurve (3, 0.1f, 0.0f, 0.0f), std::sin (2.1), 1e-4));
    // Octaver: X = 0.55, Y = 0.4: t = frac(((x + 1)/2)/Y) = 0.5 < X → 2t/X − 1
    CHECK (flm189hc::near (waveshaperCurve (4, 0.2f, 0.5f, 0.5f), 2.0 * 0.5 / 0.55 - 1.0, 5e-4));
    // Rectify at X = Y = 0.5 is |x| through (−0.999, 1), (0, 0), (0.999, 1); Y = 1 bends the right side (curve −0.8)
    CHECK (flm189hc::near (waveshaperCurve (5, 0.5f, 0.5f, 0.5f), 0.5 / 0.999, 2e-5));
    CHECK (flm189hc::near (waveshaperCurve (5, -0.5f, 0.5f, 0.5f), 0.5 / 0.999, 2e-5));
    CHECK (flm189hc::near (waveshaperCurve (5, 0.5f, 0.5f, 1.0f), 0.1296238, 2e-5));
    // CrossFold: exp(−e·x²)·sin(2π·Y·x) + x, e = exp(20^(1 − X)), X = 0.8, Y = −0.03
    CHECK (flm189hc::near (waveshaperCurve (6, 0.5f, 0.5f, 0.5f), 0.4799022, 2e-5));
}

TEST (flm189h_pocket_waveshaper_engine)
{
    using namespace lynx;
    const auto K = EffectKind::pocketWaveshaper;
    const auto tone = [] (int i) -> std::array<float, 2> {
        const auto v = 0.5f * (float) std::sin (6.283185307179586 * 440.0 * i / 48000.0);
        return { v, -v };
    };
    // Mix 0: the wet weight √0 and the dry √1 from the first sample (the Mix glide starts at 0)
    {
        const auto o = flm189hc::run (K, { { 7, 0.0f } }, 4800, tone);
        bool same = true;
        for (int i = 0; i < 4800; ++i)
            same = same && o[(std::size_t) i][0] == tone (i)[0] && o[(std::size_t) i][1] == tone (i)[1];
        CHECK (same);
    }
    // every control at its ends: finite output
    for (const auto v : { 0.0f, 1.0f })
    {
        std::initializer_list<std::pair<int, float>> all { { 0, v }, { 1, v }, { 2, v }, { 3, v }, { 4, v }, { 5, v }, { 6, v },
                                                           { 7, v }, { 8, v }, { 9, v }, { 10, v }, { 11, v }, { 12, v }, { 13, v },
                                                           { 14, v }, { 15, v }, { 16, v }, { 17, v }, { 18, v }, { 19, v } };
        const auto o = flm189hc::run (K, all, 9600, tone);
        bool finite = true;
        for (const auto& x : o)
            finite = finite && std::isfinite (x[0]) && std::isfinite (x[1]) && std::abs (x[0]) < 100.0f;
        CHECK (finite);
    }
    // the texts: FLM's dB / Hz laws
    CHECK (formatEffectParam (K, 5, 0.75f, 120.0) == "9.0 dB" && formatEffectParam (K, 8, 1.0f, 120.0) == "20.00 kHz"
           && formatEffectParam (K, 9, 0.0f, 120.0) == "20.0 Hz" && formatEffectParam (K, 11, 0.5f, 120.0) == "1.00 kHz"
           && formatEffectParam (K, 0, 2.0f / 6.0f, 120.0) == "Crossover" && formatEffectParam (K, 17, 1.0f, 120.0) == "9.92 Hz");
}

TEST (flm189h_pocket_compressor)
{
    using namespace lynx;
    const auto K = EffectKind::pocketCompressor;
    const auto half = [] (int) -> std::array<float, 2> { return { 0.5f, -0.25f }; };
    // defaults: Ratio 1:1 (slope 1, make-up 1); Level 0.5001 with Auto gain → 10^(2·0.5001 − 1)
    {
        const auto o = flm189hc::run (K, {}, 64, half);
        CHECK (flm189hc::near (o[63][0], 0.5 * std::pow (10.0, 0.0002), 1e-6) && flm189hc::near (o[63][1], -0.25 * std::pow (10.0, 0.0002), 1e-6));
    }
    // Ratio ∞, Thresh −20 dB, Attack 0 (the envelope is the peak at once): g = T/env, × make-up 1/T → 1.0. A steady
    // level only re-arms the peak every second sample: an equal sample is not above the peak, so it decays by `rel`
    {
        const auto o = flm189hc::run (K, { { 0, 0.5f }, { 1, 1.0f }, { 2, 0.0f }, { 4, 0.5f } }, 16, half);
        const auto rel = (double) (float) std::pow (0.1, 1.0 / ((double) (0.49f * 0.36f + 0.01f) * 48000.0));
        CHECK (flm189hc::near (o[0][0], 1.0, 1e-5) && flm189hc::near (o[14][1], -0.5, 1e-5) && flm189hc::near (o[15][0], 1.0 / rel, 1e-5));
    }
    // Ratio 4:1 at −20 dB: the line through (T, T) and (1, 10^(−15/20)); Auto gain off, Level 0 (0 dB)
    {
        const auto t = 0.1, t2 = std::pow (10.0, -0.75), slope = (t2 - t) / (1.0 - t);
        const auto o = flm189hc::run (K, { { 0, 0.5f }, { 1, std::sqrt (3.0f / 19.0f) }, { 2, 0.0f }, { 4, 0.0f }, { 6, 0.0f } }, 16, half);
        CHECK (flm189hc::near (o[8][0], 0.4 * slope + 0.1, 1e-5));
    }
    // Input gain +20 dB reaches the detector only: 0.05 is detected as 0.5 → gain 0.2
    {
        const auto o = flm189hc::run (K, { { 0, 0.5f }, { 1, 1.0f }, { 2, 0.0f }, { 4, 0.0f }, { 6, 0.0f }, { 7, 1.0f } }, 16,
                                      [] (int) -> std::array<float, 2> { return { 0.05f, 0.05f }; });
        CHECK (flm189hc::near (o[4][0], 0.01, 1e-6));
    }
    // Hold 50 ms (2400 samples at 48 kHz) keeps the 0.5 peak, then the release multiplies it by 0.1^(1/(t·fs)) per sample
    {
        const auto o = flm189hc::run (K, { { 0, 0.5f }, { 1, 1.0f }, { 2, 0.0f }, { 4, 0.0f }, { 5, 1.0f }, { 6, 0.0f } }, 2500,
                                      [] (int i) -> std::array<float, 2> { return { i < 10 ? 0.5f : 0.2f, 0.0f }; });
        const auto rel = (double) (float) std::pow (0.1, 1.0 / ((double) (0.49f * 0.36f + 0.01f) * 48000.0));
        CHECK (flm189hc::near (o[2000][0], 0.04, 1e-6) && flm189hc::near (o[2400][0], 0.04, 1e-6)
               && flm189hc::near (o[2401][0], 0.2 * 0.1 / (0.5 * rel), 1e-6));
    }
    CHECK (formatEffectParam (K, 0, 0.499f, 120.0) == "-20.0 dB" && formatEffectParam (K, 1, 1.0f, 120.0) == "+inf"
           && formatEffectParam (K, 1, 0.0f, 120.0) == "1.0:1" && formatEffectParam (K, 2, 0.6f, 120.0) == "18.0 ms"
           && formatEffectParam (K, 3, 0.6f, 120.0) == "186.4 ms" && formatEffectParam (K, 7, 0.5f, 120.0) == "0.0 dB");
    // Level: 0…+40 dB, or −20…+20 dB with Auto gain on
    auto ctx = makeEffectSlot (K).params;
    CHECK (formatEffectParam (K, 4, 0.75f, 120.0, ctx) == "10.0 dB");
    ctx[6] = 0.0f;
    CHECK (formatEffectParam (K, 4, 0.75f, 120.0, ctx) == "30.0 dB");
}

TEST (flm189h_pocket_spreader)
{
    using namespace lynx;
    const auto tone = [] (int i) -> std::array<float, 2> {
        return { 0.5f * (float) std::sin (6.283185307179586 * 300.0 * i / 48000.0), 0.3f * (float) std::sin (6.283185307179586 * 2100.0 * i / 48000.0) };
    };
    // FL's Spreader engine: FLM's Width 0.25 (zone 0.5), Low Bypass 1/3 (10·1000^v = 100 Hz), Separation 0.5 equals FL's
    // Spreader at width 50 %, low bypass 100 Hz (unit 0.1), separation 0
    const auto pocketOut = flm189hc::run (EffectKind::pocketSpreader, { { 0, 0.25f }, { 1, 1.0f / 3.0f }, { 2, 0.0f }, { 3, 0.5f } }, 9600, tone);
    const auto flOut = flm189hc::run (EffectKind::spreader, { { 0, 0.5f }, { 1, 0.5f }, { 2, 0.1f }, { 3, 0.0f }, { 4, 1.0f } }, 9600, tone);
    double err = 0.0;
    for (std::size_t i = 0; i < pocketOut.size(); ++i)
        err = std::max ({ err, std::abs ((double) pocketOut[i][0] - flOut[i][0]), std::abs ((double) pocketOut[i][1] - flOut[i][1]) });
    CHECK (err < 1e-4);
    // Width past 0.5: the delay stays at its maximum and only the delayed level rises (different from Width 0.5)
    const auto wide = flm189hc::run (EffectKind::pocketSpreader, { { 0, 1.0f } }, 9600, tone);
    const auto mid = flm189hc::run (EffectKind::pocketSpreader, { { 0, 0.5f } }, 9600, tone);
    CHECK (std::abs (wide[9000][0] - mid[9000][0]) > 1e-3);
    const auto K = EffectKind::pocketSpreader;
    // FLM's reported latency: the engine's longest delay, (int)(fs · 0.0063492064) = 304 at 48 kHz
    {
        auto fx = makeEffect (K);
        fx->prepare (48000.0);
        CHECK (fx->latencySamples() == 304);
    }
    CHECK (formatEffectParam (K, 0, 1.0f, 120.0) == "2.0" && formatEffectParam (K, 1, 0.0f, 120.0) == "10 Hz" && formatEffectParam (K, 1, 1.0f, 120.0) == "10 kHz"
           && formatEffectParam (K, 1, 0.5f, 120.0) == "316 Hz" && formatEffectParam (K, 3, 0.5f, 120.0) == "Original"
           && formatEffectParam (K, 3, 1.0f, 120.0) == "100 %" && formatEffectParam (K, 3, 0.52f, 120.0) == "4.0 %");
}

TEST (flm189h_pocket_c_import)
{
    using namespace lynx;
    const auto w = flmfx::map (flm188f::moduleOf (41, { 1.0f, 0.5f, 0.0f, 0.3f, 0.7f, 0.4f }));
    CHECK (w.slots.size() == 1 && w.slots[0].kind == EffectKind::pocketWaveshaper && w.slots[0].params[0] == 0.5f
           && w.slots[0].params[4] == 0.4f && w.slots[0].params[7] == 1.0f);
    const auto c = flmfx::map (flm188f::moduleOf (8, { 1.0f, 0.3f, 0.6f }));
    CHECK (c.slots.size() == 1 && c.slots[0].kind == EffectKind::pocketCompressor && c.slots[0].params[0] == 0.3f
           && c.slots[0].params[1] == 0.6f && c.slots[0].params[6] == 1.0f);
    const auto s = flmfx::map (flm188f::moduleOf (44, { 1.0f, 0.8f, 0.0f, 1.0f, 0.3f }));
    CHECK (s.slots.size() == 1 && s.slots[0].kind == EffectKind::pocketSpreader && s.slots[0].params[0] == 0.8f && s.slots[0].params[3] == 0.3f);
}
