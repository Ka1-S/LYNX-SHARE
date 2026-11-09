# Round 188d — Plugin wrapper Processing › Connections (multi-output generators, issue #114)

Issue #114 ("I cannot create sends in the processing window for a drum plugin and route the instruments to the
channel rack"): the reporter's screenshots show MT-PowerDrumKit in FL's wrapper, Processing tab, Connections list
after "Auto map outputs" (outputs 1…7 on offsets 1…7). LYNX took only a plugin's main stereo output.

## Evidence

- **[EXEC]** FL Studio 2026.1.7 (build 5654), session 2026-11-09 08:17–08:31, shots in
  `/mnt/e/lynx-work/multiout188d/shots/fl/`. Plugin: `tools/testplugin/lynx_test_drums_vst2.c` (8 stereo outputs
  named Kick … Crash, all pins flagged active; "LYNX Test Drums 2" plays output (key − 36) mod 8 so the typing
  keyboard reaches each output). FL found it after adding the search path `E:\lynx-work\multiout188d\vst\` in the
  Plugin Manager (quick scan); the settings changes are logged in `/mnt/e/lynx-work/multiout188d/fl/settings-change.log`.
- **[DATA]** `Fruity Wrapper_x64.dll` form resources (TPF0): `ConnectionsPanel` (caption "Connections") with
  `ResetIOBtn` "Reset" (hint "Set all inputs and outputs to their default values"), `AutoAssignInputsBtn`
  "Auto map inputs" ("Assign all inputs incrementally"), `AutoAssignOutputsBtn` "Auto map outputs" (Tag 1, "Assign all
  outputs incrementally"), the inputs / outputs glyphs, `RefreshConnectionNamesBtn` ("Refresh inputs and outputs (after a
  change in the plugin)"), `ProcessInactiveBox` "Process inactive inputs and outputs".
- **[DECOMP]** (Ghidra on the same DLL, published handler addresses from the Delphi method table):
  `FUN_0078aea0` (AutoAssignInputsBtnClick, shared by both buttons through Tag): starts at the first input's /
  output's current value and gives each next row one more, every row of that side (no check of the LED);
  `FUN_00790380` (ResetIOBtnClick): every row's wheel to 0; `FUN_0078df00` (rows): name = the plugin's pin label (64
  chars), else "Input %d" / "Output %d"; a generator's output wheel ranges −999…999 with the hint "Mixer track offset for
  %s %d" (an effect's: "Sidechain index for …", 0…tracks−1); `FUN_00794040` sets a row's value.

## What FL does [EXEC]

| Step | Shot | Result |
|---|---|---|
| Processing tab of a fresh 8-output plugin | a31 | rows "1. Kick" … "8. Crash", LEDs on, every wheel "---" |
| Hover a wheel / an LED / the buttons | a32, hints1 | "Mixer track offset for output 2", "Deactivate output 2", "Assign all outputs incrementally", "Refresh inputs and outputs (after a change in the plugin)" |
| Auto map outputs | a33 | Kick "---", Snare 1, Clap 2 … Crash 7 |
| LED of Snare off | a38 | the row dims, its value stays |
| Reset | a40 | every wheel "---", Snare's LED still off |
| Channel on insert 5, defaults, play Kick / Snare / Clap | c0x | all on insert 5 |
| Auto map, Clap inactive, play | d0x, e0x | Kick 5, Snare 6, Hat 8, Tom 1 9 … Crash 12; Clap: no meter anywhere |
| Channel pan 100 % left | g0x | Snare (insert 6) also left only |
| Channel volume 0 % | h0x | Kick and Snare silent |

So: target insert = the channel's insert + offset; offset 0 ("---") = the channel's insert; an inactive output is
silent; the channel's volume and pan act on every output. Seen once more: after an LED was turned off, the "Process
inactive inputs and outputs" box showed off (a38, b23) — not modelled (LYNX has no such option yet).

## LYNX after this round

- `src/engine/Vst2Format.cpp`: a VST2's outputs past the first pair are extra stereo buses named by
  `effGetOutputProperties` (the pin label; "Output n" without one), enabled by default when the pin is flagged active.
- `src/engine/PluginHost.cpp`: `pluginOutputs()` lists a plugin's outputs (per bus; a one-bus plugin with more than 2
  channels — a bridged one — in pairs). `PluginInstrument` mixes each active output to the channel's insert (offset 0) or
  to its extra-output buffer for insert + offset (the renderer's existing path, as the generator Patcher's outputs), with
  the channel's gains; fixed-size buffers carry every output channel. The bridge's shared block grew from 8 to 32 channels.
- `src/core/Song.h` `PluginOutputRoute` {offset, active −1/0/1 — −1 = as the plugin declares}, saved per plugin
  (`outputs` in the project), `core/FlWrapper` `autoMapOutputs` / `resetOutputOffsets` as FL's handlers.
- `src/ui/PluginWindow.cpp`: the Connections group in the Processing tab of a generator: Reset, Auto map outputs, the
  outputs glyph, Refresh; rows with LED, "n. name", the offset wheel (wheel, vertical drag, double-click to type).
- Tests: `tests/MultiOut188dTests.inc`; `LYNX --multiouttest "LYNX Test Drums.dll" report.txt` (real VST2 through the
  engine, master measured per case).

## Not done (open)

- Effects' Connections (inputs as sidechain indexes, "Auto map inputs"): LYNX's sidechain picker covers side inputs.
- Importing / exporting the mapping in .flp (the wrapper data).
- Which outputs start active when a VST2 does not flag its pins (FL's rule not observed: the test plugin flags all).
- "Process inactive inputs and outputs".

## Best way?

- **FL:** above — per-output LED + offset, Auto map from output 1's offset, inserts not renamed, used inserts not skipped.
- **Ableton Live:** the instrument stays on its MIDI track; each extra output is taken by an audio track whose
  "Audio From" picks the instrument's output ("Kick", "Snare"…); a Drum Rack instead gives each pad a chain shown as
  its own mixer strip ("Show/Hide Chains"), no routing needed.
- **Users:** FL users like Auto map as a one-click setup but complain that it maps onto inserts already in use
  (Image-Line forum, "Instrument tracks vs. multi out instruments", forum.image-line.com/viewtopic.php?p=1973798);
  Native Instruments' FL guides for Maschine (Auto map outputs) and Kontakt (each output set by hand) both go through
  Processing › Connections (support.native-instruments.com), so the feature is what multi-output users look for.
- **Verdict:** FL-exact by default. Proposed option (not built, for the user): "Auto map to free inserts" — start at the
  first empty insert after the channel's and name each insert after its output (Live-like strips named Kick, Snare…),
  as an extra menu item beside FL's button.
