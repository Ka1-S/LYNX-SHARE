# LYNX's own plugins as VST3 and CLAP (round 180b)

Status: **prototype, off by default, nothing shipped.** The normal build doesn't change. Packaging is up to the
user (see "Rollout").

## What is exported

Only engines that are LYNX's own clean-room code and need no FL Studio data file. Inside LYNX the engines keep their
names. Outside LYNX they use the names below, which live in **one table**, `src/plugin/plugins.def`. A rename is one edit
there.

| Exported name | Engine inside LYNX | Kind | Built now |
|---|---|---|---|
| LYNX Facet EQ | Facet EQ | effect | yes |
| LYNX FM10 | DX10-style FM | instrument | yes |
| LYNX Osc Trio | 3x Osc | instrument | no (needs a host parameter table) |
| LYNX SimOsc | SimSynth | instrument | no |
| LYNX Harmlite | Harmless | instrument | no |
| LYNX Plucker | Plucked! | instrument | no |
| LYNX Wave Wanderer | Wave Traveller | instrument | no (needs a host parameter table) |

The names were checked by a **web search only, with no trademark registry check**. None of them contains "Fruity",
uses an exact product name or a logo, or says it is the FL plugin.

The other LYNX-named effects can follow with one table line each: Apex Limiter, Ridge Strip, Glow Leveler, Hush
De-Esser, Aether, Ember, LYNX Tune, Epoch Verb, Atrium, Foil Plate, Nebula, Halo, Horizon, Echoform, Spiral Echo, Orbit,
Lattice Mod, Crest Compressor, Prism Multiband, Sentry Gate, Quell Denoiser, Gemini Doubler, Voice Shift and Kiln
Channel. The test `pluginexport180b_catalog_needs_no_fl_data` fails if a table entry needs FL data.

## Licences

- LYNX's code is AGPL-3.0, and the plugins are the same code.
- The VST3 SDK in JUCE 8.0.4 is dual-licensed (Steinberg proprietary or GPLv3). We use it under GPLv3, which is
  compatible with AGPL-3.0 (section 13 of each).
- JUCE 8 is AGPLv3 or commercial. We use it under AGPLv3, as the app does.
- The CLAP headers (`third_party/clap`) are MIT.
- A binary release has to carry the source offer, the same as the app's.

## Design: one runtime, thin wrappers, no copied code

```
src/plugin/plugins.def      the table (names, ids, VST3 codes, categories)
src/plugin/ExportCatalog.h  the table as constexpr C++
src/plugin/ExportProcessor  juce::AudioProcessor + editor over the app's own engine
src/plugin/Vst3Entry.cpp    JUCE's VST3 wrapper, one binary per table entry (identity from -D)
src/plugin/ClapEntry.cpp    own CLAP bridge: one .clap holds every entry
```

- **Audio.** The app's own `RackRenderer` runs a one-slot Song for an effect, or a one-channel Song for an instrument.
  - Effect input goes into Insert 1 through `setMetronomeInput`.
  - Host parameter values reach the slot through `RackRenderer::setSlotParamsOverride`, with no snapshot rebuild per
    block.
  - Instrument MIDI becomes `NoteEvent`s in a `LiveEventQueue`, and the block is cut at each event.
  - The result matches the effect alone to the sample. This is tested by `pluginexport180b_rack_path_equals_the_effect_alone`.
- **Editor.** The plugin opens the same editor as LYNX: Facet EQ's editor, or the channel settings plate for synths.
  - The editor gets a private `AudioEngine(false, 0)`, with no audio device and no workers, that exists only while
    the editor is open.
  - `setExternalRuntime` feeds its meters and analyser from the plugin's own processing.
  - A 30 Hz timer syncs the editor's Song and the plugin's state both ways. Changes made in the editor go to the host
    as gestures.
  - `plugui::hostedElsewhere` hides controls that make no sense outside LYNX: the slot and track names, and Facet's
    sidechain detector and target submenu. There is no host sidechain bus yet, so no control is left dead.
  - Plates show the exported name, never the FL name.
- **State.** The plugin saves its one-slot Song as `.lynx` JSON (`ProjectFile::toJson`). A plugin's state can be
  opened as a LYNX project and the other way round. A state that isn't a valid LYNX plugin state is refused.
- **Parameters.** Every named engine parameter is a host parameter (`p<index>`).
  - Stepped parameters snap to their steps.
  - Value text round-trips through the same text the editor shows.
  - The parameter ids never change once released, because hosts store them.

## Building

Linux, macOS, or Windows with MSVC:

```
cmake -B build-plug -DLYNX_BUILD_PLUGINS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-plug --target lynx_vst3_0 lynx_vst3_1 lynx_clap plugexporttest
```

Only with the option on, the build also:

- turns on position-independent code;
- adds `-ffunction-sections -fdata-sections`;
- links each plugin with `--gc-sections`, hidden visibility and `-z defs`.

