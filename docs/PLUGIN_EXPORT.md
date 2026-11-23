# LYNX's own plugins as CLAP, VST3 and LV2 (rounds 180b, 188b, 190ov, 190pk, 190pq)

Status: **built by the release workflow as a separate, optional download** (the user's option C, 2026-11-08): every LYNX
effect in one CLAP, a short list also as VST3 (and on Linux as LV2), for Windows, Linux and macOS. The
user-facing summary is [PLUGINS-PACK.md](PLUGINS-PACK.md). It is never part of the app's installer, and
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
| LYNX Strike Shaper | transient designer (round 190ov) | yes | |
| LYNX Anvil Bus | drum-bus processor (190ov) | yes | |
| LYNX Corrode | degrader (190ov) | yes | |
| LYNX Soundboard | resonant body (190ov) | yes | |
| LYNX Platter | record-wear effect (190ov) | yes | |
| LYNX Timeprism | spectral freezer / delay (190ov) | yes | |
| LYNX Chimefield | spectral resonator (190ov; the Harmonia engine) | yes | |
| LYNX Model | instrument (physical models, 190ov) | yes | yes |
| LYNX Fourfold | instrument (4-operator FM: the Tetra engine, 190pq) | yes | yes |
| LYNX Wavecoil | instrument (wavetable: the Helix engine, 190pq) | yes | yes |
| LYNX Duoloom | instrument (two-engine macro synth: the Braid engine, 190pq) | yes | yes |

LV2 (Linux; rounds 190ov and 190pk, shipped since 0.1.31): every VST3 of the list is also built as an LV2 bundle on Linux
(`-DLYNX_PLUGINS_LV2`, on by default; it does nothing elsewhere): JUCE's LV2 wrapper (`src/plugin/Lv2Entry.cpp`, the same
entry as the VST3's), `<name with dashes>.lv2/` with the library and the `.ttl` files JUCE's `juce_LV2ManifestHelper` writes
from it after the link; the URI is `https://lynx-share.com/plugins/lv2/<the clap id after "com.ka1-s.lynx.">` and never
changes. `package-plugins.sh` packs them in `LV2/`, `install.sh` copies them to `~/.lv2`, and `check-plugins.sh` runs
`plugexporttest` (through JUCE's LV2 host) and `lv2lint` on each. Round 190ov's CI run 44955467954 had them failing in
`plugexporttest`; round 190pk found two JUCE bugs behind that (`cmake/LynxPatchJuce.cmake`, JUCE fix 50):
- the effects crashed: JUCE's LV2 wrapper declares the side-chain ports `lv2:connectionOptional` (a host may leave them
  unconnected) but copied their input from the NULL pointer such a host gives. An unconnected port is silence now.
- FM10 and LYNX Model ended on an X error (BadAtom, X_ChangeProperty, atom 0): on an X server no window manager ever ran
  on (CI's Xvfb) the WM_PROTOCOLS atom doesn't exist, and JUCE set it on every new window with atom 0 (and read WM_STATE /
  _NET_WM_STATE with atom 0). The LYNX app ignores X errors; a plugin's host has Xlib's default handler, which exits.

`plugexporttest`'s "attempt to map invalid URI <bundle path>" line is lilv's: JUCE's host first tries the path as a URI,
then loads it as a bundle. Harmless. Two things the LV2 host test needs: JUCE's LV2 host lists the plugin's parameters
in an order of its own (plus a "Bypass" for the `lv2:enabled` port), so `plugexporttest` finds the engine's parameter
`i` by its id (`...:p<i>`); and JUCE's host tells an instrument by its class only when the LV2 specification bundles are
in `~/.lv2`, `/usr/lib/lv2` or `/usr/local/lib/lv2` (CI installs `lv2-dev`; every Linux with an LV2 host has them).

`lv2lint` (built from its author's repository at a fixed commit in CI) runs with `-Mpack` and two of its checks skipped,
both about JUCE's wrapper rather than LYNX: "Plugin Run" fails a `run()` that takes any mutex, and JUCE's takes the
processor's callback lock (uncontended unless the host is loading a state; JUCE's VST3 wrapper does the same), and "UI
Resize Interface" fails the `ui:resize` extension data JUCE's editor offers. Its warnings left: the editor uses
instance-access, the editor lives in the plugin's binary. (Since 0.1.31 each plugin's `.ttl` names its licence,
`doap:license <http://spdx.org/licenses/AGPL-3.0-or-later>`, JUCE fix 51; JUCE wrote none.)

The last column of the table says what is built: `0` nothing, `1` in the CLAP, `2` in the CLAP and as a VST3 of its own.
The VST3 short list is the user's call: each VST3 carries the whole runtime (see "Sizes"), the CLAP holds them all at
the size of one. The CLAP announces each plugin's kind to the host (`clap/plugin-features.h`: equalizer, compressor,
reverb ...).

