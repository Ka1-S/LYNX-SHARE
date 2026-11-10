// Round 141 (issue #3): FL's Patcher as a real, editable plugin graph (core/Patcher.h, REA_rodada141)

namespace patcher141
{
lynx::PatchNode effectNode (lynx::EffectKind kind)
{
    lynx::PatchNode n;
    n.effect = lynx::makeEffectSlot (kind);
    return n;
}

lynx::PatchWire audio (int from, int to, float gain = 1.0f, int fromPort = 0, int toPort = 0)
{
    lynx::PatchWire w;
    w.from = from;
    w.to = to;
    w.gain = gain;
    w.fromPort = fromPort;
    w.toPort = toPort;
    return w;
}

// one block of a constant through an effect Patcher
std::pair<float, float> run (const lynx::EffectSlot& slot, float in, int n = 512, int blocks = 1)
{
    auto fx = lynx::patcher::makeEffect (slot, 48000.0, lynx::patcher::builtInFactories (48000.0));
    fx->prepare (48000.0);
    std::vector<float> l, r;
    for (int b = 0; b < blocks; ++b)
    {
        l.assign ((std::size_t) n, in);
        r.assign ((std::size_t) n, in);
        fx->process (l.data(), r.data(), n, {}, 120.0);
    }
    return { l.back(), r.back() };
}

// FL's Patcher state: a v4 chunk stream (REA_rodada14 §1) with the given node / pin / cable groups
struct FlBlob
{
    std::vector<std::uint8_t> bytes { 4, 0, 0, 0 };
    template <typename T>
    static void put (std::vector<std::uint8_t>& out, T v)
    {
        const auto* p = reinterpret_cast<const std::uint8_t*> (&v);
        out.insert (out.end(), p, p + sizeof (T));
    }
    static void chunk (std::vector<std::uint8_t>& out, std::uint32_t id, const std::vector<std::uint8_t>& data)
    {
        put (out, id);
        put (out, (std::uint64_t) data.size());
        out.insert (out.end(), data.begin(), data.end());
    }
    static std::vector<std::uint8_t> wide (const std::string& s)
    {
        std::vector<std::uint8_t> out;
        for (const auto c : s)
            out.push_back ((std::uint8_t) c), out.push_back (0);
        return out;
    }
    void group (const std::vector<std::uint8_t>& content) { chunk (bytes, 0, content); }

    // a node: type 2 effect / 3 generator, its ports (class, index, id, input?)
    struct PortSpec
    {
        std::uint32_t kind, index;
        std::uint64_t id;
        bool input;
    };
    void node (std::uint32_t type, std::uint64_t id, const std::string& plugin, const std::vector<std::uint8_t>& state,
               const std::vector<PortSpec>& ports, const std::string& label = {}, float x = 0.0f, float y = 0.0f)
    {
        std::vector<std::uint8_t> g, header;
        put (header, type);
        put (header, id);
        put (header, (std::uint32_t) 0);
        header.resize (40, 0);
        chunk (g, 5000, header);
        chunk (g, 5008, wide (plugin));
        if (! label.empty())
            chunk (g, 5010, wide (label));
        chunk (g, 5005, state);
        for (const auto& p : ports)
        {
            std::vector<std::uint8_t> d;
            put (d, p.kind);
            put (d, p.index);
            put (d, p.id);
            put (d, (std::uint32_t) (p.input ? 3 : 2));
            d.resize (28, 0);
            chunk (g, 5009, d);
        }
        chunk (g, 5001, {});
        group (g);
        std::vector<std::uint8_t> pos, pg;
        put (pos, id);
        put (pos, x);
        put (pos, y);
        chunk (pg, 1600, pos);
        positions.push_back (pg);
    }
    void pin (std::uint64_t id, std::uint32_t kind, bool fromOutside, std::int32_t bus, const std::string& name)
    {
        std::vector<std::uint8_t> g, d;
        put (d, id);
        put (d, kind);
        put (d, (std::uint32_t) (fromOutside ? 1 : 0));
        d.push_back (2);
        put (d, bus);
        d.resize (28, 0);
        chunk (g, 1400, d);
        chunk (g, 1402, wide (name));
        chunk (g, 1401, {});
        group (g);
    }
    void cable (std::uint64_t destination, std::uint64_t source, float gain, std::uint8_t kind)
    {
        std::vector<std::uint8_t> g, d;
        put (d, destination);
        put (d, source);
        put (d, gain);
        put (d, (std::uint32_t) 2);
        d.push_back (kind);
        d.resize (32, 0);
        chunk (g, 1300, d);
        group (g);
    }
    std::vector<std::vector<std::uint8_t>> positions;
    std::vector<std::uint8_t> finish()
    {
        for (const auto& p : positions)
            group (p);
        chunk (bytes, 1001, {});
        return bytes;
    }
};
} // namespace patcher141