Windows from WSL (zig): `LYNX_BUILD_PLUGINS=1 scripts/build-windows-zig.sh`. This reuses the app's objects except
`Main.cpp`, then links the `.clap`, one `.vst3` per built table line, and `plugexporttest.exe`.

The Windows DLLs carry a Common Controls v6 manifest (`resources/LynxPlugin.rc`). JUCE imports `TaskDialogIndirect`, so
without the manifest the DLL fails to load with error 127.

## Testing

- `tests` (normal build): `pluginexport180b_*`, three tests.
- `plugexporttest <plugin>` loads the binary with JUCE's VST3 host or LYNX's CLAP host and checks:
  - the scan;
  - a null test against the effect or instrument alone, at defaults and with every parameter moved;
  - automation;
  - a state round trip into a second instance;
  - that the state opens as a `.lynx` project;
  - the hosted and in-process editors, with PNG pictures;
  - memory per instance and CPU.
- pluginval at strictness 10, and clap-validator.
- CI: `.github/workflows/plugins.yml` runs only when started by hand (`workflow_dispatch`). It builds the plugins with
  zig on Ubuntu and uploads them as the unsigned artefact `LYNX-plugins-windows-x64-unsigned`. It then runs
  plugexporttest, pluginval and clap-validator on windows-2022. Normal pushes don't build plugins.

## Rollout

Sizes measured on Windows x64, zig, `-O2`, stripped:

| Binary | Size | Compressed |
|---|---|---|
| each `.vst3` (one plugin) | 39.2 MB | 18.1 MB gzip |
| `LYNX Plugins.clap` (both plugins) | 39.1 MB | 13.8 MB xz |
| `LYNX.exe` (for comparison) | 41.5 MB | |

Linux x64 (gcc 15, `-O3`): each VST3 `.so` is 37.2 MB and the `.clap` is 37.0 MB, unstripped. Measured on the `.clap`
with the same objects, all compiled with `-ffunction-sections -fdata-sections`:

| Linking | Stripped size | xz |
|---|---|---|
| with `--gc-sections` | 31.4 MB | 10.4 MB |
| without `--gc-sections` | 46.3 MB | 16.9 MB |

Garbage collection of unused sections makes the plugin 32 % smaller, so it stays on. The Windows plugins reuse the app's
objects, which are built without section flags. **Follow-up after 0.1.25:** build the Windows plugins from their own
objects, compiled with section flags. A similar saving is likely (inferred, not measured), at the cost of a second
compile of the app.

Almost all of a plugin's size is the shared app runtime. A CLAP with N plugins stays at about 39 MB. N separate VST3s
cost about 39 MB each. The options for the user to decide:

- **A.** One VST3 per plugin. This is the simplest, but at 25 effects plus 6 synths it comes to about 1.2 GB.
- **B.** A JUCE patch for several classes in one VST3 bundle, or a shared runtime DLL next to thin VST3s. This keeps
  everything at about 40 MB, but it means patching and maintaining JUCE's wrapper.
- **C.** Ship the CLAP with every plugin, plus VST3s for a short list only.

Delivery could be a separate download, or an optional part of the installer. Until the user decides, nothing ships and
the option stays off.

Per instance, measured in plugexporttest:

- memory: 1.2 to 2.2 MB more per extra instance;
- FM10 as VST3: 7.1 MB. This is inferred to come from JUCE's MIDI CC parameter mapping, which gives 2,103 host
  parameters against 22 in the CLAP;
- an open editor: 22 to 25 MB;
- CPU: 0.15 to 0.32 % of real time.

## Installing (for testers)

- **VST3:** put the `.vst3` bundles in `C:\Program Files\Common Files\VST3` on Windows, or `~/.vst3` on Linux. FL
  Studio scans custom search paths only for VST 1 and 2 plugins. Its manual says VST3 plugins "must be installed to
  default system locations" (Options > File settings, "Plugin search paths").
- **CLAP:** put `LYNX Plugins.clap` in `C:\Program Files\Common Files\CLAP` on Windows, or `~/.clap` on Linux. Another
  folder works only if the host reads the `CLAP_PATH` environment variable.
  - The CLAP standard (`clap/entry.h`) says a host must read `CLAP_PATH`, a list of folders.
  - FL Studio's Plugin Manager contains the string `CLAP_PATH` (observed in `PluginManager.exe`, next to its CLAP
    loading code). Image-Line staff say on the forum that FL reads it. This has not been tested here.

## Known gaps

- There is no host sidechain bus, so Facet's sidechain controls are hidden in the plugins.
- Ableton Live has not been tested, at the user's request.
- Not yet tested in FL Studio. The plugins are validated with pluginval, clap-validator and the LYNX host on Windows
  and Linux. FL's quick scan, with verification off, found neither plugin in a custom search path.
- Editors other than Facet EQ and FM10 need to be checked for FL wording before they are exported.
- The plugins are unsigned.
- macOS is not built or tested. The app's macOS-only `VideoMediaMac.mm` is not in the shared source list.
