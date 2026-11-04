# Round 169g — discussion #56 (Transistor Bass, Sytrus) and the crash reports of 2026-10-31…11-03

Reporter (Jan12139, discussion #56, 2026-11-03 20:38 UTC): "Transistor Bass is described as implemented but I really
can't get it working. Sytrus: the description states it isn't featured yet but I can import presets. […] buggy and
can crash, which I am also sending reports about." Installed "via the official installer".

Which version: v0.1.23 was published at 22:55 UTC, after the post, so the reporter had **0.1.22** (or older). A crash
report from 0.1.22 (release windows-x64, commit bc2d2a2b18) arrived at 20:37:49 UTC, one minute before the post:
very likely theirs (see §3, `a3effad0`).

## 1. Transistor Bass

| Reporter | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| "can't get it working" | FL Studio 2025.1.6's installer (`flstudio_win64_25.1.6.4997.exe`, extracted on the VPS, NSIS blocks) ships `Transistor Bass\engine_x64.dll` with the **same 57 + 55 recorded "303 Pulse" cycles, byte for byte** as FL 2026's, at file offsets 0xFCEF0 / 0x1596F0 instead of 0xFD2F0 / 0x159AF0 (the code differs: image 1 629 184 vs 1 630 720 bytes). | `loadDll` accepted only FL 2026's file (whole-file SHA-256 `4cc72470…`) and read fixed addresses. With any other FL build: the `.flp` import left TB channels unsupported, the plugin list showed "Transistor Bass" (its preset folder exists) but adding it did nothing, the channel window said the engine files were missing. Without FL: "Transistor Bass (requires FL Studio)" (it has no LYNX bank; it needs FL's cycles). | The two sets are found by their shape (`[count][size]"RIFF"…`, 4-byte aligned, every WAV decoded), wherever the build put them. FL 2025's DLL renders sample-identical to FL 2026's (test `crash169g_transistor_bass_plays_with_other_fl_builds`); added from the plugin list with an FL 2025 installation it plays (`crash169g_transistor_bass_from_the_plugin_list_with_fl_2025`). Without FL it stays "(requires FL Studio)" — true: FL's oscillator cycles are not shipped. |

Same check for the other engines that read FL's DLLs (signature-free image hash `peImageSha256Hex`: the bytes up to
the certificate table, PE checksum and certificate directory zeroed; equal to the whole-file hash for FL 2026's
unsigned files, so the existing constants stay valid):

