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

## 2. Toxic Biohazard and PoiZone (this round's own items)

| | Toxic Biohazard | PoiZone |
|---|---|---|
| Reporter's words | user: "Toxic Biohazard e etc… está sem nenhum design, tem que fazer uns presets legais"; YouTube review (168d #13): "missing the whole FM grid" | YouTube review (168d #14): "doesn't look like PoiZone, I don't even know what you can do in here" |
| FL evidence | Parameters: the engine's own state layout already reproduced in src/core/Toxic.cpp (REA of Toxic Biohazard_x64.dll, earlier rounds): indices 0–156 (6 oscillators × envelope / wave / pitch / detune / offset / phase / free run, FM matrix 109 + 6·(dest−1) + (src−1), mix, pan, LFO 1 / 2 amounts per oscillator, master envelope, LFO 1 / 2, filter), 227–293 (effect slots, EQ bands 62 Hz … 8 kHz, configuration), 295–315 (pitch-mod and misc). Layout: FL's window as drawn in the installation's own `Plugins\Fruity\Generators\Toxic Biohazard\Artwork\background.tga`, viewed only as a reference for where each section sits (CONFIGURATION / MASTER ENVELOPE top, six oscillator panels in two columns, MOD panel with the 6 × 6 grid in the middle, LFO 1 / FILTER / LFO 2, EFFECT 1 / EQ / EFFECT 2 at the bottom). No pixel of it is copied. Effect-time sync labels come from `ToxicFx`'s syncFactor table. | The plugin's own Delphi form `TSynthEditorForm` (RCDATA resource in `PoiZone_x64.dll`, read with REA `inspect_pe_resources` and parsed by tmp/r168n/dfm.py → tmp/r168n/poizone_form.txt): every control's Left / Top / Width / Height and its `Tag` = parameter index (0–73, 75–80). LYNX uses FL's x positions and order; rows re-spaced for LYNX's section titles. Parameter ranges from src/core/PoiZone.cpp's engine defaults. |
| LYNX before | Generic page: a short list of knobs (no FM grid, no effects / EQ), not automatable; without FL's files the engine was not offered at all. | Generic page of 20-odd knobs in one grid, no sections; no presets without FL. |
| LYNX now | Designed 920 × 668 page in LYNX's style with every parameter of the engine (> 150 controls): the full FM grid (row modulates column), MIX / PAN / LFO rows, oscillator panels with wave pictures (or "Needs FL Studio's files" when the waves can't be read), filter, LFOs, both effect slots (knobs shown by effect type) and the EQ. Every control automatable (right-click menu, Song automation targets, live changes without restarting the voices). A LYNX bank of 26 own presets (Init + Bass / Lead / Pad / Pluck / Keys / FX), cycled by the ◀ ▶ arrows, offered also on PCs without FL (waves 0/1, which need no FL file). | Designed 856 × 378 page: unison / mode / master, MOD WHEEL, LFO, ENVELOPE, AMP, the two oscillators, FILTER, ARPEGGIATOR, DELAY, CHORUS and the 16-step GATE; every control automatable. A LYNX bank of 24 own presets (Init + Bass / Lead / Pad / Pluck / Keys / FX); PoiZone needs no FL file, so it works everywhere. |

### 2.1 Shared infrastructure (for 168o–168r)
- `src/core/EngineParams.h`: `EngineParam {index, name, low, high, stepped}` + `LiveParamQueue` (lock-free SPSC for parameter changes the audio thread applies without rebuilding the engine).
- `src/core/ModelParams.{h,cpp}` + `ModelParamList.inc`: an engine registers its parameter table once; the channel window, automation (`channelParam` targets in Song.cpp, RackRenderer binding) and tests use it by name.
- `src/core/LynxBanks.{h,cpp}` + `LynxBankList.inc`: LYNX's own preset banks per engine (`Preset {name, category, tags, apply}`), listed by the channel window's preset bar ("LYNX/<category>/<name>", pseudo paths `lynxbank:<i>`) and used by `natives::makeChannel` when FL's files are absent.
- `PluginLayout` additions: `tabs` / `pageTabs` pages, `menu` items, `value` read-outs, `whenParam` conditions, `display "toxicwave"`.
- Tools: `build-local/bankrender <dir>` renders every bank preset (a C3 chord; basses one C2, leads one C4) to WAV and prints peak / RMS / spectral centroid / tail; `LYNX.exe --instrumentshot <dir> <engine,...|banks> [lynx|fl]` pictures the channel windows at 100 / 150 / 200 %.

## 3. Checks
- Levels (build-local/bankrender, /mnt/e/lynx-work/design168n/bank-levels.txt + WAVs): every preset peaks at −3.5 dBFS or lower
  (PoiZone's Init, the engine's own default, at −0.4); 5 PoiZone and 9 Toxic presets were re-levelled after the first
  measurement (PoiZone Supersaw / Soft Pad were clipping at +0.8 dB). No preset is silent (the quietest, plucks and FX, have
  RMS around −35 to −40 dBFS because they decay).
- Pictures (/mnt/e/lynx-work/design168n/): `<engine>-before.png` (origin/main build, `--snapshot` of the same project) and
  `after/<engine>-<lynx|fl>-<100|150|200>.png` (`--instrumentshot`). The first after-shot showed two clipped captions
  (Toxic's LFO "Sync" over "Speed", "Vel curve" cut): fixed (LFO row re-spaced; small knobs' captions get 52 units).

## Where I stopped
- done: inventory (§1); Toxic + PoiZone designed pages, LYNX banks, model-knob automation, shared infra (§2.1); tests;
  shots; levels. Landing the whole branch via land.sh (2026-11-02).
- next: after landing, re-shoot with the landed exe (`LYNX.exe --instrumentshot E:\lynx-work\design168n\after2 toxicbiohazard,poizone lynx`),
  notes-0.1.23.md bullets, message the coordinator the main hash.
- not covered: Toxic's MIDI / SEQ pages (its sequencer isn't modelled), CC slots 316–339, the Browser listing of
  LYNX banks, effect banks; model knobs don't follow automation while playing.
