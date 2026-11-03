# Round 168f — Playlist track tools (v0.1.23)

Scope: rows M5–M8 of `REA_rodada168c-gaps.md` (track icons excluded: round 168h). Every item below was read from
FL Studio 2026's own binaries (FL64.exe / FLEngine_x64.dll, Ghidra headless on the `r167g` project; scripts
`tmp/r168c/scripts/Co.java`, `Ins.java`; outputs `tmp/r168f/e1–e8.txt`, `m1.txt`, `dump1–3.txt`, `co19.txt`,
`co3d.txt`) and from FL's own `.flp` files (FL Data corpus, 44 085 playlist track records). Clean-room: behaviour
documented, no code or art copied.

## FL's playlist track record

0x114 bytes per track (`imul 0x45*4`). Fields used here: +0 name, +8 colour, +0xc icon, +0x10 size (f32 factor),
+0x14 locked size (i32: > 0 = locked at that many px, < 0 = unlocked, −1 default), +0x18 enabled, +0x19 lock to
content (u8), +0x1a grouped, +0xb4 mode, +0xc0 / +0xe4 expanded (group header).

In the `.flp` the 0xEE "playlist track" event carries the same: i32 at byte 17 = locked size (corpus: −1 ×37 422,
−16 ×6 074, positive ×58), u8 at byte 21 = lock to content (1 ×194, almost always with icon 92/93).

## Items