TEST (patcher_graph_sums_cables_with_their_gains)
{
    using namespace patcher141;
    // FL's IPP sum (REA_rodada20 §3.1): IN → Utility ×0.5 → OUT plus IN → OUT ×0.25 = 0.75 of the input
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::patcher);
    slot.patcher.wires.clear();
    slot.patcher.nodes.push_back (effectNode (lynx::EffectKind::utility));
    slot.patcher.wires = { audio (lynx::patchFromHost, 0), audio (0, lynx::patchToHost, 0.5f), audio (lynx::patchFromHost, lynx::patchToHost, 0.25f) };
    auto out = run (slot, 1.0f);
    CHECK (std::abs (out.first - 0.75f) < 1e-6f && std::abs (out.second - 0.75f) < 1e-6f);

    // a disabled cable is left out of the sum; an output with no cable is silence
    slot.patcher.wires[2].enabled = false;
    out = run (slot, 1.0f);
    CHECK (std::abs (out.first - 0.5f) < 1e-6f);
    slot.patcher.wires.clear();
    out = run (slot, 1.0f);
    CHECK (out.first == 0.0f && out.second == 0.0f);

    // a new Patcher passes its input through, as FL's
    out = run (lynx::makeEffectSlot (lynx::EffectKind::patcher), 0.3f);
    CHECK (std::abs (out.first - 0.3f) < 1e-6f);

    // a node switched off passes its input; its dry/wet mixes like a mixer slot
    slot.patcher.wires = { audio (lynx::patchFromHost, 0), audio (0, lynx::patchToHost) };
    slot.patcher.nodes[0].effect.params[0] = 0.0f; // mute
    CHECK (run (slot, 1.0f).first == 0.0f);
    slot.patcher.nodes[0].effect.mix = 0.25f;
    CHECK (std::abs (run (slot, 1.0f).first - 0.75f) < 1e-6f);
    slot.patcher.nodes[0].effect.enabled = false;
    CHECK (std::abs (run (slot, 1.0f).first - 1.0f) < 1e-6f);
}

TEST (patcher_graph_compensates_branch_latency)
{
    using namespace patcher141;
    // a linear-phase Frequency Splitter (384 samples) in parallel with the dry signal: FL delays the dry cable so the
    // two branches stay aligned, and the Patcher reports the longest path
    auto splitter = lynx::makeEffectSlot (lynx::EffectKind::frequencySplitter);
    splitter.params[4] = 0.6f;
    splitter.params[6] = 0.0f;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::patcher);
    lynx::PatchNode node;
    node.effect = splitter;
    slot.patcher.nodes = { node };
    slot.patcher.wires = { audio (lynx::patchFromHost, 0), audio (0, lynx::patchToHost), audio (lynx::patchFromHost, lynx::patchToHost) };
    auto fx = lynx::patcher::makeEffect (slot, 44100.0, lynx::patcher::builtInFactories (44100.0));
    fx->prepare (44100.0);
    CHECK (fx->latencySamples() == 384);
    std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
    l[0] = r[0] = 1.0f;
    fx->process (l.data(), r.data(), 4096, {}, 120.0);
    float early = 0.0f;
    for (int i = 0; i < 300; ++i)
        early = std::max (early, std::abs (l[(std::size_t) i]));
    CHECK (early < 1e-3f); // the dry impulse waits for the splitter's
    CHECK (std::abs (l[384] - 2.0f) < 0.05f);
}

