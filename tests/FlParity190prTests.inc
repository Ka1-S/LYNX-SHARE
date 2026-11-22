// Round 190pr (0.1.32): the FL demo song sweep, each change checked against FL 2026's own renders and the installation's
// DLLs (REA_rodada190pr): Harmor's parameters under FL's automation (numbers, raw ranges, the filter frequencies and
// Mod X / Y / Z live from engine revision 3 on), FL's song-start values (0xD8) of Harmor / Sytrus parameters written into
// their states, and the raw ranges of FL's own effects that the demos' song-start values and pattern events use.
#include "core/Harmor.h"
#include "core/SytrusEngine.h"
#include <cmath>

namespace flparity190pr
{
double rmsDb (const std::vector<float>& x, std::size_t from, std::size_t to, int stride = 1)
{
    double s = 0.0;
    std::size_t n = 0;
    for (auto i = from; i < to && i < x.size(); i += (std::size_t) stride, ++n)
        s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double) std::max<std::size_t> (n, 1) + 1e-30);
}

std::vector<std::uint8_t> readFile (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), {});
}

std::filesystem::path demos()
{
    const auto d = std::filesystem::path (realFlpDir) / "Data" / "Demo projects";
    return std::filesystem::exists (d) ? d : std::filesystem::path (realFlpDir) / "Demo projects";
}
} // namespace flparity190pr

TEST (flparity190pr_harmor_fl_parameter_numbers_and_ranges)
{
    using namespace lynx;
    // FL's numbering [DECOMP FUN_00879380]: parts A / B 386 parameters each, then the 53 global ones
    CHECK (harmor::flParamSlot (52) == std::make_optional (std::pair { 0, 52 }));
    CHECK (harmor::flParamSlot (438) == std::make_optional (std::pair { 1, 52 }));
    CHECK (harmor::flParamSlot (772) == std::make_optional (std::pair { -1, 0 }));
    CHECK (harmor::flParamSlot (824) == std::make_optional (std::pair { -1, 52 }));
    CHECK (! harmor::flParamSlot (825) && ! harmor::flParamSlot (-1));
    // the DLL's own ranges [EXEC]: filter frequency 0…8192, Mod X 0…512, part volume −2000…2000, unison order 1…9
    CHECK (harmor::flParamRange (52) == std::make_optional (std::pair { 0, 8192 }));
    CHECK (harmor::flParamRange (450) == std::make_optional (std::pair { 0, 8192 }));
    CHECK (harmor::flParamRange (772) == std::make_optional (std::pair { 0, 512 }));
    CHECK (harmor::flParamRange (11) == std::make_optional (std::pair { -2000, 2000 }));
    CHECK (harmor::flParamRange (34) == std::make_optional (std::pair { 1, 9 }));
    // raw = min + Round (v · (max − min)), half to even [EXEC: −2…7 at 0.5 → 2, −7…8 → 1, 0…999 → 500]
    CHECK (harmor::flRawOf (30, 0.5) == 2 && harmor::flRawOf (31, 0.5) == 1 && harmor::flRawOf (0, 0.5) == 500);
    CHECK (harmor::flRawOf (52, 1.0) == 8192 && harmor::flRawOf (52, 0.0) == 0 && harmor::flRawOf (52, 0.5) == 4096);
    // the articulators' settings (part parameters 95…342) are not plain values of the state
    CHECK (harmor::flParamInState (52) && harmor::flParamInState (94) && ! harmor::flParamInState (95)
           && ! harmor::flParamInState (342) && harmor::flParamInState (343) && ! harmor::flParamInState (386 + 100)
           && harmor::flParamInState (772));
    CHECK (harmor::automatable (52) && harmor::automatable (64) && harmor::automatable (438) && harmor::automatable (450)
           && harmor::automatable (772) && harmor::automatable (774) && ! harmor::automatable (775) && ! harmor::automatable (53));
    CHECK (ChannelDef {}.engineRev == 3);
}

