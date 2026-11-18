# REA round 190lf, sub-round "verb" — Waves RVerb / H-Reverb / TrueVerb and H-Delay

Scope (brief SUB-VERB): the three Waves reverbs on LYNX's stand-in (Reeverb 2 since round 190js: structure, decay laws,
EQ) and Waves H-Delay on Echoform. Owner's permission of 2026-11-17 for REA on third-party binaries, clean-room: what is
written here is structure, behaviour and parameter meaning; LYNX's code is written from it and every constant is fitted on
our own renders. No vendor code, table, preset or coefficient was read out or used; no licence check was touched.

Tools: the bundles' parameter text tables (fxmeasure `params`), black-box renders in Waves demo mode on refcap030's
fxmeasure host (hidden, no editor, no input sent, the trial lock only; every probe stimulus starts with 1.5 s of silence),
refcap182's saved states (`loadcomponent`) and programme renders, and Ghidra 12.1.4 headless on `RVerb_GMCP.dll`
(the rea MCP's Ghidra provider timed out probing Java under the machine's load, so the same Ghidra ran from the shell:
`lab/gh/`). Lab and evidence: `/mnt/e/lynx-work/waves2_190lf/verb/` (cap/, lab/, runs/, HANDOFF.md).

Probe caveat (found by the coordinator in the same round): on this host `defaults` + `set` does not always reach the
Waves DSP; `loadcomponent` does. Every result below that a law rests on comes from a `loadcomponent` state, or from a
fresh `load` per job (d5), or was cross-checked between settings that must differ (the H-Delay filter probes agree with
their knobs to 0.03 dB). The reverb parameter sweeps of batch v1 (`defaults` + `set`) partly did not take (RVerb's
Gated / Non-Linear types decay like its halls) and are not used.

## 1. The reverbs: what limits the score

- **RVerb is exactly linear and time-invariant**: refcap182's programme convolved with the impulse responses of its own
  state (L / R impulses, `vimp`) reproduces its programme renders to ESR −101 … −135 dB (`lab/ub.py`). H-Reverb X1 / X3
  too (−70 … −110 dB); TrueVerb to −46 … −67 dB. H-Reverb D / X2 / X4 are not (level-dependent: X4 ducks; D / X2's
  programme sits ~3.7 dB under their impulse response).
- **The ceiling of an independent reverb.** A reverb with the reference's time-frequency envelope (STFT magnitude kept,
  phases random, third-octave energy matched — the best any reverb that is not the reference's own network can do on
  average) scores 3.6 … 3.8 per RVerb setting on the prog182 table (spec 1 … 3 dB on bass / guitar: a few notes sample
  the response's fine structure). The current translation is already 3.48 (D 4.0). Reverb gains on this table are capped
  near 4 unless the early, deterministic part matches sample by sample.
- **Decay structure.** Reeverb 2 as translated rings about twice as long as the references below 1 kHz (RVerb D: RT60
  5.4 / 4.2 / 0.9 s at 125 Hz / 1 kHz / 8 kHz against 2.3 / 2.1 / 1.6 s; its early 80 ms carry 60 % of the energy against
  19 %): round 190js fitted the long-term spectrum only. A better decay law moves the envelope correlation, not the
  table's score (it sits under the ceiling above), so it was not changed in this round (see §5).

## 2. TrueVerb's direct sound [EXEC]

With the direct sound and the early reflections on, the first samples of TrueVerb's impulse response are exactly
`a, −b, −b·ρ, −b·ρ², …` (a first-order low shelf: −4.3 … −5.6 dB at DC, −0.6 dB at the top; residual 0 to the 5th decimal),
before the first reflection (5 … 14 ms). With the early reflections off it is a flat 0.9785. Over refcap182's four states
with the direct sound on (D 10.02 m, X1 4.5, X2 2.6, X3 13) the shelf deepens with Distance d:

| | D (10.02 m) | X1 (4.5) | X2 (2.6) | X3 (13) | LYNX's law (fitted) |
|---|---|---|---|---|---|
| a | 0.8758 | 0.8805 | 0.8927 | 0.8510 | 0.90024 − 0.003351 d (±0.009) |
| b | 0.0881 | 0.0765 | 0.0691 | 0.0985 | 0.06297 + 0.002666 d (±0.0016) |
| ρ | 0.7223 | 0.7053 | 0.7023 | 0.7279 | 0.69484 + 0.002601 d (±0.0014) |

LYNX: Reeverb 2's hidden `reeverb2DirectLaw` adds `a·x − b·Σ ρ^(k−1)·x[n−k]` per side to the engine's output (its own
flat dry at 0); the translator sets it from Distance (was a flat −0.7 − 0.135·d dB).

**Its tail with the direct sound on.** The same impulse responses after the direct sound: RT60 at 1 kHz 0.84 … 1.03 × the
Decay knob (X2's small room 0.39), × 1.35 … 1.6 at 125 Hz, × 0.65 … 0.75 at 8 kHz; round 190js's Reeverb 2 tail rang
2.4 … 3 × as long (decay control = the knob, bass × 2.66). Reeverb 2 rings 1.68 s per 1 s of its decay control at a neutral
bass multiplier (`lab/rvgrid.py`), so: decay control 0.54 × the knob, bass × 1.30, high damping 120, crossover 2 kHz, the
wet 3 dB up on Reeverb's level law to keep its energy. The wet offset, the bass multiplier, the damping and the decay
scale were chosen on the prog182 table of the four direct-on settings (`lab/tvfit.py`, eight variants: 4.00 … 4.30; the
table is a final refinement here, stated). With the direct sound off the round-190js tail stays: its tone (fitted to the
old decay) sets that table's spectrum, and the new decay would put X4 3.8 → 3.2 … 3.6 (high band +1 dB; five high-cut /
wet variants tried).

## 3. Waves H-Delay's loop [EXEC]

Probes d1 … d5 (Waves demo mode; impulses of 0.02 / 0.25, a pulse train every 0.1 / 0.6 s, white noise; feedback 0 / 30;
refcap182's X2 / S2 states and variants; the round-124 hybrid engine reproduces the plain loop already, D / S1 / X1 / X3
score 7 … 8).

### 3.1 HiPass / LoPass
- Both sit **inside the loop**: the second echo carries the response twice, the third three times (feedback 30, Analog
  off: 50 Hz −24.3 / −48.6 / −72.9 dB re the passband per pass at HiPass 203 Hz).
- Each is a 2nd-order section: high pass Q 0.80, low pass Q 0.95, the knob's frequency mapped through a bilinear
  transform without pre-warping (HiPass 5 kHz acts at 4.83 kHz, LoPass 7.86 kHz at 7.26 kHz at 48 kHz). Fits of the
  first echo (Analog off, feedback 0): 0.00 … 0.03 dB rms magnitude, complex error −50 … −99 dB.

### 3.2 LoFi
- A steep low pass **inside the loop** whose own delay is taken off the echo time (each pass lands on T): flat to
  4.5 kHz (−0.3 dB), −1.0 @ 5 k, −4.4 @ 6 k, −11.9 @ 7 k, −27.1 @ 8 k, a null near 8.75 kHz, −56 … −64 dB above;
  31 samples long, nearly symmetric. LYNX's own design (not the reference's coefficients): a Kaiser-windowed sinc,
  33 taps at 48 kHz (scaled with the rate), 6.34 kHz, β 4.8, fitted to that magnitude (±0.6 dB down to −40 dB).