TEST (patcher_generator_plays_the_instruments_cabled_to_notes)
{
    using namespace patcher141;
    lynx::ChannelDef channel;
    channel.instrument = lynx::InstrumentKind::patcher;
    lynx::PatchNode synth;
    synth.generator = true;
    synth.channel.instrument = lynx::InstrumentKind::synth;
    channel.patcher.nodes = { synth, effectNode (lynx::EffectKind::utility) };
    lynx::PatchWire notes;
    notes.kind = lynx::PatchWire::Kind::midi;
    notes.from = lynx::patchFromHost;
    notes.to = 0;
    channel.patcher.wires = { notes, audio (0, 1), audio (1, lynx::patchToHost) };
    auto render = [] (const lynx::ChannelDef& c) {
        auto i = lynx::patcher::makeInstrument (c, 48000.0, lynx::patcher::builtInFactories (48000.0));
        i->noteOn (60, 0.8f);
        std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
        i->beginChunk (2048);
        i->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
        double e = 0.0;
        for (const auto v : l)
            e += (double) v * v;
        return e;
    };
    CHECK (render (channel) > 1e-3);
    auto unplugged = channel;
    unplugged.patcher.wires.erase (unplugged.patcher.wires.begin()); // no notes cable: nothing plays
    CHECK (render (unplugged) == 0.0);

    // an extra "To LYNX" output: its own audio, to the insert after the channel's by default (or the one chosen)
    auto multi = channel;
    multi.mixerTrack = 3;
    multi.patcher.wires.push_back (audio (0, lynx::patchToHost, 1.0f, 0, 1));
    auto i = lynx::patcher::makeInstrument (multi, 48000.0, lynx::patcher::builtInFactories (48000.0));
    CHECK (i->auxOutputCount() == 1 && i->auxOutputTrack (0) == 4);
    i->noteOn (64, 1.0f);
    std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
    i->beginChunk (1024);
    i->render (l.data(), r.data(), 512, 1.0f, 1.0f);
    i->render (l.data() + 512, r.data() + 512, 512, 1.0f, 1.0f);
    double aux = 0.0;
    for (int k = 0; k < 1024; ++k)
        aux += std::abs (i->auxOutputData (0, 0)[k]);
    CHECK (aux > 1e-3);
    multi.patcher.outputTracks = { 7 };
    i->configure (multi, 120.0);
    CHECK (i->auxOutputTrack (0) == 7);
}

TEST (patcher_control_cables_move_parameters)
{
    using namespace patcher141;
    // a Peak Controller's output drives the Utility's gain through the cable's map (here: always 0 = mute)
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::patcher);
    slot.patcher.nodes = { effectNode (lynx::EffectKind::peakController), effectNode (lynx::EffectKind::utility) };
    lynx::PatchWire control;
    control.kind = lynx::PatchWire::Kind::control;
    control.from = 0;
    control.to = 1;
    control.toPort = 0;
    control.curve = { 0.0f, 0.0f };
    slot.patcher.wires = { audio (lynx::patchFromHost, 0), audio (lynx::patchFromHost, 1), audio (1, lynx::patchToHost), control };
    CHECK (run (slot, 1.0f).first == 0.0f);
    slot.patcher.wires.pop_back();
    CHECK (std::abs (run (slot, 1.0f).first - 1.0f) < 1e-6f);

    // a published parameter (FL's record 1100): automating the Patcher's parameter 0 moves the Utility's gain
    slot.patcher.published = { { 1, 0, { 0.0f, 0.0f }, "Gain" } };
    auto fx = lynx::patcher::makeEffect (slot, 48000.0, lynx::patcher::builtInFactories (48000.0));
    std::vector<float> l (256, 1.0f), r (256, 1.0f);
    fx->setParameter (0, 0.7f);
    fx->process (l.data(), r.data(), 256, {}, 120.0);
    CHECK (l.back() == 0.0f);
    fx->setParameter (5, 0.7f); // (an index it does not publish: ignored)
}

