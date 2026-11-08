# REA round 181b: FL's Audio logger and Chord detection panel → LYNX

2026-11-06, FL Studio 2026 (FLEngine_x64.dll, addresses are Ghidra VAs at image base 0x400000). Clean-room: LYNX's code
was written from the behaviour below and checked by a differential test; no FL code, string table or artwork was copied.

Evidence markers: [STATIC] decompile or form data of FL's binary; [RUN] seen or measured in the running FL; [DOC] manual.

## Audio logger

| Behaviour | FL | Evidence |
|---|---|---|
| Menu | Tools: header "Audio logger (memory used: 20MB)", Enabled, Settings ▸ (Maximum length: 1, 2, 5, 10 minutes; Recording position: Pre effects, Post effects, pre fader), Dump to audio editor | [RUN] [STATIC] handlers 0x11c2450–0x11c26a0 |
| Defaults | Enabled, 1 minute, post effects | [RUN] menu, registry `EnableRollingAudioBuffer=1`, `RollingAudioBufferLengthSeconds=60`, `RollingAudioBufferPreFX=0` |
| Storage | in memory, never on disk until dumped; 8 bytes per stereo frame; freed when disabled | [DOC] menu_tools.htm, [STATIC] class at 0x1276000 |
| Memory figure | round(bytes / 2^20): 20 MB for 1 min at 44.1 kHz | [RUN] [STATIC] 0x11c2490 |
| Where it taps | Master only: pre before its effects; post after them, before the fader | [STATIC] 0x1276ee0, 0x1276e40 |
| Silence | a block is silent if all samples are at most 1e-6; writing stops after more than 8 s of silence, restarts on signal | [STATIC] 0x1276580 |
| Dump | Time-ordered copy, exact zeros trimmed at both ends; below 1e-6 → "Audio logger is empty"; else a WAV in Recorded, opened in Edison on the Master. The buffer is not cleared | [STATIC] 0x128f430, 0x1276320, [RUN] |
| File | `Audiolog-` + the system's date and time text; shown in Edison as "Audiolog-06-11-2026 220501", 44100 Hz, 32 | [RUN] (behaviour only: the audio's source in that shot is unknown) |
| Resize | copies in storage order, keeps the write position modulo the new size | [STATIC] 0x12761b0 |

The date comes from FL's locale date-and-time function (0x4441f0 with a FormatSettings record at PTR 0x15e3a70), not a
fixed format; the variable reads "R$" at its start, the system's currency string [RUN].

## Chord detection panel

| Behaviour | FL | Evidence |
|---|---|---|
| Placement | On the default toolbar, top row, left of the output monitor; greyed as "in use" in the toolbar palette; shows "---" | [RUN] |
| Hint | "Detected chord"; panel name "Chord detection" | [RUN] [STATIC] TToolbarForm |
| Reaction | C E G on the typing keyboard shows "C" | [RUN] |
| Range | 2 to 10 keys held, per MIDI channel, else "---" | [STATIC] 0xd328f0 |
| Timing | note-on at once; note-off after max(100 ms, delay); Piano Roll selection holds max(2000 ms, delay) | [STATIC] 0xd328f0, 0xd331b0, 0xd32da0 |
| Menu | "Display delay for played chords": None, 1 second, 3 seconds, 5 seconds, Show forever | [RUN] |
| Delay on the panel | With 3 seconds, "C" stays after release | [RUN] |
| Size | 96 x 40, box 89 x 33; font 18, smaller by 1.15 per character past 8 | [STATIC] 0xd32600 |
| Near hint bar | View ▸ Toolbars ▸ "Show detected MIDI chords near the hint bar", off by default. White outlined text with no box, just under the hint bar's left end, over the panels below. It leaves with "---". With delay None it was still up about 1 s into a held chord and gone 150 ms after release; with 3 s it was still up over 1 s after release | [RUN] FL session 22:59–23:01, registry `ShowDetectedMIDIChord=0`; [STATIC] 0xd32740: duration min(delay, 15000) ms. Timeout with a held chord and with Show forever not exercised at runtime |

### Roman has no effect in FL's panel

