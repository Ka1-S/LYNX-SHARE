# REA round natives190kj — Newtone: FL's note analysis and FL's window

Round natives190kj (0.1.30), item 1 of the brief. Clean-room as every REA round: behaviour described in our own words,
no Image-Line or zplane code or data in the repository. Newtone's engine (`Newtone_Engine_x64.dll`, zplane's licensed
code) stays a **black box**: only its output is measured, by running FL's unmodified `Newtone_x64.dll` in round 73's EXEC
harness (`fx.c`): `dfile=` loads a sample (Dispatcher 18), the engine analyses it, `save=` writes the plugin state, whose
XML project holds the analysed notes (`Pitch_N`, REA_rodada73 §2 / REA_rodada95 §2).

Evidence markers: [EXEC FL] (FL's DLL in the harness), [MANUAL] FL's online manual (Newtone page), [SHOT] FL 2026 window
captures, [TEST]. Data: `/mnt/e/lynx-work/natives190kj/newtone/det` (`fl_<name>.bin` FL's states, `lx_*.txt` LYNX's
analysis, `py/cmpdet.py` the score, `py/det.sh` the harness run).

## 1. FL's note analysis [EXEC FL]

Material: 23 files — round 95's three synthetic vowels (v1-v3), three new synthetic lines of our own (s1 staccato C D E F
G with 100 ms rests, s3 a low line A2…E3 with detune and vibrato, s4 legato steps C5 E5 G5 C6 and two G4 with a 50 ms
gap), and 17 sung phrases from the installation's Vocals pack (decoded to PCM for the measurement only, never copied
into the repository); 6 of them were kept out of the fitting (hold-out set).

What FL's analysis gives, read off the saved notes:
- **The notes tile the file.** The first note always starts at 0: a lead-in note (`fOrigVelocity` ≈ 0,
  `iOrigValidNoteEnd` = 0) up to the first voice onset, with the next note's pitch. Every other note runs from its onset
  (or from the pitch change, in a legato line) to the next note's start; a rest belongs to the note before it; the last
  note ends at the file's end. `iOrigValidNoteEnd` marks where the note's voice ends (100–800 samples after the audible
  end).
- Onsets after silence land within ±150 samples of the real onset; a legato step lands up to ~250 samples after it.
- **`fOrigPitch` from whole-sample periods**: `fs / floor(fs / f0)` (C4 261.63 Hz → 262.5 Hz → 60.058; D4 + 10 c →
  62.136; confirmed on every synthetic note).
- **Volume fades** `fVolumeFadeInTime` = `fVolumeFadeOutTime` = 0.2 × the note's length (every note of the 23 files).
- **Smooth-in** after silence on a steady note 3200–3300 samples (73 ms); a note that scoops into its pitch: the scoop's
  length; a legato step 0–210 samples. **Smooth-out** = the silence after the voice (end − valid end) + 60–540 samples.
- **Segmentation**: FL keeps short wobbles, slides and vibrato inside one note (a 4.2 s sung line is one note), splits
  at rests and at a new pitch held for a while; notes of 80 ms exist.
- **`KeyIdx`** (AudioObject): the sample's key, 0–11 C…B major, 12–23 C…B minor (s1 C major = 0; v1 / s3 A minor = 21).
- `fOrigVelocity`: a level 0…1 that follows neither the peak nor the RMS of the note (two identical G4 notes: 0.30 and
  0.20) — not reproduced.

