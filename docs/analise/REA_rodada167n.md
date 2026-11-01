# REA round 167n — finishing FL's Transporter (v0.1.22)

Takes over `feature/flgap167-tp` from round 167f's helper (engine, effect, import, layout, i18n and tests were on disk,
uncommitted). The behaviour spec is `REA_rodada167f-transporter.md`; this document covers what 167n added. Evidence
markers as there: [DECOMP] disassembly of the installation's `Transporter_x64.dll`, [EXEC] FL's plug-in DLL run in the
`fxt.exe` harness (no FL desktop session), [TEST] LYNX's tests. FL Studio was not launched in this round.

| item | reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 send track | brief: "wire the engine as an FL effect … .flp import of Transporter state" | §1 | send track mapped to the k-th output route of the insert [HYP] | send track = mixer insert number |
| 2 Update / Flush buttons | 167f §6 "momentary buttons in the editor" | §2 | latched toggles: a second click (1 → 0) did nothing | each click is a press, as FL's push buttons |
| 3 FL presets | brief: "presets" | §3 | loaded through the generic FL-preset path, not checked | the six factory presets bit-identical with FL's plug-in |

## 1. The send track is a mixer insert number
- [DECOMP Transporter_x64.dll Render 0x714700] after the engine call (`0x71488c`), the plug-in reads its send track
  (`+0x1a4`, the state's send-track field: restore `0x713833`); when it is > 0 it calls the host method at vtable
  `+0x128` with (its tag `+0x08`, the send track, a record whose flag is 0) to get the buffer, adds the two loop sends
  into it (`0x714903`), then calls `+0x128` again with the flag 1 (release, `0x714934`).
- That is exactly the call Fruity Send uses for its destination ("the host hands the plugin the destination's buffer
  (host method at +0x128 with the plugin tag, the insert index and a lock flag 0) … releases it (flag 1)",
  `REA_rodada105_send.md` §2), whose index FL renders proved to be the mixer insert number (§2 [EXEC] there). Transmitter
  (same host call, `REA_rodada167b.md` §1) is mapped the same way.
- After a restore the editor's two track menus (objects `+0x7f8` send, `+0x800` input) are set to the state's values
  through the same item-index method (`0x7139be`, `0x71399b`).
- LYNX now: `importFlp` sets the slot's Send-to parameter to the insert number (0 when it is the slot's own insert or
  past the project's tracks); the input track keeps the k-th-input mapping (host `+0x120`, the inputs list, as the
  Limiter / Transmitter side-chains). [TEST `tp167n_flp_import_send_and_input_tracks`]

## 2. Update A / B / A+B and Flush are push buttons
- FL: the engine reacts to the rising edge of controls 15–17 and to control 12 > 0.5 (`REA_rodada167f-transporter` §3.1,
  §3.3 step 5); FL's editor buttons send 1 on press and 0 on release.
- LYNX's push buttons (`plugui::momentary`) flip the stored value at each click. The effect now hands every change of
  those four values to the engine as a press: 1 for one processing block, 0 at the next; a click during the pressed
  block is queued and pressed after the release (so the engine always sees a rising edge). Loading a state, a preset
  or a project does not press anything (the first block sets the values as FL's restore does — the bit-exact path).
- The editor draws the four as push buttons (never lit). [TEST `tp167n_update_buttons_are_momentary`]

## 3. FL's factory presets
- [DATA] `Data/Patches/Plugin presets/Effects/Transporter/`: Basic reverser, Beatdrifter, Default, Flashbacker, Octavary,
  Radpad — all version-4 states (20 controls, source, stereo), in the preset's 0xD5 record.
- [EXEC] each state restored into FL's own plug-in (`fxt.exe tp.dll`, 44.1 kHz, 512-frame calls, own input, 48000
  frames of the tests' LCG noise in eight loudness steps, 60 BPM; script `tmp/flgap167tp/py/goldfst.py`, data
  `/mnt/e/lynx-work/flgap167tp/t/gf*_*.{bin,f32}`), FNV-1a over the output float bits:
  Basic reverser / Octavary 0x4a1508e9d88c40c3 (dry 0, the loops not yet triggered in this input: the same output),
  Beatdrifter 0xc64d1c414699a0e2, Default 0x6bbaf64ff2925971, Flashbacker 0x0ea5acce97ff601b, Radpad 0x9ac4aefec8553442.
- LYNX: the mixer slot's "FL presets" menu and the editor's preset bar load them (`effectFromFlPreset` → the Transporter
  state parser). [TEST `tp167n_fl_factory_presets_match_fl_plugin`: the files read from the FL installation (FL Data
  argument), all six equal FL's hashes]

## 4. Merge with main
- Merged origin/main (5d3f5c17: Emphasis `flEmphasis` took the previous last EffectKind) — `flTransporter` is appended
  after `flEmphasis`; the send-tap list of `RackRenderer` holds Fruity Send, Patcher, Transmitter and Transporter;
  i18n tables merged (both rounds' strings) and regenerated.

## Where I stopped
- Done: wip commit of 167f's helper work, merge of origin/main, §1–§3 with tests.
- Next: build-local with FL Data, Windows build + GUI check of the editor (fresh LYNX_PROFILE_DIR, LYNX_TEST_MODE=1),
  land via `flock ~/projetos/lynx/.git/lynx-merge.lock ~/.claude/jobs/f8789b49/tmp/coord/land.sh flgap167-tp`.
- Rerun: `ZIG_GLOBAL_CACHE_DIR=~/.cache/zig-flgap167tp ZIG_LOCAL_CACHE_DIR=~/.cache/zig-flgap167tp
  ~/.claude/jobs/f8789b49/tmp/coord/buildslot.sh scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"`.
