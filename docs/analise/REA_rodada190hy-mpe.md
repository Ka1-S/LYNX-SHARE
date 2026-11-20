# Round 190hy — MPE, stage 2 (issue #108): per-note expression curves

Evidence tags: **[STATIC]** read from the programs' own files on this machine (strings, shipped sets, lessons),
**[DATA]** measured in Live's shipped `.als` sets, **[SPEC]** the MPE specification (RP-053) and the CLAP headers in
`third_party/clap`, **[RUN]** run in this round (tests, renders, shots), **[MODEL]** LYNX's own choice.
Stage 1: `REA_rodada190bb-mpe.md`.

## 1. What the references have

### FL Studio 2026 — still no MPE
- [STATIC] `FLEngine_x64.dll` and both `Fruity Wrapper_x64.dll` (Generators / Effects) hold no "MPE", "polyphonic
  expression" or "note expression" caption or hint. Their only related strings are the VST3 SDK type names
  `TNoteExpressionValueEvent` / `TNoteExpressionTextEvent` (the wrapper's VST3 note on with its tuning, REA 188g) and
  the wrapper's "Send MOD X as polyphonic aftertouch". `WhatsNew.rtf` / `Help/WhatsNew.htm` never say "MPE".
- So the brief's premise ("FL 2025+ has MPE support, per-note pitch / fine pitch, event editor lanes") does not hold
  for the FL 2026 install here. FL's per-note data is one value per note (fine pitch, pan, Mod X, Mod Y, release) plus
  slide / porta notes; LYNX already has all of them (FL-exact, earlier rounds). Users say the same: slides and
  per-note parameters reach FL-native plugins only, and staff said MPE comes "much later"
  (`/mnt/e/lynx-work/ableton/research-web-2.md` row 12: IL KB 592, IL forum p=2026455, p=2040008).
- Verdict: the per-note curves are a LYNX addition. They follow Live (below) and sit on top of FL's note
  properties: a note without curves plays exactly as before.

### Ableton Live 11 / 12 — per-note expression
- [STATIC] Live 12's Clip View has the tabs **Notes | Envelopes | MPE** (shot
  `/mnt/e/lynx-work/ableton190o/shots/live-01-clipview-notes.png`). The program's strings name the parts:
  `ClipDetailView.MidiEditorSplitter.MidiExpressionEditor.ExpressionLanes`, `ExpressionLaneTypeMenuButton`,
  `ExpressionLaneLabelsCard`, `ExpressionSplitterButton`, `KeyTrackRackView.ExpressionAutomationView` (the curve over
  the note itself), `APerNoteEventStore` / `APerNoteEventList` / `APerNoteEvent`, lanes "Pitch Bend", "Slide",
  "Pressure", "MPE Slide (Sound Controller 5)", "Note Pitch Bend Range" (`live-exe-strings-u16.txt`).
- [STATIC] Live 12's What's New lesson: "*Join* ... Any MPE envelope data will be preserved when notes are joined";
  MPE MIDI Tools (Glissando, LFO) "create or transform MPE data curves that are displayed in the MPE Editor view mode".
- [DATA] The `.als` store, measured in 5491 non-empty lists of `Chuck_Sutton_-_Patience__Live_12_Suite_Demo_.xml`
  and in `Live_11_Suite_Empty.xml`, `Drums_Lesson.xml`:
  `Notes/PerNoteEventStore/EventLists/PerNoteEventList{@Id, @NoteId, @CC}/Events/PerNoteEvent{@TimeOffset, @Value,
  [@CurveControl1X/1Y/2X/2Y]}`.
  - `CC` −2 = pitch bend, Value × 48 / 8191.5 semitones (−1706.5625 = −10 st and 1194.59375 = 7 st exactly);
    74 = slide 0 … 127; −1 = pressure 0 … 127.
  - `TimeOffset` in beats from the note's start (NoteId 38 at Time ≈ 0, Duration 3.853, its events 0.377 … 3.84).
  - `CurveControl*` (Live 12 Bézier) belong to the segment that starts at that event: 2227 of 5491 first events carry
    them, 44 last events do.
  - `@NoteId` is the `MidiNoteEvent`'s NoteId in the same clip.

## 2. What was built

