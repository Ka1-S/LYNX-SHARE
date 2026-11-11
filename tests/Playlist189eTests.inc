// --- Round 189e: FL's Playlist Edit / Select parity and "Drop audio on track headers" ------------------------------
// (FLEngine 2026 [DECOMP] FUN_00eb35f0, FUN_00e51730, FUN_00eb1da0, FUN_00eb1ae0, FUN_00e71980, FUN_00e71430; FL 2026
// shots in /mnt/e/lynx-work/flgap189e/shots/fl)

#include "core/ClipOps.h"
#include "core/KeyMap.h"
#include "core/NoteEdit.h"
#include "core/PlaylistOps.h"

namespace
{
lynx::Clip clip189e (int pattern, int track, std::int64_t start, std::int64_t length)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = pattern;
    c.track = track;
    c.start = start;
    c.length = length;
    return c;
}
} // namespace

TEST (playlist189e_slice_insert_space_cuts_the_crossing_clips)
{
    using namespace lynx;
    std::vector<Clip> clips { clip189e (0, 0, 0, 400), clip189e (0, 1, 100, 100), clip189e (0, 2, 500, 100) };
    std::vector<Marker> markers;
    Marker m;
    m.tick = 300;
    markers.push_back (m);
    noteedit::sliceInsertTime (clips, markers, 200, 1000);
    CHECK (clips.size() == 4);
    CHECK (clips[0].start == 0 && clips[0].length == 200);   // the head stays
    CHECK (clips[1].start == 100 && clips[1].length == 100); // ends at the cut: not moved
    CHECK (clips[2].start == 1500);                          // after the cut: moved
    CHECK (clips[3].start == 1200 && clips[3].length == 200 && clips[3].offset == 200 && clips[3].track == 0); // the tail moved
    CHECK (markers[0].tick == 1300);

    // plain Insert space keeps a crossing clip whole and in place
    std::vector<Clip> plain { clip189e (0, 0, 0, 400) };
    noteedit::insertTime (plain, markers, 200, 1000);
    CHECK (plain.size() == 1 && plain[0].start == 0 && plain[0].length == 400);
}

TEST (playlist189e_trim_keeps_only_the_time_selection_at_the_start)
{
    using namespace lynx;
    std::vector<Clip> clips { clip189e (0, 0, 0, 100), clip189e (1, 0, 150, 200), clip189e (2, 1, 400, 100) };
    std::vector<Marker> markers;
    noteedit::trimTime (clips, markers, 200, 300);
    CHECK (clips.size() == 1);
    CHECK (clips[0].pattern == 1 && clips[0].start == 0 && clips[0].length == 100 && clips[0].offset == 50);
}

TEST (playlist189e_shift_moves_by_snap_and_by_track_held_in_range)
{
    using namespace lynx;
    std::vector<Clip> clips { clip189e (0, 0, 96, 100), clip189e (0, 3, 400, 100), clip189e (0, 5, 0, 100) };
    CHECK (clipops::shiftClipsInTime (clips, { 0, 1 }, 96) == 96 && clips[0].start == 192 && clips[1].start == 496 && clips[2].start == 0);
    CHECK (clipops::shiftClipsInTime (clips, { 0, 1 }, -384) == -192 && clips[0].start == 0 && clips[1].start == 304); // (not before 0)
    CHECK (clipops::shiftClipsTrack (clips, { 0, 1 }, 1) == 1 && clips[0].track == 1 && clips[1].track == 4 && clips[2].track == 5);
    CHECK (clipops::shiftClipsTrack (clips, { 0, 1 }, -3) == -1 && clips[0].track == 0 && clips[1].track == 3); // (not above track 1)
    clips[2].track = 499;
    CHECK (clipops::shiftClipsTrack (clips, { 2 }, 1) == 0 && clips[2].track == 499);
}

TEST (playlist189e_select_by_source_and_invert)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.playlist = { clip189e (0, 0, 0, 96), clip189e (1, 1, 0, 96), clip189e (0, 2, 96, 96) };
    for (const auto* path : { "C:/s/kick.wav", "C:/s/kick.wav", "C:/s/snare.wav" })
    {
        ChannelDef c;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = path;
        song.channels.push_back (c);
    }
    const auto a = (int) song.channels.size() - 3;
    for (int k = 0; k < 3; ++k)
    {
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = a + k;
        c.track = 3;
        c.start = 200 * k;
        c.length = 96;
        song.playlist.push_back (c);
    }
    CHECK ((clipops::clipsOfSource (song, clip189e (0, 0, 0, 0)) == std::vector<int> { 0, 2 }));
    Clip source;
    source.kind = ClipKind::audio;
    source.channel = a;
    CHECK ((clipops::clipsOfSource (song, source) == std::vector<int> { 3, 4 })); // (another channel of the same file counts)
    source.channel = a + 2;
    CHECK ((clipops::clipsOfSource (song, source) == std::vector<int> { 5 }));
    CHECK (clipops::clipsOfSource (song, clip189e (7, 0, 0, 0)).empty());
    CHECK ((clipops::invertedSelection (6, { 1, 3, 9 }) == std::vector<int> { 0, 2, 4, 5 }));
    CHECK (clipops::invertedSelection (0, {}).empty());
}

