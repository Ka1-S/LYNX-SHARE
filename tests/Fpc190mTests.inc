// Round 190m: FPC's state codec (issue #135) — read and written byte for byte (docs/analise/REA_rodada9.md §1)

namespace fpc190m
{
inline void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back ((std::uint8_t) (v >> (8 * i)));
}

// one chunk with FL's 64-bit size
inline void chunk (std::vector<std::uint8_t>& out, std::uint32_t id, const std::vector<std::uint8_t>& body)
{
    put32 (out, id);
    put32 (out, (std::uint32_t) body.size());
    put32 (out, 0);
    out.insert (out.end(), body.begin(), body.end());
}

// every FPC state in a folder of FL's (.fst kits and .flp projects)
inline std::vector<std::pair<std::string, std::vector<std::uint8_t>>> statesIn (const std::filesystem::path& folder)
{
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> out;
    std::error_code error;
    if (! std::filesystem::is_directory (folder, error))
        return out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (folder, std::filesystem::directory_options::skip_permission_denied, error))
    {
        const auto ext = entry.path().extension();
        if (ext != ".fst" && ext != ".flp")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
        try
        {
            for (const auto& c : flp::parseProject (file, ext == ".fst").channels)
                if (c.pluginName == "FPC" && ! c.pluginState.empty())
                    out.emplace_back (entry.path().filename().string(), c.pluginState);
        }
        catch (const std::exception&)
        {
        }
    }
    return out;
}
} // namespace fpc190m

TEST (fpc190m_empty_kit_reads_back_as_written)
{
    using namespace lynx;
    const auto kit = fpc::emptyKit();
    CHECK (kit.pads.size() == 32);
    const auto bytes = fpc::write (kit);
    const auto back = fpc::read (bytes);
    CHECK (back.has_value());
    if (! back)
        return;
    CHECK (*back == kit);
    CHECK (fpc::write (*back) == bytes);
    CHECK (back->pads[1].note == 36 && back->pads[16].note == 60);
    CHECK (back->pads[0].layers.size() == 1 && back->pads[0].envelopes.size() == 2);
    CHECK (back->pads[0].envelopes[0].points.size() == 4 && back->pads[0].envelopes[1].points.size() == 1);
    CHECK (! fpcPads (bytes).has_value()); // no pad has a sample
}

TEST (fpc190m_an_edit_is_what_the_state_carries)
{
    using namespace lynx;
    auto kit = fpc::emptyKit();
    auto& pad = kit.pads[1];
    pad.name = fpc::fromUtf8 ("Kick \xC3\xA9 \xF0\x9F\x98\x80");
    pad.volume = 128;
    pad.pan = -24;
    pad.cut = 0;
    pad.cutBy = 3;
    pad.tune = 64;
    pad.output = 2;
    pad.flags = 8;
    pad.trigger[0] = (std::int32_t) fpc::LayerMode::cycle;
    pad.layers[0].path = u"%FLStudioFactoryData%\\Data\\Patches\\Packs\\Drums\\Kicks\\a.wav";
    pad.layers[0].velocityHigh = 64;
    fpc::Layer second;
    second.path = u"C:\\b.wav";
    second.velocityLow = 64;
    second.volume = 99;
    second.tune = -12;
    pad.layers.push_back (second);
    pad.envelopes[0].points[1].time = 0.125;
    const auto back = fpc::read (fpc::write (kit));
    CHECK (back.has_value());
    if (! back)
        return;
    CHECK (*back == kit);
    CHECK (fpc::toUtf8 (back->pads[1].name) == "Kick \xC3\xA9 \xF0\x9F\x98\x80");
    CHECK (back->pads[1].oneLayerAtATime() && back->pads[1].mode() == fpc::LayerMode::cycle);
    // the importer's view of the same state: one pad, two velocity zones
    const auto pads = fpcPads (fpc::write (kit));
    CHECK (pads.has_value());
    if (! pads)
        return;
    CHECK (pads->size() == 1);
    const auto& p = pads->front();
    CHECK (p.key == 36 && p.cutGroup == 1 && p.cutBy == 4 && p.mixerOffset == 2 && p.layers.size() == 2);
    CHECK (std::abs (p.tune - 6.0f) < 1e-6f && std::abs (p.pan + 0.1875f) < 1e-6f);
    CHECK (p.layers[0].lowVelocity == 0 && p.layers[0].highVelocity == 63 && p.layers[1].lowVelocity == 64 && p.layers[1].highVelocity == 127);
    CHECK (std::abs (p.layers[1].tune + 1.125f) < 1e-6f);
}

