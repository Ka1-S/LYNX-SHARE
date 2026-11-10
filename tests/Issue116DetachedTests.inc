// Round 190i, issue #116: FL's "Detached" plugin window flag in the .flp (plugin record 0xD4, flags bit 0x4
// [EXEC + DATA FL 2026: docs/analise/REA_rodada190i.md]) <-> PluginRef::detached.

namespace issue116d
{
lynx::ForeignPlugin vst (const char* name, std::uint32_t id)
{
    lynx::ForeignPlugin f;
    f.format = lynx::ForeignPlugin::Format::vst2;
    f.vst2Id = id;
    f.name = name;
    return f;
}

// a hosted instrument on channel 0 and a hosted effect on insert 1, both as imported from FL (their wrapper records)
lynx::Song song (bool channelDetached, bool effectDetached, int channelFlags)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.channels.resize (1);
    auto& c = s.channels[0];
    c.instrument = InstrumentKind::plugin;
    c.plugin.name = "Drums";
    c.plugin.foreign = vst ("Drums", 0x44524D31);
    c.plugin.detached = channelDetached;
    c.flWrapperFlags = channelFlags;
    EffectSlot fx;
    fx.kind = EffectKind::plugin;
    fx.plugin.name = "Echo";
    fx.plugin.foreign = vst ("Echo", 0x4543484F);
    fx.plugin.detached = effectDetached;
    fx.smartDisable = false;
    s.mixer[1].effects.push_back (fx);
    return s;
}

flp::Project written (const lynx::Song& s)
{
    lynx::flpexport::Options options;
    lynx::flpexport::Report report;
    return flp::parseProject (lynx::flpexport::exportFlp (s, options, report));
}

lynx::Song back (const flp::Project& p)
{
    lynx::FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return true; };
    return lynx::importFlp (p, o);
}

// the imported song's channel 0 / insert 1 slot 0 plugin: 1 detached, 0 docked, −1 not there
int channelDetached (const lynx::Song& s)
{
    return s.channels.empty() ? -1 : (int) s.channels[0].plugin.detached;
}
int effectDetached (const lynx::Song& s)
{
    return s.mixer.size() < 2 || s.mixer[1].effects.empty() ? -1 : (int) s.mixer[1].effects[0].plugin.detached;
}
int channelFlags (const flp::Project& p)
{
    return p.channels.empty() ? -1 : p.channels[0].wrapperFlags;
}

int slotFlags (const flp::Project& p)
{
    for (const auto& m : p.mixer)
        if (m.index == 1 && ! m.slots.empty())
            return m.slots[0].wrapperFlags;
    return -1;
}
} // namespace issue116d

TEST (issue116_flp_detached_flag_round_trips)
{
    using namespace issue116d;
    CHECK (lynx::flDetached (0x155) && ! lynx::flDetached (0x151) && ! lynx::flDetached (-1));
    CHECK (lynx::withFlDetached (0x151, true) == 0x155 && lynx::withFlDetached (0x155, false) == 0x151);

    // docked (the default): FL's flags without 0x4 — a generator wrapper 0x150, an effect wrapper 0x140
    auto p = written (song (false, false, -1));
    CHECK (channelFlags (p) == 0x150 && slotFlags (p) == 0x140);
    auto s = back (p);
    CHECK (channelDetached (s) == 0 && effectDetached (s) == 0);

    // detached: bit 0x4 on each plugin's own record, and back
    p = written (song (true, false, -1));
    CHECK (channelFlags (p) == 0x154 && slotFlags (p) == 0x140);
    s = back (p);
    CHECK (channelDetached (s) == 1 && effectDetached (s) == 0);
    p = written (song (false, true, -1));
    CHECK (channelFlags (p) == 0x150 && slotFlags (p) == 0x144);
    s = back (p);
    CHECK (channelDetached (s) == 0 && effectDetached (s) == 1);

    // FL's other flags of an imported channel stay (0x1 visible, 0x20 smart disable); LYNX's Detached decides 0x4:
    // imported detached and docked again in LYNX, or the other way round
    CHECK (channelFlags (written (song (false, false, 0x175))) == 0x171);
    CHECK (channelFlags (written (song (true, false, 0x171))) == 0x175);

    // a channel that is not a hosted plugin keeps the flags it was imported with, 0x4 included (LYNX's own editors
    // have no Detached state to set it from)
    auto sampler = lynx::Song::makeDefault();
    sampler.channels.resize (1);
    sampler.channels[0].instrument = lynx::InstrumentKind::sampler;
    sampler.channels[0].samplePath = "C:/x.wav";
    sampler.channels[0].flWrapperFlags = 0x54;
    CHECK (channelFlags (written (sampler)) == 0x54);
}
