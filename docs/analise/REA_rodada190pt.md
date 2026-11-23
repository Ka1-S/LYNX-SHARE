# REA round 190pt — Patcher Control Surface / X-Y-Z controller links (Stroll Up), Vocodex controls under automation

FL Studio 2026 (`C:\Program Files\Image-Line\FL Studio 2026`). Clean-room: behaviour only, no code or data copied.
Evidence kinds: **[EXEC]** FL's own output — `FL64.exe /R /Ewav` renders (minimized, no injected input, under the GUI
and FL locks) of copies of the demos changed in one place (mixer slots switched off, a curve point or a Control Surface
value edited in a Patcher node's state, pattern events flattened), or the installation's DLLs driven by our harnesses
(round 35's `vcx.c` for `Vocodex_x64.dll` with `p=n,idx,val` ProcessParam calls at sample n; round 190pr's `hm.exe`
`pf=` for the parameter ranges); **[DECOMP]** Ghidra (REA) decompilation of `Vocodex_x64.dll` (SHA-256 `43d10692…4bb0`)
and round 165's decompiles of `Fruity X-Y-Z Controller_x64.dll`; **[DATA]** the .flp files. Work folder
`/mnt/e/lynx-work/patcherxyz_190pt/` (`flp/` variant projects, `fl/` FL renders, `lx/` LYNX renders, `rea/` dumps,
harnesses and gate data, `scripts/` our tools: `patdump.py`, `patedit.py`, `xyzdump.py`, `flpev.py`, `ranges.sh`,
`lx.sh`, `lxv.sh`, `seg.py`, `cmplag.py`, `specerr.py`, `survey.sh`; `tools/` C++ probes `cs`, `songinfo`, `pt`,
`vxtest`).

Round 190pr left: Stroll Up's two master Patchers (+7 dB lows / −4 dB highs in LYNX, "Control Surface / X-Y-Z
controllers not mapped"), and the Vocodex demo silent 9.5–13.5 s and 24–26 s with its pattern events on Vocodex
parameters 11 / 17 "target not reproduced".

## 1. Stroll Up's master Patchers

Patcher 1 (slot 2, 78 nodes): a Control Surface "Controls" (102 outputs) → four-band split (PEQ 2 pairs whose crossover
frequencies come from an X-Y-Z "bandsplitter"), per band Flangus / Flanger / Chorus / Stereo Enhancer / Limiter with
their parameters on the Surface or on X-Y-Z "mappers", PanOMatic gains, an X-Y-Z "bypmap" wet / dry and "ioMapper".
Patcher 2 (slot 6, 29 nodes): the same split into four Soft Clippers driven by four X-Y-Z "clipMappers". The X-Y-Z
Controllers are used as function mappers: a Surface knob into a level, its *input curve* shaping it, the outputs into
effect parameters.

| # | Fact | Evidence |
|---|------|----------|
| P1 | A Patcher control cable from a Control Surface output (port 0x10000000 + k) into an X-Y-Z Controller's parameter 0 / 1 / 2 sets the X / Y / Z level (0…65536; the host sends the value 0…1 over the range). The DLL's ranges: levels 0…65536, per-axis speed 0…4096, acceleration 0…3072, smoothing time 0…4096. | [DATA] `rea/p1.txt`, `p2.txt` (`patdump.py`); [EXEC] `rea/hm/Fruity X-Y-Z Controller.txt` |
| P2 | X-Y-Z: ProcessParam 0…2 stores `target = inCurve (level)` (FUN_00787da0 on the axis' first curve); per tick the target is smoothed and `output = outCurve (smoothed)` is sent to the host one tick later, only when it changed. | [DECOMP] 0x83C0A0, 0x83CBF0 (`/mnt/d/lynx-work/dash165/x1.c`) |
| P3 | The X-Y-Z state's curves are variable length: per axis 8 bytes, then input and output curve, each `i32 3, i32 3, i32 n`, n × 24-byte points (f64 x, f64 y, f32 tension, u8 mode, 2 bytes, i8 sign) and a 20-byte tail. Stroll Up's mappers have 3-point input curves (608-byte states): the jump at x = 0.5 of "mapper1" Y. LYNX read fixed places (2-point curves): wrong Min / Max for the curves after a 3-point one. | [DATA] every X-Y-Z state of the demos ends exactly there (`xyzdump.py`) |
| P4 | The curve walk (FUN_007858b0): before the first point its value; a segment ends when its position reaches 1 and a segment of no length is passed at once (the jump gives the next point's value at x = 0.5 exactly); after the last point its value; the segment's shape is its end point's mode / tension (core/FlCurve.h). Check: "clipMapper4" Y, (0, 0.8) → (1, 0.9375) tension −0.057 at the Surface's 0.674561 gives Soft Clipper post 141 / 160, FL's saved value. | [DECOMP] x3.c; [DATA] `rea/p2/9_Fruity_soft_clipper.bin` |
| P5 | The Surface values and the targets' saved states agree in FL's file (every link checked: PEQ crossovers 6307 / 25719 / 44347, Soft Clipper thresholds / posts, PanOMatic volumes 286 / 279 / 255 / 285, Stereo Shaper 18432 / 12800). FL does not send a Control Surface's values on load: a copy with Surface values edited in the state renders bit for bit as the original (`su21_only6_noclip`) or within render noise (`_vol`); an X-Y-Z input curve edited in the state is applied at load (`su21_only6_cvB`: −2.1 dB at 2.5–10 kHz from the moved crossover). | [EXEC] `fl/su21_only6*.wav` |
| P6 | The importer dropped every cable into an X-Y-Z level ("control of Fruity X-Y-Z Controller parameter 0…2 (not mapped)", 34 cables) and every cable into Flanger / Flangus parameters (40): `effectParamFromFl`'s Multiband Compressor line had lost its `return` in a merge, so the Fast Dist / Flangus / Flanger line became its body — Multiband, Fast Dist, Flangus and Flanger links and automation were all dropped. | code history: the merge b79b68703 (2026-10-15, round 55 into main after round 56) |
| P7 | Patcher 1 alone on the master (FL `su21_only2` vs LYNX with the same slots off): before +6.8 / +6.3 / +4.7 / +3.2 dB in the four lowest octaves; after within ±0.2 dB at every band, RMS +0.05 dB (drums group: +0.05 dB). Patcher 2 alone: before −1.7 dB broadband, −4 dB above 2.5 kHz; after a flat +0.6…+1.0 dB at every band (RMS +0.84 dB). With all its control cables removed, Patcher 2 in LYNX gives the same level as with them (the links agree with the saved states, P5): the +0.84 dB is in its audio path, not in the links (open). | [EXEC] `fl/su21_only2.wav`, `su21_only6.wav`, `sub_only*.wav`; `tools/pt` |

**LYNX now:** `xy::flCurves` reads the curves in order (Min / Max / curve mode from the right points); an input curve
that is not straight sets the hidden parameter `xy::flInputCurves` (17) and the effect then takes the input curves from
the slot's FL state: target = inCurve (level), then LYNX's smoother and output curve as before (`xy::evaluate`, P4).
`effectParamFromFl`: X-Y-Z parameters 0…2 → levels; the Multiband line's return is back (Multiband / Fast Dist /
Flangus / Flanger links and automation map). **Old projects:** flInputCurves is 0 in every .lynx saved before (and not
written while 0), so their X-Y-Z play the level as before, bit for bit; the importer changes reach new imports only.

## 2. Vocodex demo

| # | Fact | Evidence |
|---|------|----------|
| V1 | The silence was a measurement artefact: "Modulator #2" is FLS_SpokenWord 001 (9.56 s, Vorbis in WAV) stretched to 33.5 beats (keep pitch), and renderwav without `LYNX_STRETCH=1` plays samples unstretched — the clips ran out at 9.5 s and 23.5 s. With LYNX_STRETCH (the app's behaviour) nothing is silent: RMS −1.21 → +0.25 dB vs FL, 50 ms error 5.39 → 1.74 dB, within 3 dB 35.6 → 69.8 % (before this round's code). Round 190pr's sweep ran without it: demos with keep-pitch audio clips were measured unstretched. | [EXEC] `lx/voc/base*.raw`, `tools/songinfo` |
| V2 | The two automated controls: Release (11, 71 pattern events 3500…4764) and Mod pitch (17, 438 events −696…672). Vocodex's controls never moved in LYNX: the effect reads FL's state only. | [DATA] 0xDF records `0x2080800b`, `0x20808011` |
| V3 | The DLL's ranges of the 20 controls: Wet 0…2000, SG 0…1000, Mod vol 0…16000, Mod HP 0…65536, Car vol 0…16000, Car LP 0…65536, Noise 0…12800, Noise HP 0…65536, Hold / Attack 0…10000, Attack curve 0…2000, Release 0…10000, Release curve 0…2000, Freeze 0…1, Band width 5…1000, Flatness 0…100, Mod band width 0…4000, Mod pitch −1200…1200, Unison pitch 0…1000, Unison pan −256…256 (LYNX's editor table had 0…256). | [EXEC] `rea/hm/Vocodex.txt` |
| V4 | ProcessParam (FUN_00853e80) sets the control and, for every control but 0 / 1 / 2 / 3 / 4 / 5 / 6 / 7 / 13, runs the whole band set-up FUN_00851dd0 at once, in place: the band filters get new coefficients only (FUN_00626690 → FUN_006260c0 writes the coefficients, never the state), the smoother stages keep their values (FUN_00606730 → FUN_006064e0 coefficients only), a peak / RMS follower is cleared only when its window changes (FUN_00787de0 / FUN_00788190), whatever the band's oversampling or octave level becomes. | [DECOMP] |
| V5 | LYNX's engine against the DLL, the demo's state (SG 0), a pulse-train modulator and a saw carrier, 512-frame blocks, 30 control events (Release, Mod pitch, a pitch ramp every 2048 samples): before (LYNX ignoring them) residual −1.2 dB; after −177.7 dB (static −177.2 dB). Every other control moved twice the same way: −146.7 … −180.0 dB. Menagerie's instance (order 4, unison 1): −174.0 dB; the Draft states of NewStuff and RawFL (8 events of 7 controls): −141.0 / −141.8 dB, their static floor. With the first model (round 177's takeOver: band filters taken over only at the same oversampling, a band at another octave level started afresh) the Mod pitch steps left −45 dB for 50 ms and the Draft states −26 / −29 dB. | [EXEC] `rea/vcx/` (`vcx.exe`, `tools/vxtest`) |
| V6 | FL renders of the demo with the pattern events flattened (`voc_flat`, `voc_nopitch`, `voc_norel`) against the original: the automation's own contribution, FL vs LYNX, Mod pitch −4.0 / −4.2 dB relative, level difference −0.05 dB, correlation 0.70 (the rest is the stretched modulator and Sytrus' block-dependent carriers); Release −34.2 / −33.4 dB. | [EXEC] `fl/voc/*.wav`, `lx/voc/dev4_*.raw` |

