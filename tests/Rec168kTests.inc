// Round 168k: Song-mode MIDI recording folded into the first bars (the YouTube review of 0.1.21, "it just maxes out at
// two bars"). The take runs the way AudioEngine::updateRecording runs it: NoteRecorder events in time order, each
// finished batch committed by commitNoteBatch inside one edit followed by followPatternLengths (AudioEngine::editSong),
// the song played linearly from the take's first note (Song mode without a loop region).

namespace rec168k
{
struct Take
{
    lynx::Song& song;
    lynx::NoteRecorder recorder;
    std::vector<int> cleared;
    bool first = true;
    bool placeClip = true;
    lynx::TakeQuantize q {};

    explicit Take (lynx::Song& s) : song (s)
    {
        q.grid = 0;
        recorder.begin (song.patterns[(std::size_t) song.currentPattern].effectiveLength (song.meter()));
        if (song.mode == lynx::PlayMode::song && ! song.hasLoop())
            recorder.setLinear (true); // AudioEngine: noteTakeLinear for the whole take
    }
    void flush()
    {
        auto notes = recorder.takeFinished();
        if (notes.empty())
            return;
        const auto before = lynx::patternLengths (song);
        lynx::commitNoteBatch (song, recorder, std::move (notes), lynx::RecordMode::overdub, q, cleared, placeClip, first);
        lynx::followPatternLengths (before, song);
        first = false;
    }
    void event (double tick, int key, bool on)
    {
        recorder.add (song, { tick, 0, key, on ? 0.8f : 0.0f, on });
        flush();
    }
    void note (double tick, double length, int key)
    {
        event (tick, key, true);
        event (tick + length, key, false);
    }
    void stop (double tick)
    {
        recorder.end (song, tick);
        flush();
    }
};

// the probe's song: Song mode, Pattern 1 played as 4 clips of 2 bars (bars 1–8), Pattern 2 new and current
lynx::Song videoSong()
{
    auto s = lynx::Song::makeDefault();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.mode = lynx::PlayMode::song;
    s.patterns[0].notes.clear();
    s.patterns.resize (2);
    s.patterns[1] = lynx::Pattern {};
    s.playlist.clear();
    for (int i = 0; i < 4; ++i)
    {
        lynx::Clip c;
        c.kind = lynx::ClipKind::pattern;
        c.pattern = 0;
        c.track = 0;
        c.start = 2 * bar * i;
        c.length = 2 * bar;
        s.playlist.push_back (c);
    }
    s.patterns[0].lengthTicks = 2 * bar;
    lynx::Note n0;
    n0.length = lynx::ppq;
    s.patterns[0].notes.push_back (n0);
    s.currentPattern = 1;
    return s;
}

// FL's rule for takes inside a clip longer than its pattern (the pattern grows) is LYNX's with "Stretched pattern clips
// loop their pattern" off; on (the default) too since issue #126 (RecNoFold126Tests.inc)
struct FlClipRule
{
    FlClipRule() : was (lynx::stretchedClipsLoop()) { lynx::setStretchedClipsLoop (false); }
    ~FlClipRule() { lynx::setStretchedClipsLoop (was); }
    bool was;
};

std::vector<const lynx::Clip*> clipsOf (const lynx::Song& s, int pattern)
{
    std::vector<const lynx::Clip*> out;
    for (const auto& c : s.playlist)
        if (c.kind == lynx::ClipKind::pattern && c.pattern == pattern)
            out.push_back (&c);
    return out;
}
} // namespace rec168k

