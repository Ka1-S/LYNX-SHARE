# Round 167w — PRs #29 and #30 (jaeminbird): CLAP restart handshake, SF3 terminal sample record

Standard: docs/analise/REA_rodada165-issue11c.md. Neither PR is an FL-parity item (host robustness fixes), so there
is no FL evidence to collect: the evidence is a reproduction on `main` and the same check with the fix.

## 1. PR #30 — "SoundFont: check the terminal sample record of SF3 files too" (d25dc86c, kept as is)

- **Contributor's words:** "`parseFont` neutralises SF3 sample headers whose compressed data lies outside the `smpl`
  chunk, but its loop stops at `i + 1 < shdrNum` and skips the terminal (EOS) record. TinySoundFont's
  `tsf_decode_sf3_samples` walks every header (`i <= shdrLast`), so an EOS record flagged compressed with
  `start = 0xFFFFFF00`, `end = 0xFFFFFFFF` [...] reads 4 bytes about 4 GB past the buffer: a segfault on loading or
  previewing the file."
- **Review:** correct and minimal. Zeroing the EOS record's fields is harmless (it is never a playable sample).
- **LYNX before:** reproduced — the test's font through `main`'s `SoundFontLoad.cpp` (standalone, ASan):
  `SEGV ... READ in tsf_decode_sf3_samples third_party/tinysoundfont/tsf.h:925` from `parseFont` (SoundFontLoad.cpp:312).
- **LYNX now:** same font, fixed file: `parsed=1 bad_regions=0`, no ASan report. Test
  `soundfont158_sf3_terminal_sample_out_of_range` in the core suite.

## 2. PR #29 — "CLAP: wait for the block in progress before a requested restart" (c7a2aeff, kept as is)

- **Contributor's words:** "When a CLAP plugin calls `request_restart` [...] the host runs `suspendProcessing(true)` +
  `prepareToPlay` on the message thread [...]. The rack calls `processBlock` directly (`PluginHost.cpp` `runBlock`),
  never under `getCallbackLock()` or checking `isSuspended()`, so the suspend doesn't hold the audio thread off: the
  plugin is deactivated in the middle of its own `process`. [...] The restart now marks itself pending (new blocks
  output silence), waits for the block in progress, then restarts. It gives up after 2 s".
- **Review (correct; nothing to change):**
  - Handshake: audio thread `inProcess = true` then loads `restartPending`; message thread `restartPending = true`
    then loads `inProcess` — both seq_cst, so at least one side sees the other (Dekker): either the block returns
    silent at once, or the restart waits for it. `processBlock` has one early return and it clears `inProcess`; the
    normal path clears it after the last read of `scratch`. The release store of `restartPending = false` after
    `prepareToPlay` publishes `active` / `maxBlock` / `scratch` to the next block.
  - Other callers of `processBlock`: the rack (`PluginEffect::process`, `PluginInstrument::render`, fixed-size
    buffers) — all through `runBlock` on the audio / worker / export thread; the export from the Export window runs
    on its own thread (the message thread is free to run the restart); a render on the message thread itself
    (`--plugintest`) just runs the queued restarts after it. The sandbox (`PluginSandbox.cpp`) calls
    `processBlock` on its own audio thread in the child process, whose message thread runs the restart: same
    handshake. No path waits for the message thread from inside `processBlock`, so the 2 s spin cannot deadlock.
  - A plugin that crashed inside `process` is caught by the crash guard (`crashed`, never called again); its
    `inProcess` stays set, the restart gives up after 2 s and leaves it alone — consistent with "a crashed plugin is
    not called again". (Each further request_restart of such a plugin would spin 2 s; a crashed plugin does not ask.)
  - The spin is microseconds in practice (one block); the UI is not blocked otherwise.
  - PDC: `prepareToPlay` → `setLatencySamples`; `PluginEffect::latencySamples()` reads it and the engine's timer
    (`AudioEngine::timerCallback`, `currentLatencies() != latencies` → `publishSong()`) makes the new PDC plan; the
    sandbox forwards it per block (`shared->latency`).
- **Test we added (32257a2a + follow-up):** `tools/testplugin/lynx_test_restart.c` — a CLAP effect that calls
  `request_restart` every 8 blocks (48 times), counts deactivations that land inside its `process` and blocks
  processed while inactive, and reports a new latency (64–448 samples) after each activation. `LYNX.exe
  --claprestarttest "LYNX Test Restart.clap" report.txt` puts it alone on the Master (smart disable off), renders
  30 s from a thread of its own (as the Export window), waits 1 s and checks: no overlap, no inactive block, the
  restarts happened, PDC total == the plugin's latency, no plugin crash.
- **Windows (fresh LYNX_PROFILE_DIR + LYNX_TEST_MODE=1, CLI only, no window):**
  - with the PR: `requests=48 activations=51 overlapped=0 inactive_blocks=0 blocks=1724`, plugin latency 192 =
    `pdc_total=192` (3 runs: same).
  - before (c7a2aeff reverted): see §3.
- No third-party CLAP was used: the installed ones (FabFilter) are commercial and need a GUI action to change
  latency; the brief allowed a test CLAP instead.

## 3. Results
## Where I stopped

- done: worktree ~/projetos/lynx-wt-pr2930 (feature/pr2930) = c7a2aeff (ff) + merge of d25dc86c + our 32257a2a
  (tools/testplugin/lynx_test_restart.c + `--claprestarttest`); wip pushed to origin/wip/pr2930.
- in progress: build-local with FL Data (/mnt/e/lynx-work/pr2930/bl.log), Windows build (win1.log), test CLAP build.
- next: run `--claprestarttest` on Windows (fix) and with c7a2aeff reverted (before), land via land.sh pr2930.
