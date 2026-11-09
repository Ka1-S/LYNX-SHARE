// --- FL Studio Mobile (.flm) import, round 188h: FLM's own effects as LYNX's Pocket engines (core/PocketFx.h). The
// expected values are worked by hand from FLM's laws in REA_rodada182c.md "F5 effects, round 188h".

#include "core/PocketFx.h"

namespace flm188h
{
// runs `n` frames of a constant stereo input through a fresh engine; returns the outputs
inline std::vector<std::array<float, 2>> run (lynx::EffectKind kind, std::initializer_list<float> values, std::vector<std::array<float, 2>> in,
                                              double rate = 48000.0)
{
    using namespace lynx;
    auto fx = makeEffect (kind);
    fx->prepare (rate);
    auto p = makeEffectSlot (kind).params;
    std::size_t k = 0;
    for (const auto v : values)
        p[k++] = v;
    std::vector<float> l, r;
    for (const auto& x : in)
    {
        l.push_back (x[0]);
        r.push_back (x[1]);
    }
    fx->process (l.data(), r.data(), (int) l.size(), p, 120.0);
    std::vector<std::array<float, 2>> out;
    for (std::size_t i = 0; i < l.size(); ++i)
        out.push_back ({ l[i], r[i] });
    return out;
}
inline bool near (float a, double b, double tol = 1e-5) { return std::abs ((double) a - b) <= tol; }
} // namespace flm188h

TEST (flm188h_pocket_distortion)
{
    using namespace lynx;
    const auto K = EffectKind::pocketDistortion;
    // defaults: Drive 0.25, soft curve: g = 10^1.25, k = g − 1, y = g·x/(k|x| + 1)
    {
        const auto g = std::pow (10.0, 1.25), k = g - 1.0;
        const auto o = flm188h::run (K, {}, { { 0.5f, -0.25f } });
        CHECK (flm188h::near (o[0][0], g * 0.5 / (k * 0.5 + 1.0), 1e-5) && flm188h::near (o[0][1], -g * 0.25 / (k * 0.25 + 1.0), 1e-5));
    }
    // Stereo 0 = the drive on the left only (×min(1, 2s) = 0 → straight), the right at full drive
    {
        const auto o = flm188h::run (K, { 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f }, { { 0.3f, 0.3f } });
        const auto g = std::pow (10.0, 5.0);
        CHECK (flm188h::near (o[0][0], 0.3, 1e-6) && flm188h::near (o[0][1], g * 0.3 / ((g - 1.0) * 0.3 + 1.0), 1e-4));
    }
    // tanh curve normalised by tanh(k): k = 10^(4·0.5) − 0.99
    {
        const auto k = 100.0 - 0.99;
        const auto o = flm188h::run (K, { 0.5f, 0.0f, 1.0f, 0.5f, 0.34f, 0.5f }, { { 0.004f, 0.004f } });
        CHECK (flm188h::near (o[0][0], std::tanh (0.004 * k) / std::tanh (k), 1e-5));
    }
    // exponential curve: 1 − 10^(−kx)(1 − x), k = 20·d²; hard clip at ±1 after ×10^(3d²)
    {
        const auto o = flm188h::run (K, { 0.5f, 0.0f, 1.0f, 0.5f, 0.67f, 0.5f }, { { 0.2f, -0.2f } });
        CHECK (flm188h::near (o[0][0], 1.0 - std::pow (10.0, -0.2 * 5.0) * 0.8, 1e-5) && flm188h::near (o[0][1], std::pow (10.0, -0.2 * 5.0) * 0.8 - 1.0, 1e-5));
        const auto h = flm188h::run (K, { 0.5f, 0.0f, 1.0f, 0.5f, 1.0f, 0.5f }, { { 0.1f, 0.5f } });
        CHECK (flm188h::near (h[0][0], 0.1 * std::pow (10.0, 0.75), 1e-5) && flm188h::near (h[0][1], 1.0, 1e-6));
    }
    // Gain: 24·v − 24 dB; BitCrush 0.5 holds each value for 0.5⁴·256 + 1 = 17 samples
    {
        std::vector<std::array<float, 2>> ramp;
        for (int i = 0; i < 40; ++i)
            ramp.push_back ({ i * 0.001f, i * 0.001f });
        const auto o = flm188h::run (K, { 0.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f }, ramp);
        CHECK (flm188h::near (o[16][0], 0.0, 1e-9) && flm188h::near (o[17][0], 0.017 * std::pow (10.0, -12.0 / 20.0), 1e-6)
               && flm188h::near (o[33][0], o[17][0], 1e-9) && flm188h::near (o[34][0], 0.034 * std::pow (10.0, -12.0 / 20.0), 1e-6));
    }
    // Bias (2(b − 0.5))⁵ is added before the curve, then a one-pole high-pass at 220.5/fs takes the offset out
    {
        std::vector<std::array<float, 2>> silence (48000, { 0.0f, 0.0f });
        const auto o = flm188h::run (K, { 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 1.0f }, silence);
        CHECK (flm188h::near (o[0][0], 1.0 - 220.5 / 48000.0, 1e-6) && std::abs (o.back()[0]) < 1e-5f); // single precision stalls the one-pole a few µ short, as FLM's
    }
    CHECK (pocket::distortionParamText (2, 0.5f) == "-12.0 dB" && pocket::distortionParamText (4, 0.34f) == "Tanh");
}

