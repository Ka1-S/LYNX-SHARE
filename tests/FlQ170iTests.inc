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

TEST (flq170i_granulizer_and_3xosc_samples_on_posix_roots)
{
    // FL's render test "Fruity Granulizer" with the installation on a POSIX path (macOS / Linux, as renderwav and the
    // app there): the channel keeps its sample (it was prefixed with \Data again because the resolved path starts with
    // '/', and the Granulizer played nothing)
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const std::filesystem::path data (realFlpDir);
    std::ifstream in (data / "System" / "Render tests" / "Fruity Granulizer.flp", std::ios::binary);
    std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (b.empty() || data.string().empty() || data.string()[0] != '/')
        return; // (a Windows test run: the paths start with a drive letter)
    FlpImportOptions options;
    options.roots = { data.parent_path().string(), data.string(), "" };
    options.fileExists = [] (const std::string& p) {
        auto q = p;
        std::replace (q.begin(), q.end(), '\\', '/');
        return std::filesystem::exists (q);
    };
    const auto song = importFlp (flp::parseProject (b), options);
    CHECK (song.channels.size() == 1);
    if (song.channels.size() == 1)
    {
        CHECK (song.channels[0].instrument == InstrumentKind::granulizer);
        CHECK (! song.channels[0].samplePath.empty());
        CHECK (song.channels[0].samplePath.find ("808 Kick") != std::string::npos);
    }
}

TEST (flq170i_old_data_relative_plugin_sample_still_found)
{
    // an old preset's "\Patches\…" sample (relative to FL's Data folder) is still looked up there, on Windows roots
    using namespace lynx;
    flp::Project p;
    flp::Channel c;
    c.index = 0;
    c.type = flp::ChannelType::native;
    c.name = "Gran";
    c.pluginName = "Fruity Granulizer";
    c.samplePath = "\\Patches\\Packs\\k.wav";
    {
        std::vector<std::uint8_t> st (60, 0);
        st[0] = 5;
        c.pluginState = st;
    }
    p.channels.push_back (c);
    FlpImportOptions options;
    options.roots = { "C:\\FL", "C:\\FL\\Data", "" };
    std::vector<std::string> asked;
    options.fileExists = [&asked] (const std::string& path) { asked.push_back (path); return path == "C:\\FL\\Data\\Patches\\Packs\\k.wav"; };
    const auto song = importFlp (p, options);
    CHECK (song.channels.size() == 1);
    if (song.channels.size() == 1)
        CHECK (song.channels[0].samplePath == "C:\\FL\\Data\\Patches\\Packs\\k.wav");
}

TEST (flq170i_hardcore_box_type_and_cabinet_truncate)
{
    // Hardcore reads the box types and the cabinet with (int)v, as FL's plugin: FL's render test "Hardcore" stores box 5
    // as type 0.5 (on): an empty box in FL (its render nulls at −75 dB with it empty), a distortion in LYNX before
    using namespace lynx;
    for (int b = 0; b < hardcore::numBoxes; ++b)
    {
        const int p = hardcore::pBoxType + b;
        CHECK (hardcore::fromSlot (p, hardcore::toSlot (p, 0.5f)) == 0.0f);
        CHECK (hardcore::fromSlot (p, hardcore::toSlot (p, 0.99f)) == 0.0f);
        CHECK (hardcore::fromSlot (p, hardcore::toSlot (p, 6.0f)) == 6.0f);
        CHECK (hardcore::fromSlot (p, hardcore::toSlot (p, 6.7f)) == 6.0f);
        CHECK (hardcore::fromSlot (p, hardcore::toSlot (p, 11.0f)) == 11.0f);
    }
    CHECK (hardcore::fromSlot (hardcore::pCabinet, hardcore::toSlot (hardcore::pCabinet, 4.0f)) == 4.0f);
    CHECK (hardcore::fromSlot (hardcore::pCabinet, hardcore::toSlot (hardcore::pCabinet, 4.5f)) == 4.0f);
    CHECK (hardcore::fromSlot (hardcore::pCabinet, hardcore::toSlot (hardcore::pCabinet, 0.5f)) == 0.0f);
    if (realFlpDir.empty())
        return;
    std::ifstream in (std::filesystem::path (realFlpDir) / "System" / "Render tests" / "Hardcore.flp", std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (bytes.empty())
        return;
    FlpImportOptions options;
    const auto song = importFlp (flp::parseProject (bytes), options);
    bool found = false;
    for (const auto& t : song.mixer)
        for (const auto& fx : t.effects)
            if (fx.kind == EffectKind::hardcore)
            {
                found = true;
                const auto type = [&fx] (int b) { return hardcore::fromSlot (hardcore::pBoxType + b, fx.params[(std::size_t) (hardcore::pBoxType + b)]); };
                CHECK (type (0) == 1.0f); // distortion
                CHECK (type (1) == 7.0f); // noise gate
                CHECK (type (2) == 2.0f); // chorus
                CHECK (type (3) == 6.0f); // delay
                CHECK (type (4) == 0.0f); // empty (stored 0.5)
                CHECK (hardcore::fromSlot (hardcore::pCabinet, fx.params[(std::size_t) hardcore::pCabinet]) == 4.0f);
            }
    CHECK (found);
}

TEST (flq170i_slicex_velocity_reaches_the_level_through_fl_init_vol)
{
    // FL's host hands a plugin generator the note velocity as Init.Vol = (11^(v·0.01) − 1)/10 (unity at 100,
    // REA_rodada76 §1); FL's render test "Slicex" (every note at velocity 100) plays 2.14 dB louder than LYNX's linear
    // velocity did, and nulls in level with the law (REA_rodada170i §6)
    using namespace lynx;
    auto smp = std::make_shared<SampleData>();
    smp->sampleRate = 44100.0;
    smp->left.assign (44100, 0.25f);
    const std::shared_ptr<const SampleData> data = smp;
    auto level = [&data] (bool law, float velocity) {
        ChannelDef ch;
        ch.instrument = InstrumentKind::sampler;
        ch.sampler.flVelocityLaw = law;
        auto inst = makeInstrument (InstrumentKind::sampler, data);
        inst->prepare (44100.0);
        inst->configure (ch, 120.0);
        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        inst->noteOn (60, velocity);
        inst->render (l.data(), r.data(), 4096, 1.0f, 1.0f);
        return (double) std::abs (l[3000]);
    };
    const double linear = level (false, 100.0f / 128.0f), law = level (true, 100.0f / 128.0f);
    CHECK (linear > 0.0);
    CHECK (std::abs (law / linear - 1.28) < 1e-3);
    // 64 → (11^0.64 − 1)/10 ≈ 0.364 of unity
    CHECK (std::abs (level (true, 64.0f / 128.0f) / law - 0.3644) < 2e-3);
    if (realFlpDir.empty())
        return;
    std::ifstream in (std::filesystem::path (realFlpDir) / "System" / "Render tests" / "Slicex.flp", std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (bytes.empty())
        return;
    FlpImportOptions options;
    const auto song = importFlp (flp::parseProject (bytes), options);
    bool seen = false;
    for (const auto& c : song.channels)
        if (c.instrument == InstrumentKind::sampler)
            seen = seen || c.sampler.flVelocityLaw;
    CHECK (seen || song.channels.empty() || song.channels[0].samplePath.empty());
}
