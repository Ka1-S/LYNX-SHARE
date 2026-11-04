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

## Windows check

(filled after the branch build)

## Where I stopped

- Done: video analysis; FL evidence (one FL session, closed with "No" to save); main reproduction with the LYNX Test
  Synth (`tools/testplugin`) and the native Synth (`trance-ts.lynx`: Invader 2 → test synth, long notes added).
- Next: build-local + Windows build, the same deletions with the branch build, `coord/train.sh issue32d`.
