# Round 170c — plugin quality (Busy Works Beats video, LYNX 0.1.22/0.1.23 on macOS)

Source: coord/busyworks-0.1.24.md (video https://youtu.be/1OvCBNmress, frames /mnt/e/lynx-work/yt-beat/kf/). Items #11
(Facet EQ blank on macOS + unstable analyser), #14 (Fruity WaveShaper "Oversampling: off" looks dead), #15 (Ember
Saturator's first mode "garbly"), and an audit of shell controls (coord/plugin-audit-0.1.24.md).
Data: /mnt/e/lynx-work/plugq170c/.

## #11 Facet EQ draws a blank pink window on macOS

| | |
|---|---|
| Reporter | 15:50 "the interface is kind of glitchy. Well, not kinda, it is glitchy. That's not gonna work." (frames kf/15m50.jpg, 15m53.jpg: the whole graph and header one flat pink, only the bottom bar, the EQ/Source/Target switch and the preset bar visible) |
| Cause | The colour of the flood, sampled from the frame: (214, 184, 194) below the header, near white in it = exactly the gradient of the **Glow style's plugin title** (white → 0xd6d9e0 tinted 18 % with the accent), extended past its ends. Everything painted before the title (graph, header bar) is covered, everything after it (slot name, preset bar, bottom bar) is visible. JUCE 8.0.4's CoreGraphics renderer draws text with a non-colour fill glyph by glyph as paths (`CoreGraphicsContext::drawGlyphs`, non-colour branch → `fillPath`); the **space** of "Facet EQ" has an empty outline, `drawCurrentPath` then calls `CGContextClip` on an empty path, which Quartz ignores ("empty path"), and `drawGradient` fills the whole clip = the whole component. Linux / Windows (software / Direct2D renderers) clip an empty path to nothing, so it only showed on macOS. Every Glow-style editor whose title has a space was affected (Facet EQ, Apex Limiter, Prism Multiband, Hush De-esser, Crest Compressor... see the CI pictures). |
| Fix | `skin::drawFilledText` draws gradient text as one path of its non-empty glyph outlines (PluginSkin.cpp; the title uses it). JUCE patch 12 (cmake/LynxPatchJuce.cmake): `drawCurrentPath` skips a non-colour fill of an empty path on macOS. |
| Check | `LYNX --gfxprobe report.txt` (pixels a gradient text / an empty gradient path changes outside its box; 0 = right) and the `--plugui` pictures, both run by CI on Linux and macOS (artifact editor-pictures-<os>). |

## #11b Facet EQ's analyser "unstable"

Before: one analyser point per 3 pixel columns, recomputed from the pixel geometry — every resize reset the spectrum to
the floor; each point jumped to each frame's FFT maximum (flicker); below one FFT bin per column the lows were a
staircase of repeated bins. Now: a fixed grid of 480 log-spaced points over the axis (10 Hz … 30 kHz), interpolation
between bins in the lows, the band maximum above, a ¼ ½ ¼ neighbour smoothing and a ~3-frame rise; the fall keeps the
Analyzer speed setting. dB mapping unchanged (a full-scale sine = 0 dBFS, Hann coherent gain).

## #15 Ember Saturator: the first mode sounds "garbly"

Reporter (19:02–19:24): "Quality-wise, it does not sound good ... maybe it was just that first mode ... This sounds
better ... that first mode, whatever that first mode was, it was not it. It just sounds garbly." Frames 18:54–19:17: band
1 = **Tube** with character **Off** (the Style menu's Tube curve), drive +10.4 dB; he then picked Glass valve and
Round valve (the measured characters), which he liked.

Measured (scratch tool em.cpp linking core/MixSuite.cpp; makeEmber, auto gain on, default 4x oversampling, 44.1 kHz):
`ember-before-10dB.txt` / `ember-after-10dB.txt` in /mnt/e/lynx-work/plugq170c.

| drive +10.4 dB | H2 (dB) | H3 | THD % (1 kHz, −12 dBFS) | IMD: 3 kHz ± k·50 Hz sidebands (50 Hz −8 dBFS + 3 kHz −20) | LF pump < 30 Hz (80 Hz bursts) |
|---|---|---|---|---|---|
| Tube before | −19.2 | −28.8 | 11.6 | −5.8 | −16.3 |
| Tube after | −30.8 | −37.8 | 3.2 | −15.1 | −18.5 |
| Round valve (liked) | −27.2 | −39.2 | 4.5 | −13.4 | −29.1 |

The old Tube curve (tanh biased by 0.35, knee at unit level) clipped a mix's peaks at +10 dB almost half-wave (positive
side limited at 0.75, negative at −1.5): heavy even distortion, the bass amplitude-modulating the highs (−6 dB
sidebands = the garble) and envelope-following DC. New Tube: the knee at 2 (6 dB more headroom) and a 0.15 bias — 2nd
harmonic still leading (tube character), the same family at high drives (at +30 dB THD 30 % vs 39 %, aliasing −60 dB vs
−37.5 dB). The Tape pre / Triode voicings and Voice Shift keep the old curve (`Style::tubeBiased`).

## #14 Fruity WaveShaper "Oversampling: off"

The control is real (FL's OverSelect 0…4 = ×1…×16, engine REA_rodada51 §2.6 — LYNX renders it exactly). It looked dead
because the stepper only changed when the two tiny arrows were hit. Now a click on the value steps it (wrapping), a drag
or the wheel still sets it, and its hint says so — for every stepper of the generated editors.

## Audit of shell controls (coord/plugin-audit-0.1.24.md)

Static audit (every param enum use vs EffectInfo vs editor layouts, displays, macOS drawing risks) of the effects and
the main instruments. Fixed in this round, besides the items above:
- **Level meter wells** (Soft Clipper, Multiband Compressor ×4, Transient Processor ×2, Balance, Emphasizer, FL
  Emphasis) drew an empty grid: they now show the slot's live input / output peaks. RackRenderer measures a slot's
  input and output peak only while an open editor counts itself in `ProcessTimes::levelWanted[slot]` (no cost
  otherwise); the editor takes them with exchange (0) each frame (held 1 s, falling 24 dB/s).
- **Limiter / compressor / dynamics curves** of the generated editors (Fruity Limiter...) were a still picture: a
  fading trail of the signal's input → output peaks now runs over the curve (FL's display is live).
- **Echoform**: Hybrid and the tape voicings run their own loops (EchoSuite.cpp early returns) that read only the
  times, feedback, mix, output, style and phase; 22 other controls looked live and did nothing. Layout items now carry
  `idle` conditions (PluginLayout.h): drawn dimmed, with the hint "no effect with the current mode".
- **Crest Compressor**: the Character's Drive (read by the DSP) had no control: knob added.
- **Hush De-esser**: the gradient-filled output wave path is skipped when empty (the macOS flood).
- **Atrium**: Early mod rate / depth (p15 / p16) were read by nothing; the early taps now move (four phased sines over
  the taps, 0.05 … 5 Hz, depth = the late lines' law, capped below the shortest tap; depth 0 = the old output).
Open (listed in the audit with evidence): Fruity Limiter's scrolling envelope,
Ember's hidden Classic switch and silently overridden controls, Toxic's Bend (no pitch bend reaches native
generators), 3x Osc's empty page without FL, Sytrus operator / filter pages, ProEditor whole-window repaints,
**Parametric EQ 2 on new instances (FL's law or LYNX's biquads) — needs the user's decision**.

## PARITY.md: Sound and Stability columns

Added for every inspired-by pair (user's request): Sound from the measured evidence in docs/analise (per-pair survey:
null / residual, curves, THD, GR), Stability from the editors' real behaviour (macOS pictures, analyser, dead controls).
Waves and Auto-Tune trials expired → "(ref expired)". Lowest Sound (no audio comparison on record): Horizon 2, Apex /
Pro-L 2 2, Halo 3, Orbit 3, Sentry 3, Nebula 3, ValhallaPlate 3 — added to /mnt/e/lynx-work/tracker/plugin-wishlist.md
with the trial deadlines (FabFilter ~11-16, Soundtoys ~11-22).

## Second part (feature/plugq170c2)

### #14 again: the WaveShaper's OVERSAMPLE selector (video 17:30–17:50, kf/ws-stack.jpg)
He clicked the "Off" value again and again on a Mac trackpad and it stayed Off. FL's control: OverSelect 0…4 = Off,
2x, 4x, 8x, 16x (REA_rodada51 §2.6; LYNX's engine renders each exactly). The shared stepper of the generated editors
now works like a value selector:
- the arrows step one value (hit area at least 10 px wide; the up / down halves split at the value box's centre — a
  caption above the box used to move the split down);
- a click on the value lists every value (menu, current one ticked); the right-click menu lists them too, above
  Reset / automation / copy / paste / type-in;
- a vertical drag moves one value per 10 px; a wheel notch one value; a trackpad's small smooth deltas add up to one
  value per 0.12 (before, each tiny trackpad event counted as a whole notch);
- hint: "arrows step, click for the list, drag or mouse wheel to change" (15 languages).
Oversampling measured (test `waveshaper_round170c_oversampling_lowers_aliasing`, 7 kHz sine into the default curve at
the top pre-amp, strongest alias at 9.1 / 4.9 kHz): Off −23.4 dB, 2x −113.5, 4x −111.5, 8x −117.3, 16x −115.3 dB.

Audit of every stepper: `LYNX --steppertest report.txt` drives each stepper of every generated effect editor (all
pages) with synthetic input — up / down arrow clicks, the value's menu (item count, choosing a value), a click on the
value (opens the menu, changes nothing), a drag up 3 values and back, a wheel notch, a trackpad scroll — one OK / FAIL
/ SKIP line each; CI runs it on Linux and macOS (artifact editor-pictures-<os>, steppertest.txt). Results: see
"Stepper audit" below.

### #15 again: Ember's default Tube against FabFilter Saturn 2 (bass / 808 bus)
His exact setting (video 18:54–19:24, kf/sat-stack.jpg, 19m00.jpg): band 1 style Tube, character Off (the default),
Drive ~+10.4 dB, Auto gain on, Mix 100 %, 4x oversampling. Saturn 2 (trial) measured black-box with fxmeasure on the
same signals (48 kHz: 1 kHz −12 dBFS, 50 Hz −8 dBFS + 3 kHz −20 dBFS two-tone, 7 kHz −12 dBFS, an 808 pattern);
data /mnt/e/lynx-work/plugq170c/sat (compare.txt, ana.py; Saturn renders saturn/, LYNX renders lynx/).

| | THD 1 kHz | H2 / H3 (dB) | IMD 3 kHz ± k·50 Hz | aliases (7 kHz) |
|---|---|---|---|---|
| Ember Tube +10.4 dB, before (170c part 1) | 11.6 % | −19.2 / −28.8 | −5.8 dB | — |
| Ember Tube +10.4 dB, now | 3.2 % | −30.8 / −37.8 | −15.1 dB | below −59 dB |
| Saturn 2 Clean Tube 50 % | 3.5 % | −33.6 / −31.4 | −16.7 dB | −43.8 dB |
| Saturn 2 Clean Tube 100 % | 11.9 % | −24.1 / −20.5 | −5.9 dB | −28.9 dB |
| Saturn 2 Subtle Tube 100 % | 4.6 % | −36.1 / −27.5 | −14.0 dB | −39.8 dB |
| Saturn 2 Warm Tube 25 % | 4.6 % | −26.7 / −50.6 | −15.7 dB | −58.9 dB |
| Ember Round valve +10.4 dB (he liked it) | 4.5 % | −27.2 / −39.1 | −13.4 dB | below −59 dB |
| Ember Tape / Transistor +10.4 dB | 4.9 / 3.0 % | odd only | −6.2 / −5.3 dB | below −59 dB |
| Saturn 2 Gentle Saturation 50 % (odd) | 4.9 % | odd only | −1.7 dB | −59.1 dB |

The old default Tube at +10 dB was Saturn's Clean Tube at full drive (the same THD and intermodulation): far hotter
than its knob said. The new default matches Saturn's tube styles at the same distortion (IMD within 1.5 dB, even
harmonics leading) with less aliasing, and adds no sub-bass pumping on the 808 (energy below 25 Hz −31 dB re the 808's
fundamental band, input −29 dB; Saturn −35). The symmetric styles (Tape, Transistor) intermodulate as much as
symmetric saturation does in Saturn (less, at the same THD) — expected for odd-order curves; they stay. Every style
and character at +10.4 and +15 dB: emscan-10.4.txt / emscan-15.txt (same folder); the amp, fold and rectifier
characters are deliberate distortions (their auto gain leaves them 7–22 dB quieter on a 1 kHz tone: their cabinet
filters — not changed).

### Reference captures before the trials expire
User's priority (2026-11-04): Slate / SSL / Harrison / LiquidSonics iLok trials end 13/11, FabFilter ~16/11, Soundtoys
~22/11, Waves = a new 7-day demo licence from today. `/mnt/e/lynx-work/refcap0124/` (README there) holds, for every
reference with a LYNX pair that still runs: the parameter table, the default state, the default render and one-at-a-time
parameter sweeps of a 7 s stimulus (impulse, log sweep, 1 kHz level staircase, noise bursts, tail) — enough for
null / response / THD / GR-curve comparisons without the plugins.

**fxmeasure fixes found on the way** (tools/fxmeasure):
- a Waves shell's first instance in a process comes up as another of its plugins (LYNX's PluginHost has handled it
  since round 121, fxmeasure did not): today "CLA-2A Stereo" came up as **Dorrough360 5.0**, a surround meter whose
  output is the input — round 170g's Waves rows ("identical: true", 5-channel buses) measured that meter, not the
  plugins. Earlier rounds' Waves data show real processing (165x's CLA-2A gain-reduction curves, 152's RComp), so the
  swap does not happen every time; the log now names the instance, and fxmeasure creates it again until the right
  one comes (as LYNX does);
- a 5-channel main bus is asked for stereo;
- `#` started a comment anywhere, so `set #12 0.5` (by index) never worked: now only at the line start or " # ";
- `buses` prints the layouts a plugin accepts.
With the fix and the new licence, CLA-2A runs (gain +8.5 dB at its default) with no demo dropout over 75 s.

### Stepper audit (`--steppertest`)
45 steppers in the generated effect editors (all pages): Fruity WaveShaper Oversampling, Fruity Limiter attack /
release / comp. curves, Envelope sidechain, Maximus' six band curves (×2 pages), Vocodex Mod / Car input and Bands,
Fruity Delay 3 / Delay Bank dividers and repeats, Gross Beat, Patcher MIDI channel, hardware insert round trip...
- Windows (FL installed, its Default presets loaded): 45 OK, 0 failures (/mnt/e/lynx-work/plugq170c/win/steppertest-win.txt).
- Linux (no FL): 34 OK, 11 SKIP — Maximus' curves and Vocodex' inputs live in FL's state, and a slot without FL's
  state cannot be edited (`stateparams::Table::with` leaves it unchanged): on a machine without FL those controls are
  dead. Open: a built-in default state for Maximus / Vocodex (a new instance without FL), see the audit table.
- macOS: CI app job (artifact editor-pictures-macOS, steppertest.txt).
Before the change the same probe failed the body click (nothing), the trackpad (one tiny event = a whole step,
so a scroll jumped several values) and, on captioned steppers, the lower part of the up arrow.
Instrument editors (Sytrus, Toxic, 3x Osc...) have their own steppers (the generator editor), not covered by this
probe yet.

## Where I stopped
- part 1 (feature/plugq170c): in the merge train (queued 02:45, batch of 6).
- part 2 (feature/plugq170c2): steppers + --steppertest (Linux 34 OK / 11 SKIP, Windows 45 OK), fxmeasure fixes,
  Saturn 2 comparison, PARITY Facet / Atrium stability 7, build-local 1 963 936 checks 0 failures, Windows build OK
  (gfxprobe 0 on Windows, Linux, macOS CI). Train next.
- running outside git: /mnt/e/lynx-work/refcap0124 captures (run_all.sh, waves_run.sh, redo.sh); README there.
- next: Maximus / Vocodex default state without FL; instrument-editor steppers; re-measure the Waves pairs from refcap.
