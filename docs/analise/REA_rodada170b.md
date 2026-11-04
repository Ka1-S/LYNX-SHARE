# Round 170b — mixer multi-selection routing, FX slot places, slot click, categorized plugin menus

Source: Busy Works Beats, "Making a Beat on the 'Open Source' FL STUDIO" (2026-11-03, macOS, trackpad, LYNX 0.1.22/23),
items #10, #12, #13 and #16 of coord/busyworks-0.1.24.md. FL evidence: Ghidra on FL Studio 2026's own FL64.exe
(Delphi x64; decompiled excerpts kept outside the repo in the round's scratch folder `r170b/`, clean-room: only the
behaviour is written here). TShiftState bits: ssShift = 1, ssAlt = 2, ssCtrl = 4, ssLeft = 8, ssMiddle = 0x20,
ssDouble = 0x40. FL's mixer track struct: stride 0x1478, "selected" flag at +0x19, up to 501 tracks
(FUN_012c19c0 = "selected or current").

## #10 Mixer multi-select + routing all selected tracks

| | |
|---|---|
| Reporter (14:47) | "no drag / Ctrl multi-select of tracks"; "Route to this track only" only routed ONE track — FL routes all selected tracks |
| FL evidence | **Selection** — mixer MouseDown FUN_012c5800 / MouseMove FUN_012c5f60: Ctrl+click (or a double click) clears the selection and selects the range from the anchor (the clicked track) to the track under the mouse while dragging; Ctrl+Shift adds the range (or removes it when the click started on an already selected track); Ctrl+double click selects the group; a plain drag (state 2) only moves the current track. **Route arrow** — left click FUN_012aa110: no modifier = for EVERY selected track ≠ the target, set its route to the arrow's new state; Ctrl = RouteToOnlyMenu's action (field +0x870), Shift = sidechain (+0x8d8), Ctrl+Shift = sidechain only (+0x8e0). **Arrow menu** — `RouteToOnlyMenuClick` FUN_012aca70 (tags 0–3: route only / sidechain / sidechain only / route): loops over all selected tracks; the "only" tags clear the track's other routes first; the level is unity (0x3200) for routes, 0 for sidechains. |
| LYNX before | Ctrl+click toggled one track; no drag selection. The arrow and its menu acted on the current track only. |
| LYNX now | Ctrl+click / Ctrl+drag across strips selects the range (anchor → mouse), Ctrl+Shift adds or removes the range (FL), on the strip body, header and meter. The arrow: plain click switches the route of every selected insert (`mixerops::switchSelectedRoute`, a new route at unity, cycles skipped), Ctrl / Shift / Ctrl+Shift = route only / sidechain / sidechain only for all of them; the right-click menu's items act on all the selected tracks (header "N selected tracks"). Ctrl = Cmd on macOS. LYNX extras kept: Shift+click range, header drag sideways reorders. Tests: `mixfx170b_route_arrow_acts_on_every_selected_track`. |

## #12 FX slot reorder (drag, Move up / down, wheel; macOS trackpad)

