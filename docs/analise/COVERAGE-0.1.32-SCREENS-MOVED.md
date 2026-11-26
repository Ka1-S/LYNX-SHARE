# Screens re-judged for 0.1.32 (round 190rq, 2026-11-26)

Bases: **r031p** = 85ac7cacd (rollup031 step 3) + tabs2_190nz + helix4_khs3_190oc (the last judgement, round 190og); **r032** = origin/wip/rollup032 87cd3c512 (0.1.32 step 4, contains 0.1.31.2). Method: diff-first (rounds 190jw / 190og): every partial / no row's gap and screen text matched against the 532 non-merge commit subjects between the bases, the CHANGELOG 0.1.31 / 0.1.31.x / 0.1.32 sections and the rounds' HANDOFF.md files, then the new base grepped for the strings that prove the feature. A row moves only with a `file:line ('quote')` re-opened in r032 (`git show origin/wip/rollup032:<file>`); every yes / partial citation of all 774 rows was re-checked the same way: **0 failures**. Coverage % = (yes + 0.5 x partial) / rows not n/a.

## Per program

| program | rows | r031p yes/partial/no/n/a | r031p % | r032 yes/partial/no/n/a | r032 % | moved | gap text changed |
|---|---|---|---|---|---|---|---|
| FL Studio 2026 | 302 | 212/57/20/13 | 83.2 | 213/57/19/13 | **83.6** | 1 | 7 |
| Ableton Live 12.4 | 185 | 62/105/12/6 | 64.0 | 62/105/12/6 | **64.0** | 0 | 12 |
| FL Studio Mobile | 122 | 60/53/5/4 | 73.3 | 60/54/4/4 | **73.7** | 1 | 2 |
| BandLab | 89 | 53/18/2/16 | 84.9 | 54/17/2/16 | **85.6** | 1 | 0 |
| GarageBand | 76 | 46/18/12/0 | 72.4 | 47/17/12/0 | **73.0** | 1 | 3 |
| all five | 774 | 433/251/51/39 | 76.0 | 436/250/49/39 | **76.3** | 4 | 24 |

No regressions: every yes / partial citation re-opens on r032 (renamed items were re-cited by the coordinator; their notes are kept in `note_r032`), and no landed round removed a screen a yes row stands on.

## Moved rows