The names were checked by a **web search only, with no trademark registry check**. Single words that other plugin makers
may also use as product names (Nebula, Halo, Horizon, Orbit; round 190ov: Platter, also a Kontakt instrument, and Anvil, a guitar preamp plugin)
always carry the "LYNX" prefix.

Round 190ov added LYNX Model (LYNX's own physical models) and seven later LYNX-own effects. LYNX's own synths Tetra, Helix
and Braid export under names of their own (owner decision, rollup 0.1.31 step 6), because their engine names are other
makers' products (Sequential's Tetra synth, Line 6's Helix / Helix Native, Mutable Instruments' Braids, also a Softube
plugin): **LYNX Fourfold**, **LYNX Wavecoil** and **LYNX Duoloom**, built as CLAP, VST3 and LV2 since round 190pq
(0.1.31). Tetra and Braid have no preset bank: their plugin starts from the engine's defaults
(`plugexport::instrumentChannel`). CI plugins run 44975814572 had held them back (step 6) with two failures, both fixed in
round 190pq:
- `plugexporttest` aborted in the CLAP (`std::length_error`, `basic_string::_M_create`; a segmentation fault locally):
  its reference render (`directInstrument`) dereferenced the empty `banks::defaultChannel` of Tetra / Braid and copied a
  `ChannelDef` from garbage. A bug in the test tool, not in the plugins: it now starts from `plugexport::instrumentChannel`,
  as the plugin does.
- clap-validator's state-reproducibility-binary (save, load into a new instance, save again: the same bytes) failed on
  Fourfold and Wavecoil: every save wrote each host value back into the channel through the parameter's range
  (raw -> 0..1 -> raw), which moves some values by a float step, and `modelParams` are written with 7 digits, so the
  second save differed in a last digit (Fourfold `modelParams` 46 and 138, Wavecoil 46 and 63). A save now writes only
  the values the host moved. `.lynx` loading is unchanged. `plugexporttest` checks the same thing on every plugin
  and format (every parameter moved, a third instance loads the state and saves it back byte for byte).

With those fixed, the CLAP test went further and found a crash that every LYNX instrument already had (FM10 and Model
too): an instrument's editor crashed in a host that had removed every LYNX plugin and then added a LYNX instrument
again. Each plugin instance holds a JUCE initialiser, and the last one to go shuts JUCE down, which deletes
`WaveformCache` (a `DeletedAtShutdown` singleton). The library stays loaded, because JUCE's own `thread_local` text
shaping registers a TLS destructor on the host's main thread, and that pins the library until the thread ends. The cache
was declared "never create again after deletion", so the next instrument editor's `WaveformDisplay` got a null cache.
It may be created again now (`src/ui/WaveformView.h`); the app deletes it only at its own shutdown. `plugexporttest`'s
plain-host CLAP pass, which creates and frees one plugin at a time, covers this case. The instrument check also moves
the engine's first continuous parameter by id under JUCE's LV2 host (that host lists parameters in its own order, and
its parameter 1 was a stepped on / off switch on the three synths).

The editor pictures `check-plugins.sh` draws showed two more things, both fixed in round 190pq:
- LYNX Duoloom opened the Sampler's page (File, Time stretching, "No sample loaded"), not Braid's knob list.
  `makeChannelSettingsPanel` had its own copy of the model-engine list, and round 190ko added Braid only to
  `isModelKind`. The copy is gone, so the app's channel window for a Braid channel (Pattern layout) also shows the knob
  list 190ko described, instead of the Sampler's page.
- LYNX Fourfold's title strip printed "TETRA". As a plugin it prints the plugin's name (FOURFOLD), as the other
  editors' name plates do; inside LYNX it stays TETRA. The knob-list page names the plugin as its description too. The Harmonia engine exports as **LYNX
Chimefield** (Cherry Audio sells a Harmonia synth). Inside LYNX the engines keep their names. Instruments that stand in for FL Studio's follow later with their own names (round 188b report: a
proposed list for the user). Until then they stay at `0`:

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
| `LYNX-Plugins-<version>-macos-universal.tar.xz` | macOS 11 or later, Apple silicon and Intel |
| `LYNX-Plugins-<version>-linux-x86_64.tar.xz` | Linux x86_64, glibc 2.35 or later (Ubuntu 22.04 and later) |
| `LYNX-Plugins-<version>-SHA256SUMS.txt` | the SHA-256 of the three archives |