TEST (flparity190pr_harmor_follows_filter_and_xyz_automation)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    // Lightning's "pling riser" (FL channel 50, no rack compressor): DLL renders of a held note with ProcessParam changes
    // match LYNX's reference renderer within −87 dB (EXEC, rea/hm: p_cut); here the reference renderer's own laws
    const auto song = flparity190pr::demos() / "Product demos" / "Square a Saw - Lightning (VEELA Vocals Resynthesized).flp";
    if (! std::filesystem::exists (song))
        return;
    const auto project = flp::parseProject (flparity190pr::readFile (song));
    std::vector<std::uint8_t> state;
    for (const auto& c : project.channels)
        if (c.index == 50)
            state = c.pluginState;
    if (! harmorStateUsable (state))
        return;
    const auto withValue = [&] (int value) {
        auto s = state;
        const auto at = harmorParamOffset (s, 0, 52);
        CHECK (at >= 0);
        std::memcpy (s.data() + at, &value, 4);
        return s;
    };
    const std::vector<harmor::RefNote> note { { 0, 72, 0.78, 1.0, 0.0, -1 } };
    // a change before the note = the same value saved in the state (the knob's two laws agree within float rounding)
    const auto saved = harmor::renderReference (withValue (4000), note, 44100.0, 22050, 512, {}, 120.0, {}, 1);
    const auto automated = harmor::renderReference (state, note, 44100.0, 22050, 512, {}, 120.0, {}, 1, { { 0, 52, 4000 } });
    CHECK (saved.size() == automated.size() && ! saved.empty());
    std::vector<float> diff (saved.size());
    for (std::size_t i = 0; i < saved.size(); ++i)
        diff[i] = saved[i] - automated[i];
    const auto residual = flparity190pr::rmsDb (diff, 0, diff.size()) - flparity190pr::rmsDb (saved, 0, saved.size());
    std::printf ("  pling riser, filter 1 at 4000 saved vs automated before the note: residual %.1f dB\n", residual);
    CHECK (residual < -100.0);
    // a change while the note sounds: the level goes to the new value's (FL: 8192 → −13 dB, 1000 → −35 dB here)
    const auto sweep = harmor::renderReference (state, note, 44100.0, 88200, 512, {}, 120.0, {}, 1, { { 22050, 52, 8192 }, { 44100, 52, 1000 } });
    const auto open = flparity190pr::rmsDb (sweep, 2 * 33075, 2 * 44100), shut = flparity190pr::rmsDb (sweep, 2 * 66150, 2 * 88200);
    CHECK (open - shut > 10.0);
    // other parameters are not moved by the reference renderer's changes (only harmor::automatable ones)
    const auto other = harmor::renderReference (state, note, 44100.0, 22050, 512, {}, 120.0, {}, 1, { { 0, 53, 1 } });
    const auto plain = harmor::renderReference (state, note, 44100.0, 22050, 512, {}, 120.0, {}, 1);
    CHECK (other == plain);

    // the instrument: engine revision 3 follows FL's automation, revision 2 (a project saved before) ignores it, bit for bit
    ChannelDef ch;
    ch.instrument = InstrumentKind::harmor;
    ch.pluginState = state;
    const auto play = [&] (int rev, bool automate) {
        ch.engineRev = rev;
        auto in = makeInstrument (InstrumentKind::harmor);
        in->prepare (44100.0);
        in->configure (ch, 120.0);
        in->settleNew();
        std::vector<float> l (88200), r (88200);
        in->noteOn (72, 0.78125f); // (velocity 100: Init.Vol 1)
        in->render (l.data(), r.data(), 44100, 0.7071f, 0.7071f);
        if (automate)
            in->setParameter (52, 1.0f); // filter 1 wide open
        in->render (l.data() + 44100, r.data() + 44100, 44100, 0.7071f, 0.7071f);
        return l;
    };
    const auto old = play (2, false), oldAutomated = play (2, true), now = play (3, false), nowAutomated = play (3, true);
    CHECK (old == oldAutomated);
    CHECK (std::abs (flparity190pr::rmsDb (old, 0, 88200) - flparity190pr::rmsDb (now, 0, 88200)) < 1.0e-4); // (velocity 100: Init.Vol 1)
    CHECK (flparity190pr::rmsDb (nowAutomated, 66150, 88200) - flparity190pr::rmsDb (now, 66150, 88200) > 10.0);

    // FL's link velocity (Harmor never asks voice event 6): Final.Vol = Init.Vol × channel volume from revision 3 on —
    // velocity 128 is +6.25 dB against 100, velocity 88 −2.79 dB (FL's render of Lightning's "sub bass", FL channel 16, a
    // patch that ignores the velocity itself [EXEC: the DLL at 0.5 / 0.78 / 1.0, the same level]: +6.3 / −2.8 dB)
    for (const auto& c : project.channels)
        if (c.index == 16)
            ch.pluginState = c.pluginState;
    const auto atVelocity = [&] (int rev, float velocity) {
        ch.engineRev = rev;
        auto in = makeInstrument (InstrumentKind::harmor);
        in->prepare (44100.0);
        in->configure (ch, 120.0);
        in->settleNew();
        std::vector<float> l (44100), r (44100);
        in->noteOn (27, velocity);
        in->render (l.data(), r.data(), 44100, 0.7071f, 0.7071f);
        return flparity190pr::rmsDb (l, 4410, 44100);
    };
    CHECK (std::abs (atVelocity (3, 1.0f) - atVelocity (3, 0.78125f) - 6.246) < 0.01);
    CHECK (std::abs (atVelocity (3, 88.0f / 128.0f) - atVelocity (3, 0.78125f) + 2.794) < 0.01);
    CHECK (atVelocity (2, 1.0f) == atVelocity (2, 0.78125f)); // (this patch ignores the velocity itself)
}

