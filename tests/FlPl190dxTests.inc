// Round 190dx: FL's audio clip menu Automate › Panning / Volume and Preview, new instrument / audio tracks (Channel rack
// "Assign to new instrument track", the Playlist's "+" menu, Track mode › Instrument track), Tools › One-click audio
// recording and Mixer › Disk recording › Latency compensation. FL facts in /mnt/e/lynx-work/flpl190dx/FL-FACTS.md.
namespace flpl190dx
{
inline std::vector<std::string> texts (const std::vector<lynx::fltrackmenus::Entry>& entries)
{
    std::vector<std::string> out;
    for (const auto& e : entries)
        out.push_back (e.kind == lynx::fltrackmenus::Entry::Kind::separator ? std::string ("-")
                       : e.kind == lynx::fltrackmenus::Entry::Kind::caption ? "[" + e.text + "]"
                       : e.kind == lynx::fltrackmenus::Entry::Kind::submenu ? e.text + " >"
                       : e.kind == lynx::fltrackmenus::Entry::Kind::list    ? "<" + e.text + ">"
                                                                            : e.text + (e.isDefault ? " (default)" : ""));
    return out;
}

// Song::makeDefault (channels 0..4 on inserts 1..5) plus an audio clip channel "Vox" and its clip on track 3
inline lynx::Song songWithAudioClip (int& vox)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.push_back ({});
    song.channels.back().name = "Vox";
    song.channels.back().instrument = InstrumentKind::audioClip;
    song.channels.back().volume = 0.6f;
    song.channels.back().pan = -0.5f;
    vox = (int) song.channels.size() - 1;
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = vox;
    c.track = 3;
    c.start = 960;
    c.length = 3840;
    song.playlist.push_back (c);
    return song;
}
} // namespace flpl190dx

TEST (flpl190dx_menu_models_are_fls)
{
    using namespace lynx;
    // Tools › One-click audio recording ([FORM] OneClickRecMenu; live 190cq): Edison first and FL's default item
    CHECK (flpl190dx::texts (fltrackmenus::oneClickRecording())
           == std::vector<std::string> ({ "[I want to record audio...]", "Into Edison audio editor/recorder (default)", "Into playlist as an audio clip" }));
    // the audio clip menu's Automate › (Panning Tag 0, Volume Tag 1 = default)
    CHECK (flpl190dx::texts (fltrackmenus::automate()) == std::vector<std::string> ({ "Panning", "Volume (default)" }));
    // the Playlist's "+" (AddInstrumentMenu)
    CHECK (flpl190dx::texts (fltrackmenus::addTrack())
           == std::vector<std::string> ({ "[Audio track]", "Add audio track (default)", "[Instrument track]", "More plugins...", "-", "<generators>" }));
    // Track mode › Instrument track ›
    CHECK (flpl190dx::texts (fltrackmenus::instrumentTrack())
           == std::vector<std::string> ({ "Use existing channel >", "-", "More plugins... (default)", "-", "<generators>" }));
    // the Channel rack item, both states (ChannelMenuPopup)
    CHECK (fltrackmenus::assignInstrumentTrackText (false) == "Assign to new instrument track");
    CHECK (fltrackmenus::assignInstrumentTrackText (true) == "Unassign from instrument track");
    // Disk recording › Options: Latency compensation comes right after Auto-create audio clip
    const auto options = fltrackmenus::diskRecordingOptions();
    CHECK (options.size() == 7 && options[1] == "Auto-create audio clip" && options[2] == "Latency compensation");
}

