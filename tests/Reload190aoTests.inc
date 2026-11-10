// Round 190ao (TOP-20 #1, CROSS-DAW F50: Bitwig's "Reload plug-in"): a hosted plugin that crashed is started again
// without reopening the project. The engine keeps a plugin's running instance from one rack build to the next;
// forgetHostedPlugin makes the next build create that one again, and only that one, with the song untouched.

namespace reload190ao
{
struct FakeSynth final : lynx::Instrument
{
    static inline int made = 0;
    std::string stateAtCreation;
    explicit FakeSynth (std::string state) : stateAtCreation (std::move (state)) { ++made; }
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
};

struct FakeFx final : lynx::Effect
{
    static inline int made = 0;
    std::string stateAtCreation;
    explicit FakeFx (std::string state) : stateAtCreation (std::move (state)) { ++made; }
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const lynx::EffectParams&, double) noexcept override {}
};

lynx::RackState::Factories factories()
{
    lynx::RackState::Factories f;
    f.pluginInstruments = [] (const lynx::PluginRef& ref, double) -> std::shared_ptr<lynx::Instrument> { return std::make_shared<FakeSynth> (ref.state); };
    f.pluginEffects = [] (const lynx::PluginRef& ref, double) -> std::shared_ptr<lynx::Effect> { return std::make_shared<FakeFx> (ref.state); };
    return f;
}

lynx::Song song()
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (3);
    for (const char* name : { "Synth A", "Synth B" })
    {
        lynx::ChannelDef c;
        c.name = name;
        c.instrument = lynx::InstrumentKind::plugin;
        c.plugin.id = name;
        c.plugin.name = name;
        c.plugin.state = "c2F2ZWQ="; // what the last save captured
        c.mixerTrack = 1;
        s.channels.push_back (c);
    }
    for (const char* name : { "Fx A", "Fx B" })
    {
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::plugin);
        slot.plugin.id = name;
        slot.plugin.name = name;
        slot.plugin.state = "c2F2ZWQ=";
        s.mixer[1].effects.push_back (slot);
    }
    s.mixer[2].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::delay));
    s.patterns.emplace_back();
    s.uniqueInstances();
    return s;
}

// what the engine keeps between builds (AudioEngine::lastPublishedShape)
lynx::RackState shapeOf (const lynx::RackState& built)
{
    lynx::RackState shape;
    shape.song.channels = built.song.channels;
    shape.song.mixer = built.song.mixer;
    shape.song.bpm = built.song.bpm;
    shape.instruments = built.instruments;
    shape.effects = built.effects;
    shape.sampleRate = built.sampleRate;
    shape.serial = built.serial;
    return shape;
}
} // namespace reload190ao

TEST (reload190ao_a_plain_rebuild_keeps_every_running_plugin)
{
    using namespace reload190ao;
    const auto s = song();
    const auto f = factories();
    FakeSynth::made = FakeFx::made = 0;
    const auto a = lynx::RackState::build (s, nullptr, 48000.0, f);
    CHECK (FakeSynth::made == 2 && FakeFx::made == 2);
    auto shape = shapeOf (*a);
    const auto b = lynx::RackState::build (s, &shape, 48000.0, f);
    CHECK (FakeSynth::made == 2 && FakeFx::made == 2); // nothing reloads by itself
    CHECK (b->instruments[0] == a->instruments[0] && b->instruments[1] == a->instruments[1]);
    CHECK (b->effects[1][0] == a->effects[1][0] && b->effects[1][1] == a->effects[1][1]);
}

