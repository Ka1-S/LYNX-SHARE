// --- Round 190dv: the Simple workflow's first-song tour (core/FirstSongTour.h) and LYNX's demo songs (core/DemoSongs.h).
// The tour's steps are read from the song (model events: a beat, a key, an instrument, notes, an audio clip, a one-knob
// effect, a Master look), the state machine (advance on a step done, Next / Back / Skip, pause and resume, the saved
// state), "Do it for me"'s song edits, and the two new demo songs rendered: no clipping, loudness about −14 LUFS.
// LYNX_190DV_REPORT=1 prints the demos' measurements.

#include <cstdlib>

#include "core/DemoSongs.h"
#include "core/FirstSongTour.h"
#include "core/Loudness.h"
#include "core/RackRenderer.h"

namespace tour190dv
{
using namespace lynx;

inline Song empty()
{
    Song s;
    s.mixer = Song::defaultMixer();
    return s;
}

inline simpletracks::Result addDrums (Song& s, bool withNotes = true)
{
    simpletracks::Request q;
    q.kind = simpletracks::Kind::drums;
    ChannelDef kick;
    kick.instrument = InstrumentKind::kick;
    q.kit = { kick };
    Pattern p;
    p.lengthTicks = 16 * ticksPerStep;
    if (withNotes)
        p.toggleStep (0, 0);
    q.pattern = p;
    return simpletracks::addTrack (s, q);
}

inline simpletracks::Result addInstrument (Song& s)
{
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    ChannelDef c;
    c.instrument = InstrumentKind::synth;
    q.channel = c;
    q.emptyBars = 4;
    return simpletracks::addTrack (s, q);
}

inline simpletracks::Result addLoop (Song& s)
{
    simpletracks::Request q;
    q.kind = simpletracks::Kind::audio;
    q.name = "Loop";
    q.audioFile = "/nowhere/Loop 120 Am.wav";
    q.audioLength = ppq * 8;
    return simpletracks::addTrack (s, q);
}

struct Measured
{
    double lufs = 0.0, truePeak = 0.0, samplePeak = 0.0, seconds = 0.0;
    bool finite = true;
};

// the whole song through the real render path (the Playlist, the inserts' effects, the Master look), 48 kHz
inline Measured render (const Song& source)
{
    constexpr int rate = 48000, block = 256;
    std::int64_t ticks = 0;
    const auto song = makeExportSong (source, true, ppq * 4, ticks);
    const auto tps = ticksPerSample (song.bpm, rate);
    auto state = RackState::build (song, nullptr, rate);
    RackRenderer renderer;
    renderer.prepare (rate, block);
    loudness::Meter meter;
    meter.prepare (rate);
    Measured m;
    const auto total = (std::int64_t) ((double) ticks / tps);
    std::vector<float> l (block), r (block);
    double tick = 0.0;
    for (std::int64_t pos = 0; pos + block <= total; pos += block)
    {
        renderer.process (state.get(), true, tick, block, l.data(), r.data());
        tick += block * tps;
        for (int i = 0; i < block; ++i)
            m.finite = m.finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
        meter.process (l.data(), r.data(), block);
    }
    meter.flushTruePeak();
    m.lufs = meter.integratedLoudness();
    m.truePeak = meter.truePeakDb();
    m.samplePeak = meter.samplePeakDb();
    m.seconds = (double) total / rate;
    return m;
}
} // namespace tour190dv

