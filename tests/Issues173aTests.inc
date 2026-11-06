// Round 173a: issues #70, #72, #73 (docs/analise/REA_rodada173a.md)

namespace i173a
{
// a dense piano part as in the #70 video: chords strummed over a bar, each chord note held to the bar's end, a key
// played again right where its previous note ends, and more than 20 notes in the pattern
std::vector<lynx::Note> videoPart()
{
    using lynx::ppq;
    std::vector<lynx::Note> notes;
    const auto add = [&] (double beat, double beats, int key) { notes.push_back (i32d::Rig::note ((std::int64_t) (beat * ppq), (std::int64_t) (beats * ppq), key)); };
    for (int bar = 0; bar < 2; ++bar)
    {
        const auto b = bar * 4.0;
        add (b + 0.0, 4.0, 43);                               // G2 held the whole bar
        add (b + 0.5, 2.5, 50), add (b + 3.0, 1.0, 50);       // D3, then D3 again where it ends
        add (b + 1.0, 3.0, 55), add (b + 1.0, 3.0, 59);       // a chord
        add (b + 1.05, 2.95, 60), add (b + 1.5, 2.5, 66);     // strummed: one sample block later
        add (b + 2.0, 2.0, 69), add (b + 3.0, 1.0, 62);
        add (b + 2.25, 0.75, 64), add (b + 3.0, 1.0, 64);     // E3 twice, end to start
        add (b + 3.5, 0.5, 71);
    }
    return notes;
}
// FL's SoundFont Player state, version 6 (as tests/SoundFont158Tests.inc writes it)
std::vector<std::uint8_t> flSoundfontState6 (const std::array<int, 13>& p, const std::string& path)
{
    std::vector<std::uint8_t> s;
    const auto i32 = [&s] (int v) {
        for (int b = 0; b < 4; ++b)
            s.push_back ((std::uint8_t) ((std::uint32_t) v >> (8 * b)));
    };
    i32 (6);
    for (auto v : p)
        i32 (v);
    s.push_back ((std::uint8_t) path.size());
    s.insert (s.end(), path.begin(), path.end());
    i32 (-1), s.push_back (0); // reverb: send to none, not built in
    i32 (-1), s.push_back (0); // chorus
    s.push_back (1);
    s.push_back (1), s.push_back (1); // fix levels, velocity filter
    i32 (4), i32 (1), i32 (0);        // standard resampling, (1), preset index 0
    s.push_back (1);                  // EMU envelopes
    s.push_back (1), s.push_back (1);
    i32 (0);
    return s;
}
} // namespace i173a

// #70: "the notes keep on cutting off when two or more notes play". Every note of a dense, overlapping part reaches the
// instrument as its own note on at its start and its own note off at its end: no note is ended early by another one,
// and nothing releases every note (allNotesOff) while the pattern plays on, over the loop too.
TEST (issues173a_overlapping_notes_each_end_at_their_own_end)
{
    i32d::Rig rig;
    rig.song.patterns[0].notes = i173a::videoPart();
    rig.song.patterns[0].lengthTicks = 8 * lynx::ppq;
    rig.rebuild();
    rig.play (16.0); // two passes
    const auto& notes = rig.song.patterns[0].notes;
    int ons = 0, offs = 0;
    for (const auto& e : rig.spy->log)
        if (e.sample < rig.sampleOfBeat (16.0)) // (the last block runs into a third pass)
            (e.on ? ons : offs) += 1;
    CHECK (ons == 2 * (int) notes.size());
    int endsBefore16 = 0; // (the notes that end with the second pass end at beat 16 itself)
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& n : notes)
            endsBefore16 += rig.sampleOfBeat (pass * 8.0 + (double) (n.start + n.length) / lynx::ppq) < rig.sampleOfBeat (16.0) ? 1 : 0;
    CHECK (offs >= endsBefore16);
    // each note: on at its start, and the key's next off at its end (never before)
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& n : notes)
        {
            const auto start = rig.sampleOfBeat (pass * 8.0 + (double) n.start / lynx::ppq);
            const auto end = rig.sampleOfBeat (pass * 8.0 + (double) (n.start + n.length) / lynx::ppq);
            if (end >= rig.sampleOfBeat (15.9))
                continue;
            long long onAt = -1, offAt = -1;
            for (const auto& e : rig.spy->log)
            {
                if (e.key != n.key)
                    continue;
                if (e.on && onAt < 0 && std::llabs (e.sample - start) <= 2)
                    onAt = e.sample;
                else if (! e.on && onAt >= 0 && offAt < 0 && e.sample >= onAt)
                    offAt = e.sample;
            }
            CHECK (onAt >= 0);
            if (std::llabs (offAt - end) > 2)
                std::printf ("  #70: key %d at beat %.2f ends at sample %lld, not %lld\n", n.key, pass * 8.0 + (double) n.start / lynx::ppq, offAt, end);
            CHECK (std::llabs (offAt - end) <= 2);
        }
    CHECK (rig.spy->allOffs == 0);
}

