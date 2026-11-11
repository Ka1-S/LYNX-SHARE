# Round 190ad — hosted plugins of an FL project played 3 dB under FL [DECOMP][DOC][FL render][TEST]

Round 190x (issue #143) measured Surge XT (VST3 in FL's wrapper) 3 dB quieter in LYNX than in FL's render of the same
project, cut group or not. This round finds the law and fixes it.

Binaries: FL Studio 2026, `Plugins/Fruity/Generators/Fruity Wrapper/Fruity Wrapper_x64.dll` (Ghidra project
~/ghproj/r134w/fw, as round 188g). Queries and decompilations: `/mnt/e/lynx-work/pluglevel190ad/rea/` (q1 … q11).
Third-party plugins were only rendered (black box).

## 1. The wrapper's output gain **[DECOMP]**

- The wrapper's plugin VMT (`0x868840`): slot 5 ProcessEvent `0x878ef0`, slot 8 Gen_Render `0x879810`, slot 9
  TriggerVoice `0x879a00`.
- **ProcessEvent** (`0x878ef0`): event 2 (the SDK's FPE_MIDI_Pan) stores its integer pan (−64…64) at `+0x5108`; event 3
  (FPE_MIDI_Vol) stores **`vol × 2.0`** at `+0x50f0` (`vol` = the event's normalised float, the constant `0x8f4f40` =
  2.0f). Both then call `0x878e60`, which asks the host for the gains: host slot 3, `ComputeLRVol (&L, &R, pan, vol)`
  with the integer pan — FL's pan kernel (`FLEngine` `FUN_006a3a00`, REA_rodada30 §1.3 / REA_rodada167j §1: pan / 64,
  circular `L = vol·cos((1 + p)·π/4)`, `R = vol·sin(…)`; triangular project law: the louder side `vol·0.70710677`,
  the other falls linearly). The wrapper starts at pan 0 (`0x878cf0`).
- **Gen_Render** (`0x879810` → `0x8790a0`): the plugin processes into its own buffers, then `0x776680` (float) /
  `0x7763e0` (double) add every output bus to the host's buffer with those two gains, ramped from the previous block's
  (`0x61cbb0`). "Use fixed size buffers" (`0x8794f0` → `0x879770`) runs the same call into a ring and copies it out at
  unity. No MIDI CC 7 / CC 10 is sent to the plugin (the base ProcessEvent `0x870790` handles only the tempo).

So a hosted generator's output is scaled by **L = 2·f(v)·cos((1 + p)·π/4), R = 2·f(v)·sin((1 + p)·π/4)**, with f the
channel volume curve `(11^v − 1) / 10` (the renders of §3 fix `vol` = f(v)): **√2·f(v) at the centre**, 2·f(v) on the
side panned to. FL's native voice generators get the same kernel without the 2 (REA_rodada169l).

**[DOC]** Image-Line's manual (Levels, Mixing & Clipping) says the same: VST instruments get 6 dB of compensation, so at
the default channel settings a VST at 0 dB comes out 2.2 dB lower, "instead of the 8.2 dB for FL native plugins"
(6.02 − 3.01 − 5.21 = −2.20 dB). <https://www.image-line.com/fl-studio-learning-content/fl-studio-online-manual/html/mixer_levelsandmixing.htm>

LYNX's channel law (`panGains`, RackRenderer.cpp) is `f(v)·√2·cos / sin`: f(v) at the centre for every instrument,
hosted plugins included — 1 / √2 of FL's wrapper, **−3.01 dB**, at every volume and pan.

## 2. The wrapper's note velocity **[DECOMP]**

TriggerVoice (`0x879a00`): MIDI velocity = `Round (velocity · 128.0)` (single precision product, `0x40c3c0` =
cvtsd2si, ties to even), clamped to 0…127, with `velocity` the note's own (`FPV_GetVelocity`). (With the wrapper
setting at `+0x44` set, the velocity comes from Init.Vol instead; off in every project seen.) An .flp note's velocity
byte b is b / 128 in LYNX, and LYNX sent JUCE's `round (127 v)`: 100 went out as 99, 127 as 126.