TEST (rec168k_song_take_of_8_bars_into_an_empty_pattern_keeps_8_bars)
{
    // the video (probe case A): 4 notes a bar over bars 1–8, Pattern 2 has no clip → it used to store all 32 notes in
    // bar 1 (the loop length frozen at the empty pattern's 1 bar)
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    rec168k::Take take (s);
    for (int b = 0; b < 8; ++b)
        for (int beat = 0; beat < 4; ++beat)
            take.note ((double) (b * bar + beat * lynx::ppq), lynx::ppq * 0.9, 60 + b);
    take.stop ((double) (8 * bar));

    const auto& p = s.patterns[1];
    CHECK (p.notes.size() == 32);
    CHECK (p.effectiveLength (s.meter()) == 8 * bar);
    for (const auto& n : p.notes)
        CHECK (n.start / bar == n.key - 60); // every note in the bar it was played in
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1 && clips[0]->start == 0 && clips[0]->length == 8 * bar && clips[0]->offset == 0);
    CHECK (clips[0]->track == 1); // the first free track
    CHECK (s.songLength() == 8 * bar);
}

TEST (rec168k_take_clip_starts_at_the_bar_of_the_first_note_and_grows_past_the_song_end)
{
    // FL (REA_rodada165z-b fh2–fh5): recording started at bar 1, the first event at bar 3 → the clip starts at bar 3;
    // the song plays on past its end (8 bars) while recording: bars 9–10 are kept where they were played
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    rec168k::Take take (s);
    for (int b = 2; b < 10; ++b)
        take.note ((double) (b * bar + lynx::ppq / 2), lynx::ppq, 60 + b);
    take.stop ((double) (10 * bar + lynx::ppq));

    const auto& p = s.patterns[1];
    CHECK (p.notes.size() == 8);
    CHECK (p.notes.front().start == lynx::ppq / 2 && p.notes.front().key == 62);
    CHECK (p.notes.back().start == 7 * bar + lynx::ppq / 2 && p.notes.back().key == 69);
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1 && clips[0]->start == 2 * bar && clips[0]->length == 8 * bar);
    CHECK (s.songLength() == 10 * bar);
    // every note plays at the song position it was played at
    for (const auto& n : p.notes)
        CHECK (clips[0]->start + n.start == (std::int64_t) (n.key - 60) * bar + lynx::ppq / 2);
}

TEST (rec168k_note_held_at_stop_is_kept_to_the_stop)
{
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    rec168k::Take take (s);
    take.note ((double) bar, lynx::ppq, 60);
    take.event ((double) (5 * bar + lynx::ppq), 67, true); // held when the transport stops
    take.stop ((double) (5 * bar + 3 * lynx::ppq));
    const auto& p = s.patterns[1];
    CHECK (p.notes.size() == 2);
    CHECK (p.notes.back().key == 67 && p.notes.back().start == 4 * bar + lynx::ppq && p.notes.back().length == 2 * lynx::ppq);
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1 && clips[0]->start == bar && clips[0]->length == 5 * bar);
}

TEST (rec168k_take_extends_the_clip_it_starts_in)
{
    // a clip of the pattern (automatic length, 1 bar) where the take starts: what is played past its end continues the
    // pattern after it, and the clip follows the pattern's length
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 1;
    c.track = 2;
    c.start = 4 * bar;
    c.length = bar;
    s.playlist.push_back (c);
    rec168k::Take take (s);
    for (int b = 4; b < 8; ++b)
        take.note ((double) (b * bar), lynx::ppq, 60 + b);
    take.stop ((double) (8 * bar));
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1 && clips[0]->start == 4 * bar && clips[0]->length == 4 * bar && clips[0]->track == 2);
    CHECK (s.patterns[1].notes.size() == 4 && s.patterns[1].notes.back().start == 3 * bar);
}

TEST (rec168k_set_pattern_length_grows_to_the_notes_recorded_past_it)
{
    // Pattern 2 set to 1 bar (Channel Rack › Length), no clip: 3 bars recorded → 3 bars (whole bars), clip follows
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.patterns[1].lengthTicks = bar;
    rec168k::Take take (s);
    for (int b = 0; b < 3; ++b)
        take.note ((double) (b * bar + lynx::ppq), lynx::ppq, 60 + b);
    take.stop ((double) (3 * bar));
    CHECK (s.patterns[1].lengthTicks == 3 * bar);
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1 && clips[0]->start == 0 && clips[0]->length == 3 * bar);
}

