// --- Round 184a, slice S3: the New track chooser's core parts — when a project counts as empty (it then opens on the
// chooser) and a starter beat (a Loop Starter plan) turned into a Drums track.

#include "core/LoopStarter.h"
#include "core/SimpleTracks.h"

TEST (simple184a_empty_song_rule)
{
    using namespace lynx;
    Song s;
    CHECK (simpletracks::isEmptySong (s));
    // a template's channels without notes and its inserts don't count
    s.channels.resize (4);
    s.mixer.resize (5);
    s.patterns.resize (1);
    CHECK (simpletracks::isEmptySong (s));
    s.patterns[0].notes.push_back (Note {});
    CHECK (! simpletracks::isEmptySong (s));
    s.patterns[0].notes.clear();
    s.playlistTracks.resize (2);
    s.playlistTracks[1].name = "Vocals";
    CHECK (! simpletracks::isEmptySong (s));
    s.playlistTracks[1].name.clear();
    s.playlistTracks[1].mode = PlaylistTrackMode::audio;
    CHECK (! simpletracks::isEmptySong (s));
    s.playlistTracks[1].mode = PlaylistTrackMode::unassigned;
    CHECK (simpletracks::isEmptySong (s));
    // a clip anywhere: the Playlist, another arrangement or the clip grid
    s.arrangements.push_back (Arrangement { "B", {}, 0, 0, {} });
    s.arrangements[1].clips.push_back (Clip {});
    CHECK (! simpletracks::isEmptySong (s));
    s.arrangements.pop_back();
    CHECK (simpletracks::isEmptySong (s));
    s.playlist.push_back (Clip {});
    CHECK (! simpletracks::isEmptySong (s));
}

TEST (simple184a_starter_beat_becomes_a_drums_track)
{
    using namespace lynx;
    loopstarter::Plan plan;
    plan.genre = "test";
    plan.bpm = 92.0;
    plan.bars = 2;
    for (const char* category : { "kick", "snare", "hat" })
    {
        loopstarter::ChannelPlan c;
        c.category = category;
        plan.channels.push_back (c);
    }
    plan.channels[1].samplePath = "C:/drums/snare 01.wav";
    // steps: kick on 1 and 3 of bar 1, snare on 2, hat on every 8th of bar 2
    auto hit = [] (std::int64_t start) {
        Note n;
        n.start = start;
        n.length = ticksPerStep;
        n.key = 60;
        return n;
    };
    plan.channels[0].notes = { hit (0), hit (8 * ticksPerStep) };
    plan.channels[1].notes = { hit (4 * ticksPerStep) };
    for (int i = 0; i < 8; ++i)
        plan.channels[2].notes.push_back (hit (16 * ticksPerStep + 2 * i * ticksPerStep));

    const auto kit = loopstarter::kitChannels (plan);
    CHECK (kit.size() == 3);
    CHECK (kit[0].name == "Kick" && kit[1].name == "Snare" && kit[2].name == "Hat");
    CHECK (kit[0].instrument == loopstarter::builtinDrumFor ("kick"));
    CHECK (kit[1].instrument == InstrumentKind::sampler && kit[1].samplePath == "C:/drums/snare 01.wav");
    CHECK (kit[0].loopStarterPattern < 0 && kit[0].mixerTrack == 0); // plain channels, not Loop Starter's
    const auto beat = loopstarter::kitPattern (plan);
    CHECK (beat.lengthTicks == 2 * 16 * ticksPerStep);
    CHECK (beat.notes.size() == 11);
    CHECK (std::is_sorted (beat.notes.begin(), beat.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; }));
    CHECK (beat.notes[0].channel == 0 && beat.notes[1].channel == 1 && beat.notes[2].channel == 0 && beat.notes.back().channel == 2);

    Song s;
    s.mixer.resize (3);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::drums;
    q.kit = kit;
    q.pattern = beat;
    q.bpm = plan.bpm;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok());
    CHECK (s.bpm == 92.0); // the starter's tempo, in the same edit
    CHECK (s.mode == PlayMode::song);
    CHECK (r.channels.size() == 3);
    CHECK (s.patterns[(std::size_t) r.pattern].lengthTicks == 2 * 16 * ticksPerStep);
    CHECK (s.playlist.size() == 1 && s.playlist[0].length == 2 * 16 * ticksPerStep);
    CHECK (s.patterns[(std::size_t) r.pattern].notes.size() == 11);
    CHECK (simpletracks::kindOf (s, r.track) == simpletracks::Kind::drums);
    CHECK (! simpletracks::isEmptySong (s));
}

