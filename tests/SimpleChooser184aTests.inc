// --- Round 184a, slice S3: the New track chooser's core parts — when a project counts as empty (it then opens on the
// chooser) and a starter beat (a Loop Starter plan) turned into a Drums track.

#include <cmath>

#include "core/ClipOps.h"
#include "core/LoopStarter.h"
#include "core/Meters.h"
#include "core/RackRenderer.h"
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

// Round 190t, slice A: a starter beat is placed for four bars (whole clips of its pattern), in the same edit.
TEST (simple190t_touch_keyboard_note_sounds_on_a_new_instrument_track)
{
    using namespace lynx;
    // The Instrument tile's track, then a key of the Touch keyboard (ui/TouchKeyboard.cpp: AudioEngine::sendLiveNote
    // pushes this NoteEvent on the selected channel into the live queue the renderer reads): the note is heard at the
    // Master and on the track's own insert, with the transport stopped, and ends after the key is let go.
    auto s = Song::makeDefault();
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();
    s.mixer = Song::defaultMixer (4);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    q.name = "Instrument";
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth; // SimpleWorkflow::addInstrument's default
    q.channel = synth;
    const auto added = simpletracks::addTrack (s, q);
    CHECK (added.ok() && added.channels.size() == 1 && added.insert > 0);
    if (! added.ok() || added.channels.empty())
        return;
    const auto state = RackState::build (s, nullptr, 48000.0);
    RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    LiveEventQueue live;
    MeterBank meters;
    std::vector<float> l (512), r (512);
    auto render = [&] (int blocks) {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            renderer.process (state.get(), false, 0.0, 512, l.data(), r.data(), &live, &meters);
            for (int i = 0; i < 512; ++i)
                peak = std::max ({ peak, std::fabs (l[(std::size_t) i]), std::fabs (r[(std::size_t) i]) });
        }
        return peak;
    };
    CHECK (render (8) == 0.0f); // nothing held: silence
    (void) meters.takePeak (added.insert, 0);

    NoteEvent on;
    on.type = NoteEvent::Type::on;
    on.channel = added.channels.front(); // the channel the new track selects
    on.key = 60;
    on.velocity = 0.8f;
    CHECK (live.push (on));
    const auto held = render (20); // ~0.2 s
    CHECK (held > 0.01f);
    CHECK (meters.takePeak (added.insert, 0) > 0.001f); // through the track's own mixer insert

    auto off = on;
    off.type = NoteEvent::Type::off;
    CHECK (live.push (off));
    render (600); // ~6.4 s: the release is over
    CHECK (render (8) < 1e-4f);
}

TEST (simple190t_starter_tempo_is_the_one_on_the_button)
{
    using namespace lynx;
    // every starter: with "Use the suggested tempo" the project gets the BPM written on the button (Lo-fi 84 -> 84),
    // whatever tempo the generated plan drew from the genre's range
    const auto config = loopstarter::builtinConfig();
    loopstarter::Sources sources;
    sources.config = &config;
    const auto genres = config.listed();
    CHECK (genres.size() >= 3);
    int drewAnother = 0;
    for (const auto* genre : genres)
    {
        const auto onButton = (int) std::lround (genre->defaultTempo);
        CHECK (loopstarter::suggestedTempo (*genre) == onButton);
        CHECK (loopstarter::suggestedTempo (sources, genre->id) == onButton);
        for (std::uint64_t seed = 1; seed <= 12; ++seed)
        {
            const auto plan = loopstarter::generate (sources, genre->id, seed);
            drewAnother += std::fabs (plan.bpm - (double) onButton) > 0.5 ? 1 : 0;
            simpletracks::Request q; // what the starter page asks for
            q.kind = simpletracks::Kind::drums;
            q.kit = loopstarter::kitChannels (plan);
            q.pattern = loopstarter::kitPattern (plan);
            q.fillBars = 4;
            q.bpm = loopstarter::suggestedTempo (sources, genre->id);
            if (q.kit.empty())
                continue;
            Song s;
            s.mixer.resize (4);
            s.bpm = 133.0;
            CHECK (simpletracks::addTrack (s, q).ok());
            CHECK (s.bpm == (double) onButton);
        }
    }
    CHECK (drewAnother > 0); // the plan's own tempo does differ: the old code took that one
    loopstarter::Sources none; // no configuration, unknown genre: LYNX's own default genre
    CHECK (loopstarter::suggestedTempo (none, "no-such-genre") == loopstarter::suggestedTempo (*config.find (config.defaultGenre)));
}

