# REA round 169h — Ogun / Autogun live automation, Ogun's X/Y pad, Autogun in the editor pictures, FL Slayer's description

Clean-room: the behaviour below was read from FL Studio 2026's own `Ogun_x64.dll` (SHA-256
`c2c717df…a1e4c6`, Ghidra 12.1.4 headless, project `~/ghproj/r57_og`, our scripts `tmp/r169h/gh/DecTree.java`,
`DecRange.java`, `DecAt.java`; decompilations `tmp/r169h/pp.c`, `fx.c`, `rv.c`, `at.c`) and from its editor form
(`TSynthEditorForm`, read in round 168o: `tmp/r168o/Ogun/TSynthEditorForm.dfm.txt`). Autogun_x64.dll shares this code
(REA_rodada57 §1.1: `FUN_008684e0` is identical). No code or art was copied. Toxic Biohazard's MIDI / SEQ pages are in
`REA_rodada169h-toxic.md` (sub-round).

| # | Item (coordinator's words) | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 | "Ogun and Autogun: live Playlist clip automation of their parameters must drive the engine in real time like FL" | §1 | clips bound (`modelparams::find`) but `OgunInstrument` had no `setParameter`: nothing happened; a knob edit rebuilt the engine (held notes cut) | §1: `Engine::setParam` applies exactly what ProcessParam recomputes; knob edits go the same live way |
| 2 | "Ogun editor: the XY pad's dot sits half on the pad's left border at its default value" | §2 (form + driven FL) | a filled glow + ring drawn over the pad's border (outside half visible), one centre cross | §2: FL's 4 × 4 grid and small ring, clipped to the pad |
| 3 | "Autogun is missing from the --instrumentshot tool" | §3 | Autogun not in `natives::instrumentEngines()` (no preset files) → never pictured | §3 |
| 4 | Toxic Biohazard MIDI / SEQ pages | REA_rodada169h-toxic.md | | |
| 5 | "FL Slayer editor: description cut with '…' at 800 px wide" | LYNX-own line (no FL equivalent) | one `drawText` line, ellipsis | §5 |

## 1. Ogun / Autogun parameters while playing

### 1.1 FL's ProcessParam (`FUN_008684e0`, VMT slot 6) [DECOMP]