TEST (simple184a_imported_audio_is_a_track_with_its_clip)
{
    using namespace lynx;
    Song s;
    s.mixer.resize (3);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::audio;
    q.audioFile = "D:/loops/Funky Guitar 98.wav";
    q.audioLength = 8 * ppq;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok());
    CHECK (r.channels.size() == 1);
    const auto& c = s.channels[(std::size_t) r.channels[0]];
    CHECK (c.instrument == InstrumentKind::audioClip && c.samplePath == q.audioFile && c.name == "Funky Guitar 98");
    CHECK (c.mixerTrack == r.insert);
    CHECK (s.playlist.size() == 1 && s.playlist[0].kind == ClipKind::audio && s.playlist[0].channel == r.channels[0]);
    CHECK (s.playlist[0].track == r.track && s.playlist[0].start == 0 && s.playlist[0].length == 8 * ppq);
    CHECK (s.mode == PlayMode::song);
    // its insert holds only an audio clip channel: still an audio track, not drums
    CHECK (simpletracks::kindOf (s, r.track) == simpletracks::Kind::audio);
    CHECK (s.mixer[(std::size_t) r.insert].inputChannel == -1);
    // a name with no folder and no extension, and no file at all
    q.audioFile = "take";
    CHECK (s.channels[(std::size_t) simpletracks::addTrack (s, q).channels[0]].name == "take");
    q.audioFile.clear();
    CHECK (simpletracks::addTrack (s, q).channels.empty());
}

TEST (simple184a_imported_midi_track_is_an_instrument_with_its_notes)
{
    using namespace lynx;
    Song s;
    s.mixer.resize (3);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    q.name = "Piano";
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    q.channel = synth;
    Pattern notes;
    for (int i = 0; i < 3; ++i)
    {
        Note n;
        n.channel = 5; // (the file's group index: replaced by the track's channel)
        n.key = 60 + i;
        n.start = (2 - i) * ppq;
        n.length = ppq;
        notes.notes.push_back (n);
    }
    q.pattern = notes;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok() && r.pattern >= 0);
    const auto& p = s.patterns[(std::size_t) r.pattern];
    CHECK (p.name == "Piano" && p.notes.size() == 3);
    CHECK (std::all_of (p.notes.begin(), p.notes.end(), [&] (const Note& n) { return n.channel == r.channels[0]; }));
    CHECK (p.notes[0].start == 0 && p.notes[0].key == 62); // sorted
    CHECK (s.playlist.size() == 1 && s.playlist[0].pattern == r.pattern && s.playlist[0].track == r.track && s.playlist[0].length == 4 * ppq);
    // without notes nothing is placed
    q.pattern.reset();
    const auto plain = simpletracks::addTrack (s, q);
    CHECK (plain.ok() && plain.pattern == -1 && s.playlist.size() == 1);
}

TEST (simple184a_dropped_files_start_where_they_land_and_recolour)
{
    using namespace lynx;
    Song s;
    s.mixer.resize (4);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::audio;
    q.audioFile = "loop.wav";
    q.audioLength = 4 * ppq;
    q.start = 8 * ppq;
    const auto a = simpletracks::addTrack (s, q);
    CHECK (a.ok() && s.playlist.back().start == 8 * ppq);
    q.start = -5; // clamped
    simpletracks::addTrack (s, q);
    CHECK (s.playlist.back().start == 0);

    simpletracks::Request m;
    m.kind = simpletracks::Kind::instrument;
    ChannelDef synth;
    m.channel = synth;
    Pattern p;
    p.notes.push_back (Note {});
    m.pattern = p;
    m.start = 16 * ppq;
    const auto i = simpletracks::addTrack (s, m);
    CHECK (i.ok() && s.playlist.back().start == 16 * ppq && s.playlist.back().pattern == i.pattern);

    // recolour: the track, its insert and its channel together
    simpletracks::recolour (s, i.track, 0xFF112233u);
    CHECK (s.playlistTracks[(std::size_t) i.track].colour == 0xFF112233u);
    CHECK (s.mixer[(std::size_t) i.insert].colour == 0xFF112233u);
    CHECK (s.channels[(std::size_t) i.channels[0]].colour == 0xFF112233u);
    // the audio track's clip channel keeps its own colour
    const auto before = s.channels[(std::size_t) a.channels[0]].colour;
    simpletracks::recolour (s, a.track, 0xFF445566u);
    CHECK (s.mixer[(std::size_t) a.insert].colour == 0xFF445566u && s.channels[(std::size_t) a.channels[0]].colour == before);
    // a track that is not a Simple one: nothing
    const auto copy = s.playlistTracks;
    simpletracks::recolour (s, 40, 0xFF000000u);
    CHECK (s.playlistTracks == copy);
}
