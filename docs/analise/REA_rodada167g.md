# Round 167g (v0.1.22) — REA gap-hunt: FL Studio app behaviours and .flp events

Clean-room study of FL Studio 2026 (build 4720 template / 5654 engine) as a black box and with Ghidra on FL's own
`FLEngine_x64.dll` (our own copy of the analysed project at `/mnt/e/lynx-work/r167g`, scripts in
`tmp/r167g/scripts`: `Dec.java`, `Refs.java`, `Find.java`). Nothing of FL's code or art is copied: behaviour and
formats only. Gap list: `REA_rodada167g-gaps.md`.

Evidence markers: **[DECOMP addr]** Ghidra pseudocode, **[DATA]** FL's own files (the 165 projects under FL's `Data`
folder, surveyed with `tmp/r167g/py/flpctx.py` / `projev.py`), **[FL render]** FL's command-line render
(`FL64.exe /R /Ewav`, both locks, three renders in one go), **[TEST]** LYNX unit tests.

## 0. Method: which .flp events LYNX's importer skips

`flpdump --events` over the 165 projects in FL's Data folder lists 147 event ids; 73 are read by `flp::parseProject`.
Each unread id was classified by where it sits (after which "new channel / pattern / arrangement / insert / track"
event), its values, and — for the project-level ones — by FL's own project saver **FUN_00f40840** [DECOMP], which
writes them in a fixed order: 0xC8 (licensee), 0x9C tempo, **0x43**, **0x09**, 0x0B swing, **0x50**, 0x11, **0x12**,
0x23, 0xD9, 0x17 pan law, 0x2C, 0x1E, **0x0A**, 0xC2 title, 0xCE genre, 0xCF artist, 0xA7, 0xCA, 0xC3 comments, …

## 1. Main pitch (event 0x50)

