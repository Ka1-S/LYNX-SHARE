# FL Studio's workflow: a clean-room study for LYNX

Last updated: 2026-10-14. Status: study / design input. No code was changed while it was written; the gap matrix
and the P1 list were later updated as items landed (P1-2, 3, 9, 10, 11, 12, 13 on `feature/wf-editors`; P1-1, 4, 5, 6, 7,
8, 14, 15 on `feature/wf-shell`; the P2 items and the P1 leftovers on `feature/wf-p2`, see the P2 status notes).

LYNX interprets FL Studio's *workflow*: the way a producer moves between the Channel Rack, Piano Roll,
Playlist and Mixer. It keeps its own look, names and code. This document has four parts:

1. what that workflow is,
2. the interactions producers rely on, each tied to the official manual page that describes it,
3. where LYNX stands today (with file references) and a prioritised plan,
4. what must stay different for clean-room and branding reasons.

## Sources and rules

- **[DOC]**: the official Image-Line FL Studio online manual. Each `[DOC]` tag is a link to the page (and
  anchor) that describes the behaviour. All descriptions here are **paraphrased**; nothing is quoted. The pages
  were read in October 2026 through a summarising web reader, so exact key combinations are worth re-checking on
  the live page before they are built. Where two manual pages disagree, both readings are given and marked
  **CONFLICT**.
- **[DATA]**: FL Studio 2026's installed data files under `C:\Program Files\Image-Line\FL Studio 2026\`
  (folder names, plain-text presets). Only names and structure are reported. No content was copied.
- **LYNX**: the LYNX source (`src/ui/*`, `src/core/*`, `src/engine/*`) and docs (`docs/STATUS.md`,
  `docs/ROADMAP.md`). File references are `file:line` under `src/` as of this date.
- **Not documented** means the manual pages read do not say. Nothing in this document guesses at FL's
  behaviour beyond what a source states.
- No decompilation was used for this study. The REA rounds in `docs/analise/` cover the engine and file format,
  not the UI.

---

## 1. The core workflow model

### 1.1 Patterns are the building block; the Playlist arranges them
- A **pattern** holds notes, and optionally event automation, for *any number of channels*. The step sequencer
  and the Piano Roll are two editors over the same pattern data. [DOC][wf] [DOC][plp]
- The **Playlist** arranges clips in time. There are three kinds: pattern clips, audio clips and automation
  clips. A placed pattern becomes a *pattern clip* that shows the pattern's name. [DOC][plp]
- **Playlist tracks are generic lanes**: any clip type can go on any track, clips may overlap, and tracks are
  not bound to an instrument or a mixer insert. Mixer routing comes from the channels, so one pattern clip can
  feed many inserts. [DOC][pl]
- An optional per-track mode binds one channel, one Playlist track and one mixer insert together ("instrument" or
  "audio" track). "Unassigned" returns the track to free mode. [DOC][pl-trackmode]
- **Arrangements** are alternative Playlist layouts. They share the same Channel Rack and Mixer, so an edit there
  affects every arrangement. [DOC][pl]
- Documented four-step flow: load instruments and write patterns → arrange clips in the Playlist → route
  channels to Mixer inserts and add effects → export. The Playlist only plays in Song mode. [DOC][wf]

### 1.2 The Channel Rack is the hub
- Every sound source is a **channel** in the rack: an instrument, a sampler, or a holder for an audio clip or
  automation clip. [DOC][cr] [DOC][ac]
- Each row has, from left to right: an on/off LED (solo through modifiers), a pan knob and a volume knob (both
  sent to the instrument, not the mixer), an optional **mixer insert number**, the **channel button** (its name),
  a **selection indicator**, and either step buttons or a mini piano-roll preview. [DOC][cr]
- Selection matters: the selected channels are what copy/paste acts on, and they receive live MIDI input.
  [DOC][cr]
- Clicking the channel button opens that channel's settings and plugin window. [DOC][cr]

### 1.3 Step sequencer and Piano Roll
- Each step is a 1/16 note. Once a channel has non-step notes, a read-only mini piano roll replaces its step row.
  [DOC][cr-steps]
- Steps are a view of the Piano Roll. Notes stay "step-compatible" while they are zero-length and on the grid.
  The manual describes how to turn piano-roll notes back into steps: select all, snap off, discard lengths,
  quantise. [DOC][cr-steps]
- The **graph editor** gives each step a value in lanes for note, velocity, release, fine pitch, Mod X/Y, shift,
  pan and repeat. [DOC][cr-graph]
- The Piano Roll edits **one target channel of the current pattern** at a time. Notes of other channels show as
  **ghost notes**. [DOC][pr]

### 1.4 Mixer inserts and routing
- 500 inserts plus Master, and a "Current" insert that carries the selected insert's output for analysis tools.
  New channels go to Master unless assigned elsewhere. [DOC][mx]
- Every insert sends to Master by default. Sends to other inserts are made by selecting the source insert and
  clicking a send switch on the destination. A send at level 0 is a **sidechain-only** link. [DOC][mx]
- Each insert has 10 effect slots, processed top to bottom, each with a wet/dry mix and an enable switch.
  [DOC][mxp]

### 1.5 The Browser feeds everything by drag-and-drop
- One tree of samples, presets, plugins and project contents. Items load by being dropped on a target: the rack
  makes a channel, the Playlist makes an audio clip, a mixer insert or slot takes an effect, and an instrument
  preset dropped on an insert also sets up routing. [DOC][br] [DOC][br-use]
- Clicking an audio file previews it. [DOC][br]

### 1.6 Plugin windows
- Every plugin, native or third-party, sits in a common **wrapper** window. Its header holds the plugin menu,
  preset browsing arrows, minimise and close. [DOC][wrap]
- Clicking a channel button opens or closes its window. By default, opening another channel's window closes the
  previous one; Alt+click keeps several open. [DOC][wrap] [DOC][cr]

### 1.7 Pattern mode vs Song mode
- One transport, two scopes: **PAT** loops the current pattern and **SONG** plays the Playlist. The L key toggles
  between them. [DOC][tb] [DOC][sc]
- Right-clicking PAT opens the Channel Rack; right-clicking SONG opens the Playlist. The song-position slider
  follows whichever scope is active. [DOC][tb]

### 1.8 Recording
- Arm Record, then press Play. A **recording filter** chooses what is captured: automation, notes, audio, or
  clips from the live-launch mode. [DOC][tb-rec]
- Notes go into the current pattern. In Song mode a pattern clip is also placed where playback started.
  [DOC][rn]
- Audio records per mixer insert, by choosing an input on the insert. In Song mode the take becomes a Playlist
  audio clip. [DOC][ra] [DOC][ra-pl]
- Related toggles: count-in, metronome, wait-for-input, blend (overdub), loop record and step edit. [DOC][tb]

### 1.9 Automation clips
- An automation clip is a channel-like source that draws a curve in the Playlist. [DOC][ac]
- To create one, right-click a native control and choose "create automation clip". Other routes:
  - "last tweaked" for third-party parameters,
  - converting event data from the Piano Roll or event editor,
  - volume or pan automation for an audio clip. [DOC][ac] [DOC][pl-clipmenu]
- The clip remembers the control's value at creation as an **initial value**. The control returns to it at the
  start of the song. [DOC][ac]

### 1.10 "Everything is a window"
- The Playlist, Channel Rack, Piano Roll, Mixer, Channel Settings, Event editor and plugins are all **child
  windows on one desktop**. They can be moved, resized, overlapped and maximised, and they snap to each other
  (Alt stops the snapping). [DOC][iface]
- Any of them can be made **Detached**, which turns it into a separate OS window for a second monitor. [DOC][iface]
  [DOC][wrap]
- Window keys:
  - F5 Playlist, F6 Channel Rack, F7 Piano Roll, F9 Mixer, Alt+F8 Browser, F8 plugin picker.
  - F12 closes all windows, Alt+F12 closes plugin windows, Ctrl+F12 closes the unfocused ones.
  - A layout can be "realigned" to its default, saved and restored, and off-screen windows can be brought back.
  [DOC][view]
- **CONFLICT**: one page labels F6 "Step Sequencer"; the View menu calls it "Channel Rack". [DOC][sc] [DOC][view]

### 1.11 Toolbar, transport, hint bar
- A configurable toolbar of panels: menus, transport, PAT/SONG, tempo, time display, global snap, pattern
  selector, recording toggles, CPU/memory, shortcut icons for each window, and the hint bar. Its layout comes
  from presets. [DOC][tb]
- [DATA] `System/Config/Toolbar presets/*.tpr` are plain-text toolbar layouts, one panel per entry with a width
  and a row. There are presets for older versions and for a single-line toolbar.
- The **hint bar** describes the control under the mouse and shows its value while it is being changed. Small
  icons mark whether the control can be automated, can be linked to MIDI, or has a right-click menu. [DOC][tb]

### 1.12 Undo history
- Edits are undoable, and there is a history list (in the Browser, under the current project). Clicking an entry
  jumps to that state, newest at the bottom. Settings include a maximum number of undo levels and whether knob
  tweaks are recorded. [DOC][edit] [DOC][envgen] [DOC][br]
- **CONFLICT** on Ctrl+Z:
  - One page says Ctrl+Z toggles between undoing and redoing the last edit, and Ctrl+Alt+Z steps back.
    [DOC][sc]
  - The Edit menu says Ctrl+Alt+Z steps back, Ctrl+Z steps forward, and Ctrl+Z toggles only at the newest step.
    [DOC][edit]
  - An "alternate undo mode" makes Ctrl+Z step backwards. [DOC][envgen]

### 1.13 Templates, autosave
- File → New uses the last chosen template. File → New from template lists templates by category, and users can
  save their own. The startup project can be empty, the default template or the last project. [DOC][file]
  [DOC][envgen]
- [DATA] `Data/Templates/` holds each template as a project file plus a small `.nfo` with a one-line description
  and a menu icon index. Categories on disk:
  - Advanced (Electronic / Instrumental genre starters), Empty, Minimal (basic kit, with or without a master
    limiter, delay/reverb sends), Other, Performance, Power User, and Utility (chord progression, drum mix,
    record audio, vocoder, surround …).
  - The manual names different categories (Genre / Minimal / Other / Utility). [DOC][file]
- Autosave has several frequency levels. The most frequent ones also save before risky operations. Backups go to
  a backup folder, and "revert to last backup" restores one. [DOC][envfiles] [DOC][file]
- [DATA] `System/Config/NamePresets_{Channel,Pattern,MixerTrack,MixerSlot,PlaylistTrack}.txt` are lists of
  suggested names with a colour and icon index, shown in rename dialogs.
- [DATA] `System/Config/Typing to piano/*.scr` map PC keys to notes. Their notes say that unmapped keys stay
  free for shortcuts and "locked" keys do nothing. The default map puts four rows of the keyboard on four octaves.

---

## 2. Interactions producers rely on

Each interaction has an ID, which section 3 uses for the gap matrix.

### 2.1 Global (G)

| ID | Interaction (paraphrased) | Source |
|---|---|---|
| G1 | F5/F6/F7/F9 show the Playlist / Channel Rack / Piano Roll / Mixer; Alt+F8 the Browser; F8 a quick plugin picker | [DOC][view] |
| G2 | F12 closes all windows, Alt+F12 closes plugin windows, Ctrl+F12 closes unfocused windows; Esc closes the focused window; Tab cycles windows | [DOC][view] [DOC][sc] |
| G3 | Windows are child windows on one desktop: free move/resize/overlap, snapping (Alt disables), each can be **Detached** to its own OS window | [DOC][iface] |
| G4 | Window layouts: realign to default, bring back off-screen windows, layout presets, save/restore a custom layout | [DOC][view] |
| G5 | Enter toggles the Playlist between maximised and restored | [DOC][sc] |
| G6 | Space play/stop; Ctrl+Space play/pause; Play pressed again stops and rewinds; double-click Stop = panic; right-click Stop = "remember seek time" | [DOC][sc] [DOC][tb] |
| G7 | L toggles PAT/SONG; right-click PAT/SONG opens the matching window | [DOC][sc] [DOC][tb] |
| G8 | Tempo: drag to change; fine digits; right-click for type-in, tap tempo, nudge, presets, half speed, automation | [DOC][tb] |
| G9 | **Global snap** in the toolbar, used by every editor whose own snap is set to "Main"; it also sets record quantise; Backspace toggles it; Alt while dragging bypasses snap | [DOC][tb-snap] [DOC][sc] |
| G10 | Right-click on snap sets the default note length | [DOC][tb-snap] |
| G11 | Pattern selector: +/− (numpad) next/previous, numpad 1–9 picks a pattern, right-click lists all; F4 next empty pattern; F2 rename/colour; Alt+C clone; Shift+Ctrl+Ins insert; Del delete; "split by channel" | [DOC][tb] [DOC][pat-menu] [DOC][sc] |
| G12 | Hint bar: hover description + live value + capability icons | [DOC][tb] |
| G13 | Undo/redo with a clickable history list; maximum levels; knob tweaks optionally undoable | [DOC][edit] [DOC][envgen] |
| G14 | Ctrl+N saves a *new version* (MyProj_2…), not a new project; Ctrl+S/Ctrl+Shift+S save/save-as; Ctrl+R export WAV; Alt+0..9 recent files | [DOC][sc] [DOC][file] |
| G15 | Templates by category; save as template; default startup choice | [DOC][file] [DOC][envgen] |
| G16 | Typing keyboard as piano (Ctrl+T); while on, single-key shortcuts are disabled | [DOC][sc] [DATA] |
| G17 | Toolbar shortcut icons for each window; right-clicking the Piano Roll/Playlist icons opens them maximised | [DOC][tb] |
| G18 | Recent-change "last tweaked" tools: create automation or link from the last moved control | [DOC][tools] [DOC][ac] |

