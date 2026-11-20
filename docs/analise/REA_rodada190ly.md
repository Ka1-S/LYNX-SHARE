# REA round 190ly (0.1.30): Valhalla → LYNX stand-ins, part 2

Owner's permission of 2026-11-17: REA on third-party plug-ins for parity, clean-room only. This round is black-box: renders of
the official plug-ins (refcap030's captures, round 190hv; tp172d / verb183b / refcap182 for the gate banks), LYNX's own code and
fits. Nothing of the references is copied — no code, no tables, no presets. Lab: /mnt/e/lynx-work/valhalla2_190ly (lab/, runs/,
runs2/, gold/, cpu/, mini/, probe/). Score: round 190lg's scorer (/mnt/e/lynx-work/rea3_190ln/scorer), the same copy for every
before and after. Base: origin/valhalla190ls (3084a5121; its runs/b0 for refcap030, `fxr_n2` for the gates and the gold).

## 1. Atrium's dense tank only for a translated ValhallaRoom (owner decision, perf-first) [built]

Round 190ls gave every new Atrium the dense 32-line late tank (+48 % instructions for +0.18 of score). The owner's call:
the dense tank (`verbs::atrium::reaLaw`) is set only when a ValhallaRoom is translated. `makeEffectSlot` no longer sets it; the
substitute table gets a `replaced = …` key (items like `preset`, applied only when the slot replaces an original device — a
project / import translation, not a substitute added by hand from a menu) and `[valhalla-room]` carries `replaced = #24=1`.
- A new Atrium renders byte for byte as before round 190ls (fxrender: 190ls's binary with `--set 24=0` = this round's default);
  a translated ValhallaRoom renders byte for byte as round 190ls (refcap030 Room scores unchanged: 3.62 → 3.80 stays).
- CPU (fxrender, 20 s at 48 kHz incl. file I/O, user instructions): a new Atrium 4.81 G (light tank) against 6.78 G with the
  dense tank (−29 %).

## 2. ValhallaDelay → Echoform: Feedback 100 % and the diffusion's timing [EXEC, built]

Echo-pass energies of an impulse (windows of one echo time around n × 300 ms; refcap030 pairs, Tape, Era Past, Wow / Flutter
50 %, `lab/passE.py`, `lab/track.py`):

| Feedback | reference dB / pass (passes 2 … 10) | Echoform before |
|---|---|---|
| 40 % | −9 … −10 (then its noise floor) | −8.4 … −9.5 |
| 100 % | −1.5 (steady to pass 12) | −1.4 for three passes, then +0.2 … +0.4 (grows) |
| 160 % | +2.5 | +2.5 |

- At 100 % the reference loses 1.5 dB a pass; Echoform's measured tape stage (round 190dl, fitted at 70 %) grew. 160 % already
  matches, 70 % was the fit. A loop gain correction over Feedback (`valhallaLoopDb`, knots at 0 / 0.2 / 0.35 / 0.5 / 0.65 / 0.8 /
  1): −1.1 dB at 100 %, 0 at 70 % and 160 % (linear between). On the pairs bank −0.8 / −1.0 / −1.2 / −1.5 / −2.0 dB gave 4.17 /
  4.22 / 4.22 / 4.17 / 4.17; −1.1 kept every file (the three score-1 files → 4, rms +10.5 … +12.4 → +0.1 … +0.8 dB).
- Diffusion: every Echoform pass with any loop diffusion came 33 ms (× size) late — a Schroeder all-pass of small gain passes
  its input mostly delayed by its length (round 190is found the same for the echo voicings). The reference keeps the echo time
  and spreads around it: the first echo's energy 5 % / 50 % / 95 % points at Diffusion 20 / 50 / 80 % are −55 / +4 / +84,
  −83 / +9 / +198, −88 / +36 / +253 ms around the time; Echoform's were +12 … +62 ms, always late. The law reads the
  diffusers' delay back out of the echo time and makes them 3.5 × as long at 0.8 × Diffusion: −31 / +1 / +35, −59 / +1 / +151,
  −88 / +1 / +203 ms. (Length × 1 … 3.5, gain 0.5 … 0.8, read-back 0.7 … 1 all scored the same on the bank; the closest spread
  was kept. The reference's spread at low Diffusion is wider still — its gain law is not linear in the knob; not fitted.)
- Hidden `echosuite::echoform::valhallaLaw` (index 109 on the round's base, one past round 190is's revision; **115 in the 0.1.30 rollup since step 10**, after round 190kw's lineLaw and round 190lf's five hybrid values), read without a voicing; only the
  ValhallaDelay translator sets it (`replaced = #109=1`): a new Echoform and every other role keep their sound.
- The probe renders at 80 … 140 % (`probe/`, refcap030's capture host under the trial + GUI locks) were queued all
  round but the GUI lock stayed held by other rounds; the law rests on the 40 / 70 / 100 / 160 % captures. When the probes
  land (`probe/dly`, `irlv` at 8 Feedback values), the 0.65 knot (130 %) can be measured instead of interpolated.

## 3. ValhallaFreqEcho at 96 kHz → Spiral Echo's loop phase [EXEC, built]

The shifted loop reads its signal through an analytic filter (two 4-stage all-pass chains in z⁻², coefficients fixed per
sample). Its real branch's phase at a given frequency therefore changes with the rate: against 48 kHz it is a near-constant
turn of −66.7° at 96 kHz (+8.2° at 44.1 kHz) plus a small delay (−0.52 / +0.05 samples), residue ≤ 11° (96 kHz) over 60 Hz …
12 kHz. A steady tone's level through the loop is that phase (the loop's comb), so 96 kHz played the nburst / staircase tones
3 … 4 dB off where 48 kHz was right. Hidden `spiral::rateLaw` (index 10): `rateTurn ()` fits θ and the delay at prepare
(least squares over 48 log-spaced points), the loop turns its analytic signal by θ (four multiplies a sample and side) and reads
the line that much later; at 48 kHz it is the old path exactly. New instances and translated states (makeEffectSlot).
- refcap030 FreqEcho sr96 3.31 → 4.81 (13 up, 0 down; 96 kHz now scores above 48 kHz's 4.06), sr48 unchanged (bit for bit).
- A steady 1 kHz tone through the default loop: 48 kHz −17.81 dB, 96 kHz −17.82 dB (−20.00 without), 44.1 kHz −17.86 dB.

## 4. ValhallaVintageVerb Concert Hall at short decays → Epoch Verb [EXEC, built]

Revision 1's trims left Concert Hall on its 0.1.27 voicing (fitted at 2 … 4 s). At 0.51 s (the ColorMode sweep, the Decay 0.2
pairs, sr S1) the reference's impulse sat 2.0 dB over ours, its spectrum tilted (LYNX − reference: +3.4 dB at 125 Hz, −1.4 …
−1.8 dB from 500 Hz up). Hidden `verbs::epoch::shortTrim` (index 19 on the round's base; **20 in the 0.1.30 rollup since step 10**, after round 190kw's loopLaw): an octave-band trim (`epochShort`, three closed-loop passes
of `lab/vvtrim.py` on 11 impulse renders, residue within ±0.8 dB; 500 Hz … 8 kHz +2.68 / +1.76 / +1.45 / +2.07 / +1.27 dB) at
0.51 s and under, faded out on the decay's logarithm by 2.9 s (sr S3 at 2.9 s already sits on the reference). Concert Hall only;
new instances and translated states.
- The lows: the loop also cut 63 … 250 Hz (−5.4 dB at 125 Hz). That took the other banks' renders of the same setting under
  their reference (tp172d S1 impulse −1.0 → −3.9 dB, refcap182 S1 bass −0.9 → −4.4 dB: two gate files a step down). Half the
  cut still took tp172d's impulse one down; without it every bank rose (refcap030 pairs 3.56 → 3.67, sr48 3.55 → 3.65, gates
  +7 files, none down): the lows keep the 0.1.27 voicing. The two references disagree under 250 Hz at this setting.
