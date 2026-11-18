# PitchNet's polyphonic engine in LYNX SHARE

These files are the polyphonic engine of **PitchNet** (`Source/Audio/Polyphonic`), taken unchanged at commit
`121b3307f2fbb55c19677d6e36be44af27d645b1` (2026-11-17) of <https://github.com/SessionLoops/PitchNet>, and then
modified for LYNX as listed below.

- Copyright (c) 2026 Session Loops and contributors. PitchNet is a modified derivative of HachiTune
  (<https://github.com/KCKT0112/HachiTune>) by the HachiTune contributors.
- License: GNU Affero General Public License, version 3 (`LICENSE` in this folder), the same license as LYNX SHARE.
- PitchNet's docs for the engine: `docs/polyphonic-editing.md` and `docs/testing/` in its repository.
- The note-filter models `PolyPurifierModel.h` and `PolyGuidedPurifierModel.h` are gradient-boosted trees that PitchNet
  trained on features of recordings from the **Salamander Grand Piano V3** by Alexander Holm (CC BY 3.0,
  <https://creativecommons.org/licenses/by/3.0/>) and the **VSCO 2 Community Edition** by Versilian Studios and Ivy
  Audio (CC0 1.0). No audio from those libraries is included.

Not taken: `BasicPitchEvidence.h/.cpp` (PitchNet's own ONNX Runtime wrapper). LYNX feeds the engine's
`NoteEvidence` from its own Basic Pitch session (`src/ui/BasicPitchSession.h`, `src/ui/PolyEditor.cpp` `evidenceOf`).

## LYNX modifications (round 190lk)

Every changed place is marked `LYNX modification (round 190lk)` in the source.

1. **Masks on demand** (memory follows the edited spans, not the length of the recording). PitchNet keeps every active
   note's masks for the whole recording after `analyze()` (about 0.6 MB per second of dense chords; a 288 s test file
   held 178 MB of masks). LYNX analyses once, saves only the notes, and builds the masks of the NMF blocks an edit
   reaches when it renders:
   - `Params::deferMasks` (`PolyEngine.h`): `analyze()` leaves the masks out.
   - `Analysis::maskBlocksBuilt` (`PolyEngine.h`): which NMF blocks are built.
   - `analyzeSpectra (ctx, cancel, frameLo, frameHi)` (`PolySpectral.cpp`, `PolyInternal.h`): spectral peaks for a
     frame range only (each frame's peaks depend only on that frame and its neighbours, so they equal a whole pass).
   - `maskBlocks()` / `buildMaskBlock()` (`PolySoundModel.cpp`, `PolyInternal.h`): the block grid and one block's
     solve, split out of `buildMasks()`, which solves the same blocks the same way.
   - `PolyLazy.cpp` (new): `framesForEdits()`, `ensureMasks()`, `releaseMasks()`, `maskBytes()`.
   Measured (round 190lk lab): renders with masks built on demand equal PitchNet's full-analysis renders sample for
   sample (24 of 24 edit cases bit-identical; one take-background case differs by 2e-9); on a 288 s file the analysis
   peak memory above the loaded audio fell from ~172 MB to ~27 MB and the analysis time by about two thirds (the NMF
   now runs only for edited blocks).

Round 190lk made no change to the detection, the sound model's mathematics or the renderer.

## LYNX modifications (round 190lo)

Marked `LYNX modification (round 190lo)` in the source; both are off in PitchNet's default `Params`, LYNX turns them on
for new analyses (a project saved by round 190lk plays exactly as before).

2. **Slow re-attacks** (`Params::slowReattacks`, `splitSlowReattacks()` in `PolyExtraction.cpp`, called in
   `PolyEngine.cpp` after the note filter). PitchNet splits a held note where its level rises 2x within ~45 ms; a pad or
   strings playing the same pitch in the next chord rises over ~0.25 s, so two chords' notes became one. The new pass
   cuts an active note where its level dips to at most 30 % of what it had reached and comes back 3x within 0.5 s with
   most of its partials rising together (a dip above 15 % only where other notes begin or end too: beating detuned
   voices dip inside a held chord); the pieces keep the note's probability (it never changes which candidates are
   notes). Lab: pad notes found with their own onset 9 → 14 of 17 (with LYNX's evidence rules in `core/PolyEdit.cpp`:
   16 of 17); piano, guitar, organ, real piano and real strings unchanged.
3. **Overlap-aware moves** (`Params::overlapAwareMoves`, `PolyRenderer.cpp`). A moved note's copy comes from another
   frame; in bins it shares with other notes the copy carries their sound from that frame too, and its complex sum
   with what stays partly cancels. The copy is now joined with the phase of the complex sum and the level of the power
   sum. Lab, 8 moves: bar spectral SNR +0.1 … +1.0 dB on 7, −0.2 dB on one (pad, late), NMR equal or better on 7,
   +0.2 dB on one (organ, late).

## Updating

Copy the files of a newer PitchNet commit over these, re-apply the marked changes (they are small and local), run the
core tests (`tests/PolyEdit190lkTests.inc`, `tests/PolyEdit190loTests.inc`, including the on-demand-masks = full-analysis check) and the rounds 190lk / 190lo
quality labs (`/mnt/e/lynx-work/polyedit190lk/s/`, `/mnt/e/lynx-work/polyedit2_190lo/s/`), and update the commit above and the row in `THIRD_PARTY.md`.
Fixes worth offering upstream are kept as patches in the round's folder (not posted to PitchNet's repository).