TEST (tour190dv_steps_are_read_from_the_song)
{
    using namespace lynx;
    using namespace tour190dv;
    auto s = empty();
    for (int i = 0; i < tour::stepCount; ++i)
        CHECK (! tour::satisfied (s, i));
    CHECK (! tour::satisfied (s, tour::stepCount)); // (the end card is never "done")

    // a Drums track with an empty grid is not a beat yet; with a step it is
    auto d = addDrums (s, false);
    CHECK (d.ok());
    CHECK (! tour::satisfied (s, tour::beat));
    s.patterns[(std::size_t) d.pattern].toggleStep (0, 4);
    CHECK (tour::satisfied (s, tour::beat));

    projectkey::set (s, tour::defaultKey());
    CHECK (tour::satisfied (s, tour::key));
    CHECK (projectkey::name (*projectkey::get (s)) == "A Minor Natural (Aeolian)");

    CHECK (tour::firstInstrumentTrack (s) < 0);
    auto inst = addInstrument (s);
    CHECK (inst.ok());
    CHECK (tour::firstInstrumentTrack (s) == inst.track);
    CHECK (tour::satisfied (s, tour::instrument));
    CHECK (! tour::satisfied (s, tour::chords)); // (its 4-bar pattern is empty)

    // "Do it for me" on the chords: i – VI – III – VII of A minor (Am F C G), one triad per bar from C3 up
    CHECK (tour::writeChords (s, inst.track) == 12);
    CHECK (tour::satisfied (s, tour::chords));
    {
        const auto& p = s.patterns[(std::size_t) simpletracks::patternOf (s, inst.track)];
        std::vector<int> bar1, bar2;
        for (const auto& n : p.notes)
        {
            CHECK (n.channel == s.playlistTracks[(std::size_t) inst.track].channel);
            if (n.start == 0) bar1.push_back (n.key);
            if (n.start == 4 * ppq) bar2.push_back (n.key);
        }
        std::sort (bar1.begin(), bar1.end());
        std::sort (bar2.begin(), bar2.end());
        CHECK ((bar1 == std::vector<int> { 57, 60, 64 }));  // A C E
        CHECK ((bar2 == std::vector<int> { 65, 69, 72 }));  // F A C
    }
    CHECK (tour::writeChords (s, d.track) == 0); // (not an Instrument track)

    CHECK (! tour::satisfied (s, tour::loop));
    auto loop = addLoop (s);
    CHECK (loop.ok());
    CHECK (tour::satisfied (s, tour::loop));

    // an ordinary effect is not a one-knob effect; Room is; a Room edited by hand in its full editor is not any more
    CHECK (! tour::satisfied (s, tour::effect));
    s.mixer[(std::size_t) inst.insert].effects.push_back (makeEffectSlot (EffectKind::reverb));
    CHECK (! tour::satisfied (s, tour::effect));
    CHECK (tour::addOneKnob (s, inst.track));
    CHECK (tour::satisfied (s, tour::effect));

    CHECK (! tour::satisfied (s, tour::finish));
    MasterLookSettings look;
    look.id = looks::balanced;
    CHECK (looks::apply (s, look));
    CHECK (tour::satisfied (s, tour::finish));

    // every step names controls for its ring; only the loop step can't be done for the user
    for (int i = 0; i < tour::stepCount; ++i)
    {
        CHECK (! tour::targets (i).empty());
        CHECK (tour::canDoForUser (i) == (i != tour::loop));
        CHECK (std::string (tour::title (i)).size() > 4 && std::string (tour::text (i)).size() > 20);
    }
}