- The level: the loop leaves the mean band gain where it was; +0.7 … +1.9 dB more lifted the 1 kHz staircase files a step (the
  mode block 2.48 → 2.72) but took sr96 S1's impulse one step down (Epoch sits 1.3 dB higher at 96 kHz than at 48 kHz on that
  setting — its own finding, not this trim's): not shipped (option for the owner below).
- refcap030 VintageVerb 2.67 → 2.92 (154 files: mode 2.16 → 2.48, pairs 3.44 → 3.67, sr48 3.50 → 3.65, sr96 3.60 → 3.65; 39 up,
  0 down). The staircase (`stim`) files stay 2 … 5 dB under: the reference's tank answers a steady tone louder than its impulse
  energy predicts at this decay (more modulation did not move it, round 190ls).

## 5. Gates, CPU

- Gold (`gold/gold.sh`, 190ls's fxrender against the round's with the hidden values off; Atrium new / translated Room, Epoch
  Verb, Echoform, Spiral Echo, the other reverbs / delays / modulation stand-ins and Valhalla states, 44.1 / 48 / 96 kHz):
  126 / 126 renders byte-identical.
- Gate banks (`lab/gate2.py`, `runs2/g0` → `runs2/g2`; tp172d, verb183b captures, refcap182 programme of VintageVerb, Delay,
  FreqEcho, Room): 8 files up, none down (VintageVerb tp172d 3.73 → 3.87, captures 3.56 → 3.61, programme 3.48 → 3.64; Delay
  programme 4.20 → 4.25; the rest equal).
