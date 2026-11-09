# LYNX's own plugins as CLAP and VST3 (rounds 180b, 188b)

Status: **built by the release workflow as a separate, optional download** (the user's option C, 2026-11-08): every LYNX
effect in one CLAP, a short list also as VST3, for Windows, Linux and macOS. It is never part of the app's installer, and
the app's own build doesn't change (the option stays off there).

## What is exported

Only engines that are LYNX's own clean-room code and need no FL Studio data file. Inside LYNX the engines keep their
names. Outside LYNX they use the names below, which live in **one table**, `src/plugin/plugins.def`. A rename is one edit
there. Plugin names are LYNX names only, never an FL Studio name (a test checks this).

| Exported name | Kind | CLAP | VST3 |
|---|---|---|---|
| LYNX Facet EQ | EQ | yes | yes |
| LYNX Apex Limiter | limiter | yes | yes |
| LYNX Crest Compressor | compressor | yes | yes |
| LYNX Atrium | reverb | yes | yes |
| LYNX Echoform | delay | yes | yes |
| LYNX Tune | pitch correction | yes | yes |
| LYNX FM10 | instrument (FM) | yes | yes |
| LYNX Prism Multiband | multiband dynamics | yes | |
| LYNX Sentry Gate | gate / expander | yes | |
| LYNX Glow Leveler | optical leveler | yes | |
| LYNX Hush De-esser | de-esser | yes | |
| LYNX Ridge Strip | channel strip | yes | |
| LYNX Kiln Channel | console channel | yes | |
| LYNX Fret Channel | guitar / bass strip | yes | |
| LYNX Shell Channel | drum strip | yes | |
| LYNX Aether | air band + exciter | yes | |
| LYNX Ember Saturator | saturation | yes | |
| LYNX Epoch Verb | reverb | yes | |
| LYNX Foil Plate | plate reverb | yes | |
| LYNX Nebula | delay-reverb | yes | |
| LYNX Halo | shimmer reverb | yes | |
| LYNX Horizon | echo + reverb | yes | |
| LYNX Spiral Echo | frequency-shifting echo | yes | |
| LYNX Lattice Mod | modulated multitap delay | yes | |
| LYNX Orbit | chorus / flanger | yes | |
| LYNX Voice Shift | pitch / formant | yes | |
| LYNX Gemini Doubler | doubler | yes | |
| LYNX Quell Denoiser | noise reduction | yes | |

The last column of the table says what is built: `0` nothing, `1` in the CLAP, `2` in the CLAP and as a VST3 of its own.
The VST3 short list is the user's call: each VST3 carries the whole runtime (see "Sizes"), the CLAP holds them all at
the size of one. The CLAP announces each plugin's kind to the host (`clap/plugin-features.h`: equalizer, compressor,
reverb ...).

The names were checked by a **web search only, with no trademark registry check**. Single words that other plugin makers
may also use as product names (Nebula, Halo, Horizon, Orbit) always carry the "LYNX" prefix.

Instruments other than FM10 follow later with their own names (round 188b report: a proposed list for the user). Until
then they stay at `0`:

| Exported name | Engine inside LYNX | Kind | Built now |
|---|---|---|---|
| LYNX Osc Trio | 3x Osc | instrument | no (needs a host parameter table) |
| LYNX SimOsc | SimSynth | instrument | no |
| LYNX Harmlite | Harmless | instrument | no |
| LYNX Plucker | Plucked! | instrument | no |
| LYNX Wave Wanderer | Wave Traveller | instrument | no (needs a host parameter table) |

## Download and install (users)

Each release has, next to the app, one archive per system and a checksum file. LYNX SHARE itself doesn't need them.

| File | System |
|---|---|
| `LYNX-Plugins-<version>-win-x64.zip` | Windows 10 / 11, x64 |
| `LYNX-Plugins-<version>-macos-universal.zip` | macOS 11 or later, Apple silicon and Intel |
| `LYNX-Plugins-<version>-linux-x86_64.tar.xz` | Linux x86_64, glibc 2.35 or later (Ubuntu 22.04 and later) |
| `LYNX-Plugins-<version>-SHA256SUMS.txt` | the SHA-256 of the three archives |

Inside: `CLAP/LYNX Plugins.clap` (every plugin), `VST3/*.vst3` (the short list), `PLUGINS.txt`, `README.txt`, an install
script, `LICENSE` and `THIRD_PARTY.md`.

- **Windows:** right-click `install.cmd` > Run as administrator. It copies the CLAP to
  `C:\Program Files\Common Files\CLAP` and the VST3 bundles to `C:\Program Files\Common Files\VST3`. Or copy them there
  by hand. `install.cmd /uninstall` removes them.
- **macOS:** in Terminal, in the unpacked folder: `sh install.sh`. It copies them to `~/Library/Audio/Plug-Ins/CLAP` and
  `~/Library/Audio/Plug-Ins/VST3` and clears the download quarantine. The bundles are signed ad hoc only: there is no
  Apple Developer ID yet, so they are not notarized.
- **Linux:** `sh install.sh` copies them to `~/.clap` and `~/.vst3`. `sh install.sh --uninstall` removes them.
- Then rescan in the host. FL Studio: Options > Manage plugins > Find installed plugins. FL Studio finds VST3 only in
  the system folder (its manual: VST3 plugins "must be installed to default system locations").
- Check a download: `sha256sum <file>` (Linux), `shasum -a 256 <file>` (macOS) or `Get-FileHash <file> -Algorithm
  SHA256` (PowerShell), against the line in `LYNX-Plugins-<version>-SHA256SUMS.txt`.

