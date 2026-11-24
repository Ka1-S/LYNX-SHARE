# REA round 190qo: typing keyboard layouts (#270), slide-note velocity on FL's generators (#271), slide note marks (#272)

Clean-room: FL Studio 2026 run on this PC (command-line `/R /Ewav` renders, no input; one short GUI session under the
GUI and FL locks for the menu and Piano roll captures) and FL's own data files read as data. No FL code or artwork copied.
Evidence: `/mnt/e/lynx-work/issues270_190qo/` (`slide/`, `shots/fl/`, `py/`).

## 1. #271: which FL generators follow a slide note's velocity

Test projects: FL's own render tests (`Data/System/Render tests/<generator>.flp`, PPQ 96, 120 BPM) with their notes
replaced (`py/mkslide.py`), one channel each. Cases: `s100` one note of 384 ticks at velocity 100; `s10` the same at 10
(the reporter's 8 %); `vo` velocity 100 + a slide note on the same key at tick 96, 96 ticks long, velocity 10; `vi` the
reverse. FL's SoundFont Player has no render test: `py/mksf.py` takes the Harmless render test and puts FL's own
"Fruity Soundfont Player" wrapper event (from `Data/System/SoundfontPlayerTrack.fst`) and a version-6 state
(REA_rodada158 §1) with `%FLStudioFactoryData%\Data\Patches\Soundfonts\STR_Ensemble.sf2` in its place.

Level after the slide (ticks 200..380, dB RMS), `slide/fl-classify.txt`:

| Generator | s100 | s10 | vo | vi | FL |
|---|---|---|---|---|---|
| SoundFont Player | -29.0 | -44.0 | -44.0 | -29.0 | follows |
| 3x Osc | -18.9 | -50.2 | -50.2 | -18.9 | follows |
| Sampler | -17.9 | -49.2 | -49.2 | -17.9 | follows |
| Autogun | -31.1 | -62.3 | -61.9 | -31.1 | follows |
| BeepMap | -15.3 | -46.8 | -46.7 | -15.3 | follows |
| BooBass | -34.2 | -65.6 | -65.6 | -34.2 | follows |
| Fruit Kick | -88.7 | silent | silent | -88.7 | follows |
| DX10 | -27.8 | -59.1 | -59.1 | -27.8 | follows |
| Granulizer | -38.6 | -69.9 | -69.9 | -38.6 | follows |
| Harmor | -12.3 | -43.6 | -43.6 | -12.3 | follows |
| Patcher (3x Osc inside) | -11.2 | -42.5 | -42.5 | -11.2 | follows |
| Wave Traveller | -22.2 | -53.6 | -53.6 | -22.2 | follows |
| FLEX | -27.1 | -54.2 | -50.3 | -29.1 | follows (86 / 93 %) |
| GMS | -21.9 | -41.9 | -21.9 | -41.9 | does not follow |
| PoiZone | -24.4 | -44.4 | -24.4 | -44.4 | does not follow |
| Sakura | -24.1 | -28.6 | -24.1 | -28.6 | does not follow |
| Sawer | -27.1 | -47.0 | -27.1 | -47.0 | does not follow |
| Drumpad | | | | | velocity does not change its level |
| BassDrum, Drumaxx, DrumSynth Live, Slicer, Slicex | | | | | silent before the slide (n/a) |
| FL Keys | | | | | the render test plays nothing at key 60 (n/a) |

Earlier rounds: Sampler, 3x Osc, DirectWave, SimSynth, Harmless, FPC follow, MiniSynth does not (190od); Sytrus follows,
Ogun / Autogun follow with link velocity (190pi).

The generators that follow move the level by FL's host Final.Vol: velocity 10 sits 31.3 dB under velocity 100 =
20·log10(Init.Vol(100/128) / Init.Vol(10/128)), Init.Vol(v) = (11^(1.28·v) − 1) / 10; the slide moves it in the per-tick
steps of 190od's law. The SoundFont Player moves its own velocity (its SoundFont velocity curve: 15 dB between 100 and 10
for this font) the same way: `vo` reaches `s10`'s level at the slide's end and stays there.

