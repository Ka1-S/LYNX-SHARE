// Round 174b (issue #81, Francisc0422): "When moving notes up and down pitches while a pattern is playing it makes
// 'ghost' notes that are sustained until the end of the pattern". Video (0.1.23): the default "Synth" channel, its
// 16 notes all selected in the Piano roll and moved up / down with the keyboard while the song plays; the Piano
// roll's keyboard shows more and more keys lit (voices whose note off never came). FL 2026 (r174b, 3x Osc, a C5
// note of 3 beats, Pattern mode playing): a sounding note moved to another key (Shift+Up, or dragged) keeps its old
// voice to the note's original end; the old key goes dark there; the new key plays from the next pass. LYNX 0.1.23
// emitted each note off for the note's key as it is now, so the old key never got one. Round 169d's ScheduledNotes
// (0.1.24) ends each started note where it was going to; these tests pin the issue's gestures.
// Uses i32d::Rig (Issue32dTests.inc).

namespace i81
{
const std::int64_t bar = 4 * lynx::ppq;
// the video's melody: 16 sixteenths (staccato: an eighth of a beat each, so every sixteenth ends with a gap)
inline void addMelody (lynx::Pattern& p)
{
    const int keys[16] = { 69, 72, 74, 76, 79, 81, 79, 76, 74, 72, 69, 67, 64, 67, 69, 72 };
    for (int i = 0; i < 16; ++i)
        p.notes.push_back (i32d::Rig::note (i * lynx::ppq / 4, lynx::ppq / 8, keys[i]));
}
inline void transposeAll (lynx::Pattern& p, int keys)
{
    for (auto& n : p.notes)
        n.key += keys;
}
inline void playTo (i32d::Rig& rig, double beat)
{
    while (rig.tick < beat * lynx::ppq)
    {
        rig.renderer.process (rig.state.get(), true, rig.tick, 256, rig.l.data(), rig.r.data());
        rig.tick += 256 * rig.tps;
    }
}
inline int soundingTotal (const i32d::Rig& rig)
{
    int total = 0;
    for (auto s : rig.spy->sounding)
        total += s;
    return total;
}
// plays to beat `to` and checks, in the middle of every sixteenth's gap, that nothing sounds
inline int ghostsUpTo (i32d::Rig& rig, double from, double to)
{
    int worst = 0;
    for (double gap = std::floor (from * 4.0) / 4.0 + 0.1875; gap < to; gap += 0.25)
        if (gap > from)
        {
            playTo (rig, gap);
            worst = std::max (worst, soundingTotal (rig));
        }
    playTo (rig, to);
    return worst;
}
} // namespace i81

TEST (issue81_moving_the_whole_melody_up_and_down_while_the_song_plays_leaves_no_ghost_notes)
{
    // the video: Song mode, the pattern in a Playlist clip, all its notes moved by a semitone at a time while they play
    i32d::Rig rig;
    i81::addMelody (rig.song.patterns[0]);
    lynx::Clip clip;
    clip.pattern = 0;
    clip.track = 1;
    clip.start = 0;
    clip.length = i81::bar;
    rig.song.playlist.push_back (clip);
    rig.song.mode = lynx::PlayMode::song;
    rig.rebuild();

    // each move lands inside a sounding sixteenth (1/32 beat after it starts), up and down as in the video
    const int moves[] = { +1, +1, +1, -1, +1, -1, -1, -1, +2, -2, +1, +1 };
    double at = 0.53;
    int worst = 0;
    for (const auto keys : moves)
    {
        worst = std::max (worst, i81::ghostsUpTo (rig, rig.tick / lynx::ppq, at));
        I32D_REQUIRE (i81::soundingTotal (rig) == 1); // a note sounds while it is moved
        i81::transposeAll (rig.song.patterns[0], keys);
        rig.rebuild();
        at += 0.75; // (a different sixteenth each time, across the loop of the 1-bar song)
    }
    worst = std::max (worst, i81::ghostsUpTo (rig, rig.tick / lynx::ppq, std::floor (at) + 8.1875)); // two more passes (to a gap)
    CHECK (worst == 0); // 0.1.23: the old keys hung (up to one per move)
    CHECK (rig.spy->allOffs == 0); // (nothing reset the instrument meanwhile)

    // every note on got exactly one note off, of its own key
    std::array<int, 128> balance {};
    for (const auto& e : rig.spy->log)
        balance[(std::size_t) e.key] += e.on ? 1 : -1;
    for (auto b : balance)
        CHECK (b == 0);
    CHECK (rig.renderer.scheduledNotesSounding() == 0);
}

