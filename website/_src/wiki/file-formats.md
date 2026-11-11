---
title: File formats
summary: Every format LYNX SHARE 0.1.25 opens, imports, saves and exports, with its limits.
status: full
---
A project opened from another program's format is an import. `File › Save` then asks where to write a `.lynx` file, and the original file is never changed.

## What LYNX SHARE opens and imports

| Format | What it is | Limits |
|---|---|---|
| `.lynx` | LYNX SHARE's own project format (JSON) | None |
| `.lynxpack` | A project or a part sent in an [online session](/wiki/online-sessions/) | Opens after a summary. A part is merged into the host's project |
| `.zip` | A project with its samples, exported by LYNX SHARE, or FL Studio's zipped loop package | Extracted to a folder, then the project inside is opened |
| `.flp` | FL Studio project | Native plugins play on recreated engines of varying exactness. FLEX packs are never opened. See [Coming from FL Studio](/wiki/coming-from-fl-studio/) |
| `.als` | Ableton Live set, Live 11 and 12 | Live's own effects are dropped and racks keep their first chain. See [Coming from Ableton Live](/wiki/coming-from-ableton/) |
| `.flm` | FL Studio Mobile project | Partial: FLM's effects and automation are not imported |
| `.mid`, `.midi` | MIDI file | `File › Open...` makes a new project from it. `File › Import › MIDI file...` and the Piano roll import it into the current one |
| `.wav`, `.aif`, `.aiff`, `.flac`, `.ogg`, `.mp3` | Audio files, loaded as samples and audio clips | Files longer than 30 seconds are streamed from a disk cache |
| `.wv` | WavPack samples, as shipped in FL Studio's packs | Read only |
| `.sf2`, `.sf3` | SoundFonts, played by LYNX Soundfont | None stated |
| `.fst` | Preset of an FL Studio native plugin | Only for the plugins LYNX SHARE has an engine for |
| `.fsc` | FL Studio score and automation file | Piano roll and Event editor |
| Video file | `File › Import › Video file...` puts a video on the Playlist | Windows and macOS use the system decoders. Linux needs FFmpeg |

## What LYNX SHARE saves and exports

| Format | What it is | Limits |
|---|---|---|
| `.lynx` | The project (`File › Save`), with autosave | None stated |
| `.zip` | `File › Export › Project with samples (.zip)...` | None stated |
| `.lynxpack` | `File › Online session › Send project...` | Third-party plugin parts travel as audio renders plus the plugin settings |
| `.flp` | `File › Export › FL Studio project...`, an FL Studio 2026 project or a zipped loop package | LYNX-only instruments and effects are rendered to audio or swapped for the nearest FL plugin. Modulators are left out |
| `.als` | `File › Export › Ableton Live set (.als)...`, for Live 12 or Live 11 | LYNX SHARE's own instruments and effects are not converted. Effect-parameter automation and insert-to-insert routes are dropped |
| WAV | `File › Export › Wave file...`, 16-bit, 24-bit or 32-bit float | Optional dither, tempo information, loop markers and Playlist markers |
| MP3 | `File › Export › MP3 file...` | Constant bitrate |
| OGG | `File › Export › OGG file...` | None stated |
| FLAC | `File › Export › FLAC file...`, compression 0 to 8 | Lossless at every level |
| Stems | The `Files` option of the export window: one file per mixer insert, per channel, or per channel with instruments only | `File › Export › All playlist tracks` and `All playlist track groups` write one file per track or group |
| `.mid` | `File › Export › MIDI file...` (`Ctrl+Shift+M`) | None stated |
| Mastered file | `File › Export › Master...` renders the project and masters it to a loudness target, on your computer | None stated |
| Video | `Also write a video with this mix` in the export window | Needs FFmpeg |
| Project bones | `File › Export › Project bones...` writes channel and effect presets and scores to a folder | None stated |

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