LYNX before: the SoundFont, DX10, BeepMap, Granulizer, Fruit Kick, Wave Traveller ports kept the note's level through a
slide; LYNX's BooBass ignored the velocity altogether (every note at velocity 100's level, 31 dB too loud at 10).
LYNX now (engine revision 5, new channels and .flp imports; older projects keep their revision and sound): those ports
set the voice's Final.Vol (pan: Final.Pan) from the slide's steps; BooBass plays Init.Vol of the velocity at note on and
follows slides; the SoundFont voices recompute their velocity gain and velocity → cut-off. GMS, PoiZone, Sakura and Sawer
stay as they are (FL's don't follow either). Not checked (no FL render test that sounds after the slide): Toxic
Biohazard, Morphine, Kepler, Plucked!, Transistor Bass, FL Keys, the drum generators.

### 1b. The SoundFont Player's velocity law

FL's SoundFont Player at velocities 1, 5, 10, 20, 32, 40, 50, 64, 80, 100, 115, 128 (`sfvel/`, STR_Ensemble.sf2; Default.sf2
at 10 / 64 / 128): the level is 15·log10(v / 128) dB from velocity 128, i.e. (v / 128)^0.75, within 0.05 dB on both fonts.
LYNX's LYNX Soundfont used the SF2 default modulator curve, 40 dB a decade: equal to FL at velocity 100, 24.8 dB too quiet
at 10, 49.7 dB at 1, 2.8 dB too loud at 128. From engine revision 5 the LYNX Soundfont (SF2 default curve chosen) plays
FL's law, scaled so velocity 100 keeps its level; a slide note's velocity follows the same law. Default.sf2 also shows a
level offset of about +2.4 dB at every velocity that STR_Ensemble does not have (an absolute level question of that font,
not of the velocity; left as it is).

### 1c. Patcher

FL's Patcher render test (a 3x Osc inside) follows the slide velocity as 3x Osc does; LYNX's Patcher did not pass the
channel notes' properties to the instruments its "From LYNX SHARE" Notes pin reaches. It does now (engine revision 5).

## 2. #270: FL's typing keyboard layouts

FL 2026's right-click on the typing keyboard button (`shots/fl/270-fl-typing-keyboard-menu.png`): two columns — Root Note
C..B, Octave 2..6, Options › Reset; then Layout: the 31 `.scr` files of `System/Config/Typing to piano` in name order.
LYNX had the same items in one column; it now breaks the column as FL does.

FL ships "+ Chords" and "Chordmap" layouts for Major and Minor Natural only. Their rule (checked key for key, `KeyMap.cpp`
`chordRuleCheck`, a test and a `--selftest` item):
- "+ Chords": the scale's own layout, its number row the scale's triads in root position, degree k on key k from the root
  at MIDI 60 (key 1 = I, key 2 = ii ... key = = the 12th degree).
- "Chordmap": the Q row the triads of degrees 1..7 then 1..5, each chord's three notes placed in the 12 semitones from
  the scale's note below the root at 60 (Major: B4..A#5); the number row the same chords with the lowest note an octave
  up; the A row with the highest an octave down; the Z row the scale from MIDI 36.
LYNX builds both for FL's other seven-note scales ("More scales with chords (LYNX)"). FL's text file
`01 Typing Keyboard Layouts Explained.txt` documents the format of user-made `.scr` files; LYNX reads them from FL's
folder (an FL install found on the computer) and from `<LYNX settings>/Typing to piano`, sub folders as sub menus.
Format: one line per key in PC scan-code order from 0x02, FREE / LOCK, MIDI numbers or names with FL's octaves
(C5 = 60), chords with commas.

## 3. #272: slide notes in the Piano roll

FL 2026 (`shots/fl/272-fl-pianoroll-slides.png`, `272-fl-slide-mark-x8.png`): a slide note is drawn as any note, with a
7 x 7 px right triangle in it, right angle at the bottom right (◢), centred in the 15 px note's height, its right side
2 px in from the note's right edge, dark on the note colour. Note length and position are the note's own (the data
matches the .flp import). LYNX drew a ▶ wedge 7 px wide over the note's whole height; it now draws FL's ◢. FL's Piano
roll View menu has no option for how slide notes are drawn (`272-fl-pianoroll-view-menu.png`).
