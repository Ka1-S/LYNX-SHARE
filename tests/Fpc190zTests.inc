// Round 190z: FPC plays what FL's FPC plays (issue #135) — pad volume / pan envelopes (core/FpcEnvelope), layer pan,
// "Scale volume with velocity", the layer modes (all layers / Random / Random (avoid previous) / Cycle backward /
// Cycle), FPC's own choke, reversed layers (docs/analise/REA_rodada190z.md)

namespace fpc190z
{
inline lynx::fpc::Envelope envelope (int index, std::vector<lynx::fpc::EnvelopePoint> points, int decay = -1, int loop = -1, int sus = -1,
                                     std::array<int, 4> knobs = { 128, 128, 0, 128 }, bool on = true)
{
    lynx::fpc::Envelope e;
    e.index = index;
    e.points = std::move (points);
    e.tail = { 0, on ? -1 : 0, decay, loop, sus, knobs[0], knobs[1], knobs[2], knobs[3] };
    return e;
}

// FL's render of round 190z's project z1_env (/mnt/e/lynx-work/fpc190z/flp/z1_env.flp → fl/out/z1_env/z1_env.wav,
// FL Studio 2026 /R, 16-bit): a 440 Hz sine layer per pad, the gain at the start of FPC's tick k (48 samples at
// 44.1 kHz) divided by the pad's level. `released`: the first tick boundary after the note's end.
struct Case
{
    lynx::fpc::Envelope env;
    int released;
    std::vector<std::pair<int, float>> fl;
};

inline std::vector<Case> flCases()
{
    using P = lynx::fpc::EnvelopePoint;
    std::vector<Case> c;
    // the kits' decay: 1 → 0 in 1.25 units (0.625 s), tension 0.2
    c.push_back ({ envelope (0, { P { 0, 1, 0, 0 }, P { 1.25, 0, 0.2f, 0 } }), 6,
                   { { 0, 1.0f }, { 1, 0.99999f }, { 50, 0.77578f }, { 200, 0.34232f }, { 400, 0.08911f }, { 560, 0.00513f }, { 574, 0.00043f }, { 575, 0.0f } } });
    // FL's default volume envelope (decay point 1, sustain point 2), held 3 s
    const std::vector<P> adsr { P { 0, 0, 0, 0 }, P { 0.75, 1, 0.2f, 0 }, P { 1.5, 0.5, 0.2f, 0 }, P { 0.25, 0, 0.2f, 0 } };
    c.push_back ({ envelope (0, adsr, 1, -1, 2), 2757,
                   { { 0, 0.0f }, { 100, 0.58487f }, { 345, 0.99975f }, { 500, 0.75360f }, { 1500, 0.49999f }, { 2757, 0.5f }, { 2800, 0.16062f }, { 2870, 0.00234f }, { 2872, 0.0f } } });
    // the same with ATT 64, DEC 192, SUS 64, REL 256, held 2.5 s
    c.push_back ({ envelope (0, adsr, 1, -1, 2, { 64, 192, 64, 256 }), 2297,
                   { { 0, 0.0f }, { 100, 0.84922f }, { 200, 0.99997f }, { 900, 0.99995f }, { 2297, 1.0f }, { 2400, 0.24554f }, { 2520, 0.00648f } } });
    // tensions and shapes: single curve −0.6, double curve 0.7, mode 7 0.5, straight
    c.push_back ({ envelope (0, { P { 0, 0, 0, 0 }, P { 0.5, 1, -0.6f, 0 }, P { 1.0, 0.3, 0.7f, 1 }, P { 0.6, 0.6, 0.5f, 7 }, P { 0.5, 0, 0, 0 } }), 6,
                   { { 0, 0.0f }, { 100, 0.00873f }, { 230, 0.97546f }, { 400, 0.65023f }, { 700, 0.33510f }, { 900, 0.56419f }, { 1100, 0.24916f } } });
    // a sustain point without a decay point, SUS 64: the lift from the start
    c.push_back ({ envelope (0, { P { 0, 0, 0, 0 }, P { 0.2, 1, 0, 0 }, P { 0.6, 0.4, 0, 0 }, P { 0.4, 0, 0, 0 } }, -1, -1, 2, { 128, 128, 64, 128 }), 1838,
                   { { 0, 0.0f }, { 50, 0.53333f }, { 92, 0.99052f }, { 200, 0.96114f }, { 1000, 0.89997f }, { 1838, 0.90001f }, { 1900, 0.60125f } } });
    // a loop (points 1…3) released from its middle
    c.push_back ({ envelope (0, { P { 0, 0, 0, 0 }, P { 0.1, 1, 0, 0 }, P { 0.3, 0.2, 0, 0 }, P { 0.3, 1, 0, 0 }, P { 0.4, 0, 0, 0 } }, -1, 1, 3), 2020,
                   { { 0, 0.0f }, { 50, 0.98219f }, { 100, 0.69197f }, { 300, 0.86903f }, { 1000, 0.26749f }, { 2019, 0.75223f }, { 2022, 0.73655f }, { 2100, 0.42217f },
                     { 2203, 0.00705f }, { 2204, 0.0f } } });
    return c;
}

// a constant sample (FPC's gains read straight off the output)
inline std::shared_ptr<const lynx::SampleData> dc (std::size_t frames, float value = 0.5f)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    d->left.assign (frames, value);
    return d;
}

