# REA round 178a: issues #87, #63, #86, #88 and PRs #89, #91, #80, #71, #76 (reported on 0.1.24)

Per item: the reporter's words | FL evidence | LYNX before | LYNX now. Target: FL Studio 2026 (`FLEngine_x64.dll`, the
build of rounds 165–177), Ghidra 12.1.4 headless plus FL sessions on Windows. Markers: **[DECOMP]** FL code, **[DATA]** FL's
own files, **[FL]** seen in a running FL, **[CODE]** LYNX source, **[model]** our reading, **UNPROVEN** not verified.

## 1. #87 An audio clip does not follow a tempo change

- **Reporter (Jocho-Smith):** an audio clip dropped at 130 BPM, then the tempo lowered to 108: the audio ends before the
  clip does (an empty tail), with a screenshot.
- **FL evidence [FL]:** project `l_all.flp` (round 167l), every audio clip on channels with Time stretch off. At 86 BPM
  and then at 43 BPM (shots `b86c.png` / `b43c.png`) every clip **halves its length in bars**: FL keeps a non-synced
  clip's region in milliseconds and resizes it with the tempo. The same project with the `ring_full` channel's stretch
  time set to 1152 (0xD7 +0x60, a float since bit 0 of 0xA9 is set; `t87b.flp`): that clip **keeps its bars** at both
  tempos (`d86c.png` / `d43c.png`). So both directions are FL-exact.
- **LYNX before:** a tempo change only wrote `song.bpm`; the clip kept its ticks while its audio got shorter or longer in
  bars.
- **LYNX now:** `clipops::followTempo (song, bpm)` (src/core/ClipOps.h). For each audio clip of every arrangement whose
  channel has no stretch (`stretchBeats == 0`) it scales the length, the start offset (with its fraction) and the fades by
  new / old; the position stays. Every UI tempo setter uses it: the tempo display, the tap tempo, the settings dialog
  and Detect tempo's "Use it as the project tempo". The nudge keeps plain `bpm`, because it is a transport nudge. Pattern
  clips and tempo-synced audio clips keep their beats.
- **Test:** issues178a_audio_clips_follow_a_tempo_change (whole and trimmed clip, fades, a synced clip, a stored
  arrangement, 220 small steps back to the start tempo within 2 ticks).

## 2. #63 Note pan (the rest of it)

- **Reporter:** per-note pan has no effect. Round 171a fixed the drums.
- **FL evidence:** a note's Mod X / Mod Y on FL's sampler voice become the voice's `Init.FCut` / `Init.FRes` =
  (b − 128) / 128 of the note bytes, added to the channel filter's X / Y (REA_rodada76 §1 [DECOMP]). FL's
  Arpeggiator plays the held notes with their own properties [model, as FL's arpeggiated notes keep their colour and
  pan in a render].
- **LYNX before:** the Arpeggiator kept only key and velocity, so arpeggiated notes were centred with no fine pitch or
  Mod X / Y. FL's voice ignored a note's Mod X / Y.
- **LYNX now:** `Arpeggiator::hold (NoteEvent)` keeps each held note's `Expression` (pan, cents, Mod X / Y, gain,
  colour), applied to every on it plays, in normal mode and in Repeat 0 (the whole chord). `FlVoiceProcessor::Proc`
  gains `noteCut` / `noteRes` added to the channel's X / Y in `filter()`. The Sampler stores `setNextNoteModulation`
  and sets them at the voice start (Mod X 0.5 = 0, unchanged).
- **Still open:** release velocity and Mod X / Y on LYNX's own Sampler, and VST3 note pan / fine pitch (JUCE's host has no
  note expression).
- **Tests:** issues178a_arpeggiator_keeps_the_note_pan, issues178a_note_mod_x_moves_the_fl_voice_filter (channel X 228 with
  note Mod X 0.25 renders the same as X 100 centred, within 1e-6).

## 3. #86 Tempo as a pattern event: storage and playback

