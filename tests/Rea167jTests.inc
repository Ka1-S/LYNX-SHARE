// Round 167j: FL's Options › Project › "Panning law" and the mixer's pan fold, the transport options of Settings ›
// Audio (docs/analise/REA_rodada167j.md)
#define REA167J_REQUIRE(x) \
    do { if (! (x)) { CHECK (x); return; } } while (false)

TEST (rea167j_panning_law_flp)
{
    // .flp 0x17 = 2 × FL's pan-law mode (FLEngine's project writer FUN_00f40840): 2 = triangular, 0 = circular
    FlpBuilder b;
    b.u8 (0x17);
    b.u8 (2);
    CHECK (lynx::importFlp (flp::parseProject (b.bytes)).panTriangular);
    CHECK (! lynx::importFlp (flp::parseProject (FlpBuilder().bytes)).panTriangular);
}

TEST (rea167j_mixer_pan_folds_the_other_side_through_the_renderer)
{
    // a stereo input (L 0.5, R −0.25) through insert 3 panned hard right: FL puts both inputs on the right at half
    // the side gain (circular: √2·½ each; triangular: ½ each), the left output is silent
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    for (auto& p : song.patterns)
        p.notes.clear();
    song.mixer = lynx::Song::defaultMixer (4);
    song.mixer[3].pan = 1.0f;
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    std::vector<float> inL (256, 0.5f), inR (256, -0.25f), l (256), r (256);
    auto run = [&] (const lynx::RackState* st) {
        for (int i = 0; i < 20; ++i) // past the glides (0.001 per sample)
        {
            renderer.setMetronomeInput (inL.data(), inR.data(), 3);
            renderer.process (st, false, 0.0, 256, l.data(), r.data());
        }
    };
    auto circular = lynx::RackState::build (song, nullptr, 48000.0);
    run (circular.get());
    CHECK (std::abs (l[200]) < 1e-5f && std::abs (r[200] - 0.70710677f * 0.25f) < 1e-4f);

    song.panTriangular = true;
    auto triangular = lynx::RackState::build (song, circular.get(), 48000.0);
    run (triangular.get());
    CHECK (std::abs (l[200]) < 1e-5f && std::abs (r[200] - 0.5f * 0.25f) < 1e-4f);

    // swap after the pan (FL swaps the matrix rows): hard right + swap = everything on the left
    song.mixer[3].swapChannels = true;
    auto swapped = lynx::RackState::build (song, triangular.get(), 48000.0);
    run (swapped.get());
    CHECK (std::abs (r[200]) < 1e-5f && std::abs (l[200] - 0.125f) < 1e-4f);
}

TEST (rea167j_play_truncated_notes_on_transport)
{
    // FL's Settings › Audio › "Play truncated notes on transport" (on by default, FUN_011e7470): playback that starts
    // inside a note plays it at once with what is left of it; without the option (or not at a start) it stays silent
    lynx::Pattern p;
    lynx::Note n;
    n.start = 0;
    n.length = 4 * lynx::ppq;
    n.key = 60;
    p.notes.push_back (n);
    lynx::Note later = n;
    later.start = 3 * lynx::ppq;
    later.length = lynx::ppq;
    later.key = 64;
    p.notes.push_back (later);
    const auto tps = lynx::ticksPerSample (120.0, 48000.0); // 1 beat = 24000 samples
    auto ons = [&] (bool chase, double start) {
        lynx::EventBuffer ev;
        lynx::schedulePatternLoop (p, 16 * lynx::ppq, start, 512, tps, ev, 0.0f, nullptr, chase);
        std::vector<std::pair<int, int>> out;
        for (const auto& e : ev)
            out.emplace_back (e.type == lynx::NoteEvent::Type::on ? e.key : -e.key, e.sampleOffset);
        return out;
    };
    CHECK (ons (false, 2.0 * lynx::ppq).empty());
    const auto chased = ons (true, 2.0 * lynx::ppq);
    CHECK (chased.size() == 1 && chased[0].first == 60 && chased[0].second == 0);
    CHECK (ons (true, 5.0 * lynx::ppq).empty()); // past both notes

    // Song mode: the same through a pattern clip
    lynx::Song s;
    s.patterns.push_back (p);
    lynx::Clip c;
    c.pattern = 0;
    c.start = 0;
    c.length = 8 * lynx::ppq;
    s.playlist.push_back (c);
    auto songOns = [&] (bool chase) {
        lynx::EventBuffer ev;
        lynx::scheduleSong (s, 3.5 * lynx::ppq, 512, tps, ev, true, nullptr, true, chase);
        int on60 = 0, on64 = 0;
        for (const auto& e : ev)
            if (e.type == lynx::NoteEvent::Type::on)
                (e.key == 60 ? on60 : on64) += e.sampleOffset == 0 ? 1 : 100;
        return std::make_pair (on60, on64);
    };
    CHECK (songOns (false) == std::make_pair (0, 0));
    CHECK (songOns (true) == std::make_pair (1, 1)); // both sound at beat 3.5
}

