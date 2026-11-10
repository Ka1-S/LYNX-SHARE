# Round 167q (mixpre167q): issue #25 (clip repeats) and FL's mixer track presets

Evidence tags: [FILE] FL's own files read byte by byte (demo projects, `Data\Patches\Mixer presets\*.fst`), [DFM] FL's
form resources (`TFXForm` in FL64.exe), [DECOMP] Ghidra on FL's own binaries only (FL64 / FLEngine_x64, project
`/mnt/e/lynx-work/r167g`), [HELP] FL's `WhatsNew.htm`, [EXEC] LYNX run on Windows with the real mouse, [TEST]
`tests/MixPre167qTests.inc`. No third-party plugin was decompiled. Clean-room: this document describes behaviour; the
code was written from it.

## B. Issue #25 (Disar, 0.1.21, Win 11, "Windows shared low latency"): "Region/clip repeats not played"

| reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| "create a clip, extend it beyond its length so it repeats, play" — the repeats are not played | [EXEC, round 168k] FL 2026 command-line render of a 1-bar pattern clip stretched to 4 bars (all items in ONE 0xE9 event): onsets only at 0.0 / 1.0 s, silence 2–8 s, the control clip at 10 / 11 s → **FL plays a stretched pattern clip once; the extended part is silent.** (My own FL render in this round was inconclusive: my test file wrote several 0xE9 events and FL keeps only the last. The demo projects with clips longer than their pattern, e.g. `Exoplanet.flp` pattern 14, do not prove repetition: they only show such clips exist.) | plays the pattern once, silence after (= FL); the Playlist draws repeats (round 168k removes that drawing, FL-exact) | unchanged = FL. This round first made the Scheduler repeat the pattern; that change was reverted before landing after 168k's evidence. [TEST] `mixpre167q_stretched_pattern_clip_plays_once` pins FL's behaviour (4-bar clip of a 1-bar pattern, a clip with an offset, block sizes 64…4096, with/without the index). |
| "LYNX closes (crash)" | — | Not reproduced. [EXEC] main (land-polish167p) and the 0.1.21 release, Windows 11, real mouse: pattern clip extended to 5 bars, Song mode, play; audio clip (DL Breaker) extended past its sample; "Windows Audio (shared, low-latency period)" at 480 samples; resizing while playing; Channel rack open — no crash (only a 2 s UI stall at the first play, session log). No crash report from the reporter in the crash inbox. `--render` of a project with long clips also fine. | No code cause found, so no crash fix to make. [TEST] `mixpre167q_repeats_stress`: 40 random songs (clips of 1 tick … 64 beats, offsets past the pattern, explicit pattern lengths shorter than the notes, slides, truncated notes on/off), random block sizes 16…2048, with and without the index: identical events, none dropped, all in range, no crash. The reply asks for the crash report and the audio device. |
| audio clips | an audio clip longer than its sample plays the sample once, then silence | same | unchanged (as FL) |

### B2. Reopened for 0.1.22: the repeats play, as a LYNX extra (the user's decision)

0.1.21 drew the repeats of a stretched pattern clip but played the pattern once (a YouTube reviewer praised the loop and
then noticed it did not sound). The user chose to keep LYNX looping, as a LYNX extra and not FL's behaviour:
**Options › General › "Stretched pattern clips loop their pattern (LYNX)"**, on by default, saved with the app settings
(`ui-options.txt`, `stretchedClipsLoop=`); off = FL (once, silence after; the 168k behaviour above).

- Core: one app-wide flag (`stretchedClipsLoop()` in `core/Song.h`). With it on, the Scheduler plays the pattern pass by
  pass along the clip (period = the pattern's `effectiveLength`; notes past that length are not played), the automation
  of pattern events and `ticksToNextEvent` follow the passes (RackRenderer), the MIDI export takes them, the Piano roll's
  playhead wraps (`patternTickInClip`), and the Playlist and the clip ghost notes draw the passes (`clipPasses`). Off,
  every one of them is the single pass of round 168k.
- Recording into a clip longer than its pattern keeps 168k's rule in both modes: the notes stay where they were played
  and the pattern grows (FL measured); the grown pattern is what the clip then loops.
  (Commit 281a06893 later folded the take into the pattern with the option on; issue #126 (0.1.26) showed it filled
  only the first bar of an empty clip, so it now folds only into existing content (the user's hybrid rule, REA_rodada185b.md), `RecNoFold126Tests.inc`.)
- [TEST] `mixpre167q_stretched_pattern_clip_loops_its_pattern` / `..._plays_once_with_the_option_off` (events, offsets,
  offset past the pattern, MIDI export, playhead, pattern length 2 bars / half a bar), `mixpre167q_repeats_stress` (80
  random songs, both modes), `polish_clip_ghost_notes` and `rec168k_stretched_pattern_clip_plays_its_pattern_once` (both
  modes). Audio, RackRenderer, 120 BPM / 48 kHz, the synth of a new channel, a 1-bar pattern in a 4-bar clip:
  `mixpre167q_stretched_clip_repeats_sound` — bar RMS 0.024670 ×4 (ratio 1.000000), max |bar k − bar 1| 0 to 4.99e-5
  (the events' sub-sample place against 512-sample blocks; 0 with 480), the same render as four 1-bar clips: max diff 0;
  option off: bars 2–4 RMS 0. `..._offsets_and_length_changes_sound`: offset 1 beat, offset past the pattern, pattern of
  2 bars, of half a bar, length changed while playing — each identical (max diff 0) to the same passes placed as plain
  clips. `..._through_jumps_and_loops`: start inside a repeat, a jump mid-clip, a loop region over the repeats: RMS
  ratio 1.000000, ≤ 5e-5 from bar 1, identical to plain clips.
- [EXEC] `LYNX.exe --cliplooptest report.txt export.wav` (Windows): the real AudioEngine device callback (a stand-in
  device, blocks of 512): bars 2–4 RMS 0.024670 = bar 1, max diff 4.99e-5; start inside a repeat 4.6e-5; seek mid-clip
  4.5e-5; loop region bars 2–3 played 3× 4.99e-5; option off: bars 2–4 silent; the WAV export read back: bars 1–4 RMS
  0.024670, option off 0 after bar 1. Result OK.
- [EXEC] Windows GUI, fresh profile: `cliploop-gui.lynx` (1-bar clip) opened, the clip's end dragged with the mouse to
  bar 5 (Playlist draws the 4 passes, shot `C:\Users\kauan\lynx-test\r167q\c3.png`), saved, the option switched off
  (the clip then shows one pass, `c9.png`; `ui-options.txt` gets `stretchedClipsLoop=0`); `--render` of the saved project
  (44.1 kHz, 24-bit): bars 1–4 RMS 0.024669 each, max |bar k − bar 1| ≤ 5.7e-5.
- Seen on the way (not changed here): in the real-time engine each wrap of a loop region plays one extra sample
  (`ceil` of a remaining length that is 192000.00000001 samples), and in RackRenderer a note on the exact target tick of a
  jump starts 1 sample late. Both exist with plain clips too.

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

- B: the repeats play again as a LYNX extra with FL's behaviour as an option (B2); the crash of #25 is not reproduced
  (no cause found in code; the reply asks for the crash report and the device).
- A: landed as described; open: "Tree display" for the States list, "Increase number of tracks to...", a mixer preset
  dropped on a Channel rack channel / Playlist track header, multiple presets dropped at once → consecutive tracks,
  saving as FL .fst, FL's undo names ("load mixer state" …: LYNX's undo step is the generic edit).
