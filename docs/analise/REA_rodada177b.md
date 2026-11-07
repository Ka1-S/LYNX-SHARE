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

## Issue #97 — several automation clips on one target

**Report** (Disar, 0.1.24): a second automation clip on a plugin's CC silences the first clip.

**Cause in LYNX [CODE]:** `RackState::build` bound one automation per target, "the last automation of the same target wins", and an automation with any clip produced a value at every tick (it held its first or last point outside its clips). So the highest-numbered automation drove the target for the whole song. This applied to every target kind: channel and mixer volume and pan, effect knobs, plugin parameters, MIDI CC, tempo. Plugin and CC bindings were made per automation, so both wrote every block and the last write stayed. `AutomationDisplay` and `Song::tempoAt` repeated the same rule.

**FL's rule [EXEC]:** FL 2026 command-line renders (`FL64 /R /Ewav`, no input injected) of generated projects: a 440 Hz tone on channel 0, automation channels A (ch 1) and B (ch 2) both linked to channel 0's volume, 120 BPM, 1 bar = 2 s. Generator, projects and renders: `/mnt/e/lynx-work/177b/issue97/` (`py/mk97.py`, `mk97r.py`, `mk97s.py`, `flr/fl/`). Ramps: A 0.1→0.3, B 0.5→0.7, so the level names the clip.

| Case | Clips (bars) | What FL plays |
|---|---|---|
| ra / rb | A 4–6, B 8–10 (and swapped) | own value before the first clip; each clip; the last clip's end value holds between and after; channel order does not matter |
| rc / rd | A 4–8, B 6–10 (and swapped) | the later start takes over at its start and plays to its end |
| re / rf | one clip 4–10, the other 6–8 inside it, on an upper track | the inner clip, then the outer one resumes at its own position |
| rg / rh / ri | same span, tracks swapped, file order swapped | A, the lower channel, after a short B blip at the start in rg and rh |
| rj | the same automation twice, 4–8 and 6–10 | the later clip restarts the curve |

**Rule built:** where clips of the target play, the one that started last (same start: the lowest automation); between clips, the end value of the clip that ended last; before the first clip, the control's own value. `RackState` gives the automations of a target one slot (`automationGroups`, `automationFollowers`); `AutomationDisplay` and `Song::targetValueAt` (tempo) use the same rule. Tests: `tests/Issue97Tests.inc`.

**Behaviour change beyond the report:** before the first clip LYNX used to play the clip's first point. It now keeps the control's own value, as in ra and rb. REA_rodada83 already recorded this rule for pattern events.

**Other target kinds [EXEC]:** the Master fader (`y_mv_ra`, `y_mv_rc`, `y_mv_re`) and a plugin parameter, Emphasizer's input gain on the Master, slot 9, id `0x8000` (`pa`–`pk`, `py/mk97p2.py`), follow the same main rule: own value before the first clip, the later start wins, a clip still playing resumes, the end value holds. Renders in `/mnt/e/lynx-work/177b/issue97/out/`, launched with `render180d/flrun.ps1` (not activated). In these renders the slot marker 0x62 comes after the slot's plugin events, so the plugin before `0x62 = 9` is slot 9. A MIDI CC target itself was not measured.

**Known differences (FL's evaluator not traced):** REA was time-boxed. It reached the clip's range setup (`FUN_00fe9b90`), but `TACArticulator.Compute` (FUN_00ba1700) has only virtual-table references and the per-tick loop was not found. LYNX keeps the simple rule in these cases:
- `rk_three` and `pk` (A 4–6, B 5–8, A 6–9): FL plays A's second clip for t = 12–14 s, then B until B ends at 16 s, then A. The rule predicts A from 12 s to 18 s. FL does the same on channel volume and on the plugin parameter, so this is not noise.
- Overlapping flat clips (`c`, `g` in `py/mk97.py`): the earlier clip comes back about a second after the later one starts (c at about 13.1 s, g at about 13.7 s).
- The same start: on channel volume A, the lower channel, wins whatever the track or file order (`rg`, `rh`, `ri`). On the plugin parameter, `pg` stays on B and `ph` plays B for 4 s and then A. LYNX uses the lowest automation on every target.

**Checked in the GUI (Windows work build of f402eaa15, 2026-11-07):** the reporter's flow on a VST3 instrument. sforzando is not installed here, so the instrument was a CC-logging test VST3 that maps every CC to a parameter and logs what it receives (`/mnt/e/lynx-work/177b/gui/probe/`). The steps: Channel rack › right-click the instrument › Automate MIDI controller › Common controllers › CC #1 › Create automation clip, then draw a line; a second clip the same way with a different line. Both clips start at the song start. Logged while playing: the first clip's curve only (both start together: one plays). After removing the second clip, the first clip's curve plays. After undo and then removing the first clip, the second clip's curve plays (peak 0.92 at beat 4). With the second clip moved to bar 6, both curves play in turn, and the CC holds the first clip's end value between them. Log: `/mnt/e/lynx-work/177b/issue97/gui-cc-prof.log`. Not checked: sforzando itself.