TEST (rea167j_transport_options_through_the_renderer)
{
    // a note sounding at the start position plays only with "Play truncated notes on transport"
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.mode = lynx::PlayMode::pattern;
    REA167J_REQUIRE (song.channels.size() > 4 && ! song.patterns.empty());
    song.patterns[0].notes.clear();
    lynx::Note n;
    n.start = 0;
    n.length = 8 * lynx::ppq;
    n.key = 60;
    n.channel = 4; // the Synth
    song.patterns[0].notes.push_back (n);
    song.currentPattern = 0;
    auto peak = [&] (bool truncated) {
        auto state = lynx::RackState::build (song, nullptr, 48000.0);
        lynx::RackRenderer renderer;
        renderer.prepare (48000.0, 512);
        renderer.setTransportOptions (truncated, false);
        std::vector<float> l (512), r (512);
        float m = 0.0f;
        for (int b = 0; b < 8; ++b)
        {
            renderer.process (state.get(), true, 2.0 * lynx::ppq + b * 512 * lynx::ticksPerSample (song.bpm, 48000.0), 512, l.data(), r.data());
            for (auto v : l)
                m = std::max (m, std::abs (v));
        }
        return m;
    };
    CHECK (peak (false) < 1e-6f);
    CHECK (peak (true) > 1e-3f);
}

TEST (rea167j_export_insert_effects_switch)
{
    // FL's render window "Enable insert effects" (InputFXBox, on by default): off bypasses every insert's effect slots,
    // not the Master's
    using namespace lynx::exportopts;
    auto song = lynx::Song::makeDefault();
    REA167J_REQUIRE (song.mixer.size() > 2);
    lynx::EffectSlot slot;
    slot.kind = lynx::EffectKind::limiter;
    song.mixer[2].effects.push_back (slot);
    Options o;
    CHECK (o.insertEffects);
    std::int64_t music = 0, loop = 0;
    auto on = makeRenderSong (song, o, 0, music, loop);
    CHECK (! on.mixer[2].effects.empty() && ! on.mixer[0].effects.empty());
    o.insertEffects = false;
    auto off = makeRenderSong (song, o, 0, music, loop);
    CHECK (off.mixer[2].effects.empty() && ! off.mixer[0].effects.empty());
    // remembered with the other choices
    CHECK (! fromText (toText (o)).insertEffects);
}

TEST (rea167j_first_channel_with_notes)
{
    // FL's "Select first channel with notes when the pattern changes" picks the first channel in rack order with a
    // note in the new pattern (FUN_00e67f20), none in an empty pattern
    lynx::Song s;
    s.channels.resize (5);
    s.patterns.resize (2);
    lynx::Note n;
    n.channel = 3;
    s.patterns[1].notes.push_back (n);
    n.channel = 2;
    n.start = 960;
    s.patterns[1].notes.push_back (n);
    CHECK (lynx::firstChannelWithNotes (s, 1) == 2);
    CHECK (lynx::firstChannelWithNotes (s, 0) == -1);
    CHECK (lynx::firstChannelWithNotes (s, 7) == -1);
}

