# Round 190rq: tutorials and built-in help coverage (0.1.32 quality release)

Read-only study. Code: `origin/wip/rollup032` @ `87cd3c512` (worktree `/home/kauan/projetos/lynx-wt/rollup032`).
Sources:
- **Live 12**: the built-in lessons in `C:\ProgramData\Ableton\Live 12 Trial\Resources\Builtin Lessons\` (EN files).
- **FL 2026**: the online manual pages `menu_help`, `toolbar_panels`, `channelrack`, `mixer_iorouting`, `basics_workflow`, `basics_new` and `menu_file`, plus the local `Help\HelpAliases.scr`. The local `Help\html` holds only an "install the offline help" stub, so there is no offline manual.
- **BandLab and FLM**: the earlier studies `bandlab182d/REPORT.md`, `bandlab190p/STUDY.md` and `flm182c/dll-strings.txt`.

Every lesson below is summarised in my own words. No vendor text is copied.

Paths:
- `src/...` means `/home/kauan/projetos/lynx-wt/rollup032/src/...`.
- `learn/x.md:n` means `website/_src/learn/x.md` line n. That file is the source of `website/learn/x/index.html`, and each EN page has a pt-BR twin and 13 other languages under `website/_src/learn/<lang>/`.

**Twin check:**
- All 36 EN Learn pages have a pt-BR twin, and also de, es, fr, id, it, ja, ko, nl, pl, ru, tr, zh-Hans and zh-Hant: 36 of 36 each.
- In every page, the pt-BR copy has the same number of headings and steps as the EN copy, so no translation is structurally stale.
- **Any text fix below must be made in all 15 language files of that page.**

---

## 1. Lesson / help topic → LYNX equivalent

Status key:
- **same**: LYNX covers it.
- **detail**: LYNX misses a detail (named in the row).
- **WRONG**: the LYNX text is wrong.
- **missing**: no LYNX lesson, hint or help text.
- **n/a**: LYNX lacks the feature, so it is a 0.1.33 feature item, not a text fix.

### 1a. Ableton Live 12 built-in lessons (Help View)

| Lesson (Live) | What it teaches (summary) | LYNX equivalent | Status |
|---|---|---|---|
| Live 12 Lessons (TOC) | A home page of picture tiles for each lesson, a link to the manual and to the website, and "show all lessons" | `Help › How to use...` (11 window guides, `src/ui/MainComponent.cpp:12271-12276`); `website/learn/index.html` (36 tutorials) | **detail**: the app has no "all tutorials" or "online manual" entry. The Help menu never links lynx-share.com/learn or /wiki (`MainComponent.cpp:12263-12330`), see T-6 |
| What's New in Live 12 (40 pages) | One short page per new feature of the version (Stem separation, Bounce, Device A/B, Auto Shift, Roar, MIDI tools, Keyboard navigation...) | The update bar's `What's new` (`src/ui/Updater.cpp:1209,1458`), but only *before* an update; website `/updates` | **missing** after updating: no `Help › What's new` (FL has one too), see T-9 |
| Recording Audio (14 pages) | Arrangement as a tape recorder; mixer In/Out section; input type and channel choosers; Arm and the input meter; Monitor modes (Off / Auto / In) and echo or feedback with direct monitoring; Stop twice to go to the start; Arrangement Record; zoom and scroll with the ruler; undo a take; metronome and cue volume; count-in and Tap; a new track (Ctrl+T) and multi-arm (Ctrl+click); punch in/out with the loop brace; loop recording that keeps every pass | `learn/record-voice.md` (Simple only); wiki `recording.md` (insert input box, Recording window, loop recording and takes) | **detail**: there is no Pattern or Clip-launch audio-recording lesson (insert input box, arm, monitoring, count-in, punch, takes); see T-12 (0.1.33 lesson) and T-11 (0.1.32 paragraph) |
| Creating Beats (10 pages) | Load a drum-kit preset from the browser onto a MIDI track; play it with the computer keys; double-click a slot for a clip; record quantization; overdub with Session Record; undo the last loop pass; Draw mode (B) and the grid from the right-click; velocity editor (one sound's velocities); clip length; duplicate | `learn/simple-beat.md` (Drum Machine, Length, Swing, keys per row); `learn/pattern-tour.md` (steps, Piano roll) | **detail**: no Learn text on velocity (the Channel rack graph editor or the Piano roll lane), the pattern length, or playing drums from the typing keyboard in the Pattern layout (low) |
| Playing Software Instruments (13 pages) | Browser labels and arrow-key navigation; drag a preset onto a track; Device View; arming; computer MIDI keyboard (rows, Z/X octave, C/V velocity); Save Preset; Hot-swap (Q) and search; external plugins after enabling plugin folders; metronome; record MIDI into the arrangement; open and edit the clip | `learn/midi-keyboard.md`, `learn/shortcuts-keys.md` (typing keyboard), `learn/vst-plugins.md`, `learn/lynx-instruments.md`, `learn/device-view.md` | **detail**: no Learn text on saving or recalling a preset, or on the typing keyboard's octave and velocity (they are in `Typing keyboard settings`, `MainComponent.cpp:12123,12193`) |
| Max for Live Devices | (noted only: M4L device tours) | none | **n/a** |
| Setting up Audio I/O (Pc, 24 pages) | Driver type (ASIO vs MME/DirectX); input and output device (same device advised); Input/Output Config (mono or stereo pairs, naming channels, switch off unused ones for CPU); sample rate; find the lowest clean buffer with a continuous test tone plus a CPU-load simulator; input buffer via monitoring; ASIO control panel | `learn/audio-setup.md` (driver, output device, buffer, test sound) | **detail**: the page never names `Input device` or `Sample rate` (both on the page, `src/ui/SettingsPages.cpp:1063-1064`), nor LYNX ASIO (built in) or the two Windows Audio modes (`SettingsPages.cpp:1644-1679`). See T-10 |
| Driver Error Compensation (6 pages) | Measure the interface's real round-trip delay with a loop cable, enter it as an offset, check it, and why it matters only without software monitoring | Settings › Audio › `Recording latency compensation` with `Measure the round trip` (8 automatic clicks, `SettingsPages.cpp:1712-1716,1619`), described in wiki `audio-setup.md:50` | **missing in Learn**: LYNX's way is better (automatic), but no Learn page or in-app tip mentions it. See T-10 |
| Connecting MIDI Controllers (10 pages) | Keyboard: the port's Track switch and the MIDI-in indicator, a MIDI track, arm. Control surface: pick it among the native scripts, input and output ports, preset Dump, the Remote switch for manual mappings, the in/out indicators, lock a surface to a device | `learn/midi-keyboard.md` (Input devices, channel, plays the selected channel); wiki `midi-controllers.md` (Controller profiles, MPE, MIDI sync) | **detail**: the Learn page never mentions controller profiles or that knobs can be linked (wiki only) |
| Control Surface Reference | Per-controller pages for about 45 surfaces (what each knob or pad does) | `docs/CONTROLLERS.md`, wiki `midi-controllers.md` § Controller profiles | same in spirit; nothing to fix |
| Locating Missing Samples | File Manager: repair by hand (drop a file on the missing entry, any file allowed) or Automatic Search (folder / project / library), candidates, Show Candidates | `Help › Missing files...` window (Search folder, Locate, Skip; wiki `importing-projects.md:46`); Settings › Files `Search folders for missing samples` | **detail**: Learn mentions it only as one line in `open-flp.md:28`. Nothing says it also works for a `.lynx` project moved to another computer (low) |
| Collecting External Samples | Samples outside the project folder are "external"; collect them per category (library / other projects / elsewhere) with Collect and Save; mind the size; licensed content | `File › Collect all and save`, `File › Always collect and save` (`MainComponent.cpp:11891-11896`), `Tools › Collect project samples into a folder...` (`:11820`) | **missing**: none of these appears in any Learn **or wiki** page. See T-7 |
| Understanding Limitations | Why an edition or add-on blocks an action (trial, buy, work without saving) | LYNX is free with no editions | **n/a** |
| Packing Projects | A Pack = the project folder in one lossless-compressed file; collect externals first; unpack by double-click or drag | `File › Export › Zipped loop package...` (every sample inside; it opens again with `File › Open`), wiki `exporting.md:82`, `file-formats.md:22` | **detail**: `save-backups.md` never mentions how to send a whole project with its samples (low, add one line) |
| Purging Unused Samples | Find the project folder's samples that no set uses, review and delete | `File › Delete unused recordings and save...` (`:11907`), `Tools › Delete unused files and save project` (`:11799`), `Tools › Purge unused audio clips` (`:11786`) | **detail**: only "delete unused recordings" is in the wiki (`recording.md:109`); the Tools items are undocumented (low) |
| Report a Crash | After a crash, a zipped report (preferences and logs) to send by mail; privacy; it gets no reply, support is separate | Next-start dialog "LYNX SHARE closed unexpectedly" (consent, `Show what will be sent`, file names off by default, `Report on GitHub instead`, `Open the folder with the report`, `src/ui/CrashReporter.cpp:588-639`); `Help › Report a problem...`; wiki `troubleshooting.md:144` | same, LYNX is better (in-app send with consent). `help-and-tips.md` never mentions it (low) |
| Getting Technical Help | Attach the status-report pack (prefs, run log) to a support request | `Help › Report a problem...` (adds the version, system and session steps) | same |
| Creating Manual MIDI Mappings (12 pages) | MIDI Map Mode (tinted mappable elements), map clips, scenes, chromatic key ranges, clip-view controls, relative navigation, volume and pan, note ranges, device parameters; the Mapping Browser (Min/Max, invert, delete); tips (transport, several surfaces, default set) | `Ctrl+M` MIDI Map Mode, `Ctrl+K` Key Map Mode, right-click › `Link to controller...` / Remote control settings, `Options › Multilink to controllers` (Ctrl+J); wiki `midi-controllers.md:31-112` | **missing in Learn**: no tutorial on linking a knob or a pad. See T-13 (0.1.33 lesson); in 0.1.32 add one line to `midi-keyboard.md` (T-13a) |
| Using the Help View / Finding More Help | How the lesson viewer navigates (back, forward, home, next page); the **Info View** explains what is under the mouse; `Read the Live Manual` in the Help menu | Hint bar (FL), Live-look **Info View** in Clip-launch (`src/ui/HintBar.h:112-125`, toggled by `Shift+/`, `src/core/KeyMap.cpp:114`), `Help › How to use...`, How-to cards with `Watch the tutorial` (`src/ui/HowTo.cpp:202`) | **detail**: `help-and-tips.md` never mentions the Info View or the extended hint panel; **WRONG**: its `?` claim breaks in Clip-launch (T-1); **missing**: no manual link in Help (T-6) |
| The File List | Each sample of the set: replace by drop, keep warp markers, hot-swap, edit externally, Location column, candidates | The Browser's current-project list ("Audio files used by this project ...", I18nEnglish key), channel `Replace` | **detail** (low): not taught anywhere |
| What is a Project | A Project is a folder that gathers sets, recordings, samples and presets; Live makes it on the first save | `.lynx` file + `Save to new project folder...` (`:11890`), `Open the project's folder` (`:11909`), `Samples/Imported` with Always collect (`:11300`) | **detail**: `save-backups.md:9` says a `.lynx` "keeps everything", which a beginner reads as "the samples are inside". Samples and recordings stay as files beside or outside it. See T-7 |
| Info View (concept, 190rl "Loved") | Hover any control: a title and one paragraph in a fixed bottom-left panel, can be folded | Clip-launch Live look: `InfoView` (title = the hint before its first colon); other layouts: the one-line hint bar + optional `Hint bar (extended hint panel)` (Toolbar menu, `MainComponent.cpp:2241`) | **detail**: the hints are one line; Live's paragraph-length help per control is **n/a** (0.1.33 idea T-19). Neither panel is documented (T-2) |

