# Round 170f — Pattern Blocks (issue #60, xerydian) → 0.1.24

Design + evidence for FL Studio's legacy **Pattern Blocks** playlist, rebuilt in LYNX SHARE as an opt-in, per-project
mode that works next to the clip playlist. (Copy of this design: tmp/coord/pattern-blocks-0.1.24.md.)

## 1. The request (reporter's words)

> "This Pattern Blocks playlist was a staple from the first FL Studio versions up until FL Studio 9, then tucked under an
> opt-in setting in FL Studio 10, then hidden behind a registry key in FL Studio 11, then fully deleted in FL Studio 12.
> […] as seen in FL Studio 10, as an opt-in setting for those who miss it, that works in tandem with the current
> playlist." Why: **more efficient** (one click, no pattern to pick first), **more legible** (no note-preview clutter),
> **more predictable** (the row always is its pattern; whole patterns are guaranteed: no shifted, cropped or
> moved-to-another-row clips, which FL 11's "Blocks" template with *Lock to content* tracks can't guarantee).

## 2. FL evidence

FL 10 is not installed and must not be downloaded from unofficial sources, so FL 10/9 was not driven. Evidence is
Image-Line's own documentation from that era plus FL 2026's binary (static strings only; FL was not launched).

| # | Fact | Source |
|---|---|---|
| E1 | "Since FL Studio 10, the Pattern Blocks are disabled by default. It is still possible to activate them in the menu Options > General Settings > Enable legacy pattern blocks." FL 11/12: "Block tracks were removed from the Playlist"; emulation = File › New from template › Other › Blocks. | Image-Line knowledge base, ans=120 (support.image-line.com/action/knowledgebase?ans=120) |
| E2 | "The Playlist is split into an upper Clip track and a lower Block track area. Find the line between the 2 and drag it up." FL 7–9: Pattern Clips in the upper area replace the lower Pattern Blocks; Playlist menu Edit › "Turn pattern instances into clips" converts them. | same KB article |
| E3 | FL 8 manual: the block playlist "is similar to the pattern view, except that the steps operate on whole patterns rather than single channels, and the bar lengths here vary to reflect the pattern length." "The Playlist has an upper and lower area. These can be swapped with the menu options in the Playlist, but we will assume your Clip Tracks are above the Block tracks." "A pattern can be placed in either a clip track OR block track … you only need to use one or the other for the pattern to play." "The clip tracks show more information about the data patterns & clips contain. The blocks are more minimalist." | FL Studio 8 Getting Started Manual (fl8_englishgettingstartedmanual.pdf), pages 22–24 |
| E4 | Enabling it in FL 10 shows: "Pattern Insances (Blocks) have become obsolete and will not appear in future version of FL Studio." | beatproduction.net "How to Enable Legacy Pattern Blocks in FL Studio" (quote, typo theirs) |
| E5 | Users: blocks are "the things on the bottom of the playlist"; a one-bar part is "painted" across bars; "cloning in the pattern blocks viewer". | ocremix.org forum thread 24095 |
| E6 | FL 2026 still carries the legacy conversion strings: "Send pattern instances here" (FLEngine_x64.dll UTF-16 at file offset 0x107fa2d / 0x13d5500), "pattern instances into clips" (0x108b781), "replace pattern instances" (0x108bc2d). Old block projects are turned into clips on load. | `strings -el -t x FLEngine_x64.dll` (static; FL not launched) |

What the sources settle: one row per pattern (the step-sequencer analogy: a row = a pattern instead of a channel),
blocks as long as their pattern on the bar grid, a click paints (no pattern chosen first), clip tracks above and block
rows below with a draggable divider (swappable in FL 8), blocks drawn minimalist, a pattern plays from either area,
and a command that turns the block instances into clips. FL 10's option was a *General* (global) setting; the issue
and the LYNX design make it per project (it is project data anyway).

What they don't settle (LYNX choices, marked "LYNX" below): the exact row height, overlapping blocks after a pattern
grows, a block row's header gestures, and the conversion's target tracks.

## 3. Design (LYNX)

**Option.** `Song::patternBlocksOn` (bool, saved in .lynx) — the Playlist's right-click menu and its panel options menu
*Pattern blocks (current project)*, key-map action `playlist.patternBlocks` (no default key; assignable). Turning it on shows the blocks section; turning it off while
blocks exist asks: *Turn them into clips* / *Delete them* / *Cancel* (LYNX: hidden blocks that still play would be
confusing). Off by default (as FL 10).

**Model.** `PatternBlock { int pattern; int64 start; }` in `Song::blocks` (current arrangement) and
`Arrangement::blocks` (others) — blocks follow arrangements like clips. Start = the start of a bar (time-signature
aware, `Song::barAt`). Length is never stored: always the pattern's `effectiveLength` (whole pattern, E3). Patterns
inserted / cloned / split / moved / deleted reindex or remove blocks with the clips (RackOps, Song::removePattern).