### 2.2 Common control behaviour (K)

| ID | Interaction | Source |
|---|---|---|
| K1 | Knob/slider: drag up/down; **Ctrl+drag fine**; Shift while dragging disables value snapping | [DOC][iface] |
| K2 | **Alt+click or middle-click resets** to default | [DOC][iface] |
| K3 | Right-click a control: type in value, larger control, link to controller, create automation clip, "init song with this position" | [DOC][iface] [DOC][ac] |
| K4 | Number displays: drag to change, hover top/bottom edge to step by one, click-and-hold to type | [DOC][iface] |
| K5 | Multi-link: link several controls to controllers in the order they were tweaked | [DOC][link] |
| K6 | Mouse wheel and double-click on knobs | not documented ([DOC][iface]) |
| K7 | Tickable menu items can be right-clicked to try them without closing the menu | [DOC][iface] |

### 2.3 Channel Rack and step sequencer (C)

| ID | Interaction | Source |
|---|---|---|
| C1 | Step: left-click turns on, right-click turns off; an option makes left-click toggle | [DOC][cr-steps] [DOC][cr] |
| C2 | Drag-painting across steps; Shift/Ctrl/Alt on steps | not documented ([DOC][cr]) |
| C3 | "Mute removed steps" option: a removed step keeps its data, muted | [DOC][cr-opt] |
| C4 | Graph editor (Ctrl+K): per-step lanes; drag a column, Ctrl scales all, right-drag draws a ramp, Alt+click resets | [DOC][cr-graph] |
| C5 | Channel button: left-click opens/closes settings + plugin; Ctrl+click auditions without opening; Alt+click keeps other windows open; Shift+wheel moves the channel; right-click = channel menu | [DOC][cr] [DOC][wrap] |
| C6 | Channel menu: piano roll, graph editor, rename/colour/icon (random colour key in the dialog), load sample, insert, replace, **clone** (settings without notes), delete, cut/copy/paste note data, **fill every 2/4/8 steps**, advanced fill (incl. Euclidean), **rotate left/right**, burn MIDI to pattern | [DOC][cr-btn] [DOC][fill] |
| C7 | Selection: click a selector; right-click toggles independently; right-drag multi-selects; double-click selects all; Up/Down select neighbour; **Alt+Up/Down moves** selected channels; sort by colour/name/insert/type; "select unused" | [DOC][cr] [DOC][sc] |
| C8 | Mute LED: left-click mute; right-click solo menu; Ctrl+click solo; Shift+click locks state | [DOC][cr] |
| C9 | Number keys 1–0 mute channels 1–10, Ctrl+number solos | [DOC][sc] |
| C10 | Mixer insert field (Alt+M shows it): drag vertically; **Ctrl+L assigns selected channels to free inserts** (named after the channel); click "TRACK" in channel settings = next free insert; double-click opens the mixer there | [DOC][cr] [DOC][csm] |
| C11 | Ctrl+C/X/V copy step/score data between channels and patterns via the selection | [DOC][sc] [DOC][cr] |
| C12 | Shift+Ctrl+Left/Right shifts note data by one snap unit | [DOC][sc] |
| C13 | Filter groups: show only a group's channels; Alt+G adds the selection to a group; PgUp/PgDn switch groups; new channels join the current group | [DOC][cr-filter] |
| C14 | Global swing knob; per-channel swing mix multiplies it; "set swing mix for selected" | [DOC][cr] [DOC][csx] [DOC][cr-swing] |
| C15 | Pattern length: auto (to the last bar with data) or set; drag the rack corner to show more steps; 1–512 steps | [DOC][cr] |
| C16 | Shift+wheel on the rack title changes pattern | [DOC][cr] [DOC][sc] |
| C17 | Layer channel: forwards notes to children (set from the selection); random / crossfade / round-robin options | [DOC][lay] |
| C18 | Clone channels (Alt+C), delete (Alt+Del), zip/unzip (Alt+Z/U) | [DOC][sc] |
| C19 | Per-channel cut groups / "cut itself" | [DOC][csx] |
| C20 | Loop ghost steps: per-channel looping shows ghost steps; "burn to pattern" makes them real | [DOC][cr] |

### 2.4 Piano Roll (R)

| ID | Interaction | Source |
|---|---|---|
| R1 | **Tools**: Draw (P), Paint (B), Paint-drums (N), Delete (D), Mute (T), Slice (C), Select (E), Zoom (Z), Playback/scrub (Y); right-click-hold a tool icon + wheel switches tools | [DOC][pr] |
| R2 | Draw: left-click adds, drag moves, **right-click deletes** (right-drag deletes many); right-click-hold on empty grid = temporary slice | [DOC][pr] |
| R3 | Paint: drag lays a run of notes; Shift = monophonic paint (replaces overlaps) | [DOC][pr] |
| R4 | New notes use the **last-clicked note's length**; right-click snap sets the default length | [DOC][pr] [DOC][tb-snap] |
| R5 | Resize from the right edge in snap steps (Alt = free); left-edge resize is an option (Ctrl+Alt+Home); stretch a selection with a handle or Right-Shift+edge | [DOC][pr] [DOC][prm] |
| R6 | Selection: Ctrl+drag box; Ctrl+Shift adds; Ctrl-click empty deselects; Ctrl+click/drag on the ruler selects a time range, on the keyboard a pitch range | [DOC][pr] |
| R7 | Move locks: click a note then Shift+drag = time only; Ctrl+drag = pitch only | [DOC][pr] |
| R8 | Shift+left-click clones the selection | [DOC][sc-pr] |
| R9 | Double-click a note → **Note properties** (pan, velocity, release, Mod X/Y, slide, porta, colour, time, duration) applied to the whole selection | [DOC][pr] [DOC][sc-pr] |
| R10 | Event lane: velocity/pan/release/Mod X/Y/fine pitch or controller automation; Shift+drag sets crossed notes to one value; Alt+click resets; Alt+wheel over a note edits; "turn into automation clip" | [DOC][pr-ev] [DOC][prm] |
| R11 | Snap: Main / Line / Cell / none / step & beat fractions / bar / **Events** (snap to existing and ghost notes) / markers | [DOC][prm-snap] |
| R12 | **Ghost notes**: other channels in the pattern drawn solid grey, other clips' notes hollow; Alt+V toggles; Ctrl+Alt+V makes them editable; **double right-click a ghost switches to that channel** | [DOC][pr] [DOC][sc-pr] |
| R13 | Target channel selector in the title bar (channels with notes are ticked); wheel steps through channels with notes, Ctrl+wheel through all; G/K previous/next channel, H/J previous/next with notes | [DOC][pr] [DOC][sc-pr] |
| R14 | Scale: snap-to-scale toggle and root/scale menu; shading of out-of-scale rows; snapping of incoming MIDI; key markers | [DOC][pr-key] |
| R15 | Chord stamp: a whole shape per click, following snap and scale; "only one" mode; top-down / bottom-up modes | [DOC][pr-stamp] |
| R16 | Colour groups (16): draw in a colour, Alt+C recolours, Shift+C selects a colour; groups behave as separate mono/slide voices | [DOC][pr-col] |
| R17 | Slide (S) and porta (O) note toggles | [DOC][pr] |
| R18 | Keys: Ctrl+X/C/V (paste at the left of the view), **Ctrl+B duplicate right**, Del, Shift+Left/Right move by snap, Alt+arrows nudge, Shift+Up/Down semitone, Ctrl+Up/Down octave, Shift+Ctrl+Left/Right rotate within the bar | [DOC][prm-edit] [DOC][sc-pr] |
| R19 | Selection keys: Ctrl+A / Ctrl+D, Shift+I invert, Shift+R/M random select, Shift+O select off-grid notes; Ctrl+Enter select the span around the selection | [DOC][sc-pr] |
| R20 | Space editing: Ctrl+Ins insert space, Ctrl+Del delete space, Ctrl+Alt+Ins slice + insert | [DOC][sc-pr] |
| R21 | Alt+M / Alt+Shift+M mute / unmute the selection; Shift+G / Alt+G group / ungroup notes | [DOC][sc-pr] |
| R22 | **Quick tools** (no dialog): quick quantise (CONFLICT: printed as both Ctrl+Q and a Ctrl+Alt variant), Shift+Q starts-only quantise, **Ctrl+U quick chop**, **Ctrl+G glue**, **Ctrl+L quick legato**, Shift+D discard lengths | [DOC][prm-tools] [DOC][prm] |
| R23 | **Tool dialogs** (Accept/Reset, undoable): Arpeggiate (Alt+A), Strum (Alt+S), Chop (Alt+U), Quantise (Alt+Q), Articulate (Alt+L), Flip (Alt+Y), Randomise (Alt+R), Limit (Alt+K), Scale levels (Alt+X), Flam (Alt+F), slice-by-period tool (Alt+W), LFO into the event lane (Alt+O), melody generator (Alt+E), chord progression (Alt+P) | [DOC][arp] [DOC][strum] [DOC][chop] [DOC][qnt] [DOC][art] [DOC][flip] [DOC][rnd] [DOC][lim] [DOC][scl] [DOC][flam] [DOC][claw] [DOC][lfo] [DOC][riff] [DOC][chord] |
| R24 | Zoom/scroll: PgUp/PgDn zoom, Shift+1..5 zoom presets / all / selection, Shift+0 centre on the playhead; middle-drag pans both axes; middle-drag on the keyboard zooms vertically | [DOC][sc-pr] |
| R25 | Preview: clicking the keyboard auditions (not during playback unless an option is on); the playback tool scrubs | [DOC][pr] [DOC][prm] |
| R26 | Whether drawing or moving a note auditions it | not documented ([DOC][pr]) |
| R27 | Time markers in the roll (Alt+T named, Ctrl+T auto); per-pattern time signature (Shift+Alt+T) | [DOC][sc-pr] [DOC][pr] |
| R28 | MIDI file import/export from the roll (Ctrl+M / Shift+Ctrl+M) | [DOC][sc-pr] |
| R29 | Play button in the roll: right-click returns to the last start / roll start | [DOC][pr] |
| R30 | Piano roll scripts: Tools › Scripts lists Python `.pyscript` files (factory, user, downloaded); a script edits the notes and markers through `flpianoroll`, with an automatic form and live preview; Run last script again Ctrl+Alt+Y | [DOC] Piano roll script reference, manual pianoroll_scripting_api; REA_rodada190as-pyscript.md |

### 2.5 Playlist (P)

| ID | Interaction | Source |
|---|---|---|
| P1 | Tools: Draw (P), Paint (B), Delete (D), Mute (T), **Slip (S)**, Ripple (Shift+S), Slice (C), Select (E), Zoom (CONFLICT: Z vs Shift+Z), Playback (Y) | [DOC][pl] [DOC][sc-pl] |
| P2 | In any tool: right-click = delete, double right-click = mute, double-click = clip properties, middle-drag = pan, Ctrl = select, Shift = paint/copy, Alt = no snap | [DOC][sc-pl] [DOC][pl] |
| P3 | **Clip source selector**: left-click on empty track space places the current source; right-click (or Ctrl+F8) opens the project picker with hover preview | [DOC][pl] [DOC][pl-pp] |
| P4 | Picker panel (Alt+P): every pattern/audio/automation source; rename, colour, select unused, sort, preview | [DOC][pl-picker] |
| P5 | Clip edges: Shift+drag the start/end edge to trim without moving content; left-edge resize option; slip moves content inside a fixed clip; ripple resizes while the content at the other end slips | [DOC][pl] [DOC][pl-work] |
| P6 | Move by left-drag; Shift+drag copies the selection; Ctrl+drag box selects; Shift+Left/Right move the selection (all clips when nothing is selected) by one snap unit, Shift+Up/Down by one track; Alt+Left/Right (also with Shift) nudge it by one pixel's worth of ticks, ignoring snap; one undo step per key press | [DOC][pl-work] [DOC][sc-pl]; Alt nudge, undo and limits: FLEngine 2026 `FUN_00e51730` / `FUN_00e52360` + FL 2026 live check (round 190ag) |
| P7 | Wheel nudge (CONFLICT: Shift+Alt+wheel by default, Shift+wheel with an option) | [DOC][pl] [DOC][sc-pl] |
| P8 | Clip menu: preview, select source, channel settings, rename, colour, **make unique** (also Shift+V+drag), select all similar, delete, chop (grid-based; for audio by bar/beat, also shuffled) | [DOC][pl-clipmenu] |
| P9 | Audio clip menu extras: volume/pan automation for the clip, crossfades, time-stretch tools, "make unique as sample" (copies the file) | [DOC][pl-clipmenu] |
| P10 | Double-click a pattern clip opens the Piano Roll or the Channel Rack per the "edit patterns in" setting; Alt+double-click opens the other one | [DOC][pl] [DOC][pl-editin] |
| P11 | Track header menu: rename/colour/icon, auto-name, track mode, size/lock, group with the track above, merge pattern clips, consolidate, mute all clips, insert/delete/clone/move | [DOC][pl-trackmenu] |
| P12 | Track mute/solo; grouped tracks mute together; Shift+wheel over headers reorders tracks | [DOC][pl] [DOC][sc-pl] |
| P13 | Ruler: click = song position; **Ctrl+drag = time selection across all tracks** (it loops, and clip parts outside it are shown as not playing); Shift+drag moves/copies it; double-click clears | [DOC][pl-time] [DOC][pl-markers] |
| P14 | Space editing on the time selection: Ctrl+Ins insert space, Ctrl+Del delete space; Insert slices at the cursor | [DOC][pl] [DOC][sc-pl] |
| P15 | **Ctrl+B duplicates** the selection to the right; with a time selection, that sets the repeat interval | [DOC][pl-menu] [DOC][sc-pl] |
| P16 | Markers: named/auto markers, loop/skip/pause/start/stop-recording actions, song loop marker sets the render end; jump to previous/next marker | [DOC][pl-markers] |
| P17 | Zoom: Ctrl+wheel around the mouse; drag the scrollbar ends; PgUp/PgDn; Shift+1..5, Shift+0; Ctrl+right-click zooms to the selection / everything | [DOC][pl] [DOC][sc-pl] |
| P18 | Track height for all tracks; drag a divider for one | [DOC][pl] |
| P19 | Merge pattern clips (Ctrl+G), group/ungroup clips (Shift+G / Alt+G), Alt+M mute selection, quantise (CONFLICT: Ctrl+Q vs Shift+Q; Alt+Q full) | [DOC][pl] [DOC][sc-pl] |
| P20 | Arrangements: dropdown in the title bar; add, clone, rename, delete, merge one into another | [DOC][pl] |
| P21 | Clip focus selector: chooses which clip type is on top for picking stacked clips | [DOC][pl-focus] |
| P22 | Audio clip keys: transpose (7/8), reverse (9), stretch mode, fades view | [DOC][sc-pl] |
| P23 | Dropping a plugin/audio file on a track header creates a linked instrument/audio track | [DOC][pl-trackmode] |
| P24 | Live clip launching ("performance" mode, Ctrl+P in the Playlist) | [DOC][perf] |
| P25 | Audio clips show a detailed waveform at every zoom level; the sample's waveform is also drawn in the Sampler channel settings and while previewing in the Browser | [DOC][ac] [DOC][csm] [DOC][br] |