### 1b. FL Studio 2026 help mechanics

| FL help topic | What it teaches / does | LYNX equivalent | Status |
|---|---|---|---|
| Hint bar | Hover: a one-line description; a click or drag shows the value; peak meters show the value under the pointer; icons on the right flag automatable, linkable or right-click-able | `src/ui/HintBar.h:10-30` (hover hint, live value, mouse and key glyph markup, `postNotice`), quick-tip bubbles for toolbar icons | same; `help-and-tips.md:9` describes it correctly |
| Extended Hint Panel | The same hint, larger, floating at the lower left, click-through, movable; toggled from the toolbar's `Hint bar` item | `Hint bar (extended hint panel)` in the Toolbar menu (`MainComponent.cpp:2241`, `src/ui/ExtendedHintPanel.h`) | **detail**: undocumented in Learn and wiki (T-2) |
| F1 / Help index (context help) | F1 opens the manual, at the focused window's or plugin's page (`HelpAliases.scr` maps about 230 window ids to manual pages) | F1 = the **shortcut sheet** (`KeyMap.cpp:199`); per-window `How to use this...` + `Watch the tutorial` (`HowTo.cpp`); built-in plugins' wrapper Help opens their wiki page (`src/core/PluginHelp.h:3-6`, `ChannelSettings.cpp:7218`) | **decision D-2** (F1 differs from FL); a text fix is still possible: say in `shortcuts-keys.md:9` that FL's F1 opens its manual while LYNX's opens the sheet, and where the guides are |
| Help › Gopher (AI, Alt+F1) | An assistant trained on the manual | none | **n/a** |
| Help › Loop Starter tutorial / Stepsequencer tutorial (new in 2026) | Interactive in-app lessons: steps, velocity, per-step controls, pattern vs song, then the Loop Starter flow | First-song tour (Simple only, 7 steps, `src/core/FirstSongTour.h:31-72`) | **n/a** for the Pattern layout: there is no interactive step-sequencer tour. T-15 (0.1.33). The Learn `pattern-tour` is the static stand-in |
| Welcome window (View › Welcome to FL Studio; "Remix a song" tile in 2026) | Fresh-install window with tools and recent projects | Welcome card (5 choices, `src/core/Onboarding.h:64-86`), `File › Start page...` (`:11881`), Simple Welcome screen (`FirstSongTour.cpp:428-443`) | same (LYNX has more) |
| Help › What's new | Opens the installed changes list | none after an update (T-9) | **missing** |
| Help › FL Studio tutorials (YouTube), homepage | Video tutorials channel | Only via each window's How-to card → `Watch the tutorial`; Next-3 card `Learn more...` → `wiki/getting-started/` (English, no `?lang`, `src/ui/MainOnboarding.cpp:325`) | **missing** as a Help item (T-6); **detail**: the Next-3 link has no language (T-8) |
| Help › Support forum / Knowledgebase | Links to forum and Q&A base | `Help › Report a problem...`; no link to the FAQ, troubleshooting or Discord | **missing** (T-6) |
| Help › Diagnostics | Separate tool: troubleshooting, project recovery, plugin removal | Crash guard + next-start report (offers "run the plugin in a separate process"), `File › Revert to last backup...`, `Open autosave folder` | same in spirit |
| Plugin wrapper › Help | The plugin's manual page | `pluginhelp::pageFor` → `lynx-share.com/wiki/built-in/<fx\|gen>-<slug>/` | same |
| Dialogs' "Remember my choice" (2026 rename of "Don't show again") | Wording convention | LYNX uses `Remember my choice` (wiki `mixer.md`) but the Recording window says `Don't ask again (right-click the record button ...)` (`src/ui/Dialogs.cpp:235`) | **detail** (low, FL's 2026 wording; T-18) |
| Basics: Playlist tracks vs channels vs mixer (manual `basics_workflow`, `channelrack`) | Playlist tracks are free lanes, not tied to an instrument or a mixer track; each channel's mixer-track selector picks its insert, and several can share one; the "Instrument track" mode links channel + playlist track + mixer track | see §3 | **detail** (high, user feedback): T-3, T-4, T-5 |

