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

@@TABLE@@

## 4. Change

- `Song::flPluginLevels` (saved as `"pluginLevels": "fl"`): the project's hosted plugin channels follow Fruity Wrapper.
  The .flp import sets it. `RackRenderer::renderChannel` gives such a channel FL's kernel of (pan, 2·f(v)), with the
  project's panning law (`flWrapperGains`); `PluginInstrument` sends `Round (128 v)` (`hostpitch::flWrapperVelocity`;
  a note on never goes below 1).
- **Projects saved before keep their sound**: without the property a project keeps LYNX's own law (f(v) at the centre,
  JUCE's velocity). New LYNX projects and .als imports (Live plays a plugin at unity = volume 100 % under LYNX's own
  law) are unchanged too. Non-plugin channels are untouched in both cases.
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
- **Verdict:** an FL project must play at FL's level, so the import follows the wrapper exactly. Whether **new** LYNX
  projects should also adopt FL's wrapper level (a plugin 3 dB louder than in 0.1.25 at the same knob) is a product
  choice: not changed here, sent to the coordinator (recommended: yes, for new projects only, as the flag already
  keeps old ones).

## 6. Not verified

- A plugin generator inside a Patcher (FLEngine's own copy of the host): its level law was not measured; unchanged.
- VST2 and CLAP wrapper channels: the same wrapper class and Gen_Render (inferred, only VST3 rendered).
- Notes played live from a MIDI keyboard into a project with FL's levels also go through `Round (128 v)` (LYNX keeps
  a played velocity as byte / 127, so byte 100 goes out as 101); what FL's wrapper gets for a played note was not
  probed.
- `.flp` export of a project with LYNX's own law compensates the knob only; a channel-volume automation of a hosted
  plugin is written as it is.