TEST (rec168k_loop_region_overdubs_one_origin)
{
    // Song mode with a loop region over bars 3–4: two passes land in the same 2 bars, the clip starts at the region
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.loopStart = 2 * bar;
    s.loopEnd = 4 * bar;
    rec168k::Take take (s);
    take.note ((double) (3 * bar + lynx::ppq), lynx::ppq / 2, 60); // first pass: bar 4
    take.note ((double) (2 * bar + lynx::ppq), lynx::ppq / 2, 62); // second pass: bar 3
    take.stop ((double) (3 * bar));
    const auto& p = s.patterns[1];
    CHECK (p.notes.size() == 2);
    CHECK (p.notes[0].key == 62 && p.notes[0].start == lynx::ppq);
    CHECK (p.notes[1].key == 60 && p.notes[1].start == bar + lynx::ppq);
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1 && clips[0]->start == 2 * bar && clips[0]->length == 2 * bar);
}

TEST (rec168k_pitch_bend_lands_with_the_notes)
{
    // the wheel (automation clip in Song mode) and the notes of one take, both past the song's end: same song position
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    rec168k::Take take (s);
    const auto at = (double) (9 * bar + lynx::ppq);
    take.note ((double) (6 * bar), lynx::ppq, 60);
    take.note (at, lynx::ppq, 64);
    take.stop ((double) (10 * bar));
    lynx::AutomationWrite w;
    w.target = { lynx::AutomationTargetKind::channelParam, 0, 0, lynx::midictl::pitchBendBase };
    w.name = "Pitch";
    lynx::commitAutomationPoints (s, w, { { at, 0.5f }, { at + lynx::ppq / 2, 0.9f } }, 0, true);
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1);
    const auto& late = s.patterns[1].notes.back();
    CHECK (late.key == 64 && clips[0]->start + late.start == (std::int64_t) at);
    const lynx::Clip* bend = nullptr;
    for (const auto& c : s.playlist)
        if (c.kind == lynx::ClipKind::automation && c.automation == w.automation)
            bend = &c;
    CHECK (bend != nullptr);
    if (bend != nullptr)
    {
        const auto& a = s.automations[(std::size_t) bend->automation];
        CHECK (! a.points.empty() && bend->start + a.points.front().tick - bend->offset == (std::int64_t) at);
    }
}

TEST (rec168k_fl_measured_take_in_a_one_bar_song)
{
    // FL 2026 [EXEC rec168k m0a/m1a/m1b]: Pattern 1 clip on bars 1–2 (a 1-bar song), Song mode, new Pattern 2,
    // record from bar 1; the song counted on (2:08 before any note), notes just before bars 3…8, the last one held
    // through Stop at ~8:3 → Pattern 2 clip on Track 2 from bar 2 (the first note's bar) to bar 9, every note where it
    // was played, the held note kept to the Stop
    auto s = lynx::Song::makeDefault();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.mode = lynx::PlayMode::song;
    s.patterns.resize (2);
    s.patterns[1] = lynx::Pattern {};
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 0;
    c.length = bar;
    s.playlist = { c };
    s.currentPattern = 1;
    CHECK (s.songLength() == bar);
    rec168k::Take take (s);
    const auto early = lynx::ppq / 8;
    for (int b = 2; b < 7; ++b) // (tick b·bar = FL's bar b + 1)
        take.note ((double) (b * bar - early), lynx::ppq / 2, 60 + b);
    take.event ((double) (7 * bar - early), 67, true);
    take.stop ((double) (7 * bar + 2 * lynx::ppq + lynx::ppq / 3));
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1 && clips[0]->start == bar && clips[0]->end() == 8 * bar && clips[0]->track == 1);
    const auto& p = s.patterns[1];
    CHECK (p.notes.size() == 6);
    for (const auto& n : p.notes)
        CHECK (clips[0]->start + n.start == (std::int64_t) (n.key - 60) * bar - early);
    CHECK (p.notes.back().key == 67 && p.notes.back().length == 2 * lynx::ppq + lynx::ppq / 3 + early);
}

