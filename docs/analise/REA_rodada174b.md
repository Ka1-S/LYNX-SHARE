# Round 174b — issue #81 (Francisc0422): ghost notes when notes are moved while playing

> "When moving notes up and down pitches while a pattern is playing it makes 'ghost' notes that are sustained until
> the end of the pattern" (0.1.23-alpha, Windows 11, NVIDIA HD Audio)

Evidence in `/mnt/e/lynx-work/issue81/` (video, frames, shots, logs) and `C:\Users\kauan\lynx-test\issue81\`.
Worktree `lynx-wt/issue81`, branch `wip/issue81` from `origin/main` dea61794 (0.1.24).

## The video (19 s, 60 fps, frames at 1 fps)

- LYNX 0.1.23 in Song mode. Pattern 2 sits on Playlist track 2. Its piano roll is open on the default **"Synth"**
  channel (the native generator) with a 16-note melody of staccato sixteenths in one bar.
- All 16 notes are selected and moved up and down a semitone at a time while the song plays. The mouse never
  moves, so the moves come from the keyboard (Shift+Up/Down).
- The piano roll keyboard lights more and more keys over time (t = 8 s: B5, G#5, F#5, D5, C#5 and C5 at once,
  while only one sixteenth sounds at a time). Those are voices whose note off never came.

## Cause (0.1.23)

The schedulers emitted each note off from the notes as they are *now* (`schedulePatternLoop`, `scheduleClip`).
A note moved to another key while it sounds got its note off for the **new** key, so the old key never got one and
kept sounding. It stopped only when Stop was pressed or when a later note off of the same key came by chance.
Moving the selection up and down, as in the video, made that happen often.

Round 169d (issue #32 follow-up 2, `ScheduledNotes` in core/Scheduler.h) is in **0.1.24**. It ends each started
note at the channel, key and end it started with, and drops the scheduler's note off for the edited key. It was
written for notes deleted or moved while they sound, and it covers this issue too. The tests below pin the
reporter's exact gestures. No engine change was needed.

## FL evidence (FL Studio 2026.1.7, 3x Osc, one note of 3 beats in a 1-bar pattern, Pattern mode playing)

| Gesture | [SHOT] | FL |
|---|---|---|
| Shift+Up on the selected sounding note (C5 → D5) | `shots/fl-shiftup-b1-b3-b5-b7.png` (b1 1:04:05, b3 1:09:08, b5 1:15:01, b7 next pass 1:03:20) | C5 stays lit after the move and goes dark after the note's original end (1:13). D5 is not lit until the next pass, then D5 plays. POLY is 1 throughout (`shots/fl-poly-b-c-d.png`) |
| Mouse drag of the selected sounding note (D5 → C5) | `shots/fl-drag-c1-c4.png`, `fl-drag-c5-c7.png`, `fl-drag-clock.png` | the same: the old key D5 sounds to 1:13, then nothing, and C5 plays from the next pass |
| Mouse drag of an **unselected** sounding note (D5 → C5) | `shots/f_z.png`, `f_t.png` | the old key D5 keeps sounding to its end. While the mouse is down POLY = 2, so the grabbed note is previewed, as when stopped (e1/e2: the preview of a clicked note) |

So FL keeps a started note's voice to its original end, whatever the note became. The new key plays from the next
pass. Dragging a note with the mouse previews it while the button is down, during playback too. LYNX does all of
this: `ScheduledNotes` keeps the old voice, and the piano roll's drag preview (`previewOn` while dragging, ended by
`HeldNotes` on mouse up) previews the note. Keyboard moves of a selection of more than one note make no preview in
either.

## Windows check (real time, the reporter's gesture)

The project is `vst3.lynx` / `native.lynx`, made by `/mnt/e/lynx-work/issue81/tools/mkproj.py`: the video's melody
in Song mode. The piano roll is open, Ctrl+A, Play, then 12 Shift+Up/Down at 330 ms intervals. Builds:
`LYNX0123.exe` = c50aeb49 (0.1.23 + 169c, before 169d) and `LYNX0124.exe` = the 0.1.24 landing build.

| | LYNX Test Synth (VST3), its log through `tools/ghosts.py` | native Synth |
|---|---|---|
| 0.1.23 | 74 note ons, 77 offs, 5 offs of a key that was not sounding (`on 77` … `off 78`), **up to 4 voices at once**, 658 of 889 playing blocks with more than one voice | POLY 1 and a steady tone in the scope through the melody's gaps (`shots/n0123b.png`, `npoly.png`) |
| 0.1.24 | 73 ons, 73 offs, 0 stray offs, **at most 1 voice**, 0 blocks with more than one | covered by the render test below |

## Tests (`tests/Issue81Tests.inc`)

- `issue81_moving_the_whole_melody_up_and_down_while_the_song_plays_leaves_no_ghost_notes`: the video in Song
  mode. 12 moves of the whole melody, each one inside a sounding note. Nothing sounds in any sixteenth's gap, and
  every note on has exactly one note off of its own key.
- `issue81_a_sounding_note_moved_to_other_keys_keeps_its_voice_to_its_end`: the FL shots. The note goes up, down,
  back and up again while it sounds. Its voice ends once, at its original end. There is no note on for the new keys
  until the next pass.
- `issue81_a_playlist_clip_moved_while_its_notes_sound_leaves_none_hanging`: a Playlist clip moved to another track
  and later in the song while its note sounds.
- `issue81_render_the_native_synth_falls_silent_after_a_moved_note_s_end`: the default Synth, rendered. The output
  is silent between the old note's end (plus its release) and the next pass.

To check that the tests catch the bug, the renderer was built once with `scheduledNotes->process` taken out (the
0.1.23 behaviour). All four tests then fail, with 12 failed checks, the native render among them
(`/mnt/e/lynx-work/issue81/build-negative.log`).

The tests use the renderer with a recording instrument, so they cover native generators, samplers and hosted
VST2 / VST3 / CLAP plugins alike: `ScheduledNotes` runs before any instrument gets its events. The VST3 Windows
run above checks a hosted plugin end to end.
