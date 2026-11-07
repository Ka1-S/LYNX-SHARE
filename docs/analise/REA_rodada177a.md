# REA round 177a — mixer: routing that stays put, folders/groups, VCA faders

Round 177a covers three mixer areas the user approved on 2026-11-06 (research: `/mnt/e/lynx-work/ableton/research-web-2.md`
row 4 and §3 items 1, 4):

1. **Slice 177a-1:** moving, inserting or deleting mixer inserts never re-wires a send, side-chain, route, automation or
   link.
2. **Slice 177a-2:** collapsible mixer groups/folders. The design goes to the coordinator first.
3. **Slice 177a-3:** VCA faders. The design goes to the coordinator first.

## 1. Routing stays put when inserts move (slice 177a-1)

### 1.1 What FL does [DECOMP FLEngine_x64.dll, FL Studio 2026]

FL's Mixer › "Move left / right" (strings `&Move left` / `&Move right`, `FUN_00a7aa40`) goes through an undoable
command (`FUN_01018cc0` "move mixer track(s)" → `FUN_0101caa0` → command class at `FUN_010215a0`, executed by
`FUN_010216b0`). The command calls `FUN_012a4490 (delta)`. That function:

- builds a permutation of the 502 track slots (`local_48[i] = i`);
- swaps the selected tracks one place with `FUN_012a43d0`, with wrap-around over the used tracks;
- applies the permutation in one step with `FUN_012a4030 (perm)`.

`FUN_012a4030` is FL's whole "keep the links" pass. It rewrites, in order:

| Reference | Where in FL | Follows the move? |
|---|---|---|
| The track records themselves (0x1478 bytes each) | copied through a temporary array | yes (they move) |
| Each track's route array (+0x394: 502 send levels, one per destination) | permuted by the same table | **yes**: sends and side-chain routes follow |
| Playlist audio tracks (mode 1) linked to an insert (+0xb8 of each of the 500 tracks per arrangement) | inverse table | yes |
| Each slot's owner track (`FUN_012be0c0` per slot: +0x130 track, +0x9c event id `0x70000000 + track << 22 + slot << 16`) | rewritten | yes |
| Channels' mixer track (+600 of each channel, `PTR_DAT_015e2218` list) | inverse table | yes |
| Automation clips, event data, remote-control links, the "last tweaked" ids, the Patcher's links (`FUN_012cb6c0` with callback `FUN_012a3f00`) | event ids `0x2000 + track` (mixer params) and `0x70000000…` (slot params) | yes. **A deleted track maps to −1 and the target is cleared (`0xffffffff`)** |
| Playlist track records (`FUN_00d2bed0` + `FUN_012a4010`, field +0x268) | inverse table | yes |

What FL does **not** touch: numbers stored inside a plugin's own state. A plugin is only told its new owner track
(`FUN_012be0c0`), so the following keep pointing at the old insert number and end up on another track after a move:

- Fruity Send's "Send to" (an insert index in its 16-byte state, REA_rodada105_send §1);
- the Transporter's loops send (REA_rodada167f);
- the Soundfont Player's reverb / chorus "send to" track (REA_rodada158 §1);
- the Voltage Controller's extra outputs (REA_rodada164a);
- a Patcher's side inputs and outputs. They are ordered by source position, so a reorder changes which cable gets which
  track. This is the complaint in [IL forum, Aug 2025](https://forum.image-line.com/viewtopic.php?p=2024623)†: "whenever
  you reorder tracks on the mixer that are routed to a bus with patcher on it … the order changes inside patcher as
  well"; the poster wants outputs "set by reference rather than by some offset".
- a generator's extra outputs, which FL routes *relative* to the channel's insert ("the k-th insert after"). Moving the
  channel's insert carries them to other tracks.

The IL manual's Mixer menu page documents Move left / right, Insert and Delete, but says nothing about links
([mixer_mixermenu.htm](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm)).

### 1.2 What LYNX did before

`mixerops::remapTrackReferences` already followed channels, Playlist audio tracks, sends, side-chains, Transmitter
sends, automation targets and controller sources. That matches FL's own pass. The gaps were:

1. **Plugin-side numbers** were not remapped: Fruity Send (`params[3]`), the Transporter (`params[24]`), a Patcher's
   "To LYNX" outputs (`PatcherDoc::outputTracks`, effect and generator), the Soundfont's `pReverbTrack` / `pChorusTrack`,
   and the Voltage Controller's `tracks` (in its saved state). Moving an insert re-wired them, as in FL.
2. **A generator Patcher's relative outputs** (`outputTracks[k] = -1`: the k-th insert after the channel's own) landed on
   another track whenever the channel's insert and the output's insert moved apart.
