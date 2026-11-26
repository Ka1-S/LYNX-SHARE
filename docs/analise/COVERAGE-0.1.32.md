# LYNX coverage for 0.1.32 (round 190rq, stage Q5, 2026-11-26): FL Studio 2026, Ableton Live 12, BandLab, FL Studio Mobile

Pictures, raw data and scripts: `/mnt/e/lynx-work/rea_cov_190rq/` (paths below such as `shots/` are relative to it).

0.1.32 is a **quality release** (feature freeze since 11-24). This round updates the 0.1.31 coverage
(`COVERAGE-0.1.31*.md`, round 190og). It doesn't redo it. Its output is a **gap list of fixes**, `GAPS-0.1.32.md`.

- **Base:** `origin/wip/rollup032` 87cd3c512 (step 4, contains 0.1.31.2).
- **LYNX pictures** come from the step-4 Linux build, run off screen in test mode on a private display.
- **References** were captured today on this PC:
  - FL Studio 2026 26.1.7: "Ookay - Thief", a new project, both built-in tutorials;
  - Ableton Live 12.4.16 Trial: an empty Set, the Info View, menus, the Learn View.
  - One GUI hold of 10.5 minutes, with the input wall and no dropped input.
  - FL's audio settings were not touched. Both programs were closed with "No" to saving.

## 1. What moved since 0.1.31

| Yardstick | 0.1.31 (190og) | **0.1.32 (r032)** | Note |
|---|---|---|---|
| Screens, all five programs (774) | 76.0 % | **76.3 %** | 4 rows moved: FL Patterns › Transpose... (190ri); GarageBand Arrangement track = Sections lane (190ol); BandLab no-input notice (a re-check); FLM Reset this page (190qx, partial). 0 citation failures, 0 regressions |
| FL screens (302) | 83.2 | **83.6** | |
| Live screens (185) | 64.0 | 64.0 | 12 gap texts updated |
| FLM screens (122) | 73.3 | **73.7** | |
| BandLab screens (89) | 84.9 | **85.6** | |
| GarageBand screens (76) | 72.4 | **73.0** | |
| Functions (7,327 rows) | 74.7 % all five; FL value ≥ 3: 93.8 % | not re-judged | the feature freeze adds no functions; the 0.1.31 numbers stand. 0.1.31's tail (190oh … 190qd), which landed after 190og measured, is LYNX-own work or already counted. A function re-judgement belongs to the 0.1.33 coverage round |
| Technology rows (51) | has 31 / ahead 7 / partial 9 / missing 4 | unchanged | no technology work in a quality release |

Details: `COVERAGE-0.1.32-SCREENS.md` § 1 and `COVERAGE-0.1.32-SCREENS-MOVED.md`.

## 2. Screens side by side (new in this round)

The inventory says *whether* a screen exists. The pairs say *how close* it is. 22 pairs were made (`shots/pairs/`), marked same /
missing detail / wrong / LYNX's own (`COVERAGE-0.1.32-SCREENS.md` §§ 2-5).

