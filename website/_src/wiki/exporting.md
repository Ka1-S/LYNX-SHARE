---
title: Exporting
summary: Render the song to audio, stems or MIDI, or save it for another DAW.
status: full
---
![The export window with WAV selected: range, tail and files on the left, format on the right, mix options below](/assets/img/wiki/export.webp)

Everything that leaves LYNX SHARE goes through `File › Export`. The audio formats (WAV, MP3, OGG, FLAC, AIFF and M4A) share one export window. MIDI, FL Studio projects, Ableton Live sets, DAWproject files and zipped projects have their own entries in the same menu.

## Export the song as a WAV

1. Choose `File › Export › Wave file...`.
2. Leave `Range` on `Song (Playlist)` and `Files` on `One file (the mix)`.
3. Pick a `Bit depth`: 16 bits, 24 bits or 32 bits (float).
4. Set `Tail` to `Leave remainder` so reverbs and releases ring out after the last bar.
5. Click `Export...`, choose a name and a folder, and wait for the render.
6. When it is done, LYNX SHARE reports where the file went and its peak level.

`Ctrl+R` opens the window with the format you exported last. `Ctrl+Shift+R` opens it on MP3.

## What to render

| Setting | What it does |
|---|---|
| `Range` | `Song (Playlist)`, `Current pattern` or `Playlist selection` |
| `Play` | How many times the range plays in the file, from `Once` to 16 times |
| `Tail` | `Cut remainder` stops exactly at the end. `Leave remainder` lets the sound ring out. `Wrap remainder` mixes the tail into the start, for loops |
| `Longest tail` | 3, 6, 12 or 30 seconds. The tail stops earlier when the sound has faded out |
| `Files` | One file, or stems (see below) |

## Output format

The buttons choose the format. M4A is offered only where the system has an AAC encoder: on Windows and macOS, not on Linux. `Also save` writes other formats from the same render, with the same name.

| Format | Options |
|---|---|
| WAV | `Bit depth`: 16 bits, 24 bits or 32 bits (float). `Dither`: `Off`, `TPDF` or `TPDF + noise shaping` |
| MP3 | `Bitrate`, constant |
| OGG | Bitrate |
| FLAC | `Bit depth`, `Compression` from `0 (fastest)` to `8 (smallest)`, `Dither`. Lossless at every level |
| AIFF | `Bit depth`: 16 bits or 24 bits. `Dither` |
| M4A | `Bitrate`: 96, 128, 160 or 192 kbps. AAC, written by the system's encoder |

`Sample rate` and `Channels` apply to every format. `Channels` is `Stereo`, `Mono (both sides mixed)`, `Mono (left only)` or `Mono (right only)`.

## Mix options

| Setting | What it does |
|---|---|
| `Normalize (peak to 0 dB)` | Raises or lowers the whole file so its loudest peak reaches full scale |
| `Master effects` | Off: the Master insert's effects are bypassed |
| `Insert effects` | Off: the effects of the other inserts are bypassed |
| `HQ for all plugins` | Plugins use their render quality. Off: the export uses the playback quality |
| `Plugin warm-up` | Third-party plugins process a moment of silence before the export starts |
| `Disable maximum polyphony` | Lifts the channels' voice limit, so no note is cut |
| `Trim the delay compensation's silence` | Removes the silence that plugin latency compensation adds at the start |
| `Tempo information`, `Loop markers`, `Playlist markers` | WAV only: store the tempo, the loop and the Playlist's markers in the file |
| `Show the file(s) when complete` | Opens the file manager on the result |

## Stems and tracks

![The export window set to FLAC with one file per mixer insert, nine files in total](/assets/img/wiki/export-flac-stems.webp)

`Files` has three stem modes:

- `One per mixer insert (stems)`: a file for each insert that gets sound.
- `One per channel (stems)`: a file for each channel that plays.
- `One per channel, instruments only (no sends or buses)`.

Tick `Also the full mix` to get the mix next to the stems. The line at the bottom shows how many files will be written.

When the song has sections in the Sections lane, `One file per section` cuts it at the sections and names each file after its section. With stems, each section gets its own set.

`File › Export › All playlist tracks` and `All playlist track groups` write one file per Playlist track or per group. In the Mixer's menu, `Disk recording › Render armed tracks to wave file(s)...` (`Alt+R`) opens the export window for the armed inserts only.

## Other entries of the Export menu