3. **Effects inside a Patcher** (a Fruity Send node, a nested Patcher) were not visited.
4. **Delete** shifted the deleted insert to the end with one swap per following insert. So every automation clip,
   MIDI link, initial value or pattern event of the deleted insert then drove the *blank insert at the end*. A
   side-chain from it became `0`, which the renderer reads as "the Master". It was also O(n²) on a big mixer.

### 1.3 What LYNX does now

`mixerops::remapTrackReferences (song, map)` takes `map: old insert → new insert, or -1 = deleted`. `swapTracks`,
`moveTracks`, `insertTracks` and `deleteTrack` all go through it, so every reference passes through one table, as in FL.

| Reference | Move / insert | Deleted insert |
|---|---|---|
| Channel's insert | follows | the Master (FL: the same) |
| Playlist audio / instrument track link | follows | none; an audio track becomes unassigned |
| Sends (incl. level-0 side-chain sends) | follow | removed |
| Side-chain sources (`sidechain`, `sidechain2`) | follow | −1 (none) |
| Transmitter / EQUO `sendTracks` | follow | 0 (off) |
| Fruity Send "Send to", Transporter send | follow (**LYNX fix; FL re-wires**) | none |
| Patcher "To LYNX" outputs (insert chosen) | follow (**LYNX fix**) | effect Patcher: none; generator: the Master |
| Generator Patcher relative outputs (−1) | stay relative while they still land on the same track; else pinned to the track they fed (**LYNX fix**) | the Master |
| Soundfont reverb / chorus send track | follow (**LYNX fix**) | −1 (none) |
| Voltage Controller outputs 2–4 | follow (**LYNX fix**) | 0 (off) |
| Effects in containers and in Patchers (any of the above) | follow | as above |
| Automation clips, MIDI links, initial values, pattern events (mixer volume / pan / send / slot params / slot mix) | follow | target cleared (FL: −1). Links and initial values without a target are removed |
| Controller source (`Automation::source.track`) | follows | −1 (no longer live) |

Moving effect slots inside an insert was already correct: `Song::swapEffectSlots` / `removeEffectSlot` carry automation,
links and controller sources (round 170b).

**Is this the best way?**

