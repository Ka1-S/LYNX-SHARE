# GAPS: what to fix after the 0.1.32 REA coverage (round 190rq, 2026-11-26)

Base: `origin/wip/rollup032` 87cd3c512 (0.1.32 step 4). References: FL Studio 2026 26.1.7 and Ableton Live 12.4.16,
captured today on this PC (`shots/ref/`), earlier BandLab / FLM captures, Live's built-in lesson files, FL's online
manual. Sources inside this folder: `COVERAGE-0.1.32-CLARITY92.md` (C92-*), `COVERAGE-0.1.32-TUTORIALS.md` (T-*), `COVERAGE-0.1.32-DETAILS.md`
(item numbers), `notes/SCREENS-FL.md` / `COVERAGE-0.1.32-SCREENS.md` (pairs), `COVERAGE-0.1.32-SCREENS-MOVED.md`.

**Columns.** Sev = high / med / low (how many users hit it and how lost they get). Rel = **0.1.32** (quality / fix: a
text, a hint, a label, the behaviour of an existing control, a wrapper over an existing operation) or **0.1.33** (a new
feature, a new lesson, new DSP). Every new string goes into the 14 language tables (`tools/i18n_tool.py` lint clean);
every Learn page fix touches the EN page and its 14 twins under `website/_src/learn/<lang>/` + a site build. UI changes
need the FL | LYNX (or Live | LYNX) pair at 100 % and @150 and the paint gate (perf-first).

Nothing here changes routing, sound or saved files.

## A. Clarity: "Playlist track 1 is not Mixer track 1" (queue item 92)

