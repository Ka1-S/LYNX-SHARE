# REA round 170a — Channel Rack and pattern workflow (Busy Works Beats' video)

Source: Busy Works Beats, "Making a Beat on the 'Open Source' FL STUDIO..." (2026-11-03, macOS, LYNX 0.1.22/0.1.23),
items #1 #4 #6 #7 #8 #9 of coord/busyworks-0.1.24.md. FL evidence from FL Studio 2026's own binaries (FLEngine_x64.dll
in the round 167g Ghidra project, headless decompilation; DFM resources dumped in round 165) and, where marked
[SHOT], one driven FL session. Clean-room: behaviour only, no code or art.

| # | Reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 | 00:18 "Control X doesn't work on the channel rack" | §1 | Ctrl+X/C/V did nothing in the rack | Ctrl+X/C/V cut / copy / paste the selected channels' steps and notes, as FL |
| 4 | 03:50 "by default we should have every key labeled" | FL labels the Cs (LYNX extra) | Cs only (white keys too when rows ≥ 16 px) | every key named (≥ 8 px rows), on by default; Settings › General + Piano roll menu switch |
| 6 | 09:57 "in FL Studio you can left click and drag ... dropdowns take a long time" | §3 | dropdown; rack selector had arrows + wheel | left-drag up / wheel up = next pattern, down = previous, on the toolbar box and the rack selector; past the last one a new pattern is made (one); a click still opens the list (LYNX) |
| 7 | 12:41 "too close of a color ... dramatically different" | §5 | groups 0x2a2d34 / 0x33363f | every other group = the off step tinted 28 % with the accent (0x5e3036 on the default theme), every theme |
| 8 | 13:18, 20:48 "can't see where the playhead is", "the sound lighting up when it hits" | §2 | playhead only in Pattern mode, a 28 % tint | Pattern and Song mode; FL's "Playing step" LED strip under the steps, the hit step glows, the channel's selector flashes on its hit; LYNX extras: a soft playing column under every row, bar marks on the strip (one option) |
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
- [SHOT r170a f3/f5/s1, FL 2026 driven once] the strip lights the playing step in Pattern mode and in Song mode (a
  pattern clip playing); an on-step's own small LED turns red while the playhead is on it; the channel's selector
  strip (between the name and the steps) flashes white as that channel's note hits.
- LYNX look: the strip sits at the top of the "add channel" row (9 px, beat groups alternating, the playing cell in
  the playhead colour); an on-step (or a loop repeat) at
  the playhead is filled white 55 % with a playhead outline; the channel's selector pill flashes white.

## 3. The pattern selector

- [DFM TToolbarForm] `PatSelect: TVectorDigiWheel` (Min 1, Max 999, MoveSpeed 2, Options wwIncMode), hint
  "^?172^^d^^+- ^Pattern selector", OnDblClick → rename (FUN_012d2920), OnGetText → the pattern's name when wide
  enough. A digiwheel: left-drag up/down and the wheel change the value.
- [SHOT r170a p1–p6] wheel up = next pattern (769 → 770), wheel down = previous (stops at 1); drag up = next,
  accelerated (50 px jumped many patterns, creating them up to the value), drag down 10 px = −1. A plain click opened
  no list in FL 2026; LYNX keeps its dropdown on a click (LYNX extra, rules). Past the last pattern FL creates
  patterns; LYNX creates one new empty pattern when the last one is not empty (patterndrag / stepPatternCreating),
  10 px per pattern, no acceleration.

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
  gets " (D)"), the LookAndFeel underlines it, JUCE fix 13 (cmake/LynxPatchJuce.cmake) picks the item on the key:
  the first item with the key in menu order runs (a submenu opens).
- [SHOT r170a m1–m4] FL draws the letter with a dotted underline; 'D' in the channel menu ran "Delete..." at once (its
  confirm dialog); a letter shared by two items ('t': "Cu&t itself", "Cu&t") ran the FIRST one and closed the menu.
  LYNX draws a 1 px solid underline (theme look).

