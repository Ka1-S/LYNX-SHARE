# Screens coverage for 0.1.32: FL Studio 2026, Ableton Live 12, BandLab, FL Studio Mobile (round 190rq, 2026-11-26)

Two parts:
1. **The inventory** (774 screens of five programs, rounds 190jw → 190og), re-judged diff-first on the 0.1.32 base.
2. **Side-by-side pairs** made today: FL 2026 and Live 12.4 captured on this PC, LYNX from the same project on the
   0.1.32 build. Each pair is marked **same** / **missing detail** (what) / **wrong** (what) / **LYNX's own** (fine).

**Bases.** r031p = the last judgement (190og: rollup031 step 3 85ac7cacd + tabs2_190nz + helix4_khs3_190oc). r032 =
`origin/wip/rollup032` 87cd3c512 (0.1.32 step 4, contains 0.1.31.2).

**Material** (in `/mnt/e/lynx-work/rea_cov_190rq/`):
- References (`shots/ref/`):
  - FL Studio 2026 26.1.7: "Ookay - Thief", a new project, the Step sequencer and Loop starter tutorials, menus, hints. The owner's name in FL's hint panel is blurred. The raw shots stay in `raw/` and are never committed.
  - Ableton Live 12.4.16 Trial: the empty Set, Info View, menus, Learn View, Arrangement, a double-click on an empty slot.
  - BandLab and FLM: the captures of rounds 190p and 190ia.
- LYNX (`shots/lynx/<route>/`, 100 % and `@150`): offscreen routes of the step-4 Linux build, test mode, private
  display, on the same Thief `.flp` and Patience `.als`.
- Pairs: `shots/pairs/NN-*.png` (reference left / top, LYNX right / bottom).

## 1. The inventory, diff-first

Method as 190jw / 190og:
- Every yes / partial citation of the 774 rows was re-opened on r032: **0 failures**. Six quotes had been reworded and were re-cited: Live's control bar, the File / Edit / Create menus, and FLM / GarageBand's zip, which is now FL's "Zipped loop package...".
- Every open row was matched against the 532 commits and the rounds' HANDOFFs between the bases.
- Files: `screens/{fl,live,flm,bandlab,garageband}.json` (fields `lynx_r032`, `where_r032`, `note_r032`, `gap_r032`), `screens/summary-r032.json`, `COVERAGE-0.1.32-SCREENS-MOVED.md`.

| Program | Screens | r031p (0.1.31) | **r032 (0.1.32)** | Moved |
|---|---:|---:|---:|---:|
| FL Studio 2026 | 302 | 83.2 | **83.6** | 1 (Patterns › Transpose..., 190ri) |
| Ableton Live 12.4 | 185 | 64.0 | **64.0** | 0 (12 gap texts updated) |
| FL Studio Mobile | 122 | 73.3 | **73.7** | 1 (Settings › Reset this page, 190qx: partial) |
| BandLab | 89 | 84.9 | **85.6** | 1 (no-input-device notice: a re-check, already in r031p) |
| GarageBand | 76 | 72.4 | **73.0** | 1 (Sections lane = Arrangement track, 190ol) |
| **All five** | 774 | 76.0 | **76.3** | 4 |

Percent = (yes + ½ partial) / screens not n/a. No regressions.

0.1.32 is a quality release. Most of what landed since 0.1.31 is one of three things:
- LYNX's own work: Note Repeats, modulators, automation modes, the Chord lane, JSFX / LV2, Soft look, options UX;
- fixes and perf;
- polish on a screen that doesn't close what its row lists.

So the inventory barely moves. The pairs below are where 0.1.32's work is.

## 2. Pairs: FL Studio 2026 | LYNX (Pattern layout)

