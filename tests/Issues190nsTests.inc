// Round 190ns: GitHub issues #231 (Project info › Created), #233 (offline), #234 (Manage shortcuts link), #235 (linked
// track and mixer track colours).
#include "core/PlaylistOps.h"

namespace issues190ns
{
// Song::makeDefault with an audio track linked to a new mixer track (FL's Track mode › Audio track)
inline lynx::Song songWithAudioTrack (int& track, int& mixer)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.playlistTracks.resize (4);
    track = 2;
    song.playlistTracks[2].name = "Vocals";
    song.playlistTracks[2].colour = 0xff8040c0;
    CHECK (trackmodes::makeAudioTrack (song, track));
    mixer = song.playlistTracks[2].mixerTrack;
    return song;
}
} // namespace issues190ns

// #235 (FL 2026 [DECOMP] FUN_012e60f0 → FUN_012e6010): the Playlist track's Rename / color gives its mixer track the same
// name and colour
TEST (issues190ns_recoloured_audio_track_recolours_its_mixer_track)
{
    using namespace lynx;
    int track = -1, mixer = -1;
    auto song = issues190ns::songWithAudioTrack (track, mixer);
    CHECK (mixer > 0);
    // the link itself: one name and colour
    CHECK (song.mixer[(std::size_t) mixer].colour == 0xff8040c0 && song.mixer[(std::size_t) mixer].name == "Vocals");

    auto before = trackmodes::linkedNames (song);
    song.playlistTracks[(std::size_t) track].colour = 0xff20a050;
    song.playlistTracks[(std::size_t) track].name = "Lead vox";
    CHECK (trackmodes::followLinkedNames (song, before) == 1);
    CHECK (song.mixer[(std::size_t) mixer].colour == 0xff20a050);
    CHECK (song.mixer[(std::size_t) mixer].name == "Lead vox");

    // a track back to the default colour and no name: the mixer track's default colour, "Insert N"
    before = trackmodes::linkedNames (song);
    song.playlistTracks[(std::size_t) track].colour = 0;
    song.playlistTracks[(std::size_t) track].name.clear();
    trackmodes::followLinkedNames (song, before);
    CHECK (song.mixer[(std::size_t) mixer].colour == 0u);
    CHECK (song.mixer[(std::size_t) mixer].name == "Insert " + std::to_string (mixer));
}

// #235 (FUN_012be6f0 → FUN_012a7810): the mixer track's Rename / color reaches the Playlist track linked to it
TEST (issues190ns_recoloured_mixer_track_recolours_its_audio_track)
{
    using namespace lynx;
    int track = -1, mixer = -1;
    auto song = issues190ns::songWithAudioTrack (track, mixer);
    const auto before = trackmodes::linkedNames (song);
    song.mixer[(std::size_t) mixer].colour = 0xffc03030;
    song.mixer[(std::size_t) mixer].name = "Bus";
    CHECK (trackmodes::followLinkedNames (song, before) == 1);
    CHECK (song.playlistTracks[(std::size_t) track].colour == 0xffc03030);
    CHECK (song.playlistTracks[(std::size_t) track].name == "Bus");

    // an edit that sets both sides (a new link) and other inserts / unassigned tracks are left alone
    auto again = trackmodes::linkedNames (song);
    song.mixer[(std::size_t) mixer].colour = 0xff111111;
    song.playlistTracks[(std::size_t) track].colour = 0xff222222;
    const auto otherColour = song.mixer[1].colour;
    song.playlistTracks[0].colour = 0xff333333; // unassigned
    CHECK (trackmodes::followLinkedNames (song, again) == 0);
    CHECK (song.mixer[(std::size_t) mixer].colour == 0xff111111 && song.playlistTracks[(std::size_t) track].colour == 0xff222222);
    CHECK (song.mixer[1].colour == otherColour);

    // instrument tracks are not FL-synced (FUN_012e6010 checks for an audio track)
    auto inst = Song::makeDefault();
    inst.playlistTracks.resize (2);
    CHECK (trackmodes::makeInstrumentTrack (inst, 1, 0));
    CHECK (trackmodes::linkedNames (inst).pairs.empty());
}

// #235: LYNX's "Link Playlist tracks to mixer tracks": after switching it on every linked pair carries one colour
TEST (issues190ns_switching_the_link_on_leaves_one_colour_per_pair)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    for (int c = 0; c < (int) song.channels.size(); ++c)
        song.channels[(std::size_t) c].colour = 0xff000000u | (std::uint32_t) (0x203040 * (c + 1));
    for (int c = 0; c < 3 && c < (int) song.patterns.size(); ++c)
    {
        Clip p;
        p.track = c;
        p.pattern = c;
        p.length = 384;
        song.playlist.push_back (p);
    }
    for (int m = 1; m < (int) song.mixer.size(); ++m) // inserts with colours of their own, tracks with the default
        song.mixer[(std::size_t) m].colour = 0xff000000u | (std::uint32_t) (0x102030 * m);
    autolink::setOn (song, true);
    int pairs = 0;
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (const auto m = autolink::linkedInsertOf (song, t); m > 0 && m < (int) song.mixer.size())
        {
            std::printf ("  track %d -> insert %d: %08x / %08x\n", t, m, song.playlistTracks[(std::size_t) t].colour, song.mixer[(std::size_t) m].colour);
            CHECK (song.playlistTracks[(std::size_t) t].colour == song.mixer[(std::size_t) m].colour);
            CHECK (song.mixer[(std::size_t) m].colour != 0u);
            ++pairs;
        }
    CHECK (pairs > 0);
}

// #235: a project saved with the switch on whose linked track has the default colour and its insert a colour (the
// reporter's purple insert under a grey header) shows one colour when opened; switch off or two own colours: untouched
TEST (issues190ns_open_matches_linked_colours_while_the_switch_is_on)
{
    using namespace lynx;
    int track = -1, mixer = -1;
    auto song = issues190ns::songWithAudioTrack (track, mixer);
    song.playlistTracks[(std::size_t) track].colour = 0;
    song.mixer[(std::size_t) mixer].colour = 0xff7a4bd1;
    auto off = song;
    CHECK (autolink::matchLinkedColours (off) == 0 && off.playlistTracks[(std::size_t) track].colour == 0);
    song.linkTracksToMixer = true;
    CHECK (autolink::matchLinkedColours (song) == 1);
    CHECK (song.playlistTracks[(std::size_t) track].colour == 0xff7a4bd1);
    // the other way: the track's colour reaches an insert with the default one
    song.mixer[(std::size_t) mixer].colour = 0;
    song.playlistTracks[(std::size_t) track].colour = 0xff3fa66b;
    CHECK (autolink::matchLinkedColours (song) == 1 && song.mixer[(std::size_t) mixer].colour == 0xff3fa66b);
    // two colours of their own: left alone
    song.mixer[(std::size_t) mixer].colour = 0xff112233;
    CHECK (autolink::matchLinkedColours (song) == 0 && song.playlistTracks[(std::size_t) track].colour == 0xff3fa66b);
}
