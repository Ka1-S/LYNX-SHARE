# Round 188d — Plugin wrapper Processing › Connections (multi-output generators, issue #114)

Issue #114 ("I cannot create sends in the processing window for a drum plugin and route the instruments to the
channel rack"): the reporter's screenshots show MT-PowerDrumKit in FL's wrapper, Processing tab, Connections list
after "Auto map outputs" (outputs 1…7 on offsets 1…7). LYNX took only a plugin's main stereo output.

## Evidence

- **[EXEC]** FL Studio 2026.1.7 (build 5654), sessions 2026-11-09 08:17–08:31 and 08:46–08:48, shots in
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

| Second session (08:46): "LYNX Test Drums 3", the Crash pins without kVstPinIsActive | j18 | "8. Crash" starts with its LED off; the others on |

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

## Second block (coordinator's follow-up, same round)

### .flp records [DATA: FL 2026 saves mo-a.flp, mo-b.flp, mo-c.flp in /mnt/e/lynx-work/multiout188d/fl/]

Wrapper (event 0xD5, version 12) records, written for every hosted plugin:

| Record | Layout | Seen |
|---|---|---|
| `0x1E` | 4 × u32: input count, output count, 0, 0 | Drums 2: `0, 8`; LYNX Test SC (2 in / 2 out pairs): `2, 2` |
| `0x1F` | per input: `i32 index, u32 active, u32 0` | SC2 after Auto map inputs: `{0,1}, {1,1}` |
| `0x20` | per output: `i32 offset / index, u32 active, u32 0` | Drums 2 after Auto map, Clap off, Hat at −2: `{0,1} {1,1} {2,0} {−2,1} {4,1} {5,1} {6,1} {7,1}` |

Record 0x02's flags word: `0x7D84` in mo-a (after an LED was turned off: "Process inactive inputs and outputs" off)
and `0x7D8C` in mo-b (turned back on): **bit 0x8 = "Process inactive inputs and outputs"** (on in FL's default
0xB4007D8C). LYNX reads and writes all three records and the bit (`core/FlWrapper.cpp`, `core/FlpExport.cpp`).

### "Process inactive inputs and outputs" [EXEC k29–k34]

On (default), an output whose LED is off still plays on its insert (Clap off at offset 2 plays on insert 2 again,
k33); turning any LED off turns the option off on FL's page (a38/b23/k19, mo-a's flags). LYNX: wrapper option in
Processing › Options, saved, exported; the engine plays inactive outputs while it is on; an LED click off clears it.

### Effects [EXEC k65, n00–n25; DATA mo-c]

An effect's Processing › Connections has two columns: inputs (wheel, "n. name", LED) and outputs (LED, "n. name",
wheel); header: inputs glyph, **Auto map inputs**, Reset, **Auto map outputs**, outputs glyph, refresh. Hints:
"Sidechain index for input 1", "Deactivate input 1", "Assign all inputs incrementally", "Sidechain index for output 1".
With insert 2 sidechained to insert 1 (LYNX Test SC2 on insert 1, whose "Side" output is its side input): input 2 at
"---" → insert 1 silent (n13); input 2 at 1 → insert 2's audio on insert 1 (n19); inputs 1 and 2 both at 1 → louder (n06; input 1's own
effect was not isolated). So index k picks entry k of [the insert itself, the inserts routed to it in ascending order]
(REA_rodada22 §1's list, which the Fruity effects' sidechain indexes use).

LYNX: the effect's page shows both columns; input 2's index sets the slot's side input source (and LYNX's SC picker
shows as the matching index); Auto map inputs, Reset as FL; imported projects take the side input from the index.

## Not done (open)

- (Done in round 189j, `docs/analise/REA_rodada189j.md`: an effect's input 1 index, its output indexes and the .flp
  export of a picked side input.)

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
