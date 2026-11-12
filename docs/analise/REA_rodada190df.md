# REA round 190df: FL's multilink menu, the plugin menu's spare state and "Link all parameters...", the sampler's File row and channel envelope presets

Goal (coverage programme of 0.1.27, WHAT-LYNX-LACKS items 22 and 30). Method: static study only of the FL Studio 2026
engine library on the study install (form and menu resources, strings by address, the behaviour of the handlers read
with headless Ghidra on the shared analysed project), plus two FL screenshots already on disk (the multilink menu from
the live session of round 190cq and a sampler channel window of round 188a). FL was not started. Clean-room: behaviour
in words; no decompiled text was kept in the repo. Code: `src/core/FlControlMenus.h` (no JUCE), `src/ui/RemoteControl.cpp`
(multilink menu), `src/ui/PluginWindow.cpp` (plugin menu items), `src/ui/ChannelSettings.cpp` (File row, FL's page for
samplers running FL's voice), `src/ui/FlVoicePanel.cpp` (envelope presets); tests `tests/FlCtl190dfTests.inc`; offscreen
checks `LYNX --flctlshot <prefix>`.

Markers: [FORM] form resource, [H 0x...] the handler at that address (image base 0x400000), [STR] a string read by
address, [SHOT] an FL screenshot, [LIVE] a live FL answer (round 190cq).

## LYNX choices
- The plugin menu keeps FL's names: "Spare state", "Store in spare state", "Flip with spare state" (FL-native
  equivalents keep FL's names; coordinator decision).
- The Multilink button's menu is FL's menu first, in FL's order; LYNX's own "Link to controller" / "Override global
  links" (round 177b-4, one-step linking of the queued controls) stay after it, below a line.
- FL's Envelope / instrument page (FlVoicePanel) is the Envelope tab of samplers that run FL's voice (an FL project's
  samplers). LYNX's own samplers keep LYNX's envelope page (switching their voice would change how they sound).
- Left out (no dead stubs): Browse parameters (LYNX's Browser lists no plugin parameters), Save channel state as... /
  Browse channel states (LYNX writes no FL channel-state file), the waveform editing inside the window (not settled
  statically), the 17 items FL shows above the multilink menu's caption while multilink is off.

## Item 22: control right-click menu / plugin menu

### 22a. The multilink menu (MLWheelMenu, `wrapper_wheel-0133...0145`)

| Fact | Evidence |
|---|---|
| It pops up on a right-click on the toolbar's Multilink button | [LIVE] Q9, [SHOT] 09a |
| Order: caption; "Automation" caption; Edit events; Init song with these positions; Init pattern with these positions; line; Create automation clips; "Remote control" caption; Link to controllers...; Override global links...; "Value" caption; Randomize; Humanize; line; Cancel | [FORM] MLWheelMenu, [SHOT] 09a |
| No other items above the caption while multilink is on (LYNX had "Link to controller" / "Override global links" there since round 177b-4) | [SHOT] 09a |
| First caption: multilink on: "%d parameter selected" (1) / "%d parameters selected" (2+); off: "No parameter selected" (none), "Last tweaked parameter" (one), "Last 2 tweaked parameters" (two); all drawn as section captions | [H 0x1234ae0] MLWheelMenuPopup; [STR 0x1234ec0...] |
| Multilink off: the items act on the last two tweaked parameters (copied into the queue first) | [H 0x1233e10] (reads the 2-entry last-tweaked array) |
| Every item is enabled when there is at least one parameter | [H 0x1234ae0] (enable = count > 0 for the items after the caption) |
| Cancel and the line above it are visible only while multilink is on | [H 0x1234ae0] (two items' visibility = multilink flag) |
| Multilink off: FL also fills the menu's top part with a caption naming the last tweaked parameter ("No parameter" if none) and 17 items copied from another menu (hidden while multilink is on) | [H 0x123d1d0]; which menu and which items: **live check** |
| Edit events: FL's event-editor opener once per parameter, then the queue ends | [H 0x1234110] |
| Init song / Init pattern with these positions: each parameter's current value written at position 0 of the song's init list (Tag 0) or of the current pattern's events (Tag 1), then the queue ends | [H 0x12345c0] -> [H 0x1052040] |
| Create automation clips; Link to controllers... / Override global links... (the Remote control settings window stepping through the queue) | round 190bt (`link190bt/HANDOFF.md`) |
| Randomize: each value = Random(2^30 + 1) / 2^30 (anywhere in the range) | [H 0x12349a0]; Random(n) = (rand32 * n) >> 32 [H 0x40a140]; [LIVE] 80 % -> 8 % / 92 % |
| Humanize: value (0...2^30) - 2^27 + Random(2^28 + 1), below 1 -> 0, above 2^30 -> 2^30 (at most 1/8 of the range either way) | [H 0x12343f0]; [LIVE] steps of at most ~11 points |
| Randomize / Humanize end the queue only while multilink is OFF; with multilink on the queue stays (they can be repeated) | [H 0x12349a0], [H 0x12343f0] (`if multilink == 0: end`); [LIVE] Humanize applied 3 times in a row |
| The end of the queue also switches multilink off | [H 0x1233ea0] |

### 22b. The plugin menu (PluginMenu, `wrapper_wheel-0075...0080`, `-0107...0109`)

| Fact | Evidence |
|---|---|
| Sections "Spare state": Store in spare state, Flip with spare state (hint "A/B compare"); "Parameters": Link all parameters..., Browse parameters; "Channel": Save channel state as... (hint "Save channel state as a preset"), Browse channel states | [FORM] PluginMenu |
| Store in spare state: the plugin's own state (flags 0x30: plugin data, not the channel) saved to a temporary .FST kept per plugin instance | [H 0x1233080] -> [H 0x12bb790] |
| Flip with spare state: with a kept state, the current state is saved aside, the kept one loaded, and the two swap (A/B); without one nothing happens; errors "An error occurred while saving / loading the spare state." | [H 0x1233060] -> [H 0x12bb880] |
| Spare state lives in temporary files, outside the project and the undo history | [H 0x12bb790] (temp folder path) |
| Link all parameters...: a list of every parameter of the plugin (count from the plugin), handed to the Remote control settings window (stepping through them) | [H 0x124dea0] -> [H 0xf83a20] |
| Browse parameters: opens FL's browser at a special node (-106, the plugin's parameter list in the browser) | [H 0x123d070] |
| Save channel state as...: a save dialog, extension ".fst", filter "FL Studio state file (*.fst)", a remembered folder (two slots), the channel written with flags 0x20 (the whole channel), the browser refreshed; a drag variant ("Dragging %s") exists | [H 0x11edf90] |
| Browse channel states: opens the browser at node 0x3f7 (the channel presets folder) | [H 0x123ced0] |

## Item 30: channel settings

### 30a. The sampler's File row (`chansettings-0088...0092`)

| Fact | Evidence |
|---|---|
| Row (GrooveBox, top left of the Sample tab): "File" label, the sample's file name (path ellipsis, its own context menu), then right-aligned Load (folder glyph), Locate sample in browser, Remove sample (x) | [FORM] GrooveBox; [SHOT] dock188a crop |
| Hints: "Load sample file... / Sample history", "Locate sample in browser", "Remove sample" | [FORM] |
| Load, left click: the sample file dialog | [H 0xf68f20] |
| Load, right click: a menu built on popup: caption "Sample history", then every path of one global list, by file name; a click loads that sample into the current channel (if it is not already its sample) | [FORM] OpenBtn PopupMenu WAVFileNameListMenu; [H 0x121afb0]; [H 0x123d0b0] |
| The list: one for all channels; a newly loaded path goes first; a path already in it does not move; at most 30 | [H 0x1231030] (called from the sampler's sample loader) |
| Load, mouse wheel: with 2+ paths in the list and the channel's sample among them, wheel down loads the next (older) path, wheel up the previous one | [H 0xf68f50] (PTR 0x15e4840 -> the same list 0x16bc9a0) |
| Locate sample in browser: the sample's path, mapped to the browser, selected there | [H 0xf6c890] |
| Remove sample: only with a sample; one undo step named "remove channel sample"; the sample cleared | [H 0xf667a0] |

### 30b. Envelope tensions, predelay, presets (`chansettings-0056...0059`, `-0077`)

| Fact | Evidence |
|---|---|
| Envelope box: DELAY (predelay, 100...65536, default 100), ATT, HOLD, DEC, SUS, REL; three small tension wheels (-128...128, default 0) between them: Attack tension, Decay tension, Release tension, labelled TENSION; AMT; Tempo | [FORM] EnvPanel |
| The values are the articulator record of the target shown (Panning / Volume / Mod X / Mod Y / Pitch) | [H 0xf63070] (writes param target*0x100 + tag) |
| Presets button: top right of the Envelope box (EnvPanel 260, 2, 22 x 22), down-arrow style, left click, hint "Channel envelope presets" | [FORM] ChanEnvPresetBtn |
| Its menu: caption "Presets", the *.CHENV files of one folder by name, a line (only when there are files), "Save as..." | [FORM] ChanEnvPresetMenu; [H 0xf5d510] |
| Picking one: one undo step "Channel envelope preset change", the file loaded into the shown target | [H 0xf5aba0] -> [H 0xf71560] |
| Save as...: asks "Envelope preset name", writes <name>.CHENV | [H 0xf5acd0] |
| A .CHENV file is the shown target's 68-byte record (17 whole numbers) as kept in memory | [H 0xf71560] (reads / writes 0x44 bytes at target * 0xa0) |
| Folder: FL's data-folder id 0x41d; FL's strings name "Presets\\Channel envelopes\\" (user) and "Data\\Patches\\Channel envelopes\\" (factory); no .CHENV file exists on the study install | [STR 0x5dc440]; disk search |

### 30c. Waveform editing inside the window (`chansettings-0087`)

| Fact | Evidence |
|---|---|
| The Sample tab's waveform is a TMEnvEditor ("WAVEditor", hint "Waveform") over a hidden older scope (WAVScope) | [FORM] |
| In FL 2026 it shows a scroll bar under the waveform and a "16" + icon at its bottom right | [SHOT] dock188a crop |
| What can be edited there (selection, regions, zoom, tools) was not found statically | **live check** |

## Live-check list (not built where it would need a guess)
1. Multilink off: the 17 items FL puts above the caption (copied from which menu?) and the caption naming the last tweaked parameter.
2. Whether FL greys "Flip with spare state" before a Store (LYNX: greyed).
3. Whether FL keeps the Sample history between sessions, and whether loading a project fills it.
4. Whether Remove sample also renames the channel; what the file-name label's own context menu holds.
5. The .CHENV byte order (save one in FL and compare with the .flp 0xDA record order LYNX assumes); FL's real folder for id 0x41d.
6. What the Sample tab's waveform editor lets you edit (and what the "16" box does).
7. Browse parameters / Browse channel states: what the browser shows at nodes -106 and 0x3f7 in FL 2026.
8. Edit events with several parameters: one event-editor window each, or one window with several lanes?
9. Whether the presets button is enabled for every generator that has FL's Envelope / instrument page.
