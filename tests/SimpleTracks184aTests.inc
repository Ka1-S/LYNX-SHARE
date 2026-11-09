// --- Round 184a, slice S2: adding a track in the Simple workflow (core/SimpleTracks.h). One track = one insert (and one
// channel for an instrument), built from FL's own track modes, named and coloured together, one edit or none at all.

#include "core/SimpleTracks.h"

namespace simple184a
{
using namespace lynx;

// A project as a new one starts: Master and a few free inserts (more are added when they run out)
inline Song freshSong (int inserts = 4)
{
    Song s;
    s.mixer.resize ((std::size_t) inserts + 1);
    s.mixer[0].name = "Master";
    for (int i = 1; i <= inserts; ++i)
        s.mixer[(std::size_t) i].name = "Insert " + std::to_string (i);
    return s;
}

inline ChannelDef synthChannel (const char* name = "Synth")
{
    ChannelDef c;
    c.name = name;
    c.instrument = InstrumentKind::synth;
    return c;
}

inline ChannelDef samplerChannel (const char* path = "C:/samples/vox.wav")
{
    ChannelDef c;
    c.name = "vox";
    c.instrument = InstrumentKind::sampler;
    c.samplePath = path;
    return c;
}

inline Note stepNote (int row, std::int64_t start)
{
    Note n;
    n.channel = row;
    n.key = 60;
    n.start = start;
    n.length = ppq / 4;
    return n;
}
} // namespace simple184a

TEST (simple184a_voice_track_is_an_audio_track_with_its_own_insert)
{
    using namespace simple184a;
    auto s = freshSong();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::voice;
    q.input = 1;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok());
    CHECK (r.track == 0);
    CHECK (r.insert == 1);
    CHECK (r.channels.empty());
    const auto& t = s.playlistTracks[0];
    CHECK (t.mode == PlaylistTrackMode::audio);
    CHECK (t.mixerTrack == 1);
    CHECK (t.name == "Voice");
    CHECK (s.mixer[1].name == "Voice");
    CHECK (t.colour == simpletracks::defaultColour (simpletracks::Kind::voice) && s.mixer[1].colour == t.colour);
    CHECK (s.mixer[1].inputChannel == 1 && ! s.mixer[1].inputStereo); // a microphone: mono
    CHECK (simpletracks::kindOf (s, 0) == simpletracks::Kind::audio);
    CHECK (simpletracks::insertOf (s, 0) == 1);
    CHECK (s.channels.empty()); // a recording makes its audio clip channel later, routed to the track's insert
}

TEST (simple184a_audio_import_track_has_no_input)
{
    using namespace simple184a;
    auto s = freshSong();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::audio;
    q.name = "Loop 120";
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok());
    CHECK (s.playlistTracks[(std::size_t) r.track].name == "Loop 120");
    CHECK (s.mixer[(std::size_t) r.insert].inputChannel == -1);
    CHECK (simpletracks::kindOf (s, r.track) == simpletracks::Kind::audio);
}

TEST (simple184a_instrument_track_owns_one_channel_and_one_insert)
{
    using namespace simple184a;
    auto s = freshSong();
    s.channels.push_back (synthChannel ("Old")); // a channel already there (on the Master)
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    q.channel = synthChannel ("Deep Sub");
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok());
    CHECK (r.channels.size() == 1 && r.channels[0] == 1);
    CHECK (s.channels.size() == 2);
    const auto& c = s.channels[1];
    CHECK (c.name == "Instrument"); // the track's name, so track, channel and insert read the same
    CHECK (c.mixerTrack == r.insert && r.insert == 1);
    CHECK (c.colour == simpletracks::defaultColour (simpletracks::Kind::instrument));
    const auto& t = s.playlistTracks[(std::size_t) r.track];
    CHECK (t.mode == PlaylistTrackMode::instrument && t.channel == 1 && t.mixerTrack == 1);
    CHECK (s.mixer[1].name == "Instrument");
    CHECK (simpletracks::kindOf (s, r.track) == simpletracks::Kind::instrument);
    CHECK (simpletracks::insertOf (s, r.track) == 1);
    CHECK (s.channels[0].mixerTrack == 0); // the other channel is untouched
}

TEST (simple184a_sampler_track_needs_a_sampler_channel)
{
    using namespace simple184a;
    auto s = freshSong();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::sampler;
    q.channel = samplerChannel();
    auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok());
    CHECK (simpletracks::kindOf (s, r.track) == simpletracks::Kind::sampler);
    CHECK (s.channels[(std::size_t) r.channels[0]].samplePath == "C:/samples/vox.wav");
    CHECK (s.playlistTracks[(std::size_t) r.track].name == "Sampler");

    const auto before = s;
    q.channel = synthChannel();
    r = simpletracks::addTrack (s, q);
    CHECK (r.error == simpletracks::Error::noChannel);
    CHECK (s.channels.size() == before.channels.size() && s.playlistTracks == before.playlistTracks);
}

