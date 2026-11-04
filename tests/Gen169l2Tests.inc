// Round 169l2: every new generator is FL's Plugin database state when FL is installed (REA_rodada169l2)

namespace gen169l2
{
// the channel's sound data, as the engines keep it
inline bool sameSound (const lynx::ChannelDef& a, const lynx::ChannelDef& b)
{
    return a.instrument == b.instrument && a.pluginState == b.pluginState && a.modelParams == b.modelParams
           && a.samplePath == b.samplePath && a.zones.size() == b.zones.size();
}
} // namespace gen169l2

// The engines whose Default preset differs from FL's Plugin database state (user decision, round 169l2): a new one is
// the database entry (FL's picker / "+" load it), not the Default preset
TEST (gen169l2_new_generators_are_the_plugin_database_entry)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    for (const auto kind : { InstrumentKind::threeosc, InstrumentKind::sytrus, InstrumentKind::harmor, InstrumentKind::harmless,
                             InstrumentKind::poizone, InstrumentKind::minisynth, InstrumentKind::sawer, InstrumentKind::flkeys,
                             InstrumentKind::granulizer, InstrumentKind::toxic, InstrumentKind::beepmap })
    {
        const auto* e = natives::instrumentEngine (kind);
        CHECK (e != nullptr);
        if (e == nullptr)
            continue;
        const auto c = natives::presetCandidates (e->presetFolder, true, fl);
        const auto first = c.empty() ? std::string() : c.front();
        const auto fromDb = first.find ("Plugin database") != std::string::npos
                            && std::filesystem::path (first).filename().string() == std::string (e->presetFolder) + ".fst";
        if (! fromDb)
            std::printf ("  %s starts from %s\n", e->name, first.c_str());
        CHECK (fromDb);
        // the Default preset still follows it (a broken database entry falls back to it)
        CHECK (c.size() > 1 && c[1].find ("Plugin presets") != std::string::npos
               && std::filesystem::path (c[1]).stem().string() == "Default");
        const auto made = natives::makeChannel (kind, fl);
        const auto fromDbFile = natives::channelFromPresetFile (kind, first, fl);
        const auto fromDefault = c.size() > 1 ? natives::channelFromPresetFile (kind, c[1], fl) : std::nullopt;
        CHECK (made.has_value() && fromDbFile.has_value());
        if (made && fromDbFile)
        {
            if (! gen169l2::sameSound (*made, *fromDbFile))
                std::printf ("  %s: the new channel is not the database state\n", e->name);
            CHECK (gen169l2::sameSound (*made, *fromDbFile));
            CHECK (! made->pluginState.empty() || ! made->modelParams.empty());
            // ... which is not the Default preset (FL's files differ: REA_rodada169l §3)
            if (fromDefault)
                CHECK (! gen169l2::sameSound (*made, *fromDefault));
        }
    }
    // where the Default preset IS the database state, a new channel sounds the same as before (Kepler, DX10, SimSynth)
    for (const auto kind : { InstrumentKind::kepler, InstrumentKind::dx10, InstrumentKind::simsynth })
    {
        const auto* e = natives::instrumentEngine (kind);
        if (e == nullptr)
            continue;
        const auto c = natives::presetCandidates (e->presetFolder, true, fl);
        CHECK (c.size() > 1);
        if (c.size() < 2)
            continue;
        const auto a = natives::channelFromPresetFile (kind, c[0], fl), b = natives::channelFromPresetFile (kind, c[1], fl);
        CHECK (a.has_value() && b.has_value() && gen169l2::sameSound (*a, *b));
    }
}

// Without FL every generator keeps LYNX's bank default (round 168n), and an engine whose database entry LYNX can't
// read falls through to its Default preset / folder (Wave Traveller)
TEST (gen169l2_without_fl_the_lynx_bank_and_unreadable_entries_fall_through)
{
    using namespace lynx;
    natives::FlAccess none;
    for (const auto kind : { InstrumentKind::sytrus, InstrumentKind::harmor, InstrumentKind::poizone, InstrumentKind::toxic })
    {
        CHECK (natives::presetCandidates (natives::presetFolderOf (kind), true, none).empty());
        const auto made = natives::makeChannel (kind, none);
        const auto bank = banks::defaultChannel (kind, natives::instrumentEngine (kind)->name);
        CHECK (made.has_value() == bank.has_value());
        if (made && bank)
            CHECK (gen169l2::sameSound (*made, *bank));
    }
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    const auto c = natives::presetCandidates ("Wave Traveller", true, fl);
    CHECK (! c.empty() && c.front().find ("Plugin database") != std::string::npos);
    CHECK (natives::makeChannel (InstrumentKind::wavetraveller, fl).has_value());
}
