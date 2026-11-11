---
title: Coming from FL Studio
summary: What maps to what, opening .flp and .flm projects, and sending a song back to FL Studio.
status: full
---
LYNX SHARE is an independent program. It is not affiliated with, endorsed or sponsored by Image-Line, and it contains no Image-Line code. FL Studio is a registered trademark of Image-Line Software.

## What maps to what

The windows keep the names and keys you know.

| In FL Studio | In LYNX SHARE |
|---|---|
| Playlist, Channel rack, Piano roll, Mixer | Same names, on `F5`, `F6`, `F7` and `F9` |
| Plugin picker | `F8` |
| Browser | `Alt+F8` |
| Pattern / Song mode | `L` |
| Undo | `Ctrl+Z` undoes, or redoes after an undo. `Ctrl+Alt+Z` undoes backwards |
| Save new version | `Ctrl+Alt+S` |
| Export | `Ctrl+R`, MP3 on `Ctrl+Shift+R`, MIDI on `Ctrl+Shift+M` |
| Fruity Wrapper | Third-party plugins are hosted directly, see [Plugins](/wiki/plugins/) |
| `.flp` project file | `.lynx` project file |

The Mixer has inserts, sends, 10 effect slots per track and sidechain. `Help › Shortcuts` lists every key.

## Opening a .flp

1. Choose `File › Open...` (`Ctrl+O`) and pick the `.flp`.
2. LYNX SHARE resolves the project's plugins and samples, then opens it. The title shows "(imported from FL Studio)".
3. `File › Save` asks where to write a `.lynx` file. The `.flp` itself is never written.

What is imported:

- Channels, samples, patterns, slide and porta notes, every arrangement, markers, time-signature changes, automation and controller links.
- The Mixer: routing, levels, sidechains, track EQ and native effects with their saved settings.
- Third-party VST3, VST2 and CLAP plugins inside Fruity Wrapper: replaced by the same plugin installed on your computer, with its saved state.

## FL Studio native plugins

FL Studio's own generators and effects are not loaded. LYNX SHARE plays them on engines it recreated, and each one has a quality level:

| Level | Meaning |
|---|---|
| Exact | Bit-identical to the original on every state tested |
| Near-exact | Bit-identical on most states, the rest differ below about -100 dB |
| Close | The same algorithm, within -60 dB of the original on almost all tested states |
| Approximate | The saved settings are mapped onto a LYNX SHARE built-in synth or effect |
| Placeholder | The state is kept and the plugin is silent or passes audio through |

The level of every engine, and what each one still lacks, is in [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md).

FLEX packs are encrypted and are never opened. A FLEX channel gets a synth patch chosen by its preset name.

## What needs your own FL Studio installation

Opening a `.flp` works without FL Studio installed. An installation on the same computer adds two things:

- FL Studio's factory samples and its plugin database, so samples and plugins are found without a scan.
- The built-in waves, samples, impulses and tables some engines need (FL Keys, Soundgoodizer, Maximus and the others marked with a dagger in PLUGINS.md). They are read from your installation at run time and are not shipped with LYNX SHARE.

On Linux, LYNX SHARE looks for a Windows FL Studio under Wine. On macOS the app bundle is looked for, but this is untested.

## The import report

`Help › Last FL Studio import report` lists what was approximated by the built-in synth, what had no equivalent, which plugins were found or are not installed, which effects were left out or partly reproduced, and how many samples are missing. `Help › Missing files...` lists the missing samples and plugins.

## Exporting back to .flp

1. Choose `File › Export › FL Studio project...`.
2. Pick `FL Studio project (.flp)` or `Zipped loop package (.zip, with every sample)`.
3. Choose what happens to things FL Studio does not have: render them to audio (sounds the same), or use the nearest FL plugin (stays editable, may sound different, and LYNX-only instruments are left out).

The file is an FL Studio 2026 project. Samplers, audio clips, FL's own generators and effects, third-party plugins (inside Fruity Wrapper), patterns, the Playlist, the Mixer and automation go over with their settings. Modulators are left out. A report lists what went over exactly and what was rendered, replaced or left out.

## FL Studio Mobile (.flm)

`File › Open...` also takes `.flm` projects. The import is partial.

- Imported: tempo, time signature, one mixer insert per FLM channel, one Playlist track per lane, patterns (each loop pass as a clip), audio clips with offset and pitch, drum kits, DW Sampler samples (as samplers) and `.dwp` programs (in DirectWave).
- Not imported: FLM's effects and automation lanes. Other FLM generators play the built-in synth.
- Samples are searched next to the `.flm` and in FL Studio's copy of the FLM resources. A `.dwp` that is not found plays a stand-in.
