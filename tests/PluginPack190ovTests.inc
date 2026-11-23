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
                                                           { "LYNX Chimefield", EffectKind::harmonia } }; // (rollup 0.1.31 step 6: Harmonia exports as LYNX Chimefield)
    for (const auto& [name, kind] : effects)
        CHECK (byName.count (name) == 1 && ! byName[name]->instrument && byName[name]->effect == kind);
    CHECK (byName.count ("LYNX Model") == 1 && byName["LYNX Model"]->instrument && byName["LYNX Model"]->synth == InstrumentKind::lynxModel);
    // rollup 0.1.31 step 6 (owner decision): Tetra, Helix and Braid under export names of their own; in the CLAP, as VST3 and
    // on Linux as LV2 since round 190pq (their state saves back byte for byte); the engines' own names (other makers'
    // products) are never an export name
    const std::pair<const char*, InstrumentKind> synths[] = { { "LYNX Fourfold", InstrumentKind::tetra }, { "LYNX Wavecoil", InstrumentKind::helix },
                                                              { "LYNX Duoloom", InstrumentKind::braid } };
    for (const auto& [name, kind] : synths)
        CHECK (byName.count (name) == 1 && byName[name]->instrument && byName[name]->synth == kind);
    for (const auto& e : plugexport::catalog)
        for (const char* taken : { "Tetra", "Helix", "Braid", "Harmonia" })
            CHECK (std::string (e.name).find (taken) == std::string::npos);
    // the clap ids keep the "com.ka1-s.lynx." prefix the LV2 URIs are made from (cmake/LynxPlugins.cmake)
    for (const auto& e : plugexport::catalog)
        CHECK (std::string (e.clapId).rfind ("com.ka1-s.lynx.", 0) == 0 && std::string (e.clapId).size() > 15);
}
