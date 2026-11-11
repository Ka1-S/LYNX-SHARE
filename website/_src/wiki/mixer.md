---
title: Mixer
summary: Inserts with faders, effect slots, sends and sidechain routing.
status: full
---
![The Mixer](/assets/img/mixer.webp)

Every channel plays through a Mixer insert. Each insert has a fader, pan, mute and solo, a peak meter, ten effect slots with a dry/wet amount each, and a built-in EQ. All inserts end up in the Master unless you route them elsewhere. Press `F9` to show or hide the Mixer.

A project can have up to 127 inserts. The Mixer has an add strip for a new one.

## Send a channel to an insert

In the [Channel rack](/wiki/channel-rack/), drag the channel's route box (or use the wheel on it) to pick the insert number. Double-clicking the box shows that insert in the Mixer.

To give several channels their own inserts in one go, select them in the Channel rack and press `Ctrl+L`. Each goes to a free insert, which takes the channel's name and colour.

With the Mixer focused, `Ctrl+L` links the selected channels to the selected insert, and `Ctrl+Shift+L` links them to consecutive inserts starting from it.

## Select and move inserts

- Click a strip to select it.
- `Ctrl`+click, or drag across strips, selects a range. `Ctrl+Shift`+click adds or removes one.
- Drag a strip sideways, or press `Alt+Left` and `Alt+Right`, to move the selected inserts.
- Right-click a strip for the insert menu. `F2` renames and colours the selected insert.

Moving, inserting or deleting inserts keeps sends, sidechains and automation on their insert.

## Mute and solo

Click the mute light to mute an insert. Right-click or `Ctrl`+click it to solo. `S` solos the selected inserts, and `Alt+S` solos them together with the inserts that feed them.

## Effects

The effects panel shows the slots of the selected insert.

1. Click an empty slot and pick an effect.
2. Click the slot's name to show or hide the effect's window.
3. Use the slot's light to bypass it.

Drag a slot, or use the wheel over it, to move it up or down. Right-click a slot for its options (replace, move, delete). `Del` deletes the plugin in the selected slot. The switch in the panel's header turns all effects of the insert on or off.

You can also drag an effect from the Browser's Plugin database onto an insert or an effect slot.

## Insert EQ

Each insert has its own EQ. Drag a band to set its frequency and level, use the wheel for the width, and double-click a band to put it back to 0 dB. Right-click the EQ's title to bypass it. If you want a Mixer with no built-in EQ, new projects can be set to start with a clean mixer.

## Routing, sends and sidechain

Select the insert you want to route. Every other strip then has a route switch:

- Click the switch on another strip to send the selected insert there. Dragging after enabling sets the send level.
- `Ctrl`+click routes to that insert only.
- `Shift`+click makes a sidechain send. `Ctrl+Shift`+click makes it sidechain only.
- Right-click opens the same choices as a menu.

Effects with a side input, such as a compressor, have a `Sidechain from` picker that lists the inserts to listen to. Picking one creates the route for you.

## Groups and VCA faders

Mixer groups fold with their chevron (or `Alt`+click for all groups). A VCA fader moves the level of its member inserts while they keep their own faders. Create one from the insert menu: under Group, choose `VCA › New VCA from selected`.

## Recording

Each insert has a record-arm button. With an insert armed, Rec and Play record its audio input into a Playlist clip. `Shift+R` arms the selected insert and starts recording in one step.

## Known issue

From the 0.1.25 release notes: `Render to audio` (an insert bounced to an audio clip) plays about 5 dB louder than the insert it replaces.