### 2.6 Automation clips (A)

| ID | Interaction | Source |
|---|---|---|
| A1 | Right-click a control → create automation clip. The length is the Playlist time selection, or the whole song without one. The clip goes on the highest empty track in view | [DOC][ac] |
| A2 | Points: right-drag adds; Shift+right-click adds without changing level; left-drag moves (Shift locks vertical, Ctrl horizontal, Alt no snap); right-click point menu: delete, type in value, copy/paste value | [DOC][ac] |
| A3 | Tension handle per segment (Ctrl fine, right-click resets); curve types per segment (single, double, hold, stairs, pulse, wave, smooth …) | [DOC][ac] |
| A4 | Ctrl+drag selects points; dragging the last point stretches the clip; step mode draws freehand; slide mode keeps later points' spacing | [DOC][ac] |
| A5 | LFO overlay on the envelope (speed, tension, skew, pulse width, level; multiply vs add) | [DOC][ac] |
| A6 | Partial | Right-click on the curve: **scale the values into a range**, flip upside down, all segments straight. No playback speed |
| A7 | Initial value stored at creation; "init song with this position" | [DOC][ac] |
| A8 | Make unique on an automation clip; tempo automation from the tempo display (60–180 BPM default) | [DOC][ac] [DOC][tb] |

### 2.7 Mixer (M)

| ID | Interaction | Source |
|---|---|---|
| M1 | Select an insert by clicking its fader/pan; multi-select with Ctrl+Shift+click or drag over labels; selected faders move together | [DOC][mx] |
| M2 | Reorder inserts: Alt+Left/Right or Shift+wheel (CONFLICT: the menu page says Shift+Left/Right) | [DOC][mx] [DOC][mxm] |
| M3 | Send switch on each insert: select a source, click the destination's switch → a send knob appears; right-click → route or sidechain to this track only | [DOC][mx] |
| M4 | Right-click an insert: route/sidechain *selected to this* or *this to selected* (each with an "only" variant); create submix; reset routing | [DOC][mxm] |
| M5 | **Link selected channels to this insert** (Ctrl+L), "…starting from this insert" (Ctrl+Shift+L); Alt+L selects the channels linked to it | [DOC][mxm] [DOC][sc] |
| M6 | Mute: left-click; Solo: right-click or Ctrl+click; Ctrl+right-click unmutes all; Alt+click solos along the signal path; Shift+click locks | [DOC][mx] |
| M7 | Rename/colour: F2, middle-click the label, or the menu; random colour in the dialog | [DOC][mx] [DOC][mxm] |
| M8 | Effect slot: left-click selects, right-click the arrow → plugin picker; click the name → its window; **wheel over a slot reorders it**; mix knob; enable switch; slot menu (replace, presets, A/B "spare state", move up/down, rename) | [DOC][mx] [DOC][mxp] |
| M9 | FX switch per insert: left-click toggles all effects, right-click opens/closes all effect windows | [DOC][mx] |
| M10 | Insert record arm (left-click; right-click with file naming); input selector arms the insert; pickup point | [DOC][mx] [DOC][ra] [DOC][mxt] |
| M11 | Docks (left/middle/right), view sizes, routing cables view, separators, groups | [DOC][mx] [DOC][mxm] |
| M12 | Track properties panel (Ctrl+Enter): 3-band EQ (left-drag freq/gain, right-drag width/gain), delay comp with wheel/Ctrl+wheel | [DOC][mxt] |
| M13 | Save/load an insert's state (chain + settings); drag a saved state onto another insert | [DOC][mxm] |
| M14 | Add inserts with "+"; trim unused inserts | [DOC][mx] [DOC][mxm] |
| M15 | Clicking above/below the fader handle moves it smoothly there | [DOC][mx] |

### 2.8 Browser (B)

| ID | Interaction | Source |
|---|---|---|
| B1 | Click audio previews the first seconds; Alt+click (or an option) plays it fully; Shift+click from the mouse position | [DOC][br] |
| B2 | Shift+Up/Down steps through files, previewing each, and **swaps each one into the selected channel** | [DOC][br] [DOC][sc] |
| B3 | Enter / middle-click / "send to selected channel" loads the item into the selected channel | [DOC][br] [DOC][br-use] |
| B4 | Drop targets: rack (new channel), Playlist (audio clip), mixer insert or slot (effect), mixer insert (instrument + routing), track header (linked track); Alt while dropping keeps the window closed | [DOC][br] [DOC][br-use] [DOC][mx] |
| B5 | Dropping on an existing channel / on the Piano Roll | not documented ([DOC][br]) |
| B6 | Search (Ctrl+F): wildcards, AND/OR/NOT, exact phrase, current folder only, F2/F3 previous/next result | [DOC][br] |
| B7 | Tabs (all, current project, plugin database, starred), user folders, layouts (tree, columns, samples with waveforms, plugin grid), tags, favourites (star) | [DOC][br] |
| B8 | Current-project tab: history, patterns, automatable controls (right-click → automation / link) | [DOC][br] |
| B9 | Quick plugin picker (F8): generators on one side, effects on the other; type to filter; double-click loads into a new channel or the selected insert | [DOC][br-picker] |
| B10 | Docks left/right; can detach; Backspace/Shift+Backspace navigate back/forward | [DOC][br] |
| B11 | Sounds tab (FL Cloud, online): a sound's menu › **Find similar samples** lists sounds of the online library with a similar tone and rhythm; Loop Starter's channel menu has the same query. Nothing local is analysed | [DOC][br] `REA_rodada190as.md` §1 |

### 2.9 Plugin windows (W)

| ID | Interaction | Source |
|---|---|---|
| W1 | Common header: plugin menu, preset arrows (previous/next), right-click preset name = plugin's own preset list, minimise, close | [DOC][wrap] |
| W2 | Channel button toggles the window; another channel closes it unless Alt+click | [DOC][wrap] [DOC][cr] |
| W3 | Menu: replace, delete, presets (save/browse/randomise), A/B spare state, parameter list for linking, processing options, rename/colour, **Detached** | [DOC][wrap] |
| W4 | Settings tab: MIDI ports, release velocity, pitch-bend range, notes-off on stop, processing options | [DOC][wrap] |
| W5 | Shift+F12 aligns plugin windows; Alt+F12 closes them all | [DOC][view] |
| W6 | A "remember window position" option | not documented ([DOC][wrap]) |

### 2.10 Recording (X)

| ID | Interaction | Source |
|---|---|---|
| X1 | R arms; Play records; right-click Record = filter (automation / notes / audio / clips) + record quantise | [DOC][tb-rec] |
| X2 | Count-in (bars set by right-click), metronome (Ctrl+M), wait-for-input (Ctrl+I), blend/overdub, loop record | [DOC][tb] [DOC][sc] |
| X3 | Notes go to the current pattern; in Song mode a pattern clip is placed at the start point (an empty clip placed first decides the track) | [DOC][rn] |
| X4 | Recorded MIDI is quantised by global snap unless it is set to line/cell/none | [DOC][rn] [DOC][rn-q] |
| X5 | Automation recording: moving controls while recording writes event data to the pattern; it can be converted to a clip later | [DOC][rau] |
| X6 | Audio: per-insert input; Song-mode takes become Playlist clips (each loop pass a separate clip, earlier takes audible only with blend on) | [DOC][ra] [DOC][ra-pl] [DOC][ra-loop] |
| X7 | Step edit (Ctrl+E): each key adds a note at the cursor and advances one step; numpad keys skip or go back | [DOC][rs] |
| X8 | Always-on capture of recent playing that can be dumped into a pattern afterwards | [DOC][rn] |
| X9 | Cancel a take; recording start/stop markers limit the recorded region | [DOC][rn] [DOC][pl-markers] |

---

## 3. Gap analysis

Legend: **Yes** = present and close to the documented behaviour · **Partial** = present with a different
gesture or reduced scope · **No** = absent (searched, not found). Locations are under `src/`.

### 3.1 Gap matrix

**Global**