TEST (flpl190dx_automate_volume_and_panning_of_an_audio_clip)
{
    using namespace lynx;
    int vox = -1;
    auto song = flpl190dx::songWithAudioClip (vox);
    const auto automations = song.automations.size();

    // Volume: the clip channel's Levels adjustment volume multiplier (FL's channel parameter 0x11; round 190dz), a flat
    // line at its current value over the clip, where the clip is (80 % of the 0…200 % travel: 0.4)
    song.channels[(std::size_t) vox].misc.levels.volume = 0.8f;
    song.channels[(std::size_t) vox].misc.levels.pan = -0.5f;
    CHECK (clipops::automateTarget (song, 0, clipops::AutomateWhat::volume)
           == (AutomationTarget { AutomationTargetKind::channelAdjust, vox, 0, leveladjust::volume }));
    const auto v = clipops::automateAudioClip (song, 0, clipops::AutomateWhat::volume, "Vox - Volume");
    CHECK (v == (int) automations && song.automations.size() == automations + 1);
    if (v >= 0 && v < (int) song.automations.size())
    {
        const auto& a = song.automations[(std::size_t) v];
        CHECK (a.name == "Vox - Volume" && a.target.kind == AutomationTargetKind::channelAdjust && a.target.index == vox
               && a.target.param == leveladjust::volume);
        CHECK (a.points.size() == 2 && a.points[0].tick == 0 && a.points[1].tick == 3840);
        CHECK (std::abs (a.points[0].value - 0.4f) < 1.0e-6f && std::abs (a.points[1].value - 0.4f) < 1.0e-6f);
        CHECK (a.points[0].tension == 0.0f && a.points[1].tension == 0.0f);
        // it plays what the channel had: no jump when the clip starts
        CHECK (std::abs (a.valueAt (1920.0) - song.targetValue (a.target)) < 1.0e-6f);
    }
    CHECK (song.playlist.size() == 2);
    const auto& made = song.playlist.back();
    CHECK (made.kind == ClipKind::automation && made.automation == v && made.track == 3 && made.start == 960 && made.length == 3840);

    // Panning: the Levels adjustment's pan offset (0x10), as automation's 0..1 (−0.5 → 0.25)
    const auto p = clipops::automateAudioClip (song, 0, clipops::AutomateWhat::panning, "Vox - Pan");
    CHECK (p == v + 1);
    if (p >= 0 && p < (int) song.automations.size())
    {
        const auto& a = song.automations[(std::size_t) p];
        CHECK (a.target.kind == AutomationTargetKind::channelAdjust && a.target.index == vox && a.target.param == leveladjust::pan);
        CHECK (std::abs (a.points[0].value - 0.25f) < 1.0e-6f && a.points[1].tick == 3840);
    }

    // not an audio clip (the automation clip just made): nothing
    const auto before = song.playlist.size();
    CHECK (clipops::automateAudioClip (song, (int) song.playlist.size() - 1, clipops::AutomateWhat::volume, "x") == -1);
    CHECK (clipops::automateAudioClip (song, 99, clipops::AutomateWhat::panning, "x") == -1);
    CHECK (song.playlist.size() == before);
}

TEST (flpl190dx_add_audio_track_takes_the_first_free_track_and_mixer_track)
{
    using namespace lynx;
    int vox = -1;
    auto song = flpl190dx::songWithAudioClip (vox); // a clip on track 3
    Clip p;
    p.track = 0;
    p.length = 384;
    song.playlist.push_back (p); // track 0 has a clip
    song.playlistTracks.resize (3);
    CHECK (trackmodes::makeInstrumentTrack (song, 1, 0)); // track 1: an instrument track (insert 1)
    // track 2 has no clip and is unassigned: the first free one
    CHECK (trackmodes::firstFreeTrack (song) == 2);
    const auto track = trackmodes::addAudioTrack (song);
    CHECK (track == 2 && song.playlistTracks[2].mode == PlaylistTrackMode::audio);
    const auto mixer = song.playlistTracks[2].mixerTrack;
    // the mixer track nothing uses: inserts 1..5 carry channels 0..4 (Song::makeDefault)
    CHECK (mixer == 6);
    // next: track 4 (track 3 has the audio clip)
    CHECK (trackmodes::firstFreeTrack (song) == 4 && trackmodes::addAudioTrack (song) == 4 && song.playlistTracks[4].mixerTrack != mixer);

    // every track taken: nothing changes
    for (int t = 0; t < 500; ++t)
    {
        Clip c;
        c.track = t;
        c.length = 1;
        song.playlist.push_back (c);
    }
    const auto mixers = song.mixer.size();
    CHECK (trackmodes::firstFreeTrack (song) == -1 && trackmodes::addAudioTrack (song) == -1 && song.mixer.size() == mixers);
}

TEST (flpl190dx_assign_and_unassign_a_new_instrument_track)
{
    using namespace lynx;
    int vox = -1;
    auto song = flpl190dx::songWithAudioClip (vox); // a clip on track 3
    // FL: generators only
    CHECK (trackmodes::canBeInstrumentTrack (song, 2) && ! trackmodes::canBeInstrumentTrack (song, vox) && ! trackmodes::canBeInstrumentTrack (song, 99));
    CHECK (trackmodes::assignToNewInstrumentTrack (song, vox) == -1);
    // the first free track becomes channel 2's instrument track, on its own mixer track
    const auto track = trackmodes::assignToNewInstrumentTrack (song, 2);
    CHECK (track == 0);
    if (track == 0)
        CHECK (song.playlistTracks[0].mode == PlaylistTrackMode::instrument && song.playlistTracks[0].channel == 2
               && song.playlistTracks[0].mixerTrack == song.channels[2].mixerTrack && song.channels[2].mixerTrack > 0);
    CHECK (trackmodes::instrumentTracksOf (song, 2) == std::vector<int> { 0 });
    // the menu now offers "Unassign from instrument track": every instrument track of the channel goes back
    CHECK (trackmodes::unassignFromInstrumentTracks (song, 2) == 1 && trackmodes::instrumentTracksOf (song, 2).empty());
    CHECK (song.playlistTracks[0].mode == PlaylistTrackMode::unassigned);
    CHECK (trackmodes::unassignFromInstrumentTracks (song, 2) == 0);
}

