# Round 171b (REA + feature) — issue #65: Patcher parameter (event) inputs and outputs

Issue #65 (0.1.23, Linux): *"Patcher inputs are not working at all yet. In FL you can select inputs to map them to
knobs … here this is not available."* The reporter's FL screenshot (`/mnt/e/lynx-work/issues171/i65-3.png`) shows a
Patcher node's right-click menu — *Rename… / Save preset as… / Disconnect / Delete / Inputs ▸ / Outputs ▸ / Allow
threaded processing* — with *Inputs ▸ Parameters ▸* open: *Last tweaked (1. Gain)*, then *1. Gain, 2. Soft saturation
threshold, 3. Limiter ceiling …* as check items. LYNX's node menu (`i65-4.png`) only had *Open editor / Rename… /
Bypass / Delete*, and a Surface knob could only be cabled by dropping the cable on a node's body (nothing showed that).

Evidence markers: **[DECOMP]** FL Studio 2026's own `Plugins/Fruity/Generators/Patcher/Patcher_x64.dll` (7,052,800
bytes, 2026-10-08) in headless Ghidra 12.1.4 (read-only on the round-162 project `/mnt/d/lynx-work/patcher162/ghidra`;
decompiles in `/mnt/e/lynx-work/patcher171b/rea/strrefs.c`, `menu.c`), **[DATA]** FL's own Patcher presets
(`Data/Patches/Plugin presets/Effects/Patcher/**/*.fst`, scanned by `scratchpad pscan.py`), **[SHOT]** the reporter's FL
screenshot, **[TEST]** LYNX's tests, **[HYP]** inference. Clean-room: behaviour and formats only, no FL code copied.

| Reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| "In FL you can select inputs to map them to knobs" | §1.1–1.3 | no Inputs/Outputs menu; a control cable could only be dropped on a node body | FL's node menu with Inputs ▸ / Outputs ▸ ▸ Parameters (checkable, Last tweaked); active parameter ports drawn on the node; a Surface knob cabled to any of them (§2) |
| "and map them to knobs/etc. after that" | §1.4 | Surface → parameter worked in the engine, invisible in the editor | the value reaches the parameter every block, plugins only on change (§2.2) |

---

## 1. What FL's Patcher does

### 1.1 The node menu [DECOMP `FUN_0083bdc0`, strings at file 0x43be9c…0x43c05c; SHOT i65-3]

For a plugin node, in this order:

