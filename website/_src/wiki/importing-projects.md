---
title: Importing projects
summary: Open FL Studio, Ableton Live, FL Studio Mobile, DAWproject, GarageBand, BandLab and MIDI files in LYNX SHARE.
status: full
---
`File › Open...` (`Ctrl+O`) opens LYNX SHARE's own `.lynx` projects and also `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx` and `.mid` files. A project from another program is an import: it becomes a new, unsaved LYNX SHARE project, and the original file is never written.

## Open a project from another program

1. Choose `File › Open...` and pick the `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid`, `.zip` or tracker module (`.mod`, `.xm`, `.s3m`, `.it`) file.
2. Wait while LYNX SHARE looks for the project's plugins and samples.
3. If something was not found, the `Missing files` window opens. Point it to your samples, or close it.
4. The title bar now shows where the project came from, for example "(imported from FL Studio)".
5. Choose `File › Save`. LYNX SHARE asks where to write a `.lynx` file.

## What comes over

| Format | Converted | Not converted |
|---|---|---|
| `.flp` (FL Studio) | Channels, samples, patterns, slide and porta notes, every arrangement, markers, time-signature changes, automation, controller links, the Mixer with routing, sidechains, track EQ and native effects | FLEX packs are encrypted and never opened. A FLEX channel gets a synth patch chosen by its preset name |
| `.als` (Live 11 and 12) | MIDI, audio, group and return tracks, tempo, time signature, locators, the loop, volume, pan, mute, solo and sends with their automation, Session clips and scenes, warp markers, racks and Drum Racks, and many of Live's own devices (EQ Eight, Compressor, Glue Compressor, Reverb, Hybrid Reverb, Delay, Grain Delay, Auto Filter, Saturator, Roar, Utility, Simpler, Drift, Operator, Wavetable and more) as LYNX SHARE's own | Live's devices with no LYNX SHARE equivalent (Amp, Cabinet, Beat Repeat, Vocoder and others) are dropped and listed |
| `.flm` (FL Studio Mobile) | Tempo, time signature, one mixer insert per channel, one Playlist track per lane, patterns, audio clips with offset and pitch, drum kits, DW Sampler and `.dwp` programs, the Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth and SuperSaw, the effects, the automation and the chord sets | Other FLM generators play the built-in synth |
| `.dawproject` (Bitwig Studio, Studio One, Cubase and other apps) | Tracks, folders, clips, notes, audio with its warp markers, automation, tempo, markers, the mixer with sends, scenes into the clip grid, and VST3, VST2 and CLAP plugins with their state. The format's EQ, compressor, gate and limiter become LYNX SHARE's own | Note effects, per-note expressions and the other app's own devices are listed in the import report |
| `.band` (GarageBand 10, GarageBand for iOS) | Tracks, MIDI and audio regions, tempo, time signature, key, sections as markers, level, pan and mute, as Simple-layout tracks. Sampler instruments made in GarageBand for iOS | Instrument tracks play the built-in synth and drums. Drummer regions, take folders and tempo changes are listed. Projects from GarageBand 11 and older are not readable |
| `.blx` (BandLab) | Tracks, MIDI and audio regions with their loops, tempo, time signature, key, volume, pan and mute, volume and pan automation, and the common effects, as Simple-layout tracks | Instrument tracks play the built-in synth and drums. Other effects and the Mastering preset are left out and listed. Tested on made-up projects only |
| `.mid` | Notes, time signatures and every tempo change | A MIDI file carries no sounds: channels get the LYNX SHARE synth or a MIDI Out channel |
| `.mod`, `.xm`, `.s3m`, `.it` (tracker modules) | Instruments as Sampler channels with their samples, patterns, the order list as the Playlist, speed and tempo, note volume, pan, delay and cut; volume and pan changes as the channel's events (MOD and XM), and slides, vibrato, arpeggios and pitch envelopes as the notes' pitch curves | Other effects and instrument envelopes are counted in the import report. `View › Tracker` edits the notes the tracker way |
| `.zip` | FL Studio's zipped loop package, or a project exported with its samples: unpacked into a folder of its own, then the project inside opens with its samples | |

Third-party VST2, VST3 and CLAP plugins in an `.flp` or a `.dawproject`, and VST2 and VST3 plugins in an `.als`, load with their saved state if the same plugin is installed on your computer.

## FL Studio's own plugins

FL Studio's generators and effects are not loaded. LYNX SHARE reads their saved settings and plays them on engines it recreated. Each engine has a quality level, from `Exact` (bit-identical to the original on every state tested) down to `Placeholder` (the settings are kept, but the plugin is silent or passes audio through). The levels are explained in [Coming from FL Studio](/wiki/coming-from-fl-studio/).