TEST (playlist189e_drop_on_header_as_audio_clips_tracks_and_instrument_tracks)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    ChannelDef old;
    old.name = "Conga";
    old.instrument = InstrumentKind::audioClip;
    old.samplePath = "C:/s/Conga.wav";
    old.colour = 0xFFAA5500;
    song.channels.push_back (old);
    const auto oldChannel = (int) song.channels.size() - 1;
    const auto channels = song.channels.size();

    // audio clips: the channel of the same file is reused, the clip goes to the song start, the track is named after it
    CHECK (trackmodes::dropOnHeader (song, 4, "C:/s/Conga.wav", "Conga", 300, trackmodes::HeaderDrop::audioClips, false));
    CHECK (song.channels.size() == channels && song.playlist.back().channel == oldChannel && song.playlist.back().start == 0
           && song.playlist.back().track == 4 && song.playlist.back().length == 300);
    CHECK (song.playlistTracks[4].name == "Conga" && song.playlistTracks[4].colour == 0xFFAA5500
           && song.playlistTracks[4].mode == PlaylistTrackMode::unassigned);

    // audio track: a new channel ("Conga #2"), the track linked to a free mixer track, the clip plays through it
    CHECK (trackmodes::dropOnHeader (song, 5, "C:/s/Conga.wav", "Conga", 300, trackmodes::HeaderDrop::audioTracks, true));
    const auto audio = (int) song.channels.size() - 1;
    CHECK (song.channels[(std::size_t) audio].name == "Conga #2" && song.channels[(std::size_t) audio].instrument == InstrumentKind::audioClip);
    CHECK (song.playlistTracks[5].mode == PlaylistTrackMode::audio && song.playlistTracks[5].name == "Conga #2" && song.playlistTracks[5].lockedToContent);
    CHECK (song.channels[(std::size_t) audio].mixerTrack == song.playlistTracks[5].mixerTrack && song.playlistTracks[5].mixerTrack > 0);
    CHECK (song.playlist.back().channel == audio && song.playlist.back().track == 5 && song.playlist.back().start == 0);

    // instrument track: a Sampler of the file, its instrument track with its pattern at the start
    CHECK (trackmodes::dropOnHeader (song, 6, "C:/s/Conga.wav", "Conga", 300, trackmodes::HeaderDrop::instrumentTracks, false));
    const auto sampler = (int) song.channels.size() - 1;
    CHECK (song.channels[(std::size_t) sampler].instrument == InstrumentKind::sampler && song.channels[(std::size_t) sampler].samplePath == "C:/s/Conga.wav");
    CHECK (song.playlistTracks[6].mode == PlaylistTrackMode::instrument && song.playlistTracks[6].channel == sampler);
    CHECK (song.playlist.back().kind == ClipKind::pattern && song.playlist.back().track == 6 && song.playlist.back().start == 0);
    CHECK (! song.playlistTracks[6].lockedToContent);

    // only an unassigned track takes a header drop; "ask" is not a result
    const auto clips = song.playlist.size();
    CHECK (! trackmodes::dropOnHeader (song, 5, "C:/s/x.wav", "x", 300, trackmodes::HeaderDrop::audioClips, false));
    CHECK (! trackmodes::dropOnHeader (song, 7, "C:/s/x.wav", "x", 300, trackmodes::HeaderDrop::ask, false));
    CHECK (song.playlist.size() == clips);
}

TEST (playlist189e_drop_on_header_changes_nothing_when_no_mixer_track_is_free)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    for (int i = 0; i < 600 && trackmodes::freeMixerTrack (song) > 0; ++i)
    {
        ChannelDef c;
        c.mixerTrack = trackmodes::freeMixerTrack (song);
        song.channels.push_back (c);
    }
    CHECK (trackmodes::freeMixerTrack (song) <= 0);
    const auto channels = song.channels.size(), tracks = song.playlistTracks.size(), mixers = song.mixer.size(), clips = song.playlist.size();
    CHECK (! trackmodes::dropOnHeader (song, 2, "C:/s/x.wav", "x", 300, trackmodes::HeaderDrop::audioTracks, false));
    CHECK (song.channels.size() == channels && song.playlistTracks.size() == tracks && song.mixer.size() == mixers && song.playlist.size() == clips);
    CHECK (! trackmodes::dropOnHeader (song, 2, "C:/s/x.wav", "x", 300, trackmodes::HeaderDrop::instrumentTracks, false));
    CHECK (song.channels.size() == channels && song.playlistTracks.size() == tracks && song.playlist.size() == clips);
}

TEST (playlist189e_new_keys_are_fl_defaults_without_conflicts)
{
    using namespace lynx;
    keys::KeyMap map;
    CHECK (map.find (keys::Chord { keys::left, keys::shift }, keys::Context::playlist, false) == "playlist.shiftLeft");
    CHECK (map.find (keys::Chord { keys::down, keys::shift }, keys::Context::playlist, false) == "playlist.shiftDown");
    CHECK (map.find (keys::Chord { keys::insert, (std::uint8_t) (keys::ctrl | keys::alt) }, keys::Context::playlist, false) == "playlist.sliceInsertSpace");
    CHECK (map.find (keys::Chord { 'D', keys::ctrl }, keys::Context::playlist, false) == "playlist.deselect");
    CHECK (map.find (keys::Chord { 'C', keys::shift }, keys::Context::playlist, false) == "playlist.selectBySource");
    CHECK (map.find (keys::Chord { 'I', keys::shift }, keys::Context::playlist, false) == "playlist.invertSelection");
    CHECK (map.find (keys::Chord { 'C', keys::shift }, keys::Context::pianoRoll, false) == "pianoRoll.selectByColour");
}
