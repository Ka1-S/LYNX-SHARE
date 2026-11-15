// Round 170d (the Busy Works video, 02:07–02:52: "the first chord on the downbeat after the pre-count is not recorded",
// FL drops it too): notes played during the count-in are kept, the ones just before the start go onto it (Cubase's
// catch range, Logic keeps them), MIDI notes are timed by the device's stamp against the audio clock and moved back by
// the output latency (Pro Tools / Cubase / REAPER), audio takes by the measured loopback round trip, and Record ›
// Capture MIDI (Ableton / Logic / Cubase retrospective record). coord/recording-research-0.1.24.md.

namespace rec170d
{
constexpr double bar = 4.0 * lynx::ppq; // 4/4

// a take the way AudioEngine::updateRecording runs it: each event through the count-in catch, then the NoteRecorder,
// every finished batch committed by commitNoteBatch inside one edit
struct Take
{
    lynx::Song& song;
    lynx::CountInCatch catcher;
    lynx::NoteRecorder recorder;
    std::vector<int> cleared;
    bool first = true;
    lynx::TakeQuantize q {};

    Take (lynx::Song& s, std::int64_t start, std::int64_t window, std::int64_t countIn) : song (s)
    {
        q.grid = 0;
        const auto loop = song.patterns[(std::size_t) song.currentPattern].effectiveLength (song.meter());
        catcher.start = start;
        catcher.window = window;
        catcher.songMode = song.mode == lynx::PlayMode::song;
        catcher.wrap = lynx::countInWrap (countIn, loop);
        recorder.begin (loop);
        if (song.mode == lynx::PlayMode::song && ! song.hasLoop())
            recorder.setLinear (true);
    }
    void flush()
    {
        auto notes = recorder.takeFinished();
        if (notes.empty())
            return;
        const auto before = lynx::patternLengths (song);
        lynx::commitNoteBatch (song, recorder, std::move (notes), lynx::RecordMode::overdub, q, cleared, true, first);
        lynx::followPatternLengths (before, song);
        first = false;
    }
    void event (double tick, int key, bool on)
    {
        lynx::RecordedEvent e { tick, 0, key, on ? 0.8f : 0.0f, on };
        if (lynx::catchEarlyNote (e, catcher))
            recorder.add (song, e);
        flush();
    }
    void stop (double tick)
    {
        recorder.end (song, tick);
        flush();
    }
};

lynx::Song emptySong (lynx::PlayMode mode)
{
    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0;
    s.mode = mode;
    for (auto& p : s.patterns)
        p.notes.clear();
    s.playlist.clear();
    s.currentPattern = 0;
    return s;
}

const lynx::Note* find (const lynx::Pattern& p, int key)
{
    for (const auto& n : p.notes)
        if (n.key == key)
            return &n;
    return nullptr;
}

const lynx::Clip* clipOf (const lynx::Song& s, int pattern)
{
    for (const auto& c : s.playlist)
        if (c.kind == lynx::ClipKind::pattern && c.pattern == pattern)
            return &c;
    return nullptr;
}
} // namespace rec170d

TEST (rec170d_live_clock_uses_the_event_time_and_the_output_latency)
{
    // 120 bpm at 48 kHz: 1.92 ticks a millisecond
    lynx::LiveClock c;
    c.tick = 7680.0;
    c.timeMs = 10000.0;
    c.ticksPerMs = 960.0 * 2.0 / 1000.0;
    c.running = true;
    // an event stamped 5 ms after the block, 16 ms of output latency: heard 11 ms before the clock's position
    CHECK (std::abs (lynx::liveTickAt (c, 10005.0, 16.0) - (7680.0 - 11.0 * 1.92)) < 1e-9);
    // without compensation: sample-accurate against the block, not "now"
    CHECK (std::abs (lynx::liveTickAt (c, 10005.0, 0.0) - (7680.0 + 5.0 * 1.92)) < 1e-9);
    // a stamp before the block (the device stamped it while the callback ran)
    CHECK (std::abs (lynx::liveTickAt (c, 9998.0, 0.0) - (7680.0 - 2.0 * 1.92)) < 1e-9);
    // a stamp from another clock is held to a quarter second
    CHECK (std::abs (lynx::liveTickAt (c, 99999.0, 0.0) - (7680.0 + 250.0 * 1.92)) < 1e-9);
    // a stopped clock: its position, whatever the time
    c.running = false;
    CHECK (lynx::liveTickAt (c, 10500.0, 16.0) == 7680.0);
}

