# Round 167a (left167): leftovers from 0.1.21 → v0.1.22

Per item: reporter's words | FL evidence | LYNX before | LYNX now. Evidence markers: [FL render] FL 2026 exporting a
test project written for this round, [SHOT] FL driven as a black box (screenshots in
`C:\Users\kauan\lynx-test\left167\s\`), [DECOMP] Ghidra on FL's own binaries (`fle.dll`, read-only project
`/mnt/d/lynx-work/rea165/flea`), [TXT] FL's own files, [TEST] LYNX unit tests. FL was opened once for the whole round
(2026-10-31 13:07–13:14, one session, closed by its own PID; shot s38 = the desktop afterwards).

## 1. Live channel PITCH knob and main pitch on a sounding note

- **Reporter:** turning the channel PITCH knob or the master pitch while a note sounds only changes the next notes on
  LYNX's native engines.
- **FL evidence [FL render + TXT]:** `allpitch2.flp` (27 engines × 3 segments of 5 bars, one held C4 each): `kn` = a
  pattern event on the channel's PITCH (`REC_Chan_Pitch` = 4, FL's `Shared/Python/Lib/midi.py`; .flp 0xDF records
  {tick, control 4, value}, value 200 = +200 cents) at bar 3, `mp` = the main pitch (`REC_MainPitch` = 0x40000002) at
  bar 3. Pitch read by autocorrelation (`py/pt.py`, table `py/an2.txt`): every synth reads **+200 cents at 4.05 s**,
  50 ms after the move, and stays there: 3x Osc, Sytrus, Harmless, MiniSynth, Sawer, SimSynth, PoiZone, GMS, Harmor,
  DX10, FL Keys, BooBass, Sakura, Autogun, FLEX (main pitch). The held note moves at once, it does not glide.
  The knob's range is the channel's pitch *bend* range (.flp record 0x01 +8, LYNX `misc.pitchRange`).
- **LYNX before:** native engines baked the knob (configure) and the main pitch (setNextNoteExpression) into each voice
  at note on; only hosted plugins followed (`setLivePitch` → MIDI pitch bend, issue #14).
- **LYNX now:** `Instrument::setChannelPitchNow (knob, rest)` every render (RackRenderer), `rest` = fine tune + main
  pitch. Each voice anchors the channel pitch it started with (`HostSlide::anchor`) and follows the difference
  (`HostSlide::follow` → an immediate jump applied by the voice's existing slide path, so a slide in progress keeps
  its length and ends on the right pitch). Engines: 3x Osc, Sytrus, Harmless, MiniSynth, Sawer, SimSynth, PoiZone,
  GMS, Harmor, DX10, FL Keys, BooBass, Sakura, BeepMap, Granulizer, DirectWave, Kepler, Morphine, Toxic Biohazard,
  Ogun, BassDrum, Drumaxx and the Sampler (its rate, or the transposition of a keep-pitch voice). Nothing changes when
  the knob does not move (bit-exact). Not following: Fruit Kick (reads Final.Pitch only at the trigger,
  REA_rodada43 §1.6 [DECOMP]), Plucked! (sizes its delay line at the start, REA_rodada65 §1.2), Slayer and Transistor
  Bass (their own mono pitch models, not measured), audio clips (FL: a clip follows its fine tune only).
  [TEST] `left167_sounding_notes_follow_a_live_pitch_knob_and_main_pitch` (+2 semitones on the knob or +200 cents of
  main pitch at 0.75 s: +200 ± 30 cents at 1.25 s, for the engines FL measured), `left167_host_slide_live_jump`.

## 2. Slide notes on Drumaxx, Fruit Kick and BassDrum

- **Reporter:** do slide notes glide on Drumaxx, Fruit Kick and BassDrum?
- **FL evidence:** [FL render] `allpitch2.flp` segment `sl` (a slide note to G at bar 3): a slide never retriggers a
  hit on Fruit Kick, BassDrum, Drumpad or Ogun (silence at 4.00–4.05 s where a retrigger would sound). The synths glide
  linearly in cents over the slide note's length (+35 / +87 / +192 / +366 / +700 at 0.05 / 0.2 / 0.5 / 1.0 / 2.0 s).
  [DECOMP] Fruit Kick reads Final.Pitch only at the trigger (REA_rodada43 §1.6). BassDrum reads the host's per-note
  pitch table every chunk (REA_rodada43 §2, `v.ratio = 2^(Tp[note]/12)·g` in its process). Drumaxx reads it every
  host block (FUN_180034280, REA_rodada67 §7). The default Drumaxx kit rendered silent in FL, so the glide itself is
  [INFERENCE] from those reads plus the host moving Final.Pitch for every wrapper generator (measured above).
- **LYNX before:** no glide and no retrigger on the three.
- **LYNX now:** BassDrum and Drumaxx glide a still-ringing hit (their per-note pitch moves; no retrigger); Fruit Kick
  stays as it was (FL can't glide it). Both also follow the live knob (§1).

## 3. .flp import: "Automatic crossfades"

- **Reporter:** FL's project flag "Automatic crossfades" is not imported.
- **FL evidence [DECOMP]:** FL's project saver FUN_00f40840 writes the global byte DAT_0152c363 (via
  PTR_DAT_015e4680) as byte event **0x2C**, between 0x17 and 0x1E (sequence 0x12, 0x23, 0xD9, 0x17, 0x2C, 0x1E, 0x0A).
  Its setter is FUN_00e34030 (called by the loader FUN_00f46490); the project reset FUN_011d3790 sets it to 0, a new
  project from a template (FUN_011db460) takes the global "in new projects" setting (+0x49e). UI: AutomaticCrossfades
  BoxClick FUN_01355210, EEEnableAutomaticCrossfadesClick FUN_00f35130. [TXT] FL's demos carry 0x2C from
  FL 20.99.3000.3213 on, 0 or 1 ("Basic with limiter.flp" = 1, "Mehdi Ebrahiminejad - Never Out.flp" = 0).
- **LYNX before:** imported projects always had automatic crossfades off.
- **LYNX now:** `flp::Project::autoCrossfades` (event 0x2C in the global section) → `Song::autoCrossfades`, then the
  crossfades are derived (`clipops::updateCrossfades`). [TEST] `left167_flp_import_reads_the_automatic_crossfades_flag`
  (built .flp both ways, and the two demos).

## 4. Mixer "Render armed tracks to wave file(s)" also places the audio in the Playlist

- **Reporter:** FL also puts the rendered audio in the Playlist.
- **FL evidence [SHOT s28–s37]:** Mixer menu › Disk recording: Render armed tracks to wave file(s)... (Alt+R), Render
  selected... (Shift+Alt+R); Options: Auto-unarm, **Auto-create audio clip ✓**, Latency compensation ✓, Auto name
  playlist tracks ✓, Group loop recorded takes ✓, format. Render armed opens the "Rendering mixer track" window (Full
  song) and writes the insert's Record-to file with no file dialog. Afterwards the History shows "Insert audio clip": a
  new audio clip channel "recto" at the end of the Channel Rack, target mixer track "---", and a clip of the whole
  file from bar 1 on Track 28 = the first empty Playlist track after every used one; the track keeps the name
  "Track 28"; the insert stays armed (Auto-unarm off).
- **LYNX before:** the files were written, nothing was added to the project.
- **LYNX now:** Disk recording › Options › "Auto-create audio clip" (on by default, saved in the UI settings as
  `renderAutoClip`): after the render, one undo step adds, per file, an audio clip channel named after the file (to the
  Master) and a whole-file clip from the song start on the first free track after the used ones
  (`mixerops::placeRenderedFiles`). Inserts with a Record-to file (§7) render to it, and when every armed insert has
  one there is no file dialog, as in FL. [TEST] `left167_rendered_tracks_go_on_the_first_free_playlist_track`.

## 5. Fruity Send "Send to": old-version compatibility

- **Reporter:** the sendSpan change of 0.1.21 must keep older projects / presets / automations right.
- **FL evidence:** none needed (a LYNX file format question): FL's own Fruity Send stores the target as a 0..1 value
  over its own span.
- **LYNX before (0.1.21):** "Send to" was saved in the new 500-insert span; LYNX 0.1.20 and older read it in the 127
  span, so an older LYNX opened a newer file with wrong targets, and presets / automation points had no span at all.
- **LYNX now:** a file keeps the legacy span (127) when every target of the slot (its value, its A/B other state, its
  automation clips and pattern events) fits, and writes `sendSpan` otherwise; presets write "Send to span = N";
  automation and lane values carry their own span and are rescaled on load (missing = 127).
  [TEST] `left167_fruity_send_files_keep_the_legacy_span_when_every_target_fits`,
  `left167_fruity_send_presets_carry_their_span`.

## 6. "Add multiple" dialog

- **Reporter:** Ctrl+A should select the field's text; what does FL do after the "maximum of N" error?
- **FL evidence [SHOT s13–s19]:** "Add multiple..." opens a small inline box with **10**, already selected (hint
  "Enter the number of mi.."); End deselects, Ctrl+A selects all again (s14/s15). 999 + Enter (125 inserts used) shows
  "Error: You can add a maximum of 375 tracks."; after Ok **nothing** is added (no History entry) and the box closes.
- **LYNX before:** default "2", the field not focused; after the error nothing was added (already as FL).
- **LYNX now:** default 10, digits only, the field focused with its text selected; Ctrl+A is the text editor's own
  select-all (JUCE's TextEditor key map; the AlertWindow is its own top-level window, so LYNX's shortcuts never see
  the key). After the error, still nothing is added.

## 7. Mixer: right-click on the arm switch = FL's "Record to" dialog

- **Reporter:** right-click on arm should open FL's "Record to" file dialog.
- **FL evidence [SHOT s20–s27]:** the arm switch is the small dot at the bottom of the strip, hint "Arm disk
  recording". Right-click opens a Save dialog titled "Record <insert name> to", in Documents\Image-Line\FL
  Studio\Audio\Recorded, default name "<project title>_YYYY-MM-DD HH-MM-SS_<insert name>", type wav. After Save the
  insert is armed and selected, and its hint reads "Record to: recto.wav".
- **LYNX before:** right-click opened the input / monitoring menu (also on the input box).
- **LYNX now:** right-click on the arm switch (strip and effects panel) opens a Save dialog "Record <insert> to" in
  the project's recordings folder with FL's default name; the file becomes the insert's `recordTo` (saved with the
  project, undoable) and the insert is armed. The hint starts with "Record to: <file>". Live recordings of that insert
  write to that file (LYNX never overwrites an earlier take: "_2", "_3"... — its own choice) and "Render armed /
  selected tracks" render to it. The input / monitoring / record-safe menu stays on the input box (the notices that
  pointed to the arm's right-click now point to the input box). A record-safe insert explains why it can't be armed.

## 8. Pitch-bend recording test with injected MIDI

- **LYNX now:** `LYNX --bendrectest <report> [song|pattern]` (test mode only): records with the engine, injects a
  note on and a pitch-wheel sweep through `handleIncomingMidiMessage` while recording, stops, and checks the recorded
  channel pitch-bend lane (or clip): ≥ 20 points, values 0..1, the last back at 0.5, a span of 3–6 beats, ≥ 1 note.
  Run on the Windows build with a fresh profile (see "Where I stopped").

## Where I stopped

- 2026-10-31: items 1–8 implemented in ../lynx-wt-left167 (feature/left167). Tools in tmp/left167/py.
