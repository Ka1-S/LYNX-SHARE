# Round 176a — 32-bit plugin bridge (Windows)

Users asked for this in discussion #10, and the user decided on it on 2026-11-04 (REA_rodada169k §4).

LYNX is a 64-bit program. A 64-bit process cannot load a 32-bit DLL, so old 32-bit VST2 / VST3 plugins did not work
at all: the scan child's `LoadLibrary` failed, and the file was remembered as "no plugins". FL Studio 2026 runs them
in a 32-bit helper process, `System\Tools\Bridge\32bit\ilbridge.exe`. LYNX now does the same with
`LYNX-bridge32.exe`.

Evidence tags: **[DECOMP]** Ghidra / objdump on FL's own binaries; **[DATA]** strings, DFM forms, files;
**[EXEC]** our own runs; **[UI]** FL screenshots. No third-party plugin was disassembled. The only plugin used is
LYNX's own test synth (`tools/testplugin/lynx_test_vst2.c`), built for x86.

## 1. What existed

- **Separate-process plugins** (`engine/PluginSandbox`, rounds 157a/161/168s). FL calls this "Make bridged".
  - The child is `LYNX.exe --plugin-host` (JUCE `ChildProcessCoordinator`).
  - Audio, MIDI, parameters and the render mode go through one shared-memory block per block. Two events act as
    doorbells.
  - State and editor requests go through messages.
  - A hang or crash of the child mutes the plugin.
- **The 32-bit toolchain.** The Windows build already made `lynx-asio32.dll` with zig `x86-windows-gnu`.
- **The scan.** Each file is described in a child process (`--scan-one`). A 32-bit DLL came back empty. It went into
  `scan-empty.txt` and was skipped after that.

## 2. FL's bridge **[DATA + DECOMP]**

