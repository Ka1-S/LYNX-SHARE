# REA round 167e — flaky tests, performance and robustness leftovers (perf167, v0.1.22)

Brief: flaky tests (VfxScript165 endless-loop p95, `vfx_script_late_answers_play_at_the_next_block`, natives142
FL Keys silent), VFX Script export tail, Contrary Motion blip, `--visualtest` on silent starts, PCM cache size limit,
audio device opened in parallel with the main window, start-up / heavy-project profiling with before/after numbers.

Evidence markers: **[EXEC]** measured here, **[DECOMP]** Ghidra/objdump of FL's binaries, **[CODE]** our source,
**[model]** our reading. Raw data: `tmp/perf167` (start-up traces, jsonl), `E:\lynx-work\perf167` (renders, profiles).

Every item is LYNX-internal (tests, robustness, speed) except two FL-parity ones: the VFX Script export tail and the
Contrary Motion blip. No issue number; no reply drafts.

## 1. Flaky tests

### 1a. `vfx_script_errors_are_reported_and_never_hang` (endless loop, "p95 < 2.5")

- Reproduced [EXEC]: main's test binary, the VFX tests pinned to two cores shared with four busy loops
  (`taskset -c 9-10`): 3 runs → p95 7.99 / 0.09 / 9.54 ms, 2 of 3 failed. Unloaded: p95 0.01–0.04 ms, pass.
- Root cause [CODE]: once the script was stopped ("took too long"), `Runtime::process` still handed every audio block
  to the Python thread and spun (with `yield()`) up to the 2 ms budget for its empty answer. On a busy machine the
  Python thread wakes late, and `yield()` hands the core to the other processes for a whole time slice: the test
  thread came back 4–10 ms later. The bound was right; LYNX made the audio thread wait for nothing.
- Fix: a script that stopped (error, too slow, too much memory) or never compiled is *halted*: after the block in
  which that happened the Python thread sets `halted`, and the audio thread then returns at once (no job, no wait).
  A compile / Restart clears it before the next block (the voices it ended still get their note-offs).
  Real-time benefit in the app too: a broken VFX Script costs the audio thread nothing.
- Now [EXEC]: same contention, 3 of 3 pass, p95 0.00 ms. New test
  `perf167_stopped_vfx_script_costs_the_audio_thread_nothing` (5000 blocks of a stopped script: 0.04 ms in all).

### 1b. `vfx_script_late_answers_play_at_the_next_block`

- Reproduced [EXEC]: same contention, 3 of 3 failed (`first.size() == 0` and the second block's check).
- Root cause: the test's margins, not LYNX. The script took 6 ms against the 2 ms budget and the test looked again
  after a fixed 30 ms. Preempted inside its own wait for longer than the script, the test thread found the answer
  already there (correct: an answer in time plays at its sample); a Python thread starved for > 30 ms answered after
  the second look. Neither is a LYNX bug: a late answer must play at the start of the first block after it.
- Fix (the bounds were wrong): the script takes 40 ms (20× the budget, far inside the 250 ms limit) and the test
  keeps playing 5 ms blocks until the answer comes (at most 2 s), then checks it plays at offset 0.

### 1c. A third one found on the way: `vfx_script_memory_runaway_is_stopped`

- [EXEC] Under heavier contention (two cores, ten busy loops) it failed 1 of 2: "Stopped: the script took too long"
  instead of "more than 768 MB" (4.3 s wall for 0.19 s of user time).
- Root cause [CODE]: the Python time limit (export 3 s, real time 250 ms) was wall-clock time. On a busy machine a
  good script could be stopped as "too slow" in an export — a user-facing bug, not only a test one.
- Fix: the limit counts the Python thread's own CPU time (`ThreadCpuClock`: `pthread_getcpuclockid` on Linux,
  `thread_info` on macOS, `GetThreadTimes` on Windows), with 4× the limit of wall time as a backstop for a script
  that waits. The endless-loop test now waits for the stop (up to 3 s) instead of assuming it came within its 400
  blocks.

### 1d. natives142 "FL Keys silent"

- The only record: one full run on 2026-10-29 (`silent: FL Keys (0)`, RMS exactly 0) on a heavily loaded machine
  with faulty RAM. [EXEC] Not reproducible: the test alone 2× unloaded, 3× on two cores with four busy loops — pass.
- RMS 0 means the FL Keys channel had no wavetable: `flKeysWavetable` returns none when any of its 30 piano files
  (read from the FL install on /mnt/c) fails to read. Most likely a failed read of one file in that run.