**Same:**
- the Playlist, the Channel rack (insert boxes), the Mixer insert menu (FL's order kept);
- the Track mode submenus;
- right-click deletes a clip, Esc closes the focused window;
- Live's double-click on an empty slot;
- the Session grid with numbered activators.

**Missing details** (all small):
- Playlist track menu: Show ghost notes, Consolidate track(s), Change color... as a dialog;
- Channel menu: MIDI channel through, Auto fill, Show waveform preview, and FL's name for the auto-sampler;
- no instrument-track or audio-track mark when a channel or insert is linked;
- Thief opens at bar 1 instead of FL's saved zoom and scroll;
- the Help menu has no Tutorials, Manual or What's new;
- Live's track title-bar menu: Insert Audio / MIDI / Return Track, Duplicate, Delete, Select Track Content;
- Live's empty-state texts;
- the Finish dock has no M4A.

**Wrong:**
- the Mixer's Ctrl+L / Ctrl+Shift+L have two names each (FL 2026 "Route…" in one menu, the old "Link…" in the key map, the Mixer menu and the site);
- the rack box hint promises "[RMB] next free insert" where right-click opens a menu;
- Help › First-song tour silently switches an FL user to the Simple layout.

**LYNX's own (fine):**
- the empty-workspace card;
- the richer hint bar, and the insert hint that lists its channels;
- the New project window;
- the Clip View's "Select a clip…".

## 3. Tutorials and built-in help (`COVERAGE-0.1.32-TUTORIALS.md`)

What the others teach:
- **FL 2026:**
  - the hint bar (a name plus a word);
  - F1 opens the manual at the focused window;
  - Help › Tutorials: a 9-step Step sequencer tutorial and an 11-step Loop starter tutorial. These are interactive: a dimmed overlay, a spotlight, an "n/9" card, and steps that move on when the user does them.
- **Live 12.4:**
  - the Info View (a paragraph per control);
  - the Learn View: video lessons by topic in 12.4;
  - the older text lessons still on disk: Recording Audio, Creating Beats, Playing Software Instruments, Audio I/O, Driver Error Compensation, MIDI controllers, Manual Mappings, File Manager / Collect / Pack / Project.
- **BandLab / FLM:** a welcome flow, video tutorials and a help centre.

LYNX's 36 Learn pages (each with a pt-BR twin and 13 more languages) were checked against the code:
- 9 wrong texts. The worst: `open-flp` / `open-als` / `open-mobile` promise the file opens in its own layout, but it never switches.
- 12 less-clear texts.
- No Help-menu link to the tutorials, manual or FAQ.
- No Pattern-layout guided lesson.
- File management is undocumented.

## 4. Details users notice (`COVERAGE-0.1.32-DETAILS.md`)

18 items checked from code against today's FL / Live observation and the 190rl quality study.

Already right:
- right-click deletes a clip;
- Esc closes the focused window;
- Space in the audio editor plays its own audio;
- the Browser never jumps to a plugin's preset folder;
- backups carry the date;
- the plugin monitor shows fractions of a percent;
- the scroll bar never zooms;
- the recovery box only appears after an unclean exit;
- detached plugin windows give the keys back.

Open:
- the selection after undo is kept by index, and notes / clips are re-sorted, so it can land on other notes or clips (needs a test);
- the "Route / Link" names;
- the missing link marks;
- the Clip-launch header menu;
- a docked plugin frame closing may keep keyboard focus (can't tell from code; needs a run).

## 5. "Playlist track 1 is not Mixer track 1" (queue item 92, `COVERAGE-0.1.32-CLARITY92.md`)

- **FL** shows the link in several places:
  - the channel's insert number box ("Target mixer track (double-click)");
  - "Assign to new instrument track";
  - Track mode › Audio track › (every insert by number and name);
  - the insert's waveform mark.
  - It never says that Playlist track N is not insert N.
- **Live** has no gap to explain: one track is one mixer channel, with "Audio To" on every track and an Info View paragraph on the activator.
- **LYNX already shows more than FL:**
  - the insert hint lists its channels;
  - linked Playlist headers say "(Audio track, linked to mixer track N)";
  - the Clip-launch track detail names the insert.
  - It also has its own opt-in answer, "Link Playlist tracks to mixer tracks", but that is hard to find.
- **The confusing case is the unassigned Playlist track,** which is most tracks in an FL project: nothing says where its sound goes.
- **Proposed 0.1.32 fixes,** with no routing change:
  - G-01, the header hint names where the track's clips play;
  - G-02 / G-03, Learn pages, How-to cards and the Mixer tip explain the route box;
  - G-04, the box hint names the insert;
  - G-05, the Link switch is findable from Track mode;
  - G-06, link marks.
  - All are text / visual extras that may ship ON with a switch (owner rule 11-23).

## 6. Top 15 gaps (full list with fixes, files and checks: `GAPS-0.1.32.md`)

| # | Id | Sev | Gap → fix | Rel |
|---|---|---|---|---|
| 1 | G-01 | high | Unassigned Playlist header hint says where its clips play ("plays Kick → insert 1 (KICK)") | 0.1.32 |
| 2 | G-10 | high | open-flp / open-als / open-mobile wrongly promise a layout switch: rewrite | 0.1.32 |
| 3 | G-11 | high | Help › First-song tour switches Pattern users to Simple silently: ask first, name it | 0.1.32 |
| 4 | G-02 | high | pattern-tour / mixer-routing explain the route box and "Playlist track ≠ insert" | 0.1.32 |
| 5 | G-03 | high | How-to cards + Mixer tip: one sentence on the route box and the Link switch | 0.1.32 |
| 6 | G-20 | high | Help menu: Tutorials, Manual, FAQ links in the app's language | 0.1.32 |
| 7 | G-12 | high | `?` text wrong in Clip-launch (Info View there); menu shows the real key | 0.1.32 |
| 8 | G-13 | med | One FL name per routing command ("Route selected channels to this track", "Route to free mixer track") | 0.1.32 |
| 9 | G-06 | med | Instrument-track / audio-track link marks on the channel button, header and insert | 0.1.32 |
| 10 | G-30 | med | Clip-launch track menu: Live's Insert Audio / MIDI / Return Track, Duplicate, Delete, Select Track Content, Assign Color to Clips | 0.1.32 |
| 11 | G-31 | med | Selection after undo by content, not index (test first) | 0.1.32 |
| 12 | G-04 | med | Rack box hint: insert name, "[RMB] menu" | 0.1.32 |
| 13 | G-21 | med | Help › What's new (link now, panel 0.1.33) | 0.1.32 |
| 14 | G-35 | med | Simple Finish dock: M4A row where AAC exists | 0.1.32 |
| 15 | G-22 | med | A guided lesson inside the Pattern layout (FL's Step sequencer tutorial) on the tour engine | 0.1.33 |

## 7. Owner decisions (no silent parking; details in GAPS.md)

- **D-1:** should opening a file switch the layout? Recommended: fix the texts now and offer the switch after an import in 0.1.33.
- **D-2:** F1. FL opens the focused window's manual page; LYNX opens the shortcut sheet. Recommended: keep the sheet and fix the text; add a switch in 0.1.33.
- **D-3:** the insert of a newly added channel. FL's new project shows "---" (Master); LYNX gives a free insert. Recommended: an REA check in FL first.
- **D-4:** Ctrl+N. Recommended: keep LYNX's.
- **D-5:** ask before the tour switches layout. **Taken as recommended** (reversible).
- **D-6:** the auto-sampler makes a DirectWave channel under FL's name. Recommended for 0.1.33.
- Carried from rollup s4: FL's "Show ghost notes" in the Playlist track menu (G-34, 0.1.33).

## 8. Not verified (plain list)

- Function rows (7,327) were not re-judged: the 0.1.31 numbers are carried.
- Every status and detail verdict comes from reading the source or from pictures. Nothing behavioural was played or measured.
- BandLab and FLM were not re-captured. Their pairs use earlier captures (190p, 190ia).
- Live 12.4's Learn View is a web panel in another process, so it couldn't be scrolled safely. Only its first page was seen.
- FL's Loop starter tutorial needs FL Cloud and was stopped at step 2/11.
- Live 13 (beta) watch list: unchanged since 190og. Live 13 isn't installed.
- "Can't tell" items that need a run:
  - keyboard focus after a docked plugin frame closes;
  - selection after undo;
  - FL's Auto fill / Show waveform preview;
  - FL's insert for a newly added channel (D-3).

## Files (`/mnt/e/lynx-work/rea_cov_190rq/`)

- **Documents:**
  - `COVERAGE-0.1.32.md` (this file)
  - `COVERAGE-0.1.32-SCREENS.md`
  - `GAPS-0.1.32.md`
  - `HANDOFF.md`
- **Notes** (`notes/`):
  - `CLARITY-92.md`
  - `TUTORIALS.md`
  - `DETAILS.md`
  - `SCREENS-FL.md`
- **Screens data** (`screens/`):
  - the five re-judged JSONs
  - `summary-r032.json`
  - `MOVED-r032.md`
- **Pictures** (`shots/`):
  - `ref/`: FL with the owner's name blurred, and Live
  - `lynx/<route>/`: 100 % and `@150`
  - `pairs/`
- **Raw:** `raw/` holds the unblurred FL shots. It stays local.
- **Scripts:** `s/`: the GUI toolkit (copied from 190ia), `lynxroutes.sh`, `recite.py`, `judge_r032.py`, `write_moved.py`, `pair.py`.