TEST (fpc190m_chunks_lynx_does_not_know_stay_in_place)
{
    using namespace lynx;
    using fpc190m::chunk;
    using fpc190m::put32;
    // a state with a chunk of a later version in each list: the kit, a pad and a layer; and an envelope with another layout
    std::vector<std::uint8_t> layer, pad, state;
    chunk (layer, 0x12C, { 'a', 0, '.', 0, 'w', 0 });
    chunk (layer, 0x777, { 1, 2, 3 });
    std::vector<std::uint8_t> params;
    for (const std::uint32_t v : { 100u, 0u, 0u, 127u, 1u, 0u, 0xFFFFFFFFu, 42u }) // an eighth value
        put32 (params, v);
    chunk (layer, 0x12D, params);
    chunk (layer, 0x12E, {});
    std::vector<std::uint8_t> padParams (64, 0);
    padParams[0] = 99;
    padParams[12] = 38;
    chunk (pad, 0x888, { 9 });
    chunk (pad, 0x64, padParams);
    chunk (pad, 0x65, { 'S', 0 });
    chunk (pad, 0x66, std::vector<std::uint8_t> (32, 0));
    chunk (pad, 0x69, layer);
    chunk (pad, 0x67, { 0, 0, 0, 0, 0, 0, 0, 0 });
    chunk (pad, 0x68, { 5, 0, 0, 0, 6 }); // not the known layout
    chunk (pad, 0x999, {});
    chunk (pad, 0x6A, {});
    put32 (state, fpc::newestVersion);
    chunk (state, 2, pad);
    chunk (state, 1, { 'x', 0 });
    chunk (state, 0x55, { 7, 7 });
    chunk (state, 3, { 1 });
    chunk (state, 4, std::vector<std::uint8_t> (32, 3));
    const auto kit = fpc::read (state);
    CHECK (kit.has_value());
    if (! kit)
        return;
    CHECK (fpc::write (*kit) == state);
    CHECK (kit->pads.size() == 1 && kit->extra.size() == 1 && kit->extra[0].id == 0x55);
    const auto& p = kit->pads[0];
    CHECK (p.note == 38 && p.volume == 99 && p.extra.size() == 2 && p.layers.size() == 1 && p.envelopes.size() == 1);
    CHECK (p.envelopes[0].unparsed.size() == 5 && p.envelopes[0].points.empty());
    CHECK (p.layers[0].volume == 100 && p.layers[0].region == -1 && p.layers[0].moreParams == std::vector<std::int32_t> { 42 });
    CHECK (p.layers[0].extra.size() == 1 && fpc::toUtf8 (p.layers[0].path) == "a.w");
    // an edit keeps them too
    auto edited = *kit;
    edited.pads[0].volume = 64;
    const auto back = fpc::read (fpc::write (edited));
    CHECK (back.has_value() && *back == edited);
}

