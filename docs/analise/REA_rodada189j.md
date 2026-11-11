# Round 189j — an effect's Processing › Connections played (issue #114, next slice)

Round 188d stored and round-tripped an effect's Connections (FL's wrapper, Processing tab) but played only input 2's
index (as the side input). This round measured in FL what input 1's index does on its own and what the outputs'
indexes do, and made the engine play both. It also writes a side input picked with LYNX's "SC" picker as FL's input 2
index on .flp export.

## Method [EXEC]

FL Studio 2026.1.7, `/R /Ewav` command-line renders, no injected input (render180d's `flrun.ps1`: FL minimized
without activation). Each project was written by LYNX's own .flp export (`LYNX.exe --export-flp`) from a `.lynx`
made by `/mnt/e/lynx-work/fxconn189j/py/gen*.py`, then rendered by FL; `py/tones.py` measures each tone's level
(Goertzel, dBFS of a sine) in L and R over 0.5–1.5 s. Renders: `/mnt/e/lynx-work/fxconn189j/render/eNN/`.

The project: four FL Sampler channels playing sines (440 Hz on insert 1, 1000 Hz on 2, 1500 Hz on 3, 2500 Hz on 4);
"LYNX Test SC" (`tools/testplugin/lynx_test_sc_vst2.c`: output pair 1 = input pair 1, pair 2 = input pair 2) in
insert 1's first slot; inserts 2 and 3 routed to insert 1 at level 0 (sidechain only, not to the Master); insert 4 a
reference on the Master; insert 1 routed to the Master and at level 0 to inserts 5 (panned left) and 6 (right).
A tone on its own at unity reads −20.5 dB, two copies −14.4, hard-panned −17.5 on one side.

## Results

`[i1, i2]` = the inputs' indexes, `[o1, o2]` the outputs' (default 0, active).

