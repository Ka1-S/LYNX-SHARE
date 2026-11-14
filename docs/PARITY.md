# Parity audit: LYNX's "inspired by" plugins against the originals

Rounds 128–129 (2026-10-21). The user's question: "people tell me details are missing in the plugins — is it
configuration or design?" Round 114 scored only the layout. This audit checks **everything**, item by item:

- **Configuration**: every control, mode, menu option, switch, range / unit / default, per-band option, context
  menu, mouse gesture, side chain, oversampling / quality, latency, A/B, undo / redo, presets, MIDI learn, analyser
  options, meters, window size, help.
- **Design**: what is shown where (display contents, readouts, labels, sections, proportions), drawn in LYNX's own
  style (skin122 / skin126 own the look; this audit owns the *what* and *where*).

Sources: each original's official trial installed on this PC, hosted black-box in our own host (parameter lists read
through the public VST3 API — every control, range and option text — plus screenshots of each view as a user sees
it), the official manuals, and `docs/specs/*.md`. Nothing was decompiled; no graphics, presets or names were copied.
Reference screenshots stay outside git (`D:\lynx-work\parity128\ref`); the side-by-side pages are on the Desktop
(`LYNX-paridade`).

Legend: ✅ same · ⚠️ partial (note says what differs) · ❌ missing · — not applicable / deliberately left out.
Scores are 0–10 (10 = a user of the original finds every control and every piece of information where they expect).

Round 128 covers the FabFilter / Slate / Antares roles (below). Round 129 covers the Waves / SSL / Valhalla roles
(its own section).

## Current scores (config / design / sound / stability) — one table, the latest round per pair

Kept current by the parity rounds (round 165h onwards). "Since" = the round that last changed the pair; "165" = this round's
before → after (blank = not touched in round 165). Lowest first.

**Sound** and **Stability** (round 170c, the user's request after Facet EQ — Config 9 / Design 8 — drew nothing on
macOS): Config and Design score only the controls and the layout, and proved optimistic. **Sound** = the audio measured
against the original (black-box, our own host): null / residual, curves, THD, gain-reduction trajectory — 10 = a deep
null at every setting, 7–8 = within ~1 dB on the main controls (or a deep null only at the measured states), 4–6 =
partial (some modes, static curves only), ≤ 3 = no audio comparison on record (only laws / config). No pair reaches 10:
the −60 dB nulls were measured on her saved states, not across every setting. **Stability / real use** = the editor
draws on Windows, Linux and macOS (CI pictures, `--plugui`, artifact *editor-pictures-<os>*), the analyser / meters
are steady, automation sweeps, sample-rate and buffer changes, no clicks / NaN / crashes, no control that silently does
nothing; 8 = no known problem (not yet proven on macOS for every pair), lower = the problem in the notes below.
"(ref expired)": the Waves and Auto-Tune trials expired (10-25 / 10-31), so the score rests on the earlier
measurements (docs/analise, rounds 123–165) and cannot be raised without a new trial. FabFilter runs until ~11-16 and
Soundtoys until ~11-22: Pro-L 2, Pro-G and Pro-C 3 (no programme comparison yet) should be measured before then. Since
2026-11-04 every reference that still runs (Waves on a new 7-day licence included) is captured to
/mnt/e/lynx-work/refcap0124 (parameter tables, default states, one-parameter sweeps of a test stimulus): the "(ref
expired)" Waves rows can be re-measured from those files; Auto-Tune stays on tune157 / tune165 refs.

Stability notes (round 170c): **Facet EQ** 7 (was 5): blank on macOS until 170c — the macOS CI picture now draws it (artifact editor-pictures-macOS, gfxprobe 0 changed pixels; the run without JUCE patch 12 showed the raw flood: 16 320); analyser reworked 170c (fixed grid, smoothing); one analyser tap per engine (two open EQs take turns); **Apex Limiter** 6: blank on macOS until 170c; ProEditor repaints the whole window at 30 Hz; **Prism Multiband** 6: blank on macOS until 170c; whole-window repaint at 30 Hz; **Hush De-esser** 6: blank on macOS until 170c; empty wave path guarded 170c; **Crest Compressor** 6: blank on macOS until 170c; Drive had no control until 170c; **Quell Denoiser** 6: blank on macOS until 170c; **Echoform** 6: ~22 controls did nothing with Hybrid / a tape voicing on: dimmed since 170c; **Ember Saturator** 6: Tube (the first mode) garbled at +10 dB until 170c; Style / Auto gain silently overridden by a character; Classic voicing has no switch in the editor; **Atrium** 7 (was 6): Early mod rate / depth did nothing until 170c (now modulate the early taps). The lowest Sound scores (Horizon,
Apex / Pro-L 2, Halo, Orbit, Sentry, Nebula, ValhallaPlate) have no audio comparison at all: the next parity rounds start
there.

| LYNX engine | Original | Config | Design | Sound | Stability | Since | Round 165 |
|---|---|---|---|---|---|---|---|
| Ridge Strip | SSL E-Channel (Waves) | 9 | 8 | 7 (ref expired) | 8 | 165 | 8 → 9 / 7 → 8 |
| LYNX Tune | Auto-Tune Pro | 8 | 7 | 6 (ref expired) | 8 | 135 | 165j: sound only (highs kept, measured; scores unchanged) |
| Ridge Strip (Vocal strip) | SSL Native Vocalstrip 2 | 9 | 7 | 8 (ref expired) | 8 | 145 |  |
| Ridge Strip (Modern) | SSL Native Channel Strip 2 | 9 | 8 | 7 | 8 | tp172c | sound 3 → 7 (fxparity 2.6 → 7.1, 30 renders); look: the console's colour-coded caps on grey metal, three columns |
| Ridge Strip (4K) | SSL 4K E | 9 | 8 | 6 | 8 | 148 | tp172c4: the 4K compressor blends its measured ratio nodes in log slope (10:1 acted like ~100:1; S4 6 dB too deep): fxparity 5.4 → 6.0 |
| Ridge Strip (4K) | SSL 4K B | 8 | 8 | 5 | 8 | tp172c | tp172c4: its stepped compressor (out / 2 / 4 / 10 : 1 / ds, stepped releases) read as the 4K's near-limiting curve with the measured knee offsets, its threshold-only auto make-up, its gentler mic pre: fxparity 3.5 → 5.2 |
| Ridge Strip (4K) | SSL 4K G | 8 | 8 | 5 | 8 | tp172c | tp172c4: its ±18 / ±20 / ±22 dB dials scaled onto the 4K's ±15 (same curves), its deeper slow compressor: fxparity 4.4 → 5.1; its Pink EQ type still plays as Black |
| Ridge Strip | Harrison 32Classic Channel Strip | 7 | 6 | 4 | 8 | tp172c | tp172c5: its state translated (was Ridge's defaults): EQ shelves / bells / filters fitted on the trial's impulse grid (a mid out of Ridge's band range moves to a free neighbour), the compressor's static line (the dial - 2 dB RMS, the Limiter ~1.8 dB lower, under -40 dBFS through the input trim), its auto make-up law, gate / expander: fxparity 3.0 → 4.5 (EQ bank 2.8 → 5.4, dynamics 3.8 → 5.5, make-up 3.6 → 6.6); tp172c6: its saturator on Ridge's new DRIVE section (Saturate model, the dial's -24 .. +24 dB shifting the measured clip curve): 32C 4.5 → 5.2 (saturator bank 5.3); tp172c10: its compressor's 27.5 dB reduction cap as a hidden Ridge parameter set by the translation (the original has no control for it): dynamics bank 5.5 → 6.2; tp172c11: its attack dial as a real time constant (hidden Ridge parameter), release time constants from the stair decay (0.25 / 0.35 / 1.6 s at 50 / 104 / 351.5 ms, half in Limiter mode): dynamics bank 6.2 → 6.7, make-up bank 6.7 → 6.6, 32C 5.2 = 5.2 |
| Ridge Strip | Harrison MPC Channel Strip | 7 | 6 | 5 | 8 | tp172c | tp172c5: its 8 bands onto Ridge's 4 (shelves on LF / HF, the bells on the free slots), the 12 dB/oct filters on the 32C's laws, its compressor's measured line and knee: fxparity 4.6 → 5.5 (dynamics bank 3.8 → 6.6, EQ bank 1.9 → 3.2: band-pass / notch shapes, Q under 0.5 and ±19 dB gains are outside Ridge's bands; open for this release: no new Ridge bands or plugin) |
| Kiln Channel (Console E / G / A / N / Y, Tube) | Slate VCC Channel | 7 | 7 | 7 | 8 | tp172c7 | tp172c7: its own plugin (was Ridge's DRIVE section): a compact dark single-channel console like the original (amber VU, console selector with the six models round it, Input / Output, Drive, Group, Noise reduction and Group bypass keys, main In / Out columns with meters), own art and emblem; the same measured drive stages; its noise floor per console (NR off: E / G -113.9, A -110.0, N -109.8, Y -101.6, Tube -101.9 dBFS RMS, uncorrelated L / R) and group bypass shared by the group's channels; state fully translated (console, drive, trims, NR, group, group bypass, main gains): fxparity 6.8 (drive bank 6.8, US A 6.3, group / NR sets 6.7 / 6.9) |
| Fret Channel (guitar / bass channel strip) | SSL Guitarstrip | 8 | 7 | 6 | 8 | tp172c8 | tp172c8: its own plugin (was Ridge with its DRIVE section): input / output, phase correction (align to side chain / left / right, ±20 ms delay with ±1 / ±10 sample nudges, polarity, 1st / 2nd-order all-pass with its phase screen, correlation), the EQ (LF / MF / HF shelves and bell, high- and low-pass, pre / post), the compressor (five measured modes, threshold, mix, auto gain), the guitar amp (pre-amp, drive, Boost / Punch / Edge) and bass amp (drive, Punch / Shape) with mix, output mix, mono and side-chain listen; the panel in the original's layout, own drawing. State translated 1:1. fxparity 5.7 (bank 5.5 → 5.8 → 6.0, voicings at drive 2.8 → 3.2 → 3.9). tp172c9: part of each amp's tone runs ahead of its curve, as the original's drive sweeps show (guitar: a 4 kHz low-pass, so its highs compress less; bass: a +17 dB 1.27 kHz pre-emphasis shelf, so its highs compress more), nodes refitted on the de-embedded harmonics, bass voicings ahead of the amp, Boost / Punch laws measured at drive. Missing: the EQ's Auto listen key (not in the state) |
| Shell Channel (drum channel strip) | SSL Native Drumstrip | 8 | 7 | 6 | 8 | tp172c8 | tp172c8: its own plugin (was Ridge's gate): gate (open / close, range, attack, hold, release) with its meter, transient shaper (gain, speed, amount, invert, listen; two measured followers), low and high frequency enhancers (frequency, drive, amount), listen-mic compressor (comp, narrow band, make-up, mix), output mix, and the 120 process orders (drag the chevrons); latency 4.1 ms like the original. State translated 1:1. fxparity 4.4 → 6.1 (bank 4.3 → 6.0) |
| Strike Shaper (transient designer) | SPL Transient Designer (2-knob; Plus) | 8 | 7 | 3 | 7 | trans189d | trans189d (issue #94): its own plugin, from the public description of the differential-envelope method (no trial on this PC, so no audio comparison yet: sound stays ≤ 3 by the scale above): attack ±15 dB and sustain ±24 dB follow the ratio of two envelopes, so there is no threshold and the gains are the same at any level (drum hits 0 … −60 dBFS: onset / tail within 0.2 dB; Airwindows Point, MIT, measured on the same hits as an open reference of the ratio method: also level independent, but unbounded, +41 dB at its top); output −20 … +6 dB on the printed scale, parallel mix, Guard (soft clip, 0 dBFS ceiling), link, detector high-pass; no latency; neutral = bit-exact. Editor: the 2-knob panel's layout (SIG / OVL, Attack, Sustain, On / Link, Output gain) in brushed silver round black capsules, LYNX's own plate and mark; CPU ≈ 0.3–0.6 % of real time per stereo instance |
| Halo | ValhallaShimmer | 8 | 9 | 4 | 8 | 135 | 183b: sound 3 → 4 (fxparity 3.00 → 3.25, verb 6.52 → 7.17; bank 2.66 → 2.93 / 5.89 → 6.80): the Mono mode's partly correlated tail and quieter onset |
| Horizon | ValhallaFutureVerb | 8 | 9 | 4 | 8 | 135 | 183b: sound 2 → 4 (fxparity 3.00 → 3.25, verb 7.84 → 8.35 on the matched settings; 45-setting bank 2.25 → 2.81 / 6.79 → 7.54; music level error 4.2 → 1.2 dB): echo as pre-delay, EQ / colour / mode laws measured |
| Orbit | ValhallaSpaceModulator | 8 | 9 | 3 | 8 | 135 |  |
| Facet EQ | Pro-Q 4 | 9 | 8 | 8 | 7 | 152c | 170c: Stability 5 → 7 (draws on macOS CI, steady analyser) |
| Crest Compressor | Pro-C 3 | 9 | 8 | 6 | 6 | trial187b | tp172b: sound 4.7 → 5.7 measured (auto gain and release follow the attack, detector floor); look toward the reference; trial187b: all 14 styles nulled on programme (5.3 → 5.4), Upward / Classic refitted; round 190v: Two-way and Vari-mu refitted on a threshold × ratio × knee × attack grid (gain error 4.3 → 1.9 dB and 5.7 → 0.55 dB) |
| Sentry Gate | Pro-G | 9 | 8 | 7 | 8 | trial187b | tp172b: sound 3 → 7 (first programme comparison: 7.2; upward law, hysteresis); trial187b: 12 more settings 7.3 → 7.6 (wet / dry are Expert-only); round 190v: the Vocal style's noise reduction (noise probes 3.7 → 4.9) |
| Ember Saturator | Saturn 2 | 9 | 8 | 5 | 6 | 152b |  |
| Aether | Fresh Air | 9 | 8 | 8 | 9 | tp172c | issue #77: the measured dynamic air model is the default (a new Aether ran the round-114 shelf + exciter: Mid air nearly inaudible, High air ~1.5 dB short of loudness); fxparity on Mid / High air 0–100 % sweeps 5.0 → 8.3; look: frosted plate, white disc knobs |
| Glow Leveler | CLA-2A (Waves) | 10 | 8 | 6 (ref expired) | 8 | 165x | 9 → 9 → 10 / 8 → 8 (165w: the measured release; 165x: the measured attack, Auto Makeup's reference, the VU display saved and translated); 190fx: SC weighting Old \| Measured for the other optical references — FG-2A 3.75 → 4.08, SD-3A 4.64 → 4.84, Custom Opto 4.96 → 5.16 — this pair unchanged |
| Hush De-esser | DeEsser (Waves) | 10 | 8 | 7 (ref expired) | 6 | 165 | 9 → 10 / 8 → 8 |
| Echoform | H-Delay (Waves) | 9 | 8 | 7 (ref expired) | 6 | 137 |  |
| Echoform | ValhallaDelay | 9 | 8 | 5 | 6 | 137 | 183b: sound 4 → 5 (fxparity 3.75 → 4.06, verb 5.12 → 7.39; bank 2.88 → 3.44 / 5.18 → 6.77): tape repeats without −1.9 dB per pass, one tape transport in the Single style |
| Atrium | ValhallaRoom | 9 | 8 | 5 | 7 | 135 | 170c: Stability 6 → 7 (Early mod rate / depth work); 183b: sound 4 → 5 (fxparity 3.17 → 3.50, verb 8.08 → 8.54; bank 3.14 → 3.59 / 8.14 → 8.52): level laws of Early/Late, Decay and Late size |
| Auto-pan (stand-in) | PanMan | 9 | 8 | 6 | 8 | 152 |  |
| Foil Plate | Little Plate | 9 | 8 | 6 | 8 | 152 |  |
| Ember Saturator | Decapitator | 9 | 9 | 5 | 6 | trial187b | 8 → 9 / 7 → 9; trial187b: drives 8 / 10 measured, a hot model per style: 4.8 → 5.0 (high-drive bank 3.1 → 3.8); round 190ar: the coupling ahead of the curve in styles T and P, high-drive bank 3.76 → 3.91 |
| Apex Limiter | Pro-L 2 | 9 | 9 | 7 | 6 | tp172b | tp172b: sound 2 → 7 (first programme comparison: 5.7 → 6.9; two-stage release); trial187b: 16 more settings measured (6.8), no change |
| Epoch Verb | ValhallaVintageVerb | 9 | 9 | 5 | 8 | 135 |  |
| Nebula | Valhalla Supermassive | 9 | 9 | 4 | 8 | 135 | 183b: sound 3 → 4 (fxparity 3.20 → 3.33, verb 6.05 → 7.19 on the matched settings; bank 3.11 → 3.33 / 6.16 → 7.30; stereo part 1.8 → 9.3): the tail is stereo as the reference's, the modulation law of its level |
| Spiral Echo | ValhallaFreqEcho | 9 | 9 | 7 | 8 | 190fy | 190do: measured loop (fxparity 3.38 → 3.88); 190fy: the loop's read and all-pass refitted, revision 1 (fxparity 3.88 → 4.50, 7 up / 9 same / 0 down, tail 6.79 → 6.98; refcap0124 36 captures 3.69 → 3.75) |
| Lattice Mod | ValhallaUberMod | 9 | 9 | 4 | 8 | 144 |  |
| Quell Denoiser | NS1 (Waves) | 9 | 9 | 4 (ref expired) | 6 | 137 |  |
| Hush De-esser | Pro-DS | 10 | 9 | 6 | 6 | tp172b | 8 → 9 → 10 / 8 → 8 → 9 (165w: pans, state levels; 165x: ranges, the 0 … 200 % link knob) |
| Foil Plate | ValhallaPlate | 9 | 10 | 4 | 8 | 135 | 183b: sound 3 → 4 (fxparity 3.13 → 3.73, verb 8.15 → 8.49; bank 3.03 → 3.64 / 8.13 → 8.48): EQ laws, Width keeps the level, the reference's tone |
| Gemini Doubler | Doubler2 / Doubler4 (Waves) | 10 | 8 | 6 (ref expired) | 8 | 165w | 9 → 10 / 8 → 8 (defaults; 165w: shelf range −88 … +12 and law) |
| Compressor (stand-in) | Renaissance Compressor (Waves) | 10 | 8 | 6 (ref expired) | 8 | 152 | 190fx: the FET references' maps and translators refitted on the pink stairs — FG-116 4.80 → 5.80, Modern 4.08 → 4.76, Vintage 5.00 → 5.56, all-buttons 4.28 → 4.48 — this pair unchanged |
| LYNX Tune | Waves Tune Real-Time | 10 | 8 | 6 (ref expired) | 8 | 152b | 165j: sound only (measured; scores unchanged) |
| Echoform | EchoBoy / PrimalTap | 10 | 8 | 6 | 6 | 150 |  |
| Voice Shift | Little AlterBoy | 10 | 8 | 4 | 8 | trial187b | trial187b: dual mono like the original, the input itself at no shift, level laws: 2.8 → 3.1 |
| Prism Multiband | Pro-MB | 10 | 10 | 8 | 6 | tp172b | tp172b: programme 5.9 → 7.6 (default state level fixed) |
| LuxeVerb (Space engine) | Pro-R 2 | 7 | 7 | 8 | 8 | tp172b | new: Space engine page, verb score 7.4 → 8.9 (RT60 / EDC / early / stereo / modulation, measured) |

## Round tp172b: FabFilter roles measured against the trials (black-box, programme material)

Our own host rendered bursts / drums / music / stair / sweep / vocal through each trial at its default and 3–5 saved
settings; LYNX rendered the same with the translated states (`tools/fxrender`, `scripts/fxparity.py`). Mean score
before → after (0–10): Facet / Pro-Q 4 9.3 → 9.3; Crest / Pro-C 3 4.7 → 5.7 (auto gain follows the attack; the release shortens with a slow attack; a 1 ms detector floor in Clean / Allround / Opto);
Apex / Pro-L 2 5.7 → 6.9 (two-stage release, per-style laws); Prism / Pro-MB 5.9 → 7.6; Sentry / Pro-G 6.2 → 7.2;
Hush / Pro-DS 6.0 → 6.4 (Allround follows the measured level law, 0.2 dB per dB); Saturn 2 5.1 (Ember: round 172d). LuxeVerb gained a **Space** engine page for the Pro-R 2
role (16-line FDN with per-band decay; Modern / Vintage / Plate; distance, thickness, character, width, decay EQ),
fitted from measured RT60 per octave, EDC, early energy and modulation; `fxparity verb` 7.4 → 8.9 on the programme
states, 8.9 over 28 probe settings. Saved Pro-R 2 states translate to it (`translate = pro-r2`). The FL engine stays the
default and FL-exact. Looks: Facet, Crest, Apex, Prism, Sentry and Hush moved toward their references' palettes and
flat caps; Fruity Delay 3 stays FL-exact (Timeless 3 compared for design only). Open: Pro-G's burst attenuation (S3),
Crest S1 / S3 music −1 … −1.6 dB and S4 (Bus, auto gain) +1 … +2 dB, Apex's default attack, Sentry's Upward knobs, the Space
engine's early onset at 100 % mix. Data and pictures: /mnt/e/lynx-work/tp172b (report.md).

## Round trial187b: captures before the trials end; Apex, Sentry, Crest, Ember, Voice Shift

Captured on 2026-11-09, before the iLok (11-13), FabFilter (~11-16) and Soundtoys (~11-22) trials end: 526 renders
in /mnt/e/lynx-work/trial187b/cap (report there). They cover Pro-L 2 at 16 more settings (Allround and Bus for the
first time, every style at +10 dB, timing extremes), Pro-G at 22, Pro-C 3's 14 styles at one common setting plus 7
knob sweeps, Decapitator's five styles at drive 8 / 10 / 10 with Punish plus tone / mix / filter variants, Little
AlterBoy at 13 settings on three stereo test signals, and the Slate FG-Stress / FG-MU / FG-Red compressors. All of it
is black-box, through our own host and the public parameter API.

Mean fxparity, before → after (old bank = tp172b / tp172d settings; new bank = this round's):

| LYNX plugin | Original | Old bank | New bank | What changed |
|---|---|---|---|---|
| Apex Limiter | Pro-L 2 | 7.1 → 7.1 | 6.8 → 6.8 | measured only (a Bus / Safe refit and a slower Aggressive fast stage gained nothing overall) |
| Sentry Gate | Pro-G | 7.2 → 7.2 | 7.3 → 7.6 | Pro-G's Wet / Dry levels and pans act in Expert mode only (with it off, wet −6 / dry −12 passes at 0 dB and closes to −100 dB): the translation follows |
| Crest Compressor | Pro-C 3 | 6.2 → 6.2 | 5.3 → 5.4 | Upward (it held −6.2 dB over the threshold where the trial holds −2.8; now about −5.3) and Classic (its release was too fast) refitted on tp172b's bank plus the new programme; both smooth the gain |
| Ember Saturator (Classic) | Decapitator | 4.8 → 5.0 | 3.1 → 3.8 | a hot model per style and per style with Punish, blended in from drive 5 to 10 (at 8 / 10 the original was up to 5 dB quieter than ours: E +5.1, T / P +2.4 dB; now within 0.3 dB); the small-signal drive / Punish laws are unchanged |
| Voice Shift | Little AlterBoy | 2.8 → 3.0 | 2.8 → 3.1 | each channel shifted on its own (the original is dual mono: a voice on the left leaves the right silent), the right 1.25 ms behind once shifted (the original's L / R correlation drops from 1.0 to about 0), the input itself at Pitch 0 / Formant 0 (the original nulls it to −15 dB; ours drifted up to half a period), the level law against the shift (+3.2·tanh(x/4) dB up, −0.43 dB/st down; was a straight 0.36), the drive's ceiling −2.5 dB at 10 |

Open, all measured:
- Pro-G's Vocal style turns broadband noise down, by 9 … 15 dB at −30 dBFS and 21 … 28 dB at −13. It leaves
  low-passed noise and sines alone. The threshold and the side-chain filter don't change it, and Range caps it. It is
  not built: the trigger isn't simply high-frequency noise, because the drums' hats are not cut.
- Pro-C 3's TTM and Vari-Mu read 2.5 … 4.5 dB louder than ours on programme. Their refits halve the bank error but
  lose round 108's own stairs, so they are kept as they were.
- Voice Shift's spectrum on a shifted voice: the overlap-add leaves energy between the harmonics and under the
  fundamental. The original's is clean. This is now the main gap, at 4 … 8 dB third-octave distance.

## Round 183b: the Valhalla reverbs' sound, one engine per slice

**Method**
- Black-box only, as round 172d: tp172d's matched settings (D, S1 …) and its probe renders, plus round 183b's own captures (run by round 182a's capture holds: fxmeasure, public parameter API, one process per script). Per pair the captures sweep every main control from the default state at Mix 100 %, and add the impulse at −20 dB, music at −20 dB and a noise burst. Data: /mnt/e/lynx-work/verb183b (cap/, bank.py, hz_before.txt / hz_after.txt).
- Scores: **fxparity** per setting and signal (impulse file, drums, music), and **verb** on the impulse render's first 3.9 s (tp172d's windowing), both on the matched settings ("main") and on the whole bank. The PARITY "Sound" column follows them.
- **The reference is not linear in Tape echo mode** (FutureVerb's default; ValhallaDelay's Tape too). Its response to tp172d's 0.5 impulse has 5 dB more below 500 Hz than its response to the same impulse at −20 dB. Its music, drums and noise follow the −20 dB response within ~2 dB, and the excess goes away in its Modern echo mode. So the low end of a 0.5-impulse verb score is partly this artefact. Round 183b fits the −20 dB response and the programme material, and reports verb on both impulses.
- Valhalla demo dropouts: 2 of the 62 FutureVerb captures (High EQ 0.6, Hall Early/Late 100 %) fade to silence near 2.4 s and are left out of the bank. The Low EQ 0 capture starts 0.7 s late and is left out too. Five music renders (Size 100 %, Dark, the level test, Drive 0, Cathedral) have a 1.5–1.8 s hole at random times, so their music is the demo's. Those stay in both the before and the after bank. The loud-programme figure below is taken without them: music at 0 dB sits 1.05 dB under its −20 dB render.

| LYNX engine | Original | fxparity main / bank | verb main / bank | What changed |
|---|---|---|---|---|
| Horizon (slice 1) | ValhallaFutureVerb | 3.00 → 3.25 / 2.25 → 2.81 | 7.84 → 8.35 / 6.79 → 7.54 | See the list below. Over the bank, the music level error is 4.2 → 1.2 dB (music score 2.3 → 3.2), drums 3.25 → 3.75, the tail balance 4.8 → 6.8 and the level 5.5 → 8.1 of 10. |

**What changed in Horizon (slice 1):**
- **The echo is the pre-delay.** This follows the reference's manual and was measured. In Echo → Reverb, the reverb hears the echo's output, not the dry input. Echo Level only sets the echo heard beside the reverb, so at Level 0 the echo is still the pre-delay (30 ms by default), and the reference's onset moves with Echo time. Before, the echo was skipped at Level 0 and was never heard directly. Drive works in the plain echo modes and Detune in the granular ones (Tape played 4 cents sharp before).
- **Low EQ** is a single −6 dB/oct cut, in the echo. Moving it from 100 to 400 Hz takes 11.5 dB at 30 Hz; with the old echo and output cuts together it would be 23.
- **High EQ** is a −6 dB/oct cut in the echo and in the reverb, and it sets the high decay. RT(8 kHz) / RT(1 kHz) is 0.47 / 0.65 / 0.99 at 1 / 4.5 / 20 kHz; before, it was fixed.
- A +3 dB shelf below 1.4 kHz: the reference's tail is fuller there, measured on the −20 dB impulse and on music.
- **Colours** follow the manual and the renders. Bright and Neutral play alike. Dark adds a steep 8 kHz low-pass. The fourth colour, now named Studio as in the reference (it was "Vintage"; saved projects keep it), adds a 12 dB/oct cut at 560 Hz and a steep 12 kHz low-pass. Before, these were first-order cuts at 16, 11 and 9 kHz.
- **Modes:** RT against Decay, bass decay, the onset after the echo, level and tone are measured per mode (Room, Chamber, Plate, Hall, Cathedral, Space).
  - Room's lines are 1.6 × longer. Before, a tank mode sat on the stimulus's 48 Hz bass note, and music was 9 dB loud.
  - Plate feeds its tank at once. Before, its 8 ms build-up taps cancelled each other below 120 Hz.
  - Nonlin's length follows Size (gated after 0.1 / 0.33 / 1.05 s at Size 0 / 50 / 100 %), and Frozen's RT does too (2 / 15 s at Size 0 / 50 %). Both followed Decay before.
  - The mode steps switch at the reference's (k + 1) / 24 points (its factory presets): 0.0833 still plays Room and 0.3333 plays Frozen.
- **Size and Density:** the onset grows with Size (25 / 100 / 139 % of its 50 % value at Size 0 / 50 / 100 %). The level holds over Size and Density as measured. Early/Late barely moves the level (+1.6 / −1.3 dB at 0 / 100 %).
- **Loud programme:** the reference runs 1–2 dB under its −20 dB response (music, drums, a noise burst; about 1 dB with its clean Modern echo mode). Horizon now has a gentle input compressor: −30 dBFS threshold, 1.25 : 1 ratio, 30 ms.

**Open (Horizon):**
- The onset. The reference rises 6 dB in 10 ms and then holds flat. Ours has the early taps first, then a 15 ms dip before the tank arrives. A first try made the onset right but moved the level (worse overall), so it was not kept.
- The reference's loud-burst behaviour: its tail is 5 dB down after a 0.3 s burst at −1.6 dBFS peak. A static compressor cannot match music, drums and that burst at once.
- Frozen's hold-and-release envelope (about a 4.5 s hold at Size 50 %, then a fade) is not modelled: the tail just decays.
- Space's attack shapes, and Cathedral's lows below 100 Hz.
- The tank loses some highs of its own, so RT(8 kHz) reaches 2.75 s at High EQ 20 kHz where the reference has 3.0 s. The mid RT runs ~8 % long at the default (3.27 against 3.01 s at 1 kHz), from the bass-decay crossover.

## Round 172d: Soundtoys / Valhalla / Waves roles — measured sound, before → after

**Method**
- Black-box only. The originals are rendered by fxmeasure, with one process per setting for the demo-limited Valhalla and Waves plugins. LYNX is rendered by `tools/fxrender` through its own state translator; the Soundtoys mappings are done by hand.
- **fxparity** (`scripts/fxparity.py`, round 172b's harness) is residual-first. A 100 %-wet reverb cannot null, so it tops out near 4.
- **verb** (`scripts/fxparity.py verb`, round 172b's shared reverb score, one metric for every reverb) scores a reverb's impulse response 0–10 as the mean of seven parts:
  - RT60 per octave
  - EDC shape
  - early energy and pre-delay
  - the tail's tonal balance
  - L/R correlation
  - tail movement
  - wet level

Both numbers are given below.

| LYNX engine | Original | fxparity | verb | What changed |
|---|---|---|---|---|
| Nebula | Valhalla Supermassive | 1.5 → 3.2 | 5.7 → 6.8 | a wet gain per mode (22-mode probe); level flat over Feedback (measured knots); a low shelf with Density; first-order cuts |
| Atrium | ValhallaRoom | 1.8 → 3.2 | 7.5 → 7.4 | linear early/late crossfade (late +6 dB at 50 %); +2 dB; the original's low end (+10 dB shelf at 90 Hz, −5 dB at 140 Hz) and air (+4 dB at 9 kHz) |
| Horizon | ValhallaFutureVerb | 1.6 → 3.0 | 6.5 → 8.1 | level ∝ 1/√RT (the original's wet energy stays at 22–24 dB over Decay); HF damping ×0.7 above 4 kHz; first-order EQ cuts; Room / Hall / Space gains |
| Foil Plate | ValhallaPlate | 2.7 → 3.1 | 8.2 → 8.4 | band edge ~11 kHz, 16 poles (the original is brick-walled: 12.7 kHz −35 dB); +2.3 dB |
| Foil Plate (plate voicing) | Little Plate | 1.4 → 2.2 | 6.0 → 7.5 | level re-measured at Mix 100 % (it was 6–16 dB low, most at short decays: knots per decade of Decay); darker above 2.5 kHz (−5 dB shelf, 7 kHz low-pass); early part −9 dB; RT per band already within 5 % |
| Lattice Mod | ValhallaUberMod | 3.1 → 3.9 | — | the wet is in phase with the dry (the dry + wet comb notch measured on music; round 101's inverted wet does not hold here) |
| Ember Saturator (Classic) | Decapitator | 4.6 → 4.8 | — | with Punish on, E / N / T / P saturate at the measured level (the curve, knees and bias, scaled ×0.40 / 0.90 / 0.65 / 0.86; the small-signal law is unchanged) |
| Halo | ValhallaShimmer | 2.2 → 3.2 | 6.2 → 7.1 | slice 5, measured on a 12-setting bank of the original (refs/halo: Feedback 0…100 % × shift 0 / +12 st × Size 30 / 50 / 80 %; 20 s impulses, music, drums). The shimmer is now injected: the line keeps all of itself and the shifted voice (high-passed at 120 Hz) is added on top. The old crossfade moved a share of every pass up an octave, where the absorption took it, so the tail died in ~2 s. RT now follows the original's law per Size (1.5 + 12.5 × Size s) and Feedback (×1.2 / 1.65 / 2.2 at 50 / 75 / 100 % shifted, ×1.4 / 2.1 / 4.3 unshifted). It blooms over ~0.3 s (early level 0.45 → 0.3), and the wet is −2 dB up to Feedback 75 %. RT per octave is now within ~10 % in every setting; it was 2–3 s against 9–19 s with the shift. The music / drums level error across the bank is 1.6 dB mean (it was 1.8, and the RT was wrong). Per setting, fxparity / verb: D 3.3 → 3.7 / 7.8 → 7.5, S1 2.0 → 2.3 / 3.3 → 6.8, S2 1.7 → 3.3 / 6.0 → 6.7, S3 2.0 → 3.7 / 7.6 → 7.5. Open: the original's impulse has +9 / +5 dB more at 63 / 125 Hz while its music and drums don't (a shelf for it made drums 5 dB loud), and +10 dB more at 16 kHz. |
| Orbit / Spiral Echo / Epoch Verb | Space Modulator / Freq Echo / VintageVerb | 3.8 / 3.4 / 2.9 (unchanged) | Epoch 7.0 | Epoch's "39 Hz mode" is not a mode. 85 % of the music stimulus's energy is three bass notes (32, 44 and 48 Hz). The original's response has a notch there (−10…−13 dB below its low-end mean); ours has a peak (+6…+9). The impulse's band levels and RTs per octave match within 2 dB. Left alone: matching the original's mode positions would tune to the stimulus, not to the reverb. |
| Echoform | EchoBoy / ValhallaDelay / H-Delay | 5.2 / 3.8 / 5.9 | — | measure only (EchoBoy's mapping fixed: feedback units) |
| Halo (slice 6) | ValhallaShimmer | 3.2 → 3.0 | 7.1 → 7.5 | the build-up, measured on refs/halo plus two Diffusion captures (0 / 50 %). The original's energy rises for 0.6 / 0.9 / 1.3 s at Size 30 / 50 / 80 % (ours peaked at 0.3 s): the tank is now fed evenly over 0.62 + 1.8 × Size s, which fits its envelope within 0.7 dB. With less Diffusion the original gathers late (50 %: a cluster near 1.2 s; 0 %: one arrival at 1.217 s), its RT shortens (~5.5 / 6 s against 9.8 s) and its early part is empty; the feed leans late, the RT scales by 0.55 + 0.45 × d³, the early level by d⁸ and the level rises up to +5 dB (d = Diffusion / 92 %; a model between the three measured points). The wet is −3 dB up to Feedback 75 %, then rises to +1 dB at 100 %. The bank's music / drums level error is 0.7 dB mean (it was 1.6); early / mid / late energy is within +2 / +3 / +4 dB (it was −4.5 / +2.6 / +9.7; part of the late gap is the demo's noise floor). Per setting, fxparity / verb: D 3.7 / 8.7, S1 2.0 / 7.5, S2 2.7 / 7.1, S3 3.7 / 6.6 (S1 / S2 lost a point on drums for the better impulse). |
| Auto-pan (Rhythm) | PanMan | 3.2 → 4.8 | — | PanMan's default shape is a ramp (fitted within 1° on its 0.25 / 0.63 Hz pan trajectories): right to left over the period, back over 45 ms + Smoothing × a quarter of the period. New Pattern option "Ramp" (Left / right · Ramp · 8 steps; saved 0 / 1 keep their meaning). Harness fix: the original's In/Out "Analog" is the analog stage, not the "hot" one (3.2 → 4.3 of the gain). Per setting: D 2.7 → 4.7, S1 2.7 → 4.7, S2 4.3 → 5.0. |
| Voice Shift | Little AlterBoy | 2.0 → 2.8 | — | measured on refcap0124's Drive, Pitch and Formant sweeps: Drive adds 1.72 dB per step into an output that falls 1.46 dB per step (a soft clipper with a small bias); the wet sits 2.7 dB under the input; the original's voice gets 0.36 dB louder per semitone of pitch against the formants (ours kept the level). Levels now within ~1 dB (they were up to +5.5 dB). Per setting: D 2.7 → 4.0, S1 2.3 → 2.3, S2 2.0 → 2.3, S3 1.0 → 2.7. Open: the original decorrelates L / R in shifted settings; its tracking of polyphonic music differs. |
| Glow Leveler / Compressor / Gemini / Quell | CLA-2A / RComp / Doubler2 / NS1 (Waves) | 4.3 / 6.4 / 4.4 / 4.0 | — | measure only (refcap licence of 11-04) |
| Glow Leveler (slice 9) | CLA-2A (Waves) | 4.3 → 5.2 | — | the Compress side chain refitted (black-box: the trial's renders of 11-04 and refcap0124's sweeps at peak reduction 46 / 56 / 100). The original reduces more at both ends of the band than at 750 Hz (−18 dBFS sweep, peak reduction 56: 1.9 dB more at 95 Hz, 5.2 dB more at 6 kHz), so music, drums and pink noise came out about 2 dB too loud while the 1 kHz stairs matched. A low peak (105 Hz, +5 dB) and a wider, stronger high one (8.9 kHz, +11.9 dB, bounded at 12 dB) replace round 123's emphasis; the chain keeps its 1 kHz gain, so the 1 kHz laws of rounds 152 / 165w / 165x hold. Gain error rms 1.54 → 1.02 dB; per setting D 4.0 → 5.5, S1 4.3 → 5.5, S3 3.5 → 4.3. Limit keeps the old side chain (its law was fitted on it; the new one made it worse). Open: a steady 6 kHz sine reduces 7.1 dB more than 750 Hz, the sweep 5.2 |
| Foil Plate (plate voicing, slice 10) | Little Plate | 2.2 → 3.4 | 8.1 → 8.6 | measured on the trial's renders (Decay 1 / 3 / 10 s, impulse, drums, music) and refcap0124's sweeps at Decay 0.5 / 1.7 / 3 / 5.7 / 19 s. The original has no sub-bass: 20 … 32 Hz sat 11 … 16 dB and 32 … 40 Hz 2 … 8 dB above it, which put music 5 dB too loud (its energy is in bass notes at 32 … 48 Hz). The voicing now cuts at 55 Hz, 12 dB/oct, unless the panel's Low Cut is higher (45 Hz scored 3.2). It was also 7 … 9 dB too bright from 2 to 8 kHz up to Decay 1.7 s, 5 … 7 dB at 3 s and 2 … 3 dB from 5.7 s on: the high shelf at 2.5 kHz goes from −5 dB to −13.5 / −12 / −9 dB with Decay, the edge from 7 to 9 kHz. Per setting D 2.0 → 3.7, S1 1.7 → 2.7, S2 3.3 → 3.3, S3 2.0 → 4.0. ValhallaPlate (voicing off) unchanged, 3.1. Open: at Decay 1 s the trial's impulse and music read 4 … 5 dB too loud from 250 Hz to 4 kHz, while its sweeps at 0.5 / 1.7 s do not; not changed. verb (the shared reverb score on the impulse renders' first 3.9 s, before the stimulus's burst at 4 s): D 8.28 → 8.74, S1 7.25 → 8.10, S2 8.43 → 8.73, S3 8.30 → 8.74; the level part gains most (S1 2.7 → 5.9, D 6.7 → 8.8), the early part drops a little (D 6.8 → 6.6, S2 6.2 → 6.0) |
| Voice Shift (slice 11) | Little AlterBoy | 2.8 → 2.9 | — | at its default state (Pitch and Formant 0) the original passes a vocal almost untouched: 2.7 dB down, flat within 0.4 dB to 4 kHz, the rest 15.5 dB under it (a sweep 6.4 dB). Voice Shift sent it through the overlap-add anyway and left the rest only 2.2 dB under it (a sweep 0.5 dB). With nothing to shift (Transpose, no MIDI note, Pitch and the formant shift 0) the wet path is now the delayed input, crossfaded over 20 ms (−145 dB residual on a 200 Hz voice at 0 st). fxparity (fxrender16): D_vocal 4 → 5 (ESR −15 dB), the shifted settings unchanged. A probe bank of the original (182a, fresh instances: Formant −12 … +12, Pitch −12 … +12, Link, Quantize, Drive 3 / 5 on a synthetic vowel, a 120 Hz pulse train and the vocal) agrees with Voice Shift on pitch and on where the vowel's formant peaks move. Levels on the vocal are within ±1 dB except Formant −12 (+2.0 dB) and Pitch ±12 (+1.1 / +1.2). The S1–S3 L/R de-correlation of the round's first captures is a history effect: the original keeps a pitch shifter per channel that drifts apart once it has processed stereo input (a reset does not re-align them). A fresh instance keeps a mono voice mono (L = R) as Voice Shift does; not modelled. Open: on the broadband pulse train Voice Shift is 2 … 4.5 dB louder in shifted settings (not on the vocal); above 11 kHz the original adds energy when shifting (+3 … +26 dB against the input on the vocal, likely its own aliasing), where Voice Shift loses 4 … 7 dB |

**Decapitator captures:** the original's Auto moves its Output knob asynchronously after a Drive change made by host automation. The earlier captures had the trim in some settings only, which made styles E / N / T look 8–11 dB too loud and gave a mean of 3.2. All its captures now set Auto off and pin Output at 0 dB first.

**Look:**
- The Valhalla roles have flat themes in the original's manner: solid panels, flat disc knobs, bar sliders, large legends, and per-plugin palettes.
- Echoform, Ember, Voice Shift and Auto-pan carry a Soundtoys-style hardware theme: black face, walnut cheeks, rubber knobs and wide-tracked caps.
- Everything is drawn by LYNX code, with LYNX names.

**Look, second slice (the Soundtoys roles' panels):**
- **Ember ← Decapitator.** An Ember added from the mixer opens on the Classic panel, laid out like the original:
  - Punish top left, the VU in the middle with the name beside it, Output and Auto under the name;
  - the big Drive left, then Low cut (Thump under it), Tone, High cut (Steep under it) and Mix in one row;
  - the five style keys (A E N T P) joined by a line along the bottom.
  
  "Advanced" swaps the window for the multiband page, whose "Classic" key swaps back. Loaded projects keep their voicing: only the mixer's Add sets Classic, and Ember's default parameters are unchanged.
- **Echoform ← EchoBoy.** Three rounded pods:
  - the echo pod: Mix, Echo time and Feedback; Low cut, Prime numbers and High cut under them;
  - the tempo pod: the tap pad, Groove / Feel and the echo mode;
  - the saturation pod: Saturation, Age and Output, with the Style and Tape menus.
  
  The Tweak strip and the rest of the controls sit below.
- **Voice Shift ← Little AlterBoy:**
  - the three printed fields (mustard pitch / formant, teal mode, red drive / mix);
  - square mode keys;
  - the name strip along the bottom.
- **Auto-pan ← PanMan:** rounded pods and square keys.
- **The hardware themes:**
  - square bevelled keys for tabs, switches and option lists;
  - wider walnut cheeks;
  - a fill colour per section.

**Look, third slice:**
- **Echoform ← EchoBoy.**
  - The default page is the three pods only, with bigger knobs (Echo time 84 px), generous spacing and the walnut cheeks.
  - The Tweak strip, the switch rows and the Style edit moved behind a "Main | Tweak" key pair, bottom left: EchoBoy's TWEAK key.
- **Auto-pan ← PanMan.**
  - Bigger pan knobs (52 px), the name in the free corner, and the walnut cheeks.
  - The Rhythm voicing's Tweak and Trigger rows are on a Tweak page, so the panel is 290 px tall instead of 444.
  - The plain panner (Voicing off) shows on both pages.

**Look, fourth slice (the Waves roles with a trial):**
- **Glow Leveler ← CLA-2A:**
  - a light brushed-silver faceplate with corner screws and black bakelite knobs read off their pointers;
  - red lit keys and dark legends;
  - a near-black lower strip under the trims (Analog, HF emphasis, Mix, Trim, Makeup ref), with light legends.
- **Quell Denoiser ← NS1:**
  - a graphite plate;
  - a dark recessed meter well with a thin soft light bar and its scale on both sides, and a small orange read-out;
  - a large grooved metal fader cap on a recessed slot, with the 0 … 100 scale on both sides.
- Gemini Doubler already matched Doubler's look.
- **Pending a licence:** LYNX Tune ← Auto-Tune Pro. The trial has expired. Once a licence is in, sound captures (correction speed, humanize, formant, the artefact character) and the look will be done together.

**Look, fifth slice (round tp172d7: the Soundtoys and Valhalla roles, closer to their originals):**
- **The rule for every third-party look:** a caption over each knob and the original's printed scale words under it (Min / Max, Dry / Wet, −24 / +24, 20 Hz / 1 kHz…); the value shows only while the knob is hovered or dragged. Halo keeps its values under the knobs because its original prints them there.
- **Halo ← ValhallaShimmer:**
  - a thin, wide-spaced, two-tone lower-case "lynx halo" wordmark;
  - nine big flat discs shading from black to light grey, with no ring;
  - large lower-case captions and values;
  - the three modes as large "reverb mode  mono" text along the bottom (a click opens the menu).
- **Auto-pan ← PanMan (the Rhythm voicing):**
  - the name block top left;
  - three bowed pods with scale words;
  - the direction keys as square keys with drawn arrows (◄ ramp, ◄► left / right, ► ramp the other way, plus 8 steps), lit amber;
  - Input and Output in the level pod: the Rhythm voicing now reads Auto-pan's Input, ±24 dB, 0 dB by default;
  - the TWEAK key.
  - Ramp ► is a new pattern: the measured ramp, left to right.
- **Voice Shift ← Little AlterBoy:**
  - a dark face round the three printed fields;
  - big legends;
  - LINK as a small key on a line joining Pitch and Formant;
  - Min / Max and Dry / Wet under Drive and Mix;
  - a "voice shift" wordmark.
- **Echoform ← EchoBoy:**
  - TIME / NOTE / DOT / TRIP keys over a red LED time read-out;
  - a TAP TEMPO pod with its key and LED tempo;
  - Groove (Shuffle / Swing) and Feel (Rushing / Dragging);
  - the echo mode as a rotary with its five modes printed round it;
  - Input / Saturation / Output;
  - Style and Tape on red LED displays with step keys;
  - spaced pod titles.
- **Ember ← Decapitator:**
  - chunky black caps on rings of dots, with no lit arc;
  - the big Drive printed 0 … 10;
  - bigger spaced legends;
  - EMBER between two rules;
  - a chrome bezel round the VU.
- **Glow Leveler ← CLA-2A:** Gain and Peak reduction printed 0 … 100; the lower strip's scale words beside its knobs.
- The arrows, LED read-outs, bezel and wordmarks are drawn by LYNX code. No logo, bitmap or font of the originals is used.

**Look, sixth slice (round tp172d8):**
- **Glow Leveler ← CLA-2A:**
  - the original's wide proportions (660 × 312) on one untitled faceplate;
  - Gain and Peak reduction as chrome domes in black knurled skirts, printed 0 … 100, the number at the pointer in red;
  - COMPRESS / LIMIT and AUTO MAKEUP as metal bat toggles on hex nuts, the active word red (AUTO MAKEUP above, OFF under);
  - VU DISPLAY as a black pointer knob with INPUT / GR / OUTPUT printed round it (the chosen one red), the VU in a gunmetal frame, "GLOW" in big type under it;
  - a charcoal strip with gold 50Hz / 60Hz / OFF keys (the lit one cream), HI FREQ as a big cream chicken-head pointer knob, MIX and TRIM as medium knurled knobs, and big ANALOG / HI FREQ / MIX / TRIM captions; [ FLAT ], 0 / 100 and -18 / 18 printed as on the original;
  - LYNX's extras (Modern / Opto classic, the opto law and release) are one compact group of small muted keys where the original has its badge (full while hovered, the chosen one red); Makeup ref sits at the strip's right end.
- **Echoform ← EchoBoy:** the name block top left ("Echoform" and LYNX's own subtitle); LOWCUT / HIGHCUT and RUSHIN / DRAGGIN printed as on the original; thicker, whiter pod outlines.

**Design gaps still open:**
- Atrium's Predelay and Decay bars draw linear while the original's are skewed.
- Ember's VU reads "VU". The original's meter face and logo are not reproduced, as the rules require (the Drive's printed 0 … 10 came in the fifth look slice).
- Auto-pan has no LFO key: the Rhythm voicing has no free-LFO switch to put there.

## Round 183b slice 2: Nebula ← Valhalla Supermassive

Measured on tp172d's 41 probe renders and 19 new captures at Mix 100 %: Mod depth 0 / 10 / 25 / 50 / 100 %, Mod rate 0 / 100 %, Width 0 / 50 %, Density 50 / 100 %, and the default. Method and scores as in "Round 183b" (slice 1, Horizon) above; a 52-setting bank.

| LYNX engine | Original | fxparity main / bank | verb main / bank | What changed |
|---|---|---|---|---|
| Nebula (slice 2) | Valhalla Supermassive | 3.20 → 3.33 / 3.11 → 3.33 | 6.05 → 7.19 / 6.16 → 7.30 | Over the bank, the verb's stereo part goes from 1.8 to 9.3 of 10 and its level part from 7.7 to 8.1; drums 3.00 → 3.17, music 3.17 → 3.33, impulse 2.90 → 3.29. |

- **Stereo.** The reference's tail is decorrelated whenever it modulates: L/R correlation −0.01 at the default Mod depth (50 %), 0.13 at 25 %, 0.34 at 10 %, and 1.0 (mono) at 0. That holds at every Density, and Width 50 % is mono. Nebula played mono at Density 0, because its old per-mode correlations were set from a different measurement. Now the width is the panel's, and this network decorrelates through its modulated lines in the same way (0.06 / 0.08 / 1.0 at 10 / 25 / 0 %). Only the reference's mono modes (five of them, plus one with L = −R) keep a narrow width. Their correlations were re-measured per mode at the defaults.
- **Level against Mod depth.** The reference's eight voices fall apart as they modulate. Its unmodulated tail is 8.9 dB louder than at 50 %, and 2.4 dB louder at 10 %. This network's lines part less, so a law adds the rest (+5 dB × e^(−depth / 10 %)). With the full width, the level is re-measured: −0.9 dB at Density 0 and −4.2 dB from Density 50 % up, where the dense network's sides were nearly mono before.
- **Open:**
  - Several modes swell up over 1–2 s at Density 0 (the reference's 3rd to 6th modes: −45 → −12 dB over six repeats in the 4th); ours start at full level.
  - The reference smears each repeat over 2–5 ms, with different smears left and right. An in-loop all-pass for that was tried and scored lower, so it was dropped.
  - The 4th mode (S2) has its first arrival 300 ms later in the reference.

## Round 165j: LYNX Tune sound quality, before → after (measured)

Round 165j (`docs/analise/REA_rodada165j.md`): LYNX Tune against the official Auto-Tune Pro 11.6 and Waves Tune
Real-Time trials (hosted black-box) on ten synthetic takes with a known f0 and on her 24 takes with her own Auto-Tune
states (translated as LYNX loads them). Config / design scores unchanged (no controls were added); this table is the
sound.

| Measure (her 24 takes unless noted) | Auto-Tune Pro | LYNX 0.1.19 | LYNX 0.1.20 |
|---|---|---|---|
| pitch: median / p90 cents from the note | 5.79 / 26.2 | 4.39 / 20.9 | 4.42 / 21.0 |
| same note as Auto-Tune (% voiced frames) | 100 | 95.8 | 95.7 |
| HF balance vs the dry take (2.5–6.3 kHz over 0.3–1 kHz, dB) | −0.07 | −0.52 | −0.13 |
| formant envelope distance to the dry take (dB) | 0.49 | 0.80 | 0.78 |
| short bursts / min not in the dry take | 31.2 | 40.9 | 40.1 |
| Classic / Low latency: bursts / min, HF balance | 27.9, −0.02 | 48.8, −0.51 | 45.4, −0.13 |
| synthetic: latency ms, accuracy med / p90 c, retune ms, vibrato kept | 61.6, 2.8 / 10.1, 42, 1.01 | 25.4, 1.7 / 4.4, 38, 0.97 | 25.4, 1.7 / 4.4, 38, 0.97 |
| synthetic: HF balance, formant distance (dB) | —, 0.18 | −0.40, 0.28 | −0.04, 0.36 |
| CPU per instance (block 128, 44.1 kHz, median) | — | ~64 µs (2.3 % RT) | ~65 µs (+1 %) |

## Round 183b slice 3: Halo ← ValhallaShimmer

Measured on tp172d's settings, refs/halo (13 states) and 13 new captures, with the impulse at −20 dB, at Mix 100 %: Mod depth 0 / 60 / 100 %, Mod rate, Size 20 / 80 %, the stereo modes, Feedback 50 / 80 %, Diffusion 50 %. Method and scores as in "Round 183b" (slice 1, Horizon); the bank has 31 settings. **Sound 3 → 4** in the main table (that row is set when the slice lands, so it doesn't clash with slice 1's row next to it).

| LYNX engine | Original | fxparity main / bank | verb main / bank | What changed |
|---|---|---|---|---|
| Halo (slice 3) | ValhallaShimmer | 3.00 → 3.25 / 2.66 → 2.93 | 6.52 → 7.17 / 5.89 → 6.80 | Over the bank, the verb's stereo part goes from 7.2 to 9.5 and its early part from 3.5 to 7.6; music scores 2.95 → 3.35 and drums 2.72 → 3.11. |

- **Mono mode** (the reference's default: mono in) gives a partly correlated tail: L/R correlation 0.25 / 0.31 / 0.43 at Size 30 / 50 / 80 %. Ours was ~0, so the side gain now follows Size, and the level is kept equal to the full width. The stereo modes stay at ~0, as the reference's.
- In Mono mode the reference's first 80 ms hold 3.4 dB less of the energy than ours did, so the early pattern is at 40 % there.
- **Linearity:** the reference's tail is linear with Mod depth 0. With its modulation on, the 0.5 impulse comes out 2–6 dB fuller below 300 Hz than the −20 dB one. 172d saw the same low end against music; as in slice 1, the fit follows the −20 dB impulse and the programme.
- **Open:**
  - The reference's tail fluctuates about twice as much frame to frame (spectral centroid, 385 against 223), even at Mod depth 0. That looks like a sparser, grainier tank, not modulation.
  - Its RT at Diffusion 50 % (verb 3.8 there).
  - The tail balance, 2.7 dB off on average.

## Round 145: SSL Native Vocalstrip 2 (Ridge Strip's Vocal strip voicing)

Round 145 (`docs/analise/REA_rodada145.md`) measured the official Vocalstrip 2 trial (Complete Access) black-box and
gave Ridge Strip a **Vocal strip voicing** with every control of the reference: config **2 → 9** (no FFT
analyser), design **1 → 7** (the reference's modules and reading order in
LYNX's style; the transfer-curve and GR-history displays are bars). Her Vocalstrip 2 state now plays the measured
model (−60 … −67 dB null on her takes). The other Complete Access plugins (Slate, SSL, LiquidSonics, sonible,
Harrison) map to LYNX effects by role only.

## Round 183b slice 4: Foil Plate (voicing off) ← ValhallaPlate

Measured on tp172d's settings and 14 new captures, with the impulse at −20 dB (the reference is linear: its 0.5 and −20 dB impulses agree within 0.1 dB): Decay 20 / 80 %, Size 0 / 100 %, Mod depth 50 / 100 %, Low and High EQ gain 0 / 100 %, Width 0 / 100 %, and Pre-delay. Method and scores as in "Round 183b" (slice 1, Horizon); the bank has 19 settings.

| LYNX engine | Original | fxparity main / bank | verb main / bank | What changed |
|---|---|---|---|---|
| Foil Plate (slice 4) | ValhallaPlate | 3.13 → 3.73 / 3.03 → 3.64 | 8.15 → 8.49 / 8.13 → 8.48 | Over the bank: drums 3.00 → 3.83, impulse 2.80 → 3.80, −20 dB impulse 2.86 → 3.50; the verb's balance goes from 6.6 to 8.0 and its level from 8.1 to 9.1 |

- **Low EQ gain** spans ±12 dB, as the reference's (it was ±9). Its half-gain point is ~0.47 × the frequency law: −6 dB near 330 Hz at the default 711 Hz.
- **High EQ gain** boosts up to +12 dB with its half-gain point at ~0.5 × the frequency law (5 kHz at the default), but cuts only −4.5 dB at 0 %, as measured.
- **Width** keeps the level, as the reference does (−27.4 / −27.5 / −27.2 dB at 0 / 50 / 100 %). Ours moved 4 dB.
- **Tone:** the reference's tail is fuller below 80 Hz (+5 dB at 28–71 Hz) and less forward at 2.8–7 kHz (−3.5 dB). It sits about 1 dB lower, and its short decays are a little quieter than ours were. The engine gets a second low shelf for this.
- **Unchanged:** the plate voicing (the Little Plate role); its EQ gains sit at 0 dB there.
- **Open:** the EDC shape (verb edc part 8.5 → 7.8); music at Decay 20–25 % still runs +1 dB.

## Round 152: what changed (config / design, after 150 → after 152)

Round 152 (`docs/analise/REA_rodada152.md`, sub-rounds `REA_rodada152a.md` / `152b` / `152c`).

| LYNX engine | Original | Config 150 → 152 | Design 150 → 152 | Round-152 changes |
|---|---|---|---|---|
| Prism Multiband | Pro-MB | 10 → 10 | 10 → 10 | the attack in the gain stage (slow attacks now ≤ 0.4 dB off), Dynamic phase's own narrow-band / cut shapes (measured table, ≤ 0.05 dB), 0.8-octave minimum band; editor menus at the mouse, read-out above the band panel (checked by hand) |
| Compressor (stand-in) | Renaissance Compressor | 9 → 10 | 7 → 8 | bipolar gain-change meter, measured static curve at every ratio, RComp-style factory start (152a) |
| Glow Leveler | CLA-2A | 7 → 9 | 7 → 8 | the reference's fresh instance, ANALOG 50 / 60 Hz, measured Limit and AUTO MAKEUP; translated (152a) |
| Auto-pan (stand-in) | PanMan | 8 → 9 | 7 → 8 | trigger sources Ping-pong / Random step (threshold, divider, detector filter, manual trigger), Random steps |
| Foil Plate | Little Plate | 9 → 9 | 6 → 8 | the one-knob Plate view (the substitute opens in it) |
| Facet EQ | Pro-Q 4 | 9 → 9 | 8 → 8 | spectral dynamics (Spectral / Density / Tilt per band, measured per-bin law, 5120-sample latency) and EQ match (capture reference + track, Match 25 … 100 %) (152c) |
| Crest Compressor / Echoform | Pro-C 3 / EchoBoy | — | — | stale ❌ rows re-checked: Host sync / MIDI trigger and audition (round 137), EchoBoy state recognition (round 136), EchoBoy ping-pong start side (round 135w); no ❌ row is left |

### Sub-round 152b

Sub-round 152b (`docs/analise/REA_rodada152b.md`), measured black-box on the official Saturn 2 trial.

| LYNX engine | Original | Config 150 → 152 | Design 150 → 152 | Round-152b changes |
|---|---|---|---|---|
| Ember Saturator | Saturn 2 | 8 → 9 | 8 → 8 | each crossover's own slope (6 … 48 dB/oct, exact), linear phase (zero-phase bands, 3072-sample latency), modulation (2 LFOs, envelope follower, 4 slots, the reference's laws), Breakdown's pitch drop on Collapse; translated |
| LYNX Tune | Waves Tune Real-Time | 9 → 10 | 8 → 8 | Group Octaves off (per-key statuses), MIDI reference tone |

## Round 183b slice 5: Atrium ← ValhallaRoom

Measured on tp172d's settings and 13 new captures, with the impulse at −20 dB (the reference is linear): Decay 10 %, Early/Late 0 / 100 %, Late size 0 / 100 %, Bass and High multiply, Late mod depth 0 / 100 %, Early size, Diffusion 0, High cut. Method and scores as in "Round 183b" (slice 1, Horizon); the bank has 17 settings.

| LYNX engine | Original | fxparity main / bank | verb main / bank | What changed |
|---|---|---|---|---|
| Atrium (slice 5) | ValhallaRoom | 3.17 → 3.50 / 3.14 → 3.59 | 8.08 → 8.54 / 8.14 → 8.52 | Over the bank: music 3.17 → 3.83, drums 3.60 → 3.80, the −20 dB impulse 3.00 → 3.54; the verb's early part goes from 7.0 to 9.4 and its level from 8.4 to 8.9 |

- **Early/Late** is an equal-power crossfade. The reference's level moves only 2.6 dB from early only to late only (−28.7 / −27.2 / −26.1 dB at 0 / 50 / 100 %). Round 172d's linear fade moved it 10.7 dB.
- **The late level holds over Decay** (the reference: −27.2 / −26.8 dB at RT 2.1 / 8.6 s; ours rose by 5.6 dB) and over **Late size** (ours ran +3 dB at 0 % and −1.2 dB at 100 %).
- The low shelf goes from +10 to +5 dB at 90 Hz: with the louder early part it was 4–7 dB too much below 80 Hz on the impulse and on music. The level is +1.4 dB.
- **Open:**
  - The reference's early part alone is nearly mono (L/R correlation 0.96 at Early/Late 0); ours is decorrelated (−0.55).
  - Its RT is 1.28 / 1.22 × ours at 125 Hz / 8 kHz at the defaults.
  - S3 (High cut 30 %, Early/Late 20 %): the music is +2.6 dB while the impulse is −2.9 dB.

## Round 150: what changed (config / design, after 147 → after 150)

Round 150 (`docs/analise/REA_rodada150.md`, sub-rounds `REA_rodada150a.md` / `REA_rodada150b.md`).

| LYNX engine | Original | Config 147 → 150 | Design 147 → 150 | Round-150 changes |
|---|---|---|---|---|
| Prism Multiband | Pro-MB | 10 → 10 | 9 → 10 | free layout with gaps, slopes 6 … 48 dB/oct (measured per processing mode, the 6 dB/oct designs), side-chain input per band, narrow free side-chain filter, measured detector (soft range, release after the curve); translated |
| Compressor (stand-in) | Renaissance Compressor | 8 → 9 | 7 → 7 | the reference's own ranges: ratio 0.5 … 50 with the measured upward expansion, gain ±30, attack 0.5 … 500, release 5 … 5000 ms; translated |
| Echoform | EchoBoy / PrimalTap | 10 → 10 | 8 → 8 | voicings in Dual / Ping-Pong (measured), L/R offset, wobble shapes / depths from delay tracks (no style has several voices on the trial) |
| Ember Saturator | Saturn 2 | 7 → 8 | 7 → 8 | six bands, global input / output pan, 28 measured characters (LYNX names); translated |
| LYNX Tune | Waves Tune Real-Time | 9 → 9 | 7 → 8 | Send / Receive of the scale between instances |
| Auto-pan (stand-in) | PanMan | 7 → 8 | 6 → 7 | Tweak page (rate / offset / width modulation by the measured envelope law, Feel / Groove, beyond the speakers) |
| Foil Plate | Little Plate | 8 → 9 | 6 → 6 | measured Little Plate voicing (band decays, 0.7 ms onset, ∞ ≈ 80 s) |

## Round 183b slice 6: Epoch Verb ← ValhallaVintageVerb

Measured on tp172d's settings and 12 new captures, with the impulse at −20 dB (the reference is linear): Decay 20 %, Size 50 %, Attack 0 / 100 %, Bass multiply 0, Mod depth 0 / 100 %, Early / Late diffusion 0, and the 1970s / 1990s colours. Method and scores as in "Round 183b" (slice 1, Horizon); the bank has 17 settings. The main table's Sound stays 5: Atrium scores 5 at fxparity 3.50 / verb 8.54, and this pair is at 3.07 / 8.44.

| LYNX engine | Original | fxparity main / bank | verb main / bank | What changed |
|---|---|---|---|---|
| Epoch Verb (slice 6) | ValhallaVintageVerb | 2.93 → 3.07 / 3.06 → 3.39 | 8.11 → 8.44 / 8.11 → 8.49 | Over the bank: music 1.89 → 2.89 (level error 3.3 → 1.9 dB), drums 3.00 → 3.17, the −20 dB impulse 3.67 → 3.83; the verb's early part goes from 6.9 to 8.4 and its edc part from 8.2 to 8.6 |

- **The early share grows with Decay.** At its default 4 s, the reference's first 80 ms hold −3.8 dB of the total; ours held −7.2. The early pattern now scales by 1.13 × √(Decay / 2 s), capped at 1.6, so round 100's measurement at 2 s (−3.1 dB in the first 100 ms) still holds.
- **The lines are 15 % shorter.** That puts the modes denser low down. The −20 dB impulses come closer (3.75 → 3.92 with the early change alone) and so does music: the 172d "39 Hz mode" gap goes from +5.7 to +3.5 dB at the defaults. The level is −0.7 dB.
- **Open:** music at the defaults still runs +2…+3.5 dB, on its bass notes.

## Round 147: what changed (config / design, after 144 → after 147)

Round 147 (`docs/analise/REA_rodada147.md`, sub-rounds `REA_rodada147a.md` / `REA_rodada147b.md`).

| LYNX engine | Original | Config 144 → 147 | Design 144 → 147 | Round-147 changes |
|---|---|---|---|---|
| Prism Multiband | Pro-MB | 9 → 10 | 8 → 9 | Dynamic phase measured and rebuilt (minimum-phase bands in cascade; older projects keep the classic engine), expert panel: side-chain filtering band / free, stereo link + mid / side-only, each crossover its own slope; translated |
| Echoform | EchoBoy / PrimalTap | 9 → 10 | 8 → 8 | 31 echo voicings measured from EchoBoy's styles (LYNX names), saturation type menu, PrimalTap's Multiply 1x–8x |
| Ember Saturator | Saturn 2 | 6 → 7 | 7 → 7 | 5 | 6 | channel mode L/R \ | M/S, each band's drive pan and pan (measured), translated |
| LYNX Tune | Waves Tune Real-Time | 8 → 9 | 6 → 7 | keyboard strip: Bypass / Remove modes, note status with direction, range bar and presets, octaves, scroll, latch |
| Ridge Strip | SSL E-Channel | 7 → 8 | 7 → 7 | ANALOG (measured noise floor), auto make-up for Ridge's own compressor, translated |
| Hush De-esser | DeEsser | 9 → 9 | 7 → 8 | the energy-detector bar beside the threshold arrow |

## Round 183b slice 7: Echoform ← ValhallaDelay

Measured on tp172d's settings and 12 new captures at Mix 100 %: the default (Tape), Age 0 / 100 %, wow and flutter off, Feedback 70 %, Drive, Diffusion, the Past / Future eras, and three other modes. Method and scores as in "Round 183b" (slice 1, Horizon); the bank has 16 settings. **Sound 4 → 5** in the main table: that row sits next to Atrium's, so it is set when the slice lands.

| LYNX engine | Original | fxparity main / bank | verb main / bank | What changed |
|---|---|---|---|---|
| Echoform (slice 7) | ValhallaDelay | 3.75 → 4.06 / 2.88 → 3.44 | 5.12 → 7.39 / 5.18 → 6.77 | Over the bank: music 3.27 → 3.82, drums 3.80 → 4.00, the −20 dB impulse 1.83 → 2.50; the verb's rt60 part goes from 2.8 to 4.3 and its edc part from 2.7 to 6.0 |

- **Tape mode's repeats.** At 300 ms and Feedback 70 % (its defaults), the reference's first repeat sits 2.6 dB above ours. After that it loses 5.6, 4.9, 4.6 … 4.3 dB per repeat; ours lost 7.9, 6.8 … 6.3. Echoform's tape path took −1.9 dB on every pass, so that loss is removed. The other modes were already right: the 0.2083 mode matches per repeat within 0.3 dB. EchoBoy's and H-Delay's settings don't use this path, and their scores are unchanged.
- **One tape transport in the Single style.** The reference's repeats stay mono with its default wow and flutter (tail L/R correlation 0.998, the same as with both off). Echoform swung each side on its own random wow, so the sides came apart (0.18) and their sum combed the repeats. In the Single style both sides now share the left side's swing: the verb's stereo part goes from 1.8 to 10 on the defaults, and the main verb score from 6.32 to 7.39. EchoBoy's voiced settings use their own measured wobble and are unchanged (5.33 before and after).
- **Measured, not modelled:**
  - The reference's tape hiss (−92 dBFS RMS, uncorrelated; −88 dBFS in the Future era) is inaudible, so it is left out, but it dominates the score of the quiet −20 dB impulses.
  - The Tape mode is not linear: the 0.5 impulse comes out 2–6 dB fuller below 300 Hz than the −20 dB one, as in FutureVerb.
  - At Feedback 140 % the reference's repeats grow by ~0.4 dB per pass at −20 dB, where ours stay level.
  - The reference's spectral fluctuation over the tail is 3–15× below ours on the defaults, and scaling our wow / flutter depth by 0.5–2× doesn't move it: the cause is elsewhere (open).
  - Its wow and flutter are random per render: two renders of the same setting differ by +2.7 dB ESR, so their phase cannot be matched.

## Round 144: what changed (config / design, after 137 → after 144)

Round 144 (`docs/analise/REA_rodada144.md`, sub-rounds `REA_rodada144a.md` / `REA_rodada144b.md`).

| LYNX engine | Original | Config 137 → 144 | Design 137 → 144 | Round-144 changes |
|---|---|---|---|---|
| Echoform | EchoBoy / PrimalTap | 8 → 9 | 7 → 8 | rhythm editor (Even / Free / Grid taps, Shape, 12 LYNX patterns), Style edit row (input, loop EQ, FB balance, diffusion loop / post, wobble shape / sync) |
| Lattice Mod | ValhallaUberMod | 8 → 9 | 9 → 9 | DelaySync measured (18 steps, 2750 ms cap) and translated |
| Facet EQ | Pro-Q 4 | 9 → 9 | 8 → 8 | linear-phase resolution Low … Maximum (the trial's latencies), translated |
| Hush De-esser | DeEsser | 9 → 9 | 6 → 7 | the GR meter's held peak (click to reset) |
| Prism Multiband | Pro-MB | 7 → 9 | 7 → 8 | Processing (Dynamic / Linear / Minimum phase, measured latencies), oversampling 2x / 4x, mid / side band pan, analyser resolution / tilt, translated; Dynamic phase still the old engine |

## Round 137: what changed (config / design, after 135 → after 137)

Round 137 (`docs/analise/REA_rodada137.md`) took the biggest remaining ❌ / ⚠️ rows and re-measured rows marked "not
measured" on the trials (EchoBoy, Pro-C 3 and Pro-Q 4 trials; Valhalla demos — black-box, short runs between the
demo's fades).

| LYNX engine | Original | Config 135 → 137 | Design 135 → 137 | Round-137 changes |
|---|---|---|---|---|
| Echoform | EchoBoy / PrimalTap | 7 → 8 | 6 → 7 | Rhythm echo mode (measured law: repeats, decay, 2-beat pattern, feedback replay), a bar per tap |
| Echoform | ValhallaDelay | 8 → 9 | 7 → 8 | Era (measured bandwidth per mode), ducking law measured, quad taps / Repeat-Swell / Era translated; stale rows re-checked |
| Echoform | H-Delay | 9 → 9 | 7 → 8 | LoFi switch independent of the mode, the note / BPM read-out on the picture |
| Lattice Mod | ValhallaUberMod | 7 → 8 | 9 → 9 | Input pan (measured routes), translated |
| Crest Compressor | Pro-C 3 | 8 → 9 | 8 → 8 | side-chain trigger: Host sync (note, rate, length — measured) and MIDI |
| Voice Shift | Little AlterBoy | 9 → 10 | 8 → 8 | MIDI control of the pitch / the robot note |
| Gemini Doubler | Doubler2 / Doubler4 | 9 / 8 → 9 / 8 | 7 → 8 | the three graphs editable (drag, double-click), axis labels |
| Quell Denoiser | NS1 | 9 → 9 | 8 → 9 | the fader's 0–100 scale, held reduction read-out |
| Horizon / Facet EQ | FutureVerb / Pro-Q 4 | — | — | measured only: Horizon's note list confirmed; Pro-Q 4's linear-phase latencies per resolution recorded |

## Round 135: summary (all pairs, config / design)

Round 135 (`docs/analise/REA_rodada135{f,s,v,w}.md`) implemented the ❌ / ⚠️ items, biggest user-visible gaps first
(149 ❌ rows before, 36 after). The Soundtoys trials were activated by the user and are now measured black-box (their
tables are no longer "manual only"); Auto-Tune Pro was re-activated and hosted black-box. **Waves has no licence on
this PC: every Waves plugin runs in demo mode** (the UI is complete, the audio mutes periodically), so Waves rows were
re-checked for controls and layout only, never for sound. "Before" = the scores after rounds 128 / 129.

| LYNX engine | Original | Config before → 135 | Design before → 135 | Main round-135 changes |
|---|---|---|---|---|
| Facet EQ | Pro-Q 4 | 8 → 9 | 8 → 8 | Flat Tilt / All Pass shapes, Character menu |
| Apex Limiter | Pro-L 2 | 7 → 9 | 8 → 9 | loudness meter (M / S / I / LRA / true peak), peak-type switch, display modes, oversampling, side-chain trigger |
| Crest Compressor | Pro-C 3 | 8 → 8 | 8 → 8 | wet / dry pan |
| Prism Multiband | Pro-MB | 5 → 7 | 6 → 7 | up to six bands (double-click splits), band pan, external side chain |
| Sentry Gate | Pro-G | 8 → 9 | 8 → 8 | oversampling |
| Hush De-esser | Pro-DS | 7 → 8 | 8 → 8 | oversampling |
| LYNX Tune | Auto-Tune Pro | 7 → 8 | 7 → 7 | Learn scale (MIDI), Classic (low-latency path, same retune — measured) |
| Compressor | Renaissance Compressor | 5 → 8 | 5 → 7 | ARC / Manual, Electro / Opto, Warm / Smooth, 0 dBFS limiter + pill |
| Hush De-esser | DeEsser | 8 → 9 | 5 → 6 | output meter + clip lamp, "Classic split" presets |
| Gemini Doubler | Doubler2 / Doubler4 | 8 / 5 → 9 / 8 | 7 → 7 | voices 3–4, modulation reset, clip lamp |
| Echoform | H-Delay | 7 → 9 | 5 → 7 | own BPM, ping-pong start side, Link EQ, output meter + clip lamp |
| LYNX Tune | Waves Tune Real-Time | 5 → 8 | 5 → 6 | Real-time page: Speed + Link, Tolerance cents / time, Vibrato, Correction, meters |
| Echoform | ValhallaDelay | 7 → 8 | 7 → 7 | Quad taps A–D, Repeat / Swell, description line |
| Echoform | EchoBoy / PrimalTap | 5 → 7 | 5 → 6 | Groove / Feel (measured), Accent, Prime numbers, cross feedback / balance, Freeze |
| Epoch Verb | ValhallaVintageVerb | 9 → 9 | 8 → 9 | description line, MIX-label lock, original preset names |
| Atrium | ValhallaRoom | 9 → 9 | 8 → 8 | (same) |
| Foil Plate | ValhallaPlate | 9 → 9 | 9 → 10 | (same) |
| Nebula | Supermassive | 9 → 9 | 9 → 9 | (same) |
| Halo | ValhallaShimmer | 7 → 8 | 8 → 9 | the four reference modes (Mono / small / medium / big stereo) |
| Horizon | ValhallaFutureVerb | 7 → 8 | 7 → 9 | mode-dependent controls |
| Orbit | ValhallaSpaceModulator | 8 → 8 | 7 → 9 | description line |
| Lattice Mod | ValhallaUberMod | 7 → 7 | 7 → 9 | the nine named modes |
| Ember Saturator | Decapitator | 5 → 8 | 4 → 7 | Punish, Thump, Steep, five one-click voicings, wide cuts, 0–10 drive, IN / OUT meters, her states translated |
| Auto-pan | PanMan | 3 → 7 | 2 → 6 | free LFO 0.01–10 Hz, rhythm steps, direction, smoothing, in / out, analog stage, pan-position strip, her states |
| Foil Plate | Little Plate | 6 → 8 | 5 → 6 | Infinite, low cut 20 Hz–1 kHz |
| Voice Shift (new) | Little AlterBoy | 0 → 9 | 0 → 8 | new engine: Pitch / Formant ±12 st, Link, Transpose / Quantize / Robot, Drive, Mix; her 52 states translated |

Shared by every LYNX engine editor (round 135): the per-control description line at the bottom of the window
(Valhalla's strip; ~330 sentences, pt-BR), the mix lock by clicking the MIX caption, and the name of the original's
preset (Valhalla / Waves states) instead of "(no preset)".

---

# FabFilter / Slate / Antares roles (round 128)

| LYNX engine | Original (trial here) | Config before | Design before | Config after | Design after | Config 135 | Design 135 |
|---|---|---|---|---|---|---|---|
| Facet EQ | Pro-Q 4 (v4.1.3) | 6 | 8 | 8 | 8 | 9 | 8 |
| Apex Limiter | Pro-L 2 (v2.2.6) | 4 | 7 | 7 | 8 | 9 | 9 |
| Crest Compressor | Pro-C 3 (v3.0.2) | 6 | 7 | 8 | 8 | 8 | 8 |
| Prism Multiband | Pro-MB (v1.3.3) | 4 | 6 | 5 | 6 | 7 | 7 |
| Sentry Gate | Pro-G (v1.4.2) | 6 | 7 | 8 | 8 | 9 | 8 |
| Hush De-esser | Pro-DS (v1.3.2) | 4 | 6 | 7 | 8 | 8 | 8 |
| Ember Saturator | Saturn 2 (v2.1.3); Decapitator role (not installed) | 3 | 6 | 6 | 7 | 6 | 7 |
| Aether | Fresh Air (v1.1.7) | 8 | 8 | 9 | 8 | 9 | 8 |
| LYNX Tune | Auto-Tune Pro (v11.6, Auto mode) | 5 | 6 | 7 | 7 | 8 | 7 |

Columns "135" = round 135 (parity135f, `docs/analise/REA_rodada135f.md`): Apex's loudness meter, true / sample peak
metering, display modes, oversampling and side-chain triggering; oversampling in Sentry and Hush; Prism up to six
bands (double-click splits a band), band pan, external side chain; Crest's wet / dry pan; Facet's Flat Tilt / All
Pass shapes and Character; LYNX Tune's Learn (scale from MIDI) and Classic mode.

After = round 128 (per-plugin tables below and `docs/analise/REA_rodada128.md`). The shared header (undo / redo, A/B,
presets) is round 129's PresetBar, reused here (no second system); the scores include it.

## Shared by every editor (the frame around the controls)

All five FabFilter originals share one header and one bottom bar; Fresh Air and Auto-Tune have their own versions of
the same things. This is where most of the "missing details" were: things a user touches before any knob.

| Original feature | Where in the originals | LYNX before | Note |
|---|---|---|---|
| Undo / Redo buttons | header (FabFilter, Fresh Air, Auto-Tune) | ✅ | round 129's shared preset bar (per-plugin undo / redo), wired into every editor incl. Facet (round 128) |
| A/B compare + Copy (A→B) | header (FabFilter: A/B + Copy; Fresh Air: A > B) | ✅ | round 129's preset bar, saved with the project |
| Preset browser: name, previous / next, menu, save | header (all three) | ✅ | round 129's preset bar (factory + user presets, save / delete); round 128 added the factory presets of Facet, Apex, Crest, Sentry, Ember, Aether |
| Help | header (FabFilter "Help", Auto-Tune ⚙ / manual) | ⚠️ | the header shows a one-line gesture hint; no help menu |
| Resizable / full-screen window | FabFilter ⤢ corner + full-screen button, Fresh Air corner | ⚠️ | round 128: every display-first editor window resizes (70–200 %, scaled); Facet resizes with its own layout; no full-screen button |
| MIDI Learn | FabFilter bottom bar | ⚠️ | per control: right-click → "Link to controller" (LYNX's automation menu); no global learn mode |
| Right-click any control: type value / reset / automate | FabFilter: right-click menu | ✅ | right-click → Type value, Reset, automation / controller link |
| Double-click / Alt-click = reset, Ctrl-drag = fine, wheel | FabFilter | ✅ | same contract |
| Hover read-out of a value | FabFilter | ✅ | value tip while dragging / wheel; hint bar |
| Input / output level and pan (bottom bar "0.0 dB 0.0 dB") | FabFilter bottom bar | ⚠️ | in / out levels on some engines (see each table); no pan |
| Global bypass | FabFilter bottom bar (⏻), Fresh Air POWER, Auto-Tune ⏻ | ✅ | the slot's power LED in the header (Aether: the POWER button) |
| Oversampling menu | bottom bar | ⚠️ | per engine (see tables) |

## Facet EQ ← Pro-Q 4

Parameters of the original: 24 bands × (used, enabled, frequency 10 Hz–30 kHz, gain ±30 dB, Q 0.025–40, shape
(Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt, All Pass), slope 0–96
dB/oct + Brickwall, stereo placement (Left, Right, Stereo, Mid, Side), dynamic range, dynamics on / auto, threshold,
attack, release, external side chain, side-chain filtering, spectral mode, density, tilt, solo) + processing mode,
resolution, character, gain scale, output level / pan / pan mode, phase invert, auto gain, analyser options, display
range.

| Original feature | LYNX | Note |
|---|---|---|
| Full-window spectrum + EQ curve, nodes on the curve | ✅ | |
| Double-click empty space = new band; drag = frequency / gain; wheel = Q | ✅ | |
| Bands | ⚠️ | 8 bands (original: 24) |
| Shapes | ✅ | Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt Shelf; round 135: Flat Tilt (a constant slope over ±5 octaves) and All Pass (phase only), translated from Pro-Q 4 states (were a tilt shelf / dropped) |
| Slopes | ⚠️ | 6–96 dB/oct; no 0 dB/oct, no Brickwall |
| Floating band panel: shape, slope, FREQ, GAIN, Q, band ◀ n ▶, close | ✅ | |
| Band panel: power button (bypass one band) | ✅ | round 128: the band's coloured power button (also in the node tip and the band menu); a band switched off keeps its settings and shows a hollow node |
| Band panel: stereo placement (L / R / Stereo / M / S) | ✅ | round 128: placement button in the panel and the band menu, an L / R / M / S tag beside the node; in the natural / linear phase modes a placed band runs minimum phase after the FIR |
| Dynamic EQ: range, threshold, attack, release | ⚠️ | attack / release are global (original: per band); no "auto" threshold |
| Dynamic EQ: external side chain per band, side-chain filtering | ⚠️ | one detector switch (input / side chain) for all bands |
| Spectral dynamics (density, tilt) | ✅ | **round 152c** [EXEC] (Pro-Q 4 trial, black-box: Spectral / Density / Tilt host parameters, partials and pink noise through a dynamic bell): **Spectral** in the band panel (beside Dynamic; right-click: density 0 … 100 %) and **Tilt** — an STFT stage (4096 points, latency 5120 samples = the trial's): each bin's level weighted by the band's shape, a threshold 30.5 dB under the knob and never lower than the band's loudest bin − 11.6 dB, 1.07 dB per dB up to the range × shape^2.18 (0.33 dB rms over 22 measured cases); density blurs the gains (100 %: a pure tone −11.2 dB, as the trial), tilt slopes the threshold 4.5 dB/oct [model] |
| Solo band (audition) | ✅ | |
| Hover tooltip next to a node (freq, gain, Q, power, solo) | ✅ | read-out box at the node with power (round 128), solo and remove |
| Right-click a node: shape, slope, dynamic range, solo, reset, remove | ✅ | |
| Processing: Zero Latency / Natural Phase / Linear Phase | ✅ | |
| Linear-phase resolution | ✅ | round 144: Low / Medium (default) / High / Very High / Maximum in the Processing menu (with each latency) and in the bottom bar; the FIR is 2 × 2048 … 65536 taps in blocks of 1024, so the latencies are the trial's exactly; the buffers are sized per instance (choosing another resolution builds a new instance); translated from Pro-Q 4 states (value 553, and the character at 554). Was: fixed (LYNX: 4608 samples at 48 kHz). Round 137 [EXEC] the trial's latencies at 48 kHz: natural phase 320 (LYNX 320 ✅); linear phase Low 3072 / Medium 5120 (default) / High 9216 / Very High 17408 / Maximum 66560 = 1024 + 2^11 … 2^16 (no 2^15) — an FIR of 2× that half-length; needs per-instance buffers sized by the resolution |
| Character (Clean / Subtle / Warm) | ✅ | round 135: in the Processing menu (shown after the mode in the bottom bar); LYNX's own soft-knee curves on the output, unity below about −20 dBFS (the reference's law is not measured) |
| Gain scale | ✅ | |
| Output level | ✅ | |
| Output pan / pan mode, phase invert | ✅ | round 128: PAN in the bottom bar (left / right or mid / side), polarity in OUT's right-click menu |
| Auto gain | ✅ | round 128: bottom-bar button; takes back the mean level change of the static curve |
| Analyser: off / pre / post / pre+post | ✅ | |
| Analyser range, resolution, speed, tilt, freeze, collisions, spectrum grab | ⚠️ | round 128: range 60 / 90 / 120 dB, speed (5 steps), tilt 0 – 6 dB/oct, freeze in the analyser menu (saved with the slot); no resolution, collisions or spectrum grab |
| Display range 3 / 6 / 12 / 30 dB | ✅ | saved with the slot since round 128 |
| EQ match (external spectrum) | ⚠️ | **round 152c**: **EQ match** in the bottom bar — capture a reference (played through the slot) and this track from the analyser, then Match 25 … 100 % with up to 2 … 8 bands and 1/12 … 1 octave smoothing: LYNX's own fit (bells, shelves at the ends, in the unused bands; one undo step) [model]; Pro-Q 4's match is not reachable through host parameters, so its rules are not measured; no live side-chain / other-instance reference |
| Piano display (note names on the frequency axis) | ✅ | piano strip toggle at the bottom left; note name of the mouse position |
| Output meter with peak hold | ✅ | |
| Window size | ✅ | resizable |

**After round 144**: configuration 9/10, design 8/10 (the linear-phase resolution row closed; left: 24 bands, spectral dynamics, EQ match, per-band attack / release).

**After round 152c**: configuration 9/10, design 8/10 — spectral dynamics (Spectral / Density / Tilt, measured per-bin law and latency) and LYNX's EQ match close the two ❌ rows; left: 24 bands, per-band attack / release and side chain, brickwall slopes, a live reference for the match.

## Apex Limiter ← Pro-L 2

Parameters of the original: Gain 0–30 dB, Style (Transparent, Punchy, Dynamic, Allround, Aggressive, Modern, Bus,
Safe), Lookahead 0–5 ms, Attack 0–10 s, Release 0–10 s, Channel Link Transients / Release 0–100 %, Oversampling
Off–32x, True Peak Limiting, Dithering Off / 16–24 bits, Noise Shaping (None, Basic, Optimized, Weighted), Filter DC
Offset, Side Chain Triggering, Unity Gain, Audition Limiting, Output Level −30…0 dBTP, meter scale (−16/−32/−48 dB,
K-12/14/20, Loudness), loudness meters (momentary / short term / integrated, target, recording).

| Original feature | LYNX | Note |
|---|---|---|
| Display = input / output level / gain reduction history, biggest reductions tagged | ✅ | |
| GAIN slider on the display's left edge | ✅ | |
| Output (ceiling) line, draggable | ✅ | |
| Style button / menu | ✅ | 8 styles (round 128: Clean, Glue, Punch, Dynamic, Dense, Loud, Bus, Safe — LYNX's own names and release laws) |
| LOOKAHEAD knob | ✅ | 0–5 ms |
| ATTACK knob | ✅ | round 128: 0–10 s (how slowly the slow release stage engages; 0 = at once, the old behaviour) |
| RELEASE knob | ✅ | |
| CHANNEL LINKING: TRANSIENTS and RELEASE knobs | ✅ | round 128: two knobs; the release is linked at least as much as the transients |
| Knob panel folds away («) | ✅ | |
| ADVANCED panel (»): channel link details, DC offset filter, side-chain triggering | ✅ | round 128: » fold-out with DC offset filter, unity gain, audition limiting; round 135: side-chain triggering |
| True Peak Limiting (bottom bar) | ✅ | TP button + bar menu |
| Oversampling (bottom bar) | ⚠️ | round 135: Off / 2x / 4x / 8x in the bottom bar (the limiter runs at the high rate; latency + the oversampler's); the original goes to 32x |
| Dither + noise shaping (bottom bar) | ✅ | round 128: Off / 16–24 bits, TPDF; None / Basic / Optimized / Weighted error-feedback shaping |
| Output level read-out (bottom bar "Out: 0.0 dBTP") | ✅ | |
| Unity Gain (monitor at the same loudness) | ✅ | round 128 (Advanced) |
| Audition Limiting (hear what is removed) | ✅ | round 128 (Advanced) |
| Meters: output peak bars with holds + gain-reduction meter | ✅ | |
| Meter scale menu (−16/−32/−48 dB, K-12/14/20) | ✅ | round 128: next to TP; rescales the display and the meters |
| Loudness meter (LUFS momentary / short term / integrated, range, true peak) | ✅ | round 135: LOUDNESS button on the display → a panel at the right: short-term and momentary bars with the target line (−23 … −9 LUFS), integrated, short term, momentary, range (LU) and peak read-outs, Reset; ITU-R BS.1770-4 / EBU R 128 (K-weighting, 400 ms / 3 s, gated integration, EBU Tech 3342 range), checked on the EBU test cases |
| True-peak / sample-peak metering switch | ✅ | round 135: in the loudness panel; the output meter and its hold follow it (4x true-peak interpolation) |
| Display mode (Slow Down / Fast / Slow / Infinite / Off) | ✅ | round 135: menu beside the meter scale — Fast (10 ms a pixel), Slow, Slow down (recent fast, the past compressed), Infinite (0.5 s a pixel, ~11 minutes), Off (meters only) |
| External side chain | ✅ | round 135: "Side-chain triggering" (Advanced): the slot's side chain triggers the limiting, the ceiling still holds the programme |

## Crest Compressor ← Pro-C 3

Parameters of the original: Style (Clean, Versatile, Smooth, Punch, Upward, TTM, Op-El, Vari-Mu, Classic, Opto,
Vocal, Mastering, Bus, Pumping), Threshold −60…0 dB + Auto Threshold (+ lock), Ratio 1–100:1, Knee 0–72 dB,
Range 0–60 dB, Attack 0.005–250 ms, Release 10 ms–2.5 s + Auto, Lookahead, Hold, Character (Off, Tube, Diode,
Bright) pre / post + drive, Wet / Dry gain and pan, Auto Gain, side chain (Internal, External, Host Sync, MIDI; level;
host trigger sync / offset / length), stereo link 0–100 % + mode (Mid, Side, M→S, S→M), side-chain EQ (6 bands, 10
shapes), audition side chain / triggering, Mix 0–200 %, input / output level and pan, oversampling Off–32x, maximum
lookahead, meter scale, knee display, input meter.

| Original feature | LYNX | Note |
|---|---|---|
| Full-window level display (input, output, gain-reduction line, threshold) | ✅ | |
| Knee / transfer panel sliding out from the left | ✅ | |
| THRESHOLD, RATIO, style button, ATTACK, RELEASE knobs floating on the display | ✅ | |
| AUTO threshold | ✅ | round 128: the threshold follows the programme (3 s RMS of the detector input; the knob's −18 dB = at the average level) |
| AUTO release | ✅ | |
| KNEE, RANGE, LOOKAHEAD, HOLD small knobs | ✅ | |
| Styles | ⚠️ | 11 (no Smooth, TTM, Op-El, Mastering under those names; the translator maps them onto the nearest fitted family — new names would need their own measured families) |
| Character Off / Tube / Diode / Bright, pre / post, drive | ✅ | |
| AUTO GAIN | ✅ | |
| DRY knob (dry level, parallel compression) | ✅ | round 128: DRY under AUTO GAIN (−∞ … +36 dB, the original's law); MIX moved to the bottom bar (0–100 %, the original goes to 200 %) |
| Wet / dry pan (mid / side balance) | ✅ | round 135: WET PAN / DRY PAN in the side-chain panel (left / right balance of each path) |
| SIDE CHAIN panel: listen, level, stereo link, detector EQ with nodes | ✅ | 3 EQ bands (original: 6) |
| Side chain Internal / External | ✅ | |
| Side chain Host Sync / MIDI triggering | ✅ | round 137: side-chain trigger Host sync (note, rate, length — measured on the trial) and MIDI (row re-checked in round 152) |
| Stereo link mode (Mid / Side / M→S / S→M) | ✅ | round 128: "Link mode" in the side-chain panel (Stereo, Mid, Side, Mid > side, Side > mid); translated from Pro-C 3's mid-only link range + mode |
| Audition triggering | ✅ | round 137 / re-checked round 152: SC LISTEN plays the trigger key while a Host sync / MIDI trigger drives the detector (the detector's signal is the trigger) |
| Oversampling (bottom bar) | ⚠️ | up to 8x (original 32x) |
| Lookahead maximum (bottom bar) | ✅ | |
| In / out levels (bottom bar) | ✅ | no pan |
| Meter scale (9/18/36/72/90 dB) | ✅ | round 128: the dB box at the display's bottom right |
| Input level meter switch | ✅ | round 128: click IN above the meters |
| Output + gain-reduction meters with holds | ✅ | |

## Prism Multiband ← Pro-MB

Parameters of the original: up to 6 freely placed bands × (state, low / high crossover + slope 6–48 dB/oct, dynamics
mode compression / expansion, threshold −90…0, range ±30 dB, ratio, attack %, release %, knee 0–48 dB, lookahead
0–20 ms, level ±30 dB, pan, side-chain filtering band / free + low / high, side-chain input, stereo link + mode,
solo / mute) + mix 0–200 %, input / output, processing mode (Linear / Dynamic / Minimum phase), oversampling,
lookahead on, expert mode, display range, analyser options.

| Original feature | LYNX | Note |
|---|---|---|
| Spectrum window with band regions and nodes | ✅ | |
| Click / drag in empty space = create a band anywhere; bands with gaps | ✅ | round 135: double-click the empty display = a new band there (the band under the mouse splits at that frequency, the new one starts as its copy); right-click → Split / Remove the band. Round 150: **Free layout** (Split \| Free beside the display): each band its own low / high edge and slopes, the room between bands passes untouched (measured: Minimum phase = the crossover tree with the gaps at unity, exact); double-click or right-click in a gap = a new band there; drag an edge or the node; Pro-MB states translate to it |
| Up to 6 bands | ✅ | round 135: 1–6 bands (the band count; 4 = the old engine sample for sample); Pro-MB states translate up to six |
| Node: drag up / down = range, left / right = move band; edges = crossovers | ✅ | |
| Wheel = threshold | ✅ | |
| S / M buttons per band | ✅ | |
| Floating band panel: power, THRESHOLD, RANGE, ATTACK, RELEASE, OUTPUT, mode, RATIO, KNEE | ✅ | |
| LOOKAHEAD per band | ✅ | round 128: LOOKAHEAD in the band panel (0–20 ms, capped by the global look-ahead, which sets the latency); translated from Pro-MB |
| EXPERT (»): side-chain filtering, external side chain, stereo link per band | ✅ | round 135: external side chain (Int / Ext: the detectors hear the side chain split by the same crossovers); round 147: **Expert >>** in the band panel — side-chain filtering Band / Free with Low / High (30 Hz – 30 kHz; the free filter measured: 4th-order sides, −24 dB an octave out), **stereo link** 0–100 % and 100 % + mid- / side-only with its Mid / Side mode (measured laws, within 0.25 dB of the trial); translated from Pro-MB states (only when its Expert mode is on, as the reference does). Round 150: **a side-chain input per band** (Global / Plug-in / External, measured: −10.3 dB on the external band, the other untouched), the free filter's **narrow-band boost** (+13.7 dB at the centre of 2 – 2.5 kHz: a centre bell fitted per width, within 0.1 – 0.3 dB) |
| Band output pan (mid / side) | ✅ | round 135: PAN in the band panel (left / right balance of the band); round 144: L/R \| M/S switch in the band panel (mid / side = the reference's law, measured: the far side's gain 1 − \|pan\|), translated from Pro-MB states |
| Crossover slope per band 6–48 dB/oct | ✅ | round 144: the global slope in the band panel; round 147: **each crossover its own slope** (Global / 12 / 24 / 48: a band's Low / High slope in its expert part); round 150: in the free layout **every slope 6 … 48 dB/oct** with Pro-MB's laws [EXEC]: Linear phase continuous (1 / (1 + x^m), m = slope / 6), Dynamic / Minimum phase quantised as the reference (6, 12, 24, 36, 48), 6 dB/oct as its first-order designs (edge shelf, the bell of a 6 / 6 band, the measured 6 / n table) — within 0.01 – 0.08 dB of the trial; translated |
| Processing mode Linear / Dynamic / Minimum phase | ✅ | round 144: Phase menu in the bottom bar; linear = FIR crossovers with the same magnitudes, 3072 samples at 48 kHz (measured); minimum = all-pass compensated sum; translated. Round 147: **Dynamic phase measured and rebuilt** — each working band is a minimum-phase filter with the linear-phase magnitude, the bands in cascade (Pro-MB's −4.1 dB / +53° at the crossover, −8.15 dB for two adjacent −12 dB bands: matched within 0.05 dB / 1°); projects saved before round 147 keep the classic engine ("Classic dynamic" in the bottom bar updates them) |
| Oversampling | ✅ | round 144: Off / 2x / 4x in the bottom bar: the gains applied at the high rate; latency + the oversampler's; translated |
| Display range 3 / 6 / 12 / 30 dB | ✅ | round 128: 3 dB added |
| Analyser off / pre / post / pre+post | ✅ | |
| Analyser resolution / speed / tilt / freeze | ✅ | round 128: the analyser button opens a menu (off / pre / post / pre+post, speed, freeze); round 144: Resolution (Low … Maximum) and Tilt (0 … 6 dB/oct) |
| Mix, In, Out (bottom bar) | ✅ | |
| Output meter | ✅ | |

**After round 144** (REA_rodada144b): configuration 7 → **9**/10, design 7 → **8**/10 — processing modes, oversampling, mid / side band pan, analyser resolution / tilt, the reference's bottom-bar order. Left: per-band slopes, side-chain filtering, per-band stereo link, the dynamic mode's crossover shape, free band placement.

**After round 147** (REA_rodada147 §1–2): configuration 9 → **10**/10, design 8 → **9**/10 — the measured Dynamic phase (the reference's sound when bands work), the expert panel (side-chain filtering, stereo link, the band's own slopes). Left: free band placement with gaps (a band switched off is the gap), a side-chain input per band, slopes only 12 / 24 / 48 (no 6 or in-between values), the free side-chain filter's extra boost on bands narrower than an octave.

**After round 150** (REA_rodada150): configuration **10**/10, design 9 → **10**/10 — the free layout with gaps (bands placed anywhere, as the reference), slopes 6 … 48 dB/oct with the reference's laws per processing mode, a side-chain input per band, the narrow free side-chain filter, and the **measured detector** (the range's 12 dB soft knee, the release after the curve: a range-limited band recovers at once; release trajectories within 0.1 – 0.7 dB of the trial where the old detector was 1 – 4 dB off; projects saved before keep the old one). Left: the slow attacks' tail (50 – 100 %: 0.5 – 1.3 dB rms), narrow even-order bands in Dynamic phase (0.3 dB at their edges), Linear phase on LYNX's prewarped axis (≤ 0.35 dB at high crossovers).

**After round 152** (REA_rodada152 §1–2): configuration **10**/10, design **10**/10 (sound closer) — the attack in the gain stage (slow attacks 50 – 100 % now within 0.4 dB of the reference over the first 400 ms; round 150 was up to 3 dB off), Dynamic phase's own band shapes for narrow bands and cuts (edges moved out, gain moved — the measured table, ≤ 0.05 dB), the 0.8-octave minimum band width in every mode. Left: Linear phase on LYNX's prewarped axis (≤ 0.35 dB at a 3 kHz edge, 6 dB/oct), mixed 6 / n sides beyond the measured table.

## Sentry Gate ← Pro-G

Parameters of the original: Threshold, Ratio, Range, Style (Classic, Clean, Vocal, Guitar, Upward; Ducking as an
expert style), Attack 0–1 s, Release 0–5 s, Hold 0–250 ms, Knee 0–30 dB, Lookahead 0–10 ms + on, upward threshold /
ratio, side chain (normal / external, per-channel gain and L/R mix, high / low pass, audition), wet / dry level +
pan, oversampling Off / 2x / 4x, expert mode, channel mode L/R or M/S, input / output.

| Original feature | LYNX | Note |
|---|---|---|
| THRESHOLD big knob with RATIO / RANGE under it | ✅ | |
| Display: level history behind the transfer curve, threshold dashed | ✅ | |
| Style button (5 styles) | ✅ | |
| Ducking style | ✅ | round 128: DUCKING in the expert strip (the gate upside down: an open gate cuts by the range); shown under the style button when on |
| ATTACK, RELEASE, HOLD, KNEE, LOOKAHEAD (+ on) | ✅ | |
| EXPERT strip: side chain In / Ext, high / low-pass range slider, Audition | ✅ | |
| EXPERT: side-chain GAIN + L/R MIX (two channels) | ✅ | round 128: GAIN / MIX per detector channel (L / R, or M / S); translated from Pro-G |
| EXPERT: WET + PAN, DRY + PAN | ✅ | round 128: WET PAN / DRY PAN next to DRY / OUTPUT; translated |
| Channel mode Left/Right or Mid/Side (bottom bar) | ✅ | round 128: bottom bar; the detector channels become mid / side; translated |
| Oversampling (bottom bar) | ✅ | round 135: Off / 2x / 4x / 8x (the gain is applied at the high rate; latency + the oversampler's); the look-ahead switch stays under its knob |
| In / Out levels (bottom bar) | ✅ | round 128: In added (−24 … +24 dB); translated |
| Level meter | ✅ | |

## Hush De-esser ← Pro-DS

Parameters of the original: Mode (Single Vocal, Allround), Threshold −∞…0 dB, Range 0–24 dB, Band Processing (Wide
/ Split), Stereo Link 0–100 % + Mid / Side, Lookahead 0–15 ms + on, Audition Triggering, side chain normal /
external, High-pass frequency 2–20 kHz, Low-pass frequency 2–20 kHz, Audition side chain, oversampling, input /
output level and pan.

| Original feature | LYNX | Note |
|---|---|---|
| Scrolling waveform with the removed part, threshold lines | ✅ | |
| THRESHOLD and RANGE big knobs, listen (headphones) button | ✅ | |
| Frequency slider with TWO handles (high-pass and low-pass of the detection band) | ✅ | round 128: left = where the band starts, right = where it ends (far right = off), 2–20 kHz; round 165x [EXEC]: the high-pass reaches 20 kHz too (the trial's 2000 · 10^v Hz; Hush's was 2 … 16 kHz), translated one to one |
| Audition (hear the detection band) | ✅ | |
| MODE: SINGLE VOCAL / ALLROUND | ✅ | round 128 (Allround: the sibilance must also stand out from the full-band level); LYNX's high-pass / band detection stays in the bottom bar |
| WIDE BAND / SPLIT BAND | ✅ | |
| SIDE CHAIN In / Ext | ✅ | round 128: Ext picks the source track (menu) |
| STEREO LINK knob with MID switch | ✅ | round 128; round 165x [EXEC]: the knob's upper half (100 % + n % Mid-only / Side-only): appended **Link only** and **Link only side** (Mid only \| Side only), the trial's blend (k < 100 %: each side detects on (1 − k/2) own + (k/2) other; above: the mid / side detection and output blend); link matrices within 0.014 of the trial (mid and side modes); translated |
| LOOKAHEAD knob 0–15 ms with power button | ✅ | round 128 (the look-ahead is the latency; the mixer re-plans its delay compensation) |
| DISPLAY button (hide / show the waveform) | ✅ | round 128: the window shrinks without it |
| Oversampling (bottom bar) | ✅ | round 135: Off / 2x / 4x / 8x for Hush's own voicing (the band and its gain at the high rate; the measured classic voicing has none) |
| In / Out levels (bottom bar) | ✅ | round 128: In ±12 dB, Out ±12 dB; [EXEC] round 165w: the 17.1 trial's run −∞ … +36 dB (36 dB per unit above −18); a state's levels are now read right (stored as 2 v − 1; +7.2 dB was read as +0.2) and the input translated; round 165x [EXEC]: the trial's law (72 v − 36 dB above v = 0.2, 40 log10 v + 6.36 below, 0 = −∞) with **Range law** Extended (new instances; Old ranges for older projects), both levels translated one to one |
| In / Out pan (bottom bar) | ✅ | round 165w [EXEC]: **In pan / Out pan** — the trial's balance law (the far channel × (1 − \|pan\|), the near one untouched; in before the detector, out after the output level), translated from Pro-DS states; oversampling translated too |
| Release | — | Pro-DS has no release control; LYNX keeps RELEASE as an extra (bottom bar) |
| Input meter with threshold arrow, reduction meter | ✅ | |

**Round 165w** (REA_rodada165w §5): configuration 8 → **9**, design 8 → 8 — In pan / Out pan (measured balance), the Pro-DS state's levels read correctly, input level / pans / oversampling translated. Left: the −∞ … +36 dB level range, the high-pass up to 20 kHz (Hush: 16 kHz), Stereo Link above 100 % (the mid-only blend) not translated.

**Round 165x** (REA_rodada165x §3): configuration 9 → **10**, design 8 → **9** — the measured ranges (levels −∞ … +36 dB, high-pass to 20 kHz) behind an appended **Range law** (Old ranges | Extended: older projects unchanged), Stereo Link's upper half (appended **Link only** / **Link only side**, the measured mid- and side-only blends, matrices within 0.014), all translated from Pro-DS states; the STEREO LINK knob runs 0 … 200 % as the trial's ("100 %, 40 % mid only"; one knob over Stereo link and Link only, Mid | Side under it). Left (design): the waveform's look, the knob's printed scale.

## Ember Saturator ← Saturn 2 (and Decapitator's role)

Parameters of Saturn 2: up to 6 bands × (style — 28: Subtle / Clean / Warm / Broken Tube, Subtle / Clean / Warm /
Old Tape, 9 amps, Subtle / Gentle / Heavy Saturation, 3 transformers, Smudge, Breakdown, Foldback, Rectify, Destroy —,
drive + drive pan, feedback amount + frequency, dynamics −1…+1, tone Bass / Mid / Treble / Presence ±24 dB, mix,
level, pan, enabled, solo / mute, crossover + slope 6–48 dB/oct) + global input / output gain and pan, mix, channel
mode L/R or M/S, processing mode (minimum / linear phase), high-quality mode, unrestricted feedback, and the
modulation system (6 XY controllers, 6 XLFOs with 16 steps, 6 envelope generators, 4 envelope followers, 10 MIDI
sources, 50 modulation slots).

| Original feature | LYNX | Note |
|---|---|---|
| Spectrum with bands side by side, split by clicking / double-clicking | ✅ | |
| Drag a band's handle = drive, drag a crossover | ✅ | |
| Up to 6 bands | ✅ | round 150b: six (the Bands button in the bottom bar: 1 … 6; double-click splits up to six; crossovers 4 / 5 appended); translated from Saturn 2 states with 5 / 6 bands |
| Floating band panel: power, style button, MIX, FEEDBACK + FREQ, DYNAMICS, big DRIVE, TONE faders, LEVEL | ✅ | round 128: the same order — power / solo / mute, style, MIX, FEEDBACK, FREQ, DYNAMICS, DRIVE, BASS / MID / TREBLE / PRESENCE faders, LEVEL |
| 28 styles in 6 families | ✅ | round 152b: Breakdown measured black-box — a pitch drop of 24 semitones × drive at every level and frequency, its level curve refitted (0.02 – 0.37 dB rms over seven levels): LYNX's "Collapse" now pitches down the same way (projects saved before keep the static stand-in); the amps' level "memory" re-measured at 2 ms resolution: it settles within 20 ms (≤ 1.5 dB after 10 ms, none from 50 ms), so nothing to model; Smudge (a smeared, delayed, modulated copy) and Destroy (a level-dependent quantiser) stay static stand-ins. Round 150b: a per-band **Character** menu with 28 LYNX-named voicings measured black-box on the trial's 28 styles (static curve, its drive law over 0–100 %, level, and the tone: 0–1.2 dB rms on the level curves and 0–1.4 dB rms on the tone for the 23 saturation / amp / transformer styles); the 5 effect styles: Foldback ≈ a sine fold (fit ≤ 1.3 dB to 60 % drive), Smudge / Destroy level-only, Breakdown / Rectify not static in the reference (LYNX: a gentle saturation / a full-wave rectifier); translated per band from Saturn 2 states. The 4-family Style menu stays (Character Off) |
| Per-band tone: Bass / Mid / Treble / Presence | ✅ | round 128: ±18 dB (Saturn ±24) on the band's saturated signal — LYNX's own curves (shelf 120 Hz, bell 800 Hz, shelf 3.5 kHz, bell 6 kHz); the global tilt / cuts stay as LYNX extras in the bottom bar |
| Per-band power, solo, mute | ✅ | round 128 (panel buttons, tags on the display) |
| Drive pan (L / R), band pan | ✅ | round 147b: per band DRIVE PAN (beside the drive) and PAN (under the level) [EXEC trial: the far channel's drive × (1 − \|pan\|) on the 0–100 % scale; the band pan's far side 1 − \|pan\|]; translated from Saturn 2 states |
| Feedback amount / frequency | ✅ | round 128: a band-passed loop around the shaper (10 Hz – 1 kHz, loop gain up to 0.9) |
| Dynamics (compress / expand the band) | ✅ | round 128: −1 … +1, the band's level follows its input level |
| Modulation (+ Add Modulation: XLFO, EG, EF, MIDI, XY, slots) | ⚠️ | round 152b: a **Mod** page in the band panel — two LFOs (rate 0.02 – 500 Hz with the reference's knob law; Sine / Triangle / Square / Ease in / Ease out = its two-step XLFO with the four glide functions or none), an envelope follower (attack 1 ms – 1 s, release 50 ms – 5 s, the reference's laws; mean-square follower fitted to 0.018 source units rms) and four slots source → target × amount (27 targets: in / out gain, mix, each band's drive / level / mix, the five crossovers; the reference's level law and target ranges) [EXEC trial]; translated (XLFO 1 / 2, EF 1, the first four usable slots). Not: the 16-step editor, EGs, XY, MIDI sources, 50 slots |
| Crossover slope (bottom bar) | ✅ | global; round 152b: each crossover its own slope (6 / 12 / 24 / 36 / 48 dB/oct or Global) in the band panel [EXEC trial: minimum phase = the first-order pair, LR2, LR4, LR6, LR8 — within 0.05 dB]; translated per band |
| Linear phase / minimum phase | ✅ | round 152b: "Linear phase" in the band panel [EXEC trial: zero-phase bands 1 / (1 + (f/fc)^(slope/6)) on the analog axis, the bands sum to the delayed input; latency 3072 samples at 48 kHz] — LYNX's oversampler adds 55 samples; translated (state slot 696) |
| High Quality mode (oversampling) | ✅ | "Oversampling 2x / 4x / 8x" |
| Channel mode M/S | ✅ | round 147b: "Channel L/R \| M/S" in the bottom bar [EXEC trial: M = (L + R) / 2, S = (R − L) / 2 through the bands' two channels; drive pan and band pan then balance mid / side]; translated (state slot 695) |
| In / Out gain + mix | ✅ | round 128: In (±24 dB) in the bottom bar; Out; Dry / wet |
| Input / output pan | ✅ | round 150b: In pan / Out pan in the strip [EXEC trial: the far side × (1 − \|pan\|); input pan before the drive, output pan after the mix; with M/S they act on mid / side]; translated (state slots 1 / 3) |
| Auto gain | — | LYNX extra (Saturn has none; Decapitator has Auto) |
| Decapitator: Drive, 5 styles (A, E, N, T, P), Low Cut, Tone, High Cut, Thump, Steep, Mix, Output, Auto, Punish | ✅ | see "Ember Saturator ← Decapitator (Soundtoys)" below (the Classic voicing, measured on the trial since round 135 / 136; round 165: Punish's law, Auto, the one-screen panel) |

**Round 147b:** configuration 6 → 7 (channel mode M/S, drive pan, band pan, measured on the trial and translated), design 7 → 7. Remaining: 6 bands, 28 styles, modulation, linear phase.

**Round 150b (after 150):** configuration 7 → 8 (six bands, the 28 measured characters, input / output pan, all translated), design 7 → 8 (Character menu in the band panel, Bands 1 … 6, In / Out pan in the strip). Remaining: modulation, linear phase, per-band crossover slopes, the effect styles' time-varying behaviour.

**Round 152b (after 152):** configuration 8 → 9 (each crossover's slope, linear phase, a modulation subset with the reference's laws, Breakdown's pitch drop; all translated), design 8 → 8 (the Mod page and the slope / linear-phase controls in the band panel; Saturn's modulation is drawn as draggable sources and slot cables — LYNX lists them). Remaining: the step-LFO editor (16 steps), EGs / XY / MIDI sources, Smudge / Destroy, the High Quality mode's latencies (Saturn: 0 / 8 / 9 samples; LYNX always oversamples).

## Aether ← Fresh Air

Parameters of the original: Mid Air 0–100 %, High Air 0–100 %, Bypass, Trim −12…0 dB.

| Original feature | LYNX | Note |
|---|---|---|
| Arc output meter on top | ✅ | |
| MID AIR and HIGH AIR big knobs | ✅ | |
| Link button between them | ✅ | |
| POWER (bypass) bottom left | ✅ | |
| TRIM small knob bottom right | ✅ | |
| Preset menu, save, undo / redo, A > B | ✅ | the shared preset bar (round 129) + factory presets (round 128) |
| Extra settings (exciter / shelf frequency, character, mix) | — | LYNX extras in the bottom bar |

## LYNX Tune ← Auto-Tune Pro (Auto mode)

Auto-mode parameters of the original: Correction mode, Scale (29 legacy + 15 modern), Key, Detune ±100 cents, Retune
Speed 0–400 ms, Humanize, Flex-Tune, Natural Vibrato ±12 dB, Tracking, Input Type (Soprano, Alto-Tenor, Low Male,
Instrument, Bass Inst.), Classic mode, Formant Correction + Throat Length 50–180 %, Transpose ±12 (+ in scale),
Wet-Dry Mix, Create Vibrato (shape, rate, pitch, amplitude, formant, variation, onset delay / rate), Ignore Vibrato,
Learn Scale from MIDI, Target Notes via MIDI, MIDI octaves, Latency Removal, per-note Bypass / Remove, keyboard
edits (Remove / Bypass, Latched / Momentary), Hold, Auto Key.

| Original feature | LYNX | Note |
|---|---|---|
| Top strip: Tracking, Input Type, Key, Scale, Formant, Transpose, Detune, Mix | ✅ | Detune shown as the A4 frequency (440 Hz ± 100 c) |
| Retune Speed, Flex-Tune, Humanize big knobs | ✅ | |
| Natural Vibrato knob (±12 dB) | ✅ | round 128: right of the ring, as in the original |
| Centre: correction ring with the note name and cents | ✅ | |
| Hold (freeze the read-out) | ✅ | round 128: click the ring (HOLD lights) |
| Scales | ⚠️ | 16: the modern list (chromatic, major, minor, harmonic / melodic minor, modes, pentatonics, blues, whole tone, diminished) + custom; no historical tunings (Pythagorean, just, 19/24/31/53-tone …) |
| Auto Key (detect the key) | ✅ | round 128: button under Scale; key + major / minor from what was sung while the editor is open (pitch-class time, Krumhansl–Kessler profiles) |
| Learn (scale from MIDI) | ✅ | round 135 (the original's "Learn Scale from MIDI" switch, read from its parameter list): LEARN SCALE (correction section, top right): while on, the notes played on the MIDI channel build the custom scale (switching it on starts a new one) and are not used as targets |
| Keyboard at the bottom: click a note = remove / bypass, latch | ✅ | round 128: four-octave keyboard with BYPASS / REMOVE modes and LATCH; a key edits its note name in every octave (no single-octave edits) |
| Per-note Bypass (pass uncorrected) vs Remove | ✅ | round 128 (translated from the state's legacy scale edits) |
| Classic mode | ✅ | round 135: CLASSIC (correction section, top left): the short-latency path with the same retune. [EXEC] Auto-Tune Pro hosted black-box: "Use Classic Mode DSP" leaves the retune trajectory unchanged (452 Hz at 150 ms: the same pitch at every point to 0.1 Hz) and drops the latency from 2670 to 37 samples; LYNX's classic = its low-latency mode |
| Formant Correction on / off | ✅ | |
| Throat Length | ⚠️ | "Formant shift" in semitones instead of a throat length % |
| Transpose ±12 | ✅ | |
| Mix | ✅ | |
| ADVANCED: Create Vibrato (shape, rate, depth, onset), Ignore Vibrato, scale detune | ⚠️ | round 128: ADVANCED page with Create Vibrato (shape, rate, depth, onset delay / rate) and Ignore Vibrato, translated; no amplitude / formant / variation of the created vibrato, no per-note scale detune |
| Target notes via MIDI | ✅ | "MIDI channel" |
| Low latency | ✅ | |
| Sound: pitch accuracy, timbre (highs, formants), bursts | ⚠️ | round 165j [RUN] on her takes: tighter pitch than Auto-Tune (median 4.4 vs 5.8 c), highs now within 0.1 dB of Auto-Tune (normalised grain overlap-add + 3 kHz shelf; was 0.45 dB darker), formant distance 0.78 vs 0.49 dB, short bursts 40 vs 31 / min (note changes / onsets) |
| Pitch graph | ✅ | LYNX extra in Auto mode (input vs corrected) |
| Graph mode (offline editing) | — | out of scope (a separate editor) |
| Harmony Player | — | out of scope |
| Preset browser, undo / redo | ✅ | the shared preset bar (round 129) with LYNX Tune's factory presets |
| Breathiness | — | LYNX extra (round 190bb, issue #103; not a control of the original): BREATH beside SHIFT and THROAT in the output section, 0 … 100 %, off by default (the stage is skipped: old projects render bit-identically). Aspiration noise with the corrected voice's own spectral envelope (third-octave channel-vocoder bank, +6 dB / octave tilt around 2 kHz), on voiced sounds only, never louder than the voice. Measured (REA_rodada190bb-tune §3): at the formant peaks the noise sits within 3.5 dB of the voice's level times the tilt; valleys next to a strong formant fill by up to 16 dB (third-octave resolution); about +0.6 % of a core at 44.1 kHz while on (harness figure), nothing while off |

---

# Waves / SSL / Valhalla / Soundtoys roles (round 129)

Trials: Waves (WaveShell 17.1, demo mode) and Valhalla DSP (demos) hosted here; the reference screenshots are from
rounds 100 / 101 / 113 / 119 and this round (outside git: `D:\lynx-work\parity129\ref`, `D:\lynx-test-archive`).
The Soundtoys 5 trials were installed during this round but every one of them stops on its iLok activation window
until the user starts the trial, so their tables were from the official manuals ("manual only"). Round 135: the user
activated them; the Decapitator, PanMan, Little Plate and Little AlterBoy tables are now from the trials (black-box).
Late in the round (after the user activated the Waves trials) the Waves shell loaded again and RComp, DeEsser,
Doubler4, Waves Tune Real-Time, SSL E-Channel, H-Delay and Doubler2 were captured; the tables above agree with those
captures (RComp: Thresh / Gain faders on the input / output meters, ARC · Electro · Warm, Mix, Trim — ARC / Electro /
Warm still missing in LYNX's Compressor; Waves Tune RT: Speed / Note Transition / Tolerance / Vibrato / Correction and the
range buttons — see its table). Our host still shows their demo banner.

| LYNX engine | Original (trial here) | Config before | Design before | Config after | Design after |
|---|---|---|---|---|---|
| Glow Leveler | CLA-2A (Waves) | 5 | 6 | 6 (ref expired) | 8 | 7 | 7 |
| Ridge Strip | SSL E-Channel (Waves) | 6 | 6 | 7 (ref expired) | 8 | 7 | 7 |
| Compressor (stand-in) | Renaissance Compressor (Waves) | 2 | 1 | 6 (ref expired) | 8 | 5 | 5 |
| Hush De-esser | DeEsser (Waves) | 7 | 5 | 7 (ref expired) | 6 | 8 | 5 |
| Quell Denoiser | NS1 (Waves) | 8 | 8 | 4 (ref expired) | 6 | 9 | 8 |
| Gemini Doubler | Doubler2 / Doubler4 (Waves) | 7 / 4 | 6 | 6 (ref expired) | 8 | 8 / 5 | 7 |
| Echoform | H-Delay (Waves) | 5 | 4 | 7 (ref expired) | 6 | 7 | 5 |
| LYNX Tune | Waves Tune Real-Time | 4 | 5 | 6 (ref expired) | 8 | 5 | 5 |
| Epoch Verb | ValhallaVintageVerb | 7 | 8 | 5 | 8 | 9 | 8 → 9 (r135) |
| Atrium | ValhallaRoom | 7 | 6 | 4 | 6 | 9 | 8 |
| Foil Plate | ValhallaPlate | 8 | 9 | 3 | 8 | 9 | 9 → 10 (r135) |
| Nebula | Valhalla Supermassive | 7 | 8 | 3 | 8 | 9 | 9 |
| Halo | ValhallaShimmer | 6 | 8 | 3 | 8 | 7 → 8 (r135) | 8 → 9 (r135) |
| Horizon | ValhallaFutureVerb | 6 | 7 | 2 | 8 | 7 → 8 (r135) | 7 → 9 (r135) |
| Echoform | ValhallaDelay | 6 | 6 | 4 | 6 | 7 | 7 |
| Spiral Echo | ValhallaFreqEcho | 9 | 9 | 7 | 8 | 9 | 9 |
| Orbit | ValhallaSpaceModulator | 7 | 7 | 3 | 8 | 8 | 7 → 9 (r135) |
| Lattice Mod | ValhallaUberMod | 6 | 7 | 4 | 8 | 7 | 7 → 9 (r135) |
| Ember Saturator | Decapitator (Soundtoys; trial measured in round 135) | 4 | 4 | 5 → **8** (r135) | 4 → **7** (r135) |
| Fruity Delay 3 (stand-in) | EchoBoy / PrimalTap (Soundtoys; trial measured in round 135) | 2 | 2 | 5 → **7** (Echoform, r135) | 5 → **6** (Echoform, r135) |
| Auto-pan (stand-in) | PanMan (Soundtoys; trial measured in round 135) | 2 | 2 | 3 → **7** (r135) | 2 → **6** (r135) |
| Reeverb 2 (stand-in) | Little Plate (Soundtoys; trial measured in round 135) | 4 | 3 | 6 → **8** (Foil Plate, r135) | 5 → **6** (Foil Plate, r135) |
| Voice Shift (round 135) | Little AlterBoy (Soundtoys; trial measured in round 135) | 0 | 0 | 0 → **9** (r135) | 0 → **8** (r135) |

Scores "after" = with round 129's changes (below). The biggest single gap was the frame every original has around its
controls — undo / redo, A / B, presets — which none of LYNX's engines had; it is now in every LYNX engine editor.

**Round 135w (Waves roles + Echoform; `docs/analise/REA_rodada135w.md`)** — config / design after round 135w:

| LYNX engine | Original | Config 129 → 135w | Design 129 → 135w | What changed |
|---|---|---|---|---|
| Compressor (stand-in) | Renaissance Compressor | 5 → 8 | 5 → 7 | ARC / Manual, Electro / Opto, Warm / Smooth, output limiter + pill, all translated |
| Hush De-esser | DeEsser | 8 → 9 | 5 → 6 | output meter with clip lamp / peak hold, the reference's five frequencies as LYNX presets |
| Gemini Doubler | Doubler2 / Doubler4 | 8 / 5 → 9 / 8 | 7 → 7 | voices 3–4 (page), modulation RESET, clip lamp |
| Echoform | H-Delay | 7 → 9 | 5 → 7 | own BPM sync, ping-pong start side, filter link, output meter + clip lamp |
| LYNX Tune | Waves Tune Real-Time | 5 → 8 | 5 → 6 | "Real-time" page: Speed + Link, tolerance cents / time, vibrato on / depth, correction %, IN / OUT meters |
| Echoform | ValhallaDelay | 7 → 8 | 7 → 7 | Quad taps A–D, Repeat / Swell |
| Echoform | EchoBoy / PrimalTap | 5 → 7 | 5 → 6 | Groove / Feel (measured on the trial), Accent, Prime numbers, X-feed, Balance, Freeze, own BPM |

## Round 129: what changed

- **Every LYNX engine editor** (the generic panels and the display-first editors) has the originals' header: undo / redo
  of that plugin's own edits, A / B with copy to the other setting, the preset name with ◀ ▶ and a menu of LYNX's
  factory presets (90, own names and settings, in folders), the user's presets (Save preset…, Delete, Show the presets
  folder) and "Keep the Mix when loading presets" (Valhalla's mix lock). A / B and the preset name are saved with the
  project. LYNX Tune's presets leave its key, scale and note switches alone.
- **Glow Leveler** (CLA-2A): the VU DISPLAY selector (input / GR / output), TRIM ±18 dB, AUTO MAKEUP, the bottom strip
  of the reference (HF emphasis, Mix, Trim).
- **Ridge Strip** (SSL E-Channel): the GAIN block's output fader beside an input / output meter with peak read-outs;
  dynamics before the EQ by default, as on the reference.
- **Compressor** (the Renaissance Compressor's stand-in): its own panel — threshold fader by the input / output
  meters, the gain-reduction meter with the ratio under it, the gain fader, attack / release, MIX and TRIM (it had a bare row
  of knobs and no meters).
- **Quell Denoiser** (NS1): the reduction meter now moves (the engine's meter was never published to the editor).
- **Gemini Doubler** (Doubler): output level meters beside the output fader.
- **Echoform** (H-Delay / ValhallaDelay): Phase L / Phase R (translated from H-Delay states), a tap-tempo pad, the
  modulation section named after the mode (Mod / Freq / Pitch) and "Delay L" in dual / ping-pong.
- **Atrium** (ValhallaRoom): the Early / Late controls on two tabs like the reference; "Depth" for the early / late
  balance. **Nebula** (Supermassive): the CLEAR button. **Foil Plate**: EQ defaults 700 Hz / 10 kHz. **Halo**: the low
  cut shows what the engine applies. **Lattice Mod**: starts with two taps like UberMod.
- **Substitutes**: Soundtoys EchoBoy / PrimalTap → Echoform (was Fruity Delay 3 at FL's default), Little Plate → Foil
  Plate (was Reeverb 2), PanMan's Auto-pan really sweeps (its depth was 0 %).

Still missing (ranked): the per-control description line of the Valhalla panels; Doubler4's voices 3–4; H-Delay's
own-BPM sync, LoFi switch and Analog 0–4; Ridge's Split / Dyn S.C. routing and Analog; RComp's ARC / Opto / Warm (a
re-route to Crest needs the vocal-chain refit); Waves Tune Real-Time's Speed / Tolerance / Vibrato (LYNX Tune is being
extended by round 128c); Decapitator's Punish / Thump / Steep and five styles; EchoBoy's Rhythm mode; PanMan's
rhythm / trigger sources; a Little AlterBoy role. Soundtoys and the newer Waves captures need the user (iLok
activation; the Waves shell hangs in our host since this round).

## The frame shared by every original (Waves bar, Valhalla preset row, Soundtoys control strip)

| Original feature | Where in the originals | LYNX before | Note |
|---|---|---|---|
| Undo / Redo of the plugin's own edits | Waves bar (every Waves plugin) | ⚠️ → ✅ | round 129: ↶ ↷ in every LYNX engine's header |
| A/B compare (Setup A / B) + copy A→B | Waves bar; Soundtoys strip | ❌ → ✅ | round 129: A, B and A→B / B→A, saved with the project |
| Preset name, ◀ ▶, menu of factory presets | Waves bar; Valhalla "PRESET: Default ◀ ▶" row; Soundtoys strip | ❌ → ✅ | round 129: LYNX factory presets in folders (FL presets stay listed for FL effects) |
| Save / load the user's own presets | Waves Save / Load; Valhalla preset menu (save, copy); Soundtoys | ❌ → ✅ | round 129: .lynxpreset files per engine |
| Name of the preset a project was saved with | Waves / Valhalla show it | ❌ → ✅ | LYNX presets' names are saved; round 135: a translated original's preset name is read too (Valhalla `presetName`, Waves `<Preset Name>`) and shown in the header |
| Per-control description line inside the window | Valhalla (bottom strip) | ⚠️ → ✅ | round 135: a strip at the bottom of every LYNX engine editor (generic panels and display-first editors): one sentence about the control under the mouse in LYNX's own words (core/ParamHelp, every control of every LYNX engine, pt-BR), or the current mode / type and what it is while nothing is hovered |
| Mix lock (Mix stays while presets change) | Valhalla (click the MIX label) | ❌ → ✅ | round 129: in the preset menu, per engine; round 135: also a click on the MIX caption (boxed in the accent colour while locked) |
| Typed values, reset, fine drag, wheel | all | ✅ | right-click value entry, double-click / Alt-click reset, Ctrl fine, wheel |
| MIDI learn | Waves (right-click) | ✅ | right-click → automation / controller link |
| Resizable window | Waves ≡ menu, Valhalla corner | ✅ | the generic panels resize at a fixed aspect ratio |

## Glow Leveler ← CLA-2A (Waves)
Sources:
- `docs/specs/cla-2a.md` (§1, §3, §6);
- trial screenshot `/mnt/c/Users/kauan/lynx-test/w119/ref/CLA-2A_Stereo.png`;
- LYNX screenshot `lynx-before/Glow Leveler.png`;
- `glowLeveler()` in PluginLayout.cpp, `glow::Leveler` and `paramText` in MixSuite.cpp, the `vumeter` display in
  PluginEditors.cpp.

Trial on this PC: yes (the new parity129 capture is missing; the w119 capture was used).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Header: Undo / Redo | WaveSystem bar, top left | song-level Ctrl+Z only | ✅ | round 129's preset bar (per-plugin undo / redo) |
| Header: Setup A/B + A→B copy | bar ("A→B", "Setup A") | none | ✅ | round 129's preset bar: A / B and copy, saved with the project |
| Header: preset name + Load menu | bar ("A: Start Me Up"), factory presets by CLA | presets box lists FL presets only, "(no preset)" | ✅ | no factory or user presets for Glow; round 129: LYNX factory presets for Glow + user presets |
| Header: Save | bar | none | ✅ | round 129: Save preset… (.lynxpreset) |
| Header: ≡ menu (help, resize) | bar, right | options menu (Presets, Reset, Bypass); window resizable | ⚠️ | |
| Bypass | VST param | slot LED / options menu | ✅ | |
| GAIN knob | 0–100 scale, default **40** (≈ +8 dB below threshold), left of the VU; nonlinear law (20 → −18.5 dB, 40 → +8, 80 → +31.8) | "Gain" 0…+30 dB linear, default **+0.0 dB**, big knob on the left | ✅ | round 123: the Opto classic voicing reads 0–100 with the measured law; round 152: a new Glow starts there at **40** (the trial's fresh instance: +8.45 dB at −40 dBFS) |
| PEAK REDUCTION knob | 0–100, default **46**, right of the VU, nonlinear | "Peak reduction" 0–100 (side chain −20…+40 dB linear), default **0** = off, big knob on the right | ✅ | round 123: the classic voicing's measured taper (one to one); round 152: default **46** |
| COMPRESS / LIMIT switch | toggle, left side under the logo; default Compress | "Compress | Limit" tabs under the VU | ✅ | different place (centre, not left) but easy to find; Limit γ = 9 (~10:1) vs about 100:1 on the hardware |
| VU meter | analog VU in the centre; −20…+3 scale, red above 0 | analog VU in the centre, same −20…+3 scale | ✅ | good match |
| VU DISPLAY selector | rotary knob INPUT / **GR** (default) / OUTPUT, top right | hidden: clicking the meter switches GR ↔ Output; no Input | ✅ | no visible selector, and Input is missing; add a 3-position switch beside the VU; round 129: INPUT / GR / OUTPUT selector beside the VU; round 165x [EXEC]: saved with the project (**VU display**) and translated from the state (RealWorld 17) |
| VU readout text | none (needle only) | "Gain reduction −x.x dB" or "Output x VU" inside the face | ➕ | |
| VU calibration | 0 VU = −18 dBFS (spec, unverified) | output mode 0 VU = −18 dBFS | ✅ | |
| HI FREQ (side-chain HF emphasis) | knob 0–100, default **50** (≈ +5–6 dB detector sensitivity at 6 kHz), with a "FLAT" mark; bottom strip | "Emphasis" 0–100 % (+0…+10 dB shelf at 2.5 kHz in the detector), default **0 %**, small knob under the switch | ✅ | round 123: the classic voicing's measured side-chain emphasis and Hi Freq law, translated (RW 153); round 152: default **50** |
| ANALOG | 3 buttons 50Hz / 60Hz / OFF, bottom left; default **Off** (the trial's fresh instance) | none | ✅ | round 152 [EXEC]: **Analog Off \| 50 Hz \| 60 Hz** bottom left — the measured hum (mains harmonics, the 2nd strongest at −98.7 dBFS, both channels alike) and noise (−89.5 dBFS RMS, flat per third octave, L / R uncorrelated) in the wet path after the gain (Trim moves it, Mix 0 % removes it); translated (RW 140) |
| MIX | knob 0–100 %, default 100, bottom strip | "Mix" 0–100 %, default 100 %, small knob | ✅ | round 152: translated (RW 204) |
| TRIM | knob −18…+18 dB, default 0, bottom right (after the mix) | none (Gain is pre-mix and 0…+30 only) | ✅ | new parameter (output trim ±18 dB); round 129: Trim ±18 dB after the mix |
| AUTO MAKEUP | toggle under the VU-display knob, right; default Off | none | ✅ | new parameter: makeup tied to peak reduction; round 129: Auto makeup toggle; round 165x [EXEC]: the make-up counts from the dial where it was switched on (**Makeup ref**), switching off keeps it in Gain, translated; round 152 [EXEC]: the classic voicing's measured law (a make-up that follows the Peak reduction dial: −1.5 dB at 0, 0 at 46, +6.8 at 62.6, +11.4 at 80, +16.7 at 100 — not a level follower), translated (RW 207) |
| Mono / stereo components | separate Mono and Stereo plugins | stereo-linked engine | ⚠️ | no mono variant needed in a DAW slot |
| Attack / release | fixed (opto cell, program dependent: about 66 ms near the knee to 4 ms deep; two-stage release 60 ms then 0.5–15 s) | fixed: fast cell 10 ms / 50 ms + slow cell 400 ms / 0.4–5 s with memory | ✅ | round 165x [EXEC]: the measured attack (rate-limited: the transient goes through, ≈ 2 dB per ms; Limit 80 −0.9 / −2.9 / −4.8 / −7.9 / −13 dB at 0 / 1 / 2 / 4 / 10 ms), with the Opto release Measured; round 123: the classic voicing's measured log-domain attack (faster as the reduction grows), hyperbolic two-stage release and memory; round 152 check: the recovery after a loud passage is ≈ 1 dB slower than the trial's on 0.5 s stairs round 165w [EXEC]: the release measured on the 17.1 trial (bursts of 0.1 … 10 s at −24 … 0 dBFS, then a −40 dBFS probe; Compress 46 / 62.6 / 80, Limit 62.6): the same recovery after 0.5, 2 or 10 s (no long memory), back within 0.1 dB after ≈ 10 s; refitted (recovery 0.08 … 0.84 → 0.03 … 0.15 dB rms; held-out 0.5 s stairs, falling half 0.52 … 0.76 → 0.35 … 0.45 dB); older projects keep round 123's (Release Old \| Measured) |
| Static curve | γ ≈ 4.54 (≈ 4.2:1 local, 5.5:1 asymptotic) | γ = 2 (≈ 3:1) | ✅ | round 123: the Opto classic voicing's cell law fitted on the trial (γ 2.15 on the side-chain level above its threshold, feedback detection); round 152 [EXEC]: **Limit** measured (a steeper cell, 1.5 % of the input in the side chain — the trial over-compresses at 62.6 — and the cell's deepest reduction, 30.4 dB): 0.34 / 0.52 / 0.50 dB rms at peak reduction 46 / 62.6 / 80 (round 123's Limit 0.37 / 1.00 / 1.32); older projects keep the old Limit (Opto law Old \| Measured) |
| HF tilt under GR / 2nd harmonic | yes (+1…+6 dB above 6 kHz under GR; ≈ −52 dBc H2) | none | ✅ | round 123: the classic voicing's measured tilt (up to +7 dB at 20 kHz under reduction) and 2nd / 3rd harmonics |
| Section layout | Gain left, VU centre, Peak Reduction right; Comp/Limit left; VU selector + Auto Makeup right; bottom strip Analog · Hi Freq · Mix · Trim | Gain left, VU centre, Peak reduction right; Comp/Limit under the VU; Emphasis + Mix small at the bottom | ⚠️ | the main row matches; the bottom strip has 2 of its 4 items, and the right column (selector, auto makeup) is missing |
| Proportions | 659×296 window, big knobs, VU in the middle third | 540×270, same composition | ✅ | |
| Knob scales | 0–100 printed around Gain and Peak Reduction | arc only; the value appears under the knob | ⚠️ | a CLA user reads Gain as 0–100; LYNX shows dB |
| Value entry / fine / reset | Waves value handling | right-click entry, Ctrl fine, double-click reset | ✅ | |
| Saved-state translation | — | Gain (via the measured table), Peak Reduction, Comp/Limit translated; Hi Freq, Analog, Mix, Trim, Auto Makeup and VU ignored | ✅ | PLUGIN_FALLBACK row 140 / round 113 §3c; round 123: Hi Freq, Trim; round 152: Mix (RW 204), Auto Makeup (207), Analog (140); the VU selector is the editor's view only |

**Round 165x** (REA_rodada165x §1, §2, §5b): configuration 9 → **10**, design 8 → 8 — the measured attack (the transient goes through, ≈ 2 dB per ms; first 50 ms of 30 onsets 5.86 → 1.21 dB rms, worst 26.7 → 3.3 dB; round 123's for older projects), AUTO MAKEUP's reference point (appended **Makeup ref**: the dial where it was switched on; switching it off folds the make-up into Gain, as the trial turns its own Gain knob; translated), the VU DISPLAY selector saved with the project (appended **VU display**) and translated (RealWorld 17). Left (design): the 0–100 scales printed around the big knobs, the ≡ menu.

**Round 165w** (REA_rodada165w §2): configuration 9 → 9, design 8 → 8 — sound: the classic voicing's measured release (appended **Opto release**: Measured for new instances and translated states, Old for projects / presets saved before; Release Old | Measured tabs top right with the classic voicing). Left: Auto Makeup's reference point, the VU selector not translated.

**After round 152** (REA_rodada152a §2): configuration 7 → **9**/10, design 7 → **8**/10 — the reference's fresh instance (Opto classic, 40 / 46 / 50), ANALOG 50 / 60 Hz (measured hum and noise), the measured Limit and AUTO MAKEUP, Mix / Auto Makeup / Analog translated. Left: the release tail ≈ 1 dB slower than the trial's after loud passages, Auto Makeup's reference point (LYNX: the dial's default 46; the trial: the dial where it was switched on).

**Scores:** configuration 5/10, design 6/10. The main row is correct. A CLA-2A user misses Trim, Auto Makeup,
Analog, the 3-way VU selector, the 0–100 Gain scale and CLA's defaults (Gain 40 / PR 46 / Hi Freq 50).

**Top gaps:**
1. **Defaults and Gain scale.** Default Gain should give ≈ +8 dB, Peak reduction ≈ 46, Emphasis ≈ the Hi Freq 50
   equivalent. Show Gain as a 0–100 knob value (the dB value can go in the tooltip), following the measured table
   already used in `translateWavesCla2a`.
   Where: `glowInfo()` (MixSuite.cpp), `paramText`. Engine: only the Gain mapping law. Effort S.
2. **VU DISPLAY selector Input / GR / Output** as a visible 3-way switch beside the VU (today a hidden click with
   no Input).
   Where: `glowLeveler()` + the `vumeter` display (PluginEditors.cpp; it needs input-peak live output 2, which the
   engine already sets). UI only; it could be a non-audio parameter so it is saved. Effort S.
3. **TRIM ±18 dB** (post-mix) and **AUTO MAKEUP** toggle.
   Where: new parameters in `glowInfo()`, `glow::Leveler::process`, the layout's right column / bottom strip, and
   the translator (RW 206 / 207). Engine + UI. Effort S.
4. **Rename/relabel Emphasis as "HF emphasis (side chain)"** with a flat point, and translate Hi Freq (RW 153)
   once its law is measured (spec §8 Q4). Engine law + translator. Effort S–M.
5. **ANALOG Off / 50 / 60 Hz** (hum + noise, default Off). Engine (noise generator). Effort M; low priority.
6. **Header: A/B + copy, user presets (Load/Save), per-plugin undo.** These are shared generic-editor work
   (PluginEditors.cpp header) and benefit every engine. UI + state storage. Effort L. Also add a few factory
   presets for Glow.
7. **Engine fidelity** (spec §7: γ ≈ 4.5, level-dependent attack, HF tilt, H2). DSP. Effort M–L. A user hears this
   as "details missing" even when every control is there.

---

## Ridge Strip ← SSL E-Channel (Waves); also SSL Native Vocalstrip 2 (round 145: the Vocal strip voicing)
Sources:
- `docs/specs/ssl-e-channel.md`, `docs/specs/ssl-native-vocalstrip-2.md`;
- Waves "SSL E-Channel & G-Channel User Manual" (assets.wavescdn.com/pdf/plugins/ssl-e-channel.pdf);
- trial screenshot `look114/ref/sslch_main.png` and parameter list `look114/sslch_params.txt`;
- LYNX: `ridgeStrip()` (PluginLayout.cpp), `ridge::` (MixSuite.cpp), `lynx-before/Ridge Strip.png`.

Trial on this PC: yes (older capture used).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Header: Undo/Redo, A/B, A→B, preset Load/Save, name | WaveSystem bar | generic header, "(no preset)" | ✅ | round 129's preset bar |
| Overall layout | left = Filters + EQ (HF top → LF bottom); right = Dynamics (Compress, Expand, GR LEDs), EQ TO / DYN TO routing, Analog, Gain block with meters + Out fader | left = Filters, HF, HMF, LMF, LF, EQ curve; right = Compressor, Expander/gate, Dynamics (GR bars, routing), Gain | ✅ | same reading order and columns |
| HPF | 16–350 Hz, 18 dB/oct, fully left = Out (default Out) | "High-pass" 16–350 Hz 3rd order, 0 = off | ✅ | |
| LPF | 3–22 kHz, 12 dB/oct, fully left = Out | "Low-pass" 22 k→3 kHz 12 dB/oct, 0 = off | ✅ | |
| SPLIT (filters before dynamics) | button under the LPF, default off | "Split" under the low-pass (E console voicing, round 123) | ✅ | round 165: shown only in the E console voicing, where it acts (Ridge's own engine keeps its filters before the dynamics) |
| HF band: gain / freq / BELL | ±16.5 shelf / ±18 bell, 1.5–16 kHz, default 8 kHz; BELL button | gain ±15 (brown) / ±18 (black), 1.5–16 kHz, default 7.87 kHz; Bell toggle | ✅ | round 165: the read-out shows only the gain of the curve in use (was "+6.0 dB / +7.2", both curves at once) |
| HMF: gain / freq / Q | 600 Hz–7 kHz (default 3.5 k), Q 0.1–3.5 (default 1.5), gain ±15…±18 depending on Q | 600–7 kHz (default 2.0 k), Q 0.5–2.5 (default 1.0), gain ±15/±18 | ⚠️ | E console voicing (round 123): the console's Q 3.5 → 0.1 in its direction (measured); Ridge's own engine keeps 0.5–2.5; different defaults |
| LMF: gain / freq / Q | 200 Hz–2.5 kHz (default 500 Hz), Q 0.1–3.5 (default 2.5) | 200–2.5 k (default 593 Hz), Q 0.5–2.5 (default 1.0) | ⚠️ | as HMF |
| LF band: gain / freq / BELL | ±16.5 shelf / ±18 bell, 30–450 Hz, default 60 Hz | ±15/±18, 30–450 Hz, default 89 Hz; Bell | ✅ | default differs |
| EQ curve family | E = "Black" 242 only (G-Channel = 383) | Brown \| Black tabs, default **Brown** | ➕ | E-Channel users expect Black; the translator uses Brown because it was fitted |
| EQ TO: BYP | button, EQ section bypass ("hardware-flat") | "EQ in" toggle | ✅ | |
| EQ TO: DYN S.C. (filters **and EQ** into the side chain) | button | "EQ to side chain" (Dynamics section) | ✅ | **round 165** [EXEC trial]: the filters (unless Split) and the EQ leave the audio and feed only the dynamics' detectors (the compressor's feedback detector and the expander's); both voicings but the 4K; translated (RealWorld 26). The old filters-only "To sidechain" switch shows only in projects that use it |
| DYN TO: BYP | button | "Dyn in" toggle | ✅ | |
| DYN TO: CH OUT (dynamics post-EQ) | button, default **pre-EQ** | "EQ>Dyn \| Dyn>EQ" tabs, default **EQ>Dyn** | ✅ | the default order is the opposite of SSL's (the translator sets Dyn>EQ); round 129: Dyn>EQ is the default, as on the reference |
| Comp RATIO | 1…∞, default 1 (off) | 1 + 19v² … ∞, default 1 (off) | ✅ | |
| Comp THRESHOLD | +10…−20 dBu (= −8…−38 dBFS), default 0 dBu (−18 dBFS); clockwise = lower | −40…0 dBFS, default 0 dBFS; clockwise = higher | ⚠️ | E console voicing: the console's +10 … −20 dB scale (round 123), but clockwise = higher threshold (SSL: lower); Ridge's own engine: dBFS |
| Comp RELEASE | 0.1–4 s, default 0.4 | 0.1–4 s, default 0.29 | ✅ | |
| Comp F.ATK | fast 1 ms; off = auto, program dependent | "Fast attack" 1 ms; off = fixed 10 ms | ⚠️ | no program-dependent attack |
| Auto make-up from ratio + threshold | always on (the manual) | none (the translator adds +0.9 dB output) | ✅ | round 123: the E voicing has the console's measured make-up; round 147b: "Auto make-up" (Compressor section) gives Ridge's own engine the same law (threshold dBFS + 18 = the console's scale, ratio) |
| Comp Mix | — | "Mix" 0–100 % | ➕ | |
| Comp curve display | — | static transfer curve | ➕ | |
| Expander THRESHOLD | −30…+10 dBu (−48…−8 dBFS), default −30 | −80…0 dBFS, default −60 | ⚠️ | E console voicing: −30 … +10 dB, the console's (round 123) |
| Expander RANGE | 0–40 dB, default 0 | 0–60 dB, default 0 (off) | ⚠️ | E console voicing: 0 … 40 dB (round 123) |
| Expander RELEASE | 0.1–4 s, default 0.2 | 0.1–4 s, default 0.29 | ✅ | |
| Expander F.ATK | 1 ms / auto | "Fast" 0.1 ms / 1.5 ms | ⚠️ | |
| GATE switch (Exp default) | button + LED; expander is the default | "Gate \| Expand" tabs, default **Gate** | ⚠️ | default is the opposite |
| Threshold hysteresis | variable (program dependent) | fixed 4 dB (gate) | ⚠️ | |
| GR indicators | two LED ladders (comp, exp) 3/6/10/14/20 dB + gate LED | two LED ladders "Comp" / "Exp" 3 / 6 / 10 / 14 / 20 dB | ✅ | round 165: the reference's ladders (were two bars); no separate gate-open LED |
| ANALOG on/off | switch, centre right; default (her state) Off | none | ✅ | round 147b: "Analog" (Dynamics section) [EXEC trial: Analog on adds white noise of −110.7 dBFS RMS per channel at the output, uncorrelated L / R, whatever the input / output / compressor settings; harmonics unchanged]; both voicings; translated (RealWorld 31) |
| INPUT trim | ±18 dB, knob in the Gain block | "Input" ±20 dB | ✅ | |
| Phase Ø | button + LED | "Phase" toggle | ✅ | |
| OUTPUT fader | fader −24…+12 dB beside the meter | "Output" knob ±20 dB | ⚠️ | knob, not a fader; different range |
| Level meter | stereo bar meter −50…0 dBFS with an INPUT / OUTPUT selector and a numeric peak readout ("−7.2") | none | ✅ | UI (the engine already exports in/out peaks via meters.set); round 129: the GAIN block's input / output meter with peak read-outs |
| EQ response display | — | "Equalizer" curve | ➕ | |
| Latency | 1 sample | 0 | ✅ | irrelevant |
| Vocalstrip 2 (SSL Native) role | de-esser, de-ploser, 3-band EQ, compander with knee/makeup/drive, process order | round 145: Ridge's **Vocal strip voicing** — every control of the reference (34 parameters, its own laws), its own panel (level, compander, de-esser / de-ploser, equaliser, order menu, GR meters), measured on the official trial; her state translated (−60 … −67 dB null on her takes) | ✅ | REA_rodada145 §2; the broadband de-esser / de-ploser on bursts is the least exact part (−13 … −25 dB) |
| Saved-state translation | — | every control (E console voicing, round 123; Analog 147b; Dyn S.C. round 165) | ✅ | round 113 §3c, 123, 165 |

**Round 147b:** configuration 7 → 8 (auto make-up, ANALOG measured and translated), design 7 → 7.

**Round 165** (REA_rodada165h): configuration 8 → **9**, design 7 → **8** — EQ TO DYN S.C. (the filters and the EQ only in the dynamics' side chain, measured: −25 … −27 dB null on a de-essing setting), the console's ∞ : 1 (state value 100, was read as 20 : 1) and its law refitted on the trial's stairs (∞ settings −0.6 … −2 dB null → −21 … −39 dB; 10 : 1 −17 … −22 → −23 … −51 dB), the two GR LED ladders, the gain read-out of the curve in use, Split shown where it acts. Left: the E voicing's compressor threshold turns the opposite way (clockwise = higher), the output fader's −24 … +12 range, a gate-open LED, Expand as the default.

**Scores:** configuration 6/10, design 6/10. An SSL user finds every knob in the expected column. What is missing
or wrong: Split / Dyn S.C. semantics, Analog, auto make-up, the In/Out meter with the output fader, and
threshold units and directions.

**Top gaps:**
1. **In/Out level meter with an INPUT/OUTPUT selector and peak readout** in the Gain block.
   Where: `ridgeStrip()` (a new display id or reuse of a level-meter display) and PluginEditors.cpp. UI only;
   `meters.set` already exports in/out peaks. Effort S.
2. **Defaults to SSL's:** order Dyn>EQ, Expand (not Gate), Black curve for new instances, Q defaults 2.5 / 1.5,
   LMF 500 Hz, HMF 3.5 kHz, LF 60 Hz, comp release 0.4 s.
   Where: `ridgeInfo()`. Caveat: a default change affects existing projects that rely on unsaved defaults; check the
   state loader. Effort S.
3. **Routing semantics:** add a **Split** switch (filters pre-dynamics vs with the EQ). Turn "To sidechain" into
   SSL's **Dyn S.C.** (filters + EQ feed the side chain, audio unchanged).
   Where: `ridge::Strip::process` (new parameter "Split"; change `filtersToSidechain`). Engine. Effort M. Watch
   compatibility: today's "Filters to SC" behaviour must be kept or migrated.
4. **Auto make-up** M(threshold, ratio) (spec §7.4) and **program-dependent attack** when F.ATK is off.
   Engine. Effort M.
5. **Threshold display in dBu (0 dBu = −18 dBFS), SSL direction, ranges +10…−20 (comp) and −30…+10 (exp), range
   0–40.** `paramText` + laws. This is a range change, so the translator must be updated as well. Effort M.
6. **Q knob 0.1–3.5, reversed direction, gain range depending on Q**; show only the active curve's gain in the
   readout. Engine law + `paramText`. Effort S–M.
7. **Analog on/off** (console colour). DSP. Effort M.
8. **Header A/B, presets, undo**: shared generic work (see Glow #6).
9. **Vocalstrip 2 translator** (XML) to Compressor/Crest (spec ssl-native-vocalstrip-2 §7). Effort S.

---

## Compressor (generic stand-in) ← Renaissance Compressor / RComp (Waves); also CLA-76
Sources:
- `docs/specs/renaissance-compressor.md`, `docs/specs/cla-76.md`;
- Waves "Renaissance Compressor User Guide" (assets.wavescdn.com/pdf/plugins/renaissance-compressor.pdf);
- **new trial capture `/mnt/c/Users/kauan/lynx-test/parity129/ref/rcomp.png`** (371×525, latency 64 reported by
  the host in ref1.log);
- LYNX `effectInfo "Compressor"` + `class Compressor` + `formatEffectParam` (Effects.cpp). The panel is the
  generated generic one: a plain knob row Threshold · Ratio · Attack · Release · Makeup · Auto gain, with no meters
  and no display.

Trial on this PC: yes.

What the trial shows (rcomp.png):
- left: **Thresh** slider over a stereo **Input** meter, with numeric peak readouts;
- centre: a large **gain-change meter** (+6…−12 dB, bipolar for expansion) with the **Ratio** value box overlaid,
  and the GR readout below;
- right: **Gain** fader over a stereo **Output** meter, with a **limiter-activity** pill under the value;
- bottom: **Attack** and **Release** horizontal sliders with value boxes; **ARC**, **Electro**, **Warm** toggle
  buttons; **Mix** knob (0–100) and **Trim** knob (±18).
- There is **no transfer-curve graph**.

| Original feature | Original (range / default / where) | LYNX Compressor now | Status | Note |
|---|---|---|---|---|
| Header: Skins menu | Light / Dark / Legacy | — | ➕/n.a. | not needed |
| Header: Undo/Redo, A/B, A→B, Load/Save, preset name | WaveSystem bar ("A: Default Preset") | generic header, "(no preset)" | ✅ | round 129's preset bar |
| Threshold | −60…0 dBFS, default **0**, vertical slider integrated in the input meter | knob −60…0 dB, default **−18** | ✅ | range ✅; round 152: the trial's fresh instance read back (threshold 0.0 dB, ratio 1.00, attack 16 ms, release 160 ms, gain 0) is the factory preset **RComp-style start** (vintage voicing, RComp ranges, limiter on); a new Compressor keeps LYNX's −18 dB so it compresses at once (it is also FL's generic compressor) |
| Ratio | 0.50…50 (below 1 = expansion), default **1.0**, value box inside the GR meter | knob 1…20, default **4:1** | ✅ | round 150: **Ranges = RComp** (LYNX \| RComp tabs): the reference's own ratio control 50 … 1.00 … 0.50 : 1 (its value law, read from the trial), below 1 : 1 the measured upward expansion above the threshold (0.50 / 0.79 / 0.90 : 1 within 0.4 dB); translated states use it |
| Gain (output) | −30…+30 dB, default 0, vertical fader on the output meter | "Makeup" 0…+24 dB | ✅ | round 150: with the RComp ranges the Gain fader runs −30 … +30 dB |
| Attack | 0.5…500 ms, default 16 ms, horizontal slider | 0.1…100 ms, default 10.2 ms | ✅ | round 150: RComp ranges 0.5 … 500 ms (default 16 ms) |
| Release | 5…5000 ms, default 160 ms, horizontal slider | 10…1000 ms, default 120 ms | ✅ | round 150: RComp ranges 5 … 5000 ms (default 160 ms) |
| ARC / Manual | toggle, bottom | none (the translator fakes ARC with a fixed 300 ms) | ✅ | engine; round 135w: ARC / Manual switch (vintage voicing; ARC = the fitted program-dependent release, Manual = one release time [model]); translated (RW 17) |
| Electro / Opto | toggle (GR-dependent release around 3 dB) | none | ✅ | engine; round 135w: Electro / Opto switch (Opto inverts the slow release curve [model]); translated (RW 22) |
| Warm / Smooth | toggle | none | ✅ | engine; round 135w: Warm / Smooth switch (Smooth = ratio as shown, without Warm's extra reduction [model]); translated (RW 16) |
| Mix | 0–100 %, default 100, knob | none (only the DAW slot dry/wet) | ✅ | round 129: Mix knob |
| Trim | −18…+18 dB, post-limiter, knob | none | ✅ | round 129: Trim ±18 dB |
| Built-in 0 dBFS output limiter + activity indicator | always on; pill under Gain | none | ✅ | round 135w: Limiter switch (on for translated RComp states) + the activity pill under it |
| Knee | soft, starts ≈ 6 dB below the threshold (fixed) | hard knee | ✅ | engine; round 123: the vintage voicing (substitutes start there) has the fitted soft knee; Classic stays hard |
| Static curve per ratio | 1.05 … 50 : 1 (20 and 50 alike), knee from ≈ 12 dB under the threshold, the same at every threshold; Warm = Smooth, ARC = Manual, Electro = Opto statically | round 123's fit at her 2.95 : 1 (0.6 … 1.8 dB rms off elsewhere, up to 3.5 dB at 1.5 : 1, 30 dB over) | ✅ | round 152 [EXEC]: **Vintage curve = Measured** (new instances and translated states; older projects keep the round-150 curve, the Old \| Measured tabs above the meter): the trial's stairs at 11 ratios as a correction table — every ratio within 0.07 dB (holdouts 1.2 / 3.3 / 8 / 14 : 1 within 0.06); her state unchanged (≤ 0.01 dB up to −3 dBFS) |
| Input meter (stereo) + peak readouts | left | none | ✅ | round 129: input / output meters with peak read-outs |
| Gain-change meter (bipolar, +6…−12) + readout | centre, the main display | none (no live output) | ✅ | round 129: the reduction meter with its read-out (0…−30, not bipolar); round 152: the **bipolar gain-change meter** — +6 … 0 … −12 dB on the reference's scale (deeper reductions compressed to −24), reduction downwards and the expansion's boost upwards from the 0 dB line, the held largest change under it (click = reset); both voicings |
| Output meter (stereo) + peak readouts | right | none | ✅ | round 129 |
| Latency / look-ahead | 64 samples @ 48 kHz | 0 | ✅ | round 123: the vintage voicing has the measured 64-sample delay |
| Auto gain (power-matched) | — | "Auto gain" toggle | ➕ | |
| External side chain | — | yes (DAW side-chain routing) | ➕ | |
| Gestures | drag, value windows accept typed values | generic contract | ✅ | |
| Saved-state translation | — | threshold (−5 dB), ratio, attack, release (ARC → 300 ms), gain; Warm/Smooth, Electro/Opto, mix, trim not translated | ✅ | round 113 §3c; 0.53 dB RMS on the stairs; round 123 / 135w: threshold, ratio, attack, release, gain, mix, trim, ARC, Electro / Opto, Warm / Smooth, limiter |
| CLA-76 (same stand-in) | Input/Output −∞…0, Attack/Release 1–7, Ratio 4/8/12/20/All, Meter In/GR/Out, Analog, Bluey/Blacky, Mix | Compressor at a fixed preset, no translation | ✅ | round 127: CLA-76 → Crest Compressor (Bus style) fitted on her state, RealWorld list translated (input / output, attack / release 1–7, ratio buttons, mix) |

**Scores:** configuration 2/10, design 1/10. A plain row of 6 knobs with no meters cannot be read as RComp.

**After round 152** (REA_rodada152a §1): configuration 9 → **10**/10, design 7 → **8**/10 — the bipolar gain-change meter, the measured static curve at every ratio, the reference's fresh instance as a factory start. Left: Warm / Smooth's and Opto's transient differences (statically identical on the trial), the meter's ratio box (the ratio is a knob under it).

**After round 150** (REA_rodada150 §6): configuration 8 → **9**/10, design 7/10 — the reference's own control ranges (ratio 0.5 … 50 with expansion, gain ±30, attack 0.5 … 500, release 5 … 5000 ms), translated. Left: threshold default 0 dB, the bipolar gain-change meter, the vintage voicing's compression away from her 2.95 : 1 (1.2 – 1.7 dB deeper at 10.46 : 1).

**Answer: does RComp need its own LYNX engine/editor?** **No new engine.** **Crest Compressor**
(`src/core/DynamicsSuite.cpp` `crest::`, editor `CrestEditor` in `src/ui/ProEditors.cpp`) already has almost
everything an RComp user looks for:
- threshold −60…0 and ratio 1…100;
- **soft knee 0–72 dB**;
- attack 0.005–250 ms and release 10–2500 ms;
- **Auto release** (≈ ARC), and an **opto** and a **classic** style (release-shape families);
- **Character Warm** (≈ Warm);
- Mix, Input/Output ±36 dB, Auto gain, look-ahead 0–20 ms (it can reproduce the 64-sample delay), oversampling;
- **IN / OUT / GR meters**, a GR history display and a knee/curve side panel.

Glow is the wrong host: it is a fixed-time opto leveler with no threshold, ratio or attack.

Recommendation:
1. **Re-route `[waves-rcomp]` (PluginSubstitutes.cpp) from Compressor to Crest Compressor.** Write a
   `translateWavesRComp` for Crest along the lines of spec §7:
   - threshold RW1 − 4, ratio RW9, **knee 18 dB**, attack RW8, release RW0;
   - auto release = ARC (RW17 = 0);
   - Character Warm when RW16 = 0;
   - Mix (RW26), Output = gain RW2, look-ahead 1.33 ms.
   - Re-fit on the round-113 stairs and take (target ≤ 0.15 dB RMS). Effort M.
2. **Add the two things Crest lacks**, both in `crest::`:
   - an **"Electro / Opto" release-shaping option**: GR-dependent τ around 3 dB (spec §5.3), as a "release
     behaviour" parameter that is active with Auto release;
   - a **post-output 0 dBFS safety limiter + trim**.
   - Expansion ratios 0.5–0.99 can be left out at first: her state never uses them, and Sentry Gate covers
     downward expansion.
   - Effort M.
3. **Keep "Compressor"** as FL's generic effect. Give its generated panel at least a GR meter: add
   `controlOutput` to `class Compressor` and a layout with a `grmeter` (S). Point CLA-3A / C1 / SSLComp /
   API-2500 at Crest as well with style presets (classic for C1/SSLComp, opto for CLA-3A). CLA-76 → Crest
   "classic" style with µs attack (Crest goes to 5 µs) and a ratio set 4/8/12/20 (translator after spec §8
   measurements).
4. **Editor (design):** Crest's display-first layout is a different design from RComp's three vertical meters,
   but every RComp control has an obvious home: Thresh = threshold line + knob, Ratio knob, Gain knob + Out meter,
   Attack/Release knobs, Auto (= ARC), Style menu (Electro/Opto would sit here), Character (Warm), Mix. An RComp
   user should rate it 7/10 for findability. Optionally add a compact "classic" view toggle (input meter + threshold,
   GR meter + ratio, output meter + gain): UI only, L. It is not required.

**Top gaps** (if Compressor stays the substitute):
1. GR / In / Out meters (S).
2. Soft knee 6–18 dB (S).
3. Signed gain −30…+30 + trim + 0 dBFS limiter (M).
4. ARC + Electro/Opto release (M).
5. Warm/Smooth (M).
6. Ranges: ratio 0.5–50, attack 0.5–500, release 5–5000 (S).
7. Mix (S).
8. Header A/B / presets / undo (L, shared).

The re-route to Crest gets 1, 2, 5, 7 and most of 3/6 for free.

---

## Hush De-esser ← DeEsser (Waves)
Sources:
- `docs/specs/deesser.md`;
- Waves "DeEsser Plug-in Manual" (assets.wavescdn.com/pdf/plugins/deesser.pdf): chapters 2 (Quick Start),
  4 (Controls and Displays), 5 (Internal design) and 7 (Factory Presets);
- `HushEditor` (ProEditors.cpp), `hush::` + `paramText` (MixSuite.cpp), `lynx-before/Hush De-esser.png`.
- **Trial screenshot not available**: `deesser.png` was not produced in this run, so the original's layout is
  taken from the manual's text.

Trial on this PC: yes (no screenshot this round).

| Original feature | Original (range / default / where) | LYNX Hush now | Status | Note |
|---|---|---|---|---|
| Header: Undo/Redo, A/B, A→B, Load/Save, resize | WaveSystem bar | ProEditor header: title, slot, gesture hint only | ✅ | round 129's preset bar in the ProEditor header; round 128: resizable |
| Factory presets | Default (5506 Hz HP Split), Male Ess 4500, Male Shh 3385, Female Ess 6779, Female Shh 5077 (all BandPass + Split) | none | ✅ | 4–5 generic-named presets (frequency + detector) would cover it; round 135w: "Classic split (band detector)" folder: Classic default 5.5 kHz, Male S 4.5, Male SH 3.4, Female S 6.8, Female SH 5.1 kHz (LYNX names; classic voicing, band detector, split) |
| Thresh | slider −80…0 dB, default **0**, aligned with the Energy Detector meter | "Threshold" big knob −60…0 dB, default **−30 dB**; also a draggable line on the waveform and an arrow on the meter | ⚠️ | 3 ways to set it ➕; range is 20 dB shorter; default differs |
| Freq | value window 2000–16000 Hz (log), default **5506 Hz** | horizontal slider 2–16 kHz with end labels, same log law; default **6.02 kHz** | ✅ | same law; set the default to 5506 (v = 0.487) |
| SideChain HighPass / BandPass | button, default HighPass | "Detection: High-pass \| Band" tabs (+ bottom-bar menu), default High-pass | ✅ | **round 165** [EXEC]: the trial's BandPass is its HighPass side chain followed by a 4th-order low-pass (Q 0.68 ×2 at 1.84 × the frequency at 5.5 kHz, 1.75 × at 8 kHz): 0.1 … 0.35 dB rms at 4.5 … 14 kHz; BandPass states are now translated (17.1 stores HighPass 1 / BandPass 0, they were all read as HighPass) |
| Audio Split / Wideband | button, default **Split** | "Processing: Wide band \| Split band" tabs, default Split | ✅ | Hush's split is 2nd order at the frequency; Waves' is 1st order at ≈ Freq/π (engine) |
| Monitor Output / SideChain (+ yellow light) | button | "Audition" toggle under the frequency slider (plays the detection band) | ✅ | same function, different name |
| Energy Detector meter (side-chain energy, dB below 0 dBFS, blue) aligned with the threshold | main meter | "IN" meter = input peak with a threshold arrow, and **inside it a blue bar = the detector's energy** (what the threshold is compared with, in the threshold's scale; both engines) | ✅ | round 147 |
| Attn meter (red, instantaneous GR) + **infinite peak hold, click to reset** | meter | "GR" bar with a held-peak line and number, click to reset | ✅ | round 144 |
| Output meter −30…0 dBFS + peak readout (click to reset) | meter | none (bottom bar shows the Output **setting**) | ✅ | UI; round 135w: OUT meter −30…0 beside IN / GR, clip lamp and highest-peak read-out, click to reset |
| Frequency-response display | **none in the classic DeEsser** (the manual lists no graph; Renaissance DeEsser has one) | small detector-band graph only in the generic `hushDeEsser()` layout; the display-first editor shows the scrolling waveform with the reduction in red | ➕ | nothing to add for parity |
| Range | none (the floor is the split's leakage) | "Range" 0–24 dB, default 12 | ➕ | translator fixes it at 12 dB |
| Release | none (hidden, unknown) | 10–250 ms, default 61 ms | ➕ | |
| Lookahead | none, 0 latency | 2 ms toggle, **on** by default (adds 2 ms of latency) in Hush's own engine | ✅ | **round 165** [EXEC]: the classic voicing (the DeEsser role, where the substitutes start) has no latency, as the trial (it reported 96 samples) |
| Output gain | none | ±12 dB | ➕ | |
| Knee / ratio | hard knee, effective ratio ≈ 7, RMS energy | classic voicing (round 123): ratio 7.1, 1.3 dB knee, power detector; Hush's own engine: 3 dB soft knee, ∞, peak | ✅ | **round 165** [EXEC] 7 kHz stairs, thresholds −10 … −60, HighPass / BandPass, Split / Wideband (24 settings): 0.10 … 0.25 dB rms; the wide-band reduction stops at 25.6 dB (was unlimited: up to 24 dB off at −60) |
| Gestures | slider drag, value-window typing | drag, Ctrl fine, wheel, reset, right-click entry, line/arrow drags | ✅ | |
| Saved-state translation | — | frequency (×0.81), threshold (+5 dB), Split/Wide, HP/BP, Monitor translated; range fixed at 12 dB | ✅ | round 113 §3c |

**Scores:** configuration 7/10 (every Waves control exists, plus extras), design 5/10. **After round 144**: configuration 9/10, design 7/10 (round 135w: 9 / 6; round 144: the GR meter's held peak). **After round 147**: design 7 → **8**/10 (the energy-detector bar beside the threshold arrow). The scrolling-waveform
editor is Pro-DS-like. A DeEsser user finds Threshold, Freq, HP/BP, Split/Wide and Monitor (as "Audition")
quickly. What they miss: the side-chain energy meter beside the threshold, the Attn peak hold, the output meter,
presets, and the Waves defaults.

**Round 165** (REA_rodada165h §2): configuration 9 → **10**, design 8 → 8 — BandPass measured and translated, the 25.6 dB floor, no latency; vocal-take nulls −16 … −37 dB over six settings (as shipped: −12 … −34, BandPass states playing as HighPass). Left (design): Hush's threshold knob vs the reference's −80 … 0 slider aligned with the meter.

**Top gaps:**
1. **Defaults:**
   - Frequency 5506 Hz (v 0.487);
   - Lookahead Off;
   - a threshold default that does not de-ess until it is lowered: Waves starts at 0 dB, Hush at −30 dB.
   Where: `hushInfo()`. Effort S.
2. **Meter semantics:** make the IN meter show the **detector (side-chain) energy** the threshold is compared with
   (or add it as a second bar). Add **GR peak hold with click-reset** and an **OUT meter**.
   Where: `HushEditor` (ProEditors.cpp) plus a live output for the detector level in `hush::DeEsser`
   (`meters.set`). Engine export + UI. Effort S–M.
3. **Factory presets**: Default / Male S / Male SH / Female S / Female SH frequencies, with BP + Split.
   Generic names, no Waves trade names needed. This needs the shared preset mechanism (see Glow #6) or a Hush-only
   menu. Effort S once presets exist.
4. **Threshold range −80…0.** Law + translator. Effort S.
5. **Engine** (spec §7): 1st-order split at Freq/π, RMS detector, ratio ≈ 7 hard knee, no range clamp for
   translated states, 0 latency. DSP. Effort M.
6. **Header A/B, undo, presets** in the ProEditor header (shared with Crest, Apex and the others). Effort L.

---

## Saved-state translation summary (docs/PLUGIN_FALLBACK.md, round 113 §3c)

| Original | LYNX | Translated | Not translated |
|---|---|---|---|
| RComp | Compressor | threshold (−5 dB), ratio, attack, release (ARC → 300 ms), gain | Warm/Smooth, Electro/Opto, Mix, Trim (auto gain forced off) |
| CLA-2A | Glow Leveler | Gain (measured table + 1.5 dB), Peak Reduction (0.40 + 0.021·(k − 40)), Comp/Limit | Hi Freq (emphasis forced 0), Analog, Mix, Trim, Auto Makeup, VU |
| SSL E-Channel | Ridge Strip | HP, 4 bands (fitted laws), EQ/dyn bypass, comp threshold (−15.7)/ratio/release/fast, output (+0.9 make-up) | Split, Dyn S.C., Ch Out (forced Dyn>EQ), expander, Analog, Input, Phase, LP (forced off) |
| DeEsser | Hush | frequency (×0.81), threshold (+5), Split/Wide, HP/BP, Monitor | range fixed at 12 dB; look-ahead/release stay at defaults |
| CLA-76, CLA-3A, C1, SSLComp, API-2500 | Compressor | nothing (fixed preset: −15 dBFS, 2:1, auto gain) | everything |
| SSL Native Vocalstrip 2 / Channel Strip | Ridge Strip | nothing (neutral) | everything |

## Quell Denoiser ← NS1 Noise Suppressor (Waves)
Sources: docs/specs/ns1.md; REA_rodada119 §3; trial screenshot w119/ref/NS1_Stereo.png (default preset); LYNX
src/core/VoiceTools.cpp (`quellInfo`, `paramText`), PluginLayout.cpp `quellDenoiser()`, lynx-before/Quell Denoiser.png;
third-party control reference josephnilo.com/blog/waves-ns1-explained. Trial on this PC: yes.

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| NS1 fader (the only control) | 0–100 %, tall vertical fader on the right | "Amount" tall vertical slider on the right, 0–100 % | ✅ | same place, same unit |
| Fader default | 0.0 (default preset reads 0.0) | 30 % | ⚠️ | LYNX opens already suppressing; her states are 23–70 % (translated, so only new inserts are affected) |
| Fader scale | 0…100 printed both sides every 10 | round 137: 0 … 100 printed beside the fader every 10 | ✅ | round 137 (one side) |
| Fader read-out | numeric box under the fader ("0.0"), editable | "30.0%" value under the slider, right-click entry | ✅ | |
| Attenuation meter | left, vertical, 0 … −48 dB, ticks every 6 dB both sides | left, vertical, 0 … −30 dB, ticks every 6 dB | ⚠️ | NS1 can show 48 dB; Quell clips its scale at 30 (Quell max reduction ~25 dB so fine for the engine, but the scale differs) |
| Meter read-out | numeric box under the meter | "0.0 dB" under the meter | ✅ | |
| Meter meaning | "overall energy removed from the input" | broadband reduction | ✅ | |
| Meter peak hold / reset | numeric read-out box (Waves convention: click to reset) | round 137: the read-out under the meter holds the deepest reduction; click the meter = reset | ✅ | round 137 |
| Listen (hear the removed noise) | — | toggle bottom-left | ➕ | |
| Output trim | — | −12…+12 dB knob bottom-right | ➕ | |
| Mono / Stereo components | NS1 Mono, NS1 Stereo | one stereo engine, linked | ✅ | LYNX is per-slot stereo; fine |
| Adaptive noise-floor behaviour | "analyzes and adapts in real time"; steady noise drifts by a few dB over seconds | fixed 6-band expander (fitted), no adaptation | ⚠️ | engine, not UI (spec §4.3–4.4; 63 Hz octave +5…7 dB after SSL HPF) |
| Latency | not published; not measured (the first plugin our host loads from the shell is mis-instantiated, so its reported latency is not trusted) | 0 | ? | measure on the trial if it matters |
| Undo / Redo | toolbar | song-level Ctrl+Z only | ⚠️ | |
| Presets (load/save, factory) | toolbar | "(no preset)", no save | ✅ | round 129's preset bar: 4 LYNX factory presets + user presets |
| A/B, Setup A/B, copy A→B | toolbar | — | ✅ | round 129's preset bar |
| ◀ ▶ preset step | toolbar | ◀ ▶ | ✅ | empty list |
| Help / options menu | toolbar ≡ | options menu (reset, bypass) | ✅ | |
| MIDI learn | right-click | right-click MIDI link | ✅ | |
| Default / fine / wheel gestures | Alt-click, Ctrl-drag, wheel | double/Alt-click, Ctrl-drag, wheel | ✅ | |
| Bypass | host / Waves toolbar | LED in header + options | ✅ | |
| Window size | fixed 345×525-ish | resizable, fixed aspect, 300×420 | ✅ | |
| Logo / branding area | top-left logo, product name bottom-left | section caption "Noise suppressor" | ✅ | (trade dress not copied — correct) |

**Scores** — configuration: 8/10, design: 8/10.
**Top gaps**
1. Meter scale 0 … −48 dB (ticks 6 dB) instead of 0 … −30 — `quellDenoiser()` meter item / the grmeter range in
   src/ui/PluginEditors.cpp (`grmeter` scale per item; add a scale field) — UI only, S.
2. Amount default 0 % (or keep 30 % but say so) — `quellInfo()` default; translation unaffected — S (decide; a
   one-knob denoiser that does nothing on insert matches NS1, but LYNX users may prefer an audible default).
3. Fader scale 0…100 with ticks every 10 beside the slider — PluginEditors.cpp vslider drawing (a `scale` style
   flag) — UI, S.
4. Peak-hold on the meter read-out (click to reset) — PluginEditors.cpp grmeter — UI, S.
5. Adaptive noise-floor tracker (spec §7) — VoiceTools.cpp quell — engine, L.
6. (Global) A/B + user preset save — generic editor — M (shared with every engine).

## Gemini Doubler ← Doubler2 / Doubler4 (Waves)
Sources: docs/specs/doubler.md; REA_rodada119 §4; Waves Doubler user guide (assets.wavescdn.com/pdf/plugins/doubler.pdf,
text extracted); trial screenshot w119/ref/Doubler2_Stereo.png; LYNX VoiceTools.cpp (`geminiInfo`, laws),
PluginLayout.cpp `geminiDoubler()`, PluginEditors.cpp `geminipan` / `geminimap` / `geminieq`; lynx-before/Gemini Doubler.png.
Trial on this PC: yes (Doubler2 / Doubler4).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Pan / gain graph (half circle, voices + D marker) | top-left; angle = pan, radius = gain; **"Gain, Pan, Delay and Detune can be controlled using the graphs"** (drag) | `geminipan`, same place, D + 1 + 2 (+ 3 + 4) markers; round 137: drag a marker = its pan (angle) and gain (radius) | ✅ | round 137 |
| Delay / detune graph | top-centre; x 0…100 ms (labels 0 20 40 60 80 100), y +100…−100 c with "100 / 0 / 100" labels; draggable | `geminimap`, x labels 0…100; round 137: +100 / 0 / −100 cent labels, drag a voice = its delay and detune | ✅ | round 137 |
| EQ graph | top-right; L and H markers on the curve, x labels 62 / 250 / 1K / 4K, 0 dB tick; drag markers, **double-click a marker = filter on/off** | `geminieq`; round 137: L and H markers on the curve (drag = frequency / gain, double-click = on / off), labels 62 / 250 / 1K / 4K and the 0 dB tick | ✅ | round 137 |
| Direct on/off | row 2, left; On | toggle "Direct", On | ✅ | |
| Direct gain | 0…−48 dB (manual default −12; trial default 0) | 0…−48 dB, default 0 dB | ✅ | |
| Direct pan / stereo rotation | ±45°, centre | ±45°, rotation law measured | ✅ | |
| Align (direct) | NO/YES button; default Off | toggle, default Off | ✅ | round 165: default Off, as the 17.1 trials |
| FB high-pass | 16…21357 Hz, default 16 ([EXEC] 17.1 trial; the manual says 12 kHz); numeric box with a HP icon | "FB low cut" knob 16 Hz…21.4 kHz, default 16 | ✅ | same range and law |
| Range | 80 Hz / 20 Hz, manual default 20 Hz (trial screenshot shows 80HZ) | tabs 80 Hz / 20 Hz, default 80 | ✅ | |
| Modulation RESET | button, automatable, restarts every LFO in sync | — | ✅ | needs a trigger param + engine reset; round 135w: RESET button (momentary): every voice's LFO restarts in sync |
| Low shelf on / gain / freq | L marker button + gain knob + freq box; gain +12…−24 dB, below −24 "the floor drops to make a cut filter"; default On | L toggle, gain −88…+12 (round 165w), freq 16 Hz–21 kHz; round 165: on, 0 dB at 101 Hz by default (the trial's) | ✅ | [EXEC] round 165w: the 17.1 trial's −88 … +12 dB (linear in the value) and its law — two first-order sections, the fixed corner at f0 / 1.557 (a 12 dB / oct cut below −24): 0.02 dB rms over 38 measured curves (round 124's shelf: 3.6 dB); older projects keep round 124's (EQ law Old \| Measured) |
| High shelf on / gain / freq | H marker + gain + freq box | H toggle, gain −88…+12, freq; round 165: on, +4.1 dB at 1306 Hz by default (the trial's) | ✅ | round 165w: the same law, fixed corner at f0 · 1.557 |
| EQ acts on voices only | yes | yes | ✅ | |
| Voice on/off | coloured numbered buttons 1 / 2 | toggles "1" / "2" | ✅ | |
| Voice gain | 0…−48 dB, default −12 | 0…−48 dB, default −6 | ✅ | default differs slightly |
| Voice pan | ±45°, defaults V1 22.5 / V2 −22.5 (V3 45, V4 −45) | ±45°, defaults −45 / +45 | ✅ | [EXEC] the Doubler2 17.1 trial's defaults are −45 / +45 too (the manual's ±22.5 is older) |
| Voice delay | 0…100 ms, default 8 ms; min 8 / 14 ms when not aligned | 0…100 ms, defaults 9.4 / 23.7 | ✅ | the 8/14 ms minimum not enforced (measured: not enforced by the trial either, spec §4.2) |
| Voice feedback (FDBK) | 0…100 %, default 0, warning > 25 % | 0…100 %, 0 | ✅ | |
| Octaver | ▼ arrow button, octave down | "Oct" toggle | ✅ | |
| Detune | ±100 c | ±100 c | ✅ | |
| Mod depth | ±200 c, positive starts upward | ±200 c | ✅ | |
| Mod rate | 0.1…200 (trial up to 400), default 1.0 | 0.1…400, default 1.0 | ✅ | |
| Value editing in the voice strip | numeric boxes (drag / type); gain & pan as small knobs | knobs everywhere with values under them | ⚠️ | design only; drag/type works on both |
| Per-voice column order | Voice · Gain · Pan · Delay · Fdbk · Oct · Detune · Depth · Rate | 1 · Gain · Pan · Delay · Feedback · Oct · Detune · Depth · Rate · Source | ✅ | same order |
| Source per voice (In 1 / In 2) | bottom-left drop-downs "In 1 L▾", "In 2 R▾" (L / L+R / L−R) | tabs at the end of each voice row | ⚠️ | different place, same options |
| Master output gain | right column, vertical fader 0 … −∞ (scale 0 / 24) | vertical fader −24…0 dB, no scale | ✅ | [EXEC] the 17.1 trial's Master Gain runs −24 … 0 dB (no −∞) |
| Output meters | stereo bar meters beside the fader, scale 0…−24, peak counters under them (click to reset) | — | ✅ | round 129: output meters with peak read-outs beside the fader |
| Clip light | red, click to reset | — | ✅ | round 135w: the meter's OUT label turns red when the output clipped; click the meter to reset |
| Doubler4: voices 3 and 4 | 4 rows, each with its source | 2 voices only | ✅ | engine + params + translation (spec §7 item 6); round 135w: voices 3 / 4 (page "Voices 3-4" of the voice rows; off by default), drawn on the graphs; a Doubler4 state's blocks 46 / 56 are translated [model: same layout as voices 1 / 2; sources 3 / 4 not probed] |
| Components Mono / Mono→Stereo / Stereo | 3 | 1 stereo | ⚠️ | M→S = stereo on a mono-fed track; fine |
| Undo / Redo, presets, A/B, Setup A/B, Save | toolbar | see header note | ✅ | round 129's preset bar |
| MIDI learn, gestures | right-click; Alt-click etc. | yes | ✅ | |
| Window | fixed 800×580 | resizable 800×460 | ✅ | |

**Scores** — configuration: 7/10 for Doubler2 users, 4/10 for Doubler4 users; design: 6/10.

**Round 165** (REA_rodada165h §3): configuration 9 → 9, design 8 → 8 — the 17.1 trials' fresh instance (Align off, both shelves on at their frequencies / gains); rows re-checked against the trials' parameter lists (feedback high-pass range, voice pans, master range match). Left: the shelves' −88 … −24 dB cut range.

**Round 165w** (REA_rodada165w §3): configuration 9 → **10**, design 8 → 8 — the shelves' whole range (−88 … +12 dB) and measured law (two first-order sections; the deep cut is a 12 dB / octave filter), the EQ graph on that scale, deep cuts translated one to one; appended **EQ law** (Old | Measured tabs on the Direct row) so older projects and presets sound the same. Left (design): numeric boxes in the voice strip, the source drop-downs' place.
**Top gaps**
1. Draggable markers in the three graphs (pan/gain, delay/detune, EQ L/H; double-click a shelf marker toggles it) —
   PluginEditors.cpp `geminipan` / `geminimap` / `geminieq` + a display-drag hook in mouseDown/mouseDrag (today only
   `vumeter` handles clicks) — UI only, M.
2. Doubler4 voices 3–4 — VoiceTools.cpp gemini (append V3/V4 params so old projects load unchanged), `geminiDoubler()`
   rows 3–4 (layout grows ~150 px or a 2/4 voice switch), translator `waves-doubler` blocks 46/56 — engine, M.
3. Output meters + clip light beside the output fader — `geminiDoubler()` add `meter(...)`/vumeter items in the Output
   section — UI, S.
4. Modulation Reset button — new trigger param ("Mod reset") + LFO phase reset in gemini::Gemini — engine, S.
5. Graph labels: cents scale on the delay/detune graph, frequency labels + 0 dB tick + L/H markers on the EQ graph —
   PluginEditors.cpp — UI, S.
6. Shelf "cut" below −24 dB and defaults (shelves On, Align Off, pans ±22.5, gain −12) — VoiceTools.cpp — engine +
   defaults, S (defaults must not change translated states: translation sets every value).
7. Output fader to −∞ with a scale — VoiceTools.cpp `outputDb` law + vslider scale — S.

## Echoform ← H-Delay Hybrid Delay (Waves)
Sources: docs/specs/h-delay.md; REA_rodada119 §2; josephnilo.com/blog/waves-h-delay-explained (citing the H-Delay
guide); trial screenshot w119/ref/H-Delay_Stereo.png; LYNX EchoSuite.cpp (`echoformInfo`, laws, `paramText`),
PluginLayout.cpp `echoformLayout()` (ValhallaDelay-style section row), lynx-before/Echoform.png. Trial on this PC: yes.

How an H-Delay user finds things on Echoform today: Delay → "DELAY" big knob (✓, same idea); BPM/HOST/MS → a small
menu "ms|Note|Dotted|Triplet" under the knob (no own-BPM mode); note buttons → note knob + the dotted/triplet menu;
Feedback → small knob in its own section (H-Delay: the second big knob); Mix → top-left "MIX" (H-Delay: top-right
"DRY/WET"); LoFi / Analog → hidden inside the 12-entry MODE menu at the bottom; Ping-pong / Dual → the STYLE knob
(5 styles) + "Pairs"; Phase L/R → nowhere; HiPass / LoPass → "EQ: High / Low" (stacked, reversed order); Mod
depth/rate → "MOD" section, which shows Wow/Flutter in the default Tape mode; output → "OUTPUT: Level"; tap → nowhere;
delay display → an echo-train picture (not the note/BPM read-out).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Delay knob | big, left; 1–3500 ms or note 1/64T … 2 Bar; default 1/8 dotted (Host) | big "Delay" knob in DELAY section; 0–20000 ms (piecewise law) or note 1/64…8/1; default 300 ms | ⚠️ | same role; default ms not a note |
| Note values on one knob | one knob steps triplet / straight / dotted for each length (1/64T … 2 Bar) | note knob (10 lengths) + separate kind menu (Note / Dotted / Triplet) | ⚠️ | two controls for one; reachable |
| Sync HOST | button under the display; default | menu entry "Note" (follows project tempo) | ⚠️ | named differently |
| Sync BPM (own tempo) + BPM value | button + BPM number in the display | — | ✅ | translator folds BPM into ms; round 135w: "Project / Own BPM" + BPM (30–300) on the second row; H-Delay's own-BPM states keep their note and tempo (was folded into ms) |
| Sync MS | button | menu entry "ms" | ✅ | |
| Central display | note value (e.g. "1/8 D") and tempo "120 BPM" (or ms); click-drag / type to edit | value text under the knob + echo-train picture (echoes on a time axis); round 137: the picture's top right reads the note and its tempo ("1/8 D  120.0 BPM") or the ms | ✅ | echotrain ➕; edited with the knob / sync menu, not by dragging the read-out |
| Tap pad | big pad far left; last two taps; Host: snaps to a note, BPM: sets BPM, MS: sets ms | — | ✅ | round 129: TAP pad (sets the left time) |
| Ping Pong | button above the display; Off; start-side value | STYLE = "Ping-pong" (5th of 5) + "Pairs" toggle (alternate / H-Delay pairs) | ⚠️ | no separate button; start side ✅ (next row, round 135w; stale ❌ re-checked round 152) |
| Ping-pong start side (L→R / R→L) | value box | — | ✅ | translator ignores RealWorld 45; round 135w: "Start left / Start right" (ping-pong); translated from RealWorld 45 (0 left, 1 right [EXEC] round 119 probe) |
| Dual (independent L/R times) | button; drag L or R in display | STYLE = "Dual" + Delay R / Sync R | ✅ | |
| Phase L / Phase R (ØL ØR) | two buttons beside Ping Pong / Dual; Off | — | ✅ | new params; round 129 |
| Feedback | big knob right of display, 0–200 %, default 60 | small knob, 0–200 %, default 70 | ⚠️ | size / place / default |
| Dry/Wet | top-right knob 0–100, default 100, equal power | "Mix" top-left, 0–100 %, default 50 %, equal power | ⚠️ | place + default |
| Output | knob −18…+18 dB, default 0 | "Level" −18…+18 dB, default 0 (round 119) | ✅ | |
| Output meter | vertical, 0 … −48 dB, beside Dry/Wet | — | ✅ | round 135w: OUT meter (0 … −48 dB) at the right edge |
| Peak / clip LED | red lamp above the meter | — | ✅ | round 135w: clip lamp above the meter + highest-peak read-out; click to reset |
| LoFi | switch under Tap; Off; steep LP (≈ 5.8 kHz, 8th order+) inside the loop | MODE "Lo-Fi" (a menu entry that also sets the whole character); round 137: LOFI switch (second row), independent of the mode | ✅ | round 137: the in-loop 8th-order low-pass at 5.8 kHz of docs/specs/h-delay.md §4.2 (−22 dB at 8 kHz); the steeper stop band above 8 kHz is not modelled; translated (RealWorld 21) |
| Analog | knob Off / 1 / 2 / 3 / 4, default 2 | MODE menu (Analog, Bucket brigade, Tape …) + Drive / Age | ⚠️ | no 0–4 positions, no hiss / loop compression of Mod2 |
| Mod Depth | 0–100, default 0; bottom-left MODULATION group | "Depth" in MOD section, only in non-tape modes; default 50 % | ⚠️ | default Tape mode shows Wow/Flutter instead |
| Mod Rate | 0.1 … 6000 Hz (BPM sync in concept), default 0.1 | 0.01 … 10 Hz, default ≈ 0.5 Hz | ⚠️ | range / default |
| HiPass | Off, 20 Hz–20 kHz, default Off; bottom FILTERS group (left) | "Low" (EQ section, lower knob) 10 Hz–2 kHz linear | ⚠️ | no Off label, range to 2 kHz, linear taper |
| LoPass | 20 Hz–20 kHz, Off, default Off (right) | "High" (upper knob) 200 Hz–20 kHz | ⚠️ | range, order reversed |
| Filter LINK | button between HP and LP; moves both | — | ✅ | round 135w: "Link EQ": moving the low or the high cut moves the other by the same ratio |
| Components Mono / Stereo / M>S | 3 | one stereo engine + Width | ⚠️ | Width ➕ |
| Delay changes glide (tape-like) | yes | depends on mode | ⚠️ | |
| Loop limiter (fb > 100 % stays bounded; −3 dB loud passes) | engine | Drive/Age shaper in some modes; no loop limiter in Clean | ⚠️ | engine (spec §7 item 1) |
| Style Single / Ratio / Quad | — | STYLE | ➕ | |
| Diffusion amount / size | — | DIFFUSION section | ➕ | |
| Ducking | — | OUTPUT section | ➕ | |
| Width | — | FEEDBACK section | ➕ | |
| 12 modes (Tape, Shifter, Pitch, Reverse, …) | — | MODE menu | ➕ | |
| MIDI learn (all but meter) | right-click | right-click MIDI link | ✅ | |
| Gestures (Alt-click reset, Ctrl fine, wheel, type) | yes | yes | ✅ | |
| Undo / Redo, presets, A/B, Setup A/B, Save | toolbar | see header note | ✅ | round 129's preset bar |
| Window | fixed ~750×290 | resizable 780×300 | ✅ | |

**Scores** — configuration: 5/10, design: 4/10 (the controls exist for ~60 % of H-Delay, but in ValhallaDelay's
places and names; Phase, Tap, own BPM, Link, meter are missing).
**Top gaps**
1. Tap pad — a `tap` button item in `echoformLayout()` (DELAY section) + editor handler (PluginEditors.cpp) that
   averages the last two taps and writes Time L (ms mode) or the nearest Note L/kind (note mode) — UI only, S/M.
2. Phase L / Phase R toggles — append params "Phase L", "Phase R" (polarity of each delay line) in EchoSuite.cpp,
   two toggles in the FEEDBACK or DELAY section; translator RealWorld 8 / 9 — engine (trivial DSP) + UI, S.
3. Own BPM sync — append "Sync source" (host / own BPM) + "BPM" params; ms/note text shows tempo; translator keeps
   the BPM instead of folding it to ms — engine + UI, M.
4. Ping-pong as a button + start side — keep STYLE but add a visible "Ping-pong" toggle that sets Style, append
   "Start side" param; translator RealWorld 45 — engine (swap L/R at the input) + UI, S.
5. Output meter + clip lamp in the OUTPUT section — `echoformLayout()` meter item (output level) — UI, S.
6. LoFi as an independent switch (in-loop ≈ 5.8 kHz 8th-order LP, spec §4.2) and an "Analog 0–4" selector bound
   to the loop limiter / colour / hiss of spec §7 — EchoSuite.cpp — engine, M/L.
7. Filters: HP and LP over 20 Hz–20 kHz (log taper) with "Off" at the ends, HP left / LP right, + LINK — append new
   params (keep the old ones for old projects) — engine + UI, M.
8. A note/tempo read-out block in place of (or above) the echo-train picture: "1/8 D · 120 BPM" / "375 ms" — UI, S.
9. Defaults when inserted fresh (Mix 100 on an FX bus is H-Delay's; LYNX's 50 % suits inserts) — decide; S.

**Waves delays mapped to Fruity Delay 3 (one table)**

| Original | Maps to now | Better LYNX engine | Recommendation |
|---|---|---|---|
| Waves SuperTap (2-Taps / 6-Taps: per tap delay, gain, pan, feedback, filter, pitch mod; tap pad, BPM/ms) | Fruity Delay 3 at FL's default (no translation) | **Fruity Delay Bank** (FL's multi-tap bank, exact engine) for the per-tap layout; **Lattice Mod** (multi-tap with spread/slope/tap gain) as a creative alternative | Move SuperTap → Fruity Delay Bank (taps = SuperTap taps; times/gains/pans translatable once the RealWorld list is read on the trial); keep FD3 only as fallback |
| (H-Delay already → Echoform, round 119) | Echoform | — | keep |
| Other Waves delays seen in the substitute table | none (only SuperTap and H-Delay are listed under `waves =`) | Echoform for Manny Marroquin Delay / Abbey Road tape-style delays if they appear | add rows only when found in her projects |

## LYNX Tune ← Waves Tune Real-Time (Waves)
Sources: docs/specs/waves-tune-real-time.md (v16 user guide [S1], tapers read from the trial in round 120);
REA_rodada120.md; josephnilo.com/blog/waves-tune-real-time-explained (control reference citing the guide); LYNX
LynxTune.cpp (`info`, `paramText`, laws), PluginLayout.cpp `lynxTune()` (Auto-Tune-Pro "auto" workflow, round 120;
re-skinned round 122: `pitchring`, `pitchtrace`), lynx-before/LYNX Tune.png. Trial on this PC: yes — but the
round-129 screenshot (ref/wtune.png) had **not been produced** when this was written (the capture run stopped
after rcomp.png), so the "where" column for Waves Tune RT comes from the guide, not from a picture. Re-check the
positions against wtune.png once it exists.

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Speed | 0.1–800 ms, default 15; piecewise taper (½ travel = 50 ms); in-note drift / vibrato tightness | — (one "RETUNE SPEED" knob only) | ✅ | measured effect small (only < 5 ms), but a WTRT user looks for two time knobs; round 135w: SPEED (0.1–800 ms, the trial's taper, default 15) on the "Real-time" page; it acts with Link off (flattens part of the in-note deviation below ~5 ms, as measured in round 120); translated (RW 20) |
| Note Transition | 0.1–800 ms (0.1 ms steps), default 120 | "RETUNE SPEED" 0–400 ms (quadratic), default 20 ms; translator maps NT → retune (τ = NT, matched ±10 %) | ⚠️ | name, range (800), default; behaviour matched |
| Link (Speed ↔ NT) | Off | — | ✅ | follows from Speed; round 135w: "Link" (on = the retune speed handles the deviations — the old behaviour; translated states keep it on unless Speed < 5 ms) |
| Tolerance Cents | Off–40 c (added to the 50 c switch threshold) | — | ✅ | LYNX has fixed hysteresis; round 135w: TOL. CENTS Off–40 c (added to the half-way point) [model] |
| Tolerance Time | Off–300 ms | — | ✅ | round 135w: TOL. TIME Off–300 ms (a new note must last that long) [model] |
| Vibrato On/Off | On | — | ✅ | (Flex-Tune / Humanize are Auto-Tune concepts, not equivalents); round 135w: "Vibrato" switch (off = flattened) |
| Vibrato Depth | 0–200 %, default 100 | — | ✅ | round 135w: VIB. DEPTH 0–200 % (multiplies Natural vibrato) |
| Correction On/Off | On | slot bypass / header LED | ⚠️ | no in-panel switch |
| Correction % | 1–100 %, default 100 | MIX 0–100 % (dry/wet) | ✅ | not the same: Mix blends dry + shifted (phasey); Correction scales the pitch shift; round 135w: CORRECTION 0–100 % scales the shift (not a dry mix); RW 22 translated [model] |
| Formant | Corrected / Non-Corrected, default Corrected | "FORMANT" toggle, On | ✅ | |
| Formant shift | — | ±7 st | ➕ | |
| Reference pitch | ±100 c (415.3–466.16 Hz), 440 | "A4" knob 430–450 Hz | ⚠️ | range ±40 c only |
| Root | 17 names (sharps and flats) | KEY menu, 12 names (sharps) | ✅ | |
| Scale type | 44 (User, Chromatic, Major, Natural/Harmonic/Melodic minors, Justonic, modes, Blues, Whole tone, Diminished, Bebop, pentatonics, Pythagorean, Maqamat, Indian thaats …) | SCALE menu, 13 + Custom | ⚠️ | her states use Major / Natural Minor only (covered) |
| Range | keyboard span with draggable boundary handles + presets Bass / Baritone / Tenor / Alto / Mezzo / Soprano / Generic; notes outside → **bypassed** | "Input type" menu (Soprano / Alto-Tenor / Low male / Instrument) = detector range | ✅ | round 147b: the range bar over the keys (drag a boundary), seven presets in the keyboard strip (LYNX's spans [model]: classical voice ranges ± a third; they also pick the detector's input type), notes sung outside pass uncorrected |
| Keyboard | multi-octave piano at the bottom, per-note status area | one octave of on/off key toggles at the bottom ("TARGET NOTES") | ✅ | round 128: four octaves; round 147b: 2–7 octaves shown, scrollable (see navigation) |
| Note status per key | Legal / Illegal / Illegal-Pitch-Up / Illegal-Pitch-Down / Bypass (click cycles) | on / off | ✅ | round 128: Bypass mode; round 147b: REMOVE mode cycles legal → illegal → illegal up → illegal down (arrows on the key; an illegal note's direction picks the next legal note up / down) |
| Group Octaves | switch (per-note edits apply to all octaves) | always grouped | ✅ | round 152b: "Group" (octaves) in the keyboard strip (on by default = as before); off = a click edits that key alone (REMOVE cycles its status, BYPASS toggles it; an outline marks a key with its own status), the correction picks targets per key (MIDI 24 … 119, saved in a packed key map); Reset clears it; Send / Receive carry it [MANUAL S1 p.17] |
| Reset Scale / Send Scale to All / Receive (key) | buttons | — (key comes from translation / project key) | ✅ | round 147b: RESET SCALE in the keyboard strip; round 150b: SEND (this key, scale, note statuses / bypasses / directions and range to every other LYNX Tune in the project whose RECEIVE is on; one undo step) and RECEIVE (a saved switch, on by default; switching it on takes the scale sent last in this LYNX session) |
| Keyboard view / navigation buttons | yes | — | ✅ | round 147b: − / + (more / fewer octaves, 2–7) and ◀ / ▶ (scroll by an octave) in the keyboard strip |
| Central display | target note name + correction amount in cents | `pitchring`: note name, ±100 c ring, sung offset + octave | ✅ | |
| Scrolling pitch graph | not in WTRT (guide lists none) | `pitchtrace` (input vs corrected, 8 s) | ➕ | Auto-Tune-style; useful |
| Input / output meters | −28 … 0 dBFS | — | ✅ | round 135w: IN / OUT meters on the "Real-time" page |
| MIDI Target Pitch | MIDI note forces the target (Speed/NT timing) | "MIDI channel" (held pitch classes become the targets) | ⚠️ | pitch class only, no octave |
| MIDI Reference Tone + level | tone generator ±18 dB | — | ✅ | round 152b: "Ref. tone" + level (±18 dB) on the Real-time page — the notes held on the MIDI channel play as sines at the reference pitch (0 dB = −12 dBFS peak per note [model]) [MANUAL S1 p.21] |
| Latency | reports 0; real 0–4 ms dynamic | ~25 ms reported + PDC; "Low latency" toggle | ⚠️ | ➕ toggle; different by design |
| Sound (measured, round 165j, synthetic takes) | latency 5.4 ms, accuracy 2.3 / 12.2 c, retune 29 ms, bursts 18.5 / min, formant 0.26 dB | 25.4 ms (Low latency 6.4), 1.7 / 4.4 c, 38 ms, 17.6 / min, 0.36 dB; highs within 0.1 dB of the dry take | ⚠️ | tighter pitch; slower to land by ~9 ms at the same transition setting; slightly larger envelope change |
| Transpose | — | ±12 st stepper | ➕ | |
| Tracking | — | knob | ➕ | |
| Humanize / Flex-Tune | — | knobs | ➕ | Auto-Tune Pro role |
| Mono / Stereo components (detect on (L+R)/2) | 2 | one stereo slot | ✅ | |
| Undo / Redo, presets (factory/user/favourites, search), A/B, Setup A/B, Save | toolbar | see header note | ✅ | round 129's preset bar (no search / favourites) |
| MIDI learn, gestures | right-click; Alt-click etc. | yes | ✅ | |
| Window | fixed | resizable 800×548 | ✅ | |

**Round 147b:** configuration 8 → 9, design 6 → 7 (the keyboard: note statuses with directions, the range bar and presets, Reset scale, the view's navigation). Remaining: Group Octaves off, Send / Receive, MIDI reference tone.

**Round 150b (after 150):** configuration 9 → 9, design 7 → 8 (Send / Receive in the keyboard strip). Remaining: Group Octaves off, MIDI reference tone.

**Round 152b (after 152):** configuration 9 → 10 (Group Octaves off with per-key statuses, the MIDI reference tone), design 8 → 8. Remaining: the 44 scale types (13 + Custom here), Reference pitch ±100 c, MIDI Target Pitch with octaves.

**Scores** — configuration: 4/10 (for a Waves Tune RT user: key/scale/formant/time are there, but Speed, Tolerance,
Vibrato, Correction %, note statuses and range bypass are not), design: 5/10 (note display centre + keyboard at the
bottom match; the knob row is Auto-Tune's).
**Top gaps**
1. Vibrato On + Depth 0–200 % — LynxTune.cpp (spec §7.6 model: split slow note component / residual FM, scale the
   residual) + two controls in the correction row — engine, M. Needed before Tolerance because a WTRT user's
   quick-start is "Vibrato Off for the hard effect".
2. Correction % (1–100, scales the shift, not a dry mix) — LynxTune.cpp multiply `wanted`; knob next to MIX — engine,
   S (spec §7.7).
3. Tolerance Cents / Time — LynxTune.cpp target hysteresis (50 c + cents, hold time) + two knobs — engine, S/M.
4. Note statuses Up / Down / Bypass on the keyboard (click cycles states) and a multi-octave keyboard with range
   handles that bypass notes outside — params (per-key 0…4 instead of on/off; append, keep old on/off meaning) +
   `lynxTune()` keyboard + PluginEditors.cpp — engine + UI, M/L.
5. Retune range to 800 ms and a second "Speed" knob (in-note, acts only on fast deviations; spec §8.1 to measure
   first) — LynxTune.cpp `retuneMs` (append a new param to keep old states) — engine, M.
6. Reference ±100 c (415–466 Hz) — `referenceHz` law (append a param or remap with translation) — S.
7. Input / output meters (−28…0 dBFS) — `lynxTune()` meter items — UI, S.
8. More scale types (Natural minor named as such, Justonic, Bebop, Whole tone, Diminished, Maqamat …) —
   VocalPitch scale table — engine, S/M; low priority (her states: Major / Natural Minor).

## Other Waves stand-ins (short tables)

**Waves L1 / L2 / L3 / L4 → "Limiter"** (Effects.cpp: Ceiling / Gain / Release, fixed 1.5 ms look-ahead, no panel
of its own — FL "Fruity Limiter" is a different engine)

| Original control | LYNX "Limiter" | Apex Limiter (FabFilter Pro-L role) |
|---|---|---|
| Threshold (L1/L2: 0…−30 dB, drives gain) | Gain 0…+24 dB (inverse of threshold) | Gain |
| Out Ceiling (0…−30 dB) | Ceiling −12…0 dB (default −0.3) | Ceiling (true-peak option) |
| Release (0.01…1000 ms) | Release 10…1000 ms | Release |
| ARC auto release (L2/L3) | — | Style (Clean / Punch / Loud / Safe) ≈ adaptive release |
| Look-ahead (fixed 1.5 ms in L2) | fixed 1.5 ms | Lookahead knob |
| Quantize / Dither type / Noise shaping (L1/L2) | — | — (render-time dither belongs to export) |
| Domain analog/digital (L2), Separation / Profile (L3 multiband) | — | Link (stereo) only |
| Attenuation meter + I/O meters | none | GR trace + meters (display-first editor) |

Recommendation: **map L1 / L2 / L3 / L3-16 / L4 to Apex Limiter**, Threshold → Gain (−threshold), Ceiling →
Ceiling, Release → Release, ARC on → Style "Clean"/"Safe" (adaptive), Lookahead 1.5 ms. L3-LL Multi's 5 bands have
no LYNX multiband limiter (Maximus via FL is the closest; keep Apex as a full-band stand-in). Keep "Limiter" only as
the no-state fallback. Effort S (substitute table row + small translator once the RealWorld lists are read on the
trials).

**Waves RVerb / H-Reverb / TrueVerb → Reeverb 2** (FL's Reeverb 2 clone, `preset Dry=0; Wet=0.8`, slot mix 25 %)

| Original | Key controls | Best LYNX engine | Why |
|---|---|---|---|
| RVerb | Type (Hall / Room / Plate…), Pre-delay, Time, Size, Diffusion, Decay shape, Early ref level, Reverb level, Damping (5-band), EQ (5-band), Dry/Wet | **Atrium** (type / space, early/late, early size, late size, bass/high mult + xovers, predelay, decay, mix) | early/late split, room / hall / plate types and damping multipliers all exist; Reeverb 2 has no type or ER level control beyond "Early ref." |
| H-Reverb | FIR-based hall, Pre-delay, Time, Size, Build-up, Early/late, Damping & EQ, Dynamics (ducking), Modulation, Drive, Output | **Epoch Verb** (predelay, decay, size, attack ≈ build-up, bass mult/xover, high shelf, early/late diffusion, mod rate/depth, colour/mode) | the "Attack"/build-up and colour modes have H-Reverb counterparts; ducking missing everywhere |
| TrueVerb | Room size, Distance (direct/early/late balance), Early reflections, Reverb (time, density, damping), EQ | **Atrium** (room space, early send/size, early/late) | distance = early/late + direct level |

Recommendation: RVerb → Atrium, H-Reverb → Epoch Verb, TrueVerb → Atrium; keep Reeverb 2 only as fallback when no
state is readable. Without a translator, pick a neutral hall preset with Mix = 100 % wet and LYNX slot mix 25 % as
today. Effort S (table) / M (translators after reading the RealWorld lists on the trials).

**Waves Q10 / Renaissance EQ / SSLEQ → Parametric EQ** (FL's Parametric EQ 2 clone, 7 bands)

| Original | Bands / types | Best LYNX engine | Why |
|---|---|---|---|
| Q10 | 1–10 bands (Q1, Q2, Q4, Q6, Q8, Q10 components), each Bell / Shelf / Cut / Notch, gain ±18 dB, Q | **Facet EQ** (8 bands, shape / freq / gain / Q / slope, display-first) — or Parametric EQ when ≤ 7 bands | per-band shapes and slopes; display with the curve; 10-band presets need 2 bands merged or dropped |
| Renaissance EQ (REQ 2 / 4 / 6) | 2/4/6 bands, Bell / Shelf (resonant "Renaissance" shelves) / HP / LP, Gain, Q | **Facet EQ** | 6 bands fit in 8; shelf / cut shapes present |
| SSLEQ (SSL G/E EQ) | HPF, LPF, LF (shelf/bell), LMF (Q), HMF (Q), HF (shelf/bell), E/G curve | **Ridge Strip** (EQ on alone: HPF, LPF, LF/LMF/HMF/HF with bell switches and Q, EQ curve E/G) | identical band structure — the SSL E-Channel already maps there with measured laws (round 113) |

Recommendation: SSLEQ → Ridge Strip with dynamics off (reuse `translateWavesSslChannel`'s EQ part; S), Q10 and REQ
→ Facet EQ (translator per band: type, freq, gain, Q; M). Parametric EQ remains the fallback.

## Epoch Verb ← ValhallaVintageVerb (Valhalla DSP)
Sources: docs/specs/valhalla-vintageverb.md; REA_rodada100.md §1/§7b; product page (valhalladsp.com/shop/reverb/valhalla-vintage-verb/, read from the web.archive.org 2025 snapshot because the live site returns 403); ref ui_ValhallaVintageVerb.png. LYNX: `epochVerbLayout()`, `verbs::info/paramText` (src/core/LynxVerbs.cpp:1258, 1300+), lynx-before/Epoch Verb.png. Trial on this PC: yes (4.0.5 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX | 0–100 %, 100 %, equal-power; top left | same law/default, top left | ✅ | |
| PREDELAY | 0–500 ms skew 2.32, 20 ms; under Mix | same | ✅ | |
| DECAY (big knob) | 0.2–70 s skew 3.36, 4.00 s | same, big knob | ✅ | |
| DAMPING HighFreq | 100–20000 Hz, 6000 Hz | same (reads "6.00 kHz") | ✅ | |
| DAMPING HighShelf | −24…0 dB, −24 dB | same | ✅ | |
| DAMPING BassFreq | 100–10000 Hz skew 4.04, 300 Hz | same, caption "Bass freq" | ✅ | |
| DAMPING BassMult | 0.25–4 X, 1.50 X | same | ✅ | |
| SHAPE Size / Attack | 0–100 %, 100 % / 50 % | same | ✅ | Slow-attack build-up shape is not reproduced (round 100). |
| DIFF Early / Late | 0–100 %, 100 % | same, box named "Diffusion" | ✅ | |
| MOD Rate / Depth | 0.1–10 Hz 2.53 Hz / 0–100 % 38 % | same | ✅ | |
| EQ HighCut / LowCut | 100–20000 Hz 8 kHz / 10–1500 Hz 10 Hz, 6 dB/oct | same | ✅ | |
| MODE menu | 22 algorithms, Concert Hall; bottom left | 22 entries in the same order, own names (Nave, Grit…, Drift…, Gated, Grand Hall, Vintage Chamber/Hall); ◀ ▶ on the menu | ⚠️ | Positions match, so translation is exact. Users won't recognise the names (renaming is deliberate). Gated envelope and per-mode early patterns are approximate. |
| COLOR menu | 1970s / 1980s / Now, 1970s | same names/default | ⚠️ | DSP is only a band limit: no reduced internal rate, no "dark and noisy" quantised modulation (spec §7.4, §7.8). |
| PRESETS row | "PRESETS: Default ◀ ▶" bottom right; click name = menu (factory folders, select/copy/save) | header preset bar: LYNX factory presets in folders, ◀ ▶, user presets (save / delete) | ✅ | Rounds 129 / 135. Her corpus is 89 % two factory presets (spec §1). |
| Preset name of the loaded state | shown in the preset field | the original's `presetName` in the header ("Detuned & Big") | ✅ | round 135 |
| Mix lock | click the MIX label to lock Mix while browsing presets (state `mixLock`) | click the MIX caption (or the preset menu entry); per engine | ✅ | round 135; the saved `mixLock` is not read (LYNX's lock is per engine, not per slot). |
| Per-control description line | one sentence at the bottom of the window for the hovered control (the "manual") | bottom strip: the hovered control's sentence, else "Mode: Plate — a plate: dense from the first moment…" | ✅ | round 135 |
| Header info | version / format / demo note top right | title + insert name | ➕/n/a | |
| Resizable UI | yes (state stores uiWidth/uiHeight) | resizable, fixed aspect | ✅ | |
| Value text entry / reset / fine drag | not documented on the product page | right-click entry, double-/Alt-click reset, Ctrl fine, wheel | ➕ | |
| A/B, undo | none in the original | song-level undo only | ✅ | Nothing missing. |
| Tempo sync | none | none | ✅ | |
| Meters / display | none | none | ✅ | |
| Bypass parameter | Bypass (VST param) | slot on/off + options menu Bypass | ✅ | |
| Value readout under each knob, caption above | yes | yes (round 114 `captionsAbove`) | ✅ | |
| Box grouping and order | Mix/Pre · Decay · DAMPING 2×2 · SHAPE · DIFF · MOD · EQ | identical order and grouping | ✅ | |
| Translation of saved state | — | every parameter (class ID + names, `steps` for Mode/Color) and the preset name | ✅ | mixLock and ui size are dropped. |

**Scores** — configuration: 7/10, design: 8/10. **After round 135**: configuration 9/10, design 9/10 (left: the 1970s / 1980s colour character).
**Top gaps** (1–3 and 5 done in rounds 129 / 135)
1. **Factory/user preset browser for LYNX engines.** LYNX would need its own named starting points plus save/copy, and the existing ◀ ▶ should step through them. Code: src/ui/PluginEditors.cpp (`presetFiles()`, header items 2–4), presets stored per EffectKind. UI + data only; shared by all six engines. **M–L**
2. **Show the translated state's preset name.** Keep `presetName` from the XML in the substitute and show it in the header box. Code: PluginSubstitutes.cpp + PluginEditors.cpp `presetName`. UI/data only. **S**
3. **Per-control description line.** Add a `help` text to EffectParamInfo (LynxVerbs.cpp `info`) and show it in the hint bar, or in a footer line inside the panel the way the reference does. Code: PluginEditors.cpp `hintAt`. UI only. **M** (writing about 100 texts across the six engines)
4. **Colour character for 1970s/1980s.** Quantised, low-rate modulation and a half-rate tank for 1970s. Code: LynxVerbs.cpp EpochFx `configure` plus the engine's mod read. Engine/DSP. **M**
5. **Mix lock.** A toggle on the Mix caption, stored in the effect state and read from `mixLock`. Code: PluginEditors.cpp + PluginSubstitutes. UI only, once presets exist. **S**

## Atrium ← ValhallaRoom (Valhalla DSP)
Sources: REA_rodada100.md §2/§7b; product page (valhalladsp.com/shop/reverb/valhalla-room/, archive snapshot); ref ui_ValhallaRoom.png. LYNX: `atriumLayout()`, lynx-before/Atrium.png. Trial on this PC: yes (2.0.5 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX fader | 0–100 %, 100 % | vertical fader, 100 % | ✅ | |
| PREDELAY fader | 0–500 ms, 10 ms; fader sits at ~30 % height at 10 ms | 0–500 ms linear, 10 ms sits near the bottom | ⚠️ | The reference fader is drawn with a skewed taper. In LYNX, small predelays are hard to set by drag. |
| DECAY fader | 0.1–100 s (param linear), 2.00 s at ~35 % height | LYNX's own law, 2 s at the centre | ⚠️ | Translation uses a `curve`, but automation recorded on the original won't line up 1:1. |
| LO CUT fader | 0–1000 Hz, 0 Hz | same | ✅ | |
| HI CUT fader | 100–15000 Hz, 8000 Hz, 2nd order | same | ✅ | |
| DEPTH fader (early/late) | 0–100 %, 50 %; caption "DEPTH" | same value, caption "Depth" | ✅ | round 129 |
| Fader captions | above the faders | below the faders (values under them) | ⚠️ | Minor. |
| EARLY / LATE tabs | two tabs switch the knob grid (state `earlyLateTabState`) | Early / Late page tabs | ✅ | round 129; `earlyLateTabState` is not read |
| Early Size | 1–1000 ms, 30.0 ms | same | ✅ | |
| Early Cross | 0–1, 0.10 | same | ✅ | |
| Early Mod Rate / Depth | 0.05–5 Hz 0.50 Hz / 0–1 0.00 | same | ✅ | |
| Early Send | 0–1, 0.00 | same | ✅ | |
| Diffusion | 0–1, 1.00 | same | ✅ | |
| Space (2.0) | 0–100 %, 0 %; feedback around predelay + early | same, in Early | ✅ | |
| Late Size / Cross | 0–1, 0.5 / 1.0 | same | ✅ | |
| Late Mod Rate / Depth | 0.05–5 Hz / 0–1 | same | ✅ | |
| Bass Mult / Xover | 0.5–2 X @ 100–10000 Hz, 1 X @ 1 kHz | same | ✅ | |
| High Mult / Xover | 0.1–1 X @ 100–15000 Hz, 0.5 X @ 8 kHz | same | ✅ | |
| REVERB MODE menu | 12 types, Large Room | 12 in the same order; last four renamed (Deep/Mirror/Vast/Distant Space) | ⚠️ | Dark types are band-limited only to −20 dB at 16 kHz (reference −60) and there is a 250 Hz dip (round 100 residuals). |
| PRESET row | "PRESET: Default ◀ ▶" (factory folders incl. "Space" folder, save/copy) | header preset bar (LYNX factory + user presets, ◀ ▶) and the original's preset name | ✅ | rounds 129 / 135 |
| GUI Mode (colour schemes Electric Blue / Old School) | top-right selector | LYNX theme only | n/a | Trade dress; do not copy. |
| Description line | bottom of the window | bottom strip (hovered control, else the type) | ✅ | round 135 |
| Mix lock | state `mixLock` (MIX label is highlighted) | click the MIX caption: boxed while locked | ✅ | round 135 |
| Resizable | "Resizable GUI" | yes, fixed aspect | ✅ | |
| Gestures / text entry | not documented | LYNX's full set | ➕ | |
| True-stereo routing | stereo in/out | stereo | ✅ | |
| Translation | — | every parameter; decay through `curve`; the preset name (round 135) | ✅ | earlyLateTabState and mixLock are dropped. |

**Scores** — configuration: 7/10, design: 6/10. **After round 135**: configuration 9/10, design 8/10 (left: fader tapers, the dark types' band limit).
**Top gaps** (1, 2, 4, 5 done in rounds 129 / 135)
1. **Early/Late page tabs.** Put the two groups on `page` 0/1 with `pageTabs`, and optionally restore the page from `earlyLateTabState`. Code: PluginLayout.cpp `atriumLayout()`. UI only. **S**
2. **Preset browser and translated preset name.** Same work as Epoch gaps 1–2. **M–L / S**
3. **Display taper on the Predelay/Decay faders**, so the default and small values sit where they do on the original. Use a slider skew in the layout item while keeping the parameter law. Code: PluginLayout.cpp / PluginEditors.cpp slider mapping. UI only. **S**
4. **Fader captions above and "Depth" wording.** Code: `atriumLayout()`. UI only. **S**
5. **Description line.** Same as Epoch gap 3. **M**
6. **Dark-type band limit and the 250 Hz dip.** Code: `atriumTypes` table in LynxVerbs.cpp. Engine tuning. **S–M**

## Foil Plate ← ValhallaPlate (Valhalla DSP)
Sources: REA_rodada100.md §3/§7b; product page (valhalladsp.com/shop/reverb/valhalla-plate/, archive snapshot); ref ui_ValhallaPlate.png. LYNX: `foilPlateLayout()`, lynx-before/Foil Plate.png. Trial on this PC: yes (1.6.3 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX | 0–100 %, 100 % | same | ✅ | |
| PREDELAY | 0–500 ms skew, 0.0 ms | same | ✅ | |
| DECAY (very large knob) | 0.5–30 s piecewise, 3.0 s | same law, big knob | ✅ | |
| SIZE | 0–200 %, 100 % | same | ✅ | |
| WIDTH | 0–200 % (0 = mono, 200 = anti-correlated), 100 % | same | ✅ | |
| EQ LowFreq | 20–2000 Hz, **700 Hz** | 700 Hz | ✅ | round 129 |
| EQ HighFreq | 200–20000 Hz, **10000 Hz** | 10 kHz | ✅ | round 129 |
| EQ LowGain / HighGain | ±12 dB, 0 dB | same | ✅ | |
| MOD Rate | 0.05–5 Hz skew 2.38, 0.50 Hz | same | ✅ | |
| MOD Depth | 0–100 %, 0 % | same | ✅ | |
| MODE menu | 12 plate types, Chrome | 12 in the same order, own metal names; ◀ ▶ | ⚠️ | Positions match. Per-type onsets were fitted. Mono-in types (Adamantium/Titanium/Osmium) are not modelled as mono-in. |
| PRESETS row | "PRESETS: Default ◀ ▶" | header preset bar + the original's preset name | ✅ | rounds 129 / 135 |
| Description line | bottom | bottom strip (hovered control, else the plate type) | ✅ | round 135 |
| Mix lock | state `mixLock` | click the MIX caption | ✅ | round 135 |
| Resizable | yes (uiWidth/uiHeight in state) | yes | ✅ | |
| Layout order | Mix/Pre · Decay · Size/Width · EQ 2×2 · MOD · Mode | identical | ✅ | |
| Value readouts / captions | above/below | same | ✅ | |
| Gestures | not documented | full set | ➕ | |
| Translation | — | every parameter (Type `steps`) | ✅ | |

**Scores** — configuration: 8/10, design: 9/10. **After round 135**: configuration 9/10, design 10/10 (left: the mono-in types).
**Top gaps** (1–3 done in rounds 129 / 135)
1. **Preset browser and preset name.** Same work as Epoch. **M–L / S**
2. **Fix the EQ frequency defaults** to 700 Hz and 10 kHz (0.343434 / 0.494949). Code: LynxVerbs.cpp `foilInfo`. Data only. **S**
3. **Description line.** **M**
4. **Mono-in behaviour for the three mono-in types.** Code: FoilFx `configure` (sum the input). Engine. **S**

## Nebula ← Valhalla Supermassive (Valhalla DSP)
Sources: REA_rodada100.md §4/§7b; product page with its control tooltips (valhalladsp.com/shop/reverb/valhalla-supermassive/, archive snapshot); ref ui_ValhallaSupermassive.png. LYNX: `nebulaLayout()`, lynx-before/Nebula.png. Trial on this PC: yes (5.0.0, free).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX | 0–100 %, 50 % | same | ✅ | |
| MIX lock | click the boxed MIX label = lock/unlock (documented) | click the MIX caption: boxed in the accent colour while locked | ✅ | round 135 |
| WIDTH | −100…+100 % (negative swaps L/R), 100 % | same | ✅ | |
| DELAY (big) | 0–2000 ms piecewise, 300.0 ms | same | ✅ | |
| Delay sync combo | Msec / Note / Dotted / Triplet as a combo under Delay; note values 1/64…4/4 (7) | 4 tabs + a separate always-visible note menu (1/64…1/1) | ⚠️ | Same options. In the original the sync setting changes what the Delay knob sets; LYNX shows both controls at once. |
| WARP (big) | 0–100 %, 0 % | same | ✅ | |
| CLEAR button | under Warp; flushes the delay network | CLEAR push button under Warp | ✅ | round 129; not translated (it is a button). |
| FEEDBACK | 0–100 %, 50 % (gain per pass = √fb) | same | ✅ | |
| DENSITY | 0–100 %, 0 % | same | ✅ | |
| MOD Rate / Depth | 0.01–10 Hz skew 2.33, 0.50 Hz / 50 % | same | ✅ | |
| EQ High / Low | 200–20000 Hz 20 kHz / 10–2000 Hz 10 Hz, 6 dB/oct output | same | ✅ | |
| MODE menu + ◀ ▶ (5.0) | 22 modes, Gemini | 22 in the same order, own names; ◀ ▶ | ⚠️ | Density-50 % decay and ×8 shapes at long delays are approximate. |
| PRESET row | "PRESET: Default ◀ ▶"; click = select/copy/save | header preset bar + the original's preset name | ✅ | rounds 129 / 135 |
| Description line | bottom left | bottom strip (hovered control, else the mode) | ✅ | round 135 |
| Resizable | yes (uiWidth/uiHeight) | yes | ✅ | |
| Layout | Mix/Width · Delay+Warp box with sync/Clear · Fb/Density · MOD · EQ | same order | ✅ | Clear is missing. |
| Gestures | not documented | full set | ➕ | |
| Translation | — | every parameter except Clear; Sync/Note/Mode `steps` | ✅ | |

**Scores** — configuration: 7/10, design: 8/10. **After round 135**: configuration 9/10, design 9/10 (left: sync as one combo).
**Top gaps** (1–3 and 5 done in rounds 129 / 135)
1. **Clear button.** A momentary button in `nebulaLayout()` that calls the effect's buffer flush (`VerbEffect` already has reset/clear). Code: PluginLayout.cpp + PluginEditors.cpp action + LynxVerbs.cpp. Small engine hook. **S**
2. **Mix lock on the MIX caption.** **S**
3. **Preset browser and preset name.** **M–L / S**
4. **Sync as a single combo**, with the Delay knob showing and setting note values when not in Msec. The Note menu would then be hidden or merged. Code: `nebulaLayout()` + paramText. UI only (no parameter-conditional visibility exists in Layout yet). **S–M**
5. **Description line.** **M**

## Halo ← ValhallaShimmer (Valhalla DSP)
Sources: REA_rodada100.md §5/§7b; product page with mode descriptions (valhalladsp.com/shop/reverb/valhalla-shimmer/, archive snapshot); ref ui_ValhallaShimmer.png. LYNX: `haloLayout()`, HaloFx (LynxVerbs.cpp:997–1040), lynx-before/Halo.png. Trial on this PC: yes (1.2.2 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Row of 9 knobs | mix, shift, feedback, diffusion, size, low cut, high cut, mod rate, mod depth | same order, one row | ✅ | |
| mix | shown as 0.50 (raw 0–1) | 50 % | ➕ | LYNX shows units. |
| shift | ±12 st, shown 12.00 | +12.0 st | ✅ | |
| feedback | 0–1, 0.000 | 0 % | ✅ | |
| diffusion | 0–1, 0.910 | 92 % (0.919) | ⚠️ | Tiny default difference. |
| size | 0–1, 0.50 (sets decay, about 1.5 + 12.5·size s) | 50 % | ✅ | No seconds readout in either. |
| low cut | starts at 10 Hz, default 10 | the display shows what the engine applies (0–250 Hz) | ⚠️ | round 129 fixed the mismatch; the range still differs from the reference's |
| high cut | 100–15000 Hz, 8000 | same | ✅ | |
| mod rate | default shown 0.59 | 0.55 Hz (law not confirmed) | ⚠️ | |
| mod depth | 0.30 | 30 % | ✅ | |
| reverb mode | **4: Mono / Small Stereo / Medium Stereo / Big Stereo**, default mono (mono-in for Mono) | round 135: Mono (mono in), Small / Medium / Big stereo (two separate sides) first, LYNX's Wide / Dense / Bright after them; menu with ◀ ▶ | ✅ | [EXEC] the trial with a left-only impulse: below 0.25 the input is summed and the tail leaves both sides, from 0.25 up the left input stays left; steps at 0.25 / 0.5 / 0.75; translated 1:1. The default stays Wide (older projects unchanged). |
| pitch mode | **Single (up or down by Shift) / Dual / SingleReverse / DualReverse / Bypass**, default single | round 135: caption "Pitch mode", Single / Dual / Single, bright / Dual, bright / No shift | ⚠️ | The reverse-grain modes are still approximated by a +4 dB shelf (engine). |
| color mode | Bright / Dark (Dark = steep loss above ~10 kHz) | Bright / Dark (5 kHz loop band limit) | ✅/⚠️ | Dark is darker than described. |
| Programs | 8 host programs (Program param, `currentProgram`) | LYNX's own factory presets in the preset bar | — | round 137: deliberately left out — the programs are the vendor's presets (the third-party rule: never copy their presets); Halo's own factory presets play that role |
| Description line | none visible in 1.2.2's UI (the page says tooltips) | hint bar | ✅ | |
| Resizable | not in 1.2.2 (fixed small UI) | resizable | ➕ | |
| Gestures | not documented | full set | ➕ | |
| Translation | — | parameter0–11; modes through `steps` (Reverb mode 4, Shift mode 5, Color 2) | ✅ | round 135: the four reverb modes measured and kept apart. Program (`currentProgram`) is not read. |

**Scores** — configuration: 6/10, design: 8/10. **After round 135**: configuration 8/10, design 9/10 (left: reverse-grain pitch modes, programs).
**Top gaps** (1, 3, 4 done in rounds 129 / 135)
1. **Four reverb modes in the original's order** (mono-in, then three stereo sizes; LYNX's own names, e.g. "Mono / Small / Medium / Large"). Re-measure the breakpoints (likely k/4) and update the `steps` map. Code: LynxVerbs.cpp `haloModes`, `haloLayout()`, PluginSubstitutes.cpp `[valhalla-shimmer]`. Engine + data. **M**
2. **Pitch modes that match the original's behaviour.** Single (sign of Shift), Dual, reversed-grain Single/Dual (reverse each grain before shifting in the two-head shifter), Bypass. Code: HaloFx `configure` + shifter. Engine/DSP. **M**
3. **Fix the low-cut display/DSP mismatch** so both use the same law. Code: HaloFx `configure` line `s.outLowCut = 250.0 * p[5]` vs `paramText` case 5. **S**
4. **Mode menus with ◀ ▶** (the Reverb mode button is flat), and caption "Pitch mode". Code: `haloLayout()`. UI only. **S**
5. **Programs / presets.** Same as the shared preset work. **M–L**

## Horizon ← ValhallaFutureVerb (Valhalla DSP)
Sources: REA_rodada100.md §6/§7b; product page and mode descriptions (valhalladsp.com/shop/reverb/valhallafutureverb/, archive snapshot); user-manual PDF found but not readable here (equipboard copy, no PDF tool installed); ref ui_ValhallaFutureVerb.png. LYNX: `horizonLayout()`, HorizonFx (LynxVerbs.cpp:1053–1230), lynx-before/Horizon.png. Trial on this PC: yes (1.0.2 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX / WIDTH | 0–100 % 50 % / width 100 % | same | ✅ | |
| Mix lock | boxed MIX label (lock) | click the MIX caption: boxed while locked | ✅ | round 135 |
| ECHO Delay (big) | 0–2000 ms piecewise, 30.0 ms | same | ✅ | |
| Echo sync | "Msec ⇕" combo under Delay (ms / note / dotted / triplet) | tabs + note menu (1/64…1/2, 6 values) | ✅ | round 137 [EXEC] the demo's EchoNote texts: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2 in sixths of the range — the same list and steps as LYNX's menu / translator |
| Feedback / Level | 0–100 %, 0 % / 0 % | same | ✅ | |
| Drive | 0–24 dB, 3.0 dB | same | ✅ | |
| Spread | 0–100 %, 0 % | same | ✅ | |
| Detune | ±30 ct; **shown only in echo modes that use it** | round 135: in Drive's place in the pitch modes (Detune … Swarm), as on the reference | ✅ | [EXEC] the trial's editor in every echo mode: Drive in Modern / Tape / Digital / Analog (/ LoFi), Detune in Detune, Reverse, the three ReverseOct modes, Sparkle and Swarm |
| Mode-dependent visibility | "Only the parameters that are active in a given ECHO MODE will be visible" | round 135: Drive ↔ Detune by echo mode, Early / late ↔ Attack by reverb mode | ✅ | `LayoutItem::whenParam` conditions |
| REVERB Decay (big) | 0.2–70 s piecewise, 3.00 s | same | ✅ | |
| Echo→Rev routing | toggle button under Decay | 2 tabs under Decay | ✅ | |
| Size / Level | 50 % / 100 % | same | ✅ | |
| Early/Late / Density | 50 % / 100 % | same | ✅ | |
| Attack | exists; not visible in Chamber (mode-dependent) | round 135: in Early / late's place in Space and Freeze, as on the reference | ✅ | [EXEC] the trial's editor in every reverb mode (Nonlin not captured; it keeps Early / late) |
| MOD Rate / Depth | 0.01–10 Hz 0.10 Hz / 40 % | same | ✅ | |
| EQ HighCut / LowCut | 1000–20000 Hz 4500 Hz / 20–1000 Hz 100 Hz; they also set the high/low **decay** (loop) | same ranges/defaults; output filters only | ⚠️ | Missing the in-loop RT part (measured in round 100; the vendor says Low Freq EQ controls the bass decay of Chamber/Plate/Cathedral). |
| ECHO MODE | 12: Modern, Tape, Digital, Analog, Detune, Reverse, ReverseOctUp/Down/UpDown, Sparkle, Swarm, LoFi; default Tape | 12 in the same order (Clean…Lo-fi), default Tape, ◀ ▶ | ⚠️ | Behaviour is LYNX's interpretation (not measured): Digital 12-bit companding, Reverse 2–8 grains by Spread, Sparkle long grains. |
| REVERB MODE | 8: Room, Chamber, Plate, Hall, Cathedral, Space, Frozen, Nonlin; default Chamber | 8 in the same order (Freeze, Gated), default Chamber | ✅ | |
| COLOR | Bright / Neutral (HF RT capped 1–1.5 s) / Dark (steep 8 kHz) / Studio (steep 10 kHz + steep 600 Hz low cut); default Neutral | Bright / Neutral (16 kHz cut) / Dark (11 kHz) / "Vintage" (9 kHz + 250 Hz, gentle) | ⚠️ | The cut frequencies and slopes differ from the documented ones. |
| Preset bar | top centre "◀ Default Preset ▶" | header preset bar + the original's preset name | ✅ | rounds 129 / 135 |
| Description line | bottom | bottom strip (hovered control, else the reverb mode; [EXEC] the reference's strip reads e.g. "Modern: a clean predelay") | ✅ | round 135 |
| Resizable | yes (uiWidth/uiHeight) | yes | ✅ | |
| Gestures | not documented | full set | ➕ | |
| Layout | Mix/Width · ECHO box · REVERB box · MOD · EQ · mode row | same order | ✅ | |
| Translation | — | every parameter (modes `steps`, routing) | ✅ | |

**Scores** — configuration: 6/10, design: 7/10. **After round 135**: configuration 8/10, design 9/10 (left: colour definitions, EQ in the loop).
**Top gaps** (1, 4, 5 done in rounds 129 / 135)
1. **Preset bar.** Same as the shared preset work. **M–L**
2. **Colour modes to the documented values**: Dark = steep 8 kHz; the 4th colour = steep 10 kHz plus steep 600 Hz high-pass; Neutral = an HF RT cap (≈ 1–1.5 s) instead of an output cut. Code: LynxVerbs.cpp `horizonColourCut` / HorizonFx `configure`. Engine. **S**
3. **EQ in the loop.** Low/High EQ should also scale the low- and high-band RT. Code: HorizonFx `configure` (the absorption bands already exist). Engine. **S–M**
4. **Echo-mode / reverb-mode dependent visibility** (Detune, Attack, Drive…). Needs a `visibleWhen param/value` field in Layout items. Code: PluginLayout.h/.cpp + PluginEditors.cpp. UI framework change. **M**
5. **Mix lock, description line.** **S / M**

---
**Cross-cutting findings (all six):**
- **Shared gaps.** No preset browser or preset name, no mix lock, and no per-control description line. These are the first things a Valhalla user touches. A/B, undo, meters and oversampling are absent in the originals too, so they are not gaps.
- **Measured-value checks.** Every measured parameter law and almost every default is identical. The only defaults that are off are Foil Plate's EQ frequencies (711 Hz vs 700 Hz, 10.1 kHz vs 10 kHz).
- **The real configuration differences:**
  - Halo's mode lists (Shimmer has 4 reverb modes and reverse-grain pitch modes).
  - Halo's low-cut display/DSP bug.
  - Horizon's colour definitions and loop EQ.
  - Nebula's missing Clear button.
- **Translation.** Every saved value maps to the substitute, but `mixLock`, `presetName`, ui size, Room's tab state and Shimmer's program are dropped. Shimmer's 4 reverb modes collapse into 3.
- **Web access.** The valhalladsp.com pages return a Cloudflare 403, so they were read from web.archive.org snapshots via curl.

Sources: [VintageVerb](https://valhalladsp.com/shop/reverb/valhalla-vintage-verb/), [Supermassive](https://valhalladsp.com/shop/reverb/valhalla-supermassive/), [Room](https://valhalladsp.com/shop/reverb/valhalla-room/), [Plate](https://valhalladsp.com/shop/reverb/valhalla-plate/), [Shimmer](https://valhalladsp.com/shop/reverb/valhalla-shimmer/), [FutureVerb](https://valhalladsp.com/shop/reverb/valhallafutureverb/), [FutureVerb Reverb Modes](https://valhalladsp.com/2025/10/03/valhallafutureverb-the-reverb-modes/), [FutureVerb manual PDF](https://images.equipboard.com/uploads/item/manual/159855/valhalla-futureverb-manual.pdf) (downloaded, not parsed).

## Echoform ← ValhallaDelay (Valhalla DSP)
**Sources:** docs/analise/REA_rodada101.md §1–3 and §8 (our black-box measurements); [ValhallaDelay: The Controls](https://valhalladsp.com/2019/04/16/valhalladelay-the-controls/), [The STYLE Control](https://valhalladsp.com/2019/04/16/valhalladelay-the-style-control/), [1.5.2 update: three new modes and ducking](https://valhalladsp.com/2019/11/26/valhalladelay-update-three-new-modes-and-ducking/) (the pages were read through archive.org, because the live site returns 403); reference screenshots `valhd101/ref/delay*.png` (Single, Quad, Ratio, Dual, Note, Digital, Ghost, Pitch); LYNX `lynx-before/Echoform.png`; `echoformLayout()` and `echosuite::echoformInfo()` / `paramText()`. **Trial on this PC:** yes (3.0.5 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Mix | 0–100 %, 50 %, equal-power law; top of the left box | Mix 0–100 %, 50 %, equal-power, same place | ✅ | |
| Mix lock | Click the "MIX" label to lock Mix while browsing presets | — | ✅ | Only useful once presets exist; round 129: "Keep the Mix when loading presets" in the preset menu |
| Style | Single / Dual / Ratio / PingPong / Quad knob under Mix, default Single | Style knob with 5 steps, same place; order Single / Ratio / Quad / Dual / Ping-pong; the substitute reorders | ✅ | The knob order is different, so turning it gives a different sequence than the original |
| Style changes which controls are shown | Spread / Ratio / Spacing / Delay R swap in place | Same swaps (`when` items) | ✅ | |
| Delay time | 0–20 000 ms, piecewise law, default 300 ms, big knob | Same law and default | ✅ | |
| Delay label L / R in Dual and PingPong | Captions read "DELAY L" and "DELAY R" | Captions read "Delay L" and "Delay R" in Dual / Ping-pong | ✅ | round 129 (row re-checked in round 137) |
| Sync combo under the time knob | Msec / Note / Dotted / Triplet; "DELAY Sync" becomes L and R in Dual / PingPong | Same 4 choices, same place, R menu only in Dual / Ping-pong | ✅ | |
| Note value | 1/64 … 8/1 (10 values); the knob turns into a note knob | Same 10 values; note knob replaces the time knob | ✅ | |
| Spread (Single) | ±20 ms, 0 ms | Same, ±¼ applied per side as measured | ✅ | |
| Ratio (Ratio) | 1–100 %, 61.8 % | Same | ✅ | |
| Spacing (Quad) | ±100 %, 0 % | Same | ✅ | |
| REPEAT / SWELL button (Quad) | Feedback from TapD (Repeat) or from all active taps (Swell) | — (always takes feedback from the last tap) | ✅ | Needs an engine change; round 135w: Repeat / Swell (Quad): feedback from the last tap or the mean of the taps heard [model] |
| TapA / B / C / D on/off (Quad) | 4 buttons under the time knob, greyed out in the other styles | — (all 4 taps always on) | ✅ | Needs an engine change; round 135w: Taps A B C D (Quad, second row) |
| Feedback | 0–200 %, 70 % | Same | ✅ | |
| Width | ±100 %, 100 % | Same | ✅ | |
| Color: Drive | 0–24 dB, 0 dB | Same | ✅ | |
| Color: Age | 0–100 %, 50 %; meaning depends on mode (asperity, splice, BBD noise, bit depth) | Age 50 %, per-character meaning | ⚠️ | Not every mode-specific Age effect is modelled (e.g. tape splices, bit-depth Age in Pitch) |
| Diff Amount | 0–100 %, 0 = OFF | Same, shows "off" | ✅ | |
| Diff Size | 0–100 %, 100 % | Same | ✅ | |
| Mod: Wow / Flutter (tape modes) | 0–100 %, 50 / 50 | Same, shown in Tape only | ✅ | Original also shows them in DuckTape and ChromeTape; LYNX maps both to Tape |
| Mod: Rate / Depth (HiFi / BBD / Digital) | 0.01–10 Hz, 0.50 Hz; 0–100 %, 50 % | Same values, shown for Lo-Fi … Phaser | ✅ | |
| Section title changes to FREQ (Ghost) / PITCH (Pitch) | The section header is renamed | Mod / Freq / Pitch by mode | ✅ | round 129 (row re-checked in round 137) |
| Freq Shift / Detune (Ghost) | ±1000 Hz (\|x\|^5), 0 Hz; ±10 Hz, 0.10 Hz | Same laws and defaults (Shifter mode) | ✅ | |
| Pitch Shift / Detune (Pitch / RevPitch) | ±24 st, 0; ±100 c, 0 | Same (Pitch, Reverse) | ✅ | |
| EQ High / Low | 200–20 000 Hz, 20 kHz; 10–2000 Hz, 10 Hz; in the feedback path | Same, High above Low | ✅ | |
| Ducking | 0–100 % one-knob ducking (feedback + output); comes with the duck modes | Ducking knob in an "Output" section, active in every mode; round 137: the original's curve | ✅ | round 137 [EXEC] measured on the demo (1 kHz tone at −29 / −15 / −9 dBFS rms, ducking 25–100 %): a gain reduction on the echoes keyed by the input = input rms − T, T = −51 dB × ducking^1.28, released at ≈ 85 dB/s; LYNX uses that law (REA_rodada137 §5) |
| Mode list | 17 modes: Tape, HiFi, BBD, Digital, Ghost, Pitch, RevPitch, BBDuck, Clarity, DuckTape, PitchDuck, LoFi, Quartz, PhaserDDL, RichPitch, ChromeTape, Analog | 12 characters | ⚠️ | BBDuck → bucket (loses brightness that follows the delay time), Clarity / Quartz → Clean (loses Clarity's limiter and Era waveforms), DuckTape / ChromeTape → Tape, PitchDuck → Pitch; built-in ducking lost |
| Mode selector with ◀ ▶ | Bottom left, "MODE: Tape ◀▶" | Bottom left menu with ◀ ▶ | ✅ | |
| Era | Past / Present / Future, ◀ ▶, bottom middle; changes filters, saturation, diffusion and Clarity's waveforms | round 137: Past / Present / Future tabs, bottom right; default Past (the old sound) | ✅ | [EXEC] the demo's default is Past (value 1/3; Present from 2/3, Future at 1); the first repeat's top end opens with the era (Tape 8 kHz −10.1 / −7.1 / −6.5 dB, Digital 12 kHz −45 / −6.6 / −6.3, LoFi 8 kHz −10 / −5.3 / −1.8, BBD 4 kHz −27 / −17 / −8; HiFi and Clarity unchanged): LYNX scales each character's loop low-pass by the fitted factor; the era's saturation / diffusion differences are not modelled |
| Colour theme follows the mode | The whole panel recolours per mode family (green tape, blue digital, grey Ghost, navy Pitch) | Fixed theme | ⚠️ | Design cue only; a LYNX accent per mode family would do, without copying the original's colours |
| Preset browser | "PRESET: Default ◀▶" bottom right, large factory library | Header box shows "(no preset)" | ✅ | Applies to all LYNX engines; round 129's preset bar (LYNX factory presets for Echoform) |
| Description line | Bottom strip describes the current Style / Mode, and the hovered control | bottom strip (hovered control, else the mode) | ✅ | round 135 (row re-checked in round 137) |
| Text entry / reset gestures | Not confirmed in our refs | Right-click value entry, double-click / Alt-click default, Ctrl fine drag, wheel | ✅ | |
| Resizable window | Resize corner; uiWidth / uiHeight saved in the state | Resizable, fixed aspect ratio | ✅ | uiWidth / uiHeight not translated (harmless) |
| Output level | — | Level ±18 dB | ➕ | |
| Ping-pong "Pairs" toggle | — | Pairs / alternate | ➕ | |
| Echo-train display | — | Echoes on a time axis | ➕ | |
| Saved-state translation | — | Translates everything (round 137: + Era, TapA–D, RepeatSwell); 17 modes → 12 characters; duck variants lose built-in ducking | ⚠️ | `PluginSubstitutes.cpp [valhalla-delay]` |

**Scores:** configuration 6/10, design 6/10.

**Top gaps:**
1. **Era selector** (bottom row, between Mode and Preset). Needs a new `Era` parameter in `echoformInfo()`, per-character voicing in `Echoform::configure()` and a map line for `xml:Era`. Engine change, L.
2. **Quad TapA–D and Repeat/Swell buttons** under the time knob, shown only in Quad. Needs 5 new parameters, a tap mask in the quad read loop and Swell feedback (sum of active taps) in `EchoSuite.cpp` around lines 565 and 618. Also map TapA–D and RepeatSwell. Engine change, M.
3. **Description strip** under the bottom row: a one-line text for the current style and mode, plus the hovered control's hint. Add it in `PluginEditors.cpp`; it could become a generic "info line" layout item. UI only, S–M.
4. **Missing modes**: give BBDuck, Clarity (limiter), DuckTape / PitchDuck (built-in ducking) and Quartz their own characters, and update the map's `steps`. Engine change, M–L.
5. **Ducking**: show it only in the duck characters, or keep it global but measure the original's curve. Engine plus layout, M.
6. **Section title follows the mode** ("Freq" / "Pitch"), and the left delay caption reads "Delay L" in Dual / Ping-pong. Add a `when` on section labels in `echoformLayout()`. UI only, S.
7. **Preset browser plus Mix lock**: needs a LYNX factory / user preset system (affects all engines). L.

---

## Spiral Echo ← ValhallaFreqEcho (Valhalla DSP)
**Sources:** REA_rodada101 §2 and §4; [FreqEcho product page](https://valhalladsp.com/shop/delay/valhalla-freq-echo/) (archive.org); `ref/FreqEcho.png`; `lynx-before/Spiral Echo.png`; `spiralLayout()` and `spiralInfo()`. **Trial on this PC:** yes (1.2.0).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Mix | 0–100 %, 50 %, linear law; first knob | Same | ✅ | |
| Delay | 0.01–1000 ms (skew 3.322); screenshot shows **0.01 ms** (knob at minimum) | Same law; default **100 ms** (v = 0.5) | ⚠️ | Default differs |
| Shift | ±1000 Hz (\|x\|^5), 0 Hz; big centre knob | Same, big centre knob | ✅ | |
| Feedback | 0–100 % (loop gain 1.5×), 50 % | Same | ✅ | |
| Low Cut | 20–3000 Hz, 200 Hz | Same | ✅ | |
| High Cut | 50–15 000 Hz, 5000 Hz | Same | ✅ | |
| Delay Sync | Free plus 23 notes (straight / dotted / triplet), under Mix / Delay | Same 24 entries, same place | ✅ | |
| Output Mode | Mono / Stereo, Stereo default, bottom right | Same | ✅ | |
| Per-pass shift, opposite shift in stereo | — | Matches the measurement | ✅ | |
| Presets | None | None | ✅ | |
| Tooltip / help line | Product page says tooltips show bottom left; none visible in the screenshot | Main-window hint bar | ⚠️ | |
| Resizable | Fixed size | Resizable | ➕ | |
| Gestures | Not confirmed | Full LYNX set | ✅ | |
| Saved-state translation | — | All 8 settings | ✅ | |

**Scores:** configuration 9/10, design 9/10.

**Top gaps:**
1. **Delay default**: set `{ "Delay", 0.0f }` in `spiralInfo()` if the 0.01 ms default is confirmed on a fresh instance (the screenshot suggests it). Parameter default only, S.
2. Optional: a small spiral / spectrum display. Not needed for parity.

---

## Orbit ← ValhallaSpaceModulator (Valhalla DSP)
**Sources:** REA_rodada101 §2 and §5; [SpaceModulator product page](https://valhalladsp.com/shop/modulation/valhalla-space-modulator/) (archive.org); `ref/SpaceModulator.png`; `lynx-before/Orbit.png`; `orbitLayout()` and `orbitInfo()`. **Trial on this PC:** yes (1.1.6).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Mix | 0–100 %, 50 %, equal power | Same | ✅ | |
| Rate | ±10 Hz (\|x\|^3.333), 0.10 Hz | Same (+0.10 Hz) | ✅ | |
| Depth | 0–50 ms (skew 2.322), 10 ms | Same | ✅ | |
| Feedback | ±100 %, 0 % | Same | ✅ | |
| Manual | ±50 ms (\|x\|^4), 0 ms | Same | ✅ | |
| Five knobs in a row, each its own colour | Mix, Rate, Depth, Feedback, Manual | Same order, each knob its own LYNX colour | ✅ | |
| Mode | 11 modes (Up, Up/Down, TZFlange±, TriFlange, Ocho, Doubler, VariUp, VariUpDown, Ensemble360, Symphonic), clickable text "Mode: Up" bottom left | 11 modes renamed (Rise …), menu with ◀ ▶ bottom left | ✅ | |
| Mode voices | Measured tap ranges per mode | Own designs within the measured ranges | ⚠️ | Round 101 marks finer captures as still to do |
| Preset browser | "PRESETS: Default ◀▶" bottom middle / right | header preset bar (LYNX factory + user presets, ◀ ▶) | ✅ | rounds 129 / 135 |
| Description line | Bottom strip, e.g. a one-line summary of the current mode | bottom strip: the hovered control, else "Mode: Through-zero + — a through-zero flanger…" | ✅ | round 135 |
| Resizable | Fixed size | Resizable | ➕ | |
| Gestures / text entry | Not confirmed | Full LYNX set | ✅ | |
| Saved-state translation | — | All 6 settings | ✅ | |

**Scores:** configuration 7/10, design 7/10. **After round 135**: configuration 8/10, design 9/10 (left: the per-mode voices).

**Top gaps** (1–2 done in rounds 129 / 135):
1. **Mode description line** under the mode row, one sentence per mode. Same info-line item as Echoform. UI only, S.
2. **Factory presets** with ◀ ▶ next to the mode. Needs the shared preset system, L.
3. **Voice shapes per mode** checked against finer captures (the round 101 follow-up), in `orbit::Orbit::process`. Engine change, M.

---

## Lattice Mod ← ValhallaUberMod (Valhalla DSP)
**Sources:** REA_rodada101 §2 and §6; [UberMod: The Modes](https://valhalladsp.com/2012/01/06/valhallaubermod-the-modes/), [The TAPS Parameters](https://valhalladsp.com/2012/01/26/valhallaubermod-the-taps-parameters/), [The DIFF Parameters](https://valhalladsp.com/2012/01/27/valhallaubermod-the-diff-parameters/) (archive.org); `ref/UberMod.png` and `uber_*.png`; `lynx-before/Lattice Mod*.png`; `latticeLayout()`, `latticeInfo()` and `lattice::tapCount()`. **Trial on this PC:** yes (1.1.6).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Faders Mix / Depth / Width / Delay / Feedback, values under them | 50 % / 50 % / 100 % (0–200) / 10 ms (0–1000) / 0 % | Same faders, values and defaults | ✅ | |
| **MODE** menu (under the faders) | Named algorithms: 2TapChorus (default), 4TapEnsemble, SuperSix, 6TapRandom, DualEnsemble, 8Tap, 16Phase, 16Tap, 32Tap; each has its own LFO design (antiphase triangle, 3-phase, randomised triangle, 16-phase) | round 135: a "Mode" menu with the nine algorithms under LYNX's own names (2-tap chorus … 32-tap) on the reference's type index, default 2-tap chorus (round 129), ◀ ▶ | ⚠️ | The three 6-tap modes and the two 16-tap modes still play alike (one random-detune plus one vibrato LFO for every mode — vox136 / engine) |
| Sync | Free plus 17 note values, next to the mode | round 144: the reference's list on its own value law (Free, 1/16 … 2/1, 1/16. … 1/1., 1/16T … 1/1T), next to the mode; translated | ✅ | round 144 [EXEC] (REA_rodada144a §1): index = floor (17 v) (boundaries at k / 17, checked ± 0.002); 0 and 1 both free; 2–7 straight 1/16 … 2/1, 8–12 dotted 1/16. … 1/1., 13–17 triplet 1/16T … 1/1T; the synced delay is capped at 2750 ms (2/1 and 1/1. at 120 BPM; uncapped at 240 BPM). The older by-length list stays for LYNX projects that use it (shown only while it is set) |
| Page tabs | MOD / TAPS / DIFF / EQ / DRIVE / WARP, top right | Same 6 tabs, same order | ✅ | |
| MOD page | SlowRate 0.50 Hz, SlowDepth 50 %, VibRate 6.00 Hz, VibDepth 0 %, OverMod 1.0× | Same 5 knobs and defaults | ✅ | |
| TAPS page | Spread 50 %, Slope 0, Skew 0, Random 0, TapGain 0 dB | Same | ✅ | Smoothing of tap-time changes (Warp Smoothing) should be checked |
| DIFF page | On/Off (OFF), Diffusion 50 %, Size 20 ms, ModRate 0.50 Hz, ModDepth 50 % | Same | ✅ | |
| EQ page | Mode Bright / Dark, Low Cut 10 Hz, High Cut 20 kHz, SpatialEQ 300 Hz | Same | ✅ | |
| DRIVE page | On/Off, InGain 9 dB, OutGain −6 dB, NoiseGain −90 dB, Pre/Post (POST) | Same | ✅ | |
| WARP page: Speed, fbMix, Smoothing | 1X / ½X, 50 %, 100 ms | Same | ✅ | |
| WARP page: InputPan | 0–8 steps, shows "Stereo" | round 137: Input pan menu on the WARP page (Stereo / Swapped / Mono / Mono left / Mono right) | ✅ | [EXEC] left- and right-only impulses through the demo: index 0–1 stereo (default 1), 2 swapped, 3 (L + R) / 2 to both, 4 to the left only, 5 to the right only, 6–8 like stereo; translated one to one |
| 16-tap alternating polarity / type ≥ 10 | Measured | Not modelled | ⚠️ | |
| Preset browser | "PRESET: Default ◀▶" next to the page area | header preset bar + the original's preset name | ✅ | rounds 129 / 135 |
| Description / tooltip line | Bottom strip shows hover help (e.g. the OverMod warning) | bottom strip: every control described ("Over-mod — pushes the modulation past its normal range (can sound unstable)") | ✅ | round 135 |
| Tap display | — | Tap graph under the pages | ➕ | |
| Resizable | Fixed size | Resizable | ➕ | |
| Gestures / text entry | Not confirmed | Full LYNX set | ✅ | |
| Saved-state translation | — | Everything (round 137: InputPan; round 144: DelaySync) | ✅ | `PluginSubstitutes.cpp [valhalla-ubermod]` |

**Scores:** configuration 6/10, design 7/10. **After round 135**: configuration 7/10, design 9/10 (left: per-mode LFO designs, InputPan, Sync translation). **After round 144**: configuration 9/10, design 9/10 (left: per-mode LFO designs, 16-tap polarity).

**Top gaps** (1 and 5 done in round 135, except the per-mode sound):
1. **Named Mode menu with the correct default**: replace "Taps" with 9 LYNX-named algorithms using the measured `type` index (2TapChorus = index 1), and set the default to index 1 (1.5/24) instead of 6.5/24. Changes `latticeLayout()`, `latticeInfo()` and `tapCount()`. UI plus default, S.
2. **LFO structure per mode** (antiphase triangle, 3-phase, randomised triangle, 16-phase) in `LatticeMod::process`, so SuperSix, 6TapRandom, DualEnsemble and 16Phase sound different from each other. Engine change, M–L.
3. **Sync translation**: measure the 18 sync steps, confirm the list, then add `map = xml:DelaySync -> Sync`. Engine plus map, S.
4. **InputPan** on the WARP page: new parameter, input routing and map line. Engine change, M (meaning to be measured first).
5. **Description line plus presets**: the shared items described in the other plugins' gaps. S / L.

---

**Common to all four:**
- **No presets or Mix lock.** The original Delay, SpaceModulator and UberMod all ship factory presets; FreqEcho has none.
- **No in-panel help line.** Valhalla's plugins "self-document" through tooltips at the bottom of the panel; LYNX only shows hints in the main window's hint bar.

A single "info line" layout item in `PluginEditors.cpp` would close the help-line gap for all four in one change.

## Ember Saturator ← Decapitator (Soundtoys)
Sources: the official trial (Decapitator 5.5.5, hosted black-box in our host: its 12 host parameters with every value
text, screenshots, and measurements of sines / noise through it — docs/analise/REA_rodada135s.md §2); docs/specs/decapitator.md;
`mixsuite::ember` in src/core/MixSuite.{h,cpp}; `EmberEditor` in src/ui/ProEditors.cpp. Round 135 re-checked every row on the trial
(it was "manual only" before). The Soundtoys control strip (◀ ▶ preset name, save, revert, settings, menu, BYPASS) has
**no A/B**: LYNX's header (undo / redo, A/B, presets) is a superset.

| Original feature | Original (range / default / where) [EXEC trial] | LYNX now | Status | Note |
|---|---|---|---|---|
| Drive | 0.0…10.0, big knob, default 0; ≈ 3.2 dB per step at −18 dBFS | Drive 0…+36 dB; read-out "+x dB (n.n)" on the 0–10 scale (3.6 dB / step) | ✅ | round 135: the 0–10 read-out |
| Punish | on / off, top left; adds 13.8 – 14.1 dB + 0.10 … 0.74 dB per Drive unit (per style) before the drive [EXEC round 165] | **Punish** in the strip on top of the display; Classic voicing: Punish top left with the measured law per style | ✅ | round 135: +20 dB pre-drive (single- and multiband); round 165: the Classic voicing's Punish follows the trial's gain law (0.03 – 0.14 dB) and style A its fitted Punish curve (null −9.5 → −17.3 dB over drives 1 … 10) (round 136's +9.75 dB came from a reading the trial gets wrong right after a toggle); projects saved before keep it ("Classic law Old") |
| Style A / E / N / T / P | 5 one-click buttons, bottom row | **one-click voicing row** (Tape pre, Console, Grit, Triode, Pentode; "Style menu" = the old 4 styles) | ✅ | round 135: five voicings (a curve each — a new "console" curve for E — and their own broad EQ, from the measured responses); own names |
| Per-style tone / harmonics | E brightest top (+3 dB > 8 kHz), N darker top, T thinner bass, P bass bump [EXEC noise, Drive 5] | each voicing's pre / post shelves follow these; the Classic voicing's five styles fitted on the trial | ✅ | round 136 (A) / 138 (E N T P): mean nulls A −25, E −16, N −17, T −12, P −13 dB |
| Low Cut | 20 Hz…1 kHz, 12 dB/oct, before the drive | **Wide cuts**: 20 Hz…1 kHz (old range 20–500 Hz kept for older projects) | ✅ | round 135 |
| Thump | switch beside Low Cut: a resonant bump at the cutoff (+3.5 dB) | **Thump** (low cut Q 1.6) | ✅ | round 135 |
| Tone | Dark…Bright ±12 (a pre-drive tilt: +12 = −18 dB at 30 Hz / +3 dB at 16 kHz) | Classic voicing: the measured 2nd-order tilt before the drive (±12, within 0.25 dB); Ember's own: Tilt ±6 dB after the curve | ✅ | round 136 |
| High Cut | 1…20 kHz, gentle ≈ 6 dB/oct | **Wide cuts**: 1…20 kHz, 6 dB/oct (old: 2–20 kHz, 12 dB/oct) | ✅ | round 135 |
| Steep | switch beside High Cut: ≈ 30 dB/oct | **Steep** (5th-order Butterworth, 30 dB/oct) | ✅ | round 135 |
| Auto | on (default); links the Output knob to Drive in the GUI — the audio is identical on / off [EXEC]; −1.6 dB per Drive unit [EXEC her 63 states: 11 distinct drive / output pairs, all on the line within the 0.1 dB read-out] | Classic voicing: **Auto** beside Output — turning Drive in the editor moves Output −1.6 dB per unit (host automation does not, as the trial); Ember's own: Auto gain (RMS make-up) | ✅ | round 165 |
| Output | −24…0 dB, main panel | **Output on the band panel** (±18 dB), beside Dry / wet | ✅ | round 135: moved from the bottom bar to the panel |
| Mix | Dry…Wet, default 100 % | Dry / wet | ✅ | |
| VU meter ("attitude") | analog VU on top | **IN / OUT meters** at the right of the display with the output read-out; Classic voicing: a needle VU top centre (output level, click: input / output) | ✅ | round 135; round 165: the VU |
| Latency | 62 samples at 48 kHz [EXEC] | the oversampler's latency (reported) | ✅ | |
| Oversampling | not exposed | 2× / 4× / 8× (bottom bar) | ➕ | |
| Multiband (Saturn role) | — | up to 4 bands | ➕ | |
| Preset strip ◀ ▶ / save / revert | Soundtoys strip | LYNX preset bar (◀ ▶, factory incl. a "Voicings" folder, user presets) | ✅ | |
| A/B | — (none in the strip) | LYNX's A/B | ➕ | |
| Translation of her saved state | encoded | **her 7 distinct states translated** (read in the trial: style A, Drive 1.9–2.0, Tone +1.7 / +4.2, High Cut 8.1 kHz / off, Mix 88 / 100 %, Trim −3.0 / −3.2 dB) | ✅ | round 135: payload hash → settings (core/SoundtoysStates.inc); other states: the light default |
| One-screen layout | VU top, Drive left, filters row, styles row | Classic voicing: the same arrangement — Punish top left, VU top centre, Output + Auto top right, the big Drive left, Low cut (+ Thump) / Tone / High cut (+ Steep) / Mix, the five styles A … P one click each along the bottom; Ember's own: display + band panel | ✅ | round 165 (LYNX's widgets and colours; the Classic toggle returns to Ember's engine) |

**Scores**: configuration 4/10 → **8/10**, design 4/10 → **7/10** (round 135).

**Round 165:** config 8 → 9, design 7 → 9 (Punish's measured law per style, Auto as the reference's Output-follows-Drive
link, the Classic voicing's one-screen panel with the VU; REA_rodada165ha).

**Left**: drive ≥ 5 on style A (the null falls to −11 dB at drive 10: a level-dependent even-harmonic term, round 138 §5);
Punish's curve at high level beyond the gain law; the VU's ballistics / calibration (not measurable black-box without the
GUI); the trial's own hiss.

---

## Fruity Delay 3 (stand-in) / Echoform ← EchoBoy (Soundtoys), with PrimalTap
Sources: docs/specs/echoboy.md (Pro Tools-era guide pp. 22–54, store page); `[soundtoys-echoboy]` / `[soundtoys-primaltap]` in src/core/PluginSubstitutes.cpp (→ Fruity Delay 3, no preset, no translation); `fruityDelay3()` and `echoformLayout()` in src/core/PluginLayout.cpp; `echoform::*` in src/core/EchoSuite.cpp; screenshots lynx-before/Fruity Delay 3.png and Echoform.png. PrimalTap facts are from search snippets of the manual (gear4music PDF copy), not a full read. Trial on this PC: no. **Manual only (trial not installed).** Round 135: the EchoBoy / PrimalTap trials are now active; their parameter lists, a screenshot and her 22 EchoBoy states read black-box in the trial (Ping-Pong 1/4 note, feedback 0.52, saturation 3.72, "Edited Style" in most slots) are in docs/analise/REA_rodada135s.md §4.

| Original feature | Original (range / default / where) | Fruity Delay 3 now | Echoform now | Status (best) | Note |
|---|---|---|---|---|---|
| Echo Mode: Single | per-channel echo | Delay model Mono/Stereo | Style "Single" (+ Spread) | ✅ (Echoform) | |
| Echo Mode: Dual | two independent times, FB mix / balance | Offset only | Style "Dual" (Time L/R, sync L/R) | ✅ | no FB cross-mix or FB balance; round 135w X-feed, round 144 FB balance; **round 150** [EXEC]: every style in Dual = its Single sound on each side (first-echo tone, per-pass tone, wobble within 0.1 dB / the same track), each side with its own time and the L/R offset law; no cross-feed (a left-only input leaves the right silent) — the voicings do the same |
| Echo Mode: Ping-Pong | mono in, L then R | Ping pong | Style "Ping-pong" (+ Pairs) | ✅ | **round 150** [EXEC] with every style: the input (L + R) / √2 (+3 dB against Single for L = R), left at Time 1, right at Time 1 + Time 2 with exactly the left echo (same level and tone, no own wobble), each left-right pair the feedback gain and a style-dependent pair tone (e.g. the telephone style band-passes every pair); with a voicing and "Stereo: Measured" Echoform does the same (the pair tone fitted per voicing: ≤ 2 dB rms, median 0.4 dB; round 147's law: median 4.7 dB) |
| Echo Mode: Rhythm (≤16 taps, 1–8 beats, grid ½…1/64, Shape) | Rhythm editor | — | round 137: RHYTHM switch (third row): evenly spaced taps (ms / note / dotted / triplet), Repeats 1–16, Decay 0–40 dB, Length 1–8 beats; the echo train shows the pattern | ✅ (Echoform) | [EXEC] measured on the trial (impulses at 60 / 120 BPM; its host parameters RhythmMode / Note / Time / Repeats / Decay): Repeats = slots incl. the input, so repeats − 1 taps are heard, each 1 / √slots (equal power, with the decay spread from the first slot to the last); the pattern spans 2 beats (taps past it are dropped) and the feedback replays it every 2 beats — LYNX's Length defaults to 2 beats. Left: the Pattern presets and the Custom (drawn) taps of the rhythm editor (not host parameters, not measured); **round 144**: the rhythm editor (a row under the panel): the taps on the pattern's time axis, drag = level / time, double-click = add / remove a tap; Even / Free / Grid edits (Grid ½ … 1/64 — EchoBoy's Custom Time / Custom Beats roles), Shape (Decay, Reverse, Swell, Fade, Nonlin — LYNX's own laws, [model]), and a Patterns menu of 12 LYNX-authored patterns (gallop, tresillo, clave, stutter, triplet roll …; none copied). [EXEC] round 144: on the trial the Pattern / Custom Time / Custom Beats modes give no echo when only its host parameters are set (their taps live in its editor), so those are not measured |
| Echo Style menu (~30 voicings) | main panel menu | — | **round 147**: Echo voicing menu (Style edit row): 31 voicings with LYNX names, one per factory style, each measured black-box on the trial (first-echo tone and per-pass loop tone from noise bursts, steady-sine gain / harmonics at Saturation 0 … 24 dB, pitch wobble, impulse spread) and modelled (fitted biquad tones ≤ 0.3 dB rms for most, a knee shaper or a compressor, sine / random wobble, diffusion) | ✅ (Echoform) | [EXEC] REA_rodada147a §2; EchoBoy's style names are not shown; the reference's L/R offset (−3.8 / +4.2 ms around the time) stays Echoform's Spread; **round 150**: the offset is measured and built (Tweak: L/R Offset row); the wobbles re-measured as delay tracks (impulse trains, 24 s): no style has several voices (a steady tone shows no beating: AM < 0.05 dB in all 31; the "fat" / ambient styles are a fixed tap pattern that moves as one), the CE-1-like style's swing is a triangle (1.63 Hz, ±1.0 ms), the others sines or smooth random swings — the voicings' depths now within 11 % median of the reference's delay swing (round 147: 40 %) |
| Echo Time ms | ms, pitch-bends when moved | Time + Keep pitch | Time L/R 0–20 s, default 300 ms | ✅ | |
| Note / Dotted / Triplet | Note/Dot/Trip buttons | Tempo sync | Sync menu ms/Note/Dotted/Triplet, 1/64…8/1 | ✅ | |
| Tempo source | host, tap, MIDI, 30–300 BPM | host | host only | ✅ | no tap tempo, no free BPM; round 129 tap pad + round 135w own BPM (30–300) |
| Mix | Dry…Wet, 12 o'clock = 50/50 | Wet/Dry faders | Mix 0–100 % (default 50) | ✅ | |
| Feedback | Min…Max, can run away | Level | 0–200 %, default 70 % | ✅ | |
| Saturation | Less…More, style-dependent, centre default | Feedback distortion Sat/Limit | Drive 0–24 dB (Colour) | ✅ | **round 147**: with a voicing, Drive = EchoBoy's Saturation 0 … 24 dB with each style's own measured law (drive / make-up per 6 dB step; e.g. studio tape: −3.5 dB at low level up to 12 dB, then +2.3 dB at 24; the radio / limited styles compress) |
| HighCut | style-dependent darkening | FB cutoff/res, LP/HP/BP | High cut 200 Hz–20 kHz + Low cut 10 Hz–2 kHz | ✅ | |
| Prime Numbers | detunes repeat times | — | — | ✅ | trivial: snap times to primes in samples; round 135w (Echoform): "Primes" — the delay times snap to a prime number of samples |
| Groove (shuffle…swing) | alternate repeats | — | — | ✅ | round 135w (Echoform): Groove ±25 % = odd repeats late / early by that share of the echo time ([EXEC] EchoBoy trial: +0.25 → +113…125 ms at 500 ms) |
| Feel (rushin'…draggin') | global pre-delay ± | Offset (partial) | Spread ±20 ms (partial) | ✅ | round 135w (Echoform): Feel ±25 % moves the whole train by that share of the echo time ([EXEC]: +0.125 → +56…62 ms at 500 ms) |
| Input level (echo path only) | −24…+24 dB | Input Wet | — | ✅ | **round 144**: Echoform "Input level" −24 … +24 dB on the echo path only (Style edit row) |
| Output level (echo only) | −24…+24 dB | Wet | Level ±18 dB | ✅ | |
| Tweak: Accent | bipolar | — | — | ✅ | round 135w (Echoform): Accent ±100 % (odd vs even repeats) [MANUAL: not a host parameter on the trial, law not measured] |
| Tweak: Width | min…max, past 3 o'clock out-of-phase | Stereo | Width ±100 % | ✅ | |
| Tweak: L/R Offset | 0–25 ms, default 8 | Offset | Spread ±20 ms | ✅ | **round 150** [EXEC]: at the default 8 ms the left side's odd repeats come 4 ms early and its even ones 6 ms late, the right side the other way round (+4 / −6 ms), for every style and time, in Single and Dual (not in Ping-Pong); Echoform: "L/R offset" 0–25 ms (default 8) with a voicing and Stereo "Measured" (two loops per side, every pass coloured); other offsets scale the measured law [model] |
| Tweak Dual: FB Mix / Balance / FB Balance | knobs | — | — | ✅ | round 135w (Echoform): X-feed (each side's feedback from the other) and Balance; no FB Balance; **round 144**: FB balance (±100 %, each side's feedback) |
| Style Edit: 3-band EQ gain + per-repeat Decay, Mid Res | ±18 dB etc. | FB filter only | Low/High cut only | ⚠️ | the "EQ in the loop vs first repeat" idea is missing; **round 144**: loop EQ (bass 120 Hz shelf, mid 1.2 kHz bell, treble 5 kHz shelf, ±18 dB) inside the loop — every repeat is shaped again; no per-repeat decay / Mid Res |
| Style Edit: saturation type (Clean, Tape, Warm, Pump, Dirt, Limit×4) | menu | Limit/Sat + knee/symmetry | Saturation type menu | ✅ | **round 147**: Auto / Clean / Tape / Tube / Squash / Grit / Ceiling — the measured curves of six representative styles (digital, studio tape, valve tape, FM radio's compressor, distorted, limited); works with or without a voicing. [EXEC] the reference's own type menu lives in its editor (not a host parameter), so its types were not measured one by one [model] |
| Style Edit: Diffusion + Size, Loop/Post | knobs + switch | Diffusion level/spread | Diffusion amount/size | ✅ | no Loop/Post choice; **round 144**: Diffusion Loop / Post |
| Style Edit: Wobble, Rate, Shape (5), Sync | knobs/menu | Mod rate/time | Wow, Flutter (Tape) / Rate, Depth | ✅ | no shape menu, no LFO sync; **round 144**: Wobble shape (sine, triangle, square, saw, random) and LFO sync (free, 1/16 … 2 bars) for the LFO modes |
| Ducking | — (not in EchoBoy) | — | Ducking | ➕ | |
| Pitch / shifter / reverse modes | — | — | yes | ➕ | |
| Echo display | Tweak "visualiser" of delay tails | — | "echotrain" display of repeats | ✅ (Echoform) | |
| Freeze (PrimalTap) | holds the loop; ≤2048 ms at 8× | — | — | ✅ | round 135w (Echoform): Freeze holds the loop (no input, unity feedback, no colour) |
| Multiply 2×/4×/8× (PrimalTap; halves the rate per step) | knob | Smp rate / Bits (lo-fi) | Multiply 1x / 2x / 4x / 8x (Style edit row) | ✅ | **round 147** [EXEC] PrimalTap 5.5.5: the delay × 2 / 4 / 8 exactly, a brick-wall band of ≈ 6.2 / 3.1 / 1.55 kHz (−43 … −49 dB a third of an octave above), up to 8 s (16 s gave silence); LYNX: the same factor on the times (up to 20 s) and two 8th-order low-passes at 12.4 kHz / factor inside the loop |
| VCO Rate/Depth per tap, phase flip (PrimalTap) | knobs | Mod rate | Rate/Depth (one LFO) | ⚠️ | |
| Preset menu / user presets / A/B | Soundtoys strip | FL factory list only; no user save, no A/B | "(no preset)" | ✅ | round 129's preset bar (Echoform) |
| Gestures, value entry, automation | standard | generic editor: full set | full set | ✅ | |
| Translation of the saved state | encoded | none, FL defaults (ms delay, FB, etc. unrelated) | her states recognised (round 136) | ⚠️ | round 136 / re-checked round 152: the encoded preset is not decoded; her EchoBoy states are recognised by their hash and play Echoform's measured Tape voicing, any other state the Studio reel voicing at 1/4 note, 25 % feedback (`[soundtoys-echoboy]`) |

**Scores**: Fruity Delay 3 as the stand-in: configuration 2/10, design 2/10. Echoform: configuration 5/10, design 5/10. **After round 144** (Echoform): configuration 9/10, design 8/10 (round 137: 8 / 7) — the rhythm editor (patterns, free / grid taps, shapes) and the Style Edit role (input, loop EQ, FB balance, diffusion place, wobble shape / sync); left: the saturation types, the ~30 style voicings, PrimalTap's Multiply. **After round 147a**: configuration 10/10, design 8/10 — the 31 measured voicings, the style-dependent Saturation, six saturation types, PrimalTap's Multiply; left: the reference's per-voicing Style Edit details (per-repeat decay, Mid Res), the chorus styles' multiple modulated voices (one wobble per side here), PrimalTap's per-tap VCO. **After round 150a**: configuration 10/10, design 8/10 (sound closer) — the voicings in Dual / Ping-Pong as measured (ping-pong copy, summed input, pair tone), EchoBoy's L/R offset (a control, measured law), the wobbles' shapes and depths from delay tracks; the "several voices" of the chorus styles do not exist on the trial (one modulated read per side, measured), so nothing is missing there; left: the per-voicing Style Edit details, PrimalTap's per-tap VCO, EchoBoy's offset law at offsets other than 8 ms (not a host parameter).

**Verdict:** **Echoform is clearly the better EchoBoy (and PrimalTap) substitute.** It has the four-mode-like Style switch (Single/Dual/Ping-pong + Ratio/Quad), a character Mode menu standing in for EchoBoy's Style, Drive ≈ Saturation, Wow/Flutter ≈ Wobble, Diffusion, note/dotted/triplet sync and an echo display. Fruity Delay 3 is a clean FL delay with none of the "style" concept, played at FL defaults. The spec's own recommendation (§7.1) agrees. H-Delay already moved to Echoform in round 119.

**Top gaps** (ordered by what a user touches first):
1. **Point `[soundtoys-echoboy]` and `[soundtoys-primaltap]` at Echoform**, with a conservative default: Tape mode, low Age, 1/4 note at host tempo, ~25 % FB, mix ~25–30 %. File: src/core/PluginSubstitutes.cpp. Table only. Effort S.
2. **Rhythm mode** (≤16 taps on a 1–8-beat grid, Shape presets) as a 6th Echoform Style. New params + DSP in `echoform::Echoform`, a grid display in PluginEditors.cpp. Effort L.
3. **Groove / Feel / Accent / Prime Numbers.** Four new Echoform params (timing offsets per repeat, a prime-snap switch). Engine change, small DSP. Effort S–M.
4. **Saturation centre default and style-dependent saturation/HC** (Drive interacting with Mode). Engine change. Effort M.
5. **Dual-mode FB Mix / FB Balance; EQ-in-loop "Decay" per band.** Engine change. Effort M.
6. **Freeze switch** (PrimalTap). Engine change. Effort S.
7. **Tap tempo** in the Delay section. UI + param. Effort S.
8. **Preset box / A/B / user presets** for LYNX engines (generic editor, src/ui/PluginEditors.cpp). Effort M.

---

## Auto-pan (stand-in) ← PanMan (Soundtoys)
Sources: the official trial (PanMan 5.5.5, black-box: 16 host parameters, screenshots of its sources, measurements —
docs/analise/REA_rodada135s.md §3); `AutoPan` / `autopan::` in src/core/Effects.{h,cpp}; `autoPanLayout()` in PluginLayout.cpp. Round 135
gave Auto-pan the rhythmic-panner sources while FL's own LFO (the Love Philter conversion it was made for) stays its
default source.

| Original feature | Original [EXEC trial] | LYNX now | Status | Note |
|---|---|---|---|---|
| Offset | −105…+105°, default 0 | Offset (the Pan control) ±100 % | ✅ | ±90° = a hard side |
| Width | 0…210°, default 180 | Width (Depth) ±100 % (180° = 100 %) | ✅ | no "beyond the speakers" above 180°; round 136's Rhythm voicing: 0–210°; **round 150** [EXEC]: past ±90° the angle folds back with the far side inverted (offset 45° + 90° of modulation → left at tan 7.5° of the right, polarity inverted) — the Rhythm voicing's "Beyond" switch (new instances on; projects saved before keep the clipped angle) |
| Smoothing | Hard…Soft, default Soft | **Smoothing** (LFO: square → sine; steps: jump → glide over a third of a step) | ✅ | round 135 |
| Pan law | equal power: centre −3 dB per side, the far side silent at the extremes | **equal-power balance** for the new sources (FL's laws for the FL source) | ✅ | round 135, measured |
| Rate (free) | 100 s … 10 Hz (0.01 · 1000^v Hz), default 0.25 Hz | **Rate** 0.01–10 Hz, same law and default | ✅ | round 135 |
| Sources: LFO / Rhythm Step / Rhythm Shape / Random / PingPong / Random Step | LFO button; rhythm note + steps (2…8); the two audio-triggered sources | **Source**: FL LFO · LFO · Rhythm steps (2–8 steps on 1/1…1/32 notes); **round 152** (the Rhythm voicing's Trigger row): **Ping-pong** and **Random step** moved by the input (threshold, divider 1–12, the detector's HP / LP filter, *Trigger now*), **Random steps** (the clock's steps at random places, reproducible) | ⚠️ | round 135; round 152 [MANUAL docs/specs/panman.md; laws model — the sources are not host parameters]; no free rhythm-shape editor (her "shape" presets play as 2 steps) |
| Direction ◀ / ◀▶ / ▶ | three buttons | **Direction** (right to left, back and forth, left to right) | ✅ | round 135 |
| Input / Output | −24…+24 dB, LED meters | **Input / Output** ±24 dB + IN / OUT meters | ✅ | round 135 |
| Analog / Digital | switch (default Analog): asymmetric saturation, H2 −36 dBc at −18 dBFS | **Digital / Analog** (fitted on the measured harmonics) | ✅ | round 135 |
| Pan position display | L … C … R LED strip | **pan-position strip** (live) | ✅ | round 135 |
| Feel / Groove | knobs (rhythm modes) | **round 150**: Feel / Groove ±0.25 of a step in the Rhythm voicing (the whole pattern / the odd steps late or early) | ⚠️ | [EXEC] on the trial they do nothing in LFO mode (all host-parameter runs identical); the rhythm modes are not reachable through host parameters, so the law is LYNX's [model] (EchoBoy's measured Groove / Feel shape) |
| Tweak: envelope / trigger (Rate / Width mod, threshold, divider, manual trigger) | Tweak page | **round 150**: the Rhythm voicing's Tweak row — Rate mod ±3 oct, Offset mod / Width mod ±180°, Threshold, Range | ✅ | [EXEC] (OffsetMod +90°, Width 0, 1 kHz stairs −60 … 0 dBFS and steps): the follower hears the input before Input / Output gain; no modulation below −20 dBFS (a sine's peak), linear in amplitude to full at −9 dBFS (0 / 28 / 54 / 90° at −30 / −15 / −12 / −6 dBFS); attack ≈ 21 ms, release ≈ 290 ms (one-pole fit, 2 % rms); Rate mod = octaves × amount, Offset / Width mod = degrees × amount (offset clamped to ±105°). Left: the editor's threshold / sensitivity values other than the default (not host parameters: LYNX's Threshold / Range follow the measured default, other settings scale it [model]), the divider and ManualTrigger (each `process` restarts the trial's LFO from the same phase, so a trigger is not observable). Round 152: the divider (1–12) and the manual trigger, with the trigger sources (Trigger row) |
| Tap tempo / MIDI | in rhythm modes | follows the project tempo | ⚠️ | |
| Presets | Soundtoys strip | FL presets (Auto-pan is an FL effect) | ⚠️ | |
| Translation of her saved state | encoded | **her 7 settings translated** (LFO 3.56 Hz / 150° / soft — 52 % of her slots; 5-step and 2-step rhythm presets; analog) | ✅ | round 135: the hidden source / steps / direction read from the trial's window per setting |

**Scores**: configuration 3/10 → **7/10**, design 2/10 → **6/10** (round 135). **After round 150a**: configuration **8/10**, design **7/10** — the Tweak row (envelope → rate / offset / width, measured follower and default threshold), Feel / Groove, the measured "beyond the speakers" fold; left: the divider / manual trigger, the free rhythm-shape editor, tap tempo / MIDI. **After round 152**: configuration 8 → **9/10**, design 7 → **8/10** — the trigger sources (Ping-pong / Random step by the input's threshold, divider, detector filter, manual trigger) and the Random source in a Trigger row; left: the free rhythm-shape editor, tap tempo / MIDI, the trigger laws measured (not reachable through host parameters).

---

## Foil Plate ← Little Plate (Soundtoys)
Sources: the official trial (Little Plate 5.5.5, black-box: 5 host parameters — Mix, Decay 0.5 s…∞, Low Cut 20 Hz…1 kHz,
Mod on / off; screenshot); `foilInfo` / `FoilFx` in src/core/LynxVerbs.cpp. The substitute is Foil Plate since round 129.

| Original feature | Original [EXEC trial] | Foil Plate | Status | Note |
|---|---|---|---|---|
| Decay (one big knob) | 0.5 s … 32 s … ∞, default 3 s | Decay 0.5–30 s (big knob) + **Infinite** | ✅ | round 135: ∞ holds the plate |
| Low Cut | 20 Hz…1 kHz, default 20 | **Low cut** 20 Hz–1 kHz (0 = off), 12 dB/oct | ✅ | round 135 |
| Mix | 0–100 %, default 100 % | Mix (the substitute: 100 % wet at the slot's 25 %) | ✅ | |
| Mod | on / off | Mod rate / depth | ✅ | [EXEC] round 150: Mod on leaves the decay per band unchanged (≤ 0.1 s at 3 s); its tail tone is narrower, not wider (spectral spread 5.4 → 2.4 Hz on a 1 kHz tail) — not modelled |
| Plate character | one EMT-style plate | 12 plate types | ✅ | **round 150**: **Measured plate** switch [EXEC] (impulses at Decay 0.5 / 1 / 2 / 3 / 5 / 10 / 20 s and ∞): the plate speaks after 0.7 ms (Foil's types after 1 … 81 ms), the decay per octave against the label (20 s: 35 s at 125 Hz, 14 s at 1 kHz, 5.5 s at 8 kHz; short labels ring a little longer in the presence band; ∞ ≈ 80 s), fitted through Foil's engine: mid / bass / high RT within 5 % at every label up to 20 s (∞: the highs die sooner than on the trial), the tail's energy within ±0.5 dB; the Little Plate substitute plays it |
| Extras (predelay, size, width, EQ) | none on the panel | yes | ➕ | a fuller panel than the 3-knob original; round 152: the **Plate view** shows only the reference's controls (Full \| Plate tabs) |
| Presets | Soundtoys strip | LYNX preset bar | ✅ | |

**Scores**: configuration 6/10 → **8/10**, design 5/10 → **6/10** (round 135). **After round 150a**: configuration **9/10**, design **6/10** — the measured plate voicing (onset, decay per band, level); left: Mod's narrowing of the tail, the single-knob panel (Foil keeps its fuller layout). **After round 152**: configuration **9/10**, design 6 → **8/10** — the **Plate view** (View: Full \| Plate; the Little Plate substitute opens in it): Low Cut · the big Decay with its ∞ · Mod · Mix, the reference's one-knob panel; left: Mod's narrowing of the tail.

---

## Voice Shift ← Little AlterBoy (Soundtoys)
Sources: the official trial (Little AlterBoy 5.5.5, black-box: 7 host parameters with their value texts, screenshot,
measurements of harmonic test tones — docs/analise/REA_rodada135s.md §1); docs/specs/little-alterboy.md;
src/core/VoiceShift.{h,cpp}; `voiceShiftLayout()`. Round 135 created **Voice Shift**, LYNX's own voice shifter (YIN +
TD-PSOLA from core/VocalPitch.h), for this role: 696 slots in 399 of her projects had no LYNX module before.

| Original feature | Original [EXEC trial] | Voice Shift | Status | Note |
|---|---|---|---|---|
| PITCH | −12.0…+12.0 st, 0.1 st read-out, default 0 | Pitch ±12 st, read-out box (0.1 st), big knob | ✅ | transpose +2.5 st: 231 Hz on both |
| FORMANT | −12.0…+12.0 st, default 0 | Formant ±12 st, read-out box | ✅ | |
| LINK | on / off between the knobs | Link (formants follow the pitch; Formant offsets them) | ✅ | |
| MODE Transpose / Quantize / Robot | three buttons | Mode radio (same three) | ✅ | Quantize: nearest semitone at once (210 → 208 Hz on both); Robot: C4 at Pitch 0 (262 Hz on both), Pitch moves it |
| DRIVE | 0.0…10.0 | Drive 0–10 (tube stage on the shifted voice, ≈ 1.75 dB / step, make-down above 6) | ✅ | curve fitted roughly on the trial's level / harmonics (round 136 may refine) |
| MIX | 0–100 %, default 100, linear | Mix (linear, dry delayed by the latency) | ✅ | measured linear on the trial |
| Latency | 2417 samples at 48 kHz (reported) | two periods of 70 Hz (≈ 1376 at 48 kHz), reported | ⚠️ | different (both compensated by PDC) |
| MIDI → pitch | host MIDI | round 137: MIDI channel stepper under the modes: the highest note held on that Channel Rack channel replaces Pitch (relative to C4) in Transpose / Quantize and is the note Robot sings | ✅ | [MANUAL] the original's MIDI mode; the trial's MIDI response was not measured (our measurement host sends no notes) |
| One-screen panel | Pitch / Formant · Mode · Drive / Mix | the same three sections | ✅ | |
| Presets | Soundtoys strip | LYNX preset bar with 7 factory presets | ✅ | |
| Substitute entry | — | `[soundtoys-littlealterboy]` (VST2 `StLa`, names) | ✅ | round 135 |
| Translation of her saved state | encoded | **her 4 distinct settings translated** (Quantize, formant +1.2 st, drive 4.6, mix 100 % = 84 % of her slots; an octave-down Transpose; two 44 % mixes); others: her usual setting | ✅ | round 135 |

**Scores**: configuration 0/10 → **9/10**, design 0/10 → **8/10** (round 135).

Sources (web): [Decapitator guide (B&H copy)](https://static.bhphoto.com/lit_files/89645.pdf), [SOS Soundtoys 5 review](https://www.soundonsound.com/reviews/soundtoys-5); the rest from the official trials (round 135).

---

## Round 190ar, part 2: Compressor, the classic voicing's FET law

Measured black-box (public parameters only) on four FET-style compressor references' probe renders (round trial190k:
1 kHz stairs, tone bursts of 10 ms … 2 s and the recovery after them). What the classic voicing could not do:

- the recovery is not one time constant. Read off the gain after a 2 s burst, the reduction G (dB) falls per second by
  about a · G + b · G²: a = 0.35 … 0.8 / s (a tail of 1.3 … 2.7 s whatever the release control says), b = 0 … 0.08 / (dB s)
  growing with the release control. Deep reduction comes back fast, the last decibels slowly;
- the knee is about 5 dB wide (0.6 dB of reduction at the threshold).

`Classic law` (new parameter, **off by default**: every existing project and new instance sounds as before; the panel
shows `Clean | FET` under the voicing tabs) adds that law to the classic voicing: a 5 dB knee, and per second the
reduction falls by G / tail + G² / (10 dB · release), tail = the release time, at most 2.7 s. For small reductions that
is the classic exponential release; at 10 dB it is twice as fast, at 20 dB three times. No latency, same CPU (render
time of a 35 s file 0.16 … 0.18 s with the law off or on, as before). The vintage voicing and the upward expansion
(ratio under 1) do not have it.

Scores with matched settings (the setting maps of round 190ar fitted again on the probes, with the law on and the wide
ranges; programme bank of round 182, 5 settings × 5 signals; before = the classic law's own best map):

| Reference kind | Before → after | Null | Level error | Files up / same / down |
|---|---|---|---|---|
| FET compressor | 2.76 → 4.80 | −6.5 → −15.8 dB | 3.9 → 1.5 dB | 20 / 4 / 1 |
| FET compressor, modern revision | 3.52 → 4.08 | −11.8 → −12.5 dB | 2.1 → 1.9 dB | 10 / 15 / 0 |
| FET compressor, vintage revision | 3.08 → 5.00 | −8.2 → −16.4 dB | 3.2 → 1.3 dB | 17 / 7 / 1 |
| FET compressor, all-buttons style | 3.60 → 4.28 | −10.3 → −13.2 dB | 1.9 → 1.2 dB | 14 / 7 / 4 |

Not modelled: the faster recovery after bursts shorter than about 50 ms at the fastest release, the modern revision's
two-stage attack (5 … 20 ms), the all-buttons style's overshoot in the first 100 ms. The law is not yet selected by the
plugin substitutes (a project that used such a compressor still loads the classic law).

## Round tp172c: SSL Native Channel Strip 2 (Ridge Strip's Modern voicing)
Sources: the official SSL trial (Native Channel Strip 2 v2.10, black-box only): its parameter table and read-outs, 30
programme renders (defaults + 5 settings × drums / music / vocal / sweep / stair), 224 + 224 impulse renders on a dense
grid (every EQ band's frequency × gain, the mid bands' Q × gain, both EQ types, the filters), sine ramps, stairs and
tone bursts through 48 dynamics settings (ratios, thresholds, attack, peak / RMS, release, auto make-up, gate /
expander ranges and releases). Fits: tp172c `ana/` (bandfit, shelf4, filtfit, dynfit, the ramp fits). No plug-in code,
data or image was read or copied. LYNX: `vmatch::strip2` (core/VoiceMatch.h, core/ConsoleStrip2.inc and its generated
tables), Ridge's appended **Modern voicing**, **Peak detect** and **Gate hold**.

| Original feature | Original [EXEC trial] | Ridge Strip (Modern voicing) | Status | Note |
|---|---|---|---|---|
| E / G EQ types | switch between HMF and LMF | the EQ curve tabs read "E type / G type" | ✅ | E shelves: first order (0.01 dB); G shelves: second order with the dip / bump either side (≤ 0.4 dB); bells fitted per grid point (≤ 0.2 dB below 10 kHz) |
| Band gains | LF ±16.5, mids / HF ±20 dB, a non-linear law ("+12" on a mid = +7.3 dB E, +9.25 dB G) | the dial reads the reference's value, the measured gain is applied | ✅ | interpolated on the measured grid |
| Bell width | follows the gain (Q 0.15 at ±3 dB … 0.7 at ±20 on the mids) | measured per gain and Q | ✅ | |
| HPF / LPF | 3rd-order high-pass 20 … 500 Hz, 2nd-order low-pass 3 … 27.5 kHz, corners off the read-out by up to 25 % | the measured corners | ✅ | |
| Compressor | feed-forward, RMS (soft knee, threshold −13.5 dB under the dial) or Peak (hard, −17.9 dB); fast attack 3 ms, normal ~45 ms; release τ = 0.68 × the dial | the same laws | ✅ | dynfit residual 0.03 … 0.4 dB on stairs and bursts |
| Auto make-up and its offset | 5/6 dB per dB of threshold under +3, × the ratio's slope | the same | ✅ | slice 2 refit on three measured points (ratio 2 / 10 at −15, ratio 3 at −12) |
| Gate / expander | gate with hysteresis and hold, expander 0.86 dB/dB, range 10 / 40 dB = 7.9 / 47.8 dB, release a dB slew | the same; Hold dial | ✅ | |
| Mix | 0 … 100 % | Comp. mix | ✅ | linear blend (not measured) |
| Width, pan, fader, cut, listen, routing panel | centre module | input / output / phase; fader folded into Output | ⚠️ | width / pan / S/C routing not translated |

**Sound** (fxparity, LYNX as it loads the trial's saved states): before 2.6 (the state was not translated), with the 4K
voicing 3.6, with the Modern voicing **7.1** (defaults 7…9, EQ settings 6…8, compressor 6…8, gate + peak 5…8). The
remaining error: the dense drum transients through the fast peak compressor + gate (S4 drums 5) and the make-up with
the dynamics before the EQ (S5, +0.5 dB).

## Round 190ar, part 2: Apex Limiter, the levelling stage

Measured black-box (public parameters only) on a limiter that has a threshold under its ceiling (round trial190k's
probes: 1 kHz stairs, tone bursts and the recovery after them). Apex could only be a brickwall: with the threshold
matched by its ceiling it clamped every transient, and came out 8 … 11 dB lower in peak and 2 … 4 dB lower in level
on programme. What the reference does:

- above the threshold (default −12 dB) the steady level is held at the threshold, with a slow attack — about 9 ms in
  its transparent style, under 1 ms in the other — and an exponential release that is the release control;
- the ceiling only catches what the slow attack lets through.

`Level threshold` and `Level attack` (new parameters; the threshold is **off by default**: every existing project and
new instance sounds as before) add that stage in front of the limiter: the linked peak after the input gain, over the
threshold (0 … −36 dB), ∞ : 1 with a 6 dB knee, one-pole attack (0.1 … 100 ms, default 9 ms), released with the
limiter's Release. No extra latency; the gain reduction meter shows both. Render time of a 35 s file 0.52 … 0.57 s
before, 0.52 … 0.55 s with the stage off, 0.55 … 0.62 s with it on.

Scores with matched settings (the setting map of round 190ar fitted again on the probes with the two new parameters;
before = the brickwall's own best map):

| Reference kind | Bank | Files | Before → after | Null | Level error | Up / same / down |
|---|---|---|---|---|---|---|
| Limiter with threshold and ceiling | tp172c | 16 | 3.25 → 4.94 | −11.8 → −17.0 dB | 2.4 → 0.9 dB | 14 / 1 / 1 |
| | programme 182 | 20 | 3.35 → 5.65 | −11.3 → −21.5 dB | 2.5 → 0.5 dB | 17 / 3 / 0 |

One file went down (the fast style on tone bursts, 5 → 3). With the stage off, 28 of 28 renders of the round's Apex
maps are byte-identical to before.

In the editor the stage sits in the Advanced panel under the four switches: a `Levelling` switch (off by default; it
remembers the threshold, -12 dB the first time) and the `Threshold` / `Attack` value rows. The fit's first run stayed in the
brickwall solution (ceiling = threshold, stage off: 3.12 / 3.35); the map above starts from ceiling near 0 dB and the
threshold at −12 dB. Not modelled: the reference's fast style shapes bursts differently in the first milliseconds.

## Round 180c: Merge similar pattern clips, Merge automation clips (issue #74)

FL's Ctrl+Shift+G and Ctrl+Alt+G, from `FLEngine_x64.dll`. See `docs/analise/REA_rodada180c.md`, including its list of differences from FL.

## Round 186c: mixer Delete, links to deleted inserts, groups that follow the Playlist

Proof: `docs/analise/REA_rodada186c.md` (REA of FL 2026 + a hand check in FL).

| Item | FL Studio | LYNX | Status |
|---|---|---|---|
| Delete (menu, Del) on the selected inserts | "Delete tracks" question with Yes / No and "Remember my choice" (Yes is remembered, No asks again) | same | ✅ |
| Delete when a Playlist audio / instrument track is linked | "Playlist tracks": Delete tracks and channels / Unlink tracks and channels / Cancel | same; for an audio track "Delete" resets the Playlist track only | ⚠️ |
| Delete during audio recording | error, nothing deleted | same | ✅ |
| Tracks that sent into a deleted insert | sent on to its destinations | same | ✅ |
| Settings › General › Hints and warnings › Manage messages | ~50 messages by category | the messages LYNX has (Delete mixer track) | ⚠️ |
| Del in the mixer | deletes the selected tracks; never an effect (except with the slot menu open) | same (Del was "delete the effect"; that action stays, unbound) | ✅ |
| Mixer count after Delete | the mixer shrinks | a blank insert takes the last place | ⚠️ |
| Links to deleted inserts (pain point 1) | dropped | Mixer menu option, off: kept as a greyed dashed chip, "Disconnected links" in the Routing section reconnects or removes them | LYNX extra |
| Mixer groups that follow the Playlist (pain point 4) | none | Mixer menu option, off, + "Arrange inserts like the Playlist" | LYNX extra |

## Round 190ar, part 2: missing plugins load with the measured settings (fitted-map translators)

Until now a project that used one of these six plugins and was opened without it got LYNX's stand-in at its default
settings. Round 190ar measured, black-box, which LYNX setting matches each of their settings (one fitted transfer per
control, from probe renders through the public parameters). `translate = fitted-map` carries those transfers into the
substitute table: the saved state's parameter values are read (the plugin's own plain XML state as the host stores
it, captured by saving probe settings; nothing else of the plugin is read) and every linked LYNX parameter is set from
the measured transfer, linear between the measured settings. A state that cannot be read leaves the stand-in at
its defaults, as before. Nothing changes for projects that have the plugin installed, and no LYNX effect's sound
changed.

Scores of what the stand-in sounds like against the plugin (before = stand-in at its defaults = what a project got
until now; after = stand-in with the translated state):

| Missing plugin (kind) | Stand-in | Bank | Files | Before → after | Null | Level error | Up / same / down |
|---|---|---|---|---|---|---|---|
| Bus compressor (SSL Native Bus Compressor 2) | Crest Compressor | tp172c | 16 | 2.69 → 6.06 | 1.1 → −23.2 dB | 8.6 → 0.4 dB | 14 / 2 / 0 |
| | | programme 182 | 20 | 2.60 → 6.20 | 1.2 → −23.9 dB | 8.9 → 0.5 dB | 19 / 1 / 0 |
| Compressor (SSL Native X-Comp) | Crest Compressor | tp172c | 12 | 1.75 → 6.75 | 0.9 → −37.3 dB | 7.7 → 0.6 dB | 12 / 0 / 0 |
| | | programme 182 | 25 | 3.00 → 6.44 | −6.1 → −35.1 dB | 4.9 → 0.6 dB | 21 / 4 / 0 |
| Limiter (FG-X 2) | Apex Limiter | tp172c | 12 | 3.92 → 7.58 | −50.3 → −90.1 dB | 3.7 → 0.3 dB | 10 / 2 / 0 |
| | | programme 182 | 25 | 3.48 → 7.44 | −50.7 → −66.6 dB | 3.8 → 0.2 dB | 20 / 5 / 0 |
| Saturator (SSL Native X-Saturator) | Ember Saturator | tp172c | 12 | 2.33 → 5.00 | −6.4 → −18.3 dB | 8.4 → 1.0 dB | 11 / 1 / 0 |
| | | programme 182 | 25 | 2.04 → 5.60 | −6.5 → −19.7 dB | 7.1 → 0.8 dB | 24 / 1 / 0 |
| Gate (SSL X-Gate) | Sentry Gate | tp172c | 12 | 6.75 → 7.33 | −23.3 → −33.6 dB | 0.1 → 0.0 dB | 5 / 7 / 0 |
| | | programme 182 | 25 | 6.56 → 7.24 | −30.9 → −35.9 dB | 0.1 → 0.1 dB | 11 / 14 / 0 |
| Lookahead compressor (Harrison) | Crest Compressor | tp172c | 8 | 2.75 → 5.50 | −7.2 → −18.6 dB | 3.3 → 1.2 dB | 7 / 1 / 0 |
| | | programme 182 | 25 | 3.04 → 5.92 | −9.8 → −22.1 dB | 3.1 → 0.7 dB | 19 / 5 / 1 |

The tp172c rows and the lookahead compressor's programme row are rendered from the C++ translator's own output on the
captured states; the other programme rows are the same maps evaluated by the fitter (the C++ translator agrees with it
within 2e-4 on the 18 captured states of the test). The lookahead compressor's map was fitted again on the Vari-mu
law of round 190v (33 files: 5.48 with the old map on the new law → 5.82). One file went down (lookahead compressor,
one vocal setting, 6 → 5).

Limits: values between the captured settings are interpolated, and a few fitted transfers are not monotonic (they
are carried as measured); beyond the captured range the first-to-last trend continues. The second (A/B) slot of the
FG-X 2 state is not read. The EQ modules have no translator: their fitted per-setting maps are not smooth enough to
interpolate (a translation in physical units is needed).

With the two new laws of this round in (the Compressor's `Classic law` and Apex's levelling stage), the same route
covers five more plugins, each rendered from the C++ translator's own output on the captured states:

| Missing plugin (kind) | Stand-in | Bank | Files | Before → after | Null | Level error | Up / same / down |
|---|---|---|---|---|---|---|---|
| Limiter with threshold (SSL X-Limit) | Apex Limiter | tp172c | 16 | 2.38 → 4.94 | −3.9 → −17.1 dB | 3.5 → 0.9 dB | 14 / 1 / 1 |
| | | programme 182 | 20 | 3.05 → 5.65 | −7.5 → −21.5 dB | 2.5 → 0.5 dB | 19 / 1 / 0 |
| FET compressor (VM FG-116) | Compressor | programme 182 | 25 | 3.08 → 4.80 | −8.8 → −15.8 dB | 2.8 → 1.5 dB | 19 / 6 / 0 |
| FET compressor, modern revision | Compressor | programme 182 | 25 | 3.04 → 4.08 | −9.1 → −12.5 dB | 2.5 → 1.9 dB | 13 / 12 / 0 |
| FET compressor, vintage revision | Compressor | programme 182 | 25 | 3.48 → 5.00 | −10.5 → −16.4 dB | 2.1 → 1.3 dB | 14 / 10 / 1 |
| FET compressor, all-buttons style (VM The Monster) | Compressor | programme 182 | 25 | 3.28 → 4.28 | −8.4 → −13.2 dB | 2.1 → 1.2 dB | 15 / 9 / 1 |

Three files went down by one step (a limiter burst setting, one vocal and one drum file). The FET compressors'
ratio transfer is not monotonic between its four positions (carried as measured). The other names of the vintage
compressor role (no captured state) still load the stand-in at its defaults.

## Round 177b: workflow extras beyond FL (FL's own way stays unchanged)

| Feature | FL Studio | LYNX | Status |
|---|---|---|---|
| Sidechain from an effect | route arrow › "Sidechain to this track" (/ only), then the plugin's own input selector | the same route menu, plus a "SC: … ▾" picker in every side-input effect's window header, the plugin wrapper header, the mixer's selected-effect panel and the slot menu (makes FL's level-0 link) | LYNX extra |
| Automation lanes under Playlist tracks | — (automation clips only) | Playlist ruler menu › "Automation lanes", off by default | LYNX extra (Ableton-style, optional) |
| Modulators (LFO / random moving a knob around its value) | — | Tools › "Modulators…" and every control's "Modulate" submenu; no cost until the first one exists | LYNX extra |

## Host workflow: LYNX extras over FL (round 177d)

These are things LYNX's host does that FL Studio does not. None of them changes an FL gesture or default. They come from the FL pain-point list the user approved (docs/analise/REA_rodada177d.md, which also has the FL checks).

| Item | FL | LYNX |
|---|---|---|
| Ultra-fine knob and slider drag (pain point 20) | Ctrl = fine. Ctrl+Shift is the same as Ctrl (checked in FL 2026) | Ctrl = fine (FL). Ctrl+Shift = 100× finer, for drags and the wheel |
| Insert EQ bypass (pain point 6a) | No bypass, and no menu on the EQ title | Right-click the EQ title › "Bypass the EQ (its settings are kept)". A bypassed EQ costs nothing, and FLP export writes the bands flat |
| Gain-reduction bar on the mixer strip (pain point 6b) | None (each plugin shows its own) | A 3 px bar, only on strips that have a LYNX dynamics effect |
| Picker panel › "Delete unused patterns", "Merge identical patterns" (pain point 31a) | Not available (FL has "Select unused" and "Sort", and LYNX has those too) | After a separator, below FL's Tools items |

## Audio logger and Chord detection panel (round 181b)

Proof: `docs/analise/REA_rodada181b.md`.

| FL | LYNX | Status | Note |
|---|---|---|---|
| Rolling Master buffer, on by default, 1 / 2 / 5 / 10 minutes, pre or post effects, memory shown in the header | same (`RollingAudioLog`, Tools menu) | ✅ | |
| Writing stops after 8 s of silence | same | ✅ | |
| Dump to audio editor, "empty" message | same, into LYNX's audio editor | ⚠️ | own folder and date format; WAV written off the UI thread |
| Chord detection panel on the default toolbar | same | ✅ | |
| Chord names | `chordread::nameKeys` | ✅ | 0 differences in 19852 measured key sets; own wording for the joke names |
| Display delay menu | same | ✅ | |
| Chord style (Roman) | a working item | ➕ | LYNX addition: FL hides the item and its flag does nothing in the detector |
| Near the hint bar | same menu item, outlined text under the hint bar | ✅ | seen in FL at runtime; the 15 s cap comes from the decompile |
| Tools › Macros › Change declicking for selected channels to | same six modes, same messages | ✅ | LYNX can undo it, so no "(no undo)" |
| Tools › Macros › Prepare for MIDI export | same numbering (FPC on 10, others 1–16 without 10), names kept | ✅ | FL skips only automation clip channels (seen at runtime); LYNX's automation clips aren't channels |
| Tools › Riff machine | opens LYNX's Riff machine on the selected channel | ⚠️ | FL inserts a new Sytrus channel from `Data\System\Riff machine\*.fst` first |
| Tools › Chord progression tool | opens LYNX's chord progression generator | ⚠️ | FL's is its own Python window (`cpt.pyscript`), much larger |
| Tools › Macros › Remove all Edison instances | same item, in FL's place (after "Reset empty playlist tracks") | ✅ | round 190cd/190ct; asks first with FL's question, enabled with none (FL); LYNX can undo it (one step), so no "(No undo)" line (rollup 190cv) |
| Tools › Macros › Switch all / selected audio clip channels to | same items, place and texts, with LYNX's modes (Resample, stretch engine, Ableton-style warp) | ⚠️ | user's decision: no élastique / slice modes; no tempo detection for channels without a stretch length |
| Tools › Clipboard › Save audio / MIDI clipboard to browser, greyed when empty | same, into the Browser's "Clipboard files" | ⚠️ | audio: CF_WAVE on Windows, else LYNX's audio editor clipboard; " (2)" naming INFERRED |
| Tools › Score logger (Dump score log to selected pattern, Clear log) | same, moved from Options to Tools | ✅ | |

## Plugin wrapper: Processing › Connections, multi-output generators (round 188d, issue #114)

FL 2026 checked with a multi-output test VST2 (`tools/testplugin/lynx_test_drums_vst2.c`); the Fruity Wrapper code
read for Auto map / Reset. Details: `docs/analise/REA_rodada188d.md`.

| | FL Studio | LYNX | Status |
|---|---|---|---|
| Output list | Processing › Connections: one row per output, "n. pin name" (else "Output n"), LED, mixer track offset wheel ("---" = 0) | the same rows in the Processing tab; VST2 pins are grouped in stereo pairs, VST3 by bus; a bridged plugin lists "Output n" pairs (up to 16) | same |
| Default routing | every active output on the channel's insert (offset 0) | same | same |
| Offset | output plays on the channel's insert + offset (wheel −999…999) | same (wheel, drag, double-click types it) | same |
| LED off | the output is silent | same | same |
| Auto map outputs | from output 1's offset, +1 per output, inactive ones included | same | same |
| Reset | offsets back to "---", LEDs unchanged | same | same |
| Channel volume / pan | apply to every output | same | same |
| Which outputs start active | as the plugin declares them (a VST2 pin pair without the "active" flag starts off) | same (VST2 pin flag, VST3 bus default-active) | same |
| Process inactive inputs and outputs | on: an output with its LED off still plays; an LED turned off turns it off | same | same |
| .flp import / export of the mapping | records 0x1E / 0x1F / 0x20 and flag 0x8 | read and written | same |
| Effects' Connections | inputs (sidechain index into [insert, inserts routed to it]) and outputs; Auto map inputs | same page; input 2's index drives the side input | same |
| Effect input index (round 189j) | input k at index i plays entry i of [insert, inserts routed to it at any level, ascending], post-fader / pan / mute; 0 or past the list = the insert's own audio; input 1 so replaces the insert's audio | same, with the effect's Connections set (every FL-imported effect; LYNX's own once touched) | same |
| Effect output index (round 189j) | output pairs at index 0 summed into the insert; index k = entry k of [insert, Master, inserts it routes to ascending] (past the end: the last), straight into that track, past the insert's fader | same (the first 8 pairs; with "Use fixed size buffers" output 2 only) | same |
| Inactive effect input / output (round 189j) | silent when "Process inactive inputs and outputs" is off | same | same |
| Effect with untouched Connections | input 2 gets the insert's own audio; every output pair summed into the insert | input 2 fed only by a picked side input (issue #109); output 1 only | differs (kept as before; decision for the user) |
| .flp export of a picked side input (round 189j) | input 2's index | the SC picker's source written as input 2's index (an older index replaced) | same |

## Round a2m189b: Convert to MIDI (Ableton Live's Convert Melody / Harmony / Drums, LYNX extra)

| Item | FL Studio | Ableton Live 12 | LYNX |
|---|---|---|---|
| Where | Edison › Tools › "Convert to score and dump to piano roll" (monophonic, into the selected channel's notes) | audio clip menu › "Convert Melody / Harmony / Drums to New MIDI Track" | Playlist audio clip menu › "Convert to MIDI" › Melody / Harmony / Drums "to new pattern", and the same submenu in the audio editor's Tools › Analysis (the selection, or the whole audio). FL's own item stays as it was |
| Result | the notes replace the channel's notes in the current pattern | a new MIDI track under the clip with an instrument (Melody / Harmony: "… to MIDI" instrument racks; Drums: a 606 Drum Rack, kick C1, snare D1, hi-hat F#1) | a new pattern on new channels (melody / harmony: one Synth channel; drums: Kick, Snare and Hat channels, one step note per hit), on a new track right under the clip, aligned with it and as long; one undo step; the pattern is selected |
| Analysis | FL's pitch detector | Live's own (not public) | melody and harmony: Spotify Basic Pitch (Apache-2.0, 230 KB model compiled in, run by the ONNX Runtime LYNX already ships), with settings chosen on round a2m189b's clips; drums: LYNX's own semi-adaptive NMF (kick / snare / hat templates); on a worker thread with a progress window and Cancel, nothing on the audio thread |

Measured against ground truth (note F1, onset within 50 ms) on round a2m189b's clips (`/mnt/e/lynx-work/a2m189b/report.md`):
LYNX matches or beats Live 12 on bass, synthetic voice, guitar, piano, drums; Live is better on a real sung vocal
(F1 0.48 vs 0.43).

# FL Studio Mobile roles: the Pocket effects (round 188h)

FL Studio Mobile's own effect modules, as LYNX engines with LYNX names (the "(FL Mobile)" role). Clean-room: the
modules' laws were read from FL Studio Mobile's plugin code (REA_rodada182c.md "F5 effects, round 188h") and are
restated in `src/core/PocketFx.{h,cpp}`, `PocketMultiFx.cpp`, `PocketTape.h`. A `.flm` import puts them on the channel's
insert with the module's own values; they are also in the mixer's effect list. Editors: LYNX's generated knob page
for now (FL Studio Mobile reference shots and the matching layouts are the next UI step).

| LYNX effect | Role (FL Mobile) | Sound | Checked against |
|---|---|---|---|
| Pocket MultiFX | MultiFX (X/Y pad: Delay, Reverb, LP / HP Filter, Flanger, Phaser, Panner, Vox; song-locked LFO) | FLM-exact laws; the reverb kind uses LYNX's Reeverb 2 at FLM's levels (≤ 0.4 dB diffuser tilt) | hand-worked echo times / levels, filter DC gain, panner delay |
| Pocket Trance Delay | Trance Delay (beat-synced, Stereo / Ping-Pong / Mirror) | FLM-exact | echo times and levels |
| Pocket Distortion | Distortion (four curves, bit crush, bias) | FLM-exact | each curve, hold length, DC removal |
| Pocket Limiter Classic | Limiter (legacy) | FLM-exact | attack, cos² recovery, make-up |
| Pocket Limiter | Limiter (2 ms look-ahead) | FLM-exact | attack, quarter-sine recovery, latency |
| Pocket Leveller | Leveller (gain / balance trim) | FLM-exact | gain law, balance |
| Pocket Gate | Gate (256-sample RMS blocks, curves, hold) | FLM-exact | floor, opening, delay |
| Pocket Tape Delay | Tape Delay (variable-speed tape) | FLM-exact | echo position and spread |

(The effects this section once listed as "not yet" — Chorus, Spacer, Wow & Flutter, Phaser, Waveshaper, Compressor,
Spreader, Stereoizer, Flanger, Autoduck (29), Tremolo — are Pocket engines since rounds 188i / 189h, measured against
FL's FL Studio Mobile plugin there.)

## Round 190by: Pocket SuperSaw, Comb Filter, Multiband Compressor, Tape Stop; Reverb (32) and Tuned EQ mapped

Laws and measurements: `docs/analise/REA_rodada190by.md`. Measured = FL's command-line render of FL Studio Mobile
inside FL against LYNX's import of the same module (16-bit files: about −75 dB is a null).

| LYNX | Role (FL Mobile) | Sound | Measured against FL Studio Mobile in FL |
|---|---|---|---|
| Pocket SuperSaw (instrument, `core/PocketSuperSaw.*`) | SuperSaw (module 23, 59 knobs: unison saw / pulse, sub, SuperSync, filter with envelope, LFO, arpeggiator, chorus / phaser / delay) | ✅ FLM's laws; band-limited tables | 37 sets: 29 within 0.03 dB energy and 0.3 dB per third octave; the rest differ by FL's 16-bit clipping or by random phases / unison draws. Open: pitch-bend and mod-wheel scale |
| Pocket Comb Filter (`core/PocketFxD.cpp`) | Comb Filter (40): tape-loop comb, Manual / Lowest Note / Detect Pitch | ✅ FLM's laws, the loop's own delay law | 5 sets score 10 (−74…−78 dB); Detect Pitch 8 on tones, sweep and music; on drums FLM itself does not repeat between two renders |
| Pocket Multiband Compressor (`core/PocketFxD.cpp`) | Multiband Compressor (36): three bands, linear-domain peak compressors, width per band | ✅ FLM's laws (ballistics fixed to 44.1 kHz as in FLM); Solo knob is LYNX's own | 8 sets score 9–10 (−64…−78 dB) |
| Pocket Tape Stop (`core/PocketFxD.cpp`) | Tape Stop (42): stop / start ramps on the Play switch, Warp curve, synced times | ✅ FLM's laws | steady states exact; ⚠️ the ramps are checked by unit tests only (need a live check) |
| Reeverb 2 (+ utility) by import | Reverb (32) | ⚠️ mapped: levels within 0.4 dB, decay and width equal; PAN, WIDTH on the raw early reflections and modulation not reproduced (reported) | 5 sets |
| Parametric EQ (1–2 slots) by import | Tuned EQ (30) | ⚠️ mapped: exact when flat, −39 dB at the default bandwidth; bandwidths outside the EQ's range are reported; FLM's 8-sample delay not reproduced | 4 sets |

## Round 190dw: FL Studio Mobile's Equalizer, Graphic EQ, Filter, Autoduck (legacy) and reverbs on FLM's own engines

Laws: `docs/analise/REA_rodada190dw.md`, `docs/analise/FLM_FORMAT.md` §7.9–7.10. Measured = FL's command-line render of
FL Studio Mobile inside FL against LYNX's import of the same module (16-bit files: about −75 dB is a null); settings
mostly from FL's FLM sample songs. Before = the earlier mapping (Parametric EQ / rhythm gain / Reeverb 2), still what
projects saved with it play.

| LYNX | Role (FL Mobile) | Sound | Before → after (score, ESR) |
|---|---|---|---|
| Pocket EQ (`core/PocketFxE.cpp`) | Equalizer (3): 4 bands, ±24 dB, 2× design, channel modes | ✅ FLM's engine | 6 sets: 3–8 (−4…−41 dB) → 9–10 (−66…−83 dB) |
| Pocket Graphic EQ (`core/PocketFxE.cpp`) | Graphic EQ (7): 8 bands, 8-sample pipeline | ✅ FLM's engine | 4 sets: 2–8 (−8…−44 dB, 8 samples late) → 9–10 (−62…−75 dB) |
| Pocket Filter (`core/PocketFxE.cpp`) | Filter (9): LP / BP / HP, 24 / 48 / 96 dB | ✅ FLM's engine (Slope switches at once, as FLM) | 7 sets: 3–10 (−9…−71 dB) → 9–10 (−68…−85 dB) |
| Pocket Autoduck (legacy curve) by import | Autoduck legacy (10) | ✅ exact (module 29's engine) | 4 sets: 5–8 (−19…−41 dB) → 10 (−71…−74 dB) |
| Pocket Reverb Classic (`core/PocketReverb.cpp`) | Reverb legacy (12) | ✅ FLM's plate | 4 sets: 6–8 (−17…−20 dB) → 10 (−75…−77 dB) |
| Pocket Reverb 2 (`core/PocketReverb.cpp`) | Reverb 2 (35): separation, pan, side input, tempo delay | ✅ FLM's plate | 4 sets: 3–7 (−5…−16 dB) → 10 (−74…−76 dB) |
| Pocket Reverb (`core/PocketReverb.cpp`) | Reverb (32) | ✅ FLM's plate | 5 sets: 2–6 (−2…−6 dB) → 9–10 (−70…−76 dB) |
| Pocket GMSynth effects (`core/GmsMobile.cpp`) | GMSynth's FLNG / F HP / RING / VOX (item 4) | ⚠️ level and spectrum within 0.03 dB / 0.8 dB; RING and VOX-with-LFO phase not matched (ESR −2 dB) | 6 sets, pass-through before |

## Round 190o: Live's MIDI Transformations (core) and Scale Mode in .als (LYNX extras, Ableton-style)

| Item | FL Studio | Ableton Live 12 | LYNX |
|---|---|---|---|
| Span, Strum (grid % + tension curve), Ornament (flam, grace notes), Recombine, Arpeggiate (18 styles), Connect, Chop | its own Piano roll tools (Strum, Arpeggiate, Chop, Articulate, Flam…) | Clip View › Transform panel | ⚠️ core only (`core/MidiTools.h`), note for note against a Live 12.4 save; the panel is the next slice. FL's tools unchanged |
| Time Warp, Velocity Shaper, Glissando / LFO (MPE), generators Rhythm / Seed / Shape / Stacks / Euclidean | — | Transform / Generate panels | ❌ not built (panels and defaults recorded in REA_rodada190o.md) |
| Project key ↔ Live's Scale Mode | .flp 0xF3 (as before) | set + clip `ScaleInformation`, `InKey` | ✅ .als import and export (`core/LiveScales.h`); scales Live has and FL's list lacks import as no key |

## Round 190s: the MIDI Tools panel and Clip View pieces (LYNX extras, Ableton-style)

| Item | FL Studio | Ableton Live 12 | LYNX |
|---|---|---|---|
| Transform panel: Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum; Auto apply, Transform | tool dialogs with preview / Accept (unchanged) | Clip View › Transform | ✅ `ui/MidiToolsPanel`: the Clip View's fourth panel (Clip-launch workflow) and Piano roll › Tools › MIDI Tools…; Live's controls and defaults (`docs/analise/REA_rodada190s.md`) |
| Glissando / LFO (MPE), the Generate section | — | Transform / Generate | ❌ not built (Time Warp and Velocity Shaper: round 190aa) |
| Clip colour / activator square, pitch range (drag to transpose), Add interval | clip menu; transpose keys | Clip View title bar, Pitch & Time | ✅ Clip View column (Clip-launch workflow) |
| Set Length + grid, Humanize amount, Scale / Highlight Scale buttons in the editor header | — | Pitch & Time, editor header | ❌ open (GAPMAP-0126 D5, D13) |

## Round 190aa: the MIDI Transformations measured in Live (Ableton-style, LYNX extras)

| Item | FL Studio | Ableton Live 12 | LYNX |
|---|---|---|---|
| Arpeggiate (18 styles, overlapping notes), Connect (chords, Tie), Recombine on Grid, Span Offset, Strum (High + Tension), Quantize (incl. 1/8 + 1/8T) | its own Piano roll tools (unchanged) | Clip View › Transform | ✅ note for note against Live 12.4.16 (60-clip set, `docs/analise/REA_rodada190aa.md`); random settings checked by range |
| Chop in full: pattern row (plays / emphasis), Stretch, Variation | Chop (Alt+U) | Clip View › Transform › Chop | ✅ core note for note; panel page as Live's (Variation's law inferred) |
| Time Warp (3 breakpoints, Quantize, Range Fit, Note End) | — | Transform › Time Warp | ✅ core within 1e-5 beats on 7 settings (Note End off: up to 7 ticks apart); panel page as Live's |
| Velocity Shaper (envelope, Min / Max, Loop, Rotate, Division) | — | Transform › Velocity Shaper (Max for Live) | ✅ core note for note on 6 settings; envelope editing LYNX's own gestures |
| Glissando, LFO (MPE) | — | Transform | ❌ need MPE notes (issue #108); requirements in REA_rodada190aa.md §5 |
## Round 190as: Piano roll scripts (FL's `.pyscript`, `flpianoroll`)

Details and proof: `docs/analise/REA_rodada190as-pyscript.md`.

| FL | LYNX | | Notes |
|---|---|---|---|
| Piano roll › Tools, second column "Scripts": Run last script again (Ctrl+Alt+Y), Open script folder..., the scripts | same column, order and key; LYNX's own scripts, My scripts (`Documents/LYNX/Piano roll scripts`), and FL's factory / user / downloaded script folders when FL is installed | ✅ | FL's five shipped scripts run unmodified (read in place, never copied) |
| `flpianoroll`: score, Note, Marker, ScriptDialog, Utils | LYNX's own module on the embedded Python, same names and behaviour (what a script sees, live notes, typed members, defaults, storage rounding) | ✅ | `score.loadScoreFromFile` (undocumented) is refused: scripts cannot read files |
| Note `repeats`, Marker `scale_root` / `scale_helper` | kept for the script, not stored | ⚠️ | LYNX has no note repeats; scale markers carry no scale data yet |
| Script form: Preview (on), Reset, Regenerate, Accept; two columns of knobs | LYNX's Configure script panel with Preview on and Accept; one column of sliders; the preview shows in the Piano roll, Accept is one undo step | ⚠️ | layout differs (review pending) |
| No limits on a script | no files / programs / network; stopped after 20 s (5 s for a preview) | ➕ | LYNX's sandbox |
## Round 190bb: MPE, stage 1 (issue #108, a LYNX addition)

FL Studio 2026 has no MPE switch (docs/analise/REA_rodada190bb-mpe.md §1); LYNX adds it on FL's per-note fields.

| Feature | FL 2026 | LYNX | State |
|---|---|---|---|
| MPE controller input (lower zone, member channels 2 … 16, bend range 48) | — | Settings › MIDI › device › "MPE controller" + member channel count | ✅ played per note; recorded as the note's fine pitch (bend at the note on), Mod X (CC 74), Mod Y (peak pressure) |
| Per-note response of built-in instruments | — | Sampler (pitch, Mod X / Mod Y filter), built-in synth (pitch) | ✅ those two; ❌ the other generators |
| MPE to hosted plugins | — (fine pitch as key / VST3 tuning, Mod X as poly aftertouch) | wrapper Settings › MIDI › "MPE": one member channel per note with bend / CC 74 / pressure; CLAP note expressions (tuning, brightness, pressure) for CLAP-note plugins | ✅ ; ❌ VST3 note expression events (MPE MIDI instead) |
| Per-note expression curves in the piano roll | — | — | ❌ stage 2 |

## Round 190i: the plugin window's Detached flag in `.flp` (issue #116)
FL saves a plugin window's **Detached** state in the plugin record (`0xD4`, flags bit `0x4`; FL 2026 driven, see
`docs/analise/REA_rodada190i.md`). LYNX reads it into a hosted plugin's Detached setting on import and writes it on
export, so a plugin detached in FL opens detached in LYNX and the other way round. LYNX's own instruments and effects
have no Detached state: their imported flags are kept as they were.

## Round 190v: Crest's Two-way and Vari-mu laws, Sentry's Vocal noise reduction

Black-box, from round trial190k's reference renders of LYNX's own test signals (no reference plug-in ran in this round).
Tools and tables: `/mnt/e/lynx-work/parity190v` (`crest/bank.py`, `sentry/sbank.py`, `hash/hash.md`). Gain error = the rms
difference between the reference's and LYNX's gain in 5 ms windows; score = `scripts/fxparity.py`.

### Crest Compressor: Two-way and Vari-mu (reference styles 5 and 7 of 13)

Measured on 1 kHz stairs, pink-noise stairs and a step probe at thresholds −12 … −48 dB × ratios 2 / 4 / 10 × knees
0 … 72 dB × attacks 0.25 / 10 / 100 ms, plus a release sweep (about 880 renders) and the earlier banks.

- **Two-way** is one law over the whole grid (186 static curves fitted to 0.08 dB rms): a fixed gain, upward compression
  under threshold − 6.3 dB and downward compression over threshold + 3 dB, both corners rounded by the knee. The fixed
  gain falls as the threshold rises above −36 dB and as the attack gets slower. Its detector follows the amplitude
  (not the level in dB) with τ = 0.12 × attack and 0.19 × release, so the release knob now sets the recovery (it was
  back within 3 ms whatever the knob). Round 108's stairs at threshold −18 are one line of this grid (0.26 → 0.08 dB and
  3.0 → 0.15 dB on its two replicas): no conflict, nothing of the old fit is kept.
- **Vari-mu** reduces by the level more than by the overshoot: each static curve is a soft corner into a straight
  line whose slope, corner and width come from a measured table (threshold × ratio × knee). The corner stops following
  the threshold under about −30 dB and a larger knee reduces less (ratio 4 at 0 dBFS: −14.6 / −13.0 / −10.8 dB at knee
  0 / 18 / 72; the old law reduced more with the knee). The detector keeps its timing; the release no longer shortens
  with a slow attack, and attacks under 0.9 ms act like 0.9 ms (pink noise read 0.4 dB hot).
- Cost: Two-way about 20 % less CPU than before, Vari-mu the same (its curve comes from a table), no latency change.
- Auto gain with Two-way follows the new law (one capture: threshold −18, ratio 2, knee 24, attack 30 ms), at most 3 dB
  over the other styles' make-up.

| Style | Bank | Files | Gain error before | after | Score before | after | Score up / same / down |
|---|---|---|---|---|---|---|---|
| Two-way | tp172b + trial187b (incl. round 108 replicas) | 60 | 4.00 dB | 2.18 | 2.52 | 5.05 | 49 / 10 / 1 |
| Two-way | 190k knee grid | 72 | 4.20 | 1.75 | 2.31 | 4.71 | 72 / 0 / 0 |
| Two-way | 190k threshold × ratio × knee × attack, release | 370 | 4.36 | 1.89 | 2.39 | 3.94 | 293 / 75 / 2 |
| Two-way | all | 506 | 4.29 | 1.91 | 2.39 | 4.19 | 418 / 85 / 3 |
| Vari-mu | tp172b + trial187b (incl. round 108 replicas) | 61 | 4.38 | 0.67 | 2.28 | 6.26 | 61 / 0 / 0 |
| Vari-mu | 190k knee grid | 72 | 4.52 | 0.23 | 2.39 | 6.93 | 72 / 0 / 0 |
| Vari-mu | 190k threshold × ratio × knee × attack, release | 370 | 6.15 | 0.57 | 2.55 | 6.03 | 340 / 24 / 6 |
| Vari-mu | all | 507 | 5.74 | 0.55 | 2.49 | 6.19 | 477 / 24 / 6 |

Not better: three Two-way pink-noise files at threshold −12, attack 10 ms (gain error 1.76 → 2.02, 1.80 → 2.07 and
1.69 → 1.84 dB; the old law's level error happened to cross zero exactly there, the new one sits 1 dB low on pink
noise everywhere) and nine files whose 0–10 score fell by one or more steps with the gain error within 0.12 dB
(pink noise that is hardly compressed, two attack-100 stairs, one vocal).

Old projects sound the same: the laws are versioned by Crest's appended `Law` parameter (on = measured, for new
instances and for reference states translated on load; a project or preset saved before has no such parameter and
loads with it off = round 108's laws, `upgradeLoadedEffectParams`, like Prism's `Detector`). No control in the editor;
the parameter list shows it as measured / classic. `hash/hash.md`: all 36 legacy-state renders (every style and
voicing of Crest and Sentry) are bit-identical to 0.1.25; a new instance differs only in the three refitted styles.
No factory preset uses Two-way, Vari-mu or Sentry's Vocal style, so none moved.
For reference, the level of the measured law against the classic one on the grid (mean over knees and attacks):
Two-way −1 … +1 dB at thresholds −12 / −18, +2 dB at −24, +3.5 at −30, +5 at −36, +4.5 … +6 at −48; Vari-mu within
about 1 dB at −12 / −18, +1.5 dB at −24, +3 at −30, +5 at −36 and +8 … +11 dB at −48.

Open: the reference's Two-way depends on frequency (100 Hz gets about 1.4 dB more gain than 1 kHz, 8 kHz 4.5 – 6.7 dB
more reduction: probably bands); not modelled, it is most of the remaining 1.9 dB.

### Sentry Gate: the Vocal style's noise reduction

The reference's Vocal style turns hiss-like sound down when it starts after a pause and lets it come back while it
lasts; its other styles do not, and neither do LYNX's (bit-identical, also Vocal with ducking).
Sentry's appended `Law` parameter versions it the same way: projects saved before load with it off and render
bit-identically (no noise reduction).

- What counts: the part above 4 kHz, when it holds most of the power (10 % → 75 % of it: none → full). White noise
  low-passed at 2 kHz, tones, music and voiced notes pass untouched.
- Depth at the onset: 0.375 dB per dB of that part above −62.8 dBFS (white noise at −20 dBFS: −16 dB), × √(1 − 1 / ratio),
  softly limited by the range, less with a longer hold (none at 250 ms). The deeper of the gate and the reduction applies.
- Recovery: in gain, with a time constant that grows with the level (1.9 s at −20 dBFS, 0.6 s at −30, 0.15 s at −42).
- It arms again after a pause of 60 ms + 1.5 × the hold (fully after 250 ms more).

| Bank | Files | Gain error before | after | Worse | Score before | after | Score up / same / down |
|---|---|---|---|---|---|---|---|
| tp172b Vocal settings (programme) | 76 | 11.22 dB | 11.07 | 0 | 5.64 | 5.64 | 0 / 76 / 0 |
| trial187b G6 / G7 | 10 | 15.59 | 15.47 | 0 | 5.40 | 5.40 | 0 / 10 / 0 |
| 190k noise probes N1 – N7 | 35 | 15.19 | 14.32 | 0 | 3.69 | 4.86 | 17 / 18 / 0 |
| 190k T* | 8 | 16.47 | 16.29 | 0 | 6.12 | 6.12 | 0 / 8 / 0 |

(The gain error is dominated by windows where one gate is open and the other closed.) Open: the reference seems to
learn — with a 200 ms hold it stops reducing repeated bursts after about 3 s, a kick after a 0.2 s gap loses 7 dB, and
at thresholds −25 / −10 its onsets are far deeper (−30 … −40 dB); none of that is modelled.

## Round 190ad: hosted plugins of an FL project at FL's level
FL's wrapper plays a VST / VST3 / CLAP instrument through the pan kernel of (channel pan, 2 × the channel volume
curve): √2 × the curve at the centre (−2.2 dB at the default 78 %), where LYNX's own channel law gives the curve
(−5.2 dB). A project imported from `.flp` now follows the wrapper (level, pan in steps of 1 / 64, the project's
panning law, note velocity `Round (128 v)`): Surge XT renders within 0.05 dB of FL's at every volume and pan tested
(`docs/analise/REA_rodada190ad.md`; before: 3.0 dB under). The project keeps this as `"pluginLevels": "fl"`. LYNX's
own projects, saved or new, keep LYNX's law and sound as before; their `.flp` export writes the plugin channel's
volume that sounds the same in FL. New projects (File › New, built-in templates, user templates without a hosted
plugin) start with FL's levels; saved projects and `.als` imports keep theirs.
## Round 190an: Snap to scale by scale steps (LYNX's own option, off by default)
FL's Piano roll only highlights the scale; its free drawing is LYNX's default too. With a scale chosen, **Snap notes to
the scale** (the magnet beside the scale box, the Piano roll menu, the key action `pianoRoll.scaleSnap`, no default key)
works as in Bitwig 6's Snap to Key, through `core/ScaleSnap.h` for every note editor:

| Gesture | With Snap to scale on |
|---|---|
| Draw (pencil, brush, chord stamp) | the note lands on the nearest in-scale key (a tie goes down) |
| Drag notes (also a Shift-drag clone and a chord still held after drawing) | the grabbed note follows the mouse onto the scale; every other note moves the same number of scale steps, so C E G dragged one step is D F A |
| Drag along the time axis, resize, paste, switching the option on | pitches are left alone, also off the scale (use *Move the notes onto the scale* for that) |
| Up / Down, Shift+Up / Down (FL: one semitone) | one scale step; an off-scale note reaches the scale with it |
| Ctrl+Up / Down (FL: one octave) | one octave, as in FL |
| A group at the top or bottom of the keyboard | stops as a whole, so it keeps its shape |

Before this round a dragged group snapped note by note to the nearest key (a triad could lose its shape between two
scale keys) and an off-scale note jumped onto the scale as soon as it was dragged sideways.

## Round 190an: a key for "Automation for last tweaked parameter"
FL has the menu item (Add › Automation for last tweaked parameter) and no shortcut for it: the DFM item
`Add_AutoLastMenu` carries no `ShortCut` and FL's ShortcutsModule has no action for it (round 167j's list of 125
default shortcuts). LYNX adds the global key action `automation.lastTweaked` ("Create automation clip for the last
tweaked control"), with no default key as in FL; bind one in the shortcut editor. It does what the menu item does, for
LYNX's own controls and for a hosted plugin's parameters, and the menu item shows the key once one is bound. With
nothing tweaked yet the hint bar says so.
## Round 190am: LYNX extras (loudness meter, used inserts only, the Settings page)
None of these is an FL Studio feature: they are LYNX's own, all off by default, so FL's look and behaviour are
unchanged until one is switched on. Nothing is saved in projects.

- **Loudness meter beside the Master** (Mixer menu, `mixerLoudness`): a narrow panel right of the Master shows the
  short-term (S) and integrated (I) loudness in LUFS and the true peak (TP) in dBTP of the Master's output, a
  short-term bar with the integrated value as a line, and a Reset button. It is the meter the export and Apex Limiter
  use (`core/Loudness.h`, ITU-R BS.1770-4), run on the audio thread by `core/MasterLoudness.h`. Off, the audio thread
  does one atomic load per block and measures nothing. Measured (2026-11-10, `verify/results.txt` of the round):
  integrated loudness within 0.03 LU of the EBU Tech 3341 cases 1 to 5 at 44.1, 48 and 96 kHz and equal to ffmpeg's
  `ebur128` to its 0.1 LU read-out on four songs; true peak within +0.27 / -0.23 dB on the cases 15 to 18 and within
  0.13 dB of ffmpeg on the songs.
- **Show only used inserts** (Mixer menu, round 163) now has an exact rule, `mixerops::hiddenUnusedInserts`: an
  insert hides only when it is not the Master, is not selected, was not brought back with "+", shows no level on its
  meter and nothing uses it (`Song::insertInUse`: no channel, send, effect, input, Playlist link, sidechain or extra
  output, default name and settings, no automation). A hidden insert that is selected from elsewhere or starts to
  receive audio comes back and stays.
- **Settings › LYNX extras**: one page with every LYNX improvement that is off by default (`core/LynxExtras.h`), each
  with its switch and one line saying what it does and where its own switch is. The page stores nothing: each switch
  reads and writes the feature's own preference.
## Round 190ao: plugin window options (Reload, Pin on top, per-plugin velocity, note on preset load)
All four are options that change nothing by default.

| Option | FL Studio | Ableton Live | LYNX |
|---|---|---|---|
| Reload a crashed plugin | no separate process per plugin: a plugin crash usually takes FL down; the wrapper's Info group has a "Reload / Refresh" entry (`docs/analise/REA_rodada149.md`, not driven yet) | a crashed plugin takes Live down (no sandbox) | "Reload" in the crash message, on the crashed slot / channel menu and in the plugin window (shown in place of the editor); starts the plugin again with its last captured state, in place, with no undo step and without reopening the project. Never automatic: one reload per click. "Reload in a separate process" is offered for a plugin that ran inside LYNX |
| Pin on top | detached plugin windows stay above FL's main window; no pin among them | plugin windows have no pin; "Auto-Hide Plug-In Windows" is the opposite idea | a pin in the wrapper header and "Pin on top" in the plugin menu: the window stays above LYNX's other secondary windows (not above other applications, like every detached LYNX window). Saved per plugin instance (`pinnedOnTop`) |
| Per-plugin velocity | none in the wrapper's MIDI group (MIDIInPortWheel, MIDIOutPortWheel, SendNoteOffVelocityBox, SendPitchBendRangeBox, SendPolyAftertouchBox, MidiEventBusCombo, SendAllNotesOffBox; REA 149 C7); the curve is global (MIDI settings) | Velocity MIDI effect per track | LYNX extra: Wrapper settings > MIDI > Velocity: Follow MIDI settings (default) / Linear / Soft / Hard / Fixed, with the fixed value. For notes from a MIDI device; replaces the global curve for that instrument. Saved only when changed (`velocityMode`, `fixedVelocity`) |
| Note on preset load | none | Hot-Swap previews browser presets, not plugin programs | LYNX extra: "Play a note when a preset loads" in the plugin preset menu of an instrument (off by default, a user setting): a short C5 after a load while the song is stopped |
## Round 190al: Crest's Two-way works in three bands

Black-box, from round trial190k's reference renders of LYNX's own test signals (no reference plug-in ran in this round).
Tools and tables: `/mnt/e/lynx-work/parity190al` (`crest/fit3.py`, `crest/tw.py`, `crest/final_TTM.txt`, `hash/hash.md`);
bank and metrics are round 190v's.

What the renders show (reference style 5, our Two-way):

- Pink noise comes out with three different gains: one under about 90 Hz, one up to 2.5 kHz, one above (up to 4 dB
  apart). The step between the upper two follows a fourth-order Linkwitz-Riley split at 2.5 kHz (within 0.02 of the
  measured transition, averaged over 162 settings).
- The output's phase turns 360 degrees around 88 Hz and again around 2.5 kHz (−57 / −145 / −205 / −257 degrees at 30 /
  70 / 100 / 140 Hz; −480 / −534 / −587 at 1.8 / 2.5 / 3.5 kHz): two such splits in series.
- An 8 kHz tone is turned down from 4.6 dB lower than a 1 kHz tone and along the ratio itself (slopes 0.50 / 0.76 /
  0.92 at ratio 2 / 4 / 10, against 0.25 / 0.62 / 0.84); a 100 Hz tone behaves like a 1 kHz tone about 4 dB quieter,
  which is the share of it each split passes (−4.1 dB to the mid band, −8.5 dB to the low band).

Model: the signal is split at 88 Hz and 2.5 kHz, each band runs round 190v's law form with its own constants (fitted
jointly on the 1 kHz, 100 Hz and 8 kHz stairs of 162 settings: 0.064 dB rms), and the bands are added. The fixed gain
round 190v had to give the single band is gone: it was the high band's share of the 1 kHz tone. The pink-noise stairs
were not part of the fit: through the engine their band gains land within 0.05 / 0.14 / 0.18 dB (low / mid / high).

| Bank | Files | Gain error before | After | Worse (> 0.15 dB) | Score before | After | Score up / same / down |
|---|---|---|---|---|---|---|---|
| trial187b + tp172b programme | 60 | 2.18 | 0.34 | 0 | 5.05 | 7.32 | 53 / 7 / 0 |
| 190k knee grid | 72 | 1.75 | 0.14 | 0 | 4.71 | 8.03 | 72 / 0 / 0 |
| 190k threshold × ratio × knee × attack grid | 370 | 1.89 | 0.25 | 0 | 3.94 | 7.46 | 345 / 25 / 0 |
| 190k T* | 4 | 1.72 | 0.12 | 0 | 4.75 | 8.00 | 4 / 0 / 0 |
| All | 506 | 1.91 | 0.25 | 0 | 4.19 | 7.53 | 474 / 32 / 0 |

("Before" is round 190v's single-band law; against 0.1.25 the gain error was 4.29 dB and the score 2.39.)

- Only new instances of the Two-way style change (Law = Measured). A project saved before round 190v keeps the classic
  law, one band, bit-identical (`hash/hash.md`: 36 of 36 legacy renders identical; every other style identical to
  round 190v's build).
- Cost: the measured Two-way runs three detectors and the splits: 0.65 → 1.31 s of CPU for 72 s of audio (about 0.8 %
  → 1.7 % of one core at 48 kHz). Latency unchanged (0). The editor's curve is that of a 1 kHz tone, as before.
- Open: the low band's law rests on the 100 Hz tone alone (it sits on the split); ratios under 2 and over 10 are
  extrapolated; auto gain as in round 190v.

### Vari-mu at slow attacks (same round)

On the reference's −40 → −6 dBFS step at attack 100 ms the reduction starts at once and closes in like a smoothed
gain (−2.1 / −4.8 / −6.3 / −7.0 dB after 15 / 30 / 45 / 60 ms, towards −7.8); ours smoothed the level, did nothing
for 45 ms and settled 1 dB short. Above an attack of 30 ms Vari-mu now smooths the gain in part (a share of 0.6 at
100 ms, by the logarithm of the attack in between). The share is the most that made no capture worse: with the full
share the 30 ms programme captures and the pink-noise stairs at 100 ms lost 0.2 … 0.3 dB.

| Bank | Files | Gain error before | After | Worse (> 0.15 dB) | Score before | After | Score up / same / down |
|---|---|---|---|---|---|---|---|
| trial187b + tp172b programme | 61 | 0.67 | 0.67 | 0 | 6.26 | 6.26 | 0 / 61 / 0 |
| 190k knee grid | 72 | 0.23 | 0.23 | 0 | 6.93 | 6.93 | 0 / 72 / 0 |
| 190k threshold × ratio × knee × attack grid | 370 | 0.57 | 0.33 | 0 | 6.03 | 6.48 | 85 / 284 / 1 |
| All | 507 | 0.55 | 0.38 | 0 | 6.19 | 6.52 | 85 / 421 / 1 |

The 100 ms stairs go from 1.28 to about 0.55 dB. One file loses a score step (pink noise at threshold −48, ratio 2,
attack 100 ms: gain error 0.10 → 0.14 dB, level +0.20 → −0.20 dB). CPU: unchanged up to 30 ms, 0.49 → 0.55 s for 72 s of
audio at 100 ms. Attacks of 30 ms and under, the classic law and every other style are bit-identical. Open: the step
is still slower than the reference's (−2.4 dB after 30 ms against −4.8).
## Round 190as: "Find similar samples" in the Browser (FL's is FL Cloud's; LYNX's is local)

| FL Studio 2026 | LYNX | | Note |
|---|---|---|---|
| Sounds tab (FL Cloud, online) › a sound's menu › Find similar samples: FL Cloud ranks its own catalogue ("tone and rhythm"); Loop Starter's channel menu › Find similar samples... opens the same query. No local analysis, no index, no settings (`REA_rodada190as.md` §1) | Browser › right-click an audio file › Find similar samples: the samples of the Browser's own folders, most similar first, with a percentage, in the results list; search box "Similar to <name>" | ⚠️ | LYNX's own measure on local files (`core/SampleSimilarity.*`); FL's ranking can't be compared offline. On FL's packs the 10 nearest files sit in the query's own class folder 56 % of the time (chance 2.9 %) |
| FL Cloud's online library | — | ❌ | no FL Cloud in LYNX |
## Round 190bt: "Remote control settings" (the detailed controller link)

REA: docs/analise/REA_rodada190bt.md. One-step learning stays the default way of linking.

| FL | LYNX | State |
|---|---|---|
| Control menu › "Link to controller...": window "Remote control settings" (parameter, assignment "Link 1"... "New link", Port / Channel / Ctrl, Auto detect, Omni, internal controller, mapping formula with presets and curve, smoothing, Remove conflicts, Reset / Skip / Accept) | Right-click a control › *Link to controller...*: the same window, FL's fields, hints, ranges and defaults | Done (tests, shots). Round 190cs: FL's live layout (assignment list, Skip, internal controller group with "Record automation" shown only when FL shows them), Mode list with key / button links (Default, Hold, Latch, Inc, Stay), "Note C0" in the Ctrl menu, Omni on mixer tracks, FL's curve colours and hover read-out. Left out: channel / key aftertouch and pitch bend sources |
| Mapping formula over `Input` with FL's functions (Tension, Min, Max, Int, Round, Frac, If*, Case...) and 25 presets | `core/LinkFormula`: same variable, functions and presets; own parser | Done (tests). Round 190cs: FL's operator priorities and error texts, compile on Enter, a formula with a mistake is not kept on Accept, `inter`, `internoswap`, `fold`, `warp`, `map`, `fmod`, `!`, `%`. Incremental presets apply to absolute controllers and Inc-mode links |
| Smoothing per link: slider -1024...1024 (default 469, about 200 ms), 1 pole below 0, 3 poles above | Same slider, time law and pole count | Done; the filter's exact response needs a live check |
| Port number per link and per input device | `MidiLink::port`, Settings › MIDI › input devices › *Port* | Done (tests) |
| "Override global link...": window "Global link settings" | Same | Done |
| Multilink menu: Link to controllers..., Override global links..., Init song with these positions, Create automation clips, Cancel | In the Multilink button's menu, on the queued controls | Done; Edit events, Init pattern, Randomize, Humanize left out |
| Tempo menu: 80 BPM ... 160 BPM | Same nine items | Done |

## Round 190aw: MIDI controller profiles (phases A and B of the controller plan)

REA: docs/analise/REA_rodada190aw.md; format: docs/CONTROLLERS.md.

| FL | LYNX | State |
|---|---|---|
| Options › MIDI › Input: "Controller type" per device: "(generic controller)", device scripts, the user's with "(user)" | Settings › MIDI › Input devices: a Controller type list in each device row: Detect automatically, Generic controller, the profiles, the user's with "(user)" | Done; "Detect automatically" is LYNX's (FL: "Auto accept detected controller" switch) |
| Device scripts (`device_*.py`) drive transport, mixer, channels, plugins and send LEDs / displays | Controller profiles (`*.lynxctl`, data only) on the controller layer: transport, mixer bank of 8, Channel Rack, plugin parameter pages, pads as notes / steps; lights, motor faders, rings, SysEx text | Done for profiles; FL-compatible scripts are a later phase on the same layer |
| "Update MIDI scripts" | "Refresh device list" also reads the user's profiles again; "Controller profiles folder" opens their folder | Done |
| Links by hand and Pickup in the same panel | Unchanged; a CC linked by hand is never taken by a profile | Done (tests) |
## Round 190aq: audio-path extras (each off by default; FL's way stays the default)

These come from the cross-DAW study of round 190ak (TOP-20 items 15, 7 and 6). With the options off a project renders
what it rendered before.

| Feature | FL Studio | Ableton Live | LYNX | Status |
|---|---|---|---|---|
| Pre-fader send | none: every route between inserts is post-fader | a Pre / Post toggle per return track (all tracks' sends to that return) | a switch per send: right-click the send knob or the route arrow › "Pre-fader send". The send then leaves after the insert's effects and EQ and before its fader, pan, separation, polarity and swap; the insert's mute and solo still silence it. A "PRE" mark sits beside the knob. Saved only when used (`preSends`): an older LYNX opens the project with those sends post-fader. `.flp` export writes FL's post-fader route and says so in the report. `.als`: Live's "Pre" return comes in as pre-fader sends; a return whose sends are all pre-fader goes out as "Pre" (mixed: "Post", with a note). The planned session-recording mixer (GAPMAP-0126 item 6) uses the same field | LYNX extra |
| Low latency while recording | "Bypass track latency compensation" per track (the track's outputs are not delayed; its own slow effects still delay it) | "Reduced Latency When Monitoring" (skips the compensation, not the effects) | Record menu and Mixer › Disk recording › "Low latency while recording". While an insert with an audio input is armed or monitored "On", every active slot with a latency above 0 samples (reported, or set by hand for a hosted plugin) on that insert and on its path to the Master (sends above level 0, effect sends, the Master) is bypassed and the PDC plan is made without it; the slot shows an amber ring and a "LOW LAT" badge. Back at disarm (at stop with "Disarm the inserts when recording stops"), when monitoring leaves "On", or when the option goes off; a returning slot starts from a reset state. Never changes the take: an insert whose take is tapped after its slots (FL's default "Post EQ"), or that feeds an armed insert recording more than its device input, keeps its slots. Lives in the engine's snapshot only: the song, undo and the saved project are untouched; exports never use it. First step of the cross-DAW item (Cubase "Constrain Delay Compensation"); the dual-path engine is decision D6 | LYNX extra |
| Browser preview in time | the Browser plays a file as it is; tempo-synced audition needs the sample dropped into the Playlist (stretch) | the Browser's Preview button with "Raw" off: loops play warped to the set's tempo and wait for the global quantization | Browser options menu › "Preview in time (loops play at the project's tempo, on the beat)". The file's tempo comes from its ACID/tempo chunk, then its name ("… 128 bpm", or a bare number that fits the length in whole beats), then LYNX's tempo detection (only when the file is a whole number of beats long). It is stretched with LYNX's own stretcher (pitch kept) off the audio thread and starts on the next beat heard while the song plays; at once when stopped. One-shots, long files and files with no usable tempo play as today. Key matching is not part of this | built (round 190aq), off by default |
## Round 190ar: Ember Classic at high drive (styles T and P), the coupling ahead of the curve
Black-box on the reference's captures (round trial190k's 1 kHz stairs at drive 8 / 10, round trial187b's programme
takes). Driven hard, the reference's square wave is uneven: the side with the lower ceiling lasts 67 … 77 % of the
period from −21 dBFS up (T: +0.33 / −0.85, P: +0.32 / −0.85) and the second harmonic stays near −10 dB. Round
trial187b's tables went even at the top (±0.42; second harmonic −41 dB at −3 dBFS): their bias follows the curve's
low-passed output, which cannot grow with the input. Now the bias also takes the integral of the curve's output
(`StyleModel::servo`), so the curve's own output averages to nothing whatever the level.

| Bank | Files | Score before → after | Null before → after | Up / same / down |
|---|---|---|---|---|
| trial187b, drives 8 … 10 (20 settings) | 100 | 3.76 → 3.91 | −7.5 → −8.3 dB | 12 / 88 / 0 |
| T 8 / 9 / 10, P 8 / 9 / 10, P 10 + Punish | 35 | 3.43 → 3.86 | −4.7 → −6.8 dB | 12 / 23 / 0 |
| tp172d (drives up to 8, styles A / E / N) | 24 | 5.00 → 5.00 | −14.4 → −14.4 dB | 0 / 24 / 0 |

Versioned: the appended `Classic hot law` parameter is on for new instances; a project saved before loads with it off
and renders bit for bit as before (40 of 40 renders, every style, with and without Punish). Drives up to 5, styles A
and N, style E and T / E with Punish keep round trial187b's tables: A and N had nothing to gain, the other three each
put one take a score step down (E alone: drive 8 3.6 → 4.0 and drive 10 3.0 → 3.2, one vocal take 3 → 2).
CPU and latency unchanged (one multiply-add per oversampled sample). Open: style E's ceilings give way above about
−14 dBFS at drive 10 (their sum 1.45 → 0.62); a level-dependent ceiling scale did not lower the fit's error.
Tools and tables: `/mnt/e/lynx-work/parity190ar/ember` (`emfit3.cpp`, `apply_tables.py`, `gatecmp.py`, `gate/`).

## Round 190bp: the tail lab and the TAIL SCORE (measurement only, no DSP change)

A reverb or a modulated effect cannot null against a second correct render of itself, so the waveform score
(`scripts/fxparity.py`, unchanged) saturates near 3 ... 6 on those rows whatever the model. `scripts/taillab.py`
adds a second number beside it, the **tail score**: per octave band the decay (T20 / T30 / late slope), early decay,
clarity and pre-delay, the energy decay relief, echo-density build-up, band balance and L/R correlation in time
slices, a time-variance index (how much the tail is modulated, blind to the modulators' phase), level and dry gain
on the impulse render; long-term spectrum, wide-band envelopes, level and band correlation on programme renders;
for modulation effects the modulation spectrum of the band transfer and of the L/R level difference. Each part
loses a dead zone equal to the estimator's own noise between two phase-realisations of one reverb.
Both scores are always reported; the tail score replaces nothing.

Anchors on real renders: LYNX against itself at another modulator phase 9.6 ... 10.0 on dense reverbs (wave 4 ... 7
for the same pairs), a reference against another setting of itself 0.7 ... 9.8 (median 5.8), unrelated references
2.6 ... 6.6. Sparse cloud modes repeat only to 7.7 ... 9.1, on the reference too: that row's practical ceiling is
about 8.4. Smallest step worth claiming: 0.15 on a dense reverb row, 0.5 on the cloud row.

Baseline at d141370db (wave | tail): Epoch Verb 3.07 | 6.82, Atrium 3.50 | 7.92, Foil Plate 3.73 | 8.12 and
3.42 | 7.55, Halo 3.25 | 6.02, Horizon 3.25 | 7.71, Nebula 3.33 | 5.84, LuxeVerb 3.83 | 7.72; Orbit 3.75 | 6.32,
Lattice Mod 3.92 | 6.33, Gemini Doubler 4.44 | 7.89, Auto-pan 4.78 | 8.31, Spiral Echo 3.38 | 5.93; Echoform
1.67 ... 5.92 | 1.90 ... 6.34 over seven references.

Use: `python3 scripts/taillab.py dir <ref-dir> <lynx-dir> --stim <stim-dir> [--kind verb|delay|mod]`;
`python3 scripts/taillab.py engine <fxrender> <effect> <probe-dir> <out-dir>` for one LYNX effect on the lab's own
probes; `python3 scripts/taillab_test.py` (80 checks on synthetic decays, densities, correlations and modulations
of known value). Tables, anchors and the per-plugin loss lists: `/mnt/e/lynx-work/parity190bp/BASELINE-TAIL.md`.

## Round 190ca: Nebula's cloud law (a port of an MIT-licensed model of six networks)

The tail lab's candidate (round 190bp: the Faust Libraries function `re.valhallaSupermassive` by David Braun, MIT,
transcribed outside the product) scored 7.91 tail / 6.20 wave against Nebula's 5.84 / 3.33, above the owner's bar
for outside code (at least + 1.0). It is now Nebula's **Measured** model, written as native C++
(`src/core/NebulaCloud.h` / `.cpp`; no Faust code or runtime; MIT notice in the header and in `THIRD_PARTY.md`).
Provenance: github.com/grame-cncm/faustlibraries, `reverbs.lib` 1.6.2 (pull requests 256 and 328); the structures
follow the diagrams their designer presented at DAFx26, the laws were matched to the reference by black-box
measurement — the method LYNX allows itself.

| Mode (LYNX name) | Network |
|---|---|
| 1 Ripple | sixteen modulated lines scattered by a butterfly of rotations |
| 3 Fold | four four-channel matrix all-passes in series |
| 4 Drift | eight stereo matrix all-passes in series |
| 5 Abyss | a plain four-channel stage and three matrix all-passes inside one feedback loop |
| 7 Glint | four modulated lines with a four-channel scattering matrix |
| 20 Mirage | the same with both cuts inside the loop |

Laws (the model's, in the reference's own units, which Nebula's parameters already use): line length
D · r^k + 33 samples with r = 1 − warp³ / √8; scattering angle π · density / 4 (no mixing at 0); loop gain √feedback
(feedback networks) or all-pass coefficient 0.91 · feedback (all-pass networks); one quadrature modulator low-passed
at 0.25 Hz, excursion 500 / 48000 s · depth², one phase per line; lines read through a moving first-order all-pass
interpolator; first-order cuts. LYNX's own part: controls are glided (line lengths at 0.25 samples per sample, as
the classic network; coefficients over 5 ms), the signal path is single precision, and the looped network's global
gain is capped at 0.9995 like the others (the model lets it reach 1).

**What changes for whom.** Parameter 14, "Law" (panel: Model, Classic | Measured). New instances and the factory
presets run the Measured model in the six modes above; the other sixteen modes have no published structure and keep
the classic network under either setting (same samples, tested). A project or user preset saved before the round
loads with Classic in every mode (`upgradeLoadedEffectParams`): its renders are byte-identical to the build before
the round (78 of 78 fxrender pairs: 22 modes × 3 settings, six modes at 44.1 and 96 kHz). A change of model or mode
while playing fades the wet signal out on the old network and in on the new one (11 ms each way).

**Score, from the product's fxrender through LYNX's own state translator** (reference bank tp172d, five settings ×
impulse / drums / music; the practical ceiling of this row is about 8.4 — the reference against its own second
recording reads 7.7 and 9.1):

| | wave | tail | D | S1 | S2 | S3 | S4 |
|---|---|---|---|---|---|---|---|
| Classic (before) | 3.33 | 5.84 | 6.19 | 6.08 | 4.13 | 5.38 | 7.41 |
| Measured (the bank's render) | **6.27** | **8.43** | 8.09 | 8.06 | 9.07 | 8.19 | 8.71 |
| Measured, three other modulator phases | 6.13 … 6.33 | 8.01 … 8.25 | 8.26 … 8.63 | 8.66 … 9.14 | 6.87 … 7.06 | 7.34 … 7.86 | 8.71 |
| Classic, the same phases | 3.33 | 5.87 … 5.92 | 6.09 … 6.30 | 6.06 … 6.29 | 4.27 … 4.34 | 5.33 … 5.47 | 7.41 |

All fifteen files rise on both scores; the worst Measured phase is above the best Classic phase on every setting.
One part of the impulse half falls at two settings (L/R correlation of the single-impulse answer at Density 0:
4.9 → 1.0 and 5.0 → 1.3 at the bank's phase, 5.4 … 6.8 over the row at the other phases): a handful of modulated
echoes has no stable correlation, on the reference either.

CPU and memory: see `/mnt/e/lynx-work/nebula190ca/HANDOFF.md` (the Measured model costs less than the classic
network in every one of the six modes); + 8.4 MB per instance at 48 kHz for the sixteen 2.1 s lines.
Open: the other sixteen modes (classify each from black-box impulses into one of the five structures and fit its
laws; one or two rounds), and the factory presets, which were written on the classic network.

## Round 190ce: Browser collections, hot-swap, device commands (LYNX extras, Ableton-style)

| Item | FL Studio | Ableton Live 12 | LYNX |
|---|---|---|---|
| Collections: seven colour labels per item, several at once; rename, show / hide; keys 1-7 and 0 | one Starred flag (STARRED tab) | Browser sidebar › Collections | ✅ Starred is collection 1, six more (`core/BrowserCollections.h`); the STARRED tab lists the collections shown; the key of each colour needs a live check (`docs/analise/REA_rodada190ce.md` §1) |
| Search by a collection's name | — | search matches tags | ⚠️ collection names only; no other tags |
| Automatic preview switch | a click previews | headphone button | ✅ Browser options › Preview files when they are selected |
| Show a search result in its folder | — | Show in Places | ✅ right-click › Show in its folder |
| Hot-swap (Q): the Browser replaces the linked device, preset or sample in place | Replace in the slot / channel menus (unchanged) | Hot-Swap mode | ✅ `core/HotSwap.h`: effects, instruments, effect / synth presets, samples; bar in the Browser, button on the device card. ❌ rack chains, drum pads, similar-sample swap |
| Device Cut / Copy / Paste / Duplicate, Ctrl-drag copy, 0 = on / off, Ctrl+G / Ctrl+Shift+G | slot menu, Alt-drag in the Mixer | Device View edit commands | ✅ effect cards of the Device View (Ableton-style layout). ❌ several cards at once, drag to another track |
| MIDI / Key Map Mode overlay | Link to controller, Multilink | Ctrl+M / Ctrl+K | ✅ Clip-launch layout (`ui/MapMode.h`, `core/RemoteMap.h`; REA_rodada190ef.md §3 and §5, checked against Live 12.4.16 in round 190em: Min-first key toggle, Min / Max in units, Path "track \| device"). ❌ Macro Map Mode, relative navigation |

## Round 190cd: the audio editor as a mixer-slot plugin (FL's Edison)
Static study of FL's own Edison plugin (docs/analise/REA_rodada190cd.md). The plugin is LYNX's existing audio editor
in a slot mode: one editor, no fork.

| FL | LYNX | Status | Notes |
|---|---|---|---|
| Edison loads in a mixer insert slot, several instances | effect "Edison" in the effect list, any slot, several per track | ✅ | its window is the audio editor |
| Record modes Now / On input / Input / On play, default On input | same names, order, default; hold 1 s, pre-roll 0.1 s, threshold 0.02 | ✅ | the 0.2 s trimming step of FL's buffer is not reproduced |
| Maximum recording time: Forever, 1, 3, 5, 10, 30, 60 min, default 5; megabyte limits | the seven time choices, default 5 min | ⚠️ | megabyte limits left out |
| APPEND (on by default) | Append: on = after the audio, off = replaces it | ⚠️ | what FL does at the moment Record is pressed with Append off: needs a live check |
| Input passes through; Mute input; own playback added to the slot | same; pass-through bit-exact | ✅ | |
| Slave playback to host, Ignore host / own selection | Slave: plays at the song time from the selection start; "Ignore own selection" | ⚠️ | host selection not used; time unit needs a live check |
| On play: "Song jump" marker when the song position jumps | same marker, one per jump (loop passes included) | ✅ | |
| Sample saved inside the project (embedded) | saved as a WAV in the project's recordings folder, path in .lynx | ⚠️ | deliberate: the rule of takes; small project files and undo steps |
| Edison slot in an .flp | kept as Edison with its settings; audio, markers, regions and loop decoded from the state | ✅ | envelopes of the state not read |
| Mixer track: open audio editor (Ctrl+E) / and record (Shift+E) load Edison in the first empty slot | same | ✅ | a full track opens the plain editor window |
| Send to playlist / to channel | Send menu from the slot window, plus "Send to the selected channel" | ✅ | no mouse drag out of the editor |
| Select menu: before / after selection, zoomed part, previous / next region; Zoom on left / right of selection | same commands in every audio editor window | ✅ | |
| Regions: Rename all..., Set info..., Clone as region | same | ✅ | Quick rename all, Auto rename all, trigger-note assignment: left out (flows not established) |
| View: regions / markers / loop switch, noise threshold mark | View menu switches "Regions", "Loop", "Noise threshold mark" | ⚠️ | menu items, not FL's toolbar buttons |

## Round 190bz: the measured engine law of the reverbs (R1, R4, R5; new instances only)

The 8-line network of `core/LynxVerbs.cpp` (Epoch Verb, Atrium, Foil Plate, Halo, Horizon) has a second engine law.
Each of the five effects got an appended parameter **Law** (Classic | Measured, default Measured): a project saved
before this round ends one parameter short, so `upgradeLoadedEffectParams` loads it as Classic and it renders bit for
bit as before (135 of 135 bank renders byte-identical to the base commit with Law = Classic, 44.1 / 48 / 96 kHz
included; ten render hashes in `tests/Reverb190bzTests.inc`). Nebula keeps its one engine (its port is a round of its
own); LuxeVerb runs on its own 16-line engine (`core/SpaceVerb.h`) and is not touched.

What the Measured law adds (law 1 in `verbs::Settings`, all neutral unless a mode's table says otherwise):

| Part | What | Plan item |
|---|---|---|
| Per-band decay | the three-band absorption is replaced by six first-order shelves + a gain per line, fitted (least squares) so the loss per pass at eight octave bands is the classic law's divided by a per-band multiplier (`bandRtMult`); the T60 per band follows `verbs::bandTargets` within 10 % on the test rows (125 Hz … 8 kHz at 44.1 / 48 / 96 kHz); line lengths are whole samples, so a still line is read without interpolation loss | R1 |
| Tone corrector | an eight-band proportional equaliser after the loop (`bandDb`, gains solved through the bands' interaction) + three optional narrow bells | R1 |
| Onset | a three-step diffusion network (delay, polarity, 4-point mix per step: 64 arrivals inside `onsetSec`) beside the tank, at the input's energy per unit level | R4 |
| Density | the mixing angle inside the halves (`mixAngle` × 45°; 1 = the classic Hadamard) | R2 / R7 |
| Modulation | `mod` scales the delay modulation per mode; `matrixModDepth` swings the rotations of the mixing matrix (orthogonal at any depth), which moves low and mid bands as well | R5 |
| Wet level | `gainDb` per mode | level laws |

Fitted with `scripts/taillab.py` against the black-box reference renders of bank tp172d (closed loop on the lab's
signed differences: T30 per band, band spectra of impulse and programme, level, onset level by grid). Tables
`epochLaw` (Concert Hall, Room), `atriumLaw` (Large Room, Bright Room), `foilLaw` (Silver, Pewter), `foilMeasuredLaw`
(the measured plate voicing at 1 / 3 / 10 s decay, blended on the decay's logarithm), `haloLaw`, `horizonLaw`
(Chamber). Every other mode runs the new loop with neutral numbers (decay per band and level within the classic
law's; test `reverb190bz_unfitted_modes_stay_close_to_the_classic_law`).

Rows (wave | tail, base d141370db + 190bp → this round; the lab's anchors: a reference against its own second
recording 9.57 median, other settings of the same reference 5.80, unrelated references 3.83):

| Row | Wave | Tail | Per setting (tail) |
|---|---|---|---|
| Epoch Verb | 3.07 → 3.73 | 6.82 → **7.99** | D 7.65 → 8.20, S1 6.16 → 7.12, S2 4.82 → 7.95, S3 7.44 → 8.49, S4 8.00 → 8.17 |
| Atrium | 3.50 → 3.58 | 7.92 → **8.72** | D 8.01 → 8.81, S1 8.11 → 8.84, S2 7.66 → 8.75, S3 7.90 → 8.47 |
| Foil Plate (hall plate reference) | 3.73 → 3.73 | 8.12 → **8.84** | D 8.34 → 8.95, S1 8.09 → 8.88, S2 7.69 → 8.55, S3 8.16 → 8.87, S4 8.33 → 8.95 |
| Foil Plate (small plate reference) | 3.42 → 3.50 | 7.55 → **8.46** | D 8.00 → 8.76, S1 6.29 → 8.28, S2 8.31 → 8.56, S3 7.61 → 8.23 |
| Halo | 3.25 → 3.50 | 6.02 → **7.02** | D 6.70 → 7.80, S1 5.04 → 6.51, S2 6.56 → 7.44, S3 5.78 → 6.31 |
| Horizon | 3.25 → 3.58 | 7.71 → **8.25** | D 7.78 → 8.29, S1 7.70 → 8.31, S2 7.61 → 8.10, S3 7.77 → 8.30 |
| Nebula, LuxeVerb, the 18 other rows | unchanged | unchanged | renders identical |

What moved: decay 7.1 → 8.1 / 7.6 → 9.5 / 8.2 → 9.7 / 9.1 → 9.7 / 5.1 → 6.4 / 9.0 → 9.6 (Epoch, Atrium, the two plate
rows, Halo, Horizon), early energy (C80) 8.0 → 8.8 / 8.2 → 8.7 / 7.9 → 9.1 / 7.3 → 8.9 / – / 5.8 → 7.6, impulse level
7.2 → 8.8 / 8.8 → 9.7 / 8.9 → 9.7 / 7.5 → 10.0 / 5.3 → 8.4 / 9.2 → 9.8, Halo's modulation part 1.8 → 7.2 (delay
modulation × 6: its reference moves in every band). What did not: echo density (4.9 on Epoch, 5.5 on Halo: the
build-up law of the references is not the mixing angle's; the angle gave under 0.15 per row and was taken only on
Atrium's Bright Room and Horizon's Chamber), Halo's shimmer spectrum per setting (S1, S3).

CPU (thread time, 20 s of noise bursts at 48 kHz, blocks of 512, Measured against Classic): Epoch −31 % (Room mode
−20 %), Atrium −16 % (Bright Room −16 %), Foil −16 % (measured plate −15 %), Halo −30 % (with shimmer −15 %),
Horizon −14 % (Chamber −10 %): the shelves run eight lines wide and the modulators are rotations instead of `sin` calls.

The devices that only borrow Foil Plate (rule `complete-access-reverbs`) stay on the Classic law (`Law=0` in the
rule's preset): their maps were fitted on it, and their rows are unchanged.

## Round 190cm — the 0.1.27 integration made green

No sound law changed; what the full suite found in the new 0.1.27 code is closed in the product:

- **Pocket SuperSaw** follows LYNX's channel workflow like FL's generators: channel Mono / Porta (the rack's glide is
  taken at note on, `HostSlide`), piano roll slide notes (each sounding voice glides to the slide note and keeps its
  key), the channel's PITCH knob, fine tune and main pitch live on sounding notes and on the arpeggiator's notes, and
  the MIDI wheel through the module's own **Pitch bend range** (1…12 semitones; it was a stored value with no source).
  The module's own Monophonic mode and Slide time are unchanged and stack with them. Parity against FL Studio Mobile:
  the 37 A/B renders of round 190by are byte-identical before / after.
- **Foil Plate**: switching **Voicing** (plate types ↔ the measured plate) while playing fades the wet signal out on
  the old tank and in on the new one (11 ms each way, the fade a type or Law change already had), under both laws.
  A steady setting renders the same samples as before (Classic: the 190bz goldens and the 135-render identity).
- **Reverb stand-ins**: SSL Native FlexVerb and VerbSuite Classics keep their fitted starting presets of round 190ca
  but open at the role's 25 % slot mix again (the effect all wet), since nothing in their saved state is translated;
  `default = … ; mix=0.25` in the substitutes table. SSL PlateVerb / SpringVerb are unchanged (their own even mix).
- **Keys**: Ctrl+Shift+M (new empty MIDI clip in the clip grid), Ctrl+I and Ctrl+U (insert warp markers, quantize
  audio in the audio editor) act as default keys only in the Clip-launch layout (`core/SessionKeys.h`); in the Pattern
  and Simple layouts the commands stay in their menus without a key, so Export › MIDI file keeps Ctrl+Shift+M and
  "Recording waits for input" keeps Ctrl+I.
- **Mixer insert menu**: "Clean voice" (LYNX's own) sits in the menu's Sound section with LYNX's other sound items.
- Audits: the generator and effect control audits now know the contexts of the Pocket engines' conditional controls
  (`tools/CtlWake.h`, `tools/FxAudit.h`); Pocket Tape Stop silent with Play off is FL Studio Mobile's behaviour.

## Round 190co: Apex Limiter holds the true-peak ceiling; the true-peak meter reads what a 16× meter reads
Round 190cn measured the 0.1.26 limiter with True peak on and the ceiling at −1 dB: up to +1.07 dBTP with
oversampling off, and LYNX's own meter 0.2 … 2.4 dB under a reference meter on limited material. Two causes: the
limiter's last word was a clip of the samples at the base rate (flat tops overshoot between the samples), and the
meter's 4-point interpolator was 12 taps long and never looked at the samples themselves.

Reference meter of this round (and of `tests/ApexFix190coTests.inc`): 16× polyphase, 128 taps per phase, Kaiser
β = 10, the sample peak folded in. ffmpeg's `ebur128=peak=true` (4×, libswresample's 32-tap filter, printed to
0.1 dB) is the second opinion.

**Meter** (`core/Loudness.h` `TruePeak`, used by the Loudness meter, the mastering assistant, the Master loudness
read-out and Apex's own display): a half-band cascade, 1× → 2× with 128 taps (Kaiser β = 10), 2× → 4× with 12 taps,
a parabola through every local maximum of the 4× sequence, and the samples themselves. Against the reference over 60
files (five signals, three rates, limited and not): 0.1.26 −2.41 … +0.25 dB, now −0.024 … +0.012 dB. The reading is
68 samples late; `Meter::flushTruePeak` runs the tail out at the end of a file. Cost of the true-peak part, stereo at
48 kHz: about 0.25 → 0.35 % of one core (g++ −O2, best of five on a loaded machine; quiet stretches cost nothing, the
filters sleep under −60 dB).

**Limiter**: with True peak on, a guard stage ends the chain at the base rate, after the oversampler: the true peak of
every sample period by the meter's estimator and by a short 4× interpolator of the kind most meters use (the higher
counts), required gain = ceiling / peak taken 6 % deeper, held over the ramp and 8 samples each side and smoothed by
two running means of 0.125 ms. Under the ceiling the guard is a delay, bit for bit. Dither, Unity gain and Audition
moved behind it. (Round 190co2 tuned the dip: 0.25 ms cost the trial187b bank a step, 6.800 → 6.763; 0.125 ms with 8
samples each side keeps every bank's score and holds the ceiling as well.)

Highest output over the ceiling, True peak on (16× reference; ffmpeg in brackets), 0.1.26 → now:

| Signal (round 190cn's) | 44.1 kHz, Off | 48 kHz, Off | 96 kHz, Off | 2× … 8×, all rates |
|---|---|---|---|---|
| Dense mix, + 12 dB | +1.64 (+1.0) → +0.03 (0.0) | +1.59 (+1.1) → +0.02 (0.0) | +1.34 (+1.1) → +0.02 (0.0) | +0.24 (+0.4) → +0.02 (0.0) |
| Clipped sweep, + 6 dB | +2.07 (+1.9) → +0.01 (0.0) | +2.07 (+1.9) → +0.01 (0.0) | +2.06 (+1.8) → +0.01 (0.0) | +0.10 (+0.2) → +0.02 (0.0) |
| Quarter-rate sine, + 6 dB | −0.08 → −0.08 | −0.08 → −0.08 | −0.08 → −0.08 | −0.01 → −0.01 |
| White noise, + 12 dB | +1.65 (+1.0) → +0.02 (+0.1) | +1.80 (+1.4) → +0.02 (0.0) | +2.03 (+1.3) → +0.02 (0.0) | +0.32 (+0.2) → +0.02 (0.0) (4×) |
| Square, + 9 dB | +2.85 (+1.4) → −0.10 (−1.5) | +2.85 (+1.4) → −0.10 (−1.5) | +2.85 (+1.4) → −0.14 (−1.5) | +0.21 → 0.00 (4×) |
| Mix, look-ahead 0 | +1.94 → +0.01 | +2.19 → +0.02 | +1.98 → +0.02 | +0.40 → +0.02 (4×) |
| Mix, Loud, ceiling 0 dB, + 18 dB | +1.64 → +0.03 | +1.59 → +0.02 | +1.34 → +0.02 | +0.24 → +0.02 (4×) |
| Mix, Unity gain | +1.64 → +0.03 | +1.59 → +0.02 | +1.34 → +0.02 | +0.62 → +0.02 (4×) |

72 renders with True peak on: highest +0.026 dB (0.1.26: +2.85); ffmpeg reads at most +0.1 (its 0.1 dB step). True
peak off: the 9 renders are the same files, byte for byte, and so are 36 more (oversampling Off/2×/4×/8× × three
styles × three signals), and the latency is unchanged. Per-render table: `/mnt/e/lynx-work/apexfix190co/table_final.md`.

Not versioned: a project saved with True peak on now holds its ceiling (the switch never did what it says; no "law"
parameter). True peak off is the 0.1.26 limiter exactly. What changes for a saved project with True peak on: the
overshoots are gone (on the dense mix at 48 kHz, + 12 dB into −1 dB, 2.3 % of the samples differ from 0.1.26's with
oversampling off and 1.2 % at 4×, latency-compensated; the level changes by −0.003 / −0.000 dB RMS) and the reported
latency grows by the guard's. FG-X 2 substitutes now run with True peak off, as the reference's own switch says.

Latency (samples; reported to the host, `apex::latencyFor (rate, factor, truePeak)`), 0.1.26 → now with True peak on:
44.1 kHz 225 → 310, 48 kHz 244 → 329, 96 kHz 484 → 581; + 55 with oversampling on, as before. True peak off: 225 /
244 / 484 as before. Switching True peak changes the reported latency the way the oversampling switch always did.
CPU (48 kHz, dense mix, % of one core, g++ −O2, best pass, 0.1.26 → now), True peak on: Off 0.96 → 1.60, 2× 3.52 →
4.20, 4× 6.36 → 7.00, 8× 11.13 → 11.95; True peak off: unchanged (0.93 / 3.37 / 6.05 / 10.68 → 0.87 / 3.40 / 6.07 /
10.68).

Parity banks (fxparity score; the reference captures themselves read up to +1.18 dBTP over their ceiling with their
true-peak switch on, mean +0.30 dB over 100 takes; LYNX 0.1.26 read up to +3.61, mean +1.01; now up to +0.03):

| Bank | Files | Score before → after | Null before → after | Up / same / down |
|---|---|---|---|---|
| tp172b, Pro-L 2 | 24 | 7.083 → 7.083 | −41.1 → −40.8 dB | 1 / 22 / 1 |
| trial187b, Pro-L 2 | 80 | 6.800 → 6.800 | −28.2 → −28.1 dB | 3 / 73 / 4 |
| tp172c, SSL X-Limit | 16 | 4.938 → 4.938 | −17.0 → −17.0 dB | 0 / 16 / 0 |
| tp172c, FG-X 2 | 12 | 7.583 → 7.750 | −90.2 → −90.7 dB | 2 / 10 / 0 |
| tp172c, Bus Clipper | 12 | 3.333 → 3.333 | −12.2 → −12.2 dB | 0 / 12 / 0 |

No bank's score drops. The Pro-L 2 takes that went down a step (tp172b S1 drums, trial187b L15 / L16 drums, L5
bursts) are takes where the reference lets through more than its ceiling (drums: +0.7 … +1.2 dB over) and 0.1.26
happened to overshoot with it; the ones that went up are where 0.1.26 overshot more than the reference. trial187b L14
stair is the scorer, not the sound: LYNX's 3.2 kHz third-octave band is the same before and after (63.0 dB), the
reference's sits on the 60 dB inclusion threshold (29.5 / 29.6 dB) and crosses it because the longer latency changes
the aligned overlap by 95 samples. Mean distance between LYNX's and the reference's true peak: 0.71 → 0.33 dB.

FG-X 2 (round 190co2): its captured states store the reference's true-peak switch (`LevelTruePeakLimiting`) at 0.25,
off, the way they store their bypass switches, and its renders run +2 … +5 dBTP over a sample peak at the ceiling. The
fitted map left that switch out, so Apex ran with True peak on (its default); now that True peak holds, the bank fell
to 7.417. The map now carries the switch (core/FittedMaps.inc, by hand until the generator's map in
`/mnt/e/lynx-work/parity190ar/maps/FG_X_2.json` has it): 7.750.
Tools and tables: `/mnt/e/lynx-work/apexfix190co` (`tp.py`, `bank.py`, `pb.py`, `partp.py`, `scorediff.py`,
`metertable.py`; round 190co2's variant search and final runs in `var/`, `parity_final/`, `table.py`).

## Round 190do: deterministic modulation — Spiral Echo, Orbit, Lattice Mod, Gemini Doubler (new instances only)

Plan 0.1.27 §4 item 5 (track B). Each of the four effects got an appended law parameter (**Law**; Gemini Doubler:
**Head law**), Classic | Measured, default Measured: a project or preset saved before ends one parameter short, so
`upgradeLoadedEffectParams` loads it as Classic and it renders bit for bit as before (16 of 16 fxrender comparisons with
the base commit byte-identical; eight render fingerprints from the base commit's objects in `tests/Mod190doTests.inc`,
hash-checked on glibc ≥ 2.43, energy / sample tolerances everywhere). All facts below are black-box: the tp172d
reference renders and the refcap0124 one-parameter captures, tracked with the work folder's scripts.

| Effect | What the Measured law changes | Evidence |
|---|---|---|
| Spiral Echo | loop cuts as first-order bilinear sections (low cut at 0.8 × the knob), a second-order all-pass at 2875 Hz (Q 0.34) in the loop, loop limiter 0.256 · tanh (x / 0.256), delay changes glide with τ = 0.175 s, the shifter's oscillator as a rotating phasor | per-echo transfer and group delay of S1 / S3 echo trains; level of 100 ms windows against loudness (3 … 4 dB under the classic loop on loud material); refcap delay sweeps (exponential glide, τ 0.16 … 0.18 s); fitted on the 16 bank files |
| Orbit | Rise: two taps sweep the whole depth (0 … depth; classic 0.25 … 0.75 above a 0.25 ms base). Rise / fall: left channel rises, right channel falls, in phase (classic: one triangle tap). Feedback 0.7 · \|x\|^0.891 (0.377 at +50 %; classic 0.247) | taps tracked over 8 s of music (deconvolution, 60 ms windows): 48 samples/s at 0.10 Hz × 10 ms, L and R a quarter cycle apart, start phase 0 after a reset |
| Lattice Mod | detune excursion × 3.5 (the classic law multiplied both 50 % settings into a quarter of the ±25-cent swing) | refcap captures: taps anywhere in 340 … 739 samples for a 10 ms delay at the default depth (±200 samples ≈ ±22 cents at 0.5 Hz) |
| Gemini Doubler | Align off: one read head per voice drifting by (1 − ratio) per sample away from the delay and jumping back by 252 samples (2-sample crossfade), linear reads (classic: two taps half a 13.3 ms window apart, sin weights: a fixed 150 Hz comb inside each voice) | refcap default impulse (one tap per voice); voices tracked over 8 s: V2 within [delay + 2, delay + 254], V1 within [delay − 265, delay − 10], jumps every 1.52 … 1.57 s |

Bank (tp172d, same renders and scorer as SCOREBOARD-v3; tail = `scripts/taillab.py`, round 190bp):

| Reference | LYNX | Files | wave before → after | tail before → after | files up / same / down | mean ESR |
|---|---|---|---|---|---|---|
| ValhallaFreqEcho | Spiral Echo | 16 | 3.38 → 3.88 | 5.93 → 6.79 | 7 / 9 / 0 | −3.5 → −7.3 dB |
| ValhallaSpaceModulator | Orbit | 12 | 3.75 → 5.25 | 6.32 → 8.27 | 9 / 3 / 0 | −2.6 → −9.0 dB |
| ValhallaUberMod | Lattice Mod | 12 | 3.92 → 4.00 | 6.33 → 7.05 | 1 / 11 / 0 | −0.0 → −0.5 dB |
| Doubler2 Stereo | Gemini Doubler | 9 | 4.44 → 4.67 | 7.89 → 8.63 | 2 / 6 / 1 | −3.0 → −3.0 dB |

No row drops on either score. The one file down (Gemini D_vocal 6 → 4: 50 ms envelope correlation 0.982 → 0.970) is
set by where the reference's free-running heads happened to be: its heads are not reset by the host's reset (their
offset at the start of the vocal and the music file of one setting differ by ~500 samples), so no reset-deterministic
model can null those files; S2 (direct off) music / vocal went 4 → 6 with spectra 1.8 / 2.0 → 0.5 dB.
Not modelled (left out): Orbit's other nine modes (not in the bank, no evidence), Spiral's near-zero-delay resonator at
its default (D: the reference's loop phase is not matched by the fitted all-pass; spectrum 3.5 → 2.8 dB, still step 3),
the reference's startup glide in the impulse captures (a capture-history effect), Lattice's per-mode LFO designs.
CPU and the work folder: see `/mnt/e/lynx-work/mod190do/HANDOFF.md`.

## Round 190dm: dynamics parity — Sentry's transient trigger, Glow's cell memory, Hush's Air and Mix, the three-band bus compressor on Prism

Roadmap item 6 of the 0.1.27 parity plan (research190bl §3.15–3.17, §3.19), highest gain per row first. Everything
was measured black-box on the references' recorded probes (trial190k: `tc`, `stairs`, `pinkst`, `stereo`, `ess`,
`nburst`); the programme banks were only scored, never fitted. No third-party code. Tooling and logs:
`/mnt/e/lynx-work/dyn190dm/` (`lab/`, `fit/`, `bank.py`, `runs/`).

**Sentry Gate — Trigger: Level | Transient** (appended `Trigger`, `Sensitivity`, `Release shape`; default Level = every
gate before, sample for sample). The drum-gate reference is not a level gate: an onset over the threshold opens it at
once, and its gain then follows one curve whatever the input does, down to the range — a steady tone that jumps from
−40 to −6 dBFS opens it and is gone 0.8 s later, a stair of +6 dB steps never reopens it, range 40 and 100 run the same
curve until −40. Measured on the tone-burst probe at six release / shape settings:
- hold = 18.75 ms + 5.6 % of the release, then −slope · u dB (u = time / release) times a fade to silence
  (1 − (u / end)^power): Smooth 16.1 dB per release, no fade; Mid 15.8 dB, end 1.45, power 3.5; Sharp 10.0 dB, end 0.86,
  power 2.5 (rms 0.06 … 0.31 dB over release 52 ms … 3.5 s);
- the onset detector: the side chain's first difference (unity at 1 kHz — or at the side chain's low-pass when lower —
  85 %) plus 15 % of the side chain itself, a peak follower (instant / 10 ms) and a slower follower of it (5 / 50 ms);
  an onset is the fast one over
  max (slow, threshold) by 8 dB (sensitivity 5), re-armed 3 dB lower. On all 30 probe captures (D, S1, X1–X3 and the
  release / slope / threshold sweeps) the gain trace is within 0.06 dB mean (a 100 Hz note at −48 dBFS from silence
  does not open it, 8 kHz at the same level does; clicks on a pink bed do, pink steps do not).
Sensitivity 1 … 10 scales the 8 dB (own law: 8 · 10 / (s + 5)); the reference's other values were not captured. The
normalisation at a lower side-chain low-pass is not pinned by the probes (the reference never opens on the X3 probes,
detection low-pass 150 Hz, and neither law does): it is chosen so a kick's low-passed side chain still sees its onsets,
and the programme rows at X3 agree (prog182 4.32 → 4.76 with it, every other file the same).

**Glow Leveler — Opto memory** (appended, Opto classic voicing, Compress; default off). The FG-2A-type reference
recovers fast after a short burst and slowly after a long one (0.09 dB left 1.1 s after a 10 ms burst, 2.0 dB after 2 s).
The memory law adds an exposure state E (0 … 1) that charges towards the light (drive / (1 + drive)) with 394 ms while
lit and fades with 2.44 s; both conductances release (1 + 4.24 · E^1.23) times slower, with the round-123 cell refitted
around it (optofit: Nelder-Mead, tc probe at D / S1 / X1 / X2): mean gain error 0.94 → 0.57 dB (D 0.77 → 0.29). Left
after a 10 ms burst at 1.1 s: 0.07 dB (round 123's law 1.17); after 2 s: 1.29 dB (reference 2.00). The two other
optical references whose maps also run round 123's release take it too, decided on their own tc probes (mean gain error
SD-3A 1.31 → 0.97 dB, Custom Opto 0.99 → 0.85 dB).

**Hush De-esser — Air and Mix** (appended; defaults off / 100 %). The E2-type reference passes a kit and a mix brighter,
not darker: its wet path carries a static high shelf (on by default) — measured on the noise-burst and stereo probes
at Dry/Wet 100 %: an RBJ high shelf at 8151 Hz, S 0.5, +8.5 dB (0.05 dB rms, 500 Hz … 16 kHz) — and its default Dry/Wet
is 50 %. Air (0 … +12 dB) puts that shelf on the processed signal; Mix blends the dry signal back, delayed by the
latency (Mix 0 nulls with the input below −100 dB). This is what round 190ar's relative detector could not find: the
kit rows were 1.2 dB low because Hush cut what the reference lifts.

**Re-paired: the two three-band compressors → Prism Multiband** (maps only, no DSP change; plan §3.17 item 5, §7
question 7, owner: "do the recommended"). SSL G3 MultiBusComp was paired with Crest and MO-TT with Compressor (one band
each); Prism runs three bands on the reference's own crossovers (120 Hz / 6 kHz, 88 Hz / 2.5 kHz), the three bands set
alike as the references' settings move them (a tied map, fitted on the probes with parity190ar's fitter;
`fit/fit190.py`, `fit/specs190dm.py`). MO-TT's upward half has no Prism counterpart in the same band (a band compresses
up or down by the sign of its range): the fit chose deep downward bands with a high make-up, which levels the bands from
both sides on programme material; an upward branch per band (plan §3.17) stays open. The missing-plugin table follows
where the stand-in at its measured default is better on every bank setting (core/PluginSubstitutes.cpp): G3 on Prism
(2.50 / 2.36 → 4.25 / 2.76, tp172c / programme), the drum gate on Sentry's transient trigger (2.25 / 1.73 → 4.12 / 3.41);
MO-TT stays on the Compressor (its Prism default would score 2.92 / 2.48 against 3.08 / 2.56).

Scores (same banks and score as SCOREBOARD-v3; before = origin/wip/rollup027 77026c3a2 with the v3 maps, after = the
branch; per-file up / same / down):

| reference | LYNX plugin | bank | before → after | up / same / down | went down | change |
|---|---|---|---|---|---|---|
| VM Gate Drums | Sentry Gate | tp172c | 1.88 → **5.00** | 5 / 3 / 0 | – | Trigger = Transient (map from the probe laws) |
| VM Gate Drums (programme) | Sentry Gate | prog182 | 1.18 → **4.76** | 18 / 6 / 1 | X2_bass 3→2 | |
| SSL G3 MultiBusComp | Crest → **Prism Multiband** | tp172c | 3.62 → **6.00** | 7 / 1 / 0 | – | re-paired, three tied bands |
| SSL G3 MultiBusComp (programme) | Crest → Prism | prog182 | 3.32 → **5.16** | 22 / 3 / 0 | – | |
| MO-TT | Compressor → **Prism Multiband** | tp172c | 2.92 → **4.08** | 8 / 4 / 0 | – | re-paired, three tied bands |
| MO-TT (programme) | Compressor → Prism | prog182 | 2.76 → **4.36** | 17 / 7 / 1 | D_drums 4→3 | |
| Eiosis E2Deesser | Hush De-esser | tp172b | 4.44 → **5.12** | 6 / 5 / 5 | S1/S2/S3_sweep 5→4, S2_drums 3→2, S2_music 5→3 | Air + Mix, map refitted on the probes |
| Eiosis E2Deesser (programme) | Hush De-esser | prog182 | 5.90 → **6.95** | 13 / 6 / 1 | S2_drums 4→3 | |
| VM FG-2A | Glow Leveler | tp172c | 3.42 → **3.75** | 3 / 9 / 0 | – | Opto memory on (v2 map + the switch) |
| VM FG-2A (programme) | Glow Leveler | prog182 | 4.68 → **4.80** | 3 / 21 / 1 | S1_bass 5→3 | |
| VM SD-3A | Glow Leveler | prog182 | 4.52 → **4.64** | 4 / 19 / 2 | X3_mix 5→4, X4_vocal 6→5 | Opto memory on (v3 map + the switch) |
| VM Custom Opto | Glow Leveler | prog182 | 4.96 → 4.96 | 0 / 25 / 0 | – | Opto memory on (same scores) |

Per plugin (the v3 rows; the G3 and MO-TT rows move to Prism):

| LYNX plugin | v3 rows | mean before | mean after (same rows) | after, re-paired rows moved to Prism |
|---|---|---|---|---|
| Sentry Gate | 8 | 6.35 | **6.74** | 6.74 |
| Hush De-esser | 5 | 6.28 | **6.42** | 6.42 |
| Crest Compressor | 13 | 5.10 | **5.28** | 5.22 (12 rows) |
| Compressor | 10 | 4.76 | **4.87** | 4.96 (9 rows) |
| Glow Leveler | 4 | 4.52 | **4.64** | 4.64 |
| Prism Multiband | 1 | 7.60 | 7.60 | 5.89 (3 rows: Pro-MB 7.60, G3 6.00, MO-TT 4.08) |

The other 33 rows of the five plugins score exactly as before (same files, same scores).

Off = the base commit's sound: `tests/Dyn190dmTests.inc` renders 11 settings of the three effects (defaults and the
other voicings / styles) hash-identical to the base commit (glibc ≥ 2.43 on x86-64; energy and samples within 1e-4 /
2e-6 elsewhere), and every render of the rows whose map did not change is byte-identical between the base and the branch (824 of 824 files; the 7
other Sentry rows again on the final binary, 165 of 165). CPU (user time, one instance, 35 s stereo programme, best of five, g++ −O2):
level gate 0.43 s before and after, Transient 0.34 s; Hush 0.29 s before and after, with Air and Mix 0.29 s; Glow
0.38 s before and after, with the memory 0.46 s; Prism with three bands 1.43 s where Crest took 0.35 s (the G3 row now
runs a multiband: about 4 % of one core) No latency changes (Transient and Air / Mix add none; Mix uses the voicing's own latency).

Not done, and why: the Compressor / Crest feedback detector and grey-box refit (plan §3.17 items 1–4) — by gain per row
it came last (a few tenths per row against +0.3 … +3.1 for the items above), and the remaining FET-family gaps are level
errors at settings the maps cannot express (FG-116 Modern X2: side-chain HP on, LYNX +4 … 6 dB loud), so it needs its
own probe lab and five map refits: a round of its own. Prism has no upward branch per band (MO-TT's upward half). The
drum gate's Debleed is not modelled (its release sweep changes nothing on the probes; a bass line at D still opens LYNX on
note onsets the reference ignores). No state translators were added for these references (their saved states were never
captured); the missing-plugin table uses the measured defaults instead.

## Round 190fx: dynamics parity — Glow's side-chain weighting, the FET maps refitted on the pink stairs

Round 190fx of the 0.1.28 parity work: the dynamics rows with the lowest Sound numbers, highest gain per hour first. Black-box
only: the references' recorded probe captures (trial190k: `stairs` — 1 kHz up and down, then 100 Hz and 8 kHz — `tc`,
`pinkst`), never their code; the programme banks (tp172c, refcap182) were only scored, never fitted. No third-party code.
Tooling, logs and every render score: `/mnt/e/lynx-work/paritydyn190fx/` (`tools/probeerr.py`, `lab/`, `fit/`, `bank.py`,
`runs/`, `SCOREBOARD-190fx.md`).

**What the probes showed.** The dynamics maps were fitted on the 1 kHz stairs and tone bursts only. Read on the pink stairs and
on the 100 Hz / 8 kHz parts of the stairs, two kinds of error were left:

- the optical references (FG-2A, SD-3A and Custom Opto types) hear treble much less and bass a little more than Glow's Opto
  classic voicing, whose side chain carries the treble emphasis measured on another leveler: at 8 kHz LYNX reduced 4 … 8 dB
  more, at 100 Hz 1 … 3 dB less, and pink noise came out 2 … 4 dB too low (the programme rows' S2 / X1 files the same way);
- the FET references (FG-116 family, all-buttons type) compress much harder above their knee than their maps had them (on the
  1 kHz stairs about 14 : 1 where the maps ran 3 … 4 : 1 with a lower threshold): pink noise and the top steps came out 2 …
  3.5 dB too loud. A side-chain filter for the FET law was tried in the lab (a low-pass and a low shelf, `lab/fetfit.py`): it
  trades the 8 kHz steps against the pink steps and made the pink stairs worse, so it was dropped: the gap was in the maps.

**Glow Leveler — SC weighting: Old | Measured** (appended parameter 14, Opto classic voicing; default Old = every project and
new instance as before, sample for sample). Measured: two peaking filters in place of the emphasis — 4.56 kHz, Q 0.38,
+1.5 dB and 225 Hz, Q 0.59, +2.5 dB — with the chain's 1 kHz gain kept where the emphasis had it (a level shift computed from
both filters' responses), so every 1 kHz law fitted before (rounds 123 … 190dm) holds. Fitted by a grid and a coordinate
search on the three references' stair and pink-stair step levels at all their probe settings (`lab/scwfit.py`,
`lab/refine.py`): rms step-level error 2.01 → 1.23 dB (FG-2A type 2.15 → 1.16, SD-3A type 1.84 → 1.25, Custom Opto type
1.89 → 1.16). The measured Limit law and Glow's own engine ignore the switch. The editor shows `Side chain` `Old | Measured`
under the cell-memory keys (existing strings, 14 languages; the four key groups are 12 px high now so the column ends above
the COMPRESS caption). CPU: one biquad per sample and channel more than the emphasis (35 s stereo programme, user time, best of
five, `logs/cpu2.txt`): Glow 0.356 → 0.352 s with the switch off, 0.356 s (base, emphasis) → 0.360 s with it on, with the cell
memory 0.408 → 0.398 s — inside the timing noise; the Compressor 0.127 → 0.128 s (same code); no latency.

**Setting maps refitted with the pink stairs in the loss** (round 190ar's fitter, `fit/fitfx.py`, specs `fit/specs190fx*.py`):
- FET references → Compressor (FET law, no code change): stairs, tone bursts and pink stairs; pass 1 restarts the ratio at
  14 : 1 with attack / release / mix kept, pass 2 frees every link; two cycles each, the same procedure for all four. On the
  probe loss pass 2 beats round 190ar's maps for three of them (FG-116 −9.30 → −10.05, Modern −7.97 → −9.57, all-buttons
  −5.03 → −6.10); for the Vintage revision it does not (−7.86 → −6.54) although the programme bank improves — a sign the
  probes under-weight what that revision's programme rows depend on (noted, shipped by the same rule as the others);
- optical references → Glow with SC weighting on: Peak Reduction and Gain searched again (the switches keep round 190dm's
  choices: Opto law, release, cell memory). Custom Opto's search moved nothing (its map is round 190dm's plus the switch).

The missing-plugin translators of the four FET references (`core/FittedMaps.inc`, regenerated with round 190ar's
`tr/gentr.py`; test data with `tr/gentest.py`) load such projects at the refitted settings: rendered from the translator's own
output they score exactly the map rows below (5.80 / 4.76 / 5.56 / 4.48). The optical references have no translator (their
stand-in stays at Glow's defaults).

Scores (same banks and score as SCOREBOARD-v3 / round 190dm; before = origin/main fdfe73139 with round 190dm's maps, which
reproduces round 190dm's after-numbers exactly; per-file up / same / down):

| reference | LYNX plugin | bank | files | before → after | null | level error | up / same / down | went down | change |
|---|---|---|---|---|---|---|---|---|---|
| VM FG-116 | Compressor | prog182 | 25 | 4.80 → **5.80** | −15.8 → −21.1 dB | 1.5 → 0.6 dB | 15 / 10 / 0 | – | map refit (pink stairs) + translator |
| VM FG-116 Modern | Compressor | prog182 | 25 | 4.08 → **4.76** | −12.5 → −15.7 dB | 1.9 → 1.3 dB | 13 / 10 / 2 | D_bass 8→5, X2_drums 4→2 | map refit + translator |
| VM FG-116 Vintage | Compressor | prog182 | 25 | 5.00 → **5.56** | −16.4 → −19.4 dB | 1.3 → 0.7 dB | 11 / 13 / 1 | X3_guitar 8→7 | map refit + translator |
| VM The Monster | Compressor | prog182 | 25 | 4.28 → **4.48** | −13.2 → −13.7 dB | 1.2 → 1.0 dB | 4 / 20 / 1 | X3_drums 4→3 | map refit + translator |
| VM FG-2A | Glow Leveler | tp172c | 12 | 3.75 → **4.08** | −10.9 → −11.4 dB | 1.8 → 1.3 dB | 4 / 8 / 0 | – | SC weighting + refit |
| VM FG-2A (programme) | Glow Leveler | prog182 | 25 | 4.80 → **4.84** | −15.8 → −16.5 dB | 1.0 → 1.0 dB | 3 / 20 / 2 | X2_bass 7→5, X2_mix 8→6 | |
| VM SD-3A | Glow Leveler | prog182 | 25 | 4.64 → **4.84** | −14.8 → −16.8 dB | 1.2 → 1.1 dB | 11 / 9 / 5 | X1_bass 5→2, X1_guitar 5→2, X1_mix 3→2, X1_vocal 3→2, X2_bass 6→5 | SC weighting + refit |
| VM Custom Opto | Glow Leveler | prog182 | 25 | 4.96 → **5.16** | −17.3 → −18.5 dB | 1.0 → 0.8 dB | 4 / 21 / 0 | – | SC weighting |

Per plugin (the SCOREBOARD-v3 rows of `bank.py`, same rows before and after): Compressor 4.88 → **5.12** (10 rows),
Glow Leveler 4.64 → **4.82** (4 rows; the FG-2A programme column is an extra row, as in round 190dm). The other rows of both
plugins score exactly as before: the Compressor's code did not change, and with SC weighting Old every render of the Glow
rows is byte-identical to the base binary's (87 of 87 files on the programme and tp172c banks, the CLA-2A row's 16 of 16 on
tp172d, `ident.py`); `tests/Dyn190dmTests.inc` keeps its goldens (the defaults). Gate as in round 190dm: no row went down;
the files that did are listed (SD-3A's X1 setting was already 2 … 3 dB over-compressed by its map and the extra bass weight
deepens it; a refit that fixes X1 alone was not searched).

Sound column (0–10, table above): the pairs it lists (CLA-2A, Renaissance Compressor) did not change and keep their numbers;
the rows here are the stand-in pairs of the SCOREBOARD, whose fxparity means above are their scores.

Not done, and why: the Harrison MPC-type compressor on Crest (3.88: its Depth control caps the reduction in a way Crest's
range does not — a law of its own), the vari-mu row (4.32: below the threshold the reference's 100 Hz is 2 dB lower than its
1 kHz, a fixed tone Crest has no control for), X-Limit on Apex (4.94: LYNX limits pink noise 2 … 3 dB harder while the
programme comes out louder — a time-constant question, not a map). Hush, Sentry and the non-dynamics rows were not touched.

## Round 190dl: Echoform — H-Delay's clocked modulation, the measured tape stage (Law: Classic | Measured)

Roadmap item 3 of the 0.1.27 parity plan (Echoform, 10 reference rows). Black-box only: every number below comes from
the reference renders already in the tp172d / tp172c / refcap182 banks; nothing was captured, no third-party binary was
opened, no open-source code was ported (the open BBD / tape models were reading material; the fitted structure is
LYNX's own).

**S2-class diagnosis (H-Delay, wave 2–3 on S2).** The failing setting is the only one with modulation (Depth 50 %,
stored rate 0.1, LoFi on). Its impulse and noise-burst echo trains show the echo time swinging as a **triangle of
± 87.5 ms (23 % of the 375 ms time) with a 5.0 s period, both sides in step** (fitted to 3 ms rms over 12 echoes);
LYNX translated it to its ms law (± 5 ms, a quarter period between the sides). The LFO keeps running between the
capture's files (its phase follows the processed-sample count, not a reset), so the waveform cannot null; the
statistics can. New appended parameter **Mod clock** (off by default = the ms law): the swing is ½ · Depth of the echo
time, the sides in step; the H-Delay translator turns it on with the triangle, Depth × 0.932 and rate × 2 (one depth
and one rate measured: proportional-to-time and linear-in-percent are assumptions, the clocked-line reading of the
vendor's PCM42 lineage).

**Tape stage (ValhallaDelay's Tape mode, the setting with Wow / Flutter 0 is deterministic).** Echo-train system
identification: the first echo is a record tone, each further pass adds a feedback-only tone that loses the lows
(−9 dB at 63 Hz against 1.3 kHz per pass, where Echoform's write-path shelf lost 4 dB), both minimum phase within
0.02 rad, plus **0.102 ms** more delay per pass. Programme came out 2 … 4 dB under the impulse gain (round 190ca's
−1.67 dB Output trim covered it on average): a level law with memory. Built as Echoform's **Measured** law for the
Tape character: record level law (mean-square, fast attack, 44 ms release, ratio 1.06 above −47.5 dB), a harder head
curve, the record tone, the feedback tone, the extra delay, and the measured **hiss** (−91.5 dBFS at Mix 100 %, pink
through 4.5 kHz, uncorrelated sides; it plays while the tape holds anything and fades when the insert is idle — an idle
Echoform still outputs exact zeros). Constants: cookbook sections fitted on the impulse first, then all of it by
Nelder-Mead on that setting's four renders (ESR + level + third-octave spectrum; mean ESR −3.1 → −11.2 dB); the other
three settings (default wow, other feedback) only validate it. The translator's Tape trim is back at 0 dB.

**Old projects.** Appended parameter **Law** (Classic | Measured): new instances Measured, a project saved before loads
Classic; the Classic path is the unchanged code (tape `[[fallthrough]]` into the old case). 10 / 10 classic renders
hash-identical to the base commit (test, guarded to glibc ≥ 2.43 like round 190bz, with energy / sample tolerances
everywhere) and 131 / 131 bank renders of the untouched rows byte-identical. The Complete Access delay role (Stellar
Echo, X-Echo, X-Delay, 3D Delay) keeps Classic (`Law=0` in its preset): the measured stage lowered the multi-head tape
row's tail score 2.72 → 2.28 because it is another device.

| Row (bank) | files | wave before → after | tail before → after |
|---|---|---|---|
| H-Delay (tp172d) | 12 | 5.92 → **6.17** | 5.54 → **6.15** (S2 3.08 → 4.90) |
| EchoBoy (tp172d) | 20 | 5.15 → 5.15 | 6.34 → 6.34 |
| EchoBoy Jr (prog182) | 25 | 4.56 → 4.56 | 8.08 → 8.08 (programme only) |
| ValhallaDelay (tp172d) | 16 | 4.25 → **4.56** | 5.90 → **6.22** (D 6.32 → 6.62, S1 6.71 → 7.08, S3 6.58 → 7.18) |
| PrimalTap (prog182) | 25 | 3.64 → 3.64 | 7.40 → 7.40 |
| Stellar Echo (tp172c) | 6 | 3.33 → 3.33 | 2.72 → 2.72 |
| Little PrimalTap (prog182) | 25 | 2.84 → 2.84 | 6.23 → 6.23 |
| X-Echo (tp172c) | 9 | 2.78 → 2.78 | 4.55 → 4.55 |
| 3D Delay (tp172c) | 6 | 1.67 → 1.67 | 2.47 → 2.47 |
| X-Delay (tp172c) | 3 | 1.67 → 1.67 | 1.90 → 1.90 |
| **Family mean** | | **3.58 → 3.64** (10 rows) | **4.20 → 4.34** (7 impulse-bank rows) |

Baseline = this branch's base (rollup027 77026c3a2; ValhallaDelay already 4.25 with round 190ca's trim). No file went
down on the wave score and no setting on the tail score; the 16 renders that changed all moved up on the tail score
(ValhallaDelay S3 music ESR −7.5 → −16.1 dB, S1 drums / music 4 → 6; H-Delay S2 spectrum 3.7 / 5.6 / 3.2 / 10.0 →
0.7 / 3.0 / 1.0 / 5.0 dB). CPU per instance (48 kHz, 20 s of bursts, blocks of 512, ten interleaved runs against the
base build's binary): Tape Classic min 1.32 / median 1.44 % of one core, Tape Measured min 1.34 / median 1.52 %
(+ 2 … 6 %). That needed: both sides recorded at once (2-double sections), one level follower for the pair whose gain is
worked out again on a 0.5 dB rise or every 16 samples (polynomial log2 / exp2 from the bits), coefficients only when the
time / Era / rate change, the hiss read from a 2.7 s table made in prepare () (+ 1 MB per instance), and two weak fitted
sections left out (the bank scores the same or higher without them). Clean with Mod clock = the ms law within noise.
Latency: 0 samples reported, unchanged. Not done (next): Hi-Fi tape / the other ValhallaDelay modes (S2 still
4.0, its wow is random per render), wow / flutter depth law (EchoBoy's mod part), BBD clock stage (no bank row runs a
BBD mode through the maps), the structure rows (3 taps, multi-head + spring). Lab: `/mnt/e/lynx-work/echo190dl`.

## Round 190dp: Voice Shift's unvoiced path, Quell's noise-floor tracker (Ember: two-stage E tried, not shipped)
Black-box on the trials' renders (tp172d, trial187b, refcap182's 35 s programme bank, refcap0124's stimulus captures);
nothing of the reference binaries was opened. Gate: every file of the same banks before (rollup027 77026c3a2) and
after, through the same translator / fitted settings. New behaviour sits behind an appended `Law` parameter
(Measured for new instances, Classic for projects saved before); Classic renders hash-identical to the base commit
(Voice Shift and Quell, 9 cases). LYNX Tune is not changed: it shares the TD-PSOLA shifter, whose new unvoiced path
only Voice Shift asks for (`Psola::setUnvoicedShift`, off by default), so Tune has no law switch; new Tune instances
(default, +5 st with Formant +2 and a slow retune, low latency) and a Tune loaded from an older project render
hash-identical to the base commit's own Psola users (4 of 4 cases in the test).

**Voice Shift.** The reference's sine sweep comes out at exactly the Formant shift wherever its detector finds no
pitch (above ~1.6 kHz: −2.9 … −3.2 st at Formant −3, +1.7 … +2.0 st at Formant +2, unshifted at Pitch +5 alone);
drums and a full mix move the same way. LYNX passed unvoiced grains through. Now they are read at the formant factor
(Link: pitch × formant), in longer flat-topped grains (9.5 ms hop, 60 % crossfade): the first try (3 ms Hann grains)
combed noise near 1.4 kHz and folded a voice's 250 … 500 Hz energy into the lowest bands.

| Bank | Files | Score before → after | Spectrum error | Up / same / down |
|---|---|---|---|---|
| tp172d (Pitch +5, Formant −3, −7 / +2 / Drive 3) | 12 | 3.00 → 3.17 | 3.29 → 3.16 dB | 2 / 10 / 0 |
| trial187b (13 settings) | 78 | 3.12 → 3.27 | 3.31 → 3.01 dB | 12 / 66 / 0 |
| refcap182 programme (35 s, 5 signals) | 20 | 3.60 → 3.80 | 2.37 → 2.21 dB | 5 / 14 / 1 |

The one file down: refcap182 Formant −3 vocal 3 → 2 (spectrum 3.56 → 4.34 dB over the 4 dB step; its 63 … 130 Hz
bands +4 … +6 dB, the grain joins' sidebands). Every constant tried for the grains left one such file on a step edge
(`vsx/`: Hann grains put tp172d's −7 / +2 music down instead). CPU: 2.15 … 2.74 → 2.20 … 2.56 s per 35 s stereo take
(inside the noise); latency unchanged (1376 samples at 48 kHz).

**Quell (matched engine).** At a high setting the reference takes a steady programme down more and more over the
first seconds (a dense mix 10 … 24 dB at 100 %, nothing at 35 … 50 %): a noise floor estimated from the signal. Now
each band's floor is the minimum of its smoothed power over 1.1 s (minimum statistics, R. Martin, IEEE TSAP 9(5),
2001; eight sub-windows), seen through its own Q 8.5 band-pass on the channels' mean (a tone sweeping by is not its
own floor), and sets a second soft-knee expander `off(amount)` dB above it, evaluated every 16 samples; the larger of
its reduction and the fixed law's is applied (the gains still move every sample).
Constants fitted (`qfit/`) on refcap182's programme takes at 0 / 20 / 35 / 50 / 100 % and refcap0124's stimulus at
0 … 100 %: band-envelope distance 4.15 → 2.95 dB. Latency stays 0.

| Bank | Files | Score before → after | Level error | Spectrum error | Up / same / down |
|---|---|---|---|---|---|
| tp172d (0 / 50 / 100 %) | 9 | 4.00 → 4.11 | 0.87 → 0.60 dB | 1.67 → 1.80 dB | 1 / 8 / 0 |
| refcap182 programme (0 / 20 / 35 / 50 / 100 %, 35 s) | 25 | 4.68 → 4.84 | 0.85 → 0.33 dB | 1.34 → 1.08 dB | 2 / 23 / 0 |

At 100 % the level error falls where the reference's floor tracker acts (tp172d music +3.6 → +0.6 dB, refcap182
guitar +7.7 → +0.5, mix +6.5 → +3.2, bass +2.8 → −0.0 dB); at 0 … 50 % nothing moves (the offset's law,
−30 + 46 · amount² dB, keeps the floor's threshold under programme there); tp172d's sweep is now 1.6 dB low instead of
1.0 dB loud (same score). A first version tracked every sample on both channels (refcap182 4.88, tp172d 4.00) at
+35 % CPU; this one: the matched engine on a 35 s stereo take 1.65 … 1.70 → 1.73 … 1.81 s at 100 % (+5 %),
1.72 … 1.78 → 1.77 … 1.80 s at 50 % (+3 %); the default (Matched off) and old projects cost what they did.

**Ember.** Style E driven hard as two cascaded curve stages with a coupling high-pass (the plan's §3.18 item 1) was
built in the lab and fitted from two starts on the 22 E takes (`ember/emfit5.cpp`): −4.00 dB (shipped one-stage table)
→ −3.98 dB at best, the other start diverged. No gain, so nothing shipped; the engine is unchanged (every Ember row
of the gate identical: tp172b Saturn 2 5.10, tp172d / trial187b Decapitator 5.00 / 3.91, tp172c and refcap182
X-Saturator, Revival, Fusion rows, Harrison Tape 2.67 / 2.76, Heatwave 3.08 / 3.04). Found for the tape rows (no
DSP yet): on the 1 kHz stairs the Harrison tape reference is flat at default (−0.0 … −0.3 dB up to −3 dBFS) and
at Drive 15 / fast tape a hard ceiling (−0.6 / −5.3 / −10.5 dB at −15 / −9 / −3 dBFS), while the fitted Ember map
loses 2.7 dB flat at default and stays almost linear at Drive 15: Ember's own Tape curve cannot be fitted to that
law. A tape style with a level-dependent ceiling (hysteresis core) is the next step there.
Tools and tables: `/mnt/e/lynx-work/vqe190dp` (`bank.sh`, `gate.py`, `vsx/`, `qfit/`, `qv/`, `ember/`, `golden/`).

## Round 190ff: Live's Freeze Track with the insert effects, Bounce Track in Place (Flatten), Bounce to New Track, Consolidate (Clip-launch workflow)

Evidence: Live 12.4.16's own command list (`ableton/live-exe-strings.txt`: `Edit>Freeze Track(s)`, `Edit>Bounce
Track(s) in Place`, `Edit>Bounce to New Track(s)`, `Edit>Paste Bounced Audio`, menu keys `sMenuFreeze` / `sMenuUnfreeze`
/ `sMenuBounceTrackInPlace` / `sMenuBounceToNewTrack`; no `sMenuFlatten` — Flatten survives only as the scripting API's
`NTrackApi::Flatten`), the arrangement clip menu of 12.4.16 [SHOT live190ei/shots/live-audioclip-context-menu.png:
Freeze Track Ctrl+Shift+Alt+F, Bounce Track in Place, Bounce to New Track Ctrl+B, Paste Bounced Audio Ctrl+Alt+V],
its errors (`sErrorRouteInFreeze`, `sErrorMonitoringStateFreeze`, `sErrorGroupExternalFreeze`) and the folders
`Samples/Processed/Freeze|Bounce|Consolidate`.

| Live | LYNX (Clip-launch workflow) | Status |
|---|---|---|
| Freeze Track: the track with all its devices; the mixer stays live | Column title menu / Playlist clip menu › Freeze Track (Ctrl+Shift+Alt+F in the grid): the channel with its volume and pan through its insert's effects, before the insert's EQ, fader, pan and sends; the instrument and the effects are not loaded | ✅ null −139 dB (core), real export: `--trackfreezetest` |
| Unfreeze Track | the same item | ✅ project byte for byte as before |
| A routing into the track / an input refuses the freeze | refused with the reason: another channel or insert into the insert, a side-chain or send out of it, an audio input, a live controller link | ✅ (a freeze that stops holding plays live) |
| Frozen devices locked | Mixer slots and knobs refuse with a notice; the Device view dims them with the freeze tag | ✅ |
| Flatten (12.2+: Bounce Track in Place on a frozen track) | Bounce Track in Place on a frozen track: the channel becomes an audio clip channel of its stem, one clip per clip at its place and length plus tail clips, devices removed | ✅ bit-identical to the frozen playback |
| Freeze and Flatten / Bounce Track in Place | Bounce Track in Place on a live track: rendered and flattened, one undo step | ✅ |
| Bounce to New Track | the selected clips of one track through its devices, onto a new track below (the clips stay) | ✅ −135 dB vs the track's devices |
| Consolidate (Ctrl+J) | pattern clips: one pattern, no render; audio clips of one channel: rendered into one clip | ✅ bit-identical / −134 dB |
| Session clips of a frozen track, group tracks, Paste Bounced Audio, Consolidate Time to New Scene, files in the project's Samples/Processed | — | open (see the round's HANDOFF) |

Also: `Song::automationChunks` (runtime only): stand-in / freeze render songs and projections keep the chunking of the
song they stand for (the renderer works in 128-sample chunks while any automation plays; some instruments sound about
−66 dB different at 512): frozen audio now nulls where the freeze took the song's automation away.

## Round 190gx: Anvil Bus refitted on Live's second Drum Buss render set

Round 190gu rendered Live 12.4's Drum Buss again: the settings 190fk could not measure (types at 25 / 50 %, Soft 75 %,
Boom amount / decay / frequency, Crunch 0.25 / 1.0, Transients on a sustained pad, Comp with Drive and with Trim, Dry/Wet
with Comp, every type at +6 dBFS, Damping 500 Hz / 2 kHz), 46 cases, each at 44.1, 48 and 96 kHz. Anvil Bus was
refitted on them with the plugin quality gate: the 31 cases of 190ei and the 46 new ones, every case at or under its
error before (to 0.001 dB), at all three rates. Anvil Bus is in no release yet (only the 0.1.28 rollup), so the new laws
need no engine-version switch.

| Item | Live (190gu) | Anvil Bus now | |
|---|---|---|---|
| Comp's place | before the curve (Comp + Drive 50 %: staircase THD and level follow) | moved before the curve, refitted: 8 Comp cases all closer (Drive 50 % stair 1.63 → 0.17 dB, drums 2.65 → 0.65) | ✅ |
| Dry/Wet with Comp | dry = the raw input (0.003 dB) | the same (test: sample for sample) | ✅ |
| Type EQ below 100 % | Soft / Medium keep the neutral shape until near 100 % | depth = amount⁸ for Soft and Medium (Medium 50 % noise 2.95 → 0.41 dB) | ✅ |
| Soft 75 % | folds 0 dBFS to −12.1 dBFS | its own knot (stair 1.07 → 0.33 dB) | ✅ |
| Boom amount | lift and ring grow fast (1.0 rings ≈ −20 dB/s at 50 Hz) | lift and Q per amount (0.25: drums 4.98 → 0.43; 1.0: 7.82 → 3.32 dB) | ⚠️ |
| Boom decay 0 / 0.5 | | refitted knots (drums 3.22 → 3.08, 1.87 → 1.75) | ⚠️ |
| Damping | one-pole | corner 6 % above the set value (0.38 → 0.24 dB) | ✅ |
| Transients on steady sound | a pad: −1.0 → −3.9 dB, +0.25 → +1.15 | a broadband level term joins the shaper (pad 3.53 → 3.05, 0.98 → 0.80 dB); the full depth would move the drum loop | ⚠️ |
| Crunch's grit | THD 1 → 2.3 / 4.4 / 12 % at 0.25 / 0.5 / 1.0 | not carried (tried: lift before the curve; failed the gate) | ❌ |
| Output never above 0 dBFS | yes (+6 dBFS in → ≤ 0.0) | not limited (tried an output ceiling; failed the gate) | ❌ |
| Rates 44.1 / 48 / 96 kHz | agree within 0.25 dB | errors against Live the same at the three rates (mean 0.77 / 0.77 / 0.79 dB) | ✅ |
| Panel | Drive, types, Trim, Comp \| Crunch, Damp, Transients \| Boom, Freq, Decay \| Out, Dry/Wet | the generic panel in those four columns | ✅ |

Mean headline error: 190ei bank 1.216 → 1.198 dB, 190gu bank 1.261 → 0.766 dB (48 kHz); details and CPU in
`/mnt/e/lynx-work/livefacts190gx/db/REPORT.md`.

## Round 190fk: Anvil Bus — LYNX's drum-bus engine in the role of Ableton Live's Drum Buss

Owner decision O20: Live's Drum Buss colours even at its all-zero setting, so it imports onto a dedicated engine
instead of the round-190ek rack (Ember → Facet → Crest → Shell; projects that hold the rack keep it). Anvil Bus is a
built-in effect (mixer menu, eight factory presets, generic editor in its own forged-gunmetal theme, 14 languages).
Laws from LYNX's own analysis of Live 12.4.16's renders (round 190ei); nothing of Live's is used.

| Item | Live's Drum Buss | Anvil Bus | |
|---|---|---|---|
| Controls | Drive, type (3), Crunch, Damp, Transients, Boom amount / freq / decay, Comp, Trim, Output, Dry/Wet | the same twelve, Live's ranges (Output to +12 dB) | ✅ |
| Boom audition / Force to note | yes | not built (the import lists audition) | ❌ |
| Colour at zero | EQ "smile" + soft saturation | the same (fitted, noise TF rms 0.22 dB) | ✅ |
| Drive curves | Soft folds, Medium / Hard clip with their EQ | fitted per type and amount (staircase level rms 0.04 … 0.46 dB; Soft's fold null −40 vs −130 dBFS) | ⚠️ |
| Comp | fixed ≈ 4 : 1, +11 dB | the same (staircase 0.09 dB, drums 0.73 dB) | ✅ |
| Transients | attack + sustain above the lows | Strike's detector above 330 Hz (drums 1.1 … 2.4 dB) | ⚠️ |
| Latency | 188 samples | 55 samples (rack: 254) | ✅ |

Before → after on Live's 31 Drum Buss renders (headline error, rack → engine): mean 2.13 → 1.19 dB, no case worse;
per case in `/mnt/e/lynx-work/drumbus190fk/HANDOFF.md`.

## Round 190fj: Ember's Tape on a measured magnetic core (Law: Classic | Measured), tape speed

**Finding (black-box, the tape reference's recorded renders).** At small level the reference is a pure 147-sample
delay (impulse: one tap of 0.962, response flat to 0.2 dB from 20 Hz to 20 kHz), odd harmonics only (H2 below −150 dB,
H3 rising 12 dB per 6 dB), its drive compensated (the quiet stair steps read 0.00 dB at every drive). On the 1 kHz
staircase the fundamental lags the input as the level rises: 0° up to −24 dBFS, −1.9° / −8.7° at −12 / 0 dBFS with
Drive +10 dB, −26.6° at Drive +15 dB and 1⅞ ips. A static curve fitted per step leaves −16 dB (Drive +10, 0 dBFS) and
−9 dB (Drive +15) of residual, while any memoryless curve — with any EQ around it — keeps the fundamental's phase
exactly. At Drive +15 / 1⅞ ips the three loudest steps come out identical (−127 dB apart): the field is clamped ahead
of the core. Its tails are exactly zero (no remanent offset). Ember's Classic tape curve (round 190dp: "cannot follow
that level law") lost 2.7 dB flat at the default and stayed almost linear at Drive +15.

**Engine (`core/EmberTape.h`, LYNX's own).** Record EQ (a low and a high first-order shelf) → record level → field
clamp (`clip · tanh`) → Jiles–Atherton loop (D. C. Jiles, D. L. Atherton, J. Magn. Magn. Mater. 61, 1986; for tape
J. Chowdhury, DAFx-19 — literature only, no code of any tape model) solved per oversampled sample with midpoint
Runge–Kutta on the field step, the Langevin function from a 4096-point table, and a 5 ms creep back to the
anhysteretic curve (without it the loop keeps a remanent offset after loud passages: −12 dB of the peak at Drive +15;
a DC blocker instead cost the default's sweep 19 dB of ESR) → small-signal make-up (Auto gain on: 0 dB at quiet
levels, as the reference) → playback EQ (the record EQ's exact inverse) → playback loss (one-pole low-pass per speed)
→ trim. Ember's half-band oversampler around the core as for every style; a switch onto the measured Tape on a running
signal starts its filters and loop at the signal's state (no click; FxAudit clean). Auto gain off: the small-signal
gain is Ember's drive gain, as for every other style.
Constants fitted with `fit/tapefit` (Nelder–Mead on mean ESR + level error, the model compiled from the same header):
the loop, record EQ and drive law at 15 ips on the default and Drive +10 takes (tp172c stairs, sweeps, drums, music);
1⅞ ips on Drive +15 (tp172c + refcap182 programme); 30 ips on refcap182's X1; 7½ ips on X2 (a take with "Average"
tape and flutter on: its level law only); 3¾ ips interpolated. Ember's drive 18 dB = the reference's 0 dB (record
level = gain0 + 1.384 × (drive − 18) dB). The fit keeps the loop narrow: the Langevin saturation, the level law, the
clamp and the losses carry the match. The reference's lag (which grows as it saturates, where a JA loop's shrinks) is
not reproduced yet.

**Parameters.** `Law` (appended; Measured for new instances, Classic for projects saved before:
`upgradeLoadedEffectParams`) and `Tape speed` (30, 15, 7.5, 3.75, 1.875 ips; 15 by default). The measured core runs
only where the Classic tape curve ran: single band, Style Tape, no voice / band character / Classic voicing / band
feedback; every other path is untouched. Panel: the speed tabs under the curve while Tape runs measured; the Law tabs
in the same place while a project's Classic tape is set (both hidden on the Classic voicing's page).

**Gate** (base b0cb90aec objects vs branch, same banks, same harness: `/mnt/e/lynx-work/ember190fj`, `gate.py`):

| Reference row | Bank | Files | Before → after | Up / same / down |
|---|---|---|---|---|
| Harrison Tape Saturator → Ember (measured Tape) | tp172c | 12 | 2.67 → **5.75** | 12 / 0 / 0 |
| Harrison Tape Saturator → Ember (measured Tape) | refcap182 programme | 25 | 2.76 → **5.56** | 23 / 2 / 0 |
| Saturn 2 | tp172b / refcap182 | 20 / 25 | 5.10 / 6.16 → same | 0 / 45 / 0 |
| Decapitator | tp172d / trial187b / refcap182 | 24 / 100 / 30 | 5.00 / 3.91 / 4.70 → same | 0 / 154 / 0 |
| X-Saturator v6, VM Revival, Fusion Vintage Drive, Fusion Transformer | tp172c / refcap182 | 40 / 100 | 5.00, 5.75, 4.58, 5.12 / 5.60, 6.08, 5.04, 6.24 → same | 0 / 140 / 0 |
| Heatwave (its map keeps the Classic tape: Law Classic) | tp172c / refcap182 | 12 / 25 | 3.08 / 3.04 → same | 0 / 37 / 0 |

Total 35 up / 376 same / 0 down; the 376 unchanged renders are byte-identical to the base's. Per file (tp172c):
default ESR −5.8 … −14.9 → −19.5 … −41.3 dB (level error up to 5.8 → 0.2 dB); Drive +10 −10.3 … −10.7 → −12.7 …
−20.0 dB; Drive +15 / 1⅞ ips +2.7 … −2.1 → −12.8 … −20.9 dB (level +3.9 … +6.7 → −0.6 … 0.0 dB). refcap182 X1
(30 ips) 2 … 5 → 7 … 8; X2 (flutter on, not modelled) 2 … 3 → 3 … 4.
Classic law / old projects / every non-tape path: bit for bit the base (8 hash goldens, `tests/Ember190fjTests.inc`). The
Classic voicing's golden is kept for both compilers (g++ 15 and zig c++ 0.13 round the base's own Classic voicing
differently; each build of the branch matches its build of the base). Imported Live Saturators (Bass Shaper, Hard
Curve map to the Tape style) keep the Classic tape: their fits (round 190da) were made on it.

**CPU** (one instance, 30 s stereo at 48 kHz, best of 5, -O2 no-LTO objects at machine load ~20): Tape at 4×
(Ember's default) Classic 1.8 % of a core → Measured 4.0 % (4.2 % at 1⅞ ips with the clamp); 2× 1.2 → 2.3 %; 8× 3.1 →
6.9 … 7.7 %. Paid only by the measured Tape (other styles and old projects unchanged). **Latency** unchanged (the
oversampler's 55 samples, reported to the host as before).

**Not shipped.** Transformer: the same core with a low-frequency record emphasis and an output shelf per Shine was
fitted on the transformer reference (tp172c D / S1, refcap182 D / S1 / X1 / X2): better on the default's stair /
drums / music (−24.5 → −36.6, −8.2 → −10.1, −17.1 → −18.5 dB ESR) but worse on Amount 200 %'s stair and sweep
(−38.4 → −20.1, −7.4 → −6.2 dB) — no clear win; its distortion falls ~50 dB from 400 Hz to 1 kHz (a split low band,
not a first-order emphasis) and Shine / LF Extend would need their own controls. Heatwave is no tape: its harmonics
are level-independent (Drive 50: H2 −49, H3 −35 dB at every level from −48 to −12 dBFS) and its default is flat in
magnitude but dispersive in phase (group delay ~370 samples at 60 Hz → ~96 at 15 kHz); its map keeps the Classic law.

## Round 190fy: Spiral Echo's measured loop, revision 1 (non-dynamics parity round, 0.1.28)

Scope: the lowest non-dynamics rows where the recorded reference renders allow a real gain (Ember, LYNX Tune and the
dynamics plugins belong to other rounds). Black-box only: the tp172d bank renders and refcap0124's one-parameter
captures of the reference; no plugin was run again, nothing of its binary was opened, no third-party code.

**What changed.** Spiral Echo's measured loop (round 190do, the default of new instances since 0.1.27) gets a
revision. Echo by echo (echo 2 over echo 1 of the bank's 0.5 impulse, S1 = 10 ms, S3 = 18 ms on the dial), revision 0
was late against the reference above 1 kHz and early under 200 Hz: its 2875 Hz (Q 0.34) all-pass delayed the mids more
than the reference's loop does. Revision 1 reads the line 1.8 samples sooner (at 48 kHz; at least one sample, a linear
read under two samples: the 0.01 ms floor answers two samples after the input, the reference one, revision 0 three) and replaces that all-pass with a broad one at 7080 Hz (Q 0.101): 3.7 samples more delay under 300 Hz
against the top, 2.4 fewer at 2 kHz. The constants were fitted on the error of the 16 bank renders (Nelder-Mead over
read offset, all-pass frequency and Q and a time scale; mean ESR −6.8 → −10.2 dB; the scale came back at 1.000) and
checked on the 36 refcap0124 renders, which the fit never saw.

**Old projects.** The revision is a hidden parameter (index 9, not in the info: no control, no string): a new instance
starts at 1 (`makeEffectSlot`), a project or preset saved by 0.1.27 (nine values, Law = Measured) loads at 0 and renders
bit for bit as before (4 of 4 fingerprints from the base commit's objects in `tests/Parity190fyTests.inc`; 16 of 16 bank
renders byte-identical), one saved before 0.1.27 keeps the classic loop (Law = 0). Law = Classic ignores the revision.

| Bank | Files | Before → after | Up / same / down | Mean ESR |
|---|---|---|---|---|
| tp172d ValhallaFreqEcho (the scoreboard row) | 16 | **3.88 → 4.50** | 7 / 9 / 0 | −6.8 → −10.2 dB |
| refcap0124 ValhallaFreqEcho (default + every one-parameter render) | 36 | 3.69 → 3.75 | 2 / 33 / 1 | |

Per setting (tp172d): D 3.00 → 3.00, S1 4.00 → 5.00, S2 4.00 → 5.50, S3 4.50 → 4.50. Tail score (`scripts/taillab.py`,
delay kind): 6.79 → 6.98, every setting up (D 6.26 → 6.67, S1 6.98 → 7.15, S2 6.49 → 6.53, S3 7.43 → 7.56). The one refcap file down is the −1000 Hz shift
(`p001_0.000`, 6 → 4): its spectrum error crosses the 0.6 dB step (0.56 → 0.62 dB) while its ESR improves (−10.5 → −11.2
dB) and its level stays (+0.1 dB); its mirror, +1000 Hz (`p001_1.000`), goes 4 → 6. Every candidate of the fit that
moved the bank moved that file across the same step; the coordinator decides whether a 0.06 dB spectrum step counts.

**CPU** (thread time, 20 s of noise bursts at 48 kHz, blocks of 512, best of 5; base, revision 0 and revision 1
binaries interleaved four times, machine load ~30): % of one core, default 0.564 / 0.596 / 0.590, S1 0.580 / 0.590 /
0.608, S3 0.564 / 0.581 / 0.587, +100 Hz shift 0.590 / 0.588 / 0.558 — the same within noise (two constants and one
branch, no new filter). Latency 0, memory unchanged.

**Looked at, nothing shipped (the gate or the evidence said no):**
- *Voice Shift ← Little AlterBoy* (3.17 / 3.27 / 3.80): the shifted files' spectrum error comes from the reference's
  own low-level output under 130 Hz — a DC offset that changes per note (−32 dB at 0 Hz in a held note) and a floor
  45 … 55 dB down — where LYNX is clean. LYNX also thumps at each voiced onset under 130 Hz (Formant −6: −26 … −29 dB
  with Law Measured, −35 … −37 dB with Classic, the reference −41 … −45 dB): round 190dp's shifted unvoiced grains
  meeting the first voiced grain (Voice Shift does not normalise the window sum as LYNX Tune does). Removing the thump
  is a quality fix the score cannot see (one file 3 → 2), copying the DC is not one; left open for the owner.
- *Halo ← ValhallaShimmer*: the top two bands are 3.5 / 8 dB under the reference on every wet file (the output high
  cut is first order at 8 kHz at the bank's setting); opening it moved one file down and none up (the tone corrector of
  round 190bz is fitted with that cut). The level error has opposite signs on the impulse and programme files of one
  setting, so a level law cannot fix it either.
- *Orbit ← ValhallaSpaceModulator* S1: the music file sits 1.0 dB low, all of it under 110 Hz; a stronger feedback
  (0.45 instead of 0.377) did not lift it (another mechanism), so nothing shipped.
- *Ridge Strip ← SSL 4K G, Pink EQ*: the translator plays Pink as Black. The 206 Pink impulse renders of tp172c's
  K4G_ir grid show a different EQ (larger gain range, other centre frequencies, a high shelf that tilts the lows down
  by up to 11 dB, LF Bell = LF Shelf): a new curve family to fit, a round of its own; data and tools listed in the
  handoff. Lattice Mod: every file is at step 4 with an ESR near 0 (random modulation) and envelope correlation
  0.49 … 0.97, under step 6's 0.98: no statistic within reach.

Tools: `/mnt/e/lynx-work/parityfx190fy` (`lab/spiral.py`, `lab/refcap_sp.py`, `lab/sp_search.py`, `lab/cands.sh`,
`lab/echoes.py`, `lab/loopid.py`, `lab/bands.py`, `lab/lf.py`, `gold/`, `cpu/`).

