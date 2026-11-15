// Round 177d: the recording in progress drawn live (MusicTech's FL Studio 2025 review: recorded MIDI is not shown in the
// Playlist while recording). NoteRecorder::preview (the notes still held, as they would be committed now),
// liverec::audioSpan (where the pass being recorded shows) and liverec::LivePeaks (the growing waveform).

TEST (rec177d_preview_shows_held_notes_without_changing_the_take)
{
    auto song = rec170d::emptySong (lynx::PlayMode::pattern);
    lynx::NoteRecorder r;
    r.begin (song.patterns[0].effectiveLength (song.meter()));
    CHECK (r.preview (song, 0.0).notes.empty());
    r.add (song, { 240.0, 0, 60, 0.8f, true });
    r.add (song, { 480.0, 0, 64, 0.5f, true });
    auto p = r.preview (song, 960.0);
    CHECK (p.notes.size() == 2);
    for (const auto& n : p.notes)
    {
        if (n.key == 60)
            CHECK (n.start == 240 && n.length == 720 && std::abs (n.velocity - 0.8f) < 1e-6f);
        else
            CHECK (n.key == 64 && n.start == 480 && n.length == 480);
    }
    // nothing finished yet: the preview does not commit anything
    CHECK (r.takeFinished().empty());
    // released: the finished note is the one the preview showed at that moment
    r.add (song, { 960.0, 0, 60, 0.0f, false });
    const auto done = r.takeFinished();
    CHECK (done.size() == 1 && done[0].start == 240 && done[0].length == 720);
    p = r.preview (song, 1200.0);
    CHECK (p.notes.size() == 1 && p.notes[0].key == 64 && p.notes[0].length == 720);
    // a note-on stamped a hair after the UI's "now": its minimum length, never negative
    r.add (song, { 1300.0, 0, 67, 0.8f, true });
    for (const auto& n : r.preview (song, 1250.0).notes)
        CHECK (n.length > 0);
}

TEST (rec177d_preview_song_mode_origin_and_reach_before_the_first_release)
{
    auto song = rec170d::emptySong (lynx::PlayMode::song);
    lynx::NoteRecorder r;
    r.begin (song.patterns[0].effectiveLength (song.meter()));
    r.setLinear (true);
    // the first note held in bar 3: the take's origin is that bar, no clip yet
    r.add (song, { 2.0 * rec170d::bar + 100.0, 0, 60, 0.8f, true });
    const auto p = r.preview (song, 3.0 * rec170d::bar + 100.0);
    CHECK (p.origin == (std::int64_t) (2.0 * rec170d::bar));
    CHECK (! p.fromClip);
    CHECK (p.notes.size() == 1 && p.notes[0].start == 100 && p.notes[0].length == (std::int64_t) rec170d::bar);
    CHECK (p.reach == p.origin + 100);
    // the take itself still has no origin (only the commit sets it, as before)
    CHECK (r.songOrigin() == (std::int64_t) (2.0 * rec170d::bar)); // (set by add's note-on, round 168k)
    // the clip would go to the first free track at that bar
    CHECK (lynx::patternClipTrack (song, p.origin) == 0);
}

TEST (rec177d_place_pattern_clip_uses_the_shared_track_rule)
{
    auto song = rec170d::emptySong (lynx::PlayMode::song);
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 0;
    c.track = 0;
    c.start = 0;
    c.length = (std::int64_t) rec170d::bar;
    song.playlist.push_back (c);
    // a clip ending right at bar 2 keeps Track 1 busy from there (round 168k fl2): the take goes to Track 2
    CHECK (lynx::patternClipTrack (song, (std::int64_t) rec170d::bar) == 1);
    CHECK (lynx::placePatternClipAt (song, 0, (std::int64_t) rec170d::bar));
    CHECK (song.playlist.back().track == 1 && song.playlist.back().start == (std::int64_t) rec170d::bar);
}

TEST (rec177d_audio_span_maps_frames_as_the_placement_does)
{
    // linear: the first frame sounds one latency before the transport position it arrived at
    const double tps = 0.04; // ticks a sample
    auto s = lynx::liverec::audioSpan (1000.0, 48000, tps, 480, -1, 0);
    CHECK (s.valid());
    CHECK (std::abs (s.start - (1000.0 - 480 * tps)) < 1e-9);
    CHECK (std::abs (s.length - 48000 * tps) < 1e-9);
    CHECK (s.firstFrame == 0.0);
    // it matches takes::place's exact start
    const auto placed = lynx::takes::place (1000.0, 48000 * tps, tps, 480);
    CHECK (std::abs (placed.exactStart - s.start) < 1e-6);

    // a loop pass: from the loop start, from the wrap frame plus the latency
    s = lynx::liverec::audioSpan (1000.0, 48000, tps, 480, 30000, 3840);
    CHECK (s.start == 3840.0 && s.firstFrame == 30480.0);
    CHECK (std::abs (s.length - (48000 - 30480) * tps) < 1e-9);

    // before the song start: cut at 0
    s = lynx::liverec::audioSpan (5.0, 48000, tps, 480, -1, 0);
    CHECK (s.start == 0.0 && std::abs (s.firstFrame - 480.0 + 5.0 / tps) < 1e-6);

    // nothing recorded yet: nothing shown
    CHECK (! lynx::liverec::audioSpan (-1.0, 100, tps, 0, -1, 0).valid());
    CHECK (! lynx::liverec::audioSpan (10.0, 0, tps, 0, -1, 0).valid());
}

TEST (rec177d_live_peaks_follow_the_take)
{
    lynx::liverec::LivePeaks p;
    constexpr int per = lynx::liverec::LivePeaks::framesPerPeak;
    std::vector<float> l ((std::size_t) per * 4, 0.0f), r ((std::size_t) per * 4, 0.0f);
    l[(std::size_t) per + 3] = -0.5f;   // peak 1, left
    r[(std::size_t) per * 3 + 7] = 0.9f; // peak 3, right
    const float* data[] = { l.data(), r.data() };
    p.append (data, 2, per * 4 - 10); // the last peak is not whole yet
    CHECK (p.size() == per * 4 - 10);
    float out[4] {};
    p.read (0.0, per, 4, out);
    CHECK (out[0] == 0.0f && out[1] == 0.5f && out[2] == 0.0f && out[3] == 0.9f); // (the newest, still pending, shows)
    // coarser columns take the largest
    float two[2] {};
    p.read (0.0, 2.0 * per, 2, two);
    CHECK (two[0] == 0.5f);
    // past what was recorded: silence
    float past[1] { 1.0f };
    p.read (100.0 * per, per, 1, past);
    CHECK (past[0] == 0.0f);
    // mono takes, null input: no crash
    const float* mono[] = { l.data() };
    p.append (mono, 1, 10);
    p.append (nullptr, 1, 10);
    CHECK (p.size() == per * 4);
}
