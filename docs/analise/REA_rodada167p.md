# Round 167p (polish167p): MIDI Out editor polish (#17) and Channel Rack submenus (#18)

Evidence: [SHOT] LYNX before = `/mnt/e/lynx-work/shot167q/*.png` (main 5d3f5c17), LYNX after =
`/mnt/e/lynx-work/shot167p/*.png`; FL's MIDI Out = the issue's screenshot (568 x 420, FL 2026) and [DFM] FL's forms in
`MIDI Out_x64.dll` (`TSynthEditorForm`, `TCtrlSetForm`, round 167i: `~/.claude/jobs/f8789b49/tmp/midiout167/dfm`);
[DECOMP] FUN_006ff060 (knob labels, REA_rodada167i §1.4). [TEST] `tests/Polish167pTests.inc`.

## 1. MIDI Out editor (issue #17)

| item | reporter / brief | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| cog | "gear on each knob's top-right, as in FL; the label must never replace it" | [SHOT] FL: cog at each knob's top-right; [DECOMP FUN_006ff060] unassigned = cog | cog UNDER the knob, in the label's place; a configured knob lost its cog (its name replaced it) | cog at the top-right of every knob (`midiout::view::layout`), always opens *Configure...*; hover = accent + hand cursor + hint |
| name | click toggles on/off | [DECOMP FUN_006ffa10] label click (de)activates; bold when active | name in the cog's place | name under the knob; bold white when sending, grey when off; click toggles (assigned only) |
| looks | configured knob must look different | FL draws assigned / active knobs differently (bold label) | every knob drew "0" and the same grey arc | three looks (`midiout::view::look`): unassigned = dimmed, no arc, no value, no name; off = grey arc + pointer, grey value; sending = accent arc + pointer, bold value |
| grid | more vertical space, label never near the next row | [SHOT] FL rows ~80 px apart | 88 px rows, label 2 px above the next knob | 100 px rows, knob 64 px, name 18 px, ≥ 8 px gap ([TEST] `polish167p_midiout_knob_cell`) |
| page row | aligned with Channel / Patch | [SHOT] FL: Page under Channel / Patch | Page combo at its own x | "Page" label in the Channel column, combo exactly over the Patch column, pencil after it |
| margins | no big empty side margins | FL's form is 558 x 389 | 560 px page centred in the 800 px channel window (Misc tab needs 800) → 120 px empty each side | the editor uses the window's 800 px; the title line ("MIDI Out", already the window title) is gone, Presets / Options moved onto the page row → the window is 30 px shorter |
| bottom | | [SHOT] FL bottom bar: Reset, Deactivate all, Send pan…, Map note color | toggles on their own row | Reset, Deactivate all and both toggles on one row (FL's bar); LYNX's own Send to a plugin / Automate / "Plays:" on the next |
| Configure | consistent −/+ sizes, no empty gap, dialog behaviour | [DFM TCtrlSetForm] compact 238 x 286 form | Controller's −/+ 72 px wide, Range's 15 px; fixed 360 x 300 with an empty band above the buttons; launched without `keepAboveMainWindow` | every number field 116 px (same −/+), Range "min – max", window height from its rows (290 px, no gap); launched like Export / Customize toolbar (`keepAboveMainWindow`: modal, above the LYNX windows) |
| title | "MIDI Out (Insert 6)" | [EXEC round 149] FL names a channel window "<channel name> (<mixer track>)" | — | unchanged: it IS the channel's name + its insert, like every other channel window (rename the channel → the title follows). Checked, nothing to change. |
| preset arrows | not here (167o) | | | room left free right of *Presets* (56 px) for 167o's ◀ ▶ |

## 2. Submenus of two-column menus (issue #18)

- Reporter: "I want the drop-down list to appear near the item to which it is linked, now you can select something in
  the list either with a lot of luck". 0.1.19–0.1.21 (round 162) moved a submenu beside the whole menu, but kept JUCE's
  vertical rule: an item in the lower half of the screen got a submenu growing UPWARDS, bottom-aligned with it — far
  from the item. 167i went back to JUCE's own placement (beside the item), which paints the submenu OVER the menu's
  right column ([SHOT] `shot167q/rack_ctx_insert_submenu.png`, `rack_ctx_replace_submenu.png`).
- Now (`core/MenuPlacement.h`, used by `LookAndFeel::preparePopupMenuWindow`, so every LYNX menu: Channel Rack,
  Playlist, Mixer, Piano Roll, menu bar…): beside the parent menu's OUTER edge, its first item level with the item that
  opened it, growing downwards; moved up only by what the screen needs; to the left when the right has no room.
  The item is the one under the mouse (hover/click) or the one JUCE placed it beside (keyboard → right arrow).
- Crossing the right column: JUCE keeps the open submenu only while the mouse stays in a thin triangle towards it, so
  one pixel of drift on the way highlighted a right-column item and closed the submenu (why 0.1.19 felt like "luck").
  JUCE patch "submenu corridor" (`cmake/LynxPatchJuce.cmake` #5, applied to the fetched JUCE and to the Windows build's
  copy): the band of the item's row (± one row), between the item and the submenu, while the mouse moves towards it,
  also counts as "moving towards the submenu". Resting there for JUCE's 350 ms still selects the item under the mouse.
- [TEST] `polish167p_submenu_beside_outer_edge` (outer edge, row-aligned, bottom of the screen, no room on the right,
  taller than the screen, second monitor at negative x).

## Where I stopped
- Done: code (Theme.cpp, MenuPlacement.h, LynxPatchJuce.cmake, MidiOutEditor.cpp, MidiOutView.h), tests.
- In progress: build-local + Windows build, Windows GUI check (after shots in /mnt/e/lynx-work/shot167p/).
- Next: look at the shots, fix, land via `flock ~/projetos/lynx/.git/lynx-merge.lock ~/.claude/jobs/f8789b49/tmp/coord/land.sh polish167p`.
- Rerun: `OUT=build-win ~/.claude/jobs/f8789b49/tmp/coord/winbuild.sh scripts/build-windows-zig.sh` (zig cache
  ~/.cache/zig-polish167p), GUI script /mnt/e/lynx-work/shot167p/run.sh.
