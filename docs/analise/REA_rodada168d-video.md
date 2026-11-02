# Round 168d — a YouTuber tests LYNX SHARE: every problem in the video, checked on current main

Video: **"NEW Open Source FL Studio!"** — channel **Busy Works Beats** (host "Game", busyworksbeats.com), uploaded
2026-11-01, 16:50 long, ~18 k views when fetched. Description: links to lynx-share.com, the GitHub repo and releases, and
his own plugin bundle. Captions: manual English (en-US). He found LYNX through another YouTuber and compares it all the
time with **FL Studio** and with **"Lemon Studio"** (another open-source FL-like project).

LYNX version: **macOS build (universal .dmg)**, window title "LYNX SHARE — drum mixing (imported from FL Studio)", menu
bar clock "Sat Oct 31 11:24–11:39 PM". The About box is never shown; 0.1.21-alpha (released 2026-10-31 11:56 UTC with a
macOS .dmg) is the only build that fits the date, so the video is **0.1.21 on macOS** (not the Windows build).

Overall impression (his words): *"well more fitted than Lemon Studio"*, *"they really went in on this one"*, *"the beat
sounds like a beat"*, *"this is the one"* if the community wants an open FL; **but** *"there's certain things that just
don't work"*: MIDI recording that stops at two bars is his main complaint (said 5 times, including the outro: *"give me
recording that actually works. Make it so that it doesn't stop at two bars … make Gross Beat work"*).

Liked (no action): opens an FL project, built-in mid/side + dynamic EQ, third-party plugins (his Cassette deck,
Illuminati, Bit Boy), the "What would you like to record? … Everything" dialog (*"exactly what we need"*), count-in,
notes visible while recording (*"I don't think FL Studio does that"*), FL's shortcuts (Ctrl+B), a stretched pattern clip
repeating the pattern, new instruments routed to a free mixer insert automatically, effects with their controls inside
the mixer, Ember Saturator, Hush De-esser, delays, the size of the effect list.

## Method

- Tools in user space: yt-dlp 2026.10.26 and a static ffmpeg 7.0.2 in `~/.local/bin`. Data on E: under
  `/mnt/e/lynx-work/video168d/` — `video/v.mp4` (720p), `video/v.en-US.vtt`, `video/transcript.txt`, `video/v.info.json`,
  `f5/tNNNN.png` (one frame every 5 s: `tNNNN` = second 5·NNNN), `f1/sNNNN.png` (one frame per second: `sNNNN` = second
  NNNN), `sheets/` (3×3 contact sheets of f5) and `crops/` (crops / sheets of the marked moments). Video content was
  treated as data only.
- Every moment where he complains, hesitates, searches or compares with FL was marked from the transcript, then read
  frame by frame (1/s) around it.
