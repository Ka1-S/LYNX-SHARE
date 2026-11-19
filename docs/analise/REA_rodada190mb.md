# REA round 190mb (0.1.30): Valhalla → LYNX stand-ins, part 3; Crest's last Pro-C 3 styles

Owner's permission of 2026-11-17: REA on third-party plug-ins for parity, clean-room only. Part A (Valhalla) is black-box:
renders of the official plug-ins through refcap030's capture host (round 190hv; new probe renders this round, see §1),
tp172d / verb183b / refcap182 for the gate banks, LYNX's own code and fits. Part B (Crest) is static REA of the Pro-C 3
binary plus refcap030's captures (sub-round, §7). Nothing of the references is copied — no code, no tables, no presets;
LYNX's laws are written in its own words and fitted with its own smooth curves. Lab: /mnt/e/lynx-work/parity190mb (lab/,
runs/, runs2/, fit/, fvcap/, gold/, cpu/, mini/; crest/ for part B). Score: round 190lg's time-window scorer
(/mnt/e/lynx-work/rea3_190ln/scorer), the same copy for every before and after. Base ("before"): `bin/fxr_m0` =
origin/valhalla2_190ly (1688827b4) merged with origin/rea3_190lp (600a79376), commit 621cd37bd.

## 0. The merge (621cd37bd)
Conflicts resolved in the 0.1.30 rollup's order: Crest's `revision` after `busLaw` (`revisionOn`: the console law takes
precedence), Echoform's `lineLaw` (#109) before `valhallaLaw` (#110; the ValhallaDelay substitute moved with it), Epoch's
`loopLaw` before `shortTrim`; round 190kw's Crest follower fields renamed q0 … q3 (the console law keeps c1 / c2).
Found and fixed this round: the merge script had also turned Sentry's `revision = count` into `count + 1` (Sentry
revision 1 at #33 instead of #32 in fxr_m0; back to #32 — no saved project carries it yet).

## 1. Capturing the references' own names and FutureVerb [EXEC]
- Valhalla's plug-ins report a stale value text through the host (every Mode value read "Up", every colour "seventies"):
  the text follows the processor, not the value asked about. Set → 30 ms of processed silence → get reads the right
  name. Mode name tables at 97 values (`refcap030/scr/*__p190mb_names.txt`, logs in `refcap030/rep/`):
  - SpaceModulator: Up, Up/Down, TZFlange+, TZFlange−, TriFlange, Ocho, Doubler, VariUp, VariUpDown, Ensemble360,
    Symphonic; mode = floor (11 v) − 1 (Up below 2/11, Symphonic at 1 only) — Orbit's own order and value law.
  - Supermassive (22 modes from Gemini to Sirius, wrapping to Gemini at the top), FutureVerb (8 reverb modes, 12 echo modes,
    4 colours), VintageVerb (22 modes; colours seventies < 0.667, eighties < 1, now at 1): every LYNX translator map
    already agrees (rounds 172d / 183b / 190ly measured them by ear and level).
