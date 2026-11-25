# Round 190rh — FL's native engines without FL Studio installed (QUEUE-0.1.33 item 78)

Owner, 2026-11-25: "It makes no sense to have everything if people can only use it when they've installed FL." LYNX's
engines were rebuilt long ago; what was missing without FL is the data some of them read at run time, and a start state.

Clean-room rules of this round: nothing of FL's (tables, cycles, recordings, impulses, presets) is copied, converted,
resampled, fitted point by point, embedded or shipped. LYNX's sets are computed from textbook formulas and generators;
a small number of meaningful parameters are placed with black-box measurements, listed here per set. Bank presets are
designed for LYNX on the documented state layouts; no FL preset file was read for them.

## 1. Which set a channel plays (`core/NativeData.h`)

`ChannelDef::samplePath` is FL's file, or `lynxdata:<engine>/<revision>` = LYNX's own set. Loading or saving never changes
it. FL's file not where the project says: the same file of this computer's installation (`locate`: from
`Plugins\Fruity\` on, names ignoring case, never outside the installation's folder); else LYNX's own set plays, the
project keeps FL's path and the channel window says so. The channel ▸ menu switches between the two sets (undoable).

Cases: shared sessions and packages (`SessionPack::confine`) keep reserved paths and re-root FL data files to the
receiver's installation; remote document edits may name a reserved path (it is no file); a copy for a release before
this one gets a report line (the channel is silent there); `.flp` export is unchanged (these paths are never written).

## 2. Transistor Bass — own "303 Pulse" cycles, revision 1

What FL has: 57 + 55 single cycles in the engine DLL (REA_rodada57 §2.3). What LYNX computes (`bassPulseCycles`): one
cycle per semitone from C0, 192 kHz × 57 and 44.1 kHz × 55, so the engine's "shortest cycle longer than the period"
selection works alike. Source of the shape: the TB-303's square as described in public circuit literature — a saturated
transistor (flat low side), a high side sagging through the coupling capacitor, a duty cycle that narrows with the pitch.

Parameters placed by measurement of the level statistics of FL's cycles (duty, RMS, where the cycle starts), none fitted
to samples: duty 0.68 − 0.065 per octave above C0 (0.30…0.70); high side 1.05 + 1.05·e^(−t/10 ms) above the low side;
switching time 25 µs; the cycle starts at the fall. Edges are averaged under a raised cosine of two samples.

Result, engine on FL's cycles vs on LYNX's, the bank's presets (44.1 kHz, three notes): the 21 presets without "303 Pulse"
bit-identical; with it: level +0.05 / +0.07 / +1.07 dB, residual −13.2 / −9.1 / −7.7 dB (similar, not the same; the
third mixes the pulse with the saw and half cancels). None of FL's 128 factory programs uses the mode (REA_rodada57 §2.1).

Bank: 24 presets (`core/TransistorBassBank.cpp`), two with a sequence that follows the played key.
`transistorBassWriteState` gained the sequence argument (no sequence: the same bytes as before).

## 3. MiniSynth — own oscillator, revision 1

FL's `oscdata.dat` is 1024 amplitudes and phases of a falling saw. LYNX computes a falling saw whose reset is not instant:
x(p) = ½ − p − ½·e^(−p/τ), its Fourier series in closed form. One parameter, τ = 0.05 cycle, placed by looking at the
shape of FL's cycle (the rounded start); amplitude and phase are the textbook series, nothing is fitted per harmonic.
Engine on FL's table vs LYNX's, the bank's 24 presets: level +0.06…+0.98 dB (median 0.4), residual −17…−23.5 dB.
Bank: `core/MiniSynthBank.cpp`, on the .flms layout (REA_rodada41 §1.1).

## 4. Sawer — own cycle, revision 1 (engine only; bank open)

FL's `data.saw` is one 8192-point cycle. LYNX computes a saw's harmonics (1/h) through a first-order coupling high-pass at
the fundamental and a constant 0.3 rad turn, RMS 0.28 as FL's. Not yet compared through the engine. The bank waits for the
modulation block that follows the 96 parameters in a state (REA_rodada65 §3.1 does not give its layout): FL must be able to load
what LYNX writes, so it is taken from the engine's loader, not from a saved preset.
