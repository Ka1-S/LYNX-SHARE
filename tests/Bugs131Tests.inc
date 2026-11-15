// --- Round 131 (issue #4): MIDI inputs that were not ready at start-up, pattern length on the first recording,
// Song mode looping at the song's end ------------------------------------------------------------------------------

TEST (bugs131_midi_inputs_open_when_they_appear_and_reopen_after_a_reconnection)
{
    using namespace lynx::prefs;
    MidiPrefs prefs;
    MidiInputPrefs off;
    off.name = "Pads";
    off.enabled = false;
    prefs.setInput (off);

    // start-up: the keyboard is listed but its opening failed (not ready yet): it is opened at the next check
    const std::vector<MidiPort> both { { "usb#kbd", "Keystation 49" }, { "usb#pads", "Pads" } };
    auto plan = planMidiInputs (prefs, both, { "usb#kbd", "usb#pads" }, {});
    CHECK (plan.open == std::vector<std::string> { "usb#kbd" } && plan.reopen.empty() && plan.close.empty());

    // open and still there: nothing to do (a disabled device stays closed)
    plan = planMidiInputs (prefs, both, { "usb#kbd", "usb#pads" }, { "usb#kbd" });
    CHECK (plan == MidiInputPlan {});

    // the keyboard was not listed at the last check (unplugged, or the system listed it late) and is still
    // "open" with its old handle: reopen it so it plays again
    plan = planMidiInputs (prefs, both, { "usb#pads" }, { "usb#kbd" });
    CHECK (plan.reopen == std::vector<std::string> { "usb#kbd" } && plan.open.empty());

    // a device turned off in the settings while open is closed
    plan = planMidiInputs (prefs, both, { "usb#kbd", "usb#pads" }, { "usb#kbd", "usb#pads" });
    CHECK (plan.close == std::vector<std::string> { "usb#pads" } && plan.open.empty() && plan.reopen.empty());

    // a device that is gone is left alone
    plan = planMidiInputs (prefs, {}, { "usb#kbd" }, {});
    CHECK (plan == MidiInputPlan {});
}

TEST (bugs131_pattern_length_is_respected_on_the_first_recording_in_song_mode)
{
    // an empty pattern set to 2 bars, an empty Playlist, Song mode: the song (what the transport loops) is 2 bars
    auto s = lynx::Song::makeDefault();
    const auto bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.patterns[0].notes.clear();
    s.playlist.clear();
    s.mode = lynx::PlayMode::song;
    CHECK (s.songLength() == bar); // automatic length: 1 bar
    s.patterns[0].lengthTicks = 2 * bar;
    CHECK (s.songLength() == 2 * bar);
    CHECK (lynx::playbackLoop (true, 0.0, 0.0, (double) s.songLength()).end == (double) (2 * bar));

    // a note recorded on bar 2 stays on bar 2 of the song and the clip placed for the take is 2 bars long; FL places
    // the take's clip from the bar of its first note (round 168k, REA_rodada165z-b fh2–fh5), so it starts at bar 2
    lynx::NoteRecorder recorder;
    recorder.begin (s.patterns[0].effectiveLength (s.meter()));
    recorder.add (s, { (double) (bar + lynx::ppq), 0, 60, 0.8f, true });
    recorder.add (s, { (double) (bar + 2 * lynx::ppq), 0, 60, 0.0f, false });
    auto notes = recorder.takeFinished();
    CHECK (notes.size() == 1 && notes[0].start == lynx::ppq && notes[0].length == lynx::ppq);
    CHECK (recorder.songOrigin() == bar && ! recorder.originFromClip());
    std::vector<int> cleared;
    lynx::commitNoteBatch (s, recorder, std::move (notes), lynx::RecordMode::overdub, {}, cleared, true, true);
    CHECK (s.playlist.size() == 1 && s.playlist[0].start == bar && s.playlist[0].length == 2 * bar && s.songLength() == 3 * bar);
    CHECK (s.playlist[0].start + s.patterns[0].notes[0].start == bar + lynx::ppq);
}