- FutureVerb had no refcap030 bank: its demo opens holes in the output ~40 s after the plug-in loads. Split into one host
  process per setting (load → set → 3 renders, ~5 s each; `lab/fvsplit.py`, `lab/fvextra.py`), no hole in any of the
  183 renders (`lab/holes.py`; the one flagged gap is Nonlin's own gate). Blocks: refcap030's generated mode / pairs /
  sr44 / sr48 / sr96 settings plus every reverb mode, echo mode and colour at its own value (`X_*`, base S1: Mix 100 %,
  Decay 25 %; echo modes at S2's echo level / feedback). Captures and component states: `fvcap/Valhalla/ValhallaFutureVerb`.
- Round 190ly's ValhallaDelay probes (Feedback 80 … 140 %, `scr/ValhallaDelay__p190ly.txt`) ran in the same session
  (18 / 18 renders, `valhalla2_190ly/probe`).

## 2. ValhallaSupermassive → Nebula: the classic networks' tone per mode [EXEC, built]
- Round 190ly found the mode sweep's classic-network files with their lows 2 … 8 dB over the reference and their top
  1 … 3 dB under; Nebula's classic engine (law 0) had no tone corrector. `Settings::classicTone` (new, 0 everywhere else)
  runs law 1's octave-band tone corrector (`bandDb`, the 8-band least-squares EQ) after the classic loop as well.
- Hidden `verbs::nebula::toneLaw` (#15; saved while non-zero), set only by the Supermassive translator (`replaced = #15=1`):
  a new Nebula, one added by hand and every saved project keep their sound. Read only by the 16 classic networks.
- Fit (`lab/nbtrim.py`, three closed-loop passes on refcap030's Mode sweep, impulse + test signal, Mix 100 %, Feedback
  80 %): per mode gain + 8 bands, on each channel's power (Eternity's sides are anti-phase; a mono-sum fit had pushed it
  +5 dB). Haze and Plateau 1 dB under the fit (their test signal sat 2 dB louder than their impulse against the reference).
- Feedback: the same kind of fit at 50 % (tp172d's mode probes) took 12 of its 94 files down — the scorer's time-window
  spectra of music and bursts are not the long-term spectrum the fit sees. The trim comes in from Feedback 65 % and is full
  from 80 %; at 50 % every render is bit for bit.

## 3. ValhallaVintageVerb → Epoch: the merge's double level at 0.51 s, and 96 kHz [EXEC, built]
- The merge stacked round 190kw's `loopLaw` (level over Decay / Size) and round 190ly's `shortTrim` (fitted without it):
  the impulses sat 1.9 dB over the reference (27 refcap030 files down against 190ly's binary).
- Short trim refitted on top of the loop law (`lab/vvtrim.py`): −2.7 dB with the 500 Hz … 8 kHz bands, the lows cancelled
  (they stay where the loop law puts them, as round 190ly kept them), the 1 kHz band 2.5 dB over the impulse fit — the test
  signal's 1 kHz staircase sat 2.8 … 4.3 dB under the reference where the impulse matched (+1.5 / +2.5 / +3.5 dB tried).
- 96 kHz: the premise was the other way round. Epoch is rate-independent (its 96 kHz impulse is its 48 kHz one − 6.02 dB,
  a single-sample impulse's own scaling, in every window and band); the reference is not: against its own 48 kHz renders
  its sr96 captures lose 1.4 … 1.7 dB in the first 80 ms and ~0.9 dB to 300 ms in the 1970s colour (D / S1 / S2), nothing
  in the tail, and in the 1980s colour (S3) its tail is ~0.5 dB higher (`lab/rateprobe.py`). Hidden `epoch::rateLaw` (#21,
  only the VintageVerb translator sets it): the early field's and the level's change per octave of rate per colour.
  Grid (`lab/erate.sh`): 1970s early −2 dB / octave, 1980s level +0.5 dB / octave. Every sr96 file keeps its score, the
  mean |level| error 0.88 → 0.76 dB and spectrum 2.18 → 2.11 dB; more early cut or a level cut took 1 … 3 files down.

## 4. ValhallaSpaceModulator → Orbit: the map stands, the level per mode [EXEC, built]
- The captured names (§1) put the reference's modes in Orbit's own order, with Orbit's own value law: the translator's
  direct `Mode` map was right. Round 190ly's "order differs" came from scoring each reference value against every Orbit
  voice — Orbit's voices for Up/Down, the through-zero pair, Tri and Ocho simply sound least like the reference's.
- What a level can do: on the Mode sweep (Mix 100 %, Feedback +50 %) Orbit's Up/Down sat +2.3 dB (steady) / +8 dB (test
  signal) over, the through-zero pair −4 … −6 dB under, Ocho +6.4 / +8.2 dB over. Hidden `orbit::smLaw` (#7, only the
  SpaceModulator translator sets it, `replaced = #7=1`): Up/Down −3.5, TZ+ +4.8, TZ− +4.0, Octo −6.5, Drift rise +0.8 dB
  (three variants scored; Tri's steady tone and test signal sit ±2.5 dB either side, no level fits both).
- Those levels are the feedback's resonance: at Feedback 0 (verb183b's mode captures, refcap182 S2) the voices already sat
  on the reference and the full trim took 4 files down, so the trim scales with |feedback| up to ±50 % (held beyond;
  negative feedback assumed alike, not captured). Re-voicing Orbit's Up/Down, through-zero, Tri and Octo voices is the
  next step (their time structure, not their level, keeps those files at 2 … 3).

## 5. ValhallaDelay → Echoform: the 80 … 140 % probes [EXEC, built]
- Echo-pass energies (passE, impulse at 0.1 s, 300 ms echo) of the 18 probe renders against Echoform's translated renders
  (`lab/dlyprobe.py`): Echoform faded 0.4 … 0.9 dB a pass slower at every Feedback probed; at 105 … 120 % it held or grew
  where the reference faded 0.7 … 1.1 dB a pass (impulses +4 … +15 dB loud).
- A deeper cut below 100 % (−1.3 dB at 80 %, −1.5 at 100 %) matched the probes but took refcap182 / tp172d's S2 (80 %) and
  one pairs file down — those states fade faster than the probes' base. Kept: round 190ly's line up to 100 %, then −1.1 dB
  held to 120 %, −0.5 dB at 140 %, 0 from 160 % (knots now 0 / 0.2 / 0.35 / 0.40 / 0.5 / 0.6 / 0.7 / 0.8 / 1).
- refcap030 Delay (164 files), refcap182 / tp172d: identical. verb183b's 140 % capture (dlFb7) 1 → 3. The probes:
  105 % 2 → 4, 110 % 1 → 1 / 1 → 2, 120 % 1 → 2 / 1 → 3, but 140 % 3 → 2 (both signals: −2.3 dB under, where dlFb7 at the
  same Feedback rose two steps; the probes are this round's new references, not a gate bank — listed, not hidden).

## 6. ValhallaFutureVerb → Horizon: tone and level per reverb mode [EXEC, built]
- Before: the weakest Valhalla stand-in on every bank — this round's probe bank 2.19 … 2.61 (183 files), verb183b's captures
  2.68, tp172d's probes 3.17. Echo modes are far off (Digital's impulse +19 dB, the reverse-octave and granular modes
  +6 … +7 dB): a per-mode echo law is the next step (not touched here).
- Hidden `horizon::revision` (#25, one past the controls; new instances and translated states, like round 190it's revisions
  of the other stand-ins): a per-reverb-mode gain + tone (`horizonTrim`, the 0.1.27 law's tone corrector). Fit: three
  closed-loop passes of `lab/hztrim.py` on the `X_ReverbMode_<k>` renders (impulse + test signal).
- Gated variants (`lab/hzrun.sh`, the probe bank + every gate bank): the full fit 2.53 → 2.86 on the mode block but the
  music files 1 … 2 dB loud (its lows) and Chamber's tone taking verb183b's Chamber music captures down; without the lows
  (V1) 7 + 4 files down; Chamber at half its tone (V6) 22 up on the mode block, pairs / sr all up, but 5 files down (two
  Reverse-echo files at the Chamber base, three verb183b Chamber music captures). Shipped (V7): Room and Chamber untouched,
  Space its tone only (its +5.9 dB at the probe's value took tp172d's Space impulse 2 → 1: the reference's level moves
  inside that mode's value range), Plate −0.8, Hall 0, Cathedral −3.4, Freeze +2.0, Gated −4.0 dB with their 500 Hz … 8 kHz
  tone. 0 files down on every bank.

## 7. Pro-C 3 → Crest: Upward and Vari-Mu rebuilt (revision 1); TTM read, kept on its measured law [STATIC + EXEC] (sub-round)
Branch `parity190mb-crest` (merged: 504334533). The full notes are in /mnt/e/lynx-work/parity190mb/crest/REA_part.md; in short:
- Upward (UpwardCompressor 0x180161af0, update 0x1801632d0; the generic Detector's purpose-1 branch 0x18010d7d0, one-sided
  knee, × (ratio − 1)): the graph lift E (silence) − follower (E (silence) − E (level)), the style's range limited to 36 dB.
  The "5 dB too loud" of round 190lp is a constant, level-independent cut measured on the stairs:
  (1 − 1 / ratio) · √a · (threshold + 36) dB (a = the attack knob's position), within 0.02 dB on 5 attacks × 3 ratios × 4
  thresholds; where the plug-in applies it was not found (applied as measured).
- Vari-Mu (VariMuCompressor node 0x1801170f0, process 0x1801172a0; shaper 0x180109cf0 = the C library's tanh): a push-pull
  tube stage with feedback — the follower on the output over the threshold sets a bias, the gain is the tube curve's
  slope at it over the signal's own swing. LYNX's own quintic fit of the curve; the stairs within 0.06 dB.
- TTM (TTMCompressor 0x18015eb40): three bands at 88 Hz / 2.5 kHz LR4 — the split Crest's measured three-band law already
  runs, so no Crest + Prism combination; the band processors' hold / look-ahead smoother (0x18015c5e0, 0x18015b890) and a
  second crossover (6.7 / 13.3 kHz) are the next step.

## 8. Not built / findings
- Shimmer and UberMod (item 5): not reached. UberMod's spectra already sit within ~1 dB (its scores are held by the
  modulated taps' time structure, ESR −65 dB on its steady files); Shimmer has no systematic level error per mode (−0.5 …
  +1.4 dB means): neither is a trim job.
- Orbit's Up/Down, through-zero, Tri and Octo voices, FutureVerb's echo modes, Horizon's Room / Chamber tone (it needs
  decay / size dependence: verb183b's Chamber music captures want the opposite of the probe renders): voice work, next.
- Epoch at 96 kHz: see §3 (the reference, not Epoch, is the rate-dependent one).

## 9. Gates, CPU
- Final binary `bin/fxr_w3` against `bin/fxr_m0` (time-window scorer): refcap030 Supermassive / VintageVerb / SpaceModulator /
  Delay and this round's FutureVerb bank — 0 files down (table in docs/PARITY.md "Round 190mb (Valhalla, part 3)");
  every gate bank of all ten Valhalla tags (verb183b captures, tp172d matched settings and probes, refcap182 programme):
  12 up, 0 down.
- Gold (`gold/gold.sh`): with the round's hidden values off (Nebula #15, Epoch #20 / #21, Horizon #25; translated states,
  new stand-ins, 44.1 / 48 / 96 kHz), 126 / 126 renders byte-identical with the before binary.
- CPU (perf user instructions, fxrender 20 s incl. file I/O): Supermassive's tone law +7.6 % on a classic network (the
  8-band corrector), Epoch's rate law +0.01 % at 96 kHz, Orbit's level law +0.01 %, Horizon's revision ≈ 0 where it has no
  trim (+ the corrector's biquads where it has). Crest (sub-round): Upward −0.05 point, Vari-Mu +0.37 point of a core.
