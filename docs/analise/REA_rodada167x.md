# Round 167x: PR #31 (LordVersA) "Extract stems from sample" (closes #27): review, test, FL comparison

PR head 11d4570b (f51f9dad + merge of origin/main 11d4570b, up to date with main e6e36fdd). The user answered the
PR's 6 decisions (2026-11-02): keep the licence wording, keep the pinned Hugging Face revision + SHA-256, macOS 13.4+
for this feature only, VC++ message with a link + the Store's VCLibs dependency, size accepted, SignPath unchanged
for now. Landed with his two commits unchanged and ours on top, after the lightness audit of §4.

## Where I stopped

- Done: review, FL REA, fixes, measurements and the Windows GUI session (§4); merged origin/main (ee1c8b8e, i18n
  tables regenerated). Release workflow on wip/pr31-review (run 41461808932, aa07837f): all
  green (exe, self-test, zip, NSIS, MSIX installed with -DependencyPath VCLibs and self-tested, AppImage, dmg). Next: land with land.sh (feature/pr31), reply drafts in coord/replies-0.1.22.md.

## 1. What FL 2026 does (FLEngine_x64.dll of FL 2026, md5 fce4ea55…, clean-room: behaviour only)

Evidence: [DFM] = the TDemucsForm resource (tmp/rea165/dfm/TDemucsForm.dfm.txt), [DECOMP] = Ghidra decompile of the
named function (tmp/pr31/demucs1-7.c). Published methods of TDemucsForm: FormCreate 0x11356c0, FormShow 0x11357a0,
OkBtnClick 0x1135890, CancelBtnClick 0x1136620, FormDestroy 0x1135780, OpenRemixButtonClick 0x1136860; the clip menu
handler EEAUExtractStemsMenuClick 0x123a9a0.

