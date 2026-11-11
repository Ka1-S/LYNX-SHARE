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
    // an insert in use (effects, a name) is a valid target and is not changed
    CHECK (trackmodes::canLinkToMixerTrack (s.song, 2, 7));
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 7));
    const auto& t = s.song.playlistTracks[2];
    CHECK (t.mode == PlaylistTrackMode::audio && t.mixerTrack == 7 && t.channel == -1 && t.name == "Vocals");
    CHECK (s.song.mixer[7].name == "Vox chain" && s.song.mixer[7].colour == 0xFF20C060 && s.song.mixer[7].effects.size() == 1);
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
    // FL: "Track 1" › Audio track › "6. Insert 6" renames the track "Insert 6"; the insert keeps its name
    CHECK (trackmodes::linkToMixerTrack (s.song, 0, 9));
    CHECK (s.song.playlistTracks[0].name == "Insert 9" && s.song.mixer[9].name == "Insert 9" && s.song.mixer[9].colour == 0);
    s.song.mixer[10].name.clear();
    s.song.playlistTracks.resize (5);
    CHECK (trackmodes::linkToMixerTrack (s.song, 3, 10) && s.song.playlistTracks[3].name == "Insert 10" && s.song.mixer[10].name.empty());
    CHECK (trackmodes::linkToMixerTrack (s.song, 4, 7) && s.song.playlistTracks[4].name == "Vox chain");
    // a named track keeps its name and colour, and so does the insert
    CHECK (trackmodes::linkToMixerTrack (s.song, 2, 11));
    CHECK (s.song.playlistTracks[2].name == "Vocals" && s.song.playlistTracks[2].colour == 0xFF3366CC);
    CHECK (s.song.mixer[11].name == "Insert 11" && s.song.mixer[11].colour == 0);
}

TEST (tracklink_audio_track_relinks_and_the_old_insert_stays)
{
    using namespace lynx;
    tracklink::Setup s;
    s.song.mixer[6].name = "Old bus";
    s.song.mixer[6].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
    const auto a = s.clip (1, 0);
    CHECK (trackmodes::followAudioTracks (s.song, { a }) == 1 && s.song.channels[(std::size_t) s.take].mixerTrack == 6);
    CHECK (trackmodes::linkToMixerTrack (s.song, 1, 7));
    CHECK (s.song.playlistTracks[1].mode == PlaylistTrackMode::audio && s.song.playlistTracks[1].mixerTrack == 7);
    CHECK (s.song.channels[(std::size_t) s.take].mixerTrack == 7); // the clip on the track follows
    CHECK (s.song.mixer[6].name == "Old bus" && s.song.mixer[6].effects.size() == 1); // the old insert is left alone
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
    CHECK (trackmodes::linkToMixerTrack (s.song, 0, 7));
    const auto& t = s.song.playlistTracks[0];
    CHECK (t.mode == PlaylistTrackMode::audio && t.channel == -1 && t.mixerTrack == 7);
    CHECK (s.song.channels[4].mixerTrack == 5 && s.song.mixer[7].name == "Vox chain");
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

TEST (tracklink_next_free_mixer_track_moves_a_linked_track)
{
    using namespace lynx;
    tracklink::Setup s;
    // the menu's "Next free insert" on an audio track: freeMixerTrack + linkToMixerTrack
    const auto spare = trackmodes::freeMixerTrack (s.song);
    CHECK (spare == 8); // 1..5 channels, 6 linked, 7 has effects
    CHECK (trackmodes::linkToMixerTrack (s.song, 1, spare) && s.song.playlistTracks[1].mixerTrack == 8);
    CHECK (s.song.playlistTracks[1].name == "Insert 8" && s.song.mixer[8].name == "Insert 8");
    // on an unassigned (or instrument) track it is makeAudioTrack, as Track mode › Audio track was
    CHECK (trackmodes::makeAudioTrack (s.song, 2) && s.song.playlistTracks[2].mixerTrack == 6 && s.song.mixer[6].name == "Vocals");
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
