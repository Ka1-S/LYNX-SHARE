// FL's "Track mode › Audio track › N. <insert>": a Playlist track linked to the mixer track the user picks, not only the
// next free one (issue #15's "route the track itself to 14", issue #122's links kept on reopen).

namespace tracklink
{
// tracks: 0 = unassigned, 1 = audio track on insert 6, 2 = unassigned "Vocals"; insert 7 a prepared vocal chain
struct Setup
{
    lynx::Song song = lynx::Song::makeDefault(); // channels 0..4 on inserts 1..5
    int take = -1;
    Setup()
    {
        using namespace lynx;
        while (song.mixer.size() < 16)
            song.mixer.emplace_back().name = "Insert " + std::to_string (song.mixer.size());
        song.playlistTracks.resize (3);
        song.playlistTracks[1].mode = PlaylistTrackMode::audio;
        song.playlistTracks[1].mixerTrack = 6;
        song.playlistTracks[2].name = "Vocals";
        song.playlistTracks[2].colour = 0xFF3366CC;
        song.mixer[7].name = "Vox chain";
        song.mixer[7].colour = 0xFF20C060;
        song.mixer[7].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
        ChannelDef c;
        c.name = "Take";
        c.instrument = InstrumentKind::audioClip;
        song.channels.push_back (c);
        take = (int) song.channels.size() - 1;
    }
    int clip (int track, std::int64_t start)
    {
        lynx::Clip c;
        c.kind = lynx::ClipKind::audio;
        c.channel = take;
        c.track = track;
        c.start = start;
        c.length = 384;
        song.playlist.push_back (c);
        return (int) song.playlist.size() - 1;
    }
};
} // namespace tracklink

TEST (tracklink_unassigned_track_links_to_a_prepared_insert)
{
    using namespace lynx;
    tracklink::Setup s;
    s.clip (2, 0);
    // an insert in use (effects, a name) is a valid target; FL (round 190cp, FUN_012f38f0): an insert with its own name
    // gives it to the track, the track keeps its own (non-default) colour and the insert takes it: one name, one colour
    CHECK (trackmodes::canLinkToMixerTrack (s.song, 2, 7));
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 7));
    const auto& t = s.song.playlistTracks[2];
    CHECK (t.mode == PlaylistTrackMode::audio && t.mixerTrack == 7 && t.channel == -1 && t.name == "Vox chain" && t.colour == 0xFF3366CC);
    CHECK (s.song.mixer[7].name == "Vox chain" && s.song.mixer[7].colour == 0xFF3366CC && s.song.mixer[7].effects.size() == 1);
    CHECK (trackmodes::describe (s.song, 2) == "(Audio track, linked to mixer track 7)" && trackmodes::armableInsert (s.song, 2) == 7);
    // the audio clips already on the track play through it, as with Track mode › Audio track
    CHECK (s.song.channels[(std::size_t) s.take].mixerTrack == 7);
    // the insert it already has: nothing changes
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 7) && s.song.playlistTracks[2].mixerTrack == 7);
}

TEST (tracklink_an_unnamed_track_takes_the_insert_name)
{
    using namespace lynx;
    tracklink::Setup s;
    // FL: "Track 1" › Audio track › "6. Insert 6" renames the track "Insert 6"
    CHECK (trackmodes::linkToMixerTrack (s.song, 0, 9));
    CHECK (s.song.playlistTracks[0].name == "Insert 9" && s.song.mixer[9].name == "Insert 9" && s.song.mixer[9].colour == 0);
    s.song.mixer[10].name.clear();
    s.song.playlistTracks.resize (5);
    CHECK (trackmodes::linkToMixerTrack (s.song, 3, 10) && s.song.playlistTracks[3].name == "Insert 10" && s.song.mixer[10].name == "Insert 10");
    // an unnamed track with the default colour takes the insert's name and colour
    CHECK (trackmodes::linkToMixerTrack (s.song, 4, 7) && s.song.playlistTracks[4].name == "Vox chain" && s.song.playlistTracks[4].colour == 0xFF20C060);
    CHECK (s.song.mixer[7].name == "Vox chain" && s.song.mixer[7].colour == 0xFF20C060);
    // a named track and an insert without a name of its own: the insert takes the track's name and colour
    s.song.mixer[11].colour = 0xFF808000;
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 11));
    CHECK (s.song.playlistTracks[2].name == "Vocals" && s.song.playlistTracks[2].colour == 0xFF3366CC);
    CHECK (s.song.mixer[11].name == "Vocals" && s.song.mixer[11].colour == 0xFF3366CC);
}

