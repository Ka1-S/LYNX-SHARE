# Round 168p — designed editors and LYNX preset banks, group C (drums / physical models)

Plugins: Drumpad, Sakura, Fruity DrumSynth Live, BassDrum, Fruit Kick, Drumaxx, Plucked!, FL Slayer.
Reporter (user, 2026-11-02, via 168n): every plugin with a generic knob page gets a real design and good presets.
Method and inventory: docs/analise/REA_rodada168n.md §1 (branch wip/design168n).

## 1. FL evidence: the editor forms of FL's own plugin DLLs [DECOMP: DFM]

Each plugin's editor is a Delphi form (`TSynthEditorForm`, binary DFM "TPF0" in the DLL's resources) whose controls
carry the parameter index in `Tag`, their geometry (`Left/Top/Width/Height`, FL pixels at 100 %), range (`Min/Max`),
default and hint name. Dumped with our own DFM reader (tmp/rea165/dfm.py; summaries tmp/r168p/sumdfm.py, outputs in
tmp/r168p/dfm/<plugin>/TSynthEditorForm.dfm.txt). Only layout and control facts are used; no artwork is copied.

| Plugin (DLL) | Form offset | Panel size | Controls (by Tag = parameter index) |
|---|---|---|---|
| Fruit Kick (Fruit Kick_x64.dll) | 0x4106fc | 380 × 80 | MAX 0 (−2400…3600), MIN 1 (−1200…1200), DECAY 2 (0…256) in one bevel; AMP DECAY 3 (0…256), CLICK 4 (0…64), DIST 5 (0…128) in three bevels; below it FL's channel envelope frame (TInsEditorFrame) |
| Plucked! (Plucked!_x64.dll) | 0x385164 | 360 × 156 | Decay 0 (0…256, 88 px wheel), Color 1 (0…128, 88 px), NORM 2 / GATE 3 / WIDEN 4 pad buttons, "PLUCKED!" title |
| Drumpad (Drumpad_x64.dll) | 0x3e2a7c | 794 × 225 | Mallet 2–5 + retrig 0; membrane 6–12; LOW 13/14; SOF 15–17; MID 18–23 + retrig 1 (3-way); HIGH 24–26; Hold 28, Lo-Fi 27, Pitch 29; 4 velocity-mod slots: source 30–33 (combo), value 34–37 |
| BassDrum (BassDrum_x64.dll) | 0x3dba88 | 389 × 420 | Output 0, Sampling 1, Duration 2, Sub bass 3, Drive 4, Attack 5, Base 6, Peak 7, Slide 8, Var. base/peak/slide/phase 9–12, Click cut/freq/level 13–15, Noise A/D/S 16–18, start 19, pitch 20, mix 21, filter 22; noise sample ◀ combo ▶ |
| Fruity DrumSynth Live | 0x3f2c9c | 580 × 358 | WAVE (select 15 −1…17, tune 16, mix 17, vel→tune 29), TRIG (voice mode 24, count 18, rate 19, decay 20, vel→tone 30, tone 2, click 23), OSC 1 (freq 4, width 5, vel→freq 25, harm 3, dec 7, sweep 6, noise-mode 32, invert 22, vel→mix 26, mix 8, vel→harm 31), OSC 2 (freq 10, width 11, vel→freq 27, ring 33, att 12, dec 13, shape 9, rel 21, vel→mix 28, mix 14), MAIN OUT pan 1 / vol 0, SEND TO num 36 / level 35, SELECTED patch, MORPH TO 34 + mod 37, the patch keyboard |
| Sakura (Sakura_x64.dll) | 0x401490 | 855 × 532 | Exciter / Hi-Cut 26–33, Lo-Cut 24/25; String 1 6–10, String 2 11–15 + ratio 111; comb 19–23; string mix/stereo/saturation 16–18; Amp ADSR 2–5; resonators freq 39–46 / fdbk 47–54 / on 55–62; master 0, tune 1, transpose 113; EG 34–37, LFO 38, matrix 8 × (source 79+4k, amount 80+4k, negative 81+4k, destination 82+4k); delay 63–69 + sync 112, chorus 70–73, reverb 74–77 |
| Drumaxx (Drumaxx_x64.dll) | 0x41964c | 762 × 614 | 16 pads (2 banks of 8): volume, pan, mute, solo, key (pad·44 + 0…4); the selected pad's synth = Drumpad's controls at pad·44 + 5…42; EQ on 707 + 8 bands 708–715, limiter on 716, gain 717, release 718, swing 706, master 705; the pattern sequencer (not modelled in LYNX) |
| FL Slayer (FL Slayer_x64.dll) | 0x576e04 | 396 × 404 | strings 0, coils 1, pickup 2 (slider), tone 3, slap 4, fret 5, harmonic 6, damp 7, vel vol/damp 8/9, amp type 12, drive 13, presence 14, feedback 15, EQ 16–18, cabinet 19, FX 20 + par 21/22, mode 24, timing 25, latch 26, glissando 27, pitchbend 2 28 |

