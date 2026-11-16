# Round 190ht — Pattern Blocks tools (issue #127, phase 2) and insert effects on linked tracks (issue #154) → 0.1.30

## 1. Issue #127: the other Playlist tools on Pattern Blocks

Phase 1 (PR #132 and its follow-up 471fd25b3) gave the blocks FL 10's Pencil, Brush and Eraser and the right button's
erase. This round adds every other tool.

### Evidence

| # | Fact | Source |
|---|---|---|
| E1 | FL 10 was the last version with Pattern Blocks (opt-in), FL 11 hid them, FL 12 removed them | Image-Line KB ans=120 (round 170f, E1) |
| E2 | FL 2026's FLEngine_x64.dll has only the conversion strings left ("replace pattern instances", "pattern instances into clips"); no block editing code to study | `strings -el` on FLEngine_x64.dll (static, 2026-11-15; round 170f E6) |
| E3 | The reporter's side-by-side test of LYNX 0.1.24 and FL 10, tool by tool (issue #127 body): Mute, Select (lasso), Slice, Zoom, Listen; which tools stay in one row and which cross rows | github.com/Ka1-S/LYNX-SHARE/issues/127 |
| E4 | FL 2026's Playlist tools on clips (the tools FL 10 shared with the blocks): Mute toggles, the first clip decides a sweep; Zoom drag = range; Listen plays while held | LYNX's FL-checked Playlist code (rounds 165z, 189g) |

FL 10 is not installed and is not downloaded from unofficial sources, so E3 is the behaviour evidence; REA cannot
add to it (E2). Where E3 is unclear the choice is marked below and asked in the reply.

### Per tool

| Tool | FL 10 (E3) | LYNX now | Kind |
|---|---|---|---|
| Pencil | press on empty: one block that follows the mouse in its row; press on a block: moves it in its row | as before; **a selected block moves the whole selection**, each block in its own row (all or nothing) | FL 10-exact |
| Brush | press on a block moves it; drag paints | as before; a selected block moves the selection | FL 10-exact |
| Eraser / right button | deletes the blocks under the mouse, any row | as before | FL 10-exact |
| Mute | click / drag mutes the blocks under the mouse in any row; starting on a muted block unmutes | same; the first block crossed decides (the Playlist's MuteSweep); a muted block is drawn faded, plays nothing (its clip is muted in playback / export), becomes a muted clip in "Turn pattern blocks into clips", and is saved in .lynx (third element `1`; older versions skip it) | FL 10-exact |
| Mute, right button | E3 says both "right-click mutes" and "right-click drag deletes" | **deletes** (as every other tool and the Playlist's grammar) | choice, asked in the reply |
| Select | either button; click / box selects blocks it meets in any row; the selection moves with Pencil / Brush | same; Shift adds; a click on a block selects it (Shift toggles), on empty space clears; pressing an unselected block with Pencil / Brush moves it alone and drops the selection | FL 10-exact |
| Slice | either button on a block: destructive. Pattern N keeps the start, a new pattern N+1 gets the rest, N+1 blocks start where every N block ends; a note across the cut is trimmed, not split; a vertical drag slices only the row under the mouse | same. The cut snaps like the Playlist (Alt: free); the row is the one under the mouse at release; the new pattern is named like Clone ("<name> #2"), events and markers are shared out at the cut, a set length is shared out (an automatic one stays automatic), every arrangement's blocks follow, one undo step ("Slice pattern"). Clips of the pattern keep pointing at it | FL 10-exact; naming is LYNX's |
| Zoom | either button; drag zooms to the time range (rows keep their height); click zooms back | same (the click goes back to the view before the drag; with none, it zooms out one step) | FL 10-exact |
| Listen | "does not do much" | as the Playlist's Listen: plays from there while held | Playlist's |
| Slip | (no Slip tool in FL 10) | keeps the first version's paint | unchanged |

Core: `patternblocks::blocksInRange / setMuted / inArea / contains / indexOf / moveGroup / slice` (core/PatternBlocks.h),
`PatternBlock::muted`. UI: ui/PatternBlocksView.cpp. Tests: tests/Issues190htTests.inc; app check: `--issuesshot190ht`.

## 2. Issue #154: insert effects on the track

The request: show a track's insert effects on the track, as in the reporter's REAPER screenshots (a track panel FX
list and a mixer strip FX list).

### Evidence and "Best way?"

- **FL:** the insert chain is shown only in the Mixer's right panel (FL 2026; rounds 104 / 170b). There is no track
  layout with inserts in FL (the reporter's "another track layout" is not in FL 2026's Playlist or its View menu,
  rounds 190w / 190gz). So this is a LYNX extra, off by default, and nothing changes in FL's own panels.
- **REAPER:** the track control panel can list a track's FX by name (theme layouts; the reporter's picture), and the
  mixer strip lists "FX inserts when size permits", top to bottom (Cockos wiki, Mixer Control Panel). Click opens
  the FX, drag reorders / moves to another track, bypass is shown per row.
- **Bitwig / Ableton:** the chain lives in a bottom device panel (LYNX already has it: the Clip-launch layout's
  Device View, round 183a); the track headers carry no list.
- **Studio One / Cubase:** an Inspector beside the tracks lists the selected track's inserts; the console's narrow
  strips list them too.
- **Users:** the request itself ("more intuitive"; "it's that way for many other DAWs"); FL users ask for chain
  visibility without opening the Mixer.
- **Verdict:** REAPER's per-track list is the cleanest fit: LYNX's linked headers (#170) already carry the insert's
  M / S / volume / pan like REAPER's track panel, every linked track shows its own chain at once (an Inspector shows
  one), and it needs no new panel. Default stays FL's.

### Design (LYNX extra)

- Switch: Playlist › Edit › "Show insert effects on linked tracks" (action `playlist.trackInserts`, assignable key),
  Settings › LYNX extras, the Playlist panel menu. A view option (ui-options.txt `trackInserts`), not project data.
- Only while the project's "Link Playlist tracks to mixer tracks" is on and only on linked tracks (an unassigned
  track has no insert of its own). The linked headers grow from 152 to 256 px; the list is a 104 px column at the
  header's right, left of FL's selection strip; M / S / volume / pan move left of it.
- Rows (13 px, as many as the row height allows): the insert's effects in chain order (the Mixer's slots top to
  bottom, empty slots skipped), then "+ Add" while a slot is free. A thin mark shows when there are more rows than
  fit; the wheel scrolls the list.
- Gestures, all through the Mixer slot's own code (an EffectsPanel that is never shown, as the Device View does):
  dot = on / off (bypass), name = open / close the window, right-click = the slot's menu (on "+": the add menu),
  drag = reorder (the Device View's move), Ctrl+drag = copy, drag onto another linked track's list = move there
  (round 190dr's `moveEffectToInsert`), a Browser effect dropped on the list goes in between the rows it lands on.
  Each edit is one undo step.
- Not done: the list on the Mixer's strips (the reporter's second picture). That would change FL's own Mixer panel
  and needs the owner's decision; it is listed in the handoff.

Code: core/TrackInserts.h (layout and hit testing), PlaylistComponent (`trackInserts`, `paintInserts`,
`insertsMouse*`, `insertsDrop`). Tests: tests/Issues190htTests.inc; app check: `--issuesshot190ht`.
