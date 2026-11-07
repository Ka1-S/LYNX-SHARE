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
| Logging pauses only for the copy; the WAV is written off the UI thread | Paused during the whole dump | No freeze |
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

A dump doubles this for the moment of the copy.

`push` on a 512-frame block, measured with a plain loop on one core (3 runs, -O2, this machine):

| State | ns per block | Share of the block's time at 48 kHz |
|---|---|---|
| Off | 0.4–6 | under 0.0001 % |
| On, the other position | 0.5–5 | under 0.0001 % |
| On, signal | 1135–1253 | 0.011 % |
| On, past the 8 s silence stop (scan only) | 777–881 | 0.008 % |

## Best way?

- **FL:** the above: on by default, always rolling, dump on demand.
- **Ableton:** Capture MIDI only: Live keeps what was played and turns it into a clip. LYNX has it since round 170d. No audio equivalent verified.
- **Users:** one forum request found, asking that the Piano Roll audition (Alt+right-click) also shows the chord (AjwMusic, https://forum.image-line.com/viewtopic.php?p=2066377). No broad reviews found.
- **Verdict:** FL-exact defaults. Options for the coordinator, none switched on: show the chord on audition; dump straight to the Playlist.