| # | Question | FL 2026 | Evidence |
|---|---|---|---|
| F1 | Menu place | Audio clip menu, "Sample" section: Make unique as sample, Edit sample, Pitch-correct sample, Time-warp sample, **Extract stems from sample**, Deverb, Detect tempo, Fit to tempo | [DFM] tmp/rea165/menus.txt:804 |
| F2 | First use | the engine is an optional component ("Stem extractor Windows"): if missing, "Extract stems\|This feature needs to be downloaded before it can be used." + "Download it now?" (Yes/No); FL's own downloader fetches it | [DECOMP] 0x123a9a0 |
| F3 | While recording | refused: "Can't extract stems while recording … Please stop recording before extracting stems." | [DECOMP] 0x123a9a0 |
| F4 | Options dialog | TDemucsForm "Extract stems from sample" (hint "Extract stems responsibly"): check boxes **Drums, Bass, Instruments, Vocals** (all on), **Limit CPU usage** (off), **When done**: Mute source clip / Mute source track / Do nothing, **Group playlist tracks** (on), **Safe mode** (off, "Use this for troubleshooting."), **Assign to new mixer tracks** (on); buttons Cancel / Extract; a progress gauge and a status line. "Select at least one stem to continue." if none is ticked | [DFM]; [DECOMP] 0x11354b0 |
| F5 | Remembered options | every option is saved on close and read on open (registry values "Drums", "Bass", "Instruments", "Vocals" = 1, "Limit CPU" = 0, "Group tracks" = 1, "Assign to mixer" = 1, "Safe mode" = 0, **"When done" = 1 = Mute source track** when never set) | [DECOMP] 0x1135a60 (read), 0x1136090 (write) |
| F6 | What is separated | when the clip has its start / end offsets set (playlist item +0x18 / +0x1c ≥ 0), **only that region** of the sample is written to a temporary WAV and separated; otherwise the whole sample | [DECOMP] 0x1134440 → 0x10644d0, 0x672130 |
| F7 | Model rate | the input is resampled to 44 100 Hz; each stem is resampled back to a global rate (the project's / output rate, `*PTR_DAT_015e1f40`) before it is saved | [DECOMP] 0xcae090 (0x695240 with 44100.0, then with param_5) |
| F8 | Threads | the engine gets all cores (`PTR_DAT_015e4600`), **half with Limit CPU**; **Safe mode** pins the process to cores 0-3 (affinity 0xF) for the run | [DECOMP] 0xcad780, 0xcae090 |
| F9 | Engine | FL's own native module `stemextract.dll` + `data.mod` (demucsCreate / InitFromMemory(…, 7.8 s segment, threads) / NumChunks / ProcessChunk / PostprocessFull / SetInferenceParams(0.1, 1.0) / SetSelected), not ONNX Runtime | [DECOMP] 0xcad970, 0xcad780 |
| F10 | Progress | status "Loading module...", "Initializing...", "Loading sample...", "Processing..." (chunk n of N, Cancel checked per chunk), "Saving stems (%d of %d)" | [DECOMP] 0xcae090 |
| F11 | Files | `<sample name>_<stem>.wav` (stems "drums", "bass", "instruments", "vocals"; a 5th "residual" exists in the table) in FL's audio folder (path kind 1: folder id 0x3F0 + "Audio") | [DECOMP] 0xcae090 (table 0x150a850, "_", ".wav"), 0x11bf050 |
| F12 | Order | drums, bass, instruments, vocals (only the ticked ones) | [DECOMP] 0xcae090 (bit order 1, 2, 8, 4 → 1, 2, 4, 8) |
| F13 | Tracks | for each stem FL **inserts a new Playlist track** right under the source clip's track (source + 1 + i, the tracks below move down; max 500 tracks, stops when full); names it **"Drums" / "Bass" / "Instruments" / "Vocals"** (first letter upper-cased) and sets a per-stem value 4 / 14 / 12 / 20 on the track (+0x30, most likely the track icon; not verified); with Group playlist tracks the new tracks are grouped (+0x3E, "grouped with above") | [DECOMP] 0x1134c30, 0x12e62e0 ("Playlist insert track"), 0x12e60f0, 0x65e4f0 |
| F14 | Channels | each stem becomes a new audio-clip channel (FL's "AudioTrack.fst" preset) with the stem loaded, taking the source channel's stretch / pitch fields (+0x2e4…+0x2f0); with Assign to new mixer tracks it gets a free mixer track, otherwise the **source channel's mixer track** | [DECOMP] 0x1134c30 (0xff7e00 / +0x258) |
| F15 | Clips | a clip per stem at the source clip's position; with a region (F6) the new clip has no offsets (it plays its whole, region-sized file), otherwise it copies the source clip's offsets | [DECOMP] 0x1134c30 (local_74 / local_70 = −1 or +0x82 / +0x86) |
| F16 | Source clip | When done = Mute source clip → the clip is muted; Mute source track → the source track is muted (+0x3C = 0); Do nothing | [DECOMP] 0x1134c30 (flags 0x20 / 0x40) |
| F17 | Undo | one undo group "extract stems from sample" around the whole result | [DECOMP] 0x1134c30 (0x1016600 … 0x10169b0) |
| F18 | Error | "Stem separation\|There was a problem with stem separation. The error message is:" + the message | [DECOMP] 0x1134c30 |
| F19 | Remix a song | the same form also drives Tools › "Remix a song" (a template): tempo + downbeat detection ("Detecting tempo...", "Detecting downbeat..."), a "Downbeat" marker, then the 4 stems; note "You are free to remix songs for personal use. Just don't publish them without the author's permission." | [DFM], [DECOMP] 0x1136860 |

FL's own model / module was not touched (no copy, no run, no hash): only FLEngine's code that drives it.

## 2. Review of the PR

Overall: clean, small, well commented; the C API is loaded at run time as asked, the download is consented, pinned and
hashed, the work is off the audio and message threads, the result is one undo step. Nothing executable is downloaded
at run time (the model is data; the runtime ships in the packages). Findings, most important first:

| # | Severity | Where | Finding | Suggested fix |
|---|---|---|---|---|
| R1 | high | StemExtract.cpp `StemThread` + JUCE `ThreadWithProgressWindow::timerCallback` | Cancel calls `stopThread (30000)` **on the message thread**: the UI freezes until the worker returns, and after 30 s JUCE `killThread`s it (TerminateThread / pthread_cancel inside ONNX Runtime = leaked locks, possible later hang or crash). Several phases never look at `threadShouldExit()`: the whole-file resample, `CreateSession` (166 MB model + graph optimisation), the overlap-add normalisation and the four `writeWav` | make the window non-blocking (own `juce::Thread` + a small progress component / `juce::ProgressBar` in an async dialog, cancel = flag + `RunOptionsSetTerminate`, never kill); check the flag between phases |
| R2 | medium | `extractStemsFromClip` | the runtime is only loaded **after** the 166 MB download: a user without the VC++ redistributable, or with the bare `LYNX.exe` (a release asset of its own, line 228 of release.yml, used as "portable" by #4/#9/#12's reporter), downloads 166 MB and then gets an error | load / check ONNX Runtime (and `GetApi (ORT_API_VERSION)`) before the consent dialog |
| R3 | medium | `runtimeLoadError` | one message for every failure: a **missing** `onnxruntime.dll` (bare exe, portable copy without the DLL) says "install the Visual C++ Redistributable" | distinguish "file not found next to LYNX" from "found but failed to load" (Windows: `GetLastError` 126 = a dependency missing → VC++ hint) |
| R4 | medium | `modelFile()` | the model goes to the **settings** folder: Windows `%APPDATA%` (Roaming: 166 MB synced by roaming profiles), Linux `~/.config`. LYNX_PROFILE_DIR is honoured (it becomes `<profile>/LYNX/models`) | Windows `%LOCALAPPDATA%\LYNX\models`, Linux `$XDG_DATA_HOME/LYNX/models`, macOS Application Support (not Caches: purged); keep the profile override |
| R5 | medium | release.yml SignPath list | `signed/onnxruntime.dll` would re-sign **Microsoft's** binary (it is Authenticode-signed by Microsoft Corporation, status Valid): SignPath's OSS terms only allow signing the project's own binaries, and the Microsoft signature is better | leave onnxruntime.dll out of the signing request; copy the original into the package |
| R6 | medium | MSIX (make-msix-layout.py / AppxManifest) | the DLL needs MSVCP140(_1) / VCRUNTIME140(_1); a fresh Windows from the Store has none | add the framework dependency `Microsoft.VCLibs.140.00.UWPDesktop` (the Store installs it, nothing proprietary shipped by LYNX); installer / zip keep the "install the redistributable" message |
| R7 | medium | memory | everything is whole-file: copy + resample (2 ch) + 8 stem channels + weights at 44.1 kHz float ≈ 44 B per output frame ≈ 0.47 GB for 4 min, 1.2 GB for 10 min, plus ONNX Runtime (~1.1 GB per segment per the model card) and the model; no length limit | write the overlap-added part of each stem to the WAV writers as soon as it is final (the window only needs one segment of history) → O(segment); or at least warn above ~15 min |
| R8 | low | `SetIntraOpNumThreads (numCpus − 1)` | logical cores minus one, and ORT's default **spinning** threads keep the cores busy between ops; FL uses all cores, half with "Limit CPU" | `AddSessionConfigEntry ("session.intra_op.allow_spinning", "0")`; a "Limit CPU usage" option like FL |
| R9 | low | `modelFile().existsAsFile()` only | a truncated / damaged model (disk error, antivirus) gives an ONNX error forever, without saying where the file is | check the size (or hash once after download and store a marker); on a CreateSession error offer to delete and download again |
| R10 | low | Linux `runtimeFile()` | `currentExecutableFile` comes from `dladdr` → argv[0]: started by name through `~/.local/bin/LYNX` (install.sh's symlink is resolved, but a plain `LYNX` from PATH resolves against the current directory) the .so is not found | use `/proc/self/exe` (`hostApplicationPath`) on Linux |
| R11 | low | Main.cpp `--stemsplit` | args not checked (missing args → empty paths), result written next to the output folder | check `args.size()`; fine as a test hook |
| R12 | low | `addStemClips` | `clipIndex` + same path is the only identity check; the dialog is modal so the song can't change meanwhile from the UI, but scripts / MIDI controller actions could | store the clip's start/track and match on those too |
| R13 | info | `AudioEngine::defaultRecordingFolder()` (not this PR) | an unsaved project writes the stems to `Music/LYNX/Recordings/Stems`, outside LYNX_PROFILE_DIR | pre-existing: make defaultRecordingFolder honour the test profile |
| R14 | info | i18n | pt-BR done, the 13 other languages in pending.keys (rule: all 15) | we add them on top |
| R15 | info | THIRD_PARTY / PRIVACY | text is accurate (3rd connection, Hugging Face sees the IP, audio never uploaded); README row OK | PRIVACY: say the model file size and that it can be deleted from `models` |

Correct as written (checked): overlap-add (triangular weights that never reach 0, ends of the file keep weight 1),
padding of the last segment, segment size 343 980 = the graph's fixed input, output name/shape check, the ORT objects
released in order (library last), wide-char model path on Windows, Linux download through `curl` (as the updater,
`JUCE_USE_CURL=0`), `.part` + hash + rename, nothing left on Cancel / error, stems on new tracks under the clip with
the clip's offset / speed / pitch / fades and a new insert each (FL's default), `sortClips` after the edit, the fetch
script (`set -euo pipefail`, pinned SHA-256 = GitHub's digests, zip extracted by Python's zipfile which strips `..`).

## 3. Downloads checked (untrusted data, kept in /mnt/e/lynx-work/pr31)

- ONNX Runtime 1.23.2 release assets: the three SHA-256 in scripts/fetch-onnxruntime.sh equal GitHub's own `digest`
  of the assets (`gh api repos/microsoft/onnxruntime/releases/tags/v1.23.2`): win-x64 zip 0b38df9a…, linux-x64 tgz
  1fa4dcae…, osx-universal2 tgz 49ae8e3a…. The script ran here for windows and linux: `onnxruntime.dll` 14 186 016 B
  (Authenticode: Valid, CN=Microsoft Corporation, FileVersion 1.23.20251021.3.a83fc4d), `libonnxruntime.so.1.23.2`
  22 326 072 B (SONAME libonnxruntime.so.1, needs GLIBC 2.27, libstdc++). The DLL imports MSVCP140, MSVCP140_1,
  VCRUNTIME140, VCRUNTIME140_1 (VC++ 2019+ redistributable) and dxgi / dbghelp / setupapi.
- Model: Hugging Face API for adowu/htdemucs-onnx at the pinned revision e6b688a9 (also the repo's current head,
  last modified 2026-07-23): `htdemucs_fp16weights.onnx` 165 612 636 B, LFS sha256 d05c269d… = the PR's constants.
  The model card (licence tag "mit", "first ONNX export of htdemucs", built with the StemSplit/demucs-onnx exporter,
  input `mix` (1, 2, 343980) float32 44.1 kHz, output `stems` (1, 4, 2, 343980) drums / bass / other / vocals,
  ~1.1 GB RAM and ~1.6 s per segment on an M4 Pro) matches what StemExtract.cpp does.
- This machine: VC++ redistributable x64 14.51.36247 installed (HKLM …\VC\Runtimes\x64 Installed=1; msvcp140_1.dll
  and vcruntime140_1.dll in System32). Windows also has its own C:\Windows\System32\onnxruntime.dll 1.17 (Windows ML):
  the PR loads its copy by full path, so that one is never picked up (and API 23 would be refused by 1.17 anyway).

## 4. Status of the findings and measurements (Windows 11, i7 6 cores / 12 threads, 64 GB; other builds running)

Fixed on top of his commits: R1 (e2570b84: own low-priority thread + non-blocking progress window, Cancel = flag +
ORT terminate + download cancel, never a kill; resample checks the flag every 65 536 frames; one job at a time; app
shutdown waits for the job), R2 / R3 (runtime checked before the consent, "file missing" vs "VC++ missing" message with
the aka.ms link), R4 (models in %LOCALAPPDATA%\LYNX\models, $XDG_DATA_HOME/LYNX/models, mac Application Support; the
test profile wins), R6 (abfa1d49: MSIX dependency Microsoft.VCLibs.140.00.UWPDesktop, release.yml's MSIX install
test passes the framework), R8 (threads = physical − 1, at most logical − 2, no spinning), R9 (size check), R10
(/proc/self/exe), R14 (9383b96a: 13 languages), R15 (PRIVACY). Plus aa07837f: no ORT CPU arena. Left as is: R5 (user:
"SignPath nothing now"), R7 (streaming, follow-up; the whole-file buffers are ~0.4 GB for 3 min), R11-R13.

Lightness audit (requirement of the user):
- Startup / idle, fresh profile, same machine, alternated (`idle.ps1`: ready = main window shown, then 15 s, then
  idle window): main (9fab84ca, no stems code) ready 1.85-2.09 s, working set 110-111 MB, private 127-130 MB, 53-58
  threads, 93 modules; fixed build ready 1.82-1.97 s, 110-111 MB, 127-129 MB, 52-57 threads, 93 modules. Idle CPU
  (60 s): main 4.5 / 6.1 s, fixed 5.6 s (the load of the other builds makes this noisy; 20 s windows overlap both
  ways). `onnxruntime.dll` is never in the module list at start (it sits next to LYNX.exe), no .onnx file is read or
  created, no model check at start or on project load: the code only runs from the clip menu item.
- Threads: the job runs on its own low-priority thread; the message thread only polls progress (100 ms timer); the
  audio thread never sees it. ORT: physical − 1 = 5 intra-op threads (PR: logical − 1 = 11), inter-op 1, no spinning.
- `--stemsplit` of a 180 s song (LYNX render of FL's demo "Song For You", 44.1 kHz stereo), run back to back at load
  ~30: PR 209.7 s wall, 1 372 s CPU, peak working set 5 286 MB; fixed 155.5 s wall, 442 s CPU, peak 4 589 MB. At load
  ~10: PR 173.7 s / 1 260 s CPU / 5 316 MB, fixed 137 s / 428 s CPU / 5 285 MB (arena still on).
- Memory (xmem log of a temporary build): session open +410 MB (12.8 s); each 7.8 s segment needs ~4.1 GB of tensors
  for a moment (the same with 1 or 5 threads, with or without graph optimisation / prepacking: it is the model). With
  ORT's arena the process stayed at 4.9 GB until the end (and 5.3 GB with 11 threads); without it (aa07837f) it goes
  back to ~850 MB between segments (session + whole-file buffers) and 506 MB right after the job, before the model is
  released. Same speed.
- Quality: the fixed build's stems equal the PR's within float rounding (SNR 122-134 dB per stem); the sum of the
  four stems rebuilds the mix at 32.9 dB SNR; no time offset (cross-correlation lag 0).
- Windows GUI session (fresh profile on E:, LYNX_TEST_MODE=1, a 180 s song clip, master at 0.15, the temporary build
  with the arena off; shots in /mnt/e/lynx-work/pr31/shots g01-g14):
  - menu: audio clip menu, after Reverse and before Pitch (LYNX has no Time-warp / Deverb yet; FL's place is between
    them); the consent shows the size, the folder (`<profile>\LYNX\models`), the source and the licence text. The
    runtime check before it loads and frees onnxruntime.dll (not in the module list afterwards).
  - download: 166 MB in ~19 s with a progress bar; Cancel during the download closed the window at once and deleted
    the `.part`. The finished download's SHA-256 = d05c269d….
  - with the song playing the whole time: click → stems on 4 new tracks in ~166 s (download 19 s, separation of
    the 3-min song ~147 s); the toolbar's underrun counter stayed empty (0 underruns), CPU meter 1 %, the UI kept
    painting the playhead. During the job: working set 1.4-3.1 GB (peak 4.74 GB), private ~3.0 GB, 64 threads,
    onnxruntime.dll loaded. **After the job: 207 MB working set / 164 MB private (191 / 146 before), 54 threads,
    onnxruntime.dll unloaded.**
  - result: tracks 2-5 "song - drums / bass / instruments / vocals" under the source, aligned with it; the source
    stays unmuted (FL mutes the source track by default). Undo removes the four in one step (the WAVs stay in
    `<project>\Audio\Recorded\Stems`, as FL keeps its files).
  - Cancel during the separation: the window went at once; 1 s later 223 MB / 179 MB, onnxruntime.dll unloaded, no
    stem file written. Playback went on.
  - the main window's close button does nothing while the progress window is up (JUCE ignores closing a window
    blocked by a modal component; the PR's ThreadWithProgressWindow was modal too): Cancel first. The test process
    was ended by its PID.
- Release workflow (workflow_dispatch on wip/pr31-review, run 41461808932 at aa07837f): every job green. The
  ONNX Runtime archives passed their SHA-256 on all three runners, onnxruntime.dll is in the zip, the installer and
  both MSIX layouts, and the test MSIX installed with Microsoft.VCLibs as its dependency and passed the self-test.

