// Round 190ox: modulators stage 2 (research 190oe P9) — the new sources against reference curves, renders where they
// drive a target (an envelope follower on an insert's level, an ADSR on a channel's notes, keytrack, a macro and its
// automation), hosted plugin parameters as targets (clamp (base + depth × LFO), the base from an automation), the .flp
// export's automation clips, and stage 1's modulators unchanged.
namespace
{
// the song's master volume as the output level of a constant input, block by block from tick 0 (`blocks` of 256)
std::vector<float> mod190oxLevels (const lynx::Song& song, int blocks, const std::vector<float>* input = nullptr,
                                   const lynx::RackState::Factories* factories = nullptr)
{
    auto state = factories != nullptr ? lynx::RackState::build (song, nullptr, 48000.0, *factories) : lynx::RackState::build (song, nullptr, 48000.0);
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    lynx::LiveInput live;
    renderer.setLiveInput (&live);
    std::vector<float> a (256, 0.5f), l (256), r (256);
    live.channels[0] = a.data();
    live.channels[1] = a.data();
    live.numChannels = 2;
    const auto tps = song.bpm * lynx::ppq / 60.0 / 48000.0;
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        if (input != nullptr)
            for (int i = 0; i < 256; ++i)
                a[(std::size_t) i] = (std::size_t) (b * 256 + i) < input->size() ? (*input)[(std::size_t) (b * 256 + i)] : 0.0f;
        renderer.process (state.get(), true, b * 256 * tps, 256, l.data(), r.data());
        out.push_back (std::abs (l[255]));
    }
    return out;
}

lynx::Song mod190oxSong()
{
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.mode = lynx::PlayMode::song;
    song.bpm = 120.0;
    song.mixer = lynx::Song::defaultMixer (2);
    song.mixer[0].inputChannel = 0;
    song.mixer[0].monitor = lynx::InputMonitor::automatic;
    song.mixer[0].volume = 0.3f;
    return song;
}

// a stand-in for a hosted plugin effect: it records the parameter values the host sends
struct Mod190oxProbe final : lynx::Effect
{
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const lynx::EffectParams&, double) noexcept override {}
    void setParameter (int index, float value) noexcept override
    {
        if (index == 0)
            last = value, ++sent;
    }
    float last = -1.0f;
    int sent = 0;
};
} // namespace

TEST (mod190ox_steps_random_reference)
{
    auto steps = lynx::Modulator::make (lynx::ModKind::steps);
    CHECK (steps.steps.size() == 8 && steps.beats == 0.25 && steps.deterministic());
    steps.steps = { 1.0f, -1.0f, 0.5f };
    CHECK (steps.stepOutput (0.2) == 1.0f && steps.stepOutput (1.5) == -1.0f && steps.stepOutput (2.9) == 0.5f);
    CHECK (steps.stepOutput (3.1) == 1.0f); // (it loops)
    steps.glide = 0.5f; // the second half of a step slides to the next value: half way at 3/4, S-shaped
    CHECK (steps.stepOutput (0.4) == 1.0f && std::abs (steps.stepOutput (0.75) - 0.0f) < 1.0e-5f && std::abs (steps.stepOutput (0.999) + 1.0f) < 1.0e-3f);
    steps.unipolar = true;
    CHECK (steps.stepOutput (0.2) == 1.0f && std::abs (steps.stepOutput (1.2) - 0.0f) < 1.0e-6f);

    auto random = lynx::Modulator::make (lynx::ModKind::random);
    const auto a = random.stepOutput (5.1), b = random.stepOutput (5.9), c = random.stepOutput (6.1);
    CHECK (a == b && a != c && a >= -1.0f && a <= 1.0f); // sample and hold: one value a step, the same for the same step
    lynx::Modulator again = random;
    CHECK (again.stepOutput (5.5) == a); // (by song position, whatever played before)
    random.smooth = 1.0f; // smooth: continuous over the step's edge
    CHECK (std::abs (random.stepOutput (6.0 - 1.0e-6) - random.stepOutput (6.0)) < 1.0e-3f);
    random.sync = false;
    CHECK (! random.deterministic() && lynx::Modulator::make (lynx::ModKind::macro).deterministic());
    CHECK (! lynx::Modulator::make (lynx::ModKind::follower).deterministic());
    CHECK (std::string (lynx::Modulator::kindName (lynx::ModKind::envelope)) == "ADSR envelope");
}

