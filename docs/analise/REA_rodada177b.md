# Round 177b — sidechain and automation (user-approved area, 2026-11-06)

Brief: `/mnt/e/lynx-work/feat177/brief.md`. Research input: `/mnt/e/lynx-work/ableton/research-web-2.md` (rows #6
and #8, §2.3, §2.5, §3 items 2, 9, 28, 29) and `/mnt/e/lynx-work/ableton/STUDY.md`. FL REA: FL Studio 2026
`FLEngine_x64.dll` (sha256 6d9c5ab0…), headless Ghidra project `~/ghproj/r177b` (copy of r176a's analysed project);
raw outputs in `/mnt/e/lynx-work/177b/rea/`.

Evidence tags: **[STR]** string in the binary, **[DFM]** form resource, **[DECOMP]** decompiled function, **[CODE]**
LYNX source, **[WEB]** user reports (URLs).

Order of work (brief): slice 1 one-click sidechain → slice 2 "automate this" in one step → slice 3 optional automation
lanes (design sent to the coordinator first) → slice 4 modulators (proposal only).

---

## Slice 1 — one-click sidechain

### What LYNX had before this round [CODE]
- FL's way, complete: the route arrow under an insert (Shift+click = sidechain, Ctrl+Shift = sidechain only), its
  right-click menu "Sidechain to this track" / "… only", and the mixer track menu's Routing › "Sidechain selected to
  this track" / "Sidechain this track to selected" (`mixerops::sidechainTo`, `routeSelectedTo`, round 165f-mixer).
  A sidechain link is a level-0 send; the compressors / limiters / vocoder on the target listen to it (round 170h:
  `linkAddedSidechain` also links a dynamics effect added later).
- A per-slot "Sidechain from" submenu in the mixer slot menu — but only for Compressor, Fruity Limiter, Fruity
  Compressor and Facet EQ, and it only set `EffectSlot::sidechain`: **no route was made**, so the source could be
  processed after the target (the renderer reads the source buffer of the current pass) and nothing showed the link
  in the routing.
- Facet EQ and Crest/Apex editors had their own source menus, with the same missing route.
- **Third-party plugins never heard a sidechain**: `PluginEffect::process` enabled every bus (`preparePlugin` →
  `enableAllBuses`) but cleared the buffer and only filled channels 0–1, so a plugin's side-input bus (FabFilter Pro-C,
  any VST3 compressor) always got silence.

### FL Studio [STR] [DECOMP]
- Mixer route menu items "Sidechain to this track", "Sidechain to this track only", "Sidechain selected to this
  track", "Sidechain this track to selected" (+ "only") [STR FLEngine], menus `SideChainMenu` / `SideChainOnlyMenu`
  [STR]. The link is a send at level 0 (REA_rodada165f-mixer: FUN_012ac520 / FUN_012ac950).
- FL's own effects pick which of the incoming sidechain sends they listen to with a number: Fruity Limiter
  "SIDECHAIN" / "SIDECHAIN INPUT" [STR], Vocodex "Modulator sidechain input number" / "Carrier sidechain input number"
  [STR]; stored in the plugin state (REA round 48).
- Third-party plugins: the wrapper's input port page. `FUN_011f3c00` builds the port list; when no port is set it
  shows "No input port set (click to auto assign)" [STR 0x011f42bc, DECOMP]; ports print as `Input port %d TO "%s"`
  [STR]. So a VST sidechain in FL = route arrow → "Sidechain to this track" → open the plugin → wrapper → processing
  page → assign the input port. This is the multi-step chain users complain about.

### What producers say [WEB]
- r/FL_Studio "Dear FL developers, stop hiding the sidechain" (~412 upvotes, snippet only):
  https://redlib.astrial.org/r/FL_Studio/comments/1jecq08/dear_fl_developers_stop_hiding_the_sidechain
- IL forum May 2026: a 5-step sidechain setup counted: https://forum.image-line.com/viewtopic.php?p=2061097
- KVR: "convoluted" sidechain routing: https://www.kvraudio.com/forum/viewtopic.php?p=8685126
- Complaint that "route to this track only" pulls the source off the Master (same IL thread).
- Live: every device with a sidechain has its own "Audio From" chooser in the device's sidechain section (Live
  manual, Compressor › Sidechain); Bitwig: a sidechain selector in the device header. Both are praised for being
  discoverable; neither needs a routing step.

### Is this the best way? — verdict
- **Keep FL's way exactly** (arrow, Shift / Ctrl+Shift, menus): untouched.
- **Add a picker where the effect is**: the effect's own header and the mixer's selected-effect panel show
  `SC: None ▾` / `SC: 1 Kick ▾`; picking a source makes FL's link itself (a level-0 send when there is no route;
  an existing route keeps its level; the source keeps its Master send and its other sends). A source that would
  make a loop is greyed out "(loop)". This is Live's "Audio From" placed in FL's routing model, so the mixer still
  shows the link as FL's sidechain cable and `soleSidechainSender` / FL's own effects see it.
- **Third-party plugins hear it**: a plugin with a second input bus gets the slot's sidechain there (plain and fixed-
  size buffers). Its wrapper header gets the same picker. FL needs the port page; LYNX needs one pick.

### Built (slice 1)
| Piece | Where |
|---|---|
| `mixerops::acceptsSidechain(kind)` — every engine that reads a side input (Compressor, Fruity Limiter / Compressor, vocoder, Vocodex, Facet EQ, Apex Limiter, Hush de-esser, Crest, Prism, Sentry, LuxeVerb, Patcher, Transmitter, Transporter) | `core/MixerOps` |
| `mixerops::sidechainFrom(song, to, slot, from, second)` — the picker's edit: level-0 send when no route, the slot listens; None clears the slot only; loop → nothing; the Master needs no route | `core/MixerOps` |
| Plugin side bus: `PluginEffect::setSidechain` feeds the second input bus (channel index from the plugin's layout), silence when the slot has none; fixed-size buffers carry it as channels 2–3 | `engine/PluginHost` |
| Picker UI helpers: `takesSidechain`, `sidechainSourceText`, `addSidechainPicker`, `showSidechainPicker` (hint: "1 Kick → 3 Bass · the source still plays through its own outputs") | `ui/PluginHeader` |
| Header chip on every built-in effect window with a side input; "Sidechain from" in its options menu | `ui/PluginEditors` |
| Plugin wrapper header button for plugins with a sidechain bus | `ui/PluginWindow` |
| Mixer: `SC: … ▾` button on the selected effect's title row (built-ins) / under "Open plugin interface" (plugins); the slot menu's "Sidechain from" now for every capable effect and plugins, through `sidechainFrom` | `ui/MixerComponent` |
| Facet EQ / Crest / Apex own source menus now make the route too | `ui/FacetEqEditor`, `ui/ProEditors` |
| Tests: `sc177b_sidechain_picker_makes_the_route`, `sc177b_accepts_sidechain_matches_the_engines`; selftest `plugin_sidechain` (a stand-in plugin that plays its side bus, plain + fixed blocks, and silence after the link is removed) | `tests/Sc177bTests.inc`, `Main.cpp`, `engine/PluginHost.cpp` |

Cost when unused: none. The plugin path copies the side input only when the slot has one (pointer set per block by
the renderer, cleared after use); no allocation on the audio thread (the fixed-block scratch is sized at creation).
