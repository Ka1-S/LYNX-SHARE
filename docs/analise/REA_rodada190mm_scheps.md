# REA round 190mm, sub-round "scheps": Waves Scheps Omni Channel 2 → Ridge Strip's hidden omni law

Owner's decision 3 (round 190mm): build a stand-in chain from the plug-in's five inner modules and their order. Before
this round the plug-in had no entry (a project that used it lost it). Clean-room: the bundle's user guide (PDF), its
ParamXML, the saved states of refcap182's bank and our own demo-mode probes (two takes 7.5 s apart, the demo mutes
spliced out) — no vendor code, tables or presets copied. Every constant below was fitted on our own probe renders.
Lab: /mnt/e/lynx-work/waves4_190mm/scheps (lab/*.py, sim/comp.c, fit/*.json / *.txt, cap/scr/*.txt probe scripts).

## 1. Structure (the state)

- The component state's outer RealWorld list (ParamXML order): 0 input dB, 2 / 3 phase L / R, 4 output dB, 35 limiter on,
  36 limiter threshold, 57 `PARAM_SLOT_SERILIZATION_PLUGIN` = the module order as slot numbers 1 … 6, first to last
  (default 153426), 58 the per-slot bypass digits (not kept up to date by the plug-in; the slots' own flags are used).
- `<slot index="0…5">` each carries one inner module preset (its own SETUP_A RealWorld list) and `<plugin_bypass>`:
  0 Pre (`SOC_Pre`), 1 Gate (`SCS Gate`), 2 DS2 (`SCS DeEsser`), 3 EQ, 4 Comp (`SCS`), 5 the insert slot (a hosted
  plug-in: no LYNX module). Default order 153426 = Pre → Comp → DS2 → EQ → Gate (the guide: Pre sits first by default;
  "make sure your Compressor is post EQ" — i.e. it is not by default).
- Inner lists (left / mid values; the right / side copies follow): Pre 9 saturation %, 10 saturation on, 11 type
  (Odd / Even / Heavy / Crush), 12 thump on, 13 thump +2 / +4, 14 HP on, 15 slope (6 / 12 / 18 / 24), 16 HP Hz, 17 HP Q,
  18 … 21 the LP; EQ band k (LF, Tone, Mid, HF): 6k+1 on, +2 type, +3 gain, +4 Hz, +5 Q; Comp 29 type (VCA / FET / Opt /
  Soft), 34 out dB, 38 ratio, 40 threshold, 42 release ms, 44 attack ms, 78 mix %, 47 / 49 SC HP on / Hz, 51 / 53 / 55
  SC bell on / dB / Hz, 59 / 61 / 63 SC shelf on / dB / Hz; DS2 band 1 at 20 … 24 (on, threshold, range, Hz, type),
  band 2 at 28 … 32, 17 output, 12 / 15 attack reference / release ms; Gate 18 threshold, 23 floor, 24 / 25 attack /
  release, 26 Gate / Exp, 28 close, 13 … 16 SC HP / LP (the gate's indices are read from the default state only: the
  bank never switches the gate on — unverified, see §6).
- A bypassed strip (refcap182 D, every module off) is bit-transparent (-220 dB), latency 0.

## 2. EQ (probe batches eq1 / eq2: impulse responses, lab/fiteq*.py, lab/fitgen.py)

- Every band is one biquad (a free biquad fit nulls every probe to -160 dB).
- Shelf types: RBJ shelves. LF Shelf: f0 = f × r(|g|) pre-warped (ln r 2.753 / 2.055 / 1.309 / 0.845 at 3 / 6 / 12 / 18 dB,
  ~1 / g below), Q 0.490 … 0.458 boost, 0.503 … 0.535 cut (-103 … -130 dB). HF Shelf: f0 from the unwarped
  K = π f r / sr with log2 r = 0.04155 (|g| - 12), Q 0.498 … 0.440 (-66 … -82 dB).
- Bells (Tone / Mid Wide and Narrow, Parametric, LF / HF Parametric): an analog bell with pole Q = q·A and zero Q = q / A
  (A = gain^½), bilinear pre-warped poles, numerator matched to the analog magnitude at DC, f0 and Nyquist (-60 … -108
  dB at 200 Hz / 1 kHz; -32 … -43 dB at 5 / 8 kHz). Parametric q = Q / √2; Wide / Narrow q against |gain| per band
  (Tone Wide 0.235 → 0.471, Tone Narrow 0.471 → 0.937, Mid Wide 0.045 → 0.326, Mid Narrow 0.188 → 1.168 from 1 to 18 dB).
- ResShelf (both shelves): not reproduced exactly (LF is not one biquad); an RBJ shelf approximation (§6).

## 3. Pre (probe batch pre1)

- Filters: RBJ high- / low-pass. 6 dB: first order; 12 dB: second order, Q 0.7071·Q (Q ≥ 1), 0.7071·Q^0.301 below;
  18 dB: second order (Q 1 at the default) × first order; 24 dB "resonant": two second-order sections at f·10^∓0.2 with
  Q 4.016·√Q and 2.259·√Q (all -110 dB).
- Thump: a very wide low bell (+2: 65 Hz Q 0.107 2 dB; +4: 122 Hz Q 0.098 4 dB) and a faint high shelf (5.8 kHz, +0.27 dB),
  -37 … -45 dB.
- Saturation: Odd / Even = a level-independent gain (Even 0.5 + 0.05 × amount dB; Odd +1.39 dB at 30, +8.69 at 100) times a
  first-order all-pass at ~8 Hz (in phase at the top, inverted at DC: -70 dB fit of the sweep), harmonics -66 dB at 30 %;
  Heavy = a static odd soft clipper (residual -148 dB; ceiling 0.215 at 100 %, knee n ≈ 5, gain +1.76 / +9.47 dB at 30 /
  100); Crush = clipping with its own phase shift (approximated by a harder clipper, §6).

## 4. Compressor (probe batches comp1 stairs / bursts, mk1 / mk2 the make-up grid, prog1 programme)

- Feed-forward log-domain gain computer per type (feedback fits worse on our probes): threshold offset, knee, ratio
  scale (VCA 2.9 dB / 14.7 dB / 1.0, FET 3.76 / 18.1 / 1.52, Opt 2.29 / 16.0 / 0.65, Soft -0.54 / 34.1 / 0.67); ratio
  capped at 20 : 1 ("Inf" = 20, read from the make-up grid).
- Automatic make-up: always on; a smooth function of threshold × ratio (separable within ±0.1 dB) times a factor of
  attack × release (longer attack less, longer release more make-up). LYNX keeps our measured grid (fit/makeup.json:
  7 thresholds × 8 ratios at 0.4 / 50 ms and 5 × 5 attack / release at -30 dB 4 : 1 per type) and interpolates
  (checked on five other settings within 0.05 dB).
- Time constants: VCA attack 0.22 ms + 0.047 × setting, release 0.58·(rel / 50 ms)^0.4·50 ms plus a slow stage (3.5 × rel,
  weight 0.24 + 0.51·log10 (rel / 200 ms)); FET attack 0.45 × setting, release 1.1·(rel / 50)^1.17·50 ms; Soft 0.15 ms +
  0.042 × setting, 0.49·(rel / 50)^0.43·50 ms + slow stage; Opt (programme probes): attack 0.0367 × max (setting, 50 ms
  from a 200 ms release on), release 0.385 × setting.
- Mix: linear dry / wet after the make-up; out gain after the mix (X4: -30 … -40 dB ESR on the bank).

## 5. DS2 and Gate

- DS2 (prog1, sweep / stair probes): per band a side-chain shape (high-pass / low-pass for the shelves, bells for Bell /
  Notch) detects; above the threshold the band is cut on a shelf / bell of the same type (shelf corner 0.55 × f for the
  high shelf, 1.8 × f for the low shelf, Q 0.6), up to the range, 3 : 4 slope; matches our music probe to -30 dB.
- Gate / expander: a standard model (threshold, Close as the lower closing threshold, floor, attack, release, 2 : 1
  expander mode, SC HP / LP). Not probed (the bank never uses it).

## 6. Not reproduced / approximations (documented, reversible)

- Stereo modes Duo / M-S and unlinked left / right values (the translator reads the left / mid values, Stereo mode).
- ResShelf shapes, Crush's phase, the even / odd harmonic curves above 30 % (approximated), the output limiter (a plain
  instant-attack peak limiter), the insert slot (a hosted plug-in), external side chain, listen / monitor modes.
- The Opt type's attack depends on the programme (bursts say ~50 ms, programme probes ~2 ms): LYNX follows the programme.

## 7. Results (refcap182 bank, 25 files; before: no stand-in, n = 0)

See /mnt/e/lynx-work/waves4_190mm/scheps/HANDOFF.md for the table, CPU and gold renders.