| | |
|---|---|
| FL evidence | Saver FUN_00f40840 writes 0x50 as a 16-bit value from the main pitch global (PTR 0x015e2c50) right after swing [DECOMP]. Demos: 0 in 162 projects, 65136 (= −400) in both "Electroconductor - Inner Days" projects, 695 in "LeriQ - SHINE" [DATA]. FL renders of three test projects (Empty template + a sampler playing a 440 Hz sine, `tmp/r167g/py/mk167g.py`): 0x50 = 0 → **440.02 Hz**, +300 → **523.26 Hz (+300.0 cents)**, −400 → **349.26 Hz (−399.8 cents)** [FL render, `C:\Users\kauan\lynx-test\r167g\g_pitch*.wav`]. So it is signed cents and FL's export follows it. |
| LYNX before | Ignored: every imported project played at main pitch 0 (the Electroconductor demos 4 semitones sharp of FL). |
| LYNX now | `Project::mainPitch` (i16, project section only) → `Song::masterPitch` (the toolbar's master pitch knob, ±1200 cents), which the built-in instruments with "main pitch" on already follow. Test `rea167g_flp_main_pitch` (values 0/+300/−400/695, a channel-section 0x50 ignored, the Electroconductor demo = −400). |

## 2. The state FL saves for the next opening (0x43, 0x09, 0x64, 0x0A)

FL's loader **FUN_00f46490** [DECOMP] (the event switch; found through the globals the saver writes):

| Event | Saver FUN_00f40840 [DECOMP] | Loader FUN_00f46490 [DECOMP] | Demos [DATA] |
|---|---|---|---|
| 0x43 u16 | the global 0x015e4018, clamped to 1…999 (the index range of the 999-pattern array the saver walks next) | global = value, out of 1…999 → 1; after loading, the transport's `PatSelect` (field 0x7a8) is set to it | 1…74, never above the pattern count |
| 0x09 u8 | 1 when the transport's `PatBtn` (field 0x770 of the transport form, published field table at 0x9c5e22) is **up** | `PatBtn.Down := value ≠ 1` | 1 in every demo song, 0 in templates and render tests |
| 0x64 u16 | the global 0x015e4be0, after every arrangement's 0x63…0xF2 | 0 ≤ value ≤ count − 1 → that arrangement, else the first | 1 in Asher Postman, Eric Kauffmann, Sid De La Cruz (2 arrangements each) |
| 0x0A u8 | `ShowInfoBox` (field 0x7d8) of the Project info form; its neighbours `InfoEdit` 0x7e0, `URLEdit` 0x7e8, `AuthorEdit` 0x7f0, `TitleEdit` 0x7f8, `GenreEdit` 0x800 (table at 0xf5033d) are exactly the edits whose text the saver writes as 0xC3, 0xC5, 0xCF, 0xC2, 0xCE | `ShowInfoBox.Checked := value = 1` | 1 in most demo songs |

LYNX before: the importer guessed — the pattern with the most notes, SONG when the Playlist had clips, the arrangement
with the most clips — and left "Show this page when the project is opened" off.
LYNX now: `flp::Project::currentPattern / songMode / currentArrangement / showInfo` with FL's loader rules (pattern
out of range → 1; 0x09 / 0x0A only "on" for 1; arrangement out of range → the first) → `Song::currentPattern` (FL's
number through the importer's pattern map; an empty saved pattern is not imported, then the old guess), `Song::mode`,
the open arrangement, `ProjectInfo::showOnOpen`; opening an imported project with it on opens Settings › Info, as the
.lynx path already did. Files without the events (older FL) keep the old guesses.
Test `rea167g_flp_saved_state` (FL's rules on built files; "Mehran Abbasi - With You" opens on pattern 46 in SONG with
the info shown; "Asher Postman - Future House" on its second arrangement).

## 3. Project created / working time (0xED)

| | |
|---|---|
| FL evidence | Saver: 0xED = 16 bytes, the f64 at global 0x015dfdc0 and FUN_011d55b0() = `Now − DAT_016bcbe8` (Now = GetLocalTime as a TDateTime, FUN_00442c30) [DECOMP]. Loader case 0xED: copies the 16 bytes, then sets the clock reference to `Now − saved working time` [DECOMP] — so the working time is wall-clock time with the project open, summed over sessions. The Project info form has `WorkingTimeLabel` right after the info edits [field table 0xf5033d]. Demos: creation dates 2009…2026, working times 0.01 h (templates) … 147 h (Astes - Bien Duro); every genre template shares one 2020-04-28 date and the Sacco visualiser demos share their template's date, so FL keeps a template's date for a new project [DATA]. |
| LYNX before | No creation date, no working time. |
| LYNX now | `ProjectInfo::created` (seconds since 1970 UTC; FL's local-time TDateTime converted in this machine's time zone by `flDateTimeToUnix`) and `ProjectInfo::workSeconds`; imported from 0xED, kept in .lynx (`info.created`, `info.workSeconds`). The engine's work clock starts when a project is set (open / import / new) and adds the elapsed wall-clock time before every save (`AudioEngine::stampWorkTime`, called by `capturePluginStates`, which every save path calls first). A new project gets created = now unless its template has a date. Settings › Info shows "Created: 2018-07-13 18:38    Working time: 12:58:47" (updated every second), all 15 languages. Test `rea167g_flp_created_and_working_time`. |

## 5. Play truncated notes in clips (0x1E)

| | |
|---|---|
| FL evidence | Loader case 0x1E: global 0x015e1b18 = value > 0 and the Settings form's `TruncateClipNotesBox` (field 0xaf8) follows it [DECOMP FUN_00f46490]; its caption is "Play truncated notes in clips", hint "Play partial notes in playlist clips" [DFM TMIDIForm]. The saver writes 0x1E **only when it is on** [DECOMP FUN_00f40840]. The flag is passed to the clip's note cutter **FUN_010608a0** (from FUN_012293d0 and the playlist scheduler FUN_012dc060, `flag × 2 | 1`) [DECOMP]: for each note whose position is before the clip's offset — off: the note is dropped; on: length −= offset − position, dropped when < 1 tick, else position = offset; the caller then shifts the kept notes by the offset and drops those past the clip's end. Demos: present (= on) in 156 of the 165 projects, FL's Empty template included [DATA]. |
| LYNX before | Always "off": a pattern clip trimmed or sliced into a held note was silent until the next note. |
| LYNX now | `Song::playTruncatedNotes` (Settings › Project › Playlist › "Play truncated notes in clips…", undoable, saved in .lynx; on for new projects like FL's template; projects saved before keep it off; .flp 0x1E present → on). The scheduler plays such a note from the clip's start with the rest of its length (cut at the clip's end as before). 15 languages. Test `rea167g_play_truncated_notes_in_clips`. |

## Windows check [SHOT]

Branch build, fresh `LYNX_PROFILE_DIR` + `LYNX_TEST_MODE=1`, `g_state_a.flp` (FL's Empty template with 3 patterns, two
arrangements, 0x43 = 2, 0x09 = 1, 0x64 = 1, 0x0A = 1, comments "Info 167g A"; its 0xED is the template's):
`C:\Users\kauan\lynx-test\r167g\shots\lynx_open.png` — opens on Pat2, SONG lit, arrangement Arr2;
test-mode.log "skipped: project info shown on open"; F11 → `lynx_info.png` — Settings › Info with the comments, "Show
this page when the project is opened" ticked, "Created: 2015-01-22 09:52    Working time: 0:03:56" (the template's
3 min 36 s + this session); closed normally, `desktop_after.png` clean.

## 4. Channel events still not read (for a next round)

The channel saver **FUN_00f3e190** [DECOMP] writes, per channel, in this order: 0x40, 0x15, 0x00, (names, plugin),
0xD1 echo, 0x8A (+0x214 / +0x218 << 16), **0x8B** (+0x250 / +0x254 << 16), 0x59, 0x61, **0x30** (+0x224, u8),
**0x45** (+0x228), **0x56** (+0x24c), **0x47** (+0x234), **0x53** (+0x238), **0x4A / 0x4B / 0x4C** (+0x23c / +0x240 /
+0x244), **0x55** (+0x248), **0x83** (+0x22c / +0x230 << 16), **0x46** (bits: +0x25d, +0x25e ×2, +0x25f ×4, +0x25c
×256), 0x68, 0x0F, 0x5E (layer children), **0x32** (+0x1fc, u8), 0xDB, 0xE5, 0xDD, 0xD7, 0x84, 0x90, 0x91, 0xEA,
**0x20** (+0x892 ≠ 0), **0xFB** (a list at +0x784), 0xE4 ×2, 0xDA ×5, then for samplers **0x8F** (sample flags:
+0xbd, +0xf9 ×2, +0xfa ×4, +0x14d ×8, +0xf8 ×256), **0x14** (+0x14c), 0xC4, 0x87, 0x8E, **0xAA** (+0x714, a count;
> 0 → strings 0xF5, 0xF6, 0xF8, 0xF9, 0xFE, 0xFF), **0x33** (bits). Which knob each offset is needs the channel
settings form's handlers traced; the demos mostly hold the defaults (0x45 = 128 in 3434 of 3443 channels, 0x55 =
2048 in 3438, 0x83 = 0x800000 in 3431), the varied ones are 0x8F (2 / 3 / 10 / 11 / 258), 0x4B / 0x4C (≈ 140 / 50
channels), 0x29 / 0x32 (≈ 1 500 / 1 170 channels with 1). Not done this round.

## Where I stopped

- Done (branch feature/flapp167, pushed as wip/flapp167): §1 main pitch, §2 saved state, §3 created / working time,
  with tests in tests/Rea167gTests.inc; gap list REA_rodada167g-gaps.md.
- Windows check done (section above). In progress: build-local with FL Data (tmp/r167g/bl1.log), then land.sh.
- Next: §4 channel events (trace the channel settings form), gap items 8, 10, 13.
- Rerun: `tmp/r167g/g.sh <out.c> 0 <addr>` (headless decompile on /mnt/e/lynx-work/r167g), `python3
  tmp/r167g/py/flpctx.py "<FL Data>" 0x..` (event contexts), `tmp/r167g/flrender.sh` (FL renders — both locks).