TEST (patcher_keeps_running_nodes_across_snapshots)
{
    using namespace patcher141;
    auto song = lynx::Song::makeDefault();
    if (song.mixer.size() < 2)
        song.mixer.resize (2);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::patcher);
    slot.patcher.nodes = { effectNode (lynx::EffectKind::reverb), effectNode (lynx::EffectKind::utility) };
    slot.patcher.wires = { audio (lynx::patchFromHost, 0), audio (0, 1), audio (1, lynx::patchToHost) };
    song.mixer[1].effects = { slot };
    const auto a = lynx::RackState::build (song, nullptr, 44100.0);
    // a parameter, a cable gain: the same running graph takes the values
    song.mixer[1].effects[0].patcher.nodes[1].effect.params[0] = 0.25f;
    song.mixer[1].effects[0].patcher.wires[1].gain = 0.5f;
    const auto b = lynx::RackState::build (song, a.get(), 44100.0);
    CHECK (a->effects[1][0] == b->effects[1][0]);
    // a new cable: a new graph, but the reverb keeps its instance (and its tail)
    song.mixer[1].effects[0].patcher.wires.push_back (audio (lynx::patchFromHost, lynx::patchToHost));
    const auto c = lynx::RackState::build (song, b.get(), 44100.0);
    CHECK (c->effects[1][0] != b->effects[1][0]);
    auto* before = lynx::patcher::graphOf (b->effects[1][0].get());
    auto* after = lynx::patcher::graphOf (c->effects[1][0].get());
    CHECK (before != nullptr && after != nullptr && before->effectAt (0) == after->effectAt (0));

    // editing helpers: removing a node takes its cables and renumbers the rest
    auto doc = song.mixer[1].effects[0].patcher;
    lynx::patcher::removeNode (doc, 0);
    CHECK (doc.nodes.size() == 1 && doc.wires.size() == 2 && doc.wires[0].from == 0 && doc.wires[0].to == lynx::patchToHost);
    CHECK (! lynx::patcher::connect (doc, doc.wires[0]));        // the same cable twice
    CHECK (lynx::patcher::connect (doc, audio (lynx::patchFromHost, 0)));
}

TEST (patcher_generator_runs_inside_the_rack_with_extra_outputs)
{
    using namespace patcher141;
    auto song = lynx::Song::makeDefault();
    song.mixer.resize (6);
    song.channels.resize (1);
    auto& channel = song.channels[0];
    channel = {};
    channel.instrument = lynx::InstrumentKind::patcher;
    channel.mixerTrack = 2;
    lynx::PatchNode synth;
    synth.generator = true;
    synth.channel.instrument = lynx::InstrumentKind::synth;
    channel.patcher.nodes = { synth };
    lynx::PatchWire notes;
    notes.kind = lynx::PatchWire::Kind::midi;
    notes.to = 0;
    channel.patcher.wires = { notes, audio (0, lynx::patchToHost, 1.0f, 0, 1) }; // only the extra output: insert 3
    lynx::Pattern p;
    p.notes.push_back ({ 0, lynx::ppq, 0, 60, 0.8f });
    song.patterns = { p };
    lynx::Clip clip;
    clip.pattern = 0;
    clip.length = lynx::ppq * 4;
    song.playlist = { clip };
    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> l (512), rr (512);
    double e = 0.0, tick = 0.0;
    for (int b = 0; b < 8; ++b, tick += 512.0 * song.bpm * lynx::ppq / 60.0 / 48000.0)
    {
        r.process (state.get(), true, tick, 512, l.data(), rr.data(), nullptr);
        for (const auto v : l)
            e += (double) v * v;
    }
    CHECK (e > 1e-4); // it reached the Master through insert 3
}

