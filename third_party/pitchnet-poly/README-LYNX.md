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

No change was made to the detection, the sound model's mathematics or the renderer.

## Updating

Copy the files of a newer PitchNet commit over these, re-apply the marked changes (they are small and local), run the
core tests (`tests/PolyEdit190lkTests.inc`, including the on-demand-masks = full-analysis check) and the round 190lk
quality lab (`/mnt/e/lynx-work/polyedit190lk/s/`), and update the commit above and the row in `THIRD_PARTY.md`.
Fixes worth offering upstream are kept as patches in the round's folder (not posted to PitchNet's repository).
