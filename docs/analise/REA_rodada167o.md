# Round 167o — issues #22 (SFZ), #23 (F-keys), #24 (preset arrows)

Clean-room: Ghidra (headless, the analysed FLEngine project of round 167g) and file reads on FL Studio 2026's own
binaries (`FLEngine_x64.dll` SHA-256 6d9c5ab0…, `DirectWave_x64.dll`, FLEX `engineV2_x64.dll`), and one driven FL
session (FL 26.1.7 build 5654, 2026-11-01 17:12–17:16, real keys and mouse, shots in `/mnt/e/lynx-work/r167o/`).
Markers: **[DECOMP addr]** Ghidra pseudocode, **[DFM]** a form resource, **[FL shot]** a screenshot of FL driven by
real input, **[STR]** a string in FL's binary, **[TEST]** a LYNX unit test.

## #23 (vespadj) "Key F1, F2, F4, F8, F10, F11 usability"

Reporter: "Many F1...F12 keys works to toggle windows or widget. Key F1, F2, F4, F8, F10, F11 not toggle windows.
F12 close Settings but F10 and F11 should be to the same. F1 opens Shortcuts but not reporting F1..F12 keys."

FL's F-key table (DFM `ShortcutsModule` action lists, round 167j `sc-fl.txt`): F5 Playlist, F6 Channel rack, F7 Piano
roll, F8 Plugin picker, Alt+F8 Browser, Shift+F8 Plugin database, Ctrl+F8 Project picker, F9 Mixer, F10 MIDI
settings, F11 Project info, Ctrl+F11 hide toolbars, F12 Close all windows, Alt+F12 Close all plugin windows,
Ctrl+F12 Close all unfocused windows, Shift+F12 Align all channel editors, Ctrl+F5 Tempo tapper, Alt+F7 Touch
controller, Alt+F1 Gopher; Patterns menu F2 rename, F4 / Shift+F4 / Ctrl+F4 next / first empty / next empty without
naming. No action has F1 (FL's F1 is Windows' context help, `Help/HelpAliases.scr`).

