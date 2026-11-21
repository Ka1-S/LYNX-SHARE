# REA round 190oc — the Kilohearts Nonlinear Filter (0.1.31)

Lab, captures and logs: `/mnt/e/lynx-work/helix4_khs3_190oc/` (HANDOFF.md). Branch `helix4_khs3_190oc` off
origin/wip/rollup031. Evidence classes as in rounds 190nb / 190nh: [STATIC] read in Kilohearts' HeartCore 2.4.6
(`HeartCore.core_64`, Kilohearts' own build of 2026-11-19; Ghidra headless, no execution), [MEASURED] black-box against
round 190nb's 56 lossless kHs Nonlinear Filter references, [DATA] the snapin's own saved state. Clean-room: LYNX's code
restates the law as formulas; the plug-in's half-band coefficients are a fixed table that LYNX does not use — LYNX designs
its own half-band (§3). No Kilohearts code, table or preset is copied.

## 1. Hosting

A hidden law of FL's Effector, as rounds 190nb / 190nh's (core/KiloheartsDsp.h): [12] = 49 / 64 (the code round 190nh kept
for it; `Law::nonlinearFilter`), [13] type / 4, [14] mode / 8, [15] cutoff on a log scale 20 … 20 480 Hz, [16] Q on a log
scale 0.5 … 24, [17] drive 0 … 1; the mix is Effector's Wet. The translator opens the snapin on Effector (Type: Filter);
a slot saved with the earlier Filter stand-in keeps it. Every project saved before has no law 49: unchanged (gold, §5).
[DATA] The state keeps `filter_type` (low_pass, band_pass, high_pass, notch, all_pass), `dirt_type` (clean, saturated,
tubular, clipped, warm, biased, fuzzy, metallic, digital: the order of the plug-in's own name table, which is the order
of its mode switch), `cutoff` (Hz), `q`, `drive`; the defaults (no key saved) are low pass, saturated, 440 Hz, 0.707, 0.25.

## 2. DirtyFilterDsp [STATIC]

Vtable 0x18158e128: prepare (slot 7, 0x1802fd690), process (slot 14, 0x1802fd750), reset (slot 15, 0x1802fd5d0), the
designer 0x1802feba0 (slot 13 only feeds the scope: (L + R) / 2 into a ring).

- **Prepare:** the snapins' Chebyshev II designer (0x180bacef0, round 190nh's) for 3 sections (6th order) at 8 fs, stop band
  from 1.5 fs, 85 dB; the cookbook biquad designer (0x180bac490, type 2: high pass) at 17 Hz, Q 1/√2, at fs.
