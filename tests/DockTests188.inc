// Round 188a (issue #116, slice 3): FL's General › "Detach all new plugins"

TEST (detach_new_plugins_marks_only_the_plugins_an_edit_added)
{
    Song song;
    ChannelDef loaded;
    loaded.instrument = InstrumentKind::plugin;
    loaded.plugin.id = "VST3-Synth-1";
    loaded.plugin.state = "c3RhdGU="; // loaded with the project: has a saved state
    song.channels.push_back (loaded);
    song.mixer = Song::defaultMixer (2);
    const auto before = pluginIdCounts (song);
    CHECK (before.at ("VST3-Synth-1") == 1);

    // an edit adds the same synth again (fresh: no state), a new effect, and a clone of the loaded one (with state)
    ChannelDef fresh;
    fresh.instrument = InstrumentKind::plugin;
    fresh.plugin.id = "VST3-Synth-1";
    song.channels.push_back (fresh);
    EffectSlot fx;
    fx.kind = EffectKind::plugin;
    fx.plugin.id = "VST3-Delay-2";
    song.mixer[1].effects.push_back (fx);
    ChannelDef builtIn; // not a hosted plugin: never marked
    builtIn.instrument = InstrumentKind::sampler;
    song.channels.push_back (builtIn);

    CHECK (detachNewPlugins (before, song) == 2);
    CHECK (! song.channels[0].plugin.detached); // the loaded one keeps its flag
    CHECK (song.channels[1].plugin.detached);
    CHECK (song.mixer[1].effects[0].plugin.detached);
    CHECK (! song.channels[2].plugin.detached);

    // nothing added: nothing marked
    const auto now = pluginIdCounts (song);
    song.channels[1].plugin.detached = false;
    CHECK (detachNewPlugins (now, song) == 0);
    CHECK (! song.channels[1].plugin.detached);

    // a clone of a plugin with a saved state is not new (FL: the clone follows its source)
    const auto beforeClone = pluginIdCounts (song);
    song.channels.push_back (loaded);
    CHECK (detachNewPlugins (beforeClone, song) == 0);
}
