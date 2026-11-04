# REA round 169l — Drumaxx's level, a stateless Drumaxx, what a NEW generator is in FL

Evidence markers: [DECOMP] Ghidra / objdump on FL's own binaries; [DATA] FL's own files read byte by byte; [FL render]
FL Studio 2026's command-line render (`FL64.exe /R /Ewav`, both FL locks, one launch); [CODE] LYNX source; [TEST] LYNX
tests. Clean-room: behaviour only, nothing of FL's is copied or shipped (the test project is built at run time from the
user's own FL files and stays under `C:\Users\kauan\lynx-test\gen169l`).

## 1. Drumaxx +3.0 dB: the host's pan law was missing [DECOMP][FL render]

Round 169e: rendering `drumaxx169e\fl\dmx169e.flp`, every LYNX pad came out +3.0 dB over FL's render.

- The ratio LYNX / FL is **1.4142** on both sides of every pad (per-note peaks, `cmpw.py`); scaled by 1/√2 the whole
  LYNX render equals FL's within the 16-bit render's LSB (RMS difference 8.8·10⁻⁶ in every 100 ms window, tails
  included). So the engine is right; one √2 factor is missing in front of it.
- **Where** (`Plugins/Fruity/Generators/Drumaxx/engine_x64.dll`, the FL-plugin side of the engine, image base
  0x180000000): `FUN_180003e60` runs every block. For each key's host voice it loads **Final.Pan** (`[voice+0x14]`, into
  xmm3) and **Final.Vol** (`[voice+0x18]`, on the stack) and calls host slot 31 (`call [rax+0xf8]` at `0x180003efe`,
  the SDK's `ComputeLRVol(&L, &R, Pan, Vol)`), then stores L and R in the engine's per-key tables (`+0x20` / `+0x220`,
  the `keyL` / `keyR` the pads' gains are scaled by), the pitch offset in `+0x420`; keys without a voice take the
  nearest held voice's values. It is the same framework code as Sakura's and Drumpad's wrapper (REA_rodada28 §4.2).
- FL's ComputeLRVol (REA_rodada28 §4.1, `FLEngine_x64.dll` `FUN_006a3a00`, law A): `L = Vol·cos((1 + pan)·π/4)`,
  `R = Vol·sin(…)` — **0.7071·Vol at the centre**.
- Round 67's harness (`r67/run/dm.c`) stood in for slot 31 with `L = R = vol` (no pan law), so the port took
  "keyL = keyR = Final.Vol" and concluded "Final.Pan is ignored". Both were artefacts of the stand-in: the engine is
  still bit-identical to the DLL for given tables; what FL hands it is `Vol·cos/sin`, and **the pan does count**.

LYNX now (`core/Drumaxx.cpp`, `DrumaxxInstrument::keyLevels`): each key's tables are law A of (note pan + channel pan,
clamped; channel volume), refreshed when the channel's volume / pan move (FL recomputes them every block), as LYNX's
BassDrum wrapper already did.

## 2. A Drumaxx channel without a state [FL render][CODE]

FL renders a Drumaxx channel with no state (the DLL's own defaults) silent on every key (REA_rodada169e §2). LYNX's
importer left such a channel unsupported, so the stand-in synth played a tone. Now an FL Drumaxx with **no state at all**
becomes a Drumaxx channel without a kit, silent like FL (an unreadable, non-empty state stays unsupported as before).
Old LYNX projects are not affected: LYNX never made a Drumaxx without a state (a new one gets a kit, an imported one
without a state was the synth channel and is saved as such).

## 3. What a NEW generator is in FL [DECOMP][DATA]

- Every way of adding a generator in FL 2026's UI goes through the **Plugin database**: the Channel rack's "+" button
  (`AddChanBtnMouseDown`, `0x103aa00`) opens the plugin list `FUN_00ebf510(0, 0, −10)` (TPlugListForm, the picker of
  round 169k). Picking an entry (`FUN_00ec7840`, "insert channel") makes a new channel (`FUN_011f53d0`) and loads the
  **entry's .fst file** into it (`FUN_00ec7720`: channel vmt `+0x150` (path, 1, 2, 0, −1.0)). So FL's fresh generator is
  the Plugin database `.fst` state.
- FLEngine never loads a generator's `Plugin presets\…\Default.fst` (its four "Default.fst" references are the mixer
  track reset / instrument-track paths, REA_rodada169e §1).
- Inventory of the FL engines LYNX has (`Data/Patches`, MD5 of the plugin state):

(table filled below)
