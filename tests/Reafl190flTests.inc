// Round 190fl: FL's mixer menu tail found by REA of FLEngine_x64.dll (FL Studio 2026): the "+" menu's "Increase number of
// tracks to..." (AddOneMixerTrackMenuClick 0x12a8bb0, Tag -1), "Plugin delay compensation › Set for all tracks"
// (SetupPDCMenuClick 0x12a3830 → 0x12a3880(1, 1) → 0x12a3920(-1)) and "Select › Channels and playlist tracks routed to
// this track" (SelectLinkedMenuClick 0x129d110). Evidence: /mnt/e/lynx-work/reafl190fl/FL-FACTS.md.

#include "core/MixerOps.h"

namespace reafl190fl
{
inline lynx::Song mixerOf (int inserts)
{
    lynx::Song s;
    s.mixer.clear();
    s.mixer.resize ((std::size_t) inserts + 1);
    return s;
}
} // namespace reafl190fl

TEST (reafl190fl_increase_number_of_tracks_to)
{
    auto s = reafl190fl::mixerOf (125);
    CHECK (lynx::mixerops::insertCount (s) == 125);
    // FL refuses below the current count and above 500 ("Track count must be between 125 and 500.")
    CHECK (! lynx::mixerops::validInsertCount (s, 124));
    CHECK (lynx::mixerops::validInsertCount (s, 125));
    CHECK (lynx::mixerops::validInsertCount (s, 500));
    CHECK (! lynx::mixerops::validInsertCount (s, 501));
    CHECK (lynx::mixerops::increaseInsertsTo (s, 124) == 0 && lynx::mixerops::insertCount (s) == 125);
    CHECK (lynx::mixerops::increaseInsertsTo (s, 125) == 0);
    s.mixer[125].name = "Last";
    CHECK (lynx::mixerops::increaseInsertsTo (s, 130) == 5);
    CHECK (lynx::mixerops::insertCount (s) == 130);
    CHECK (s.mixer[125].name == "Last"); // the new ones come at the end
    CHECK (lynx::mixerops::increaseInsertsTo (s, 500) == 370 && (int) s.mixer.size() == lynx::maxMixerTracks);
    CHECK (lynx::mixerops::increaseInsertsTo (s, 501) == 0);
}

TEST (reafl190fl_pdc_set_for_all_tracks_resets_manual_latency)
{
    auto s = reafl190fl::mixerOf (4);
    s.mixer[1].latencyMs = 12.0f;
    s.mixer[3].latencyMs = -3.5f;
    CHECK (lynx::mixerops::setPdcForAllTracks (s) == 2);
    for (const auto& t : s.mixer)
        CHECK (t.latencyMs == 0.0f);
    CHECK (lynx::mixerops::setPdcForAllTracks (s) == 0);
}

TEST (reafl190fl_select_channels_and_playlist_tracks_routed)
{
    auto s = reafl190fl::mixerOf (6);
    s.channels.resize (4);
    s.channels[0].mixerTrack = 2;
    s.channels[1].mixerTrack = 3;
    s.channels[2].mixerTrack = 2;
    s.channels[3].mixerTrack = 0; // Master: never part of the selection
    s.playlistTracks.resize (6);
    s.playlistTracks[0].mode = lynx::PlaylistTrackMode::audio;      // linked to insert 2
    s.playlistTracks[0].mixerTrack = 2;
    s.playlistTracks[1].mode = lynx::PlaylistTrackMode::instrument; // channel 1 → insert 3
    s.playlistTracks[1].channel = 1;
    s.playlistTracks[2].mode = lynx::PlaylistTrackMode::instrument; // channel 2 → insert 2
    s.playlistTracks[2].channel = 2;
    s.playlistTracks[3].mixerTrack = 2;                       // unassigned: its stale insert number doesn't count
    s.playlistTracks[4].mode = lynx::PlaylistTrackMode::audio;      // insert 5
    s.playlistTracks[4].mixerTrack = 5;
    s.playlistTracks[5].mode = lynx::PlaylistTrackMode::instrument; // a channel that doesn't exist
    s.playlistTracks[5].channel = 9;
    CHECK ((lynx::mixerops::playlistTracksOn (s, { 2 }) == std::vector<int> { 0, 2 }));
    CHECK ((lynx::mixerops::playlistTracksOn (s, { 2, 3 }) == std::vector<int> { 0, 1, 2 }));
    CHECK ((lynx::mixerops::playlistTracksOn (s, { 5 }) == std::vector<int> { 4 }));
    CHECK (lynx::mixerops::playlistTracksOn (s, { 0 }).empty());
    CHECK ((lynx::mixerops::channelsOn (s, std::vector<int> { 2, 3 }) == std::vector<int> { 0, 1, 2 }));
    CHECK (lynx::mixerops::channelsOn (s, std::vector<int> { 0 }).empty());
}
