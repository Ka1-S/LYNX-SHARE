// Round 171b (issue #65, REA_rodada171b): Patcher parameter (event) inputs and outputs — FL's node menu › Inputs /
// Outputs › Parameters. A Surface knob (or any controller / parameter output) cabled to a parameter input of any node,
// the input published as a Patcher parameter, FL's visible parameter ports imported.

namespace patcher171b
{
// A hosted plugin stand-in: records what reaches its parameters, reports its own values
struct ProbeEffect final : lynx::Effect
{
    std::array<float, 16> values {};
    int calls = 0;
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const lynx::EffectParams&, double) noexcept override {}
    void setParameter (int index, float value) noexcept override
    {
        ++calls;
        if (index >= 0 && index < (int) values.size())
            values[(std::size_t) index] = value;
    }
    float parameterValue (int index) const noexcept override { return index >= 0 && index < (int) values.size() ? values[(std::size_t) index] : -1.0f; }
};

lynx::PatchNode pluginNode (const std::string& name)
{
    lynx::PatchNode n;
    n.effect = lynx::makeEffectSlot (lynx::EffectKind::plugin);
    n.effect.plugin.name = name;
    return n;
}

// builtInFactories, with every hosted plugin a ProbeEffect (kept in `probes`, in creation order)
lynx::patcher::Factories probeFactories (std::vector<std::shared_ptr<ProbeEffect>>& probes)
{
    auto f = lynx::patcher::builtInFactories (48000.0);
    auto builtIn = f.effect;
    f.effect = [&probes, builtIn] (const lynx::EffectSlot& slot) -> std::shared_ptr<lynx::Effect> {
        if (slot.kind != lynx::EffectKind::plugin)
            return builtIn (slot);
        auto p = std::make_shared<ProbeEffect>();
        probes.push_back (p);
        return p;
    };
    return f;
}

lynx::PatchWire control (int from, int fromPort, int to, int toPort)
{
    lynx::PatchWire w;
    w.kind = lynx::PatchWire::Kind::control;
    w.from = from;
    w.fromPort = fromPort;
    w.to = to;
    w.toPort = toPort;
    return w;
}

void block (lynx::patcher::Graph& g, int n = 256)
{
    std::vector<float> l ((std::size_t) n, 0.5f), r ((std::size_t) n, 0.5f);
    g.process (l.data(), r.data(), nullptr, nullptr, l.data(), r.data(), n, 120.0);
}
} // namespace patcher171b

TEST (patcher171b_surface_knob_drives_a_plugin_parameter)
{
    using namespace patcher171b;
    // the issue's case: the Surface's "Knob 1" cabled to parameter input 3 of a plugin node (a Limiter's gain)
    lynx::PatcherDoc doc;
    doc.nodes = { pluginNode ("Limiter") };
    lynx::SurfaceControl knob;
    knob.name = "Knob 1";
    knob.value = 0.25f;
    doc.surface = { knob };
    lynx::patcher::setParamInput (doc, 0, 3, true, "Limiter - Gain");
    CHECK (lynx::patcher::connect (doc, control (lynx::patchSurface, 0, 0, 3)));
    CHECK (lynx::patcher::shownParamInputs (doc, 0) == std::vector<int> { 3 });
    // the active input is a Patcher parameter (FL's record 1100)
    CHECK (doc.published.size() == 1 && doc.published[0].node == 0 && doc.published[0].param == 3 && doc.published[0].name == "Limiter - Gain");

    std::vector<std::shared_ptr<ProbeEffect>> probes;
    lynx::patcher::Graph g (doc, 48000.0, probeFactories (probes));
    g.prepare (48000.0);
    CHECK (probes.size() == 1);
    if (probes.empty())
        return;
    block (g);
    CHECK (probes[0]->values[3] == 0.25f && probes[0]->calls == 1);
    // the same value is not sent again every block (a change in the plugin's own window stays)
    block (g);
    block (g);
    CHECK (probes[0]->calls == 1);
    // the knob moves (the editor hands the graph the new values): the plugin follows in real time, at the next block
    auto moved = doc;
    moved.surface[0].value = 0.8f;
    CHECK (lynx::patcher::sameStructure (doc, moved));
    g.takeValues (moved);
    block (g);
    CHECK (std::abs (probes[0]->values[3] - 0.8f) < 1e-6f && probes[0]->calls == 2);
    // the cable's value map applies (here: inverted)
    auto inverted = moved;
    inverted.wires[0].curve = { 1.0f, 0.0f };
    std::vector<std::shared_ptr<ProbeEffect>> probes2;
    lynx::patcher::Graph g2 (inverted, 48000.0, probeFactories (probes2));
    g2.prepare (48000.0);
    block (g2);
    CHECK (! probes2.empty() && std::abs (probes2[0]->values[3] - 0.2f) < 1e-5f);
    // automating the Patcher's parameter 0 from outside reaches the same plugin parameter
    g.setPublished (0, 0.4f);
    CHECK (std::abs (probes[0]->values[3] - 0.4f) < 1e-6f);
}

