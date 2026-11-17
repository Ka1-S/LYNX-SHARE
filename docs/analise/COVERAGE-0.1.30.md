# LYNX coverage for 0.1.30: FL Studio, FL Studio Mobile, Ableton Live, BandLab, GarageBand (round 190jw, 2026-11-16)

This round only measured and wrote. It changed no product code, built nothing, and ran neither LYNX nor the original
programs. The method and rows are the same as in rounds 190bk, 190dq, 190es and 190gf:
- one obligation row for each thing the original has;
- a status for each row (covered, partial, missing, unknown, excluded);
- a `file:line ('quoted string')` citation for every covered row;
- a script that re-opens every citation in the base it was judged on;
- REA's obligation ledger built over all rows.

**GarageBand is new.** It gets 225 rows, written from the topic lists of Apple's public *GarageBand User Guide* for Mac
and for iPad, plus round 190ic's `.band` findings. Every row was judged from scratch on all three bases.

**How to read the numbers:**
- Percent = (covered + half of partial) / rows not excluded.
- Strict = covered only. Unknown counts as not covered.
- The numbers mean "LYNX has a counterpart in source", not "LYNX behaves the same". Sound, speed and look are measured
  by other rounds (parity, perf, design) and are not scored here.

## The bases

| Column | What it is |
|---|---|
| **190gf** | The last measurement (2026-11-13): the simulated 0.1.28 integration. Shown for reference only |
| **0.1.29 = before** | Tag `v0.1.29-alpha`, as released. Its first judgement is in this round, so it also absorbs the corrections where 190gf was wrong (see §2) |
| **0.1.30 = after** | `origin/wip/rollup030` at **4c45364e4**, the 0.1.30 integration at its head today: 37 branch merges since 0.1.29 |
| **0.1.30 + pending** | rollup030 plus the finished, pushed branches not merged yet, merged locally at 21adb9556: capfix190jj, livefix190jd, polish190jp-je (which includes g1menus190je), playlist190jq, livelook190jn, bandlab190jr, hosting190jg, dawproj190jf, midiout190jl, parity190jh, parity190js, workflow190iu and perfgate190id. Where a merge conflicted, the branch side won, so a few hunks may differ from the real merge |
| left out | **fllook190jo**, still in progress. **wip/design190ig-s4**, an old slice off main (11-15) whose "branch wins" merge would have reverted newer rollup hunks. **dockpaint190jy** and **overlay2190jx** had no HANDOFF and were still running |

## 1. Coverage per program

### 1.1 The headline table

| Program | Rows | Covered % 190gf → **0.1.29** → **0.1.30** → +pending | Strict % 0.1.29 → 0.1.30 → +pending |
|---|---:|---|---|
| FL Studio 2026 (17 areas) | 3,581 | 66.8 → **67.4** → **67.7** → 68.1 | 61.9 → 62.1 → 62.6 |
| Ableton Live 12 (14 areas) | 1,068 | 68.9 → **69.9** → **71.7** → 72.1 | 60.6 → 63.0 → 63.5 |
| Live natives (devices, technologies) | 121 | 60.7 → **61.6** → **63.2** → 63.2 | 38.0 → 40.5 → 40.5 |
| *Live with its natives* | *1,189* | *68.1 → 69.0 → 70.8 → 71.2* | *58.3 → 60.7 → 61.1* |
| FL Studio Mobile (9 areas) | 282 | 83.2 → **83.2** → **83.2** → 83.2 | 75.9 → 75.9 → 75.9 |
| BandLab (15 areas) | 289 | 90.0 → **90.0** → **90.0** → 90.3 | 82.7 → 82.7 → 83.4 |
| **GarageBand (14 areas, new)** | 225 | – → **65.1** → **66.9** → 67.3 | 52.4 → 53.3 → 54.2 |
| **The four from before** | 5,341 | 69.2 → **69.8** → **70.4** → 70.8 | 62.9 → 63.6 → 64.1 |
| **All five programs** | 5,566 | – → **69.6** → **70.2** → 70.6 | 62.5 → 63.2 → 63.7 |

