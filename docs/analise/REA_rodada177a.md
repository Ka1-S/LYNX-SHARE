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

## 3. VCA faders (slice 177a-3)

### 3.1 FL and the others

- **FL Studio 2026:** no VCA. The closest tools are linking several faders to one controller (Multilink / a
  Formula or Peak controller) or a submix bus. A bus changes the sound path (one more insert, its effects, its PDC);
  a link moves the faders themselves, so the members' own fader values are lost.
- **Pro Tools, Cubase, Studio One, Logic, Reaper (VCA faders / VCA groups):** a VCA strip carries no audio. It offsets
  its members' faders in dB, its mute and solo act on them, VCAs nest, and Pro Tools' *Coalesce VCA* (Studio One:
  *merge*) bakes the VCA into the members. **Ableton Live 12** has none.
- **Users:** FL users have asked for a VCA since 2021 ([IL VCA 2021](https://forum.image-line.com/viewtopic.php?p=1646924)†), and Live users ask too ([Ableton forum](https://forum.ableton.com/viewtopic.php?p=1774646)). Studio One's folder tracks with VCA get praise ([MusicRadar](https://musicradar.com/how-to/vca-grouping)).
- **Verdict:** an addition, since FL has none: nothing changes until a VCA is made. Coordinator's decision 2026-11-06: VCA block left of the Master, *Merge VCA into members* in v1 (undoable).

### 3.2 What LYNX does

A **VCA** (`Song::vcas`) has a name, a colour, a fader, a mute, its members (mixer inserts) and an optional parent
VCA.

- **Make one:** insert menu › Group › VCA › *New VCA from selected*. *Assign to VCA* and *Remove from VCA* are in the
  same submenu. A VCA's own menu has *Assign selected inserts* and *Remove selected inserts*.
- **Where:** the VCA strips have their own block, left of the Master, in the Master's dock. A VCA strip shows "VCA"
  where an insert shows its number, the member count, the name, mute, solo, the loudest member's meter and the
  fader. A dashed outline marks it as a strip that carries no audio. It has no slots, pan, EQ or routing.
- **Fader:** the VCA's gain (FL's fader curve; unity = 0 dB) multiplies each member's fader gain, so the levels add
  in dB. A member keeps its own fader value and automation. A tick on the member's fader shows the level that plays,
  and the member's fader hint names its VCAs and that level. The VCA fader drags, takes the wheel and resets on a
  double-click like an insert's. Its level is an automation target (*VCA level*).
- **Mute / solo:** the VCA's mute mutes its members. Its solo solos them with FL's solo rules (`soloSelected`). A
  click on the VCA's name selects its members.
- **Nesting:** *Follow VCA* sets a parent. The parent's gain and mute reach the child's members. The menu never
  offers a parent that would make a loop, and the gain code has a cycle guard too.
- **Merge VCA into members** (undoable): each member's fader and volume automation take the VCA's gain, then the VCA
  goes back to unity. The sound doesn't change, within the faders' range. **Delete VCA:** the members keep their
  faders, child VCAs lose their parent, the VCA's automation loses its target.
- **Moves:** inserting, moving or deleting inserts keeps the members (the slice-1 remap). A deleted member leaves
  the VCA.
- **Saving:** VCAs are saved in the project (`vcas`). FL has no VCA. The .flp export writes each member's fader with
  its VCA gain baked in, and its report says that VCA automation and mutes are left out.

### 3.3 Cost and the audio thread

- With no VCA, `RackState::vcaChains` is empty and the per-insert check is one `empty()` test.
- With VCAs, the snapshot holds each insert's VCA chain, built off the audio thread. The audio thread multiplies the
  insert's levels by the chain's gains, read from the snapshot's `LiveLevels::vcaLevel` atomics (a live drag) or the
  VCA's automation. It takes no locks and makes no allocations. A VCA mute is baked into `mixerAudible` when the
  snapshot is built.

### 3.4 Tests

`tests/Mixer177a3Tests.inc`:

- `mixer177a3_vca_gains_add_in_db_nest_and_follow_moves`: unity on create, dB adding, nesting, mute through the parent,
  membership after inserts move and a member is deleted, assign / remove.
- `mixer177a3_merge_and_delete_vca`: merge keeps each member's level and scales its volume automation; merge at unity
  does nothing; the *VCA level* target reads and writes; delete clears children's parent and the automation target.
- `mixer177a3_vca_scales_its_members_in_the_render`: no VCA leaves the snapshot without chains; a −6 dB VCA lowers
  only its member by 6 dB; a live level store reaches the render without a rebuild; a VCA mute silences only its
  member.
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

The step, measured in fader units: one click moves the value by 0.0056 of the travel (FL's hint: 0.0 dB → -0.2 dB at
unity). Held, the press repeats about every 64 ms (about 15.6 steps a second, with no first delay) and stops once the
cap's centre is under the mouse.

**Timing, measured 2026-11-07 13:13 (FL, inserts 1–8 at 0 dB, a press below the cap held for a set time, cap read from
the pixels at about 1.07 px a step):** 50 ms → 1 step, 100 → 2, 130 → 2, 160 → 3, 200 → 3, 250 → 4, 300 → 5,
400 → 7. FL repeats one period (about 64 ms) after the press, with no longer first delay. The 130 and 200 ms points are
one step below `1 + t / 64`, within the pixel and sleep precision.

**LYNX's extra steps (real window, 2026-11-07 10:39):** ten clicks gave -2.4 dB. Four more single clicks about 3 s apart
went -2.6, -2.8, -3.1 and -3.4 dB, so some isolated 50 ms clicks took two steps. The release check is not the cause:
JUCE reads the button through GetAsyncKeyState. The cause is a JUCE timer whose first callback can come before its
period. 177a-10 counts the steps from the press time (`faderRailStepsForHold`), polling every 16 ms, so an early
callback can't add a step. The earlier 300 ms first delay (177a-6, INFERRED) is replaced by FL's measured timing.

LYNX (slice 177a-4) now does the same by default, as the coordinator decided: a stray click can't jump a mix.
`faderRailStep = 0.0056` and `faderRailRepeatMs = 64` are in `Song.h`, and Ctrl+click still resets. The VCA faders
behave the same. In the real LYNX window one click gave -0.2 dB, as FL. Ten quick clicks first gave -2.1 dB against
FL's -1.6 dB: the repeat timer fired before a late mouse-up reached the message thread. Commit c4229c2b9 stops the
repeat once no button is down. That fix is covered by the offscreen check (`mixer_basics`), but it has not been
clicked again in a real window yet.

### 4.3 The mixer track menu against FL's

Added in 177a-4: "Change color..." opens the colour picker at once (mixer only, with a Reset), "Open audio editor and
record" (Shift+E), "Track routing" as a submenu name, and "Compensate for master latency" in the main menu.

The label stays "Rename and color..." where FL says "Rename, color and icon...". The LYNX dialog sets the name and the
colour only. The icon is the separate "Change icon..." item, so FL's text would promise something the dialog doesn't do.

Still open, each as its own slice: FL's "Gradient color" and "Assign to new Audio track(s)" / "Unassign from Audio
track(s)".

## 5. Gradient color (slice 177a-6) [FLEngine_x64.dll, static]

FL's mixer handler is `SetGradientColorMenuClick` (code 0x12a3110; its undo text is "name/color/icon mixer track(s)").
It reads:

1. It walks the selected inserts in mixer order. The first one's colour is the start and the last one's is the end.
2. With fewer than two selected it calls 0x128f9f0 instead, so the menu item is greyed with one track (seen in the
   menu shot).
3. It calls 0x12e2080 (start, end), which returns false on cancel. This is a dialog that takes the two colours. What it
   looks like is still to be shot in FL before LYNX builds any UI for it.
4. One undo step. Then track k of n gets `blend (start, end, k * 256 / (n - 1))`. The blend is 0x660c40, MMX:
   `c = a + ((b - a) * 16 * amount * 16) >> 16` on R, G and B, so it is a floor. FL also stores a darker shade
   (0x660a00 with -150) for the strip itself.

**FL's dialog [SHOT 2026-11-07 13:16, pairs/fl-gradient-editor.png]:** with several tracks selected the menu title is
"Mixer tracks (18 selected)" and "Gradient color..." is enabled. It opens a "Gradient editor" window:

- On the left, "Gradient colors": a "First" swatch (ticked while it is the one being edited), a wide preview of the blend
  and a "Last" swatch. Under them are three buttons: copy First into Last (→), swap (↔) and copy Last into First (←).
- On the right, FL's full colour selector: a wheel and a palette view, presets 0–9, the named colour (e.g. "Granite Gray
  #636C71"), Hue / Saturation / Luminance and Red / Green / Blue.
- At the bottom, "Reset all" and "Accept". Escape doesn't close it; its X cancels.

**Reset all [FLEngine_x64.dll, static]:** `ResetAllBtnClick` (0x7b60c0) calls 0x7b5dd0 (self, i) for every swatch,
then refreshes. 0x7b5dd0 reads swatch i's line from the list the editor saved when it opened (+0x9a4), takes the text
after "=", prefixes "$" and parses it as a hex colour (StrToIntDef, default 0x808080). So Reset all puts First and Last
back to the first and last selected tracks' colours, as they were when the editor opened. `ResetBtnClick` (0x7b6120),
the "Reset" half of the split button, does the same for the swatch being edited. LYNX has "Reset all", which keeps the
editor open.

LYNX: `mixerops::gradientColourStep` and `mixerops::gradientColour` follow those steps exactly. The test checks the ends,
amount 85 (0xaa / 0x54) and 128 (0x7f). The menu item waits for FL's dialog shot.

## 6. Assign to new / Unassign from Audio track(s) (slice 177a-7) [FLEngine_x64.dll, static]

Both items are one handler, `AssignAudiotrackMenuClick` (0x12a2a90), split on the item's Tag.

- **Assign** (Tag 0): for each selected insert (not the Master), FL takes the first playlist track 1–500 that is
  untouched and holds no clip. It checks 0x12ef3d0 (no name, default colour, two more fields 0) and 0x12ef2a0 (no clip
  on it). That track gets mode audio and the insert, and its name and colour are set from the insert (0x12be330,
  0x12e60f0). Then it stops for that insert.
- **Unassign** (Tag 1): for each selected insert, the first track with mode audio and that insert gets mode 0 and
  insert -1. Name and colour stay.
- There is one undo step, and the Playlist refreshes.

FL skips an insert in two cases:

- 0x12a76e0 (t, false) is true when any Playlist track's insert field (+0xdc) is t, in any mode.
- 0x12a7620 (t, true) >= 0 when a channel routed to t (+0x258) is on an instrument track (mode 3, via 0xffac90 /
  0xffaee0) and is the only channel on that insert (0xffae00 with false). Channel types 1, 4 (audio clip) and 5 are
  excluded by the mask 0b110010.

LYNX's `assignNewAudioTracks` follows both (`hasPlaylistTrack`); the test is `mixer177a_d_...`.

The menu items are enabled as in FL's shot: Assign when a selected insert has no audio track, Unassign when one has.
