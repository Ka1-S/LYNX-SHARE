# Round 182e: FL import fixes from issues #110-#113

Read from FL Studio 2026's own binaries (`FLEngine_x64.dll` in the headless Ghidra project `r167g`; outputs in
`/mnt/e/lynx-work/flp182e/dec/`). Clean-room: behaviour documented, no code copied.

## 1. Playlist clip mute (issue #110) [DECOMP]

- FL keeps each playlist item in memory as the record it loads from the file: the post-load fix-up (`FUN_00f55690`)
  works on the loaded items with the file's offsets (`+0x04` base `0x50000000`, `+0x0C` track, `+0x20` clip id from
  `FUN_0105f2e0`, `+0x24` fade, `+0x3C…+0x50` the 80/88 B tail via `FUN_01064e20`).
- The clip mute is **bit `0x20` of byte `+0x13`**, i.e. **bit `0x2000` of the u16 flags at `+0x12`**:
  - `FUN_00e84110`, the playlist's track-header action with the undo name **"flip mute"**, toggles
    `byte[+0x13] ^= 0x20` on every clip of that track;
  - `FUN_0105f3c0` and `FUN_010646c0` are the clip's set-mute helpers (`|= 0x20` / `&= 0xDF`), called from 7 and 5
    places;
  - Merge pattern clips (round 180c) leaves out clips with that bit, and its "similar" test compares it.
- The reporter's FL renders (issue #110: 20/91 and 10/191 clips with the bit) match only with those clips silent.
- LYNX: `flp::PlaylistItem::muted()` → `Clip::muted` for pattern, audio and automation clips. Export to FL writes
  `0x40 | 0x2000` for a muted clip (before: the clip was left out of the file).

### Best way?

- **FL:** a muted clip stays in the playlist, greyed out and silent, and the file keeps it with bit 0x2000 (above).
- **Ableton:** a deactivated clip stays in the arrangement and is saved as `Disabled` (LYNX's .als import/export maps
  it to `Clip::muted`, `AlsImport.cpp` / `AlsExport.cpp`).
- **Users:** the reporter's stacked alternative takes are the usual reason to mute a clip: it keeps the take without
  playing it. Leaving muted clips out of an export to FL (LYNX before) lost those takes.
- **Verdict:** FL-exact. Import and export now keep the clip and its mute.
