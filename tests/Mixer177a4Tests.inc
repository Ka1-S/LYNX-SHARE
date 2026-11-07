// --- Round 177a: a press on a mixer fader's rail steps towards the mouse, as FL (REA_rodada177a section 4.2) ---------------

TEST (mixer177a_a_rail_press_steps_towards_the_mouse_and_stops_there)
{
    using lynx::faderRailStepTowards;
    using lynx::faderRailStep;
    CHECK (std::abs (faderRailStepTowards (0.8f, 0.2f) - (0.8f - faderRailStep)) < 1e-6f);
    CHECK (std::abs (faderRailStepTowards (0.5f, 0.9f) - (0.5f + faderRailStep)) < 1e-6f);

    // FL's hint after 100 clicks below the cap: 100 % -> 30 % of 0 dB's position
    auto value = lynx::unityFader;
    for (int i = 0; i < 100; ++i)
        value = faderRailStepTowards (value, 0.0f);
    CHECK (std::abs (value / lynx::unityFader - 0.30f) < 0.005f);

    // held, it reaches the mouse and stays: never past it
    value = lynx::unityFader;
    for (int i = 0; i < 1000; ++i)
        value = faderRailStepTowards (value, 0.61f);
    CHECK (value == 0.61f);
}

// --- Round 177a: "Gradient color..." as FL's SetGradientColorMenuClick (REA_rodada177a section 5) -----------------------

TEST (mixer177a_b_gradient_colour_runs_from_the_first_to_the_last_selected_track)
{
    using lynx::mixerops::gradientColourStep;
    // ends are exact, the middle follows FL's 8-bit blend: amount = k * 256 / (n - 1), c = a + floor ((b - a) * amount / 256)
    CHECK (gradientColourStep (0xffff0000u, 0xff0000ffu, 0, 4) == 0xffff0000u);
    CHECK (gradientColourStep (0xffff0000u, 0xff0000ffu, 3, 4) == 0xff0000ffu);
    CHECK (gradientColourStep (0xffff0000u, 0xff0000ffu, 1, 4) == 0xffaa0054u); // amount 85: 255 + floor (-84.7) = 0xaa, floor (84.7) = 0x54
    CHECK (gradientColourStep (0xff000000u, 0xff00ff00u, 1, 3) == 0xff007f00u); // amount 128: floor (127.5) = 0x7f

    lynx::Song song;
    song.mixer.resize (8);
    for (auto& t : song.mixer)
        t.colour = 0;
    // the selection is taken in mixer order, and tracks out of range are ignored
    CHECK (lynx::mixerops::gradientColour (song, { 5, 2, 3, 99 }, 0xff102030u, 0xff405060u) == 3);
    CHECK (song.mixer[2].colour == 0xff102030u);
    CHECK (song.mixer[5].colour == 0xff405060u);
    CHECK (song.mixer[3].colour == lynx::mixerops::gradientColourStep (0xff102030u, 0xff405060u, 1, 3));
    CHECK (song.mixer[4].colour == 0);
    // one track: nothing (FL then opens the plain colour picker)
    CHECK (lynx::mixerops::gradientColour (song, { 4 }, 0xff102030u, 0xff405060u) == 0);
}

// --- Round 177a: Assign to new / Unassign from Audio track(s), as FL's AssignAudiotrackMenuClick (section 6) -------------

TEST (mixer177a_c_assign_and_unassign_audio_tracks)
{
    lynx::Song song;
    song.mixer.resize (6);
    song.mixer[2].name = "Bass";
    song.mixer[2].colour = 0xff3060a0u;
    song.playlistTracks.resize (2);
    song.playlistTracks[0].name = "Drums"; // touched: skipped
    lynx::Clip clip;
    clip.track = 1; // a clip on track 1: skipped
    song.playlist.push_back (clip);

    CHECK (lynx::mixerops::assignNewAudioTracks (song, { 3, 2, 0 }) == 2); // the Master is never assigned
    CHECK (song.playlistTracks.size() >= 4);
    CHECK (song.playlistTracks[2].mode == lynx::PlaylistTrackMode::audio && song.playlistTracks[2].mixerTrack == 2);
    CHECK (song.playlistTracks[2].name == "Bass" && song.playlistTracks[2].colour == 0xff3060a0u);
    CHECK (song.playlistTracks[3].mixerTrack == 3 && song.playlistTracks[3].name == "Insert 3");
    // already linked: nothing new
    CHECK (lynx::mixerops::assignNewAudioTracks (song, { 2 }) == 0);

    CHECK (lynx::mixerops::unassignAudioTracks (song, { 2, 4 }) == 1);
    CHECK (song.playlistTracks[2].mode == lynx::PlaylistTrackMode::unassigned && song.playlistTracks[2].mixerTrack == -1);
    CHECK (song.playlistTracks[2].name == "Bass"); // name and colour stay
}

TEST (mixer177a_d_assign_skips_inserts_with_a_track_as_fl)
{
    lynx::Song song;
    song.mixer.resize (6);
    song.channels.resize (3);
    song.channels[0].mixerTrack = 1; // the only channel on insert 1, with an instrument track: skipped
    song.channels[1].mixerTrack = 2; // insert 2 has two channels: not skipped
    song.channels[2].mixerTrack = 2;
    song.playlistTracks.resize (3);
    song.playlistTracks[0].mode = lynx::PlaylistTrackMode::instrument;
    song.playlistTracks[0].channel = 0;
    song.playlistTracks[0].mixerTrack = 5; // (its own insert differs) and insert 5 is pointed at: skipped
    song.playlistTracks[1].mode = lynx::PlaylistTrackMode::instrument;
    song.playlistTracks[1].channel = 1;
    song.playlistTracks[1].mixerTrack = 4; // insert 4 pointed at by an instrument track: skipped
    CHECK (lynx::mixerops::assignNewAudioTracks (song, { 1, 2, 3, 4, 5 }) == 2);
    CHECK (song.playlistTracks[2].mixerTrack == 2 && song.playlistTracks[2].mode == lynx::PlaylistTrackMode::audio);
    CHECK (song.playlistTracks.size() == 4 && song.playlistTracks[3].mixerTrack == 3);
}
