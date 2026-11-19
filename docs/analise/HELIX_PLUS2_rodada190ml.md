# Helix+ 2 — round 190ml (0.1.30)

The second part of LYNX's answer to "a Serum 2-like synth" (Discord): the items round 190mi left for later
(`/mnt/e/lynx-work/helix190mi/STUDY.md` §2 and §5), built clean-room in LYNX's own design. Public Serum material was a
reference for features only; no Serum binary, preset, table or layout was used.

## Layout and old projects

- A second block after Helix+'s (`wt::ext2Base` = 1189, `wt::ext2::*`, `wt::stateSize` = 1851): 44 automatable values,
  oscillator 3's table, the three "plays its user table" switches, the sample's root key, and the matrix cells of the
  new sources (Macro 5…8, Chaos 1 / 2) on the older rows and of the new rows (oscillator 3's six rows and Warp 2, the
  sample's pitch / gain / pan, Shift, Multiband Depth) for every source.
- The same `ext::revision` gates it: **2** = Helix+ 2 live (new channels, the factory presets, any edit of a Helix+ or
  Helix+ 2 value). 0 and 1 ignore the new block, so projects of 190ja … 190mi render bit for bit as before.
  Gold harness (`/mnt/e/lynx-work/helix2_190ml/gold`): base library (origin/helix190mi) vs this round, 300 random
  patches each: revision 0 (Live layout) 300 / 300 identical, revision 1 with Helix+'s defaults 300 / 300, **revision 1
  with random Helix+ features in use** (warps, unison, filters, macros, drawn LFOs, the rack, macro cells) 300 / 300,
  revision 2 with everything new at its default = revision 0's bits, 300 / 300.
- The user tables and the sample live in the channel's `pluginState` (`wt::encodeUserData`: "HLX2", chunks per
  oscillator table — 64 frames × the partials used, amplitude as half float, phase as 1/65536 turn — and the sample as
  16-bit), so they are saved with the project and with .lynxpreset files and undo like any channel edit.

## Features ([MODEL]: LYNX's own laws)

| Feature | Notes |
|---|---|
| User tables | per oscillator; built by the factory tables' mip builder (19 mips, half an octave apart: the same band limit); shared by content |
| Import audio → table | drop / open a file: slice by pitch (YIN, the cycle resampled to 2048), fixed frame size, or 64 frames spread over the file; Normalise (RMS per frame) and Phase-align (each fundamental at phase 0) |
| Table editor | Draw (a frame's cycle), Harmonics (the first 64 partials, one frame or all), Formula (below), Copy to all frames, Morph 1 → 64, From factory table, Export WAV (64 × 2048 float) |
| Formulas | LYNX's own expression language, a tree walker with numbers only (no names but x, p, t, f, pi, e and 31 math functions; 2000 characters, 4000 nodes, depth 64; non-finite results → 0) |
| Anti-aliased hard warps (revision 2) | Sync, PWM, Flip: polyBLEP + polyBLAMP at each cut (the jump and the slope change read at the cut); Mirror: polyBLAMP at its turns; Crush: first-order antiderivative of the quantiser |
| Oscillator 3 | Live's oscillator set (table, position, FM / Classic / Modern, transpose, detune, pan, gain) + Warp 2 (FM / AM / RM read oscillator 1); its own matrix rows; joins oscillator 1's bus |
| Sample oscillator | one-shot or loop from Start, root key, unison as the oscillators, Warp 2 on a sample: Sync (restarts at the note's cycle, polyBLEP), FM / AM / RM ← oscillator 1, Crush; 10 s at most per channel |
| Macros 5…8 | automatable; sources for every row |
| Chaos 1 / 2 | Smooth Random, Lorenz, Rössler; rate 0.01…30 Hz, amount; each voice its own (seeded by the voice, no engine random drawn); internal sources (Global Mod Amount scales them) |
| Rack | Frequency shifter (a 90° all-pass pair, SIMD: one vector per stage for both paths and both sides; ±5 kHz cubic), EQ (low shelf 120 Hz, bell 100 Hz…10 kHz, high shelf 6 kHz), Multiband (3 bands at 120 Hz / 2.5 kHz, up- and downward, Depth, Time, Gain). Order: Distortion, Shift, Chorus, Delay, Reverb, EQ, Multiband |
| Card (Clip-launch) | the Device View card gets a title-bar well that shows a Helix+ panel beside Live's device, in Live's card look (Live's knobs and power discs) |

The rack's rows (Shift, Multiband Depth) follow the newest note's modulation (the rack is one per channel).