TEST (bugs131_pattern_clips_follow_their_pattern_length)
{
    auto s = lynx::Song::makeDefault();
    const auto bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.patterns[0].notes.clear();
    s.patterns.emplace_back().name = "Pattern 2";
    s.playlist.clear();
    lynx::Clip auto1 { 0, 0, 0, bar, 0 };       // auto-sized clip of pattern 1
    lynx::Clip auto2 { 0, 0, 2 * bar, bar, 0 }; // another one
    lynx::Clip cut { 0, 1, 0, bar / 2, 0 };     // resized by hand: keeps its length
    lynx::Clip offset { 0, 2, 0, bar, bar / 4 }; // starts inside the pattern: keeps its length
    lynx::Clip other { 1, 3, 0, bar, 0 };       // another pattern
    s.playlist = { auto1, auto2, cut, offset, other };
    CHECK (s.songLength() == 3 * bar);

    const auto before = lynx::patternLengths (s);
    s.patterns[0].lengthTicks = 2 * bar; // "Length > 2 bars"
    lynx::followPatternLengths (before, s);
    CHECK (s.playlist[0].length == 2 * bar && s.playlist[1].length == 2 * bar);
    CHECK (s.playlist[2].length == bar / 2 && s.playlist[3].length == bar && s.playlist[4].length == bar);
    CHECK (s.songLength() == 4 * bar); // the song now ends after the second clip's 2 bars

    // recording past the end of an automatic-length pattern grows it and its clips
    const auto before2 = lynx::patternLengths (s);
    lynx::Note late;
    late.start = 2 * bar + lynx::ppq;
    late.length = lynx::ppq;
    late.channel = 0;
    s.patterns[1].notes.push_back (late);
    lynx::followPatternLengths (before2, s);
    CHECK (s.playlist[4].length == 3 * bar);

    // a pattern added or removed by the edit: nothing is guessed
    auto copy = s;
    const auto before3 = lynx::patternLengths (copy);
    copy.patterns.emplace_back();
    copy.patterns[0].lengthTicks = bar;
    lynx::followPatternLengths (before3, copy);
    CHECK (copy.playlist[0].length == 2 * bar);
}

TEST (bugs131_song_mode_loops_back_to_the_start_at_the_song_end)
{
    auto s = lynx::Song::makeDefault();
    const auto bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.mode = lynx::PlayMode::song;
    s.playlist = { lynx::Clip { 0, 0, 0, bar, 0 }, lynx::Clip { 0, 1, 3 * bar, bar, 0 } };
    CHECK (s.songLength() == 4 * bar);

    // without a loop region the transport loops over the whole song; with one, over the region; Pattern mode: none
    auto loop = lynx::playbackLoop (true, 0.0, 0.0, (double) s.songLength());
    CHECK (loop.active() && loop.start == 0.0 && loop.end == (double) (4 * bar));
    loop = lynx::playbackLoop (true, (double) bar, (double) (2 * bar), (double) s.songLength());
    CHECK (loop.start == (double) bar && loop.end == (double) (2 * bar));
    CHECK (! lynx::playbackLoop (false, 0.0, 0.0, (double) s.songLength()).active());

    // hosted plugins see the position within the song (Song mode) or within the pattern (Pattern mode)
    CHECK (lynx::hostPositionTick (s, (double) (4 * bar + 10), s.songLength()) == 10.0);
    s.mode = lynx::PlayMode::pattern;
    const auto patternLength = (double) s.patterns[0].effectiveLength (s.meter());
    CHECK (std::abs (lynx::hostPositionTick (s, patternLength * 3 + 5.0, s.songLength()) - 5.0) < 1e-9);
    s.mode = lynx::PlayMode::song;

    // a note held across the song's end, recorded while the song loops, keeps its played length
    lynx::NoteRecorder recorder;
    recorder.begin (bar);
    recorder.add (s, { (double) (4 * bar - lynx::ppq), 0, 60, 0.8f, true });
    recorder.add (s, { (double) lynx::ppq, 0, 60, 0.0f, false }); // after the wrap
    const auto notes = recorder.takeFinished();
    CHECK (notes.size() == 1 && notes[0].length == 2 * lynx::ppq);
}