TEST (patcher171b_surface_knob_drives_a_built_in_parameter)
{
    using namespace patcher141;
    // a Surface knob on the Utility's gain input: at the bottom the Utility mutes (FL's knob → parameter link)
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::patcher);
    slot.patcher.nodes = { effectNode (lynx::EffectKind::utility) };
    lynx::SurfaceControl knob;
    knob.value = 0.0f;
    slot.patcher.surface = { knob };
    slot.patcher.wires = { audio (lynx::patchFromHost, 0), audio (0, lynx::patchToHost) };
    lynx::patcher::setParamInput (slot.patcher, 0, 0, true);
    lynx::patcher::connect (slot.patcher, patcher171b::control (lynx::patchSurface, 0, 0, 0));
    CHECK (run (slot, 1.0f).first == 0.0f);
    slot.patcher.surface[0].value = 0.5f; // 0 dB
    CHECK (std::abs (run (slot, 1.0f).first - 1.0f) < 1e-5f);
}

TEST (patcher171b_parameter_output_feeds_another_node)
{
    using namespace patcher171b;
    // node 0's parameter 5 (Outputs › Parameters) cabled to node 1's parameter input 2: the value travels each block
    lynx::PatcherDoc doc;
    doc.nodes = { pluginNode ("A"), pluginNode ("B") };
    lynx::patcher::setParamOutput (doc, 0, 5, true);
    lynx::patcher::setParamInput (doc, 1, 2, true);
    CHECK (lynx::patcher::connect (doc, control (0, lynx::patchParamOutput (5), 1, 2)));
    CHECK (lynx::patcher::shownParamOutputs (doc, 0) == std::vector<int> { 5 });
    std::vector<std::shared_ptr<ProbeEffect>> probes;
    lynx::patcher::Graph g (doc, 48000.0, probeFactories (probes));
    g.prepare (48000.0);
    CHECK (probes.size() == 2);
    if (probes.size() != 2)
        return;
    probes[0]->values[5] = 0.7f;
    block (g);
    CHECK (std::abs (probes[1]->values[2] - 0.7f) < 1e-6f);
    probes[0]->values[5] = 0.1f; // the source plugin's own change
    block (g);
    CHECK (std::abs (probes[1]->values[2] - 0.1f) < 1e-6f);
    // a built-in effect's parameter as the source: its value in the song
    lynx::PatcherDoc doc2;
    doc2.nodes = { patcher141::effectNode (lynx::EffectKind::utility), pluginNode ("B") };
    doc2.nodes[0].effect.params[0] = 0.3f;
    lynx::patcher::connect (doc2, control (0, lynx::patchParamOutput (0), 1, 4));
    std::vector<std::shared_ptr<ProbeEffect>> probes2;
    lynx::patcher::Graph g2 (doc2, 48000.0, probeFactories (probes2));
    g2.prepare (48000.0);
    block (g2);
    CHECK (probes2.size() == 1 && std::abs (probes2[0]->values[4] - 0.3f) < 1e-6f);
}