inline lynx::fpc::Layer layer (const std::string& path, int low = 0, int high = 127)
{
    lynx::fpc::Layer l;
    l.path = lynx::fpc::fromUtf8 (path);
    l.velocityLow = low;
    l.velocityHigh = high;
    return l;
}

// an FPC channel with `kit`, its instrument made with `samples`, prepared at 44.1 kHz
inline std::unique_ptr<lynx::Instrument> instrument (const lynx::fpc::Kit& kit, const std::function<std::shared_ptr<const lynx::SampleData> (const std::string&)>& samples)
{
    using namespace lynx;
    ChannelDef c;
    c.instrument = InstrumentKind::fpc;
    setFpcKit (c, kit, {});
    auto inst = makeFpcInstrument (c, samples);
    inst->prepare (44100.0);
    inst->configure (c, 120.0);
    return inst;
}

inline std::vector<float> run (lynx::Instrument& inst, int frames, std::vector<float>* right = nullptr)
{
    std::vector<float> l ((std::size_t) frames, 0.0f), r ((std::size_t) frames, 0.0f);
    for (int at = 0; at < frames; at += 256)
    {
        const auto n = std::min (256, frames - at);
        inst.render (l.data() + at, r.data() + at, n, 1.0f, 1.0f);
    }
    if (right != nullptr)
        *right = std::move (r);
    return l;
}
} // namespace fpc190z

TEST (fpc190z_tick_and_time_scale)
{
    // FPC steps its envelopes every 48 samples at 44.1 kHz (4·round(48·rate/44100/4) else) and a point's time unit is
    // half a second (FUN_00873060 / FUN_00872ff0)
    using namespace lynx;
    CHECK (FpcEnvelope::ticksFor (44100.0) == 48 && FpcEnvelope::ticksFor (48000.0) == 52 && FpcEnvelope::ticksFor (96000.0) == 104);
    CHECK (std::abs (FpcEnvelope::scaleFor (44100.0) - 2.0 * 48.0 / 44100.0) < 1e-15);
    CHECK (std::abs (FpcEnvelope::scaleFor (48000.0) - 2.0 * 52.0 / 48000.0) < 1e-12);
}

TEST (fpc190z_envelopes_follow_fl_tick_by_tick)
{
    // The six envelopes of FL's render, tick by tick: FL's gain at the start of tick k is the level FPC read at the
    // tick before (the first tick jumps to the start level)
    using namespace lynx;
    const auto cases = fpc190z::flCases();
    for (std::size_t n = 0; n < cases.size(); ++n)
    {
        const auto& c = cases[n];
        const FpcEnvelope e (c.env, false);
        CHECK (e.enabled());
        const auto trace = e.trace (44100.0, c.released, 4000);
        CHECK (! trace.empty());
        for (const auto& [tick, fl] : c.fl)
        {
            // (the tick whose step ends the envelope goes to 0, FUN_0086a2d0)
            const auto at = std::max (0, tick - 1);
            const auto mine = at < (int) trace.size() - 2 ? trace[(std::size_t) at] : 0.0f;
            if (std::abs (mine - fl) > 3.0e-4f)
                std::printf ("  envelope %d tick %d: FL %.5f, LYNX %.5f\n", (int) n, tick, (double) fl, (double) mine);
            CHECK (std::abs (mine - fl) <= 3.0e-4f);
        }
    }

    // switched off (FL's default for most pads): nothing to play
    auto off = fpc190z::flCases()[0].env;
    off.tail[1] = 0;
    CHECK (! FpcEnvelope (off, false).enabled());
    // the pan envelope reads its level around the centre: FL's default pan envelope (0.5) is the centre
    using P = fpc::EnvelopePoint;
    const FpcEnvelope pan (fpc190z::envelope (1, { P { 0, 0.5, 0.2f, 0 } }), true);
    FpcEnvelope::State s;
    pan.start (s, FpcEnvelope::scaleFor (44100.0));
    CHECK (s.out == 0.0f);
}

