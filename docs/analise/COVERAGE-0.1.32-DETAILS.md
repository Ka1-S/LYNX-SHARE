# DETAILS: small behaviours, LYNX vs FL 2026 / Live 12 (round 190rq sub-round, 2026-11-26)

Source: `origin/wip/rollup032` @ 87cd3c512, read through the worktree `/home/kauan/projetos/lynx-wt/rollup032`
(read only). Paths below are relative to `src/`, except `website/`. The reference behaviour comes from the parent's
FL 26.1.7 / Live 12.4.16 notes, `quality_study_190rl/STUDY.md` (§ C, Q10, Q11, Q16) and `FIX-LIST.md`, and the FL
online manual (channelrack.htm, playlist.htm, basics_new.htm, fetched today). The earlier details rounds
(details190ih … details11_190oa) were checked: anything they already fixed is cited, not reported again.

Verdicts: **same** / **missing** (LYNX lacks the detail) / **wrong** (LYNX does it differently or says it wrongly) /
**own** (LYNX's own, fine) / **can't tell** (how to check it in the app).
Release: **0.1.32** = quality (text, hint or behaviour of an existing control); **0.1.33** = new feature.

---

## 1. Right-click on a Playlist clip deletes it — **same**

- `ui/PlaylistComponent.cpp:7064-7103`: in the Pattern layout a right-click on the clip-menu icon (top left,
  `clipMenuButton`) opens the clip menu; anywhere else on a clip it deletes (`Drag::erase`, with the #250 fade); a double
  right-click is FL's Mute sweep (`:7072-7091`). The hint says so (`ui/EditorTools.cpp:381` "Clip: … [RMB] deletes").
- Touch long-press keeps its own menu (`:16081-16102`), LYNX's own, fine.
- Fix: none. Severity: —.

## 2. Esc closes the focused window — **same** (issue #217, round 190ng)

- `ui/MainComponent.cpp:2584-2590` + `core/EscClose.h:1-25`: an Esc nothing else used closes the focused Playlist / Channel
  rack / Piano roll / Mixer in the Pattern layout (switch "escClosesWindows", on by default). An open JUCE pop-up menu is
  modal and takes the Esc itself, so the window stays (FL: Esc only closes the menu). Plugin / tool windows:
  `ui/PluginWindow.cpp:2137`, `ui/ToolWindow.cpp:55`.
- LYNX's own on top: the first Esc clears a Playlist / Piano roll selection, the next closes (EscClose.h:4-6); Esc first
  gives the keys back from a drop-down (`MainComponent.cpp:2516`, #218) and ends hot-swap (`:2520`). Fine.
- Fix: none.

## 3. Channel rack target-mixer-track box — **wrong (hint text), small differences**

| | FL 2026 | LYNX |
|---|---|---|
| hover hint | "Target mixer track (double-click)" | "<channel> - Target mixer track" + "[LMB] drag or [Wheel] routes  [2xLMB] show in the Mixer  [RMB] next free insert", value "Master" / "Insert N" (`ui/ChannelRackComponent.cpp:2058-2070`, `:3765-3772`) |
| double-click | shows that insert in the Mixer | same (`:3684-3690`, `onOpenMixerInsert`) |
| drag / wheel | changes it | same (digit wheel, `:2070`) |
| right-click | (not captured) | **a menu**: section header, value items, "Next free insert", "Send the selected channels to free inserts Ctrl+L", "Show this insert in the Mixer" (`:3363-3389`) |
| Master | "---" | "-" (`:1335-1341`) |

- **Wrong:** the hint promises "[RMB] next free insert" but the right button opens a menu (the item is in it). Users
  read the hint and expect a one-click action.
- **Fix (0.1.32, text):** `ChannelRackComponent.cpp:3771` → "[LMB] drag or [Wheel] routes  [2xLMB] show in the Mixer
  [RMB] menu (next free insert)"; optionally Master as FL's "---" at `:1340` (`juce::String ("---")`, check that three
  dashes fit the 3-digit box at 100 / 150 %).
- Files: `ui/ChannelRackComponent.cpp` (+ 14 language tables for the hint). Verify: hover shot of the box, FL | LYNX
  pair; `--snapshot` of the rack at 100 / 150 %. Severity: low.

## 4. Channel menu › "Assign to new instrument track" — **missing (the instrument-track icons)**

- Steps LYNX does as FL: the first free Playlist track becomes the channel's instrument track
  (`core/PlaylistOps.cpp:754-767`), named after the channel when it has no name (`:493-494`), the channel gets a free
  mixer insert so the box shows its number (`:483-492`); "Unassign from instrument track" while linked
  (`ui/ChannelRackComponent.cpp:4137-4152`); the header hint value ends "(Instrument track)"
  (`ui/PlaylistComponent.cpp:15607-15611`, `core/PlaylistOps.cpp:798-806`). Same.
- **Missing:** FL marks the channel button and the track header with an instrument-track icon (FL manual, channel rack:
  "Instrument track icon: shown when a Channel is linked to a Playlist Track"). LYNX draws only the user's own icon on the
  channel button (`ChannelRackComponent.cpp:1425-1430`) and on the header (`PlaylistComponent.cpp:3236-3247`, big icon
  `:3215-3223`); nothing shows the link. Only the hint tells.
- **Fix (0.1.32, refinement of an existing state):** when `trackmodes::instrumentTracksOf (song, row)` is not empty and the
  channel has no own icon, draw LYNX's `trackicons::Icon::keyboard` (own vector, not FL's glyph) where the user icon goes
  on the channel button; same on the header of a `PlaylistTrackMode::instrument` track with no own icon. A user icon
  wins (FL shows the user's icon? — check in FL with an icon set on a linked channel).
- Also check in FL whether the new insert takes the channel's name / colour (LYNX's `freeMixerTrack`,
  `core/PlaylistOps.cpp:422-442`, does not rename it).
- Files: `ui/ChannelRackComponent.cpp`, `ui/PlaylistComponent.cpp`. Verify: FL | LYNX pair after "Assign to new instrument
  track" on Kick (rack + Playlist headers), 100 / 150 %, paint gate. Severity: med (the link is invisible today).

## 5. Track mode › Audio track › / Instrument track › — **same**

- `ui/PlaylistComponent.cpp:10819-10875`: Unassigned; Audio track › "New mixer track(s)", a line, every insert as
  "N. Name" (linked one ticked, one linked elsewhere greyed); Instrument track › "Use existing channel ›", a line,
  "More plugins...", a line, the generator list (`quickpick::addGeneratorMenu`) + LYNX's folder-view items
  (`quickpick::addFolderViewItems`). LYNX extra after a line: "Link Playlist tracks to mixer tracks (current project)" (own,
  fine: add, never remove).
- Fix: none. (Check once with a shot that the generator list's categories are FL's order; `quickpick` is shared with the
  rack's Insert menu, already compared by earlier rounds.)

## 6. Mixer insert header: waveform icon when linked to an audio track — **missing**

- `ui/MixerComponent.cpp:2122-2124` draws only the insert's own icon; the link is known (`:4351-4354`, the
  "Assign to new Audio track(s)" / "Unassign" items) but not shown. The header hint is LYNX's gesture line + "Channels: …"
  (`:3598-3600`, `:3527-3537`) — LYNX's hint convention, fine as own (FL shows only the name).
- **Fix (0.1.32):** in the header paint, when any `PlaylistTrackMode::audio` track has `mixerTrack == t` and the insert has
  no own icon, draw `trackicons::Icon::audio` (LYNX's own waveform glyph) in `iconArea (t)`. Hint: append
  "(audio track N)" to the value, LYNX's own (one new string). Cache the "linked" flag per song stamp (the strip paint
  runs at 30 Hz: no per-frame scan of 500 tracks).
- Files: `ui/MixerComponent.cpp`. Verify: FL | LYNX pair with an audio track linked to insert 3; paint gate. Severity: low-med.

## 7. "Route selected channels…" vs "Link the selected channels…" — **wrong (four names for two commands)**

What the user sees for the Mixer's Ctrl+L / Ctrl+Shift+L:
- insert menu › Channel routing: "Route selected channels to this track Ctrl+L", "Route selected channels starting from
  this track Ctrl+Shift+L" (`ui/MixerComponent.cpp:4461-4468`) — FL 2026's wording;
- Mixer window menu: "Link the selected channels to the selected insert", "Link the selected channels to inserts
  starting from the selected one" (`ui/MainComponent.cpp:6026-6027`);
- key map (Search actions `?`, Options › Shortcuts, the F1 sheet): the same "Link …" texts (`core/KeyMap.cpp:543-544`);
- website shortcuts page: "Link …" (`website/_src/wiki/shortcuts.md:244-245`).

The Channel rack's Ctrl+L has four names too: "Send the selected channels to free mixer inserts" (key map,
`core/KeyMap.cpp:226`), "Send to free mixer inserts" (channel menu, `ui/ChannelRackComponent.cpp:4344`), "Send the selected
channels to free inserts" (route-box menu, `:3379`), "Route to free mixer track" (plugin wrapper menu,
`ui/WrapperMenu.h:153`, FL's name). FL's channel name is "Route to free mixer track" (Ctrl+L).

