# REA round 190kt — third-party plugin binaries for parity (owner's permission of 2026-11-17)

Scope: the plugins round 190hv could not capture (trials expired or audio muted): SSL, Harrison, LiquidSonics Lustrous
Plates, D16 Repeater, Slate, Eiosis, Overloud, Sonic Academy ANA 2, Antares, Waves (demo mode), Valhalla FutureVerb
(inventory: `/mnt/e/lynx-work/refcap030/INVENTORY.md`, `NOTES.md`). Tools: the rea MCP (Ghidra 12.1.4 headless,
`decompile_all` into `/mnt/e/lynx-work/rea190kt/ev/`), a read-only PE section survey (`lab/pesurvey.py`), and the
plugins' own resource files. Clean-room: what is recorded here is structure, behaviour and parameter meaning; LYNX's
code is written from this description, and every constant LYNX uses is fitted on our own renders. No code, table,
preset or audio content of a vendor is copied into LYNX. No licence check was touched, bypassed or patched: protected
code stayed protected, and the Waves demo's periodic mutes are masked out of our measurements, not removed.

## 1. What static analysis can read at all [EXEC: `ev/pesurvey1.txt`, `ev/pesurvey2.txt`]

| vendor (sample binary) | protection seen | code readable? | consequence |
|---|---|---|---|
| SSL (Native Bus Compressor 2, 4K E, …) | PACE Fusion wrapper: `.text` and `.data` at entropy 8.000, `.guard` / `.proxy` sections, only 4 system imports | no | black-box only (existing banks); `.rdata` strings readable |
| Harrison (32Classic Channel Strip, …) | same PACE Fusion layout | no | black-box only |
| LiquidSonics Lustrous Plates | same (`.text` 8.000, `dummy1/2`, `.guard`) | no | black-box only |
| Antares Auto-Tune (all) | same | no | black-box only; no reference bank exists (trial expired before 11-04) |
| Sonic Academy ANA 2, D16 Repeater, Overloud TH-U | same | no | black-box only |
| Slate Digital (FG-X 2, VM FG-2A, …), Eiosis (AirEQ, E2Deesser) | PACE licensing SDK linked (strings), but `.text` at 6.5 … 6.6: not encrypted | yes, but large (31 … 44 MB of code with JUCE and the licence SDK, no symbols) | possible; not done this round (cost per plug-in far above the Waves DLLs below) |
| Valhalla FutureVerb | none | yes (3 MB) | possible; the demo still renders (mutes ~40 s after load) |
| Waves (V17 bundles) | WaveShell + WavesLib licence; the per-plugin DSP DLLs are not wrapped (`.text` 6.2, imports `WavesLib1_17.1.42.dll`) | yes, small (SSLComp: 764 KB control DLL + 94 KB `GenericMultiCoreProcessor.dll` exporting `SSLCompProc` / `SSLCompMProc`); the bundle's `Resources/ParamXML` lists every parameter's range and law | done for SSLComp (§2); the same path is open for CLA-76 / CLA-2A / L2 / RVerb / H-Delay / PuigTec (each bundle has the same two DLLs) |

Decrypting a PACE-wrapped image needs the licence to run (all expired) or defeating the wrapper; neither is done. For
those vendors this round adds nothing beyond their existing black-box banks.

## 2. Waves SSLComp — the processing graph [DECOMP]

Sources: `GenericMultiCoreProcessor.dll` (`SSLCompProc.A`, `ev/sslcomp_gmcp/functions/0x18000103c.c`) and the control
DLL's coefficient setters (`ev/sslcomp_dll_all/functions/0x180025ed0.c`, `…25700.c`, `…25a00.c`, `…26a00.c`,
`…26370.c`, `…277a0.c`, `…28340.c`, `…270b0.c`, `…27dc0.c`, `…252c0.c`), and the bundle's ParamXML.

Per sample, stereo linked:
1. **Side chain**: a 2nd-order high-pass (bilinear Butterworth, off at ≤ 20 Hz: the "SC Filter" control) on each
   channel's input.
2. **Analog extras** (only with ANLG on, off by default): coloured noise (an LCG through a shelving pair) added to the
   detector and the output, a small second-order term on the output (x + c·x²), and a mains-frequency ripple
   (50 / 60 Hz) amplitude-modulating the output. All below −78 dB.
3. **Detector**: the larger of both channels' short peak estimate (the sample, the previous sample, and a
   one-step interpolation between them), clipped at full scale.
4. **Feedback**: the detector level is multiplied by the gain applied now (`exp(state)`), so the detector hears the
   compressed output — the classic feedback bus-compressor topology.
5. **Gain computer in the log domain**: `o = ln(level + floor) − T` (o ≥ 0), `y = slope · o` (slope < 0, from the
   ratio), `target = y² / (y − k)` — a rational soft knee that starts quadratically and approaches `y + k`. The
   threshold, slope and knee depend on the ratio step and (for 2:1 strongly) on the threshold knob: the knob's
   scale is not linear in dB.
6. **Ballistics as an RC network on the log gain** (state ≤ 0): attack: `state += a·(target − state) − r·state`
   (the timing capacitor charges through the attack resistor while the release resistor discharges it — so a slow
   attack also settles a little short of the target); release: `state = min(state·(1 − r), target)` (recovery
   towards 0 dB, never past the target). The six attack and four release positions are resistor values with one
   capacitor: `a = 1 / (fs·R_a·C)`, `r = 1 / (fs·R_r·C)`.
7. **Auto release**: two capacitors in series (the gain is the sum of their voltages), both charged by the same
   attack current, each with its own discharge path — a fast one and a slow one (seconds): short peaks recover fast,
   long reduction slowly.
