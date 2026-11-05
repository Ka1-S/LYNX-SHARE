// Round 169d (issue #32 follow-up 2, medievilmusic): docs/analise/REA_rodada169d.md
#define I32D_REQUIRE(x) \
    do { if (! (x)) { CHECK (x); return; } } while (false)

namespace i32d
{
// records each note on / off with the sample it reached the instrument at (the renderer's block clock)
struct NoteSpy final : lynx::Instrument
{
    struct Event
    {
        bool on;
        int key;
        long long sample;
    };
    std::vector<Event> log;
    long long rendered = 0, position = 0, chunk = 0;
    std::array<int, 128> sounding {};
    void prepare (double) noexcept override {}
    void noteOn (int key, float) noexcept override
    {
        log.push_back ({ true, key, rendered + position });
        ++sounding[(std::size_t) key];
    }
    void noteOff (int key) noexcept override
    {
        log.push_back ({ false, key, rendered + position });
        sounding[(std::size_t) key] = std::max (0, sounding[(std::size_t) key] - 1);
    }
    void allNotesOff() noexcept override { sounding.fill (0); }
    void render (float*, float*, int n, float, float) noexcept override { position += n; }
    bool idle() const noexcept override { return false; }
    void beginChunk (int n) noexcept override // (a chunk's notes split its render calls: position = within it)
    {
        rendered += chunk;
        chunk = n;
        position = 0;
    }
    int offs (int key) const
    {
        int c = 0;
        for (const auto& e : log)
            c += (! e.on && e.key == key) ? 1 : 0;
        return c;
    }
    long long lastOff (int key) const
    {
        long long at = -1;
        for (const auto& e : log)
            if (! e.on && e.key == key)
                at = e.sample;
        return at;
    }
};

struct Rig
{
    lynx::Song song = lynx::Song::makeDefault();
    double rate = 48000.0, tps = 0.0, tick = 0.0;
    std::shared_ptr<NoteSpy> spy = std::make_shared<NoteSpy>();
    std::shared_ptr<const lynx::RackState> state;
    lynx::RackRenderer renderer;
    std::vector<float> l = std::vector<float> (256), r = std::vector<float> (256);

    Rig()
    {
        song.playlist.clear();
        song.mode = lynx::PlayMode::pattern;
        for (auto& p : song.patterns)
            p.notes.clear();
        song.currentPattern = 0;
        tps = lynx::ticksPerSample (song.bpm, rate);
        renderer.prepare (rate, 256);
        renderer.setTransportOptions (true, false, 0);
    }
    static lynx::Note note (std::int64_t start, std::int64_t length, int key)
    {
        lynx::Note n;
        n.start = start;
        n.length = length;
        n.key = key;
        n.channel = 1;
        return n;
    }
    void rebuild() // the song was edited: a new snapshot, as the engine publishes it (same instrument)
    {
        state = lynx::RackState::build (song, state.get(), rate);
        const_cast<lynx::RackState&> (*state).instruments[1] = spy;
    }
    void play (double beats)
    {
        const auto blocks = (int) std::ceil (beats * lynx::ppq / (256 * tps));
        for (int b = 0; b < blocks; ++b, tick += 256 * tps)
            renderer.process (state.get(), true, tick, 256, l.data(), r.data());
    }
    long long sampleOfBeat (double beat) const { return (long long) std::llround (beat * lynx::ppq / tps); }
};
} // namespace i32d