- **Fix (0.1.32, text only):** one FL name per command everywhere:
  - `mixer.linkChannels` → "Route selected channels to this track" (KeyMap.cpp:543, MainComponent.cpp:6026; the
    Mixer window menu may say "…to the selected track" — keep FL's text, the menu acts on the selected insert);
  - `mixer.linkChannelsFrom` → "Route selected channels starting from this track" (KeyMap.cpp:544, MainComponent.cpp:6027);
  - `rack.sendToInserts` → "Route to free mixer track" (KeyMap.cpp:226, ChannelRackComponent.cpp:3379 and :4344);
  - `website/_src/wiki/shortcuts.md:244-245`, `website/_src/wiki/mixer.md:118`, `first-song.md:65` follow.
  - Keep `mixer.selectLinkedChannels` (Alt+L) as is unless FL names it otherwise (check FL's Mixer menu).
  - Old strings stay in the tables until the next string sweep (no user-visible loss).
- Verify: grep shows one text per action id; `?` search for "route" finds both Mixer actions; shots of both menus.
  Severity: med (Search actions for "route" finds nothing today).

## 8. FL 2026 channel-menu items LYNX lacks

From the FL manual (channel rack page, today):
- **MIDI channel through** (tick): "Allows all MIDI Channels from a controller or external sequencer to pass to the
  plugin" (multi-timbral plugins answer per channel; incoming notes take the colour of their MIDI channel). **Missing**:
  no such switch anywhere (grep "through" / "omni" in `core`, `ui`, `engine`: none). LYNX has "Receive notes from"
  (lock to a device + channel, `ChannelRackComponent.cpp:4292-4299`), which is a different thing. **0.1.33** (engine
  routing of MIDI channels to a hosted plugin). Severity: med for multi-timbral plugin users (Kontakt, SampleTank).
- **Auto fill**: not in the manual (greyed in the parent's capture). **Can't tell**: in FL, hover the greyed item and read
  the hint bar, then try it on a channel with steps and in a Loop starter project (the manual's Loop starter has
  "auto-generated steps"); decide after that. Not reported as a gap yet.
- **Show waveform preview** (Sampler channels): listed in the manual without a text. FL 2026 also added the Browser's
  "Show sample waveform". Probably the waveform drawn on the channel's step / pattern row. **Missing / can't tell**: check
  in FL on a sample channel (tick it, shot the rack). LYNX has the waveform only in the channel settings
  (`ui/ChannelSettings.cpp:3544`). 0.1.33 (new drawing in the rack), unless FL shows it inside the channel button.
- **"Create DirectWave instrument..." vs LYNX "Create sampled instrument..."** (`ChannelRackComponent.cpp:4326-4328`):
  LYNX's result is a **Sampler** channel with zones (`ui/AutoSamplerDialog.cpp:57,164`, `autosample::makeSamplerChannel`),
  not a DirectWave channel, although LYNX has its own DirectWave (`InstrumentKind::directwave`,
  `core/DirectWave.h`, `core/NativeEngines.cpp:154`). So the different name is honest today. **Wrong vs FL**, but the
  rename only makes sense with the result: make the auto-sampler fill a `directwave` channel (zones + a program), then
  call it FL's "Create DirectWave instrument...". **0.1.33** (behaviour change of the result); in 0.1.32 leave it.
  Owner rule "FL names stay" applies once the result is DirectWave.

## 9. Playlist track menu: Show ghost notes / Consolidate track(s) / Change color... — **confirmed**

- **Show ghost notes**: absent; the menu's comment says FL items with no counterpart were left out, ghost notes among
  them (`ui/PlaylistComponent.cpp:10746-10748`). LYNX's Piano roll has ghost notes of other channels (Alt+V,
  `ui/PianoRollComponent.cpp:367-377`) but no per-Playlist-track source. **Missing, 0.1.33** (check in FL first what the
  tick does: the clips of that track as ghost notes in the Piano roll?).
- **Consolidate**: LYNX "Consolidate track ›" From track start / From song start (`:11129-11133`), acting on the menu's
  track only (`consolidatePlaylistTrack (engine, track, …)`, `ui/ExportDialog.cpp:2463`). FL 2026: "Consolidate
  track(s)", "on each selected track/s" (manual). **Wrong (text + scope).** Fix 0.1.32: the text "Consolidate track(s)";
  the multi-track scope = loop over `tracksForMenu (track)` with one render window for all (if `quickrender::planTrack`
  can take several tracks; else 0.1.33).
- **Change color**: LYNX "Change color" is a palette submenu (`:10764-10766`); FL's "Change color..." opens a colour
  dialog. LYNX already has that dialog (`ui::showColourPickerPopup`, used by the FL clip menu at `:12726-12728`).
  **Wrong vs FL.** Fix 0.1.32: item "Change color..." → `showColourPickerPopup` on every selected track (one undo step),
  keep the palette as a LYNX submenu "Color palette" right under it (add, never remove; the channel menu already uses
  "Color palette", `:12957`). Same for the rack's channel menu ("Change color" submenu, `ChannelRackComponent.cpp:4022`)
  if FL's channel menu item also opens a dialog (manual: "Chooses a color directly from the selector" — check).
- Files: `ui/PlaylistComponent.cpp`, `ui/ExportDialog.cpp`. Verify: menu shots FL | LYNX; consolidate 2 selected tracks.
  Severity: low (text), med (consolidate scope).

## 10. Empty workspace — **own (fine), small gaps**

- `ui/Workspace.cpp:1789-1806` (#305, round 190ri): a card "F5 Playlist   F6 Channel Rack   F7 Piano Roll   F9 Mixer
  Alt+F8 Browser" in the room left by a docked Browser. Helpful and better than FL's logo for a newcomer; keep.
- Gaps: the keys are a fixed string — a user who rebinds `panel.*` (`core/KeyMap.cpp:86-97`) gets a wrong card; the card
  is not clickable; no version line (FL shows edition + version).
- **Fix (0.1.32, small):** build the text from the key map (`keyMap.describe ("panel.playlist")` etc., hide an unbound
  one); optional: a click on a name shows that window. Version line: LYNX's own choice, low; skip unless the owner wants
  FL's line.
- Verify: rebind F5 in Options › Shortcuts, close all windows, shot. Severity: low.

## 11. Help menu — **missing: What's new, Diagnostics, online manual; F1 differs**

LYNX's Help (`ui/MainComponent.cpp:12261-12330`): Welcome…, First-song tour, Getting started, How to use… ›
(11 windows, `ui/HowTo.h:14`), Show tips and hints, Search actions… ?, Shortcuts F1, Missing files…, Last project import
report, Built-in plugins (online list)…, Test audio output…, Report a problem…, Check for updates…, Go back to …,
Support…, About.
- **What's new**: **missing** from Help. The notes window exists (`ui/Updater.cpp:1453-1466`, "What's new in LYNX SHARE
  {0}") but only behind the update bar's button (`:1209`, `:1238`, `:1258`), i.e. only while an update is offered or
  right after one. Fix 0.1.32: Help › "What's new" → the running version's notes: the release body if cached, else open
  `https://lynx-share.com/updates/` (the site has `website/updates/`). Small.
- **Diagnostics**: FL groups crash log etc. LYNX has the pieces (Test audio output, Report a problem, the plugin
  performance monitor) spread out. Own, fine; optional 0.1.32: a "Diagnostics ›" submenu holding them + "Open the crash
  log folder" (moving items = keep the old places too).
- **Help index / manual**: no direct item to the wiki (only Getting started's "Tutorials" button,
  `ui/MainOnboarding.cpp:325`, and each How-to card's link, `ui/HowTo.cpp:202`). Fix 0.1.32: "Online manual (wiki)" →
  `https://lynx-share.com/wiki/`.
- **F1**: FL = the manual page of the focused window; LYNX F1 = Shortcut sheet (`core/KeyMap.cpp:199`). A FL gesture that
  differs in the Pattern layout. **Needs owner decision** (see Decisions D1).
- Gopher (chat assistant): FL's own, not a gap for LYNX now (0.1.33+ idea only).
- Severity: med (What's new: users ask "what changed?" after each update).

## 12. Guided lesson in the Pattern layout — **missing (0.1.33) + one behaviour to fix now**

- Help › First-song tour switches the app to the Simple layout and saves it, without asking
  (`ui/MainOnboarding.cpp:539-551`). An FL user who clicks it loses his layout until he finds View › Layout style.
  **Wrong (behaviour).** Fix 0.1.32: in the Pattern / Clip-launch layout ask first — "The first-song tour uses the Simple
  layout. Switch to it now? (View › Layout style brings the Pattern layout back.)" [Switch and start] [Cancel]; and the
  tour's end card (or closing it) offers "Back to the Pattern layout". One or two new strings.
- No Pattern-layout guided lesson exists. Getting started for an FL user lists three tasks ("Click steps in the Channel
  Rack to make a beat" › Channel Rack, "Open one of your .flp projects", "Find your plugins") (`core/Onboarding.h:137-147`,
  `:160-199`); How to use the Channel Rack / Playlist are static cards; `website/_src/learn/pattern-tour.md` is a good
  text + picture tour (rack, piano roll, playlist, mixer) but outside the app.
- FL's Step sequencer tutorial (spotlight, 9 cards, action steps that advance on the user's action, Esc ends) =
  **0.1.33 feature**. LYNX already has the pieces: the Simple tour's ring + card (`ui/FirstSongTour.h`,
  `core/FirstSongTour.h:35`, `SimpleWorkflow.h:66` "the tour's ring and card") and the action checks of Getting started
  (`onboarding::tick`). A "Step sequencer lesson" in the Pattern layout can reuse both (own text, clean-room steps).
- Severity: med (newcomers coming from FL tutorials).

## 13. Live: double-click an empty Session slot — **same** (DL-22, details2_190ld)

- `ui/ClipGridComponent.cpp:878-895`: empty slot → `insertEmptyClip` (one bar, `core/SessionOps.cpp:223-228`) and the
  clip opens in the Detail View (`GridPick::openClip`); Alt keeps the old "current pattern" (owner L2). Hint at `:1137`.
- Small difference: Live greys "Insert MIDI Clip" / ignores the double-click on an **audio** track; LYNX makes a MIDI
  clip on any column (`:2804-2806` "Insert empty MIDI clip" always enabled). **Can't tell** whether a LYNX column is an
  audio track in this sense: in the app, double-click an empty slot under an audio clip (Patience set) and see. If it
  makes a MIDI clip there, grey it as Live (0.1.32, behaviour). Live's item name is "Insert MIDI Clip"; LYNX's "Insert
  empty MIDI clip" — Clip-launch look could use Live's text (low).

## 14. Live's Info View — **partly same (box exists), missing the explanations**

- LYNX has the Info View in the Clip-launch layout (`ui/HintBar.cpp:309-365`, rounds 190jn / 190oo; `ui/DetailView.cpp:4112,
  4256`; View › Info View, `MainComponent.cpp:5365`): title + wrapped body, idle text "Point at anything to read about it
  here.". But the body is the hint line itself split at the first colon (`HintBar.cpp:315-322`): mostly the gesture list
  ("[LMB] select [Ctrl][LMB] …"), not what the control is for. `ui::Hint` has no field for a longer text
  (`ui/ValueControl.h:140-151`).
- The "How to use…" cards (`ui/HowTo.h`) explain each window, not each control.
- **Missing (0.1.33):** an optional `about` text in `ui::Hint` shown only by the Info View (title = the control's name,
  then the paragraph, then the gestures), starting with the ~40 Clip-launch controls Live explains (track activator,
  solo, arm, launch button, stop button, crossfader, scene launch, warp …). Own wording (clean-room). Many new strings,
  so not in the 0.1.32 freeze.
- 0.1.32 option (text only): for hints of the "<name> - gestures" form, split at " - " too, so the title is the control's
  name and the body the gestures (today the title is whatever stands before the first colon, e.g. "Kick - Click" for
  the rack's mute hint, or nothing when the line has no colon in its first 48 characters). Small; verify with Info View
  shots on 10 controls.

## 15. Live empty states (Clip-launch) — **partly missing**

| Live | LYNX |
|---|---|
| Session drop area "Drop Files and Devices Here" | no text; the drop area works (`ClipGridComponent.cpp:751-758`) — **missing** |
| Detail, MIDI track without instrument "Drop an Instrument or Sample Here" | "Select a track in the clip grid to see its devices" / "This track has no clips yet: put a clip on it to see its devices" (`DetailView.cpp:397-399`) — LYNX's model (a track's devices come from its clips), own, fine |
| Detail "Drop Audio Effects Here" | "Drop effects here" (`DetailView.cpp:408`) — wording |
| Clip View "No clip selected" | "Select a clip in the clip grid or the Arrangement to see it here" (`DetailView.cpp:4062`) — own, more helpful, fine |

- **Fix (0.1.32, text):** paint "Drop Files and Devices Here" in the empty column right of the last track (only in the
  Clip-launch look, `textSecondary`, cached text: paint gate); in the Live look use Live's "Drop Audio Effects Here" at
  `DetailView.cpp:408`. Two strings. Severity: low.

## 16. Live track title-bar menu vs LYNX's Clip-launch header menu — **missing several**

LYNX (`ui/ClipGridComponent.cpp:2969-3020`): Rename / Freeze Track (Unfreeze), Re-freeze (own), Bounce Track in Place,
Deactivate Track (own), Hide Track (own) / Change color › (submenu).

Live: Cut, Copy, Duplicate, Delete / Rename, Edit Info Text / Freeze Track, Bounce Track in Place / Insert Audio Track,
Insert MIDI Track, Insert Return Track, Group Tracks / Select Track Content / Save as Default MIDI Track / colour palette /
Assign Track Color to Clips.

- **Missing, 0.1.32 (wrap existing operations):** Duplicate (Ctrl+D) and Delete (Del) of the track (the Playlist
  track menu's clone / delete ops), Insert Audio Track (`trackmodes::addAudioTrack`), Insert MIDI Track
  (`trackmodes::addInstrumentTrack`), Insert Return Track (exists in the session mixer menu, `ui/SessionMixer.cpp:560`),
  Select Track Content (select the column's slots), Assign Track Color to Clips (colour every clip of the column).
  In Live's order, LYNX's own items after a line (add, never remove).
- **Missing, 0.1.33:** Cut / Copy / paste of whole tracks, Group Tracks (Ctrl+G in the grid), Edit Info Text (needs the
  Info View's `about`, item 14), Save as Default MIDI Track.
- Colour: Live shows the palette inside the menu; LYNX a submenu. Own, fine.
- Verify: Live | LYNX menu pair (live look), each item once with undo. Severity: med (Clip-launch users reach for
  Insert MIDI Track / Duplicate here first).

## 17. Study § C details

- **(a) Browser jumps to a plugin's preset folder when the plugin opens (FL friction):** LYNX does not do it — no code
  moves the Browser on a plugin window opening (`ui/PluginWindow.cpp`, `MainComponent.cpp` browser calls at :405-672,
  :3146, :6091 are all user actions). **Own, fine** (no friction, no switch needed). If ever added: a switch, off.
- **(b) Space in the audio editor:** `ui/AudioEditor.cpp:2238-2239` — with the editor focused Space plays / stops the
  editor's audio, not the song. **Same as FL on Windows, better than FL Mac.** Fine.
- **(c) Selection over undo / redo:** Piano roll (`ui/PianoRollComponent.cpp:6958-6985`) and Playlist
  (`ui/PlaylistComponent.cpp:16289-16296`) keep the selection **by index** when the item count is unchanged and clear it
  when it changed. Notes have no stable id and are re-sorted after a move (`PianoRollComponent.cpp:1528-1537`); clips are
  re-sorted too (`PlaylistComponent.cpp:6211`). So after "select → move → Ctrl+Z" the highlighted notes can be **other
  notes** than the ones moved. **Can't tell without a run; likely wrong.** LYNX's "Restore selection (LYNX)"
  (`PianoRollComponent.cpp:5597,5745`, `PlaylistComponent.cpp:9681`, round 190qe) is a separate key-based history.
  Fix 0.1.32 (FIX-LIST 12): before undo / redo remember the selection by content (`noteKeys` / `clipKeys`, which the
  selection history already computes; clips by `uid`), re-find after the song is replaced; FL check first whether FL keeps
  it. Verify: a suite test (select notes 3 and 4 of 8, move them past note 6, undo: notes 3 and 4 selected) + Playlist
  twin. Severity: med.
- **(d) Crash-recovery prompt:** only when the session marker survived (unclean exit) **and** a backup was written in that
  session (`ui/MainComponent.cpp:9816-9836`, `:9887-9915`, `safety::crashRecoveryCandidate`); asked once (the marker is
  rewritten), Esc = Ignore. No switch, and none is needed (Live's "every launch" friction does not exist here). **Own,
  fine.** FIX-LIST 2 (open without third-party plugins) is still open.
- **(e) Backup names carry the date:** yes — "<name> (<kind> on YYYY-MM-DD at HHhMM)" (`core/ProjectSafety.cpp:75-80`,
  used at `MainComponent.cpp:9228`), older " (YYYY-MM-DD HH.MM.SS)" (`core/Preferences.cpp:101-106`). **Same** as FL 2026.
- **(f) Plugin monitor fractional %:** one decimal below 10 % (`ui/PluginMonitorWindow.cpp:19`), "-" below 0.05.
  **Same** as FL 2026.1.6.
- **(g) Wheel conventions:** LYNX extra "Wheel: scroll over the track headers, zoom over the arrangement"
  (`core/LynxExtras.h:59-62`, issue #186, off = FL / Live), Audio editor "Wheel scrolls (LYNX)"
  (`core/LynxExtras.h:232-233`). **Own, fine.**
- **(h) Horizontal scroll bar drag never zooms:** the Playlist / Piano roll overview strip moves on X only
  (`ui/EditorTools.cpp` `OverviewBar::mouseDrag` → `gesture::overviewDrag (… getDistanceFromDragStartX …)`); only its
  edges zoom, as documented (`ui/EditorTools.h:325-328`). **Same as FL, no Bitwig friction.**

## 18. Keyboard focus after closing a plugin window with the mouse — **same for detached windows; can't tell for docked frames**

- Detached `PluginWindow`: `~PluginWindow` calls `activateMainWindow()` when it was the active window
  (`ui/PluginWindow.cpp:1898-1905`, round 165q; `ui/WindowOwner.cpp:87-95`), so Space reaches the host again. While
  open, keys the plugin leaves come back (`WindowOwner.cpp:800-830`, issue #96; `PluginWindow.cpp:2026-2028`
  round 190ra). Drop-downs never keep the keys (`ui/KeyFocus.h`, #218, `--focus218shot190ng`).
- **Docked `DockedPluginFrame`:** its destructor (`PluginWindow.cpp:2045-2059`) detaches the native child without handing
  the keyboard back; if the plugin's own HWND had the focus (`hostActive`, `:2064-2073`), Windows leaves no focused
  window and plain keys may arrive as WM_SYSKEYDOWN or nowhere until a click. **Can't tell from source.** Check: dock a
  hosted VST3 (Surge XT), click into its GUI, close it with the frame's X, press Space (Windows under the GUI lock with
  safeinput; Linux Xvfb + xdotool). Fix if it fails (0.1.32, behaviour): in `~DockedPluginFrame`, if `hostActive()`,
  `keyfocus::giveBackNow (*mainWindowComponent())`. Sandboxed plugins (other process) same check. Part of FIX-LIST 5.
  Severity: med (Q10 is a common complaint).

---

## Decisions for the owner (no silent parking)

- **D1 — F1 in the Pattern layout.** (a) FL-exact: F1 = help for the focused window (its How-to card, or its wiki page),
  the Shortcut sheet moves to Ctrl+F1 / Help menu; (b) keep F1 = Shortcut sheet (LYNX today); (c) a switch, default (a)
  in the Pattern layout. Recommended (c) with (a) as default: FL's gesture, and the How-to cards already exist per window.
- **D2 — First-song tour from the Pattern layout.** (a) ask before switching to Simple (fix in item 12); (b) keep the silent
  switch. Recommended (a).
- **D3 — "Create sampled instrument..."** (a) 0.1.33: make it a DirectWave channel and use FL's name; (b) keep the Sampler
  result and LYNX's name. Recommended (a).

## Top detail gaps (for the parent)

1. Ctrl+L / Ctrl+Shift+L have 2-4 names each ("Route…" / "Link…" / "Send…"); Search actions for "route" misses the key-map ones (item 7, 0.1.32 text).
2. Instrument-track link invisible: no instrument-track icon on the channel button / track header (item 4, 0.1.32).
3. Selection after undo is kept by index while notes / clips are re-sorted: undo of a move can highlight other notes (item 17c, 0.1.32, test first).
4. Help › First-song tour silently switches an FL user to the Simple layout; no Pattern-layout lesson (item 12; ask = 0.1.32, lesson = 0.1.33).
5. Clip-launch track header menu lacks Live's Duplicate / Delete / Insert Audio / MIDI / Return Track / Select Track Content / Assign Track Color to Clips (item 16, 0.1.32 wrappers).
6. No Help › What's new (the notes window exists only behind the update bar) and no direct manual link (item 11, 0.1.32).
7. Track menu "Change color" is a submenu (FL: dialog) and "Consolidate track" works on one track (FL: "track(s)", selected) (item 9, 0.1.32).
8. Mixer insert linked to an audio track has no waveform mark; rack route-box hint says "[RMB] next free insert" but RMB opens a menu (items 6 and 3, 0.1.32).
