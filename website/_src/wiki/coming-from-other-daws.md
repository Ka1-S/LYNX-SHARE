---
title: Coming from Logic, Cubase or Reaper
summary: For people who know a track-based DAW (also Pro Tools and Studio One): the layout to pick, what works the same, the keys that differ, and moving projects across.
status: full
---
LYNX SHARE has three layouts. Coming from Logic, Cubase, Reaper, Pro Tools or Studio One, start with the **Simple layout with the Mixer at hand**: one track per instrument or voice on a timeline, as you know it. The Pattern layout, FL Studio's way of working, is there when you want patterns.

## Your first steps

1. **Pick the layout.** The first time LYNX SHARE starts, a welcome card asks where you are coming from. Click `Logic, Cubase, Reaper, Pro Tools or Studio One`: the Simple layout opens on its New track card, and the Mixer comes up with your first track. Later, `View › Layout style › Simple layout` switches to it at any time, and `F9` opens the Mixer.
2. **Add a track.** `+ Add track` offers `Voice / audio`, `Instrument`, `Drums`, `Sampler`, `Guitar / bass`, `Import` and `Loops`. Play an instrument track with the computer keys, the on-screen keyboard or a MIDI keyboard.
3. **Record.** Pick your audio device in `Options › Audio settings...`, then see [Record your voice](/learn/record-voice/).

## What is the same

- Tracks on a timeline with audio, MIDI and automation clips.
- Audio and MIDI recording, with loop takes and comping.
- A Mixer with inserts, sends, sidechain, groups and delay compensation.
- Third-party plugins: VST3 and CLAP on every system, Audio Units on macOS, LV2 on Linux, VST2 on Windows and Linux.
- Export of the song, of stems per track, and of MIDI.

## What is different

- **Tracks and mixer inserts are separate.** In the Simple layout each new track gets its own channel and mixer track. In the Pattern layout you route channels to inserts yourself, as in FL Studio.
- **Patterns.** In the Pattern layout, notes live in patterns that you place on the Playlist, and one pattern can hold several instruments.
- **The keys follow FL Studio.** `Ctrl+Z` undoes one step, and pressed again redoes it. `Ctrl+Alt+Z` goes further back.
- **No project import from Logic, Cubase, Reaper or Pro Tools.** Move projects with DAWproject files, stems or MIDI (below).

## Keys

| To | Press |
|---|---|
| Play, or stop and go back | `Space` |
| Record on or off | `R` |
| Mixer | `F9` |
| Piano roll | `F7` |
| Playlist | `F5` |
| Browser | `Alt+F8` |
| Open, save | `Ctrl+O`, `Ctrl+S` |
| Undo, and further back | `Ctrl+Z`, `Ctrl+Alt+Z` |
| Export audio, MP3, MIDI | `Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+Shift+M` |

`Help › Shortcuts` lists every key. See [Shortcuts](/wiki/shortcuts/).

## Moving projects across

- **Cubase, Studio One and Bitwig**: save a DAWproject file (`.dawproject`) there and open it with `File › Open...`. Tracks, folders, clips, notes, audio with its warp markers, automation, tempo, markers, the mixer with sends, and VST3, VST2 and CLAP plugins with their state come over. `File › Export › DAWproject (.dawproject)...` goes the other way.
- **Logic, Reaper and Pro Tools**: export each track as an audio or MIDI file from the start of the song into one folder. In LYNX SHARE click `+ Add track`, then `Folder...` beside `Import`: each file becomes a track that starts at the beginning.
- **Back out**: the export window writes one file per mixer insert or per channel, and `File › Export › MIDI file...` writes the notes.

See [File formats](/wiki/file-formats/) and [Exporting](/wiki/exporting/).

## Good to know

- Logic is a trademark of Apple Inc., Cubase of Steinberg Media Technologies, Reaper of Cockos Incorporated, Pro Tools of Avid Technology, Studio One of PreSonus Audio Electronics and Bitwig of Bitwig GmbH. LYNX SHARE is an independent program, not affiliated with or endorsed by them.
- The Clip-launch layout adds a clip grid with scenes beside the timeline. See [Layouts](/wiki/layouts/).

## Related

- [Layouts](/wiki/layouts/)
- [Mixer](/wiki/mixer/)
- [Recording](/wiki/recording/)
- [Coming from FL Studio](/wiki/coming-from-fl-studio/)
- [Learn: tutorials](/learn/)
