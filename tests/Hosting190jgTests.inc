// Round 190jg (BUILD-LIST 18): sample-accurate automation for hosted plugins. The renderer hands a plugin slot that
// takes timed changes (Effect / Instrument::setParameterAt) the automation's changes inside a chunk: a jump (Hold,
// Stairs, a step drawn) at its own sample, a ramp every 16 samples of the export grid. A stand-in "plugin" here applies
// them at their offsets and puts the parameter on its output, as LYNX's test plugins do (tools/testplugin/).

namespace hosting190jg
{
struct TimedDc final : lynx::Effect
{
    float level = 0.0f;
    struct Change
    {
        int offset;
        float value;
    };
    std::vector<Change> pending;
    int timedCalls = 0;
    bool timed = true;

    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    bool takesTimedParameters() const noexcept override { return timed; }
    void setParameter (int index, float value) noexcept override
    {
        if (index == 0)
            level = value;
    }
    void setParameterAt (int index, float value, int offset) noexcept override
    {
        if (index == 0)
            pending.push_back ({ offset, value });
        ++timedCalls;
    }
    void process (float* left, float* right, int n, const lynx::EffectParams&, double) noexcept override
    {
        std::size_t next = 0;
        for (int i = 0; i < n; ++i)
        {
            while (next < pending.size() && pending[next].offset <= i)
                level = pending[next++].value;
            left[i] = right[i] = level;
        }
        pending.clear();
    }
};

struct Render
{
    std::vector<float> out;
    int timedCalls = 0;
};

// a song whose automation drives parameter 0 of a hosted effect on the master; rendered in device blocks of `block`
Render render (const std::vector<lynx::AutomationPoint>& points, int block, bool timed, double seconds = 1.5)
{
    using namespace lynx;
    Song song;
    song.bpm = 120.0;
    song.mode = PlayMode::song;
    song.mixer = Song::defaultMixer (1);
    auto slot = makeEffectSlot (EffectKind::plugin);
    slot.plugin.id = "CLAP-Timed";
    slot.smartDisable = false;
    song.mixer[0].effects = { slot };
    Automation a;
    a.target = { AutomationTargetKind::effectParam, 0, 0, 0 };
    a.points = points;
    song.automations = { a };
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = 0;
    c.length = 8 * ppq;
    song.playlist = { c };

    std::shared_ptr<TimedDc> effect;
    RackState::Factories f;
    f.pluginEffects = [&] (const PluginRef&, double) -> std::shared_ptr<Effect> {
        effect = std::make_shared<TimedDc>();
        effect->timed = timed;
        return effect;
    };
    auto state = RackState::build (song, nullptr, 44100.0, f);
    RackRenderer renderer;
    renderer.prepare (44100.0, 2048);
    const auto tps = ticksPerSample (song.bpm, 44100.0);
    const auto total = (int) (seconds * 44100.0);
    Render result;
    result.out.assign ((std::size_t) total, 0.0f);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        renderer.process (state.get(), true, pos * tps, n, l.data(), r.data());
        std::copy (l.begin(), l.begin() + n, result.out.begin() + pos);
    }
    result.timedCalls = effect != nullptr ? effect->timedCalls : 0;
    return result;
}

lynx::AutomationPoint point (std::int64_t tick, float value, int mode)
{
    lynx::AutomationPoint p;
    p.tick = tick;
    p.value = value;
    p.mode = (std::uint8_t) mode;
    return p;
}

// the first sample (from `from`) whose level differs from the one at `from`
long firstChange (const std::vector<float>& x, long from)
{
    for (auto i = from + 1; i < (long) x.size(); ++i)
        if (x[(std::size_t) i] != x[(std::size_t) from])
            return i;
    return -1;
}
} // namespace hosting190jg

TEST (s190jg_a_step_reaches_a_timed_plugin_at_its_own_sample)
{
    using namespace lynx;
    constexpr std::int64_t t1 = ppq + 7, t2 = 2 * ppq + 5;
    const auto tps = ticksPerSample (120.0, 44100.0);
    const auto expected1 = (long) std::ceil ((double) t1 / tps - 1.0e-6), expected2 = (long) std::ceil ((double) t2 / tps - 1.0e-6);
    CHECK (expected1 % 16 != 0 && expected2 % 16 != 0); // (off every grid)
    const std::vector<AutomationPoint> steps { hosting190jg::point (0, 0.2f, flcurve::hold), hosting190jg::point (t1, 0.8f, flcurve::hold),
                                               hosting190jg::point (t2, 0.35f, flcurve::hold) };

    const auto on = hosting190jg::render (steps, 512, true);
    CHECK (hosting190jg::firstChange (on.out, expected1 - 400) == expected1);
    CHECK (hosting190jg::firstChange (on.out, expected2 - 400) == expected2);
    CHECK (on.out[(std::size_t) expected1] == 0.8f && on.out[(std::size_t) expected1 - 1] == 0.2f);
    CHECK (on.timedCalls >= 2);

    // the option off: at the next 128-sample piece, as before
    setSampleAccurateAutomation (false);
    const auto off = hosting190jg::render (steps, 512, true);
    setSampleAccurateAutomation (true);
    const auto lateFirst = hosting190jg::firstChange (off.out, expected1 - 400);
    CHECK (lateFirst > expected1 && lateFirst % 128 == 0 && lateFirst - expected1 < 128);
    CHECK (off.timedCalls == 0);

    // a plugin that takes no timed changes (VST2, a separate process): the chunk's value, as before
    const auto plain = hosting190jg::render (steps, 512, false);
    CHECK (plain.out == off.out);
}