The archive names never end like the app's own files: the in-app updater picks the app's files by their ending
(`core/UpdateCheck.cpp`), and `pluginexport188b_plugin_downloads_are_never_taken_for_the_app` keeps it so.

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
    sidechain detector and target submenu. The other effects' external side chains listen to the plugin's side-chain bus (see "Side chain").
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
cmake --build build-plug --target lynx_plugins
scripts/package-plugins.sh linux build-plug/plugins <label> dist      # or macos
```

`lynx_plugins` builds the CLAP, every VST3 of the table and `plugexporttest`. On macOS the CLAP and the VST3s are
bundles (`resources/plugin/Info.plist.in`); the packaging strips them and signs them ad hoc.

Only with the option on, the build also:

- turns on position-independent code;
- adds `-ffunction-sections -fdata-sections`;
- links each plugin with `--gc-sections`, hidden visibility and `-z defs`.

Windows from WSL (zig): `LYNX_BUILD_PLUGINS=1 scripts/build-windows-zig.sh`, then
`scripts/package-plugins.sh windows build-win/plugins <label> dist`. The build reuses the app's objects except
`Main.cpp`, then links the `.clap`, one `.vst3` per table line with built = 2, and `plugexporttest.exe`. With
`LYNX_BUILD_PLUGINS=1` the objects are compiled with `-ffunction-sections -fdata-sections`, so `--gc-sections` can drop
what a plugin never reaches (`LYNX_PLUGIN_SECTIONS=0` turns that off). The app's normal build is unchanged.

The Windows DLLs carry a Common Controls v6 manifest (`resources/LynxPlugin.rc`). JUCE imports `TaskDialogIndirect`, so
without the manifest the DLL fails to load with error 127.

## Testing

- `tests` (normal build): `pluginexport180b_*` and `pluginexport188b_*`. Every exported effect, run through the plugin's
  rack path, matches the effect alone to the sample. The table's names, ids and codes are unique and LYNX-named. The
  plugin downloads are never taken for an app update.
- `plugexporttest <plugin>` loads the binary with JUCE's VST3 host or LYNX's CLAP host and checks:
  - the scan;
  - a null test against the effect or instrument alone, at defaults and with every parameter moved;
  - automation;
  - a state round trip into a second instance;
  - that the state opens as a `.lynx` project;
  - the hosted and in-process editors, with PNG pictures;
  - memory per instance and CPU.
- pluginval at strictness 10, and clap-validator.
- `scripts/check-plugins.sh <unpacked download> <plugexporttest> <pluginval> <clap-validator> <reports> [pictures]` runs
  the three on exactly what an archive holds: plugexporttest on the CLAP and each VST3, pluginval on each VST3,
  clap-validator on the CLAP.
- CI: `.github/workflows/plugins.yml` builds, packages and checks the download for Windows (zig on Ubuntu, checked on
  windows-2022), Linux (Ubuntu 22.04 container, glibc check, editor pictures under Xvfb) and macOS (universal), then
  writes `LYNX-Plugins-<label>-SHA256SUMS.txt`. Started by hand on any branch (`workflow_dispatch`), or by `release.yml`
  on a release tag, which attaches the archives to the draft release (`plugins-publish`). The app's release doesn't wait
  for the plugins. Normal pushes don't build plugins.

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

## Side chain (round 188b)

Every exported effect has a second audio input, **Sidechain** (stereo or mono; off until the host connects it). In the
plugin's one-slot song it feeds a third mixer track that goes nowhere else, and that track is the slot's side-chain
source. So each effect's own "external" side-chain switch (Crest Compressor, Sentry Gate, Prism Multiband, Apex Limiter,
the channel strips) listens to what the host sends there, exactly as it listens to another insert inside LYNX. The VST3
calls it an aux input bus, the CLAP a second, non-main audio input port. A silent or unconnected bus changes nothing
(`plugexporttest` and `pluginexport188b_side_chain_bus_reaches_the_detector` check both). Facet EQ's sidechain
controls stay hidden in the plugins for now.

## Known gaps

- Facet EQ's sidechain controls are still hidden in the plugins (the bus is there; its editor's target menus assume LYNX's
  mixer).
- Ableton Live has not been tested, at the user's request.
- FL Studio 2026 (Windows) was tested with the CLAP only, found through `CLAP_PATH`:
  - the quick scan lists both plugins, and both load, play and open their editors;
  - a saved project reopens with their state;
  - FL exits cleanly.
- The VST3s were tested in FL from `Common Files\VST3`, the only place FL scans for VST3:
  - load, play and both editors;
  - a saved project reopens with its state;
  - closing the editors with X works;
  - FL exits in about 1.4 s.
- Both formats are validated with pluginval, clap-validator and the LYNX host on Windows and Linux.
- Opening a project and closing it untouched doesn't make FL ask to save. A state load sends the host no edits:
  `plugexporttest` checks this, and for the CLAP it also checks for no `mark_dirty`.
- Fixed in this round: FL hung on exit when the host unloaded the plugin DLL. LYNX's static image caches still held JUCE's
  Direct2D device, and freeing it under the loader lock deadlocked in the graphics driver. The last plugin instance
  now empties them (`ui/ImageCaches.h`), and a lint test keeps new static image caches registered.
- Editors other than Facet EQ and FM10 need to be checked for FL wording before they are exported.
- The plugins are unsigned.
- macOS is not built or tested. The app's macOS-only `VideoMediaMac.mm` is not in the shared source list.
