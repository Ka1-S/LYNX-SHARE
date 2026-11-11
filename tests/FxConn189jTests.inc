// Round 189j (issue #114, next slice): a hosted effect's Processing › Connections played as FL plays them [EXEC FL 2026
// /R renders, docs/analise/REA_rodada189j.md]: input 1's sidechain index, the outputs' indexes, the side input written
// back to .flp.

namespace fxconn189j
{
// A stand-in for tools/testplugin/lynx_test_sc_vst2.c inside the renderer: output pair 1 = input 1, output pair 2 =
// input 2 (its side input), routed as engine/PluginEffect routes them
struct SidePass final : lynx::Effect
{
    lynx::SlotConnections c;
    bool mainSet = false;
    const float *mL = nullptr, *mR = nullptr, *sL = nullptr, *sR = nullptr;
    float* xL[lynx::maxPluginConnections] {};
    float* xR[lynx::maxPluginConnections] {};
    int configured = 0;
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void setConnections (const lynx::SlotConnections& x) override
    {
        c = x;
        ++configured;
    }
    void setMainInput (const float* l, const float* r) noexcept override
    {
        mainSet = true;
        mL = l;
        mR = r;
    }
    void setSidechain (const float* l, const float* r) noexcept override
    {
        sL = l;
        sR = r;
    }
    void setExtraOutput (int o, float* l, float* r) noexcept override
    {
        if (o >= 0 && o < lynx::maxPluginConnections)
        {
            xL[o] = l;
            xR[o] = r;
        }
    }
    void process (float* l, float* r, int n, const lynx::EffectParams&, double) noexcept override
    {
        std::vector<float> in[4];
        for (auto& v : in)
            v.assign ((std::size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            in[0][(std::size_t) i] = ! mainSet ? l[i] : mL != nullptr ? mL[i] : 0.0f;
            in[1][(std::size_t) i] = ! mainSet ? r[i] : mR != nullptr ? mR[i] : 0.0f;
            in[2][(std::size_t) i] = sL != nullptr ? sL[i] : 0.0f;
            in[3][(std::size_t) i] = sR != nullptr ? sR[i] : 0.0f;
        }
        mainSet = false;
        sL = sR = nullptr;
        if (! c.routed)
        {
            std::copy (in[0].begin(), in[0].end(), l);
            std::copy (in[1].begin(), in[1].end(), r);
            return;
        }
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        for (int k = 0; k < 2; ++k)
        {
            const auto mode = c.outputs[(std::size_t) k];
            float* dl = mode == -1 ? l : mode >= 0 ? xL[k] : nullptr;
            float* dr = mode == -1 ? r : mode >= 0 ? xR[k] : nullptr;
            if (dl == nullptr || dr == nullptr)
                continue;
            for (int i = 0; i < n; ++i)
            {
                dl[i] += in[(std::size_t) (2 * k)][(std::size_t) i];
                dr[i] += in[(std::size_t) (2 * k + 1)][(std::size_t) i];
            }
        }
    }
};

// A channel that plays a constant level (its plugin id "DC <level>")
struct Dc final : lynx::Instrument
{
    float level = 0.0f;
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float* l, float* r, int n, float, float) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            l[i] += level;
            r[i] += level;
        }
    }
};

// the experiments' mixer (py/gen.py): insert 1 = own 1, the SidePass; inserts 2 (2) and 3 (4) routed to 1 at level 0
// only; 4 = a reference (8) on the Master; 1 routed to the Master and at level 0 to 5 (panned left) and 6 (right)
lynx::Song song (std::vector<lynx::PluginOutputRoute> inputs, std::vector<lynx::PluginOutputRoute> outputs)
{
    auto s = lynx::Song::makeDefault();
    s.channels.clear();
    const float levels[] = { 1.0f, 2.0f, 4.0f, 8.0f };
    for (int k = 0; k < 4; ++k)
    {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::plugin;
        c.plugin.id = "DC " + std::to_string (levels[k]);
        c.mixerTrack = k + 1;
        s.channels.push_back (c);
    }
    if (s.mixer.size() < 8)
        s.mixer.resize (8);
    for (auto& t : s.mixer)
    {
        t.effects.clear();
        t.sends.clear();
        t.toMaster = true;
        t.pan = 0.0f;
    }
    lynx::EffectSlot fx;
    fx.kind = lynx::EffectKind::plugin;
    fx.plugin.id = "VST-SidePass";
    fx.plugin.inputs = std::move (inputs);
    fx.plugin.outputs = std::move (outputs);
    s.mixer[1].effects.push_back (fx);
    s.mixer[1].sends = { { 5, 0.0f }, { 6, 0.0f } };
    for (const int t : { 2, 3 })
    {
        s.mixer[(std::size_t) t].toMaster = false;
        s.mixer[(std::size_t) t].sends = { { 1, 0.0f } };
    }
    s.mixer[5].pan = -1.0f;
    s.mixer[6].pan = 1.0f;
    return s;
}

