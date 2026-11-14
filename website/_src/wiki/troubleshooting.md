---
title: Troubleshooting
summary: First-launch warnings, no sound, plugin problems, crash reports and where to report bugs.
status: full
---
Each heading below is one problem. If yours is not here, report it as described at the end.

## Starting the program

### Windows: SmartScreen warns on first launch

The executable is not code-signed yet, so SmartScreen may warn the first time you run it.

1. Click `More info`.
2. Click `Run anyway`.

The installer needs no admin rights.

### macOS: "cannot be opened"

The app is not notarized by Apple and has no Developer ID signature, so the first launch needs one extra step.

1. Drag LYNX SHARE to `Applications`.
2. Right-click (Control-click) it and choose `Open`.
3. Click `Open` again.

On macOS 15 Sequoia or newer, try to open it once, then go to `System Settings › Privacy & Security` and click `Open Anyway`. Or, in Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS asks for microphone access the first time you record.

### Linux: the AppImage does not start

LYNX SHARE needs an x86_64 system with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36, current Arch or Manjaro) and X11 or XWayland.

1. Make the file executable and run it, with the two commands below.
2. If it still does not start, install libfuse2. The package is `libfuse2` on Ubuntu 22.04, `libfuse2t64` on Ubuntu 24.04, `fuse2` on Arch and `fuse-libs` on Fedora.
3. If you prefer not to install it, download the `.tar.gz` instead, extract it and run `./install.sh`.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` installs LYNX SHARE for your user under `~/.local` with a menu entry. `./install.sh --uninstall` removes it.

## Sound

### No sound, or the wrong audio device

1. Open `Options › Audio settings...`.
2. Pick a driver.
3. Pick an output device.

| System | Drivers |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO and the built-in LYNX ASIO driver |
| macOS | Core Audio |
| Linux | PipeWire (native), JACK and ALSA. On a first start LYNX SHARE opens the first of these that has devices |

ASIO is a Windows driver model and exists only on Windows. `Options › Setup wizard (language, audio, plugin folders)...` also tests the audio device. See [Audio setup](/wiki/audio-setup/).

### Linux: the sound breaks up

The audio thread should run with real-time priority.

1. Open `Options › Audio settings...` and read `Status`. It shows whether the thread got real-time priority.
2. If it did not, install rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` or `sudo pacman -S rtkit`), or add yourself to the `audio` group with a real-time limit, as JACK set-ups have.
3. Log out and in again.

The details are in [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

### The sound crackles or drops out

LYNX SHARE counts every dropout (buffer underrun) and says why it happened. Point at the `CPU` meter in the toolbar, or open `Tools › Plugin performance monitor...`. A red number next to the CPU percentage is the count.

The hint names the cause of the last dropout:

| Cause | What it means | What to try |
|---|---|---|
| The processing took longer than the buffer | A block of audio took longer to compute than the time it lasts. The hint names the slowest channel or effect in that block | Raise the buffer size in `Options › Audio settings...`, freeze that channel, or turn the plugin's quality down |
| The processing came near the buffer's limit | The load was close to 100 % around the dropout | Raise the buffer size, or freeze the heaviest channels |
| Driver or system latency | The driver or the system was late while the load stayed low. No plugin caused it | Raise the buffer size, try another driver (on Windows the interface's ASIO driver), and on Linux check real-time priority (above) |

Each dropout is also written to `audio-glitches.log` in the settings folder, with the device, the buffer and the heaviest channels.

## Plugins

### A plugin crashes

Most plugin crashes switch only that plugin off: an effect is bypassed, an instrument goes silent, and LYNX SHARE and the project keep running. The `Plugin crashed` message then offers:

- `Reload`, which starts the plugin again with the settings from the last save or backup.
- `Always run <plugin> in a separate process`, which starts it again in a process of its own, now and in every project. A crash there cannot take LYNX SHARE down. A plugin in a separate process opens its window on its own (it cannot be docked).

The plugin's window shows the same two buttons in place of its editor. `Options › Plugin manager...` lists the plugins that always run in a separate process in its `Separate process` column; click the box to switch one on or off.

To run every plugin in its own process:

1. Open `Options › General settings...`.
2. Turn on `Run plugins in a separate process (protects LYNX SHARE from crashes)`.
3. Reopen the project.

A plugin that crashed or stopped responding in its own process is muted. Save the project and reopen it to load the plugin again. See [Third-party plugins](/wiki/plugins/).

### A plugin is not in the list

1. Choose `Options › Scan for plugins`.
2. If it is still missing, open `Options › Plugin manager...`, click `Plugin folders...` and add the plugin's folder.
3. Look at the `Failed` tab. A plugin file that fails the scan is skipped. On Windows the reason is in `%APPDATA%\LYNX\scan-failures.log`.
4. Click `Rescan all...` to try the failed files again.

### A project uses a plugin that is not installed

An effect is replaced by a LYNX SHARE effect with the same role, and its settings are kept in the project. `Help › Missing files...` lists what is missing. See [Built-in instruments and effects](/wiki/built-in/).

### Video does not work on Linux

Video tracks and `Export with video` need FFmpeg. LYNX SHARE says how to install it.

## Crash reports

![The crash report question shown at the next start, with Send, Show what will be sent and Don't send](/assets/img/wiki/crash-report.webp)

Crash reports are opt-in. After a crash or a freeze, the next start asks whether to send a report.

| Command | What it does |
|---|---|
| `Show what will be sent` | Shows the exact text |
| `Report on GitHub instead` | Opens GitHub's issue form, prefilled. You attach the files yourself |
| `Open the folder with the report` | Shows the report files on your computer |
| `Include file names (projects, samples)` | Off, only the extension of each file is sent |
| `Always send without asking` | Sends future reports without the question |

`Crash reports` in `Options › General settings...` sets `Ask` (the default), `Always send` or `Never send`.

A report contains the LYNX SHARE version, the operating system, the CPU, the graphics cards and their drivers, the screen size, the audio driver and device, the crash or freeze file, the session's last 60 steps and the names and versions of the third-party plugins that were loaded.

It does not contain audio, projects, samples, your computer's name or your user name. The server stores no IP address. Sent ids are listed in `crash-reports\sent.log` in the settings folder. To have a report deleted, open an issue with its id. The full text is in the [privacy policy](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Report a problem

![The Report a problem window: a description box, an optional e-mail address and the Send button](/assets/img/wiki/report-problem.webp)

1. Choose `Help › Report a problem...`.
2. Describe what happened. The e-mail address is optional, for when you want an answer.
3. Leave `Attach the newest crash / freeze files and this session's steps` ticked if the problem was a crash.
4. Click `Show what will be sent` to check the text.
5. Click `Send`. Nothing is sent before that.

You can also open an issue at [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues).

## Good to know

- After a crash, backup copies of your project are in LYNX SHARE's autosave folder.
- When a plugin was involved in the crash, the question offers to run that plugin in a separate process from now on.
- The settings folder is `%APPDATA%\LYNX` on Windows, `~/Library/Application Support/LYNX` on macOS and `~/.config/LYNX` on Linux.

## Related

- [Audio setup](/wiki/audio-setup/)
- [Third-party plugins](/wiki/plugins/)
- [Install](/wiki/install/)
- [FAQ](/faq/)
- [Crash protection](/why/#crash)