8. **Output**: input × gain × make-up; Mix blends the dry input (`out = trim·(dry·(1 − mix) + wet·mix)`); a fade
   in / out stage (the FADE control, 1 … 60 s) multiplies the gain; the IN switch puts the slope to that of 1:1.
9. The DSP reports a maximum sample rate of 48 kHz in its process description (`ProcessXML`): at 96 kHz the host
   side resamples.

State format: the Waves preset/setup chunk carries the RealWorld value list LYNX already reads (round 190jh): 0
threshold (−15 … +15), 1 ratio (0 / 1 / 2 = 2 / 4 / 10 : 1), 2 fade mode, 3 attack (0 … 5 = 0.1 … 30 ms), 4 release
(0 … 4 = 0.1 / 0.3 / 0.6 / 1.2 s / Auto), 5 make-up dB, 7 fade time, 8 IN, 9 ANLG, 19 mix %, 20 trim dB, 21 SC
high-pass Hz (20 = off). The internal list has two entries more than the ParamXML (the VU / meter entries), which
explains the shift found by round 190jh at index 19.

## 3. LYNX's model and its fits [EXEC: `lab/gbus.py`, `lab/fitstatic2.py`, `lab/fitdyn.py`, `lab/fitauto.py`]

Probes rendered in Waves demo mode (`cap/sslcomp.txt`: refcap's fxmeasure host, hidden, no editor, no input, the trial
lock only; 44 renders in 76 s; mutes masked): 1 kHz stairs −50 … 0 dBFS at 3 ratios × thresholds −15 / −5 / +5 / +15,
pink-noise stairs, −40 → −10 → −40 dBFS steps at every attack and release position, drum-like bursts.

LYNX's console law (Crest Compressor, Bus style, hidden `busLaw`) follows §2's structure with its own parameters:
detector floor −60 dBFS (fitted floor values ran into the bound; −60 costs < 0.01 dB), threshold = knob + a cubic in
the knob per ratio step, loop gain and knee linear in the knob, RC time constants per position. Fit quality (rms dB
of the steady gain over the 4 thresholds × 26 steps): 2:1 0.16, 4:1 0.05, 10:1 0.02. Time constants (ms): attack
0.047 / 0.99 / 1.65 / 8.4 / 30.8 / 111; release 85.8 / 127.6 / 264.3 / 560.5 (rms 0.24 … 0.27 dB on the steps);
Auto: fast capacitor 26.7 ms, slow 2.53 s, the slow capacitor charging 0.17 × as fast. The release fits land on
one capacitor × the release resistor ladder (0.47 µF with 180 k / 270 k / 560 k / 1.2 M gives 84.6 / 127 / 263 /
564 ms), which is the published console design; attack positions read faster than the resistor ladder because of the
feedback loop's gain.

Not modelled (all below audibility or off by default): the analog noise / ripple / second-order term, the
inter-sample peak allowance (Crest's detector reads sample peaks), the fade stage.

## 4. SSL Native Bus Compressor 2 on the same law — tried, not shipped [EXEC, black-box]

Its code is PACE-encrypted (§1), so nothing of its DSP was read. Its parameter list (refcap0124's host table): threshold
−20 … +20, seven ratio steps (1.5 / 2 / 3 / 4 / 10 / 20 : 1 / X), seven attack steps (0.1 / 0.3 / 1 / 3 / 10 / 20 / 30 ms),
seven release steps (0.1 / 0.3 / 0.4 / 0.6 / 0.8 / 1.2 s / AUTO), make-up, mix, side-chain high-pass, bypass; state = an
`SSL_PLUGIN_STATE` XML with A / B blocks of `PARAM id / value` (values are the step indices). Its four tp172c stairs
(default, −10 / 4:1, −15 / 10:1 + 5 dB, −8 / 2:1 at 50 % mix) fit the console law with SSLComp's ratio nodes and a linear
threshold scale (0.971 × knob − 25.6 dB at 4:1; 2:1 softer than SSLComp's, the knee at its top and the loop smaller):
0.24 dB rms (`lab/fitbc2.py`; the 10:1 stair's 0 dBFS step is left out: the reference stops reducing there). Steps
between the nodes interpolate in log ratio; the 20 ms attack and the 0.4 / 0.8 s releases log-interpolate SSLComp's
time constants.

Not shipped: with SSLComp's time constants its dynamics are off — tp172c 6.06 → 5.06 (the 10:1 / 0.1 ms / 0.1 s setting's
bursts, drums and music 5 → 2 … 3, the default's drums 8 → 5), refcap182 programme 6.20 → 5.30. The static curve fits;
its attack / release positions do not behave as SSLComp's RC values (no level-step probes of it exist: its trial
expired before any were made). It keeps round 190ar's fitted map. With step probes (a new licence) the same law would
need only its own time constants.

## 5. Waves CLA-76 and CLA-2A — structure only, for the next round [DECOMP]

`CLA-76.bundle/Contents/Win64/GenericMultiCoreProcessor.dll` exports `CLA76Proc` (`ev/cla76_gmcp/functions/0x180001118.c`):
16-bit fixed-point I/O buffers, the same LCG noise through a two-pole colouring filter, a detector feeding a
**tabulated transfer per ratio mode** (one table block per mode, linear interpolation over ~765 points indexed by the
detector state, a pass-through above −0.01), followed by the gain stage; an oversampled path (`FUN_180004b40`) for
another bus layout. The tables are the vendor's data and are not used: the structure says LYNX's FET law should be a
per-mode static curve on a smoothed detector, to be measured black-box (refcap182 CLA-76 renders; the demo for probes).
`CLA-2A` exports `LA2AProc` (not decompiled this round).
