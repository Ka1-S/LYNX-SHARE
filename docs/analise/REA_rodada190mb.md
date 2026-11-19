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
