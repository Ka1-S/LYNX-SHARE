# The LYNX plugin pack: LYNX's own effects and instruments in other DAWs

LYNX SHARE's own engines also come as plugins for other hosts (Ableton Live, Bitwig, REAPER, FL Studio, Ardour, Studio
One...). This is a separate, optional download next to each release: `LYNX-Plugins-<version>-<system>`. LYNX SHARE
itself doesn't need it. How it is built and tested: [PLUGIN_EXPORT.md](PLUGIN_EXPORT.md).

On Windows the pack comes twice: `LYNX-Plugins-<version>-win-x64.zip` (about 195 MB: a zip packs every file on its own, and
the 12 plugin files share nearly all their bytes) and `LYNX-Plugins-<version>-win-x64.7z` (about 20 MB, the same files; Windows 11
opens it in Explorer, older Windows needs 7-Zip). Take the `.7z` if you can.

## What's in it

Only LYNX's own clean-room engines. FL Studio's native plugins that LYNX plays on its own engines (Fruity Limiter,
Gross Beat, Sytrus...) are not in the pack: their names belong to Image-Line, and inside LYNX they keep those names. The
plugins use LYNX's own product names, which all start with "LYNX".

| Plugin | Kind | Formats |
|---|---|---|
| LYNX Facet EQ | 8-band EQ | CLAP, VST3, LV2 (Linux) |
| LYNX Apex Limiter | true-peak limiter | CLAP, VST3, LV2 (Linux) |
| LYNX Crest Compressor | compressor | CLAP, VST3, LV2 (Linux) |
| LYNX Atrium | room reverb | CLAP, VST3, LV2 (Linux) |
| LYNX Echoform | multi-mode delay | CLAP, VST3, LV2 (Linux) |
| LYNX Tune | vocal pitch correction | CLAP, VST3, LV2 (Linux) |
| LYNX FM10 | FM instrument | CLAP, VST3, LV2 (Linux) |
| LYNX Model | physical-model instrument (marimba, vibraphone, xylophone, glockenspiel, tubular bells, kalimba) | CLAP, VST3, LV2 (Linux) |
| LYNX Fourfold | 4-operator FM synth (LYNX's Tetra engine) | CLAP, VST3, LV2 (Linux) |
| LYNX Wavecoil | wavetable synth (LYNX's Helix engine) | CLAP, VST3, LV2 (Linux) |
| LYNX Duoloom | two-engine macro synth (LYNX's Braid engine) | CLAP, VST3, LV2 (Linux) |
| LYNX Prism Multiband, Sentry Gate, Glow Leveler, Hush De-esser | dynamics | CLAP |
| LYNX Ridge Strip, Kiln Channel, Fret Channel, Shell Channel | channel strips | CLAP |
| LYNX Aether, Ember Saturator | air band / exciter, saturation | CLAP |
| LYNX Epoch Verb, Foil Plate, Nebula, Halo, Horizon | reverbs | CLAP |
| LYNX Spiral Echo, Lattice Mod, Orbit | delays and modulation | CLAP |
| LYNX Voice Shift, Gemini Doubler, Quell Denoiser | voice tools | CLAP |
| LYNX Strike Shaper | transient designer | CLAP |
| LYNX Anvil Bus | drum-bus processor | CLAP |
| LYNX Corrode | degrader (modulated short delay) | CLAP |
| LYNX Soundboard | resonant body | CLAP |
| LYNX Platter | record-wear distortion and crackle | CLAP |
| LYNX Timeprism | spectral freezer and delay | CLAP |
| LYNX Chimefield | spectral resonator | CLAP |

The CLAP holds every plugin in one file; each VST3 carries the whole LYNX engine, about 30 MB, so only a short list comes
as VST3. On Linux the same short list also comes as LV2 bundles (since 0.1.31). Audio Units (macOS) are not built yet: Logic users can load the
CLAP through a CLAP-to-AU host, or wait for a native AU.

New in 0.1.31 (round 190ov): Strike Shaper, Anvil Bus, Corrode, Soundboard, Platter, Timeprism and Chimefield in the CLAP,
LYNX Model as CLAP and VST3. The Harmonia engine exports as LYNX Chimefield (Cherry Audio sells a Harmonia synth); inside
LYNX it keeps its name. New in 0.1.31: the VST3 list also as LV2 on Linux.

New in 0.1.31 (round 190pq, rollup step 8): LYNX Fourfold, LYNX Wavecoil and LYNX Duoloom, LYNX's synths Tetra, Helix and Braid, as
CLAP, VST3 and LV2 (Linux). Their engine names are other makers' products, so they export under these names; inside LYNX
the engines keep their names. Fourfold and Duoloom start from the engine's defaults (they have no preset bank), Wavecoil
from its first preset.

### Not in the pack yet

| Engine in LYNX | Why |
|---|---|
| LYNX Soundfont | it needs a SoundFont file chosen in LYNX's browser |
| Visualizer, analyser, controllers, hardware insert | no sound of their own, or they need LYNX's mixer |
| FL Studio's native plugins on LYNX engines | the names are Image-Line's |

Names were checked with a web search only, not in a trademark registry.

## Install

Unpack the archive and run the install script in it:

| System | How | Folders |
|---|---|---|
| Windows | right-click `install.cmd` > Run as administrator | `C:\Program Files\Common Files\CLAP`, `...\VST3` |
| macOS | `sh install.sh` in Terminal | `~/Library/Audio/Plug-Ins/CLAP`, `.../VST3` |
| Linux | `sh install.sh` | `~/.clap`, `~/.vst3`, `~/.lv2` |

Then rescan the plugins in your host. `--uninstall` (Windows: `/uninstall`) removes them again. Each archive's
SHA-256 is in `LYNX-Plugins-<version>-SHA256SUMS.txt`.

## Projects and presets

- A plugin's state is a one-slot LYNX project. In LYNX, *File › Open* opens it, and the other way round.
- The plugin opens the same editor as inside LYNX.
- Plugin ids never change once released, so projects in other hosts keep working after an update.

## Licence

The plugins are LYNX SHARE's code, under the AGPL-3.0. The source is at https://github.com/Ka1-S/LYNX-SHARE.