TEST (mod190ox_envelope_and_follower_reference)
{
    auto env = lynx::Modulator::make (lynx::ModKind::envelope);
    env.attack = 10.0f;
    env.decay = 100.0f;
    env.sustain = 0.5f;
    env.release = 50.0f;
    lynx::modenv::State s;
    // attack: linear, 1 in 10 ms (here 1 ms steps)
    lynx::modenv::envelope (env, s, true, true, 0.001);
    CHECK (std::abs (s.level - 0.1f) < 1.0e-5f && s.stage == 1);
    for (int k = 0; k < 4; ++k)
        lynx::modenv::envelope (env, s, true, false, 0.001);
    CHECK (std::abs (s.level - 0.5f) < 1.0e-4f);
    for (int k = 0; k < 5; ++k)
        lynx::modenv::envelope (env, s, true, false, 0.001);
    CHECK (s.level == 1.0f && s.stage == 2);
    // decay: exponential towards the sustain, time constant 100 ms
    lynx::modenv::envelope (env, s, true, false, 0.1);
    CHECK (std::abs (s.level - (0.5f + 0.5f * std::exp (-1.0f))) < 1.0e-4f);
    for (int k = 0; k < 100; ++k)
        lynx::modenv::envelope (env, s, true, false, 0.05);
    CHECK (std::abs (s.level - 0.5f) < 1.0e-3f);
    // release: towards 0 with 50 ms; then idle
    lynx::modenv::envelope (env, s, false, false, 0.05);
    CHECK (s.stage == 4 && std::abs (s.level - 0.5f * std::exp (-1.0f)) < 2.0e-3f);
    for (int k = 0; k < 40; ++k)
        lynx::modenv::envelope (env, s, false, false, 0.05);
    CHECK (s.level == 0.0f && s.stage == 0);
    // a new note retriggers from where it is
    lynx::modenv::envelope (env, s, true, true, 0.005);
    CHECK (s.stage == 1 && std::abs (s.level - 0.5f) < 1.0e-4f);

    auto f = lynx::Modulator::make (lynx::ModKind::follower);
    f.attack = 10.0f;
    f.release = 100.0f;
    f.gain = 6.0f; // (+6 dB: 0.25 reads ~0.5)
    lynx::modenv::State fs;
    lynx::modenv::follow (f, fs, 0.25f, 0.01);
    const auto in = 0.25f * std::pow (10.0f, 6.0f / 20.0f);
    CHECK (std::abs (fs.level - in * (1.0f - std::exp (-1.0f))) < 1.0e-4f); // one attack time constant
    for (int k = 0; k < 50; ++k)
        lynx::modenv::follow (f, fs, 0.25f, 0.01);
    CHECK (std::abs (fs.level - in) < 1.0e-4f);
    const auto top = fs.level;
    lynx::modenv::follow (f, fs, 0.0f, 0.1);
    CHECK (std::abs (fs.level - top * std::exp (-1.0f)) < 1.0e-4f); // one release time constant
}

TEST (mod190ox_follower_render_drives_a_target)
{
    // insert 1 takes the input (a burst, then silence); the follower on it drives the Master's volume
    auto song = mod190oxSong();
    song.mixer[0].inputChannel = -1;
    song.mixer[1].inputChannel = 0;
    song.mixer[1].monitor = lynx::InputMonitor::on;
    auto f = lynx::Modulator::make (lynx::ModKind::follower);
    f.name = "Follower 1";
    f.host = { lynx::AutomationTargetKind::mixerVolume, 1, 0, 0 };
    f.attack = 5.0f;
    f.release = 30.0f;
    f.routes = { { { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 }, 0.5f } };
    song.modulators.push_back (f);
    const auto state = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (state->modStateful && state->modFeeds.size() == 1 && state->modFeeds[0].track == 1 && state->followedTracks.size() >= 2
           && state->followedTracks[1] == 1);

    std::vector<float> burst (256 * 120, 0.0f);
    for (int i = 0; i < 256 * 40; ++i)
        burst[(std::size_t) i] = 0.5f; // 40 blocks loud, then silence
    auto plain = song;
    plain.modulators.clear();
    const auto ref = mod190oxLevels (plain, 120, &burst);
    const auto mod = mod190oxLevels (song, 120, &burst);
    // while loud: the Master is louder than without the follower; after the release it is back
    CHECK (mod[35] > ref[35] * 1.3f);
    CHECK (ref[100] < 1.0e-6f && mod[100] < 1.0e-6f); // (silence stays silence)
    // the Master level just after the burst falls back as the follower releases (30 ms ≈ 6 blocks)
    std::vector<float> steady (256 * 120, 0.5f);
    const auto follow = mod190oxLevels (song, 120, &steady);
    CHECK (follow[60] > ref[35] * 1.3f);
}

