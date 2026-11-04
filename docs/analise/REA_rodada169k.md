# REA round 169k (v0.1.24) — proactive FL Studio 2026 gap-hunt

Date: 2026-11-03. Clean-room: FL Studio 2026's own `FLEngine_x64.dll` only (the round 167g Ghidra project, read-only,
headless `Dec.java`; scripts and outputs in `~/.claude/jobs/f8789b49/tmp/r169k/`: `render.c`, `picker.c`, `acts.c`),
the DFM dump of round 167j (`/mnt/e/lynx-work/r167j/dfm-all.txt`) and the strings of the DLL. Behaviour only; no FL
code, art, preset or sample is copied. FL Studio was not launched for the ranking.

Markers: **[DECOMP addr]** Ghidra pseudocode, **[DFM]** form resource, **[STR]** string in the DLL, **[CODE]** LYNX
source (grep of origin/main c50aeb49).

Not in scope (other running rounds): Online Sessions (169j), Ogun/Autogun/Toxic/FL Slayer editors (169h), Drumaxx kits
(169e), stuck notes / note-offs (169c), typing keyboard layouts / Mute removed steps / Piano roll play button (169f),
crash reports / Transistor Bass / Sytrus texts (169g).

## 1. Ranked gap list

Impact: H = a daily FL workflow or a user request, M = common action, L = rare. Effort: S < ½ day, M ≈ 1 day,
L > 1 day. Rank = impact × 1/effort, user requests first among equals.

| # | Gap | FL evidence | LYNX now [CODE] | Impact | Effort | Plan |
|---|---|---|---|---|---|---|
| 1 | **Render several patterns at once** from the Playlist picker (Quick render / Render as audio clip / Render and replace on the picked patterns) | §2.1 [DECOMP 0x11884b0 `PSPatQuickRenderMenuClick`] | current pattern only (`renderPatternAsAudioClip`, REA_rodada168e §3.2) | M | M | **this round** |
| 2 | **Picker multi-selection** (click / Ctrl+click / Shift+click / right-click keeps the selection) — needed by #1 | §2.2 [DECOMP 0xe4d5d0 `PickerPanelMouseDown`, 0xe4e080 `PickerPanelMouseUp`] | single click only (`PatternPicker`) | M | S | **this round** (with #1) |
| 3 | **Plugin picker as FL's full view** (§2.5) (the "tab scroll" a user asked for): every plugin of the Plugin database as a tile, grouped by category | View › Plugin picker, `ShowPluginPickerAction` → FUN_00ebf510(0, -1, -10) [DECOMP 0xf1c240]; Channel menu "Open plugin picker" → same call with the channel kind [DECOMP 0x11ffcb0]; [STR] "To make a favorite plugin in the Plugin database, Plugin picker & Plugin list menus…" | a dialog with a category column and a list (`ui/PluginPicker.cpp`, round 151) | H (request) | M–L | layout needs one planned FL session (the picker is drawn at run time, no DFM: `TPlugListForm` is empty [DFM]) → §3 |
| 4 | **Step edit** (Options, Ctrl+E): with the transport stopped, played notes are typed into the Piano roll at the cursor, which advances by the snap | `OptionsStepEditAction` "Step edit" ShortCut 16453 = Ctrl+E [DFM]; handler passes the checked state to the editor object (vtable +0xb0) [DECOMP 0xf1e740]; toolbar `StepEditBtn` [DFM] | none | M–H | M | next (Piano roll owner after 169f lands) |
| 5 | **Multilink to controllers** (Ctrl+J): move several parameters, then several controls, they link in order | `OptionsToggleMultiLinkAction` ShortCut 16458 = Ctrl+J, toolbar `MultiLinkBtn` [DFM] | "Link to controller" one at a time | M–H (controller owners) | M | next |
| 6 | **Tools › Cancel recording**: drops this pass's recorded notes / automation / audio takes, turns record off, keeps the arming | §2.3 [DECOMP 0xf1d7e0] | none | M | S–M | this round if time |
| 7 | **View › Align all channel editors** (Shift+F12): every channel editor takes the focused one's position | §2.4 [DECOMP 0xf18a80 → 0x12317c0] | none (Ctrl+F12 / F12 / Alt+F12 exist, round 167o) | L | S | this round if time |
| 8 | Options › General: **Restore previous state after solo** (off), **Create automation clips at the playback position** (on), **Monophonic painting in piano roll** (off), **Auto zip empty channels** (off) | `RememberMuteStateBox`, `CreateAutoClipsAtCursorBox` "Checked": true, `PRPaintModeBox`, `AutoZipBox` "Zip empty channels on pattern change" [DFM MIDIForm/GeneralPage/MiscPanel] | none of the four | L–M | S each | later (each needs its handler traced) |
| 9 | FL's own templates in File › New from template (Data/Templates: 35 .flp) | REA_rodada168c F1 [FILES] | LYNX templates only | M | S–M | later |
| 10 | **Enable groups** (Alt+Shift+G) | `OptionsEnableGroupsAction` "Checked": true, ShortCut 41031 [DFM] | none | L–M | M | later |
| 11 | **Blend recorded notes** key (Ctrl+B) / **Loop record** toolbar toggle | `OptionsBlendRecordedNotesAction` 16450 = Ctrl+B, `OptionsLoopRecordAction` [DFM] | overdub / replace and loop recording exist in the record menu, no keys or toolbar toggles | L–M | S | later |
| 12 | **32-bit VST2 plugins** (FL: `System/Tools/Bridge/32bit/ilbridge.exe`) | REA_rodada68 / 79 [FILES, DECOMP ilbridge] | 64-bit only; LYNX's sandbox (`engine/PluginSandbox`) runs plugins in a child LYNX.exe | M (old plugins) | L | **needs user decision** (§4) |
| 13 | Share link for projects through api.lynx-share.com | docs/design/SHARE_API.md §2.6 (LYNX-own, no FL equivalent) | server live, app does not use it | M | M–L | Online Sessions area → round 169j / coordinator |
| 14 | "Audio clips" channel group | REA_rodada168t §3 [DECOMP 0xfff0b0] | **done** in 168t (`core/RackFilter.h`, 57cb7802): the rack's filter offers *Audio clips* and *Unsorted*; rendered clips are audio clip channels, so they show there | — | — | nothing to do |

