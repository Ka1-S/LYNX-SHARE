---
title: Built-in instruments and effects
summary: LYNX SHARE's own instruments and effects, how to add them, presets and stand-ins.
status: full
---
![The Quick-add palette: instruments on the left, effects on the right, each tagged built-in or built-in FL engine](/assets/img/wiki/quickadd.webp)

LYNX SHARE ships its own instruments and effects, so a project plays without any third-party plugin. There are three kinds: LYNX SHARE originals (a synth, a sampler, a SoundFont player, drums, EQs, compressors, limiters, reverbs, delays), engines written to play the settings of FL Studio's native plugins, and effects that stand in for well-known third-party plugins.

Every one of them has its own page in the [catalogue](/instruments-and-effects/). `Help › Built-in plugins (online list)...` opens the list with the status of each engine.

## Add an instrument and an effect

1. Press `F6` to show the Channel rack.
2. Click the `+ add channel` box below the last row and pick an instrument. The menu groups them by category.
3. Press `F9` to show the Mixer and select the insert the channel plays through.
4. Click an empty slot in the effects panel and pick an effect. Its window opens.
5. Click the slot's name to hide or show that window again.

## The plugin picker

Press `F8` for the plugin picker. It covers the screen and shows every generator and effect as a picture of its own editor, with the categories as tabs along the bottom.

| Command | Key | What it does |
|---|---|---|
| Search | Type | Dims the plugins that do not match |
| Step through the plugins | `Up` / `Down` | Moves the highlight |
| Add | `Enter`, double-click or right-click | A generator becomes a new channel. An effect goes into the selected Mixer insert |
| Drag a picture | | Onto the Channel rack for a new channel, onto a channel to replace its instrument, or onto a Mixer insert or slot |
| Close | `Esc` or `F8` | Closes the picker without adding anything |

The pictures of LYNX SHARE's own plugins are drawn by LYNX SHARE in the background. A third-party plugin shows a plain tile with its maker's name until its editor has been opened once.

`Edit › Quick-add palette...` opens a plain list instead: instruments on the left, effects on the right, filtered as you type. `Tab` switches lists and `Enter` adds the highlighted item. The [Browser](/wiki/browser/)'s Plugin database (`Shift+F8`) lists the same plugins as a tree.

![The Browser's Plugin database with the FL engines folder open](/assets/img/wiki/browser-plugins.webp)

## Presets

A built-in effect's window has a preset bar in its header.

| Control | What it does |
|---|---|
| Arrows | Previous and next preset |
| Preset name | Opens the menu: `LYNX SHARE presets`, `Your presets`, `FL Studio presets` where they exist, `Save preset...` and `Show the presets folder` |
| `A` / `B` | Two settings to compare. The button beside them copies the one you hear to the other |

The preset name, and the A and B settings, are saved with the project.

## What "opens FL's settings" means

FL Studio's native plugins are never loaded. LYNX SHARE has its own engine for each of them, and that engine reads the settings the FL plugin saved. This works in two places:

- A `.flp` project. Each FL generator and effect opens on its LYNX SHARE engine with the saved settings, and its controls stay editable and automatable.
- A `.fst` preset. Double-click a synth preset in the Browser to get a new channel, or drag an effect preset onto a Mixer insert. Right-click a filled effect slot for `FL presets`, which lists the presets found in your FL Studio installation.

How close each engine is to the original is stated per engine, from Exact to Placeholder. The levels are explained in [Coming from FL Studio](/wiki/coming-from-fl-studio/).

## Engines that need your FL Studio installation

LYNX SHARE contains no Image-Line code, audio or wave data. Some engines need the waves, samples, impulses or tables that belong to the original plugin, and read them from your own FL Studio installation while they run.

| Type | Engines |
|---|---|
| Instruments | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Effects | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

If the files are not found, adding such an engine can fail with a message that the data files of an FL Studio installation were not found or could not be read.

## Stand-ins for third-party plugins

When a project uses a third-party effect that is not installed, a LYNX SHARE effect with the same role takes its slot: a reverb for a reverb, a delay for a delay.

- The slot shows the original plugin's name with a LYNX SHARE badge.
- Where the saved state can be read, its settings are translated. Otherwise the stand-in starts from a default preset.
- The original plugin and its settings stay in the project. Once the plugin is installed, the project opens with it again.
- Right-click a stand-in slot and choose `Bypass all LYNX SHARE substitutes of this project` to hear the project without them.

The switch is `Substitute missing plugins with LYNX SHARE equivalents` in `Options › General settings...`, on by default. In a new project, the same effects are in the empty slot's menu under `LYNX SHARE equivalents`.

## Good to know

- Missing instruments are not substituted yet. A missing instrument plugin becomes a built-in synth chosen by the channel name.
- Automation of the original plugin's parameters is kept but does not drive the stand-in.
- FLEX packs are encrypted and are never opened. A FLEX channel gets a synth patch chosen by its preset name.
- An insert has 10 effect slots.

## Related

- [Instruments and effects catalogue](/instruments-and-effects/)
- [Third-party plugins](/wiki/plugins/)
- [Channel rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Coming from FL Studio](/wiki/coming-from-fl-studio/)
