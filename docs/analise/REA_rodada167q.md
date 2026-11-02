# Round 167q (mixpre167q): issue #25 (clip repeats) and FL's mixer track presets

Evidence tags: [FILE] FL's own files read byte by byte (demo projects, `Data\Patches\Mixer presets\*.fst`), [DFM] FL's
form resources (`TFXForm` in FL64.exe), [DECOMP] Ghidra on FL's own binaries only (FL64 / FLEngine_x64, project
`/mnt/e/lynx-work/r167g`), [HELP] FL's `WhatsNew.htm`, [EXEC] LYNX run on Windows with the real mouse, [TEST]
`tests/MixPre167qTests.inc`. No third-party plugin was decompiled. Clean-room: this document describes behaviour; the
code was written from it.

## B. Issue #25 (Disar, 0.1.21, Win 11, "Windows shared low latency"): "Region/clip repeats not played"

| reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| "create a clip, extend it beyond its length so it repeats, play" — the repeats are not played | [FILE] `Exoplanet.flp` (FL 12.9 demo): pattern 14 is 1 bar of 16 notes, placed as 4-bar (1536 ticks) and 8-bar clips back to back — FL repeats the pattern for the whole clip. `Astes - Raubana - LIFE.flp`: pieces of a split clip keep offsets 8448…9216 of a 384-tick pattern (the pass it falls in). Many demos have pattern clips longer than their pattern (scan of all demos). | The Playlist and the ghost notes DREW the repeats (`PlaylistComponent`), but `scheduleClip` played the notes of `[offset, offset+length)` once: everything after the pattern's end was silent | `Scheduler.cpp` `scheduleClip`: a pattern clip longer than its pattern (or with an offset past it) plays pass after pass, period = the pattern's length (`ScheduleIndex::patternLength` / `Pattern::effectiveLength`); notes at or after the period are not repeated; truncated notes are cut at the clip's end as before; the block windowing of the index is kept, so long clips cost nothing extra. The same wrap (`patternTickInClip`, `Song.h`) for the rack's event lanes (`RackRenderer`), the Piano roll playhead (`TransportEdit::patternPlayhead`, `EventEditor`), recording into a pattern from Song mode (`Recorder::patternPositionForTick`) and MIDI export (`ExportOptions::midiExportNotes`). |
| "LYNX closes (crash)" | — | Not reproduced. [EXEC] main (land-polish167p) and the 0.1.21 release, Windows 11, real mouse: pattern clip extended to 5 bars, Song mode, play; audio clip (DL Breaker) extended past its sample; "Windows Audio (shared, low-latency period)" at 480 samples; resizing while playing; Channel rack open — no crash, no hang beyond a 2 s UI stall at the first play (session log). No crash report from the reporter in the crash inbox. `--render` of a project with long clips also fine. | Stress test `mixpre167q_repeats_stress`: 40 random songs (clips of 1 tick … 64 beats, offsets past the pattern, explicit pattern lengths shorter than the notes, slides, truncated notes on/off), random block sizes 16…2048, with and without the index: identical events, none dropped, all in range. The reporter's crash remains unexplained; the reply asks for the crash report (LYNX writes one) and the audio device. |
| audio clips | FL: an audio clip longer than its sample plays the sample once and then silence (it does not loop unless the sample is looped). [EXEC] FL CLI render of a test file was inconclusive (my test file wrote several 0xE9 events; FL keeps only the last), so this column is from the demos + FL's UI, not a render. | plays the sample once, then silence | unchanged (as FL) |

[TEST] `mixpre167q_pattern_clip_repeats_its_pattern` (4-bar clip of a 1-bar pattern; a clip with an offset of 1 beat;
a clip within its pattern plays once; an offset past the pattern = the pass it falls in; MIDI export; Piano roll
playhead and recording position), `mixpre167q_repeats_stress`.

## A. Mixer track presets (the user: "I didn't find the feature to pull a preset straight on the mixer channel")

### FL (measured)

