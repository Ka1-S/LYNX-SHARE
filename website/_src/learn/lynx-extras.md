---
title: LYNX extras: FL-exact or Recommended
summary: The LYNX extras are LYNX's own improvements on top of how FL Studio works. Switch them one by one, or all at once with FL-exact and Recommended.
---
## What the extras are

![The LYNX extras page, grouped by area](/assets/img/learn/lynx-extras-1.webp)

1. Open `Options › LYNX extras...`: one switch per extra, grouped by area (Mixer, Playlist, Piano roll, editing and more).
2. Each switch says what it does and where else you find it (often a window's own menu too).

## Switch one on

![The loudness meter beside the Master](/assets/img/learn/lynx-extras-2.webp)

1. Press `Ctrl+F` and type part of its name, here "loudness", then click its switch.
2. The change is immediate: the Mixer now shows the loudness of the song beside the Master.

## FL-exact or Recommended

![FL-exact and Recommended at the top of the page](/assets/img/learn/lynx-extras-3.webp)

1. `FL-exact` turns every extra off, so LYNX works exactly like FL Studio.
2. `Recommended` puts each extra back at its default. Most of the ones on by default change how things look or add something FL Studio doesn't have, and none changes what FL Studio's keys and clicks do. Four of them change the sound or your files, and their lines say so.

- `Stretched pattern clips loop their pattern (LYNX)` plays the pattern again where FL Studio is silent after it.
- `Dropped loops take the tempo and key found in their audio`, in the Simple layout, changes how a dropped loop plays.
- `MIDI files dropped on the Playlist become pattern clips` adds a pattern to the song when you drop a file (FL Studio refuses the drop).
- `One row per plugin installed in several formats` picks the format a new plugin loads in, and the project keeps that format.

## One more that changes the sound

`Compensate fader and route automations too (LYNX)`, under `Plugin delay compensation` in a mixer insert's menu, is on in every new project. It is not on the LYNX extras page, so `FL-exact` leaves it on.

1. With plugins that add latency (a linear-phase EQ, a lookahead limiter), it plays the automation of faders, pans, sends and VCA faders as late as the audio they act on, so a fade lands where you hear it.
2. FL Studio delays only the automation of plugin knobs, so there the same fader move is heard a little early. Projects opened from FL Studio, and projects made before, keep it off.
3. For FL Studio's timing, switch it off in that menu: it is one setting for the whole project.

See also [Settings](/wiki/settings/) and [A tour of Settings](/learn/settings-tour/).
