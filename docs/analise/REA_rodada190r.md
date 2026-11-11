# REA round 190r: Slicex region keys and layering against FL, the editor, the "add channel" entry

Continues `REA_rodada190h.md` (state, regions, key map, voice engine) and `REA_rodada190l.md` (the channel, FL's renders
of edited states). Clean-room: FL Studio 2026 was run, its windows were photographed, its files read and its renders
measured; no Image-Line code, bitmaps or fonts.

## 1. Region keys: what FL writes [EXEC]

In FL's own Slicex (project `Slicex_vel64` of round 170i2, deck A = "DL Breaker", 22 regions): right click on the
keyboard's C#5 → "Assign current region to this key" (region 1), then save. The deck's embedded WAV grew by 36 bytes:

```
… cue (532) · LIST adtl (1016) · tlst (28) · LIST INFO (36)
74 6c 73 74  1c 00 00 00   01 00 00 00   63 75 65 20  00 00 00 00  01 00 00 00  ff 3d 90 00  00 00 00 00  00 00 00 00
"tlst"       size 28       count 1       "cue "       cue id − 1   1            FF key 90 00 0            0
```

So the trigger list is **a chunk of its own**, placed after LIST/adtl, not a LIST with the type `tlst` (what
REA_rodada190h §3.1 read from the writer and round 190l built by hand — FL ignored that one, which is why its 13
`key_*` renders were identical to the unedited project). The entry layout of 190h holds: 24 bytes, the cue id minus
one, the key in byte 13. `slicex::wavRegions` now reads the chunk where FL puts it (and no longer a LIST-wrapped one);
`slicex::withRegionKeys` writes it (the editor's "Assign current region to this key").

Side observations from the same session: with "Dump deck A to piano roll after editing" on, the assignment re-dumped
the score (22 notes: region 1 and region 2 both on C#5 = 61, then 62, 63 …: an automatic region keeps `base + index`
even when a keyed region sits on that key); the keyboard no longer lights C5; the region shows "C#5" in its lower
left corner.

## 2. Layering against FL's renders [EXEC]

17 projects (`p/sxmake`, /mnt/e/lynx-work/slicex190r/r190r): regions 1, 2 on key 60 and regions 3, 4, 5 on key 61
through the trigger list as FL writes it, 10 notes of velocity 64 on keys 60…69, rendered by FL (`/R`, 16-bit) and
compared with the engine alone (`p/flcmp`, least-squares gain, residual relative to FL). FL's renders now differ from
the unedited project's and from each other, i.e. FL layers.

| layering mode | crossfade | residual | worst sample |
|---|---|---|---|
| 0 All | – | −74.8 dB | 1 LSB |
| 1 All (gain-compensated) | – | −71.0 | 1 LSB |
| 2 Velocity-mapped | none / linear / balanced / unbalanced / adding | −70.7 / −70.9 / −70.9 / −72.5 / −74.0 | 1 LSB |
| 3 Modulation X-mapped | none (Mod X 40), linear (150), adding (300) | −71.5 / −70.9 / −73.8 | 1 LSB |
| 4 Modulation Y-mapped | balanced (Mod Y 400) | −71.2 | 1 LSB |
| 6 Cycle | none, linear | −71.5 / −71.5 | 1 LSB |
| 7 Song time-synced | none, linear, unbalanced | −71.5 / −71.4 / −71.4 | 1 LSB |
| 5 Random | none | −7.8 | (FL's random numbers are its own: not comparable sample by sample) |

−71 dB with a worst sample of 3·10⁻⁵ is the 16-bit file's quantisation (as in 190l). Every deterministic mode,
including mode 7 as implemented in 190l from the REA reading and every crossfade shape, is FL's. No engine change was
needed; the only fix is where the keys are read from.

The project FL itself saved (22 dumped notes at tick positions that fall between samples) matches at −80 dB on most
notes, with −65 dB on four of them and −35 dB in the last 20 ms. Both are outside Slicex: where the host puts a note
that starts between two samples, and what the host does with the last note at the end of a render. Open (the rack
path, not the plugin).
