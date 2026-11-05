// Round 170i2: FPC's level law, Slicex slice starts, EQUO in FL's command-line render (docs/analise/REA_rodada170i.md
// §7–§9: FL renders of probe projects made from FL's render tests)

TEST (flq170i2_slice_starts_on_their_exact_frame)
{
    // a slice stored as float(p / total) a hair below p / total started one frame early (floor): FL's render test
    // "Slicex" played every slice after the first one sample late against FL
    using namespace lynx;
    constexpr int total = 100000;
    int p = -1;
    for (int q = 1; q < total && p < 0; ++q)
        if (std::floor ((double) total * (double) (float) ((double) q / total)) < (double) q)
            p = q;
    CHECK (p > 0);
    if (p <= 0)
        return;
    auto ramp = std::make_shared<SampleData>();
    ramp->sampleRate = 44100.0;
    for (int i = 0; i < total; ++i)
        ramp->left.push_back ((float) i / (float) total);
    auto inst = makeInstrument (InstrumentKind::sampler, ramp);
    inst->prepare (44100.0);
    ChannelDef c;
    c.sampler.slices = { 0.0f, (float) ((double) p / total) };
    inst->configure (c, 120.0);
    inst->noteOn (61, 1.0f);
    std::vector<float> l (64), r (64);
    inst->render (l.data(), r.data(), 64, 1.0f, 1.0f);
    CHECK (std::abs ((double) l[0] * total - (double) p) < 0.01);
}