- **Process, per block:** up 2× through a polyphase IIR half-band (two branches of first-order all-passes, run on the
  zero-stuffed input, ×2 in / ×½ out: H (z) = ½ (A0 (z²) + z^-1 A1 (z²))) — its seven coefficients are a fixed table;
  then, per 2× sample, four samples at 8 fs by Catmull-Rom between the second and third of the last four 2× samples
  (t = ¼, ½, ¾, 1; the weights are the Catmull-Rom polynomials' values); per 8× sample the filter (below, in double),
  the output through the three Chebyshev II sections (double, each section's output rounded to float), every 4th kept
  (the one at t = 1); down 2× through the same half-band; the 17 Hz high pass. The designer's seven values ramp linearly
  across the block (step (new − now) / 8n at each 8× sample) and jump when the mode changes (a reset sets mode 9, so the
  first block jumps).
- **The filter:** a Chamberlin state-variable filter: hp = x − lp + k · bp (k = (1 − e^(g / Q)) / g), bp += g · hp,
  lp += g · bp, where g solves g + g³ / 24 = ω (the plug-in writes the cubic's root by Cardano: two cube roots and a
  square root; ω = 2π · scale · cutoff / 8 fs; g ≈ 2 sin (ω / 2)). Output y = cIn · x + cLp · lp[n−1] + cBp · bp[n−1] +
  cHp · hp[n] (summed in float). Types: low pass (cLp 1), band pass (cBp 1), high pass (cHp 1), notch (lp + hp; in the
  fuzzy and digital modes x − bp / Q), all-pass (lp + hp − bp / Q; digital: − 2 bp / Q).
- **The modes** (drive d = 10^(dB / 20), dB = lo + Drive · (hi − lo); the designer's constants are exp2 of multiples of
  log2 10, i.e. dB laws):

| mode | the nonlinearity | lo … hi dB | the designer's extras |
|---|---|---|---|
| 0 clean | none | 0 … 0 | — |
| 1 saturated | the low pass fed back as f (d · lp) / d, f (a) = a + a³/3 + a⁵/5 (|a| ≤ 1; beyond, 3a ∓ 22/15) | −12 … +24 | cutoff × 0.66^(D²), low pass × 10^(0.35 D²) |
| 2 tubular | (lp, bp) scaled back onto d · |(lp, bp)| ≤ 1 after each step | −12 … +16 | — |
| 3 clipped | bp and lp clipped to ±d | +3 … −16 | — |
| 4 warm | x − lp through (v − 0.9 tanh v) / d, v = d (x − lp) (tanh: its [7/6] Padé fraction, |v| ≤ 4.373) | +24 … 0 | cutoff × 1.5^(D²), band pass × 10^(D/2), high pass × 10^(0.75 D + 0.1) |
| 5 biased | the low pass fed back as (1 − h (−d · lp)) / d, h = exp on |a| ≤ 1, e · a above, (a + 2) / e below | −16 … +24 | — |
| 6 fuzzy | bp clipped to ±1 / d | 0 … +36 | band pass × 10^(D²) |
| 7 metallic | x − lp through (0.75 sin v + 0.25 v) / d | −12 … +24 | cutoff × 1.2^(D²), band pass × 10^(D²/4), high pass × 10^(0.35 D⁴) |
| 8 digital | x − lp through ⌊v⌋ + ½ averaged over v ± 0.05 (a box-filtered staircase: its antiderivative's difference × 10) | +24 … 0 | — |

The math library calls were identified by their use: exp2 (the log2 10 multiples), cbrt (Cardano), exp, sin, floor.

## 3. LYNX's own half-band

The plug-in's seven all-pass coefficients equal, to 1e-7, the elliptic (Valenzuela–Constantinides) half-band design with
seven coefficients at a transition band of 0.05 of the 2× rate [STATIC + lab/halfband.py]. LYNX does not use that
table: it designs its own at first use (`Engine::nlfHalfBand`, the textbook formulas — selectivity from the transition
band, the nome, its theta series) with LYNX's own specification, **eight coefficients at a transition band of 0.054**
(17th order, 109 dB stop band from 0.277 of the 2× rate, flat to 0.223) — a different filter with different
coefficients. The specification was chosen black-box on the references (lab/nlfscore.py, lab/hbfit.py): the plug-in's
chain, measured against LYNX's on the high-pass render, is 0.92 samples later at low frequencies and 1.11 at 20 kHz;
eight coefficients at 0.054 with the decimator keeping H's odd outputs puts LYNX's low-frequency delay within a
hundredth of a sample of the plug-in's (low-pass renders null at −73 … −108 dB), while the phase near 20 kHz still
differs (the high-pass / notch / all-pass renders at −16 … −23 dB: §4). The candidates tried: 6 or 8 coefficients at
0.03 … 0.08, both branch orders, both decimator phases (7 coefficients avoided: their best spec would be the
plug-in's own table).

## 4. Scores (fxparity time window, round 190nb's references and song positions, the same harness)

Before = the Filter stand-in (round 190ln's map, origin/wip/rollup031), after = the law through fxrender; bypass = round
190nh's column.

| plug-in | files | bypass | before | after | up / same / down |
|---|---|---|---|---|---|
| kHs Nonlinear Filter | 56 | 2.18 | 4.62 | 9.43 | 55 / 1 / 0 |

Per file (runs/after2_nlf.txt): 41 files at 10 (ESR −71 … −108 dB: every low-pass render, every mode the references
hold — clean, saturated, tubular, warm, fuzzy — Q 0.5 … 24, drive 0 … 1, cutoff 20 … 640 Hz, and the default on the
stimulus, drums, vocal, mix), 1 at 9 (warm, −55 dB), 11 at 8 (band pass −42 / −45 dB, digital −46 / −54, cutoff 3.6 kHz
−45 / −47, 20.5 kHz −23 / −26, the high-pass / notch / all-pass level sweeps −21 … −23), 3 at 7 (the high-pass, notch
and all-pass sine sweeps, −16 … −18 dB: the half-band's phase near Nyquist, §3). The one file the same: the high-pass
sine sweep stays 7 (the stand-in's spectrum matched there already).

## 5. Gates

- Gold (gold/khsgold.sh, fxr_base vs the round's fxrender): 192 renders, 0 differ — round 190nh's 168 (Facet via Pro-Q,
  22 stand-in defaults, Effector's 12 types × 2 settings, round 190nb's five laws) plus round 190nh's eight laws through
  their translator, at 44.1 / 48 / 96 kHz.
- CPU (cpu/khsbench.log, cpu/khsbench_cg.log): 1.41 s for 35 s stereo at 48 kHz on one pinned core (the Filter stand-in
  0.22 s; the 190nh Distortion law 1.40 s in the same run) — about 4 % of a core per instance, the plug-in's own 8×
  design; exact instructions per stereo sample by mode 3 160 (clean) … 4 660 (biased).

## 6. Not done

- The plug-in's sleep on silence (HeartCore's) is not modelled: the filter runs on through silence (its tail decays to
  zero; no reference shows a difference).
- The high-pass / notch / all-pass renders keep the half-band's phase difference near Nyquist (−16 … −23 dB): a closer
  match would mean fitting the half-band to the plug-in's phase, i.e. rebuilding its table — not done (clean-room).
- No reference exercises the clipped, biased and metallic modes, the mix, or automation (the ramps): those follow the
  static read only.
