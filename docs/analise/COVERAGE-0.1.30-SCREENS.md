# Screens gap map 0.1.30: FL Studio 2026, Ableton Live 12.4, FL Studio Mobile, BandLab, GarageBand vs LYNX (round 190jw, 2026-11-16)

This map re-judges every screen row of round 190gf (`cov190gf/screens/*.json`, 698 rows) on three bases, and adds a
new GarageBand inventory of 76 screens. A screen is a window, view, dialog, panel, menu, settings page, or plugin /
device editor. Each row is marked:
- **yes**: LYNX has an equivalent screen;
- **partial**: the screen exists but lacks parts or differs in layout;
- **no**: LYNX has nothing for it;
- **n/a**: out of scope (store, licence, cloud account, social, Max for Live, Push).

**Bases** (snapshots, source only, nothing run):
- **v029** = tag v0.1.29-alpha, 0.1.29 as released. This is the "before".
- **r030** = `origin/wip/rollup030` 4c45364e4, the 0.1.30 integration.
- **r030p** = r030 + the finished pending branches merged locally:
  - livelook190jn, bandlab190jr, polish190jp-je, playlist190jq, capfix190jj, livefix190jd;
  - hosting190jg, dawproj190jf, midiout190jl;
  - parity190jh / js, workflow190iu, perfgate190id.

**Method (diff first).**
- Every yes / partial citation of 190gf was re-opened in each base. Of the 607 yes / partial rows, 603 kept every quote
  in all three bases; the quotes of 4 rows had changed:
  - three "Save changes?" prompts, now FL's "Save changes to {0}?" (190jp);
  - the BandLab tour step counter (redesigned tour, design190ia slice 22).

  These 4 were re-cited with the same status.
- Rows whose screen a 0.1.30 round touched were checked against source, using the HANDOFF claims of 190ig / in / ih / im
  / iv / iz / jb / jc / jr / jq / jp / jd / jj / io / jn as leads only.
- The sibling obligation re-judgements of this round (`../out/rejudge-*.json`) were used as a cross-check.
- A row that nobody touched keeps its 190gf status, with note_now "unchanged since 190gf".

