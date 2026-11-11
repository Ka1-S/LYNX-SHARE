# REA round 190as: FL gap hunt (Find similar samples, MIDI controller scripting)

FL Studio 2026 (25.1.x, `FLEngine_x64.dll` of 2026-10-08, image base 0x400000), its shipped files read in place, and
FL's online manual. Clean-room: behaviour and formats only; LYNX's code is its own. The third feature of this round,
Piano roll scripts, is in `REA_rodada190as-pyscript.md` (branch `feature/pyscript190as`).

Evidence tags: [LIVE] seen in the running FL, [BIN] FLEngine strings / code, [FILE] a file FL ships, [MAN] FL's manual, [LOG] FL's change log
(`Help/WhatsNew.htm`), [EXEC] measured by running LYNX's code.

## 1. "Find similar samples"

### 1.1 What FL does

**FL's similarity search is an FL Cloud query. FL analyses nothing on the user's disk and keeps no local index.**

| Fact | Proof |
|---|---|
| The command exists in two places: the Sounds tab's per-sound menu (three dots) and Loop Starter's channel menu | [MAN] Browser › Sounds tab › Sound menu: "Open/Edit in...", "Find similar samples", "Show in FL Studio Browser", "Generate PDF license", "Show less like this", "Report". [BIN] the only "similar samples" string in the engine is the Loop Starter channel menu's `Find similar samples...` with the hint `Find samples that match the loop in FL Cloud` (menu builder at `0x1035046`/`0x103506c`, next to `Show more from this pack...` / `Show this pack in FL Cloud`) |
| FL's ordinary Browser (local folders) has no such command | [BIN] no other "similar" menu string in `FLEngine_x64.dll` or `FL64.exe` (the Sounds tab's own menu is web content: the tab loads `https://sounds.<domain>/fl-studio...` in WebView2); [LIVE] 2026-11-11, ALL tab › Packs › Loops › right-click `DL Ninety.wav`: Send to selected channel or focused plugin, Open in new channel, Send to playlist as an audio clip / audio track / instrument track, Edit in audio editor / pitch corrector / time warper, Open in new sampler / Fruity Granulizer / Fruity Slicer / Fruity Slicer 2 / Slicex channel, System (Windows shell menu, Open parent folder, Locate file, Delete file...), Tags (Favorite, Tags, Add tag, Edit tags). No similarity item (`shots/similar-fl-browser-file-menu.png`) |
| The Loop Starter item only builds a search string and opens the Sounds tab with it | [BIN] click handler `0x1036470` → `0xdc0d40`: builds the channel's query (`0xdc24c0`), prefixes `?searchString=` (`0xdc0e28`) and calls the Sounds-tab opener `0x1083710`. No audio is read in that path |
| The query carries tags and key, not audio features | [LOG] "20957 Added key info to the FL Cloud similar sound query", "20970 Loop Starter similar search breaks with sharpened root notes", "20372 Find similar samples always adds tags to the previous search", "19910 Implemented 'Find similar' option to search in Sounds tab"; [BIN] the neighbouring query fields `rootNote=`, `chordType=`, `bpm=`, `packGenres=`, `packName=` |
| The ranking is the service's | [MAN] "Shows a list of samples with a similar sound. The similarity search includes both tone and rhythm." Endpoints in the engine: `https://search.<domain>/search/file-by-id/`, `.../search/pack`, `.../secured/loop-starter/batch` [BIN]. What the service computes is not in FL's binaries |
| Results | [MAN] the Sounds tab's usual result list (waveform, length, name, type, tags; filters shown as removable tags; sort: Most popular, Hidden gems, Newest arrivals, Shortest first, Longest first, Random) |

So the questions of the brief have these answers: UI = one menu entry, results in the Sounds tab's list, no settings;
analysis = none in FL (server side, undisclosed); index file = none; when computed = never locally; ranking = the
service's, not reproducible offline. A like-for-like ranking comparison with FL is therefore not possible on local
packs: FL cannot rank them.