TEST (patcher171b_port_editing)
{
    using namespace patcher171b;
    lynx::PatcherDoc doc;
    doc.nodes = { pluginNode ("A"), pluginNode ("B"), pluginNode ("C") };
    lynx::patcher::setParamInput (doc, 1, 4, true, "B - 5");
    lynx::patcher::setParamInput (doc, 1, 1, true, "B - 2");
    lynx::patcher::setParamInput (doc, 2, 0, true, "C - 1");
    lynx::patcher::setParamInput (doc, 1, 4, true, "again"); // (already active: unchanged)
    CHECK (doc.nodes[1].paramIns == (std::vector<int> { 1, 4 }));
    CHECK (doc.published.size() == 3 && doc.published[0].name == "B - 5");
    lynx::patcher::connect (doc, control (lynx::patchSurface, 0, 1, 4));
    lynx::SurfaceControl knob;
    doc.surface = { knob };
    lynx::patcher::connect (doc, control (lynx::patchSurface, 0, 1, 4));
    lynx::patcher::connect (doc, control (0, lynx::patchParamOutput (7), 2, 0));
    CHECK (lynx::patcher::shownParamOutputs (doc, 0) == std::vector<int> { 7 }); // (shown because cabled)
    CHECK (lynx::patcher::publishedIndex (doc, 2, 0) == 2);

    // Deactivate unused: B's input 1 has no cable (goes), 4 stays
    lynx::patcher::deactivateUnusedParams (doc, 1, true);
    CHECK (doc.nodes[1].paramIns == std::vector<int> { 4 });
    CHECK (lynx::patcher::publishedIndex (doc, 1, 1) < 0 && doc.published.size() == 3 && doc.published[1].node == -1);
    // deactivating a cabled input removes its cables and frees its Patcher parameter (the others keep their index)
    lynx::patcher::setParamInput (doc, 1, 4, false);
    CHECK (doc.nodes[1].paramIns.empty() && lynx::patcher::shownParamInputs (doc, 1).empty());
    CHECK (doc.published.size() == 3 && doc.published[0].node == -1 && doc.published[2].node == 2);
    // FL's Disconnect: the node's cables go, its ports stay
    CHECK (lynx::patcher::nodeHasCables (doc, 2));
    lynx::patcher::disconnectNode (doc, 2);
    CHECK (! lynx::patcher::nodeHasCables (doc, 2) && doc.nodes[2].paramIns == std::vector<int> { 0 });
    // removing a node frees its Patcher parameters and shifts the later nodes' down
    lynx::patcher::removeNode (doc, 0);
    CHECK (doc.published.size() == 3 && doc.published[2].node == 1 && doc.published[2].param == 0);
    lynx::patcher::removeNode (doc, 1);
    CHECK (doc.published.empty() || std::all_of (doc.published.begin(), doc.published.end(), [] (const lynx::PatchParam& p) { return p.node < 0; }));
    // the last free index of a deactivated port is dropped
    lynx::PatcherDoc small;
    small.nodes = { pluginNode ("A") };
    lynx::patcher::setParamInput (small, 0, 2, true);
    lynx::patcher::setParamInput (small, 0, 2, false);
    CHECK (small.published.empty() && small.nodes[0].paramIns.empty());
    // the ports are part of the document (an undo restores them) but not of the running structure
    auto withPort = small;
    lynx::patcher::setParamOutput (withPort, 0, 3, true);
    CHECK (! (withPort == small) && withPort.nodes[0].paramOuts == std::vector<int> { 3 });
    CHECK (lynx::patcher::sameStructure (withPort, small));
}