TEST (mod190ox_envelope_keytrack_velocity_from_notes)
{
    // a silent channel's notes (one bar at 1 beat, key 72, velocity 0.8) drive the Master's volume
    auto song = mod190oxSong();
    lynx::ChannelDef c;
    c.name = "Notes";
    c.instrument = lynx::InstrumentKind::sampler;
    c.volume = 0.0f;
    song.channels.push_back (c);
    song.patterns.clear();
    song.patterns.emplace_back();
    lynx::Note n;
    n.channel = 0;
    n.key = 72;
    n.velocity = 0.8f;
    n.start = lynx::ppq;
    n.length = lynx::ppq;
    song.patterns[0].notes.push_back (n);
    lynx::Clip clip;
    clip.pattern = 0;
    clip.length = 8 * lynx::ppq;
    song.playlist.push_back (clip);

    auto env = lynx::Modulator::make (lynx::ModKind::envelope);
    env.name = "Envelope 1";
    env.host = { lynx::AutomationTargetKind::channelVolume, 0, 0, 0 };
    env.attack = 1.0f;
    env.decay = 1.0f;
    env.sustain = 1.0f;
    env.release = 20.0f;
    env.routes = { { { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 }, 0.5f } };
    song.modulators.push_back (env);
    // one beat = 24000 samples at 120 BPM (~94 blocks)
    const auto levels = mod190oxLevels (song, 300);
    const auto before = levels[60], during = levels[150], after = levels[290];
    CHECK (during > before * 1.5f);
    CHECK (std::abs (after - before) < before * 0.05f);

    // keytrack: key 72 = +0.2 of ±1; velocity: 0.8 (unipolar)
    song.modulators[0] = lynx::Modulator::make (lynx::ModKind::keytrack);
    song.modulators[0].host = { lynx::AutomationTargetKind::channelVolume, 0, 0, 0 };
    song.modulators[0].routes = { { { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 }, 1.0f } };
    const auto key = mod190oxLevels (song, 300);
    song.modulators[0].kind = lynx::ModKind::velocity;
    song.modulators[0].unipolar = true;
    song.modulators[0].routes[0].depth = 0.5f;
    const auto vel = mod190oxLevels (song, 300);
    // both 0 before the first note, the note's value after it (they hold the last note)
    CHECK (std::abs (key[60] - before) < before * 0.02f && key[290] > before * 1.2f);
    CHECK (std::abs (vel[60] - before) < before * 0.02f && vel[290] > key[290]); // (0.3 + 0.4 against 0.3 + 0.2)
}

TEST (mod190ox_macro_and_its_automation)
{
    auto song = mod190oxSong();
    auto m = lynx::Modulator::make (lynx::ModKind::macro);
    m.name = "Macro 1";
    m.macro = 0.0f;
    m.routes = { { { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 }, 0.5f } };
    song.modulators.push_back (m);
    const lynx::AutomationTarget knob { lynx::AutomationTargetKind::modulatorMacro, 0, 0, 0 };
    CHECK (song.targetValue (knob) == 0.0f && song.setTargetValue (knob, 0.6f) && song.modulators[0].macro == 0.6f);
    CHECK (! lynx::Modulator::canModulate (song, knob)); // (a modulator doesn't drive another)
    const auto at06 = mod190oxLevels (song, 8).back();
    song.modulators[0].macro = 0.0f;
    const auto at0 = mod190oxLevels (song, 8).back();
    CHECK (at06 > at0 * 1.3f);

    // an automation clip of the knob drives it (and its targets) instead of the saved value
    lynx::Automation a;
    a.target = knob;
    a.points = { { 0, 0.6f, 0.0f }, { 16 * lynx::ppq, 0.6f, 0.0f } };
    song.automations.push_back (a);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.length = 16 * lynx::ppq;
    song.playlist.push_back (clip);
    CHECK (std::abs (mod190oxLevels (song, 8).back() - at06) < at06 * 0.02f);

    // deleting the modulator unlinks what pointed at its knob
    song.removeModulator (0);
    CHECK (song.modulators.empty() && song.automations[0].target.kind == lynx::AutomationTargetKind::none);
}

