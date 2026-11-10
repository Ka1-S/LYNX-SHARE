// --- Round 186c: FL's "Delete" on the selected mixer tracks (REA FUN_012a5b60 / FUN_012a5280, checked in FL 2026) -----

#include "core/MixerOps.h"

TEST (mixer186c_delete_tracks_reroutes_feeders_and_skips_master)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (8);
    // 1 → 2 (only), 2 → 5 and the Master; 3 → 2 as a level-0 sidechain link (no signal)
    song.mixer[1].toMaster = false;
    song.mixer[1].sends = { { 2, 0.5f } };
    song.mixer[2].sends = { { 5, 1.0f } };
    song.mixer[3].sends = { { 2, 0.0f } };
    song.mixer[4].name = "keep";

    CHECK (mixerops::deleteTracks (song, { 2, 0, 2, 99 }) == 1); // (the Master, duplicates and out-of-range left out)
    CHECK (song.mixer.size() == 9);
    // insert 1 now sends where insert 2 sent: insert 5 (now 4) and the Master, at its own send level
    CHECK (song.mixer[1].toMaster);
    CHECK (song.mixer[1].sends.size() == 1 && song.mixer[1].sends[0].target == 4 && song.mixer[1].sends[0].level == 0.5f);
    CHECK (song.mixer[2].sends.empty()); // the sidechain link to the deleted insert is gone, nothing added
    CHECK (song.mixer[3].name == "keep");

    // several at once: one shift each, from the right
    song.mixer[5].name = "five";
    CHECK (mixerops::deleteTracks (song, { 1, 3 }) == 2);
    CHECK (song.mixer[3].name == "five" && song.mixer.size() == 9);
}

TEST (mixer186c_delete_tracks_linked_playlist_tracks)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (6);
    song.channels.resize (1);
    song.channels[0].mixerTrack = 0;
    song.channels.push_back (song.channels.front());
    song.channels.push_back (song.channels.front());
    song.channels[1].mixerTrack = 3;
    song.channels[2].mixerTrack = 4;
    song.playlistTracks.resize (4);
    auto& audio = song.playlistTracks[0];
    audio.name = "Vox";
    audio.mode = PlaylistTrackMode::audio;
    audio.mixerTrack = 2;
    auto& inst = song.playlistTracks[1];
    inst.name = "Bass";
    inst.mode = PlaylistTrackMode::instrument;
    inst.channel = 1;
    inst.mixerTrack = 3;

    CHECK ((mixerops::playlistTracksLinkedTo (song, { 2 }) == std::vector<int> { 0 }));
    CHECK ((mixerops::playlistTracksLinkedTo (song, { 3, 5 }) == std::vector<int> { 1 })); // (by the channel's insert)
    CHECK (mixerops::playlistTracksLinkedTo (song, { 4, 5 }).empty());

    // "Unlink tracks and channels": the Playlist track stays, named, unassigned; the channel stays (to the Master)
    auto unlink = song;
    CHECK (mixerops::deleteTracks (unlink, { 2, 3 }, mixerops::LinkedPlaylistTracks::unlink) == 2);
    CHECK (unlink.playlistTracks[0].name == "Vox" && unlink.playlistTracks[0].mode == PlaylistTrackMode::unassigned);
    CHECK (unlink.playlistTracks[1].name == "Bass" && unlink.playlistTracks[1].mode == PlaylistTrackMode::unassigned
           && unlink.playlistTracks[1].channel == -1 && unlink.playlistTracks[1].mixerTrack == -1);
    CHECK (unlink.channels.size() == 3 && unlink.channels[1].mixerTrack == 0 && unlink.channels[2].mixerTrack == 2);

    // "Delete tracks and channels": the Playlist tracks reset ("Track N"), the instrument track's channel goes
    auto del = song;
    CHECK (mixerops::deleteTracks (del, { 2, 3 }, mixerops::LinkedPlaylistTracks::deleteToo) == 2);
    CHECK (del.playlistTracks[0].name.empty() && del.playlistTracks[0].mode == PlaylistTrackMode::unassigned);
    CHECK (del.playlistTracks[1].name.empty() && del.playlistTracks[1].mode == PlaylistTrackMode::unassigned);
    CHECK (del.channels.size() == 2 && del.channels[1].mixerTrack == 2);
}

