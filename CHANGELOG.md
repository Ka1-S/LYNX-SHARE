# Changelog

The short version of each release. The full notes, with measurements, known issues and downloads, are on the
[GitHub releases page](https://github.com/Ka1-S/LYNX-SHARE/releases).

## 0.1.32 alpha (unreleased: the release round writes the rest of these notes)

**From contributors**
- A Mixer insert can hold up to 25 effect slots, as in FL Studio 2026.2: the slot list keeps its ten rows and scrolls, a
  "+" tab under it adds a slot, and Options › General settings sets how many slots new mixer tracks get. PR #260 by
  @codewithamoako.
- The Mixer's *Reset selected track(s) to default* also removes the track's plugins and resets its name and color, after FL's
  warning; the routing stays. PR #284 by @codewithamoako.

**Added**
- *Transpose...* (Patterns, picker and Channel rack menus, FL's small "Semitones" box) and *Make unique as sample* with progress
  and Cancel; DAWproject export runs in the background with a progress window.
- Easier options: every Settings page has **Reset this page** (with Undo); the **?** command palette finds every Settings
  option and every window's own menu items, and opens Settings with the option outlined; every switch and box in Settings
  says what it does when you point at it; *Settings › LYNX extras* is grouped by area, keeps **FL-exact** and
  **Recommended** at the top and shows a small picture of what each visual extra adds.
- **How to use this...** at the end of each window's menu (and *Help › How to use...*): a short guide with a link to its
  Learn tutorial. A one-time tip the first time the Mixer, the Piano roll, the Tracker, the Device view or an automation
  lane shows (*Settings › General › Show tips and hints* turns them off).
- Notices: a plugin that can't open, a saved copy, a full insert or a failed backup now show a small card at the lower
  right (with a button such as *Plugin manager...* or *Show in folder*) instead of a box to click away.
- Soft look (*Options › Theme › Appearance*): flat steps and buttons, muted colors and slimmer plates, close to 0.1.29's look.
- *Playlist › Edit › Cut clips at the song position* (#285) and *File › Open the project's folder* (#286).
- Track-only mode and the Simple layout: the Piano roll shows the other tracks' notes as ghosts; a click on a Simple track's
  instrument icon opens the instrument (Discord).

**Changed**
- Dragging the tempo hides the pointer and is not stopped by the screen's edge, as in FL Studio (#283).
- *Settings › LYNX extras* also lists the track colors taken from the accent (#238), the Recent list and the one row per plugin
  in the plugin lists, and the Browser's MIDI preview and Tab completion (all on, as they were).
- Clearer names: Interface size, Menu spacing, Darken image, Waveform colors, Always on top (plugin windows), *Let other apps
  on this PC control LYNX (Remote API)*, *Line up recordings with the audio latency* (FL's Latency compensation).
- Opening a plugin or instrument window on Linux takes about half the work, and long paints of the Playlist (audio clips whose
  sample was not loaded) are gone.
- LYNX extras that only change the look are now on by default (the Chord lane's chord in the Piano roll, effects on
  linked track headers, the project name in the Simple layout, Live's scale control in the Clip-launch layout, dimmed
  unselected and darkened switched-off Playlist tracks); extras you switched
  yourself keep your choice, and *Settings › LYNX extras* has **FL-exact** and **Recommended** buttons.
- The Mixer's peak values and correlation bars (*Mixer › View*) are off by default: with them on, every fader and meter is
  19 px shorter than FL Studio's. If you switched them on yourself, they stay on.
- Two more LYNX extras are on by default: an audio clip's gain handle stays in view while the clip's middle is scrolled
  away (FL Studio then shows none; a press beside it still moves the clip), and a new project made in the Clip-launch
  layout gets a mixer track for every track, as a new Live set. *Settings › LYNX extras* now says which extras are on by
  default and names the two that change the sound; the first start's card for FL Studio users points to the page.
- The zoom keys work as in FL Studio 2026 in the Pattern layout: PgUp / PgDn zoom around the mouse pointer and Shift+5
  pressed again leaves the view as it is. LYNX's own way (around the playhead or the selection, Shift+5 again goes back)
  is the LYNX extra *Zoom keys: LYNX's anchor, and Shift+5 again goes back*, always on in the Clip-launch and Simple layouts.
- Export to FL Studio keeps FL Studio's trial-save mark: a project that came from a trial save (also after a .lynx save,
  imported tracks or a paste) goes back as one, so FL Studio's trial treats it as its own save; other projects are unchanged.

**Fixed**
- **Saving on a full disk no longer destroys the project.** Since 0.1.30 a save (and Save as, autosave, every export and
  settings file) could replace the project with a cut file when the disk filled up, and only said so afterwards. Every file
  LYNX writes now goes through one checked writer: if anything fails, the old file is left as it was and LYNX says why
  ("Not saved: the disk is full. Your project file was not changed.").
- Export to FL Studio: plugin values at the song's start are written on the plugin's own range (FL rendered LYNX's export of
  Thief as silence with a DC offset). DAWproject: every sample gets its real format (FL's Ogg-in-WAV and .wv are written as
  WAV), clip gain and pitch are kept. MIDI files carry the tempo map, time signatures and the name; File › Open takes the
  meter. FL Studio Mobile export lists tempo and signature changes and markers, and leaves out clips of muted tracks.
- Hosted plugins: a plugin window resizing itself in a loop no longer freezes LYNX; CLAP plugins get a buffer per audio port
  (FabFilter's CLAP effects crashed), a Sidechain input and the transport; a plugin installed in several formats loads VST3,
  then CLAP, then VST2.
- The undo history is capped at 1 GB as well as its step count (a big project's 100 steps reached ~1 GB); the splash goes at
  the logo drop on Linux and macOS.
- Track-only mode and the Simple layout: a clip moved or copied onto another instrument's track plays that instrument (Discord).
- Undo and redo keep the project's working time; Export to FL Studio's report names each mixer slot it left out; audio clips of
  GarageBand and BandLab imports keep their exact start after a save.
- Crashes and races found by crash reports, sanitizers and a UI stress test on Windows: Helix sample voices, quitting with a
  box open, the Project picker, the worker pool and render-ahead, Parametric EQ / Maximus / limiter latency reads, a lost X
  display, DrumGizmo's LV2 editor and u-he VST3 plugins on Linux.
- Online Sessions: the host's tempo and project settings reach every guest again, and VCAs, main volume, modulators, the clip
  grid, grooves, mix snapshots and the Master look stay as each copy set them (a hint says they aren't shared yet); the host is
  told when a guest's LYNX is too old to follow. PipeWire channels are used in the device's port order (left and right could be
  swapped). Remote Control's project.edit works on new projects again.
- Community reports: Alt+R renders as FL does (a WAV in Audio\Recorded, on the first free track); FL's marker menu on a
  marker's right-click; Alt+C and the other Alt shortcuts as in FL; clone names (#2, #3...); a whole selection of Playlist tracks
  takes the header menu; slide notes of no length from an .flp (#272); markers that skip in a row; Settings search keeps the
  keyboard (#247); value bubbles close (#299); cut hints and shortened names show whole (#296, #301, #303); no folders made from
  Windows paths on Linux.

## 0.1.31.2 alpha — fixes

A second hotfix for 0.1.31.

- Fixed: the start page's "Start a new project" list could read a template name past the end of its list while it was
  scrolled, at display scales that are not a whole number. It is the likely cause of a crash reported on Windows, which
  could not be reproduced here (Discord).
- Fixed: with a plugin window's keyboard button on "LYNX first" ("Allow the plugin to handle keystrokes before the host
  does" unticked), the typing keyboard never took its keys before a docked VST3 plugin. It does now, docked and detached
  (Discord: the typing keyboard silent over a docked plugin).
- Fixed: with "Stretched pattern clips loop their pattern" on, a clip drawn on an instrument track looped its first bar
  for ever: notes drawn, pasted, moved or stretched past a pattern's set length are now heard, as the length grows to
  whole bars that hold them, like a step past it already did; a clip saved that way is mended by the next edit in it
  (Discord).
- Fixed: Playlist › "Move automation clips into their tracks' lanes" put wrong values on the controls of big FL
  projects; each lane now plays exactly what its clips played (FL's clip range and mapped controls, the value held
  between clips, muted and overlapping clips) and stays within the engine's automation slots (Discord).
- Fixed: "Manage shortcuts" and the other search windows opened without the caret in their search box on desktops that
  activate a new window late (Linux, Wayland) (#244). They should now open with it; tested by simulation only.
- New: Options › *Typing keyboard settings* (and the same in the View menu): the typing keyboard button's right-click
  menu (Root note, Octave, Layout), also for windows of 1280-1440 px whose toolbar has no room for that button (#270).
- Fixed: Export to FL Studio wrote back the project data folder of an .flp made on another computer; FL 2026 stopped
  such a project at "Invalid project data folder". A data folder that isn't on this computer is left out.
- Fixed: an FL preset of a stock synth (Sytrus, Harmor, GMS, Toxic Biohazard, Morphine...) clicked in the Browser opened
  LYNX's own synth with an approximation of the sound; it opens that synth itself with the preset loaded and named after
  it, and Send to selected channel puts it into the selected channel's synth. The channel window's preset menus of Toxic
  Biohazard, GMS and Morphine list FL's presets (#288).
- Fixed 12 crashes and memory errors found by sanitizer and UI stress testing, plus reported crashes (Browser, Device
  View, Settings search, Tracker, Piano roll drag, Linux VST3, Simple drum machine…).
- Changed: the LYNX extra "MIDI files dropped on the Playlist become pattern clips" is on by default (FL ignores a .mid
  dropped on its Playlist, so no FL gesture changes; Settings › LYNX extras turns it off) (#287).

## 0.1.31.1 alpha — fixes

A hotfix for 0.1.31.

- Fixed: automation of FL Studio projects on the controls. An FL automation clip linked to several controls, or to one
  through a mapping formula (imported this way since 0.1.31), now moves every control it drives, by its formula, in the
  Mixer (faders, send knobs), the plugin windows and the Device view; before, only the first control showed it, and
  with the clip's raw value (#253, Discord).
- Fixed: a control you move by hand while the song plays shows the value you gave it for as long as it stays off its
  automation (until the next Play in the Pattern layout, until *Re-Enable Automation* in the Clip-launch layout), as you
  hear it; before, it was drawn back on its automation while the sound kept your value.
- Fixed: Device view cards follow automation while the song plays, as the plugin's own window and the Mixer do (Discord).
- Fixed: in the Clip-launch layout, choosing a track in the Arrangement (its header, a clip or a spot of its lane)
  shows that track's devices in the Device view, as in Live (Discord).
- Fixed: docked plugin and channel windows opened a little further away each time, until after a few clicks they opened
  outside the main window and seemed not to open at all (Discord).
- Fixed: Export audio with a folder in the file name (`/BOUNCE/$project...`) writes there without asking for the file
  again; a new *Folder...* button picks the export folder, and a second export no longer goes into `BOUNCE/BOUNCE` (#215).
- Fixed: in Track-only mode and in the Simple layout, a double-click on a clip of an instrument track opens that
  instrument's notes even when the clip is empty (it opened the Channel rack); the Simple layout's editor follows the
  clip you open, and a hint on an empty instrument track says how to draw a MIDI clip (Discord).
- Fixed: Automatic colors and icons let you add only one rule (#239).
- Fixed: right-clicking the hint bar crashed LYNX (#269).
- Fixed: markers couldn't be deleted (#281).
- Fixed: two LYNX entries in the Linux app menu (#278).
- Fixed: a Patcher used as an instrument came back in FL as an empty synth.
- Changed: plugins FL saved as a trial keep FL's trial mark on export (#282).
- Fixed: 0.1.31 used ~7 MB more memory and more CPU right after start-up.

## 0.1.31 alpha

**Performance**
- Measured again on one Windows PC in a quiet-machine run: while FL's *Thief* demo plays, LYNX SHARE uses 34.4 % of one
  CPU core (0.1.30 in the same session: 32.0 %) and *Exoplanet* 44.2 % (0.1.30: 46.7 %); *Thief* takes 564 MB of memory
  (FL Studio 2026: 905 MB) and opens in 4.3 s (FL Studio: 7.1 s). FL Studio's own CPU load varied between sessions on
  this PC, so it isn't compared.
- Known, being investigated for 0.1.32: on Windows *Thief* takes 24 MB more memory than in 0.1.30 (540 → 564 MB), and an
  empty project idles at 7.2 % of a core instead of 5.3 %.
- *Safe overloads* lets go as soon as the transport stops, the CPU meter shows the live part and the part rendered ahead,
  the plugin performance monitor lists each plugin's longest block and spikes, Windows PCs with four logical CPUs or fewer
  no longer start graphics-driver threads for drawing, Atrium's dense tank only takes memory where it runs (Live's
  *Patience* set), and on Linux a hosted plugin's memory is faulted in while it loads, off the audio thread.

**New**
- Linux arm64 builds (AppImage and tar.gz), for a Raspberry Pi 4 / 5 with a 64-bit OS and other arm64 machines.
- First start: a welcome card asks where you are coming from and sets up the matching layout; later starts open on a
  start page with recent projects and templates (#252).
- *Search actions...* (`?` or Ctrl+Shift+Y) finds and runs any command, menu item, LYNX extra, Settings page or recent
  project (#228); Settings has its own find bar (Ctrl+F, #247).
- *File › Open in new window*: a second project beside the first, with copy and paste between the windows (#232).
- The built-in **Tracker** (Shift+F7, *Edit in tracker* in a channel's menu) and tracker module import.
- **LYNX Model**, a physical-model instrument: marimba, vibraphone, xylophone, glockenspiel, tubular bells and kalimba.
- Playlist: a **Sections lane** for whole song parts, a **Chord lane** with **Smart Tempo**, **Track versions**, *Clips own
  their notes* on instrument tracks, hide and deactivate tracks, the song position to an audio clip's next transient,
  FL's automation clip rows in the Channel rack (steps and notes trigger them, MIN / MAX knobs), FL's fade previews (#230), FL's delete fade (#250).
- Automation: write modes Touch, Latch and Write, *Capture automation*, and modulators (LFO, envelope follower, ADSR,
  steps, random, macro, keytrack, velocity) for any knob, also on third-party plugins.
- Piano roll: key regions, a *Scale degree chord* stamp and pinned relationships in the Chord helper (discussion #105),
  stacked controller lanes, slide notes that move velocity, pan and Mod X / Y as in FL (#236).
- Clip-launch layout: Live's rack devices inline in the Device View with a Macro Map mode, Live's Solo / Cue, and range
  edits on chosen tracks.
- Simple layout: *Duck under*, Reverb and Echo sends, sections, and time signature changes.
- ARA plugins on a mixer insert, and the tempo map and time signatures sent to ARA plugins (#243).
- **JSFX**: REAPER's effect and instrument scripts in slots and as instruments, with 31 free scripts (#241).
- *Zipped loop package*, *Project data files* and *Collect all and save* (#251); *Import › Tracks from another
  project...*.
- Online Sessions: the host can remove a guest. `LYNX_OFFLINE=1` / `--offline` turns off every connection of LYNX's own
  (#233).
- The plugin pack has 39 plugins, LYNX Model, LYNX Fourfold, LYNX Wavecoil and LYNX Duoloom among them, and comes as
  LV2 on Linux too; the Windows pack also comes as a `.7z` of about 20 MB (the `.zip` is about 195 MB, same files).
- A details pass (the last open rows of FL Studio, Live and BandLab behaviours) and a second design review.

**Formats**
- Opens `.flp` (also FL's zipped loop packages), `.als`, `.flm` (partial), `.dawproject`, GarageBand `.band` (partial),
  BandLab `.blx` (partial), **tracker modules `.mod`, `.xm`, `.s3m`, `.it`** (partial) and MIDI; exports `.flp` (also as a
  zipped loop package), `.als`, `.dawproject`, **`.flm`** (*Save a copy as FL Studio Mobile project*) and MIDI.
- FL imports: automation clips linked to several controls drive all of them, send-level automation (#253), FL 11 and
  older step grids and parameter numbers, Patcher's Control Surface and X-Y-Z links.

**Plugin parity**
- Eight more Kilohearts laws on Effector (Comb Filter, Chorus, Trance Gate, Phaser, Distortion, Bitcrush, Frequency
  Shifter, Ensemble): Kilohearts Essentials 5.88 → 6.85 over 1,648 test files, and the Nonlinear Filter 4.62 → 9.43.
- FL's slide law on every generator, Sytrus slide velocity and main-page automation, Ogun / Autogun link velocity,
  SimSynth velocity, Harmor automation: FL's *Overcome* demo is within 3 dB of FL in 96.4 % of its windows (before:
  90.5 %).
- Helix: unison on the granular and spectral oscillators, reverse grains, a partials view. Projects saved earlier keep
  their sound.

**Fixed**
- #193, #206, #215, #218 and #222 (follow-ups), #225–#244, #246, #247, #249–#254; discussions #105 (Chord helper part 2) and #202
  (Transistor Bass *Copy as MIDI*).

**From contributors**
- Docked channel and plugin windows have a gear in their title bar that hides the header strip (tabs, presets, the
  channel's controls) and shows it again; the header shows by default and each window keeps its choice with the project.
  PRs #248 and #256 by @FKHR26.
- Channel and plugin windows take the size of the tab they show, as in FL Studio; the Sampler's window keeps one size
  on all its tabs, as FL's does. PR #259 by @FKHR26.
- An FL Studio Patcher cable into the a / b / c knobs of a Fruity Formula Controller now comes along on import. PR #257
  by @codewithamoako.
- Thanks to yannikthegenius, mysteditors26, eugenialoli, kubapl2004gmail, esiowry, subhankardas15071992-cloud,
  HamonTeacher, veinhusk and Jan12139 for reporting.

## 0.1.30 alpha

**Performance**
- While FL's *Thief* demo plays, LYNX SHARE uses 27.5 % of one CPU core (0.1.29: 77.8 %; FL Studio 2026: 59.5 %), and
  *Exoplanet* 45.9 % (0.1.29: 99.7 %; FL Studio: 59.5 %), on one Windows PC in a quiet-machine run. Output unchanged.
- The playhead is drawn on its own layer on Windows and Linux; covered windows stop drawing; every view draws a frame
  with fewer instructions than in 0.1.29; the plugin picker's first open no longer freezes (#209); Helix, SoundFonts,
  DirectWave and exports use less memory.

**New**
- ARA 2 hosting: Melodyne and other ARA 2 plugins edit audio clips in place (clip menu › LYNX extras).
- Polyphonic note editor and polyphonic audio to MIDI (Basic Pitch) for chords and mixed audio.
- Newtone's natural pitch engine with per-note pitch modulation and drift.
- Undo as in FL Studio 2026: Ctrl+Z steps back through the history (#214); Esc closes the focused window (#217).
- FL's Current mixer track, FL's Project picker, Piano roll click-and-hold, the window layout saved with a project.
- New instruments and devices: Helix (wavetable) with Helix+ (granular and spectral oscillators too), Tetra (4-operator FM), and Live's Meld, Erosion, Vinyl
  Distortion, Corpus, Spectral Resonator / Time and Looper as LYNX SHARE devices.
- Chord helper (Piano roll, Simple editor, Clip View), FL's chord strip, MPE (#108), hardware MIDI Out, DirectWave
  editor (#157), Slicex snapping (#172), FL's Browser tabs and favourites, docked Browser (#192), audio clip fade
  handles (#194), FL's graph-editor lanes (#208).
- Clip-launch workflow: return tracks, Sends rows, crossfader, scene follow actions, Live's look for the Session mixer
  and views.
- Simple workflow: BandLab-style Drum Machine, tuner, song fade-out, Controls tab, Loop Browser, loops that fit the
  song's key and tempo.
- Plugin hosting: grouped plugin processes, more CLAP extensions, sample-accurate automation for CLAP and VST3.
- Design and details passes: higher contrast, FL-style menus, Bold text, Text size, and many small behaviours of FL
  Studio, Live, BandLab and FL Studio Mobile.

**Formats**
- Opens `.flp`, `.als`, `.flm` (partial), **`.dawproject`**, GarageBand **`.band`** (partial), BandLab **`.blx`**
  (partial) and MIDI; exports `.flp`, `.als`, **`.dawproject`** and MIDI.
- Instruments and samples: `.dwp` / `.dwb`, `.gig`, Kontakt `.ncw` (#149).
- Audio export: WAV, MP3, FLAC, OGG, **AIFF** and **M4A** (AAC, with the system's encoder on Windows and macOS).

**Plugin parity**
- More than 40 stand-ins refitted against the plugins they replace, among them FabFilter Pro-C 3 (6.30 → 8.60),
  Waves L2 (3.44 → 9.52), Timeless 3 (2.37 → 6.93), Harrison 32C (5.88 → 8.54) and Eiosis AirEQ (5.78 → 8.85), and new
  stand-ins for Waves C4, PuigTec, Q10 (10.00), API-560 and others, and Kilohearts' own laws for five kHs effects.
  Projects saved earlier keep their sound.

**Fixed**
- #108, #127, #138, #149, #154, #157, #164, #165, #170, #172, #177–#189, #191–#195, #197–#201, #203–#209, #211–#215,
  #217–#222,
  discussions #105 and #202. PR #176 by @FKHR26.

## 0.1.29 alpha
- Help › *Go back to the previous version* installs the version you had before; the first audio block after Play
  always starts at the right place.

## 0.1.28 alpha
- The performance release: about 1 s to a ready window, FL's *Thief* opens in 5.2 s with 345 MB, stems in one pass,
  render-ahead, Ableton Link, Freeze and Bounce in the Clip-launch workflow, clip envelopes, FL Studio Mobile factory
  sounds from your own copy of the app, a 16-pad Sampler in the Simple workflow, and the experimental Remote Control
  API by @guike8.