TEST (issue32d_scheduled_notes_ledger_keeps_each_note_s_end)
{
    lynx::ScheduledNotes ledger;
    const double tps = 0.01;
    lynx::EventBuffer ev;
    auto add = [&] (lynx::NoteEvent::Type type, int key, int offset, float length) {
        lynx::NoteEvent e;
        e.type = type;
        e.channel = 2;
        e.key = key;
        e.sampleOffset = offset;
        e.lengthTicks = length;
        ev.add (e);
    };
    // block 1 (100 samples = 1 tick): key 60 starts at sample 10, 3 ticks long (ends at sample 310)
    add (lynx::NoteEvent::Type::on, 60, 10, 3.0f);
    add (lynx::NoteEvent::Type::off, 61, 50, 0.0f); // nothing of key 61 sounds: dropped
    ledger.process (ev, 100, tps);
    I32D_REQUIRE (ev.size() == 1);
    CHECK (ev[0].type == lynx::NoteEvent::Type::on && ledger.sounding() == 1);
    // the note was deleted: no "off" comes from the scheduler. Blocks 2 and 3: nothing yet; block 4: its own off
    for (int b = 1; b < 3; ++b)
    {
        ev.clear();
        ledger.process (ev, 100, tps);
        CHECK (ev.size() == 0);
    }
    ev.clear();
    ledger.process (ev, 100, tps);
    I32D_REQUIRE (ev.size() == 1);
    CHECK (ev[0].type == lynx::NoteEvent::Type::off && ev[0].key == 60 && ev[0].channel == 2);
    CHECK (ev[0].sampleOffset == 12); // its end (sample 310) + the 2-sample allowance
    CHECK (ledger.sounding() == 0);

    // an "off" at a note's end ends it (and passes); one far from it (the note was edited) is dropped
    ev.clear();
    add (lynx::NoteEvent::Type::on, 64, 0, 0.5f); // ends at sample 50
    add (lynx::NoteEvent::Type::off, 64, 30, 0.0f);
    add (lynx::NoteEvent::Type::off, 64, 51, 0.0f);
    ledger.process (ev, 100, tps);
    I32D_REQUIRE (ev.size() == 2);
    CHECK (ev[1].type == lynx::NoteEvent::Type::off && ev[1].sampleOffset == 51);
    CHECK (ledger.sounding() == 0);

    // audio clips (keys from 128) and notes of unknown length pass as before
    ev.clear();
    add (lynx::NoteEvent::Type::on, lynx::audioClipKey (3), 0, 1.0f);
    add (lynx::NoteEvent::Type::on, 70, 0, 0.0f);
    add (lynx::NoteEvent::Type::off, 70, 90, 0.0f);
    add (lynx::NoteEvent::Type::off, lynx::audioClipKey (3), 95, 0.0f);
    ledger.process (ev, 100, tps);
    CHECK (ev.size() == 4 && ledger.sounding() == 0);

    // channels moved: the note goes with its channel; a removed channel's note is forgotten
    ev.clear();
    add (lynx::NoteEvent::Type::on, 40, 0, 50.0f);
    ledger.process (ev, 100, tps);
    ledger.moveChannels ({ 0, 2, 1 }); // old channel 2 is now 1
    ev.clear();
    for (int b = 0; b < 60 && ev.size() == 0; ++b)
        ledger.process (ev, 100, tps);
    I32D_REQUIRE (ev.size() == 1);
    CHECK (ev[0].channel == 1 && ev[0].key == 40);
    ev.clear();
    add (lynx::NoteEvent::Type::on, 41, 0, 50.0f);
    ledger.process (ev, 100, tps);
    ledger.moveChannels ({ 0, 1 }); // channel 2 removed
    CHECK (ledger.sounding() == 0);
}

TEST (issue32d_a_note_deleted_while_it_sounds_ends_where_it_was_going_to)
{
    // "while editing notes during playback some notes randomly start voicing": a note deleted (or moved) while it
    // sounds never got its note off and hung until Stop. FL plays it to its end (r169d f13–f16).
    i32d::Rig rig;
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (0, 2 * lynx::ppq, 60));
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (3 * lynx::ppq, lynx::ppq / 2, 62)); // (keeps the pattern 1 bar)
    rig.rebuild();
    rig.play (1.0);
    I32D_REQUIRE (rig.spy->sounding[60] == 1);
    rig.song.patterns[0].notes.erase (rig.song.patterns[0].notes.begin());
    rig.rebuild();
    rig.play (0.9);
    CHECK (rig.spy->offs (60) == 0); // not cut: it plays on...
    rig.play (0.2);
    CHECK (rig.spy->offs (60) == 1); // ...to its end
    CHECK (rig.spy->sounding[60] == 0);
    CHECK (std::abs (rig.spy->lastOff (60) - rig.sampleOfBeat (2.0)) <= 3);
    CHECK (rig.renderer.scheduledNotesSounding() == 0);
}

TEST (issue32d_a_new_pattern_while_playing_leaves_no_note_hanging)
{
    // "same when adding patterns while playing": Pattern mode switches to the new, empty pattern; the old pattern's
    // sounding notes still end (FL r169d f17–f20)
    i32d::Rig rig;
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (0, 3 * lynx::ppq, 57));
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (0, 3 * lynx::ppq, 64));
    rig.song.patterns.push_back (rig.song.patterns[0]);
    rig.song.patterns.back().notes.clear(); // the new pattern
    rig.rebuild();
    rig.play (1.0);
    I32D_REQUIRE (rig.spy->sounding[57] == 1 && rig.spy->sounding[64] == 1);
    rig.song.currentPattern = 1;
    rig.rebuild();
    rig.play (2.5);
    CHECK (rig.spy->sounding[57] == 0 && rig.spy->sounding[64] == 0);
    CHECK (rig.spy->offs (57) == 1 && rig.spy->offs (64) == 1);
}

TEST (issue32d_an_edited_note_s_off_cannot_cut_another_voice)
{
    // a note drawn in behind the play head (its "on" already passed) has an "off" ahead; it must not end the long
    // note of the same key that is sounding. And a sounding note made longer still ends where it started to end.
    i32d::Rig rig;
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (0, 3 * lynx::ppq, 60));
    rig.rebuild();
    rig.play (1.0);
    rig.song.patterns[0].notes.push_back (i32d::Rig::note (lynx::ppq / 2, lynx::ppq, 60)); // ends at beat 1.5
    rig.rebuild();
    rig.play (1.0);
    CHECK (rig.spy->offs (60) == 0);
    CHECK (rig.spy->sounding[60] == 1);
    rig.song.patterns[0].notes[0].length = 4 * lynx::ppq - 10; // longer while it sounds
    rig.rebuild();
    rig.play (1.5);
    CHECK (rig.spy->offs (60) == 1); // one off, at the length it started with
    CHECK (std::abs (rig.spy->lastOff (60) - rig.sampleOfBeat (3.0)) <= 3);
    CHECK (rig.spy->sounding[60] == 0);
}