- Done: the test now says, when an engine is silent, which of its files does not read now and whether a second
  render plays — so the next occurrence names the cause. Not loosened.

## 2. VFX Script: the export tail (FL parity)

- FL [EXEC, renders of round 165d: `D:\lynx-work\vfxscript165\fl\*.wav`, FL64.exe /R of the 12 cases]: every FL render
  ends at the song's end (+5 ms): Tutorial 3's pulse (a note every 1/8 while `vfx.context.isPlaying`) has no note
  after 4.0 s. Tutorial 3's own text gates generation on `isPlaying` [FL factory script]. So in FL's remainder the
  song is stopped for the script.
- LYNX before [EXEC]: the export's tail ran with the transport playing: tut3 went on pulsing every 1/8 note
  until the tail's 6 s maximum (a 10.0 s file against FL's 4.0 s).
- LYNX now: the export tells VFX Script when the music has ended (`vfx::script::setSongEnded`); in the tail the
  scripts see `isPlaying` False. Only VFX Script (the FL evidence is about it); hosted plugins keep their transport.
  Test `perf167_vfx_script_sees_the_song_stopped_in_an_export_tail`.

## 3. Contrary Motion "blip" (FL parity) — measured again, not changed

- Round 165d's note-detector (8192-sample frames) saw a one-semitone-lower note for ~35 ms at each release in FL's
  render. [EXEC] A finer look (5 ms RMS + zero-crossing pitch, `tmp/perf167`): there is no second voice; FL's sound
  ends ~10 ms sooner than LYNX's at every release, *also in Tutorial 1* (pure pass-through): FL −24 dB at 0.755 s and
  silent at 0.760, LYNX −23 dB at 0.760 and silent at 0.770. The pitch moves during FL's short decay (the detector's
  wide frame turned that into a "lower note").
- So the difference is in the release of the instrument behind the script (Sytrus in a Patcher), the same with and
  without Contrary Motion, not in the script. Left open: it needs FL's note-off timing of Patcher → Sytrus measured on
  its own (a render without VFX Script), outside this round.

## 4. `--visualtest` on a project with a silent start

- Before: the check required the worker-thread export's first sound within 0.1 s (right for the default demo); a
  project starting later (FL's ZGE demo "Astes - LIFE": 1.2 s) reported result=ERROR.
- Now: the reference is the same export run on the message thread (round 164c's bug lost the start only on another
  thread); the two first sounds must agree within 10 ms. Report line: `first sound at X s (message thread: Y s)`.

## 5. PCM cache size limit

- Before: the decoded copies (`LYNX-cache\*.L.pcm / *.R.pcm`, memory-mapped streaming of long samples) had no limit
  (2.4 GB on this PC, round 165i). Only the stretched copies had one (1 GB).
- Now: 2 GB; past it, the least recently used are deleted down to 3/4 (1.5 GB), once per session on the cache
  writer's background thread. A copy found again is marked used (its date, at most once an hour). A copy mapped now
  is safe: Windows refuses the delete, and on Linux/macOS the mapping keeps the data. The budget logic is shared
  with the stretch cache (`core/CacheBudget.h`), test `perf167_cache_budget_prunes_the_least_recently_used`.

## 6. Start-up: the audio device and the main window — measured, not changed

- [EXEC] Start-up trace of main's build (`LYNX_LOADTRACE`, empty start, 6 runs, `tmp/perf167/startup-*.log`): splash
  ready ~340 ms; engine workers ~117 ms; **audio device opened 0.83–1.13 s** (WASAPI); then the start-up sound and
  its 1050 ms landing; the main window is built in **80–130 ms**; window shown at 2.5–3.0 s.
- So opening the device while the window is built could save at most the window's build, ~0.1 s, of a 2.5–3.0 s
  start, and the device open already overlaps the splash. Without the saved input device the open took 825 / 936 /
  1127 ms against 1116–1131 ms with it: inconclusive (noise of the same size).
- Not done, on purpose: ASIO drivers and JUCE's WASAPI need the thread that opened them (COM apartment,
  `CoInitialize`; ASIO calls must come from one thread); the window reads the device's state as it is built
  (Mixer, audio settings) and the song load touches `deviceManager`. A threaded open would trade ≤ 0.1 s for
  races in every driver type. The time that matters is the start-up sound's landing (LYNX-own, kept as asked).

## 7. Heavy project: CPU per block and allocations on the audio thread