1. a caption separator "Module" (FL's `-` prefixed item; drawn as a plain separator in the screenshot)
2. **Rename...** (`FUN_008423e0`)
3. **Save preset as...** — only for plugin nodes (class check `PTR_PTR_007f8840`), greyed when the plugin flag `+0x733`
   is set
4. separator
5. **Disconnect** (`FUN_00842300`) — enabled when the node has cables (`FUN_0079a850`)
6. **Delete** (`FUN_008422d0`)
7. when more than one node is selected: separator, **Align ▸** (Top, Left, Bottom, Right, —, Center Vertical, Center
   Horizontal)
8. separator, **Inputs ▸** and **Outputs ▸** (each built by `FUN_00839fc0`, greyed when empty)
9. caption "Options", **Allow threaded processing** (check item, `FUN_00842520`), **Compatible voice levels
   (deprecated)** (generators), and **Scaling ▸** (System, Host, 100 %…400 %) for plugins that scale.

The I/O nodes ("From / To FL Studio") skip 1–3 and Delete: Disconnect, Inputs ▸ / Outputs ▸ (round 162e).

### 1.2 Inputs ▸ / Outputs ▸ [DECOMP `FUN_00839fc0`, `FUN_00839ab0`]

Each side has up to four submenus, by port class: **Audio** (class 0), **Events** (class 1, notes / MIDI),
**Parameters** (class 2, index < 0x10000000) and **Controllers** (class 2, index 0x10000000 + k: a controller's
outputs). A submenu with no port is left out. Each submenu (`FUN_00839ab0`):

- **Activate all**, **Deactivate all**, **Deactivate unused**, **Add one** (Audio only), separator;
- Parameters only: **Last tweaked** (caption "Last tweaked (`<port name>`)", ticked like that port, built from the
  node's last-tweaked index), separator;
- then every port as a **check item** (ticked = active; `+0x24`), named `"%d. %s"` (`0x7f8c20`: "1. Gain").

### 1.3 What FL saves [DECOMP REA_rodada14 §1.3.1 + DATA]

A node's port is a `5009` record: class, index, port id, flags. Flags bit 0 = input, **bit 1 = visible (activated in
the menu)**, bit 3 = a parameter / controller port. In FL's own presets: parameter inputs `(2, p, flags 3)`, controller
outputs `(2, 0x10000000 + k, flags 10)`. A cable (`1300`) between a controller output and a parameter input is the
event link. **Every activated parameter input is also published as a Patcher parameter** (record `1100`: port id →
Patcher parameter index, sparse: "8 step pattern" uses 0, 1, 2, 21…28), and the Patcher's own Surface controls are
published from 2048 on [DATA: all 309 Patcher presets scanned; e.g. "8 step pattern": 11 links Control Surface →
Fruity Love Philter / Fruity delay bank, 11 published inputs + 11 published Surface outputs].

### 1.4 Values [HYP, unchanged from REA_rodada141]

The smoothing / timing of an event value inside FL lives in each plugin node and was not traced; LYNX applies the
latest value once per block (≤ 2048 samples), as its mixer links do.

### 1.5 Not traced

- How FL draws event ports vs audio ports (colours / shapes): not located in the binary this round; no FL session was
  started (FL discipline rule: the decompile answered the menu questions). LYNX draws event ports as **diamonds** in its
  automation colour (LYNX's own look).
- The per-port right-click menu (`FUN_0083ce50`): Deactivate, Insert plugin (callback +0x948), Disconnect all, then host
  items for parameter ports (callback +0x958, not resolved in the DLL).

## 2. LYNX

### 2.1 Model (`core/Song.h`, `core/Patcher.h/.cpp`)
- `PatchNode::paramIns / paramOuts`: the activated parameter ports (sorted; LYNX's own parameter index for built-in
  effects, the plugin's for hosted plugins). A port with a cable is always shown (`shownParamInputs/Outputs`).
- A node's parameter output is a control cable source with `fromPort = patchParamOutput (p)` (0x100000 + p).
- `setParamInput (on)` also publishes the parameter as a Patcher parameter (FL's 1100), named "`<node> - <param>`";
  off removes its cables and frees the Patcher parameter (the index stays, so other automation keeps its target;
  trailing free ones are dropped). `deactivateUnusedParams`, `disconnectNode` (FL's Disconnect), `publishedIndex`.
- `removeNode` now also frees / renumbers the published parameters (before, they kept pointing at shifted nodes).
- Saved in `.lynx` (node `paramIns`, `paramOuts`; the wires keep their ports).

### 2.2 Engine (`patcher::Graph`)
- Control cables from a parameter output read the source's current value: a hosted plugin's
  (`Effect / Instrument::parameterValue`, new, crash-guarded `getValue`), a built-in effect's from the running values.
- A plugin / instrument target gets the value **when it changes** (before: `setParameter` every block). The audio thread
  allocates nothing; a new graph sends once.
- Real-time: the Surface knob → `takeValues` (lock-free swap) → next block.

### 2.3 Editor (`ui/PatcherEditor.cpp`)
- Node menu in FL's order: *Open editor* (LYNX), —, *Rename...*, *Save preset as...* (hosted plugins, LYNX's plugin
  preset formats), —, *Disconnect*, *Delete*, —, *Inputs ▸* / *Outputs ▸* (*Audio*, *Events*, *Parameters*,
  *Controllers*), —, *Bypass* (LYNX). LYNX's audio / note ports are fixed: listed ticked and greyed.
- *Parameters ▸*: Activate all, Deactivate all, Deactivate unused, *Last tweaked (N. name)*, then every parameter
  "N. name" as a check item — the running plugin's parameters, a built-in effect's, an Envelope Controller's Mod X / Y.
- "Last tweaked": the editor watches each node's values (10 Hz) and remembers the last one moved by hand (values its
  own control cables move are ignored).
- Active ports are drawn on the node (inputs left, outputs right, as diamonds); a control cable dropped on one connects
  at once; dropped on a node body it still asks for the parameter and activates that input.
- Right-click a parameter port: *Deactivate*, *Disconnect all*, and for an input the automation / link items of its
  Patcher parameter (create an automation clip, link to a controller…).
- The mixer slot / Channel Rack menu of a Patcher: *Automate Patcher parameter ▸* with its published parameters.

### 2.4 FL import (`FlPatcher.cpp`, `FlpImport.cpp`)
- `PatcherPort::flags` kept; visible class-2 parameter ports become `paramIns / paramOuts` (through
  `effectParamFromFl` for built-ins). Cables from a node's parameter output now become parameter-output cables (before,
  FL's parameter index was taken as a controller output number and the cable was dropped or wired wrongly).

## 3. Tests / checks
- `patcher171b_surface_knob_drives_a_plugin_parameter`, `…_built_in_parameter`, `…_parameter_output_feeds_another_node`,
  `patcher171b_port_editing`, `patcher171b_fl_parameter_ports_import`, `patcher171b_fl_patcher_preset_with_event_links`
  (FL's "8 step pattern" preset) **[TEST]**.
- Windows GUI (`--patchershot`, fresh `LYNX_PROFILE_DIR`, `LYNX_TEST_MODE=1`): the demo Patcher with a Surface knob on
  a parameter input and a parameter output cable, the node menu and its Parameters submenu, and the `.lynx` round trip
  (`LYNX_PATCHERSHOT_SAVE`) — screenshots in `/mnt/e/lynx-work/patcher171b/`.

## 4. Left open
- *Allow threaded processing*: LYNX runs a Patcher's nodes on one thread; the item is not shown.
- *Align ▸* (needs multi-selection in the Patcher map), *Scaling ▸*, *Compatible voice levels*.
- *Add one* for audio ports (LYNX nodes have fixed audio ports), the per-port *Insert plugin*.
- Parameter ports of built-in instruments (they have no parameter list yet) and of nested Patchers.

## Where I stopped
- done: core / engine / import / save, editor menus and pins, tests written.
- in progress: build-local + tests, Windows GUI check.
- next: notes / reply drafts, train.sh.
