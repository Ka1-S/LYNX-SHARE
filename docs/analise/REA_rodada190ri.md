# Round 190ri — community batch of 0.1.32 (nynsai's feedback video, Discord 11-25, and open items of sweep 190re)

FL evidence: live checks of FL Studio 2026 (v26.1.7, build 5654), 25 Nov 2026, two short GUI-locked sessions, on a copy of
"GMS.flp" (FL's render tests) saved by FL in the same sessions: `/mnt/e/lynx-work/community_190ri/fl/*.flp`, shots `raw/`.
FL's audio settings were not touched. The Alt keys were sent with the typing keyboard button on (FL's default) unless said.

| Question | FL 2026 | Evidence |
|---|---|---|
| Alt+R, an insert armed | The window "Rendering mixer track": Mode (Pattern / Full song, following the PAT / SONG switch), Tail, WAV bit depth, Stereo, Resampling, HQ for all plugins, Disable maximum polyphony, Dithering, Upload, Render minimized, Start. No format choice, no name, no file dialog | n03, p02 |
| ... the file | `Documents\Image-Line\FL Studio\Audio\Recorded\<project>_<yyyy-mm-dd hh-mm-ss>_<insert>.wav` (the time of Start) | files |
| ... afterwards, song rendered | A new audio clip channel at the end of the rack ("---", named after the file), a clip of the whole file from bar 1 on the **first Playlist track with no clip**: with pattern clips on tracks 1 and 4, Insert 5 went to Track 2 and the Master's render to Track 3. Not the track of the insert's number | q02, r02 |
| ... afterwards, pattern rendered | Only the channel: the Playlist's clip source is the new clip and the picker lists it; nothing is placed. In nynsai's video the render was a pattern's, and he placed the clip on Track 2 himself (frames 60-64 s) | o01-o04, video |
| Shift+Alt+R (Render selected) | The same, clip included; the arm states come back | q04, r02 |
| Armed disk recording | Each loop pass: an audio clip, muted, on the first free tracks (Insert 5: 5, 6, 7; Insert 10: 8, 9), grouped; the channel goes to the insert. LYNX already does this (190ev / 177c) | v01, w03 |
| Transpose... (Patterns menu, Picker's pattern menu) | A value box "Semitones", empty; every channel's notes of the picked patterns, steps too (a step note got key 62 and stayed a step); each note clamped on its own (129 + 2, 131 + 2 → 131) | e04, t1_before / t1_after.flp |
| Transpose selected... (Channel rack menu) | The same box; the selected channels in the current pattern only | f02, g00, h01 |
| Scale button of the Piano roll (note icon) | Left-click: Snap to scale on / off. Right-click: the Root note \| Scale menu (roots greyed on "Black notes") | c01, c03 |
| Alt+C | Playlist: **Slice tool** (Alt+D Delete, Alt+E Select, Alt+B Paint, Alt+Y Playback, Alt+S Slip the same way; the letters alone play notes). Typing keyboard off: C = Slice, Alt+B / Alt+C nothing. Piano roll: change colour (Alt+D there picks Delete). Channel rack: clones the channels. Cloning a pattern is Shift+Ctrl+C | b01, k1-k10 |
| Clone names | "foo" → "foo #2" → "foo #3" → "foo #4"; "foo #2" cloned with #3 and #4 there → "foo #5", right after it. A pattern with no name is shown by its position: "Pattern 1" → "Pattern 2" → "Pattern 3", the later ones moving down. Channels: "GMS" → "GMS #2" → "GMS #3" → "GMS #4" | i01, n1, n2, j01 |
| Make unique as sample, clip menu | The clicked clip only, "Save sample selection" asks for the file ("<sample> - Part_1"); manual: Shift skips the question | y02, z00, z01 |
| ... Edit menu, 3 selected clips that play the same | No question; one file each (Part_2, _3, _4) | ab1 |
| Slide notes of length 0 (#272) | Drawn as a pill, 7 x 17 px on an 18 px row, rounded 3 px, centred on the start, no name, no slide mark, the same at every zoom (plain notes of length 0 are drawn so too); the sounding notes move at once (3x Osc and Sampler: 130 -> 261 Hz at the slide's tick; a slide of 12 / 24 ticks takes its 12 / 24). FL 2026 with the reporter's project (local study only; 20.8.4, PPQ 96, flags 0x4008, 134 of 693 slide notes of length 0) and a synthetic one (syn272.flp) | k4, z1, sy1; fl272/fl/*.wav |

## LYNX before / now

- **Render armed / selected tracks:** the Export window kept the last export's format and name and asked for a file; an MP3 render was
  never placed (`registerBasicFormats` reads no MP3), Render selected never placed anything, and the clip went after the last used
  track. Now: the window opens on WAV, "Start" writes to the recordings folder under FL's name, no file dialog (Folder..., the
  name, the other formats and tails stay for who wants them); `mixerops::placeRenderedFiles` uses the first track with room,
  a pattern render only adds the channel, the Playlist's tools paint it. Two inserts of one name get "_2"; "$track" of the full mix is "Master".
- **Transpose:** `transposePatterns` (core/RackOps). LYNX's keys end at 127 (FL: 131).
- **Scale box:** shrinks to icon and arrow before anything else of its row is given room; right-click in its menu chooses and keeps it open (FL).
- **Alt + tool letter:** `KeyMap::findToolWithAlt`. Alt+C as a second key of "Clone the current pattern" is gone (not FL's).
- **Copy names:** core/CopyName.h, used by patterns, channels, unique clips, audio clip channels, Edison and Slicex regions, cloned tracks.
- **Make unique as sample:** engine/UniqueSamples.h: a worker with progress and Cancel (a cancelled run deletes its files), one undo step.
- **Imported Windows paths on Linux / macOS:** the project data folder is used only when it exists here; also "Record to" with the dry take, Newtone's two save names.
- **F12** closes the Search palette.
- **#272:** core/StepSlide.h. An .flp slide note of length 0 was imported one step long (held on generators): it looked and glided a step
  too long. Now one tick, held, drawn as FL's pill, written back as length 0. Projects saved by an older LYNX keep what they play. The
  .flp export wrote every slide note of one step on a sample channel as length 0 (it would jump in FL): it keeps its length now.
