---
title: File formats
summary: Every format LYNX SHARE 0.1.26 opens, imports, saves and exports, with its limits.
status: full
---
LYNX SHARE saves projects as `.lynx` files. It also opens projects of other programs, loads the usual audio files and exports audio, MIDI and projects for other programs. The table lists every format of 0.1.26 alpha.

A project opened from another program's format is an import. `File › Save` then asks where to write a `.lynx` file, and the original file is never changed.

## Open a project from another program

1. Choose `File › Open...` (`Ctrl+O`).
2. Pick a `.flp`, `.als`, `.flm`, `.mid` or `.zip` file.
3. For a `.flp`, choose `Help › Last FL Studio import report` to read what was approximated or left out.
4. Choose `File › Save` and pick where to write the `.lynx` file.

## Formats

| Format | Open | Save / Export | Notes |
|---|---|---|---|
| `.lynx` | `File › Open...` | `File › Save`, with autosave | LYNX SHARE's own project format (JSON) |
| `.zip` | `File › Open...` extracts it to a folder and opens the project inside | `File › Export › Project with samples (.zip)...` | Also opens FL Studio's zipped loop package |
| `.lynxpack` | Opens after a summary. A part is merged into the host's project | `File › Online session › Send project...` | A project or a part sent in an [online session](/wiki/online-sessions/). Third-party plugin parts travel as audio renders plus the plugin settings |
| `.flp` | `File › Open...` | `File › Export › FL Studio project...`, an FL Studio 2026 project or a zipped loop package | Native plugins play on recreated engines of varying exactness. FLEX packs are never opened. On export, LYNX-only instruments and effects are rendered to audio or swapped for the nearest FL plugin, and modulators are left out. See [Coming from FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `File › Open...` | No | FL Studio Mobile project. Partial: instruments other than MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, the Slicer and DirectWave play the built-in synth |
| `.als` | `File › Open...`, sets of Live 11 and 12 | `File › Export › Ableton Live set (.als)...`, for Live 12 or Live 11 | On import, Live's devices with no LYNX SHARE equivalent are dropped. On export, LYNX SHARE's own instruments and effects are not converted, and effect-parameter automation and insert-to-insert routes are dropped. See [Coming from Ableton Live](/wiki/coming-from-ableton/) |
| `.mid`, `.midi` | `File › Open...` makes a new project. `File › Import › MIDI file...` and the Piano roll import into the current one | `File › Export › MIDI file...` (`Ctrl+Shift+M`) | |
| `.wav` | As a sample or an audio clip | `File › Export › Wave file...`, 16-bit, 24-bit or 32-bit float | Optional dither, tempo information, loop markers and Playlist markers |
| `.mp3` | As a sample or an audio clip | `File › Export › MP3 file...` | Constant bitrate |
| `.ogg` | As a sample or an audio clip | `File › Export › OGG file...` | |
| `.flac` | As a sample or an audio clip | `File › Export › FLAC file...`, compression 0 to 8 | Lossless at every level |
| `.aif`, `.aiff` | As a sample or an audio clip | No | |
| `.wv` | As a sample | No | WavPack samples, as shipped in FL Studio's packs |
| `.sf2`, `.sf3` | Played by LYNX Soundfont | No | SoundFonts |
| `.fst` | From the Browser | No | Preset of an FL Studio native plugin. Only for the plugins LYNX SHARE has an engine for |
| `.fsc` | Piano roll and Event editor | Piano roll and Event editor | FL Studio score and automation file |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Load preset file...` in a hosted plugin's preset menu | `Save preset...` and `Export preset file...` | Third-party plugin presets. `.fxp` and `.fxb` are for VST2 plugins and `.vstpreset` for VST3 plugins, and only while the plugin runs inside LYNX SHARE |
| Video file | `File › Import › Video file...` puts a video on the Playlist | `Also write a video with this mix` in the export window | Import uses the system decoders on Windows and macOS, and needs FFmpeg on Linux. Export needs FFmpeg |

## Other exports

| Command | What it does |
|---|---|
| `Files` option of the export window | Stems: one file per mixer insert, per channel, or per channel with instruments only |
| `File › Export › All playlist tracks` | One file per Playlist track |
| `File › Export › All playlist track groups` | One file per track group |
| `File › Export › Master...` | Renders the project and masters it to a loudness target, on your computer |
| `File › Export › Project bones...` | Writes channel and effect presets and scores to a folder |

The export window renders the song, the current pattern or the Playlist selection.

## From the command line

On Windows the program is `LYNX.exe`. On macOS and Linux it is `LYNX`.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` and `--export-flp` also accept an `.als` as the input.

## Good to know

- Audio files longer than 30 seconds are streamed from a disk cache.
- A `.flp`, `.als` or `.flm` is never written when you save. The export commands write new files.
- `.wv` files are read only.
- A `.zip` is extracted to a folder first, then the project inside is opened.

## Related

- [Importing projects](/wiki/importing-projects/)
- [Exporting](/wiki/exporting/)
- [Command line](/wiki/command-line/)
- [Coming from FL Studio](/wiki/coming-from-fl-studio/)
- [Coming from Ableton Live](/wiki/coming-from-ableton/)
