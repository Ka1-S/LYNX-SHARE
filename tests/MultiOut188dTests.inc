// Round 188d (issue #114): FL's wrapper Processing › Connections — a hosted generator's outputs to mixer inserts.

namespace multiout188d
{
// A two-output "plugin" as engine/PluginInstrument routes it: output 1 = 0.25, output 2 = 0.5 (DC), each to the
// channel's insert + its offset (offset 0: the channel's own audio), silent when inactive, the channel's gains on both
struct TwoOutputs final : lynx::Instrument
{
    int configured = 0, track = 0;
    bool processInactive = true;
    lynx::PluginOutputRoute routes[2];
    std::vector<float> aux = std::vector<float> (4096, 0.0f);
    int cursor = 0;
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void configure (const lynx::ChannelDef& c, double) noexcept override
    {
        ++configured;
        track = c.mixerTrack;
        processInactive = c.plugin.processInactive;
        for (int k = 0; k < 2; ++k)
            routes[k] = c.plugin.outputRoute (k);
    }
    void beginChunk (int n) noexcept override
    {
        cursor = 0;
        std::fill (aux.begin(), aux.begin() + n, 0.0f);
    }
    void render (float* l, float* r, int n, float gl, float gr) noexcept override
    {
        const float level[2] = { 0.25f, 0.5f };
        for (int k = 0; k < 2; ++k)
            if (routes[k].isActive (true) || processInactive)
                for (int i = 0; i < n; ++i)
                {
                    if (routes[k].offset == 0)
                    {
                        l[i] += level[k] * gl;
                        r[i] += level[k] * gr;
                    }
                    else if (k == 1)
                        aux[(std::size_t) (cursor + i)] += level[k] * gl;
                }
        cursor += n;
    }
    int auxOutputCount() const noexcept override { return (routes[1].isActive (true) || processInactive) && routes[1].offset != 0 ? 1 : 0; }
    int auxOutputTrack (int) const noexcept override { return track + routes[1].offset; }
    const float* auxOutputData (int, int) const noexcept override { return aux.data(); }
};

// the master's level with the channel's own insert (1) muted or not
float master (const lynx::Song& song, std::shared_ptr<TwoOutputs> fake, const lynx::RackState* previous = nullptr,
              std::unique_ptr<const lynx::RackState>* keep = nullptr)
{
    lynx::RackState::Factories f;
    f.pluginInstruments = [&] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Instrument> { return fake; };
    auto state = lynx::RackState::build (song, previous, 48000.0, f);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256);
    for (int b = 0; b < 4; ++b)
        r.process (state.get(), true, 0.0, 256, l.data(), rr.data(), nullptr);
    if (keep != nullptr)
        *keep = std::move (state);
    return std::abs (l.back()) + std::abs (rr.back());
}

lynx::Song song (std::vector<lynx::PluginOutputRoute> routes, bool muteOwnInsert, bool processInactive = false)
{
    auto s = lynx::Song::makeDefault();
    s.channels.clear();
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::plugin;
    c.plugin.id = "VST-Drums-1";
    c.mixerTrack = 1;
    c.plugin.outputs = std::move (routes);
    c.plugin.processInactive = processInactive;
    s.channels.push_back (c);
    if (s.mixer.size() < 4)
        s.mixer.resize (4);
    s.mixer[1].muted = muteOwnInsert;
    return s;
}
} // namespace multiout188d

TEST (multiout188d_connections_auto_map_and_reset_as_fl)
{
    // FL [EXEC, fl188d a33/b23]: Auto map from output 1's offset, one more per output, inactive ones included
    std::vector<lynx::PluginOutputRoute> r;
    lynx::autoMapOutputs (r, 8);
    CHECK (r.size() == 8 && r[0].offset == 0 && r[1].offset == 1 && r[7].offset == 7);
    r[2].active = 0;
    r[0].offset = 3;
    lynx::autoMapOutputs (r, 8);
    CHECK (r[0].offset == 3 && r[2].offset == 5 && ! r[2].isActive (true) && r[7].offset == 10);
    // Reset: offsets to "---" (0), the LEDs stay; what is left at the defaults is not stored
    lynx::resetOutputOffsets (r, 8);
    CHECK (r.size() == 3 && r[2].offset == 0 && ! r[2].isActive (true));
    r[2].active = -1;
    lynx::trimOutputRoutes (r);
    CHECK (r.empty());
    // the route of an output past the stored ones: the channel's insert, active as the plugin declares it
    lynx::PluginRef ref;
    CHECK (ref.outputRoute (5).isActive (true) && ! ref.outputRoute (5).isActive (false) && ref.outputRoute (5).offset == 0);
    auto other = ref;
    other.outputs = { { 0, -1 }, { 2, -1 } };
    CHECK (! (ref == other));
    CHECK (lynx::outputTargetTrack (4, { 2, 1 }) == 6 && lynx::outputTargetTrack (4, { 2, 0 }) == -1);
    CHECK (lynx::outputTargetTrack (4, { 2, -1 }, false) == -1 && lynx::outputTargetTrack (4, { 2, 1 }, false) == 6);
}