- Index mapping (`FUN_008698e0`): index `i < 0x31` (0…48) is `P[i]` (plugin `+0x12c + 4·i`); `49 ≤ i < 89` is an
  articulator knob: articulator `(i − 49) / 8` (0…4), knob `(i − 49) mod 8` (0…3 envelope knobs `+0x98`, re-scaled by
  `FUN_00789c70`; 4…7 LFO knobs `+0xd0`, LFO speed recomputed through the curve object's VMT `+0x40`). Indices ≥ 89 do
  nothing. So **FL's automation targets are P0…P48 and the knobs of articulators 0…4** — Richness (P52), Quality (P54),
  filter type / order (P49…P51) and the other settings are not automatable.
- Value: with flag `0x20` (a host value 0…65536, automation / links) the plugin takes
  `min + round(value/65536 · (max − min))` (banker's rounding `FUN_0040c3e0`) of the control's range (`+0x3a8 / +0x3ac`
  of the control object).
- With the "set" flag (bit 0), after storing P[i]:
  - P0…P2 → `FUN_0086a570`: master level `+0x5bc` (`(3^(P0/128) − 1)/2`, 1 at 128) and tuning `+0x5c0 = P1·100 + P2`.
  - P3, P4 → `FUN_0086a610`: `+0x5cc / +0x5d0 = P/512` and a refresh flag; the voices read P3 / P4 at every control
    block (LYNX `Engine::control`, smoothing `xysmooth` when flag `+0x208`).
  - P5…P9 (under the plugin lock, VMT `+0x100 / +0x108`): pre-decay `+0x248`, fullness `+0x24c`, decay `+0x250` (−1 at
    10000), release `+0x254`, morph `+0x258`; P10, P11: the seeds `+0x23c / +0x240` (`mix32(P − 0x20cf)` when P ≥ 1,
    else −1). The voices copy these at note start (`FUN_00842ee0`), so a change reaches the next notes.
  - P12…P16 (unison): nothing but the stored value (read at note start), except P15 → `FUN_0086c530`: every active
    voice's sub-voice detune factors are recomputed at once.
  - P17, P18 → filter cutoff / resonance `+0x530 / +0x534` = P/256 (read by every control block).
  - P19…P25 → `FUN_0086a2d0` → `FUN_00840690` (chorus): order 0 frees the chorus object, a chorus switched on is a new
    object; otherwise the taps are recomputed (SetLength keeps the running phase `[4]` of existing taps, new taps start
    at 0) and only when the ring's length (`FUN_0083cd50` returns true) or the spread (`+0x48` vs `+0x30`) changed are
    all phases restarted (`[5] → [4]`) and the ring cleared (`FUN_00840d30` → VMT 0).
  - P26…P30 → `FUN_00869dd0` (delay): stereo offset, length (tempo-synced when the sync byte `+0x358` is set),
    feedback, volume, mode recomputed; `FUN_0083cd50(line, block + offset, length)` re-allocates and clears the line
    (read point restarted) only when its total size changes (`FUN_0083ccf0`).
  - P31…P39 → `FUN_0086a0a0` (reverb, FL's shared `Reverb_x64.dll` behind the wrapper `0x83f…`): a new predelay length
    calls the wrapper's VMT 0 = `FUN_0083f650` (lines re-allocated) → `FUN_0083f5d0` (every line and state zeroed); the
    other fields set the dirty flag, and the next block's `FUN_0083f840` recomputes the coefficients and line lengths,
    keeps each line's position modulo its new length and zeroes the 12 lines only when the room size differs from the
    last one (`+0x1e8`).
  - P40…P48 → `FUN_0086a670(band = (i − 40) mod 3)`: that band's level / frequency / width and its biquad
    coefficients (`FUN_00625290`); the filter state is kept.
- Autogun's Magic dust (P63, REA_rodada57 §1.4): automation updates P63 only (the stage's on/off test in Gen_Render);
  the knob handler `FUN_0088d930` also sends the band mix to the hosted Soundgoodizer.

### 1.2 LYNX

- `Engine::setParam (index, value)` (src/core/Ogun.cpp) does the same per index on the engine's own patch copy:
  `designMaster`, `designTimbre`, `designFilterKnobs`, `designEqBand`, and `liveChorus` / `liveDelay` / `liveReverb`
  with FL's keep / clear rules above. `buildPatch` now calls the same `design*` helpers, so a fresh engine and a live
  change compute identical values.
- `OgunInstrument::setParameter` (automation clips, links, the Event editor): FL's value law on the knob range of
  `ogunEngineParams()` / `autogunEngineParams()`, indices 0…48 and Autogun's 63; Richness / Quality are ignored like
  FL. The automated values are re-applied to an engine rebuilt later (tempo, sample rate).
- The editor's knobs: `configure` compares the new state with the loaded one; when only P0…P48 differ the values go
  to the audio thread through a `LiveParamQueue` (as FL's knobs call ProcessParam) — the held notes keep playing.
  Magic dust from the knob still rebuilds (the band mix must reach the stage).
- Not done: the articulator knobs (FL indices 49…88) — LYNX's Ogun page does not show the articulators' knobs, so they
  can't be automated from LYNX, and FL projects' clips on them are not bound. LYNX's automation index is the P index,
  so an FL clip on index 52 (articulator 0, knob 3) would bind to Richness and do nothing.
- Tests: tests/Inst169hTests.inc (`inst169h_ogun_automation_is_live`, `..._knob_edits_keep_the_note...`,
  `..._automation_clip_drives_the_engine`, `inst169h_autogun_shot_channel`).

## 2. Ogun's X / Y pad

- FL: `ModVisual: TQuickXYZ` (Left 99, Top 8, 77 × 77, `CursorStyle = QXYZCS_Ogun`, X / Y selectors `ModXWheel` /
  `ModYWheel`: Max 512, **Value = DefaultValue = 256**) [DATA: the form]. So at FL's default the cursor is in the middle;
  the preset FL's Ogun channel starts from ("300 Ft Glass Bowl") has Mod X = 0, so the dot sits on the left edge in
  both programs — the value is right.
- Driven FL (one session, both locks, FL closed by its PID 23472; shots `/mnt/e/lynx-work/r169h/fl-ogun-open.png`,
  `fl-ogun-x0.png`, `fl-ogun-x0b.png`): `Data/System/Render tests/Ogun.flp` opened with its editor; at X = Y = 256 the
  cursor is a small hollow ring in the middle of a pad with a 4 × 4 grid; after dragging the X wheel to its minimum the
  ring's centre is on the pad's left edge and **only its inner half is drawn** — FL clips the cursor to the pad.
- LYNX before: a 16-unit filled glow plus a 10-unit ring, not clipped, so half of it was painted over the pad's border
  and the section background, and only one centre cross. LYNX now (ChannelSettings `xy` display): the 4 × 4 grid (the
  centre lines a little stronger), the 10-unit ring in the accent colour, clipped to the pad like FL.

## 3. Autogun in `--instrumentshot`

- Autogun has no preset files (`Plugins/Fruity/Generators/Autogun/Data` holds `Maps`, `Random pad`, `SG.dat`; its
  patches are numbers) [DATA], so it is not in LYNX's Add menus. The shot tool now takes `autogun` and pictures FL's own
  `Data/System/Render tests/Autogun.flp` channel (`natives::autogunShotChannel`), imported with SG.dat.

## 5. FL Slayer's description

- LYNX-own text (FL has no such line). The description is now drawn with `drawFittedText` in `descriptionArea()`: two
  lines above the knob grid (whose captions start at y 32), one line slightly condensed on designed pages, clear of the
  sample button.

## Windows checks

`LYNX.exe --instrumentshot E:\lynx-work\r169h\shots ogun,autogun,flslayer fl` (fresh LYNX_PROFILE_DIR, LYNX_TEST_MODE=1,
under the GUI lock), all at 100 / 150 / 200 %, looked at:
- `ogun-fl-*.png` (800 × 789): the pad shows FL's 4 × 4 grid, the ring's centre on the left edge (Mod X = 0 in "300 Ft
  Glass Bowl") with only its inner half drawn — as in `fl-ogun-x0.png`.
- `autogun-fl-*.png` (800 × 789): FL's render-test Autogun channel ("Wide whistle 3"): the Autogun page with Magic
  dust, Seed 1 3475, Seed 2 Random (0), the ring in the pad's lower-right corner (X high, Y low), clipped like FL.
- `flslayer-fl-*.png` (800 × 609): the description line now fits in one line at 800 px (condensed a little) instead of
  ending in "…".

## Where I stopped

- Done: §1–§3, §5 with tests (build-local: all Inst169h tests pass; the one other failure, Feat166d's idle-insert
  timing, is a load-sensitive timing check unrelated to this round), Windows shots above. §4 is the sub-round's
  (`REA_rodada169h-toxic.md`, branch feature/toxseq169h).
- Landing: `coord/train.sh inst169h`.
