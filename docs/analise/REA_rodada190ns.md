# REA round 190ns — linked audio track and mixer track names / colours (issue #235)

Target: FL Studio 2026 `FLEngine_x64.dll` (sha256 `6d9c5ab0…8e02`), Ghidra 12.1.4 through REA, snapshot
`/mnt/e/lynx-work/win190ev/flengine.snapshot`. Clean-room: behaviour only, no code or data copied.

## Playlist track → mixer track

- `FUN_012e60f0 (arrangement, track, name, colour)` — the Playlist track's "Rename / color" result: sets the track's
  name (`FUN_012e5da0`) and colour (`FUN_012e5dc0`), then calls `FUN_012e6010` for the track, then repaints.
- `FUN_012e6010 (track context)` — only when the track record's mode (+0xd8) is 1 (**audio track**) and its mixer
  track (+0xdc) is > 0: the mixer track's colour becomes the track's colour (+0x2c; FL's default track colour with the
  "own colour" flag +0x70 clear maps to the mixer's default 0x716c63), its icon (+0x14) the track's icon (+0x30), then
  `FUN_012be6f0 (mixer track, track name, notify)`.
- Instrument tracks are not synced by this path.

## Mixer track → Playlist track

- `FUN_012be6f0 (mixer track, name, notify)` — sets the mixer track's name (+0xc), then for every arrangement
  (`FUN_012f9f10` count, `FUN_012f9f00` item) looks up the Playlist track linked to that mixer track
  (`FUN_012a7810`: the track list at +0x21c7c whose +0xdc equals it) and copies the mixer track's name (track +0x24),
  icon (+0x30) and colour (+0x2c and +0x64; the mixer default 0x716c63 maps back to the default track colour). 23
  callers (every mixer rename, recolour, group, routing, the Playlist path above).

## LYNX

`core/PlaylistOps.h` `trackmodes::linkedNames` / `followLinkedNames`, run by `AudioEngine::editSong`: after any edit,
whichever side of an audio track ⇄ mixer track pair changed (name or colour) is copied to the other; an edit that set
both (a new link) is left alone. LYNX's own "Link Playlist tracks to mixer tracks" switch keeps syncing instrument
tracks too (`autolink::followNewContent`), and on open matches a linked pair where one side has the default colour
(`autolink::matchLinkedColours`). Icons: LYNX's mixer tracks have no icon (the Playlist track's FL icon is kept and saved), so only names and colours are synced.
Tests: `issues190ns_*` (tests/Issues190nsTests.inc), `--issuesshot190ns`.