TEST (reload190ao_reloading_a_channel_plugin_makes_only_that_one_again_from_the_song_state)
{
    using namespace reload190ao;
    auto s = song();
    const auto f = factories();
    FakeSynth::made = FakeFx::made = 0;
    const auto a = lynx::RackState::build (s, nullptr, 48000.0, f);
    auto shape = shapeOf (*a);
    // the state captured since the instance was made (a save, a backup) is what the new instance starts from
    s.channels[1].plugin.state = "Y2FwdHVyZWQ=";
    shape.song.channels = s.channels;
    const auto before = s.channels;
    CHECK (lynx::forgetHostedPlugin (shape, 1));
    CHECK (s.channels == before); // the song is not edited: no undo step, nothing to save
    const auto b = lynx::RackState::build (s, &shape, 48000.0, f);
    CHECK (FakeSynth::made == 3 && FakeFx::made == 2);
    CHECK (b->instruments[0] == a->instruments[0]);
    CHECK (b->instruments[1] != a->instruments[1]);
    CHECK (static_cast<FakeSynth*> (b->instruments[1].get())->stateAtCreation == "Y2FwdHVyZWQ=");
    CHECK (b->song.channels[1].plugin.id == "Synth B"); // (the built state carries the real plugin)
    CHECK (b->effects[1][0] == a->effects[1][0] && b->effects[1][1] == a->effects[1][1]);

    // asked once, done once: the next build keeps the new instance (a plugin that crashes again is not reloaded
    // unless the user asks again)
    auto next = shapeOf (*b);
    const auto c = lynx::RackState::build (s, &next, 48000.0, f);
    CHECK (FakeSynth::made == 3);
    CHECK (c->instruments[1] == b->instruments[1]);
}

TEST (reload190ao_reloading_a_mixer_slot_plugin_keeps_the_others)
{
    using namespace reload190ao;
    const auto s = song();
    const auto f = factories();
    FakeSynth::made = FakeFx::made = 0;
    const auto a = lynx::RackState::build (s, nullptr, 48000.0, f);
    auto shape = shapeOf (*a);
    CHECK (lynx::forgetHostedPlugin (shape, 1, 1));
    const auto b = lynx::RackState::build (s, &shape, 48000.0, f);
    CHECK (FakeSynth::made == 2 && FakeFx::made == 3);
    CHECK (b->effects[1][0] == a->effects[1][0]);
    CHECK (b->effects[1][1] != a->effects[1][1]);
    CHECK (b->effects[2][0] == a->effects[2][0]);
    CHECK (b->instruments[0] == a->instruments[0] && b->instruments[1] == a->instruments[1]);
}

TEST (reload190ao_undo_after_a_reload_keeps_the_new_instance)
{
    using namespace reload190ao;
    auto s = song();
    const auto f = factories();
    const auto earlier = s; // an undo step taken before the crash
    s.channels[0].volume = 0.5f; // an edit
    FakeSynth::made = FakeFx::made = 0;
    const auto a = lynx::RackState::build (s, nullptr, 48000.0, f);
    auto shape = shapeOf (*a);
    CHECK (lynx::forgetHostedPlugin (shape, 0));
    const auto b = lynx::RackState::build (s, &shape, 48000.0, f);
    CHECK (FakeSynth::made == 3);
    // Undo puts the earlier song back: same plugin, same processing options, so the reloaded instance stays
    auto after = shapeOf (*b);
    const auto c = lynx::RackState::build (earlier, &after, 48000.0, f);
    CHECK (FakeSynth::made == 3);
    CHECK (c->instruments[0] == b->instruments[0]);
}

TEST (reload190ao_only_hosted_plugins_can_be_forgotten)
{
    using namespace reload190ao;
    const auto s = song();
    const auto a = lynx::RackState::build (s, nullptr, 48000.0, factories());
    auto shape = shapeOf (*a);
    CHECK (! lynx::forgetHostedPlugin (shape, -1));
    CHECK (! lynx::forgetHostedPlugin (shape, 2));
    CHECK (! lynx::forgetHostedPlugin (shape, 2, 0)); // a built-in effect
    CHECK (! lynx::forgetHostedPlugin (shape, 1, 5));
    CHECK (! lynx::forgetHostedPlugin (shape, 9, 0));
}
