# Round 190aj: Slicex editor — deck zoom write-back, wave selection, Add region / Set loop, end-tag drag (#74)

Continues REA_rodada190ac.md. Everything below was measured on FL's own saves of one project copy
(`/mnt/e/lynx-work/slicex190aj/fl/aj_2.flp … aj_29.flp`, one save per step; FL session 2026-11-10 20:20–20:33
under the GUI + FL locks, no FL setting changed). Tools: `p/sxparams` (deck parameters), `p/sxreg` (region list),
`p/sxwavout` (the embedded WAV), `p/sxprove` (LYNX's writers against FL's bytes). The DirectWave part of the round is
in REA_rodada190aj_dw.md.

## 1. What the deck parameters hold (corrects 190h)

| parameter | meaning | proof |
|---|---|---|
| 0x00 / 0x08 (doubles) | the wave selection, start / end, in BEATS; −1 / −1 = none | aj_4: 1.41176… / 3.20448… for a drag over 126…286 of 714 px on an 8-beat deck |
| 0x10 / 0x18 (doubles) | the shown range in beats (start may be > 0) | aj_2: 0.5763301107581897 … 7.421090316219005 |
| 0x36 | View ▸ Loop (shown) | Set loop turns it on (aj_25) |

190h's note read 0x00/0x08 as frames; FL's saves say beats. The frame of a beat is `round (beat · frames / beats)`,
with the deck's beat count from the embedded WAV's `acid` chunk (190ac).

## 2. Mouse in the wave (FL's deck: x 274…988 on screen, 714 px)

- No selection, click: nothing changes (the region shown stays). LYNX keeps its stage-1 extra here: a plain click
  picks and plays the region under the mouse.
- No selection, drag: selection from the press to the mouse, to the pixel, not snapped (aj_4).
- With a selection, press: the NEAREST edge jumps to the mouse and follows the drag (aj_12, aj_18, aj_19, aj_20).
- Double click: deselect (aj_21).
- LENGTH / SEL shows "001:02:075 to 001:04:030" (bar:beat:tick, 96 ticks per beat) while a selection exists.

## 3. Zoom and Select menus

- Zoom on selection: the shown range = the selection exactly (aj_8).
- Zoom on left / right of selection: 357 frames (0.5 frame per pixel of the 714 px deck) centred on that edge
  (aj_9, aj_10). `slicex::zoomEdgeFramesPerPixel`.
- Zoom in / out (menu, PgUp / PgDn): ×0.8555950256826 (= 2^−0.225) about the centre of the shown range (aj_17).
  `slicex::zoomStep`. The mouse wheel's step was not measured again (stays as built in 190ac).
- Select before current selection = [0, start]; after = [end, sample end]; Select zoomed part = the shown range.
- Select next / previous region: from a selection that is no region → region 0 (next); then one step each time,
  wrapping around; the region becomes the one shown (aj_14–16, aj_22, aj_23). Previous from "no region" was not
  measured on its own; LYNX takes the last region.
- The shown range is written back to 0x10/0x18 by FL at every save. LYNX writes it 300 ms after the last zoom or
  scroll and when the editor closes, without an undo step.

## 4. Add region, Set loop

- Add region (Alt+M) asks the name in an inline box ("New region name"). The region: start = round (selStart ·
  frames per beat), last frame = round (selEnd · fpb) − 1, purpose "beat", inserted in sample order; the region shown
  did not change (aj_24: 62259…96846 "Zap"). Measured on one deck; LYNX uses its standard name prompt.
- Set loop (Alt+L): `smpl` loop start = round (start), end = round (end) − 1 (aj_25: 111671…161081), and View ▸
  Loop goes on. FL draws the loop as a red line with a "Loop" tag at the bottom at its start and a thin line + tag
  at its end.

## 5. Tags

- End tag drag: only the length changes, by the mouse distance, unsnapped; the region may then overlap the next ones
  (aj_26: 22049 → 14637; aj_27: → 54166). A click on an end tag changes nothing (aj_28).
- A start tag is grabbed on its name too (aj_29: grabbed 12 px right of its line, 66150 → 21679; the list is
  re-sorted by start). This closes 190ac's open difference 3.

## 6. The `strc` chunk

FL keeps a slice list next to the cue list. Its layout holds on all 9 saves: a 28-byte header (28, regions + 1, five
more words LYNX keeps as found), one entry (2, 0, 0, 0, 0, 0), then one entry (0, start, 0, 0, 0, 0) per region, 24
bytes each. `slicex::withRegions` rebuilds it; with that, LYNX's output for Add region, Set loop, both end-tag drags
and the start-tag move is byte for byte FL's file (`p/sxprove` → out/sxprove.txt, result=OK).

## 7. Proof in the editor itself

`LYNX --slicexedittest project script report [png-prefix]` opens the project's Slicex editor off screen and runs a
script of gestures at fractions of the deck's width (so FL's pixels can be repeated), menu entries and shortcuts;
after each command it reports what the state holds, and `savewav` writes the deck's WAV for a byte compare with FL's.
The round's script and report: /mnt/e/lynx-work/slicex190aj/edit/.

## 8. Best way?

- Selection in beats (FL's own unit) rather than frames: a project saved by LYNX opens in FL with the same
  selection, and nothing has to be converted on tempo changes. Kept.
- Nearest-edge on press: FL has no way to start a new selection without a double click first. Ableton and most
  editors start a new selection on a press outside the old one. Option for the user (not built): Shift+press = move
  the nearest edge, plain press outside the selection = new selection. Built: FL's rule, as measured.
- Plain click without a selection plays the region in LYNX (FL: nothing). An addition, kept; it does not change what
  is saved.
- Name prompt: FL's inline box is quicker than a dialog. Recommended for a later round: an inline box for rename and
  add (one widget for both).
- Still not drawn: Snap and View menus, Export regions, the tools listed in REA_rodada190ac §1.