TEST (tour190dv_the_tour_moves_on_when_the_song_shows_a_step_done)
{
    using namespace lynx;
    using namespace tour190dv;
    auto s = empty();
    tour::Machine m;
    CHECK (! m.running() && ! m.canContinue());
    m.start (s);
    CHECK (m.running() && m.canContinue() && m.step() == tour::beat);
    CHECK (! m.observe (s)); // (nothing changed)
    CHECK (m.step() == tour::beat);

    // the beat: on to the key
    addDrums (s);
    CHECK (m.observe (s));
    CHECK (m.step() == tour::key);
    CHECK ((m.state().completed & tour::Machine::bit (tour::beat)) != 0);

    // a key chosen AND an instrument added in one edit: both steps pass at once, the card shows the chords
    projectkey::set (s, tour::defaultKey());
    auto inst = addInstrument (s);
    CHECK (m.observe (s));
    CHECK (m.step() == tour::chords);

    // Next without doing it: passed, not completed
    m.next (s);
    CHECK (m.step() == tour::loop);
    CHECK ((m.state().passed & tour::Machine::bit (tour::chords)) != 0);
    CHECK ((m.state().completed & tour::Machine::bit (tour::chords)) == 0);

    // Back on a step done: it stays on the card with its tick until Next (no jump forward by itself)
    m.back();
    m.back();
    CHECK (m.step() == tour::instrument);
    CHECK (m.shownDone (s));
    CHECK (! m.observe (s));
    CHECK (m.step() == tour::instrument);
    m.next (s); // (done: not "passed")
    CHECK ((m.state().passed & tour::Machine::bit (tour::instrument)) == 0);
    CHECK (m.step() == tour::chords);
    CHECK (! m.shownDone (s));

    // the chords written: on to the loop
    tour::writeChords (s, inst.track);
    CHECK (m.observe (s));
    CHECK (m.step() == tour::loop);
    addLoop (s);
    CHECK (m.observe (s) && m.step() == tour::effect);
    tour::addOneKnob (s, inst.track);
    CHECK (m.observe (s) && m.step() == tour::finish);
    MasterLookSettings look;
    look.id = looks::warmTape;
    looks::apply (s, look);
    CHECK (m.observe (s));
    CHECK (m.atEnd() && m.step() == tour::stepCount);
    CHECK (! m.observe (s)); // (the end card stays)
    m.next (s);
    CHECK (m.step() == tour::stepCount);
    m.finishTour();
    CHECK (! m.running() && ! m.canContinue() && m.state().done);
    CHECK (m.state().completed == 0x7fu); // (all seven: the chords too, once written)

    // a step undone after Back is advanced again when it is done once more
    tour::Machine m2;
    auto s2 = empty();
    m2.start (s2);
    auto d = addDrums (s2);
    CHECK (m2.observe (s2) && m2.step() == tour::key);
    m2.back();
    CHECK (m2.step() == tour::beat && ! m2.observe (s2));
    s2.patterns[(std::size_t) d.pattern].notes.clear(); // (the steps cleared)
    CHECK (! m2.observe (s2));
    s2.patterns[(std::size_t) d.pattern].toggleStep (0, 8);
    CHECK (m2.observe (s2) && m2.step() == tour::key);
}

TEST (tour190dv_skip_pause_resume_and_the_saved_state)
{
    using namespace lynx;
    using namespace tour190dv;
    // the saved state reads back as written, and odd text is ignored
    tour::State st;
    st.welcomeSeen = true;
    st.started = true;
    st.step = 4;
    st.passed = tour::Machine::bit (2);
    st.completed = 0x0b;
    CHECK (tour::parse (tour::serialize (st)) == st);
    CHECK (tour::parse ("") == tour::State {});
    {
        const auto odd = tour::parse ("step=99\nwelcome=x\nnothing\npassed=1023\ndone=1\n");
        CHECK (odd.step == tour::stepCount && ! odd.welcomeSeen && odd.passed == 0x7fu && odd.done);
    }

    // Welcome shown once
    tour::Machine fresh;
    CHECK (! fresh.state().welcomeSeen);
    fresh.welcomeShown();
    CHECK (tour::parse (tour::serialize (fresh.state())).welcomeSeen);

    // pause after the beat and the key, passing over the instrument
    auto s = empty();
    tour::Machine m;
    m.start (s);
    addDrums (s);
    projectkey::set (s, tour::defaultKey());
    m.observe (s);
    CHECK (m.step() == tour::instrument);
    m.next (s);
    CHECK (m.step() == tour::chords);
    m.pause();
    CHECK (! m.running() && m.canContinue());

    // a restart: the saved state continues on the same song where it was
    tour::Machine again (tour::parse (tour::serialize (m.state())));
    CHECK (! again.running() && again.canContinue() && again.step() == tour::chords);
    again.resume (s);
    CHECK (again.running() && again.step() == tour::chords);

    // ... and on another, empty song from the first step that song lacks and the user didn't pass over: the beat
    tour::Machine other (tour::parse (tour::serialize (m.state())));
    auto s2 = empty();
    other.resume (s2);
    CHECK (other.step() == tour::beat);
    // with only the beat there: the key (the instrument was passed with Next, so it is not asked again)
    addDrums (s2);
    tour::Machine third (tour::parse (tour::serialize (m.state())));
    third.resume (s2);
    CHECK (third.step() == tour::key);
    projectkey::set (s2, tour::defaultKey());
    // (going forward the tour still visits a step passed earlier that this song lacks: the instrument)
    CHECK (third.observe (s2) && third.step() == tour::instrument);

    // Skip tour: ended for good; it can't be continued, a new start begins again (Help)
    third.skip();
    CHECK (! third.running() && ! third.canContinue() && third.state().done);
    tour::Machine fromFile (tour::parse (tour::serialize (third.state())));
    CHECK (fromFile.state().done && ! fromFile.canContinue());
    fromFile.resume (s2); // (nothing to continue: a new tour, past what the song shows)
    CHECK (fromFile.running() && fromFile.step() == tour::instrument && ! fromFile.state().done);
    CHECK (fromFile.state().passed == 0);

    // a tour started on a finished song goes straight to the end card
    auto full = empty();
    addDrums (full);
    projectkey::set (full, tour::defaultKey());
    auto inst = addInstrument (full);
    tour::writeChords (full, inst.track);
    addLoop (full);
    tour::addOneKnob (full, inst.track);
    MasterLookSettings look;
    look.id = looks::balanced;
    looks::apply (full, look);
    tour::Machine done;
    done.start (full);
    CHECK (done.atEnd());
}

