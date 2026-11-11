// Round 190ao (pain point 12, LYNX's own: FL's wrapper has no velocity setting): a hosted instrument's own velocity
// curve or fixed velocity for the notes played into it. core/Preferences.h mapPluginVelocity; PluginRef::velocityMode.

TEST (velocity190ao_the_default_follows_the_midi_settings_exactly)
{
    using namespace lynx::prefs;
    lynx::PluginRef ref; // a new plugin, and a project saved before the setting existed
    CHECK (ref.velocityMode == pluginVelocityFollow && ref.fixedVelocity == defaultFixedVelocity);
    for (const auto global : { VelocityCurve::linear, VelocityCurve::soft, VelocityCurve::hard, VelocityCurve::fixed })
        for (const auto v : { 0.0f, 0.01f, 0.25f, 0.5f, 0.8f, 1.0f })
            CHECK (mapPluginVelocity (v, ref.velocityMode, ref.fixedVelocity, global) == mapVelocity (v, global));
}

TEST (velocity190ao_a_plugin_curve_replaces_the_midi_settings_curve)
{
    using namespace lynx::prefs;
    // whatever the MIDI settings say, the plugin's own curve is the one applied (once, not on top of the other)
    for (const auto global : { VelocityCurve::linear, VelocityCurve::soft, VelocityCurve::hard, VelocityCurve::fixed })
    {
        CHECK (mapPluginVelocity (0.5f, 1, 100, global) == 0.5f);                                       // as played
        CHECK (mapPluginVelocity (0.5f, 2, 100, global) == mapVelocity (0.5f, VelocityCurve::soft));
        CHECK (mapPluginVelocity (0.5f, 3, 100, global) == mapVelocity (0.5f, VelocityCurve::hard));
    }
    CHECK (mapPluginVelocity (0.5f, 2, 100, VelocityCurve::linear) > 0.5f);
    CHECK (mapPluginVelocity (0.5f, 3, 100, VelocityCurve::linear) < 0.5f);
}

TEST (velocity190ao_fixed_velocity_is_the_plugins_own_value)
{
    using namespace lynx::prefs;
    CHECK (pluginVelocityFixed == 4);
    for (const auto v : { 0.01f, 0.4f, 1.0f })
    {
        CHECK (std::abs (mapPluginVelocity (v, pluginVelocityFixed, 100, VelocityCurve::linear) - 100.0f / 127.0f) < 1e-6f);
        CHECK (std::abs (mapPluginVelocity (v, pluginVelocityFixed, 64, VelocityCurve::hard) - 64.0f / 127.0f) < 1e-6f);
        CHECK (mapPluginVelocity (v, pluginVelocityFixed, 127, VelocityCurve::soft) == 1.0f);
    }
    // a note-off stays a note-off; out-of-range values are brought into range
    CHECK (mapPluginVelocity (0.0f, pluginVelocityFixed, 100, VelocityCurve::linear) == 0.0f);
    CHECK (std::abs (mapPluginVelocity (0.5f, pluginVelocityFixed, 0, VelocityCurve::linear) - 1.0f / 127.0f) < 1e-6f);
    CHECK (mapPluginVelocity (0.5f, pluginVelocityFixed, 500, VelocityCurve::linear) == 1.0f);
    CHECK (clampPluginVelocityMode (-3) == pluginVelocityFollow && clampPluginVelocityMode (9) == pluginVelocityFollow);
    CHECK (mapPluginVelocity (0.5f, 9, 100, VelocityCurve::soft) == mapVelocity (0.5f, VelocityCurve::soft)); // unknown mode: follow
}

TEST (velocity190ao_the_setting_is_part_of_the_plugin_and_never_recreates_it)
{
    lynx::PluginRef a, b;
    b.velocityMode = lynx::prefs::pluginVelocityFixed;
    CHECK (! (a == b));
    b = a;
    b.fixedVelocity = 64;
    CHECK (! (a == b));
    // changing it keeps the running plugin (the rack build compares the processing options only)
    lynx::Song song;
    song.mixer = lynx::Song::defaultMixer (2);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::plugin;
    c.plugin.id = "synth";
    song.channels.push_back (c);
    song.patterns.emplace_back();
    song.uniqueInstances();
    int made = 0;
    lynx::RackState::Factories f;
    f.pluginInstruments = [&made] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Instrument> {
        ++made;
        return std::make_shared<reload190ao::FakeSynth> (std::string());
    };
    const auto first = lynx::RackState::build (song, nullptr, 48000.0, f);
    auto shape = reload190ao::shapeOf (*first);
    song.channels[0].plugin.velocityMode = lynx::prefs::pluginVelocityFixed;
    song.channels[0].plugin.fixedVelocity = 90;
    const auto second = lynx::RackState::build (song, &shape, 48000.0, f);
    CHECK (made == 1);
    CHECK (second->instruments[0] == first->instruments[0]);
}