TEST (simple190t_a_starter_beat_fills_four_bars)
{
    using namespace lynx;
    const auto bar = (std::int64_t) 16 * ticksPerStep;
    auto add = [bar] (Song& s, int patternBars, int fillBars) {
        s.mixer.resize (4);
        simpletracks::Request q;
        q.kind = simpletracks::Kind::drums;
        ChannelDef kick;
        kick.instrument = InstrumentKind::kick;
        q.kit = { kick };
        Pattern beat;
        beat.lengthTicks = patternBars * bar;
        Note n;
        n.length = ticksPerStep;
        n.key = 60;
        beat.notes = { n };
        q.pattern = beat;
        q.fillBars = fillBars;
        return simpletracks::addTrack (s, q);
    };
    {
        Song s; // one bar: four clips, one after the other, all the same pattern on the drum track
        const auto r = add (s, 1, 4);
        CHECK (r.ok() && s.playlist.size() == 4);
        for (std::size_t i = 0; i < s.playlist.size(); ++i)
            CHECK (s.playlist[i].start == (std::int64_t) i * bar && s.playlist[i].length == bar && s.playlist[i].pattern == r.pattern
                   && s.playlist[i].track == r.track && s.playlist[i].kind == ClipKind::pattern);
        CHECK (r.pattern == (int) s.patterns.size() - 1); // one pattern added, not four
    }
    {
        Song s; // two bars: twice
        CHECK (add (s, 2, 4).ok() && s.playlist.size() == 2 && s.playlist[1].start == 2 * bar);
    }
    {
        Song s; // three bars: whole clips only, so six bars
        CHECK (add (s, 3, 4).ok() && s.playlist.size() == 2 && s.playlist[1].start == 3 * bar);
    }
    {
        Song s; // four bars or more: once
        CHECK (add (s, 4, 4).ok() && s.playlist.size() == 1);
        Song t;
        CHECK (add (t, 8, 4).ok() && t.playlist.size() == 1);
    }
    {
        Song s; // 0: as before
        CHECK (add (s, 1, 0).ok() && s.playlist.size() == 1);
    }
    {
        Song s; // a waltz: four bars of three beats
        s.beatsPerBar = 3;
        const auto r = add (s, 1, 4);
        const auto length = s.playlist[0].length;
        CHECK (r.ok() && length > 0 && (std::int64_t) s.playlist.size() * length >= 4 * 3 * ppq
               && (std::int64_t) (s.playlist.size() - 1) * length < 4 * 3 * ppq);
    }
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

TEST (simple190p_a_loop_keeps_time)
{
    using namespace lynx;
    using simpletracks::loopBeats;
    // the tempo in the name
    bool said = false;
    CHECK (simpletracks::tempoInName ("DL1_102_Dm_Regulators_Stack_Verse.wav", &said) == 102.0 && ! said);
    CHECK (simpletracks::tempoInName ("Funky Guitar 98.wav") == 98.0);
    CHECK (simpletracks::tempoInName ("Break Scatty 174 bpm.wav", &said) == 174.0 && said);
    CHECK (simpletracks::tempoInName ("loop_128BPM") == 128.0);
    CHECK (simpletracks::tempoInName ("808 Kick 01.wav") == 0.0);   // out of range, too short
    CHECK (simpletracks::tempoInName ("Chord Dm7 4bars.wav") == 0.0); // numbers inside words
    CHECK (simpletracks::tempoInName ("Lead vocal.wav") == 0.0);
    CHECK (simpletracks::tempoInName ("120.wav") == 120.0);
    CHECK (simpletracks::tempoInName ("Take 1200.wav") == 0.0);
    // whole bars at the name's tempo: 4 bars at 102 BPM last 9.412 s
    CHECK (loopBeats ("DL1_102_Dm_Regulators_Stack_Verse.wav", 16.0 * 60.0 / 102.0, 0.0f, lynx::Meter (4, 4)) == 16.0f);
    CHECK (loopBeats ("Funky Guitar 98.wav", 8.0 * 60.0 / 98.0 + 0.05, 0.0f, lynx::Meter (4, 4)) == 8.0f); // a little padding
    CHECK (loopBeats ("Waltz 90.wav", 6.0 * 60.0 / 90.0, 0.0f, lynx::Meter (3, 4)) == 6.0f);
    CHECK (loopBeats ("Hat 120.wav", 0.5, 0.0f, lynx::Meter (4, 4)) == 1.0f);  // one beat
    CHECK (loopBeats ("Hat 120.wav", 1.0, 0.0f, lynx::Meter (4, 4)) == 2.0f);  // half a bar
    // a number that doesn't make whole bars is not a tempo: a take, a song
    CHECK (loopBeats ("Take 90.wav", 13.7, 0.0f, lynx::Meter (4, 4)) == 0.0f);
    CHECK (loopBeats ("Lead vocal.wav", 8.0, 0.0f, lynx::Meter (4, 4)) == 0.0f);
    CHECK (loopBeats ("My song 120.wav", 180.0, 0.0f, lynx::Meter (4, 4)) == 0.0f);
    // "bpm" in the name keeps a loop with a tail; the ACID beat count wins; long files are left alone
    CHECK (std::abs (loopBeats ("Pad 100 bpm.wav", 10.5, 0.0f, lynx::Meter (4, 4)) - 17.5f) < 1.0e-4f);
    CHECK (loopBeats ("Pad 100 bpm.wav", 10.5, 8.0f, lynx::Meter (4, 4)) == 8.0f);
    CHECK (loopBeats ("anything.wav", 4.0, 8.0f, lynx::Meter (4, 4)) == 8.0f);
    CHECK (loopBeats ("Song 100 bpm.wav", 200.0, 0.0f, lynx::Meter (4, 4)) == 0.0f);
    CHECK (loopBeats ("anything.wav", 0.0, 8.0f, lynx::Meter (4, 4)) == 0.0f);

    // the track: the channel is stretched to the beats with its pitch kept, and the clip is that long
    Song s;
    s.mixer.resize (3);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::audio;
    q.audioFile = "D:/loops/DL1_102_Dm_Regulators_Stack_Verse.wav";
    q.audioLength = 13 * ppq; // (its length at its own speed: not used for a loop)
    q.audioBeats = 16.0f;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok() && r.channels.size() == 1);
    const auto& c = s.channels[(std::size_t) r.channels[0]];
    CHECK (c.sampler.stretchBeats == 16.0f && c.sampler.keepPitch);
    CHECK (s.playlist.size() == 1 && s.playlist[0].length == 16 * ppq);
    // the same 16 beats at any tempo
    CHECK (clipops::fileTicks (c, 415059.0, 44100.0, 120.0) == 16 * ppq);
    CHECK (clipops::fileTicks (c, 415059.0, 44100.0, 85.0) == 16 * ppq);
    // without it nothing changes
    q.audioBeats = 0.0f;
    const auto plain = simpletracks::addTrack (s, q);
    CHECK (s.channels[(std::size_t) plain.channels[0]].sampler.stretchBeats == 0.0f);
    CHECK (! s.channels[(std::size_t) plain.channels[0]].sampler.keepPitch);
    CHECK (s.playlist.back().length == 13 * ppq);
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