### 3.3 Modulation
- The delay swings as a **triangle smoothed by a one-pole of 0.37 s** (its harmonics: H3 / H1 = 0.072 at a 5 s period,
  0.094 at 10 s, 0.042 at 2 s, 0.039 at 1 s — a one-pole of fixed time constant on a triangle gives 0.072 / 0.094 / 0.047 /
  0.040).
- Swing ± depth · T / 2 before the smoothing (depth 25 / 50 / 75 / 100 % at T = 375 ms: H1 34.3 / 68.7 / 103.1 / 137.2 ms;
  ms mode 375 ms: 74.1 at a 10 s period; one point off the law: 1/4 note (500 ms) swings 7 % less).
- Period: 1 / (2 · Rate) with the BPM / host sync (Rate 0.1 → 5.000 s, 0.25 → 2.000 s; at 120 BPM), 1 / Rate in ms mode
  (0.1 → 9.99 s).
- **Free-running**: the LFO starts with the instance at the centre, rising, and runs on across the host's reset (six
  renders after different histories: the phase moves exactly by the samples processed in between). refcap182's S2
  renders each start a fresh process and are 35 s long (7 periods): they all start at the LFO's start.
- Both sides move together (stereo and ping-pong).

**What the table cannot credit.** S2 (LoFi + 50 % modulation) stays at 4: the model follows the reference's measured
delay track to 2.4 ms rms (d4), but a sample match needs the delay to ~0.02 ms over the whole 5 s period, so the echoes stay
uncorrelated (xcorr < 0.1) and the score stays on the level / spectrum rules; and refcap182 rendered each S2 file from the
LFO's start while fxrender processes 0.5 s of silence first (the LFO runs through it, as the reference's does). With the
modulation off the same loop matches the reference's S2 echo train to −11.7 dB ESR (d2 s2d0).

LYNX: Echoform's hidden `hybridLaw` (+ `hybridHp`, `hybridLp`, `hybridDepth`, `hybridRate`): the round-124 loop with
those filters on every line write, LoFi's FIR with its delay off the read, and a read tap at T − (FIR delay) + swing ·
the smoothed triangle (cubic read); the LFO restarts only in prepare(). The translator uses it whenever the state has a
filter, LoFi or depth (it used round 119's approximation there); states without keep the round-124 loop bit for bit.

## 4. RVerb's DSP (structure only) [DECOMP: `ev/rverb_gmcp/`]

`RVerb_GMCP.dll` (the bundle's GenericMultiCoreProcessor): two large per-block process functions (`FUN_1800016a0`,
11.6 kB, and `FUN_180007350`, 11 kB, SIMD) and a third (`FUN_180005180`, 6.9 kB) — not read further this round (time;
the black-box ceiling of §1 says what a structure match could buy on the table).

## 5. Not done / next
- A Waves reverb engine with exact per-band decay (the LuxeVerb Space engine's Jot network would carry RVerb's / TrueVerb's
  damping ratios and H-Reverb's directly): not done, because the table cannot show it (§1) and the per-file gate would
  block its random per-file moves; worth doing for the sound (envelope), with its own listening check.
- H-Reverb's level dependence (D / X2 sit ~3.7 dB under their impulse response) and its ducking model.
- H-Delay's Analog Mod1 / Mod3 / Mod4 and the hiss; the ms-mode delay that `set` did not move in one probe (d2 ms101d0).
