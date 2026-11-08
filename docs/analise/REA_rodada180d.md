# REA round 180d: what separates an exported Thief from FL's own

Method: FL's `/R` render of FL's Thief with one group of LYNX's records swapped in, compared with FL's own render
(`scripts/flref_compare.py pair`). Files and renders: `E:\\lynx-work\\render180d` (`hyb`, `flr`). Only FL's own files and
binary were studied. Nobody has listened to these; every number is a measurement.

## 1. Ranking (lower xcorr = further from FL)

| from LYNX | xcorr | ESR dB | rms dB | env_corr | spec |
|---|---|---|---|---|---|
| whole export, before this round's fixes | 0.673 | -2.20 | -0.47 | 0.932 | 1.03 |
| `0xD7` stretch mode -2 instead of -1 | 0.869 | -4.15 | -0.36 | 0.987 | |
| `0xD4` +16, plugin flags (0x20 = Smart disable) | 0.891 | -8.33 | 0.00 | 0.972 | 0.03 |
| item +0x38, fade flags | 0.931 | -11.06 | -0.02 | 0.983 | 0.34 |
| stretch PITCH moved to the header pitch | 0.934 | -11.92 | +0.01 | 1.000 | 0.16 |
| audio clip start + end offsets | 0.946 | -7.66 | -0.28 | 0.956 | 0.53 |
| notes + links | 0.967 | -14.64 | | | |
| audio clip end offsets alone | 0.977 | -11.22 | -0.11 | 0.992 | 0.31 |
| item +0x30, fade-out tension | 0.992 | -20.63 | +0.05 | 0.999 | 0.04 |

At FL's repeat scatter (xcorr 0.997, ESR -17 to -31 dB): `0xDA`, `0x87`, `0x8F`, `0x91`, `0xDD`, `0xE5`, `0xD4` +20 and window
numbers, mixer `0xE1` / `0xD4` / `0x80` / `0x95`, automation clips together with their range, item +36..+47, +54.

Not rankable: `0xDB` or `0xEA` alone (102.49 s and 378.13 s) split an automation clip from its range.

## 2. FL's clip fades [FL renders + DECOMP, FLEngine_x64.dll]

Item fields: +0x24 fade-in ms, +0x28 its tension, +0x2C fade-out ms, +0x30 its tension, +0x38 flags (`FUN_00cf8750`).

Flags: bit 0 / 1 manual fade-in / -out; `>> 2 & 7`, `>> 14 & 7` the declick choice of each side; `>> 8 & 3`, `>> 12 & 3` the shape,
1 = linear, else the S curve (`FUN_0097e0f0`). Measured: a stored length with its bit off is not played when nothing overlaps
(probes `p56`, `p57`). The voice code uses any stored length above 1e-6 ms; the editor functions zero or recompute non-manual
sides from the overlap (`FUN_00cfbf60`, `FUN_00cfdd90`, `FUN_00cfd680`), so the load path is inferred to do it too (not read).

A fade of N = round(ms * 0.001 * rate) samples, x = (i + 1) / (N + 1):
- y = sin²(pi/2 x) in, cos²(pi/2 x) out; linear shape: x, 1 - x (`FUN_00977570`, `FUN_00977790`)
- t = round(tension * 100) * 0.01 (`FUN_0097d980`), then (`FUN_006774b0`, `FUN_00977870`):
  t = 0: g = y; t > 0: g = 1 - ((1 + t)^(1 - y) - 1) / t; t < 0: g = ((1 + |t|)^y - 1) / |t|

Checks: 200 ms at -812.933 against the render: RMS 0.0115, max 0.032; other curves 0.15 to 0.67. Probe `p57` (one SAX clip with S,
linear, +-50, +-5000, in only, out only, off): the model has the smallest residual in every row (-22.6 to -29 dB); the wrong
ones reach -3.7 to -22.4 dB. The rest is inferred to be the channel's mixer effects.

File versions (`FUN_00f558e0`, `FUN_010657a0`, `FUN_01061660`): below 20.99.1 the records have no fades; below 25.1.0 FL sets the
declick choice to 2 on audio clips. An 11.1.0 demo with 88-byte records and bits off: forcing the bits on changes FL's render
(xcorr 0.840), so FL honours them there too. Its fade fields hold garbage (1.6e10, NaN). Record sizes below 88 keep LYNX's old reading,
untested.

## 3. What FL's TIME stretches [FL renders + DECOMP]

- Rate (`FUN_00d26890`, every block): Stretch modes with TIME: inc = L / (TIME/192 beats × 2^(MUL/10000) × item +0x40). A
  clip's start / end are source milliseconds (`FUN_00d2a7b0`); the end only cuts (probe `p59`: end at 0.5x / 1x / 2x of
  the clip length plays at one speed).
- L (`FUN_00ff0db0`, `FUN_00ff0b30`): the region after the precomputed effects, cut by Trim (backward scan, trim/32768
  per frame); 0xD7 +0x9D on: the trimmed length.
- Probe `p61` (Importer Ride alone): TIME 367 plays 2.00x slower than TIME 0, TIME 734 about 4x, Trim off 0.55x (the
  whole 1.30 s), stretch PITCH -100 or 0 the same (cents, pitch only). Ride's Precomputed fade out 807 (0x4B) makes the
  trimmed length 0.38 s; Titey Tom (no fade) trims to 1.668 s = its TIME, and FL leaves it alone.
- Probe `p60`: an audio clip channel's Start 0.25 is honoured (the clip starts 618 ms into Titey Tom). LYNX plays audio
  clips over the whole file (round 167k): a follow-up.
- REA sessions this round: the trim session ran about 35 minutes, longer than the 20-minute turn the brief asks for (it was
  not pre-sized). The Ghidra lock was held for all of it and the binary closed after.

## 4. Open

- Audio clip offsets: what FL's span / length ratio depends on. Start and end must be read together (FL's start with LYNX's end
  moves the song to 0.977; LYNX's start with FL's end to 0.627).
- Short stretched one-shots in LYNX decay faster than in FL (probe `p56`: Importer Ride at TIME 367 is -47 dB at 160 ms where FL is
  -26 dB; 808 Snare at TIME 63 likewise; Titey Tom at TIME 801 matches).
- FL's declick choice for a side with no fade is kept for the export but LYNX does not play it.

## Best way?

- **FL:** S curve with a per-side log-like tension and a linear shape, baked into the voice.
- **Ableton:** per-clip fades with curve skew and slope (`Fades` in `ALS_FORMAT.md`), not compared in sound here.
- **Users:** not asked; Thief's exported fades were simply missing or wrong in FL.
- **Verdict:** match FL exactly for imported and new clips; a `.lynx` saved before this round keeps straight fades, so a finished mix does not change on reopening.
