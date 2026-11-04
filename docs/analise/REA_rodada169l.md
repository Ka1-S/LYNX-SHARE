# REA round 169l — Drumaxx's level, a stateless Drumaxx, what a NEW generator is in FL

Evidence markers: [DECOMP] Ghidra / objdump on FL's own binaries; [DATA] FL's own files read byte by byte; [FL render]
FL Studio 2026's command-line render (`FL64.exe /R /Ewav`, both FL locks, one launch); [CODE] LYNX source; [TEST] LYNX
tests. Clean-room: behaviour only, nothing of FL's is copied or shipped (the test project is built at run time from the
user's own FL files and stays under `C:\Users\kauan\lynx-test\gen169l`).

## 1. Drumaxx +3.0 dB: the host's pan law was missing [DECOMP][FL render]

Round 169e: rendering `drumaxx169e\fl\dmx169e.flp`, every LYNX pad came out +3.0 dB over FL's render.

- The ratio LYNX / FL is **1.4142** on both sides of every pad (per-note peaks, `cmpw.py`); scaled by 1/√2 the whole
  LYNX render equals FL's within the 16-bit render's LSB (RMS difference 8.8·10⁻⁶ in every 100 ms window, tails
  included). So the engine is right; one √2 factor is missing in front of it.
- **Where** (`Plugins/Fruity/Generators/Drumaxx/engine_x64.dll`, the FL-plugin side of the engine, image base
  0x180000000): `FUN_180003e60` runs every block. For each key's host voice it loads **Final.Pan** (`[voice+0x14]`, into
  xmm3) and **Final.Vol** (`[voice+0x18]`, on the stack) and calls host slot 31 (`call [rax+0xf8]` at `0x180003efe`,
  the SDK's `ComputeLRVol(&L, &R, Pan, Vol)`), then stores L and R in the engine's per-key tables (`+0x20` / `+0x220`,
  the `keyL` / `keyR` the pads' gains are scaled by), the pitch offset in `+0x420`; keys without a voice take the
  nearest held voice's values. It is the same framework code as Sakura's and Drumpad's wrapper (REA_rodada28 §4.2).
- FL's ComputeLRVol (REA_rodada28 §4.1, `FLEngine_x64.dll` `FUN_006a3a00`, law A): `L = Vol·cos((1 + pan)·π/4)`,
  `R = Vol·sin(…)` — **0.7071·Vol at the centre**.
- Round 67's harness (`r67/run/dm.c`) stood in for slot 31 with `L = R = vol` (no pan law), so the port took
  "keyL = keyR = Final.Vol" and concluded "Final.Pan is ignored". Both were artefacts of the stand-in: the engine is
  still bit-identical to the DLL for given tables; what FL hands it is `Vol·cos/sin`, and **the pan does count**.

LYNX now (`core/Drumaxx.cpp`, `DrumaxxInstrument::keyLevels`): each key's tables are law A of (note pan + channel pan,
clamped; channel volume), refreshed when the channel's volume / pan move (FL recomputes them every block), as LYNX's
BassDrum wrapper already did.

## 2. A Drumaxx channel without a state [FL render][CODE]

FL renders a Drumaxx channel with no state (the DLL's own defaults) silent on every key (REA_rodada169e §2). LYNX's
importer left such a channel unsupported, so the stand-in synth played a tone. Now an FL Drumaxx with **no state at all**
becomes a Drumaxx channel without a kit, silent like FL (an unreadable, non-empty state stays unsupported as before).
Old LYNX projects are not affected: LYNX never made a Drumaxx without a state (a new one gets a kit, an imported one
without a state was the synth channel and is saved as such).

## 3. What a NEW generator is in FL [DECOMP][DATA]

- Every way of adding a generator in FL 2026's UI goes through the **Plugin database**: the Channel rack's "+" button
  (`AddChanBtnMouseDown`, `0x103aa00`) opens the plugin list `FUN_00ebf510(0, 0, −10)` (TPlugListForm, the picker of
  round 169k). Picking an entry (`FUN_00ec7840`, "insert channel") makes a new channel (`FUN_011f53d0`) and loads the
  **entry's .fst file** into it (`FUN_00ec7720`: channel vmt `+0x150` (path, 1, 2, 0, −1.0)). So FL's fresh generator is
  the Plugin database `.fst` state.
- FLEngine never loads a generator's `Plugin presets\…\Default.fst` (its four "Default.fst" references are the mixer
  track reset / instrument-track paths, REA_rodada169e §1).
- Inventory of the FL engines LYNX has, with FL installed [DATA][CODE] (`Data/Patches`; "same" = the Plugin database
  state is byte-identical to the Default preset's):

| Engine | FL's `Plugin presets\…\Default.*` | Plugin database entry | LYNX before | LYNX now |
|---|---|---|---|---|
| Drumaxx, BassDrum | none | `Drum\<name>.fst` | DB (169e) | DB |
| Drumpad | only a `Default\` folder of single drums | `Drum\Drumpad.fst` | DB (169e) | DB |
| GMS | none | `Synth classic\GMS.fst` | first preset `Basses\Agressive TE.gmsynth` | **DB** |
| Morphine | none | `Synth special\Morphine.fst` | first preset `Bass\BAS Acoustic 1 FG.mrp` | **DB** |
| Transistor Bass | none | `Synth classic\Transistor Bass.fst` (= the "Factory programs" preset, sequencer on) | first preset `Empty.fst` | **DB** |
| Sakura | only a `Default\` folder (the DB state = `Default\Bowed 6.fst`) | `Synth special\Sakura.fst` | first preset `Atmosphere\ATM ChainSaw Symphony FG.fst` | **DB** |
| Ogun | `Default.fst` (version 12, ≠ DB) | `Synth special\Ogun.fst` = `Drum\Ogun.fst` (version 11) | neither readable → first readable preset `Atmospheric\300 Ft Glass Bowl.fst` | **DB** (§4) |
| FL Slayer | none | **none** (FL 2026's picker doesn't list it) | first preset `AUTO Chorus Chords.fst` | unchanged (no FL counterpart; LYNX can't play a stateless Slayer) |
| BooBass | (no preset folder) | `Misc\BooBass.fst`: version 1, knobs 0x7fff | knobs 32768 | knobs **32767** |
| Kepler, Kepler Exo, SimSynth, Fruity DX10, Plucked!, Fruit Kick | `Default.fst` | same | Default.fst | unchanged (= FL) |
| BeepMap | `Default.fst` | 1 byte differs | Default.fst | unchanged |
| 3x Osc, Sytrus, Harmor, Harmless, PoiZone, MiniSynth, Sawer, FL Keys, Fruity Granulizer, Toxic Biohazard (`Default.tbio`) | `Default.*` | **different state** (12 … 36 355 bytes differ) | Default.* | unchanged — **for the user to decide** (below) |
| Wave Traveller | `Default.fst` (LYNX can't read it) | `Sampler\Wave Traveller.fst` (36 508 B) | first readable preset `classic kick.fst` | unchanged (see "left") |

**For the user (decision):** by the evidence above, FL's own new 3x Osc, Sytrus, Harmor, Harmless, PoiZone, MiniSynth,
Sawer, FL Keys, Granulizer and Toxic Biohazard are their Plugin database state, which differs from the
`Default.*` preset LYNX starts them from (since round 142). Switching them to the Plugin database state is the FL-exact
choice but changes their fresh sound in LYNX; not done without a yes.

## 4. Ogun states of versions 10–13 [DECOMP]

FL's Plugin database Ogun is a **version-11** state (`Default.fst` is version 12); LYNX read 14 and up only. Ogun's
SaveRestoreState (`Ogun_x64.dll` `FUN_00864d00`), for a state of version v < 14 (≥ 0x130004 and 0x7fffffff refused):
after the parameter reset it sets plugin bytes `+0x20f…+0x228` (0, 0, 1, 0, 0, 16 × 0 at `+0x214`, 0, 0, –, 0, i32 430
at `+0x228`), reads **12 bytes** into `+0x12c` (8 below version 8) and **0xf4 − 1 (v < 1) − 3 (v < 2) − 1 (v < 7) − 16
(v < 9) − 1 (v < 11) − 1 (v < 12) − 6 (v < 13)** bytes into `+0x138`, clears `+0x226` below version 14, then reads the
rest exactly like version 14 (3 delay bytes, 3 EQ types, the comment as u32 count + bytes below 18, the 13 articulators —
all of them from version 10 on —, the 4 curves). For versions 10–13 every byte of the 256-byte parameter block is thus
either read or one of those explicit values. LYNX (`core/Ogun.cpp` `parseState`) now builds that block the same way and
the importer stores such a state rewritten in version 14's layout (`ogunCurrentLayout`), so the editor's knobs
(`ogunParam`) see today's offsets.

## 5. FL render [FL render]

One FL launch (`FL64.exe /R /Ewav`, both locks, FL exited by itself), project `C:\Users\kauan\lynx-test\gen169l\fl\
g169l.flp` built at run time from `dmx169e.flp` and the user's Plugin database files (`mk169l.py`): Drumaxx centred
(FL's default volume 10000), Drumaxx at pan −50 % (3200) and volume 8000, Drumaxx without a state, then GMS, Morphine,
Transistor Bass, Sakura, BassDrum, Drumpad, Ogun with their Plugin database states (keys 60, 48, 72). Per-note peaks
L / R in dB (FL `g169l.wav` vs LYNX `renderwav`, LYNX de-normalised), and the RMS difference relative to FL's:

| channel | FL | LYNX | rel. diff |
|---|---|---|---|
| Drumaxx centre, 36 / 38 / 43 | −16.3 −16.3 / −19.1 −19.9 / −19.9 −23.7 | identical | 0.000 / 0.001 / 0.002 |
| Drumaxx pan −50 %, vol 8000, 36 / 38 / 43 | −18.0 −25.6 / −20.8 −29.3 / −21.6 −33.1 | identical | 0.000 / 0.002 / 0.002 |
| Drumaxx, keys 60 / 48 (both) | silent | silent | |
| Drumaxx without a state, all 10 keys | silent | silent (before: the stand-in synth) | |
| BassDrum 60 / 48 / 72 | −10.0 | −10.0 | 0.004 / 0.000 / 0.000 |
| Drumpad 60 / 48 / 72 | −16.5 | −16.5 / −16.5 / −16.4 | 0.001 / 0.03 / 0.03 |
| GMS 60 / 48 / 72 | −21.3 −21.9 / −20.1 −21.1 / −19.4 −19.4 | −21.3 −21.7 / −20.0 −21.0 / −19.3 −19.5 | 0.05–0.07 |
| Morphine 60 / 48 / 72 | −30.5 −29.9 / −34.0 −31.5 / −27.3 −27.8 | −31.2 −26.6 / −33.8 −31.2 / −27.7 −26.6 | 0.6–1.0 (same level, different waveform: see "left") |
| Sakura 60 / 48 / 72 | −16.3 −14.1 / −16.2 −14.1 / −20.0 −14.2 | −16.7 −14.3 / −17.0 −14.5 / −19.3 −15.5 | 0.2–0.3 |
| Transistor Bass 60 / 48 / 72 | −7.5 / −19.0 / −15.2 | −9.5 / −22.0 / −18.2 | 1.3 (LYNX 2–3 dB lower) |
| Ogun 60 / 48 / 72 | −3.4 −4.6 / −3.7 −3.2 / −5.2 −5.1 | (this render: before §4, the stand-in synth) | |

The 16-bit render's LSB is 3·10⁻⁵: Drumaxx, BassDrum and Drumpad are bit-close. The Drumaxx channel at pan −50 %
proves Final.Pan's law A (L/R −7.6 dB apart = 20·log10(tan(π/8))).
