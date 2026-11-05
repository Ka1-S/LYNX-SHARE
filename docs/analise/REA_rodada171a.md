# REA round 171a: issues #67, #68, #63, #66 and #62 (reported on 0.1.23)

Per item: the reporter's words | FL evidence | LYNX before | LYNX now.

## #67 Parametric EQ loses its preset after save and reopen

- **Reporter:** "40Hz + 18kHz cut" chosen (band 1 low cut 32 Hz, "Steep 8"), project saved and reopened: the readout says
  "12 dB/oct" and the curve has resonant bumps at both ends; the inactive bands' sliders show "random" values.
- **FL evidence:** FL's own preset file `Data/Patches/Plugin presets/Effects/Fruity Parametric EQ 2/40Hz + 18kHz cut.fst`
  (state 0xD5, version 6, 350 bytes): gains `322, -184, 0, 0, -520, 50, -950`, frequencies `4523, 7228, 25486, 32768, 64210,
  64613, 64901`, widths `36068, 41796, 25412, 25412, 47712, 7456, 36068`, types `3, 0, 0, 0, 0, 0, 1`, orders `-3, 0, 0, 0, 0,
  0, -3`, options 66, mask 0x7F. So FL itself stores gains, frequencies and widths on the bands it does not use (types 0):
  the sliders of those bands show FL's preset values, in FL as in LYNX. That part is not a bug.
- **LYNX before:** a slot imported from FL runs FL's exact engine and keeps FL's values in its parameters; the marker that
  says so (the "law", parameter [36]) and processing [37], linear phase [38], band mask [39], linear quality / fade
  [40, 41] (v1: FL's raw frequencies [40…46] and main level [47]) live after the 36 named parameters. The project file
  (engine/ProjectFile.cpp `effectToVar`) saved only the named ones, rounded to 4 decimals: after a reload the slot was
  LYNX's own EQ reading FL's values with LYNX's meaning (order −3 → "12 dB/oct", FL's width as Q → the resonant cuts,
  FL's band-1 gain of +0.89 dB on a cut). User presets (`presets::toText`) had the same gap (named only, `%.6g`).
- **LYNX now:** `savedEffectParamCount` = the named parameters plus every hidden one up to the last that holds a value;
  `savedEffectParamValue` = 4 decimals when that reads back as the same float, else the shortest decimal that does (FL's
  raw/65536 values come back bit for bit). Used by the project file (params and A/B) and by user presets (hidden ones as
  `#index = value` lines, which older versions ignore). Audit: every built-in effect, every parameter at an odd value,
  through the project round trip (selftest `effect_params_saved`) and the preset round trip (tests).
- **Tests:** issues171a_parametric_eq_fl_law_survives_save_and_reload, issues171a_parametric_eq_fl_presets_round_trip_exactly
  (every FL Parametric EQ / EQ 2 preset of the installation), issues171a_every_effect_param_survives_save_and_reload.

## #68 "Cut itself" out of sync between the rack menu and the sample panel

- **Reporter:** ticking it in the Channel Rack's menu does not tick the sample panel's checkbox, and the other way round.
- **FL evidence:** FL has one control, "Cut self" (hint "Cut itself when retriggered"), in the channel's Misc functions cut
  panel: FLEngine_x64.dll form resource `forms.pluginform.funcsheet.cutpanel.cutitselfbtn`, OnClick `CutItselfBtnClick`
  (method table → 0xF67C60), which calls 0xFEAAF0 (channel, true, true): `[ch+0x27C] = a group (0xFED900)`,
  `[ch+0x280] = [ch+0x27C]` — Cut and By set to the same group. One state, the cut group pair.
- **LYNX before:** the rack menu and the Misc page wrote the cut group pair (`cutGroup == cutBy`), the sample panel a
  separate sampler flag (`sampler.cut`); each read only its own.
- **LYNX now:** one state: `channelCutsItself` = the pair is the channel's own **or** the sampler flag (projects saved by
  the panel before), `setChannelCutsItself` writes both; the rack menu, the sample panel and the Misc page all use them.
- **Test:** issues171a_cut_itself_is_one_state.

## #63 Per-note pan not heard

- **Reporter (Linux, PipeWire):** the default project's Snare, pan drawn in the Channel Rack graph editor (and seen in the
  piano roll): no effect; the mixer pan works.
- **Cause:** the default project's Kick / Clap / Hat / Snare are LYNX's built-in drum generators (`DrumInstrument`), which
  ignored `setNextNoteExpression` (pan and fine pitch). The Sampler, the synths and most native generators already apply
  note pan (test `note_pan_pitch_and_mute`). Not Linux-specific.
