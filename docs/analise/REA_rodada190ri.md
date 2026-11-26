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
| ... the box (coordinator review, FL 2026 live, t1_before.flp) | Not a window: FL's small in-place name box (TNameEditForm "Semitones", 202 x 51 px), its bottom-right corner 10 px past the mouse (mouse 231,231 -> box 39,190-241,241; the F2 name box the same way); the field takes any text ("a-1.5x+3" typed as is); ✓ / ✕ under it | raw/t2, t3 |
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
- **Transpose:** `transposePatterns` (core/RackOps). LYNX's keys end at 127 (FL: 131). The value is asked in FL's in-place box (ui/InlineTypeIn.h, shared with the video player's time box), placed as FL places it; LYNX's field takes a signed whole number (anything else does nothing).
- **Scale box:** shrinks to icon and arrow before anything else of its row is given room; right-click in its menu chooses and keeps it open (FL).
- **Alt + tool letter:** `KeyMap::findToolWithAlt`. Alt+C as a second key of "Clone the current pattern" is gone (not FL's).
- **Copy names:** core/CopyName.h, used by patterns, channels, unique clips, audio clip channels, Edison and Slicex regions, cloned tracks.
- **Make unique as sample:** engine/UniqueSamples.h: a worker with progress and Cancel (a cancelled run deletes its files), one undo step.
- **Imported Windows paths on Linux / macOS:** the project data folder is used only when it exists here; also "Record to" with the dry take, Newtone's two save names.
- **F12** closes the Search palette.
- **#272:** core/StepSlide.h. An .flp slide note of length 0 was imported one step long (held on generators): it looked and glided a step
  too long. Now one tick, held, drawn as FL's pill, written back as length 0. Projects saved by an older LYNX keep what they play. The
  .flp export wrote every slide note of one step on a sample channel as length 0 (it would jump in FL): it keeps its length now.

## Second batch (kainproctor and yannikthegenius, 25 Nov)

FL 2026 live, synthetic projects saved by FL in the same sessions: `flmark/m1.flp` (markers of every kind), `m2.flp` (markers a 1,
b 3, c 7, d 9 with clips at 1, 3, 4, 6, 7, 9), `m2_right.flp` / `m2_left.flp` (what Move content right / left on b saved).
Shots in `raw/` stay local (FL's title bar shows the licence owner).

| Question | FL 2026 | Evidence |
|---|---|---|
| Two skip markers in a row (k3) | A skip that lands on another skip's position goes on to that one's target (1–2 → 7 in m1); the chain is followed at once, also in the render | m1.flp, raw/mk* |
| A skip that lands on a pause marker | Playback stops there (11:01:00); Play goes on from there, not from the song start | m1.flp, raw/mk* |
| Marker loop after a skip | 17–18 repeats as when reached without the skip | m1.flp, raw/mk* |
| Right-click on a marker's tag (k4/k5) | Add marker..., Add time signature..., Place loop, header "<name> at 8:01:00", Delete, Rename..., (separator) Move content left / right, header "Action", radios None, Start, Loop, Marker loop, Marker skip, Marker pause, Time signature, Start recording, Stop recording | raw/mg3 |
| Left-click / drag on a tag | A click does nothing; a drag moves the marker (snapped) | raw/mg0–mg2 |
| Move content right on b | b's section (3–6) and c's (7–8) change places with their clips and markers: c at 3, b at 5 | m2_right.flp |
| Move content left | the same with the section before (from the previous marker, or bar 1) | m2_left.flp |
| Track headers, several selected (k6) | Ctrl+Shift+click adds tracks to the selection (Shift+click renames); the header menu's Group with above track, Ungroup, Delete (one question with the list: "Are you sure you want to delete 3 selected tracks?"), Move up / down (as a block, past a whole group), Reset, Size, Mute all clips, Random color (a colour per track) act on every selected track | raw/g* (gw: the delete question) |
| ... right-click on a track that is not selected | it alone becomes the selection | raw/gt* |
| A shortened channel name (#301) | The button shows "BWB WA..RT (22)"; hovering it, the hint bar shows the whole name (two lines) and the plugin ("3x Osc") on the right | fl301/long.flp, shots/18-301-fl-hover.png |
| Long hints (#296) | FL's hints are short; the hint bar wraps to a second line; nothing longer is shown | FL's own hints as captured for 190jo |

## Second batch: LYNX before / now

- **Skip chains (item 12):** `markers::compile` followed only the first skip. Now a jump that lands on another jump's start goes on
  to its target, and one that lands on a pause becomes "jump then pause" (`MarkerJump::jumpThenPause`). A pause kept position 0,
  so Play after a pause restarted at the song's start: it keeps the marker's position now. The export's spans skip the same way.
- **Marker menu (item 13):** `PlaylistComponent::markerMenu`, FL's items first; LYNX's ruler menu stays one level down ("Ruler").
  The tag's hit area takes 3 px above it too. Left-drag moves the marker in one undo step; a click without a drag still goes to it
  (LYNX). `markers::moveContent` / `canMoveContent` (core/MarkerPlan).
- **Track menu on a selection (item 16):** the menu used the clicked track for Group, Ungroup, Delete, Move, Reset, Size, Mute all
  clips and Random color (one colour for all). Now the selection, each in one undo step; `noteedit::moveTrackBlock`; FL's
  question before deleting more than one.
- **DAWproject export (item 11):** an exception in the writer was never caught and no window came ("Doesn't work"). Every failure
  shows a message now. `--export-dawproject <in> <out> [--stress]` writes without the window (see HANDOFF for the stress numbers
  and the schema check).
- **Settings search (#247, item 14):** `TabbedComponent::setCurrentTabIndex` calls `toFront (true)`, which takes the keyboard from
  the search box when a match is on another page. The box gets the keyboard, caret and selection back at once. As a net for every
  text field: `ui::keyfocus::typeIntoFieldJustLeft` — for 400 ms after a field lost the keyboard without a mouse press, a plain
  character goes back into it instead of running a shortcut.
- **Hint bar (#296, item 15):** a hint that is cut shows whole in the bar's bubble; the bar brings back a cut hint of the last 4 s
  while the pointer is on it. US English for what the app shows, through the English display table (the tr() keys stay):
  colour(s/ed), centre(d), licence, grey(ed/s), analyse(d/ing), normalise, favourite(s), cancelled, neighbour(s), equaliser,
  labelled (109 texts). "Dialogue" (spoken dialogue, ADR) stays.
- **Value bubbles (#299, item 17):** JUCE closes a slider's bubble on its mouse-exit or hover timer; a page that scrolls under a
  still mouse sends neither. `theme::watchSliderPopup` (from the look and feel, as the bubble is made) closes it once its knob is
  not under the pointer, is hidden, or its window loses the focus; never while it is dragged.
