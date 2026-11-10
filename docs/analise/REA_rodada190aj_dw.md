# Round 190aj — DirectWave: the three programs that did not match FL's render (#74 family)

> Clean-room, behavioural. **[EXEC]** = FL's own renders and LYNX's renders compared; **[model]** = LYNX's code.
> Continues REA_rodada72 (engine) and REA_rodada190ac §6 (the table of nine programs).

## 1. Cause **[EXEC]**

Round 190ac measured nine programs against FL's 16-bit renders (FL's `System/Render tests/Directwave.flp` with the
state of a demo channel swapped in): six at the file's floor (−73…−77 dB), Stage Grand −52.8 dB, Strings −27.9 dB,
Choir Ahh −33.4 dB.

The error over time (100 ms windows, `slicex190aj/p/win.py`) shows the same picture in all three: the residual is at
the floor (−101 dB per window) for as long as a voice sounds, and from the moment the last voice ends LYNX is silent
while FL goes on decaying for more than a second (Strings: LYNX stops at 1.87 s, FL is at −53 dB there and reaches
−99 dB at 2.9 s; Stage Grand: LYNX stops at 2.5 s). The three programs are the ones with the program **reverb** on and
a reverb send in their zones; the six that match have no ringing effect at that point.

So the engine was right and the envelope, loop, filter and matrix suspects were not involved: the **rack** stopped
rendering the channel. `DirectWaveInstrument::idle()` answered "no voice is playing", and the rack skips an idle
channel (`RackRenderer`: "idle channel, no notes in this chunk and no PDC: nothing to do"). In FL the plugin runs its
delay, chorus and reverb in every block (REA_rodada72 §1), so they ring out.

## 2. Fix **[model]**

`dw::Engine::ringing()`: with the delay, the chorus or the reverb on, the engine counts the frames since the output
last rose above a floor while no voice plays; the channel is idle only when no voice plays **and** that count has
reached 131 072 frames (longer than the 65 536-frame delay line plus the chorus lines, so the gap between two echoes
does not end it). The floor is −200 dB plus what the delay and the chorus make of their own dither: both add
`rand()·10⁻¹⁴` (up to 3.3·10⁻¹⁰) into their feedback loop in every sample, which settles at up to
`3.3·10⁻¹⁰ / (1 − feedback)`; the floor adds `5·10⁻¹⁰ / (1 − feedback)` for each of the two that is on, and with a
feedback of 0.999 or more the channel never rests (FL's never does). A program without these effects rests with its
last voice, as before.

Not a change of the engine's laws: every sample the engine produced before is produced unchanged (the 57-program
bit-exact goldens of round 72 are untouched); the change is that the rack no longer stops asking for samples while the
effects ring. A saved project with such a program now keeps its tail, as FL does; nothing is versioned, because the
old behaviour was the truncation, not a sound law.

What the rest costs: after the last voice the effects run on for the tail plus 3 s (at 44.1 kHz), then the channel
is skipped again. FL runs them always.

## 3. Numbers

See `/mnt/e/lynx-work/slicex190aj/out/before.txt` and `after.txt` (table in the round's HANDOFF).

## 4. Best way?

- **FL:** the plugin's effects run in every block, voices or not (REA_rodada72 §1); a render with "leave remainder"
  or "wait for silence" keeps the tail.
- **Ableton:** Sampler / Simpler have no program reverb, so there is no direct counterpart; not studied further
  for this fix (nothing in /mnt/e/lynx-work/ableton/STUDY.md on it).
- **Users:** a cut reverb tail is a plain defect; nobody asks for it.
- **Verdict:** FL's result (the tail rings out); the channel rests once the tail is silent, which FL's plugin does
  not do but which cannot be heard. No option needed.