### 1c. BandLab / FL Studio Mobile onboarding

| Topic (program) | What it teaches / does | LYNX equivalent | Status |
|---|---|---|---|
| BandLab Help › Show Tutorial (23 coach-mark steps; dims the screen, pulsing dot, "n of 23 · Next", Next can do the step, replay from Help, asks to save or discard first) | Builds a first song: track, drum beat, tempo, effect preset, loop, MIDI bass, arrange, mix (keep out of the red), mute/solo, name, save | First-song tour: 7 steps, ring + card beside the target, `Do it for me`, Next / Back / Skip, resumes after a restart; Help › First-song tour (any layout, switches to Simple, `MainOnboarding.cpp:540-551`) | same (shorter by design). **detail**: the tour never teaches tempo, mixing levels, mute/solo or naming; `Finish` covers master and save (low) |
| BandLab new-track tiles with one help line each | What each track type is for | Simple New track chooser (`src/ui/SimpleWorkflow.cpp:255-261`, one line per tile) | same |
| BandLab Help › Shortcuts panel (Ctrl+/, search + category chips) | Searchable shortcut list | F1 sheet (not searchable, `MainComponent.cpp:12600-12640`); `Manage shortcuts...` is searchable; the `?` palette lists actions with keys | **detail** (low) |
| BandLab Help › Video Tutorials, Help Center, Help Bot | Links out | none in the Help menu (T-6) | **missing** |
| BandLab demo projects (Project › Load Demo Project) | Finished songs to take apart | `Open a demo song` (tour end card, Simple Welcome screen, Start page, `File › New from template`, `MainOnboarding.cpp:184,501`) | same. `clip-launch-tour.md:11` doesn't say where to open it (T-14) |
| FLM first run: offers to play the getting-started video; Help › Video tutorials ("you can watch it any time under Help") | Video onboarding | Welcome card has no video link; videos are reached only from How-to cards | **missing** (T-6; T-8 for the card) |
| FLM "Show hints" switch | Hints on/off | `Help › Show tips and hints` and Settings › General › Hints and warnings (`MainComponent.cpp:12283`, `SettingsPages.cpp:537-539`) | same |
| FLM Help › Users & support forums, Open logs folder | Support links, logs | `Report a problem...`; the crash panel's `Open the folder with the report` | **detail** (low) |

