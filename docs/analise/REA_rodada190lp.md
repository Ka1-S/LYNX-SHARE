# Round 190lp: Crest's Smooth / Op-El on Pro-C 3's graphs, Pro-L 2 / Pro-G black-box style fits, Pro-R 2's fixed early taps (0.1.30 parity)

2026-11-18. Continues round 190lg (docs/analise/REA_rodada190lg.md). Owner's permission as there: static analysis of
third-party plug-in binaries for sound parity, clean-room only. Laws and structure are written down in our own words and
re-written as LYNX's code. No code, tables, piecewise curve points or presets (embedded presets included) are copied, and
no protection is bypassed. Evidence markers as in 190kw: [STATIC] read in the decompiled / disassembled binary, [EXEC]
measured on renders, [CODE] LYNX source. Every score uses the time-based alignment window (190lg §1); "before" is
rea2_190lg's final fxrender (fxr_m4).

Owner decisions on 190lg's open items, recommended options taken: (1) Pro-R 2: build the early-reflection taps first,
then refit and re-gate, merge only with no file down (or listed one-step drops with a clearly better waveform match);
(2) Pro-L 2 / Pro-G: black-box fitting only (their styles are embedded presets, not read); (3) Crest's remaining Pro-C 3
styles by the Versatile method.

Status: §1 Crest Smooth / Op-El landed (Upward / Vari-Mu / TTM read, not built); §2 Apex / Sentry revision 1 landed;
§3 Pro-R 2 taps built on branch `rea3_190lp-r2`, NOT merged (26 refcap030 files + 1 programme file down; owner decision).

## 1. Pro-C 3 → Crest: Smooth and Op-El rebuilt (revision 1); Upward, Vari-Mu, TTM read, not built [STATIC + EXEC] (round 190lp, part crest)

Branch `rea3_190lp-crest` (off `rea2_190lg` 2e04775e5). Evidence: the existing decompiles in
`/mnt/e/lynx-work/rea190kw/rea/out/proc3_{smooth,smctor,opel,opel2,vmu,vmctor,styles,det,cap,e5b0,nodes}.c`, this round's
headless Ghidra output on its own copy of the proc3 project (`crest/rea/eq*.c`: EQFilter / CustomTransformFilter / VolumeFilter /
analog prototype; `up.c`: UpwardCompressor; `ttm.c`: TTMCompressor; kept in crest/rea/), objdump disassembly where the decompiler dropped float
arguments, constants read with `rea/rd.py` on bin/FabFilter_ProC_3.dll. Everything below is written in LYNX's own words;
LYNX's code is written from these laws. The reference's piecewise-linear knob curves are not copied: LYNX fits its own
smooth curves to their shape. Addresses are image virtual addresses (base 0x180000000).

