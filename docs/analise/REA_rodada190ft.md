# Round 190ft — issues #163 (Ctrl+click multi-select) and #164 (resize Playlist tracks by their edge)

FL evidence: Ghidra 12.1.4 headless on FL Studio 2026's `FLEngine_x64.dll` (sha256 6d9c5ab0…, the analysed project of
round flgap187a), plus REA MCP (`xrefs`, `record_unknown`) on the same binary from the 190ev snapshot. Decompiled
excerpts stay outside the repo (`/mnt/e/lynx-work/issues190ft/dec/`); only behaviour is written here (clean-room).
Delphi TShiftState bits: Shift 1, Alt 2, Ctrl 4, Left 8, Middle 0x20, Double 0x40. Playlist track record: 0x114 bytes,
+0x10 size factor (f32), +0x14 locked size (i32), +0x18 enabled, +0x1a grouped, +0xc1 selected, +0x94 shown height.

## Handlers

| Handler | Address | Role |
|---|---|---|
| `AUTracksPanelMouseDown` | 0xea9660 | press on the Playlist track headers |
| `AUTracksPanelMouseMove` | 0xeab1f0 | hover cursor / hint, range drag, resize drag |
| `AUTracksPanelMouseUp` | 0xeabd30 | ends both drags |
| `AUTracksPanelMouseWheel` | 0xeabda0 | Shift+wheel moves the track (or the selected tracks) up / down — no height change |
| header hit test | FUN_00ea93a0 | −2 nothing, 0xd1 resize band, 0xd2 / 0xd0 lock strip (unlocked / locked) |
| selection modes | FUN_012e83e0 | 0 add, 1 only, 2 toggle, 3 remove, 4 all, 5 double-click (only ↔ all), 6 none |
| range | FUN_012e8740 | applies mode 0 or 3 to every track from the anchor to the mouse |
| selected-only side effects | FUN_012e3aa0 | (mode 1) instrument track → its channel selected; mixer showing → linked insert current |
| resize of the selected tracks | FUN_012eda20 | equal heights, shared from the first selected track's top |
| copy to selected | FUN_012eafa0 | mask 0x80 size, 0x10 lock, 0x90 both |
| row layout | FUN_00ea0b30 | shown = 16 + round((base − 16) × size) or the locked px; even; 16 … 528 |
| Mixer strip press / move | FUN_012c5800 / FUN_012c5f60 | selection state machine (round 170b) |
| Mixer Shift+click | FUN_012be9b0 | "Rename %d tracks" / name dialog |

## #163 — selection

**Playlist headers (0xea9660)**, left press on the plain area of a header:

| Press | FL |
|---|---|
| click | the track alone (mode 1) |
| Ctrl+click | the track alone (mode 1), and the clip grid's box selection is started from the header with Ctrl (FUN_00e5cd20 with the "from header" flag 0x16baa98; FUN_00e684e0 then spans the whole song): every clip on the track is selected; dragging across the headers adds the tracks crossed (mode 0) and their clips |
| Ctrl+Shift+click | toggles the track (mode 2); the box keeps the clip selection (Shift) and adds the track's clips; a drag selects (track now selected) or deselects (now unselected) the tracks crossed |
| Shift+click | the track alone, then the "Rename, color and icon" dialog (FUN_012e2510; "Rename %d tracks" when several are selected) |
| middle click | the same dialog (on the resize band: reset, below) |
| right-click | the track alone when it was not selected, then the track menu |
| double click | opens the source of the track's clip (or Performance mode launch) |
| the strip at the header's right edge | click only / Shift toggle / Ctrl only + clips / double mode 5 / right-click toggle; drag = range |

Mode 1 also runs FUN_012e3aa0: an instrument track selects its channel; when the mixer window shows (+0xa9), the audio
track's linked mixer track (or the instrument channel's mixer track) becomes the mixer's current track. The playlist
selection does **not** multi-select inserts.

**Mixer (0x12c5800 / 0x12c5f60, constants 0x12c649c = 0x180, 0x12c64a0 = 0x44)**: a plain press makes the track current
(no range); Ctrl (or a double press) + click clears the selection and selects from the anchor to the mouse (alone on a
click); Ctrl+Shift adds the range, or removes it when the press started on a selected track (a click = toggle one);
Ctrl+double = the group. Shift+click without Ctrl opens the rename dialog (FUN_012be9b0). LYNX already did all of this
(round 170b) except the rename; nothing changed in the mixer.

