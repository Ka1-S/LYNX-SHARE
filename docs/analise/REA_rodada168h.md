# REA round 168h — Mixer layouts (issue #52), track icons, default insert names (v0.1.23)

Scope: issue #52 "Implement Mixer Sizes" (FL's Mixer layout drop-down), drawing the icons round 168j imports/saves
(channel 0x9B, insert 0x5F, Playlist track 0xEE+8) with an icon picker, and the coordinator's check from issue #47's
screenshot (strip 8 named "Insert 16"). Clean-room: FL was analysed (FLEngine_x64.dll, headless Ghidra, own copy of
the r167g project) and driven once as a black box; LYNX draws its own art.

## 1. Issue #52 — Mixer layout

| | |
|---|---|
| Reporter | "Implement Mixer Sizes" — FL screenshot of the Mixer title bar drop-down: Compact, Compact 2, Compact alt, Wide, Wide 2, Wide 3, Wide alt, Extra large. "More flexibility". |
| FL evidence | **DFM** TFXForm `FXViewModeSelect: TQuickCombo`, hint `|Mixer layout`, `Align = alLeft` in the Mixer's caption bar (after the menu, multi-touch and waveform buttons), `Items.Strings = ('Compact' 'Compact 2' 'Compact alt' '-' 'Wide' 'Wide 2' 'Wide 3' 'Wide alt' '-' 'Extra large')`, `OnChange = FXViewModeSelectChange` (0x128e190). The index (separators not counted, 0..7) goes to a global (`PTR_DAT_015e0618` → 0x15d47ac) and the layout is rebuilt (FUN_0127fe50 → FUN_01280570). |
| | **Storage**: per user, not per project — FUN_0119fad0 reads/writes it as registry `HKCU\Software\Image-Line\FL Studio 26\General\FruityLoopsMainForm\FXViewMode` (this PC: `3`). Initial value of the global in the image: **3 = Wide** (FL's default). |
| | **Widths** (FUN_01280570, `s` = UI scale, FUN_0040c660 = Round): `base = even(Round(s·(20 + 2·[Wide 3 or XL]) − 0.5) + 1)`; Compact group: `base + 2·[Compact 2]`; others: `2·base + 2·Round(s·(1 + [Wide or Wide alt] + [Wide 3 or XL] + 8·[XL]) − 0.5)` → at 100 %: **20, 22, 20, 44, 42, 48, 44, 64 px**. Bit masks in the function: 0x07 compact group, 0x78 wide group, 7 = Extra large (19 special cases: horizontal parts), 0x44 the alt layouts. |
| | **Alt layouts**: switching into Compact alt / Wide alt clears "Names at top" (`NamesTop`, FUN_0128e190); modes 2 and 6 move the name label (`+0x1c`) into the lower part of the strip (from under the record/route controls down to the strip's bottom, FUN_01280570 tail). |
| | **Black box** (one FL session, 2026-11-03 11:20, demo "Astes - Bien Duro", Wide): strips 44 px apart, header = number + the insert's icon under it, name written bottom-to-top in a column with a thin level meter beside it, mute LED, pan knob, small stereo controls, fader, routing at the bottom. Shot: `/mnt/e/lynx-work/mixer168h/s3.png`. The session ended before the other 7 layouts could be shown (FL was closed on the desktop while the user was using it — see §5). |
| LYNX before | One fixed strip (60 px, Master 76): number + horizontal name, pan + stereo side by side, mute + solo. |
| LYNX now | A **"Mixer layout"** drop-down in the Mixer's title bar (LYNX skin) and in the mixer track menu, with FL's 8 names, order and separators; default **Wide**; kept per user in `ui-options.txt` as `mixerLayout=<FL's index>`. Strip widths = FL's (20/22/20/44/42/48/44/64; the Master is wider for LYNX's dB scale). Every layout but Extra large writes the name up a column of the strip (as FL's), Wide layouts with a thin level meter beside it; the alt layouts put that name column low in the strip; Compact layouts: one knob, mute above solo, thin meters, no dB read-out, arm button under the fader; Wide 2/3 stack pan above stereo; Extra large = horizontal name and knobs side by side. The icon is drawn under the number. Core: `src/core/MixerLayout.h` (names, FL index, widths, spec), UI: `MixerStrips::rows()` + area functions, `MixerLayoutBox`. Tests `mixer168h_layouts_names_order_and_fl_widths`. |
| Left / to check in FL | The exact element arrangement of the 7 other layouts was not seen in FL (only Wide); LYNX's arrangement per layout is an interpretation of the decompiled branches above. Next FL session: switch the drop-down through the 8 items and screenshot each (plan in §5). |

## 2. Track icons (channels, inserts, Playlist tracks)

| | |
|---|---|
| Ask | Draw the icons 168j imports/saves where FL shows them, with an icon picker; LYNX's own icon set, FL's indices mapped to the closest. |
| FL evidence | **Index = glyph**: FL's icon index i is the glyph U+F400 + i of its icon font ILGlyphsEx (glyph names in `Help/fonts/ILGlyphsEx.svg`: F401 Dry_guitar, F402 Electric_quitar, F403 Violin, F404 Snare_drum, F405 Kick_drum, F406 Cymbal … F484 Clap, F485 Cloud, F4ED Keys, F4EF Hi_Hat). Cross-check on the 165 FL demo projects (3 937 non-zero icons, `tmp/r168h/icons.py`): index 5 on "kick" ×42, 4 on "snare" ×35, 239 on "hi-hats" ×27, 20 on "vocals" ×35, 3 on "strings" ×40, 14 on "sub/bass", 59..67 on "fx 1".."fx 9", 93 on "sidechain", 116 on "master". |
| | **Where FL shows them** (shot s3): the mixer strip header under the number; the Channel Rack's channel button, before the name; the Playlist track header, before the name. Menus: mixer "Change icon..." (`SetIconMenuClick` 0x1295570 → FUN_012be9b0 mode 0x202: the name/colour/icon dialog, applied to every selected track), channel "Change icon" (`SetPlugIconMenuClick`), Playlist track (`EEPLTrackIconMenuClick`); the rename dialog's icon button (F5) opens a popup menu of icons whose items carry the index as Tag (IconMenuPopup 0x7bc870). |
| LYNX before | `icon` kept in the model and .lynx (168j), never drawn. |
| LYNX now | 78 original LYNX line icons (`src/ui/TrackIcons.cpp`, the 24×24 grid and stroke of ui/Icons): drums, instruments, voices, audio, effects/routing, numbers 1–9, misc. `core/TrackIcons.h`: each LYNX icon carries the FL index it saves; FL indices LYNX has no icon for show the closest one (pan flute → flute, gong → cymbal, banjo → acoustic guitar …) or a neutral label, and keep their FL index until the user picks another. Drawn in the mixer header, the Channel Rack name button and the Playlist header. "Change icon..." in the mixer track menu (all selected tracks, as FL), the channel menu and the Playlist track menu opens a grid picker (current icon marked, hovered name shown). Tests `mixer168h_track_icons_map_fl_indices`. |
| Left | The rename dialog has no icon button yet (FL's F5); the picker is reached from the menus. |

## 3. Issue #47 screenshot — "Insert 16" on strip 8

| | |
|---|---|
| Observation | Strip 8 named "Insert 16", strip 9 "Insert 8": insert 16 had been moved left 8 times (Alt+Left / drag). |
| FL | An unnamed mixer track shows "Insert <its number>" (the name is empty in the .flp: FL's saver writes 0xCC only for named tracks, REA 168j), so the default name follows the position. |
| LYNX before | Default names are stored as "Insert N"; `swapTracks` swapped them with the track (delete/insert already renumbered). |
| LYNX now | `mixerops::swapTracks` renames a default name to the new position (a user's name stays with its track). Test `mixer168h_default_insert_names_follow_the_position`. |

## 4. Files

`src/core/MixerLayout.h`, `src/core/TrackIcons.h` (header-only), `src/ui/TrackIcons.{h,cpp}`, `src/ui/MixerComponent.{h,cpp}`,
`src/ui/MainComponent.cpp` (title bar accessory, ui-options), `src/ui/ChannelRackComponent.cpp`, `src/ui/PlaylistComponent.cpp`,
`src/core/MixerOps.cpp`, `tests/Mixer168hTests.inc`, i18n (36 keys × 15 languages).

## 5. FL session note and next FL check

The single FL session (locks taken, FL launched once, PID 16648) loaded the demo and took the Wide shot; then the user's
browser came to the front and FL was closed from the desktop (not by this round). The round stopped driving the mouse
at once, released the locks, checked that `FXViewMode` was still 3 and that no FL process or dialog was left. Next check
(one session, when the desktop is free): open any project, click the Mixer's layout drop-down and pick each of the 8
items, one screenshot each; compare with LYNX's `--snapshot` of the mixer in each layout.

## Checks

- build-local: 2 408 795 checks, 0 failures (the 3 new tests included).
- Windows build + `LYNX.exe --snapshot` (fresh LYNX_PROFILE_DIR, LYNX_TEST_MODE=1, off-screen: the user was on the
  desktop), FL demo "Astes - Bien Duro" imported: the 8 layouts `/mnt/e/lynx-work/mixer168h/shots/s-mixer168h-0..7.png`
  (icons in the headers, names up the strips, alt names low, Extra large horizontal) and the picker
  `s-trackicons168h.png`. Looked at: Compact, Wide, Wide alt, Extra large, the picker.

## Where I stopped

- Done: core + UI + tests + i18n + snapshots on feature/mixer168h; queued for landing with train.sh.
- Open: the FL screenshots of the 7 other layouts (one FL session, §5); an icon button in the rename dialog.