struct Lr
{
    float l = 0.0f, r = 0.0f;
};
Lr master (const lynx::Song& song, std::shared_ptr<SidePass> fake)
{
    lynx::RackState::Factories f;
    f.pluginInstruments = [] (const lynx::PluginRef& ref, double) -> std::shared_ptr<lynx::Instrument> {
        auto dc = std::make_shared<Dc>();
        dc->level = 0.01f * std::stof (ref.id.substr (3));
        return dc;
    };
    f.pluginEffects = [fake] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Effect> { return fake; };
    auto state = lynx::RackState::build (song, nullptr, 48000.0, f);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256);
    for (int b = 0; b < 4; ++b)
        r.process (state.get(), true, 0.0, 256, l.data(), rr.data(), nullptr);
    return { l.back(), rr.back() };
}
} // namespace fxconn189j

TEST (fxconn189j_lists_and_indexes_as_fl)
{
    using namespace fxconn189j;
    // the inputs list [1, 2, 3] and the outputs list [1, Master, 5, 6] (ascending, the Master first) [EXEC e02 e05 e11
    // e18]; routes at level 0 count
    const auto s = song ({}, {});
    CHECK ((lynx::insertInputList (s, 1) == std::vector<int> { 1, 2, 3 }));
    CHECK ((lynx::insertOutputList (s, 1) == std::vector<int> { 1, 0, 5, 6 }));
    CHECK ((lynx::insertInputList (s, 1) == lynx::mixerops::insertInputs (s, 1)));

    // untouched Connections: LYNX as before (not routed)
    CHECK (! lynx::slotConnections (s, 1, s.mixer[1].effects[0]).routed);
    // input 1 at 1 / 2 = inserts 2 / 3; at 5 (past the list) the slot's own input [EXEC e01 e02 e10]
    for (const auto& [k, source] : { std::pair { 1, 2 }, { 2, 3 }, { 5, -1 }, { 0, -1 } })
    {
        const auto t = song ({ { k, 1 } }, {});
        const auto c = lynx::slotConnections (t, 1, t.mixer[1].effects[0]);
        CHECK (c.routed && c.mainSource == source && c.sideSource == -1 && c.outputs[0] == -1 && c.outputs[1] == -1);
    }
    // output 2 at 1 / 2 / 3 / 4 / 5: the Master, 5, 6, then the last one [EXEC e04 e05 e11 e12 e07]
    for (const auto& [k, target] : { std::pair { 1, 0 }, { 2, 5 }, { 3, 6 }, { 4, 6 }, { 5, 6 } })
    {
        const auto t = song ({ { 0, 1 }, { 1, 1 } }, { { 0, 1 }, { k, 1 } });
        CHECK (lynx::slotConnections (t, 1, t.mixer[1].effects[0]).outputs[1] == target);
    }
    // (a negative index, out of FL's wheel: the Master [EXEC e17 e23])
    const auto negative = song ({ { 0, 1 }, { 1, 1 } }, { { 0, 1 }, { -1, 1 } });
    CHECK (lynx::slotConnections (negative, 1, negative.mixer[1].effects[0]).outputs[1] == 0);
    // no route to the Master: index 1 = insert 5 [EXEC e13]
    auto noMaster = song ({}, { { 0, 1 }, { 1, 1 } });
    noMaster.mixer[1].toMaster = false;
    CHECK (lynx::slotConnections (noMaster, 1, noMaster.mixer[1].effects[0]).outputs[1] == 5);
    // inactive: played while "Process inactive inputs and outputs" is on (FL's default), else silent [EXEC e09 e15 e16
    // e22]
    auto inactive = song ({ { 1, 0 }, { 0, 0 } }, { { 0, 1 }, { 0, 0 } });
    auto c = lynx::slotConnections (inactive, 1, inactive.mixer[1].effects[0]);
    CHECK (c.mainSource == 2 && c.sideSource == -1 && c.outputs[1] == -1);
    inactive.mixer[1].effects[0].plugin.processInactive = false;
    c = lynx::slotConnections (inactive, 1, inactive.mixer[1].effects[0]);
    CHECK (c.mainSource == -2 && c.sideSource == -2 && c.outputs[1] == -2 && c.outputs[0] == -1);
    // input 2 follows the slot's side input source (its index set it, or LYNX's SC picker)
    auto side = song ({ { 0, 1 }, { 2, 1 } }, {});
    side.mixer[1].effects[0].sidechain = 3;
    CHECK (lynx::slotConnections (side, 1, side.mixer[1].effects[0]).sideSource == 3);

    // a routed output is a slot tap after the slot, the Master too; a built-in effect is never routed
    const auto routed = song ({ { 0, 1 }, { 1, 1 } }, { { 1, 1 }, { 2, 1 } });
    const auto taps = lynx::fruitySendsOf (routed, 1);
    CHECK (taps.size() == 2 && taps[0].target == 0 && taps[0].output == 0 && taps[1].target == 5 && taps[1].output == 1 && taps[1].afterSlot);
    lynx::EffectSlot eq;
    eq.plugin.outputs = { { 1, 1 } };
    CHECK (! lynx::slotConnections (routed, 1, eq).routed);
}