- Reproduction on **current origin/main (86a03295)**: the Windows build of the last landing (`land-issues167o`, = main),
  copied to `C:\Users\kauan\lynx-test\video168d\`, run 11 times with a fresh `LYNX_PROFILE_DIR` + `LYNX_TEST_MODE=1`
  under `~/.cache/lynx-wingui.lock`, driven by `drive.ps1` (real mouse / keyboard), closed by its own PID, desktop shot
  checked clean after each run. Shots: `C:\Users\kauan\lynx-test\video168d\s*.png`.
  Limitation: in these sessions the transport did not advance while playing (position stayed at 1:1:1–1:1:2; the
  desktop showed a remote-session banner, no audio device ran), so time-based recording could not be replayed in the
  GUI. The recording bugs were reproduced instead with a **core probe** that drives the real `NoteRecorder`,
  `commitTake`, `placePatternClipAt` and `followPatternLengths` exactly as `AudioEngine::updateRecording` does
  (`~/.claude/jobs/f8789b49/tmp/video168d/probe/probe.cpp`, built from main's `src/core/Song.cpp` + `Recorder.cpp`).
- `git diff v0.1.21-alpha origin/main` was checked for every file involved: `Recorder.cpp/.h`, `GrossBeat.*`, the
  channel Misc logic in `RackOps.cpp` and `PluginEditors.cpp` are unchanged since 0.1.21 for these behaviours.

## Moments

Legend — kind: **BUG**, **FL** (FL-parity gap), **UX** (works, but he could not find / understand it), **MAC** (macOS
specific), **OK** (no defect: user action or works as FL). Status = on current main.

| # | time | what he did | what happened / what he said | kind | evidence (E:/lynx-work/video168d/…) | status on main |
|---|---|---|---|---|---|---|
| 1 | 0:35–1:14 | opens his FL project "drum mixing", inserts his Cassette deck | works; *"it can even load up third-party plug-ins"* | OK | f5/t0007–t0014 | — |
| 2 | 2:01–2:19 | Record → dialog → **Everything**; looks for the metronome, turns count-in on | finds both; *"Finally, they have the word everything"* | OK | f1/s0125 (dialog), f1/s0140 | same dialog on main (s2-a.png) |
| 3 | 2:41–2:54 | drags the right edge of the recorded Pattern 1 clip from 4 to 8 bars | the clip repeats the pattern; *"it just keeps on looping … a feature that FL Studio does not have"* | OK (LYNX-own behaviour he liked) | f1/s0162, s0172, crops/k176.png | — |
| 4 | 2:58–3:03 | tries to make a time selection on the ruler (double-click and drag) | *"the double click to highlight this bar is … hard to do … I would recommend to change as soon as you guys can"*. The status bar hint says `Ctrl+drag or ×2 drag: time selection` | **MAC** + UX | crops/k176.png, crops/hint176.png (hint text) | **still broken on macOS (code)**: `PlaylistComponent::mouseDown` checks `e.mods.isPopupMenu()` first; on macOS JUCE reports **Ctrl+click as a popup click**, so Ctrl+drag on the ruler opens the ruler menu instead of a time selection, leaving only the double-click-drag he found hard. Same pattern for every `isCtrlDown()` gesture (Ctrl+drag select in the Playlist, Piano Roll…). Not reproducible on Windows; needs a macOS check |
| 5 | 3:09–3:34 | Ctrl+C, then Ctrl+B with a loop selection, then without | *"selecting stuff is a little weird"*, then *"That worked … the same shortcuts so far as FL Studio"* | OK / UX (selection) | f1/s0189–s0214 | — |
| 6 | 3:39–3:52 | plays the beat | *"I'm not hearing — Okay, I turned the pattern off, apparently"*; *"I'm missing a note. I had the note stop"* | OK (track muted by himself) + see #8 (missing note) | crops/k222.png (Track 3 dimmed) | — |
| 7 | 4:02–4:13 | opens the Piano Roll of Pattern 1 | *"it says two bars here, but visually it's showing four bars"* — pattern 2 bars, its clip stretched to 8 bars (#3) | **BUG** (part of #8) | crops/k234.png (piano roll shows bars 1–2 only) | still broken (see #8b) |
| 8 | 4:25 / 6:55–7:42 / 11:04 / 16:20 | records MIDI over 4 and then ~8 bars in Song mode (count-in on, empty new pattern, no loop region) | the new pattern clip ends at **2 bars**; *"I recorded near eight bars, but it's stopping at two bars … it just maxes out at two bars, and you can't record more than two bars"*; *"I'm pretty sure I recorded four bars, but I guess it didn't record"* | **BUG (critical)** | crops/k376.png (8:1:3 on the counter, Pattern 2 clip 1–2 bars long), crops/k420.png (the take's clip ends at bar 3), f1/s0688.png (Pattern 4: 2 bars) | **still broken on main** — reproduced with the core probe: **(a)** empty pattern, Song mode, no clip of it under the playhead: `NoteRecorder::begin` freezes the loop length at the pattern's length at the take's start (1 bar for an empty pattern) and `patternPositionForTick` folds every note outside the pattern's clip `% loopLength` → 32 notes played over 8 bars all land in **bar 1** (`probe 8`: "pattern 2: 32 notes, length 1.00 bars"); a note held over the first bar line makes the pattern 2 bars, hence his "two bars". **(b)** a fixed 2-bar pattern whose clip was stretched to 8 bars (#3/#7): positions inside the clip are not folded by the pattern length → notes stored at bars 3–8 of a 2-bar pattern, invisible and silent (`probe 8 B`). FL evidence: REA_rodada165z-b.md (fh2–fh5, fa10–fa20): FL records Song-mode input into the pattern and places/extends the pattern clip from the first event's bar to the pattern's whole bars at Stop — no folding to the first bar |
| 9 | 4:13–4:25 | Piano Roll + Channel Rack windows cover the Playlist | *"How do we get back to the playlist? Where is the playlist? Holy crap … these buttons are kind of like weird"* | **UX** | crops/k234.png (6th tile: Playlist hint shown while hovering the toolbar) | unchanged: the view buttons are icon-only; F5 works. Ideas: label/tooltip "Playlist (F5)" visible on first run, or a View › Playlist entry in the window menu |
| 10 | 4:42–4:52 | looks at the Channel Rack of the imported project | *"the channel rack stuff being up here … it's hard to tell what is what"*: 7 of 13 rows are FL audio-clip channels (`Adieu – Cascade Dru…`, `drum mixing – _4` "audio clip — plays from the Playlist") | **UX** | crops/k234.png | unchanged. FL shows the same channels; a rack filter preset "hide audio clip channels" (FL's "Group: Audio clips" filter) would help |
| 11 | 5:33–5:58 | 3x Osc (imported FL channel) → Misc → clicks **Mono** (and Porta) many times | the boxes stay off: *"this monophonic is not turning on. So I really can't play legato stuff … this is not working"* | **BUG / FL** | f1/s0338, s0350, crops/mono.png (cursor on the dim Mono box, never ticks) | **still broken on main, reproduced on Windows**: add channel › FL engines › 3x Osc › Misc, click Mono and Porta → nothing (s11-a.png / s11-crop.png). Same for the built-in Kick/Hat channels (s9-crop.png) and Toxic Biohazard (crops/k515.png). Cause: `channelHasMono()` / `channelHasPortamento()` (RackOps.cpp) only accept `InstrumentKind::synth` (+ sampler for Porta); the Misc tab disables the boxes for every other kind. FL: Misc › Polyphony Max / Slide / Porta / Mono exist for every generator channel (REA_rodada143 B2, [EXEC misc.png]) |
| 12 | 6:00 | looks for how to change patterns | *"How do you change patterns? Okay, patterns."* — found the toolbar pattern picker | UX (minor) | f1/s0360 | — |
| 13 | 8:30–8:51 | FL engines › Toxic Biohazard, Plugin tab | *"Definitely not Toxic Biohazard. It's missing the whole FM grid … I wouldn't rely on built-in plugins right now"* | **FL (UI)** | crops/k515.png (Toxic opens on a generic page; Mono/Porta dim) | unchanged: no Toxic Biohazard layout in `PluginLayout.cpp` (3x Osc, FL Keys, Sytrus, Harmor… have one). Also on macOS the engine's wave spectra need the user's `Toxic Biohazard_x64.dll` (core/Toxic.h), which does not exist on a Mac → only the sine / drawn waves can sound, which matches *"sounds like a saw"* style comments |
| 14 | 9:10–9:22 | FL engines › PoiZone | *"this doesn't look like Poizone. I don't even know what you can do in here"* | **FL (UI)** | crops/k515.png (5th tile: 16 plain knobs, "PoiZone — FL's engine …") | unchanged: generic knob page, no PoiZone layout |
| 15 | 9:32–10:16 | loads his Bit Boy (VST), tries to make a loop on the ruler | *"Kind of weird trying to create a loop in here"* | UX / MAC (same as #4) | crops/k598.png (ruler hint, no selection appears) | see #4 |
| 16 | 10:49–11:30 | records a melody with the pitch wheel | the bend goes to a separate automation clip ("BWB BitBoy – MIDI Channel 1 Pitch") on its own track, the notes to a 2-bar Pattern 4; *"Like it just doesn't play my notes right. Like what is going on? … clearly there's something missing with the MIDI and recording process"* | **BUG** (#8 folding stacks the later bars' notes on bars 1–2) + FL difference (bend storage) | crops/k640.png, crops/k682.png, f1/s0688.png | notes: still broken (#8). Bend as an automation clip in Song mode: known, documented difference from FL (REA_rodada165z-b.md: FL writes events into the pattern), left to the user's decision in 0.1.20 |
| 17 | 12:00–12:16 | adds an instrument | routed to a free insert automatically: *"Why is that not the default state of FL Studio"* | OK | f5/t0145 | — |
| 18 | 13:30–13:44 | Delay on the mixer | *"delay seems to work. It's hard to tell"* | OK | sheets/s019.png | — |
| 19 | 14:40–14:49 | finds Gross Beat | *"one thing I would like as a beginner is presets cuz there's no way I'm drawing every single Gross Beat"* | **FL** | f1/s0905.png (preset box "(no preset)", 36+36 empty slots) | still missing on main: a new Gross Beat has no slot content; FL presets are only listed when an FL install provides them (`addFlPresets`), which a Mac has not. FL ships Gross Beat with factory slots filled (time and volume presets) |
| 20 | 15:02–15:13 | clicks and drags in the Gross Beat envelope area, changes slots | *"it doesn't do anything. I'm clicking in here and it's not doing anything. Why would you make a Gross Beat that doesn't do anything? Why would you trick us like that?"* | **BUG / FL** | crops/k880.png (13 tiles, envelope stays flat, cursor moving over it) | **still broken on main (code)**: `grossbeat::make ({})` — without an FL state the effect passes the signal (Effects.cpp: "without a state: passes the signal"); `paintGrossBeat` only draws the slot of an imported FL state and the editor has no mouse handling for the `grossbeat` area (no point add / drag); every slot of a new instance is flat |
| 21 | 15:36–15:52 | Hush De-esser | *"They even have a de-esser … I'm cracking up"* | OK | sheets/s021.png | — |

## Confirmed bugs (prioritised)

1. **MIDI recording folds into the first bar(s)** (#7, #8, #16) — critical, the video's headline. Two code paths:
   (a) no clip of the pattern under the playhead → `% loopLength` frozen at the take's start; (b) a pattern clip longer
   than the pattern → notes past the pattern's length are stored but never shown or played. Reproduced by the probe on
   main.
2. **Misc › Mono / Porta dead on every channel but LYNX Synth / Sampler** (#11) — reproduced on Windows main
   (3x Osc FL engine, built-in drums, Toxic). FL has them on every generator.
3. **Gross Beat does nothing on a new instance** (#19, #20) — no editable envelopes, no factory slots, no presets
   without FL installed.
4. **macOS: Ctrl+click is a right click** (#4, #15) — every Ctrl gesture (ruler time selection, Playlist / Piano Roll
   Ctrl+drag select) becomes a popup menu on a Mac; must map FL's Ctrl to Cmd on macOS (as FL for Mac does) or accept
   both. Code-confirmed, needs a Mac run.
5. **FL engines without their FL UI** (#13, #14) — Toxic Biohazard (FM matrix) and PoiZone show generic knob pages; on a
   Mac, engines that read the user's FL DLLs (Toxic waves, BooBass, FL Slayer…) cannot be complete — the picker should
   say so (BooBass / FL Slayer already say "requires FL Studio"; Toxic / Kepler / Transistor Bass do not).
6. UX: finding the Playlist again (#9), a rack crowded by FL audio-clip channels (#10), selection feel (#5).

Already fixed in 0.1.22 work: none of the items above (the recorder, Misc and Gross Beat code is unchanged since
v0.1.21-alpha; 167a's channel-pitch work on ThreeOsc.cpp does not touch Mono/Porta).

## Proposed fix rounds (independent files)

| round | scope | files | FL evidence needed |
|---|---|---|---|
| **R-rec** (first) | Song-mode MIDI recording: notes outside any clip of the recorded pattern are placed at their real song position (the pattern grows, its clip is placed from the take's first bar and grows to the last recorded bar, as FL); inside a clip longer than the pattern, fold by the pattern length (the clip repeats it) or grow the pattern, whichever FL does; held notes at Stop kept; pitch-bend lane aligned with the notes. Tests from the probe's two cases (8 bars → 8 bars). Then the user's exact flow on Windows with audio (count-in, 8 bars, new pattern) | `src/core/Recorder.cpp/.h`, `src/engine/AudioEngine.cpp` (`updateRecording` take placement only), `tests/` | one FL session: record 8 bars into an empty pattern in Song mode (no clip / a 2-bar pattern with a stretched clip) — what FL stores, where the clip goes |
| **R-misc** | Channel Misc › Mono / Porta / Slide for every generator kind (FL engines, built-in drums, sampler-based, hosted plugins: LYNX applies mono/porta in the voice allocator before the engine, as FL's channel layer does); keep LYNX Synth's own flags in sync | `src/core/RackOps.cpp`, the per-engine voice entry (`RackRenderer.cpp` / `Instruments.cpp`), `src/ui/ChannelSettings.cpp`, `src/core/FlpImport.cpp` (FL channel mono flag), tests | REA FL64 channel polyphony (REA_rodada143 B2 + FLEngine voice code); FL: 3x Osc with Mono on, legato playing |
| **R-gross** | Gross Beat usable without FL: point editing in the time / volume envelopes (add, drag, delete, tension), per-slot sync / hold, a set of LYNX-made factory slots (clean-room: shapes described from FL's behaviour, not copied), preset save/load, state written back into the LXGB container | `src/core/GrossBeat.cpp/.h`, `src/ui/PluginEditors.cpp` (grossbeat area), `src/core/PluginLayout.cpp`, `resources/` presets, tests | REA_rodada53 (engine) + FL's Gross Beat editor gestures (black-box FL session / REA of the FL plugin) |
| **R-mac** | macOS modifier mapping: FL-for-Mac uses Cmd where Windows uses Ctrl; make LYNX gestures accept Cmd on macOS and never treat Ctrl+left as a popup where a Ctrl gesture exists; hint texts show ⌘ on Mac | `src/ui/PlaylistComponent.cpp`, `PianoRoll*.cpp`, `EditorGestures*`, hint strings (15 languages) | FL for Mac manual / FL Mac build behaviour (no Mac here: document, test on the CI macOS runner or ask the user) |
| **R-flui** | FL-like layouts for Toxic Biohazard (operator matrix) and PoiZone; picker note "(needs FL Studio files)" for engines that read FL DLLs when they are absent (macOS/Linux) | `src/core/PluginLayout.cpp`, `src/ui/PluginSkin.cpp`, picker menu in `ChannelRack*.cpp` | FL screenshots of both plugins (one FL session) |
| **R-ux** | "Where is the Playlist": first-run hint / labelled view buttons; rack filter to hide audio-clip channels; check double-click-drag tolerance on the ruler | `src/ui/MainComponent.cpp`, `ChannelRack*.cpp`, `PlaylistComponent.cpp` | FL's rack "Audio clips" group filter |

Order: R-rec and R-misc first (the two things he showed as "doesn't work"), then R-gross, then R-mac / R-flui / R-ux.

## Where I stopped

- Done: video, captions, metadata, frames (E:), moment table, Windows reproduction of #2 and #11 on main, core probe of
  #8 (both cases), code check of #4, #19, #20, #13, #14. Doc landed docs-only.
- Not done (by design, no product changes in this round): every fix above.
- Rerun: `~/.claude/jobs/f8789b49/tmp/video168d/probe/probe 8` and `probe 8 B` (build line in this doc's Method);
  GUI: `flock ~/.cache/lynx-wingui.lock ~/.claude/jobs/f8789b49/tmp/video168d/session.sh NAME "cmds"` (drive.ps1
  commands: focus, sleep:ms, click:x:y, key:VK:ctrl, hold:VK:ms, shot:path).
