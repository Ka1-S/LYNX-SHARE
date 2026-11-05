# Round 168g: Piano roll items M10–M13 of the 168c gap plan (v0.1.23)

Source: `docs/analise/REA_rodada168c-gaps.md` rows M10–M13. Every FL claim below comes from FL 2026's own binaries
(FLEngine_x64.dll analysed headless in Ghidra, project `/mnt/e/lynx-work/r167g`, decompilations in
`~/.claude/jobs/f8789b49/tmp/r168g/d*.txt`), FL's DFM forms (EEMenu, ShortcutsModule: `tmp/r167l/dfm-all.txt`) and
FL's own files (`Data/Patches/Scores`, the demo projects). Clean-room: behaviour written here, LYNX implemented from
this text. Delphi ShortCut values: 0x2000 Shift, 0x4000 Ctrl, 0x8000 Alt.

Tags: [DFM] form resources, [DECOMP] Ghidra decompilation of FLEngine_x64.dll, [FILES] FL's own data files,
[MEM] global / ini reads.

## M10 Piano roll View helpers

| item | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| Note grid highlights (Alt+B) | [DFM] EEHighlightNotesAction "Note grid highlights", ShortCut 0x8042, hint "Mark special notes (or detected scale)". [MEM] global DAT_0152c358, ini "BlackKeys" (General), default 1. [DECOMP] FUN_00eba400 toggles it; FUN_00e500c0 builds the per-key highlight array (black keys / detected scale masks 0x54A major, 0x652 minor / labelled keys); off: no row highlighting | rows always shaded | Alt+B and View › Note grid highlights toggle the row shading (black keys, scale, labelled keys); on by default, saved in ui-options (`prGridHighlights`) |
| Show chord name for selection | [DFM] EEShowDetectedChordAction (no key). [DECOMP] FUN_00eba970 toggles obj(PTR_DAT_015e1e48)+0xb8; each selection change FUN_00e5a850 → FUN_00d32da0: distinct keys of the selected (flag 0x80) notes, muted (0x20) left out; 2..10 keys → chord name (FUN_00c43c20); when on, a hint at the editor's top-left + (16,16) px, ~2 s (+0xac = max(+0x90, 2000) ms). Off by default (Delphi zero-init) | the toolbar chord readout only | View › Show chord name for selection: a bubble with the chord name at the grid's top-left + 16 px for 2 s on every selection change (2–10 distinct unmuted keys); off by default, saved (`prShowChordName`) |
| Background waveform (Alt+N) | [DFM] EEWaveformAction "Background waveform", ShortCut 0x804E. [DECOMP] flag DAT_016ba824 (bss: off, not saved); FUN_00ead950(on) loads the last path DAT_016ba830; dropping an audio file on the editor (FUN_00e70bf0: FUN_00a65260 ≥ 0) loads it and turns it on (FUN_00eada20 + FUN_00ead950(1)). Paint FUN_00e45a00: from pattern tick 0, width = the sample's length at the song tempo, centred vertically in the grid, amplitude 0.9 × half the height, alpha 0xa0 | none | Alt+N / View › Background waveform; drop an audio file on the Piano roll to load it (peaks read on a thread); drawn from tick 0 at the song tempo, centred, 0.9 × half height, translucent; off at start, not saved |

## M11 Piano roll File

| item | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| Import MIDI (Ctrl+M) / Export MIDI (Ctrl+Shift+M) | [DFM] EEImportMIDIAction 0x404D, EEExportMIDIAction 0x604D | in the More menu, no keys in the Piano roll | File › Import MIDI file... (Ctrl+M), Export as MIDI file... (Ctrl+Shift+M), Piano roll context (the global Ctrl+M metronome stays elsewhere) |
| Paste from MIDI clipboard (Ctrl+Shift+V) / Copy to MIDI clipboard | [DFM] 0x6056 / no key. [DECOMP] FUN_00988b90 RegisterClipboardFormatW(L"Standard MIDI File"); copy FUN_01214510 writes an SMF of the channel's notes (FUN_010d5e70: tempo, channel name) with SetClipboardData; paste FUN_012132c0 → the MIDI import dialog FUN_00e1d090 titled "MIDI from clipboard" | none | the same registered format on Windows (`ui/MidiClipboard`), SMF written by `MidiFileIo::writeNotes` (tempo, track name, muted notes left out), paste goes through the MIDI import dialog |
| Open score / Save score as / Browse scores | [DECOMP] save FUN_0121b330 → dialog "FL Studio score file (*.fsc)\|*.fsc\|MIDI file (*.mid)\|…"; FUN_0121b1b0 keeps the edited channel's notes (positions kept) with channel field 0 (FUN_012dfdb0(list, 0)); saver FUN_00f40840 mode 0x10: FLhd format 0x10, 0xC7 version, 0x41 u16 0, 0xE0 24-byte records. [FILES] 609 factory .fsc (FL 3…20.9): 0xC7, 0x41, 0xE0 (+0x9F, 0x1C, 0x25 in 20.9), flags 0x4000 (+8 slide). Open FUN_00ff84b0 → FUN_00ff85c0 replaces the channel's notes of the pattern; Browse FUN_0123a580 opens browser folder 0x3f9 (Scores) | .fsc read for the tools only | File › Open score... (.fsc / .mid: replaces the channel's notes), Save score as... (.fsc as FL writes it, or .mid), Browse scores (the Browser on FL's scores); dropping .fsc / .mid on the Piano roll opens it |

