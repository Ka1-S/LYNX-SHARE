// Round 167d (REA_rodada167d): Kepler Exo on Kepler's engine port — the "Kepler-2" state, the import from FL projects
// and presets, and renders against FL's own engine DLL.

namespace
{
// A "Kepler-2" state written from scratch (REA_rodada167d §1 layout): version, n panel floats, the matrix block
// (flag 1, 8 slots), version > 2 bytes (arp restart aligned, ×4 oversampling, alt-osc)
std::vector<std::uint8_t> keplerExoTestState (const std::vector<float>& panel, int version = 2,
                                              const std::vector<std::pair<int, int>>& slots = {}, bool oversample4 = false)
{
    std::vector<std::uint8_t> st;
    auto u32 = [&] (std::uint32_t v) { for (int i = 0; i < 4; ++i) st.push_back ((std::uint8_t) (v >> (8 * i))); };
    auto f32 = [&] (float f) { std::uint32_t v; std::memcpy (&v, &f, 4); u32 (v); };
    u32 (0), u32 (0), u32 (0), u32 (9);
    for (char c : std::string ("Kepler-2"))
        st.push_back ((std::uint8_t) c);
    st.push_back (0);
    u32 ((std::uint32_t) version), u32 ((std::uint32_t) panel.size());
    for (auto f : panel)
        f32 (f);
    u32 (1), u32 (8);
    for (int k = 0; k < 8; ++k)
    {
        const auto sl = k < (int) slots.size() ? slots[(std::size_t) k] : std::pair<int, int> { -1, -1 };
        u32 ((std::uint32_t) sl.first), u32 ((std::uint32_t) sl.second);
    }
    if (version > 2)
        st.push_back (0), st.push_back (oversample4 ? 1 : 0), st.push_back (0);
    const auto size = (std::uint32_t) st.size() - 12;
    std::memcpy (st.data() + 4, &size, 4);
    return st;
}

std::string keplerExoDll()
{
    if (realFlpDir.empty())
        return {};
    return (std::filesystem::path (realFlpDir).parent_path() / "Plugins" / "Fruity" / "Generators" / "Kepler Exo" / "engine_x64.dll").string();
}
} // namespace

TEST (kepler_exo_state_layout)
{
    const std::vector<float> panel (0x97, 0.25f);
    const auto st = keplerExoTestState (panel);
    CHECK (lynx::keplerExoState (st) && lynx::keplerStateUsable (st));
    CHECK (lynx::keplerPanelOffset (st, 0x48) == 0x21 + 4 * 0x48 && lynx::keplerPanelOffset (st, 0x97) == -1);
    CHECK (! lynx::keplerExoState (keplerTestState())); // a Kepler-1 state is not Exo's
    // version 3 with the ×4 oversampling byte (not modelled; no FL data uses it) and a non-finite value are refused
    CHECK (lynx::keplerExoState (keplerExoTestState (panel, 3)));
    CHECK (! lynx::keplerExoState (keplerExoTestState (panel, 3, {}, true)));
    auto bad = panel;
    bad[5] = std::numeric_limits<float>::quiet_NaN();
    CHECK (! lynx::keplerExoState (keplerExoTestState (bad)));
    auto cut = st;
    cut.resize (0x21 + 4 * 10);
    CHECK (! lynx::keplerExoState (cut));
}

TEST (kepler_exo_imports_from_fl_projects)
{
    // FL's plugin "Kepler Exo" saves the "Kepler-2" state; with the user's Kepler Exo engine DLL it becomes the engine
    // channel, a Kepler-1 state under that name or a missing DLL does not
    const auto st = keplerExoTestState (std::vector<float> (0x97, 0.25f));
    auto import = [] (const std::string& plugin, const std::vector<std::uint8_t>& state, const std::string& factory) {
        flp::Project p;
        flp::Channel c;
        c.type = flp::ChannelType::instrument;
        c.pluginName = plugin;
        c.pluginState = state;
        p.channels.push_back (c);
        lynx::FlpImportOptions options;
        options.roots.factoryData = factory;
        options.fileExists = [] (const std::string& path) { return std::filesystem::exists (path); };
        return lynx::importFlp (p, options);
    };
    const auto none = import ("Kepler Exo", st, "/nonexistent-fl");
    CHECK (none.channels.size() == 1 && none.channels[0].instrument != lynx::InstrumentKind::keplerExo);
    const auto dll = keplerExoDll();
    if (dll.empty() || ! lynx::keplerExoDllUsable (dll))
        return;
    CHECK (! lynx::keplerExoDllUsable (keplerDll()) && lynx::keplerDllUsable (dll));
    const auto factory = std::filesystem::path (realFlpDir).parent_path().string();
    const auto s = import ("Kepler Exo", st, factory);
    CHECK (s.channels.size() == 1 && s.channels[0].instrument == lynx::InstrumentKind::keplerExo);
    CHECK (s.channels[0].pluginState == st && ! s.channels[0].samplePath.empty());
    const auto k1 = import ("Kepler Exo", keplerTestState(), factory);
    CHECK (k1.channels.size() == 1 && k1.channels[0].instrument != lynx::InstrumentKind::keplerExo);
    // the engines refuse the other plugin's DLL
    CHECK (lynx::kepler::renderReference (st, keplerDll(), { { 0, 60, 1.0, 0.0, 0.0, -1 } }, 44100.0, 512).empty());
    CHECK (! lynx::kepler::renderReference (st, dll, { { 0, 60, 1.0, 0.0, 0.0, -1 } }, 44100.0, 512).empty());
}
