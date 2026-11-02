# REA round 168o — designed editors and LYNX preset banks for the synth group

Scope: Harmless (first), Ogun + Autogun, Fruity DX10, MiniSynth, GMS, Morphine and SimSynth get a designed channel page
(LYNX's dark style with the red accent, FL's arrangement of the plugin's own editor as the reference) in place of the
generic knob grid, every control automatable, and — where the engine needs no FL file — a bank of LYNX-made presets
that the preset arrows cycle through after FL's own `.fst` list. Built on round 168n's infrastructure
(core/EngineParams.h, core/ModelParams.h, core/LynxBanks.h).

Clean-room: only FL Studio's own binaries were studied (the plugins' DLL editor forms, read as data); FL's artwork was
only looked at for the placement of the non-Delphi editors (MiniSynth, GMS, Morphine) and is not in the repository;
no FL preset was read, converted or copied — every preset here was designed for LYNX and checked by rendering it.

## 1. Per-item table

| Item | State | Where |
|---|---|---|
| Harmless page (848 × 492: timbre, harmonic mask, envelope, pluck, volume, pitch, filter, unison, phaser, harmonizer, LFO, effects; combos as menus, VEL buttons) | done | core/PluginLayoutSynths.cpp |
| Harmless automation (p0…p99 + 7 combos; live, effects rack re-prepared only when an effect parameter moves) | done | core/ModelParamsSynths.cpp, core/Harmless.cpp |
| Harmless bank (22 presets: Init, Bass, Lead, Pad, Pluck, Keys, FX) | done, rendered | core/HarmlessBank.cpp |
| Fruity DX10 page (536 × 236: amp envelope, two modulators, LFO / waveform / octave) | done | core/PluginLayoutSynths.cpp |
| DX10 automation (22 knobs, live) | done | core/Dx10.cpp |
| DX10 bank (15 presets) | done, rendered | core/Dx10Bank.cpp |
| SimSynth page (562 × 376: 3 oscillators, amp / filter envelopes, LFO, chorus) | done | core/PluginLayoutSynths.cpp |
| SimSynth automation (FL's 58 host parameters, live without restarting voices) | done | core/SimSynth.cpp |
| SimSynth bank (15 presets) | done, rendered | core/SimSynthBank.cpp |
| Ogun / Autogun page (560 × 548: master, X/Y modulation, timbre, unison, filter, EQ, chorus / delay / reverb; Autogun's Magic dust) | done | core/PluginLayoutSynths.cpp |
| Ogun / Autogun automation | knob edits apply (state rebuild); song clips: not live yet (the engine rebuilds its voices from the state) | — |
| Ogun / Autogun bank | not made: Ogun's patch is the 13 articulator curves + seeds (needs FL's own preset to be useful), Autogun needs FL's SG.dat | notice |
| MiniSynth page (6 pages: Oscillator, Amplifier, Filter, Filter env, LFO, Effects) | done | core/PluginLayoutSynths.cpp |
| MiniSynth automation (43 floats, live) | done | core/MiniSynth.cpp |
| GMS page (oscillators, filter, 2 envelopes, 2 LFOs, level EG, channel, 10 effects on pages, output EQ) | done | core/PluginLayoutSynths.cpp |
| GMS automation (P[i], live) | done | core/Gms.cpp |
| Morphine page (top bar, master envelope, drive / level, noise, chorus / delay / reverb / 8-band EQ) | done | core/PluginLayoutSynths.cpp |
| Morphine automation (global A0…A39 except the stepped A2 / A3; live, reverb tail kept) | done | core/Morphine.cpp |
| MiniSynth / GMS / Morphine banks | not made: their sound comes from FL's data (MiniSynth / GMS oscillator files, Morphine's generators) — the page says so | notice |
| Tests | tests/Design168oTests.inc | |

## 2. What FL's editors give (REA)