TEST (fpc190z_voices_take_the_envelope_and_end_with_it)
{
    // One pad, a DC layer: without envelope the output is the sample × the pad's level; with the kits' decay envelope
    // it follows the envelope tick by tick (linear ramps between ticks, the first tick at once) and the voice ends with
    // it, long before the sample; FL's default sustain envelope holds while the note lasts and releases after it
    using namespace lynx;
    using P = fpc::EnvelopePoint;
    auto kit = fpc::emptyKit();
    auto& pad = kit.pads[0];
    pad.note = 36;
    pad.layers = { fpc190z::layer ("dc.wav") };
    pad.envelopes[0].tail[1] = 0; // (FL's default volume envelope, off)
    const auto samples = [] (const std::string&) { return fpc190z::dc (44100 * 3); };
    constexpr float velocity = 100.0f / 128.0f; // (FL's velocity law is 1 at velocity 100)

    auto plain = fpc190z::instrument (kit, samples);
    plain->noteOn (36, velocity);
    const auto flat = fpc190z::run (*plain, 44100);
    const auto level = flat[1000];
    CHECK (level > 0.01f && flat[30000] == level && flat[0] == level);

    pad.envelopes[0] = fpc190z::envelope (0, { P { 0, 1, 0, 0 }, P { 1.25, 0, 0.2f, 0 } });
    auto decaying = fpc190z::instrument (kit, samples);
    decaying->noteOn (36, velocity);
    const auto out = fpc190z::run (*decaying, 44100);
    const auto trace = FpcEnvelope (pad.envelopes[0], false).trace (44100.0, -1, 4000);
    CHECK (trace.size() > 570 && trace.size() < 580);
    float worst = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        const auto k = i / 48, j = i % 48;
        // (the tick whose step ends the envelope ramps to 0, and the voice ends after it)
        const auto last = (int) trace.size() - 2;
        const auto from = k == 0 ? trace[0] : trace[(std::size_t) std::min (k - 1, last)];
        const auto to = k >= last ? 0.0f : trace[(std::size_t) k];
        const auto expected = k == 0 ? level * trace[0] : (k > last ? 0.0f : level * (from + (to - from) * (float) j / 48.0f));
        worst = std::max (worst, std::abs (out[(std::size_t) i] - expected));
    }
    CHECK (worst < 2.0e-5f);
    CHECK (out[28000] == 0.0f && decaying->idle()); // (the voice ended with its envelope, at 0.63 s)

    // held, then released: the sustain level until the note ends, the release after
    pad.envelopes[0] = fpc190z::envelope (0, { P { 0, 0, 0, 0 }, P { 0.75, 1, 0.2f, 0 }, P { 1.5, 0.5, 0.2f, 0 }, P { 0.25, 0, 0.2f, 0 } }, 1, -1, 2);
    auto held = fpc190z::instrument (kit, samples);
    held->noteOn (36, velocity);
    auto a = fpc190z::run (*held, 44100 * 2);
    CHECK (std::abs (a[44100 + 30000] / level - 0.5f) < 1.0e-4f && ! held->idle());
    held->noteOff (36);
    a = fpc190z::run (*held, 44100 / 2);
    CHECK (std::abs (a[100] / level - 0.5f) < 0.02f && a[10000] == 0.0f && held->idle()); // (REL: 0.25 units = 0.125 s)
}