| program | screen | r031p → r032 | round | citation (r032) |
|---|---|---|---|---|
| FL Studio 2026 | Transpose (patterns) | no → **yes** | community_190ri | src/ui/MainComponent.cpp:7615 ('menu.addItem (tr ("Transpose..."), [this, picked] {'); src/ui/Dialogs.cpp:585 ('ui::openInlineTypeIn ({}, tr ("Semitones")'); src/ui/ChannelRackComponent.cpp:4355 ('menu.addItem (tr ("Transpose selected..."), [this] { transposeSelected(); });') |
| GarageBand | Arrangement track (sections: Intro, Verse, Chorus; drag to move / copy) | partial → **yes** | sections_190ol | src/ui/SectionsLane190ol.h:6 ('Simple (44 px): GarageBand's Arrangement track'); src/ui/SectionsLane190ol.h:482 ('sections::copy (s, index, before);'); src/ui/SectionsLane190ol.h:484 ('sections::move (s, index, before);'); src/ui/SimpleWorkflow.cpp:649 ('std::make_unique<juce::TextButton> (tr ("Sections"))') |
| BandLab | Headphone and microphone notices | partial → **yes** | re-check (predates r031p) | src/ui/SimpleDock.cpp:3564 ('Recording: wear headphones'); src/ui/MixerComponent.cpp:9207 ('No audio input device was found. Connect a microphone or an audio interface'); src/ui/MainComponent.cpp:7762 ('mixerinput::explain (notice);') |
| FL Studio Mobile | Restore default settings | no → **partial** | optionsux_190qx | src/ui/SettingsDialog.cpp:1509 ('juce::TextButton resetButton { tr ("Reset this page") }'); src/ui/SettingsDialog.cpp:1492 ('tr ("Reset the {0} page to its defaults?", { name })') |

- **FL Studio 2026 / Transpose (patterns)**: no -> yes: Patterns › Transpose... (every picked pattern, all channels) opens FL's small in-place 'Semitones' box where the menu was (FL 2026 TNameEditForm, checked live by the round), plus Channel rack › Transpose selected...
- **GarageBand / Arrangement track (sections: Intro, Verse, Chorus; drag to move / copy)**: partial -> yes: the Sections lane in the Simple layout is GarageBand's Arrangement track: named sections (Intro, Verse, Chorus...), drag moves a section with everything under it, Ctrl+drag copies, a drop on another swaps, Shift+drop replaces, the right edge resizes, double-click renames (core/SectionOps.h; one undo step each)
- **BandLab / Headphone and microphone notices**: partial -> yes (re-check, not a round's work): the 'no input device' explainer the gap asked for exists in every layout: the engine's recording notice noInputDevice shows 'No audio input device was found. Connect a microphone or an audio interface, then choose it as the input device in Settings > Audio.' (also in 85ac7cacd at MixerComponent.cpp:8352; 190jw / 190og missed it). With the 0.1.28 headphone note the row is covered.
- **FL Studio Mobile / Restore default settings**: no -> partial: Settings › Reset this page puts the shown page back at its defaults (General, Audio, MIDI, Theme, Ableton Link, LYNX extras; with Undo for 12 s); FLM's single 'restore all settings' step has no counterpart

Why so few: most of 0.1.32 so far is LYNX-own work (Note Repeats and Conditions, modulators, automation write modes, range edits, Track versions, Chord lane, JSFX, LV2, Soft look, options UX, notices), fixes and perf, or refines a screen without closing what its row lists (Transpose box aside). One of the four moves is a re-check correction, not new work: BandLab's 'no input device' explainer was already in r031p (MixerComponent.cpp:8352 there). Two gap texts were also corrected by re-check: FLM Analyzer (Wave Candy's vectorscope was already there) and Live Utility (Bass Mono / DC filter confirmed missing).

## Rows whose gap text changed (status kept; old `gap` kept, new text in `gap_r032`)

| program | screen | status | round(s) | gap on r032 |
|---|---|---|---|---|
| FL Studio 2026 | PATTERNS menu | partial | community_190ri, discord1124_190qu | Transpose... added (FL's in-place 'Semitones' box, community_190ri) and Split by channel made FL-exact (190qu); Group / Ungroup selected (patterns) still missing; Set time signature is still a song-wide submenu. |
| FL Studio 2026 | HELP menu | partial | optionsux_190qx, tutorials_190pg / 190pl | Help › How to use... (11 per-window guides, each with a button to its Learn video tutorial on lynx-share.com) stands in for a tutorials entry; still no Help index (manual) entry; registration and shop parts out of scope. |
| FL Studio 2026 | Main toolbar and panels | partial | toolbar_190qg | An overflow '»' menu now lists every item the window has no room for and an 'FL classic (one row)' preset matches FL's Single line (toolbar_190qg); FL's other toolbar options (flat buttons, several show-panel toggles) are still missing. |
| FL Studio 2026 | Welcome wizard | partial | issues243_190oq | A start page with recent projects and templates opens at every start, as FL's Welcome window (issue #252, on by default); FL's edition / trial pages are out of scope and its Gopher link is absent. |
| FL Studio 2026 | Tutorial highlight (guided tutorial) | partial | optionsux_190qx | LYNX's first-song tour plus one-time window tips (Mixer, Piano roll, Tracker, Device view, automation lane); still not FL's per-window highlight steps. |
| FL Studio 2026 | Fruity dB Meter | partial | details11_190oa | Mapped to Wave Candy's peak-meter mode, which now follows FL's dB Meter conventions (orange above -6 dB, dB under the mouse, right-click resets the holds, DP-76); no dB Meter editor of its own. |
| FL Studio 2026 | Fruity Envelope Controller | partial | details11_190oa | LYNX's own panel; FL's tension handles, Ctrl for fine moves and Shift / Ctrl level lock with a crosshair added (DP-71). |
| Ableton Live 12.4 | Arrangement View | partial | discord1124_190qu, automodes_190pa | The Arrangement is the FL-style Playlist (Live's look since 190jn: colour blocks, 19 px lanes). r031: track clips (190ne) let an instrument track own its note clips as Live's tracks do (on by default in Clip-launch); still no Live track header with device / In-Out / sends columns. r032: the Clip View also shows a clicked Arrangement clip (190qu), and range edits on chosen tracks exist (190pa); the track header gap is unchanged. |
| Ableton Live 12.4 | Browser: Filter View / Tag editor / Quick Tags | partial | picker_190qi | LYNX lists under a search/filter, but has no tag groups, tag editor or Quick Tags panel (doubt: filter scope not checked in a run). r032: tags form a tree ('Drums/Kick') with group chips and a batch 'Add tag to N items...' (190qi); still no Quick Tags panel or Live Filter View. |
| Ableton Live 12.4 | Learn View (Help View / Lessons) | partial | optionsux_190qx, tutorials_190pg / 190pl | A guided first-song tour and a Help menu exist; no docked lessons pane with topic filters. r032: Help › How to use... guides with Learn video links and one-time window tips (190qx); still no docked lessons pane. |
| Ableton Live 12.4 | Clip View: audio sample editor with warp markers | partial | discord1124_190qu | Warp markers and Warp From Here live in the audio editor window and the Playlist; the clip view itself shows no waveform. r032: an Arrangement audio clip now shows in the Clip View with its name and 'Open in the audio editor' (190qu); still no waveform in the Clip View. |
| Ableton Live 12.4 | Collect All and Save | partial | issues243_190oq, issues270_190qo | Live's File › Collect all and save now exists (every file from outside the project folder copied into Samples/Imported, the project relinked and saved; plus Always collect and save); Live's per-category choice (elsewhere / other projects / User Library / packs) is missing. |
| Ableton Live 12.4 | Startup dialog / first-run setup | partial | issues243_190oq | Doubt: Live's startup dialog contents were not captured; LYNX has a setup wizard. r032: a start page with recent projects and templates opens at every start (190oq). |
| Ableton Live 12.4 | File menu | partial | issues243_190oq | FL-order File menu in both layouts; Collect all and save is now in it; Manage Files and Export Live Set as stems are still elsewhere. |
| Ableton Live 12.4 | Edit menu | partial | flm_cov_190og (gap190og) | FL-order Edit menu; in Clip-launch it now has Live's Cut / Copy / Paste / Duplicate / Delete on the Device View (gap190og); Consolidate / Insert Silence / Split / Capture are still spread over context menus. |
| Ableton Live 12.4 | Help menu | partial | optionsux_190qx | Help menu with How to use... guides that link Learn video tutorials; no Live-style lessons / manual entries. |
| Ableton Live 12.4 | Audio clip context menu (Arrangement / Session) | partial | flm_cov_190og (gap190og) | Convert to MIDI, Slice to New MIDI Track, consolidate exist; Live's Crop Sample, Reverse, Reload, Warp submenus differ. r032: Live's Crop Clip exists for MIDI clips (gap190og); the audio clip's Crop Sample, Reverse, Reload and Warp submenus still differ. |
| Ableton Live 12.4 | Automation lane / envelope context menu | partial | details11_190oa, discord1124_190qu | A lane menu exists (now with 'Time selection to automation clip'); Re-Enable Automation is on the Clip-launch control bar (DL-58); Live's insert-shape list and Simplify Envelope were not found. |
| Ableton Live 12.4 | Utility | partial | re-check | LYNX Utility (gain, pan, stereo matrix) has no Bass Mono and no DC filter (re-check: an .als Utility's Bass Mono / DC filter are dropped with a report note, src/core/AlsDevices.cpp:472-474), so the 'not confirmed' is now confirmed missing. |
| FL Studio Mobile | Effect panel: Analyzer (26) (spectrum, oscilloscope, spectrogram, vectorscope) | partial | re-check | Wave Candy covers spectrum, oscilloscope, spectrogram and a vectorscope mode (re-check: the vectorscope was already in r031p); an .flm Analyzer module is dropped at import (sound-neutral) and has no panel of its own. |
| FL Studio Mobile | Help page (User manual, Getting started video, Video tutorials, forum) | partial | optionsux_190qx, tutorials_190pg / 190pl | Help menu with Getting started, the first-song tour and How to use... guides that link Learn video tutorials; no user-manual or forum entry. |
| GarageBand | Transposition track (key changes over time) | no | chordlane_190pf | No transposition over time; the Chord lane's follow (190pf) moves following channels' notes onto chords, which is not a semitone transposition track. |
| GarageBand | iPad: Song sections editor (A / B / C sections) | partial | sections_190ol | Sections lane (190ol): named sections, '+' adds 8 bars at the end, duplicate, resize, Loop this section; a resize does not ripple the later sections and there is no iPad-style section list that plays one section at a time. |
| GarageBand | iPad: Beat Sequencer (step grid, step velocity / repeat / chance) | partial | repeats_190pm | Channel Rack steps with velocity graph; per-note ratchets and conditions now exist in the Piano roll (190pm), but not as per-step repeat / direction / rate in the step grid. |

## Top 15 open screen gaps on r032 (by value)

Value 5 rows first (all 14 of them), then the most actionable value-4 row. Size: **S, 0.1.32** = behaviour or look of an existing screen (fits the quality release); **0.1.33** = a new feature, editor, panel or DSP.

| # | program / screen | value | status | what's missing | size / release |
|---|---|---|---|---|---|
| 1 | BandLab / Download menu / Mixdown As (M4A 128 / 192, MP3 320, WAV) | 5 | partial | the Finish dock's quick rows are WAV / MP3 only; M4A (AAC) sits in the Export window | S, 0.1.32: an M4A row where the system AAC encoder exists (look / behaviour of an existing dock) |
| 2 | Ableton Live 12.4 / Drum Rack: pad view (4x4 pads + overview strip) | 5 | partial | default pad view and I-O / S / R placement listed as different in ANSWERS B1a / 1c, never re-verified | S, 0.1.32 if real: needs one Live / LYNX shot pair first |
| 3 | Ableton Live 12.4 / Clip View: MIDI Note Editor (Notes tab) | 5 | partial | Fold / Fold to Scale and the pad-name-only key column in the docked note editor | split: pad-name key column S (0.1.32); Fold / Fold to Scale is a new view mode, 0.1.33 |
| 4 | Ableton Live 12.4 / Clip View: Sample box (audio: warp, mode, BPM, gain, pitch, reverse, edit) | 5 | partial | Follow, RAM, HiQ and Complex Pro in the Sample box | split: Follow display S (0.1.32); RAM / HiQ / Complex Pro are engine options, 0.1.33 |
| 5 | Ableton Live 12.4 / Export Audio/Video dialog | 5 | partial | Live's Rendered Track chooser, Render Start / Length fields, analysis file, render as loop | 0.1.33 (new export options; Live capture exists) |
| 6 | Ableton Live 12.4 / Clip View: audio sample editor with warp markers | 5 | partial | a waveform with warp markers inside the Clip View (an Arrangement audio clip now shows there, without one) | 0.1.33 (M: a new editor in the Clip View) |
| 7 | Ableton Live 12.4 / Arrangement View | 5 | partial | a Live track header (device, In / Out, sends) beside the Arrangement lanes | 0.1.33 (layout design call) |
| 8 | FL Studio 2026 / Export (render) dialog incl. Mastering | 5 | partial | Mastering: reference A/B algorithm choice and reference-file mastering | 0.1.33 (needs an FL capture of the Mastering section) |
| 9 | Ableton Live 12.4 / Simpler | 5 | partial | a Simpler single panel with Classic / One-Shot / Slice modes and warp | 0.1.33 (M-L, needs Live captures of the three modes) |
| 10 | Ableton Live 12.4 / Utility | 5 | partial | Bass Mono and DC filter (confirmed missing: dropped on .als import) | 0.1.33 (new DSP, plugin quality gate) |
| 11 | Ableton Live 12.4 / EQ Eight | 5 | partial | Mid / Side and A / B curve handling of EQ Eight | 0.1.33 (device panel, M) |
| 12 | Ableton Live 12.4 / Compressor | 5 | partial | Live's transfer-curve / activity views and Peak / RMS / Expand modes | 0.1.33 (device panel, M) |
| 13 | Ableton Live 12.4 / Reverb | 5 | partial | Live Reverb's algorithm and layout (Atrium / Reeverb stand in) | 0.1.33 (device panel + DSP, M-L) |
| 14 | Ableton Live 12.4 / Delay | 5 | partial | Live Delay's panel (Echoform / Fruity Delay 3 stand in) | 0.1.33 (device panel, M) |
| 15 | FL Studio 2026 / Main toolbar and panels | 4 | partial | FL's remaining toolbar options (flat buttons, several show-panel toggles); overflow and one-row preset landed | S, 0.1.32: toggles on an existing toolbar, each checked against FL |

Next tier (value 4, all 0.1.33-sized panels unless noted): FL Sytrus / Harmor / Kepler / Wave Candy / Formula Controller panels, FLEX (no: encrypted packs), Live Clip box signature, Launch velocity (doubt), Envelopes choosers, lane device / parameter choosers (doubt), take-lane commands (doubt), Live Operator / Drift / Auto Filter / Saturator / Glue / Limiter / Multiband / Echo / Drum Buss / Arpeggiator devices, FLM DW Sampler and the FLM effect panels (generated knob pages), BandLab Voice dock Input tab, BandLab chain-preset categories, GarageBand Drummer editor, iPad track controls, Beat Sequencer per-step repeat.

## Not verified / doubts kept
- GarageBand iPad Song sections editor stays partial: the Sections lane (190ol) has named sections, add / duplicate / resize / Loop this section, but a resize does not ripple and there is no iPad-style one-section-at-a-time list; no iPad | LYNX pair was taken.
- Live Collect All and Save stays partial: Live's command (copy, relink, save) landed, Live's per-category dialog did not.
- FL HELP menu, FLM Help page, Live Help menu / Learn View stay partial: How-to guides now link the Learn video tutorials, but there is no manual / Help-index / forum entry and no docked lessons pane.
- Rows whose doubt is about behaviour on screen (Live 43 launch velocity, 57 lane choosers, 58 take lanes, 66 / 67 Drum Rack, 84 startup dialog) are unchanged: source alone does not settle them; they need a Live | LYNX shot pair.

Files: `screens/{fl,live,flm,bandlab,garageband}.json` (fields `lynx_r032`, `where_r032`, `note_r032`, `changed_by_r032`, `gap_r032` where the gap changed, `cite_fail_r032`), `screens/summary-r032.json`.