| Id | Sev | Gap | Fix | Files | Verify | Rel |
|---|---|---|---|---|---|---|
| G-01 (C92-1) | high | An **unassigned Playlist track** (most tracks of every FL project) says nothing about where its sound goes; this is the confusing case. FL says nothing either, Live has no such case (track = mixer channel, "Audio To" on every track) | Header hint value after the name, from the track's clips: one channel → "plays Kick → insert 1 (KICK)"; several → "plays 4 channels → inserts 1, 2, 5"; audio clips on the Master → "→ Master"; no clips → "(a lane for clips: they play through their channels' inserts)". LYNX extra "Routing hints (LYNX)" in Settings › LYNX extras, ON (text only, no gesture change), FL-exact turns it off | `src/ui/PlaylistComponent.cpp` ~15595-15620; helper next to `trackmodes::describe` in `src/core/PlaylistOps.cpp:798`; `src/core/LynxExtras.h`; i18n | hint shot on Thief's "Synth Bass" track (unassigned) and an empty track; unit test of the helper; FL-exact → bare name | 0.1.32 |
| G-02 (T-3, T-5, C92-4) | high | The Learn pages people read don't explain it: `pattern-tour` says "each channel plays into a Mixer track" without saying which or how to change it; `mixer-routing` covers insert-to-insert sends only and uses "track" loosely. The wiki explains it well (`mixer.md`, `first-song.md`) | `pattern-tour` § Mixer: the route box (the number between the knobs and the name; starters use 1-5; drag / wheel, double-click shows it, Ctrl+L gives free ones) + "Playlist tracks only hold clips, so Playlist track 1 is not Mixer track 1"; `mixer-routing` top section "Playlist track ≠ mixer insert" with the three places to look and the Link switch; one term "Mixer track (insert)" | `website/_src/learn/*/pattern-tour.md`, `mixer-routing.md` (×15), `first-song-tour.md` link | site `build.py --check`, `check-links` | 0.1.32 |
| G-03 (T-4, C92-3) | high | The in-app guides don't say it: Channel rack / Mixer "How to use" cards and the Mixer's first-open tip (`Tip::mixerRouting`, only insert-to-insert sends) | Channel rack card: "The number box beside the knobs is the Mixer track the channel plays through: drag it to change it, double-click it to show it." Mixer card + tip: "Each channel plays into the insert in its box in the Channel rack; Playlist tracks only hold clips. Want them paired? Playlist menu › Link Playlist tracks to mixer tracks." | `src/ui/HowTo.cpp:22-80`, `src/core/Onboarding.h:369,386` | Help › How to use… shots; Onboarding tests | 0.1.32 |
| G-04 (C92-2, details 3) | med | The rack's insert-box hint shows "Insert 10" without the insert's name, and promises "[RMB] next free insert" while right-click opens a menu | Value "Insert 10 · Synth Bass" (drag read-out too); gesture text "[RMB] menu (next free insert)"; Master as FL's "---" (now "-") if three dashes fit at 100 / 150 % | `src/ui/ChannelRackComponent.cpp:2063,3771,1340`; `src/ui/ChannelSettings.cpp:6824` | hover shot FL | LYNX (`pairs/92a`) | 0.1.32 |
| G-05 (C92-5) | med | LYNX's own answer, **"Link Playlist tracks to mixer tracks"** (Track N = insert N; `core/PlaylistOps.h:325`), is off by default and only in Settings › Project and the Playlist menu | Also at the end of Track mode › (one line: "Link all tracks to mixer tracks (LYNX)…", the same switch with its explanation); Search actions finds it under "mixer", "route", "link"; named in G-02 / G-03 | `src/ui/PlaylistComponent.cpp` ~10876, action list | menu shot, search test | 0.1.32 |
| G-06 (details 4, 6) | med | A channel linked to an instrument track gets **no mark** on its channel button or the track header; an insert linked to a Playlist audio track gets no mark unless it has its own icon. FL marks both (instrument icon; waveform icon on the insert, `pairs/92b`, `pairs/16`) — Thief's LYNX inserts show waveforms only because the .flp carries the icons | When linked and no own icon: LYNX's own `trackicons::Icon::keyboard` on the channel button and the instrument-track header, `Icon::audio` on the insert header; cache the linked flags per song stamp (30 Hz strip paint) | `src/ui/ChannelRackComponent.cpp:1425`, `src/ui/PlaylistComponent.cpp:3236`, `src/ui/MixerComponent.cpp:2122` | FL | LYNX pair in a **new** project after "Assign to new instrument track" / "Assign to new Audio track(s)", 100 / 150 %, paint gate | 0.1.32 |
| G-07 (D-3) | — | **Owner / REA question:** FL 2026's new project has one Sampler whose box reads "---" (Master, seen today, `ref/fl-e-rack`); the FL manual says new channels go to the Master. LYNX gives new channels a free insert (`ChannelRackComponent.cpp:4401`, round 162). Which is FL-exact for a channel *added* in FL 2026? | REA in FL (add a channel in a new project, read the box) before any change; G-02 texts must describe what LYNX does | — | FL shot | decision |
| G-08 | low | No insert badge on unassigned headers ("→ 1") | a visual extra: needs FL | LYNX review first | `PlaylistComponent.cpp` header paint | pair + paint gate | 0.1.33 |

## B. Wrong or misleading texts (fix now)