TEST (rea167j_piano_roll_arrow_keys_are_fl_s)
{
    using lynx::noteedit::Arrow;
    using lynx::noteedit::arrowMove;
    // Shift+Up/Down = a semitone, Ctrl+Up/Down = an octave (FL EEShiftU / EEShiftU12)
    CHECK (arrowMove (Arrow::up, false, true, false).handled && arrowMove (Arrow::up, false, true, false).keys == 1);
    CHECK (arrowMove (Arrow::down, false, true, false).keys == -1);
    CHECK (arrowMove (Arrow::up, true, false, false).keys == 12);
    CHECK (arrowMove (Arrow::down, true, false, false).keys == -12);
    CHECK (arrowMove (Arrow::up, false, false, false).keys == 1); // plain arrows = Shift (LYNX extra)
    // Shift+Left/Right = one grid step, not a bar
    CHECK (arrowMove (Arrow::right, false, true, false).gridSteps == 1);
    CHECK (arrowMove (Arrow::left, false, true, false).gridSteps == -1);
    CHECK (arrowMove (Arrow::left, false, true, false).keys == 0);
    // Ctrl+Shift+Left/Right rotates, Ctrl+Left/Right and Alt+arrows are not moves
    CHECK (! arrowMove (Arrow::right, true, true, false).handled);
    CHECK (! arrowMove (Arrow::right, true, false, false).handled);
    CHECK (! arrowMove (Arrow::up, false, false, true).handled);
    CHECK (! arrowMove (Arrow::up, true, true, false).handled);
}

TEST (rea167j_fl_pattern_keys)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    // FL Patterns menu: Clone Ctrl+Shift+C, Delete Ctrl+Shift+Del, Find first empty Shift+F4, no naming Ctrl+F4
    CHECK (map.find ({ 'C', ctrl | shift }, Context::global, false) == "pattern.clone");
    CHECK (map.find ({ 'C', ctrl | shift }, Context::playlist, false) == "pattern.clone");
    CHECK (map.find ({ 'C', alt }, Context::playlist, false) == "pattern.clone"); // the old key stays
    CHECK (map.find ({ del, ctrl | shift }, Context::global, false) == "pattern.delete");
    CHECK (map.find ({ f4, shift }, Context::global, false) == "pattern.firstEmpty");
    CHECK (map.find ({ f4, ctrl }, Context::global, false) == "pattern.nextEmptyNoName");
    CHECK (map.find ({ f4, none }, Context::global, false) == "pattern.nextEmpty");
    // FL File › Export: MP3 Ctrl+Shift+R, MIDI Ctrl+Shift+M
    CHECK (map.find ({ 'R', ctrl | shift }, Context::global, false) == "file.exportMp3");
    CHECK (map.find ({ 'M', ctrl | shift }, Context::global, false) == "file.exportMidi");
    // (round 168g: in the Piano roll FL's own EEExportMIDIAction, ShortCut 0x604D, exports the edited score)
    CHECK (map.find ({ 'M', ctrl | shift }, Context::pianoRoll, false) == "pianoRoll.exportMidi");

    auto song = lynx::Song::makeDefault();
    song.patterns.resize (4);
    song.patterns[1].notes.push_back ({});
    song.currentPattern = 3;
    // the lowest-numbered empty one (pattern 0 has the default notes, 1 has a note), not the next after the current
    CHECK (lynx::gotoFirstEmptyPattern (song) == 2 && song.currentPattern == 2);
    song.patterns[2].notes.push_back ({});
    song.patterns[3].notes.push_back ({});
    CHECK (lynx::gotoFirstEmptyPattern (song) == 4 && song.patterns.size() == 5 && song.currentPattern == 4);
}