| ID | LYNX | Where / note |
|---|---|---|
| G1 | **Done** | F5 Playlist, F6 Channel Rack, F7 Piano Roll, F9 Mixer, Alt+F8 Browser **toggle and focus** floating panels (`ui/Workspace.*`, `core/Workspace.cpp` `Layout::toggle`). **F8 opens the Plugin picker** (`ui/PluginPickerView.*`, round 171c); the quick-add palette (`ui/QuickAddPalette.*`, P2) has no default key since then: `Edit › Quick-add palette...`, or a right-click on the toolbar's plugin-picker button. Keys checked on [DOC][sc] |
| G2 | No | No F12 family; Esc only deselects inside the editors |
| G3 | **Done** (shell + P2) | Workspace desktop: panels move by their title strip, resize by their edges, overlap, maximise (button or double-click on the title) and **detach** into a native window and back. Snapping to other panels and the edges (`workspace::snap`); Alt while dragging turns it off, as on [DOC][iface]. **Detached windows snap too** (to the screen, the main window and each other; `SnapConstrainer` in `ui/Workspace.cpp`). Plugin, settings and automation windows stay native windows |
| G4 | **Done** | View → Reset layout, Save layout as…, Load layout (`%APPDATA%\LYNX\layouts\*.txt`), Bring back off-screen panels. The current layout is saved in the user settings (`workspace.txt`), not the project |
| G5 | **Done** | Enter maximises/restores the Playlist (key map `panel.maximisePlaylist`) |
| G6 | **Done** (except remember-seek) | Space and the Play button: play, or stop and go back to where playback started; Ctrl+Space pauses; Stop returns to the start; **double-click Stop = all notes off** (`AudioEngine::panic`, a channel −1 choke in `RackRenderer`). Right-click Stop ("remember seek time") not done. Keys checked on [DOC][sc] and [DOC][tb] |
| G7 | **Done** | L toggles; right-click Pattern shows the Channel Rack, right-click Song shows the Playlist ([DOC][tb]; that page is itself inconsistent, LYNX follows the per-button reading) |
| G8 | **Done** (LYNX variant) | Tempo display (`MainComponent::TempoDisplay`): drag the whole number (1 BPM per 4 px) or the decimals (0.01 BPM/px), Ctrl finer, wheel, double-click types, Alt/middle-click resets. Right-click: type value, **tap tempo** (call-out pad, from the 2nd tap, `controls::TapTempo`), **half / double** (permanent, not FL's temporary half speed), reset, automation clip, MIDI link. No nudge |
| G9 | Yes | Global grid combo in the main toolbar (`ui/EditorTools.h` `GlobalGridBox`, `EditorGlobals`); each editor's grid combo starts on "Follow global grid". Backspace in the Piano Roll / Playlist toggles it with "No snap". Alt bypass: Yes. **Record quantise follows it** (fixed fractions and bars; none/screen/nearest record as played; Options → Recording turns it off) |
| G10 | Yes | Right-click the global grid → "Default note length" (or "Last clicked note", the default) |
| G11 | **Done** | Numpad +/− next/previous, **numpad 1–9 select patterns 1–9**, F4 next empty pattern (then the naming dialog), F2 rename + colour dialog (F2 inside it = random palette colour), Ctrl+Shift+Ins insert before the current one, **Alt+C clone** (the rack keeps Alt+C for channels while it has focus), **Shift+Ctrl+Up/Down move**, "Split by channel" in the pattern menu and the **Patterns** menu (`core/RackOps.cpp`). Keys checked on [DOC][pat-menu] and [DOC][sc]. Del is not bound (it deletes notes/clips in the editors); Delete pattern stays in the menu |
| G12 | **Done** | **Hint bar** under the menu, as wide as the toolbar allows (360–620 px; one line, or two smaller lines for long hints): describes the control under the mouse (`ui::HintSource`, hint property or tooltip), shows its **value live while it is dragged** (the pressed control stays the source for the whole drag, its value in an accent pill), and marks "can be automated" and "has a right-click menu" with LYNX icons. Hint texts use a small markup drawn as **mouse-button and key glyphs** (`core/HintMarkup.h`: `[LMB] [2xLMB] [RMB] [MMB] [Wheel]`, any other `[Key]` as a key cap). Panels can **post a live hint** for a gesture in progress (`ui::postHint`, `ui/HintBar.h`). Every main-toolbar control has a markup hint. MIDI learn prompt kept. [DOC][tb] |
| G13 | **Done** (LYNX variant) | Snapshot undo with 100 steps, one step per mouse gesture; Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z. **Undo history panel** (Ctrl+Alt+H since round 170h2 — Ctrl+H is FL's Stop sound — Edit menu; `ui/HistoryPanel.*`): steps named after what changed (`core/History.h` compares the snapshots), newest at the bottom, a click jumps there (`AudioEngine::jumpToHistory`). No undo-level or knob-tweak settings |
| G14 | Partial | Ctrl+N new, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+R export, recent-files menu (`MainComponent.cpp:250-358, 833-957`). No save-new-version |
| G15 | Partial | 4 built-in templates in "New from template" (`core/Templates.cpp:57`); no user templates |
| G16 | **Done** | Ctrl+T and a toolbar toggle (off by default, so the editors' letter tools work; the manual does not state a default); saved in the user settings and passed to `EditorGlobals::typingPiano`. Every binding lives in `core/KeyMap` (remappable via `%APPDATA%\LYNX\keys.json`, Options → Edit keyboard shortcuts). While the piano is on its keys win; other plain printable keys only work for actions marked `whilePiano` (L); chords, F-keys and Space/Enter/arrows always work |
| G17 | **Done** | Toolbar icons for the five panels (click toggles, right-click shows the panel maximised); undo/redo icons: right-click opens the undo history |
| G19 | **Done** (LYNX variant) | Toolbar meters and master controls ([DOC][tb], `ui/Toolbar.*`): **song position** slider under the time display (click/drag seeks, snapped to steps, Alt free; wheel = one bar; Playlist in Song mode, the pattern in Pattern mode); **time display** switches bar:beat:step / min:sec:cs on click; **master volume** = the Master insert's fader (automatable, so it also affects renders, unlike FL's monitor volume); **master pitch** ±1200 cents saved with the project (`Song::masterPitch`, applied to the built-in instruments' notes in `RackRenderer`; semitone steps, Shift free; plugins are not retuned); **CPU / RAM / polyphony** (polyphony = notes sounding in the song plus held PC keys, `core/SongStats.h`; voices still releasing are not counted); **output** oscilloscope with a peak meter that turns red after clipping |
| G18 | No | No "last tweaked" |

**Controls**

| ID | LYNX | Where / note |
|---|---|---|
| K1 | **Done** | One contract (`ui/ValueControl.*`, math in `core/ValueMath.h`): drag, **Ctrl+drag fine** (switchable mid-drag), wheel (Ctrl finer) on rack knobs and route box, mixer faders/pans, effect knobs and wet/dry, tempo, swing and every plain `juce::Slider` (global contract). No value snapping, so no Shift override. **UI-C (2026-10-15):** a floating value read-out follows every custom-painted knob, fader, send knob, graph lane and effect knob while it is dragged, wheeled or reset (`ui::showValueTip`/`flashValueTip`), and plain sliders without a text box get it from the global contract; hovered knobs light their body/arc |
| K2 | **Done** | Alt+click or middle-click resets; double-click still resets |
| K3 | **Done** (most) | One menu: Type value…, Reset, Create automation clip, Link to MIDI controller, Unlink (wet/dry included). No "init song with this position" or larger-control view |
| K4 | Partial | The tempo display is a draggable number; other number displays (route box) use the knob contract. No edge-hover stepping |
| K5 | No | — |
| K7 | No | — |

**Channel Rack**

| ID | LYNX | Where / note |
|---|---|---|
| C1 | Yes | Left toggles and paints by drag, right erases (`ChannelRackComponent.cpp:315-328, 391-412`) |
| C2 | Yes (own) | Drag-paint/erase is a LYNX design choice (FL: not documented) |
| C3 | No | — |
| C4 | **Done** (LYNX lanes) | Graph lane under the selected channel (Ctrl+K, channel menu → Steps): velocity, pan, release, fine pitch, Mod X, Mod Y; drag sets, right-drag draws a ramp, Alt+click resets; it writes the same note fields as the Piano Roll (`core/NoteTools.h` levels). No "note" or "shift" lanes, no Ctrl scaling |
| C5 | **Done** | Click opens/closes the channel's editor (plugin window or settings), closing the previous one unless Alt is held or Options → Keep channel editors open is on; Ctrl+click auditions; Shift+wheel on the name/selector moves the channel; double-click opens the Piano Roll. Built-in drums and audio clips have no editor (the click only selects). Checked on [DOC][cr]. **UI-C:** the editor now opens on release, so dragging the channel button up/down **reorders the selected channels** (LYNX gesture; FL uses Shift+wheel/Alt+Up/Down, both kept) |
| C6 | **Done** (most) | Menu has piano roll, instrument replace, rename **and colour dialog**, colour, clear notes, arpeggiator, sampler slicing, clone, insert channel here, move up/down, send to free inserts, delete selected, "Channel settings…", and **Steps → fill every 2/4/8, Euclidean fill… (pulses / length / rotation with a ring preview), rotate left/right, graph lanes**. Missing: copy/paste note data, burn MIDI |
| C7 | **Done** (LYNX gestures) | Selector column: click selects, Ctrl+click or right-click toggles, Shift+click range, double-click all; Up/Down, Shift+Up/Down; **Alt+Up/Down moves**. The selection drives live MIDI (up to 16 extra channels) and the PC keyboard. **Sort channels by name / colour / insert / type and Select unused channels** in the channel menu (`channelSortOrder`, `unusedChannels`). **UI-C:** **left-drag over the selectors selects a range, right-drag adds the crossed rows** ([DOC][cr]: "right-drag multi-selects") |
| C8 | Partial | Left mute; right-click or Ctrl+click solo (`:267-283`). No lock |
| C9 | **Done** | 1..0 mute, Ctrl+1..0 solo channels 1–10 while the rack has focus and the typing piano is off |
| C10 | **Done** (most) | Ctrl+L (and menus) send the selection to free inserts, naming and colouring each insert after its channel (`sendChannelsToFreeInserts`). Route box follows the control contract. No Alt+M toggle. **UI-C:** double-click on the route box shows the Mixer at that insert ([DOC][cr]); "Next free insert" moved to its right-click menu |
| C11 | No | No step-data clipboard in the rack |
| C12 | Partial | Ctrl+Shift+Left/Right in the rack **rotates the selected channels' steps** by one step (wrapping in the pattern); the Piano Roll's Ctrl+Shift+arrows rotate within the bar |
| C13 | No | — |
| C14 | **Done** (most) | Global swing knob in the toolbar (`MainComponent.cpp:121-128`); per-channel swing mix exists in the model and is imported, but there is no UI for it. **UI-C:** channel menu → Steps → **Swing mix (n%)…** sets the per-channel swing mix for the selected channels ([DOC][csx]) |
| C21 | **Done** (round 90) | Rows whose notes are not plain steps (other key, longer than a step, off the grid, slides) show a **miniature of the notes** instead of the step buttons; a click opens the Piano Roll, drag-painting skips them (`showsNotePreview` in `core/RackOps.h`, LYNX's own rule). Name buttons are tinted with the channel colour; the fitted rack shows at most 32 steps and scrolls beyond |
| C15 | Partial | Length via the pattern menu (Automatic, 1–32 bars); the grid follows the length (`ChannelRackComponent.cpp:89-99`). No corner drag |
| C16 | **Done** | Shift+wheel over the Channel Rack panel title changes the pattern (the title shows the pattern's name) |
| C17 | No | — |
| C18 | Partial | Alt+C clones the selected channels (settings, not notes), Alt+Del deletes them. No zip/unzip |
| C19 | Yes | Cut groups, mono (STATUS.md, channel settings) |
| C20 | No | — |

**Piano Roll**

| ID | LYNX | Where / note |
|---|---|---|
| R1 | Yes | Tool strip: Pencil, Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen (`ui/EditorTools.h`, `ui/PianoRollComponent.cpp`). Letter keys P/B/D/T/C/E/Z/Y work while the typing piano is off (`EditorGlobals::typingPiano`, wired by P1-14). **The wheel over the strip steps through the tools** (LYNX's take on right-click-hold + wheel). **Each tool and target has its own cursor** (pencil, brush, eraser, knife, zoom, speaker icons; four-way move, copy, left-right resize, crosshair select) decided by the same rules as the click (`core/EditorGestures.h` `leftAction`/`cursorFor`, unit-tested), and it follows Ctrl/Shift/Alt without moving the mouse. No drum-paint mode |
| R2 | Yes | Pencil keeps the old gestures; right-click/right-drag deletes in every tool; right-press-and-hold on empty grid then drag = temporary Knife |
| R3 | Yes | Brush lays notes (or the chord stamp) at the default length while dragging right; Shift = monophonic (replaces overlapping notes of the channel) |
| R4 | Yes | The resized length becomes the next default (`:938-954`) |
| R5 | Partial | **Both edges resize by default** (LYNX choice; FL makes the left edge an option — Ctrl+Alt+Home / … menu turns it off here). Edge zones scale with the note (a quarter of its width, at most 6 px; no left edge under 16 px) so short notes stay movable; the hovered edge shows a grip. Alt = free. No stretch handle |
| R6 | **Done** (most) | Ctrl+drag box; Shift+drag adds; Ctrl+click toggles one note; **Ctrl+drag on the ruler selects a time range, Ctrl+click/drag on the keyboard selects a key / key range** (Shift adds) |
| R7 | Yes | After the click, pressing Shift locks the pitch (time only) and Ctrl locks the time (pitch only); Shift held at the click still drags a copy |
| R8 | Partial | Shift+drag on a note drags a copy (`:785-805`) |
| R9 | Yes | Double-click a note → Note inspector call-out (velocity, pan, release, fine pitch, Mod X/Y, slide, porta; time and length for one note) applied to the selection with Apply / Revert, one undo step. Release and Mod X/Y are stored but not yet used by the instruments; no colour group |
| R10 | Partial | Lane with velocity / pan / fine pitch: drag draws, **right-drag draws a straight line** (as the rack graph lane, C4), **Shift+drag sets every crossed note to the press value, Alt+click resets, Alt+wheel over a note (grid or lane) changes it, Ctrl+Alt+wheel in fine steps** (the selection when the note is selected; a wheel burst is one undo step); the hovered stem is lit. No release/Mod X/Y lanes, no controller automation lane, no "turn into automation clip" |
| R11 | Partial | Follow global / none / Screen grid (zoom-dependent) / Nearest note (edges of notes and visible ghosts) / step and beat fractions / bar. No Line-vs-Cell distinction, no markers |
| R12 | Partial | Ghost notes of the pattern's other channels, toggle button and Alt+V; double right-click a ghost switches to its channel (the hovered ghost lights up and the hint names its channel). No hollow ghosts from overlapping Playlist clips (needs "opened from clip" context), no editable ghosts |
| R13 | Yes | Channel selector in the roll's toolbar (● = has notes in this pattern); wheel steps through channels with notes, Ctrl+wheel through all; G/K previous/next channel and H/J previous/next with notes (typing piano off) |
| R14 | **Done** | Scale highlight combo (12 roots × 8 scales, list in `core/NoteTools`); **… menu → Snap notes to the scale**: drawn, painted and moved notes and the chord stamp land on the scale, and so do incoming MIDI and PC-keyboard notes (`AudioEngine::setInputScale`); "Move the notes onto the scale" fixes existing notes |
| R15 | Partial | Chord stamp combo of 11 shapes; follows scale snap. No "only one" or inversion modes |
| R16 | No | — |
| R17 | Partial | Slide toggle in the toolbar; Ctrl+P toggles porta on the selection |
| R18 | Partial | Ctrl+X/C/V (paste in place, not at the view's left), Ctrl+B, Del, arrows: Up/Down ±1 (Shift = octave), Left/Right by grid (Shift = bar), Ctrl+Shift+Left/Right rotate within the bar. Different key mapping from FL for transposition |
| R19 | Partial | Ctrl+A, Esc. No invert/random/off-grid selection |
| R20 | No | — |
| R21 | Partial | Alt+M mutes, Shift+Alt+M unmutes the selection (FL keys, round 167j); no note grouping |
| R22 | Yes | One undo step each, on the selection or all notes (`core/NoteEdit.cpp`): Ctrl+U chop to grid, Ctrl+G join touching notes, Ctrl+L legato, Shift+D shrink to grid, Shift+Q quantise starts, Ctrl+Q (and Ctrl+Alt+Q) quantise starts and lengths; also in the roll's … menu |
| R23 | Partial (first set) | **Arpeggiate (Alt+A), Strum (Alt+S), Flip (Alt+Y), Randomise (Alt+R), Scale levels (Alt+X), Limit (Alt+K)** in `ui/NoteToolDialogs.*` (maths in `core/NoteTools.cpp`, unit-tested): live preview on the pattern, Accept = one undo step, Cancel/close restores. Not done: chop, quantise, articulate, flam, period slicing, LFO, melody/chord generators (dialogs) |
| R24 | **Done** | Wheel / Shift+wheel scroll (**smooth: glides at the display rate; trackpad deltas scroll proportionally; a horizontal wheel scrolls sideways**), Ctrl+wheel horizontal zoom **anchored at the mouse**, **Ctrl+Alt+wheel vertical zoom, Shift+Alt+wheel nudges the selection by the grid, PageUp/PageDown, Shift+1/2/3 (1, 4, 16 bars), Shift+4 all, Shift+5 selection, Shift+0 centre on the playhead, middle-drag pans, middle-drag on the keyboard zooms vertically around the clicked key**. The editors paint opaque (no flicker of what is behind) |
| R25 | Yes | Keyboard click auditions (drag glides over the keys); clicking/moving a note also auditions (a LYNX choice, R26). The keyboard is wider (72 px), labels every C with its octave in a font that follows the row height, names every white key when rows are ≥ 16 px, tints scale keys and lights the hovered key and its grid row; notes show their name whenever it fits |
| R27 | No | Markers exist only on the Playlist ruler |
| R28 | Yes | MIDI menu import/export (`:1163-1209`) |
| R29 | No | — |
| R30 | Yes | Round 190as: `core/PianoRollScript.h` (LYNX's own `flpianoroll` on the embedded Python), Tools › Scripts with LYNX's scripts, My scripts and FL's folders; FL's shipped scripts run unmodified. Sandbox and time limit are LYNX additions |

**Playlist**

| ID | LYNX | Where / note |
|---|---|---|
| P1 | Yes | Tool strip with Pencil, Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen, Slip; keys P/B/D/T/C/E/Z/Y/S while the typing piano is off. **Ripple is a mode** (Shift+S, ruler menu), not a tool: resizing a clip's end moves the later clips on its track, Delete closes the gaps |
| P2 | **Done** | Right-click deletes in every tool (right-drag many, hold on empty = Knife), **double right-click mutes the clip** (the first click's delete is undone), **middle-drag pans**, Ctrl selects, Shift copies, Alt no snap. Right-click deletes on the name strip too, as in FL; the clip menu opens from the icon at the clip's top left (click or right-click; round 88). **The hovered clip is outlined, its edge under the mouse shows a grip, its title strip shows a menu button (FL's clip menu icon, [DOC][pl]); the cursor shows the action (move, copy, resize, slip, fade, automation point)** |
| P3 | Partial | Picker panel selects the source; click on empty space places it (`:844-882, 1604-1739`). No source menu, no hover preview |
| P4 | Partial | Picker lists patterns / audio / automation (`:1604-1739`); no right-click actions |
| P5 | Partial | Fade handles fixed. Left-edge trim moves start and offset together; Shift on an edge trims; Slip tool or Alt+Shift+drag slips the content. **Ripple resize** (ripple mode). No stretch |
| P6 | **Done** | Drag, group move, Shift+drag copy, Ctrl+box. **Shift+Left/Right** move the selected clips (all clips when none is selected) by one snap step ((none) = one tick), **Shift+Up/Down** by one track, **Alt+Left/Right** (or Alt+Shift) by a fine step without snap (one pixel's worth of ticks at the current zoom), as in FL (issue #147). The selection stops as a whole at the song start and at the first / last track (no wrap); clips landing on occupied space overlap, as after a drag; each key press is one undo step; the view follows the leading edge of the moved clips. In a text field or an open menu the arrows keep their normal function. Also in Playlist menu › Edit (`clipops::shiftClipsInTime` / `shiftClipsTrack`, `PlaylistComponent::moveClips`) |
| P7 | Yes | Shift+Alt+wheel nudges the selected clips by the grid (one undo step per burst; the same move as the Shift+arrow keys, P6). Shift+wheel with Edit › "Nudge clips with mouse wheel" on shifts the clips' content, as FL 2026 does (round 189g) |
| P8 | Partial | Split, **mute/unmute, chop to grid**, make unique, select all of this pattern, open in rack, **open in Piano Roll**, delete. Missing: per-clip colour and name (no model field yet) |
| P9 | Partial | Clip volume, normalise, remove fades. No clip automation, crossfades, reverse |
| P10 | **Done** | Double-click a pattern clip opens the **Piano Roll when the pattern has piano-roll notes, else the Channel Rack** (`hasPianoRollNotes`); Options → "Double-click on a pattern clip opens" sets it; **Alt+double-click opens the other one**; automation clips open their editor |
| P11 | Partial | LED mutes; **right-click menu: rename and colour, mute, solo, insert above/below, move up/down, delete** (`noteedit::insertTrack/removeTrack/swapTracks/soloTrack`); **double-click the name renames; the hovered header and LED light up**. **Round 90: track colours** (imported from the FLP track record 0xEE, kept in .lynx, header tinted). Missing: icon, grouping |
| P12 | Partial | Track mute; **Ctrl+click or right-click on the LED solos** (again: all play), also in the track menu; **Shift+wheel over a header moves the track up/down** (the mouse follows it). No grouping |
| P13 | **Done** | Click/drag on the ruler seeks; Ctrl+drag makes the time selection (= the loop), which selects the clips it touches; double-click or Ctrl+click clears it. **Shift+drag inside it moves the selection with its contents, Ctrl+Shift+drag copies** (clips are cut at its edges; `noteedit::moveTimeRange`). No "outside the selection" tint |
| P14 | Partial | Ctrl+Ins inserts and Ctrl+Del removes the time selection's length across all tracks (clips cut at the edges, markers move) (`core/NoteEdit.cpp` `insertTime`/`deleteTime`). No slice-and-insert, no Insert-key slice |
| P15 | Yes | Ctrl+B with a time selection repeats it after itself (selected clips starting inside it, or all that do) and moves the selection on; without one, duplicates by the span rounded to bars |
| P16 | Partial | Ruler menu: add/rename/remove marker, time-signature change, remove loop (`:1049-1089`). No marker actions, no jumps |
| P17 | **Done** (most) | Ctrl+wheel zoom anchored at the mouse, smooth wheel scrolling, **Ctrl+right-click on empty space zooms to everything and back**, **PageUp/PageDown, Shift+1/2/3, Shift+4 all, Shift+5 selection (or the time selection), Shift+0 centre**, also in the ruler menu. No vertical zoom |
| P18 | No | — |
| P19 | No | — |
| P20 | Yes | Arrangement combo: new, clone, rename, remove (`:54-135`); no merge |
| P21 | No | — |
| P22 | No | — |
| P23 | No | — |
| P24 | No | — |
| P25 | **Done** | `core/WaveAnalysis.*` + `ui/WaveformView.*` (feature/spectral-wave). Own design, not FL's: a multi-resolution peak cache (min/max/RMS per channel + the RMS of three bands, low < 250 Hz, mid 250 Hz-4 kHz, high > 4 kHz) built on a worker thread and cached on disk (`%APPDATA%\LYNX\wavecache`, keyed by path + size + date). Each column is coloured by its band mix in LYNX's own palette (warm red-orange = bass, soft lime = mids, cyan-white = treble), RMS body + peak hull with a light outline, interpolated when zoomed far in. Options → Waveforms: spectral (default) or classic one colour, stereo lanes. Drawn in the Playlist clips, in a strip at the top of the Sampler settings (Start/End region dimmed, reverse) and under the Browser list for the previewed file |

**Automation**

| ID | LYNX | Where / note |
|---|---|---|
| A1 | Partial | Right-click any knob → clip + editor. With a time selection the clip spans it; otherwise the whole song. Goes on a new bottom track (not "highest empty track in view") |
| A2 | **Done** (most) | Editor window: click adds, drag moves, **Shift / Ctrl pressed after the click lock time / value**, **right-click point menu: type value, copy/paste value, straight segment, delete**; Shift+right-click adds a point on the curve. **Inline in the Playlist** (Pencil/Brush): drag a point, click on the curve to add one, right-click a point deletes it |
| A3 | **Done** | Alt+drag on a segment sets tension; the point menu has FL's **Mode** list (Smooth, Hold, Single curve 1–3, Double curve 1–3, Half sine, Stairs, Smooth stairs, Pulse, Wave) in the Playlist and in the editor, with FL's shapes (`core/FlCurve`, REA round 190bw), saved in `.lynx` and carried by `.flp` import and export; a new point takes the last mode and tension used, as in FL. "Straight segment" stays as a LYNX extra |
| A4–A6 | No | — |
| A7 | No | — |
| A8 | Partial | Make unique and tempo automation exist |

**Mixer**

| ID | LYNX | Where / note |
|---|---|---|
| M1 | **Done** | Click selects; **Ctrl+click / Shift+click on a strip's header add / select a range**; selected faders and pans move together (offsets kept) |
| M2 | **Done** | **Alt+Left/Right and Shift+wheel move the selected inserts**; channels, sends, sidechains and automations follow (`mixerops::moveTracks`). Strip menu: move left/right. **UI-C:** dragging a strip header sideways reorders the selected inserts |
| M3 | **Done** | Send arrow on other strips when one is selected; drag after enabling sets the level; **right-click the arrow: send here / send only here / sidechain only** (a level-0 send, the compressors and vocoders there listen to the source; `mixerops::sidechainTo`). **UI-C:** compact routing lane under the strips: send switch (→ n), a **send level knob** under it once the send is on (full control contract), an "→ selected" tag on inserts that feed the selected one, and **wires** from the selected insert to its sends (red) / Master (dim) and from its sources (blue) |
| M4 | **Done** (most) | Strip menu: **route the selected inserts here / only here**, send only to this track, send only to Master again. No submix |
| M5 | **Done** | **Ctrl+L links the selected channels to the selected insert, Ctrl+Shift+L to consecutive inserts from it, Alt+L selects the channels linked to it** (Mixer focused; also in the strip menu). Keys checked on [DOC][sc] |
| M6 | **Done** | Mute LED: left-click mutes, **right-click or Ctrl+click solos exclusively** (again: unsolo); the "S" button still toggles; solo along the path. No Ctrl+right-click unmute-all |
| M7 | Partial | Rename in the menu; colour only shown if imported (`:150-154`). **Round 90:** the whole strip is tinted with the insert colour |
| M8 | **Done** (most) | 10 slots, click empty → add menu, bypass LED, dry/wet, menu with replace / presets / sidechain / move / delete; **drag a slot or wheel over its name to reorder it** (`mixerops::moveEffectSlot`); **A/B compare** (store as B, swap with B) in the slot menu; effect presets dropped from the Browser load into a slot |
| M9 | **Done** | "FX" switch in the effects panel header: click turns all effects of the insert on/off, **right-click opens or closes their plugin windows** |
| M10 | **Done** (LYNX variant) | **Record-arm dot on every insert**: arming turns audio recording on and the take plays through the armed insert (`AudioEngine::setInsertArmed`); not saved with the project. No per-insert input selector |
| M11 | Partial | **UI-C:** routing wires for the selected insert (see M3); no docks, view sizes or separators |
| M12 | Partial | Track EQ callout with 9 sliders (`:475-562`) |
| M13 | No | — |
| M14 | Partial | "+" adds an insert (`:323-333`); no delete/trim/reorder |
| M15 | Yes | A press on the rail steps the fader towards the mouse while held, as FL (REA_rodada177a section 4.2) |

**Browser**

| ID | LYNX | Where / note |
|---|---|---|
| B1 | **Done** | A click on an audio file previews it (again on every click); the **preview strip** under the tree shows its waveform, length, rate and the playback position, and can be clicked (hear again) or dragged like the file. The tree is LYNX's own (`BrowserComponent::FileTree`): one click opens / closes a folder, hover highlight, per-type icons, the playing file in red, Left/Right close / open folders, right-click folder menu (collapse all, show in the file manager, add folder), and every row explains itself in the hint bar [DOC][br-use] |
| B2 | **Done** | Browser focused: Up/Down step and preview; **Shift+Up/Down step and load each file into the selected channel** (sampler or built-in drum); also through the search results |
| B3 | **Done** | **Enter loads the selected file into the selected channel** (or opens it like a double-click when the channel can't take it); double-click still makes a new channel |
| B4 | Partial | Rack and Playlist accept drops (tree and search results); dropping onto a rack row replaces that channel's sample; **effect presets (.fst) drop on the effects panel**. **a generator dropped on a mixer strip or one of its effect slots adds a channel routed to that insert** (the insert takes its name while it has its default one). No drop on the empty mixer space (FL 2026: a new instrument track) |
| B6 | **Done** | **Search box (Ctrl+F)** under the folder list: searches the current folder and below in the background (up to 500 results), words AND-ed, OR, NOT / -word, "exact phrase", * and ? wildcards (`core/SearchQuery`); Esc clears. No F2/F3 stepping |
| B7 | Partial | Root-folder combo (`BrowserComponent.cpp:80-91`); no tabs, user folders, tags or favourites |
| B8 | No | — |
| B9 | **Done** | **Quick-add palette** (`Edit › Quick-add palette...`, or right-click the toolbar's plugin-picker button; F8 opens the Plugin picker since round 171c): instruments (built-in and scanned plugins) and effects side by side, filtered as you type, Enter/double-click adds an instrument as a new channel or an effect into the selected insert; opened from the Mixer it starts on the effects |
| B10 | Partial | Fixed left panel (detachable as a workspace panel); **user folders through "Add folder…" in the folder list** (kept in `browser-folders.txt`). No back/forward |
| B11 | **Done** (LYNX's own, local) | Right-click an audio file › **Find similar samples**: the Browser lists the samples of its own folders that sound most alike, most similar first, with a percentage (`core/SampleSimilarity.*`: 49 numbers per file for tone, shape, rhythm and pitch classes; index `browser-similar.idx`, at most 150,000 files; one lowest-priority worker that waits while LYNX plays; nothing is analysed before the first use). FL's search is FL Cloud's and can't rank local files; no FL Cloud in LYNX |

**Plugin windows**

| ID | LYNX | Where / note |
|---|---|---|
| W1 | Partial | External plugins: native window with minimise and close (`ui/PluginWindow.cpp`); no header bar, presets or menu. **Native effects (round 93): their own window in the FL plugin's arrangement** (`ui/PluginEditors.cpp`, layouts in `core/PluginLayout.cpp`, study in `docs/analise/UI_STUDY_plugins93.md`) with FL's wrapper bar: options arrow (presets, reset, hidden switches, bypass), preset name with the FL presets of the plugin, previous / next; opened by double-clicking the Mixer slot or the **Editor** button. 3x Osc and FL Keys pages follow FL's arrangement in the channel settings |
| W2 | **Done** | The channel button toggles the plugin window (`PluginWindows::isOpen/close`) |
| W3 | Partial | Always detached; parameters are reachable through the "Automate plugin parameter" submenus |
| W4, W5 | No | — |

**Recording**

| ID | LYNX | Where / note |
|---|---|---|
| X1 | Partial | Rec button + "v" menu (notes always, audio arm, **Recording options**); R toggles recording. No automation filter |
| X2 | **Done** | Metronome (Ctrl+M), **count-in of 1/2/4 bars (Ctrl+P toggles one bar)**, **wait for the first note (Ctrl+I)**, **overdub vs replace**. Keys checked on [DOC][sc]. No loop-record option |
| X3 | **Done** | Notes go into the current pattern, one undo step per take; **in Song mode the take places a pattern clip where recording started** if none plays there |
| X4 | **Done** | Recorded notes are quantised to the global grid (option on by default) |
| X5 | No | — |
| X6 | Partial | Input → 24-bit WAV → new audio channel + clip on a new track (`AudioEngine.cpp:286-405`); not tested by a human (STATUS.md) |
| X7, X8, X9 | No | — |

### 3.2 Prioritised implementation list

The specs below are **LYNX's own design**. They borrow the *idea* of FL's workflow, not its names, look or exact
key map. Where an FL key is a widely shared convention (Ctrl+B duplicate, Space play, F-keys for panels), LYNX
may keep it. All key bindings should live in one remappable table (see P1-14).

#### P1: the core feel of the workflow

Status: items 1, 4, 5, 6, 7, 8, 14 and 15 were built on `feature/wf-shell`; items 2, 3, 9–13 on `feature/wf-editors` (merged into
`feature/wf-shell`). Remaining gaps inside the shell items: no right-click on Stop (remember seek time), tempo nudge or
temporary half speed; no channel zip/unzip. Closed on `feature/wf-p2`: detached windows snap, F8 opens the quick-add
palette, numpad 1–9 picks patterns, patterns move up/down (Shift+Ctrl+Up/Down) and clone (Alt+C), channels sort and
select-unused, ripple editing and Shift+drag time-selection move/copy.

1. **Workspace desktop (G1, G3, G4, G5, G17).** Replace the tab bar with a *workspace*: Playlist, Channel
   Rack, Piano Roll, Mixer and Browser become movable, resizable, overlapping panels inside the main window.
   - Each panel has a title strip with *maximise*, *detach* (moves it into its own native window and back) and
     *close*. Panels snap to each other and to the edges; Alt disables snapping.
   - F5/F6/F7/F9/F8 **toggle and focus** a panel: the first press shows and focuses it, a press while focused
     hides it. Enter maximises or restores the Playlist.
   - "Workspace → Reset layout" and "Save layout as…" go to `%APPDATA%\LYNX\layouts`. The layout is saved with
     the user settings, not the project.
   - Keyboard shortcuts go to the focused panel.
2. **Edit tools in the Piano Roll and Playlist (R1, R2, R3, P1, P2).** Add a tool strip to both editors: Pencil,
   Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen (and Slip in the Playlist).
   - Keys (when the typing piano is off): P, B, D, T, C, E, Z, Y, S.
   - Rules for every tool: right-click always deletes, Alt always bypasses snap, Ctrl always selects.
   - Right-press-and-hold on empty grid gives a temporary Knife.
   - The current gestures stay as the Pencil's behaviour, so nothing breaks.
   - **Status:** Done (feature/wf-editors): tool strips in both editors; single-letter keys wait for the typing-piano toggle (P1-14).
3. **Global grid + per-editor "Follow" snap (G9, G10, R11).** Add a grid selector to the main toolbar. Each
   editor's grid combo gets a "Follow global" entry, which becomes the default.
   - Add a "Screen" grid that follows the visible grid lines, and a "Nearest note" grid that snaps to existing
     notes and ghosts.
   - Backspace toggles the global grid between its last value and none.
   - Right-click the grid selector → "Default note length" for new notes.
   - **Status:** Done: global grid + Follow, Screen and Nearest note grids, Backspace, default note length. Record quantise still ignores it.
4. **Pattern navigation keys (G11, C16).** Numpad +/− for next/previous pattern; F4 jumps to the next empty
   pattern and offers a name; F2 opens a combined rename + colour dialog (a key in it picks a random colour from
   LYNX's palette).
   - Add Insert pattern and Split by channel to the pattern menu.
   - Shift+wheel over the rack title changes the pattern.
5. **Channel Rack selection and organisation (C7, C10, C18, C6).** Add a selection column to each row.
   - Click selects one row, Ctrl+click toggles, Shift+click selects a range. Up/Down move the selection;
     Alt+Up/Down move the selected rows.
   - "Clone channel" (settings without notes), "Insert channel here" and "Delete selected".
   - **"Send selection to free inserts" (Ctrl+L)** creates one insert per selected channel, named and
     coloured after it.
   - The selection also drives MIDI input and step copy/paste.
6. **Channel button = window toggle (C5, W2).** A single click on the channel name opens or closes that
   channel's editor: plugin UI or settings. Opening another channel's editor closes the previous one unless Alt
   is held, and Settings → "Keep editors open" turns the auto-close off.
   - Ctrl+click auditions only. This moves the current "click = audition" to Ctrl+click.
   - Expose the existing model-channel settings windows (`ChannelSettings.cpp:1197-1202`) here.
7. **One control contract for every knob and fader (K1, K2, K3, K4).** Create a shared `lynx::ui::ValueControl`
   helper that every knob, fader, route box and number display uses:
   - drag to change, Ctrl+drag fine, wheel = small step (Ctrl+wheel finer);
   - Alt+click or middle-click resets (double-click also resets, as today);
   - right-click menu: *Type value…, Reset, Create automation clip, Link to MIDI controller…, Unlink*.
   This also fixes the inconsistencies the inventory found: Shift-fine only on the mixer, no wheel on the rack
   or mixer, no menu on the effect wet/dry knob.
8. **Hint strip (G12).** Replace the static shortcut line with a strip at the bottom of the main window.
   - It shows a one-line hint for the control under the mouse, the live value while dragging, and small markers
     for "can be automated" and "has a menu".
   - Hints come from a `hint` property set on each component, through the control contract in item 7.
   - LYNX's own icons; the MIDI-learn prompt keeps working.
9. **Piano Roll channel switching and ghost notes (R12, R13).** Add a channel selector to the roll's toolbar
   (channels with notes in this pattern are marked; wheel steps through them).
   - Previous/next channel keys, previous/next channel with notes keys.
   - Double right-click a ghost note to switch to its channel. A ghost toggle in the toolbar.
   - Ghosts also from overlapping Playlist clips (shown hollow) when the roll was opened from a clip.
   - **Status:** Done, except ghosts from overlapping Playlist clips (deferred).
10. **Clip edge editing (P5) and the fade bug.** First fix the fade handles: `mouseDown` must call
    `fadeHandleAt` before the move/resize logic. Then:
    - left-edge trim (adjusts `offset` and `start` together);
    - Shift+drag an edge = trim without moving content;
    - Slip tool or Alt+Shift+drag inside the clip = move content under a fixed clip.
   - **Status:** Done: fade bug fixed, left-edge trim, Shift+edge trim, Slip tool and Alt+Shift slip.
11. **Note inspector (R9, R7, R5).** Double-click a note to open a small inspector with velocity, pan, release,
    fine pitch, two "mod" values, slide/porta, time and length.
    - Edits apply to the whole selection, with Apply / Revert.
    - Also: Shift+drag locks to time and Ctrl+drag to pitch after the first move, and an opt-in left-edge resize.
   - **Status:** Done: Note inspector call-out, move locks, opt-in left-edge resize. Release / Mod X / Mod Y are stored, not yet played.
12. **Quick note operations (R22, R18).** Add one-key note operations, each one undo step and acting on the
    selection (or all notes when nothing is selected):
    - *Chop to grid* (Ctrl+U), *Join touching notes* (Ctrl+G), *Make legato* (Ctrl+L inside the roll),
      *Shrink to grid* (Shift+D), *Quantise starts* (Shift+Q) and *Quantise* (Ctrl+Q, starts + lengths).
    - Rotate within the bar (Ctrl+Shift+Left/Right).
   - **Status:** Done (core/NoteEdit.cpp, unit-tested).
13. **Playlist time selection (P13, P14, P15, A1).** Ctrl+drag on the ruler makes a *time selection* that also
    selects the clips inside it and becomes the loop. It is cleared with a double-click.
    - Ctrl+B with a time selection repeats it after itself.
    - Ctrl+Ins / Ctrl+Del insert or remove that much time across all tracks.
    - New automation clips take its length.
   - **Status:** Done: Ctrl+drag time selection, Ctrl+B repeat, Ctrl+Ins/Del, automation clips take its span.
14. **Typing-piano toggle and a remappable key table (G16).** Make the PC keyboard piano a toolbar toggle
    (Ctrl+T). When it is off, single letters are shortcuts (tools, L, numbers to mute channels 1–10).
    - When it is on, only Ctrl/Alt chords and F-keys are shortcuts.
    - Move every binding into one data table (`core/KeyMap`), which can be loaded from JSON. This avoids the
      clash between notes and future single-key tools.
15. **Transport details (G6, G7, G8).** Space toggles play/stop and returns to the start point. Ctrl+Space
    pauses. Double-click Stop = all notes off.
    - Right-click PAT/SONG focuses the matching panel.
    - The tempo display becomes a drag-able number (whole BPM on the integer part, decimals on the fraction).
      Its right-click menu: type in, tap, half speed, automation.

#### P2: deepens the workflow

Status (`feature/wf-p2`, merged with the FL-style layout of section 5): every item below landed except where its
note says otherwise. Each window's own P2 items are also in the menu at the left of its title bar
(`MainComponent::addPanelMenuItems`). Testable logic is in
`core/NoteTools`, `core/MixerOps`, `core/History`, `core/SearchQuery`, `core/RackOps`, `core/NoteEdit` and
`core/Recorder` (tests in `tests/WorkflowP2Tests.inc`); new keys are in `core/KeyMap`. Deferred: per-segment curve
shapes for automation (needs a model field), clip colour/name (model fields), track colour/height/groups, F2/F3
search stepping, instrument drops on mixer inserts, record filters for automation, and the remaining tool dialogs
(chop, quantise, articulate, flam, period slicing, LFO).

- **Step graph lanes (C4).** A collapsible lane under the rack row for per-step velocity, pan and fine pitch.
  Drag sets a value, right-drag draws a ramp, Alt+click resets. It writes the same note fields the Piano Roll uses.
  - **Status:** Done (Ctrl+K; velocity, pan, release, fine pitch, Mod X/Y).
- **Step fill and rotate (C6, C20).** Channel menu → *Fill every 2/4/8*, *Euclidean fill…* (pulses / length /
  rotation, with a preview), *Rotate left/right*.
  - **Status:** Done (channel menu → Steps; rotate also on Ctrl+Shift+Left/Right in the rack).
- **Mixer selection and routing gestures (M1, M2, M3, M4, M5, M6).**
  - Multi-select inserts; selected faders move together; Alt+Left/Right reorder.
  - Right-click the send button → "Sidechain only" (a level-0 route that LYNX's compressor already treats as a
    sidechain).
  - Menu: *Route selected here / here only*, *Link selected channels here* (Ctrl+L in the Mixer).
  - Mute = left-click, solo = right-click or Ctrl+click on the same LED, matching the rack.
  - **Status:** Done (Ctrl/Shift+click on headers; Ctrl+L / Ctrl+Shift+L / Alt+L; right-click a send arrow for
    "sidechain only").
- **Effect slot handling (M8, M9).** Drag a slot to reorder it, or wheel over it; a per-insert "all effects
  on/off" switch; right-click that switch to open or close all effect windows; an A/B compare buffer per slot.
  - **Status:** Done (the A/B buffer lives for the session).
- **Browser search and audition-swap (B2, B3, B6, B10).**
  - Ctrl+F opens a filter box (wildcards, AND/NOT).
  - Up/Down auditions the files in turn. Shift+Up/Down swaps each one into the selected sampler channel; Enter
    loads it there.
  - Drop targets on mixer inserts and slots (effects, plugin presets).
  - User folders.
  - **Status:** Done (effect presets drop on the effects panel, generators on a strip or its effect slots). Plain Up/Down
    preview, Shift+Up/Down load into the selected channel (checked on [DOC][br]).
- **Quick-add palette (B9).** A searchable plugin list with instruments and effects. Double-click
  adds it to a new channel or to the selected insert, depending on which panel is focused.
  - **Status:** Done (`Edit › Quick-add palette...` and a right-click on the toolbar's plugin-picker icon; F8 opens the Plugin picker since round 171c; the Browser moved to Alt+F8).
- **History panel (G13).** A list of undo steps with names (the action labels already passed to `editSong`).
  Clicking a step jumps there. Keep conventional undo (Ctrl+Z back, Ctrl+Y/Ctrl+Shift+Z forward). FL's
  toggle-style undo is deliberately *not* copied.
  - **Status:** Done (Ctrl+Alt+H, Edit → Undo history; Ctrl+H = Stop sound as in FL, round 170h2).
- **Recording set (X1, X2, X3, X4, X9).**
  - A record menu with filters (notes, audio, automation).
  - Count-in bars, "start on first note" (wait-for-input), *Overdub vs Replace*.
  - Input quantise follows the global grid.
  - In Song mode a take places a pattern clip at the start point.
  - **Status:** Done except the automation filter: count-in (Ctrl+P), wait for input (Ctrl+I), overdub/replace,
    quantise and song clips in Options → Recording and the toolbar's "what to record" menu; count-in and
    wait-for-input also have switches next to the metronome; R toggles recording.
- **Automation clip editing (A2, A3, A4, A6).** Edit inline in the Playlist as well as in the window. Axis locks
  (Shift/Ctrl), right-click a point to type a value, a curve-shape menu per segment (single, double, hold,
  stairs, pulse, wave, smooth), min/max re-range.
  - **Status:** Done except the curve-shape menu (only "straight segment" for now).
- **Note tool dialogs, first set (R23).** *Strum*, *Arpeggiate (offline)*, *Flip*, *Randomise*, *Scale
  velocities*, *Fit to range*. Each is a small modal dialog with a live preview, Apply/Revert, and one undo step.
  - **Status:** Done as Arpeggiate, Strum, Flip, Randomise, Scale levels and Limit (non-modal windows, Accept /
    Reset / Cancel; keys Alt+A/S/Y/R/X/K checked on [DOC][sc]).
- **Clip menu completion (P8, P9, P11, P12).**
  - Clips: mute, colour, rename, chop to grid.
  - Tracks: colour, height, solo, insert/delete/move, and a track header menu.
  - Double-click a pattern clip opens the Piano Roll when the pattern has piano-roll notes, else the rack. A
    preference sets this.
  - **Status:** Done except clip colour/rename and track colour/height (no model fields yet).
- **Scale snap (R14).** "Snap to scale" when drawing or moving notes, and for incoming MIDI.
  - **Status:** Done (switch next to the Piano Roll's scale box, also in its window menu and … menu).
- **Zoom presets (R24, P17).** PgUp/PgDn, zoom to selection, show all, vertical zoom (Ctrl+Alt+wheel), and
  middle-drag pan.
  - **Status:** Done in both editors (vertical zoom in the Piano Roll only).
- **Per-insert record arm (M10).**
  - **Status:** Done (record dot on each insert).

#### P3: later or optional

- Filter groups in the rack (C13); layer channels (C17); per-channel loop/ghost steps (C20).
- Colour groups as separate voices (R16); Events-to-automation conversion (R10).
- Space editing in the Piano Roll (R20); random and off-grid selection (R19).
- Melody and chord generators (R23, last two). These need original LYNX designs and are not in scope for
  workflow parity.
- Bound channel ↔ track ↔ insert "instrument tracks" (P23).
- Live clip launching (P24); always-on capture buffer (X8); step entry (X7).
- Mixer docks, views, routing cables, insert state presets (M11, M13); browser tabs, tags, favourites (B7).
- Toolbar presets and free spacers (DATA: toolbar preset files; LYNX has show/hide/reorder, icon size and UI scale,
  section 5.1); save-new-version (G14); user templates (G15).
- Plugin window header with preset arrows, rename and MIDI options (W1, W3, W4).

---

## 4. What must stay different (clean-room and branding)

- **Source of knowledge.** Behaviour comes only from the public manual (paraphrased), the names and structure of
  installed data files, and observation. No Image-Line code, text, images, icons or skins go into LYNX. This
  document quotes nothing.
- **Look.** LYNX keeps its own theme (`ui/Theme.h`), its own layout proportions, its own control drawing and its
  own icon set. Do not reproduce FL's:
  - colour scheme or skins;
  - step-button, LED, knob or fader art;
  - window title-bar *art* (the arrangement of its parts, see section 5, is followed);
  - splash, mascot or logo (LYNX's splash uses its own logo, by w3teal, and its own synthesised sound).
  Since October 2026 the *arrangement* of the toolbar panels and window title bars follows FL's (section 5,
  the user's decision); everything is still drawn by LYNX.
- **Names.** Generic terms are fine: Channel Rack, Step sequencer, Piano Roll, Playlist, Mixer, Browser,
  pattern, clip, automation clip, ghost notes, make unique, quantise, strum, arpeggiate, chop. Avoid
  Image-Line's product or brand-specific names in LYNX's UI:
  - plugin and tool names: anything "Fruity", Edison, Riff Machine, the period-slicing tool's name, Patcher,
    NewTone/NewTime, FL Cloud, Gopher;
  - feature names: "Performance mode", "Spare state", "Score logger".
  Use LYNX names: e.g. Quick-add palette, Note inspector, A/B compare, Live launch, Capture buffer, Hint strip.
  Imported effects and instruments may *mention* the FL plugin they came from in the import report, as today,
  for interoperability.
- **Sounds and content.** Do not ship FL's samples, presets, templates, demo projects or metronome sounds.
  - LYNX may *read* them from the user's own FL installation (as it does for imports and browser roots).
  - LYNX templates (`core/Templates.cpp`) and the metronome stay original.
  - LYNX's name presets, toolbar presets and typing-piano maps must be written from scratch. They must not be
    converted from FL's `NamePresets_*.txt`, `.tpr` or `.scr` files.
- **Key map.** Common conventions (Space, Ctrl+Z/C/X/V/B, F-keys for panels, L for the mode) are fine to share.
  The full map lives in LYNX's own remappable table (P1-14), so the default map is LYNX's choice and not a copy
  of FL's list.
- **Deliberate differences already chosen.** Conventional undo/redo instead of toggle-style undo;
  drag-painting of steps (not documented for FL); auditioning notes while drawing; standard Ctrl+N = new
  project.
- **Trademarks.** "FL Studio" and "Image-Line" are mentioned only to describe compatibility, e.g. ".flp import".
  This study is internal design documentation.

## 5. Layout

How FL Studio arranges its main window, and how LYNX follows that structure. Read in October 2026 from the
manual pages below (paraphrased). Items marked **[unverified]** come from general knowledge of the program or
from third-party pages, not from an Image-Line page; they are worth checking against a fresh FL install.
LYNX copies only the *arrangement*: where things are, in which order, and how windows behave. All drawing,
colours, icons and fonts stay LYNX's own (dark theme, red accent, `ui/Icons.cpp`).

### 5.1 Main toolbar
- The toolbar is a set of panels. The manual lists: the menu bar, the title/hint panel (project name above, hint
  bar below), the transport (Pattern/Song switch, play, stop, record, tempo), song position, the window
  controls, monitor volume and master pitch, global snap, the "add" panel, multilink, the time display, the
  pattern selector, the output scope/meter, CPU/memory/polyphony, the chord panel and the online panel.
  The manual lists them by topic and does not give their order on screen. [DOC][tb]
- Its shortcut icons open the Playlist, Channel Rack, Piano Roll, Mixer, Browser and plugin picker, and also
  cover the metronome and recording options, undo and save. Icons can be added or removed. [DOC][tb]
- Right-click on an empty part of the toolbar, then "Edit", to reorder or hide panels and save presets.
  Ctrl+F11 hides the whole toolbar. [DOC][tb] [DOC][view]
- **Hint bar position.** It is the lower line of the title/hint panel, under the menu at the top left. It is not a
  status bar at the bottom of the window. An optional extended hint panel can sit at the lower left. [DOC][tb]
  [DOC][view]
- **Global snap.** The toolbar has one snap selector. It sets the snap for the Piano Roll, the Playlist and the
  event editor (each editor set to "Main" follows it) and the quantise for recorded notes. [DOC][tb-snap]

**LYNX** (`ui/MainComponent.cpp`, `resized()`; widgets in `ui/Toolbar.*`, the hint bar in `ui/HintBar.*`): one
band, two rows high, panels left to right:
1. the menu, with the project name beside it (FL's title/hint panel) and the **hint bar under it**; this column
   takes the room the other panels leave (360–620 px), so hints are shown in full;
2. the transport: a two-line Pattern/Song switch, play, stop, record, the tempo, and the time display with the
   **song position slider** under both;
3. **master volume over master pitch** (small knobs named MASTER and PITCH, value beside them);
4. the recording options: metronome, count-in and wait-for-input over "what to record" and the typing keyboard
   as piano;
5. global snap over the pattern selector, each with a caption (SNAP, PATTERN); the wheel steps both;
6. shortcuts, two groups of three icons per row: the windows (Playlist, Channel Rack, Piano Roll / Mixer,
   Browser, plugin picker = the quick-add palette, F8), then the file actions (new, open, save / export, undo,
   redo);
7. on the right, after any spare room: **CPU / RAM / polyphony**, then the **output** oscilloscope and peak meter.

This is the default order; the user can hide and reorder the groups (see "Toolbar customization" below).
When the window is narrow, panels drop by priority (the first to go: output, meters, recording options, file
actions, window shortcuts, master, snap/pattern); the transport always stays. Everything they hold is also in the menus or on
keys. Every control has a hint with its mouse gestures and keys. Toolbar icon buttons fade their hover state in
and out (`IconButton`). The LYNX logo is not drawn inside the window; it stays the window and taskbar icon.

**Toolbar customization and icon sizes (observed in FL Studio 2026, October 2026; behaviour only).**
- *Observed* (fresh FL Studio 2026 window, 1920×1032, 100% scaling): the toolbar is two rows of about 41 px.
  Shortcut buttons are about 30 px wide and 32 px high with glyphs of about 20 px; play/stop/record are the same
  size. The editors' tools sit in the window's title row (about 22 px high), at about 21 px per tool with glyphs
  of about 16 px. So the tools are small next to the toolbar, but not tiny. **[observed]**
- *Data*: the toolbar preset files list each panel or button with a width and a row (row 13 = upper, 14 =
  lower); icon buttons are 30 px wide at 100%, and a spacer ("Splitter") holds the spare width. [DATA]
- Right-click on an empty part of the toolbar opens the toolbar menu: Edit (edit mode), Separators, Flat
  buttons, Hint bar (the extended hint panel), Hide (Ctrl+F11) and Presets (factory presets and "Auto saved",
  the last edited layout). [DOC][tb]
- Edit mode: items are dragged from a tray below the toolbar onto it to add them, and dragged off it to remove
  them. Spacers push items to the right; the rightmost spacer absorbs window resizes. When the window is too
  narrow, the right spacer shrinks first, then the other spacers, then resizable panels, then icons. Presets are
  saved and loaded from a menu in edit mode. [DOC][tbe]
- Sizes: there is no icon-size setting. General Settings has **Main GUI scaling** (System by default, or a
  percentage; 125–200% suggested for 4K/5K), with separate **Toolbars**, pop-up and plugin scaling that follow it
  by default; changes need a restart. [DOC][envgen]

**LYNX** (`core/ToolbarLayout.h`, unit-tested; `MainComponent::setToolbarLayout`, `toolbar::CustomizePanel`):
- **Right-click on the toolbar background** opens the toolbar menu: one tick item per group (Transport, Master
  volume and pitch, Recording options, Snap and pattern, Window shortcuts, File and undo, CPU/memory/polyphony,
  Output monitor) to show or hide it, **Icon size** (small / medium / large), **UI scale** (80–250%),
  **Customize toolbar…** and **Reset toolbar to default**. The same menu is in Options → Toolbar.
- **Customize toolbar…** lists the groups in toolbar order with a check box each; Move up / Move down (or
  Alt+Up/Down) reorder the selected group; icon size, UI scale and Reset to default are there too. Changes apply
  at once and are saved in `ui-options.txt` (`toolbarGroups`, `toolbarIcons`, `uiScale`).
- The menu with the hint bar under it always comes first. The spare width goes before the first of the meters or
  the output monitor (LYNX's single "spacer"). When the window is narrow, shown groups still drop by priority;
  the transport stays.
- **Edit mode** (round 115, docs/analise/REA_rodada115.md; `toolbar::EditOverlay`): the toolbar menu now starts the
  way FL's does - **Edit toolbar**, **Separators**, **Hide the toolbar panels** (Ctrl+F11: only the menu and the hint
  bar stay, on one row), **Presets** (LYNX's own: Default, Compact, Minimal, Mixing) - then the per-panel switches.
  In edit mode the panels are outlined with their names and do not react to clicks; a tray under the toolbar lists
  every panel. Drag a panel sideways to move it, into the tray to remove it, from the tray onto the toolbar to add it
  where it is dropped; right-click a panel: remove / move left / move right; Done or Esc leaves. Saved as before,
  plus `toolbarSeparators` and `toolbarVisible`. Still panel groups, not single buttons or spacers.
- **Icon size** sets the toolbar row height (24 / **28** / 34 px) and the icon box (14 / **16** / 20 px). Since the
  light-header round (docs/analise/UI_toolbar_study.md) icons use one fine line (`icons::strokeWidth`, about
  1.25-1.5 px at these sizes), every toolbar button is flat (a faint wash on hover, a faint accent wash when on),
  and play / stop / record are one slim row in a quiet well. The same setting sizes the editors' tool strips:
  14 / **16** / 18 px icon boxes. The window and file shortcuts are blocks of three per row.
- **Quick tips**: every icon button of the toolbar and of the panel title bars (and the toolbar's displays and
  selectors) shows its name, and its shortcut if it has one, in a small dark bubble under it about 400 ms after
  the mouse rests on it (`ui::setQuickTip`, `ui::QuickTipWindow` in `ui/HintBar.h`); moving to the next control
  swaps the bubble at once. The hint bar keeps the full description and the mouse gestures.
- **UI scale** scales the whole interface (`juce::Desktop` global scale factor) at once, without a restart.

### 5.2 Editor tools live in each editor's own toolbar
- **Playlist.** Its toolbar runs along the top of the Playlist window. Left to right: the Playlist menu, the snap
  selector, the tools, then play/pause. The tools are Draw (P), Paint (B), Delete (D), Mute (T), Slip (S),
  Slice (C), Select (E), Zoom (Shift+Z) and Playback (Y). The title bar also holds the arrangement selector.
  [DOC][pl]
- **Piano Roll.** Same idea. Its toolbar carries the menu, snap, snap to scale and chord stamp, then the tools:
  Draw, Paint, paint-drum, Delete, Mute, Interpolate, Slice, Select, Zoom and Play, then the ghost-note toggle.
  The target channel selector is in its title bar. [DOC][pr]
- **No main-toolbar option for tools.** No manual page describes a way to put the editing tools in the main
  toolbar. The main toolbar only has the *global snap* panel (5.1) and shortcut icons that do not include the
  tools. [DOC][tb]
- In FL, each editor's toolbar is part of its window's title bar. The window menu is at the left (it is small
  and easy to miss), then the title or selectors, then the tools. [DOC][iface] Minimise, maximise/restore and
  close are on the right **[unverified]**.

**LYNX**: the tools stay in each editor (`ui/EditorTools.h` `ToolStrip`). The Playlist and Piano Roll now
*share* their window's title row (`SharesTitleBar`): the frame keeps the window menu and title on the left and
the window buttons on the right, and the editor lays its toolbar out between them. The Playlist's pattern picker
starts under that row.
The title row carries only controls: the line of keyboard shortcuts that used to sit after the Piano Roll's tools
is gone. Instead both editors (and the automation editor) are `ui::HintSource`s: the hint bar describes, in the
hint markup ([LMB], [RMB], [Ctrl]...), what a press does on the note, clip, edge, handle, ruler, keyboard, lane or
track header under the mouse, with the tool and the modifiers held, plus the note name/velocity or clip name as
the value. The shortcuts stay in the … menu next to each operation. [DOC][pr] [DOC][pl]

**Mouse feel (both editors).** One rule set (`core/EditorGestures.h`, unit-tested) decides, for a tool, a target
and the modifiers, what a left press does; the editors use it for the hit zones, the cursor, the hover highlight
and the hint, so they always agree with the click. Hover shows the item under the mouse (brighter note / outlined
clip, the grip of the edge that would resize, the clip menu button, the key and grid row); the cursor changes per
action and when Ctrl/Shift/Alt are pressed or released. Wheel scrolling glides; zoom stays anchored at the mouse.

### 5.3 Windows: placement, maximise, detach
- The Playlist, Channel Rack, Piano Roll, Mixer and Browser are windows on one desktop. They can be moved,
  resized, overlapped, maximised and detached. They snap to each other while dragged, and Alt stops the snapping.
  [DOC][iface] [DOC][main]
- Keys: F5 Playlist, F6 Channel Rack, F7 Piano Roll, F9 Mixer, F8 plugin picker, Alt+F8 Browser. F12 closes
  all windows. [DOC][view] [DOC][iface]
- The View menu can realign windows to the fresh-install layout and bring off-screen windows back. It has layout
  presets (Desktop, Alternate, Tablet...) and can save a custom layout. [DOC][view]
- **Maximise.** Windows have a maximise/restore button. Right-clicking the toolbar's Playlist icon shows the
  Playlist maximised. [DOC][iface] [DOC][tb] What a maximised window fills is not documented. As far as we know
  it fills the area under the toolbar and keeps its own title bar and toolbar **[unverified]**.
- **Detached.** "Detached" in a window's top-left menu frees it from the desktop, e.g. for a second monitor.
  [DOC][iface] [DOC][cr] [DOC][pl]
- **Mixer.** The manual describes no special docked or bottom-panel mode for the Mixer window. Its "dock to
  left/middle/right" options are about *tracks inside* the Mixer. [DOC][mx] A user asked on the forum for
  bottom-docking as a feature, so it does not exist (https://forum.image-line.com/viewtopic.php?p=1816065). The
  Mixer is an ordinary window like the others: floating, resizable, maximisable and detachable, and closed in a
  fresh project **[unverified]**.
- **Fresh-install layout** (not documented): the Browser on the left, the Playlist filling the rest, the
  Channel Rack floating over it, and the Piano Roll and Mixer closed **[unverified]**. A third-party tutorial
  confirms the Browser on the left (https://beatkitchen.io/guides/fl-studio/01-welcome-the-fl-studio-workflow/).
- The Channel Rack's title bar has its options menu, the channel filter and the global swing knob.
  Shift+wheel on it changes the pattern. [DOC][cr] [DOC][cr-swing]

**LYNX** (`core/Workspace.cpp` `makeDefault`, `ui/Workspace.cpp`):
- The default (start-up) layout has the Browser on the left at full height, the Playlist filling the rest as
  the background, and the Channel Rack floating, sized to its rows, to the right of the Playlist's pattern
  picker and track names (so the arrangement stays readable). The Piano Roll and the Mixer are hidden until
  called (F7, F9) but have their places: the Mixer along the bottom (at least 400 px high), the Piano Roll
  above it, tiled when the desktop is tall enough (≥ 300 px left for the roll), overlapping otherwise.
- While nobody has moved, resized, detached or loaded anything, the layout is "pristine": when the window
  changes size (the window opens small and is maximised, or the user resizes it) the panels take the default
  rects for the new size instead of being scaled, so 1280×720 and 2560×1440 both start with sensible sizes
  (`Layout::relayoutDefault`). Once a panel is touched, its place is remembered and scales with the window.
- The Piano Roll and the Mixer open as floating windows. The Mixer is a wide window over the lower two thirds
  of the Playlist. Like every panel, it is maximised by double-clicking its title, with its title-bar button, or
  by right-clicking its toolbar icon. It is detached from the window menu.
- **Title bars.** Left: the window menu (this panel's own items, then Detached, Maximized and Close) and the
  title. Right: maximise/restore and close, plus "dock back" while detached. The Channel Rack's swing knob moved
  from the main toolbar into its title bar, as in FL.

### 5.4 Browser
- The Browser is on the left by default. Its header can be dragged to dock it left or right, and it can be
  detached or set to auto-hide. [DOC][br]
- Its default tabs are All, Current project, Plugin database, Online content and Starred. Its tree holds folders
  such as Channel presets, Projects, Current project and Plugin database. Search and filters are at the bottom by
  default. [DOC][br]

**LYNX**: the Browser stays docked on the left. Its tree is drawn by LYNX (one-click folders, hover, type icons, the
sample being previewed in red) with a waveform preview strip at the bottom (UI-D). A tabbed tree like FL's is
left for later (P2).

### 5.5 Start-up splash
- FL shows a splash with its version while it starts. A General setting turns it off. [DOC][envgen]

**LYNX** (`ui/Splash.*`, `core/StartupSound.*`):
- A splash of its own: the LYNX SHARE logo (by w3teal) drops onto a dark card and bounces, then "LYNX" and the version slide
  in while the audio engine and the workspace load. It lasts about 2 s.
- A short start-up sound is synthesised in code: a whoosh, a landing "pop" timed to the first bounce, and a
  plucked chime.
- Both can be turned off in Options. They never appear in the command-line modes.

[main]: https://www.image-line.com/support/flstudio_online_manual/html/main_windows.htm

---

### 5.6 Channel Rack rows and Mixer strips (UI-C, 2026-10-15)

FL's Mixer page ([DOC][mx]) describes each insert as: name, peak meter, pan/stereo separation, fader, send
switch/knob, plus routing cables between inserts; the Channel Rack page ([DOC][cr]) puts mute, pan, volume,
the mixer-track field, the channel button and the selector on each row. LYNX keeps that order, drawn its own way:

- **Mixer strip** (60 px, Master 76 px), top to bottom: colour band; record arm, number, effect count (`FX2`,
  blue when on); the name **once**; pan and stereo separation knobs; mute LED and solo; stereo meters (6 px
  each) with a **peak-hold line** and a **clip latch** (click the meter to clear; `core/MeterBallistics.h`) next
  to the fader; the dB read-out; then the **routing lane**: send switch, send level knob, and wires. The list of
  channels routed into an insert moved from under the strip into its hint.
- **Routing lane**: only shown relative to the selected insert, so it never clutters: red wires to its sends,
  a dim wire to Master, blue wires from inserts that feed it.
- **Hover**: every knob, LED, step, route box, slot and header under the mouse is lifted, and the hint bar
  describes it; while a value is dragged its read-out floats next to the control (`ui::showValueTip`).

## Appendix: manual pages cited

Base: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/`

[iface]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_interface.htm
[wf]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_workflow.htm
[sc]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_shortcuts.htm
[sc-pr]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_shortcuts.htm#shortcuts_pianoroll
[sc-pl]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_shortcuts.htm#shortcuts_playlist
[view]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/menu_view.htm
[edit]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/menu_edit.htm
[file]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/menu_file.htm
[tools]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/menu_tools.htm
[pat-menu]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/menu_patterns.htm
[tb]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/toolbar_panels.htm
[tbe]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/toolbar_edit.htm
[tb-snap]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/toolbar_panels.htm#panel_globalsnap
[tb-rec]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/toolbar_panels.htm#record_button
[envgen]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/envsettings_general.htm
[envfiles]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/envsettings_files.htm
[cr]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm
[cr-steps]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm#Stepsequencer
[cr-graph]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm#channelrack_grapheditor
[cr-btn]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm#channelrack_channelbutton
[cr-opt]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm#channelrack_optionsmenu
[cr-filter]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm#channel_filter_groups
[cr-swing]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm#channelmenu_swingmacros
[csm]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/chansettings_main.htm
[csx]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/chansettings_misc.htm
[lay]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/chansettings_layer.htm
[fill]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/stepsequencer_advancedfilltool.htm
[pr]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm
[pr-ev]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm#Pianoroll_eventeditor
[pr-key]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm#pianoroll_KeySignatures
[pr-stamp]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm#chordstamp_icon
[pr-col]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm#Pianoroll_ColorGroups
[prm]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_menu.htm
[prm-snap]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_menu.htm#PianoRoll_Snap
[prm-tools]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_menu.htm#PianoRoll_Tools
[prm-edit]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_menu.htm#pianorollmenu_edit
[arp]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_arpeggiate.htm
[strum]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_strum.htm
[chop]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_chp.htm
[qnt]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_qnt.htm
[art]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_articulate.htm
[flip]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_flip.htm
[rnd]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_random.htm
[lim]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_limit.htm
[scl]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_scale.htm
[flam]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_flam.htm
[claw]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_claw.htm
[lfo]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_lfo.htm
[riff]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_riff.htm
[chord]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_chordprogression.htm
[pl]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm
[pl-clipmenu]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#ClipMenus
[pl-menu]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#Playlist_Menu
[pl-editin]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#EditIn
[pl-work]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#Working_with_Clips
[pl-focus]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#Playlist_ClipFocus
[pl-picker]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#Playlist_picker_panel
[pl-pp]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#ProjectPicker
[pl-trackmode]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#playlist_trackmode
[pl-trackmenu]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#track_rightclick_menu
[pl-time]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#playwithtime
[pl-markers]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm#Playlist_TimeMarkers
[plp]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist_patterns.htm
[ac]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist_automationclip.htm
[perf]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist_performance.htm
[rn]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/recording_scores.htm
[rn-q]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/recording_scores.htm#recbutton_quantization
[ra]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/recording_audio.htm
[ra-pl]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/recording_audio.htm#PlaylistRecording
[ra-loop]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/recording_audio.htm#PlaylistLoopRecording
[rau]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/recording_automation.htm
[rs]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/recording_step.htm
[mx]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm
[mxm]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm
[mxp]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_plugin.htm
[mxt]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_trackprops.htm
[br]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/browser.htm
[br-use]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/browser.htm#UsingBrowserContent
[br-picker]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/browser.htm#Plugin_Picker
[wrap]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm
[link]: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/automation_linking.htm

Full list of the pages above, for readers of the raw file: basics_interface, basics_workflow, basics_shortcuts,
menu_view, menu_edit, menu_file, menu_tools, menu_patterns, toolbar_panels, envsettings_general,
envsettings_files, channelrack, chansettings_main, chansettings_misc, chansettings_layer,
stepsequencer_advancedfilltool, pianoroll, pianoroll_menu, pianoroll_arpeggiate, pianoroll_strum, pianoroll_chp,
pianoroll_qnt, pianoroll_articulate, pianoroll_flip, pianoroll_random, pianoroll_limit, pianoroll_scale,
pianoroll_flam, pianoroll_claw, pianoroll_lfo, pianoroll_riff, pianoroll_chordprogression, playlist,
playlist_patterns, playlist_automationclip, playlist_performance, recording_scores, recording_audio,
recording_automation, recording_step, mixer, mixer_mixermenu, mixer_plugin, mixer_trackprops, browser,
plugins/wrapper, automation_linking (all `.htm`).
