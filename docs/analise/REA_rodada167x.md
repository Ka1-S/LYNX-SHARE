# Round 167x: PR #31 (LordVersA) "Extract stems from sample" (closes #27): review, test, FL comparison

PR head 11d4570b (f51f9dad + merge of origin/main 11d4570b, up to date with main e6e36fdd). Not landed: the user must
first answer the PR's 6 "Decisions for you". This round only reviews, builds, tests and compares with FL.

## Where I stopped

- Done: code review (§2), checksums of ONNX Runtime and of the model (§3), FL 2026 REA (§1).
- In progress: build-local + Windows zig build queued (buildslot, load ~32); then the Windows GUI test (§4).
- Next: Windows run with a fresh LYNX_PROFILE_DIR on E: (consent, download, separation of a LYNX render of an FL demo
  song, Cancel, undo), `--stemsplit` time / RAM, then the reply draft in coord/replies-0.1.22.md "## PR #31".
- Commands: `tmp/coord/buildslot.sh tmp/coord/bl-wt.sh pr31` (log tmp/coord/bl-pr31.log);
  `tmp/coord/winbuild.sh tmp/pr31/winbuild.sh` (log tmp/pr31/win.log); Ghidra: `tmp/pr31/g.sh <out.c> <depth> <addr…>`
  (read-only on the analysed FLEngine project /mnt/e/lynx-work/pr28); data in /mnt/e/lynx-work/pr31.

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

[pending: see the final section after the tests]

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