TEST (patcher171b_fl_parameter_ports_import)
{
    using namespace patcher141;
    // FL's state: a Control Surface's output 0 (0x10000000) on a Fruity Balance's volume (parameter 1, port flags 3 =
    // input + visible), the Balance's pan port active but unused, a plugin's parameter 6 as an output (flags 2) cabled to
    // another plugin's parameter 2; record 1100 publishes the volume port as Patcher parameter 0
    FlBlob blob;
    std::vector<std::uint8_t> balance;
    FlBlob::put (balance, (std::int32_t) 1);
    FlBlob::put (balance, (std::int32_t) 0);
    FlBlob::put (balance, (std::int32_t) 256);
    auto port = [] (std::uint32_t kind, std::uint32_t index, std::uint64_t id, std::uint32_t flags) {
        std::vector<std::uint8_t> d;
        FlBlob::put (d, kind);
        FlBlob::put (d, index);
        FlBlob::put (d, id);
        FlBlob::put (d, flags);
        d.resize (28, 0);
        return d;
    };
    // nodes by hand (FlBlob::node writes flags 3 / 2 only)
    auto node = [&] (std::uint32_t type, std::uint64_t id, const std::string& plugin, const std::vector<std::uint8_t>& state,
                     const std::vector<std::vector<std::uint8_t>>& ports) {
        std::vector<std::uint8_t> g, header;
        FlBlob::put (header, type);
        FlBlob::put (header, id);
        header.resize (40, 0);
        FlBlob::chunk (g, 5000, header);
        FlBlob::chunk (g, 5008, FlBlob::wide (plugin));
        FlBlob::chunk (g, 5005, state);
        for (const auto& p : ports)
            FlBlob::chunk (g, 5009, p);
        FlBlob::chunk (g, 5001, {});
        blob.group (g);
    };
    node (2, 100, "Fruity Balance", balance, { port (0, 0, 1001, 3), port (0, 0, 1002, 2), port (2, 1, 1003, 3), port (2, 0, 1004, 3) });
    node (2, 101, "Control Surface", {}, { port (2, 0x10000000, 1012, 10) });
    node (2, 102, "Fruity Wrapper", {}, { port (0, 0, 1021, 3), port (0, 0, 1022, 2), port (2, 6, 1023, 2) });
    node (2, 103, "Fruity Wrapper", {}, { port (0, 0, 1031, 3), port (0, 0, 1032, 2), port (2, 2, 1033, 3) });
    blob.pin (2001, 0, true, 0, "Audio input");
    blob.pin (2002, 0, false, 0, "Audio output");
    blob.cable (1001, 2001, 1.0f, 0);
    blob.cable (2002, 1002, 1.0f, 0);
    blob.cable (1003, 1012, 1.0f, 2);
    blob.cable (1033, 1023, 1.0f, 2);
    {
        std::vector<std::uint8_t> exposed;
        FlBlob::put (exposed, (std::uint64_t) 1003);
        FlBlob::put (exposed, (std::int32_t) 0);
        exposed.resize (32, 0);
        FlBlob::chunk (blob.bytes, 1100, exposed);
    }
    const auto state = blob.finish();
    const auto graph = lynx::parsePatcher (state);
    CHECK (graph && graph->ports.size() >= 9);
    if (graph)
        for (const auto& [id, p] : graph->ports)
            if (id == 1023)
                CHECK (p.flags == 2 && ! p.input && p.kind == 2 && p.index == 6);

    flp::Project project;
    project.tempo = 120.0;
    flp::MixerInsert insert;
    insert.index = 1;
    flp::MixerInsert::Slot s;
    s.plugin = "Patcher";
    s.state = state;
    insert.slots.push_back (s);
    project.mixer.push_back (insert);
    const auto song = lynx::importFlp (project);
    CHECK (song.mixer.size() > 1 && song.mixer[1].effects.size() == 1);
    if (song.mixer.size() < 2 || song.mixer[1].effects.empty())
        return;
    const auto& doc = song.mixer[1].effects[0].patcher;
    CHECK (doc.nodes.size() >= 2);
    if (doc.nodes.size() < 2)
        return;
    const auto balanceVolume = lynx::effectParamFromFl ("Fruity Balance", 1);
    const auto balancePan = lynx::effectParamFromFl ("Fruity Balance", 0);
    CHECK (balanceVolume.has_value() && balancePan.has_value());
    if (balanceVolume && balancePan)
    {
        // both visible parameter inputs of the Balance are active ports
        auto expected = std::vector<int> { balanceVolume->param, balancePan->param };
        std::sort (expected.begin(), expected.end());
        CHECK (doc.nodes[0].paramIns == expected);
        // the Control Surface → volume event link
        CHECK (std::any_of (doc.wires.begin(), doc.wires.end(), [&] (const lynx::PatchWire& w) {
            return w.kind == lynx::PatchWire::Kind::control && w.from == 1 && w.fromPort == 0 && w.to == 0 && w.toPort == balanceVolume->param;
        }));
        CHECK (! doc.published.empty() && doc.published[0].node == 0 && doc.published[0].param == balanceVolume->param);
    }
    // the plugin → plugin parameter link (when the import keeps the wrapped plugins as nodes)
    for (int n = 0; n < (int) doc.nodes.size(); ++n)
        if (doc.nodes[(std::size_t) n].effect.kind == lynx::EffectKind::plugin && ! doc.nodes[(std::size_t) n].paramOuts.empty())
        {
            CHECK (doc.nodes[(std::size_t) n].paramOuts == std::vector<int> { 6 });
            CHECK (std::any_of (doc.wires.begin(), doc.wires.end(), [n] (const lynx::PatchWire& w) {
                return w.kind == lynx::PatchWire::Kind::control && w.from == n && w.fromPort == lynx::patchParamOutput (6) && w.toPort == 2;
            }));
        }
}