Opening an `.flp` works without FL Studio installed. Some engines need waves, samples or impulses that are read from your own FL Studio installation at run time.

An `.flp` that uses FL Studio's "FL Studio Mobile Rack" (FL Studio Mobile's own "Save as FLP" writes one per instrument track) plays its instrument and effects on the same engines as an `.flm`, and the "FL Studio Mobile Rack FX" effect too. A rack with a drum kit or several instruments still plays the built-in synth.

## FL Studio Mobile factory sounds

The factory sounds an `.flm` uses (drum kits, DirectWave programs, SoundFonts, the GMSynth waves) belong to FL Studio Mobile and are never shipped with LYNX SHARE. LYNX SHARE looks for them in your own copy of the app's `Resources` folder: add it under `Settings › Files › FL Studio Mobile resources` (picking the folder above it, or an FL Studio installation folder, also works). FL Studio's install holds only part of them, and is searched after your folders. Files that are still missing are listed in the Missing files window.

## Missing samples

![The Missing files window on its Samples tab: found files in green, missing ones in amber, with Search folder, Locate and Skip](/assets/img/wiki/missing-files.webp)

The `Samples` tab lists every sample that was not found, with its saved path and what uses it.

| Command | What it does |
|---|---|
| `Search folder...` | Searches a folder and its sub-folders for all missing samples |
| `Locate...` | Asks where one sample is. Others found in the same folder are picked up too |
| `Skip` | Leaves the sample out. Its channel stays silent |

`Apply` uses the files that were found.

The `Found automatically` tab lists files LYNX SHARE found in another folder by itself. Save the project to keep the new places.

## Missing plugins

LYNX SHARE looks for each third-party plugin by its identifier, then by its file, then by its name. A project saved with the VST2 opens with the VST3 of the same product and the other way round, but then with default settings.

When an effect is not installed, a LYNX SHARE effect with the same role takes the slot: a reverb for a reverb, a compressor for a compressor.

- The slot shows the original plugin's name with a LYNX SHARE badge.
- Where the saved state can be read, the settings are translated. Otherwise the substitute starts from a default preset. The table of products, their substitutes and what is translated for each is in [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- Automation of the original's parameters is kept but does not drive the substitute.
- The original plugin's reference and state stay in the project. Once the plugin is installed, the project opens with it again.
- `Bypass all LYNX SHARE substitutes of this project`, in a substitute slot's right-click menu, switches them all off at once.

The switch is `Substitute missing plugins with LYNX SHARE equivalents` in `Options › General settings...`, on by default. With it off, the slot of a missing effect is left out of the mixer. A missing instrument is not substituted. It becomes a built-in synth with a preset chosen by the channel name.

To get the real plugin back, install it, choose `Options › Scan for plugins`, then open the project again.

## MIDI files

![The Import MIDI data window: tracks, channels, channel type and options](/assets/img/wiki/midi-import-dialog.webp)

`File › Open...` makes a new project from a MIDI file. `File › Import › MIDI file...` opens the `Import MIDI data` window first, where you choose what to bring in.

| Setting | What it does |
|---|---|
| `Which tracks to import?` | All tracks, or one |
| `Which channels to import?` | Click a MIDI channel to leave it out. Right-click keeps only that one |
| `Channel type` | `LYNX SHARE synth` or `MIDI Out` |
| `Start new project` | Starts a blank project. Off: adds to the open one |
| `Create one channel per track` | Off: one channel per MIDI channel |
| `Set mixer tracks for new channels` | Routes the new channels to consecutive mixer inserts |
| `Import time signatures` | The file's time signature becomes the project's |

`Ctrl+M` in the [Piano roll](/wiki/piano-roll/) imports a MIDI file into the current channel.

## Import tracks from another project

`File › Import › Tracks from another project...` brings tracks of another project into the open one, without closing it. It reads every format `File › Open...` reads.

1. Choose the project. A window lists its Playlist tracks and what each one holds.
2. Tick the tracks you want.
3. Choose whether `With mixer inserts and effects` and `With automation` come along, and `Place them at` the song start or the start of the time selection.
4. Click to import. The new tracks, channels, patterns and inserts arrive as one step, so one undo takes them all back.

## Good to know

- `Help › Missing files...` opens the window again later. `Help › Last project import report` lists what was approximated, left out or not installed.
- Session clips of a Live set go into the clip grid of the Clip-launch layout. `Session clips of Live sets` in `Options › General settings...` places them on the Playlist instead.

## Related

- [File formats](/wiki/file-formats/)
- [Coming from FL Studio](/wiki/coming-from-fl-studio/)
- [Coming from Ableton Live](/wiki/coming-from-ableton/)
- [Plugins](/wiki/plugins/)
- [Exporting](/wiki/exporting/)