TEST (rec170d_count_in_catch_rules)
{
    using lynx::RecordedEvent;
    lynx::CountInCatch c;
    c.start = 7680;
    c.window = lynx::ppq / 4; // a 1/16 note
    c.songMode = true;

    RecordedEvent on { 7600.0, 0, 60, 0.8f, true };
    CHECK (lynx::catchEarlyNote (on, c) && on.tick == 7680.0); // a hair early: onto the start
    RecordedEvent off { 7650.0, 0, 60, 0.0f, false };
    CHECK (lynx::catchEarlyNote (off, c) && off.tick == 7730.0); // released before the start: its played length (50)
    RecordedEvent held { 7590.0, 0, 64, 0.8f, true }, heldOff { 8000.0, 0, 64, 0.0f, false };
    CHECK (lynx::catchEarlyNote (held, c) && held.tick == 7680.0);
    CHECK (lynx::catchEarlyNote (heldOff, c) && heldOff.tick == 8000.0); // released after the start: kept
    RecordedEvent early { 4800.0, 0, 62, 0.8f, true };
    CHECK (lynx::catchEarlyNote (early, c) && early.tick == 4800.0); // earlier in the count-in: its real song position
    RecordedEvent late { 9000.0, 0, 65, 0.8f, true };
    CHECK (lynx::catchEarlyNote (late, c) && late.tick == 9000.0); // after the start: untouched

    // the song start: nothing before it
    lynx::CountInCatch z;
    z.start = 0;
    z.window = lynx::ppq / 4;
    RecordedEvent before { -1000.0, 0, 60, 0.8f, true };
    CHECK (! lynx::catchEarlyNote (before, z));
    RecordedEvent hair { -100.0, 0, 60, 0.8f, true };
    CHECK (lynx::catchEarlyNote (hair, z) && hair.tick == 0.0);

    // pattern mode: the count-in is the loop's previous pass
    lynx::CountInCatch p;
    p.start = 0;
    p.window = lynx::ppq / 4;
    p.songMode = false;
    p.wrap = lynx::countInWrap (3840, 3840);
    CHECK (p.wrap == 3840 && lynx::countInWrap (7680, 3840) == 7680 && lynx::countInWrap (3840, 7680) == 7680);
    RecordedEvent beat4 { -960.0, 0, 67, 0.8f, true }, beat4Off { -500.0, 0, 67, 0.0f, false };
    CHECK (lynx::catchEarlyNote (beat4, p) && beat4.tick == 2880.0);
    CHECK (lynx::catchEarlyNote (beat4Off, p) && beat4Off.tick == 3340.0);
    RecordedEvent over { -960.0, 0, 69, 0.8f, true }, overOff { 500.0, 0, 69, 0.0f, false };
    CHECK (lynx::catchEarlyNote (over, p) && lynx::catchEarlyNote (overOff, p) && overOff.tick == 3840.0); // to the loop's end

    // no count-in: nothing changes
    lynx::CountInCatch none;
    RecordedEvent any { -5.0, 0, 60, 0.8f, true };
    CHECK (lynx::catchEarlyNote (any, none) && any.tick == -5.0);
}