| # | Screen / element | Pair | Mark | What |
|---|---|---|---|---|
| 01 | Playlist track header, right-click | `pairs/01` | **missing detail** | FL 2026's "Show ghost notes" (ticked) is not there (rollup s4 question 1); FL "Consolidate track(s)", LYNX "Consolidate track"; FL "Change color..." opens the colour dialog, LYNX's "Change color" is a submenu. LYNX's own (fine, in FL's order otherwise): the "PLAYLIST TRACK / Track 7" header, "Comp these takes", "Clips own their notes", "Send pattern instances here" |
| 02 | Track mode › Audio track › / Instrument track › | `ref/fl-f08`, `ref/fl-f09`, `pairs/02` | **same** (route `--trackmodeshot`: "ALL SAME AS FL" on 3 states) | FL lists "New mixer track(s)" then "N. Name" for every insert; Instrument track › "Use existing channel ›", "More plugins...", categories. LYNX `PlaylistComponent.cpp:10816-10876` builds the same items (round 146a / 190dx); LYNX picture: `shots/lynx/trackmode*` |
| 03 | Channel button, right-click | `pairs/03` | **missing detail + LYNX's own** | FL 2026 items LYNX lacks: "MIDI channel through" (tick), "Auto fill" (greyed), "Show waveform preview" (Sampler); FL's "Create DirectWave instrument..." is "Create sampled instrument..." in LYNX (`ChannelRackComponent.cpp:4327`, names-stay rule). LYNX adds 17 items (Edit in tracker, Lock active/mute state, Freeze, Copy channel / Paste channels, Channel settings..., Miscellaneous functions, Arpeggiator ›, Loop point ›, Swing mix, Clear notes, Move up/down, Send to free mixer inserts, Sort, Select unused, Group selected, Transpose selected); FL 2026's menu is shorter, so FL's right column items sit lower in LYNX |
| 04 | Mixer insert, right-click | `pairs/04` | same + LYNX's own | FL's order kept; LYNX adds Audio input..., Track latency ›, a Sound section and a second column (Group extras, Mixer options). FL's header "Mixer track (KICK)", LYNX "Mixer tracks (3 selected)" when several are selected (fine) |
| 05 | Mixer › Channel routing › | `ref/fl-f17` | same in the insert menu; **wording** elsewhere | FL "Route selected channels to this track (Ctrl+L)" / "...starting from this track (Shift+Ctrl+L)" = LYNX `MixerComponent.cpp:4461/4465`; but LYNX's Mixer window menu and keymap call them "Link the selected channels to the selected insert" (`MainComponent.cpp:6026`, `KeyMap.cpp:543`), FL 20's old wording |
| 06 | Empty workspace (all windows closed) | `pairs/06` (190ri) | LYNX's own (fine) | FL: logo + edition line; LYNX: the window-keys card (#305) |
| 07 | Help menu | `pairs/05` | **missing detail** | FL: Help index, Gopher, **Tutorials › Loop starter / Step sequencer tutorial**, Diagnostics, **What's new**, web links. LYNX: Welcome, First-song tour, Getting started, How to use ›, tips, Search actions, Shortcuts (F1), Missing files, import report, Built-in plugins, Test audio output, Report a problem, updates, Support, About. No What's new, no Tutorials / Manual / FAQ links (T-6, T-9) |
| 08 | In-app tutorial (Help › Tutorials › Step sequencer tutorial) | `ref/fl-tut-step-*`, `pairs/13` | **LYNX missing (Pattern layout)**; LYNX's Simple tour is the same idea (dim + ring + "1 of 7" card, steps advance when done) plus Back and "Do it for me" | FL: dim overlay + round spotlight + card "STEP SEQUENCER TUTORIAL n/9", info steps (Continue), action steps that advance when done (rack Play, click steps, right-click › Fill each 4 steps, snare Fill each 8, drag Pattern 1 onto Track 1's header, Song mode + Play), "Understood"; Esc ends it. LYNX: Help › First-song tour in the Pattern layout **switches the app to the Simple layout without asking** (`MainOnboarding.cpp:540-551`); no Pattern-layout lesson. Loop starter tutorial (11 steps) needs FL Cloud: not followed |
| 09 | Hint bar | `ref/fl-f03`, `ref/fl-f12`, `pairs/92a` | LYNX's own (richer) | FL: name + a word on the right ("Audio", "(double-click)", "Track 4", position). LYNX: name + gestures + value (`s-hint-*`) |
| 10 | Playlist (Thief) | `pairs/10` | **missing detail** | FL opens Thief zoomed out to the whole song (bars 1-89) and scrolled down to "CUTL..ted"; LYNX imports the row height (event 0xF2, `FlpImport.cpp:4587`) but opens at bar 1 / track 1 with its default horizontal zoom. Needs REA of the .flp's playlist view events first |
| 11 | Channel rack (Thief) | `pairs/11` | same | insert boxes, buttons, steps; FL shortens names in the middle ("Titey..m #2"), LYNX shows them whole (wider buttons; earlier design rounds) |
| 12 | Mixer (Thief) | `pairs/12`, `pairs/16` | same, **one detail missing** | FL "Extra large" vs LYNX "Wide" layout preset; insert numbers on both. The waveform icons LYNX shows on Thief's inserts 1-9 are the icons stored in the .flp: LYNX draws no mark of its own when a Playlist audio track is linked to an insert (`MixerComponent.cpp:2122`), FL does (G-06) |
| 13 | Empty project: Playlist / rack / piano roll / mixer | `ref/fl-f24`, `fl-e-rack`, `fl-e-piano`, `fl-e-mixer` | see D-3 | FL's new project: one "Sampler" channel whose insert box reads "---" (Master); LYNX's templates give channels free inserts (`ChannelRackComponent.cpp:4401`) |
| 14 | Instrument track (Assign to new instrument track) | `pairs/92b` | **missing detail** | FL: header + channel button get the instrument-track icon, channel box → 1, hint "Sampler (Instrument track)". LYNX does the same steps and hint (`core/PlaylistOps.cpp:483-494,754-767`, `PlaylistComponent.cpp:15607-15611`) but draws no instrument-track mark on the channel button or the header (G-06) |