TEST (fpc190m_versions_and_broken_states)
{
    using namespace lynx;
    auto kit = fpc::emptyKit();
    kit.pads[0].layers[0].path = u"k.wav";
    // the versions in FL's own files: 2,000,100, 2,000,014 and FL Mobile's 1,000,011 (text of one byte per character)
    for (const std::uint32_t version : { 0x1E84E403u, 0x1E848Eu, 0xF424Bu })
    {
        kit.version = version;
        const auto bytes = fpc::write (kit);
        const auto back = fpc::read (bytes);
        CHECK (back.has_value() && *back == kit && fpc::write (*back) == bytes);
    }
    kit.version = fpc::newestVersion;
    const auto wide = fpc::write (kit);
    kit.version = 0xF424B;
    CHECK (fpc::write (kit).size() < wide.size());
    // outside the range, too short, cut off anywhere, any byte changed: no crash, and a cut keeps the pads before it
    auto bad = wide;
    bad[3] = 0x7F;
    CHECK (! fpc::read (bad).has_value());
    CHECK (! fpc::read ({ 1, 2, 3 }).has_value());
    for (std::size_t n = 16; n < wide.size(); n += 37)
    {
        const std::vector<std::uint8_t> cut (wide.begin(), wide.begin() + (std::ptrdiff_t) n);
        const auto part = fpc::read (cut);
        CHECK (part.has_value() && part->pads.size() <= 32);
        if (part)
            fpc::write (*part);
        fpcPads (cut);
    }
    std::uint32_t seed = 190;
    for (int i = 0; i < 400; ++i)
    {
        auto flipped = wide;
        seed = seed * 1664525u + 1013904223u;
        flipped[4 + (seed >> 8) % (flipped.size() - 4)] = (std::uint8_t) (seed >> 24);
        if (const auto got = fpc::read (flipped))
            fpc::write (*got);
        fpcPads (flipped);
    }
    const std::vector<std::uint8_t> head (wide.begin(), wide.begin() + 2000);
    const auto part = fpc::read (head);
    CHECK (part.has_value() && part->pads.size() == 3); // an empty pad is 525 bytes
}

TEST (fpc190m_fl_kits_and_projects_round_trip_byte_for_byte)
{
    using namespace lynx;
    const std::filesystem::path data (realFlpDir);
    auto states = fpc190m::statesIn (data / "Patches" / "Plugin presets" / "Generators" / "FPC");
    const auto kits = states.size();
    for (const auto* folder : { "Demo projects", "System", "Projects" })
        for (auto& s : fpc190m::statesIn (data / folder))
            states.push_back (std::move (s));
    if (states.empty())
        return; // no FL Studio here
    int exact = 0, pads = 0, layers = 0;
    for (const auto& [name, state] : states)
    {
        const auto kit = fpc::read (state);
        CHECK (kit.has_value());
        if (! kit)
            continue;
        const bool same = fpc::write (*kit) == state;
        if (! same)
            std::printf ("  FPC state of %s is not written back as read\n", name.c_str());
        exact += same ? 1 : 0;
        CHECK (kit->pads.size() == 32 && kit->extra.empty());
        for (const auto& pad : kit->pads)
        {
            ++pads;
            layers += (int) pad.layers.size();
            CHECK (pad.extra.empty() && pad.envelopes.size() == 2 && pad.moreParams.empty());
            for (const auto& e : pad.envelopes)
                CHECK (e.unparsed.empty() && ! e.points.empty());
            for (const auto& layer : pad.layers)
                CHECK (layer.extra.empty() && layer.moreParams.empty());
        }
    }
    std::printf ("  FPC: %d states (%d kits) with %d pads and %d layers, %d written back byte for byte\n", (int) states.size(), (int) kits, pads, layers, exact);
    CHECK (exact == (int) states.size());
    CHECK (kits >= 21);
    // FL's Empty kit is LYNX's empty kit
    std::ifstream in (data / "Patches" / "Plugin presets" / "Generators" / "FPC" / "Empty.fst", std::ios::binary);
    const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
    if (file.empty())
        return;
    const auto project = flp::parseProject (file, true);
    CHECK (project.channels.size() == 1);
    if (project.channels.size() == 1)
        CHECK (project.channels[0].pluginState == fpc::write (fpc::emptyKit()));
}
