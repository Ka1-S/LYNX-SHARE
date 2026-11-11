// Round 190az: a hosted plugin gets its saved settings once, when its instance is created. A project opened over
// another one with the same plugin in the same channel / slot kept the first project's running instance, so it played
// (and at the next save, stored) the first project's settings: --vst2test's second render, peak_gain_0.4=0.1.

namespace pluginstate190az
{
struct Silent final : lynx::Instrument
{
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
};
struct Pass final : lynx::Effect
{
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const lynx::EffectParams&, double) noexcept override {}
};
// a project as it comes from a file: every channel and slot is a new one (new instance tags)
inline lynx::Song project (const std::string& instrumentState, const std::string& effectState)
{
    auto s = lynx::Song::makeDefault();
    auto& c = s.channels[0];
    c.instrument = lynx::InstrumentKind::plugin;
    c.plugin = {};
    c.plugin.id = "VST-Test";
    c.plugin.state = instrumentState;
    lynx::EffectSlot slot;
    slot.kind = lynx::EffectKind::plugin;
    slot.plugin.id = "VST-Effect";
    slot.plugin.state = effectState;
    s.mixer[1].effects.clear();
    s.mixer[1].effects.push_back (slot);
    return s;
}
struct Made
{
    std::vector<std::string> instruments, effects; // the saved settings each created instance was given
    lynx::RackState::Factories factories()
    {
        lynx::RackState::Factories f;
        f.pluginInstruments = [this] (const lynx::PluginRef& ref, double) -> std::shared_ptr<lynx::Instrument> {
            instruments.push_back (ref.state);
            return std::make_shared<Silent>();
        };
        f.pluginEffects = [this] (const lynx::PluginRef& ref, double) -> std::shared_ptr<lynx::Effect> {
            effects.push_back (ref.state);
            return std::make_shared<Pass>();
        };
        return f;
    }
};
} // namespace pluginstate190az

TEST (pluginstate190az_a_project_opened_over_another_gets_its_own_plugin_settings)
{
    using namespace pluginstate190az;
    Made made;
    const auto f = made.factories();
    auto state = lynx::RackState::build (project ("gain 0.1", "wet 0.1"), nullptr, 48000.0, f);
    CHECK (made.instruments == std::vector<std::string> { "gain 0.1" } && made.effects == std::vector<std::string> { "wet 0.1" });
    const auto* firstInstrument = state->instruments[0].get();
    const auto* firstEffect = state->effects[1][0].get();

    // the same plugins in the same places, saved with other settings: new instances, created with those settings
    state = lynx::RackState::build (project ("gain 0.4", "wet 0.4"), state.get(), 48000.0, f);
    CHECK (made.instruments == (std::vector<std::string> { "gain 0.1", "gain 0.4" }));
    CHECK (made.effects == (std::vector<std::string> { "wet 0.1", "wet 0.4" }));
    CHECK (state->instruments[0].get() != firstInstrument && state->effects[1][0].get() != firstEffect);

    // only one of the two differs: only that one is created again
    state = lynx::RackState::build (project ("gain 0.4", "wet 0.9"), state.get(), 48000.0, f);
    CHECK (made.instruments.size() == 2 && made.effects.size() == 3 && made.effects.back() == "wet 0.9");
    state = lynx::RackState::build (project ("gain 0.9", "wet 0.9"), state.get(), 48000.0, f);
    CHECK (made.instruments.size() == 3 && made.instruments.back() == "gain 0.9" && made.effects.size() == 3);

    // a project with no saved settings for the plugin (its defaults) over one that has them
    state = lynx::RackState::build (project ({}, {}), state.get(), 48000.0, f);
    CHECK (made.instruments.size() == 4 && made.instruments.back().empty() && made.effects.size() == 4 && made.effects.back().empty());
}

TEST (pluginstate190az_the_same_saved_settings_keep_the_running_plugins)
{
    using namespace pluginstate190az;
    Made made;
    const auto f = made.factories();
    auto state = lynx::RackState::build (project ("gain 0.1", "wet 0.1"), nullptr, 48000.0, f);
    const auto* instrument = state->instruments[0].get();
    const auto* effect = state->effects[1][0].get();
    // the song arrives as new channels and slots with what the running plugins were created from (a session's
    // snapshot, the same file again): nothing is created
    state = lynx::RackState::build (project ("gain 0.1", "wet 0.1"), state.get(), 48000.0, f);
    CHECK (made.instruments.size() == 1 && made.effects.size() == 1);
    CHECK (state->instruments[0].get() == instrument && state->effects[1][0].get() == effect);
}

TEST (pluginstate190az_a_channel_and_a_slot_keep_their_plugin_whatever_the_song_saved)
{
    using namespace pluginstate190az;
    Made made;
    const auto f = made.factories();
    auto song = project ("saved first", "saved first");
    auto state = lynx::RackState::build (song, nullptr, 48000.0, f);
    const auto* instrument = state->instruments[0].get();
    const auto* effect = state->effects[1][0].get();

    // the settings were captured for a save, then an undo brings back the song with the older ones: the same channel
    // and slot (copies keep their instance), so the running plugins and what was set in their windows stay
    auto saved = song;
    saved.channels[0].plugin.state = "saved later";
    saved.mixer[1].effects[0].plugin.state = "saved later";
    state = lynx::RackState::build (saved, state.get(), 48000.0, f);
    state = lynx::RackState::build (song, state.get(), 48000.0, f);
    CHECK (made.instruments.size() == 1 && made.effects.size() == 1);
    CHECK (state->instruments[0].get() == instrument && state->effects[1][0].get() == effect);

    // also when they moved in their lists in the same edit (issue #32)
    auto moved = saved;
    moved.channels.insert (moved.channels.begin(), lynx::Song::makeDefault().channels[1]);
    lynx::EffectSlot eq;
    eq.kind = lynx::EffectKind::eq;
    moved.mixer[1].effects.insert (moved.mixer[1].effects.begin(), eq);
    state = lynx::RackState::build (moved, state.get(), 48000.0, f);
    CHECK (made.instruments.size() == 1 && made.effects.size() == 1);
    CHECK (state->instruments[1].get() == instrument && state->effects[1][1].get() == effect);
}