**LYNX now:** FL's 20 controls are hidden slot parameters ((raw − min) / (max − min), V3) with a switch
`vocodex::liveControls` (20) the importer sets; a control whose parameter moves (automation, pattern events, song-start
values) is set as FL's ProcessParam and the band set-up runs again on a spare engine prepared with the effect
(`Engine::reconfigure`, no allocation while the band count stays), which takes the running signal over as FL keeps it in
place (`Engine::takeOverInPlace`: every band's state, also across an oversampling or octave-level change, V4),
then the two engines swap. The effect starts from its state and follows a parameter once it moves away from the value
it was built with (an edit in the editor is not undone by parameters that did not move; the importer writes the
song-start values into the state too). SG (1) is not followed (it builds its own Maximus stage). The renderer works out the 20
parameters' automation for such a slot (planSlot). **Old projects:** liveControls is 0 in every .lynx saved before (and
in new Vocodex slots): the state alone, as before, bit for bit.

Vocodex demo (LYNX_STRETCH, lag 974): RMS +0.25 → +0.11 dB vs FL, 50 ms error 1.74 → 1.43 dB, within 3 dB 69.8 →
75.6 %, third-octave spectral error 5.89 → 4.94 dB (20–28 s, where Mod pitch moves most: 7.9 → 3.8 dB).

## 3. Open

- Patcher 2's audio path: +0.84 dB flat against FL with its links right (P7).
- FL sends a Control Surface's values only when they change (P5); LYNX's Patcher sends them every block. The same
  result while the targets agree with the Surface (every FL file seen); a target edited by hand after linking would
  differ.
- The sweep's renderwav runs should use `LYNX_STRETCH=1` for songs with keep-pitch audio clips (V1).