TEST (simple184a_instrument_errors_leave_the_song_alone)
{
    using namespace simple184a;
    auto s = freshSong();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    auto r = simpletracks::addTrack (s, q);
    CHECK (r.error == simpletracks::Error::noChannel);
    auto clip = synthChannel();
    clip.instrument = InstrumentKind::audioClip;
    q.channel = clip;
    r = simpletracks::addTrack (s, q);
    CHECK (r.error == simpletracks::Error::notAnInstrument);
    CHECK (s.channels.empty() && s.playlistTracks.empty() && s.mixer.size() == 5);
}

TEST (simple184a_mixer_full_is_refused_without_a_trace)
{
    using namespace simple184a;
    // every insert in use and the mixer at FL's maximum: nothing can be linked
    auto s = freshSong (maxMixerTracks - 1);
    for (int i = 1; i < maxMixerTracks; ++i)
    {
        auto c = synthChannel();
        c.mixerTrack = i;
        s.channels.push_back (c);
    }
    const auto before = s;
    simpletracks::Request q;
    q.kind = simpletracks::Kind::voice;
    auto r = simpletracks::addTrack (s, q);
    CHECK (r.error == simpletracks::Error::mixerFull);
    q.kind = simpletracks::Kind::instrument;
    q.channel = synthChannel();
    r = simpletracks::addTrack (s, q);
    CHECK (r.error == simpletracks::Error::mixerFull);
    q.kind = simpletracks::Kind::drums;
    q.kit = { synthChannel ("Kick") };
    r = simpletracks::addTrack (s, q);
    CHECK (r.error == simpletracks::Error::mixerFull);
    CHECK (s.channels.size() == before.channels.size() && s.playlistTracks == before.playlistTracks
           && s.mixer.size() == before.mixer.size() && s.channelGroups == before.channelGroups && s.patterns.size() == before.patterns.size());
}

TEST (simple184a_a_new_insert_is_added_when_none_is_free)
{
    using namespace simple184a;
    auto s = freshSong (1);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::guitar;
    CHECK (simpletracks::addTrack (s, q).insert == 1);
    q.kind = simpletracks::Kind::bass;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok() && r.insert == 2 && s.mixer.size() == 3);
    CHECK (s.mixer[2].name == "Bass");
    CHECK (r.track == 1);
}

TEST (simple184a_drums_are_a_kit_group_on_one_insert)
{
    using namespace simple184a;
    auto s = freshSong();
    s.channels.push_back (synthChannel ("Lead")); // channel 0, on the Master
    simpletracks::Request q;
    q.kind = simpletracks::Kind::drums;
    for (const char* name : { "Kick", "Snare", "Hat" })
    {
        auto c = synthChannel (name);
        c.instrument = InstrumentKind::sampler;
        q.kit.push_back (c);
    }
    Pattern beat;
    beat.notes = { stepNote (0, 0), stepNote (2, ppq / 2), stepNote (1, ppq), stepNote (7, ppq), stepNote (0, 2 * ppq) }; // row 7: none
    q.pattern = beat;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok());
    CHECK (r.channels == (std::vector<int> { 1, 2, 3 }));
    CHECK (r.insert == 1);
    for (auto ch : r.channels)
    {
        CHECK (s.channels[(std::size_t) ch].mixerTrack == 1);       // ONE insert for the whole kit
        CHECK (s.channels[(std::size_t) ch].group == r.group);
    }
    CHECK (s.channels[1].name == "Kick" && s.channels[3].name == "Hat"); // rows keep their sounds' names
    CHECK (r.group >= 0 && s.channelGroups[(std::size_t) r.group] == "Drums");
    const auto& p = s.patterns[(std::size_t) r.pattern];
    CHECK (p.name == "Drums");
    CHECK (p.notes.size() == 4); // the note on a missing row is dropped
    CHECK (p.notes[0].channel == 1 && p.notes[1].channel == 3 && p.notes[2].channel == 2 && p.notes[3].channel == 1);
    // the pattern is placed at the song's start on the track
    CHECK (s.playlist.size() == 1);
    CHECK (s.playlist[0].track == r.track && s.playlist[0].start == 0 && s.playlist[0].pattern == r.pattern);
    CHECK (s.playlist[0].length == 4 * ppq);
    const auto& t = s.playlistTracks[(std::size_t) r.track];
    CHECK (t.mode == PlaylistTrackMode::audio && t.mixerTrack == 1);
    CHECK (s.mixer[1].name == "Drums");
    CHECK (simpletracks::kindOf (s, r.track) == simpletracks::Kind::drums);
    CHECK (simpletracks::insertOf (s, r.track) == 1);
    CHECK (s.channels[0].mixerTrack == 0);

    // an empty starter: one empty bar, and the track still knows its insert
    simpletracks::Request empty;
    empty.kind = simpletracks::Kind::drums;
    empty.kit = { synthChannel ("Kick") };
    const auto e = simpletracks::addTrack (s, empty);
    CHECK (e.ok() && e.track == 1 && e.insert == 2);
    CHECK (s.patterns[(std::size_t) e.pattern].notes.empty());
    CHECK (s.playlist.back().length == 4 * ppq);
    CHECK (s.playlistTracks[1].name == "Drums 2" && s.channelGroups[(std::size_t) e.group] == "Drums 2");
    CHECK (simpletracks::kindOf (s, 1) == simpletracks::Kind::drums);
}

