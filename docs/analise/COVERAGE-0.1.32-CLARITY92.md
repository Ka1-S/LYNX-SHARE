# Item 92: "Playlist track 1 is not Mixer track 1" (round 190rq)

Users' words (ytg. / yannik, Discord #learning, 2026-11-26): routing is confusing because Playlist track N and Mixer
track N look like the same thing and aren't. The owner's line: "simple by default, powerful when needed". 0.1.32 rule:
no new routing model. Only labels, hints and tutorials that SHOW the link that already exists.

## How FL Studio 2026 shows the link (observed today, FL 26.1.7, shots in `shots/ref/` and `shots/pairs/92*`)

FL's model: a **channel** (Channel rack) plays into a **mixer insert** chosen by the number box left of the channel
button. A Playlist track is only a lane for clips: a pattern clip's notes play through each channel's insert, wherever
the clip sits. FL makes the link visible in five places:

| Where | What FL shows | Shot |
|---|---|---|
| Channel rack, left of each channel button | the target insert's **number** (`10`, `1`, `2` ...; `---` = Master in a new project) | `pairs/92a` left |
| Hint bar over that box | "Target mixer track" + "(double-click)" on the right; double-click shows the insert in the Mixer | `pairs/92a` right |
| Channel menu › **Assign to new instrument track** | makes a Playlist track named after the channel (an instrument icon on the header and the channel button), gives the channel a free insert (the box then reads `1`); hint over the header: "Sampler (Instrument track)" | `pairs/92b` |
| Playlist track menu › **Track mode** › Unassigned / Audio track › / Instrument track › | Audio track › lists "New mixer track(s)" then every insert as "N. Name"; Instrument track › "Use existing channel ›", "More plugins...", the plugin categories | `ref/fl-f08.png`, `ref/fl-f09.png` |
| Mixer insert header | the insert **number** top-left, its name, and a **waveform icon** when a Playlist audio track is linked to it; the insert menu has "Assign to new Audio track(s)" / "Unassign from Audio track(s)" and Channel routing › "Route selected channels to this track (Ctrl+L)" | `pairs/16`, `ref/fl-f16.png`, `ref/fl-f17.png` |

FL never says "Playlist track N is not insert N" in the UI. Its tutorial (Help › Tutorials › Step sequencer tutorial,
step 7/9) has the user drag Pattern 1 "on Track 1's header" without saying where its sound goes.

## How Live 12.4 shows it (observed today, Live 12.4.16)

Live's model: one track = one mixer channel. Nothing to link, so nothing to explain:
- the track's **number** is on its activator button at the bottom of the strip (`1`, `2` ...);
- the **In/Out** section shows "MIDI From / Audio From", "Monitor In Auto Off", "Audio To: Main" on every track, in the
  Session mixer and inline in each Arrangement track header (`pairs/92c`, `pairs/92d`);
