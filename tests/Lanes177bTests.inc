// Round 177b: optional automation lanes (Song.h AutomationLane, core/PlaylistOps autolanes): the lane plays over the
// song; a clip of the same target wins where it plays (Bitwig's rule); conversions clip <-> lane; .flp export writes
// lanes as clips; projects keep them.
namespace
{
float lanes177bLevel (const lynx::Song& song, double tick)
{
    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    lynx::LiveInput input;
    renderer.setLiveInput (&input);
    std::vector<float> a (256, 0.5f), l (256), r (256);
    input.channels[0] = a.data();
    input.channels[1] = a.data();
    input.numChannels = 2;
    for (int i = 0; i < 4; ++i) // (the volume smoothing settles)
        renderer.process (state.get(), true, tick, 256, l.data(), r.data());
    return std::abs (l[255]);
}

lynx::Song lanes177bSong()
{
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.mode = lynx::PlayMode::song;
    song.mixer = lynx::Song::defaultMixer (2);
    song.mixer[0].inputChannel = 0;
    song.mixer[0].monitor = lynx::InputMonitor::automatic;
    return song;
}
} // namespace

TEST (lanes177b_lane_plays_and_clips_win)
{
    namespace al = lynx::autolanes;
    auto song = lanes177bSong();
    const lynx::AutomationTarget master { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 };

    // a clip automation at 0.8 over [1000, 2000) and [6000, 7000)
    lynx::Automation loud;
    loud.target = master;
    loud.points = { { 0, 0.8f, 0.0f }, { 1000, 0.8f, 0.0f } };
    song.automations.push_back (loud);
    for (const auto start : { 1000, 6000 })
    {
        lynx::Clip c;
        c.kind = lynx::ClipKind::automation;
        c.automation = 0;
        c.track = 1;
        c.start = start;
        c.length = 1000;
        song.playlist.push_back (c);
    }
    const auto clipsOnly = lanes177bLevel (song, 3000.0); // (between clips FL holds the last clip value)
    CHECK (clipsOnly > 0.05f);

    // a silent lane on track 0: it plays between the clips; inside a clip the clip wins
    const auto lane = al::addLane (song, 0, master, "Master volume", 0.0f);
    CHECK (lane == 1 && al::ownerTrack (song, lane) == 0 && song.playlistTracks[0].lanes.size() == 1);
    CHECK (lanes177bLevel (song, 3000.0) < 1.0e-4f);
    CHECK (lanes177bLevel (song, 1500.0) > 0.05f);
    // a muted track's lanes don't play
    song.playlistTracks[0].muted = true;
    CHECK (lanes177bLevel (song, 3000.0) > 0.05f);
    song.playlistTracks[0].muted = false;

    // the same target again: the existing lane comes back (one lane per target and track)
    CHECK (al::addLane (song, 0, master, "Master volume", 0.3f) == lane);

    // (the project round trip of the lanes: LYNX.exe --selftest, lanes_saved)
}

TEST (lanes177b_clips_of_two_automations_win_over_a_lane)
{
    // issue #97: two automations of one target share a slot; the lane still gets the value of whichever clip plays
    auto song = lanes177bSong();
    const lynx::AutomationTarget master { lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 };
    int index = 0;
    for (const auto value : { 0.8f, 0.4f })
    {
        lynx::Automation a;
        a.target = master;
        a.points = { { 0, value, 0.0f }, { 1000, value, 0.0f } };
        song.automations.push_back (a);
        lynx::Clip c;
        c.kind = lynx::ClipKind::automation;
        c.automation = index;
        c.track = 1;
        c.start = 1000 + index * 5000;
        c.length = 1000;
        song.playlist.push_back (c);
        ++index;
    }
    const auto first = lanes177bLevel (song, 1500.0), second = lanes177bLevel (song, 6500.0);
    CHECK (first > 0.05f && second > 0.05f && first > second * 1.2f);

    lynx::autolanes::addLane (song, 0, master, "Master volume", 0.0f);
    CHECK (std::abs (lanes177bLevel (song, 1500.0) - first) < 1.0e-4f);  // (the follower's clip)
    CHECK (std::abs (lanes177bLevel (song, 6500.0) - second) < 1.0e-4f); // (the slot holder's clip)
    CHECK (lanes177bLevel (song, 3000.0) < 1.0e-4f);                     // between the clips: the lane
}

