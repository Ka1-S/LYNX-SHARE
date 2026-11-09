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
