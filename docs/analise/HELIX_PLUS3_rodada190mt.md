# Helix round 3 — round 190mt (0.1.30)

The third part of LYNX's answer to "a Serum 2-like synth" (Discord): the oscillators round 190ml left for later (granular,
spectral) and the sample oscillator's band limit when pitched up, built clean-room in LYNX's own design. No other
product's code, preset, table, sample or layout was used; the laws below are LYNX's own [MODEL].

## Layout and old projects

- A third block after Helix+ 2's (`wt::ext3Base` = 1851, `wt::ext3::*`, `wt::stateSize` = 2149): 21 automatable values
  (Granular 9, Spectral 11, Sample Clean) and the matrix cells of the 12 new rows for every source (23).
- The same `ext::revision` gates it: **3** = round 3 live (new channels, the factory presets, any edit of a Helix+ value).
  0, 1 and 2 ignore the block, so projects of 190ja … 190ml render bit for bit as before. Every value of the block in
  `defaults()` is "off", Clean included: a revision 2 channel taken to 3 by an edit keeps its sound; `helixDefaults()`
  (new channels, presets) switches Clean on.
- The analysis of the sample (its partials) lives in the channel's `pluginState` as a `SPEC` chunk next to the sample
  (64 frames × up to 64 tracks; frequency on a 1/4096-octave grid above 5 Hz, amplitude as half float: ≤ 16 KB). It is
  made when a sample is loaded (and when the spectral oscillator is switched on for a sample that has none); a channel
  without it (a 190ml sample) gets it made at play time from the same code (the analysis rounds to the stored grid, so
  made-now and stored play the same, bit for bit — tested).
- Gold (`/mnt/e/lynx-work/helix3_190mt/gold`, base = origin/helix2_190ml's core library, 300 random patches each, 44.1 /
  48 / 96 kHz, block sizes 100 / 256): see the round's HANDOFF for the counts (every check 300 / 300).

## Features ([MODEL]: LYNX's own laws)

| Feature | Notes |
|---|---|
| Granular oscillator | Grains of the channel's sample (the sample oscillator's: one sample per channel, loaded from any of the three sections). Density 1 … 200 a second (± a quarter of Spray in time), Size 5 ms … 1 s under a Hann window, from Position ± Spray × half the sample, at the note's speed over the root key + Pitch (±24 st), each on its own side (Pan ± Spread at random); overlapping grains scaled by 1 / √overlap. Up to 16 grains a voice; the randomness is the voice's own (seeded by the voice, no engine random drawn): a note renders the same every time. Grains faster than 1.2 × the sample's speed read its band-limited levels. |
| Spectral oscillator | The sample's partials (Blackman-Harris peaks, 85 ms windows, tracked from frame to frame within half a semitone, the loudest tracks first) played as rotating (cos, sin) pairs, eight at a time (SSE), amplitudes ramped over each 16-sample step. Position moves through the 64 frames; Stretch bends the partials around the root (f' = root × (f / root)^(1 + Stretch / 2)); Shift adds ±2 kHz (cubic); Tilt ±6 dB an octave around 1 kHz; Band / Width keep a band of the partials (flat top, cosine skirts an octave wide; Width 10 = open); Partials keeps the loudest 1 … 64; Semi, Gain, Pan. Partials at or above 0.475 × the rate are dropped (faded from 0.45): anti-aliased by construction. |
| Sample Clean | The sample oscillator above 1.2 × the sample's speed reads one of its half-octave band-limited levels (level k cut at 0.5 × 2^(−k/2) of its rate, kept at twice the rate its band needs; Kaiser low-passes, ~80 dB) through an 8-point windowed sinc (2048 phases, SSE). Levels made once per sample content, shared, only when Clean or the grains need them. Up to 1.2 × its speed (and at its own pitch) the sample itself plays through 190ml's 4-point Hermite: unchanged. |
| Matrix | 12 new rows: Grain Pos / Size / Density / Spray / Pitch / Gain, Spectral Pos / Stretch / Shift / Tilt / Band / Gain, for every source (the macros and chaos sources included). |
| Channel window | A fifth strip row: Granular (On, Load..., Position, Size, Density, Spray, Pitch, Spread, Gain, Pan) and Spectral (On, Load..., Position, Stretch, Shift, Tilt, Band, Width, Partials, Semi, Gain, Pan); Clean in the Sample section. An audio file dropped on either section becomes the channel's sample and switches that oscillator on. |
| Card (Clip-launch) | The Helix+ panel's Oscillators+ section gets Granular and Spectral discs with their positions. |
| Presets | Grain Cloud (Pad), Spectral Glass (Keys), Frozen Choir (Pad), appended after 190ml's (indices unchanged); samples made in code (190ml's vowel, a struck glass), their partials kept. |

## Not done / later

- Unison on the granular and spectral oscillators (one voice each); reverse grains; a partial display.
- The grain loop stays scalar (its reads are gathers); SIMD is in the partial bank, the sinc read and the level filters.
