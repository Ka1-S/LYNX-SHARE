# Round 190ef: Live workflow items for the Clip-launch layout (scene work, Draw Mode, map modes)

Ableton Live 12.4.16 (the study install) is the reference for the optional Clip-launch (Ableton-style) layout. FL's
Pattern layout stays the default and unchanged: every key below acts only in the Clip-launch layout
(`core/SessionKeys.h`). Clean-room: facts in our own words; no Live artwork, text or presets are in the repo.

Evidence keys:
- **[BIN n]**: line n of the program file's string table (`live-exe-strings.txt`, the Live 12 executable's strings,
  made by an earlier round); **[BIN16 n]**: its UTF-16 strings.
- **[LESSON]**: Live's built-in lesson "Creating Manual MIDI Mappings" (`Resources/Builtin Lessons/.../Creating Manual
  Mappings Lessons/LessonsEN.txt`) and its pictures (`MappingBrowser.png`, `HighlightedElements.png`,
  `PanVolumeMapped.png`, `SceneAssign.png`, `MidiMapSwitch.png`, `MappingControlBar.png`).
- **[MAN]**: the public Live manual (Draw Mode).
- **[XML]**: Live's own set files (DefaultLiveSet, the lesson set `Creating_Manual_Mappings.xml`).

## 1. Scene work (Session view)

| Item | Evidence | LYNX | Exact / approximate |
|---|---|---|---|
| Duplicate scene | [BIN 1592455] `Edit>Duplicate`, [BIN 1606365] `sMenuDuplicate`, test step "I duplicate" | Scene menu › Duplicate, Ctrl+D on a selected scene (a click on a scene selects it); `session::duplicateScene` | Copies every slot (clip + launch settings) and the scene's name, colour, tempo, bar length, directly below. Whether Live renames the copy: live check |
| Capture and Insert Scene | [BIN 1592592] test step "I Capture and Insert Scene", [BIN 1586725] `sMenuInsertCapturedScene`, [BIN 1672928] "The capture mode that is used for capture and insert scene." | Scene menu, Ctrl+Shift+I: a new scene below the selected one with copies of the clips playing now (`session::captureScene`) | The playing clips are not relaunched. Whether Live launches the new scene seamlessly, and its "capture mode": live check |
| Insert Scene | [BIN 1592501] `Create>Insert Scene` | Ctrl+I in the grid (Insert scene below the selected one; the menu items were there) | |
| Scene tempo / time signature | [BIN 1592535] `View>Scene Tempo And Time Signature`, [BIN 1683741] `sMenuPrefSceneTempoAndTimeSignature`, [BIN 1609441] `ShouldSceneTempoAndTimeSignatureBeVisible`, [BIN 1703126 / 1703133] `sMenuDisableSceneTempo` / `sMenuDisableSceneTimeSignature`, `ASceneTempoViewModel` with an `ASliderEditField`; [BIN16 2152 / 2154]; [XML] Scene/Tempo, IsTempoEnabled | View switch "Scene tempo and time signature" (UI setting, off by default); per scene a tempo field and a signature field: drag, double-click to type, right-click › Disable; launch applies them (unchanged engine path) | Signature is n/4 (LYNX's song bar length has no denominator). Field look, place and drag rate: approximate |
| Rename / colour from the track title | [BIN 1592459] `Edit>Rename`, [BIN16 2544] `SessionView.MainTrack.TitleBar`, [XML] MainTrack/Name, Color | Title menu: Rename (Ctrl+R) and Change color; the scene menu gets a colour too | Rename in a dialog (Live edits in place); LYNX's colour submenu (Live: a palette grid) |
| Capture MIDI into a slot | [BIN16 5259] `Trigger Capture`, `MenuCaptureMidi`, `mpTempoAfterCapture` | With the grid on screen, Capture MIDI makes a new clip in the selected slot (the next empty one below when taken), named "<track> <n>" | Track / slot choice, launch, tempo after a capture made while stopped, capture into a playing clip: live checks |
| Playlist clip into a slot | `ASessionCopyTrackSource`; the manual's drag between the views | A Playlist clip dragged off the Playlist onto a slot drops a copy | Copy only |

## 2. Draw Mode (Arrangement automation)

| Item | Evidence | LYNX |
|---|---|---|
| Draw Mode switch (B) | [BIN 1592536] `Options>Draw Mode`; [BIN16 5288 / 5297] "Draw Mode", "Automation Draw Mode"; the pencil in the control bar ([LESSON] `MidiMapSwitch.png`) | B (Clip-launch layout), Options › Draw Mode, the pencil switch in the Playlist's title bar beside Both / Grid / Song |
| Steps at the grid width | [MAN] | A drag in an automation lane draws one flat step per grid cell (the Playlist's current grid); the curve outside is kept (each edge is a jump: two points on one tick) |
| Freehand with the grid off | [MAN] | Grid none, or Alt held (LYNX's no-snap modifier; Live: Ctrl+4) |

## 3. MIDI Map Mode and Key Map Mode

Rules (all from [LESSON] unless marked):

1. A switch (Live: the MIDI and Key switches in the control bar; menu Edit › MIDI Map Mode, Ctrl+M / Key Map Mode,
   Ctrl+K: [BIN 1586681 / 1586682] `sMenuEditMidiMap` / `sMenuEditKeyMap`) turns the mode on; "Live highlights every
   interface element which is capable of remote-control".
2. "Click once on any clip to select it ... Press any MIDI-capable key, button or pad on your control surface to assign
   it to the selected clip slot." Volume / pan: "Move the knob or fader to assign it".
3. "MIDI-mappings won't function while you're in MIDI Map Mode."
4. "The numbers superimposed onto mapped controls are ... the MIDI channel ... and ... the MIDI CC number."
5. "MIDI assignments belong to clip slots rather than the clips themselves."
6. "The same MIDI key, button or pad can be assigned to multiple clips so long as they all reside in different tracks;
   Live will warn you if you try to assign the same control to more than one clip from a single track."
7. "You can't assign a MIDI controller to a scene if it has already been assigned to a clip."
8. Mapping Browser: "For each entry, it indicates the MIDI channel, the MIDI note or CC number, as well as the path and
   name of the mapped parameter ... minimum and maximum values"; columns Channel, Note/Control, Path, Name, Min, Max
   (picture); "The Min and Max fields let you refine the ranges ... Mappings can be inverted by setting the value of Min
   to be greater than Max, or by ... choosing Invert Range from the context menu"; "delete a mapping by selecting it
   ... then pressing the Delete or Backspace keys. Multiple mappings can also be selected and removed at once."; "only
   accessible while the MIDI, Key or Macro Map Modes are active". [BIN 1647547 / 1647548] `sOsColumnMin` / `sOsColumnMax`.
9. Transport controls are mappable ([LESSON] "Tips": play, stop, record; [BIN 1608756-1608758] `PlayKeyMidi`,
   `StopKeyMidi`, `RecordKeyMidi`); "the same control to several volume sliders" is allowed.
10. Key mappings and MIDI mappings are saved with the set ([LESSON] "will include all MIDI and key mapping
    information"; [XML] `<KeyMidi>` with `PersistentKeyString`).

LYNX (`core/RemoteMap.h`, `ui/MapMode.h`):

| Item | LYNX | Exact / approximate |
|---|---|---|
| Switches | Ctrl+M / Ctrl+K (Clip-launch layout only; elsewhere Ctrl+M stays FL's metronome) and Key / MIDI switches beside Both / Grid / Song in the Playlist's title bar | Live's switches are in the control bar: LYNX puts them in the Clip-launch title bar (the toolbar's groups are FL's) |
| Overlay | Tints every mappable element: clip slots, scene launch buttons, track clip-stop buttons, Stop all clips, Session Record, Play, Stop, Record, Metronome, Tempo, Session mixer volume / pan / sends, Device View knobs. A click selects one (white frame); the overlay takes clicks only on elements | Colours: LYNX's theme blue (MIDI) and orange (Key). Mixer activator / solo / arm, Clip View controls, the FL Mixer and plugin windows are not mappable from the overlay yet |
| Assigning | MIDI: the next controller moved or key pressed (the engine catches it while MIDI Map Mode is on; keys do not play meanwhile). Key: the next plain key. Delete / Backspace over a selected element removes its mapping | Rules 2, 5, 6, 7 exact. Delete on a selected element: LYNX's choice |
| Labels | "1/19" (channel / controller), "1/C3" (channel / note), the key's name | Rule 4. Live's note naming (C3 = 60) |
| Mapping Browser | In the Browser's place while a map mode is on; MIDI: Channel, Note/Control, Path, Name, Min, Max; Key: Key, Path, Name, Min, Max; Min / Max dragged or typed; row menu Invert Range / Delete; Delete / Backspace; Ctrl / Shift-click selects several | Rule 8 exact. Path / Name come from LYNX's target names ("Bass", "Volume") |
| Ranges | `MidiLink::min / max` (also for key mappings): position = Min + (Max − Min) · controller; a key toggles a parameter between Min and Max | The lesson states Min / Max and inversion; a key on a continuous control toggling between Min and Max is LYNX's reading of the manual (live check) |
| Buttons | Notes press on note-on, release on note-off; a controller presses at ≥ 64 and releases below | The threshold for controllers is LYNX's choice (row `mapping-0017` stays unknown) |
| Acting | MIDI mappings of slots / scenes / buttons fire on the message thread (`AudioEngine::onRemoteFire`); key mappings act in the Clip-launch layout and win over the typing keyboard; a slot's release reaches Gate-mode clips | |
| Storage | Parameter MIDI mappings are FL's links (`Song::midiLinks`, the same storage as Link to controller); everything else `Song::remoteMappings` (.lynx "remoteMappings"); a link's range in "midiLinkSettings" (`min`, `max`). Older versions skip both and keep the plain link | |
| Rows follow scenes | Inserting / removing / duplicating scenes moves the mappings of slots and scene buttons with their rows; a removed row's mappings go | Rule 5 |

Left out (no dead controls): Macro Map Mode (Racks have their own map mode), note ranges on a clip (chromatic
playback) and on a parameter, relative navigation (scene up / down / selected scene launch, Track Launch), Clip View
scrub / nudge mappings, Takeover modes per mapping (LYNX keeps the global pickup setting), mapping of the mixer's
activator / solo / arm buttons, double-click on a row to show the device.

## 4. Needs a live check

1. Capture and Insert Scene: does Live launch the new scene (seamlessly), and what does nothing playing do?
2. Duplicate scene: is the copy's name changed?
3. Scene tempo / signature fields: default visibility, look, place, drag rate, what a disabled field shows.
4. Capture MIDI in the Session view: which track (armed / selected) and slot; does the new clip launch; tempo after a
   capture made while stopped; capture into a playing clip.
5. Draw Mode: how a step's edges are stored; a click without a drag; fast moves; the step width vs the zoom grid.
6. Map modes: Live 12's overlay colours; the status-bar text; whether Delete over a selected element removes its
   mapping; a key on a continuous control (toggle between Min and Max?); a controller's threshold on a button.
