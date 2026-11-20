# REA round 190ma — Channel Rack graph editor: Note pitch, Shift, Repeat (issue #208); Plugin picker first open (issue #209)

FL Studio 2026, `FLEngine_x64.dll` (md5 fce4ea557685c0aeeeeb126013066c25, image base 0x400000), headless Ghidra 12.1.4
(read-only copy of round 187a's analysed project). Clean-room: only names, tables, ranges and observed behaviour are
used; no code is copied.

## 1. The graph editor's lanes [DECOMP + DATA]

`FUN_0130ff30` fills the graph editor's tab bar (9 tabs) from three tables, chosen by the available width:

| index | full (0x15d81e8) | middle (built in code) | short (0x15d8278) |
|---|---|---|---|
| 0 | Note pitch | Note pitch | Note |
| 1 | Velocity | Velocity | Vel |
| 2 | Release velocity | Release | Rel |
| 3 | Fine pitch | Fine pitch | Fine |
| 4 | Panning | Pan | Pan |
| 5 | Mod X | Mod X | X |
| 6 | Mod Y | Mod Y | Y |
| 7 | Shift | Shift | Shift |
| 8 | Repeat | Repeats | Rep |

`FUN_01307690` builds the same nine entries (0x15d81e8) as a menu. The plural table at 0x15d8230 ("pitches",
"velocities", "release velocities", "fine pitches", "panning", "mod X", "mod Y", "shifts", "repeats") names the lanes in
FL's messages.

## 2. The hint bar's value per lane [DECOMP]

`FUN_013098e0 (form, value)` writes `^^<value>^<lane name>` (lane names 0x15d8300: Note pitch, Velocity, Release, Fine
pitch, Panning, Mod X, Mod Y, Shift, Repeat), the lane being the form's +0x7c4:

| lane | value shown |
|---|---|
| 0 Note pitch | the key (rounded, 0…131) as a note name |
| 1, 2 Velocity / Release | `%d%%` of value × 100 |
| 3 Fine pitch | `%d cents`: (value − 0.5) × 2400 / 10 |
| 4 Panning | `%d% %s`, (value − 0.5) × 200 and Left / Centre / Right (0x15d82e8) |
| 5, 6 Mod X / Y | `%d%%` of (value − 0.5) × 200 |
| 7 Shift | `0.00` (the part of a step) |
| 8 Repeat | a name from the table at 0x154a138 |

## 3. Repeat = an index into FL's rate table [DATA]

Table at 0x154a138 (15 entries), also FL's Piano roll note properties ("Note repeat length"):

| index | name | LYNX interval (ppq 960) |
|---|---|---|
| 0 | No repeat | — |
| 1 | 1/4 (Beat) | 960 |
| 2 | 1/8 dotted | 720 |
| 3 | 1/4 triplet | 640 |
| 4 | 1/8 | 480 |
| 5 | 1/16 dotted | 360 |
| 6 | 1/8 triplet | 320 |
| 7 | 1/16 (Step) | 240 |
| 8 | 1/32 dotted | 180 |
| 9 | 1/16 triplet | 160 |
| 10 | 1/32 (Half step) | 120 |
| 11 | 1/64 dotted | 90 |
| 12 | 1/32 triplet | 80 |
| 13 | 1/64 (Quarter step) | 60 |
| 14 | 1/64 triplet | 40 |

The .flp note record's byte 0x11 (REA_rodada2 §4.1 `repeats`) holds this index. FL's demos confirm it: hi-hat rolls at
4 (1/8), 10 (1/32), 11 (1/64 dotted), 12 (1/32 triplet) and 13 (1/64) (Mehran Abbasi - With You, Astes - Bien Duro,
Purps On The Beat - Stroll Up). The Note properties DFM's REPEAT wheel range 0…96 (REA_rodada168g) is the DFM default,
not the stored range.

## 4. Shift and Note pitch [DECOMP + DATA]

FL's 24-byte note record has no shift field: a shifted step is a note whose position is past its step's start (the
step it belongs to is the one its position falls in). The hint shows the shift as the part of a step ("0.00"). Note
pitch is the note's key (0…131; C5 = 60).

## 5. Behaviour checks (FL renders, round 190ma) [EXEC]

Test projects written by LYNX's .flp export with the repeat bytes patched (sampler with a 4 ms click, 120 BPM), rendered
with `FL64 /R /Ewav`, onsets measured (work folder /mnt/e/lynx-work/issues208_190ma/rep):

- One note per bar, 24 cases (every index 0…14 on a one-beat note, step notes, 1/8, 1/4, 2 and 4 beats, lengths that
  are not a multiple of the rate): FL strikes at start + k × interval while k × interval < the note's length, every strike
  at the note's velocity. A rate equal to or slower than the note gives one strike. PPQ 96 and PPQ 960 files give the same
  strikes (the byte is an index, not ticks).
- Index 15 (outside FL's table; FL's UI never writes it) plays like index 9 in FL. LYNX imports anything above 14 as no
  repeat.
- 16 step notes with shift 0…23 ticks (PPQ 96), keys 48…78 and repeats 0…14: FL plays each step at its shifted position
  for one step, its repeats inside that step.
- LYNX (renderwav, this branch) on the same files: 23 of 24 bars with the same strike count and onsets within 0.1 ms
  (the 24th is index 15), and the 30 step onsets within 0.07 ms.

FL's graph editor (shots raw/fl-note.png, fl-shift.png, fl-rep.png): Note pitch is a small piano roll (C rows named,
each step a block named with its key); Shift draws bars from the bottom; Repeat draws a block at the rate's height,
1/16 (no second strike in a step) at the bottom, with one line per extra strike in a step; slower rates draw nothing.
The hint bar shows "Repeat 1/32 (Half step)".

## 6. Issue #209: the Plugin picker's first opening [EXEC]

The plugin list ("More plugins...", FL's picker list) asked `isPlugin32Bit` of every scanned VST / VST3 plugin when it
opened (the Bits column and "Combine 32 and 64 bit versions", round 176a). The answer read the first 4 KB of each
plugin's module (a VST3 bundle's x86_64-win module). The first read of a DLL waits for the virus scanner: on the test PC
(486 VST / VST3 modules of the owner's plugin list) 99.3 s for the first pass, 0.45 s for the second (PowerShell,
same reads). The cache only lived in memory, so every LYNX start paid it again at the first opening once Windows had
forgotten the files. Fix: the answers are kept in `plugin-bits.txt`; the list and the Plugin manager use what is known
and read the missing modules on a background thread, then update the Bits column.