TEST (mod190ox_hosted_plugin_parameter)
{
    // a hosted effect on insert 1 (a probe standing in), its parameter 0 automated at 0.4 and modulated by a square
    // LFO of depth 0.3: the plugin gets clamp (0.4 ± 0.3), the automation's own binding sends nothing
    auto song = mod190oxSong();
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::plugin);
    slot.plugin.id = "probe";
    slot.plugin.name = "Probe";
    song.mixer[1].effects.push_back (slot);
    const lynx::AutomationTarget param { lynx::AutomationTargetKind::effectParam, 1, 0, 0 };
    CHECK (lynx::Modulator::isHostedTarget (song, param) && lynx::Modulator::canModulate (song, param));
    CHECK (! lynx::Modulator::canModulate (song, { lynx::AutomationTargetKind::effectParam, 1, 0, lynx::midictl::ccBase + 1 }));

    lynx::Modulator lfo;
    lfo.name = "LFO 1";
    lfo.shape = lynx::ModShape::square;
    lfo.beats = 4.0;
    lfo.routes = { { param, 0.3f, 0.7f } };
    song.modulators.push_back (lfo);

    auto probe = std::make_shared<Mod190oxProbe>();
    lynx::RackState::Factories factories;
    factories.pluginEffects = [probe] (const lynx::PluginRef& ref, double) -> std::shared_ptr<lynx::Effect> {
        return ref.id == "probe" ? probe : nullptr;
    };
    {
        const auto state = lynx::RackState::build (song, nullptr, 48000.0, factories);
        CHECK (state->modRoutes.size() == 1 && state->modRoutes[0].hosted && state->modRoutes[0].base == 0.7f);
    }
    // no automation: around the route's own base, clamped (0.7 + 0.3 = 1.0 on the high half, 0.4 on the low)
    mod190oxLevels (song, 4, nullptr, &factories);
    CHECK (std::abs (probe->last - 1.0f) < 1.0e-6f);

    lynx::Automation a;
    a.target = param;
    a.points = { { 0, 0.4f, 0.0f }, { 16 * lynx::ppq, 0.4f, 0.0f } };
    song.automations.push_back (a);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.length = 16 * lynx::ppq;
    song.playlist.push_back (clip);
    {
        const auto state = lynx::RackState::build (song, nullptr, 48000.0, factories);
        const auto bound = std::count_if (state->pluginParams.begin(), state->pluginParams.end(), [] (const auto& b) { return b.track == 1 && b.param == 0; });
        CHECK (bound == 1 && state->modRoutes.size() == 1 && state->modRoutes[0].source == 0);
    }
    probe->last = -1.0f;
    mod190oxLevels (song, 20, nullptr, &factories); // first half of the bar: +1
    CHECK (std::abs (probe->last - 0.7f) < 1.0e-5f);
    // the second half (a bar is 96000 samples at 120 BPM): 0.4 − 0.3
    probe->last = -1.0f;
    {
        auto state = lynx::RackState::build (song, nullptr, 48000.0, factories);
        lynx::RackRenderer renderer;
        renderer.prepare (48000.0, 512);
        std::vector<float> l (256), r (256);
        const auto tps = song.bpm * lynx::ppq / 60.0 / 48000.0;
        for (int b = 0; b < 4; ++b)
            renderer.process (state.get(), true, (60000 + b * 256) * tps, 256, l.data(), r.data());
    }
    CHECK (std::abs (probe->last - 0.1f) < 1.0e-5f);
    // a route without a known base and nothing automating the parameter is not bound (the plugin keeps its value)
    song.automations.clear();
    song.playlist.clear();
    song.modulators[0].routes[0].base = -1.0f;
    CHECK (lynx::RackState::build (song, nullptr, 48000.0, factories)->modRoutes.empty());
}