TEST (patcher_state_from_fl_becomes_the_graph)
{
    using namespace patcher141;
    // IN → Fruity Balance "Left" → OUT ×0.5 and IN → OUT, a Peak Controller's output 0 (Peak+LFO) on the Balance's volume
    // (FL parameter 1), the node positions and an extra output named "Insert 5"
    FlBlob blob;
    std::vector<std::uint8_t> balance;
    FlBlob::put (balance, (std::int32_t) 1);   // version
    FlBlob::put (balance, (std::int32_t) -64); // balance
    FlBlob::put (balance, (std::int32_t) 256); // volume
    blob.node (2, 100, "Fruity Balance", balance, { { 0, 0, 1001, true }, { 0, 0, 1002, false }, { 2, 1, 1003, true } }, "Left", 200.0f, 50.0f);
    blob.node (2, 101, "Fruity Peak Controller", std::vector<std::uint8_t> (64, 0), { { 0, 0, 1011, true }, { 2, 0x10000000, 1012, false } },
               {}, 200.0f, 200.0f);
    blob.pin (2001, 0, true, 0, "Audio input");
    blob.pin (2002, 0, false, 0, "Audio output");
    blob.pin (2003, 0, false, 1, "Insert 5");
    blob.cable (1001, 2001, 1.0f, 0);
    blob.cable (2002, 1002, 0.5f, 0);
    blob.cable (2002, 2001, 1.0f, 0);
    blob.cable (1011, 2001, 1.0f, 0);
    blob.cable (1003, 1012, 1.0f, 2);
    blob.cable (2003, 1002, 1.0f, 0);
    {
        std::vector<std::uint8_t> exposed; // Patcher parameter 0 = the Balance's volume port
        FlBlob::put (exposed, (std::uint64_t) 1003);
        FlBlob::put (exposed, (std::int32_t) 0);
        exposed.resize (32, 0);
        FlBlob::chunk (blob.bytes, 1100, exposed);
    }
    const auto state = blob.finish();
    const auto graph = lynx::parsePatcher (state);
    CHECK (graph.has_value());
    if (! graph)
        return;
    CHECK (graph->nodes.size() == 2 && graph->nodes[0].label == "Left" && graph->nodes[0].placed && graph->nodes[0].x == 200.0f);
    CHECK (graph->cables.size() == 6);

    flp::Project project;
    project.tempo = 120.0;
    flp::MixerInsert insert;
    insert.index = 1;
    flp::MixerInsert::Slot s;
    s.index = 0;
    s.plugin = "Patcher";
    s.state = state;
    insert.slots.push_back (s);
    project.mixer.push_back (insert);
    const auto song = lynx::importFlp (project);
    CHECK (song.mixer.size() > 1 && song.mixer[1].effects.size() == 1);
    if (song.mixer.size() < 2 || song.mixer[1].effects.empty())
        return;
    const auto& fx = song.mixer[1].effects[0];
    CHECK (fx.kind == lynx::EffectKind::patcher);
    const auto& doc = fx.patcher;
    CHECK (doc.nodes.size() == 2 && doc.nodes[0].label == "Left" && doc.nodes[0].effect.kind == lynx::EffectKind::fruityBalance);
    const auto audioWires = std::count_if (doc.wires.begin(), doc.wires.end(), [] (const lynx::PatchWire& w) { return w.kind == lynx::PatchWire::Kind::audio; });
    CHECK (audioWires == 5);
    CHECK (std::any_of (doc.wires.begin(), doc.wires.end(), [] (const lynx::PatchWire& w) {
        return w.kind == lynx::PatchWire::Kind::audio && w.from == 0 && w.to == lynx::patchToHost && w.toPort == 0 && w.gain == 0.5f;
    }));
    CHECK (std::any_of (doc.wires.begin(), doc.wires.end(), [] (const lynx::PatchWire& w) {
        return w.kind == lynx::PatchWire::Kind::control && w.from == 1 && w.to == 0 && w.fromPort == 0; // FL's 0 "Peak+LFO" = LYNX's 0
    }));
    CHECK (doc.outputTracks.size() == 1 && doc.outputTracks[0] == 5);
    CHECK (doc.published.size() == 1 && doc.published[0].node == 0);
    CHECK (lynx::fruitySendTarget (fx) == 5);
    CHECK (std::isfinite (run (fx, 0.5f).first));
}

