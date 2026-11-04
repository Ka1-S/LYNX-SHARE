# Round 170d — recording timing: the chord on the downbeat after the count-in, played-note latency, Capture MIDI

Source: coord/busyworks-0.1.24.md item #3 (Busy Works Beats, "Making a Beat on the 'Open Source' FL STUDIO", LYNX
0.1.22/0.1.23 on macOS, 02:04–02:53). Research of the other DAWs: coord/recording-research-0.1.24.md.

## Reporter's words (transcript)

- 02:07 "last time I remember, it stopped recording after two bars, so let's see if that's still a thing. Also, I need
  the pre-count on."
- 02:32 "it looks like it didn't get the first chord I recorded … that's been an issue even with FL Studio … They just
  don't record that first chord. So whoever figures out a way to record, like Logic, how you can record before the
  downbeat, that is super needed in these DAWs."
- 02:52 "I recorded a little bit after the beat, just so it would record it."
- Frame 02:33 (`/mnt/e/lynx-work/rec170d/f0233.jpg`): Song mode, 140 BPM, the take is a Pattern 1 clip from bar 1 to
  bar 9 — eight bars recorded and kept (the two-bar bug of 0.1.21 is gone, round 168k); the first chord missing.

## Items

| # | Reporter / user | Reference behaviour | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 | chord on / a hair before the downbeat after the pre-count is lost | FL: same flaw (the reporter). Logic keeps count-in notes at their real time (before the region); Cubase "MIDI Record Catch Range" catches notes just before the record start | MIDI and typing-keyboard notes were queued only while `transport.playing`; the count-in is not playing → dropped. A take only began with playback | notes during the count-in are part of the take (`AudioEngine::updateRecording`, the MIDI / typing paths). `catchEarlyNote` (core/Recorder): a note-on at most the **catch window** before the start goes onto the start (released before the start: its played length kept); earlier ones keep their real song position (Song mode, at/after the song start; before it: dropped), Pattern mode wraps them into the loop as its previous pass. Record menu › "Notes played just before the start go onto it": Off / 1/32 / **1/16 (default)** / 1/8 / 1/4 |
| 2 | sample-accurate MIDI timestamps | Pro Tools / Core MIDI: notes stamped at the interface | the tick was `estimatedPositionNow()` when the MIDI thread got to the message: last block's position + wall time since, clamped 0..100 ms, no latency | `recordTickFor (eventMs)`: the message's own time stamp (JUCE stamps MIDI input with `getMillisecondCounterHiRes`; a stamp more than 1 s off falls back to now) against the **live clock** the audio callback leaves at its end (the next block's musical position — count-in included — its time and tempo; `LiveClock`, `liveTickAt`) |
| 3 | "what you hear is what is recorded" | Pro Tools ADC / Cubase "ASIO Latency Compensation" / REAPER: move recorded MIDI back by the output latency | none | moved back by one output buffer + the driver's output latency (the figure the MIDI clock output already uses, MidiSyncIo); Record menu › "Played notes: compensate the output latency" (on) |
| 4 | audio record latency like Pro Tools / REAPER | REAPER: driver-reported latency + manual in/out offsets + loopback ping; Pro Tools: interface latency, H/W insert ping | already on main (round 163): driver input+output or the **measured** loopback round trip per device setup, manual offset (samples), Master PDC; Settings › Audio recording › Measure | unchanged; covered again by a loopback test (ping measures 731 samples, the placed take puts the click on the tick it was played) |
| 5 | Capture MIDI (LYNX extra) | Ableton Capture MIDI, Logic Capture as Recording, Cubase Retrospective Record | only FL's "Dump score log" (wall-clock, first note at the pattern start) | Record menu › "Capture MIDI…" (Ctrl+Shift+Alt+R): the score log entries now carry the transport position (when playing) and whether they were recorded; the last phrase not recorded (back to two bars of silence or a change of transport state) goes into the current pattern — played while playing: at the positions played, as a take (clip placed in Song mode); while stopped: from the playhead's bar (Song) / pattern start (Pattern) at the project tempo. Record quantize / overdub-replace apply; one undo step; a second capture takes only newer notes |
| 6 | "stopped recording after two bars" | — | fixed in 0.1.22 (round 168k) | re-checked: frame 02:33 shows 8 bars kept; unit test with 6 bars after the count-in and the engine test (`--countinrectest`, 7 bars) keep every bar |

## Code

- `src/core/Recorder.h/.cpp`: `LiveClock`, `liveTickAt`, `CountInCatch`, `countInWrap`, `catchEarlyNote`,
  `CaptureOptions/CaptureResult`, `capturePhrase`, `captureMidi`.
- `src/core/ScoreLog.h/.cpp`: `Entry::startTick`, `Entry::recorded`.
- `src/engine/AudioEngine.h/.cpp`: live clock atomics stamped at the end of the callback, `outputCompensationMs`
  (device start), `recordTickFor`, count-in catch per take (`startPlayback`), takes active during the count-in,
  `captureMidi`; RecordOptions `catchWindow`, `compensateOutput`.
- `src/ui/MainComponent.cpp`: record menu items, `record.captureMidi` action, settings keys `recordCatchWindow`,
  `recordCompensateOutput`. `src/core/KeyMap.cpp`: Ctrl+Shift+Alt+R. 10 new strings × 14 tables.
- `src/Main.cpp`: `LYNX --countinrectest report.txt` (engine end to end, stand-in device).

## Tests

- `tests/Rec170dTests.inc`: live clock (event stamp, output latency, clamp, stopped); catch rules (song / song start /
  pattern wrap / no count-in); the video's take (chord 15 ms early on bar 3 + a note a bar for 6 bars, all kept); window
  off (real times, origin bar 2); pattern mode (previous pass); Capture MIDI played / stopped / pattern / song /
  recorded entries skipped / once only; score-log dump unchanged; loopback audio take placement with the measured ping.
- `LYNX --countinrectest`: pass 1 finds the metronome's bar-3 click in the rendered output; pass 2 sends MIDI stamped at
  the times those samples are heard (stand-in device, 256 samples output latency + 512 buffer): every note must land
  within 2 ticks (~1 ms) of the tick it was heard at.

## Decisions for the user

1. Count-in notes earlier than the catch window: kept at their real position (Song mode), wrapped into the loop
   (Pattern mode), dropped before the song start. Alternative: snap every count-in note of the last beat onto the start.
2. Catch window default 1/16 note (107 ms at 140 BPM). Bigger (1/8)?
3. Capture MIDI while stopped keeps the project tempo (no tempo detection like Ableton's on an empty Set).

## Where I stopped

- Code + unit tests + CLI test written in ../lynx-wt-rec170d (feature/rec170d), wip pushed. Next: build-local, Windows
  build + `--countinrectest`, merge train, notes.