- `ChordDetectMenuPopup` (0x11f8630) makes "Chord style", "Standard" and "Roman" invisible each time, with no condition
  [STATIC]. The menu shows only the display delay [RUN].
- The flag (`UseRomanChordStyle`, detector +0xba) is still saved, but both detector callers pass key -1, so the naming
  function takes its letter branch [STATIC 0xd32c0b].
- With the flag set in memory, FL's detector still shows "G" for G B D [RUN]. The naming function itself returns "V"
  when it is given key C [RUN].

LYNX adds a working Chord style item (decision B through the coordinator). It is a LYNX addition, not FL parity. The
default is Standard, as FL behaves.

### Naming differential

FL's naming function (0xc43c20) was called on its own process with random key sets; LYNX's `nameKeys` got the same sets.
FL's nine joke names (seen on 7 to 10 keys in this sample, varying between names) count as one class, because LYNX words its own.

| Set | Key sets | Differences |
|---|---|---|
| Sharps | 11552 | 0 |
| Flats | 300 | 0 |
| Roman in C | 4000 | 0 |
| Roman in G | 4000 | 0 |

A sample of 80 measured names is in `tests/Logger181bTests.inc`. Measured at 22:01:57 and 22:03:28 on the same FL.

## Where LYNX differs

| LYNX | FL | Why |
|---|---|---|
| Changing the length keeps the newest audio | Storage-order copy | FL's copy can mix the order |
| While a dump or a new length has the ring, the audio waits in a 1 s spill and joins at the next block; the WAV is written off the UI thread | The audio thread waits on a critical section | No loss and no wait on the audio thread |
| Documents/LYNX/Audio logger | Audio/Recorded | LYNX's own folder |
| `Audiolog-YYYY-MM-DD HH-MM-SS.wav` | System date text | Same name on every system |
| Opens LYNX's audio editor | Edison on the Master | LYNX's editor |
| Joke name stable for the same keys, own wording | FL's wording; picks not traced | Clean-room, and no flicker |
| Keys polled at the UI timer rate | Note entry called per event | Audio thread stays free of UI work; very short blips can be missed |
| Post position always taps | Idle Master skips it | Same audio; the silence stop covers idle |
| Chord style item | Hidden | LYNX addition, see above |
| Note names follow LYNX's note-name setting | Letters | Existing LYNX option |

## Memory and audio-thread cost

Stereo float, 8 bytes per frame, allocated up front on the control side, freed when off:

| Length | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| 1 min | 20 MiB | 22 MiB (23.04 MB) | 44 MiB |
| 2 min | 40 MiB | 44 MiB | 88 MiB |
| 5 min | 101 MiB | 110 MiB | 220 MiB |
| 10 min | 202 MiB | 220 MiB | 440 MiB |

A dump doubles this for the moment of the copy. One second of spill is on top (0.35 MiB at 44.1 kHz, 0.73 MiB at 96 kHz).

`push` on a 512-frame block, measured with a plain loop on one core (3 runs, -O2, this machine):

| State | ns per block | Share of the block's time at 48 kHz |
|---|---|---|
| Off | 0.4–6 | under 0.0001 % |
| On, the other position | 0.5–5 | under 0.0001 % |
| On, signal | 1135–1253 | 0.011 % |
| On, past the 8 s silence stop (scan only) | 777–881 | 0.008 % |

## Audio during a dump or a length change

Stress test with numbered frames, 200 runs and 6000 dumps while lengths change (50 us per 64-frame block):

| Logger | Gaps in dumps | Torn frames |
|---|---|---|
| First version (dropped blocks during the pause) | 42597 | 0 |
| With the spill | 0 | 0 |

ThreadSanitizer: 20 runs, no reports. One core: 50 runs, no gaps.

## Best way?