TEST (rec168k_take_inside_a_stretched_clip_grows_the_pattern)
{
    const rec168k::FlClipRule flRule;
    // FL 2026 [EXEC rec168k z_m2b, f12]: Pattern 3 = one kick step (1 bar), its clip stretched to bars 1–5; notes at
    // 2.1, 3.3 and 4.2 stay there (D4 at the pattern's bar 2 in the Piano roll) — the pattern grows, nothing folds
    auto s = lynx::Song::makeDefault();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.mode = lynx::PlayMode::song;
    s.patterns[0].notes.clear();
    lynx::Note kick;
    kick.length = lynx::ticksPerStep;
    s.patterns[0].notes.push_back (kick);
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 0;
    c.track = 2;
    c.length = 4 * bar;
    s.playlist = { c };
    rec168k::Take take (s);
    take.note ((double) bar, lynx::ppq / 2, 62);
    take.note ((double) (2 * bar + 2 * lynx::ppq), lynx::ppq / 2, 64);
    take.note ((double) (3 * bar + lynx::ppq), lynx::ppq / 2, 65);
    take.stop ((double) (3 * bar + 3 * lynx::ppq));
    const auto& p = s.patterns[0];
    CHECK (p.notes.size() == 4);
    CHECK (p.notes[1].start == bar && p.notes[2].start == 2 * bar + 2 * lynx::ppq && p.notes[3].start == 3 * bar + lynx::ppq);
    CHECK (p.effectiveLength (s.meter()) == 4 * bar);
    CHECK (s.playlist.size() == 1 && s.playlist[0].length == 4 * bar); // no clip added
}

TEST (rec168k_probe_case_b_set_length_pattern_in_an_8_bar_clip)
{
    const rec168k::FlClipRule flRule;
    // the video's other case (probe "8 B"): Pattern 2 set to 2 bars, its clip stretched to 8 bars, 8 bars recorded →
    // the notes of bars 3–8 used to be stored past the pattern's 2 bars, never shown nor played. Now the pattern grows to
    // 8 bars (as FL's does), so the clip shows and plays all 8 bars once
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.patterns[1].lengthTicks = 2 * bar;
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 1;
    c.track = 1;
    c.length = 8 * bar;
    s.playlist.push_back (c);
    rec168k::Take take (s);
    for (int b = 0; b < 8; ++b)
        for (int beat = 0; beat < 4; ++beat)
            take.note ((double) (b * bar + beat * lynx::ppq), lynx::ppq * 0.9, 60 + b);
    take.stop ((double) (8 * bar));
    const auto& p = s.patterns[1];
    CHECK (p.notes.size() == 32 && p.lengthTicks == 8 * bar && p.effectiveLength (s.meter()) == 8 * bar);
    for (const auto& n : p.notes)
        CHECK (n.start / bar == n.key - 60);
    const auto clips = rec168k::clipsOf (s, 1);
    CHECK (clips.size() == 1 && clips[0]->start == 0 && clips[0]->length == 8 * bar);
}

TEST (rec168k_auto_length_pattern_stretched_clip_8_bars)
{
    const rec168k::FlClipRule flRule;
    // the video's moment 7: Pattern 1 recorded as 2 bars, its clip dragged to 8 bars, recorded again over the 8 bars
    auto s = rec168k::videoSong();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    lynx::Note held;
    held.start = bar + lynx::ppq;
    held.length = lynx::ppq;
    held.key = 50;
    s.patterns[1].notes = { held }; // automatic length: 2 bars
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 1;
    c.track = 1;
    c.length = 8 * bar;
    s.playlist.push_back (c);
    rec168k::Take take (s);
    for (int b = 2; b < 8; ++b)
        take.note ((double) (b * bar), lynx::ppq, 60 + b);
    take.stop ((double) (8 * bar));
    CHECK (s.patterns[1].notes.size() == 7 && s.patterns[1].effectiveLength (s.meter()) == 8 * bar);
    CHECK (s.patterns[1].notes.back().start == 7 * bar);
    CHECK (rec168k::clipsOf (s, 1).size() == 1);
}