Inside: `CLAP/LYNX Plugins.clap` (every plugin), `VST3/*.vst3` (the short list), on Linux `LV2/*.lv2` (the same short
list), `PLUGINS.txt`, `README.txt`, an install script, `LICENSE` and `THIRD_PARTY.md`.

- **Windows:** right-click `install.cmd` > Run as administrator. It copies the CLAP to
  `C:\Program Files\Common Files\CLAP` and the VST3 bundles to `C:\Program Files\Common Files\VST3`. Or copy them there
  by hand. `install.cmd /uninstall` removes them.
- **macOS:** in Terminal, in the unpacked folder: `sh install.sh`. It copies them to `~/Library/Audio/Plug-Ins/CLAP` and
  `~/Library/Audio/Plug-Ins/VST3` and clears the download quarantine. The bundles are signed ad hoc only: there is no
  Apple Developer ID yet, so they are not notarized.
- **Linux:** `sh install.sh` copies them to `~/.clap`, `~/.vst3` and `~/.lv2`. `sh install.sh --uninstall` removes them.
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
  windows-2022), Linux (Ubuntu 22.04 container, glibc check, no static TLS, editor pictures under Xvfb) and macOS (universal), then
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

Garbage collection of unused sections makes the plugin 32 % smaller, so it stays on. Since round 188b a Windows build
with `LYNX_BUILD_PLUGINS=1` compiles every object with section flags as well.

Almost all of a plugin's size is the shared app runtime. A CLAP with N plugins stays at about 39 MB. N separate VST3s
cost about 39 MB each. The user chose option **C** (2026-11-08) from:

- **A.** One VST3 per plugin. This is the simplest, but at 25 effects plus 6 synths it comes to about 1.2 GB.
- **B.** A JUCE patch for several classes in one VST3 bundle, or a shared runtime DLL next to thin VST3s. This keeps
  everything at about 40 MB, but it means patching and maintaining JUCE's wrapper.
- **C.** Ship the CLAP with every plugin, plus VST3s for a short list only, as a separate download.

The download (round 188b, 1 CLAP + 7 VST3):

| Archive | Size |
|---|---|
| `LYNX-Plugins-<v>-linux-x86_64.tar.xz` (stripped; one xz stream with a 256 MiB dictionary, so the VST3s cost almost nothing next to the CLAP) | 14.9 MB |
| `LYNX-Plugins-<v>-macos-universal.tar.xz` (arm64 + x86_64 in each binary; Archive Utility opens it) | 22.2 MB |
| `LYNX-Plugins-<v>-win-x64.zip` (zip compresses each file alone; each binary 24.9 MB, with section flags) | 84.6 MB |

Round 190pk's Linux package (1 CLAP with 36 plugins, 8 VST3, 8 LV2) is 26.3 MB from CI (GCC 13), 31.6 MB locally
(GCC 15): the 256 MiB window holds
only about five of its 48 MB binaries, so the LV2 bundles, packed after the VST3s, add their own share.

Round 190pq (1 CLAP with 39 plugins, 11 VST3, 11 LV2; CI run 45058238805): the Linux archive is 27.1 MB (CI's
artifact, xz inside), 31.7 MB locally; the Windows zip is 195.4 MB (8 VST3: 140.8 MB in round 190ov): each Windows
VST3 is compressed on its own, so each of the three synths adds about 18 MB there; macOS universal 39.0 MB (34.6 MB).
The Windows build at JOBS=2 took 1 h 10 min (45 min with 8 VST3), macOS universal 2 h 22 min.

Round 190pu (shrinking the Windows pack; audit in lynx-work/packsize_190pu/AUDIT.md): the zip's cost is its method, not the
binaries: 12 files of 37 MB (the CLAP and 11 VST3, each 16.3 MB deflated, differing from each other by under 0.5 %). The same
folder as one solid LZMA2 stream (`.7z`, 128 MiB dictionary) is 20 MB, the same files byte for byte (CI diffs the two). So
the Windows pack is now offered twice: `LYNX-Plugins-<v>-win-x64.zip` (195 MB, opens everywhere) and `...-win-x64.7z` (20 MB,
Windows 11 Explorer or 7-Zip). Tried and not taken: ICF (zig's linker driver rejects `--icf`), a shared runtime DLL or several
classes in one VST3 (option B above: it needs a patch of JUCE's VST3 wrapper, whose class ids, bus layout and MIDI mode are
compile-time macros; the owner chose C in round 188b).

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

## Known gaps and test record

- Facet EQ's sidechain controls are still hidden in the plugins (the bus is there; its editor's target menus assume LYNX's
  mixer).