| Case | Change | 440 | 1000 | 1500 | Reading |
|---|---|---|---|---|---|
| e00 | none ([0,0] / [0,0]) | −14.4 | – | – | input 2 at 0 also gets the insert's audio; both pairs summed into the insert |
| e01 | i1 = 1 | −20.5 | −20.5 | – | input 1 = insert 2 (entry 1 of [1, 2, 3]); the insert's own audio gone from input 1 |
| e02 | i1 = 2 | −20.5 | – | −20.5 | entry 2 = insert 3 (not absolute insert 2) |
| e10 | i1 = 5 | −14.4 | – | – | past the list = the own audio (index 0) |
| e03 | i2 = 1 | −20.5 | −20.5 | – | output 2 (the side input) summed into the insert |
| e08 | i1 = i2 = 1 | – | −14.4 | – | (188d's n06, isolated) |
| e04 | i2 = 1, o2 = 1 | −20.5 | −20.5 centre | – | output index 1 = the Master |
| e05 | i2 = 1, o2 = 2 | −20.5 | L −17.5 | – | index 2 = insert 5 |
| e11 | o2 = 3 | −20.5 | R −17.5 | – | index 3 = insert 6 |
| e12 / e07 | o2 = 4 / 5 | −20.5 | R −17.5 | – | past the list: the last entry |
| e13 | o2 = 1, insert 1 not routed to the Master | – | L −17.5 | – | index 1 = insert 5: the Master is in the list only when routed |
| e18 | o2 = 3, insert 1 also routed to 7 (listed first) | −20.5 | R −17.5 | – | the list is ascending, not in route order: [1, M, 5, 6, 7] |
| e19 | o2 = 1, Master route level 0 | – | −20.5 | – | the Master stays in the list; the output skips the route level |
| e14 | o2 = 1, insert 1 fader 0.4 | −33.2 | −20.5 | – | an output sent elsewhere skips the insert's fader |
| e06 | o1 = 1 | −14.4 | – | – | output 1 to the Master, output 2 (own audio) into the insert |
| e09 | o2 inactive, Process inactive on | −20.5 | −20.5 | – | played (FL's default option) |
| e15 / e16 / e22 | i2 / o2 / i1 inactive, Process inactive off | −20.5 | – | – | that input / output silent |
| e24 | i1 = 1, insert 2 fader 0.4 | −20.5 | −33.2 | – | input = the source's post-fader audio |
| e25 | i1 = 1, insert 2 muted | −20.5 | – | – | post-mute |
| e26 | i1 = 1, insert 2 panned left | −20.5 | L −17.5 | – | post-pan |
| e17 / e23 | o2 = −1 (out of FL's wheel) | −20.5 / −33.2 | centre −20.5 | – | to the Master past the fader |
| e21 | i1 = −1 (out of FL's wheel) | −20.5 | – | – | 2500 Hz doubled: some other track; not modelled (the wheel never gives it) |

So, for an effect on insert t:
- **inputs**: index k plays entry k of [t, every insert routed to t at any level, ascending] (REA_rodada22 §1's list),
  the source's post-fader, post-pan, post-mute audio; 0 or past the list = t's own audio at the slot. Every input,
  input 2 included, is fed this way, so by default a side input gets the insert's own audio.
- **outputs**: the pairs at index 0 are summed into the insert (input 1 is replaced, not mixed); index k goes to entry
  k of [t, the Master if t routes to it, the inserts t routes to, ascending] — past the end the last one — straight into
  that track, past t's fader and route levels.
- an inactive input / output is silent only with "Process inactive inputs and outputs" off (188d).

## LYNX after this round

- `core/Song` `slotConnections` / `insertInputList` / `insertOutputList`: the above, for a hosted effect whose
  Connections are set (`PluginRef::inputs` or `outputs` not empty — every effect of an imported FL project, a LYNX
  effect once its Connections page is touched). Input 2 = the slot's side input source (SC picker or its index), else
  its index, else the own audio.
- `core/RackRenderer`: input 1 / input 2 from their inserts (ordered before the effect like a sidechain source; smart
  disable never skips an effect fed from elsewhere); routed outputs are slot taps (`fruitySendsOf`, PDC included) into
  their track, the Master too (`maxSlotSends` 2 → 8).
- `engine/PluginEffect`: sums the index-0 pairs into the insert, sends the others to their tap, silences inactive ones;
  input 1 from `setMainInput`. With "Use fixed size buffers" output pair 2 (not 3+) can go elsewhere.
- `core/FlpExport`: a picked side input is written as input 2's index in the exported routes' list (an older index
  replaced); an effect with only its inputs mapped gets FL's one default output entry.
- Tests: `tests/FxConn189jTests.inc` (the lists, the renderer with a stand-in, the export); `LYNX --multiouttest
  "LYNX Test Drums.dll" report.txt` with "LYNX Test SC.dll" beside it runs 23 of the cases above through the real VST2
  and compares every tone with FL's level (±0.5 dB).

## Open — decision for the user

FL's defaults also apply to an effect nobody touched: its side input gets the insert's own audio and every output pair
is summed into the insert (e00). LYNX keeps its earlier behaviour for those (side bus on only with a picked source,
issue #109's fix; output 1 only), so a plugin with two output pairs sounds different from FL until its Connections are
set. Recommended: keep it (no change to saved LYNX projects; FL projects already play FL's way); option: make FL's
defaults universal after an A/B on real sidechain plugins.

## Best way?

- **FL:** above [EXEC].
- **Ableton Live:** a device's sidechain picks its source in its own "Audio From" chooser; a plugin's extra outputs
  are taken by other tracks' "Audio From" (the device output), never pushed from the device; no index list to learn.
- **Users:** the usual FL advice for a VST sidechain is "route the source to the insert, then in the wrapper's
  Processing › Connections set the side input from --- to 1" (FabFilter's support on its forum,
  prod.fabfilter.com/forum/topic/1089; or press "Auto map inputs", prod.fabfilter.com/forum/add-comment/5803); an
  Image-Line forum proposal asks for a faster way because FL does not know whether a plugin expects a sidechain and
  the index has to be set by hand (forum.image-line.com/viewtopic.php?p=2061097).
- **Verdict:** FL-exact by default. Possible LYNX extra (not built): show the insert name next to each index in the
  Connections wheels ("1 · Kick") — display only, nothing changes in the data.