| Item | FL (evidence) | LYNX before | LYNX now |
|---|---|---|---|
| Track menu › **Lock to content** | `EEPLTrackClipLockMenuClick` 0x12236c0, undo "lock/unlock to content" → FUN_012e7a90 toggles +0x19; on: icon 0 → 93 and auto name when the track has no name (FUN_012ef490); off: icon 93 → 0; mask 4 of FUN_012eafa0 applies the menu track's new state to every selected track. Paint loop FUN_00e5cd20 → FUN_01220fe0: painting on a locked track whose clips don't use the brush's source copies the track's most frequent clip template (0x54 B) and puts it at the click | missing | same toggle, selected tracks follow, auto name; painting on a locked track paints its dominant clip (`pltracks::applyLockToContent`). Saved (`playlistTrackLocks`), imported from `.flp` (byte 21). Icon 0 ↔ 93 set on the track (drawn by 168h's icon work) |
| Track menu › **Lock to this size** | `EEPLTrackLockMenuClick` 0x1228cb0: +0x14 < 1 → +0x14 = shown height (+0x94), else negated (unlock keeps the value). Size submenu `EEPLTrackSize1MenuClick` 0x1228f10 sets +0x10 from {0.333, 0.5, 1, 1.5, 2} and unlocks (−abs); reset FUN_012e6760 → −1 | missing | `lockedHeight` per track; a locked row keeps its pixel height whatever the vertical zoom; the Size menu / track resize unlocks. Saved and imported (byte 17) |
| Track menu › **Auto name** | FUN_012233c0 → FUN_012ef490: name and colour of the source most clips on the track play | missing | `pltracks::autoName` (pattern / audio channel / automation) |
| Track menu › **Send pattern instances here** | 0x1228ea0 → FUN_012db4e0(−2, …, track): with a selection the selected pattern clips, else every clip of the current pattern, move to the track (positions kept); a nameless track is auto-named | missing | `pltracks::sendPatternInstancesHere`, one undo step |
| Tools › **Lock all / Unlock all playlist tracks to content** | `MacroLockAllPLTracks` 0xf1d940 / unlock 0xf1df60 → FUN_011c2190(1/0): "Resize tracks? — Also resize all playlist tracks to their minimum / default size?" Yes / No / Cancel; tracks 1…500 locked / unlocked (icon, auto name); Yes → locked size 1 (min) / −1 and size 1.0 | missing | same dialog and answers (Panel options › Tools and Tools › Macros) |
| Tools › **Reset empty playlist tracks** | 0xf1dcd0 "Reset empty tracks? — Reset all empty playlist tracks in current arrangement? (no undo)" OK / Cancel → FUN_012e33f0: no clips, mode 0, and a name / icon / colour → defaults | missing | same rule; LYNX keeps it undoable (one step) — the only deliberate difference, LYNX's undo covers every song edit |
| Tools › **Collapse / Expand all grouped tracks** | 0x1214390 / 0x1214450: expanded flag of every group header 0 / 1 | missing (one header at a time) | `pltracks::setAllGroupsCollapsed` |
| Tools › **Purge unused audio clips** | 0xf1d9a0 → FUN_011f9a70, undo "purge unused audio clips"; unused = FUN_011f8880: no pattern, no playlist clip in any arrangement, no layer child uses the channel | missing | `pltracks::purgeUnusedAudioClips` (audio clip channels only, all arrangements, notes in patterns count) |
| **Merge pattern clips** Ctrl+G | `EEMergeAction` 0xf35070 → FUN_01302d30: mixed kinds → "Can't merge / The selection contains different clip types, can't merge."; no selection = all clips → FUN_012dc060: < 2 → "Could not find enough valid pattern clips or instances to merge."; new pattern after the lowest one involved, each clip's visible window copied in, one clip at the first position, undo "merge pattern clips" | Ctrl+G free | same rules and messages; the merged pattern is named after the first clip's pattern, made unique |
| **Make unique** Ctrl+Shift+V | `EEMakeUniqueAction` 0xf34f30 → FUN_0123d640: ≥ 20 selected → "Make all selected clips unique? — Are you sure you want to make all %d selected clips unique?" | menu item without key | key + the ≥ 20 confirmation |
| **Clone selected playlist track(s)** Ctrl+Shift+B | `EEClonePLTracksAction` 0xf351b0 → FUN_012edbe0: "Clone — What do you want to clone along with the track?" Patterns ✓, Audio clips ✓, Automations ✓, Effects ✓, Mixer track routing, Grouped tracks (✓ when a group header is selected); all empty → "Nothing to clone"; > 250 → error; > 4 → "This may take a while" confirm. FUN_012ee550 / FUN_012f1280: clones under the source, unique name via FUN_012f2850 ("Name 2"; empty stays empty), audio tracks get a new mixer track, instrument tracks a cloned channel | quick "Clone" (clips only) | dialog in LYNX style with the same checkboxes, limits and messages (`pltracks::cloneTracks`) |
| **Create variants when stretching audio clips** + STRETCH | `CreateVariantsMenuClick` 0xe3a090 flips global 0x15452e5 (initial 1). FUN_00e66eb0: stretching an audio clip makes a variant (per-clip stretch) when the flag is on and ≥ 2 clips use the channel (FUN_00e66e00) or the clip is already a variant (FUN_01065000); otherwise the channel's timing changes. Toolbar `ClipStretchBtn` "Shift+M Stretch audio when resizing audio clips" | no stretch-on-resize gesture | STRETCH toggle (Shift+M, button next to Follow) — resizing / left-trimming an audio clip stretches it; Create variants toggle (saved in ui-options) chooses clip-only vs channel-wide, FL's rule |
| **Zoom on performance clips** Shift+6 | `EEZoomOnPerformanceClipsAction` 0xf34df0 (key table, `r167j/sc-fl.txt`) | action without key | Shift+6 |

## Not done here

Auto name *clips*, Change icon (168h), Ctrl+Shift+G / Ctrl+Alt+G (merge similar / automation clips), the
"Dropping on track header locks to content" option (FL global DAT_0152c384), "Switch audio clip stretch modes"
macros, the M9 view items.

## Tests

`tests/Playlist168fTests.inc`, 10 tests `pl168f_*`: lock to content paints the dominant clip, lock all + the resize
answer, lock to this size, send pattern instances here, reset empty tracks + groups, purge unused audio clips, merge
pattern clips, clone tracks, stretch with / without variants, keys + `.flp` import.

## Windows GUI check (2026-11-03, real mouse, fresh profile, test mode)

On the demo project (`/mnt/e/lynx-work/playlist168f/g0–g17.png`): the track menu shows Auto name, Lock to this size,
Lock to content, Send pattern instances here, Clone...; with Lock to content on Drums, painting the Hook brush on
Drums painted a Beat clip; Ctrl+Shift+B opened the Clone dialog (Patterns, Audio clips, Automations, Effects ticked;
Mixer track routing, Grouped tracks off) and OK made "Drums 2" with its clips; Ctrl+G on everything (automation
included) showed "Can't merge", on one clip "Could not find enough valid pattern clips...", on three Melody clips
made one "Melody #2" clip (new pattern right after Melody); the panel options menu lists Make unique Ctrl+Shift+V,
Merge pattern clips Ctrl+G, Purge, Stretch Shift+M, Create variants (ticked) and the Tools submenu; Lock all asked
"Resize tracks?" Yes / No / Cancel and Yes shrank and locked every row.

## Where I stopped

2026-11-03: merged origin/main again (168j's track `icon` field now exists, so Lock to content also sets / clears FL's
lock icon 93 like FUN_012e7a90); build-local 2 404 631 checks, 0 failures; landing through coord/train.sh.
