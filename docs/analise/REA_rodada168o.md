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
release, tail) and checked: not silent (held RMS > −50 dB), no clipping (peak < −0.1 dBFS), finite. Results in
§5; the WAVs are in /mnt/e/lynx-work/design168o/banks/ (not in the repository).

## 4. Where I stopped

(see the end of the round)

## 5. Render results

(filled from the bank harness)
