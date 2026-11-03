# REA round 169h — Toxic Biohazard: MIDI and SEQ pages

Leftover of round 168n: *"not covered: Toxic's MIDI / SEQ pages (its sequencer isn't modelled), CC slots 316–339"*.
This round studies FL's own `Toxic Biohazard_x64.dll` (clean-room: the behaviour is written down here and LYNX is
implemented from this text; no code, strings or artwork are copied) and builds both pages and the engine behind them.

- **Binary:** `Toxic Biohazard_x64.dll`, SHA-256 `5c326637be38fcea7148e170b83e6bec8600c87f00fa774eb610ed68f136cb46`
  (the same build as REA_rodada45), image base 0x180000000.
- **Tools:** Ghidra 12.1.4 headless (own project, one instance, 2 CPUs), BFS decompiles from the vtables, a small PE
  constant reader for floats / doubles / strings. No FL session was needed (no plugin scan, no presets touched).
- **Evidence tags:** DECOMP = decompiled code read; DATA = constants read from the image; TEST = LYNX unit test of the
  documented behaviour (`tests/Toxic169hTests.inc`).

## 1. Items

| # | Reporter's words (168n leftover) | FL evidence (addresses) | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 | "its sequencer isn't modelled" — Seq Play | `FUN_180040240` case 157 → `FUN_180025f00`: when the flag changes every key 0…127 is released through vtable 0x10 with the sustain value (+0x1368 = CC64) forced to 0, then +0x398c = (x ≥ 0.5). `FUN_1800406d0` calls the clock `FUN_18001bbb0(seq, fs, tempo, n)` at the top of every chunk while the flag is set [DECOMP] | parameter 157 ignored; keys always played the synth | `Engine::seqPlay` / `arpTick` at the top of `process`; the keys feed the pattern instead of the synth while it is on [TEST `toxic169h_sequencer_steps_and_clock`] |
| 2 | — the 64 steps | 163…226: `FUN_18002ecf0(seq, i, acb(x, −26, 24))` (clamped −26…24) then the rebuild; value text `FUN_18003b1b0`: −26 "---", −25 "SUS", else "%d" [DECOMP + DATA `0x18008788c` = "%d"] | not listed, not edited | "Seq step 1…64" (units −26…24, text ---/SUS/n); 64 number boxes on the SEQ page |
| 3 | — Steps, Swing, Ping-pong, Random, Dual | 162 = acb(x, 1, 64) → +0x3abc; 158 swing +0x3abb, 159 P-P +0x3ab9, 160 Rnd +0x3aba, 161 Dual +0x3ab8, each x ≥ 0.5 (`DAT_18008bdb0` = 0.5), each followed by the rebuild `FUN_18002ed20` [DECOMP + DATA] | not modelled | `Engine::seqParam`; listed parameters "Seq steps / swing / ping-pong / random / dual" |
| 4 | — how a step becomes a note | `FUN_18002ed20`: lowest held key L (velocity table +0x1068 ≠ 0; none → stop `FUN_18001c110`); gain = vel(L) clamped (`FUN_18001be90`); start `FUN_18001c140`. Pitch classes of all held keys → chord: type 0 = anything held with root = highest class, then in order (last match wins): k,k+3 → 1; k,k+4 → 2; (0,3,7) → 1; (0,4,7) → 2; (0,3,10) → 3; (0,4,10) → 4; (0,3,7,10) → 3; (0,4,7,10) → 4; (0,3,11) → 5; (0,4,11) → 6; (0,3,7,11) → 5; (0,4,7,11) → 6; (0,3,6) → 7. Scale tables (12 for octave 0, 12 for other octaves) built inline per type (§2). Step v: −26 → −1 rest, −25 → −2 hold, else oct = ⌊v/12⌋, i = v mod 12, note = root + T[oct = 0 ? i : 12 + i] + 12·(oct + ⌊L/12⌋); < 0 → += 12·(~note/12) + 12; > 120 → += 12·(9 − note/12) [DECOMP] | — | `Engine::rebuild` + `stepNote` with the same tables [TEST `toxic169h_sequencer_chords_dual_and_stop`] |
| 5 | — Dual | rebuild: Dual → steps = min(steps, 32) and lane 1 of step k = lane 0 of step k + 32 (k = 0…31) [DECOMP `FUN_18002ed20` tail] | — | same [TEST] |
| 6 | — timing (sequencer driven by the tempo) | `FUN_18001bbb0`: when due, fix the position per mode (forward: ≥ steps or < 0 → 0; ping-pong: < −1 → 0 up, < 0 → 1 up, > steps−1 → steps−2 down, clamp; random: ⌊rnd·steps⌋ clamped), play the 8 lanes (−2 keeps the lane's note; else the lane's old note off through vtable 0x1c8 with velocity 0, the new one on with lane velocity × gain), advance; then dur = 1 + swing (toggle 0) or 1 − swing (toggle 1), phase += n·((1/15)/1.0·tempo)/fs, while phase ≥ dur: phase −= dur, toggle flips, due. 1/15 = `DAT_18008bee0` (0.0666…), 1.0 = `DAT_18008c000`, swing 0.25 = `DAT_18008bf08`. Reset `FUN_18001bde0`: phase 0, toggle 0, due 1, pos −2, up, lanes −1 [DECOMP + DATA] | — | `Engine::arpTick`; a step = a 16th (5292 samples at 125 BPM / 44.1 kHz) quantised to the 64-sample chunks (5312) [TEST: onsets 0 / 5312 / 10624 / … and with swing 0 / 6656 / 10624 / 17216] |
| 7 | — Random mode's generator | `FUN_180025ed0`: global `DAT_18009ee80` (start 2222) = ·0xbb38435 + 0x3619636b, value·2⁻³²; also drawn by every synth note-on with velocity ≠ 0 (`FUN_1800250b0`, `FUN_1800256a0`) — a different global from the oscillators' 0x18009ee84 [DECOMP] | — | `Engine::seqRandom` per instance (FL's is process-global, as the phase generator in REA_rodada45 §7) |
| 8 | "CC slots 316–339" — the MIDI page | builder `FUN_1800304b0`: 8 rows (y 156 + 17i): source combo (x 376), amount box (x 448), destination combo (x 470). Sources 0 "---", 1 velocity, 2/3 LFO 1/2, 4 aftertouch, 5 pitch wheel, (6 = the "Channel Controllers" submenu), 7…134 = CC 0…127. Destination raw = 1 + parameter (0 = none). Amount text `(int)(200x − 100)` ("%ld"; `DAT_18008c200` = 200, `DAT_18008c1f0` = 100), engine law acb(x, −99, 100)/100 [DECOMP + DATA] | 316–339 loaded from the state but not editable; MIDI sources read as constants | the MIDI page (8 rows: source menu, amount box, destination menu), listed "Mod n source / destination / amount" |
| 9 | — the slots' MIDI sources | `FUN_1800413f0`: 1 → +0x3c58 (velocity), 2/3 → LFO tables, 5 → +0x1468, 4 → +0x166c, 7…134 → CC table +0x1268[t − 7]; clamp 0…1 for parameters < 294 [DECOMP] | CC table = constants (CC7 1, CC10 0.5, others 0), aftertouch 0 | `Engine::cc[128]`, `aftertouch` fed by the controller's MIDI [TEST `toxic169h_modulation_slot_midi_sources`] |
| 10 | — how CCs reach the engine | wrapper `FUN_1800034b0` (ProcessParam): indices ≥ 294 are "MIDI CC 0…127" and "Chn. Aftertouch" (k = 128); value v·(1/256) clamped, v7 = int(v·127); CC 7, 10 and 32 are **not** forwarded (mask 0x100000480); CCs go to vtable 0x20 = `FUN_180007aa0` (table = v7/127, then `FUN_1800411a0`), aftertouch to 0x30 = `FUN_180007b00` (+0x166c, then every slot target again). Pitch bend `FUN_180007a80` (+0x1468) has no caller in the wrapper [DECOMP] | — | `ToxicInstrument::midiMessage` (0xB0 / 0xD0 from a controller, round 162's live path) → `controlChange` / `channelPressure`, CC 7/10/32 ignored as FL's wrapper |
| 11 | — CC side effects | `FUN_1800411a0` → `FUN_180025d20`: CC64 up → keys already let go (velocity 0, still sounding) released through vtable 0x138; CC121 → table cleared, CC7 = 1, CC10 = 0.5, every CC (but 32/64/121/123) applied again; CC123 → every key released (sustain forced 0); others → the **CC link map** (the state's 294 bytes after the parameters, parameter → CC, 0xFF none): each linked parameter takes the CC's value. Then (CC ≠ 7, 10, 32) every slot target again; CC32 = bank select (preset change) [DECOMP] | not modelled | `ccSideEffects` / `ccChanged`; the link map read from the state (`Patch::link`) [TEST `toxic169h_cc_link_map_and_state`]; CC32's bank change not modelled |
| 12 | — sustain pedal | `FUN_180025fe0` (key up): returns at once while CC64 ≠ 0 (the rebuild still runs, so the sequencer sees the key up) [DECOMP] | not modelled (REA45 §7) | `Engine::keyUp` [TEST `toxic169h_seq_off_plays_keys_and_sustain`] |
| 13 | — the host key path | vtable 0x08 `FUN_180007a20`: velocity table +0x1068 = v7/127, then 0x130 = `FUN_180041140` (`FUN_180025f80` + rebuild) or 0x138 = `FUN_180041170` (`FUN_180025fe0` + rebuild). `FUN_180025f80`: held table byte = vel·127; Seq Play on → the generic arpeggiator update `FUN_18001bfd0` (only its start / stop lasts: the rebuild overrides it), off → synth note 0x1c8 [DECOMP] | `Engine::noteOn` = synth note directly | `noteOn` / `noteOff` (host) → `keyDown` / `keyUp` → `synthNote` or the pattern |
| 14 | — rate set | `FUN_180024e80`: every key released (sustain forced 0), voices re-rated, then the sequencer reset + stop [DECOMP] | voices only | same |
| 15 | — the SEQ page | builder `FUN_180030f30`: Play (157) image button, Swing (158), P-P (159), Rnd (160), Dual (161), "Number of steps" box (162); 64 number boxes in 8 rows of 8 (x = 375 + 21k; rows 149…197 and 221…269, two blocks of four rows) with row labels 0, 8, … 56 and 4, 12, … 60 (Artwork `bg_seq`, looked at only) [DECOMP + artwork] | — | the SEQ page in LYNX's style: the same 8 × 8 grid in two blocks, labels, number of steps, five switches |
| 16 | — the page switch | the centre panel's tabs MOD / MIDI / SEQ (Artwork `background`, looked at only) | one page (MOD) | `pageTabs` Mod / MIDI / Seq on the Modulation section; the old content is the Mod page |

## 2. Scale tables of the rebuild (DECOMP, inline constants of `FUN_18002ed20`)

Offsets in semitones above the root for a step's degree 0…11; first row = octave 0, second = the other octaves.

| Type (chord) | Octave 0 | Other octaves |
|---|---|---|
| 0 (no chord) | 0 1 2 3 4 5 6 7 8 9 10 11 | same |
| 1 (minor) | 0 1 2 3 3 5 6 7 8 8 10 10 | same |
| 2 (major) | 0 1 2 4 4 5 6 7 9 9 11 11 | same |
| 3 (minor 7) | 0 1 2 3 3 5 6 7 8 10 10 10 | −2 1 2 3 3 5 6 7 8 10 10 10 |
| 4 (dominant 7) | 0 1 2 4 4 5 6 7 8 10 10 10 | −2 1 2 4 4 5 6 7 8 10 10 10 |
| 5 (minor maj7) | 0 1 2 3 3 5 6 7 8 11 11 11 | −1 1 2 3 3 5 6 7 8 11 11 11 |
| 6 (major 7) | 0 1 2 4 4 5 6 7 8 11 11 11 | −1 1 2 4 4 5 6 7 8 11 11 11 |
| 7 (diminished) | 0 1 2 3 3 5 6 6 8 8 10 10 | same |

## 3. State

The TBi2 state already carries everything (REA_rodada45 §2.1): parameters 157–226 (sequencer) and 316–339 (slots)
among the 340 floats, then the 294-byte CC link map. The loader (`FUN_180041520`, DECOMP this round) copies the
0x40-byte slot header first and then sends all 340 parameters through the setter, so 316–339 win over the header; LYNX
reads and writes the parameters and now also reads the link map.

## 4. Not done / remaining

- FL's **"MIDI CC n" / "Chn. Aftertouch" automation parameters** (wrapper indices ≥ 294): an FLP automating them would
  need a remap on import (LYNX's channelParam indices 295+ are Toxic's internal values). Live controllers reach the
  engine (item 10); imported CC automation does not.
- The editor's option-menu commands **Import Sequencer Pattern / Reset Sequencer / Import CC Map / Reset CC Link
  Map / Import MIDI Modulations / Reset MIDI Modulations** (the loader's partial modes 1 and 3) are not built.
- **CC32 bank select** (preset change by CC) is not modelled.
- Linking a parameter to a CC from LYNX's UI (FL's per-control "link to controller" writes the link map) is not built;
  a state that has links plays them.
- The **pitch-wheel source** stays 0, as in FL (the wrapper never writes it).
- The sequencer's generator is per instance (FL: process-global).

## 5. Where I stopped

- REA complete (items 1–16). Engine, parameters, layout pages, stepper units, tests written.
- Next: full build + tests, i18n, Windows build, shots, wip push, train.