## 2. FL behaviour of the items done this round

### 2.1 Pattern render with several picked patterns [DECOMP 0x11884b0]

The Patterns-menu handler (one for the three items, the item's tag: 0 = Render as audio clip, 1 = Quick, 2 = Render
and replace) has two paths:

- **The menu was opened from the Playlist's picker** (flag +0x6f7 of the picker set by the picker's mouse handler,
  §2.2) **and the picker has a selection** (its list +0x6ac): the flag is cleared, then for every selected entry, in
  selection order: that pattern becomes the current one (FUN_012d8b30); if it is empty it is skipped (and remembered);
  else it is rendered with flags `quick | 0x74`. The first render shows the render window unless Quick; every later one
  is rendered without the window (the "quick" flag is set after the first success). Cancelling the window stops the
  loop. Each result becomes its own audio clip channel (FUN_01188230, as for one pattern; "Render and replace" replaces
  that pattern's clips). One undo step for all: "quick render pattern(s)" (or "replace pattern instances"). Afterwards
  the old current pattern comes back, and if a pattern was skipped: "Pattern is empty, nothing to render." when only
  one was picked, else "Empty patterns were ignored.". FL then plays its "render done" sound (RenderSnd.wv) — LYNX has
  no such sound, left out.
- **Otherwise** (menu from elsewhere, or nothing picked): the current pattern only — what LYNX already did
  (REA_rodada168e §2.1).

### 2.2 Picker selection [DECOMP 0xe4d5d0 / 0xe4e080]

Delphi shift state: bit 0 Shift, bit 2 Ctrl, bit 6 double click; button 0 left, 1 right.

- **Right button down** on an entry: without Ctrl, an entry that is not selected clears the selection first; the entry
  is then selected; the pattern menu opens (flag +0x6f7 = "from the picker"). So right-click on one of several selected
  patterns keeps them all.
- **Left button up** on an entry: no Ctrl and no Shift → only that entry is selected. Ctrl → toggles it (an entry that
  was selected at the press is deselected). Shift with a selection → the range from the first selected entry to the
  clicked one is added (in that order). Clicking still chooses the pencil's pattern as before.

### 2.3 Cancel recording [DECOMP 0xf1d7e0]

Under the recording lock: the three recorded-event buffers are emptied (FUN_012df0d0 ×3: notes, automation, …), every
mixer track's disk-recording take is dropped (FUN_01274280: +0xc = 0, state 2) and each track is re-armed
(FUN_0126c420, state 1), then the transport's record button is released (FUN_011c8f30 → button +0x788 Down := false).
Playback is not stopped.

### 2.4 Align all channel editors [DECOMP 0x12317c0]

The reference is the focused channel editor's channel (or the selected channel, FUN_011e82c0); its editor position
(+0x20 of the window record) is copied to every channel whose window record is not flagged 4 (detached); open editors
are closed and reopened at the new place.

### 2.5 Plugin picker (F8) [FL session + DFM]

One FL session (both locks, FL launched once, closed by its PID; shots `C:\Users\kauan\lynx-test\r169k\p0–p7.png`):
`ShowPluginPickerAction` "Plugin picker" ShortCut 119 = F8 [DFM]; the picker is drawn at run time (`TPlugListForm`
has no controls in its DFM).

- F8 lays a dark veil over the whole main window (FL's own UI dimmed under it) [p1].
- **All** (the start): two halves headed "Generators" (left) and "Effects" (right); every plugin of the Plugin
  database as a tile (its database screenshot; "Audio Clip" / "Automation Clip" as text), tiles flowing in rows [p1].
- A tab strip along the bottom: the generator categories (Controller, Drum, MIDI, Misc, Patcher, Sampler, Synth
  classic, Synth special, Visual), a large **All** in the middle, then the effect categories (Controller, Delay reverb,
  Distortion, Dynamics, Filter, Flanger, Gain, Misc, Patcher, Visual); "..." at both ends [p1].
- **Mouse wheel = one tab per notch**: 3 notches down from All → Distortion [p2], 9 → Patcher [p3]. The shown tab is
  white and bold with a small mark above it; a category shows only its plugins, centred in a row [p2, p3].
- **Pointing at a tab shows it**: moving the mouse onto "All" switched back from Patcher [p4 → p5].
- F8 again closes it [p6].
- Not measured (left as LYNX behaviour): what a click on a tile does (LYNX: adds it, like its quick-add palette),
  dragging tiles.

## 3. LYNX work this round

### 3.1 #1 + #2 Picker multi-selection and multi-pattern render (branch feature/gap169k-patrender)

| | |
|---|---|
| FL evidence | §2.1, §2.2 |
| LYNX before | the picker chose one pattern; the Patterns-menu renders took the current pattern only |
| LYNX now | `core/PickerSelection.h` (FL's press / release rules, the render plan and its two warnings); `PatternPicker` keeps the picked patterns (click, Ctrl+click, Shift+click, right-click keeps them), outlines them in red when several are picked; the pattern menu opened from the picker passes them to `renderPatternsAsAudioClips` (ui/ExportDialog): the window once, then one render after the other with the same choices, empty ones skipped with FL's warning, every result in one undo step, a cancel keeps what was rendered. One pattern = the old path. |
| Tests | `gap169k_picker_selection_like_fl`, `gap169k_render_plan_and_warnings` (tests/Gap169kTests.inc) |
| Not done | FL's render-done sound (RenderSnd.wv); other pattern-menu items that may act on the picker selection were not traced |

### 3.2 #7 Align all channel editors (branch feature/gap169k-align)

| | |
|---|---|
| FL evidence | §2.4; ShortCut 8315 = Shift+F12 [DFM] |
| LYNX before | none |
| LYNX now | View menu + Shift+F12 (`view.alignChannelEditors`): every open Channel settings window moves to the focused one's place (else the selected channel's, else the first open one); windows opened later come up there too (FL stores the place per channel). Envelope windows are left alone. |
| Tests | `gap169k_align_channel_editors_key` (Shift+F12, no conflicts) |
| Not done | plugin (VST) editor windows of channels are not moved; per-channel window places are not saved in the project |

## 4. Needs user decision

- **32-bit VST2 plugins (bridge).** FL hosts them through `ilbridge.exe` (32-bit). LYNX could build its sandbox worker
  (`--plugin-host`, shared-memory audio, already used for "Make bridged") as a separate 32-bit `LYNX-bridge32.exe`
  (the Windows build already makes a 32-bit `lynx-asio32.dll` with zig, so the toolchain is there), plus a 32-bit scan.
  Pros: old freeware / discontinued VST2 synths work, a common FL-migrant ask. Cons: a second Windows binary to build,
  sign and ship (+~15 MB), cross-bitness IPC structs and editor embedding to verify, 3–5 days; Windows only.

## Where I stopped

- Done: §1 ranking, §2 FL evidence for #1, #2, #6, #7.
- In progress: #1 + #2 on branch feature/gap169k-patrender.
- Next: #6, #7 (feature/gap169k-recview), then the plugin picker layout (#3) with one planned FL session.
- Rerun: `~/.claude/jobs/f8789b49/tmp/r169k/g.sh <out.c> <depth> <hex addr…>` (Ghidra, read-only, under the lock).