- Tests: tests/Parity190lyTests.inc (4 tests); the mini runner (rea3_190ln's 82 test files + Parity190ls / 190ly + 8 more files
  that touch Echoform / Spiral / Epoch / the substitutes): 555 009 checks, 0 failures.
- CPU (fxrender, 20 s incl. file I/O, `cpu/instr.log`): Echoform in the ValhallaDelay role +0.9 % with the law; Spiral Echo at
  96 kHz +1.8 % with the rate law (0 at 48 kHz); Epoch Concert Hall short trim +0.01 %; a new Atrium −29 % against round 190ls.

## 6. Not built this round, with findings

- **Supermassive** (Nebula): the mode sweep's low files are the classic-engine networks; per mode the low band runs +2 … +8 dB
  over the reference and the top −1 … −3 dB (Libra / Gemini / … — mode values 0.40 … 0.98). A per-mode tone trim needs Nebula's
  classic engine to carry the tone corrector (it has none today); not started.
- **SpaceModulator** (Orbit): the mode sweep (2.85) scored every reference value against each of Orbit's 11 modes
  (`lab/smmode.py`, `logs/smmode.log`): values 0.19 … 0.27 play best on Through-zero (2 / 3, not Rise / fall), 0.31 … 0.35 on
  Ensemble-like modes (8 / 9), 0.48 … 0.52 on Symphonic (10). The reference's mode order is not Orbit's beyond the first; the
  value texts the host logged are stale (every value reads "Up"), so the order needs a text capture of the Mode parameter
  before the map can change.
- **FutureVerb** (Horizon) has no refcap030 captures, but verb183b's 62 capture files score 2.68 and tp172d's probe 40 files
  3.17 (gate baseline `runs2/g0`) — the lowest Valhalla stand-in, the next round's first target.
- **Shimmer** / **UberMod**: not reached (UberMod's ColorMode files sit at the 2.5 dB / 1.5 dB score steps on their staircase
  tones: time-varying draws).
- **Epoch at 96 kHz**: sr96 S1's impulse 1.3 dB over sr48's against the same reference; worth the same check as FreqEcho's.
