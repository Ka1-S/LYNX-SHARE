// Round 177b: FL's "Create automation clip" placement (REA FUN_01191a50 / FUN_00e50b50, checked live in FL 2026):
// the time selection, else the playback position (Options › General "Create automation clips at the playback
// position", on) or the song start; it reaches the song end (at least one bar); the first track with no clips at all,
// skipping instrument tracks.
TEST (auto177b_new_automation_clip_place)
{
    namespace ap = lynx::autoclipplace;
    const auto bar = (std::int64_t) lynx::ppq * 4;
    auto song = lynx::Song::makeDefault();
    song.beatsPerBar = 4;
    song.playlist.clear();
    song.loopStart = song.loopEnd = 0;

    // empty Playlist: one bar at the playhead, track 0 (FL: 1:01 → Track 1, one bar)
    auto p = ap::forNewClip (song, true, 0);
    CHECK (p.track == 0 && p.start == 0 && p.length == bar);

    // FL's live sequence: a clip on track 0 over bar 1; the playhead at 5:02 → track 1, one bar, at the playhead
    lynx::Clip c;
    c.track = 0;
    c.start = 0;
    c.length = bar;
    song.playlist.push_back (c);
    const auto at52 = bar * 4 + lynx::ppq / 4;
    p = ap::forNewClip (song, true, at52);
    CHECK (p.track == 1 && p.start == at52 && p.length == bar);
    lynx::Clip c2 = c;
    c2.track = 1;
    c2.start = at52;
    song.playlist.push_back (c2);
    // the playhead past the song (9:02) → track 2, at the playhead
    const auto at92 = bar * 8 + lynx::ppq / 4;
    p = ap::forNewClip (song, true, at92);
    CHECK (p.track == 2 && p.start == at92 && p.length == bar);
    lynx::Clip c3 = c;
    c3.track = 2;
    c3.start = at92;
    song.playlist.push_back (c3);
    // the playhead inside the song (3:02) → track 3 (track 0 is free there but not used), up to the song end (10:02)
    const auto at32 = bar * 2 + lynx::ppq / 4;
    p = ap::forNewClip (song, true, at32);
    CHECK (p.track == 3 && p.start == at32 && p.start + p.length == at92 + bar);
    // the option off: the song start
    p = ap::forNewClip (song, false, at32);
    CHECK (p.start == 0 && p.length == at92 + bar && p.track == 3);

    // an instrument track is skipped
    song.playlistTracks.resize (6);
    song.playlistTracks[3].mode = lynx::PlaylistTrackMode::instrument;
    p = ap::forNewClip (song, true, at32);
    CHECK (p.track == 4);

    // a track left empty above the used ones is taken
    song.playlist.erase (song.playlist.begin() + 1); // track 1 is empty again
    p = ap::forNewClip (song, true, at32);
    CHECK (p.track == 1);
    song.playlist.insert (song.playlist.begin() + 1, c2);

    // the time selection wins
    song.loopStart = bar;
    song.loopEnd = bar * 3;
    p = ap::forNewClip (song, true, bar * 5);
    CHECK (p.start == bar && p.length == bar * 2 && p.track == 4);
}
