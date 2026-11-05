// Round 169l2: every new generator is FL's Plugin database state when FL is installed (REA_rodada169l2)

namespace gen169l2
{
// the channel's sound data, as the engines keep it
inline bool sameSound (const lynx::ChannelDef& a, const lynx::ChannelDef& b)
{
    return a.instrument == b.instrument && a.pluginState == b.pluginState && a.modelParams == b.modelParams
           && a.samplePath == b.samplePath && a.zones.size() == b.zones.size();
}
} // namespace gen169l2

// The engines whose Default preset differs from FL's Plugin database state (user decision, round 169l2): a new one is
// the database entry (FL's picker / "+" load it), not the Default preset
TEST (gen169l2_new_generators_are_the_plugin_database_entry)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    for (const auto kind : { InstrumentKind::threeosc, InstrumentKind::sytrus, InstrumentKind::harmor, InstrumentKind::harmless,
                             InstrumentKind::poizone, InstrumentKind::minisynth, InstrumentKind::sawer, InstrumentKind::flkeys,
                             InstrumentKind::granulizer, InstrumentKind::toxic, InstrumentKind::beepmap })
    {
        const auto* e = natives::instrumentEngine (kind);
        CHECK (e != nullptr);
        if (e == nullptr)
            continue;
        const auto c = natives::presetCandidates (e->presetFolder, true, fl);
        const auto first = c.empty() ? std::string() : c.front();
        const auto fromDb = first.find ("Plugin database") != std::string::npos
                            && std::filesystem::path (first).filename().string() == std::string (e->presetFolder) + ".fst";
        if (! fromDb)
            std::printf ("  %s starts from %s\n", e->name, first.c_str());
        CHECK (fromDb);
        // the Default preset still follows it (a broken database entry falls back to it)
        CHECK (c.size() > 1 && c[1].find ("Plugin presets") != std::string::npos
               && std::filesystem::path (c[1]).stem().string() == "Default");
        const auto made = natives::makeChannel (kind, fl);
        const auto fromDbFile = natives::channelFromPresetFile (kind, first, fl);
        const auto fromDefault = c.size() > 1 ? natives::channelFromPresetFile (kind, c[1], fl) : std::nullopt;
        CHECK (made.has_value() && fromDbFile.has_value());
        if (made && fromDbFile)
        {
            if (! gen169l2::sameSound (*made, *fromDbFile))
                std::printf ("  %s: the new channel is not the database state\n", e->name);
            CHECK (gen169l2::sameSound (*made, *fromDbFile));
            CHECK (! made->pluginState.empty() || ! made->modelParams.empty());
            // ... which is not the Default preset (FL's files differ: REA_rodada169l §3)
            if (fromDefault && gen169l2::sameSound (*made, *fromDefault))
                std::printf ("  %s: database = Default as LYNX imports them\n", e->name);
        }
    }
    // where the Default preset IS the database state, a new channel sounds the same as before (Kepler, DX10, SimSynth)
    for (const auto kind : { InstrumentKind::kepler, InstrumentKind::dx10, InstrumentKind::simsynth })
    {
        const auto* e = natives::instrumentEngine (kind);
        if (e == nullptr)
            continue;
        const auto c = natives::presetCandidates (e->presetFolder, true, fl);
        CHECK (c.size() > 1);
        if (c.size() < 2)
            continue;
        const auto a = natives::channelFromPresetFile (kind, c[0], fl), b = natives::channelFromPresetFile (kind, c[1], fl);
        CHECK (a.has_value() && b.has_value() && gen169l2::sameSound (*a, *b));
    }
}

