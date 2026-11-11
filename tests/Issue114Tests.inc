// Round 190i, issue #114 as reported: a multi-output drum plugin (the reporter's MT-PowerDrumKit: 8 outputs) on a
// channel routed to Master, its outputs numbered 1…8 in the wrapper's Processing › Connections (their second
// screenshot), so that every kit piece gets a mixer insert of its own, which can then be routed and sent on like
// any insert. The Connections code is round 188d's (MultiOut188dTests.inc); this is the issue's own scenario.

namespace issue114
{
constexpr int kitOutputs = 8;
// output k's level (DC): all different, so the insert an output lands on is known from the level alone
constexpr float levelOf (int k) noexcept { return 0.02f * (float) (k + 1); }

// A drum plugin's outputs as engine/PluginInstrument hands them to the rack: offset 0 = with the channel's own
// audio, anything else = an extra output for the insert `offset` tracks after the channel's
struct DrumKit final : lynx::Instrument
{
    int track = 0;
    lynx::PluginOutputRoute routes[kitOutputs];
    std::vector<float> aux[kitOutputs];
    int cursor = 0;

    DrumKit()
    {
        for (auto& a : aux)
            a.assign (4096, 0.0f);
    }
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void configure (const lynx::ChannelDef& c, double) noexcept override
    {
        track = c.mixerTrack;
        for (int k = 0; k < kitOutputs; ++k)
            routes[k] = c.plugin.outputRoute (k);
    }
    void beginChunk (int n) noexcept override
    {
        cursor = 0;
        for (auto& a : aux)
            std::fill (a.begin(), a.begin() + n, 0.0f);
    }
    void render (float* l, float* r, int n, float gl, float gr) noexcept override
    {
        for (int k = 0; k < kitOutputs; ++k)
        {
            if (! routes[k].isActive (true))
                continue;
            for (int i = 0; i < n; ++i)
                if (routes[k].offset == 0)
                {
                    l[i] += levelOf (k) * gl;
                    r[i] += levelOf (k) * gr;
                }
                else
                    aux[k][(std::size_t) (cursor + i)] += levelOf (k) * gl;
        }
        cursor += n;
    }
    int auxOutputCount() const noexcept override { return kitOutputs; }
    int auxOutputTrack (int k) const noexcept override
    {
        return routes[k].offset == 0 ? -1 : lynx::outputTargetTrack (track, routes[k]);
    }
    const float* auxOutputData (int k, int) const noexcept override { return aux[k].data(); }
};

// the Master's level after a few blocks
float master (const lynx::Song& song)
{
    auto kit = std::make_shared<DrumKit>();
    lynx::RackState::Factories f;
    f.pluginInstruments = [&] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Instrument> { return kit; };
    const auto state = lynx::RackState::build (song, nullptr, 48000.0, f);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256);
    for (int b = 0; b < 8; ++b)
        r.process (state.get(), true, 0.0, 256, l.data(), rr.data(), nullptr);
    return std::abs (l.back()) + std::abs (rr.back());
}

// the reporter's project: the drum plugin on a channel routed to Master ("MT-PowerDrumKit (Master)")
lynx::Song project()
{
    auto s = lynx::Song::makeDefault();
    s.channels.clear();
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::plugin;
    c.plugin.id = "VST-MT-PowerDrumKit";
    c.mixerTrack = 0;
    s.channels.push_back (c);
    if (s.mixer.size() < 12)
        s.mixer.resize (12);
    return s;
}

// only insert `keep` of 1…8 plays (keep = 0: none of them)
lynx::Song onlyInsert (lynx::Song s, int keep)
{
    for (int t = 1; t <= kitOutputs; ++t)
        s.mixer[(std::size_t) t].muted = t != keep;
    return s;
}
} // namespace issue114

TEST (issue114_drum_plugin_outputs_get_their_own_mixer_inserts)
{
    using namespace issue114;
    // as loaded: every output plays with the channel, on Master; inserts 1…8 carry nothing
    auto song = project();
    const auto all = master (song);
    CHECK (all > 0.1f);
    CHECK (std::abs (master (onlyInsert (song, 0)) - all) < 1.0e-4f * all);

    // the reporter's Connections list: output 1 on "1", then Auto map outputs → 1, 2, … 8
    auto& routes = song.channels[0].plugin.outputs;
    routes = { { 1, -1 } };
    lynx::autoMapOutputs (routes, kitOutputs);
    CHECK (routes.size() == (std::size_t) kitOutputs);
    for (int k = 0; k < kitOutputs; ++k)
        CHECK (routes[(std::size_t) k].offset == k + 1
               && lynx::outputTargetTrack (0, routes[(std::size_t) k]) == k + 1);

    // the mix is the same as before; nothing is left on Master itself: with inserts 1…8 muted it is silent
    CHECK (std::abs (master (song) - all) < 1.0e-3f * all);
    CHECK (master (onlyInsert (song, 0)) == 0.0f);

    // each insert carries exactly its own output: alone, insert k+1 plays output k's level and nothing else
    const auto first = master (onlyInsert (song, 1));
    CHECK (first > 0.0f);
    for (int k = 0; k < kitOutputs; ++k)
    {
        const auto alone = master (onlyInsert (song, k + 1));
        CHECK (std::abs (alone / first - levelOf (k) / levelOf (0)) < 0.01f);
    }

    // "create sends": the snare's insert (2) can now be routed like any insert — only to insert 10, off Master
    auto routed = onlyInsert (song, 2);
    const auto snare = master (routed);
    CHECK (lynx::mixerops::routeTo (routed, { 2 }, 10, true) > 0);
    CHECK (! routed.mixer[2].toMaster);
    CHECK (std::abs (master (routed) - snare) < 1.0e-3f * snare); // through insert 10 now
    routed.mixer[10].muted = true;
    CHECK (master (routed) == 0.0f);

    // one output switched off in the list (its LED) is silent; the others stay where they are. ("Process inactive
    // inputs and outputs" is the wrapper's own switch, PluginInstrument::configure; a route's LED is what it reads.)
    auto off = song;
    off.channels[0].plugin.outputs[1].active = 0;
    CHECK (master (onlyInsert (off, 2)) == 0.0f);
    CHECK (std::abs (master (onlyInsert (off, 3)) / first - levelOf (2) / levelOf (0)) < 0.01f);

    // Reset: everything back with the channel, on Master
    auto reset = song;
    lynx::resetOutputOffsets (reset.channels[0].plugin.outputs, kitOutputs);
    CHECK (reset.channels[0].plugin.outputs.empty());
    CHECK (std::abs (master (onlyInsert (reset, 0)) - all) < 1.0e-3f * all);

    // the same list with the channel on insert 3: the numbers count from the channel's insert (FL: 4, 5, … 11)
    auto moved = song;
    moved.channels[0].mixerTrack = 3;
    for (int t = 1; t < 12; ++t)
        moved.mixer[(std::size_t) t].muted = t != 4 + 2;
    CHECK (std::abs (master (moved) / first - levelOf (2) / levelOf (0)) < 0.01f);
}