Each Delphi editor is a `TSynthEditorForm` resource in the plugin's DLL (dumped with tmp/rea165/dfm.py). A control's
**Tag is the parameter number** the plugin keeps the value under and **Min / Max its range**; Left / Top / Width /
Height place it. These numbers are what core/ModelParamsSynths.cpp lists and core/PluginLayoutSynths.cpp places.

- **Harmless** (Harmless_x64.dll, form 848 × 569, the keyboard strip below y = 500 left out): p0…p99 with FL's ranges;
  the combo boxes are stored apart (modelParams 100…106 = filter type (17 options), resonance type (9), phaser type
  (10), compressor (5), colour (12, incl. Custom), threaded, quality). Other lists read from the form: LFO shape 14,
  unison type 3, harmonizer type 5 / position 4, chorus 10, delay 4, reverb 7.
- **Fruity DX10** (form 536 × 340): 22 parameters, all 0…65536 except Octave −3…3.
- **SimSynth** (form 562 × 376): Tags are FL's host parameters 0…57 (knobs 0…65535, coarse pitch 0…20160 = 48 steps
  × 420, the switches S[] at state offset 0x168 + 76, wave selectors 0 pulse, 1 half-cosine, 2 triangle, 3 noise,
  4 sine mix; LFO 0 square, 1 saw, 2 triangle, 3 S&H).
- **Ogun / Autogun** (form 560 × 640): P0…P63 (richness P52 6…16, quality P54); the articulators (point curves) are
  FL's curve editor — LYNX keeps them as saved. Autogun adds P63 Magic dust.
- **MiniSynth, GMS, Morphine** have no Delphi form (their own skins): the parameter numbers are the state floats
  already decoded (REA_rodada41 §1.2, REA_rodada12 §1.2, the Morphine global block A0…A39); the arrangement follows
  their editor backgrounds, looked at only.

## 3. Banks

Banks are registered through round 168n's `LYNX_BANK` list and appear after FL's `.fst` presets under
"LYNX/<category>"; the channel's preset arrows step through both. Every preset was rendered (held note / chord 2 s,
release, tail; pads and keys as a 4-note chord, basses on C2) on a channel at FL's default volume (78 % = gain 0.549)
and checked: finite, not silent (held RMS > −54 dB), headroom (peak < −1 dBFS). Harmless's master volume (p36) was
set per preset so the loud ones (chords, unison, resonance) peak near −3 dBFS; DX10's single voices stay at the
engine's own level (about −16 dBFS, as FL's DX10). tools/bankrender renders the same banks. Results in
§5; the WAVs are in /mnt/e/lynx-work/design168o/banks/ (not in the repository).

## 4. Where I stopped

- Done: the eight pages (layout check: every parameter of every table is on its page, all items inside the page),
  parameter tables + automation, the three banks (51 presets, all clean), envelope displays, notices, translations.
- Not done: Ogun / Autogun live automation clips (knob edits work; clips need the engine to recompute its voices from
  P without a rebuild); MiniSynth / GMS / Morphine / Ogun banks (FL data needed, see §1).
- Built on round 168n's infrastructure merged from origin/wip/design168n (168n had not landed on main yet).
- 2026-11-02 11:10 the PC crashed during the post-merge rebuild (branch at b885a072 = 168n wip + main efb8344d, all
  pushed): rebuilt from clean build folders afterwards. Landing waits for 168n on main (then merge origin/main, land).
- Linux tests before the crash: all 168o tests pass; the 3 remaining failures were 168n's own (Design168nTests
  59 / 187) on its wip state.

## 5. Render results

Phrase peak / held RMS at gain 0.549 (zc/s = zero crossings per second of the held part, a brightness hint):