---

## 2. Learn pages vs the code (rollup032)

**Method:**
- Every back-quoted UI string of all 36 EN pages was grepped against the C++ string literals of `src/` (tests excluded) and against the English display table `src/ui/I18nEnglish.h`. That table renames "workflow" to "layout" in English, so `Pattern layout (default)` etc. are correct.
- After that, every behaviour claim of the 17 key pages was traced in the code.
- Only `‹ Back` had no literal: it is built from glyph + `tr ("Back")` (`src/ui/FirstSongTour.cpp:231`), so it is correct.

### 2a. WRONG texts

| # | Page:line | The claim | What the code does | Evidence |
|---|---|---|---|---|
| W-1 | `open-flp.md:10` (also the summary line 3) | The `.flp` opens "in the Pattern layout" | Opening a file never switches the layout. The only `setLayoutStyle` callers are the View menu, the welcome card, the setup wizard, the tour, Touch mode and restoring the UI options. A `.flp` opened while in Simple or Clip-launch stays in that layout | `MainComponent.cpp:5315,4930,10730,12816`; `MainOnboarding.cpp:253,545`; the load path `MainComponent.cpp:10300-10400` has none |
| W-2 | `open-als.md:10` (summary line 3) | "LYNX SHARE opens it in the Clip-launch layout" | Same as W-1: the layout stays as it is. A Pattern-layout user sees the set in the Playlist and Channel rack, not the clip grid | same |
| W-3 | `open-mobile.md:16` (summary) | "The song opens in the Simple layout" | Same as W-1 | same |
| W-4 | `help-and-tips.md:23`, `command-palette.md:9` | "Press `?` for the command palette" | In the Clip-launch layout with Live's look (its default; only `View › Classic LYNX look in the Clip-launch layout` turns it off), `Shift+/` = `?` shows or hides the **Info View**. The palette opens only from `Ctrl+Shift+Y` or the toolbar magnifier there. The Help menu still prints `Search actions...  ?` in every layout | `KeyMap.cpp:114` (`view.detailInfo`, Shift+/), `MainComponent.cpp:5593-5596` (only while `livelook::active()`), `:2574-2580` (the `?` fallback runs only when no action took the key), `:12291` |
| W-5 | `open-flp.md:19` | "Third-party VST3 and CLAP plugins load" | Windows and Linux also host **VST (VST2)**, macOS hosts AU. Most `.flp` files use VST2 plugins | `src/platform/Paths.cpp:158-166`, VST2 folders `:130` |
| W-6 | `vst-plugins.md:10` | `Plugin folders...` shows "the standard VST3 and CLAP folders" | The defaults include the VST2 folders (`VSTPlugins`, `Steinberg/VSTPlugins`, `Common Files/VST2`...) and LV2 on Linux | `Paths.cpp:130,158-166` |
| W-7 | `first-song-tour.md:11` | "Do the step yourself, or click `Do it for me`" (every step) | Step 5 (voice or loop) has **`Show the loops`** instead: a voice or a loop can't be made up | `FirstSongTour.cpp:261-263`, `core/FirstSongTour.h:93` |
| W-8 | `switch-layouts.md:10-13` | The welcome card's answers are three | The card has **five**: also `Logic, Cubase, Reaper, Pro Tools or Studio One` (Simple + the Mixer at hand) and `I'm new to making music` (Simple + the first-song tour) | `core/Onboarding.h:64-86,91-112,115` |
| W-9 | `first-song-tour.md:9` | "On the first start, LYNX SHARE asks where you are coming from" (the only way in) | Only a fresh profile gets the card: a profile with older LYNX settings gets the Setup wizard. The tour also starts any time from `Help › First-song tour`, from any layout (it switches to Simple). The page only says Help brings a *paused* tour back | `Onboarding.h:121-129`; `MainComponent.cpp:12266-12269`; `MainOnboarding.cpp:540-551` |

### 2b. Less clear / missing detail on the 17 key pages

| # | Page:line | Problem | Code fact |
|---|---|---|---|
| L-1 | `pattern-tour.md:35` | "Each channel plays into a Mixer track" never says which one, how to see or change it (the route box), or that a Playlist track is **not** a Mixer track (see §3) | Route box visible by default (`ChannelRackComponent.h:88`), hint `[LMB] drag or [Wheel] routes [2xLMB] show in the Mixer [RMB] next free insert` (`ChannelRackComponent.cpp:3771`), `Ctrl+L` (`KeyMap.cpp:226`); starter channels on inserts 1-5 (`core/Song.cpp:2501-2502`), new channels take a free insert (`ChannelRackComponent.cpp:4401`) |
| L-2 | `mixer-routing.md:9` | "Every track goes to the Master": "track" here means Mixer track, but the Playlist also has tracks. There is no step on how a channel reaches an insert | as L-1 |
| L-3 | `audio-setup.md:10-11` | The driver list misses LYNX ASIO (built in, any device) and the Windows Audio shared/exclusive choices. `Input device`, `Sample rate` and `Recording latency compensation` are never named | `SettingsPages.cpp:1063-1065,1644-1679,1712-1716` |
| L-4 | `help-and-tips.md:9-10` | The only help channel it names is the hint bar. It misses: the Info View (Clip-launch), the extended hint panel (Toolbar menu), the quick-tip bubbles on icons, `Help › Getting started`, `Help › First-song tour`, `Help › Report a problem...`, and that tips come at most one per session | `HintBar.h:41-47,112`; `MainComponent.cpp:2241,12263-12311`; `core/Onboarding.h:516` |
| L-5 | `help-and-tips.md:23` vs the Help menu | The page says "command palette", the menu says `Search actions...`, and the key list says "Command palette: find and run..." (`KeyMap.cpp:209`). Users look for the name they read | `MainComponent.cpp:12291` |
| L-6 | `clip-launch-tour.md:11` | "Here we open LYNX's demo song" never says how. The demo songs are under `File › New from template` and on the Start page | `MainOnboarding.cpp:184`; `core/DemoSongs.h:267-269` (Intro / Verse / Hook) |
| L-7 | `open-als.md`, `open-flp.md`, `open-mobile.md` | After W-1 to W-3: none says which layout shows the file best, or that `View › Layout style` switches with the project unchanged | `switch-layouts.md:22` already says the project stays |
| L-8 | `save-backups.md:9` | ".lynx keeps everything" misleads: samples and recordings are separate files. It never names `Collect all and save` / `Always collect and save` / `Save to new project folder...` / `Export › Zipped loop package...` for moving a project to another computer | `MainComponent.cpp:11890-11896,11909,11944` |
| L-9 | `midi-keyboard.md:17` | "Select an instrument in the Channel rack" is Pattern-only; in Simple and Clip-launch you select a track. No line on linking knobs (`Link to controller...`, Ctrl+M) | wiki `midi-controllers.md:31-112` |
| L-10 | `record-voice.md:25` | Quotes `Don't ask again`; the box reads `Don't ask again (right-click the record button for the recording filter)` | `Dialogs.cpp:235`. Not wrong, but the full label tells where the filter is |
| L-11 | `shortcuts-keys.md:9` | "FL Studio's own" keys, yet F1 itself differs: FL's F1 opens the manual | `KeyMap.cpp:199` (see D-2) |
| L-12 | `record-voice.md` (whole page) | Simple-layout only; no pointer for Pattern or Clip-launch users (insert input box + arm, or `Tools › One-click audio recording`) | `MainComponent.cpp:11724-11730`; wiki `recording.md:10-55` |

### 2c. Pages checked and found correct

Every quoted control, menu path, shortcut and behaviour claim in these pages matches the code:
- `pattern-tour` (apart from L-1): starter channels Kick, Clap, Hat, Snare and Synth (`Song.cpp:2493-2499`); `L` toggles Pattern / Song (`KeyMap.cpp:63`); F5, F6, F7 and F9 show the windows and F8 opens the Plugin picker (`KeyMap.cpp:94`).
- `clip-launch-tour` (apart from L-6): `Create scenes from sections` (`ClipGridComponent.cpp:3475`), the `Back to Playlist` button (`:200`), `Song` / `Grid` / `Both` (`MainComponent.cpp:4833-4835`), Ctrl+Alt+3 and Ctrl+Alt+4 (`KeyMap.cpp:110-111`), the `Main` column (`SessionMixer.cpp:39`).
- `mixer-routing` (apart from L-2): `Route to this track only` and `Sidechain to this track` (`MixerComponent.cpp:3405-3407`), the wires lane (`:88-92`).
- `record-voice` (apart from L-10/12): `Audio, into the Playlist as an audio clip` (`Dialogs.cpp:258`), `Input Level` (`SimpleDock.cpp:3334`), the Room, Echo and Warm one-knob effects (`core/GoalKnobs.cpp:59-95`), `Preset` (`SimpleDock.cpp:354`), the `Clean voice` submenu (`PlaylistComponent.cpp:11193`), `+ Voice track` (`core/Onboarding.h:212`).
- `midi-keyboard`: F10 opens MIDI settings (`KeyMap.cpp:101`), `Input devices` (`SettingsPages.cpp:2646`), `Notes and automation` (`Dialogs.cpp:260`).
- `save-backups`:
  - Ctrl+Alt+S saves a new version (`KeyMap.cpp:167`).
  - `Versions...`, `Open as a copy` and `Label...` (`VersionsPanel.cpp:325-344`).
  - `Revert to last backup...` (`MainComponent.cpp:12026`).
- `shortcuts-keys`:
  - `Manage shortcuts...` sits at the bottom of the sheet (`MainComponent.cpp:12631`).
  - `Change` and `Reset to default` (`ShortcutManager.cpp:535`), `Changed only` (`:160`).
  - Ctrl+T toggles the typing keyboard (`KeyMap.cpp:66`).
  - The rows are ZSXDCVGBHNJM and Q2W3ER5T6Y7U... (`KeyMap.cpp:759-760`).
- `switch-layouts` (apart from W-8): `Help › Welcome...` (`MainComponent.cpp:12265`), `View › Layout style` with its three items (`:5319-5323`).
- `vst-plugins` (apart from W-6): `Find new plugins` and `Plugin folders...` (`PluginManager.cpp:665-666`), the `Failed` filter (`:85`), `Plugin (VST/VST3/CLAP)` (`MixerComponent.cpp:8811`), `More plugins... (n effects)` (`PluginWindow.cpp:3093`), `Add` (`PluginPicker.cpp:750`).
- `help-and-tips` (apart from W-4, L-4, L-5): `Got it` and `No more tips` (`ui/Onboarding.cpp:735`), Settings › General's `Show tips and hints` (`SettingsPages.cpp:539`), `How to use this...` (`HowTo.cpp:84`), `Watch the tutorial` (`HowTo.cpp:89`).
- `lynx-extras`: `Options › LYNX extras...` (`MainComponent.cpp:12164`), `FL-exact (turn all LYNX extras off)` and `Recommended` (`SettingsPages.cpp:3091`).
- `simple-beat`:
  - `+ Add track` (`SimpleWorkflow.cpp:549`) and the `Drum Machine` tab (`SimpleDock.cpp:3101`).
  - `Length` and `Swing` (`SimpleStepsPage.inc:155,179`); the row keys are in the tooltips (`:373`).
  - `Finish` gives Master looks plus a WAV or MP3 save (`FinishDock.cpp:216-363`).
- `first-song-tour` (apart from W-7, W-9):
  - The tour has 7 steps, and the step texts match the page (`core/FirstSongTour.h:31-72`).
  - `Next ›`, `‹ Back` and `Skip tour` (`FirstSongTour.cpp:229-232`).
  - The Help item says "Continue the first-song tour (step n of 7)" (`MainComponent.cpp:12266`).
- `settings-tour`:
  - Ctrl+, opens Settings on the last page (`MainComponent.cpp:3161-3167`), and F10 opens the MIDI page.
  - The tab order and names match (`SettingsDialog.cpp:1189-1201,1522`).
  - `Reset this page`, `Reset` and `Undo` (`:1509`).
- `export-mp3`: Ctrl+R / Ctrl+Shift+R (`KeyMap.cpp:182-183`), `What to render`, `Also save`, `Bitrate` and `Export...` (`ExportDialog.cpp:459-559,1798`).
- `command-palette` (apart from W-4): Ctrl+Shift+Y (`KeyMap.cpp:209`), `Tools › Switch smart disable for all plugins` (`:11787`), `Deactivate Track` (`ClipGridComponent.cpp:2997`).
- `track-only`: `View › Layout style` also lists Track-only mode (`MainComponent.cpp:5322`), and the `Yes` / `No` box (`WorkflowExtras.cpp:54-56`).

### 2d. In-app guide texts (`src/ui/HowTo.cpp:22-80`) and tips (`src/core/Onboarding.h:383-395`)

All of their strings match the controls. Two omissions:
- The **Channel rack guide** (`HowTo.cpp`, 2nd entry) never mentions the route box.
- The **Mixer guide** and the one-time **mixerRouting tip** (`Onboarding.h:387`) explain only insert-to-insert routing, never how a channel gets into an insert.

Those are exactly the places the "Playlist track 1 is not Mixer track 1" user lands on (T-4).

---

## 3. "Playlist track 1 is not Mixer track 1"

**LYNX today**

The **Learn pages** barely touch it:
- `pattern-tour.md:35` says only that each channel plays into a Mixer track. It never says which one or how to change it.
- `mixer-routing.md` covers insert-to-insert sends only. Its line 9 ("Every track goes to the Master") uses "track" for a Mixer track on a site where "track" also means a Playlist track.
- `first-song-tour` is Simple-only. There each track has its own channel and insert (`simpletracks::insertOf`), so the issue does not arise.
- No Learn page says that a Playlist track has no insert of its own in the Pattern layout.

The **wiki** does explain it, but nothing on the Learn pages or in the app links there:
- `wiki/first-song.md:39-40`: the five starting channels play through inserts 1 to 5, a new channel takes a free insert, and the route box shows which one; Ctrl+L.
- `wiki/mixer.md:22-26`: send a channel to an insert with the route box, Ctrl+L.
- `wiki/channel-rack.md:41`: the route box.
- `wiki/coming-from-other-daws.md:24`: "Tracks and mixer inserts are separate."

The **app**:
- The route box shows the insert number.
- Its hint gives the gestures, and its value name is `Target mixer track` (`ChannelRackComponent.cpp:2060,3771`).
- `Mixer › Arrange inserts like the Playlist` exists.
- No how-to card or tip says "the number is the insert; Playlist tracks don't route".

The **terms mix**: Learn says "Mixer track", the wiki says "insert", and the app says both ("Target mixer track", "Next free insert", "Send to free mixer inserts").

**The starter project hides the confusion:**
- Kick, Clap, Hat, Snare and Synth sit on inserts 1-5.
- The beat sits on Playlist track 1, while Mixer track 1 is the Kick only.
- So "Playlist track 1 = Mixer track 1" looks true for the Kick and false for everything else.

**FL manual (`basics_workflow`, `channelrack`, `mixer_iorouting`), in my words:**
- Playlist tracks are free lanes for pattern, audio and automation clips. They belong to no instrument and no Mixer track.
- Each Channel Rack channel has a Mixer-track selector (shown from the rack's options) that picks its insert, and several channels may share one. Ctrl+L sends the selected channels to free tracks.
- The exception is "Instrument track" (and Audio track) mode, which links a channel, a Playlist track and a Mixer track as one group. The selector then dims, and "Separate from instrument track" breaks the link.
- A Mixer track feeds the Master by default. Route switches send it elsewhere, and a send knob sets the level.
- Per the 2026 manual page, a new channel goes to the Master ("---") and selectors are hidden by default. LYNX shows the selector and gives new channels a free insert, see D-3.

**Live lessons, in my words:**
- A track *is* its mixer channel: the track's In/Out section holds its input choosers (type and channel) and its output.
- Arming and monitoring are per track.
- There is no separate routing step, because the numbering is shared (track 3 is mixer channel 3).

**Verdict:**
- LYNX explains the FL model correctly only in the wiki. The Learn tour and the in-app guides, which users actually read, skip it.
- Fix: T-3 for the texts, T-4 for the guide and tip, T-5 for the terms.

---

## 4. Gap list

The "Rel." column holds 0.1.32 for a text, hint or Learn fix, and 0.1.33 for a new lesson or feature. Every Learn fix touches the EN page and its 14 translations under `website/_src/learn/<lang>/`, then needs a site build (`website/_src/build.py`).

| Id | Sev | Fix (what to write or change) | Files | Verify | Rel. |
|---|---|---|---|---|---|
| T-1 | high | **`?` in Clip-launch** (W-4). In `help-and-tips.md:23` and `command-palette.md:9`, write: press `?` or `Ctrl+Shift+Y` (in the Clip-launch layout `?` shows or hides the Info View, as in Live: use `Ctrl+Shift+Y` or the magnifier). In the Help menu, show the key the layout really uses: in Live look, `Search actions...  Ctrl+Shift+Y` | the two `.md` × 15; `src/ui/MainComponent.cpp:12291` | In Clip-launch, press `?`: the Info View toggles; the menu shows Ctrl+Shift+Y. In Pattern, `?` opens the palette | 0.1.32 |
| T-2 | med | **Document the other hint channels** in `help-and-tips.md` §1 (L-4). Add: in the Clip-launch layout the Info View at the bottom left shows the same help with a title (`?` toggles it); `Options › Toolbar › Hint bar (extended hint panel)` floats a larger hint panel; resting on a toolbar icon shows its name. Also list `Help › Getting started`, `Help › First-song tour` and `Help › Report a problem...`, and say tips come at most one per session | `help-and-tips.md` × 15; wiki `tour.md` § The hint bar | Each named item exists (`MainComponent.cpp:2241,12263-12311`) | 0.1.32 |
| T-3 | high | **Explain channel → insert, and that Playlist tracks don't route** (L-1, L-2, §3). In `pattern-tour.md` § The Mixer step 2, write: each channel plays into the Mixer track whose number is in its route box, the small number between the knobs and the name in the Channel rack (the starter channels use 1 to 5). Drag the box or use the wheel to change it; double-click it to see that Mixer track; `Ctrl+L` gives the selected channels free ones. Add: Playlist tracks only hold clips, so Playlist track 1 is not Mixer track 1. In `mixer-routing.md:9` write "Every Mixer track goes to the Master" and add a line pointing to the route box | `pattern-tour.md`, `mixer-routing.md` × 15 | Read against `ChannelRackComponent.cpp:2060,2125,3771`, `Song.cpp:2501` | 0.1.32 |
| T-4 | high | **In-app guide and tip.** Add to the Channel rack how-to (`HowTo.cpp`, `channelRack`): "The number box beside the knobs is the Mixer track the channel plays through: drag it to change it, double-click it to show it." Add to the Mixer how-to and the `mixerRouting` tip text (`Onboarding.h:387`): "A channel reaches an insert through its route box in the Channel rack; Playlist tracks don't route sound." New tr() keys need 14 translations | `src/ui/HowTo.cpp`, `src/core/Onboarding.h:387`, `resources/i18n/*` | `Help › How to use... › Channel rack` / `Mixer` show the line; Onboarding tests (tip text) still pass | 0.1.32 |
| T-5 | low | **One term.** On Learn pages write "Mixer track (insert)" the first time, then "Mixer track", as the app's `Target mixer track` does. Align the wiki (`mixer.md`, `first-song.md`) with one sentence: "a Mixer track, also called an insert" | Learn + wiki pages | grep "insert" in learn/*.md | 0.1.32 |
| T-6 | high | **Help menu links** (FL Help index / tutorials / forum; Live's manual item; BandLab's Video tutorials and Help Center; FLM's Video tutorials). Add to the Help menu: `Tutorials (lynx-share.com/learn)...`, `Manual (lynx-share.com/wiki)...` and `FAQ and troubleshooting...`, each with `?lang=` as `HowTo::tutorialUrl` builds it. Optionally add `Community (Discord)...` | `src/ui/MainComponent.cpp:12263-12330` (+ i18n keys) | Each item opens the page in the app's language | 0.1.32 (link items, no new behaviour) |
| T-7 | med | **File management is undocumented** (L-8; Live's Collect / Pack / Project lessons). In `save-backups.md` add § Move a project to another computer: samples and recordings are files beside the `.lynx`, not inside it; `File › Collect all and save` copies the outside ones into the project's folder (`Always collect and save` does it on every save); `File › Export › Zipped loop package...` writes one `.zip` with everything; `File › Open the project's folder`. Rewrite line 9 as "keeps patterns, the Mixer, plugins and their settings, and points to its samples". Add `Collect all and save` and the Tools clean-up items to wiki `exporting.md` or `file-formats.md` | `save-backups.md` × 15; wiki | Items exist (`MainComponent.cpp:11786,11799,11820,11890-11909,11944`) | 0.1.32 |
| T-8 | low | **Next-3 card link.** `Learn more...` opens `wiki/getting-started/` in English. Send it to the layout's Learn tutorial (`pattern-tour` / `clip-launch-tour` / `simple-beat`) with `?lang=` | `src/ui/MainOnboarding.cpp:325` | Click it in pt-BR: the pt page opens | 0.1.32 |
| T-9 | med | **What's new after an update.** Add `Help › What's new in this version...`. 0.1.32 can open `lynx-share.com/updates/` (a link); the in-app notes panel for the installed version is 0.1.33 | `MainComponent.cpp` Help menu; `Updater.cpp:1383,1458` | The item opens the right version's notes | 0.1.32 (link) / 0.1.33 (panel) |
| T-10 | med | **audio-setup page** (L-3; Live's Audio I/O and Driver Error Compensation lessons). §1: name `LYNX ASIO (built in)` first for "no ASIO driver of your own", plus Windows Audio shared and exclusive; name `Input device` (for recording) and `Sample rate`. New § "Recorded takes late?": Settings › Audio › `Recording latency compensation` › `Measure the round trip` (a cable or loopback from an output to an input, 8 clicks, then takes land in time) | `audio-setup.md` × 15 | Strings at `SettingsPages.cpp:1063-1065,1619,1644-1716` | 0.1.32 |
| T-11 | med | **record-voice for the other layouts** (L-12). Add "Good to know": in the Pattern or Clip-launch layout, choose the input in the Mixer track's input box and arm it, or use `Tools › One-click audio recording › Into playlist as an audio clip`; see wiki Recording | `record-voice.md` × 15 | Menu path `MainComponent.cpp:11724-11730` | 0.1.32 |
| T-12 | med | **New Learn: "Record audio in the Pattern layout"**: input box, arm, Record window, count-in, punch / loop takes, monitoring (Live's Recording Audio lesson) | new `learn/record-audio.md` + video | Build + check-links | 0.1.33 |
| T-13 | med | **Linking knobs** (Live's Manual Mappings lesson). 0.1.32 (T-13a): add to `midi-keyboard.md` § Good to know: "Right-click any knob › `Link to controller...` and move a knob on your keyboard; `Ctrl+M` maps several at once; see MIDI controllers". 0.1.33: a new Learn page "Map knobs and pads" | `midi-keyboard.md` × 15; new page | Strings `AutomationUi.cpp:2272`, `ToolbarLooks.inc:102` | 0.1.32 / 0.1.33 |
| T-14 | low | **clip-launch-tour demo song** (L-6). Write: "Open a demo song (`File › New from template` or the Start page): its arrangement has Intro, Verse and Hook" | `clip-launch-tour.md:11` × 15 | Demo entries `MainOnboarding.cpp:184` | 0.1.32 |
| T-15 | med | **Interactive Pattern-layout tour** (FL 2026's Stepsequencer tutorial; BandLab's tour): a 5-6 step coach-mark tour for the FL choice (steps, Piano roll, place in the Playlist, Song mode, route to a Mixer track). It reuses the tour engine | `core/FirstSongTour.h`, `ui/FirstSongTour.cpp` | Tests like FirstSongTour190dv | 0.1.33 |
| T-16 | high | **Opening a file does not change the layout** (W-1 to W-3, L-7). 0.1.32 text: in `open-flp.md:10` write "...then opens it. In the Pattern layout (`View › Layout style`) it looks as it did in FL Studio." Use the same pattern for `open-als.md:10` (Clip-launch: the clip grid) and `open-mobile.md:16` (Simple); fix the three summaries too. Note the tutorial videos may show the target layout already set; their narration is fine if it says "in the Clip-launch layout". 0.1.33 option for the owner: offer the matching layout after an import (a one-line prompt), see D-1 | 3 pages × 15 (+ `index.json` summaries, `website/learn/index.html`) | Open an `.als` from the Pattern layout: the layout stays | 0.1.32 (text) / D-1 |
| T-17 | med | **Small wrong texts**: W-5 (`open-flp.md:19`: "Third-party VST, VST3 and CLAP plugins (AU on a Mac)"); W-6 (`vst-plugins.md:10`: "the standard VST, VST3 and CLAP folders"); W-7 (`first-song-tour.md:11`: add "(the loop step offers `Show the loops`)"); W-8 (`switch-layouts.md`: list all five answers, or say "five answers, for example:"); W-9 (`first-song-tour.md`: add "Missed it? `Help › First-song tour` starts it from any layout"; mention `Skip tour`) | those pages × 15 | Code refs in §2a | 0.1.32 |
| T-18 | low | **Wording**: `Dialogs.cpp:235` "Don't ask again (...)" → "Remember my choice (...)", as FL 2026 and LYNX's other dialogs say; then `record-voice.md:25` quotes it in full (L-10) | `src/ui/Dialogs.cpp:235`, i18n, `record-voice.md` | The Recording window shows the new label | 0.1.32 |
| T-19 | low | **Paragraph help per control** (Live's Info View depth): a second, longer line for the most-used controls in the Info View or extended panel | `HintBar`, hint sources | n/a | 0.1.33 |
| T-20 | low | **Searchable shortcut sheet** (BandLab Ctrl+/): a search box on the F1 sheet, or a sheet note "type `?` to search every action" | `MainComponent.cpp:12600-12755` | n/a | 0.1.33 (box) / 0.1.32 (note) |
| T-21 | low | **Learn misc**: `midi-keyboard.md:17` add "(in Simple or Clip-launch: select a track)"; `pattern-tour` mention the velocity graph and pattern length; `help-and-tips` mention the crash dialog and `Report a problem...` | pages × 15 | grep | 0.1.32 |

### Decisions for the owner (FL-parity, not text fixes; per "no silent parking")

- **D-1: should opening a file switch the layout?**
  - The pages promise that a file opens in its own layout. The code never switches.
  - Options: (a) fix the texts only (T-16, recommended for 0.1.32); (b) 0.1.33: after an import, a one-line offer "Show it in the Clip-launch layout?"
- **D-2: F1.**
  - FL's F1 opens the manual at the focused window (`HelpAliases.scr`). LYNX's F1 opens the shortcut sheet (`KeyMap.cpp:199`).
  - FL-exact would mean F1 opens that window's How-to card or Learn page, with the sheet on another key.
  - Recommend keeping the sheet and fixing the text (L-11). REA of FL's F1 per window can follow in 0.1.33.
- **D-3: new channels' insert.**
  - The FL 2026 manual says a new instrument channel goes to the Master and that Mixer-track selectors are hidden by default.
  - LYNX gives every new channel a free insert (`ChannelRackComponent.cpp:4401`, `Song.h:3377-3379` cites FL round 162) and shows the selectors.
  - Needs an REA check against live FL 2026 before any change. The Learn texts (T-3) must describe whatever LYNX does.
- **D-4: Ctrl+N.**
  - FL's File menu gives Ctrl+N to `Save new version` (and to `Save to new project folder`). LYNX uses Ctrl+N for New project and Ctrl+Alt+S for Save new version (`KeyMap.cpp:163,167`).
  - `save-backups.md:16` matches LYNX. This is a keymap-parity question, not a text error.