## 3. FL's renders

`/mnt/e/lynx-work/pluglevel190ad` (`flp/`, FL's renders `flr/`, LYNX's `lx/`; `tools/mkflp.py` patches round 190x's
`probe/cut0.flp`: Surge XT VST3 at its default patch on insert 1, a 4-beat note on key 48). RMS of each side over
0.5…2.0 s, dBFS; LYNX − FL in dB. Surge's oscillators start at a random phase: ±0.05 dB between runs; the R / L ratio
of one render is exact (the patch is mono).

| project (volume, pan, law) | FL L / R | LYNX before L / R | before − FL | LYNX after L / R | after − FL | R / L: FL, before, after |
|---|---|---|---|---|---|---|
| volume 25 %, centre | −40.72 / −40.72 | −43.72 / −43.72 | −3.01 | −40.70 / −40.70 | +0.01 | 1.0000, 1.0000, 1.0000 |
| volume 50 % | −31.70 / −31.70 | −34.71 / −34.71 | −3.01 | −31.69 / −31.69 | +0.01 | 1, 1, 1 |
| volume 78 % (default) | −24.20 / −24.20 | −27.22 / −27.22 | −3.01 | −24.20 / −24.20 | 0.00 | 1, 1, 1 |
| volume 100 % | −19.00 / −19.00 | −22.00 / −22.00 | −2.99 | −18.99 / −18.99 | +0.02 | 1, 1, 1 |
| pan hard left | −21.20 / silent | −24.20 / silent | −3.00 | −21.20 / silent | 0.00 | 0, 0, 0 |
| pan half left | −21.88 / −29.54 | −24.89 / −32.55 | −3.01 | −21.89 / −29.54 | 0.00 | 0.4142, 0.4142, 0.4142 |
| pan hard right | silent / −21.20 | −142.5 / −24.21 | −3.01 | silent / −21.19 | +0.01 | — |
| velocity 51 | −24.21 | −27.21 | −2.99 | −24.21 | 0.00 | 1, 1, 1 |
| velocity 127 | −24.20 | −27.22 | −3.01 | −24.21 | 0.00 | 1, 1, 1 |
| pan +1.5 / 64 | −24.32 / −24.11 | −27.38 / −27.06 | −3.06 / −2.95 | −24.31 / −24.10 | +0.01 | 1.0249, 1.0375, 1.0248 |
| pan +2.5 / 64 | −24.42 / −24.00 | −27.49 / −26.96 | −3.07 / −2.96 | −24.41 / −23.99 | +0.01 | 1.0503, 1.0633, 1.0503 |
| pan +0.4 / 64 | −24.21 / −24.21 | −27.26 / −27.18 | −3.05 / −2.96 | −24.21 / −24.21 | 0.00 | 1.0000, 1.0099, 1.0000 |
| triangular law, half left | −24.21 / −30.23 | −24.90 / −32.55 | −0.69 / −2.32 | −24.20 / −30.22 | 0.00 | 0.5000, 0.4142, 0.5000 |
| triangular law, centre | −24.21 / −24.21 | −27.22 / −27.22 | −3.01 | −24.20 / −24.20 | +0.02 | 1, 1, 1 |