## 2. Plan
- Layouts (src/core/PluginLayoutDrums.inc, included by PluginLayout.cpp): FL's arrangement per plugin, LYNX style.
- Knob tables for every control above (src/ui/ChannelSettings*.cpp, own blocks only), automatable.
- LYNX banks (src/core/banks/Bank_<engine>.cpp) for the engines that run without FL's files: Fruit Kick, Plucked!,
  Drumpad, Sakura, Drumaxx (built states), DrumSynth Live (synth part; recorded waves need FL's DLL). BassDrum
  (FL's sounddata table) and FL Slayer (Slayer_Engine.dll) say "needs FL Studio's files" when FL is absent.

## 3. What was built
- **Pages** (`core/PluginLayoutDrums.inc`): the eight engines in FL's arrangement (§1 positions, LYNX skin). Drumaxx:
  two pages of eight pads (pad button + name, volume, pan, mute, solo), the selected pad's Drumpad voice below, EQ and
  limiter; DrumSynth Live: the patch knobs of the key picked on the 120-key display (4 rows of 30, keys with a sounding
  patch lit), its send / morph targets shown read-only (FL's digit wheels).
- **Automation** (`core/DrumControls`, `core/DrumParamRanges.h`, `core/ModelParamList.inc`): one table per engine
  by the engine's own parameter index (Drumaxx 685 = 16 × (4 + 38) + 13, DrumSynth Live 4 320 = 120 keys × 36);
  every knob's right-click menu makes an automation clip; every engine implements `setParameter (index, 0…1)` on the
  running engine. A knob turned while notes sound no longer restarts Fruit Kick or BassDrum (live parameter queue);
  Plucked!, Drumpad, Sakura, Drumaxx, DrumSynth Live and Slayer were already live.
- **Fixes found on the way**: DrumSynth Live's voice-mode menu listed "Poly, Mono" — the engine (REA_rodada30 §1.3:
  0 = Mono, 1 = Poly) is the other way round; a fresh Plucked! channel (empty state) showed no knobs — it now shows
  the engine's defaults and gets a state on the first edit.
- **Banks** (`core/DrumBanks.cpp`, values from the engines' laws REA_rodada28 §1–2, REA_rodada30 §1.4–1.5,
  REA_rodada43 §1; checked by rendering with `tools/bankaudit`): Fruit Kick, Plucked!, Drumpad, Sakura, Drumaxx kits,
  DrumSynth Live kits. BassDrum (FL's `sounddata`) and FL Slayer (`Slayer_Engine.dll`) need FL's files: no bank;
  BassDrum's noise display says "Needs FL Studio's files".

## Pictures
- /mnt/e/lynx-work/design168p/before/<engine>-before.png: main's exe (b7966651), `--snapshot` of the same project, channel
  frame; drumsynth-before-kit.png with LYNX's first DSL kit.
- /mnt/e/lynx-work/design168p/after/<engine>-lynx-<100|150|200>.png (`--instrumentshot … lynx`), bassdrum/flslayer-fl-*
  (`… fl`, FL's files), drumsynth-after-kit.png (`--snapshot`, DSL is not in the instrument list: it comes from .flp files).

## Where I stopped
- done: §1, layouts, tables, live automation, part selection, displays, banks (66 presets, 0 flagged), translations,
  shot review fixes (c940e1b8), before/after shots, notes in coord/notes-0.1.23.md, merged origin/main 8a788038 (d687b456).
- Linux suite (separate-object build, mkchunk.sh): 15 failures, all Kepler (kepler_chorus_like_the_dll energy
  343.102312086 vs 343.102284005, + Kepler165k / KeplerExo167d bits) and multiband_delay_exact (tests.cpp:11543). They
  fail deterministically, also alone (`LYNX_TEST=multiband_delay_exact`), while every other round's build-local.sh build
  (168g, 168o, 168n wip, 168j, pr31) gets the exact values. Coordinator: not "known" — must be 0 before landing.
  Checked without building (paused): every object is newer than all its dependencies (.d files); Kepler.o and
  MultibandDelayFx.o have the same machine code as 168g's; no object defines a libm function; no FP-state change
  (MXCSR / fesetround / denormals) in the branch's diff; no clash of exported names between the new objects and
  Kepler / IldspMath / MultibandDelayFx.
  Cause (168w, confirmed here): zig c++ -O2 does full LTO when it compiles and links in one invocation (build-local.sh);
  mkchunk.sh's `-c` objects are native ELF (no LTO), and without LTO exactly these 15 x87 bit-exact goldens fail.
- A corrupt build-local/obj168p/src_core_Harmor.cpp.o made ld.lld segfault (found by bisecting the link); rebuilt.
- PAUSED 2026-11-02 (coordinator, for round 168w's faster pipeline). Post-merge (d687b456) tests build linked; the suite run
  finished: 2132748 checks, 15 failures = only the known Kepler / multiband-delay bit-exact checks (tmp/r168p/tests4.log).
- exact next step on RESUME: build this branch the official way (`coord/bl-wt.sh design168p` or 168w's new pipeline, with LTO)
  and confirm 0 failures; only if Kepler / multiband delay still fail, bisect origin/main..HEAD (first: the 168n wip
  merge fb691f7e vs my own commits). Land only with 0 failures. Then, once 168n is on
  main: `git merge origin/main`, and land via `flock ~/projetos/lynx/.git/lynx-merge.lock coord/land.sh design168p`
  (in parts if land.sh asks), record the hashes in the final report.
- left: a DrumSynth Live channel with no state at all (not reachable from the browser) shows the page with knobs missing;
  FL Slayer's Type / Coils steppers show numbers (pre-existing layout).