| Part | Behaviour | Where |
|---|---|---|
| Model | `Note::curves`: three lanes of breakpoints (tick from the note's start, value), linear, held before the first and after the last. Pitch = cents over the note's fine pitch (±4800), timbre = Mod X law (0.5 = CC 74 at 64), pressure 0 … 1. A value handle (one pointer, deep copy, compared by content): plain notes stay as small and as fast to copy [MODEL] | `core/NoteCurves.h`, `core/Song.h` |
| Recording | an MPE controller's member-channel bend / CC 74 / pressure during a take go to the held note (`RecordedEvent::lane`), thinned (Ramer–Douglas–Peucker, 1 cent / 0.004) into its curves; a lane that never moved is left out (stage 1's fine pitch / Mod X / Mod Y say it); a bend beyond ±120 cents at the note on keeps the rest in the pitch lane | `AudioEngine::handleIncomingMidiMessage`, `NoteRecorder::takeCurves` |
| Playback | the scheduler gives a note its curves' first values at the "on" (fine pitch + bend, Mod X, Mod Y) and, while it sounds, one voice property per moving lane every 64 samples (coarser in long blocks: ≤ 16 per lane and block), loops and Chance included; the renderer hands them to `Instrument::noteExpression` as stage 1 does for played notes | `core/Scheduler.cpp`, `core/RackRenderer.cpp` |
| Built-in instruments | Sampler (pitch, Mod X / Mod Y filter), the built-in synth (pitch), FPC — the instruments with per-voice properties (stage 1) | existing `voiceProperty` |
| Hosted plugins (wrapper MPE on) | the curves on the note's member channel: pitch bend (±48), CC 74, channel pressure; CLAP plugins that take CLAP notes: tuning / brightness / pressure note expressions; VST3: the same MPE MIDI through JUCE's per-channel MIDI mapping | stage 1's `PluginInstrument::noteExpression`, `ClapFormat.cpp` |
| Piano roll (FL layout) | the bottom lane's list gains "MPE pitch bend", "MPE slide", "MPE pressure": each note's curve over its length (selected notes bright, dashed = the note's own value); drag draws the curve of the note under the mouse (a selected one first), a point is dragged to move it and double-clicked to delete it (Live), right-drag erases, Alt+click clears (LYNX's lane gestures); the notes show their pitch curve over their key rows while an MPE lane shows (Live's `ExpressionAutomationView`). Pitch lane range ±12, ±24 or ±48 semitones as the curves need | `ui/PianoRollComponent.cpp` |
| Clip View (Clip-launch layout) | a third tab **MPE** after Notes | Envelopes, as Live 12: the docked Piano roll with the three lanes stacked under the notes | `ui/DetailView.cpp` |
| Note tools | split, chop (FL Chop tool) and the knife keep each part's stretch of the curves; Join keeps both notes' curves (Live) | `core/NoteEdit.cpp`, `core/PianoRollTools.cpp` |
| `.lynx` | a curved note's 20th element: three flat `[tick, value, ...]` lanes; other notes keep their old, shorter form (an older LYNX opens the file and drops only the curves) | `engine/ProjectFile.cpp` |
| `.als` | import: `PerNoteEventList` by NoteId, CC −2 / 74 / −1, Bézier segments flattened (8 steps) and thinned; export: one list per lane, linear points, fine pitch added to the pitch points; the device's `MpeEnabled` ↔ the wrapper's MPE switch | `core/AlsImport.cpp`, `core/AlsExport.cpp` |
| `.flp` | FL has no per-note curves: the start values stay FL's fine pitch / Mod X / Mod Y (stage 1); the curves are not written | — |
| Remote control | `pattern.notes` / `pattern.replace_notes` carry `expression` {pitch, timbre, pressure: [[tick, value], ...]}, required and validated like the other fields (editable; read → write keeps it); document edits accept the note's curve element (plain data arrays) | `engine/RemoteNotes.h`, `docs/REMOTE_CONTROL.md` |

## 3. Best way?
- **FL:** no per-note curves; one value per note and slide notes [STATIC above].
- **Ableton:** curves per note in the MPE view (Notes | Envelopes | MPE), lanes Pitch Bend / Slide / Pressure under
  the notes, the pitch curve over the note, Join keeps curves, MPE MIDI Tools [STATIC, DATA].
- **Users:** MPE / note expression for third-party plugins is a recurring FL request (2024–26 threads); Bitwig's
  note expressions are the praised model (research-web-2.md row 12, KVR p=7288872).
- **Verdict:** FL's piano roll stays as it is; the curves are three more entries in its lane list (the FL layout),
  and the Clip-launch layout gets Live's MPE tab. Nothing changes for a note without curves.

## 4. Open (needs a live check / later)
1. Live's MPE tab layout in detail (lane heights, the lane type menu, the splitter, whether all three lanes show at
   once, Live's draw-mode and breakpoint gestures): no Live shot of the MPE tab exists on this machine; LYNX stacks the
   three lanes and uses Live's breakpoint gestures as its lesson and strings describe them. **Needs a live check.**
2. VST3 Note Expression events (`kNoteExpressionValueEvent` tuning / brightness, `PolyPressureEvent` with the note
   id) for VST3 plugins that declare `INoteExpressionController`: needs a further JUCE patch beside fixes 18 / 26
   (`cmake/LynxPatchJuce.cmake`) and a VST3 test plugin that logs them. VST3 plugins get MPE MIDI meanwhile (works with
   JUCE-built MPE synths, which map per-channel MIDI to parameters).
3. Live's Bézier curve controls are flattened on import and not written on export (linear points).
4. MPE MIDI Tools (Glissando, LFO) can now be built on the curves (REA_rodada190aa.md §5).
5. More LYNX generators answering per-voice timbre / pressure (only the Sampler maps them today); that is a DSP
   change under the plugin quality gate.