- **Reporter (kubapl2004gmail):** "Tempo as an event and you can save it as .fsc as any other score or automation.
  Implement detecting tempo changes and any other MIDI automation or event." FL: Piano roll › Events › Tempo.
- **How the FL user gets the lane [DECOMP FUN_0124a8e0 + DATA DFM]:** the piano roll's Target control menu has Note
  properties, Channel controls (panning, volume, pitch), **Pattern controls** (controls with events in the pattern) and
  **Recent controls**. There is no fixed Tempo item. The direct route is the tempo control's own menu: Edit events / Edit
  events in new window / Edit events in piano roll / Init song with this position.
- **Storage [DECOMP saver FUN_00f40840, DATA]:** `.flp` event **0xDF** in a pattern, records {u32 tick, u32 controlID,
  i32 value}. Tempo = **0x40000005**, value **BPM × 1000** (10 000…522 000, 0.001 BPM resolution; FUN_00d62a00 rejects
  values outside that range). Files older than FL 10.1.0 (version < 10001000) stored whole BPM (the loader FUN_00f45960
  multiplies them). 0xD8 (initialized controls) holds a start tempo. Secret Agent Rendezvous: 0xD8 110 000 while its
  0x9C project tempo is 120. LYNX's tempo value is the same normalisation: v = (raw − 10 000) / 512 000.
- **Playback [DECOMP time map FUN_012c9e20]:**
  - **Song mode:** pattern clips whose pattern has tempo events, plus tempo automation clips. Each writes only while
    the position is inside it. In the map's loop the pattern events come first, then the clip, so **inside a tempo clip
    the clip wins; elsewhere the latest write holds** and nothing is restored after a clip.
  - **Pattern mode:** the current pattern's events.
  - **Start value:** the 0xD8 tempo, else the project tempo.
  - The setter FUN_01046a70 writes the project tempo itself, as the knob does.
- **LYNX before:** tempo lanes were dropped on import ("target not reproduced"), the renderer skipped them, the Event
  editor and the menus hid the tempo, and the export wrote a tempo lane as v·65536.
- **LYNX now:**
  - src/core/TempoMap.h: header-only, allocation-free `tempomap::tempoAt` with the rule above, including LYNX's stretched
    clips that loop their pattern.
  - `Song::tempoAt` / `secondsForTicks` use it (song length, time display).
  - The renderer evaluates it once per block when `RackState::tempoMap` is set (a tempo automation or any tempo
    event), and the transport follows it. Nothing runs when a song has neither.
  - FLP import converts tempo events and the 0xD8 tempo (applied after the audio clips' millisecond offsets are
    placed). The export writes `round (10000 + 512000·v)`.
  - UI: the tempo menu shows "Edit events in the pattern" / "in piano roll". The Event editor and the piano-roll lane
    accept the tempo and show values in BPM.
- **Known gaps:**
  - The tempo is evaluated per audio block, not per chunk. FL's exact sub-block timing is UNPROVEN: the audio-thread
    dispatcher was not found.
  - The piano roll's Target control menu sections (Pattern / Recent controls) are not reproduced; LYNX's lane box
    lists the lanes that exist.
- **Tests:** issues178a_tempo_events_in_pattern_mode, issues178a_tempo_events_in_song_mode,
  issues178a_tempo_events_survive_flp.

## 4. #86 Event scores (.fsc)

- **FL evidence [DECOMP FUN_0121b330 / FUN_0121b1b0 / FUN_00e709b0]:**
  - In the event editor, **Save score as** keeps only the shown target's events, sets every controlID to 0 and
    normalises the values to 0..1 as float bits (FUN_0105fc40). The saver mode is **0x18**: `FLhd` format 0x18, `0x41 = 0`,
    then **0xDF** {tick, 0, f32}.
  - **Opening** one sets the IDs to the shown target, de-normalises with that target's range and **replaces all its
    events** (undo "automation loading"). Scores are target-agnostic.
  - The piano roll's own scores are notes only (format 0x10, 0xE0).
  - [DATA] none of FL's 609 factory .fsc files is an event score, so the format comes from the code alone.
