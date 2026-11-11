---
title: Troubleshooting
summary: First-launch warnings, no sound, plugin problems, crash reports and where to report bugs.
status: full
---
## Windows: SmartScreen warns on first launch

The executable is not code-signed yet, so SmartScreen may warn the first time you run it. Click `More info`, then `Run anyway`. The installer needs no admin rights.

## macOS: "cannot be opened"

The app is not notarized by Apple and has no Developer ID signature, so the first launch needs one extra step.

1. Drag LYNX SHARE to `Applications`.
2. Right-click (Control-click) it, choose `Open`, then `Open` again.

On macOS 15 Sequoia or newer, try to open it once, then go to `System Settings › Privacy & Security` and click `Open Anyway`.

Or, in Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS asks for microphone access the first time you record.

## Linux: the AppImage does not start

LYNX SHARE needs an x86_64 system with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36, current Arch or Manjaro) and X11 or XWayland.

Make the file executable first:

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Some distributions need libfuse2 to run AppImages. The package is `libfuse2` on Ubuntu 22.04, `libfuse2t64` on Ubuntu 24.04, `fuse2` on Arch and `fuse-libs` on Fedora.

If you prefer not to install it, download the `.tar.gz` instead, extract it and run `./install.sh`. It installs LYNX SHARE for your user under `~/.local` with a menu entry. `./install.sh --uninstall` removes it.

## No sound, or the wrong audio device

1. Open `Options › Audio settings...`.
2. Pick a driver and an output device.

The drivers per system:

- Windows: Windows Audio (WASAPI), DirectSound, ASIO and the built-in LYNX ASIO driver.
- macOS: Core Audio.
- Linux: PipeWire (native), JACK and ALSA. On a first start LYNX SHARE opens the first of these that has devices.

ASIO is a Windows driver model and exists only on Windows.

`Options › Setup wizard (language, audio, plugin folders)...` also tests the audio device.

## Linux: the sound breaks up

The audio thread should run with real-time priority. `Options › Audio settings › Status` shows whether it got it. If it did not, do one of these, then log out and in again:

- Install rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` or `sudo pacman -S rtkit`).
- Add yourself to the `audio` group with a real-time limit, as JACK set-ups have.

The details are in [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

## A plugin crashes

Run it in its own process, so that only the plugin stops:

1. Open `Options › General settings...`.
2. Turn on `Run plugins in a separate process (protects LYNX SHARE from crashes)`.
3. Reopen the project.

A plugin that stopped responding in its own process is muted. Save the project and reopen it to reload the plugin. See [Plugins](/wiki/plugins/).

## A plugin is missing

- If the plugin is installed but not listed, choose `Options › Scan for plugins`, or add its folder in `Options › Plugin manager...` with `Plugin folders...`.
- A plugin file that fails the scan is skipped. On Windows the reason is in `%APPDATA%\LYNX\scan-failures.log`. `Rescan all...` in the Plugin manager tries it again.
- If the plugin is not installed, an effect is replaced by a LYNX SHARE effect with the same role and its settings are kept in the project. `Help › Missing files...` lists what is missing.

## Video does not work on Linux

Video tracks and `Export with video` need FFmpeg. LYNX SHARE says how to install it.

## Crash reports

Crash reports are opt-in. After a crash or a freeze, the next start asks whether to send a report: `Send`, `Show what will be sent`, `Don't send` or `Report on GitHub instead`. `Settings › General › Crash reports` sets `Ask` (the default), `Always send` or `Never send`.

A report contains the LYNX SHARE version, the operating system, the CPU, the graphics cards and their drivers, the screen size, the audio driver and device, the crash or freeze file, the session's last 60 steps and the names and versions of the third-party plugins that were loaded.

It does not contain audio, projects, samples, your computer's name or your user name. File names are replaced by their extension unless you tick "include file names". The server stores no IP address.

To have a report deleted, open an issue with its id. Sent ids are listed in `crash-reports\sent.log` in the settings folder. The full text is in the [privacy policy](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Reporting a bug

Open an issue at [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues). `Help › Report a problem...` sends a report by hand, with an optional description.

The settings folder is `%APPDATA%\LYNX` on Windows, `~/Library/Application Support/LYNX` on macOS and `~/.config/LYNX` on Linux.
