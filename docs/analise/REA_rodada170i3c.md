# Round 170i3 (item 3): every control of the FL-native generators' editors vs the sound

Request (round 170i3, item 3): "The audit 170i skipped: controls and editors of the FL-native plugins vs FL. Check
that every knob and switch the importer reads also works in the LYNX editor and changes the sound (a 'every control
changes the render' test like tb169g2 did for Transistor Bass), for all FL-native engines. List any dead controls and
fix them."

## 0. Method

- **Tool:** `scripts/ctlaudit.sh --fl <FL Data> [--only <engine>] [--all] [--set i=raw]… [--controls i,j]`
  (`tools/ctlaudit.cpp`, `tools/CtlAudit.h`; objects in `build-ctl/`, ignored by git). For each engine in
  `natives::instrumentEngines()` it makes the channel the Add menu makes (FL's Default preset; Granulizer gets FL's
  loop `Patches/Packs/Loops/DL Pendulus.wav`, it is silent without a sample, as in FL).
- **Controls:** exactly the ones the LYNX editor shows: the engine's parameter table (`core/ModelParams`, what
  `ModelPanel::addModelParam` builds, written with `modelparams::setRawValue` + `natives::followControl`), or the own
  knob lists of 3x Osc, FL Keys, Sytrus, Harmor and BooBass (`ChannelSettings.cpp`).
- **Pattern:** a 4-note chord at velocity 0.8, a second chord at block 16 (most engines read a control at note on),
  edit at block 8, keys up at block 40, 30 release blocks (512 samples, 44.1 kHz, 120 BPM). Drumaxx plays the 8 pads of
  its Default Kit in both chords; Drumpad, BassDrum and Fruit Kick one key.
- **Each control** goes to the far end of its range and is rendered through three paths:
  - **state:** a new instrument from the edited channel (project saved and opened);
  - **live:** `RackState::build (next, previous)` → `configure` on the running instrument, as an editor knob's
    `ChannelRef::edit` does. The tool also notes when the instrument object is replaced;
  - **auto:** `Instrument::setParameter (index, 0…1)`, as an automation clip or a MIDI link does.
  A path "moves" when its render differs from the unedited one by more than −100 dB relative RMS. Every base render
  is deterministic (noise −300 dB).
- **Context pass:** a control that moves nothing is tried again in 6 random settings of all the other controls
  (seed 170; master, volume, output and mute controls keep their values). A control that never moves is classified by
  reading its code and with targeted `--set` probes (a section switched on).

## 1. Results per engine

"Tested" counts the editor's controls; "Moved" counts those that changed the render directly or in a context.

