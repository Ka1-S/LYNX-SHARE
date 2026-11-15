// --- REA round 143b: FL's channel window "Misc" tab, Browser menus, hint bar ---

TEST (rea143b_channel_misc_functions)
{
    using namespace lynx;
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    ChannelDef sampler;
    sampler.instrument = InstrumentKind::sampler;
    ChannelDef plugin;
    plugin.instrument = InstrumentKind::plugin;
    // Mono / Porta exist on every generator, a hosted plugin's too (round 168l: FL's channel layer plays them)
    CHECK (channelHasMono (synth) && channelHasMono (sampler) && channelHasMono (plugin));
    CHECK (channelHasPortamento (synth) && channelHasPortamento (sampler) && channelHasPortamento (plugin));
    setChannelMono (synth, true);
    CHECK (channelMono (synth) && synth.synth.mono);
    setChannelMono (plugin, true);
    CHECK (channelMono (plugin));
    setChannelPortamento (sampler, true, 0.5f);
    CHECK (channelPortamento (sampler) && sampler.sampler.porta && std::abs (channelPortamentoBeats (sampler) - 0.5f) < 1e-6f);
    setChannelPortamento (synth, true, 9.0f); // clamped to 4 beats
    CHECK (synth.synth.porta && std::abs (synth.synth.portaBeats - 4.0f) < 1e-6f);
    setChannelPortamento (plugin, true, 1.0f);
    CHECK (channelPortamento (plugin));
    // "Cut self": the channel's own group (1000 + channel) for both Cut and By, as the rack menu's "Cut itself"
    ChannelDef hat;
    setChannelCutsItself (hat, 3, true);
    CHECK (channelCutsItself (hat) && hat.cutGroup == 1003 && hat.cutBy == 1003);
    CHECK (cutGroupText (hat.cutGroup, 3) == "self" && cutGroupText (0, 3) == "--" && cutGroupText (2, 3) == "2");
    setChannelCutsItself (hat, 3, false);
    CHECK (! channelCutsItself (hat) && hat.cutGroup == 0 && hat.cutBy == 0);
}

TEST (rea143b_step_position_like_fl_hint_bar)
{
    using namespace lynx;
    // FL's hint bar over the second step of bar 1: "1:02:00" (bar:step:tick, 24 ticks per step)
    CHECK (stepPositionText (0, lynx::Meter (4, 4)) == "1:01:00");
    CHECK (stepPositionText (ticksPerStep, lynx::Meter (4, 4)) == "1:02:00");
    CHECK (stepPositionText (ticksPerStep * 16, lynx::Meter (4, 4)) == "2:01:00");
    CHECK (stepPositionText (ticksPerStep * 15 + ticksPerStep / 2, lynx::Meter (4, 4)) == "1:16:12");
    CHECK (stepPositionText (ticksPerStep * 12, lynx::Meter (3, 4)) == "2:01:00"); // 3/4: 12 steps per bar
    CHECK (stepPositionText (-5, lynx::Meter (4, 4)) == "1:01:00");
}