| Program | Value-weighted 0.1.29 → 0.1.30 → +pending | Rows of value ≥ 3 | Covered % of those 0.1.29 → 0.1.30 → +pending |
|---|---|---:|---|
| FL | 76.3 → 76.5 → 76.8 | 1,409 | 90.0 → **90.2** → 90.6 |
| Live | 78.6 → 80.5 → 80.8 | 725 | 84.8 → **87.0** → 87.2 |
| Live natives | 69.2 → 71.1 → 71.1 | 78 | 77.6 → **80.1** → 80.1 |
| FLM | 89.5 → 89.5 → 89.5 | 206 | 92.7 → 92.7 → 92.7 |
| BandLab | 92.4 → 92.4 → 92.8 | 191 | 94.8 → 94.8 → 95.3 |
| GarageBand | 76.4 → 77.9 → 78.5 | 131 | 85.9 → **86.6** → 87.4 |
| All five | 78.5 → 79.1 → 79.4 | 2,740 | 88.6 → **89.4** → 89.7 |

**GarageBand without the Apple-only rows.** 24 rows are Learn to Play (13) or Apple-ecosystem features: the Music app,
AirDrop, Mail, iCloud, Schoolwork, Apple TV, Touch Bar, Force Touch, CD burning and Jam sessions. Without them
GarageBand is 72.1 → 74.1 → 74.6 % (201 rows), and value-weighted it is 79.5 → 81.0 → 81.6. The rows stay counted in the
table above, as the method requires (see owner decision D1).

**What the yardsticks say:**
- **FL value ≥ 3 passes 90 %** on the 0.1.30 rollup: 90.2, or 90.6 with the pending branches.
- **Live gained the most:**
  - +1.8 points raw from the rollup alone, and +2.2 on value ≥ 3;
  - the browser (190iz), the Detail View and racks (190hr), warp modes (190ik), MPE (190hy) and Wavetable / Helix (190ja).
- **FLM did not move.** No 0.1.30 branch touched the `.flm` code or the FLM rows. The FLM residue is queue item 14,
  round 190jv, which was not finished when this was measured.
- **BandLab stays above 90 %.** Only 190jr (not merged) moves it, with 2 rows.
- **GarageBand starts at 65 / 67 %.** The Simple layout, the Clip-launch grid and the 190ic `.band` import already
  carry most of it. The holes are clear: Drummer, Learn to Play, Amp / Bass Amp Designer, the Score editor, AAC/AIFF
  export, and Loop Browser keywords.

### 1.2 Screens (windows, dialogs, panels, menus)

These come from 190gf's per-program screen inventories, re-judged on the three bases, plus a new GarageBand inventory of
76 screens. Percent = (yes + half of partial) / screens not n/a. Every yes / partial citation was re-opened in its base:
0 failures. Details are in `COVERAGE-0.1.30-SCREENS.md` (next to this file).

| Program | Screens | 190gf | 0.1.29 | 0.1.30 | + pending | Moved (by round) |
|---|---:|---:|---:|---:|---:|---|
| FL Studio | 302 | 82.4 | 82.4 | 82.5 | 82.5 | DirectWave in FL's layout (190hz) |
| Live | 185 | 56.7 | 57.5 | **60.1** | 60.9 | r030: Browser, Browser Places, Control bar, MPE tab, clip Scale, Quantize Settings, Wavetable as Helix. +pending: Info View, Status bar, Stem dialog (livelook190jn, bandlab190jr). Link toggle and Grain Delay corrected (already in 0.1.28) |
| FLM | 122 | 72.9 | 72.9 | 72.9 | 72.9 | none |
| BandLab | 89 | 81.5 | 81.5 | 81.5 | 82.9 | Region loop menu, Automation mode (190jr) |
| GarageBand (new) | 76 | – | 67.8 | 69.1 | 69.1 | Open a `.band` project (190ic) |
| **All five** | 774 | – | **73.2** | **74.0** | 74.4 | |

Top screen gaps:
1. Live's Arrangement view look. O29 says it stays an option for later.
2. The export dialogs: FL's Mastering section, and M4A / AAC export for BandLab and GarageBand.
3. Live's Export dialog.
4. Live's Clip View with warp markers.
5. Live's Sample box switches (Follow / RAM / HiQ).

