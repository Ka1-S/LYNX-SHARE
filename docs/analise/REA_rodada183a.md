# Round 183a: the Detail View of the Clip-launch workflow (Clip View / Device View), what it is based on

Clean room: LYNX's own code and art. Sources are the reference DAW's published manual, its own shipped lessons and sets
(text and data read, pictures looked at, nothing copied), its binary's string and RTTI tables, and screenshots of the
running program. No trademarks, names or art of the reference DAW in LYNX's UI.

Tags:
- **[MANUAL]** the published manual (ableton.com/en/manual, Live 12).
- **[LESSON]** Live 12's own built-in lessons (`Resources/Builtin Lessons/*/LessonsEN.txt` and their pictures).
- **[DATA]** read from Live's own shipped sets (`Core Library/Lessons/Demo Songs/*.als`, decoded in `ableton/live11-sets/shipped12`).
- **[BIN]** Live 12.4's string and RTTI tables (`ableton/live-exe-strings*.txt`, round 175a).
- **[SHOT]** seen in the running Live 12.4 Trial (shots under `/mnt/e/lynx-work/detail183a/live/` and round 175a's `ableton175a/live/`).
- **PROVEN** = one of the tags above shows it. **INFERRED** = deduced, not seen.

## 1. What the Detail View is
| Fact | Source | Status |
|---|---|---|
| The Detail View sits at the bottom of the main window, under the Session/Arrangement area, beside the Info View | [SHOT] `ableton175a/live/m3-live-clipview2.png` | PROVEN |
| It shows either the **Clip View** (the selected clip) or the **Device View** (the selected track's devices) | [MANUAL] Live Concepts 3.12; [LESSON] Playing Software Instruments | PROVEN |
| **Shift+Tab** toggles between Clip View and Device View | [LESSON] Creating Beats ("the $ShiftMod-Tab shortcut ... toggles between the Clip View ... and the Device View"); [SHOT] d05 View menu "Toggle Clip/Device View Shift+Tab", d07→d08→d09 | PROVEN |
| View menu (Live 12.4): Clip View Ctrl+Alt+3, Device View Ctrl+Alt+4 (ticked items), Toggle Clip/Device View Shift+Tab, Expand Clip View Ctrl+Alt+E, **Hide Device View Ctrl+Alt+L** (hides the Detail View), Arrange Clip View Panels Automatically / Horizontally / Vertically, MIDI Note Editor Shift+Alt+1, Envelope Editor Shift+Alt+2, Expression Editor Shift+Alt+3 | [SHOT] d05 | PROVEN |
| Clicking a clip selects it but leaves the Device View showing | [SHOT] d07 | PROVEN |
| Clicking the two **view selectors** in the bottom-right corner of the window also switches | [LESSON] Playing Software Instruments, picture `DetailOverviewsInst.png` (two selector tiles, bottom right) | PROVEN |
| Live 12: both views can be shown at once, stacked; triangle buttons next to the selectors, **Ctrl+Alt+3** (Clip View) and **Ctrl+Alt+4** (Device View); Alt+click on a selector opens both; with both open, Shift+Tab moves the focus between them | [LESSON] What's New "Toggle Clip View Alongside Device View"; [MANUAL] 3.12 | PROVEN |
| Double-clicking a clip opens the Clip View; double-clicking a track's title bar opens the Device View | [MANUAL] 3.12; [LESSON] ("double-click its title bar - the MIDI notes ... will appear in the Clip View") | PROVEN |
| Alt+3 focuses the Clip View, Alt+4 the Device View (keyboard navigation) | [LESSON] What's New "Keyboard Navigation" | PROVEN |
| The set stores which views are open: `ViewStateMainWindowClipDetailOpen`, `ViewStateMainWindowDeviceDetailOpen` (two booleans, so both can be open) and the same for a second window | [DATA] Patience demo set | PROVEN |
| Live's own view names are `Detail`, `Detail/Clip`, `Detail/DeviceChain`; RTTI shows `TDetailViewState`, `TDetailViewLayout`, `ADeviceDetailView`, `AClipDetailView`, and `IsDetailOpen(DetailType)` | [BIN] | PROVEN (names only) |
| The Clip View can lay out its panels `ClipDetailViewLayoutAuto / Horizontally / Vertically` | [BIN] | PROVEN (names only) |

## 2. Device View
| Fact | Source | Status |
|---|---|---|
| Shows the devices of the **selected track**: MIDI effects, the instrument, then audio effects, left to right in signal order ("Signals in a device chain always travel from left to right") | [MANUAL] Working with Instruments and Effects | PROVEN |
| Audio, group and return tracks hold audio effects only | [MANUAL] 3.12 | PROVEN |
| The free space after the last device shows the drop hint "Drop Audio Effects Here", with devices or without | [SHOT] `ableton175a/live/m3-live-clipview.png` (empty), `detail183a/live/d01-chain.png` (three devices) | PROVEN |
| A deactivated device: its activator goes dark and its controls dim | [SHOT] d02 | PROVEN |
| A folded device: a narrow column with the activator on top, the name written upwards, its title buttons below | [SHOT] d03 | PROVEN |
| The title bar's context menu: Cut, Copy, Rename, Edit Info Text, Delete, Group, Group To Drum Rack, Fold, Show Preset Name, Compare: Switch to B / Copy A to B, Save as Default Preset, Hi-Quality | [SHOT] d04 | PROVEN |
| Input and output meters enclose the chain (left: a MIDI activity column on a MIDI track; right: a stereo output meter) | [LESSON] `DeviceViewMeters.png`; [MANUAL] "meters appear between devices" | PROVEN (ends); between devices: see §6 |
| Title bar: the **Activator** (on/off, a round switch at the left), the name, then Hot-Swap, Save Preset, the Options button, and optional toggles (expand arrow, scale awareness) | [MANUAL]; [LESSON] What's New "Device Title Bar Updates", `DeviceTitleBar.png` | PROVEN |
| A deactivated device passes the signal unprocessed | [MANUAL] | PROVEN |
| **Fold**: double-click the title bar, or "Fold" in the title bar's context menu | [MANUAL] | PROVEN |
| Fold state is saved per device: `IsFolded`; the separate expanded section: `IsExpanded` | [DATA] 49 folded / 213 unfolded devices in the demo set | PROVEN |
| The selected device is saved per track: `SelectedDevice` | [DATA] | PROVEN |
| **Reorder**: drag a device by its title bar and drop it next to another one; drag into the Session/Arrangement moves it to another track | [MANUAL] | PROVEN |
| **Add**: drag a device into the Device View, or double-click / Enter on it in the browser to append to the selected track | [MANUAL] | PROVEN |
| Delete: select the title bar, Delete/Backspace; cut/copy/paste/duplicate; "Pasted devices are inserted in front of the selected device" | [MANUAL] | PROVEN |
| Devices are placed, moved and deleted without interrupting the audio | [MANUAL] | PROVEN |
| A plug-in device shows parameters only after they are added in Configure mode; its own editor opens in a window | [MANUAL]; [LESSON] Playing Software Instruments ("The plug-in's custom editor window will open automatically") | PROVEN |

## 3. Clip View
| Fact | Source | Status |
|---|---|---|
| Audio clip: a left column of panels (Clip: name, Start/End with Set, Loop, Position, Length, Signature, Groove, Scale; Audio: Warp, Follow, warp-mode chooser, BPM with ÷2 ×2, Reverse, Edit, Fade, RAM, HiQ; Gain slider; Pitch knob with semitones and cents), then the sample editor with tabs **Sample / Envelopes**, a beat ruler, warp markers above the waveform and the loop brace | [SHOT] `ableton175a/live/m3-live-clipview2.png` | PROVEN |
| MIDI clip: the same Clip panel, a Notes / transform-tools column, the note editor with a piano key column, velocity lane below | [LESSON] What's New `ClipDeviceView.png`; [MANUAL] 3.12 | PROVEN |
| Session clips also show the Launch panel (launch mode, quantisation, follow actions) | [MANUAL] 3.12 | PROVEN |
| The Info View text for the sample display: "This is a display of the clip's waveform on a local beat-time axis. [Ctrl + Alt + Drag] Scroll Display [Ctrl + Scroll Wheel] Zoom In/Out [Shift + Ctrl + Drag] Slide Waveform" | [SHOT] `m3-live-clipview2.png` | PROVEN |
| Clip envelopes: the Envelopes tab with a **device chooser** (Clip, each device, Mixer, MIDI Ctrl) and a **control chooser**; envelopes of edited controls get an LED; Session clips have Automation (absolute, red) and Modulation (relative, blue) | [MANUAL] Clip Envelopes | PROVEN |
| Clip envelope loops can be unlinked from the clip loop | [MANUAL] Clip Envelopes | PROVEN |

## 4. Sizes (measured on 1920 × 1080 shots at 100 %) [SHOT]
| What | Size | Shot |
|---|---|---|
| Clip View (audio), frame to frame | 228 px (inner 224) | `ableton175a/live/m3-live-clipview2.png` |
| Device View with devices | 188 px (y 807–995) | `detail183a/live/d01-chain.png` |
| Device title bar | 17 px | d01 |
| Folded device | a column of about 20–22 px | d03 |
| Status bar with the two view selectors | 19 px; each selector about 197 px wide | `m3-live-clipview2.png` |
| Stacked (Clip View above Device View) | the Clip View takes the window's full width over the Info View; the Device View keeps its height | d10 |

## 5. What LYNX already has to reuse
| Piece | Where | Reuse |
|---|---|---|
| Mixer insert slots: on/off (`EffectSlot::enabled`), dry/wet (`mix`), rows, A/B, presets, add menu, slot menu, Browser drops (effects, `.fst`, generators) | `ui/MixerComponent.cpp` `EffectsPanel` | the chain's model and actions; the drop rules (`BrowserComponent::isEffectDrag / draggedEffect / isGeneratorDrag`) |
| Reorder and insert keeping FL's row places | `core/MixerOps.h` `moveEffectSlot`, `moveEffectToRow`, `insertEffectAtRow`, `removeEffectKeepRows`, `effectRows` | as is |
| Each built-in effect's parameters (names, defaults, value text) | `effectInfo(kind).params`, `formatEffectParam` | the knobs of an unfolded device |
| Effect windows | `PluginWindows::open`, `ParametricEqWindows::open`, `NativeEffectWindows::open` (as `EffectsPanel::openEditor`) | the device's "open window" button |
| Knob drawing and dragging | `theme::drawKnob`, `ui::ValueDrag`, `ui::ValueSpec` with automation targets | device knobs (right-click automation menu as in the mixer) |
| Plugin pictures | `ui/PluginThumbs.h` | a plugin device's face |
| A track's insert and instrument | `PlaylistTrack::mode / mixerTrack / channel` (FL's track mode) | the selected track → its chain |
| Clip grid selection | `ui/ClipGridComponent.cpp` (`selectedTrack`, `selectedScene`, private) | needs a callback (coordinate with 181a) |
| Clip-launch layout and grid host | `MainComponent::setClipLaunchWorkflow`, `PlaylistArea::setClipGrid` | the Detail View docks in `PlaylistArea`, only in Clip-launch workflow |
| Piano roll | `MainComponent::pianoRoll` in workspace panel `Panel::pianoRoll` | Clip View (MIDI) hosts it (slice 2) |
| Audio editor with warp lane | `ui/AudioEditor.cpp`, round 175a M3 (wip/ableton175a-m3, not landed); embeddable as `makeAudioEditorForSnapshot` shows | Clip View (audio) (slice 2, with 175a) |
| Clip properties | `ui/ClipProperties.cpp` `showClipProperties` | the clip panel fields |

## 6. Design proposal (coordinator decisions D1a / D2a, 2026-11-08)
D1a: the Clip View docks LYNX's **existing** Piano roll and Audio editor. D2a: the Device View is a **view** of the selected
track's existing insert slots, as a horizontal chain, with **no new model**.

### 6.1 Where it lives
- **Clip-launch workflow only.** The Detail View docks at the bottom of the Playlist panel, under the grid and the
  Playlist (`PlaylistArea`, next to round 181a's `setClipGrid`). In Pattern workflow it is `nullptr`: no component, no
  timer, nothing in the layout file. The workspace's panel list and saved layout don't change.
- Height: Live's is about 225 px at 100 % (§4). LYNX starts at the measured height, the top edge drags (min 140 px, max
  60 % of the panel), and the height is kept in the UI settings (`detailHeight`), not in the project.
- **View selectors** at the bottom-right of the Detail View: two tabs, "Clip" and "Devices" (Live: two selector tiles
  in the window's bottom-right corner [LESSON]). On the left of that strip, the context: "Track 3 · Insert 5 Bass" or the
  clip's name.
- Keys, Clip-launch workflow only (all four chords are free in LYNX's key map, and remappable as usual):
  **Shift+Tab** Clip ↔ Devices (opens the Detail View if hidden) [PROVEN]; **Ctrl+Alt+3** Clip, **Ctrl+Alt+4**
  Devices [PROVEN]; **Ctrl+Alt+L** show/hide the Detail View [PROVEN, d05: "Hide Device View Ctrl+Alt+L"]. In the
  reference DAW Ctrl+Alt+3/4 toggle each view on its own (both can show); LYNX's slice 1 switches between them, and
  "show both" comes with slice 2. A double-click on a grid
  column's title shows Devices, and on a filled slot shows Clip [MANUAL].
- Live 12 can stack both views [LESSON]. LYNX: slice 2 adds "Show both" (Alt+click on a selector, as in Live) once the
  Clip View exists.

### 6.2 Device View (slice 1)
Which chain: `core/DetailChain.h` `forTrack()` (tests `detail183a_*`):
1. FL's instrument track: its channel, then the insert the channel plays through;
2. FL's audio track: its linked insert;
3. otherwise the clips decide (Playlist + grid): one channel = that channel and its insert; several channels on one
   insert = that insert; several inserts = a picker ("Insert ▾") at the chain's left, preset to the first.

Left to right [MANUAL "left to right"; LESSON meters]:
```
 | in |[● Sytrus      ⧉]|[● Fruity Reeverb 2 ⧉]|[●]|[● Facet EQ ⧉]| + |out|
 |  ▮ | (picture)        | (o) (o) (o) (o)      | R | (o) (o) (o)    |   | ▮▮ |
 |  ▮ | (o)Vol (o)Pan    | (o) (o) (o) (o) Mix  | e | (o) (o) Mix    |   | ▮▮ |
                                                ^ folded device: a narrow column, name written upwards
```
- **Instrument card** (when the chain has a channel): title bar with on/off (the channel's mute, inverted), its name and
  the window button (the channel's own window). Body: the plugin picture (`ui/PluginThumbs`), then Volume and Pan knobs.
- **Effect cards**, one per occupied insert slot, in chain order (FL's empty slot rows are not drawn; the order is the
  slots' order). Title bar 18 px: the **on/off LED** (`EffectSlot::enabled`, the mixer LED's edit), the name
  (`quickpick::slotName`), the **window button** (the mixer's `openEditor`: plugin window, Parametric EQ window or
  native editor). Body: the effect's first parameters as knobs (`effectInfo(kind).params`, two rows, up to 8) and Mix;
  the same value specs, hints, double-click reset and right-click automation menu as the mixer knobs. A third-party
  plugin shows its picture and the window button (Live shows a plug-in's parameters only after Configure [MANUAL]).
- **Fold**: double-click the title bar, or Fold/Unfold in its menu [MANUAL]. Folded = a 22 px column with the LED and the
  name written upwards. UI-only state per (insert, slot instance), **not saved** (D2a: no model change; Live saves
  `IsFolded` per device [DATA], so .als import/export ignores it: noted, not built).
- **Select**: click the title bar (red outline). **Delete**: Del deletes the selected effect (`removeEffectKeepRows`,
  undoable). **Right-click** the title bar: the mixer slot's own menu (same items, same code), plus Fold/Unfold.
- **Reorder**: drag a title bar; an insertion line shows between cards; drop moves the slot
  (`mixerops::moveEffectSlot`, FL's rows keep their places) [MANUAL].
- **Drop from the Browser**: an effect from the Plugin database, an effect preset (.fst) or a plugin goes in at the
  insertion line (`insertEffectAtRow`); onto a card it replaces it, as on a mixer slot. A generator dropped makes a new
  channel routed to this insert, as on the mixer (round 167u). With 10 effects the insert is full: the drop is refused
  with a hint.
- **"+" card** at the end: the mixer's add menu for the next free slot. The free room after the last card shows
  "Drop effects here", with cards or without [SHOT d01] (the round's first note said "empty only": the session showed
  otherwise).
- **Off**: the LED dark and the card's controls dimmed [SHOT d02].
- **Meters**: the insert's input on the left and output on the right [LESSON]; between cards: not built (LYNX has no
  per-slot level; noted).
- A wider chain than the view scrolls sideways (bar under the cards; Shift+wheel).
- Everything goes through `AudioEngine::editSong`, as the mixer's slots do, so undo, live sessions and the Mixer stay in
  step; the Mixer's effects panel and the Device View show the same slots.

### 6.3 Clip View (slice 2)
- A pattern clip: the existing **Piano roll** component, moved into the Detail View (the floating Piano roll panel is
  hidden while it is docked and gets it back when the Detail View closes or the style goes back to Pattern workflow).
- An audio clip: the existing **Audio editor** with round 175a's warp lane, through 175a's
  `makeAudioEditorPanel (engine, channel, clip, showWarp = true)` (175a adds it in M3; this slice waits for M3 on main).
  The panel is rebuilt when the selection changes or an edit renumbers the clip or its channel (175a's note).
- Left column (Live's Clip / Launch panels [SHOT]): the grid clip's launch settings already in round 181a's grid menu;
  name, colour, start offset and length from Clip properties. Proposed as a narrow column in slice 2b, after the
  editors are docked.

### 6.4 Clip envelopes (slice 3, needs a decision)
- Pattern clips already carry FL's per-pattern event lanes (`Pattern::events`, the Event editor): an "Envelopes" tab in
  the Clip View can show them with a target chooser (Live's device/control choosers [MANUAL]). Existing model: in scope.
- Audio clips have no envelope model in LYNX (Live: Gain, Transposition, Sample Offset, and device parameters
  [MANUAL]). That needs a model change: proposed as out of scope here.
- Live's modulation (relative) vs automation (absolute) per Session clip [MANUAL]: not in LYNX's model; out of scope.

### 6.5 Best way?
- **FL:** no chain view. The Mixer's effects panel lists the selected insert's 10 slots vertically (REA round 104,
  `EffectsPanel`); the instrument is in the Channel rack and its window. Each effect's controls live in its own window.
- **Live:** the horizontal Device View under the grid, with each device's controls inline, folded or not, and Shift+Tab
  to the Clip View (§1–§3).
- **Users:** FL users ask for one place per track to flip through its plugins instead of many windows
  ([IL forum: Mixer workflow, plugin handling](https://forum.image-line.com/viewtopic.php?p=2029096); a channel-rack-style
  mixer like Live/Bitwig: [IL forum](https://forum.image-line.com/viewtopic.php?p=2001576)). Live users complain that
  plugin windows don't follow the selected device and that no key opens the selected device's window
  (research-web-2.md, "Live complaints").
- **Verdict:** in Clip-launch workflow, Live's layout and gestures over LYNX's own inserts (D2a). Two LYNX additions are
  proposed as options, not defaults: (a) the window button on every card, and the selected card's window on **W**,
  answering the Live complaint above; (b) the Mixer selects the same insert when a card is selected. Pattern workflow
  is untouched.

## 7. Open points
- Per-device meters between cards: no per-slot level in LYNX.
- Fold state is not saved (D2a); the reference saves `IsFolded` [DATA].
- The reference's left meter column on a MIDI track (note activity) [LESSON]: not built in slice 1; LYNX shows the
  output meter on the right only.
- Selection from a Playlist track header (Both / Song view): slice 1 follows the grid's selection only.

## 8. Live GUI session (2026-11-08 04:56–05:01, round 183a)
- GUI lock held 04:54:44–05:01:44 (holder pid 63895), idle gate passed 04:56:43, every input through `act.ps1` /
  own `rclick.ps1` (fresh shot + `safeinput.ps1` + licence-window guard); the input wall logged no new drop.
- Set: Live's own shipped demo, `Core Library/Lessons/Demo Songs/Chuck Sutton - Patience (Live 12 Suite Demo).als`,
  opened read-only in effect (closed with "No" to the save prompt; nothing saved).
- Dialogs: the trial info box "Saving and exporting were successfully activated / Time remaining: 28 days" → OK
  (`trialok.ps1`, pre-approved); no licence or PACE box appeared; no plugin scan ran.
- Shots in `/mnt/e/lynx-work/detail183a/live/`: d01 chain (Drift, Utility, Audio Effect Rack), d02 Utility off,
  d03 Utility folded, d04 the title bar's context menu, d05 the View menu, d07–d09 Shift+Tab both ways, d10 Clip and
  Device View stacked (Ctrl+Alt+3).
- Prefs: no setting changed. `Preferences.cfg` differs from the 04:19 backup only in Live's own runtime state (browser
  filter rows) and a recent-file entry left by round 182b's session in between; backup and after-copy are kept next to
  the shots. Not overwritten (that would erase 182b's state too).
- Closing: `finish.ps1` skipped Live's ordinary save prompt because its licence filter matches Live's own "Track /
  Clip / Device Activator" labels (bare "activat"); `guard.ps1`'s stricter filter does not, so "No" was clicked
  through `act.ps1`. Live and its helper processes exited; AbletonAudioCpl (pid 20004) was running before the session.

## 9. Slices built
### Slice 1: Device View (approved by the coordinator 2026-11-08, LYNX|Live pair `/mnt/e/lynx-work/detail183a/pair-slice1.png`)
- `src/ui/DetailView.*`, `core/DetailChain.h` (`forTrack`, `insertEffectAt`, `moveEffectBefore`), the grid's pick listener,
  four key-map actions (Clip-launch only), the Mixer's additive hooks (menu target, `extendSlotMenu`, knob accessors,
  shared A/B spare, `shownLevel`), `--detailshot`.
- Pattern workflow unchanged: `--snapshot` base bbb948055 vs branch, both cloud builds: 152/156 identical; the 4 others
  are the new actions count (257 → 261), the profile path, the working-time clock and the tempo tapper's known noise.
- The slot's dry/wet reads "Slot mix" on effects that have a Mix parameter of their own (found in the pair).
- A card hook (`DetailCard`, `DetailView::effectCardFactory` / `instrumentCardFactory`) for round 183c's racks.

### Slice 2: Clip View docks the Piano roll (D1a)
- A pattern clip selected in the grid shows the existing Piano roll in the Clip page. Its workspace panel lets go of it
  (`Workspace::releaseContent`) and takes it back first whenever the panel is shown in any way (`onWillShow`), when the
  Clip page hides, or when Pattern workflow is chosen; the main window gives it back before its members are destroyed.
- Keys: after a click in the docked Piano roll the keys are its own (a global mouse listener, only while docked).
- The audio clip side waits for round 175a's `makeAudioEditorPanel (engine, channel, clip, showWarp)` (M3).

### Slice 3: Envelopes tab (pattern clips)
- A pattern clip's Clip page has the reference's **Notes | Envelopes** tabs [SHOT d08]. Envelopes hosts the existing Event
  editor (`makeEventEditor`) of the clip's pattern, preset to its first event lane (else its channel's volume). Its own
  target box picks the target: one chooser where the reference has two (device, then control) [MANUAL]. Existing model
  only (`Pattern::events`).
- Audio clip envelopes: round 177d's #24 (not started) will provide `makeClipEnvelopeEditor (engine, clip)`; it goes in
  the same tab when it lands.
- Not built: the reference's Shift+Alt+1 / 2 / 3 (Note / Envelope / Expression editor) [SHOT d05]; modulation vs
  automation per clip (no model).