| key | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| F10 / F11 | [DECOMP 0xf1e5c0 / 0xf1e600 → 0x13545f0] `ShowSettings(page, toggle=1)`: if the Settings form is visible AND its page is the asked one → Hide; else switch the page and show. [FL shot k10a/k10b/k11a/k1011/k11b/k11c]: F10 opens MIDI, F10 closes; F11 opens Info, F10 switches the same window to MIDI, F11 back to Info, F11 closes | always (re)opened Settings on the page; never closed | FL's rule (`keys::settingsKeyPress`), the window switches page in place |
| F8 | [DECOMP 0xf1c240 → 0xebf510] plugin picker form not created → create + show, else close it. [FL shot k8a/k8b] F8 opens, F8 closes | opened the Quick-add palette again | toggles |
| F12 | [DECOMP 0xf18ad0 → 0x121a690] closes every form in FL's window lists. [FL shot k12pre/k12a] closes Settings and every window | closed plugin, channel, automation and tool windows (Settings too) | + the shortcut sheet |
| F2 / F4 | [FL shot k2a/k2b, k4a/k4b, rb1–rb3]: no toggle. F2 twice opens a second rename box on top of the first; F4 twice makes two new patterns, each with its naming box (four boxes stacked, closed one by one) | F2 opens Rename, F4 next empty + Rename | unchanged (FL's behaviour); explained in the reply |
| F1 | none in FL (context help) — LYNX's own shortcut sheet | opened the sheet; F1 again opened nothing new; the sheet had no F-key list (and said F8 = Browser) | F1 toggles the sheet (LYNX-own); the sheet has a "Function keys" column built from the live key map (custom keys show) |
| Ctrl+F12 / Alt+F7 | FL `CloseAllUnfocusedWindowsAction`, `ShowTouchControllerAction` | missing | Ctrl+F12 closes every window but the focused one; Alt+F7 shows / hides the Touch keyboard |

Not done: Shift+F12 (align channel editors), Ctrl+F8 (project picker: LYNX has no project picker yet).

## #24 (matdinu) "preset arrows"

Reporter: "be able to cycle through a plugin's presets using arrows (previous preset / next preset)".

FL evidence:
- [DFM] `PluginForm/NewCaption/PrevPresetBtn` (Tag −1) and `NextPresetBtn` (Tag 1), both `OnClick = PrevPresetBtnClick`
  (0xf5a610), which posts message 0x54D with the tag. TPluginForm's DMT (0xf58d88) maps 0x54D → 0xf5a640 →
  **0x12b6c70 `NextPrevPreset(delta)`** [DECOMP]:
  - list = the plugin's own programs (count of the program list at +0x94) followed by the `.fst` files of the plugin's
    preset folder: `Plugin presets\<Generators|Effects>\<folder>\` (0x12ba230), scanned through the Browser's
    "Plugin presets" place (0x3f3, mask `.FST`, recursive) so FL's factory folder and the user's folder are merged;
  - sorted by 0x662a40: folder part first, files of the folder itself before its subfolders, then by name with
    `CompareStringW(…, SORT_DIGITSASNUMBERS)` (natural numbers);
  - no current preset (index < 0): ▶ goes to the first, ◀ to the last; then `index = (index + delta + n) mod n` (wraps);
  - the hint bar shows "<index+1> / <n>", the step is an undo point "next/previous preset";
  - 0xf5c950: the mouse's back / forward side buttons (mbExtra1 / mbExtra2) on the window caption do ◀ / ▶.
- [FL shot pn1–pn5, pq1–pq6] Ogun (229 factory presets: 3 in the folder, 8 subfolders): ▶ from a fresh channel:
  "Default" 1/229, "Default (pad)" 2/229, "Default (tonal)" 3/229, "300 Ft Glass Bowl" (Atmospheric) 4/229, "Alien
  Queens Lair" 5/229; ◀ back to 1/229, ◀ again wraps to "Waterphone" (SFX, the last) 229/229, ▶ wraps to "Default"
  1/229. The preset's name shows between the arrows and in the window title, and the channel takes the preset's
  name (rack shot `rack.png`: channel "Default").

| window | LYNX before | LYNX now |
|---|---|---|
| FL native generators on a channel (Ogun, Sytrus, Harmor, 3x Osc… 30 engines), MIDI Out, Wave Traveller | no arrows (presets only in menus, some none) | ◀ name ▶ in the channel window header, FL's list / order / wrap, "n / N" hint, channel renamed, mouse side buttons |
| hosted VST / VST3 / CLAP | ◀ ▶ (programs then LYNX presets) | unchanged order (programs first, as FL) |
| mixer-slot FL effects | ◀ ▶ over FL's .fst presets (natural sort on the full path) | FL's order (folder files first, subfolders after) |

## #22 (Reaper10) "sfz support"

Reporter: "https://sfzformat.com/ — would with muri sampler".

FL evidence [STR]: `DirectWave_x64.dll` lists `.sfz` among its import formats (with .sf2 .akp .gig .exs .nki .pgm
.krz…); FLEX's `engineV2_x64.dll` has an SFZ parser (`SFZGroup`, `SFZRegion`, "SFZ File Summary: Total regions /
Groups / Standalone regions"); `FLEngine_x64.dll` has "Export kit as SFZ file...". FL's Browser lists no `.sfz`
(no such string in FLEngine's file filters). So FL loads SFZ instruments through DirectWave.

LYNX (no DirectWave clone: an .sfz opens in LYNX's own sampler-player, the LYNX Soundfont channel):
- `core/Sfz.{h,cpp}` reads the file into a one-preset `sf::Font` (headers `<control> <global> <master> <group>
  <region>` with their inheritance, comments, `#define` / `#include`, `default_path`, `note_offset`, `octave_offset`,
  note names c4 = 60); the samples are WAV (LYNX's reader, the `smpl` loop kept) or FLAC / OGG / AIFF / MP3 through
  JUCE's readers (`sfz::setDecoder`, installed by the app). Stereo samples become a left / right region pair.
- the voice engine (`core/SoundFont.cpp`) gained what SF2 has no generator for: `trigger=release`, `loop_mode=one_shot`,
  `group` / `off_by` (choke), `seq_length` / `seq_position` (round robin), `lorand` / `hirand`, key switches
  (`sw_lokey`/`sw_hikey`/`sw_last`/`sw_default`), `amp_veltrack`.
- `sf::loadFontFile` and `sf::scanFontFile` dispatch `.sfz`, so the Browser (lists `.sfz`, preview, double-click,
  Enter, menu, drag to the Channel Rack), the Channel Rack file drop and the Soundfont editor's Load dialog take it.
- [TEST] issue22_sfz_parse_headers_defines_and_note_names, issue22_sfz_loads_and_plays_at_the_regions_pitch,
  issue22_sfz_round_robin_random_and_key_switches, issue22_sfz_release_trigger_choke_groups_and_loops.
- Not yet: filters other than low-pass (hpf / bpf), `xfin` / `xfout` crossfades, CC-driven opcodes (`*_oncc`),
  generated waves (`*sine`), `<curve>` / `<effect>`.

## LYNX check on Windows (2026-11-01, fresh test profile, LYNX_TEST_MODE)

F1 opens the sheet with its "Function keys" column, F1 closes it; F10 → MIDI, F11 switches to Info in place, F11
closes; F8 opens / closes the Quick-add palette; Alt+F7 shows / hides the Touch keyboard. Ogun: ▶ from a fresh channel
→ "300 Ft Glass Bowl 4 / 229" (FL's three "Default" presets are Ogun state version 12, which LYNX's Ogun doesn't read:
passed over), ▶ "Alien Queens Lair 5 / 229" with the knobs, window title and rack name updated; ◀ ◀ wraps to
"Waterphone 229 / 229". LYNX Soundfont › Load (filter *.sf2;*.sf3;*.sfz) → "Test Keys (Test Keys.sfz)", one preset.
Known gap: about 95 of FL's 229 Ogun presets are state versions < 14 (LYNX's Ogun engine reads 14+).

## Where I stopped

- 2026-11-01 20:00: all done; tests 0 failures (Linux, FL Data); landing feature/issues167o.