| | |
|---|---|
| Reporter (16:34) | status bar says drag / wheel reorders, but he could not move the compressor down |
| FL evidence | FL's 10 slots are places, not a packed list. Move FUN_01233b30(target, undo "move effect plugin"): target = (slot ± 1) mod 10, wrapping, swapping with whatever is in that slot — an empty slot included. The plugin wrapper menu's "Move up" / "Move down" (FUN_01233de0 / FUN_01235190) call it. Mixer wheel FUN_012ab750: the plain wheel over the slot list moves the plugin under the mouse by −sign(delta) (wheel up = up) and moves the cursor with it (SetCursorPos); Shift+wheel elsewhere moves tracks. Drag & drop onto another slot swaps. |
| LYNX before | A compact vector: with one effect (his case) the drag, the wheel and Move down had nowhere to go; a trackpad's wheel flood (and its inertia) moved several slots or nothing. |
| LYNX now | `EffectSlot::row` (saved as "row" only when not packed, imported from the .flp's slot index): slots are places as in FL. Drag onto any slot (empty: moves there, taken: swaps), Move up / Move down always enabled and wrapping 1↔10, picking an effect on an empty slot puts it in that slot, deleting leaves the slot empty. Automations follow (reordering goes through `Song::swapEffectSlots`). Wheel: `controls::slotWheelSteps` — a mouse notch = one slot; a trackpad's smooth deltas add up to one slot per 0.3, its inertial glide is ignored, a direction change restarts, honours "natural scrolling" (isReversed); the mouse follows the slot. Tests: `mixfx170b_effect_rows_*`, `mixfx170b_moving_onto_an_effect_swaps_and_automations_follow`, `mixfx170b_trackpad_wheel_steps_add_up_and_ignore_inertia`. |

## #13 Single click on an FX slot opens its window

| | |
|---|---|
| Reporter (17:28) | wants one click on the slot to open the plugin window instead of clicking "Editor" |
| FL evidence | The slot name button (TPlugNameBtn, VMT 0x12c13b8), click handler FUN_012bde10 (assigned in FUN_0127a4e0): plain click = plugin vmt+0xe0(−1), toggling the editor window; Shift or middle click = vmt+0x108, the slot menu at the mouse. The slot's arrow FUN_012969f0: left = plugin picker FUN_00ebf510(0,1,…), right = options menu. Hint "FX slot %d options". |
| LYNX now | A click (no drag) on a slot shows or hides its window (plugins: PluginWindows; LYNX / FL-style effects: their editor, closed through the `SlotGoneWatcher` registry); Shift+click = slot menu (FL); the quick-param strip below keeps showing the selected slot's knobs. Patcher windows only open (they do not use the watcher). |

## #16 FX picker order

| | |
|---|---|
| Reporter (20:05) | "random order" — LYNX / Fruity / FL mixed; must be alphabetical or grouped by category |
| FL evidence | FL's Plugin database folders (FL install, Data/Patches/Plugin database/Effects and Generators: Controller, Delay reverb, Distortion, Dynamics, Filter, Flanger, Gain, Misc, Patcher, Visual / Controller, Drum, MIDI, Misc, Patcher, Sampler, Synth classic, Synth special, Visual) and REA_rodada166f §1: the slot menu shows a heading per folder, A-Z, with the Categories / Simple / Tree radio items. |
| LYNX before | Built-ins not in a user folder were listed in enum order (the "random" order), in both the mixer menus and the Channel Rack's Add menu. |
| LYNX now | `plugincatalog::withDefaultCategories`: every built-in sits in FL's category for it (`flCategory`, names matched with or without "Fruity "; FL legacy effects placed by LYNX: Fruity Delay / Reeverb → Delay reverb, 7 Band EQ / Bass Boost / Fast LP / Free Filter → Filter, Center / Mute 2 / Phase Inverter → Gain), FL plugins FL files nowhere → Misc, LYNX's own → **LYNX SHARE** (first); the user's folders win. Sorted A-Z inside; the view radio items always shown. Same tree in the mixer's empty-slot menu, "Replace with" and the Channel Rack Add menu. Installed VST/AU plugins stay in their own Plugin submenu. Tests: `mixfx170b_plugin_menus_group_by_fl_category_lynx_first`. |

## Issue #64 (follow-up branch feature/typeahead170b) — letter keys in the plugin menus

| | |
|---|---|
| Reporter (Jocho-Smith, Linux, 0.1.23) | "in FL Studio if one clicks a slot [...] press a letter to search through the plugins. Pressing 'e' once will jump to (but not select yet) the 'EQ' plugin. Pressing 'e' again will then jump to the next plugin starting with e [...] Pressing Enter would then select the plugin" |
| FL evidence | The reporter's description of FL (an FL user); FL's own menu key code was not traced this round. It is also the Windows menu convention FL's menus follow (a letter moves through the items starting with it, per menu level). Not taken from FL: Windows runs an item at once when it is the ONLY one starting with the letter; LYNX only highlights it (as the issue asks: nothing is picked before Enter). |
| LYNX before | Only a renamed plugin's first character did something (issue #11: it picks that plugin); other letters did nothing. 0.1.23's effect list was flat in 3 columns (screenshot i64-2.png). |
| LYNX now | In the mixer slot's plugin menu, its slot menu (Replace with ›) and the Channel Rack's Add menu (`quickpick::showMenu`): a letter or digit moves the highlight to the next item starting with it, in reading order across the category headings, wrapping, skipping headings and unavailable items (`prefs::nextTypeAhead`); again = the next one; from an item the mouse or the arrows highlighted, the next one after it. In an open submenu (Tree view, Plugin ›, Replace with ›) the letters search that submenu. Enter picks, arrows / wheel scroll, Esc closes (JUCE's). The renamed-plugin shortcut keeps priority. The highlight goes through JUCE's own accessibility actions of the menu items (focus = highlight + scroll into view), so it works the same on Windows, macOS and Linux without patching JUCE. Tests: `typeahead170b_letters_step_through_the_items_starting_with_them`; `LYNX --typeaheadtest out.txt` drives a real menu + submenu with OS-level key presses (Windows: result=OK, 14 checks). |

## Verification
- Tests: tests/Mixfx170bTests.inc (6 tests), build-local 1963936 checks, 0 failures.
- Windows `--snapshot` (fresh LYNX_PROFILE_DIR, LYNX_TEST_MODE=1): `s-mixer170b.png` (inserts 1-3 selected, all routed
  to insert 4 by one switchSelectedRoute, the lone compressor moved down into slot 2 with slot 1 empty) and
  `s-menus170b.png` (the mixer slot's effect menu and the Channel Rack's Add menu: LYNX SHARE first, then FL's
  categories A-Z, Categories / Simple / Tree at the bottom). Kept in C:\Users\kauan\lynx-test\mixfx170b\shots.
- Not driven with real mouse input on Windows: Ctrl+drag selection, the slot click toggle and the wheel (their logic is
  covered by the tests; the input paths are JUCE mouse events, Ctrl = Cmd on macOS).

## Where I stopped
- Done: code, tests, translations, Windows build, snapshots. Next: merge train `coord/train.sh mixfx170b`, notes.
