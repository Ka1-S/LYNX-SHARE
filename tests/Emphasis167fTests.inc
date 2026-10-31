// Round 167f (REA_rodada167f §1): FL's Emphasis on its own engine

namespace
{
std::vector<std::uint8_t> emphasisState (int version, std::array<int, 13> raw, std::array<std::uint8_t, 3> options)
{
    // FL's 0xD5 layout: i32 version · 13 i32 controls · i32 UI page · i32 (v ≥ 7) · 3 option bytes (v ≥ 9 / 10)
    std::vector<std::uint8_t> d (4 + 13 * 4 + 8, 0);
    std::memcpy (d.data(), &version, 4);
    std::memcpy (d.data() + 4, raw.data(), 13 * 4);
    const std::int32_t one = 1;
    std::memcpy (d.data() + 60, &one, 4);
    if (version >= 9)
        d.push_back (options[0]);
    if (version >= 10)
    {
        d.push_back (options[1]);
        d.push_back (options[2]);
    }
    return d;
}
} // namespace

TEST (emphasis_state_and_engine_values_match_fl)
{
    using namespace lynx;
    // the "Default" preset (version 10): the controls, the option bytes, FL's texts
    const auto def = emphasisState (10, { 0, 0, 1, 0, 0, 0, 0, 10000, 10000, 9912, 0, 2, 7 }, { 0, 1, 1 });
    const auto s = emphasis::parse (def);
    CHECK (s && s->raw == emphasis::rawDefault);
    auto slot = effectFromFl ("Emphasis", def);
    CHECK (slot && slot->kind == EffectKind::flEmphasis);
    if (slot)
        for (int k = 0; k < emphasis::numParams; ++k)
            CHECK (std::abs (slot->params[(std::size_t) k] - effectInfo (EffectKind::flEmphasis).params[(std::size_t) k].defaultValue) < 1e-6f);
    // versions before 8 store the envelope order 0-based; before 9 / 10 the option bytes are missing (off)
    const auto v7 = emphasis::parse (emphasisState (7, { 0, 0, 0, 0, 0, 0, 0, 10000, 10000, 9912, 0, 2, 7 }, {}));
    CHECK (v7 && v7->raw[2] == 1 && v7->raw[13] == 0 && v7->raw[14] == 0 && v7->raw[15] == 0);
    const auto v9 = emphasis::parse (emphasisState (9, { 0, 0, 3, 0, 0, 0, 0, 10000, 10000, 9912, 0, 2, 7 }, { 1, 1, 1 }));
    CHECK (v9 && v9->raw[2] == 3 && v9->raw[13] == 1 && v9->raw[14] == 0);
    CHECK (! emphasis::parse (std::vector<std::uint8_t> (20, 0)));

    // what FL's front end sends to the engine (EXEC: the plug-in's calls logged in our harness)
    CHECK (emphasis::engineValue (2, 4) == 3.0f);
    CHECK (emphasis::engineValue (6, 625) == 1.5f);
    CHECK (emphasis::engineValue (6, 2081) == 4.9944f);
    CHECK (emphasis::engineValue (7, 7475) == 0.7475f);
    CHECK (emphasis::engineValue (8, 5033) == 0.5033f);
    const std::pair<int, std::uint32_t> ceiling[] { { 1000, 0xc1fabac9u }, { 3000, 0xc19c6707u }, { 7000, 0xc0e6e0c7u },
                                                    { 9500, 0xbf938d0au }, { 9900, 0xbe6ae35bu }, { 9912, 0xbe4eabd5u } };
    for (const auto& [raw, bits] : ceiling)
    {
        const float v = emphasis::engineValue (9, raw);
        std::uint32_t u;
        std::memcpy (&u, &v, 4);
        CHECK (u == bits);
    }
    CHECK (emphasis::engineValue (9, 10000) == 0.0f && std::isinf (emphasis::engineValue (9, 0)));
    CHECK (formatEffectParam (EffectKind::flEmphasis, 5, 1.0f, 120.0) == "Steady");
    CHECK (formatEffectParam (EffectKind::flEmphasis, 11, 1.0f, 120.0) == "16x");
    CHECK (formatEffectParam (EffectKind::flEmphasis, 4, 1.0f, 120.0) == "M/S");
    CHECK (formatEffectParam (EffectKind::flEmphasis, 8, 0.0f, 120.0) == "Off");
    CHECK (formatEffectParam (EffectKind::flEmphasis, 2, 1.0f, 120.0) == "4");

    // the editor shows every control (plugin_layouts_name_real_parameters checks them all)
    CHECK (plugui::hasFlLayout (EffectKind::flEmphasis));
}