TEST (tracklink_audio_track_relinks_and_the_old_insert_is_reset)
{
    using namespace lynx;
    tracklink::Setup s;
    s.song.mixer[6].name = "Old bus";
    s.song.mixer[6].colour = 0xFFAA0000;
    s.song.mixer[6].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
    const auto a = s.clip (1, 0);
    CHECK (trackmodes::followAudioTracks (s.song, { a }) == 1 && s.song.channels[(std::size_t) s.take].mixerTrack == 6);
    CHECK (trackmodes::linkToMixerTrack (s.song, 1, 7));
    CHECK (s.song.playlistTracks[1].mode == PlaylistTrackMode::audio && s.song.playlistTracks[1].mixerTrack == 7);
    CHECK (s.song.channels[(std::size_t) s.take].mixerTrack == 7); // the clip on the track follows
    // FL: the previous insert goes back to its default name and colour; its effects stay
    CHECK (s.song.mixer[6].name == "Insert 6" && s.song.mixer[6].colour == 0 && s.song.mixer[6].effects.size() == 1);
    // the unnamed track takes the new insert's name and colour
    CHECK (s.song.playlistTracks[1].name == "Vox chain" && s.song.playlistTracks[1].colour == 0xFF20C060);
    // the previous insert is free for another track now
    CHECK (trackmodes::canLinkToMixerTrack (s.song, 0, 6));
}

TEST (tracklink_clips_follow_by_followaudiotracks_rules)
{
    using namespace lynx;
    tracklink::Setup s;
    // the same take also sits on another audio track (insert 6): FL's "Make clip unique?" case, which a relink does
    // not answer: the channel keeps its route (issue #15)
    s.song.channels[(std::size_t) s.take].mixerTrack = 3;
    s.clip (1, 0);
    s.clip (2, 1000);
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 9));
    CHECK (s.song.channels[(std::size_t) s.take].mixerTrack == 3 && s.song.channels.size() == 6);
    // a clip of a channel used only on unassigned tracks besides this one simply follows
    tracklink::Setup u;
    u.clip (0, 0);
    u.clip (2, 1000);
    CHECK (trackmodes::linkToMixerTrack (u.song, 2, 9) && u.song.channels[(std::size_t) u.take].mixerTrack == 9);
}

TEST (tracklink_instrument_track_becomes_an_audio_track)
{
    using namespace lynx;
    tracklink::Setup s;
    // as Track mode › Audio track does: the track becomes an audio track, its former channel stays on its insert
    CHECK (trackmodes::makeInstrumentTrack (s.song, 0, 4) && s.song.playlistTracks[0].mixerTrack == 5);
    const auto fiveName = s.song.mixer[5].name;
    const auto fiveColour = s.song.mixer[5].colour;
    CHECK (trackmodes::linkToMixerTrack (s.song, 0, 7));
    const auto& t = s.song.playlistTracks[0];
    CHECK (t.mode == PlaylistTrackMode::audio && t.channel == -1 && t.mixerTrack == 7);
    // FL: the (named) track gives its name and colour to the insert; its former insert is the channel's, not reset
    CHECK (s.song.channels[4].mixerTrack == 5 && ! t.name.empty() && s.song.mixer[7].name == t.name && s.song.mixer[7].colour == t.colour);
    CHECK (s.song.mixer[7].effects.size() == 1 && s.song.mixer[5].name == fiveName && s.song.mixer[5].colour == fiveColour);
}

