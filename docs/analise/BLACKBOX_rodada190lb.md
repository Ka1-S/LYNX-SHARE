# Black-box round 190lb — licence-protected plug-ins studied from outside (owner, 2026-11-17)

Owner's rule of 2026-11-17: **never break the encryption.** Round 190kt (`REA_rodada190kt.md` §1) found the code of SSL,
Harrison, LiquidSonics Lustrous Plates, Antares Auto-Tune, Sonic Academy ANA 2, D16 Repeater and Overloud TH-U wrapped
by PACE Fusion (`.text` at entropy 8.000). This round treats each of them as a **black box**: what the plug-in exposes
through the normal VST3 API, its audio measured from outside, public manuals and research, and public descriptions of
the hardware the plug-in emulates. LYNX's models are written from those descriptions and fitted on our own renders.

Not done, by rule: no decrypting, unpacking or dumping of protected code, no hooking inside a protected module, no licence
check or trial limit touched, no preset / table / IR / audio content of a vendor copied into LYNX. Saved states were read
only where LYNX must import them and only where they are plain (XML, a JUCE ValueTree).

Work folder: `/mnt/e/lynx-work/blackbox190lb/` (lab scripts in `lab/`, logs in `logs/`, HANDOFF.md).

## 1. What could be observed this round

Every trial of this group ended on 2026-11-13 (iLok / PACE) and the Antares trials before 2026-11-04
(`/mnt/e/lynx-work/refcap030/INVENTORY.md`). refcap030's alive probe of 2026-11-15 showed PACE's "Software Activation"
window at load for SSL, Harrison, LiquidSonics and D16 (host stopped, nothing clicked). Loading them again would only open
that window on the owner's screen (he may be on AnyDesk), and its buttons are not among the allowed licence answers, so
**no plug-in of this group was loaded this round**. Method 1 therefore rests on what was captured through the plug-ins' own
VST3 API while the trials ran (refcap0124's host tables: every parameter's name, unit, steps and the plug-in's own text at
nine positions, `/mnt/e/lynx-work/refcap0124/par/<plugin>.tsv`; saved states in tp172c / refcap182 `st/`), method 2 on the
banks rendered before the trials ended (tp172c, refcap0124, refcap182, trial190k), method 3 on public sources (§9).