// Without FL every generator keeps LYNX's bank default (round 168n), and an engine whose database entry LYNX can't
// read falls through to its Default preset / folder (Wave Traveller)
TEST (gen169l2_without_fl_the_lynx_bank_and_unreadable_entries_fall_through)
{
    using namespace lynx;
    natives::FlAccess none;
    for (const auto kind : { InstrumentKind::sytrus, InstrumentKind::harmor, InstrumentKind::poizone, InstrumentKind::toxic })
    {
        CHECK (natives::presetCandidates (natives::presetFolderOf (kind), true, none).empty());
        const auto made = natives::makeChannel (kind, none);
        const auto bank = banks::defaultChannel (kind, natives::instrumentEngine (kind)->name);
        CHECK (made.has_value() == bank.has_value());
        if (made && bank)
            CHECK (gen169l2::sameSound (*made, *bank));
    }
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    const auto c = natives::presetCandidates ("Wave Traveller", true, fl);
    CHECK (! c.empty() && c.front().find ("Plugin database") != std::string::npos);
    CHECK (natives::makeChannel (InstrumentKind::wavetraveller, fl).has_value());
}

// A new Granulizer / Wave Traveller is FL's database entry, which names no sample: silent, as FL renders it
TEST (gen169l2_new_granulizer_and_wave_traveller_have_no_sample_like_fl)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    for (const auto kind : { InstrumentKind::granulizer, InstrumentKind::wavetraveller })
    {
        const auto made = natives::makeChannel (kind, fl);
        CHECK (made.has_value());
        if (! made)
            continue;
        CHECK (made->instrument == kind && made->samplePath.empty() && ! made->pluginState.empty());
        const auto first = natives::presetCandidates (natives::presetFolderOf (kind), true, fl);
        const auto fromDb = first.empty() ? std::nullopt : natives::channelFromPresetFile (kind, first.front(), fl);
        CHECK (fromDb.has_value() && gen169l2::sameSound (*made, *fromDb));
    }
}

// PoiZone's chorus / delay ring on after the last voice (FL's render of its database patch: echoes falling ≈ 6 dB per
// 0.5 s): the instrument stays busy while they sound, so the renderer (which skips idle channels) neither cuts them
// nor replays a stale delay line when the channel wakes up later (the burst at other channels' notes, round 169l2)
TEST (gen169l2_poizone_delay_tail_rings_out_before_idle)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    const auto made = natives::makeChannel (InstrumentKind::poizone, fl);
    CHECK (made.has_value());
    if (! made)
        return;
    const double rate = 44100.0;
    auto in = makePoiZoneInstrument();
    in->prepare (rate);
    in->configure (*made, 120.0);
    std::vector<float> l (256), r (256);
    const float g = 0.70710678f;
    const auto block = [&] { std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
                             in->render (l.data(), r.data(), 256, g, g);
                             float p = 0.0f;
                             for (int i = 0; i < 256; ++i) p = std::max ({ p, std::fabs (l[(std::size_t) i]), std::fabs (r[(std::size_t) i]) });
                             return p; };
    block();
    in->noteOn (60, 100.0f / 128.0f);
    for (int k = 0; k < (int) (0.5 * rate) / 256; ++k)
        block();
    in->noteOff (60);
    int blocks = 0;
    float tailPeak = 0.0f; // the loudest block from 1 s after the note off on
    while (! in->idle() && blocks < (int) (20.0 * rate) / 256)
    {
        const auto p = block();
        if (blocks++ * 256 > (int) rate)
            tailPeak = std::max (tailPeak, p);
    }
    std::printf ("  PoiZone idle %.2f s after the note off, tail peak from 1 s %.1f dB\n", blocks * 256 / rate,
                 20.0 * std::log10 ((double) tailPeak + 1e-12));
    CHECK (in->idle());
    CHECK (blocks * 256 > (int) (1.5 * rate)); // (the echoes: FL's are still at ≈ −40 dB 1.5 s after the note off)
    CHECK (tailPeak > 1.0e-3f);
    // woken later (another channel's note in the same cut group, an automation...), it has nothing left to play
    for (int k = 0; k < 4; ++k)
        CHECK (block() < 2.0e-6f);
}