| Command | Key | What it does |
|---|---|---|
| `MIDI file...` | `Ctrl+Shift+M` | Writes the notes as a standard MIDI file (`.mid`) |
| `Master...` | `Ctrl+Alt+M` | Renders the project and masters it to a loudness target in LUFS, on your computer |
| `Zipped loop package...` | none | The project and every sample it uses in one `.zip`, as FL Studio's zipped loop package |
| `Project data files...` | none | Copies every file the project uses to a folder you choose; the project keeps using its own files |
| `FL Studio project...` | none | An `.flp`, or a zipped loop package with every sample |
| `Ableton Live set (.als)...` | none | A set for Live 12 or Live 11 |
| `DAWproject (.dawproject)...` | none | A project for Bitwig Studio, Studio One, Cubase and other apps that read DAWproject |
| `For other apps...` | none | One folder with stems, MIDI files, a Live set, an FL Studio project, the mixdown and a README, zipped if you want |
| `Project bones...` | none | Channel and effect presets and scores, in a folder |

## Exporting to FL Studio, Ableton Live and DAWproject

The `.flp` is an FL Studio 2026 project. Samplers, audio clips, FL Studio's own generators and effects, third-party plugins, patterns, the Playlist, the Mixer and automation go over with their settings. For the things FL Studio does not have, you choose between `Render them to audio (sounds the same)` and `Use the nearest FL plugin (stays editable, may sound different)`. With the second choice, LYNX-only instruments are left out. Modulators that follow the song position become automation clips: a synced LFO becomes FL Studio's clip LFO, and steps, random sources and macros become points. Followers, envelopes, keytrack and velocity are left out. A report lists what was rendered, replaced or left out. Details are in [Coming from FL Studio](/wiki/coming-from-fl-studio/).

The `.als` holds a MIDI track per channel, audio tracks, groups, returns, sends, racks and Drum Racks, VST2 and VST3 plugins with their state, markers, and volume, pan and tempo automation. LYNX SHARE's own instruments and effects are not converted, though their tracks and notes are kept. Helix is the exception: it becomes Live's Wavetable. Automation of effect parameters and insert-to-insert routes are dropped. Details are in [Coming from Ableton Live](/wiki/coming-from-ableton/).

The `.dawproject` holds tracks, folders, clips, notes, audio with its warp markers, automation, tempo, markers, the mixer with sends, and VST3, VST2 and CLAP plugins with their state. LYNX SHARE's own instruments and effects are written so that LYNX SHARE can open them again. The clip grid, other arrangements and some per-note details are left out, and a report lists them.

`File › Save a copy as FL Studio Mobile project (.flm)...` writes an `.flm` for FL Studio Mobile and copies the samples into a `<name> Samples` folder beside it. Before it saves, a window lists what the copy loses, as `File › Save a copy for an older LYNX...` does. The `.flm` keeps the notes and patterns (FL Studio Mobile has one pattern per track), the tempo, beats per bar, key and chord set, the mixer's levels, panning, mute, solo and sends to buses, FL Studio Mobile's own instruments (MiniSynth, SuperSaw, GMSynth and Transistor Bass, and FL Studio's MiniSynth and Transistor Bass), drum samplers as a drum kit, Layers as a rack with several instruments, every Pocket (FLM) effect with its mix, and audio clips with their level, pan, pitch and reverse. It also writes the DW Sampler (with its zones) and 3x Osc, and automation clips of mixer level and pan, of the Pocket (FLM) effects and of the DW Sampler's and GMSynth's own values. Other instruments (for now including FL Studio Mobile's Slicer and SoundFont Player) keep their notes on FL Studio Mobile's MiniSynth, and effects without an FLM module and automation of other targets, such as the synths' own knobs and third-party plugins, are left out; all of it is listed.

## Good to know

- `Dither` is greyed out for MP3, OGG, M4A and 32-bit WAV, and the tempo and marker options are greyed out for every format but WAV.
- With `Playlist selection` and no selection in the Playlist, the whole song is rendered. The window warns you.
- If the Master insert is muted, the window warns that the file will be silent.
- `Also write a video with this mix` appears only when the project has a video track, and it needs FFmpeg.
- `Render through the hardware in real time` appears only when the project has an active hardware insert. That export takes as long as the song.
- Render to audio, where an insert is bounced to an audio clip, plays about 5 dB louder than the insert it replaces in this version.

## Related

- [File formats](/wiki/file-formats/)
- [Command line](/wiki/command-line/)
- [Coming from FL Studio](/wiki/coming-from-fl-studio/)
- [Coming from Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