| Engine DLL | FL 2026 | FL 2025.1.6 | LYNX now |
|---|---|---|---|
| FL Slayer `Slayer_Engine.dll` | signed, whole-file `84b64c43…`, image `f33fd40e…` | **same image**, other signature (whole-file hash differs) | accepted by image hash; renders identical (test) |
| BooBass `BooBass_Eng_x64.dll` | `4e10efcd…` | image `ed3affa1…`, 38 bytes differ (time stamps, debug directory), same size | FL 2025 build accepted; renders identical (test) |
| DrumSynth Live `drumsynth_eng_x64.dll` | `9a5f1682…` | image `1b503eb2…`, 51 bytes differ, same size | FL 2025 build accepted (WAVES resource read by name) |
| Kepler / Kepler Exo `engine_x64.dll` | `baf37a1d…` / `10ed2464…` | a different build (2.5 M bytes differ, other layout) | **still FL 2026 only** (fixed RVAs of the LFO / noise / exp tables; needs a content search like TB's — next round) |
| Toxic Biohazard | optional table check | — | unchanged |

Reproduction on 0.1.22/0.1.23 with FL 2026 installed (the build whose hash LYNX knew) — the engine plays: the
existing test `natives142_engines_from_the_fl_installation` makes the channel from the plugin list and renders it.
New: `LYNX_NO_FL=1` makes LYNX behave as on a computer without FL (docs/FL_INTEGRATION.md).

## 2. Sytrus wording

`docs/PLUGINS.md` said "(Sytrus and Harmor are not featured yet: both are *Close*)" — "Featured" is the list of
Exact / Near-exact engines, but the sentence read as "not supported yet". Rewritten: both work today (imported
projects and `.fst` presets, engines rebuilt from the plugin state; a new instance starts from FL's Default preset),
they are only left out of the Featured list because their match is Close. The Sytrus and Transistor Bass rows say
what they need. README, website (15 languages: Sytrus = "Close", correct), the in-app texts and the Microsoft Store
listing (`lynx-listings-v3/listings.csv`: no plugin named) needed no change.

## 3. Crash reports (lynx-share.com/api/reports.php → crash-fetch.sh → /mnt/d/lynx-work/crash-reports)

196 reports on the server, 117 new; since 2026-10-31 12:00: 161 (crash / hang / manual 86, the rest "unclean" exits
without a stack). By version: mostly 0.1.21 (136 of all), 0.1.22: 16.

| Signature | Count | Versions | Root cause | Fix |
|---|---|---|---|---|
| macOS `juce::DirectoryIterator::next` → `NSURL fileURLWithPath:` throws (Browser search index thread) | 18 + 1 | 0.1.20, 0.1.21 | one odd file name | already fixed in 0.1.23 (7b6f35f5, issue #42, readdir) |
| Windows `LoadPanel::refresh` / `PluginHost::createInstance` / `preloadKey` access violations while opening a project, all with **Auto-Key** on the stack (`DispatchMessageW` inside its `setupIO` / `prepareToPlay`) | 5 | 0.1.21 | the plugin pumps the message loop while it is created: the project-load panel's timer ran again *inside* `preloadNextPlugin`, created the next plugin of the same queue and refreshed the panel under the outer turn | `LoadPanel`: one turn at a time (nested timer ticks and refreshes skipped; `SafePointer` because the panel deletes itself when done) |
| Windows access violation in the lambda at `ExportDialog.cpp:132` after a 10 s "not responding" (stems export cancelled) | 1–2 | 0.1.21 | Cancel → `stopThread (10 s)` on the message thread while the stems thread waited for the message thread to delete its job → JUCE killed the thread, the queued lambda ran on freed locals | `core/MessageThreadOwned.h`: the worker stops waiting when asked to exit; the job lives in shared state, still made/deleted by the posted calls (stems + playlist-track exports); test `crash169g_message_thread_owned_cancel` |
| Windows hang `juce::FileInputStream::FileInputStream` (opening an .flp) | 3 | 0.1.21 | a sample on a slow / offline path opened on the message thread | not changed (needs the hang files; next round) |
| macOS arm64 bus error `AudioIODeviceCombiner::close` ← `restartAsync` ← CoreAudio device listener (AirPods / WH-1000XM5 disconnecting) | 2 | 0.1.21, 0.1.22 | JUCE 8.0.4 combiner (input ≠ output device) restarted from CoreAudio's thread while being deleted | not changed (JUCE bug; needs a Mac to verify a patch) |
| Windows `Vst2PluginFormat::createPluginInstance + 0x61` adding "FL Studio VSTi" | 1 | 0.1.22 | the plugin's `VSTPluginMain` / `effOpen` returned or left an invalid AEffect | not changed (one report) |
| third-party plugin code (Black Metal Keys, DecentSampler, Kick 2, Grand Piano, Arcade, Multimode, Waves shells, Kontakt, Splice, Unison Drum Monkey, SwarPlug, RTSS overlay hook…) with the sandbox off | ~25 | all | crashes inside the plugin / its editor | none in LYNX; the sandbox option exists (offered after an editor crash) |
| Linux `LYNX+0x…` frames (`__libc_free` abort 0xd53602 ×2 on 0.1.22, segfaults on 0.1.21) | ~15 | 0.1.19–0.1.22 | **cannot be named**: the Linux release binary is stripped and no Linux symbols are kept | next round: keep a Linux symbol file per release (like Windows' LYNX-symbols) |

The reporter's own report (`a3effad0`, 0.1.22, Windows 11, Ryzen 7 7800X3D, RX 7900 XTX): an access violation inside
**Black Metal Keys.dll** (Pepto Audio, VST2) right after its editor opened — the plugin's code, not LYNX's.

## Where I stopped

- Done: TB DLL search by shape + FL 2025 builds of Slayer / BooBass / DrumSynth; LoadPanel re-entrancy; export
  Cancel; `LYNX_NO_FL`; PLUGINS.md wording; this doc; tests in `tests/Crash169gTests.inc` (FL 2025 DLLs read from
  `LYNX_FL25_DLLS`, default /mnt/e/lynx-work/crash169g/fl25 — 3392 = TB, 1542 = Slayer, 310 = BooBass,
  2741 = DrumSynth; not in the repo).
- Next: Kepler content search; Linux symbols; FileInputStream hangs; CoreAudio combiner.
- Rerun: `LYNX_TEST=crash169g coord/buildslot.sh scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"`.