TEST (flm188h_pocket_limiter_classic)
{
    using namespace lynx;
    // threshold −12 dB (v 0.5), release 0.7 (rate 100·0.06² + 1 = 1.36 per second), output 0 dB: a full-scale sample
    // comes out at the output level (gain = threshold, × output/threshold); the gain then recovers along
    // 1 − cos²(πt/2)(1 − g₀): half way (t = 0.5, 0.3676 s) it is 1 − 0.5(1 − g₀)
    const auto thr = std::pow (10.0, -0.6);
    std::vector<std::array<float, 2>> in (48000, { 0.1f, 0.05f });
    in[0] = { 1.0f, -0.5f };
    const auto o = flm188h::run (EffectKind::pocketLimiterClassic, { 0.5f, 0.7f, 2.0f / 3.0f }, in);
    CHECK (flm188h::near (o[0][0], 1.0, 1e-5) && flm188h::near (o[0][1], -0.5, 1e-5));
    const auto half = (int) std::lround (0.5 / (1.36 / 48000.0));
    CHECK (flm188h::near (o[(std::size_t) half][0], 0.1 * (1.0 - 0.5 * (1.0 - thr)) / thr, 2e-4));
    // fully recovered after 1/1.36 s: the input × the make-up (+12 dB)
    CHECK (flm188h::near (o.back()[0], 0.1 / thr, 1e-4) && flm188h::near (o.back()[1], 0.05 / thr, 1e-4));
    CHECK (pocket::limiterClassicParamText (2, 0.0f) == "-24.0 dB" && pocket::limiterClassicParamText (0, 0.5f) == "-12.0 dB");
}

TEST (flm188h_maps_pocket_modules)
{
    using namespace lynx;
    // Distortion and Limiter (legacy) copy FLM's values (parameter k + 1 → slot parameter k), Enable → the slot's switch
    const auto d = flmfx::map (flm188f::moduleOf (6, { 0.0f, 0.7f, 0.1f, 0.9f, 0.4f, 0.67f, 0.3f }));
    CHECK (d.slots.size() == 1 && d.label == "FL Studio Mobile: Distortion");
    if (d.slots.size() == 1)
        CHECK (d.slots[0].kind == EffectKind::pocketDistortion && ! d.slots[0].enabled && d.slots[0].params[0] == 0.7f
               && d.slots[0].params[4] == 0.67f && d.slots[0].params[5] == 0.3f);
    // a module saved with fewer values keeps FLM's defaults for the rest
    const auto l = flmfx::map (flm188f::moduleOf (5, { 1.0f, 0.4f }));
    CHECK (l.slots.size() == 1);
    if (l.slots.size() == 1)
        CHECK (l.slots[0].kind == EffectKind::pocketLimiterClassic && l.slots[0].enabled && l.slots[0].params[0] == 0.4f
               && l.slots[0].params[1] == 0.7f && std::abs (l.slots[0].params[2] - 2.0f / 3.0f) < 1e-7f);
    // a BitCrush lane moves the slot's parameter 1 with FLM's own values
    const auto t = flmfx::automate (flm188f::moduleOf (6, { 1.0f, 0.25f, 0.0f }), 2, { 0.0f, 0.5f, 1.0f });
    CHECK (t.size() == 1 && t[0].kind == AutomationTargetKind::effectParam && t[0].param == 1 && t[0].values[1] == 0.5f);
}
