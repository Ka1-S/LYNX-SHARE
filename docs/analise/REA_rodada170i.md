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

## 4. FPC's level (solved in §7)

The render test plays −4.57 dB too quietly in LYNX, a pure gain (gain-fitted ESR −39 dB) except on the hi-hat pad
(−5.25 dB). **[DECOMP FPC_x64.dll `FUN_0086b480` (voice start), constant at `0x8E0FA0` = 20.526958 (f32, read-only:
2 read references)]:** a layer's gain is `(exp(ln(21.526958) · v/127) − 1)/10` (v = layer volume, 99 ≈ unity) and the
pad's gain the same law on the pad volume (`pad+0x14E`); LYNX uses `(11^(v/127) − 1)/10` for both. The difference
between the two pads (0.68 dB measured; 0.76 dB predicted by the new law for layers 99 and 117) confirms the layer
law. The absolute level is still not explained (pad volume 128 under the same law would make FL 6.5 dB louder than
measured), so the law is **not** changed yet: the voice's host level (`TVoiceParams`) and FPC's output stage need
one more REA pass. Second pass (this round): `FUN_0086b480` stores the layer gain at voice `+0x19` and the pad gain
at the pad voice `+0x18`; the note velocity only picks the layer there (`+0x2c` ≤ v < `+0x30`, 127 inclusive). The test
pads are pad volume 128, layer volume 99 / 117, so the missing factor (FL ≈ 0.095 × (21.527^(v/127) − 1) · P against
LYNX's 0.56) sits in the voice render, where velocity and the pad gain are applied: still open. Arithmetic for the next pass: with
§6's host law (velocity 100 = unity, +2.14 dB over LYNX's linear velocity) and the layer law above (+5.16 dB at 99,
+5.86 dB at 117), FL would be +7.3 / +8.0 dB over LYNX; it measures +4.57 / +5.25, a constant −2.7 dB left — close to a
centre pan-law cut (−3.0 dB) on these nearly centred pads (pan 0…12 of 128), but **not confirmed**: no code changed.

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

## 6. Slicex: velocity through FL's Init.Vol law

**FL evidence [DECOMP FLEngine `FUN_00d29aa0` (REA_rodada76 §1) + REA_rodada67 §host + FL render].** FL's host turns a
note's velocity byte into the voice's Init.Vol `(exp(v·0.01·ln 11) − 1)·0.1` (exactly 1 at velocity 100) and plugin
generators play Final.Vol = Init.Vol × channel volume. FL's render test "Slicex" has its 10 notes at velocity 100 and
plays 2.15 dB louder than LYNX (gain fit 1.2786 ≈ 128/100); the same project with the notes rewritten to velocity 128,
played by LYNX's linear velocity, matches FL's level (RMS −0.01 dB, spectrum 0.03 dB).

**LYNX before:** the sampler that plays an imported Slicex channel scaled the level by velocity/128
(−2.14 dB at FL's default velocity 100).

**LYNX now:** `SamplerSettings::flVelocityLaw` (set by the importer for Slicex, saved in the project) makes the sampler use FL's law (`flInitVolume`). FPC is left as it was (§4: its level gap is not only the
velocity), and so is Fruity Slicer: its render test (notes at 100 too) is already 2.4 dB louder in LYNX, the level
being ruled by its stretch pre-processing (licensed engine, REA_rodada69 §7), so it gives no evidence either way. Test:
`flq170i_slicex_velocity_reaches_the_level_through_fl_init_vol`.

# Round 170i2 (the open items of 170i)

**FL sessions [FL render].** Three FL launches, one at a time under `~/.cache/lynx-wingui.lock` and
`~/.cache/lynx-fl.lock`, each rendering one folder of probe projects (`FL64.exe /R /Ewav /F<folder> /O<out>`; FL exits by
itself after the folder; no plugin scan; the probes are FL's render tests with one state field or note byte changed in
place, `E:\lynx-work\flq170i2\probe*`). The unchanged `FPC.flp` rendered again is bit-identical to the 2026-10-16
cache, so the cache and the new renders agree.

## 7. FPC: FL's volume law, velocity and a constant

| FL render (kick pad, its sample fitted, residual ≤ −65 dB) | FL gain | LYNX before | FL / LYNX |
|---|---|---|---|
| the render test (pad 128, layer 99, velocity 100, centre) | 0.40777 | 0.24099 | 1.692 |
| every pad hard left | L 0.57667, R 0 | L 0.34081 | 1.692 |
| pad volume 64 | 0.07159 | 0.05542 | 1.2916 |
| layer volume 127 | 0.84191 | 0.4395 | 1.9156 |
| velocity 64 (layer 1 of the kick) | 0.14841 = 0.364 × velocity 100 | — | — |