TEST (fpc190z_layer_pan_pan_envelope_and_scale_volume)
{
    // Layer pan and the pan envelope (level × 2 + layer pan, within −1…1) go through the constant-power law, centred at
    // 1 (FL's render: hard left / right sweep to 1 LSB); a layer that does not scale its volume with the velocity
    // plays every velocity at the level of velocity 100 (FPC divides FL's Init.Vol out)
    using namespace lynx;
    using P = fpc::EnvelopePoint;
    const auto samples = [] (const std::string&) { return fpc190z::dc (44100); };
    auto kit = fpc::emptyKit();
    auto& pad = kit.pads[0];
    pad.note = 36;
    pad.layers = { fpc190z::layer ("dc.wav") };
    pad.envelopes[0].tail[1] = 0;
    constexpr float velocity = 100.0f / 128.0f;
    auto centre = fpc190z::instrument (kit, samples);
    centre->noteOn (36, velocity);
    std::vector<float> r0;
    const auto l0 = fpc190z::run (*centre, 2000, &r0);
    CHECK (l0[500] == r0[500]);

    pad.layers[0].pan = -64;
    auto left = fpc190z::instrument (kit, samples);
    left->noteOn (36, velocity);
    std::vector<float> r1;
    const auto l1 = fpc190z::run (*left, 2000, &r1);
    const auto angle = (-0.5f + 1.0f) * 0.25f * 3.14159265f;
    CHECK (std::abs (l1[500] / l0[500] - std::cos (angle) * 1.41421356f) < 1.0e-5f);
    CHECK (std::abs (r1[500] / r0[500] - std::sin (angle) * 1.41421356f) < 1.0e-5f);

    // the pan envelope: 0.5 → 0 over 1 unit (0.5 s); layer pan −64 on top: from half left to hard left
    pad.layers[0].pan = -64;
    pad.envelopes[1] = fpc190z::envelope (1, { P { 0, 0.5, 0, 0 }, P { 1.0, 0.0, 0, 0 } });
    auto sweep = fpc190z::instrument (kit, samples);
    sweep->noteOn (36, velocity);
    std::vector<float> r2;
    const auto l2 = fpc190z::run (*sweep, 44100, &r2);
    CHECK (std::abs (l2[0] / l0[500] - std::cos (angle) * 1.41421356f) < 1.0e-5f); // (starts at the layer's −64)
    CHECK (std::abs (r2[30000]) < 1.0e-6f && std::abs (l2[30000] / l0[500] - 1.41421356f) < 1.0e-4f); // (2·(−0.5) − 0.5, clamped)

    // scale volume off: velocity 40 plays as loud as velocity 100
    pad.layers[0].pan = 0;
    pad.envelopes[1].tail[1] = 0;
    pad.layers[0].flags = 0;
    auto flat = fpc190z::instrument (kit, samples);
    flat->noteOn (36, 40.0f / 128.0f);
    const auto l3 = fpc190z::run (*flat, 2000);
    CHECK (l3[500] == l0[500]);
    pad.layers[0].flags = 1;
    auto scaled = fpc190z::instrument (kit, samples);
    scaled->noteOn (36, 40.0f / 128.0f);
    CHECK (fpc190z::run (*scaled, 2000)[500] < 0.5f * l0[500]);
}

