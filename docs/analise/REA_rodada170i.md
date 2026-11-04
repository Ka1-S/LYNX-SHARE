# Round 170i: FL-native plugin parity QA (sound vs FL renders, controls, editors, stability)

User request (2026-11-04): a general quality pass over every FL-native plugin: render the same patch in FL and in
LYNX, compare, score 0–10, fix the worst first. The score table lives in `E:\lynx-work\tracker\fl-parity-0.1.24.md`
(outside git). Round 170c owns LYNX's "inspired-by" effects; this round owns the FL engines and Fruity effects.

## 0. Method

- **FL side [FL render]:** FL Studio 2026's own renders of the 82 projects in `Data/System/Render tests/*.flp`
  (`FL64.exe /R /Ewav`, cache `C:\Users\kauan\lynx-test\flref\fl\System-Render_tests\`, made 2026-10-16 with
  `scripts/flref-fl.sh`). No new FL session was needed for this round's batch 1.
- **LYNX side:** `build-local/renderwav <flp> /dev/null song` (float raw), `scripts/flref-lynx.sh`.
- **Compare:** `scripts/flref_compare.py batch` (ESR after alignment, gain-fitted ESR, RMS, envelope correlation,
  third-octave spectrum distance). 16-bit FL output: the floor is about −76 … −92 dB.
- **Probe without rebuilding:** a small Python rewriter (`nativize.py`, scratch) replaces a slot's state in an FL
  project and renders it with the unchanged renderwav; each hypothesis about a state mapping is checked against
  FL's own render.

## 1. FL 11's VST2 Fruity effects open with their saved controls

**FL evidence [FL render + DATA].** 13 render tests save the effect as a VST2 inside Fruity Wrapper
(`C:\Program Files (x86)\Image-Line\FL Studio 11\Plugins\VST\Fruity <name>.dll`, VST2 state version −9, the
parameters as 0…1 floats): Balance, Bass Boost, Center, Chorus, Compressor, Fast LP, Filter, Flanger, Free Filter,
Mute 2, Phase Inverter, Phaser, Reeverb. The installation's demos hold the same set (Compressor ×12, Chorus ×6,
Flanger ×6 incl. one spelled "Fruity flanger", Fast LP ×3). FL 2026 plays them with its native plugins. Rebuilding
the native state as `raw = lo + round(v · (hi − lo))` per control, in the native control order, nulls against FL's
render at the 16-bit floor for 11 effects (table below). Free Filter and Fruity Filter have no VST2 parameter for
CENTER: FL's render matches only with CENTER on (−40 → −75 dB and −34 → floor after the first call); every other
type/frequency/Q/gain choice is worse (grid search ±12 raw per control, all 7 filter types).

**LYNX before:** only Compressor and Phaser were rebuilt; the other 11 opened with their default controls (Balance
+4.7 dB, Phase Inverter not inverting, Mute 2 not muting, Free Filter +10 dB, Bass Boost −3.9 dB, …).

**LYNX now:** `wrappedFruityState` (`core/FlpImport.cpp`) rebuilds all 13 (name matched case-insensitively).

| render test | ESR before | ESR now (probe) | note |
|---|---|---|---|
| Fruity Balance | −0.6 dB (RMS +4.7) | −80.4 | |
| Fruity Phase Inverter | +3.0 | −85.1 | |
| Fruity Center | +2.4 | −81.3 | |
| Fruity Bass Boost | −3.0 | −88.8 | |
| Fruity Mute 2 | 0.0 (RMS +3.0) | −85.1 | |
| Fruity Flanger | −2.0 | −86.5 | |
| Fruity Reeverb | −7.5 | −76.2 | |
| Fruity Free Filter | +9.6 | −74.9 | CENTER on |
| Fruity Filter | −10.0 | −33.8 (floor after 50 ms) | CENTER on; start: see §2 |
| Fruity Fast LP | −16.3 | −30.7 (floor after 50 ms) | start: see §2 |
| Fruity Chorus | −5.8 | −32.4 (spectrum 0.01 dB) | FL draws the LFO start phases at random (REA_rodada59) |

## 2. FL's render warm-up settles the parameter glides

Fast LP and Fruity Filter differ from FL only in the first ~50 ms (then −85 dB). FL's output at sample 0 already uses
the loaded cutoff (Fast LP: first sample = input × 0.056 = the coefficient of cutoff 560), i.e. the plugin's
first-call glide from its defaults (REA_rodada63 §1.3) happened before the song: FL processes 1536–2304 samples of
silence before a render (REA_rodada154). In the app this does not matter (the engines run while the transport is
stopped); `renderwav` now takes `LYNX_WARMUP=n` to do the same before the song.

## Where I stopped

- Done: §1 code + tests (`tests/FlQ170iTests.inc`), `LYNX_WARMUP` in renderwav.
- In progress: build-local, rescore, land batch 1.
- Next: FPC level (−4.6 dB; REA: layer and pad volume law `(21.527^(v/127) − 1)/10`, constant at FPC_x64.dll
  `0x8E0FA0`, used by `FUN_0086b480`), Slicex level (−2.2 dB), Fruity Granulizer render test silent in LYNX,
  Hardcore (env corr 0.04).
- Commands: `scripts/flref-lynx.sh /mnt/e/lynx-work/flq170i/rt.list <out> build-local/renderwav 2`, then
  `python3 scripts/flref_compare.py batch /mnt/e/lynx-work/flq170i/flref <out> <out>/report.md`.