TEST (patcher171b_fl_patcher_preset_with_event_links)
{
    // FL's own Patcher preset "8 step pattern" (Plugin presets › Effects › Patcher › Filter): a Control Surface whose 11
    // controls are cabled to Fruity Love Philter / Fruity delay bank parameters; every target port is visible and
    // published (record 1100). The import keeps the Surface's event links its targets map to.
    if (realFlpDir.empty())
        return;
    std::ifstream in (std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Effects" / "Patcher" / "Filter" / "8 step pattern.fst",
                      std::ios::binary);
    if (! in)
        return;
    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const auto preset = flp::parseProject (bytes, true);
    std::vector<std::uint8_t> state;
    for (const auto& c : preset.channels)
        if (c.pluginName == "Patcher")
            state = c.pluginState;
    if (state.empty())
        for (const auto& t : preset.mixer)
            for (const auto& sl : t.slots)
                if (sl.plugin == "Patcher")
                    state = sl.state;
    CHECK (! state.empty());
    const auto graph = lynx::parsePatcher (state);
    CHECK (graph.has_value());
    if (! graph)
        return;
    int flLinks = 0, visibleIns = 0;
    for (const auto& cable : graph->cables)
        flLinks += cable.kind == 2;
    for (const auto& [id, p] : graph->ports)
        visibleIns += p.node >= 0 && p.kind == 2 && p.input && (p.flags & 2) != 0;
    CHECK (flLinks == 11 && visibleIns == 11 && graph->exposed.size() >= 11);

    flp::Project project;
    project.tempo = 120.0;
    flp::MixerInsert insert;
    insert.index = 1;
    flp::MixerInsert::Slot s;
    s.plugin = "Patcher";
    s.state = state;
    insert.slots.push_back (s);
    project.mixer.push_back (insert);
    lynx::FlpImportReport report;
    lynx::FlpImportOptions options;
    options.report = &report;
    const auto song = lynx::importFlp (project, options);
    CHECK (song.mixer.size() > 1 && song.mixer[1].effects.size() == 1);
    if (song.mixer.size() < 2 || song.mixer[1].effects.empty())
        return;
    const auto& doc = song.mixer[1].effects[0].patcher;
    int links = 0, ports = 0;
    for (const auto& w : doc.wires)
        links += w.kind == lynx::PatchWire::Kind::control;
    for (const auto& n : doc.nodes)
        ports += (int) n.paramIns.size();
    std::printf ("  8 step pattern: %d FL event links -> %d LYNX control cables, %d active parameter inputs, %d published\n", flLinks, links, ports,
                 (int) std::count_if (doc.published.begin(), doc.published.end(), [] (const lynx::PatchParam& p) { return p.node >= 0; }));
    // every imported cable ends on an active (shown) parameter input, and the graph runs
    for (const auto& w : doc.wires)
        if (w.kind == lynx::PatchWire::Kind::control)
        {
            const auto shown = lynx::patcher::shownParamInputs (doc, w.to);
            CHECK (std::find (shown.begin(), shown.end(), w.toPort) != shown.end());
        }
    CHECK (std::isfinite (patcher141::run (song.mixer[1].effects[0], 0.5f, 512, 4).first));
}
