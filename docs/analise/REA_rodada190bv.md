# Round 190bv — Session view: recording into slots, clips on the move, the Groove Pool

Scope: the optional Ableton-style layout's clip grid (items 6 and 7 of `WHAT-LYNX-LACKS.md`, the Session and Groove pool
rows of `COVERAGE-LIVE.md`). The FL-style workflow stays the default and is untouched: nothing here exists in a project
that never used the grid or a groove.

Method: clean-room. Facts come from (a) the published reference manual of the studied program, (b) static reading of
its installed program file (strings only: scripting-interface docstrings and the names of its built-in test steps) and
(c) the built-in lessons that ship as plain text and pictures in its install. Every fact below is written in our own
words; no code, preset, sample or groove of that program is in the repository.

Source tags used below:

- `[MAN §n]` — reference manual, chapter / section (version 12).
- `[EXE]` — a string in the program file (work copy of the string dump: `lynx-work/ableton/live-exe-strings*.txt`).
- `[LESSON]` — built-in lesson text or picture in the install (`Resources/Builtin Lessons/.../LessonsEN.txt`).
- `[AGR]` — the structure of a groove file, read from files the user owns.

## 1. Recording into clip slots

| # | Behaviour of the studied program | Source | LYNX |
|---|---|---|---|
| R1 | Arming a track makes round record buttons appear in its empty slots (other tracks keep the square stop buttons). | [MAN §19.3.2] "Clip Record buttons will appear in the empty slots of the armed tracks"; [LESSON] picture `EmptySessionSlot`; [EXE] `ASlotRecordButton` | exact (round button in the launch zone of each empty slot of an armed track) |
| R2 | Arm is a button per track at the bottom of its strip, lit red when on. Arming is exclusive by default; Ctrl-click arms a further track. | [LESSON] "Recording Audio" text and picture `ArmButton`; [MAN §19.1] | exact for the behaviour; placement approximate (LYNX has no mixer under the grid: the button sits in a status row under the slots) |
| R3 | Pressing a slot's record button starts a recording into that slot; with a global quantisation other than None the start (and the end) falls on a quantisation point, so the clip is "correctly cut". | [MAN §19.3.2]; [EXE] `will_record_on_start`, `is_recording` on the slot | exact: `launcher::Command::Type::record`, start = next point of the global quantisation |
| R4 | While it waits and while it records the slot's button shows the recording state (test steps check `IsShownRecording()` on the fire button). | [EXE] `pFireButton->IsShownRecording()`, `IsShownPlaying()` | approximate: red launch triangle, blinking until the start; the exact picture needs a live shot |
| R5 | Pressing the recording clip's launch button ends the recording and goes "from recording immediately into loop playback". | [MAN §19.3.2] | exact for MIDI (the clip is made before its end and launched in phase); audio joins in phase once its file is closed (a short gap, see unknown U4) |
| R6 | A Clip Stop button or the transport's Stop ends the recording without playing it. | [MAN §19.3.2] | built: the end falls on the next quantisation point for Clip Stop (U1), at the stop position for the transport |
| R7 | The Session Record button starts recording "into the selected scene in all armed tracks"; pressing it again sends the new clips into loop playback. | [MAN §19.3.2]; [LESSON] "click the Session Record Button to activate recording", picture `SessionRecordButton` (a ring on a square button in the control bar); [EXE] `Transport.SessionRecord`, `trigger_session_record`, `session_record_status` | exact behaviour; the button sits in the grid's footer (LYNX's control bar is FL-style) |
| R8 | Overdub: with clips playing, each press of Session Record toggles between playback and overdub recording into the playing MIDI clips. | [MAN §19.3.3]; [LESSON] "While clips are playing, each press of the Session Record button toggles between playback and overdub recording"; [EXE] `is_overdubbing`, `Slot Overdub Recording` | exact: notes land where the loop was when played; one undo step per overdub pass |
| R9 | Count-in: with a count-in set, recording does not begin until it has run. | [MAN §19.6]; [LESSON] "Live will not begin recording until the count-in is complete" | exact: uses LYNX's existing count-in setting when the transport was stopped |
| R10 | Fixed length: a slot fired with a record length is refired after that length (the clip then plays). Offered by controllers and the scripting interface, not by a desktop control. | [EXE] "If 'record_length' is passed, the clip will be refired after the given recording length. Raises an error if the slot owns a clip."; `mpFixedLength` | LYNX's own placement: grid options › Fixed recording length (Off, 1, 2, 4, 8, 16, 32 bars) |
| R11 | A slot that owns a clip cannot be recorded into from its button (the record call fails). | [EXE] same docstring | exact: the record button exists only in empty slots |
| R12 | Launching a scene does not start recording in armed tracks unless a preference says so. | [MAN §19.3.2]; [EXE] "Start Recording on Scene Launch" | exact for the default; the preference is left out |
| R13 | A new empty MIDI clip: double-click an empty slot (one bar long). | [LESSON] "double-click an empty slot"; [MAN §19.3.3] | LYNX keeps its older double-click (puts the current pattern); the empty clip is in the slot menu and on Ctrl+Shift+M |