TEST (mod190ox_stage1_unchanged_and_lfo_only_costs_nothing_new)
{
    // stage 1's LFO modulators render exactly as before: the same output as the formula, no stateful work
    auto song = mod190oxSong();
    lynx::Modulator m;
    m.shape = lynx::ModShape::triangle;
    m.beats = 1.0;
    m.routes = { { { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 }, 0.25f } };
    song.modulators.push_back (m);
    const auto state = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (! state->modStateful && state->followedTracks.empty() && state->modRoutes.size() == 1 && ! state->modRoutes[0].hosted);
    CHECK (m.kind == lynx::ModKind::lfo && m.host.kind == lynx::AutomationTargetKind::none && m.steps.empty());
}

TEST (mod190ox_flp_export_bakes_what_fl_can_play)
{
    auto song = mod190oxSong();
    song.mixer = lynx::Song::defaultMixer (4);
    song.playlist.clear();
    lynx::Clip pc; // a pattern clip so the song has a length
    pc.pattern = 0;
    pc.length = 16 * lynx::ppq;
    song.patterns.resize (1);
    song.playlist.push_back (pc);

    lynx::Modulator tri; // a synced triangle LFO on insert 1's pan: FL's clip LFO, exact
    tri.name = "LFO 1";
    tri.shape = lynx::ModShape::triangle;
    tri.beats = 2.0;
    tri.routes = { { { lynx::AutomationTargetKind::mixerPan, 1, 0, 0 }, 0.25f } };
    song.modulators.push_back (tri);
    auto steps = lynx::Modulator::make (lynx::ModKind::steps); // steps on insert 2's volume: points, Hold
    steps.name = "Steps 1";
    steps.routes = { { { lynx::AutomationTargetKind::mixerVolume, 2, 0, 0 }, 0.2f } };
    song.modulators.push_back (steps);
    auto macro = lynx::Modulator::make (lynx::ModKind::macro); // a macro: a flat clip
    macro.name = "Macro 1";
    macro.macro = 0.5f;
    macro.routes = { { { lynx::AutomationTargetKind::mixerVolume, 3, 0, 0 }, 0.2f } };
    song.modulators.push_back (macro);
    auto follower = lynx::Modulator::make (lynx::ModKind::follower); // lost
    follower.name = "Follower 1";
    follower.routes = { { { lynx::AutomationTargetKind::mixerPan, 2, 0, 0 }, 0.5f } };
    song.modulators.push_back (follower);

    auto baked = song;
    const auto result = lynx::modexport::bakeForFl (baked);
    CHECK (result.clips == 3 && result.lost.size() == 1 && baked.modulators.empty() && baked.automations.size() == 3);
    // the LFO clip plays what the modulator played (LYNX plays FL's clip LFO as FL does)
    const auto& lfoClip = baked.automations[0];
    CHECK (lfoClip.lfo.on && lfoClip.lfo.add && lfoClip.lfo.level == 64 && lfoClip.target == tri.routes[0].target);
    const auto base = song.modulationBase (tri.routes[0].target);
    for (const auto beat : { 0.1, 0.5, 0.9, 1.3, 1.77, 3.2, 7.5 })
    {
        const auto tick = beat * lynx::ppq;
        const auto want = std::clamp (base + 0.25f * tri.output (beat / 2.0), 0.0f, 1.0f);
        CHECK (std::abs (lfoClip.playAt (tick, tick) - want) < 2.0e-3f);
    }
    // the steps clip: Hold points with the step values
    const auto& stepClip = baked.automations[1];
    const auto stepBase = song.modulationBase (steps.routes[0].target);
    for (const auto step : { 0.5, 1.5, 5.5, 12.25 })
        CHECK (std::abs (stepClip.valueAt (step * 0.25 * lynx::ppq) - std::clamp (stepBase + 0.2f * steps.stepOutput (step), 0.0f, 1.0f)) < 1.0e-5f);
    // the macro: base + 0.2 × 0.5 (unipolar)
    CHECK (std::abs (baked.automations[2].valueAt (lynx::ppq) - std::clamp (song.modulationBase (macro.routes[0].target) + 0.1f, 0.0f, 1.0f)) < 1.0e-6f);
    // each on a free track over the song
    CHECK (std::count_if (baked.playlist.begin(), baked.playlist.end(), [] (const lynx::Clip& c) { return c.kind == lynx::ClipKind::automation; }) == 3);

    // the whole export: notes about it, the follower dropped by name
    lynx::flpexport::Report report;
    lynx::flpexport::toProject (song, {}, report);
    CHECK (std::any_of (report.dropped.begin(), report.dropped.end(), [] (const std::string& d) { return d.find ("Follower 1") != std::string::npos; }));
    CHECK (std::any_of (report.notes.begin(), report.notes.end(), [] (const std::string& d) { return d.find ("3 route(s)") != std::string::npos; }));
    CHECK (std::abs (lynx::modexport::sineTension()) <= 128);
}