TEST (flparity190pr_song_start_values_of_harmor_and_sytrus)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    // Lightning: "saw chords" (FL channel 32) is saved with its filter shut (0) and FL starts the song at 8192 (0xD8): the
    // FL render plays it open from bar 1 (solo RMS vs FL −31.2 → −0.5 dB). The import writes the value into the state
    const auto song = flparity190pr::demos() / "Product demos" / "Square a Saw - Lightning (VEELA Vocals Resynthesized).flp";
    if (! std::filesystem::exists (song))
        return;
    const auto project = flp::parseProject (flparity190pr::readFile (song));
    FlpImportReport report;
    FlpImportOptions options;
    options.report = &report;
    const auto s = importFlp (project, options);
    const auto find = [&] (const std::string& name) -> const ChannelDef* {
        for (const auto& c : s.channels)
            if (c.name == name)
                return &c;
        return nullptr;
    };
    const auto* saw = find ("saw chords");
    CHECK (saw != nullptr && saw->instrument == InstrumentKind::harmor);
    if (saw == nullptr)
        return;
    const auto at = harmorParamOffset (saw->pluginState, 0, 52);
    std::int32_t value = -1;
    if (at >= 0)
        std::memcpy (&value, saw->pluginState.data() + at, 4);
    CHECK (value == 8192);
    CHECK (report.converted["Harmor: song-start value of a parameter"] == 8);
    CHECK (report.converted["Sytrus: song-start value of a main-page knob"] == 2); // ("fm bass": Mod X 123, Mod Y 256)
    CHECK (report.notReproduced.count ("Pattern events: value scale not known (Harmor)") == 0);
    if (const auto* fm = find ("fm bass"))
    {
        const auto& table = sytrus::settings();
        for (int k = 0; k < (int) table.size(); ++k)
            if (table[(std::size_t) k].index == 190)
                CHECK (sytrus::settingValue (fm->pluginState, k) == std::make_optional (123));
    }
}

TEST (flparity190pr_fl_effect_raw_ranges)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    // FL's own effects' song-start values (0xD8) now reach the effects: none of the demos keeps a "value scale not known"
    // line for Fruity Balance, Fruity Filter, Fast LP, Free Filter, WaveShaper, PanOMatic, Delay 2 / 3, Limiter, PEQ 2
    const char* const fixed[] = { "Fruity Balance", "Fruity Filter", "Fruity Fast LP", "Fruity Free Filter", "Fruity WaveShaper",
                                  "Fruity PanOMatic", "Fruity Delay 2", "Fruity Delay 3", "Fruity Limiter", "Fruity Parametric EQ 2" };
    int songs = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (flparity190pr::demos()))
    {
        if (entry.path().extension() != ".flp")
            continue;
        FlpImportReport report;
        FlpImportOptions options;
        options.report = &report;
        try
        {
            importFlp (flp::parseProject (flparity190pr::readFile (entry.path())), options);
        }
        catch (...)
        {
            continue;
        }
        ++songs;
        for (const auto* f : fixed)
            CHECK (report.notReproduced.count (std::string ("Pattern events: value scale not known (") + f + ")") == 0);
    }
    std::printf ("  %d demo songs imported\n", songs);
}