| Id | Sev | Gap | Fix | Files | Verify | Rel |
|---|---|---|---|---|---|---|
| G-10 (T-16) | high | `open-flp`, `open-als`, `open-mobile` promise the file opens in its own layout (Pattern / Clip-launch / Simple); the code never switches layout on open | "…then opens it. In the Pattern layout (`View › Layout style`) it looks as it did in FL Studio" (same pattern for .als / .flm); fix the three summaries. Owner option D-1 (0.1.33): offer the matching layout after an import | 3 Learn pages ×15, `index.json`, `website/learn/index.html` | open an .als from the Pattern layout: the layout stays | 0.1.32 |
| G-11 (details 12, D2) | high | **Help › First-song tour switches an FL user to the Simple layout without asking** (`MainOnboarding.cpp:540-551`); the item's name doesn't say so | Ask first: "The first-song tour runs in the Simple layout. Switch to it? (View › Layout style brings you back)" [Switch] [Cancel]; name it "First-song tour (Simple layout)" outside Simple | `src/ui/MainOnboarding.cpp`, `src/ui/MainComponent.cpp:12266-12269` | menu + box shot in Pattern; tour tests | 0.1.32 |
| G-12 (T-1) | high | `help-and-tips` and `command-palette` say `?` opens Search actions; in the Clip-launch layout with Live's look `?` toggles the Info View, while the Help menu still shows "Search actions… ?" | Pages: "press `?` (in Clip-launch: `Ctrl+Shift+Y`, `?` shows the Info View as in Live)"; the Help menu shows the key the layout really uses | `help-and-tips.md:23`, `command-palette.md:9` ×15; `src/ui/MainComponent.cpp:12291` | press `?` in each layout; menu shot | 0.1.32 |
| G-13 (details 7) | med | Two Mixer commands have two names each ("Route selected channels to this track" in the insert menu = FL 2026; "Link the selected channels to the selected insert" in the Mixer menu, key map, F1 sheet, site); the rack's Ctrl+L has four names. Search actions for "route" misses them | One FL name per command: `mixer.linkChannels` → "Route selected channels to this track", `mixer.linkChannelsFrom` → "Route selected channels starting from this track", `rack.sendToInserts` → "Route to free mixer track" | `src/core/KeyMap.cpp:226,543-544`, `src/ui/MainComponent.cpp:6026-6027`, `src/ui/ChannelRackComponent.cpp:3379,4344`, `website/_src/wiki/shortcuts.md:244-245`, `mixer.md:118`, `first-song.md:65` | grep: one text per action id; `?` "route" finds both | 0.1.32 |
| G-14 (T-17) | med | Small wrong Learn texts: plugin formats (open-flp / vst-plugins say VST3 + CLAP only; Windows / Linux host VST2 too, macOS AU), first-song-tour step 5 ("Show the loops", not "Do it for me"), switch-layouts lists 3 of the welcome card's 5 answers, no "Help › First-song tour from any layout / Skip tour" | as listed in TUTORIALS.md §2a (W-5…W-9) | those pages ×15 | code refs in TUTORIALS.md | 0.1.32 |
| G-15 (details 9) | low | Playlist track menu: "Consolidate track" acts on one track (FL "Consolidate track(s)" on all selected); "Change color" is a submenu (FL opens the colour dialog) | FL's name + selected tracks; "Change color..." opens LYNX's colour dialog like the rename window's | `src/ui/PlaylistComponent.cpp:10764,11129-11133` | FL | LYNX pair `pairs/01` | 0.1.32 |
| G-16 (T-18) | low | Recording dialog "Don't ask again (...)" while FL 2026 and LYNX's other boxes say "Remember my choice" | rename | `src/ui/Dialogs.cpp:235`, `record-voice.md:25` | shot | 0.1.32 |
| G-17 (details 15) | low | Clip-launch: no "Drop Files and Devices Here" in the empty Session area; Detail says "Drop effects here" where Live says "Drop Audio Effects Here" | paint Live's text right of the last track (Live look only, cached text); reword | `src/ui/ClipGridComponent.cpp:751-758`, `src/ui/DetailView.cpp:408` | Live | LYNX pair (`ref/live-l02`), paint gate | 0.1.32 |

## C. Missing help / lessons

