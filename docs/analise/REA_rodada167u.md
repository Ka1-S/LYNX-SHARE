# Round 167u — PR #28 (LordVersA): "Mixer: drop a generator on a strip to add a channel routed to that insert"

Standard: docs/analise/REA_rodada165-issue11c.md (per item: reporter's words | FL evidence | LYNX before | LYNX now).

## 1. The PR (+53/-13, 10 files, commit c9259b88 by Parsa Radfar, kept as it is)

- **Contributor's words:** "Dragging a generator from the Browser's Plugin database onto a Mixer strip now adds a new
  channel routed to that insert, as one undo step. Dropping on Master routes the channel straight to Master. [...]
  Dragging sample files or Soundfont presets onto a strip is not part of this PR." Tested on macOS only.
- **Review:** `rackops::addChannelRouted` (new, core) is what `ChannelRackComponent::addChannel` now calls (an
  `insert` argument, default -1 = claim a free insert as before); `MixerStrips` accepts generator drags next to effect
  drags and calls `onInstrumentDropped`; `MixerComponent` selects the track and forwards to `MainComponent`, which
  adds the channel through the rack (one `editSong` = one undo step, editor opened like any new channel). Effects
  dropped on a strip are unchanged (still need `onEffectDropped`). The test `workflow_add_channel_routed` covers the
  last insert, Master, nothing claimed, an out-of-range insert and the default; its last two lines compare against a
  copy (`expected`) on which `claimFreeInsert` was already called — convoluted but correct (claiming doesn't change
  which insert is free while no channel is routed there). No new UI text. Clean, no change requested.

## 2. FL evidence (FL Studio 2026, FL64 / FLEngine_x64.dll)

- [REA, FLEngine_x64.dll, Ghidra headless on a copy of the analysed project] the mixer's drop handler
  `FUN_012875e0` (refs to the strings "drop instrument track" 0x128a91c and "drop mixer/plugin preset" 0x128a968):
  - on a track (`FUN_0127f7c0` finds no slot under the cursor) it first selects the track hit (`FUN_0128fa20`); a
    generator/channel preset (`FUN_00f54cb0(file, 0x31 | 0x20)`) gets the undo entry **"insert channel"** and is
    loaded for the current mixer track (`FUN_012a73a0(track, file)` → `FUN_00f54760(file, 0x40, track)`); the track
    index only has to be 0..500, so **Master (0) is accepted**;
  - on an effect slot, a generator also gets **"insert channel"** (effects get "insert effect plugin" / "replace
    effect plugin"), then the queued slot loader `FUN_00dd4db0(track, slot, files)`;
  - on the empty mixer space (no track hit) a generator makes a new **instrument track** ("drop instrument track"),
    a mixer preset a new mixer track.
- [DOC, FL's own Help/WhatsNew.htm] 9.0.0: "Channel & generator presets onto Mixer tracks & effect slots";
  20.9 #11191 fixed "Dropping a generator on a Mixer track creates a 'load Mixer state' undo entry"; #15490
  "Dropping a generator on a mixer slot doesn't add the plugin and crashes on undo"; 24.2.99 #19807 "Create a new
  instrument track when a generator preset is dropped on empty space in the mixer".
- [SHOT, one planned FL session, both locks, closed normally with Save changes? › No] `E:\lynx-work\pr28\s\`
  (Browser › Plugin database › Generators › Synth classic, default template, 808 channels on inserts 1–5):
  - `fl3` 3x Osc dropped on Insert 3's strip → new channel "3x Osc" at the end of the rack, routed to 3, its editor
    opens ("3x Osc (3x Osc)"), Insert 3 is selected and **renamed "3x Osc"** (it had its default name, and
    808 HiHat was already routed there);
  - `fl4` 3x Osc dropped on Master → channel **"3x Osc #2"** routed to Master ("---"), editor "(Master)"; Master's
    strip is **renamed "3x Osc #2"**;
  - `fl6` Insert 5 selected, 3x Osc dropped on its **effect slot 2** → channel "3x Osc #3" routed to 5, editor open,
    **slot 2 stays empty**, Insert 5 renamed "3x Osc #3";
  - `fl8`/`fl10` FLEX dropped on Insert 3 (now named "3x Osc") → channel "FLEX" routed to 3, editor open, **Insert 3
    keeps "3x Osc"**; `fl10` shows the four channels (3, ---, 5, 3) and the strip names; `fl11` desktop clean.
  - Rule from the shots: the insert takes the channel's name while it has its default name ("Insert N", "Master"); a
    name already given stays. A channel name that exists gets " #2", " #3".

## 3. LYNX before / now

| Case | FL 2026 | LYNX with the PR | LYNX now (our commit on top) |
|---|---|---|---|
| Generator on an insert strip | new channel at the end, routed there, editor opens, track selected | same | same |
| Generator on Master | channel routed to Master | same | same |
| Generator on an effect slot | same as on the strip; slot unchanged | nothing (the effects panel took only effects / .fst) | same as the strip: new channel routed to the panel's insert, slots unchanged |
| Name of the target insert | takes the channel's name if still default | unchanged | takes it if default (`""`, "Insert N", Master's "Master") |
| Channel name already used | "3x Osc #2", "#3" | duplicate name | " #2", " #3" (same helper as Clone) |
| Generator on the empty mixer space | new instrument track | nothing | nothing (left: LYNX's mixer "+" area isn't a drop target yet) |

- Code: `rackops::addChannelRouted` (core) does the naming when an existing insert is given; plain new channels
  (`insert = -1`: rack "+", Browser double-click, drop below the rack's channels) are unchanged.
  `EffectsPanel::isInterestedInDragSource/itemDropped` accept generator drags and call `onInstrumentDropped(track)`,
  wired to the strips' handler in `MixerComponent`.
- Test: `pr28_generator_dropped_on_an_insert_names_it_like_fl` replays the FL session (fl3–fl10) plus a user-named
  insert and a plain add; the PR's `workflow_add_channel_routed` still passes unchanged.

## 4. Verification

(see "Where I stopped")

## Where I stopped

- Done: review; REA + one FL session (shots above); follow-up commit on top of c9259b88 (his commit untouched);
  docs/WORKFLOW_FL.md B4 updated.
- Next: build-local with FL Data, Windows build, Windows GUI drag check (strip, Master, effect slot), land with
  `flock ~/projetos/lynx/.git/lynx-merge.lock ~/.claude/jobs/f8789b49/tmp/coord/land.sh pr28`, notes + reply draft.
- Commands: `~/.claude/jobs/f8789b49/tmp/coord/buildslot.sh ~/.claude/jobs/f8789b49/tmp/coord/bl-wt.sh pr28`,
  `OUT=build-win ~/.claude/jobs/f8789b49/tmp/coord/winbuild.sh scripts/build-windows-zig.sh` (zig caches ~/.cache/zig-pr28*).