### Where the styles live [STATIC]
`ProCCompressor`'s constructor (0x1801696d0) creates one object per Pro-C 3 style and keeps it: +0x270 Smooth
(`SmoothCompressor`, 0x18015a8f0), +0x290 Versatile (0x180167ce0), +0x288 Vari-Mu (`VariableMuCompressor`, 0x180163780),
+0x278 Op-El (`OpElCompressor`, 0x180153e30), +0x280 Upward (`UpwardCompressor`, 0x180161af0). The internal style enum
(`CompressorStyle`) numbers them 8 Smooth, 9 Versatile, 10 Vari-Mu, 11 Op-El, 12 Upward, 13 TTM. The per-style wiring is
in the compressor's graph function (0x18016b990, cases 8 … 13). TTM (13) has no detector object there: its side chain only
passes through, and the work is done by a separate multiband object (`TTMCompressor`, created at 0x18015eb40 from the
plug-in's audio graph, 0x180194627) with a crossover and per-band processors.

Generic elements met here (in addition to round 190kw's Detector / Capacitor / Mixer / DBConverter):
- `Mixer` operation 3 = product of (input × its gain) (0x18011f950); operation 0 sum, 2 maximum of magnitudes.
- `TransformFilter` (0x1801199f0): a linear map of the clamped input, out = (clamp (x, lo, hi) − lo) · (b − a) / (hi − lo) + a;
  with (lo, hi, a, b) = (0, 1, 1, 0) it is 1 − clamp (x, 0, 1) (a reduction share → a gain).
- `VolumeFilter` (0x180133b30): the input × a gain taken from a bus.
- `CustomTransformFilter` (0x18011b860): mode 1 = x · |x| (signed square), mode 2 = sign (x) · √|x|.
- `EQFilter`: parameters frequency (log2 Hz, clamped ≤ 20), gain (dB, ±30), Q; a shape enum (0x490) whose family table
  (0x18020c870) groups 0 … 2 and 12 as "low side", 3 … 6 as their mirrored "high side", 7 … 9 band, 10, 11 others; shapes
  1, 2, 4, 5, 6, 7, 12 use the gain. The exact pole / zero prototype (0x180103140) was not decoded this round.
- `RandomModulationProducer`: in every style here its output reaches the graph through a mixer gain set from one boolean
  style property (+0xa8 / +0xb0 / +0xf0, default false); not built (LYNX runs the default).

### Smooth [STATIC] (outer 0x18015a8f0, update 0x18015b510; main algorithm 0x180158270, update 0x18015a110; feedback
### algorithm 0x180155e00, update 0x180157e60)
Knob → property laws (outer update): attack = 0.25 · a³ s; release = 0.02 + 0.96417 · tan (1.2 r²) s (= 0.02 + 2.48 s ·
tan (1.2 r²) / tan (1.2)); auto-release constant k = K (3.6 r² + 0.1) · 0.846823, K a piecewise-linear curve equal to its
input up to 1.6 and rising faster above (5.1 at 3.7). Ratio, knee, range and the auto-release switch pass through.

Graph (x = the side chain):
- Feedback part: gain computer E (threshold + 0.2149 dB, ratio R_E = the knob's ratio up to 10 : 1, then easing to 16 : 1
  at 40 : 1 and above (a piecewise-linear curve), the knob's knee and range) on x (after an EQFilter of shape 6 at
  2^9.78 ≈ 880 Hz, gain −0.116 dB, Q 0.25: a 0.1 dB tilt, negligible); followers F0 (attack × 0.99909, release × 0.43484 ·
  1.00869, falling step term p5 0.98669, auto constant × 1.0905) and F1 (attack × 8.99406, release × 2.25011 · 1.00229,
  p6 0.0042, auto × 1.2350); G = max (0.760859 F0, 0.768633 F1) (mixer, maximum) → TransformFilter → 1 − G →
  VolumeFilter: the side chain x · (1 − G) is the main gain computer's input. Outputs: that signal and F0.
- Main part: gain computer D (threshold × P (ratio) — a product mixer with a parameter that is a piecewise-linear curve of
  ratio / 10, 1.0 at 1 : 1, about 1.15 at 10 : 1, 1.187 at 20 : 1, 1.13 from 50 : 1; ratio, knee, range from the knobs) →
  followers C0 (the knob's attack, release × 0.43484, auto constant × 0.85185) and C1 (attack × 9, release × 2.25011,
  rising step term p1 0.904, p6 0.0042, auto constant × 1);
  B = 0.610426 · C0 · F0 (product mixer); A = 1.008093 − 0.012346 · B (sum mixer);
  reduction share = max (A · C0, C1) (maximum mixer, A as C0's gain) → DBConverter mode 2 (−20 log10 |1 − x|).
- The outer adds a random drift to the threshold, gated by a style property that is off by default.
LYNX builds all of it except the 0.1 dB EQ and the random drift. Read as the reduction share (like Versatile, round 190lg): the "feedback" is a feed-forward stand-in — the main detector
measures a signal already turned down by its own copy of the gain computer, so the effective ratio is softer.

### Op-El [STATIC] (OpElCompressor 0x180153e30, update 0x1801559b0; wiring case 11 of 0x18016b990)
Knob laws: attack attS = 0.25 a³ s (≤ 0.25); release relS = min (2.5, 0.97195 · tan (1.2 r²)) s (= 2.5 · tan (1.2 r²) /
tan (1.2)). Graph:
- D1: gain computer with the knobs' threshold, ratio, knee, range, on the side chain after an EQFilter (shape 5, frequency
  2^14.70 ≈ 26.7 kHz, gain −10 … −11 dB, Q 0.245: a wide high-side shelf) → signed square → follower A (attack 0.998 attS
  + 0.5 ms, release 0.596 relS + 10 ms) and follower B (9.992 attS + 2 ms, 1.788 relS + 30 ms);
- D2: gain computer with the knob's threshold and ratio but a fixed knee 30 dB and range 60 dB, on the side chain →
  signed square → follower C (149.94 attS + 15 ms, 4.768 relS + 80 ms);
- sum 0.8 A + 0.15 B + 0.05 C → signed square root → DBConverter mode 2 → VolumeFilter × a style property (1.0).
  No auto release (the followers' switch is never set). Threshold random drift: off by default.
  LYNX builds all of it, the shelf as its own second-order high shelf (−11 dB, Q 0.245) on the signed side chain per
  channel, linked like the level (see [EXEC] below for its placement).

### Upward [STATIC] (UpwardCompressor 0x180161af0, update 0x1801632d0; wiring case 12)
Knob laws: attack 0.25 a³ s; release 0.2 + 2.3 · F (r) s, F a piecewise-linear curve of the release knob position r
(linear 0 → 0.5 over r 0 … 0.8, then to 1.0 at r = 1); auto-release constant 9 r² + 1. Graph: side chain → rectifier →
follower (attack 0, release 5 ms) → DBConverter mode 0 (20 log10) → detector E1 (dB input, the expansion "purpose":
threshold, ratio' = 2 − 1 / ratio, the knobs' knee and range) and detector E2 (same settings) on a constant −180 dB;
mixer: −E1 + E2 → follower (the knob times) → the style's output. E2 is the lift at silence; the output smooths
"E (silence) − E (level)" so that the lift comes back at the release time and goes away at the attack time.

### [EXEC] what the bank settled (refcap030 files of each style, time-window scorer; lab: env-overridable law)
(quick set = 8 files of the style: styles stairs / tc at three thresholds, grid knee 0 / 72, prog drums / mix / vocal.)
- Smooth: read as the reduction share (like Versatile) the static stairs reach ESR −35 … −54 dB. The threshold factor of
  the ratio is load-bearing: without it every file drops (quick set 7.62 → 4.50); LYNX's first smooth fit of it was 0.011
  high near 4 : 1 (−0.2 dB on the stairs), a closer fit (max error 0.013 over 1 … 50 : 1, 0.003 near 4 : 1) took the stairs
  to ESR −39 … −54 dB. The followers' step terms (p1 / p5 / p6) change nothing measurable (kept, as read). E's +0.21 dB
  threshold offset: kept as read (dropping it moved nothing above 0.05 dB).
- Op-El: without D1's shelf the 8 kHz part of the threshold −48 dB stairs read up to 6 dB too little reduction: on an
  8 kHz tone sampled at 48 kHz every third sample is near zero, which pulls the reduction share's followers down near
  full scale; the reference's shelf (a phase shift as well as a cut) takes those dips off. LYNX's own second-order shelf
  (−11 dB, Q 0.245) placed at 20 kHz at 48 kHz (= 0.4167 × the rate; 26.7 kHz cap) matches it (that file 5 → 8, the
  quick set 7.25 → 8.25; at 26.7 kHz → Nyquist cap it was 7.62, at 14 kHz 7.75).
- Upward (built in the lab, not kept): "lift = E (silence) − follower (E (silence) − E (level))" read 5 dB too loud on
  every file but the threshold −48 dB stairs (quick set 6.50 → 2.38). The reference's static curve (stairs at threshold
  −48 / −30 / −12 dB, ratio 4 : 1, knee 18, range 60) has a level-independent cut on loud signals of 0 / 2.6 / 10.5 dB
  and a 0.75 slope (1 − 1 / ratio) below a corner 0.4 / 7.7 / 8.9 dB under the threshold: neither the expansion detector's
  "upward" mode nor that cut was decoded this round (the detector's purpose-1 branch needs a full read). Upward keeps its
  fitted law (rounds tp172b / trial187b), which already holds that cut (−11.1 against −10.5 dB).

### Vari-Mu [STATIC, not built] (VariableMuCompressor 0x180163780, update 0x1801647f0; node VariMuCompressor 0x1801170f0 /
### process 0x1801172a0)
Knob laws: an attack-like time 0.249 (a² + a³ − a⁴) + 0.001 s (0.001 … 0.25), release 0.005 + 0.97 · tan (1.2 r²) s,
auto-release constant 3.6 r² + 0.1, a "ratio" property from a piecewise-linear curve of ratio / 10 (1 at 1 : 1, 40 at
10 : 1, 80 at 100 : 1), the knee and range. The node is a feedback tube model, not a graph of generic elements: a follower
(attack / release) on the output level above the threshold, a bias from it scaled by (r' − 1), r' = (1 − 0.5 √(knee / 60)) ·
ratio property, and the reduction read as the slope of a fixed polynomial tube curve at that bias (a central difference),
with the output fed back to the detector; the side chain enters the tube at −6 dB, and each side of the central difference
also passes a second shaper object (a transfer of type 4, created by 0x180109cf0) driven by the bias. Rebuilding it needs the tube curve's constants and the node's own state handling
read in full: not done this round (time); it keeps round 190v's measured law.

### TTM [STATIC, not built]
A multiband object (`TTMCompressor`: crossover, per-band processors with their own detector, followers, a hold and a
look-ahead smoother). LYNX already runs a measured three-band law (rounds 190v / 190al). Not rebuilt this round (time).

## 2. Round 190lp, sub-round lg: Pro-L 2 → Apex Limiter and Pro-G → Sentry Gate, black-box only (owner decision 2)

No static analysis of Pro-L 2 or Pro-G in this sub-round (no Ghidra, no REA session, no preset read): every law below
was measured on the reference renders already in the banks (refcap030 Pro-L 2 359 files / Pro-G 204 files, refcap182
programme, tp172b) and LYNX's own smooth laws were fitted to those measurements. Tools (folder
/mnt/e/lynx-work/rea3_190lp/lg/lab): `gtraj.py` / `trj.py` (gain trajectories, output − input energy per 1 … 100 ms
frame after cross-correlation alignment), `genv.py` / `stepdn.py` / `hc.py` / `inst.py` (sample-resolution gain from the
analytic envelope, per half cycle, and per sample on the 1 kHz / 100 Hz stairs and on drums), `harm.py` / `xs.py`
(fundamental and harmonics per stair step), `lab.py` / `opt.py` (the bank files of one style rendered through a lab
fxrender whose revision-1 table is overridden by LYNX_LGLAB, scored with the time-window scorer, coordinate search with
"no file down" as a hard penalty).

### Pro-L 2 [EXEC]
1. **Output level is a gain after a 0 dB limiter.** S3 (Aggressive, gain 8 dB, output level −1 dB): the −40 dBFS tc
   carrier comes out at +7.00 dB (8 − 1), the bursts' peaks at −1 dB. LYNX (ceiling −1 dB) left the quiet part at +8.
2. **Ripple / harmonics on steady tones by style** (stairs, gain 6 dB, 6 dB over the ceiling): Transparent, Punchy,
   Dynamic and Allround ride the waveform as LYNX does (H3 −30 … −50 dB at 1 kHz); Aggressive, Bus and Safe hold one
   gain through the cycle (H3 under −150 dB, at 1 kHz and at 100 Hz); Modern nearly (−87 / −57 dB). LYNX's Loud / Bus /
   Safe / Dense rode it (H3 −32 / −55 / −75 / −37 dB).
3. **Aggressive holds about one half period, not a window.** At a 1 kHz step down the gain changes within the half
   cycle in which the level drops (−6.04 → −4.24 dB, then the slow stage); on drums it follows the waveform inside a
   half cycle; on a 100 Hz tone it stays at −6.04 dB through the trough. A fixed 5 ms detection window reproduces the
   tones but takes drums 1.5 … 3 dB too deep (prog drums 8 → 5); a hold of ~0.75 × the mean time between the detected
   signal's zero crossings reproduces both (stairs THD −32 → −150 dB, drums error 0.06 → 0.09 dB).
4. **Programme-dependent release (Transparent).** At 15 / 24 dB of gain on the mix the reference recovers faster the
   deeper it limits: LYNX sat 0.5 … 2 dB lower in the troughs (pairs mix, 100 ms frames: mean error −0.89 dB). A slow
   stage whose speed grows with the reduction it holds (× (1 + 0.15 per dB)) brings it to −0.25 dB (Allround 0.05 per dB;
   Dynamic's fit took a programme file down and was not kept).
5. Reference latency per style (refcap030 manifest, 48 kHz): Transparent / Punchy 731 samples, Dynamic + 4x 866, Safe
   (true peak off) 2640, Modern 3115, Aggressive 3131 — the smooth styles look ~50 ms further ahead (their gain starts
   to fall 15 … 30 ms before a loud tone). LYNX's latency is not changed (329 samples, constant).
6. Not modelled: Aggressive takes ~1.2 dB more than the ceiling needs on a tone held 15 … 18 dB over (not at 9 or 24 dB);
   the smooth styles' long look-ahead (it would need their latency).

### Pro-G [EXEC]
1. **The closing runs in dB.** After the tc burst (−6 dBFS → −40 dBFS carrier): Classic (release 100 ms, hold 20 ms,
   range 40 dB) waits ~25 ms and falls along an S-curve that is straight in dB in its middle; at release 27 ms it waits
   ~20 ms then falls 0.45 dB/ms; at the longest release it waits ~165 ms (wait ≈ 20 ms + 0.08 × release). Guitar waits
   ~2.4 × release (+ ~20 ms) then falls ~30 dB per release time (0.6 dB/ms at 50 ms, 0.35 at 100 ms) whatever the
   range. Clean falls ~0.085 dB/ms toward its expansion target; Vocal accelerates slowly (an S-curve over ~0.4 s).
   LYNX's revision 0 closed exponentially in the linear domain (up to 10 dB off mid-way).
2. **The opening**: Classic / Clean open as a linear-domain one-pole (≤ 2 ms at attack 1.6 ms); Guitar (attack 5 ms)
   rises −100 → −12 dB in 4 ms like two cascaded poles in dB; Vocal approaches 0 dB exponentially in dB (τ ≈ 6 ms).
3. Upward's lift already matches on the tones (revision 0, scores 10); on the mix LYNX lifts 1 … 2 dB more in the
   quieter passages (S5 mix): a mean-square detector fixed the mix but broke the tones — left as it is.

(numbers of the fit and the gate: docs/PARITY.md "Round 190lp (Pro-L 2 → Apex, Pro-G → Sentry)")

## 3. Pro-R 2 → LuxeVerb Space revision 1: the reference's fixed early taps (round 190lp, part r2) → NOT merged (owner decision)

Branch `rea3_190lp-r2` (off `rea2_190lg` 2e04775e5 + merge of `rea2_190lg-r2`). Lab: `/mnt/e/lynx-work/rea3_190lp/r2/`
(taps/: peek.py, cluster.py, onsets.py, amp2.py, calib.py, fitall.py, early.py; lab/: svlab.cpp = core/SpaceVerb.h alone,
comp4.py + fit5.py component refit, tone.py). Black-box only (renders of the refcap030 impulse captures); no static
analysis this round. LYNX's code is written from the measurements and laws below.

### The early field of the reference [EXEC]

- Until the late network's first line returns (pre-delay + 5.18 ms × 1.12402^(k + n0), e.g. ≈ 38 ms at Space ¼, ≈ 22 ms at
  Space 0, ≈ 90 ms at Space 1) the reference's output is **only discrete single-sample arrivals with exact silence
  between them** (1-ms bins at −200 dB). Each arrival is one sample through the output tone filters (its shape follows
  Brightness: a sharp click at 100 %, a slow low pass at 0 %).
- The arrivals sit at **fixed times**: the same onset sample at every Space, Decay Rate, Character ≤ ½ and Distance ≤ ½,
  and the same ms at 44.1 / 48 / 96 kHz. Clustering every Modern impulse capture (Distance ≤ ½, Character ≤ ½, 81 files
  at 48 kHz, an arrival = a sample 12 dB over the local diffuse level, onset = first sample of its shape) gives 42 left and
  36 right arrivals within 105 ms present in ≥ 40 % of the files, each with a fixed polarity (left 6.667 +, 8.958 +,
  11.313 −, 14.354 −, 14.604 −, 18.125 +, 18.333 −, …; right 8.292 +, 10.958 −, 12.479 +, 15.896 +, 16.146 +, …), plus the
  left 25.417 ms one at Space 0 only. 190lg's 4 + 4 are the loudest of them.
- They are not comb echoes of a few lines (no t → 2t − P pattern, no repeated spacing) and they do not move with k: a
  fixed sparse tap set.
- **Levels** (the energy of each arrival over 0.27 ms, + the LYNX output filters' unit-tap energy 11.92 dB at 48 kHz /
  Brightness ½, = the tap's gain; peaks first, but near Distances the reference's arrivals are sharper, so a peak match
  gave LYNX +3 dB of early energy): a per-tap level, then −2.8 dB per unit Space, −0.54 dB per octave of Decay Rate,
  −5.9 dB per unit Distance, −3.2 dB × (t / T) (T the decay time: late taps of short spaces fade); fifteen taps stand out
  by 8 … 25 dB at some Spaces and **switch with the integer k = ⌊14 sin (π/2 S^0.8)⌋ of the line-length law** (Space ⅕ /
  k 5 and ¼ / k 6 differ, ¾ and ⅘ / k 13 are identical, 1 / k 14 differs again): k 0: left 6.67, 11.31, 18.33, 25.42,
  right 8.29, 12.48, 21.13, 32.40; k 5: left 11.31, 28.44, right 12.48; k 6: left 11.31, 49.08, 50.96, right 21.13; k 10 …
  13: left 18.33, right 32.40; k 14: left 18.33, right 55.33 (32.40 drops 20 dB). The early standing-out ones fall
  ~17 dB per unit Distance more than the rest (the late ones at 49 … 98 ms hardly move), and they rise 2.2 dB from
  Character ¼ to ½ from Distance ¼ on (at Distance 0 they do not move). Fit: robust sd 0.7 dB over 4 413 readings.
- **Character ≥ ¾**: the arrivals move between captures (their times differ per file: modulated); in 20 … 40 ms the
  reference is 4 … 8 dB quieter than at Character ½, the first ones stay. LYNX keeps the taps static and drops the lift of
  the standing-out ones after 20 ms from Character 0.55 to 0.75.
- **Distance ≥ ¾**: none of the taps; the early field starts at ~1 ms instead and is dense (no arrival set consistent
  across captures): LYNX fades the taps out from Distance ½ to ¾ and its early / 2-line networks in.

### LYNX (revision 1, hidden; revision 0 untouched)

`detail::tapsL / tapsR` (times, polarities, per-tap levels as measured) read the channel's input before the network's
pre-delay (the user's pre-delay moves them); the level law above in `Engine::setTaps` (smooth terms in Space, rate,
Distance, t / T; Gaussian bumps over k for the standing-out taps, their own Distance slope; a smoothstep fade from
Distance ½ to ¾; Character smoothsteps). They replace tp172b's twelve taps × the old spread in revision 1. Near (Distance
≤ ½) the early and 2-line networks are silent and not run (the reference has nothing but the taps before its late network);
they fade in as the taps fade out. Late / 2-line / early levels refitted with the taps (lab/comp4.py + fit5.py; real bank
lab/lab2.py: variants with the taps 1.9 dB lower or a −1 dB bell at 800 Hz scored worse, 30 / 32 downs against 26).
With the taps fxparity aligns at lag 0: |lag| ≤ 3 samples on 3 % → 72 % of the 193 impulse files, median xcorr 0.057 →
0.206, median ESR +2.55 → +1.56 dB.

### The 1 kHz tone (stim / nburst) [EXEC]

On the nburst 3 s 1 kHz sine (G4, Space ¼, rate × 1) the reference's wet sits at −16.5 / −13.4 dB (L / R, 100 ms frames,
sd 2.8 / 1.3 dB), LYNX's (190lg law) at −7.9 / −10.7 (sd 2.0 / 1.0): the frame-to-frame movement is alike (both modulate),
so the gap is the level of a single-frequency response. LYNX's impulse responses carry +2 … +3 dB in 0.5 … 2 kHz against
the reference (fixed by the level refit) and the rest is the draw of a static network's modal response (the reference's
own left / right differ by 3 dB). Adding modulation only to move that draw would be tuning to the scorer: not done.