TEST (fxconn189j_renderer_plays_the_connections_as_fl)
{
    using namespace fxconn189j;
    // DC levels: insert 1's own 0.01, insert 2 0.02, insert 3 0.04, the reference 0.08; the stand-in sums its output
    // pairs at index 0 into insert 1 (FL's levels: py/tones.py on the renders, docs/analise/REA_rodada189j.md)
    auto fake = std::make_shared<SidePass>();
    // (the scale of a level through the mixer: the reference alone, insert 1 muted)
    auto alone = song ({}, {});
    alone.mixer[1].muted = true;
    const auto unit = master (alone, fake).l / 0.08f;
    CHECK (unit > 0.1f);
    auto near = [unit] (float a, float b) { return std::abs (a - b * unit) < 1e-3f * unit; };
    // untouched: input in, output 1 out (LYNX as before; FL would also feed input 2 its own audio, e00)
    auto m = master (song ({}, {}), fake);
    CHECK (near (m.l, 0.09f) && near (m.r, 0.09f));
    CHECK (fake->configured > 0);
    // FL's defaults set ([0, 1] per input, as FL saves them): both inputs take the insert's audio, both pairs summed [e00]
    m = master (song ({ { 0, 1 }, { 0, 1 } }, {}), fake);
    CHECK (near (m.l, 0.10f) && near (m.r, 0.10f));
    // input 1 at 1: insert 2 replaces the insert's audio, input 2 still the own one [e01]
    m = master (song ({ { 1, 1 } }, {}), fake);
    CHECK (near (m.l, 0.11f) && near (m.r, 0.11f));
    // input 1 at 2: insert 3 [e02]
    m = master (song ({ { 2, 1 } }, {}), fake);
    CHECK (near (m.l, 0.13f));
    // both at 1 [e08]: insert 2 twice, the own audio gone
    m = master (song ({ { 1, 1 }, { 1, 1 } }, {}), fake);
    CHECK (near (m.l, 0.12f));
    // output 2 at 2: input 2 (insert 2) on insert 5, panned left; output 1 stays [e05]
    m = master (song ({ { 0, 1 }, { 1, 1 } }, { { 0, 1 }, { 2, 1 } }), fake);
    CHECK (m.l > m.r + 0.015f * unit && near (m.r, 0.09f));
    // output 2 at 3: insert 6, panned right [e11]
    m = master (song ({ { 0, 1 }, { 1, 1 } }, { { 0, 1 }, { 3, 1 } }), fake);
    CHECK (m.r > m.l + 0.015f * unit && near (m.l, 0.09f));
    // output 2 at 1 straight to the Master, past insert 1's fader (here all the way down) [e04 e14]
    auto past = song ({ { 0, 1 }, { 1, 1 } }, { { 0, 1 }, { 1, 1 } });
    past.mixer[1].volume = 0.0f;
    m = master (past, fake);
    CHECK (near (m.l, 0.10f) && near (m.r, 0.10f));
    // inactive, "Process inactive inputs and outputs" off: input 1 silent [e22], output 2 silent [e16]
    auto off = song ({ { 1, 0 } }, {});
    off.mixer[1].effects[0].plugin.processInactive = false;
    m = master (off, fake);
    CHECK (near (m.l, 0.09f)); // (only input 2's own audio through output 2)
    off = song ({ { 0, 1 }, { 1, 1 } }, { { 0, 1 }, { 0, 0 } });
    off.mixer[1].effects[0].plugin.processInactive = false;
    m = master (off, fake);
    CHECK (near (m.l, 0.09f));
}

