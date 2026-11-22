# The LYNX plugin pack: LYNX's own effects and instruments in other DAWs

LYNX SHARE's own engines also come as plugins for other hosts (Ableton Live, Bitwig, REAPER, FL Studio, Ardour, Studio
One...). This is a separate, optional download next to each release: `LYNX-Plugins-<version>-<system>`. LYNX SHARE
itself doesn't need it. How it is built and tested: [PLUGIN_EXPORT.md](PLUGIN_EXPORT.md).

## What's in it

Only LYNX's own clean-room engines. FL Studio's native plugins that LYNX plays on its own engines (Fruity Limiter,
Gross Beat, Sytrus...) are not in the pack: their names belong to Image-Line, and inside LYNX they keep those names. The
plugins use LYNX's own product names, which all start with "LYNX".

| Plugin | Kind | Formats |
|---|---|---|
| LYNX Facet EQ | 8-band EQ | CLAP, VST3 |
| LYNX Apex Limiter | true-peak limiter | CLAP, VST3 |
| LYNX Crest Compressor | compressor | CLAP, VST3 |
| LYNX Atrium | room reverb | CLAP, VST3 |
| LYNX Echoform | multi-mode delay | CLAP, VST3 |
| LYNX Tune | vocal pitch correction | CLAP, VST3 |
| LYNX FM10 | FM instrument | CLAP, VST3 |
| LYNX Model | physical-model instrument (marimba, vibraphone, xylophone, glockenspiel, tubular bells, kalimba) | CLAP, VST3 |
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
| LYNX Fourfold | 4-operator FM synth | CLAP |
| LYNX Wavecoil | wavetable synth | CLAP |
| LYNX Duoloom | two-engine macro synth | CLAP |

The CLAP holds every plugin in one file; each VST3 carries the whole LYNX engine, about 30 MB, so only a short list comes
as VST3. Audio Units (macOS) are not built yet: Logic users can load the
CLAP through a CLAP-to-AU host, or wait for a native AU.

New in 0.1.31 (round 190ov): Strike Shaper, Anvil Bus, Corrode, Soundboard, Platter, Timeprism and Chimefield in the CLAP,
LYNX Model as CLAP and VST3. LYNX's synths Tetra, Helix and Braid come as LYNX Fourfold, LYNX Wavecoil and LYNX Duoloom
(CLAP), and the Harmonia engine as LYNX Chimefield: other makers sell products of the engines' names. Inside LYNX the
engines keep their names. LV2 bundles for Linux are built behind an option (`LYNX_PLUGINS_LV2`) but not shipped yet:
they still fail the host test (planned for 0.1.32).

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
| Linux | `sh install.sh` | `~/.clap`, `~/.vst3` |

Then rescan the plugins in your host. `--uninstall` (Windows: `/uninstall`) removes them again. Each archive's
SHA-256 is in `LYNX-Plugins-<version>-SHA256SUMS.txt`.

## Projects and presets

- A plugin's state is a one-slot LYNX project. In LYNX, *File › Open* opens it, and the other way round.
- The plugin opens the same editor as inside LYNX.
- Plugin ids never change once released, so projects in other hosts keep working after an update.

## Licence

The plugins are LYNX SHARE's code, under the AGPL-3.0. The source is at https://github.com/Ka1-S/LYNX-SHARE.
