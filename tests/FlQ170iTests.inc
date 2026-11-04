// Round 170i: FL's own Fruity effects saved as VST2 plugins inside Fruity Wrapper (FL 11 projects) open on the exact
// engines with their saved controls (docs/analise/REA_rodada170i.md §1)

namespace flq170i
{
inline std::vector<std::uint8_t> le32 (std::initializer_list<std::int32_t> v)
{
    std::vector<std::uint8_t> out;
    for (auto x : v)
        for (int b = 0; b < 4; ++b)
            out.push_back ((std::uint8_t) ((std::uint32_t) x >> (8 * b)));
    return out;
}
// an FL VST2 state (version −9, no flags, no chunk, current program 0) holding these parameter floats
inline lynx::ForeignPlugin wrappedFruity (const std::string& name, std::initializer_list<float> params)
{
    lynx::FlVst2State s;
    s.params.assign (params.begin(), params.end());
    s.programNames = { "Default" };
    lynx::ForeignPlugin p;
    p.format = lynx::ForeignPlugin::Format::vst2;
    p.name = name;
    p.state = lynx::writeFlVst2State (s);
    return p;
}
} // namespace flq170i

TEST (flq170i_wrapped_fruity_vst2_states)
{
    using namespace lynx;
    using flq170i::wrappedFruity;
    using flq170i::le32;
    // the floats of FL's render tests (System/Render tests/*.flp) → the native states FL 2026 plays them with
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Balance", { 0.29296875f, 0.64375f })) == le32 ({ -53, 206 }));
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Phase Inverter", { 1.0f })) == le32 ({ 1, 1024 }));
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Center", { 1.0f })) == le32 ({ 1, 1 }));
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Bass Boost", { 0.99609375f, 1.0f })) == le32 ({ 1, 1020, 1024 }));
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Mute 2", { 0.0f, 1.0f })) == le32 ({ 1, 0, 2 }));
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Fast LP", { 0.546875f, 0.3671875f })) == le32 ({ 1, 560, 376 }));
    {
        auto ff = le32 ({ 2, 262, 320, 121, 860 });
        ff.push_back (1); // CENTER on
        CHECK (wrappedFruityState (wrappedFruity ("Fruity Free Filter", { 0.26953125f, 0.3125f, 0.11812f, 0.83969f })) == ff);
    }
    {
        auto f = le32 ({ 2, 480, 641, 1024, 0, 760, 0 });
        f.push_back (1);
        CHECK (wrappedFruityState (wrappedFruity ("Fruity Filter", { 0.46875f, 0.62578f, 1.0f, 0.0f, 0.7421875f, 0.33f })) == f);
    }
    // the old lower-case spelling, and the flanger's twelve controls over their ranges
    CHECK (wrappedFruityState (wrappedFruity ("Fruity flanger", { 0.0f, 0.4f, 0.4f, 0.33f, 0.0f, 0.0f, 0.24f, 1.0f, 1.0f, 0.7f, 0.6f, 0.5f }))
           == le32 ({ 1, 0, 2000, 2000, 119, 0, 0, 24, 1024, 1024, 3500, 3000, 2500 }));
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Reeverb", { 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f }))
           == le32 ({ 1, 32768, 0, 250, 0, 0, 20, 0, 0, 0, 65536 }));
    // too few parameters, an unknown effect, an unreadable state: no native state (the effect keeps its defaults)
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Balance", { 0.5f })).empty());
    CHECK (wrappedFruityState (wrappedFruity ("Fruity Something", { 0.5f, 0.5f })).empty());
    {
        auto p = wrappedFruity ("Fruity Balance", { 0.5f, 0.5f });
        p.state.resize (3);
        CHECK (wrappedFruityState (p).empty());
    }
    // the rebuilt states are read by the exact engines
    {
        const auto slot = effectFromFl ("Fruity Balance", wrappedFruityState (wrappedFruity ("Fruity Balance", { 0.29296875f, 0.64375f })), 120.0);
        CHECK (slot && slot->kind == EffectKind::fruityBalance);
    }
    {
        const auto slot = effectFromFl ("Fruity Mute 2", wrappedFruityState (wrappedFruity ("Fruity Mute 2", { 0.0f, 1.0f })), 120.0);
        CHECK (slot && slot->kind == EffectKind::fruityMute2);
    }
    {
        const auto slot = effectFromFl ("Fruity Reeverb", wrappedFruityState (wrappedFruity ("Fruity Reeverb", { 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f })), 120.0);
        CHECK (slot && slot->kind == EffectKind::reeverb1);
    }
}

TEST (flq170i_render_tests_wrapped_fruity_effects)
{
    // FL's own render tests: every VST2-wrapped Fruity effect opens on its exact engine (none on the approximations)
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const std::pair<const char*, EffectKind> cases[] {
        { "Fruity Balance", EffectKind::fruityBalance },     { "Fruity Phase Inverter", EffectKind::flPhaseInverter },
        { "Fruity Center", EffectKind::flCenter },           { "Fruity Bass Boost", EffectKind::flBassBoost },
        { "Fruity Mute 2", EffectKind::fruityMute2 },        { "Fruity Fast LP", EffectKind::fruityFastLP },
        { "Fruity Free Filter", EffectKind::fruityFreeFilter }, { "Fruity Filter", EffectKind::fruityFilter },
        { "Fruity Flanger", EffectKind::fruityFlanger },     { "Fruity Chorus", EffectKind::fruityChorus },
        { "Fruity Reeverb", EffectKind::reeverb1 },          { "Fruity Compressor", EffectKind::fruityCompressor },
        { "Fruity Phaser", EffectKind::fruityPhaser },
    };
    int seen = 0;
    for (const auto& [name, kind] : cases)
    {
        std::ifstream in (std::filesystem::path (realFlpDir) / "System" / "Render tests" / (std::string (name) + ".flp"), std::ios::binary);
        std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        if (b.empty())
            continue;
        ++seen;
        FlpImportOptions options;
        const auto song = importFlp (flp::parseProject (b), options);
        bool found = false;
        for (const auto& t : song.mixer)
            for (const auto& fx : t.effects)
                found = found || fx.kind == kind;
        CHECK (found);
        if (! found)
            std::printf ("  %s: not on its exact engine\n", name);
    }
    CHECK (seen == 0 || seen == (int) std::size (cases));
}
