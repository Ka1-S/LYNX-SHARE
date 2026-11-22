# JSFX support in LYNX SHARE (issue #241): feasibility, probe, design, estimate

Round 190od, 2026-11-21 (issue #241). Study and probe: LYNX hosts no JSFX yet; this is the design and the estimate for the owner's decision.
Work folder (probe sources, logs, sweep tables, licence evidence): `/mnt/e/lynx-work/issues236_190od/241/`.
- `scripts/`: the probe host, the bench, the test JSFX and the build scripts.
- `out/`: logs, numbers, sweep tables, @gfx renders and licence evidence.

## 1. Summary

- **Licences: OK.** ysfx is Apache-2.0. The parts it bundles are zlib-style (WDL/EEL2/LICE/SWELL), Unlicense/MIT-0 (dr_libs), MIT/Unlicense (stb) and GPL-2+ with the Bison exception (EEL2's parser). All of them can ship inside an AGPL-3.0 program. Nothing is non-commercial or GPL-incompatible.
- **REAPER's stock effects:** do not bundle them as a set. Their licences are mixed, and 85 of the 225 files carry no licence or a restrictive one (§2.3).
- **Probe: works.** All 18 checks of the CLI host pass on Linux and on Windows, the Windows build being cross-compiled with zig. Those checks cover load, compile, `import`, @init/@slider/@block/@sample, a slider change, state save/restore, PDC, MIDI in/out with an instrument, loop caps and file sliders.
- **Sweep: 329 of 329 files.** Every third-party and REAPER stock JSFX I fed it compiled and produced finite output.
- **@gfx renders headless into a BGRA buffer** on Linux and on Windows, with identical output. That is what a JUCE component would blit.
- **CPU:** about 9x the instructions of the same algorithm in plain C, measured with callgrind on a trivial effect. Roughly 100 of those instructions per frame are fixed per-sample dispatch overhead. In absolute terms a trivial stereo effect costs about 0.2–0.4 % of one core at 48 kHz.
- **Windows (zig, x86_64-windows-gnu): builds and runs.** ysfx's GAS port of EEL2's x64 asm assembles with zig's clang, and the Win64 ABI path works.
- **macOS:** every ysfx/EEL2 source compiles for aarch64-macos with zig (compile only, no Mac to run on). EEL2 has an aarch64 code generator. JIT needs either no hardened runtime (today's LYNX) or an entitlement (§6).
- **Recommendation:** go, in the 0.1.31 queue. Host the ysfx **library** (not the ysfx plugin) as one more `juce::AudioPluginFormat`, as ClapFormat already does. Estimate: stage 1 ≈ 9–11 days, stage 2 ≈ 3–4, stage 3 ≈ 5–7, stage 4 ≈ 3–4.

## 2. Licences (evidence: `out/licence-evidence/`, `out/reaper786_stock_jsfx_licences.txt`)

### 2.1 The runtime

Source: shallow clone of github.com/JoepVanlier/ysfx at `5c3452fe` (2026-08-19, version 0.0.47), with its submodules. Upstream github.com/jpcima/ysfx is Apache-2.0 per the GitHub API; it is not archived, but its last push was 2024-07-02 and the fork's README says its maintainer "seems to have disappeared".

| Component (path in ysfx) | Licence (file read) | In an AGPL-3.0 program |
|---|---|---|
| ysfx library `sources/`, `include/` | Apache-2.0 (`LICENSE`; 73 files carry `SPDX: Apache-2.0`; unlabelled files fall under the repo LICENSE) | Yes. Apache-2.0 is compatible with GPLv3 and AGPLv3. Keep the NOTICE/attribution. |
| ysfx JUCE plugin `plugin/` (only needed if its gfx view or panel code is reused) | Source Apache-2.0. Binaries ship under AGPL-3.0 because of JUCE (`plugin_license/`). | Yes |
| WDL `thirdparty/WDL` (EEL2, LICE, SWELL, fft, win32_utf8) | Cockos WDL licence, zlib-style (`thirdparty/WDL/LICENSE.txt`) | Yes. Altered sources must be marked. |
| eel2-gas `sources/eel2-gas` (GAS port of `asm-nseel-x64-sse.asm`) | zlib (`LICENSE.txt`) | Yes |
| EEL2 `y.tab.c`/`y.tab.h` (Bison 2.3 output) | GPL-2+ with the Bison "special exception" | Yes (and GPL-2+ is compatible on its own) |
| dr_libs (`dr_wav`, `dr_flac`; audio files for `file_open`) | Unlicense or MIT-0 | Yes |
| stb (LICE image loaders for `gfx_loadimg`) | MIT or Unlicense | Yes |
| nlohmann json | MIT; not used by the library sources (plugin only) | Not needed |
| clap-juce-extensions, clap, miniaudio (dr_libs tests) | MIT / MIT / Unlicense or MIT-0 | Not needed (plugin wrapper and tests only) |

No piece is GPL-incompatible or non-commercial. `scripts/collect-licenses.sh` would pick up the LICENSE files automatically if vendored as `third_party/ysfx/...`, because all of them sit at depth ≤ 4.

### 2.2 Cockos' own JSFX source

github.com/justinfrankel/jsfx is "the source code to JSFX", licensed **LGPL-2.1** (its LICENSE file). It covers jsfx.dll/.so/.dylib and the legacy ReaJS VST2, and contains no stock effects. LGPL-2.1 is compatible with AGPL-3.0: §3 lets you apply GPL v2 or later, and GPLv3 §13 then allows combination with AGPLv3. It is useful as a reference for REAPER behaviour, but ysfx is the better fit: it is already a host library with a C API and does not depend on REAPER's plugin API.

### 2.3 REAPER's stock effects

I read the headers of all 225 effect files in the REAPER 7.86 Linux tarball (`InstallData/Effects`, extracted from the tarball, nothing run). Classification by header text:

| Licence in header | Files | Where |
|---|---:|---|
| LGPL ("This effect Copyright (C) 2004 and later Cockos Incorporated / License: LGPL") | 50 | analysis, delay, filters, guitar, pitch, loopsamplers, synthesis, utility, misc, … |
| GPL (unversioned "gnu.org/licenses/gpl.html" or v2+) | 52 | IX, Liteon, remaincalm_org, some sstillwell and loser |
| BSD-style (Stillwell, Teej) | 35 | sstillwell, Teej, midi/drumtrigger |
| Custom-restrictive (LOSER/Michael Gruhn: "use … only granted if used in the sense of the author's intention") | 31 | loser |
| No licence notice at all | 54 | all 22 other midi/*, schwa, Till, some utility |
| Other | 3 | |

Verdict: LYNX must **not** bundle REAPER's stock set. 85 files have no grant or a restrictive one, and shipping "REAPER's effects" would also invite confusion with Cockos.

Two things are fine:
- Scanning the user's own REAPER Effects folder when REAPER is installed (`%APPDATA%\REAPER\Effects`, `~/.config/REAPER/Effects`, `~/Library/Application Support/REAPER/Effects`).
- Cherry-picking individual LGPL/BSD files later, if the owner ever wants that. It is not recommended.

### 2.4 Freely licensed JSFX collections

| Collection | Licence (file read) | Bundle in LYNX? |
|---|---|---|
| Saike / Tukan Studios (Joep Vanlier), github.com/JoepVanlier/JSFX (51 .jsfx) | MIT (repo LICENSE; 59 files say `license: MIT`). 5 analyzer files carry Cockos LGPL headers. | Yes. The MIT ones directly; the 5 LGPL ones also allowed. |
| chokehold, github.com/chkhld/jsfx (39 .jsfx) | MIT | Yes |
| SoX Plugins JSFX (Dr. Thomas Tensi), github.com/prof-spock/SoX-Plugins-JSFX (15) | GPL-3.0 | Allowed (GPLv3 + AGPLv3 §13). Ship the source as is. |
| Geraint Luff, github.com/geraintluff/jsfx | Custom: personal/commercial **use** allowed, but "You may not otherwise publicly redistribute this software … without permission" | **No** (users may install it themselves) |

## 3. Probe results

### 3.1 Builds

All builds ran at -j3 under the ≤24 load gate, with `quietwait.sh` before each job.

| Build | Result |
|---|---|
| Linux, gcc 15.2, CMake/Ninja, `YSFX_PLUGIN=OFF YSFX_GFX=OFF` | 30 steps, `libysfx.a` 1.0 MB, 65 s wall |
| Linux, `YSFX_GFX=ON` (LICE + SWELL generic, headless) | 59 steps, 2.7 MB, 82 s |
| Linux, `YSFX_PORTABLE=ON` (EEL2 portable generator, no executable memory) | OK |
| Windows, zig 0.14.1 `x86_64-windows-gnu`, own script without CMake (`scripts/build_win_zig.sh`) | 28 objects without gfx, 43 with LICE; probe host, bench and gfx probe linked |
| macOS arm64, zig `aarch64-macos`, compile only (`scripts/build_mac_arm64_compileonly.sh`) | All 9 EEL2/WDL C files, the .S (empty on arm64) and 18/18 ysfx .cpp compiled to Mach-O arm64. SWELL Cocoa .mm not tried (needs the SDK). |

### 3.2 Functional checks

Source: `scripts/probe_host.cpp`, tests T1–T4. Results: `out/probe_run_mit.txt` (Linux) and `out/win/probe_win.txt` (Windows native, run through WSL interop, console only). All checks pass on both.

**T1: own effect `probe_gainlp.jsfx`**
- Feature coverage: slider with a log curve, enum slider, `import` of a .jsfx-inc, `pdc_delay`, @serialize.
- Against a C reference over 100 blocks, with a slider change at block 50: **max |err| = 0**.
- PDC: `pdc_delay` reads back 32.
- State: `ysfx_save_state` is 3 sliders (doubles) plus 12 bytes of @serialize.
- A state restored into a fresh instance gives the same output within float rounding (max 6e-8 in the first block, 0 after). `file_var` serializes 32-bit floats, as REAPER does.
- A slider-only state restores bit-identical output.

**T2: own instrument `probe_synth.jsfx`**
- MIDI in through `ysfx_send_midi`: note 69 gives **440.00 Hz** with the expected peak and release envelope.
- MIDI out through `ysfx_receive_midi`: the note is re-sent an octave up (81).
- The MIDI offset reaches `midirecv`. Sample-accurate timing is up to the script, as in REAPER.

**T3: runaway loops**
- An infinite `while(1)` and a 1e6 `loop()` both return, because EEL2 caps each loop at 1,048,576 iterations.
- The cost is real CPU: 7–35 ms per 512-sample block, wall clock on the loaded machine. A hostile or buggy script can cause dropouts, not hangs.

**T4: file access**
- A `slider1:/probe_data:none:` file slider lists `values.txt`, and `file_var` reads 0.25.
- **A script can open any absolute path given as a string**: `file_open("/etc/hostname")` succeeded on Linux.
- A relative `../../..` path was refused.
- Script file APIs are read-only. ysfx exposes no write, exec or network functions to scripts.

### 3.3 Compatibility sweep (T6)

Every file was loaded, compiled without @gfx, run for 220 blocks of noise at 48 kHz/512, and checked for finite output.

| Set | Files | Compiled | Finite output | Have @gfx | Compile ms median / max | µs per 512 block median / p90 / max |
|---|---:|---:|---:|---:|---|---|
| MIT (Saike + chkhld) | 90 | 90 | 90 | 52 | 32 / 5550 (Tight_Compressor) | 301 / 1717 / 6067 |
| REAPER 7.86 stock (local test only) | 224 | 224 | 224 | 51 | 12 / 272 | 42 / 226 / 1467 |
| SoX Plugins (GPL-3) | 15 | 15 | 15 | 15 | 223 / 513 | 372 / 2947 / 4185 |

Two caveats on the times:
- They are wall clock on a busy 12-thread box (load 20–28), so they are indicative only.
- Compile times include reading through WSL's /mnt/e. The same probe effect compiled in 1.4 ms on Windows vs 25 ms in WSL.

Big Saike scripts take seconds to compile, so compiling must happen off the message thread and off the audio thread. Tables: `out/t6_sweep.tsv`, `out/sweep_stock/t6_sweep.tsv`, `out/sweep_sox/t6_sweep.tsv`.

### 3.4 CPU

`scripts/bench.cpp` under callgrind (`out/cg/`) measures the instructions of a 200-block difference, so setup is excluded. Stereo, 512-frame blocks.

| Code | Instructions per frame |
|---|---:|
| Plain C, gain + one-pole LP | 21 |
| JSFX JIT, gain + one-pole LP | **192** (≈ 9.1x) |
| Plain C, gain only | 13 |
| JSFX JIT, gain only | 123 |
| JSFX **portable** (no JIT), gain + LP | 738 (≈ 3.9x the JIT) |

Where the JIT's 192 instructions per frame go:
- 90 in the generated @sample code itself, since EEL2 keeps every variable in memory.
- 59 in `ysfx_process_generic`: per-sample float↔double copies.
- 42 in `NSEEL_code_execute`: one call per sample.

So the floor is about 100 instructions per frame per instance. It is a fixed cost and does not grow with DSP complexity.

Wall clock for the same JIT effect:

| Platform | Gain + LP | Plain C | Share of one core at 48 kHz |
|---|---|---|---|
| Linux | 64–78 ns/frame | 2.6–2.8 ns | 0.3–0.4 % |
| Windows | 45 ns/frame | 2.4 ns | 0.22 % |

Per-block overhead is small: from block 32 to 1024 the cost per frame stays flat within noise.

### 3.5 @gfx

`scripts/gfx_probe.cpp` sets up `ysfx_gfx_config_t` with a 640x400 BGRA memory buffer and calls `ysfx_gfx_run` with mouse state:
- An own drawing test (rect, text in LICE's built-in font, circle) and Saike's Tight_Compressor UI render correctly on Linux and on Windows. See `out/gfx/*.png` and `out/win/*.png`.
- Frame cost: 0.07–0.3 ms for the simple UI and about 6.5 ms for Tight_Compressor (each frame includes one audio block).

## 4. Windows and macOS build findings

**Windows (zig/MinGW): yes.**
- The non-MSVC path of ysfx uses `sources/eel2-gas/sources/asm-nseel-x64-sse.S`. It has `#if !defined(_WIN32)` → AMD64ABI switches, so the same file serves SysV and Win64, and zig's clang assembles it.
- The upstream WDL guard already covers `_WIN64` for MinGW. The MinGW patch from ysfx's `resources/patches` is already applied in the vendored copy.
- Needed for linking: `-lcomdlg32 -lshell32 -lole32 -luser32 -lgdi32`, plus `-lmsimg32` with LICE. LYNX's link already has most of these through JUCE.
- `_USE_MATH_DEFINES` is needed for `M_PI` in host code.
- ysfx's own CI also builds MinGW64 (msys2), MSVC x64/Win32 (nasm) and macOS universal.
- The Windows probe ran natively and passed every check.

**macOS: very likely yes; not executed.**
- EEL2 selects `glue_aarch64.h` / `asm-nseel-aarch64-gcc.c` on `__aarch64__`. Zig compiles the whole core for aarch64-macos, and ysfx's CI ships universal (x86_64;arm64) builds.
- JIT memory: EEL2 `mmap`s RW, then `mprotect`s RX and calls `sys_icache_invalidate`. That is W^X compliant (never RWX) but uses no `MAP_JIT`. LYNX's app has `HARDENED_RUNTIME_ENABLED FALSE` and an ad-hoc signature, so it should work as is. Confirm with a CI self-test on the macos-15 runner.
- If LYNX is ever notarized (hardened runtime), it needs `com.apple.security.cs.allow-unsigned-executable-memory`; `allow-jit` alone is not enough without `MAP_JIT`. The alternatives are a patch to EEL2 (MAP_JIT + `pthread_jit_write_protect_np`) or the portable generator (≈ 3.9x the instructions, needs no executable memory).
- @gfx on macOS needs SWELL's Cocoa `.mm` files, which use the Cocoa/Carbon/Metal frameworks. ysfx's JUCE plugin links JUCE and SWELL together, so coexistence is proven upstream.

**Linux:**
- @gfx builds headless with SWELL generic, plus fontconfig/freetype if present.
- Nothing needs GTK/GDK.
- All library symbols are built with hidden visibility.

## 5. Integration design

### 5.1 Principle

Add a **`JsfxPluginFormat : juce::AudioPluginFormat`** next to `ClapPluginFormat` and `Vst2PluginFormat`. ClapFormat.h says it plainly: "scanning, the plugin list, the plugin window, saved state, latency (PDC) and parameter automation all work through the same path as VST3". A JSFX instance is then an `AudioPluginInstance`, so everything below LYNX's plugin layer is reused unchanged.

**Reused unchanged:**
- `PluginInstrument` / `PluginEffect` adapters: notes become MIDI at chunk starts; mixer slots and channel-rack generators.
- `PluginRef.state` (base64 of `getStateInformation`) in the project.
- Automation and MIDI learn through `HostedParameter`.
- PDC through `getLatencySamples`.
- Plugin database, browser, favourites, user names, Plugin manager.
- `PluginWindow`, including its generic-editor fallback for plugins without a UI (`PluginWindow.cpp:1406`, `juce::GenericAudioProcessorEditor`).
- Preset bar, if banks are mapped to programs.
- Optional "Make bridged" sandbox: the child calls `addPluginFormats`.

**Precedent for an embedded script runtime:** VFX Script (`src/core/VfxScript.cpp`, pocketpy with `PK_ENABLE_OS=0` and a watchdog). The same stance applies here: the runtime gets no OS access beyond what LYNX allows.

### 5.2 Files to add or change

| File | Content |
|---|---|
| `third_party/ysfx/` (new) | ysfx pinned at a commit, trimmed to `include/`, `sources/`, `thirdparty/WDL/source/WDL/{eel2,lice,swell,fft.*,win32_utf8.*,+headers}`, dr_libs (dr_wav.h, dr_flac.h), stb (image headers), their LICENSE files. About 4–5 MB of source. LYNX changes marked "LYNX", as with pocketpy. |
| `CMakeLists.txt` | `add_library(lynx_ysfx STATIC …)` mirroring ysfx's `cmake.wdl.txt`/`cmake.ysfx.txt`: the GAS `.S` on non-MSVC, `-fsigned-char`, `WDL_FFT_REALSIZE=8`, `NSEEL_ATOF`/`WDL_LINEPARSE_ATOF=ysfx_wdl_atof`, `YSFX_NO_FTS` on Windows, `YSFX_NO_GFX` until stage 3; linked into `lynx_core`. |
| `scripts/build-windows-zig.sh` | Same file list (proven by `scripts/build_win_zig.sh`), `-lcomdlg32 -lmsimg32`. |
| `src/engine/JsfxFormat.h/.cpp` (new, ≈ 900–1200 lines like ClapFormat) | See §5.3. |
| `src/engine/JsfxEditor.h/.cpp` (stage 3) | See §5.6. |
| `src/core/JsfxRoots.h/.cpp` (new) | JSFX roots and identity: `Documents/LYNX/JSFX` (user), the bundled pack folder, the REAPER Effects folder if present, and user-added folders. Maps a relative id ↔ an absolute path. Holds the file-access policy (§6). |
| `src/engine/ClapFormat.cpp` `addPluginFormats` | `manager.addFormat (new JsfxPluginFormat())` |
| `src/engine/PluginHost.cpp` | `.jsfx` in the `hosted` file filter. JSFX roots in `suggestedFolders()`/`pluginFolders()`. In-process scan for JSFX (`isTrivialToScan() == true`, so no `--scan-one` child). Format label in the Plugin manager. |
| `src/engine/HostingTests.cpp` / `--selftest` | Port of the probe: load, compile, process vs reference, slider automation, state round trip, PDC, MIDI, file slider, loop cap. A tiny test JSFX lives in the tests' resources. |
| `THIRD_PARTY.md`, `resources/licenses` | ysfx/WDL/eel2-gas/dr_libs/stb entries. `collect-licenses.sh` picks the files up as is. |
| i18n (14 tables) | Format name, compile-error messages, panel labels, file-policy prompt. |

### 5.3 `JsfxFormat.h/.cpp` in detail

**Scan:**
- Files `*.jsfx`, plus extensionless files whose first lines contain `desc:`, as in REAPER's Effects.
- Parse the header only: `ysfx_load_file` with `ysfx_load_ignoring_imports`, no compile.
- From the header: name (`desc:`), author, tags → category, pins → I/O.
- `isInstrument` when there are no input pins or a tag says instrument/synth. Those files are listed twice, as Effect and as Instrument, as ysfx's plugin does.

**Identity:** `fileOrIdentifier` = absolute path; `uniqueId` = hash of the path **relative to its JSFX root**. A project made on another PC can then find "utility/volume" in any root. This uses the same idea as LYNX's portable identity for Online Sessions.

**`JsfxInstance : juce::AudioPluginInstance`:**
- **Creation:** compile in `createPluginInstance` on the loader thread, since some scripts take seconds. Compile errors become the failure string shown in the UI.
- **`prepareToPlay`:** `ysfx_set_sample_rate`/`set_block_size`/`ysfx_init`, never on the audio thread.
- **`processBlock`:**
  - Apply the queued slider changes with `ysfx_slider_set_value`. ysfx's @slider must not run concurrently with @sample, so changes are applied on the audio thread at block start.
  - Then `ysfx_set_time_info` from the AudioPlayHead, MIDI in, `ysfx_process_float`/`_double`, MIDI out.
  - Then `ysfx_fetch_slider_automations`/`changes`/`visibility`, posted to the message thread.
- **Parameters:** one `HostedParameter` per existing slider (up to 256), using ysfx's curve functions (linear/log/sqr) for normalisation.
  - Enum sliders become choice parameters; file sliders become choices over the listed files.
  - Hidden (`-`) sliders stay automatable but are not shown.
  - `slider_automate()` from the script → `setValueNotifyingHost`, so it records automation as in REAPER.
- **PDC:** `setLatencySamples(round(pdc_delay))` after @init/@slider. `pdc_bot_ch/top_ch` (partial-channel PDC) cannot be expressed per channel in LYNX, so it is treated as whole-plugin latency (documented).
- **State:** `getStateInformation` writes:
  - a magic and a version;
  - the relative id;
  - a source hash, to warn when the script changed;
  - the sliders as doubles;
  - the @serialize blob (`ysfx_save_state`).

  It runs under the processor's callback lock, as ysfx's plugin does. `setStateInformation` → `ysfx_load_state`.
- **Presets:** REAPER `.rpl` banks (`ysfx_load_bank`) are exposed as programs.

### 5.4 MIDI (stage 2)

- **In:** the `MidiBuffer` from `PluginInstrument` (notes, CC, pitch bend; MPE when on) → `ysfx_send_midi` with the sample offset, on bus 0.
- **Out:** `ysfx_receive_midi` → the plugin's output MIDI, which `PluginRef.midiOutPort` already routes.
- **Other buses:** `ysfx_max_midi_buses = 16` exists. It is not needed at first.

### 5.5 Resources

- `import` and `.jsfx-inc` files resolve through `ysfx_set_import_root` (the root the effect was found in) and the effect's own folder.
- `filename:` and file sliders resolve through `ysfx_set_data_root`: `<root>/../Data`, REAPER's convention, or a LYNX data folder.
- Audio files (`file_open` on wav/flac) are read through dr_libs.

### 5.6 @gfx (stage 3)

- `JsfxEditor` is a `juce::AudioProcessorEditor`. It owns a `juce::Image` (ARGB) whose BitmapData is the ysfx pixel buffer (BGRA little-endian), and calls `ysfx_gfx_run` from a timer at `ysfx_get_requested_framerate()`. ysfx requires one dedicated UI thread: the message thread, or a worker that blits, as ysfx's `graphics_view.cpp` does.
- Inputs: mouse, wheel and keys → `ysfx_gfx_update_mouse`/`add_key`. `show_menu` → `juce::PopupMenu`; drag-and-drop → `get_drop_file`. Retina → `scale_factor`.
- A button switches between the @gfx view and the slider panel, as in REAPER.
- ysfx's Apache-2.0 `plugin/components/graphics_view.cpp` can be adapted, with attribution.
- The perf gates apply (idle CPU, `--paintbench`). Effects like Tight_Compressor cost ~6 ms per frame, so the editor must stop rendering when hidden (`ysfx_gfx_set_window_state`).

## 6. Risks

- **JIT on macOS:** see §4.
  - Today: no hardened runtime, so OK (to confirm on the macOS CI runner).
  - With notarization: entitlement `allow-unsigned-executable-memory`, or an EEL2 MAP_JIT patch, or the portable fallback (≈ 3.9x instructions).
  - Windows: `VirtualAlloc RW` → `VirtualProtect RX`, no special policy needed. Arbitrary Code Guard (ACG) is not enabled by LYNX.
- **Security:** a JSFX is code that runs inside LYNX.
  - **File reads:** proven — absolute paths given as strings are opened (read-only; no write, exec or network API for scripts). A shared project normally carries only an id plus state, not code, unless LYNX embeds sources (owner decision below).
  - **Mitigation (recommended, small patch in `ysfx_find_data_file`):** allow only the JSFX roots, the data root and the project folder. Treat third-party folders like plugins: the user chose to install them.
- **CPU and real-time safety:**
  - EEL2's loop caps prevent hangs, not overloads (T3: up to 35 ms in one 10.7 ms block).
  - EEL2 memory is allocated lazily in 64K-entry blocks (calloc of 512 KB) on first touch, which can happen on the audio thread. Up to 8M doubles by default, 128M with `options:maxmem`.
  - @init can run on the audio thread after a delayed init. `file_open` I/O in @init/@slider runs wherever those run (REAPER behaves the same).
  - Mitigations: compile and init off the audio thread, LYNX's existing CPU-overload/smart-disable handling, and optionally `options:prealloc`.
- **Compatibility:** 329 of 329 tested scripts compile and run. REAPER-only APIs are stubbed in `ysfx_api_reaper.cpp`, so some scripts that query REAPER may behave differently. Partial-channel PDC is approximated.
- **Maintenance:** the ysfx fork has one active maintainer. Pin a commit, keep LYNX changes minimal and marked, and use Cockos' LGPL JSFX source as the behaviour reference.
- **Symbol and size cost:** about +1 MB of library without gfx, +2.7 MB with LICE/SWELL. Symbols are built hidden.

## 7. Stages and estimate (working days, one developer, including tests and CI for all three OSes)

| Stage | Scope | Days |
|---|---|---:|
| 1 | Vendor ysfx + CMake + zig script (1); `JsfxFormat` (scan, identity, instance, parameters, state, PDC, automation, compile off-thread, errors) (4); roots/folders/Plugin manager/browser label + file policy (1.5); slider panel: start with the existing generic editor, then a JSFX-aware panel with enums, file sliders and hidden sliders (1.5); tests + `--selftest` + Windows/macOS CI fixes (1.5–2); i18n/docs (0.5) | **9–11** |
| 2 | Instrument listing, MIDI in with offsets, MIDI out to `midiOutPort`, time info, `.rpl` banks as programs, tests | **3–4** |
| 3 | LICE/SWELL in the builds (Linux generic, Windows GDI, macOS Cocoa .mm), `JsfxEditor` (blit, input, menus, drops, retina, fps, hide = stop), perf gates, tests | **5–7** |
| 4 | Curated free pack (Saike + chkhld MIT, optionally SoX GPL-3), per-file licence headers kept, attribution in THIRD_PARTY.md, compile test of every bundled file in `--selftest`, browser category, CPU numbers per effect | **3–4** |
| | **Total** | **20–26** |

Stages 1 and 2 together (≈ 2.5 weeks) already answer the issue's request: run JSFX effects and instruments, on Linux too.

## 8. Recommendation

Do it. Queue it for 0.1.31; the 0.1.30 scope is closed.
- Use the ysfx **library**, as a LYNX-owned `JsfxPluginFormat` in the CLAP/VST2 pattern. Do not ship ysfx's own plugin.
- Ship stage 1+2 first. Stage 3 next, because about 58 % of the MIT scripts and all SoX scripts have @gfx. Stage 4 last, as a small MIT pack.
- Do not bundle REAPER's stock effects. Offer to scan a local REAPER Effects folder.

**Owner decisions (recommended option first):**
1. **File access policy:** restrict script file reads to the JSFX roots, the data root and the project folder (recommended), or REAPER-compatible unrestricted.
2. **Bundled pack:** MIT only (Saike + chkhld, recommended), or MIT + SoX GPL-3, or none.
3. **Portable projects:** store relative id + source hash only (recommended; warn if missing or changed), or also embed the script source in the project. Embedding raises the security question for shared projects.
4. **Slider panel look:** JSFX is not an FL feature, so there is no FL reference. Recommended: LYNX's existing generic plugin panel style, extended for enums, file sliders and hidden sliders. Alternative: a REAPER-style list.

## 9. Reproduce

```
scripts/build_linux.sh <ysfx> <build> OFF|ON          # CMake/Ninja, load gate, quietwait
scripts/build_linux_portable.sh <ysfx> <build>
scripts/build_win_zig.sh <ysfx> <out> 0|1 <scripts>   # zig x86_64-windows-gnu, lib + probe_host.exe/bench.exe
scripts/build_mac_arm64_compileonly.sh <ysfx> <out>
g++ -std=c++17 -O2 -I<ysfx>/include scripts/probe_host.cpp <build>/libysfx.a -lpthread -ldl -o probe_host
probe_host scripts/jsfx out [list.txt]                # T1–T6
bench <file.jsfx> jsfx|c 0|1 <blocks>                 # run under callgrind for instruction counts
gfx_probe <file.jsfx> <out.ppm> 640 400               # headless @gfx (needs the GFX=ON lib)
```

`out/probe_run0_before_test_fixes.txt` is the first run. Its 4 FAILs were bugs in the probe's own expectations, not in ysfx:
- it expected a sample-accurate note start from a script that reads MIDI in @block;
- it expected bit identity after a float32 @serialize;
- its zero-crossing count was too coarse;
- its release window was too early.

The downloaded sources (ysfx, the collections and the REAPER tarball) were deleted after the study; licence texts and header samples are kept in `out/licence-evidence/`. ysfx was read at commit `5c3452fee62583aa3d1b7e877d0c758c4024af89`.

## 10. Stages 1–2 as built (round 190pj, 0.1.32)

Branch `jsfx_190pj` (off `wip/rollup031`). What the design above asked for stages 1 and 2, and where it is:

| Part | Where |
|---|---|
| ysfx vendored (Apache-2.0), trimmed: no @gfx (LICE / SWELL / stb), no plugin, tests or tools; one LYNX change, a host file filter | `third_party/ysfx` (`README-LYNX.md`), `CMakeLists.txt` target `lynx_ysfx` (linked into `lynx_core`), `scripts/build-windows-zig.sh`, `THIRD_PARTY.md` |
| `JsfxPluginFormat` (scan in LYNX's own process, identity, instance, parameters, state, PDC, MIDI in / out, `.rpl` banks as programs, compile errors as the failure message) | `src/engine/JsfxFormat.h/.cpp`; registered in `addPluginFormats` (`src/engine/ClapFormat.cpp`) |
| Identity, file policy and saved state, JUCE-free | `src/core/Jsfx.h/.cpp` |
| Roots: Documents/LYNX/JSFX, the bundled pack (`<settings>/JSFX pack`), REAPER's Effects folder when it is there, Settings › File › JSFX folders | `jsfx::roots()`; Settings list in `src/ui/SettingsDialog.cpp` (`jsfx-folders.txt`) |
| Scan: the JSFX roots only (not the VST folders), in process (`isTrivialToScan`), on the start-up warm-up thread and with every plugin scan; scripts gone from the roots leave the list | `PluginHost::scanJsfx`, `PluginHost::scanFiles`, `listPluginFiles` |
| A project made on another computer finds the script by its relative path (the unique id is a hash of it; JUCE's identifier also carries a hash of the absolute path) | `jsfx::findPortable`, used by `PluginHost::find` |
| Slider panel (the plugin window's editor) | `src/ui/JsfxPanel.cpp` |
| Browser category: Plugin database › Generators › JSFX and › Effects › JSFX | `src/ui/BrowserComponent.cpp` |
| Bundled pack: chokehold's 31 MIT scripts without @gfx, embedded in the executable and written to the settings folder when the pack changes | `resources/jsfx/chokehold`, `tools/make_jsfx_pack.py` → `src/engine/JsfxPackData.h` |
| Save a copy for 0.1.30 / 0.1.28: JSFX slots become empty (an insert a switched-off Utility in its place, a channel an empty Sampler), one line says so | `jsfxFor0130` in `src/engine/ProjectDowngrade.cpp` |
| Tests: core (`tests/Jsfx190pjTests.inc`: ids, paths, headers, state, the embedded pack, the runtime against upstream ysfx's render) and the app (`--selftest` / `--jsfxtest`: the format end to end) | `jsfx::selfTest`; the reference: `tools/jsfx_ref.cpp` → `src/engine/JsfxReference.h` |
| CPU per instance | `LYNX --jsfxbench <out.txt>` (every bundled script) |
| Shots | `LYNX --jsfxshots <folder> [--shot-scale 1.5 --shot-suffix @150]` |

### Owner decisions taken (the recommended option of §8 each)

1. **File access:** restricted. A script's `file_open`, file sliders and (later) `gfx_loadimg` open files only inside the
   JSFX roots, their `Data` folders (`<root>/Data` and REAPER's `<root>/../Data`) and the open project's folder. The path
   is resolved (symlinks and `..`) before the check. Scripts still have no write, exec or network API.
2. **Bundled pack:** MIT only. This round ships chokehold's MIT scripts that have no @gfx display (31 of 39); the 8 with
   @gfx and Saike's MIT set (mostly @gfx) wait for stage 3, where their displays can be drawn. No SoX (GPL-3) and never
   REAPER's mixed-licence stock set; a local REAPER Effects folder is scanned when it is there.
3. **How projects store scripts:** the relative id and a hash of the script's source, with the slider values and the
   @serialize data; never the source. A missing script is a missing plugin as for any format; a script whose source
   changed still loads and its panel says the settings may not fit.
4. **Slider panel:** LYNX's generic plugin panel look (dark panels, red accent): one row per slider the script shows, a
   bar for numbers (the script's own curve: linear / log / sqr; double-click resets, Ctrl drags fine), a menu for enum
   and file sliders; hidden sliders (`-` names, `slider_show`) are left out but stay automatable. A header names the
   script, its author and id, and shows its PDC when it has one.

### Behaviour notes

- Compiling happens where the instance is made (LYNX's loader thread for a project open); @init runs in `prepareToPlay`,
  and on the audio thread only where REAPER runs it too (a transport start, a script's delayed init).
- Sliders changed by the host or the panel are applied at the start of the next block (ysfx's @slider must not run
  beside @sample); `slider_automate` from a script reaches the host from the message thread, so it records automation.
- The state is taken under the processor's callback lock, as ysfx's plugin does (@serialize is script code); a block
  that meets it plays dry, as for a state restore.
- Latency changes the script makes while it runs are applied on the message thread (`setLatencySamples` informs the
  listeners); the engine reads the plugin's latency as for any other.
- Buses: stereo in / stereo out for an effect, stereo out for a generator. Scripts with more pins get silence on their
  extra inputs (side-chain pins: a later stage).
- Not in stages 1–2: @gfx (stage 3), the pack's @gfx scripts and Saike's set (stage 4), per-channel PDC.

### Measured (Linux, Ryzen 5 3600, round 190pj)

- **Reference render:** chokehold's soft_clipper through a LYNX mixer slot (`PluginEffect`) is bit-identical to ysfx run
  directly and to upstream ysfx's own output (`tools/jsfx_ref.cpp`): 64 blocks of 512 frames, a slider change at block
  32, all 32,768 frames equal.
- **CPU per instance** (`--jsfxbench`, ns per stereo frame at 48 kHz, 512-frame blocks, two runs): the lightest bundled
  scripts (volume, pan, MIDI chord trigger, DC filter) 25–65 ns, i.e. 0.12–0.3 % of one core in real time, which is the
  fixed per-instance floor of §3.4; mid-weight effects (delay, EQ, dynamics, lo-fi) 400–770 ns (2–3.7 %); soft_clipper
  with 2x oversampling 1.3–1.4 µs (6.5 %); amp_sim and cabinet_sim (a long convolution) 3.5–3.7 µs (17–18 %), the
  scripts' own DSP.
- **Gates:** 14 gold renders (renderwav, 11 FL demos' busiest patterns and 3 whole songs) byte-identical to the base;
  `--paintbench` on Thief: every view's instructions within ±0.5 % of the base (noise), pixels identical except the
  desktop frame, whose Browser now lists the bundled JSFX scripts.
