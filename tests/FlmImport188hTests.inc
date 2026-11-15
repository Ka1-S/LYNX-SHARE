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
    // round 188i (A/B against FLM in FL): a fractional hold (BitCrush 0.7: (0.7²)²·256 + 1 = 62.47 samples) falls
    // where FLM's own single-precision counter puts it, with the length squared twice as FLM computes it
    {
        std::vector<std::array<float, 2>> ramp;
        for (int i = 0; i < 4000; ++i)
            ramp.push_back ({ (float) i * 1e-5f, (float) i * 1e-5f });
        const auto o = flm188h::run (K, { 0.0f, 0.7f, 1.0f, 0.5f, 0.0f, 0.5f }, ramp);
        const float sq = 0.7f * 0.7f, crush = sq * sq * 256.0f + 1.0f;
        float hold = 0.0f;
        bool same = true;
        for (int i = 0; i < 4000; ++i)
        {
            const auto starts = ! (hold > 0.0f);
            if (starts) hold += crush;
            hold -= 1.0f;
            if (i > 0 && starts != (o[(std::size_t) i][0] != o[(std::size_t) i - 1][0])) same = false;
        }
        CHECK (same);
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

TEST (flm188h_pocket_limiter_leveller)
{
    using namespace lynx;
    // Limiter: In / Out 0 dB, threshold −12 dB, release 0.2 → 0.2⁴·1.9 + 0.1 s. The output is 2 ms (96 samples at 48 kHz)
    // late; a full-scale sample sets the floor g₀ = threshold at once, felt from the next sample (the delayed copy of
    // the peak meets the gain 95 samples into its quarter-sine recovery)
    {
        std::vector<std::array<float, 2>> in (20000, { 0.1f, 0.1f });
        in[0] = { 1.0f, 1.0f };
        auto fx = makeEffect (EffectKind::pocketLimiter);
        fx->prepare (48000.0);
        CHECK (fx->latencySamples() == 96);
        const auto o = flm188h::run (EffectKind::pocketLimiter, { 0.5f, 0.5f, 0.2f, 0.5f }, in);
        const auto thr = std::pow (10.0, -0.6), rel = 0.2 * 0.2 * 0.2 * 0.2 * 1.9 + 0.1, dec = 1.0 / (rel * 48000.0);
        const auto g = [&] (int n) { const auto s = std::sin ((1.0 - (n - 1) * dec) * 1.5707963267948966); return (1.0 - thr) * (1.0 - s) + thr; };
        CHECK (o[95][0] == 0.0f && flm188h::near (o[96][0], g (96), 1e-4) && flm188h::near (o[200][1], 0.1 * g (200), 1e-4));
        CHECK (flm188h::near (o.back()[0], 0.1, 1e-6)); // recovered, no make-up
        CHECK (pocket::limiterParamText (2, 0.2f) == "0.10 s");
    }
    // Leveller: Gain 1 = +12 dB, Pan 0 = left only (linear balance); the first block starts on its targets
    {
        const auto o = flm188h::run (EffectKind::pocketLeveller, { 1.0f, 0.0f }, { { 0.1f, 0.1f } });
        CHECK (flm188h::near (o[0][0], 0.1 * std::pow (10.0, 0.6), 1e-6) && o[0][1] == 0.0f);
        CHECK (pocket::levellerParamText (0, 0.7379573f) == "0 dB" && pocket::levellerParamText (1, 0.25f) == "50.0% L");
    }
}

TEST (flm188h_pocket_gate)
{
    using namespace lynx;
    // defaults: threshold −60 dB, attack 10 ms (A. curve −1), release 250 ms, hold 250 ms; 255 samples late. A −20 dB
    // tone: the gate opens at the first 256-sample block and is fully open well before 0.2 s; its floor is −60 dB
    std::vector<std::array<float, 2>> in (24000);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = { 0.1f * (float) std::sin (0.05 * (double) i), 0.0f };
    const auto o = flm188h::run (EffectKind::pocketGate, {}, in);
    CHECK (std::abs (o[200][0]) < 1e-3f);
    CHECK (flm188h::near (o[12000][0], 0.1 * std::sin (0.05 * (12000 - 255)), 1e-5));
    // threshold above the tone: stays at the floor (Mix 1 → 0.001)
    const auto c = flm188h::run (EffectKind::pocketGate, { 1.0f }, in);
    CHECK (flm188h::near (c[12000][0], 0.001 * 0.1 * std::sin (0.05 * (12000 - 255)), 1e-6));
    auto fx = makeEffect (EffectKind::pocketGate);
    CHECK (fx->latencySamples() == 255);
}

TEST (flm188h_pocket_delays)
{
    using namespace lynx;
    // Trance Delay at 120 BPM, 48 kHz: Time 0.775 = 3/4 beat = 18000 samples; the loop stores fb·(input + echo), so the
    // first echo is fb (0.4 · 0.99) × Mix² and the second fb²; the dry stays at unity
    {
        std::vector<std::array<float, 2>> in (40000, { 0.0f, 0.0f });
        in[0] = { 1.0f, 0.5f };
        const auto o = flm188h::run (EffectKind::pocketTranceDelay, {}, in);
        const auto fb = 0.4 * 0.99;
        CHECK (o[0][0] == 1.0f && std::abs (o[17999][0]) < 1e-6f && flm188h::near (o[18000][0], fb, 1e-6)
               && flm188h::near (o[36000][0], fb * fb, 1e-6) && flm188h::near (o[18000][1], 0.5 * fb, 1e-6));
        // Ping-Pong: the left input comes back on the right first
        const auto pp = flm188h::run (EffectKind::pocketTranceDelay, { 0.775f, 0.4f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f }, { in.begin(), in.begin() + 40000 });
        CHECK (std::abs (pp[18000][0] - 0.5 * fb) < 1e-6 && flm188h::near (pp[18000][1], fb, 1e-6));
        CHECK (pocket::tranceDelayParamText (0, 0.775f) == "3/4" && pocket::tranceDelayParamText (4, 0.5f) == "Ping-Pong");
    }
    // Tape Delay: Time 0.5 = 0.25 s → one loop = 0.25·(48000 − 2) + 2 samples (+1 by the read order); the echo of an
    // impulse is spread over the two reads around it (linear), × Mix × Feedback = 0.4 · 0.6
    {
        std::vector<std::array<float, 2>> in (30000, { 0.0f, 0.0f });
        in[0] = { 1.0f, 1.0f };
        const auto o = flm188h::run (EffectKind::pocketTapeDelay, { 0.5f, 0.6f, 0.5f, 0.4f }, in);
        std::size_t peak = 1;
        double sum = 0.0;
        for (std::size_t i = 1; i < 20000; ++i)
        {
            if (std::abs (o[i][0]) > std::abs (o[peak][0]))
                peak = i;
            sum += o[i][0];
        }
        CHECK (peak >= 12001 && peak <= 12004 && std::abs (sum - 0.24) < 0.005);
        CHECK (pocket::tapeDelayParamText (0, 0.6f) == "360 ms");
    }
}

TEST (flm188h_post_gain_and_new_modules)
{
    using namespace lynx;
    // Post Gain 0.75 = +10 dB (40g − 20) on the module's output: a utility after it (Mix 100 %)
    auto m = flm188f::moduleOf (28, { 1.0f, 0.5f, 0.5f, 0.2f, 0.8f });
    m.reserved = { 0.75f, 1.0f, 0.5f };
    const auto mapped = flmfx::map (m);
    CHECK (mapped.slots.size() == 2 && mapped.partial.empty());
    if (mapped.slots.size() == 2)
        CHECK (mapped.slots[0].kind == EffectKind::pocketLimiter && mapped.slots[0].params[3] == 0.8f
               && mapped.slots[1].kind == EffectKind::utility && std::abs ((mapped.slots[1].params[0] - 0.5f) * 48.0f - 10.0f) < 1e-4f);
    // with Mix below 100 % (round 190fe): FLM's blend itself, a parallel container — the dry signal at 1 − Mix, the module
    // at Mix × Post Gain (an automated module keeps its plain slot, the Post Gain listed)
    m.reserved = { 0.75f, 0.5f, 0.5f };
    const auto half = flmfx::map (m);
    CHECK (half.slots.size() == 1 && half.slots[0].kind == EffectKind::parallel && half.partial.empty());
    if (half.slots.size() == 1 && half.slots[0].branches.size() == 2)
        CHECK (half.slots[0].branches[0].chain.empty() && half.slots[0].branches[0].gain == 0.5f && half.slots[0].branches[1].chain.size() == 1
               && half.slots[0].branches[1].chain[0].kind == EffectKind::pocketLimiter && std::abs (half.slots[0].branches[1].gain - 0.5f * std::pow (10.0f, 0.5f)) < 1e-5f);
    const auto live = flmfx::map (m, true);
    CHECK (live.slots.size() == 1 && live.slots[0].kind == EffectKind::pocketLimiter && live.slots[0].mix == 0.5f && live.partial.size() == 1);
    // Trance Delay: FLM's saved dry-pan byte; a module without one loads with it on
    auto t = flm188f::moduleOf (16, { 1.0f, 0.775f, 0.845f, 1.0f, 0.71f, 0.5f, 0.703f });
    CHECK (flmfx::map (t).slots[0].params[6] == 1.0f && flmfx::map (t).slots[0].params[5] == 0.703f);
    t.custom = { 0 };
    CHECK (flmfx::map (t).slots[0].params[6] == 0.0f);
    CHECK (flmfx::map (flm188f::moduleOf (20, { 1.0f, 0.5f })).slots[0].kind == EffectKind::pocketLeveller
           && flmfx::map (flm188f::moduleOf (27, { 1.0f })).slots[0].kind == EffectKind::pocketGate
           && flmfx::map (flm188f::moduleOf (18, { 1.0f })).slots[0].kind == EffectKind::pocketTapeDelay);
}

TEST (flm188h_pocket_multifx)
{
    using namespace lynx;
    const auto K = EffectKind::pocketMultiFx;
    // Delay (kind 0), LFO Rate 1 beat → the delay runs at twice it: 2 beats at 120 BPM = 1 s, × ⅓ → (72000 − 1)·⅓ =
    // 23999 samples; Y 0.5 = an open loop filter (one sample later); X 0.7 → feedback and echo level 0.49
    {
        std::vector<std::array<float, 2>> in (50000, { 0.0f, 0.0f });
        in[0] = { 1.0f, 1.0f };
        const auto o = flm188h::run (K, { 0.0f, 0.7f, 0.5f, 0.61f, 0.0f, 0.0f }, in);
        CHECK (o[0][0] == 1.0f && std::abs (o[23999][0]) < 1e-7f && flm188h::near (o[24000][0], 0.49, 1e-5) && flm188h::near (o[24000][1], 0.49, 1e-5)
               && flm188h::near (o[48000][0], 0.49 * 0.49, 1e-5));
    }
    // LP Filter (kind 2): four equal biquads; at DC each passes (1 + s/Q)/(1 + s/2Q) (FLM's gain normalisation)
    {
        std::vector<std::array<float, 2>> in (40000, { 0.5f, 0.5f });
        const auto o = flm188h::run (K, { 2.0f / 7.0f + 1e-4f, 0.5f, 0.5f, 0.61f, 0.0f, 0.0f }, in);
        const auto hz = std::pow (2.0, 8.28 * 0.5 + 6.0), q = 1.5, s = std::sin (6.283185307179586 * hz / 48000.0);
        const auto dc = std::pow ((1.0 + s / q) / (1.0 + s / (2.0 * q)), 4.0);
        CHECK (flm188h::near (o.back()[0], 0.5 * dc, 1e-3));
    }
    // Panner (kind 6) at the centre: a one-sample delay
    {
        std::vector<std::array<float, 2>> in (64, { 0.0f, 0.0f });
        in[0] = { 1.0f, -1.0f };
        const auto o = flm188h::run (K, { 6.0f / 7.0f + 1e-4f, 0.5f, 0.5f, 0.61f, 0.0f, 0.0f }, in);
        CHECK (std::abs (o[0][0]) < 1e-7f && flm188h::near (o[1][0], 1.0, 1e-6) && flm188h::near (o[1][1], -1.0, 1e-6));
    }
    // Reverb (kind 1): the dry passes, the plate adds a tail
    {
        std::vector<std::array<float, 2>> in (24000, { 0.0f, 0.0f });
        in[0] = { 1.0f, 1.0f };
        const auto o = flm188h::run (K, { 1.0f / 7.0f + 1e-4f, 1.0f, 0.5f, 0.61f, 0.0f, 0.0f }, in);
        double tail = 0.0;
        for (std::size_t i = 4800; i < o.size(); ++i)
            tail += std::abs (o[i][0]);
        CHECK (flm188h::near (o[0][0], 1.0, 0.05) && tail > 1.0);
    }
    // the other kinds run and stay finite
    for (int k = 0; k < 8; ++k)
    {
        std::vector<std::array<float, 2>> in (9600);
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = { 0.3f * (float) std::sin (0.03 * (double) i), 0.3f * (float) std::cos (0.017 * (double) i) };
        const auto o = flm188h::run (K, { (float) k / 7.0f + 1e-4f, 0.8f, 0.7f, 0.3f, 0.5f, 0.0f }, in);
        bool finite = true;
        for (const auto& f : o)
            finite = finite && std::isfinite (f[0]) && std::isfinite (f[1]) && std::abs (f[0]) < 10.0f;
        CHECK (finite);
    }
    CHECK (pocket::multiFxParamText (0, 1.0f / 7.0f + 1e-4f) == "Reverb" && pocket::multiFxParamText (3, 0.61f) == "beat"
           && std::string (pocket::multiFxAxisName (7, true)) == "Size");
    // the import copies FLM's values
    const auto mapped = flmfx::map (flm188f::moduleOf (4, { 1.0f, 0.1429f, 0.3f, 0.6f, 0.61f, 0.25f, 0.5f }));
    CHECK (mapped.slots.size() == 1 && mapped.slots[0].kind == K && mapped.slots[0].params[0] == 0.1429f && mapped.slots[0].params[5] == 0.5f);
}