TEST (rec170d_the_video_chord_on_the_downbeat_after_the_count_in_is_recorded)
{
    // Song mode, a 1-bar count-in before bar 3: the chord played 15 ms early (29 ticks), held a beat; then a note a bar
    // for 6 bars ("it stopped after two bars", round 168k): everything is kept, the chord on bar 3
    auto s = rec170d::emptySong (lynx::PlayMode::song);
    const auto start = (std::int64_t) (2 * rec170d::bar);
    rec170d::Take take (s, start, lynx::ppq / 4, (std::int64_t) rec170d::bar);
    for (int key : { 60, 64, 67 })
        take.event ((double) start - 29.0, key, true);
    for (int key : { 60, 64, 67 })
        take.event ((double) start + lynx::ppq, key, false);
    for (int b = 1; b <= 6; ++b)
    {
        take.event ((double) start + b * rec170d::bar, 70 + b, true);
        take.event ((double) start + b * rec170d::bar + lynx::ppq / 2, 70 + b, false);
    }
    take.stop ((double) start + 7 * rec170d::bar);

    const auto& p = s.patterns[0];
    CHECK (p.notes.size() == 9);
    for (int key : { 60, 64, 67 })
    {
        const auto* n = rec170d::find (p, key);
        CHECK (n != nullptr && n->start == 0 && n->length == lynx::ppq); // on the take's first beat, its clip at bar 3
    }
    for (int b = 1; b <= 6; ++b)
    {
        const auto* n = rec170d::find (p, 70 + b);
        CHECK (n != nullptr && n->start == (std::int64_t) (b * rec170d::bar));
    }
    const auto* clip = rec170d::clipOf (s, 0);
    CHECK (clip != nullptr && clip->start == start && clip->length == (std::int64_t) (7 * rec170d::bar));
}

TEST (rec170d_count_in_notes_keep_their_real_time_and_the_window_can_be_off)
{
    // a note in the count-in bar (beat 2 of bar 2) and the chord 15 ms early, catch window off: both where played
    auto s = rec170d::emptySong (lynx::PlayMode::song);
    const auto start = (std::int64_t) (2 * rec170d::bar);
    rec170d::Take take (s, start, 0, (std::int64_t) rec170d::bar);
    take.event ((double) rec170d::bar + lynx::ppq, 62, true);
    take.event ((double) rec170d::bar + 1.5 * lynx::ppq, 62, false);
    take.event ((double) start - 29.0, 60, true);
    take.event ((double) start + lynx::ppq, 60, false);
    take.stop ((double) start + rec170d::bar);
    const auto& p = s.patterns[0];
    const auto* pickup = rec170d::find (p, 62);
    const auto* chord = rec170d::find (p, 60);
    CHECK (pickup != nullptr && pickup->start == lynx::ppq); // the take's origin is bar 2 now
    CHECK (chord != nullptr && chord->start == (std::int64_t) rec170d::bar - 29);
    const auto* clip = rec170d::clipOf (s, 0);
    CHECK (clip != nullptr && clip->start == (std::int64_t) rec170d::bar);
}

TEST (rec170d_pattern_mode_count_in_is_the_previous_loop_pass)
{
    auto s = rec170d::emptySong (lynx::PlayMode::pattern);
    s.patterns[0].lengthTicks = (std::int64_t) rec170d::bar; // a 1-bar pattern
    rec170d::Take take (s, 0, lynx::ppq / 4, (std::int64_t) rec170d::bar);
    take.event (-20.0, 60, true); // a hair early: beat 1
    take.event (400.0, 60, false);
    take.event (-960.0, 67, true); // beat 4 of the count-in: beat 4 of the pattern
    take.event (-480.0, 67, false);
    take.event (960.0, 62, true);
    take.event (1200.0, 62, false);
    take.stop (2000.0);
    const auto& p = s.patterns[0];
    CHECK (p.notes.size() == 3);
    const auto* one = rec170d::find (p, 60);
    const auto* four = rec170d::find (p, 67);
    const auto* two = rec170d::find (p, 62);
    CHECK (one != nullptr && one->start == 0 && one->length == 400);
    CHECK (four != nullptr && four->start == 2880 && four->length == 480);
    CHECK (two != nullptr && two->start == 960);
    CHECK (s.patterns[0].effectiveLength (s.meter()) == (std::int64_t) rec170d::bar);
}

