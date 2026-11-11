# Round 190x — issue #143: Surge XT channels from FL projects silent in LYNX [FL render][DATA][TEST]

Report: a project saved in FL Studio with Surge XT (VST3 in FL) opens in LYNX with the Surge XT channels silent, while
other plugins of the same project play; Surge XT works in projects made in LYNX (as CLAP).

## 1. What was ruled out

Surge XT 1.3.4 (official portable zip, VST3 + CLAP + factory data in the standard folders), FL 2026 (study install),
LYNX 0.1.25 release and an origin/main build. Every case below plays in LYNX (headless `--export` and the app):

| project | how it was made | LYNX |
|---|---|---|
| Surge VST3, Acidofil patch | LYNX `--export-flp`, opened and saved in FL (Ctrl+S) | plays, same peak as the native project |
| same, other patch picked in Surge's editor, saved with the editor open | FL | plays |
| Surge CLAP | LYNX export, saved in FL (FL's CLAP state is `u32 0` + the plugin's own state, as REA_rodada22 §2.5 had decoded) | plays |
| any of the above with only the CLAP known to LYNX (VST3 state dropped, default patch) | | plays |
| separate process, fixed size buffers | | plays |

FL's wrapper state for the VST3 (records 1, 3 = component with JUCE's private-data trailer, 4 = parameter slots;
no record 2) restores correctly through `splitFlVst3State`.

## 2. The cause: FL's template puts every new channel in "cut itself" [DATA]

A Surge XT channel added in FL itself (ADD › More plugins › Surge XT VST3, steps entered, saved) is silent in LYNX
(its insert's render equals the render without it). Its channel events differ from the LYNX-exported one in 0x84 =
`0x00010001`: cut group 1, cut by 1 — the value FL's default template gives every new channel (REA_rodada170i3 §2
found it on all channels of `g169l.flp`).

`RackState::build` cut every hosted plugin by its group (VST plugins were left out of 170i3's probe). The choke is
queued after the note on at the same sample, so `PluginInstrument::cut()` (each held key's note off, then All Notes /
All Sound Off) ended the note that had just started: a plugin that stops at once (Surge XT) is silent; one with a
long release still leaks its tail, which is why other plugins of the same project seemed fine.

## 3. FL never cuts a wrapped plugin [FL render]

Probe (`probe/cut0.flp`, `probe/cut1.flp` in /mnt/e/lynx-work/surge190x): Surge XT VST3 at its default patch, a
4-beat note on key 48 and a short key 60 half a beat later; 0x84 = 0 vs 0x10001. Energy at 130.8 Hz and its octave
(key 48), 0.25 s windows, dB:

| render | 0–0.25 s | 0.25–0.5 | 0.5–0.75 | … 2.0 s |
|---|---|---|---|---|
| FL, no cut | 48.1 | 47.7 | 48.1 | 48.1 |
| FL, cut itself | 48.1 | 47.7 | 48.1 | 48.1 |
| LYNX before, no cut | 45.1 | 46.7 | 45.1 | 45.1 |
| LYNX before, cut itself | −54.9 | −50.0 | −200 | −200 |

FL renders both the same: its cut groups don't touch Fruity Wrapper channels, like its own plugin generators
(170i3 §2). The wrapper is one plugin class for VST2, VST3 and CLAP; VST2 and CLAP are inferred, not probed.

## 4. Change

`RackState::build`: `InstrumentKind::plugin` joins FL's plugin generators that cut groups never cut (as a target, by
its own group or another's). A plugin channel's notes still cut the Sampler / audio clip / LYNX channels of their
group, as before (not probed). Test `s190x_cut_itself_never_chokes_a_hosted_plugin`.