// #73 evidence (not a check): LYNX_173A_WRITE_FLP=<folder> writes FL projects with FL's SoundFont Player (FL's own
// Default.sf2) on one long C5: no detune, the channel PITCH knob at −400 cents (range 12) and Misc › Fine tune at −40.
// FL renders them (FL64.exe /R /Ewav), LYNX imports the same files: REA_rodada173a.md #73.
TEST (issues173a_write_fl_detune_probes)
{
    const auto* dir = std::getenv ("LYNX_173A_WRITE_FLP");
    if (dir == nullptr)
        return;
    const std::string font = "C:\\Program Files\\Image-Line\\FL Studio 2026\\Plugins\\Fruity\\Generators\\Soundfont Player\\Data\\Default.sf2";
    for (const auto& [name, pitch, fine] : { std::tuple { "sf_pitch0", 0, 0 }, std::tuple { "sf_pitch-400", -400, 0 },
                                             std::tuple { "sf_fine-40", 0, -40 }, std::tuple { "sf_pitch-400_fine-40", -400, -40 } })
    {
        flp::Project p;
        p.tempo = 120.0;
        flp::Channel c;
        c.index = 0;
        c.type = flp::ChannelType::instrument;
        c.name = "SF";
        c.pluginName = "Fruity Soundfont Player";
        c.pluginState = i173a::flSoundfontState6 ({ 1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -128, -1, -1 }, font);
        c.hasLevels = true;
        c.volume = 10000;
        c.pan = 6400;
        c.pitchCents = pitch;
        c.pitchRange = 12;
        c.fineTune = fine;
        p.channels.push_back (c);
        flp::Pattern pat;
        pat.index = 1;
        flp::Note n;
        n.position = 0;
        n.length = 96 * 6;
        n.key = 60;
        n.velocity = 100;
        pat.notes.push_back (n);
        p.patterns.push_back (pat);
        flp::Arrangement a;
        a.name = "Arrangement";
        flp::PlaylistItem item;
        item.position = 0;
        item.length = 96 * 8;
        item.index = 1;
        item.track = 0;
        a.items.push_back (item);
        p.arrangements.push_back (a);
        const auto bytes = flp::writeProject (p);
        std::ofstream out (std::string (dir) + "/" + name + ".flp", std::ios::binary);
        out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
        std::printf ("  wrote %s/%s.flp\n", dir, name);
    }
}

// #73: "SoundFont Player detune not being imported correctly". The FL project's channel PITCH knob (−400 cents, its
// range 12) reached LYNX's channel, but the LYNX Soundfont that plays FL's SoundFont Player ignored the channel's PITCH
// knob (only the Misc fine tune and the main pitch reached it, through each note). It plays at FL's pitch now, and
// follows a live knob move on a sounding note as FL's generators do.
TEST (issues173a_soundfont_player_follows_the_channel_pitch_knob)
{
    using namespace lynx;
    const auto folder = std::filesystem::temp_directory_path() / "lynx-173a-sf";
    std::filesystem::create_directories (folder);
    const auto path = (folder / "font.sf2").string();
    {
        const auto bytes = sf::makeTestFont ({ { "Tone", 0, 0 } }, 1);
        std::ofstream out (path, std::ios::binary);
        out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    }
    const auto keep = sf::FontLibrary::instance().getNow (path); // (loaded: the rack's channel takes it at once)

    const auto imported = [&] (int pitchCents, int fineTune) {
        flp::Project project;
        flp::Channel c;
        c.index = 0;
        c.type = flp::ChannelType::instrument;
        c.name = "SF";
        c.pluginName = "Fruity Soundfont Player"; // (FL 12's name)
        c.pluginState = i173a::flSoundfontState6 ({ 1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -128, -1, -1 }, path);
        c.hasLevels = true;
        c.pitchCents = pitchCents;
        c.pitchRange = 12;
        c.fineTune = fineTune;
        project.channels = { c };
        const auto song = importFlp (project, {});
        return song.channels.empty() ? ChannelDef {} : song.channels[0];
    };
    Note held;
    held.key = 60;
    held.length = ppq * 8;
    held.velocity = 0.8f;
    const auto plain = imported (0, 0);
    I32D_REQUIRE (plain.instrument == InstrumentKind::soundfont);
    const auto base = issue14::pitchAt (issue14::renderPattern (plain, { held }, 0.0f, 1.0), 0.5);
    const auto detuned = imported (-400, 0);
    CHECK (std::abs (detuned.sampler.pitch + 4.0f) < 1.0e-4f);
    const auto down = issue14::cents (base, issue14::pitchAt (issue14::renderPattern (detuned, { held }, 0.0f, 1.0), 0.5));
    const auto fine = issue14::cents (base, issue14::pitchAt (issue14::renderPattern (imported (0, -40), { held }, 0.0f, 1.0), 0.5));
    const auto both = issue14::cents (base, issue14::pitchAt (issue14::renderPattern (imported (-400, -40), { held }, 0.0f, 1.0), 0.5));
    std::printf ("  #73: base %.1f Hz, knob -400 -> %+.0f cents, fine -40 -> %+.0f, both -> %+.0f\n", base, down, fine, both);
    CHECK (base > 50.0);
    CHECK (std::abs (down + 400.0) < 15.0);
    CHECK (std::abs (fine + 40.0) < 15.0);
    CHECK (std::abs (both + 440.0) < 15.0);
    // a live PITCH knob move retunes the note that sounds (FL's generators, REA_rodada167a §1)
    const auto live = left167::renderLiveTweak (plain, 2.0f, 0.0f, 0.75, 2.0);
    CHECK (std::abs (issue14::cents (issue14::pitchAt (live, 0.3), issue14::pitchAt (live, 1.25)) - 200.0) < 20.0);
    // and a live main pitch move too, once (not counted twice: the note's own cents already held the old one)
    const auto main = left167::renderLiveTweak (plain, 0.0f, 100.0f, 0.75, 2.0);
    CHECK (std::abs (issue14::cents (issue14::pitchAt (main, 0.3), issue14::pitchAt (main, 1.25)) - 100.0) < 20.0);
}
