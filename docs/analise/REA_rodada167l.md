# Round 167l (v0.1.22) — Sampler legacy effects, export extras, Alt solo, channel lock, CC 123 on reset

Clean-room: Ghidra on FL Studio 2026's own `FLEngine_x64.dll`, its DFM form resources and published field / method
tables; command-line renders by FL itself (`FL64.exe /R`, both FL locks held). Behaviour and formats only; nothing of
FL's code is copied.

Markers: **[DECOMP addr]** Ghidra pseudocode, **[DFM]** a form resource, **[DATA]** FL's demo projects, **[FL render]**
FL's command-line render, **[GUI]** LYNX driven on Windows (fresh profile, `LYNX_TEST_MODE=1`), **[TEST]** LYNX tests
(`tests/Rea167lTests.inc`).

(sections are filled as the items land)

## 4. Mixer: "Alt solo selected track(s)" (Alt+S)

- **Brief:** "Alt+S (prove in FL first)".
- **FL evidence:**
  - [DFM] `/FXForm/EffectsMenu`: `SoloMenu` "Solo selected track(s)" ShortCut 83 (S) and `AltSoloMenu` Tag 1 "Alt solo
    selected track(s)" ShortCut 32851 = 0x8000 (Alt) | 'S'. Both items share the handler 0x12abe00.
  - [DECOMP 0x12abe00 → FUN_0127c2a0 (−1 = the selected tracks, 2, Tag == 1 ? 3 : 2)] the solo of the selection with
    flags 2 (plain) or 3 (alt). [DECOMP FUN_0127bb10 / FUN_0127bab0] flag 2: the target and the tracks it reaches
    (its path to the Master stays heard); flag 1 (the alt bit): also the tracks that reach the target (its sources).
    [DECOMP FUN_0127c640] the reach test is transitive over the route bytes (+0x398). Locked tracks (+0x1b) are
    skipped by the solo.
  - Toggle [DECOMP FUN_0127c2a0]: alt on tracks already soloed: an alt solo goes away, a plain one becomes alt;
    plain: an alt solo with sources becomes plain, a plain solo goes away, anything else is soloed plain.
- **LYNX before:** one solo kind: `Song::soloAudible` always kept both the upstream and the downstream tracks (= FL's
  alt solo); the mixer menu's "Solo selected track(s)" (S) soloed with that; no Alt+S.
- **LYNX now:** `MixerTrack::soloSources` (true = LYNX's strip solo and FL's alt solo; saved in .lynx only when
  false, compared by undo). `mixerops::soloSelected (song, targets, alt)` is FL's toggle; "Solo selected track(s)\tS"
  is the plain one (only the path to the Master), "Alt solo selected track(s)\tAlt+S" the alt one, in the mixer's
  menu and as keys (`mixer.soloSelected`, `mixer.altSoloSelected`). `soloAudible` keeps the upstream tracks only of
  tracks soloed with their sources. The strip's solo button stays LYNX's (with sources). [TEST]
  `rea167l_mixer_alt_solo`.

## 6. CC 123 to the MIDI outputs on a transport reset

- **Brief:** "CC123 to MIDI Out on transport reset".
- **FL evidence:** [DECOMP FUN_011e7a10] setting the song position (rewind, a click on the ruler, a jump) with "Reset
  plugins on transport" on (Settings form +0xb50) calls [DECOMP FUN_011c89c0]: for every open MIDI output device of
  the list 0x15e4560 whose "Send all notes off" flag is set (+0x49, written by `SendAllNotesOffBoxClick` 0x135cd70),
  `midiOutShortMsg (0x7bb0 + channel)` for the 16 channels: CC 123 value 0 only (the start-up / after-render reset
  also sends CC 64 = 0). The MIDI Out *plugin* gets the generic plugin reset (round 167k).
- **LYNX before:** "Send all notes off" outputs got CC 64 + CC 123 at start-up and after a render only.
- **LYNX now:** the audio thread calls `MidiSyncIo::requestTransportNotesOff()` on a rewind or seek when "Reset
  plugins on transport" is on (a counter; it never touches a device); the MIDI sync sender thread (now also running
  when an output only has "Send all notes off") sends `midisync::transportNotesOff` (16 × B0+ch 7B 00) to those
  outputs. [TEST] `rea167l_transport_notes_off_bytes`; `--midisync-test` reports the burst through the sender thread
  in dry run ("transport notes off: bursts=1").


## Where I stopped

- In progress: item 1 (Sampler legacy panel) — REA done (see §1 once written), FL test project
  `tmp/r167l/py/mk167l.py` → `C:\Users\kauan\lynx-test\r167l\fl\l_fx.flp` (not rendered by FL yet).
- REA notes so far: tmp/r167l/d*.c (decompiles), m*.txt (constants). Alt solo = FUN_0127c2a0 flags 3; channel lock
  0x20 = SoloLockMenuClick 0x1213320 → FUN_00ffac40; CC 123 = FUN_011c89c0 (outputs with "Send all notes off").
- Rerun: `tmp/r167l/g.sh <out.c> 0 <hex addr…>` (decompile), `SCRIPT=Mem.java|Dump.java|Off.java tmp/r167l/g.sh …`.
