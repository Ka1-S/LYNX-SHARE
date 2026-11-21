# Helix round 4 — round 190oc (0.1.31)

The follow-ups round 190mt left (QUEUE-0.1.31 item 7): unison on the granular and spectral oscillators, reverse grains
and a view of the spectral oscillator's partials. LYNX's own design, clean-room: no other product's code, preset, table,
sample or layout was used; the laws below are LYNX's own [MODEL].

## Layout and old projects

- A fourth block after round 3's (`wt::ext4Base` = 2149, `wt::ext4::*`, `wt::stateSize` = 2157): seven automatable values
  (Grain Unison, Grain Unison Detune, Grain Unison Width, Grain Reverse, Spectral Unison, Spectral Unison Detune,
  Spectral Unison Width); no new matrix rows.
- The same `ext::revision` gates it: **4** = round 4 live (new channels, the factory presets, any edit of a Helix+ value).
  0 … 3 ignore the block, so every project of 190ja … 190mt renders bit for bit as before; the block's defaults (one
  voice, no reverse) change nothing, so a revision 3 channel taken to 4 by an edit keeps its sound. One voice plays
  190mt's code path unchanged (the grains draw no extra random number, the partial bank the same arithmetic).
- Gold (`/mnt/e/lynx-work/helix4_khs3_190oc/gold`, base = origin/wip/rollup031's core library, 300 random patches each,
  44.1 / 48 / 96 kHz, blocks 100 / 256): revisions 0, 1, 2 and 3 (with random round 3 features: grains, partials, Clean,
  cells on round 3's rows) identical to the base, the revision 3 patches at revision 4 identical to the base's revision 3,
  revision 4 at the defaults identical to the base's revision 0 — every set 300 / 300.
- "Save a copy for 0.1.30": the block is cut (0.1.30 reads the first 2149 values) and the revision taken back to 3; a
  loss line names the channels whose unison or reverse grains played. The copy for 0.1.28 then turns Helix into an empty
  Sampler as before.

## Features ([MODEL]: LYNX's own laws)

| Feature | Notes |
|---|---|
| Grain unison | 1 … 8 voices: each spawn makes one grain a voice (each with its own random position within Spray and its own Spread), voice u of n at its place s = 2u / (n − 1) − 1: detuned s × Detune × 50 cents, panned Pan + s × Width (Live's pan law, as the oscillators'). The sum is kept near one grain's level: scaled by 1 / √(the grains sounding at once: overlap × voices, at most 24), as uncorrelated grains add up in power. Up to 24 grains a voice sound with unison (16 without, as 190mt). |
| Reverse grains | Reverse 0 … 1: the share of grains read backwards (1: all; between: each grain at random from the voice's own seeded generator). A reversed grain reads the stretch it would have read forwards, from its end back to its start, under the same window; it stays inside the sample (or the band-limited level it reads). |
| Spectral unison | 1 … 8 voices, each its own block of the partial bank with its own start phases, detuned s × Detune × 50 cents, panned Pan + s × Width, scaled by 1 / √n. The voices share 128 partial oscillators: each voice plays at most min (64, 128 / n) of the loudest partials (two voices: all 64; four: 32 each; eight: 16 each), so the heaviest spectral patch costs at most two full banks and stays inside Helix's heaviest patch (3 oscillators × unison 16). Changing the voice count while a note plays starts the bank again (no fault). |
| Partials view | A sixth strip row's right part: the spectral oscillator's partials at its Position (the analysis frames interpolated as the engine does), at the root key + Semi with Stretch, Shift, Tilt, the band and the top fade applied: a line per partial on a log-frequency axis (20 Hz … 20 kHz), as loud as it plays (0 … −60 dB of the frame's loudest); the partials that Partials (and the unison's share) keeps lit in the accent colour, the others dim; the band (Band, Width) shaded with its skirts; with unison, the outer voices' detune as thin lines beside each lit partial. It follows every value (automation included) and repaints with the strip. |
| Channel window | The sixth strip row: Grain Unison (Voices, Detune, Width, Reverse), Spectral Unison (Voices, Detune, Width), Partials (the view). The window grows by one row (76 px). |
| Presets | Grain Swarm (Pad: five grain voices, a third of the grains reversed) and Spectral Ensemble (Pad: four spectral voices on 190mt's glass), appended (indices of the 28 before unchanged). |

## Not done / later

- The grain loop stays scalar (gathers); the unison grains reuse it.
- No matrix rows for the new values (they are automatable; a row each can follow if asked).
