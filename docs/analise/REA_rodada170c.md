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

## Where I stopped
- done: #11 diagnosis + skin fix + JUCE patch 12 (CoreGraphics empty path), `--gfxprobe`, CI editor pictures step
  (Linux + macOS app jobs), Facet analyser grid; #15 Ember Tube curve (measured before / after); #14 stepper click;
  audit fixes (meter wells, limiter trail, Echoform idle controls, Crest Drive, Hush guard, Atrium early modulation);
  PARITY.md Sound / Stability columns; tests (tests/PlugQ170cTests.inc), build-local 1 963 870 checks, 0 failures;
  Linux app build + `--plugui` pictures (Facet EQ draws; /mnt/e/lynx-work/plugq170c/plugui-linux).
- pending: the macOS CI pictures (runs on wip/plugq170c-probe = without patch 12, wip/plugq170c = with it) were still
  queued at landing; check the editor-pictures-macOS artifact and `gfxprobe.txt` (raw drawText > 0, drawFilledText 0).
- next (audit, open rows): Fruity Limiter scrolling envelope, Ember dimming of overridden controls + Voicing switch,
  Toxic Bend (pitch bend to native generators), 3x Osc default page, Sytrus operator / filter pages, ProEditor partial
  repaints, one analyser tap per slot; Parametric EQ 2 law on new instances = user decision.
- rerun: scratchpad linbuild.sh (buildslot), tool/em.cpp measurements (/mnt/e/lynx-work/plugq170c/ember-*.txt).
