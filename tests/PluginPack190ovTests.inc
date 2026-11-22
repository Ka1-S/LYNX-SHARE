// Round 190ov (0.1.31): LYNX's later own engines in the plugin pack for other DAWs (docs/PLUGINS-PACK.md)

TEST (pluginpack190ov_later_own_engines_are_in_the_pack)
{
    using namespace lynx;
    // round 190ov (Discord: LYNX's own effects and instruments for other DAWs): the later LYNX-own engines are in the CLAP,
    // LYNX Model also as VST3 / LV2; the engines whose export names wait for the owner are not
    std::map<std::string, const plugexport::Entry*> byName;
    for (const auto& e : plugexport::catalog)
        byName[e.name] = &e;
    const std::pair<const char*, EffectKind> effects[] = { { "LYNX Strike Shaper", EffectKind::strikeShaper }, { "LYNX Anvil Bus", EffectKind::anvilBus },
                                                           { "LYNX Corrode", EffectKind::corrode }, { "LYNX Soundboard", EffectKind::soundboard },
                                                           { "LYNX Platter", EffectKind::platter }, { "LYNX Timeprism", EffectKind::timeprism },
                                                           { "LYNX Harmonia", EffectKind::harmonia } };
    for (const auto& [name, kind] : effects)
        CHECK (byName.count (name) == 1 && ! byName[name]->instrument && byName[name]->effect == kind);
    CHECK (byName.count ("LYNX Model") == 1 && byName["LYNX Model"]->instrument && byName["LYNX Model"]->synth == InstrumentKind::lynxModel);
    for (const auto& e : plugexport::catalog)
        CHECK (! e.instrument || (e.synth != InstrumentKind::tetra && e.synth != InstrumentKind::helix && e.synth != InstrumentKind::braid));
    // the clap ids keep the "com.ka1-s.lynx." prefix the LV2 URIs are made from (cmake/LynxPlugins.cmake)
    for (const auto& e : plugexport::catalog)
        CHECK (std::string (e.clapId).rfind ("com.ka1-s.lynx.", 0) == 0 && std::string (e.clapId).size() > 15);
}