TEST (emphasis_matches_fl_engine)
{
    // FL's Emphasis through the import (state → slot → effect) against goldens of the installation's Emphasis_x64.dll
    // run in our harness (rate set before the state, 2304 samples of silence as FL's mixer runs it before a render,
    // 512-frame calls): FNV-1a of the output's float bits, at 44.1 and 48 kHz
    auto signal = [] {
        std::vector<float> l (24000), r (24000);
        std::uint32_t s = 12345;
        const float amps[] { 0.3f, 1.5f, 0.05f, 3.0f, 0.8f, 0.0f, 2.0f, 0.4f };
        for (int i = 0; i < 24000; ++i)
        {
            const float a = amps[(i / 3000) % 8];
            s = s * 1664525u + 1013904223u;
            l[(std::size_t) i] = (float) ((s >> 8) / 8388608.0 - 1.0) * a;
            s = s * 1664525u + 1013904223u;
            r[(std::size_t) i] = (float) ((s >> 8) / 8388608.0 - 1.0) * a;
        }
        return std::pair { l, r };
    };
    struct Case
    {
        const char* name;
        int version;
        std::array<int, 13> raw;
        std::array<std::uint8_t, 3> options;
        std::uint64_t hash44, hash48;
    };
    const Case cases[] {
        { "default", 10, { 0, 0, 1, 0, 0, 0, 0, 10000, 10000, 9912, 0, 2, 7 }, { 0, 1, 1 }, 0x6dbb0cdfd687f2abull, 0xf990cbf2ee5448bfull },
        { "M/S +6 dB (NIGHT TERROR)", 8, { 0, 0, 1, 0, 1, 0, 2499, 7475, 10000, 9900, 0, 1, 8 }, { 0, 0, 0 }, 0x5b8303e83cbc37e5ull, 0x2b8742c7006b815bull },
        { "steady, true peak, 4th order", 8, { 0, 0, 4, 1, 0, 3, 889, 10000, 10000, 9874, 0, 1, 6 }, { 0, 0, 0 }, 0xc665f77415828310ull, 0xdbe633988df90965ull },
        { "loud, 16x, level matching", 10, { 0, 0, 1, 0, 0, 1, 4000, 10000, 10000, 9912, 1, 4, 0 }, { 1, 0, 1 }, 0x9500799e32c644e2ull, 0xe2cbb82388789043ull },
        { "transient, no oversampling, delta", 10, { 0, 1, 2, 0, 0, 2, 6000, 10000, 5000, 9912, 0, 0, 9 }, { 0, 1, 0 }, 0x53159b307e2c57daull, 0x87aa8e9a0d1e1ec2ull },
        { "bypass", 10, { 1, 0, 1, 0, 0, 0, 3000, 10000, 10000, 9912, 0, 2, 7 }, { 0, 1, 1 }, 0xbe5984abc51cdaa6ull, 0xdaf3a86157b4b7cbull },
    };
    for (const auto& c : cases)
        for (const double rate : { 44100.0, 48000.0 })
        {
            const auto slot = lynx::effectFromFl ("Emphasis", emphasisState (c.version, c.raw, c.options));
            CHECK (slot && slot->kind == lynx::EffectKind::flEmphasis);
            if (! slot)
                continue;
            auto fx = lynx::makeEffect (*slot);
            fx->prepare (rate);
            auto [l, r] = signal();
            for (int i = 0; i < 24000; i += 512)
                fx->process (l.data() + i, r.data() + i, std::min (512, 24000 - i), slot->params, 120.0);
            std::uint64_t h = 0xcbf29ce484222325ull;
            for (int i = 0; i < 24000; ++i)
                for (float v : { l[(std::size_t) i], r[(std::size_t) i] })
                {
                    std::uint32_t u;
                    std::memcpy (&u, &v, 4);
                    h = (h ^ u) * 0x100000001b3ull;
                }
            if (h != (rate == 44100.0 ? c.hash44 : c.hash48))
                std::printf ("emphasis %s @ %.0f: 0x%016llx\n", c.name, rate, (unsigned long long) h);
            CHECK (h == (rate == 44100.0 ? c.hash44 : c.hash48));
        }
}

TEST (emphasis_reports_its_latency_before_the_first_block)
{
    // FL's engine reports 2381 samples for the Default preset at 44.1 kHz (4x oversampling); LYNX knows it from the
    // slot's controls before the first block (PDC) and keeps it after
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::flEmphasis);
    auto fx = makeEffect (slot);
    fx->prepare (44100.0);
    CHECK (fx->latencySamples() == 2381);
    std::vector<float> l (512, 0.0f), r (512, 0.0f);
    fx->process (l.data(), r.data(), 512, slot.params, 120.0);
    CHECK (fx->latencySamples() == 2381);
}