TEST (issue32d_unedited_notes_keep_their_exact_offs)
{
    // nothing changes for notes nobody touches: each off is the scheduler's own, on its sample
    i32d::Rig rig;
    for (int i = 0; i < 16; ++i)
        rig.song.patterns[0].notes.push_back (i32d::Rig::note (i * lynx::ppq / 4, lynx::ppq / 8 + i, 48 + (i % 5)));
    rig.rebuild();
    rig.play (7.95); // two passes of the pattern (the last note ends at beat 7.89)
    int ons = 0, offs = 0;
    for (const auto& e : rig.spy->log)
        (e.on ? ons : offs) += 1;
    CHECK (ons == 32 && offs == 32);
    // each note's length is the scheduler's (± its rounding of on and off)
    for (std::size_t i = 0; i < rig.spy->log.size(); ++i)
        if (rig.spy->log[i].on)
            for (std::size_t j = i + 1; j < rig.spy->log.size(); ++j)
                if (! rig.spy->log[j].on && rig.spy->log[j].key == rig.spy->log[i].key)
                {
                    const auto n = (int) ((rig.spy->log[i].sample - rig.sampleOfBeat (0)) * rig.tps / (lynx::ppq / 4.0) + 0.5) % 16;
                    const auto expected = (double) (lynx::ppq / 8 + n) / rig.tps;
                    CHECK (std::abs ((double) (rig.spy->log[j].sample - rig.spy->log[i].sample) - expected) <= 1.0);
                    break;
                }
    CHECK (rig.renderer.scheduledNotesSounding() == 0);
}

TEST (issue32d_a_ruler_click_goes_to_the_nearest_grid_line)
{
    // "the cursor in the sequencer is clicking on one place but the playback triangle is reacting on an earlier spot":
    // the ruler put the song position on the grid line before the mouse (up to a whole grid step earlier; "Line
    // (follows zoom)" zoomed out is two bars or more). FL takes the nearest line (r169d f32–f34, Bar snap).
    const std::int64_t bar = 4 * lynx::ppq;
    CHECK (lynx::noteedit::snapRound (4.33 * (double) bar, bar) == 4 * bar);  // bar 5.33 → 5:01
    CHECK (lynx::noteedit::snapRound (4.5 * (double) bar, bar) == 5 * bar);   // bar 5.5 → 6:01
    CHECK (lynx::noteedit::snapRound (4.83 * (double) bar, bar) == 5 * bar);  // bar 5.83 → 6:01
    CHECK (lynx::noteedit::snapDown (4.83 * (double) bar, bar) == 4 * bar);   // (what it did)
    CHECK (lynx::noteedit::snapRound (0.2 * (double) bar, bar) == 0);
    CHECK (lynx::noteedit::snapRound (10.6, 1) == 11);
}

TEST (issue32d_the_brush_does_not_stack_a_copy_on_the_same_clip)
{
    // "when I use the brush or pen tool this can cause problems sometimes by adding some patterns in places I didn't
    // want and later on playback they sound doubled (louder)": a brush stroke over clips of the same pattern put a
    // copy on each of them. FL's brush adds none there (r169d f37–f39).
    std::vector<lynx::Clip> playlist;
    lynx::Clip a;
    a.kind = lynx::ClipKind::pattern;
    a.pattern = 3;
    a.track = 2;
    a.start = 4 * lynx::ppq;
    a.length = 4 * lynx::ppq;
    playlist.push_back (a);
    auto c = a;
    CHECK (lynx::noteedit::paintWouldStack (playlist, c)); // the same place
    c.start = 6 * lynx::ppq;
    CHECK (lynx::noteedit::paintWouldStack (playlist, c)); // half over it
    c.start = 8 * lynx::ppq;
    CHECK (! lynx::noteedit::paintWouldStack (playlist, c)); // right after it: painted
    c.start = a.start;
    c.track = 3;
    CHECK (! lynx::noteedit::paintWouldStack (playlist, c)); // another track
    c.track = 2;
    c.pattern = 4;
    CHECK (! lynx::noteedit::paintWouldStack (playlist, c)); // another pattern (FL lets clips overlap)
    lynx::Clip audio;
    audio.kind = lynx::ClipKind::audio;
    audio.channel = 7;
    audio.track = 2;
    audio.start = a.start;
    audio.length = lynx::ppq;
    CHECK (! lynx::noteedit::paintWouldStack (playlist, audio));
    playlist.push_back (audio);
    audio.start += lynx::ppq / 2;
    CHECK (lynx::noteedit::paintWouldStack (playlist, audio));
}