TEST (rec168k_take_inside_a_long_clip_keeps_the_clip)
{
    const rec168k::FlClipRule flRule;
    // a take inside a pattern clip longer than its pattern grows the pattern only to the last recorded bar; the clip
    // keeps its length and place (FL plays it once: empty after the pattern's end); takes into another pattern leave
    // it alone
    auto s = lynx::Song::makeDefault();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.mode = lynx::PlayMode::song;
    s.patterns[0].notes.clear();
    lynx::Note kick;
    kick.length = lynx::ticksPerStep;
    s.patterns[0].notes.push_back (kick); // 1 bar
    s.patterns.resize (2);
    s.patterns[1] = lynx::Pattern {};
    lynx::Clip loop;
    loop.kind = lynx::ClipKind::pattern;
    loop.pattern = 0;
    loop.length = 8 * bar; // the 1-bar pattern 8 times
    s.playlist = { loop };

    // a take into Pattern 2 elsewhere: the loop clip and its pattern are untouched
    s.currentPattern = 1;
    {
        rec168k::Take take (s);
        take.note ((double) (3 * bar), lynx::ppq, 60);
        take.stop ((double) (4 * bar));
    }
    CHECK (s.playlist[0].length == 8 * bar && s.patterns[0].effectiveLength (s.meter()) == bar);

    // a take inside the long clip, in its second bar: the pattern becomes 2 bars, the clip stays 8 bars
    s.currentPattern = 0;
    {
        rec168k::Take take (s);
        take.note ((double) (bar + lynx::ppq), lynx::ppq, 64);
        take.stop ((double) (2 * bar));
    }
    const auto clips = rec168k::clipsOf (s, 0);
    CHECK (clips.size() == 1 && clips[0]->length == 8 * bar && clips[0]->offset == 0);
    CHECK (s.patterns[0].effectiveLength (s.meter()) == 2 * bar);
    CHECK (s.patterns[0].notes.size() == 2 && s.patterns[0].notes.back().start == bar + lynx::ppq);
}

TEST (rec168k_stretched_pattern_clip_plays_its_pattern_once)
{
    // FL 2026 [EXEC rec168k q168k.wav, command-line render]: a 1-bar pattern (notes at beats 1 and 3) in a clip
    // stretched to 4 bars sounds at 0.0 s and 1.0 s only (120 BPM), then silence to the clip's end; a 1-bar control clip
    // at 10 s sounds at 10.0 and 11.0 s. LYNX plays it the same with its extra "Stretched pattern clips loop their
    // pattern" off; on (the default, issue #25) the pattern plays 4 times.
    using namespace lynx;
    const auto wasLooping = stretchedClipsLoop();
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    const std::int64_t bar = (std::int64_t) ppq * s.beatsPerBar;
    s.patterns[0].notes.clear();
    Note a;
    a.length = ppq / 2;
    s.patterns[0].notes.push_back (a);
    a.start = 2 * ppq;
    a.key = 72;
    s.patterns[0].notes.push_back (a);
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = 0;
    c.length = 4 * bar;
    s.playlist = { c };
    const auto sr = 48000.0;
    const auto tps = ticksPerSample (s.bpm, sr);
    const auto index = makeScheduleIndex (s);
    for (const auto loops : { false, true })
    {
        setStretchedClipsLoop (loops);
        int ons = 0;
        for (double t = 0.0; t < (double) (4 * bar); t += 512 * tps)
        {
            EventBuffer ev;
            scheduleSong (s, t, 512, tps, ev, t == 0.0, &index, false);
            for (const auto& e : ev)
                if (e.type == NoteEvent::Type::on)
                    ++ons;
        }
        CHECK (ons == (loops ? 8 : 2));
    }
    setStretchedClipsLoop (wasLooping);
}
