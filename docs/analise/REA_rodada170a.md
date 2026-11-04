# REA round 170a — Channel Rack and pattern workflow (Busy Works Beats' video)

Source: Busy Works Beats, "Making a Beat on the 'Open Source' FL STUDIO..." (2026-11-03, macOS, LYNX 0.1.22/0.1.23),
items #1 #4 #6 #7 #8 #9 of coord/busyworks-0.1.24.md. FL evidence from FL Studio 2026's own binaries (FLEngine_x64.dll
in the round 167g Ghidra project, headless decompilation; DFM resources dumped in round 165) and, where marked
[SHOT], one driven FL session. Clean-room: behaviour only, no code or art.

| # | Reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 | 00:18 "Control X doesn't work on the channel rack" | §1 | Ctrl+X/C/V did nothing in the rack | Ctrl+X/C/V cut / copy / paste the selected channels' steps and notes, as FL |
| 4 | 03:50 "by default we should have every key labeled" | FL labels the Cs (LYNX extra) | Cs only (white keys too when rows ≥ 16 px) | every key named (≥ 8 px rows), on by default; Settings › General + Piano roll menu switch |
| 6 | 09:57 "in FL Studio you can left click and drag ... dropdowns take a long time" | §3 | dropdown; rack selector had arrows + wheel | left-drag up/down and the wheel step the patterns on the toolbar box and the rack selector; a click still opens the list |
| 7 | 12:41 "too close of a color ... dramatically different" | §5 | groups 0x2a2d34 / 0x33363f | see §5 |
| 8 | 13:18, 20:48 "can't see where the playhead is", "the sound lighting up when it hits" | §2 | playhead only in Pattern mode, a 28 % tint | see §2 |
| 9 | 11:43 "right clicking and hitting shortcut key like D" | §4 | no letters | FL's letters in the Channel Rack, Playlist and Piano roll menus |

## 1. Ctrl+X / C / V in the Channel Rack

- [DECOMP 0xf20030 EditCutActionExecute / 0xf1ffe0 EditCopyActionExecute / 0xf20080 EditPasteActionExecute]: when no
  Playlist / Piano roll editor has the focus (FUN_011e9220 false) they call FUN_011e87b0(cut, 0) / FUN_011e8cf0.
  The channel menu's Cut / Copy / Paste (CutMenuClick 0x11e9650, CopyMenuClick 0x11e96a0) call the same functions.
- FUN_011e87b0: first = the first selected channel (FUN_011e82c0); the clipboard holds one slot per channel from
  `first` to the last channel; a channel that is not selected (+0x32c) is marked 0xFF (a gap), a selected one gets its
  step data for the current pattern (0xFE when it has none) and its per-pattern record (the loop point, 0x14 bytes).
  With cut = 1 it clears the selected channels' steps (FUN_012d3000) and deletes their notes (FUN_012def00). An undo
  step is saved.
- FUN_011e8cf0 (paste): from the first selected channel, slot k → channel first + k (stops at the last channel); a gap
  is skipped, a 0xFE slot clears the channel, otherwise its steps are replaced and its notes deleted then the copied
  ones added; the loop record is set from the slot or reset when the slot had none. Undo entry "paste".
- So FL's Ctrl+X cuts the selected channels' steps/notes (not the channels: deleting channels is Alt+Del). LYNX:
  core `copyChannelSteps` / `clearChannelSteps` / `pasteChannelSteps` (RackOps), keys rack.cut / rack.copy /
  rack.paste, the channel menu's Cut / Copy / Paste act on the selection the same way.

## 2. The Channel Rack playhead

- [DFM TStepSeqForm] `PosLEDPanel: TQuickPaintBox`, 8 px high, hint "Playing step", in the rack's bottom panel under
  the steps. [DECOMP 0x1039610 PosLEDPanelPaint]: the strip alternates beat groups and, while a step plays
  (global ≥ 0), fills that step's cell.
- LYNX before: the step was only computed in Pattern mode (`song.mode == PlayMode::pattern`) — in Song mode
  (Busy Works' case) the rack never showed a playhead even while the shown pattern played in the Playlist.
- LYNX now: `transportedit::patternPlayhead` (the Piano roll's rule since round 162): Pattern mode, or a clip of the
  shown pattern playing in Song mode.

## 3. The pattern selector

- [DFM TToolbarForm] `PatSelect: TVectorDigiWheel` (Min 1, Max 999, MoveSpeed 2, Options wwIncMode), hint
  "^?172^^d^^+- ^Pattern selector", OnDblClick → rename (FUN_012d2920), OnGetText → the pattern's name when wide
  enough. A digiwheel: left-drag up/down and the wheel change the value.

## 4. Menu accelerator letters

- [DFM TFruityLoopsMainForm] the channel menu: "&Rename, color and icon...", "&Load sample...", "Cu&t itself",
  "&Insert", "&Clone", "Clone &with links", "&Delete...", "Cu&t", "C&opy", "&Paste", "Fill each &2 / &4 / &8 steps",
  "Advanced &fill...", "Receive &notes from" (› "&Unlock"), "Burn MIDI to" › "&Current pattern" / "&New pattern",
  "&Group selected...". The Playlist track menu: "&Rename, color and icon...", "&Auto name", "&Track mode"
  (› "&Unassigned", "&Audio track", "&Instrument track"), "&Performance settings", "&Size", "&Lock to this size",
  "&Group with above track", "U&ngroup", "C&onsolidate this track" (› "&From track start", "From &song start"),
  "&Insert one", "Cl&one...", "D&elete", "Move &up", "Move &down". [DFM TShortcutsModule] the editors' Edit actions:
  "Cu&t", "&Copy", "&Paste", "D&uplicate", "&Delete", "Delete &all"; Select: "&Deselect", "Select &all",
  "Select one at &random", "Select more at &random", "Select by &color", "Select &odd", "Select &muted",
  "&Invert selection"; the Piano roll's menu: "&File" ("&Open score...", "&Browse scores", "&Save score as...",
  "&Import MIDI file...", "&Export as MIDI file...", "&Paste from MIDI clipboard...", "&Copy to MIDI clipboard..."),
  "&Edit", "&Tools", "&View", "&Select".
- LYNX: core/MenuMnemonic.h marks the letter (U+0001) in the translated caption (a translation without the letter
  gets " (D)"), the LookAndFeel underlines it, JUCE fix 12 (cmake/LynxPatchJuce.cmake) picks the item on the key:
  one item runs (a submenu opens), several items with the key are highlighted in turn.

## 5. Step colours

(see the driven session below)

## Where I stopped

- done: §1, §2 (Song mode), §3 (drag + wheel), §4, note names; tests tests/Rack170aTests.inc.
- in progress: FL session (shots), playhead/step look, beat-group colours, Windows screenshots.