```
harmless  Init Saw               Init   peak  0.530 ( -5.5 dB) rmsOn  -11.7 dB zc/s    524 tail -240.0 dB 
harmless  Round Sub Bass         Bass   peak  0.618 ( -4.2 dB) rmsOn  -11.7 dB zc/s    132 tail -240.0 dB 
harmless  Rubber Pluck Bass      Bass   peak  0.636 ( -3.9 dB) rmsOn  -22.5 dB zc/s    131 tail -240.0 dB 
harmless  Square Growl           Bass   peak  0.705 ( -3.0 dB) rmsOn  -11.0 dB zc/s    330 tail -240.0 dB 
harmless  Acid Squelch           Bass   peak  0.700 ( -3.1 dB) rmsOn  -13.3 dB zc/s    424 tail -240.0 dB 
harmless  Supersaw Lead          Lead   peak  0.629 ( -4.0 dB) rmsOn  -15.3 dB zc/s   1796 tail  -75.0 dB 
harmless  Glide Mono Lead        Lead   peak  0.641 ( -3.9 dB) rmsOn  -13.1 dB zc/s    726 tail -106.9 dB 
harmless  Hollow Square Lead     Lead   peak  0.604 ( -4.4 dB) rmsOn  -10.8 dB zc/s    524 tail  -62.7 dB 
harmless  Velvet Pad             Pad    peak  0.703 ( -3.1 dB) rmsOn  -15.5 dB zc/s    934 tail  -38.1 dB 
harmless  Phaser Choir           Pad    peak  0.702 ( -3.1 dB) rmsOn  -16.2 dB zc/s   2048 tail  -61.0 dB 
harmless  Harmonized Air         Pad    peak  0.703 ( -3.1 dB) rmsOn  -15.8 dB zc/s   1858 tail  -64.5 dB 
harmless  Shimmer Mask           Pad    peak  0.705 ( -3.0 dB) rmsOn  -14.3 dB zc/s   1180 tail  -38.8 dB 
harmless  Dark Drone             Pad    peak  0.701 ( -3.1 dB) rmsOn  -15.5 dB zc/s    724 tail  -38.3 dB 
harmless  Glass Pluck            Pluck  peak  0.604 ( -4.4 dB) rmsOn  -23.0 dB zc/s    633 tail  -98.9 dB 
harmless  Soft Mallet            Pluck  peak  0.621 ( -4.1 dB) rmsOn  -23.0 dB zc/s    538 tail -240.0 dB 
harmless  Echo Pluck             Pluck  peak  0.638 ( -3.9 dB) rmsOn  -25.0 dB zc/s    524 tail -115.2 dB 
harmless  Tine Keys              Keys   peak  0.701 ( -3.1 dB) rmsOn  -23.1 dB zc/s    484 tail -240.0 dB 
harmless  Drawbar Organ          Keys   peak  0.701 ( -3.1 dB) rmsOn  -13.2 dB zc/s    710 tail -154.3 dB 
harmless  Slow Sweep             FX     peak  0.621 ( -4.1 dB) rmsOn  -14.2 dB zc/s    694 tail  -42.2 dB 
harmless  Noise Wind             FX     peak  0.657 ( -3.6 dB) rmsOn  -21.1 dB zc/s   5724 tail  -68.4 dB 
harmless  Grit Riser             FX     peak  0.535 ( -5.4 dB) rmsOn  -17.5 dB zc/s    533 tail  -71.8 dB 
dx10      Init FM                Init   peak  0.152 (-16.4 dB) rmsOn  -23.8 dB zc/s    554 tail -240.0 dB 
dx10      Tine E-Piano           Keys   peak  0.593 ( -4.5 dB) rmsOn  -15.5 dB zc/s    753 tail -240.0 dB 
dx10      Soft Clav              Keys   peak  0.580 ( -4.7 dB) rmsOn  -20.5 dB zc/s   1529 tail -240.0 dB 
dx10      FM Organ               Keys   peak  0.539 ( -5.4 dB) rmsOn  -15.5 dB zc/s    832 tail -240.0 dB 
dx10      Punch FM Bass          Bass   peak  0.151 (-16.4 dB) rmsOn  -27.9 dB zc/s    326 tail -240.0 dB 
dx10      Solid Sub FM           Bass   peak  0.161 (-15.8 dB) rmsOn  -19.1 dB zc/s    556 tail -240.0 dB 
dx10      Slap Bass              Bass   peak  0.151 (-16.4 dB) rmsOn  -29.8 dB zc/s     83 tail -240.0 dB 
dx10      Crystal Bell           Pluck  peak  0.152 (-16.4 dB) rmsOn  -20.4 dB zc/s   1412 tail  -88.0 dB 
dx10      Marimba                Pluck  peak  0.151 (-16.4 dB) rmsOn  -30.9 dB zc/s    525 tail -240.0 dB 
dx10      Glass Pluck            Pluck  peak  0.160 (-15.9 dB) rmsOn  -29.7 dB zc/s    554 tail -240.0 dB 
dx10      FM Brass               Lead   peak  0.152 (-16.3 dB) rmsOn  -19.8 dB zc/s   2900 tail -240.0 dB 
dx10      Reedy Lead             Lead   peak  0.171 (-15.3 dB) rmsOn  -18.1 dB zc/s   1792 tail -240.0 dB 
dx10      Warm FM Pad            Pad    peak  0.606 ( -4.4 dB) rmsOn  -12.8 dB zc/s    804 tail  -92.9 dB 
dx10      Metal Haze             Pad    peak  0.612 ( -4.3 dB) rmsOn  -13.4 dB zc/s    835 tail  -69.8 dB 
dx10      Laser Drop             FX     peak  0.152 (-16.4 dB) rmsOn  -21.3 dB zc/s   3582 tail -240.0 dB 
simsynth  Init Pulse             Init   peak  0.124 (-18.2 dB) rmsOn  -23.1 dB zc/s    524 tail -240.0 dB 
simsynth  Analog Bass            Bass   peak  0.166 (-15.6 dB) rmsOn  -30.0 dB zc/s    200 tail -240.0 dB 
simsynth  Warm Sine Sub          Bass   peak  0.151 (-16.4 dB) rmsOn  -24.8 dB zc/s    130 tail -240.0 dB 
simsynth  Resonant Squelch       Bass   peak  0.282 (-11.0 dB) rmsOn  -26.6 dB zc/s    244 tail -240.0 dB 
simsynth  Warm Sync Lead         Lead   peak  0.335 ( -9.5 dB) rmsOn  -21.9 dB zc/s   1023 tail -240.0 dB 
simsynth  Square Chip Lead       Lead   peak  0.141 (-17.0 dB) rmsOn  -27.5 dB zc/s   1522 tail -240.0 dB 
simsynth  Ring Lead              Lead   peak  0.331 ( -9.6 dB) rmsOn  -23.9 dB zc/s   1308 tail -240.0 dB 
simsynth  String Pad             Pad    peak  0.599 ( -4.5 dB) rmsOn  -17.2 dB zc/s    280 tail -240.0 dB 
simsynth  Glass Pad              Pad    peak  0.448 ( -7.0 dB) rmsOn  -18.8 dB zc/s    278 tail  -95.9 dB 
simsynth  Sweep Pad              Pad    peak  0.555 ( -5.1 dB) rmsOn  -17.7 dB zc/s   1718 tail -240.0 dB 
simsynth  Bright Pluck           Pluck  peak  0.302 (-10.4 dB) rmsOn  -31.4 dB zc/s    272 tail -240.0 dB 
simsynth  Soft Keys              Keys   peak  0.612 ( -4.3 dB) rmsOn  -21.6 dB zc/s    941 tail -240.0 dB 
simsynth  Toy Organ              Keys   peak  0.623 ( -4.1 dB) rmsOn  -15.8 dB zc/s   1356 tail -240.0 dB 
simsynth  Noise Sweep            FX     peak  0.542 ( -5.3 dB) rmsOn  -20.9 dB zc/s   4805 tail  -78.2 dB 
simsynth  Random Bleeps          FX     peak  0.237 (-12.5 dB) rmsOn  -22.1 dB zc/s    566 tail -240.0 dB 
0 bad
```
