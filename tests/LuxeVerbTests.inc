// LuxeVerb, exact (REA_rodada60): goldens of LuxeVerb_x64.dll (the installation's plugin, run unmodified) on our own
// version-9 states and this test's input, rendered in place with the rate set before the state, 44.1 kHz, blocks of 512
TEST (luxeverb_engine)
{
    auto hex = [] (const char* h) {
        std::vector<std::uint8_t> v;
        for (std::size_t i = 0; h[i] != 0 && h[i + 1] != 0; i += 2)
            v.push_back ((std::uint8_t) std::stoi (std::string (h + i, 2), nullptr, 16));
        return v;
    };
    constexpr int N = 8192;
    std::vector<float> inL (N), inR (N);
    std::uint32_t seed = 12345;
    for (int i = 0; i < N; ++i)
    {
        seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
        const auto nz = (float) ((double) seed / 2147483648.0 - 0.5);
        const auto tri = (float) ((i % 200) - 100) / 100.0f;
        const float g = i < 4000 ? 1.0f : 0.0f;
        inL[(std::size_t) i] = 0.5f * tri * g + (i >= 100 && i < 300 ? nz * 0.6f : 0.0f);
        inR[(std::size_t) i] = 0.25f * tri * g;
    }
    struct Golden
    {
        int i;
        float l, r;
    };
    auto run = [&] (const std::vector<std::uint8_t>& state, double bpm, const std::vector<Golden>& gold) {
        const auto slot = lynx::effectFromFl ("LuxeVerb", state, bpm);
        CHECK (slot.has_value() && slot->kind == lynx::EffectKind::luxeVerb);
        if (! slot || slot->kind != lynx::EffectKind::luxeVerb)
            return;
        auto fx = lynx::makeEffect (*slot);
        fx->prepare (44100.0);
        fx->reset();
        std::vector<float> l (inL), r (inR);
        for (int s = 0; s < N; s += 512)
            fx->process (l.data() + s, r.data() + s, 512, slot->params, bpm);
        for (const auto& g : gold)
            CHECK (l[(std::size_t) g.i] == g.l && r[(std::size_t) g.i] == g.r);
    };
    // A: feedback through the pitch shifter (+12 st), envelope → decay, synced predelay, modulation
    // B: feed-forward shifters (+7 / −12 st, synced first delay), envelope → wet, no limiter, low quality, width 200 %
    // C: reverb off, feedback −8.4 st (synced delay at 140 BPM), freeze mode "sustain", filters
    run (hex ("09000000000000000100000000ea020000bf320000140000000100000028230000a40100000c00000052030000460000003c0000009001000028000000e80300000000000001000000010000007800000084030000c00000008a020000b706000014000000580200002c010000a0f6ffff32000000900100003200000002000000000000002d0100000a000000ea020000840300002e1e000000000000430700007d00000001000000000000000000000000000000140000000000000014000000e8030000c0000000e8030000c00000000000"), 120.0,
         { { 777, 0.29935136437416077f, 0.14967568218708038f }, { 1555, 0.23220683634281158f, 0.10810191184282303f }, { 3333, 0.055371008813381195f, -0.018393725156784058f }, { 5000, -0.002418848918750882f, 0.027777602896094322f }, { 6500, -0.2116280049085617f, -0.2151600867509842f }, { 8191, -0.0015378956450149417f, 0.2623780071735382f } });
    run (hex ("09000000000000000000000000ea020000bf32000014000000010000003133000001000000c0000000520300003200000014000000640000000a000000e803000000000000000000000100000000000000e8030000c0000000bc020000b706000014000000e80300000cfeffff60f0ffff32000000900100003200000001000000000000002d0100000a000000ea020000ea020000b80b00008403000043070000c800000000000000010000002c01000007000000f4ffffff2003000058020000dc050000c00000002c010000c00000000100"), 120.0,
         { { 777, 0.29935136437416077f, 0.14967568218708038f }, { 1555, 0.2478388547897339f, 0.12373393774032593f }, { 3333, 0.16134345531463623f, 0.08045194298028946f }, { 5000, -0.002444570418447256f, -0.0005268710083328187f }, { 6500, -0.0038152404595166445f, 0.001154958619736135f }, { 8191, -0.004168912768363953f, 0.002039722865447402f } });
    run (hex ("09000000000000000001000000ea020000bf32000014000000000000003133000001000000c0000000520300003200000014000000640000000a000000e8030000020000000100000001000000acffffffe80300006000000084030000a00f00003c000000e80300000000000060f0ffff32000000900100003200000001000000000000002d0100000a000000c8000000ea0200002e1e000000000000430700007d00000001000000000000000000000000000000140000000000000014000000e8030000c0000000e8030000c00000000000"), 140.0,
         { { 777, 0.4329463243484497f, 0.1936376541852951f }, { 1555, 0.3382323980331421f, 0.1458330899477005f }, { 3333, 0.22464606165885925f, 0.09564708173274994f }, { 5000, -0.0027704837266355753f, -0.0011408103164285421f }, { 6500, -3.8662026781821623e-05f, -1.591997715877369e-05f }, { 8191, 0.09418727457523346f, 0.09418746829032898f } });

    // the plugin's control → engine mapping (bit patterns from the DLL)
    struct Map
    {
        int k, v;
        std::uint32_t bits;
    };
    for (const auto& m : std::vector<Map> { { 0, -600, 0xc2700000u }, { 0, 703, 0xbf6c19afu }, { 0, 1500, 0x41700000u }, { 1, 40, 0x42200000u }, { 1,
          12996, 0x466a6e22u }, { 1, 20000, 0x469c4000u }, { 4, 250, 0x3e800000u }, { 4, 5757, 0x3f87c9b8u }, { 4,
          20000, 0x41a00000u }, { 5, 156, 0x3ef9335cu }, { 5, 5000, 0x43fa0000u }, { 6, 192, 0x407effabu }, { 6, 768,
          0x41800000u }, { 8, 35, 0x3eb33333u }, { 9, 32, 0x41ffffffu }, { 9, 100, 0x42c80000u }, { 16, -120,
          0xc1400000u }, { 16, 120, 0x41400000u }, { 17, 2989, 0x43957333u }, { 23, -1000, 0xbf800000u }, { 23, 300,
          0x3e999998u }, { 24, -4002, 0xc220147bu }, { 27, 50, 0x00000000u }, { 30, 301, 0x426f8466u }, { 31, 10,
          0x3f800000u }, { 31, 1000, 0x42c80000u }, { 34, 2970, 0x44617c03u }, { 35, 258, 0x40251eb8u }, { 36, 486,
          0x3f30adb8u }, { 36, 5000, 0x40a00000u }, { 37, 120, 0x3f999999u }, { 41, -24, 0xc1c00000u }, { 41, 7,
          0x40e00000u }, { 13, 2, 0x40400000u }, { 28, 1, 0x40000000u } })
    {
        const float f = lynx::luxeverb::engineValue (m.k, m.v);
        std::uint32_t u = 0;
        std::memcpy (&u, &f, 4);
        CHECK (u == m.bits);
    }

    // state layouts: version 7 (38 controls), 8 (39), 9 (49 + feed-forward sync); unknown versions are refused
    std::vector<std::uint8_t> v7 (13 + 38 * 4, 0), v8 (13 + 39 * 4, 0);
    v7[0] = 7;
    v8[0] = 8;
    v7[8] = 1; // predelay sync
    v8[9] = 1; // feedback delay sync
    v7[13 + 4 * 4] = 0x7c; v7[13 + 4 * 4 + 1] = 0x16; // decay 5756
    v8[13 + 38 * 4] = 1;                             // limiter
    const auto s7 = lynx::luxeverb::parseState (v7);
    const auto s8 = lynx::luxeverb::parseState (v8);
    CHECK (s7 && s7->raw[4] == 5756 && s7->raw[38] == 1 && s7->raw[41] == 24 && s7->raw[43] == 1719 && s7->sync[0] && ! s7->sync[1]);
    CHECK (s8 && s8->raw[38] == 1 && s8->raw[42] == 20 && s8->sync[1] && ! s8->sync[2]);
    CHECK (! lynx::luxeverb::parseState ({ 10, 0, 0, 0 }).has_value());
    v7[0] = 10;
    CHECK (! lynx::luxeverb::parseState (v7).has_value());
    v8.resize (40);
    CHECK (! lynx::luxeverb::parseState (v8).has_value());

    // the parameters map back to FL's integers; texts in FL's units
    lynx::luxeverb::Settings s;
    s.raw[0] = -600; s.raw[16] = -84; s.raw[24] = -4000; s.raw[35] = 1800; s.sync = { true, false, true, false };
    lynx::EffectParams p {};
    lynx::luxeverb::toParams (s, p);
    CHECK (lynx::luxeverb::fromParams (p) == s);
    CHECK (lynx::luxeVerbParamText (0, 0.0f) == "-60.0 dB" && lynx::luxeVerbParamText (13, 0.5f) == "freeze");
    CHECK (lynx::luxeVerbParamText (49, 1.0f) == "on" && lynx::luxeVerbParamText (16, 1.0f) == "+12.0 st");
    CHECK (lynx::effectInfo (lynx::EffectKind::luxeVerb).params.size() == (std::size_t) lynx::luxeVerbAllParamCount); // + the Space engine (round tp172b)
}