TEST (flpl190dx_new_instrument_track_with_a_new_channel)
{
    using namespace lynx;
    int vox = -1;
    auto song = flpl190dx::songWithAudioClip (vox);
    const auto channels = song.channels.size();
    ChannelDef synth;
    synth.name = "Lead";
    synth.instrument = InstrumentKind::synth;
    synth.mixerTrack = 3; // (ignored: a new instrument track takes a free mixer track)
    // the "+" menu / More plugins...: a new channel on the first free track
    const auto ch = trackmodes::addInstrumentTrack (song, synth);
    CHECK (ch == (int) channels && song.channels.size() == channels + 1);
    if (ch == (int) channels)
    {
        CHECK (song.channels[(std::size_t) ch].name == "Lead" && song.channels[(std::size_t) ch].mixerTrack == 6);
        CHECK (song.playlistTracks[0].mode == PlaylistTrackMode::instrument && song.playlistTracks[0].channel == ch && song.playlistTracks[0].name == "Lead");
    }
    // on a chosen unassigned track (Track mode › Instrument track › a generator)
    const auto ch2 = trackmodes::addInstrumentTrack (song, synth, 7);
    CHECK (ch2 == ch + 1 && song.playlistTracks[7].channel == ch2);
    // an instrument track already: the UI replaces its generator instead; the core refuses a second channel there
    const auto count = song.channels.size();
    CHECK (trackmodes::addInstrumentTrack (song, synth, 7) == -1 && song.channels.size() == count);
    // not a generator: nothing
    ChannelDef audio;
    audio.instrument = InstrumentKind::audioClip;
    CHECK (trackmodes::addInstrumentTrack (song, audio) == -1 && song.channels.size() == count);
}

TEST (flpl190dx_one_click_recording_targets)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    CHECK (song.mixer.size() > 6);
    if (song.mixer.size() <= 6)
        return;
    song.mixer[4].inputChannel = 0;

    // Into playlist, nothing armed, the selected insert has an input: it is armed, Song mode, Record + Play
    auto p = oneclick::plan (song, {}, 4, oneclick::Into::playlist);
    CHECK (p.insert == 4 && p.armInsert && ! p.askInput && p.songMode && p.recordOn && p.startPlayback && p.record == std::vector<int> { 4 } && ! p.loadEdison);
    // ... no input yet: FL asks for one first
    p = oneclick::plan (song, {}, 2, oneclick::Into::playlist);
    CHECK (p.insert == 2 && p.armInsert && p.askInput && p.record == std::vector<int> { 2 });
    // ... inserts already armed: they record as they are, nothing else is armed
    p = oneclick::plan (song, { 5, 6 }, 2, oneclick::Into::playlist);
    CHECK (p.record == std::vector<int> ({ 5, 6 }) && ! p.armInsert && p.recordOn);
    // ... the Master selected (LYNX): the first insert with an input
    p = oneclick::plan (song, {}, 0, oneclick::Into::playlist);
    CHECK (p.insert == 4 && ! p.askInput);
    // Into Edison: Edison recording on play on the selected insert, then Play; no Record switch, no Song mode
    p = oneclick::plan (song, {}, 3, oneclick::Into::edison);
    CHECK (p.insert == 3 && p.loadEdison && p.startPlayback && ! p.recordOn && ! p.songMode && p.record.empty());
    // FL's modes
    CHECK ((int) oneclick::Into::edison == 3 && (int) oneclick::Into::playlist == 2);
}

TEST (flpl190dx_latency_compensation_switch_in_samples)
{
    using namespace lynx;
    takes::Latency l;
    l.driverInput = 128;
    l.driverOutput = 256;
    l.manual = 10;
    l.pdc = 64;
    CHECK (l.total() == 458);
    // on (FL's default): the whole compensation; off: none at all (FUN_0126c040: "switch ? latency : 0")
    CHECK (takes::compensation (l, true) == 458 && takes::compensation (l, false) == 0);
    l.measured = 300; // a measured round trip replaces the driver's figures
    CHECK (takes::compensation (l, true) == 374 && takes::compensation (l, false) == 0);

    // the take's placement, at 0.04 ticks per sample: on = moved back by 374 samples (14.96 ticks), off = where it arrived
    const auto on = takes::place (1000.0, 2000.0, 0.04, takes::compensation (l, true));
    const auto off = takes::place (1000.0, 2000.0, 0.04, takes::compensation (l, false));
    CHECK (std::abs (off.exactStart - 1000.0) < 1.0e-9 && std::abs (on.exactStart - (1000.0 - 374.0 * 0.04)) < 1.0e-9);
    CHECK (std::abs ((off.exactStart - on.exactStart) / 0.04 - 374.0) < 1.0e-6 && off.latencyTicks == 0.0);
}