TEST (rec170d_capture_midi_played_while_playing_lands_where_played)
{
    auto s = rec170d::emptySong (lynx::PlayMode::song);
    lynx::scorelog::Log log;
    // an old phrase (stopped), then a phrase played while the song played (bars 5–6), not recorded
    log.noteOn (1000.0, 0, 50, 0.7f);
    log.noteOff (1200.0, 0, 50);
    const double barMs = 2000.0; // 120 bpm
    for (int i = 0; i < 4; ++i)
    {
        const auto ms = 60000.0 + i * barMs / 2;
        log.noteOn (ms, 0, 60 + i, 0.8f, 4 * rec170d::bar + i * 2 * lynx::ppq);
        log.noteOff (ms + 250.0, 0, 60 + i);
    }
    lynx::CaptureOptions o;
    o.nowMs = 70000.0;
    o.gapMs = 4000.0;
    const auto r = lynx::captureMidi (s, log.snapshot(), o);
    CHECK (r.notes == 4 && r.played && r.newestMs == 60000.0 + 3 * barMs / 2);
    const auto& p = s.patterns[0];
    CHECK (p.notes.size() == 4 && rec170d::find (p, 50) == nullptr);
    const auto* clip = rec170d::clipOf (s, 0);
    CHECK (clip != nullptr && clip->start == (std::int64_t) (4 * rec170d::bar)); // a clip where they were played
    for (int i = 0; i < 4; ++i)
    {
        const auto* n = rec170d::find (p, 60 + i);
        CHECK (n != nullptr && n->start == i * 2 * lynx::ppq && n->length == lynx::ppq / 2); // 250 ms = half a beat
    }
    // captured once: the next capture finds nothing new
    o.afterMs = r.newestMs;
    CHECK (lynx::capturePhrase (s, log.snapshot(), o).empty());
}

TEST (rec170d_capture_midi_while_stopped_keeps_the_timing_from_the_start)
{
    // Pattern mode, stopped: the last phrase (after a 10 s pause) from the pattern start, at the project tempo
    auto s = rec170d::emptySong (lynx::PlayMode::pattern);
    lynx::scorelog::Log log;
    log.noteOn (1000.0, 0, 40, 0.5f);
    log.noteOff (1100.0, 0, 40);
    log.noteOn (20000.0, 0, 60, 0.8f);
    log.noteOff (20250.0, 0, 60);
    log.noteOn (20500.0, 0, 64, 0.8f); // a beat later
    log.noteOff (20750.0, 0, 64);
    log.noteOn (22000.0, 0, 67, 0.8f); // a bar after the first
    // (67 still held: it ends now)
    log.noteOn (22100.0, 0, 72, 0.8f, -1.0, true); // went into a take: never captured
    lynx::CaptureOptions o;
    o.nowMs = 22500.0;
    o.gapMs = 4000.0;
    const auto r = lynx::captureMidi (s, log.snapshot(), o);
    CHECK (r.notes == 3 && ! r.played);
    const auto& p = s.patterns[0];
    CHECK (p.notes.size() == 3);
    CHECK (rec170d::find (p, 60) != nullptr && rec170d::find (p, 60)->start == 0 && rec170d::find (p, 60)->length == lynx::ppq / 2);
    CHECK (rec170d::find (p, 64) != nullptr && rec170d::find (p, 64)->start == lynx::ppq);
    CHECK (rec170d::find (p, 67) != nullptr && rec170d::find (p, 67)->start == (std::int64_t) rec170d::bar
           && rec170d::find (p, 67)->length == lynx::ppq); // held: to now (500 ms)
    CHECK (rec170d::find (p, 40) == nullptr && rec170d::find (p, 72) == nullptr);

    // Song mode, stopped with the playhead in bar 3: from bar 3, a clip placed there
    auto song = rec170d::emptySong (lynx::PlayMode::song);
    o.playhead = (std::int64_t) (2 * rec170d::bar + 500);
    const auto r2 = lynx::captureMidi (song, log.snapshot(), o);
    CHECK (r2.notes == 3);
    const auto* clip = rec170d::clipOf (song, 0);
    CHECK (clip != nullptr && clip->start == (std::int64_t) (2 * rec170d::bar));
    CHECK (rec170d::find (song.patterns[0], 64) != nullptr && rec170d::find (song.patterns[0], 64)->start == lynx::ppq);

    // nothing played: nothing captured, nothing changed
    auto quiet = rec170d::emptySong (lynx::PlayMode::pattern);
    CHECK (lynx::captureMidi (quiet, {}, o).notes == 0 && quiet.patterns[0].notes.empty());
}