| Id | Sev | Gap | Fix | Files | Verify | Rel |
|---|---|---|---|---|---|---|
| G-20 (T-6, details 11) | high | Help menu has no Tutorials / Manual / FAQ links (FL: Help index, Tutorials ›, forum / knowledge base; Live: Learn View, Read the Live Manual, Learn Live at ableton.com; BandLab / FLM: video tutorials, help centre) | Add "Tutorials (lynx-share.com/learn)…", "Manual (wiki)…", "FAQ and troubleshooting…" with the app's language (`HowTo::tutorialUrl`) | `src/ui/MainComponent.cpp:12263-12330` | each item opens the page in pt-BR too | 0.1.32 |
| G-21 (T-9, details 11) | med | No Help › What's new (FL has it); LYNX's notes window shows only behind the update bar | 0.1.32: "What's new in this version…" opens `lynx-share.com/updates/#<version>`; 0.1.33: the in-app notes panel | `src/ui/MainComponent.cpp` Help; `src/ui/Updater.cpp:1383,1453` | item opens the right notes | 0.1.32 / 0.1.33 |
| G-22 (T-15, details 12) | med | No guided lesson **inside the Pattern layout**. FL 2026: Help › Tutorials › Step sequencer tutorial (9 steps: spotlight + "n/9" card, action steps that advance when done, Esc ends; `ref/fl-tut-step-*`) and Loop starter tutorial (11, FL Cloud). LYNX's tour machinery (coach ring by component ID, steps advance when the song shows it done, `core/FirstSongTour.h`) runs only in Simple | a 6-step Pattern tour on the same engine (steps, Fill each 4, Piano roll, drag the pattern to the Playlist, Song mode, the route box → Mixer), LYNX's words; then Clip-launch | `core/FirstSongTour.h`, `ui/FirstSongTour.cpp`, Help menu | tour tests like FirstSongTour190dv; FL | LYNX card shots | 0.1.33 |
| G-23 (T-7) | med | File management undocumented (Live's Collect / Pack / Missing samples / Project lessons): Collect all and save, Always collect, Zipped loop package, samples live beside the .lynx | `save-backups.md` § "Move a project to another computer"; wiki `exporting.md` | Learn ×15 | items exist (`MainComponent.cpp:11786,11799,11944`) | 0.1.32 |
| G-24 (T-10) | med | `audio-setup` doesn't name LYNX ASIO (built in), Input device, Sample rate or "Measure the round trip" (Live's Audio I/O + Driver Error Compensation lessons) | as TUTORIALS.md T-10 | `audio-setup.md` ×15 | strings `SettingsPages.cpp:1063-1065,1619,1644-1716` | 0.1.32 |
| G-25 (T-11, T-13a, T-21, T-14, T-8, T-2) | low-med | record-voice only for Simple; MIDI keyboard page lacks "Link to controller…"; Next-3 card opens the English wiki; Info View / extended hint panel undocumented; clip-launch-tour demo path | as TUTORIALS.md | pages ×15, `MainOnboarding.cpp:325` | — | 0.1.32 |
| G-26 (details 14, T-19) | low | LYNX has Live's Info View box (Clip-launch) but it repeats the one-line gesture hint; Live gives a title + paragraph per control (`pairs/92c`) | paragraphs for the 30 most-used controls (an `about` text beside the hint) | `HintBar`, hint sources | — | 0.1.33 |
| G-27 (T-12, T-13) | med | New Learn pages: "Record audio in the Pattern layout", "Map knobs and pads" | new pages + video | — | — | 0.1.33 |

## D. Screens and menus (FL / Live parity details)