- The side-chain bus is checked through the VST3 (JUCE's host: a loud side signal into Crest Compressor's external side
  chain takes its output energy to 0.14 of the quiet case). LYNX's own CLAP host has no side buses, so the CLAP's second
  port is checked by clap-validator only, not with audio.
- Ableton Live has not been tested, at the user's request.
- LV2, round 190pk: CI run 45010577680 (Plugins workflow, Linux job) passes all 34 checks of the archive, the 8 LV2
  bundles' `plugexporttest` and `lv2lint` among them. Locally (Linux, WSL, a private Xvfb with no window manager) the 8
  bundles of the packed archive pass
  `plugexporttest` (scan, instrument / effect, null test at the defaults and with every parameter moved, side-chain
  bus, state, editor, 8 more instances) and `lv2lint`; jalv 1.8.0 (JACK's dummy driver) runs each for 10 s and lv2bench
  (lilv 0.26.2) instantiates, runs 1,024 blocks and frees each. The same run passes every CLAP and VST3 check
  (`plugexporttest`, pluginval strictness 10, clap-validator). Not tried in Ardour, Carla, Qtractor or Zrythm yet, and
  the editor only inside JUCE's host (jalv's command-line host shows no X11 editor).
- Round 190pq: CI run 45058238805 (Plugins workflow): Linux 46 / 46 checks pass (plugexporttest on the CLAP, 11 VST3 and
  11 LV2; pluginval strictness 10 on 11 VST3; clap-validator; lv2lint on 11 LV2), Windows and macOS universal 24 / 24
  each (plugexporttest on the CLAP and 11 VST3, pluginval on 11 VST3, clap-validator). The same 46 pass locally. New in plugexporttest: a state saved again after a load is the
  same bytes with every parameter moved, on every plugin and format.
- FL Studio 2026 (Windows), round 180b: Facet EQ and FM10, as CLAP (through `CLAP_PATH`) and as VST3 (from
  `Common Files\VST3`): load, play, both editors, a saved project reopens with its state, FL exits in about 1.4 s.
- FL Studio 2026 (Windows), round 188b, the release download from CI: ADD > Refresh plugin list (fast scan) lists all 27
  effects of the CLAP and the VST3s. Kiln Channel, Halo and Sentry Gate (CLAP) and Apex Limiter and LYNX Tune (VST3) in
  one insert: each loads and opens its editor (FL shows Apex's 225-sample latency). LYNX Tune's Humanize set to 29 %, the
  project saved; reopened, every slot and the 29 % come back; reopened and closed untouched, FL asks nothing and exits in
  1.2 s; with an editor open it exits in 1.5 s. Shots: round 188b's `flshots/`.
- Every plugin passes `plugexporttest`, pluginval (strictness 10) and clap-validator on Windows and Linux (CI and local);
  macOS in CI.
- Opening a project and closing it untouched doesn't make FL ask to save. A state load sends the host no edits:
  `plugexporttest` checks this, and for the CLAP it also checks for no `mark_dirty`.
- Fixed in round 180b: FL hung on exit when the host unloaded the plugin DLL. LYNX's static image caches still held JUCE's
  Direct2D device, and freeing it under the loader lock deadlocked in the graphics driver. The last plugin instance
  now empties them (`ui/ImageCaches.h`), and a lint test keeps new static image caches registered.
- Fixed in round 190fp: no host on Linux could load the 0.1.27 plugins ("cannot allocate memory in static TLS block";
  the Linux archive was missing from that release). The crash guard's per-thread pointer (`platform/CrashGuard.cpp`) was
  an initial-exec `thread_local`, which makes the module's whole TLS block (about 33 KB) static, far more than glibc
  keeps for modules loaded with dlopen. In the plugins' runtime it is a pthread key now, and
  `scripts/check-linux-compat.sh` fails any library that still has the `STATIC_TLS` flag.
- Fixed in round 188b: a host restart (prepareToPlay) kept the engines of the last build, so a reverb's tail, LYNX Tune's
  detector history and Quell's state outlived it; it now starts from new engines, as a fresh instance.
- The editors of all 27 effects were checked for FL wording (round 188b, `plugexporttest` pictures): none; the header no
  longer prints "Insert 1 - slot 1".
- The plugins are unsigned (Windows), and signed ad hoc only on macOS (no Apple Developer ID, not notarized).