TEST (multiout188d_outputs_reach_their_inserts)
{
    using namespace multiout188d;
    // by default every output plays on the channel's insert: muting it silences them all
    auto fake = std::make_shared<TwoOutputs>();
    CHECK (master (song ({}, false), fake) > 0.1f);
    CHECK (fake->configured > 0); // a new plugin instrument takes its routes when it is built
    CHECK (master (song ({}, true), fake) == 0.0f);
    // output 2 auto-mapped (offset 1): it plays on insert 2, past the muted insert 1
    CHECK (master (song ({ { 0, 1 }, { 1, 1 } }, true), fake) > 0.1f);
    // inactive: silent anywhere
    CHECK (master (song ({ { 0, 1 }, { 1, 0 } }, true), fake) == 0.0f);
    CHECK (master (song ({ { 0, 1 }, { 0, 0 } }, false), fake) > 0.1f); // (output 1 still plays)
    // "Process inactive inputs and outputs" on (FL's default) [EXEC fl188d k33]: the inactive output plays on its insert
    CHECK (master (song ({ { 0, 1 }, { 1, 0 } }, true, true), fake) > 0.1f);

    // the same running instrument takes new routes (no new instance: the reuse path configures it)
    std::unique_ptr<const lynx::RackState> first;
    auto a = song ({}, true);
    master (a, fake, nullptr, &first);
    const auto before = fake->configured;
    a.channels[0].plugin.outputs = { { 0, 1 }, { 1, 1 } };
    CHECK (master (a, fake, first.get()) > 0.1f);
    CHECK (fake->configured > before && fake->routes[1].offset == 1);
}

TEST (multiout188d_flp_connections_records)
{
    // FL 2026's records [DATA: mo-a.flp, LYNX Test Drums 2 after Auto map, Clap off, Hat at -2; flags without 0x8 =
    // "Process inactive inputs and outputs" off]
    lynx::ForeignPlugin made;
    made.format = lynx::ForeignPlugin::Format::vst2;
    made.vst2Id = 0x4C584433;
    made.name = "LYNX Test Drums 2";
    lynx::PluginRef ref;
    ref.outputs = { { 0, -1 }, { 1, -1 }, { 2, 0 }, { -2, -1 }, { 4, -1 } };
    ref.processInactive = false;
    const auto blob = lynx::flpexport::writeWrapper (made, true, &ref);
    lynx::ForeignPlugin back;
    CHECK (lynx::parseFlWrapper (blob, back));
    CHECK (back.hasRoutes && back.inputRoutes.empty() && back.outputRoutes.size() == 5);
    CHECK (back.outputRoutes[2].offset == 2 && back.outputRoutes[2].active == 0 && back.outputRoutes[3].offset == -2 && back.outputRoutes[1].active == 1);
    CHECK ((back.wrapperFlags & 0x8u) == 0);
    lynx::PluginRef imported;
    lynx::applyFlWrapperOptions (back, imported);
    CHECK (! imported.processInactive && imported.outputs.size() == 5 && imported.outputs[3].offset == -2 && ! imported.outputs[2].isActive (true));

    // the bytes FL wrote: 0x1E {0 inputs, 8 outputs}, 0x20 = 8 x {i32 offset, u32 active, u32 0}
    std::vector<std::uint8_t> fl { 12, 0, 0, 0 };
    auto record = [&fl] (std::uint32_t id, std::vector<std::uint32_t> words) {
        const auto n = (std::uint64_t) words.size() * 4;
        for (int k = 0; k < 4; ++k) fl.push_back ((std::uint8_t) (id >> (8 * k)));
        for (int k = 0; k < 8; ++k) fl.push_back ((std::uint8_t) (n >> (8 * k)));
        for (const auto w : words)
            for (int k = 0; k < 4; ++k) fl.push_back ((std::uint8_t) (w >> (8 * k)));
    };
    record (0x02, {}); // (an empty options record: ignored)
    record (0x1E, { 0, 8, 0, 0 });
    record (0x20, { 0, 1, 0, 1, 1, 0, 2, 0, 0, 0xFFFFFFFEu, 1, 0, 4, 1, 0, 5, 1, 0, 6, 1, 0, 7, 1, 0 });
    record (0x32, { 4, 0, 0, 0 });
    record (0x33, { 0x4C584433 });
    lynx::ForeignPlugin parsed;
    CHECK (lynx::parseFlWrapper (fl, parsed));
    CHECK (parsed.outputRoutes.size() == 8 && parsed.outputRoutes[3].offset == -2 && parsed.outputRoutes[2].active == 0 && parsed.outputRoutes[7].offset == 7);

    // an effect's inputs (sidechain indexes) travel too [DATA: mo-c.flp]
    lynx::PluginRef fx;
    fx.inputs = { { 0, 1 }, { 1, 1 } };
    fx.outputs = { { 0, 1 }, { 1, 1 } };
    lynx::ForeignPlugin fxBack;
    CHECK (lynx::parseFlWrapper (lynx::flpexport::writeWrapper (made, false, &fx), fxBack));
    CHECK (fxBack.inputRoutes.size() == 2 && fxBack.inputRoutes[1].offset == 1 && fxBack.outputRoutes.size() == 2);
    // without a mapping: one input and one output, as before
    lynx::ForeignPlugin plain;
    CHECK (lynx::parseFlWrapper (lynx::flpexport::writeWrapper (made, true, nullptr), plain) && plain.outputRoutes.size() == 1);
}