Next come the Drum Rack pads (they need a Live check), Simpler, BandLab's Drum Machine dock and GarageBand's Loop
Browser, Drummer Editor, Score Editor and Amp Designer.

### 1.3 Technology

190gf's technology map (performance P1-P12, engine T1-T23, formats) was re-judged, and interop and GarageBand rows
were added (47 rows). Details are in `COVERAGE-0.1.30-TECH.md` (next to this file).

| Base | has | ahead | partial | missing |
|---|---:|---:|---:|---:|
| 0.1.29 | 18 | 5 | 16 | 8 |
| 0.1.30 | 20 | 5 | 15 | 7 |
| + pending | 25 | 6 | 11 | 5 |

What moved:
- **Corrections, already in 0.1.28:**
  - render-ahead (P2, now *ahead* of FL and Live);
  - one-pass stems (P9);
  - Ableton Link (T13).
- **0.1.30 rollup:**
  - every Live warp mode on its own engine (T7, 190ik);
  - `.band` import (F5, partial, 190ic);
  - `.dwp` / `.gig` / NCW formats (F6).
- **Pending branches only.** These ship in 0.1.30 only if the branches are merged:
  - grouped plugin processes (P8, now *ahead*), sample-accurate automation into hosted plugins (P10) and the CLAP
    extensions (T4), all from hosting190jg;
  - stem options (T9) and `.blx` (F4), from bandlab190jr;
  - DAWproject (F3), from dawproj190jf;
  - hardware MIDI out (F7), from midiout190jl.

Biggest technology gaps left:
1. GPU drawing on Linux and macOS (P6). Windows has Direct2D and the 190ip overlay.
2. "Safe overloads" (P11): keep the UI responsive when the audio thread overruns.
3. M4A / AAC export (F9, O31).
4. A virtual drummer (G1, D3).
5. Converting the rest of a `.band` project (effects, Drummer regions, tempo changes) and Apple Loops key / keyword tags
   (F5 / F8).

## 2. What moved: 133 rows in the four older programs, all up except one

| Step | FL | Live | Live natives | FLM | BandLab | Total |
|---|---:|---:|---:|---:|---:|---:|
| 190gf → 0.1.29: corrections, code already in 0.1.28 / 0.1.29 that 190gf missed | 36 up, 1 side | 15 | 1 | 0 | 0 | 53 |
| 0.1.29 → 0.1.30 (rollup030) | 11 | 33 | 4 | 0 | 0 | 48 |
| 0.1.30 → + pending branches | 24 up, 1 down | 5 | 0 | 0 | 2 | 32 |

GarageBand, which is new, moved 8 rows on the rollup (190ic `.band` import) and 2 more with 190jr.

**0.1.30 rollup, by round:**
- **190hr liverack** (Live 13): the Detail View residue, Follow Action layout, clip grid menus, rack Group / Ungroup and
  `.adg` racks with chains, the Drum Rack pad sample.
- **190iz browser** (Live 11, FL 6):
  - Live: the Library categories (Sounds, Drums, Samples...), the User Library place, the tag filter with Any / All;
  - FL: the six browser tabs, Send to selected channel / focused plugin, tags, Full sample preview, type to jump (partial).
