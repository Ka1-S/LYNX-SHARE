---
title: Recording
summary: Record audio from an input, notes from a keyboard, and the controls you move.
status: full
---
![The Recording window asks what to record: audio, notes and automation, everything, or audio into the audio editor](/assets/img/wiki/record-dialog.webp)

The record button in the transport, or the `R` key, turns recording on. The first time, a window asks what you want to record. Audio goes to the [Playlist](/wiki/playlist/) as audio clips. Notes go into the current pattern. The controls you move become automation.

## Record a microphone or an instrument

1. Open `Options › Audio settings...` and pick an input device.
2. Open the [Mixer](/wiki/mixer/) (`F9`) and select an empty insert.
3. Click the input box above the insert's effect slots and choose an input. Stereo pairs come first, single channels are marked `(mono)`.
4. Click the record arm button next to the input box.
5. Press `R` and choose `Audio, into the Playlist as an audio clip`.
6. Press play if playback did not start by itself, and perform.
7. Press stop. The take is an audio clip on the Playlist.

`Shift+R` does steps 4 to 6 in one key: it arms the selected insert, records audio only and starts playback. Press it again to stop.

When you choose an input for an insert that records more than that input (other channels sending to it are recorded too), LYNX SHARE asks, as FL Studio does, whether to switch to `External input only`. Answer `Yes` to record the input alone on that insert.

## One-click audio recording

`Tools › One-click audio recording` starts a take in one step, as in FL Studio. It works on the selected mixer insert. With the Master or nothing selected, it takes the first insert with an input, or else a free insert.

| Command | What it does |
|---|---|
| `Into Edison audio editor/recorder` | Puts Edison on the insert, set to record when playback starts, and starts playback. See [Mixer](/wiki/mixer/) |
| `Into playlist as an audio clip` | Switches to Song mode, arms the insert, asks for its input if it has none, and starts recording and playback. Inserts that are already armed record as they are |

## What the Recording window offers

| Choice | What it does |
|---|---|
| `Audio, into the Playlist as an audio clip` | Arms the selected mixer insert and asks for its input if it has none |
| `Notes and automation` | Records notes from a MIDI keyboard or the typing keyboard, and the controls you move |
| `Everything` | Notes, automation, and audio from the armed inserts |
| `Audio, into the audio editor` | Records the input into a new audio editor while the song plays |

Tick `Don't ask again` to keep your choice. After that, right-click the record button for the recording filter: `Automation`, `Notes`, `Audio`, `Clips`, and `Ask what to record when recording starts` to bring the window back.

## The input box of an insert

![The Mixer with insert 3 armed and its input box set to inputs 1 and 2](/assets/img/wiki/mixer-input.webp)

| Setting | What it does |
|---|---|
| `Audio input source` | The device input this insert plays and records, or `(none)` |
| `Record` | Where the take is tapped: `External input only`, `External and mixer input`, `Post effects`, `Post EQ`, `Post level and panning` or `Post track` |
| `Also record the dry input as a second take (LYNX)` | Keeps the unprocessed input next to a processed take |
| `Monitor external input` | `Off`, `When armed`, `Auto (when armed, not while playing back)` or `On` |
| `Record safe (can't be armed)` | Protects the insert from being armed by accident |

## Recording notes

Notes come from a MIDI keyboard, the typing keyboard (`Ctrl+T`) or the touch keyboard, and go to the selected channels in the current pattern. See [MIDI controllers](/wiki/midi-controllers/).

- `Blend recorded notes` (`Ctrl+B`) decides between overdub and replace. On, new notes join the pattern's notes. Off, the old notes of a recorded channel are cleared.
- `Quantize recorded notes to the global snap` is in the recording options.
- `Step edit` (`Ctrl+E`) works while stopped: each note you play goes into the pattern one snap step after the other.
- `Capture MIDI` (`Ctrl+Shift+Alt+R`) puts the notes you just played into the pattern, even though recording was off.
- The sustain pedal (CC 64) and the sostenuto pedal (CC 66) hold the notes you play, and a recorded note lasts until the pedal comes up. A Layer channel passes the pedal on to its channels. With `Foot pedal controls note off` off in `Options › MIDI settings...`, the pedal goes to the plugins as a controller instead.
- In Song mode, notes recorded into a pattern clip that is longer than its pattern stay where you played them, and the pattern grows. The take is not folded back over the pattern's first bars. With `Stretched pattern clips loop their pattern (LYNX)` on, only notes played over bars that already had notes go into the loop.