**LYNX before → now**

| | before | now |
|---|---|---|
| Playlist Ctrl+click | toggled the track | FL: the track alone + its clips; Ctrl+drag adds tracks and clips |
| Playlist Ctrl+Shift+click | (= Ctrl+click) | FL: adds / removes the track, adds its clips; drag adds / removes the tracks crossed |
| Playlist right-click on an unselected track | menu only | FL: selects it alone first |
| Playlist click on an audio / instrument track | nothing else | FL: instrument channel selected, linked insert current when the mixer shows |
| Playlist / Mixer Shift+click | range | kept (LYNX extra; FL opens Rename — **owner decision**, see below) |
| Mixer Ctrl / Ctrl+Shift | FL (170b) | unchanged |

## #164 — track height

| Rule | FL |
|---|---|
| Where | FUN_00ea93a0: a band at the bottom of the header, inside the row: clamp(height / 4, 4, 14) px × UI scale (18 / 10 px in the other header style, PTR_DAT_015e1438 == 2), left of the LED, not over the icon rect or the group arrow; rows of 6 px or less have none; nothing while the vertical zoom's base row equals the minimum |
| Cursor | hit code 0xd1 = FL's vertical resize cursor; a locked row shows 0xd0 |
| Drag | size = max(0, newHeight − 16) / (base − 16): the row follows the mouse; no upper limit in the drag, the layout keeps 16 … 528 px and even heights |
| Selected | when the dragged track is selected, every selected track gets the same height: (mouse − top of the first selected) / (position of the dragged one in the selection), applied only above 16 px |
| Past the top | at size 0, 32 px (× scale) above the row's top: "Group with above track" flips (undo names "group with above track" / "ungroup from above track"), the size goes back to 1.0, the drag ends |
| Alt+click / middle click on the band | size 1.0, unlocked (−1); copied to the selected tracks (mask 0x90) |
| Double click on the band, or a click on a locked row | "Lock to this size" flips by negating the locked size (−1 → 1 = the smallest row); copied to the selected tracks (mask 0x10) |
| Left strip (x ≤ 10 px) | the same lock flip — **not built** (what FL draws there needs a live look: unknown `unk_e272e0ad…`) |
| Wheel | header Shift+wheel moves tracks; height zoom stays Alt+wheel / Ctrl+middle-drag (already in LYNX) |
| Saved | per track in the .flp 0xEE event: f32 size at byte 13, i32 locked size at byte 17 (round 168f) |

**LYNX before**: a ±3 px line across the boundary (hard to hit between the rounded plates, the likely cause of #164),
Shift = every track, double click = default height, rows 20 … 160 px, Size menu = 38 × factor, the size factor read
from the .flp as 38 × factor and never written.

**LYNX now**: FL's band, cursor, drag, selected-tracks rule, the group gesture, Alt / middle reset, double-click lock;
rows 16 … 528 px and even; FL's law for the Size menu and the .flp (read and written; `polish::heightForFlSize` /
`flSizeForHeight` with LYNX's 38 px default as FL's base row); .lynx keeps px (`playlistTrackHeights`, load limit now
528). LYNX extra kept: Shift+drag on the band = every track.

## Owner decision (recommended option applied)

FL's Shift+click on a Playlist header or a mixer strip opens the rename dialog. LYNX uses Shift+click for a range
selection (rounds 165m / 170b), and #163's reporter relies on it. Recommended and applied: keep Shift+click = range
(LYNX extra); rename stays on F2 / the menu / double-click on the mixer name. Switching to FL's rename is a one-line
change in `trackhdr::planPress` and the mixer's Shift branch.

## Best way?

FL's Ctrl+Shift+click toggle is the same in the Playlist and the Mixer; Ableton (and most DAWs) toggle with
Ctrl/Cmd+click and range with Shift+click. LYNX now has both FL's gestures and Shift = range; an option "Ctrl+click
toggles (Ableton style)" could follow if users ask (#163's reporter expected it).

## Tests

`tests/Issues190ftTests.inc` (selection modes and press plans, range drag, band and presses, heights, FL size law and
the .flp size field) and `LYNX --hdrshot190ft <prefix>` (synthetic mouse events on the real Playlist headers and mixer
strips, the .lynx JSON round trip and the exported .flp; `<prefix>-checks.txt`).