TEST (flq170i2_fpc_volume_law_and_velocity)
{
    // FL's renders of FL's render test "FPC" and of pad-volume 64 / layer-volume 127 / hard-left / velocity-64
    // variants: the kick's gain is (21.527^(v/127) − 1)/10 for the pad and the layer, × 1/(2·√2), with the velocity
    // through FL's Init.Vol law (unity at 100) — 0.000 dB on all of them (REA_rodada170i §7)
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    std::ifstream in (std::filesystem::path (realFlpDir) / "System" / "Render tests" / "FPC.flp", std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (bytes.empty())
        return;
    const auto project = flp::parseProject (bytes);
    const flp::Channel* fpc = nullptr;
    for (const auto& c : project.channels)
        if (c.pluginName == "FPC")
            fpc = &c;
    CHECK (fpc != nullptr);
    if (fpc == nullptr)
        return;
    const auto pads = fpcPads (fpc->pluginState);
    CHECK (pads.has_value());
    if (! pads)
        return;
    const auto law = [] (int v) { return (std::exp ((double) v / 127.0 * std::log (21.526958)) - 1.0) / 10.0; };
    bool kick = false;
    for (const auto& pad : *pads)
        if (pad.name == "Kick Drum")
        {
            kick = true;
            CHECK (std::abs (pad.gain / (law (128) / (2.0 * std::sqrt (2.0))) - 1.0) < 1e-5);
            CHECK (! pad.layers.empty());
            for (const auto& layer : pad.layers)
                CHECK (std::abs (layer.gain / law (99) - 1.0) < 1e-5);
        }
    CHECK (kick);
    FlpImportOptions options;
    const auto song = importFlp (project, options);
    bool seen = false;
    for (const auto& c : song.channels)
        if (c.name.find ("Kick Drum") != std::string::npos)
        {
            seen = true;
            CHECK (c.sampler.flVelocityLaw);
            CHECK (! c.zones.empty());
            for (const auto& z : c.zones)
                CHECK (std::abs (z.gain / (law (128) * law (99) / (2.0 * std::sqrt (2.0))) - 1.0) < 1e-5);
        }
    CHECK (seen);
}

TEST (flq170i2_equo_render_is_not_hq_and_glides_from_construction)
{
    // FL's command-line render of FL's render test "EQUO" uses the plain band design (not the "HQ non-realtime" one)
    // and its motion smoother glides from FL's construction values after the load; with motion smoothness 0 it starts
    // on the loaded curve (FL render −87 dB against LYNX, REA_rodada170i §8)
    using namespace lynx;
    std::vector<std::uint8_t> st (4143, 0);
    auto put = [&st] (std::size_t at, std::int32_t v) { std::memcpy (st.data() + at, &v, 4); };
    put (0, 4);
    for (int b = 0; b < 31; ++b)
        for (int t = 0; t < 3; ++t)
        {
            const float v = t == 0 ? 0.5f + 0.03f * (float) b : 1.0f;
            std::memcpy (st.data() + 4 + 8 * (std::size_t) (3 * b + t), &v, 4);
        }
    for (int bank = 0; bank < 9; ++bank)
        for (int i = 0; i < 93; ++i)
        {
            const float v = bank == 8 ? 1.0f : (i < 31 ? 0.4f + 0.04f * (float) (i % 31) : 1.0f);
            std::memcpy (st.data() + 748 + 4 * (std::size_t) (93 * bank + i), &v, 4);
        }
    put (4096, 0), put (4100, 128), put (4104, 0), put (4108, 170), put (4112, 128);
    put (4116, 0), put (4120, 150), put (4124, -1);
    const auto s = equofx::parse (st);
    CHECK (s.has_value());
    if (! s)
        return;
    std::vector<float> inL (32768), inR (32768);
    for (int n = 0; n < 32768; ++n)
        inL[(std::size_t) n] = inR[(std::size_t) n] = (float) std::sin (0.05 * n) * 0.5f;
    auto run = [&] (int smooth, bool offline, bool hqAll) {
        auto slot = effectFromFl ("EQUO", st, 120.0);
        CHECK (slot && slot->kind == EffectKind::equo);
        auto k = *s;
        k.knob[3] = smooth;
        equofx::toParams (k, slot->params);
        setOfflineRendering (offline);
        setHqForAllPlugins (hqAll);
        auto fx = makeEffect (*slot);
        fx->prepare (44100.0);
        std::vector<float> l (inL), r (inR);
        for (int i = 0; i < 32768; i += 512)
            fx->process (l.data() + i, r.data() + i, 512, slot->params, 120.0);
        setOfflineRendering (false);
        setHqForAllPlugins (false);
        return l;
    };
    const auto live = run (170, false, false), render = run (170, true, false), hq = run (170, true, true);
    CHECK (live == render); // an export without "HQ for all plugins" sounds like playback
    CHECK (hq != render);   // HQ for all plugins: the doubled design
    // the glide: with smoothness the first block is not yet the settled curve; without it, it is
    const auto still = run (0, true, false);
    double early = 0, late = 0;
    for (int i = 0; i < 512; ++i)
        early += std::abs ((double) render[(std::size_t) i] - still[(std::size_t) i]);
    for (int i = 32256; i < 32768; ++i)
        late += std::abs ((double) render[(std::size_t) i] - still[(std::size_t) i]);
    CHECK (early > 10.0 * late);
}

TEST (flq170i2_drumaxx_pitched_up_at_low_rates_stays_in_its_buffer)
{
    // tools/genfuzz: at 22.05 kHz a pad with Pitch at max and the channel pitched up steps over 4 internal samples per
    // host sample, and a 256-sample pass overran the DLL-sized 1024-sample buffer into the next pad (crash in reset)
    using namespace lynx;
    auto c = banks::defaultChannel (InstrumentKind::drumaxx, "Drumaxx");
    CHECK (c.has_value());
    if (! c)
        return;
    for (int k = 0; k < 16; ++k)
        drumaxxSetParam (c->pluginState, 44 * k + 34, 1.0f);
    auto in = makeDrumaxxInstrument();
    in->prepare (22050.0);
    in->configure (*c, 120.0);
    in->setChannelPitchNow (1200.0f, 0.0f);
    std::vector<float> l (4096), r (4096);
    double peak = 0.0;
    bool finiteOut = true;
    for (int round = 0; round < 3; ++round)
    {
        for (int key = 36; key <= 51; ++key)
            in->noteOn (key, 1.0f);
        for (int b = 0; b < 12; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
            in->render (l.data(), r.data(), 4096, 1.0f, 1.0f);
            for (int i = 0; i < 4096; ++i)
            {
                finiteOut = finiteOut && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
                peak = std::max ({ peak, (double) std::abs (l[(std::size_t) i]), (double) std::abs (r[(std::size_t) i]) });
            }
        }
        for (int key = 36; key <= 51; ++key)
            in->noteOff (key);
    }
    CHECK (finiteOut);
    CHECK (peak > 1e-3 && peak < 16.0);
}