TEST (simple184a_next_empty_track_skips_used_ones)
{
    using namespace simple184a;
    auto s = freshSong();
    CHECK (simpletracks::nextEmptyTrack (s) == 0);
    Clip c;
    c.track = 0;
    c.length = ppq;
    s.playlist.push_back (c);
    CHECK (simpletracks::nextEmptyTrack (s) == 1);
    s.playlistTracks.resize (3);
    s.playlistTracks[1].name = "Named";
    CHECK (simpletracks::nextEmptyTrack (s) == 2);
    // a clip in another arrangement counts too
    s.arrangements.push_back (Arrangement { "B", {}, 0, 0, {} });
    s.arrangements[1].clips.push_back (Clip { 0, 2, 0, ppq });
    CHECK (simpletracks::nextEmptyTrack (s) == 3);
    // an FL project's track with a mode but no clips is taken
    s.playlistTracks.resize (4);
    s.playlistTracks[3].mode = PlaylistTrackMode::audio;
    s.playlistTracks[3].mixerTrack = 1;
    CHECK (simpletracks::nextEmptyTrack (s) == 4);
}

TEST (simple184a_rename_changes_track_insert_and_channel_together)
{
    using namespace simple184a;
    auto s = freshSong();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    q.channel = synthChannel();
    const auto keys = simpletracks::addTrack (s, q);
    q.kind = simpletracks::Kind::drums;
    q.kit = { synthChannel ("Kick"), synthChannel ("Snare") };
    const auto drums = simpletracks::addTrack (s, q);
    q.kind = simpletracks::Kind::voice;
    const auto voice = simpletracks::addTrack (s, q);

    CHECK (simpletracks::rename (s, keys.track, "Bass") == "Bass");
    CHECK (s.playlistTracks[(std::size_t) keys.track].name == "Bass");
    CHECK (s.channels[(std::size_t) keys.channels[0]].name == "Bass");
    CHECK (s.mixer[(std::size_t) keys.insert].name == "Bass");

    CHECK (simpletracks::rename (s, drums.track, "Beat") == "Beat");
    CHECK (s.mixer[(std::size_t) drums.insert].name == "Beat");
    CHECK (s.channelGroups[(std::size_t) drums.group] == "Beat");
    CHECK (s.patterns[(std::size_t) drums.pattern].name == "Beat");
    CHECK (s.channels[(std::size_t) drums.channels[0]].name == "Kick"); // the kit's rows keep their names

    // a name another track has is numbered
    CHECK (simpletracks::rename (s, voice.track, "Beat") == "Beat 2");
    CHECK (s.mixer[(std::size_t) voice.insert].name == "Beat 2");
    // empty: ignored
    CHECK (simpletracks::rename (s, voice.track, "").empty());
    CHECK (s.playlistTracks[(std::size_t) voice.track].name == "Beat 2");
}

TEST (simple184a_kind_of_other_tracks)
{
    using namespace simple184a;
    auto s = freshSong();
    CHECK (! simpletracks::kindOf (s, 0)); // no such track yet
    CHECK (simpletracks::insertOf (s, 0) == -1);
    s.playlistTracks.resize (2);
    CHECK (! simpletracks::kindOf (s, 0)); // an FL track without a mode
    s.playlistTracks[1].mode = PlaylistTrackMode::audio;
    s.playlistTracks[1].mixerTrack = 9; // gone
    CHECK (! simpletracks::kindOf (s, 1));
    CHECK (simpletracks::insertOf (s, 1) == -1);
    // an audio track whose insert only holds audio clip channels is audio, not drums
    s.playlistTracks[1].mixerTrack = 2;
    auto clipChannel = synthChannel ("take");
    clipChannel.instrument = InstrumentKind::audioClip;
    clipChannel.mixerTrack = 2;
    s.channels.push_back (clipChannel);
    CHECK (simpletracks::kindOf (s, 1) == simpletracks::Kind::audio);
}

TEST (simple184a_kinds_names_and_colours)
{
    using namespace lynx::simpletracks;
    const Kind all[] { Kind::voice, Kind::instrument, Kind::drums, Kind::sampler, Kind::guitar, Kind::bass, Kind::audio };
    for (auto a : all)
    {
        CHECK (std::string (defaultName (a)).size() > 0);
        CHECK ((defaultColour (a) >> 24) == 0xFF);
        for (auto b : all)
            if (a != b)
                CHECK (defaultColour (a) != defaultColour (b) && std::string (defaultName (a)) != defaultName (b));
    }
    CHECK (isAudioKind (Kind::voice) && isAudioKind (Kind::guitar) && isAudioKind (Kind::bass) && isAudioKind (Kind::audio));
    CHECK (! isAudioKind (Kind::instrument) && ! isAudioKind (Kind::drums) && ! isAudioKind (Kind::sampler));
}