- the **Info View** explains each control on hover: the activator ("…to set the destination use the Output Type and
  Output Channel choosers in the In/Out section"), "Output Type", "Monitoring" (`pairs/92c` right, `92d` right).

## What LYNX r032 already shows (code on origin/wip/rollup032)

| Where | LYNX | vs FL |
|---|---|---|
| Channel rack insert box | number, digit wheel (`ChannelRackComponent.cpp:2058-2070`), value text "Master" / "Insert N" | same |
| Its hint | "<channel> - Target mixer track", value "Insert N", gestures "[LMB] drag or [Wheel] routes [2xLMB] show in the Mixer [RMB] next free insert" (`:3771`) | richer than FL; **no insert name** |
| Channel menu | "Assign to new instrument track" (`:4142`) | same |
| Playlist header hint | "(Instrument track)" / "(Audio track, linked to mixer track N)" (`PlaylistComponent.cpp:15607-15611`) | richer than FL |
| Playlist header hint, **Unassigned** track (most tracks in an FL project) | the track name only | same as FL, and that is the confusing case |
| Track mode submenu | `PlaylistComponent.cpp:10816-10876` | see `pairs/02` |
| Mixer insert header | number, waveform icon on audio-track inserts (`pairs/16`) | same |
| Mixer insert hint | name + gestures + "Channels: Kick, Snare, …" (`MixerComponent.cpp:3527-3537`, `:3599`, `:3610`) | richer than FL (FL shows the name only) |
| Opt-in LYNX extra | **"Link Playlist tracks to mixer tracks"** (Track N = insert N; `core/PlaylistOps.h:325-345`, Settings › Project `SettingsDialog.cpp:112/122`, Playlist menu `PlaylistComponent.cpp:9640`). Off by default; turning it on re-routes channels (undoable, sound unchanged) | LYNX's own; exactly what these users expect, but hard to find |
| Clip-launch layout | each track = its own channel + insert (`sessionmixer::stripOf`), Live's model | same as Live |

## Proposals for 0.1.32 (labels / hints / Learn only; no routing change)

Each is a LYNX extra that only helps (visual / text, no FL gesture changed, no file change), so per the owner's
2026-11-23 rule it may ship ON with a switch in Settings › LYNX extras ("Routing hints"), FL-exact turns it off.

1. **C92-1 Unassigned Playlist header hint says where the sound goes** (S). Value text after the name, from the clips on
   the track: one channel → "plays Kick → insert 1 (KICK)"; several → "plays 4 channels → inserts 1, 2, 5"; audio clips on
   the Master → "plays → Master"; no clips → "(a lane for clips: they play through their channels' mixer inserts)".
   Files: `src/ui/PlaylistComponent.cpp` (header hint ~15595-15620), a helper in `src/core/PlaylistOps.cpp`
   (`trackmodes::describe` neighbour), strings × 14 tables. Verify: a `--…shot` route of the hint on Thief's
   "Synth Bass" (unassigned) track + unit test of the helper; FL-exact off → FL's bare name.
2. **C92-2 Insert box hint names the insert** (S). "Insert 10 · Synth Bass" instead of "Insert 10" (also in the drag
   read-out). Files: `ChannelRackComponent.cpp:2063` (`toText`), `ChannelSettings.cpp:6824` (same box in the channel
   settings). Verify: hint shot.
3. **C92-3 The Mixer's first-open tip says it** (S): the existing window tip `Tip::mixerRouting` ("Tip: sending one
   insert into another", `core/Onboarding.h:337/369/386`) only covers insert-to-insert sends. Add one sentence before
   it: "Each channel plays into the insert whose number is in its box in the Channel rack; Playlist tracks only hold
   clips (Playlist track 1 is not insert 1). Want them paired? Playlist menu › Link Playlist tracks to mixer tracks."
   No new tip, no new state; respects Help › Show tips and hints (same as T-4 of TUTORIALS.md). Verify: the tip's
   route shot, the Onboarding tests.
4. **C92-4 Learn pages** (S): `website/learn/mixer-routing` gets a top section "Playlist track ≠ mixer insert" with the
   three places to look (insert box, header hint, insert hint) and the switch; `pattern-tour` and `first-song-tour`
   link to it; pt-BR twins. Verify: site build `--check`, links check.
5. **C92-5 Make the switch findable** (S): the Playlist track menu › Track mode › gets "Link all tracks to mixer tracks
   (LYNX)…" at the end (it opens the same switch with its one-line explanation); Search actions finds it under "mixer",
   "route", "link". Files: `PlaylistComponent.cpp` ~10876, the action list. Verify: menu shot, search test.
6. **(0.1.33, not now) C92-6** an insert badge on unassigned headers ("→ 1") when all the track's clips go to one insert:
   a visual extra, but it changes the header's look in the Pattern layout and needs FL | LYNX shot review first.

Not proposed: any default change of the routing (FL projects must open as in FL), renaming FL's "Track N".