"Before" = round 190x's build (c7b997151); "after" = a0d2ff1bd (cloud build, run 42906499360). What the renders fix:
the gain is √2·f(v) at every volume (so the event's `vol` is the curve f(v)); the pan is the kernel's, in whole steps
of 1 / 64 **truncated** (1.5 → 1, 2.5 → 2, 0.4 → 0; negative pans assumed to truncate towards zero too); the project's
triangular law reaches the wrapper (the louder side at the centre's level, R / L = 0.5 at half left). Surge's default
patch does not react to velocity; the velocity law was rendered separately:

**Velocity [FL render].** The same patch with Surge's "Vel > VCA gain" at −36 dB (0.28 dB per velocity step),
velocity bytes 70 / 100 / 127 (`flp/vs*.flp`): FL −40.37 / −31.86 / −24.20 dBFS; LYNX with the old law (base build, also
3.01 dB under) −43.65 / −35.15 / −27.50, i.e. one more velocity step under FL at each (−0.27 / −0.28 / −0.29 dB beyond
the level); LYNX after −40.36 / −31.85 / −24.21 (within 0.01 dB). FL sends the byte itself: `Round (128 v)`.

**Gate** (same cloud build of origin/main e6c77291b vs a0d2ff1bd, `lx/base_gate_*` / `lx/after_gate_*`): the selftest's
demo project (.lynx), FL's "Use Me" and "NewStuff" demo .flp (no hosted plugin) export **byte-identical**; a LYNX
project with Surge XT (`gate/s3.lynx`) keeps its level (−25.92 dBFS RMS both). Its `.flp` export rendered by FL:
−22.71 dBFS before (3.2 dB over LYNX), −25.71 after (peak −16.01 vs LYNX's −16.03).

## 4. Change

- `Song::flPluginLevels` (saved as `"pluginLevels": "fl"`): the project's hosted plugin channels follow Fruity Wrapper.
  The .flp import sets it. `RackRenderer::renderChannel` gives such a channel FL's kernel of (pan, 2·f(v)), with the
  project's panning law (`flWrapperGains`); `PluginInstrument` sends `Round (128 v)` (`hostpitch::flWrapperVelocity`;
  a note on never goes below 1).
- **Projects saved before keep their sound**: without the property a project keeps LYNX's own law (f(v) at the centre,
  JUCE's velocity). .als imports (Live plays a plugin at unity = volume 100 % under LYNX's own law) are unchanged too.
  Non-plugin channels are untouched in both cases.
- **New projects** (coordinator's decision, 2026-11-10): File › New, the built-in templates and a user's template that
  holds no hosted plugin start with FL's levels (`makeNewProject`, `adoptFlPluginLevels`, core/Templates.cpp); a user's
  template with a hosted plugin saved under LYNX's law keeps it. Selftest `plugin_levels_new_project`.
- `.flp` export: a project with LYNX's own law writes the plugin channel's volume that sounds the same in FL
  (f(v′) = f(v) / √2; before, FL played such an export 3 dB louder than LYNX); a project with FL's levels writes the
  knob as it is. `.als` export: a plugin track's gain is × √2 in a project with FL's levels.
- Tests: `tests/PlugLevel190adTests.inc`.

## 5. Best way?

- **FL:** the wrapper adds 6 dB to a VST and the channel's law takes 3 dB at the centre and 5.2 dB at the default
  78 % — a VST sits 2.2 dB under its own output on a new channel (§1, DECOMP + DOC + renders).
- **Ableton:** a plugin device plays at unity; the track fader starts at 0 dB (ALS_FORMAT.md; LYNX's .als import maps
  it to channel volume 100 %).
- **Users:** Image-Line's support calls the 78 % default "headroom"; plugin developers notice the wrapper's hidden
  level when they compare hosts (<https://forum.juce.com/t/flstudio-levels-issue/59509>).
- **Verdict:** an FL project must play at FL's level, so the import follows the wrapper exactly; new LYNX projects
  take FL's level too (a plugin 3 dB louder than in 0.1.25 at the same knob), saved ones keep theirs.

## 6. Not verified

- A plugin generator inside a Patcher (FLEngine's own copy of the host): FL's level there was not measured. LYNX's
  Patcher channel is not touched by the flag (test `plug190ad_a_patcher_channel_is_not_compensated`), so nothing is
  raised twice.
- VST2 and CLAP wrapper channels: the same wrapper class and Gen_Render (inferred, only VST3 rendered). No VST2
  instrument is installed on the study machine (the VST2 folders hold effects only).
- Notes played live from a MIDI keyboard into a project with FL's levels also go through `Round (128 v)` (LYNX keeps
  a played velocity as byte / 127, so byte 100 goes out as 101); what FL's wrapper gets for a played note was not
  probed.
- `.flp` export of a project with LYNX's own law compensates the knob only; a channel-volume automation of a hosted
  plugin is written as it is.