Left out (no dead controls): Prepare Scene for New Recording, Start Recording on Scene Launch, Session automation
recording, Capture MIDI into a new slot, audio overdub / punch.

## 2. Clips on the move

| # | Behaviour | Source | LYNX |
|---|---|---|---|
| C1 | Clips move between slots by drag and drop; with Ctrl the clip is copied. | [MAN §7.4] | exact |
| C2 | Cut, Copy, Paste and Duplicate act on the selected slot. | [MAN §7.5]; Edit-menu names in [EXE] | exact for single slots; Duplicate puts the copy in the slot below (U3: what it does when that slot is taken) |
| C3 | Clips copy between Session and Arrangement through the clipboard and by dragging. | [MAN §7.5] | clipboard both ways; drag from a slot onto the Playlist (both views are side by side in LYNX). Dragging a Playlist clip into a slot is left out (use Copy / Paste) |
| C4 | Files dragged from the browser or from outside land in the slot under the mouse; several files fill successive slots of the track. | [MAN §7.4] | exact for audio files and for patterns from the Browser's "Current project"; Ctrl for "across tracks" is left out |
| C5 | A scene with a recording clip cannot be copied. | [EXE] "cannot copy a scene with a recording clip" | not applicable yet (no scene copy in LYNX) |

A copy keeps the clip's name, colour, launch settings, follow action and groove. A moved clip keeps its identity.

## 3. The Groove Pool

| # | Behaviour | Source | LYNX |
|---|---|---|---|
| G1 | The pool lists the grooves of the set; each row has Base, Quantize, Timing, Random and Velocity; a Global Amount scales them. | [MAN §14]; [EXE] groove `base`, `quantization_amount`, `timing_amount`, `random_amount`, `velocity_amount`, "Get/Set the global groove amount, that adjust all setup grooves" | exact columns and ranges; the panel sits under the grid (in the studied program it is under the browser) |
| G2 | Base: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32. | [MAN §14]; [AGR] `Grid` 0–5 | exact |
| G3 | Quantize is applied before the groove's own timing. | [MAN §14] | exact |
| G4 | Random is different per note; the manual describes it per voice. | [MAN §14] | approximate: per note and pass, stateless (U2) |
| G5 | Velocity runs from −100 to +100 %; negative values reverse the accents. | [MAN §14] | built with our own formula (U2) |
| G6 | Global Amount: 0–130 %; scales Timing, Random and Velocity. | [MAN §14] | exact range; Quantize is not scaled |
| G7 | Grooves play non-destructively; Commit writes them into the clip and sets the clip's groove to None. | [MAN §14] | exact for MIDI clips |
| G8 | A groove is shared by the clips that use it; editing it changes all of them. | [MAN §14] | exact |
| G9 | Extract Groove makes a groove from a clip. | [MAN §14] | MIDI clips only |
| G10 | Grooves on audio clips need Warp. | [MAN §14]; [EXE] "Groove is not supported for audio clips" (one clip model) | left out |

LYNX's grooves (decision D5 / D7): an own bank generated by formulas in `core/Groove.cpp` — Swing 8ths / 16ths / 32nds
at 54–75 %, seven Feels, eight Accent shapes and the plain quantise grids. Nothing of the other program's library is
shipped. A groove file the user owns can be read (`Import a groove file...`): `.agr` is XML, plain or gzip-packed; the
template is a MIDI clip (`LoopStart` / `LoopEnd` in beats, notes with `Time` and `Velocity`), followed by `Grid`,
`QuantizationAmount`, `TimingAmount`, `RandomAmount` and `VelocityAmount`.

The arithmetic LYNX uses (`groove::apply`): (1) the note is pulled to the Base grid by Quantize; (2) the template
slot nearest to the note gives an offset (template note minus its grid point) that is added, times Timing × Amount;
(3) a random offset of at most a quarter of the Base, times Random × Amount; (4) the velocity is scaled towards the
template's accent (relative to its loudest note) by Velocity × Amount, mirrored for negative values.

Left out: grooves on audio clips, groove parameters as automation / MIDI targets, saving `.agr`, hot-swap, the
Browser's Grooves category, groove ↔ `.als`, drag a groove onto a track, default groove for new clips.

## 4. Unknowns — need a live check

Recorded in the REA unknown ledger (domains `live-session-record`, `live-groove-pool`).

- U1. Clip Stop on a recording slot: immediate or quantised end? LYNX: quantised.
- U2. Groove arithmetic: offset-add vs absolute pull, the span of Random at 100 %, the Velocity formula. LYNX: as
  described above; the audible result of an imported groove may differ a little from the other program's.
