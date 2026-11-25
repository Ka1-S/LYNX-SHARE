# Round 190rh — FL's native engines without FL Studio installed (QUEUE-0.1.33 item 78)

Owner, 2026-11-25: "It makes no sense to have everything if people can only use it when they've installed FL." LYNX's
engines were rebuilt long ago; what was missing without FL is the data some of them read at run time, and a start state.

Clean-room rules of this round: nothing of FL's (tables, cycles, recordings, impulses, presets) is copied, converted,
resampled, fitted, embedded or shipped. LYNX's sets are computed from textbook formulas and public literature, cited per set.
Constants come from there, not from FL's stored data. The one thing adjusted is a level per set, from **rendered audio** (a
held note through the engine, whose output with FL's data is the plugin's: earlier rounds' EXEC checks); a spectrum tilt would
be allowed from the same audio and is left at the textbook value so far. Bank presets are designed for LYNX on the
documented state layouts; no FL preset file was read for them.

(The first version of three sets in this round, before review, had constants chosen while looking at the stored data. They
were thrown away and the sets rebuilt as described here; nothing of them is in the code.)

## 1. Which set a channel plays (`core/NativeData.h`)

`ChannelDef::samplePath` is FL's file, or `lynxdata:<engine>/<revision>` = LYNX's own set. Loading or saving never changes
it. FL's file not where the project says: the same file of this computer's installation (`locate`: from
`Plugins\Fruity\` on, names ignoring case, never outside the installation's folder); else LYNX's own set plays, the
project keeps FL's path and the channel window says so. The channel ▸ menu switches between the two sets (undoable).

Cases: shared sessions and packages (`SessionPack::confine`) keep reserved paths and re-root FL data files to the
receiver's installation; remote document edits may name a reserved path (it is no file); a copy for a release before
this one gets a report line (the channel is silent there); `.flp` export is unchanged (these paths are never written).

## 2. Transistor Bass — own "303 Pulse" cycles, revision 1

What FL has: 57 + 55 single cycles in the engine DLL (REA_rodada57 §2.3). What LYNX computes (`bassPulseCycles`): one cycle
per semitone from C0, 192 kHz × 57 and 44.1 kHz × 55, so the engine's "shortest cycle longer than the period" selection
works alike.

- Shape: Open303's TB-303 square (R. Schmidt, `rosic_MipMappedWaveTable.cpp`, `fillWithSquare303`, MIT licence,
  https://github.com/RobinSchmidt/Open303): −tanh (G·saw + O) with `tanhShaperFactor` = 36.9 dB, `tanhShaperOffset` = 4.37.
  The duty cycle follows from them (about 53 %) and does not move with the pitch. The saw is the engine's own falling
  one, so the two line up when mixed (Open303's phase shift does that for its saw). That one transistor stage saturates on
  the saw, with one slow and one sharp edge, is also how the KVR thread on the square describes it
  (https://www.kvraudio.com/forum/viewtopic.php?p=3732152).
- Sag: a first-order coupling high-pass at 10 Hz in its steady state, the order of the corners used in the Open303 thread
  (https://www.kvraudio.com/forum/viewtopic.php?p=3876764, 10 Hz and 80 Hz). It makes the low notes sag more. Sources say
  the real square's shape depends on the charge of a capacitor and so on the pitch, but give no measured law: a duty law
  was not invented.
- Level 0.88, from rendered audio (filter open, the pulse alone, engines V1 / V2, keys 36…84): LYNX's cycles are then within
  −0.7…+0.5 dB on the keys from 60 up and up to 2.3 dB louder below (the pitch dependence that is not modelled).

Engine on FL's cycles vs LYNX's, the bank (44.1 kHz, three notes): the 21 presets without "303 Pulse" bit-identical; the three
with it: level +0.60 / 0.00 / −0.23 dB, residual −9.8 / −11.3 / −4.2 dB. Similar, not the same; the texts say so. None of FL's
128 factory programs uses the mode (REA_rodada57 §2.1).

Bank: 24 presets (`core/TransistorBassBank.cpp`), two with a sequence that follows the played key. The state writer
(`transistorBassWriteState`) now fills the fields around the patch with neutral values of the documented layout (C5, empty
steps, 120 BPM off, no shuffle, zero editor bytes); they followed a saved state before.

## 3. MiniSynth — own oscillator, revision 1

`fallingSaw`: the Fourier series of a falling saw, 1/(πh) and a quarter turn, for the 1024 harmonics the engine takes. Level
0.865 from rendered audio (a held note, three keys: within 0.1 dB). The plugin's own saw is duller than a textbook one (rendered
octave bands: about 5 dB at 4–8 kHz), so LYNX's is brighter; the tilt is not adjusted. The bank's 24 presets, engine on FL's table
vs LYNX's: level −0.6…+3.3 dB, residual −3.9…−12.7 dB (median −10.4). Bank: `core/MiniSynthBank.cpp`, on the .flms layout
(REA_rodada41 §1.1).

## 4. Sawer — own cycle, revision 1

`sawCycle`: a falling saw of 2048 harmonics over 8192 points, faded by a raised cosine. Level 0.507 from rendered audio (the
constructor's defaults, filter open, keys 36…84: about 0.5 dB).

State (DECOMP, the engine's loader `FUN_180034770` and writer `FUN_180034660`, Ghidra): tag + 96 floats + a block of N bytes (N
from the plugin's parameter manager) + a 128-byte name. The loader copies the block only when at least N bytes remain
and the name only when at least 128 do; more than 0x280 bytes is read as a bank with a header. A state that ends after the 96
floats is therefore valid by the loader's code and keeps the constructor's map and "<noname>" (to be confirmed in FL 2026).
Bank: 24 presets (`core/SawerBank.cpp`).
Engine on FL's cycle vs LYNX's, rendered: octave bands 125 Hz…8 kHz within −1.6…+1.8 dB of the plugin's after the level (measured
before the level was rounded, plain saw, five keys); the bank's presets within −0.0…+1.3 dB in level. The waveforms are not
aligned sample by sample (different phases), so a residual says nothing here.

## 5. BassDrum — own table and noises, revision 1

The oscillator table is a sine (1024 points), as a kick oscillator's, at level 0.856 from rendered audio (the bank's kicks). The noises (`drumNoise`) are generated: white noise with a
decay ("LYNX Hiss"), white noise held at 3.7 kHz and 16 steps ("LYNX Crush"), six square waves at the TR-808 cymbal
oscillators' frequencies through a 3 kHz high-pass ("LYNX Metal"; the frequencies are the service notes', as published in K.
Werner, J. Abel, J. Smith, "The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model", ICMC 2014) and noise in
a 1 kHz band under three bursts 10 ms apart ("LYNX Clap", the analogue clap circuits' principle). No constant was taken from FL's
table or samples. (The round's analysis had looked at both; the choice of a sine and of the noises is the textbook one.) A state made with FL's noises plays the one its name suggests (metal, clap, crunch / bit),
else the hiss. Bank: 20 presets (`core/BassDrumBank.cpp`). Differences from FL: the body is close (a sine), noise layers differ.

## 6. GMS — 29 waves, revision 1

`core/NativeWavesGms.cpp`. The file names a preset may ask for are part of the state layout (REA_rodada12); the shapes under
those names are LYNX's: plain waves from their Fourier series, and formants, organ registrations, FM pairs and combs from
formulas. Levels are set by rendered audio. FL's `.wav` folder is used whenever it is there.

## 7. Sytrus, Ogun, Morphine

Sytrus and Ogun need no FL data: only a bank was missing. Morphine's engine needs FL's noise table for its noise section only;
LYNX's is a 16-bit white noise from a linear congruential generator, used when the channel's path says so or FL's file is not
on this computer. All three banks are written from the documented parameter layouts by name, never from an FL preset file.

## 8. Kepler, Kepler Exo — revision 1

Three tables only: the LFO cycle (a triangle), the noise (the 16-bit generator of section 7, level from rendered audio) and the
band-limited impulse rows, which the engine already computed (a 16th-order Butterworth response). Everything else is the engine's own code. FL's power tables are loaded only from known builds; once FL's engine has been
loaded in a process, LYNX's own output can differ in the last bits (the tests allow 1e-4).

## 9. Toxic Biohazard, Fruity DrumSynth Live — revision 1

Toxic's waves 2…32 are families of additive spectra (plain waves, duller saws, organ stops, vowels, bells, combs, rough), all
scaled to one loudness, the mean level from rendered audio. DrumSynth's eighteen waves are the published TR-808 oscillator
frequencies (Werner, Abel, Smith, ICMC 2014), their cowbell pair, and filtered noises; the engine's own table still says where
each one starts and loops.

## 10. BooBass, FL Slayer — revision 1

BooBass: six zones tuned in fourths from E1 (a bass guitar's strings and two more), each a plucked string's partials (position comb, faster decay of high partials), the attack ending on a
whole cycle that the loop carries on. Level from rendered audio.
FL Slayer: picks are short filtered noise bursts; the chord modes use a root, fifth, octave, third, fifth, octave shape along the
neck; the glide lengths are note values. Owner decision D4.

## 11. Effects

Maximus, EQUO, Vocodex, Love Philter, Convolver and Distructor start from states written by `core/NativeSlots.cpp` from the
documented layouts when FL's default preset is not there. Convolver's embedded room is generated noise under an exponential decay.

## 12. FL's data in builds LYNX has not seen

Where a table lies together in one block, `core/DataLocator.h` looks for that block by a hash of its first 32 bytes and accepts it only
with the right length and SHA-256: Kepler, Toxic, Slayer. BooBass is found by its six zone roots and read by the same relative
places with every size checked. DrumSynth is checked by its resource. No FL byte is stored in LYNX; only the hashes are.

## 13. FL Studio's folder (#291)

Settings > Files > FL Studio folder. `LYNX_FL_INSTALL` wins over it. The folder is looked at on a worker thread; a caller on the
message thread waits at most 0.8 s (a project import 15 s), then goes on with what was found last, and the rest follows when the disk answers.

## 14. Harmor, Autogun, the .flp export, FL 2026's own check

- Harmor: a state written from REA_rodada11 §4.1 / REA_rodada34 §1 / REA_rodada75 §1.1 (part A on, part B present and off, the modules in their
  natural order, 43 articulators, 26 shaping curves, an empty image object with its three plane counts, the global block). FL 2026 refused it at
  first ("Something went wrong ... loading the plugin (Harmor) state"): the three plane counts were missing.
- Autogun: Ogun's state (REA_rodada57) and Ogun's bank, 12 dB louder as far as the level knob goes.
- Kepler Exo: FL keeps the "Kepler-2" engine state in a wrapper (1, 1, size, 0, the state, 68 bytes; REA_rodada167d §1). A state without it
  crashed the plugin in FL 2026. The bank writes the wrapper with 68 zero bytes (their meaning was not decoded; FL loads and plays it), and the
  .flp export leaves out a Kepler Exo state without the wrapper (FL starts its default; the report says so).
- BassDrum and Morphine name their noise by file; LYNX's own noises are taken out of the state on .flp export (FL would ask for a missing file).
- Check: FL64 /R renders every bank preset in FL 2026 (one project per engine) and LYNX renders the same .flp on FL's data; per preset level
  differences over 3 dB were explained by free-running phases (LYNX's own spread over 24 starting points) or the preset was dropped
  (FL-LOAD-TABLE.md in the round folder).
