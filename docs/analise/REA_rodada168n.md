# Round 168n — designed editors and LYNX preset banks for the engines that still show a generic knob page

Reporters: the user (2026-11-02): "Toxic Biohazard e etc… está sem nenhum design, tem que fazer uns presets legais";
YouTube review (168d #13/#14): Toxic Biohazard "missing the whole FM grid", PoiZone "doesn't look like PoiZone, I don't
even know what you can do in here".

## 1. Inventory (2026-11-01, origin/main e6e36fdd)

Method: a plugin has a *designed* editor when `plugui::instrumentLayout` / `plugui::layoutFor` (src/core/PluginLayout.cpp)
returns an arrangement, or when it has its own editor class (FacetEqEditor, ProEditors, NewtoneEditor, FlExtraEditors,
VisualEditors, ControllerEditors, SoundFontEditor, WaveTravellerEditor, Layer/Patcher/Voltage/VFX editors). Everything
else gets the generated grid (`generated (kind)` for effects, the 7-column knob grid of `ModelPanel` for instruments).

Usage = FL plugin instances (event 0xC9) / files in FL 2026's 165 shipped .flp projects (`Data\`, list in
tmp/allflp.txt; script tmp/r168n/count.py, output tmp/r168n/counts.txt) and the number of FL factory presets
(`Data\Patches\Plugin presets\Generators\<plugin>`). Instruments with a layout today: 3x Osc, FL Keys, BooBass, Sytrus,
Harmor (5 of ~30).

### 1.1 Instruments with the generic page

| Engine | FL instances / files | FL presets | Runs without FL files? | Priority | Group |
|---|---|---|---|---|---|
| Harmless | 160 / 25 | 430 | yes (own) | 1 | B |
| Ogun (+ Autogun 9 / 9) | 23 / 7 | 229 | Ogun needs a preset; Autogun needs SG.dat | 2 | B |
| Fruity DX10 | 11 / 6 | 59 | yes (own) | 3 | B |
| MiniSynth | 11 / 2 | 92 | no (oscdata.dat) | 4 | B |
| Drumpad | 10 / 2 | 961 | needs a preset | 5 | C |
| GMS | 9 / 5 | 306 | no (waves) | 6 | B |
| Fruity DrumSynth Live | 8 / 1 | 2 | no (recorded waves) | 7 | C |
| Morphine | 7 / 4 | 300 | no (noise sample) | 8 | B |
| Sakura | 5 / 4 | 501 | needs a preset | 9 | C |
| BassDrum | 5 / 4 | 52 | no (tables) | 10 | C |
| Kepler / Kepler Exo | 4 / 3, 0 | 277 / 188 | no (engine DLL) | 11 | D |
| **Toxic Biohazard** | 3 / 2 | 791 | **yes with LYNX's own spectra** (waves 2–32 need the DLL) | **user + review** | **A (168n)** |
| Fruit Kick | 3 / 3 | 8 | yes | 12 | C |
| **PoiZone** | 1 / 1 | 385 | **yes (self-contained engine)** | **user + review** | **A (168n)** |
| SimSynth | 1 / 1 | 73 | yes (own) | 13 | B |
| BeepMap | 1 / 1 | 7 | partly | 14 | D |
| Fruity Granulizer | 1 / 1 | 8 | yes (needs a sample) | 15 | D |
| Drumaxx | 1 / 1 | 131 | needs a preset | 16 | C |
| Plucked! | 0 | 1 | yes (own) | 17 | C |
| Sawer | 0 | 421 | no (data.saw) | 18 | D |
| FL Slayer | 0 | 64 | no (Slayer_Engine.dll) | 19 | C |
| Transistor Bass | 0 | 3 | no (engine_x64.dll) | 20 | D |
| Wasp / Wasp XT, Fruity Dance | 0 | 47 / 40, — | placeholders (no sound) | 21 | D |

### 1.2 Effects with the generated page (FL plugins first, then LYNX's own simple built-ins)

| Effect (LYNX kind) | FL instances / files | Priority | Group |
|---|---|---|---|
| Pitcher (pitchCorrect) | 61 / 17 | 1 | E |
| Fruity Love Philter (lovePhilter) | 43 / 18 | 2 | E |
| Fruity Peak Controller (peakController) | 36 / 27 | 3 | E |
| Spreader (spreader) | 33 / 9 | 4 | E |
| Fruity Fast LP (fruityFastLP) | 17 / 2 | 5 | E |
| Frequency Shifter (flFrequencyShifter) | 5 / 4 | 6 | E |
| Low Lifter (flLowLifter) | 4 / 3 | 7 | E |
| Distructor (distructor) | 4 / 3 | 8 | E |
| Fruity Bass Boost (flBassBoost) | 2 / 1 | 9 | E |
| Fruity Delay (flDelay1) | 1 / 1 | 10 | E |
| Fruity 7 Band EQ, Fruity Center, Fruity Phase Inverter | 0 | 11 | E |
| LYNX built-ins: EQ, Delay, Reverb, Limiter, Filter, Distortion, Chorus, Utility, Lo-fi, Crossover, Pitch shift, Frequency shifter, Rhythmic envelope, Step gate (volumeGate) | — (LYNX's own / substitutes) | 12 | E |

(Own editors already, not generic: Facet EQ, Pro editors, Newtone/Newtime, Big Clock, Tuner, HTML NoteBook, Scratcher,
Wave Candy, ZGameEditor Visualizer, X-Y / X-Y-Z controllers, Control Surface, Formula Controller, Patcher.)

### 1.3 Groups for parallel rounds
- **A (this round, 168n):** Toxic Biohazard, PoiZone — designed editors, LYNX preset banks, presets usable without FL,
  automation of their controls.
- **B (synths):** Harmless, Ogun + Autogun, Fruity DX10, MiniSynth, GMS, Morphine, SimSynth.
- **C (drums / physical):** Drumpad, Sakura, Fruity DrumSynth Live, BassDrum, Fruit Kick, Drumaxx, Plucked!, FL Slayer.
- **D (FL-DLL engines / placeholders):** Kepler, Kepler Exo, Transistor Bass, Sawer, BeepMap, Fruity Granulizer, Wasp /
  Wasp XT / Fruity Dance placeholders.
- **E (effects):** the table of §1.2.

## Where I stopped
- done: inventory (§1).
- in progress: Toxic Biohazard editor (FL's layout read from the installation's own Artwork\background.tga as a
  reference only — nothing copied), then PoiZone, then LYNX banks.