- Pan: the same ratio centred and hard left, so FPC's pan law is LYNX's channel pan law.
- Pad and layer volume follow `(21.527^(v/127) − 1)/10` (§4's decompile): predicted ratios 1.2916 and 1.9156 against
  LYNX's `(11^x − 1)/10`, measured 1.2916 and 1.9156.
- Velocity: 64 gives 0.364 of 100 = `(11^0.64 − 1)/10`, FL's Init.Vol law (§6), so velocity 100 is unity.
- What is left is a constant: FL = pad law × layer law × velocity law × LYNX's channel chain × **0.35355 = 1/(2·√2)**
  (−9.03 dB), the same on all four renders to 0.000 dB. Where FPC applies it (its output stage) is not traced.

**LYNX now:** `fpcPads` uses FPC's law (`fpcVolume`) and carries the constant in the pad gain; the split channels keep
the FPC channel's volume and put the pad × layer gain on their zones (FPC's law goes above unity, which the channel
volume curve clipped), with FL's velocity law (`SamplerSettings::flVelocityLaw`). The render test: **−7.7 → −75.0 dB**
(score 2 → 10). Test: `flq170i2_fpc_volume_law_and_velocity`.

## 8. EQUO: no HQ in a plain render, and the smoother glides from FL's construction values

- **[FL render]** FL's command-line render of the render test matches EQUO's plain band design: with LYNX's
  offline flag driving the "HQ non-realtime" design (REA_rodada61 §2.2, "not confirmed") the render was 2.25 dB louder
  (ESR −5.6 dB); with the plain design the level is right. The HQ design now follows the export's **HQ for all plugins**
  (`offlineRendering() && hqForAllPlugins()`), like the other HQ switches (REA_rodada134 B6).
- **[FL render]** FL's output starts 1 dB low and reaches the loaded curve after ~0.45 s; the same project with motion
  smoothness 0 starts on the curve at once. That is REA_rodada61 §2.5: the smoother states start at (1, 0, 1) when the
  plugin is created and a state load leaves them. LYNX started them at the loaded values (marked there as an
  inference); now a new engine starts at (1, 0, 1) and `set()` / `reset()` keep them.
- Result: smoothness 0 nulls at **−87 dB**; the render test −5.6 → −17.4 dB as `renderwav` renders it (no warm-up)
  and **−55 dB** with a warm-up of 2688 samples (FL's own render pre-roll, which moves by a few hundred samples from
  run to run, REA_rodada154 / REA_rodada165g). In the app the engines run while stopped, so an export has no glide.
  Test: `flq170i2_equo_render_is_not_hq_and_glides_from_construction`.
- The earlier note "send not routed" was wrong for this test: its state has no send (REA_rodada61 §1).

## 9. Slicex: each slice one frame early

**[FL render + debug]** After the first slice every slice played the frame just before its start: the slice starts are
stored as fractions (`float(p / total)`), and `floor(total · f)` lands on `p − 1` when the float is a hair under.
The sampler now takes the nearest frame. The render test: −24.1 → **−35.1 dB**; the first slice nulls at −82 dB, what is
left is at the slice changes (FL switches instantly, LYNX's cut fades the old slice over a few samples: not traced).
Test: `flq170i2_slice_starts_on_their_exact_frame`.

## 10. FL Keys and Fruity Scratcher: no fix possible from LYNX's side

- **FL Keys [FL render]:** FL's render of the render test is silent again (2026-11-04), and with every note moved 8 beats
  later too: not a loading race. FL 2026's command-line render plays nothing for this project, so it gives no
  reference; the engine's evidence stays REA_rodada69 §8.5 (DLL harness, bit-identical on the 11 demo states).
- **Fruity Scratcher [FL render + REA_rodada165g §6]:** the score comes from FL's own nondeterminism: FL runs the plugin
  for a pre-roll of 75 + 544k samples that changes from run to run (two FL renders of this test differ by −18…−20 dB).
  Outside the first 0.35 s LYNX's spectrum is within 0.03 dB and its level within 0.1 dB; the waveform is −11 dB off at a
  108-sample lag (FL's event grid, which moves with that pre-roll). Not reproducible, not a defect.

## Where I stopped

- Done: §1, §3 (batch 1–2: build-local 1963871 checks / 0 failures, queued on the merge train), §5 and §6 (batch 3:
  1963902 checks / 0 failures). Rescore of the 82 render tests (`E:\lynx-work\flq170i\b3`): 14 improved, no
  regressions; mean sound score 6.64 → 7.78 (81 scorable), 38 at the 16-bit floor (was 28). Tracker regenerated.
- Open: FPC (§4), Slicex's remaining −24 dB (slice edges), EQUO (send not routed, lag 341), Fruity Scratcher (r167c
  area), FL Keys (FL's own render is silent: needs one FL re-render), random-phase engines (not defects), FLEX and Fruity
  Slicer's stretch (permanent decisions). fxfuzz not re-run this round.
- Commands: `scripts/flref-lynx.sh /mnt/e/lynx-work/flq170i/rt.list <out> build-local/renderwav 2`, then
  `python3 scripts/flref_compare.py batch /mnt/e/lynx-work/flq170i/flref <out> <out>/report.md`;
  tracker: `python3 /mnt/e/lynx-work/flq170i/py/mkdoc.py <out>/compare.jsonl`.