- U3. Name and colour of a recorded clip (LYNX: the next free pattern name / the take's file name, the track's colour);
  Duplicate onto an occupied slot (LYNX: replaces it); a second record button on a recording track (LYNX: ignored).
- U4. How the other program closes an audio recording into loop playback without a gap. LYNX starts the loop in phase
  as soon as the file is closed (some milliseconds after the end).
- U5. The exact pictures of the pending / recording slot and of the pool rows (shots of the real program).

Answered by the live check of Live 12.4.16 (round 190cw, /mnt/e/lynx-work/live190cw/ANSWERS.md) and built in round 190cy:
U1 quantised (as built); U3 a recorded MIDI clip is named "<track> <n>" ("rec MIDI 1", "rec MIDI 2") in the track's
colour, Duplicate replaces (as built), and a second record button on the recording track ends the take into loop
playback and records the clicked slot from that point (built: `Command::Type::record` on a recording track queues
the slot; audio takes are placed when the shared take file closes, so on an audio track the first take is heard only
after the second ends); U5 the recording slot shows the new clip at once with its name, the track's colour and a red
launch triangle, and the track status shows a green length counter (built; the waiting slot keeps the blinking red
triangle: Live's waiting picture was not shot). Audio clip names stay the take file's name (Live's audio naming was not
checked).

## 5. Best way?

- **FL**: records into the Playlist or the pattern; its Performance Mode launches clips but does not record into
  cells. Groove is a destructive quantise template plus a global swing knob.
- **Ableton**: slot recording with quantised boundaries and immediate looping is the heart of its loop workflow;
  grooves are non-destructive, shared, with a global amount.
- **Users**: the praise for the Session workflow is "record a loop, it plays on, record the next" without stopping;
  the usual complaints are clips cut at odd lengths (when quantisation is None) and the lack of a visible fixed length
  on the desktop.
- **Verdict**: follow the session-style behaviour inside the optional layout, and add one option of our own where it
  is cheap and reversible: a visible fixed recording length (the other program hides it on controllers). Grooves stay
  non-destructive for grid clips; FL's swing and quantiser are untouched.

## 6. Code map

- `src/core/ClipLauncher.*` — `record` / `recordEnd` commands, quantised boundaries on the launcher's clock, the
  end told to the message thread ahead of time (`RecordedTake`).
- `src/core/SessionOps.*` — the recorder's arithmetic, the notes of a take, overdub merge, clip copy / move / duplicate,
  grid ↔ Playlist.
- `src/core/Groove.*`, `src/core/Scheduler.cpp` — the pool, the maths, the own bank, `.agr` import; applied to grid
  clips while scheduling.
- `src/engine/AudioEngineSession.cpp` — arming, takes, MIDI and audio placement, overdub.
- `src/ui/ClipGridComponent.cpp`, `src/ui/GroovePoolComponent.*` — the status row, record buttons, Session Record,
  clipboard, drag and drop, the pool.
- Tests: `tests/Session190bvTests.inc`; `.lynx` round trip in `--selftest`; pictures by `--gridshot`.

## 7. Round 190fm: grooves beyond the grid

- **Arrangement clips** [STR: `.GrooveChooser`, `.GrooveCommitButton` in the clip panel's widget list, the same panel for
  Session and Arrangement clips]: a Playlist pattern clip has its own groove (`Clip::groove`, 0 = none = every clip saved
  before; saved in `.lynx` as the clip array's index 24). The scheduler plays it as it plays a grid clip (`groove::ofClip`
  reads only; nothing is allocated on the audio thread). The Playlist's pattern clip menu has Groove (None, the pool's
  grooves, Add a groove), Commit groove (`commitPlaylistClip`: a pattern other clips play is copied first) and Extract
  groove (`extractFromClip`: from the clip's offset over its length). The items show in the Clip-launch layout, or once
  the project has a pool, or on a clip that has a groove: FL's clip menu stays as it was otherwise. A clip moved between
  the grid and the Playlist keeps its groove (on the grid it is the slot's).
- **.als**: ALS_FORMAT.md §22.
- **Classic swing** (LYNX's own, by formula): 8ths / 16ths / 32nds at the swing steps drum machines publish for their
  timing correction (50, 54, 58, 62, 66, 71, 75 % and 50, 54, 58, 63, 67, 71 %), named "Swing 16ths 54 %" etc. As on
  those machines the notes are put on the grid first (Quantize 100 %), then every second one is delayed; Timing and
  Quantize can be lowered in the pool. The machines' own recorded feel (small velocity and timing deviations in Live's
  library files) is not copied.
- Left out: grooves on audio clips (LYNX has no groove on audio), the Detail View's chooser for Playlist clips (the
  Detail View's clip panel shows grid clips only; the Playlist clip menu has the chooser), a default groove for new
  clips, Live's other library families (Logic, Notator, Double Up / Down / Funker, styles and percussion accents).