TEST (tour190dv_demo_songs_are_lynx_made_simple_tracks)
{
    using namespace lynx;
    for (int d = 1; d < demos::count; ++d)
    {
        const auto song = demos::make (d);
        const auto again = demos::make (d);
        // the same song every time (Loop Starter's own grooves with a fixed seed, no sample library)
        CHECK (song.channels.size() == again.channels.size() && song.patterns.size() == again.patterns.size()
               && song.playlist.size() == again.playlist.size());
        bool same = true;
        for (std::size_t p = 0; p < song.patterns.size() && p < again.patterns.size(); ++p)
            same = same && song.patterns[p].notes == again.patterns[p].notes;
        CHECK (same);
        // only LYNX's own instruments: no samples, no plugins
        for (const auto& c : song.channels)
        {
            CHECK (c.samplePath.empty());
            CHECK (c.instrument != InstrumentKind::plugin && c.instrument != InstrumentKind::sampler);
        }
        // Simple tracks: drums and three instruments, each with notes; a key, one-knob effects, a Master look
        int drums = 0, instruments = 0;
        for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
            if (const auto k = simpletracks::kindOf (song, t))
            {
                drums += *k == simpletracks::Kind::drums ? 1 : 0;
                instruments += *k == simpletracks::Kind::instrument ? 1 : 0;
            }
        CHECK (drums == 1 && instruments == 3);
        for (int step : { tour::beat, tour::key, tour::instrument, tour::chords, tour::effect, tour::finish })
            CHECK (tour::satisfied (song, step));
        CHECK (song.info.comments.find ("LYNX") != std::string::npos && song.info.author == "LYNX SHARE");
        // the bass in a bass register (keys C3 … B3 + a fifth: "Reese Bass" sounds an octave and two under the key)
        for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
            if (song.playlistTracks[(std::size_t) t].name == "Bass")
                if (const auto p = simpletracks::patternOf (song, t); p >= 0)
                    for (const auto& n : song.patterns[(std::size_t) p].notes)
                        CHECK (n.key >= 48 && n.key <= 66);
        CHECK (song.bpm == (d == 1 ? 90.0 : 112.0));
        // 16 bars long
        std::int64_t end = 0;
        for (const auto& c : song.playlist)
            end = std::max (end, c.start + c.length);
        CHECK (end == 16 * 4 * (std::int64_t) ppq);
    }
}

TEST (tour190dv_demo_songs_render_clean_at_about_minus_14_lufs)
{
    using namespace lynx;
    const auto report = std::getenv ("LYNX_190DV_REPORT") != nullptr;
    for (int d = 1; d < demos::count; ++d)
    {
        const auto m = tour190dv::render (demos::make (d));
        if (report)
            std::printf ("  demo %d %s: %.1f s, %.2f LUFS, true peak %.2f dBTP, sample peak %.2f dBFS\n", d, demos::name (d), m.seconds, m.lufs,
                         m.truePeak, m.samplePeak);
        CHECK (m.finite);
        CHECK (m.seconds > 30.0 && m.seconds < 50.0);
        CHECK (m.lufs > -15.0 && m.lufs < -13.0); // (about −14 LUFS: the streaming level)
        CHECK (m.samplePeak < -0.5);              // no clipping
        CHECK (m.truePeak < -0.5);                // (the Apex Limiter's true-peak ceiling: −1 dBTP)
    }
}