TEST (tracklink_refuses_master_range_linked_and_reserved_inserts)
{
    using namespace lynx;
    tracklink::Setup s;
    const auto size = (int) s.song.mixer.size();
    CHECK (! trackmodes::canLinkToMixerTrack (s.song, 2, 0) && ! trackmodes::linkToMixerTrack (s.song, 2, 0)); // Master
    CHECK (! trackmodes::linkToMixerTrack (s.song, 2, -1) && ! trackmodes::linkToMixerTrack (s.song, 2, size));
    CHECK (! trackmodes::linkToMixerTrack (s.song, -1, 7) && ! trackmodes::linkToMixerTrack (s.song, 500, 7));
    // one Playlist track per insert: insert 6 belongs to track 1
    CHECK (! trackmodes::canLinkToMixerTrack (s.song, 2, 6) && ! trackmodes::linkToMixerTrack (s.song, 2, 6));
    CHECK (trackmodes::canLinkToMixerTrack (s.song, 1, 6));
    // an online session: inserts reserved for another participant are his
    SessionParticipant host, guest;
    host.tag = session::hostTag;
    guest.tag = 2;
    guest.insertFirst = 10;
    guest.insertCount = 3;
    s.song.session.participants = { host, guest };
    CHECK (! trackmodes::canLinkToMixerTrack (s.song, 2, 11) && ! trackmodes::linkToMixerTrack (s.song, 2, 11));
    CHECK (trackmodes::canLinkToMixerTrack (s.song, 2, 13));
    // ... and the guest may use them
    s.song.session.me = 2;
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 11));
    CHECK (s.song.playlistTracks[2].mode == PlaylistTrackMode::audio && s.song.playlistTracks[2].mixerTrack == 11);
    // nothing changed on a refusal
    tracklink::Setup r;
    const auto before = r.song.playlistTracks;
    CHECK (! trackmodes::linkToMixerTrack (r.song, 2, 6) && r.song.playlistTracks == before);
}

TEST (tracklink_new_mixer_track_always_adds_an_insert)
{
    using namespace lynx;
    tracklink::Setup s;
    // FL's "New mixer track(s)" (FUN_012ec590 / FUN_012a71e0): a new insert past the last one, even with free ones
    CHECK (trackmodes::canAddMixerTrack (s.song) && trackmodes::freeMixerTrack (s.song) == 8);
    s.song.mixer[6].name = "Insert 6";
    s.song.playlistTracks[1].name = "Guitar";
    CHECK (s.song.mixer.size() == 17); // Master + inserts 1..16
    CHECK (trackmodes::assignAudioTracks (s.song, { 1 }, trackmodes::newMixerTrack) == 1);
    CHECK (s.song.mixer.size() == 18 && s.song.playlistTracks[1].mixerTrack == 17);
    CHECK (s.song.mixer[17].name == "Guitar" && s.song.mixer[6].name == "Insert 6");
    // an unassigned, unnamed track: both become "Insert N"
    CHECK (trackmodes::assignAudioTracks (s.song, { 0 }, trackmodes::newMixerTrack) == 1);
    CHECK (s.song.playlistTracks[0].mixerTrack == 18 && s.song.playlistTracks[0].name == "Insert 18" && s.song.mixer[18].name == "Insert 18");
    // greyed when the mixer is full: nothing is added or linked
    while ((int) s.song.mixer.size() < maxMixerTracks)
        s.song.mixer.emplace_back().name = "Insert " + std::to_string (s.song.mixer.size());
    const auto tracksBefore = s.song.playlistTracks;
    CHECK (! trackmodes::canAddMixerTrack (s.song) && trackmodes::addMixerTrack (s.song) == -1);
    CHECK (trackmodes::assignAudioTracks (s.song, { 2 }, trackmodes::newMixerTrack) == 0);
    CHECK ((int) s.song.mixer.size() == maxMixerTracks && s.song.playlistTracks == tracksBefore);
    CHECK (! trackmodes::audioTrackMenu (s.song, 2).canAddMixerTrack);
    // the Master and inserts that do not exist are no pick
    CHECK (trackmodes::assignAudioTracks (s.song, { 2 }, 0) == 0 && trackmodes::assignAudioTracks (s.song, { 2 }, maxMixerTracks) == 0);
}