- **FL:** the above: on by default, always rolling, dump on demand.
- **Ableton:** Capture MIDI only: Live keeps what was played and turns it into a clip. LYNX has it since round 170d. No audio equivalent verified.
- **Users:** one forum request found, asking that the Piano Roll audition (Alt+right-click) also shows the chord (AjwMusic, https://forum.image-line.com/viewtopic.php?p=2066377). No broad reviews found.
- **Verdict:** FL-exact defaults. Options for the coordinator, none switched on: show the chord on audition; dump straight to the Playlist.

# Part 2: Tools › Macros follow-up (round 181b)

Static read of FL 2026's `FLEngine_x64.dll` (Ghidra, read-only). Except where marked, nothing below was seen running in FL.

## Change declicking for selected channels to

- The action leads to FUN_011f7180 with the menu tag. Six items, tags 0 to 5: "Out only (no bleeding)", "Transient (no bleeding)", "Transient (bleeding)", "Generic (bleeding)", "Smooth (bleeding)", "Crossfade (bleeding)".
- No audio clip channel in the selection: "No selection" / "There are no audio clip channels selected.".
- Otherwise it asks "Change declicking mode?" / "Change the declicking mode of all selected audio clip channels? (no undo)", with OK and Cancel (flags 0x21). On OK it sets the mode of every selected audio clip channel. Other channel kinds are left alone.
- Seen in FL 2026 (session 2026-11-07): the submenu with these six captions, in this order (shot f3.png).
- LYNX: `setDeclickOfAudioClips` uses the same six modes (`SamplerSettings::declickMode` 0..5). The change can be undone in LYNX, so the text drops "(no undo)".

## Prepare for MIDI export

- FUN_0122d440 asks "Confirm" / "All of the channels are about to be turned into MIDI Out channels in order to allow output to a MIDI file. (no undo)". In some state the question is skipped (a bitmask on a byte of a global; not traced).
- It goes through every channel whose byte at +0x890 is 0. **Seen in FL 2026 (session 2026-11-07 11:12–11:14):** with a Layer, an Automation Clip and a Fruity Envelope Controller in the rack, the Layer and the Envelope Controller became MIDI Out (the controller on MIDI channel 7, after five generators and the Layer), and the Automation Clip stayed as it was and took no MIDI channel. So the flag marks automation clip channels (others not tested: Dashboard, Voltage Controller).
- For each such channel it notes whether the generator is "FPC", keeps the channel name, loads `MIDITrack.fst` and sets +0x7c to 0x60 (meaning not known).
- If the generator is now "MIDI out", it sets the MIDI channel: 9 for an FPC (MIDI channel 10). The others take a counter that starts at 0 and steps (n+1)&15, jumping from 9 to 10.
- LYNX: `prepareForMidiExport` uses the same numbering and keeps the names.
  - As in FL, every channel converts, Layer and controllers included. LYNX's automation clips are not channels, so there is nothing to skip.
  - It can be undone, so the text drops "(no undo)".

## Riff machine (Tools)

- FUN_0121d410 builds `<FL>\Data\System\Riff machine\` (FUN_00414da0). FUN_0081acf0 picks one .FST there: random, or by a fraction when the double argument is set; the argument DAT_0121d5b8 was not read.
- If none is found: "Error|Some required files are missing.".
- Otherwise it inserts a new channel (FUN_011f53d0 with mode 0, undo text "insert channel") and loads the preset (vtable +0x150). The installed `Default.fst` is a Sytrus state, and seven "Riff Machine - …" presets sit next to it.
- Then it calls FUN_00ff8480 (window message 0x4000 + channel id). When a state field (+0xb18) is 1, it also opens window 0xb. It was not checked whether the Piano Roll's Riff machine tool opens by itself.
- **LYNX gap:** the Tools entry opens LYNX's Riff machine on the selected channel's Piano Roll. It adds no channel and no preset.

## Chord progression tool

- FUN_011809b0 logs the analytics event "Chord Progression Tool opened" (FUN_00e12230) with source "Welcome window", and opens FL window 0xe.
- The window class is TChordProgression / TChordProgressionScript. FUN_00d2fd60 lists stamps (`.FSC`) from `Stamps\Melodic - chords\` and runs `System\Tools\Chord progression\cpt.pyscript` ("CPT comms script").
- The tool itself is Python: compiled modules for chords, notes, states (markov, sequencer), consonance, toolbox and the rack. FL warns when the window closes ("Warn when closing Chord Progression Tool"). The .pyc files were not read.
- **LYNX gap:** the Tools entry opens LYNX's chord progression generator on the Piano Roll, a much smaller tool. It is not FL's window.

## Other items read for the coordinator

- **Control creator** (FUN_00f1c5c0 → FUN_00ed6780(0, 0, flags)): entry 0 of the tool table at 0x015342a0 (`Control Creator\` + `ControlCreator.exe` under the folder with id 0x3ff). FL starts it with ShellExecuteW, so it is a separate program, not part of the DAW. Other entries: `Diagnostics\fldiagnostic.exe` (1), `License\CopyLicenseToAllUsers.exe` (1000), `CloudBounce\CloudBounceConsole.exe` (1001), `RemoveLicenses\RemoveLicenses.exe` (1002).
- **Save audio / MIDI clipboard to browser** (FUN_00f1cad0 / FUN_00f1cba0 → FUN_0124d070): if the clipboard holds the format (CF_WAVE = 12 for audio, the MIDI format otherwise), FL saves it as "Audio clipboard.wav" / "MIDI clipboard.mid" in Browser folder 0x3f2. FUN_009db5b0 maps that folder to the user data `Presets\Clipboard files` (info `System\Config\Browser\Infos\Clipboard files`).
  - It creates the folder or shows "Error|Unable to create folder:%s%s", then refreshes the Browser at that folder.
  - FUN_00819ca0 is assumed to make the name unique; it was not read.
  - LYNX (round 181b): saves into `<settings>/Presets/Clipboard files` (Browser root "Clipboard files"), audio from CF_WAVE on Windows and else LYNX's audio editor clipboard, MIDI from the MIDI clipboard; items greyed when empty, as in FL. A taken name gets " (2)", " (3)"... **INFERRED**: FL's naming in FUN_00819ca0 is not read.
- **Remix a song** (FUN_00f1bd40): posts window message 0x569. The receiver was not read.
- **Remove all Edison instances:** not added. LYNX's Edison is the audio editor window, not a mixer effect (no `EffectKind`; FLP import drops FL's Edison slots), so the item could do nothing. FL gap: Edison as a mixer effect.
- **Delete unused files and save project:** greyed in FL 2026's Tools menu in a project that was never saved (seen at runtime, session 2026-11-07 11:12). The enabling condition was not read in the binary. LYNX greys it the same way until the project has a file.

## Switch all / selected audio clip channels to

- FUN_01213a20(mode, scope). Scope 0 = the selected channels ("Selected" / "selected"), 1 = all ("All" / "found"), 2 = the channels of the Playlist's selected clips (not in the Tools menu). Strings at 0x015ca580 / 0x015ca598.
- Modes in FL 2026's submenu (seen at runtime, session 2026-11-07, f4.png): Realtime: Resample, Stretch, Stretch Pro; Elastique: e3 generic, e3 mono; Special: Slice stretch, Slice map; Legacy: Auto, e2 generic, e2 transient, e2 mono, e2 speech. Mode names in the message come from the table at 0x015ca530 (resample, e3 generic, e3 mono, slice stretch, slice map, auto, e2 generic, e2 transient, e2 mono…).
- No audio clip channel: "Error|No audio clip channels were %s." Otherwise "Confirm|%s audio clip channels will be set to %s mode. Some channels may need manual time adjustment." (OK / Cancel). On OK: one undo step "switch audio clips to <mode>", then for each audio clip channel: mode at +0x2f0; if its stretch time (+0x2e4) is at or under a small constant, FUN_00fed4f0(channel, 0); then the stretch is applied again.
- FUN_00fed4f0 is FL's tempo detection of the sample (its helpers FUN_00feb250 / FUN_00fec040 / FUN_00feced0 hold "Tempo detection|%s", "The tempo for this sample has been detected as |%s| BPM.", "This sample is too short for tempo detection."…). With 0 it does not offer to set the project tempo and applies the result (FUN_00feced0). **INFERRED, not seen:** whether FL shows its tempo detection dialog for each such channel during the macro.
- LYNX (round 181b, the user's decision): the two items in FL's place and with FL's texts, with LYNX's own modes inside: Realtime: Resample (stretch engine off); Stretch engine: General, Drums / transients, Vocal / mono; Warp (Ableton-style): Beats, Tones, Texture, Complex (no "Warp:" prefix under the header, as FL lists modes). One undo step. LYNX does not run a tempo detection for channels without a stretch length: it only sets the mode.
