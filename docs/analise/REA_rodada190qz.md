# REA round 190qz — plug-in parity round 2: controllers' first value, notes' Mod X / Y on 3x Osc and Harmless

FL Studio 2026 (`C:\Program Files\Image-Line\FL Studio 2026`, FL Studio ASIO 44.1 kHz untouched). Clean-room: behaviour
only, no code, tables or presets copied. Evidence kinds: **[EXEC]** FL 2026's own `/R /Ewav` renders of variant .flp files
(our `scripts/slotoff.py` switches mixer slots off with FL's slot-bypass controls, `scripts/solofl.py` keeps one channel on;
`scripts/flsession.sh` / `flrun.ps1`, no injected input) against LYNX's `renderwav` of the same files; **[DATA]** the .flp
files (`tools/mixdump`, `autofx`, `livedump`, `oscdump`, `modscan`, `patev` — our own probes over LYNX's importer);
**[STATIC]** earlier REA (REA_rodada162e-envelope §Runtime: the Envelope Controller's tick "sent only when it changes";
REA_rodada76 §1: a note's Mod X / Y byte b → Init.FCut / FRes = (b − 128) / 128; the Harmless engine's Final.FCut / FRes
terms, round 33 §1.15). Work folder `/mnt/e/lynx-work/parity2_190qz/` (`SCORES.md`, `fl/`, `flp/`, `lx/`, `gold/`,
`logs/`, `tools/`, `scripts/`). The Kilohearts part is the sub-round's own document (REA_rodada190qz-khs.md).

Picks (from round 190py's follow-ups, SCORES.md): Asher Postman's Harmless "Wavvyy Synth" 25 dB under FL in its solo;
RawFL's "3xOsc #5" +4 dB; a scan of every demo for the same causes.

## 1. Controllers send nothing before their first value

| # | Fact | Evidence |
|---|------|----------|
| K1 | Asher's "Wavvyy Synth" solo (Harmless → insert 15 → send to insert 13 "Sidechain": Fruity Filter, Reeverb 2, Love Philter → Master) was −25.1 dB under FL. Switching slots off one at a time in both: the 25 dB appear only with insert 13's Love Philter on (LYNX), FL's level is the same with it on or off. | [EXEC] `fl/ah_v0…v6.wav` vs `lx/base/ah_v*.raw`, LYNX-only splits `ah_v7…v9` |
| K2 | That Love Philter's parameter 7 is driven by a link from the "Basic Sidechain" channel (a Fruity Envelope Controller, output 0, base 0). In the solo the controller channel is off (no notes); LYNX sent the output's base from the first block, so the Love Philter sat at 0; FL leaves the parameter at its own value. | [DATA] `tools/livedump`, `autofx`; [STATIC] REA_rodada162e (the tick sends a value only when it changes; without a voice and without continuous output the last value is kept) |
| K3 | The whole song shows the same: FL's intro (0–14 s, before the controller's first note) is 3–7 dB louder than LYNX's; with the Love Philter switched off in LYNX the intro is within ~1 dB of FL. | [EXEC] `flparity2_190pr/fl/Asher Postman - Future House.wav` vs `lx/full/*.raw` (`scripts/seglev.py`) |
| K4 | RawFL's "3xOsc #5" has a Fruity Peak Controller (insert 23 slot 4, output 2 = LFO) linked to its osc-2 mix (FL parameter 6). With the Peak Controller's slot bypassed, FL plays the 3x Osc at its own saved mix; LYNX sent 0 (the output before any block) and the osc-2 mix went to 0. | [EXEC] `fl/rf_v1.wav`, `rf_v4.wav` vs `lx/base/rf_v1.raw`; [DATA] `tools/oscdump` (P[6] = 54) |

**LYNX now:** `Instrument::controlSent` / `Effect::controlSent` (default: sent). The rack reads a link's source only when
it has sent; otherwise the automation value is NaN — the target keeps its own value (the same rule as pattern events
before their first clip). Envelope Controller (ChannelDef::engineRev **6**): an output sends once a note has reached it
(continuous output and the Keyboard Controller as before). Peak Controller (EffectSlot::engineRev **3**): sends once it has
processed a block (also after a reset). Projects saved before (channel revision ≤ 5, slot revision ≤ 2) keep the old rule.
Asher's solo: −25.13 → **+0.12 dB** RMS vs FL (50 ms error 24.9 → 3.0 dB).

## 2. A note's Mod X / Y reach 3x Osc and Harmless voices

| # | Fact | Evidence |
|---|------|----------|
| M1 | With every slot off (`rf_v1`, the Peak Controller fix in), LYNX's "3xOsc #5" was still +7.7 dB over FL; per window FL's lowest oscillator was within 1 dB, the middle 3 dB, the top one 16–18 dB under LYNX's — a low-pass on the voice. | [EXEC] `fl/rf_v1.wav` vs `lx/d1/rf_v1.raw` (spectral peaks at 1 / 5 / 12 / 20 s) |
| M2 | The channel's 16 notes all carry Mod X 40 (of 256). FL hands a note's Mod X / Y to the generator's voice as Final.FCut / FRes = (b − 128) / 128, which the plug-ins' voice processor adds to the channel filter's X / Y (`flvp::filter`, the same term LYNX's Sampler has used since issue #63). LYNX's 3x Osc left them at 0. | [DATA] `tools/oscdump`; [STATIC] REA_rodada76 §1; core/FlVoiceProcessor.h |
| M3 | Handing the note's values to the voice: RawFL solo (every slot off) +10.03 → **+0.07 dB**, 50 ms error 10.5 → 1.65 dB, within 3 dB 5 → 77 %; the full chain (`rf_v0`) +5.08 → **−0.05 dB**, 5.07 → 1.38 dB, 21.5 → 81.9 % — the "+4 dB" of round 190py. | [EXEC] `lx/d2/rf_v*.raw` |
| M4 | Harmless' engine already has FL's Final.FCut / FRes terms (round 33 §1.15) with 0 in them. Harmless Galactic's solos (FL renders): "Kick" (16 of 28 notes off centre) 50 ms error 0.83 → 0.48 dB, within 3 dB 81.1 → 89.9 %; "Rubber" 0.72 → 0.66 dB; "Zeta" (6 of 24 notes) 2.02 → 2.08 dB (its random phases; RMS unchanged). | [EXEC] `fl/hg_c0/5/16.wav` vs `lx/base` / `lx/d3` |
| M5 | Every demo's notes with Mod X / Y off centre (`logs/modscan.txt`): 3x Osc (RawFL ×5), Harmless (Harmless Galactic ×3), Sytrus (round 190pr), Harmor (Astes, Oxygen, Bass Antics), Ogun, DX10, Sampler channels (issue #63). Harmor / Ogun / DX10 not changed: no FL evidence yet (Oxygen's Harmor solo renders silent in both; Astes' "Glass pluck" solo is +1.8 dB / 0.92 dB in 50 ms before any change). | [DATA] |

**LYNX now:** 3x Osc and Harmless take `setNextNoteModulation` from ChannelDef::engineRev 6: 3x Osc writes the voice
processor's noteCut / noteRes (desktop channels; an .flm 3x Osc unchanged), Harmless its records' finalFcut / finalFres.
Revision ≤ 5: every note at the centre, bit for bit as before.

## 3. Old projects, copies for 0.1.31

ChannelDef::engineRev 6 (5 is round 190qo's SoundFont slide), EffectSlot::engineRev 3. A .lynx saved before loads with
its own revisions and renders as before. "Save a copy for 0.1.31" writes slots at 2 (channels at 3, the existing step)
and lists the Peak / Envelope Controllers that have links ("… set their targets from the start in this release …").