- [DFM] `TFXForm`, the mixer track menu: **File** submenu = `OpenMixStateMenu` "&Open mixer track state...",
  `SaveMixStateMenu` "&Save mixer track state as..." (Ctrl+Shift+S, hint "Save current mixer track as a preset"),
  `BrowsePresetsMenu` "Browse states", `MixStateSeparatorMenu` "-States" (the list is filled when the menu opens);
  `InsertMixerTrackFromStateMenuItem` "Insert from state". The "+" button (`AddTrackAfterSelectedBtn`, "Add a new track
  after the selected track") has `AddMixerTracksMenu`: Add one / Add multiple... / Increase number of tracks to... /
  "Add from state". There is NO separate "open presets" button on a mixer strip in FL's form: the presets are in the
  track menu › File (and in the Browser).
- [DECOMP] FUN_009db5b0: Browser folder 0x3f8 = "Mixer presets": the user's `Presets\Mixer presets` (Documents\
  Image-Line\FL Studio) + FL's `Data\Patches\Mixer presets\` (18 factory .fst: Big meter, Chorus send, Default,
  General purpose mastering 1–4 / air, Reverb send, tape simulations, …). FUN_0128e290 fills File › States from the
  *.FST of both roots (with a "Tree display" option); its handler FUN_012a0e60 → FUN_012a7440 loads the preset into
  ALL selected tracks (undo "load mixer state" / "… on multiple tracks"); the same list fills Insert from state (FUN_012a80a0:
  a new track at the current one, then the load, undo "insert mixer track from preset") and Add from state (FUN_012a87d0 /
  FUN_012a83b0, undo "add mixer track from preset"). FUN_012a73a0(track, path) → FUN_00f54760(path, 0x40, track): a
  preset is loaded with FL's generic project loader, restricted to one track. FUN_012a39c0 "Reset selected track(s) to
  default" loads `Default.fst` after warning "This will reset the selected mixer track(s) and remove all plugins" → a
  preset load replaces the whole effect chain.
- [HELP] WhatsNew: a mixer preset dropped on empty mixer space creates a new track; dropped on a channel → its mixer
  track; multiple presets → consecutive tracks; loaded on all selected tracks; loading no longer resets the recording
  location / input monitoring; the preset restores the track name.
- [FILE] the .fst format (FL project events, header format 0x40): Default (21.1): 0xEC, 0x62 × 10, 0xE1. Big meter
  (8.0.2): 0x95 colour, 0xCC name "", 0xEC, 0xC9 "Wave Candy", 0xD4 (insert 2), 0xD5, 0xE1. Reverb / Chorus / Flanger
  send (3.6.5): 0xC9, 0xD4 (insert 65, slot 1), 0xD5, 0x95, 0xCC "Reverb", 0xE1 — no 0xEC, name/colour after the slots.
  0xE1 control ids carry the SOURCE track (65, 2, 0 = Master for the mastering ones). Files before 8.0 (3.6.5, 4.5.2,
  5.3.3) keep the track's level/pan/separation/EQ block at 0x1F60 (0x1FC0 from 8.0) with level, pan and slot mix in
  0..128 (12800 later); 0x1F40… are the sends.

| reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| "pull a preset straight on the mixer channel" | track menu › File › States list of FL's + the user's mixer presets [DFM][DECOMP] | File › States listed only LYNX's own `.lynxtrack` states (Documents\LYNX\Mixer states) — empty for a new user | File › States lists FL's factory mixer presets, the user's FL ones and LYNX's states, sorted by name; loads onto every selected track (one undo step) |
| "the plugins you already saved" | a preset = the whole chain + the track's settings; Reset to default = Default.fst | LYNX states only | FL .fst presets read (`flp::parseMixerPreset`, `mixerTrackFromFlPreset`): effects converted as an .flp import does (FL natives LYNX has, plugins by the wrapper), level, pan, separation, EQ, phase/swap, name, colour; old pre-8.0 layout remapped |
| Open mixer track state... | opens .fst | `.lynxtrack` only | `.fst` and `.lynxtrack`; starts in FL's user mixer presets folder |
| Insert from state | same list | LYNX states | same list as States |
| Add from state ("+" menu) | `AddMixerTracksMenu` › Add from state | missing | added (new track at the end, as LYNX's "Add one") |
| Browser | folder "Mixer presets" (user + factory) | missing | roots "Mixer presets" (FL user folder, when FL is installed) and "Mixer presets (FL)"; double-click = onto the selected mixer tracks |
| drag a preset | onto a track = load there; onto empty mixer space = new track [HELP] | effects only | Browser drag of an .fst / .lynxtrack onto a strip loads it there; onto the empty space or "+" adds a track |
| keeps | routing, recording location, input monitoring [HELP] | — | `applyMixerTrackState` keeps sends, input, monitoring, arm/record path, mute/solo |
| name | "preset restores track name" [HELP] | LYNX states keep the target's name on load | FL presets with a name set it (and colour); presets without a name (Default, Big meter) keep the track's |

Not done here: FL's "Tree display" toggle for the States list; "Increase number of tracks to..."; dropping a mixer
preset on a Channel rack channel / Playlist track header; saving in FL's .fst format (Save still writes `.lynxtrack`,
LYNX's own); the per-effect-slot plugin preset list (round 167o's area).

[TEST] `mixpre167q_fl_mixer_preset_new_layout`, `mixpre167q_fl_mixer_preset_old_layout`,
`mixpre167q_fl_factory_mixer_presets` (every factory preset of the installed FL reads; runs only where FL is
installed).

[EXEC] Windows 11, real mouse, build of this branch (fresh LYNX_PROFILE_DIR, test mode): track menu › File › States
lists FL's 18 factory presets; "General purpose mastering" on Insert 1 → Parametric EQ + Multiband Compressor + Stereo
Enhancer, name kept; "+" › Add from state › Reverb send → new Insert 17 "Reverb" with a reverb; Browser "Mixer presets
(FL)" › Chorus send dragged onto Insert 3 → "Chorus" with a chorus; double-click Big meter → Wave Candy replaces the
chain of the selected Insert 3 (name kept: the preset has none). Shots `C:\Users\kauan\lynx-test\r167q\m1–m11.png`.

## Where I stopped

- B: fixed and tested; the crash of #25 is not reproduced (the reply asks for the crash report and the device).
- A: landed as described; open: "Tree display" for the States list, "Increase number of tracks to...", a mixer preset
  dropped on a Channel rack channel / Playlist track header, multiple presets dropped at once → consecutive tracks,
  saving as FL .fst, FL's undo names ("load mixer state" …: LYNX's undo step is the generic edit).