## 5. Step colours

- [DECOMP 0x1039610 PosLEDPanelPaint] and the step buttons alternate per beat group. [SHOT r170a f2, crops c_f2]
  FL's groups: dark grey vs a clearly red / maroon tinted group; on-steps white-ish in grey groups, pink in the red ones.
- LYNX before: 0x2a2d34 / 0x33363f (grey vs slightly lighter grey). Now: themes::derivePalette sets
  `stepOffAlt = mix (stepOff, accent, stepGroupTint = 0.28)` for every palette (light themes and other accents too);
  the default theme's built-in table and theme::colour::stepOffAlt = 0x5e3036. Test rack170a_step_groups_clearly_apart.

## LYNX extras on the playhead (coordinator + user, 2026-11-04)

On top of FL's strip and hits (kept FL-exact), on by default, Settings › General "Playhead column in the Channel
Rack" turns both off (settings key rackPlayheadColumn): (1) a soft column (playhead colour, 20 %) under every row's
step buttons at the playing step, so the time reads on row 12 without looking at the strip; (2) on the strip, each
bar's first step carries a 2 px accent mark (32 / 64-step patterns). FL has neither (its rack shows only the strip).

## LYNX verification (Windows, build cfea95e2, fresh profile, LYNX_TEST_MODE, Demo.lynx)

Shots in C:\Users\kauan\lynx-test\r170a: L1 / c_L4 (Song mode, the Beat clip playing: the strip lit, the playing
column, the snare's hit glowing, its selector flashing white; groups grey / dark red), L8 / c_L8 (the channel menu
with the underlined letters), L9 (D in the menu deleted Clap), L18 (the menu's Cut cleared Hat), M2 (Ctrl+X cut Clap's
steps), M3 (Ctrl+V into Reese Bass), M4 (left-drag up 30 px on the rack's pattern box: Beat → Chords, 3 patterns),
M5 (one wheel notch down: Chords → Bass), c_M6 (every Piano roll key named). Note: synthetic Ctrl chords need the
scan code (in.ps1 ctrlkey2); without it no Ctrl shortcut reaches LYNX (Ctrl+Z neither) — a test-harness artefact.

## Where I stopped

- done: §1–§5, note names, FL session (shots in C:\Users\kauan\lynx-test\r170a), tests tests/Rack170aTests.inc.
- done: LYNX Windows screenshots. In progress: merge train.

## Round 170a2 — beat-group tint and lit-step colours (coordinator + user, 2026-11-05)

- Problem (LYNX shot N7): at 28 % accent the empty steps of groups 2 / 4 were a mid red, the hue of a lit step — they
  read as active steps. Now `stepOffAlt = mix (mix (stepOff, textPrimary, stepGroupLift = 0.08), accent,
  stepGroupTint = 0.12)`: a dull reddish grey (0x4f3c42 on the default theme; a pinkish grey on a light theme), as
  FL's alternate groups (r170a f2: a dull reddish grey, much darker than a lit step). Tests: low saturation (channel
  spread <= 30), far from the lit step, still >= 45 apart from the plain group.
- Lit steps in the channel's colour (user request). [SHOT r170a2 FL 2026, one session, channels given random colours,
  /mnt/e/lynx-work/r170a/tint/fl_*.png] FL's lit steps stay white / pink whatever the channel colour; the Channel rack
  options menu's "Colorful channel controls" (TFruityLoopsMainForm ColorfulChannelControlsMenu, off by default,
  restored off after the test) only colours each channel's mixer-insert box, not the steps. FL has no option for
  coloured steps, so LYNX adds its own: Settings › General "Steps use channel colour", on by default (settings key
  rackStepsChannelColour). `themes::litStepColour` lifts the colour (to white on a dark theme, black on a light one)
  until its WCAG contrast is >= 2.6 against both empty-step greys; a channel without a colour keeps the theme's lit step.