TEST (lanes177b_conversions_and_export)
{
    namespace al = lynx::autolanes;
    auto song = lanes177bSong();
    const lynx::AutomationTarget pan { lynx::AutomationTargetKind::mixerPan, 1, 0, 0 };
    const auto lane = al::addLane (song, 2, pan, "Pan", 0.5f);

    // a clip of a ramp 0 → 1 over its 960 ticks, offset 480, placed at 2000: its span replaces the lane there
    lynx::Automation ramp;
    ramp.target = pan;
    ramp.points = { { 0, 0.0f, 0.0f }, { 1920, 1.0f, 0.0f } };
    song.automations.push_back (ramp);
    lynx::Clip c;
    c.kind = lynx::ClipKind::automation;
    c.automation = 1;
    c.track = 0;
    c.start = 2000;
    c.length = 960;
    c.offset = 480;
    song.playlist.push_back (c);
    CHECK (al::clipToLane (song, 0, 2) == lane);
    CHECK (song.playlist.empty());
    const auto& a = song.automations[(std::size_t) lane];
    CHECK (std::abs (a.valueAt (1000.0) - 0.5f) < 1e-6f);  // before: the lane's own value
    CHECK (std::abs (a.valueAt (2000.0) - 0.25f) < 1e-4f); // the clip's start (offset 480 of 1920)
    CHECK (std::abs (a.valueAt (2480.0) - 0.5f) < 1e-4f);  // its middle
    CHECK (std::abs (a.valueAt (5000.0) - 0.5f) < 1e-6f);  // after: the lane's own value again

    // lane → clip: a clip of the same automation on a free track; the lane entry goes
    const auto clip = al::laneToClip (song, 2, 0);
    CHECK (clip >= 0 && song.playlistTracks[2].lanes.empty() && song.playlist[(std::size_t) clip].automation == lane
           && song.playlist[(std::size_t) clip].start == 0);

    // removing an automation renumbers the lanes after it
    al::addLane (song, 1, { lynx::AutomationTargetKind::mixerVolume, 1, 0, 0 }, "Vol", 0.7f);
    const auto volLane = song.playlistTracks[1].lanes[0].automation;
    song.removeAutomation (0);
    CHECK (song.playlistTracks[1].lanes[0].automation == volLane - 1);

    // the .flp export writes the lane as an automation clip
    lynx::flpexport::Report report;
    const auto before = song.playlist.size();
    auto copy = song;
    CHECK (al::lanesToClips (copy) == 1 && copy.playlist.size() == before + 1 && copy.playlistTracks[1].lanes.empty());
    (void) lynx::flpexport::toProject (song, {}, report);
    CHECK (std::any_of (report.notes.begin(), report.notes.end(), [] (const std::string& n) { return n.rfind ("Automation lanes", 0) == 0; }));
}

TEST (lanes177b_track_for_a_target)
{
    namespace al = lynx::autolanes;
    auto song = lanes177bSong();
    CHECK (! al::shown()); // off by default: FL users see no lanes
    song.channels.resize (2);
    song.channels[1].mixerTrack = 1;
    song.playlistTracks.resize (6);
    song.playlistTracks[3].mode = lynx::PlaylistTrackMode::instrument;
    song.playlistTracks[3].channel = 1;
    song.playlistTracks[3].mixerTrack = 1;
    song.playlistTracks[4].mode = lynx::PlaylistTrackMode::audio;
    song.playlistTracks[4].mixerTrack = 2;
    lynx::Clip clip;
    clip.track = 0;
    clip.length = 960;
    song.playlist.push_back (clip);

    using K = lynx::AutomationTargetKind;
    CHECK (al::trackFor (song, { K::channelVolume, 1, 0, 0 }) == 3); // the channel's instrument track
    CHECK (al::trackFor (song, { K::mixerPan, 2, 0, 0 }) == 4);       // the audio track of its insert
    CHECK (al::trackFor (song, { K::effectParam, 1, 0, 3 }) == 3);    // the insert of the instrument track
    CHECK (al::trackFor (song, { K::tempo, 0, 0, 0 }) == 1);          // else the first empty track (0 has a clip)
    al::addLane (song, 5, { K::tempo, 0, 0, 0 }, "Tempo", 0.3f);
    CHECK (al::trackFor (song, { K::tempo, 0, 0, 0 }) == 5);          // the track that already has its lane
    CHECK (al::trackFor (song, { K::mixerVolume, 0, 0, 0 }) == 1);
}
