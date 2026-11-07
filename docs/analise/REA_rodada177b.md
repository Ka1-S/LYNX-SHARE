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

---

## Slice 3 — optional automation lanes (design approved by the coordinator, 2026-11-06)

User decisions (through the coordinator): (a) **off by default**, FL users see no change; (b) **a clip wins over a
lane** where it plays (Bitwig 6's rule); (c) the Ableton-style layout mode (round 175a, N6) turns lanes on by
default (the flag is coordinated with 175a); "lanes follow moved clips" is phase 2 and **open**; the .flp export
writes lanes as clips on free tracks.

### Research [WEB] (full list: `/mnt/e/lynx-work/177b/research-lanes.md`)
- Praised: shape stamping and curves (Live; help.ableton.com/hc/en-us/articles/209070629), Simplify Envelope, the "A"
  automation mode that keeps the view clean (forum.ableton.com p=783744), Studio One's paint shapes and Transform box
  (soundonsound.com/techniques/paint-points), Bitwig 6's clips + track automation in one lane with the clip winning,
  pencil curves, spray can, Hold / Spread (soundonsound.com/reviews/bitwig-studio-6).
- Complaints: clutter and no global hide (Live), lanes reopening, the Re-Enable Automation state (forum.ableton.com
  p=1766337), jagged or over-smoothed recordings, automation not following moved clips; FL's clips praised exactly
  for following the arrangement (kvraudio.com p=8381379). FL users ask for optional collapsible lanes that keep clips
  (forum.image-line.com p=2051841) and complain of "a million automation clips" (r/FL_Studio, snippet).
- Live [STR, its own binary]: "Automation Lane", "Show Automation", "Draw Mode", "Re-Enable Automation"; the set
  format stores per-track `AutomationEnvelopes` plus per-clip `ClipEnvelope`s (ALS-FORMAT.md §3).

### Model and engine (built, slice 3a)
- `AutomationLane { automation, height }` in `PlaylistTrack::lanes` (+ `lanesFolded`). A lane's points are in song
  time. One lane per target and track (`autolanes::addLane` returns the existing one).
- Engine (`RackState::laneAutomations`): the lanes of unmuted tracks play their curve; the lane binds its target last
  and, at a tick where a clip of an automation with the same target plays, takes that clip's value (the clip wins);
  between clips the lane's own value plays (with clips only, FL holds the last clip value — unchanged). A lane's own
  automation never also plays as a clip. No lane = no work.
- Conversions: `clipToLane` (the clip's span, offset applied, replaces the lane there; the clip goes, its automation
  stays as FL keeps it), `laneToClip` (the lane's automation becomes a clip from 0 on the first free track),
  `lanesToClips` (the .flp export, with a report note). Project files: `playlistTrackLanes`.
- Lanes are per track, so they are shared by every arrangement (FL's automation clips are per arrangement) — noted.
- Not compensated by "Compensate automation" PDC (clips are): logged.
- Tests: `lanes177b_lane_plays_and_clips_win` (rendered levels: lane between clips, clip inside, muted track),
  `lanes177b_conversions_and_export`; selftest `lanes_saved` (project round trip).

### UI (slice 3b, built; lands after the UI gate)
| Piece | Where |
|---|---|
| Playlist options menu and the main menu's Playlist part: "Automation lanes" (off; an app setting, `automationLanes` in the UI settings; action `playlist.automationLanes` for a shortcut). `autolanes::shown()` is the flag round 175a's layout mode sets | `ui/PlaylistComponent`, `ui/MainComponent`, `core/KeyMap`, `core/PlaylistOps.h` |
| Layout: a track's lanes sit under its row (34 px each at 100 %, follow the vertical zoom); `heightOf` stays the row, so clips never draw into lanes; `trackTops` include them | `PlaylistComponent::laneBlockHeight`, `laneRow`, `refreshTrackLayout` |
| Track header: a fold arrow when the track has lanes; "+A" on hover adds a lane — the picker lists the track's own channel / mixer track volume and pan, then the two last tweaked controls, then Tempo and Master volume | `trackLaneArrow`, `trackAddLane`, `showAddLaneMenu` |
| Every control's menu: "Automate in lane" under "Create automation clip" (only while lanes are on: FL's menu is unchanged otherwise). The lane goes on `autolanes::trackFor`: the track that has its lane, else the channel's instrument track, else the audio / instrument track of its mixer track, else the first empty track | `ui/AutomationUi` `createAutomationLane`, `core/PlaylistOps` |
| Lane editing, the same in every tool: [LMB] adds a point and drags it ([Shift]: on the line), drag a point moves it ([Shift] / [Ctrl] after the click keep time / value, as in clips), [RMB] or [Alt]+click on a point deletes it, [Alt]+drag on the line bends the segment (FL's tension), [RMB] elsewhere = lane menu. The hint bar shows the value under the mouse and while dragging | `laneMouseDown`, `mouseDrag` `lanePoint` / `laneTension` |
| Where a clip of the same target plays, the lane is shaded (the clip wins) | `paintLanes` |
| Lane header: the target's name; [RMB] menu: Edit points..., To automation clip, Remove the lane, Fold the lanes, Hide all lanes; [2xLMB] opens the point editor | `paintLaneHeaders`, `showLaneMenu` |
| Clip menu (automation clips, lanes on): "Move into lane" (`clipToLane` on `trackFor`) | `buildClipMenu` |
| Test: `lanes177b_track_for_a_target` (off by default; where a lane goes) | `tests/Lanes177bTests.inc` |

Not in this slice (open): "Everything" in the "+A" picker (any control reaches a lane through its own menu instead);
lanes following moved clips (phase 2); lanes in PDC "Compensate automation".

---

## Slice 3c — FL's automation clip LFO ("LFO mode", coordinator: build it FL-exact)

Full REA: `/mnt/e/lynx-work/177b/lfo/FL-AUTOMATION-LFO.md` (headless Ghidra on `FLEngine_x64.dll`, 1,781 0xEA events of
1,300+ local .flp files).

### FL Studio [DECOMP] [DATA] [DFM]
- LFO mode is **not** a loop of the clip's points: every automation clip (`TACArticulator`, VMT 0x00FE2580) carries a
  main envelope (its points) and a `TMLFO` with its own points (the LFO's amplitude over the clip's time) and FL's
  usual LFO knobs. "LFO mode" = the LFO's enabled LongBool; the editor then edits the LFO's points (FUN_00f6b6a0).
- .flp: everything is in event 0xEA. The "112-byte trailer" LYNX wrote as a constant is the main envelope's tail
  (flags, enabled, 3 loop points, A/D/S/R knobs 128/128/0/128) plus the LFO stream (kind 5, version 3, M points,
  flags, enabled, 3 loop points, speed, tension, skew, pulse width, phase): `112 + 24·(M − 1)` bytes. Header +4 = LFO
  level (−128…128, 64), +8 = mode (0 multiply, 1 add). Writer FUN_00fe8640 → FUN_00b9fd20 / FUN_00b97c40 /
  FUN_00ba01a0. Every local file parses with 0 bytes left.
- Evaluation: `out = base + env·range`; LFO on: add `out += L·level/128·range`, multiply `out *= 1 + 2·L·level/128·range`
  (FUN_00ba1700, constants 2.0 / 1.0 read from the binary). `L = amp(t)·shape(phase)` (FUN_00ba0710); the phase is a
  u32 per cycle starting at the middle of the rising ramp + PHASE (FUN_00ba06a0); cycles per beat =
  `(10001^(speed/65536) − 1)/625` (FUN_00ba03d0; 45819 = 1 beat, 40898 = 2, 26257 = 16 [DATA]); the waveform
  (FUN_00677660/820) is a triangle bent by tension `T = sign·((1e18)^((21^(|t|/128) − 1)·0.05) − 1)`, skewed, with
  pulse-width flat zones. Flags: 1 tempo, 2 global phase, 4 unipolar tension ("Bipolar LFO tension" off), 8 frozen
  (FL sets 2 with it).
- UI: channel settings "LFO" panel (LFO box, speed / tension / skew / pulse width / level wheels, "Multiply" box) and the
  automation editor's " LFO" button; menu "Bipolar LFO tension", "Frozen LFO". Delete: the LFO keeps ≥ 1 point.

### Ableton Live 12 / users
- Live has no LFO inside an automation envelope; it is a device (LFO Max device, Shaper) mapped to a parameter — the
  modulators slice (4). Bitwig: modulators per device. FL users praise the clip LFO for quick wobbles without a plugin
  and complain that it is hidden in the channel settings (r/FL_Studio threads on "automation clip LFO", snippet level).

### Is this the best way? — verdict
FL-exact: the same model (points + LFO), the same math, the same file bytes, and FL 2026's editor layout (checked live
at the UI gate): the "Automation editor | LFO" row above the grid; in LFO mode the small SPD / TNS / SKEW / PW / AMNT
knobs, MUL and the options arrow bottom-left under the grid; the wave that plays is the main line. The options menu
has FL's two flags only (Bipolar LFO tension, Frozen LFO). PHASE and the global-phase flag have no control in FL: LYNX
keeps them as passthrough values (read, played, written back).
- Possible option (not built, for the user): draw the LFO's amplitude points as the bold line with the played wave thin
  behind (LYNX's first draft). It shows the amplitude envelope better, but it is not FL's look; FL's stays the default.

### Built
| Piece | Where |
|---|---|
| `AutomationLfo` (on, level, add, flags, speed, tension, skew, pulse width, phase, range, amplitude points; FL's unused fields + raw point records kept), `shape`, `cyclesPerBeat`, `speedForBeats`, `apply`; `Automation::playAt` | `core/Song` |
| Playback: clips (and the value between clips), Performance-mode clips, `Song::automationValueAt` use `playAt`; nothing computed while the LFO is off | `core/RackRenderer`, `core/Song.cpp` |
| .flp read: the whole 0xEA layout; write: from the fields (an untouched clip writes FL's 112 bytes exactly as before), the LFO point records byte for byte while unchanged | `flp/Flp`, `flp/FlpWrite` |
| Import / export through the song (the clip's range kept: the level is scaled by it on export) | `core/FlpImport`, `core/FlpExport` |
| Project files: `lfo` / `lfoFl` per automation | `engine/ProjectFile` |
| Editor (FL 2026's layout): "Automation editor \| LFO" row (the LFO toggle edits the LFO's points while on); bottom-left SPD (shown in beats), TNS, SKEW, PW, AMNT knobs, MUL, options (Bipolar LFO tension, Frozen LFO); the played wave is the main line, the amplitude points small on a thin line; Playlist clips draw the played curve | `ui/AutomationUi`, `ui/PlaylistComponent` |
| Tests: `lfo177b_untouched_clip_writes_fls_tail`, `lfo177b_flp_round_trip_is_byte_exact` (FL's Astes clip: 136-byte trailer, unknown bytes kept), `lfo177b_rate_and_shape_match_fl` | `tests/Lfo177bTests.inc` |

Export: LYNX always writes a clip at the whole range, so a clip with a partial range gets `level × range` in the header (the played amount is the same; header byte +4 and the points differ from FL's file). Byte-exact holds for whole-range clips. Tests: `lfo177b_flp_round_trip_is_byte_exact`, `lfo177b_export_scales_the_level_by_a_partial_range`.

Open: FL's knobs 0x600–0x604 are automatable channel parameters (not automatable in LYNX yet); FL's rate cap per tick
(805306368) is not applied (it is above the fastest speed at FL's PPQ); the playlist call site and FL's final clamp
were not traced (LYNX clamps to 0…1); "Global / retriggered LFO" handler not traced (LYNX: phase from the song start);
the LFO under a non-linear target map (an imported effect parameter) is applied in LYNX's value space.

## Slice 4 — modulators (proposal, then built as P1 on the user's go for 0.1.25)

### What LYNX and FL have [CODE] [STR]
- FL-exact controllers: Envelope Controller (channel), Keyboard Controller, Peak Controller and Formula Controller
  (mixer slots), X-Y / X-Y-Z Controller, Voltage Controller; FL's "Link to controller" maps an output through a curve
  to one target (`Automation::source` + `curve`, live). Each link is absolute: it replaces the knob's value.
- Channel LFOs (pan, volume, cutoff, pitch) per sampler channel.
- **FL-exact gap found:** FL's automation clips have an **LFO mode** ("The selection contains at least one
  automation clip in LFO mode." [STR]; the clip's channel settings LFO: amount, speed, tension, shape, tempo sync).
  LYNX's automation clips have no LFO mode → sent to the coordinator.
- User complaints about FL's modulation [WEB]: "the biggest pain point" — Peak Controller + Formula routing is
  complicated, the LFO moves the knob's base value, Peak Controller is unipolar, step/bar rates need formulas
  (forum.image-line.com p=2074232, Sep 2026); Peak Controller needs an extra mixer track (admiralbumblebee 2018).
- Bitwig (praised as best): click a modulator's routing button, valid targets light up, drag on a knob to set a
  bipolar depth; the depth shows as a ring on the knob; modulation is relative to the knob's value; modulators work
  on VST parameters too; an inspector lists routings (bitwig.com/en/bitwig-studio/devices/modulators.html).
  Live 12: LFO "Mod" (relative) vs "Remote" (absolute).

### Proposal
1. **Model**: `Song::modulators` — `Modulator { kind (LFO, random / S&H, envelope follower, note envelope, steps),
   rate (Hz or tempo-synced in steps / beats / bars), shape, phase, retrigger (song / note / free), source insert (for
   the follower), routings: [{ AutomationTarget, depth −1..1 }] }`. Relative and bipolar: the knob keeps its value; the
   played value = clamp(base + depth · m(t)). FL's "Link to controller" stays as it is (absolute, FL-exact).
2. **Engine**: evaluated per block inside `RackRenderer` next to live automation (the same target tables: a modulated
   target reads base + offset), no allocation, no lock; a modulator without routings is skipped; nothing exists
   until the first modulator is made. Per-voice modulation only for LYNX's own synths, later.
3. **UI**: a "Modulators" strip in the channel settings and the mixer insert (folded when empty). Each modulator has
   a handle; drag it onto any LYNX knob (`ValueControl`) to route it. The knob shows a depth ring; Alt-drag the ring
   changes the depth; the knob menu gets "Modulation › <source> (depth) / Remove". Plugin parameters: the parameter
   list / Last tweaked menu "Modulate with…" (and the VST3 host-menu idea from slice 2).
4. **Reuse**: the envelope follower is the Peak Controller's engine without a mixer slot; note envelopes are the
   Envelope Controller's; steps are the X-Y / Formula patterns. FL's controllers stay for FL projects.
5. **.flp export**: FL has no modulators — a modulated target is rendered to an automation clip (lossy; reported), as
   lanes are.
6. **Phases**: P1 LFO + random with rings on LYNX knobs; P2 envelope follower from any insert (no extra track); P3 note
   envelopes and steps; P4 per-voice for LYNX synths. Needs a user decision on the order, and whether FL's automation
   clip LFO mode (the FL-exact gap) comes first.

### Built (P1; the depth ring on the knobs waits for the UI gate)
| Piece | Where |
|---|---|
| `Modulator` (shape: sine, triangle, saw up / down, square, random, smooth random; synced in beats or free in Hz; phase; unipolar; seed; routes `{target, depth −1..1}`), `Song::modulators`; `canModulate` = LYNX's own controls (not the tempo, not plugin parameters) | `core/Song` |
| Engine: each route gets its own slot bound after everything (it wins) and plays `clamp (base + depth·m(t))`, base = the automation that drove the target, else the control's value; synced modulators follow the song position (live clock when stopped), the random shapes are a hash of the cycle (the same at the same position); no modulator = nothing bound, nothing computed; no allocation or lock | `core/RackRenderer` `modRoutes`, `updateModulators` |
| Project files (`modulators`), .flp export: dropped with a report line (FL has none) | `engine/ProjectFile`, `core/FlpExport` |
| UI: every LYNX control's menu gets "Modulate" after FL's items (each modulator, ticked with its depth when routed; New modulator (LFO); Modulators...); Tools › "Modulators..." window: name, shape, Sync + cycle length or Hz, phase, 0..1, a scope of one cycle, the routes with a depth knob and remove | `ui/AutomationUi`, `ui/MainComponent` |
| Tests: `mod177b_shapes`, `mod177b_modulates_a_target_around_its_value` (rendered levels: up / down around the value, around an automation, depth 0 and no modulator = nothing bound); selftest `lanes_saved` round-trips modulators | `tests/Mod177bTests.inc`, `Main.cpp` |

Open (phases, as proposed): the depth ring and drag-to-route on knobs (P1b, after the UI gate), plugin parameters
(their value lives in the plugin: needs a base read from the plugin), envelope follower (P2), note envelopes / steps
(P3), per-voice (P4).
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

**Not checked:** the report's "removing the last clip does not bring the first back". In the engine the first automation takes the slot again (`issue97_one_target_shares_a_slot`). Not run in the GUI with sforzando.