TEST (tracklink_new_mixer_track_skips_inserts_reserved_in_a_session)
{
    using namespace lynx;
    tracklink::Setup s;
    SessionParticipant host, guest;
    host.tag = session::hostTag;
    guest.tag = 2;
    guest.insertFirst = 17;
    guest.insertCount = 2;
    s.song.session.participants = { host, guest };
    // the host's new mixer track is 19: 17 and 18 are added as plain inserts for the guest
    CHECK (trackmodes::assignAudioTracks (s.song, { 2 }, trackmodes::newMixerTrack) == 1);
    CHECK (s.song.playlistTracks[2].mixerTrack == 19 && s.song.mixer.size() == 20 && s.song.mixer[17].name == "Insert 17");
    // review L3: the free mixer track an audio / instrument track takes is never one reserved for another participant
    tracklink::Setup f;
    guest.insertFirst = 8;
    guest.insertCount = 3;
    f.song.session.participants = { host, guest };
    CHECK (trackmodes::freeMixerTrack (f.song) == 11);
    CHECK (trackmodes::makeAudioTrack (f.song, 0) && f.song.playlistTracks[0].mixerTrack == 11);
    // ... while the guest takes them
    f.song.session.me = 2;
    CHECK (trackmodes::freeMixerTrack (f.song) == 8);
}

TEST (tracklink_a_pick_links_every_selected_track)
{
    using namespace lynx;
    tracklink::Setup s;
    s.song.playlistTracks.resize (6);
    // FL (FUN_012ec590): the first selected track takes the pick, each next one the next insert no track is linked to
    // (here 9 is linked to track 5, so 10), new inserts past the last one
    CHECK (trackmodes::linkToMixerTrack (s.song, 5, 9));
    CHECK (trackmodes::assignAudioTracks (s.song, { 0, 2, 3 }, 8) == 3);
    CHECK (s.song.playlistTracks[0].mixerTrack == 8 && s.song.playlistTracks[2].mixerTrack == 10 && s.song.playlistTracks[3].mixerTrack == 11);
    CHECK (s.song.playlistTracks[5].mixerTrack == 9);
    CHECK (trackmodes::assignAudioTracks (s.song, { 3, 4 }, 16) == 2);
    CHECK (s.song.playlistTracks[3].mixerTrack == 16 && s.song.playlistTracks[4].mixerTrack == 17 && s.song.mixer.size() == 18);
    // track 3's previous insert is reset; the track's stored name ("Insert 11", from its first link) travels to the new
    // insert (FL's audio-track branch: the insert takes the track's name)
    CHECK (s.song.mixer[11].name == "Insert 11" && s.song.playlistTracks[3].name == "Insert 11" && s.song.mixer[16].name == "Insert 11");
    CHECK (s.song.playlistTracks[4].name == "Insert 17" && s.song.mixer[17].name == "Insert 17");
    // the menu's track (2, on 10) ticks its own insert with track 0 selected above it: track 0 can't take 10, so the
    // pick goes on to track 2, which keeps it
    CHECK (trackmodes::assignAudioTracks (s.song, { 0, 2 }, 10) == 1);
    CHECK (s.song.playlistTracks[0].mixerTrack == 8 && s.song.playlistTracks[2].mixerTrack == 10);
    // "New mixer track(s)" with two tracks: two new inserts
    CHECK (trackmodes::assignAudioTracks (s.song, { 0, 2 }, trackmodes::newMixerTrack) == 2);
    CHECK (s.song.playlistTracks[0].mixerTrack == 18 && s.song.playlistTracks[2].mixerTrack == 19 && s.song.mixer.size() == 20);
}

TEST (tracklink_audio_track_menu_model)
{
    using namespace lynx;
    tracklink::Setup s;
    s.song.playlistTracks[2].name.clear(); // FL builds the entries from the inserts only
    s.song.mixer[9].name.clear();
    auto menu = trackmodes::audioTrackMenu (s.song, 2);
    CHECK (menu.canAddMixerTrack && menu.inserts.size() == 16); // inserts 1..16, no Master
    if (menu.inserts.size() == 16)
    {
        CHECK (menu.inserts[0].mixer == 1 && menu.inserts[6].mixer == 7 && menu.inserts[6].name == "Vox chain");
        CHECK (menu.inserts[8].name.empty()); // the UI shows "9. Insert 9"
        // insert 6 is track 1's: greyed, its text unchanged (no "(other track)"); the others enabled, none ticked
        CHECK (! menu.inserts[5].enabled && menu.inserts[5].name == "Insert 6");
        CHECK (std::count_if (menu.inserts.begin(), menu.inserts.end(), [] (const trackmodes::AudioTrackMenuItem& i) { return i.ticked; }) == 0);
        CHECK (std::count_if (menu.inserts.begin(), menu.inserts.end(), [] (const trackmodes::AudioTrackMenuItem& i) { return ! i.enabled; }) == 1);
    }
    // track 1's own insert is ticked and enabled
    menu = trackmodes::audioTrackMenu (s.song, 1);
    CHECK (menu.inserts.size() == 16 && menu.inserts[5].ticked && menu.inserts[5].enabled);
    // review L6: FL ticks the stored link whatever the mode (an instrument track's insert)
    CHECK (trackmodes::makeInstrumentTrack (s.song, 0, 4));
    menu = trackmodes::audioTrackMenu (s.song, 0);
    CHECK (menu.inserts.size() == 16 && menu.inserts[4].ticked && menu.inserts[4].enabled);
    // an insert reserved for another participant is greyed too (LYNX's online sessions)
    SessionParticipant host, guest;
    host.tag = session::hostTag;
    guest.tag = 2;
    guest.insertFirst = 12;
    guest.insertCount = 1;
    s.song.session.participants = { host, guest };
    menu = trackmodes::audioTrackMenu (s.song, 2);
    CHECK (menu.inserts.size() == 16 && ! menu.inserts[11].enabled && menu.inserts[12].enabled);
    // a track past the stored ones: nothing ticked, the free inserts enabled
    menu = trackmodes::audioTrackMenu (s.song, 40);
    CHECK (menu.inserts.size() == 16 && ! menu.inserts[6].ticked && menu.inserts[6].enabled);
}