TEST (flparity190pr_export_follows_start_skip_and_pause_markers)
{
    // FL 2026's /R renders (REA_rodada190pr §4): Umziky starts at its Start marker wherever it is (1536, 2976 or 3456 ticks;
    // made a plain marker: bar 1), Raubana – LIFE jumps over its Marker skip sections; a Marker pause does not stop it
    using namespace markers165;
    auto song = eightBars();
    song.addMarker (marker (bar (2), lynx::Marker::start, "Start"));
    song.addMarker (marker (bar (3), lynx::Marker::markerSkip, "Skip"));
    song.addMarker (marker (bar (5), lynx::Marker::none, "Here"));
    song.addMarker (marker (bar (6), lynx::Marker::markerLoop, "Loop"));
    song.addMarker (marker (bar (7), lynx::Marker::markerPause, "Pause"));
    using Spans = std::vector<std::pair<std::int64_t, std::int64_t>>;
    // off (a project saved before 0.1.32): the whole song, straight through, as before
    CHECK (lynx::exportopts::exportSpans (song) == (Spans { { 0, bar (9) } }));
    std::int64_t music = 0, loop = 0;
    const auto before = lynx::exportopts::makeRenderSong (song, {}, 0, music, loop);
    CHECK (music == song.songLength() && ! before.playlist.empty());
    // on: bars 2…3, then 5 to the end (the marker loop at 6 and the pause at 7 are not followed: FL's render goes on)
    song.exportMarkers = true;
    CHECK (lynx::exportopts::exportSpans (song) == (Spans { { bar (2), bar (3) }, { bar (5), bar (9) } }));
    const auto render = lynx::exportopts::makeRenderSong (song, {}, 0, music, loop);
    CHECK (music == bar (2) + bar (5) - bar (1)); // 1 + 4 bars
    CHECK (std::any_of (render.playlist.begin(), render.playlist.end(), [] (const lynx::Clip& c) { return c.start == 0 && c.offset == bar (2); }));
    CHECK (std::any_of (render.playlist.begin(), render.playlist.end(), [] (const lynx::Clip& c) { return c.start == bar (2) && c.offset == bar (5); }));
    CHECK (lynx::makeMarkerPlan (render).count == 0);
    // skips chain: a skip that lands on another skip marker follows it too (Raubana – LIFE: 30720 → 43008 → 49152 → 52224)
    auto chained = eightBars();
    chained.exportMarkers = true;
    chained.addMarker (marker (bar (2), lynx::Marker::markerSkip, "S1"));
    chained.addMarker (marker (bar (3), lynx::Marker::markerSkip, "S2"));
    chained.addMarker (marker (bar (5), lynx::Marker::none, "Here"));
    CHECK (lynx::exportopts::exportSpans (chained) == (Spans { { 0, bar (2) }, { bar (5), bar (9) } }));
    // a Start marker alone: from there to the end; none: bar 1 (the selection range is unchanged by the switch)
    auto start = eightBars();
    start.exportMarkers = true;
    start.addMarker (marker (bar (4), lynx::Marker::start, "Start"));
    CHECK (lynx::exportopts::exportSpans (start) == (Spans { { bar (4), bar (9) } }));
    auto plain = eightBars();
    plain.exportMarkers = true;
    plain.addMarker (marker (bar (4), lynx::Marker::none, "Here"));
    CHECK (lynx::exportopts::exportSpans (plain) == (Spans { { 0, bar (9) } }));
    // .lynx: saved only when on; a project without the key loads with it off
    CHECK (lynx::Song {}.exportMarkers == false);
}

TEST (flparity190pr_sytrus_takes_the_notes_mod_x_and_y)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    // Overcome's "Vinyl Click" (FL channel 82): 1111 four-tick notes whose Mod Y (FL's Final.FRes) sets each click's level
    // through the patch's Mod Y maps. Engine revision 3 hands the note's Mod X / Y to the voices; revision 2 and the
    // note's default (0.5) play as before, bit for bit
    const auto song = flparity190pr::demos() / "Product demos" / "Synth_dfr - Overcome - Sytrus.flp";
    if (! std::filesystem::exists (song))
        return;
    const auto project = flp::parseProject (flparity190pr::readFile (song));
    ChannelDef ch;
    ch.instrument = InstrumentKind::sytrus;
    for (const auto& c : project.channels)
        if (c.index == 82)
            ch.pluginState = c.pluginState;
    if (! sytrus::stateUsable (ch.pluginState))
        return;
    const auto play = [&] (int rev, float modY) {
        ch.engineRev = rev;
        auto in = makeInstrument (InstrumentKind::sytrus);
        in->prepare (44100.0);
        in->configure (ch, 124.0);
        in->settleNew();
        std::vector<float> l (11025), r (11025);
        in->setNextNoteModulation (0.5f, modY);
        in->noteOn (60, 0.78125f);
        in->render (l.data(), r.data(), 11025, 0.7071f, 0.7071f);
        return l;
    };
    CHECK (play (2, 0.5f) == play (3, 0.5f));
    CHECK (play (2, 0.5f) == play (2, 190.0f / 256.0f));
    const auto plain = flparity190pr::rmsDb (play (3, 0.5f), 0, 11025), up = flparity190pr::rmsDb (play (3, 190.0f / 256.0f), 0, 11025),
               down = flparity190pr::rmsDb (play (3, 0.0f), 0, 11025);
    std::printf ("  Vinyl Click at Mod Y 0 / 128 / 190: %.1f / %.1f / %.1f dB\n", down, plain, up);
    CHECK (std::abs (up - plain) > 0.5 || std::abs (down - plain) > 0.5);
}
