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

---

## Slice 2 — "Automate this" in one step

### What LYNX had [CODE]
- Every LYNX control's right-click menu has FL's parameter menu: Automation › Edit events…, Init song with this
  position, Delete initial value, **Create automation clip**, Edit automation clip…; Remote control › Link to MIDI
  controller, Override global link…, Link to internal controller (round 149, `addAutomationItem`).
- Third-party plugin parameters: Tools › Last tweaked / the toolbar read-out (round 162d) and Add › "Automation for
  last tweaked parameter" (round 143), as in FL.
- `addAutomationClip` put the clip on the track **after the last used track**, from the song start (or the time
  selection), over the whole song.

### FL Studio [DFM] [DECOMP]
- Options › General › Miscellaneous: **"Create automation clips at the playback position"**
  (`CreateAutoClipsAtCursorBox`, `"Checked": true` in FL's form, hint "When enabled, automation clips are created at
  the playback position") [DFM, REA_rodada169k #8; STR 0x0022xxxx forms.options.generalpage.miscpanel].
- `FUN_01191a50 (param, place, …, undo)` = Create automation clip [DECOMP, string "create automation clip" @
  0x01192628]. It makes the automation clip channel ("AutoTrack.fst" template, name "Automation"), sets its range,
  then with `place`:
  - the Playlist **time selection** (playlist +0xd74 / +0xd78) gives start and end;
  - else, when the options form's check box (+0x10e0 → +0x49e, read straight from the control) is on: the **song
    position** when it lies inside the arrangement, otherwise the Playlist's left edge; when it is off: the song start;
  - end = max(start + a minimum, the arrangement end (+0xb1c));
  - the track: `FUN_00e50b50` walks the 500 tracks from a start track and takes the **first one whose span is free**
    (`FUN_01062140` checks the range) [DECOMP].
- Instrument tracks refuse automation clips: "Instrument tracks are not for audio or automation clips." [STR].
- **Checked live in FL 2026 (UI gate, round 177b)**, four "Create automation clip" from mixer faders in a new project:
  playhead 1:01 → Track 1, one bar; 5:02 → Track 2, one bar; 9:02 (past the last clip) → Track 3 at 9:02, one bar;
  3:02 → Track 4, up to the song end (10:02). So: the clip starts at the song position even past the last clip, an
  empty Playlist gives one bar, and the track is the **first one with no clips at all** — Track 1 was skipped although
  it was free over the new clip's span. The decompiled "span" check is read that way (the start track / range it is
  given cover the whole track); LYNX follows what FL does. [EXEC]


### Is this the best way? — verdict
- FL's way is already one step from any LYNX control (right-click › Create automation clip). The missing piece was
  FL's placement: at the playhead, on the first free track. Built **FL-exact**, with FL's option (on by default).
- Live / Bitwig draw automation straight onto the track's lane: that is slice 3 (optional lanes), not a change to
  FL's clips.
- Third-party plugin knobs: FL relies on Last tweaked (kept). Proposal (needs a JUCE patch, not built): VST3 plugins
  that open a host context menu for a parameter (`IComponentHandler3::createContextMenu`; JUCE hosts it but adds no
  host items) get LYNX's items at the top — Create automation clip, Link to controller. Bitwig / Cubase / Studio One
  do this; it is the real one-step for plugin knobs. → asked the coordinator.

### Built (slice 2)
| Piece | Where |
|---|---|
| `autoclipplace::forNewClip(song, atPlayback, playTick)` — FL's placement (selection › playhead › song start; to the song end, ≥ 1 bar; first free track, instrument tracks skipped) and the app setting `atPlayback()` | `core/PlaylistOps` |
| `addAutomationClip` uses it (Song mode playhead) | `ui/AutomationUi` |
| Options › General › Editing: "Create automation clips at the playback position" (on), saved with the app settings | `ui/SettingsPages`, `ui/MainComponent` |
| Test `auto177b_new_automation_clip_place` | `tests/Auto177bTests.inc` |