**Files:**
- `fl.json`, `live.json`, `flm.json`, `bandlab.json`, `garageband.json`. Each row carries:
  - the 190gf fields (`lynx_190gf` = 190gf's status);
  - `lynx_v029` / `lynx_r030` / `lynx_r030p`;
  - `where_v029` / `where_r030` / `where_r030p` (`path:line ('quote')`, quote present in that base);
  - `note_now` and `changed_by`.
- `summary.py` re-opens every yes / partial citation in its base and writes `summary.json`. Result: **0 failures**.
- Build scripts: `work/build.py`, `work/gb_rows.py`, `work/cites.py`.

**Coverage %** = (yes + ½ partial) / rows that are not n/a.

## 1. Counts per program (before v029 → after r030 → r030p)

| Program | Screens | 190gf (r028) % | v029 yes / partial / no / n/a | v029 % | r030 yes / partial / no / n/a | r030 % | r030p yes / partial / no / n/a | r030p % |
|---|---:|---:|---|---:|---|---:|---|---:|
| FL Studio 2026 | 302 | 82.4 | 208 / 60 / 21 / 13 | 82.4 | 209 / 59 / 21 / 13 | 82.5 | 209 / 59 / 21 / 13 | **82.5** |
| Ableton Live 12.4 | 185 | 56.7 | 49 / 108 / 22 / 6 | 57.5 | 56 / 103 / 20 / 6 | 60.1 | 59 / 100 / 20 / 6 | **60.9** |
| FL Studio Mobile | 122 | 72.9 | 59 / 54 / 5 / 4 | 72.9 | 59 / 54 / 5 / 4 | 72.9 | 59 / 54 / 5 / 4 | **72.9** |
| BandLab | 89 | 81.5 | 48 / 23 / 2 / 16 | 81.5 | 48 / 23 / 2 / 16 | 81.5 | 50 / 21 / 2 / 16 | **82.9** |
| GarageBand (Mac 10.4 + iPad) | 76 | - | 40 / 23 / 13 / 0 | 67.8 | 41 / 23 / 12 / 0 | 69.1 | 41 / 23 / 12 / 0 | **69.1** |
| **The four 190gf programs** | | 73.6 | | 73.8 | | 74.6 | | **75.0** |
| **All five** | | - | | 73.2 | | 74.0 | | **74.4** |

**Corrections to 190gf.** 190gf read a simulated 0.1.28 integration. Two Live rows were already better in 0.1.28 as
released, so they move on v029:
- **Link toggle: no → yes.** `engine/AbletonLink.cpp` and `ui/Toolbar.h` LinkToggle with the peer count.
- **Grain Delay: no → partial.** Live's Grain Delay is imported onto Echoform in Pitch mode since 190fm.

**No regressions.** No screen is worse on r030 than on v029, or on r030p than on r030.

## 2. What moved, and which round moved it

**FL Studio 2026: +1 screen.**
- **DirectWave: partial → yes on r030.** Rounds 190hz (d7206cc61, eceb6bf9d) and 190jd (zone-map gestures, r030p).
  - The editor follows FL's form layout: PROGRAM bar, zone map, MOD MATRIX, LFO and FX groups, sample and loop rows.
  - It reads and writes .dwp / .dwb files.
- Other FL work in 0.1.30 sits inside screens that were already yes. None of it moves a screen:
  - wrapper menu (190in);
  - native editor headers (design190ia FW-S2);
  - Browser tabs and tags (190iz);
  - Theme page Adjustments (190jp);
  - mixer and rack looks (190io);
  - Extract stems options (190jr).

**Ableton Live 12.4: 57.5 → 60.1 → 60.9 %.** Moves on r030:
- **Browser: partial → yes.** 190iz: Live's sidebar (Collections / Library / Places) in Clip-launch.
- **Browser Places: partial → yes.** 190iz.
- **Control bar: partial → yes.** 190iv: Live's control bar. Tempo Follower is still absent and has its own row.
- **Expression Editor / MPE tab: no → yes.** 190hy MPE stage 2.
- **Clip Scale controls: partial → yes.** 190hr.
- **Quantize Settings: partial → yes.** 190hr.
- **Wavetable: no → yes.** 190ja: Helix, laid out as Live's Wavetable. Its wavetables are LYNX's own.

Moves on r030p:
- **Info View: partial → yes.** 190jn.
- **Status bar: partial → yes.** 190jn.
- **Stem Separation dialog: partial → yes.** 190jr: stem ticks, model, CPU and placement options.

Better, but still partial:
- Arrangement View: Live look, 190jn.
- Library categories: 190iz.
- Tag filter: 190iz.
- Launch box: Follow Action controls, 190hr.
- Sample box: Complex Pro / Texture settings rendered, 190ik.
- MIDI note editor: Live grid, 190hr.
- Drum Rack pads: Live look, 190jn.
- Operator: Tetra, LYNX's own 4-operator layout, 190hs.

**FL Studio Mobile: 72.9 % on all three bases.** No FLM screen moved. The sibling FLM obligation judge also found 0 moved
rows. One citation was re-cited: the confirm dialogs, now 190jp's text.

**BandLab: 81.5 → 81.5 → 82.9 %.** Moves on r030p:
- **Region context menu: partial → yes.** 190jr: Loop until next / Unloop and the loop handle.
- **Automation mode: partial → yes.** 190jr: the Automation switch in the Simple title bar.

Better, but still partial:
- Splitter: 6-stem model, 190jr.
- Voice dock: Input Level meter, 190ih.

**GarageBand (new): 67.8 → 69.1 → 69.1 %.**
- **Open a .band project: no → yes on r030.** 190ic, 09e161828.
- **Loop Browser:** user tags (190iz). Better, still partial.
- **Tempo track:** ruler tempo markers (190hq). It was already yes on v029; the tempo lane covered it.

## 3. Top 10 screens still missing or partial on r030p, by value

Ranked by value, then by how much of the daily workflow they carry. Ties at value 5 were broken by reach: a screen every
user opens comes first.

| # | Program | Screen | Value | r030p | What is missing (r030p) | Queue / plan |
|---:|---|---|---:|---|---|---|
| 1 | Live | Arrangement View | 5 | partial | Live look since 190jn, but the tracks are still FL-style free tracks: no track-bound lanes with In/Out / device / send columns beside the arrangement | owner decision O29 (BUILD-LIST): Live-style Arrangement "no for now" |
| 2 | FL / BandLab / GarageBand | Export dialogs: FL Mastering section; BandLab Mixdown As M4A; GarageBand Export Song to Disk (AAC / AIFF) | 5 | partial | FL: no reference A/B algorithm or reference-file mastering. BandLab and GarageBand: no M4A / AAC (or AIFF) writer | BUILD-LIST S1 (FL Mastering) and O31 / S5 (M4A via the OS encoder) |
| 3 | Live | Export Audio/Video dialog | 5 | partial | no Rendered Track chooser, Render Start / Length or Live's per-track options; FL-style dialog | BUILD-LIST slice 2 (Export dialog fields) |
| 4 | Live | Clip View: audio sample editor with warp markers | 5 | partial | warp markers live in the audio editor window and the Playlist; the clip view shows no waveform | BUILD-LIST slice 8 (warp markers in the clip view); not done by 190ik |
| 5 | Live | Clip View: Sample box | 5 | partial | Follow, RAM and HiQ switches (warp settings are complete since 190ik) | slice 8 residue (S) |
| 6 | Live | Drum Rack pad view | 5 | partial | Live look since 190jn; the default view and I-O / S / R placement differences from 190ei ANSWERS were never re-checked live | live check |
| 7 | Live | Simpler | 5 | partial | no Simpler-style single panel with Classic / One-Shot / Slice modes and warp inside the device | O29 (Live device panels later) |
| 8 | BandLab | Drum Machine dock | 5 | partial | no step-grid tab in the Simple dock and no PC-key row labels (steps only in the Channel Rack) | BUILD-LIST S5 |
| 9 | GarageBand | Loop Browser | 5 | partial | no shipped instrument / genre / mood keyword buttons, no key-follow preview, no audio / MIDI / drummer filter (user tags since 190iz) | not queued |
| 10 | Live | MIDI Note Editor (Notes tab) | 5 | partial | no Fold / Fold to Scale or pad-name-only key column | not queued |

Also value 5, but a group of their own: Live's five core effect panels (EQ Eight, Compressor, Reverb, Utility, Delay).
Each has a LYNX equivalent with a different panel. They need a "Live look" device-panel pass, as Helix got for
Wavetable (BUILD-LIST O29: 5 devices first, as skins on the existing engines).

Next, at value 4:
- FLEX (no editor, encrypted packs);
- FL main toolbar options;
- the per-chord menu of FL's chord progression tool;
- Harmor / Kepler / Sytrus pages in FL's layout;
- Live's Sends / Returns column and Clip box signature;
- GarageBand's Drummer Editor, Smart Controls, Beat Sequencer and Arrangement track.

## 4. Per group

**FL Studio 2026** (per group: screens, % v029 → r030 → r030p)

| Group | Screens | v029 | r030 | r030p |
|---|---:|---:|---:|---:|
| Main windows | 25 | 92.0 | 92.0 | 92.0 |
| Settings | 15 | 85.7 | 85.7 | 85.7 |
| Main menus | 8 | 75.0 | 75.0 | 75.0 |
| Context menus | 11 | 95.5 | 95.5 | 95.5 |
| Project and file dialogs | 21 | 78.6 | 78.6 | 78.6 |
| Editing dialogs | 23 | 78.3 | 78.3 | 78.3 |
| Piano roll tools | 18 | 94.4 | 94.4 | 94.4 |
| Edison tools | 23 | 91.3 | 91.3 | 91.3 |
| Tools and monitors | 7 | 64.3 | 64.3 | 64.3 |
| Out of scope (vendor, cloud, licence) | 12 | n/a | n/a | n/a |
| Native effects | 82 | 87.8 | 87.8 | 87.8 |
| Native effects / generators (both) | 8 | 100.0 | 100.0 | 100.0 |
| Native generators | 49 | 60.2 | **61.2** | 61.2 |

**Ableton Live 12.4** (per group: screens, % v029 → r030 → r030p)

| Group | Screens | v029 | r030 | r030p |
|---|---:|---:|---:|---:|
| Views | 22 | 63.6 | **65.9** | **70.5** |
| Mixer | 7 | 64.3 | 64.3 | 64.3 |
| Browser | 9 | 61.1 | **72.2** | 72.2 |
| Dialogs | 20 | 63.2 | 63.2 | **65.8** |
| Mapping | 3 | 100.0 | 100.0 | 100.0 |
| Detail/Clip View | 12 | 54.2 | **70.8** | 70.8 |
| MIDI Tools | 2 | 100.0 | 100.0 | 100.0 |
| Arrangement | 3 | 66.7 | 66.7 | 66.7 |
| Device View & Racks | 10 | 85.0 | 85.0 | 85.0 |
| Preferences | 10 | 55.0 | 55.0 | 55.0 |
| Menus | 8 | 37.5 | 37.5 | 37.5 |
| Context menus | 7 | 85.7 | 85.7 | 85.7 |
| Instruments | 15 | 46.4 | **53.6** | 53.6 |
| Audio effects | 45 | 43.2 | 43.2 | 43.2 |
| MIDI effects | 10 | 55.6 | 55.6 | 55.6 |
| Out of scope | 2 | n/a | n/a | n/a |

**FL Studio Mobile** (per group: screens, % v029 → r030 → r030p)

| Group | Screens | v029 | r030 | r030p |
|---|---:|---:|---:|---:|
| Views and editors | 15 | 86.7 | 86.7 | 86.7 |
| Mixer and rack | 9 | 83.3 | 83.3 | 83.3 |
| Keyboard and performance | 11 | 63.6 | 63.6 | 63.6 |
| Song settings and dialogs | 8 | 100.0 | 100.0 | 100.0 |
| Instrument panels | 8 | 68.8 | 68.8 | 68.8 |
| Effect panels | 33 | 63.6 | 63.6 | 63.6 |
| Note-effect panels | 5 | 40.0 | 40.0 | 40.0 |
| Main menu and files | 13 | 84.6 | 84.6 | 84.6 |
| Settings | 7 | 64.3 | 64.3 | 64.3 |
| Export and share | 6 | 66.7 | 66.7 | 66.7 |
| Help and onboarding | 3 | 83.3 | 83.3 | 83.3 |
| Online and store | 4 | n/a | n/a | n/a |

**BandLab** (per group: screens, % v029 → r030 → r030p)

| Group | Screens | v029 | r030 | r030p |
|---|---:|---:|---:|---:|
| Mix Editor | 13 | 88.5 | 88.5 | **96.2** |
| Start and tracks | 6 | 91.7 | 91.7 | 91.7 |
| Instrument | 6 | 91.7 | 91.7 | 91.7 |
| Drums and sampler | 7 | 71.4 | 71.4 | 71.4 |
| Voice and guitar | 6 | 58.3 | 58.3 | 58.3 |
| Effects | 6 | 91.7 | 91.7 | 91.7 |
| Finish and export | 6 | 80.0 | 80.0 | 80.0 |
| Project and menus | 13 | 84.6 | 84.6 | 84.6 |
| Onboarding | 5 | 100.0 | 100.0 | 100.0 |
| Local tools | 6 | 50.0 | 50.0 | 50.0 |
| Online and store | 15 | n/a | n/a | n/a |

**GarageBand (Mac 10.4 + iPad)** (per group: screens, % v029 → r030 → r030p)

| Group | Screens | v029 | r030 | r030p |
|---|---:|---:|---:|---:|
| Main window | 9 | 94.4 | 94.4 | 94.4 |
| Tracks | 4 | 100.0 | 100.0 | 100.0 |
| Mix | 6 | 83.3 | 83.3 | 83.3 |
| Global tracks | 4 | 62.5 | 62.5 | 62.5 |
| Media | 1 | 100.0 | 100.0 | 100.0 |
| Recording | 4 | 100.0 | 100.0 | 100.0 |
| Editors | 7 | 71.4 | 71.4 | 71.4 |
| Drummer | 2 | 25.0 | 25.0 | 25.0 |
| Loops | 3 | 50.0 | 50.0 | 50.0 |
| Amps | 3 | 50.0 | 50.0 | 50.0 |
| Settings | 7 | 85.7 | 85.7 | 85.7 |
| Share | 4 | 62.5 | 62.5 | 62.5 |
| Remote | 1 | 0.0 | 0.0 | 0.0 |
| Learn to Play | 2 | 0.0 | 0.0 | 0.0 |
| iPad | 14 | 50.0 | 50.0 | 50.0 |
| Live Loops | 4 | 62.5 | 62.5 | 62.5 |
| Interop | 1 | 0.0 | **100.0** | 100.0 |

## 5. GarageBand screen inventory (new, 76 screens)

**Sources.**
- The Apple GarageBand guide topic names (Mac 10.4 and iPad) listed in `in/garageband-rows.json` (column "source").
- General knowledge of the windows: the main window and its panes, editors, the Drummer, amps, settings, Learn to Play,
  the iPad Touch Instruments and Live Loops.
- GarageBand was not run and no shots exist on either side.

**LYNX side.** Started from this round's obligation judge (`../out/rejudge-garageband.json`); every quote was re-opened in
each base.

**Learn to Play and the Apple-ecosystem screens are judged "no", not n/a:** the Share menu, Logic Remote and the Sound
Library download.

**Result:** 41 yes / 23 partial / 12 no on r030p, **69.1 %**. Per group:
- Tracks, Recording and the main window are near 100 %.
- Lowest: Drummer (25 %), Learn to Play / Remote (0 %), Loops and Amps (50 %), iPad Touch Instruments (50 %).

| # | Screen | Group | Value | v029 | r030 | r030p | LYNX (r030p citation) | Gap / note |
|---|---|---|---:|---|---|---|---|---|
| 0 | Project chooser (New Project templates, Recent, Open) | Main window | 5 | yes | yes | yes | src/ui/StartFromPanel.cpp:141 ('Your template'); src/ui/MainComponent.cpp:9027 ('New from template') | Start-from panel and New from template; GarageBand's template set (Keyboard / Amp Collection, Songwriter...) differs. |
| 1 | Main window (control bar, Library, Tracks area, Smart Controls / editors pane) | Main window | 5 | yes | yes | yes | src/ui/SimpleWorkflow.cpp:540 ('Add a track: voice, instrument, drums, sampler') | The Simple layout has the same frame: tracks with headers, a bottom dock with sounds / effects / editor, top bar. |
| 2 | Control bar: transport (play, stop, go to start, record, cycle) | Main window | 5 | yes | yes | yes | src/ui/MainComponent.cpp:6079 ('Stop: [LMB] stop and go back to the start') | Same buttons. |
| 3 | LCD: Beats & Project (position, tempo, key, time signature) | Main window | 5 | yes | yes | yes | src/ui/MainComponent.cpp:6173 ('Tempo (BPM)'); src/ui/MainComponent.cpp:5629 ('Song time signature') | Tempo, key and signature in the toolbar (Simple: tempo / signature / Key capsules since r030). |
| 4 | LCD: Tuner mode | Main window | 4 | partial | partial | partial | src/ui/FlExtraEditors.cpp:318 ('class TunerEditor final') | The tuner is an effect plugin window, not a mode of the toolbar display. |
| 5 | Master volume slider and level meter (control bar) | Main window | 3 | yes | yes | yes | src/ui/Toolbar.cpp:488 ('Master volume') | Same. |
| 6 | Tracks area: ruler, playhead, cycle area, zoom | Tracks | 5 | yes | yes | yes | src/ui/PlaylistComponent.cpp:9627 ('Place loop'); src/ui/PlaylistComponent.cpp:9729 ('Zoom to show everything') | Playlist ruler loop and zoom. |
| 7 | Track header (mute, solo, volume, pan, record enable, input monitoring, lock) | Tracks | 5 | yes | yes | yes | src/ui/PlaylistComponent.cpp:12588 ('arm / disarm recording'); src/ui/PlaylistComponent.cpp:12572 ('Track pan: [LMB] drag up / down') | Lock is Freeze track in LYNX. |
| 8 | New Track dialog (Software Instrument, Audio mic / guitar, Drummer) | Tracks | 5 | yes | yes | yes | src/ui/SimpleWorkflow.cpp:251 ('Record your voice or any sound from a microphone.') | Simple chooser has voice, instrument, drums, sampler, guitar / bass, import; no Drummer track type. |
| 9 | Library (patch categories, sound packs, search, save patch) | Tracks | 5 | yes | yes | yes | src/ui/SimpleDock.cpp:856 ('Search sounds') | Sound picker in the Simple dock; no per-sound-pack view. |
| 10 | Smart Controls pane (macro knobs per patch) | Mix | 4 | partial | partial | partial | src/ui/RackEditor.cpp:92 ('Macro {0}'); src/ui/SimpleDock.cpp:267 ('Blend') | Rack macros and the Simple dock knobs; no curated macro panel per sound. |
| 11 | Smart Controls inspector / Transform Pad | Mix | 2 | partial | partial | partial | src/ui/RackEditor.cpp:1917 ('Variation morph (LYNX)') | Variation morph in racks, not an XY snapshot pad. |
| 12 | Arpeggiator (Smart Controls) | Mix | 3 | yes | yes | yes | src/ui/ChannelRackComponent.cpp:656 ('Arpeggiator') | Channel arpeggiator (order, rate, octaves). |
| 13 | Plug-ins area of the track (add / reorder / bypass effects) | Mix | 4 | yes | yes | yes | src/ui/SimpleDock.cpp:476 ('More effects...') | Simple dock effects and the FL Mixer slots. |
| 14 | Channel EQ (with analyser) | Mix | 4 | yes | yes | yes | src/core/FacetEq.cpp:1352 ('Facet EQ') | Facet EQ with analyser; different look. |
| 15 | Master track (master effects, volume automation) | Global tracks | 3 | yes | yes | yes | src/ui/MixerComponent.cpp:5481 ('Master chain in new projects') | Mixer master and its automation lane. |
| 16 | Automation lanes (track automation curves, parameter menu) | Mix | 4 | yes | yes | yes | src/ui/PlaylistComponent.cpp:12523 ('Add an automation lane under this track') | Same. |
| 17 | Arrangement track (sections: Intro, Verse, Chorus; drag to move / copy) | Global tracks | 4 | partial | partial | partial | src/ui/PlaylistComponent.cpp:9506 ('Add marker here...'); src/ui/PlaylistComponent.cpp:8075 ('Duplicate time') | Markers and time-based duplicate / delete; no drag of a whole section with its regions. |
| 18 | Tempo track (tempo points) | Global tracks | 3 | yes | yes | yes | src/ui/PlaylistComponent.cpp:7806 ('Lowest and highest tempo of the tempo lane (BPM)'); src/ui/PlaylistComponent.cpp:9527 ('Add tempo change here...') | The tempo lane (v029); ruler tempo markers since r030 (190hq). r030: ruler tempo markers (190hq) - better, same status. |
| 19 | Transposition track (key changes over time) | Global tracks | 2 | no | no | no | searched: transposition track, transposition point, key change over time; only the global Master pitch knob | No transposition over time. |
| 20 | Movie track / Movie window | Media | 2 | yes | yes | yes | src/ui/VideoTracks.cpp:447 ('Import video file') | Video track and export with the mix. |
| 21 | Take folder (choose / comp / delete takes) | Recording | 4 | yes | yes | yes | src/ui/PlaylistComponent.cpp:9231 ('Comp these takes') | Take lanes and comping. |
| 22 | Musical Typing window | Recording | 3 | yes | yes | yes | src/ui/MainComponent.cpp:2603 ('Typing keyboard to piano: on') | Typing keyboard to piano (no window of its own). |
| 23 | On-screen Keyboard window | Recording | 3 | yes | yes | yes | src/ui/TouchKeyboard.cpp:144 ('Touch keyboard') | Touch keyboard panel. |
| 24 | Metronome and count-in (control bar) | Recording | 4 | yes | yes | yes | src/ui/MainComponent.cpp:6088 ('Metronome: clicks on every beat while playing') | Same. |
| 25 | Note Pad | Main window | 2 | yes | yes | yes | src/ui/LyricsPanel.cpp:111 ('Lyrics and notes') | Lyrics / Notes panel. |
| 26 | Quick Help (help tags on hover) | Main window | 2 | yes | yes | yes | src/ui/HintBar.h:10 ('The hint bar') | Hint bar; Info View in the Clip-launch layout (r030p). |
| 27 | Undo History | Main window | 3 | yes | yes | yes | src/ui/HistoryPanel.cpp:19 ('Undo history') | Same. |
| 28 | Audio Editor (waveform, trim, reverse) | Editors | 5 | yes | yes | yes | src/ui/AudioEditor.cpp:1300 ('Insert warp markers') | Edison-style audio editor. |
| 29 | Audio Editor: Flex Time (flex markers, quantize audio) | Editors | 4 | yes | yes | yes | src/ui/AudioEditor.cpp:1302 ('Quantize audio') | Warp markers and audio quantize. |
| 30 | Track inspector: Pitch Correction (amount, Limit to Key) | Editors | 4 | yes | yes | yes | src/core/Effects.cpp:2161 ('Pitch correction') | LYNX Tune effect with a key; not a slider in the track inspector. |
| 31 | Piano Roll Editor | Editors | 5 | yes | yes | yes | src/ui/PianoRollComponent.cpp:3750 ('Quick quantize') | FL-style piano roll (docks in the Simple dock). |
| 32 | Piano Roll: MIDI controller lanes (modulation, pitch bend, sustain) | Editors | 3 | yes | yes | yes | src/ui/PianoRollComponent.cpp:5023 ('Pattern events') | Event lanes. |
| 33 | Score Editor (notation, note palette) | Editors | 2 | no | no | no | searched: notation, staff, score view, note palette, clef | No notation view. |
| 34 | Print music notation | Editors | 1 | no | no | no | searched: print, notation, PDF score | No printable score. |
| 35 | Drummer Editor (genre, drummer, XY loud / complex pad, kit pieces, fills, swing) | Drummer | 4 | partial | partial | partial | src/ui/LoopStarterWindow.cpp:241 ('Loop Starter genre: its tempo range, kit and drum patterns') | Loop Starter generates genre drum patterns; no drummer that plays a part, no XY complexity / loudness, no fills knob. |
| 36 | Drummer track with drummer regions and Follow (kick & snare follow a track) | Drummer | 3 | no | no | no | searched: Drummer, follow rhythm, kick & snare follow, drummer region | No drummer track type; Loop Starter output is an ordinary pattern. |
| 37 | Loop Browser (keyword buttons, column view, scale filter, favourites, preview in tempo) | Loops | 5 | partial | partial | partial | src/ui/BrowserComponent.cpp:3365 ('Search (Ctrl+F)'); src/ui/BrowserComponent.cpp:3678 ('Add tag...') | Search, starred items and in-tempo preview; user tags since r030 (190iz); no shipped instrument / genre / mood keywords, no key-follow preview, no loop-type filter. r030: user tags with Any / All filtering (190iz) - better, same status. |
| 38 | Media Browser (own audio / movie files) | Loops | 3 | yes | yes | yes | src/ui/BrowserComponent.cpp:4152 ('Add folder...') | Browser folders. |
| 39 | Sound Library (download more sounds and loops) | Loops | 3 | no | no | no | searched: Download, Get more sounds, sound pack, Sound Library | No downloadable content library. |
| 40 | Amp Designer (model, amp / cabinet / mic, EQ, reverb / tremolo) | Amps | 3 | partial | partial | partial | src/ui/ProEditorsStrips.inc:1 ('Fret Channel') | Fret Channel guitar strip with amp models and drive; no cabinet / mic placement. |
| 41 | Bass Amp Designer (models, DI box blend, signal flow) | Amps | 2 | partial | partial | partial | src/core/StripSuite.inc:777 ('{ "Bass drive", 0.0f }') | Fret Channel bass drive and drive mix; no bass cabinets or mic choice. |
| 42 | Pedalboard (stompbox browser, A / B router, mixer) | Amps | 3 | partial | partial | partial | src/ui/PluginArtEffects.inc:2601 ('Hardcore (guitar-rig role)') | Hardcore pedalboard with boxes on / off; no split routing. |
| 43 | Settings: General | Settings | 2 | yes | yes | yes | src/ui/SettingsPages.cpp:603 ('Hints and warnings') | General page. |
| 44 | Settings: Audio / MIDI (devices, buffer) | Settings | 4 | yes | yes | yes | src/ui/SettingsPages.cpp:852 ('Buffer size') | Audio and MIDI pages. |
| 45 | Settings: Metronome (sound, volume, when it clicks) | Settings | 2 | yes | yes | yes | src/ui/MainComponent.cpp:5109 ('Metronome volume') | Metronome menu items. |
| 46 | Settings: Loops (keep key, reset keywords) | Settings | 2 | partial | partial | partial | src/ui/SimpleDock.cpp:2573 ('Follow the project key') | Key follow per loop; no Loops page. |
| 47 | Settings: My Info (artist, composer, album for export) | Settings | 1 | partial | partial | partial | src/ui/SettingsDialog.cpp:277 ('Author') | Project author field; no default artist / album tags. |
| 48 | Settings: Advanced (show advanced tools) | Settings | 2 | yes | yes | yes | src/ui/SettingsPages.cpp:178 ('Layout style') | Layout style chooses the simple or full workflow. |
| 49 | Keyboard Command editor | Settings | 2 | yes | yes | yes | src/ui/MainComponent.cpp:9286 ('Manage shortcuts') | Shortcut manager. |
| 50 | Export Song to Disk dialog (AAC / MP3 / AIFF / WAV, quality) | Share | 5 | partial | partial | partial | src/ui/ExportDialog.cpp:429 ('"WAV", "MP3", "OGG", "FLAC"') | WAV, MP3, OGG, FLAC; no AAC (M4A) or AIFF. |
| 51 | Share menu (Music app, AirDrop, Mail, Burn CD, GarageBand for iOS) | Share | 2 | no | no | no | searched: Music app, AirDrop, Mail, burn, CD, share sheet | Apple-ecosystem share targets; LYNX shows the file instead. |
| 52 | Compress / Save project with its files | Share | 3 | yes | yes | yes | src/ui/MainComponent.cpp:9096 ('Project with samples (.zip)...') | Project with samples (.zip). |
| 53 | Clean Up Project (remove unused files) | Share | 2 | yes | yes | yes | src/ui/MainComponent.cpp:6974 ('Delete unused files and save project') | Same. |
| 54 | Logic Remote (control from a phone / tablet) | Remote | 2 | no | no | no | searched: Logic Remote, phone, tablet remote, OSC; LYNX Remote Control is a local loopback API | No companion remote app. |
| 55 | Learn to Play: lesson chooser and lesson window (video, notation, mix, slow down, How Did I Play) | Learn to Play | 2 | no | no | no | searched: lesson, Learn to Play, How Did I Play, teacher | No lessons. |
| 56 | Chord Trainer | Learn to Play | 1 | no | no | no | searched: chord trainer, practice chords | No trainer. |
| 57 | iPad: Song settings (tempo, signature, key, metronome / count-in, fade out) | iPad | 4 | partial | partial | partial | src/ui/MainComponent.cpp:6173 ('Tempo (BPM)'); src/ui/MainComponent.cpp:5629 ('Song time signature') | All but the automatic fade out. |
| 58 | iPad: Song sections editor (A / B / C sections) | iPad | 3 | partial | partial | partial | src/ui/PlaylistComponent.cpp:9506 ('Add marker here...'); src/ui/PlaylistComponent.cpp:8075 ('Duplicate time') | Markers and duplicate time; no section list that sets the song length. |
| 59 | iPad: Track controls (volume, pan, echo / reverb sends, compressor, EQ, gate) | iPad | 4 | partial | partial | partial | src/ui/SimpleDock.cpp:285 ('How much of the track goes to the shared reverb') | Simple dock reverb / echo sends and the effect chain; compressor / EQ / gate are chain effects, not one panel. |
| 60 | iPad: Mixer / track overview | iPad | 3 | yes | yes | yes | src/ui/MixerComponent.cpp:3920 ('Arm selected track(s)') | FL Mixer and Session mixer. |
| 61 | iPad: Keyboard Touch Instrument (scale, glissando, arpeggiator, sustain, chord strips) | iPad | 3 | partial | partial | partial | src/ui/TouchKeyboard.cpp:335 ('Scale keys'); src/ui/TouchKeyboard.cpp:335 ('Chord pad') | Scale keys and chord pads; no glissando / scroll strip or arpeggiator on the keyboard. |
| 62 | iPad: Drums Touch Instrument (kit pads) | iPad | 3 | yes | yes | yes | src/ui/SimpleDock.cpp:1612 ('class SimpleDock::PadsPage final') | Simple pads (16) with kits. |
| 63 | iPad: Smart Drums (instrument grid by complexity / loudness) | iPad | 2 | no | no | no | searched: Smart Drums, complexity, loudness grid | No such grid. |
| 64 | iPad: Smart Strings / Bass / Guitar (chord strips, autoplay, notes) | iPad | 2 | partial | partial | partial | src/ui/TouchKeyboard.cpp:860 ('Strum: the chord') | Chord pads with strum; no autoplay patterns or fret view. |
| 65 | iPad: Sampler (record, trim, shape, play chromatically) | iPad | 3 | partial | partial | partial | src/ui/PlaylistComponent.cpp:10753 ('Open in a new Sampler channel') | Sampler channel from a clip; no record-into-sampler screen. |
| 66 | iPad: Audio Recorder (voice effects: Robot, Telephone, Chipmunk...) | iPad | 2 | partial | partial | partial | src/core/VoiceShift.cpp:80 ('"Robot"') | Voice track plus Voice Shift presets; not one recorder screen. |
| 67 | iPad: Amp Touch Instrument (amp, stompboxes, tuner) | iPad | 2 | partial | partial | partial | src/core/ChainPresets.cpp:66 ('Crunch Stack') | Guitar chain presets; no amp picker screen. |
| 68 | iPad: Beat Sequencer (step grid, step velocity / repeat / chance) | iPad | 4 | partial | partial | partial | src/ui/ChannelRackComponent.cpp:2692 ('Graph editor') | Channel Rack steps with velocity graph; no per-step repeat, direction or rate. |
| 69 | iPad: Traditional instruments (Pipa, Erhu, Guzheng, Koto) | iPad | 1 | no | no | no | searched: Pipa, Erhu, Guzheng, Koto | None. |
| 70 | Live Loops grid (cells, columns, trigger, stop) | Live Loops | 4 | yes | yes | yes | src/ui/ClipGridComponent.cpp:186 ('Launch quantisation: when a launched clip starts') | Clip grid of the Clip-launch layout. |
| 71 | Live Loops cell editor (length, play mode, quantize start, time stretch) | Live Loops | 3 | yes | yes | yes | src/ui/DetailView.cpp:2030 ('Loop Length (bars. beats. sixteenths)') | Clip View. |
| 72 | Live Loops starter templates (genre grids) | Live Loops | 2 | no | no | no | searched: Live Loops template, starter grid, genre grid | No starter grids. |
| 73 | Remix FX (XY: filter, repeater, gater, wobble, reverse, scratch, downsampler, orbit) | Live Loops | 2 | partial | partial | partial | src/core/Effects.cpp:1284 ('Gross Beat') | Gross Beat and Pocket effects cover repeats / gates; no XY performance panel. |
| 74 | Jam Session (play with other users) | iPad | 1 | partial | partial | partial | src/ui/LiveSessionUi.cpp:66 ('Start live session...') | Online live sessions over the LYNX relay. |
| 75 | Open a GarageBand project (.band) in LYNX | Interop | 3 | no | yes | yes | src/core/BandImport.cpp:338 ('GarageBand: MIDI region') | r030: tracks, MIDI / audio regions, tempo, key, level, pan, mute; effects, Drummer regions, tempo changes and sections are listed in the report, not converted; GarageBand 6-11 refused. r030: .band import (190ic). |

## 6. What could not be verified

- **Source only.** No GUI, no shots this round. A "yes" means the screen and its function exist in code. It does not
  mean the look was compared:
  - 190jn's Live-look shots and 190jr's BandLab pairs are in their round folders;
  - this round did not re-compare them.
- **Drum Rack pad view.** Still waits on a Live check.
- **r030p is a local "branch wins" merge.** A few hunks may differ from the real merge.
- **GarageBand.** Screens are from Apple's guide topics and general knowledge. Not seen this round:
  - the iPad screens: Smart instruments, Remix FX, Live Loops templates;
  - Mac panes such as Configure Track Header;
  - so some partials could be finer.
- **The DirectWave editor.** Judged "yes" from its FL-form code. The 190gf row had no FL shot. One is due next session.