## 3. Pairs: Ableton Live 12.4 | LYNX (Clip-launch layout)

Live references come from 2026-11-26 (an empty Set) and from 190ia's 2026-11-16 Patience captures. LYNX shows the
Patience `.als` imported (`shots/lynx/gridals/`).

| # | Screen / element | Pair | Mark | What |
|---|---|---|---|---|
| 20 | Session View (Patience) | `pairs/20` | same (known partials) | grid, scenes, stop buttons, mixer, track numbers on the activators. Open rows as in the inventory (session-065 In/Out etc.) |
| 21 | Session mixer In/Out | `pairs/21` | **missing** (0.1.33) | Live shows MIDI From / Audio From, Monitor In Auto Off, MIDI To / Audio To on every track (and inline in Arrangement headers, `pairs/92d`); LYNX's session mixer has no In/Out section (inventory row session-065, partial) |
| 22 | Info View | `pairs/22` | same box, **missing depth** | LYNX has the box (bottom left, "Point at anything to read about it here.") and its track detail names the insert ("Insert 1 Coffee Leaf", channel) — clearer than Live on the link. Live's Info View gives a title + a paragraph per control (Track Activator, Output Type, Monitoring, Clip Slot, Track Title Bar); LYNX repeats the one-line gesture hint (G-26, 0.1.33) |
| 23 | Track title bar, right-click | `ref/live-l06` | **missing detail** | Live: Cut, Copy, Duplicate, Delete / Rename, Edit Info Text / Freeze, Bounce in Place / Insert Audio, MIDI, Return Track, Group Tracks / Select Track Content / Save as Default MIDI Track / palette / Assign Track Color to Clips. LYNX: Rename, Freeze, Re-freeze, Bounce in Place, Deactivate, Hide, Change color › (`ClipGridComponent.cpp:2969-3020`). The insert / duplicate / delete / select items are 0.1.32 wrappers (G-30) |
| 24 | Double-click an empty MIDI slot | `ref/live-l17` | same | a one-bar MIDI clip that opens in the Clip View (DL-22, `ClipGridComponent.cpp:878-895`) |
| 25 | Empty states | `ref/live-l02`, `live-lq` | **missing detail** | Live: "Drop Files and Devices Here" (Session), "Drop an Instrument or Sample Here", "Drop Audio Effects Here", "No clip selected". LYNX: no text in the empty Session area; "Drop effects here"; its own, more helpful Clip View text (G-17) |
| 26 | Help menu / Learn View | `ref/live-l09`, `live-l11` | **missing detail** | Live 12.4: Learn View (video lessons by topic: First Steps, Core Workflows, Navigating, Recording and Editing, Exporting), Pack Overview, Load Demo Set, Read the Live Manual, Learn Live at ableton.com, forum, support, account, updates, About. Live's older text lessons (Help View) are still in `Resources/Builtin Lessons` (TUTORIALS.md §1a). LYNX in Clip-launch: the same Help menu as Pattern, no manual / tutorials links (G-20) |
| 27 | Crash recovery on start | `raw/l00` (not published) | LYNX's own (fine) | Live 12.4.16 asked to recover "Untitled" after its own earlier crash and opened the Report a Crash lesson; LYNX asks once after an unclean exit (`MainComponent.cpp:9816-9915`) |