TEST (patcher_demo_graphs_import_and_run)
{
    // every Patcher of FL's demo projects becomes a LYNX graph (mixer slots and generator channels) that runs
    if (realFlpDir.empty())
        return;
    int flSlots = 0, slots = 0, flChannels = 0, channels = 0, sounding = 0, nodes = 0, controls = 0, notes = 0, finite = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (std::filesystem::path (realFlpDir) / "Demo projects"))
    {
        if (entry.path().extension() != ".flp")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
        try
        {
            const auto project = flp::parseProject (file);
            int here = 0;
            for (const auto& insert : project.mixer)
                for (const auto& slot : insert.slots)
                    here += slot.plugin == "Patcher";
            for (const auto& c : project.channels)
                flChannels += c.pluginName == "Patcher";
            if (here == 0 && std::none_of (project.channels.begin(), project.channels.end(), [] (const flp::Channel& c) { return c.pluginName == "Patcher"; }))
                continue;
            flSlots += here;
            const auto song = lynx::importFlp (project);
            for (const auto& track : song.mixer)
                for (const auto& fx : track.effects)
                    if (fx.kind == lynx::EffectKind::patcher)
                    {
                        ++slots;
                        nodes += (int) fx.patcher.nodes.size();
                        for (const auto& w : fx.patcher.wires)
                            controls += w.kind == lynx::PatchWire::Kind::control;
                        auto effect = lynx::patcher::makeEffect (fx, 44100.0, lynx::patcher::builtInFactories (44100.0));
                        effect->prepare (44100.0);
                        std::vector<float> l (4096), r (4096);
                        std::uint32_t seed = 1;
                        double e = 0.0;
                        bool ok = true;
                        for (int b = 0; b < 4; ++b)
                        {
                            for (std::size_t i = 0; i < l.size(); ++i)
                            {
                                seed = seed * 1664525u + 1013904223u;
                                l[i] = r[i] = (float) ((seed >> 9) & 0xffff) / 65536.0f * 0.5f - 0.25f;
                            }
                            effect->process (l.data(), r.data(), 4096, {}, song.bpm);
                            for (std::size_t i = 0; i < l.size(); ++i)
                            {
                                ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]);
                                e += (double) l[i] * l[i];
                            }
                        }
                        finite += ok;
                        sounding += e > 1e-6;
                        if (e <= 1e-6)
                            std::printf ("  (silent with noise in: %s, %d nodes)\n", entry.path().filename().string().c_str(), (int) fx.patcher.nodes.size());
                    }
            for (const auto& c : song.channels)
                if (c.instrument == lynx::InstrumentKind::patcher)
                {
                    ++channels;
                    for (const auto& w : c.patcher.wires)
                        notes += w.kind == lynx::PatchWire::Kind::midi;
                }
        }
        catch (const flp::Error&) {}
    }
    std::printf ("  Patcher graphs: %d of %d mixer slots (%d nodes, %d control cables; %d run finite, %d sounding), %d of %d channels (%d note cables)\n",
                 slots, flSlots, nodes, controls, finite, sounding, channels, flChannels, notes);
    CHECK (slots == flSlots && slots >= 20 && finite == slots && sounding >= slots - 2);
    CHECK (channels >= 2 && notes >= channels && controls > 0);
}

TEST (patcher_midi_port_output_reaches_plugins_on_that_port)
{
    // a generator Patcher's "MIDI port 3" output: its notes go to the plugin channels whose MIDI input port is 3
    auto song = lynx::Song::makeDefault();
    song.channels.resize (3);
    song.channels[0] = {};
    song.channels[0].instrument = lynx::InstrumentKind::patcher;
    lynx::PatchWire out;
    out.kind = lynx::PatchWire::Kind::midi;
    out.from = lynx::patchFromHost;
    out.to = lynx::patchToHost;
    out.toPort = 3;
    song.channels[0].patcher.wires = { out };
    for (int c : { 1, 2 })
    {
        song.channels[(std::size_t) c] = {};
        song.channels[(std::size_t) c].instrument = lynx::InstrumentKind::plugin;
        song.channels[(std::size_t) c].plugin.id = "test";
        song.channels[(std::size_t) c].plugin.midiInPort = c == 1 ? 3 : 4;
    }
    const auto state = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (state->hasMidiRoutes && state->midiTargets[0] == std::vector<int> { 1 } && state->midiOutChannel[0] == 0);
}