Live look (2026-11-11, under the GUI and FL locks): the Sounds tab opened on FL Cloud's "Personalize your experience"
page (`shots/similar-fl-sounds-tab.png`). The tab is a WebView2 child process, so the round's input guard
(`safeinput.ps1`: the point must belong to FL's own process) refuses every click into it; the guard was not bypassed,
so the Sounds list and its "Find similar samples" result list were not driven or shot. Evidence tag [LIVE] below is
from that session. FL changes made: closing the Welcome window loaded FL's default template; nothing was saved.

### 1.2 What LYNX does (its own, local)

The same workflow for the files the user already has: right-click an audio file in the Browser › **Find similar
samples** (FL's wording, placed after "Edit in the audio editor...", as FL's item follows "Open/Edit in...") › the
Browser's results list shows the samples of its folders that sound most alike, most similar first, each with a
percentage. The search box reads "Similar to <name>"; Esc, clearing it or typing another search leaves the list.

- `core/SampleSimilarity.{h,cpp}` (no JUCE):
  - `analyse()`: 49 numbers per file from its first 20 s after the leading silence, level-normalised: 12 MFCC means
    and 12 deviations (26 mel bands, 30 Hz to 16 kHz, frames of about 46 ms, hop a quarter), spectral centre, its
    movement, flatness; length, attack, where the energy sits, decay, sustain (level every 2 ms); number and rate of
    onsets, how regularly the spectral rise repeats (lags of 0.2 to 1 s) and its period; 12 pitch classes and how much
    one stands out.
  - `Index`: path, file size and time, the vector; about 326 bytes per file on disk [EXEC]; at most 150,000 files
    (about 45 MB); files over 200 MB are not analysed; a cut or foreign index file is read as far as it is whole.
  - `rank()`: each dimension divided by its spread over the index, groups weighted (tone 1, tone movement 0.2,
    brightness 0.5, shape 3, rhythm 0.4, pitch classes 0.15, tonal 0.05), squared distance, nearest first.
  - `Service`: one worker at the system's lowest priority (Windows background mode, which also lowers its disk
    priority; QoS background on macOS; nice 19 on Linux). It rests as long as each file took (at most half of one
    core) and waits while LYNX plays, records or renders. A query is served before the pass goes on and analyses the
    clicked file first. Nothing is killed: it stops between two files and saves.
- The index is `browser-similar.idx` in LYNX's settings folder. **Nothing is analysed until the command is used
  once**; from then on the folders are checked for new samples 20 s after each start.
- Decoding goes through the engine's readers (`SampleLibrary::readMono`: FL's Ogg-in-WAV, WavPack, FLAC, MP3, AIFF...)
  with a library object of its own, so nothing enters the engine's sample cache.

### 1.3 Validation [EXEC]

FL's ranking cannot be the reference (1.1). The reference used instead is FL's own pack layout: a sample's class is
the pack folder it sits in (`Data/Patches/Packs`: Drums/Kicks, Snares, Hats, Claps..., Instruments/Bass..., Loops,
Risers, Vocals, Shapes; "Legacy" and "Kits" are in the pool but not used as queries, their folders mix classes).
4,825 files (3,150 Ogg-in-WAV / PCM WAV, 1,675 WavPack), every labelled file asked once, the share of its 10 nearest
files that sit in a folder of the same class (harness: `/mnt/e/lynx-work/flgap190as/similar/harness.cpp`):

| class | queries | P@10 | chance |
|---|---|---|---|
| kick | 478 | 89.2 % | 9.9 % |
| riser | 25 | 89.2 % | 0.5 % |
| keys | 235 | 70.0 % | 4.9 % |
| shape | 41 | 69.3 % | 0.8 % |
| snare | 377 | 67.6 % | 7.8 % |
| hat | 428 | 67.5 % | 8.9 % |
| bass | 46 | 64.6 % | 1.0 % |
| clap | 121 | 58.0 % | 2.5 % |
| loop | 15 | 54.7 % | 0.3 % |
| orch | 76 | 53.2 % | 1.6 % |
| vocal | 47 | 50.4 % | 1.0 % |
| guitar | 106 | 49.6 % | 2.2 % |
| cymbal | 77 | 41.0 % | 1.6 % |
| tom | 77 | 29.9 % | 1.6 % |
| shaker | 48 | 28.3 % | 1.0 % |
| rim | 57 | 16.0 % | 1.2 % |
| **mean of 16 classes** | 2,254 | **56.2 %** | 2.9 % |

Limits of this number: folder names are a coarse truth (a tom next to a kick, a shaker next to a hat or a rim next to
a snare counts as a miss although they do sound alike), the set is drum-heavy, and the group weights were chosen on
this same set (a coordinate search reached 57.1 % with shape weighted 10 and rhythm and pitch classes near 0; LYNX
keeps milder weights so that loops still match by rhythm and key). It shows that like goes with like, not how close
LYNX is to FL Cloud.

Cost on this machine while it was saturated (load 35): 46 ms per file including decoding from the Windows drive
(4,825 files, 12,123 s of audio, 223 s); a query over the 4,825 files takes 0.22 ms.

### 1.4 Best way?