- **190ik warp** (Live 7, natives 2): Complex Pro, Formants, Envelope, Flux and Texture rendered, and warped clips that
  follow tempo automation (#187).
- **190hy mpe** (Live 2 + natives 1): per-note MPE curves in the Detail View.
- **190ja wavetable** (natives 1): Wavetable becomes partial. Helix, LYNX's own wavetable synth, has Wavetable's
  parameters, matrix and `.als` in / out.
- **190im details** (Live 2): Follow pausing after an edit, the session scene item.
- **190in design** (FL 2): FL's channel wrapper menu (rename / colour / icon) and Make editor thumbnail.
- **190ib** (FL 1): Preview notes while playing (#183).
- **190ic band** (GarageBand 8): `.band` open, patch names, and effects / Drummer / sections / tempo changes listed.

**Pending branches (+pending column):**
- **190jr bandlab** (FL 9, BandLab 2, GarageBand 2): FL's Extract stems form (stem ticks, Limit CPU, When done, Safe
  mode, mixer tracks), the Simple region loop handle and the Simple automation switch.
- **190jp polish** (FL 8): the Theme Adjustments knobs and colour dots, and channel name alignment.
- **190jf dawproj** (FL 2, Live 2): Hide collapsed grouped tracks, nested groups, drag into a group.
- **190je g1menus** (FL 3): the grid line intensity slider (partial).
- **190jl midiout** (FL 2): the MIDI output Port wheel, Stop sound.
- **190jd livefix** (FL 1, Live 2): the Release Velocity lane, stacked MPE lanes, Snap to pitch period (Slicex only).
- **190jq playlist** (Live 1): header width. **190iu workflow** (Live 1): drag a MIDI file onto a track.

**Corrections (190gf was wrong; the code was already in 0.1.28 or 0.1.29):**
- **Ableton Link** (190gl): 10 Live rows and `tech-0009` are covered. Link goes 29.1 → 48.8 %.
- **Sampler waveform menu** (7 FL rows): `SamplerPage.h` is the same as in 0.1.28.
- **Slicex articulator flags** (190hg, 6 rows).
- **Picker panel › Enable, Pitch-correct / Time-warp sample, Crossfade with** (190he / 190gw, 10 rows).
- **Playlist track Size 33–200 %** (190ft, 5 rows).
- **Scaling 100–400 %** (10 rows, missing → partial): it exists on native windows only.

**The one regression:**
- **What:** `rack-0114` "Name alignment › Left" (value 1) goes from partial to missing on +pending.
- **Why:** 190jp made channel names always centred, which is FL's default, so Left can no longer be picked.
- **Fix:** an alignment option (S). Listed for the integration.

**Where the judges disagreed with a round's claim:**

| Claim | Judged | Why |
|---|---|---|
| 190ih: Gross Beat follows a MIDI-picked slot | **partial** (`natives-E060`) | The grid now shows the playing slot filled and the queued one outlined, but the selected (edited) slot does not follow. One FL check is listed |
| 190ib: type to filter in the browser | **partial** (`browser-0011`) | Typing jumps to a row. FL filters the list |
| 190hr: racks as `.adg` | **partial** (`racks-068`) | Hosted plugins and built-ins without a Live device are left out of the file |
| 190ik: Transpose ±48 | **partial** (`detail-072`) | Stored, shown and saved; Beats, Complex Pro and Texture render it, but most real-time modes still stop at ±24 |
| 190jr: Splitter models | **partial** (`online-001`) | Per-stem ticks and 4/6-stem models; no lead/backing vocal split or per-drum split |

**Checks:**
- Every covered row was re-opened by script on its base, with 0 failures: 0.1.29 3,362 PASS, 0.1.30 3,399 PASS,
  +pending 3,425 PASS (`notes/verify-*.json`).
- Of the 3,331 citations covered at 190gf, 4 had moved (1 FL, 2 Live, 1 BandLab). All were re-cited and none was lost.
- `tools/check_out5.py` gives 0 errors for all 11 clusters (2,239 rows re-judged).
- REA `build-reconstruction-obligation-ledger` ran on all 7,102 rows of the four older programs for each base.
  GarageBand has no REA evidence bundle, because its binary is not on this PC. Verified stays 0, the same limit as before
  (no behaviour comparison):

| Ledger | Implemented | Unowned | Out of scope | Unknown |
|---|---:|---:|---:|---:|
| 0.1.29 | 3,362 | 1,979 | 1,761 | 5 |
| 0.1.30 | 3,399 | 1,942 | 1,761 | 5 |
| 0.1.30 + pending | 3,425 | 1,916 | 1,761 | 5 |

## 3. Per area (0.1.30 + pending), lowest first

Open points = 1 per missing / unknown row + 0.5 per partial. The full table is in `notes/agg-final.txt`.

| Program | Lowest areas, covered % 0.1.29 → 0.1.30 (+pending) | Open points all / value ≥ 3 |
|---|---|---|
| FL | Piano roll 52.7 → 52.9, Export 53.4, Settings 54.3 → 55.9 (p), Browser 51.9 → **58.7**, Event editor 60.0, Main menu 66.3 → 67.0 (p), Wheel menu 66.9 | 1,143 / 133 |
| Live | Link 48.8, Groove pool 51.6 → 52.4, Device view 60.0 → 61.0, Detail View 57.7 → **62.4** (63.4 p), Browser 63.1 → **68.8**, Session 69.8 → **72.0** | 298.5 / 93 |
| Live natives | MIDI effects 55.6, instruments 53.3 → 56.7, audio effects 60.8, technologies 66.3 → **69.6** | 44.5 / 15.5 |
| FLM | Instruments 75.0, .flm fields 78.8, App features 81.0, Piano roll 83.3 | 47.5 / 15 |
| BandLab | Online 50.0, Looper 56.2, Templates 70.0, Tuner / metronome 76.7, Import / export 79.2 | 28 / 9 |
| GarageBand | Learn to Play 0, file interop 16.7 → **61.1**, Drummer 33.3, Amps + pedals 38.2, Share + settings 50.0, Touch instruments 60.7, Editors 73.7 | 73.5 / 16.5 |

**Path to ~90 % raw (points still needed on +pending):**
- BandLab: 0, it is there.
- FLM: 19.3. Queue item 14 (round 190jv) holds most of it, and an FLM-in-FL session would settle its 12 unknown rows.
- GarageBand: 51.0. It drops to about 30 without the 24 Apple-only rows.
- Live natives: 32.4.
- Live: 191.7.
- FL: 784.9, nearly all value 1-2 menu and dialog tails. FL value ≥ 3 is already over 90 %.

## 4. The biggest remaining gaps, ranked by user value

Every row still open on +pending with value 5 or 4 is listed in `notes/uncovered-v45.txt` (64 rows). Grouped into
work items, highest value and broadest reach first:

| # | Gap (programs) | Value | Status | Effort | Holder |
|---|---|---:|---|---|---|
| 1 | **Live's .als read and write residue** (Live): Meld, Corpus, Spectral, Erosion, Vinyl, Looper and MIDI-effect racks dropped on read; most LYNX devices not written (`tech-0039`, `tech-0040`, `tech-0030`) | 5 | partial | L | O17 (exact maths only) |
| 2 | **Clip launching residue** (Live, GarageBand Live Loops): no crossfader, no MIDI / key mapping of slots, scenes and track stop, no movable session ring on a control surface (`tech-0010`, `session-085`, `mapping-0059`) | 5 / 4 | partial / missing | M | no queue line |
| 3 | **Warp time line** (Live): clip start / end and loop brace set in beats on the warped time line (`tech-0001`, `tempo_warp-0062`); real-time Transpose beyond ±24 (`detail-072`) | 5 / 4 | partial | M | no queue line |
| 4 | **Loop Browser keywords and filters** (GarageBand, Live Browser): instrument / genre / mood keyword buttons, filter by loop type, Live's filter groups, Sounds by sound type, Drums as kits, a loop preview that follows the project key (`gb-loops_media-001/002/003`, `browser-0011/0012/0043/0044`) | 5 / 4 | partial | M | **D2** (shipped tags or content) |
| 5 | **Song export to AAC (M4A) and AIFF** (GarageBand, BandLab) (`gb-share_settings-005`) | 5 | partial | S | O31 / BUILD-LIST S5 (M4A); AIFF has no line |
| 6 | **FL natives still stand-ins or partial**: Gross Beat's grid follow (`natives-E060`, S), Edison as effect, Newtone, Harmor (L), FLEX (O13 keeps the stand-in) | 5 / 4 | partial | S–L | BUILD-LIST 3a, S3 |
| 7 | **FLM audio stretch ratio, Pitcher and Pitcher 2, the FLM Rack generator in FLM-exported .flp** (`audiotracks-0003`, `effects-0018/0033`, `instruments-0022`) | 5 / 4 | partial | M | QUEUE-0.1.30 #14 (190jv) |
| 8 | **Live device panels in the chain**: every effect's own panel inside the Device View, not just racks and Helix (`detail-089`); Configure mode for plugins (`deviceview-0046`); device Cut / Copy / Paste (`deviceview-0012`, S) | 4 | partial | M | O29 (Live-style panels as an option) |
| 9 | **Groove pool engine**: Commit Groove, Extract Groove, a shipped groove library (`groove-020/021/039`, `tech-0016`) | 4 | partial | M–L | no queue line |
| 10 | **Control surfaces**: device profiles with lights, motor faders and displays (FL controller scripts, Live's ~150 scripts), the selected device on the encoders in banks (`midilink-X003`, `mapping-0053/0056`) | 4 | partial | L | QUEUE-0.1.30 #12 |
| 11 | **A virtual drummer** (GarageBand Drummer, BandLab-style starters): a track that plays a part from genre, drummer, complexity / loudness, fills and swing, and follows another track's kick and snare (`gb-drummer-001`, `gb-projects_tracks-016`) | 4 | partial | L | **D3** |
| 12 | **Live Detail View Envelopes tab** (clip envelopes and modulation, `detail-033`, `tech-0032`) | 4 | partial | L | BUILD-LIST #6, no queue line |
| 13 | **BandLab Voice Cleaner and the rest of Splitter** (`online-003`, `online-001`) | 4 | partial | M | no queue line |
| 14 | **Smart Controls**: a curated macro panel per sound (`gb-mix_smart_controls-005`) | 4 | partial | M | **D4** |
| 15 | **Technology**: GPU drawing on Linux / macOS (P6), safe overloads (P11) | (perf) | partial | M–L | perf rounds |
| 16 | **Small GarageBand / Live items**: tuner in the toolbar display, song fade-out, Beat Sequencer per-step repeat, drag a whole arrangement section with its regions, fold a Live track, Live take lanes for MIDI, Session mixer In/Out | 4 | partial | S–M | **D5** for the Simple-only ones |

## 5. Gaps that need an owner decision

The reversible ones take the recommended option unless the owner objects, as the common rules say. None is parked.

| # | Question | Recommended |
|---|---|---|
| **D1 new** | GarageBand's 24 Apple-only rows (Learn to Play, Music app, AirDrop, Mail, iCloud, Schoolwork, Apple TV, Touch Bar, Force Touch, CD burn, Jam): keep counting them, or exclude them like O19 did for FL's hidden items? | Keep them counted, as now, and also report the figure without them (74.6 % on +pending). LYNX will not build Apple-ecosystem features; Learn to Play is out of scope |
| **D2 new** | Loop Browser keywords (GarageBand) and Live's filter groups / Sounds / Drums categories need tagged content. LYNX ships little loop content, and Apple Loops / Live packs cannot be shipped | Tag the factory content LYNX already ships (Loop Starter, Simple pads, presets) with instrument / genre / mood, and add a loop-type filter (S–M). No third-party content |
| **D3 new** | A virtual drummer (GarageBand Drummer) is a LYNX-own engine, not FL parity | Yes, later, in the Simple layout only: build it on Loop Starter (genre patterns), with a complexity × loudness pad, fills and swing (L). FL and Clip-launch layouts unchanged |
| **D4 new** | GarageBand Smart Controls: a curated macro panel per built-in sound | Yes, in the Simple layout's dock only: reuse rack macros, with 4-8 named knobs per factory preset (M) |
| **D5 new** | GarageBand / BandLab small extras: a tuner in the Simple toolbar display, a song-level automatic fade-out, AIFF export | Yes, all three (S each). The tuner and fade-out go in the Simple layout only; AIFF goes in the export dialog for every layout (JUCE already writes AIFF) |
| **D6 new** | GarageBand Score Editor (notation view and printing) | Not now. It is a large L item that no other reference program needs |
| O17 (carried) | `.als` export of Live devices (`tech-0040`) | Exact maths only (Utility, EQ Eight first). Helix → Wavetable is done (190ja) |
| O13 (carried) | FLEX stand-in | Keep the stand-in |
| O15 (carried) | Own 4-operator FM engine so Operator sets open | **Done**: Tetra (190ix / fm190hs) is merged in rollup030. `instruments-0005` stays partial, because pan by key / random, envelope time by key and LFO-by-key are dropped |
| O8 (carried) | BandLab sampled soundbanks | Still no sampled content; `instruments-018` stays partial |
| O19 (carried) | FL 2026-hidden menu items | Still counted |
| O25-O31 (carried) | FLM generator via the rack importer, Ctrl+B in Clip-launch, Freeze with inserts in the Pattern layout (no), Playlist per-clip warp copy, Live-style device panels as an option, FLM effect panels, M4A | Unchanged from 190gf; O31 (M4A) also closes most of gap 5 |
| rack-0114 | Channel name alignment Left became unreachable in 190jp | Not an owner question: add FL's Name alignment option back (S), FL-exact |

## 6. Live checks: 98 rows flagged (`notes/live-checks.txt`)

FL 16, Live 44, Live natives 13, FLM 22, BandLab 1, GarageBand 2. New in this round:
- **Live 12.4:**
  - the Link toggle text with no peer, its phase bar, a quantum below one beat, and a LYNX ↔ Live Link session;
  - the Follow Action layout in the Launch panel, and the grid menu order with its "Off" item;
  - an `.adg` holding a plugin;
  - a LYNX-exported `.als` with a Drum Rack, a GroovePool and warp settings, opened in Live 12.4 and Live 11.
- **FL:**
  - when a MIDI note picks a Gross Beat slot, does the grid's selected slot follow?
  - the 2-3-region Edison `tlst` layout (carried);
  - `mixer-0037/0038` with D2's take (carried).
- **LYNX hardware:** MIDI Out on Port 1 drives a real device, and Ctrl+H silences it (190jl).
- **GarageBand:** a real GarageBand 10 Mac project with tempo changes, markers, take folders and Drummer regions; the
  app-side `.band` open was never run.
- **FLM:** all carried; it needs an FLM-in-FL session.

## 7. Not verified (plain list)

- Nothing was run. Every status comes from reading source, and REA's ledger has 0 verified rows.
- +pending is a local merge in which "branch wins" settled the conflicts (mostly the i18n tables, `tests.cpp`,
  `PlaylistComponent.cpp`, `MainComponent.cpp`). The real merges may differ in those hunks.
- **Who judged what:**
  - Opus judged flrack, flpl, fledison, live1, live2, flm + bandlab and garageband.
  - Sonnet judged flctl, flmenu and flpr, diff-first. Families whose files did not change kept their 190gf status.
  - The flpr judge changed only 1 row; the partial and missing rows there keep 190gf's line numbers.
- The GarageBand rows come from Apple's guide topics, not from REA on a binary, because GarageBand does not run on this
  PC. One GarageBand judgement to watch: "lock track" counts as covered by Freeze (GarageBand's lock also renders the
  track).
- 16 FL, 33 Live and 12 FLM rows are still unknown, and they count as not covered.

## Files (all in `/mnt/e/lynx-work/coverage190jw/`)

- `COVERAGE-0.1.30.md` (this file), `AGENT-BRIEF.md`, `HANDOFF.md`.
- `screens/` (SCREENS-0.1.30.md, per-program inventories) and `tech/` (TECH-0.1.30.md).
- `ledger-rows-190jw.json`: every row with its 190gf / 0.1.29 / 0.1.30 / +pending status, evidence, changed_by and note.
- `ev/ledger-in-*.json`, `ev/ledger-out-*.json`, `ledger-summary-*.json`: REA's input and output.
- `in/<cluster>.json`, `out/rejudge-<cluster>.json` / `.md` for 11 clusters; `in/garageband-rows.json`, the new rows.
- `notes/`: `agg-final.txt`, `summary.json`, `moved.txt`, `uncovered-v45.txt`, `live-checks.txt`, `verify-*.json`,
  `commits.txt`, `changed-*.txt`, `integration-merges.txt`.
- `tools/`: `agg5.py`, `verify5.py`, `mkin5.py`, `mkgb.py`, `check_out5.py`, `report5.py`, `ledger5.py`.
- Snapshots (temporary, in the session scratch folder): `jw-028`, `jw-029`, `jw-r030`, `jw-r030p`. Recreate them with
  `git archive` of the tag or rollup; the +pending one by merging the branches above onto rollup030 with `-X theirs`
  (`notes/integration-merges.txt`).