| Engine | Tested | Moved | Expected no-ops (reason) | Fixed |
|---|---|---|---|---|
| 3x Osc | 26 | 26 | – | – |
| Sytrus | 25 | 22 | Porta (needs legato notes); Export oversampling, Export env accuracy (export render only) | – (left: §3.1) |
| Harmor | 14 | 13 | A/B mix (the Default preset's part B is off; it moves with B on) | – (left: §3.1) |
| Harmless | 107 | 105 | Release velocity to release (needs a note-off velocity); Threaded (CPU use only) | Compression type, Compression level (§2.2) |
| GMS | 136 | 126 | Pitch bend range (needs bend); Osc 2 phase retrigger; the reverb slot's LFO (rate, shape, sync, amount: the reverb has none); the delay slot's LFO shape / amount (it uses only the rate, as its time); the trancer's LFO shape / amount (its gate has its own shape) | – |
| Morphine | 38 | 33 | Bend range (needs bend); Noise mix / pan / mute / velocity (the Default preset has no noise sample) | knob edits no longer cut the notes (§2.1) |
| Ogun | 51 | 46 | Delay feedback / time / stereo offset / volume / feedback mode: the editor has no **Delay on** switch (§3.2) | – (parent's area) |
| Toxic Biohazard | 339 | 224 | seq steps / ping-pong (the sequencer off); the mod matrix (needs its sources); the inactive effect type's fields; LFO 1/2 free (differs only after a pause with no voice); Bend range | – |
| PoiZone | 80 | 65 | Mod wheel amount / destination; gate steps 6–16 and Gate speed (gate off); Bend range | – |
| Sakura | 113 | 113 | – (Delay time and Mod 7 move with the delay / slot on: `--set 63=1 69=1`) | – |
| Kepler | 34 | 33 | Arp range (arpeggiator off) | – |
| Kepler Exo | 149 | 137 | Retrigger, Release mode (mono patch with overlapping keys); Mod 1–8 amount (sources); X / Y offset | – |
| Transistor Bass | 124 | 42 (+ tb169g2) | covered by round 169g2's own tests (sequencer on / off) | – |
| BooBass | 3 | 3 | – | – |
| SimSynth | 58 | 58 | – | – |
| MiniSynth | 43 | 41 | Mod wheel destination / amount | – |
| Fruity DX10 | 22 | 22 | – | – |
| Plucked! | 5 | 5 | – | – |
| Sawer | 94 | 75 | Glide key mode (legato); Bend range; the mod matrix's sources / destinations / amounts | – |
| BeepMap | 8 | 7 | Loop (the picture is longer than the pattern) | – |
| FL Keys | 15 | 15 | – | – |
| FL Slayer | 25 | 24 | Glissando (legato notes) | – |
| Fruity Granulizer | 15 | 13 | Transient hold, Tempo locked (the pattern's tempo is the loop's) | – |
| Drumaxx | 685 | 420 | pads 11–16 (not in the Default Kit's 8 keys); Mallet / Mid retrig (a pad hit again while it sounds); Limiter release | – |
| Drumpad | 38 | 38 | – | – |
| Fruit Kick | 6 | 6 | – | – |
| BassDrum | 23 | 23 | – | – |
| Wave Traveller | own editor | – | not in this audit: its editor is the scratch view (WaveTravellerEditor.cpp) | – |
| Wasp, Fruity Dance | no knobs | – | – | – |

Controls the first run listed as "never moved" that targeted probes showed working (no defect): Harmless Delay
feedback (delay on: −19 dB), Toxic Filter decay / sustain (filter mode on, envelope amount up; with sustain 1 the decay
can't be heard, as in any ADSR), Toxic Osc 4/6 decay (the operator on with sustain < 1), Toxic FX 2 delay sync (FX 2
on as a delay: −25 dB in all three paths), GMS Trancer LFO sync (slot on: −3 dB), Drumaxx pad 7 SOF freq / decay (SOF
level up) and Vel mod 2 value (a target set), Sakura Delay time, MiniSynth Filter decay / sustain.

Paths that differ by design (not dead controls):
- **Toxic:** automation of the oscillator / LFO waves and the FX types does nothing while notes play. Their tables are
  built off the audio thread, so a change goes through a new state (the editor's path works).
- **Ogun:** Richness and Quality have no automation; FL's ProcessParam covers 0…48 (and 63 for Autogun).
- **Granulizer:** Transients source and Tempo locked re-slice the sample, so they follow the channel's state, not
  automation (round 168q).
- **Harmless:** an edit of a rack control (90…98, compression type) rebuilds the rack, which cuts the delay and reverb
  tails (round 168o, FL's own rebuild); the voices go on.
- **BeepMap:** changing Max height decodes the picture again and kills the voices, as FL does when a new bitmap loads.

## 2. Fixed

### 2.1 Morphine: a knob turned while notes play no longer cuts them

Every Morphine editor knob (they write the patch's floats in its state) went through `configure` → a new synth,
which ended the sounding notes. Now, when only the patch's live floats changed, `configure` queues them
(`LiveParamQueue`, as Toxic and Ogun do), and the audio thread writes `A[index]` and calls `liveGlobal (index)`, the
path automation already used. Indices 2 and 3 and changes outside the 40 floats still build a new synth.
`src/core/Morphine.cpp`.

### 2.2 Harmless: the editor's Compression type menu loads its preset

The importer loads `Harmless\Data\Compression\<n>.fst` (the type's Maximus preset) into the channel. The editor's
**Compression type** menu changed only the number, so the compressor stayed as it was (off on a new channel). That
also left the **Compression level** knob without effect. `natives::followControl (channel, index, fl)`
(`core/NativeEngines`) now loads the preset when the menu changes (type 0 removes it). `ModelPanel`'s knob calls it
after `setRawValue`. After the fix the type moves the render by −1 dB and the level does too (state, live,
automation). Automation of the type changes only the number (the preset is loaded with the state).

## 3. Left, with the reason

### 3.1 Sytrus and Harmor: an editor knob restarts the engine

Both build a new engine for any change of their state (`SytrusEngine.cpp configure`, `Harmor.cpp configure`), so a
knob turned while notes play cuts them. The control itself works (it moves the render through the state and on the
next notes). A live path needs the patch-derived structures (operator tables, voice pool, effect rings) to be updated
in place: a refactor of its own, not done in this round. `ctl170i3_a_knob_edit_keeps_the_sounding_notes` skips these
two engines.

### 3.2 Ogun: the editor has no Delay on, Delay tempo or Reverb on switch (parent's area: not edited)

`Ogun.cpp designDelay` turns the delay on only with `S.b358[1]` (the tempo flag is `b358[0]`); the reverb is on only
with `P[53] & 0xff`. The parameter table (`ModelParamsSynths.cpp ogunCommon`) lists none of the three, so on a new
channel (delay off) the five delay knobs 26–30 never change the sound. They work on an imported project whose delay
is on. Reported to the round's parent (Ogun is its area).

## 4. Tests

`tests/CtlAudit170i3Tests.inc` (skipped without FL's Data folder):
- `ctl170i3_every_control_changes_the_render`: per engine, a sample of at most 24 editor controls (the allowed ones
  out, each with its reason in the file). Each must move the render through a new instrument, directly or in a
  random setting of the other controls.
- `ctl170i3_editor_edits_and_automation_reach_the_playing_instrument`: per engine, up to 3 controls that moved. Each
  must also move through the editor's live path and through automation (except the listed ones), and the instrument
  must not be replaced.
- `ctl170i3_a_knob_edit_keeps_the_sounding_notes`: per engine (not Sytrus / Harmor), two non-level controls edited
  live. The notes must still sound between the edit and the next chord.

## 5. Where I stopped

- Sytrus / Harmor live editing (§3.1) and Ogun's missing switches (§3.2) are open.
- Transistor Bass is not re-audited here (round 169g2 covers every knob with the sequencer on and off).
- Wave Traveller's own editor is not in the tool (it has no knob list).