| Item | FL 2026 | Where |
|---|---|---|
| 32-bit host | `System\Tools\Bridge\32bit\ilbridge.exe` (Delphi, i386). A 64-bit `ilbridge.exe` is also shipped for "Make bridged". | files; FLEngine strings `32bit\ilbridge.exe` (xref `FUN_00c9f500`), `64bit\ilbridge.exe` |
| IPC | `IPC.Bridge`: `TBridgeShare`, `TBridgeDataShare` (shared memory), `TBridgeMessageShare`, `TBridgePingShare` / `TBridgePingThread` (liveness), `TBridgeProcessingThread`, `TWaitingThreadInfo` | FLEngine / Fruity Wrapper RTTI |
| Loading | through a queue: `ILBridgeIntf.TBridgeLoaderQueue` / `TBridgeLoaderQueueItem`, `TBridgedPlugin.LoadPluginNow` | RTTI |
| Teardown | "Bridge process destruction timed out" (`TBridgedPlugin.Destroy`, `FUN_00c9e320`) | DECOMP xref |
| Large address aware | `ilbridge.exe` COFF characteristics `0x0122`, which includes `IMAGE_FILE_LARGE_ADDRESS_AWARE` (`0x20`): 4 GB for a 32-bit plugin on 64-bit Windows | PE header |
| x87 control word | `0x528ba0`: `fnclex; fldcw [0x71b368]; emms`. `[0x71b368]` = `0x033F` (all exceptions masked, 64-bit precision, round to nearest). Called from 40 sites. | objdump on ilbridge.exe |
| Wrapper option | "Make bridged" / "\|Load plugin in separate process (bridged)". Flag `0x40000` of the wrapper record (REA_rodada134 B3). | resource pool |
| Editor option | "\|Keep plugin editor in bridge process" | resource pool |
| Plugin manager option | "\|Add a single entry for plugins that have both a 32 bit and a 64 bit version" | resource pool |
| Labels | "(%s, %s bit)", "32 bit", "(bridged)" | resource pool (no direct xrefs: FL's translated-string table) |
| Detached-editor stand-in | `TBridgedEditorForm`: "This plugin's editor window is detached." / "If you don't see it, click the button below to show it." / button "Toggle" (hint "Show plugin editor") | FLEngine DFM |
| Plugin database | `.nfo` keys `ps_file_bitsize_<n>` (32 / 64) and `ps_file_arch_<n>` (`Intel`) | FLEngine UTF-16 strings; this PC's 429 entries are all 64 |

## 3. FL's UI for a 32-bit plugin **[UI]**

FL Studio 2026, with LYNX's own test synth built for x86 (`LYNX Test Synth 32.dll`) in `C:\Program Files\VstPlugins`.
The shots are in the round's work folder (`fl15c` … `fl36c`).

| Where | What FL shows |
|---|---|
| Plugin manager (Options › Manage plugins) | A **Bits** column: `32` for the 32-bit DLL (`fl15c`). The columns are F, Name, Status, Format, Bits, Effect/Synth, Vendor, Filename. |
| Plugin picker ("Select generator plugin", the Channel rack's + › More plugins...) | The same **Bits** column: F, Name, Format, Bits, Vendor (`fl19c`). |
| Channel rack | The plugin's editor is **embedded** in its wrapper window, as for a 64-bit plugin (`fl20`). |
| Wrapper › Settings › Info › Type | `VSTi v2.4 (32bit - bridged)` (`fl23c`). A 64-bit plugin: `VSTi v2.4 (64bit - native)` (`fl33c`). |
| Wrapper › Processing › Options | **No "Make bridged"** for the 32-bit plugin, only "External window" (`fl25c`). A 64-bit plugin has "Make bridged" and "External window" (`fl34c`). |
| Wrapper menu (the arrow) | No bridging item for either. |
| "Combine 32 and 64 bit versions of a plugin" (Plugin manager scan option, on) | A 32-bit and a 64-bit build of the same plugin (same ID and name) are **one entry, Bits `32+64`** (`fl29c`). It loads the 64-bit build: Type `(64bit - native)`. Its `.nfo` lists both files (`ps_files=2`, `ps_file_bitsize_0=32`, `ps_file_bitsize_1=64`). |
| Wrapper › Settings › Options › **Bit size** | `Native` / `32 bit` / `64 bit` (`fl36c`): which build loads. `Native` for both plugins above. |

**LYNX now:**
- The Plugin manager and the plugin picker have a **Bits** column (Windows x64 only), after Format.
- They combine the two builds of one plugin into one `32+64` entry (`plugincatalog::combinedBits`, keyed by format, ID
  and name: `PluginHost::bitnessKey`).
- The wrapper's Settings page:
  - Info › Type ends with `(32bit - bridged)`, `(64bit - native)` or `(64bit - bridged)`;
  - a new **Options › Bit size** (Native / 32 bit / 64 bit), saved per plugin (`PluginRef::bitSize`, project key
    `bitSize`), picks the build (`PluginHost::buildFor`). Changing it re-creates the plugin with its state, like "Make
    bridged".
- A 32-bit plugin's Processing page and its Processing menu have no "Make bridged" / "Run in a separate process".

**Not yet like FL:**
- **The editor.** FL embeds the bridged editor in its wrapper window ("External window" off). LYNX keeps its
  separate-process stand-in, with the plugin's own window floating, as for "Make bridged" since issue #8. A window
  owned across processes ties the two input queues, so a hung plugin can freeze the host's UI. **User decision
  2026-11-06: match FL** — embedded by default plus an "External window" toggle — in a follow-up round. 176a ships
  with the floating window.
- **The scan option.** FL has an on/off scan option for combining (on by default). LYNX always combines.
- **The `.flp` field.** Where FL stores "Bit size" in the `.flp` was not looked for. LYNX imports and exports it as
  Native.


## 4. LYNX design

- **`LYNX-bridge32.exe`** (`src/bridge/Bridge32Main.cpp`, `scripts/build-bridge32.sh`) is a small JUCE app built for
  x86.
  - Only the modules a plugin host needs: core, data_structures, events, graphics, gui_basics, gui_extra,
    audio_basics, audio_processors.
  - Formats: LYNX's own VST2 host (`engine/Vst2Format`) and JUCE's VST3.
  - Two jobs: `--plugin-host` (the sandbox child) and `--scan-one` (the scan child).
  - Size: 7.5 MB. Large-address aware, like ilbridge.
- **One child side for both bitnesses.**
  - The sandbox's child half moved out of `PluginSandbox.cpp` into `engine/SandboxWorker.cpp`.
  - The wire format moved into `engine/SandboxShared.h`.
  - Where the two programs differ, the worker calls hooks (`engine/SandboxWorker.h`: open, prepare, captureState,
    window colour / icon, x87 control word). LYNX.exe implements them with its PluginHost; the bridge with its own
    two formats.
  - `SharedBlock` has only fixed-size fields. `static_assert`s pin its layout, so the 64-bit parent and the 32-bit
    child agree byte for byte.
- **New in the protocol (both bitnesses): the transport.** The parent copies its play head's position into
  `SharedBlock::transport` every block: BPM, PPQ, bar start, loop, samples, time signature, and the playing,
  looping and recording flags. The child gives it to the plugin as its play head. Before this, a bridged plugin had
  no play head at all: a tempo-synced plugin ran at 120 BPM.
- **x87.** The bridge loads FL's control word `0x033F` before each plugin call (audio block, open, state, editor).
  An old plugin that does its maths on the x87 then rounds as it does in FL. Windows' own default for a 32-bit
  program is `0x027F` (53-bit precision). `LYNX_BRIDGE32_X87=0` leaves the default (for A/B tests).
- **Detection** (`core/PeArch.h`, pure and covered by core tests). The machine field of the PE header tells the
  architecture.
  - A VST3 bundle counts by its module: `Contents/x86_64-win`, `arm64-win`, then `x86-win`. A bundle that also has a
    64-bit module is loaded 64-bit.
  - `isModule32Bit` caches the answer by path and date. Windows x64 builds only; elsewhere it is always false, so
    nothing changes there.
- **Routing.**
  - `PluginHost::createInstance`: a 32-bit plugin always goes to the sandbox, in the bridge, whatever "Make
    bridged" says. FL does the same.
  - `createSandboxedPlugin` picks `LYNX-bridge32.exe` by itself. The crash notes say "32-bit bridge".
  - Scan: `scanOne` starts the bridge for a 32-bit file. Without the bridge the file is skipped and not marked bad.
  - `addPluginFile` (Plugin manager "Add plugin file", the tests) goes through the same path.
  - `scan-empty.txt`: an empty result for a 32-bit file only counts when the bridge produced it (`|bridge32`). So
    32-bit DLLs that earlier 64-bit scans marked empty are described again once.
- **Crash isolation.**
  - A crash takes only the bridge process down. LYNX notices on the broken connection, mutes the plugin and keeps
    the project running.
  - Deaths are recorded without allocating or locking (they can happen on the audio thread).
  - The UI shows round 174a's "Plugin crashed" prompt (Save as... / Close) and logs the death to
    `plugin-crashes.log`.
  - This replaces the old one-line "stopped responding" alert. It also covers "Make bridged" plugins.
- **Packaging.** Release zip, NSIS installer (install and uninstall), MSIX layout, the CI artifacts and the SignPath
  configuration doc.

## 5. Tests and numbers **[EXEC]**

`LYNX.exe --bridge32test "LYNX Test Synth 32.dll" report.txt --compare "LYNX Test Synth.dll"` (`engine/Bridge32Test.cpp`),
Windows 11, 48 kHz, on LYNX's test synth built for x86 and x64 (`tools/testplugin/lynx_test_vst2.c`):

| Step | Result |
|---|---|
| Detection | PE machine x86: `module_32bit=1`, bridge found beside LYNX.exe |
| Scan through the bridge | 212 ms, 1 plugin: `LYNX Test Synth`, VST, instrument, `is32=1` |
| Open | 40 ms; runs in the bridge (`bridged32=1`), the plugin itself reports 32 bits (its "Bits" parameter), latency 64 samples reported (set by the test through the plugin) |
| Audio (1 s, 512-sample blocks) | peak 0.5, 439.5 Hz (the synth plays A4) |
| Automation | gain 0.25 → peak ratio 0.500 |
| State | 33-byte chunk: gain 0.25, tempo 137 from the play head (the new transport in the shared block) |
| Restore | a second instance at 96 kHz with odd block sizes: gain 0.25, 436.8 Hz (frequency estimate over odd blocks) |
| Editor | the plugin's window of the bridge's PID shows (1.7 s, first show) and hides again |
| Crash | the plugin writes to a null pointer in its 20th block: dead after 20 blocks / 0.5 s, silence after, another bridged instance still plays (peak 0.25), the crashed bridge process is gone |

Cost, 10 s of 256-sample blocks (5.3 ms each) at 48 kHz:

| | µs per block | CPU (% of real time) |
|---|---|---|
| 64-bit, in LYNX's process | 3.6 | ~0 |
| 64-bit, separate process ("Make bridged") | 24.9 | 0.31 |
| **32-bit, in the bridge** | **26.6** | **0.47** |

- The bridge adds **23 µs per block** (two event round trips through shared memory). It adds **no latency**: each block
  is processed in the bridge before the call returns.
- No allocation per block, in either process (fixed `SharedBlock`, preallocated parameter and MIDI queues).
- Core tests (`tests/Bridge176aTests.inc`): PE machine detection, non-PE input, VST3 bundle folders, the 32+64
  combining.
