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

## Where I stopped

- Done: event survey; main pitch (0x50) implemented with test (not yet built).
- In progress: decompiling FL's project loader FUN_00f46490 (headless, `tmp/r167g/g.sh d2.c 0 f46490`) to confirm
  0x43 (current pattern, saver clamps it to 1…999 from global 0x015e4018), 0x09 (saved as "checkbox +0x49e of form
  +0x770 is off"), 0x0A (checkbox +0x49e of the form at 0x015e13b8 +0x7d8, the form whose +0x7e0/+0x7f0/+0x7f8/+0x800
  hold comments / artist / title / genre = project info) and 0x64 (current arrangement).
- Next: implement those in `src/flp/Flp.cpp` + `src/core/FlpImport.cpp`, tests in `tests/Rea167gTests.inc`, write the
  gap doc, build-local with FL Data, land via land.sh.