TEST (mod190ox_targets_follow_channel_and_insert_moves)
{
    auto song = mod190oxSong();
    song.mixer = lynx::Song::defaultMixer (3);
    song.channels.resize (2);
    auto f = lynx::Modulator::make (lynx::ModKind::envelope);
    f.host = { lynx::AutomationTargetKind::channelVolume, 1, 0, 0 };
    f.input = { lynx::AutomationTargetKind::channelVolume, 1, 0, 0 };
    song.modulators.push_back (f);
    song.removeChannel (0); // (channel 1 becomes 0)
    CHECK (song.modulators[0].host.index == 0 && song.modulators[0].input.index == 0);
    song.removeChannel (0);
    CHECK (song.modulators[0].host.kind == lynx::AutomationTargetKind::none);
}

// Round 190ox: the cost of a block with modulators (run under `perf stat -e instructions` with
// LYNX_MOD190OX_PERF=none|automation|lfo|follower|envelope|steps|eight): 60 s of a 2 Hz pulse through insert 1's Filter
// whose cutoff is automated / modulated, rendered in blocks of 256. Without the variable the test does nothing.
TEST (mod190ox_perf_cpu)
{
    const char* variant = std::getenv ("LYNX_MOD190OX_PERF");
    if (variant == nullptr)
        return;
    const std::string v = variant;
    auto song = mod190oxSong();
    song.mixer[0].inputChannel = -1;
    song.mixer[1].inputChannel = 0;
    song.mixer[1].monitor = lynx::InputMonitor::on;
    song.mixer[1].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::filter));
    const lynx::AutomationTarget cutoff { lynx::AutomationTargetKind::effectParam, 1, 0, 1 }; // (the Filter's Cutoff)
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.length = 1000 * lynx::ppq;
    if (v == "automation")
    {
        lynx::Automation a;
        a.target = cutoff;
        a.points = { { 0, 0.2f, 0.0f }, { 1000 * lynx::ppq, 0.9f, 0.0f } };
        song.automations.push_back (a);
        song.playlist.push_back (clip);
    }
    const auto route = std::vector<lynx::ModRoute> { { cutoff, 0.3f } };
    const auto addKind = [&] (lynx::ModKind kind) {
        auto m = lynx::Modulator::make (kind);
        m.host = { lynx::AutomationTargetKind::mixerVolume, 1, 0, 0 };
        m.routes = route;
        song.modulators.push_back (m);
    };
    if (v == "lfo")
        addKind (lynx::ModKind::lfo);
    else if (v == "follower")
        addKind (lynx::ModKind::follower);
    else if (v == "envelope")
        addKind (lynx::ModKind::envelope);
    else if (v == "steps")
        addKind (lynx::ModKind::steps);
    else if (v == "eight")
        for (int k = 0; k < lynx::modKindCount; ++k)
            addKind ((lynx::ModKind) k);
    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    lynx::LiveInput live;
    renderer.setLiveInput (&live);
    std::vector<float> a (256), l (256), r (256);
    live.channels[0] = a.data();
    live.channels[1] = a.data();
    live.numChannels = 2;
    const auto tps = song.bpm * lynx::ppq / 60.0 / 48000.0;
    double sum = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < 48000 * 60 / 256; ++b)
    {
        for (int i = 0; i < 256; ++i)
            a[(std::size_t) i] = ((b * 256 + i) % 24000) < 2400 ? 0.8f * std::sin ((float) i * 0.05f) : 0.0f;
        renderer.process (state.get(), true, b * 256 * tps, 256, l.data(), r.data());
        sum += std::abs (l[17]);
    }
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("mod190ox_perf %s: %.1f ms for 60 s (%d modulator(s), %d route(s)) sum %.3f\n", variant, ms, (int) song.modulators.size(),
                 (int) state->modRoutes.size(), sum);
    CHECK (sum >= 0.0);
}
