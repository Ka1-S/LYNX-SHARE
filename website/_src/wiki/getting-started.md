---
title: Getting started
summary: Install LYNX SHARE, set up audio, make a first pattern and save it.
status: full
---
LYNX SHARE is an alpha. Expect bugs, and save often. Downloads for every system are on the [releases page](https://github.com/Ka1-S/LYNX-SHARE/releases), with checksums in `SHA256SUMS.txt`.

## Install on Windows

Windows 10 or 11, 64-bit. There are three ways:

- **Installer** (`LYNX-<version>-setup-x64.exe`): installs for your user only, needs no admin rights, adds Start menu and desktop shortcuts and opens `.lynx` projects. It offers LYNX SHARE under *Open with* for `.flp` files without changing FL Studio's default.
- **Portable zip** (`LYNX-<version>-windows-x64.zip`): extract it anywhere and run `LYNX.exe`.
- **Microsoft Store**: [LYNX SHARE on the Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N). The Store installs it and keeps it updated.

The executable is not code-signed yet, so SmartScreen may warn on first launch. Choose *More info*, then *Run anyway*.

## Install on macOS

macOS 11 Big Sur or newer, Intel and Apple silicon (one universal `.dmg`).

1. Open the `.dmg` and drag LYNX SHARE to *Applications*.
2. The app is not notarized by Apple, so the first launch needs an extra step: right-click (or Control-click) LYNX SHARE in *Applications*, choose *Open*, then *Open* again.
3. On macOS 15 Sequoia or newer, try to open it once, then go to *System Settings › Privacy & Security* and click *Open Anyway*.

The Terminal alternative:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS asks for microphone access the first time you record.

## Install on Linux

x86_64 with glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+, Fedora 36+, current Arch or Manjaro). LYNX SHARE needs X11 or XWayland. Audio goes through PipeWire, JACK or ALSA, and nothing extra has to be installed for that.

AppImage:

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Some distributions need libfuse2 to run AppImages (`libfuse2` on Ubuntu 22.04, `libfuse2t64` on 24.04, `fuse2` on Arch, `fuse-libs` on Fedora). If you would rather not install it, use the `.tar.gz`: extract it and run `./install.sh`. That installs LYNX SHARE for your user under `~/.local` with a menu entry, an icon and the `.lynx` file type. `./install.sh --uninstall` removes it.

## First start

A short setup wizard opens on the first start. It picks the language, tests the audio output and lists the plugin folders, which are then scanned in the background. You can skip it and reopen it later from `Options › Setup wizard`.

To change the audio device afterwards, open `Options › Audio settings`. The available drivers are:

| System | Drivers |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO, built-in LYNX ASIO driver |
| macOS | Core Audio |
| Linux | PipeWire (native), JACK, ALSA |

On Linux the first start picks the first of PipeWire, JACK and ALSA that has devices.

## Make a first sound

1. Press `F6` to open the [Channel rack](/wiki/channel-rack/).
2. Click the `+ add channel` box under the last row and pick an instrument from the menu.
3. Click some steps on the channel's row. Dragging draws several, right-drag erases.
4. Press `Space`. In Pattern mode the current pattern plays in a loop. Press `Space` again to stop.

To use a sample instead, open the [Browser](/wiki/browser/) with `Alt+F8`, click an audio file to hear it, then double-click it to add it as a new channel. You can also drag it onto the Channel rack.

`File › New from template` has four starting points: Empty, Basic beat, Trap with 808 and House.

## Save

`Ctrl+S` saves, `Ctrl+Shift+S` saves under a new name, and `Ctrl+Alt+S` saves a new version (Song_2, Song_3 and so on). Projects use LYNX SHARE's own `.lynx` format. There is an autosave, reachable from `File › Open autosave folder` and `File › Revert to last backup`.

## The demo project

The Windows installer and zip, the macOS `.dmg` and the Linux `.tar.gz` include a demo project, `Demo.lynx`. Open it with `File › Open` (`Ctrl+O`). On Windows it sits in the installation folder, next to `LYNX.exe`.

You can also generate it from a terminal:

```
LYNX.exe --showcase out.lynx
```

On macOS and Linux the binary is called `LYNX`.
