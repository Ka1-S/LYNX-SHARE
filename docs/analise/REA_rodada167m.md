# Round 167m — PR #19 (LordVersA): Keyboard Controller in the Channel Rack's generator list; `--selftest` SILENT without FL

Standard: docs/analise/REA_rodada165-issue11c.md (per item: reporter's words | FL evidence | LYNX before | LYNX now).

## 1. PR #19 — "Add Keyboard Controller to the Channel Rack's generator list" (+27/-16, 5 files)

- **Contributor's words:** "Adds **Keyboard Controller** to the Channel Rack's generator list, so it appears in
  Insert ›, Replace › and "+ add channel" (and in the Browser's Plugin database, which reads the same list). Until now
  only the Patcher's Add menu could create one, although rack channels already support it (editor, instrument, `.flp`
  import)." `defaultKeyboardController()` moves unchanged from `PatcherEditor.cpp` to core (`FlSynths.h` /
  `FlSytrus.cpp`); a new test `keyboard_controller_default_state`.
- **FL evidence:**
  - [FILES, FL 2026 install] FL's plugin database puts the two controller generators side by side in one category:
    `Data/Patches/Plugin database/Generators/Controller/` holds exactly `Fruity Envelope Controller.fst` and
    `Fruity Keyboard Controller.fst` (same in `Data/System/Plugin databases/Plugin database (full)/Generators/Controller/`);
    the generator DLL is `Plugins/Fruity/Generators/Fruity Keyboard Controller`, next to `Fruity Envelope Controller`.
  - [DFM, `FLEngine_x64.dll`, REA_rodada167i §2] the channel menu's Insert (0x13bf64f) / Replace (0x13bf7ad) and the
    Channel Rack's "Add one" (`ChanAddMenu`, 0x13d6bff) share one generator list built at run time, so every
    generator FL lists — the Keyboard Controller included — is in all three.
  - [SHOT, REA_rodada162e §1 s1] FL's Add plugin menu shows Generators › Controller: Envelope Controller, Keyboard
    Controller.
- **LYNX before:** the Keyboard Controller channel worked (editor, instrument, `.flp` import) but only the Patcher's
  Add menu could create one; the rack's one list (`quickpick::generatorEntries`, round 167i) had only the Envelope
  Controller.
- **LYNX now:** the contributor's commit 92a8ffd9 kept as it is (his authorship, not squashed): "Keyboard Controller"
  right after "Envelope Controller" in the tools block — FL's Controller category order. Insert ›, Replace › and "+"
  list it; Replace's tick skips controllers as before. The UI string already existed in the 15 languages.
- **Verification:** core suite (below); Windows GUI with the real mouse (shots in `E:\lynx-work\pr19\s\`, see §3).

## 2. `--selftest` → `result=FAILED (native_engines=SILENT 1 (12 made))` on macOS arm64

- **Contributor's words:** "`--selftest` reports `result=FAILED` (`native_engines=SILENT 1 (12 made)`) on macOS. The
  output is identical on unmodified `main`, so this PR doesn't cause it."
- **Diagnosis:** not macOS-specific. GitHub's Tests workflow shows the same line on **both** App jobs (ubuntu-24.04
  and macos-15) since main bf6defb9 / f29400ea (2026-11-01 00:26, `feature/flgap167`: Wave Traveller added); the last
  green main run was f3c2157c. Without FL Studio, `natives::available` lists only the engines that need nothing:
  6 instruments (3x Osc, Harmless, SimSynth, Fruity DX10, Plucked!, **Wave Traveller**) + 6 effects = "12 made".
  Wave Traveller plays the channel's sample, and a new one has none, so its note is silent — expected (FL's Wave
  Traveller is also silent with no sample; the core test `natives142_lists_and_no_fl_installation` already exempts it).
  The self-test only exempted the Granulizer. On Windows with FL installed it passes because the FL-preset path is
  taken (the landing's self-test runs with FL).
- **Fix (`src/Main.cpp`, self-test "native_engines"):** sample-playing engines (Granulizer, Wave Traveller) with no
  sample get a generated one (1 s, 220 Hz, temp WAV deleted after), and then **every** engine must make sound — the
  Granulizer is no longer skipped, so the check got stricter, not looser. A silent engine is now named in the report:
  `SILENT n [names]`.
- **Test:** `natives167m_sample_engines_play_a_sample_without_fl` (no FL access): Wave Traveller and Granulizer are
  silent without a sample and play (rms > 1e-3) with one.
- **CI:** the Tests workflow's App (macos-15) job already builds the app and runs `--selftest` (and so does
  release.yml's macos job); no step missing. It was red for this reason; the fix makes it green again (checked on
  the wip/pr19 run, §3).

## 3. Verification

- [CI] wip/pr19 Tests run 41320958734: **App (ubuntu-24.04) and App (macos-15) green** — both build the app and
  require `result=OK` from `--selftest` without FL; on main they were red since f29400ea.
- [SELFTEST, Windows with FL 2026] `native_engines=OK (41 made)`, `result=OK` (the Granulizer now must play too).
- [GUI, Windows, real mouse, 1920×1080, fresh LYNX_PROFILE_DIR + LYNX_TEST_MODE] shots in `E:\lynx-work\pr19\s\`:
  `b1` "+ add channel": Keyboard Controller right under Envelope Controller; `b2` picking it creates the channel
  and opens its editor with the default ramp, auto release on; `c2` Clap › Insert ›: same list; `c3` the Keyboard
  Controller inserted above Clap, editor open; `d1` Kick › Replace ›: Kick ticked, Keyboard Controller listed;
  `d2` Kick replaced, keeping row 1, its steps and Insert 1; `e1` the rack with the three; `e2` LYNX closed normally
  (Save changes? › Discard), desktop clean.
- [TEST] core suite with FL's data folder: 1,890,599 checks, 0 failures (new: `natives167m_sample_engines_play_a_sample_without_fl`, the PR's `keyboard_controller_default_state`).

## Where I stopped

- Done: PR #19 fetched as feature/pr19 (based on current main), self-test fix + core test committed, wip/pr19 pushed.
- Done too: Windows build, Windows selftest, GUI check (Insert ›, Replace ›, "+"), CI App jobs green on wip/pr19.
- Done: core suite 0 failures.
- Next: land via `flock ~/projetos/lynx/.git/lynx-merge.lock ~/.claude/jobs/f8789b49/tmp/coord/land.sh pr19`.
- Commands: `scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"` (via buildslot),
  `OUT=build-win coord/winbuild.sh scripts/build-windows-zig.sh`.