## 2. LYNX's analysis now (`newtonefx::analyseNotes`, src/core/NewtoneFx.cpp)
The YIN track of round 95 with a looser voicing threshold (0.3: rough and low voices stay voiced as in FL), a new pitch
must hold 58 ms to start a note (fitted), onsets 1.75 hops (10 ms) before the first voiced frame, a legato boundary
where the pitch leaves the note before; then FL's layout: lead-in note, tiling, `validEnd`, whole-sample pitch, fades
0.2 × length, smooth-in / out measured as in §1, the key (Krumhansl–Schmuckler on the notes; FL's own detector is
zplane's). The fields travel in LYNX's format version 3 (`validEnd`, `velocity`); FL states give them from
`iOrigValidNoteEnd` / `fOrigVelocity` / `KeyIdx`. The score (Create score, Save as MIDI, Edison's Convert to score) takes
the voiced notes up to their voice's end.

| 17 fitted files | LYNX before | LYNX now |
|---|---|---|
| boundaries F-score (±50 ms) | 0.76 (P 0.77 R 0.75) | 0.88 (P 0.92 R 0.84) |
| median boundary error | 12.3 ms | 5.9 ms |
| note count equal to FL's | 2 / 17 | 10 / 17 |
| note pitch within 5 c of FL's | 29 / 71 (median 5.8 c) | 43 / 71 (median 4.3 c) |
| tiled like FL | 0 / 17 | 17 / 17 |
| median error smooth-in / smooth-out / fades (samples) | 2545 / 2598 / 8635 | 285 / 564 / 87 |

| 6 hold-out files | before | now |
|---|---|---|
| boundaries F-score | 0.68 | 0.70 |
| median boundary error | 21.4 ms | 13.3 ms |
| note count equal | 0 / 6 | 3 / 6 |
| pitch within 5 c | 6 / 25 | 6 / 25 (median 15.4 → 14.1 c) |
| tiled | 0 / 6 | 6 / 6 |
| smooth-in / smooth-out / fades | 294 / 445 / 944 | 444 / 942 / 252 |

Key: 17 of 23 files give FL's `KeyIdx`; the others a relative or neighbouring key.
Analysis time: 0.83 s for the 17 files (45 s of audio; before 0.75 s), offline on Analyse / a new sample.

Old projects: a slot's notes are saved with it (LXNT), so the new analysis changes nothing that was saved; version-2
states load as before (the voiced end unknown = the whole note) and render bit-identically (the render reads none of the
new fields; test `natives190kj_newtone_native_v3_and_old_states`).

## 3. FL's Newtone window [DFM, SHOT, MANUAL]
**[DFM]** `Newtone_x64.dll` carries its Delphi forms as binary form data (`TPF0`); our reader
(`/mnt/e/lynx-work/natives190kj/newtone/rea/dfm.py`, layout facts only) lists `TSynthEditorForm` (727 × 582): the 88 px
toolbar `ToolbarWP` (TColor #1a2227) with LoopBtn 5,11 29×28, PlayBtn 34,11, StopBtn 63,11 (FS_Sober, #292f32), SyncBtn =
"Slave playback to host" (H) 5,41 35×17 lit #ff9b37, AutoScrollBtn "Scroll to follow cursor" (A) 43,41 lit #40c65a, the
logo 107,12, TITLE 5,61 / name 6,70 301×14 (#ebeef9), FileMenuBtn 326,11 26×29, EditMenuBtn 350, SelectMenuBtn 374,
UndoBtn 398 ("Undo / Redo"), SendToPianorollBtn 437 ("Send to pianoroll / Copy to MIDI clipboard"), SaveAsBtn 461 ("Save
as... / save as WavPack..."), DragBtn 485 ("Drag selection"), SendToPlaylistBtn 509, LENGTH / SEL 405,61 + value 402,70
right-aligned, TEMPO 477,61 + value 480,70, the SYNC pad ("Tempo-sync") 514,60 31×12, the wheels Center 566,9 40×40
(Max 8192, fill #33c18c), Variation 612,9 (Max 16384, default 8192 = 100 %, fill #ffb00d), Transition 658,9 (Max 16384,
value 16384, fill #33c1e1), rail #3f484d, sweep 304° from 146° left of the top, labels at 51 (P_SmallBold #bccaca),
CutModeBtn 600,64 31×18 (C, lit #799bbd), AdvancedEditBtn 633,64 (Ctrl+E, lit #c460a0), a level meter 710,2 14×84; the
right-hand controls are anchored to the right edge. The menu `WAVOptionsMenu`:
- File: New; Load sample... Ctrl+O; Save sample as... Ctrl+S; Save as MIDI...; Export regions (a checked option, on);
  Slave to host playback ▸ Ignore host selection / Ignore own selection / Ignore space bar key; Use project tempo on load
  (checked); Edit properties... F2; About.
- Edit: Undo Ctrl+Z (Redo); Cut / Copy / Paste; Copy to MIDI clipboard; Delete Del; Quantize time Ctrl+Q; Advanced edit
  pitch Ctrl+E; Advanced edit vibrato; Cut mode C; Snap to scale; Scale ▸ Major C…B | Minor C…B (= KeyIdx 0–23); Snap to
  grid ▸ Beat, 1/2, 1/3, 1/4, 1/6, 1/8 beat, Off; Send to playlist as audio clip; Send to piano roll.
- Select: Deselect (Up); Select time around selection (Ctrl+Enter); Select before / after current selection; Select all
  (time); Select all (slices) Ctrl+A; Select all (time and slices).
- Zoom: Zoom in PgUp; Zoom out PgDn; Zoom out full Home; Zoom on selection End; Zoom on left / right of selection
  Ctrl+Left / Ctrl+Right.

**[SHOT]** FL 2026 with `r5.wav` (GUI session 07:48–08:19, GUI + FL locks, idle gate, input wall, safeinput per batch;
`shots/ref/fl-*.png`): the blob is the note's level, ~2.3 rows tall at its loudest; every note has an orange outline at
its average pitch over its whole span; a note sung off its semitone (24 c) gets its semitone row shaded orange (a 12 c
one does not); selected notes (Ctrl+A) are outlined red; a click puts the play cursor (white line, an orange marker in
the ruler) and does not select; Auto-scroll is on at start; a double-click opens Advanced edit on the note: the view
zooms in (rows ~33 px), the note three rows tall with "volume" on top, "variation" below, green / red bars and arrows at
its start / end and a formant comb; Export regions and Use project tempo on load are checked by default.

**Built** in `src/ui/NewtoneEditor.cpp` (Newtone only; Newtime keeps its layout): the header at the form's rectangles,
colours and hints, the four menus with FL's items and shortcuts (left out until LYNX does them, no stubs: New, the three
slave options, Use project tempo on load, Edit properties, About, Cut / Copy / Paste, Advanced edit vibrato), a
right-click on the empty roll = the whole menu, the note view (keyboard, bar ruler with the time selection and the
cursor marker, rows shaded by the key's scale, 1/4-beat grid, the notes as above, the detected pitch line joined across
legato notes), Advanced edit's in-note handles (volume, variation, fades, smooth times, ramp-in / out, formant; LYNX's
note panel stays beside it for the vibrato / tremolo fields, a LYNX extra), and the shortcuts above (Shift = Cut mode while
held, Ctrl / Shift+Alt + click glues, note edges move the boundary (Ctrl detaches), right-click on a note snaps it,
Space, L, A, H, C, Ctrl+E, Ctrl+A, Up, Ctrl+Q, Ctrl+O, Ctrl+S, Ctrl+Enter, PgUp / PgDn / Home / End / Ctrl+arrows,
wheel / Shift / Ctrl / Alt + wheel, middle-drag pans).
