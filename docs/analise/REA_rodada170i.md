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

## 3. Fruity Granulizer (and 3x Osc's custom wave) lost their sample on macOS / Linux

**FL evidence [FL render]:** FL's render of `Render tests/Fruity Granulizer.flp` plays the channel's sample
(`%FLStudioFactoryData%\Data\Patches\Packs\Drums\Kicks\808 Kick.wav`, an Ogg-in-WAV) at −11 dB RMS.

**LYNX before:** silent (score 0). Cause [debug build]: the importer treated any plugin sample path starting with `\`
or `/` as an old preset's path relative to FL's Data folder and prefixed `<FL>\Data` again. On Windows the resolved
path starts with a drive letter, but with a POSIX FL root (macOS, Linux, a Wine prefix, renderwav on Linux) every
resolved path starts with `/`, so the Granulizer and 3x Osc (custom wave, shape 6) channels lost their sample. A
second bug showed up on the way: with no sample the Granulizer refuses the note (voice −1) and the instrument wrote
the pitch-slide anchor into voice −1 before checking it (out-of-bounds write, a crash risk).

**LYNX now:** the Data-relative rule applies only when the path does not exist as it is (`dataRelativeSample`,
`core/FlpImport.cpp`); the Granulizer checks the voice before touching it. Tests:
`flq170i_granulizer_and_3xosc_samples_on_posix_roots`, `flq170i_old_data_relative_plugin_sample_still_found`.

## 4. FPC's level (open)

The render test plays −4.57 dB too quietly in LYNX, a pure gain (gain-fitted ESR −39 dB) except on the hi-hat pad
(−5.25 dB). **[DECOMP FPC_x64.dll `FUN_0086b480` (voice start), constant at `0x8E0FA0` = 20.526958 (f32, read-only:
2 read references)]:** a layer's gain is `(exp(ln(21.526958) · v/127) − 1)/10` (v = layer volume, 99 ≈ unity) and the
pad's gain the same law on the pad volume (`pad+0x14E`); LYNX uses `(11^(v/127) − 1)/10` for both. The difference
between the two pads (0.68 dB measured; 0.76 dB predicted by the new law for layers 99 and 117) confirms the layer
law. The absolute level is still not explained (pad volume 128 under the same law would make FL 6.5 dB louder than
measured), so the law is **not** changed yet: the voice's host level (`TVoiceParams`) and FPC's output stage need
one more REA pass.

## 5. Hardcore: a box type of 0.5 is an empty box

**FL evidence [FL render + DATA + DECOMP (REA_rodada71 §1.1: box type `(int)(float)(int)v`, cabinet `(int)v`)].** The
render test's Hardcore state (preset "80s lead") stores the box types 1, 7, 2, 6, **0.5** (distortion, noise gate,
chorus, delay, and box 5 *on* with type 0.5) and the cabinet 4.0; boxes 2–4 are off. FL truncates: box 5 is empty.
(REA_rodada71's "box 1 empty, box 5 delay" summary of this state was wrong.) LYNX's render with box 5 switched off nulls
against FL's render at **−75.2 dB** (ESR; RMS 0.00 dB, envelope corr 1.000).

**LYNX before:** the slot values were rounded (`lround`), so 0.5 became type 1 and box 5 added a second distortion
(+4.6 dB, a 0.2 s ringing tail after the note; score 2).

**LYNX now:** `hardcore::toSlot` truncates the cabinet and the box types like FL. Test:
`flq170i_hardcore_box_type_and_cabinet_truncate`.

## Where I stopped

- Done: §1 and §3 code + tests (`tests/FlQ170iTests.inc`), `LYNX_WARMUP` in renderwav.
- Verified: build-local 1963871 checks / 0 failures; rescore of the 82 render tests (`E:\lynx-work\flq170i\b2`): 12
  improved, no regressions. Tracker regenerated. Landing batch 1+2.
- Next: FPC level (−4.6 dB; REA: layer and pad volume law `(21.527^(v/127) − 1)/10`, constant at FPC_x64.dll
  `0x8E0FA0`, used by `FUN_0086b480`), Slicex level (−2.2 dB), Hardcore (env corr 0.04).
- Commands: `scripts/flref-lynx.sh /mnt/e/lynx-work/flq170i/rt.list <out> build-local/renderwav 2`, then
  `python3 scripts/flref_compare.py batch /mnt/e/lynx-work/flq170i/flref <out> <out>/report.md`.
