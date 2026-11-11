# Round 190bb — MPE, stage 1 (issue #108)

Evidence tags: **[STATIC]** read from FL Studio 2026's own files (strings / the wrapper's settings form), **[REA]** an
earlier round's decompilation (cited), **[SPEC]** the MPE specification (MIDI Association RP-053) and the CLAP headers
in `third_party/clap`, **[RUN]** run in this round, **[MODEL]** LYNX's own choice.

## 1. What FL Studio 2026 has

- **No MPE switch, in or out.** [STATIC] `FLEngine_x64.dll` and `Fruity Wrapper_x64.dll` hold no "MPE" caption or hint
  (only byte noise), `WhatsNew.rtf` / `Help/WhatsNew.htm` never mention it, and the wrapper's settings form (MIDI
  section) holds exactly: Input port, Output port, `SendNoteOffVelocityBox` "Send note release velocity",
  `SendPitchBendRangeBox` "Send pitch bend range (semitones)" + `MIDIPBRangeWheel`, `SendPolyAftertouchBox`
  "Send MOD X as polyphonic aftertouch", `MidiEventBusCombo` "Event bus", `SendAllNotesOffBox`. The offline manual is
  not installed (`Help/html` holds only two image folders), so the manual text could not be read.
- **What a hosted plugin gets per note** [REA `REA_rodada188g.md` §2, `core/HostNotePitch.h`]: the note's fine pitch as
  the key (VST2 / MIDI: rounded, the rest lost), as the VST3 note on's tuning, CLAP: the truncated key and nothing else;
  Mod X as polyphonic aftertouch when its switch is on; pan never.
- **Per-note data in the piano roll**: one value per note (fine pitch, pan, Mod X, Mod Y, release velocity) and slide
  notes; no per-note curves.

So MPE is a **LYNX addition**. The brief's premise "FL has a per-plugin MPE switch in the wrapper settings" does not
hold for FL 2026 [STATIC]; the switch was put where it would be if FL had one (the wrapper's MIDI section, after "Send
MOD X as polyphonic aftertouch"). **Decision for the owner** (recommended option taken): keep that place.

## 2. What was built

| Part | Behaviour | Where |
|---|---|---|
| Laws | per-note bend ↔ the note's fine pitch (cents); CC 74 ↔ Mod X (64 = 0.5); pressure ↔ Mod Y (0 = 0.5, full = 1) [MODEL, on FL's per-note fields] | `core/Mpe.h` |
| Input | lower zone, master channel 1, member channels 2 … 1 + n (n = 15 by default, set per device or by the controller's MPE Configuration Message), bend range 48 (or the controller's RPN 0) [SPEC]. Member-channel bend / CC 74 / channel pressure / poly pressure → the notes on that channel; everything else is an ordinary message | `mpe::Input`, `AudioEngine::handleIncomingMidiMessage` |
| Playing | the note starts at its bend / timbre / pressure and follows them while it sounds (live `voiceProp` events → `Instrument::noteExpression`) | `RackRenderer.cpp`, `Instruments.h` |
| Recording | the note's fine pitch = its bend at the note on (clamped to the piano roll's ±120 cents), Mod X = CC 74 at the note on, Mod Y = its peak pressure | `Recorder.cpp` |
| Built-in instruments | Sampler voices: pitch, Mod X / Mod Y (the voice filter); the built-in synth: pitch. Others ignore it (their `voiceProperty` is empty) | existing `voiceProperty` |
| Hosted plugins, "MPE" on | each note on its own member channel (longest-released first); before the note on: pitch bend (rest of the fine pitch over ±48), CC 74 (Mod X), channel pressure (Mod Y); the zone's setup (MCM + RPN 0) with the first block; the channel's own pitch / slides stay on the master channel | `PluginInstrument` |
| CLAP | a plugin that takes CLAP notes gets the member channels' data as `CLAP_EVENT_NOTE_EXPRESSION` tuning / brightness / pressure (channel = the note's, key and note id wildcards) [SPEC]; a MIDI-dialect plugin gets the MPE MIDI | `ClapFormat.cpp` |
| VST3 / VST2 | the MPE MIDI through the format's MIDI path (VST3: JUCE maps bend / pressure / CC to the plugin's `IMidiMapping` parameters per channel) | unchanged path |
| Settings | Settings › MIDI › a device's channel box gains "MPE controller" (+ the member channel count beside it); the plugin wrapper's Settings › MIDI gains "MPE" | `SettingsPages.cpp`, `FlWrapper.cpp` |

Off by default everywhere: a device without MPE and a plugin without the switch run the code they ran before (the MPE
branch is not entered; `NoteEvent::expression` is false; the recorder's fields keep the note defaults). Settings files
gain an `inputmpe=` line only for MPE devices; projects an `"mpe"` property only when on.

## 3. Best way?

- **FL:** no MPE. Per-note data is static per note plus slide notes [STATIC, REA 188g].
- **Ableton Live 11+** (from its public manual as recalled, not re-read or run this round): an "MPE" checkbox per
  input port in the MIDI preferences; per-note Pitch, Slide (CC 74) and Pressure **curves** in the clip's Note
  Expression tab; a per-device MPE mode for plugins (channel rotation); `.als` `PerNoteEventStore`
  (docs/analise/ALS_FORMAT.md).
- **Users:** not researched this round (no web search was run); the issue itself names Live and Bitwig as the models.
- **Verdict:** per-device switch + per-plugin switch as Live (and as FL would place them); FL's per-note fields as the
  storage in stage 1; per-note curves are stage 2 (needs a new note-expression lane, a model change to discuss).

## 4. Stage 2 (not built)

1. Per-note expression **curves** recorded and edited in the piano roll (pitch / timbre / pressure lanes per note),
   played back to instruments and plugins; `.als` `PerNoteEventStore` import / export.
2. **VST3 note expression events** (`kNoteExpressionValueEvent` with the note id) instead of MPE MIDI for VST3 plugins
   that declare `INoteExpressionController` (a further patch to JUCE's `MidiEventList`, beside the note id / tuning
   patches in `cmake/LynxPatchJuce.cmake`).
3. More built-in instruments per voice (`voiceProperty` exists only in the Sampler and the built-in synth); pressure
   mapped per instrument.
4. Slide notes → per-note bend ramps on the member channel; release velocity ("lift") per note from the controller is
   already passed; upper zone; two MPE controllers at once (one decoder today); MPE out to MIDI Out channels.