| plug-in (code PACE-encrypted) | loads today | API data from before expiry | audio references | method this round | result |
|---|---|---|---|---|---|
| SSL 4K G | no (PACE activation) | parameter table, XML states per setting | tp172c K4G_ir 412 impulse renders, K4G_dyn, K4G_mic, refcap182 programme | 1 + 2 + 3 | **Pink EQ, own filters, output ceiling: implemented, scored (§2)** |
| SSL 4K E | no | table, XML states | tp172c K4E_ir (600), refcap182 | 2 + 3 | **Black / Orange high-pass 18 dB/oct: implemented, scored (§3)** |
| SSL 4K B, Channel Strip 2, Bus Compressor 2 | no | tables, states | tp172c, refcap182 | 3 | spec notes only (§3); unchanged |
| Harrison 32Classic Channel Strip | no | table, XML states | tp172c H32_eq 148 impulse renders | 1 + 2 + 3 | **exact EQ law found (§4)**; implementation needs an owner decision (Ridge's ranges) |
| LiquidSonics Lustrous Plates | no | table, ValueTree state (plain) | refcap0124 144 sweeps, refcap182 20 programme | 1 + 3 | parameter laws and state format (§5); translator not built (§5.4) |
| Antares Auto-Tune Pro / EFX+ | no (expired before 11-04) | — (states already read since round 127) | tune157 / tune165 (round 165j) | 3 | nothing new to build (§6) |
| D16 Repeater | no (PACE activation) | none (refcap030's probe empty) | none | 3 | **substitute added: Echoform, spec-based, unscored (§7)** |
| Sonic Academy ANA 2 | no | none | none | 3 | spec only (§8) |
| Overloud TH-U | no (iLok group) | none | none | 3 | spec only (§8) |

## 2. SSL 4K G — the Pink EQ colour, its filters and its output ceiling

### 2.1 Evidence
- **API (method 1)**, refcap0124 host table: `EQ Colour` is a two-step switch, **Pink (default)** / Black; LF 30 … 450 Hz,
  ±18 dB, Shelf / Bell; LMF 0.20 … 2.50 kHz, ±22 dB, Q 3.00 … 0.50, "÷3"; HMF 0.60 … 7.00 kHz, ±22 dB, Q, "×3"; HF 1.50 …
  16.0 kHz, ±20 dB, Shelf / Bell; high-pass OUT, 16.1 … 350 Hz; low-pass OUT, 20.88 … 3.00 kHz. The saved state is plain XML
  (`<SSL_PLUGIN_STATE PluginName="SSL 4K G">`, `<PARAM id="EqType" value="0.0"/>` = Pink), values in the dial's units.
- **Retail / review texts (method 3)**: the switch selects between the "pink" and "black" SSL EQ versions and changes their
  ranges ([Sound On Sound](https://www.soundonsound.com/reviews/ssl-4k-g)); dealers' copy calls Pink the variable-Q one
  ([Best Service](https://www.bestservice.com/en/4k_g.html), [Thomann](https://www.thomannmusic.com/ssl_4k_g.htm)). The
  G-series console EQ of 1987 ties Q to gain, its shelves show a dip / bump before the shelf, and ÷3 / ×3 replace the
  mids' Bell switches ([Abbey Road Institute](https://abbeyroadinstitute.nl/blog/ssl-e-g-series-eq/)).
- **Audio (method 2)**: tp172c's K4G_ir grid (made on the trial, 2026-11-05: every band × 5 frequencies × 6 gains, both
  colours, Q × gain at the mid dial, ÷3 / ×3, 7 high-pass and 7 low-pass positions; impulse 0.5 at 48 kHz, dynamics out).
  Before this round LYNX played Pink as Black: the mean error of LYNX's impulse response against the reference's
  (30 Hz – 16 kHz, rms dB, `lab/curveerr.py`) was 3.5 dB on Pink mids, 4.2 dB on Pink LF "bell", 4.1 dB on every
  high-pass setting of either colour.

### 2.2 What the measurements say (lab `fitpink.py`, `fithp.py`, `fitlp.py`; one second-order section fitted per render)
- **Pink bands are one second-order section each, boost and cut mirror each other** (same frequency and Q, opposite gain:
  LF at ±18 dB 63.0 / 64.7 Hz, −20.3 / +19.7 dB).
- **Pink LF and HF have no bell**: the Bell switch gives the same response as the shelf (all 60 pairs identical).
- **Pink shelves** are resonant second-order shelves (Q 0.45 … 0.69: a dip of ~1 dB after a full LF boost, e.g. −1.2 dB at
  3.2 kHz for +18 dB at 200 Hz); their turnover moves down as the gain rises (LF dial 30 Hz: 129 Hz at ±5.4 dB, 64 Hz at
  ±18 dB), and the plateau grows faster than the dial (±5.4 dB → 3.6 dB, ±18 dB → 20.0 dB).
- **Pink bells: proportional Q**: dial Q 3.0 / 1.5 / 0.5 gives Q 0.55 / 0.42 / 0.17 at ±6.6 dB and 0.76 / 0.53 / 0.21 at
  ±22 dB; the centre rises with the gain (1 kHz dial: 922 Hz at ±6.6, 984 Hz at ±15.4, 1080 Hz at ±22); the dial's full
  ±22 dB is reached. Frequency at full gain per dial: LMF 0.2 / 0.47 / 1.0 / 1.83 / 2.5 kHz → 204 / 503 / 1080 / 1587 /
  2068 Hz; HMF 0.6 / 1.03 / 3.0 / 5.5 / 7.0 kHz → 608 / 1500 / 3210 / 4723 / 6148 Hz. ÷3 moves the 1 kHz dial to 262 Hz
  (×0.242), ×3 the 3 kHz dial to ~9.2 kHz (×2.88, from clipped renders).
- **Filters, both colours alike**: the high-pass is **third order (18 dB/oct)**: a first-order and a second-order section at
  the same frequency, Q ≈ 1 (146.7 Hz dial: −8.0 dB at 100 Hz, −20.6 at 60 Hz, −38.5 at 30 Hz; fit rms 0.05 dB up to 233 Hz);
  LYNX's 4K voicing (fitted on the 4K E) has a 12 dB/oct one. The low-pass is a Butterworth second-order section **without
  bilinear cramping** (3 kHz dial: −31 dB at 20 kHz where a bilinear section gives −46), and its first dial position
  (23.8 kHz) is still active.
- **Output ceiling**: every render whose peak would pass 1.1108 is hard-clipped there, both polarities, either colour
  (55 of 412 renders: the large HF and mid boosts). The renders cannot tell whether the clip sits before or after the
  output trim (all trims were 0); LYNX puts it before.

### 2.3 LYNX (behind hidden parameters, off = the old sound)
`core/VoiceMatch` `echannel::Settings::pink` (+ `lmfDiv3`, `hmfMult3`) and `k4g`; Ridge Strip hidden parameters
`k4Pink`, `k4LmfDiv3`, `k4HmfMult3`, `k4gLaw` (after `compAttackFine`, saved only while non-zero), set only by the SSL 4K G
translation (`EqType` 0 → Pink; `k4gLaw` for either colour). The Pink bands come from `core/Console4KPinkTables.inc`
(generated by `lab/mktab.py`: per dial frequency node × |gain| node the fitted f0 / gain / Q, interpolated in log frequency
and in gain, mirrored for cuts; shelves are the 4K voicing's bilinear second-order shelves, bells its magnitude-matched
peaks); the high-pass is a first-order + second-order pair from a 7-node table, the low-pass a magnitude-matched
second-order section from a 7-node table; the ceiling is a clamp at 1.1108 before the output trim. New Ridge Strips, the
4K B / Channel Strip 2 translations, the 4K E's (except its Black / Orange high-pass, §3) and every project saved before
keep the old code path.

### 2.4 Scores (fxparity 0–10; base = round 190kt's fxrender, new = this round's; `logs/`, `runs/`)
| bank (reference: SSL 4K G trial) | files | before → after | up / same / down |
|---|---|---|---|
| tp172c K4G_ir (impulse grid, both colours) | 412 | 5.01 → **6.50** | 236 / 175 / 1 |
| · Pink only (LF / LMF / HMF / HF / HPF / LPF) | 206 | 4.07 → **6.73** (4.6→6.8 / 3.4→7.4 / 3.4→6.4 / 4.0→6.2 / 5.0→7.4 / 5.7→7.4) | |
| · Black only | 206 | 5.94 → 6.28 (filters 5.0 → 7.4 / 5.9 → 7.7; bands +0.1 … +0.3) | |
| tp172c SSL_4K_G (D, S1, S2, S3 on drums / music / stair / sweep / vocal) | 20 | 5.10 → **6.00** | 6 / 13 / 1 |
| tp172c K4G_dyn, K4G_mic | 114 + 28 | 5.47 → 5.47, 6.11 → 6.11 | bit-identical paths |
| refcap182 programme (D, S1–S3 × 5 signals) | 20 | 5.45 → **6.25** | 7 / 13 / 0 |
| **all** | **594** | **5.17 → 6.26** | 249 / 343 / **2** |

The two files that went down (the gate's "nothing worse" is not met to the letter — owner decision §11):
- `SSL_4K_G S1_stair` 7 → 5: S1 is Pink with LF +6 dB, LMF −4 dB at 0.4 kHz, HMF +5 dB, HF +4 dB. At 1 kHz the reference
  gives +0.33 dB, LYNX now +1.16 (before, playing Black, +0.58), so a 1 kHz sine stair is 0.8 dB loud; the same setting's
  programme files rose (refcap182 S1 5 / 5 / 5 / 5 / 5 → 5 / 6 / 5 / 6 / 5). Every single band of the grid fits within
  0.05 … 0.7 dB; this combination of four small gains between the grid's nodes does not (rms 0.57 dB over the sweep).
  Not tuned on this file: matching it needs four free band changes that contradict each other (LMF deeper, HMF
  shallower), i.e. fitting a scored render.
- `K4G_ir BlHFBf1g5` 4 → 3: Black, HF bell +20 dB at 3 kHz — the reference clips at the ceiling and so does LYNX now,
  at a different level (Black's measured nodes stop at ±15 dB on the 4K E's scale).

CPU (`logs/cpu.log`, one binary, the new law on vs forced off, 280 s stereo programme, user seconds, best of 3, two
passes): Pink S1 1.74 / 1.72 vs 1.68 / 1.75, Black S3 1.69 / 1.94 vs 1.72 / 1.99 — no measurable difference (same
section count; the ceiling is a clamp). Latency 0 (unchanged).

## 3. SSL — the 4K E's high-pass, and spec notes on the others
- **4K E — its Black / Orange high-pass (implemented)**: Black (242) has an 18 dB/oct high-pass on the console
  ([Abbey Road Institute](https://abbeyroadinstitute.nl/blog/ssl-e-g-series-eq/)). tp172c's K4E_ir HPF renders confirm it
  for the plug-in: Black and Orange are third order (146.7 Hz dial: −7.8 dB at 100 Hz, −20.2 at 60 Hz), Brown is second
  order (−2.7 / −9.5). LYNX's 4K voicing (round 148) used Brown's 12 dB/oct for every colour. Now the 4K E translation sets
  hidden `k4eHpLaw` for Black / Orange: a first-order + second-order pair per colour from a 7-node table fitted above
  the renders' hiss floor (`lab/fithp2.py`, rms 0.02 … 0.28 dB). Scores (`lab/changed.py`: every 4K E setting of the
  banks rendered with both builds; only those that differ scored): K4E_ir 600 settings, 14 changed (the Black / Orange HPF
  ones) 5.14 → **7.43** (14 up, 0 down); tp172c SSL_4K_E 1 setting changed, 3.80 → 4.60 (2 up, 0 down); refcap182 1 setting
  changed, 4.20 → 4.60 (2 up, 0 down); K4E_dyn / dynr / gate unchanged. The 4K E's low-pass is a different, cramped design
  (its renders), so it keeps round 148's.
- **G-series bus compressor / Native Bus Compressor 2**: attack 0.1 / 0.3 / 1 / 3 / 10 / 30 ms, release 0.1 / 0.3 / 0.6 /
  1.2 s / Auto, ratio 2 / 4 / 10 : 1 on the hardware ([SOS, XLogic G](https://www.soundonsound.com/reviews/ssl-xlogic-g-series-compressor));
  the console's design is a feedback VCA compressor (also REA_rodada190kt §2 on Waves' licensed model). Round 190kt's
  console law fits Bus Compressor 2's static curve but not its timing; what is missing is level-step probes of the
  plug-in (a licence).

## 4. Harrison 32Classic Channel Strip — the EQ law (evidence; implementation pending a decision)

### 4.1 Evidence
- **Manual** ([32Classic manual v1.5](https://harrisonaudio.com/assets/uploads/downloads/32Classic_manual_v1.5.pdf),
  §2.1.3): Hi and Lo bands are **6 dB/oct shelves**, switchable to proportional-Q bells; Hi-Mid and Low-Mid are
  second-order proportional-Q bells without a Q knob; 12 dB/oct filters after the EQ. The plug-in build measured (trial
  of 2026-11) reads its dials wider than the manual's table: Lo 18.9 … 799 Hz, Low-Mid 104 … 4366 Hz, Hi-Mid 271 … 8000 Hz,
  Hi 689 … 14.5 kHz, ±13.74 dB.
- **Audio**: tp172c's H32_eq grid (148 impulse renders), fitted after removing the default 25 Hz / 20 kHz filters that
  run in every render (`lab/fith32.py`, `logs/fith32.log`).

### 4.2 What the measurements say
- **Shelves are exactly first order** (fit rms 1e-5 … 2e-4 dB on all 60 shelf renders): a pole / zero pair whose
  geometric centre depends on the frequency dial only (Lo 18.9 / 58.8 / 111 / 369 / 799 Hz → 25 / 85 / 145 / 823 /
  1500 Hz; Hi 689 / 1390 / 3620 / 9020 / 14500 Hz → 400 / 1520 / 2630 / 6100 / 9400 Hz) and a plateau that is not the dial:
  ±2.49 → ±4.05 dB, ±6.94 → ±9.45 dB, ±13.74 → ±13.5 dB (the Hi shelf's last two dial positions reach 12.3 / 11.1 dB,
  the plateau lying beyond Nyquist). Boost and cut mirror each other.
- **Bells are exact at the dial**: centre = the dial's frequency, peak gain = the dial's gain; Q depends on the gain only
  (0.525 at ±2.49 dB, 0.57 at ±6.94, 0.75 at ±13.74; analog peaking prototype, fit rms < 0.08 dB up to 8 kHz — no
  bilinear cramping). The end bands' Bell mode has the same law.

- **Filters are 12 dB/oct** and reach far into the mids: high-pass 25 Hz … 3.15 kHz, low-pass down to ~160 Hz (H32_eq
  hpf / lpf renders; the manual's 25 Hz – 3.1 kHz / 160 Hz – 20 kHz).

### 4.3 LYNX today and what it would take
LYNX plays the 32C on Ridge Strip's own engine (round tp172c5): its second-order shelves at a fitted corner / plateau,
bells with a gain-dependent Q, and band swapping where a 32C dial falls outside Ridge's band ranges. Measured on the H32_eq
bank with today's build (`runs/h32base.json`, `lab/curveerr.py`-style comparison of the impulse responses, 30 Hz – 16 kHz):
fxparity **5.4** over 148 renders; curve error 0.38 / 0.42 dB on the mid bells, 1.00 / 1.16 dB on the Hi / Lo shelves,
0.72 / 0.80 dB on the end bells, and **8.3 / 9.0 dB on the filters** (Ridge's high-pass is 18 dB/oct and stops at 350 Hz,
its low-pass stops at 3 kHz: a 3.15 kHz high-pass plays at 350 Hz). The exact law above needs first-order shelves, the
32C's 12 dB/oct filters and band / filter ranges that hold the 32C's (its Lo shelf's centre reaches 1.5 kHz, Ridge's LF
dial stops at 450 Hz). Two ways (owner decision, §11): **(a, recommended)** a "32C" voicing of Ridge
Strip with its own ranges and read-outs, as the 4K and Modern voicings have; (b) hidden per-band frequencies set by the
translation (the visible dials would no longer drive those bands — not recommended).

**Built in round 190le** (owner decision 3, recommended option): Ridge Strip's "32C" voicing — Ridge's own engine with
this law (Lo shelf bilinear prewarped at its centre, Hi shelf magnitude-matched at DC / centre / Nyquist, Vicanek-matched
bells and Butterworth filters), the original's own dial scales (the host table's texts) and read-outs, a hidden parameter
with its own panel switch. H32_eq 5.36 → 9.99, curve error 1.15 → 0.00 dB rms. `docs/PARITY.md`, round 190le.

## 5. LiquidSonics Lustrous Plates — parameter laws and state (spec)
### 5.1 API (refcap0124 host table)
Master Gain −20 … +20 dB (linear); Dry/Wet "Dry" … "Equal Mix" … "Wet" (default Wet); Plate Model 25 steps (Chrome,
Rhodium, Iridium, Gold, then "Reserved (Using Iridium)"); Decay "Damper" 0 … 10 (linear, default 5.0); Dispersion 0 … 10;
Dispersion Bias L … Balanced … R; Pre-delay 0 … 500 ms with **ms = 500 · v^2.5** (0.209 → 10 ms, the default; 0.5 → 88 ms);
Width Mono … Full (0 … 10, default 7.5); Modulation Depth, Rate (Off, 0.35 … 1.50 Hz); Master EQ: low cut 20 … 750 Hz, low
band shelf / bell 50 Hz … 2 kHz ±12 dB, high band 2 … 12 kHz ±12 dB, high cut 500 Hz … 18 kHz at 6 / 12 / 18 / 24 dB/oct;
a dynamics block (duck / compress / gate). No factory programs through the API.
### 5.2 State
The VST3 component state is a JUCE ValueTree (`LP_FullState` → `StateVT` with `PARAM id="<index>" value=<double>` in the
parameter table's own units, e.g. id 3 = Damper 5.0, id 6 = pre-delay 10 ms), plain: readable with LYNX's existing
JUCE-state helpers (`lab/vtree.py` reads it). refcap182's four settings (D / S1 / S2 / S3) are all 100 % wet: Damper 5 / 5 /
2.5 / 8, pre-delay 10 / 0 / 10 / 10 ms, model 0 / 0 / 0 / 12.
### 5.3 Physics (method 3)
The EMT 140 is a 2 × 1 m steel plate 0.5 mm thick, one driver, two pickups, decay set by a porous damper 11 … 66 mm from the
plate (closer = shorter); losses: thermoelastic (nearly constant above ~500 Hz), radiation and attachment; modal density
roughly constant in frequency, so the high end is dense from the start and there are no discrete early reflections
([Russo, AAU thesis](https://projekter.aau.dk/projekter/files/517547034/Master_Thesis_Russo.pdf) ch. 1 / 3, after
[Arcas & Chaigne](https://ccrma.stanford.edu/~bilbao/dafx10/electro.pdf)). The plug-in's manual adds plate reverb times of
just under 1 s to ~5 s, varying by band, and a low-frequency onset ~10 ms later than the highs (dispersion)
([Lustrous Plates manual](https://www.falkemedia-shop.de/media/pdf/b4/00/f4/Lustrous_Plates_ManualCddKXRlmsIyQw.pdf)).
### 5.4 Why no translator this round
(Round 190le: 190bz is in; the translator was built on round 190ca's physical Foil fit, which runs on Foil's Classic
law — untouched by 190bz — so it needed no refit. `docs/PARITY.md`, round 190le.)
Today the role preset plays (Foil Plate, Decay 0.3, 25 % wet). A translator needs Damper → Foil decay and model → Foil type
per band, calibrated on Foil's wet law, which round 190bz is changing (`/mnt/e/lynx-work/nebula190ca/HANDOFF.md`: all Foil maps
need a refit after 190bz lands); its only scorable references with states are refcap182's 20 programme files (the tp172c
default render is silent). Next step, once 190bz is in: per-band T30 of refcap0124's Damper and Model sweeps (p002, p003)
→ Foil Decay / Type, Dry/Wet → Mix, pre-delay through the law above.

## 6. Antares Auto-Tune — nothing new to build
LYNX Tune already reads Auto-Tune Pro / EFX+ states (rounds 120 … 165j) and models Classic mode without Flex-Tune, as the
manual says ([Auto-Tune Realtime Advanced manual](https://media.uaudio.com/support/manuals/dd/Auto-Tune%20Realtime%20Advanced%20Manual.pdf):
Retune Speed in ms, 0 = instant; Flex-Tune shrinks the correction window; Humanize slows the retune on sustained notes;
Tracking 50 default, higher = more relaxed; Classic = the Auto-Tune 5 sound, no Flex-Tune). Measuring more needs the licence
the owner said he would activate himself (memory: Auto-Tune licence).

## 7. D16 Repeater — spec, and an Echoform stand-in (spec-based, unscored)
- **Manual** ([D16 Repeater manual](https://d16.pl/pub/manuals/Repeater-manual-gb.pdf)): input gain −12 … +12 dB into a
  clipper with a Colour control; a delay line per channel 0.1 … 1000 ms (or tempo-synced) with feedback L / R, an
  "Analog" mode (each pass darker) and Ping-Pong (crossed loops); a phase inverter, a dual filter (HPF 40 Hz … 1 kHz, LPF
  1 … 22 kHz) and a stereo mixer after the loop; 23 delay models (tape: Space Delay, Plexy Echo, Tape Delay Ancient /
  Classic / Modern / Vintage, Cassette Tape; bucket-brigade: DM-2, Mirky, Memory Guy; digital and lo-fi ones) that change
  the line's degradation / detune / saturation, the loop's frequency response and the filter calibration — no wow /
  flutter control.
- **Hardware** (the Space Delay model's original, the Roland RE-201): heads 2 and 3 at 2× / 3× head 1
  ([Roland](https://static.roland.com/manuals/gx-10_parameter/en-US/95886987161270155.html)); measured tape drift is
  noise rolling off near 1 Hz, speed changes settle with 1.1 … 1.9 s time constants, a Baxandall tone stack around
  750 Hz and tape saturation + band limit in the loop
  ([Downing & Terjesen, U. Rochester](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/projects/2016/Downing_Terjesen_paper.pdf));
  modelling reference: Arnardottir, Abel & Smith, "A digital model of the Echoplex tape delay", AES 125 (2008).
- **LYNX**: no reference render and no saved state of Repeater exist, so no setting can be translated. New substitute row
  `[d16-repeater]` (name "Repeater"): Echoform at the delay role's quarter-note preset (as SSL X-Echo / Stellar Echo /
  Harrison 3D Delay), so a project that used it plays an echo instead of a missing plug-in. **Spec-based, unscored.**
  What a measured model would need: one capture session (impulse trains per model at fixed times, feedback steps) —
  a licence.

## 8. Sonic Academy ANA 2 and Overloud TH-U — spec only
- **ANA 2**: a subtractive / hybrid synth — three oscillator types (analog, advanced noise, attack), 133 waveforms, 23
  filter types in ANA 1 (2 / 4-pole LP / BP / HP, vintage multimode, formant), 46 in ANA 2; filter and amp envelopes with
  three curve shapes; effects (phase, distortion, compression, delay, reverb, chorus)
  ([KVR](https://www.kvraudio.com/product/ana-by-sonic-academy), [KVR news](https://www.kvraudio.com/news-print.php?id=39339)).
  No state sample, no render: a stand-in would play an invented patch, so none is added (owner decision §11).
- **Overloud TH-U (Slate edition)**: 39 amps, 39 cabinets, 35 effects; the full product 239 models, amps named after the
  originals they capture ([Mixdown](https://mixdownmag.com.au/news/slate-digital-announce-update-th-u/),
  [Overloud](https://overloud.com/node/219)). A behavioural model per amp = preamp + tone stack + power-amp saturation +
  cabinet IR; without a state sample or renders a stand-in would be a guess of the amp, so none is added (§11).

## 9. Sources
Manuals and product pages: SSL 4K G (Sound On Sound review; Best Service; Thomann), SSL XLogic G-series compressor (Sound On
Sound), Harrison 32Classic manual v1.5, Lustrous Plates manual, D16 Repeater manual, Antares Auto-Tune Realtime Advanced
manual, KVR (ANA), Overloud / Mixdown (TH-U). Background: Abbey Road Institute (SSL E / G EQ history), Russo (AAU master
thesis, EMT 140 modelling, after Arcas & Chaigne), Downing & Terjesen (RE-201 measurements), Arnardottir / Abel / Smith (AES
125, Echoplex model), Roland (Space Echo head ratios). All links in the sections above; texts fetched into the round's
scratch, nothing of them is copied into LYNX.

## 10. Gate
- **Old projects bit-identical** (`lab/gold.sh`, base = round 190kt's fxrender): (1) the substitutes built from the
  states of the strips whose translation is unchanged (4K E Brown, 4K B, Channel Strip 2, Vocalstrip 2, Harrison 32C)
  through `--sub`; (2) for the SSL 4K E Black / Orange and 4K G states, the Ridge Strip a project saved before holds (the
  base's translated parameters, `--dump`, set on a plain Ridge Strip in both builds); (3) Ridge Strips set directly (own
  engine brown / black, E voicing, 4K voicing in three colours with filters, Modern); 44.1 / 48 / 96 kHz on music, drums
  and the impulse: **522 renders, 0 mismatches**.
- Tests: `tests/Parity190lbTests.inc` (7 tests) + the Black pin in `Tp172cTests.inc`; mini runner on the round's objects
  (190kt's list + the Ridge / strip tests): see HANDOFF.
- Scores and CPU: §2.4, §3.

## 11. Owner decisions
1. **Ship the SSL 4K G Pink / filters / ceiling law with its two down files** (594 files: 249 up, 343 same, 2 down;
   5.17 → 6.26) — recommended (reversible: the law is hidden and set only by the translation), or hold it until a licence
   allows probes of band combinations. The plug-in quality gate asks for "nothing worse", so this needs the owner's yes.
2. **SSL 4K E Black / Orange high-pass at 18 dB/oct**: 24 changed files, 24 up / 0 down — recommended: ship.
3. **Harrison 32C**: a Ridge "32C" voicing with its own ranges (first-order shelves, 12 dB/oct filters reaching 3.15 kHz /
   160 Hz, bells at the dial with Q(|gain|)) — recommended for the next round; or hidden per-band frequencies (not
   recommended: the visible dials would stop driving those bands).
4. **D16 Repeater → Echoform at the delay role's preset** (spec-based, unscored) — taken (reversible); ANA 2 and TH-U get
   no stand-in (an invented patch / amp would be a guess) — taken; say if a generic stand-in is wanted.
5. **Licences that would allow measuring**: SSL (Bus Compressor 2 step probes; 4K G band combinations), LiquidSonics
   (Lustrous Plates per-model decay probes — or the existing refcap0124 sweeps after round 190bz's Foil refit), D16
   (Repeater: every model), Sonic Academy (ANA 2), Overloud (TH-U), Antares (Auto-Tune: the owner activates it himself).
