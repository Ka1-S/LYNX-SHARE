# Round 180c: issue #74, Merge similar pattern clips and Merge automation clips (slice 1)

Read from FL Studio 2026's own `FLEngine_x64.dll` (Ghidra headless on the `r167g` project, outputs in
`/mnt/e/lynx-work/issues180c/d1.c` to `d6.c`). Clean-room: behaviour documented, no code or art copied.

## Where they are in FL

`ShortcutsModule` [DFM]: `EEMergeAction` Ctrl+G, `EEMergeSimAction` Ctrl+Shift+G, `EEMergeAutoClipsAction` Ctrl+Alt+G.
Menu order in the DFM: Merge pattern clips, Merge similar pattern clips, Merge automation clips, then Purge unused audio
clips. LYNX uses the same order and keys.

## Merge similar pattern clips (`0xf35090` → `FUN_00eb40b0` → `FUN_012dc060 (1, 1)`)

The same routine as Ctrl+G, with a flag. It does not pass through `FUN_01302d30`, so other clip kinds in the selection are
skipped rather than refused.

1. Candidates: `(flags & 0xa0) == mask`, with mask `0x80` when something is selected and `0` otherwise. `0x80` is selected
   and `0x20` is muted, so muted clips never take part. Fewer than two: "Could not find enough valid pattern clips or
   instances to merge."
2. They merge into a new pattern after the lowest one involved, and one clip goes at the first position. The row is the
   largest stored row (`local_84`). Stored rows are `500 − track` (`FUN_01306040`), so this is the topmost track.
3. Similar groups (`FUN_012dbb20`, restarted from the first clip after every hit):
   - The anchor is any clip that equals the group's first clip. `FUN_012dbac0` compares the id (pattern), the window
     (+0x18, +0x1c) and the mute bit, and for legacy blocks the row. **The track is not compared.**
   - Every other group clip must be found at the same time distance. All must be found, or the anchor fails.
   - The hits go, and one clip of the new pattern goes at the anchor's time on the topmost track of the hits.

## Merge automation clips (`0xf350b0` → `FUN_00eb40d0` → `FUN_013009f0`)

`FUN_01302d30` (Ctrl+G) also comes here when the whole selection is automation clips (type 5).

Checks, in order:

| Check | FL's message | LYNX |
|---|---|---|
| Fewer than 2 selected | "Please select at least two automation clips." | same |
| Another kind selected | "The selection may only contain automation clips." | same |
| LFO mode (+0x18 → +0x48) | lossy question | no such clip mode in LYNX |
| Window with no point at its start and end (`FUN_013007a0`) | lossy question | same |
| Min / max differ | "The clips don't have all the same min and max values." | LYNX points are always 0..1 |
| Min / max automated (`FUN_01300950`) | "...has its min or max value automated." | not possible in LYNX |
| Overlap | lossy question | same |
| Links differ (`FUN_012e1a80`) | "Target links conflict": Keep links of the first / Merge all / Delete all | same question; see Differences |

Lossless: a new automation channel named `<first clip> - merged`. Points are copied per clip, rebased to the earliest
clip. A gap gets a point with the last value at the next clip's start. The clips go; the old channels stay. The new clip
goes at the earliest time on the first clip's track.

Lossy (`FUN_012ff0d0 (1)`): overlapping clips are cut at the next clip's start, then each clip is sampled per tick.

## Differences from FL, for the coordinator

1. **Merge all links.** An FL clip holds several links; a LYNX `Automation` holds one target. Each other target gets its
   own clip with the same curve, on the same track. The sound is the same, but there is more than one clip.
2. **Lossy tool.** FL opens its events-to-automation tool (`TMEEventsToAutoTool`, `FUN_00dfb410`); Cancel there deletes
   the new channel and changes nothing. LYNX opens its own "Convert to automation" panel at the same step, starting at
   99.8 % rather than the panel's usual 100 % (a step per sample would make thousands of points). The panel isn't modal
   here, so a later edit of those clips cancels the merge.
3. **Legacy pattern blocks** (id `0x60000000`) take part in FL's merges. They do not in LYNX's, as with Ctrl+G since 168f.
4. **A clip past its last point.** LYNX lets a clip run on past its automation's last point (painted clips are 4 bars).
   It only holds a value, so it counts as whole, not as a window missing its end point.

## Found and fixed in Ctrl+G (168f)

The merged clip went on the first clip's track; FL uses the topmost track. Muted clips were merged; FL leaves them out.
A selection of only automation clips was refused; FL sends it to the automation merge.

## Not done

`FUN_01302d30` sends an all-audio selection to `FUN_012fc180` (an audio merge). LYNX's Ctrl+G still answers "different
clip types" there, and the clip menu's "Merge clips" is a different operation. Not in this round's scope; not read in
detail.

## Best way?

- **FL:** as above, REA proof in this file.
- **Ableton:** Live has no equivalent of "find every other instance". Consolidate (Ctrl+J) joins a selection into a new
  clip, per track (`/mnt/e/lynx-work/ableton/STUDY.md` item 7, `research-web.md` row 12); repeats stay
  separate clips.
- **Users:** the command dates from FL 9.5 ([Image-Line, "Merge Similar Clips"](https://www.image-line.com/fl-studio-news/merge-similar-clips)).
  Users report merged clips coming out longer than the selection in Ctrl+G
  ([IL forum, June 2026](https://forum.image-line.com/viewtopic.php?p=2062414)); seen in search results only, not
  reproduced in FL or LYNX. LYNX sets the merged clip's length to the span of the selection.
- **Verdict:** FL-exact default. No alternative proposed. Round 177d's "Merge identical patterns" is a different, complementary
  tool: it merges whole duplicate patterns, whereas this merges groups of clips.