TEST (issue81_a_sounding_note_moved_to_other_keys_keeps_its_voice_to_its_end)
{
    // FL r174b b1–b7 / c1–c7: the old key stays lit until the note's original end, then goes dark; the new key plays
    // from the next pass; no note on for the new key meanwhile (the scheduler never restarts a note behind the play head)
    i32d::Rig rig;
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (0, 3 * lynx::ppq, 72)); // C5, 3 beats of a 1-bar pattern
    rig.rebuild();
    i81::playTo (rig, 0.4);
    I32D_REQUIRE (rig.spy->sounding[72] == 1);
    for (const auto key : { 74, 73, 75, 72, 74 }) // up, down, up, back to C5, up to D5
    {
        rig.song.patterns[0].notes[0].key = key;
        rig.rebuild();
        i81::playTo (rig, rig.tick / lynx::ppq + 0.3);
        CHECK (rig.spy->sounding[72] == 1);
        CHECK (rig.spy->offs (72) == 0);
    }
    i81::playTo (rig, 3.1);
    CHECK (rig.spy->offs (72) == 1); // ended once, at its end
    CHECK (std::abs (rig.spy->lastOff (72) - rig.sampleOfBeat (3.0)) <= 3);
    int sounding = 0;
    for (auto s : rig.spy->sounding)
        sounding += s;
    CHECK (sounding == 0);
    int ons = 0;
    for (const auto& e : rig.spy->log)
        ons += e.on ? 1 : 0;
    CHECK (ons == 1); // the moved note waits for the next pass
    i81::playTo (rig, 4.5);
    CHECK (rig.spy->sounding[74] == 1 && rig.spy->sounding[72] == 0); // the next pass: D5
    i81::playTo (rig, 7.2);
    CHECK (rig.spy->sounding[74] == 0 && rig.spy->offs (74) == 1);
}

TEST (issue81_a_playlist_clip_moved_while_its_notes_sound_leaves_none_hanging)
{
    // Playlist edits: the clip dragged to another track and later in the song while its note sounds; a second clip
    // of the same pattern stays. Each started note still ends where it was going to.
    i32d::Rig rig;
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (0, 2 * lynx::ppq, 60));
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (2 * lynx::ppq, lynx::ppq, 67));
    for (int i = 0; i < 2; ++i)
    {
        lynx::Clip clip;
        clip.pattern = 0;
        clip.track = i;
        clip.start = i * 2 * i81::bar;
        clip.length = i81::bar;
        rig.song.playlist.push_back (clip);
    }
    rig.song.mode = lynx::PlayMode::song;
    rig.rebuild();
    i81::playTo (rig, 1.0);
    I32D_REQUIRE (rig.spy->sounding[60] == 1);
    rig.song.playlist[0].track = 5;
    rig.song.playlist[0].start = i81::bar; // now starts at bar 2, after the play head
    rig.rebuild();
    i81::playTo (rig, 2.5);
    CHECK (rig.spy->offs (60) == 1);
    CHECK (std::abs (rig.spy->lastOff (60) - rig.sampleOfBeat (2.0)) <= 3);
    CHECK (rig.spy->sounding[60] == 0);
    i81::playTo (rig, 12.5); // the moved clip's pass (bar 2) and the other clip's (bar 3)
    CHECK (rig.spy->sounding[60] == 0 && rig.spy->sounding[67] == 0);
    CHECK (rig.spy->offs (60) == 3 && rig.spy->offs (67) == 2);
    CHECK (rig.renderer.scheduledNotesSounding() == 0);
}

TEST (issue81_render_the_native_synth_falls_silent_after_a_moved_note_s_end)
{
    // the reporter's instrument (the default "Synth" channel), rendered: a long note moved up a fifth while it sounds.
    // The output is silent between the old note's end (+ its release) and the next pass; 0.1.23 kept the old key
    // sounding through it.
    lynx::Song song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.mode = lynx::PlayMode::pattern;
    for (auto& p : song.patterns)
        p.notes.clear();
    song.currentPattern = 0;
    int synth = -1;
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        if (song.channels[c].instrument == lynx::InstrumentKind::synth)
            synth = (int) c;
    I32D_REQUIRE (synth >= 0);
    auto n = i32d::Rig::note (0, 2 * lynx::ppq, 60);
    n.channel = synth;
    song.patterns[0].notes.push_back (n);

    const double rate = 48000.0;
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    lynx::RackRenderer renderer;
    renderer.prepare (rate, 256);
    renderer.setTransportOptions (true, false, 0);
    auto state = lynx::RackState::build (song, nullptr, rate);
    std::vector<float> l (256), r (256);
    double tick = 0.0;
    auto peakUntil = [&] (double beat) {
        float peak = 0.0f;
        while (tick < beat * lynx::ppq)
        {
            renderer.process (state.get(), true, tick, 256, l.data(), r.data());
            tick += 256 * tps;
            for (int i = 0; i < 256; ++i)
                peak = std::max ({ peak, std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]) });
        }
        return peak;
    };
    const auto playing = peakUntil (1.0);
    CHECK (playing > 0.01f);
    song.patterns[0].notes[0].key = 67; // moved while it sounds
    state = lynx::RackState::build (song, state.get(), rate);
    CHECK (peakUntil (1.9) > 0.01f); // the old voice plays on (FL)
    peakUntil (3.0);                  // its end at beat 2, then the release
    CHECK (peakUntil (3.7) < 1.0e-3f); // silent: no ghost
    CHECK (peakUntil (4.9) > 0.01f);   // the next pass: the moved note
}