- Method [EXEC]: `tools/renderwav` on FL's demo songs "Ookay - Thief" (37 channels, Harmless, 3x Osc, Vocodex, many
  Reeverb 2) and "Eric Kauffmann & NrgMind - Exoplanet", 256-sample blocks, one pinned core, `LYNX_PROFILE=1`
  (per-block thread CPU against the 5.8 ms budget), `perf record` for the hot functions, and a new
  `LYNX_ALLOCS=1` mode in renderwav: `operator new` replaced, counting the allocations made while a block is
  processed, with a backtrace of each distinct site (`addr2line -e renderwav`).
- Hot functions (perf, Thief): Sytrus' per-sample voice, the Vocodex filter bank kernel, the FL filters, the
  Reeverb 2 tank; `SamplerInstrument::idle` 0.86 %, `scheduleClip` 1.08 %. These are FL-exact DSP (bit-compared with
  FL in earlier rounds): not rewritten here, a faster version must stay bit-identical — left for a dedicated round.
- Allocations on the audio thread (the real-time hazard: a heap lock can stall a block for milliseconds):
  - Thief, before: **259 allocations**, all in the first block: 12 Reeverb 2 built their lines in their first
    `process`; Harmless built its effects rack (chorus / delay / reverb / Maximus) at its first note.
  - Fixes: Reeverb 2 reserves every buffer for the rate in `prepare` (`reeverb2::Engine::reserve`, predelay room for
    its 2 s tempo maximum); Harmless takes its settings on the UI thread when the channel is made
    (`Instrument::settleNew`, called by the rack builder for a new instrument); Fruity Reeverb (`reeverb1`) makes its
    lines in `prepare`; Stereo Enhancer / Shaper reserve their delay ring (0.5 s); Fruity Delay 2 makes its engine in
    `prepare` with room for tempos down to 60 BPM and restarts it in place on the first block (or after a reset).
  - After: **Thief 0, Exoplanet 0** allocations in 3446 blocks (Exoplanet had 79 left after the first two fixes:
    Fruity Reeverb, Stereo Enhancer, Fruity Delay 2).
  - Output unchanged: both songs render **bit-identical** to main's renderwav (20 s / 40 s, every sample).
  - Left: a Harmless *settings change while it plays* still rebuilds its rack on the audio thread (its mailbox hands
    the settings over there). Delay 2 below 60 BPM at its longest time still grows its line once.
- Tests: `perf167_reeverb2_first_block_and_predelay_changes_do_not_allocate`,
  `perf167_new_harmless_channel_does_not_allocate_at_its_first_note`,
  `perf167_reeverb1_stereo_enhancer_delay2_do_not_allocate_on_the_audio_thread`,
  `perf167_delay2_restart_matches_a_new_engine` (bit-identical to a new engine).

## 8. Numbers

| measure | before | after |
|---|---|---|
| VFX endless-loop test p95 under contention (2 cores, 4 busy loops) | 7.99 / 0.09 / 9.54 ms (2/3 fail) | 0.00 ms (3/3 pass) |
| `late_answers` test under contention | 3/3 fail | 3/3 pass |
| `memory_runaway` under 10 busy loops | 1/2 fail ("too long", 4.3 s wall / 0.19 s CPU) | 2/2 pass |
| a stopped VFX Script, 5000 blocks | a 2 ms wait per block | 0.04 ms in all |
| VFX tut3 export length (FL: 4.0 s) | 10.0 s, pulsing in the tail | stops pulsing at the song's end |
| audio-thread allocations, Thief 20 s | 259 (first block) | 0 |
| audio-thread allocations, Exoplanet 20 s | 79 (after the Reeverb 2 / Harmless fix) | 0 |
| thread CPU per block, Thief 40 s (same load, interleaved runs) | mean 35 %, p99 99 % | mean 38 %, p99 107 % (noise: other builds running) |
| thread CPU per block, Exoplanet 40 s | mean 41 %, p99 67 % | mean 41 %, p99 69 % |
| start-up: device open / main window build | 0.83–1.13 s / 0.08–0.13 s | unchanged (§6) |

The per-block CPU did not change, as expected: no DSP was changed (the renders are bit-identical) and the machine
was shared with other builds (the same binary moved 29 % → 38 % mean between two runs). What changed is the first
block's allocation spike and the stopped scripts' waits.

## Where I stopped

- 2026-10-31 15:40 — all items done in the worktree, tests pass (perf167 15 checks, natives142 261, vfx_script 340,
  delay / harmless / stereo / reeverb / drumaxx groups). Next: merge origin/main and land.
