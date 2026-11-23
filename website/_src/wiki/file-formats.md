---
title: File formats
summary: Every format LYNX SHARE 0.1.31 opens, imports, saves and exports, with its limits.
status: full
---
LYNX SHARE saves projects as `.lynx` files. It also opens projects of other programs, loads the usual audio files and exports audio, MIDI and projects for other programs. The table lists every format of 0.1.31 alpha.

A project opened from another program's format is an import. `File › Save` then asks where to write a `.lynx` file, and the original file is never changed.

## Open a project from another program

1. Choose `File › Open...` (`Ctrl+O`).
2. Pick a `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid` or `.zip` file.
3. Choose `Help › Last project import report` to read what was approximated or left out.
4. Choose `File › Save` and pick where to write the `.lynx` file.

## Formats

| Format | Open | Save / Export | Notes |
|---|---|---|---|
| `.lynx` | `File › Open...` | `File › Save`, with autosave | LYNX SHARE's own project format (JSON). `File › Save a copy for an older LYNX...` writes a copy that 0.1.27 or 0.1.28 opens, and lists what that version cannot keep |
| `.zip` | `File › Open...` extracts it to a folder and opens the project inside | `File › Export › Zipped loop package...` | Also opens FL Studio's zipped loop package |
| `.lynxpack` | Opens after a summary. A part is merged into the host's project | `File › Online session › Send project...` | A project or a part sent in an [online session](/wiki/online-sessions/). Third-party plugin parts travel as audio renders plus the plugin settings |
| `.flp` | `File › Open...` | `File › Export › FL Studio project...`, an FL Studio 2026 project or a zipped loop package | Native plugins play on recreated engines of varying exactness. FLEX packs are never opened. On export, LYNX-only instruments and effects are rendered to audio or swapped for the nearest FL plugin, and modulators are left out. See [Coming from FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `File › Open...` | `File › Save a copy as FL Studio Mobile project (.flm)...` | FL Studio Mobile project. Partial: instruments other than MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, the Slicer and DirectWave play the built-in synth. Export is a copy: it keeps the notes, tempo, mixer, FL Studio Mobile's own instruments, the DW Sampler, 3x Osc, drum kits, effects and the automation of mixer level, pan, effects, the DW Sampler and GMSynth, with the samples beside it. In the copy, other instruments keep their notes on the MiniSynth, and effects without an FLM module and automation of other targets (the synths' own knobs, third-party plugins) are left out; a window lists all of it before saving. |
| `.als` | `File › Open...`, sets of Live 11 and 12 | `File › Export › Ableton Live set (.als)...`, for Live 12 or Live 11 | On import, Live's devices with no LYNX SHARE equivalent are dropped. On export, LYNX SHARE's own instruments and effects are not converted (Helix becomes Wavetable), and effect-parameter automation and insert-to-insert routes are dropped. See [Coming from Ableton Live](/wiki/coming-from-ableton/) |
| `.dawproject` | `File › Open...` | `File › Export › DAWproject (.dawproject)...` | The open exchange format of Bitwig, Studio One, Cubase and other apps: tracks, folders, clips, notes, audio with its warp markers, automation, tempo, markers, the mixer with sends, and VST3, VST2 and CLAP plugins with their state. Note effects and MIDI expressions are left out on import; the clip grid, other arrangements and some per-note details on export. Both directions list what was left out |
| `.band` | `File › Open...` (the package, its `projectData` file or a zip of it) | No | GarageBand 10 or GarageBand for iOS project, as Simple-layout tracks. Partial: instrument tracks play the built-in synth and drums, except Sampler instruments made in GarageBand for iOS. Projects from GarageBand 11 and older are not readable |
| `.blx` | `File › Open...`, or its folder with the Simple layout's `Folder...` import | No | BandLab project downloaded by Cakewalk or Sonar, as Simple-layout tracks with regions, tempo, key, the mix, volume and pan automation and the common effects. Partial: BandLab's instrument sounds and other pedals are listed in the import report |
| `.mid`, `.midi` | `File › Open...` makes a new project. `File › Import › MIDI file...` and the Piano roll import into the current one | `File › Export › MIDI file...` (`Ctrl+Shift+M`) | |
| `.mod`, `.xm`, `.s3m`, `.it` | `File › Open...` | No | Tracker modules (ProTracker, FastTracker 2, Scream Tracker 3, Impulse Tracker). The instruments become Sampler channels with their samples, the song becomes pattern clips with its tempo changes, and `View › Tracker` edits the notes. Partial: effects such as slides, vibrato and arpeggios, envelopes and volume changes on a sounding note are listed in the import report |
| `.wav` | As a sample or an audio clip | `File › Export › Wave file...`, 16-bit, 24-bit or 32-bit float | Optional dither, tempo information, loop markers and Playlist markers |
| `.mp3` | As a sample or an audio clip | `File › Export › MP3 file...` | Constant bitrate |
| `.ogg` | As a sample or an audio clip | `File › Export › OGG file...` | |
| `.flac` | As a sample or an audio clip | `File › Export › FLAC file...`, compression 0 to 8 | Lossless at every level |
| `.aif`, `.aiff` | As a sample or an audio clip | `File › Export › AIFF file...`, 16-bit or 24-bit | |
| `.m4a` | – | `File › Export › M4A file...` | Export only. AAC with the system's encoder on Windows and macOS; not offered on Linux |
| `.wv` | As a sample | No | WavPack samples, as shipped in FL Studio's packs |
| `.sf2`, `.sf3` | Played by LYNX Soundfont | No | SoundFonts |
| `.dwp`, `.dwb` | Double-click in the Browser, a drop on the Channel rack, or DirectWave's `Open program / bank...` | DirectWave's program and bank save | DirectWave programs and banks. The samples are looked for where DirectWave looks for them |
| `.gig` | As a DirectWave channel | No | GigaStudio instruments: velocity layers, round robin, loops, tuning and the amp envelope. Release-trigger samples, keyswitches and compressed samples are not read yet |
| `.ncw` | As a sample or an audio clip | No | Kontakt's compressed samples. Kontakt `.nki` instruments need Kontakt itself as a plugin; protected libraries are never decrypted |
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
| `File › Export › For other apps...` | One folder with stems, MIDI files, an Ableton Live set, an FL Studio project, the mixdown and a README, zipped if you want |
| `File › Export › Project bones...` | Writes channel and effect presets and scores to a folder |

The export window renders the song, the current pattern or the Playlist selection.

## From the command line

On Windows the program is `LYNX.exe`. On macOS and Linux it is `LYNX`.

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` and `--export-flp` also accept an `.als` as the input.

## Good to know

- Audio files longer than 30 seconds are streamed from a disk cache.
- A `.flp`, `.als`, `.flm`, `.dawproject`, `.band` or `.blx` is never written when you save. The export commands write new files.
- `.wv` files are read only.
- A `.zip` is extracted to a folder first, then the project inside is opened.

## Related

- [Importing projects](/wiki/importing-projects/)
- [Exporting](/wiki/exporting/)
- [Command line](/wiki/command-line/)
- [Coming from FL Studio](/wiki/coming-from-fl-studio/)
- [Coming from Ableton Live](/wiki/coming-from-ableton/)
