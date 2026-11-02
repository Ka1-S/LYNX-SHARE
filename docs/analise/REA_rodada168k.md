# Round 168k — Song-mode MIDI recording folds into the first bars (the YouTube review, critical)

Source: docs/analise/REA_rodada168d-video.md, moments #7, #8 and #16 ("I recorded near eight bars, but it's stopping at
two bars … it just maxes out at two bars"; "it says two bars here, but visually it's showing four bars"; the pitch
wheel landing apart from the notes). Busy Works Beats, LYNX SHARE 0.1.21 on macOS. Nothing in the cause or the fix is
platform-specific (core recorder + the engine's take placement).

## FL evidence (one planned FL session, 2026-11-01 23:24–23:27)

FL Studio 2026 driven as a black box with real keyboard / mouse input (`keybd_event` / `mouse_event`), both locks
(`~/.cache/lynx-wingui.lock` + `~/.cache/lynx-fl.lock`), FL started once and closed by its own PID (Save changes → No).
Notes played with FL's own typing keyboard (already on: registry `KbToMIDIBtn=1`, layout "Layout Piano"), default
template (130 BPM, 808 Kick selected), Song mode, Record → "Everything" (= the user's `RecordingFilter2=15`, unchanged).
No FL setting was changed: the registry export before / after differs only in FL's own run counter, cloud timestamp,
recent-color list and the Channel rack window height, and those three were written back. Screenshots in
`C:\Users\kauan\lynx-test\rec168k\` (crops `c_*`, zooms `z_*`); scripts `drive.ps1`, `timed.ps1` (timed key down/up).

| # | setup | what was played | what FL did | shots |
|---|---|---|---|---|
| fl1 | Pattern 1 = one kick step, its clip on Track 1 bar 1 (a **1-bar song**); new empty Pattern 2 current; record armed; Play from 1:1 | nothing for the first 2.8 s | the song did **not** go back to bar 1: the counter read **2:08** at 2.8 s and **8:08** at 13.9 s — while recording, Song mode plays on past the song's end (linear), from the start of the take, before any note | `c_m0a.png`, `c_m1a.png` |
| fl2 | same take | one short note just before bars 3, 4, 5, 6, 7 and 8 (D4…B4), the last one **held through Stop** (~8:3, released 0.4 s after Stop) | a **Pattern 2 clip on Track 2 from bar 2 to bar 9**: it starts at the bar of the first note (that note fell at ~2:04:15, just before bar 3) and ends at the pattern's whole bars; every note sits **where it was played** (no folding to bar 1); the held note is kept, ending at the Stop | `c_m1b.png`, `z_m1b.png` |
| fl3 | Pattern 3 = one kick step (1 bar); its clip on Track 3 from bar 1 **stretched to bar 5** (4 bars, the clip repeats the 1-bar pattern) | notes at 2:1, 3:3 and 4:2 | the notes stay at 2:1, 3:3 and 4:2 of the clip; the Piano roll of Pattern 3 shows the kick at 1:1 and **D4 at the pattern's bar 2**: FL **grows the pattern** to the recorded position, it does **not fold** by the pattern's length; no new clip | `z_m2b.png`, `f12.png` |

Earlier evidence agrees: REA_rodada165z-b fh2–fh5 (automation recorded from 2.3 on an empty Playlist: a Pattern 1 clip
placed at Stop from bar 2, "the bar of the first move", to the pattern's whole bars; the song played on to 7.1).

## Item

| Reporter's words | FL evidence | LYNX before (0.1.21 … main e6e36fdd) | LYNX now |
|---|---|---|---|
| "I recorded near eight bars, but it's stopping at two bars … you can't record more than two bars" (#8: new empty pattern, Song mode, no clip of it under the playhead) | fl1, fl2: the take keeps its song positions, the clip goes from the first note's bar to the last recorded bar, the song plays on past its end while recording | `NoteRecorder::begin` froze the loop length at the empty pattern's 1 bar and `patternPositionForTick` folded every note outside a clip of the pattern `% loopLength` → 32 notes over 8 bars all in bar 1 (a note held over the bar line made it 2 bars: "maxes out at two bars"); the clip was placed where recording started; the song looped at its end while recording notes | Song mode: the take's **origin** = the bar of its first note (or, when that note is inside a clip of the pattern, that clip's pattern start); every note is stored at `song position − origin` (inside a clip: the clip's position); the clip is placed at the origin with the first batch and grows with the pattern to the last recorded bar; a pattern with a set length (Channel rack › Length) grows by whole bars to the notes past it; while a take records in Song mode without a loop region the song plays on past its end (`noteTakeLinear` → `liveInput.linearSong`) |
| "it says two bars here, but visually it's showing four bars" (#7: a 2-bar pattern whose clip was stretched to 8 bars, recorded into) | fl3: FL grows the pattern, no folding | notes past the pattern's length were stored past it: never shown in the Piano roll nor played (probe "8 B") | the notes stay at their clip position and the pattern grows to them (automatic length by itself, a set length by whole bars): 8 bars recorded → 8 bars stored, shown and played |
| held notes at Stop | fl2: kept, ending at the Stop | kept (to the last playing position) | unchanged, now covered by a test with the note past the song's end |
| "Like it just doesn't play my notes right" (#16, pitch wheel) | FL writes the wheel with the notes (REA_rodada165z-b, pattern events) | the wheel went to an automation clip at its real song position while the notes were folded into bar 1–2: they no longer lined up | the notes are at their real song positions, the wheel's automation clip too (both linear for the whole take): they line up. The storage difference (automation clip instead of pattern events in Song mode) stays as decided in 0.1.20 (REA_rodada165z-b) |

Not changed: Pattern mode (loops the pattern, positions within the loop); loop regions in Song mode keep looping —
a take whose first note is inside the region uses the bar of the region's start as its origin, so every pass lands
in the same bars (FL's "MIDI loop recording (song mode)" option is off here and was not measured). Round 167q's
`patternTickInClip` fold for recording (not on main yet when this round was written) is superseded for notes by fl3:
recording never folds.

## Code

- `src/core/Recorder.h/.cpp`: `takeOriginFor`, `songNotePosition`, `growForSongTake`, `commitNoteBatch` (the commit of a
  batch of a take, shared by the engine and the tests); `NoteRecorder` keeps the take's origin (set by the first note-on
  in Song mode) and a `linear` flag (no wrap of positions at the song's end).
- `src/engine/AudioEngine.cpp/.h`: `updateRecording` commits through `commitNoteBatch` (the clip at the origin instead
  of the take's start), `noteTakeLinear` for the whole Song-mode take (also used by the wheel / automation of the same
  take), the recording-marker check does not wrap a linear take.

## Tests

`tests/Rec168kTests.inc` (the probe's two cases first — they fail on main: 32 notes in bar 1 / notes past a 2-bar
pattern — then FL's measured cases fl1–fl3, the held note at Stop, a take continuing the clip it starts in, a set
length growing, a loop region keeping one origin, the wheel landing with the notes). `bugs131_pattern_length_is_
respected_on_the_first_recording_in_song_mode` now expects FL's placement (clip from the first note's bar).

## Where I stopped

- Done: FL session (fl1–fl3), code, tests written.
- Next: build the tests (mini harness `~/.claude/jobs/f8789b49/tmp/rec168k/mini/build.sh`, then `scripts/build-local.sh`
  with FL Data), the probe against the new code, the Windows build + GUI check of the user's flow (fresh profile), notes,
  land.
