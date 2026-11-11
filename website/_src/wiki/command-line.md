---
title: Command line
summary: Open, render and convert projects from a terminal, without the window.
status: full
---
LYNX SHARE also runs without opening its window. One option renders a project to audio, others convert it to an FL Studio project or an Ableton Live set, and several exist for testing. The project's own checks use the same options.

The program is `LYNX.exe` on Windows and `LYNX` on macOS and Linux. On macOS the full path is `/Applications/LYNX.app/Contents/MacOS/LYNX`. The options are the same on every system.

## Render a project to a file

1. Open a terminal in the folder that holds the program. On Windows that is the installation folder or the folder where you extracted the zip.
2. Run `LYNX.exe --export project.flp out.flac`, with your own project and output file.
3. Wait for the program to exit. No window opens.
4. Read `out.flac.txt`, written next to the output. For an imported project it lists what was converted, approximated and left out, and which plugins were found or missing.

## Open a project

```
LYNX.exe project.lynx
```

Starts LYNX SHARE with the window and opens the file. It accepts `.lynx`, `.flp`, `.als`, `.flm` and `.mid`. A relative path is taken from the current folder.

```
LYNX.exe --no-plugins project.lynx
```

Opens a `.lynx` project without loading its third-party plugins, as `File › Open without third-party plugins...` does. Each plugin keeps its settings. Use it for a project that a plugin keeps from opening.

## Options for everyday use

| Option | What it does |
|---|---|
| `--export <project> <out>` | Renders the whole song with its tail, as the Export window does. The extension of `<out>` picks the format: `.wav` (24-bit), `.flac`, `.ogg` or `.mp3` (320 kbps) |
| `--export ... --pattern` | Renders the current pattern only |
| `--export ... --insert N` | Renders the output of Mixer insert N only |
| `--export ... --rate N` | Renders at another sample rate, in Hz |
| `--stems <project> <base.wav>` | Writes one WAV per Mixer insert that has channels: `base - 1.wav`, `base - 2.wav` and so on |
| `--export-flp <project> <out.flp>` | Writes an FL Studio project. With `out.zip` it writes a zipped loop package |
| `--export-flp ... --substitute` | Swaps LYNX-only plugins for the nearest FL plugin instead of rendering them to audio |
| `--export-als <project> <folder>` | Writes an Ableton Live set to `<folder>/<name> Project/<name>.als`, for Live 12 |
| `--export-als ... --live11` | Writes the set for Live 11 |
| `--export-bundle <project> <folder>` | Writes the package of `File › Export › For other apps...` into the folder |
| `--export-bundle ... --zip` | Zips the folder. It is replaced by `<folder>.zip` |
| `--export-bundle ... --no-mixdown` | Leaves the mixdown out of the package |
| `--devices <report.txt>` | Lists the audio driver types and devices of this computer |
| `--showcase <out.lynx>` | Writes LYNX SHARE's own demo project |
| `--lang <id>` | Uses another interface language for this run, for example `--lang pt-BR` |
| `--menu-entry on` or `off` | Linux AppImage only: adds LYNX SHARE to the applications menu, or removes it |

`--export`, `--stems` and `--export-flp` take a `.lynx`, `.flp` or `.als` as the project. `--export-als` also takes `.flm`. `--export-bundle` takes a `.lynx`, `.flp` or `.als`.

Examples:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --export-bundle song.lynx out-folder --zip
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Options meant for testing

These exist to check LYNX SHARE itself. They are useful in a bug report.

| Option | What it does |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Runs the self-test without an audio device. The report ends with `result=OK` when it passes |
| `--snapshot <prefix> [project]` | Draws the interface and its windows to PNG files named after the prefix. The pictures in this wiki are made this way |
| `--render <project> <out.wav> <seconds>` | Renders that many seconds of the current pattern to a 24-bit WAV |
| `--plugintest <plugin> <report.txt>` | Loads and tests one VST3 or CLAP plugin |
| `--sandboxtest <plugin> <report.txt>` | Tests a plugin in a separate process, including a hung one |
| `--scantest <folder> <report.txt>` | Scans a folder for plugins, in separate processes |
| `--fldb <report.txt> [project.flp]` | Reports the FL Studio installation found here, its plugin database and a project's plugin matches |
| `--devicetest <type> <device> <report.txt>` | Opens an audio driver for one second of silence and reports it |
| `--uiperf <project> <report.txt>` | Times the drawing of the Mixer, Playlist and Channel rack |

Examples:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Good to know

- Results go to files, not to the terminal. `--export` writes `<out>.txt`, `--export-flp` writes `<out.flp>.txt`, `--export-als` writes `export-als.txt` in the folder, `--export-bundle` writes `export-bundle.txt` in the folder and `--stems` writes `<base.wav>.txt`.
- The program reads standard input. In a shell loop, add `< /dev/null` to each call.
- `--snapshot` draws at 1280x720 unless the environment variable `LYNX_SNAPSHOT_SIZE` says otherwise, for example `1920x1080`.
- `--lang` takes the ids of the languages listed in `Options › General settings...`, such as `en`, `pt-BR`, `de`, `es`, `fr` or `ja`. An unknown id is ignored.
- `--export` uses the same render path as the Export window, but none of the window's other options. For dither, bit depth or normalizing, use the window.
- The source has many more test options. They are internal and can change between versions.

## Related

- [Exporting](/wiki/exporting/)
- [File formats](/wiki/file-formats/)
- [Build from source](/wiki/build-from-source/)
- [Troubleshooting](/wiki/troubleshooting/)
