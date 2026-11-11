---
title: Build from source
summary: Requirements and commands to build LYNX SHARE on Windows, Linux and macOS.
status: full
---
LYNX SHARE is written in C++20 with JUCE 8 and built with CMake. The source is at [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) under the GNU Affero General Public License v3.0. The release files are built from the tagged source by GitHub Actions, so you only need this page to change the code or to build for yourself.

## Build on Linux

1. Install the dependencies listed below.
2. Get the source with `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` and go into the `lynx` folder.
3. Configure with `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Build with `cmake --build build`.

The dependencies on Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Build on Windows with Visual Studio

Requires Visual Studio 2022 Build Tools with "Desktop development with C++", CMake 3.22 or newer and Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Build for Windows from WSL or Linux

This is how the project itself is developed: the Windows program is cross-compiled with Zig, without Visual Studio.

```
./scripts/build-windows-zig.sh
```

The result is `build-win/LYNX.exe`.

| Requirement | Version | Default place |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE can be fetched with:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Set the environment variables `ZIG` and `JUCE_DIR` if your paths differ.

## Build on macOS

Needs the Xcode command line tools (`xcode-select --install`), CMake and Ninja, for example from `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` produces a universal app. Leave it out to build for the current Mac only.

## Run the core tests

The core (model, sequencer, instruments, effects and `.flp` import) is plain C++ and is tested without JUCE, with any C++20 compiler.

```
./scripts/build-local.sh ["folder with .flp files"]
```

The run must end with `N checks, 0 failures`. Given the `Data` folder of an FL Studio 2026 installation, it also parses the `.flp` projects that ship with FL Studio. The script builds two small tools as well: `build-local/flpdump`, which dumps a `.flp`, and `build-local/renderwav`.

After a build, the program can check itself without an audio device:

```
LYNX.exe --selftest report.txt
```

The report ends with `result=OK`. More options are on the [Command line](/wiki/command-line/) page.

## The source tree

| Folder | What it holds |
|---|---|
| `src/core/` | Model, sequencer, instruments, effects and `.flp` import. Plain C++, testable without JUCE |
| `src/flp/` | The `.flp` format reader |
| `src/engine/` | The audio engine: device, graph, samples, plugins, project files |
| `src/ui/` | The interface: Channel rack, Piano roll, Playlist, Mixer, windows |
| `third_party/` | CLAP and the WavPack decoder |
| `tests/` | The core tests |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Architecture, status, roadmap and the behavioural analysis of FL Studio |

## Good to know

- JUCE 8 does not officially support MinGW. The Zig build script works around the differences. The fixes are in `cmake/mingw/` and `src/platform/MingwFixes.cpp`.
- Run only one `build-windows-zig.sh` at a time. Two Zig builds at once can hang. For parallel builds in separate worktrees, give each its own cache with `ZIG_GLOBAL_CACHE_DIR` and `ZIG_LOCAL_CACHE_DIR`.
- Some engines read built-in sounds and tables from your own FL Studio installation. Nothing from FL Studio is in the repository.
- Part of the documentation in `docs/` is still in Portuguese. It is being translated.
- If you distribute a modified version, including as a network service, the licence requires you to publish its source under the same licence.

## Related

- [The full development environment (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE on Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [The repository and its README](https://github.com/Ka1-S/LYNX-SHARE)
- [Command line](/wiki/command-line/)
- [File formats](/wiki/file-formats/)