| Id | Sev | Gap | Fix | Files | Verify | Rel |
|---|---|---|---|---|---|---|
| G-30 (details 16) | med | Clip-launch track header menu lacks Live's Duplicate, Delete, Insert Audio / MIDI / Return Track, Select Track Content, Assign Track Color to Clips (`ref/live-l06`) | add in Live's order as wrappers over existing ops (Playlist clone / delete, `trackmodes::addAudioTrack` / `addInstrumentTrack`, `SessionMixer.cpp:560`); LYNX's own items after a line | `src/ui/ClipGridComponent.cpp:2969-3020` | Live | LYNX menu pair, each item + undo | 0.1.32 |
| G-31 (details 17c, 190rl FIX-LIST 12) | med | Selection after undo / redo is kept **by index**, but notes and clips are re-sorted after a move, so undo can highlight other notes / clips | remember the selection by content (`noteKeys` / clip `uid`, already computed by the selection history) and re-find it after the song is replaced; FL check first | `src/ui/PianoRollComponent.cpp:6958-6985,1528`, `src/ui/PlaylistComponent.cpp:16289` | suite test: select notes 3-4 of 8, move past 6, undo → 3-4 selected; Playlist twin | 0.1.32 (test first) |
| G-32 | low-med | Opening Thief: FL shows the whole song (bars 1-89) and the Playlist scrolled to where it was saved; LYNX imports the row height (0xF2) but opens at bar 1 / track 1 at its default zoom (`pairs/10`) | REA the .flp's playlist view events (horizontal zoom, scroll); apply on import | `src/core/FlpImport.cpp:4587` | FL | LYNX open shots of 3 demo songs | 0.1.32 if the events are found, else 0.1.33 |
| G-33 (details 8) | low | Channel menu: FL 2026's "MIDI channel through" (tick), "Auto fill", "Show waveform preview" (Sampler) are missing; "Create sampled instrument..." is FL's "Create DirectWave instrument..." but makes a Sampler channel | 0.1.33: MIDI channel through; FL check of Auto fill / waveform preview; D3: make the auto-sampler create a DirectWave channel, then take FL's name | `src/ui/ChannelRackComponent.cpp:4327` | — | 0.1.33 |
| G-34 | low | Playlist track menu: FL 2026's "Show ghost notes" (rollup s4 question 1) | add, ticked by FL's default | `PlaylistComponent.cpp` track menu | pair `pairs/01` | 0.1.33 (owner asked in s4) |
| G-35 (MOVED-r032 #1) | med | BandLab Mixdown: the Finish dock offers WAV / MP3, no M4A (the Export window has it where AAC exists) | add the M4A row where `aac::available()` | Simple Finish dock | BandLab capture `bandlab190p` | 0.1.32 |
| G-36 (MOVED-r032 #2) | med | Live Drum Rack pad view default and I-O / S / R placement reported different, never re-checked | a Live | LYNX pair, then fix | `DrumRack*` | pair | 0.1.32 if confirmed |
| G-37 (MOVED-r032 #3, #4) | med | Live Note Editor: no pad-name key column; Sample box: no Follow | small additions to existing screens | Detail view | Live | LYNX pairs | 0.1.32 (S) |
| G-38 (MOVED-r032 #5-#10) | — | Live Export Rendered Track / Start / Length, Clip View audio editor, Arrangement track header, FL Export Mastering, Simpler single panel, Utility Bass Mono / DC | features | — | — | 0.1.33 |

## E. Checked and fine (no fix)

Same as FL / Live: right-click deletes a Playlist clip; Esc closes the focused window (#217); Track mode submenus
(`ref/fl-f08`, `fl-f09`); the Mixer insert menu's FL order (`pairs/04`); double-click an empty slot makes a one-bar MIDI
clip (DL-22); Space in the audio editor plays its audio; backups carry the date; plugin monitor fractional %; the
scroll bar never zooms; the crash-recovery box only after an unclean exit; detached plugin windows give the keys back.
LYNX's own and fine: the empty-workspace card (#305), the richer hint bar, the insert hint's "Channels: …" list, the
New project window, the Clip View's "Select a clip…" text.

## Owner decisions found (no silent parking)

- **D-1** Opening a file: switch to the matching layout? Recommended: texts now (G-10), an offer after an import in
  0.1.33.
- **D-2** F1: FL opens the focused window's manual page, LYNX the shortcut sheet. Recommended: keep the sheet in 0.1.32
  and fix the text; 0.1.33 a switch "F1 opens this window's help (FL)".
- **D-3** New channel's insert (G-07): REA in FL first.
- **D-4** Ctrl+N: FL gives it to Save new version, LYNX to New project. Recommended: keep LYNX's (common to every other
  app), list it in the FL-differences page.
- **D-5** First-song tour from the Pattern layout (G-11): ask before switching. Recommended: ask (taken as the
  recommended, reversible option).
- **D-6** "Create sampled instrument" → DirectWave + FL's name (G-33): recommended for 0.1.33.
