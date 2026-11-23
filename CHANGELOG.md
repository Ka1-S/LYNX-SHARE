# Changelog

The short version of each release. The full notes, with measurements, known issues and downloads, are on the
[GitHub releases page](https://github.com/Ka1-S/LYNX-SHARE/releases).

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