TEST (rec170d_score_log_dump_ignores_the_capture_fields)
{
    // FL's "Dump score log" (round 165f) is unchanged by the new fields
    lynx::scorelog::Log log;
    log.noteOn (0.0, 0, 60, 0.8f, 1234.0, true);
    log.noteOff (500.0, 0, 60);
    lynx::scorelog::DumpOptions d;
    d.bpm = 120.0;
    const auto notes = lynx::scorelog::dump (log.snapshot(), d);
    CHECK (notes.size() == 1 && notes[0].start == 0 && notes[0].length == lynx::ppq);
}

TEST (rec170d_loopback_audio_take_lands_on_the_played_click)
{
    // (c) audio: a click played at song tick 7680 comes back through a loopback `delay` samples later; the ping measures
    // the loopback, the take is placed with the measured round trip (+ a manual offset): the recorded click sits on 7680
    using namespace lynx::takes;
    const auto delay = 731; // e.g. 512 buffer + converters
    LatencyPing ping;
    ping.start (8, 4800, 0.5f);
    std::vector<float> line (200000, 0.0f), out (128), in (128);
    std::int64_t pos = 0;
    for (int guard = 0; ping.isRunning() && guard < 4000; ++guard)
    {
        for (int i = 0; i < 128; ++i)
            in[(std::size_t) i] = pos + i - delay >= 0 ? 0.8f * line[(std::size_t) (pos + i - delay)] : 0.0f;
        std::fill (out.begin(), out.end(), 0.0f);
        float* outs[] = { out.data(), nullptr };
        ping.process (in.data(), outs, 2, 128);
        std::copy (out.begin(), out.end(), line.begin() + pos);
        pos += 128;
    }
    const auto measured = ping.result();
    CHECK (measured.ok && measured.samples == delay);

    Latency l;
    l.driverInput = 64; // what the driver reports is replaced by the measurement
    l.driverOutput = 64;
    l.measured = measured.samples;
    const auto tps = 960.0 / 24000.0; // 120 bpm, 48 kHz
    // the take's first sample arrived when the transport was at 7000 ticks; the click is in it at sample s
    const auto firstTick = 7000.0;
    const auto clickSample = (7680.0 - 7000.0) / tps + delay; // played at 7680, recorded `delay` later
    const auto p = place (firstTick, 48000.0 * tps, tps, l.total());
    const auto clickTick = p.exactStart + clickSample * tps;
    CHECK (std::abs (clickTick - 7680.0) < 1e-6);
    // a manual offset of +24 samples moves it 24 samples earlier (a converter the loopback did not include)
    l.manual = 24;
    const auto q = place (firstTick, 48000.0 * tps, tps, l.total());
    CHECK (std::abs ((q.exactStart + clickSample * tps) - (7680.0 - 24 * tps)) < 1e-6);
}