**Playback / render.** `patternblocks::expandSong` turns the blocks into ordinary pattern clips (track −1: no
Playlist track, never muted by a track) where the rack state is built (next to channelloops::expandSong), and in
makeExportSong / makeRenderSong / stem detection — so live play, song mode loop length (`Song::songLength` includes
blocks), WAV/MP3/stems export and Quick render all hear them, with no audio-thread cost. LYNX: when a pattern grows so
that a block overlaps the next block of its row, the earlier one stops where the next starts (a pattern never plays
twice on top of itself); it is drawn cut there too.

**Section.** Below the clip tracks (FL's default, E2/E3), as tall as the user drags its divider (default ~⅓ of the
grid, min 3 rows; double-click the divider: fit all rows); it shares the Playlist's horizontal zoom, scroll and
playhead; it has its own vertical scroll (wheel). Left: one header per pattern in pattern order (colour swatch + name,
the current pattern highlighted). Rows 18 px (LYNX). Blocks are solid rectangles in the pattern's colour, no notes, no
text (E3 "minimalist"); a thin darker edge separates touching blocks.

**Mouse.** Left-click on a row = a block at that bar (snapped to the bar; no pattern to choose: the row *is* the
pattern). Left-drag paints along the row (each bar whose block would fit). Right-click deletes the block under the
mouse; right-drag erases every block crossed (any row). There is no move, resize, slip, slice or row change for blocks
— the guarantees asked for. A click inside an existing block does nothing but select its pattern. Header: click =
current pattern; double-click = open it in the Channel Rack; right-click = the pattern's Rename and color. Wheel
scrolls rows; Shift+wheel / Ctrl+wheel go to the Playlist (time scroll / zoom). Every paint / erase gesture is one undo
step (editSong groups a held click).

**Convert.** Playlist menu *Turn pattern blocks into clips* (FL's Edit › "Turn pattern instances into clips", E2/E6):
each pattern's blocks become pattern clips on a track of its own, below the last used track, named after the pattern
and locked to its content (FL 2026's *Lock to content*), at the same positions and lengths (cut like playback).

**Not done (said in the reply):** importing pre-FL 10 .flp files that still hold blocks (no such file to test against;
FL 2026 itself converts them to clips); FL 8's swap of the two areas; Online Sessions don't split blocks by owner (the
host's blocks travel with the project).

## 4. Per-case table

| Item | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| Opt-in block playlist | E1, E4 | none | per-project option in the Playlist menus (+ key-map action) |
| Row = pattern, one click paints | E3, E5 | Picker + pencil (pick a pattern first) | block rows, left-click / drag paints |
| Whole-pattern blocks on the bar grid | E3 | clips can be trimmed/slipped/moved | blocks = pattern length at bar starts, no move/resize |
| Minimal display | E3 | clip previews | solid colour blocks |
| Clip area above, blocks below, divider | E2, E3 | — | section under the tracks, draggable divider |
| Blocks → clips | E2, E6 | — | Turn pattern blocks into clips |

## 5. Verification

- Core tests (tests/PatternBlocks170fTests.inc): paint/drag/erase, playback through RackState::build,
  blocks + clips together, grown pattern cut at the next block, time-signature bars, pattern
  delete/move/clone remapping, arrangements (switch + copy), turn into clips (one and every
  arrangement), WAV/stem/MIDI export, undo labels, key-map actions.
- Selftest (Windows, LYNX.exe --selftest): `pattern_blocks=OK` (save/load round trip incl. arrangements).
- Snapshot: `--snapshot` writes `-playlist-blocks.png` (5 pattern rows under the clip tracks).
- Real-mouse GUI pass on Windows (fresh LYNX_PROFILE_DIR, LYNX_TEST_MODE=1): click paints at the
  bar, left-drag paints a row, right-click / right-drag erase, a click inside a block is a no-op,
  a 2-bar pattern spans 2 bars, playback crosses the blocks with meter activity, Save writes
  `"patternBlocks": true` + `"blocks"` (13 blocks checked in the saved JSON).
- Screenshots: /mnt/e/lynx-work/patblocks170f (g1-g9.png, shots/s-playlist-blocks.png).

## Where I stopped

- Done: research, design, core + UI + i18n (15 languages), tests, selftest, snapshot, GUI pass.
- Not done (follow-ups): importing legacy FL <=11 .flp pattern-block data; FL 8's "swap blocks
  above/below clip tracks" toggle; a clean GUI check of Ctrl+Z on a plugin-free project
  (undo is covered by the core test, SendKeys focus during the GUI pass was unreliable).