## 4. Pairs: BandLab / FL Studio Mobile | LYNX (Simple layout)

The references are earlier captures (BandLab web Studio, round 190p; FLM hosted in FL, round 190ia). BandLab and FLM
were not re-captured today.

| # | Screen | Pair | Mark | What |
|---|---|---|---|---|
| 30 | Mix Editor | `pairs/30` | same + LYNX's own | track headers (M / S, volume, +FX), timeline, bottom dock (LYNX: Instrument, Keyboard, Editor, Tracker, Controls, Effects = BandLab's Instrument / Effects / MIDI Editor and more). **Detail:** BandLab keeps its dashed "Drop a loop or an audio/MIDI/video file" row under the last track in every project; LYNX shows it only on an empty project unless the 190qu switch is on. BandLab numbers its tracks ("01 Instrument"), LYNX doesn't. Both low |
| 31 | FLM arrange | `pairs/31` | LYNX's own (desktop) | one window like FLM; FLM's phone layout was never meant to be copied |
| 32 | Mastering / Finish | `pairs/32` | **missing detail** | Finish dock: WAV / MP3, no M4A row (BandLab Mixdown M4A; inventory gap #1, G-35) |

## 5. Clarity of the channel → insert link (queue item 92)

See `COVERAGE-0.1.32-CLARITY92.md`:
- `pairs/92a`: FL's rack insert box and its hint "Target mixer track (double-click)".
- `pairs/92b`: FL's Assign to new instrument track.
- `pairs/92c`, `pairs/92d`: Live's I/O, its activator numbers and the Info View.

LYNX already shows more than FL does:
- the "Channels: …" list in the insert hint;
- "(Audio track, linked to mixer track N)" on the Playlist header;
- the insert in the Clip-launch track detail.

**The gap is the unassigned Playlist track, the common case:** nothing says where its clips' sound goes.

Fixes G-01 to G-06 are in GAPS.md.

## 6. @150

Every LYNX route was shot again at 1.5x (`*@150`): `fl15` (225), `gridals15`, `simple15`, `detail15`,
`trackmode15`. The onboarding route writes its own @150.

These were looked at:
- the Playlist headers, the picker and the toolbar (`fl15/s-playlist@150`);
- the channel menu;
- the Clip-launch Info View, the track detail and its devices (`gridals15/g-file-both@150`);
- the Simple tracks and dock (`simple15/s-simple-tracks@150`).

All are crisp:
- vector icons, no blurred bitmaps, no half-pixel hairlines;
- long names are shortened with ".." in the middle, as FL does ("Whit..Noise").

No @150-only defect was found. The onboarding route (`--onboard190npshot`) ran past the 15-minute timeout this round gave it after writing
52 pictures. Its `o-checks.txt` ends without a result line, so that route's own checks are incomplete. The pictures
used here were already written.

## 7. Top 10 screen gaps on r032 (inventory, by value)

From `COVERAGE-0.1.32-SCREENS-MOVED.md`:

| # | Gap | Size |
|---|---|---|
| 1 | BandLab Mixdown: no M4A in the Finish dock | S, 0.1.32 (G-35) |
| 2 | Live Drum Rack pad view: default view and the I-O / S / R places never re-checked against Live | S if a Live / LYNX pair confirms it (G-36) |
| 3 | Live Note Editor: no pad-name key column (S); Fold / Fold to Scale | 0.1.33 |
| 4 | Live Sample box: no Follow (S); RAM / HiQ / Complex Pro | 0.1.33 |
| 5 | Live Export: no Rendered Track chooser, Render Start / Length | 0.1.33 |
| 6 | Live Clip View audio editor (waveform, warp markers docked) | 0.1.33 |
| 7 | Live Arrangement: no Live track header (device, In / Out, sends) | 0.1.33 |
| 8 | FL Export Mastering: reference A/B | 0.1.33, needs an FL capture |
| 9 | Live Simpler single panel | 0.1.33 |
| 10 | Live Utility: Bass Mono, DC filter | 0.1.33 (DSP) |