TEST (tracklink_relinked_audio_track_names_the_new_insert)
{
    using namespace lynx;
    tracklink::Setup s;
    // a named audio track (FL: its name was set by the first link) relinks: the old insert is reset, the new one takes
    // the track's name and colour even when it had one of its own (FL's audio-track branch of FUN_012f38f0)
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 11));
    CHECK (s.song.mixer[11].name == "Vocals" && s.song.mixer[11].colour == 0xFF3366CC);
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 7));
    CHECK (s.song.mixer[11].name == "Insert 11" && s.song.mixer[11].colour == 0);
    CHECK (s.song.playlistTracks[2].name == "Vocals" && s.song.mixer[7].name == "Vocals" && s.song.mixer[7].colour == 0xFF3366CC);
    CHECK (s.song.mixer[7].effects.size() == 1);
    // hasOwnName: a stored name other than the default "Insert N"
    CHECK (! trackmodes::hasOwnName (s.song, 11) && trackmodes::hasOwnName (s.song, 7) && ! trackmodes::hasOwnName (s.song, 0));
    s.song.mixer[3].name.clear();
    CHECK (! trackmodes::hasOwnName (s.song, 3) && ! trackmodes::hasOwnName (s.song, 99));
}

TEST (tracklink_link_follows_insert_delete_and_move)
{
    using namespace lynx;
    tracklink::Setup s;
    s.song.mixer[9].name = "Keys";
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 9));
    // deleting an insert before it: the link follows the insert to 8
    CHECK (mixerops::deleteTrack (s.song, 3));
    CHECK (s.song.playlistTracks[2].mixerTrack == 8 && s.song.mixer[8].name == "Keys");
    // moving it right: the link moves with it
    std::vector<int> sel { 8 };
    CHECK (mixerops::moveTracks (s.song, sel, 1));
    CHECK (s.song.playlistTracks[2].mixerTrack == 9 && s.song.mixer[9].name == "Keys");
    // deleting it unlinks the track (as for any audio track)
    CHECK (mixerops::deleteTracks (s.song, { 9 }) == 1);
    CHECK (s.song.playlistTracks[2].mode == PlaylistTrackMode::unassigned);
}

TEST (tracklink_explicit_insert_survives_flp_export_and_import)
{
    using namespace lynx;
    tracklink::Setup s;
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 12) && trackmodes::linkToMixerTrack (s.song, 0, 7));
    flpexport::Options options;
    options.lynxOnly = flpexport::LynxOnly::substitute;
    flpexport::Report report;
    const auto back = importFlp (flp::parseProject (flpexport::exportFlp (s.song, options, report)));
    CHECK (back.playlistTracks.size() >= 3);
    if (back.playlistTracks.size() >= 3)
    {
        CHECK (back.playlistTracks[2].mode == PlaylistTrackMode::audio && back.playlistTracks[2].mixerTrack == 12);
        CHECK (back.playlistTracks[0].mode == PlaylistTrackMode::audio && back.playlistTracks[0].mixerTrack == 7);
    }
}