TEST (mixer186c_keep_lost_links_option)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (6);
    song.mixer[3].name = "Bus";
    song.mixer[1].sends = { { 3, 0.7f } };
    song.mixer[2].sends = { { 3, 0.0f } }; // a side-chain link

    auto fl = song; // the option off (FL): the links go
    mixerops::deleteTracks (fl, { 3 });
    CHECK (fl.mixer[1].lostSends.empty() && fl.mixer[2].lostSends.empty());

    mixerops::deleteTracks (song, { 3 }, mixerops::LinkedPlaylistTracks::unlink, true);
    CHECK ((song.mixer[1].lostSends == std::vector<LostSend> { { "Bus", 0.7f } }));
    CHECK ((song.mixer[2].lostSends == std::vector<LostSend> { { "Bus", 0.0f } }));
    CHECK (song.mixer[2].sends.empty()); // (no sound goes anywhere through it)

    // reconnect to insert 4 at its level; not to itself; remove the rest
    CHECK (! mixerops::reconnectLostSend (song, 2, 0, 2));
    CHECK (mixerops::reconnectLostSend (song, 2, 0, 4));
    CHECK (song.mixer[2].lostSends.empty() && song.mixer[2].sends.size() == 1 && song.mixer[2].sends[0].target == 4
           && song.mixer[2].sends[0].level == 0.0f);
    mixerops::removeLostSends (song, 1);
    CHECK (song.mixer[1].lostSends.empty());

    // an unnamed insert is kept by its default name
    song.mixer[4].name.clear();
    song.mixer[5].sends = { { 4, 1.0f } };
    mixerops::deleteTracks (song, { 4 }, mixerops::LinkedPlaylistTracks::unlink, true);
    CHECK (song.mixer[4].lostSends.size() == 1 && song.mixer[4].lostSends[0].to == "Insert 4");
}

TEST (mixer186c_groups_follow_the_playlist_and_arrange)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (8);
    for (int t = 1; t <= 8; ++t)
        song.mixer[(std::size_t) t].name = "T" + std::to_string (t);
    song.mixer[8].separator = true; // a mixer group of the user's own, on an insert no Playlist track names
    song.playlistTracks.resize (6);
    auto audio = [&song] (int i, int insert, bool grouped) {
        auto& pt = song.playlistTracks[(std::size_t) i];
        pt.mode = PlaylistTrackMode::audio;
        pt.mixerTrack = insert;
        pt.grouped = grouped;
    };
    // Playlist: [Drums header → 5] [kick → 3] [snare → 6]   [Vox → 2]   [Bass group: 7, 7 again]
    audio (0, 5, false);
    audio (1, 3, true);
    audio (2, 6, true);
    audio (3, 2, false);
    audio (4, 7, false);
    audio (5, 7, true);
    song.playlistTracks[0].collapsed = true;

    CHECK ((mixerops::playlistOrderInserts (song) == std::vector<int> { 5, 3, 6, 2, 7 }));
    const auto groups = mixerops::playlistGroupInserts (song);
    CHECK (groups.size() == 2 && (groups[0].inserts == std::vector<int> { 5, 3, 6 }) && groups[0].collapsed);
    CHECK ((groups[1].inserts == std::vector<int> { 7 }));

    // 3, 5, 6 aren't side by side: no mixer group yet; nothing else is touched
    CHECK (! mixerops::followPlaylistGroups (song) && song.mixer[8].separator && mixerops::followsPlaylistGroups (song));

    // Arrange: 5, 3, 6, 2, 7 become inserts 1-5 (by name), the others keep their order after them
    const auto moved = mixerops::arrangeLikePlaylist (song);
    CHECK (moved > 0);
    std::vector<std::string> names;
    for (int t = 1; t <= 8; ++t)
        names.push_back (song.mixer[(std::size_t) t].name);
    CHECK ((names == std::vector<std::string> { "T5", "T3", "T6", "T2", "T7", "T1", "T4", "T8" }));
    CHECK (song.playlistTracks[0].mixerTrack == 1 && song.playlistTracks[2].mixerTrack == 3 && song.playlistTracks[5].mixerTrack == 5);
    // the drums are a folded mixer group (separator on 1, closing separator on 4); T8 keeps its own separator
    CHECK (song.mixer[1].separator && ! song.mixer[2].separator && ! song.mixer[3].separator && song.mixer[4].separator);
    CHECK (song.mixer[1].folded && (mixerops::groupOf (song, 2) == std::vector<int> { 1, 2, 3 }));
    CHECK (song.mixer[8].separator && mixerops::followsPlaylistGroups (song));

    // the Playlist header opens and the group breaks up: the mixer follows
    song.playlistTracks[0].collapsed = false;
    CHECK (! mixerops::followsPlaylistGroups (song) && mixerops::followPlaylistGroups (song) && ! song.mixer[1].folded);
    song.playlistTracks[1].grouped = song.playlistTracks[2].grouped = false;
    CHECK (mixerops::followPlaylistGroups (song) && ! song.mixer[1].separator && ! song.mixer[4].separator && song.mixer[8].separator);
}