- **FL:** FL follows the mixer's own links but leaves plugin-side numbers and Patcher port order alone. Users read the
  result as "silent re-wiring" (IL Aug 2025†; [IL wishlist 2026](https://forum.image-line.com/viewtopic.php?t=338528)†).
- **Ableton Live 12:** routing is by track object (an `.als` `AudioOut` target names a track id, not a position;
  `ALS-FORMAT.md`), so reordering never re-wires. Returns can't be reordered without re-dialling sends
  ([AB forum](https://forum.ableton.com/viewtopic.php?p=1794161)†), which is a different limitation.
- **Users:** they want "routing by reference" and a disconnected link shown greyed out instead of shifting the others.
- **Verdict:** keep links by logical track everywhere. The parts FL already keeps are FL-exact. For the plugin-side
  numbers, FL's behaviour is the bug users report, so LYNX keeps those links too. No option is added, because no user
  wants the re-wiring. If the user wants an "FL-exact (relative)" switch, it is a one-line map change. A deleted
  insert's links are dropped, never moved.

Not covered here:

- The metronome's insert is an app setting, not a project setting (FL: the same).
- `TakeRecording::Journal::insert` lives only for the length of a recording.

### 1.4 The .flp export keeps the moved numbers

The remap runs only on edits inside LYNX. It never runs on load or save, so FL import and export stay byte-faithful.

**Effects exported as their own FL state.** An effect imported from FL is exported as its imported FL state
(`FlOrigin`) while its fingerprint matches. For these effects, the remap also rewrites the insert numbers inside that
state and re-stamps the fingerprint. Every other byte stays as imported. The numbers rewritten:

| Plugin | Number rewritten | Position in the state |
|---|---|---|
| Fruity Send | send insert | i32 at 12 |
| Transmitter | transient and sustain sends | i32s after the version and 42 controls |
| Transporter | send track | after the controls and the two flag bytes, source and stereo |
| Soundfont Player (channel) | reverb and chorus targets | after the path |

The Voltage Controller's state is FL's own format, so the export writes the remapped state.

**Side inputs (SC k).** FL's dynamics name their side input by its place *k* in the insert's inputs list: itself
first, then the inserts routing into it, in number order (REA_rodada22 §1; the importer's `sidechainSource`). A move
can change that order. FL itself then keeps the old *k*, which is the IL complaint. `flpexport` now runs a
`fixSideInputs` pass after the mixer is built:

1. It rebuilds the inputs list from the routes it writes.
2. For each effect exported as its FL state, it rewrites *k* only when the stored *k* no longer names LYNX's side-chain
   source. Otherwise the bytes stay.

| Plugin | Where *k* sits |
|---|---|
| Fruity Limiter | 0xA4, version ≥ 6 |
| Fruity Compressor | 0x20, version 3 |
| LuxeVerb | 4 |
| Transmitter | after its sends |
| Transporter | its input track |
| Vocodex | ModIn and CarIn, options block +20 / +24; no ModIn with L/R encoding, no CarIn with the internal carrier |

### 1.5 Tests

`tests/Mixer177aTests.inc`:

- `mixer177a_moving_inserts_keeps_every_route_on_its_track`: one project holds every kind of reference above. Every
  reference is checked by the track's *name* after:
  - a single move;
  - a block of three moved right 5× and left 6×;
  - a swap of the first and last inserts;
  - inserting three at the front and one at the end.
- `mixer177a_relative_patcher_output_stays_relative_when_it_still_lands_right`: a relative output that still lands right
  stays relative. One that would move is pinned.
- `mixer177a_deleting_an_insert_drops_its_links_and_never_moves_them`: after a delete:
  - Fruity Send, Transmitter, Patcher, Soundfont and Voltage links to the deleted insert are cleared, and the others
    follow;
  - its automation has no target and its MIDI link is gone;
  - a side-chain from a deleted insert is −1;
  - a controller in a deleted insert is no longer live.
- `mixer177a_moved_track_exports_the_right_insert_numbers_to_flp`:
  - unmoved, the imported Fruity Send / Transmitter states export byte for byte;
  - after a block move and a swap, they export with the new insert numbers, the other bytes unchanged.
- `mixer177a_side_input_numbers_follow_the_source_in_the_flp_export`: a Fruity Limiter listens to the second of two
  side-chain sources. Unmoved, it exports byte for byte. After the two sources swap places it exports `k = 1`, which
  still names the same source; FL itself would keep 2. Nothing else in the state changes.
- `UiFl115Tests`: a deleted side-chain source is now −1 (none), no longer 0 (which the renderer reads as the Master).

## 2. Mixer groups fold (slice 177a-2)

### 2.1 FL and the others

- **FL Studio 2026:** a mixer "group" is a run of inserts set off by separators (Group › Separator / Create group /
  Auto color group; REA_rodada146a). FL draws the separator line and tints the strips. It has no way to fold a group:
  a large mixer is only scrolled, docked, or narrowed with the Mixer layouts.
- **Ableton Live 12:** a group track folds its members into one strip; the group strip is a real bus (its own
  devices, fader and sends).
- **Logic (summing stacks), Studio One (folder tracks), Cubase (folder and group channels), Reaper (folder tracks):**
  a folder hides its members; a summing folder shows its bus strip.
- **Users:** IL wishlists ask for mixer groups or folders ([IL mixer wishlist, Jun 2025](https://forum.image-line.com/viewtopic.php?p=2008787)†, [IL wishlist 2026](https://forum.image-line.com/viewtopic.php?t=338528)†). Live's simple group tracks get praise (research row 4). The request is "fold a big mixer like Live's groups, without changing the routing".
- **Verdict:** FL's separator groups stay the default and nothing changes until a group is folded. Folding is an addition (coordinator's decision 2026-11-06: flat v1 on FL's groups, a non-bus group folds to a slim tab).

### 2.2 What LYNX does

LYNX folds FL's own separator groups. No new group object is added and the routing never changes:

- A group can fold when a separator sets it off and it has two or more inserts (`mixerops::canFold`). Its first strip
  shows a chevron after the number. A click folds or unfolds it; **Alt+click** does every group. The insert menu's
  Group section has *Fold group* / *Unfold group*, *Fold all groups* and *Unfold all groups*.
- **A summing group** (a member every other member sends only to, as *Create submix* makes it; `groupBus`) folds to
  its bus strip. The bus carries the chevron, to unfold.
- **Any other group** folds to a slim tab (20 px) in the group's colour. It shows the chevron, the number of inserts
  and one bar with the loudest member's level. A click unfolds it. The members are never hidden without a trace.
- The fold flag is saved with the project (`folded` on the group's first insert). FL projects don't carry it; the .flp
  export leaves it out.
- **Cost:** nothing is computed while no group is folded. The fold view is built only at layout time.

### 2.3 Tests

`tests/Mixer177a2Tests.inc` (`mixer177a2_groups_fold_to_their_bus_or_a_tab`): which groups can fold, the bus of a
summing group, the fold view of a summing group (only the bus shows) and of a group without a bus (a tab), a member
with a second output (the group then folds to a tab), Fold all / Unfold all, and the fold following its group when the
inserts move.

## 4. Measured in the real windows (2026-11-07)

### 4.1 The mixer picture and folded groups

`--uiperf` with `LYNX_UIPERF_ONLY="mixer meters"`: 6 s playing with the strips' picture on (`/mnt/e/lynx-work/mixer177a/gui/`).
Machine under load, so the CPU figures are noisy and only the picture counts are compared.

| Project | Picture spoiled | Picture drawn again |
|---|---|---|
| Folded tab and bus, VCAs, an automated insert folded away, before the fix | 342 | 313 |
| The same project, after the fix | 0 | 1 |
| Folded tab and bus, VCAs, no automation | 0 | 1 |
| VCAs, unfolded, no automation | 0 | 1 |
| No VCA, no fold, no automation | 0 | 1 |

A visible automated fader still spoils its own area on purpose: 350 times in the VCA project, 339 without a VCA.

### 4.2 FL's fader rail click [FL Studio 2026, real clicks, measured from screenshots]

A click on the rail away from the cap does **not** jump the fader. Each click moves the cap about 1 screen pixel towards
the click: below the cap 748 → 749, above it 748 → 747, and ten more clicks below it 750 → 760. A press on the rail
followed by a 20 px drag moved the cap 7 px, so the drag is relative to the press and doesn't jump either.

LYNX jumps the fader to the click (`MixerStrips::mouseDown`, "clicking the track jumps the fader there"). This differs
from FL. It is unchanged here and goes to the coordinator. The exact step in fader units isn't measured yet.