Controls you move while recording are written as automation clips in Song mode and as pattern events in Pattern mode. See [Automation](/wiki/automation/).

## Recording options

The options are in `Options › Recording` and in the record button's right-click menu.

| Command | Key | What it does |
|---|---|---|
| `Count-in` | `Ctrl+P` | Off, 1 bar, 2 bars or 4 bars of metronome before recording starts. `Ctrl+P` switches between off and the length you chose last. The choice is kept when you close the program; a new install starts with 1 bar, as FL Studio does |
| `Metronome` | `Ctrl+M` | Click while playing |
| `Wait for the first note to start` | `Ctrl+I` | Recording waits until you play |
| `Loop record` | none | Recording loops instead of running on |
| `Audio: punch in/out on the loop region` | none | Plays through the loop region and keeps only the audio inside it |
| `Audio: pre-roll` and `post-roll` | none | Off, 1, 2 or 4 bars played before the punch-in and after the punch-out |
| `Recording starts playback` | none | Turning recording on also presses play |
| `Disarm the inserts when recording stops` | none | Armed inserts are released after each take |

`Low latency while recording` is in the record button's right-click menu and in the Mixer's insert menu under `Disk recording`. It is off by default. While an insert with an input is armed, or its monitoring is `On`, the effects that add latency on its way to the Master are bypassed. This changes only what you hear: an insert keeps all its effects when the take is tapped after them, for example with `Post effects`. The effects come back when the insert is disarmed.

`Latency compensation` is in the same `Disk recording` menu and is on by default, as in FL Studio. It moves each take back by the audio device's round trip, the `Fine adjust (samples)` value of the audio settings and the plugins' delay compensation. Turn it off to keep the takes where they were recorded.

`Tools › Cancel recording` throws away what the current recording pass wrote (its notes, automation and audio takes) and turns recording off. Playback goes on and the armed inserts stay armed.

## Recording into clip slots

In the [Clip-launch workflow](/wiki/layouts/), takes can go into the clip grid instead of the Playlist, as in Ableton Live.

- Arm a track with its arm button in the Session mixer. Its empty slots then show a round record button in place of the stop square. Click one to record into that slot.
- `Session Record`, under the grid, records the armed tracks into the selected scene. While their clips play, new notes are added to them (overdub).
- `Fixed recording length`, in the menu of an empty slot, ends a recording into a slot after 1 or more bars, and the clip plays on. `Off` records until you stop.

## Loop recording and takes

With `Loop record` on, recording loops over the loop region in Song mode, or over the pattern in Pattern mode. With it off, recording plays straight on.

Each pass of an audio loop recording lands on the next free Playlist track, and the tracks form a group. `Blend recorded notes` decides what plays after you stop: off mutes every pass, on plays them all. Right-click a track of the group and choose `Comp these takes` to build one take out of the passes. `After loop recording, only the last pass plays` in the recording options goes back to a single track.

## Where recordings are saved

- A saved project keeps its takes in `Audio/Recorded`, next to the project file.
- An unsaved project records into `LYNX/Recordings` in your Music folder.
- Takes are WAV files at the audio device's sample rate, 16-bit, 24-bit or 32-bit float. The format (`File format`) and the take names are set under `Recording` in `Options › Audio settings...`.
- `Open recordings folder` is in the record button's right-click menu. `File › Delete unused recordings and save...` removes takes the project no longer uses.

## Good to know

- An armed insert without an input records nothing. LYNX SHARE tells you and points to the input box.
- Recording refuses to start when the drive has less than 30 seconds of room, and warns below 10 minutes.
- `Options › Record input audio` records the first two input channels into a new insert when nothing is armed.
- On macOS the system asks for microphone access the first time you record.
- If LYNX SHARE closes while recording, the next start offers to put the recovered takes on the Playlist.

## Related

- [Audio setup](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Automation](/wiki/automation/)
- [MIDI controllers](/wiki/midi-controllers/)
- [Troubleshooting](/wiki/troubleshooting/)