## M12 Piano roll keys and Edit items

| item | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| Select one / more at random (Shift+R / Shift+M) | [DFM] 0x2052 / 0x204D. [DECOMP] FUN_00eb2510: among the channel's unselected notes, one at random (Tag 0) or count / 2 rounds (Tag 1); adds to the selection | missing | `noteedit::selectRandomMore`, keys + Select menu |
| Select odd (Shift+O) | [DECOMP] FUN_00eb2b70: adds the notes with ((start + step/2) / step) odd, step = PPQ / 4 | missing | `noteedit::selectOddSteps` |
| Stamp none (Shift+N) | [DFM] 0x204E | missing | key + menu item |
| Insert space (Ctrl+Ins) / Slice & insert (Ctrl+Alt+Ins) / Delete space (Ctrl+Del) / Trim | [DECOMP] FUN_00eb35f0 / FUN_00e3b2b0 / FUN_00e3b1a0: need the time selection; insert moves notes starting ≥ from (slice: cut at from first); delete cuts at both ends, removes the inside, moves the rest left; moved notes become selected; the pattern's markers (+0xd84, 0x34 B each) move / go the same way; Trim = delete [to, end] then [0, from], clears the time selection | missing | `noteedit::insertSpace / deleteSpace / trimToSelection`; the Piano roll keeps a time selection (Ctrl+drag on the ruler) |
| Delete all / Insert current controller value (Ctrl+I) | [DECOMP] FUN_00e3d230 all the channel's notes; FUN_00eb38d0 the lane control's value at the time selection's start (else 0) | missing | Edit › Delete all; Ctrl+I on the bottom lane's control |

## M13 Pattern time markers

| item | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| storage | [FILES] the demos' .flp: a pattern's markers are 0x94 (type << 24 \| ticks), 0x21 / 0x22 num/den, 0xCD name between its 0x41 and the next pattern / channel / arrangement section. Edlan & Ella Noël pattern 10 "Halftime": type 11 at 384 "Loop: Bar", type 6 at 1536 "Length"; Sid De La Cruz: type 8 signature changes in patterns 1, 4, 5…; Exoplanet / Ookay / NewStuff: 4/4 at 0 in several patterns | **bug**: every 0x94 went to the Playlist (arrangement 0), so pattern signatures changed the song's bars | `flp::Pattern::markers`, `lynx::Pattern::markers` (import, .lynx save/load, undo label "Pattern markers") |
| Add one (Alt+T) / Add auto (Ctrl+T) / Add time signature change (Shift+Alt+T) | [DECOMP] FUN_0122b0d0 (EEMarkerAddMenuClick, editor list +0xd84): Tag 0 "Marker name" prompt then FUN_00e543b0 type 0; Tag 2: at the time selection's start (FUN_00e4e970) else the click, name "%d/%d", the old signature back at the selection's end. Same items / keys as the Playlist (round 167k) | none | Time markers submenu + ruler right-click (Rename, Delete, Delete all); markers drawn on the ruler; Insert / Delete space move them |

| Note properties compare | [DFM] TPRNotePropForm "Note properties - ": Levels PAN (0..128, def 64), VEL (0..128, def 100), REL (0..128, 64), MOD X / MOD Y (0..255, 128), PITCH (0..240, 120), Slide and Portamento boxes, "Color group / MIDI channel" button (1..16), REPEAT wheel (0..96, def 0, "Note repeat length"); Time: Start time, Duration; buttons Accept, Reset (wheels to defaults) | Note inspector: velocity, pan, release, fine pitch, mod X/Y, slide, porta, time, length, Apply / Revert; no colour, no Reset | + Colour group row (1..16) and Reset (the levels to their defaults); REPEAT not done (LYNX notes have no repeat length yet) |

Not done (left): note REPEAT length, F4 (note colour presets, typing-to-piano layouts);
pattern signature markers do not yet change the Piano roll's bar grid or playback (stored, shown, kept).

## Where I stopped

- State (2026-11-03): all code, tests, docs and 15 languages done on feature/proll168g; merged with origin/main,
  build-local 2399408 checks, 0 failures; queued in the merge train (coord/train.sh proll168g).
- Windows GUI check (2026-11-03, the branch's LYNX.exe, fresh LYNX_PROFILE_DIR, LYNX_TEST_MODE=1, real mouse/keys):
  three notes E5/G5/A#5 drawn with the pencil; Ctrl+box select shows the chord name "Edim" in the toolbar; Alt+B toggles
  the grid highlights; Alt+T asks for a name ("New marker") and draws the marker flag + name in the ruler at the song
  position; right press on the marker opens Rename marker / Delete marker / Time markers; the More menu has File / Edit /
  View / Time markers. Alt+N (background waveform) needs an audio channel: covered by the unit tests only.
- Left (not FL-parity blockers): note REPEAT length, F4 (note colour presets / typing-to-piano layouts), pattern time
  signature markers do not yet change the Piano roll's bar grid.