TEST (fxconn189j_flp_export_writes_the_picked_side_input_as_fl_index)
{
    using namespace lynx;
    // a side input picked with LYNX's SC picker is FL's input 2 index on export: its place in [insert, inserts routed
    // to it ascending] as written (the picker's level-0 route included)
    auto song = Song::makeDefault();
    if (song.mixer.size() < 6)
        song.mixer.resize (6);
    for (auto& t : song.mixer)
        t.effects.clear();
    EffectSlot fx;
    fx.kind = EffectKind::plugin;
    fx.plugin.id = "VST-SC";
    song.mixer[1].effects.push_back (fx);
    song.setRoute (2, 1, true, 0.0f);
    CHECK (mixerops::sidechainFrom (song, 1, 0, 3, false) == mixerops::SidechainPick::linked);
    flpexport::Options options;
    options.pluginRecord = [] (const PluginRef&, bool) -> std::optional<ForeignPlugin> {
        ForeignPlugin p;
        p.format = ForeignPlugin::Format::vst2;
        p.vst2Id = 0x4C585343;
        p.name = "LYNX Test SC";
        return p;
    };
    auto exported = [&options] (const Song& s) {
        flpexport::Report report;
        const auto project = flp::parseProject (flpexport::exportFlp (s, options, report));
        ForeignPlugin back;
        for (const auto& m : project.mixer)
            if (m.index == 1 && ! m.slots.empty())
                parseFlWrapper (m.slots.front().state, back);
        return back;
    };
    auto back = exported (song);
    CHECK (back.hasRoutes && back.inputRoutes.size() == 2 && back.inputRoutes[1].offset == 2 && back.inputRoutes[1].active == 1);
    CHECK (back.inputRoutes[0].offset == 0 && back.outputRoutes.size() == 1 && back.outputRoutes[0].offset == 0);
    // an index left over from an earlier pick names what LYNX plays now
    song.mixer[1].effects[0].plugin.inputs = { { 0, -1 }, { 1, -1 } };
    back = exported (song);
    CHECK (back.inputRoutes.size() == 2 && back.inputRoutes[1].offset == 2);
    // the index set in Connections, no picked source: kept as it is
    song.mixer[1].effects[0].sidechain = -1;
    back = exported (song);
    CHECK (back.inputRoutes.size() == 2 && back.inputRoutes[1].offset == 1);
    // and FL's import takes the side input back from it
    song.mixer[1].effects[0].sidechain = 3;
    song.mixer[1].effects[0].plugin.inputs.clear();
    flpexport::Report report;
    const auto again = importFlp (flp::parseProject (flpexport::exportFlp (song, options, report)), {});
    CHECK (again.mixer.size() > 1 && ! again.mixer[1].effects.empty() && again.mixer[1].effects[0].sidechain == 3);
}