- **LYNX now:**
  - `prtools::writeEventScore` / `parseEventScore` (src/core/PianoRollTools.cpp). The parser rescales ticks by the
    file's PPQ, sorts the events, keeps one per tick and refuses a note score.
  - The Event editor has a **Score** button (FL's File menu items Open score... / Save score as...). Opening replaces the
    shown target's events in one undo step.
  - LYNX's lane values are already the normalised values, so a Tempo curve goes onto a volume and back as in FL.
- **Test:** issues178a_event_score_round_trip.

## 5. #86 MIDI import of tempo changes

- **FL evidence [DECOMP FUN_00e1d090, FUN_009864f0]:**
  - The whole-file MIDI import turns **every Set Tempo** into a Tempo event of the destination pattern (its old Tempo
    events are cleared first), as clamp (round half even (60 000 000 / µs per quarter), 10, 522) × 1000.
  - It does not write the project tempo.
  - The piano roll's import, drop and MIDI-clipboard paste ignore tempo.
- **LYNX before:** only the first tempo was kept, clamped to 20..999. It was taken "while bpm == 120", so a file starting
  at exactly 120 took its second tempo.
- **LYNX now:**
  - `MidiFileIo` collects every tempo meta event (`collectTempos`) and sets the song tempo to the first one, as a whole
    BPM.
  - `midiimport::midiTempoBpm` gives FL's rounding and clamp. `midiimport::setTempoEvents` replaces the pattern's Tempo
    lane, one event per tick, moved with the realign.
  - Used by File › Import (`importInto`) and by opening a .mid as a song (`loadSong`). The piano roll's import is
    unchanged, as in FL.
- **Decision kept (add, never remove):** with "Start new project", LYNX still sets the project tempo to the first MIDI
  tempo. FL does not, but its tick-0 event sets it as soon as the song plays.
- **Test:** issues178a_midi_tempos_become_tempo_events.

## 6. #88 FL Studio Mobile projects (research; nothing landed)

- **Reporter (zh):** FLPs exported by FL Studio Mobile open as a mess.
- **Evidence:** no public FLM-exported .flp exists. FL Studio 2026 ships the FLM app as a plugin
  (`Plugins/Fruity/Generators/FL Studio Mobile/FL Studio Mobile_x64.dll`, v5.0.x), including its FLP writer at
  0x1805e37e0. Its event stream, read statically:
  - header: format 0, one channel, PPQ 96, version "24.2.99.4428";
  - every instrument is a **"FL Studio Mobile Rack"** generator (type 2) with its state in 0xD5, and no 0xDB / 0xD7 /
    0xDA;
  - the mixer has **no 0xEC (insert opener) and no 0x62 (slot index)**, and each insert ends on 0x93.
- **Why LYNX shows a mess:**
  1. The Mobile Rack is inert in LYNX, so every track plays one stand-in synth.
  2. `Flp.cpp` opens an insert only on 0xEC, so every insert name, colour, route and slot is dropped.
  3. Rack samples are FLM library paths: `DirectWave Samples EXP/...`, and paid EXP packs may be missing.
- **Best way (proposed order):** (a) accept mixer sections without 0xEC / 0x62 (open on the first insert event, close on
  0x93), (b) decode the Mobile Rack chunk tree (`SSLF/10LF → RACK 10KR → RHED RPRM RSMP RMOD{DESC PRMS CSTM}`) onto
  LYNX's existing engines, starting with DirectWave / drum pads and MiniSynth, (c) resolve FLM library paths and report
  missing EXP packs, (d) optionally open `.flm` directly (the same chunk format). Each step needs a real FLM export as a
  fixture first: the reply asks the reporter for one, and FL 2026's FLM plugin can probably save one (Save → FLP). That
  was not run in this round.

## 7. Contributor PRs

- **#89 (shaadart), LV2 scan once (fixes #85):** one child process scans every LV2 bundle in a batch, and a
  `scan-lv2.txt` stamp skips the rescan when nothing changed. A batch failure falls back to one child per plugin.
  Zero cost on Windows / macOS. Sound; merges cleanly with #91.
- **#91 (shaadart), a scan child with no answer has failed (fixes #90):** on Linux / macOS JUCE's
  `ChildProcess::getExitCode` returns 0 for a child killed by a signal. Because `scanOneFile` always writes its XML, no
  answer now means failure, and the plugin goes to the blacklist and scan-failures.log. CI gains a crashing `.clap`. Correct.
- **#80 (LordVersA), macOS out-of-process plugins:** already approved as-is for 0.1.25.
- **#71 / #76 (guike8), Remote Control API:** unchanged since the last review and now CONFLICTING. #71 conflicts in
  the i18n files only (`resources/i18n/*.txt`, `src/ui/I18nData.h`, `src/ui/i18n/*.h`). #76 conflicts in
  `src/engine/AudioEngine.cpp` plus the i18n headers. Open point on #71: `editSongRemoteTransaction` /
  `snapshotForRemoteControl` still hold the device callback lock around whole-project JSON. Rebase requests drafted.

## 8. #95 Invisible waveform on a white theme (a LYNX bug; FL has no such theme)

- **Reporter (brenhyltin):** Options › Theme, background #FFFFFF: the audio clip's waveform is invisible. The screenshot
  shows a pale blue body with nothing on it.
- **Cause:** `clipContentColour` (src/ui/PlaylistComponent.cpp) always tinted the clip colour 70% toward white, while the
  body mixes it 72% toward the theme background. On a white background both come out pale.
- **LYNX now:** when the body's perceived brightness is above 0.6, the content is the body mixed 65% toward black. Darker
  bodies keep the old colour. The same function draws pattern-clip notes and automation curves, so they follow.

## 9. Triage: #94 and #93 (nothing built)

- **#94 SPL Transient Designer:** a commercial plugin, so its code can't be used. LYNX already has FL's Transient Processor.
  A level-independent shaper of LYNX's own is a possible new effect, a decision for the user.
- **#93 MCP server:** the local control API is #75, with early work in #71. An MCP server could sit on top of it.

## 10. #86 checked in a running FL 2026.1.7 (corrects §4 and §1)

GUI session 2026-11-06 on t87b.flp and on the "Default (Basic with limiter)" template. Shots: /mnt/e/lynx-work/issues178a/fl/e2,
e5, e7, e8, e11, f2. Files FL wrote: fl/tempoev.fsc, fl/t87b.flp.

- **Menu text:** the Event editor's File menu reads *Open automation… / Browse automations / Save automation as… / Import
  MIDI file… / Paste from MIDI clipboard… / Export as score sheet…*, not "Open score / Save score as". The dialog's filter is
  "FL Studio score file (*.fsc)". LYNX now has a File button with Open automation… and Save automation as…. Browse, MIDI
  import and paste, and the score sheet are not in LYNX's Event editor.
- **Event .fsc, measured:** FLhd format 0x18, PPQ 96, `0xDF {u32 tick, u32 5, f32 0..1}` for a Tempo lane. §4 said the control
  field is 0; FL wrote 5 here. LYNX writes 5 for Tempo and 0 for the others. Other targets were not observed in FL.
- **Project storage, measured:** t87b.flp holds `0xDF {tick, 0x40000005, BPM × 1000}`. Last record 66651, and FL's hint bar showed
  "Tempo - 66.651 BPM"; the .fsc's last value 0.110646 is the same tempo on the 10..522 BPM scale. This confirms §2.
- **Tempo menu:** "Edit events" is enabled and gets a check mark once events exist. "Edit events in new window" and "Edit events in
  piano roll" were greyed in both projects, before and after drawing events (t87b) and on the empty template. What enables them
  was not tested.
- **Deliberate difference from FL (coordinator's decision, 2026-11-06):** LYNX keeps "Edit events in piano roll" enabled for the
  tempo, as a LYNX addition, because issue #86 asks for that route. This item is not FL-exact.