- **LYNX now:** the drums take the note's pan (constant-power, as the Sampler's voices) and fine pitch (the kick's and
  snare's tone); centred notes are bit for bit as before.
- **Other lanes:** velocity reaches every generator; release velocity reaches hosted plugins (note-off velocity);
  Mod X → polyphonic aftertouch on hosted plugins. Not done here (open): Sampler release velocity / Mod X / Mod Y (FL routes
  Mod X/Y to the channel filter), note pan / fine pitch to hosted plugins (no VST3 note expression in JUCE's host),
  note pan through the Arpeggiator.
- **Test:** issues171a_note_pan_reaches_the_drums (each drum alone, and the default project's Snare through the rack).

## #66 Peak Controller display does not move

- **Reporter (Linux):** the Peak / LFO lines and the knobs do not animate.
- **Cause:** the editor reads the slot's live meters, which the audio thread publishes only for the kinds in
  `hasLiveMeters` (RackRenderer.cpp); the Peak Controller (display added in round 168r) was not in it, so its lines stayed
  at 0 on every platform. On a silent insert smart disable also stopped it (its LFO and its peak's fall stopped).
- **FL:** Fruity Peak Controller's knobs are its settings (they do not move with the sound); its display scrolls the
  outputs while the project runs. LYNX now does the same.
- **LYNX now:** published like the XY controllers, and never smart-disabled (as the other controllers).
- **Test:** issues171a_peak_controller_publishes_its_outputs (the LFO line moves in silence, the peak line rises with input).

## #62 Linux: a detached Mixer is not detached after a restart

- **Reporter (AppImage):** Mixer detached, LYNX restarted: docked again.
- **Measured:** the old binary under Xvfb (no window manager) restores a detached Mixer from workspace.txt and saves it
  again. Under WSLg (Weston's window manager) it is restored and stays detached, but at the manager's place (saved
  300,200 → shown at 6,27, and 6,27 was then saved). Screenshot: /mnt/e/lynx-work/issues171/check/i62-before-wslg.png.
- **LYNX before:** (1) the layout was saved only when LYNX closed cleanly (a Linux session that ends otherwise loses it);
  (2) the layout file was written with `%.5f` in the C locale: a plugin's GTK switching LC_NUMERIC to a decimal-comma
  locale made the whole file unreadable on the next start (default layout, Mixer docked); (3) window managers place a new
  window themselves, and its first "moved" was saved over the remembered place; (4) the restored windows were built while
  the main window was hidden (splash) and before it was set as the owner: on a window manager the main window shown later
  can cover them (Windows: they were not owned).
- **LYNX now:** the layout is saved at once when a panel is detached or docked; the file is written with a decimal point
  whatever the locale and a comma file still loads; a restored window goes back to its saved place until it settles
  (~2 s, the manager's own placement is not saved); after the main window comes up the detached panels are re-owned (Windows),
  put back at their place and raised above it. Same code for Mixer, Channel Rack, Piano Roll, Playlist and Browser, on
  Linux, Windows and macOS.
- **Test:** issues171a_detached_windows_survive_a_restart (+ GUI checks below).

## GUI evidence (Windows, fresh profile, LYNX_TEST_MODE=1)

Screenshots in `E:\lynx-work\issues171\check\`:
- #67: `b-*` = preset "40Hz + 18kHz cut" loaded, saved (42 params incl. [36]=0.5, [39]=1); `c-eq-reopen.png` =
  the same steep 40 Hz / 18 kHz curve after reopening the saved file.
- #68: `d-menu-after-cut.png` = "Cut itself" ticked in the rack menu after clicking it; `d-settings.png` = the
  sample panel "Cut itself" checkbox ticked for the same channel.
- #66: `b-peak1.png` / `b-peak2.png` = Peak Controller LFO display scrolling while playing.
- #62: Linux WSLg `i62-before-wslg.png` (re-placed by the WM) vs `i62-after-wslg*.png` (kept at 300,200 over two
  restarts, above the main window).

Round 171b Patcher round-trip losses (splitter, Fruity Delay 3, Peak Controller) were the 4-decimal rounding in
`effectToVar`; `savedEffectParamValue` writes the shortest exact float, and the selftest `effect_params_saved`
compares the Patcher nodes with full `EffectSlot ==`.

## Open (not fixed in 171a)

- #63: sampler release / Mod X / Mod Y per-note lanes not routed; note pan / fine pitch do not reach VST3
  (JUCE host has no note expression); the Arpeggiator drops note pan.
- FLP-imported mono channels (`maxVoices==1` -> `sampler.cut`) now show "Cut itself" ticked (one state, as FL).

## Where I stopped

- Done: code, tests, Linux + Windows GUI checks, merge of origin/main.
- Next: build-local + Linux selftest after the merge, then `train.sh issues171a`.
