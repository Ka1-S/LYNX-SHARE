# REA round 190lf, sub-round "dyn" — Waves CLA-2A → Glow Leveler's cell law, CLA-76 leftovers

Scope (brief `waves2_190lf/briefs/SUB-DYN.md`): the Waves CLA-2A's opto behaviour as a compact LYNX model inside Glow
Leveler, then what round 190ky left open on the CLA-76 (Crest's FET law). Owner's permission of 2026-11-17 for REA on
third-party binaries, clean-room as in rounds 190kt / 190ky: what is recorded here is structure, behaviour and parameter
meaning; LYNX's code is written from this description and every constant is fitted on our own black-box renders. Vendor
tables were located, not read out and not used; no licence check was touched; the demo's mutes are masked.
Lab: `/mnt/e/lynx-work/waves2_190lf/dyn/` (lab/, cap/, runs/, ev/, HANDOFF.md).

## 1. Waves CLA-2A

### 1.1 What REA showed [DECOMP]

- DSP (`LA2AProc.A`, round 190ky's `ev/cla2a_gmcp/functions/0x1800011a4.c`), read as structure: a first-order input
  stage; the **side chain is fed back** from the cell's attenuated signal (in Limit a share of the input is blended in);
  a cascade of second-order sections (the Hi Freq weighting); two optional level-dependent stages *inside the side
  chain* (a one-pole peak detector and a gain `2^(−(20 log10 env − thr) · k)` above a threshold — the side-chain
  amplifier running out of headroom); a rectifier and a light transfer chosen by the mode (two tables: Compress /
  Limit); **two cell states** that share the drive with a split factor and each move towards their share with
  charge / discharge coefficients computed per sample from their own state (the discharge from tables); the audio is
  multiplied by the cell's gain, then split by a first-order all-pass crossover into a low and a high band, the **high
  band scaled by a function of the cell state** (reduced less under reduction: the tilt), plus a small parallel path of
  the un-attenuated signal; an optional output-transformer stage (two first-order filters and a static nonlinearity
  subtracted from the signal); mix with the dry input, output gain.
- Control DLL `CLA-2A.dll` (decompiled this round: `ev/cla2a_dll`, 2 060 functions): the knob → coefficient laws are
  tables in dB, two sets (Compress / Limit), converted to linear gains at initialisation (`0x1800270f0.c`); the scale
  getters `SideChainInGainScale` / `OutGainScale` serve the meters. Tables located, not read out.

### 1.2 Black-box findings [EXEC]

Probes (Waves demo mode on refcap030's fxmeasure host: hidden, no editor, no input sent, the trial lock only;
`cap/cla2a_1.txt`, `cla2a_2.txt`, 123 renders, 69 s of host time; every stimulus starts with 1.5 s of silence):
1 kHz stairs −40 … 0 dBFS (2 dB, 0.4 s) at peak reduction 0 / 30 / 46 / 60 / 70 / 80 / 90 / 100 (Compress) and 46 / 60 / 80
(Limit); 60 Hz / 200 Hz / 5 kHz stairs at Hi Freq 0 / 20 / 50 / 100 (peak reduction 70); −6 dBFS bursts of 50 ms / 300 ms /
2 s / 8 s on a −40 dBFS probe; pink-noise stairs; synthetic drums; a 30-tone multitone alone and under a 1 kHz tone at
−20 / −10 / 0 dBFS (the tilt); the Gain dial 0 … 100 at a low level; 1 kHz / 100 Hz level steps (harmonics); refcap182's six
states (loadcomponent) on the probe stimuli.

- **The demo mutes are fades** to about −120 dB (not runs of exact zeros as round 190ky saw on the CLA-76): masked as
  frames more than 30 dB under the input, ±4 frames (`lab/probe.py`).
- **The cell keeps its state across `reset`**: a probe processed after the stairs starts ≈ 1 dB down and recovers over
  seconds (`a_C70_b300`). The fit therefore simulates each probe after the one the host processed before it (script
  order, the 0.3 / 0.2 s settles in between). refcap182's programme renders do the same (each file after the previous
  one of its setting): their first seconds carry the previous file's tail (a host artefact, not modelled).
- **No long memory**: at peak reduction 83 the recovery after 300 ms, 2 s and 8 s bursts is the same within 0.03 dB
  (only a 50 ms burst, which leaves less reduction, recovers faster), as round 165w found; the exposure term below was
  left free in the fit.
- The Gain dial's level at 1 kHz (−40 dBFS, nothing reduced): 0 … 100 → −56.2, −35.0, −18.1, −2.5, 8.48, 16.91, 24.75,
  29.77, 33.78, 35.29, 38.80 dB — round 123's table within 0.06 dB (from 10 up).

### 1.3 LYNX's cell law

@@CELL@@

## 2. Waves CLA-76 leftovers

@@C76@@