TEST (fpc190z_layer_modes)
{
    // FPC_x64.dll FUN_00869db0: without "one at a time" every layer whose range holds the velocity plays (here two);
    // Cycle backward steps back from the previous pick (wrapping, from the last one first); Cycle steps on among the
    // layers that hold the velocity; Random (avoid previous) never picks the same twice; Random draws them all
    using namespace lynx;
    // each layer's file a DC of its own level: the output says which layers played
    const auto samples = [] (const std::string& path) { return fpc190z::dc (2000, path == "a" ? 0.001f : path == "b" ? 0.01f : path == "c" ? 0.1f : 1.0f); };
    auto kit = fpc::emptyKit();
    auto& pad = kit.pads[0];
    pad.note = 36;
    pad.layers = { fpc190z::layer ("a", 0, 64), fpc190z::layer ("b", 32, 127), fpc190z::layer ("c", 100, 127) };
    for (auto& l : pad.layers)
        l.flags = 0; // (every velocity at one level)
    pad.envelopes[0].tail[1] = 0;
    // which layers a hit at `velocity` plays (the pad's level divided out)
    const auto play = [&] (Instrument& inst, int velocity) {
        inst.noteOn (36, (float) velocity / 128.0f);
        const auto out = fpc190z::run (inst, 100)[50];
        inst.allNotesOff();
        fpc190z::run (inst, 400); // (the stopped voices fade out)
        return out;
    };
    auto all = fpc190z::instrument (kit, samples);
    const auto ab = play (*all, 50), b = play (*all, 80), bc = play (*all, 120);
    if (! (ab > b && bc > 5.0f * b))
        std::printf ("  layers: 50 -> %g, 80 -> %g, 120 -> %g\n", (double) ab, (double) b, (double) bc);
    CHECK (ab > b && bc > 5.0f * b); // (two layers hold 50 and 120, one holds 80)

    // one at a time
    pad.flags = 8;
    pad.layers = { fpc190z::layer ("a"), fpc190z::layer ("b"), fpc190z::layer ("c") };
    for (auto& l : pad.layers)
        l.flags = 0;
    auto which = [&] (Instrument& inst) {
        const auto v = play (inst, 100);
        const auto ratio = v / play (*all, 80); // (in units of layer b's level)
        return ratio < 0.5f ? 0 : ratio < 5.0f ? 1 : 2;
    };
    pad.trigger[0] = 2; // Cycle backward: c, b, a, c…
    auto back = fpc190z::instrument (kit, samples);
    CHECK (which (*back) == 2 && which (*back) == 1 && which (*back) == 0 && which (*back) == 2);
    pad.trigger[0] = 3; // Cycle: a, b, c, a…
    auto cycle = fpc190z::instrument (kit, samples);
    CHECK (which (*cycle) == 0 && which (*cycle) == 1 && which (*cycle) == 2 && which (*cycle) == 0);
    pad.trigger[0] = 1; // Random (avoid previous)
    auto avoid = fpc190z::instrument (kit, samples);
    std::array<int, 3> counts {};
    int previous = -1;
    for (int i = 0; i < 300; ++i)
    {
        const auto w = which (*avoid);
        CHECK (w != previous);
        previous = w;
        ++counts[(std::size_t) w];
    }
    CHECK (counts[0] > 60 && counts[1] > 60 && counts[2] > 60);
    pad.trigger[0] = 0; // Random
    auto random = fpc190z::instrument (kit, samples);
    counts = {};
    int repeats = 0;
    previous = -1;
    for (int i = 0; i < 300; ++i)
    {
        const auto w = which (*random);
        repeats += w == previous ? 1 : 0;
        previous = w;
        ++counts[(std::size_t) w];
    }
    CHECK (counts[0] > 60 && counts[1] > 60 && counts[2] > 60 && repeats > 40);
}

TEST (fpc190z_choke_is_one_tick)
{
    // FPC's choke (FUN_0086b480 / FUN_0086a2d0): the cut voice goes to 0 linearly over its next tick (48 samples) and
    // ends there — not the channel's declicker
    using namespace lynx;
    auto kit = fpc::emptyKit();
    kit.pads[0].note = 36;
    kit.pads[0].cutBy = 0;
    kit.pads[0].layers = { fpc190z::layer ("long") };
    kit.pads[0].envelopes[0].tail[1] = 0;
    kit.pads[1].note = 38;
    kit.pads[1].cut = 0;
    kit.pads[1].layers = { fpc190z::layer ("short") };
    kit.pads[1].envelopes[0].tail[1] = 0;
    const auto samples = [] (const std::string& path) { return fpc190z::dc (path == "long" ? 44100 : 100, path == "long" ? 0.5f : 0.0f); };
    auto inst = fpc190z::instrument (kit, samples);
    inst->noteOn (36, 100.0f / 128.0f);
    auto before = fpc190z::run (*inst, 1000); // (tick boundaries at 960, 1008)
    const auto level = before[999];
    inst->noteOn (38, 100.0f / 128.0f);
    const auto after = fpc190z::run (*inst, 200);
    CHECK (after[0] == level && after[7] == level);            // (until the voice's next tick, sample 1008)
    CHECK (std::abs (after[8 + 24] / level - 0.5f) < 1.0e-3f);   // (half way down)
    CHECK (after[8 + 48] == 0.0f && after[150] == 0.0f);
    CHECK (inst->idle());
}

TEST (fpc190z_reversed_layer)
{
    // layer flag bit 1 ("Reverse sample"): the sample plays from its end
    using namespace lynx;
    auto ramp = std::make_shared<SampleData>();
    ramp->sampleRate = 44100.0;
    for (int i = 0; i < 1000; ++i)
        ramp->left.push_back ((float) i / 1000.0f);
    auto kit = fpc::emptyKit();
    kit.pads[0].note = 36;
    kit.pads[0].layers = { fpc190z::layer ("ramp") };
    kit.pads[0].layers[0].flags = 3;
    kit.pads[0].envelopes[0].tail[1] = 0;
    auto inst = fpc190z::instrument (kit, [ramp] (const std::string&) { return ramp; });
    inst->noteOn (36, 100.0f / 128.0f);
    const auto out = fpc190z::run (*inst, 1000);
    CHECK (out[0] > out[500] && out[500] > out[990] && out[0] > 0.0f);
}
