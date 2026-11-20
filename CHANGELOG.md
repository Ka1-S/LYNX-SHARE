# Changelog

The short version of each release. The full notes, with measurements, known issues and downloads, are on the
[GitHub releases page](https://github.com/Ka1-S/LYNX-SHARE/releases).

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
