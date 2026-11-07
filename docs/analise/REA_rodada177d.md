# Round 177d — recording UX and small FL pain points

User-approved area, 2026-11-06 (brief: feat177/brief.md). Inputs: `/mnt/e/lynx-work/ableton/research-web-2.md` (§1 #7, §2.4/2.7, §3)
and `STUDY.md`. FL's workflow stays the base; changes that alter FL behaviour come as options.

## 1. Recording UX (slice 177d-1: live display)

### Is this the best way?
- **FL Studio 2026.** MusicTech's 2025 review (8/10) calls audio recording "unintuitive" and says recorded MIDI is not shown
  in the Playlist while recording; clips also land in odd places ([MusicTech](https://musictech.com/reviews/digital-audio-workstations/fl-studio-2025-review/)).
  Gearspace users record vocals in another DAW ([Gearspace](https://gearspace.com/board/rap-hip-hop-engineering-and-production/629714-fl-studio-frustrating-shit-out-me.html)).
  FL black-box evidence: §1.3. FL draws a held note live in the Piano roll, but during a Song-mode note take its Playlist
  shows nothing until Stop. An audio take shows only as a plain grey block.
- **Ableton Live 12.** A recording draws as a growing red clip in the Arrangement, with notes and the waveform as they arrive
  (Live manual, "Recording New Clips": the clip appears while recording). Live's count-in shows on the transport. Users praise it
  as "what you see is what you get". Complaints are about take lanes (Live 11 comping overwrites, missing crossfades, SOS / Audio Technology),
  which is round 177c's area, not this one.
- **Others.** Logic, Cubase, Studio One, Reaper and Bitwig all draw MIDI and audio recordings live. This is the norm outside FL.
- **Verdict** (coordinator and user, 2026-11-06). The Piano roll matches FL: a held note grows in the normal note colour.
  The Playlist goes beyond FL. A note take shows a red frame (the record button's colour) plus the held notes. An audio take
  shows a red growing clip with a header and a live waveform. At Stop the clips take their normal colours, as in FL.
  The user asked to "see the MIDI notes appear while recording". FL's grey block carries no information.
- Live display is an *addition*: it shows what the take will become at the stop, without changing where anything
  lands, when it is committed, or the undo step. So there is no FL behaviour to keep as an option, and it is on by default.

### 1.1 What LYNX did before
- Notes: `AudioEngine::updateRecording` committed a take's notes **on release** (`NoteRecorder::takeFinished`, one undo step per
  take). So a held note was invisible until its key went up. In Song mode the take's clip was placed with the first *released* note,
  so the Playlist showed nothing at all while the first notes were held.
- Audio: nothing showed until playback stopped (`finishAudioTake` → `takes::placeTakeClips`). The only feedback was the REC button.
- Count-in (round 170d: catch window, notes during count-in kept), punch in/out and pre/post-roll (round 163), loop record
  (round 170e3/e5) and the record menu (round 139) were already there.

### 1.2 What 177d-1 adds
- `NoteRecorder::preview (song, now)` (const): the notes still held, as they would be committed if released now (the same
  placement code as `finish`, now shared through `makeNote`), with the take's Song-mode origin and reach.
- `liverec::LivePeaks` (`core/LiveRecordView.h`): the waveform outline of an audio take (max |x| per 256 frames). Written by
  the **recording thread** while it drains the take to disk (`Take::drain`); the audio thread does nothing new.
- `liverec::audioSpan`: where the pass being recorded shows: the same mapping as `takes::place` / `takes::loopPasses` (latency
  compensated; in a loop pass, from the loop start).
- `AudioEngine::liveRecording()` (UI thread): held notes, the Song-mode note range and its Playlist track (the take's clip,
  or `patternClipTrack`, the rule `placePatternClipAt` uses, now shared), every open audio take with its track (the
  `takes::takeTrack` rule placeTakeClips uses), span and peaks, and the punch range.
- Piano roll: held notes drawn as ordinary notes (FL), growing with the playhead; only their rows are repainted.
- Playlist: each audio take is a growing clip (accent colour, the take's file name) with its live waveform, cut to the punch
  range; a Song-mode note take is a red frame over the range it covers, with the held notes at their pitch. Repainted at 30 Hz,
  only over the take's rows, only while a take records.
- Cost when not recording: one `bool` check per frame (`isLiveRecording`). No timers, no allocation.

### 1.3 FL black-box check (FL Studio 2026, 2026-11-06 14:39–14:43)

**Setup.** One FL session, holding the wingui and fl locks. Default template (808 Kick, 130 BPM), started once and closed
by its PID. On close: Save changes → No, and FL's "delete the unused recordings" prompt → Delete. Nothing was saved.
The registry diff shows only FL's run counter and the cloud-check timestamp. Notes came from FL's typing keyboard
(`keybd_event` with scan codes). Audio came from "FL Studio ASIO" In 1–In 2 on an armed insert. Shots are in
`E:\lynx-work\rec177d\fl\`.

| # | what | FL shows | shots |
|---|---|---|---|
| fl1 | Pattern mode, record "Notes and automation", C5 held about 1.6 s | The held note is drawn **while held**, from its start to the playhead, in the normal note colour with its name. On release it stays as the recorded note | g02, g03 (held), g04 (released) |
| fl2 | Song mode, empty Playlist, D5 held about 1.8 s from bar 1 | **Nothing** on the Playlist during the take, even after the release (h03 and h04 at 2:11 and 3:09). Only after Stop does "Pattern 1" appear on Track 1 from bar 1. The Piano roll (docked) shows the note | h01–h04, h05 (after stop) |
| fl3 | Song mode, "Audio, into the playlist as an audio clip", FL asks for the input (menu: In 1 – In 2 / In 1 / In 2 / none), and the next free track becomes "REC" (armed) | During the take, a **plain grey block** grows on the next free track, with no header, name or waveform. After Stop it becomes the clip "untitled_<date>_REC" with its waveform | j01, j02 (recording), j03 (after stop) |
| fl4 | Record button with no filter set | FL's "What would you like to record?" dialog: Edison / audio into the playlist / notes and automation / everything / help, plus "Don't show this in the future" | f02 |

Also seen: when FL closes an unsaved project it offers to delete the takes that project no longer uses. LYNX's
`sessionRecordings` / `forgetRecordings` cover the same case (to be checked in a later slice, not changed here).

## 2. The 39 FL pain points (research-web-2.md §3)

**Status key.** ALREADY = LYNX already fixes it (evidence given). PARTLY = some of it exists. HAS = LYNX has the same problem.
N/A = does not apply. Other rounds own: 1, 4 → 177a; 2, 9, 28, 29 → 177b; 33 (and the tempo side of 34) → 175a.

| # | item | owner | LYNX status | evidence | plan |
|---|---|---|---|---|---|
| 1 | routing by stable ID | 177a | — | — | — |
| 2 | sidechain picker | 177b | — | — | — |
| 3 | mixer presets include volume/pan | 177d | ALREADY | track state = effects, level, pan, stereo, EQ, name, colour (`ProjectFile.h:28-30`, `applyMixerTrackState`) | check that an FL `.fst` with no volume leaves the fader alone (batch 7) |
| 4 | mixer groups / folders | 177a | — | — | — |
| 5 | mixer wheel scroll, slot drag, no fader jump | 177d | ALREADY | `MixerStrips::mouseWheelMove`; `dragSlot` → `mixerops::moveEffectToRow`; a cap press (±9 px) never jumps | regression tests (batch 7) |
| 6 | gain-reduction meter, channel-EQ bypass | 177d | PARTLY | EQ bypass is project-wide only ("Clean mixer"); `TrackEq` has no bypass; no GR meter | 6a per-track EQ bypass (batch 7); 6b GR meter → **user decision** |
| 7 | load bridged/isolated per plugin | 177d | ALREADY (setting), PARTLY (place) | `PluginRef::separateProcess`, `pluginAlwaysSeparate` (PluginSandbox.h) in the plugin window menu | "Separate process" column in the Plugin Manager (batch 3) |
| 8 | true unload, keeping settings | 177d | PARTLY | smart disable, freeze; the instance stays loaded | "Unload (keep settings)" (batch 9) |
| 9 | sidechain on dynamics | 177b | — | — | — |
| 10 | value readout at the cursor on hover | 177d | PARTLY | value tip shows only while dragging or wheeling (`ValueControl` `showValueTip`) | option, default off = FL (batch 2) |
| 11 | plugin window "pin on top" | 177d | HAS | no `setAlwaysOnTop` in PluginWindow | "Keep on top" in the plugin window menu (batch 3) |
| 12 | per-plugin velocity curve / fixed velocity | 177d | PARTLY | global `prefs::VelocityCurve` only | per-channel override (batch 3) |
| 13 | audition on load, preset management | 177d | PARTLY | Ctrl+click audition, preset ◀▶, save/load presets; nothing plays on load | option "play a test note on load", default off (batch 3) |
| 14 | FPC per-pad sends / sample selector | 177d | N/A by design | FPC imports split into one sampler channel per pad, each with its own insert (`splitDrumChannel`) | — |
| 15 | pause autosave while playing / recording / MIDI input | 177d | PARTLY | postponed only while recording (`MainComponent.cpp` backup timer); plugin states are captured on the UI thread | also postpone while playing and right after MIDI input → **user decision** on the default (batch 1) |
| 16 | real undo labels, plugin-parameter undo | 177d | PARTLY | labelled History panel (`core/History.cpp`), one step per press; tweaks in a plugin's own GUI make no undo step; labels name the channel or insert, not the parameter | batch 1 |
| 17 | redo on Ctrl/Cmd+Shift+Z | 177d | ALREADY | `KeyMap.cpp` `edit.redo` = Ctrl+Y and Ctrl+Shift+Z; FL's Ctrl+Alt+Z kept | — |
| 18 | recorded MIDI live in the Playlist, clip at the record position | 177d | FIXED in 177d-1 | §1 | — |
| 19 | resize notes from the left edge | 177d | ALREADY | `Drag::resizeLeft`, option "Resize notes from the left edge too" | — |
| 20 | fine / ultra-fine knob modes | 177d | PARTLY | Ctrl = fine 10× (`controls::fineFactor`) | Ctrl+Shift ultra-fine plus a setting (batch 2) |
| 21 | global transpose that leaves the notes alone | 177d | PARTLY | master pitch ±1200 cents, but on plugins it is pitch bend clipped to their bend range (as in FL) | option "whole semitones as key shift for plugins", default off (batch 4) |
| 22 | solo the active channel from any editor | 177d | HAS | only rack Ctrl+1..0 and mixer S | global action `channel.soloActive` plus a Piano roll menu item (batch 4) |
| 23 | closing the Mixer returns to the last editor | 177d | ALREADY | `Layout::hide` focuses the most recent panel (Workspace.cpp), tested | — |
| 24 | volume envelopes on audio clips | 177d | PARTLY | clip gain, fades, crossfades; no breakpoints | **user decision** (L, new feature) |
| 25 | Piano roll bar numbers synced to the Playlist | 177d | HAS | the ruler shows pattern-local bars | option "Ruler: song bars", default off (batch 6) |
| 26 | ghost notes selectable / copyable | 177d | PARTLY | other channels' ghosts are editable (Ctrl+Alt+V); clip ghosts are drawn only | "Copy ghost notes" (batch 6) |
| 27 | Event Editor | 177d / 177b | PARTLY | draw, line, select, scale, offset, randomise, LFO; time snap only | copy keeping timing, value snap, curve tension (batch 8, with 177b) |
| 28 | one-step "create automation" | 177b | — | — | — |
| 29 | link many targets to one controller | 177b | — | — | — |
| 30 | one-click multitrack MIDI export | 177d | ALREADY | File › Export › MIDI file (Ctrl+Shift+M), one track per channel (`MidiFileIo::saveSong`) | — |
| 31 | pattern cleanup | 177d | PARTLY | make unique, purge unused audio clips, split by channel; no merge, purge or sort for patterns | 31a: merge identical, delete unused, sort (batch 5). 31b, the modifier that makes a clip unique when you edit it: **declined by the user** (2026-11-06), not built |
| 32 | nested groups | 177d | HAS | Playlist and rack groups are one level | **user decision** (L, model change) |
| 33 | tempo map | 175a | — | — | — |
| 34 | consistent BPM detection | 175a / 177d | ALREADY (177d part) | one detector (`core/TempoDetect.cpp`) used by Playlist and Channel settings | — |
| 35 | stems "instruments only" | 177d | PARTLY | per-channel stems keep the mixer, so sends and bus FX are in them | export option (batch 5) |
| 36 | package keeps clip settings, reports missing | 177d | ALREADY | `ProjectPackage::exportZip` keeps the full song; "Not on this PC (left out)" list | — |
| 37 | save for an older version / downgrade warning | 177d | PARTLY | FLP export reports what it leaves out; the .lynx format is at version 1, so an older build silently drops unknown fields | write `savedBy` plus a hint on load (batch 1) |
| 38 | higher PPQ without a CPU cost | 177d | ALREADY | fixed 960 PPQ | — |
| 39 | AI assistant opt-in | 177d | N/A | no assistant code | — |

### Batches (177d-2 on), most value per effort first
1. **Save / undo:** 16 (plugin-GUI tweaks as undo steps, parameter names in labels), 37 (`savedBy` and a downgrade hint), 15 (once the user decides).
2. **Knobs / values:** 20 (ultra-fine), 10 (hover readout, an option).
3. **Plugin wrapper:** 11 (keep on top), 12 (per-channel velocity), 13 (test note on load, an option), 7 (Plugin Manager column).
4. **Transport / pitch:** 22 (solo active channel), 21 (semitone key shift, an option).
5. **Patterns / export:** 31a (merge, delete unused, sort), 35 (stems that skip buses and sends).
6. **Piano roll:** 25 (song-bar ruler, an option), 26 (copy ghost notes).
7. **Mixer strip:** 6a (EQ bypass), 5 (tests), 3 (`.fst` volume check).
8. **Event editor:** 27 (with 177b).
9. **Unload plugin:** 8.

User decisions (2026-11-06, through the coordinator):
- **24, clip volume envelopes:** yes, as its own slice. It must cost nothing on clips that have no envelope. The UI gate needs FL | LYNX | Ableton shots. For .flp export, pick whichever loses nothing.
- **6b, gain-reduction meter:** yes, for LYNX's own dynamics only. It shows only on strips that have one, small, and costs nothing when hidden.
- **15, backups:** yes. They also wait while playing and right after MIDI input, behind a Settings option that is on by default.
- **32, nested groups:** declined for now.
- **31b, auto-unique clips on edit:** declined. Nothing of it is built. 31a, pattern cleanup, is a separate item and goes ahead.

## 3. Slice 177d-2: quick fixes (22, 20, 15) and pattern hygiene (31a)

### 22. Solo the selected channel from any window
- **FL:** channel solo is only Ctrl+click on the channel's mute LED in the Channel rack, or the rack's numbered keys while the rack has focus. FL has no global action for it.
- **LYNX before:** the same as FL (`rack.solo1..10`, Context::channelRack).
- **Change:** a new global action `channel.soloSelected` ("Solo the selected channel (from any window)"). It has no key by default (FL keeps its keys); you bind one in the shortcut editor. It runs the rack's solo, `noteedit::soloChannel`, so pressing it again lets every channel play, as in FL.

### 20. Ultra-fine knob drag
- **FL:** Ctrl+drag and Ctrl+wheel are fine. Checked in FL 2026 (shots in /mnt/e/lynx-work/rec177d/fl/k00–k15): Ctrl+drag of 40 px moved a knob 78 % → 82 %, and Ctrl+Shift+drag of 40 px moved it 82 % → 86 %, so FL treats Ctrl+Shift the same as Ctrl.
- **Change:** Ctrl+Shift = ultra-fine (1/100) for drags and the wheel on every `ValueControl` (`controls::fineScale`). Ctrl alone is unchanged (FL), and so is Shift alone. Switching modifiers mid-drag continues from the current value.
- **Is this the best way?** Yes, as a LYNX extra (coordinator decision, 2026-11-06). Every FL gesture stays the same: Ctrl alone is FL's fine step, and Ctrl+Shift in FL only repeats it, so LYNX gives that combination a use without taking one away. An option to turn it off was considered and not added, because nobody who uses FL's gestures can notice it. Logged in docs/PARITY.md › "Host workflow: LYNX extras over FL".

### 15. Backups that never stutter the sound
- **FL:** postpones the timed backup while recording (LYNX did the same).
- **User decision:** a Settings › General option, "Wait with backups while the song plays or a MIDI keyboard is being played", **on** by default. A due backup then waits while the transport plays and for 5 s after the last MIDI input (`prefs::backupMayRun`). It never waits while recording. Off = FL's behaviour.

### 31a. Pattern hygiene (not the declined 31b auto-unique modifier)
- **FL (FLEngine_x64.dll strings, 2026.1):** the Playlist picker has "Select unused" › Patterns / Audio clips / Automations (`EEPickerSelectUnusedMenu`, `EEPickerSelectUnusedPatternsMenu`, `…AudioClipsMenu`, `…AutomationsMenu`). It also has "Sort patterns by name / by color" (`EEPickerSortPatternsByNameMenu` / `…ByColorMenu`), sort for audio clips (by name / color / track) and sort for automations (by name / color). FL has no merge of identical patterns.
- **LYNX before:** none of these picker items. Only "Purge unused audio clips" and "Select unused channels" existed.
- **Core (this slice):** `unusedPatterns`, `deletePatterns`, `reorderPatterns`, `sortPatterns` (by name: case-insensitive, stable; by colour: hue then lightness, with uncoloured patterns last in their order), `identicalPatterns` and `mergeIdenticalPatterns`. Merge is LYNX's own (the wishlist item). Same content means notes, events, length, loops and markers; empty patterns are never merged.
- **Where FL puts them (confirmed, shots in /mnt/e/lynx-work/rec177d/fl/k00–k15):** the Playlist menu has a "Picker panel" submenu. It holds Enable (Alt+P), Show on right side and Width ›, then the Selection items, then Misc. options, and last a **Tools** section with "Select unused ›" (Patterns / Audio / Automations) and "Sort ›" (Patterns › / Audio › / Automations ›, each with By name / By color, and By track for audio). A right-click on the empty part of the picker opens no menu in FL.
- **UI (aligned with the FL shots):** Playlist menu › "Picker panel" opens with a "Tools" header, then "Select unused" › "Patterns (n)" (picks them in the picker, as FL's selection), and "Sort" › "Patterns" › By name / By color, as in FL. Below a separator are LYNX's own "Delete unused patterns (n)" and "Merge identical patterns (n)". The empty part of the picker has no right-click menu, as in FL. LYNX does not have FL's other Picker panel items yet (Enable, side, width and the Selection / Misc. options). Each change is one undo step. Audio clip and automation selection and sorting (FL's other items) are not built: the LYNX picker selects patterns only.

### 20 (continued). Sliders too
`juce::Slider` controls (plugin editors) go through the slider contract (`ValueControl.cpp`, `setFine`). They take the same scale: Ctrl = 10×, Ctrl+Shift = 100× finer, for drags and for the wheel.

### 10. The value at the cursor on hover (an option)
- **FL:** hovering a control shows its name and value in the hint bar only. A pill at the cursor shows only while dragging.
- **LYNX before:** the same, using the hint bar (`ui::hintFor` gives `Hint::value`) and the drag pill (`showValueTip`).
- **Change:** Settings › General › Editing, "Show a control's value next to the mouse when hovering over it (LYNX)", **off** by default (FL). When on, the hint timer that already runs shows the hint's value in the drag pill just above the cursor (`MainComponent::updateHoverValue`). It covers every control that reports a value to the hint bar. Nothing new runs while the option is off, and the pill is moved only when the value or the mouse position changes. A drag keeps its own read-out.

### 37. Opening a project saved by a newer LYNX SHARE
- **FL:** an FL project from a newer FL opens with "This project was made with a newer version" only when the format needs it. Otherwise an older FL drops what it does not know without saying so.
- **LYNX before:** the `.lynx` format is at version 1. A file from a later build opened silently, and saving it dropped any field this build does not know.
- **Change:** every save writes `"savedBy": "<version>"`. Opening a project whose `savedBy` is newer than the running build (`safety::compareVersions`, which compares the dotted numbers and ignores "-alpha") shows one notice: "This project was saved by LYNX SHARE {0}, and this is {1}. What the newer version added may be lost if you save it here: use File › Save as... to keep the original." Projects from before this round have no `savedBy` and never warn.

### 26. Copying ghost notes
- **FL:** ghost notes (other channels, and this channel in the clips that overlap) can only be looked at. Editing them means switching channel.
- **LYNX before:** other channels' ghosts become editable with Ctrl+Alt+V (as in FL). Clip ghosts are drawn only.
- **Change:** Piano roll › Edit › "Copy ghost notes" (LYNX's own, next to Copy). It puts every ghost note on show (the ghost channels that are ticked, plus the clip ghosts) on the note clipboard. Ctrl+V then pastes them into the channel being edited, at the same place. The item is greyed out when no ghost shows.

### 25. Song bar numbers in the Piano roll ruler (an option)
- **FL:** the Piano roll ruler numbers the pattern's own bars (1, 2, 3...) wherever the pattern plays.
- **Change:** Piano roll › View › "Ruler: song bars (LYNX)" (action `pianoRoll.songBarsRuler`, no default key), **off** by default (FL). When on, the ruler numbers the bars as the Playlist does where the pattern's first unmuted clip plays (`polish::patternSongOrigin`, the same clip the clip ghosts use). A pattern that is not on the Playlist keeps its own numbers. Only the labels change: the grid and the positions stay the same.

### 35. "Instruments only" channel stems
- **FL:** "Split mixer tracks" renders each insert, so a send reverb or a bus lands in its own file and not in the instrument's. FL has no per-channel render.
- **LYNX before:** "One per channel (stems)" mutes the other channels but keeps the mixer, so each stem carries its sends and the buses it goes through.
- **Change:** a fourth Export › Files choice, "One per channel, instruments only (no sends or buses)" (`Stems::channelsDry`). For each stem, the channel's insert loses its sends and goes straight to the Master (`makeChannelStemSong (song, c, true)`). Its own effects stay. The Master's effects follow the existing "Master effects" box. The dialog layout is unchanged (it is one more item in the same list).

## 4. Slice 177d-3: mixer strip (6a EQ bypass, 6b gain-reduction meter)

### 6a. Per-track EQ bypass
- **FL (checked, shots in /mnt/e/lynx-work/rec177d/fl/k00–k15):** the insert's 3-band Equalizer has no bypass of its own, and right-clicking its title opens no menu. You reset each band.
- **Change:** right-click the EQ title bar in the effects panel and choose "Bypass the EQ (its settings are kept)". `TrackEq::bypassed` is saved as `eqBypassed`. A bypassed EQ costs nothing in the engine (`TrackEq::active()` is false). The title then reads "bypassed" in the disabled-text colour, and the hint adds "[RMB] bypass". FLP export writes the bands flat while bypassed, which is what FL would hear.
- **Is this the best way?** Yes, as a LYNX extra (coordinator decision, 2026-11-06; pain point 6 from the approved list). It lives on a right-click that does nothing in FL, so no FL gesture changes. A visible button would be quicker to reach, but it would add a control FL does not have to every strip. Logged in docs/PARITY.md › "Host workflow: LYNX extras over FL".

### 6b. Gain-reduction meter (user decision: LYNX's own dynamics only)
- **FL:** no gain reduction on the mixer strip. Each dynamics plugin shows it in its own window.
- **Change:** a 3 px bar to the right of the strip's level meters, hanging from the top (0 to 30 dB, `mixerops::gainReductionRangeDb`). It is drawn only on strips that have an enabled LYNX compressor, Apex limiter, Crest compressor, Sentry gate, Quell denoiser, Ridge strip, Glow leveler or Hush de-esser (`mixerops::reportsGainReduction`). It reads the effect meters the engine already publishes (output 0), so the audio thread does no new work. The UI checks only strips that show a meter.

## 5. Found along the way, and later proposals

### Audio-take track name and take name disagree (traced: a routing choice, not a naming bug)
Seen at the 177d-1 GUI check (shots l25/l26). An audio take recorded through Insert 6 is named "Insert 6_Take1", but its Playlist track is named "Insert 7".

Both names are right about different things. The file name uses the insert that was recorded (`AudioEngine`, `takes::takeName`). The track name uses the insert the clip plays back through (`takes::linkTakeTrack`). A take tapped at Post effects or later sets `playThrough = 0`, so `placeTakeClips` claims a new free insert, because playing through its own insert would apply the effects twice. Post EQ is the default position (`MixerTrack::recordSource`), so an ordinary recording takes this path.

Round 168x chose this routing and noted "FL's routing of such a clip was not measured". Still to do: record through an insert in FL at Post EQ and see which insert the clip plays through, then compare. 177d-1 did not cause this: it only draws the take while it records. Nothing changes until that FL check is done.

### Proposal: undo steps for tweaks made in a plugin's own GUI (pain point 16)
Plugin parameter moves made in the plugin's own window make no undo step today, and the History labels name the channel or insert, not the parameter. This needs the plugin state captured before and after a gesture (`PluginWindows::onGesture` already reports begin and end) and stored in History. That is a larger change, logged as a later proposal (coordinator, 2026-11-06).
