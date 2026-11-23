---
title: Start here
summary: What LYNX SHARE is, what you need to run it and where to go next.
status: full
---
LYNX SHARE is a free, open-source DAW for Windows, macOS and Linux with three layouts in one program. The default is the FL Studio workflow: you program patterns in the Channel rack and the Piano roll, arrange them in the Playlist and mix them in the Mixer. The Clip-launch layout launches clips and scenes from a grid, in the style of Ableton Live, and the Simple layout mixes BandLab's approachable one-window style with FL Studio Mobile's way of working. `View › Layout style` switches between them at any time. It opens FL Studio projects (`.flp`), Ableton Live sets (`.als`), FL Studio Mobile projects (`.flm`), DAWproject files, GarageBand and BandLab projects and tracker modules, exports `.flp`, `.als`, `.dawproject` and `.flm`, and saves its own `.lynx` format.

It is an alpha. Expect bugs, and save often.

## Get it running

1. Check the table below to see that your system is supported.
2. Download and install it. [Install](/wiki/install/) has the steps for each system.
3. Start it. A welcome card asks where you are coming from and opens the matching layout in one click. It also sets the language, tests the sound and asks whether to check for updates, while the plugin folders are scanned in the background. `More setup (audio driver, plugin folders)...` opens the full setup wizard, also in `Options › Setup wizard (language, audio, plugin folders)...`.
4. If you hear nothing, go to [Audio setup](/wiki/audio-setup/).
5. Read [A tour of the window](/wiki/tour/), then make [Your first song](/wiki/first-song/).

## What you need

| System | Requirement |
|---|---|
| Windows | Windows 10 or 11, 64-bit |
| macOS | macOS 11 Big Sur or newer, Intel or Apple silicon |
| Linux | x86_64 or arm64 with glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+, Fedora 36+, current Arch or Manjaro; arm64 also on a Raspberry Pi 4 or 5 with a 64-bit OS), X11 or XWayland |

You do not need an audio interface. LYNX SHARE plays through the sound device your system already uses. You do not need FL Studio either. If it is installed on the same computer, LYNX SHARE finds its samples and uses the same plugin files for `.flp` projects.

Third-party plugins are optional. LYNX SHARE has its own instruments and effects, listed in the [catalogue](/instruments-and-effects/).

## Save your work

`Ctrl+S` saves, `Ctrl+Shift+S` saves under a new name, and `Ctrl+Alt+S` saves a new version (Song_2, Song_3 and so on). There is an autosave, reachable from `File › Open autosave folder` and `File › Revert to last backup...`.

`File › New from template` has four starting points: Empty, Basic beat, Trap with 808 and House.

## The demo project

The Windows installer and zip, the macOS `.dmg` and the Linux `.tar.gz` include a demo project, `Demo.lynx`. Open it with `File › Open...` (`Ctrl+O`). On Windows it sits in the installation folder, next to `LYNX.exe`.

You can also generate it from a terminal. See [Command line](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Where to go next

| You want to | Read |
|---|---|
| Choose a layout | [Layouts](/wiki/layouts/), [Simple layout](/wiki/simple-layout/) |
| Learn each window | [Channel rack](/wiki/channel-rack/), [Piano roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Open a project from another program | [Coming from FL Studio](/wiki/coming-from-fl-studio/), [Coming from Ableton Live](/wiki/coming-from-ableton/) |
| Use your VST, CLAP, LV2 or AU plugins | [Plugins](/wiki/plugins/) |
| Look up a key | [Shortcuts](/wiki/shortcuts/) |
| Fix a problem | [Troubleshooting](/wiki/troubleshooting/) and the [FAQ](/faq/) |

## Good to know

- The program file is called `LYNX.exe` on Windows and `LYNX` on macOS and Linux. The settings folder is also named `LYNX`.
- On Windows and macOS the first launch may show a system warning, because the Windows build is not code-signed yet and the macOS build is not notarized. [Install](/wiki/install/) explains what to click.
- The interface is in English by default on a system whose language LYNX SHARE does not have. You can change it in `Options › General settings...`. A new language applies after a restart.
- FLEX packs in FL Studio projects are encrypted and are never opened. Those channels get a synth patch chosen by the preset name.

## Related

- [Install](/wiki/install/)
- [Audio setup](/wiki/audio-setup/)
- [A tour of the window](/wiki/tour/)
- [Your first song](/wiki/first-song/)
- [Download](/download/)
