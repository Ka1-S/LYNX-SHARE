# Round 167g — gap list: FL Studio app behaviours and .flp events LYNX lacks

Scope: FL Studio 2026 itself (not its plugins; those are round 167f). Round 165f's technology inventory
(`REA_rodada165f.md` §1–2) already lists the app features by menu; this round looked where that one did not: **what
an FL project carries that LYNX's importer drops**, read from FL's own project saver (FUN_00f40840 in
`FLEngine_x64.dll`, [DECOMP]) and the 165 projects in FL's Data folder [DATA]. Items already owned by round 167a
(automatic crossfades flag, render armed tracks, "Record to", Fruity Send span, Add multiple, pitch knob live,
slides) are not repeated.

Effort: S < ½ day, M ≈ 1 day, L > 1 day. Value: H = audible / visible on every imported project, M = visible on
some, L = cosmetic or rare.

| # | Item | FL evidence | LYNX before | Effort | Value | Status |
|---|---|---|---|---|---|---|
| 1 | **Main pitch** (0x50, i16 cents) | saver writes it from the main pitch knob [DECOMP FUN_00f40840]; FL renders 0 / +300 / −400 → 440.0 / 523.3 / 349.3 Hz [FL render]; −400 in the Electroconductor demos, +695 in LeriQ [DATA] | ignored: those demos played 4 semitones off | S | H | **done** (§1) |
| 2 | **Current pattern** (0x43, u16 1…999) | saver clamps the global to 1…999, the pattern array index [DECOMP]; ≤ pattern count in every demo [DATA] | "the fullest pattern" guess | S | M | **done** (§2) |
| 3 | **PAT / SONG** (0x09, u8) | saver writes "transport form's PatBtn (field 0x770) is up" [DECOMP + field table] — 1 in every demo song, 0 in templates / render tests [DATA] | "SONG if the Playlist has clips" guess | S | M | **done** (§2) |
| 4 | **Open arrangement** (0x64, u16) | saver writes the global after every arrangement [DECOMP]; 1 in the 3 demos with 2 arrangements' second one open [DATA] | "the fullest arrangement" guess | S | M | **done** (§2) |
| 5 | **Project info › Show on opening** (0x0A, u8) | saver writes `ShowInfoBox` of the Project info form (field 0x7d8; its neighbours InfoEdit / URLEdit / AuthorEdit / TitleEdit / GenreEdit are the 0xC3 / 0xC5 / 0xCF / 0xC2 / 0xCE texts) [DECOMP + field table]; 1 in most demo songs [DATA] | LYNX has the option, the import left it off | S | M | **done** (§2) |
| 6 | **Project created / working time** (0xED, 2 × f64 TDateTime) | saver writes a global date and FUN_011d55b0's time [DECOMP]; Project info form field `WorkingTimeLabel` [field table]; dates 2009…2026, times 0…147 h in the demos [DATA] | LYNX has no creation date or working time | M | M | **done** (§3) |
| 7 | Channel-level events 0x14, 0x20, 0x29, 0x30, 0x32, 0x33, 0x45–0x56, 0x83, 0x8B, 0x8F, 0x9B, 0xAA | channel saver FUN_00f3e190 writes them from channel / sample object fields [DECOMP]; the demos mostly keep defaults [DATA] | ignored | M–L | L–M | open (§4 lists the offsets) |
| 8 | Playlist track byte 0x2B (one after each 0xEE; track + 0x4C) | saver [DECOMP]; 1 on 823 of 27 000 tracks [DATA] | ignored | ? | L | open |
| 9 | 0xF2 per arrangement (28 B: index, view position / zoom) | saver [DECOMP] | ignored (LYNX keeps its own view) | S | L | not planned |
| 10 | 0xE2 + 0xE6 (the second link list: 20 B records + names, next to 0xE3) | saver writes list 0 as 0xE2, list 1 as 0xE3 [DECOMP] | 0xE3 read, 0xE2 ignored | M | L | open |
| 11 | 0x12 time-signature denominator (always 4 in the demos) | saver [DECOMP] | LYNX's project signature is "beats per bar" only | M | L | not planned |
| 12 | 0xC6 RTF comments | the RTF twin of 0xC3 [DATA] | the plain 0xC3 is read | — | L | not needed |
| 13 | 0x1D, 0x27, 0x28, 0x26 (bit 0 / bit 1 = two form checkboxes), 0x67 (u16) after the arrangements; 0x23, 0x2C, 0x1E, 0xA7 among the project's settings | saver [DECOMP]: globals, meaning not traced yet | ignored | ? | ? | open |