TEST (s190jg_timed_automation_is_the_same_whatever_the_device_block)
{
    using namespace lynx;
    // an export renders blocks of 512; playback the device's: a piece a block cuts goes on with what it began with
    const std::vector<AutomationPoint> curve { hosting190jg::point (0, 0.0f, 0), hosting190jg::point (2 * ppq + 11, 1.0f, flcurve::hold),
                                               hosting190jg::point (2 * ppq + 300, 0.3f, 0), hosting190jg::point (5 * ppq, 0.9f, 0) };
    const auto reference = hosting190jg::render (curve, 512, true);
    for (const auto block : { 64, 100, 256, 441, 1024 })
        CHECK (hosting190jg::render (curve, block, true).out == reference.out);
}

TEST (s190jg_a_ramp_moves_every_16_samples_with_timed_automation)
{
    using namespace lynx;
    // a straight ramp 0 -> 1 over two beats: the chunk's own value every 128 samples before, every 16 now
    const std::vector<AutomationPoint> ramp { hosting190jg::point (0, 0.0f, 0), hosting190jg::point (2 * ppq, 1.0f, 0) };
    const auto distinct = [] (const std::vector<float>& x, long from, long to) {
        std::set<float> values (x.begin() + from, x.begin() + to);
        return (int) values.size();
    };
    const auto on = hosting190jg::render (ramp, 512, true, 0.5);
    setSampleAccurateAutomation (false);
    const auto off = hosting190jg::render (ramp, 512, true, 0.5);
    setSampleAccurateAutomation (true);
    CHECK (distinct (off.out, 1024, 1024 + 128) == 1);
    CHECK (distinct (on.out, 1024, 1024 + 128) == 8);
    // the same ramp, only finer: never further from the curve than the 128-sample steps were
    auto worstOn = 0.0f, worstOff = 0.0f;
    const auto tps = ticksPerSample (120.0, 44100.0);
    for (long i = 0; i < (long) on.out.size() && i * tps < 2 * ppq - 1; ++i)
    {
        const auto ideal = (float) (i * tps / (2.0 * ppq));
        worstOn = std::max (worstOn, std::abs (on.out[(std::size_t) i] - ideal));
        worstOff = std::max (worstOff, std::abs (off.out[(std::size_t) i] - ideal));
    }
    CHECK (worstOn < worstOff / 4.0f);
}

namespace hosting190jg
{
struct TailedPlugin final : lynx::Effect
{
    int tail = -1;
    long processed = 0;
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    int tailSamples() const noexcept override { return tail; }
    void process (float*, float*, int n, const lynx::EffectParams&, double) noexcept override { processed += n; }
};

long processedSamples (int tail)
{
    using namespace lynx;
    Song song;
    song.mixer = Song::defaultMixer (1);
    auto slot = makeEffectSlot (EffectKind::plugin);
    slot.plugin.id = "CLAP-Tailed";
    slot.plugin.allowSmartDisable = true;
    slot.smartDisable = true;
    song.mixer[1].effects = { slot };
    std::shared_ptr<TailedPlugin> effect;
    RackState::Factories f;
    f.pluginEffects = [&] (const PluginRef&, double) -> std::shared_ptr<Effect> {
        effect = std::make_shared<TailedPlugin>();
        effect->tail = tail;
        return effect;
    };
    auto state = RackState::build (song, nullptr, 44100.0, f);
    RackRenderer renderer;
    renderer.prepare (44100.0, 2048);
    std::vector<float> l (256), r (256);
    for (int pos = 0; pos < 44100; pos += 256)
        renderer.process (state.get(), false, 0.0, 256, l.data(), r.data());
    return effect != nullptr ? effect->processed : -1;
}
} // namespace hosting190jg

TEST (s190jg_a_hosted_plugins_declared_tail_ends_its_smart_disable_hold)
{
    // a silent insert: a plugin that declares a 1000-sample tail (CLAP's clap.tail) stops being run after it; one that
    // declares none keeps the plugins' fixed 4 s hold (the whole second here)
    const auto declared = hosting190jg::processedSamples (1000), none = hosting190jg::processedSamples (-1);
    CHECK (none >= 44100 - 256);
    CHECK (declared >= 1000 && declared <= 1000 + 2 * 256);
}
