# REA round 167f — gap-hunt: FL Studio 2026 plugins and engines that LYNX SHARE lacks

Date: 2026-10-31. Clean-room: only FL Studio's own binaries (the installation's `Plugins/Fruity/**` DLLs and
`Shared/ildsp_x64.dll`) are studied; this document describes behaviour in our own words. No Image-Line code, preset or
sample is in the repository.

## Inventory method

- FL side **[DATA]**: the folders of `C:\Program Files\Image-Line\FL Studio 2026\Plugins\Fruity\{Generators,Effects}`
  (51 generators, 87 effects) and a count of every plugin name (`.flp`/`.fst` event 201) in the installation's `Data`
  folder (demo songs, presets, plugin databases): script in the round's scratch dir, `plugcount.txt`.
- LYNX side: `src/core/FlpImport.cpp` (effect table, generator branches, the "silent" list, `flextra::inertPluginNote`),
  `src/core/FlEffects.cpp` (`effectFromFl`), `docs/PLUGINS.md`, `docs/STATUS.md`, earlier rounds (154, 165g, 165k).
- Out of scope here (other rounds of 0.1.22): Newtime / Newtone / Transmitter (167b), Edison / Scratcher (167c),
  Kepler Exo (167d). FLEX packs are encrypted and are never opened (project decision, 2026-10-13).

Status values: **none** (dropped or a stand-in synth), **approximate** (mapped to another LYNX engine), **LYNX role**
(LYNX's own engine in the same role, e.g. visualizers), **FL engine** (FL's algorithm, close), **bit-identical**.

## Table

Uses = occurrences in the installation's `Data` (demo songs in parentheses).

| FL plugin | Uses (demo songs) | In LYNX | Effort | Value |
|---|---:|---|---|---|
| **Emphasis** (mastering limiter) | 31 (4 demo songs, all on the Master) | approximate (LYNX limiter) | large: oversampled C++ engine in the DLL, ~10 MB state (round 154 §3) | **high**: the master of 4 demo songs |
| **Transporter** | 13 (1) | approximate (utility stage) | large: Faust `transientDetectionLooper` in `ildsp_x64.dll`, host song position + side-chain (round 165k §9) | medium |
| **Fruity Slicer 2** (new in FL 2026) | 7 (0) | none: not imported | large: 5.9 MB front end + 2.7 MB `engine_x64.dll`, per-slice envelopes/maps (`Data/Maps`, `Data/Envelopes`) | low now: the 7 uses are 2 distinct states, the `Default` preset and the `FruitySlicer2Track` template, both without a sample **[DATA]**; nothing to play until users' own projects use it |
| FL Studio Mobile Rack FX | 36 (0, all presets) | none: slot left out | large: the FL Studio Mobile effect modules | low–medium (presets only) |
| FL Studio Mobile / Mobile Rack | 10 (0) | none: synth preset by name | very large (FLM instruments + content) | low |
| Wave Traveller | 13 (1) | approximate: sample plays, scratch curve left out | medium | medium (1 demo song + a render test) |
| Fruity LSD (MIDI → Windows GS synth) | 6 (0) | none: silent | medium (DLS player) | low (database entries only) |
| Fruity dB Meter, Fruity Spectroman | 9 (1) | **LYNX role since 167f**: LYNX's analyser (Wave Candy role) in the peak-meter / spectrum view, the slot kept | small | low |
| Kepler Exo | 192 (presets) | synth fallback | — | 167d |
| Newtime / Newtone / Transmitter | — | placeholder / close / FL engine | — | 167b |
| Edison, Fruity Scratcher | — | — | — | 167c |
| FLEX | 195 | synth patch by preset name | — | never (encrypted content) |
| Wasp / Wasp XT | 87 | placeholder (as FL 2026: the 64-bit DLL is silent) | — | done (FL-exact) |
| Razer Chroma, ReWired | 14 | inert with a note (round 165g) | — | done |
| Every other FL native (3x Osc … Vocodex, VFX, controllers, visualizers, Patcher) | — | FL engine / bit-identical / LYNX role (docs/PLUGINS.md) | — | done in earlier rounds |

## Order of work this round

1. **Emphasis** on FL's own engine (REA on `Emphasis_x64.dll`, DLL harness, bit-compare on the installation's states).
2. Transporter on FL's engine and the Wave Traveller scratch curve (in parallel, own specs:
   `REA_rodada167f-transporter.md`, `REA_rodada167f-wavetraveller.md`).
3. Fruity dB Meter / Spectroman kept as LYNX's analyser (done). Fruity Slicer 2 waits (see the table).

The per-plugin study is in [`REA_rodada167f.md`](REA_rodada167f.md).
