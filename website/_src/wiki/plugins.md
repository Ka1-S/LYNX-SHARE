---
title: Plugins
summary: Which plugin formats load on each system, scanning, the sandbox and missing plugins.
status: full
---
## Formats per system

| System | Formats hosted |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

VST2 is not hosted on macOS. On Linux, Windows plugins work through [yabridge](https://github.com/robbert-vdh/yabridge) and Wine, which you install yourself: add the folder yabridge syncs to as a plugin folder.

## Scanning

On first start, the setup wizard lists the plugin folders and scans them in the background. `Options › Setup wizard (language, audio, plugin folders)...` opens it again.

To scan later:

1. Choose `Options › Scan for plugins`. Progress shows in the hint bar and you can keep working.
2. Or open `Options › Plugin manager...` and use `Find new plugins`, `Rescan all...`, `Plugin folders...` or `Add plugin file...`.

LYNX SHARE also looks for new plugins a few seconds after each start.

The standard folders are scanned, plus the ones you add:

- Windows: `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` and `Steinberg\VSTPlugins` under Program Files, and the folders FL Studio uses if it is installed.
- macOS: `VST3`, `Components` and `CLAP` in `~/Library/Audio/Plug-Ins` and `/Library/Audio/Plug-Ins`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` and the same folders under `/usr/lib` and `/usr/local/lib`.

Each plugin file is examined in a separate process. A file that crashes or hangs for 30 seconds is marked as failed and skipped later. On Windows the failed files are listed with the reason in `%APPDATA%\LYNX\scan-failures.log`. `Rescan all...` forgets the failed ones and tries every file again.

Press `F8` for the plugin picker, which shows every generator and effect. Type to search, then double-click or press `Enter` to add one.

## The plugin sandbox

By default a plugin runs inside LYNX SHARE, so a plugin that crashes can take the program down. The sandbox is optional and runs each plugin in its own process.

1. Open `Options › General settings...`.
2. Turn on `Run plugins in a separate process (protects LYNX SHARE from crashes)`.
3. Reopen the project to apply it to plugins that are already open.

If a sandboxed plugin crashes or stops answering, it goes silent, a notice appears and LYNX SHARE keeps running. Save the project and reopen it to reload the plugin. A sandboxed plugin's editor opens in its own window.

You can also turn it on for one plugin with `Always run in a separate process` in the plugin window's wrapper menu.

The sandbox is available on Windows and Linux. The 0.1.25 release notes add macOS.

## 32-bit plugins (Windows only)

On Windows, 32-bit VST2 and VST3 plugins run through a bridge (`LYNX-bridge32.exe`). The bridge adds no latency. A plugin that crashes in it stops on its own while LYNX SHARE keeps playing. The Plugin manager has a `Bits` column (32, 64 or 32+64), and a bridged plugin's editor opens in its own window.

## When a plugin is not installed

LYNX SHARE first looks for the same plugin by its identifier, then by file, then by name. A project saved with the VST2 opens with the VST3 of the same product and the other way round, but then the plugin plays with its default settings.

If an effect is not installed at all, a LYNX SHARE effect with the same role takes its slot (a reverb for a reverb, a delay for a delay):

- The slot shows the original plugin's name with a LYNX SHARE badge.
- Where the saved state can be read, its settings are translated. Otherwise the stand-in starts from a default preset.
- The original plugin and its settings stay in the project. Once the plugin is installed, the project opens with it again.
- Automation of the original's parameters is kept but does not drive the stand-in.

`Help › Missing files...` lists the plugins that were replaced. The switch is `Substitute missing plugins with LYNX SHARE equivalents` in `Options › General settings...`, on by default.

Missing instruments are not substituted yet. A missing instrument plugin becomes a built-in synth chosen by the channel name.

## Built-in plugins

LYNX SHARE ships its own instruments and effects: a 3-oscillator synth, a sampler, a SoundFont player, drums, EQs, compressors, limiters, reverbs, delays and more, plus engines recreated from FL Studio's native plugins that play when you open a `.flp`. The full list, with the status of each engine, is in [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md).
