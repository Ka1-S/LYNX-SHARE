# Helix+ — round 190mi (0.1.30)

Discord asked for "a Serum 2-like VST". LYNX's answer is **Helix+**: LYNX's own extension of Helix (round 190ja,
core/WavetableSynth) with the features that make Serum-class synths popular, built clean-room in LYNX's own design.
Study (public material only: Xfer's product page, the Serum 1 manual, reviews; no Serum binary, preset, table or
layout): `/mnt/e/lynx-work/helix190mi/STUDY.md`.

## Layout and old projects

- Helix's first 778 values stay Live Wavetable's set (`wt::liveStateSize`); .als import / export only touch them.
- Helix+'s block follows (`wt::extBase`, `wt::ext::*`, `wt::stateSize` = 778 + 411): revision, 36 automatable values,
  two drawn LFO shapes (16 points each), the macros' columns for Live's 52 rows, Helix+'s 4 rows × 17 sources.
- `ext::revision`: 0 = a channel saved before 190mi (its missing block reads as defaults, revision 0) — every Helix+ value
  is ignored, so the render is bit for bit the base commit's (gold harness: 300 random Live-layout patches at
  44.1 / 48 / 96 kHz, two block sizes, base library vs this round: 0 differ). 1 = new channels (`helixDefaults()`),
  the factory presets, any edit of a Helix+ value (`padHelixState`). Every Helix+ value defaults to off, so revision 1
  with nothing touched renders the same bits too (also 300 / 300).
- Automation: Helix+'s values keep their engine index (≥ extBase); `helixEngineParams()` stays sorted by index.

## Features (all [MODEL]: LYNX's own laws)

| Feature | Values | Notes |
|---|---|---|
| Warp 2 (per oscillator, after Live's effect) | Off, Sync, Window Sync, Bend + / − / ±, PWM, Asym + / − / ±, Mirror, Flip, Remap, Crush, FM / AM / RM ← the other oscillator; Amount 0…1 (a matrix row) | the mip choice includes each warp's stretch (`warpStretch`); FM / AM / RM read the other oscillator's table voice by voice, even while it is silent |
| Unison+ | Count (Live's, or 2…16), Blend (side voices' level, power kept; a matrix row), Width, Stack (+1 oct, ±1 oct, +1 +2 oct, fifth, fifth + oct, ±2 oct) | stacking walks the voices in left / right pairs from 4 voices up |
| Noise | On, Level (a matrix row), Color (dark ← white → bright) | into the oscillators' bus (filter 1 in Split) |
| Filter+ | per filter: Comb +, Comb −, Formant (A E I O U on Morph), Phaser (spread on Morph) | driven by Live's Freq / Res / Drive / Morph rows, so the matrix moves them |
| Macros | 4 (automatable) | matrix sources for every row; drag their handle onto a knob |
| Drawn LFO | per LFO: up to 16 points, curved segments | click adds, drag moves, double-click / right-click deletes, Alt-drag bends, Ctrl snaps to 1/16 |
| Drag to modulate | Env 2 / Env 3 / LFO 1 / LFO 2 / Amp tabs and M1…M4 handles onto any knob with a row | 50 % on drop; the knob shows a ring with the summed amount; the Matrix page lists the macros and Helix+'s rows (LYNX look) |
| FX rack (per channel) | Distortion (Soft, Hard, Wavefold, Crush), Chorus, Delay (tempo-synced, ping-pong, damped), Reverb (8-line FDN) | fixed order; the engine stays busy until the tail falls silent |
| Factory bank | Init (= Helix's default, revision 1), Wide Supersaw, Reese Bend, Growl FM, Comb Pluck, Vox Pad, Octave Stack Pad, Wobble Draw, Noise Riser, Phaser Keys | core/HelixBank.cpp, LYNX bank → Helix gets the preset box |

## UI

The Live-look device (Device View card) is unchanged. The channel window (Pattern layout) shows the device and the
**Helix+ strip** under it (LYNX's look): Warp 2, Noise, Unison+, Filter+, Macros | LFO Draw, Distortion, Chorus, Delay,
Reverb.

## CPU (callgrind, exact instructions of `Engine::render`, 8 held voices, 1 s at 48 kHz)

The per-step caches added this round (oscillator / unison increments, sub, filter coefficients: recomputed only when
their inputs change, same arithmetic, so the same bits) made Helix's own paths cheaper than before:

| Patch | before | after |
|---|---|---|
| default (Modern, filter) | 154.8 M | 127.3 M (−17.8 %) |
| unison Classic 8 | 401.8 M | 331.5 M (−17.5 %) |
| 2 osc + sub | 197.4 M | 170.0 M (−13.9 %) |

Wall clock (indicative, one pinned core, min of 7, µs per second of audio per voice): default 2497 … 2682 → 1873 …
1958, unison 8 7211 … 7507 → 4922 … 5005. Helix+'s own costs (instructions per voice-sample, the default patch = 344 at
revision 1): Warp 2 440 … 515, unison 16 1452, stack + blend 1015, noise 390, Comb 431, Formant 519, Phaser 449, drawn
LFO 388; the FX rack (all four, per channel) 833 per sample. Full table: /mnt/e/lynx-work/helix190mi/cpu/cg_table.md.

## Not done (next rounds)

- Import audio → wavetable, draw / formula / FFT table editing (design in STUDY.md §5: user tables stored in the
  channel as spectra, built with the factory tables' mip builder).
- A third oscillator; sample / granular / spectral oscillators; OTT-style multiband compressor, EQ and frequency shifter
  in the rack; chaos LFOs; aux sources / curves per matrix cell; an arpeggiator inside Helix.
- Helix+ values are not exported to Live (.als keeps Live's 778 values; a note could list what is lost).