- **FL:** online only (1.1): finds new sounds in FL Cloud's catalogue, nothing for the user's own library.
- **Ableton:** Live 12 has similarity search for the local library: a "Show similar files" button on a Browser entry
  lists similar samples, drum hits and presets, analysed in the background, for sounds up to about a minute. That
  is the model closest to what LYNX does here. (Live 12 manual, Browser chapter; not REA'd in this round.)
- **Users:** split on quality, agreed on speed: a fixed precomputed measure answers at once (Loopcloud), user-weighted
  measures are flexible but slow (Sononym); one buyer asked for a refund over ranking quality
  (CROSS-DAW F35, KVR Nov-Dec 2025).
- **Verdict:** FL-exact is not possible offline and would only cover FL Cloud. LYNX keeps FL's command name and
  place and a precomputed local measure. Options left to the user, not built: weights by the user (tone / shape /
  rhythm sliders), "similar" as a sort order of ordinary search results, similarity inside Loop Starter's "Replace
  sample".

## 3. MIDI controller scripting (spec only)

Decision of 2026-11-11 (user): built-in profiles for common keyboards first, then LYNX's own local control API
(issue #75); FL-compatible device scripts are a later phase. Nothing was built for this in round 190as.

### 3.1 What FL has

- **Scripts**: Python 3.12 (`Shared/Python/python312.dll` [FILE]) files named `device_<name>.py`
  ([BIN] `device_*`), one folder per controller under `System/Hardware specific/<name>/` (factory, 30 folders, only
  one script ships in the install: "Forward CCs to current plugin Port 10") and the user's
  `Documents/Image-Line/FL Studio/Settings/Hardware/<name>/` ([MAN] the folder; [BIN] the list labels `(user)` / `(factory)`).
  The first comment lines are metadata: `# name=`, `# url=`, `# receiveFrom=`, `# supportedDevices=`,
  `# supportedHardwareIds=` [BIN][FILE]; FL matches a connected device by name or by its SysEx identity reply
  ("MIDI detection") and offers the script as the device's Controller type in MIDI settings. FL also reports
  "New version of script is available" / "New firmware is available for your MIDI device" [BIN].
- **Events** FL calls when the script defines them [BIN, the table at utf-16 strings "OnMidiIn"...]:
  `OnInit`, `OnDeInit`, `OnMidiIn(event)`, `OnMidiMsg(event)`, `OnMidiOutMsg(event)`, `OnNoteOn`, `OnNoteOff`,
  `OnControlChange`, `OnProgramChange`, `OnPitchBend`, `OnKeyPressure`, `OnChannelPressure`, `OnSysEx`, `OnIdle`,
  `OnRefresh(flags)`, `OnDoFullRefresh`, `OnUpdateBeatIndicator(value)`, `OnDisplayZone`, `OnUpdateLiveMode(lastTrack)`,
  `OnDirtyMixerTrack(index)`, `OnDirtyChannel(index, flag)`, `OnUpdateMeters`, `OnWaitingForInput`,
  `OnSendTempMsg(msg, duration)`, `OnFirstConnect`, `OnProjectLoad(status)`, `OnInitAll`.
- **Event object** fields [BIN]: `handled`, `timestamp`, `status`, `data1`, `data2`, `port`, `note`, `velocity`,
  `pressure`, `progNum`, `controlNum`, `controlVal`, `pitchBend`, `sysex`, `isIncrement`, `res`, `inEv`, `outEv`,
  `midiId`, `midiChan`, `midiChanEx`, `pmeFlags`. Flow (FL's manual, MIDI scripting): `OnMidiIn` sees every message
  first; unhandled ones go to `OnMidiMsg` and then to the typed callbacks; what no callback marks `handled` falls
  through to FL's generic handling (notes to the channel, links). `pmeFlags` says what the script may do now
  (`midi.PME_*`: system, system-safe, preview note, from host / from MIDI).
- **Modules** (C functions exported to Python, names and one-line docs read from the engine's method tables [BIN];
  391 functions in this build, list in `/mnt/e/lynx-work/flgap190as/rea/flpy-api.txt`):

| module | functions | covers |
|---|---|---|
| `transport` | 19 | start / stop / record, song position, loop mode, jog, `globalTransport(command, value, pmeFlags)` = FL's ~70 `FPT_*` commands |
| `mixer` | 72 | track name / colour / volume / pan / stereo / mute / solo / arm, routing, EQ bands, slots, peaks, tempo, event ids (`getEventValue`, `automateEvent`) |
| `channels` | 50 | channel name / colour / volume / pan / pitch / mute / solo / select, target insert, step grid bits and step parameters, `midiNoteOn`, graph editor, Loop Starter re-rolls, swing |
| `patterns` | 31 | number, name, colour, length, select, clone, clear, move, jump, groups, burn loop |
| `playlist` | 42 | track name / colour / mute / solo / select, performance mode (live blocks, trigger, snaps), display zone, visible time |
| `arrangement` | 12 | markers, time selection, current time |
| `ui` | 57 | windows (focus, show, hide, next), hint message, notifications, jog / strip / arrows / zoom, snap, Browser navigation, metronome and record switches, picker |
| `device` | 29 | MIDI out (`midiOutMsg`, `midiOutSysex`), dispatch to other scripts (`receiveFrom`), linked-control values, refresh thread, `getDeviceID`, `linkToLastTweaked`, master sync |
| `general` | 30 | undo history, PPQ, time signature, changed flag, metronome / precount, `safeToEdit`, project title / author / genre, `getVersion` (API version) |
| `plugins` | 12 | `isValid`, names, parameter count / name / value / value string, set value, presets, colour, pad info |
| `launchMapPages` | 12 | pad-grid page maps (Launchpad-style overlays) |
| `screen` | 25 | device text screens (AKAI Fire style): text lines, meters, rectangles, menus |
| `midi` (Python, 770 lines) | 571 constants | `REC_*` event ids (150), `FPT_*` (66), `SBN_*`, `HW_*` dirty flags, `PME_*`, `MIDI_*` status bytes [FILE] |
| `utils` (Python, 213 lines) | 24 | colour and value helpers, `TRect`, note names [FILE] |

- **Refresh model**: FL calls `OnRefresh(flags)` with `HW_Dirty_*` bits (mixer selection, mixer display, mixer
  controls, remote links, LEDs, patterns, tracks, colours, names, channel rack...; [FILE] `midi.py`), `OnIdle` about every 20 ms [MAN],
  `OnUpdateBeatIndicator` on bars / beats, `OnUpdateMeters` when the script asked with `device.setHasMeters()`.
- **Script output window** (View › Script output) with one tab per device plus an interpreter line, and "Reload
  script" [MAN].

### 3.2 What LYNX has today

- Generic controller only (Settings › MIDI: "Controller type: Generic controller"): per-control links from a control's
  right-click menu › Link to MIDI controller (CC + channel, per project or global), Multilink (`core/Multilink.h`),
  "receives notes from" channel locks, velocity curves, Song marker jump channel, MIDI clock in / out
  (`core/MidiSync`), gamepad maps (`core/GamepadMap`).
- No controller types, no device identification, no feedback to the device (LEDs, motor faders, screens), no
  transport / mixer-bank mapping without manual links.
- The embedded Python (`core/Python.h`, pocketpy, sandboxed, interruptible, 15 VMs, a Service woken from another
  thread) already runs FL's VFX and Edison scripts, and (this round) Piano roll scripts.

### 3.3 Plan

| phase | what | effort |
|---|---|---|
| A. Controller layer (needed by every later phase) | `core/ControllerSurface`: a device session with input events, a model of "what the surface shows" (transport, selected channel, 8-strip mixer bank, selected plugin's first 8 parameters) and dirty flags; MIDI output to the same device; device identification by port name and SysEx identity reply; Settings › MIDI "Controller type" list | M (1 round) |
| B. Built-in profiles (user's first choice) | declarative tables on layer A for common keyboards: transport, 8 knobs to the selected instrument or mixer bank, pads to the Channel Rack / FPC notes. First set by what users own (to confirm): Akai MPK mini mk3, Novation Launchkey mk3 / mk4, Arturia MiniLab 3 / KeyLab Essential, M-Audio Oxygen Pro, Nektar Impact LX+, Mackie Control Universal / HUI as the generic motor-fader protocol. Each needs the maker's public MIDI implementation chart; without the hardware, tested against recorded MIDI and a loopback | M per 4 to 5 devices; MCU alone M |
| C. LYNX local control API (issue #75) | the same model over a local socket (JSON), so tools and community surfaces can drive LYNX; shares layer A's commands and dirty flags | M to L (the review of PRs #71 / #76 lists the open security points) |
| D. FL-compatible device scripts | `device_*.py` run by the embedded Python: event dispatch (3.1), then the modules in order of use by real scripts: `midi` / `utils` constants (LYNX's own values file, same names), `device`, `transport`, `ui` (hint, windows), `mixer`, `channels`, `patterns`, `general`, `plugins`, then `playlist` performance mode, `launchMapPages`, `screen`. About 390 functions; most map onto layer A's model. pocketpy runs most Python 3 but not CPython extensions; scripts that import `time`, `math`, `sys` work, those that need threads (`device.createRefreshThread`) need a LYNX timer instead | L (3 to 4 rounds: 1 dispatch + device / transport / ui, 1 mixer / channels / patterns, 1 the rest, 1 testing with community scripts read in place) |

Open questions for the user (not blocking phases A to C): which keyboards come first; whether phase D may read FL's
`midi.py` constants from the user's FL install at run time (as Piano roll scripts are read in place) or must ship its
own table of the same public names and values.
