# Platforms

LYNX SHARE runs on Windows, macOS and Linux from one code base. The core (`src/core`, `src/flp`) is plain C++20 without
JUCE and builds anywhere; the app uses JUCE 8.0.4. Everything that differs per OS sits behind a few small helpers in
`src/platform/`, so the rest of the code never spells out `%APPDATA%` or an `HWND`.

| | Windows | macOS | Linux |
|---|---|---|---|
| Builds | Zig cross build from Linux/WSL (`scripts/build-windows-zig.sh`, used for releases) or CMake + MSVC | CMake + Apple clang, universal arm64 + x86_64 | CMake + GCC or Clang |
| Package | per-user installer (NSIS) + portable .zip | .dmg (ad-hoc signed, **not notarized**) | AppImage + .tar.gz (`install.sh`) |
| Minimum | Windows 10 x64 | macOS 11 Big Sur | x86_64, glibc 2.35 (Ubuntu 22.04, Debian 12, Fedora 36), X11 or XWayland |
| Audio drivers | Windows Audio (WASAPI shared / low-latency / exclusive), DirectSound, ASIO, built-in LYNX ASIO | Core Audio | PipeWire (LYNX SHARE's own client, loaded at run time), JACK, ALSA ([LINUX.md](LINUX.md)) |
| Plugin formats | VST3, CLAP, VST2 | VST3, CLAP, Audio Units | VST3, CLAP, LV2, VST2 |
| Plugin sandbox (separate process) | yes (opt-in) | yes (opt-in, since 0.1.25) | yes (opt-in) |
| Settings | `%APPDATA%\LYNX` | `~/Library/Application Support/LYNX` | `$XDG_CONFIG_HOME/LYNX` (`~/.config/LYNX`) |
| Waveform cache | `%APPDATA%\LYNX\wavecache` | `~/Library/Caches/LYNX/wavecache` | `$XDG_CACHE_HOME/LYNX/wavecache` (`~/.cache/LYNX`) |
| Moving playheads while playing (`src/ui/MotionOverlay.h`) | DirectComposition visual (on) | Core Animation layer (**off by default**, `LYNX_OVERLAY_MAC=1`; checked headless on CI only) | drawn by the X server with XRender (on) ([LINUX.md](LINUX.md)) |
| Crash report + emergency save | `%APPDATA%\LYNX\autosave` | `.../Application Support/LYNX/autosave` | `~/.config/LYNX/autosave` |
| FL Studio installation (factory samples, plugin database) | registry, else `Program Files\Image-Line\FL Studio*` | `/Applications/FL Studio*.app/Contents/Resources/FL` (layout not confirmed) | a Windows FL Studio under Wine: `$WINEPREFIX` or `~/.wine`, `drive_c/Program Files/Image-Line/FL Studio*` |

`LYNX_FL_INSTALL=<folder>` points LYNX SHARE at an FL Studio installation anywhere (the folder that contains `Data`).
Importing `.flp`, `.als` and `.flm` projects works everywhere; the FL Studio installation only adds FL's factory samples and plugin database.

## Platform helpers (`src/platform/`)

- `Paths.h` — `settingsFolder()`, `settingsFile()`, `cacheFolder()`, `standardPluginFolders()` (VST3/VST2/CLAP/AU/LV2
  folders per OS, scanned by `PluginHost`).
- `NativeEmbed.h` — the native child view a CLAP or VST2 editor draws into: a child `HWND` (Windows), an `NSView`
  (macOS), an X11 window through `juce::XEmbedComponent` (Linux), plus the matching CLAP window API.
- `FileTypes.h` — `.lynx` file association (Windows registry; the Linux packages ship a `.desktop` file and a MIME type,
  the macOS bundle declares the document types in its Info.plist). The AppImage installs its own menu entry and MIME
  type into `~/.local/share` when the user turns it on (setup wizard, *Options › General*) and keeps the entry's path
  current when the AppImage file moves.
- `MingwFixes.cpp` — only for the MinGW cross build.

Other per-OS code, kept small and guarded in place:

- `core/WorkerPool.cpp` — the audio worker wake-up semaphore: Win32 semaphore, libdispatch semaphore (macOS), POSIX
  `sem_t` on a futex (Linux). All wake a sleeping worker at once (std::counting_semaphore is avoided: MinGW's libc++
  polls with Sleep()).
- `engine/AudioEngine.cpp` — priority of the audio workers: MMCSS "Pro Audio" (Windows), the user-interactive QoS
  class (macOS), SCHED_FIFO 70 when the user's limits allow it (Linux; otherwise unchanged).
- `engine/PipeWireDevice.cpp` — Linux: the native PipeWire driver (`libpipewire-0.3.so.0` through dlopen, vendored MIT
  headers in `third_party/pipewire`). `platform/LinuxRealtime.cpp` — Linux: real-time priority for the audio threads
  through RLIMIT_RTPRIO, RealtimeKit or the desktop portal (`libdbus-1.so.3` through dlopen); no-ops elsewhere.
- `engine/DllAsioDevice.cpp`, `asio/` — ASIO and the LYNX ASIO driver: Windows only (ASIO is a Windows driver model).
- `engine/PluginSandbox.cpp` — out-of-process plugins: Win32 shared memory and events (Windows), POSIX shared memory
  (`shm_open`) and process-shared semaphores (Linux), the same shared memory with two named FIFOs as doorbells (macOS);
  a `pidfd` (Linux) or a kqueue (macOS) notices a crashed child at once.
- `engine/Vst2Format.cpp` — VST2 hosting loads Windows DLLs and Linux shared objects (`.so`, yabridge bridges included); not macOS (VST2 is deprecated and rare there).
- `ui/WindowOwner.cpp` — dark native title bar and owned tool windows (Windows); JUCE defaults elsewhere.
- `ui/Theme.cpp` — Segoe UI on Windows; the system sans-serif elsewhere. On Linux (JUCE's software renderer) knobs and
  rounded plates are drawn from cached pre-rendered sprites, which are much cheaper there than path filling.
  Linux fonts (issue #142): JUCE 8.0.4 lists the installed fonts from the `<dir>` entries of `/etc/fonts/fonts.conf`
  only, without its `<include>`s, so on NixOS (each font package in its own `/nix/store` folder, added by an included
  file) it found DejaVu Sans alone and no monospaced font; a font whose family is not installed has no typeface on
  Linux, and drawing text with one crashed (0.1.25). LYNX now hands JUCE fontconfig's own folder list (through JUCE's
  `JUCE_FONT_PATH`; a value the user set is kept), gives every font without a typeface the bundled Open Sans
  (`LookAndFeel::getTypefaceForFont`), and JUCE fix 19 (`cmake/LynxPatchJuce.cmake`) makes a font without a typeface
  draw nothing instead of crashing. `FONTS=none scripts/check-appimage-start.sh <AppImage>` starts the AppImage with
  no font on the system; `--selftest` reports `fonts=`.
- `ui/Toolbar.cpp` — process memory (Windows API, `/proc/self/statm` on Linux).

## Floating point

Every build compiles with `-ffp-contract=off` (no fused multiply-add), so the exact FL engines keep the operation
order of the Windows build on CPUs with FMA (Apple Silicon, newer x86). Results can still differ in the last bits
between operating systems because each system's maths library (`sin`, `exp`, `pow`...) rounds differently; the
round 117 notes (`docs/analise/REA_rodada117.md`) list the measured differences.

## Building

- Windows (release): `scripts/build-windows-zig.sh` from Linux/WSL.
- Linux: see README ("Build from source"). Release builds run in an Ubuntu 22.04 container with GCC 13
  (toolchain PPA) and a static libstdc++/libgcc, so they need only glibc 2.35; `scripts/check-linux-compat.sh <binary>`
  checks that (highest GLIBC_ symbol version, no system libstdc++) and `scripts/check-appimage-start.sh <AppImage>`
  starts the AppImage offline on Xvfb like the AppImage catalog test does; `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target LYNX`,
  then `scripts/package-linux.sh build/LYNX_artefacts/Release/LYNX <label> dist`.
- macOS: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" && cmake --build build --target LYNX`,
  then `scripts/package-macos.sh build/LYNX_artefacts/Release/LYNX.app <label> dist`.
- CI: `.github/workflows/tests.yml` runs the core tests on Linux (GCC, Clang) and macOS and builds + self-tests the
  app on Linux and macOS; `release.yml` builds all three platforms on a tag and attaches the packages and
  `SHA256SUMS.txt` to the draft release.

## Release optimisation

Measured in round toolchain190gt with `perfbench` (round 190ge: the engine's render as the audio callback does it,
instructions and cycles per sample, a hash of every output sample), the same source built several ways, interleaved; on
Windows `perfbench.exe` linked from a Windows build's own objects (thread cycles).
- **Windows** (`scripts/build-windows-zig.sh`): `-O2`, `-ffp-contract=off` (as Linux and macOS; it was missing), and a
  link that drops unreferenced code and folds identical functions (`-OPT:REF -OPT:SAFEICF`, objects with
  `-ffunction-sections -fdata-sections -faddrsig`; see the script for why zig needs a hand here). LYNX.exe 53.8 →
  48.2 MB (`.text` −9 %, `.rdata` −13 %), the same output, render cost within the run-to-run noise (±5 %).
  - ThinLTO (`LYNX_WIN_LTO=thin`): streamed audio −14 %, synth100 −4 %, the rest −1…+5 %, and the FL Harmless engine
    ran 1.5× slower under it (Harmless.cpp is kept out of LTO when it is on); the link takes 15–25 minutes. Off
    (round 190kz turned it on for the release with a real `-O3`, below).
  - `-fno-math-errno`: up to 25 % fewer cycles on Linux, none on Windows (0 to 3 % more); on MinGW it also turns `sin` +
    `cos` of one value into the C runtime's `sincos`. Not used.
  - zig gives `-O3` byte-identical objects to `-O2`; a real `-O3` (`-Xclang -O3`) measured no better than ThinLTO.
  - `-mcpu=x86_64_v2` (SSE4.2, POPCNT): 0–3 % fewer cycles on Windows, outputs identical; it would need a CPU check
    before any static constructor (a "CPU too old" box instead of a crash). Not used yet.
  - Measuring: a zig build without `-target` compiles for the host CPU (`-mcpu=native`), unlike the release.
- **Linux** (GCC 13, `-O3`, LTO of the app's own objects): unchanged. Link-time optimising the core too (it is a separate
  static library the app's LTO never reached), and `-fno-math-errno`, measured no gain with GCC.
- **macOS**: unchanged (Apple's clang `-O3`, LTO of the app's own objects; Darwin has no maths `errno`).
- `scripts/build-local.sh` takes `LYNX_OPT_FLAGS` (default `-O2`) to run the test suite with other optimisation flags.

### Round 190kz: release code generation (owner decision)

The Windows release builds (`.github/workflows/release.yml`) use a real `-O3` (`LYNX_WIN_OPT="-O2 -Xclang -O3"`: zig
0.14.1 still maps its own `-O3` to clang's `-O2`, checked again: identical code) and ThinLTO of LYNX's own objects
(`LYNX_WIN_LTO=thin`; JUCE's modules stay out of it). The instruction set stays the x86-64 baseline (SSE2), so every
64-bit CPU still runs LYNX; the hottest loops pick AVX2 at run time instead (below).
- Measured on Windows (Ryzen 5 3600; `perfbench.exe` linked from each build's own objects, one thread pinned, block 128
  unless named, thread cycles per sample; the variants interleaved, their order rotated each rep; Δ = median over reps
  of each rep's variant/`-O2` ratio; the same output hash in every cell; the machine was not idle, single reps vary by
  up to ±20 %):

  | scenario | `-O2` cycles/sample | `-O3` | `-O3` + ThinLTO, Harmless outside | `-O3` + ThinLTO, Harmless inside | reps |
  |---|---:|---:|---:|---:|---:|
  | Thief, block 480 | 22575 | +3.5 % | −0.9 % | −1.1 % | 9 |
  | Thief | 22816 | +0.2 % | −2.2 % | −7.2 % | 5 |
  | Exoplanet | 34120 | −4.6 % | −7.2 % | −8.0 % | 5 |
  | Patience (Live set) | 66267 | −1.8 % | −3.7 % | −12.7 % | 5 |
  | Future House (Harmless) | 37568 | +5.7 % | +0.3 % | +5.6 % | 4 |
  | synth100 | 100014 | −0.2 % | +1.9 % | −1.4 % | 5 |
  | inserts50 | 43901 | −7.1 % | −4.3 % | −5.0 % | 5 |
  | stream | 3452 | +0.8 % | −12.9 % | −13.9 % | 5 |
  | demo | 1372 | +0.1 % | +0.9 % | −0.6 % | 5 |
  | big200 | 249797 | −0.5 % | −1.6 % | −0.3 % | 5 |

  Chosen: `-O3` + ThinLTO with `Harmless.cpp` outside it (the default `LYNX_WIN_LTO_SKIP`): nothing worse beyond the
  noise, streamed audio −13 %, the Live set and inserts −4 %, Exoplanet −7 %. `-O3` alone made Thief at block 480
  (the FL comparison) 3.5 % slower; Harmless inside LTO made the Harmless-heavy song 5.6 % slower and gained elsewhere
  only within the noise. Raw numbers: round 190kz's notes (`abwin-flags2.txt`, `abwin-flags3.txt`).
- CI time (GitHub runner, 4 threads, cold app compile, round-build workflow, same commit): the build step takes
  22.6 min at `-O2` without LTO (link 41 s) and 30.8 min with `-O3` + ThinLTO (link 530 s): +8 minutes, all in the
  link. The release's Windows job (about 40 minutes before) stays shorter than the macOS jobs it waits for.
- The plugins' Windows build (`plugins.yml`) stays at `-O2` without LTO: each of its nine links (the CLAP, seven VST3s,
  plugexporttest) would run its own LTO back end over all of LYNX's objects, and `-O3` alone is not better (above).
- Round builds (`round-build.yml`) keep the fast default (`-O2`, no LTO); `LYNX_CI_RELEASE_OPT=1 tools/ci-round-build.sh`
  marks the commit `[release-opt]` and the run builds with the release's flags.
- `LYNX_WIN_LTO_SKIP`: LYNX sources compiled outside ThinLTO in an LTO build (default `src/core/Harmless.cpp`; empty =
  every LYNX source in it). `LYNX_WIN_LTO_SCOPE=core` keeps ThinLTO to the engine (`src/core`, `src/flp`) and compiles
  the UI as plain objects: a shorter link (measured locally only), not used by the release.

## Runtime CPU dispatch

`src/core/CpuFeatures.h` (round 190kz) finds once what the CPU and the operating system support (CPUID; XGETBV for the
AVX register state): SSE4.1, AVX, AVX2. A dispatched loop is compiled a second time with
`__attribute__ ((target ("avx2")))` and taken when `cpu::hasAvx2()`; everything else is the baseline build.
- **Rules**: the same IEEE operations in the same order per element, only wider (8 floats / 4 doubles) or VEX-encoded;
  never `fma` in the target list (one rounding instead of two), no reassociated sums, no horizontal reductions, no
  approximations. So every path renders the same bits; a test pins each dispatched loop (tests/Codegen190kzTests.inc).
- **Override**: `LYNX_CPU=sse2|sse4|avx|avx2` (also `baseline`) caps the level for a process (benchmarks: hash both
  paths on one machine); `cpu::setLevelForTest` does it from code. It never raises the level above the hardware.
- **Dispatched now**: Signalsmith Stretch (the real-time pitch shifter, the offline stretch: the spectral band loops
  and the strided radix-4, 2-point and radix-5 FFT passes, 8 wide), Parametric EQ 2's band cascade and Maximus's
  band-split cascade (VEX encoding of the same SSE2 code). Older per-engine checks (`cpuHasAvx` in Harmless, Harmor,
  Ogun, HardcoreFx; FMA checks of the FL-exact engines, which must follow FL's own dispatch) stay as they are.

## Windows plugins on Linux (yabridge)

[yabridge](https://github.com/robbert-vdh/yabridge) turns Windows VST2/VST3 plugins into Linux ones running in Wine.
LYNX SHARE treats them like any other Linux plugin: add the folder yabridge syncs to (`~/.vst3/yabridge` for VST3, the folder
of the `.so` files for VST2) in *Options › Plugins* or let the standard folders be scanned. Checked in round 161 with
yabridge 5.1.1 and Wine 10.0: scanning, the plugin window, playback, export, and the plugin sandbox (the Wine host then
runs under the sandbox child, so a crash there leaves LYNX SHARE running). Wine has to be installed by the user; LYNX SHARE
itself needs nothing extra.
