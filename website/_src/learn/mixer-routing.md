---
title: Route tracks in the Mixer
summary: Send a Mixer track to another one with the route arrows, set how much goes there with the send knob, and build a reverb bus that every track can share.
---
## Playlist tracks and Mixer tracks

In the Pattern layout a Playlist track is not a Mixer track: Playlist track 1 is not Mixer track 1 (insert 1).

- Playlist tracks only hold clips. A clip plays through the Mixer tracks of its channels, wherever it sits.
- Each channel of the Channel rack plays into the Mixer track whose number is in its route box, between the knobs and its name. Drag the box to change it; `Ctrl+L` gives the selected channels free Mixer tracks.
- Three places tell you where a sound goes: the route box in the Channel rack, the hint when you point at a Playlist track's header, and the hint of a Mixer track, which lists the channels that play into it.
- Want them paired? `Link Playlist tracks to mixer tracks` in the Playlist menu links track 1 to Mixer track 1, track 2 to Mixer track 2 and so on, for the current project.

## Select a track

![The Mixer with the Chords selected](/assets/img/learn/mixer-routing-1.webp)

1. Open the Mixer with `F9`. Every Mixer track goes to the Master.
2. Click the track you want to send, here the Chords.

## Route and send level

![A route arrow and its send knob](/assets/img/learn/mixer-routing-2.webp)

1. Click the arrow under another track: the selected track now goes there as well, and a cable shows the route.
2. The knob under the arrow is the send level: how much of the track goes there.

## A reverb bus

![A reverb on the bus track](/assets/img/learn/mixer-routing-3.webp)

1. Add a reverb to that track's first slot: every track you route there shares it, and keeps its own fader.
2. Right-click an arrow for more: `Route to this track only` moves the whole track, `Sidechain to this track` sends a signal for a compressor to listen to.

See also [Mixer](/wiki/mixer/), [Mixing basics](/learn/mixing-basics/) and [Sidechain](/learn/sidechain/).
