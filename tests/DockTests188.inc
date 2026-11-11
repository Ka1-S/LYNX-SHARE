// Round 188a (issue #116, slice 3): FL's General › "Detach all new plugins"

TEST (detach_new_plugins_marks_only_the_plugins_an_edit_added)
{
    using namespace lynx;
    Song song;
    ChannelDef loaded;
    loaded.instrument = InstrumentKind::plugin;
    loaded.plugin.id = "VST3-Synth-1";
    loaded.plugin.state = "c3RhdGU="; // loaded with the project: has a saved state
    song.channels.push_back (loaded);
    song.mixer = Song::defaultMixer (2);
    const auto before = freshPluginCounts (song);
    CHECK (before.count ("VST3-Synth-1") == 0); // a plugin with a saved state is not fresh

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
    CHECK (detachNewPlugins (freshPluginCounts (song), song) == 0);

    // the user docks the new synth again: it stays docked through later edits
    song.channels[1].plugin.detached = false;
    const auto now = freshPluginCounts (song);
    CHECK (now.at ("VST3-Synth-1") == 1);
    CHECK (detachNewPlugins (now, song) == 0);
    CHECK (! song.channels[1].plugin.detached);

    // a clone of a plugin with a saved state is not new (FL: the clone follows its source), and it does not get the
    // docked-again fresh synth of the same id marked in its place
    const auto beforeClone = freshPluginCounts (song);
    song.channels.push_back (loaded);
    CHECK (detachNewPlugins (beforeClone, song) == 0);
    CHECK (! song.channels[1].plugin.detached);
    CHECK (! song.channels[3].plugin.detached);

    // another fresh one of the same id next to the docked-again one: only the new (last) one is marked
    const auto beforeSecond = freshPluginCounts (song);
    song.channels.push_back (fresh);
    CHECK (detachNewPlugins (beforeSecond, song) == 1);
    CHECK (! song.channels[1].plugin.detached);
    CHECK (song.channels[4].plugin.detached);
}

// A plugin imported from an .flp with FL's Detached flag off (issue #116, round 190i) is loaded, not new: turning
// "Detach all new plugins" on never flips it, and one imported Detached stays Detached.
TEST (detach_new_plugins_leaves_loaded_detached_flags_alone)
{
    using namespace lynx;
    Song song;
    ChannelDef docked;
    docked.instrument = InstrumentKind::plugin;
    docked.plugin.id = "VST3-Synth-1";
    docked.plugin.state = "c3RhdGU=";
    auto floating = docked;
    floating.plugin.detached = true;
    song.channels = { docked, floating };
    song.mixer = Song::defaultMixer (2);

    const auto before = freshPluginCounts (song);
    CHECK (before.empty());
    auto copy = song;
    CHECK (detachNewPlugins (before, copy) == 0);
    CHECK (! copy.channels[0].plugin.detached);
    CHECK (copy.channels[1].plugin.detached);

    // pasting both again (clones keep their state and their flag)
    copy.channels.push_back (docked);
    copy.channels.push_back (floating);
    CHECK (detachNewPlugins (before, copy) == 0);
    CHECK (! copy.channels[2].plugin.detached);
    CHECK (copy.channels[3].plugin.detached);
}

// Round 190i (issue #116): FL's Tools › Macros › "Switch all plugin editors to" › Attached / Detached
TEST (switch_all_plugin_editors_sets_every_hosted_plugins_detached_flag)
{
    using namespace lynx;
    Song song;
    ChannelDef synth;
    synth.instrument = InstrumentKind::plugin;
    synth.plugin.id = "VST3-Synth-1";
    song.channels.push_back (synth);
    synth.plugin.detached = true; // (one already detached)
    song.channels.push_back (synth);
    ChannelDef builtIn;
    builtIn.instrument = InstrumentKind::sampler;
    song.channels.push_back (builtIn);
    song.mixer = Song::defaultMixer (2);
    EffectSlot fx;
    fx.kind = EffectKind::plugin;
    fx.plugin.id = "VST3-Delay-2";
    song.mixer[1].effects.push_back (fx);

    CHECK (setAllPluginsDetached (song, true) == 2);
    CHECK (song.channels[0].plugin.detached && song.channels[1].plugin.detached && song.mixer[1].effects[0].plugin.detached);
    CHECK (! song.channels[2].plugin.detached); // not a hosted plugin
    CHECK (setAllPluginsDetached (song, true) == 0);
    CHECK (setAllPluginsDetached (song, false) == 3);
    CHECK (! song.channels[0].plugin.detached && ! song.channels[1].plugin.detached && ! song.mixer[1].effects[0].plugin.detached);
}
