# Round 169d — issue #32 follow-up 2 (medievilmusic): stuck notes

Comment of 2026-11-03 19:10 UTC with a 24 s video (`/mnt/e/lynx-work/issue32d/video.webm`, from
https://youtu.be/C5A_HYvjIFQ) and the project `Trance V1.lynx` (`/mnt/e/lynx-work/issue32d/zip/`). Worktree
`lynx-wt-issue32d`, branch `feature/issue32d` from `origin/main` c50aeb49 (0.1.23 + round 169c). Windows checks in
`C:\Users\kauan\lynx-test\issue32d\` (f = FL Studio 2026, l = LYNX main c50aeb49, x = this branch).

**Version in the video:** no version in the title bar; the comment (19:10 UTC) came before 0.1.23 was published
(22:55 UTC), so it is 0.1.22 again: without round 169c's note offs for hosted synths.

**The video, frame by frame (2 fps) and its sound level (0.5 s windows):** Pattern 2 of the project plays on
**Invader 2** (a VST3, channel 6; the native "Synth" channel has no notes). Each Stop (2.5 s, 8.0 s, 12.0 s, 20.0 s) is
followed by 2–3 s of sound at about a third of the playing level with the transport at 1:1:1 and POLY 1 (frame at
0.3 s: stopped, POLY 1); the sound ends when the reporter clicks a key of the Piano roll's keyboard (4.0 s, 16 s: the
key lit), i.e. the click's note on / note off of that key released the hung voice. That is 169c's point 1 (JUCE drops
CC 123 / 120 for a VST3 without MIDI mapping: on Stop the plugin was never told to stop its notes), fixed in 0.1.23.

| Reporter's words | FL evidence | LYNX before (main c50aeb49) | LYNX now |
|---|---|---|---|
| "after play/stop several times the last played note of any synth keeps sounding until I click that note in the Piano roll" | FL sends each plugin All Notes Off on Stop ("Send All Notes Off when playback stops", REA_rodada167j §3) | the 0.1.22 video = round 169c point 1 (fixed in 0.1.23: each held key of a hosted synth gets its own note off on Stop / jump). [LOG] `main.log.1`: on main, 0 notes left sounding after Stop | unchanged |
| "while editing notes during playback some notes randomly start voicing without being clicked" | [SHOT] f13 → f16 (FL 2026, 3x Osc, one C5 note of almost a bar, Pattern mode playing): the note is deleted at 1:04; at 1:07 and 1:11 POLY = 1, the scope shows the wave and the C5 key is lit; at the next pass (1:09 of bar 1 again) POLY = 0. FL plays a note it started to its end, even when it is deleted meanwhile | the schedulers emit each note off from the notes as they are *now* (`schedulePatternLoop`, `scheduleClip`): a note deleted, moved, or made shorter so that its end is behind the play head, while it sounds, never gets one and hangs until Stop. [LOG] `main.log.1` (test synth): the long note (3000 ticks = 1.44 s) deleted while sounding lasted **202 846 samples (4.6 s), until Stop**. [SHOT] `ln.png` (native Synth): its C5 deleted while sounding stays lit two passes later, until Stop | `ScheduledNotes` (core/Scheduler.h) keeps each started note's channel, key and end (the scheduler gives each note on its length, `NoteEvent::lengthTicks`); the note off at that end ends it; a note whose off does not come gets one there (+2 samples); any other note off of the scheduler (a note edited in behind the play head, or made shorter / longer while it sounds) is dropped, so it cannot end another voice of the key. Native and hosted instruments alike (it is the renderer's). Tests `issue32d_*` |
| "same when adding patterns in the Playlist while playing" | [SHOT] f17 → f20: a new pattern added (the "+" of the pattern selector) while the C5 note sounds: FL switches to Pattern 2, the note still sounds (POLY 1, key lit) and ends (POLY 0); nothing hangs | Pattern mode: the new pattern is the one that plays, so the old pattern's sounding notes never get their offs (same cause). Song mode: a clip moved / deleted under the play head, the same | the old notes end at their ends (test `issue32d_a_new_pattern_while_playing_leaves_no_note_hanging`) |
| "maybe note reset, polyphony, or PDC not killing the last note on stop" | — | PDC delays audio only (the channel's and the inserts' delay lines), never note events: a note off is never late. Polyphony: the channel's Max polyphony / Mono act on the note path, after the schedulers: unchanged | no change needed |
| Piano roll previews (coordinator: "make sure preview note-offs always arrive") | — | `HeldNotes` (core/HeldNotes.h) pairs every preview note on with one note off however the gesture ends (mouse up, drag to another key, the window hidden, the editor destroyed, channel switched); a Stop / jump releases a hosted synth's held keys (169c), a native engine's notes (allNotesOff) | unchanged; noted: `AudioEngine::sendLiveNote` ignores a full live queue (256 events), which can only happen if the audio thread stops reading it |

## Playlist cursor (comment of 2026-11-03 23:32, added to this round)

> "You can notice in the video how the cursor in the sequencer is clicking on one place but the playback triangle is
> reacting on an earlier spot. And when i use the brush or pen tool this can cause problems sometimes by adding some
> patterns in places i didnt want and later on playback they sound doubled (Louder). I didnt mean the ctrl+v"

The video is the round 169c one (`/mnt/e/lynx-work/issue32b/video.webm`, 0.1.22): 30–40 s, ruler clicks while
playing with "Line (follows zoom)" zoomed out to ~9 px per bar (frame at 31 s: the hand at x ≈ 988, the triangle
at 961 = bar 33, i.e. bars before the pointer).

| Item | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| Ruler click → song position | [SHOT] f32–f34 (FL 2026, main snap Bar, 60 px per bar): clicks at bar 5.83 and 5.5 → **6:01:00**, at 5.33 → **5:01:00**: the nearest grid line | `seekTo` used `snap()` = the line at or before the click (`snapDown`); "Line (follows zoom)" at that zoom is 2 bars (`screenStep`, lines ≥ 16 px), so the triangle landed up to two bars before the pointer | `PlaylistComponent::snapLine`: the nearest line (`noteedit::snapRound`; whole bars by `barAt`, so time signatures hold; "Nearest" keeps the clip edges). Alt = no snap as before. Test `issue32d_a_ruler_click_goes_to_the_nearest_grid_line` |
| Brush over existing clips | [SHOT] f35 → f37 → f38 → f39: Pattern 1 painted at bars 2–6 on Track 1; a stroke from bar 1 across them adds only bar 1's clip; right-click on bar 3's clip deletes it and nothing is under it — FL does not stack a copy | `Drag::paint` pushed a copy every clip length along the stroke whatever was there: a stroke over clips of the same pattern stacked copies on them → the pattern played twice ("doubled (louder)") | a copy that would overlap a clip of the same source (pattern / audio channel / automation) on that track is not painted; the stroke goes on after it (`noteedit::paintWouldStack`). Other patterns may still overlap (FL allows it). Test `issue32d_the_brush_does_not_stack_a_copy_on_the_same_clip` |
| Pen | not checked in FL this round | a click places the clip at the grid line at or before the pointer (the cell under it); a click on a clip grabs it (never a copy on it) | unchanged; the stacked copies came from the brush |

## Windows check

Branch build `build-win\LYNX.exe` (= `LYNXfix.exe`), `trance-ts.lynx`, fresh profile from `proftpl`:

| Check | main c50aeb49 | branch |
|---|---|---|
| test synth: long note (3000 ticks) deleted while it sounds (`main.log.1` / `fix.log.1`) | lasts 202 846 samples (4.6 s): until Stop | lasts **69 233 samples** = its 3000 ticks at 130 BPM, 48 kHz; 25/25 notes on/off, 0 left sounding |
| native Synth: C5 deleted while it sounds ([SHOT] `ln.png` / `xn.png`) | its key stays lit two passes later, until Stop | lit right after the delete (as in FL), off at the next passes |
| Playlist ruler, House project, "Line (follows zoom)" at 8.25 px per bar (2-bar lines), click at x = 794 ([SHOT] `rm.png` / `rx.png`) | 19:1:1, the triangle ~16 px (two bars) left of the click | **21:1:1**, the triangle at the click |
| brush: stroke A bars 3–9 on Track 6, stroke B from bar 1 to 11 across it (saved file, `brushcount.py`) | 18 clips: a second Pattern 1 clip on each of bars 3–9 | 11 clips (bars 1–11), none stacked |

## Where I stopped

- Done: video analysis; FL evidence (one FL session, closed with "No" to save); main reproduction with the LYNX Test
  Synth (`tools/testplugin`) and the native Synth (`trance-ts.lynx`: Invader 2 → test synth, long notes added).
- Done too: the branch build's deletions (above); FL session 2 (ruler and brush, closed with "No").
- Done too: Playlist ruler / brush check (main vs branch, above); build-local.
- Next: merge origin/main, `coord/train.sh issue32d`.
